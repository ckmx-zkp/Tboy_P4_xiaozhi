#include "dual_eye_display.h"
#include "config.h"

#include "application.h"
#include "device_state.h"

#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_random.h>

#include <cstdio>
#include <cstring>

#define TAG "DualEye"

#define EYE_ASSET(name)                                                  \
    extern const uint8_t name##_start[] asm("_binary_" #name "_start"); \
    extern const uint8_t name##_end[] asm("_binary_" #name "_end");

EYE_ASSET(eye_master_bin)
EYE_ASSET(eye_happy_bin)
EYE_ASSET(eye_angry_bin)
EYE_ASSET(eye_sad_bin)
EYE_ASSET(eye_joy_bin)
EYE_ASSET(eye_closed_bin)
EYE_ASSET(eye_blink_30_bin)
EYE_ASSET(eye_blink_70_bin)
EYE_ASSET(eye_blink_closed_bin)

namespace {
constexpr int kFps = 12;
constexpr int kGazeShift = 12;
constexpr int kBlinkSeq[] = {0, 1, 2, 1};
constexpr int kBlinkSeqLen = 4;
}  // namespace

DualEyeDisplay::DualEyeDisplay(esp_lcd_panel_handle_t left, esp_lcd_panel_handle_t right,
                               int width, int height)
    : left_(left), right_(right), width_(width), height_(height) {
    fb_bytes_ = (size_t)width_ * height_ * sizeof(uint16_t);
    fb_ = static_cast<uint16_t*>(heap_caps_malloc(fb_bytes_, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
    if (fb_ == nullptr) {
        fb_ = static_cast<uint16_t*>(heap_caps_malloc(fb_bytes_, MALLOC_CAP_DMA | MALLOC_CAP_SPIRAM));
    }
    if (fb_ == nullptr) {
        ESP_LOGE(TAG, "no DMA framebuffer");
        return;
    }
    std::memset(fb_, 0, fb_bytes_);

    use_assets_ = LoadAssets();

    mutex_ = xSemaphoreCreateMutex();
    running_ = true;
    xTaskCreatePinnedToCore(AnimTask, "s3_eyes", 4096, this, 3, &task_, 1);
    ESP_LOGI(TAG, "dual eyes %dx%d (full GRAM) left=mirror right=original assets=%d",
             width_, height_, use_assets_ ? 1 : 0);
}

DualEyeDisplay::~DualEyeDisplay() {
    running_ = false;
    if (task_ != nullptr) {
        vTaskDelay(pdMS_TO_TICKS(100));
        task_ = nullptr;
    }
    if (mutex_ != nullptr) {
        vSemaphoreDelete(mutex_);
        mutex_ = nullptr;
    }
    if (fb_ != nullptr) {
        heap_caps_free(fb_);
        fb_ = nullptr;
    }
}

bool DualEyeDisplay::LoadFrame(const uint8_t* start, const uint8_t* end, const uint16_t** out) {
    size_t n = (size_t)(end - start);
    if (n < fb_bytes_) {
        ESP_LOGE(TAG, "eye frame %u < %u", (unsigned)n, (unsigned)fb_bytes_);
        return false;
    }
    *out = reinterpret_cast<const uint16_t*>(start);
    return true;
}

bool DualEyeDisplay::LoadAssets() {
    bool ok = true;
    ok &= LoadFrame(eye_master_bin_start, eye_master_bin_end, &frame_neutral_);
    ok &= LoadFrame(eye_happy_bin_start, eye_happy_bin_end, &frame_happy_);
    ok &= LoadFrame(eye_angry_bin_start, eye_angry_bin_end, &frame_angry_);
    ok &= LoadFrame(eye_sad_bin_start, eye_sad_bin_end, &frame_sad_);
    ok &= LoadFrame(eye_joy_bin_start, eye_joy_bin_end, &frame_joy_);
    ok &= LoadFrame(eye_closed_bin_start, eye_closed_bin_end, &frame_closed_);
    ok &= LoadFrame(eye_blink_30_bin_start, eye_blink_30_bin_end, &blink_[0]);
    ok &= LoadFrame(eye_blink_70_bin_start, eye_blink_70_bin_end, &blink_[1]);
    ok &= LoadFrame(eye_blink_closed_bin_start, eye_blink_closed_bin_end, &blink_[2]);
    if (ok) {
        ESP_LOGI(TAG, "C1 images loaded (right-eye assets, left mirrored at flush)");
    }
    return ok;
}

const uint16_t* DualEyeDisplay::EmotionFrame() const {
    if (emotion_ == "happy") {
        return frame_happy_;
    }
    if (emotion_ == "angry") {
        return frame_angry_;
    }
    if (emotion_ == "sad") {
        return frame_sad_;
    }
    if (emotion_ == "joy") {
        return frame_joy_;
    }
    return frame_neutral_;
}

DualEyeDisplay::Gaze DualEyeDisplay::ParseGaze(const char* dir) const {
    if (dir == nullptr) {
        return Gaze::Center;
    }
    if (strcmp(dir, "left") == 0 || strcmp(dir, "左") == 0) {
        return Gaze::Left;
    }
    if (strcmp(dir, "right") == 0 || strcmp(dir, "右") == 0) {
        return Gaze::Right;
    }
    if (strcmp(dir, "up") == 0 || strcmp(dir, "上") == 0) {
        return Gaze::Up;
    }
    if (strcmp(dir, "down") == 0 || strcmp(dir, "下") == 0) {
        return Gaze::Down;
    }
    return Gaze::Center;
}

void DualEyeDisplay::SetEmotion(const char* emotion) {
    if (emotion == nullptr || emotion[0] == '\0') {
        return;
    }
    std::string key = emotion;
    const char* canon = "neutral";
    if (key == "happy" || key == "开心" || key == "高兴" || key == "喜") {
        canon = "happy";
    } else if (key == "angry" || key == "生气" || key == "愤怒" || key == "怒") {
        canon = "angry";
    } else if (key == "sad" || key == "难过" || key == "伤心" || key == "哀") {
        canon = "sad";
    } else if (key == "joy" || key == "兴奋" || key == "快乐" || key == "乐") {
        canon = "joy";
    } else if (key == "neutral" || key == "正常" || key == "中性" || key == "平静") {
        canon = "neutral";
    }
    if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
    }
    emotion_ = canon;
    if (mutex_ != nullptr) {
        xSemaphoreGive(mutex_);
    }
    ESP_LOGI(TAG, "emotion=%s", canon);
}

std::string DualEyeDisplay::GetState() {
    if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
    }
    const char* gaze = "center";
    if (gaze_ == Gaze::Left) {
        gaze = "left";
    } else if (gaze_ == Gaze::Right) {
        gaze = "right";
    } else if (gaze_ == Gaze::Up) {
        gaze = "up";
    } else if (gaze_ == Gaze::Down) {
        gaze = "down";
    }
    char buf[160];
    snprintf(buf, sizeof(buf),
             "{\"emotion\":\"%s\",\"gaze\":\"%s\",\"closed\":%s,\"blinking\":%s}",
             emotion_.c_str(), gaze, closed_ ? "true" : "false",
             blink_step_ >= 0 ? "true" : "false");
    if (mutex_ != nullptr) {
        xSemaphoreGive(mutex_);
    }
    return buf;
}

