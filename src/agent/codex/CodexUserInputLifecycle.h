#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>

#include <optional>

namespace snack::agent::codex {

struct CodexUserInputRequest {
    QJsonValue nativeRequestId;
    QString threadId;
    QString turnId;
    QString itemId;
    bool isBlocking{false};
    QJsonArray questions;
};

[[nodiscard]] std::optional<CodexUserInputRequest>
parseUserInputRequest(const QJsonValue& id, const QJsonValue& params, QString* error = nullptr);
[[nodiscard]] QJsonObject userInputEventPayload(const QString& requestId,
                                                const CodexUserInputRequest& request);
[[nodiscard]] bool validateUserInputAnswers(const CodexUserInputRequest& request,
                                            const QJsonObject& answers, QString* error = nullptr);
[[nodiscard]] QJsonObject userInputResponse(const QJsonObject& answers);
[[nodiscard]] std::optional<QJsonObject> parseThreadTokenUsage(const QJsonValue& params,
                                                               QString* threadId, QString* turnId,
                                                               QString* error = nullptr);

} // namespace snack::agent::codex
