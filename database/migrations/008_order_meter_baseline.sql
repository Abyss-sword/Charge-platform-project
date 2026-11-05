-- 成员1 · 第一阶段收尾
-- 迁移 008：订单表计基准（01文档 4.2 统一计量）。
-- meter_start_kwh：开始充电时记录的设备累计读数基准（订单用量 = 当前累计 - 基准）。
-- meter_last_at：订单最近一次应用（计入订单增量）的读数设备时间。
-- 与锁价（006 的 price_locked/unit_price_yuan_per_kwh）在开始事务同写；
-- 已有未完成、无基准的历史订单不回填（不伪造起点），由人工结算口径处理。

ALTER TABLE charge_order ADD COLUMN meter_start_kwh TEXT
    CHECK(meter_start_kwh IS NULL OR (
        typeof(meter_start_kwh) = 'text'
        AND meter_start_kwh NOT GLOB '*[^0-9.]*'
        AND length(meter_start_kwh) - length(replace(meter_start_kwh, '.', '')) = 1
        AND instr(meter_start_kwh, '.') > 1
        AND length(meter_start_kwh) - instr(meter_start_kwh, '.') = 2
        AND CAST(meter_start_kwh AS REAL) >= 0));

ALTER TABLE charge_order ADD COLUMN meter_last_at TEXT;

-- 2026-09-13（第二阶段 T08 修复）：原 007_meter_baseline_and_telemetry_idempotency.sql 与本文件
-- 重复添加 meter_start_kwh，导致全新库迁移重放失败（member1_flow_test 实测复现）。
-- 该文件已移入 migrations/superseded/ 停用；大屏只读投影（sqlite_dashboard）依赖的
-- pile_telemetry.source_mode 依据已验收库口径并入本迁移，遥测来源核验能力保持不变。
ALTER TABLE pile_telemetry ADD COLUMN source_mode TEXT NOT NULL DEFAULT 'UNKNOWN'
    CHECK(source_mode IN ('SIMULATED', 'REAL', 'UNKNOWN'));

