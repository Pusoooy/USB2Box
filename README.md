# STM32F401RCT6 USB2XXX — Codex开发规格书与任务包

- 文档基线：**2026-11-23 / REQ-BASE-2026-11-23-01**
- 阶段：**M12 软件集成，固件和 PC 端通过云环境回归。Windows 与实板验证进度见测试报告**。
- 内容：基于用户七组互动问卷的 **150项技术决策**，包含未问卷化但已明确追加的 Windows GUI 和 MCP Server 要求。
- 范围：裸STM32F401RCT6最小板（不加外围电路），USB2IIC、USB2UART、USB2PWM、USB2SPI、USB2ADC；每类I²C/UART/PWM至少双路独立运行；Windows10/11 API、GUI、MCP。

## 阅读和交接顺序

1. **`AGENTS.md`**：每位Agent强制行为规则。
2. **`docs/requirements.md`**：优先级、功能、验收与需求基线。
3. **`docs/decisions.md`**：150条完整已确认选项。
4. **`docs/board-identification.md`**：用户已确认开发板与前述参考图片一致；区分板型确认与电气待核验。
5. **`docs/hardware.md`**：芯片限制、候选引脚、DMA冲突、PCB电气待确认项。
5. **`docs/usb-protocol.md`**：USB端点、二进制帧、分包与错误码。
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
- `schemas/requirements_trace.csv`：需求与实际证据追踪表。
- `docs/sources.md`：ST、TinyUSB、MCP权威资料链接。

## 固件开发

使用 GNU Make、ARM GCC 14.2.1、binutils 2.44、Newlib 头文件、Python 3.11 或更新版本、Host C 编译器和 curl。云环境工具链位于 `/workspace/usb2box-tools/arm/usr/bin`。在仓库根目录运行：

```bash
make fetch-deps
make verify
```

`make fetch-deps` 按 [依赖锁](tools/arm-dependencies.lock) 下载 CMSIS Core、CMSIS Device F4 和 STM32F4 HAL。离线机器可预置 `ARM_DEPS` 下的 `cmsis-core.tar.gz`、`cmsis-device.tar.gz` 和 `hal.tar.gz`，再运行同一命令核验和解包。常规构建和 `make verify` 使用本地缓存。验证产物位于 `build/debug/` 和 `build/release/`，各含 ELF、BIN、HEX 和 MAP。Debian 云环境可用 `bash tools/install_cloud.sh` 安装或重用工具链和依赖缓存。设置 `USB2BOX_OFFLINE_CACHE` 指向两份 ARM 锁列出的 .deb 与 .tar.gz 文件，即可在本地校验、解包并将 `ARM_DEPS` 指向工具目录下的 `arm-deps` 交叉构建。

默认 `HSE_HZ=0` 使用 HSI，USB 保持未连接。实板确认晶振后可传入 `HSE_HZ=<Hz>`，产物按频率写入独立目录。支持 4–26 MHz 整数 MHz 晶振。电气和 HIL 状态见 [测试报告](docs/test-report.md)。

## 硬件阻塞与已知风险

**开发板选型/外观已由用户确认：STM32F401RCT6 黑色PCB、Type-C、Black Pill风格（与此前参考图一致）。** PCB厂商/精确版本、HSE、USB D+/D-/VBUS及Type-C CC、引脚引出仍未获得物理证据；I²C只依赖内部弱上拉不能保证稳定的100kHz或更高速；ADC单个转换器，不能多通道真正同时采样；USB FS无法持续无损传送2.4MSPS×16bit的数据；WinUSB自动绑定与ROM DFU均需要实板验证；USB合法VID/PID尚未分配。

## PC 模拟与真机入口

在仓库根目录执行：

```bash
make mock-device
PYTHONPATH=pc python3 -m usb2xxx.cli --mock status
PYTHONPATH=pc python3 -m usb2xxx.gui --mock
PYTHONPATH=pc python3 -m usb2xxx.mcp_server --mock
```

GUI 和 MCP 使用 PySide6 与官方 MCP SDK。Linux x86-64 CPython 3.12 使用 `pc/requirements-linux-cp312.lock` 中的固定版本和 SHA-256，安装后可运行 `make verify-all`。Windows 可按 [离线工作台](docs/windows-offline.md) 安装。真机使用项目已合法分配的标识以及经核验的 HSE 配置。烧录和回环参数、线序见 [HIL 指引](docs/hil-fixtures.md)。

## 仓库迁移说明

原先的任务 ZIP 为 24 项（含 `SHA256SUMS.txt`）。本 GitHub 仓库已经迁入核心设计资料、拆分文档、任务指令和结构化表；部分交接/状态资料已按最新会话要求整理为仓库版本。不要将原 ZIP 的哈希清单当作 GitHub 文件当前哈希。后续 M12 发布时应按实际仓库文件重新生成 SHA256 清单。
