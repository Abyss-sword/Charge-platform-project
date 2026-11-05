#pragma once

#include "shared/domain/decimal_value.h"

#include <QSqlDatabase>
#include <QString>

#include <optional>

// 支付所需的订单最小信息（金额为两位小数 DecimalValue）。
struct WalletOrderRecord
{
    QString id;
    qint64 userId = 0;
    qint64 pileId = 0;
    QString status;
    DecimalValue amountYuan;
};

struct WalletRequestRecord
{
    QString action;
    QString resultJson;
};

// WalletRepository：钱包/支付数据访问。核心是 CAS 余额更新——
// UPDATE user_info SET balance_yuan=新 WHERE id=? AND balance_yuan=预期旧值，
// 并发扣款/充值时只有读到正确旧余额的一方成功，防止丢失更新。
class WalletRepository
{
public:
    explicit WalletRepository(QSqlDatabase database);

    // 防重记录查询（request_id 全局唯一键）。
    std::optional<WalletRequestRecord> findRequest(const QString &requestId,
                                                    QString *error = nullptr) const;
    // 订单 / 余额查询（金额与余额按规范两位文本解析，非法即失败）。
    std::optional<WalletOrderRecord> findOrder(const QString &orderId,
                                               QString *error = nullptr) const;
    std::optional<DecimalValue> balanceYuan(qint64 userId, QString *error = nullptr) const;

    // 写防重记录 / CAS 余额更新 / 写钱包流水（退款类型仅在表结构中预留，无退款接口）。
    bool recordRequest(const QString &requestId, const QString &action,
                       const QString &resultJson, QString *error = nullptr);
    bool setBalance(qint64 userId, const DecimalValue &expectedBalance,
                    const DecimalValue &newBalance, QString *error = nullptr);
    bool addTransaction(qint64 userId, const QString &type, const DecimalValue &amountYuan,
                        const QString &orderId, const QString &requestId,
                        QString *error = nullptr);
    // 订单 PENDING_PAYMENT→FINISHED；释放电桩（容错：正常桩 CHARGING→IDLE，
    // 故障/离线桩保持原状态不算失败）。
    bool finishOrder(const QString &orderId, QString *error = nullptr);
    bool releasePile(qint64 pileId, QString *error = nullptr);
    // 昵称/头像更新（两条 SQL 变体：有无头像）。
    bool updateProfile(qint64 userId, const QString &nickname, const QString &avatarPath,
                       QString *error = nullptr);

private:
    QSqlDatabase database_;
};
