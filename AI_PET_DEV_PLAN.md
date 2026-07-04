# Xiaozhi ESP32-P4 AI Pet Development Plan

## Summary

This plan is based on the current `xiaozhi-esp32` project and the target Waveshare ESP32-P4-WIFI6-Touch-LCD-7B hardware.

The current 7B board support already covers ESP32-P4, WiFi6/BT coprocessor, ES8311/ES7210 audio, OV5647 CSI camera, and the original 7-inch MIPI LCD path. The AI pet version should be added as an independent board variant instead of modifying the existing 7B board in place.

The AI pet variant will reuse the P4, network, audio, and camera base, disable the 7-inch LCD by default, and use two 1.28-inch TFT screens as eyes. It will also add one WS2812 LED strip output and servo control for simple physical expressions.

Default assumptions:

- Eye screens: two 1.28-inch GC9A01 240x240 round TFT modules.
- 7-inch MIPI LCD: not initialized by default.
- Servo control: two channels for the first version.
- Camera behavior: active recognition is implemented as event/interval photo capture plus vision explanation, not continuous video streaming.

Waveshare board reference:

- https://docs.waveshare.net/ESP32-P4-WIFI6-Touch-LCD-7B/

## Key Changes

### Board Variant

- Add a new board type: `BOARD_TYPE_WAVESHARE_ESP32_P4_AI_PET`.
- Suggested board directory: `main/boards/waveshare/esp32-p4-ai-pet`.
- Derive the implementation from `main/boards/waveshare/esp32-p4-wifi6-touch-lcd`.
- Keep the existing `esp32-p4-wifi6-touch-lcd-7b` support unchanged for regression safety.

### Base Hardware Support

- Reuse existing ESP32-P4 network, audio codec, BOOT button, and OV5647 CSI camera initialization.
- Do not initialize the 7-inch MIPI LCD and GT911 touch path by default.
- Keep a Kconfig switch for optional 7-inch LCD debug use only if needed later.

### Dual Eye Display

- Add a dedicated pet eye display layer, for example `PetEyeDisplay`.
- Use one shared SPI bus for both TFT screens.
- Use separate chip-select pins for left and right eyes.
- Make DC, reset, and backlight pins configurable.
- First expression set:
  - `neutral`
  - `listening`
  - `speaking`
  - `thinking`
  - `sleepy`
  - `error`
- First animation set:
  - blink
  - pupil movement
  - speaking pulse
  - recognition/thinking scan

### WS2812 LED Strip

- Reuse the existing `CircularStrip` / `led_strip` RMT path where possible.
- Add configurable GPIO and LED count.
- Map device states to pet lighting effects:
  - starting: soft white or blue breathing
  - Wi-Fi configuring: blue blink
  - idle: low brightness breathing
  - listening: cyan active effect
  - speaking: warm color pulse
  - recognizing: moving scan
  - error: red blink

### Servo Control

- Add a lightweight servo controller based on LEDC PWM at 50 Hz.
- First version supports two channels.
- Provide:
  - `SetAngle(channel, degree)`
  - `MoveToPreset(name)`
  - configurable min, max, and center pulse widths
  - angle limiting to protect the mechanism
- Suggested first presets:
  - `center`
  - `look_left`
  - `look_right`
  - `nod`
  - `curious`
  - `sleep`

The servo power supply must be external. ESP32-P4 GPIO should only output PWM signals.

### Active Vision

- Keep the current camera integration through `Camera::Capture()` and `Camera::Explain()`.
- Add a pet perception controller that can trigger camera capture:
  - on user request
  - on idle interval
  - on specific pet behavior events
  - through the existing MCP camera tool path
- Add a cooldown period to avoid repeated vision uploads.
- Do not implement continuous video streaming in the first version.

### Pet Controller

Add a behavior orchestration layer, for example `PetController`, to coordinate:

- device state changes
- eye expression
- LED strip effect
- servo motion
- camera recognition state
- future pet-specific behaviors

This keeps pet behavior out of `Application` and avoids scattering hardware logic across unrelated modules.

## Public Interfaces

### Kconfig

Add configuration entries for:

