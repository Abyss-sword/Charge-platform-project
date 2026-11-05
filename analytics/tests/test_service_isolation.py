# 用路径对象拼仓库根目录，再扫 analytics/src 全部 .py 做静态检查。
from pathlib import Path

# 调用 /health 对应的处理函数，确认分析服务在无业务库写入路径时仍能报 ok。
from src.api import health


# 静态扫描源码 + 调用 health()，证明分析侧只有只读打开、没有写库语句。
def test_analytics_has_no_business_database_write_path():
    # parents[2]：本文件在 analytics/tests/，上两级才是仓库根。
    project = Path(__file__).resolve().parents[2]
    # 把 analytics/src 下每个 .py 拼成一份全文，后面用子串判断打开方式和 SQL 动词。
    source = "\n".join(path.read_text(encoding="utf-8") for path in (project / "analytics" / "src").glob("*.py"))
    # 必须同时出现 SQLite URI 只读参数和 query_only，避免只改一处就被绕过。
    assert "mode=ro" in source and "PRAGMA query_only = ON" in source
    # 源码里不允许出现会改业务表的 SQL 动词；大写比较是为了不漏小写写法。
    assert all(token not in source.upper() for token in ("INSERT INTO", "UPDATE ", "DELETE FROM", "DROP TABLE"))
    # 只读约束成立的同时，健康检查仍返回 ok，说明隔离不是把服务直接关掉。
    assert health()["status"] == "ok"

# 【文件讲解·答辩用】
# 功能作用：这是成员3“分析服务不得改业务库”的守门测试。大屏和预测都读同一份 SQLite，
#   答辩时最容易被问“会不会把订单改掉、会不会 DROP 表”。本文件用源码扫描加健康检查，
#   把这条边界钉死：打开方式必须是只读，源码里不能出现写表 SQL，服务本身仍可健康响应。
# 技术原理：pytest 收集本函数后，先用 pathlib 定位仓库根，再 glob analytics/src/*.py 读全文。
#   这是静态契约，不启动 FastAPI、不连真实库，所以跑得快、也不依赖 runtime 里有没有 sqlite。
#   mode=ro 是 SQLite URI 查询参数；PRAGMA query_only = ON 是连接级禁写。两者同时要求，
#   是因为只开 URI 或只开 PRAGMA 都可能被漏配。随后把全文 upper() 后查 INSERT/UPDATE/DELETE/DROP，
#   防止有人用小写 SQL 绕过。最后直接调用 src.api.health()，确认只读策略没有把接口打挂。
# 代码思路：三步断言，对应三句答辩词——“怎么打开”“禁止写什么”“服务还能不能用”。
#   不 mock、不造库，是因为要检查的是源码文本本身，而不是某次运行碰巧没写出。
# 与其他文件关系：被扫描的是 analytics/src/ 全部 Python（api.py、sqlite_dashboard.py、
#   simulator_control.py 等）。health 定义在 api.py。sqlite_dashboard.py 里真正执行
#   mode=ro 和 query_only。test_api.py 用“读前后字节相等”从运行时再验一遍只读；
#   本文件从源码层验，两套测试互补。模拟器写库走 TCP 服务端，不走这条分析路径。
# 答辩讲法：先说结论“分析进程对业务库只有只读句柄”。再打开本测试指出 mode=ro、
#   query_only、禁止 INSERT 三行。若被问“字符串扫描会不会误伤注释”，可承认这是教学级
#   静态门禁，真正运行时保障看 sqlite_dashboard 的只读连接和 test_api 的文件字节对比。
#   若被问“场景切换会不会写库”，转到 test_api.py 的 simulator 用例和 simulator_control.py：
#   FastAPI 只拉起 pile_simulator，不 SQL 写业务表。
