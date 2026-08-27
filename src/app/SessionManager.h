#pragma once

#include "session/SessionRuntimeRegistry.h"

#include <functional>

namespace snack::app {

class SessionManager final {
  public:
    using RuntimeFactory = std::function<agent::AgentRuntime(domain::AgentKind)>;

    SessionManager(storage::IEventRepository* repository, RuntimeFactory runtimeFactory);

    SessionManager(const SessionManager&) = delete;
    SessionManager& operator=(const SessionManager&) = delete;

    [[nodiscard]] session::SessionController* addPrepared(domain::Conversation conversation,
                                                          agent::AgentRuntime runtime,
                                                          QString* error = nullptr);
    [[nodiscard]] session::SessionController* open(domain::Conversation conversation,
                                                   QString* error = nullptr);
    bool close(const QUuid& conversationId);
    void closeAll();

    [[nodiscard]] session::SessionController* controller(const QUuid& conversationId) const;
    [[nodiscard]] agent::AgentRuntime* runtime(const QUuid& conversationId) const;
    [[nodiscard]] QList<QUuid> conversationIds() const;
    [[nodiscard]] qsizetype size() const;

  private:
    static void setError(QString* error, const QString& message);
    [[nodiscard]] session::SessionController*
    existingCompatibleController(const domain::Conversation& conversation, QString* error) const;

    RuntimeFactory runtimeFactory_;
    session::SessionRuntimeRegistry registry_;
};

} // namespace snack::app