- `BOARD_TYPE_WAVESHARE_ESP32_P4_AI_PET`
- `PET_EYE_LCD_TYPE_GC9A01`
- `PET_EYE_SPI_HOST`
- `PET_EYE_MOSI_GPIO`
- `PET_EYE_SCLK_GPIO`
- `PET_EYE_DC_GPIO`
- `PET_EYE_RST_GPIO`
- `PET_EYE_LEFT_CS_GPIO`
- `PET_EYE_RIGHT_CS_GPIO`
- `PET_EYE_BACKLIGHT_GPIO`
- `PET_WS2812_GPIO`
- `PET_WS2812_COUNT`
- `PET_SERVO1_GPIO`
- `PET_SERVO2_GPIO`
- `PET_SERVO_MIN_US`
- `PET_SERVO_MAX_US`
- `PET_SERVO_CENTER_US`
- `PET_ACTIVE_VISION_INTERVAL_SEC`
- `PET_ACTIVE_VISION_COOLDOWN_SEC`

Final GPIO values should come from the actual wiring. The software should make them configurable rather than hardcoding unfinished pin choices.

### Board Overrides

The AI pet board variant should override:

- `GetDisplay()` to return the dual eye display.
- `GetLed()` to return the WS2812 strip.
- `GetCamera()` to keep returning the existing camera.
- `GetAudioCodec()` to reuse the existing ES8311/ES7210 audio codec path.

### Optional MCP Tools

After the hardware baseline is stable, add user-facing MCP tools:

- `self.pet.set_expression`
- `self.pet.move_servo`
- `self.pet.set_light_effect`
- `self.pet.observe`

These are not required for the first hardware bring-up.

## Milestones

### 1. Hardware Baseline Build

- Add the new board type and board directory.
- Reuse the current 7B audio, network, and camera setup.
- Disable 7-inch LCD initialization in the AI pet variant.
- Build and flash the board.
- Validate boot, Wi-Fi, audio, and camera capture.

### 2. Dual Eye Display Bring-Up

- Initialize the shared SPI bus.
- Bring up the left GC9A01 screen.
- Bring up the right GC9A01 screen.
- Render static eyes.
- Add basic expression changes from device state.

### 3. LED Strip and Servo Bring-Up

- Initialize WS2812 strip using configurable GPIO and LED count.
- Add state-based LED effects.
- Initialize two LEDC servo PWM channels.
- Validate center/min/max angles.
- Add simple movement presets.

### 4. Active Recognition

- Add idle/event-triggered camera capture.
- Use existing vision explanation flow.
- Add recognition cooldown.
- Show recognition state through eyes and LEDs.
- Trigger simple servo reaction from recognition result.

### 5. Pet Experience Layer

- Add blink and idle eye animation.
- Add speaking eye pulse.
- Add small servo movement while speaking.
- Add curious and sleepy behavior loops.
- Add error and low-power behavior.

## Test Plan

### Build Tests

- Build the existing `esp32-p4-wifi6-touch-lcd-7b` target to confirm no regression.
- Build the new `esp32-p4-ai-pet` target.
- Confirm both board types remain selectable through menuconfig/config JSON.

### Hardware Smoke Tests

- Boot log reaches normal application state.
- Wi-Fi provisioning and connection work.
- Microphone input and speaker output work.
- Camera can capture and upload a photo for explanation.
- Left and right eye screens display independently.
- WS2812 strip can set all LEDs and run effects.
- Servo 1 and Servo 2 move to center/min/max safely.

### State Integration Tests

- Starting state updates eyes and LEDs.
- Wi-Fi configuring state shows distinct feedback.
- Idle state is calm and low power.
- Listening state shows active attention.
- Speaking state animates eyes and servo motion.
- Recognition state shows camera activity feedback.
- Error state is visible through eyes and LEDs.

### Stability Tests

- Run idle animation for 30 minutes.
- Run repeated camera capture with cooldown for 30 minutes.
- Confirm no obvious memory leak or task crash.
- Confirm SPI eye refresh does not break audio.
- Confirm servo PWM does not interfere with WS2812 output.

## Assumptions and Constraints

- `W2812` in the requirement is treated as `WS2812`.
- The first version focuses on usable pet interaction, not full robot motion.
- Continuous video streaming is out of scope for the first version.
- Local vision inference is out of scope for the first version.
- Final GPIO allocation depends on the actual wiring and available pin breakout on the assembled hardware.
- Servo power must not be taken directly from the ESP32-P4 GPIO rail.
