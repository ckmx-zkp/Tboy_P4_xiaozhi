#ifndef _BOARD_CONFIG_H_
#define _BOARD_CONFIG_H_

#include <driver/gpio.h>
#include <driver/ledc.h>
#include <driver/uart.h>

// AI Pet ESP32-S3 USB 摄像头大板
// 原理图：D:/User/SCH_USB摄像头大板_2026-09-13.pdf
// 旧网表：hardware/Netlist_USB摄像头大板_2026-08-25.tel（屏脚以 09-13 原理图为准）
// 模组：ESP32-S3-WROOM-2-N32R16V（U3，32MB Flash + 16MB PSRAM）
// GPIO 以模组脚位为准，不以网表丝印 IO4_TOUCH4 等名称为准。

#define AUDIO_INPUT_SAMPLE_RATE  24000
#define AUDIO_OUTPUT_SAMPLE_RATE 24000
// ES7210 MIC3 接到功放 OUTP/OUTN，可作为 AEC 参考
#define AUDIO_INPUT_REFERENCE    true

#define AUDIO_I2S_GPIO_MCLK GPIO_NUM_38
#define AUDIO_I2S_GPIO_WS   GPIO_NUM_13
#define AUDIO_I2S_GPIO_BCLK GPIO_NUM_14
#define AUDIO_I2S_GPIO_DIN  GPIO_NUM_12
#define AUDIO_I2S_GPIO_DOUT GPIO_NUM_45

// 原理图 PA_EN 接到模组脚 16 = GPIO46。ESP32-S3 的 GPIO46 为仅输入，
// 软件拉不了 NS4150B CTRL。当前按 NC 处理，等改版飞线到 GPIO9/21。
#define AUDIO_CODEC_PA_PIN       GPIO_NUM_NC
#define AUDIO_CODEC_PA_PIN_SCH   GPIO_NUM_46
#define AUDIO_CODEC_I2C_SDA_PIN  GPIO_NUM_1
#define AUDIO_CODEC_I2C_SCL_PIN  GPIO_NUM_2
#define AUDIO_CODEC_ES8311_ADDR  ES8311_CODEC_DEFAULT_ADDR  // 7-bit 0x18
// 原理图 AD0=1 → 7-bit 0x41。驱动用 8-bit 写地址，默认 0x80 是 0x40。
#define AUDIO_CODEC_ES7210_ADDR  0x82

#define BOOT_BUTTON_GPIO         GPIO_NUM_0   // SW2
#define USER_BUTTON_GPIO         GPIO_NUM_4   // SW3 / KEY1

// 双眼 XJ0.99TFT-12P / GC9107，4-line SPI；资料 hardware/XJ0.99TFT-12P/
// 资料写玻璃 128×115，IC GRAM 固定 128×160。圆屏实际能看到整段 160 行；
// 只刷 115 行会下半花屏，115 贴顶会偏上，放大到 160 高会顶边显大。
// 资产：108×108 圆眼贴进 128×160，相对中心再下移 20px。
// RES 与 ESP EN 短接，软件不再单独复位。
#define DISPLAY_WIDTH            128
#define DISPLAY_HEIGHT           160
#define DISPLAY_GRAM_HEIGHT      160
// 09-13 原理图：FPC9 SCL→IO40_LCD_MOSI，FPC10 SDA→IO41_LCD_SCK（网表名与玻璃脚对反）
#define DISPLAY_SPI_SCLK_PIN     GPIO_NUM_40
#define DISPLAY_SPI_MOSI_PIN     GPIO_NUM_41
#define DISPLAY_SPI_DC_PIN       GPIO_NUM_39
#define DISPLAY_SPI_CS1_PIN      GPIO_NUM_17  // FPC5 左眼，原理图 IO17_LCD_CS1
#define DISPLAY_SPI_CS2_PIN      GPIO_NUM_16  // FPC2 右眼，原理图 IO16_LCD_CS2
#define DISPLAY_SPI_RESET_PIN    GPIO_NUM_NC
#define DISPLAY_SPI_SCLK_HZ      (10 * 1000 * 1000)
#define DISPLAY_SPI_HOST         SPI2_HOST
#define DISPLAY_BACKLIGHT_PIN    GPIO_NUM_42
// IO42 → 1k → SI2301 门极（10k 上拉 3V3）：低电平点亮
#define DISPLAY_BACKLIGHT_OUTPUT_INVERTED true
#define DISPLAY_INVERT_COLOR     false
#define DISPLAY_MIRROR_X         false
#define DISPLAY_MIRROR_Y         false
#define DISPLAY_SWAP_XY          false
#define DISPLAY_OFFSET_X         0
#define DISPLAY_OFFSET_Y         0

