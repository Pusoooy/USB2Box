# ADR 0002 — M0 帧头与资源契约

状态：接受初版软件契约。硬件候选保留待验证状态。需求基线保持 REQ-BASE-2026-11-23-01。

- X07–X12：采用协议文档的 36 字节 little-endian 布局。v1 头部校验 magic、major、长度、消息类型和 reserved 字段。Request 与 Response 必须有非零 ID。每帧用 `offset <= total` 和 `length <= total - offset` 限制分片范围，并由调用者提供分片及逻辑长度上限。minor、服务、标志、命令及跨帧语义由后续会话和消息层协商或校验。
- F07、F11、F22、X16–X17：GPIO、TIM 主体与通道、DMA 控制器及 Stream、IRQ 和缓冲额度构成申请集合。DMA Channel 只选择请求，控制器和 Stream 才是互斥键。TIM 主体按频率组归属，同组通道共享周期。一次性校验并提交，下游配置失败释放整组。USB 和 SWD 引脚固定保留。
- STM32F401RC LQFP64 的 I2C2 改用 PB10 AF4 与 PB3 AF9。PB3 与 SWO 互斥。该修正只影响候选 BSP，不改变 API 或必须支持双 I²C 的要求。

命令号、标志位分配、流序号回绕、拼帧/组帧及会话运行时留待后续里程碑。
