-- 成员1 · 第一阶段收尾
-- 迁移 009：管理禁用覆盖标记（01文档 4.3）。
-- admin_status_override：管理员显式设为 FAULT/OFFLINE 的覆盖状态；NULL 表示无覆盖。
-- 普通正常遥测不得清除；重启保留；解除需管理员显式操作（且要求近期正常读数、
-- 无活动订单）。与通信故障（OFFLINE 扫描）与物理故障（过热 FAULT）分开管理。

ALTER TABLE pile_info ADD COLUMN admin_status_override TEXT
    CHECK(admin_status_override IS NULL
          OR admin_status_override IN ('FAULT', 'OFFLINE'));
