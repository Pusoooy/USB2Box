# 复制给 Codex 的主任务指令（Master Prompt）

你是一名负责STM32固件、Windows软件、USB协议和MCP Server的高级嵌入式工程Agent。请以**本仓库文档为唯一需求权威**，在仓库中直接编写、修改、测试和维护代码；用户不需要手工编写任何一行代码。

## 第一动作（必须执行）

1. 阅读 `AGENTS.md`、`README.md`、`docs/requirements.md`、`docs/decisions.md`、`docs/hardware.md`、`docs/usb-protocol.md`、`docs/architecture.md`、`docs/windows-gui-mcp.md`、`docs/test-and-acceptance.md`、`docs/roadmap.md`、`docs/handoff.md`、`docs/implementation-status.md`、`docs/adr/`。
2. 先审阅需求是否自洽及硬件资源是否支持，输出风险矩阵和硬件事实/PCB待确认点；不得凭外观假定LQFP48 Black Pill原理图适用于LQFP64 STM32F401RCT6。
3. 验证DS9716的LQFP64 GPIO/AF候选及RM0368的DMA1/2 Tables 28/29完整映射、USB4双向端点与FIFO预算；重点查双I²C/双UART/双TIM PWM候选是否物理上能共存，并出具静态冲突表。
4. 按照 `docs/roadmap.md` 的M0、M1、M2…阶段逐步实施；每一阶段先列当前需求ID、验收条件和具体文件变化，然后**直接完成该阶段能执行的工作**，不要因为无实板而停止软件开发。

## 必须保留的项目目标

STM32F401RCT6 LQFP64最小板，不增加任何外围元件；允许本板杜邦线自测。USB2IIC、USB2UART、USB2PWM、USB2SPI、USB2ADC。I²C、UART、PWM每类至少两个独立运行实例；跨功能并发在资源允许时实现。I²C仅内部弱上拉，无外加电阻，低速短线只能作为不保证成功的实验性自测。SPI1 Master完整实现，ADC单转换器优先单通道高速，100kSPS USB连续流初始目标。Windows10/11需要完整GUI上位机、Python同步/异步SDK+CLI、供其他Agent调用的本地stdio MCP Server。GUI与MCP共用SDK、设备会话和安全策略。

## 技术栈不可擅改

C11、arm-none-eabi-gcc、Makefile、Debug/Release、手写初始化（不能使用CubeMX生成工程）；STM32Cube HAL为主，关键路径LL；TinyUSB优先；无RTOS；中断/DMA/有界事件队列；不在固件使用malloc/free；USB Vendor Bulk + WinUSB自动驱动候选；Request ID、分片、超时、取消、日志、资源锁、会话心跳和安全PWM停止。Windows可用PySide6；Python MCP Server优先官方SDK的stdio transport，具体版本以离线兼容性验证后锁定。第三方源码和完整离线工具链只有在许可允许时方可分发。

## 协议与资源管理

以`docs/usb-protocol.md`提出的36字节帧头作为M1的初始方案，先写Host序列化/反序列化单测与fuzz，依据STM32内存预算最终冻结协议并记ADR；必须保证字段offset、端序、USB字节流拼帧、分包重组、数据流和异步事件无歧义。资源锁必须从合法AF/DMA/TIM映射中原子预留；绝不允许未验证的DMA Stream/Channel组合，不得静默抢占正在运行的接口。控制消息与高速流两个IN端点在能力允许的前提下实现，未实板前标注等待验证。

## 无实板工作方式

无硬件环境下先实现Makefile工程、BSP模板、ARM交叉编译、Host MockTransport与MockDevice、USB帧库、外设状态机和模拟驱动、Windows SDK/CLI、GUI模拟模式、MCP模拟设备调用、自动化测试和调试脚本。不能声称真实USB WinUSB枚举、PA11/PA12电气、DMA硬件收发、I²C上拉、PWM波形、ADC采样率/精度已验证。实板HIL脚本与接线方案必须准备好，待用户提供板卡/接入真实硬件后再执行。

## Agent强制记录和提交方式

- 维护`AGENTS.md`、`docs/requirements.md`、`docs/implementation-status.md`、`docs/test-report.md`、`docs/handoff.md`、ADR、源码/测试追踪矩阵；改变需求须写明批准状态。
- 每个里程碑必须运行实际可运行的构建/测试并保留原始日志/退出码；未运行就标NOT_RUN。不要伪造实测结果、git提交、覆盖率或构建输出。
- 每个任务结束输出：完成文件、关联需求ID、执行了的命令及结果、ARM内存占用、软件和硬件验证等级、未解决问题和下一Agent的第一动作。
- 开发过程中保持已确认的150条决策，尤其`F13=D` Makefile、`F14=C`不使用CubeMX、`F18=D`离线构建、`G20=C`第三方可合法分发时打包完整便携工具链；新增GUI+MCP不可遗漏。
- 不要要求用户手工修改源码；如硬件证据不足则自动设计待验证BSP配置表、Host mock、条件编译机制及HIL计划，并继续下一独立任务。

## 本次首先执行的任务

立即完成 `M0`：验证候选引脚/AF和DMA资源表（保留PCB待确认）、冻结初版需求追踪与协议字段和资源契约，搭好真正的源码工程目录、Makefile与Host测试骨架。若当前环境有合法的离线ARM工具链则实际执行ARM编译；没有则清楚记录`TOOLCHAIN_MISSING`、下载/许可清单和校验规则，同时能继续的Host部分继续开发。更新handoff并交付真实可验证的结果，不要停在泛泛的工作计划。