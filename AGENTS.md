# AGENTS.md — USB2XXX 项目智能体规则（规范起始版）

> 当前仓库内首先提供**设计文档及Codex任务说明**，并不包含已编写好的完整固件或已执行过的测试。Agent应根据这些要求创建实际源码和可重现证据。

## 每次接手的第一步

1. 阅读 `README.md`、`docs/requirements.md`、`docs/decisions.md`、`docs/hardware.md`、`docs/usb-protocol.md`、`docs/architecture.md`、`docs/windows-gui-mcp.md`、`docs/roadmap.md`、`docs/handoff.md`、`docs/test-and-acceptance.md`。
2. 先读 `docs/board-identification.md`：**开发板种类/外观已由用户确认**，无须重复询问；但没有实物照片/原理图时，具体GPIO引出、USB走线、HSE仍属待物理核验。核对`docs/implementation-status.md`与实际文件/工具日志一致。
3. 从`prompts/CODEX_MASTER_PROMPT.md`继续当前未完成里程碑。不要重复发起问卷，也不要从零设计冲突的第二套协议。

## 不可违反的工程约束

- MCU STM32F401RCT6，裸板不焊接新电路，仅允许短杜邦线自测；5功能I²C/UART/PWM/SPI/ADC；I²C/UART/PWM各至少双路独立；Windows10/11 + GUI + Python SDK + MCP。
- **Makefile + ARM GCC**，C11；手写初始化，**不准引入CubeMX生成流程**；HAL为主、必要时LL；TinyUSB优先；裸机事件循环；固件禁止malloc/free。
- 保留USB接口与SWD；变更核心资源映射必须核对DS9716与RM0368表以及真实板级证据。
- 所有固件/PC/测试代码由Agent编写维护；不要求用户手工编写或修改代码。用户提供照片/测量结果不算写代码。
- 无硬件时先完成可完成的设计、交叉编译、Host Mock、SDK/GUI/MCP模拟器；**任何未运行的命令不可冒充真实日志或通过的测试**。
- USB VID/PID未合法分配，不得使用他厂VID/PID发布；第三方工具链和依赖若未核实再分发许可，不得打包复制。
- PWM输出在USB断开/会话租约超时/紧急停止时必须安全关闭；内部弱上拉I²C不保证高速或自测可靠性。

## 修改代码前后

- 修改前：指定需求ID、受影响模块、DMA/TIM/GPIO占用、RAM增量、协议兼容影响、测试计划。
- 修改后：更新关联测试并实际运行可执行的构建/Host测试；记录命令、退出码、日志位置、版本和不足之处。
- 需求变更必须写`docs/adr/`和变更影响清单，影响P0必须先提示用户，不得自行删除强制项。
- 每个里程碑和重要变更都要更新`docs/implementation-status.md`、`docs/test-report.md`、`docs/handoff.md`与追踪矩阵。

## 证据状态枚举

`NOT_IMPLEMENTED`、`SOURCE_PRESENT_UNTESTED`、`ARM_BUILD_PASS`、`HOST_MOCK_PASS`、`WAITING_HARDWARE`、`HIL_PASS`、`FAILED`、`ELECTRICAL_BLOCKED`。不可把Mock证据升级成实板证据。编译成功不等于硬件功能实现。

## 推荐工具命令约定（待真实工程生成）

- `make debug` / `make release`：ARM交叉编译并产生ELF/BIN/HEX/MAP。
- `make test-host` / `make test-protocol` / `make verify`：Host和质量门禁。
- `make offline-verify`：校验离线依赖、不访问网络。
- `make flash` / `make hil-verify`：需要真实ST-Link和实板；硬件缺失必须NOT_RUN。

## 不得凭空生成的材料

真实GPIO接线照片、实际USB枚举截图、示波器数据、USB吞吐实测、I²C上拉电阻值、实际HSE频率、准确PCB厂商及修订号、实际Git commit hash、代码覆盖率、真实离线工具链安装包。没有证据只能列待确认，不能写虚假的结果。