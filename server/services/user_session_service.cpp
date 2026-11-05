#include "server/services/user_session_service.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QRandomGenerator>
#include <QSqlError>
#include <QSqlQuery>

namespace {
// 会话有效期：8 小时（与 admin_session 一致）。
constexpr int kSessionHours = 8;

QString sha256Hex(const QString &token)
{
    return QString::fromLatin1(QCryptographicHash::hash(
        token.toUtf8(), QCryptographicHash::Sha256).toHex());
}
}

UserSessionService::UserSessionService(QSqlDatabase database)
    : database_(std::move(database))
{
}

QString UserSessionService::issue(qint64 userId, QString *expiresAtUtc)
{
    if (userId <= 0 || !database_.isOpen()) {
        return {};
    }
    // 32 字节密码学随机 Token（系统随机源），hex 输出 64 字符。
    QByteArray raw(32, 0);
    for (int i = 0; i < 32; i += 4) {
        const quint32 word = QRandomGenerator::system()->generate();
        raw[i] = char(word & 0xFF);
        raw[i + 1] = char((word >> 8) & 0xFF);
        raw[i + 2] = char((word >> 16) & 0xFF);
        raw[i + 3] = char((word >> 24) & 0xFF);
    }
    const QString tokenHex = QString::fromLatin1(raw.toHex());
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const QDateTime expires = now.addSecs(kSessionHours * 3600);
    const QString expiresStr = expires.toString(Qt::ISODate);

    QSqlQuery cleanup(database_);
    cleanup.prepare(QStringLiteral("DELETE FROM user_session WHERE expires_at<=?"));
    cleanup.addBindValue(now.toString(Qt::ISODate));
    if (!cleanup.exec()) {
        return {};
    }
    QSqlQuery insert(database_);
    insert.prepare(QStringLiteral(
        "INSERT INTO user_session(token_hash,user_id,created_at,expires_at,revoked) "
        "VALUES(?,?,?, ?,0)"));
    insert.addBindValue(sha256Hex(tokenHex));
    insert.addBindValue(userId);
    insert.addBindValue(now.toString(Qt::ISODate));
    insert.addBindValue(expiresStr);
    if (!insert.exec()) {
        return {};
    }
    if (expiresAtUtc) {
        *expiresAtUtc = expiresStr;
    }
    return tokenHex;
}

UserSessionResult UserSessionService::verify(const QString &token) const
{
    const QString trimmed = token.trimmed();
    if (trimmed.isEmpty() || !database_.isOpen()) {
        return {false, QStringLiteral("AUTH_INVALID"), 0, false};
    }
    QSqlQuery lookup(database_);
    lookup.prepare(QStringLiteral(
        "SELECT s.user_id, u.status FROM user_session s "
        "JOIN user_info u ON u.id=s.user_id "
        "WHERE s.token_hash=? AND s.revoked=0 AND s.expires_at>?"));
    lookup.addBindValue(sha256Hex(trimmed));
    const QString now = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    lookup.addBindValue(now);
    if (!lookup.exec() || !lookup.next()) {
        return {false, QStringLiteral("AUTH_INVALID"), 0, false};
    }
    const qint64 userId = lookup.value(0).toLongLong();
    const bool restricted =
        lookup.value(1).toString() == QStringLiteral("FROZEN");
    return {true, QStringLiteral("OK"), userId, restricted};
}
