#pragma once

#include "agent/IAgentAdapter.h"
#include "storage/EventRepository.h"

#include <QHash>
#include <QObject>

namespace snack::session {

class SessionController final : public QObject {
    Q_OBJECT

  public:
    SessionController(domain::Conversation conversation, agent::IAgentAdapter* adapter,
                      storage::IEventRepository* repository, QObject* parent = nullptr);

    [[nodiscard]] const domain::Conversation& conversation() const;
    [[nodiscard]] domain::ConversationStatus status() const;
    [[nodiscard]] domain::TurnSettingsSnapshot nextTurnSettings() const;
    [[nodiscard]] const agent::CapabilitySet& capabilities() const;
    [[nodiscard]] QString connectionDetail() const;
    [[nodiscard]] qsizetype pendingApprovalCount() const;
    [[nodiscard]] qsizetype pendingInputCount() const;
    [[nodiscard]] QList<domain::AgentEvent> restoredEvents(QString* error = nullptr);

    void open();
    bool sendMessage(const QString& message, QString* error = nullptr);
    bool steerMessage(const QString& message, QString* error = nullptr);
    bool respondToApproval(const QString& requestId, domain::ApprovalDecision decision,
                           QString* error = nullptr);
    bool respondToUserInput(const QString& requestId, const QJsonObject& answers,
                            QString* error = nullptr);
    void interrupt();
    void close();
    void setNextTurnSettings(const domain::TurnSettingsSnapshot& settings);

  signals:
    void statusChanged(snack::domain::ConversationStatus status);
    void eventRecorded(const snack::domain::AgentEvent& event);
    void persistenceError(const QString& error);
    void nextTurnSettingsChanged(const snack::domain::TurnSettingsSnapshot& settings);
    void capabilitiesChanged(const snack::agent::CapabilitySet& capabilities);
    void connectionDetailChanged(const QString& detail);
    void nativeIdentityChanged(const QString& threadId, const QString& sessionId);

  private:
    void handleCapabilitiesChanged(const agent::CapabilitySet& capabilities);
    void handleNativeIdentityChanged(const QString& threadId, const QString& sessionId);
    void handleAdapterEvent(domain::AgentEvent event);
    [[nodiscard]] domain::TurnSettingsSnapshot
    normalizeSettings(const domain::TurnSettingsSnapshot& settings) const;
    void recordEvent(domain::AgentEvent event);
    void recomputeActiveStatus();
    void setStatus(domain::ConversationStatus status);

    domain::Conversation conversation_;
    agent::IAgentAdapter* adapter_{nullptr};
    storage::IEventRepository* repository_{nullptr};
    domain::TurnSettingsSnapshot nextTurnSettings_;
    agent::CapabilitySet capabilities_;
    QString connectionDetail_;
    QUuid activeTurnId_;
    domain::TurnSettingsSnapshot activeTurnSettings_;
    QHash<QString, QJsonObject> pendingApprovals_;
    QHash<QString, QJsonObject> pendingUserInputs_;
    quint64 nextSequence_{1};
};

} // namespace snack::session
