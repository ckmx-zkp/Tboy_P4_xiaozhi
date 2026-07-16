#include "pet_eye_display.h"

#include "application.h"
#include "device_state.h"

#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_random.h>
#include <cstring>

#define TAG "PetEye"

// Embedded idle frames from assets/eye_idle_N.bin (converted from eye_pic/prompt-2-n-*.jpg)
extern const uint8_t eye_idle_0_bin_start[] asm("_binary_eye_idle_0_bin_start");
extern const uint8_t eye_idle_0_bin_end[]   asm("_binary_eye_idle_0_bin_end");
extern const uint8_t eye_idle_1_bin_start[] asm("_binary_eye_idle_1_bin_start");
extern const uint8_t eye_idle_1_bin_end[]   asm("_binary_eye_idle_1_bin_end");
extern const uint8_t eye_idle_2_bin_start[] asm("_binary_eye_idle_2_bin_start");
extern const uint8_t eye_idle_2_bin_end[]   asm("_binary_eye_idle_2_bin_end");
extern const uint8_t eye_idle_3_bin_start[] asm("_binary_eye_idle_3_bin_start");
extern const uint8_t eye_idle_3_bin_end[]   asm("_binary_eye_idle_3_bin_end");
extern const uint8_t eye_idle_4_bin_start[] asm("_binary_eye_idle_4_bin_start");
extern const uint8_t eye_idle_4_bin_end[]   asm("_binary_eye_idle_4_bin_end");
extern const uint8_t eye_idle_5_bin_start[] asm("_binary_eye_idle_5_bin_start");
extern const uint8_t eye_idle_5_bin_end[]   asm("_binary_eye_idle_5_bin_end");

namespace {
constexpr int kFps = 12;
constexpr int kIdleFrames = 6;
constexpr size_t kFrameBytes = 240 * 240 * sizeof(uint16_t);

struct EyeFrame {
    const uint16_t* data;
    size_t bytes;
};

EyeFrame g_idle[kIdleFrames];

bool LoadEmbeddedIdle() {
    const uint8_t* starts[kIdleFrames] = {
        eye_idle_0_bin_start, eye_idle_1_bin_start, eye_idle_2_bin_start,
        eye_idle_3_bin_start, eye_idle_4_bin_start, eye_idle_5_bin_start,
    };
    const uint8_t* ends[kIdleFrames] = {
        eye_idle_0_bin_end, eye_idle_1_bin_end, eye_idle_2_bin_end,
        eye_idle_3_bin_end, eye_idle_4_bin_end, eye_idle_5_bin_end,
    };
    for (int i = 0; i < kIdleFrames; ++i) {
        size_t n = (size_t)(ends[i] - starts[i]);
        if (n < kFrameBytes) {
            ESP_LOGE(TAG, "idle frame %d size %u < %u", i, (unsigned)n, (unsigned)kFrameBytes);
            return false;
        }
        g_idle[i].data = reinterpret_cast<const uint16_t*>(starts[i]);
        g_idle[i].bytes = kFrameBytes;
    }
    return true;
}
}  // namespace

