# 构建原生 exe：着色器(fxc) -> 资源(rc) -> 编译链接(cl)。需要 Visual Studio（含 C++ 桌面开发）与 Windows SDK。
# 用法：powershell -File native\build.ps1
# 配置来自项目根目录的 .env（见 .env.example）：WOLF_ASSETS_DIR、WOLF_BUILD_DIR、WOLF_EXE_NAME、WOLF_VS_PATH
$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
. (Join-Path $here '..\tools\load-env.ps1')      # 提供 Get-Cfg / Resolve-Cfg：系统环境变量 > .env > 默认值

$assets = Resolve-Cfg (Get-Cfg 'WOLF_ASSETS_DIR' 'assets')
$build  = Resolve-Cfg (Get-Cfg 'WOLF_BUILD_DIR' 'native/build')
$exe    = Get-Cfg 'WOLF_EXE_NAME' 'WolfAmongUsTitleLive.exe'
$vs     = Get-Cfg 'WOLF_VS_PATH' ''

foreach ($f in 'wall.bc3', 'spot.bc1', 'app.ico') {
    if (-not (Test-Path (Join-Path $assets $f))) { throw "缺少 $assets\$f ，请先按 README.md 准备（贴图不随仓库提供）。" }
}
if (-not $vs) {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    $vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
}
if (-not $vs) { throw '没有找到带 C++ 工具集的 Visual Studio；可在 .env 里设置 WOLF_VS_PATH。' }
$vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvarsall.bat'

New-Item -ItemType Directory -Force $build | Out-Null
$buildSlash = ($build -replace '\\', '/') + '/'     # cl 的 /Fo 以斜杠结尾表示目录；避免反斜杠与引号相邻
Set-Location $here
# rc 通过 /i 在 assets 与 build 目录中查找 app.rc 里的文件名
$cmd = @(
    "call `"$vcvars`" x64 >nul",
    "fxc /nologo /T ps_5_0 /E main /O3 /Fo `"$build\ps.cso`" ps.hlsl",
    "fxc /nologo /T vs_5_0 /E main /O3 /Fo `"$build\vs.cso`" vs.hlsl",
    "rc /nologo /i `"$assets`" /i `"$build`" /fo `"$build\app.res`" app.rc",
    "cl /nologo /utf-8 /O2 /MT /EHsc /std:c++17 /W3 /Fo:`"$buildSlash`" main.cpp `"$build\app.res`" /Fe:`"$build\$exe`" /link /SUBSYSTEM:WINDOWS user32.lib gdi32.lib shell32.lib advapi32.lib d3d11.lib dxgi.lib"
) -join ' && '
cmd /c $cmd
if ($LASTEXITCODE -ne 0) { throw "构建失败（退出码 $LASTEXITCODE）" }
Write-Host "完成：$build\$exe"
