# 电子 AI 宠物人脸视觉识别方案

## 背景问题

当前 AI 宠物方案里，唯一和目标体验不匹配的是视觉能力：

- 目标体验：小智作为电子 AI 宠物实体，需要持续观察面前的用户/主人，实时识别人脸、人脸转动和夸张面部表情，并驱动眼睛、灯带、舵机反馈。
- 当前小智框架：主要通过语音唤醒进入对话；视觉能力通过 MCP 工具 `self.camera.take_photo` 调用 `Camera::Capture()` 和 `Camera::Explain()`，本质是单张照片上传解释。
- 约束：不能把小智现有对话链路直接当成实时视频识别链路，否则会遇到唤醒触发、上传频率、网络延迟、云端成本和交互节奏问题。

因此，视觉方案需要拆成两层：

- 本地实时人脸视觉状态层：持续、低延迟、低成本，用于电子宠物自身的互动状态机。
- 小智视觉解释层：低频、事件触发、单张图片，用于需要语言理解或对话解释的场景。

## 总体方案

新增一个独立的 `PetVisionService`，不替代小智现有 `Camera` 接口，而是复用同一个摄像头硬件，建立一条电子宠物专属的人脸视觉状态通道。

注意：这里的 `PetVisionService` 表示“电子 AI 宠物自身的视觉服务”，不是识别画面里的猫狗宠物。当前主目标是识别人脸、人脸转动和人类面部表情。

推荐架构：

```text
OV5647 摄像头
   |
   +-- PetVisionService：低分辨率/低帧率本地人脸状态识别
   |       |
   |       +-- PetController：驱动眼睛、灯带、舵机
   |       |
   |       +-- 事件门控：决定是否触发小智对话或拍照解释
   |
   +-- 现有 Camera::Capture() / Explain()
           |
           +-- self.camera.take_photo：单张照片上传视觉解释
```

核心原则：

- 实时状态识别不走小智对话链路。
- 小智仍保持语音唤醒和单张拍照解释的原始模型。
- 只有当本地视觉状态判断“值得说话/值得解释”时，才触发小智。

## 当前视觉目标收敛

当前 AI 宠物视觉主要用于人脸互动，不优先做通用物体检测，也不优先识别真实猫狗宠物。

目标优先级：

1. 识别人脸是否存在，以及人脸在画面中的位置。
2. 识别人脸转动方向，尤其是左转、右转、抬头、低头、靠近、远离。
3. 识别人面部表情，重点是夸张的喜、怒、哀、乐等高强度情绪。

非优先目标：

- 不优先做多类别物体检测。
- 不优先识别画面里的猫狗宠物。
- 不优先做身份识别或人脸注册。
- 不优先做复杂微表情识别。
- 不优先做连续视频语义描述。

第一版应把视觉输出定义为“互动状态”，而不是“画面描述”。

建议输出状态：

- `face_none`：没有人脸。
- `face_center`：人脸在正前方。
- `face_left`：人脸偏左或向左转。
- `face_right`：人脸偏右或向右转。
- `face_up`：人脸抬头。
- `face_down`：人脸低头。
- `face_close`：人脸靠近。
- `face_far`：人脸远离。
- `emotion_happy`：明显开心/笑。
- `emotion_angry`：明显生气。
- `emotion_sad`：明显难过。
- `emotion_surprised`：明显惊讶。
- `emotion_neutral`：无明显表情。
- `emotion_unknown`：无法稳定判断。

这些状态再由 `PetController` 映射到眼睛、灯带和舵机动作，让电子宠物对人做出反应。

## ESP-VISION / ESP-WHO / ESP-DL 适配判断

对当前目标来说，ESP-WHO 和 ESP-DL 的适配价值不同：

- ESP-WHO 适合作为参考：它提供人脸检测、人脸识别、行人检测、二维码识别等示例，并已支持 ESP32-P4，摄像头和模型可以异步运行。
- ESP-DL 适合作为实际集成依赖：它是 ESP 系列芯片上的轻量神经网络推理库，当前项目可把它作为 `PetVisionService` 的模型推理后端。
- ESP-Detection 更适合后续训练自定义目标检测模型，但当前目标是人脸姿态和表情，不应优先走通用物体检测路线。

