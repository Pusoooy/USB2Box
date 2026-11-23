# STM32F401RCT6 USB2XXX 综合开发规格书

> 此汇总文件供阅读，后续Agent应优先维护分拆后的源文件。


---

<!-- 章节来源：docs/board-identification.md -->

# 开发板识别状态 / Board Identification

- **状态：`BOARD_STYLE_CONFIRMED_BY_USER`（已确认）。**
- **确认时间：2026-11-23。**
- **确认依据：** 用户明确表示手中的开发板**就是本次对话中助手先前展示的 STM32F401RCT6 最小系统板参考图片**。
- **确认的设备类别：** 搭载 STM32F401RCT6 的黑色 PCB、USB Type-C 接口、Black Pill 风格最小系统板。这是**用户确认的外观/选型**，后续 Agent 不必再询问“是否是图片中的这种板子”。
- **证据性质：** 用户口头/文字确认参考图；**不是用户上传的实物正反照片，也不是厂商型号、PCB Revision 或原理图的认证**。

## 严格区分两个确认层级

| 对象 | 状态 | 后续工作要求 |
|---|---|---|
| 所采用的开发板类别与参考图是否一致 | **已确认（用户）** | 直接采用这款 Black Pill 风格 STM32F401RCT6 最小系统板作为目标 |
| MCU 芯片型号（项目选型） | **已确认（用户）** | 项目按 STM32F401RCT6 / LQFP64 设计；实板阶段可用 ST-Link 读取 ID 并核对丝印 |
| PCB 厂商、准确产品名、版本号、修订 | **待物理核验** | 不因外观相同即假定板级走线相同 |
| HSE 晶振频率、实际 PLL 与 USB 48 MHz | **待物理核验** | 不把常见 8/25 MHz 直接写死 |
| Type-C D+/D-、VBUS、CC 下拉和供电路径 | **待物理核验** | 不宣称 USB 枚举已经通过 |
| 引出排针/具体 GPIO、SWD、BOOT0、NRST、板载 LED 占用 | **待物理核验** | `schemas/pinmap_candidate.csv` 仍然是候选矩阵 |
| I²C 仅内部弱上拉下的物理通信可靠性 | **待实板测试** | 不把弱上拉低速实验当作有保证的电气性能 |

## 对 Codex 的要求

1. **不得将“开发板种类未确认”继续列为阻塞项，也不得重复询问用户是哪一类板。**
2. 未确认的仅是板级具体设计与电气事实；先按芯片官方引脚/AF、DMA 资料推进 Host Mock、Makefile 和固件模块。
3. 需要精确引脚布局、晶振等信息时，建立独立的 `BOARD_ELECTRICAL_UNVERIFIED` 待办；可请求用户后续提供实物照片或执行 ST-Link 测试，但不得把这些验证偷偷视为已完成。
4. 本次状态修改只更新设计/交接事实；**未新增固件代码，未进行 USB 枚举或 HIL 试验。**


---

<!-- 章节来源：docs/requirements.md -->

# USB2XXX 正式需求规格书 / Requirements Baseline

- 基线 ID：`REQ-BASE-2026-11-23-01`
- 状态：**需求已确认；设计候选待实板确认；没有固件/GUI/MCP实现或实板测试证据**。
- 目标芯片：STM32F401RCT6（LQFP64；256 KiB Flash，64 KiB SRAM）。**开发板选型/外观已由用户确认，与此前展示的黑色PCB、Type-C、Black Pill风格参考图一致**（详见`docs/board-identification.md`）；PCB准确版本、引脚引出、晶振与USB实际走线仍待实物/原理图核验。
- 源头：`decisions.md` 包含 7 组共 150 项逐条选择，任何子项目都必须能追溯到这些决定。

## 优先级与完成定义

| 级别 | 定义 | 必须实现的内容 |
|---|---|---|
| P0 硬约束 | 无法妥协，未达标不允许将功能认定为完成 | 只用现有裸系统板、不焊接新元件；五个USB2*功能；I²C、UART、PWM每类至少2个独立实例；Windows10/11；Vendor Bulk + WinUSB；不依赖CubeMX；Makefile+ARM GCC；所有代码Agent维护；没有实板不得虚报 |
| P1 V0 必需 | 无实板时的源码/模拟/单测目标 | USB协议、资源仲裁、双路逻辑、异步/超时/取消、安全状态、Windows SDK/CLI/GUI、MCP、模拟设备、测试脚本、资料/交接文档和离线构建流程 |
| P1 V1 实板 | V1.0发布前必须使用真实板完成 | SWD烧录、48MHz USB时钟、枚举WinUSB、各功能核心读写、双路独立运行、输出/输入电气行为、压力测试、断连/重连、安全停止、MCP端到端验证 |
| P2 条件增强 | 仅当具体封装/引脚/电气/DMA/内存允许 | I²C3、UART3、SPI2/3、更多PWM和ADC通道、PWM与ADC硬同步、SPI Slave、1MHz I²C、ADC极限突发速率 |
| P3 后续 | 不作为首版门槛 | 自定义DFU Bootloader、远程MCP服务、多主机远程共享、复杂协议脚本、工业级电气兼容 |

## 功能与必须支持的API

