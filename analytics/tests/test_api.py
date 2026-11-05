# sqlite3 用来在临时目录搭与业务库同构的只读样例库，并模拟独占锁。
import sqlite3
# 场景切换是后台线程，轮询 /simulator/status 时用短睡眠避免忙等。
import time
# 订单/遥测时间要落在东八区“当天”，才能断言今日营收和小时趋势。
from datetime import datetime, timedelta, timezone

# TestClient 不走真实端口，直接调用 FastAPI 路由，适合接口契约测试。
from fastapi.testclient import TestClient

# app 含 /health /dashboard /forecast /simulator/* ，是本文件的被测入口。
from src.api import app


# 模块级客户端复用同一 app；CHARGE_PLATFORM_DB 由各用例 monkeypatch 指向临时库。
client = TestClient(app)


# 按大屏查询所需的六张表建库；with_rows=False 时只建空表，测 empty 状态。
def create_dashboard_database(path, with_rows=True):
    # 普通读写连接只用于测试夹具灌数，被测接口随后会以只读方式打开同一文件。
    database = sqlite3.connect(path)
    # 下列 DDL 对齐 sqlite_dashboard 的结构检查，缺表或缺列会走“结构不兼容”。
    database.executescript(
        # 一次执行六张表 DDL，列名必须和大屏只读查询用到的字段对齐。
        """
        CREATE TABLE station_info (
            id INTEGER PRIMARY KEY, name TEXT, price_yuan_per_kwh TEXT
        );
        CREATE TABLE pile_info (
            id INTEGER PRIMARY KEY, station_id INTEGER, pile_sn TEXT, status TEXT
        );
        CREATE TABLE charge_order (
            id TEXT PRIMARY KEY, status TEXT, energy_kwh TEXT, amount_yuan TEXT,
            started_at TEXT, ended_at TEXT, created_at TEXT, price_locked INTEGER
        );
        CREATE TABLE pile_telemetry (
            id INTEGER PRIMARY KEY, pile_id INTEGER, recorded_at TEXT,
            temperature_c REAL, voltage_v REAL, current_a REAL,
            power_w INTEGER, energy_kwh TEXT, source_mode TEXT
        );
        CREATE TABLE alarm_event (
            id INTEGER PRIMARY KEY, pile_id INTEGER, alarm_type TEXT,
            severity TEXT, content TEXT, status TEXT, occurred_at TEXT
        );
        CREATE TABLE maintenance_ticket (
            id INTEGER PRIMARY KEY, alarm_id INTEGER, assignee TEXT,
            status TEXT, updated_at TEXT
        );
        """
    )  # 建表完成，此时库里还没有业务行。
    # 空库用例跳过 INSERT，只留下表结构。
    if with_rows:
        # 取东八区日历日零点，避免用 UTC 日期导致“今日营收”跨日。
        local_day_start = datetime.now(timezone(timedelta(hours=8))).replace(
            # 清零时分秒和微秒，得到当天确定的业务日起点。
            hour=0, minute=0, second=0, microsecond=0
        )  # replace 得到东八区当天 00:00:00。
        # 三个整点转成 UTC ISO 字符串，供订单、遥测、告警共用。
        times = [
            # 01:00/02:00/03:00 三个点：营收按日切、预测要有 3 点移动平均样本。
            (local_day_start + timedelta(hours=value)).astimezone(timezone.utc).isoformat()
            # 依次偏移 1、2、3 小时。
            for value in (1, 2, 3)
        ]  # times[0/1/2] 对应当天 1/2/3 点的 UTC 文本。
        # 插入唯一站点：名称给遥测站名，电价 1.20 与已完成订单金额一致。
        database.executemany(
            # 三列：id、站名、元/kWh 文本价。
            "INSERT INTO station_info VALUES (?,?,?)",
            # 站点 1 名为测试站，供 pile_info 外键和遥测 stationName。
            [(1, "测试站", "1.20")],
        )  # 站点只插一行，对应指标“站点总数”=1。
        # 三桩三种状态：IDLE/CHARGING/OFFLINE，用来算在线率和即时使用率。
        database.executemany(
            # 四列：桩 id、所属站、序列号、业务状态。
            "INSERT INTO pile_info VALUES (?,?,?,?)",
            [  # 三桩列表开始。
                # 101 空闲在线，遥测挂在这根桩上。
                (101, 1, "PILE-DEMO-01", "IDLE"),
                # 102 充电中，计入“充电中订单”之外的桩状态分布。
                (102, 1, "PILE-DEMO-02", "CHARGING"),
                # 103 离线，在线率应为 2/3≈66.7%，使用率按在线桩里充电占比 1/2=50%。
                (103, 1, "PILE-DEMO-03", "OFFLINE"),
            ],  # 三桩插完。
        )  # pile_info 灌数结束。
        # 三张订单覆盖已完成、待支付、充电中，只有 FINISHED 的 1.20 计入今日营收。
        database.executemany(
            # 八列与 charge_order 建表顺序一致。
            "INSERT INTO charge_order VALUES (?,?,?,?,?,?,?,?)",
            [  # 三张订单开始。
                # 已完成 1.00kWh/1.20 元，起止落在当天，price_locked=1。
                ("ORD-1", "FINISHED", "1.00", "1.20", times[0], times[1], times[0], 1),
                # 待支付金额 9.99 不得计入今日营收，防止把未入账当收入。
                ("ORD-2", "PENDING_PAYMENT", "2.00", "9.99", times[1], times[2], times[1], 1),
                # 充电中无结束时间、金额 0，计入“充电中订单”=1。
                ("ORD-3", "CHARGING", "0.50", "0.00", times[2], None, times[2], 1),
            ],  # 订单插完：营收只认 ORD-1。
        )  # charge_order 灌数结束。
        # 四条遥测：前三条时间递进，第四条与第三条完全重复，用来测 duplicate=1。
        database.executemany(
            # 九列：含温压流功率、累计电量字符串、SIMULATED 来源。
            "INSERT INTO pile_telemetry VALUES (?,?,?,?,?,?,?,?,?)",
            [  # 四条遥测开始，最后一条是重复点。
                # 最早一条：功率 7kW，电量 100.00，后面差分能量趋势用相邻电量。
                (1, 101, times[0], 25.0, 230.0, 30.4, 7000, "100.00", "SIMULATED"),
                # 中间条：电量 100.50，温度略升。
                (2, 101, times[1], 26.0, 231.0, 31.2, 7200, "100.50", "SIMULATED"),
                # 最新合法点：27℃/230V/30.9A/7100W/101.00kWh，大屏 telemetry[0] 应对它。
                (3, 101, times[2], 27.0, 230.0, 30.9, 7100, "101.00", "SIMULATED"),
                # 故意重复 times[2] 的同一读数，质量检查 duplicate 计 1、simulated 计 4。
                (4, 101, times[2], 27.0, 230.0, 30.9, 7100, "101.00", "SIMULATED"),
            ],  # 遥测插完。
        )  # pile_telemetry 灌数结束。
        # 一条未关闭过热告警，发生在最新采样时刻。
        database.execute(
            # 告警 id=1 挂桩 101，类型 OVERHEAT、级别 WARNING。
            "INSERT INTO alarm_event VALUES (1,101,'OVERHEAT','WARNING',"
            # 内容与 OPEN 状态；发生时间用占位符绑定 times[2]。
            "'温度超过阈值','OPEN',?)",
            # 绑定最新样例时间。
            (times[2],),
        )  # 过热告警插入完成。
        # 工单挂这条告警，状态 PROCESSING，供 tickets[0] 断言。
        database.execute(
            # 工单 id=1、alarm_id=1、处理人 engineer-a。
            "INSERT INTO maintenance_ticket VALUES "
            # PROCESSING 表示维修进行中；更新时间同样绑 times[2]。
            "(1,1,'engineer-a','PROCESSING',?)",
            # 绑定最新样例时间。
            (times[2],),
        )  # 维修工单插入完成。
    # 灌数结束落盘，随后关闭夹具连接，避免和只读查询抢锁。
    database.commit()
    # 关掉写连接，后面 TestClient 走分析进程的只读 URI。
    database.close()


