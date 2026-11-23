# USB Vendor Bulk 协议 v1（设计提案，待Codex实现与测试）

> 本文落实X01-X24，字段与错误码属于**需在M1冻结的拟定线协议**，并非已有固件事实。字节序均Little-endian；USB Bulk字节流不保证单次回调等于一帧。

## USB接口与枚举

- Interface: Vendor-specific; USB2.0 Full Speed; Endpoint0控制；候选 `OUT 0x01` 命令/上行数据、`IN 0x81` 回应/小事件、`IN 0x82` 高速流/大事件。64字节FS Bulk包；具体EP/FIFO可用性须测试。
- Windows10/11自动WinUSB绑定：BOS + Microsoft OS 2.0 descriptor + Compatible ID WINUSB + device interface GUID；TinyUSB样例仅作移植参考。VID/PID尚未分配，不得冒用其他厂商或发布带冲突标识的成品。
- USB iSerialNumber从STM32 96-bit UID派生；不将完整UID默认暴露到远程日志/网络。

## 36字节固定帧头（拟定）

| 字节偏移 | 类型 | 字段 | 语义 |
|---:|---|---|---|
| 0 | u32 | magic | `0x58533255`（字节序列 `55 32 53 58`，ASCII `U2SX`） |
| 4 | u8 | major | 主版本=1 |
| 5 | u8 | minor | 次版本=0（能力协商允许增长） |
| 6 | u8 | header_len | 固定36，保留未来扩展 |
| 7 | u8 | msg_type | 1=request 2=response 3=event 4=stream 5=credit |
| 8 | u16 | flags | FIRST/LAST/PARTIAL/ERROR/CANCELLED等 |
| 10 | u8 | service | SYS=0,I2C=1,UART=2,PWM=3,SPI=4,ADC=5,DIAG=6 |
| 11 | u8 | opcode | 服务内命令号，版本管理 |
| 12 | u16 | status | 分层状态码，0=OK |
| 14 | u16 | reserved | 发送端置0，接收端验证/忽略按版本规范 |
| 16 | u32 | request_id | 请求关联；0保留给无关联的异步通知 |
| 20 | u32 | fragment_length | 当前帧Payload字节长度 |
| 24 | u32 | total_length | 单条逻辑消息完整Payload长度或流窗口长度 |
| 28 | u32 | fragment_offset | 本分片相对逻辑消息偏移 |
| 32 | u16 | stream_id | 0控制；非0表示UART/ADC/SPI流句柄 |
| 34 | u16 | fragment_seq | 流片序号，回绕须有策略 |
| 36 | byte[] | Payload | 长度由fragment_length定义 |

协议解析必须按偏移读取字节，不准把未对齐USB buffer直接转为C结构体。所有长度加法使用溢出检查，拒绝未知的`magic/major/header_len`，禁止超大total_length直接分配。无malloc/free；以固定块池增量解析；采用分片窗口和Credit/Stop策略处理长SPI和ADC流。

## 消息类型与流控

- Response在`0x81`按Request ID匹配；诊断/状态小事件同在`0x81`，事件用type=event，request_id=0或相关请求ID；大数据块在`0x82`，以stream_id+fragment_seq+sample_offset关联。
- 固件控制命令队列有上限，额满返回`QUEUE_FULL`。允许同时执行不冲突外设任务，冲突资源上的事务串行化；并发跨多个IN端点时SDK不能假设跨端点消息天然有序。
- ADC源快于USB消费：优先停止采样并上报溢出与`sample_count/dropped_count`（A18），不允许默默丢弃；UART RX缓冲满报丢失字节计数。
- 分片逻辑：`total_length`与每个`offset+length`合法范围、FIRST/LAST标志、重传/取消策略需测试；不依赖USB包边界。对超出最大逻辑事务的业务采取流式块处理，不整体缓存。
- 可选应用层CRC在协议Minor扩展；默认依赖USB链路CRC和应用层长度/帧头校验，不推断其能覆盖所有逻辑错误。

## 服务命令最小表（命令号在M1冻结）

