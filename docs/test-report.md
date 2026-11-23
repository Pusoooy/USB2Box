# Test Report / 测试报告

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

Linux x86-64，Debian 13，Host GCC 14.2.0，ARM GCC 14.2.1，binutils 2.44。执行 `make clean && make verify && git diff --check`，退出 0。两份 ARM 镜像均为 480 B Flash，静态 RAM 0 B，链接脚本保留 8 KiB 栈空间。当前镜像只包含复位启动和空闲循环。

[原始验证日志](evidence/m0-verify.log)
