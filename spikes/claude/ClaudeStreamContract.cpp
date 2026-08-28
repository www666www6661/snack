#include "ClaudeStreamContract.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>

namespace snack::spike::claude {

StreamRecord parseStreamRecord(const QByteArray& line) {
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(line, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return {.error = parseError.error == QJsonParseError::NoError
                             ? QStringLiteral("Claude stream record is not an object")
                             : parseError.errorString()};
    }

    const QJsonObject payload = document.object();
    const QString type = payload.value(QStringLiteral("type")).toString();
    if (type == QLatin1String("system")) {
        const bool isInit = payload.value(QStringLiteral("subtype")) == QLatin1String("init");
        return {.kind = isInit ? StreamRecordKind::SystemInit : StreamRecordKind::SystemEvent,
                .payload = payload};
    }
    if (type == QLatin1String("user")) {
        return {.kind = StreamRecordKind::User, .payload = payload};
    }
    if (type == QLatin1String("assistant")) {
        return {.kind = StreamRecordKind::Assistant, .payload = payload};
    }
    if (type == QLatin1String("stream_event")) {
        return {.kind = StreamRecordKind::PartialAssistant, .payload = payload};
    }
    if (type == QLatin1String("result")) {
        return {.kind = StreamRecordKind::Result, .payload = payload};
    }
    return {.kind = StreamRecordKind::Unknown, .payload = payload};
}

bool containsImage(const StreamRecord& record) {
    if (record.kind != StreamRecordKind::User) {
        return false;
    }
    const QJsonValue content =
        record.payload.value(QStringLiteral("message")).toObject().value(QStringLiteral("content"));
    if (!content.isArray()) {
        return false;
    }
    for (const QJsonValue& block : content.toArray()) {
        if (block.toObject().value(QStringLiteral("type")) == QLatin1String("image")) {
            return true;
        }
    }
    return false;
}

bool StreamContractState::consume(const StreamRecord& record) {
    if (record.kind == StreamRecordKind::Malformed) {
        return false;
    }
    const QString recordSessionId = record.payload.value(QStringLiteral("session_id")).toString();
    if (!recordSessionId.isEmpty()) {
        if (!sessionId_.isEmpty() && sessionId_ != recordSessionId) {
            return false;
        }
        sessionId_ = recordSessionId;
    }

    if (record.kind == StreamRecordKind::SystemInit) {
        capabilities_.clear();
        for (const QJsonValue& capability :
             record.payload.value(QStringLiteral("capabilities")).toArray()) {
            capabilities_.append(capability.toString());
        }
    } else if (record.kind == StreamRecordKind::User) {
        const QString uuid = record.payload.value(QStringLiteral("uuid")).toString();
        if (!uuid.isEmpty() && !pendingUserMessages_.contains(uuid) &&
            !completedUserMessages_.contains(uuid)) {
            pendingUserMessages_.append(uuid);
        }
    } else if (record.kind == StreamRecordKind::Result) {
        QString userMessageUuid =
            record.payload.value(QStringLiteral("user_message_uuid")).toString();
        if (userMessageUuid.isEmpty() && !pendingUserMessages_.isEmpty()) {
            userMessageUuid = pendingUserMessages_.constFirst();
        }
        if (!userMessageUuid.isEmpty()) {
            pendingUserMessages_.removeAll(userMessageUuid);
            if (!completedUserMessages_.contains(userMessageUuid)) {
                completedUserMessages_.append(userMessageUuid);
            }
        }
    }
    return true;
}

QString StreamContractState::sessionId() const { return sessionId_; }

QStringList StreamContractState::capabilities() const { return capabilities_; }

QStringList StreamContractState::pendingUserMessages() const { return pendingUserMessages_; }

QStringList StreamContractState::completedUserMessages() const { return completedUserMessages_; }

} // namespace snack::spike::claude
