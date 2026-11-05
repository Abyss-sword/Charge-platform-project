-- 成员1 · 第一阶段
-- 迁移 004：金额/电量从整数分/Wh 迁移为元/kWh 两位小数文本字段，并补充相关表和约束。
-- 答辩口径：既保留原始业务数据，又统一当前两位小数格式；
-- 迁移必须按编号顺序执行，不能只执行 004。

ALTER TABLE user_info ADD COLUMN balance_yuan TEXT NOT NULL DEFAULT '0.00'
    CHECK(typeof(balance_yuan) = 'text'
          AND balance_yuan NOT GLOB '*[^0-9.]*'
          AND length(balance_yuan) - length(replace(balance_yuan, '.', '')) = 1
          AND instr(balance_yuan, '.') > 1
          AND length(balance_yuan) - instr(balance_yuan, '.') = 2
          AND CAST(balance_yuan AS REAL) >= 0);

UPDATE user_info
SET balance_yuan = printf('%d.%02d', balance_cents / 100, balance_cents % 100);

ALTER TABLE station_info ADD COLUMN price_yuan_per_kwh TEXT NOT NULL DEFAULT '1.20'
    CHECK(typeof(price_yuan_per_kwh) = 'text'
          AND price_yuan_per_kwh NOT GLOB '*[^0-9.]*'
          AND length(price_yuan_per_kwh) - length(replace(price_yuan_per_kwh, '.', '')) = 1
          AND instr(price_yuan_per_kwh, '.') > 1
          AND length(price_yuan_per_kwh) - instr(price_yuan_per_kwh, '.') = 2
          AND CAST(price_yuan_per_kwh AS REAL) >= 0);

UPDATE station_info
SET price_yuan_per_kwh = printf('%d.%02d',
    price_cents_per_kwh / 100, price_cents_per_kwh % 100);

ALTER TABLE charge_order ADD COLUMN energy_kwh TEXT NOT NULL DEFAULT '0.00'
    CHECK(typeof(energy_kwh) = 'text'
          AND energy_kwh NOT GLOB '*[^0-9.]*'
          AND length(energy_kwh) - length(replace(energy_kwh, '.', '')) = 1
          AND instr(energy_kwh, '.') > 1
          AND length(energy_kwh) - instr(energy_kwh, '.') = 2
          AND CAST(energy_kwh AS REAL) >= 0);

ALTER TABLE charge_order ADD COLUMN amount_yuan TEXT NOT NULL DEFAULT '0.00'
    CHECK(typeof(amount_yuan) = 'text'
          AND amount_yuan NOT GLOB '*[^0-9.]*'
          AND length(amount_yuan) - length(replace(amount_yuan, '.', '')) = 1
          AND instr(amount_yuan, '.') > 1
          AND length(amount_yuan) - instr(amount_yuan, '.') = 2
          AND CAST(amount_yuan AS REAL) >= 0);

ALTER TABLE charge_order ADD COLUMN unit_price_yuan_per_kwh TEXT NOT NULL DEFAULT '0.00'
    CHECK(typeof(unit_price_yuan_per_kwh) = 'text'
          AND unit_price_yuan_per_kwh NOT GLOB '*[^0-9.]*'
          AND length(unit_price_yuan_per_kwh) - length(replace(unit_price_yuan_per_kwh, '.', '')) = 1
          AND instr(unit_price_yuan_per_kwh, '.') > 1
          AND length(unit_price_yuan_per_kwh) - instr(unit_price_yuan_per_kwh, '.') = 2
          AND CAST(unit_price_yuan_per_kwh AS REAL) >= 0);

UPDATE charge_order
SET energy_kwh = printf('%d.%02d',
        ((energy_wh + 5) / 10) / 100, ((energy_wh + 5) / 10) % 100),
    amount_yuan = printf('%d.%02d', amount_cents / 100, amount_cents % 100),
    unit_price_yuan_per_kwh = printf('%d.%02d',
        unit_price_cents_per_kwh / 100, unit_price_cents_per_kwh % 100);

ALTER TABLE wallet_transaction ADD COLUMN amount_yuan TEXT NOT NULL DEFAULT '0.01'
    CHECK(typeof(amount_yuan) = 'text'
          AND ltrim(amount_yuan, '-') NOT GLOB '*[^0-9.]*'
          AND length(amount_yuan) - length(replace(amount_yuan, '.', '')) = 1
          AND instr(ltrim(amount_yuan, '-'), '.') > 1
          AND length(amount_yuan) - instr(amount_yuan, '.') = 2
          AND amount_yuan NOT IN ('0.00', '-0.00'));

UPDATE wallet_transaction
SET amount_yuan = CASE
    WHEN amount_cents < 0 THEN '-' || printf('%d.%02d',
        abs(amount_cents) / 100, abs(amount_cents) % 100)
    ELSE printf('%d.%02d', amount_cents / 100, amount_cents % 100)
END;

ALTER TABLE pile_telemetry ADD COLUMN energy_kwh TEXT NOT NULL DEFAULT '0.00'
    CHECK(typeof(energy_kwh) = 'text'
          AND energy_kwh NOT GLOB '*[^0-9.]*'
          AND length(energy_kwh) - length(replace(energy_kwh, '.', '')) = 1
          AND instr(energy_kwh, '.') > 1
          AND length(energy_kwh) - instr(energy_kwh, '.') = 2
          AND CAST(energy_kwh AS REAL) >= 0);

UPDATE pile_telemetry
SET energy_kwh = printf('%d.%02d',
    ((energy_wh + 5) / 10) / 100, ((energy_wh + 5) / 10) % 100);

ALTER TABLE user_info DROP COLUMN balance_cents;
ALTER TABLE station_info DROP COLUMN price_cents_per_kwh;
ALTER TABLE charge_order DROP COLUMN energy_wh;
ALTER TABLE charge_order DROP COLUMN amount_cents;
ALTER TABLE charge_order DROP COLUMN unit_price_cents_per_kwh;
ALTER TABLE wallet_transaction DROP COLUMN amount_cents;
ALTER TABLE pile_telemetry DROP COLUMN energy_wh;
