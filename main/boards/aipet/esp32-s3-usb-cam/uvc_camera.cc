#include "uvc_camera.h"
#include "config.h"

#include "audio_codec.h"
#include "board.h"
#include "system_info.h"

#include <cstddef>
#include <cstring>
#include <string>

#include "image_to_jpeg.h"
#include "jpeg_to_image.h"

#include <esp_heap_caps.h>
#include <esp_log.h>
#include <freertos/task.h>
#include <usb/usb_host.h>

#define TAG "UvcCamera"
#define USB_HOST_TASK_PRIO 15
#define FRAME_WAIT_MS 5000
#define OPEN_WAIT_MS 8000
#define BURST_FRAMES 3
#define BURST_SCAN_FRAMES 12

UvcCamera::UvcCamera() {
    ESP_LOGI(TAG, "U4 UVC camera D-=GPIO%d D+=GPIO%d", USB_DMINUS_GPIO, USB_DPLUS_GPIO);
    StartHost();
}

bool UvcCamera::StartHost() {
    return EnsureHost();
}

UvcCamera::~UvcCamera() {
    if (frame_q_) {
        vQueueDelete(frame_q_);
        frame_q_ = nullptr;
    }
}

void UvcCamera::SetExplainUrl(const std::string& url, const std::string& token) {
    std::lock_guard<std::mutex> lock(mutex_);
    explain_url_ = url;
    explain_token_ = token;
    ESP_LOGI(TAG, "explain url set, len=%u token=%d",
             static_cast<unsigned>(url.size()), token.empty() ? 0 : 1);
}

bool UvcCamera::SetHMirror(bool enabled) {
    (void)enabled;
    return false;
}

bool UvcCamera::SetVFlip(bool enabled) {
    (void)enabled;
    return false;
}

bool UvcCamera::FrameCallback(const uvc_host_frame_t* frame, void* user_ctx) {
    auto self = static_cast<UvcCamera*>(user_ctx);
    if (xQueueSendToBack(self->frame_q_, &frame, 0) != pdPASS) {
        return true;
    }
    return false;
}

void UvcCamera::StreamCallback(const uvc_host_stream_event_data_t* event, void* user_ctx) {
    auto self = static_cast<UvcCamera*>(user_ctx);
    switch (event->type) {
        case UVC_HOST_TRANSFER_ERROR:
            ESP_LOGE(TAG, "USB transfer error err=%d", event->transfer_error.error);
            break;
        case UVC_HOST_DEVICE_DISCONNECTED:
            self->disconnected_ = true;
            ESP_LOGW(TAG, "UVC device disconnected");
            uvc_host_stream_close(event->device_disconnected.stream_hdl);
            break;
        default:
            break;
    }
}

void UvcCamera::UsbLibTask(void* arg) {
    (void)arg;
    while (true) {
        uint32_t flags = 0;
        usb_host_lib_handle_events(portMAX_DELAY, &flags);
        if (flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) {
            usb_host_device_free_all();
        }
    }
}

bool UvcCamera::EnsureHost() {
    if (host_ready_) {
        return true;
    }

    const usb_host_config_t host_config = {
        .skip_phy_setup = false,
        .root_port_unpowered = false,
        .intr_flags = ESP_INTR_FLAG_LEVEL1,
    };
    esp_err_t err = usb_host_install(&host_config);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "usb_host_install: %s (USB IRQ must be taken before I2S/WiFi)",
                 esp_err_to_name(err));
        return false;
    }
    ESP_LOGI(TAG, "USB Host installed early, internal PHY");

    if (xTaskCreatePinnedToCore(UsbLibTask, "usb_lib", 4096, nullptr,
                                USB_HOST_TASK_PRIO, nullptr, tskNO_AFFINITY) != pdPASS) {
        ESP_LOGE(TAG, "usb_lib task create failed");
        return false;
    }

    const uvc_host_driver_config_t uvc_cfg = {
        .driver_task_stack_size = 4 * 1024,
        .driver_task_priority = USB_HOST_TASK_PRIO + 1,
        .xCoreID = tskNO_AFFINITY,
        .create_background_task = true,
    };
    err = uvc_host_install(&uvc_cfg);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "uvc_host_install: %s", esp_err_to_name(err));
        return false;
    }

    frame_q_ = xQueueCreate(6, sizeof(uvc_host_frame_t*));
    if (!frame_q_) {
        return false;
    }
    host_ready_ = true;
    return true;
}

