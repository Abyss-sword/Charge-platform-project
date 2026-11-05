from __future__ import annotations  # 3.10 延迟注解。

import math  # ceil 把占用桩数向上取整。
from typing import Any  # 返回字典给 /forecast。

# 教学容量：与 curate_batch.AVAILABLE_PILES 一致；额定按 60kW 快充口径，不是现场铭牌抄表。
STATION_CAPACITY = {  # 内部站号 → 展示名、桩数、单桩额定功率。
    "1": {"name": "东软北门站", "total_piles": 4, "nominal_kw_per_pile": 60.0},  # 站 1。
    "2": {"name": "软件园站", "total_piles": 3, "nominal_kw_per_pile": 60.0},  # 站 2。
    "3": {"name": "浑南广场站", "total_piles": 2, "nominal_kw_per_pile": 60.0},  # 站 3。
}  # 容量表结束。
CAPACITY_SOURCE = "教学容量表：available_piles 4/3/2，额定 60kW 快充；小时电量 kWh 近似该小时平均功率 kW"  # 接口必须写清来源。
CAPACITY_NOTE = "容量换算估算，不是精确排队承诺"  # 禁止把空闲桩说成预约保证。


def estimate_free_piles(predicted_load_kw: float, total_piles: int, nominal_kw_per_pile: float) -> dict[str, Any]:  # 由已发布负荷换算预计空闲桩，不另训第二目标。
    if total_piles < 0 or not (nominal_kw_per_pile > 0):  # 桩数不得为负，额定必须为正。
        return {  # 参数非法时不给数字。
            "estimatedFree": None,  # 前端应显示不可用，而不是 0。
            "totalPiles": total_piles,  # 回显调用方传入值，便于对账。
            "nominalKwPerPile": nominal_kw_per_pile,  # 回显额定。
            "source": CAPACITY_SOURCE,  # 来源说明仍带上。
            "note": CAPACITY_NOTE,  # 估算免责。
            "reason": "容量参数不可用",  # 中文原因。
        }  # 非法参数返回结束。
    safe_load_kw = max(0.0, float(predicted_load_kw))  # 负负荷按 0，避免算出超过总桩数的空闲。
    estimated_free = min(total_piles, max(0, total_piles - math.ceil(safe_load_kw / nominal_kw_per_pile)))  # 占用向上取整后再夹到 [0, 总桩]。
    return {  # 合法估算。
        "estimatedFree": int(estimated_free),  # 整数桩数。
        "totalPiles": total_piles,  # 总桩。
        "nominalKwPerPile": nominal_kw_per_pile,  # 额定。
        "source": CAPACITY_SOURCE,  # 来源。
        "note": CAPACITY_NOTE,  # 免责。
        "reason": None,  # 无错误。
    }  # 合法返回结束。

# 【文件讲解·答辩用】
# 功能作用：把站点小时负荷换成“预计空闲桩”，公式固定，不另训一个模型。
# 技术原理：先校验容量参数，再 max(0, 负荷) 和 ceil(负荷/额定) 得到占用桩，空闲 = 总桩 − 占用。
# 代码思路：非法参数返回 estimatedFree=None 和「容量参数不可用」；合法则夹紧到 [0, 总桩]。
# 与其他文件关系：/forecast 对每个站调用；推荐只用负荷比，不把本函数结果当训练标签。
# 答辩时可以怎么讲：空闲桩是容量课本当量，不是排队承诺；参数来自教学表 4/3/2 桩、60kW。
