# Codex 开发里程碑 / 可交付任务分解

## 开发顺序与分工

优先依赖顺序：**文档/硬件核验→工程底座→USB协议→资源管理器→双UART→双I²C→双PWM→SPI→ADC→Python SDK→Windows GUI→MCP→集成/交接**。部分模块设计与Mock可并行，但每个接口契约先冻结。GUI与MCP使用同一个SDK。每个里程碑必须包含代码、Host Mock、ARM交叉编译、报告、handoff更新；除非某项纯文档不涉及代码。

| 阶段 | Agent主要任务 | 软件阶段验收 / Gate | 硬件依赖 |
|---|---|---|---|
| M0 基线/候选资源 | 整理150决策、需求ID、已确认的参考图板型与板级电气证据清单、AF/DMA候选矩阵，确认合法候选与风险；建立ADR/AGENTS | 文档一致性、pinmap/资源表静态校验 | PCB准确修订、引脚走线与晶振待核验 |
| M1 ARM工程底座 | Makefile C11 ARM GCC，固定CMSIS/HAL/Startup/Linker，手写RCC/GPIO/NVIC/USB初始化，Debug/Release/MAP，静态RAM预算 | ARM目标能编译+链接（若工具链可用），Host构建与mock正常 | HSE/USB板级待验证 |
| M2 USB Vendor协议 | TinyUSB DCD移植或经ADR改ST栈，WinUSB MS OS描述符、36B帧/分片/Request ID、独立端点/事件、会话租约 | 帧解析fuzz和Mock Bulk端到端，通过自动脚本 | 真正WinUSB枚举待实板 |
| M3 资源/异步框架 | pinmux、TIM/DMA合法候选映射、有界队列、取消/超时、原子预留/提交、错误/日志 | 资源冲突、部分完成、抢占拒绝和并发Host测试 | 实际DMA冲突需实板验证 |
| M4 双UART | USART1/2，全双工DMA RX环形、异步TX、错误统计、双交叉回环脚本 | 2路全双工模拟压力+请求不互阻 | TX/RX跳线实测 |
| M5 双I²C | I2C1/2 Master/Slave、多消息重复起始、独立超时/从机事件、弱上拉风险模型 | 双路主从模拟+异常恢复+错误覆盖 | 电气上拉无法保证 |
| M6 双PWM | TIM5/TIM3双频率组、定时器计算/ppm、有限脉冲数、断线/心跳停止、输入捕获脚本 | PWM计算/并发/停机Mock测试 | 计频/示波验证 |
| M7 SPI | SPI1 Master全部模式、多CS分段/分块DMA、超时和回环；扩展SPI2候选 | 长事务/CS保持/分片Host测试 | SPI回环实测 |
| M8 ADC | 单ADC扫描、自动合法分辨率、DMA捕获/预后触发、100kSPS持续目标、溢出停止、内部测量 | DMA half/full模拟、无丢样统计、有限缓存断言 | ADC采样率/精度HIL |
| M9 Windows SDK+CLI | Python同步异步API、异常类型、安装包、设备发现、NumPy导出、MockTransport | SDK/CLI Mock全功能测试 | WinUSB实际枚举待HIL |
| M10 Windows GUI | PySide6五功能页、曲线/诊断、Mock标识、导出、设备保护逻辑 | 无设备GUI冒烟/模拟驱动测试 | 实物交互HIL |
| M11 MCP | stdio MCP tools/resources、权限/设备租约、Mock和真实SDK适配、离线可运行脚本 | MCP Inspector/Client Mock交互、输出限额、安全门禁 | 实板MCP HIL待做 |
| M12 集成与发布 | 全量回归、跨服务并发、合法离线打包、SHA256、licenses、文档冻结、handoff | V0的软件验收表，必须列所有`WAITING_HARDWARE` | V1实板验收阶段 |

## 任务工作方式（所有Agent必须遵守）

1. 开始前先读`AGENTS.md`、`docs/handoff.md`、`docs/requirements.md`和相关ADR；不得依赖聊天中未记录的假设。
2. 从用户确认的需求中抽取唯一Requirement ID并关联实现、单元测试、验收脚本。
3. 每次只完成一个可证明的垂直切片；所有任务完成后运行实际存在的Make目标并保存可复核日志。若工具链缺失，要记录不可执行原因，不能伪造命令输出。
4. 每个里程碑必须更新`docs/implementation-status.md`、`docs/test-report.md`、`docs/handoff.md`，记录代码哈希/版本、下一步和阻塞项。
5. 原芯片/电气无法满足的需求标为`CONDITIONAL`或`ELECTRICAL_BLOCKED`，提出证据和非侵入式替代方案，不得自行接入新元器件。
6. 真实硬件验证必须收到照片/PCB信息或由Agent实际读到真实芯片并保存测试证据，不得把模拟器当实板。

## V0→V1 条件

V0可以软件验收，不得打v1.0；V1需要真实板、合法USB标识、WinUSB自动绑定、核心5功能与双路目标验证、GUI/MCP实际设备访问、全量异常恢复和性能证据。不能要求用户手工写任何一行固件或脚本。


---