建议第一阶段不要直接移植完整 ESP-WHO 工程，而是：

- 参考 ESP-WHO 的人脸检测模型和预处理流程。
- 在当前工程中新增 `PetVisionService`，调用 ESP-DL 推理。
- 摄像头、音频、小智协议和宠物动作仍保持当前工程自己的结构。

## ESP-WHO 与 ESP-DL 移植计划

### 移植边界

ESP-WHO 和 ESP-DL 都要纳入方案，但角色不同：

- ESP-DL：作为当前固件内实际使用的端侧推理库，负责模型加载、算子执行和推理输出。
- ESP-WHO：作为人脸检测、关键点、人脸相关模型示例和异步摄像头推理流程的参考来源；只按需移植模型封装、预处理和后处理代码，不整体替换 `xiaozhi-esp32` 的应用框架。

不整体移植 ESP-WHO 的原因：

- ESP-WHO 是完整示例工程形态，包含自己的 BSP、摄像头任务、显示逻辑和示例应用结构。
- 当前工程已经有 `Board`、`Application`、`EspVideo`、`McpServer`、显示和音频状态机。
- 直接整体合并会造成摄像头、显示、任务调度和配置系统重复。

### 组件依赖计划

在 `main/idf_component.yml` 中新增 ESP-DL 依赖，建议只在 ESP32-P4 / ESP32-S3 目标启用：

```yaml
espressif/esp-dl:
  version: "*"
  rules:
  - if: target in [esp32p4, esp32s3]
```

ESP-WHO 不建议作为普通组件直接加入主工程。推荐做法：

- 将 ESP-WHO 作为外部参考仓库或 `third_party_reference` 资料，不参与默认构建。
- 从 ESP-WHO 中挑选人脸检测、关键点、模型预处理/后处理代码，整理成当前工程自己的 `main/pet/vision` 模块。
- 保留来源说明和许可证信息。

如果后续确实需要复用 ESP-WHO 的完整组件，应先做单独分支验证，确认不会破坏当前工程的摄像头和显示架构。

### 模型资产计划

模型文件不应写进普通源码逻辑里，应作为资源资产管理：

- ESP-DL 使用 `.espdl` 模型文件。
- 人脸检测模型优先使用 ESP-WHO/ESP-DL 官方可用模型。
- 表情分类模型如无官方现成模型，再单独训练和转换。
- 模型建议放入独立目录，例如 `main/assets/models` 或专用模型分区。
- 如果模型较大，优先评估 32 MB Flash 分区和 mmap 方式加载。

第一批模型建议：

- 人脸检测模型：用于输出 face bounding box。
- 可选关键点模型：用于眼睛、鼻尖、嘴部位置。
- 可选表情分类模型：用于 `neutral/happy/angry/sad/surprised`。

### 代码结构建议

新增模块建议：

```text
main/pet/
  pet_controller.h
  pet_controller.cc
  vision/
    pet_vision_service.h
    pet_vision_service.cc
    face_state.h
    face_detector.h
    face_detector_espdl.cc
    face_landmark.h
    face_landmark_espdl.cc
    face_emotion.h
    face_emotion_espdl.cc
    vision_preprocess.h
    vision_preprocess.cc
```

职责划分：

- `PetVisionService`：任务调度、帧读取、状态稳定、事件输出。
- `FaceDetectorEspdl`：调用 ESP-DL 人脸检测模型。
- `FaceLandmarkEspdl`：可选，调用关键点模型。
- `FaceEmotionEspdl`：可选，调用表情分类模型。
- `VisionPreprocess`：裁剪、缩放、颜色转换、归一化。
- `PetController`：把人脸事件映射为眼睛、灯带、舵机、小智触发。

### 摄像头接入计划

当前 `EspVideo` 已经负责通过 `esp_video` / V4L2 打开 OV5647 摄像头。移植 ESP-DL 后，不新建第二套摄像头驱动。

需要在 `EspVideo` 增加内部帧读取能力：

