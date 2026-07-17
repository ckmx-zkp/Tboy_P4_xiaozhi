#include "pet_eye_display.h"

#include "application.h"
#include "device_state.h"

#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_random.h>
#include <cstring>

#define TAG "PetEye"

// C1 complete state images (240x240 RGB565, little-endian host order).
// Placeholders from scripts/gen_placeholder_eyes.py — swap in AI art later.
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
constexpr size_t kFrameBytes = 240 * 240 * sizeof(uint16_t);

// How far the eye shifts when looking in a direction (pixels).
constexpr int kGazeShift = 26;

// Blink playback indexes into blink_[]. Sequence:
// emotion -> 30 -> 70 -> closed -> 70 -> emotion  (~330ms at 12fps, 1 frame each)
constexpr int kBlinkSeq[] = {0, 1, 2, 1};
constexpr int kBlinkSeqLen = 4;
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

    use_assets_ = LoadAssets();
    if (!use_assets_) {
        ESP_LOGW(TAG, "Eye assets missing/invalid; screen may stay black until assets fixed");
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

bool PetEyeDisplay::LoadFrame(const uint8_t* start, const uint8_t* end, const uint16_t** out) {
    size_t n = (size_t)(end - start);
    if (n < kFrameBytes) {
        ESP_LOGE(TAG, "eye frame size %u < %u", (unsigned)n, (unsigned)kFrameBytes);
        return false;
    }
    *out = reinterpret_cast<const uint16_t*>(start);
    return true;
}

bool PetEyeDisplay::LoadAssets() {
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
        ESP_LOGI(TAG, "C1 eye images loaded (master/happy/angry/sad/joy + 3 blink)");
    }
    return ok;
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
    ESP_LOGI(TAG, "SetupUI: C1 eye, chat/status on monitor only");
}

void PetEyeDisplay::SetEmotion(const char* emotion) {
    if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
    }
    if (emotion != nullptr && emotion[0] != '\0') {
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

PetEyeDisplay::Gaze PetEyeDisplay::ParseGaze(const char* dir) const {
    if (dir == nullptr) {
        return Gaze::Center;
    }
    if (strcmp(dir, "left") == 0) {
        return Gaze::Left;
    }
    if (strcmp(dir, "right") == 0) {
        return Gaze::Right;
    }
    if (strcmp(dir, "up") == 0) {
        return Gaze::Up;
    }
    if (strcmp(dir, "down") == 0) {
        return Gaze::Down;
    }
    return Gaze::Center;
}

void PetEyeDisplay::SetGaze(const char* dir) {
    Gaze g = ParseGaze(dir);
    int dx = 0;
    int dy = 0;
    switch (g) {
        case Gaze::Left:  dx = -kGazeShift; break;
        case Gaze::Right: dx =  kGazeShift; break;
        case Gaze::Up:    dy = -kGazeShift; break;
        case Gaze::Down:  dy =  kGazeShift; break;
        case Gaze::Center: default: break;
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
    ESP_LOGI(TAG, "Gaze -> %s (dx=%d dy=%d)", dir ? dir : "center", dx, dy);
}

void PetEyeDisplay::BlinkOnce() {
    if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
    }
    // Only request if not already mid-blink; AnimLoop picks this up.
    if (blink_step_ < 0) {
        blink_request_ = true;
    }
    if (mutex_ != nullptr) {
        xSemaphoreGive(mutex_);
    }
    ESP_LOGI(TAG, "Blink requested");
}

void PetEyeDisplay::SetClosed(bool closed) {
    if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
    }
    closed_ = closed;
    if (mutex_ != nullptr) {
        xSemaphoreGive(mutex_);
    }
    ESP_LOGI(TAG, "Eye %s", closed ? "CLOSED (sleep)" : "OPEN");
}

void PetEyeDisplay::SetAutoIdle(bool on) {
    if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
    }
    auto_idle_ = on;
    if (mutex_ != nullptr) {
        xSemaphoreGive(mutex_);
    }
    ESP_LOGI(TAG, "Auto idle (blink+wander) %s", on ? "ON" : "OFF");
}

const uint16_t* PetEyeDisplay::EmotionFrame() const {
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

void PetEyeDisplay::FlushFrame() {
    if (fb_ == nullptr || panel_ == nullptr) {
        return;
    }
    // SPI GC9A01 expects big-endian RGB565 on the wire (same as lvgl swap_bytes=1).
    const size_t n = (size_t)width_ * height_;
    for (size_t i = 0; i < n; ++i) {
        uint16_t v = fb_[i];
        fb_[i] = (uint16_t)((v >> 8) | (v << 8));
    }
    esp_err_t err = esp_lcd_panel_draw_bitmap(panel_, 0, 0, width_, height_, fb_);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "draw_bitmap: %s", esp_err_to_name(err));
    }
}

