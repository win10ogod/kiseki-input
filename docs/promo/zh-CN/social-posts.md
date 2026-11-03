# 社交平台短文案（简体中文）

> 每条都只用仓库里能查证的事实。链接统一用 https://github.com/win10ogod/kiseki-input 。
> 配图可直接用 `docs/assets/demos/` 里已有的 GIF：`notepad-unicode.gif`、`paint-macro.gif`、`heartbeat-notification.gif`、`linux-isolated-display.gif`、`macos-cua-background.gif`。

## 微博 / 即刻（短）

**A. 主推**
给 AI Agent 一双手：Kiseki Input，一个原生跨平台的桌面操作 CLI。键鼠、截图、宏、教学录制、后台操作，一套命令跑 Windows / macOS / Linux。后台模式下 Agent 在后台干活，不抢你的鼠标。WebUI 只能改配置，不能触发任何操作。https://github.com/win10ogod/kiseki-input

**B. 后台操作**
Agent 一动手就抢走你的鼠标，是 Computer Use 最扫兴的地方。Kiseki Input 通过 Cua Driver 做后台操作：Safari 一直在最前面，鼠标没动，另一个应用里已经点好了、字也打好了。#AIAgent #ComputerUse

**C. 教学录制**
教 Agent 一个操作流程，不用塞一整段视频：Kiseki 的教学录制保存“动作序列 + 关键帧 + 原生输入事件”，还自动生成 SKILL.md。Agent 读时间线，不读几百帧画面。

**D. 诚实**
一个自动化工具能做到的事，取决于平台、权限和目标应用。Kiseki 的做法是：`kiseki doctor` 和 `kiseki capabilities` 告诉你这台机器真正支持什么；做不到的，如实说做不到。

**E. 更新**
Kiseki Input 对齐 Cua Driver 0.38：browser select、guide、health、.app 路径启动……文档里有 0.31–0.38 的变化对照表。

## V2EX（节点：分享创造）

标题：**[开源] Kiseki Input：给 AI Agent 用的跨平台桌面操作 CLI（键鼠 / 截图 / 宏 / 教学录制 / 后台操作）**

正文：
做 Agent 的 Computer Use 时总在撞几堵墙：绑平台、依赖多、动手就抢鼠标、出问题不知道原因。我做了个小工具：用 C++ 写的原生 CLI，一套命令跑 Windows / macOS / Linux。

- 键鼠输入、桌面 / 窗口 / 连拍截图、窗口列表和检查、界面结构观察（UI Automation / Accessibility）
- JSON 宏：键鼠、拖拽、截图、等待，可驱动前台，也可驱动后台窗口
- 教学录制：动作序列 + 关键帧 + 原生事件，自动生成 SKILL.md，Agent 读时间线而不是整段视频
- 后台操作：通过 Cua Driver，不抢焦点、不动鼠标；Linux 可用 Xvfb 隔离桌面
- WebUI 只有读 / 写配置、读能力三个接口，不能触发任何操作
- `kiseki doctor` / `capabilities` / `modes` 告诉你这台机器能做什么、该选哪种模式

诚实说明：Windows 实测最多；Linux 面向 X11；macOS 分原生和 Cua 两条路径。最近对齐了 Cua Driver 0.38。欢迎提 Issue / PR。

https://github.com/win10ogod/kiseki-input

## 小红书（轻松版）

🖱️ 让 AI 帮你操作电脑，但不抢你的鼠标？

最近在折腾 Agent 的“手”，做了个开源小工具 Kiseki Input：
✅ 一套命令，Windows / macOS / Linux 通用
✅ 后台操作：你在前台干活，它在后台点按钮、打字
✅ 教学录制：把你的操作变成“动作序列 + 关键帧”，AI 看得懂也不费 token
✅ 安全边界：WebUI 只能改配置，不能触发任何操作
✅ 做不到的事会老实告诉你，不装懂

GitHub 搜 win10ogod/kiseki-input
#AI工具 #AIAgent #自动化 #开源 #效率工具

## 朋友圈 / 群里转发（一句话版）

我开源了一个给 AI Agent 用的桌面操作命令行 Kiseki Input：跨 Windows/macOS/Linux，能后台操作、不抢鼠标，还能把操作录成 Agent 看得懂的教学包。https://github.com/win10ogod/kiseki-input

## GitHub Release 中文说明模板

**Kiseki Input vX.Y.Z**

- 集成对齐 Cua Driver 0.38.0：`browser select`、`guide`、`health`；转发 `skills` / `history` / `perception` / `cursor` / `config`；macOS 上 `.app` 路径启动
- 文档：`docs/cua.md` 新增 0.31–0.38 变化对照；新增简体中文 README（`README.zh-CN.md`）
- 安装包：Windows x64 / macOS arm64 与 x86_64 / Linux x64 与 arm64，附 SHA-256 校验文件
