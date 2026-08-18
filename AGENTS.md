# AGENTS.md — xiaozhi-esp32（AI Pet 固件 Git 仓）

> 本文件适用于真正的固件 Git 根。先读 `D:/Home_Work/AGENTS.md`，再读上层 `D:/Home_Work/ESP32_XIAOZHI/AGENTS.md`；两者的协作和硬件约定优先于本文件。

## 开工与范围

1. 按根规则安全检查/同步 `work_dashboard`，再阅读项目全景和 `AI-Pet固件联调看板.md`。
2. 动手前阅读 `AI_PET_PROGRESS_zh.md`、`AI_PET_DEV_PLAN_zh.md` 及任务涉及的 `AI_PET_EYE_*` 或上游协议文档。
3. 仅修改本 Git 仓内代码与文档。新板型、新功能使用派生目录，不改旧 7B 板型；不要修改上层母文档资料，除非用户明确要求。

## 验证与收工

- 默认增量执行 `idf.py build`；需要烧录时使用 `idf.py flash`，并用 `idf.py monitor` 与真机目视/语音完成验证。非必要不 clean。
- 代码改动后必须更新 `AI_PET_PROGRESS_zh.md`，并在固件联调看板更新对应集成点与进度日志；看板提交遵守根目录的安全提交规则。
- OTA、WS 地址、MAC/UUID 和密钥均以联调看板或安全配置为准，不硬编码或写入 Git。
