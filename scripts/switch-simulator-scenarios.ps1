# 成员3 Windows 场景切换脚本：只启动 pile_simulator --scenario，不重置数据库。
# FastAPI 和大屏必须已经在跑。遥测走真实 C++ TCP，不在本脚本里改业务表。
param(
    [ValidateSet("normal", "charging", "overheat", "offline", "recover", "duplicate", "time-backward", "energy-backward", "unknown-pile")]
    [string]$Scenario = "normal",  # 单场景默认正常在线；名称与模拟器白名单一致。
    [switch]$AutoCycle,  # 打开后按 正常→充电→过热→离线→恢复 自动轮播。
    [string]$ServerHost = "127.0.0.1",  # 成员1服务端主机，本机答辩用 127.0.0.1。
    [int]$ServerPort = 48080,  # 正式 TCP 端口，连不上就失败，不造假成功。
    [int64]$PileId = 101,  # 种子库 pile_info.id=101。
    [ValidateRange(1000, 10000)]
    [int]$FrameIntervalMs = 5000,  # 帧间隔毫秒，传给 --interval-ms。
    [ValidateRange(0, 30)]
    [int]$PauseSeconds = 3  # 轮播时场景之间暂停，离线后另等 18 秒。
)

$ErrorActionPreference = "Stop"  # 任何命令失败立刻停，避免后面场景把现场搅乱。
$projectRoot = Split-Path -Parent $PSScriptRoot  # 脚本在 scripts/，上一级是仓库根。

# 只切换桩场景。C++ 服务端、FastAPI、大屏保持运行。
$simulatorCandidates = @(
    (Join-Path $projectRoot "build-member3-simulator\pile_simulator.exe"),  # 推荐构建目录。
    (Join-Path $projectRoot "b3req\pile_simulator.exe")  # 本机备用目录。
)
$simulator = $simulatorCandidates |
    Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } |
    Select-Object -First 1  # 用找到的第一个可执行文件。
if (-not $simulator) {
    throw "pile_simulator.exe was not found. Build simulator first."  # 没构建就不能切场景。
}

$mingwRuntimeCandidates = @()  # 收集可能含 libstdc++ 的目录。
$compiler = Get-Command g++.exe -ErrorAction SilentlyContinue  # PATH 里的 MinGW。
if ($compiler) {
    $mingwRuntimeCandidates += Split-Path -Parent $compiler.Source  # g++.exe 同目录通常有运行库。
}
foreach ($toolsRoot in @("D:\Qt6\Tools", "D:\Qt\Tools", "C:\Qt\Tools")) {  # 课程机常见 Qt 路径。
    if (Test-Path -LiteralPath $toolsRoot -PathType Container) {
        $mingwRuntimeCandidates += Get-ChildItem -LiteralPath $toolsRoot -Directory -Filter "mingw*" -ErrorAction SilentlyContinue |
            ForEach-Object { Join-Path $_.FullName "bin" }  # mingw*/bin 下才有 dll。
    }
}
$mingwRuntime = $mingwRuntimeCandidates |
    Where-Object {
        (Test-Path -LiteralPath (Join-Path $_ "libstdc++-6.dll") -PathType Leaf) -and
        (Test-Path -LiteralPath (Join-Path $_ "libgcc_s_seh-1.dll") -PathType Leaf)
    } |
    Select-Object -First 1  # 两个关键 dll 都在才算可用。
if ($mingwRuntime) {
    $env:PATH = "$mingwRuntime;$env:PATH"  # 让 pile_simulator.exe 能加载 MinGW 运行库。
}

$probe = [System.Net.Sockets.TcpClient]::new()  # 探测成员1端口，避免模拟器连上空端口还显示成功。
try {
    $connectTask = $probe.ConnectAsync($ServerHost, $ServerPort)
    if (-not $connectTask.Wait(2000) -or -not $probe.Connected) {
        throw "C++ server unreachable: $($ServerHost):$ServerPort"  # 服务端不在就停。
    }
} finally {
    $probe.Dispose()  # 探测套接字必须释放。
}

