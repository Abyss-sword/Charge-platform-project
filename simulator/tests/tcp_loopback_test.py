from __future__ import annotations  # 让 Socket 类型标注在 3.9 下也能写 receive_exact(...) -> bytes。

import json  # 把收到的 UTF-8 遥测解成 dict，并 dumps 紧凑 ACK。
import os  # 用当前测试进程号生成互不冲突的累计电表状态文件名。
import socket  # 在 127.0.0.1 上开一个假的成员1服务端，只做长度头回环。
import struct  # pack/unpack 网络序 4 字节长度，与 C++ encode_frame 同一规则。
import subprocess  # 拉起 pile_simulator --connect，验证真入口而不是只测库函数。
import sys  # argv[1] 传入已编译的 pile_simulator 路径。
import tempfile  # 状态文件放到系统临时目录，避开仓库中文路径让 MinGW 翻车。
from pathlib import Path  # 解析可执行文件绝对路径，并拼 pile-loopback-{pid}.energy。

def receive_exact(connection: socket.socket, size: int) -> bytes:  # 循环 recv，直到凑齐长度头或 JSON 正文。
    chunks = bytearray()  # 累积本次要读的定长字节。
    while len(chunks) < size:  # TCP 可能把 4 字节长度头拆成多次到达。
        chunk = connection.recv(size - len(chunks))  # 还差多少就再读多少。
        if not chunk:  # recv 返回空表示模拟器提前关掉了连接。
            raise AssertionError("connection closed before complete frame")  # 半包不能当测试通过。
        chunks.extend(chunk)  # 把本次读到的字节接上去。
    return bytes(chunks)  # 交给 unpack 或 json.loads。


def main() -> None:  # 假服务端 + 真模拟器进程的 TCP 回环验收。
    executable = Path(sys.argv[1]).resolve()  # CMake/脚本传入的 pile_simulator 绝对路径。
    state_file = Path(tempfile.gettempdir()) / f"pile-loopback-{os.getpid()}.energy"  # 每次运行用新的临时电表文件，避免吃到上次 100.06。
    state_file.unlink(missing_ok=True)  # 若同一进程号的旧残留还在，先删再启动模拟器。
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as server:  # IPv4 TCP 监听套接字，离开 with 自动关闭。
        server.bind(("127.0.0.1", 0))  # 绑本机，端口 0 表示让操作系统挑空闲端口，避免和 48080 真服务端抢。
        server.listen(1)  # 只接受模拟器这一条连接。
        port = server.getsockname()[1]  # 取出实际端口，填进 --connect 127.0.0.1:PORT。
        process = subprocess.Popen([str(executable), "--scenario", "normal", "--pile", "101", "--count", "2", "--interval-ms", "0", "--connect", f"127.0.0.1:{port}", "--state-file", str(state_file)], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)  # 跑空闲两帧、尽快发送、电表写到临时文件。
        connection, _ = server.accept()  # 阻塞等到模拟器 connect 成功。
        try:  # 交换两帧期间若模拟器崩溃，把 stdout/stderr 带进断言。
            with connection:  # 结束时关掉对模拟器的套接字。
                payloads = []  # 按到达顺序保存解出的遥测 JSON。
                for _ in range(2):  # normal 场景固定两帧，与 --count 2 一致。
                    length = struct.unpack("!I", receive_exact(connection, 4))[0]  # 先读 4 字节大端长度，对应 C++ encode_frame。
                    assert 0 < length <= 1024 * 1024  # 与 MaxPayload 一致，0 或超长视为协议错误。
                    payload = json.loads(receive_exact(connection, length))  # 再精确读正文并解析 PILE_TELEMETRY。
                    payloads.append(payload)  # 留下 requestId / version / action 等字段供后面断言。
                    response = json.dumps({"requestId": payload["requestId"], "ok": True, "code": "OK", "message": "success", "data": {}}, separators=(",", ":")).encode()  # 紧凑 ACK，requestId 原样回写，ok:true 让 C++ 计入成功。
                    connection.sendall(struct.pack("!I", len(response)) + response)  # 同样先长度头再正文，模拟成员1的写法。
        except Exception as error:  # accept 之后、两帧交换完之前的任何失败。
            stdout, stderr = process.communicate(timeout=5)  # 回收模拟器，拿到它打印的 ERROR。
            raise AssertionError(  # 把模拟器半途退出当成测试失败，而不是静默通过。
                f"simulator closed during frame exchange: code={process.returncode}, "  # 附上 C++ 退出码。
                f"stdout={stdout!r}, stderr={stderr!r}"  # 附上 SENT/ERROR 文本，方便看是协议错还是没连上。
            ) from error  # 把退出码和输出附上，避免只看到 Python 侧 traceback。
        stdout, stderr = process.communicate(timeout=5)  # 正常路径：等模拟器打印 SENT_FRAMES 后退出。
    assert process.returncode == 0, stderr  # 空闲场景两帧都应被假服务端接受，退出码必须是 0。
    summary = dict(line.split("=", 1) for line in stdout.splitlines() if "=" in line)  # 把 SCENARIO=normal 这类键值收成字典。
    assert summary["SCENARIO"] == "normal"  # 确认跑的就是空闲心跳，不是充电或倒退。
    assert summary["SENT_FRAMES"] == "2"  # 发出两帧 JSON。
    assert summary["ACKNOWLEDGED_FRAMES"] == "2"  # 假服务端两条都回了 ok:true，C++ 必须都点名成功。
    assert summary["REJECTED_FRAMES"] == "0"  # 合法空闲心跳不应被拒绝。
    assert [item["requestId"] for item in payloads] == ["PILE-101-1", "PILE-101-2"]  # 序号从 1 起，桩号 101，ACK 对账靠它。
    assert all(item["version"] == "1.0" for item in payloads)  # 协议版本字段。
    assert all(item["action"] == "PILE_TELEMETRY" for item in payloads)  # 必须是遥测动作，不是命令。
    assert all(item["payload"]["pileId"] == 101 for item in payloads)  # 对应种子库桩 101。
    assert all("recordedAt" in item["payload"] for item in payloads)  # 每帧都有采样时间，服务端靠它做乱序检测。
    assert all("voltageV" in item["payload"] and "currentA" in item["payload"] for item in payloads)  # 电压电流给大屏和告警用。
    assert all(item["payload"]["dataMode"] == "SIMULATED" for item in payloads)  # 标明模拟数据。
    assert all(isinstance(item["payload"]["energyKwh"], str) for item in payloads)  # 电表是两位小数字符串，不是 JSON number。
    state_file.unlink(missing_ok=True)  # 测完删掉临时累计电表，不带进仓库或下次运行。
    print("TCP_FRAME_TEST=PASS")  # 给调用脚本抓取的通过标记。


