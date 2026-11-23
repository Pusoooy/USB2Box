# Implementation Status / 实现状态

基线：REQ-BASE-2026-11-23-01；文档交接阶段。**尚无固件、Python SDK、GUI、MCP源码或真实测试结果。**

| 阶段 | 模块 | 状态 | 备注 |
|---|---|---|---|
| M0 | 需求/资源审查初稿 | DOC_DRAFT | 用户已确认板外观；PCB 电气、HSE、引脚输出待验证 |
| M1 | ARM Makefile + C11 启动与初始化 | NOT_IMPLEMENTED | 全部手写，不用 CubeMX |
| M2 | TinyUSB / Vendor Bulk / WinUSB | NOT_IMPLEMENTED | 端点协议见 usb-protocol |
| M3 | DMA/TIM/GPIO 资源管理 | NOT_IMPLEMENTED | 合法固定映射及原子申请 |
| M4 | UART x2 | NOT_IMPLEMENTED | USART1/2 优先 |
| M5 | I2C x2 | NOT_IMPLEMENTED | 仅片内弱上拉，电气可靠性不可保证 |
| M6 | PWM x2 | NOT_IMPLEMENTED | TIM5/TIM3 候选 |
| M7 | SPI1 | NOT_IMPLEMENTED | SPI2 等条件扩展 |
| M8 | ADC1 | NOT_IMPLEMENTED | 有界缓冲，连续 100kSPS 为实测目标 |
| M9 | Python SDK / CLI | NOT_IMPLEMENTED | Windows 10/11 |
| M10 | Windows GUI | NOT_IMPLEMENTED | PySide6 计划 |
| M11 | MCP stdio Server | NOT_IMPLEMENTED | Agent 安全与资源访问控制 |
| M12 | 集成/离线包/V0 发布 | NOT_IMPLEMENTED | 完整工具链依法打包、SHA256 |

状态标签：NOT_IMPLEMENTED、IN_PROGRESS、SOFTWARE_VERIFIED、WAITING_HARDWARE、HARDWARE_VERIFIED、BLOCKED、FAILED。必须以命令输出、提交SHA、原始日志支撑通过状态。