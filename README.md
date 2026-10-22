**中文** | [English](README.en.md)

# wolves.desktop

《The Wolf Among Us》（重制版）标题界面 / 主菜单花卉背景的动态复现：原生 Win32 + Direct3D11 程序（全屏 / 窗口 / 桌面壁纸三种显示模式）和 WebGL2 网页版。

## 下载

[Releases](https://github.com/Zaiyiiiiii/The-Wolf-Among-Us-Wallpaper/releases) 页面提供 `WolfAmongUsTitleLive.exe`（64 位 Windows）和 `WolfAmongUsWallpaper.apk`（Android 7.0 及以上的动态壁纸）。

## 使用

- 托盘图标右键菜单：场景（标题界面 / 主菜单）、播放速度、色调（原版 / 提亮 / 浅色）、显示模式（全屏 / 窗口 / 背景）、语言、随系统自动启动、退出。
- 快捷键（全屏 / 窗口模式）：Tab 切换场景，T / M 选场景，↑ / ↓ 调速度，F / F11 / Alt+Enter / 双击切换全屏与窗口，Esc 退出。
- 命令行：`--window`、`--wallpaper`、`--scene title|menu`、`--tone original|bright|light`、`--lang auto|zh|en|ja`、`--speed K`、`--seed N`、`--t 秒`（冻结）、`--log 文件`、`--dump 输出.bmp --size 宽x高`（离屏导出一帧）。
- 界面语言：简体中文 / English / 日本語，默认跟随系统界面语言（中文、日语以外用英文），可在托盘菜单的「语言 / Language」里切换。网页版按浏览器语言选择，`?lang=zh|en|ja` 可强制指定。
- 「随系统自动启动」写入当前用户的 `Run` 注册表项，开机后沿用上次的设置；exe 被移动后需重新勾选。
- Android：安装 APK 后打开应用会进入系统的动态壁纸预览；壁纸的设置页可选场景、色调、播放速度、帧率。界面语言跟随系统（简体中文 / English / 日本語）。竖屏、超宽屏会自动放大铺满。

## 从源码构建

需要 Visual Studio 的 C++ 桌面开发组件、Windows SDK、.NET SDK、Python。

### 1. 配置

复制 `.env.example` 为 `.env`。所有路径和可变项都在这里，相对路径按项目根目录解析，同名系统环境变量优先。

| 变量 | 用途 |
|---|---|
| `WOLF_GAME_DIR` | 已安装的游戏目录（重制版或重制版试玩版，含 `Archive/`） |
| `WOLF_TTK_PROJECT` | [TelltaleToolKit](https://github.com/iMrShadow/TelltaleToolKit) 源码中 `TelltaleToolKit.csproj` 的路径 |
| `WOLF_ASSETS_DIR`、`WOLF_BUILD_DIR`、`WOLF_EXE_NAME`、`WOLF_VS_PATH` | 贴图与图标目录、输出目录、exe 名、Visual Studio 位置（留空自动查找） |
| `WOLF_ANDROID_SDK`、`WOLF_JAVA_HOME` | 构建 Android 版：SDK 目录（需要 android-35 平台）、JDK 17 及以上（留空用 `JAVA_HOME`） |
| `WOLF_WEB_WALL_PNG`、`WOLF_WEB_SPOT_PNG`、`WOLF_WEB_OUT` | 网页版的输入与输出 |
| `WOLF_WALLPAPER_D3DTX`、`WOLF_SPOTLIGHT_D3DTX` | 手动提取贴图时的输入 |
| `WOLF_CAPTURE_DIR`、`WOLF_MENU_JSON`、`WOLF_DENSE_ROWS` | 分析脚本的输入，一般用不到 |

### 2. 准备贴图

贴图和图标不在仓库里，需要自行准备：

```
powershell -File tools\prepare-assets.ps1
```

脚本从游戏归档 `Archive/Fables_pc_Boot_ar_txmesh.ttarch2` 解出 `ui_menu_background_wallpaper.d3dtx`（2048×2048 BC3）和 `ui_menu_background_spotlight.d3dtx`（512×512 BC1），生成 `WOLF_ASSETS_DIR`（默认 `assets/`）下的 `wall.bc3`、`spot.bc1`。再在该目录放一个自己的 `app.ico`。

### 3. 构建

```
powershell -File native\build.ps1
```

输出 `native\build\WolfAmongUsTitleLive.exe`。

Android 动态壁纸（贴图用同一个 `WOLF_ASSETS_DIR` 里的 `wall.bc3`、`spot.bc1`，不需要 `app.ico`）：

```
powershell -File android\build.ps1
```

输出 `android\app\build\outputs\apk\release\app-release.apk`。

网页版：

```
python tools/d3dtx2png.py <wallpaper.d3dtx> 2048 2048 BC3 <WOLF_WEB_WALL_PNG 的路径>
python tools/d3dtx2png.py <spotlight.d3dtx> 512 512 BC1 <WOLF_WEB_SPOT_PNG 的路径>
python web/build_web.py
```

## 说明

- 壁纸模式面向 Windows 11 24H2 的桌面层结构；退出后只在桌面是单张图片时重设原壁纸，幻灯片壁纸只重绘桌面。
- 本项目与游戏版权方无关。游戏名称和美术资源的权利归其所有者。
