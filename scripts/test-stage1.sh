#!/usr/bin/env bash
# 第一阶段自动化回归脚本（VM / Ubuntu）
#
# 一次执行第一阶段的全部验证，逐阶段输出 PASS/FAIL/SKIP，最后给汇总表并按失败数返回退出码：
#   1) build        CMake 配置 + 构建（可跳过）
#   2) ctest        C++/Qt 单元与协议测试（离屏平台）
#   3) pytest       分析服务与治理管线
#   4) vitest       Web 大屏前端
#   5) mvn          MapReduce JUnit（bigdata）
#   6) e2e          充电闭环包装脚本
#   7) acceptance   三端联动验收驱动（自动起停服务端 + FastAPI，独立临时库）
#
# 用法:
#   bash scripts/test-stage1.sh                     # 全量（含 acceptance）
#   bash scripts/test-stage1.sh --no-build          # 跳过配置与构建
#   bash scripts/test-stage1.sh --skip acceptance   # 跳过某阶段（可重复）
#   bash scripts/test-stage1.sh --only ctest        # 只跑某阶段（可重复）
#   bash scripts/test-stage1.sh --no-acceptance     # 等价于 --skip acceptance
#
# 可通过环境变量覆盖自动探测的路径：
#   BUILD_DIR / PYTHON / NODE_BIN_DIR / JAVA_HOME / MVN / ACC_SERVER_PORT / ACC_API_PORT
set -uo pipefail

project_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"
cd "$project_root"

build_dir="${BUILD_DIR:-$project_root/build}"
acc_server_port="${ACC_SERVER_PORT:-48086}"
acc_api_port="${ACC_API_PORT:-8010}"

do_build=1
only_phases=()
skip_phases=()

while [ "$#" -gt 0 ]; do
  case "$1" in
    --no-build) do_build=0; shift ;;
    --no-acceptance) skip_phases+=("acceptance"); shift ;;
    --only) only_phases+=("$2"); shift 2 ;;
    --skip) skip_phases+=("$2"); shift 2 ;;
    -h|--help) sed -n '2,26p' "$0"; exit 0 ;;
    *) echo "未知参数: $1" >&2; exit 2 ;;
  esac
done

all_phases=(build ctest pytest vitest mvn e2e acceptance)

want_phase() {
  local p="$1"
  # --only 白名单优先：指定后仅跑白名单内的阶段（含 build）。
  if [ "${#only_phases[@]}" -gt 0 ]; then
    local hit=1
    for o in "${only_phases[@]}"; do [ "$o" = "$p" ] && hit=0; done
    [ "$hit" = 1 ] && return 1
  fi
  for s in "${skip_phases[@]:-}"; do [ "$s" = "$p" ] && return 1; done
  if [ "$p" = "build" ] && [ "$do_build" = 0 ]; then return 1; fi
  return 0
}

# ---- 结果记录 ----
declare -a res_name=() res_state=() res_note=()
record() { res_name+=("$1"); res_state+=("$2"); res_note+=("${3:-}"); }
hr() { printf '%s\n' "------------------------------------------------------------"; }

# ---- 工具自动探测 ----
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
  if [ -n "${NODE_BIN_DIR:-}" ] && [ -x "$NODE_BIN_DIR/node" ]; then printf '%s' "$NODE_BIN_DIR"; return 0; fi
  if command -v node >/dev/null 2>&1; then dirname "$(command -v node)"; return 0; fi
  local n; n="$(ls -d /home/bit/.nvm/versions/node/*/bin 2>/dev/null | sort -V | tail -1)"
  [ -n "$n" ] && printf '%s' "$n"
}
detect_java_home() {
  if [ -n "${JAVA_HOME:-}" ] && [ -x "$JAVA_HOME/bin/java" ]; then printf '%s' "$JAVA_HOME"; return 0; fi
  local c; c="$(ls -d /home/bit/opt/jdk* /opt/jdk* /usr/lib/jvm/* 2>/dev/null | head -1)"
  [ -n "$c" ] && printf '%s' "$c"
}
detect_mvn() {
  if [ -n "${MVN:-}" ] && [ -x "$MVN" ]; then printf '%s' "$MVN"; return 0; fi
  if command -v mvn >/dev/null 2>&1; then command -v mvn; return 0; fi
  local m; m="$(ls /home/bit/opt/apache-maven-*/bin/mvn /opt/apache-maven-*/bin/mvn 2>/dev/null | head -1)"
  [ -n "$m" ] && printf '%s' "$m"
}

PYTHON_BIN="$(detect_python)"
NODE_DIR="$(detect_node_dir)"
JAVA_HOME_DETECTED="$(detect_java_home)"
MVN_BIN="$(detect_mvn)"

