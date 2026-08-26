#pragma once

#include "domain/DomainTypes.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>

#include <optional>

namespace snack::agent::codex {

enum class CodexApprovalKind { CommandExecution, FileChange };

struct CodexApprovalRequest {
    QJsonValue nativeRequestId;
    CodexApprovalKind kind{CodexApprovalKind::CommandExecution};
    QString threadId;
    QString turnId;
    QString itemId;
    QString reason;
    QString command;
    QString cwd;
    QString grantRoot;
    QJsonArray commandActions;
    QJsonObject networkApprovalContext;
    QJsonArray availableDecisions;
    QJsonObject rawParams;
};

[[nodiscard]] std::optional<CodexApprovalRequest> parseApprovalRequest(const QJsonValue& id,
                                                                       const QString& method,
                                                                       const QJsonValue& params,
                                                                       QString* error = nullptr);
[[nodiscard]] QString nativeRequestKey(const QJsonValue& id);
[[nodiscard]] QJsonObject approvalEventPayload(const QString& requestId,
                                               const CodexApprovalRequest& request);
[[nodiscard]] QJsonObject approvalResponse(domain::ApprovalDecision decision);

} // namespace snack::agent::codex
