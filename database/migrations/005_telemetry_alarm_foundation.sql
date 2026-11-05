-- 成员1 · 第一阶段
-- 迁移 005：遥测与告警基础——工单解决/关闭时间字段、历史重复告警整理（ROW_NUMBER()
-- 分组排序去重）、补齐工单、活动告警唯一索引（ux_alarm_active_pile_type）、
-- 遥测与心跳查询索引。保证告警可追踪、可处理、不重复。

ALTER TABLE maintenance_ticket ADD COLUMN resolved_at TEXT;
ALTER TABLE maintenance_ticket ADD COLUMN closed_at TEXT;

WITH ranked_active_alarm AS (
    SELECT id,
           ROW_NUMBER() OVER (
               PARTITION BY pile_id, alarm_type
               ORDER BY occurred_at DESC, id DESC
           ) AS active_rank
    FROM alarm_event
    WHERE status IN ('OPEN', 'ACKNOWLEDGED')
)
UPDATE maintenance_ticket
SET status = 'CLOSED',
    notes = COALESCE(notes, '由重复活动告警迁移关闭'),
    updated_at = CURRENT_TIMESTAMP,
    resolved_at = COALESCE(resolved_at, CURRENT_TIMESTAMP),
    closed_at = COALESCE(closed_at, CURRENT_TIMESTAMP)
WHERE status <> 'CLOSED'
  AND alarm_id IN (
      SELECT id FROM ranked_active_alarm WHERE active_rank > 1
  );

WITH ranked_active_alarm AS (
    SELECT id,
           ROW_NUMBER() OVER (
               PARTITION BY pile_id, alarm_type
               ORDER BY occurred_at DESC, id DESC
           ) AS active_rank
    FROM alarm_event
    WHERE status IN ('OPEN', 'ACKNOWLEDGED')
)
UPDATE alarm_event
SET status = 'CLOSED',
    closed_at = COALESCE(closed_at, CURRENT_TIMESTAMP)
WHERE id IN (
    SELECT id FROM ranked_active_alarm WHERE active_rank > 1
);

INSERT INTO maintenance_ticket
    (alarm_id, status, notes, created_at, updated_at, resolved_at, closed_at)
SELECT alarm.id,
       CASE WHEN alarm.status = 'CLOSED' THEN 'CLOSED' ELSE 'OPEN' END,
       CASE WHEN alarm.status = 'CLOSED' THEN '由历史告警迁移生成' ELSE NULL END,
       alarm.occurred_at,
       COALESCE(alarm.closed_at, alarm.occurred_at),
       alarm.closed_at,
       alarm.closed_at
FROM alarm_event AS alarm
WHERE NOT EXISTS (
    SELECT 1 FROM maintenance_ticket AS ticket WHERE ticket.alarm_id = alarm.id
);

CREATE UNIQUE INDEX IF NOT EXISTS ux_alarm_active_pile_type
ON alarm_event(pile_id, alarm_type)
WHERE status IN ('OPEN', 'ACKNOWLEDGED');

CREATE INDEX IF NOT EXISTS ix_pile_telemetry_pile_time
ON pile_telemetry(pile_id, recorded_at DESC);

CREATE INDEX IF NOT EXISTS ix_pile_heartbeat
ON pile_info(last_heartbeat_at)
WHERE last_heartbeat_at IS NOT NULL;

CREATE INDEX IF NOT EXISTS ix_alarm_status_time
ON alarm_event(status, occurred_at DESC);
