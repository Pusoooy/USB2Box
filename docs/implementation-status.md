# Implementation Status / 实现状态

基线 REQ-BASE-2026-11-23-01。ARM、Host、SDK、GUI 和 MCP 的云端软件回归通过。Windows 实机、硬件 HIL、合法 USB 标识及发行审查仍待完成。

| 阶段 | 模块 | 状态 | 备注 |
|---|---|---|---|
| M0 | 基线、候选资源、帧头 | HOST_MOCK_PASS（单测） | 47 条封装脚记录与 128 个 DMA 映射单元已对照 ST 官方 PDF，PCB 待核验 |
| M1 | ARM Makefile + C11 启动与初始化 | ARM_BUILD_PASS / HOST_MOCK_PASS | 手写初始化、固定 CMSIS/HAL、Debug 运行断言和 Release 局部错误回收。HSE 与 USB 电气待实板 |
| M2 | TinyUSB / Vendor Bulk / WinUSB | ARM_BUILD_PASS / HOST_MOCK_PASS | 有界组帧和租约、ADC/UART 复用、诊断日志。ARM 镜像内双接口和 WinUSB 描述符已校验，Windows 绑定与吞吐待测 |
| M3 | DMA/TIM/GPIO 资源管理与任务队列 | HOST_MOCK_PASS | 驱动已接入原子预留，I²C/SPI 作业使用分通道排队、超时、取消 |
| M4 | 双UART | HOST_MOCK_PASS | USART1/2 DMA 双路回环 Mock，0x82 双订阅携带偏移、时间和丢失计数 |
| M5 | 双I²C | HOST_MOCK_PASS | 双总线中断、组合事务、从机 FIFO 和显式总线恢复，实板弱上拉待测 |
| M6 | 双PWM | HOST_MOCK_PASS | TIM5/TIM3 各两通道共享组内频率，独立启停/占空比/极性、周期边界更新、限时和安全停机。TIM4 捕获脚本已用 Mock 验证，实板引出及波形待测 |
| M7 | SPI1 | HOST_MOCK_PASS | 四模式、可配置片选、短分段中断及长分段 DMA。排队超时结束 CS 事务、回收 DMA 并保留部分结果 |
| M8 | ADC1 | HOST_MOCK_PASS | 自动分辨率、扫描、VREFINT、有限/持续采集和 TIM3 同步。同步精度与实际吞吐待测 |
| M9 | Python SDK/CLI | HOST_MOCK_PASS | 同步/异步 API、客户端独占、共享 C 模拟器与 0.1.3 wheel。Linux 离线安装已验，Windows 运行待测 |
| M10 | PySide6 GUI | HOST_MOCK_PASS | 六页共享 SDK 工作台，后台线程与 ADC 曲线，Linux offscreen 冒烟通过 |
| M11 | MCP stdio Server | HOST_MOCK_PASS | stdio 工具、能力与状态资源、输出限额和权限检查。新增 PWM 通道选择与状态查询 |
| M12 | 集成、Windows 离线工作台与验证入口 | ARM_BUILD_PASS / HOST_MOCK_PASS | 0.1.3 Windows x64 候选已生成并静态校验。ARM USB 镜像检查和 SPI 排队超时回归通过。Windows 运行与 V1 发行待验收 |

状态标签：NOT_IMPLEMENTED、IN_PROGRESS、SOFTWARE_VERIFIED、WAITING_HARDWARE、HARDWARE_VERIFIED、BLOCKED、FAILED。必须以命令输出、提交SHA、原始日志支撑通过状态。
