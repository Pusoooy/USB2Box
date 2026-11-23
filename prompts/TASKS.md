# Codex 里程碑任务执行清单

以 `docs/roadmap.md` 为里程碑权威；具体实施按以下顺序：
- [ ] M0：资料核验、引脚与 DMA 冲突矩阵、需求完整性校验
- [ ] M1：ARM GCC + Makefile / linker / startup / CMSIS / HAL 及手写初始化
- [ ] M2：USB Vendor Bulk + WinUSB 描述符 + 36B 版本协议 + Mock
- [ ] M3：独立的 GPIO/AF/TIM/DMA/Buffer 资源管理、取消/超时
- [ ] M4：USART1/2 双路全双工和回环测试脚本
- [ ] M5：I2C1/2 Master/Slave 以及电气上拉风险标注
- [ ] M6：TIM5/TIM3 独立频率 PWM，输入捕获自测试
- [ ] M7：SPI1 Master、CS、分块 DMA
- [ ] M8：ADC 单通道高速、扫描、DMA、预/后触发、100kSPS目标
- [ ] M9：Python 同步/异步 SDK + CLI
- [ ] M10：Windows GUI
- [ ] M11：MCP stdio Server（安全命令、设备租约、资源锁）
- [ ] M12：跨模块回归、离线依赖与工具链、许可及校验、V0 软件验收

每一项完成时，提交源代码、可复核的实际执行命令及日志、更新测试矩阵/实现状态/交接文档。不得将未运行测试标注为通过。实板验证必须独立登记。