#pragma once

#include "agent/IAgentAdapter.h"
#include "agent/codex/CodexAppServerClient.h"
#include "agent/codex/CodexApprovalLifecycle.h"
#include "agent/codex/CodexCliDiscovery.h"
#include "agent/codex/CodexModelCatalog.h"
#include "agent/codex/CodexThreadLifecycle.h"
#include "agent/codex/CodexTurnLifecycle.h"
#include "agent/codex/CodexUserInputLifecycle.h"

#include <QHash>
#include <QSet>

namespace snack::agent::codex {

class CodexAdapter final : public IAgentAdapter {
    Q_OBJECT

  public:
    CodexAdapter(CliInstallation installation, process::IProcessTransport* transport,
                 QObject* parent = nullptr,
                 int requestTimeoutMs = CodexAppServerClient::defaultRequestTimeoutMs);

    [[nodiscard]] domain::AgentKind kind() const override;
    [[nodiscard]] CapabilitySet capabilities() const override;
    void connectAgent(const AgentConnectionRequest& request) override;
    void startTurn(const TurnRequest& request) override;
    bool steerTurn(const SteerRequest& request) override;
    bool respondToApproval(const QString& requestId, domain::ApprovalDecision decision) override;
    bool respondToUserInput(const QString& requestId, const QJsonObject& answers) override;
    bool requestNativeThreadPage(const QString& cursor = {});
    bool requestNativeThread(const QString& threadId, bool includeTurns = true);
    void interruptTurn() override;
    void closeAgent() override;

  signals:
    void nativeThreadPageReceived(const snack::agent::codex::CodexThreadPage& page);
    void nativeThreadReceived(const snack::agent::codex::CodexThreadInfo& thread);
    void nativeThreadQueryFailed(const QString& method, const QString& detail);

  private:
    void requestModelPage(const QString& cursor = {});
    void handleResponse(qint64 id, const QString& method, const QJsonValue& result);
    void handleRequestFailure(qint64 id, const QString& method, int code, const QString& message);
    void appendModels(const QList<CodexModelInfo>& models);
    void finishModelDiscovery();
    void requestThreadLifecycle();
    void finishThreadLifecycle(const QJsonValue& result);
    void finishTurnStart(const QJsonValue& result);
    void handleNotification(const QString& method, const QJsonValue& params,
                            const QJsonObject& raw);
    void handleServerRequest(const QJsonValue& id, const QString& method, const QJsonValue& params,
                             const QJsonObject& raw);
    void handleServerRequestResolved(const QJsonValue& params, const QJsonObject& raw);
    [[nodiscard]] bool acceptNativeContext(const QString& threadId, const QString& turnId,
                                           const QJsonObject& raw);
    void sendInterruptRequest();
    [[nodiscard]] bool sendSteerRequest(const SteerRequest& request);
    void sendDeferredSteer();
    void emitActiveEvent(domain::AgentEventType type, const QJsonObject& payload = {},
                         const QJsonObject& raw = {});
    void warnActive(const QString& message, const QJsonObject& raw = {});
    void finishActiveTurn(domain::AgentEventType type, const QString& message,
                          const QString& nativeStatus, const QJsonObject& raw, bool interrupted);
    void failConnection(const QString& detail);

    CliInstallation installation_;
    CodexAppServerClient client_;
    CapabilitySet capabilities_;
    QList<CodexModelInfo> models_;
    QSet<QString> requestedCursors_;
    AgentConnectionRequest connectionRequest_;
    QString nativeThreadId_;
    QString nativeSessionId_;
    TurnRequest activeTurn_;
    SteerRequest deferredSteer_;
    QString nativeTurnId_;
    QSet<QString> startedAgentMessages_;
    QSet<QString> completedAgentMessages_;
    QHash<QString, QString> streamedAgentText_;
    QSet<QString> startedToolItems_;
    QSet<QString> completedToolItems_;
    QSet<QString> startedReasoningItems_;
    QSet<QString> completedReasoningItems_;
    QSet<QString> completedPlanItems_;
    QHash<QString, QStringList> reasoningSummaries_;
    QHash<QString, QJsonObject> activeItems_;
    QHash<QString, CodexApprovalRequest> pendingApprovals_;
    QHash<QString, QString> approvalTokenByNativeKey_;
    QHash<QString, CodexUserInputRequest> pendingUserInputs_;
    QHash<QString, QString> userInputTokenByNativeKey_;
    QString threadRequestMethod_;
    qint64 modelRequestId_{0};
    qint64 threadRequestId_{0};
    qint64 threadListRequestId_{0};
    qint64 threadReadRequestId_{0};
    qint64 turnRequestId_{0};
    qint64 steerRequestId_{0};
    qint64 interruptRequestId_{0};
    bool turnStartedEmitted_{false};
    bool interruptRequested_{false};
    bool interruptSent_{false};
    bool connecting_{false};
    bool connected_{false};
    bool closing_{false};
};

} // namespace snack::agent::codex
