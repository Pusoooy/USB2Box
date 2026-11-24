# USB2Box 实板验收

当前开发报告以 ARM 链接和 Host Mock 为证据。HIL 需要 STM32F401RCT6 实板、ST-Link、Windows 10/11 或其他受支持的 libusb 主机。先核对 PCB 的 HSE、D±、VBUS、Type-C CC、供电走线及引脚引出。当前 TinyUSB STM32F4 DCD 采用 B-session 有效覆盖，首轮验收针对 USB 总线供电。自供电方案需单独验证 VBUS 检测。使用已有短杜邦线，保持 3.3 V 电平，并保留 SWD。

## 固件与基础探测

设置已核实的 `HSE_HZ` 和合法的 `USB_VID`、`USB_PID` 后构建。使用 `make flash` 和相同 Make 参数，将 Debug BIN 交给 ST-Link 的 `st-flash`。记录芯片识别、时钟寄存器、WinUSB 的两个接口、端点地址以及固件 SHA-256。

`make hil-verify HIL_ARGS='...'` 调用 `tools/hil.py`。它需要真实 VID/PID，非 USB 探测还需 `--fixture`、`--board-rev`、`--wiring`、`--firmware` 与 `--log`。有多板时按序列号选择。每轮只连接当前夹具，避免不同输出脚互接。

| 夹具 | 短线连接与观察 |
|---|---|
| UART | PB6→PA3、PA2→PB7，双向校验 9600、115200 和 921600。独立记录帧错误和丢失 |
| I²C | PB8↔PB10、PB9↔PB3，10 kHz 尝试交换主从角色。同步记录实际上升沿和外部上拉情况，内部弱上拉条件不达标时保留现场证据 |
| PWM | 分别 PA0→PB6（`--group 0`）和 PC6→PB6（`--group 1`），TIM4 测量更新前后的频率与占空比。核验 PCB 引出后可分别用 PA1→PB6 和 PC7→PB6 测试 `--pwm-channel 2`。示波器确认同组启停互不干扰、有限脉冲数、更新边界、断线和停机后的低电平 |
| SPI | PA7→PA6，核对 4 种模式与 8/16 bit 分段回读，另外观察片选间隔和 CS 释放 |
| ADC | 指定板上可确认的模拟通道，先接 GND，再按实板规格接 VDDA 对照。记录原始码和测量电压，命令只记录读数 |
| WinUSB 与压力 | 观察两个接口自动绑定，重复插拔和主机进程异常退出。测量 0x82 持续流目标 100 kSPS、控制延迟和丢样，执行 GUI/MCP 各功能以及跨进程独占检查 |

H08 的 ADC 项使用 `--fixture adc-stream --channel 10 --seconds 10`，可替换为已核验的模拟输入。日志记录主机收到的样本速率和字节数、固件报告速率、偏移中断、丢样、超时及并发控制请求的最大往返延迟。提前停机、掉样或流读超时记录为失败，完整采集记录为测量结果。评估 100 kSPS 目标时以主机实收数据为准。

绿色软件门禁不能替代波形、时钟、实际 USB 吞吐或 Windows 装驱动结果。所有原始记录都应包含工具版本、接线、固件版本、退出码和日期。
