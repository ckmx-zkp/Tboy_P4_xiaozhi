# AI Pet ESP32-S3 USB 摄像头大板

独立板型，不改 P4 / BOX-3 / LCD EV Board。

| 项 | 值 |
|----|-----|
| 原理图 | `D:/User/SCH_USB摄像头大板_2026-09-13.pdf` |
| 网表 | `D:/Home_Work/hardware/Netlist_USB摄像头大板_2026-08-25.tel` |
| 眼睛屏 | `D:/Home_Work/hardware/XJ0.99TFT-12P/`（XJ0.99TFT-12P / GC9107 / GRAM 128×160，圆屏按 160 行居中铺满） |
| 模组 U3 | ESP32-S3-WROOM-2-N32R16V |
| Kconfig | `BOARD_TYPE_AIPET_ESP32_S3_USB_CAM` |
| 本目录 | `main/boards/aipet/esp32-s3-usb-cam/` |

当前代码：WiFi + 音频 + 双眼 C1 + 两颗 WS2812。**P1**：`self.eye.set_emotion` 同轮改灯；`self.eye.set_blink_profile` 可调眨眼间隔。**当前嵌入皮肤=巨蟹座**（`image/zodiac/cancer`，128×160）。天蝎已验素材并烧过；双鱼已转好未嵌入。无运行时换皮肤。CN2 跟随代码仍在，舵机/触摸硬件未接。UART1 收 K230 JSON。4G 起板探测由 `BOARD_ENABLE_4G_TEST` 控制，**当前为 0**。MIC3 回采试验已撤回，参考通道仍是 MIC2。

## S3 GPIO 对照（模组脚 → 功能）

| GPIO | 网表名 | 功能 | 本轮软件 |
|------|--------|------|----------|
| 0 | IO0_BOOT | BOOT 键 SW2 | 已接：配网 / 开关对话 |
| 1 | IO1_I2C_SDA | ES8311 0x18 + ES7210 0x41（驱动 8-bit `0x82`） | 已接，真机 ACK |
| 3 | IO3_LED_OUT | WS2812 DIN（2 颗） | 已接；MCP `self.led.*` |
| 4 | KEY1 | 用户键 SW3 | 仅宏 |
| 5 / 6 / 7 | IO4/5/6_TOUCH* | 电容触摸 CN1（S3 TOUCH4/5/6） | `BOARD_ENABLE_TOUCH_TEST=1`，摸到打日志 |
| 8 | SE_PWMO | 舵机 CN2 信号（MG90S，50Hz LEDC T1/CH1） | MCP `self.servo.turn_left/turn_right`；默认识别跟随 |
| 9 | （未命名） | 空闲，建议改版给 PA_EN | 未用 |
| 10 | IO10_U1TXD | UART1 TX → K230 GPIO45 RX | 已开 115200 |
| 11 | IO11_U1RXD | UART1 RX ← K230 GPIO44 TX | 已开 115200 |
| 12 | IO12_I2S_DI | ES7210 SDOUT | 已接 |
| 13 | IO13_I2S_WS | 共享 WS | 已接 |
| 14 | IO14_I2S_BCK | 共享 BCLK | 已接 |
| 15 | LED_EN | 灯带供电使能，上电拉高 | 已拉高 |
| 16 | IO16_LCD_CS2 | 右眼 CS / FPC2 | 已接 GC9107 |
| 17 | IO17_LCD_CS1 | 左眼 CS / FPC5 | 已接 GC9107 |
| 18 | 4G_PWR | TPS562200 EN，高电平出 +4V VBAT | `BOARD_ENABLE_4G_TEST=0`，不驱动 |
| 19 / 20 | IO19_USB_D− / IO20_USB_D+ | S3 原生 USB Host，插座 U4 | 量产固件未用；hwtest `cam` |
| 21 | （未命名） | 空闲，建议改版给 PA_EN | 未用 |
| 38 | IO38_I2S_MCK | MCLK | 已接 |
| 39 | IO39_LCD_DC | 双眼共用 DC | 已接 |
| 40 | IO40_LCD_MOSI | 双眼 SCL（玻璃时钟，网表名叫 MOSI） | SCLK |
| 41 | IO41_LCD_SCK | 双眼 SDA（玻璃数据，网表名叫 SCK） | MOSI |
| 42 | IO42_LCD_BL | 双眼背光（P-MOS，低电平亮） | 已接，PWM 反相 |
| 45 | IO45_I2S_DO | ES8311 DIN | 已接 |
| 46 | PA_EN | 原理图接 NS4150B CTRL，高电平开 | 播放时拉高，空闲拉低 |
| 47 | IO47_U2RXD / 4G_UART0_TXD | UART2 RX ← ML307 UART0_TXD | 关（`BOARD_ENABLE_4G_TEST=0`） |
| 48 | IO48_U2TXD / 4G_UART0_RXD | UART2 TX → ML307 UART0_RXD | 关（`BOARD_ENABLE_4G_TEST=0`） |