- **USB2IIC**：2路硬件控制器各自可Master/Slave；7bit地址、组合事务、Repeated START、多消息事务、独立超时/恢复、从机异步事件；第3路可行才开放。目标尽可能1MHz，但只能视为**受芯片、电气、实板条件约束的探索性上限**。本板只有内部弱上拉，无法承诺标准快速模式与高速可靠性；短跳线低速自测可失败且需报告明确原因。
- **USB2UART**：2路全双工独立同时接收/发送，DMA RX+环形缓冲、异步TX、波特率/奇偶/停止位、错误计数、完整性与随机数据压力测试；尽可能最高硬件波特率须实板实测；第3路可行才开放。仅3.3V TTL，不实现RS232/RS485物理层或默认COM口。默认每路 RX 4KiB + TX 2KiB，内存预算允许调整但需记录。
- **USB2PWM**：多路输出、至少2个独立频率组，同一TIM共享频率但各通道占空比/极性/启停独立；0..1000000 ppm配置、返回实际频率/精度；低频优先32位TIM；有限脉冲数输出、低电平安全停止、断线/租约超时停止；输入捕获跳线自测。
- **USB2SPI**：SPI1 Master必须支持，8/16bit、MSB/LSB、Mode0-3、Write/Read/Full-duplex/Dummy、连续CS分段事务、最多4软件片选、可配置CS极性和建立/保持延时、USB分块有界传输；SPI2及Slave可行时再增加；MOSI/MISO跳线回环。
- **USB2ADC**：单个ADC；优先单通道高速；PC可指定通道序列；速率不足时合理调整分辨率并报告；每通道采样时间可调；软件/定时器触发、DMA有限捕获、预后触发、扫描和尽力连续流；uint16原始值+块级采样元数据；默认长期稳定目标**合计100 kSPS**，高速突发采集不能等同于持续USB无损输出。可用内置VREFINT/温度/VBAT、Analog Watchdog；缓冲溢出停止并报告，不静默丢样。
- **Windows GUI**：必须交付非演示性的图形上位机，覆盖设备枚举、状态、I²C控制、UART终端、PWM控制、SPI事务、ADC绘图/触发/导出、资源冲突/错误诊断、测试入口。使用共享Python SDK，不重复独立实现USB协议。
- **MCP Server**：必须交付本地`stdio` MCP Server，使支持MCP的Agent能调用设备查询及五类功能；与GUI/CLI共用一个Python SDK及会话/权限控制；先支持模拟设备，实板接入后完成端到端验证。危险持续输出、批量事务默认需显式授权/许可，不暴露任意内存读写或不受限的原始GPIO驱动。

## 跨模块非功能约束

1. 固定基础接口 + 动态扩展引脚；资源互斥必须**在申请时原子检查**，冲突时提示资源ID与替代可能性；不得隐式关停正在运行的业务。
2. USB全速模式，PC只能通过Vendor Bulk访问全部外设。Windows WinUSB自动绑定计划使用Microsoft OS 2.0描述符，未得到合法VID/PID之前不得假定可正式分发。
3. 设备内按模块异步事件状态机执行；不同资源可并发，同一外设冲突操作排队；队列/缓冲必须有容量上限，能取消、超时、恢复并报告部分完成。
4. USB重置/断开/会话租约超时要停止PWM及持续数据任务并释放会话资源。USB物理断开不能覆盖“PC进程崩溃但线未拔”的情形，因此必须实现心跳租约。
5. 无RTOS，无固件`malloc/free`；使用C11、STM32 HAL为主必要时LL、TinyUSB优先；纯手写GPIO/时钟/USB/外设初始化，不生成CubeMX配置或依赖GUI IDE。
6. 编译和测试：arm-none-eabi-gcc、Makefile、离线固定依赖；Debug/Release；`-Wall -Wextra`，项目源码警告作为错误；Host模拟器、单测、异常注入、协议回归、性能/HIL脚本；测试结果必须有明确证据等级。
7. 全流程由Agent维护源码与文档，用户不需要手工编写代码。Agent遇到硬件验证阻塞时可继续软件工作，但**不得声称真实USB枚举、波形、吞吐或电气性能通过**。
8. 语义约定：“尽可能”表示优化目标，不能自动升级为硬性数量/性能要求；“独立”对I²C/UART/PWM意味着两个控制器/频率组的同时运行能力，**不**要求六路功能全时同时启动。

## V0 / V1发布验收

- **V0.x 软件发布**：所有P0接口与核心P1代码已交叉编译、Mock集成测试与协议回归通过；GUI和MCP模拟设备演示可运行；未测硬件明确列为`WAITING_HARDWARE`。依赖若未能合法离线打包则标记交付阻塞，不可称“完整离线包”。
- **V1.0 实板发布**：硬件引脚/时钟/电气及核心功能全部复核，双I²C/双UART/双PWM的物理验收完成（I²C弱上拉无法可靠验证时应如实记录风险并根据实际硬件条件决策，不能伪造通过），WinUSB枚举和各端点收发成功，GUI/MCP实板端到端通过，异常恢复和稳定性有实测报告。

## 需求变更机制

变更必须记录在ADR和`docs/requirements.md`，写明原需求ID、调整原因、影响模块、兼容性、测试更新、是否需要用户批准。可自主修复/优化，不允许Agent自行删除已确定的核心目标。


---