bool UvcCamera::ExtractJpeg(const uint8_t* data, size_t len, std::vector<uint8_t>& out) {
    if (!data || len < 8) {
        return false;
    }
    size_t start = SIZE_MAX;
    for (size_t i = 0; i + 2 < len; ++i) {
        if (data[i] == 0xFF && data[i + 1] == 0xD8 && data[i + 2] == 0xFF) {
            start = i;
            break;
        }
    }
    if (start == SIZE_MAX) {
        return false;
    }
    size_t end = SIZE_MAX;
    for (size_t i = len; i >= start + 4; --i) {
        if (data[i - 2] == 0xFF && data[i - 1] == 0xD9) {
            end = i;
            break;
        }
    }
    if (end == SIZE_MAX || end - start < 4096) {
        return false;
    }
    out.assign(data + start, data + end);
    return true;
}

void UvcCamera::PauseMic(bool pause) {
    auto codec = Board::GetInstance().GetAudioCodec();
    if (codec) {
        codec->EnableInput(!pause);
    }
}

void UvcCamera::StripThinkTags(std::string& text) {
    while (true) {
        const auto start = text.find("<think>");
        const auto end = text.find("</think>");
        if (start == std::string::npos || end == std::string::npos || end < start) {
            break;
        }
        text.erase(start, end + 8 - start);
    }
    size_t i = 0;
    while (i < text.size() && (text[i] == ' ' || text[i] == '\n' || text[i] == '\r' || text[i] == '\t')) {
        ++i;
    }
    if (i > 0) {
        text.erase(0, i);
    }
}

static void BlitRotatedRgb565(const uint8_t* src, size_t sw, size_t sh, size_t stride,
                              uint8_t* dst, size_t dw, int rotate) {
    for (size_t y = 0; y < (rotate == 0 ? sh : sw); ++y) {
        for (size_t x = 0; x < (rotate == 0 ? sw : sh); ++x) {
            size_t sx = x;
            size_t sy = y;
            if (rotate == 1) {
                sx = y;
                sy = sh - 1 - x;
            } else if (rotate == 3) {
                sx = sw - 1 - y;
                sy = x;
            }
            memcpy(dst + (y * dw + x) * 2, src + sy * stride + sx * 2, 2);
        }
    }
}

