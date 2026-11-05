#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""20桩压测专用遥测客户端（T08/S2-T21，成员1）。

不修改模拟器（成员3单写者文件）：本客户端按 docs/protocol.md 的 4 字节长度头 + JSON
帧格式直连服务端，以固定频率发送 PILE_TELEMETRY 心跳（IDLE、电表恒定——服务端允许
等值电表），设备时间戳单调递增，天然满足时序保护；逐帧记录往返延迟。
结束时输出一行汇总：PILE/SENT/ACK/REJ/RECONN/LAT_P50_MS/LAT_P95_MS/LAT_MAX_MS。
"""
from __future__ import annotations

import argparse
import json
import socket
import statistics
import struct
import sys
import time
from datetime import datetime, timedelta, timezone
from typing import Any


def recv_exact(sock: socket.socket, size: int) -> bytes:  # 收满 size 字节，否则视为断连。
    buffer = b""
    while len(buffer) < size:
        chunk = sock.recv(size - len(buffer))
        if not chunk:
            raise ConnectionError("connection closed by server")
        buffer += chunk
    return buffer


def exchange(sock: socket.socket, request: dict[str, Any]) -> dict[str, Any]:  # 一帧一答。
    data = json.dumps(request, ensure_ascii=False).encode("utf-8")
    sock.sendall(struct.pack(">I", len(data)) + data)
    (length,) = struct.unpack(">I", recv_exact(sock, 4))
    if length <= 0 or length > 1024 * 1024:  # 与 FrameCodec 上限一致。
        raise ConnectionError(f"bad frame length {length}")
    return json.loads(recv_exact(sock, length).decode("utf-8"))


def main() -> int:
    parser = argparse.ArgumentParser(description="压测遥测客户端：1Hz 心跳 + 延迟统计")
    parser.add_argument("--pile", type=int, required=True)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, required=True)
    parser.add_argument("--duration", type=int, default=600, help="发送时长（秒）")
    parser.add_argument("--rate", type=float, default=1.0, help="每桩帧率（Hz）")
    parser.add_argument("--energy", default="100.00", help="恒定累计电表（等值合法）")
    args = parser.parse_args()

    start = datetime.now(timezone.utc).replace(microsecond=0)
    deadline = time.monotonic() + args.duration
    sent = acked = rejected = reconnects = 0
    latencies: list[float] = []
    error_codes: dict[str, int] = {}
    sock: socket.socket | None = None

    def connect() -> socket.socket:  # 建连；失败短重试，计入重连数。
        nonlocal reconnects
        while True:
            try:
                return socket.create_connection((args.host, args.port), timeout=5)
            except OSError:
                reconnects += 1
                if time.monotonic() > deadline:
                    raise
                time.sleep(0.5)

    sock = connect()
    frame_interval = 1.0 / args.rate
    sequence = 0
    try:
        while time.monotonic() < deadline:
            recorded = start + timedelta(seconds=sequence * frame_interval)  # 设备时间单调。
            sequence += 1
            request = {
                "version": "1.0",
                "requestId": f"LOAD-{args.pile}-{sequence}",
                "action": "PILE_TELEMETRY",
                "payload": {
                    "pileId": args.pile,
                    "recordedAt": recorded.strftime("%Y-%m-%dT%H:%M:%SZ"),
                    "temperatureC": 25,
                    "voltageV": 230.0,
                    "currentA": 0.0,
                    "powerW": 0,
                    "energyKwh": args.energy,
                    "dataMode": "SIMULATED",
                },
            }
            began = time.perf_counter()
            try:
                response = exchange(sock, request)
            except (OSError, ConnectionError, json.JSONDecodeError) as error:
                print(f"CONNECTION_ERROR {error}", file=sys.stderr)  # 断连记stderr，不中断整体。
                reconnects += 1
                try:
                    sock.close()
                except OSError:
                    pass
                time.sleep(0.5)
                sock = connect()
                continue
            elapsed_ms = (time.perf_counter() - began) * 1000.0
            sent += 1
            latencies.append(elapsed_ms)
            if response.get("ok") is True:
                acked += 1
            else:
                rejected += 1
                code = str(response.get("code") or response.get("message") or "UNKNOWN")
                error_codes[code] = error_codes.get(code, 0) + 1
            # 按帧率节流，扣除本帧耗时。
            remain = frame_interval - (time.perf_counter() - began)
            if remain > 0:
                time.sleep(remain)
    finally:
        try:
            sock.close()  # type: ignore[union-attr]
        except OSError:
            pass

    if latencies:
        ordered = sorted(latencies)
        p50 = statistics.median(ordered)
        p95 = ordered[min(len(ordered) - 1, int(len(ordered) * 0.95))]
        lat_max = ordered[-1]
    else:
        p50 = p95 = lat_max = 0.0
    print(f"PILE={args.pile} SENT={sent} ACK={acked} REJ={rejected} "
          f"RECONN={reconnects} LAT_P50_MS={p50:.1f} LAT_P95_MS={p95:.1f} "
          f"LAT_MAX_MS={lat_max:.1f} ERRORS={json.dumps(error_codes, sort_keys=True)}")
    return 0 if rejected == 0 and acked == sent else 1


if __name__ == "__main__":
    sys.exit(main())
