# 安裝 Kiseki Input

Kiseki 是供 agent 使用的原生桌面操作 CLI。你不需要編譯 C++ 就能使用安裝包；只有 skill 的 Python 輔助工具需要 Python 3.9+。

## 1. 下載適合的檔案

開啟 [Releases](https://github.com/win10ogod/kiseki-input/releases)，展開最新版本的 **Assets**。不要選 GitHub 自動附上的 Source code，它是原始碼。

| 系統 | 選擇 | 安裝後位置 |
| --- | --- | --- |
| Windows 10/11 x64 | `Windows-AMD64.exe` | 安裝精靈選定的目錄，通常 `C:\Program Files\Kiseki Input` |
| macOS 14+，Apple Silicon | `Darwin-arm64.pkg` | `/usr/local/bin/kiseki` |
| macOS 14+，Intel | `Darwin-x86_64.pkg` | `/usr/local/bin/kiseki` |
| Linux x64 | `Linux-x86_64.deb` | `/usr/bin/kiseki` |
| Linux arm64 | `Linux-aarch64.deb` | `/usr/bin/kiseki` |

檔名都帶有 `KisekiInput-版本-` 前綴。Linux DEB 在 Ubuntu 22.04 建置，適合 Ubuntu 22.04+ 或提供相容相依套件的 Debian 系統。其他 Linux 發行版可使用 TAR.GZ 並安裝對應執行期函式庫；發行版的 glibc 與 libstdc++ 仍需相容。Windows 提供 x64 安裝包；Windows on Arm 可依系統的 x64 相容能力使用，這不等同原生 arm64 建置。

每個安裝包旁都有 `.sha256` 校驗檔。Windows 用 `Get-FileHash .\檔名 -Algorithm SHA256`，macOS 用 `shasum -a 256 檔名`，Linux 用 `sha256sum -c 檔名.sha256`，比對下載內容。校驗檔用來檢查檔案完整性。

尚未發行正式版本時，可從 **Actions → Build and package → 成功的執行 → Artifacts** 下載測試安裝包。GitHub 下載 Actions artifact 需要登入；先解開外層 artifact ZIP，再使用裡面的安裝包。版本標籤 `vMAJOR.MINOR.PATCH` 通過全部五個建置目標後，工作流程才會把安裝包放到 Release。

## 2. 安裝

### Windows

1. 執行 `.exe`，確認目的位置。
2. 在 PATH 頁面選擇將 Kiseki 加入 PATH。依你選的安裝範圍，系統可能要求管理員確認。
3. 完成後開啟**新的 PowerShell**，執行 `kiseki --version`。

開始選單中的 **Start here - Installation and CUA setup** 會開啟隨附的離線引導。若找不到命令，可先使用實際安裝位置：

```powershell
& 'C:\Program Files\Kiseki Input\bin\kiseki.exe' --version
```

ZIP 是免安裝版本。解壓整個資料夾，從 `bin\kiseki.exe` 執行；不要只搬走 EXE，旁邊有執行期 DLL。Windows x64 發行包也包含 IbInputSimulator DLL；使用特定裝置驅動的前提仍由該驅動決定，可用 `kiseki capabilities` 檢查實際可用性。

目前工作流程不內建私人程式碼簽署憑證。若 Windows 顯示「未知的發行者」，先核對官方 Release 與 SHA-256；確認後依 Windows 提供的個別檔案確認流程執行，不需要停用全域保護。

### macOS

1. 開啟對應 CPU 的 `.pkg`。
2. 依 Installer 精靈完成安裝；系統安裝位置是 `/usr/local`。
3. 開啟 Terminal，執行 `kiseki --version`。

安裝包目前未配置 Developer ID 簽署及公證。若 macOS 阻止開啟，先核對官方 Release 與 SHA-256，再使用「系統設定 → 隱私權與安全性」對這個安裝包提供的個別開啟選項。不要關閉整個系統的 Gatekeeper。

原生 Kiseki 截圖／鍵鼠與 CuaDriver 的權限歸屬不同。請在實際要操作的登入桌面與 Terminal 中執行，並只依該功能的提示設定權限。

### Ubuntu / Debian

在安裝包所在目錄執行，把範例檔名換成實際下載的名稱：

```bash
sudo apt install ./KisekiInput-0.1.0-Linux-x86_64.deb
kiseki --version
```

使用 `apt install ./...deb` 可一併處理相依套件。Kiseki 的 Linux 功能包含 X11 原生輸入／觀察與可選的 Wayland Portal 截圖；CUA 在實際登入桌面中另行檢查 provider 的 session 能力。

### 使用 TAR.GZ 自選目錄

先解壓縮並查看內部結構。將內含的 `bin`、`share` 等資料夾放在同一個安裝前綴下，例如 `$HOME/.local`。TAR.GZ 的打包前綴依平台可能是 `usr` 或 `usr/local`；請將該前綴下的內容搬入你選的位置，不要打散 `bin` 與 `share/kiseki` 的關係。將所選前綴的 `bin` 加入 PATH。

## 3. 確認原生 CLI

```bash
kiseki --version
kiseki modes --json
kiseki capabilities
```

這些命令不會下載 CUA、啟動錄製或送出鍵鼠事件。功能偵測不等於每個應用程式都接受事件；實際操作要以該視窗的前後狀態確認。

## 4. 第一次使用 CUA

在**要操作的登入桌面**中開始一輪工作：

```bash
kiseki background cua setup
kiseki background cua status
kiseki background cua windows
```

`setup` 會安裝缺少的官方 Cua Driver、檢查更新並啟動原生 daemon。`windows` 與 `launch` 也會執行這個準備流程；純 `status` 是唯讀檢查。macOS 透過 CuaDriver.app 啟動，以維持應用程式的權限歸屬。

macOS 若需要授權：

```bash
kiseki background cua status --prompt
```

依系統提示，在「隱私權與安全性」中為 **CuaDriver** 開啟「輔助使用」與「螢幕錄製」，然後重新執行 setup / status。若第一次啟動等待逾時，原本的 daemon 可能仍在等權限；先完成提示、再检查狀態，不必重新安裝。

Windows 需在操作桌面的使用者 session 中執行。Linux 需有實際 X11/Wayland 圖形 session，並繼承正確的 DISPLAY、Wayland socket 與桌面 D-Bus 環境；SSH shell 成功不代表 GUI session 已連好。

### 自動更新的時機

- 在 `setup`、`windows`、`launch` 這些**開始／重新探索工作流程**的命令中，每 24 小時向官方檢查一次。
- 鍵鼠 action、state、screenshot 和原樣 tool call 不會在操作中啟動更新。更新後請重新觀察，不沿用先前的 element index／snapshot。
- 新安裝預設使用官方 stable 通道；現有的 stable／nightly 選擇會保留。
- 無網路或更新失敗時保留已安裝的 Driver，輸出包含 `warnings` 或標準錯誤中的原因。沒有可用 Driver 的首次安裝失敗會回傳非零退出碼。
- `KISEKI_CUA_DRIVER` 指定的精確 binary、`CUA_DRIVER_RS_VERSION` / `CUA_DRIVER_VERSION` 版本 pin，以及 `KISEKI_CUA_SOCKET` 自訂 daemon 都由其擁有者管理，不會被自動替換。

手動檢查、安裝可用更新或切換官方通道：

```bash
kiseki background cua update
kiseki background cua update --apply
kiseki background cua driver -- channel status --json
kiseki background cua driver -- channel set nightly
```

套用更新後在新工作流程先執行 setup。若 daemon 是你以自訂參數啟動的，請依原參數重新啟動，保留自己的設定。

本次跳過更新：`kiseki background cua setup --no-update`。長期停用自動更新可設定 `KISEKI_CUA_AUTO_UPDATE=0`（Bash 用 `export`；PowerShell 用 `$env:KISEKI_CUA_AUTO_UPDATE='0'`）。這不會阻止手動 `update --apply`。

## 5. 讓 agent 找到內建 skills

兩個 skill 位於安裝前綴的 `share/kiseki/skills`：

- `kiseki-project/SKILL.md`：定位執行檔、觀察視窗、精準操作及驗證。
- `kiseki-teach-recording/SKILL.md`：錄製、停止、檢查與生成教學 skill。

讓 agent 讀取所需 SKILL.md，或依該 agent 平台的安裝方式複製**整個 skill 資料夾**，保留 references / scripts。附帶的 Python helper 使用標準函式庫與 Python 3.9+；它不是 CLI 的執行期依賴。

需要未列在簡短 CLI 封裝中的 CUA 新工具時：

```bash
kiseki background cua tools
kiseki background cua describe get_window_state
kiseki background cua call get_window_state --file request.json
```

`call` 保留完整 JSON 參數與 provider 回覆。多步操作用同一個 `session`，取用本次 `state` 回傳的 `snapshot_id` 或 `element_token`。詳見 [CUA 整合](cua.md)。

## 更新或移除 Kiseki

Kiseki 的安裝包版本與 Cua Driver 的版本分開管理。更新 Kiseki 時安裝新版套件／替換完整免安裝目錄；CUA 更新由上述流程處理。

Windows 可從「設定 → 應用程式」移除 Kiseki；Linux DEB 用 `sudo apt remove kiseki-input`。macOS PKG 的檔案清單可由 `pkgutil --files io.github.win10ogod.kiseki-input.Runtime` 查詢，再依該清單移除 Kiseki 安裝的檔案；不要刪除整個 `/usr/local`。免安裝版本只需移除你建立的安裝資料夾。

移除 Kiseki 不會刪除教學錄製、使用者設定或獨立安裝的 Cua Driver。需要移除 CUA 時，依 [官方安裝／移除指南](https://cua.ai/docs/how-to-guides/driver/install) 操作。

## 回報問題

在 [Issues](https://github.com/win10ogod/kiseki-input/issues) 附上系統／CPU、安裝包名稱、`kiseki --version`、失敗命令及完整錯誤。CUA 問題再附 `background cua status` 與 `background cua driver -- --version`。請先檢查輸出是否包含不想公開的應用程式文字或視窗標題。
