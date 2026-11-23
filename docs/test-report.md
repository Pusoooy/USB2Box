# Test Report / 测试报告

## M1

| 验证项 | 结果 | 证据 |
|---|---|---|
| 固定依赖下载与离线校验 | PASS | 空缓存 `make fetch-deps`；`make offline-verify` 校验归档 SHA-256 和解包内容。人为修改的临时 HAL 头文件副本被拒绝 |
| 时钟选择与回退 | HOST_MOCK_PASS | `make test-clock` 覆盖 23 个 HSE 频率、默认 HSI、非法频率和两种回退结果 |
| STM32F401RC 镜像 | ARM_BUILD_PASS | `make -j2 verify`，以及 `make -j2 HSE_HZ=8000000 test-arm`、`make -j2 HSE_HZ=25000000 test-arm`。六份镜像通过复位、SysTick、OTG FS 向量、栈边界和无动态分配器检查 |
| 构建拒绝无效时钟 | PASS | `make HSE_HZ=25000001 debug` 按设计在编译期拒绝 |
| 实板晶振、时钟精度、烧录、枚举与 HIL | NOT_RUN / WAITING_HARDWARE | 无实板 |
| DS9716 与 RM0368 原表复查 | NOT_RUN | ST 域名仍返回代理 HTTP 403 |

| 构建配置 | Debug Flash（BIN 字节） | Release Flash（BIN 字节） | 静态 RAM（字节） |
|---|---:|---:|---:|
| 默认 HSI | 4,360 | 3,928 | 20 |
| HSE 8 MHz 候选 | 4,440 | 3,936 | 20 |
| HSE 25 MHz 候选 | 4,388 | 3,936 | 20 |

静态 RAM 为 .data 12 B 和 .bss 8 B，另预留 8 KiB 栈。HSE 两组频率只用于交叉构建样本。环境为 Debian 13 x86-64，Host GCC 14.2.0、ARM GCC 14.2.1、binutils 2.44，验证序列退出 0。[原始验证日志](evidence/m1-verify.log)

## M0

| 验证项 | 结果 | 证据 |
|---|---|---|
| ST LQFP64 封装脚号与 AF | PASS | 固定 ST XML，经 `tools/import_resources.py` 核验 |
| GPIO 和 DMA 候选分配 | PASS（静态） | `make test-resources`，2 项 |
| 帧头向量和异常输入 | PASS（Host 单测） | `make test-protocol`，含 10,000 次确定性变异 |
| Debug / Release | ARM_BUILD_PASS | `make verify` 交叉编译、链接及生成 ELF/BIN/HEX/MAP |
| DS9716 与 RM0368 原表复核 | NOT_RUN | ST 站点当前代理返回 403 |
| 烧录、USB/WinUSB、GPIO/DMA、外设性能与 HIL | NOT_RUN / WAITING_HARDWARE | 无实板 |
| MockTransport、SDK、Windows GUI、MCP | NOT_IMPLEMENTED | 后续里程碑 |

Linux x86-64，Debian 13，Host GCC 14.2.0，ARM GCC 14.2.1，binutils 2.44。执行 `make clean && make verify && git diff --check`，退出 0。两份 ARM 镜像均为 480 B Flash，静态 RAM 0 B，链接脚本保留 8 KiB 栈空间。M0 镜像只包含复位启动和空闲循环。

[原始验证日志](evidence/m0-verify.log)

## M2

`make clean && make -j2 verify && make -j2 HSE_HZ=8000000 test-arm` 通过。Host 单测覆盖字节流切分/粘连、坏头部恢复、分片、重复请求、ID 回绕、背压及租约失效。两个构建配置均验证了 Cortex-M4 向量、栈边界及无动态分配器。默认 Release 静态 RAM 3,716 B。USB/WinUSB 实板验证 `NOT_RUN / WAITING_HARDWARE`。[原始日志](evidence/m2-verify.log)

## M3

`make -j2 verify` 退出 0。Host 检查原子预留失败无副作用、保留引脚不可抢占、DMA Stream 互斥和非法 Channel 拒绝，也检查队列溢出、同通道串行、跨通道推进、超时回绕、取消和部分结果。资源队列此阶段尚未从固件入口调用，链接结果仍是 M2 体量。[原始日志](evidence/m3-verify.log)

## M4

`make -j2 verify` 退出 0。双向各 40×513 B 的随机数据、断点跨环和溢出被实际测试。Release 静态 RAM 16,668 B。USART 实板 DMA 与跳线回环 NOT_RUN。 [原始日志](evidence/m4-verify.log)

## M5

`make -j2 verify` 退出 0，模拟总线交换主从角色，验证组合读写、接收事件及超时。PCB 上拉及数据波形 NOT_RUN。 [原始日志](evidence/m5-verify.log)

## M6

`make -j2 verify` 退出 0。检查定时器量化、极端占空比和回绕时的双组独立停机。波形、脉冲计数及输入捕获 NOT_RUN。 [原始日志](evidence/m6-verify.log)

## M7

`make -j2 verify` 退出 0。四种模式每种保持片选四个 256 B 分段，并验证取消。时序和实板回环 NOT_RUN。 [原始日志](evidence/m7-verify.log)
