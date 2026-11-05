# pytest.raises 用来断言清单校验失败时抛出 AdmissionError，而不是静默放过。
import pytest

# AdmissionError 是准入失败类型；validate_manifest 是来源清单的唯一入口。
from src.source_registry import AdmissionError, validate_manifest


# 构造一份字段齐全、明确声明 SIMULATED 的合法清单，给后面三条用例当底板。
def valid_manifest():
    # 返回教学演示用的完整 manifest，后续用例只改要测的那一个字段。
    return {
        # 数据集名和提供方、来源 URL 都是必填，缺任一会在 missing fields 里报出来。
        "dataset_name": "demo", "provider": "member3", "source_url": "local://demo",
        # 允许用途必须含 teaching-analysis；禁止用途声明 profiling，避免被当成真实画像数据。
        "allowed_uses": ["teaching-analysis"], "prohibited_uses": ["profiling"],
        # acquired_at 记录获取时间；data_mode 必须是三种合法模式之一，这里用模拟。
        "acquired_at": "2026-09-05T09:00:00+08:00", "data_mode": "SIMULATED",
        # 版本和覆盖区间用于血缘；覆盖结束不得早于开始（由 validate_manifest 检查）。
        "dataset_version": "v1", "coverage_start": "2026-09-01", "coverage_end": "2026-09-02",
        # 再分发、留存策略、隐私边界都是课程要求的声明项，不能空着混过去。
        "redistribution_allowed": True, "retention_policy": "course", "privacy_boundary": "none",
        # ingest_batch 标识本批；fields 列出将入库的列，这里只有业务主键。
        "ingest_batch": "batch-1", "fields": ["source_record_id"]
        # 合法清单到此结束，调用方拿到的是普通 dict，不是已经校验过的对象。
    }


# 合法模拟清单应被接受，并且 mode 属性读出来仍是 SIMULATED。
def test_manifest_accepts_explicit_simulation():
    # 验证完整 SIMULATED 清单能通过准入，且 SourceManifest.mode 与声明一致。
    assert validate_manifest(valid_manifest()).mode == "SIMULATED"


# 缺必填字段、以及模拟数据却声称真实，都必须被 AdmissionError 拦住。
def test_manifest_rejects_missing_fields_and_false_real_claim():
    # 先拷一份合法清单，再删掉 provider，专门测“缺必填项”。
    data = valid_manifest(); data.pop("provider")
    # 期望异常信息里点名 provider，证明报错不是笼统的“校验失败”。
    with pytest.raises(AdmissionError, match="provider"):
        # 缺提供方时不得生成 SourceManifest，否则后续流水线会把来源写成空。
        validate_manifest(data)
    # 重新拿合法清单，贴上 claims_real_data=True，测“模拟数据冒充真实”。
    data = valid_manifest(); data["claims_real_data"] = True
    # 源码拒绝 cannot claim，防止答辩材料把教学模拟说成现场采集。
    with pytest.raises(AdmissionError, match="cannot claim"):
        # 带虚假真实声明的清单同样不能过准入。
        validate_manifest(data)


# REAL_ANONYMIZED 模式若 fields 里出现手机号等隐私列，必须在清单阶段就拒绝。
def test_real_anonymized_rejects_declared_private_fields():
    # 把模式改成真实匿名，同时把字段改成 phone_number，制造隐私冲突。
    data = valid_manifest(); data["data_mode"] = "REAL_ANONYMIZED"; data["fields"] = ["phone_number"]
    # 期望异常匹配 privacy，与 source_registry 里 PRIVACY_FIELDS 检查对齐。
    with pytest.raises(AdmissionError, match="privacy"):
        # 隐私字段出现在声明列表时，连 raw 拷贝都不应开始。
        validate_manifest(data)