# /health 必须声明只读且当前库可读，不能把写权限或连不上伪装成 ok。
def test_health_includes_read_only_source_metadata(tmp_path, monkeypatch):
    # 在 pytest 临时目录放样例库，测完即删。
    database = tmp_path / "stage1.sqlite"
    # 灌入完整六表和样例行。
    create_dashboard_database(database)
    # 分析服务只认环境变量 CHARGE_PLATFORM_DB，与线上一致。
    monkeypatch.setenv("CHARGE_PLATFORM_DB", str(database))
    # GET /health 返回 JSON：状态、访问模式、是否可读。
    payload = client.get("/health").json()
    # 进程活着且自检通过。
    assert payload["status"] == "ok"
    # 对外承诺只读，前端 DataSourceBar 也读这个字段。
    assert payload["databaseAccess"] == "read-only"
    # 当前路径下的库能打开、结构可用。
    assert payload["databaseReadable"] is True


# /dashboard 必须按口径算出指标、Decimal 营收、遥测最新点，且读库不改文件字节。
def test_dashboard_uses_required_metric_definitions_and_decimal_revenue(
    # pytest 注入临时目录和 monkeypatch。
    tmp_path, monkeypatch
):  # 指标口径 + 只读字节对比。
    # 与 health 用例同一套夹具文件名，互不影响因为 tmp_path 按用例隔离。
    database = tmp_path / "stage1.sqlite"
    # 写入站点/桩/订单/遥测/告警/工单。
    create_dashboard_database(database)
    # 指向这份临时库。
    monkeypatch.setenv("CHARGE_PLATFORM_DB", str(database))
    # 读库前拍一份字节，用来证明接口没有 INSERT/UPDATE。
    before = database.read_bytes()
    # 拉大屏聚合 payload。
    payload = client.get("/dashboard").json()
    # 读库后再拍字节。
    after = database.read_bytes()

    # 有数据且查询成功时 state=normal，前端走正常布局而不是错误页。
    assert payload["state"] == "normal"
    # 源状态 connected，区别于缺文件/锁冲突。
    assert payload["sourceStatus"] == "connected"
    # 刷新间隔 3 秒，与大屏轮询约定一致。
    assert payload["refreshIntervalSeconds"] == 3
    # 把指标条转成 label→value，避免依赖数组下标顺序微调。
    metrics = {item["label"]: item["value"] for item in payload["metrics"]}
    # 站点表一行 → 站点总数 1。
    assert metrics["站点总数"] == "1"
    # 三根桩 → 电桩总数 3。
    assert metrics["电桩总数"] == "3"
    # 非 OFFLINE 为 2/3，格式化成一位小数 66.7。
    assert metrics["设备在线率"] == "66.7"
    # 订单状态 CHARGING 恰好一条。
    assert metrics["充电中订单"] == "1"
    # 今日营收只用 FINISHED 的 1.20，待支付 9.99 排除；字符串保两位小数。
    assert metrics["今日营收"] == "1.20"
    # 在线桩里 CHARGING 占 1/2 → 50.0。
    assert metrics["即时使用率"] == "50.0"
    # 能量口径说明必须从“同桩相邻”起笔，防止把累计电量当小时电量。
    assert payload["metricDefinitions"]["energy"].startswith("同桩相邻")
    # 状态分布最后一项是 OFFLINE 计 1，与桩 103 对应。
    assert payload["pileStatus"][-1] == {"status": "OFFLINE", "count": 1}
    # 遥测列表按时间倒序，[0] 应是桩 101 最新点（重复行不改变最新值）。
    latest = payload["telemetry"][0]
    # 桩主键以字符串返回，避免 JS 大整数问题。
    assert latest["pileId"] == "101"
    # 序列号与夹具一致。
    assert latest["pileSn"] == "PILE-DEMO-01"
    # 站名来自 station_info 联表。
    assert latest["stationName"] == "测试站"
    # 桩当前业务状态 IDLE（遥测最新时桩仍标空闲）。
    assert latest["status"] == "IDLE"
    # 温度取最新遥测 27.0。
    assert latest["temperatureC"] == 27.0
    # 电压 230.0。
    assert latest["voltageV"] == 230.0
    # 电流 30.9。
    assert latest["currentA"] == 30.9
    # 功率瓦 7100。
    assert latest["powerW"] == 7100
    # 前端展示用 kW：7100/1000=7.1。
    assert latest["powerKw"] == 7.1
    # 累计电量保持字符串 "101.00"。
    assert latest["energyKwh"] == "101.00"
    # 来源模式 SIMULATED，大屏要标明不是现场表计。
    assert latest["sourceMode"] == "SIMULATED"
    # 工单第一条为处理中。
    assert payload["tickets"][0]["status"] == "PROCESSING"
    # 营收趋势最后一桶应是当天那笔 1.20。
    assert payload["revenueTrend"][-1]["revenueYuan"] == "1.20"
    # 相邻累计电量差分之和：100.50-100.00 + 101.00-100.50 = 1.0（重复点不加倍）。
    assert sum(item["actual"] for item in payload["energyTrend"]) == 1.0
    # 质量面板：完全重复的遥测计 1 条。
    assert payload["quality"]["checks"]["duplicate"] == 1
    # 四条遥测 source_mode 都是 SIMULATED。
    assert payload["quality"]["checks"]["simulated"] == 4
    # 核心：读前后文件字节一致，接口没有写库。
    assert before == after