| Service | P0/必需命令概念 | 其他 |
|---|---|---|
| SYS | HELLO, GET_CAPABILITIES, GET_VERSION, GET_DEVICE_INFO, OPEN_SESSION, HEARTBEAT, CLOSE_SESSION, QUERY_RESOURCES, CANCEL_REQUEST, STOP_ALL | report errors, negotiated max payload |
| I2C | CONFIG, SET_ROLE, TRANSFER_MESSAGES, SLAVE_BUFFER_SETUP, SLAVE_EVENT_READ, RESET | 7位/10位、超时、bus recovery |
| UART | CONFIG, START_RX, READ_BUFFER, WRITE_ASYNC, STOP, STATS | STREAM_EVENT数据 |
| PWM | CONFIG, START, UPDATE, STOP, GET_ACTUAL, PULSE_COUNT | 2+独立TIM |
| SPI | CONFIG, BEGIN_TRANSACTION, TRANSFER_SEGMENT, END_TRANSACTION, ABORT, SET_CS | 多段连续CS |
| ADC | CONFIG_SCAN, READ_ONCE, START_CAPTURE, STOP_CAPTURE, SET_TRIGGER, READ_CAPTURE, STREAM_START, STREAM_STOP, STATS | 预触发、阈值事件 |
| DIAG | GET_LOG, GET_COUNTERS, GET_FAULTS, GET_MEMORY_USAGE | 版本和日志限额 |

## 错误码分层建议

`0x0xxx`协议/版本/格式；`0x1xxx`参数/引脚/能力；`0x2xxx`资源冲突/锁；`0x3xxx`阶段超时；`0x4xxx`I²C/UART/SPI/ADC/TIM具体外设失败；`0x5xxx`内存/队列/USB背压；`0x6xxx`取消/部分完成；`0x7xxx`会话过期/安全停止。失败Payload附`stage`, `resource_id`, `bytes_completed`, `retryable`, `device_monotonic_time`, `detail_code`。

## 超时、取消、会话

- 3个独立超时：PC等待USB请求的deadline、MCU排队deadline、每类外设硬件操作deadline。PC API超时本身不会自动代表MCU停止，必要时主动发CANCEL。
- `CANCEL(request_id)`针对排队请求立即生效；进行中在可安全中断边界完成，返回部分完成数量；无法立即安全打断时回报pending。
- `OPEN_SESSION`分配租约ID；按固定合理频率HEARTBEAT。USB断开或租约超时调用安全停止。单PC客户端独占硬件会话；多个GUI/CLI/MCP进程通过同一个Host设备代理或显式互斥访问，禁止多进程无仲裁直连。
- 对外API必须把链路不可用、设备忙、命令被取消、硬件错误、资源冲突区分为不同异常。

## 必备协议测试

- 随机切割/合并USB包、Magic误识别、非法header_len、长度溢出、同Request ID冲突、请求ID回绕、同端点乱序、跨IN端点乱序、丢块、重复片、断线、取消时传输进行中。
- 并发：UART RX不停，ADC流负载下执行I²C命令和PWM控制；检查命令公平性及队列占用。
- Windows WinUSB实际自动绑定只在HIL验证后允许标记通过。


---

## M2 实现契约

- Minor 0 的请求使用 FIRST=0x0001、LAST=0x0002。ERROR=0x0008 用于错误响应。一个逻辑请求可包含最多 1,024 B，每片最多 512 B，响应最多 512 B。同一时刻仅组装一个请求，要求严格递增的 offset 和序号，首片两者均为零。
- `HELLO=1`、`GET_CAPABILITIES=2` 返回分片上限 u16、消息上限 u16、租约毫秒 u32。`GET_VERSION=3` 返回 major/minor 两字节。`OPEN_SESSION=5` 返回租约 ID u32，`HEARTBEAT=6` 和 `CLOSE_SESSION=7` 携带该 ID。`STOP_ALL=10` 无载荷，结束当前会话。租约为 2 s，部分消息在 500 ms 无进展后清除。离开会话时调用同一个停止回调。
- 接口 0 承载 0x01/0x81，接口 1 保留独立 IN 0x82。TinyUSB DWC2 FS 以 320 字 FIFO 为上限，64 B 端点在当前配置下分配 RX 56 字、EP0/1/2 各 16 字。BOS/MS OS 2.0 提供两个 WinUSB 接口子集。实际 FIFO 行为及 Windows 绑定待 HIL。
- 默认 `USB_VID=0 USB_PID=0` 链接完整 USB 路径，设备保持软断开。启用设备需同时提供合法 VID/PID 和经实板确认的 HSE 配置。

### UART（service 3）

首字节为端口 0 或 1。配置命令 1 追加 baud u32、parity u8（0/1/2 对应无/偶/奇）、stop u8（1/2）。命令 2 开启 RX。命令 3 追加读取上限 u16，返回当前可读字节。命令 4 追加待发送数据，返回排队字节数 u16。命令 5 停止并释放端口。命令 6 返回 RX 丢失字节、硬件错误、待发送字节，各 u32。未读数据超过环形缓冲时计入丢失。
