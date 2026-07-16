# 魔眼素材：GPT Image / Grok Image 完整生产流程

> 适用：全部眼睛图都用 **ChatGPT GPT Image** 或 **Grok Image** 生成。  
> 目标：做出风格统一、可上板的 **包 B（32 帧）**，再转成固件可用 RGB565。  
> 关联：`AI_PET_EYE_ASSETS_SPEC_zh.md`（命名与构图）· `scripts/convert_eye_pic.py`（转 bin）  
> 版本：2026-07-17

---

## 0. 原则（先记住 4 条）

1. **先定锚点（1 张主视觉），再出序列**——不要 32 张各自乱生成。  
2. **每一批都带「参考图 / edit」**——用锚点当 reference，只改亮度/眼皮/脉动。  
3. **只做右眼（或通用正视眼）一套**——左眼固件水平镜像，不要再出镜像图。  
4. **交付 PNG 240×240（或先高清再脚本缩）**——固件侧统一转 RGB565，不直接烧 JPG。

---

## 1. 工具怎么选

| 场景 | 更推荐 | 原因 |
|------|--------|------|
| 定 **主视觉锚点**（第一张「这就是我们的眼睛」） | **两边各出 4～8 张，人工选 1 张** | 风格探索阶段，GPT / Grok 审美不同，选更好看的 |
| **同角色连续帧**（idle/blink/speak） | **以锚点为 reference 的工具为主做完一整套** | 一致性 > 单张好看；中途不要换模型乱切 |
| 眨眼眼皮不好控 | 优先 **Grok Image / GPT 的 image edit**，上传锚点 +「只加眼皮」 | 纯文生图容易换脸 |
| 已有不错 idle，要 speak 脉动 | **edit 锚点**：「更亮、星云更活跃、瞳孔中心不动」 | 比重新 txt2img 稳 |

**实操建议：**

- **锚点竞选**：GPT Image + Grok Image 各跑一轮 → 选定 **1 张 Master**。  
- **整包生产**：锁定 **同一个工具 + 同一张 Master 作参考**，一口气做完 32 帧。  
- 另一工具只在 Master 不满意时重开竞选，**不要** idle 用 GPT、blink 用 Grok 混风。

---

## 2. 目录与命名（从生成第一天就定死）

在工程里建议：

```text
eye_pic/                          # 你已有的实验区，可继续用
  master/                         # 锁定的锚点
    eye_master.png                # ★ 唯一主视觉
    eye_master_source.txt         # 记录：工具、日期、完整 prompt
  raw/                            # AI 原始导出（可 1024 等，未改名）
    gpt/  或  grok/
  select/                         # 人工筛选后的候选
  final/                          # 验收通过、按规格命名
    png_240/
      eye_idle_00.png … eye_idle_11.png
      eye_blink_00.png … eye_blink_05.png
      eye_speak_00.png … eye_speak_09.png
      eye_listen_00.png … eye_listen_03.png
  preview/                        # 可选：拼成 GIF 自检
```

命名必须与 `AI_PET_EYE_ASSETS_SPEC_zh.md` 第 4 节一致（`eye_idle_00` 这种，**两位数**）。

---

## 3. 总体步骤总览（一张图）

```text
① 写死风格卡（主色/构图）
        ↓
② GPT + Grok 各出锚点候选 → 选定 eye_master.png
        ↓
③ 用 Master 作 reference，按顺序产序列：
   idle 12 → blink 6 → listen 4 → speak 10
        ↓
④ 人工验收（清单见 §7）→ 丢弃跳帧/换脸
        ↓
⑤ 统一裁切缩放到 240×240 PNG，圆外纯黑
        ↓
⑥ python scripts/convert_eye_pic.py（或扩展脚本）→ RGB565 .bin
        ↓
⑦ 拷进板级 assets / embed → 增量编译烧录 → 真机看循环与眨眼
        ↓
⑧ 不行就：只重做失败的那几帧（仍带 Master reference）
```

---

## 4. 分步操作（按天可执行）

### 步骤 1：写「风格卡」（5 分钟，后面每张都贴）

复制保存为 `eye_pic/master/style_card.txt`：

```text
角色：电子宠物单只圆眼（无身体）
主色：aurora_blue 极光蓝，虹膜 #2EC8FF～#4A6BFF，可有少量品红星云点
构图：正方形，眼睛居中铺满，圆外纯黑，瞳孔中心固定，左上高光 1 大 1 小
风格：可爱 + 科幻星云，游戏 UI 图标质感，清晰锐利
禁止：文字、UI、logo、双瞳、人脸、耳朵、血管写实眼、透明棋盘格背景
左右：只做正视通用眼；不要左右成对
技术：最终显示 240×240 圆屏 GC9A01；细节不要细过 2px 的碎线（RGB565 会糊）
```

