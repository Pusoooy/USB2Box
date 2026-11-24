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

### UART（service 2）

首字节为端口 0 或 1。配置命令 1 追加 baud u32、parity u8（0/1/2 对应无/偶/奇）、stop u8（1/2）。命令 2 开启 RX。命令 3 追加读取上限 u16，返回当前可读字节。命令 4 追加待发送数据，返回排队字节数 u16。命令 5 停止并释放端口。命令 6 返回 RX 丢失字节、硬件错误、待发送字节，各 u32。未读数据超过环形缓冲时计入丢失。

### I²C（service 1）

首字节为总线 0 或 1。命令 1 追加速率 u32。命令 2 追加角色 u8（0=主机、1=从机）与 7-bit 从机地址。命令 3 追加任务 ID u32、超时毫秒 u32、7-bit 地址、消息数（1–8），再逐项附读写方向 u8（0=write、1=read）、长度 u8 及写数据。主机组合事务在各消息间生成 repeated START，最后 STOP。命令 4 设置从机发送缓存，命令 5 读取从机接收累计数 u32、错误数 u32 和最新接收字节，命令 6 释放总线。SYS_CANCEL=9、SYS_COLLECT=11 接收任务 ID u32。收集结果为操作状态 u16、已读字节数 u16 和已读数据。超时后的部分结果只包括已完成消息中的读字节。

### PWM（service 3）

首字节为频率组 0（TIM5）或 1（TIM3）。配置 1 和更新 3 追加频率 Hz u32、占空比 ppm u32、反相 u8，运行中更新返回 busy。启动 2 追加持续时间 ms u32（1–60,000）和脉冲数 u32（0 表示只用时限）。停止 4 释放资源，查询 5 返回实际频率 mHz、实际占空比 ppm、PSC、ARR（各 u32）以及运行标志 u8。有限脉冲使用比较中断，当前要求高、低两相各至少 10 µs，物理脉冲和输入捕获待 HIL。

### SPI（service 4）

首字节为控制器 0。配置 1 追加 Mode、字宽 8/16、LSB 优先、CS 编号 0–3、有效高（各 u8），再加时钟 Hz u32、CS 建立与保持时间 µs（各 u16，向上取整为毫秒，0 无延时），返回实际时钟 Hz u32。片选候选为 PB12、PC0、PC1、PC2。BEGIN=2，SEGMENT=3 追加任务 ID 和超时（各 u32）以及 1–256 B 同步收发数据。主机用 Dummy 字节实现读操作，可忽略返回实现纯写。END=4 释放片选，ABORT=5 停止并释放资源，COLLECT=7、CANCEL=8 追加任务 ID u32。片选在正常分段之间持续有效。

### ADC（service 5）

配置 1 接收分辨率 u8（0 默认 12，或 6/8/10/12）、序列长度 u8、各通道和采样时间代码（各 u8）。读一次 2 启动软件触发，要求单通道配置。启动 3 接收合计速率 Hz u32、有限样本数、预触发数、后触发数（各 u16）。有限与预后模式互斥，零计数代表持续采集，预触发由命令 5 软件触发。停止 4 释放资源。读取 6 携带最多读取样本数 u16。命令 7 增加 0x82 的块级 credit（u16，最多积累 16）；流帧 type=4、stream=1，seq 自然回绕。控制读取和流共同消费缓冲。

负载为实际合计速率、首样本偏移、丢失数（各 u32），样本数 u16、分辨率 u8 和标志 u8（bit0运行、bit1等待触发），最后为 uint16 样本。最大块 248 样本。满缓冲停止，读取可见丢样。实板的触发延迟、连续吞吐和精度待测。

### 诊断与附加测量

- SYS 设备信息 4 返回 ASCII 产品名。DIAG 服务 6 操作 1 返回请求、坏头部、错误响应、租约到期和总线重置计数，各 u32。
- 帧级错误有 12 B 上下文。阶段、冲突资源 ID（0xff 未知）、可重试标记、保留（各 u8），再附设备时间与已完成字节数（各 u32）。异步任务结果使用各自已有的部分读写契约。
- PWM 输入捕获操作 6 配置 TIM4 的目标计数频率 Hz u32。操作 7 返回周期、高电平宽度和实际计数 Hz（各 u32），操作 8 释放输入。PB6 AF2 与 USART1 TX 冲突，因此以资源管理器拒绝同时使用。
- ADC 块标记 bit2 表示缓冲还有数据，bit3 表示模拟看门狗越限。命令 8 在空闲时设置扫描序列内的看门狗通道 u8 与低、高阈值 u16。越限可触发预后采集，当前触发位置以服务收到 DMA 回调后的块边界为准。0x82 有最后一块标记以便采集结束时回收 credit。

