#include "agent/codex/CodexTurnLifecycle.h"

#include <QJsonArray>
#include <QSet>

namespace snack::agent::codex {
namespace {

void setError(QString* error, const QString& detail) {
    if (error != nullptr) {
        *error = detail;
    }
}

std::optional<QString> requiredString(const QJsonObject& object, const QString& name,
                                      QString* error, bool allowEmpty = false) {
    const QJsonValue value = object.value(name);
    if (!value.isString() || (!allowEmpty && value.toString().isEmpty())) {
        setError(error, QStringLiteral("Missing or invalid %1 field").arg(name));
        return std::nullopt;
    }
    return value.toString();
}

std::optional<CodexTurnInfo> parseTurnObject(const QJsonValue& value, QString* error) {
    if (!value.isObject()) {
        setError(error, QStringLiteral("Turn must be an object"));
        return std::nullopt;
    }
    const QJsonObject turn = value.toObject();
    const auto id = requiredString(turn, QStringLiteral("id"), error);
    const auto status = requiredString(turn, QStringLiteral("status"), error);
    if (!id.has_value() || !status.has_value()) {
        return std::nullopt;
    }
    static const QSet<QString> statuses = {QStringLiteral("completed"),
                                           QStringLiteral("interrupted"), QStringLiteral("failed"),
                                           QStringLiteral("inProgress")};
    if (!statuses.contains(*status)) {
        setError(error, QStringLiteral("Unknown Codex turn status: %1").arg(*status));
        return std::nullopt;
    }

    QString errorMessage;
    const QJsonValue turnError = turn.value(QStringLiteral("error"));
    if (!turnError.isNull() && !turnError.isUndefined()) {
        if (!turnError.isObject() ||
            !turnError.toObject().value(QStringLiteral("message")).isString()) {
            setError(error, QStringLiteral("Invalid turn error field"));
            return std::nullopt;
        }
        errorMessage = turnError.toObject().value(QStringLiteral("message")).toString();
    }
    return CodexTurnInfo{.id = *id, .status = *status, .errorMessage = errorMessage, .raw = turn};
}

} // namespace

QJsonObject turnAccessParameters(domain::AccessLevel accessLevel) {
    switch (accessLevel) {
    case domain::AccessLevel::Strict:
        return {{QStringLiteral("approvalPolicy"), QStringLiteral("untrusted")},
                {QStringLiteral("sandboxPolicy"),
                 QJsonObject{{QStringLiteral("type"), QStringLiteral("readOnly")}}}};
    case domain::AccessLevel::Workspace:
        return {{QStringLiteral("approvalPolicy"), QStringLiteral("on-request")},
                {QStringLiteral("sandboxPolicy"),
                 QJsonObject{{QStringLiteral("type"), QStringLiteral("workspaceWrite")}}}};
    case domain::AccessLevel::Full:
        return {{QStringLiteral("approvalPolicy"), QStringLiteral("never")},
                {QStringLiteral("sandboxPolicy"),
                 QJsonObject{{QStringLiteral("type"), QStringLiteral("dangerFullAccess")}}}};
    }
    return {};
}

QJsonObject makeTurnStartParameters(const QString& threadId, const QString& cwd,
                                    const TurnRequest& request) {
    QJsonObject params = turnAccessParameters(request.settings.accessLevel);
    params.insert(QStringLiteral("threadId"), threadId);
    params.insert(QStringLiteral("input"),
                  QJsonArray{QJsonObject{{QStringLiteral("type"), QStringLiteral("text")},
                                         {QStringLiteral("text"), request.message}}});
    params.insert(QStringLiteral("clientUserMessageId"),
                  request.turnId.toString(QUuid::WithoutBraces));
    params.insert(QStringLiteral("cwd"), cwd);
    params.insert(QStringLiteral("model"), request.settings.modelId);
    params.insert(QStringLiteral("effort"), domain::enumName(request.settings.reasoningEffort));
    return params;
}

QJsonObject makeTurnInterruptParameters(const QString& threadId, const QString& turnId) {
    return {{QStringLiteral("threadId"), threadId}, {QStringLiteral("turnId"), turnId}};
}

QJsonObject makeTurnSteerParameters(const QString& threadId, const QString& expectedTurnId,
                                    const QString& message, const QString& clientUserMessageId) {
    return {{QStringLiteral("threadId"), threadId},
            {QStringLiteral("expectedTurnId"), expectedTurnId},
            {QStringLiteral("input"),
             QJsonArray{QJsonObject{{QStringLiteral("type"), QStringLiteral("text")},
                                    {QStringLiteral("text"), message}}}},
            {QStringLiteral("clientUserMessageId"), clientUserMessageId}};
}

std::optional<CodexTurnInfo> parseTurnStartResponse(const QJsonValue& result, QString* error) {
    if (!result.isObject()) {
        setError(error, QStringLiteral("Turn response must be an object"));
        return std::nullopt;
    }
    return parseTurnObject(result.toObject().value(QStringLiteral("turn")), error);
}

std::optional<QString> parseTurnSteerResponse(const QJsonValue& result, QString* error) {
    if (!result.isObject()) {
        setError(error, QStringLiteral("Turn steer response must be an object"));
        return std::nullopt;
    }
    return requiredString(result.toObject(), QStringLiteral("turnId"), error);
}

std::optional<CodexTurnNotification> parseTurnNotification(const QJsonValue& params,
                                                           QString* error) {
    if (!params.isObject()) {
        setError(error, QStringLiteral("Turn notification params must be an object"));
        return std::nullopt;
    }
    const QJsonObject object = params.toObject();
    const auto threadId = requiredString(object, QStringLiteral("threadId"), error);
    const auto turn = parseTurnObject(object.value(QStringLiteral("turn")), error);
    if (!threadId.has_value() || !turn.has_value()) {
        return std::nullopt;
    }
    return CodexTurnNotification{.threadId = *threadId, .turn = *turn};
}

std::optional<CodexItemNotification> parseItemNotification(const QJsonValue& params,
                                                           QString* error) {
    if (!params.isObject()) {
        setError(error, QStringLiteral("Item notification params must be an object"));
        return std::nullopt;
    }
    const QJsonObject object = params.toObject();
    const auto threadId = requiredString(object, QStringLiteral("threadId"), error);
    const auto turnId = requiredString(object, QStringLiteral("turnId"), error);
    const QJsonValue itemValue = object.value(QStringLiteral("item"));
    if (!threadId.has_value() || !turnId.has_value() || !itemValue.isObject()) {
        if (!itemValue.isObject()) {
            setError(error, QStringLiteral("Missing or invalid item field"));
        }
        return std::nullopt;
    }
    const QJsonObject item = itemValue.toObject();
    const auto itemId = requiredString(item, QStringLiteral("id"), error);
    const auto itemType = requiredString(item, QStringLiteral("type"), error);
    if (!itemId.has_value() || !itemType.has_value()) {
        return std::nullopt;
    }
    QString text;
    if (*itemType == QLatin1String("agentMessage") || *itemType == QLatin1String("plan")) {
        const auto parsedText = requiredString(item, QStringLiteral("text"), error, true);
        if (!parsedText.has_value()) {
            return std::nullopt;
        }
        text = *parsedText;
    }
    return CodexItemNotification{.threadId = *threadId,
                                 .turnId = *turnId,
                                 .itemId = *itemId,
                                 .itemType = *itemType,
                                 .text = text,
                                 .rawItem = item};
}

std::optional<CodexAgentMessageDelta> parseAgentMessageDelta(const QJsonValue& params,
                                                             QString* error) {
    if (!params.isObject()) {
        setError(error, QStringLiteral("Agent message delta params must be an object"));
        return std::nullopt;
    }
    const QJsonObject object = params.toObject();
    const auto threadId = requiredString(object, QStringLiteral("threadId"), error);
    const auto turnId = requiredString(object, QStringLiteral("turnId"), error);
    const auto itemId = requiredString(object, QStringLiteral("itemId"), error);
    const auto delta = requiredString(object, QStringLiteral("delta"), error, true);
    if (!threadId.has_value() || !turnId.has_value() || !itemId.has_value() || !delta.has_value()) {
        return std::nullopt;
    }
    return CodexAgentMessageDelta{
        .threadId = *threadId, .turnId = *turnId, .itemId = *itemId, .delta = *delta};
}

std::optional<CodexTurnErrorNotification> parseTurnErrorNotification(const QJsonValue& params,
                                                                     QString* error) {
    if (!params.isObject()) {
        setError(error, QStringLiteral("Error notification params must be an object"));
        return std::nullopt;
    }
    const QJsonObject object = params.toObject();
    const auto threadId = requiredString(object, QStringLiteral("threadId"), error);
    const auto turnId = requiredString(object, QStringLiteral("turnId"), error);
    const QJsonValue errorValue = object.value(QStringLiteral("error"));
    if (!threadId.has_value() || !turnId.has_value() || !errorValue.isObject() ||
        !object.value(QStringLiteral("willRetry")).isBool()) {
        if (!errorValue.isObject()) {
            setError(error, QStringLiteral("Missing or invalid error field"));
        } else if (!object.value(QStringLiteral("willRetry")).isBool()) {
            setError(error, QStringLiteral("Missing or invalid willRetry field"));
        }
        return std::nullopt;
    }
    const auto message =
        requiredString(errorValue.toObject(), QStringLiteral("message"), error, true);
    if (!message.has_value()) {
        return std::nullopt;
    }
    return CodexTurnErrorNotification{.threadId = *threadId,
                                      .turnId = *turnId,
                                      .message = *message,
                                      .willRetry =
                                          object.value(QStringLiteral("willRetry")).toBool()};
}

} // namespace snack::agent::codex