# 电表状态文件必须与 run-member3-stack.ps1 相同，否则累计读数会从 100.00 重来被服务端拒绝。
$databasePath = [System.IO.Path]::GetFullPath((Join-Path $projectRoot "runtime\charge_platform.sqlite"))  # 统一业务库。
$stateRoot = Join-Path $env:LOCALAPPDATA "charge-platform-member3"  # ASCII 路径，避开中文工程目录。
New-Item -ItemType Directory -Path $stateRoot -Force | Out-Null  # 目录不存在就建。
$pathBytes = [System.Text.Encoding]::UTF8.GetBytes($databasePath.ToLowerInvariant())  # 用库路径哈希区分不同库。
$pathHash = [System.BitConverter]::ToString(
    [System.Security.Cryptography.SHA256]::Create().ComputeHash($pathBytes)
).Replace("-", "").Substring(0, 12)
$stateFile = Join-Path $stateRoot "pile-$PileId-$pathHash.energy"  # 每桩每库一个电表文件。
$python = Join-Path $projectRoot "analytics\.venv\Scripts\python.exe"  # 只读查询用项目 venv。
$scenarios = if ($AutoCycle) {
    @("normal", "charging", "overheat", "offline", "recover")  # 答辩轮播五步。
} else {
    @($Scenario)  # 只跑用户指定的一个场景。
}

function Get-LatestMeterKwh {
    param([string]$DatabasePath, [int64]$PileId)
    if (-not (Test-Path -LiteralPath $python -PathType Leaf)) { return "100.00" }  # 没有 Python 就用默认起点。
    $value = & $python -c @"
import sqlite3, sys
database, pile = sys.argv[1], int(sys.argv[2])
connection = sqlite3.connect(f'file:{database}?mode=ro', uri=True)
row = connection.execute(
    'SELECT energy_kwh FROM pile_telemetry WHERE pile_id=? ORDER BY id DESC LIMIT 1',
    (pile,),
).fetchone()
print(row[0] if row and row[0] else '100.00')
"@ $DatabasePath $PileId  # URI mode=ro，绝不写订单表。
    return $value.Trim()
}

Write-Host "Scenario switch started. Dashboard refreshes live telemetry every 3 seconds."
foreach ($currentScenario in $scenarios) {
    Write-Host ""
    Write-Host ">>> scenario: $currentScenario"
    Set-Content -LiteralPath $stateFile -Value (Get-LatestMeterKwh $databasePath $PileId) -Encoding ascii  # 先把库内最新累计电表写入状态文件。
    Start-Sleep -Seconds 2  # 避免两场景同一秒时间戳冲突。
    & $simulator --pile $PileId --scenario $currentScenario `
        --connect "$($ServerHost):$ServerPort" --state-file $stateFile `
        --interval-ms $FrameIntervalMs  # 真 TCP，不是本地假 ACK。
    if ($LASTEXITCODE -ne 0) {
        throw "Scenario $currentScenario failed. Check the server response above."  # 合法场景失败立即停。
    }

    if ($AutoCycle -and $currentScenario -eq "offline") {
        Write-Host "Waiting 18 seconds for the server offline scan before recover."
        Start-Sleep -Seconds 18  # 服务端离线阈值 15 秒，多留 3 秒。
    } elseif ($AutoCycle -and $PauseSeconds -gt 0) {
        Start-Sleep -Seconds $PauseSeconds  # 给大屏 3 秒刷新留观察窗口。
    }
}

Write-Host ""
Write-Host "Scenario switch finished."

# 【文件讲解·答辩用】
# 功能作用：命令行统一切换九个模拟场景，不改源码、不重启 FastAPI。
# 技术原理：找到 pile_simulator.exe，探测 48080，只读 SQLite 校准累计电表，再 --connect 真服务端。
# 代码思路：单场景跑 -Scenario；-AutoCycle 跑五步；离线后等 18 秒再恢复。状态文件路径与一键启动脚本一致。
# 与其他文件关系：场景名来自 simulator/pile/scenario.cpp；电表语义与 main.cpp 相同；大屏只读刷新结果。
# 答辩时可以怎么讲：网页按钮坏了也可以用本脚本；它和按钮走同一套 --scenario，都不会直接 UPDATE 业务表。
