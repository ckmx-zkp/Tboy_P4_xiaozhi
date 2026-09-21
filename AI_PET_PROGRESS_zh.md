# ESP32-P4 AI 宠物 — 开发进度

> **维护约定**：每次完成可验证的硬件/软件改动后，同步更新本文档（状态、引脚、文件路径、下一步）。  
> 设计总纲见 `AI_PET_DEV_PLAN_zh.md`，视觉细节见 `AI_PET_VISION_REALTIME_PLAN_zh.md`。  
> 最后更新：2026-09-20

---

## 总览状态

| 里程碑 | 状态 | 说明 |
|--------|------|------|
| 0. 基线 7B 固件可跑 | ✅ 完成 | 自编译 2.2.6，32MB Flash/分区，WiFi+MQTT+语音对话验证通过 |
| 1. 硬件基线（AI Pet 板型） | ✅ 软件完成 | 烧录成功；WiFi/MQTT/idle；SKU=`esp32-p4-ai-pet` |
| 2. 双眼 GC9A01 | 🟡 进行中 | **单眼点亮+眨眼已目视确认**；待接第二只眼 |
| 3. 灯带 + 舵机 | ⬜ 未开始 | |
| 4. K230 主动视觉 | ⬜ 未开始 | |
| 5. 宠物体验层完善 | 🟡 进行中 | 眼睛情绪/视线/眨眼/闭眼已可通过 MCP 语音驱动 |

图例：✅ 完成 · 🟡 进行中 · ⬜ 未开始 · ⏸ 暂停 · ❌ 取消

---

## 已完成事项

### 环境与基线（7B 原板型）

- [x] 工程基于 `xiaozhi-esp32` v2.2.6，目标芯片 ESP32-P4
- [x] `sdkconfig` 对齐 7B 推荐：**32MB Flash**、`partitions/v2/32m.csv`、`LWIP_MAX_SOCKETS=16`
- [x] 全擦烧录后配网成功：`TP-LINK_C738`，IP `192.168.0.101`
- [x] 激活 / MQTT `mqtt.xiaozhi.me` / 唤醒「你好小智」/ 对话 / MCP 音量调节验证通过
- [x] OV5647 CSI 摄像头在 7B 配置下可检测
- [x] 确认：**关 MIPI 大屏不影响** 语音/联网/唤醒；状态可只走串口 monitor

### AI Pet 板型骨架

- [x] Kconfig：`BOARD_TYPE_WAVESHARE_ESP32_P4_AI_PET`
- [x] CMake：`waveshare/esp32-p4-ai-pet`
- [x] 目录：`main/boards/waveshare/esp32-p4-ai-pet/`
  - `config.h` / `config.json`
  - `esp32-p4-ai-pet.cc`（无 MIPI、无 GT911；保留音频/WiFi/CSI/BOOT）
  - `pet_eye_display.h` / `pet_eye_display.cc`（无 LVGL 聊天 UI，串口打印 Status/Chat）
- [x] `sdkconfig` 已切到 `CONFIG_BOARD_TYPE_WAVESHARE_ESP32_P4_AI_PET=y`
- [x] **编译 + 烧录成功**（`idf.py build flash`，含 `pet_eye_display.cc` / `esp32-p4-ai-pet.cc`）
- [x] **串口确认驱动起来**：`gc9a01 create success`、`PetEyeDisplay started 240x240`、`GC9A01 eye panel ready`
- [x] **语音基线在 AI Pet 板型上正常**：连 `TP-LINK_C738`、MQTT、idle、唤醒词加载
- [x] **单眼画面目视确认**（同心圆虹膜 + 瞳孔高光 + 眨眼；2026-07-16）

### 显示策略决策（已确认）

- 双眼 **不显示** 小智聊天 UI / 字幕 / 状态栏
- 调试信息只看 **idf_monitor 串口**
- 7 寸 MIPI DSI：**软件不初始化**（释放带宽与显存；差分脚不能当 GPIO）
- 魔眼动效：自绘分层（星云色虹膜 + 瞳孔 + 高光 + 眨眼），非完整 LVGL 桌面

### C1 整帧状态图 + 眼睛 MCP 工具（2026-07-17/18）

