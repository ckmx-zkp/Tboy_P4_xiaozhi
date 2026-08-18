#pragma once

/**
 * K230 / local vision data plane.
 *
 * Layers:
 *   1) PetVisionSample     - one parsed recognition result (from JSON/bin frame)
 *   2) PetVisionLinkStatus - UART health (portable across P4/S3)
 *   3) PetMoodWindowState  - rolling "sustained mood" aggregator
 *   4) PetVisionEvent      - debounced local / reportable events
 *
 * Memory note (S3 R16N32):
 * - Keep hot path structs small and fixed-size.
 * - Do not store raw JSON here; parser writes directly into PetVisionSample.
 * - Ring history is optional and compile-time sized for low-RAM targets.
 */

#include "pet/pet_types.h"

#include <stddef.h>
#include <stdint.h>

namespace pet {

// ---------------------------------------------------------------------------
// 1) One recognition sample (K230 JSON → this)
// ---------------------------------------------------------------------------

/**
 * Parsed structured result. Field units:
 * - cx/cy: face center in [-1, 1], image center = 0
 * - yaw/pitch/roll: degrees (K230 face_pose); 0 if unavailable
 * - box_*: normalized bbox optional; 0 if unused
 * - seq: producer sequence for loss/reorder detection
 */
struct PetVisionSample {
    uint32_t seq = 0;
    TimeMs timestamp_ms = 0;

    bool face_present = false;
    NormQ15 cx = 0;
    NormQ15 cy = 0;

    int16_t yaw_deg_x10 = 0;    // degrees * 10
    int16_t pitch_deg_x10 = 0;
    int16_t roll_deg_x10 = 0;

    NormQ15 box_w = 0;  // relative size → close/far heuristic
    NormQ15 box_h = 0;

    EmotionLabel emotion = EmotionLabel::kUnknown;
    uint8_t emotion_conf_pct = 0;  // 0..100

    FacePose pose = FacePose::kNone;
    uint8_t pose_conf_pct = 0;

    bool valid = false;  // CRC/parse OK
};

/** Expected JSON keys (documentation for K230 firmware / parser). */
struct PetVisionJsonSchema {
    // {"face":true,"cx":0.32,"cy":-0.15,"yaw":18.2,"pitch":-7.5,
    //  "roll":0.0,"emotion":"happy","confidence":0.91,"seq":123}
    static constexpr const char* kFace = "face";
    static constexpr const char* kCx = "cx";
    static constexpr const char* kCy = "cy";
    static constexpr const char* kYaw = "yaw";
    static constexpr const char* kPitch = "pitch";
    static constexpr const char* kRoll = "roll";
    static constexpr const char* kEmotion = "emotion";
    static constexpr const char* kConfidence = "confidence";
    static constexpr const char* kSeq = "seq";
};

// ---------------------------------------------------------------------------
// 2) UART / link status
// ---------------------------------------------------------------------------

enum class VisionLinkState : uint8_t {
    kDown = 0,
    kWaitingHandshake,
    kAlive,
    kDegraded,  // CRC errors / stalls but not fully dead
};

struct PetVisionLinkStatus {
    VisionLinkState state = VisionLinkState::kDown;
    TimeMs last_rx_ms = 0;
    TimeMs last_valid_ms = 0;
    uint32_t rx_frames = 0;
    uint32_t crc_errors = 0;
    uint32_t parse_errors = 0;
    uint32_t seq_gaps = 0;
};

// ---------------------------------------------------------------------------
// 3) Sustained mood aggregator ("5 min good mood → report once")
// ---------------------------------------------------------------------------

/**
 * Per-emotion (or per mood-class) rolling window config.
 * Defaults match the product discussion: sustained positive mood ~5 minutes.
 */
struct PetMoodWindowConfig {
    EmotionLabel target = EmotionLabel::kHappy;

    uint32_t window_ms = 5 * 60 * 1000;      // observe window
    uint8_t min_ratio_pct = 70;              // % of samples in-window matching
    uint8_t min_conf_pct = 60;               // ignore low-confidence labels
    uint32_t min_samples = 30;               // avoid firing on sparse data

    uint32_t cooldown_ms = 15 * 60 * 1000;   // after a report, suppress repeats
    uint32_t face_lost_reset_ms = 10 * 1000; // no face → clear accumulator
};

/**
 * Compact rolling stats. Implementation can use:
 * - time-bucket histogram, or
 * - EMA + dwell timer
 * This struct is the *observable state*, not the algorithm internals.
 */
struct PetMoodWindowState {
    PetMoodWindowConfig cfg{};

    TimeMs window_start_ms = 0;
    uint32_t sample_count = 0;
    uint32_t match_count = 0;