if __name__ == "__main__":  # 被 `python tcp_loopback_test.py ./pile_simulator` 直接调用。
    main()  # 跑完整回环；失败则以非 0 退出。

# 【文件讲解·答辩用】
# 功能作用：在本机假装成员1服务端，拉起真正的 pile_simulator --scenario normal --connect 127.0.0.1:随机端口，
#           验证 4 字节长度头、PILE_TELEMETRY JSON、requestId 对账，以及 C++ 入口对 ok:true 的 SENT/ACK 统计。
#           它测的是“模拟器进程 + TCP”，不是只测 encode_frame 函数。
# 技术原理：Python 监听 127.0.0.1:0 拿空闲端口，按 struct "!I" 解长度头（与 C++ 大端 uint32 一致），json.loads 正文后再
#           用同样格式回 {"ok":true,"requestId":...}。电表状态放到系统临时目录，避开仓库中文路径导致 MinGW 文件系统异常。
# 代码思路：准备临时 .energy → 听端口 → Popen 模拟器 → accept → 循环两帧收包并回 ACK → communicate 收 stdout →
#           断言退出码、SCENARIO/SENT/ACK/REJECTED、协议字段 → 删临时文件。交换中途崩溃会把模拟器 stderr 带进 AssertionError。
# 与其他文件关系：可执行文件来自 CMake 目标 pile_simulator（main.cpp + pile_core）。协议与 protocol_frame.cpp 对齐。
#           场景两帧来自 scenario.cpp 的 Normal。scenario_test.cpp 不连网；本文件补上连网路径。启动脚本可单独调用它做冒烟。
# 答辩时可以怎么讲：先说“就算成员1还没开，也能用这个脚本证明模拟器真的会按长度头发 TCP，而不是只 printf”；
#           然后指 stdout 的 ACKNOWLEDGED_FRAMES=2，说明 C++ 认的是假平台回的 ok:true，没有自己造 ACK。
#           若把假服务端改成 ok:false，模拟器会非 0 退出，更能说明成功判定在服务端而不在客户端。
