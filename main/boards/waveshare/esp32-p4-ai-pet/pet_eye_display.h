#pragma once

#include "display.h"

#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <string>

/**
 * Pet eye display: C1 complete state images. Firmware switches the whole
 * 240x240 RGB565 image by emotion and plays a short blink keyframe sequence.
 * No LVGL chat UI.
 */
class PetEyeDisplay : public Display {
public:
    PetEyeDisplay(esp_lcd_panel_io_handle_t io, esp_lcd_panel_handle_t panel, int width, int height);
    ~PetEyeDisplay() override;

    void SetEmotion(const char* emotion) override;
    void SetStatus(const char* status) override;
    void SetChatMessage(const char* role, const char* content) override;
    void ShowNotification(const char* notification, int duration_ms = 3000) override;
    void SetupUI() override;

    /** Look direction: shifts the eye within the round display.
     *  dir: "center" | "left" | "right" | "up" | "down". */
    void SetGaze(const char* dir);

    /** Trigger a single blink sequence now (thread-safe, MCP-callable). */
    void BlinkOnce();

    /** Hold the eye fully closed (sleep) or open it again. While closed,
     *  emotion/gaze changes are ignored until reopened. */
    void SetClosed(bool closed);

    /** Enable/disable the automatic idle behaviors (random blink + wandering
     *  gaze). Disabled by default so MCP-driven actions are easy to observe. */
    void SetAutoIdle(bool on);

private:
    enum class Gaze : int { Center = 0, Left, Right, Up, Down };

    bool Lock(int timeout_ms = 0) override;
    void Unlock() override;

    static void AnimTask(void* arg);
    void AnimLoop();
    void FlushFrame();
    void BlitFrame(const uint16_t* src);
    void BlitFrameGaze(const uint16_t* src, int dx, int dy);

    bool LoadAssets();
    bool LoadFrame(const uint8_t* start, const uint8_t* end, const uint16_t** out);
    const uint16_t* EmotionFrame() const;
    Gaze ParseGaze(const char* dir) const;

    esp_lcd_panel_io_handle_t io_ = nullptr;
    esp_lcd_panel_handle_t panel_ = nullptr;
    uint16_t* fb_ = nullptr;
    size_t fb_bytes_ = 0;
    TaskHandle_t task_ = nullptr;
    SemaphoreHandle_t mutex_ = nullptr;
    std::string emotion_ = "neutral";
    bool running_ = false;
    bool use_assets_ = false;

    // C1 state frames (points into embedded flash; not owned).
    const uint16_t* frame_neutral_ = nullptr;
    const uint16_t* frame_happy_ = nullptr;
    const uint16_t* frame_angry_ = nullptr;
    const uint16_t* frame_sad_ = nullptr;
    const uint16_t* frame_joy_ = nullptr;
    const uint16_t* frame_closed_ = nullptr;
    const uint16_t* blink_[3] = {nullptr, nullptr, nullptr};

    // Gaze (look direction). Offset in pixels applied when blitting.
    Gaze gaze_ = Gaze::Center;
    int gaze_dx_ = 0;
    int gaze_dy_ = 0;

    // Blink state owned by AnimLoop; BlinkOnce() requests a blink by setting
    // blink_request_. Access is guarded by mutex_.
    int blink_step_ = -1;                // -1 = not blinking
    bool blink_request_ = false;

    // Sleep / auto-idle control (guarded by mutex_ where noted).
    bool closed_ = false;                // eye held shut
    bool auto_idle_ = false;             // random blink + wandering gaze
};
