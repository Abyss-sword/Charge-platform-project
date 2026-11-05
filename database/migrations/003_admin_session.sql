-- 成员1 · 第一阶段
-- 迁移 003：管理员会话表 admin_session 及过期时间索引——
-- 支持管理员登录后凭 Token（库存哈希、8 小时有效）持续访问管理接口。

CREATE TABLE admin_session (
    token_hash TEXT PRIMARY KEY,
    admin_id INTEGER NOT NULL REFERENCES admin_user(id),
    expires_at TEXT NOT NULL,
    created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE INDEX ix_admin_session_expiry ON admin_session(expires_at);
