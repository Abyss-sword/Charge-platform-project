#include "scenario.h"  // 场景枚举、帧结构和四个对外函数的声明。

#include <stdexcept>  // 未知场景名抛 invalid_argument，避免静默跑成错误演示。

namespace charge {  // 实现与头文件同一命名空间。

ScenarioKind parse_scenario(const std::string& value) {  // 把 --scenario 的英文名转成枚举，只认白名单。
    if (value == "normal") return ScenarioKind::Normal;                      // 正常在线。
    if (value == "charging") return ScenarioKind::Charging;                  // 正常充电。
    if (value == "overheat") return ScenarioKind::Overheat;                  // 高温告警。
    if (value == "offline") return ScenarioKind::Offline;                    // 停止上报。
    if (value == "recover") return ScenarioKind::Recover;                    // 恢复合法上报。
    if (value == "duplicate") return ScenarioKind::Duplicate;                // 精确重复。
    if (value == "time-backward") return ScenarioKind::TimeBackward;         // 时间倒退。
    if (value == "energy-backward") return ScenarioKind::EnergyBackward;     // 电量倒退。
    if (value == "unknown-pile") return ScenarioKind::UnknownPile;           // 数据库不存在的桩。
    if (value == "timeout") return ScenarioKind::Timeout;                    // 旧超时夹具。
    if (value == "command-failure") return ScenarioKind::CommandFailure;     // 旧命令失败夹具。
    throw std::invalid_argument("unknown scenario: " + value);               // 答辩入口只允许白名单名称。
}  // 拼错场景名立刻失败，不会误发成 normal。

std::string scenario_name(ScenarioKind kind) {  // 枚举转回命令行同款英文名，供日志和 SCENARIO= 输出。
    switch (kind) {  // 每个枚举必须有对应稳定名，大屏按钮和脚本都认这些字符串。
        case ScenarioKind::Normal: return "normal";  // 空闲心跳场景名。
        case ScenarioKind::Charging: return "charging";  // 正常充电场景名。
        case ScenarioKind::Overheat: return "overheat";  // 过热告警场景名。
        case ScenarioKind::Offline: return "offline";  // 停报离线场景名。
        case ScenarioKind::Recover: return "recover";  // 故障/离线后恢复场景名。
        case ScenarioKind::Duplicate: return "duplicate";  // 幂等重复上报场景名。
        case ScenarioKind::TimeBackward: return "time-backward";  // 时间倒退场景名。
        case ScenarioKind::EnergyBackward: return "energy-backward";  // 电量倒退场景名。
        case ScenarioKind::UnknownPile: return "unknown-pile";  // 未知桩号场景名。
        case ScenarioKind::Timeout: return "timeout";  // 旧心跳超时夹具名。
        case ScenarioKind::CommandFailure: return "command-failure";  // 旧命令失败夹具名。
    }  // switch 穷尽后若仍有未覆盖枚举，下面返回 unknown。
    return "unknown";  // 理论上走不到；保留是为了让编译器对 switch 穷尽检查后仍有返回值。
}  // 输出名必须与 parse_scenario 输入完全一致，scenario_test 会双向对拍。

std::vector<TelemetryFrame> fixed_scenario(ScenarioKind kind) {  // 返回该场景写死的遥测帧，数值不随机。
    switch (kind) {  // 按场景给出可重复的功率、温度、电量增量和时间偏移。
        case ScenarioKind::Normal:  // 两帧空闲心跳，刷新在线、电量增量为 0。
            // 两帧空闲心跳：25℃、230V、0kW，电量增量为 0，服务端应刷新在线并保持正常。
            return {{0, "IDLE", 0, 25, 230.0, 0.0, "0.00", "HEARTBEAT"},
                    {5, "IDLE", 0, 25, 230.0, 0.0, "0.00", "TELEMETRY"}};  // 第二帧仍空闲，只刷新心跳时间。
        case ScenarioKind::Charging:  // 四帧约 7kW 充电，电量增量 0.00→0.06。
            // 四帧充电：功率约 7kW，电流上升，累计增量 0.00→0.06，加到持久化电表上。
            return {{0, "CHARGING", 7000, 25, 230.0, 30.4, "0.00", "CHARGING"},
                    {5, "CHARGING", 7200, 26, 231.0, 31.2, "0.01", "CHARGING"},  // 功率略升到 7.2kW，增量 0.01。
                    {10, "CHARGING", 7100, 26, 230.0, 30.9, "0.03", "CHARGING"},  // 功率回落到 7.1kW，增量 0.03。
                    {15, "CHARGING", 7000, 27, 230.0, 30.4, "0.06", "CHARGING"}};  // 15 秒时累计增量 0.06 kWh。
        case ScenarioKind::Overheat:  // 末帧 71℃、FAULT，用来触发服务端过热告警。
            // 前两帧高温充电，最后一帧 71℃ 且状态 FAULT，触发服务端过热告警/工单复用。
            return {{0, "CHARGING", 20000, 55, 400.0, 50.0, "0.00", "CHARGING"},  // 直流桩量级 20kW/55℃，尚未过阈值。
                    {5, "CHARGING", 20000, 55, 400.0, 50.0, "0.03", "TELEMETRY"},  // 仍 55℃，先写入充电过程。
                    {10, "FAULT", 0, 71, 400.0, 0.0, "0.05", "OVERHEAT"}};  // 71℃ 掉功率，服务端应出过热告警。
        case ScenarioKind::Offline:  // 只发一帧后停报，由服务端 15 秒心跳超时判离线。
            // 只发一帧后进程退出。服务端超过 15 秒无心跳才把桩标为 OFFLINE。
            return {{0, "IDLE", 0, 25, 230.0, 0.0, "0.00", "LAST_HEARTBEAT"}};  // 最后一次合法心跳，之后不再上报。
        case ScenarioKind::Recover:  // 故障/离线后重新发两帧合法空闲数据。
            // 两帧合法空闲数据，离线或故障后按活动订单由服务端恢复 CHARGING 或 IDLE。
            return {{0, "IDLE", 0, 26, 230.0, 0.0, "0.00", "RECOVERED"},
                    {5, "IDLE", 0, 26, 230.0, 0.0, "0.00", "HEARTBEAT"}};  // 第二帧继续合法心跳，证明已恢复在线。
        case ScenarioKind::Duplicate:  // 同一秒同一电量再发一次，测幂等不计费。
            // 两帧 offset、读数、功率完全相同，服务端应 duplicate=true 且不重复计费。
            return {{0, "CHARGING", 7000, 25, 230.0, 30.4, "0.01", "TELEMETRY"},
                    {0, "CHARGING", 7000, 25, 230.0, 30.4, "0.01", "DUPLICATE"}};  // 同一秒同一电量再发一次。
        case ScenarioKind::TimeBackward:  // 第二帧时间更早，预期服务端乱序拒绝。
            // 第二帧 offset 4 < 第一帧 5，生成更早的时间戳，预期 TELEMETRY_OUT_OF_ORDER。
            return {{5, "CHARGING", 7000, 25, 230.0, 30.4, "0.01", "TELEMETRY"},
                    {4, "CHARGING", 7000, 25, 230.0, 30.4, "0.02", "TIME_BACKWARD"}};  // 时间倒退 1 秒，电量仍前进。
        case ScenarioKind::EnergyBackward:  // 第二帧电量增量更小，累计电表倒退。
            // 第二帧增量 0.01 < 第一帧 0.02，累计电表会倒退，预期 TELEMETRY_OUT_OF_ORDER。
            return {{0, "CHARGING", 7000, 25, 230.0, 30.4, "0.02", "TELEMETRY"},
                    {5, "CHARGING", 7000, 25, 230.0, 30.4, "0.01", "ENERGY_BACKWARD"}};  // 时间前进但电量从 0.02 掉到 0.01。
        case ScenarioKind::UnknownPile:  // 读数合法，非法点是 main 改掉的桩号。
            // 帧内容合法，但 main.cpp 会把 pileId 改成数据库不存在的值。
            return {{0, "IDLE", 0, 25, 230.0, 0.0, "0.00", "UNKNOWN_PILE"}};  // 遥测字段正常，非法的是桩号不是读数。
        case ScenarioKind::Timeout:  // 旧夹具：第二帧隔 35 秒，观察心跳超时。
            // 兼容旧超时场景：第二帧间隔 35 秒，便于观察心跳超时。
            return {{0, "IDLE", 0, 25, 230.0, 0.0, "0.00", "HEARTBEAT"},
                    {35, "OFFLINE", 0, 25, 230.0, 0.0, "0.00", "HEARTBEAT_TIMEOUT"}};  // 间隔超过服务端 15 秒心跳窗。
        case ScenarioKind::CommandFailure:  // 旧命令失败夹具，不纳入九场景验收。
            // 兼容旧命令失败夹具，不作为 9 月 10 日九场景验收项。
            return {{0, "IDLE", 0, 25, 230.0, 0.0, "0.00", "HEARTBEAT"},
                    {5, "IDLE", 0, 25, 230.0, 0.0, "0.00", "COMMAND_FAILED"}};  // 旧脚本用事件标签模拟命令失败。
    }  // 未列出的枚举落到下面的空序列。
    return {};  // 未识别枚举返回空序列，调用方会因此发不出帧。
}  // 正式演示只跑前九个 case，timeout/command-failure 留给旧脚本。

bool scenario_expects_rejection(ScenarioKind kind) {  // 告诉 main：哪些场景被服务端拒绝才算演示成功。
    // 这三类必须看到服务端拒绝才算演示成功；其余场景拒绝则视为失败。
    return kind == ScenarioKind::TimeBackward  // 时间戳早于上一帧，服务端应回 TELEMETRY_OUT_OF_ORDER。
        || kind == ScenarioKind::EnergyBackward  // 累计电表倒退，同样应被拒绝。
        || kind == ScenarioKind::UnknownPile;  // pile_info 里没有该桩，不得写入遥测脏数据。
}  // 合法场景（充电/过热/重复等）若被拒绝，main 会以退出码 2 失败。

}  // namespace charge

