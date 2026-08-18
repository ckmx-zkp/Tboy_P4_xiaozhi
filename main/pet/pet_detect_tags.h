#pragma once

/**
 * Stable detect-tag strings for Application::WakeWordInvoke().
 * Keep short: copied into PetVisionEvent::detect_tag[48].
 */

namespace pet {
namespace detect_tags {

inline constexpr const char* kSustainedPositive =
    "<detect>sustained_positive_mood</detect>";
inline constexpr const char* kSustainedNegative =
    "<detect>sustained_negative_mood</detect>";
inline constexpr const char* kUserApproaching =
    "<detect>human_approaching</detect>";
inline constexpr const char* kEmotionHappy =
    "<detect>human_emotion_happy</detect>";
inline constexpr const char* kEmotionSad =
    "<detect>human_emotion_sad</detect>";
inline constexpr const char* kEmotionSurprised =
    "<detect>human_emotion_surprised</detect>";
inline constexpr const char* kFacePresent =
    "<detect>human_face_present</detect>";

}  // namespace detect_tags
}  // namespace pet
