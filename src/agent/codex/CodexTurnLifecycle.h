#pragma once

#include "agent/IAgentAdapter.h"

#include <QJsonObject>
#include <QJsonValue>
#include <optional>

namespace snack::agent::codex {

struct CodexTurnInfo {
    QString id;
    QString status;
    QString errorMessage;
    QJsonObject raw;
};

struct CodexTurnNotification {
    QString threadId;
    CodexTurnInfo turn;
};

struct CodexItemNotification {
    QString threadId;
    QString turnId;
    QString itemId;
    QString itemType;
    QString text;
    QJsonObject rawItem;
};

struct CodexAgentMessageDelta {
    QString threadId;
    QString turnId;
    QString itemId;
    QString delta;
};

struct CodexTurnErrorNotification {
    QString threadId;
    QString turnId;
    QString message;
    bool willRetry{false};
};

[[nodiscard]] QJsonObject turnAccessParameters(domain::AccessLevel accessLevel);
[[nodiscard]] QJsonObject makeTurnStartParameters(const QString& threadId, const QString& cwd,
                                                  const TurnRequest& request);
[[nodiscard]] QJsonObject makeTurnInterruptParameters(const QString& threadId,
                                                      const QString& turnId);

[[nodiscard]] std::optional<CodexTurnInfo> parseTurnStartResponse(const QJsonValue& result,
                                                                  QString* error = nullptr);
[[nodiscard]] std::optional<CodexTurnNotification> parseTurnNotification(const QJsonValue& params,
                                                                         QString* error = nullptr);
[[nodiscard]] std::optional<CodexItemNotification> parseItemNotification(const QJsonValue& params,
                                                                         QString* error = nullptr);
[[nodiscard]] std::optional<CodexAgentMessageDelta>
parseAgentMessageDelta(const QJsonValue& params, QString* error = nullptr);
[[nodiscard]] std::optional<CodexTurnErrorNotification>
parseTurnErrorNotification(const QJsonValue& params, QString* error = nullptr);

} // namespace snack::agent::codex