---

### 步骤 2：生成 Master 锚点（最重要）

#### 2.1 文生图 Prompt（锚点 Master）

优先用下面 **中文主 prompt**（给 GPT Image / Grok Image 均可）。  
若某工具中文漂移严重，再用文末英文对照版。

**中文 · 主推（直接整段复制）：**

```text
一只电子宠物的单眼特写，正面直视镜头，正方形1:1构图。
巨大的圆形眼球必须铺满画面（虹膜外缘几乎顶到画布四边，只留极少黑边），圆外必须是纯黑色背景，不要渐变灰底，不要桌面，不要环境。
禁止把眼睛画得很小放在画面正中留一圈大黑边——要填满整个圆屏。
风格：可爱科幻、精致游戏UI图标感，微立体玻璃质感，清晰锐利，不要写实恐怖，不要人类眼球血管。

虹膜：极光蓝到电光青的发光星云（主色偏 #2EC8FF～#4A6BFF），内部有柔和的丝状星云与少量克制的品红/淡紫微光点，层次干净，不要杂乱噪点。
瞳孔：正中深青黑色圆形，中心有一颗很小的亮核；瞳孔边缘清晰。
高光：左上方两处冷白色镜面高光（一大一小），位置固定、干净。
状态：完全睁开，不要眼皮、不要睫毛、不要闭眼、不要眼泪。

只要这一只眼睛，不要身体、不要耳朵、不要第二只眼、不要双手托举、不要外框屏幕、不要手机、不要文字、水印、Logo、字幕、UI按钮。
居中对称，瞳孔在画面正中，适合做成圆形智能硬件表情屏。
```

**中文 · 加强约束版（出图爱跑偏、出人脸/双瞳时用）：**

```text
产品级电子宠物「圆屏表情」用的单只魔眼图标，正面、居中、正方形。
画面里只有一枚圆形发光眼睛，四周纯黑。
极光蓝星云虹膜 + 深色圆瞳孔 + 左上双高光，全睁。
禁止：人脸、五官、双瞳、动物、手、身体、相框、显示器边框、文字、水印、真实眼白血丝、恐怖风格、背景物品。
要求：对称、干净、高清、像高端玩具/游戏技能图标，细节柔和（避免过碎的星点）。
```

**中文 · 短版（有字数限制时）：**

```text
单只圆形电子宠物眼睛，正面铺满正方形，圆外纯黑，极光蓝发光星云虹膜，
深瞳孔小亮核，左上冷白双高光，全睁，可爱科幻游戏图标风，无文字无身体无UI
```

**英文对照（中文效果差时备用）：**

```text
Single circular robotic pet eye, front view, fills a square 1:1 frame,
pure black outside the circle, large cute sci-fi nebula iris,
aurora electric blue-cyan glow (#2EC8FF to #4A6BFF) with subtle magenta stardust,
deep dark centered pupil with tiny bright core, two cold-white highlights top-left,
fully open, no eyelid, premium game UI icon, sharp, no text, no body, no second eye
```

#### 2.2 每个工具的操作

| 工具 | 建议设置 | 数量 |
|------|----------|------|
| **ChatGPT · GPT Image** | 正方形 1:1；尽量选高清；可写 “square 1024” | 出 **6～8** 张变体 |
| **Grok Image** | 正方形；同一 prompt 多跑几次 | 出 **6～8** 张变体 |

#### 2.3 选定 Master 的标准

- 圆外够黑、眼睛够圆、高光位置舒服  
- 星云有层次但不脏  
- **瞳孔大致在正中**（方便后面所有帧对齐）  
- 缩到 240 预览仍清楚（可把图缩到手机圆图标大小看）

保存为：

```text
eye_pic/master/eye_master.png
```

并记下用的是 GPT 还是 Grok（整包后续都用同一工具 edit）。

---

### 步骤 3：生产 idle 呼吸 12 帧（第一批序列）

**目标：** 瞳孔/高光/虹膜半径几乎不动，只变「亮度 + 星云相位」。

#### 3.1 推荐方式：Reference / Edit（不要纯文生）

1. 上传 `eye_master.png`  
2. 使用 **Edit / 基于参考图生成**（GPT 的 edit、Grok 的 image-to-image）  
3. 每次只改一点点，按序号 00→11 做  

**Edit 提示词模板（把 {N} 换成 0～11）：**

```text
Use the reference image as the exact same eye design.
Keep pupil center, iris radius, highlight positions, and color palette identical.
Only change: subtle nebula brightness and micro particle flow for a seamless
breathing loop. Fully open eye, no eyelid.
This is frame {N} of 12 (0=dimmest calm, 4=brightest alive, 11=back toward dim).
Square, pure black outside the circle, no text, no redesign.
```