// 网表丝印 IO4/5/6_TOUCH* 实际是模组脚 5/6/7 = GPIO5/6/7
#define TOUCH_PAD1_GPIO          GPIO_NUM_5
#define TOUCH_PAD2_GPIO          GPIO_NUM_6
#define TOUCH_PAD3_GPIO          GPIO_NUM_7

#define WS2812_DIN_GPIO          GPIO_NUM_3
#define WS2812_EN_GPIO           GPIO_NUM_15  // LED_EN，高电平给灯带供电
#define WS2812_LED_COUNT         2
// 原理图仅一路舵机 CN2（PH 3P）：1=GND，2=SE_PWMO=GPIO8，3=5V 经 D5。
// 型号 MG90S：50Hz，脉宽约 0.5–2.5ms 对应 0–180°。背光占用 LEDC T0/CH0，舵机用 T1/CH1。
#define SERVO_PWM_GPIO           GPIO_NUM_8
#define SERVO_LEDC_TIMER         LEDC_TIMER_1
#define SERVO_LEDC_CHANNEL       LEDC_CHANNEL_1
#define SERVO_PWM_HZ             50
#define SERVO_LEDC_RES           LEDC_TIMER_13_BIT
#define SERVO_MIN_PULSE_US       500
#define SERVO_MAX_PULSE_US       2500
#define SERVO_PERIOD_US          20000
#define SERVO_MIN_DEG            0
#define SERVO_MAX_DEG            180
#define SERVO_CENTER_DEG         90
#define SERVO_TRACK_SPAN_DEG     45    // 跟随限幅：中位 ±45°
#define SERVO_PAN_INVERT         0     // 1=人脸在左时舵机反转
#define SERVO_SLEW_DEG_PER_S     80
#define SERVO_FACE_LOST_MS       800
#define SERVO_DEADZONE           0.08f

// UART0：U0TXD/U0RXD → CH340K，调试口，IDF 控制台已开。
// UART1：IO10_U1TXD / IO11_U1RXD → K230 GPIO45 RX / GPIO44 TX（M1.131 / M1.130）。
// UART2：IO48_U2TXD / IO47_U2RXD → ML307 UART0 RX/TX，无电平转换；原理图仍标 1.8V?。
#define K230_UART_NUM            UART_NUM_1
#define K230_UART_TX_GPIO        GPIO_NUM_10
#define K230_UART_RX_GPIO        GPIO_NUM_11
#define K230_UART_BAUD           115200
#define ML307_UART_NUM           UART_NUM_2
#define ML307_UART_TX_GPIO       GPIO_NUM_48  // S3 TX → 4G_UART0_RXD → U28.17
#define ML307_UART_RX_GPIO       GPIO_NUM_47  // S3 RX ← 4G_UART0_TXD ← U28.18
#define ML307_UART_BAUD          115200
// 4G_PWR → TPS562200 EN（U14.5），R66 1M 下拉。高电平出 +4V VBAT。
// ML307 PWR_ON/OFF 经 R56 4.7k 接地，上电后应自动开机，S3 无需再脉冲。
#define ML307_PWR_GPIO           GPIO_NUM_18
// USB 供电时模组没有 9V/VBAT，AT 探测只会空转。需要测 4G 时改为 1。
#ifndef BOARD_ENABLE_4G_TEST
#define BOARD_ENABLE_4G_TEST     0
#endif

#define USB_DMINUS_GPIO          GPIO_NUM_19
#define USB_DPLUS_GPIO           GPIO_NUM_20
// U4 模组横出 640×480，安装后画面横置。1=顺时针 90°，3=顺时针 270°，0=不转。
#define CAMERA_ROTATE_90         1

// 空闲脚，改版建议把 PA_EN 改到其中之一
#define UNUSED_GPIO_9            GPIO_NUM_9
#define UNUSED_GPIO_21           GPIO_NUM_21

#endif // _BOARD_CONFIG_H_
