# 读取项目根目录的 .env，提供 Get-Cfg / Resolve-Cfg，供各 PowerShell 脚本 dot-source 使用。
# 优先级：系统环境变量 > .env > 默认值。相对路径按项目根目录解析。
$script:Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$script:DotEnv = @{}
$envFile = Join-Path $script:Root '.env'
if (Test-Path $envFile) {
    foreach ($line in Get-Content $envFile -Encoding UTF8) {
        $l = $line.Trim()
        if ($l -eq '' -or $l.StartsWith('#') -or -not $l.Contains('=')) { continue }
        $k, $v = $l.Split('=', 2)
        $script:DotEnv[$k.Trim()] = $v.Trim().Trim('"').Trim("'")
    }
}
function Get-Cfg([string]$name, [string]$default = '') {
    $v = [Environment]::GetEnvironmentVariable($name)
    if (-not $v) { $v = $script:DotEnv[$name] }
    if (-not $v) { $v = $default }
    return $v
}
function Resolve-Cfg([string]$p) { if ([IO.Path]::IsPathRooted($p)) { $p } else { Join-Path $script:Root $p } }
