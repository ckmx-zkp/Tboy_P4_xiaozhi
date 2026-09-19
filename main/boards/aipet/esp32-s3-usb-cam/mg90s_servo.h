#ifndef _MG90S_SERVO_H_
#define _MG90S_SERVO_H_

#include "config.h"

#include <driver/gpio.h>
#include <driver/ledc.h>
#include <cstdint>

// 原理图 CN2 上的 MG90S：GPIO8 / LEDC 50Hz。只提供角度与脉宽，跟随逻辑在 FaceFollow。
class Mg90sServo {
public:
    bool Begin(gpio_num_t gpio);
    void SetAngle(float deg);
    void SetCenter();
    float angle() const { return angle_; }
    bool ready() const { return ready_; }

private:
    uint32_t AngleToDuty(float deg) const;

    bool ready_ = false;
    float angle_ = SERVO_CENTER_DEG;
    ledc_mode_t mode_ = LEDC_LOW_SPEED_MODE;
    ledc_channel_t channel_ = SERVO_LEDC_CHANNEL;
};

#endif  // _MG90S_SERVO_H_
