# S1-A 联动契约（FastAPI 只读侧）：/dashboard 必须透出 snapshotVersion；
# 旧库没有 linkage_meta 表时回退 0，不得拒绝服务。
import sqlite3  # 测试夹具直接建最小表。

# TestClient 直调 FastAPI 路由，与线上口径一致。
from fastapi.testclient import TestClient

# 复用 test_api 的最小大屏建库夹具（表结构与只读投影查询对齐；pytest 无包结构，直接模块导入）。
from test_api import create_dashboard_database
# 被测入口。
from src.api import app

# 模块级客户端。
client = TestClient(app)


# 无 linkage_meta 表：回退 0，服务不失败。
def test_snapshot_version_falls_back_without_table(tmp_path):
    from src.sqlite_dashboard import _snapshot_version  # 被测纯函数。

    database = sqlite3.connect(tmp_path / "no-meta.sqlite")  # 空库，无任何表。
    database.execute("CREATE TABLE other(x)")  # 占位表，让文件非空。
    database.commit()  # 落盘。
    assert _snapshot_version(database) == 0  # 回退口径。


# 有 linkage_meta：返回当前文本数字的整数值。
def test_snapshot_version_reads_counter(tmp_path):
    from src.sqlite_dashboard import _snapshot_version  # 被测纯函数。

    database = sqlite3.connect(tmp_path / "meta.sqlite")  # 带联动表的库。
    database.execute(
        "CREATE TABLE linkage_meta(key TEXT PRIMARY KEY, value TEXT NOT NULL)"
    )  # 与迁移 010 同构。
    database.execute(
        "INSERT INTO linkage_meta(key, value) VALUES('changeVersion', '42')"
    )  # 预置计数。
    database.commit()  # 落盘。
    assert _snapshot_version(database) == 42  # 直读。


# /dashboard 透出 snapshotVersion：灌入联动表后数值同步，且 old 库回退 0。
def test_dashboard_payload_exposes_snapshot_version(tmp_path, monkeypatch):
    from src.sqlite_dashboard import build_dashboard_payload  # 被测入口。

    database_path = tmp_path / "dashboard.sqlite"  # 临时统一库。
    create_dashboard_database(database_path, with_rows=True)  # 最小可用结构 + 样例行。
    connection = sqlite3.connect(database_path)  # 夹具读写连接（被测接口随后只读打开）。
    connection.execute(
        "CREATE TABLE IF NOT EXISTS linkage_meta(key TEXT PRIMARY KEY, value TEXT NOT NULL)"
    )  # 迁移 010 同构。
    connection.execute(
        "INSERT INTO linkage_meta(key, value) VALUES('changeVersion', '9')"
    )  # 预置写版本。
    connection.commit()  # 落盘。
    connection.close()  # 释放。

    monkeypatch.setenv("CHARGE_PLATFORM_DB", str(database_path))  # 与线上一致的环境注入。
    payload = build_dashboard_payload(quality={}, model_mode="RULE_BASELINE")  # 直接构建。
    assert payload["snapshotVersion"] == 9  # 联动版本透出。
    assert "snapshotAtUtc" in payload  # 快照时间字段存在。
