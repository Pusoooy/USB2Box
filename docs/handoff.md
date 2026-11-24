# Agent Handoff / 项目交接

**当前阶段为 M12。0.1.3 候选已通过云端软件回归，Windows 实机和 HIL 待验证。**

## 项目定位
STM32F401RCT6（256KiB Flash / 64KiB SRAM）黑色 PCB Type-C 风格裸最小系统板，USB2IIC / UART / PWM / SPI / ADC；Windows GUI、Python SDK、Agent MCP Server。不要增加任何电阻、电容或芯片，允许短杜邦线自测。I2C仅内部弱上拉。

## 接手必读
1. 根目录 `AGENTS.md`
2. `docs/requirements.md` + `docs/decisions.md`
3. `docs/board-identification.md` + `docs/hardware.md`
4. `docs/usb-protocol.md` + `docs/architecture.md`
5. `docs/windows-gui-mcp.md` + `docs/roadmap.md` + `docs/test-and-acceptance.md`
6. `prompts/CODEX_MASTER_PROMPT.md` + `prompts/TASKS.md`

## M12 交接

- 从 M11 提交 `5c9b2d7` 继续，逐阶段提交在同一历史中。M12 补齐 TIM4/PB6 输入捕获、ADC 模拟看门狗与流结束标记、设备计数和资源冲突详情、HSE 故障退出与统一 PWM 优先停机。
- 使用当前 checkout，执行 `bash tools/install_cloud.sh` 后设置 ARM GCC PATH。标准回归为 `make verify`。CPython 3.12 x86-64 的 PC 工具依赖使用 `pc/requirements-linux-cp312.lock`，完整回归运行 `make verify-all`。
- [HIL 指引](hil-fixtures.md) 给出 ST-Link 下载、USB 识别和五个夹具。首次硬件工作核验 PCB/晶振/VBUS 走线及合法 VID/PID，再执行 H00 到 H09。实物缺失时硬件记录仍为 `NOT_RUN`。
- Windows x64 wheelhouse、MinGW 模拟器、可替换 libusb 和 SHA256 清单经 Linux 构建验证。Windows 运行时需已有 CPython 3.12，待实际 Windows 10/11 测试与公开再分发审查。[离线工作台说明](windows-offline.md)
- 第二轮集成实现了双 UART 数据推送、ADC/UART 复用、有界 I²C 从机 FIFO 和 PWM 运行中频率/占空比更新。新增功能可由 SYS 12 查询，原有帧布局兼容。ADC 扫描时间使用设备侧估算，I²C 支持显式总线恢复，SPI 保留 DMA 部分数据。Linux 已验证跨进程锁的占用和释放。实板时序、Windows 真机与吞吐数据仍待采集。[测试报告](test-report.md)
- 0.1.1 基于 `4f13a34` 增加了 TIM5/PA1、TIM3/PC7 的 CH2 候选和组内共享频率管理。通过 Host 模拟重置、挂起、租约到期时关闭四个输出并释放资源。新增的 `adc-stream` 夹具记录实收速率、丢样和控制延迟。PWM 夹具测量更新前后的频率与占空比，两种夹具均已用 Mock 运行。[硬件候选与兼容性](adr/0005-pwm-channels-and-package.md)
- 0.1.2 基于 `9e6ff99`，SPI 短事务走非阻塞中断，长事务按需占用原 DMA 路径，ADC 和 SPI 共用 33 个 256 B 块。Debug 加入 HAL/项目运行断言，Release 对坏结果做局部恢复并记录 I²C/SPI 作业故障。ADC 可由现有 TIM3 PWM timebase 触发，源停止时会报告结束。STM32 官方引脚与触发/DMA 表复核通过。[ADR](adr/0006-spi-buffer-pool-and-adc-trigger.md) · [原始日志](evidence/m12-runtime-verify.log)
- 0.1.3 基于 `a6b817f`。SPI 排队作业到期会结束片选事务、回收 DMA 并保留已接收字节。原来的故障已在 Host Mock 复现。ARM 镜像门禁校验实际编译的 VID/PID、端点、BOS 和两份 WinUSB 接口记录，以及两接口 GUID 的唯一性。[原始日志](evidence/m12-spi-usb-verify.log)
- 在 Python 3.12 下运行 `make -j2 verify-all`，用 `make package-windows` 重建候选。实板到位后核验 HSE、USB 走线、实际 GPIO 引出与合法 VID/PID，再执行 H00–H09，包括 SPI 中断/DMA 切换波形与 TIM3/ADC 相位。I²C 1 MHz 和扩展接口仍是条件目标。

