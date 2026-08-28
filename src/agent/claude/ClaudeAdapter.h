#pragma once

#include "agent/IAgentAdapter.h"
#include "agent/claude/ClaudeCliDiscovery.h"
#include "agent/claude/ClaudeEventMapper.h"
#include "agent/claude/ClaudePermissionBridge.h"
#include "agent/claude/ClaudeStreamClient.h"

#include <QHash>

namespace snack::agent::claude {

class ClaudeAdapter final : public IAgentAdapter {
    Q_OBJECT

  public:
    ClaudeAdapter(CliInstallation installation, process::IProcessTransport* transport,
                  QObject* parent = nullptr, QString permissionHelperExecutable = {});

    [[nodiscard]] domain::AgentKind kind() const override;
    [[nodiscard]] CapabilitySet capabilities() const override;
    void connectAgent(const AgentConnectionRequest& request) override;
    void startTurn(const TurnRequest& request) override;
    bool steerTurn(const SteerRequest& request) override;
    bool respondToApproval(const QString& requestId, domain::ApprovalDecision decision) override;
    bool respondToUserInput(const QString& requestId, const QJsonObject& answers) override;
    void interruptTurn() override;
    void closeAgent() override;

  private:
    [[nodiscard]] QJsonObject makeUserEnvelope(const TurnRequest& request, QString* error) const;
    [[nodiscard]] QJsonObject makeUserInputEnvelope(const QString& requestId,
                                                    const QJsonObject& request,
                                                    const QJsonObject& answers) const;
    [[nodiscard]] bool launchSession(bool resume);
    void restartSession();
    void sendActiveTurn();
    void handleInitialized(const InitInfo& info);
    void handleRecord(const StreamRecord& record);
    void handlePermissionRequest(const QString& requestId, const QJsonObject& arguments);
    void finishActiveTurn(domain::AgentEventType type, const QString& message,
                          const QJsonObject& raw, bool interrupted, bool completed);
    void emitActiveEvent(domain::AgentEventType type, const QJsonObject& payload = {},
                         const QJsonObject& raw = {});
    void failConnection(const QString& detail);

    CliInstallation installation_;
    ClaudeStreamClient client_;
    ClaudeEventMapper eventMapper_;
    ClaudePermissionBridge permissionBridge_;
    CapabilitySet capabilities_;
    AgentConnectionRequest connectionRequest_;
    domain::TurnSettingsSnapshot processSettings_;
    TurnRequest activeTurn_;
    QJsonObject pendingTurnEnvelope_;
    QString expectedSessionId_;
    QString nativeUserMessageUuid_;
    QHash<QString, QJsonObject> pendingUserInputs_;
    QHash<QString, QJsonObject> pendingApprovals_;
    QString permissionHelperExecutable_;
    bool connecting_{false};
    bool connected_{false};
    bool closing_{false};
    bool reconnecting_{false};
    bool restartAfterStop_{false};
};

} // namespace snack::agent::claude
