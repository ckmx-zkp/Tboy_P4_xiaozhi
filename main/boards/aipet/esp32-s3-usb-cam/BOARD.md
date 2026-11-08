# AI Pet ESP32-S3 USB 摄像头大板

独立板型，不改 P4 / BOX-3 / LCD EV Board。

| 项 | 值 |
|----|-----|
| 原理图 | `D:/User/SCH_USB摄像头大板_2026-09-13.pdf` |
| 网表 | `D:/Home_Work/hardware/Netlist_USB摄像头大板_2026-08-25.tel` |
| 眼睛屏 | `D:/Home_Work/hardware/XJ0.99TFT-12P/`（XJ0.99TFT-12P / GC9107 / 资产 128×160，当前圆屏送屏窗口 128×128） |
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
| 15 | IO15_LCD_BL | 双眼背光（P-MOS，低电平亮） | PWM 反相 |
| 16 | IO16_LCD_CS1 | 左眼 CS / FPC3 | 已接 GC9107，软件镜像 |
| 17 | IO17_LCD_CS2 | 右眼 CS / FPC1 | 已接 GC9107 |
| 18 | 4G_PWR | TPS562200 EN，高电平出 +4V VBAT | `BOARD_ENABLE_4G_TEST=0`，不驱动 |
| 19 / 20 | IO19_USB_D− / IO20_USB_D+ | S3 原生 USB Host，插座 U4 | 量产固件未用；hwtest `cam` |
| 21 | （未命名） | 空闲，建议改版给 PA_EN | 未用 |
| 38 | IO38_I2S_MCK | MCLK | 已接 |
| 39 | IO39_LCD_DC | 双眼共用 DC | 已接 |
| 40 | IO40_LCD_MOSI | 双眼 SCL（玻璃时钟，网表名叫 MOSI） | SCLK |
| 41 | IO41_LCD_SCK | 双眼 SDA（玻璃数据，网表名叫 SCK） | MOSI |
| 42 | LED_EN | 灯带供电使能，上电拉高 | 已拉高 |
| 45 | IO45_I2S_DO | ES8311 DIN | 已接 |
| 46 | PA_EN | 原理图接 NS4150B CTRL，高电平开 | 播放时拉高，空闲拉低 |
| 47 | IO47_U2RXD / 4G_UART0_TXD | UART2 RX ← ML307 UART0_TXD | 关（`BOARD_ENABLE_4G_TEST=0`） |
| 48 | IO48_U2TXD / 4G_UART0_RXD | UART2 TX → ML307 UART0_RXD | 关（`BOARD_ENABLE_4G_TEST=0`） |

双眼是 **XJ0.99TFT-12P**（0.99 寸，驱动 **GC9107**，资料写玻璃 **128×RGB×115**，IC GRAM **128×160**，4-line SPI），不是 P4 的 GC9A01 240×240，也不能套 magiclick 那套 128×128 初始化。厂方序列在 `gc9107_boe_099_init.h`（来自 `XJ0.99TFT-12P/GC9107_BOE.99_IPS(1)(1).txt`，`0x3A=0x05` RGB565）。2026-11-09 根据实拍分离圆弧，撤销“圆屏可见整段 160 行”的假设：GC9107 数据手册说明行模式由 GM 硬件脚决定，最大 GRAM 容量不等于当前玻璃寻址范围。当前改为只发送 128×128 窗口，不再连续写 160 行；用户已确认多余圆弧消除且不再闪烁；本轮铺满尺寸仍待目视确认。资产保持 128×160（独立校验长度和源步长），以 (64,90) 为源中心、(64,64) 为目标中心，缩放跨度设为 132，使素材约 124px 的亮部直径达到约 128px，视线平移 8px。静态帧不重复发送；SPI 用 `esp_lcd_panel_io_tx_param(io,-1,nullptr,0)` 排空传输，不再每帧发送反色指令。FPC1/FPC3 为 12 针：1 GND、2 LEDK、3 LEDA、4 VDD、5–6 GND、7 D/C、8 CS、9 SCL、10 SDA、11 RESET、12 GND。**2026-10-16 原理图**：SCL 接到 GPIO40（网名 `IO40_LCD_MOSI`），SDA 接到 GPIO41（网名 `IO41_LCD_SCK`），软件按玻璃脚接线，不按网表名。FPC3 片选是 GPIO16（`IO16_LCD_CS1`，左眼，镜像），FPC1 片选是 GPIO17（`IO17_LCD_CS2`，右眼）。RES 接到 ESP `EN`，软件复位脚 `GPIO_NUM_NC`。两只眼背光经 SI2301 由 GPIO15 低电平点亮。灯带使能是 GPIO42。`GetDisplay()` 仍是 `NoDisplay`，不把聊天 UI 画到眼睛上。P4 的 240×240 C1 资产不能直接用。USB 摄像头插座 **U4** 接 S3 原生 USB（GPIO19 D− / GPIO20 D+）。K230 的 CSI 不要和眼睛 FPC1/FPC3、USB 摄像头座混插。

