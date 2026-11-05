-- 第一阶段联动契约（审查 01 规格 · S1-A）：
-- linkage_meta 保存全局写版本计数器；每次业务写事务提交前由 linkage::bumpVersion 原子 +1。
-- 查询类响应以当时版本作为 snapshotVersion 返回，客户端据此丢弃旧快照。
CREATE TABLE IF NOT EXISTS linkage_meta (
    key TEXT PRIMARY KEY,
    value TEXT NOT NULL
);
INSERT OR IGNORE INTO linkage_meta(key, value) VALUES ('changeVersion', '1');