**亮度节奏建议（方便你填 {N}）：**

| 帧 | 强度 |
|----|------|
| 00 | 最暗/最静（基准） |
| 01–03 | 渐亮 |
| 04 | **最亮** |
| 05–07 | 回落 |
| 08–09 | 星云相位微变 |
| 10–11 | 接回 00 |

#### 3.2 若模型不支持强参考

用「固定前缀 + frame 说明」文生，但 **每张都把 Master 图贴进对话**，并写：

```text
Match this reference eye exactly. Same design. Frame {N}/12 breathing only.
```

出图后若换脸：丢弃重做，不要凑合。

#### 3.3 命名落盘

```text
eye_pic/final/png_240/eye_idle_00.png … eye_idle_11.png
```

（若导出是 1024，先全部放 `raw/`，步骤 5 再统一缩。）

---

### 步骤 4：生产 blink 6 帧

**关键：** 必须基于 **同一只 open 眼**（建议用 `eye_idle_00` 或 Master），只加眼皮。

**Edit 提示词：**

```text
Same eye as reference, identical iris and highlights.
Natural upper eyelid blink animation keyframe only.
Frame {N} of 6:
0=fully open, 1=30% closed, 2=70% closed, 3=nearly closed with thin highlight slit,
4=50% open, 5=fully open again.
Do not change eye color or pupil position. No text. Black outside circle.
```

| 帧文件 | 内容 |
|--------|------|
| `eye_blink_00.png` | 全睁 |
| `eye_blink_01.png` | 闭 ~30% |
| `eye_blink_02.png` | 闭 ~70% |
| `eye_blink_03.png` | 近全闭（留缝） |
| `eye_blink_04.png` | 睁 ~50% |
| `eye_blink_05.png` | 全睁 |

**自检：** 把 6 张快速翻页，应是「眨一下」，不是 6 只不同的眼睛。

---

### 步骤 5：生产 listen 4 帧

基于 Master / idle_00：

```text
Same eye as reference. Listening / focused expression.
Pupil slightly larger (about 5-10%), higher contrast, brighter highlights,
still fully open, no blink. Frame {N} of 4 subtle pulse. No redesign.
```

命名：`eye_listen_00.png` … `eye_listen_03.png`

---

### 步骤 6：生产 speak 10 帧

基于 Master：

```text
Same eye as reference. Speaking energy pulse, fully open (no eyelid).
Rhythmically brighter glow and more active nebula core, then settle.
Frame {N} of 10 talking loop (0=base slightly brighter than idle,
3=peak energy, 9=return to base). Keep pupil center fixed. No mouth, no face.
```

命名：`eye_speak_00.png` … `eye_speak_09.png`

---

### 步骤 7：人工验收（上板前必做）

在电脑上把 `final/png_240` 做成四宫格或 GIF（可用任意看图/PS/在线工具）：

| 检查项 | 标准 |
|--------|------|
| 数量 | 正好 32 张，命名两位数 |
| 一致性 | 缩略图墙上看，像同一只眼 |
| 圆外 | 纯黑，无灰底、无棋盘 |
| idle | 循环首尾能接（00 与 11 接近） |
| blink | 只有眼皮在动 |
| speak/listen | 全睁，无误生成闭眼 |
| 文字 | 无任何字母/水印 |

**失败帧处理：** 只重做该帧，**继续带 Master + 相邻帧作参考**，不要整包重骰。

---

### 步骤 8：统一成 240×240 + 转固件格式

#### 8.1 尺寸

- 若 AI 已是 1024×1024：中心裁正方形 → 缩到 **240×240**  
- 可用现有脚本逻辑（`convert_eye_pic.py` 已做 center-crop + LANCZOS）  
- 交付给脚本的源图：建议已是正方形；圆外尽量已是黑  

#### 8.2 转 RGB565

当前实验脚本：

```bash
# 在 xiaozhi-esp32 目录
python scripts/convert_eye_pic.py --src eye_pic --out main/boards/waveshare/esp32-p4-ai-pet/assets
```

**注意：** 现脚本按 `prompt-2-n-*.jpg` 命名；包 B 齐全后应改为读取 `eye_idle_XX.png` 等（或你先把 final 图批量重命名成脚本认识的名字做实验）。

单帧结果固定：

```text
240 × 240 × 2 = 115200 字节 ≈ 112.5 KB / 张
32 张 ≈ 3.52 MB  → 不要全 embed 进 4MB app，应进 assets 分区（中长期）
```

#### 8.3 烧录验证顺序

1. 先只上 **idle 6～12 帧** 看循环与颜色（确认字节序已修）  
2. 再加 **blink** 看插入眨眼  
3. 最后接 listen/speak 状态机  

```text
idf.py build flash
```

---

## 5. 两套工具的「点击级」操作清单

