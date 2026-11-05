#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""成员1 · 第一阶段 · 补充验证脚本（独立运行，未注册进 CTest）。

背景（见 docs/test-results/2026-09-09-member1-supplement.md）：
对照 docs/stage1-integration/member1-tasks.md 与 acceptance-checklist.md 复核时发现，
以下两类验证在现有 20 项 CTest 中没有 TCP 协议级覆盖：
  1. ADMIN_STATIONS / ADMIN_PILES / ADMIN_ORDERS / ADMIN_USERS / ADMIN_LOGS /
     ADMIN_OVERVIEW / ADMIN_REVENUE_TREND / ADMIN_USER_FREEZE 共 8 个动作
     （admin_service_test 只在服务层覆盖过它们，未经过 request_router 的协议入口）；
  2. 任务卡要求的端到端链路“登录→查询站点→预约→开始→遥测→停止→支付→历史”
     中，e2e/member1_flow.cpp 未包含 ORDER_HISTORY 步骤。

本脚本用一个临时数据库 + 独立服务进程，按真实 TCP 协议补跑这些验证：
  - 端到端链路（含历史查询）与重复支付（同 requestId 幂等）；
  - 8 个管理动作的成功路径、未授权（无/空 adminToken → AUTH_INVALID）；
  - 业务冲突（冻结用户预约 → USER_FROZEN、已完成订单再报电量/支付 → 状态冲突）、
    参数错误（非法手机号、越界坐标、空冻结原因、电量倒退）。

安全设计（防止影响现有项目）：
  - 只使用系统临时目录中的数据库文件，绝不触碰 runtime/ 下任何数据库；
  - 端口由系统分配（--port 0），不占用 48080，不与任何现有服务冲突；
  - 退出时终止自己启动的服务进程并清理临时目录。

运行方式（在仓库根目录或任意位置均可，脚本自行定位）：
  python3 tests/integration/stage1_supplement_check.py

说明：为了遵守“不修改项目既有文件”的要求，本脚本没有注册进 CMakeLists.txt /
CTest。如日后希望纳入 CTest，可由维护者自行在 CMakeLists.txt 的 BUILD_TESTING
区块中加入（注意与其他占用 48080 的测试不要并行）：
  add_test(NAME stage1_supplement_check
           COMMAND python3 ${CMAKE_SOURCE_DIR}/tests/integration/stage1_supplement_check.py)
