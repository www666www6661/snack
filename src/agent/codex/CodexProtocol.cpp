#include "agent/codex/CodexProtocol.h"

#include <QJsonDocument>
#include <QJsonParseError>

namespace snack::agent::codex {

ProtocolMessage parseMessage(const QByteArray& line) {
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(line, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return {.errorDetail = parseError.error == QJsonParseError::NoError
                                   ? QStringLiteral("JSON-RPC message must be an object")
                                   : parseError.errorString()};
    }

    const QJsonObject object = document.object();
    ProtocolMessage message;
    message.raw = object;
    const bool hasId = object.contains(QStringLiteral("id"));
    const bool containsMethod = object.contains(QStringLiteral("method"));
    const bool hasMethod = object.value(QStringLiteral("method")).isString();
    const bool hasResult = object.contains(QStringLiteral("result"));
    const bool hasError = object.value(QStringLiteral("error")).isObject();

    if (containsMethod && !hasMethod) {
        message.errorDetail = QStringLiteral("JSON-RPC method must be a string");
        return message;
    }

    if (hasMethod) {
        message.id = object.value(QStringLiteral("id"));
        message.method = object.value(QStringLiteral("method")).toString();
        message.params = object.value(QStringLiteral("params"));
        message.kind = hasId ? MessageKind::Request : MessageKind::Notification;
        return message;
    }

    if (hasId && hasResult != hasError) {
        message.id = object.value(QStringLiteral("id"));
        message.result = object.value(QStringLiteral("result"));
        message.error = object.value(QStringLiteral("error")).toObject();
        message.kind = MessageKind::Response;
        return message;
    }

    message.errorDetail = QStringLiteral("invalid JSON-RPC envelope");
    return message;
}

QByteArray encodeRequest(qint64 id, const QString& method, const QJsonObject& params) {
    return QJsonDocument(QJsonObject{{QStringLiteral("method"), method},
                                     {QStringLiteral("id"), id},
                                     {QStringLiteral("params"), params}})
        .toJson(QJsonDocument::Compact);
}

QByteArray encodeNotification(const QString& method, const QJsonObject& params) {
    return QJsonDocument(
               QJsonObject{{QStringLiteral("method"), method}, {QStringLiteral("params"), params}})
        .toJson(QJsonDocument::Compact);
}

QByteArray encodeErrorResponse(const QJsonValue& id, int code, const QString& message) {
    return QJsonDocument(QJsonObject{{QStringLiteral("id"), id},
                                     {QStringLiteral("error"),
                                      QJsonObject{{QStringLiteral("code"), code},
                                                  {QStringLiteral("message"), message}}}})
        .toJson(QJsonDocument::Compact);
}

} // namespace snack::agent::codex