- [x] **C1 架构定型**：固件按「情绪 / 视线 / 眨眼 / 闭眼」切换**整张 240×240 状态图**，不播连续动画帧；RGB565 小端资产，`FlushFrame` 内做字节交换（GC9A01 线上需大端）
- [x] **占位图程序化生成**：`scripts/gen_placeholder_eyes.py` 直接生成全部 C1 资产（径向渐变虹膜+瞳孔+高光；按情绪配色；弧形眼睑遮罩），输出小端 `.bin` 到 `assets/`、PNG 预览到 `eye_pic/generated/`；避免反复打磨 AI 图
- [x] **9 帧资产嵌入**：`main/CMakeLists.txt` 显式列出 eye_master/happy/angry/sad/joy + blink_30/70/closed + eye_closed（构建日志 `AI Pet eye embed frames: 9`）
- [x] **视线（上下左右看）**：`SetGaze(dir)`，`kGazeShift=26`px，`BlitFrameGaze` 平移窗口 + 黑边填充
- [x] **眨眼**：`BlinkOnce()` → `blink_request_`，`kBlinkSeq{30,70,closed,70}` 序列
- [x] **闭眼（睡眠）**：新增 `eye_closed` 全黑帧 + `SetClosed(bool)`，闭住时保持睡眠帧
- [x] **默认自动行为关闭**：`auto_idle_=false`，自动眨眼+漂移视线默认不跑，便于测试 MCP 动作；可用 `SetAutoIdle(true)` 恢复
- [x] **5 个 MCP 眼睛工具**：新建 `eye_controller.h`（仿 `LampController` 模式，普通工具非 user_only）
  - `self.eye.look`(direction: up/down/left/right/center)
  - `self.eye.blink`
  - `self.eye.close` / `self.eye.open`（睡眠/唤醒）
  - `self.eye.set_emotion`(emotion: neutral/happy/angry/sad/joy)
- [x] **语音链路端到端验证通过**：唤醒词→云端 ASR→LLM 携带工具清单→`tool_call`(type:mcp)→`McpServer::DoToolCall`→主线程执行→TTS 回复。「眨眨眼」「向上/下/左/右看」「好了，休息」均正确触发并带中文情绪 TTS
- [x] **工具发现机制确认**：每个会话服务端自动 `tools/list` 拉取，**无需小智后台配置、无需自建云服务器**

### 激活 / 设备身份排查（2026-07-18）

- [x] `application.cc` 每次开机打印 **Device Identity** 块（Device-Id=MAC、Client-Id=UUID、User-Agent）
- [x] `ShowActivationCode` 增加每次开机仅打印一次 6 位验证码（仅当服务器下发 activation.code 时）
- [x] **根因定位**：清 NVS / `erase-flash` 后设备仍 `Activation done` 直接放行——因 `Serial-Number` 存于 **eFuse `ESP_EFUSE_USER_DATA`**（`ota.cc`），一次性可编程，擦 flash 无效；服务器见 `Activation-Version: 2` + 已注册 serial 即视为已激活，不再发验证码
- [x] 结论：设备端已激活，但绑定的账号/智能体与当前云端「星仔」（设备 0 / 未绑定）不一致；需在服务端解绑旧绑定后才会重新发验证码（用户已自行在云端定位到该设备）

---

## 当前硬件接线（单眼调试）

板子：Waveshare ESP32-P4-WIFI6-Touch-LCD-7B  
扩展座：PH2.0 12PIN（座 A，靠 TF 一侧）

| 模块脚 | 板子丝印/GPIO | 备注 |
|--------|----------------|------|
| SCL | **IO2** | SPI SCLK |
| SDA | **IO3** | SPI MOSI |
| DC | **IO4** | |
| CS | **IO5** | 单眼 |
| RST | **IO28** | |
| VCC | 3V3 | |
| GND | GND | |
| BLK | 建议接 3V3 | 代码中背光 `GPIO_NC` |

软件宏（`config.h`）：

```text
SCLK=2 MOSI=3 DC=4 CS=5 RST=28 SPI2_HOST 40MHz 240x240 GC9A01
```

### 扩展座完整丝印（备查）

