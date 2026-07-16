#pragma once

#include "display.h"

#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <string>

/**
 * Pet eye display: plays embedded RGB565 frames (experimental),
 * with optional code blink overlay. No LVGL chat UI.
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

private:
    bool Lock(int timeout_ms = 0) override;
    void Unlock() override;

    static void AnimTask(void* arg);
    void AnimLoop();
    void FlushFrame();
    void BlitFrame(const uint16_t* src);
    void ApplyEyelid(float open);

    esp_lcd_panel_io_handle_t io_ = nullptr;
    esp_lcd_panel_handle_t panel_ = nullptr;
    uint16_t* fb_ = nullptr;
    size_t fb_bytes_ = 0;
    TaskHandle_t task_ = nullptr;
    SemaphoreHandle_t mutex_ = nullptr;
    std::string emotion_ = "neutral";
    bool running_ = false;
    bool use_assets_ = false;
};
