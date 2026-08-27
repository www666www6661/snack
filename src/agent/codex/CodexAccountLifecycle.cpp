#include "agent/codex/CodexAccountLifecycle.h"

namespace snack::agent::codex {
namespace {

void setError(QString* error, const QString& message) {
    if (error != nullptr) {
        *error = message;
    }
}

} // namespace

QJsonObject accountReadParameters() { return {{QStringLiteral("refreshToken"), false}}; }

std::optional<CodexAccountInfo> parseAccountReadResponse(const QJsonValue& value, QString* error) {
    if (!value.isObject()) {
        setError(error, QStringLiteral("account response must be an object"));
        return std::nullopt;
    }
    const QJsonObject object = value.toObject();
    if (!object.value(QStringLiteral("requiresOpenaiAuth")).isBool()) {
        setError(error, QStringLiteral("requiresOpenaiAuth must be a boolean"));
        return std::nullopt;
    }

    CodexAccountInfo result;
    result.requiresOpenaiAuth = object.value(QStringLiteral("requiresOpenaiAuth")).toBool();
    const QJsonValue accountValue = object.value(QStringLiteral("account"));
    if (accountValue.isUndefined() || accountValue.isNull()) {
        return result;
    }
    if (!accountValue.isObject()) {
        setError(error, QStringLiteral("account must be an object or null"));
        return std::nullopt;
    }
    const QJsonObject account = accountValue.toObject();
    result.type = account.value(QStringLiteral("type")).toString();
    if (result.type.isEmpty()) {
        setError(error, QStringLiteral("account type is missing"));
        return std::nullopt;
    }
    result.planType = account.value(QStringLiteral("planType")).toString();
    result.hasAccount = true;
    return result;
}

} // namespace snack::agent::codex
