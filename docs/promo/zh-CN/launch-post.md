# 给 AI Agent 一双手：Kiseki Input——一个原生、跨平台、可验证的桌面操作 CLI

> 发布稿（可直接用于掘金 / 知乎 / V2EX / 公众号 / GitHub Discussions）。
> 仓库：https://github.com/win10ogod/kiseki-input

让 AI Agent 去操作一台真实的电脑，听起来很简单：截个图、点一下、打几个字。真做起来，几乎所有人都会撞上同一批墙：

- 方案绑死某一个平台，换台机器就得重写；
- 一堆 Python / Node 依赖，装完环境比干活还久；
- Agent 一动手就**抢走你的鼠标和键盘**，人和 Agent 没法同时用一台电脑；
- 出了问题只知道“没点中”，不知道是权限、是目标窗口不接受，还是根本不支持；
- 给它一个能执行任何操作的接口，你得先相信它不会乱来。

**Kiseki Input** 是我为这几堵墙做的一个小工具：一个用 C++ 写的原生命令行，把键盘鼠标、截图、宏、教学录制、后台操作和通知收在同一套命令里，Windows、macOS、Linux 通用。

## 它能做什么

**一个命令，三个平台。** 键鼠输入、桌面 / 窗口 / 连拍截图、窗口列表与检查、非视觉的界面结构观察（Windows UI Automation、macOS Accessibility），都用同样的子命令：

```bash
kiseki doctor                       # 这台机器到底支持什么
kiseki target list                  # 有哪些窗口
kiseki screenshot desktop --output shot.bmp
kiseki input text --text "你好，Kiseki"
```

**后台操作，不抢你的鼠标。** 通过 Cua Driver，`kiseki background cua ...` 可以在后台启动应用、读取窗口的控件树、对指定窗口点击、输入、按键、拖拽，也可以操作浏览器。你在前台写文档，Agent 在后台整理表格；README 里的 macOS 演示中，Safari 始终在最前面，鼠标位置纹丝不动。Linux 上还能把 GUI 应用放进独立的 Xvfb 桌面里跑，完全不碰你当前的桌面。

**用“动作序列”教 Agent，而不是塞一段视频。** `kiseki teach record` 录下的是动作序列 + 选中的关键帧 + 原生输入事件，并自动生成一份 `SKILL.md`。Agent 读的是一条紧凑的时间线，不是几百帧画面——更便宜，也更不容易看花眼。

**宏，让一次手动流程变成可重复的检查。** JSON 宏支持键盘、鼠标、拖拽、截图、等待，既能驱动当前桌面，也能驱动后台窗口。

## 为什么我愿意把它交给 Agent

**1. 边界写在代码里。** 它自带一个本地 WebUI，但 WebUI 只有三个接口：读配置、写配置、读能力。**它不能触发任何输入、截图、通知、守护进程、shell 或进程启动。**真正动手的只有命令行，而命令行是谁调用的、调用了什么，一目了然。

**2. 平台做不到，就明说。** Windows 是目前实测最多的平台；Linux 面向 X11 会话，Wayland 需要各自的后端；macOS 分成“原生路径”和“Cua 后台路径”两条，权限归属不同。`kiseki capabilities` 和 `kiseki doctor` 会告诉你这台机器真正能做什么——而不是假装所有桌面都一个样。

**3. 为“不那么聪明的模型”也能守规矩而设计。** `kiseki modes` 给出一组稳定的选择规则：`input` 开头是当前会话操作，`screenshot` 开头是非后台截图，`background` 开头是后台操作；后台操作要用**同一族**的截图来验证；平台结构信息够用时先 `observe ui`，再考虑截图。弱一点的模型照着规则走，也不容易走偏。

**4. 每一步都有验证。** Cua 返回的是带 `effect` 字段的结构化结果（confirmed / submitted / unverifiable / refused）；被拒绝的动作会变成非零退出码，而不是被悄悄吞掉。

## 最新：对齐 Cua Driver 0.38

这次更新把 Cua 的集成对齐到 Driver 0.38.0（此前是 0.30.1）：

- `background cua browser select`：原生 HTML `<select>` 的选择（0.37）；
- `background cua guide` / `health`：Driver 自带的 Agent 指南和端到端诊断，macOS 上包含 WindowServer 连接检查（0.36 起，裸二进制必须从 CuaDriver.app 运行）；
- `skills`、`history`、`perception`、`cursor`、`config` 这些 Driver 命令族直接转发；
- macOS 上 `launch --launch-path /Applications/某某.app` 会按 0.38 的约定作为 `name` 发送；
- [docs/cua.md](../../cua.md) 里有 0.31–0.38 每一项变化和 Kiseki 对应方式的对照表。

## 一个真实的用例

我把它接进了自己的一个 Emacs AI 代理里：让一个本地模型通过 Kiseki 在 Mac 上“像人一样”做事——打开计算器，读出窗口的控件树，按 `1 7 × 2 3 =`，再把显示屏上的 `391` 读回来告诉我。整个过程不抢焦点；代理每次动手前先问我，我的同意只管 10 分钟、60 个动作，随时可以收回。Kiseki 做的是“手”，什么时候、对谁、动几次，由上层的规则决定。

## 三分钟上手

```bash
# 1. 下载 Releases 里对应系统的安装包（Windows .exe / macOS .pkg / Linux .deb）
kiseki --version
kiseki doctor
kiseki modes
kiseki screenshot desktop --output screenshot.bmp

# 2. 需要后台操作时
kiseki background cua setup
kiseki background cua windows
```

构建也很直接：

```bash
cmake -S . -B build -DKISEKI_BUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

## 适合谁

- 在做 Agent / Computer Use，想要一个**不挑平台、不抢焦点、能验证**的“手”；
- 想把重复的桌面操作做成**可重放的宏**；
- 想用**教学录制**把你的操作流程交给 Agent 学；
- 想要一个**边界清楚**的本地自动化工具，而不是一个黑盒。

欢迎试用、提 Issue、提 PR：https://github.com/win10ogod/kiseki-input
