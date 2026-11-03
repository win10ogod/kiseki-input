# 简体中文宣传素材

| 文件 | 用途 |
| --- | --- |
| [`../../../README.zh-CN.md`](../../../README.zh-CN.md) | 仓库的简体中文 README（GitHub 首页入口） |
| [`launch-post.md`](launch-post.md) | 发布长文：掘金 / 知乎 / V2EX / 公众号 / GitHub Discussions |
| [`social-posts.md`](social-posts.md) | 微博、即刻、V2EX、小红书、朋友圈、Release 说明的短文案 |
| [`video-storyboard.md`](video-storyboard.md) | 60 秒视频分镜 + 旁白稿 + 10 分钟录屏清单 |

## 可以放心说的事实（都能在仓库里查到）

- 原生 C++ CLI；Windows / macOS / Linux；CI 构建并测试五个目标（Windows x64、macOS arm64 / x86_64、Linux x64 / arm64）
- 键鼠输入、桌面 / 窗口 / 连拍截图、窗口列表与检查、非视觉界面观察
- JSON 宏；教学录制（动作序列 + 关键帧 + 原生事件 + `SKILL.md`）
- 后台操作经由可选的 Cua Driver（`kiseki background cua ...`）；Linux 的 Xvfb 隔离桌面
- WebUI 只有读配置、写配置、读能力三个接口，不能触发操作
- `doctor` / `capabilities` / `modes` 自检；两个 Agent 技能
- 当前对齐 Cua Driver 0.38.0
- MIT 许可证（仅覆盖 Kiseki 自身代码）

## 说的时候要小心的事

- **不要写“所有平台完全一致”。** 仓库自己写的是：Windows 实测最多；Linux 面向 X11 会话，Wayland 另需后端；macOS 分原生与 Cua 两条路径，权限归属不同。
- **不要写“绕过检测”之类的话。** README 明确说驱动级输入“不是绕过机制”。
- **许可证**：MIT，只覆盖 Kiseki Input 自身的代码，**不含第三方依赖**（CLI11、nlohmann/json、cpp-httplib、Catch2、IbInputSimulator.dll、Cua Driver 各用各的许可证）。可以说“MIT 开源”，不要说“整个安装包都是 MIT”。
- 第三方名称（Cua Driver、IbInputSimulator、Agivar）只作事实陈述，不要暗示它们的背书。