- `Capture()`：保留当前单张拍照语义，给小智视觉解释使用。
- `ReadVisionFrame()` 或 `TryReadFrame()`：新增内部接口，给 `PetVisionService` 低频读取帧。
- 两者必须用 mutex 串行访问 V4L2 buffer，避免 `VIDIOC_DQBUF` / `VIDIOC_QBUF` 互相抢帧。

帧处理路径：

```text
EspVideo::ReadVisionFrame()
  -> 取一帧低频图像
  -> VisionPreprocess 裁剪/缩放/颜色转换
  -> ESP-DL 人脸检测
  -> 可选关键点/表情模型
  -> PetVisionEvent
```

### 分阶段移植步骤

#### 阶段 A：ESP-DL 编译接入

- 在 `idf_component.yml` 增加 `espressif/esp-dl`。
- 新增空的 `FaceDetectorEspdl` 封装类。
- 验证工程能在 ESP32-P4 目标下编译。
- 不接真实模型，不影响现有 `Capture()` 和小智语音流程。

验收标准：

- 原 7B 目标能编译。
- AI Pet 目标能编译。
- 关闭 `PET_VISION_USE_ESP_DL` 时不链接 ESP-DL 推理逻辑。

#### 阶段 B：模型加载验证

- 放入一个官方 ESP-DL/ESP-WHO 人脸检测 `.espdl` 模型。
- 验证模型能从 Flash/分区加载。
- 打印模型加载耗时、PSRAM/SRAM 占用。
- 不接摄像头实时帧，先用静态测试图或固定 buffer 验证推理入口。

验收标准：

- 模型加载成功。
- 推理接口可调用。
- 内存占用不会影响音频启动。

#### 阶段 C：接入摄像头低频帧

- 在 `EspVideo` 增加内部低频帧读取接口。
- `PetVisionService` 每 300-500 ms 取一帧。
- 预处理成模型输入尺寸，例如 224x224 或模型要求尺寸。
- 跑人脸检测并输出人脸框。

验收标准：

- 检测到人脸时输出 bounding box。
- 没有人脸时稳定输出 `human_face_none`。
- 不影响小智语音唤醒、播放和单张拍照解释。

#### 阶段 D：姿态和跟随

- 基于人脸框中心和面积计算方向、靠近、远离。
- 接入 `PetController`，驱动双眼和舵机跟随。
- 加入状态稳定逻辑，避免人脸框抖动导致舵机频繁抖动。

验收标准：

- 用户左右移动时，眼睛或舵机能跟随。
- 用户靠近/远离时，能输出稳定状态。
- 舵机动作有速度限制和角度限幅。

#### 阶段 E：关键点和表情

- 若 ESP-WHO/ESP-DL 可复用关键点模型，先接关键点。
- 基于关键点增强转头判断。
- 表情先做规则辅助；效果不足再训练/接入小型表情分类模型。

验收标准：

- 夸张开心、惊讶能稳定识别。
- 生气、难过低置信度时可输出 `human_emotion_unknown`，不强行误判。
- 表情事件不会频繁触发小智对话。

### ESP-WHO 代码参考清单

移植时重点参考 ESP-WHO 的这些内容：

- 摄像头帧到模型输入的预处理方式。
- 人脸检测模型输入尺寸、输出格式和后处理。
- 人脸关键点模型的输入裁剪策略。
- 推理任务与摄像头任务异步解耦方式。
- ESP32-P4 上模型运行的内存配置建议。

不建议移植：

- ESP-WHO 的完整主程序。
- ESP-WHO 的板级 BSP 初始化。
- ESP-WHO 的显示 UI 示例。
- 与当前小智 `Application` 状态机重复的任务调度代码。

### 授权和维护

- ESP-DL 适合作为固件依赖，但仍需记录版本号和许可证。
- ESP-WHO 中复制的代码应保留来源说明。
- ESP-Detection 如果用于训练自定义模型，需要额外评估 AGPL-3.0 授权边界；当前阶段不作为固件默认依赖。

## 人脸姿态识别方案

第一版人脸姿态不要直接做完整 3D head pose。更稳的路线是两级判断：

### 1. 基于人脸框的位置和大小

人脸检测模型输出 face bounding box 后，先做低成本姿态判断：