**座 A（靠 TF）：** `3V3, GND, IO2, IO3, IO4, IO5, IO28, IO29, IO30, IO31, IO34, IO36`  
**座 B（靠 MIC2）：** `BAT, GND, 3V3, VO4, GND, IO46–IO52`

预留：`IO29` 右眼 CS；`IO30` BL；`IO31/34` 舵机等。

---

## 关键代码路径

| 路径 | 说明 |
|------|------|
| `main/boards/esp-box-3/` | BOX-3 / BOX-3B 官方板型（未改源码） |
| `sdkconfig.defaults.esp-box-3` | BOX-3 叠加默认：官方对话屏 + AEC |
| `sdkconfig.esp32p4-ai-pet` | P4 工作配置备份（切回用） |
| `main/boards/waveshare/esp32-p4-ai-pet/` | AI 宠物板型 |
| `main/boards/aipet/esp32-s3-usb-cam/` | S3 USB 摄像头大板（2026-08-25 原理图） |
| `main/boards/waveshare/esp32-p4-ai-pet/pet_eye_display.{h,cc}` | 眼睛渲染：情绪/视线/眨眼/闭眼整帧切换 |
| `main/boards/waveshare/esp32-p4-ai-pet/eye_controller.h` | 5 个眼睛 MCP 工具注册 |
| `scripts/gen_placeholder_eyes.py` | 占位 C1 资产生成脚本 |
| `main/boards/waveshare/esp32-p4-wifi6-touch-lcd/` | 原 7B，勿破坏 |
| `main/Kconfig.projbuild` | 板型选项 |
| `main/CMakeLists.txt` | 板型源码 GLOB + 眼睛帧 EMBED |
| `AI_PET_DEV_PLAN_zh.md` | 设计计划 |
| `AI_PET_PROGRESS_zh.md` | **本文：进度（以本文为准）** |

---

## 并行板型：ESP32-S3-BOX-3B（2026-08-18）

BOX-3B 与乐鑫 ESP-BOX-3 **同一块主机**（少配件）。只保留官方 320×240 对话屏 + 语音，不搬眼睛/灯带/舵机。

| 项 | 值 |
|----|-----|
| 板型 | `BOARD_TYPE_ESP_BOX_3`（`main/boards/esp-box-3/`，未改官方板代码） |
| 芯片 | ESP32-S3，16MB Flash |
| 屏幕 | ILI9341 320×240，默认消息风格（非宠物眼、非表情资源包） |
| 对话 | 自建 OTA + AFE「你好小智」+ 设备端 AEC |
| 串口 | COM11 |
| 配置备份 | `sdkconfig.esp-box-3` |

## 并行板型：ESP32-S3-LCD-EV-Board V1.5（2026-08-19）

当前工作树已纠正为此板。官方 480×480 RGB 对话屏 + 语音，不搬 P4 眼睛代码。

| 项 | 值 |
|----|-----|
| 板型 | `BOARD_TYPE_ESP_S3_LCD_EV_Board`（`main/boards/esp-s3-lcd-ev-board/`） |
| 母板 | V1.5：I2C SDA=IO47、SCL=IO48；RGB DATA6=IO8、DATA7=IO18 |
| 芯片 | ESP32-S3 |
| 屏幕 | GC9503 480×480 RGB |
| 唤醒词 | 自定义 Multinet：`ni hao xiao lu` / 显示「你好小鹿」 |
| 对话 | 自建 OTA + 设备端 AEC |
| 串口 | COM15 |
| 叠加默认 | `sdkconfig.defaults.esp-s3-lcd-ev-board` |
| P4 眼睛 | 仍在 `main/boards/waveshare/esp32-p4-ai-pet/`，仅选 P4 板型时编译 |

三块板用 `sdkconfig` 的 Board Type 区分；P4 ↔ S3 还要 `idf.py set-target`。切回 BOX-3：复制 `sdkconfig.esp-box-3` → `sdkconfig` 再编。

## 并行板型：AI Pet ESP32-S3 USB 摄像头大板（2026-09-05）

按 `D:/User/SCH_USB摄像头大板_2026-09-13.pdf` 核过屏脚；旧 08-25 网表仅作对照。不改 P4 / BOX-3 / LCD EV。

