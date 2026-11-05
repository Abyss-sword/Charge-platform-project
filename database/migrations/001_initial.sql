-- 成员1 · 第一阶段
-- 迁移 001：初始 11 张业务表（管理员/用户/站点/电桩/订单/钱包流水/遥测/告警/工单/操作日志/
-- 请求防重）及基本关联与约束。答辩口径：这是建库起点，包含历史整数金额字段，
-- 不代表现在仍以整数分作为业务金额。

PRAGMA foreign_keys = ON;

CREATE TABLE IF NOT EXISTS admin_user (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    username TEXT NOT NULL UNIQUE,
    password_hash TEXT NOT NULL,
    password_salt TEXT NOT NULL,
    status TEXT NOT NULL DEFAULT 'ACTIVE'
        CHECK(status IN ('ACTIVE', 'DISABLED')),
    created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE IF NOT EXISTS user_info (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    phone TEXT NOT NULL UNIQUE,
    nickname TEXT NOT NULL,
    avatar_url TEXT,
    balance_cents INTEGER NOT NULL DEFAULT 0 CHECK(balance_cents >= 0),
    status TEXT NOT NULL DEFAULT 'NORMAL'
        CHECK(status IN ('NORMAL', 'FROZEN')),
    created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE IF NOT EXISTS station_info (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT NOT NULL,
    address TEXT NOT NULL,
    latitude REAL NOT NULL,
    longitude REAL NOT NULL,
    price_cents_per_kwh INTEGER NOT NULL DEFAULT 120 CHECK(price_cents_per_kwh >= 0),
    status TEXT NOT NULL DEFAULT 'OPEN'
        CHECK(status IN ('OPEN', 'CLOSED', 'MAINTENANCE')),
    created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE IF NOT EXISTS pile_info (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    station_id INTEGER NOT NULL REFERENCES station_info(id),
    pile_sn TEXT NOT NULL UNIQUE,
    charge_type TEXT NOT NULL CHECK(charge_type IN ('FAST', 'SLOW')),
    rated_power_kw REAL NOT NULL CHECK(rated_power_kw > 0),
    status TEXT NOT NULL DEFAULT 'IDLE'
        CHECK(status IN ('IDLE', 'RESERVED', 'CHARGING', 'FAULT', 'OFFLINE')),
    last_heartbeat_at TEXT,
    created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE IF NOT EXISTS charge_order (
    id TEXT PRIMARY KEY,
    user_id INTEGER NOT NULL REFERENCES user_info(id),
    pile_id INTEGER NOT NULL REFERENCES pile_info(id),
    status TEXT NOT NULL
        CHECK(status IN ('RESERVED', 'CHARGING', 'PENDING_PAYMENT', 'FINISHED', 'CANCELED', 'EXPIRED')),
    energy_wh INTEGER NOT NULL DEFAULT 0 CHECK(energy_wh >= 0),
    amount_cents INTEGER NOT NULL DEFAULT 0 CHECK(amount_cents >= 0),
    reserved_until TEXT,
    started_at TEXT,
    ended_at TEXT,
    end_reason TEXT,
    created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE IF NOT EXISTS wallet_transaction (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id INTEGER NOT NULL REFERENCES user_info(id),
    transaction_type TEXT NOT NULL CHECK(transaction_type IN ('RECHARGE', 'PAYMENT', 'REFUND')),
    amount_cents INTEGER NOT NULL CHECK(amount_cents != 0),
    order_id TEXT REFERENCES charge_order(id),
    request_id TEXT NOT NULL UNIQUE,
    created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE IF NOT EXISTS pile_telemetry (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    pile_id INTEGER NOT NULL REFERENCES pile_info(id),
    recorded_at TEXT NOT NULL,
    temperature_c REAL NOT NULL,
    voltage_v REAL NOT NULL,
    current_a REAL NOT NULL,
    power_w INTEGER NOT NULL CHECK(power_w >= 0),
    energy_wh INTEGER NOT NULL CHECK(energy_wh >= 0)
);

CREATE TABLE IF NOT EXISTS alarm_event (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    pile_id INTEGER NOT NULL REFERENCES pile_info(id),
    order_id TEXT REFERENCES charge_order(id),
    severity TEXT NOT NULL CHECK(severity IN ('INFO', 'WARNING', 'CRITICAL')),
    alarm_type TEXT NOT NULL,
    content TEXT NOT NULL,
    status TEXT NOT NULL DEFAULT 'OPEN' CHECK(status IN ('OPEN', 'ACKNOWLEDGED', 'CLOSED')),
    occurred_at TEXT NOT NULL,
    closed_at TEXT
);

CREATE TABLE IF NOT EXISTS maintenance_ticket (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    alarm_id INTEGER NOT NULL UNIQUE REFERENCES alarm_event(id),
    assignee TEXT,
    status TEXT NOT NULL DEFAULT 'OPEN' CHECK(status IN ('OPEN', 'PROCESSING', 'RESOLVED', 'CLOSED')),
    notes TEXT,
    created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE IF NOT EXISTS operation_log (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    actor_type TEXT NOT NULL,
    actor_id TEXT,
    action TEXT NOT NULL,
    target_type TEXT,
    target_id TEXT,
    result TEXT NOT NULL,
    created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE IF NOT EXISTS request_dedup (
    request_id TEXT PRIMARY KEY,
    action TEXT NOT NULL,
    result_json TEXT NOT NULL,
    created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);
