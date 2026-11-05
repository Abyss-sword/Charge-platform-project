# -*- coding: utf-8 -*-
"""Caltech ACN（Adaptive Charging Network）适配器——预留骨架，未启用。

状态：预留（RESERVED）。本模块只有映射设计与接口占位，**未下载任何 ACN 数据、不产生任何证据**。
契约里已预留 `acn` 来源命名空间（见 docs/stage2/contracts.md 与 historical_interval_v1.schema.json
的 source_id 示例 `acn`、analysis_station_id 示例 `acn:caltech-1`）。

若要启用（需要先满足的前置）：
  1. 使用者自行注册 ACN 数据访问并取得 token，或取得已下载的会话导出文件；
  2. 在 analytics/sources/acn/manifest.json 登记许可与再分发条件（ACN 条款限制再分发，
     因此原始 raw 不得进入仓库与共享区，只能保留聚合与统计）；
  3. 在本地受控区先剔除 userID（ACN 会话含 userID，属可重识别标识，PRIVACY_FIELDS 已覆盖 user_id）。

字段映射设计（ACN 会话导出 → historical_interval_v1 的 8 个必填来源列）：
  sessionID        -> source_record_id
  stationID        -> source_station_id（analysis_station_id 构造为 acn:<stationID>）
  connectionTime   -> interval_start_utc（RFC1123 UTC，需转契约的 YYYY-MM-DDTHH:MM:SSZ）
  disconnectTime   -> interval_end_utc
  kWhDelivered     -> energy（energy_unit=kwh）
  （常量）          -> observation_kind=SESSION_ALLOCATED
  timezone 字段     -> source_timezone（如 America/Los_Angeles）
  站点 EVSE 数      -> capacity_piles（ACN 站点信息提供桩数，可支撑空闲桩预测的容量维度；
                       occupied_piles 仍须满足"完整容量 + 停用信息"才可推导，否则为 null）
  丢弃：userID、userInputs、spaceID 等个人/细粒度标识（本地受控区直接不写出）

与 course_sessions.py 的关系：两者输出同一种规范 CSV，后续复用 run_governance 的
准入→适配→质量→候选→HDFS→发布链；ACN 的价值在于提供容量维度（course 数据没有），
可在启用后把 S2-08 空闲桩预测从 BLOCKED 转为可做。
"""
from __future__ import annotations  # 延迟注解，兼容 3.10。

from pathlib import Path  # 接口签名用路径类型。
from typing import Any  # 返回统计字典。

ACN_MAPPING = {  # 映射表（设计，未执行）：左为 ACN 字段，右为契约字段或常量。
    "sessionID": "source_record_id",
    "stationID": "source_station_id",
    "connectionTime": "interval_start_utc",
    "disconnectTime": "interval_end_utc",
    "kWhDelivered": "energy",
    "timezone": "source_timezone",
    "CONST": "observation_kind=SESSION_ALLOCATED",
    "STATION_INFO": "capacity_piles (from station EVSE count)",
}  # 结束：映射表。
DROP_FIELDS = ("userID", "userInputs", "spaceID", "clusterID", "pilotSignal")  # 本地受控区必须剔除的列。


def convert(*_args: Any, **_kwargs: Any) -> dict[str, Any]:  # 预留接口：与 course_sessions.convert 同签名。
    raise NotImplementedError(  # 明确失败，避免被误当成可用实现。
        "acn 适配器为预留骨架，未启用：需先注册取得 ACN 数据并在 manifest 登记许可与再分发条件。"
    )  # 结束：异常。


def _placeholder_signature(input_path: str | Path, output_csv: str | Path) -> None:  # 仅供静态检查的签名占位。
    raise NotImplementedError("reserved")  # 不执行。
