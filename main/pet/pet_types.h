#pragma once

/**
 * Shared pet-domain types for ESP32-P4 and ESP32-S3 ports.
 *
 * Design goals:
 * - POD / trivially copyable where possible (FreeRTOS queues, ISR-safe copies).
 * - No heap ownership inside these structs.
 * - Backend-agnostic: K230 UART on both chips; optional CSI/ESP-DL only on P4.
 */

#include <stdint.h>

namespace pet {

/** Which SoC / board capability set is compiled in. */
enum class PlatformId : uint8_t {
    kEsp32P4AiPet = 0,
    kEsp32S3Wroom2 = 1,
};

/**
 * Vision backend. S3 port is expected to use K230 only;
 * P4 may keep OV5647 Capture/Explain as a separate passive path.
 */
enum class VisionBackend : uint8_t {
    kNone = 0,
    kK230Uart = 1,
    kLocalEspDl = 2,  // P4-oriented; typically disabled on S3
};

/** Discrete face / head pose used by local behavior (not cloud text). */
enum class FacePose : uint8_t {
    kNone = 0,
    kCenter,
    kLeft,
    kRight,
    kUp,
    kDown,
    kClose,
    kFar,
};

/**
 * Emotion labels aligned with K230 face_emotion style classes.
 * Keep stable numeric values for flash logs / NVS if needed later.
 */
enum class EmotionLabel : uint8_t {
    kUnknown = 0,
    kNeutral,
    kHappy,
    kAngry,
    kSad,
    kSurprised,
    kFear,      // optional K230 class; may map to local "concern"
    kDisgust,   // optional
    kJoy,       // pet-facing alias / stronger happy
};

/** Who requested the current behavior intent (arbitration source). */
enum class BehaviorSource : uint8_t {
    kIdleAuto = 0,
    kVisionLocal = 1,
    kDeviceState = 2,
    kMcpUser = 3,
    kSafety = 4,
};

/** Arbitration priority bands (higher wins). */
enum class BehaviorPriority : uint8_t {
    kIdle = 10,
    kVision = 50,
    kDialogState = 60,
    kMcp = 80,
    kSleepLock = 90,
    kSafety = 100,
};

/** Normalized direction in Q0.15 style: -32768..32767 ≈ -1.0..~1.0 */
using NormQ15 = int16_t;

inline constexpr NormQ15 kNormQ15One = 32767;

inline NormQ15 FloatToNormQ15(float v) {
    if (v >= 1.0f) {
        return kNormQ15One;
    }
    if (v <= -1.0f) {
        return static_cast<NormQ15>(-32768);
    }
    return static_cast<NormQ15>(v * 32767.0f);
}

inline float NormQ15ToFloat(NormQ15 v) {
    return static_cast<float>(v) / 32767.0f;
}

/** Milliseconds since boot (esp_timer_get_time()/1000). */
using TimeMs = int64_t;

}  // namespace pet
