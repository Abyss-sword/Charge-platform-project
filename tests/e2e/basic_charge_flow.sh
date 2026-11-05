#!/usr/bin/env bash
# 测试文件头注释（成员1 · 第一阶段 · 自动测试）：
# 端到端包装脚本：调用 member1_flow_test 后对输出做文本匹配断言
# （订单 FINISHED、桩 IDLE、仅一笔支付、营收 0.00 → 10.27）。
# 历史上它检查旧整数格式 REVENUE_DELTA=1200，f428fbc 已修正为当前两位小数口径。

set -euo pipefail

project_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
build_dir="${BUILD_DIR:-${project_root}/build}"
executable="${build_dir}/member1_flow_test"

if [[ ! -x "${executable}" ]]; then
    echo "member1_flow_test not built: ${executable}" >&2
    exit 2
fi

result="$(${executable})"
printf '%s\n' "${result}"
grep -qx 'ORDER=FINISHED' <<<"${result}"
grep -qx 'PILE=IDLE' <<<"${result}"
grep -qx 'PAYMENT_COUNT=1' <<<"${result}"
grep -qx 'REVENUE_BEFORE=0.00' <<<"${result}"
grep -qx 'REVENUE_AFTER=10.27' <<<"${result}"