# /forecast 必须标明是 3 点移动平均规则估算，不能把历史 actual 原样当成预测。
def test_forecast_is_disclosed_rule_estimate_not_a_copied_actual(
    # 同样注入临时库路径和环境变量补丁。
    tmp_path, monkeypatch
):  # 预测必须披露规则估算，而不是抄 actual。
    # 需要至少三个小时点才能做 3 点平均。
    database = tmp_path / "stage1.sqlite"
    # 灌入与大屏相同的当天三小时样本。
    create_dashboard_database(database)
    # 指向夹具库，避免误读本机 CHARGE_PLATFORM_DB。
    monkeypatch.setenv("CHARGE_PLATFORM_DB", str(database))
    # 空 runtime：本用例测的是 SQLite 3 点均值回落，不能误吃本机 public-v1。
    monkeypatch.setenv("CHARGE_ANALYTICS_RUNTIME", str(tmp_path / "empty-runtime"))
    # 空模型目录：避免本机 evaluation.json 把 modelMode 带偏。
    monkeypatch.setenv("CHARGE_MODEL_DIR", str(tmp_path / "empty-models"))
    # horizon=6 请求未来 6 个点。
    payload = client.get("/forecast?horizon=6").json()
    # 规则估算成功时 status=ok，区别于缺库。
    assert payload["status"] == "ok"
    # 方法名必须写清“3点移动平均规则估算”，答辩不能说成神经网络。
    assert payload["method"] == "3点移动平均规则估算"
    # degradeReason=RULE_BASELINE 表示未用 sklearn 模型、有意降级披露。
    assert payload["degradeReason"] == "RULE_BASELINE"
    # 正好 6 个预测点，与 query 一致。
    assert len(payload["values"]) == 6
    # loadKwh 用字符串，避免前端 JSON 数字把 Decimal 弄丢精度。
    assert all(isinstance(item["loadKwh"], str) for item in payload["values"])


