-- 成员1 · 第一阶段收尾
-- 迁移 007：用户会话表 user_session（01文档 4.1）。
-- 教学用户会话：登录颁发随机 Token（客户端仅内存保存），库只存 SHA-256 摘要，
-- 8 小时有效，可撤销。手机号教学登录不变，无短信/实名。

CREATE TABLE IF NOT EXISTS user_session (
    token_hash TEXT PRIMARY KEY,
    user_id INTEGER NOT NULL REFERENCES user_info(id),
    created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
    expires_at TEXT NOT NULL,
    revoked INTEGER NOT NULL DEFAULT 0 CHECK(revoked IN (0, 1))
);

CREATE INDEX IF NOT EXISTS ix_user_session_expiry ON user_session(expires_at);
CREATE INDEX IF NOT EXISTS ix_user_session_user ON user_session(user_id);
