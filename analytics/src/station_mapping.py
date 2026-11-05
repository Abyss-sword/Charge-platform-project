from __future__ import annotations  # 允许 Path 等注解前向引用，兼容 3.10。

import json  # 站点映射文件是 JSON 对象：来源站号 → 内部正整数。
from pathlib import Path  # 用路径对象读 UTF-8 映射文件，不依赖当前工作目录字符串拼接。


def load_station_mapping(path: str | Path) -> dict[str, int]:  # 读 JSON 并校验：键必须是字符串，值必须是大于 0 的整数站号。
    data = json.loads(Path(path).read_text(encoding="utf-8"))  # 整文件解码为 dict。
    if not isinstance(data, dict) or not all(isinstance(k, str) and isinstance(v, int) and v > 0 for k, v in data.items()):  # 拒绝列表、非正整数、非字符串键。
        raise ValueError("station mapping must map source strings to positive station integers")  # 映射不合法就停，不静默跳过未知站。
    return data  # 校验通过后原样返回，供 normalize_row 查表。


def map_station(source_station_id: str, mapping: dict[str, int]) -> int:  # 把来源站点编号换成内部 station_id；查不到明确报 UNMAPPED_STATION。
    try:  # 尝试：命中则返回内部正整数。
        return mapping[source_station_id]  # 命中则返回内部正整数。
    except KeyError as exc:  # 来源站号不在教学映射表里。
        raise ValueError("UNMAPPED_STATION") from exc  # 转成清洗拒绝码，由 normalize_csv 记入 rejected。


def build_analysis_station_id(source_id: str, source_station_id: str) -> str:  # 第二阶段：带命名空间的分析站点ID（如 sim:station-a、acn:caltech-1）。
    if not source_id or not source_station_id:  # 命名空间与来源站号都必填。
        raise ValueError("MISSING_REQUIRED")  # 缺失拒绝。
    if any(ch in source_station_id for ch in ("\t", "\n", ":", " ")):  # 站号内禁止分隔符，防止构造出歧义ID。
        raise ValueError("INVALID_STATION_ID")  # 拒绝而不清洗。
    return f"{source_id}:{source_station_id}"  # 命名空间:站号；不得凭编号映射到本地业务站点。

# 【文件讲解·答辩用】
# 功能作用：把外部 CSV 里的 source_station_id 映射成平台内部站点整数，未知站拒绝而不是猜。
# 技术原理：JSON 字典查表；KeyError 转 ValueError，让清洗层用统一 reason_code 记账。
# 代码思路：先 load 校验整张表，再 map 单条；正整数约束避免 0/负数混进 hourly 聚合键。
# 与其他文件关系：normalize.py 调用 map_station；demo_pipeline 从 manifests/station_mapping.json 加载。
# 答辩时可以怎么讲：教学数据站号是 SRC-STATION-A 这种字符串，内部大屏用 1/2/3；对不上的行进 rejected，不进 curated。
