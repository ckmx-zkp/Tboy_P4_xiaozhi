# 小智 ESP32-P4 AI 宠物开发计划

## 概要

本计划基于当前 `xiaozhi-esp32` 工程，以及目标硬件 Waveshare ESP32-P4-WIFI6-Touch-LCD-7B。

当前 7B 板卡支持已经覆盖 ESP32-P4、WiFi6/BT 协处理器、ES8311/ES7210 音频、OV5647 CSI 摄像头，以及原始 7 寸 MIPI LCD 显示路径。AI 宠物版本应作为一个独立板卡变体新增，而不是直接修改现有 7B 板卡实现。

AI 宠物变体将复用 P4、网络、音频和摄像头基础能力，默认关闭 7 寸 LCD，改用两个 1.28 寸 TFT 屏幕作为眼睛。同时新增一路 WS2812 灯带输出和舵机控制，用于简单的实体表情动作。

视觉采用双通路架构：

- **被动拍照上传**：保留 P4 自带的 OV5647 CSI 摄像头，复用现有 `Camera::Capture()` + `Explain()` 链路，HTTP 上传云端解释，用于用户主动发起的语义理解（“看看这是什么”）。
- **主动识别**：外挂 K230D 视觉模组，经 UART 上报结构化结果，运行官方 `face_pose`（头部 pitch/yaw/roll）和 `face_emotion`（7 类表情）模型，用于人脸朝向→舵机跟随、表情分类→对话触发等实时本地反馈。

主控维持 ESP32-P4 + C6 协处理器（WiFi6/BT）。ESP32-P4 有 6 个 UART，外挂 K230D 后仍余 4 个，足以支撑后续 4G/NFC/调试扩展，不降级到 S3。

默认假设：

- 眼睛屏幕：两个 1.28 寸 GC9A01 240x240 圆形 TFT 模块。
- 7 寸 MIPI LCD：默认不初始化。
- 舵机控制：第一版支持两路。
- 被动拍照：P4 CSI 摄像头（OV5647 800x800），单帧 JPEG 上云解释，不做连续视频流。
- 主动识别：K230D 自带摄像头，本地实时推理，结果走 UART 上报主控。
- 两个摄像头物理独立，分别由 P4 和 K230D 管理，互不干扰。

Waveshare 板卡参考资料：

- https://docs.waveshare.net/ESP32-P4-WIFI6-Touch-LCD-7B/

## 关键改动

### 板卡变体

- 新增板卡类型：`BOARD_TYPE_WAVESHARE_ESP32_P4_AI_PET`。
- 建议板卡目录：`main/boards/waveshare/esp32-p4-ai-pet`。
- 从 `main/boards/waveshare/esp32-p4-wifi6-touch-lcd` 派生实现。
- 保持现有 `esp32-p4-wifi6-touch-lcd-7b` 支持不变，避免回归风险。

### 基础硬件支持

- 复用现有 ESP32-P4 网络、音频 codec、BOOT 按键和 OV5647 CSI 摄像头初始化。
- 默认不初始化 7 寸 MIPI LCD 和 GT911 触摸路径。
- 如后续需要开发调试，可保留一个 Kconfig 开关，用于可选启用 7 寸 LCD。

### 双眼显示

- 新增专用宠物眼睛显示层，例如 `PetEyeDisplay`。
- 两个 TFT 屏共用一路 SPI 总线。
- 左眼和右眼使用独立片选引脚。
- DC、复位和背光引脚做成可配置项。
- 第一版表情集合：
  - `neutral`
  - `listening`
  - `speaking`
  - `thinking`
  - `sleepy`
  - `error`
- 第一版动画集合：
  - 眨眼
  - 瞳孔移动
  - 说话脉冲
  - 识别/思考扫描效果

### WS2812 灯带

