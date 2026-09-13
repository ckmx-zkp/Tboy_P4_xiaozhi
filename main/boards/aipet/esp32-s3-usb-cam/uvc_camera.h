#ifndef _UVC_CAMERA_H_
#define _UVC_CAMERA_H_

#include "camera.h"

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <usb/uvc_host.h>

// U4 原生 USB Host UVC。这颗摄像头只稳定出 640×480 MJPEG。
class UvcCamera : public Camera {
public:
    UvcCamera();
    ~UvcCamera();

    // 必须在 I2S/WiFi 占满中断之前调用；USB Host 要 Level1+IRAM
    bool StartHost();

    void SetExplainUrl(const std::string& url, const std::string& token) override;
    bool Capture() override;
    bool SetHMirror(bool enabled) override;
    bool SetVFlip(bool enabled) override;
    std::string Explain(const std::string& question) override;

private:
    bool EnsureHost();
    bool CaptureFormat(uint16_t w, uint16_t h, float fps);
    static bool ExtractJpeg(const uint8_t* data, size_t len, std::vector<uint8_t>& out);
    static bool ComposeBurstJpeg(const std::vector<std::vector<uint8_t>>& frames,
                                 std::vector<uint8_t>& out, uint16_t* out_w, uint16_t* out_h);
    static void StripThinkTags(std::string& text);
    static void PauseMic(bool pause);
    static bool FrameCallback(const uvc_host_frame_t* frame, void* user_ctx);
    static void StreamCallback(const uvc_host_stream_event_data_t* event, void* user_ctx);
    static void UsbLibTask(void* arg);

    std::mutex mutex_;
    std::string explain_url_;
    std::string explain_token_;
    std::vector<uint8_t> jpeg_;
    uint16_t width_ = 0;
    uint16_t height_ = 0;
    int burst_count_ = 0;
    QueueHandle_t frame_q_ = nullptr;
    volatile bool disconnected_ = false;
    bool host_ready_ = false;
};
#endif
