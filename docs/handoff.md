# Agent Handoff / 项目交接

**当前阶段：M0 软件切片已验证，进入 M1。官方 PDF 复查仍待完成。**

## 项目定位
STM32F401RCT6（256KiB Flash / 64KiB SRAM）黑色 PCB Type-C 风格裸最小系统板，USB2IIC / UART / PWM / SPI / ADC；Windows GUI、Python SDK、Agent MCP Server。不要增加任何电阻、电容或芯片，允许短杜邦线自测。I2C仅内部弱上拉。

## 接手必读
1. 根目录 `AGENTS.md`
2. `docs/requirements.md` + `docs/decisions.md`
3. `docs/board-identification.md` + `docs/hardware.md`
4. `docs/usb-protocol.md` + `docs/architecture.md`
5. `docs/windows-gui-mcp.md` + `docs/roadmap.md` + `docs/test-and-acceptance.md`
6. `prompts/CODEX_MASTER_PROMPT.md` + `prompts/TASKS.md`

## 最近交付

- 在本地 `work` 分支运行 `make clean && make verify`，退出 0。原始记录见 [M0 日志](evidence/m0-verify.log)。基线提交为 `14065cdecc9d85d958f318d969a5d3b724a6a2f4`，本文件随 M0 变更提交。
- 候选引脚和 DMA 矩阵、验证脚本、C11 帧头编解码及手写复位镜像已进仓库。见 [ADR 0002](adr/0002-m0-resource-contract.md) 和 [资源审查](m0-resource-audit.md)。
- I2C2 SDA 使用 PB3 AF9 候选。需要核实 PCB 是否引出，并继续保留 PA13/PA14 两线 SWD。
- ST 文档站返回代理 403。RM0368 Tables 28/29 及 DS9716 PDF 复查未运行，当前依据为固定版本的 ST 官方引脚 XML 和 Embassy DMA 数据。

## 下一步

1. 使用现有 checkout，设置 ARM GCC PATH 后运行 `make verify`。
2. M1 固定 CMSIS Device、Core 与 STM32F4 HAL。实现手写板级时钟、NVIC、USB 时钟及待确认的显式 HSE 配置，保留保守启动与静态内存门禁。
3. HSE、PCB、USB 电气和所有 HIL 验收保持 `WAITING_HARDWARE`。实板获取后按硬件清单执行并记录原始证据。
