#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""M1-01 · 服务端独立启动配置测试。

验证 stage1 收尾契约（02-member1-plan M1-01）：
  1. charge_platform_server 支持 --host/--port/--db 命令行参数；
  2. --port 0 由系统分配端口，就绪后输出实际地址/端口，不与 48080 或其他实例冲突；
  3. 非法端口、空路径、监听失败返回非 0；
  4. 两个并行实例使用不同临时库互不影响；
  5. 退出后无遗留监听。

失败即 FAIL——本测试在实现前必须失败（当前 main 固定 48080，不支持参数）。
"""

import argparse
import json
import socket
import struct
import subprocess
import sys
import tempfile
import time
from pathlib import Path

BASE = Path(__file__).resolve().parents[2]
results = []


def check(name, ok, detail=""):
    results.append((name, ok, detail))
    print(f"[{'PASS' if ok else 'FAIL'}] {name}" + (f"  ({detail})" if detail and not ok else ""))


def recv_exact(sock, n):
    buf = b""
    while len(buf) < n:
        chunk = sock.recv(n - len(buf))
        if not chunk:
            raise ConnectionError("closed")
        buf += chunk
    return buf


def call(sock, action, payload=None, rid="LAUNCH-CFG-1"):
    body = json.dumps({"version": "1.0", "requestId": rid, "action": action,
                       "payload": payload if payload is not None else {}},
                      ensure_ascii=False).encode("utf-8")
    sock.sendall(struct.pack(">I", len(body)) + body)
    length = struct.unpack(">I", recv_exact(sock, 4))[0]
    return json.loads(recv_exact(sock, length).decode("utf-8"))


def wait_ping_ok(port, host="127.0.0.1", deadline_s=15):
    deadline = time.time() + deadline_s
    while time.time() < deadline:
        try:
            s = socket.create_connection((host, port), timeout=2)
            resp = call(s, "PING", rid="LAUNCH-READY")
            s.close()
            if resp.get("ok") and resp.get("data", {}).get("reply") == "PONG":
                return True
        except OSError:
            pass
        time.sleep(0.2)
    return False


def parse_listen_line(line):
    """从就绪输出解析 'listening on HOST:PORT' 形式的实际地址。"""
    if "listening on" in line:
        tail = line.split("listening on", 1)[1].strip()
        host, _, port = tail.rpartition(":")
        if port.isdigit():
            return host, int(port)
    return None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--server", type=Path, help="Absolute path to the CMake-built server executable")
    args = parser.parse_args()
    server = args.server
    if server is None:
        server = BASE / "build-stage1" / "charge_platform_server"
        server = server if server.exists() else BASE / "build" / "charge_platform_server"
    if not server.exists():
        print(f"server binary not found: {server}")
        return 2

    with tempfile.TemporaryDirectory(prefix="launch-cfg-") as tmp:
        tmp = Path(tmp)

        # ---- 1. 非法端口/空路径必须非0退出 ----
        proc = subprocess.run([str(server), "--port", "not-a-number"],
                              capture_output=True, timeout=30, cwd=str(BASE))
        check("非法端口参数返回非0", proc.returncode != 0, f"rc={proc.returncode}")

        proc = subprocess.run([str(server), "--db", ""],
                              capture_output=True, timeout=30, cwd=str(BASE))
        check("空数据库路径返回非0", proc.returncode != 0, f"rc={proc.returncode}")

        # ---- 2. --port 0 系统分配 + 就绪输出实际端口 ----
        db1 = tmp / "instance1.sqlite"
        p1 = subprocess.Popen([str(server), "--host", "127.0.0.1", "--port", "0",
                                "--db", str(db1)],
                               stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                               text=True, cwd=str(BASE))
        port1 = None
        try:
            out1 = []
            deadline = time.time() + 15
            while time.time() < deadline and port1 is None:
                line = p1.stdout.readline()
                if not line:
                    break
                out1.append(line)
                parsed = parse_listen_line(line)
                if parsed:
                    port1 = parsed[1]
            if port1 is None:
                check("--port 0 就绪输出实际端口", False, "".join(out1))
            else:
                check("--port 0 实际端口可PING通", wait_ping_ok(port1), f"port={port1}")
                check("实际端口不是48080", port1 != 48080, f"port={port1}")
        finally:
            p1.terminate()
            try:
                p1.wait(timeout=5)
            except subprocess.TimeoutExpired:
                p1.kill()

        # ---- 3. 两个并行实例：不同端口、库互不影响（两实例同时在线验证）----
        db1b = tmp / "instance1b.sqlite"
        db2 = tmp / "instance2.sqlite"
        pa = subprocess.Popen([str(server), "--host", "127.0.0.1", "--port", "0",
                               "--db", str(db1b)],
                              stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                              text=True, cwd=str(BASE))
        p2 = subprocess.Popen([str(server), "--host", "127.0.0.1", "--port", "0",
                               "--db", str(db2)],
                              stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                              text=True, cwd=str(BASE))
        port1 = None
        port2 = None
        try:
            for proc_obj in (pa, p2):
                out = []
                port = None
                deadline = time.time() + 15
                while time.time() < deadline and port is None:
                    line = proc_obj.stdout.readline()
                    if not line:
                        break
                    out.append(line)
                    parsed = parse_listen_line(line)
                    if parsed:
                        port = parsed[1]
                if port1 is None:
                    port1 = port
                    if port is None:
                        check("实例A --port 0 就绪输出实际端口", False, "".join(out))
                else:
                    port2 = port
                    if port is None:
                        check("实例B --port 0 就绪输出实际端口", False, "".join(out))
            if port1 is not None and port2 is not None:
                check("两实例端口不同", port1 != port2, f"{port1} vs {port2}")
                try:
                    s = socket.create_connection(("127.0.0.1", port2), timeout=5)
                    r2 = call(s, "ADMIN_LOGIN", {"username": "admin", "password": "123456"},
                              rid="LAUNCH-ADMIN2")
                    tok2 = r2.get("data", {}).get("adminToken", "")
                    s.close()
                    if not tok2:
                        check("实例B管理员登录（用于隔离验证）", False, "no token")
                    else:
                        s = socket.create_connection(("127.0.0.1", port2), timeout=5)
                        call(s, "ADMIN_STATION_CREATE", {
                            "adminToken": tok2, "name": "隔离验证站", "address": "隔离路1号",
                            "latitude": 24.48, "longitude": 118.10, "priceYuanPerKwh": "1.00",
                            "piles": []}, rid="LAUNCH-CREATE2")
                        s.close()
                        s = socket.create_connection(("127.0.0.1", port1), timeout=5)
                        r1 = call(s, "ADMIN_LOGIN", {"username": "admin", "password": "123456"},
                                  rid="LAUNCH-ADMIN1")
                        tok1 = r1.get("data", {}).get("adminToken", "")
                        s.close()
                        if tok1:
                            s = socket.create_connection(("127.0.0.1", port1), timeout=5)
                            q1 = call(s, "ADMIN_STATIONS",
                                      {"adminToken": tok1, "search": "隔离验证站"},
                                      rid="LAUNCH-Q1")
                            s.close()
                            rows = q1.get("data", {}).get("rows", []) if q1.get("ok") else []
                            check("实例B建站对实例A不可见（库隔离）", not rows, str(rows)[:120])
                        else:
                            check("实例A管理员登录（用于隔离验证）", False, "no token")
                except OSError as exc:
                    check("实例B管理员登录（用于隔离验证）", False, str(exc))
        finally:
            for proc_obj in (pa, p2):
                proc_obj.terminate()
                try:
                    proc_obj.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    proc_obj.kill()

        # ---- 4. 退出无遗留监听 ----
        if port2 is not None:
            time.sleep(0.5)
            try:
                socket.create_connection(("127.0.0.1", port2), timeout=1).close()
                left = True
            except OSError:
                left = False
            check("实例退出后端口关闭", not left)

    failed = [r for r in results if not r[1]]
    print(f"\n===== {len(results) - len(failed)}/{len(results)} passed =====")
    for name, _, detail in failed:
        print(f"  failed: {name}  {detail}")
    return 0 if not failed else 1


if __name__ == "__main__":
    sys.exit(main())
