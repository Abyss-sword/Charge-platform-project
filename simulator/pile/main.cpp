// 模拟器进程入口：解析 --scenario，经 4 字节长度头 TCP 发给成员1服务端，只有收到 ok:true 才算成功。
#include "pile_simulator.h"  // 组 JSON 遥测、计算累计电表。
#include "protocol_frame.h"  // 长度头编解码和 TCP 一问一答。

#include <algorithm>    // 限制实际发送帧数不超过场景固定序列。
#include <cstdlib>      // 读取 CHARGE_SIMULATOR_STATE 环境变量。
#include <filesystem>   // 电表状态文件路径；Windows 中文路径需由脚本改到 LOCALAPPDATA。
#include <fstream>      // 读写生命周期累计电表，避免进程重启从 0 开始。
#include <iostream>     // SENT_FRAMES / ACKNOWLEDGED_FRAMES 输出，答辩用来证明真连通。
#include <stdexcept>    // 参数错误和服务端拒绝都变成明确退出码。
#include <string>       // 命令行参数、连接串、ACK JSON 都按文本处理。
#include <vector>       // 待发送的 JSON 列表和服务端逐帧响应。

namespace {  // 匿名命名空间：参数解析和电表文件只给本入口用，不污染 charge。

std::string value_after(int& i, int argc, char** argv) {  // 取 --flag 后面那个参数，并让主循环跳过它。
    // 读取 --flag 后面的参数，缺值直接失败，避免把下一个选项当成桩号。
    if (i + 1 >= argc) throw std::invalid_argument(std::string("missing value after ") + argv[i]);  // 例如只有 --pile 没有 101。
    return argv[++i];  // 先把下标挪到值上，再返回该字符串。
}  // 调用方随后的 ++i 会从下一个选项继续扫。

std::string load_meter(const std::filesystem::path& state_file,  // 状态文件路径，例如 LOCALAPPDATA 下的 pile-101.energy。
                       const std::string& fallback) {  // 读上次已确认的累计电表；没有文件则用命令行初值。
    // 优先读上次被服务端确认的累计电表；没有文件才用 --energy-start。
    std::ifstream input(state_file);  // 打开 pile-101-xxxx.energy 这类状态文件。
    std::string value;  // 文件里应是两位小数，例如 100.06。
    if (input >> value) return value;  // 读到内容就用它当本轮电表起点。
    return fallback;  // 文件不存在或为空时退回 --energy-start（默认 100.00）。
}  // 绝不从 0.00 起步，否则服务端会判电量倒退。

void save_meter(const std::filesystem::path& state_file, const std::string& value) {  // 把服务端已确认的累计电表落盘。
    // 先写临时文件再改名，避免写到一半断电留下半截读数。
    if (state_file.has_parent_path()) std::filesystem::create_directories(state_file.parent_path());  // 确保 runtime 或 LOCALAPPDATA 目录存在。
    const auto temporary = state_file.string() + ".tmp";  // 同目录临时名，写完再 rename。
    {  // 缩小 ofstream 作用域，关闭后再 rename，避免 Windows 占用句柄。
        std::ofstream output(temporary, std::ios::trunc);  // 覆盖写临时文件。
        if (!output) throw std::runtime_error("cannot write simulator state");  // 目录只读或磁盘满则失败，不假装已保存。
        output << value << '\n';  // 只写一行两位小数电表。
    }  // 关闭文件后再改名，避免句柄占用。
    std::filesystem::remove(state_file);  // Windows 上 rename 到已有文件可能失败，先删旧文件。
    std::filesystem::rename(temporary, state_file);  // 原子替换成正式状态文件。
}  // 下次启动 load_meter 就能接到本次最后一次成功读数。

bool response_ok(const std::string& response) {  // 看 ACK JSON 里有没有协议成功标志。
    // 协议成功标志是 JSON 里的 "ok":true。找不到就不计入 ACKNOWLEDGED_FRAMES。
    return response.find("\"ok\":true") != std::string::npos;  // 简单子串判断，与成员1约定的紧凑 JSON 一致。
}  // 拒绝帧会走 REJECTED_FRAMES，倒退场景靠它判断演示是否成功。

}  // namespace