- 尽量复用现有 `CircularStrip` / `led_strip` RMT 路径。
- 新增可配置 GPIO 和灯珠数量。
- 将设备状态映射到宠物灯效：
  - 启动中：柔和白色或蓝色呼吸
  - Wi-Fi 配网中：蓝色闪烁
  - 空闲：低亮度呼吸
  - 聆听：青色活动效果
  - 说话：暖色脉冲
  - 识别中：移动扫描
  - 错误：红色闪烁

### 舵机控制

- 新增基于 LEDC PWM、50 Hz 的轻量舵机控制器。
- 第一版支持两路通道。
- 提供：
  - `SetAngle(channel, degree)`
  - `MoveToPreset(name)`
  - 可配置最小、最大和中位脉宽
  - 角度限幅，用于保护机械结构
- 建议第一版动作预设：
  - `center`
  - `look_left`
  - `look_right`
  - `nod`
  - `curious`
  - `sleep`

舵机必须使用外部独立供电。ESP32-P4 GPIO 只输出 PWM 控制信号。

### 主动视觉

- 视觉方案拆成两层：本地实时视觉状态层 + 小智单张照片解释层。
- 本地实时视觉状态层由 `PetVisionService` 负责，基于外挂 K230D 视觉模组实现。这里的 `PetVision` 指“电子 AI 宠物自身的视觉服务”，不是识别画面里的猫狗宠物。
- 第一优先级是识别小智面前的用户/主人：人脸是否存在、人脸在画面中的位置、人脸左转/右转/抬头/低头/靠近/远离。
- 第二优先级是识别夸张面部表情：开心、生气、难过、惊讶、中性/未知。
- 视觉推理在 K230D 上完成，运行嘉楠官方现成模型：`face_pose.kmodel`（输出 pitch/yaw/roll）和 `face_emotion.kmodel`（7 类表情）。P4 主控不承担视觉推理，只通过 UART 接收 K230D 上报的结构化结果。
- K230D 与 P4 之间走 UART 通信，协议需定义帧头、校验和结果字段（人脸框、yaw/pitch/roll、情绪类别、置信度）。波特率建议 115200 或更高。
- 本地视觉事件用于驱动眼睛、灯带和舵机，使电子宠物对用户表情和动作做即时反应。
- 小智视觉解释层（被动拍照）继续使用 P4 自带 OV5647 CSI 摄像头和现有 `Camera::Capture()` / `Camera::Explain()`，只在用户请求或高价值事件触发时拍单张照片上传云端解释。
- 不把小智对话链路改成连续视频流；小智仍保持语音唤醒和单张拍照语义。
- 需要主动开口时，由视觉事件门控后调用 `Application::WakeWordInvoke()`，传入类似 `<detect>human_face_happy</detect>` 或 `<detect>human_approaching</detect>` 的事件摘要。
- 增加冷却时间、连续命中阈值和设备状态检查，避免频繁打断、重复上传和误唤醒。
- 详细方案见 `AI_PET_VISION_REALTIME_PLAN_zh.md`。

### 宠物控制器

新增一个行为编排层，例如 `PetController`，用于协调：

- 设备状态变化
- 眼睛表情
- 灯带效果
- 舵机动作
- 人脸视觉状态和面部表情状态
- 未来宠物专属行为

这样可以让宠物行为逻辑不进入 `Application`，避免硬件逻辑分散到无关模块里。

## 对外接口

### Kconfig

新增配置项：

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
- `PET_VISION_ENABLE`
- `PET_VISION_LOCAL_STATE_ENABLE`
- `PET_VISION_TRIGGER_WAKE_WORD`
- `PET_VISION_TRIGGER_EXPLAIN`
- `PET_K230_UART_NUM`
- `PET_K230_TX_GPIO`
- `PET_K230_RX_GPIO`
- `PET_K230_BAUDRATE`
- `PET_K230_ENABLE_FACE_POSE`
- `PET_K230_ENABLE_FACE_EMOTION`
- `PET_K230_REPORT_INTERVAL_MS`
- `PET_FACE_DETECT_ENABLE`
- `PET_FACE_LANDMARK_ENABLE`
- `PET_FACE_EMOTION_ENABLE`

