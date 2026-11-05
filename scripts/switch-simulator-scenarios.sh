#!/usr/bin/env bash
# 成员3 Ubuntu 场景切换：只启动 pile_simulator --scenario，不重置数据库。
# 服务端、FastAPI、Web 大屏必须已经在跑。离线场景后等待 18 秒再恢复。
set -euo pipefail  # 未定义变量、管道失败、普通失败都立刻退出。

project_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"  # 仓库根目录。
server_host="${CHARGE_SERVER_HOST:-127.0.0.1}"  # 可用环境变量覆盖，默认本机。
server_port="${CHARGE_SERVER_PORT:-48080}"  # 成员1正式 TCP 端口。
pile_id="${CHARGE_PILE_ID:-101}"  # 种子库电桩主键。
frame_interval_ms="${CHARGE_FRAME_INTERVAL_MS:-5000}"  # 传给 --interval-ms。
pause_seconds="${CHARGE_SCENARIO_PAUSE_SECONDS:-3}"  # 轮播场景之间暂停。
requested="${1:-normal}"  # 第一个参数：场景名或 --cycle。

# 只切换模拟场景；服务端、FastAPI 与 Web 大屏必须已经启动。
simulator="$project_root/build-member3-simulator/pile_simulator"  # Linux 无 .exe 后缀。
if [[ ! -x "$simulator" ]]; then
    cmake -S "$project_root/simulator" -B "$project_root/build-member3-simulator"  # 没有可执行文件就现场构建。
    cmake --build "$project_root/build-member3-simulator" --parallel
fi

if ! timeout 2 bash -c "</dev/tcp/$server_host/$server_port" 2>/dev/null; then
    echo "成员1服务端不可达：$server_host:$server_port" >&2  # 连不上就失败，不造假 ACK。
    exit 2
fi

case "$requested" in
    --cycle) scenarios=(normal charging overheat offline recover) ;;  # 答辩五步轮播。
    normal|charging|overheat|offline|recover|duplicate|time-backward|energy-backward|unknown-pile)
        scenarios=("$requested")  # 白名单单场景。
        ;;
    *)
        echo "用法：$0 [--cycle|normal|charging|overheat|offline|recover|duplicate|time-backward|energy-backward|unknown-pile]" >&2
        exit 2  # 未知名称直接拒绝，避免改源码才换场景。
        ;;
esac

state_file="$project_root/runtime/pile-$pile_id.energy"  # Ubuntu 用电表文件；Windows 脚本用 LOCALAPPDATA。
python_bin="$project_root/analytics/.venv/bin/python"  # 项目 venv，只读查最新累计电量。
database_path="$project_root/runtime/charge_platform.sqlite"  # 与服务端同一库。
if [[ -x "$python_bin" && -f "$database_path" ]]; then
    meter_start="$("$python_bin" -c 'import sqlite3,sys
database,pile=sys.argv[1],int(sys.argv[2])
connection=sqlite3.connect(f"file:{database}?mode=ro", uri=True)
row=connection.execute("SELECT energy_kwh FROM pile_telemetry WHERE pile_id=? ORDER BY id DESC LIMIT 1",(pile,)).fetchone()
print(row[0] if row and row[0] else "100.00")' "$database_path" "$pile_id")"  # URI 只读。
    printf '%s\n' "$meter_start" > "$state_file"  # 先校准，避免电量倒退被拒。
fi

echo "场景切换已开始；Web 大屏会按 3 秒周期读取最新入库遥测。"
for scenario in "${scenarios[@]}"; do
    echo
    echo ">>> 场景：$scenario"
    if [[ -x "$python_bin" && -f "$database_path" ]]; then
        meter_start="$("$python_bin" -c 'import sqlite3,sys
database,pile=sys.argv[1],int(sys.argv[2])
connection=sqlite3.connect(f"file:{database}?mode=ro", uri=True)
row=connection.execute("SELECT energy_kwh FROM pile_telemetry WHERE pile_id=? ORDER BY id DESC LIMIT 1",(pile,)).fetchone()
print(row[0] if row and row[0] else "100.00")' "$database_path" "$pile_id")"
        printf '%s\n' "$meter_start" > "$state_file"  # 每个场景开始前再读一次最新电表。
    fi
    sleep 2  # 避免同一秒时间戳冲突。
    "$simulator" --pile "$pile_id" --scenario "$scenario" \
        --connect "$server_host:$server_port" --state-file "$state_file" \
        --interval-ms "$frame_interval_ms"  # 真 TCP 写库。

    if [[ "$requested" == "--cycle" && "$scenario" == "offline" ]]; then
        echo "等待 18 秒，让服务端离线扫描触发，再自动恢复。"
        sleep 18  # 离线阈值 15 秒，多留余量。
    elif [[ "$requested" == "--cycle" && "$pause_seconds" -gt 0 ]]; then
        sleep "$pause_seconds"  # 给大屏刷新留观察窗口。
    fi
done

echo
echo "场景切换完成。"

# 【文件讲解·答辩用】
# 功能作用：Ubuntu 上用命令参数切换九个模拟场景，不改 C++ 源码。
# 技术原理：探测 /dev/tcp 端口；只读 SQLite 取最新 energy_kwh；再调用 pile_simulator --scenario --connect。
# 代码思路：--cycle 跑五步；单参数跑白名单场景；离线后 sleep 18；缺二进制就 cmake 构建。
# 与其他文件关系：场景序列来自 simulator/pile/scenario.cpp；Windows 对应 switch-simulator-scenarios.ps1。
# 答辩时可以怎么讲：网页按钮和本脚本是同一入口的两种操作面，都不会 SQL 改订单。
