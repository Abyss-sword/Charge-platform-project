// 4 字节网络序长度头 + JSON。Windows 用 Winsock，Linux 用 POSIX socket。连不上服务端就失败，不造假 ACK。
#include "protocol_frame.h"  // MaxPayload、encode_frame、exchange_framed_json 的声明。

#include <chrono>     // 帧与帧之间按 --interval-ms 休眠，模拟真实上报节拍。
#include <cstring>    // memcpy 把 JSON 正文拷进「长度头 + 正文」缓冲区。
#include <stdexcept>  // 空包、超长、send/recv 失败、解析地址失败都抛异常给 main。
#include <thread>     // sleep_for，只在 interval_ms>0 且还有下一帧时调用。

#ifdef _WIN32  // Windows 没有 POSIX socket，必须走 Winsock2。
#define WIN32_LEAN_AND_MEAN  // 少拉 Windows.h 里用不到的宏，避免和后头类型打架。
#include <winsock2.h>  // SOCKET、send/recv、WSAStartup。
#include <ws2tcpip.h>  // getaddrinfo / freeaddrinfo。
using SocketHandle = SOCKET;  // Windows 套接字句柄不是 int。
constexpr SocketHandle InvalidSocket = INVALID_SOCKET;  // 非法句柄哨兵。
#else  // Linux/macOS 用 BSD socket。
#include <netdb.h>       // getaddrinfo 解析 127.0.0.1 和端口。
#include <sys/socket.h>  // socket / connect / send / recv。
#include <unistd.h>      // close() 关掉套接字。
using SocketHandle = int;  // POSIX 套接字就是文件描述符。
constexpr SocketHandle InvalidSocket = -1;  // 非法描述符。
#endif  // 后面 close_socket / send_all 用同一套 SocketHandle。