# 缺文件和缺表/缺列必须返回 api-error 和可读中文原因，不能 500 堆栈。
def test_dashboard_reports_missing_and_incompatible_databases(tmp_path, monkeypatch):
    # 指向一个从不创建的路径。
    monkeypatch.setenv("CHARGE_PLATFORM_DB", str(tmp_path / "missing.sqlite"))
    # 缺文件时仍返回 JSON 壳，方便前端 PageState。
    missing = client.get("/dashboard").json()
    # 前端据此切到错误态，而不是用空数组假装正常。
    assert missing["state"] == "api-error"
    # 指标条清空，避免沿用上一次轮询的数字。
    assert missing["metrics"] == []
    # 文案包含“不存在”，运维能立刻知道是路径问题。
    assert "不存在" in missing["message"]

    # 造一个只有残缺 station_info 的旧库，缺少大屏必需列和其它表。
    incompatible = tmp_path / "old.sqlite"
    # 建完立刻 close，避免句柄占用。
    sqlite3.connect(incompatible).execute("CREATE TABLE station_info(id INTEGER)").connection.close()
    # 把分析服务指到这份不兼容库。
    monkeypatch.setenv("CHARGE_PLATFORM_DB", str(incompatible))
    # 再次拉大屏。
    old = client.get("/dashboard").json()
    # 同样走 api-error，不尝试半残查询。
    assert old["state"] == "api-error"
    # 文案包含“结构不兼容”，区别于文件缺失。
    assert "结构不兼容" in old["message"]