- 人脸框中心点偏左：`face_left`
- 人脸框中心点偏右：`face_right`
- 人脸框中心点偏上：`face_up`
- 人脸框中心点偏下：`face_down`
- 人脸框面积持续变大：`face_close`
- 人脸框面积持续变小：`face_far`

这类判断不需要额外模型，实时性最好，适合第一版驱动宠物眼神和舵机。

### 2. 基于关键点的转头判断

如果 ESP-WHO/ESP-DL 人脸模型能输出关键点，增加更细判断：

- 鼻尖相对两眼中心偏左/偏右：判断左右转头。
- 鼻尖相对嘴/眼位置变化：判断抬头/低头。
- 左右眼可见差异明显：辅助判断侧脸。

这一步用于提升“人脸转动”的准确度，但不是第一版硬依赖。

## 面部表情识别方案

面部表情识别建议分三档实现。

### 档位 A：规则辅助表情

如果能拿到人脸关键点，可以先用规则判断夸张表情：

- 嘴角上扬、嘴部张开：`emotion_happy`
- 嘴部大张、眼睛区域变化大：`emotion_surprised`
- 眉眼区域压低、嘴角下压：`emotion_angry` 或 `emotion_sad`

优点是轻量、可解释；缺点是准确率有限，依赖关键点质量。

### 档位 B：小型表情分类模型

训练或引入一个小型表情分类模型，输入为裁剪后的人脸图，例如 96x96 或 112x112 灰度/RGB 图。

推荐类别：

- neutral
- happy
- angry
- sad
- surprised

不建议第一版做太多类别。夸张喜怒哀乐比细微表情更适合端侧 MCU。

### 档位 C：云端单张解释兜底

当本地表情分类置信度低，或用户明确问“他是什么表情”时，再调用小智现有单张照片解释。

这条路径只用于语义兜底，不用于实时状态驱动。

## 推荐实现阶段调整

### 阶段 1：人脸框驱动互动

- 接入人脸检测。
- 输出人脸有无、位置、大小。
- 驱动眼睛看向人脸方向。
- 舵机做轻微跟随。
- 不做表情分类。

目标：先实现“宠物看到人，并看向人”。

### 阶段 2：人脸转动/靠近远离

- 基于人脸框中心和面积做左右/上下/靠近远离状态。
- 如果模型输出关键点，再增加鼻尖/眼睛相对位置判断。
- 加入状态稳定和冷却，避免抖动。

目标：实现“用户转头或靠近时，宠物有反应”。

### 阶段 3：夸张表情识别

- 先尝试关键点规则。
- 若效果不足，再接小型 ESP-DL 表情分类模型。
- 表情只输出高置信度事件，低置信度统一为 `emotion_unknown`。

目标：支持明显的开心、生气、难过、惊讶互动。

### 阶段 4：小智对话联动

- 本地检测到高置信度表情时，先只驱动电子宠物表现。
- 连续稳定且达到触发条件时，再选择是否 `WakeWordInvoke()`。
- 用户主动询问时，使用 `Capture()` + `Explain()` 做单张照片语义解释。

目标：做到“实时反应在本地，语言解释走小智”。

## 本地实时人脸视觉状态层

第一版不做复杂大模型视觉，先做围绕人脸互动的轻量状态识别：

- 人脸存在：判断小智面前是否有人。
- 人脸位置：判断用户在画面左侧、右侧、上方、下方或中心。
- 人脸距离趋势：通过人脸框面积判断用户靠近或远离。
- 人脸转动：优先用人脸框位置近似，后续用关键点提高准确度。
- 面部表情：优先识别夸张的开心、生气、难过、惊讶。
- 明暗变化：判断是否进入暗光/强光。
- 摄像头可用性：判断画面是否黑屏、过曝、无帧。

建议输入参数：

- 分辨率：优先使用低分辨率，例如 320x240 或更低的裁剪/缩放结果。
- 帧率：2-5 FPS 起步，不追求高帧率。
- 处理频率：状态识别每 200-500 ms 一次。
- 上报频率：状态变化才上报给 `PetController`，避免刷新过多。

第一版状态枚举建议：

