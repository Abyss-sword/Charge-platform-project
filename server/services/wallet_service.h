#pragma once

#include "shared/domain/decimal_value.h"

#include <QSqlDatabase>
#include <QString>

// 钱包业务结果（ok=false 时 errorCode 说明原因：余额不足/状态冲突/重复请求等）。
struct WalletResult
{
    bool ok = false;
    QString errorCode;
};

// WalletService：钱包支付 / 充值 / 资料更新。
// 一致性设计（答辩核心）：写操作统一 BEGIN IMMEDIATE 开写事务；
// 支付、充值都以 requestId 为防重键——重复请求且原参数一致 → 返回原成功结果（幂等），
// 参数不一致 → DUPLICATE_REQUEST；扣款、流水、订单完成、释放电桩在同一事务内，
// 任何一步失败整体回滚，不会留下“扣了钱没订单”或“有订单没扣钱”的半笔交易。
class WalletService
{
public:
    explicit WalletService(QSqlDatabase database);

    // 支付待支付订单：校验 PENDING_PAYMENT 状态与余额充足；零金额订单跳过流水但仍记防重。
    // requireUserId>0 时先校验订单归属（他人订单 AUTH_INVALID，防重查询之前完成）。
    WalletResult pay(const QString &orderId, const QString &requestId,
                     qint64 requireUserId = 0);
    // 充值：金额限定 1.00~10000.00 元（两位小数字符串）。
    WalletResult recharge(qint64 userId, const DecimalValue &amountYuan,
                          const QString &requestId);
    // 昵称（trim 后 2~20 字符）与头像（本地路径，PNG/JPEG/WebP 签名 + ≤5MiB）更新。
    WalletResult updateProfile(qint64 userId, const QString &nickname,
                               const QString &avatarPath);

private:
    QSqlDatabase database_;
};
