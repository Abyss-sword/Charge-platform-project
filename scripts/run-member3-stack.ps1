param(
    [string]$DatabasePath = "",
    [string]$ServerHost = "127.0.0.1",
    [int]$ServerPort = 48080,
    [int64]$PileId = 101,
    [ValidateSet("normal","charging","overheat","offline","recover","duplicate","time-backward","energy-backward","unknown-pile")]
    [string]$Scenario = "normal",
    [switch]$SkipBrowser
)

$ErrorActionPreference = "Stop"
$projectRoot = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($DatabasePath)) {
    $DatabasePath = Join-Path $projectRoot "runtime\charge_platform.sqlite"
}
$DatabasePath = [System.IO.Path]::GetFullPath($DatabasePath)
if (-not (Test-Path -LiteralPath $DatabasePath -PathType Leaf)) {
    throw "统一数据库不存在：$DatabasePath。请先启动成员1的 C++ 服务端；本脚本不会重置或覆盖数据库。"
}

$python = Join-Path $projectRoot "analytics\.venv\Scripts\python.exe"
if (-not (Test-Path -LiteralPath $python -PathType Leaf)) {
    throw "缺少 analytics\.venv。请先按 README「首次准备」安装 Python 依赖。"
}
if (-not (Test-Path -LiteralPath (Join-Path $projectRoot "dashboard\node_modules") -PathType Container)) {
    throw "缺少 dashboard\node_modules。请先在 dashboard 目录执行 npm install。"
}

$simulatorCandidates = @(
    (Join-Path $projectRoot "build-member3-simulator\pile_simulator.exe"),
    (Join-Path $projectRoot "b3req\pile_simulator.exe")
)
$simulator = $simulatorCandidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
if (-not $simulator) {
    throw "未找到 pile_simulator.exe。请先执行：cmake -S simulator -B build-member3-simulator，再 cmake --build build-member3-simulator。"
}

# A MinGW-built executable needs the matching GCC runtime DLL directory on PATH.
# Discover common Qt installations so Windows PowerShell can launch the simulator
# without asking the user to copy DLLs into the repository.
$mingwRuntimeCandidates = @()
$compiler = Get-Command g++.exe -ErrorAction SilentlyContinue
if ($compiler) {
    $mingwRuntimeCandidates += Split-Path -Parent $compiler.Source
}
foreach ($toolsRoot in @("D:\Qt6\Tools", "D:\Qt\Tools", "C:\Qt\Tools")) {
    if (Test-Path -LiteralPath $toolsRoot -PathType Container) {
        $mingwRuntimeCandidates += Get-ChildItem -LiteralPath $toolsRoot -Directory -Filter "mingw*" -ErrorAction SilentlyContinue |
            ForEach-Object { Join-Path $_.FullName "bin" }
    }
}
$mingwRuntime = $mingwRuntimeCandidates |
    Where-Object {
        (Test-Path -LiteralPath (Join-Path $_ "libstdc++-6.dll") -PathType Leaf) -and
        (Test-Path -LiteralPath (Join-Path $_ "libgcc_s_seh-1.dll") -PathType Leaf)
    } |
    Select-Object -First 1
if (-not $mingwRuntime) {
    throw "未找到 MinGW 运行库。请把配套的 Qt Tools\mingw*_64\bin 加入 PATH。"
}
$env:PATH = "$mingwRuntime;$env:PATH"

function Test-LocalPort {
    param([int]$Port)
    $tcp = New-Object System.Net.Sockets.TcpClient
    try {
        $async = $tcp.BeginConnect("127.0.0.1", $Port, $null, $null)
        if (-not $async.AsyncWaitHandle.WaitOne(300, $false)) {
            return $false
        }
        $tcp.EndConnect($async)
        return $true
    } catch {
        return $false
    } finally {
        $tcp.Close()
    }
}

function Wait-LocalPort {
    param([int]$Port, [int]$TimeoutSec)
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    while ((Get-Date) -lt $deadline) {
        if (Test-LocalPort -Port $Port) {
            return $true
        }
        Start-Sleep -Milliseconds 300
    }
    return $false
}

$connection = Test-NetConnection -ComputerName $ServerHost -Port $ServerPort -WarningAction SilentlyContinue
if (-not $connection.TcpTestSucceeded) {
    throw "成员1 C++ 服务端不可达：$($ServerHost):$ServerPort。请先启动服务端；本脚本不会用假成功代替。"
}