- `human_face_none`：未检测到人脸。
- `human_face_center`：人脸位于画面中心。
- `human_face_left`：人脸偏左或向左转。
- `human_face_right`：人脸偏右或向右转。
- `human_face_up`：人脸抬头或偏上。
- `human_face_down`：人脸低头或偏下。
- `human_approaching`：用户靠近。
- `human_leaving`：用户远离。
- `human_emotion_happy`：明显开心。
- `human_emotion_angry`：明显生气。
- `human_emotion_sad`：明显难过。
- `human_emotion_surprised`：明显惊讶。
- `human_emotion_neutral`：无明显表情。
- `human_emotion_unknown`：表情无法稳定判断。
- `dark`：环境偏暗。
- `bright`：环境过亮。
- `camera_error`：摄像头不可用或画面异常。

这些状态用于电子宠物表现，不要求一开始达到高精度。

## 与小智唤醒的关系

小智框架仍然保留语音唤醒为主入口，但视觉事件可以作为“内部触发源”。

推荐分三档处理：

### 1. 只驱动宠物表现

默认路径。视觉状态只影响眼睛、灯带、舵机，不触发小智对话。

示例：

- 看到人脸：眼睛看向人脸方向，灯带变亮。
- 用户转头：眼睛或舵机轻微跟随。
- 用户开心：电子宠物显示开心表情。
- 用户生气/难过：电子宠物显示关心或安抚动作。
- 环境变暗：眼睛变困，灯带低亮度。

### 2. 触发单张照片解释

当本地状态足够明确，但需要语义解释时，调用现有 `Camera::Capture()` + `Camera::Explain()`。

示例：

- 长时间检测到人停留。
- 检测到高置信度但需要语言解释的夸张表情。
- 用户语音问“我现在是什么表情”或“你看到我了吗”。

注意：这仍是单张照片解释，不是视频理解。

### 3. 视觉事件触发小智对话

当视觉事件达到阈值，可调用 `Application::WakeWordInvoke()` 走小智现有唤醒流程，传入一个合成唤醒文本或事件摘要。

示例事件文本：

- `<detect>human_face_present</detect>`
- `<detect>human_approaching</detect>`
- `<detect>human_emotion_happy</detect>`
- `<detect>human_emotion_sad</detect>`

这个做法在仓库里已有可参考方向：`sensecap-watcher` 的摄像头逻辑会在检测到目标后调用 `Application::GetInstance().WakeWordInvoke(wake_word)`。AI 宠物可以沿用这个设计思路，但事件来源换成 `PetVisionService`。

## 事件门控策略

为了避免宠物频繁打断和重复上传，需要门控：

- 同类视觉事件冷却时间：建议 30-120 秒。
- 拍照解释冷却时间：建议 60-180 秒。
- 对话触发冷却时间：建议 120-300 秒。
- 正在聆听/说话时不主动触发新视觉对话，只更新电子宠物的眼睛表情和动作。
- Wi-Fi 未连接、激活中、升级中、错误状态下，不触发视觉解释。
- 电量低或温度高时，降低视觉帧率或暂停主动视觉。

状态稳定条件：

- 单帧检测只作为候选。
- 连续 N 次命中才确认状态，例如 3 次。
- 连续 M 次未命中才退出状态，例如 5 次。

## 与现有 Camera 接口的兼容

现有 `Camera` 接口只提供：

- `Capture()`
- `Explain(question)`
- `SetExplainUrl(url, token)`
- 翻转/镜像控制

不建议直接把它改成实时视频接口，因为会影响现有 MCP 工具和其他板卡。

建议新增内部接口：

```cpp
class PetVisionService {
public:
    void Start();
    void Stop();
    PetVisionState GetState() const;
    void SetEventCallback(std::function<void(const PetVisionEvent&)> callback);
};
```

`PetVisionService` 可以和 `EspVideo` 共享更底层的视频帧获取能力，但对外不要改变 `Camera` 的单张拍照语义。

如果后续需要实现，建议把 `EspVideo` 拆出一个内部帧读取能力：

- `Capture()`：保留现有单张照片语义。
- `ReadPreviewFrame()` 或 `TryReadFrame()`：仅供本地视觉服务使用，不暴露给 MCP。

