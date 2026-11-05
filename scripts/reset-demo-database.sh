#!/usr/bin/env bash
# 成员1 · 第一阶段
# 演示库重建脚本（成员1 · 第一阶段）：接受一个明确的数据库路径，
# 同目录临时建库 → 依次执行五次迁移及迁移记录 → 导入种子 → 完整性检查 → 备份替换。
# 必须先停服务再执行；会重建数据但保留旧库备份，不应当作生产数据导入命令。

set -euo pipefail

case "${OSTYPE:-}" in
    msys*|cygwin*)
        export PATH="/usr/bin:/mingw64/bin:/c/ProgramData/anaconda3/Library/bin:$PATH"
        ;;
esac

if [[ $# -ne 1 || -z "${1//[[:space:]]/}" ]]; then
    echo "用法: $0 <明确的数据库文件路径>" >&2
    exit 2
fi

project_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"
requested_path="$1"
if [[ -L "$requested_path" ]]; then
    echo "拒绝符号链接目标: $requested_path" >&2
    exit 2
fi
database_path="$(realpath -m -- "$requested_path")"
database_parent="$(dirname -- "$database_path")"

if [[ "$database_path" == "/" || "$database_path" == "$project_root" ]]; then
    echo "拒绝不安全的目标路径: $database_path" >&2
    exit 2
fi
if [[ -e "$database_path" && ! -f "$database_path" ]]; then
    echo "目标已存在但不是普通文件: $database_path" >&2
    exit 2
fi

if [[ ! -d "$database_parent" ]]; then
    mkdir -p -- "$database_parent"
fi
temporary_path="$(mktemp --tmpdir="$database_parent" '.stage1-demo.XXXXXX.sqlite')"
cleanup() {
    if [[ -f "$temporary_path" ]]; then
        rm -f -- "$temporary_path"
    fi
}
trap cleanup EXIT

sqlite3 -bail "$temporary_path" \
    'CREATE TABLE schema_migration(name TEXT PRIMARY KEY, applied_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP);'
for migration in "$project_root"/database/migrations/*.sql; do
    migration_name="${migration##*/}"
    migration_name="${migration_name//\'/\'\'}"
    # Match the server: apply the migration and record its name in one transaction.
    {
        printf '%s\n' 'PRAGMA foreign_keys=ON;' 'BEGIN;'
        cat -- "$migration"
        printf '\nINSERT INTO schema_migration(name) VALUES('\''%s'\'');\nCOMMIT;\n' "$migration_name"
    } | sqlite3 -bail "$temporary_path"
done
sqlite3 -bail -cmd 'PRAGMA foreign_keys=ON;' "$temporary_path" < "$project_root/database/seeds/demo.sql"

integrity="$(sqlite3 "$temporary_path" 'PRAGMA integrity_check;')"
foreign_key_errors="$(sqlite3 "$temporary_path" 'PRAGMA foreign_key_check;' | wc -l)"
if [[ "$integrity" != "ok" || "$foreign_key_errors" -ne 0 ]]; then
    echo "新数据库校验失败: integrity=$integrity, foreign_key_errors=$foreign_key_errors" >&2
    exit 1
fi

backup_path=""
if [[ -f "$database_path" ]]; then
    backup_path="$database_path.bak.$(date -u +%Y%m%dT%H%M%S%N)-$$"
    # 备份必须含 WAL 中已提交的事务（01 文档 T08：SQLite 用 backup 机制含 WAL）。
    # 直接 mv 主库会丢掉仍在 -wal 里的已提交数据，并留下会被新库误用的孤儿边车文件。
    if ! sqlite3 "$database_path" ".backup '$backup_path'" 2>/dev/null; then
        # 回退路径：先把 WAL 落盘到主库，再整文件移动（等价于一次完整快照）。
        sqlite3 "$database_path" 'PRAGMA wal_checkpoint(TRUNCATE);' >/dev/null 2>&1 || true
        mv -- "$database_path" "$backup_path"
    fi
    # 备份已含全部数据；清理边车文件，避免旧 WAL/SHM 被应用到刚替换的新库。
    rm -f -- "$database_path-wal" "$database_path-shm"
fi
if ! mv -- "$temporary_path" "$database_path"; then
    if [[ -n "$backup_path" && -f "$backup_path" ]]; then
        mv -- "$backup_path" "$database_path"
    fi
    exit 1
fi

trap - EXIT
echo "database=$database_path"
if [[ -n "$backup_path" ]]; then
    echo "backup=$backup_path"
fi
echo "integrity_check=$integrity"
echo "foreign_key_errors=$foreign_key_errors"
echo "stations=$(sqlite3 "$database_path" 'SELECT COUNT(*) FROM station_info;')"
echo "piles=$(sqlite3 "$database_path" 'SELECT COUNT(*) FROM pile_info;')"
echo "users=$(sqlite3 "$database_path" 'SELECT COUNT(*) FROM user_info;')"
echo "orders=$(sqlite3 "$database_path" 'SELECT COUNT(*) FROM charge_order;')"
echo "alarms=$(sqlite3 "$database_path" 'SELECT COUNT(*) FROM alarm_event;')"
echo "tickets=$(sqlite3 "$database_path" 'SELECT COUNT(*) FROM maintenance_ticket;')"