<!-- 章节来源：docs/decisions.md -->

# 决策登记簿（全部 7 组，已确认）

状态：**用户已确认**。符号 A/B/C/D 对应交互问卷选项，详细技术语义以本表为准。

重要新增项：**Windows GUI 上位机 + 供 Agent 调用的 MCP Server 均为正式交付目标**。它们未包含在旧问卷选项中，不得漏项。

## 01-基础约束

| ID | 选择 | 已确认含义 |
|---|:---:|---|
| B01 | A | 允许杜邦线连接本板GPIO进行硬件自测 |
| B02 | B | I²C仅使用MCU内部弱上拉；低速短线尝试，不承诺可靠性 |
| B03 | B | I²C/UART/PWM各自至少双路独立并发；六路不强制一起运行 |
| B04 | C | PWM尽可能覆盖芯片支持的全部频率并报告分辨率 |
| B05 | A | 核心引脚固定；扩展接口动态分配 |
| B06 | B | Windows 10/11 |

## 02-I²C

| ID | 选择 | 已确认含义 |
|---|:---:|---|
| I01 | A | I²C至少2路；第3路资源允许时开放 |
| I02 | A | 每路支持独立Master/Slave模式 |
| I03 | C | 尽可能支持1MHz，条件性目标；须满足电气与芯片要求 |
| I04 | A | 7位地址，预留10位支持 |
| I05 | A | Write/Read/Write-Read/Repeated START/组合事务 |
| I06 | A | Slave RX/TX缓冲；PC预置数据和读取访问事件 |
| I07 | A | 独立超时、错误码、控制器复位和可行的总线恢复 |
| I08 | A | 短事务中断或轮询，大事务按需DMA |
| I09 | A | 双I²C短跳线弱上拉低速自测，失败报告电气不满足 |
| I10 | A | 双控制器独立并行、独立错误恢复 |

## 02-UART

| ID | 选择 | 已确认含义 |
|---|:---:|---|
| U01 | A | UART至少2路；第3路可行时开放 |
| U02 | C | 尽可能支持硬件允许的最高波特率；实测决定上限 |
| U03 | A | 8N1/8E1/8O1及芯片支持的其他合法帧格式 |
| U04 | A | 双路全双工，DMA RX环形缓冲和异步TX |
| U05 | A | 标准3.3V TTL，不配置硬件流控 |
| U06 | A | 统一USB2XXX API，不要求虚拟COM端口 |
| U07 | A | 默认每路RX 4KiB + TX 2KiB；可根据RAM预算调整 |
| U08 | A | 异步数据流、可查询缓存及接收时间信息 |
| U09 | A | 上报帧错/溢出/丢失字节等统计 |
| U10 | A | 两路UART交叉TX/RX随机数据、多速率压力自测 |

## 03-PWM

| ID | 选择 | 已确认含义 |
|---|:---:|---|
| P01 | A | 尽可能开放PWM通道；至少2路独立输出 |
| P02 | A | 至少2组独立频率，其余有资源则扩展 |
| P03 | A | 同TIM共享频率，通道占空比/启停/极性独立 |
| P04 | A | 支持硬件最低频率，优先利用32位TIM |
| P05 | A | 占空比0..1,000,000 ppm，返回实际分辨率 |
| P06 | A | 优先频率准确并报告可达的占空比精度 |
| P07 | A | 边沿对齐优先，条件允许支持中心对齐 |
| P08 | A | 极性可配，停止后默认安全低电平 |
| P09 | A | 周期边界更新，尽量无毛刺 |
| P10 | A | 各通道独立启停，不强制同步 |
| P11 | A | 连续PWM与有限脉冲数输出 |
| P12 | A | USB断开时停止PWM并输出安全电平 |
| P13 | A | PWM跳线到输入捕获通道验证两独立频率 |
| P14 | A | 回报实际频率、占空比、TIM、通道、分辨率及状态 |

## 03-SPI

| ID | 选择 | 已确认含义 |
|---|:---:|---|
| S01 | A | SPI1必须支持；SPI2等资源可行时开放 |
| S02 | A | Master完整实现；Slave尽可能支持 |
| S03 | A | 双SPI并发条件允许时实现，否则明确冲突 |
| S04 | A | 支持硬件全部分频档，返回实际SCK |
| S05 | A | Mode0/1/2/3 |
| S06 | A | 8/16位、MSB/LSB |
| S07 | A | 每路最多4个软件CS并提供手动CS API |
| S08 | A | CS高低有效、建立/保持延时和事务内保持CS |
| S09 | A | Write/Read/Full-duplex/Dummy/多段连续CS事务 |
| S10 | A | USB分块+有界缓冲+DMA，逻辑事务可跨分块 |
| S11 | A | 大事务DMA、小事务中断轮询、异步接口 |
| S12 | A | MOSI-MISO回环；双SPI可用时补主从互测 |
| S13 | A | 超时/OVR/模式错误上报，CS安全释放、局部恢复 |
| S14 | A | 有限长高吞吐优先；流按USB FS带宽尽力而为 |

## 04-ADC