## 硬件门禁（未关不得当量产）

1. **功放使能**：`PA_EN` 在 GPIO46，软件作输出。播 PCM 时拉高，空闲拉低。不要再把该脚硬接到 3.3V，否则会和输出对打。
2. **4G UART 电平**：原理图仍标 `1.8V?`，网表是 S3 GPIO47/48 直连 ML307 UART0，中间没有转换芯片。`PWR_ON/OFF` 经 R56 4.7k 接地，给 VBAT 后应自动开机。已拉高 `4G_PWR`。真机 K230 JSON 已通，ML307 `AT` 无回包：先量 SW4 VIN 与 U14 `+4V`，再量模组 `UART0_TXD` 空闲电平（1.8V 低于 S3 Vih≈2.5V 时软件收不到）。
3. **S3 assets mmap**：`partitions/v2/32m.csv` 的 assets 是 16MB，S3 MMU 空闲页约 14MB，整分区映射会失败并关掉唤醒词。量产固件已按实际资源长度映射，不要再整分区 mmap。
4. **真机**：COM25 / MAC `d8:85:ac:ba:85:d8` 已配 `TP-LINK_C738` 并激活。
5. **U4 UVC**：须关 `USJ`。USB Host 必须在 I2S/WiFi 之前安装，否则会 `No free interrupt inputs`。已挂 `self.camera.take_photo`，一次连拍 3 帧、顺时针转 90° 再竖拼上传。插 U4。服务端 hello 需带 `vision.url`。画面仍横着就把 `CAMERA_ROTATE_90` 改成 `3`。
6. **舵机 CN2**：仅一路。1=GND，2=GPIO8 PWM，3=5V 经 D5。MG90S 堵转约 0.7A，USB 5V 只够轻载。转反了把 `SERVO_PAN_INVERT` 设 1。没有第二路 PWM，不能做俯仰。


## 2026-11-09 摄像头与对话恢复（验证中）

- 摄像头枚举实录支持 MJPEG 640×480@30fps，拍照用默认帧率；JPEG 帧缓冲 256KiB 位于 PSRAM，收流后归还所有排队帧再关闭。
- USB EP0 超时请求不得复用；未完成控制传输对应的设备关闭延后到回调，避免 `usbh_dev_close` 断言。若摄像头不再响应，返回失败而非重启主控，须继续检查枚举与取帧日志。
- Capture/Explain 使用 RAII 暂停语音算法送入、保留 I2S 硬件，任何退出路径均恢复。
- 本板聆听中再次唤醒改为保持连接并恢复聆听；其他板型不变。
- 眼睛增加刷屏失败重试、1 秒低频恢复重画、800ms 眨眼超时恢复。是否解决偶发半眨眼停住待长时间目视验收。
- 持续日志：`logs/com25-live-20261109.log`；PowerShell 用 `Get-Content -Encoding UTF8 -Tail 50 -Wait` 查看。日志不提交仓库。

- 本轮最终状态：用户确认对话恢复；USB 摄像头仍开流失败，暂存为已知问题，详见 `docs/USB摄像头已知问题-2026-11-09.md`。不登记拍照识别已通过。