最终 GPIO 值应来自实际接线。软件侧应将这些值做成可配置项，而不是在引脚尚未最终确定时写死。

### Board 覆写

AI 宠物板卡变体应覆写：

- `GetDisplay()`：返回双眼显示对象。
- `GetLed()`：返回 WS2812 灯带对象。
- `GetCamera()`：继续返回现有摄像头对象。
- `GetAudioCodec()`：复用现有 ES8311/ES7210 音频 codec 路径。

### 可选 MCP 工具

硬件基线稳定后，再新增面向用户的 MCP 工具：

- `self.pet.set_expression`
- `self.pet.move_servo`
- `self.pet.set_light_effect`
- `self.pet.observe`

这些工具不是第一阶段硬件调通所必需的。

## 里程碑

### 1. 硬件基线构建

- 新增板卡类型和板卡目录。
- 复用当前 7B 音频、网络和摄像头配置。
- 在 AI 宠物变体中关闭 7 寸 LCD 初始化。
- 构建并烧录板卡。
- 验证启动、Wi-Fi、音频和 P4 CSI 摄像头被动拍照（`self.camera.take_photo`）。
- 验证 K230D 模组供电、复位和 UART 链路打通（能收到心跳/握手响应）。

### 2. 双眼显示调通

- 初始化共享 SPI 总线。
- 点亮左眼 GC9A01 屏幕。
- 点亮右眼 GC9A01 屏幕。
- 渲染静态眼睛。
- 接入设备状态，做基础表情变化。

### 3. 灯带和舵机调通

- 使用可配置 GPIO 和灯珠数量初始化 WS2812 灯带。
- 增加基于状态的 LED 效果。
- 初始化两路 LEDC 舵机 PWM 通道。
- 验证中位、最小和最大角度。
- 增加简单动作预设。

### 4. 主动识别

- 增加 `PetVisionService`，实现 K230D UART 协议解析和本地视觉状态识别。
- 第一阶段打通 K230D UART 通信：握手、帧头校验、结构化结果解析（人脸框、yaw/pitch/roll、情绪类别、置信度）。
- 第二阶段启用 K230D 官方 `face_pose` 模型，输出人脸存在、位置、转动方向，用于眼神和舵机跟随。
- 第三阶段启用 K230D 官方 `face_emotion` 模型，输出夸张面部表情：开心、生气、难过、惊讶、中性/未知。
- 人脸框面积变化用于判断靠近/远离，无需额外模型。
- 高价值或低置信度场景再触发 P4 摄像头单张照片上传云端解释。
- 可选增加人脸事件到 `WakeWordInvoke()` 的桥接，实现“小智看到用户表情后主动回应”。

### 5. 宠物体验层

- 增加眨眼和空闲眼睛动画。
- 增加说话时的眼睛脉冲。
- 增加说话时的小幅舵机动作。
- 增加好奇和困倦行为循环。
- 增加错误和低功耗行为。

## 测试计划

### 构建测试

- 构建现有 `esp32-p4-wifi6-touch-lcd-7b` 目标，确认没有回归。
- 构建新的 `esp32-p4-ai-pet` 目标。
- 确认两个板卡类型都能通过 menuconfig/config JSON 选择。

### 硬件冒烟测试

- 启动日志进入正常应用状态。
- Wi-Fi 配网和连接正常。
- 麦克风输入和扬声器输出正常。
- P4 CSI 摄像头可以被动拍照并上传解释。
- K230D 模组上电后经 UART 能完成握手并周期上报结果。
- 左右眼屏幕可以独立显示。
- WS2812 灯带可以设置全灯颜色并运行效果。
- 舵机 1 和舵机 2 可以安全移动到中位、最小和最大位置。

