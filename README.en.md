**English** | [中文](README.md)

# wolves.desktop

A live recreation of the flower-pattern background from the title screen and main menu of *The Wolf Among Us* (Remastered): a native Win32 + Direct3D 11 program (fullscreen, windowed and desktop-wallpaper display modes) and a WebGL2 web version.

## Download

`WolfAmongUsTitleLive.exe` (64-bit Windows) is on the [Releases](https://github.com/Zaiyiiiiii/The-Wolf-Among-Us-Wallpaper/releases) page, along with `WolfAmongUsWallpaper.apk` (live wallpaper for Android 7.0 and later).

## Usage

- Tray icon right-click menu: scene (title screen / main menu), playback speed, tone (Original / Brightened / Light), display mode (Fullscreen / Windowed / Wallpaper), language, start with Windows, quit.
- Shortcuts (fullscreen / windowed): Tab switches scene, T / M select a scene, ↑ / ↓ change speed, F / F11 / Alt+Enter / double-click toggle fullscreen and windowed, Esc quits.
- Command line: `--window`, `--wallpaper`, `--scene title|menu`, `--tone original|bright|light`, `--lang auto|zh|en|ja`, `--speed K`, `--seed N`, `--t SECONDS` (freeze), `--log FILE`, `--dump OUT.bmp --size WIDTHxHEIGHT` (render one frame offscreen).
- Interface languages: Simplified Chinese / English / Japanese. The default follows the system UI language (English for anything other than Chinese or Japanese); change it in the tray menu under "语言 / Language". The web version follows the browser language; `?lang=zh|en|ja` forces one.
- "Start with Windows" writes to the current user's `Run` registry key and reuses your last settings after login; check it again if the exe is moved.
- Android: after installing the APK, opening the app takes you to the system's live-wallpaper preview; the wallpaper's settings page lets you pick scene, tone, playback speed and frame rate. The interface follows the system language (Simplified Chinese / English / Japanese). Portrait and ultrawide screens are zoomed in automatically to fill.

## Build from source

Requires the Visual Studio C++ desktop workload, the Windows SDK, the .NET SDK and Python.

### 1. Configure

Copy `.env.example` to `.env`. All paths and tunable values live there; relative paths are resolved against the project root, and an environment variable of the same name takes precedence.

| Variable | Purpose |
|---|---|
| `WOLF_GAME_DIR` | Directory of an installed game (Remastered or the Remastered Demo, containing `Archive/`) |
| `WOLF_TTK_PROJECT` | Path to `TelltaleToolKit.csproj` in a [TelltaleToolKit](https://github.com/iMrShadow/TelltaleToolKit) checkout |
| `WOLF_STEAM_USERNAME` | Without an installed game, the Steam account used to download the needed archive with [DepotDownloader](https://github.com/SteamRE/DepotDownloader); the password is typed in the terminal, never stored in `.env` |
| `WOLF_ASSETS_DIR`, `WOLF_BUILD_DIR`, `WOLF_EXE_NAME`, `WOLF_VS_PATH` | Textures-and-icon directory, output directory, exe name, Visual Studio location (leave empty to auto-detect) |
| `WOLF_ANDROID_SDK`, `WOLF_JAVA_HOME` | Android build: SDK directory (needs the android-35 platform) and JDK 17 or later (leave empty to use `JAVA_HOME`) |
| `WOLF_WEB_WALL_PNG`, `WOLF_WEB_SPOT_PNG`, `WOLF_WEB_OUT` | Inputs and output of the web version |
| `WOLF_WALLPAPER_D3DTX`, `WOLF_SPOTLIGHT_D3DTX` | Inputs when extracting the textures by hand |
| `WOLF_CAPTURE_DIR`, `WOLF_MENU_JSON`, `WOLF_DENSE_ROWS` | Inputs of the analysis scripts; usually not needed |

Anonymous accounts cannot download this demo (AppID 5280090); a regular Steam account is required (it is free; add the demo to your library).

### 2. Prepare the textures

The textures and the icon are not in the repository; prepare them yourself:

```
powershell -File tools\prepare-assets.ps1
```

The script extracts `ui_menu_background_wallpaper.d3dtx` (2048×2048 BC3) and `ui_menu_background_spotlight.d3dtx` (512×512 BC1) from the game archive `Archive/Fables_pc_Boot_ar_txmesh.ttarch2` and writes `wall.bc3` and `spot.bc1` under `WOLF_ASSETS_DIR` (default `assets/`). Then put an `app.ico` of your own in that directory.

### 3. Build

```
powershell -File native\build.ps1
```

Output: `native\build\WolfAmongUsTitleLive.exe`.

Android live wallpaper (uses `wall.bc3` and `spot.bc1` from the same `WOLF_ASSETS_DIR`; no `app.ico` needed):

```
powershell -File android\build.ps1
```

Output: `android\app\build\outputs\apk\release\app-release.apk`.

Web version:

```
python tools/d3dtx2png.py <wallpaper.d3dtx> 2048 2048 BC3 <path set in WOLF_WEB_WALL_PNG>
python tools/d3dtx2png.py <spotlight.d3dtx> 512 512 BC1 <path set in WOLF_WEB_SPOT_PNG>
python web/build_web.py
```

## Notes

- Wallpaper mode targets the desktop-layer structure of Windows 11 24H2. On exit it resets the original wallpaper only if the desktop was a single picture; for slideshow wallpapers it only redraws the desktop.
- This project is not affiliated with the game's rights holders. Rights to the game's name and art belong to their owners.
