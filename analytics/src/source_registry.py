from __future__ import annotations  # 允许 dict[str, Any] 等注解在 3.10 运行。

import json  # 来源清单是 UTF-8 JSON，字段与教学准入契约对齐。
from dataclasses import dataclass  # 冻结清单对象，防止清洗过程中被改字段。
from pathlib import Path  # 按路径读清单文件。
from typing import Any  # 清单值类型不齐，用 Any 承接 JSON。

ALLOWED_MODES = {"SIMULATED", "REAL_ANONYMIZED", "PUBLIC_AGGREGATED"}  # 只承认模拟、真实脱敏、公开聚合三种模式。
PRIVACY_FIELDS = {  # 表头或清单 fields 里出现这些名字就拒绝，防止手机号姓名车牌进分析。
    "phone", "phone_number", "mobile", "name", "full_name", "payment_account",  # 身份与支付账号。
    "user_id", "userid", "account", "account_id",  # 个人标识：会话数据里的 userId 可重识别（T03 真实数据实测补充）。
    "vehicle_id", "vin", "license_plate", "plate", "trajectory_id", "continuous_trajectory"  # 车辆与连续轨迹。
}  # 结束：表头或清单 fields 里出现这些名字就拒绝，防止手机号姓名车牌进分析。
REQUIRED_MANIFEST_FIELDS = {  # 来源准入必须声明的字段，缺一不可。
    "dataset_name", "provider", "source_url", "allowed_uses", "prohibited_uses",  # 名称、提供方、地址、用途正反面。
    "acquired_at", "data_mode", "dataset_version", "coverage_start", "coverage_end",  # 获取时间、模式、版本、覆盖区间。
    "redistribution_allowed", "retention_policy", "privacy_boundary", "ingest_batch"  # 再分发、留存、隐私边界、批次号。
}  # 结束：来源准入必须声明的字段，缺一不可。


class AdmissionError(ValueError):  # 专用异常：清单或来源文件不合格，ingest_raw 捕获后不再写入 raw。
    pass  # 专用异常：清单或来源文件不合格，ingest_raw 捕获后不再写入 raw。


@dataclass(frozen=True)  # 冻结清单，清洗过程中不能改字段。
class SourceManifest:  # 校验通过后的完整清单副本。
    values: dict[str, Any]  # 校验通过后的完整清单副本。

    @property  # 批次号快捷访问。
    def batch(self) -> str:  # 批次目录名，对应 runtime/raw/<batch>。
        return str(self.values["ingest_batch"])  # 批次目录名，对应 runtime/raw/<batch>。

    @property  # 数据模式快捷访问。
    def mode(self) -> str:  # SIMULATED / REAL_ANONYMIZED / PUBLIC_AGGREGATED。
        return str(self.values["data_mode"])  # SIMULATED / REAL_ANONYMIZED / PUBLIC_AGGREGATED。


def validate_manifest(data: dict[str, Any]) -> SourceManifest:  # 校验必填、模式、教学用途、时间范围、模拟不得冒充真实、脱敏源不得声明隐私字段。
    missing = sorted(k for k in REQUIRED_MANIFEST_FIELDS if data.get(k) in (None, "", []))  # 空串空列表都算缺字段。
    if missing:  # 有缺失就不能准入。
        raise AdmissionError("missing manifest fields: " + ", ".join(missing))  # 把缺的字段名拼进异常，方便改 JSON。
    if data["data_mode"] not in ALLOWED_MODES:  # 未知模式一律拒绝。
        raise AdmissionError("unsupported data_mode")  # 防止把“半真实”这种含糊标签写进来。
    if not isinstance(data["allowed_uses"], list) or "teaching-analysis" not in data["allowed_uses"]:  # 必须明确允许教学分析。
        raise AdmissionError("teaching-analysis is not an allowed use")  # 用途对不上就停。
    if data["coverage_start"] > data["coverage_end"]:  # 覆盖起点不能晚于终点（字符串按 ISO 比较）。
        raise AdmissionError("coverage_start is after coverage_end")  # 时间范围自相矛盾。
    if data["data_mode"] == "SIMULATED" and data.get("claims_real_data") is True:  # 模拟数据不得声称真实。
        raise AdmissionError("simulated data cannot claim to be real")  # 答辩口径：教学库是模拟。
    if data["data_mode"] == "PUBLIC_AGGREGATED" and data.get("claims_real_data") is True:  # 公开聚合也不得冒充个人脱敏真源。
        raise AdmissionError("public aggregated data cannot claim to be real")  # 防止把聚合样本改名为 REAL_ANONYMIZED。
    declared = {str(v).lower() for v in data.get("fields", [])}  # 清单里声明的字段名小写集合。
    if data["data_mode"] in {"REAL_ANONYMIZED", "PUBLIC_AGGREGATED"} and declared.intersection(PRIVACY_FIELDS):  # 真源与公开聚合都不得声明隐私列。
        raise AdmissionError("real anonymized source declares privacy fields")  # 边界写在清单里，不靠事后删列。
    return SourceManifest(dict(data))  # 拷贝一份再冻结，调用方改原 dict 不影响清单。


def load_manifest(path: str | Path) -> SourceManifest:  # 读文件 → JSON → 同一套校验。
    return validate_manifest(json.loads(Path(path).read_text(encoding="utf-8")))  # 读文件 → JSON → 同一套校验。

# 【文件讲解·答辩用】
# 功能作用：数据进流水线前的准入闸门：模式、用途、时间、隐私字段都必须说清楚。
# 技术原理：把 JSON 清单校验成不可变 SourceManifest；不合格抛 AdmissionError，ingest 不会 copy 源文件。
# 代码思路：必填集合差集找缺失；白名单限制 data_mode；模拟与真实声称互斥；脱敏源交叉检查 PRIVACY_FIELDS。
# 与其他文件关系：ingest_raw.load_manifest 与 PRIVACY_FIELDS；normalize 再用同一隐私字段集合扫表头。
# 答辩时可以怎么讲：我们先问“这批数据从哪来、能不能用于教学、有没有手机号”，问不清楚就不进入 raw 固化。
