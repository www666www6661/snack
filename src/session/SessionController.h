#pragma once

#include "agent/IAgentAdapter.h"
#include "storage/EventRepository.h"

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
    [[nodiscard]] QList<domain::AgentEvent> restoredEvents(QString* error = nullptr);

    void open();
    bool sendMessage(const QString& message, QString* error = nullptr);
    void interrupt();
    void close();
    void setNextTurnSettings(const domain::TurnSettingsSnapshot& settings);

  signals:
    void statusChanged(snack::domain::ConversationStatus status);
    void eventRecorded(const snack::domain::AgentEvent& event);
    void persistenceError(const QString& error);
    void nextTurnSettingsChanged(const snack::domain::TurnSettingsSnapshot& settings);
    void nativeIdentityChanged(const QString& threadId, const QString& sessionId);

  private:
    void handleCapabilitiesChanged(const agent::CapabilitySet& capabilities);
    void handleNativeIdentityChanged(const QString& threadId, const QString& sessionId);
    void handleAdapterEvent(domain::AgentEvent event);
    void recordEvent(domain::AgentEvent event);
    void setStatus(domain::ConversationStatus status);

    domain::Conversation conversation_;
    agent::IAgentAdapter* adapter_{nullptr};
    storage::IEventRepository* repository_{nullptr};
    domain::TurnSettingsSnapshot nextTurnSettings_;
    QUuid activeTurnId_;
    quint64 nextSequence_{1};
};

} // namespace snack::session
