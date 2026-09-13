#ifndef _S3_USB_CAM_EYE_CONTROLLER_H_
#define _S3_USB_CAM_EYE_CONTROLLER_H_

#include "dual_eye_display.h"
#include "mcp_server.h"

// 只驱动双眼屏幕。灯色是 self.led.*，不要在这里改 WS2812。
class EyeController {
public:
    explicit EyeController(DualEyeDisplay* display) : display_(display) {
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
            "只切换双眼表情（右眼原图，左眼水平镜像），绝不改 WS2812。"
            "用户说开心、高兴、生气、难过、兴奋、恢复正常、换表情时必须立刻调用。"
            "若用户同时要灯变色或说心情/氛围，同一轮再调用 self.led.set_emotion，不要用本工具改灯。"
            "emotion：neutral/happy/angry/sad/joy。",
            PropertyList({Property("emotion", kPropertyTypeString, "neutral")}),
            [this](const PropertyList& properties) -> ReturnValue {
                display_->SetEmotion(properties["emotion"].value<std::string>().c_str());
                return display_->GetState();
            });

        mcp.AddTool(
            "self.eye.get_state",
            "查询当前眼睛情绪、视线、是否闭眼。不改灯。",
            PropertyList(),
            [this](const PropertyList&) -> ReturnValue {
                return display_->GetState();
            });
    }

private:
    DualEyeDisplay* display_ = nullptr;
};

#endif  // _S3_USB_CAM_EYE_CONTROLLER_H_