双眼是 **XJ0.99TFT-12P**（0.99 寸，驱动 **GC9107**，资料写玻璃 **128×RGB×115**，IC GRAM **128×160**，4-line SPI），不是 P4 的 GC9A01 240×240，也不能套 magiclick 那套 128×128 初始化。厂方序列在 `gc9107_boe_099_init.h`（来自 `XJ0.99TFT-12P/GC9107_BOE.99_IPS(1)(1).txt`，`0x3A=0x05` RGB565）。圆屏实际能看到整段 160 行：只刷 115 行会下半花屏，115 贴顶会整只眼睛偏上，放大到 160 高会顶边显大。资产把约 108×108 圆眼贴进 128×160，相对中心再下移 20px。FPC 12 针与 FPC2/FPC5 一致：1 GND、2 LEDK、3 LEDA、4 VDD、5–6 GND、7 D/C、8 CS、9 SCL、10 SDA、11 RESET、12 GND。**09-13 原理图**：FPC9 SCL 接到 GPIO40（网表名 MOSI），FPC10 SDA 接到 GPIO41（网表名 SCK），软件按玻璃脚接线，不按网表名。RES 接到 ESP `EN`，软件复位脚 `GPIO_NUM_NC`。背光两颗白光 LED，经 Q4/Q5（SI2301）低电平点亮。`GetDisplay()` 仍是 `NoDisplay`，不把聊天 UI 画到眼睛上。P4 的 240×240 C1 资产不能直接用。USB 摄像头插座 **U4** 接 S3 原生 USB（GPIO19 D− / GPIO20 D+）。K230 另有 CSI（FPC3）和 USB1 Type-C，不要和 U4 混插。

## 硬件门禁（未关不得当量产）

1. **功放使能**：`PA_EN` 在 GPIO46，软件作输出。播 PCM 时拉高，空闲拉低。不要再把该脚硬接到 3.3V，否则会和输出对打。
2. **4G UART 电平**：原理图仍标 `1.8V?`，网表是 S3 GPIO47/48 直连 ML307 UART0，中间没有转换芯片。`PWR_ON/OFF` 经 R56 4.7k 接地，给 VBAT 后应自动开机。已拉高 `4G_PWR`。真机 K230 JSON 已通，ML307 `AT` 无回包：先量 SW4 VIN 与 U14 `+4V`，再量模组 `UART0_TXD` 空闲电平（1.8V 低于 S3 Vih≈2.5V 时软件收不到）。
3. **S3 assets mmap**：`partitions/v2/32m.csv` 的 assets 是 16MB，S3 MMU 空闲页约 14MB，整分区映射会失败并关掉唤醒词。量产固件已按实际资源长度映射，不要再整分区 mmap。
4. **真机**：COM25 / MAC `d8:85:ac:ba:85:d8` 已配 `TP-LINK_C738` 并激活。
5. **U4 UVC**：须关 `USJ`。USB Host 必须在 I2S/WiFi 之前安装，否则会 `No free interrupt inputs`。已挂 `self.camera.take_photo`，一次连拍 3 帧、顺时针转 90° 再竖拼上传。插 U4。服务端 hello 需带 `vision.url`。画面仍横着就把 `CAMERA_ROTATE_90` 改成 `3`。
6. **舵机 CN2**：仅一路。1=GND，2=GPIO8 PWM，3=5V 经 D5。MG90S 堵转约 0.7A，USB 5V 只够轻载。转反了把 `SERVO_PAN_INVERT` 设 1。没有第二路 PWM，不能做俯仰。