    TimeMs last_face_ms = 0;
    TimeMs last_match_ms = 0;
    TimeMs last_report_ms = 0;

    bool armed = true;           // false during cooldown
    bool pending_report = false; // latched until consumer acks
};

inline uint8_t PetMoodMatchRatioPct(const PetMoodWindowState& s) {
    if (s.sample_count == 0) {
        return 0;
    }
    return static_cast<uint8_t>((s.match_count * 100u) / s.sample_count);
}

inline bool PetMoodWindowReady(const PetMoodWindowState& s) {
    if (!s.armed || s.pending_report) {
        return false;
    }
    if (s.sample_count < s.cfg.min_samples) {
        return false;
    }
    return PetMoodMatchRatioPct(s) >= s.cfg.min_ratio_pct;
}

// ---------------------------------------------------------------------------
// 4) Debounced vision events
// ---------------------------------------------------------------------------

enum class VisionEventType : uint8_t {
    kNone = 0,

    // Local-only (drive eyes/head/LED; do not wake Xiaozhi)
    kFaceAppeared,
    kFaceLost,
    kPoseChanged,
    kEmotionChanged,
    kGazeTrack,  // high-rate track update (cx/cy)

    // Reportable to Xiaozhi (after gate)
    kSustainedPositiveMood,
    kSustainedNegativeMood,
    kUserApproaching,
    kHighValueExpression,  // short but strong surprise/cry etc.
};

enum class VisionEventSink : uint8_t {
    kLocalBehavior = 1 << 0,
    kXiaozhiWake = 1 << 1,
    kPhotoExplain = 1 << 2,
};

inline VisionEventSink operator|(VisionEventSink a, VisionEventSink b) {
    return static_cast<VisionEventSink>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}

inline bool HasSink(VisionEventSink mask, VisionEventSink bit) {
    return (static_cast<uint8_t>(mask) & static_cast<uint8_t>(bit)) != 0;
}

struct PetVisionEvent {
    VisionEventType type = VisionEventType::kNone;
    VisionEventSink sinks = VisionEventSink::kLocalBehavior;
    TimeMs timestamp_ms = 0;

    FacePose pose = FacePose::kNone;
    EmotionLabel emotion = EmotionLabel::kUnknown;
    uint8_t confidence_pct = 0;

    NormQ15 cx = 0;
    NormQ15 cy = 0;

    /** Text payload for WakeWordInvoke, e.g. "<detect>sustained_positive_mood</detect>" */
    char detect_tag[48] = {0};
};

/**
 * Gate policy for cloud / dialog side. Local behavior ignores most of this.
 * Values are starting points; tune per product.
 */
struct PetVisionGateConfig {
    uint32_t confirm_hits = 3;          // enter state after N consistent samples
    uint32_t exit_misses = 5;           // leave state after N misses

    uint32_t local_event_min_interval_ms = 100;     // pose/emotion local spam limit
    uint32_t gaze_publish_interval_ms = 100;        // ~10 Hz to behavior

    uint32_t wake_cooldown_ms = 180 * 1000;         // dialog trigger cool-down
    uint32_t explain_cooldown_ms = 120 * 1000;

    bool wake_only_in_idle = true;
    bool suppress_wake_while_speaking = true;
};

struct PetVisionGateState {
    PetVisionGateConfig cfg{};

    FacePose stable_pose = FacePose::kNone;
    EmotionLabel stable_emotion = EmotionLabel::kUnknown;
    uint32_t pose_hit = 0;
    uint32_t pose_miss = 0;
    uint32_t emotion_hit = 0;
    uint32_t emotion_miss = 0;

    TimeMs last_local_event_ms = 0;
    TimeMs last_gaze_publish_ms = 0;
    TimeMs last_wake_ms = 0;
    TimeMs last_explain_ms = 0;
};

// ---------------------------------------------------------------------------
// Optional short history (disable or shrink on S3 if RAM tight)
// ---------------------------------------------------------------------------

// Override in board CMake / compile defs if needed (S3 can use 8, P4 16).
#ifndef PET_VISION_HISTORY_LEN
#define PET_VISION_HISTORY_LEN 8
#endif

struct PetVisionHistory {
    PetVisionSample items[PET_VISION_HISTORY_LEN]{};
    uint8_t head = 0;
    uint8_t count = 0;
};

inline void PetVisionHistoryPush(PetVisionHistory* h, const PetVisionSample& s) {
    if (h == nullptr) {
        return;
    }
    h->items[h->head] = s;
    h->head = static_cast<uint8_t>((h->head + 1) % PET_VISION_HISTORY_LEN);
    if (h->count < PET_VISION_HISTORY_LEN) {
        ++h->count;
    }
}

}  // namespace pet