$env:CHARGE_PLATFORM_DB = $DatabasePath
$runtime = Join-Path $projectRoot "runtime\member3"
New-Item -ItemType Directory -Path $runtime -Force | Out-Null
$apiOut = Join-Path $runtime "analytics.out.log"
$apiErr = Join-Path $runtime "analytics.err.log"
$webOut = Join-Path $runtime "dashboard.out.log"
$webErr = Join-Path $runtime "dashboard.err.log"

$apiArgs = @("-m", "uvicorn", "src.api:app", "--host", "127.0.0.1", "--port", "8000")
$pidFile = Join-Path $runtime "stack.pids"
$api = $null
$web = $null
$startedThisRun = $false
$alreadyRunning = (Test-LocalPort -Port 8000) -and (Test-LocalPort -Port 4173)

try {
    if ($alreadyRunning) {
        Write-Host "FastAPI 和大屏已在运行，不再重复启动。"
    } else {
        $api = Start-Process -FilePath $python -ArgumentList $apiArgs -WorkingDirectory (Join-Path $projectRoot "analytics") -WindowStyle Hidden -PassThru -RedirectStandardOutput $apiOut -RedirectStandardError $apiErr
        $web = Start-Process -FilePath "npm.cmd" -ArgumentList @("run", "dev") -WorkingDirectory (Join-Path $projectRoot "dashboard") -WindowStyle Hidden -PassThru -RedirectStandardOutput $webOut -RedirectStandardError $webErr
        $startedThisRun = $true
        @($api.Id, $web.Id) | Set-Content -LiteralPath $pidFile -Encoding ASCII
        if (-not (Wait-LocalPort -Port 8000 -TimeoutSec 20)) {
            throw "FastAPI 未在 8000 端口就绪。请查看 runtime\member3\analytics.err.log。"
        }
        if (-not (Wait-LocalPort -Port 4173 -TimeoutSec 25)) {
            throw "Web 大屏未在 4173 端口就绪。请查看 runtime\member3\dashboard.err.log。"
        }
        if ($api.HasExited) {
            throw "FastAPI 启动后立即退出。请查看 runtime\member3\analytics.err.log。"
        }
        if ($web.HasExited) {
            throw "Web 大屏启动后立即退出。请查看 runtime\member3\dashboard.err.log。"
        }
    }

    Write-Host "FastAPI：http://127.0.0.1:8000/dashboard"
    Write-Host "Web大屏：http://127.0.0.1:4173/"
    Write-Host "统一数据库：$DatabasePath"
    Write-Host "模拟场景：$Scenario / pileId=$PileId / server=$($ServerHost):$ServerPort"

    if (-not $SkipBrowser) {
        Start-Process "http://127.0.0.1:4173/"
        Write-Host "已打开浏览器。顶栏应显示「SQLite已连接 · 3秒刷新」。"
    }

    # Keep the native C++ state path ASCII-only. MinGW std::filesystem cannot
    # reliably convert an absolute path containing Chinese characters.
    $stateRoot = Join-Path $env:LOCALAPPDATA "charge-platform-member3"
    New-Item -ItemType Directory -Path $stateRoot -Force | Out-Null
    $pathBytes = [System.Text.Encoding]::UTF8.GetBytes($DatabasePath.ToLowerInvariant())
    $pathHash = [System.BitConverter]::ToString(
        [System.Security.Cryptography.SHA256]::Create().ComputeHash($pathBytes)
    ).Replace("-", "").Substring(0, 12)
    $stateFile = Join-Path $stateRoot "pile-$PileId-$pathHash.energy"
    & $simulator --pile $PileId --scenario $Scenario --connect "$($ServerHost):$ServerPort" --state-file $stateFile
    if ($LASTEXITCODE -ne 0) {
        throw "模拟器场景失败。请查看上方真实服务端返回，不要把失败当成成功。"
    }
    Write-Host "默认场景已发送。关闭本窗口不会停止 FastAPI 和大屏；请双击 stop-member3.bat 停止。"
} catch {
    if ($startedThisRun) {
        foreach ($startedProcess in @($api, $web)) {
            if ($startedProcess -and -not $startedProcess.HasExited) {
                & taskkill.exe /PID $startedProcess.Id /T /F 2>$null | Out-Null
            }
        }
        Remove-Item -LiteralPath $pidFile -Force -ErrorAction SilentlyContinue
    }
    throw
}