| ID | 选择 | 已确认含义 |
|---|:---:|---|
| A01 | D | 优先单通道最高采样性能；其他可用输入尽可能开放 |
| A02 | A | PC任选通道与扫描顺序 |
| A03 | D | 根据速率需求自动选择合法分辨率 |
| A04 | A | 每通道单独配置采样周期 |
| A05 | A | 单点/扫描/DMA捕获/连续流 |
| A06 | A | 软件触发和定时器触发 |
| A07 | A | 资源许可时PWM同步ADC采样 |
| A08 | A | 扫描序列并报告通道顺序和实际时间间隔 |
| A09 | A | 尽可能支持VREFINT、温度、VBAT |
| A10 | A | Analog Watchdog上下阈值及异步事件 |
| A11 | A | 原始码值；PC配置VREF并可按VREFINT估算 |
| A12 | A | GND/3.3V/VREFINT物理及内部通道测试 |
| A13 | A | 尽可能达到ADC条件允许的硬件极限，必须实测 |
| A14 | A | USB连续采样总速率稳定目标100 kSPS；更高需实测 |
| A15 | D | 尽可能长捕获，分段采集/上传，不能假定无限板载缓冲 |
| A16 | A | 预/后触发样本比例可配 |
| A17 | A | DMA循环双半区事件 + 独立USB发送队列 |
| A18 | A | 缓冲溢出停止、报告累计样本数/丢失统计 |
| A19 | A | uint16原始采样+通道与速率元信息 |
| A20 | A | 每块首样本设备时间戳/序号/采样周期 |
| A21 | A | 异步Bulk+有界队列+背压或安全停止 |
| A22 | A | Python async SDK + NumPy + CSV/二进制保存 |
| A23 | A | 与无冲突的I²C/UART/PWM/SPI并发 |
| A24 | A | 电压/扫描/DMA捕获/流压力/溢出恢复验收 |

## 05-USB协议

| ID | 选择 | 已确认含义 |
|---|:---:|---|
| X01 | A | Vendor-specific Bulk统一设备API |
| X02 | A | WinUSB MS OS描述符自动绑定，先验证枚举 |
| X03 | A | EP0+Bulk OUT命令+Bulk IN应答+独立Bulk IN数据流 |
| X04 | A | 合法VID/PID尚未分配；仅提供模板，不冒用 |
| X05 | A | 96-bit UID派生USB Serial Number |
| X06 | A | 断开清理会话、停PWM和数据流；重连可重新配置 |
| X07 | A | 定长头Magic/类型/命令ID/长度/Payload |
| X08 | A | 32位Request ID独立匹配 |
| X09 | A | Major/Minor协议版本+能力查询 |
| X10 | A | 总长度/分片序号/偏移，有界流式重组 |
| X11 | A | 依赖USB CRC+业务帧头/长度检查，额外CRC可选 |
| X12 | A | 异步事件与应答依赖类型/Request ID区分 |
| X13 | A | 不同外设并发，同外设冲突事务串行、有界队列 |
| X14 | A | USB/排队/外设分阶段超时 |
| X15 | A | 按Request ID取消，运行中尽力安全中断并报部分结果 |
| X16 | A | GPIO/TIM/DMA统一资源管理，原子预留释放及冲突报告 |
| X17 | A | 资源申请/配置原子，已发生外设通信不可回滚，报部分完成 |
| X18 | A | 会话租约/心跳超时，局部安全复位，支持重连 |
| X19 | A | 分层错误码及具体上下文 |
| X20 | A | 日志级别/计数/状态/有界诊断缓冲/异步事件 |
| X21 | A | 优先局部恢复，必要时重置会话，整机复位最后手段 |
| X22 | A | 实测Bulk吞吐、命令延迟、公平性、丢包、长期稳定 |
| X23 | A | Python同步/异步SDK、类型标注、异常、CLI、安装包 |
| X24 | A | 解析单测/模拟设备/异常模糊/实板回环/并发压测 |

## 06-固件

| ID | 选择 | 已确认含义 |
|---|:---:|---|
| F01 | A | HAL为主、关键路径LL |
| F02 | A | 优先TinyUSB，失败时评估ST USB Device |
| F03 | A | 无RTOS；裸机+DMA+中断+事件循环 |
| F04 | A | 非阻塞状态机+有界任务队列 |
| F05 | A | 统一NVIC优先级，ISR最小化 |
| F06 | A | BSP/HAL-LL/资源管理/外设服务/USB协议分层 |
| F07 | A | DMA官方映射表+仲裁，合适时可降级中断 |
| F08 | A | 编译期内存预算、栈余量、MAP审查 |
| F09 | A | 基础接口固定缓存，ADC/SPI共享有界池 |
| F10 | A | 优先少拷贝，所有权安全时零拷贝 |
| F11 | A | 系统时基保留，双TIM优先PWM，其余定时器动态申请 |
| F12 | A | 依据实板HSE配置84MHz/USB48MHz并验证时钟树 |
| F13 | D | Makefile + arm-none-eabi-gcc，无CMake强依赖 |
| F14 | C | 手写初始化，不依赖CubeMX |
| F15 | A | 固定验证过的arm-none-eabi-gcc与依赖版本、一键构建 |
| F16 | A | 已有ST-Link，SWD烧录调试 |
| F17 | A | 先SWD，预留ROM USB DFU |
| F18 | D | 完整可离线构建包（遵守分发许可） |
| F19 | A | C11、统一格式、-Wall -Wextra，项目代码警告视错误 |
| F20 | A | 固件禁止malloc/free，静态+定长块池 |
| F21 | A | Debug断言；Release局部安全关闭、诊断和恢复 |
| F22 | A | BSP独立板级时钟/引脚/AF/DMA，便于迁移 |
| F23 | A | Debug和Release独立配置 |
| F24 | A | ARM交叉编译+Host Mock+协议回归+静态检查+测试报告门禁 |