int main(int argc, char** argv) {  // 充电桩模拟器进程入口。
    long long pile = 101;                 // 默认对应种子库 pile_info.id=101。
    std::string scenario = "normal";      // 默认正常在线；答辩用 --scenario 切换。
    std::string connect_to;               // 空表示只打印 JSON，不连网；联调必须给 HOST:PORT。
    std::string start_time;               // 可选 ISO-8601 起点；空则用当前时间。
    std::string energy_start = "100.00";  // 无状态文件时的累计电表初值。
    std::string state_file;               // 持久化电表路径，必须与启动脚本一致。
    int count = 0;                        // 0 表示发送该场景全部固定帧。
    int interval_ms = 0;                  // 帧间隔；0 表示尽快发送。
    try {  // 参数错误、连不上、服务端乱序拒绝都在这里变成退出码 2。
        for (int i = 1; i < argc; ++i) {  // 从 argv[1] 起扫命令行，不处理程序名。
            const std::string arg = argv[i];  // 当前选项，例如 --scenario。
            if (arg == "--pile") pile = std::stoll(value_after(i, argc, argv));  // 覆盖默认桩号 101。
            else if (arg == "--scenario") scenario = value_after(i, argc, argv);  // 九场景英文名。
            else if (arg == "--count") count = std::stoi(value_after(i, argc, argv));  // 最多发几帧，0 表示全发。
            else if (arg == "--interval-ms") interval_ms = std::stoi(value_after(i, argc, argv));  // 帧间隔毫秒。
            else if (arg == "--connect") connect_to = value_after(i, argc, argv);  // HOST:PORT，接到成员1。
            else if (arg == "--start-time") start_time = value_after(i, argc, argv);  // 固定起点，便于重复时间戳。
            else if (arg == "--energy-start") energy_start = value_after(i, argc, argv);  // 无状态文件时的电表初值。
            else if (arg == "--state-file") state_file = value_after(i, argc, argv);  // 显式指定电表文件，测试常用临时路径。
            else if (arg == "--help") {  // 打印用法后成功退出，不连网。
                std::cout  // 用法写到 stdout，方便脚本抓取。
                    << "pile_simulator --pile ID --scenario NAME [--connect HOST:PORT]\n"  // 联调最小命令行。
                    << "  NAME: normal|charging|overheat|offline|recover|duplicate|"  // 前六个正式场景名。
                       "time-backward|energy-backward|unknown-pile\n"  // 后三个负例场景名。
                    << "  Optional: --interval-ms N --count N --start-time ISO8601 "  // 可选节拍、帧数和固定起点。
                       "--energy-start 100.00 --state-file PATH\n";  // 正式九场景名；旧 timeout 名仍能 parse 但不写在帮助里。
                return 0;  // 帮助不是错误。
            } else {  // 未识别的选项立刻失败，避免把拼写错误当桩号。
                throw std::invalid_argument("unknown option: " + arg);  // 例如误写成 --sceanrio。
            }  // 一个选项处理完毕，for 继续下一个 argv。
        }  // 命令行扫描结束。

        const auto kind = charge::parse_scenario(scenario);  // 非法场景名在这里抛错。
        if (kind == charge::ScenarioKind::UnknownPile) pile = 999999999;  // 故意使用不存在的桩号。
        if (state_file.empty()) {  // 未传 --state-file 时按环境变量或默认 runtime 路径。
            if (const char* configured = std::getenv("CHARGE_SIMULATOR_STATE")) {  // 启动脚本会把 Windows 安全路径写进该变量。
                state_file = configured;  // 优先用脚本指定的 LOCALAPPDATA 电表文件。
            } else {  // 裸跑可执行文件时落到仓库 runtime。
                state_file = "runtime/pile-" + std::to_string(pile) + ".energy";  // 按桩号分文件，101 和 102 互不影响。
            }  // 环境变量分支结束。
        }  // 至此 state_file 一定非空。
        const std::filesystem::path meter_path(state_file);  // 转成 path，后面读写、建目录都用它。
        const std::string meter_start = load_meter(meter_path, energy_start);  // 本轮累计电表起点。
        const charge::PileSimulator simulator(pile, kind, start_time, meter_start);  // 用桩号、场景、时间和电表构造虚拟桩。
        if (connect_to.empty()) return simulator.run(count, interval_ms);  // 无 --connect 时只本地打印，联调不要走这条。

        const auto separator = connect_to.rfind(':');  // 从右边拆 HOST:PORT，兼容 IPv6 字面量时仍取最后一个冒号后的端口。
        if (separator == std::string::npos || separator == 0  // 没有冒号，或冒号在开头（缺主机）。
            || separator + 1 >= connect_to.size()) {  // 缺冒号、冒号在开头或后面没有端口都不合法。
            throw std::invalid_argument("--connect must be HOST:PORT");  // 答辩联调必须写成 127.0.0.1:48080。
        }  // 连接串格式检查通过。
        const auto frames = charge::fixed_scenario(kind);  // 取出该场景写死的遥测帧。
        const int limit = count <= 0 ? static_cast<int>(frames.size())  // --count 0 表示发完该场景全部固定帧。
                                     : std::min(count, static_cast<int>(frames.size()));  // 实际发送帧数不超过场景长度。
        std::vector<std::string> lines;  // 即将发给服务端的 JSON 正文列表。
        for (int i = 0; i < limit; ++i) {  // 按序号 1..N 组 requestId。
            lines.push_back(simulator.json_line(frames[static_cast<std::size_t>(i)], i + 1));  // 编成 PILE_TELEMETRY JSON。
        }  // JSON 列表准备完毕。
        const auto responses = charge::exchange_framed_json(  // 建立 TCP、按长度头一问一答。
            connect_to.substr(0, separator), connect_to.substr(separator + 1),  // 拆出主机和端口字符串。
            lines, interval_ms);  // 按 --interval-ms 间隔发送，0 表示连发。
        if (responses.size() != lines.size()) {  // 少一帧 ACK 就说明半包或中途断线。
            throw std::runtime_error("server response count mismatch");  // 半包或断线，不算成功。
        }  // 请求与响应数量对齐后才能逐帧对 requestId。

        int acknowledged = 0;  // 服务端 ok:true 的帧数，打印到 ACKNOWLEDGED_FRAMES。
        int rejected = 0;  // 非 ok 的帧数；倒退/未知桩场景必须大于 0。
        std::string latest_accepted_meter = meter_start;  // 只在确认成功时推进电表，拒绝帧不回写。
        for (std::size_t i = 0; i < responses.size(); ++i) {  // 逐条核对 ACK 是否对应本帧 requestId。
            const std::string expected_id = "\"requestId\":\"PILE-"  // ACK 必须回带本帧对账号。
                + std::to_string(pile) + '-' + std::to_string(i + 1) + "\"";  // 例如 "requestId":"PILE-101-1"。
            if (responses[i].find(expected_id) == std::string::npos) {  // ACK 对不上序号，可能串包。
                throw std::runtime_error("server response requestId mismatch: " + responses[i]);  // 把原始 ACK 打出来便于排错。
            }  // requestId 对齐。
            if (response_ok(responses[i])) {  // 成员1接受本帧遥测。
                ++acknowledged;  // 计入成功帧。
                latest_accepted_meter = simulator.energy_for_frame(frames[i]);  // 只保存服务端已确认的读数。
            } else {  // 服务端拒绝：倒退、未知桩或其它校验失败。
                ++rejected;  // 计入拒绝帧。
                std::cout << "EXPECTED_SERVER_REJECTION=" << responses[i] << '\n';  // 把拒绝原因打到 stdout，答辩可见。
            }  // 单帧处理结束。
        }  // 全部 ACK 统计完毕。
        if (charge::scenario_expects_rejection(kind)) {  // 时间倒退 / 电量倒退 / 未知桩。
            if (rejected == 0) throw std::runtime_error("invalid scenario was not rejected");  // 服务端全收了，说明校验没生效。
        } else if (rejected != 0) {  // 合法场景不该被拒。
            if (acknowledged > 0) save_meter(meter_path, latest_accepted_meter);  // 部分成功也要保住已确认电表，避免下次倒退。
            throw std::runtime_error("server rejected valid telemetry");  // 充电/过热等被拒则失败。
        }  // 接受/拒绝策略与场景类型匹配。
        if (acknowledged > 0) save_meter(meter_path, latest_accepted_meter);  // 全部成功或负例中夹杂的成功帧，电表都要落盘。

        std::cout << "SCENARIO=" << charge::scenario_name(kind) << '\n';  // 回显实际跑的场景名。
        std::cout << "SENT_FRAMES=" << lines.size() << '\n';  // 发出去的 JSON 条数。
        std::cout << "ACKNOWLEDGED_FRAMES=" << acknowledged << '\n';  // 服务端 ok:true 条数，答辩用来证明真连通。
        std::cout << "REJECTED_FRAMES=" << rejected << '\n';  // 被拒绝条数，倒退场景应大于 0。
        std::cout << "LIFETIME_METER_KWH=" << latest_accepted_meter << '\n';  // 本轮结束后的累计电表。
        return 0;  // 按场景规则验收通过。
    } catch (const std::exception& error) {  // 参数、网络、协议、业务拒绝策略失败都走这里。
        std::cerr << "ERROR: " << error.what() << '\n';  // 错误打到 stderr，启动脚本会记入 analytics.err.log。
        return 2;  // 非 0 退出，启动脚本和大屏按钮都把它当成失败。
    }  // 正常路径已在 try 里 return 0。
}  // 进程结束。