# 业务库被独占锁占用时，应返回含 locked 的错误，且测试侧在 finally 里释放锁。
def test_dashboard_returns_clear_busy_error_without_writing(tmp_path, monkeypatch):
    # 先灌一份合法库，再对它加锁。
    database = tmp_path / "busy.sqlite"
    # 完整样例数据。
    create_dashboard_database(database)
    # 指向被锁文件。
    monkeypatch.setenv("CHARGE_PLATFORM_DB", str(database))
    # timeout=0：夹具自己抢锁时不等待。
    writer = sqlite3.connect(database, timeout=0)
    # EXCLUSIVE 事务让其它连接（包括只读）也拿不到锁。
    writer.execute("BEGIN EXCLUSIVE")
    # 查询可能因锁失败，但必须在 finally 释放，否则临时文件删不掉。
    try:
        # 只读接口遇到 database is locked，应转成 api-error JSON 而不是把异常甩给 TestClient。
        payload = client.get("/dashboard").json()
    finally:  # 无论查询是否因锁失败，都必须释放独占事务。
        # 回滚独占事务。
        writer.rollback()
        # 关闭写连接。
        writer.close()
    # 锁冲突与缺文件一样走 api-error。
    assert payload["state"] == "api-error"
    # SQLite 英文 locked 应出现在 message 里（大小写不敏感），方便日志检索。
    assert "locked" in payload["message"].lower()


# 表在但一行数据都没有时，应返回 empty 和合法空数组/零营收，而不是 api-error。
def test_empty_database_returns_legal_empty_structures(tmp_path, monkeypatch):
    # 空库文件名单独区分。
    database = tmp_path / "empty.sqlite"
    # 只建表、不 INSERT。
    create_dashboard_database(database, with_rows=False)
    # 指向空库。
    monkeypatch.setenv("CHARGE_PLATFORM_DB", str(database))
    # 拉大屏。
    payload = client.get("/dashboard").json()
    # empty 让前端显示“暂无数据”，与故障态区分。
    assert payload["state"] == "empty"
    # 能量趋势无点。
    assert payload["energyTrend"] == []
    # 遥测列表为空。
    assert payload["telemetry"] == []
    # 告警为空。
    assert payload["alarms"] == []
    # 工单为空。
    assert payload["tickets"] == []
    # 指标条第 6 项是今日营收，空库应为 "0.00" 字符串。
    assert payload["metrics"][5]["value"] == "0.00"


