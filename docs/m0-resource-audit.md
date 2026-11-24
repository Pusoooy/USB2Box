# M0 资源审查

M0 依据 ST 官方 STM32_open_pin_data 的 LQFP64 封装和 AF XML 生成候选引脚表，并以 Embassy F401RC 数据生成 DMA 矩阵。0.1.2 补做 ST 官方 PDF 交叉核对。DS9716 Rev 11 表 8 验证了候选表的 47 条封装脚记录，表 9 复核 PA1/PC7 的 AF2 与 PB3 的 AF9。RM0368 Rev 6 表 28、29 和 43 分别复核 DMA 请求映射与 TIM3 TRGO 的 ADC 触发编号。DMA 128 个单元一致，比较时按目标型号去除 SPI4，并去掉候选表中 I²S 扩展请求的 RX/TX 方向后缀。PCB 引出仍待实测。[下载校验与原始日志](evidence/m12-runtime-verify.log)

- ST STM32_open_pin_data，提交 `45c04599e2e0a788c5ed9b6218dc5a28a36041f7`，文件 `mcu/STM32F401R(B-C)Tx.xml`、`mcu/IP/GPIO-STM32F401_gpio_v1_0_Modes.xml`。[来源](https://github.com/STMicroelectronics/STM32_open_pin_data/tree/45c04599e2e0a788c5ed9b6218dc5a28a36041f7)
- Embassy stm32-data-generated，提交 `5de055b8234e29262a6fcfd8b4f4340071c4752a`，文件 `data/chips/STM32F401RC.json`。[来源](https://github.com/embassy-rs/stm32-data-generated/blob/5de055b8234e29262a6fcfd8b4f4340071c4752a/data/chips/STM32F401RC.json)
- ST 数据手册与参考手册。[DS9716 Rev 11](https://www.st.com/resource/en/datasheet/stm32f401rc.pdf) · [RM0368 Rev 6](https://www.st.com/resource/en/reference_manual/rm0368-stm32f401xbc-and-stm32f401xde-advanced-armbased-32bit-mcus-stmicroelectronics.pdf)

| 风险或组合 | 静态结论与后续 |
|---|---|
| USB + SWD + 双 I²C + 双 UART + 双 PWM + SPI1 | 基础 GPIO 集合互不重叠。PB11 不在封装中，I2C2_SDA 改用 PB3 AF9。PB3 是否由 PCB 引出仍待核验 |
| USART6 与 PWM2 | PC6 互斥 |
| ADC 与基础数字组 | PA0、PA2、PA3、PA5–PA7 互斥。PA1、PA4、PB0–PB1、PC0–PC5 可供候选 |
| 双 UART、SPI1、ADC1 | 候选可全 DMA。USART1 RX/TX 用 DMA2 S2/S7，USART2 RX/TX 用 DMA1 S5/S6，SPI1 RX/TX 用 DMA2 S0/S3，ADC1 用 DMA2 S4 |
| 双 I²C | 单独可全 DMA。I2C1 RX/TX 可用 DMA1 S0/S6，I2C2 RX/TX 可用 DMA1 S2/S7。叠加双 UART 后 I2C1 TX 无剩余 Stream，可评估 I2C1 TX 中断 |
| PWM | TIM5/TIM3 独立频率。基础持续输出仅需要定时器通道。有限脉冲计数的 IRQ/DMA 选择留待实现 |
| OTG FS | 端点 0–3 范围内使用 EP0、EP1 OUT、EP1 IN、EP2 IN。320 个 32 位字的 FIFO 预算候选为 RX 128、EP0 TX 32、EP1 TX 64、EP2 TX 96。M2 需核对所选 DCD 的 FIFO 分配及收发水位 |
| 时钟 | 目标 PLL VCO 336 MHz，P=4、Q=7 得 84/48 MHz；PLL 输入可按可核实的 HSE 选择 M。待核验 HSE、供电和时钟精度后启用 USB |
| USB 走线与 I²C 上拉 | Type-C CC、VBUS、D±、实板引脚均待实物证据。弱上拉下 I²C 低速短线仍可能失败 |
| VID/PID | 未分配，USB 产品发布待合法标识 |

M0 ARM 镜像保持复位时钟和外设禁用，预留 8 KiB 栈。帧解析仅由 Host 测试执行，当前 ARM 链接的节回收会移除未引用的帧函数。
