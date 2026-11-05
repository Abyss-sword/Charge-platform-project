#!/usr/bin/env bash
# 第一阶段「全部服务一键启动」脚本（VM / Ubuntu）
#
# 把第一阶段需要的服务全部拉起来，之后由你手动打开界面测试；脚本不跑任何断言。
#
# 启动内容：
#   server     C++ 业务服务端       127.0.0.1:48080   （必需）
#   analytics  FastAPI 只读分析服务  127.0.0.1:8000
#   dashboard  Web 大屏（Vite dev）  127.0.0.1:4173
#   user       Qt 车主端窗口
#   admin      Qt 运营端窗口
#   simulator  桩模拟器（默认 normal 场景发一轮遥测，可选）
#
# 用法:
#   bash scripts/stage1-services.sh up                 # 启动全部
#   bash scripts/stage1-services.sh up --no-web        # 只起 C++服务端 + Qt双端 + 模拟器
#   bash scripts/stage1-services.sh up --no-gui        # 不起 Qt 窗口（无桌面时）
#   bash scripts/stage1-services.sh up --no-sim        # 不发模拟遥测
#   bash scripts/stage1-services.sh up --reset         # 先重建演示库再启动（会覆盖演示数据）
#   bash scripts/stage1-services.sh up --port 48090    # 换服务端端口（Qt 端自动写 app.ini 跟随）
#   bash scripts/stage1-services.sh down               # 停止本次启动的所有服务
#   bash scripts/stage1-services.sh status             # 查看各服务状态
#   bash scripts/stage1-services.sh logs [名称]        # 跟踪日志（默认全部）
#
# 可选环境变量覆盖：BUILD_DIR PYTHON NODE_BIN_DIR DISPLAY XDG_RUNTIME_DIR
set -uo pipefail

project_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"
build_dir="${BUILD_DIR:-$project_root/build}"
pid_file="$project_root/runtime/stage1-services.pids"
log_dir="$project_root/runtime/logs"
config_dir="$project_root/runtime/stage1-config"   # 给 Qt 端生成 app.ini 的当前目录
db="$project_root/runtime/charge_platform.sqlite"
host="127.0.0.1"
port="48080"
api_port="8000"
dash_port="4173"
pile_id="101"

with_web=1; with_gui=1; with_sim=1; do_reset=0

hr() { printf '%s\n' "------------------------------------------------------------"; }
log() { printf '  %-10s %s\n' "$1" "$2"; }

