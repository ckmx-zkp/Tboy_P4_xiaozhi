#ifndef _BOARD_CONFIG_H_
#define _BOARD_CONFIG_H_

#include <driver/gpio.h>

// Audio (same as Waveshare ESP32-P4 7B)
#define AUDIO_INPUT_SAMPLE_RATE  24000
#define AUDIO_OUTPUT_SAMPLE_RATE 24000
#define AUDIO_INPUT_REFERENCE    true

#define AUDIO_I2S_GPIO_MCLK GPIO_NUM_13
#define AUDIO_I2S_GPIO_WS   GPIO_NUM_10
#define AUDIO_I2S_GPIO_BCLK GPIO_NUM_12
#define AUDIO_I2S_GPIO_DIN  GPIO_NUM_11
#define AUDIO_I2S_GPIO_DOUT GPIO_NUM_9

#define AUDIO_CODEC_PA_PIN       GPIO_NUM_53
#define AUDIO_CODEC_I2C_SDA_PIN  GPIO_NUM_7
#define AUDIO_CODEC_I2C_SCL_PIN  GPIO_NUM_8
#define AUDIO_CODEC_ES8311_ADDR  ES8311_CODEC_DEFAULT_ADDR
#define AUDIO_CODEC_ES7210_ADDR  ES7210_CODEC_DEFAULT_ADDR

#define BOOT_BUTTON_GPIO         GPIO_NUM_35

// Single 1.28" GC9A01 eye (user wiring for bring-up)
// SCL->IO2, SDA->IO3, DC->IO4, CS->IO5, RST->IO28
#define DISPLAY_WIDTH            240
#define DISPLAY_HEIGHT           240
#define DISPLAY_SPI_SCLK_PIN     GPIO_NUM_2
#define DISPLAY_SPI_MOSI_PIN     GPIO_NUM_3
#define DISPLAY_SPI_DC_PIN       GPIO_NUM_4
#define DISPLAY_SPI_CS_PIN       GPIO_NUM_5
#define DISPLAY_SPI_RESET_PIN    GPIO_NUM_28
#define DISPLAY_SPI_SCLK_HZ      (40 * 1000 * 1000)
#define DISPLAY_SPI_HOST         SPI2_HOST
// Optional backlight on header; -1 = not used (tie BLK to 3V3 on module)
#define DISPLAY_BACKLIGHT_PIN    GPIO_NUM_NC
#define DISPLAY_BACKLIGHT_OUTPUT_INVERTED false

#define DISPLAY_MIRROR_X         false
#define DISPLAY_MIRROR_Y         false
#define DISPLAY_SWAP_XY          false
#define DISPLAY_OFFSET_X         0
#define DISPLAY_OFFSET_Y         0

// 7" MIPI LCD is intentionally NOT used on this board variant.

#endif // _BOARD_CONFIG_H_
