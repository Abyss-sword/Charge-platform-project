#!/usr/bin/env bash
# 测试文件头注释（成员1 · 第一阶段 · 自动测试）：
# 重建脚本回归测试：验证 reset-demo-database.sh 的路径处理、备份替换与结果正确性。

set -euo pipefail

case "${OSTYPE:-}" in
    msys*|cygwin*)
        export PATH="/usr/bin:/mingw64/bin:/c/ProgramData/anaconda3/Library/bin:$PATH"
        ;;
esac

project_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd -P)"
test_root="$(mktemp -d)"
trap 'rm -rf -- "$test_root"' EXIT
database_path="$test_root/stage1.sqlite"

"$project_root/scripts/reset-demo-database.sh" "$database_path" >/dev/null

test "$(sqlite3 "$database_path" 'SELECT COUNT(*) FROM station_info;')" = "3"
test "$(sqlite3 "$database_path" 'SELECT COUNT(*) FROM pile_info;')" = "12"
test "$(sqlite3 "$database_path" 'SELECT COUNT(*) FROM user_info;')" = "20"
test "$(sqlite3 "$database_path" 'SELECT COUNT(*) FROM charge_order;')" = "30"
test "$(sqlite3 "$database_path" 'SELECT COUNT(*) FROM alarm_event;')" = "3"
test "$(sqlite3 "$database_path" 'SELECT COUNT(*) FROM maintenance_ticket;')" = "3"
test "$(sqlite3 "$database_path" 'PRAGMA integrity_check;')" = "ok"
test "$(sqlite3 "$database_path" 'PRAGMA foreign_key_check;' | wc -l)" = "0"

sqlite3 "$database_path" "UPDATE station_info SET name='将被备份';"
"$project_root/scripts/reset-demo-database.sh" "$database_path" >/dev/null
test "$(sqlite3 "$database_path" 'SELECT name FROM station_info WHERE id=1;')" = "滨海示范站"
test "$(find "$test_root" -maxdepth 1 -type f -name 'stage1.sqlite.bak.*' | wc -l)" = "1"

if "$project_root/scripts/reset-demo-database.sh" / >/dev/null 2>&1; then
    echo "脚本不应接受根目录" >&2
    exit 1
fi

if "$project_root/scripts/reset-demo-database.sh" "$project_root" >/dev/null 2>&1; then
    echo "脚本不应接受项目根目录" >&2
    exit 1
fi

# WAL 边车文件回归（T08/S2-T20）：重置时必须先把 WAL 落盘再备份，
# 否则备份会丢失已提交但仍在 WAL 中的事务，且孤儿 -wal/-shm 可能污染新库。
wal_db="$test_root/wal.sqlite"
# 用一个保持打开的连接把事务留在 WAL 里（连接关闭前默认不 checkpoint 删除 WAL）。
# 先切 WAL 模式，再走一个显式 BEGIN...COMMIT 事务，随后让连接空转；避免裸 COMMIT 的噪声报错。
{ printf 'PRAGMA journal_mode=WAL;\nBEGIN; CREATE TABLE wal_marker(v TEXT); INSERT INTO wal_marker VALUES('"'"'committed-in-wal'"'"'); COMMIT;\n'; sleep 5; } \
    | sqlite3 "$wal_db" >/dev/null &
wal_writer_pid=$!
for _ in $(seq 1 25); do [ -f "$wal_db-wal" ] && break; sleep 0.2; done
test -f "$wal_db-wal" || { echo "前置条件失败：未生成 WAL 文件" >&2; kill "$wal_writer_pid" 2>/dev/null || true; exit 1; }
"$project_root/scripts/reset-demo-database.sh" "$wal_db" >/dev/null
kill "$wal_writer_pid" 2>/dev/null || true
wait "$wal_writer_pid" 2>/dev/null || true
# 备份必须包含 WAL 中已提交的数据（证明先 checkpoint 再备份）。
wal_backup="$(find "$test_root" -maxdepth 1 -type f -name 'wal.sqlite.bak.*' | head -1)"
test -n "$wal_backup" || { echo "重置后应生成备份文件" >&2; exit 1; }
test "$(sqlite3 "$wal_backup" "SELECT v FROM wal_marker;" 2>/dev/null)" = "committed-in-wal" \
    || { echo "备份丢失了 WAL 中已提交的数据" >&2; exit 1; }
# 新库必须是干净的种子库，且不残留旧 -wal/-shm。
test "$(sqlite3 "$wal_db" 'SELECT COUNT(*) FROM station_info;')" = "3"
test "$(sqlite3 "$wal_db" 'PRAGMA integrity_check;')" = "ok"
test ! -f "$wal_db-wal" || test "$(sqlite3 "$wal_db" 'PRAGMA journal_mode;')" = "wal"
if ! sqlite3 "$wal_db" 'PRAGMA integrity_check;' >/dev/null 2>&1; then
    echo "新库完整性检查失败，可能被孤儿 WAL 污染" >&2
    exit 1
fi