# ---------- 工具探测 ----------
detect_python() {
  for c in "${PYTHON:-}" \
           "$project_root/analytics/.venv312/bin/python" \
           "$project_root/analytics/.venv/bin/python" \
           "$(ls -d /home/bit/projects/*/analytics/.venv312/bin/python 2>/dev/null | head -1)"; do
    [ -n "$c" ] && [ -x "$c" ] && { printf '%s' "$c"; return 0; }
  done
  command -v python3 2>/dev/null || true
}
detect_node_dir() {
  if [ -n "${NODE_BIN_DIR:-}" ] && [ -x "$NODE_BIN_DIR/npm" ]; then printf '%s' "$NODE_BIN_DIR"; return 0; fi
  if command -v npm >/dev/null 2>&1; then dirname "$(command -v npm)"; return 0; fi
  ls -d /home/bit/.nvm/versions/node/*/bin 2>/dev/null | sort -V | tail -1
}
detect_display() {
  if [ -n "${DISPLAY:-}" ]; then printf '%s' "$DISPLAY"; return 0; fi
  local d; d="$(systemctl --user show-environment 2>/dev/null | sed -n 's/^DISPLAY=//p')"
  [ -n "$d" ] && printf '%s' "$d" || printf ':0'
}

PYTHON_BIN="$(detect_python)"
NODE_DIR="$(detect_node_dir)"

port_busy() { timeout 2 bash -c "</dev/tcp/$1/$2" 2>/dev/null; }

# ---------- 启动/回收工具 ----------
# 用 setsid 起独立会话（进程组首即 $!），命令结束/被杀时整组回收，
# 避免 npm->vite、bash->python、python->uvicorn 之类留下孤儿子进程。
spawn() {           # spawn LABEL cmd args...
  local label="$1"; shift
  setsid "$@" >"$log_dir/$label.log" 2>&1 </dev/null &
  local pid=$!
  printf '%s %s\n' "$label" "$pid" >>"$pid_file"
  log "$label" "启动 pid=$pid 日志 $log_dir/$label.log"
}
spawn_in() {        # spawn_in DIR LABEL cmd args...
  local dir="$1" label="$2"; shift 2
  setsid bash -c 'cd "$1" || exit 1; shift; exec "$@"' _ "$dir" "$@" \
    >"$log_dir/$label.log" 2>&1 </dev/null &
  local pid=$!
  printf '%s %s\n' "$label" "$pid" >>"$pid_file"
  log "$label" "启动 pid=$pid 日志 $log_dir/$label.log"
}

kill_group() { kill -TERM -- "-$1" 2>/dev/null || kill -TERM "$1" 2>/dev/null || true; }

live_pids_in_file() {
  [ -f "$pid_file" ] || return 1
  local found=1
  while read -r label pid; do
    [ -n "${pid:-}" ] || continue
    case "$pid" in ''|*[!0-9]*) continue ;; esac
    if kill -0 "$pid" 2>/dev/null && ps -p "$pid" -o args= 2>/dev/null | grep -q "$project_root"; then
      echo "  $label pid=$pid"; found=0
    fi
  done <"$pid_file"
  return $found
}
wait_port() {       # wait_port host port 次数(×0.5s)
  for _ in $(seq 1 "${3:-20}"); do port_busy "$1" "$2" && return 0; sleep 0.5; done
  return 1
}

# ---------- down ----------
cmd_down() {
  if [ ! -f "$pid_file" ]; then echo "没有 $pid_file，无需停止"; echo "STOP=OK"; return 0; fi
  echo "停止第一阶段服务 ..."
  while read -r label pid; do
    [ -n "${pid:-}" ] || continue
    case "$pid" in ''|*[!0-9]*) continue ;; esac
    if kill -0 "$pid" 2>/dev/null; then
      kill_group "$pid"
      for _ in $(seq 1 20); do kill -0 "$pid" 2>/dev/null || break; sleep 0.5; done
      if kill -0 "$pid" 2>/dev/null; then kill -KILL -- "-$pid" 2>/dev/null || kill -KILL "$pid" 2>/dev/null || true; fi
      echo "  STOPPED  $label pid=$pid"
    fi
  done <"$pid_file"
  rm -f "$pid_file"
  echo "STOP=OK"
}

# ---------- status ----------
cmd_status() {
  echo "第一阶段服务状态"
  hr
  if [ -f "$pid_file" ]; then
    while read -r label pid; do
      [ -n "${pid:-}" ] || continue
      case "$pid" in ''|*[!0-9]*) continue ;; esac
      local state="已停止"; kill -0 "$pid" 2>/dev/null && state="运行中"
      printf '  %-10s pid=%-7s %s\n' "$label" "$pid" "$state"
    done <"$pid_file"
  else
    echo "  （无 pid 记录）"
  fi
  hr
  for pair in "server:$port" "analytics:$api_port" "dashboard:$dash_port"; do
    local name="${pair%%:*}" p="${pair##*:}"
    port_busy "$host" "$p" && echo "  端口 $host:$p ($name) 正在监听" || echo "  端口 $host:$p ($name) 未监听"
  done
}

# ---------- logs ----------
cmd_logs() {
  local want="${1:-}"
  if [ -n "$want" ]; then tail -n 40 -f "$log_dir/$want.log"
  else tail -n 20 -f "$log_dir"/*.log; fi
}

# ---------- up ----------
cmd_up() {
  mkdir -p "$log_dir" "$config_dir/config"

  local live; live="$(live_pids_in_file || true)"
  if [ -n "$live" ]; then
    echo "已有运行中的第一阶段服务，请先执行: bash scripts/stage1-services.sh down" >&2
    printf '%s\n' "$live" >&2
    exit 3
  fi

  # 数据库准备：默认沿用既有演示库，不存在才初始化；--reset 才覆盖。
  if [ "$do_reset" = 1 ]; then
    echo "重建演示库: $db"
    bash "$project_root/scripts/reset-demo-database.sh" "$db" || { echo "重建演示库失败" >&2; exit 1; }
  elif [ ! -f "$db" ]; then
    echo "演示库不存在，首次初始化: $db"
    bash "$project_root/scripts/reset-demo-database.sh" "$db" || { echo "初始化演示库失败" >&2; exit 1; }
  else
    echo "沿用既有演示库: $db"
  fi

  # 端口占用检查（先失败再启动，避免半启动状态）。
  local pairs="server:$port"
  [ "$with_web" = 1 ] && pairs="$pairs analytics:$api_port dashboard:$dash_port"
  for pair in $pairs; do
    local name="${pair%%:*}" p="${pair##*:}"
    if port_busy "$host" "$p"; then
      echo "端口 $host:$p ($name) 已被占用，请先释放或换端口" >&2; exit 4
    fi
  done

  : >"$pid_file"
  echo "启动第一阶段服务 ..."; hr

  # 1) C++ 服务端（必需）
  local server="$build_dir/charge_platform_server"
  [ -x "$server" ] || { echo "缺少 $server，请先构建（cmake --build build）" >&2; exit 2; }
  spawn server "$server" --host "$host" --port "$port" --db "$db"
  if ! wait_port "$host" "$port" 20; then
    echo "SERVER=FAIL 未在 10 秒内就绪，见 $log_dir/server.log" >&2
    cmd_down >/dev/null 2>&1 || true; exit 1
  fi

  # 2) FastAPI + Web 大屏
  if [ "$with_web" = 1 ]; then
    if [ -z "$PYTHON_BIN" ] || [ ! -x "$PYTHON_BIN" ]; then
      log analytics "[跳过] 未找到 analytics venv python"
    else
      spawn_in "$project_root/analytics" analytics \
        env CHARGE_PLATFORM_DB="$db" PYTHONPATH="." \
        "$PYTHON_BIN" -m uvicorn src.api:app --host "$host" --port "$api_port"
      wait_port "$host" "$api_port" 30 && log analytics "就绪 http://$host:$api_port/" \
        || log analytics "[警告] 未就绪，见 $log_dir/analytics.log"
    fi
    if [ -n "$NODE_DIR" ] && [ -x "$NODE_DIR/npm" ] && [ -d "$project_root/dashboard/node_modules" ]; then
      spawn_in "$project_root/dashboard" dashboard env PATH="$NODE_DIR:$PATH" npm run dev
      wait_port "$host" "$dash_port" 30 && log dashboard "就绪 http://$host:$dash_port/" \
        || log dashboard "[警告] 未就绪，见 $log_dir/dashboard.log"
    else
      log dashboard "[跳过] 缺少 node/npm 或 dashboard/node_modules"
    fi
  fi

  # 3) Qt 双端窗口
  if [ "$with_gui" = 1 ]; then
    local display; display="$(detect_display)"
    local xdg="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}"
    # 生成 app.ini，让 Qt 端连到本次 host:port（否则用内置默认 48080）。
    cat >"$config_dir/config/app.ini" <<INI
[server]
host=$host
port=$port
INI
    for pair in "user:clients/user/charge_platform_user" "admin:clients/admin/charge_platform_admin"; do
      local name="${pair%%:*}" bin
      bin="$build_dir/${pair##*:}"
      if [ -x "$bin" ]; then
        spawn_in "$config_dir" "$name" env DISPLAY="$display" XDG_RUNTIME_DIR="$xdg" "$bin"
        log "$name" "DISPLAY=$display"
      else
        log "$name" "[跳过] 缺少 $bin"
      fi
    done
  fi

  # 4) 桩模拟器（可选）
  if [ "$with_sim" = 1 ]; then
    local sim="$project_root/build-member3-simulator/pile_simulator"
    if [ ! -x "$sim" ]; then
      cmake -S "$project_root/simulator" -B "$project_root/build-member3-simulator" >/dev/null 2>&1
      cmake --build "$project_root/build-member3-simulator" --parallel >/dev/null 2>&1
    fi
    if [ -x "$sim" ]; then
      spawn simulator "$sim" --pile "$pile_id" --scenario normal \
        --connect "$host:$port" --state-file "$project_root/runtime/pile-$pile_id.energy"
    else
      log simulator "[跳过] 模拟器未构建成功"
    fi
  fi

  hr
  echo "START=OK"
  echo "DATABASE=$db"
  echo "SERVER=$host:$port"
  [ "$with_web" = 1 ] && echo "ANALYTICS=http://$host:$api_port/   DASHBOARD=http://$host:$dash_port/"
  echo "PID_FILE=$pid_file   日志目录=$log_dir"
  echo "停止: bash scripts/stage1-services.sh down"
}

# ---------- 参数解析 ----------
cmd="${1:-}"; shift || true
logs_target=""
if [ "$cmd" = "logs" ] && [ "$#" -gt 0 ] && [ "${1#-}" = "$1" ]; then logs_target="$1"; shift; fi
while [ "$#" -gt 0 ]; do
  case "$1" in
    --no-web) with_web=0; shift ;;
    --no-gui) with_gui=0; shift ;;
    --no-sim) with_sim=0; shift ;;
    --reset) do_reset=1; shift ;;
    --db) db="$2"; shift 2 ;;
    --host) host="$2"; shift 2 ;;
    --port) port="$2"; shift 2 ;;
    --api-port) api_port="$2"; shift 2 ;;
    --dash-port) dash_port="$2"; shift 2 ;;
    *) echo "未知参数: $1" >&2; exit 2 ;;
  esac
done

case "$cmd" in
  up) cmd_up ;;
  down) cmd_down ;;
  status) cmd_status ;;
  logs) cmd_logs "$logs_target" ;;
  -h|--help|help|"") sed -n '2,26p' "$0" ;;
  *) echo "未知子命令: $cmd（可用 up|down|status|logs）" >&2; exit 2 ;;
esac
