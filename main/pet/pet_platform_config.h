#pragma once

/**
 * Compile-time / board-time capability matrix for P4 AI Pet vs S3 WROOM-2.
 *
 * ESP32-S3-WROOM-2 R16N32 implications baked into defaults:
 * - Prefer K230 UART vision (no MIPI-CSI path like P4+OV5647).
 * - Fewer UARTs (typically 3): console + K230; avoid extra debug UARTs.
 * - Tighter internal SRAM: smaller RX/history; Q15 instead of float in hot structs.
 * - Servo/LED/eye still optional via capability flags.
 */

#include "pet/pet_types.h"
#include "pet/vision/pet_vision_types.h"

#include <stdint.h>

namespace pet {

struct PetUartConfig {
    int uart_port = 1;       // never 0 if console uses UART0
    int tx_gpio = -1;        // board fills these
    int rx_gpio = -1;
    int baud_rate = 115200;
    uint16_t rx_buf_bytes = 1024;
    uint16_t tx_buf_bytes = 256;
    uint16_t line_max_bytes = 384;  // one JSON line max
};

struct PetPlatformCapabilities {
    PlatformId platform = PlatformId::kEsp32P4AiPet;
    VisionBackend vision_backend = VisionBackend::kK230Uart;

    bool has_dual_eye = false;
    bool has_servo = false;
    bool has_ws2812 = false;
    bool has_onboard_csi_camera = true;  // P4 OV5647; false on S3 pet port
    bool has_photo_explain = true;       // needs CSI + cloud vision URL

    PetUartConfig k230{};
    PetMoodWindowConfig positive_mood{};
    PetMoodWindowConfig negative_mood{};
    PetVisionGateConfig gate{};
};

/** Defaults for current Waveshare ESP32-P4 AI Pet bring-up. */
inline PetPlatformCapabilities MakeP4AiPetCapabilities() {
    PetPlatformCapabilities c{};
    c.platform = PlatformId::kEsp32P4AiPet;
    c.vision_backend = VisionBackend::kK230Uart;
    c.has_dual_eye = false;
    c.has_servo = false;
    c.has_ws2812 = false;
    c.has_onboard_csi_camera = true;
    c.has_photo_explain = true;

    c.k230.uart_port = 1;
    c.k230.tx_gpio = -1;  // fill after pin pick (e.g. 46/47)
    c.k230.rx_gpio = -1;
    c.k230.baud_rate = 115200;
    c.k230.rx_buf_bytes = 2048;
    c.k230.line_max_bytes = 384;

    c.positive_mood.target = EmotionLabel::kHappy;
    c.positive_mood.window_ms = 5 * 60 * 1000;
    c.positive_mood.min_ratio_pct = 70;
    c.positive_mood.min_conf_pct = 60;
    c.positive_mood.min_samples = 30;
    c.positive_mood.cooldown_ms = 15 * 60 * 1000;

    c.negative_mood.target = EmotionLabel::kSad;
    c.negative_mood.window_ms = 60 * 1000;  // faster care response
    c.negative_mood.min_ratio_pct = 65;
    c.negative_mood.min_conf_pct = 60;
    c.negative_mood.min_samples = 20;
    c.negative_mood.cooldown_ms = 10 * 60 * 1000;

    return c;
}

/**
 * Defaults aimed at ESP32-S3-WROOM-2 R16N32 pet port.
 * Vision = K230 only; photo explain off unless a separate camera path exists.
 */
inline PetPlatformCapabilities MakeS3Wroom2Capabilities() {
    PetPlatformCapabilities c = MakeP4AiPetCapabilities();
    c.platform = PlatformId::kEsp32S3Wroom2;
    c.vision_backend = VisionBackend::kK230Uart;
    c.has_onboard_csi_camera = false;
    c.has_photo_explain = false;

    // Smaller buffers for S3 internal RAM pressure.
    c.k230.rx_buf_bytes = 1024;
    c.k230.tx_buf_bytes = 256;
    c.k230.line_max_bytes = 256;

    // Slightly fewer samples required if K230 report rate is lower.
    c.positive_mood.min_samples = 20;
    c.negative_mood.min_samples = 15;

    c.gate.gaze_publish_interval_ms = 150;  // ~6–7 Hz
    return c;
}

}  // namespace pet