/*
【文件讲解·答辩用】
功能作用：把命令行场景名翻译成枚举，并给出每个场景写死的遥测帧。充电是四帧 7kW、电量增量 0.00→0.06；过热最后一帧 71℃；
          离线只发一帧；重复两帧时间与电量完全相同；倒退和未知桩用来证明服务端会拒绝脏数据。
技术原理：演示可重复的关键是“数值写死、不随机”。recordedAt 的先后完全由 offset_seconds 决定；电量是否单调由 energy_kwh 字符串决定。
          未知桩的帧本身合法，非法点在 main 改 pileId，这样同一套组帧代码也能测“库中无此桩”。
代码思路：parse_scenario 白名单映射 → scenario_name 反向映射 → fixed_scenario 的 switch 返回 vector<TelemetryFrame> →
          scenario_expects_rejection 标出三类负例。未知英文名抛 invalid_argument，避免答辩口误跑成错误场景还显示成功。
与其他文件关系：声明在 scenario.h；PileSimulator 按这些帧算累计电表并组 JSON；main.cpp 按 expects_rejection 决定退出码；
          scenario_test.cpp 断言九个名字、充电四帧递增、过热≥70℃、离线恰好一帧。
答辩时可以怎么讲：打开本文件指着 Charging 四行数字说“这就是大屏上功率 7000W、电表从 100.00 涨到 100.06 的来源”；
          再指 Overheat 最后一帧 71 和 Offline 只返回一个元素；最后说倒退场景我们预期服务端说不，模拟器看到拒绝才 return 0。
*/