namespace charge {  // 与头文件同一命名空间。

std::vector<std::uint8_t> encode_frame(const std::string& payload) {  // 把一条 JSON 包成 4 字节大端长度 + 正文。
    if (payload.empty() || payload.size() > MaxPayload) throw std::invalid_argument("payload length is invalid");  // 空包或超过 1MB 直接拒绝。
    const auto size = static_cast<std::uint32_t>(payload.size());  // UTF-8 字节数，不是字符数。
    std::vector<std::uint8_t> frame(4 + payload.size());  // 前 4 字节长度头，后面紧跟 JSON。
    frame[0] = static_cast<std::uint8_t>((size >> 24) & 0xffU);  // 长度最高字节，网络序（大端）。
    frame[1] = static_cast<std::uint8_t>((size >> 16) & 0xffU);  // 次高字节。
    frame[2] = static_cast<std::uint8_t>((size >> 8) & 0xffU);  // 次低字节。
    frame[3] = static_cast<std::uint8_t>(size & 0xffU);  // 最低字节；小包时常见 00 00 00 xx。
    std::memcpy(frame.data() + 4, payload.data(), payload.size());  // 长度头后面原样拷贝 JSON，不加 '\0'。
    return frame;  // 交给 send_all 一次或多次发完。
}  // scenario_test 会反解前 4 字节，确认等于 JSON.size()。

namespace {  // 套接字收发细节不对库用户暴露。

void close_socket(SocketHandle socket) {  // 跨平台关掉套接字，避免句柄泄漏。
#ifdef _WIN32  // Winsock 用 closesocket，不能当文件描述符 close。
    closesocket(socket);  // Winsock 不能用 close()。
#else  // POSIX 套接字就是 fd。
    close(socket);  // POSIX 文件描述符。
#endif  // 关闭后调用方把句柄视为失效。
}  // 调用方在成功路径和异常路径都要关。

void send_all(SocketHandle socket, const std::vector<std::uint8_t>& bytes) {  // 循环 send，直到整帧发出，防止短写。
    std::size_t sent = 0;  // 已经成功写出的字节数。
    while (sent < bytes.size()) {  // TCP 一次不一定发完整帧。
#ifdef _WIN32  // Winsock 的 send 参数类型与 POSIX 不同。
        const int count = send(socket, reinterpret_cast<const char*>(bytes.data() + sent), static_cast<int>(bytes.size() - sent), 0);  // Winsock 缓冲类型是 char*，长度是 int。
#else  // Linux 可直接传剩余缓冲区。
        const auto count = send(socket, bytes.data() + sent, bytes.size() - sent, 0);  // POSIX 可直接传 void*/size_t。
#endif  // count 是本次实际写出的字节数。
        if (count <= 0) throw std::runtime_error("TCP send failed");  // 对端关掉或出错，不假装已送达。
        sent += static_cast<std::size_t>(count);  // 累加本次写出的字节。
    }  // 整帧发送完成。
}  // 与 receive_exact 对称，保证一问一答边界清晰。

std::vector<std::uint8_t> receive_exact(SocketHandle socket, std::size_t size) {  // 循环 recv，直到读满 size 字节。
    std::vector<std::uint8_t> bytes(size);  // 先按目标长度分配，例如先读 4 再读正文。
    std::size_t received = 0;  // 已经读到的字节数。
    while (received < size) {  // TCP 可能把长度头和正文拆成多次到达。
#ifdef _WIN32  // Winsock recv 要 char* 和 int 长度。
        const int count = recv(socket, reinterpret_cast<char*>(bytes.data() + received),  // 从已读偏移处继续填。
                               static_cast<int>(size - received), 0);  // Winsock recv 同样要 char* / int。
#else  // POSIX recv 用 size_t。
        const auto count = recv(socket, bytes.data() + received, size - received, 0);  // POSIX recv。
#endif  // count<=0 表示对端关闭或出错。
        if (count <= 0) throw std::runtime_error("TCP receive failed");  // 0 表示对端关闭，半包不算成功。
        received += static_cast<std::size_t>(count);  // 累加本次读入。
    }  // 读满才返回。
    return bytes;  // 调用方再按大端拼长度或转成 string。
}  // 这是处理粘包/拆包的核心：先定长再按长度读正文。

std::string receive_frame(SocketHandle socket) {  // 读一帧完整 ACK：4 字节长度 + JSON。
    const auto header = receive_exact(socket, 4);  // 先拿长度头。
    const auto size = (static_cast<std::uint32_t>(header[0]) << 24U)  // 大端最高字节。
        | (static_cast<std::uint32_t>(header[1]) << 16U)  // 次高。
        | (static_cast<std::uint32_t>(header[2]) << 8U)  // 次低。
        | static_cast<std::uint32_t>(header[3]);  // 最低；拼出正文字节数。
    if (size == 0 || size > MaxPayload) throw std::runtime_error("invalid response frame length");  // 0 或超 1MB 视为协议错误。
    const auto payload = receive_exact(socket, size);  // 再精确读这么多字节的 ACK JSON。
    return std::string(payload.begin(), payload.end());  // 交给 main 查 "ok":true 和 requestId。
}  // 不在这里解析 JSON，避免本层依赖第三方库。

}  // 匿名命名空间结束。

std::vector<std::string> exchange_framed_json(  // 连上成员1，逐帧发送遥测并收集对应 ACK。
    const std::string& host, const std::string& port,  // 例如 127.0.0.1 和 48080。
    const std::vector<std::string>& payloads, int interval_ms) {  // payloads 已是组好的 PILE_TELEMETRY JSON。
#ifdef _WIN32  // 进程用 Winsock 前必须 WSAStartup，否则后续 socket 全失败。
    WSADATA data{};  // Winsock 启动块，进程内使用 socket 前必须 WSAStartup。
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) throw std::runtime_error("WSAStartup failed");  // 2.2 才能用 getaddrinfo。
#endif  // Linux 不需要启动套接字库。
    addrinfo hints{};  // 告诉解析器我们要 TCP 流套接字。
    hints.ai_family = AF_UNSPEC;  // IPv4 / IPv6 都可以，答辩一般是 127.0.0.1。
    hints.ai_socktype = SOCK_STREAM;  // TCP，不是 UDP。
    addrinfo* addresses = nullptr;  // getaddrinfo 返回的链表。
    if (getaddrinfo(host.c_str(), port.c_str(), &hints, &addresses) != 0) {  // 主机名或端口写错会失败。
#ifdef _WIN32  // 解析失败也要配对 WSACleanup，否则重复调用会泄漏。
        WSACleanup();  // 解析失败也要配对清理 Winsock。
#endif  // POSIX 无对应清理。
        throw std::runtime_error("cannot resolve server address");  // 连 DNS/字面量都解析不了，谈不上 ACK。
    }  // 下面遍历地址列表尝试 connect。
    SocketHandle socket = InvalidSocket;  // 尚未连上。
    for (auto* current = addresses; current != nullptr; current = current->ai_next) {  // IPv6 失败就试 IPv4。
        socket = ::socket(current->ai_family, current->ai_socktype, current->ai_protocol);  // 按该地址族建套接字。
        if (socket == InvalidSocket) continue;  // 这种族建不了就试下一个。
        if (::connect(socket, current->ai_addr, static_cast<int>(current->ai_addrlen)) == 0) break;  // 连上成员1监听端口则停止尝试。
        close_socket(socket);  // 这个地址连不上，关掉再试下一个。
        socket = InvalidSocket;  // 标记尚未成功。
    }  // 链表走完仍 InvalidSocket 就说明服务端没开。
    freeaddrinfo(addresses);  // 无论成败都释放解析结果。
    if (socket == InvalidSocket) {  // 所有地址都 connect 失败。
#ifdef _WIN32  // connect 全失败时停掉 Winsock。
        WSACleanup();  // 失败路径也要停 Winsock。
#endif  // 随后抛 cannot connect，启动脚本会当成模拟器失败。
        throw std::runtime_error("cannot connect to server");  // 大屏按钮看到这个就是成员1没起来或端口不对。
    }  // 至此 TCP 已建立。
    std::vector<std::string> responses;  // 按发送顺序收集 ACK JSON。
    try {  // 中途 send/recv 失败要关套接字再把异常抛给 main。
        for (std::size_t i = 0; i < payloads.size(); ++i) {  // 严格一问一答，发完一帧再等对应 ACK。
            send_all(socket, encode_frame(payloads[i]));  // 先发长度头+遥测 JSON。
            responses.push_back(receive_frame(socket));  // 再阻塞读服务端响应。
            if (interval_ms > 0 && i + 1 < payloads.size()) std::this_thread::sleep_for(std::chrono::milliseconds(interval_ms));  // 非末帧才休眠。
        }  // 全部帧交换完成。
    } catch (...) {  // 半包、对端断开、非法长度头。
        close_socket(socket);  // 异常也要关连接。
#ifdef _WIN32  // 收发异常同样要停 Winsock，再把原异常抛给 main。
        WSACleanup();  // 异常路径同样配对清理。
#endif  // 不吞异常，避免假成功。
        throw;  // 原样抛出，main 打 ERROR 并以退出码 2 失败。
    }  // 成功路径落到下面的正常关闭。
    close_socket(socket);  // 发完即关，模拟器不是长连接守护进程。
#ifdef _WIN32  // 正常发完后释放 Winsock。
    WSACleanup();  // 本轮 TCP 结束，释放 Winsock。
#endif  // Linux 到此只需已经 close 了套接字。
    return responses;  // 交给 main 统计 ok:true / 拒绝原因。
}  // 本函数从不自己造 ACK，连不上就是失败。

}  // namespace charge

