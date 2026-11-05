#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""第一阶段三端联动集成验收驱动（S1-E，成员1）。

对独立验收库（runtime/acceptance.sqlite）执行审查报告的三条全流程：
  A 运营改桩故障 → 用户站点/WEB 分布同步变化 → 恢复；
  B 车主充值→预约→开始→遥测→停止→支付（重复支付只扣一次）→ 运营营收/WEB 指标变化；
  C 过温告警→确认→工单流转→关闭（离线告警→工单）三端同步。
所有写操作经 C++ 服务端；WEB/分析侧只做只读 GET。逐步记录
operationId/changeVersion/serverTimeUtc、数据库前后状态与 /dashboard 快照版本。
输出：runtime/acceptance-evidence/{steps.json, report.txt}
"""
from __future__ import annotations

import json
import os
import socket
import sqlite3
import struct
import time
import urllib.request
from datetime import datetime, timedelta, timezone
from pathlib import Path

SERVER = ("127.0.0.1", int(os.environ.get("ACC_SERVER_PORT", "48090")))
API = os.environ.get("ACC_DASHBOARD_API", "http://127.0.0.1:8000/dashboard")
DB_PATH = Path(os.environ.get(
    "ACC_DB_PATH", "/home/bit/projects/stage1_linkage/runtime/acceptance.sqlite"))
OUT_DIR = Path(os.environ.get(
    "ACC_OUT_DIR", "/home/bit/projects/stage1_linkage/runtime/acceptance-evidence"))


def request(request_id: str, action: str, payload: dict) -> dict:
    """4 字节大端长度头 + JSON 正文；与服务端 FrameCodec 对齐。"""
    sock = socket.create_connection(SERVER, timeout=5)
    try:
        body = json.dumps({"version": "1.0", "requestId": request_id,
                           "action": action, "payload": payload}).encode("utf-8")
        sock.sendall(struct.pack(">I", len(body)) + body)
        header = sock.recv(4)
        while len(header) < 4:
            header += sock.recv(4 - len(header))
        (length,) = struct.unpack(">I", header)
        data = b""
        while len(data) < length:
            data += sock.recv(length - len(data))
        return json.loads(data.decode("utf-8"))
    finally:
        sock.close()


def web_snapshot() -> dict:
    """只读 /dashboard：代表 WEB 大屏一个轮询周期的取数。"""
    with urllib.request.urlopen(API, timeout=5) as response:
        return json.loads(response.read().decode("utf-8"))


def db_query(sql: str, params=()) -> list:
    """只读连接查验收库，模拟“数据库前后状态”取证。"""
    connection = sqlite3.connect(f"file:{DB_PATH}?mode=ro", uri=True)
    connection.row_factory = sqlite3.Row  # 行工厂：支持 dict(row) 取列名。
    try:
        return [dict(row) for row in connection.execute(sql, params).fetchall()]
    finally:
        connection.close()


class StepLog:
    def __init__(self) -> None:
        self.steps: list[dict] = []

    def add(self, step: str, action: str, detail: dict) -> None:
        entry = {"step": step, "action": action,
                 "timeUtc": datetime.now(timezone.utc).isoformat(timespec="seconds"),
                 **detail}
        self.steps.append(entry)
        print(f"[{step}] {action}: {json.dumps(detail, ensure_ascii=False, default=str)[:400]}\n")

    def save(self) -> None:
        OUT_DIR.mkdir(parents=True, exist_ok=True)
        (OUT_DIR / "steps.json").write_text(
            json.dumps(self.steps, ensure_ascii=False, indent=2, default=str), encoding="utf-8")
        lines = []
        for entry in self.steps:
            lines.append(f"[{entry['step']}] {entry['action']} @{entry['timeUtc']}")
            for key, value in entry.items():
                if key in ("step", "action", "timeUtc"):
                    continue
                lines.append(f"    {key}: {json.dumps(value, ensure_ascii=False, default=str)}")
        (OUT_DIR / "report.txt").write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    log = StepLog()
    now_utc = datetime.now(timezone.utc)  # 全程统一的“当前时间”基准（各步骤可更新）。
    failures: list[str] = []

    def check(condition: bool, name: str) -> None:
        if not condition:
            failures.append(name)
            print(f"  !! CHECK FAILED: {name}")

    seq = [0]

    def rid() -> str:
        seq[0] += 1
        return f"ACC-{seq[0]:04d}"

    # —— 公共身份 ——
    admin_login = request(rid(), "ADMIN_LOGIN",
                          {"username": "admin", "password": "123456"})
    check(admin_login["ok"], "A0 管理员登录")
    admin_token = admin_login["data"]["adminToken"]
    user_login = request(rid(), "USER_LOGIN", {"phone": "13800009999"})
    check(user_login["ok"], "A0 车主登录（自动注册）")
    user_token = user_login["data"]["userToken"]
    log.add("A0", "双端登录", {
        "adminSnapshotVersion": admin_login["data"].get("snapshotVersion"),
        "userSnapshotVersion": user_login["data"].get("snapshotVersion"),
    })

    # ===== 流程 A：运营改桩 FAULT → 用户站点/WEB 分布变化 → 恢复 =====
    web0 = web_snapshot()
    detail0 = request(rid(), "STATION_DETAIL", {"stationId": 1})
    pile101_before = next(p for p in detail0["data"]["piles"] if p["pileId"] == 101)
    log.add("A1", "基线：用户站点详情 + WEB 桩分布", {
        "pile101Status": pile101_before["status"],
        "webPileStatus": web0["pileStatus"],
        "webSnapshotVersion": web0["snapshotVersion"],
    })
    check(pile101_before["status"] == "IDLE", "A1 桩101初始IDLE")

    db_before = db_query("SELECT status FROM pile_info WHERE id=101")
    fault = request(rid(), "ADMIN_PILE_SET_STATUS",
                    {"adminToken": admin_token, "pileId": 101,
                     "status": "FAULT", "reason": "集成验收：改桩故障"})
    check(fault["ok"], "A2 改桩FAULT成功")
    log.add("A2", "运营改桩FAULT（写操作）", {
        "operationId": fault["data"].get("operationId"),
        "changeVersion": fault["data"].get("changeVersion"),
        "serverTimeUtc": fault["data"].get("serverTimeUtc"),
        "dbBefore": db_before,
        "dbAfter": db_query("SELECT status FROM pile_info WHERE id=101"),
    })
    check(db_query("SELECT status FROM pile_info WHERE id=101")[0]["status"] == "FAULT",
          "A2 数据库状态=FAULT")

    detail_fault = request(rid(), "STATION_DETAIL", {"stationId": 1})
    pile101_fault = next(p for p in detail_fault["data"]["piles"] if p["pileId"] == 101)
    web_fault = web_snapshot()
    log.add("A3", "车主端 + WEB 复查", {
        "userPile101Status": pile101_fault["status"],
        "userSnapshotVersion": detail_fault["data"].get("snapshotVersion"),
        "webPileStatus": web_fault["pileStatus"],
        "webSnapshotVersion": web_fault["snapshotVersion"],
    })
    check(pile101_fault["status"] == "FAULT", "A3 用户侧看到FAULT")
    check(web_fault["snapshotVersion"] >= detail_fault["data"]["snapshotVersion"],
          "A3 WEB快照版本不低于用户查询")

    restore = request(rid(), "ADMIN_PILE_SET_STATUS",
                      {"adminToken": admin_token, "pileId": 101,
                       "status": "IDLE", "reason": "集成验收：恢复"})
    check(restore["ok"], "A4 恢复IDLE")
    detail_restore = request(rid(), "STATION_DETAIL", {"stationId": 1})
    pile101_restore = next(p for p in detail_restore["data"]["piles"]
                           if p["pileId"] == 101)
    log.add("A4", "恢复IDLE并复查", {
        "changeVersion": restore["data"].get("changeVersion"),
        "userPile101Status": pile101_restore["status"],
    })
    check(pile101_restore["status"] == "IDLE", "A4 恢复后用户侧IDLE")

    # ===== 流程 B：车主充电支付闭环 → 运营营收/WEB 指标 =====
    recharge = request(rid(), "WALLET_RECHARGE",
                       {"userToken": user_token, "amountYuan": "100.00"})
    check(recharge["ok"], "B1 充值100.00")
    balance0 = db_query("SELECT balance_yuan FROM user_info WHERE phone='13800009999'")[0]
    overview0 = request(rid(), "ADMIN_OVERVIEW", {"adminToken": admin_token})
    log.add("B1", "充值并记录基线", {
        "operationId": recharge["data"].get("operationId"),
        "changeVersion": recharge["data"].get("changeVersion"),
        "balanceAfter": balance0,
        "todayRevenueBefore": overview0["data"]["todayRevenueYuan"],
    })

    reserve = request(rid(), "ORDER_RESERVE", {"userToken": user_token, "pileId": 102})
    check(reserve["ok"], "B2 预约桩102")
    order_id = reserve["data"].get("orderId")
    # M1-03 规则：开始前必须有新鲜读数（心跳），先补一帧 IDLE 心跳遥测。
    fresh = request(rid(), "PILE_TELEMETRY",
                    {"pileId": 102,
                     "recordedAt": (now_utc - timedelta(seconds=5))
                     .strftime("%Y-%m-%dT%H:%M:%SZ"),
                     "temperatureC": 25, "voltageV": 230.0, "currentA": 0.0,
                     "powerW": 0, "energyKwh": "100.00", "dataMode": "SIMULATED"})
    check(fresh["ok"], "B2b 开始前新鲜心跳")
    start = request(rid(), "ORDER_START", {"userToken": user_token, "orderId": order_id})
    check(start["ok"], "B3 开始充电")
    now_utc = datetime.now(timezone.utc)
    telemetry = request(rid(), "PILE_TELEMETRY",
                        {"pileId": 102, "recordedAt": now_utc.strftime("%Y-%m-%dT%H:%M:%SZ"),
                         "temperatureC": 25, "voltageV": 230.0, "currentA": 30.0,
                         "powerW": 6900, "energyKwh": "100.50", "dataMode": "SIMULATED"})
    check(telemetry["ok"] and telemetry["data"]["orderId"] == order_id,
          "B4 遥测挂接订单（计量联动）")
    stop = request(rid(), "ORDER_STOP", {"userToken": user_token, "orderId": order_id})
    check(stop["ok"], "B5 停止充电进入待支付")
    log.add("B4", "遥测→停止", {
        "orderId": order_id,
        "telemetryOperationId": telemetry["data"].get("operationId"),
        "orderEnergyKwh": telemetry["data"].get("orderEnergyKwh"),
        "stopAmountYuan": stop["data"].get("amountYuan"),
    })

    pay_request_id = rid() + "-PAY"
    pay1 = request(pay_request_id, "WALLET_PAY",
                   {"userToken": user_token, "orderId": order_id})
    pay2 = request(pay_request_id, "WALLET_PAY",
                   {"userToken": user_token, "orderId": order_id})
    balance1 = db_query("SELECT balance_yuan FROM user_info WHERE phone='13800009999'")[0]
    # 只统计该订单的支付流水（充值是另一条 INITIAL_CREDIT/RECHARGE 流水，不应计入）。
    pay_rows = db_query(
        "SELECT COUNT(*) AS n, transaction_type FROM wallet_transaction "
        "WHERE order_id=? GROUP BY transaction_type", (order_id,))
    order_state = db_query("SELECT status,amount_yuan FROM charge_order WHERE id=?", (order_id,))
    log.add("B6", "支付 + 重复支付（同一 requestId）", {
        "pay1Ok": pay1["ok"], "pay1OperationId": pay1["data"].get("operationId"),
        "pay2Ok": pay2["ok"], "pay2Code": pay2.get("code"),
        "balanceBefore": balance0, "balanceAfter": balance1,
        "walletPayRows": pay_rows,
        "orderState": order_state,
    })
    check(pay1["ok"], "B6 首次支付成功")
    check(order_state[0]["status"] == "FINISHED", "B6 订单FINISHED")
    payment_rows = [r for r in pay_rows if r["transaction_type"] == "PAYMENT"]
    check(len(payment_rows) == 1 and payment_rows[0]["n"] == 1,
          "B6 支付流水恰好1条（重复支付只扣一次）")

    summary = request(rid(), "ADMIN_REVENUE_SUMMARY",
                      {"adminToken": admin_token,
                       "fromDate": now_utc.date().isoformat(),
                       "toDate": now_utc.date().isoformat()})
    web_after = web_snapshot()
    log.add("B7", "运营营收汇总 + WEB 指标复查", {
        "summaryTotalRevenue": summary["data"]["totalRevenueYuan"],
        "summaryOrderCount": summary["data"]["orderCount"],
        "summaryAvgPrice": summary["data"]["avgPriceYuan"],
        "webRevenueTrendTail": web_after["revenueTrend"][-1] if web_after["revenueTrend"] else None,
        "webSnapshotVersion": web_after["snapshotVersion"],
    })
    check(float(summary["data"]["totalRevenueYuan"]) > 0, "B7 运营营收汇总包含本单")

    # ===== 流程 C：过温告警 → 确认 → 工单流转 → 关闭；WEB 同步 =====
    overheat = request(rid(), "PILE_TELEMETRY",
                       {"pileId": 103,
                        "recordedAt": (now_utc + timedelta(seconds=5))
                        .strftime("%Y-%m-%dT%H:%M:%SZ"),
                        "temperatureC": 71, "voltageV": 400.0, "currentA": 0.0,
                        "powerW": 0, "energyKwh": "200.05", "dataMode": "SIMULATED"})
    check(overheat["ok"], "C1 过温遥测被受理（触发告警）")
    alarm = db_query("SELECT id,status,severity,alarm_type FROM alarm_event "
                     "WHERE pile_id=103 ORDER BY id DESC LIMIT 1")[0]
    ack = request(rid(), "ADMIN_ALARM_ACK",
                  {"adminToken": admin_token, "alarmId": alarm["id"]})
    check(ack["ok"], "C2 告警确认")
    ticket = db_query("SELECT id,status FROM maintenance_ticket WHERE alarm_id=?",
                      (alarm["id"],))
    ticket_id = ticket[0]["id"] if ticket else 0
    ticket_start = request(rid(), "ADMIN_TICKET_START",
                           {"adminToken": admin_token, "ticketId": ticket_id,
                            "assignee": "operator"})
    ticket_resolve = request(rid(), "ADMIN_TICKET_RESOLVE",
                             {"adminToken": admin_token, "ticketId": ticket_id,
                              "notes": "高温处理完成"})
    # 设计守卫：工单关闭前设备必须修复（否则 DEVICE_NOT_RECOVERED）。
    repair = request(rid(), "ADMIN_PILE_SET_STATUS",
                     {"adminToken": admin_token, "pileId": 103,
                      "status": "IDLE", "reason": "高温故障已修复"})
    check(repair["ok"], "C3b 修复桩103")
    ticket_close = request(rid(), "ADMIN_TICKET_CLOSE",
                           {"adminToken": admin_token, "ticketId": ticket_id})
    ticket_after = db_query("SELECT status FROM maintenance_ticket WHERE id=?", (ticket_id,))
    web_alarm = web_snapshot()
    log.add("C3", "告警→工单闭环", {
        "alarmId": alarm["id"],
        "ticketId": ticket_id,
        "ticketStatusAfter": ticket_after,
        "changeVersions": {
            "ack": ack["data"].get("changeVersion"),
            "start": ticket_start["data"].get("changeVersion"),
            "resolve": ticket_resolve["data"].get("changeVersion"),
            "close": ticket_close["data"].get("changeVersion"),
        },
        "webAlarmTop": web_alarm["alarms"][0] if web_alarm["alarms"] else None,
    })
    check(ticket_after[0]["status"] == "CLOSED", "C3 工单关闭")
    check(web_alarm["alarms"], "C3 WEB 告警列表同步")

    log.save()
    print("=" * 60)
    if failures:
        print(f"ACCEPTANCE=FAIL 失败项: {failures}")
        return 1
    print("ACCEPTANCE=OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
