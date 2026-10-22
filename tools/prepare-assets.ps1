# 准备构建用的贴图：必要时用 DepotDownloader 只下载包含两张贴图的那一个归档文件，再解出 d3dtx 并生成 assets/*.bc3|bc1。
# 用法：powershell -File tools\prepare-assets.ps1
# 配置全部来自 .env（见 .env.example）。app.ico 需要自行放进 WOLF_ASSETS_DIR。
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'load-env.ps1')

$gameDir  = Resolve-Cfg (Get-Cfg 'WOLF_GAME_DIR' 'tools/.cache/game')
$archive  = Get-Cfg 'WOLF_ARCHIVE_FILE' 'Archive/Fables_pc_Boot_ar_txmesh.ttarch2'
$cache    = Resolve-Cfg (Get-Cfg 'WOLF_CACHE_DIR' 'tools/.cache')
$archivePath = Join-Path $gameDir $archive

# ---------- 1. 取得归档文件 ----------
if (-not (Test-Path $archivePath)) {
    $appId   = Get-Cfg 'WOLF_STEAM_APPID' '5280090'
    $user    = Get-Cfg 'WOLF_STEAM_USERNAME'
    $ddVer   = Get-Cfg 'WOLF_DEPOTDOWNLOADER_VERSION' '3.4.0'
    $ddDir   = Resolve-Cfg (Get-Cfg 'WOLF_DEPOTDOWNLOADER_DIR' 'tools/.cache/depotdownloader')
    $ddExe   = Join-Path $ddDir 'DepotDownloader.exe'
    if (-not (Test-Path $ddExe)) {
        New-Item -ItemType Directory -Force $ddDir | Out-Null
        $zip = Join-Path $ddDir 'dd.zip'
        $url = "https://github.com/SteamRE/DepotDownloader/releases/download/DepotDownloader_$ddVer/DepotDownloader-windows-x64.zip"
        Write-Host "下载 DepotDownloader $ddVer ..."
        Invoke-WebRequest $url -OutFile $zip
        Expand-Archive $zip $ddDir -Force
        Remove-Item $zip
    }
    if (-not $user) {
        throw "游戏目录里没有 $archivePath 。Steam 的 demo 不允许匿名下载，请在 .env 里设置 WOLF_STEAM_USERNAME（该账号需已把 demo 加入库）；或把 WOLF_GAME_DIR 指向已安装的游戏目录。"
    }
    $listFile = Join-Path $cache 'filelist.txt'
    New-Item -ItemType Directory -Force $cache | Out-Null
    ($archive -replace '\\', '/') | Set-Content $listFile -Encoding ascii        # 只下载这一个文件
    Write-Host "下载 app $appId 中的 $archive（首次需在下面输入 Steam 密码 / 令牌，之后用 -remember-password 记住）"
    & $ddExe -app $appId -username $user -remember-password -filelist $listFile -dir $gameDir
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path $archivePath)) { throw 'DepotDownloader 没有取得目标文件，见上方输出。' }
}

# ---------- 2. 构建并运行 ttk_extract，解出两个 d3dtx ----------
$ttk = Get-Cfg 'WOLF_TTK_PROJECT'
if (-not $ttk) { throw '请在 .env 里设置 WOLF_TTK_PROJECT：TelltaleToolKit 源码中 TelltaleToolKit.csproj 的路径（https://github.com/iMrShadow/TelltaleToolKit）。' }
$ttk = Resolve-Cfg $ttk
$tool = Join-Path $PSScriptRoot 'ttk_extract'
$toolBin = Join-Path $cache 'ttk_extract_bin'
dotnet build "$tool\ttk_extract.csproj" -c Release -o $toolBin "-p:TtkProject=$ttk" "-p:TargetFrameworks=net9.0" --nologo -v q
if ($LASTEXITCODE -ne 0) { throw 'ttk_extract 构建失败。' }

$wallName = Get-Cfg 'WOLF_WALLPAPER_NAME' 'ui_menu_background_wallpaper.d3dtx'
$spotName = Get-Cfg 'WOLF_SPOTLIGHT_NAME' 'ui_menu_background_spotlight.d3dtx'
$d3dtxDir = Join-Path $cache 'd3dtx'
$env:WOLF_GAME_PROFILE = Get-Cfg 'WOLF_GAME_PROFILE' 'the-wolf-among-us-2013'
Push-Location $toolBin                                  # TelltaleToolKit 要从自己的目录读取 ttk-data
try { dotnet (Join-Path $toolBin 'ttk_extract.dll') $archivePath $d3dtxDir $wallName $spotName } finally { Pop-Location }
if ($LASTEXITCODE -ne 0) { throw '解出 d3dtx 失败。' }

# ---------- 3. 生成 assets 里的 .bc3 / .bc1 ----------
$env:WOLF_WALLPAPER_D3DTX = Join-Path $d3dtxDir $wallName
$env:WOLF_SPOTLIGHT_D3DTX = Join-Path $d3dtxDir $spotName
python (Join-Path $PSScriptRoot 'extract_textures.py')
if ($LASTEXITCODE -ne 0) { throw 'extract_textures.py 失败。' }
Write-Host '完成。别忘了在 WOLF_ASSETS_DIR 里放 app.ico（任意 .ico）。'