echo "第一阶段自动化回归"
echo "项目根: $project_root"
echo "构建目录: $build_dir"
echo "python: ${PYTHON_BIN:-未找到}   node: ${NODE_DIR:-未找到}   java: ${JAVA_HOME_DETECTED:-未找到}   mvn: ${MVN_BIN:-未找到}"
hr

# ---- 1) build ----
if want_phase build; then
  echo "[build] 配置与构建 ..."
  if [ ! -f "$build_dir/CMakeCache.txt" ]; then
    cmake -S "$project_root" -B "$build_dir" -G Ninja -DBUILD_TESTING=ON || { record build FAIL "cmake 配置失败"; }
  fi
  if [ -f "$build_dir/CMakeCache.txt" ]; then
    if cmake --build "$build_dir" -j"$(nproc)"; then record build PASS "构建成功"
    else record build FAIL "编译失败，见上方输出"; fi
  fi
  hr
fi

# ---- 2) ctest ----
if want_phase ctest; then
  echo "[ctest] C++/Qt 测试 ..."
  if [ -f "$build_dir/CTestTestfile.cmake" ]; then
    out="$(QT_QPA_PLATFORM=offscreen ctest --test-dir "$build_dir" -j"$(nproc)" --output-on-failure 2>&1)"
    printf '%s\n' "$out" | tail -6
    line="$(printf '%s\n' "$out" | grep -E 'tests passed|tests failed' | tail -1)"
    if printf '%s\n' "$out" | grep -q '100% tests passed'; then record ctest PASS "$line"
    else record ctest FAIL "$line"; fi
  else
    record ctest SKIP "未找到构建产物，请先 build"
  fi
  hr
fi

# ---- 3) pytest ----
if want_phase pytest; then
  echo "[pytest] 分析服务与治理 ..."
# + 注意不加 -q：analytics/pyproject.toml 的 addopts 已含 -q，叠加会变 -qq 抹掉汇总行。
  if [ -n "$PYTHON_BIN" ] && [ -x "$PYTHON_BIN" ]; then
    out="$("$PYTHON_BIN" -m pytest analytics/tests -p no:warnings 2>&1)"; rc=$?
    printf '%s\n' "$out" | tail -4
    line="$(printf '%s\n' "$out" | grep -E '[0-9]+ passed|[0-9]+ failed|error' | tail -1)"
    [ "$rc" = 0 ] && record pytest PASS "$line" || record pytest FAIL "$line"
  else
    record pytest SKIP "未找到可用 python/venv"
  fi
  hr
fi

# ---- 4) vitest ----
if want_phase vitest; then
  echo "[vitest] Web 大屏前端 ..."
  if [ -n "$NODE_DIR" ] && [ -x "$NODE_DIR/npm" ] && [ -d "$project_root/dashboard/node_modules" ]; then
    out="$(cd "$project_root/dashboard" && PATH="$NODE_DIR:$PATH" npm run test:run 2>&1)"; rc=$?
    printf '%s\n' "$out" | grep -E 'Test Files|Tests |✓|✗' | tail -4
    line="$(printf '%s\n' "$out" | grep -E 'Tests +[0-9]+ (passed|failed)' | tail -1)"
    [ "$rc" = 0 ] && record vitest PASS "$line" || record vitest FAIL "$line"
  else
    record vitest SKIP "缺少 node/npm 或 dashboard/node_modules"
  fi
  hr
fi

# ---- 5) mvn ----
if want_phase mvn; then
  echo "[mvn] MapReduce JUnit ..."
  if [ -n "$MVN_BIN" ] && [ -n "$JAVA_HOME_DETECTED" ]; then
    out="$(JAVA_HOME="$JAVA_HOME_DETECTED" PATH="$JAVA_HOME_DETECTED/bin:$(dirname "$MVN_BIN"):$PATH" \
           "$MVN_BIN" -f bigdata/mapreduce/pom.xml test 2>&1)"; rc=$?
    printf '%s\n' "$out" | grep -E 'Tests run:|BUILD (SUCCESS|FAILURE)' | tail -4
    line="$(printf '%s\n' "$out" | grep -E 'Tests run:.*Failures' | tail -1)"
    [ "$rc" = 0 ] && record mvn PASS "$line" || record mvn FAIL "$line"
  else
    record mvn SKIP "未找到 maven 或 jdk"
  fi
  hr
fi

