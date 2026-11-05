-- Freeze the lifetime-meter baseline used by every charging order.
ALTER TABLE charge_order ADD COLUMN meter_start_kwh TEXT
    CHECK(meter_start_kwh IS NULL
          OR (typeof(meter_start_kwh) = 'text'
              AND meter_start_kwh NOT GLOB '*[^0-9.]*'
              AND length(meter_start_kwh) - length(replace(meter_start_kwh, '.', '')) = 1
              AND instr(meter_start_kwh, '.') > 1
              AND length(meter_start_kwh) - instr(meter_start_kwh, '.') = 2
              AND CAST(meter_start_kwh AS REAL) >= 0));

ALTER TABLE charge_order ADD COLUMN meter_start_recorded_at TEXT;
ALTER TABLE charge_order ADD COLUMN meter_updated_at TEXT;

-- Preserve whether telemetry came from the teaching simulator or a real device.
ALTER TABLE pile_telemetry ADD COLUMN source_mode TEXT NOT NULL DEFAULT 'UNKNOWN'
    CHECK(source_mode IN ('SIMULATED', 'REAL', 'UNKNOWN'));

-- Old databases may contain exact duplicate samples. Keep the first copy before
-- enforcing the idempotency key used by the service.
DELETE FROM pile_telemetry
WHERE id NOT IN (
    SELECT MIN(id)
    FROM pile_telemetry
    GROUP BY pile_id, recorded_at, energy_kwh
);

CREATE UNIQUE INDEX IF NOT EXISTS ux_pile_telemetry_sample
ON pile_telemetry(pile_id, recorded_at, energy_kwh);