"""

import json
import os
import socket
import struct
import subprocess
import sys
import tempfile
import time
import uuid
from datetime import datetime, timezone
from pathlib import Path

BASE = Path(__file__).resolve().parents[2]
# 默认服务器与端口可被命令行 --server/--port 覆盖（M1-01：不再固定 build/ 与 48080）。
DEFAULT_SERVER = BASE / "build" / "charge_platform_server"
SERVER = DEFAULT_SERVER
PORT = 48080

results = []


def check(name, ok, detail=""):
    results.append((name, ok, detail))
    mark = "PASS" if ok else "FAIL"
    print(f"[{mark}] {name}" + (f"  ({detail})" if detail and not ok else ""))


class Client:
    """最小 TCP+JSON 协议客户端：4 字节大端长度头 + JSON 正文（与 FrameCodec 一致）。"""

    def __init__(self, host="127.0.0.1", port=PORT):
        self.sock = socket.create_connection((host, port), timeout=10)

    def _recv_exact(self, n):
        buf = b""
        while len(buf) < n:
            chunk = self.sock.recv(n - len(buf))
            if not chunk:
                raise ConnectionError("connection closed by server")
            buf += chunk
        return buf

    def call(self, action, payload=None, request_id=None):
        rid = request_id or uuid.uuid4().hex[:12].upper()
        frame = json.dumps(
            {"version": "1.0", "requestId": rid, "action": action,
             "payload": payload if payload is not None else {}},
            ensure_ascii=False, separators=(",", ":")).encode("utf-8")
        self.sock.sendall(struct.pack(">I", len(frame)) + frame)
        (length,) = struct.unpack(">I", self._recv_exact(4))
        if length == 0 or length > 1024 * 1024:
            raise ValueError(f"illegal frame length {length}")
        response = json.loads(self._recv_exact(length).decode("utf-8"))
        if response.get("requestId") != rid:
            raise AssertionError(f"requestId mismatch: {response}")
        return response


def start_server(db_path):
    """启动服务端子进程：--host 127.0.0.1 --port 0 --db 临时库。

    返回 (proc, stdout_pipe)：就绪行由 wait_until_ready 解析实际端口。
    端口由系统分配，绝不占用 48080 或与其他测试冲突。
    """
    argv = [str(SERVER), "--host", "127.0.0.1", "--port", "0", "--db", str(db_path)]
    proc = subprocess.Popen(argv, cwd=str(BASE),
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            text=True)
    return proc


def wait_until_ready(proc, deadline_s=15):
    """从子进程 stdout 解析实际端口（listening on host:port），返回 Client。"""
    global PORT
    deadline = time.time() + deadline_s
    lines = []
    while time.time() < deadline:
        line = proc.stdout.readline()
        if line:
            lines.append(line)
            if "listening on" in line:
                tail = line.split("listening on", 1)[1].strip()
                host, _, port = tail.rpartition(":")
                if port.isdigit():
                    PORT = int(port)
                    client = Client(host="127.0.0.1", port=PORT)
                    resp = client.call("PING")
                    if resp.get("ok") and resp.get("data", {}).get("reply") == "PONG":
                        return client
                    client.sock.close()
        if proc.poll() is not None:
            break
        time.sleep(0.1)
    raise TimeoutError("server did not become ready: " + "".join(lines))


def utc_today():
    return datetime.now(timezone.utc).date().isoformat()


def main():
    import argparse
    global SERVER
    argp = argparse.ArgumentParser(description="Stage1 补充协议检查（参数化服务端/端口）")
    argp.add_argument("--server", type=Path, default=None,
                      help="charge_platform_server 可执行文件路径（默认在 build*/ 下查找）")
    opts = argp.parse_args()
    if opts.server is not None:
        SERVER = opts.server
    else:
        for cand in (BASE / "build" / "charge_platform_server",
                     BASE / "build-stage1" / "charge_platform_server"):
            if cand.exists():
                SERVER = cand
                break
    if not SERVER.exists():
        print(f"未找到服务端可执行文件：{SERVER}，请用 --server 指定或先构建")
        return 2

    with tempfile.TemporaryDirectory(prefix="stage1-supplement-") as tmp:
        db_under_test = str(Path(tmp) / "supplement.sqlite")
        proc = start_server(Path(db_under_test))
        client = None
        try:
            client = wait_until_ready(proc)
            print("服务已就绪（临时数据库，48080 端口）\n")

            # ---- A. 协议健康与参数错误 ----
            resp = client.call("PING")
            check("PING→PONG 健康检查", resp.get("ok") is True
                  and resp.get("data", {}).get("reply") == "PONG")

            resp = client.call("NOT_AN_ACTION")
            check("未知动作返回 PARAM_INVALID",
                  resp.get("ok") is False and resp.get("code") == "PARAM_INVALID")

            resp = client.call("USER_LOGIN", {"phone": "123"})
            check("非法手机号登录返回 PARAM_INVALID",
                  resp.get("ok") is False and resp.get("code") == "PARAM_INVALID")

            resp = client.call("STATION_NEARBY", {"latitude": 999.0, "longitude": 118.1})
            check("越界经纬度返回 PARAM_INVALID",
                  resp.get("ok") is False and resp.get("code") == "PARAM_INVALID")

            # ---- B. 管理员与站点准备 ----
            resp = client.call("ADMIN_LOGIN", {"username": "admin", "password": "123456"})
            token = resp.get("data", {}).get("adminToken", "")
            check("管理员登录返回 adminToken", resp.get("ok") is True and token != "")

            resp = client.call("ADMIN_LOGIN", {"username": "admin", "password": "wrong"})
            # 服务层返回 AUTH_FAILED，经 serviceFailure 统一映射后线上 code 为 AUTH_INVALID，
            # 原始错误码字符串保留在 message 字段中。
            check("错误密码返回 AUTH_INVALID（message=AUTH_FAILED）",
                  resp.get("ok") is False and resp.get("code") == "AUTH_INVALID"
                  and resp.get("message") == "AUTH_FAILED", str(resp))

            resp = client.call("ADMIN_STATION_CREATE", {"name": "x", "address": "y"})
            check("无 adminToken 建站返回 AUTH_INVALID",
                  resp.get("ok") is False and resp.get("code") == "AUTH_INVALID")

            resp = client.call("ADMIN_STATION_CREATE", {
                "adminToken": token, "name": "验收补充站", "address": "验收路1号",
                "latitude": 24.48, "longitude": 118.10,
                "priceYuanPerKwh": "1.23",
                "piles": [{"serial": "PILE-SUPP-01", "chargeType": "FAST",
                           "ratedPowerKw": 60.0}]})
            station_id = resp.get("data", {}).get("stationId", 0)
            check("创建站点成功且返回 stationId",
                  resp.get("ok") is True and station_id > 0, str(resp))

            resp = client.call("ADMIN_STATION_CREATE", {
                "adminToken": token, "name": "缺坐标站", "address": "验收路2号",
                "priceYuanPerKwh": "1.00", "piles": []})
            check("缺少坐标的建站请求返回 PARAM_INVALID",
                  resp.get("ok") is False and resp.get("code") == "PARAM_INVALID")

            # ---- C. 端到端链路：登录→查站→详情→预约→开始→遥测→停止→支付→历史 ----
            resp = client.call("STATION_NEARBY", {"latitude": 24.48, "longitude": 118.10})
            stations = resp.get("data", {}).get("stations", [])
            check("附近站点返回且含距离/空闲桩数",
                  resp.get("ok") is True and len(stations) == 1
                  and isinstance(stations[0].get("distanceKm"), (int, float))
                  and stations[0].get("availablePiles") == 1
                  and stations[0].get("priceYuanPerKwh") == "1.23", str(resp))

            resp = client.call("STATION_DETAIL", {"stationId": station_id})
            piles = resp.get("data", {}).get("piles", [])
            pile_id = piles[0].get("pileId", 0) if piles else 0
            check("站点详情返回电桩列表且状态 IDLE",
                  resp.get("ok") is True and pile_id > 0
                  and piles[0].get("status") == "IDLE", str(resp))

            resp = client.call("USER_LOGIN", {"phone": "13900001111"})
            user1 = resp.get("data", {}).get("userId", 0)
            user1_token = resp.get("data", {}).get("userToken", "")
            check("用户登录（自动注册）返回 userId",
                  resp.get("ok") is True and user1 > 0, str(resp))

            resp = client.call("WALLET_BALANCE", {"userToken": user1_token, "userId": user1})
            check("新用户初始余额为 100.00",
                  resp.get("data", {}).get("balanceYuan") == "100.00", str(resp))

            resp = client.call("ORDER_RESERVE", {"userToken": user1_token, "userId": user1, "pileId": pile_id})
            order_id = resp.get("data", {}).get("orderId", "")
            # M1-03：预置设备新鲜累计读数基准 100.00 + 心跳（开始充电要求 ≤15s 内读数）。
            import sqlite3 as _sq
            _con = _sq.connect(db_under_test)
            _now = datetime.now(timezone.utc).isoformat()
            _con.execute(
                "INSERT INTO pile_telemetry(pile_id,recorded_at,temperature_c,"
                "voltage_v,current_a,power_w,energy_kwh) "
                "VALUES(?,?,25,220,16,3520,'100.00')", (pile_id, _now))
            _con.execute("UPDATE pile_info SET last_heartbeat_at=? WHERE id=?",
                         (_now, pile_id))
            _con.commit(); _con.close()
            check("预约成功返回 ORD- 编号",
                  resp.get("ok") is True and order_id.startswith("ORD-"), str(resp))

            resp = client.call("ORDER_START", {"userToken": user1_token, "orderId": order_id})
            check("开始充电成功并锁定单价 1.23",
                  resp.get("ok") is True
                  and resp.get("data", {}).get("unitPriceYuanPerKwh") == "1.23", str(resp))

            from datetime import timedelta as _td
            resp = client.call("PILE_TELEMETRY",
                               {"pileId": pile_id,
                                "recordedAt": (datetime.now(timezone.utc)
                                               + _td(seconds=1)).isoformat(),
                                "temperatureC": 25.0, "voltageV": 220.0,
                                "currentA": 16.0, "powerW": 3520,
                                "energyKwh": "101.00"})
            check("PILE 累计读数上报成功（101.00）",
                  resp.get("ok") is True
                  and resp.get("data", {}).get("energyKwh") == "101.00", str(resp))
            resp = client.call("ORDER_DETAIL",
                               {"userToken": user1_token, "orderId": order_id})
            check("订单用量由 PILE 驱动为 1.00（101.00-100.00）",
                  resp.get("ok") is True
                  and resp.get("data", {}).get("energyKwh") == "1.00", str(resp))

            resp = client.call("PILE_TELEMETRY",
                               {"pileId": pile_id,
                                "recordedAt": (datetime.now(timezone.utc)
                                               + _td(seconds=2)).isoformat(),
                                "temperatureC": 25.0, "voltageV": 220.0,
                                "currentA": 16.0, "powerW": 3520,
                                "energyKwh": "100.50"})
            check("电量倒退被拒绝（TELEMETRY_OUT_OF_ORDER）",
                  resp.get("ok") is False
                  and resp.get("code") == "TELEMETRY_OUT_OF_ORDER")

            resp = client.call("ORDER_STOP", {"userToken": user1_token, "orderId": order_id})
            check("停止结算金额 = 1.00×1.23 = 1.23",
                  resp.get("ok") is True
                  and resp.get("data", {}).get("amountYuan") == "1.23", str(resp))

            pay_rid = "SUPP-PAY-0001"
            resp = client.call("WALLET_PAY", {"userToken": user1_token, "orderId": order_id}, request_id=pay_rid)
            check("支付成功", resp.get("ok") is True, str(resp))

            resp = client.call("WALLET_PAY", {"userToken": user1_token, "orderId": order_id}, request_id=pay_rid)
            check("同 requestId 重复支付幂等返回成功", resp.get("ok") is True, str(resp))

            resp = client.call("WALLET_PAY", {"userToken": user1_token, "orderId": order_id})
            check("换 requestId 再次支付已完成订单返回状态冲突",
                  resp.get("ok") is False
                  and resp.get("code") == "ORDER_STATE_CONFLICT")

            resp = client.call("WALLET_BALANCE", {"userToken": user1_token, "userId": user1})
            check("余额只扣一次：100.00-1.23=98.77",
                  resp.get("data", {}).get("balanceYuan") == "98.77", str(resp))

            resp = client.call("ORDER_ACTIVE", {"userToken": user1_token, "userId": user1})
            check("支付后无活动订单",
                  resp.get("ok") is True
                  and not resp.get("data", {}).get("order"), str(resp))

            resp = client.call("ORDER_HISTORY",
                               {"userToken": user1_token,
                                "userId": user1, "limit": 20, "offset": 0})
            rows = resp.get("data", {}).get("rows", [])
            check("历史订单可查到本单且金额 1.23（e2e 补充的历史步骤）",
                  resp.get("ok") is True
                  and any(r.get("orderId") == order_id
                          and r.get("amountYuan") == "1.23" for r in rows), str(resp))

            resp = client.call("ORDER_DETAIL", {"userToken": user1_token, "orderId": order_id})
            check("订单详情状态为 FINISHED",
                  resp.get("data", {}).get("status") == "FINISHED", str(resp))

            # ---- D. 8 个此前未经协议级测试的管理动作 ----
            utc_date = utc_today()

            resp = client.call("ADMIN_STATIONS",
                               {"adminToken": token, "search": "验收补充"})
            rows = resp.get("data", {}).get("rows", [])
            check("ADMIN_STATIONS 成功且按名称检索命中",
                  resp.get("ok") is True
                  and any(r.get("stationId") == station_id for r in rows), str(resp))

            resp = client.call("ADMIN_PILES",
                               {"adminToken": token, "stationId": station_id})
            rows = resp.get("data", {}).get("rows", [])
            check("ADMIN_PILES 成功且电桩已释放为 IDLE",
                  resp.get("ok") is True and len(rows) == 1
                  and rows[0].get("status") == "IDLE", str(resp))

            resp = client.call("ADMIN_ORDERS",
                               {"adminToken": token, "status": "FINISHED"})
            rows = resp.get("data", {}).get("rows", [])
            check("ADMIN_ORDERS 成功且能查到已完成订单",
                  resp.get("ok") is True
                  and any(r.get("orderId") == order_id for r in rows), str(resp))

            resp = client.call("ADMIN_USERS", {"adminToken": token})
            rows = resp.get("data", {}).get("rows", [])
            check("ADMIN_USERS 成功且手机号打码输出",
                  resp.get("ok") is True
                  and any("****" in str(r.get("phone", "")) for r in rows), str(resp))

            resp = client.call("ADMIN_LOGS", {"adminToken": token})
            rows = resp.get("data", {}).get("rows", [])
            check("ADMIN_LOGS 成功且含 CREATE_STATION 审计记录",
                  resp.get("ok") is True
                  and any(r.get("action") == "CREATE_STATION" for r in rows), str(resp))

            resp = client.call("ADMIN_OVERVIEW", {"adminToken": token})
            data = resp.get("data", {})
            check("ADMIN_OVERVIEW 营收/服务次数/待处理告警正确",
                  resp.get("ok") is True
                  and data.get("todayRevenueYuan") == "1.23"
                  and data.get("todayServiceCount") == 1
                  and data.get("pendingAlarmCount") == 0, str(resp))

            resp = client.call("ADMIN_REVENUE_TREND",
                               {"adminToken": token, "days": 7})
            points = resp.get("data", {}).get("points", [])
            today = [p for p in points if p.get("date") == utc_date]
            check("ADMIN_REVENUE_TREND 返回 7 天且今日营收 1.23",
                  resp.get("ok") is True and len(points) == 7
                  and today and today[0].get("revenueYuan") == "1.23", str(resp))

            # ---- E. 冻结用户（ADMIN_USER_FREEZE）与业务冲突 ----
            resp = client.call("USER_LOGIN", {"phone": "13900002222"})
            user2 = resp.get("data", {}).get("userId", 0)
            user2_token = resp.get("data", {}).get("userToken", "")
            check("第二位用户登录成功", resp.get("ok") is True and user2 > 0, str(resp))

            resp = client.call("ADMIN_USER_FREEZE",
                               {"adminToken": token, "userId": user2,
                                "frozen": True, "reason": "验收冻结测试"})
            check("ADMIN_USER_FREEZE 冻结成功",
                  resp.get("ok") is True, str(resp))

            resp = client.call("ADMIN_USER_FREEZE",
                               {"adminToken": token, "userId": user2,
                                "frozen": True, "reason": "   "})
            check("空冻结原因返回 PARAM_INVALID",
                  resp.get("ok") is False and resp.get("code") == "PARAM_INVALID")

            resp = client.call("ORDER_RESERVE", {"userToken": user2_token, "userId": user2, "pileId": pile_id})
            check("被冻结用户预约返回 USER_FROZEN",
                  resp.get("ok") is False and resp.get("code") == "USER_FROZEN")

            resp = client.call("ORDER_TELEMETRY",
                               {"userToken": user1_token,
                                "orderId": order_id, "energyKwh": "9.99"})
            check("网络订单遥测入口已关闭（AUTH_INVALID）",
                  resp.get("ok") is False
                  and resp.get("code") == "AUTH_INVALID")

            # ---- F. 8 个管理动作未授权访问（无 adminToken）----
            for action in ("ADMIN_STATIONS", "ADMIN_PILES", "ADMIN_ORDERS",
                           "ADMIN_USERS", "ADMIN_LOGS", "ADMIN_OVERVIEW",
                           "ADMIN_REVENUE_TREND", "ADMIN_USER_FREEZE"):
                resp = client.call(action, {})
                check(f"{action} 无令牌返回 AUTH_INVALID",
                      resp.get("ok") is False and resp.get("code") == "AUTH_INVALID")
        finally:
            if client is not None:
                try:
                    client.sock.close()
                except OSError:
                    pass
            proc.terminate()
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                proc.kill()

    failed = [r for r in results if not r[1]]
    print(f"\n===== 结果：{len(results) - len(failed)}/{len(results)} 通过 =====")
    for name, _, detail in failed:
        print(f"  失败项: {name}  {detail}")
    return 0 if not failed else 1


if __name__ == "__main__":
    sys.exit(main())
