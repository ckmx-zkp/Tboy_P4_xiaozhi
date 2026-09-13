#ifndef _DUAL_EYE_DISPLAY_H_
#define _DUAL_EYE_DISPLAY_H_

#include <esp_lcd_panel_ops.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <cstdint>
#include <string>

// S3 大板双眼 C1：资产是右眼；左眼刷屏时水平镜像。
class DualEyeDisplay {
public:
    DualEyeDisplay(esp_lcd_panel_handle_t left, esp_lcd_panel_handle_t right,
                   int width, int height);
    ~DualEyeDisplay();

    void SetEmotion(const char* emotion);
    void SetGaze(const char* dir);
    void BlinkOnce();
    void SetClosed(bool closed);
    void SetAutoIdle(bool on);
    std::string GetState();

private:
    enum class Gaze : int { Center = 0, Left, Right, Up, Down };

    static void AnimTask(void* arg);
    void AnimLoop();
    bool LoadAssets();
    bool LoadFrame(const uint8_t* start, const uint8_t* end, const uint16_t** out);
    const uint16_t* EmotionFrame() const;
    Gaze ParseGaze(const char* dir) const;
    void FlushBoth(const uint16_t* src);
    void PackBe(const uint16_t* src, bool mirror, int dx, int dy);
    bool DrawFb(esp_lcd_panel_handle_t panel);

    esp_lcd_panel_handle_t left_ = nullptr;
    esp_lcd_panel_handle_t right_ = nullptr;
    int width_ = 0;
    int height_ = 0;
    uint16_t* fb_ = nullptr;
    size_t fb_bytes_ = 0;
    TaskHandle_t task_ = nullptr;
    SemaphoreHandle_t mutex_ = nullptr;
    std::string emotion_ = "neutral";
    bool running_ = false;
    bool use_assets_ = false;

    const uint16_t* frame_neutral_ = nullptr;
    const uint16_t* frame_happy_ = nullptr;
    const uint16_t* frame_angry_ = nullptr;
    const uint16_t* frame_sad_ = nullptr;
    const uint16_t* frame_joy_ = nullptr;
    const uint16_t* frame_closed_ = nullptr;
    const uint16_t* blink_[3] = {nullptr, nullptr, nullptr};

    Gaze gaze_ = Gaze::Center;
    int gaze_dx_ = 0;
    int gaze_dy_ = 0;
    int blink_step_ = -1;
    bool blink_request_ = false;
    bool closed_ = false;
    bool auto_idle_ = true;
};

#endif  // _DUAL_EYE_DISPLAY_H_
