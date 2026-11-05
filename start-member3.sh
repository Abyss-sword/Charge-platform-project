#!/usr/bin/env bash
# 成员3一键启动：FastAPI + Web大屏 + 默认模拟场景。不重置统一库。
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
export CHARGE_SERVER_HOST="${CHARGE_SERVER_HOST:-127.0.0.1}"
export CHARGE_SERVER_PORT="${CHARGE_SERVER_PORT:-48080}"
export CHARGE_PILE_ID="${CHARGE_PILE_ID:-101}"
export CHARGE_SCENARIO="${CHARGE_SCENARIO:-normal}"
echo "成员3一键启动。默认连接本机 ${CHARGE_SERVER_HOST}:${CHARGE_SERVER_PORT}，不会重置数据库。"
echo "虚拟机里服务端在其他地址时：CHARGE_SERVER_HOST=192.168.41.128 ./start-member3.sh"
exec "$root/scripts/run-member3-stack.sh"