## 07-Agent与交付

| ID | 选择 | 已确认含义 |
|---|:---:|---|
| G01 | A | requirements.md需求ID为唯一基线并映射验收 |
| G02 | A | 必须维护AGENTS.md规则/编译/测试/更新义务 |
| G03 | A | 状态矩阵：未实现/开发中/软件验证/待实板/实板通过 |
| G04 | A | 里程碑和重要变更更新handoff.md |
| G05 | A | ADR记录关键决策；变更说明原因/影响/批准状态 |
| G06 | A | 优先推进Mock测试并记录阻塞与待实板验证 |
| G07 | A | 阶段：底座→USB协议→资源管理→五外设→SDK→集成 |
| G08 | A | 优先：USB→双UART→双I²C→双PWM→SPI→ADC |
| G09 | A | 代码+ARM编译+Host单测+协议回归+文档齐备才软件验收 |
| G10 | A | 先冻结USB协议/资源契约，独立任务可并行 |
| G11 | A | Agent自主实现/修复/优化；改变确认需求需明确提出 |
| G12 | A | 硬件阻塞不停止可离线工作，维护Mock/BSP占位/验证步骤 |
| G13 | A | 核心单测+Mock集成+回归+异常注入，按风险覆盖 |
| G14 | A | 预定义指标/脚本，无实板不承诺性能 |
| G15 | A | ARM编译/Host Mock/实板证据分离，记录命令环境日志 |
| G16 | A | 需求-代码-测试-结果可追踪矩阵 |
| G17 | A | 重要改动必须运行构建、单测与协议回归，失败不得标完成 |
| G18 | A | 未实现/未执行/待实板/失败如实标记，禁止伪造证据 |
| G19 | A | SemVer；v0.x为开发，实板验收完成再发布v1.0 |
| G20 | C | 便携工具链+第三方依赖全部离线提供，仅限许可允许 |
| G21 | A | 分阶段HIL脚本、接线表、预期输出和排错 |
| G22 | A | AGENTS+handoff+状态矩阵+脚本构成接手流程 |
| G23 | A | 双阶段验收V0软件/Mock，V1实板功能性能 |
| G24 | A | 保持协议向后兼容，ADR+回归+版本记录 |


---

<!-- 章节来源：docs/hardware.md -->

# STM32F401RCT6 硬件、引脚、DMA与并发资源审查

**状态：用户已确认开发板选型/外观与此前 STM32F401RCT6 黑色 PCB、Type-C 参考图一致；MCU官方资料可用于资源审查，但具体 PCB 走线与电气仍未核验。下述每一处引脚只能称“候选”而不是成品接线表。**

详见 `docs/board-identification.md`（`BOARD_STYLE_CONFIRMED_BY_USER` / `BOARD_ELECTRICAL_UNVERIFIED`）。

## 已证实的器件事实与限制

- STM32F401RCT6是LQFP64的xC器件，256KiB Flash、64KiB SRAM、84MHz、单12bit ADC、USB OTG FS 12Mb/s、4个双向端点编号；没有片上DAC。
- 完整MCU家族最多3 I²C、3 USART、4 SPI；SPI4的部分功能脚位于其他封装，不能因为外设寄存器存在便声称本LQFP64板可引出。仅一枚ADC，多通道是时分扫描。
- I²C手册列出100/400kHz标准/快速模式，并给出高于400kHz的扩展可能性说明；本板内部弱上拉下**不将1MHz、400kHz乃至100kHz可靠传输视为保证**。I²C测试若失败应提示Rise Time / Pull-up Risk。GPIO上拉能力、连线电容、目标板上拉均不可假定。
- ADC 2.4MSPS是特定配置下的营销最大值；数据手册在30MHz ADC时钟下给出12位和最短3周期采样约2MSPS等独立数值。实际触发、扫描、源阻抗、STM32时钟限制决定可达速率。
- 用户已经确认这块开发板属于此前参考图所示**黑色PCB、USB Type-C、Black Pill风格 STM32F401RCT6 最小系统板**；仍不能由参考图确定HSE频率、LED/电阻、BOOT0/复位电路实际连接。不得把常见48pin Black Pill板的电路图复制为本板的事实。

## 候选基础引脚组（仅芯片AF层面候选）

