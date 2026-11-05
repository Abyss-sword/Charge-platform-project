#include "server/services/wallet_service.h"

#include "server/repositories/wallet_repository.h"
#include "server/services/linkage.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlQuery>

#include <optional>
#include <utility>

namespace {
// 统一失败结果构造。
WalletResult failure(const QString &errorCode)
{
    return {false, errorCode};
}

// BEGIN IMMEDIATE：立即取得 SQLite 写资格，把“查防重→查单→扣款→完成订单”压进同一写窗口。
bool beginImmediate(QSqlDatabase database)
{
    QSqlQuery query(database);
    return query.exec(QStringLiteral("BEGIN IMMEDIATE"));
}

// 提交失败时回滚；提交前原子抬升联动版本（S1-A 契约，见 linkage.h）。
bool commitOrRollback(QSqlDatabase database)
{
    if (linkage::bumpVersion(database) < 0) {
        database.rollback();
        return false;
    }
    if (database.commit()) {
        return true;
    }
    database.rollback();
    return false;
}

QString compactJson(const QJsonObject &object)
{
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

std::optional<QJsonObject> parseObject(const QString &json)
{
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(json.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        return std::nullopt;
    }
    return document.object();
}

// 头像校验（注意边界）：检查的是服务端可访问的本地文件路径——
// 必须是文件、≤5MiB，且文件头魔数为 PNG（89 50 4E 47 0D 0A 1A 0A）、JPEG（FF D8 FF）
// 或 WebP（<4字节>"RIFF" + 偏移8起"WEBP"）。与 01 规格及客户端约束保持一致。
// 答辩口径：只校验容器魔数（不解码图像），也不是完整的客户端上传链路（无安全文件名生成/静态资源服务）。
bool isSupportedAvatar(const QString &path)
{
    if (path.isEmpty()) {
        return true;
    }
    const QFileInfo info(path);
    if (!info.isFile() || info.size() > 5 * 1024 * 1024) {
        return false;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    const QByteArray header = file.read(12);
    const QByteArray pngSignature("\x89PNG\r\n\x1a\n", 8);
    const bool png = header.size() >= pngSignature.size()
        && header.left(pngSignature.size()) == pngSignature;
    const bool jpeg = header.size() >= 3
        && static_cast<unsigned char>(header[0]) == 0xff
        && static_cast<unsigned char>(header[1]) == 0xd8
        && static_cast<unsigned char>(header[2]) == 0xff;
    const bool webp = header.size() >= 12
        && header.left(4) == QByteArray("RIFF")
        && header.mid(8, 4) == QByteArray("WEBP");
    return png || jpeg || webp;
}
}

WalletService::WalletService(QSqlDatabase database)
    : database_(std::move(database))
{
}

// 支付主流程（答辩核心，防重复扣款）：
// 1) BEGIN IMMEDIATE 开写事务；2) 查防重记录：动作=PAYMENT 且订单一致 → 复放成功（幂等），
//    否则 DUPLICATE_REQUEST（同编号换订单直接拒绝）；
// 3) 校验订单存在且状态为 PENDING_PAYMENT（重复支付已完成订单在此被拦）；
// 4) 余额检查，不足 → BALANCE_INSUFFICIENT（失败即回滚，不留支付流水）；
// 5) 零金额订单跳过扣款/流水；6) 同一事务：防重记录 + CAS 扣余额 + 写流水 +
//    订单 PENDING_PAYMENT→FINISHED + 释放电桩，任一失败整体回滚。
WalletResult WalletService::pay(const QString &orderId, const QString &requestId,
                                 qint64 requireUserId)
{
    if (orderId.trimmed().isEmpty() || requestId.trimmed().isEmpty()) {
        return failure(QStringLiteral("PARAM_INVALID"));
    }
    if (!database_.isOpen() || !beginImmediate(database_)) {
        return failure(QStringLiteral("DATABASE_ERROR"));
    }

    WalletRepository repository(database_);
    QString error;
    const auto replay = repository.findRequest(requestId, &error);
    if (!error.isEmpty()) {
        database_.rollback();
        return failure(QStringLiteral("DATABASE_ERROR"));
    }
    // 防重回放：同一 requestId 必须对应同一订单才复放成功，防止请求编号被挪用到别的订单。
    if (replay) {
        const auto stored = parseObject(replay->resultJson);
        const bool sameRequest = replay->action == QStringLiteral("PAYMENT") && stored
            && stored->value(QStringLiteral("orderId")).toString() == orderId;
        database_.rollback();
        return sameRequest ? WalletResult{true, QStringLiteral("OK")}
                           : failure(QStringLiteral("DUPLICATE_REQUEST"));
    }

    const QString resultJson = compactJson({{QStringLiteral("orderId"), orderId}});
    if (!repository.recordRequest(requestId, QStringLiteral("PAYMENT"), resultJson, &error)) {
        database_.rollback();
        return failure(QStringLiteral("DATABASE_ERROR"));
    }

    const auto order = repository.findOrder(orderId, &error);
    if (!error.isEmpty()) {
        database_.rollback();
        return failure(QStringLiteral("DATABASE_ERROR"));
    }
    // M1-02：他人订单 → AUTH_INVALID（先于状态判断，不泄露订单状态细节）。
    if (requireUserId > 0 && order && order->userId != requireUserId) {
        database_.rollback();
        return failure(QStringLiteral("AUTH_INVALID"));
    }
    if (!order || order->status != QStringLiteral("PENDING_PAYMENT")) {
        database_.rollback();
        return failure(QStringLiteral("ORDER_STATE_CONFLICT"));
    }

    // 余额检查：不足即回滚失败——绝不允许“扣到负数”或留下成功支付流水。
    const auto balance = repository.balanceYuan(order->userId, &error);
    if (!balance || !error.isEmpty()) {
        database_.rollback();
        return failure(QStringLiteral("DATABASE_ERROR"));
    }
    if (*balance < order->amountYuan) {
        database_.rollback();
        return failure(QStringLiteral("BALANCE_INSUFFICIENT"));
    }

    // 零金额订单：允许直接完成，不产生零金额流水；setBalance 仍是 CAS（预期旧余额）。
    // 非零金额：扣款（余额 CAS 更新）+ PAYMENT 负向流水，之后 finishOrder + releasePile。
    bool paymentWritten = true;
    if (!order->amountYuan.isZero()) {
        const DecimalValue newBalance = *balance - order->amountYuan;
        const DecimalValue debit = DecimalValue{} - order->amountYuan;
        paymentWritten = repository.setBalance(order->userId, *balance, newBalance, &error)
            && repository.addTransaction(order->userId, QStringLiteral("PAYMENT"),
                                         debit, orderId,
                                         requestId, &error);
    }
    if (!paymentWritten || !repository.finishOrder(orderId, &error)
        || !repository.releasePile(order->pileId, &error)
        || !commitOrRollback(database_)) {
        database_.rollback();
        return failure(QStringLiteral("DATABASE_ERROR"));
    }
    return {true, QStringLiteral("OK")};
}

    // 充值主流程：1.00~10000.00 元边界（parseCanonical 精确两位）；requestId 防重；
    // 同一事务：防重记录 + CAS 加余额（预期旧余额）+ RECHARGE 正向流水。
    // 注意：充值入的是钱包流水，不计入充电营收统计。
WalletResult WalletService::recharge(qint64 userId, const DecimalValue &amountYuan,
                                     const QString &requestId)
{
    const DecimalValue minimum = *DecimalValue::parseCanonical(QStringLiteral("1.00"));
    const DecimalValue maximum = *DecimalValue::parseCanonical(QStringLiteral("10000.00"));
    if (userId <= 0 || amountYuan < minimum || amountYuan > maximum
        || requestId.trimmed().isEmpty()) {
        return failure(QStringLiteral("PARAM_INVALID"));
    }
    if (!database_.isOpen() || !beginImmediate(database_)) {
        return failure(QStringLiteral("DATABASE_ERROR"));
    }

    WalletRepository repository(database_);
    QString error;
    const auto replay = repository.findRequest(requestId, &error);
    if (!error.isEmpty()) {
        database_.rollback();
        return failure(QStringLiteral("DATABASE_ERROR"));
    }
    if (replay) {
        const auto stored = parseObject(replay->resultJson);
        const bool sameRequest = replay->action == QStringLiteral("RECHARGE") && stored
            && stored->value(QStringLiteral("userId")).toString() == QString::number(userId)
            && stored->value(QStringLiteral("amountYuan")).toString()
                == amountYuan.toString();
        database_.rollback();
        return sameRequest ? WalletResult{true, QStringLiteral("OK")}
                           : failure(QStringLiteral("DUPLICATE_REQUEST"));
    }

    const QString resultJson = compactJson({
        {QStringLiteral("userId"), QString::number(userId)},
        {QStringLiteral("amountYuan"), amountYuan.toString()}
    });
    const auto balance = repository.balanceYuan(userId, &error);
    if (!balance || !error.isEmpty()) {
        database_.rollback();
        return failure(QStringLiteral("DATABASE_ERROR"));
    }
    const DecimalValue newBalance = *balance + amountYuan;
    if (!repository.recordRequest(requestId, QStringLiteral("RECHARGE"), resultJson, &error)
        || !repository.setBalance(userId, *balance, newBalance, &error)
        || !repository.addTransaction(userId, QStringLiteral("RECHARGE"), amountYuan,
                                      {}, requestId, &error)
        || !commitOrRollback(database_)) {
        database_.rollback();
        return failure(QStringLiteral("DATABASE_ERROR"));
    }
    return {true, QStringLiteral("OK")};
}

// 资料更新：昵称 trim 后 2~20 字符；头像走文件签名/大小校验；单条 UPDATE 落库。
WalletResult WalletService::updateProfile(qint64 userId, const QString &nickname,
                                          const QString &avatarPath)
{
    const QString normalizedNickname = nickname.trimmed();
    if (userId <= 0 || normalizedNickname.size() < 2 || normalizedNickname.size() > 20
        || !isSupportedAvatar(avatarPath)) {
        return failure(QStringLiteral("PARAM_INVALID"));
    }
    if (!database_.isOpen()) {
        return failure(QStringLiteral("DATABASE_ERROR"));
    }

    // S1-A：资料更新改为显式事务并在提交前抬升联动版本，保证其他端能以版本号感知变更。
    if (!beginImmediate(database_)) {
        return failure(QStringLiteral("DATABASE_ERROR"));
    }
    WalletRepository repository(database_);
    QString error;
    if (!repository.updateProfile(userId, normalizedNickname, avatarPath, &error)) {
        database_.rollback();
        return failure(QStringLiteral("DATABASE_ERROR"));
    }
    if (!commitOrRollback(database_)) {
        return failure(QStringLiteral("DATABASE_ERROR"));
    }
    return {true, QStringLiteral("OK")};
}
