// 把固定场景帧编成 PILE_TELEMETRY JSON：version=1.0、带时区 recordedAt、累计电表=起点+增量。
#include "pile_simulator.h"  // 类声明：桩号、场景、起点时间和百分之一 kWh 电表。

#include <algorithm>  // all_of 校验电量字符串每一位都是数字；min 限制本地打印帧数。
#include <chrono>     // 无 --connect 时按 --interval-ms 在两帧 JSON 之间休眠。
#include <cctype>     // isdigit 检查电量整数部分和小数部分。
#include <ctime>      // time / tm，用来把帧偏移编成 UTC recordedAt。
#include <iomanip>    // put_time、setw、setfill，输出 ISO-8601 和两位小数电表。
#include <iostream>   // run() 把 JSON 打到标准输出，给本地查看帧内容。
#include <sstream>    // 拼 JSON、拼时间和拼 "100.06" 这种电表文本。
#include <stdexcept>  // 电量格式非法、桩号非正、时间缺时区都抛 invalid_argument。
#include <thread>     // sleep_for，模拟真实上报间隔。

namespace charge {  // 与头文件同一命名空间。
namespace {  // 时间换算和电量整数运算只给本文件用。

std::string utc_time_after(int offset_seconds) {  // 没有 --start-time 时：当前 UTC 再加帧偏移。
    const std::time_t value = std::time(nullptr) + offset_seconds;  // 以现在为起点，充电第二帧就是 +5 秒。
    std::tm utc{};  // 存放拆开的年月日时分秒。
#ifdef _WIN32  // Windows CRT 用 gmtime_s，线程安全且参数顺序相反。
    gmtime_s(&utc, &value);  // 把 time_t 拆成 UTC 日历时间。
#else  // Linux/macOS 用 POSIX gmtime_r。
    gmtime_r(&value, &utc);  // 同样拆成 UTC，避免 gmtime 静态缓冲。
#endif  // 跨平台取出 utc 结构后统一格式化。
    std::ostringstream out;  // 按协议要求输出带 Z 的 UTC。
    out << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");  // 例如 2026-09-10T01:00:00Z。
    return out.str();  // 作为默认 start_time_ 或直接当 recordedAt。
}  // 联调若要可重复时间戳，应改传 --start-time，走 utc_time_from_base。

long long parse_hundredths(const std::string& value) {  // 把 "100.06" 收成整数 10006，单位百分之一 kWh。
    const auto dot = value.find('.');  // 必须有且只有两位小数，协议禁止科学计数法。
    if (dot == std::string::npos || value.size() - dot != 3 || dot == 0) {  // 无小数点、不是两位、或小数点在开头都不收。
        throw std::invalid_argument("energy must use a non-negative decimal with two digits");  // 例如 100、.06、100.6 都非法。
    }  // 到这里一定是 整数部分 + '.' + 两位。
    const std::string whole = value.substr(0, dot);  // 整数 kWh，如 100。
    const std::string fraction = value.substr(dot + 1);  // 两位小数，如 06。
    const auto digit = [](unsigned char character) { return std::isdigit(character) != 0; };  // 只认 0-9，禁止符号和空白。
    if (!std::all_of(whole.begin(), whole.end(), digit)  // 整数部分必须全是数字。
        || !std::all_of(fraction.begin(), fraction.end(), digit)) {  // 整数或小数里混入字母就拒绝。
        throw std::invalid_argument("energy must use a non-negative decimal with two digits");  // 与上面同一句，方便调用方统一处理。
    }  // 文本合法后再做整数换算。
    return std::stoll(whole) * 100 + std::stoll(fraction);  // 100.06 → 10006，后续只做整数加减。
}  // 这样 0.01+0.02 一定是 0.03，不会出现二进制浮点尾巴。

std::string format_hundredths(long long value) {  // 把百分之一 kWh 整数打回两位小数字符串。
    if (value < 0) throw std::invalid_argument("energy cannot be negative");  // 累计电表不允许负数，倒退由服务端拒帧而不是这里发负值。
    std::ostringstream out;  // 手动拼 "整.分"，不用 iostream 默认精度。
    out << value / 100 << '.' << std::setw(2) << std::setfill('0') << value % 100;  // 10006 → "100.06"；5 → "0.05"。
    return out.str();  // 写入 JSON 的 energyKwh 字段。
}  // 输出永远两位小数，和数据库 TEXT/DECIMAL 对账方便。

// Parse ISO-8601 once and emit UTC. This guarantees that equal offsets really
// produce duplicate timestamps and negative offsets exercise server validation.
// 先解析一次带时区的起点，再按帧偏移生成 UTC：同一偏移必得同一时间戳，负偏移才能打到服务端乱序校验。
std::string utc_time_from_base(const std::string& base, int offset_seconds) {  // --start-time + 帧 offset → recordedAt。
    if (base.size() < 20) throw std::invalid_argument("start time must be ISO-8601 with timezone");  // 最短形如 2026-09-07T01:00:00Z。
    std::tm parsed{};  // 只解析到秒，不含时区；时区后面单独处理。
    std::istringstream input(base.substr(0, 19));  // 截取 YYYY-MM-DDTHH:MM:SS。
    input >> std::get_time(&parsed, "%Y-%m-%dT%H:%M:%S");  // 填进 tm，失败则日期格式不对。
    if (input.fail()) throw std::invalid_argument("start time must be ISO-8601 with timezone");  // 例如漏了 T 或用了空格分隔。
#ifdef _WIN32  // Windows 把“当作 UTC 的 tm”收成 time_t 用 _mkgmtime。
    std::time_t value = _mkgmtime(&parsed);  // 此时 value 还没扣时区。
#else  // POSIX 等价函数是 timegm。
    std::time_t value = timegm(&parsed);  // 同样先当 UTC 日历时间。
#endif  // 接下来用第 20 个字符判断 Z 还是 +08:00。
    int zone_seconds = 0;  // Z 表示 0；+08:00 则为 +28800，后面要从本地墙上时间扣掉。
    if (base[19] != 'Z') {  // 带偏移的东八区写法，答辩测试固定用 2026-09-07T09:00:00+08:00。
        if (base.size() != 25 || (base[19] != '+' && base[19] != '-') || base[22] != ':') {  // 必须是 ±HH:MM 共 6 个字符。
            throw std::invalid_argument("start time must include Z or an offset such as +08:00");  // 不收 +0800 或 Z+08:00。
        }  // 偏移格式合法。
        const int sign = base[19] == '+' ? 1 : -1;  // +08:00 为正，-05:00 为负。
        zone_seconds = sign * (std::stoi(base.substr(20, 2)) * 3600  // 时区小时部分，例如 +08:00 的 08。
                               + std::stoi(base.substr(23, 2)) * 60);  // 小时×3600 + 分钟×60，再乘正负号。
    }  // Z 分支 zone_seconds 保持 0。
    value += offset_seconds - zone_seconds;  // 先把墙上时间扭到 UTC，再加上本帧秒偏移（倒退场景偏移更小）。
    std::tm utc{};  // 准备输出 UTC 日历。
#ifdef _WIN32  // 把 UTC time_t 再拆回日历，供 put_time 输出 Z 后缀。
    gmtime_s(&utc, &value);  // Windows 安全版 gmtime。
#else  // POSIX 可重入版本。
    gmtime_r(&value, &utc);  // POSIX 可重入 gmtime。
#endif  // 两种 CRT 都得到同一份 UTC tm。
    std::ostringstream out;  // 协议字段 recordedAt 统一带 Z。
    out << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");  // +08:00 的 09:00 会变成 01:00Z，scenario_test 按此断言。
    return out.str();  // 写入 JSON payload.recordedAt。
}  // 重复场景两个 offset 都是 0，因此两条 recordedAt 完全相同。

}  // namespace

PileSimulator::PileSimulator(long long pile_id, ScenarioKind scenario, std::string start_time,  // 记下桩号、场景。
                             std::string starting_energy_kwh)  // 记下桩号、场景、起点和电表初值。
    : pile_id_(pile_id),  // 必须是 pile_info.id；未知桩由 main 改成 999999999。
      scenario_(scenario),  // 决定 json 里的 scenario 字段和用哪组固定帧。
      start_time_(start_time.empty() ? utc_time_after(0) : std::move(start_time)),  // 没给起点就用此刻 UTC。
      starting_energy_hundredths_(parse_hundredths(starting_energy_kwh)) {  // "100.00" → 10000 百分之一 kWh。
    if (pile_id_ <= 0) throw std::invalid_argument("pile id must be a positive integer");  // 0 或负数不是合法主键。
}  // 构造完成后即可 json_line / run。

std::string PileSimulator::energy_for_frame(const TelemetryFrame& frame) const {  // 生命周期电表 = 起点 + 本帧增量。
    return format_hundredths(starting_energy_hundredths_ + parse_hundredths(frame.energy_kwh));  // 100.00 + 0.06 → "100.06"。
}  // main 只在 ACK ok:true 后才把这个值写入状态文件。

std::string PileSimulator::json_line(const TelemetryFrame& frame, int sequence) const {  // 编一条成员1能认的遥测 JSON。
    std::ostringstream out;  // 手工拼接，字段顺序固定，便于测试用 find 断言。
    const std::string recorded_at = utc_time_from_base(start_time_, frame.offset_seconds);  // 起点加偏移得到本帧时间。
    out << "{\"version\":\"1.0\",\"requestId\":\"PILE-" << pile_id_ << '-' << sequence  // 协议版本和 PILE-101-1 这种对账号。
        << "\",\"action\":\"PILE_TELEMETRY\""  // 告诉服务端这是桩遥测，不是命令回执。
        << ",\"payload\":{\"pileId\":" << pile_id_  // 必须能在 pile_info 找到；未知桩场景会找不到。
        << ",\"recordedAt\":\"" << recorded_at  // UTC 时间，供乱序检测。
        << "\",\"scenario\":\"" << scenario_name(scenario_)  // 回显场景名，方便服务端日志和大屏。
        << "\",\"status\":\"" << frame.status  // IDLE / CHARGING / FAULT / OFFLINE，最终入库由服务端裁决。
        << "\",\"powerW\":" << frame.power_w  // 功率瓦；充电约 7000，过热前 20000，故障后 0。
        << ",\"temperatureC\":" << frame.temperature_c  // 摄氏度；过热最后一帧 71。
        << ",\"voltageV\":" << frame.voltage_v  // 伏特。
        << ",\"currentA\":" << frame.current_a  // 安培。
        << ",\"energyKwh\":\"" << energy_for_frame(frame)  // 累计电表字符串，不是本帧增量。
        << "\",\"event\":\"" << frame.event << "\",\"dataMode\":\"SIMULATED\"}}";  // 事件标签 + 标明这是模拟数据。
    return out.str();  // 交给 protocol_frame 加长度头，或 run() 直接打印。
}  // 字段名与成员1解析器必须逐字一致。

int PileSimulator::run(int count, int interval_ms) const {  // 无 --connect：只把 JSON 打到 stdout，不发 TCP。
    const auto frames = fixed_scenario(scenario_);  // 取出当前场景的固定帧。
    const int limit = count <= 0 ? static_cast<int>(frames.size())  // --count 0 表示打印该场景全部固定帧。
                                 : std::min(count, static_cast<int>(frames.size()));  // --count 不能超过场景长度。
    for (int i = 0; i < limit; ++i) {  // 序号从 1 开始，与联调路径的 requestId 规则相同。
        std::cout << json_line(frames[static_cast<std::size_t>(i)], i + 1) << '\n';  // 一行一条 JSON，便于管道查看。
        if (interval_ms > 0 && i + 1 < limit) {  // 最后一帧不必再睡。
            std::this_thread::sleep_for(std::chrono::milliseconds(interval_ms));  // 模拟真实上报节拍。
        }  // 间隔分支结束。
    }  // 本地打印结束。
    return 0;  // 本地模式只要能打印就算成功，不检查 ACK。
}  // 答辩联调必须走 main 的 --connect 分支，不要只跑 run()。

}  // namespace charge