| 角色 | MCU资源 | 候选GPIO | AF / 模式 | 资源及使用说明 | 状态 |
|---|---|---|---|---|---|
| USB | USB OTG FS | PA11 DM, PA12 DP | AF10 | 需核对连接器接线、VBUS检测及供电、USB时钟 | PCB待确认 |
| SWD | SWDIO, SWCLK | PA13, PA14 | AF0 | 预留调试，不可动态抢占 | 芯片确定/板待确认 |
| I²C #1 | I2C1 | PB8 SCL, PB9 SDA | AF4 open-drain | 仅内部弱上拉；与TIM4 CH3/CH4冲突 | PCB待确认 |
| I²C #2 | I2C2 | PB10 SCL, PB3 SDA | AF4/AF9 open-drain | PB3 与 SWO 互斥，保留两线 SWD | PCB待确认 |
| UART #1 | USART1 | PB6 TX, PB7 RX | AF7 | 与TIM4 CH1/CH2及I2C1备选映射冲突 | PCB待确认 |
| UART #2 | USART2 | PA2 TX, PA3 RX | AF7 | 与ADC2/3, TIM2/5候选通道冲突 | PCB待确认 |
| PWM #1 | TIM5 CH1 | PA0 | AF2 | 32位TIM5独立频率组；占用ADC0 | PCB待确认 |
| PWM #2 | TIM3 CH1 | PC6 | AF2 | 与USART6 TX、SDIO等复用冲突 | PCB待确认 |
| SPI #1 | SPI1 | PA5 SCK, PA6 MISO, PA7 MOSI | AF5 | 需要额外软件CS GPIO；占用部分ADC引脚 | PCB待确认 |
| SPI1 CS(软件) | GPIO | PB12（建议候选） | GPIO输出 | 与SPI2 NSS及TIM1 BKIN互斥；可由资源管理器改变 | PCB待确认 |
| ADC输入池 | ADC1 | PC0-PC5、PB0/PB1、PA1/PA4等 | Analog | 单ADC时分扫描；仅列芯片候选，按实板与业务占用计算 | PCB待确认 |
| 扩展I²C | I2C3 | PA8 SCL, PC9 SDA | AF4 | 与TIM3 CH4等存在复用，属P2 | PCB待确认 |
| 扩展SPI | SPI2 | PB13 SCK/PB14 MISO/PB15 MOSI | AF5 | SPI2_CS可动态占用PB12；属P2 | PCB待确认 |
| 扩展SPI | SPI3 | PC10 SCK/PC11 MISO/PC12 MOSI | AF6 | 按LQFP64封装和板引出再核对；属P2 | PCB待确认 |
| 扩展UART | USART6 | PC6 TX/PC7 RX等 | AF8 | 与基础PWM #2直接冲突，不能无条件并发 | PCB待确认 |

**上述映射引用DS9716 Table 8/9中AF定义；是否在LQFP64具体引脚存在并被PCB接出，必须由板卡原理图/实拍逐条验证。** 当同时承诺I²C1+I²C2、USART1+USART2、TIM5+TIM3、SPI1时，这份候选方案在GPIO引脚复用上没有直接重叠，仍不等于DMA/总线带宽全并发保证。

## 固定保留与动态分配边界

1. **固定保留**：PA11/PA12 USB；PA13/PA14 SWD；经实板确认的两组I²C、两组UART、两组独立频率PWM为P0基础接口。
2. **动态扩展**：SPI额外CS、I²C3、USART6、SPI2/3、额外PWM与ADC通道可以按资源图动态申请；同一引脚不能同时处于数字AF和Analog模式。
3. 资源管理器采用原子Reserve→Validate→Commit / Release；应一次性申请GPIO AF、TIM主体、TIM通道、DMA候选流、NVIC共享中断、缓存配额。失败回滚未提交配置并明确指出GPIO/AF/TIM/Stream冲突。
4. **不承诺全部功能总并发**；分别保证双I²C、双UART、双独立PWM可运行的条件，并通过实板测试报告确认。

## DMA硬件请求候选（RM0368 Tables 28/29）

| 外设方向 | 合法候选 | 风险提示 |
|---|---|---|
| ADC1 RX | DMA2 Stream0 Channel0 **或** DMA2 Stream4 Channel0 | S0会与SPI1 RX等冲突，S4可能与其他用途竞争 |
| SPI1 RX | DMA2 Stream0 Channel3 **或** DMA2 Stream2 Channel3 | S0争ADC1，S2争USART1 RX |
| SPI1 TX | DMA2 Stream3 Channel3 **或** DMA2 Stream5 Channel3 | S5与USART1 RX有竞争 |
| USART1 RX | DMA2 Stream2 Channel4 **或** DMA2 Stream5 Channel4 | 与SPI1/可选USART6等冲突 |
| USART1 TX | DMA2 Stream7 Channel4 | 若独占可稳定使用 |
| USART2 RX | DMA1 Stream5 Channel4 | 与若干DMA1外设候选冲突 |
| USART2 TX | DMA1 Stream6 Channel4 | 与TIM4 Update等候选冲突 |
| I2C1 RX | DMA1 Stream0 Channel1 **或** DMA1 Stream5 Channel1 | 需再核对完整请求表和其他启用功能 |
| I2C1 TX | DMA1 Stream6 Channel1 **或** DMA1 Stream7 Channel1 | 与USART2 TX/TIM等冲突 |
| I2C2 RX | DMA1 Stream2 Channel7 **或** DMA1 Stream3 Channel7 | 芯片仅允许上述映射 |
| I2C2 TX | DMA1 Stream7 Channel7 | 与I2C1 TX等可能发生冲突 |
| TIM5/TIM3 DMA | 按RM0368与具体更新事件确定 | 不保证软件选择任意DMA流 |

