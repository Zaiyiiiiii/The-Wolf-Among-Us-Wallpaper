# wolves.desktop

《The Wolf Among Us》（重制版）标题界面 / 主菜单花卉背景的动态复现：原生 Win32 + Direct3D11 程序（全屏 / 窗口 / 桌面壁纸三种显示模式），以及 WebGL2 网页版。

画面逻辑来自游戏自己的场景文件、动画曲线和着色器（`ui_menu_background_pause.scene` 为标题界面，`ui_menu_background_main.scene` 为主菜单），不是目测还原。标题界面与游戏自己的渲染结果逐像素比对过，平均误差约 0.1/255；主菜单场景和「浅色」色调没有做这种比对。

## 目录

| 路径 | 内容 |
|---|---|
| `native/main.cpp` | 程序主体：曲线求值（移植自引擎）、图层参数、D3D11 渲染、桌面壁纸嵌入、托盘菜单、显示模式 / 场景 / 速度 / 色调切换 |
| `native/ps.hlsl`、`vs.hlsl` | 像素着色器（六个图层按引擎顺序混合，层间 8 位取整；含「浅色」色调分支）和全屏三角形顶点着色器 |
| `native/app.rc`、`app.manifest` | 资源脚本、DPI 清单 |
| `native/build.ps1` | 构建脚本 |
| `web/template.html`、`build_web.py` | 网页版模板（仅标题界面场景）与内嵌脚本 |
| `tools/` | 复现时用的分析脚本：引擎关键帧求值的 Python 移植、场景 / 动画 / 着色器汇编的摘要脚本、贴图提取 |
| `assets/` | **需要自行准备**，见下 |

## 贴图不随仓库提供

程序用到两张游戏贴图，版权属于游戏版权方，所以仓库里没有，`.gitignore` 也已排除：

- `ui_menu_background_wallpaper.d3dtx`：2048×2048，BC3，花卉图案
- `ui_menu_background_spotlight.d3dtx`：512×512，BC1，聚光暗角

两张贴图都在同一个归档 `Archive/Fables_pc_Boot_ar_txmesh.ttarch2`（约 5 MB）里。准备步骤：

