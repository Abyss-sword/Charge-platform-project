-- 成员1 · 第一阶段
-- 演示种子数据（答辩演示库）：固定站点、电桩、用户、订单、告警、工单；
-- 递归 CTE 生成编号，INSERT OR IGNORE 保证重复导入不产生重复数据。
-- 注意口径：这是可复现的模拟数据，不是随机生成的真实用户。

INSERT OR IGNORE INTO station_info
    (id, name, address, latitude, longitude, price_yuan_per_kwh, status, created_at)
VALUES
    (1, '滨海示范站', '滨海路1号', 24.4798, 118.0894, '1.20', 'OPEN', '2026-08-01T00:00:00Z'),
    (2, '软件园站', '软件园二期2号', 24.4955, 118.1810, '1.35', 'OPEN', '2026-08-01T00:00:00Z'),
    (3, '会展中心站', '会展路3号', 24.4679, 118.1762, '1.10', 'OPEN', '2026-08-01T00:00:00Z');

WITH RECURSIVE seq(n) AS (
    SELECT 1 UNION ALL SELECT n + 1 FROM seq WHERE n < 12
)
INSERT OR IGNORE INTO pile_info
    (id, station_id, pile_sn, charge_type, rated_power_kw, status, last_heartbeat_at, created_at)
SELECT 100 + n,
       ((n - 1) / 4) + 1,
       printf('PILE-DEMO-%02d', n),
       CASE WHEN n % 3 = 0 THEN 'SLOW' ELSE 'FAST' END,
       CASE WHEN n % 3 = 0 THEN 7.0 ELSE 60.0 END,
       'IDLE',
       NULL,
       '2026-08-01T00:00:00Z'
FROM seq;

WITH RECURSIVE seq(n) AS (
    SELECT 1 UNION ALL SELECT n + 1 FROM seq WHERE n < 20
)
INSERT OR IGNORE INTO user_info
    (id, phone, nickname, balance_yuan, status, created_at)
SELECT 1000 + n,
       printf('1380000%04d', n),
       printf('演示用户%02d', n),
       printf('%d.00', 100 + n * 5),
       'NORMAL',
       '2026-08-01T00:00:00Z'
FROM seq;

WITH RECURSIVE seq(n) AS (
    SELECT 1 UNION ALL SELECT n + 1 FROM seq WHERE n < 30
)
INSERT OR IGNORE INTO charge_order
    (id, user_id, pile_id, status, energy_kwh, amount_yuan,
     unit_price_yuan_per_kwh,
     started_at, ended_at, end_reason, created_at)
SELECT printf('ORD-DEMO-%03d', n),
       1001 + ((n - 1) % 20),
       101 + ((n - 1) % 12),
       'FINISHED',
       printf('%d.%02d', (800 + n * 25) / 100, (800 + n * 25) % 100),
       printf('%d.%02d', (1000 + n * 30) / 100, (1000 + n * 30) % 100),
       '1.20',
       strftime('%Y-%m-%dT%H:%M:%SZ', '2026-08-01T08:00:00Z', printf('+%d days', n - 1)),
       strftime('%Y-%m-%dT%H:%M:%SZ', '2026-08-01T09:00:00Z', printf('+%d days', n - 1)),
       'COMPLETED',
       strftime('%Y-%m-%dT%H:%M:%SZ', '2026-08-01T08:00:00Z', printf('+%d days', n - 1))
FROM seq;

INSERT OR IGNORE INTO alarm_event
    (id, pile_id, order_id, severity, alarm_type, content, status, occurred_at)
VALUES
    (1, 103, 'ORD-DEMO-003', 'WARNING', 'OVER_TEMPERATURE', '演示：温度超过阈值', 'CLOSED', '2026-08-03T08:30:00Z'),
    (2, 107, 'ORD-DEMO-007', 'CRITICAL', 'DEVICE_OFFLINE', '演示：设备心跳超时', 'ACKNOWLEDGED', '2026-08-07T08:30:00Z'),
    (3, 111, 'ORD-DEMO-011', 'WARNING', 'COMMAND_TIMEOUT', '演示：启动命令超时', 'OPEN', '2026-08-11T08:30:00Z');

INSERT OR IGNORE INTO maintenance_ticket
    (id, alarm_id, assignee, status, notes, created_at, updated_at, resolved_at, closed_at)
VALUES
    (1, 1, '工程师甲', 'CLOSED', '演示：散热模块已更换', '2026-08-03T08:31:00Z', '2026-08-03T09:10:00Z', '2026-08-03T09:00:00Z', '2026-08-03T09:10:00Z'),
    (2, 2, '工程师乙', 'PROCESSING', NULL, '2026-08-07T08:31:00Z', '2026-08-07T08:40:00Z', NULL, NULL),
    (3, 3, NULL, 'OPEN', NULL, '2026-08-11T08:31:00Z', '2026-08-11T08:31:00Z', NULL, NULL);
