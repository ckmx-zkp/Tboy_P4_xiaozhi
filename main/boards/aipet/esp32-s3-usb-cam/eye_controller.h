#ifndef _S3_USB_CAM_EYE_CONTROLLER_H_
#define _S3_USB_CAM_EYE_CONTROLLER_H_

#include "dual_eye_display.h"
#include "led_mood_controller.h"
#include "mcp_server.h"

// 换情时双眼和 WS2812 一起变。记忆不在本机：云端 default_emotion / 话术都走本工具。
class EyeController {
public:
    EyeController(DualEyeDisplay* display, LedMoodController* leds)
        : display_(display), leds_(leds) {
        if (display_ == nullptr) {
            return;
        }
        auto& mcp = McpServer::GetInstance();

        mcp.AddTool(
            "self.eye.look",
            "只改眼睛视线，不改灯。用户说向上看、低头、看左边、看右边、看中间、转头看时必须调用。"
            "direction：up/down/left/right/center。",
            PropertyList({Property("direction", kPropertyTypeString, "center")}),
            [this](const PropertyList& properties) -> ReturnValue {
                display_->SetGaze(properties["direction"].value<std::string>().c_str());
                return display_->GetState();
            });

        mcp.AddTool(
            "self.eye.blink",
            "只让双眼眨一次，不改灯。用户说眨眼、眨眨眼、wink 时必须调用。",
            PropertyList(),
            [this](const PropertyList&) -> ReturnValue {
                display_->BlinkOnce();
                return display_->GetState();
            });

        mcp.AddTool(
            "self.eye.close",
            "闭上双眼保持睡眠，不改灯。用户说闭眼、睡觉、休息眼睛时调用。醒来用 self.eye.open。",
            PropertyList(),
            [this](const PropertyList&) -> ReturnValue {
                display_->SetClosed(true);
                return display_->GetState();
            });

        mcp.AddTool(
            "self.eye.open",
            "睁开双眼，不改灯。用户说睁眼、醒来、别睡了时调用。",
            PropertyList(),
            [this](const PropertyList&) -> ReturnValue {
                display_->SetClosed(false);
                return display_->GetState();
            });

        mcp.AddTool(
            "self.eye.set_emotion",
            "切换双眼表情，并同步两颗 WS2812 为同一心情色（P1 同轮联动）。"
            "用户说开心、高兴、生气、难过、兴奋、恢复正常、换表情时必须立刻调用。"
            "同一轮不必再调 self.led.set_emotion，除非只要灯不要换脸。"
            "emotion：neutral/happy/angry/sad/joy。云端人设 default_emotion 也走本工具。",
            PropertyList({Property("emotion", kPropertyTypeString, "neutral")}),
            [this](const PropertyList& properties) -> ReturnValue {
                const auto emo = properties["emotion"].value<std::string>();
                display_->SetEmotion(emo.c_str());
                if (leds_ != nullptr) {
                    leds_->ApplyEmotion(emo);
                }
                return display_->GetState();
            });

        mcp.AddTool(
            "self.eye.set_blink_profile",
            "设置自动眨眼间隔（毫秒）。云端 persona blink_profile 可下发。"
            "interval_ms：800–8000，说话时固件仍会再加快。",
            PropertyList({Property("interval_ms", kPropertyTypeInteger, 2500, 800, 8000)}),
            [this](const PropertyList& properties) -> ReturnValue {
                display_->SetBlinkProfile(properties["interval_ms"].value<int>());
                return display_->GetState();
            });

        mcp.AddTool(
            "self.eye.get_state",
            "查询当前眼睛情绪、视线、是否闭眼。",
            PropertyList(),
            [this](const PropertyList&) -> ReturnValue {
                return display_->GetState();
            });
    }

private:
    DualEyeDisplay* display_ = nullptr;
    LedMoodController* leds_ = nullptr;
};

#endif  // _S3_USB_CAM_EYE_CONTROLLER_H_
