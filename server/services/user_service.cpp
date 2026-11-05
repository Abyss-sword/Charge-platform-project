#include "server/services/user_service.h"

#include "server/repositories/user_repository.h"
#include "server/services/user_session_service.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSqlError>
#include <QSqlQuery>

namespace {
// 登录防重的回放判定：查 request_dedup 中 action='LOGIN_OR_REGISTER' 的记录。
struct ReplayDecision
{
    bool found = false;
    LoginResult result;
};

// 返回 found=true 表示该 requestId 已处理过：
//   原参数（手机号）一致 → 复放原成功结果；不一致 → DUPLICATE_REQUEST（防止请求编号被挪用）。
ReplayDecision lookupReplay(QSqlDatabase database, UserRepository &repository,
                            const QString &phone, const QString &requestId)
{
    QSqlQuery replay(database);
    replay.prepare(QStringLiteral(
        "SELECT result_json FROM request_dedup WHERE request_id=? AND action='LOGIN_OR_REGISTER'"));
    replay.addBindValue(requestId);
    if (!replay.exec()) {
        return {true, {false, QStringLiteral("DATABASE_ERROR"), 0, {}, false, {}, {}}};
    }
    if (!replay.next()) {
        return {};
    }

    const QJsonObject stored = QJsonDocument::fromJson(replay.value(0).toByteArray()).object();
    if (stored.value(QStringLiteral("phone")).toString() != phone) {
        return {true, {false, QStringLiteral("DUPLICATE_REQUEST"), 0, {}, false, {}, {}}};
    }
    QString error;
    const auto user = repository.findByPhone(phone, &error);
    if (!user || !error.isEmpty()) {
        return {true, {false, QStringLiteral("DATABASE_ERROR"), 0, {}, false, {}, {}}};
    }
    // 回放成功结果但颁发新会话 Token：老用户重连需要新令牌进入自己的会话。
    LoginResult replayed{true, QStringLiteral("OK"), user->id, user->nickname,
                          user->status == QStringLiteral("FROZEN"), {}, {}};
    QString expiresAt;
    const QString token = UserSessionService(database).issue(user->id, &expiresAt);
    if (token.isEmpty()) {
        return {true, {false, QStringLiteral("DATABASE_ERROR"), 0, {}, false, {}, {}}};
    }
    replayed.userToken = token;
    replayed.expiresAtUtc = expiresAt;
    return {true, replayed};
}
}

UserService::UserService(QSqlDatabase database)
    : database_(std::move(database))
{
}

// 登录/注册主流程（答辩高频）：
// 1) 手机号正则 ^1[3-9]\d{9}$ 校验；2) 防重回放检查；
// 3) 新手机号 → 事务内建用户（昵称"用户"+手机尾号）并赠送 100.00 元初始余额 + 流水；
// 4) 老用户 → BEGIN IMMEDIATE 事务内仅记录本次登录请求编号（保证防重记录原子落库）；
// 5) 冻结用户仍可"登录"，但 restricted=true，业务操作在订单/支付层被拦；
// 6) M1-02：成功后颁发 user_session 会话 Token（库存摘要，8 小时）。
LoginResult UserService::loginOrRegister(const QString &phone, const QString &requestId)
{
    static const QRegularExpression PhonePattern(QStringLiteral("^1[3-9]\\d{9}$"));
    if (!PhonePattern.match(phone).hasMatch() || requestId.trimmed().isEmpty()) {
        return {false, QStringLiteral("PARAM_INVALID"), 0, {}, false, {}, {}};
    }
    if (!database_.isOpen()) {
        return {false, QStringLiteral("DATABASE_ERROR"), 0, {}, false, {}, {}};
    }

    UserRepository repository(database_);
    if (const auto replay = lookupReplay(database_, repository, phone, requestId);
        replay.found) {
        return replay.result;
    }

    QString lookupError;
    auto user = repository.findByPhone(phone, &lookupError);
    if (!lookupError.isEmpty()) {
        return {false, QStringLiteral("DATABASE_ERROR"), 0, {}, false, {}, {}};
    }
    if (!user) {
        QString error;
        user = repository.createWithInitialCredit(
            phone, QStringLiteral("用户") + phone.right(4), requestId, &error);
        if (!user) {
            if (const auto replay = lookupReplay(database_, repository, phone, requestId);
                replay.found) {
                return replay.result;
            }
            return {false, QStringLiteral("DATABASE_ERROR"), 0, {}, false, {}, {}};
        }
    } else {
        QSqlQuery begin(database_);
        if (!begin.exec(QStringLiteral("BEGIN IMMEDIATE"))) {
            return {false, QStringLiteral("DATABASE_ERROR"), 0, {}, false, {}, {}};
        }
        QString error;
        if (!repository.recordLoginRequest(requestId, phone, user->id, &error)
            || !database_.commit()) {
            database_.rollback();
            if (const auto replay = lookupReplay(database_, repository, phone, requestId);
                replay.found) {
                return replay.result;
            }
            return {false, QStringLiteral("DATABASE_ERROR"), 0, {}, false, {}, {}};
        }
    }
    LoginResult result{true, QStringLiteral("OK"), user->id, user->nickname,
                       user->status == QStringLiteral("FROZEN"), {}, {}};
    QString expiresAt;
    const QString token = UserSessionService(database_).issue(user->id, &expiresAt);
    if (token.isEmpty()) {
        return {false, QStringLiteral("DATABASE_ERROR"), 0, {}, false, {}, {}};
    }
    result.userToken = token;
    result.expiresAtUtc = expiresAt;
    return result;
}

UserProfileResult UserService::profile(qint64 userId) const
{
    if (userId <= 0) {
        return {false, QStringLiteral("PARAM_INVALID"), 0, {}, {}, {}, {}};
    }
    if (!database_.isOpen()) {
        return {false, QStringLiteral("DATABASE_ERROR"), 0, {}, {}, {}, {}};
    }
    QString error;
    const auto user = UserRepository(database_).findById(userId, &error);
    if (!error.isEmpty()) {
        return {false, QStringLiteral("DATABASE_ERROR"), 0, {}, {}, {}, {}};
    }
    if (!user) {
        return {false, QStringLiteral("NOT_FOUND"), 0, {}, {}, {}, {}};
    }
    // 资料查询：手机号打码（3 位前缀 + **** + 4 位尾号）；非 11 位 abnormal 直接全掩码。
    // 答辩口径：输出的是掩码手机号，接口不泄露完整号码。
    const QString masked = user->phone.size() == 11
        ? user->phone.left(3) + QStringLiteral("****") + user->phone.right(4)
        : QStringLiteral("****");
    return {true, QStringLiteral("OK"), user->id, masked, user->nickname,
            user->balanceYuan, user->status, user->avatarUrl};
}