void DualEyeDisplay::SetGaze(const char* dir) {
    Gaze g = ParseGaze(dir);
    int dx = 0;
    int dy = 0;
    if (g == Gaze::Left) {
        dx = -kGazeShift;
    } else if (g == Gaze::Right) {
        dx = kGazeShift;
    } else if (g == Gaze::Up) {
        dy = -kGazeShift;
    } else if (g == Gaze::Down) {
        dy = kGazeShift;
    }
    if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
    }
    gaze_ = g;
    gaze_dx_ = dx;
    gaze_dy_ = dy;
    if (mutex_ != nullptr) {
        xSemaphoreGive(mutex_);
    }
}

void DualEyeDisplay::BlinkOnce() {
    if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
    }
    blink_request_ = true;
    if (mutex_ != nullptr) {
        xSemaphoreGive(mutex_);
    }
}

void DualEyeDisplay::SetClosed(bool closed) {
    if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
    }
    closed_ = closed;
    if (mutex_ != nullptr) {
        xSemaphoreGive(mutex_);
    }
}

void DualEyeDisplay::SetAutoIdle(bool on) {
    if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
    }
    auto_idle_ = on;
    if (mutex_ != nullptr) {
        xSemaphoreGive(mutex_);
    }
}

void DualEyeDisplay::PackBe(const uint16_t* src, bool mirror, int dx, int dy) {
    // 右眼：资产原样 + 视线偏移。左眼先镜像，再用同样的玻璃方向偏移。
    for (int y = 0; y < height_; ++y) {
        for (int x = 0; x < width_; ++x) {
            int sx = mirror ? (width_ - 1 - x) : x;
            int sy = y;
            sx -= dx;
            sy -= dy;
            uint16_t v = 0;
            if (sx >= 0 && sx < width_ && sy >= 0 && sy < height_) {
                v = src[sy * width_ + sx];
            }
            fb_[y * width_ + x] = static_cast<uint16_t>((v >> 8) | (v << 8));
        }
    }
}

