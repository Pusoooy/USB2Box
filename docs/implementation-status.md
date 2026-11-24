# Implementation Status / 实现状态

基线 REQ-BASE-2026-11-23-01。ARM、Host、SDK、GUI 和 MCP 的云端软件回归通过。Windows 实机、硬件 HIL、合法 USB 标识及发行审查仍待完成。

| 阶段 | 模块 | 状态 | 备注 |
|---|---|---|---|
| M0 | 基线、候选资源、帧头 | HOST_MOCK_PASS（单测） | ST 封装/AF 已校验。DMA 候选静态检查通过，待对照 RM0368 原表 |
| M1 | ARM Makefile + C11 启动与初始化 | ARM_BUILD_PASS / HOST_MOCK_PASS | 固定 CMSIS/HAL、手写启动与 RCC/GPIO/NVIC/OTG FS BSP。默认 HSI，显式 HSE 的时钟与 USB 电气待实板 |
| M2 | TinyUSB / Vendor Bulk / WinUSB | ARM_BUILD_PASS / HOST_MOCK_PASS | 有界组帧和租约通过。0x82 已接入 ADC credit 流，WinUSB 枚举与物理端点吞吐待测 |
| M3 | DMA/TIM/GPIO 资源管理与任务队列 | HOST_MOCK_PASS | 驱动已接入原子预留，I²C/SPI 作业使用分通道排队、超时、取消 |
| M4 | 双UART | HOST_MOCK_PASS | USART1/2 DMA 双路回环 Mock，0x82 双订阅携带偏移、时间和丢失计数 |
| M5 | 双I²C | HOST_MOCK_PASS | 双总线中断、组合事务、从机八事件 FIFO 与溢出统计，实板弱上拉待测 |
| M6 | 双PWM | HOST_MOCK_PASS | TIM5/TIM3 独立频率组、周期边界更新、限时、安全停机和 TIM4 输入捕获；波形待测 |
| M7 | SPI1 | HOST_MOCK_PASS | SPI1 主机 4 模式及片选资源，DMA 全双工、分段和取消 |
| M8 | ADC1 | HOST_MOCK_PASS | ADC1 扫描、软件/定时器触发，有限及持续采集、超量停止，0x82 credits |
| M9 | Python SDK/CLI | HOST_MOCK_PASS | 同步/异步 API、客户端独占、共享 C 模拟器与 wheel；Windows 无实测 |
| M10 | PySide6 GUI | HOST_MOCK_PASS | 六页共享 SDK 工作台，后台线程与 ADC 曲线，Linux offscreen 冒烟通过 |
| M11 | MCP stdio Server | HOST_MOCK_PASS | 16 个 stdio 工具、能力与状态资源、输出限额和权限检查 |
| M12 | 集成、Windows 离线工作台与验证入口 | ARM_BUILD_PASS / HOST_MOCK_PASS | Windows x64 包已生成并静态校验，Debian ARM 缓存可离线重建。Windows 运行与最终发行验收待完成 |

状态标签：NOT_IMPLEMENTED、IN_PROGRESS、SOFTWARE_VERIFIED、WAITING_HARDWARE、HARDWARE_VERIFIED、BLOCKED、FAILED。必须以命令输出、提交SHA、原始日志支撑通过状态。
