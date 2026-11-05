-- 成员1 · 第一阶段
-- 迁移 002：第一阶段业务调整——历史订单单价字段、活动订单电桩部分唯一索引
-- （ux_charge_order_active_pile：仅对 RESERVED/CHARGING/PENDING_PAYMENT 生效，
-- 同一电桩可有多条历史订单但不能被两个活动订单同时占用）、钱包流水结构调整。

ALTER TABLE charge_order
ADD COLUMN unit_price_cents_per_kwh INTEGER NOT NULL DEFAULT 0
    CHECK(unit_price_cents_per_kwh BETWEEN 0 AND 100000);

CREATE UNIQUE INDEX IF NOT EXISTS ux_charge_order_active_pile
ON charge_order(pile_id)
WHERE status IN ('RESERVED', 'CHARGING', 'PENDING_PAYMENT');

ALTER TABLE wallet_transaction RENAME TO wallet_transaction_legacy;

CREATE TABLE wallet_transaction (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id INTEGER NOT NULL REFERENCES user_info(id),
    transaction_type TEXT NOT NULL
        CHECK(transaction_type IN ('INITIAL_CREDIT', 'RECHARGE', 'PAYMENT', 'REFUND')),
    amount_cents INTEGER NOT NULL CHECK(amount_cents != 0),
    order_id TEXT REFERENCES charge_order(id),
    request_id TEXT NOT NULL UNIQUE,
    created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);

INSERT INTO wallet_transaction
    (id,user_id,transaction_type,amount_cents,order_id,request_id,created_at)
SELECT id,user_id,transaction_type,amount_cents,order_id,request_id,created_at
FROM wallet_transaction_legacy;

DROP TABLE wallet_transaction_legacy;