| 项 | 值 |
|----|-----|
| 板型 | `BOARD_TYPE_AIPET_ESP32_S3_USB_CAM` |
| 目录 | `main/boards/aipet/esp32-s3-usb-cam/` |
| 芯片 | ESP32-S3-WROOM-2-N32R16V（用户确认：32MB Flash + 16MB Octal PSRAM） |
| 当前软件 | WiFi + 音频 + 双眼 C1 + WS2812（`set_emotion` 同轮改灯）+ `set_blink_profile`；CN2 跟随代码仍在但舵机未接；UART2 ML307 AT 无回包（USB 无 +4V） |
| 引脚真源 | 同目录 `BOARD.md`、`config.h` |
| 眼睛屏 | XJ0.99TFT-12P / GC9107 / 128×115；厂方初始化 `gc9107_boe_099_init.h` |
| 硬件门禁 | PA_EN=GPIO46 仅输入；4G UART 原理图仍标 1.8V?；真机 COM25 / `d8:85:ac:ba:85:d8` |
| Flash/PSRAM | `FLASHSIZE_32MB` + `partitions/v2/32m.csv` + `SPIRAM_MODE_OCT` 80MHz；不强制 `OCT_FLASH`（bootloader 会自动切 Octal） |

选中本板需 `idf.py set-target esp32s3` 后在 menuconfig 选该 Board Type。不要覆盖现有 LCD EV / P4 的 `sdkconfig`。

## 进行中 / 阻塞

| 项 | 说明 |
|----|------|
| 单眼点亮 | ✅ 已确认有基础魔眼 + 眨眼 |
| 颜色偏黄绿 | 当前程序化色环效果，可后续换底图/调色 |
| BOX-3B 对话+屏幕 | 🟡 已起板联调；睡觉/待机不挂断（无眼睛工具，S2 走不通） |
| LCD EV Board | 🟡 已用 IDF 5.5.2 构建并烧录 COM15。MAC `90:e5:b1:a8:ed:80`；串口已看到 speaking/listening 循环，说明联网语音会话运行。待复位后补齐完整启动日志及目视 480×480 屏验证 |
| S3 USB 摄像头大板 | 🟡 P1 眼灯已过。**当前眼睛皮肤=巨蟹座**（COM28，用户确认五表情通过）。天蝎烧过未记结论；双鱼已转未嵌。醒后未自动睁眼。舵机/触摸未接。 |
| BLE 偶遇双 AI 交流 | 🔴 需求已校正、零实现：匿名发现 → 主人同意 → 换短期 token → 双方上报 → backend 生成内容；BLE 包格式、双边同意和空闲控制通道待冻结 |

---

## 下一步（按优先级）

1. **LCD EV Board**：复位并验 480×480 屏 +「你好小鹿」唤醒，持续收集 COM15 日志
2. BOX-3B 睡觉挂断：改 xiaozhi-server S2（不依赖 `self.eye.close`）
3. ~~烧录并点亮单眼~~ ✅（P4）
4. **第二块屏**（P4）：共 SPI，右眼 `CS=IO29`
5. **S3 星座眼**：天蝎已烧 COM28，先验五表情+眨眼。过后再换双鱼、巨蟹。无运行时换皮。飞线 PA_EN 后再验喇叭
6. 舵机/触摸硬件未接：不改跟随、不验 CN2。接上后再按 `AI_PET_FACE_FOLLOW_PLAN_zh.md` 做 Δx

---

## 变更日志