# 场景目录必须列出全部 9 个 id、声明不写业务库，并给出答辩轮播五步。
def test_simulator_catalog_lists_all_required_scenarios():
    # GET /simulator/scenarios 来自 simulator_control.catalog。
    payload = client.get("/simulator/scenarios").json()
    # 顺序与 SCENARIOS 元组一致，前端按钮按此渲染。
    assert [item["id"] for item in payload["scenarios"]] == [
        # 前五个是正常闭环：在线→充电→过热→离线→恢复。
        "normal", "charging", "overheat", "offline", "recover",
        # 后四个是协议异常：重复、时间倒退、电量倒退、未知桩。
        "duplicate", "time-backward", "energy-backward", "unknown-pile",
    ]  # 九个 id 与 simulator_control.SCENARIOS 顺序一致。
    # 分析服务切换场景不得 SQL 写业务库，只拉起 pile_simulator。
    assert payload["writesBusinessDatabase"] is False
    # 一键轮播顺序，给 ScenarioSwitcher 用。
    assert payload["cycle"] == ["normal", "charging", "overheat", "offline", "recover"]


# 未知场景名必须 400，且不应把模拟器拉起来。
def test_simulator_rejects_unknown_scenario_without_launching():
    # 清掉上一条用例可能残留的 busy 状态。
    from src.simulator_control import reset_for_tests
    # 重置内存里的 _status。
    reset_for_tests()
    # explode 不在 SCENARIO_IDS 里。
    response = client.post("/simulator/scenario", json={"scenario": "explode"})
    # FastAPI 把 ValueError 转成 400，body 里是未知场景说明。
    assert response.status_code == 400


# 具名切换 charging 时，即使跑完状态机，业务 sqlite 字节也必须不变。
def test_simulator_named_switch_does_not_write_sqlite(tmp_path, monkeypatch):
    # 导入模块以便 patch _run_one 和 sleep，避免真的起 C++ 子进程。
    from src import simulator_control
    # 清忙闲标志。
    simulator_control.reset_for_tests()
    # 子进程启动替换成立刻成功（返回码 0），本测试只关心 FastAPI 会不会写库。
    monkeypatch.setattr(simulator_control, "_run_one", lambda name, pile_id: 0)
    # 场景间隔 sleep 去掉，让后台线程马上结束。
    monkeypatch.setattr(simulator_control.time, "sleep", lambda _seconds: None)
    # 准备一份有数据的库，用来对比字节。
    database = tmp_path / "stage1.sqlite"
    # 灌数。
    create_dashboard_database(database)
    # 指向夹具库（校准电表状态文件时可能只读它，但仍不应写入）。
    monkeypatch.setenv("CHARGE_PLATFORM_DB", str(database))
    # 切换前快照。
    before = database.read_bytes()
    # POST charging：应 ok 且声明不写库。
    payload = client.post("/simulator/scenario", json={"scenario": "charging"}).json()
    # 启动被接受。
    assert payload["ok"] is True
    # 响应里再次声明不写业务库。
    assert payload["writesBusinessDatabase"] is False
    # 最多轮询约 2 秒，等后台线程把 busy 清掉。
    for _ in range(40):
        # 读当前模拟器状态。
        status = client.get("/simulator/status").json()
        # 不忙就可以做最终断言。
        if not status["busy"]:
            # 离开等待环。
            break
        # 50ms 间隔，给线程调度时间。
        time.sleep(0.05)
    # 线程应已结束，否则后面的字节对比没有意义（仍可能只读打开）。
    assert status["busy"] is False
    # 与大屏读库测试相同：场景按钮路径也不能改 sqlite 文件。
    assert before == database.read_bytes()


