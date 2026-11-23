# Agent Handoff / 项目交接

**当前阶段：M0 设计资料迁移 GitHub 完成；未开始固件实现。**

## 项目定位
STM32F401RCT6（256KiB Flash / 64KiB SRAM）黑色 PCB Type-C 风格裸最小系统板，USB2IIC / UART / PWM / SPI / ADC；Windows GUI、Python SDK、Agent MCP Server。不要增加任何电阻、电容或芯片，允许短杜邦线自测。I2C仅内部弱上拉。

## 接手必读
1. 根目录 `AGENTS.md`
2. `docs/requirements.md` + `docs/decisions.md`
3. `docs/board-identification.md` + `docs/hardware.md`
4. `docs/usb-protocol.md` + `docs/architecture.md`
5. `docs/windows-gui-mcp.md` + `docs/roadmap.md` + `docs/test-and-acceptance.md`
6. `prompts/CODEX_MASTER_PROMPT.md` + `prompts/TASKS.md`

## 立即下一步
- 开展 M0：根据 ST 数据手册核验 STM32F401RCT6 LQFP64 的 AF/DMA 候选、封装和时钟树；整理/补全机读资源映射、软件可验证的引脚冲突测试。
- 不得因为用户确认参考图而宣称已经知道 HSE 频率、所有实际外引脚或 USB 电气走线。
- 继续 M1：纯手写启动及初始化；固定版本 ARM GCC、Makefile、Debug/Release、Host Mock 结构；不得声称已经实际编译。
- 用户不能亲自写代码；全部由 Agent 维护并记录可执行命令与结果。

## 交接更新模板
- 当前分支及提交SHA：
- 最近实际执行的命令与结果（含日志路径）：
- 文件和协议变更：
- 已验收的需求ID：
- 尚未执行 / WAITING_HARDWARE：
- 资源冲突或已知阻塞：
- 下一步可执行任务：
- 关联ADR：

**严格禁止**将编译、Mock与实板验证混淆。