| 日期 | 摘要 |
|------|------|
| 2026-07-16 | 7B 32MB 配置修复；配网与语音全链路通过 |
| 2026-07-16 | 确认扩展座丝印与单眼接线表 |
| 2026-07-16 | 新增 `esp32-p4-ai-pet` 板型 + `PetEyeDisplay`（无 MIPI、无聊天 UI） |
| 2026-07-16 | 建立本进度文档；约定后续改动同步更新 |
| 2026-07-16 | AI Pet 固件编译烧录成功；串口确认 GC9A01+PetEye+WiFi/MQTT/idle；待目视屏幕 |
| 2026-07-16 | 约定：默认增量编译，非必要不 clean |
| 2026-07-16 | 修复“亮一下就灭”：加亮颜色、字节序、DMA/cache、启动绿/蓝自检、SPI 20MHz；增量编译已烧录 |
| 2026-07-16 | 用户反馈仅背光无颜色；回退渲染到首次能看到眼睛的路径（去掉字节交换/绿蓝自检/DMA 强改），SPI 恢复 40MHz 并已增量烧录 |
| 2026-07-16 | **单眼目视通过**：同心圆虹膜 + 瞳孔 + 眨眼；接线 SCL2/SDA3/DC4/CS5/RST28 确认可用 |
| 2026-07-16 | 说话时保持眨眼：`AnimLoop` 读 `kDeviceStateSpeaking`，眨眼与说话脉动叠加，说话期间眨眼更勤 |
| 2026-07-16 | 新增设计师资产规格 `AI_PET_EYE_ASSETS_SPEC_zh.md`（包B共32张：idle12+blink6+speak10+listen4+命名/提示词） |
| 2026-07-17 | 实验：`eye_pic` 6 张 idle + 2 锚点 → RGB565 bin 嵌入；`PetEyeDisplay` 播序列帧 + 代码眨眼；脚本 `scripts/convert_eye_pic.py` |
| 2026-07-17 | **实拍花屏排查**：不是 JPEG 压缩问题。根因 SPI GC9A01 需要 RGB565 字节序交换（对齐 LVGL `swap_bytes=1`）；未交换时原图变成彩虹噪点。已在 `FlushFrame` 刷屏前做 BE 交换 |
| 2026-07-17 | 新增 GPT Image / Grok Image 全流程：`AI_PET_EYE_AI_IMAGE_PIPELINE_zh.md`（锚点→32帧→转bin→上板） |
| 2026-07-17 | 眼睛偏小/居中黑边：源图虹膜仅占画布~60–72%；`convert_eye_pic.py` 默认自动裁切放大铺满（content~97%），需重新转 bin 并增量编译 |
| 2026-07-17 | C1 架构定型：整帧状态图切换（非连续动画帧）；`gen_placeholder_eyes.py` 程序化生成全部占位资产（9 帧） |
| 2026-07-18 | 眼睛新增视线（上下左右看，`kGazeShift=26`）+ 眨眼 `BlinkOnce` + 闭眼 `SetClosed`；默认关闭自动眨眼/漂移（`auto_idle_=false`）便于测试 |
| 2026-07-18 | 新增 `eye_controller.h` 注册 5 个眼睛 MCP 工具（look/blink/close/open/set_emotion）；语音链路端到端验证通过（无需后台配置/自建云） |
| 2026-07-18 | `application.cc` 每次开机打印 Device Identity（MAC/UUID/User-Agent）+ 仅一次 6 位验证码 |
| 2026-07-18 | 激活排查：清 flash/NVS 无效——Serial-Number 在 eFuse，服务器 Activation-Version 2 直接放行；设备已激活但绑定账号与当前「星仔」不一致，需服务端解绑 |
| 2026-08-18 | **切到 ESP32-S3-BOX-3B**：P4 `sdkconfig` 备份为 `sdkconfig.esp32p4-ai-pet`；目标 `esp32s3` + 官方 `esp-box-3`；默认对话屏 + 设备端 AEC + 自建 OTA。未搬眼睛/外设。S3 全量编译通过（约 2.7MB，余量 31%），待烧录 |
| 2026-08-18 | **切到 ESP32-S3-LCD-EV-Board-2 V1.5**：BOX-3 配置备份为 `sdkconfig.esp-box-3`；唤醒词改为自定义 Multinet「你好小鹿」；串口 COM15。P4 眼睛代码未动。待编译烧录 |
| 2026-08-18 | LCD EV Board 2 已烧录 COM15：SKU 正确、WiFi `192.168.0.106`、OTA 自建、激活码 `650181`。GT1151 触摸 I2C 失败改为跳过，避免重启循环。 |
| 2026-08-19 | **纠正板型为 ESP32-S3-LCD-EV-Board V1.5**：选择 `BOARD_TYPE_ESP_S3_LCD_EV_Board` + 1.5 引脚，GC9503 480×480；IDF 5.5.2 构建成功（应用分区余 29%）并烧录 COM15，写入哈希全通过。后台日志追加至 `logs/260819_COM15.log`。 |
| 2026-08-19 | **校正双 AI 交流需求**：改为户外低速 BLE 匿名发现，主人同意后交换短期 token，双方经云端上报，由 backend 生成本次受控播报内容；不再采用 App/智控台配对或实时语音会话桥。当前仅文档，固件零实现。 |
| 2026-09-05 | **新增独立板型 `esp32-s3-usb-cam`**：按 2026-08-25 USB 摄像头大板原理图/网表登记 GPIO；当前仅 WiFi+音频骨架。PA_EN=GPIO46 仅输入、4G UART 电平未关闭，未初始化眼睛/灯带/舵机/4G/K230。 |
| 2026-09-13 | **S3 大板眼睛改为 XJ0.99TFT-12P / GC9107 128×115**：写入 BOE 厂方初始化，背光按 P-MOS 低电平点亮；开机左红右蓝，不显示聊天 UI。未改 P4 眼睛资产。 |
| 2026-09-13 | **COM25 原为 bread-compact-wifi**：咪头/屏脚全错。已切 32MB `esp32-s3-usb-cam` 并烧录。ES7210 须用 8-bit 地址 `0x82`（默认 `0x80` 会断言重启）。真机：I2C 0x18/0x41、双眼 init、codec 已启动；WiFi NVS 已空需重配。 |
| 2026-09-13 | **S3 大板已重配网并激活**：`TP-LINK_C738` / `192.168.0.107`，MAC `d8:85:ac:ba:85:d8`。32m.csv 的 16MB assets 在 S3 上因 MMU 空闲页仅 14144 KB 被整分区 mmap 关掉，唤醒词未加载。`assets.cc` 改为先读头部再按实际长度映射。I2S disable 告警可忽略。喇叭仍受 PA_EN=GPIO46 限制。 |
| 2026-09-13 | **S3 大板 U4 UVC 真机通过**：关掉 USB-Serial-JTAG 后 Host 枚举到 1 个设备；320×240 不支持，640×480 MJPEG 连取 3 帧（约 36–41 KB）。未接 MCP/聊天。双眼仍只有背光。 |
| 2026-09-13 | **S3 大板摄像头接入 MCP**：本板 `UvcCamera` 实现 `GetCamera()`，注册 `self.camera.take_photo`；拍照走已验证的 640×480 MJPEG，JPEG 直传 explain。开机探测已去掉以免占 Host。 |
| 2026-09-13 | **UVC 拍照中断占满**：对话中途 `usb_host_install` 报 `No free interrupt inputs`。USB Host 改为板级构造最早安装，避开 I2S/WiFi 占 Level1+IRAM。固件已增量编过，待 `idf.py -p COM25 app-flash`。 |
| 2026-09-13 | 用户确认模组为 **ESP32-S3-WROOM-2 / 16MB PSRAM / 32MB Flash**（即 N32R16V）。`sdkconfig` 已是 32MB + Octal PSRAM 80MHz，未再改容量。 |
| 2026-09-13 | **S3 大板拍照已通、识图未通**：`captured 640x480 jpeg 17108` 后 HTTP 连上 8003；约 280ms 结束，LLM 回复「相机暂时用不了」。8003 GET 正常。固件补 JPEG SOI/EOI 校验并打印 explain 原文，便于区分坏帧 vs 未配 VLLM。 |
| 2026-09-13 | **S3 大板一次连拍 3 帧**：`take_photo` 在同一次开流里取 3 张完整 JPEG，竖向拼成 640×1440 再上传。8003 只收单文件，拼图失败则退回最大单帧。已增量编译，未烧录。 |
| 2026-09-13 | **「看看我」未调工具**：开机已注册 `self.camera.take_photo`，hello 已下发 vision.url+token。本轮只有口头「相机暂时还看不到我」，无 UVC 日志。加强工具中文说明，禁止模型声称无相机。 |
| 2026-09-13 | **S3 大板识图真机通过**：`burst 3 frames -> 640x1440` 86KB，8003 HTTP 200 `success:true`，模型描述了自拍连拍人物。问题：画面横置 90°、`<think>` 被 TTS 念出、上传约 4s 时 AFE FEED 堵满。固件改为顺时针转 90°、剥 think、拍照期间关麦。 |
| 2026-09-13 | 「再看看我」仍描述横置自拍；「拍张照」和「必须调用工具」只有口头应付，无 UVC 日志。工具说明补上拍张照/再看看，禁止假装已拍。转正固件待烧。 |
| 2026-09-13 | **按 09-13 原理图改眼睛 SPI**：FPC SCL→GPIO40、SDA→GPIO41，不再按网表名 MOSI/SCK。时钟 10MHz。待烧录目视红蓝。 |
| 2026-09-13 | **S3 大板眼睛下半花屏**：上半红/蓝正常，判定为 GC9107 GRAM 128×160 只刷了 115 行。改为刷满 160 行并等 SPI 排空再释放缓冲。待烧录目视。 |
| 2026-09-13 | 新增 GPT Image 一次性复制文档 `AI_PET_EYE_GPT_IMAGE_PROMPTS_zh.md`（C1 共 8 张：Master + 喜怒哀乐 + 眨眼 3 帧）。 |
| 2026-09-13 | **S3 大板两颗 WS2812**：GPIO3 DIN + GPIO15 EN。MCP `self.led.set_zodiac`（12 星座）/`set_emotion`/`off`，色系内随机。不接系统状态灯。待烧录话术验收。 |
| 2026-09-13 | **S3 大板双眼 C1**：`image/` 转 128×115 右眼资产，左眼刷屏镜像。MCP `self.eye.look/blink/close/open/set_emotion/get_state` 与灯色工具分离；心情话术由模型同一轮各调一次。 |
| 2026-09-13 | **S3 眼睛偏上**：圆屏能看到整段 160 行 GRAM，旧资产 128×115 贴顶导致虹膜偏上约一半。改为 128×160 居中铺满（虹膜放大到 160 高再裁宽）。待烧录目视。 |
| 2026-09-13 | **S3 眼睛二次微调**：160 铺满仍偏上约 15% 且比 0.99 寸圆屏显大。改为 108×108 圆眼 + 下移 20px。 |
| 2026-09-13 | **S3 眼睛换图烧录**：`image/` 新图已转 128×160 bin（含更新的 happy/joy），COM25 `app-flash` 完成并开 monitor。 |
| 2026-09-16 | **S3 4G 串口起板**：按 09-13 原理图打开 UART1/UART2。K230 只收自动 JSON；ML307 主动发 AT。GPIO18 `4G_PWR` 拉高。PWRKEY 已接地。UART 无电平转换。 |
| 2026-09-16 | **S3 真机：K230 JSON 已通，ML307 AT 无回包**。探测改为等 8s、多波特率、尝试对调 TX/RX。待查 SW4/+4V 与原理图 1.8V UART（S3 Vih≈2.5V）。 |
| 2026-09-19 | **S3 舵机自动跟随**：原理图仅 CN2 一路。GPIO8 LEDC 50Hz 驱 MG90S。K230 全量 JSON 在 ESP32 解析，`tracking.dx` 跟水平，丢脸回中。MCP `self.servo.*` 只做开关/点动。 |
| 2026-09-20 | **S3 P1 眼灯同轮**：`self.eye.set_emotion` 同步 WS2812；新增 `self.eye.set_blink_profile`；聆听轻漂、说话停漂、idle 45s 眼回 neutral。记忆仍在云端。舵机未接，未改跟随。已烧 COM28，待话术验收。 |
| 2026-09-20 | **S3 P1 真机通过（COM28）**：用户确认。开心/难过/生气同轮 DualEye+LedMood；「好了，休息吧」闭眼并 S2 断开。再唤醒仍 `closed=1`。 |
| 2026-09-20 | **S3 星座眼**：`image/zodiac/{scorpio,pisces,cancer}` 各 8 张 1254²；已转 128×160 RGB565。固件嵌入天蝎并烧 COM28。旧资产在 `assets/legacy_backup`。 |
| 2026-09-20 | **S3 巨蟹眼上板**：`assets/` 换为 `image/zodiac/cancer`，增量编译烧 COM28。待五表情目视。 |
| 2026-09-20 | **关掉 4G 起板探测**：`BOARD_ENABLE_4G_TEST=0`，不开 UART2、不拉 GPIO18、不发 AT。要测时改 1。 |
