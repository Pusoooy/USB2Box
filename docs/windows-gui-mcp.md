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
