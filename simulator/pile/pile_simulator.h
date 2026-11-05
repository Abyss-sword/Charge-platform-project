#pragma once  // 桩模拟器类声明：把场景帧变成协议 JSON，并把增量加到生命周期电表上。

#include "scenario.h"  // 场景枚举和 TelemetryFrame。

#include <string>  // JSON 正文、ISO-8601 时间和两位小数电表都用 string，避免二进制浮点入库。

namespace charge {  // 与 scenario / protocol_frame 同一命名空间，入口直接 charge::PileSimulator。

class PileSimulator {  // 一台虚拟桩：记住桩号、场景、起点时间和累计电表初值。
public:  // 组 JSON、算累计电表、无网打印；不在此类里发 TCP。
    // starting_energy_kwh 是持久化累计电表，不是当前订单用电量。
    PileSimulator(long long pile_id, ScenarioKind scenario,
                  std::string start_time = {},  // 空则用当前 UTC；测试传入 +08:00 固定起点。
                  std::string starting_energy_kwh = "100.00");  // 无状态文件时从 100.00 kWh 起步，与种子库一致。

    std::string json_line(const TelemetryFrame& frame, int sequence) const;  // 生成一条 PILE_TELEMETRY JSON。
    std::string energy_for_frame(const TelemetryFrame& frame) const;         // 累计电表 = 起点 + 本帧增量。
    int run(int count, int interval_ms) const;                              // 无 --connect 时只向标准输出打印帧。

private:  // 一台虚拟桩的运行时状态，组每一帧 JSON 都要用。
    long long pile_id_{};                     // 必须对应 pile_info.id；未知桩场景会被 main 改成 999999999。
    ScenarioKind scenario_;                   // 当前固定场景。
    std::string start_time_;                  // 带时区的 ISO-8601 起点。
    long long starting_energy_hundredths_{};  // 用整数百分之一 kWh 累加，避免二进制浮点误差。
};  // 对外三个方法：组 JSON、算累计电表、本地打印；真正发 TCP 在 protocol_frame。

}  // namespace charge

/*
【文件讲解·答辩用】
功能作用：声明虚拟充电桩类。它不自己连网，只负责把 scenario.h 里的固定帧编成成员1能认的 PILE_TELEMETRY JSON，
          并把“生命周期累计电表 = 持久化起点 + 本帧增量”算出来。无 --connect 时 run() 只往标准输出打印，给本地看帧。
技术原理：电量全程用“百分之一 kWh 的整数”累加，再格式化成两位小数字符串，避免 0.01+0.02 变成 0.0300000004。
          recordedAt 由起点时间加帧偏移生成，重复场景同一偏移必须得到同一时间戳，倒退场景才能打到服务端校验。
代码思路：构造时记下桩号、场景、起点、电表初值 → json_line 填 version/requestId/action/payload →
          energy_for_frame 做整数加法 → run 按 --count / --interval-ms 打印。requestId 形如 PILE-101-1，方便 ACK 对账。
与其他文件关系：实现在 pile_simulator.cpp；帧来自 scenario.h；main.cpp 在 TCP 成功后用 energy_for_frame 回写状态文件；
          protocol_frame.cpp 负责把 json_line 的结果加上 4 字节长度头发走。
答辩时可以怎么讲：指着 json_line 说“这就是发给成员1的那条 JSON：action=PILE_TELEMETRY，dataMode=SIMULATED，
          energyKwh 是累计电表不是增量”；再强调充电场景从 100.00 加到 100.06，重启进程也不会从 0 开始，因为初值来自状态文件。
*/
