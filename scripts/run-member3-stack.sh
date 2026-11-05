#!/usr/bin/env bash
set -euo pipefail

project_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"
database_path="${CHARGE_PLATFORM_DB:-$project_root/runtime/charge_platform.sqlite}"
server_host="${CHARGE_SERVER_HOST:-192.168.41.128}"
server_port="${CHARGE_SERVER_PORT:-48080}"
pile_id="${CHARGE_PILE_ID:-101}"
scenario="${CHARGE_SCENARIO:-normal}"

if [[ ! -f "$database_path" ]]; then
    echo "统一数据库不存在：$database_path。先启动成员1服务端；本脚本不会重置数据库。" >&2
    exit 2
fi
if ! timeout 2 bash -c "</dev/tcp/$server_host/$server_port" 2>/dev/null; then
    echo "成员1服务端不可达：$server_host:$server_port；不会使用假成功。" >&2
    exit 2
fi

python_bin="$project_root/analytics/.venv/bin/python"
[[ -x "$python_bin" ]] || { echo "缺少 analytics/.venv，请先安装依赖。" >&2; exit 2; }
[[ -d "$project_root/dashboard/node_modules" ]] || { echo "缺少 dashboard/node_modules，请先执行 npm install。" >&2; exit 2; }

simulator="$project_root/build-member3-simulator/pile_simulator"
if [[ ! -x "$simulator" ]]; then
    cmake -S "$project_root/simulator" -B "$project_root/build-member3-simulator"
    cmake --build "$project_root/build-member3-simulator" --parallel
fi

wait_port() {
    local port="$1"
    local tries=0
    while (( tries < 80 )); do
        if timeout 1 bash -c "</dev/tcp/127.0.0.1/$port" 2>/dev/null; then
            return 0
        fi
        sleep 0.25
        tries=$((tries + 1))
    done
    return 1
}

export CHARGE_PLATFORM_DB="$database_path"
"$python_bin" -m uvicorn src.api:app --app-dir "$project_root/analytics" --host 127.0.0.1 --port 8000 &
api_pid=$!
npm --prefix "$project_root/dashboard" run dev &
web_pid=$!
cleanup() {
    kill "$api_pid" "$web_pid" 2>/dev/null || true
}
trap cleanup EXIT INT TERM

if ! wait_port 8000; then
    echo "FastAPI 未在 8000 端口就绪。" >&2
    exit 2
fi
if ! wait_port 4173; then
    echo "Web 大屏未在 4173 端口就绪。" >&2
    exit 2
fi

echo "FastAPI：http://127.0.0.1:8000/dashboard"
echo "Web大屏：http://127.0.0.1:4173/"
echo "模拟场景：$scenario / pileId=$pile_id / server=$server_host:$server_port"
if command -v xdg-open >/dev/null 2>&1; then
    xdg-open "http://127.0.0.1:4173/" >/dev/null 2>&1 || true
fi
"$simulator" --pile "$pile_id" --scenario "$scenario" --connect "$server_host:$server_port" --state-file "$project_root/runtime/pile-$pile_id.energy"
echo "默认场景已发送。关闭本终端会停止 FastAPI 和大屏。"
wait

