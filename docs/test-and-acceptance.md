# 测试矩阵、证据规则与验收

## 证据等级（禁止混淆）

| 状态 | 可证明什么 | 不可证明什么 |
|---|---|---|
| `NOT_IMPLEMENTED` | 需求登记完成 | 不能声称有代码 |
| `SOURCE_PRESENT_UNTESTED` | 存在实现文件 | 不能声称正确 |
| `ARM_BUILD_PASS` | 目标编译链接通过 | 不证明外设初始化或USB枚举 |
| `HOST_MOCK_PASS` | Host协议、业务状态机通过模拟测试 | 不证明真实引脚/时钟/DMA/电气性能 |
| `WAITING_HARDWARE` | 测试方案已备好 | 不能声称真实硬件通过 |
| `HIL_PASS` | 有实板、工具、接线、日志、固件hash的可核验证据 | 不等于所有板/温度/线缆工业稳定性 |
| `FAILED` | 某条测试明确失败 | 记录失败原因、复现命令、修复方案 |

测试记录最少字段：`test_id, requirement_id, code_revision, firmware_sha256, tool_versions, environment, run_command, hardware_board_rev_or_mock, wiring, timestamp, raw_log_path, exit_code, measured_metrics, verdict, reviewer`。没有真实日志就必须标记NOT_RUN。

## 最小测试套件（Host软件阶段）

| 编号 | 范围 | 必须覆盖 |
|---|---|---|
| T-USB-001 | USB帧解析 | magic、版本、分片、多包粘连、长度溢出、非法offset、请求ID回绕 |
| T-USB-002 | 会话与命令 | 心跳、排队、公平性、超时、取消、断线/恢复、安全停止 |
| T-RES-001 | 资源锁 | GPIO/TIM/DMA重叠、合法DMA映射、不当抢占、申请失败回滚 |
| T-I2C-001 | 模拟I²C | 双路Master/Slave、Repeated START、超时、Slave事件、弱上拉不作承诺 |
| T-UART-001 | 模拟UART | 双路同时全双工、环形缓冲跨界、错误/溢出、随机字节流 |
| T-PWM-001 | 计算与调度 | 频率与ARR/PSC计算、ppm量化、双独立TIM、有限脉冲、停止安全态 |
| T-SPI-001 | 模拟SPI | Mode、8/16bit、多段CS不翻转、大分块、取消/超时、部分完成 |
| T-ADC-001 | 模拟ADC | 采样序列、DMA半区事件、预/后触发、数据元信息、溢出停止 |
| T-PC-001 | Python SDK/CLI | 请求应答、异步事件、异常类型、保存导出、设备发现Mock |
| T-GUI-001 | Windows GUI | Mock设备各页基本功能、后台收流和线程安全、异常显示 |
| T-MCP-001 | MCP stdio | 工具schema、Mock调用、只读/有副作用权限、大小限制、紧急停止 |
| T-SEC-001 | 异常/模糊 | 无效帧、过大payload、整数溢出、堆栈溢出监测、拒绝未授权写操作 |

Host可使用原生编译Sanitizer或fuzz harness作为推荐增强，但不能仅因没有某个平台编译器就谎称完成测试。静态分析检查及有问题的第三方警告必须独立记录。

## HIL实板测试顺序（板型已由用户确认；待具体电气核验后由Agent执行）

1. **H00板级电气核验**：板型与参考图一致已由用户确认；待核验正反实拍/原理图、PCB修订、HSE、USB 线路/供电、BOOT0/NRST/SWD、GPIO引出；更新最终pinmap并版本化。
2. **H01安全上电与SWD**：ST-Link探测ID、下载最小固件、复位、时钟寄存器/频率验证；输出GPIO默认安全态。
3. **H02 USB设备**：Windows10/11设备管理器、WinUSB绑定、端点描述符、Bulk echo、断连/重连、无效帧、会话崩溃/租约终止。
4. **H03双UART**：两条杜邦线交叉：USART1 TX→USART2 RX、USART2 TX→USART1 RX，3.3V共地；双向随机数据和多速率测试；测试DMA缓冲溢出与恢复。
5. **H04双I²C**：两路SCL互连，两路SDA互连，使用MCU内部弱上拉**仅低速短线尝试**；记录SCL/SDA边沿和成功率，如电气条件不足标`ELECTRICAL_BLOCKED`，不伪造通过。目标设备本身若有上拉，属于另一个额外场景，不能违反裸板政策。
6. **H05双PWM**：TIM5 PA0与TIM3 PC6分别通过跳线接到可用输入捕获GPIO，测量不同频率/占空比；必须检查输入捕获TIM、GPIO引出与当前映射是否冲突。断USB与租约超时必须回安全电平。
7. **H06 SPI**：SPI1 MOSI→MISO短跳线，Master连续CS分段及不同模式读写；若可引出第二SPI再做扩展Master/Slave测试。
8. **H07 ADC**：输入ADC脚分别接GND及3.3V轨，测量单点、扫描、内部参考及基本误差；对PWM直接输入只能当数字开关信号，不能当真实模拟DAC正弦波。
9. **H08性能与可靠性**：ADC100kSPS合计持续目标、USB Bulk吞吐/时延、SPI分段吞吐、双UART压力、各接口异步命令公平性、USB连续插拔、MCP/GUI操作无泄漏。
10. **H09跨进程与MCP**：Windows GUI/CLI/MCP会话仲裁、MCP自动化场景、异常关闭进程、心跳安全终止。

## 具体门槛与失效处理

- 严格门槛：双UART双向正确收发、双PWM两组不同频率持续正常运行、双I²C两个独立外设驱动完整实现；可行实板测试须提供证据。不允许把内部弱上拉的电气不达标掩盖为固件缺陷或默认通过。
- USB连续ADC设计目标为**100kSPS合计**，每样本uint16约200kB/s不包括协议开销。若实际无法达到，必须记录原因及数据并提交ADR调整，不能写成已经满足。
- 频率/电压/边沿/串口误码等精度门槛不可凭空指定：M0先为每项定义仪器需求和可测方法，取得实板实测后确定合理阈值。
- USB物理拔线、Windows进程崩溃、Host超时后PWM必须安全停止；要求明确的测试脚本和实测证据。
- 禁止虚构“已成功编译”“已下载固件”“已连接Windows设备”“实测USB吞吐”等记录；未知硬件阻塞项列入handoff。

## 基本回归命令约定（开发时创建真实脚本）

`make verify`：环境检查+ARM交叉构建+Host单测+协议回归+静态检查+生成报告。
`make offline-verify`：不进行网络操作，并验证本地所有依赖与校验值。
`make hil-verify`：显式依赖真实硬件；无硬件时退出非零或标为`NOT_RUN`，绝不自动转为Mock成功。

当前 Makefile 提供这些目标。`make verify-all` 增加 Qt offscreen 和 MCP stdio 检查，实板夹具和参数见 [HIL 指引](hil-fixtures.md)。
