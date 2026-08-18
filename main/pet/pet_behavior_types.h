#pragma once

/**
 * Behavior orchestration outputs consumed by eye / servo / LED drivers.
 * Vision and MCP both produce intents; controller arbitrates by priority+TTL.
 */

#include "pet/pet_types.h"
#include "pet/vision/pet_vision_types.h"

#include <stdint.h>

namespace pet {

enum class LedEffect : uint8_t {
    kOff = 0,
    kSolid,
    kBreath,
    kPulse,
    kScan,
    kBlinkAlert,
};

struct Rgb8 {
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
};

struct EyeIntent {
    EmotionLabel emotion = EmotionLabel::kNeutral;
    NormQ15 gaze_x = 0;  // -1..1 → mapped to pixel shift by driver
    NormQ15 gaze_y = 0;
    bool blink = false;
    bool eyes_closed = false;
};

struct HeadIntent {
    int16_t pan_deg_x10 = 0;   // degrees * 10
    int16_t tilt_deg_x10 = 0;
    uint16_t speed_deg_s_x10 = 300;  // 30.0 deg/s default
    bool hold = false;               // keep last pose
};

struct LedIntent {
    LedEffect effect = LedEffect::kOff;
    Rgb8 color{};
    uint8_t brightness = 0;  // 0..255, driver still clamps for heat/power
};

/**
 * One arbitrated frame. Drivers should treat missing subsystems as no-ops
 * (e.g. S3 board without servos ignores HeadIntent).
 */
struct PetBehaviorFrame {
    EyeIntent eye{};
    HeadIntent head{};
    LedIntent led{};

    BehaviorSource source = BehaviorSource::kIdleAuto;
    BehaviorPriority priority = BehaviorPriority::kIdle;
    uint32_t ttl_ms = 0;  // 0 = until replaced by same-or-higher priority
    TimeMs issued_ms = 0;
};

/** MCP / debug command mapped into the same arbitration path. */
struct PetCommand {
    BehaviorSource source = BehaviorSource::kMcpUser;
    BehaviorPriority priority = BehaviorPriority::kMcp;
    uint32_t ttl_ms = 3000;

    bool has_eye = false;
    EyeIntent eye{};

    bool has_head = false;
    HeadIntent head{};

    bool has_led = false;
    LedIntent led{};
};

/**
 * Snapshot for logs / optional MCP read tools. Keep small for S3 stack use.
 */
struct PetRuntimeSnapshot {
    PetVisionLinkStatus link{};
    PetVisionSample last_sample{};
    FacePose stable_pose = FacePose::kNone;
    EmotionLabel stable_emotion = EmotionLabel::kUnknown;
    uint8_t mood_ratio_pct = 0;
    bool mood_pending_report = false;
    PetBehaviorFrame active{};
};

}  // namespace pet
