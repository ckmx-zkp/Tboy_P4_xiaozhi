# ESP32-P4 AI 宠物 — 开发进度

> **维护约定**：每次完成可验证的硬件/软件改动后，同步更新本文档（状态、引脚、文件路径、下一步）。  
> 设计总纲见 `AI_PET_DEV_PLAN_zh.md`，视觉细节见 `AI_PET_VISION_REALTIME_PLAN_zh.md`。  
> 最后更新：2026-07-18

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
| `main/boards/waveshare/esp32-p4-ai-pet/` | AI 宠物板型 |
| `main/boards/waveshare/esp32-p4-ai-pet/pet_eye_display.{h,cc}` | 眼睛渲染：情绪/视线/眨眼/闭眼整帧切换 |
| `main/boards/waveshare/esp32-p4-ai-pet/eye_controller.h` | 5 个眼睛 MCP 工具注册 |
| `scripts/gen_placeholder_eyes.py` | 占位 C1 资产生成脚本 |
| `main/boards/waveshare/esp32-p4-wifi6-touch-lcd/` | 原 7B，勿破坏 |
| `main/Kconfig.projbuild` | 板型选项 |
| `main/CMakeLists.txt` | 板型源码 GLOB + 眼睛帧 EMBED |
| `AI_PET_DEV_PLAN_zh.md` | 设计计划 |
| `AI_PET_PROGRESS_zh.md` | **本文：进度（以本文为准）** |

---

## 进行中 / 阻塞

| 项 | 说明 |
|----|------|
| 单眼点亮 | ✅ 已确认有基础魔眼 + 眨眼 |
| 颜色偏黄绿 | 当前程序化色环效果，可后续换底图/调色 |

---

## 下一步（按优先级）

1. ~~烧录并点亮单眼~~ ✅  
2. ~~眼睛视线/眨眼/情绪 MCP 化~~ ✅（语音驱动已验证；`close`/`open` 语音命令待实测）  
3. **第二块屏**：共 SPI，右眼 `CS=IO29`，左右同步/镜像  
4. 状态机映射：listening/speaking 驱动眼睛（不仅 `SetEmotion`）  
5. 魔眼观感增强：换 AI 美术底图（GPT/Grok，暂缓）/ 更自然眨眼与高光  
6. WS2812 + 双舵机  
7. K230 UART 视觉  

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