## 分阶段实现

### 阶段 1：状态模拟与电子宠物表现闭环

- 先不接真实视觉算法。
- 增加 `PetVisionState` 和 `PetController` 对接。
- 用定时器或调试命令模拟 `human_face_center`、`human_approaching`、`human_emotion_happy`、`dark` 等状态。
- 验证眼睛、灯带、舵机反应是否符合预期。

目标：先打通“人脸视觉状态 -> 电子宠物行为”的链路。

### 阶段 2：低频真实人脸检测

- 从摄像头获取低频帧。
- 实现人脸有无、位置、大小检测。
- 用人脸框中心和面积输出转向/靠近远离状态。
- 加入连续命中/连续丢失的状态稳定逻辑。
- 不触发小智，只驱动电子宠物表现。

目标：实现真正的本地实时人脸互动状态。

### 阶段 3：夸张表情识别

- 先基于关键点或小型表情分类模型识别开心、生气、难过、惊讶。
- 表情事件必须连续稳定，低置信度输出 `human_emotion_unknown`。
- 表情结果先只驱动电子宠物动作，不主动打断对话。

目标：让电子宠物能读懂用户明显表情并回应。

### 阶段 4：事件触发单张解释或小智回应

- 当本地状态满足条件时，可调用现有 `Capture()` 和 `Explain()` 做语义兜底。
- 增加视觉事件到 `WakeWordInvoke()` 的桥接。
- 只对高置信度、高价值的人脸/表情事件触发。
- 触发前检查设备状态，避免打断正常对话。
- 服务端收到 `<detect>...</detect>` 后，可以按系统提示决定是否主动回应。

目标：实现“电子宠物看到用户重要表情后主动回应”，但不是实时视频对话。

### 阶段 5：更强视觉能力

可选增强方向：

- ESP-WHO/ESP-DL 人脸检测、关键点、表情分类模型。
- 外接 K230/其他视觉模组进行本地识别。
- 局域网视觉服务，ESP32-P4 只上传低频帧。
- 后端支持视频流后，再评估 MJPEG/WebRTC/H.264 路线。

这些不进入第一版。

## 配置项建议

新增 Kconfig：

- `PET_VISION_ENABLE`
- `PET_VISION_LOCAL_STATE_ENABLE`
- `PET_VISION_FRAME_INTERVAL_MS`
- `PET_VISION_EVENT_STABLE_COUNT`
- `PET_VISION_EVENT_LOST_COUNT`
- `PET_VISION_EXPLAIN_COOLDOWN_SEC`
- `PET_VISION_WAKE_COOLDOWN_SEC`
- `PET_VISION_TRIGGER_WAKE_WORD`
- `PET_VISION_TRIGGER_EXPLAIN`
- `PET_VISION_LOW_POWER_DISABLE`
- `PET_FACE_DETECT_ENABLE`
- `PET_FACE_LANDMARK_ENABLE`
- `PET_FACE_EMOTION_ENABLE`

默认建议：

- 启用本地视觉状态。
- 视觉目标默认聚焦人脸互动。
- 默认不启用视觉事件主动唤醒小智。
- 默认允许用户语音请求时拍照解释。
- 主动解释和主动唤醒都需要后续显式开启。

## 风险和边界

- 实时状态识别不等于实时语义理解；第一版只做人脸互动状态，不做复杂画面描述。
- 单张解释有网络延迟，不适合驱动高频电子宠物动作。
- 视觉主动唤醒容易打扰用户，必须有冷却和置信度门槛。
- 摄像头、双眼 SPI、音频、WS2812、舵机同时运行时，要重点验证任务优先级和内存。
- 不修改小智原始 MCP 摄像头语义，避免影响 `self.camera.take_photo`。

## 结论

推荐采用“双通道视觉”：

- 本地实时人脸视觉状态通道负责电子宠物即时反应。
- 小智单张照片解释通道负责语义解释。

这样可以满足电子 AI 宠物“看见用户、读懂明显表情、做出反应”的体验，同时不强行突破小智当前“语音唤醒 + 单张拍照”的框架边界。
