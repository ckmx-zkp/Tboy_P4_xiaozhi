#ifndef _S3_USB_CAM_SERVO_CONTROLLER_H_
#define _S3_USB_CAM_SERVO_CONTROLLER_H_

#include "face_follow_controller.h"
#include "mcp_server.h"
#include "mg90s_servo.h"

#include <cstdio>
#include <esp_log.h>
#include <string>

#define SERVO_MCP_TAG "ServoMcp"

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
            "手动转到指定角度并暂停自动跟随。MG90S GPIO8，0–180，90 为正中。"
            "用户说转到某某度、转到中间时用。向左转/向右转请用 self.servo.turn_left/turn_right。",
            PropertyList({Property("angle", kPropertyTypeInteger, 90, 0, 180)}),
            [this](const PropertyList& properties) -> ReturnValue {
                follow_->SetManualAngle(static_cast<float>(properties["angle"].value<int>()));
                return StateJson();
            });
        mcp.AddTool(
            "self.servo.turn_left",
            "脖子向左转（CN2 / GPIO8 / MG90S）。"
            "用户说向左转、往左转、向左扭头、把头转向左边时必须立刻调用，不要只用 self.eye.look。"
            "degrees：相对正中向左偏转角度，默认 30，范围 10–90。会暂停自动跟随。",
            PropertyList({Property("degrees", kPropertyTypeInteger, 30, 10, 90)}),
            [this](const PropertyList& properties) -> ReturnValue {
                TurnByVoice(-1, properties["degrees"].value<int>());
                return StateJson();
            });
        mcp.AddTool(
            "self.servo.turn_right",
            "脖子向右转（CN2 / GPIO8 / MG90S）。"
            "用户说向右转、往右转、向右扭头、把头转向右边时必须立刻调用，不要只用 self.eye.look。"
            "degrees：相对正中向右偏转角度，默认 30，范围 10–90。会暂停自动跟随。",
            PropertyList({Property("degrees", kPropertyTypeInteger, 30, 10, 90)}),
            [this](const PropertyList& properties) -> ReturnValue {
                TurnByVoice(1, properties["degrees"].value<int>());
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
    void TurnByVoice(int dir, int degrees) {
        // dir: -1 左 / +1 右（相对正中）。SERVO_PAN_INVERT=1 时左右对调。
        int offset = dir * degrees;
        if (SERVO_PAN_INVERT) {
            offset = -offset;
        }
        const float target = static_cast<float>(SERVO_CENTER_DEG + offset);
        ESP_LOGI(SERVO_MCP_TAG, "GPIO8 turn %s %d deg -> %.0f",
                 dir < 0 ? "left" : "right", degrees, target);
        follow_->SetManualAngle(target);
    }

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