## 历史交接

### M1 交付

- 本地 `work` 分支从 M0 提交 `39eb6e4e2471d63f909e26f25fe295338f737e9d` 继续。本文件随 M1 变更提交。
- CMSIS Core 5.9.0、CMSIS Device F4 v2.6.10 与 STM32F4 HAL v1.8.3 已按提交和 SHA-256 锁定。默认构建在 HSI 启动。显式 HSE 候选镜像建立 84/48 MHz PLL 配置，异常时尝试回退 HSI。
- `make clean && make verify` 和两组 HSE 样本构建通过。静态 RAM 20 B，另预留 8 KiB 栈。[原始日志](evidence/m1-verify.log)
- 实际 HSE、PCB、USB 电气及 HIL 仍为 `WAITING_HARDWARE`。DS9716 与 RM0368 PDF 请求仍返回代理 403，现有候选依据见 [M0 资源审查](m0-resource-audit.md)。

### M1 当时的下一步

1. 在现有 checkout 运行 `make verify`。云环境可将 `/workspace/usb2box-tools/arm/usr/bin` 加入 PATH。依赖缓存缺失时按 [README](../README.md) 准备。
2. M2 固定 TinyUSB 版本，对照 STM32F401 OTG FS DCD 接口、端点和 FIFO，加入 USB 字节流拼帧、有界重组、Vendor Bulk、会话租约和端到端 Mock。复用 M0 的 [帧头与资源契约](adr/0002-m0-resource-contract.md)。
3. M2 进入 DCD 前检查 `bsp_init()` 返回 `BSP_CLOCK_HSE_PLL`。在设备模式、软断开及 VBUS 策略配置后调用 `bsp_usb_enable_pins()`，确保 DCD 完成 IRQ 和端点初始化之后再连接。没有合法 VID/PID 时继续用 Host Mock 验证协议。
4. 保留 PA13/PA14 SWD，I2C2 SDA 候选 PB3 AF9 及 SWO 互斥。取得实板后验证电气和走线，记录原始证据。

## M2 交付

工作基于 M1 `8e1c7b5`。先运行 `make fetch-deps && make verify`，TinyUSB 0.19.0 和其许可证已加入本地可验证依赖。下一阶段在设备命令和停止回调中接入 M3 资源与异步状态机，保留同一套 Host 设备核心。Newlib 头文件来自 Debian 签名仓库 `libnewlib-dev=4.5.0.20241231-1`，其库未链接。硬件状态及原始记录见测试报告。

## M3

基线是 M2 `36c9c4b`。驱动应先用 `resource_reserve()` 获取 GPIO/TIM/Stream，再写硬件配置，失败后释放资源。异步操作使用每通道互斥且允许跨通道进展的 `jobs`。待接入 USB CANCEL 和结果查询。验证见 [M3 日志](evidence/m3-verify.log)。

## M4

M3 为 3206d11。已接入 USART1/2 双路 DMA 和 UART USB 服务号已在 M11 统一为 2。下一阶段使用 I²C 中断以避免占用 DMA1 S5/S6。

## M5

M4 为 39b880b。USB 服务 1 已接入异步队列。暂以从机最新接收事件和主机已读字节报告部分完成。

## M6

M5 为 7b2d04c。PWM 租约停止走公共 app_stop()。有限脉冲比较中断暂有 10 µs 最小时隙，需实板测量，之后补输入捕获。

## M7

M6 为 a85cce0。SPI1 RX/TX 为 DMA2 S0/S3。CS 配置延时以 1 ms 级向上量化，GPIO 候选仍需核对引出。

## M8

M7 为 d518154。实现 TIM2 与 DMA2 S4，随后通过共享 SDK 读取。模拟 100 kSPS 目标配置未产生实测吞吐证据；ADC 模拟看门狗待补。

## M9

M8 为 71e3243。使用 make mock-device 启动 C 模拟器，Python CLI 使用 --mock。Windows 真机由 PyUSB/libusb 访问两个 WinUSB 接口，需经 HIL 验证。

## M10

M9 为 93e8588。启动 python -m usb2xxx.gui --mock；使用同一份 SDK 与 C 模拟设备。Linux offscreen Qt 的字体缓存警告不影响测试。

## M11

M10 为 18ec9a4。MCP 1.20.0 兼容 Pydantic 2.12.5，默认只读，显式配置合法设备的 VID/PID 和允许的序列号。服务编号统一回既有协议表。