# ---- 6) e2e ----
if want_phase e2e; then
  echo "[e2e] 充电闭环包装脚本 ..."
  if [ -x "$build_dir/member1_flow_test" ]; then
    if out="$(BUILD_DIR="$build_dir" bash tests/e2e/basic_charge_flow.sh 2>&1)"; then
      printf '%s\n' "$out"
      record e2e PASS "$(printf '%s\n' "$out" | tr '\n' ' ' | sed 's/  */ /g')"
    else
      printf '%s\n' "$out"
      record e2e FAIL "输出未同时满足 5 条断言"
    fi
  else
    record e2e SKIP "未找到 $build_dir/member1_flow_test"
  fi
  hr
fi

# ---- 7) acceptance ----
if want_phase acceptance; then
  echo "[acceptance] 三端联动验收驱动 ..."
  server_bin="$build_dir/charge_platform_server"
  if [ ! -x "$server_bin" ] || [ -z "$PYTHON_BIN" ]; then
    record acceptance SKIP "缺少 $server_bin 或 python"
  else
    run_dir="$(mktemp -d /tmp/stage1-acceptance.XXXXXX)"
    acc_db="$run_dir/acceptance.sqlite"
    acc_evidence="$run_dir/evidence"
    server_pid=""; api_pid=""
    cleanup_acc() {
      # 先杀真正的 python/uvicorn 与 server（exec 保证 $! 即进程本身），再兜底按 run_dir 清理。
      [ -n "$api_pid" ] && kill "$api_pid" 2>/dev/null
      [ -n "$server_pid" ] && kill "$server_pid" 2>/dev/null
      [ -n "$api_pid" ] && wait "$api_pid" 2>/dev/null
      [ -n "$server_pid" ] && wait "$server_pid" 2>/dev/null
      pkill -f "uvicorn src.api:app --host 127.0.0.1 --port $acc_api_port" 2>/dev/null || true
      rm -rf "$run_dir"
    }
    trap cleanup_acc EXIT
    if ! bash scripts/reset-demo-database.sh "$acc_db" >/dev/null 2>&1; then
      record acceptance FAIL "重置验收库失败"
    else
      "$server_bin" --host 127.0.0.1 --port "$acc_server_port" --db "$acc_db" >"$run_dir/server.log" 2>&1 &
      server_pid=$!
      # 等服务端就绪（最多 10 秒）
      for _ in $(seq 1 20); do
        (exec 3<>"/dev/tcp/127.0.0.1/$acc_server_port") 2>/dev/null && break
        sleep 0.5
      done
      # exec 让子 shell 进程被 python 替换，$! 即 uvicorn 本身，便于可靠清理。
      ( cd "$project_root/analytics" && CHARGE_PLATFORM_DB="$acc_db" PYTHONPATH="." \
        exec "$PYTHON_BIN" -m uvicorn src.api:app --host 127.0.0.1 --port "$acc_api_port" \
        >"$run_dir/analytics.log" 2>&1 ) &
      api_pid=$!
      api_ready=0
      for _ in $(seq 1 30); do
        if curl -sf "http://127.0.0.1:$acc_api_port/dashboard" >/dev/null 2>&1; then api_ready=1; break; fi
        sleep 0.5
      done
      if [ "$api_ready" != 1 ]; then
        record acceptance FAIL "FastAPI 未就绪（端口 $acc_api_port）"
      else
        out="$(ACC_SERVER_PORT="$acc_server_port" ACC_DB_PATH="$acc_db" \
               ACC_DASHBOARD_API="http://127.0.0.1:$acc_api_port/dashboard" \
               ACC_OUT_DIR="$acc_evidence" "$PYTHON_BIN" scripts/acceptance-driver.py 2>&1)"; rc=$?
        printf '%s\n' "$out" | tail -8
        if [ "$rc" = 0 ] && printf '%s\n' "$out" | grep -q 'ACCEPTANCE=OK'; then
          record acceptance PASS "ACCEPTANCE=OK（A/B/C 三流程）"
        else
          record acceptance FAIL "驱动返回码 $rc 或未达 ACCEPTANCE=OK"
        fi
      fi
    fi
    cleanup_acc; trap - EXIT
  fi
  hr
fi

# ---- 汇总 ----
echo "汇总"
printf '%-12s %-6s %s\n' "阶段" "结果" "说明"
printf '%-12s %-6s %s\n' "----" "----" "----"
fail_count=0
for i in "${!res_name[@]}"; do
  printf '%-12s %-6s %s\n' "${res_name[$i]}" "${res_state[$i]}" "${res_note[$i]}"
  [ "${res_state[$i]}" = FAIL ] && fail_count=$((fail_count+1))
done
hr
if [ "$fail_count" = 0 ]; then
  echo "STAGE1_TESTS=OK"
  exit 0
else
  echo "STAGE1_TESTS=FAIL（$fail_count 个阶段失败）"
  exit 1
fi
