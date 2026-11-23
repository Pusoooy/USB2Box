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
| I²C #2 | I2C2 | PB10 SCL, PB11 SDA | AF4 open-drain | 与TIM2 CH3/CH4等候选功能冲突 | PCB待确认 |
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
