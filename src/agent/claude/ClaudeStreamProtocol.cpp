#include "agent/claude/ClaudeStreamProtocol.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSet>

namespace snack::agent::claude {
namespace {

bool readStringArray(const QJsonObject& object, const QString& field, QStringList* result,
                     QString* error) {
    const QJsonValue value = object.value(field);
    if (value.isUndefined()) {
        return true;
    }
    if (!value.isArray()) {
        if (error != nullptr) {
            *error = QStringLiteral("Claude init field %1 must be an array").arg(field);
        }
        return false;
    }

    QSet<QString> observed;
    for (const QJsonValue& item : value.toArray()) {
        if (!item.isString() || item.toString().isEmpty()) {
            if (error != nullptr) {
                *error =
                    QStringLiteral("Claude init field %1 contains an invalid value").arg(field);
            }
            return false;
        }
        if (!observed.contains(item.toString())) {
            observed.insert(item.toString());
            result->append(item.toString());
        }
    }
    return true;
}

} // namespace

StreamRecord parseStreamRecord(const QByteArray& line) {
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(line, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return {.error = parseError.error == QJsonParseError::NoError
                             ? QStringLiteral("Claude stream record is not an object")
                             : parseError.errorString()};
    }

    const QJsonObject payload = document.object();
    const QJsonValue typeValue = payload.value(QStringLiteral("type"));
    if (!typeValue.isString() || typeValue.toString().isEmpty()) {
        return {.payload = payload,
                .error = QStringLiteral("Claude stream record type is missing")};
    }

    const QString type = typeValue.toString();
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

std::optional<InitInfo> parseInitInfo(const StreamRecord& record, QString* error) {
    if (record.kind != StreamRecordKind::SystemInit) {
        if (error != nullptr) {
            *error = QStringLiteral("Claude stream record is not system/init");
        }
        return std::nullopt;
    }

    const QJsonObject& payload = record.payload;
    const QStringList requiredStrings = {
        QStringLiteral("session_id"), QStringLiteral("claude_code_version"), QStringLiteral("cwd"),
        QStringLiteral("model"),      QStringLiteral("permissionMode"),
    };
    for (const QString& field : requiredStrings) {
        if (!payload.value(field).isString() || payload.value(field).toString().isEmpty()) {
            if (error != nullptr) {
                *error = QStringLiteral("Claude init field %1 is missing").arg(field);
            }
            return std::nullopt;
        }
    }

    InitInfo info{
        .sessionId = payload.value(QStringLiteral("session_id")).toString(),
        .cliVersion = payload.value(QStringLiteral("claude_code_version")).toString(),
        .workingDirectory = payload.value(QStringLiteral("cwd")).toString(),
        .modelId = payload.value(QStringLiteral("model")).toString(),
        .permissionMode = payload.value(QStringLiteral("permissionMode")).toString(),
    };
    if (!readStringArray(payload, QStringLiteral("tools"), &info.tools, error) ||
        !readStringArray(payload, QStringLiteral("capabilities"), &info.capabilities, error)) {
        return std::nullopt;
    }
    return info;
}

} // namespace snack::agent::claude
