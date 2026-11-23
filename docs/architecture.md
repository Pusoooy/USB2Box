# 固件系统架构与资源预算

## 分层与数据路径

```text
Windows GUI (PySide6)  ─┐
Python SDK + CLI        ─┼─> device-session/client manager -> libusb/WinUSB -> Bulk endpoints
MCP Server (stdio)      ─┘
                                          │
STM32 USB DCD/TinyUSB -> 帧解析/流解复用 -> Request ID会话/超时/取消
                                          │
                                 资源管理器 Reserve/Commit
                                          │
                 I²C service | UART service | PWM service | SPI service | ADC service
                                          │
                             STM32 HAL/LL + DMA/IRQ + BSP
```

- USB栈优先TinyUSB（审查所选版本能否支撑STM32F401 OTG FS DCD），阻碍必须记录ADR再替换为ST USB Device。
- 裸机事件循环：ISR只做寄存器清标志、搬移最少状态与投递有界事件；主循环执行`usb_task()`、request_dispatch、peripheral_poll、scheduler_poll、watchdog/lease_tick。不允许USB回调中阻塞等待I²C/SPI完成。
- 时间基准：独立32位单调计数，处理溢出扩展；硬件实际定时器选择在保留PWM和ADC触发后评估。PC时间戳不可当设备采样时间。
- MCU的DMA映射不支持完全动态路由；资源管理器只能在官方合法候选映射中选择，必要时按既定优先级降级或返回冲突。

## 资源管理事务与安全约束

- API `reserve(resources, owner_id)`、`validate(plan)`、`commit(plan)`、`release(lease)`、`query_capabilities()`、`query_conflicts()`、`stop_all_safe()`。
- 固定P0接口不能被可选功能静默抢占；强制重配置需用户明确停止旧接口后再申请。
- SPI CS多段事务保持锁直到结束（或超时/取消执行安全释放）；I²C组合消息必须在一次原子事务内处理START/STOP语义，不能因USB分包而插入意外STOP。
- 一项请求的“配置原子性”不意味着硬件动作可逆；对于已经发送的字节或完成的采样，返回`bytes_done/samples_done/partial=true`。
- 用户定义危险操作：高频PWM/长脉冲串/批量SPI写入/高负载ADC流；要求上位机API显式资源持有、会话有效、输出边界和紧急停止。

## 64 KiB SRAM 初始预算（设计预算，非链接实测）

| 预算项目 | 目标上限 |
|---|---:|
| ISR/main栈与安全余量 | 8 KiB |
| 静态数据、BSS、驱动状态 | 8 KiB |
| USB DCD/TinyUSB FIFO/描述符/业务队列 | 6 KiB |
| 双UART RX 2×4KiB + TX 2×2KiB | 12 KiB |
| ADC/SPI共享固定块内存池 | 20 KiB |
| 请求/事件/错误统计/日志/其余 | 4 KiB |
| 预留未分配空间 | 6 KiB |
| **合计** | **64 KiB** |

> 这是编写代码前的**预算目标**而非已验证的实际占用。HAL、USB DCD以及链接对齐可能改变结果。Codex必须在每次构建后根据ELF/MAP与实际最大栈水位调整预算。ADC大捕获与高缓存SPI的最大值可通过模式互斥使用共享池。**分段上传不等于可以在没有外部存储时无损缓存无限长度的高速连续采样**。

## 模块状态机与事件

`UNCONFIGURED -> CONFIGURED -> RUNNING -> STOPPING -> STOPPED`；失败进入 `ERROR -> RECOVERING -> STOPPED`。设备级状态包括 `NO_SESSION/SESSION_ACTIVE/LEASE_EXPIRED/USB_DISCONNECTED/SAFE_MODE`。事件通知需包含模块、通道、时间、原因、关联Request ID、已完成工作量。

## 断线与重连安全

- USB disconnect、reset或会话租约超时：停止所有PWM及其DMA触发链；终止ADC连续流；关闭或取消挂起的SPI/I²C事务，软件片选回到安全非选中态；UART接收停止/释放资源；异步清空会话队列。
- PC进程崩溃但USB仍插着：心跳超时必须触发同样安全动作；不允许只依赖物理断开事件。
- Release断言故障：记录固定容量的原因、停受影响输出；保留调试状态；不能陷入无限重试。全局崩溃只能作为最后方案并维护复位原因。

## 性能与降级原则

- UART RX DMA在保持连续接收时优先于SPI可选DMA；两个USART独立测试必须验证所有DMA分配。
- ADC高速有限捕获使用DMA；不足以持续发送时，有限捕获与上传分离，持续流以100kSPS总速率作为初始目标并依HIL调优。
- 软件/中断降级仅在可满足时序/正确性的模式开放；必须通过`capability`告知降级后的速度/限制，不能隐藏吞吐退化。
- 禁止在中断/USB回调里执行无上限日志、内存复制或等待操作。


---