/*
【文件讲解·答辩用】
功能作用：充电桩模拟器的 TCP 传输层。把 JSON 加上 4 字节大端长度头发给成员1，再按同样格式把 ACK 读回来。
          Windows 走 Winsock，Linux 走 POSIX socket。解析失败、connect 失败、短写、半包一律抛异常，绝不伪造 "ok":true。
技术原理：TCP 是字节流，必须用长度前缀划分消息。编码是 size>>24..size&0xff 四个字节再 memcpy 正文；解码先 read(4) 再 read(N)。
          send_all / receive_exact 循环处理短写短读。MaxPayload=1MB 防止异常长度把双方拖死。
代码思路：encode_frame 只打包 → close_socket/send_all/receive_exact/receive_frame 藏在匿名命名空间 →
          exchange_framed_json 里 WSAStartup（仅 Windows）、getaddrinfo、遍历 connect、一问一答、成对 WSACleanup。
与其他文件关系：头文件 protocol_frame.h 给 main.cpp 和 scenario_test.cpp 用。main 组好 JSON 列表后只调 exchange_framed_json。
          tcp_loopback_test.py 用 Python struct.pack("!I", ...) 扮演服务端，验证本文件与假平台能对上。
答辩时可以怎么讲：画一张「4 字节长度 | JSON」的图，说这和 HTTP 不同，是桩与平台的私有帧；然后说回环测试里 Python 先 unpack 四个字节再 json.loads，
          再回同样格式的 ok:true，C++ 侧 ACKNOWLEDGED_FRAMES 才会等于 SENT_FRAMES。成员1没启动时这里会报 cannot connect to server。
*/
