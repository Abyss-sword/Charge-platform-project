# normalize_row 测单行单位换算；normalize_csv 测整表六类质量分流。
from src.normalize import normalize_csv, normalize_row


# 清洗时写入的血缘：入库时间、模拟模式、数据集名版本、批次号，会原样落到输出行。
META = {"ingested_at": "2026-09-05T09:00:00+08:00", "data_mode": "SIMULATED", "dataset_name": "demo", "dataset_version": "v2", "ingest_batch": "b1"}


# 默认构造一条单位齐全的合法事件，调用方可只覆盖要测的字段。
def row(**changes):
    # 站点 A、东八区时间、7.2 kWh、30 分钟、12.5 元、completed，对应后面的 FINISHED 映射。
    data = {"source_record_id": "1", "source_station_id": "A", "event_time": "2026-09-03T09:00:00+08:00", "timezone": "Asia/Shanghai", "energy": "7.2", "energy_unit": "kwh", "duration": "30", "duration_unit": "minutes", "amount": "12.5", "amount_unit": "yuan", "event_status": "completed"}
    # 用关键字覆盖默认值，例如 energy="7.2e0" 或 event_time=""。
    data.update(changes)
    # 返回可变 dict，normalize_row 不会要求冻结结构。
    return data


# 显式 kWh / 分钟 / 元 应换算成站内 id、两位电量字符串、秒、金额字符串和 FINISHED。
def test_normalize_explicit_units():
    # 映射 A→1；META 带入血缘。这是六类质量之前的“快乐路径”。
    result = normalize_row(row(), {"A": 1}, META)
    # 7.2→"7.20"，30 分钟→1800 秒，12.5→"12.50"，completed→FINISHED；站点必须是映射后的 1。
    assert (result["station_id"], result["energy_kwh"], result["duration_seconds"], result["amount_yuan"], result["status"]) == (1, "7.20", 1800, "12.50", "FINISHED")


# 五条输入里只有第一条合法，其余四条应分别落入重复、缺字段、未映射站点、非法电量单位。
def test_six_quality_classes(tmp_path):
    # 把待测 CSV 放在 pytest 临时目录，避免写进仓库。
    source = tmp_path / "input.csv"
    # 表头取合法行的全部键，保证 DictWriter 列齐全。
    headers = list(row().keys())
    # 行0合法；行1同 source_record_id=1 触发 DUPLICATE；行2空时间 MISSING_REQUIRED；行3站点 X UNMAPPED_STATION；行4 joule INVALID_ENERGY_UNIT。
    rows = [row(), row(source_record_id="1"), row(source_record_id="2", event_time=""), row(source_record_id="3", source_station_id="X"), row(source_record_id="4", energy_unit="joule")]
    # 测试文件内现导 csv，只为写这一份夹具，不污染模块顶层。
    import csv
    # newline="" 是 Windows 上 csv 模块的常规写法，防止多出空行。
    with source.open("w", encoding="utf-8", newline="") as stream:
        # 先写表头再写五行，normalize_csv 按行调用 normalize_row 并捕获 ValueError。
        writer = csv.DictWriter(stream, fieldnames=headers); writer.writeheader(); writer.writerows(rows)
    # 只映射 A→1，站点 X 必然未映射；返回 (accepted, rejected) 两份列表。
    accepted, rejected = normalize_csv(source, {"A": 1}, META)
    # 五行只留第一条，证明重复和三类错误都没有混进 accepted。
    assert len(accepted) == 1
    # 四个 reason_code 必须恰好这四个，缺一个或多一个都说明分流表改坏了。
    assert {item["reason_code"] for item in rejected} == {"DUPLICATE", "MISSING_REQUIRED", "UNMAPPED_STATION", "INVALID_ENERGY_UNIT"}


# v2 契约拒绝科学计数法电量和字面负金额，防止 Decimal 解析出教学上不认可的值。
def test_v2_decimal_input_rejects_scientific_and_negative_values():
    # 本用例才用到 pytest.raises，所以在函数内导入。
    import pytest
    # 7.2e0 能被 float 读懂，但 PLAIN_DECIMAL 正则不收，应标成非法电量单位/格式。
    with pytest.raises(ValueError, match="INVALID_ENERGY_UNIT"):
        # 单行入口直接抛，不会写进 rejected 列表（那是 normalize_csv 的包装）。
        normalize_row(row(energy="7.2e0"), {"A": 1}, META)
    # 金额 -0.00 带负号，同样不是普通非负十进制。
    with pytest.raises(ValueError, match="INVALID_AMOUNT_UNIT"):
        # 与电量规则对称：金额也只接受普通非负十进制文本。
        normalize_row(row(amount="-0.00"), {"A": 1}, META)

# 【文件讲解·答辩用】
# 功能作用：归一化是分析契约的心脏。外来 CSV 的单位、状态、站点编号都不统一，本文件
#   证明：合法行会换成站内 id、kWh、秒、元、FINISHED；重复、缺字段、未知站点、非法
#   单位会进拒绝列表；科学计数法和负数金额在 v2 直接拒绝。
# 技术原理：normalize_row 校验必填、隐私列、站点映射、时间时区、正则约束的十进制、
#   单位换算和状态映射。电量/金额用 Decimal 量化成两位字符串，避免 JSON/Java/Python
#   各打各的 float。normalize_csv 顺序读行，已见过的 source_record_id 记 DUPLICATE，
#   其它 ValueError 的 args[0] 变成 reason_code。row() 夹具保证每条用例只改一个字段。
# 代码思路：先测快乐路径五个字段打包断言；再造五连 CSV 覆盖四类拒绝（第五类是合法）；
#   最后单独打科学计数法和 -0.00。六类质量在课程文档里还有时间倒序等，本文件覆盖
#   清洗模块实际实现的这几类码。
# 与其他文件关系：实现 normalize.py；站点映射 station_mapping.py；隐私字段来自
#   source_registry.PRIVACY_FIELDS。demo_pipeline 在 ingest 之后调用 normalize_csv。
#   test_quality.py 只对数，不管码表；test_dataset.py 吃的是本模块产出的 energy_kwh
#   字符串。契约文件 charge_event_v2.schema.json 描述清洗后字段。
# 答辩讲法：先展示 30 分钟变 1800 秒、7.2 变成 "7.20"。再打开五连 CSV：“重复 id、
#   空时间、未知站、焦耳单位，各进各的拒绝码，最后只留下 1 条。”被问为什么拒 7.2e0，
#   答 v2 要可人工对账的十进制文本，科学计数法在跨语言时容易读错。
