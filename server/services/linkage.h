#pragma once

#include <QSqlDatabase>

#include <QString>

// 第一阶段联动契约（01 修改规格 · 公共联动）：
//   - 每个业务写事务在提交前调用 bumpVersion，把 linkage_meta.changeVersion 原子 +1；
//   - 查询类响应用 currentVersion 作为 snapshotVersion，客户端比较版本丢弃旧快照；
//   - bump 必须在调用方事务内执行，保证“版本号”与“业务写入”同生共死。
// 该模块只依赖 QSqlDatabase，不持有状态，供各 Service 与 RequestRouter 复用。
namespace linkage {

// 读取当前全局写版本；表不存在或无记录时返回 1（迁移 010 保证正常情况必然存在）。
qint64 currentVersion(const QSqlDatabase &database);

// 在调用方事务内把 changeVersion 原子 +1 并返回新值；失败返回 -1（调用方应回滚）。
qint64 bumpVersion(const QSqlDatabase &database);

// 统一的服务器时间文本（UTC，ISO-8601，秒精度，Z 结尾）。
QString serverTimeUtc();

}  // namespace linkage
