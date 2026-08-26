#pragma once

#include <QJsonObject>
#include <QJsonValue>
#include <QString>

namespace snack::agent::codex {

enum class MessageKind { Request, Response, Notification, Invalid };

struct ProtocolMessage {
    MessageKind kind{MessageKind::Invalid};
    QJsonValue id;
    QString method;
    QJsonValue params;
    QJsonValue result;
    QJsonObject error;
    QJsonObject raw;
    QString errorDetail;
};

[[nodiscard]] ProtocolMessage parseMessage(const QByteArray& line);
[[nodiscard]] QByteArray encodeRequest(qint64 id, const QString& method, const QJsonObject& params);
[[nodiscard]] QByteArray encodeNotification(const QString& method, const QJsonObject& params);
[[nodiscard]] QByteArray encodeResponse(const QJsonValue& id, const QJsonValue& result);
[[nodiscard]] QByteArray encodeErrorResponse(const QJsonValue& id, int code,
                                             const QString& message);

} // namespace snack::agent::codex