此表只列部份常用候选；Codex 必须生成**官方完整二维DMA请求矩阵**，通过脚本校验合法映射与分配冲突，不得在运行时任意调换Channel。UART RX保持DMA优先；I²C小事务必要时可中断降级；ADC连续捕获不得在没有确认中断吞吐能力的情况下“透明降级”。

## ADC候选输入的占用说明

- 优先模式：仅单通道ADC工作，尽可能提高采样率，资源管理器可向ADC归还平时用于其他功能的Analog脚位。
- 多通道模式：扫描顺序、每通道采样时间、实际总采样率明确回传。不同输入通道**不是同步转换**。
- ADC输入有效范围是`VSSA .. VREF+`（F401的VREF+与VDDA内部连接），本设计不承诺输入5V或负压容限；模拟模式不要使用5V tolerant数字IO的简化规则。
- GND/3.3V跳线测试需核对VDDA、电源轨及脚位合法，不能将输出GPIO与外部供电强行相连。

## USB端点/时钟/ROM DFU

- 计划 EP0 Control；`0x01` Bulk OUT；`0x81` Bulk IN应答/事件；`0x82` Bulk IN大数据流；USB FS Bulk最大包64B。这里为**设计候选地址**，具体OTG FIFO/RX FIFO/端点寄存器配置需要USB栈及实测确认。
- 必须校验HSE实际频率及PLL配置，SYSCLK目标84MHz、USB时钟48MHz；HSI替代方案不可未经USB电气准确度验证即批准量产。
- AN2606指出xB/C系统存储器Bootloader可支持DFU USB FS；但Boot模式、BOOT0脚、HSE频率限制和USB物理连线仍需实板验证。第一阶段只承诺ST-Link SWD。

## PCB证据获取清单

- [x] 用户确认：开发板与此前展示的 STM32F401RCT6 Black Pill 风格参考图一致（外观/选型确认，非电气核验）
- [ ] 芯片正面清晰丝印与PCB正反面**实物照片**
- [ ] HSE晶振丝印或频率测量结果
- [ ] USB连接器D+/D-/VBUS/CC及供电路径（Type-C需验证CC下拉实现）
- [ ] BOOT0/NRST/SWD的连接与可操作性
- [ ] 每个候选GPIO的PCB实际引出表
- [ ] 板载LED、按键、上拉电阻是否占用候选GPIO
- [ ] 最终引脚图、DMA分配、并发组合基于以上证据签署确认


---

<!-- 章节来源：docs/architecture.md -->

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

<!-- 章节来源：docs/usb-protocol.md -->

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

<!-- 章节来源：docs/windows-gui-mcp.md -->

# Windows SDK / GUI / MCP Server 设计规格

## 一个共享设备栈

`transport WinUSB/libusb -> usb2xxx-protocol -> Python SDK -> [CLI, PySide6 GUI, MCP Server]`

Windows 10/11 优先 WinUSB 自动绑定。Python层可采用经Windows验证的libusb-1.0+WinUSB backend；具体Python绑定和本地DLL来源由Codex选型、锁版本、做许可审查并留存ADR。SDK同时提供同步与async接口、类型提示、异常体系及可安装离线wheel。GUI和MCP不能各自重新解析USB帧。

**进程仲裁**：同一块设备原则上只能由一处会话管理器独占。若GUI与MCP同时运行，推荐本机broker进程持有独占USB句柄，GUI/CLI/MCP使用本地IPC调用或显式提示设备已被其他客户端占用；不能让多个Python进程同时独立发送Request ID。M0可先实现单进程独占，正式跨进程共享属后续里程碑的必测项。

## SDK建议接口

```python
from usb2xxx import USB2XXX, I2CMessage, SpiSegment

with USB2XXX.open(serial="...") as dev:
    caps = dev.capabilities()
    value = dev.adc.read_once(pin="PC0", resolution="auto")
    dev.pwm.configure(channel="pwm0", frequency_hz=1000, duty_ppm=250000)
    dev.pwm.start("pwm0")
    # 默认安全停止；上下文退出自动关闭会话

# await USB2XXX.open_async(...), async for packet in dev.adc.capture_stream(...)
```

以上只是接口设计例子，不是现有可运行SDK。实际必须由capabilities+资源锁确定逻辑通道与物理引脚映射，不得对PCB引出情况作假设。

## Windows GUI（P1 必交）

- 推荐 PySide6（版本经离线分发许可验证）；GUI不直接持有底层IO线程的长期阻塞操作。通讯Worker和async调度要与Qt事件循环保持线程安全。
- 设备页：连接/断开/重连、UID别名、固件/协议版本、时钟与供电信息（仅真实可读取数据）、引脚资源占用、会话租约、错误日志。
- I²C页：2路以上配置、Master/Slave切换、设备扫描（符合弱上拉风险提示）、组合事务编辑器、寄存器读写、Slave事件。
- UART页：双路独立终端、HEX/ASCII、波特率/帧格式、发送文件、收发流量和错误计数。
- PWM页：多通道频率/占空比ppm/极性/有限脉冲，显示实际硬件频率与分辨率、独立TIM资源及安全停机。
- SPI页：Mode0-3、MSB/LSB、8/16bit、CS选择、分段事务编辑、HEX读写、吞吐统计。
- ADC页：单通道高速优先、扫描/触发/预后采样、实时曲线（显示速率/丢样/缓冲状态）、CSV/二进制/NumPy导出；实时绘图不能拖慢USB采集。
- 诊断与测试页：Mock/实板标志、自测试接线、Host单测入口、故障/冲突码、日志导出、恢复操作。用户不需要通过修改源码来配置任何基础功能。
- GUI在无硬件时必须能够连到MockDevice展示功能，且明确视觉标识为SIMULATED，不可冒充真实板。

