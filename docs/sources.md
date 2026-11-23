# 参考资料 / 资料来源

下列资源应在实际编码前由 Agent 核验版本和适用型号：

- ST STM32F401xB/xC 数据手册 DS9716 — MCU Flash/SRAM、外设、AF、ADC性能、封装；从 [ST STM32F401RC 产品页](https://www.st.com/en/microcontrollers-microprocessors/stm32f401rc.html) 进入。
- ST RM0368 Reference Manual — GPIO AF、RCC、TIM、ADC、SPI/I2C/USART、DMA1/2 请求 Stream/Channel 固定映射。
- ST AN2606 — STM32 ROM Bootloader 与 USB DFU 进入条件，必须核对目标具体子系列和硬件引出。
- [TinyUSB](https://github.com/hathach/tinyusb) 官方仓库及 STM32F4 Device DCD 示例。
- [Microsoft WinUSB 文档](https://learn.microsoft.com/windows-hardware/drivers/usbcon/automatic-installation-of-winusb) — MS OS 描述符与自动安装。
- [Model Context Protocol](https://modelcontextprotocol.io/) 官方规范、Python SDK 文档。

务必固定版本、许可与原始URL。资料没有核实前不能据此声称某引脚在本PCB已引出。

## M1 固定依赖

| 组件 | 版本 | 上游归档中的许可文件 |
|---|---|---|
| CMSIS Core | 5.9.0 | `LICENSE.txt`，Apache-2.0 |
| CMSIS Device F4 | v2.6.10 | `LICENSE.md`，Apache-2.0 |
| STM32F4 HAL | v1.8.3 | `LICENSE.md`，BSD-3-Clause |

提交 URL 和归档的 SHA-256 见 [依赖锁](../tools/arm-dependencies.lock)。工具链取自 Debian trixie 签名仓库的 `gcc-arm-none-eabi=15:14.2.rel1-1` 和 `binutils-arm-none-eabi=2.44-3+23+b1`。离线发行物和工具链再分发清单留待 M12。
