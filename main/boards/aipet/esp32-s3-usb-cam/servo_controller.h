#ifndef _S3_USB_CAM_SERVO_CONTROLLER_H_
#define _S3_USB_CAM_SERVO_CONTROLLER_H_

#include "face_follow_controller.h"
#include "mcp_server.h"
#include "mg90s_servo.h"

#include <cstdio>
#include <string>

// 自动跟随是默认路径。MCP 只做查询、开关和临时指定角度，不负责追人脸。
class ServoController {
public:
    ServoController(Mg90sServo* servo, FaceFollowController* follow)
        : servo_(servo), follow_(follow) {
        if (servo_ == nullptr || follow_ == nullptr) {
            return;
        }
        auto& mcp = McpServer::GetInstance();
        mcp.AddTool(
            "self.servo.set_follow",
            "打开或关闭人脸自动跟随（默认开）。关闭后舵机停在当前角。"
            "enabled：true/false。",
            PropertyList({Property("enabled", kPropertyTypeBoolean, true)}),
            [this](const PropertyList& properties) -> ReturnValue {
                follow_->SetFollowEnabled(properties["enabled"].value<bool>());
                return StateJson();
            });
        mcp.AddTool(
            "self.servo.set_angle",
            "手动转到指定角度并暂停自动跟随。MG90S 0–180，90 为正中。"
            "用户说转头、看左边、看右边且要转脖子时用。恢复跟随再调 self.servo.set_follow。",
            PropertyList({Property("angle", kPropertyTypeInteger, 90, 0, 180)}),
            [this](const PropertyList& properties) -> ReturnValue {
                follow_->SetManualAngle(static_cast<float>(properties["angle"].value<int>()));
                return StateJson();
            });
        mcp.AddTool(
            "self.servo.center",
            "舵机回到正中 90°，并重新打开自动跟随。",
            PropertyList(),
            [this](const PropertyList&) -> ReturnValue {
                follow_->SetManualAngle(static_cast<float>(SERVO_CENTER_DEG));
                follow_->SetFollowEnabled(true);
                return StateJson();
            });
        mcp.AddTool(
            "self.servo.get_state",
            "查询舵机角度、是否自动跟随、是否看到人脸。",
            PropertyList(),
            [this](const PropertyList&) -> ReturnValue { return StateJson(); });
    }

private:
    std::string StateJson() const {
        char buf[192];
        snprintf(buf, sizeof(buf),
                 "{\"angle\":%.1f,\"target\":%.1f,\"follow\":%s,\"face\":%s,\"dx\":%.3f,\"dy\":%.3f}",
                 follow_->current_deg(), follow_->target_deg(),
                 follow_->follow_enabled() ? "true" : "false",
                 follow_->face_present() ? "true" : "false",
                 follow_->last_dx(), follow_->last_dy());
        return buf;
    }

    Mg90sServo* servo_ = nullptr;
    FaceFollowController* follow_ = nullptr;
};

#endif  // _S3_USB_CAM_SERVO_CONTROLLER_H_
