#pragma once

#include "agent/IAgentAdapter.h"

#include <QTimer>

namespace snack::agent {

class FakeAgentAdapter final : public IAgentAdapter {
    Q_OBJECT

  public:
    explicit FakeAgentAdapter(QObject* parent = nullptr, int chunkDelayMs = 35);

    [[nodiscard]] domain::AgentKind kind() const override;
    [[nodiscard]] CapabilitySet capabilities() const override;
    [[nodiscard]] AgentConnectionRequest lastConnectionRequest() const;
    [[nodiscard]] QString lastApprovalRequestId() const;
    [[nodiscard]] domain::ApprovalDecision lastApprovalDecision() const;
    [[nodiscard]] QString lastUserInputRequestId() const;
    [[nodiscard]] QJsonObject lastUserInputAnswers() const;
    [[nodiscard]] SteerRequest lastSteerRequest() const;
    void connectAgent(const AgentConnectionRequest& request) override;
    void startTurn(const TurnRequest& request) override;
    bool steerTurn(const SteerRequest& request) override;
    bool respondToApproval(const QString& requestId, domain::ApprovalDecision decision) override;
    bool respondToUserInput(const QString& requestId, const QJsonObject& answers) override;
    void interruptTurn() override;
    void closeAgent() override;

  private slots:
    void emitNextChunk();

  private:
    void emitEvent(domain::AgentEventType type, const QJsonObject& payload = {});

    QTimer timer_;
    int chunkDelayMs_{35};
    TurnRequest activeRequest_;
    QStringList chunks_;
    qsizetype chunkIndex_{0};
    bool connected_{false};
    AgentConnectionRequest lastConnectionRequest_;
    QString lastApprovalRequestId_;
    domain::ApprovalDecision lastApprovalDecision_{domain::ApprovalDecision::Decline};
    QString lastUserInputRequestId_;
    QJsonObject lastUserInputAnswers_;
    SteerRequest lastSteerRequest_;
};

} // namespace snack::agent