/*
【文件讲解·答辩用】
功能作用：把 scenario.cpp 的固定帧变成成员1协议 JSON。核心有三件事：用百分之一 kWh 整数累加电表、把带时区的起点加偏移编成 UTC recordedAt、
          填齐 version/requestId/action/payload。无网模式 run() 只打印，真正发 TCP 不在本文件。
技术原理：电量不用 double 累加，避免 0.1+0.2 误差；时间先解析 ISO-8601（支持 Z 和 +08:00），再统一输出 ...Z，这样 duplicate 两帧时间戳字节级相同，
          time-backward 第二帧一定更早。Windows 用 gmtime_s/_mkgmtime，Linux 用 gmtime_r/timegm，语义一致。
代码思路：匿名命名空间里做时间与电量工具 → 构造函数记下状态 → energy_for_frame 做加法 → json_line 按固定字段顺序拼接 →
          run 按 count/interval 打印。requestId 为 PILE-{桩号}-{序号}，ACK 必须带同一字符串。
与其他文件关系：声明在 pile_simulator.h；帧来自 scenario.h；main.cpp 在 TCP 成功后调用 energy_for_frame 回写状态文件；
          protocol_frame.cpp 给 json_line 的返回值加 4 字节长度头。scenario_test.cpp 用 +08:00 起点断言 recordedAt 变成 01:00Z，电表 100.00→100.01。
答辩时可以怎么讲：打开 json_line 指着 energyKwh 说“这里是累计电表，充电从 100.00 加到 100.06，不是把 0.06 当订单电量”；
          再指 recordedAt 说东八区 09:00 转成 01:00Z，和测试断言一致；dataMode=SIMULATED 让服务端/大屏知道这是模拟桩不是真桩。
*/
