# Kiseki Input

**给 AI Agent 的一双手：一个原生、跨平台、可验证的桌面操作命令行。**

[English](README.md) · 简体中文

Kiseki Input 是一个用 C++ 写的原生 CLI：键盘鼠标输入、系统截图、可重放的 JSON 宏、教学录制（动作序列 + 关键帧）、连拍、心跳通知，以及一个只管配置的本地 WebUI——一个小工具全包。

它面向的是想要一座“实用自动化实验室”的开发者，而不是一堆一次性脚本。命令行优先，每个后端各自独立，**平台做不到的事会如实报告，而不是假装所有桌面、游戏、合成器都一个样**。

## 为什么值得试

- **一个命令，三个平台。** Windows、macOS、Linux（x64 / arm64）同一套命令：`kiseki input`、`kiseki screenshot`、`kiseki macro`、`kiseki teach`……
- **后台操作，不抢你的鼠标。** 通过 [Cua Driver](https://cua.ai)，`kiseki background cua ...` 可以后台启动应用、读取窗口控件树、对指定窗口点击、输入、按键、拖拽、画路径、操作浏览器；你在前台干活，Agent 在后台干活。在 README 的 macOS 演示里，Safari 始终在最前面，鼠标位置没有动。
- **Agent 友好的教学录制。** 录制保存的是“动作序列 + 选中的关键帧 + 原生输入事件 + 自动生成的 `SKILL.md`”，不是一整段视频：Agent 读一条紧凑的时间线，而不是几百帧画面。
- **Linux 隔离桌面。** 在独立的 Xvfb `DISPLAY` 里运行、点击、输入、截图，不碰你当前的桌面。
- **安全边界写在代码里。** WebUI 只有三个接口（读配置、写配置、读能力），**不能触发任何输入、截图、通知、守护进程、shell 或进程启动**；所有操作只走 CLI。
- **为“弱模型”也能守规矩而设计。** `kiseki modes` 给出稳定的选择规则（现场输入用 `input`，后台用 `background`，后台操作要用同一族的截图验证……），仓库自带两个 Agent 技能（`kiseki-project`、`kiseki-teach-recording`）。
- **能自检。** `kiseki doctor` 看诊断，`kiseki capabilities` 看当前机器真正支持什么，`kiseki modes --json` 给机器读的模式指南。
- **CI 覆盖五个目标。** GitHub Actions 构建并测试 Windows x64、macOS arm64 / x86_64、Linux x64 / arm64，生成安装包与 SHA-256 校验文件。

## 安装

从 [Releases](https://github.com/win10ogod/kiseki-input/releases) 下载对应的安装包：Windows `.exe`、macOS `.pkg`（Apple Silicon 或 Intel）、Linux `.deb`（x64 或 arm64）；也有 ZIP / TAR.GZ。逐步说明见[安装指南](docs/install.md)。

```sh
kiseki --version
kiseki background cua setup     # 需要后台操作时：安装并启动 Cua Driver
kiseki background cua status
```

Cua 是运行时才用到的可选组件：C++ 构建和原生命令都不需要安装它。

## 60 秒上手

```bash
kiseki doctor
kiseki modes --json
kiseki target list
kiseki screenshot desktop --output screenshot.bmp
kiseki screenshot burst --directory frames --prefix frame --frames 8 --fps 60
kiseki macro validate --file docs/assets/demos/demo-notepad-unicode.json
kiseki teach record --output teach-session --frame-interval-ms 500 --text "打开设置并保存修改"
# 再运行同一条命令即可停止并生成录制包
kiseki teach record
```

Windows 上可以直接让演示宏驱动可见的桌面应用：

```bash
kiseki macro run --file docs/assets/demos/demo-notepad-unicode.json
kiseki macro run --file docs/assets/demos/demo-paint-macro.json
```

## 选对操作模式

运行 `kiseki modes` 看人类可读的指南，或 `kiseki modes --json` 给程序读。

| 需求 | 用 | 后台？ | 验证截图 |
| --- | --- | --- | --- |
| 当前会话里打字、点击、画图 | `kiseki input ...` | 否 | `kiseki screenshot desktop` 或 `kiseki screenshot window` |
| 当前会话截图或连拍 | `kiseki screenshot desktop\|burst\|window\|window-burst ...` | 否 | 同一族命令 |
| 不激活窗口的指定窗口截图 | `kiseki background window screenshot ...` | 是 | 同一条命令 |
| 指定窗口的消息 / API 输入 | `kiseki background window text\|key\|mouse\|drag ...` | 是（取决于目标） | `kiseki background window screenshot` |
| Linux 隔离 DISPLAY | `kiseki background desktop ...` | 是 | `kiseki background desktop screenshot` |
| 通过 Cua 对目标应用操作 | `kiseki background cua ...` | 是（取决于 provider） | `kiseki background cua screenshot` 或 `state --output` |
| 结构化界面观察 | `kiseki observe ui ...` | 不是截图，也不是输入 | JSON 结果本身 |

几条稳定的规则：

- 以 `input` 开头的是**当前会话**操作，可能用到焦点和真实指针。
- 以 `screenshot` 开头的是**非后台**截图，哪怕它选了目标窗口。
- 以 `background` 开头的是后台、隔离会话或定向操作。
- 后台操作要用**同一族**的截图验证，不要拿桌面截图去验证后台动作。
- 平台 / 应用的结构信息够用时，先 `observe ui`，再考虑截图。

## 后台操作（Cua Driver）

```sh
kiseki background cua setup
kiseki background cua windows
kiseki background cua state --pid 123 --window-id 456 --output before.png
kiseki background cua click --pid 123 --window-id 456 --element-token CURRENT_TOKEN
kiseki background cua text  --pid 123 --window-id 456 --element-token CURRENT_TOKEN --text "你好"
```

`state` 返回窗口的控件树（每个控件带编号和 token）和一张截图；点击和输入对着控件 token 做，操作后重新 `state` 确认结果。浏览器有专门的语义快照与动作：`kiseki background cua browser state|prepare|navigate|click|type|dialog|upload|download|pointer|select`。

当前集成按 **Cua Driver 0.38.0** 检查（此前是 0.30.1）；0.31–0.38 的变化以及 Kiseki 如何对应，见 [docs/cua.md](docs/cua.md)。Kiseki 对工具调用做的是转发而不是拷贝 schema，所以更新的 Driver 也能继续用；`kiseki background cua tools` / `describe` 读的是你机器上真正安装的版本。

## 教学录制

录制遵循与 Agivar 相同的核心设计：给模型看的是**动作序列 + 选中的关键帧**，不是整段视频。一个录制目录包含 `manifest.json`、`frames.json`、`actions.json`、`timeline.json`、`events.jsonl`、`instruction.txt`、`annotations.json`、`keyframes/`，以及自动生成的 `SKILL.md`。第一次运行 `kiseki teach record` 开始录制，再运行一次写入停止请求并等待收尾。Windows 钩子、macOS 事件监听、X11 RECORD 独立于截图接收事件；拿不到原生事件源时，清单会明确标记为不完整的轮询回退。

## 宏

宏是带 `steps` 数组的 JSON 文件，只由 CLI 执行：`key`、`combo`、`text`、`mouse`、`drag`、`background-mouse`、`background-drag`、`screenshot`、`background-screenshot`、`sleep`。适合把一次“手动点一遍”的界面流程变成可重复的检查。

## 平台说明（如实）

- **Windows**：目前实测最多的平台。屏幕截图、窗口列表、系统输入、基于窗口消息的后台输入都在这个代码树里测过；可选的 `IbInputSimulator.dll` 驱动级输入（不可用时回退到 `SendInput` 并如实说明）。
- **Linux**：当前面向图形 X11 会话；X11/XTest 输入、窗口截图、Xvfb 隔离后台桌面；Wayland 的当前会话截图通过 XDG Desktop Portal。Wayland 与受限合成器还需要各自的后端工作；只在 WSL 里跑通不算 Linux 桌面的证明。
- **macOS**：两条明确分开的路径——原生路径（窗口服务、ScreenCaptureKit 截图、Quartz CGEvent 输入，需要当前进程有相应权限）和可选的 Cua 后台路径（权限归 CuaDriver.app 所有）。

## 目录与构建

```bash
cmake -S . -B build -DKISEKI_BUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

多配置生成器（如 Windows 的 Visual Studio）下，可执行文件通常在 `build/Debug/kiseki.exe`。

## Agent 技能

| 技能 | 用途 |
| --- | --- |
| [kiseki-project](skills/kiseki-project/SKILL.md) | 发现目标、选对操作模式、精确输入、验证应用状态，以及开发 / 测试各平台切片 |
| [kiseki-teach-recording](skills/kiseki-teach-recording/SKILL.md) | 录制或检查演示、控制录制生命周期、校验证据、编译成可复用的流程 |

## 致谢

特别感谢 [Chaoses-Ib/IbInputSimulator](https://github.com/Chaoses-Ib/IbInputSimulator)：Kiseki Input 的 Windows 驱动后端围绕它的 `IbInputSimulator.dll` 设计。