def _write_tiny_public_curated(runtime):
    # 最少几行小时表，只够 /forecast 取“最后一小时”，不在本用例里训练。
    curated = runtime / "curated" / "public-v1"
    curated.mkdir(parents=True, exist_ok=True)
    csv_text = (
        "station_id,hour_start,order_count,load_kwh,average_duration_seconds,available_piles,hour,weekday,is_holiday,data_mode,dataset_name,dataset_version,ingest_batches\n"
        "1,2026-01-08T22:00:00+08:00,1,5.00,1800,4,22,3,0,PUBLIC_AGGREGATED,public-charge-events,public-v1,public-20260910-001\n"
        "1,2026-01-08T23:00:00+08:00,1,6.00,1800,4,23,3,0,PUBLIC_AGGREGATED,public-charge-events,public-v1,public-20260910-001\n"
        "2,2026-01-08T22:00:00+08:00,1,4.00,1800,3,22,3,0,PUBLIC_AGGREGATED,public-charge-events,public-v1,public-20260910-001\n"
        "2,2026-01-08T23:00:00+08:00,1,4.50,1800,3,23,3,0,PUBLIC_AGGREGATED,public-charge-events,public-v1,public-20260910-001\n"
        "3,2026-01-08T22:00:00+08:00,1,3.00,1800,2,22,3,0,PUBLIC_AGGREGATED,public-charge-events,public-v1,public-20260910-001\n"
        "3,2026-01-08T23:00:00+08:00,1,3.20,1800,2,23,3,0,PUBLIC_AGGREGATED,public-charge-events,public-v1,public-20260910-001\n"
    )
    (curated / "hourly_station.csv").write_text(csv_text, encoding="utf-8")
    (curated / "metadata.json").write_text(
        '{"row_count":6,"rejected_count":0,"coverage_start":"2026-01-08T22:00:00+08:00","coverage_end":"2026-01-08T23:00:00+08:00","data_mode":"PUBLIC_AGGREGATED","dataset_name":"public-charge-events","dataset_version":"public-v1","input_batches":["public-20260910-001"]}',
        encoding="utf-8",
    )


def test_health_lists_horizon_modes_and_source_fields(tmp_path, monkeypatch):
    monkeypatch.setenv("CHARGE_ANALYTICS_RUNTIME", str(tmp_path / "runtime"))
    monkeypatch.setenv("CHARGE_MODEL_DIR", str(tmp_path / "models"))
    monkeypatch.setenv("CHARGE_PLATFORM_DB", str(tmp_path / "missing.sqlite"))
    payload = client.get("/health").json()
    assert payload["status"] == "ok"
    assert payload["horizonModes"] == {"1": "baseline", "6": "baseline", "24": "baseline"}
    assert payload["dataMode"] == "SIMULATED"
    assert "datasetVersion" in payload and "updatedAt" in payload
    assert payload["sampleCount"] == 0


def test_forecast_from_curated_includes_capacity_and_range(tmp_path, monkeypatch):
    runtime = tmp_path / "runtime"
    models = tmp_path / "models"
    models.mkdir()
    _write_tiny_public_curated(runtime)
    monkeypatch.setenv("CHARGE_ANALYTICS_RUNTIME", str(runtime))
    monkeypatch.setenv("CHARGE_MODEL_DIR", str(models))
    payload = client.get("/forecast?horizon=1").json()
    assert payload["status"] == "ok"
    assert payload["method"] == "昨日同窗口基线"
    assert payload["degradeReason"] == "MODEL_NOT_TRAINED"
    assert payload["capacity"]["note"]
    assert payload["predictedRange"]["end"]
    assert len(payload["stations"]) == 3
    assert len(payload["values"]) == 1
    assert all(item["loadKwh"] for item in payload["stations"])
    assert all(float(item["loadKwh"]) >= 0 for item in payload["stations"])
    six = client.get("/forecast?horizon=6").json()
    assert six["horizonHours"] == 6 and len(six["values"]) == 6
    twenty_four = client.get("/forecast?horizon=24").json()
    assert twenty_four["horizonHours"] == 24 and len(twenty_four["values"]) == 24


