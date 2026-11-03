# 60 秒宣传视频分镜（简体中文）

> 没时间做视频也没关系：下面的“录屏清单”大约 10 分钟能录完，剪成 60 秒；也可以把旁白稿交给任何配音 / AI 视频工具。
> 现成素材：`docs/assets/demos/` 里的 5 个 GIF（Notepad 输入、Paint 宏、心跳通知、Linux 隔离桌面、macOS Cua 后台）。

**画面比例** 16:9　**字幕** 简体中文，每条不超过 14 个字　**背景音乐** 轻快、无歌词

## 分镜

| 时间 | 画面 | 旁白 | 屏幕字幕 |
| --- | --- | --- | --- |
| 0–6 s | 一个终端；光标闪烁；窗口外一只鼠标被“抢走”飞出屏幕（可用后期动画，或直接用字幕代替） | 让 AI 操作电脑，最烦的是什么？它一动手，就抢走你的鼠标。 | AI 一动手，就抢走你的鼠标 |
| 6–14 s | 打出 `kiseki --version`，然后 `kiseki doctor`，显示这台机器的能力 | Kiseki Input，一个原生的桌面操作命令行，Windows、macOS、Linux 同一套命令。 | 一个命令，三个平台 |
| 14–26 s | 用 `notepad-unicode.gif` 的画面：命令行自动在记事本里输入中文 | 键盘、鼠标、截图、宏，都是一条命令。 | 键鼠 · 截图 · 宏 |
| 26–40 s | 用 `macos-cua-background.gif`：Safari 一直在最前面，另一个应用被后台点击、输入；鼠标位置不动 | 后台模式下，你在前台干活，Agent 在后台干活，不抢焦点，不动鼠标。 | 后台操作，不抢你的鼠标 |
| 40–50 s | 打开一个教学录制目录：`actions.json`、`keyframes/`、`SKILL.md`；高亮“动作序列 + 关键帧” | 教 Agent 一个流程，不用塞一整段视频，动作序列加关键帧就够了。 | 教学录制 → SKILL.md |
| 50–56 s | 浏览器打开本地 WebUI：只有配置页；终端里 `curl` 一个“触发操作”的地址得到 404 | WebUI 只能改配置，不能触发任何操作。边界，写在代码里。 | WebUI 只管配置 |
| 56–60 s | 仓库页：Star 按钮高亮；终端 `kiseki doctor` | Kiseki Input，开源，欢迎试用。 | github.com/win10ogod/kiseki-input |

## 旁白稿（连读，约 55 秒）

让 AI 操作电脑，最烦的是什么？它一动手，就抢走你的鼠标。
Kiseki Input，一个原生的桌面操作命令行，Windows、macOS、Linux 同一套命令。
键盘、鼠标、截图、宏，都是一条命令。
后台模式下，你在前台干活，Agent 在后台干活：不抢焦点，不动鼠标。
教 Agent 一个流程，不用塞一整段视频，动作序列加关键帧就够了。
WebUI 只能改配置，不能触发任何操作——边界，写在代码里。
Kiseki Input，开源，欢迎试用。

## 10 分钟录屏清单（每条录 10–20 秒）

1. **终端**：`kiseki --version`；`kiseki doctor`；`kiseki modes`
2. **截图**：`kiseki target list`；`kiseki screenshot desktop --output shot.bmp`；打开 `shot.bmp`
3. **宏**（Windows）：`kiseki macro run --file docs/assets/demos/demo-notepad-unicode.json`
4. **后台操作**（macOS）：`kiseki background cua setup`；`kiseki background cua windows`；对一个后台应用 `state` → `click` / `text`，镜头里保持另一个应用在前台
5. **教学录制**：`kiseki teach record --output demo-session --text "打开设置并保存"`，做几步操作，再运行一次 `kiseki teach record`；打开录制目录给一个镜头
6. **WebUI**：`kiseki config-ui`，浏览器里翻一下配置页；终端 `curl` 一个不存在的“操作”路径给 404 的镜头

## 封面与标题备选

- 《给 AI Agent 一双手：跨平台、不抢鼠标》
- 《Agent 操作电脑，凭什么要抢我的鼠标？》
- 《一个命令行，三个平台：Kiseki Input》
