$ErrorActionPreference = "Stop"
$projectRoot = Split-Path -Parent $PSScriptRoot
$pidFile = Join-Path $projectRoot "runtime\member3\stack.pids"
if (-not (Test-Path -LiteralPath $pidFile -PathType Leaf)) {
    Write-Host "没有记录到正在运行的成员3后台进程。"
    exit 0
}
foreach ($processId in Get-Content -LiteralPath $pidFile) {
    if ($processId -match '^\d+$') {
        & taskkill.exe /PID ([int]$processId) /T /F 2>$null | Out-Null
    }
}
Remove-Item -LiteralPath $pidFile -Force
Write-Host "已停止成员3 FastAPI 和 Web 大屏。C++ 服务端和统一数据库未被修改。"