## Version 1.0 compatible extensions

SYS operation 12 has an empty input and returns a u32 mask. Bit 0 indicates tagged bulk streaming, bit 1 indicates I²C slave FIFO records, and bit 2 indicates live PWM updates. The 36-byte header and the existing opcode payloads remain compatible.

- UART operations 7 and 8 accept a port byte and, respectively, a u32 tag or u16 added credit. Tag zero unsubscribes. Each subscribed port owns the receive consumer and command 3 returns busy. Frames have type 4, service 2, opcode 8, IDs 2 and 3, and a payload of tag, first-byte offset, device time in ms at buffer readout, cumulative lost bytes and errors (all u32), followed by at most 492 data bytes.
- Tagged ADC credit operation 9 takes a nonzero tag u32 and credit u16. A new tag resets old credit. Stream ID 1 responds with service 5 and opcode 9 and places the tag before the original ADC block. Legacy opcode 7 remains available.
- Each stream gets its own wrapping u16 sequence. Firmware holds at most 16 credits per stream and schedules ready sources round-robin. The SDK grants one block at a time and keeps an outstanding credit through a read timeout.
- I²C operation 7 pops the oldest event for the specified bus. Layout is sequence, device time in ms, dropped events, bytes received and errors (u32), kind and count (u8), then RX bytes when kind=1. Kind 0 means empty, 2 means TX (bytes loaded into the controller), and 3 means error. The eight-record FIFO drops new events when full and retains the drop count. Legacy operation 5 pops an RX event using the original summary layout and skips other event kinds.
- PWM operation 3 accepts a live frequency/duty change with unchanged polarity and updates at the next timer rollover. The final status byte reports running in bit 0 and pending in bit 1. Operation 3 acknowledges the quantized new settings, while operation 5 reports the applied plan. The original stop deadline and remaining pulse count still apply.

### Timing, recovery and log extensions

- SYS 12 bit 3 adds timed ADC streaming. ADC operation 10 accepts the same tag and credit as operation 9 and returns opcode 10 frames. Operation 11 reads a timed block with a limit of up to 219 samples for any supported scan. The prefix is tag u32 (zero on control reads), first-sample device time in µs u64, scan trigger interval in ns u32, rank count u8, first rank u8, then one channel u8 plus scan-relative sampling offset u32 ns per rank. The existing 16-byte ADC block and samples follow. Time is estimated from the start tick, programmed timer and ADC clocks. Cross-rank sampling remains sequential. OP12 returns the factory 12-bit VREFINT code and its calibration reference in mV (u16 each), with synthetic values in Mock.
- ADC resolution 0 selects the highest supported resolution fitting the complete scan within the requested trigger period. The actual resolution is reported in each block. Explicit resolution is either applied or rejected, and the algorithm keeps configured sample times. Watchdog thresholds in automatic mode use a 12-bit configuration scale and are reduced to the effective resolution.
- SYS 12 bit 4 adds I²C operation 8. Input is bus u8, job ID u32 and timeout ms u32. An idle master can release the open-drain lines, apply up to nine clocks and generate STOP once SDA is released. SCL must actually rise before moving on. The existing collect/cancel operations carry two result bytes (clock count and instantaneous SCL/SDA high bits), including partial data on timeout. GPIOs return to I²C mode when the operation ends.
- SYS 12 bit 5 adds DIAG operation 2 for an active session. Input is minimum level u8 (0–2), limit u8 (1–16). Output starts with overwritten count u32 and record count u8 plus three zero bytes. Records are device time u32, request ID u32, status u16, level u8, event kind u8, service u8, opcode u8 and reserved u16. Kinds 1–6 record command errors, lease expiry, reset, suspend, open and stop. Logs contain no command payloads.
- A stopped or failed SPI segment reports the RX DMA prefix already stored before abort. The SDK joins this prefix with earlier completed segments in the same transfer. These bytes describe RX progress, so external peripherals may already have observed more bus activity.
