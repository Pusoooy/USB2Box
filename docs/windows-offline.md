# USB2Box Windows x64 离线工作台

使用已有的 CPython 3.12 x64 和 Windows PowerShell。在离线包目录中运行下列命令。安装首先校验 SHA-256，再从本地 wheelhouse 安装依赖。

```powershell
.\usb2xxx.ps1 -Mode Install
.\usb2xxx.ps1 -Mode Gui
.\usb2xxx.ps1 -Mode Cli status
.\usb2xxx.ps1 -Mode Mcp
```

默认入口使用随包提供的 C 模拟器。MCP 默认限制写操作，可用 `-Mode Mcp --allow-writes` 开启该能力。真机启动使用 `-Physical`，并传入项目合法分配的 VID/PID，MCP 还需要明确授权的序列号。当前 Windows 构建及驱动枚举仍需实机验收。

libusb v1.0.29 DLL 为可替换组件。对应源归档在 `sources/`，许可证在 `licenses/`。各 wheel 中提供的许可证与 NOTICE 已复制至 `licenses/`。PySide6 与 Qt 的 LGPL/GPL 组件需保留来源和许可，公开再分发前仍需按发布方许可流程审查。Python 运行时由目标机器提供。

维护者可以使用 Python 3.12、pip、7z 和 MinGW 在 Linux 上创建离线目录。先构建 `usb2xxx-0.1.0-py3-none-any.whl` 与 Windows mock，再运行 `tools/stage_windows_release.py`。Windows wheel 和 libusb 缓存可从已校验的离线副本传入。

`make package-windows` 可执行上述三个步骤。需要 Python 构建环境中的 setuptools/wheel、MinGW 和 7z。可通过 `WIN_CC`、`SEVEN_ZIP` 和 `WIN_PACKAGE` 指定工具与新建输出目录。传入 `WIN_STAGE_ARGS='--wheels /path/to/wheels --libusb-cache /path/to/libusb'` 使用已准备的缓存，SHA-256 仍会逐项校验。
