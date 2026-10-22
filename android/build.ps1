# 构建 Android 动态壁纸 APK。
# 用法：powershell -File android\build.ps1 [-Task assembleRelease|assembleDebug]
# 配置来自项目根目录的 .env（见 .env.example）：WOLF_ANDROID_SDK、WOLF_JAVA_HOME（可选）、WOLF_ASSETS_DIR（贴图目录，与 exe 共用）
param([string]$Task = 'assembleRelease')
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '..\tools\load-env.ps1')

$sdk = Get-Cfg 'WOLF_ANDROID_SDK' $env:ANDROID_HOME
if (-not $sdk) { throw '请在 .env 里设置 WOLF_ANDROID_SDK（Android SDK 目录），或设置环境变量 ANDROID_HOME。' }
$env:ANDROID_HOME = Resolve-Cfg $sdk
$javaHome = Get-Cfg 'WOLF_JAVA_HOME'
if ($javaHome) { $env:JAVA_HOME = Resolve-Cfg $javaHome }
if (-not $env:JAVA_HOME) { throw '请在 .env 里设置 WOLF_JAVA_HOME（JDK 17 或更高），或设置环境变量 JAVA_HOME。' }

Set-Location $PSScriptRoot
& (Join-Path $PSScriptRoot 'gradlew.bat') $Task --no-daemon
if ($LASTEXITCODE -ne 0) { throw "构建失败（退出码 $LASTEXITCODE）" }

$variant = if ($Task -match 'Debug') { 'debug' } else { 'release' }
$apk = Get-ChildItem (Join-Path $PSScriptRoot "app\build\outputs\apk\$variant") -Filter *.apk | Select-Object -First 1
Write-Host "完成：$($apk.FullName)"
