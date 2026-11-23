# STM32F401RCT6 USB2XXX — Codex开发规格书与任务包

- 文档基线：**2026-11-23 / REQ-BASE-2026-11-23-01**
- 阶段：**设计与开发指令准备完成；尚无实际固件/GUI/MCP代码、ARM编译或HIL结果**。
- 内容：基于用户七组互动问卷的 **150项技术决策**，包含未问卷化但已明确追加的 Windows GUI 和 MCP Server 要求。
- 范围：裸STM32F401RCT6最小板（不加外围电路），USB2IIC、USB2UART、USB2PWM、USB2SPI、USB2ADC；每类I²C/UART/PWM至少双路独立运行；Windows10/11 API、GUI、MCP。

## 阅读和交接顺序

1. **`AGENTS.md`**：每位Agent强制行为规则。
2. **`docs/requirements.md`**：优先级、功能、验收与需求基线。
3. **`docs/decisions.md`**：150条完整已确认选项。
4. **`docs/board-identification.md`**：用户已确认开发板与前述参考图片一致；区分板型确认与电气待核验。
5. **`docs/hardware.md`**：芯片限制、候选引脚、DMA冲突、PCB电气待确认项。
5. **`docs/usb-protocol.md`**：USB端点、候选二进制帧、分包与错误码。
6. **`docs/architecture.md`**：裸机事件循环、资源管理、64KiB内存预算。
7. **`docs/windows-gui-mcp.md`**：Windows SDK/GUI/MCP工具清单和安全政策。
8. **`docs/roadmap.md`**：M0-M12阶段任务、依赖及门禁。
9. **`docs/test-and-acceptance.md`**：Mock与HIL测试及证据标准。
10. **`docs/implementation-status.md`** 和 **`docs/handoff.md`**：当前进度与新Agent接手位置。
11. **`prompts/CODEX_MASTER_PROMPT.md`**：直接粘贴给Codex执行的主任务指令。
12. **`prompts/TASKS.md`**：阶段任务拆解与任务完成模板。

## 结构化表

- `schemas/board_status.json`：开发板确认层级与电气待核验事项（机器可读）。
- `schemas/pinmap_candidate.csv`：STM32芯片AF层面的可行基础候选，引出情况待实板确认。
- `schemas/dma_candidate.csv`：官方RM中已核查的DMA请求候选。
- `schemas/requirements_trace.csv`：150决策追踪起始表（初始状态均未实现/待软件开发）。
- `docs/sources.md`：ST、TinyUSB、MCP权威资料链接。

## 有意不包括的内容

当前文件包是**需求/设计/任务交接包**，不是已实现产品的离线发行包。没有ARM工具链、STM32驱动源码、TinyUSB源码、Python wheel、编译产物或实板日志。后续Codex的M1/M12必须按G20=C合法打包便携工具链与第三方依赖、锁版本并校验SHA256；这一步需要Agent实际下载/取得许可后完成。**不要把当前ZIP解释为G20已经执行。**

## 硬件阻塞与已知风险

**开发板选型/外观已由用户确认：STM32F401RCT6 黑色PCB、Type-C、Black Pill风格（与此前参考图一致）。** PCB厂商/精确版本、HSE、USB D+/D-/VBUS及Type-C CC、引脚引出仍未获得物理证据；I²C只依赖内部弱上拉不能保证稳定的100kHz或更高速；ADC单个转换器，不能多通道真正同时采样；USB FS无法持续无损传送2.4MSPS×16bit的数据；WinUSB自动绑定与ROM DFU均需要实板验证；USB合法VID/PID尚未分配。