/*
【文件讲解·答辩用】
功能作用：充电桩模拟器的真正入口。解析 --pile / --scenario / --connect 等参数，读取持久化累计电表，把固定场景编成 JSON，
          经 4 字节长度头 TCP 发给成员1；只有 ACK 里 "ok":true 且 requestId 对得上才计入成功。倒退和未知桩必须看到拒绝才算成功。
技术原理：累计电表不放内存、不放 SQLite，而放纯文本状态文件（脚本在 Windows 上放到 LOCALAPPDATA，避开中文路径）。
          先写 .tmp 再 rename，防止写到一半损坏。TCP 一问一答，半包或 ACK 数量不够直接失败，绝不伪造成功。
代码思路：扫 argv → parse_scenario → 未知桩改 999999999 → load_meter → 无 --connect 则本地打印 → 有 --connect 则组 JSON、
          exchange_framed_json → 核对 requestId 和 ok → 按场景决定拒绝是否合法 → save_meter → 打印 SENT/ACK/REJECTED。
与其他文件关系：组 JSON 用 pile_simulator.h；发 TCP 用 protocol_frame.h；帧序列用 scenario.cpp。analytics 的 simulator_control.py
          和大屏按钮最终 subprocess 调本程序。tcp_loopback_test.py 用假服务端验证本入口的 --connect 路径。
答辩时可以怎么讲：先跑 `pile_simulator --scenario charging --connect 127.0.0.1:48080`，指着 stdout 说 SENT_FRAMES 和
          ACKNOWLEDGED_FRAMES 必须相等才算真连通；再切 time-backward，必须出现 EXPECTED_SERVER_REJECTION，退出码仍按“负例成功”处理。
          强调电表文件让重启后不会从 0 开始，否则服务端会说电量倒退。
*/
