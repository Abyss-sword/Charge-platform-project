# pytest 用来断言数量对不上时必须抛 RECONCILIATION，而不是给出虚假有效率。
import pytest

# build_quality_report 是归一化之后、模型训练之前的数量守恒关口。
from src.quality import build_quality_report


# 输入 3 条、通过 1 条、拒绝 2 条时，应对账成功且有效率严格等于 1/3。
def test_quality_reconciles_counts_and_reasons():
    # 拒绝列表带两种不同 reason_code，报告里应能按原因汇总且条数加总得 2。
    report = build_quality_report(3, [{"id": 1}], [{"reason_code": "DUPLICATE"}, {"reason_code": "MISSING_REQUIRED"}])
    # reconciled 为真表示 input = accepted + rejected；valid_rate 用真除法而不是四舍五入百分比。
    assert report["reconciled"] and report["valid_rate"] == 1 / 3
    assert report["duplicate_count"] == 1
    assert report["missing_count"] == 1
    assert report["unmapped_count"] == 0
    assert report["unit_error_count"] == 0
    assert report["privacy_count"] == 0
    assert report["duplicate_count"] + report["missing_count"] + report["unmapped_count"] + report["unit_error_count"] + report["privacy_count"] + report["other_rejected_count"] == report["rejected_count"]


# 声称输入 4 条但只给出 1 条通过 + 1 条拒绝，必须立刻失败，禁止带着错账继续训练。
def test_quality_stops_on_mismatch():
    # 源码抛 ValueError("QUALITY_RECONCILIATION_FAILED")，match 用 RECONCILIATION 覆盖该前缀。
    with pytest.raises(ValueError, match="RECONCILIATION"):
        # 4 ≠ 1+1，对账失败；调用方（demo_pipeline）会因此中止而不是静默写成 quality.json。
        build_quality_report(4, [{"id": 1}], [{"reason_code": "DUPLICATE"}])

# 【文件讲解·答辩用】
# 功能作用：质量报告必须能对上账。清洗说“进了 3 条、过了 1 条、拒了 2 条”，数字必须
#   加得上，有效率必须是 1/3，对不上就停。这避免后面模型用一份说不清来源的样本集。
# 技术原理：build_quality_report(input_count, accepted, rejected) 做两层守恒：
#   input_count == len(accepted)+len(rejected)，以及拒绝原因计数之和 == len(rejected)。
#   用 Counter 按 reason_code 汇总。valid_rate = accepted/input（input 为 0 时给 0.0）。
#   失败抛 ValueError，不返回“凑合能看”的报告。pytest.raises 锁定失败模式。
# 代码思路：第一条是黄金账——3、1、2 和两种原因。第二条故意把 input 改成 4，证明
#   函数不会自己“补一条”或忽略差额。测试传入的 dict 只需要 id / reason_code，
#   因为质量模块不解释业务字段，只数条数。
# 与其他文件关系：实现在 analytics/src/quality.py。normalize_csv 产出 accepted/rejected，
#   demo_pipeline.run_pipeline 把 input_count 和这两份列表送进本函数，再写入
#   staging/.../quality.json。test_normalize.py 测六类拒绝码；本文件不重复测码表，
#   只测账能不能平。test_model.py 会断言 quality["reconciled"] is True。
# 答辩讲法：先写 1/3 在黑板上：“有效率不是拍脑袋的 90%，是通过对账的分数”。再说明
#   对不上就抛 RECONCILIATION，训练不准开始。被问拒绝原因时，可转到 test_normalize
#   的 DUPLICATE、MISSING_REQUIRED 等六类，本文件只借用其中两个做计数。
