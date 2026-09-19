#include "face_follow_controller.h"
#include "config.h"

#include <cJSON.h>
#include <esp_log.h>
#include <esp_timer.h>

#include <algorithm>
#include <cmath>

#define TAG "FaceFollow"

namespace {
constexpr float kYawToNorm = 1.0f / 45.0f;

float Clampf(float v, float lo, float hi) {
    return std::clamp(v, lo, hi);
}
}  // namespace

FaceFollowController::FaceFollowController(Mg90sServo* servo, DualEyeDisplay* eyes)
    : servo_(servo), eyes_(eyes) {
    last_move_us_ = esp_timer_get_time();
}

void FaceFollowController::SetFollowEnabled(bool on) {
    follow_ = on;
    if (on) {
        ESP_LOGI(TAG, "auto follow on");
    } else {
        ESP_LOGI(TAG, "auto follow off, hold %.1f deg", current_deg_);
    }
}

void FaceFollowController::SetManualAngle(float deg) {
    follow_ = false;
    target_deg_ = Clampf(deg, static_cast<float>(SERVO_MIN_DEG),
                         static_cast<float>(SERVO_MAX_DEG));
    current_deg_ = target_deg_;
    if (servo_ != nullptr) {
        servo_->SetAngle(current_deg_);
    }
}

bool FaceFollowController::ParseOffset(const char* line, bool* face, float* dx, float* dy) {
    if (line == nullptr || line[0] != '{') {
        return false;
    }
    cJSON* root = cJSON_Parse(line);
    if (root == nullptr) {
        return false;
    }

    bool present = false;
    float out_dx = 0;
    float out_dy = 0;
    bool have_norm = false;

    if (cJSON* face_obj = cJSON_GetObjectItemCaseSensitive(root, "face")) {
        if (cJSON* p = cJSON_GetObjectItemCaseSensitive(face_obj, "present")) {
            present = cJSON_IsTrue(p);
        }
        if (cJSON* n = cJSON_GetObjectItemCaseSensitive(face_obj, "count")) {
            if (cJSON_IsNumber(n) && n->valuedouble > 0) {
                present = true;
            }
        }
    }

    if (cJSON* tr = cJSON_GetObjectItemCaseSensitive(root, "tracking")) {
        if (cJSON* jdx = cJSON_GetObjectItemCaseSensitive(tr, "dx")) {
            if (cJSON_IsNumber(jdx)) {
                out_dx = static_cast<float>(jdx->valuedouble);
                have_norm = true;
            }
        }
        if (cJSON* jdy = cJSON_GetObjectItemCaseSensitive(tr, "dy")) {
            if (cJSON_IsNumber(jdy)) {
                out_dy = static_cast<float>(jdy->valuedouble);
            }
        }
    }

    if (!have_norm) {
        if (cJSON* pose = cJSON_GetObjectItemCaseSensitive(root, "pose")) {
            if (cJSON* yaw = cJSON_GetObjectItemCaseSensitive(pose, "yaw")) {
                if (cJSON_IsNumber(yaw)) {
                    out_dx = Clampf(static_cast<float>(yaw->valuedouble) * kYawToNorm, -1.f, 1.f);
                    have_norm = true;
                }
            }
            if (cJSON* pitch = cJSON_GetObjectItemCaseSensitive(pose, "pitch")) {
                if (cJSON_IsNumber(pitch)) {
                    out_dy = Clampf(static_cast<float>(pitch->valuedouble) * kYawToNorm, -1.f, 1.f);
                }
            }
        }
    }

    cJSON_Delete(root);
    *face = present;
    *dx = Clampf(out_dx, -1.f, 1.f);
    *dy = Clampf(out_dy, -1.f, 1.f);
    return present || have_norm;
}

void FaceFollowController::SlewTo(float target_deg) {
    const int64_t now = esp_timer_get_time();
    const float dt_s = static_cast<float>(now - last_move_us_) / 1000000.f;
    last_move_us_ = now;
    const float max_step = SERVO_SLEW_DEG_PER_S * std::max(dt_s, 0.001f);
    float delta = target_deg - current_deg_;
    if (delta > max_step) {
        current_deg_ += max_step;
    } else if (delta < -max_step) {
        current_deg_ -= max_step;
    } else {
        current_deg_ = target_deg;
    }
    if (servo_ != nullptr) {
        servo_->SetAngle(current_deg_);
    }
}

void FaceFollowController::Apply(bool face, float dx, float dy) {
    const int64_t now = esp_timer_get_time();
    if (face) {
        face_ = true;
        last_face_us_ = now;
        last_dx_ = dx;
        last_dy_ = dy;
        if (std::fabs(dx) < SERVO_DEADZONE) {
            dx = 0;
        }
        if (std::fabs(dy) < SERVO_DEADZONE) {
            dy = 0;
        }
        if (SERVO_PAN_INVERT) {
            dx = -dx;
        }
        target_deg_ = static_cast<float>(SERVO_CENTER_DEG) + dx * SERVO_TRACK_SPAN_DEG;
        target_deg_ = Clampf(target_deg_,
                             static_cast<float>(SERVO_CENTER_DEG - SERVO_TRACK_SPAN_DEG),
                             static_cast<float>(SERVO_CENTER_DEG + SERVO_TRACK_SPAN_DEG));
        if (eyes_ != nullptr) {
            eyes_->SetGazeNorm(dx, dy);
        }
    } else if (face_ && (now - last_face_us_) > static_cast<int64_t>(SERVO_FACE_LOST_MS) * 1000) {
        face_ = false;
        last_dx_ = 0;
        last_dy_ = 0;
        target_deg_ = static_cast<float>(SERVO_CENTER_DEG);
        if (eyes_ != nullptr) {
            eyes_->SetGaze("center");
        }
    }

    if (follow_) {
        SlewTo(target_deg_);
    }
}

void FaceFollowController::OnVisionJson(const char* line) {
    bool face = false;
    float dx = 0;
    float dy = 0;
    if (!ParseOffset(line, &face, &dx, &dy)) {
        Apply(false, 0, 0);
        return;
    }
    Apply(face, dx, dy);
}