## MCP Server 正式需求（P1 必交）

- 建议 Python MCP SDK（经验证并锁定的版本）实现**本机stdio**，供支持MCP的Agent/IDE/自动化程序启动；`stdout`仅用于MCP协议，日志向stderr；不默认监听HTTP公网端口。
- 一个MCP Server共享Python SDK，可连接真实Device或显式MockDevice；工具返回结构化字段`device_id, simulated, request_id, status, actual_values, warnings, resource_conflicts`。
- MCP `tools` 最小集合：

| 工具建议名 | 参数/职责 | 危险等级 |
|---|---|---|
| `usb2xxx_list_devices` | 搜索设备和模拟器 | 只读 |
| `usb2xxx_get_capabilities` | 查看功能/引脚/带宽/版本/资源 | 只读 |
| `usb2xxx_get_status` | 会话/输出状态/异常/统计 | 只读 |
| `usb2xxx_i2c_transfer` | bus_id, master/slave, 多消息组合事务, timeout | 写操作可能改变外设 |
| `usb2xxx_uart_configure` | uart_id, baud, parity, stop bits | 配置 |
| `usb2xxx_uart_write` | UART发送二进制/文本(大小上限) | 写操作 |
| `usb2xxx_uart_read` | 最大长度/超时 | 读数据 |
| `usb2xxx_pwm_configure` | 输出、频率Hz、duty_ppm、极性 | 输出风险 |
| `usb2xxx_pwm_start` | 资源租约、最大持续时间、明确arm | 输出风险 |
| `usb2xxx_pwm_stop` | 安全停止指定输出/全部PWM | 安全控制 |
| `usb2xxx_spi_transfer` | 控制器、CS、模式、读/写分段和超时 | 写操作 |
| `usb2xxx_adc_read` | 输入通道/采样周期/分辨率 | 只读采样 |
| `usb2xxx_adc_capture` | 通道序列、采样率、长度、触发、timeout | 资源消耗 |
| `usb2xxx_get_diagnostics` | 运行日志、错误码、缓冲统计 | 只读 |
| `usb2xxx_self_test` | 选择安全测试方案，要求确认已按接线文档连接 | 可能驱动引脚 |
| `usb2xxx_stop_all` | 所有输出进入安全状态 | 紧急操作 |

- 长时间ADC/UART传输不应直接以海量文本塞入MCP Tool结果；应提供`start_capture`返回`capture_id`、`get_capture_status`、`read_capture_chunk`或本地文件导出工具，块大小限制明确，防止Agent上下文被占满。
- 提供MCP `resources`，例如`usb2xxx://devices/{id}/capabilities`、`.../pinmap`、`.../status`、`.../handoff`；这些只读资源必须标明Mock或实板来源。
- 可选MCP `prompts`：UART自测流程、I²C弱上拉诊断、ADC采集计划、SPI回环流程。

## MCP与真实硬件的安全策略

1. 默认只允许本机已授权USB设备，并可按UID建立allowlist；不能透传任意物理内存写入/调试寄存器写入/固件刷写作为普通MCP工具。
2. 输出类工具要求明确设备、资源、限时、最大波特率/频率/持续时间、最大字节数/样本数；超限快速拒绝并返回可解释错误。
3. 为Agent提供`dry_run`/`validate_resources`预检；对实际写操作可设显式`confirm=true`或在本机配置允许的策略；不允许通过返回的工具文本绕过权限校验。
4. 会话心跳在工具调用结束后不应无限续命持续输出；MCP进程退出/USB断开/租约超时必须触发安全停止。
5. 所有二进制数据以有界hex/base64或文件引用处理，所有写/更改操作在结构化诊断记录中记录，但不记录敏感内容或完整UID到可上传遥测。
6. `stdio`环境仅代表本地传输，不替代外设安全防护。任何HTTP远程模式必须独立ADR与认证方案，**不是当前首版要求**。

## 软件测试与交付

- `MockTransport`与`MockDevice`是核心验收组成部分，GUI、CLI、MCP都必须使用同一假设备进行无硬件演示。
- MCP自动化：Inspector或正式Python MCP client进行`list_tools`、参数schema、正反向调用、设备断线、错误码、模拟器保护检查；实际USB和真实端口的测试留待HIL。
- Windows离线包要求：Python环境/依赖wheel、本机libusb.dll合法分发、GUI运行入口、MCP启动入口、SHA256校验清单、许可证/NOTICE、Windows10/11安装和权限说明。

权威参考： https://py.sdk.modelcontextprotocol.io/ 、 https://modelcontextprotocol.io/ 、 https://github.com/hathach/tinyusb


---

<!-- 章节来源：docs/roadmap.md -->

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

<!-- 章节来源：docs/test-and-acceptance.md -->

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

这些目标是未来Makefile的接口要求；当前文档包没有Makefile或可执行固件，请勿将命令当作已经存在。