bool UvcCamera::ComposeBurstJpeg(const std::vector<std::vector<uint8_t>>& frames,
                                 std::vector<uint8_t>& out, uint16_t* out_w, uint16_t* out_h) {
    if (frames.empty()) {
        return false;
    }

    const int rotate = CAMERA_ROTATE_90;
    size_t tile_w = 0;
    size_t tile_h = 0;
    uint8_t* canvas = nullptr;
    size_t canvas_w = 0;
    size_t canvas_h = 0;

    for (size_t i = 0; i < frames.size(); ++i) {
        uint8_t* rgb = nullptr;
        size_t rgb_len = 0;
        size_t w = 0;
        size_t h = 0;
        size_t stride = 0;
        if (jpeg_to_image(frames[i].data(), frames[i].size(), &rgb, &rgb_len, &w, &h, &stride) != ESP_OK ||
            !rgb || w == 0 || h == 0) {
            if (rgb) {
                heap_caps_free(rgb);
            }
            if (canvas) {
                heap_caps_free(canvas);
            }
            return false;
        }

        const size_t rw = (rotate == 0) ? w : h;
        const size_t rh = (rotate == 0) ? h : w;
        if (i == 0) {
            tile_w = rw;
            tile_h = rh;
            canvas_w = tile_w;
            canvas_h = tile_h * frames.size();
            canvas = static_cast<uint8_t*>(
                heap_caps_malloc(canvas_w * canvas_h * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
            if (!canvas) {
                heap_caps_free(rgb);
                return false;
            }
        } else if (rw != tile_w || rh != tile_h) {
            ESP_LOGW(TAG, "burst frame %u size mismatch", static_cast<unsigned>(i));
            heap_caps_free(rgb);
            heap_caps_free(canvas);
            return false;
        }

        BlitRotatedRgb565(rgb, w, h, stride, canvas + i * tile_h * canvas_w * 2, canvas_w, rotate);
        heap_caps_free(rgb);
    }

    uint8_t* jpg = nullptr;
    size_t jpg_len = 0;
    const bool ok = image_to_jpeg(canvas, canvas_w * canvas_h * 2,
                                  static_cast<uint16_t>(canvas_w),
                                  static_cast<uint16_t>(canvas_h),
                                  V4L2_PIX_FMT_RGB565, 80, &jpg, &jpg_len);
    heap_caps_free(canvas);
    if (!ok || !jpg || jpg_len == 0) {
        if (jpg) {
            heap_caps_free(jpg);
        }
        return false;
    }
    out.assign(jpg, jpg + jpg_len);
    heap_caps_free(jpg);
    if (out_w) {
        *out_w = static_cast<uint16_t>(canvas_w);
    }
    if (out_h) {
        *out_h = static_cast<uint16_t>(canvas_h);
    }
    return true;
}

bool UvcCamera::CaptureFormat(uint16_t w, uint16_t h, float fps) {
    uvc_host_stream_config_t cfg = {};
    cfg.event_cb = StreamCallback;
    cfg.frame_cb = FrameCallback;
    cfg.user_ctx = this;
    cfg.usb.vid = UVC_HOST_ANY_VID;
    cfg.usb.pid = UVC_HOST_ANY_PID;
    cfg.usb.uvc_stream_index = 0;
    cfg.vs_format.h_res = w;
    cfg.vs_format.v_res = h;
    cfg.vs_format.fps = fps;
    cfg.vs_format.format = UVC_VS_FORMAT_MJPEG;
    cfg.advanced.frame_size = 0;
    cfg.advanced.number_of_frame_buffers = 3;
    cfg.advanced.number_of_urbs = 3;
    cfg.advanced.urb_size = 10 * 1024;
    cfg.advanced.frame_heap_caps = MALLOC_CAP_SPIRAM;

    uvc_host_stream_hdl_t stream = nullptr;
    disconnected_ = false;
    esp_err_t err = uvc_host_stream_open(&cfg, pdMS_TO_TICKS(OPEN_WAIT_MS), &stream);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "open %ux%u failed: %s", w, h, esp_err_to_name(err));
        return false;
    }

    // 重新挂上 disconnect 回调的 this
    // stream 已打开；用独立成员 disconnected_
    if (uvc_host_stream_start(stream) != ESP_OK) {
        uvc_host_stream_close(stream);
        return false;
    }

    std::vector<std::vector<uint8_t>> burst;
    burst.reserve(BURST_FRAMES);
    for (int i = 0; i < BURST_SCAN_FRAMES && !disconnected_ && burst.size() < BURST_FRAMES; ++i) {
        uvc_host_frame_t* frame = nullptr;
        if (xQueueReceive(frame_q_, &frame, pdMS_TO_TICKS(FRAME_WAIT_MS)) != pdPASS) {
            break;
        }
        std::vector<uint8_t> one;
        const bool have_jpeg = frame->data &&
                               ExtractJpeg(static_cast<const uint8_t*>(frame->data),
                                           frame->data_len, one);
        uvc_host_frame_return(stream, frame);
        if (have_jpeg) {
            burst.push_back(std::move(one));
        }
    }

    if (!disconnected_) {
        uvc_host_stream_stop(stream);
        uvc_host_stream_close(stream);
    }
    if (burst.empty()) {
        ESP_LOGW(TAG, "no complete JPEG at %ux%u", w, h);
        return false;
    }

    uint16_t composed_w = w;
    uint16_t composed_h = h;
    std::vector<uint8_t> composed;
    int used = static_cast<int>(burst.size());
    if (!ComposeBurstJpeg(burst, composed, &composed_w, &composed_h)) {
        size_t best_i = 0;
        for (size_t i = 1; i < burst.size(); ++i) {
            if (burst[i].size() > burst[best_i].size()) {
                best_i = i;
            }
        }
        std::vector<std::vector<uint8_t>> one{std::move(burst[best_i])};
        if (!ComposeBurstJpeg(one, composed, &composed_w, &composed_h)) {
            ESP_LOGW(TAG, "burst stitch failed at %ux%u", w, h);
            return false;
        }
        used = 1;
        ESP_LOGW(TAG, "burst stitch failed, fallback 1 frame rotated");
    }
    jpeg_.swap(composed);
    burst_count_ = used;
    width_ = composed_w;
    height_ = composed_h;
    ESP_LOGI(TAG, "burst %d frames -> %ux%u jpeg %u bytes", burst_count_, width_, height_,
             static_cast<unsigned>(jpeg_.size()));
    return true;
}

