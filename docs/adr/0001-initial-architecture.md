# ADR-0001：初始软件架构和硬件约束

状态：ACCEPTED（需求基线确定，尚未代码验证）
日期：2026-11-23

## 背景
用户要求一块不改造的 STM32F401RCT6 裸最小系统板实现 USB2IIC/UART/PWM/SPI/ADC；三类接口各能独立双路，允许短跳线；Windows10/11 GUI、Python SDK、MCP Server，云端 Codex 全权开发并交接。

## 决策
- Bare-metal C11，HAL 主导，关键路径按需 LL；无 RTOS、无 malloc/free。
- Makefile + arm-none-eabi-gcc；手写启动和 RCC/GPIO/NVIC/外设初始化，不使用 CubeMX。
- TinyUSB 优先，必要时以 ADR 分析 ST Device 替代。
- Vendor Bulk + WinUSB；协议版本、32bit 请求ID、有界分片、异步事件、明确取消与错误码。
- GPIO/TIM/DMA 资源管理器：只能申请合法硬件固定映射，不得静默抢占。
- 64KiB SRAM 静态预算；基础缓冲固定、ADC/SPI 共享内存池。
- Windows SDK、CLI、GUI 和 MCP 共享单一受控设备访问层。
- AGENTS.md + 唯一需求基线 + 测试追踪 + handoff 用于跨 Agent 接手。

## 权衡和约束
- I2C 内部弱上拉不保证标准速率可靠；不足视为电气阻塞。
- USB FS 吞吐限制不可视为 480Mbps HS。
- ADC 仅一实例，时间复用扫描；高速模式并非 USB 连续无损输出能力。
- 开发板外观已由用户确认，PCB 走线、HSE 和实际 USB 电气尚待实测。
- V0 为软件验收版本；V1 需实际硬件证据。

后续重大改变必须新增 ADR 并征得用户对需求变更的明确确认。