1. 复制 `.env.example` 为 `.env`，按需填写：
   - `WOLF_TTK_PROJECT`：自行 clone [TelltaleToolKit](https://github.com/iMrShadow/TelltaleToolKit) 后，其中 `TelltaleToolKit.csproj` 的路径（必填，需要 .NET SDK）。
   - `WOLF_GAME_DIR`：已安装的游戏目录（重制版或重制版试玩版，里面有 `Archive/`）。这是主要用法。
   - 没有安装游戏时，可改填 `WOLF_STEAM_USERNAME`，脚本会用 [DepotDownloader](https://github.com/SteamRE/DepotDownloader) 只下载上面那一个归档文件。**匿名下载不行**：我分别用 DepotDownloader 和 SteamCMD 的匿名登录试过，这个 demo（AppID 5280090）分别返回 “not available from this account” 和 “No subscription”，SteamCMD 的 `app_license_request` 对匿名账号提示 “Not for anonymous users”。所以需要一个普通 Steam 账号（免费，把 demo 加进库即可）；密码不写进 `.env`，首次运行时在终端输入，之后由 DepotDownloader 记住登录。这条路径没有实测。
2. 运行：
   ```
   powershell -File tools\prepare-assets.ps1
   ```
   它会（必要时先下载归档）解出两个 `.d3dtx`，生成 `WOLF_ASSETS_DIR`（默认 `assets/`）下的 `wall.bc3`、`spot.bc1`。已用本机的 demo 验证过这一段，结果与之前使用的数据逐字节一致。
3. 再在同一目录放一个自己的图标 `app.ico`（任意 .ico）。

只想手动处理时：自己解出两个 `.d3dtx`，在 `.env` 里填 `WOLF_WALLPAPER_D3DTX`、`WOLF_SPOTLIGHT_D3DTX`，运行 `python tools/extract_textures.py`。

## 配置（.env）

所有路径和可变项都放在项目根目录的 `.env` 里，不写进代码；`.env` 不进仓库，模板见 `.env.example`。相对路径按项目根目录解析，同名的系统环境变量优先于 `.env`。

| 变量 | 用途 |
|---|---|
| `WOLF_ASSETS_DIR`、`WOLF_BUILD_DIR`、`WOLF_EXE_NAME`、`WOLF_VS_PATH` | 构建：贴图与图标目录、输出目录、exe 名、Visual Studio 位置（留空自动查找） |
| `WOLF_WALLPAPER_D3DTX`、`WOLF_SPOTLIGHT_D3DTX` | 提取贴图的输入 |
| `WOLF_WEB_WALL_PNG`、`WOLF_WEB_SPOT_PNG`、`WOLF_WEB_OUT` | 网页版的输入与输出 |
| `WOLF_CAPTURE_DIR`、`WOLF_MENU_JSON`、`WOLF_DENSE_ROWS` | 分析脚本读取的抓帧导出数据（复现时才用） |

## 构建

原生程序（需要 Visual Studio 的 C++ 桌面开发组件和 Windows SDK）：

```
powershell -File native\build.ps1
```

默认输出 `native\build\WolfAmongUsTitleLive.exe`，只依赖系统自带的 DLL。

网页版（需要 Pillow 把 d3dtx 转成 PNG）：

```
python tools/d3dtx2png.py <wallpaper.d3dtx> 2048 2048 BC3 <WOLF_WEB_WALL_PNG 的路径>
python tools/d3dtx2png.py <spotlight.d3dtx> 512 512 BC1 <WOLF_WEB_SPOT_PNG 的路径>
python web/build_web.py
```

## 使用（原生程序）

- 托盘图标（所有模式都有）右键菜单：场景（标题界面 / 主菜单）、播放速度、色调（原版 / 提亮 / 浅色）、显示模式（全屏 / 窗口 / 背景）、退出。选择会记在 `HKCU\Software\WolfAmongUsTitleLive`。
- 快捷键（全屏 / 窗口模式）：Tab 切换场景，T / M 直接选场景，↑ / ↓ 调速度，F / F11 / Alt+Enter / 双击切换全屏与窗口，Esc 退出。
- 托盘菜单里的「随系统自动启动」可勾选：写入 `HKCU\Software\Microsoft\Windows\CurrentVersion\Run`（仅当前用户，不需要管理员权限），取消勾选即删除。启动命令只带 exe 路径，所以开机后沿用上次选的显示模式、场景、色调、速度和语言；exe 被移动后需要重新勾选一次。
- 命令行：`--window`、`--wallpaper`、`--scene title|menu`、`--tone original|bright|light`、`--lang auto|zh|en|ja`、`--speed K`、`--seed N`、`--t 秒`（冻结）、`--log 文件`、`--dump 输出.bmp --size 宽x高`（离屏导出一帧）。

## 多语言

- 原生程序的界面文字（托盘菜单、托盘提示、窗口标题、错误提示）有简体中文、English、日本語三套。默认「自动」：跟随系统界面语言，中文用中文、日语用日语，其余用英文。
- 托盘右键菜单的「语言 / Language」子菜单可手动切换，选择记在 `HKCU\Software\WolfAmongUsTitleLive\Lang`；切换后窗口标题和托盘提示立即更新，菜单下次打开时是新语言。`--lang` 只对本次运行生效，不会写入注册表。
- 网页版按浏览器语言选择（中文 / 日语 / 其余英文），URL 参数 `?lang=zh|en|ja` 可强制指定。
- 要加语言：在 `native/main.cpp` 的 `STR` 表里加一行并扩展 `LangSetting`，网页版在 `web/template.html` 开头的 `TEXT` 里加一项。README 本身目前只有中文。

## 实现要点

- 三个互相独立的动画叠加成无缝循环：整体旋转 300 秒一圈；近景 / 远景贴图用 Wrap 采样，UV 偏移分别在 90 / 120 秒内走整数个周期；颜色动画 20 秒。每个动画的速度（0.85~1.15 倍）和起始位置每次启动随机，与游戏的 `module_walk_animator` 一致。
- 引擎的关键帧插值（Knot=线性、Smooth=Catmull-Rom、Flat=零切线、Stepped=保持）按反汇编结果移植。
- 渲染目标是 8 位 UNORM，各层在伽马空间混合，每层结束取整，与游戏一致。
- 「提亮」是亮度倍率 2×（游戏主菜单亮度动画的峰值）；「浅色」是自制配色，不是游戏里的画面。

## 说明与限制

- `tools/` 里的分析脚本是复现过程中用的，读取的抓帧导出数据（RenderDoc 抓帧导出的 json / 文本）不随仓库提供，路径通过 `.env` 配置。
- `--wallpaper` 壁纸模式在 Windows 11 24H2 的桌面层结构下实现，退出后只在桌面是单张图片时重设原壁纸，幻灯片壁纸只重绘桌面。
- 本项目与游戏版权方无关。游戏名称和美术资源的权利归其所有者。
