#include "agent/codex/CodexApprovalLifecycle.h"

#include <QJsonDocument>

namespace snack::agent::codex {
namespace {

void setError(QString* error, const QString& message) {
    if (error != nullptr) {
        *error = message;
    }
}

bool validRequestId(const QJsonValue& id) {
    return (id.isString() && !id.toString().isEmpty()) || id.isDouble();
}

} // namespace

std::optional<CodexApprovalRequest> parseApprovalRequest(const QJsonValue& id,
                                                         const QString& method,
                                                         const QJsonValue& params, QString* error) {
    if (!validRequestId(id)) {
        setError(error, QStringLiteral("approval request id is invalid"));
        return std::nullopt;
    }
    if (!params.isObject()) {
        setError(error, QStringLiteral("approval params must be an object"));
        return std::nullopt;
    }

    CodexApprovalRequest request;
    request.nativeRequestId = id;
    if (method == QLatin1String("item/commandExecution/requestApproval")) {
        request.kind = CodexApprovalKind::CommandExecution;
    } else if (method == QLatin1String("item/fileChange/requestApproval")) {
        request.kind = CodexApprovalKind::FileChange;
    } else {
        setError(error, QStringLiteral("unsupported approval method"));
        return std::nullopt;
    }

    request.rawParams = params.toObject();
    request.threadId = request.rawParams.value(QStringLiteral("threadId")).toString();
    request.turnId = request.rawParams.value(QStringLiteral("turnId")).toString();
    request.itemId = request.rawParams.value(QStringLiteral("itemId")).toString();
    if (request.threadId.isEmpty() || request.turnId.isEmpty() || request.itemId.isEmpty()) {
        setError(error, QStringLiteral("approval routing fields are missing"));
        return std::nullopt;
    }

    request.reason = request.rawParams.value(QStringLiteral("reason")).toString();
    request.command = request.rawParams.value(QStringLiteral("command")).toString();
    request.cwd = request.rawParams.value(QStringLiteral("cwd")).toString();
    request.grantRoot = request.rawParams.value(QStringLiteral("grantRoot")).toString();
    request.commandActions = request.rawParams.value(QStringLiteral("commandActions")).toArray();
    request.networkApprovalContext =
        request.rawParams.value(QStringLiteral("networkApprovalContext")).toObject();
    request.availableDecisions =
        request.rawParams.value(QStringLiteral("availableDecisions")).toArray();
    return request;
}

QString nativeRequestKey(const QJsonValue& id) {
    return QString::fromUtf8(
        QJsonDocument(QJsonObject{{QStringLiteral("id"), id}}).toJson(QJsonDocument::Compact));
}

QJsonObject approvalEventPayload(const QString& requestId, const CodexApprovalRequest& request) {
    QJsonObject payload{{QStringLiteral("requestId"), requestId},
                        {QStringLiteral("kind"), request.kind == CodexApprovalKind::CommandExecution
                                                     ? QStringLiteral("commandExecution")
                                                     : QStringLiteral("fileChange")},
                        {QStringLiteral("itemId"), request.itemId},
                        {QStringLiteral("reason"), request.reason},
                        {QStringLiteral("command"), request.command},
                        {QStringLiteral("cwd"), request.cwd},
                        {QStringLiteral("grantRoot"), request.grantRoot},
                        {QStringLiteral("commandActions"), request.commandActions},
                        {QStringLiteral("networkApprovalContext"), request.networkApprovalContext},
                        {QStringLiteral("availableDecisions"), request.availableDecisions}};
    return payload;
}

QJsonObject approvalResponse(domain::ApprovalDecision decision) {
    return {{QStringLiteral("decision"), domain::enumName(decision)}};
}

} // namespace snack::agent::codex
