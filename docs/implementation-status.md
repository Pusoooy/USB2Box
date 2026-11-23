# Implementation Status / 实现状态

基线：REQ-BASE-2026-11-23-01；M0 软件切片已验证，官方 PDF 复查待完成。

| 阶段 | 模块 | 状态 | 备注 |
|---|---|---|---|
| M0 | 基线、候选资源、帧头 | HOST_MOCK_PASS（单测） | ST 封装/AF 已校验。DMA 候选静态检查通过，待对照 RM0368 原表 |
| M1 | ARM Makefile + C11 启动与初始化 | ARM_BUILD_PASS（M0 底座） | 复位镜像已链接。HAL/CMSIS、板级时钟和 USB 初始化待实现 |
| M2 | TinyUSB / Vendor Bulk / WinUSB | IN_PROGRESS | 帧头单测通过。完整 USB 栈、字节流解析及实板枚举待实现 |
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