### A. ChatGPT（GPT Image）

1. 新对话，粘贴风格卡 + 锚点 prompt → 生成多张 → 下载候选。  
2. 选定 Master，上传该图。  
3. 说：「以这张为唯一参考，生成呼吸动画第 N 帧…」（用 §3–6 模板）。  
4. 对满意帧点下载；不满意说「瞳孔偏了，高光位置必须和参考完全一致，重做」。  
5. **同一对话里连续做完一类（如 12 张 idle）**，比开 12 个新聊天更一致。

### B. Grok Image

1. 同样先 txt2img 选 Master。  
2. 使用 **带参考图 / 编辑** 能力时：上传 Master + edit 模板。  
3. 若只有文生：每条 prompt 末尾加 `exact same design as previous`，并尽量在同一会话。  
4. 导出后立刻按 `eye_xxx_NN` 重命名，避免后期对不上帧号。

### C. 混用规则（避免翻车）

| 可以 | 不可以 |
|------|--------|
| 锚点竞选阶段 GPT、Grok 都用 | idle 用 GPT、blink 用 Grok 硬拼 |
| Master 选定后全程锁定一个工具 | 中途换 prompt 主色（蓝变紫） |
| 单帧失败换工具「修一张」且强绑 Master | 失败就整包换风格重开却不改 Master |

---

## 6. 建议生产排期（一个人 + AI）

| 阶段 | 内容 | 粗估 |
|------|------|------|
| Day 0 | 风格卡 + 两边出锚点 + 锁定 Master | 0.5 天 |
| Day 1 | idle ×12 + 验收 GIF | 0.5～1 天 |
| Day 2 | blink ×6 + listen ×4 | 0.5 天 |
| Day 3 | speak ×10 + 全套验收 | 0.5～1 天 |
| Day 4 | 转 bin、上板、调 hold 时间/状态机 | 0.5～1 天 |

**最小可演示集（先做这个也能很好看）：**

- Master 1 + idle 8～12 + blink 6 ≈ **15～19 张**  
- speak/listen 可第二轮再补  

---

## 7. 与固件的对接关系

```text
GPT/Grok 出图
    → eye_pic/final/png_240/*.png
    → convert → RGB565 .bin（每张 112.5KB）
    → PetEyeDisplay 按状态播 clip
         idle 循环 | blink 插入 | speak/listen 切换
    → SPI GC9A01（注意 RGB565 字节序已在 FlushFrame 处理）
```

状态建议：

| 设备状态 | 播哪个 |
|----------|--------|
| idle / connecting 等 | `eye_idle_*` |
| listening | `eye_listen_*` |
| speaking | `eye_speak_*` + 仍可插入 blink |
| 定时 | blink 序列优先于黑条眼皮 |

---

## 8. 复制即用：会话开场白（推荐）

每次新开 AI 绘图会话，先发：

```text
You are helping me make animation frames for a 240x240 round GC9A01 pet-eye display.
I will give ONE master reference eye. All outputs must match it exactly:
same iris, pupil center, highlights, palette. Only animate what I ask
(brightness / eyelid / glow). Square image, pure black outside the eye circle,
no text, no UI, no body. I will request frames one by one with frame index.
```

然后上传 `eye_master.png`，再按帧发 edit 指令。

---

## 9. 常见问题

| 问题 | 处理 |
|------|------|
| 每帧都像新眼睛 | 没用参考图；改用 edit + 降低「创意度」；写死 Keep exact same design |
| 上板颜色怪 | 先查 RGB565 字节序（已修 FlushFrame）；不是生成问题 |
| 星云太碎、RGB565 脏 | 生成时加 soft gradients, fewer tiny sparkles |
| 眨眼像矩形 | 提示词强调 natural curved upper eyelid soft shadow |
| Flash 不够 | 32×112.5KB≈3.52MB 放 assets，勿全 embed 进 4MB app |
| JPG 发灰/有块 | 交付尽量 **PNG**；转换前不要多次重压 JPG |

---

## 10. 完成定义（Done）

- [ ] `eye_master.png` 已锁定并归档 prompt  
- [ ] `png_240` 下 32 张命名正确（或最小集 idle+blink 已齐）  
- [ ] 电脑 GIF 预览循环顺、眨眼顺  
- [ ] 转 bin 后真机颜色接近 Master  
- [ ] idle 循环 + 代码/序列眨眼在 speaking 时仍工作  

---

## 11. 下一步工程（生成完成后）

1. 扩展 `convert_eye_pic.py` 支持 `eye_idle_XX.png` 包 B 命名。  
2. `PetEyeDisplay` 按 clip 播多组帧。  
3. 帧数 >12 时迁 **assets 分区**，避免撑爆 ota 4MB。  
4. 第二只眼：共 SPI，CS 分离，软件镜像。  