void PetEyeDisplay::BlitFrame(const uint16_t* src) {
    if (fb_ == nullptr || src == nullptr) {
        return;
    }
    // Assets are little-endian RGB565 (host order); byte-swap happens in FlushFrame.
    std::memcpy(fb_, src, fb_bytes_);
}

// Blit the eye shifted by (dx, dy). The vacated border is filled black so the
// iris appears to look toward that direction within the round display.
void PetEyeDisplay::BlitFrameGaze(const uint16_t* src, int dx, int dy) {
    if (fb_ == nullptr || src == nullptr) {
        return;
    }
    if (dx == 0 && dy == 0) {
        std::memcpy(fb_, src, fb_bytes_);
        return;
    }
    std::memset(fb_, 0, fb_bytes_);
    const int W = width_;
    const int H = height_;
    // Source window we copy from, and where it lands in fb.
    int sx0 = dx > 0 ? 0 : -dx;          // looking right: take left part of src
    int sy0 = dy > 0 ? 0 : -dy;
    int dstx0 = dx > 0 ? dx : 0;
    int dsty0 = dy > 0 ? dy : 0;
    int copy_w = W - (dx > 0 ? dx : -dx);
    int copy_h = H - (dy > 0 ? dy : -dy);
    if (copy_w <= 0 || copy_h <= 0) {
        return;
    }
    for (int row = 0; row < copy_h; ++row) {
        const uint16_t* s = src + (sy0 + row) * W + sx0;
        uint16_t* d = fb_ + (dsty0 + row) * W + dstx0;
        std::memcpy(d, s, (size_t)copy_w * sizeof(uint16_t));
    }
}

void PetEyeDisplay::AnimTask(void* arg) {
    static_cast<PetEyeDisplay*>(arg)->AnimLoop();
    vTaskDelete(nullptr);
}

void PetEyeDisplay::AnimLoop() {
    float next_blink = 2.5f;
    const float dt = 1.f / kFps;
    int blink_hold = 0;
    uint32_t tick = 0;

    // Idle wandering gaze: occasionally glance somewhere, then return to center.
    float next_gaze = 4.0f;
    float gaze_back = -1.f;              // <0 = not waiting to recenter

    while (running_) {
        const DeviceState state = Application::GetInstance().GetDeviceState();
        const bool is_speaking = (state == kDeviceStateSpeaking);
        const bool is_listening = (state == kDeviceStateListening);

        // Snapshot shared flags.
        if (mutex_ != nullptr) {
            xSemaphoreTake(mutex_, portMAX_DELAY);
        }
        const bool closed = closed_;
        const bool auto_idle = auto_idle_;
        // Pick up an externally requested blink (MCP BlinkOnce).
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
            // Sleep: hold the fully-closed frame, ignore everything else.
            src = frame_closed_;
            blink_step_ = -1;
        } else if (blink_step_ < 0) {
            // Show current emotion; auto-blink only when auto_idle is on.
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
                    blink_step_ = -1;      // back to emotion
                }
            }
        }

        // Auto wandering gaze only when enabled, awake, and idle.
        if (auto_idle && !closed && !is_speaking && !is_listening) {
            if (gaze_back >= 0.f) {
                gaze_back -= dt;
                if (gaze_back <= 0.f) {
                    gaze_back = -1.f;
                    SetGaze("center");
                    next_gaze = 3.0f + (float)(esp_random() % 400) / 100.f;
                }
            } else {
                next_gaze -= dt;
                if (next_gaze <= 0.f) {
                    static const char* dirs[] = {"left", "right", "up", "down"};
                    SetGaze(dirs[esp_random() % 4]);
                    gaze_back = 0.9f + (float)(esp_random() % 120) / 100.f;
                }
            }
        }

        if (fb_ != nullptr && panel_ != nullptr && use_assets_ && src != nullptr) {
            BlitFrameGaze(src, gaze_dx_, gaze_dy_);
            FlushFrame();
        }

        if ((tick++ % 48) == 0) {
            ESP_LOGI(TAG, "eye emotion=%s blink=%d gaze=(%d,%d) closed=%d auto=%d speaking=%d",
                     emotion_.c_str(), blink_step_, gaze_dx_, gaze_dy_,
                     closed ? 1 : 0, auto_idle ? 1 : 0, is_speaking ? 1 : 0);
        }

        vTaskDelay(pdMS_TO_TICKS(1000 / kFps));
    }
}
