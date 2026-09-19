#ifndef _FACE_FOLLOW_CONTROLLER_H_
#define _FACE_FOLLOW_CONTROLLER_H_

#include "dual_eye_display.h"
#include "mg90s_servo.h"

#include <cstdint>

// K230 全量 JSON → ESP32 本地自动跟随。不走 MCP 闭环。
// 水平：CN2 上的 MG90S 偏航；垂直：只有眼睛（原理图没有第二路 PWM）。
class FaceFollowController {
public:
    FaceFollowController(Mg90sServo* servo, DualEyeDisplay* eyes);

    void SetFollowEnabled(bool on);
    bool follow_enabled() const { return follow_; }
    bool face_present() const { return face_; }
    float target_deg() const { return target_deg_; }
    float current_deg() const { return current_deg_; }
    float last_dx() const { return last_dx_; }
    float last_dy() const { return last_dy_; }

    // 手动角度（MCP）。follow=false 时保持该角。
    void SetManualAngle(float deg);

    void OnVisionJson(const char* line);

private:
    bool ParseOffset(const char* line, bool* face, float* dx, float* dy);
    void Apply(bool face, float dx, float dy);
    void SlewTo(float target_deg);

    Mg90sServo* servo_ = nullptr;
    DualEyeDisplay* eyes_ = nullptr;
    bool follow_ = true;
    bool face_ = false;
    float last_dx_ = 0;
    float last_dy_ = 0;
    float target_deg_ = SERVO_CENTER_DEG;
    float current_deg_ = SERVO_CENTER_DEG;
    int64_t last_face_us_ = 0;
    int64_t last_move_us_ = 0;
};

#endif  // _FACE_FOLLOW_CONTROLLER_H_