### 状态集成测试

- 启动状态会更新眼睛和 LED。
- Wi-Fi 配网状态有明确反馈。
- 空闲状态表现安静且低功耗。
- 聆听状态表现出注意力。
- 说话状态驱动眼睛动画和舵机动作。
- K230D 上报人脸朝向时，舵机能跟随转向并回中。
- K230D 上报表情变化时，能触发对应眼睛/灯效/对话。
- 被动拍照状态显示摄像头活动反馈。
- 错误状态可以通过眼睛和 LED 明确看出。

### 稳定性测试

- 空闲动画连续运行 30 分钟。
- 带冷却时间的重复 P4 摄像头被动拍照连续运行 30 分钟。
- K230D 持续上报视觉结果连续运行 30 分钟，UART 无丢帧/错帧。
- 确认没有明显内存泄漏或任务崩溃。
- 确认 SPI 眼睛刷新不破坏音频。
- 确认舵机 PWM 不干扰 WS2812 输出。
- 确认 K230D UART 接收不与音频/I2S 抢中断。

## 假设和约束

- 需求里的 `W2812` 按 `WS2812` 处理。
- 第一版重点是可用的宠物交互，不是完整机器人运动。
- 连续视频流不属于第一版范围（P4 硬件具备 H.264 编码能力，但小智协议层无视频通道，留作后续里程碑）。
- 复杂本地视觉语义推理不属于第一版范围；第一版本地视觉只做人脸存在、人脸姿态和夸张表情等轻量互动状态识别，由 K230D 完成。
- 被动拍照上传仍走现有 P4 CSI 摄像头 + 云端 Explain 链路，不做连续视频流。
- 主动识别由 K230D 承担，P4 主控不承担视觉推理，只通过 UART 接收结构化结果。
- 最终 GPIO 分配取决于实际接线和装配后可用的板卡引脚。
- 舵机电源不能直接取自 ESP32-P4 GPIO 供电轨。
- K230D 模组必须使用独立供电，不能取自 P4 3.3V 轨；UART 共地。

## 成本决策记录

### 主控选型：P4 + C6 vs S3 R8N16

- ESP32-P4 无内置 WiFi/BT 射频，必须外挂 C5/C6 协处理器（esp_hosted），P4+C6 模组约 50 元。
- ESP32-S3 R8N16 约 20 元，内置 WiFi/BT，但只有 3 个 UART、无 MIPI-CSI、无 H.264 硬件编码。
- 本方案保留 P4+C6：双视觉通路需要 P4 的 MIPI-CSI 摄像头（被动拍照）和充裕的 UART（6 个，外挂 K230D 后仍余 4 个）；S3 在串口和摄像头接口上均不满足。
- 若未来验证发现 P4 摄像头被动拍照链路用不上，可再评估降级到 S3 + K230D 单通路方案。

### 视觉方案选型：ESP-DL 本地 vs K230D 外挂

- P4 + ESP-DL 路线：ESP-DL 模型库只有人脸检测 + 人脸识别，没有表情分类、头部姿态、人脸关键点模型，需自行训练和部署，工程风险高。
- K230D 路线：嘉楠官方提供现成 `face_pose.kmodel`（pitch/yaw/roll）和 `face_emotion.kmodel`（7 类表情），开箱即用，且推理不占 P4 算力。
- 结论选 K230D：消除模型工程风险，代价是增加约 80 元 BOM 和一路 UART 协议。
- K230D 量产用裸模组跑官方 demo；前期验证可用 DFRobot HUSKYLENS 2（K230，出厂预置表情+朝向，UART/I2C 输出）快速跑通全链路，量产再换裸模组降本。
- 详细视觉实时方案见 `AI_PET_VISION_REALTIME_PLAN_zh.md`（该文档上层状态机和事件门控可复用，推理后端章节需按 K230D 重写）。

