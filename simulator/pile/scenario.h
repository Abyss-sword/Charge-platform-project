#pragma once  // 头文件只编译一次，避免场景枚举和帧结构被重复定义。

#include <string>   // 场景名、状态、累计电量增量都用十进制文本，不用二进制浮点累计金额。
#include <vector>   // 每个场景返回一组固定遥测帧，保证答辩演示可重复。

namespace charge {  // 模拟器独立命名空间，不进入成员1的 server 命名空间。

// 命令行 --scenario 使用的稳定英文名。答辩时只改参数，不改源码。
// Timeout / CommandFailure 保留兼容旧脚本，第一阶段正式演示用前九个名称。
enum class ScenarioKind {  // 场景白名单：解析、组帧、是否允许被服务端拒绝都认这个枚举。
    Normal,          // 正常在线：合法心跳，桩保持空闲。
    Charging,        // 正常充电：功率和累计电量稳定增加。
    Overheat,        // 高温告警：温度达到或超过 70℃。
    Offline,         // 离线：发完最后一帧后停止上报，超过 15 秒由服务端判离线。
    Recover,         // 恢复：故障或离线后重新发送合法数据。
    Duplicate,       // 重复上报：同一时间、同一读数再发一次，应幂等成功。
    TimeBackward,    // 时间倒退：第二帧时间早于第一帧，服务端应拒绝。
    EnergyBackward,  // 电量倒退：第二帧累计电量小于第一帧，服务端应拒绝。
    UnknownPile,     // 未知电桩：pileId 不在 pile_info 中，不得写脏数据。
    Timeout,         // 旧名称：心跳超时夹具，兼容早期脚本。
    CommandFailure   // 旧名称：命令失败夹具，兼容早期脚本。
};  // 九个正式场景 + 两个旧名，命令行只认这些英文名。

// 一帧遥测的固定输入。energy_kwh 是相对持久化电表的非负增量，不是订单电量。
struct TelemetryFrame {  // 场景脚本的一帧读数，稍后由 PileSimulator 加成累计电表并编 JSON。
    int offset_seconds{};     // 相对启动时间的秒偏移，用于生成带时区的 recordedAt。
    std::string status;       // 模拟器声明的桩状态文本，最终是否入库由服务端决定。
    int power_w{};            // 功率，单位瓦。
    int temperature_c{};      // 温度，单位摄氏度；过热场景最后一帧为 71。
    double voltage_v{};       // 电压，单位伏特。
    double current_a{};       // 电流，单位安培。
    std::string energy_kwh;   // 本帧电量增量，两位小数字符串，例如 "0.06"。
    std::string event;        // 帧事件标签，只用于日志，不是协议必填业务字段。
};  // 功率/温度/电压/电流给服务端告警和展示，电量增量给累计电表。

ScenarioKind parse_scenario(const std::string& value);                 // 把命令参数解析成枚举，未知名称直接抛错。
std::string scenario_name(ScenarioKind kind);                          // 枚举转回稳定英文名，供日志和 ACK 输出。
std::vector<TelemetryFrame> fixed_scenario(ScenarioKind kind);         // 返回该场景的固定帧序列，数值写死以便复现。
bool scenario_expects_rejection(ScenarioKind kind);                    // 倒退和未知桩场景允许非零退出码，因为服务端拒绝才算成功。

}  // namespace charge

/*
【文件讲解·答辩用】
功能作用：定义充电桩模拟器的九个正式演示场景和两帧一套的固定遥测结构。命令行 --scenario 只认这里的英文名；
          每帧给出时间偏移、状态、功率、温度、电压、电流和电量增量。energy_kwh 是相对持久化电表的增量，不是订单电量。
技术原理：用枚举而不是魔法字符串贯穿后继模块，避免拼写漂移。帧数值全部写死在 scenario.cpp，答辩每次跑 charging
          都是同一组 7kW / 0.00→0.06。服务端是否接受由成员1决定，模拟器只负责按脚本发。
代码思路：ScenarioKind 列出场景 → TelemetryFrame 描述一帧 → parse_scenario / scenario_name 做命令行双向转换 →
          fixed_scenario 给出帧序列 → scenario_expects_rejection 告诉 main：时间倒退、电量倒退、未知桩被拒绝才算成功。
与其他文件关系：scenario.cpp 实现本头文件；pile_simulator.cpp 按帧生成 PILE_TELEMETRY JSON；main.cpp 解析 --scenario
          并在未知桩时改 pileId=999999999；scenario_test.cpp 断言九个名字和关键数值。
答辩时可以怎么讲：先说“演示不改代码，只改 --scenario”；再报九个名字（normal/charging/overheat/offline/recover/
          duplicate/time-backward/energy-backward/unknown-pile）；强调过热最后一帧 71℃、离线只发一帧、倒退场景必须看到服务端拒绝。
*/