bool UvcCamera::Capture() {
    std::lock_guard<std::mutex> lock(mutex_);
    burst_count_ = 0;
    jpeg_.clear();
    PauseMic(true);
    if (!EnsureHost()) {
        PauseMic(false);
        return false;
    }

    usb_host_lib_info_t info = {};
    int waited = 0;
    while (waited <= 3000) {
        if (usb_host_lib_info(&info) == ESP_OK && info.num_devices > 0) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
        waited += 100;
    }
    if (info.num_devices <= 0) {
        ESP_LOGE(TAG, "no USB device on U4");
        PauseMic(false);
        return false;
    }

    static const struct {
        uint16_t w;
        uint16_t h;
        float fps;
    } formats[] = {
        {640, 480, 15},
        {640, 480, 0},
        {320, 240, 15},
        {160, 120, 15},
    };
    for (const auto& format : formats) {
        if (CaptureFormat(format.w, format.h, format.fps)) {
            ESP_LOGI(TAG, "captured %ux%u jpeg %u bytes frames=%d head=%02x%02x%02x",
                     width_, height_, static_cast<unsigned>(jpeg_.size()), burst_count_,
                     jpeg_[0], jpeg_[1], jpeg_[2]);
            return true;
        }
    }
    ESP_LOGE(TAG, "UVC capture failed");
    PauseMic(false);
    return false;
}

std::string UvcCamera::Explain(const std::string& question) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (explain_url_.empty()) {
        PauseMic(false);
        throw std::runtime_error("Image explain URL is not set");
    }
    if (jpeg_.empty()) {
        PauseMic(false);
        throw std::runtime_error("No camera frame captured");
    }

    auto network = Board::GetInstance().GetNetwork();
    auto http = network->CreateHttp(3);
    std::string boundary = "----ESP32_CAMERA_BOUNDARY";

    http->SetHeader("Device-Id", SystemInfo::GetMacAddress().c_str());
    http->SetHeader("Client-Id", Board::GetInstance().GetUuid().c_str());
    if (!explain_token_.empty()) {
        http->SetHeader("Authorization", "Bearer " + explain_token_);
    }
    http->SetHeader("Content-Type", "multipart/form-data; boundary=" + boundary);
    http->SetHeader("Transfer-Encoding", "chunked");
    if (!http->Open("POST", explain_url_)) {
        PauseMic(false);
        throw std::runtime_error("Failed to connect to explain URL");
    }

    std::string ask = question;
    if (burst_count_ > 1) {
        ask = "这是连续" + std::to_string(burst_count_) +
              "帧连拍，从上到下按时间排列。" + question;
    }

    std::string question_field;
    question_field += "--" + boundary + "\r\n";
    question_field += "Content-Disposition: form-data; name=\"question\"\r\n\r\n";
    question_field += ask + "\r\n";
    http->Write(question_field.c_str(), question_field.size());

    std::string file_header;
    file_header += "--" + boundary + "\r\n";
    file_header += "Content-Disposition: form-data; name=\"file\"; filename=\"camera.jpg\"\r\n";
    file_header += "Content-Type: image/jpeg\r\n\r\n";
    http->Write(file_header.c_str(), file_header.size());
    http->Write(reinterpret_cast<const char*>(jpeg_.data()), jpeg_.size());

    std::string footer = "\r\n--" + boundary + "--\r\n";
    http->Write(footer.c_str(), footer.size());
    http->Write("", 0);

    const int status = http->GetStatusCode();
    std::string result = http->ReadAll();
    http->Close();
    PauseMic(false);
    StripThinkTags(result);
    ESP_LOGI(TAG, "explain HTTP %d jpeg=%u frames=%d q=%s result=%s", status,
             static_cast<unsigned>(jpeg_.size()), burst_count_, ask.c_str(), result.c_str());
    if (status != 200) {
        throw std::runtime_error("Failed to upload photo");
    }
    if (result.find("\"success\":false") != std::string::npos ||
        result.find("\"success\": false") != std::string::npos) {
        ESP_LOGW(TAG, "vision service rejected photo");
    }
    return result;
}
