#pragma once  // TCP 帧编解码：4 字节网络序长度头 + UTF-8 JSON，与成员1协议一致。

#include <cstdint>  // 长度头用 uint32_t，字节缓冲用 uint8_t。
#include <string>   // 一帧 JSON 正文，以及服务端返回的 ACK 字符串。
#include <vector>   // 编码后的字节帧，以及一批请求/响应对。

namespace charge {  // 与场景、模拟器同一命名空间，main 直接调用 encode_frame / exchange_framed_json。

constexpr std::uint32_t MaxPayload = 1024U * 1024U;  // 超长帧直接拒绝，防止把服务端拖死。

std::vector<std::uint8_t> encode_frame(const std::string& payload);  // 把 JSON 包成「长度 + 正文」。

std::vector<std::string> exchange_framed_json(  // 连接 HOST:PORT，逐帧发送并等待对应响应。
    const std::string& host, const std::string& port,  // 成员1监听地址，答辩常用 127.0.0.1 和 48080。
    const std::vector<std::string>& payloads, int interval_ms);  // payloads 是已组好的 JSON；interval_ms 为帧间隔，0 表示连发。

}  // namespace charge

/*
【文件讲解·答辩用】
功能作用：约定充电桩模拟器与成员1服务端之间的 TCP 粘包格式：每条消息先发 4 字节大端长度，再发等长 UTF-8 JSON。
          encode_frame 只打包；exchange_framed_json 负责真正 connect、一问一答、按间隔发送。连不上服务端就失败，不造假 ACK。
技术原理：TCP 是字节流，没有消息边界，所以必须自己加长度头。长度用网络字节序（大端）四个字节，和 Java/Netty、多数物联网网关一致。
          超过 1MB 的正文直接拒绝，避免异常长度把双方缓冲区拖垮。
代码思路：头文件只暴露两个函数和一个上限常量；Windows/Linux 套接字差异全部藏在 protocol_frame.cpp 里。main 组好 JSON 列表后一次性交给 exchange_framed_json。
与其他文件关系：实现在 protocol_frame.cpp；调用方是 main.cpp（联调发遥测）和 scenario_test.cpp（只测 encode_frame 长度头）。
          成员1服务端必须按同样的 4 字节长度头拆包，否则大屏看不到 ACKNOWLEDGED_FRAMES。
答辩时可以怎么讲：拿 Wireshark 或回环测试说“先看到 00 00 00 xx 四个字节，后面才是 {\"version\":\"1.0\"...}”；
          强调这不是 HTTP，是桩与平台的私有长度头协议；tcp_loopback_test.py 就是在本机假扮服务端验证这一层。
*/
