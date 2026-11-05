from __future__ import annotations  # 类型注解延后求值，兼容 3.10。

from collections import Counter  # 按拒绝码统计条数，用于质量报告里的 rejection_reasons。
from typing import Any  # 接受清洗后的 dict 记录，不把字段钉死成 TypedDict。

REASON_CATEGORIES = {  # D8 要求的拒绝大类；未列入的码记入 other_rejected_count。
    "duplicate": frozenset({"DUPLICATE"}),  # 同源记录号重复。
    "missing": frozenset({"MISSING_REQUIRED"}),  # 必填空缺。
    "unmapped": frozenset({"UNMAPPED_STATION"}),  # 人工映射表没有该来源站。
    "unit_error": frozenset({"INVALID_ENERGY_UNIT", "INVALID_DURATION_UNIT", "INVALID_AMOUNT_UNIT"}),  # 单位不认或数值非法。
    "privacy": frozenset({"PRIVACY_FIELD"}),  # 行里出现隐私列名。
}  # 结束拒绝大类。


def _category_counts(reasons: dict[str, int]) -> dict[str, int]:  # 把细码折成大类计数。
    counts = {name: sum(reasons.get(code, 0) for code in codes) for name, codes in REASON_CATEGORIES.items()}  # 各大类求和。
    categorized = sum(counts.values())  # 已归类条数。
    counts["other"] = sum(reasons.values()) - categorized  # INVALID_STATUS 等进 other。
    return counts  # 给报告字段用。


def _primary_reason(item: dict[str, Any]) -> str:  # 兼容两种记账形态：charge_event_v2 用 reason_code，historical_interval_v1 用 primary_reason（主原因唯一）。
    return str(item.get("reason_code") or item.get("primary_reason") or "")  # 取主原因；都没有则空串。


def build_quality_report(input_count: int, accepted: list[dict[str, Any]], rejected: list[dict[str, Any]]) -> dict[str, Any]:  # 输入条数必须等于通过+拒绝；主原因计数必须等于拒绝列表长度；诊断标签单独计数不参与对账。
    reasons = dict(sorted(Counter(filter(None, (_primary_reason(item) for item in rejected))).items()))  # 各主原因出现次数，键排序方便对比测试。
    if input_count != len(accepted) + len(rejected):  # 有行既没进 accepted 也没进 rejected 就是流水线丢数。
        raise ValueError("QUALITY_RECONCILIATION_FAILED")  # 对账失败直接抛，禁止写出虚假有效率。
    if sum(reasons.values()) != len(rejected):  # 每条拒绝必须恰有一个主原因，否则 Counter 对不上。
        raise ValueError("REJECTION_REASON_RECONCILIATION_FAILED")  # 拒绝原因对账失败。
    diagnostics = Counter(  # 诊断标签只做观察，不改变拒绝计数（S2-T04：诊断多标签不重复计拒绝数）。
        str(tag)  # 标签名。
        for item in rejected  # 逐条拒绝。
        for tag in (item.get("diagnostic_tags") or [])  # 可空标签列表。
    )  # 结束：诊断标签计数。
    categories = _category_counts(reasons)  # 大类计数。
    if sum(categories.values()) != len(rejected):  # 大类加 other 必须仍等于拒绝总数。
        raise ValueError("REJECTION_CATEGORY_RECONCILIATION_FAILED")  # 分类对账失败。
    return {  # 给 staging/quality.json 和大屏 quality 面板用的汇总。
        "input_count": input_count, "accepted_count": len(accepted),  # 原始输入条数与清洗通过条数。
        "rejected_count": len(rejected), "valid_rate": len(accepted) / input_count if input_count else 0.0,  # 空输入有效率记 0，避免除零。
        "rejection_reasons": reasons,  # 主拒绝原因用于守恒式对账。
        "diagnostic_tags": dict(sorted(diagnostics.items())),  # 多标签诊断仅用于观察。
        "reconciled": True,  # reconciled=True 表示主原因与拒绝总数对账通过。
        "duplicate_count": categories["duplicate"],  # 重复。
        "missing_count": categories["missing"],  # 缺失。
        "unmapped_count": categories["unmapped"],  # 未映射。
        "unit_error_count": categories["unit_error"],  # 单位错误。
        "privacy_count": categories["privacy"],  # 隐私命中。
        "other_rejected_count": categories["other"],  # 其余拒绝码。
    }  # 结束：给 staging/quality.json 和大屏 quality 面板用的汇总。


def enforce_min_valid_rate(report: dict[str, Any], minimum: Any) -> None:  # 阈值来自数据集清单，不在代码里写死统一比例。
    if minimum in (None, ""):  # 清单未声明则不拦。
        return  # 教学公开聚合批次可以不设门槛。
    if float(report["valid_rate"]) < float(minimum):  # 有效率低于清单要求。
        raise ValueError("QUALITY_THRESHOLD_FAILED")  # D8：阈值失败不得继续写 curated 当正式集。

# 【文件讲解·答辩用】
# 功能作用：把清洗结果收成可对账的质量报告：输入、通过、拒绝、有效率、拒绝码与五大类计数。
# 技术原理：守恒式校验 input = accepted + rejected；Counter 校验细码；大类加 other 再等于拒绝数。
# 代码思路：先统计再断言再返回；失败抛错而不是把 reconciled 写成 False 继续跑。min_valid_rate 只在清单提供时检查。
# 与其他文件关系：demo_pipeline 在 normalize_csv 之后调用；夹具 runner 再调用 enforce_min_valid_rate。
# 答辩时可以怎么讲：我们不修数据，只记账；6 进 2 过 4 拒必须能加回来，重复/缺失/未映射/单位各 1 条。