bool DualEyeDisplay::DrawFb(esp_lcd_panel_handle_t panel) {
    if (panel == nullptr) {
        return false;
    }
    if (esp_lcd_panel_draw_bitmap(panel, 0, 0, width_, height_, fb_) != ESP_OK) {
        return false;
    }
    // 排空异步 SPI，再画另一只眼 / 释放
    return esp_lcd_panel_invert_color(panel, DISPLAY_INVERT_COLOR) == ESP_OK;
}

void DualEyeDisplay::FlushBoth(const uint16_t* src) {
    if (fb_ == nullptr || src == nullptr || !use_assets_) {
        return;
    }
    PackBe(src, false, gaze_dx_, gaze_dy_);
    DrawFb(right_);
    PackBe(src, true, gaze_dx_, gaze_dy_);
    DrawFb(left_);
}

void DualEyeDisplay::AnimTask(void* arg) {
    static_cast<DualEyeDisplay*>(arg)->AnimLoop();
    vTaskDelete(nullptr);
}

void DualEyeDisplay::AnimLoop() {
    float next_blink = 2.5f;
    const float dt = 1.f / kFps;
    int blink_hold = 0;
    uint32_t tick = 0;

    while (running_) {
        const DeviceState state = Application::GetInstance().GetDeviceState();
        const bool is_speaking = (state == kDeviceStateSpeaking);

        if (mutex_ != nullptr) {
            xSemaphoreTake(mutex_, portMAX_DELAY);
        }
        const bool closed = closed_;
        const bool auto_idle = auto_idle_;
        if (blink_request_ && blink_step_ < 0) {
            blink_request_ = false;
            blink_step_ = 0;
            blink_hold = 0;
        }
        if (mutex_ != nullptr) {
            xSemaphoreGive(mutex_);
        }

        const uint16_t* src = nullptr;
        if (closed) {
            src = frame_closed_;
            blink_step_ = -1;
        } else if (blink_step_ < 0) {
            if (auto_idle) {
                next_blink -= dt;
                if (next_blink <= 0.f) {
                    blink_step_ = 0;
                    blink_hold = 0;
                    next_blink = is_speaking
                        ? 1.2f + (float)(esp_random() % 180) / 100.f
                        : 2.0f + (float)(esp_random() % 300) / 100.f;
                }
            }
            src = EmotionFrame();
        } else {
            src = blink_[kBlinkSeq[blink_step_]];
            if (++blink_hold >= 1) {
                blink_hold = 0;
                ++blink_step_;
                if (blink_step_ >= kBlinkSeqLen) {
                    blink_step_ = -1;
                }
            }
        }

        FlushBoth(src);

        if ((tick++ % 48) == 0) {
            ESP_LOGI(TAG, "emotion=%s blink=%d gaze=(%d,%d) closed=%d",
                     emotion_.c_str(), blink_step_, gaze_dx_, gaze_dy_, closed ? 1 : 0);
        }
        vTaskDelay(pdMS_TO_TICKS(1000 / kFps));
    }
}
