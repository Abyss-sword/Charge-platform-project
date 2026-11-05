# 容量公式必须在非法参数时拒绝给数字，合法时按 ceil 占用再夹紧空闲桩。
from src.capacity import estimate_free_piles


def test_invalid_capacity_returns_unavailable_not_a_number():
    negative = estimate_free_piles(10, -1, 60)
    zero_nominal = estimate_free_piles(10, 4, 0)
    assert negative["estimatedFree"] is None and negative["reason"] == "容量参数不可用"
    assert zero_nominal["estimatedFree"] is None and zero_nominal["reason"] == "容量参数不可用"


def test_capacity_formula_ceils_occupancy_and_clamps():
    # 4 桩、60kW、61kW → ceil(61/60)=2 占用，空闲 2。
    assert estimate_free_piles(61, 4, 60)["estimatedFree"] == 2
    # 负负荷按 0，空闲等于总桩。
    assert estimate_free_piles(-8, 3, 60)["estimatedFree"] == 3
    # 远超容量时空闲为 0，不得变负数。
    assert estimate_free_piles(1000, 2, 60)["estimatedFree"] == 0

# 【文件讲解·答辩用】
# 功能作用：锁住“预计空闲桩不是第二个训练目标”，只是负荷换算。
# 技术原理：非法参数返回 None；合法则 max(0,负荷) 后 ceil(负荷/额定)。
# 答辩讲法：61kW 在 60kW 额定下占 2 桩；参数坏了就说容量不可用，不给 0 假装算过。
