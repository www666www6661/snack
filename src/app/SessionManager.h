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
    [[nodiscard]] session::SessionController* create(const QString& workingDirectory,
                                                     domain::AgentKind requestedKind,
                                                     const QString& placeholderTitle,
                                                     QString* error = nullptr);
    bool setArchived(const QUuid& conversationId, bool archived, QString* error = nullptr);
    bool setPinned(const QUuid& conversationId, bool pinned, QString* error = nullptr);
    bool setTags(const QUuid& conversationId, const QStringList& tags, QString* error = nullptr);
    bool setGroup(const QUuid& conversationId, const QString& groupName, QString* error = nullptr);
    bool deleteConversation(const QUuid& conversationId, QString* error = nullptr);
    [[nodiscard]] session::SessionController* restore(const QUuid& conversationId,
                                                      QString* error = nullptr);
    [[nodiscard]] QList<domain::Conversation> catalog(QString* error = nullptr) const;
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
    storage::IEventRepository* repository_{nullptr};
    session::SessionRuntimeRegistry registry_;
};

} // namespace snack::app
