#include "pile_simulator.h"  // 用 PileSimulator 组 JSON，并间接带上 scenario.h 的九场景。
#include "protocol_frame.h"  // 只测 encode_frame 的 4 字节大端长度头，不连真服务端。

#include <cassert>   // 数值或协议字段不对就立刻 abort，CTest 记失败。
#include <iostream>  // 全部通过后打印 SIMULATOR_SCENARIOS=9 和 SIMULATOR_TEST=PASS。
#include <set>       // 用集合列出九个正式场景名，防止漏测某一个。

int main() {  // 场景与组帧的离线断言入口，不需要成员1在线。
    using namespace charge;  // 测试里直接写 parse_scenario / ScenarioKind，少写前缀。

    const std::set<std::string> required{  // 第一阶段正式演示的九个稳定英文名。
        "normal", "charging", "overheat", "offline", "recover",  // 正常/充电/过热/离线/恢复。
        "duplicate", "time-backward", "energy-backward", "unknown-pile"  // 重复/时间倒退/电量倒退/未知桩。
    };  // timeout、command-failure 是旧名，不纳入这九项验收。
    for (const auto& name : required) {  // 每个名字都要能解析回去，且有非空固定帧。
        assert(scenario_name(parse_scenario(name)) == name);  // 命令行名与枚举双向一致，大屏按钮才不会跑错场景。
        assert(!fixed_scenario(parse_scenario(name)).empty());  // 没有帧就发不出遥测，演示会空转。
    }  // 九个名字全部通过映射和帧非空检查。

    const auto charging = fixed_scenario(ScenarioKind::Charging);  // 取出充电四帧，后面还要拿来组 JSON。
    assert(charging.size() == 4);  // 充电必须是四帧：功率约 7kW，增量 0.00→0.06。
    for (std::size_t i = 1; i < charging.size(); ++i) {  // 从第二帧起对比前一帧。
        assert(charging[i].energy_kwh >= charging[i - 1].energy_kwh);  // 合法充电的电量增量不得倒退。
        assert(charging[i].offset_seconds > charging[i - 1].offset_seconds);  // 时间必须前进，否则变成 time-backward。
    }  // 充电场景自身是单调的，倒退另有专用场景。
    const auto duplicate = fixed_scenario(ScenarioKind::Duplicate);  // 两帧完全相同的充电读数。
    assert(duplicate[0].offset_seconds == duplicate[1].offset_seconds);  // 同一秒再发一次，服务端应幂等。
    assert(duplicate[0].energy_kwh == duplicate[1].energy_kwh);  // 电量也相同，才不会被当成新的累计。
    assert(fixed_scenario(ScenarioKind::TimeBackward)[1].offset_seconds  // 第二帧时间偏移。
           < fixed_scenario(ScenarioKind::TimeBackward)[0].offset_seconds);  // 必须小于第一帧，才能生成更早的 recordedAt。
    assert(fixed_scenario(ScenarioKind::EnergyBackward)[1].energy_kwh  // 第二帧电量增量。
           < fixed_scenario(ScenarioKind::EnergyBackward)[0].energy_kwh);  // 必须小于第一帧，累计电表才会倒退。
    assert(fixed_scenario(ScenarioKind::Overheat).back().temperature_c >= 70);  // 过热最后一帧 ≥70℃（实际为 71）。
    assert(fixed_scenario(ScenarioKind::Offline).size() == 1);  // 离线只发一帧，之后靠服务端 15 秒心跳超时判 OFFLINE。

    const PileSimulator simulator(  // 用种子库桩 101、东八区固定起点、电表 100.00 组充电 JSON。
        101, ScenarioKind::Charging, "2026-09-07T09:00:00+08:00", "100.00");  // 09:00+08:00 应对成 01:00Z。
    const auto first = simulator.json_line(charging.front(), 1);  // 第一帧：增量 0.00，累计仍 100.00，requestId=PILE-101-1。
    const auto second = simulator.json_line(charging[1], 2);  // 第二帧：增量 0.01，累计 100.01，时间 +5 秒。
    assert(first.find("\"version\":\"1.0\"") != std::string::npos);  // 协议版本字段必须在。
    assert(first.find("\"dataMode\":\"SIMULATED\"") != std::string::npos);  // 标明模拟数据，避免和大屏真桩混淆。
    assert(first.find("\"energyKwh\":\"100.00\"") != std::string::npos);  // 起点 + 0.00，JSON 里是累计不是增量。
    assert(second.find("\"energyKwh\":\"100.01\"") != std::string::npos);  // 百分之一整数加法：10000+1=10001。
    assert(first.find("\"recordedAt\":\"2026-09-07T01:00:00Z\"")  // 东八区 09:00 转 UTC。
           != std::string::npos);  // 时区换算错了服务端时间线会对不上。
    assert(second.find("\"recordedAt\":\"2026-09-07T01:00:05Z\"")  // 偏移 5 秒。
           != std::string::npos);  // duplicate 依赖“相同偏移 → 相同时间戳”，这里先锁住换算。
    assert(first.find("\"pileId\":101") != std::string::npos);  // 默认桩必须能在 pile_info 对上。

    const auto framed = encode_frame(first);  // 给第一帧 JSON 加上 4 字节网络序长度头。
    const auto payload_size = (static_cast<unsigned>(framed[0]) << 24U)  // 大端最高字节。
        | (static_cast<unsigned>(framed[1]) << 16U)  // 次高。
        | (static_cast<unsigned>(framed[2]) << 8U)  // 次低。
        | static_cast<unsigned>(framed[3]);  // 最低；拼出正文字节数。
    assert(payload_size == first.size());  // 长度头必须等于 JSON 的 UTF-8 字节数。
    assert(std::string(framed.begin() + 4, framed.end()) == first);  // 长度头后面必须是原文，不能改字段或加 '\0'。
    std::cout << "SIMULATOR_SCENARIOS=9\nSIMULATOR_TEST=PASS\n";  // 给 CTest/脚本抓取的通过标记。
}  // 任一 assert 失败进程非 0 退出，CMake 的 add_test(scenario_test) 会红。

/*
【文件讲解·答辩用】
功能作用：不连网的场景回归测试。锁死九个正式场景名、充电四帧单调递增、重复帧时间/电量相同、时间与电量倒退方向正确、
          过热末帧 ≥70℃、离线恰好一帧；再锁死 JSON 协议字段、东八区转 UTC、累计电表 100.00→100.01，以及 4 字节长度头。
技术原理：纯 assert + 固定输入。时间用写死的 2026-09-07T09:00:00+08:00，所以 recordedAt 永远是 01:00:00Z / 01:00:05Z，
          CI 和答辩机时区怎么设都不影响。电表用字符串精确匹配，验证的是整数百分之一加法而不是浮点。
代码思路：先对拍九个名字 → 再查各场景的关键不变量 → 用 PileSimulator 组两帧 JSON 做字段 find →
          encode_frame 反解长度头。timeout/command-failure 故意不进 required 集合，避免把旧夹具算进九场景验收。
与其他文件关系：链接 pile_core，覆盖 scenario.cpp、pile_simulator.cpp、protocol_frame.cpp 的 encode_frame。
          真正的 TCP 一问一答由 tcp_loopback_test.py 测。CMakeLists.txt 把本文件登记为 CTest 项 scenario_test。
答辩时可以怎么讲：先说“场景数字不是口头约定，是测试钉死的”；然后跑 scenario_test 给老师看 SIMULATOR_TEST=PASS；
          若有人改过热温度为 60，这里 assert ≥70 会立刻失败。强调本测试不需要服务端，笔记本离线也能证明组帧正确。
*/