# 缺覆盖时间和版本时必须拒绝，这是 D7 准入清单的硬字段。
def test_manifest_rejects_missing_coverage_and_version():
    missing_coverage = valid_manifest(); missing_coverage.pop("coverage_start")
    with pytest.raises(AdmissionError, match="coverage_start"):
        validate_manifest(missing_coverage)
    missing_version = valid_manifest(); missing_version.pop("dataset_version")
    with pytest.raises(AdmissionError, match="dataset_version"):
        validate_manifest(missing_version)


# 公开聚合是第二阶段合法模式，但不得声称真实个人数据，也不得声明隐私列。
def test_public_aggregated_accepts_teaching_source_and_rejects_real_claim():
    data = valid_manifest(); data["data_mode"] = "PUBLIC_AGGREGATED"
    assert validate_manifest(data).mode == "PUBLIC_AGGREGATED"
    claimed = valid_manifest(); claimed["data_mode"] = "PUBLIC_AGGREGATED"; claimed["claims_real_data"] = True
    with pytest.raises(AdmissionError, match="cannot claim"):
        validate_manifest(claimed)
    private = valid_manifest(); private["data_mode"] = "PUBLIC_AGGREGATED"; private["fields"] = ["license_plate"]
    with pytest.raises(AdmissionError, match="privacy"):
        validate_manifest(private)


# 个人标识列（userId/user_id 等）在真实匿名模式下也属于隐私边界，必须在清单阶段拒绝。
def test_real_anonymized_rejects_user_identifier_fields():
    # 课程真实数据常见 userId：即便不在最初黑名单里，也属于可重识别标识，不能声明入库。
    data = valid_manifest(); data["data_mode"] = "REAL_ANONYMIZED"; data["fields"] = ["sessionId", "userId"]
    # 驼峰写法命中。
    with pytest.raises(AdmissionError, match="privacy"):
        validate_manifest(data)
    # 下划线写法同样命中，避免只挡一种拼写。
    data = valid_manifest(); data["data_mode"] = "REAL_ANONYMIZED"; data["fields"] = ["user_id"]
    with pytest.raises(AdmissionError, match="privacy"):
        validate_manifest(data)

# 【文件讲解·答辩用】
# 功能作用：守住“数据从哪来、能不能进分析链路”这一关。课程不允许把教学模拟说成真实数据，
#   也不允许手机号、车牌这类字段混进清单。本文件测三件事：合法模拟清单能过；缺字段或
#   虚假真实声明不能过；真实匿名模式声明了隐私列也不能过。
# 技术原理：validate_manifest 读 dict，核对 REQUIRED_MANIFEST_FIELDS、ALLOWED_MODES、
#   用途、时间范围、claims_real_data 与 data_mode 是否打架、fields 是否落在 PRIVACY_FIELDS。
#   失败统一抛 AdmissionError（ValueError 子类），pytest.raises(..., match=...) 既查类型
#   也查关键字，避免抛了别的异常却被当成通过。valid_manifest() 是测试夹具，保证三条用例
#   只改一个变量，失败时能立刻看出是哪条规则生效。
# 代码思路：先给一份“应该成功”的底板，再分别做减法（pop provider）、做加法
#   （claims_real_data）和改模式+改字段。这是典型的边界测试：正向一条、负向两条。
# 与其他文件关系：实现在 analytics/src/source_registry.py。ingest_raw.py 在拷贝 CSV 前
#   会 load_manifest，等于把本文件的规则接到入库口。test_ingest_raw.py 再测“CSV 表头
#   带 phone_number 时，即使清单没声明隐私列也要拒绝”。contracts 下的 JSON Schema
#   描述事件字段，manifests/source.example.json 是流水线真正使用的示例清单。
# 答辩讲法：先举合法清单说“我们强制声明 SIMULATED”。再演示删 provider 和勾选
#   claims_real_data 两条失败。最后强调：匿名真实模式不是“改个标签就能用手机号”，
#   fields 里出现 phone_number 一样会被挡在门外。被问三种 data_mode 时，答 SIMULATED /
#   REAL_ANONYMIZED / PUBLIC_AGGREGATED，本测试覆盖前两种的关键冲突。