PetEyeDisplay::PetEyeDisplay(esp_lcd_panel_io_handle_t io, esp_lcd_panel_handle_t panel, int width, int height)
    : io_(io), panel_(panel) {
    width_ = width;
    height_ = height;
    fb_bytes_ = (size_t)width_ * height_ * sizeof(uint16_t);
    fb_ = (uint16_t*)heap_caps_malloc(fb_bytes_, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (fb_ == nullptr) {
        fb_ = (uint16_t*)heap_caps_malloc(fb_bytes_, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (fb_ == nullptr) {
        ESP_LOGE(TAG, "Failed to allocate framebuffer");
        return;
    }
    std::memset(fb_, 0, fb_bytes_);

    use_assets_ = LoadEmbeddedIdle();
    if (use_assets_) {
        ESP_LOGI(TAG, "Using %d embedded idle RGB565 frames from eye_pic experiment", kIdleFrames);
    } else {
        ESP_LOGW(TAG, "Embedded eye frames missing/invalid; screen may stay black until assets fixed");
    }

    mutex_ = xSemaphoreCreateMutex();
    running_ = true;
    xTaskCreatePinnedToCore(AnimTask, "pet_eye", 4096, this, 3, &task_, 1);
    ESP_LOGI(TAG, "PetEyeDisplay started %dx%d asset_mode=%d", width_, height_, use_assets_ ? 1 : 0);
}

PetEyeDisplay::~PetEyeDisplay() {
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

bool PetEyeDisplay::Lock(int timeout_ms) {
    if (mutex_ == nullptr) {
        return true;
    }
    TickType_t ticks = timeout_ms <= 0 ? 0 : pdMS_TO_TICKS(timeout_ms);
    return xSemaphoreTake(mutex_, ticks) == pdTRUE;
}

void PetEyeDisplay::Unlock() {
    if (mutex_ != nullptr) {
        xSemaphoreGive(mutex_);
    }
}

void PetEyeDisplay::SetupUI() {
    setup_ui_called_ = true;
    ESP_LOGI(TAG, "SetupUI: asset eye experiment, chat/status on monitor only");
}

void PetEyeDisplay::SetEmotion(const char* emotion) {
    if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
    }
    if (emotion != nullptr) {
        emotion_ = emotion;
    }
    if (mutex_ != nullptr) {
        xSemaphoreGive(mutex_);
    }
    ESP_LOGI(TAG, "Emotion -> %s", emotion_.c_str());
}

void PetEyeDisplay::SetStatus(const char* status) {
    ESP_LOGI(TAG, "Status: %s", status ? status : "");
}

void PetEyeDisplay::SetChatMessage(const char* role, const char* content) {
    ESP_LOGI(TAG, "Chat [%s]: %s", role ? role : "?", content ? content : "");
}

void PetEyeDisplay::ShowNotification(const char* notification, int duration_ms) {
    (void)duration_ms;
    ESP_LOGI(TAG, "Notify: %s", notification ? notification : "");
}

void PetEyeDisplay::FlushFrame() {
    if (fb_ == nullptr || panel_ == nullptr) {
        return;
    }
    // SPI GC9A01 expects big-endian RGB565 on the wire.
    // Same as SpiLcdDisplay lvgl_port flags.swap_bytes=1 — without this,
    // photo frames look psychedelic/rainbow while shapes stay roughly visible.
    const size_t n = (size_t)width_ * height_;
    for (size_t i = 0; i < n; ++i) {
        uint16_t v = fb_[i];
        fb_[i] = (uint16_t)((v >> 8) | (v << 8));
    }
    esp_err_t err = esp_lcd_panel_draw_bitmap(panel_, 0, 0, width_, height_, fb_);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "draw_bitmap: %s", esp_err_to_name(err));
    }
    // Next frame always BlitFrame() overwrites fb_ in host (LE) order.
}

void PetEyeDisplay::BlitFrame(const uint16_t* src) {
    if (fb_ == nullptr || src == nullptr) {
        return;
    }
    // Assets are little-endian RGB565 (host order), matching C uint16_t packing.
    std::memcpy(fb_, src, fb_bytes_);
}

void PetEyeDisplay::ApplyEyelid(float open) {
    if (fb_ == nullptr || open >= 0.99f) {
        return;
    }
    int lid = (int)((1.f - open) * (height_ / 2));
    if (lid <= 0) {
        return;
    }
    // Black bars as simple blink overlay on top of photo frames
    std::memset(fb_, 0, (size_t)lid * width_ * sizeof(uint16_t));
    std::memset(fb_ + (height_ - lid) * width_, 0, (size_t)lid * width_ * sizeof(uint16_t));
}

void PetEyeDisplay::AnimTask(void* arg) {
    static_cast<PetEyeDisplay*>(arg)->AnimLoop();
    vTaskDelete(nullptr);
}

void PetEyeDisplay::AnimLoop() {
    float t = 0.f;
    float next_blink = 2.5f;
    float blink_t = -1.f;
    const float dt = 1.f / kFps;
    int frame_idx = 0;
    float frame_hold = 0.f;
    // ~2s for full idle loop with 6 frames
    const float hold_per_frame = 2.0f / kIdleFrames;
    uint32_t tick = 0;

    while (running_) {
        const DeviceState state = Application::GetInstance().GetDeviceState();
        const bool is_speaking = (state == kDeviceStateSpeaking);
        const bool is_listening = (state == kDeviceStateListening);

        // Advance idle frame
        frame_hold += dt;
        float hold = hold_per_frame;
        if (is_speaking) {
            hold = hold_per_frame * 0.7f;  // slightly faster energy
        } else if (is_listening) {
            hold = hold_per_frame * 0.85f;
        }
        if (frame_hold >= hold) {
            frame_hold = 0.f;
            frame_idx = (frame_idx + 1) % kIdleFrames;
        }

        // Blink (keeps working while speaking)
        float open = 1.f;
        if (blink_t < 0.f) {
            next_blink -= dt;
            if (next_blink <= 0.f) {
                blink_t = 0.f;
                if (is_speaking) {
                    next_blink = 1.2f + (float)(esp_random() % 180) / 100.f;
                } else {
                    next_blink = 2.0f + (float)(esp_random() % 300) / 100.f;
                }
            }
        } else {
            blink_t += dt;
            if (blink_t < 0.08f) {
                open = 1.f - blink_t / 0.08f;
            } else if (blink_t < 0.16f) {
                open = (blink_t - 0.08f) / 0.08f;
            } else {
                blink_t = -1.f;
                open = 1.f;
            }
        }

        if (fb_ != nullptr && panel_ != nullptr && use_assets_) {
            BlitFrame(g_idle[frame_idx].data);
            ApplyEyelid(open);
            FlushFrame();
        }

        if ((tick++ % 48) == 0) {
            ESP_LOGI(TAG, "asset eye frame=%d open=%.2f speaking=%d", frame_idx, open, is_speaking ? 1 : 0);
        }

        t += dt;
        vTaskDelay(pdMS_TO_TICKS(1000 / kFps));
    }
}
