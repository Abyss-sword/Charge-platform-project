-- Existing charging orders retain the legacy settlement rule (flag 0).
-- New starts persist their tariff and set this flag in the same transaction.
ALTER TABLE charge_order ADD COLUMN price_locked INTEGER NOT NULL DEFAULT 0
    CHECK(price_locked IN (0, 1));
