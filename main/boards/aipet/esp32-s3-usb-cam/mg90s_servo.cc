#include "config.h"
#include "mg90s_servo.h"

#include <esp_log.h>

#include <algorithm>

#define TAG "Mg90s"

bool Mg90sServo::Begin(gpio_num_t gpio) {
    const ledc_timer_config_t timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = SERVO_LEDC_RES,
        .timer_num = SERVO_LEDC_TIMER,
        .freq_hz = SERVO_PWM_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
        .deconfigure = false,
    };
    if (ledc_timer_config(&timer) != ESP_OK) {
        ESP_LOGE(TAG, "LEDC timer failed");
        return false;
    }

    const ledc_channel_config_t ch = {
        .gpio_num = gpio,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = SERVO_LEDC_CHANNEL,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = SERVO_LEDC_TIMER,
        .duty = 0,
        .hpoint = 0,
        .flags = {.output_invert = 0},
    };
    if (ledc_channel_config(&ch) != ESP_OK) {
        ESP_LOGE(TAG, "LEDC channel failed GPIO%d", (int)gpio);
        return false;
    }

    ready_ = true;
    mode_ = LEDC_LOW_SPEED_MODE;
    channel_ = SERVO_LEDC_CHANNEL;
    SetCenter();
    ESP_LOGI(TAG, "MG90S PWM GPIO%d 50Hz T1/CH1, center=%d deg",
             (int)gpio, SERVO_CENTER_DEG);
    return true;
}

uint32_t Mg90sServo::AngleToDuty(float deg) const {
    deg = std::clamp(deg, static_cast<float>(SERVO_MIN_DEG),
                     static_cast<float>(SERVO_MAX_DEG));
    const float span = static_cast<float>(SERVO_MAX_PULSE_US - SERVO_MIN_PULSE_US);
    const float us = SERVO_MIN_PULSE_US +
                     (deg - SERVO_MIN_DEG) * span / (SERVO_MAX_DEG - SERVO_MIN_DEG);
    const uint32_t max_duty = (1u << SERVO_LEDC_RES) - 1u;
    return static_cast<uint32_t>(us * max_duty / SERVO_PERIOD_US);
}

void Mg90sServo::SetAngle(float deg) {
    if (!ready_) {
        return;
    }
    deg = std::clamp(deg, static_cast<float>(SERVO_MIN_DEG),
                     static_cast<float>(SERVO_MAX_DEG));
    angle_ = deg;
    ledc_set_duty(mode_, channel_, AngleToDuty(deg));
    ledc_update_duty(mode_, channel_);
}

void Mg90sServo::SetCenter() {
    SetAngle(static_cast<float>(SERVO_CENTER_DEG));
}