def test_forecast_missing_model_file_falls_back_to_baseline(tmp_path, monkeypatch):
    runtime = tmp_path / "runtime"
    models = tmp_path / "models"
    models.mkdir()
    _write_tiny_public_curated(runtime)
    (models / "evaluation.json").write_text(
        '{"evaluations":[{"horizon_hours":1,"selected_mode":"trained","model_mae":1,"baseline_mae":2,"sample_count":10}]}',
        encoding="utf-8",
    )
    monkeypatch.setenv("CHARGE_ANALYTICS_RUNTIME", str(runtime))
    monkeypatch.setenv("CHARGE_MODEL_DIR", str(models))
    payload = client.get("/forecast?horizon=1").json()
    assert payload["modelMode"] == "baseline"
    assert payload["degradeReason"] == "MODEL_FILE_MISSING"
    assert payload["stations"]


def test_recommend_returns_summary_without_paths_or_personal_ids(tmp_path, monkeypatch):
    monkeypatch.setenv("CHARGE_ANALYTICS_RUNTIME", str(tmp_path / "runtime"))
    monkeypatch.setenv("CHARGE_MODEL_DIR", str(tmp_path / "models"))
    payload = client.get("/recommend").json()
    assert payload["status"] == "ok"
    assert payload["inputSummary"]["weights"]["predictedLoad"] == "0.20"
    assert payload["inputSummary"]["note"]
    assert "databasePath" not in payload
    blob = str(payload)
    assert "phone" not in blob.lower() and "身份证" not in blob
    degraded = client.get("/recommend?model_available=false").json()
    assert degraded["modelMode"] == "baseline"
    assert degraded["degradeReason"] == "MODEL_UNAVAILABLE"
    assert "predictedLoad" not in degraded["inputSummary"]["weights"]

# 【文件讲解·答辩用】

# 功能作用：这是成员3大屏后端的接口验收清单。覆盖健康检查只读声明、指标口径与
#   Decimal 营收、规则预测披露、缺库/结构不兼容/锁冲突/空库四种页面态，以及模拟器
#   场景目录、拒未知名、切换不写库。答辩演示大屏前，应先让这组测试全绿。
# 技术原理：FastAPI TestClient 在同一进程调路由，不占 8000 端口。CHARGE_PLATFORM_DB
#   用 monkeypatch 指到 tmp_path 里现搭的 SQLite，与 sqlite_dashboard 的只读打开方式
#   相同。营收用字符串 "1.20" 是因为金额走 Decimal 文本，禁止 float。能量趋势用同桩
#   相邻累计电量差分，所以夹具造 100.00→100.50→101.00。重复遥测测质量计数。
#   锁用例用 BEGIN EXCLUSIVE 模拟成员1正在写库。模拟器用 patch 掉 _run_one，把
#   “会不会写库”从“子进程是否存在”里拆出来，专门打 FastAPI 边界。
# 代码思路：create_dashboard_database 是共用夹具——六张表对齐生产库，样例时间钉在
#   东八区当天，避免午夜跨日。每个测试只改一个风险点：只读元数据、指标与字节不变、
#   预测方法名、错误态文案、空结构、场景目录、400、切换后字节不变。
# 与其他文件关系：被测 app 在 analytics/src/api.py；SQL 聚合在 sqlite_dashboard.py；
#   场景启停在 simulator_control.py，真正的桩程序在 simulator/pile。前端 App.vue
#   轮询 /dashboard，ScenarioSwitcher.vue 调 /simulator/scenario。test_service_isolation.py
#   从源码扫描禁写；本文件从运行时比文件字节。流水线测试（normalize/dataset/model）
#   不经过这些 HTTP 路由。
# 答辩讲法：打开 test_dashboard_uses_required_metric_definitions_and_decimal_revenue，
#   讲“今日营收为什么是 1.20 不是 11.19”——因为待支付不算。再指 before==after：
#   “分析进程读完库，文件一个字节都没变。”预测用例强调方法名和 RULE_BASELINE，
#   不要说成深度学习。场景用例强调 writesBusinessDatabase is False：按钮只启动
#   pile_simulator 走 TCP，不在 Python 里 UPDATE 订单。被问空库和坏库，分别对应
#   empty 与 api-error 两种前端态。
