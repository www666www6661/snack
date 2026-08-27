#pragma once

#include "agent/AgentRuntime.h"
#include "session/SessionController.h"

#include <memory>
#include <vector>

namespace snack::session {

class SessionRuntimeRegistry final {
  public:
    explicit SessionRuntimeRegistry(storage::IEventRepository* repository);
    ~SessionRuntimeRegistry();

    SessionRuntimeRegistry(const SessionRuntimeRegistry&) = delete;
    SessionRuntimeRegistry& operator=(const SessionRuntimeRegistry&) = delete;

    bool add(domain::Conversation conversation, agent::AgentRuntime runtime,
             QString* error = nullptr);
    bool close(const QUuid& conversationId);
    void closeAll();

    [[nodiscard]] SessionController* controller(const QUuid& conversationId) const;
    [[nodiscard]] agent::AgentRuntime* runtime(const QUuid& conversationId) const;
    [[nodiscard]] QList<QUuid> conversationIds() const;
    [[nodiscard]] qsizetype size() const;

  private:
    struct Entry {
        agent::AgentRuntime agentRuntime;
        std::unique_ptr<SessionController> sessionController;
    };
    using Entries = std::vector<std::unique_ptr<Entry>>;

    [[nodiscard]] Entries::iterator find(const QUuid& conversationId);
    [[nodiscard]] Entries::const_iterator find(const QUuid& conversationId) const;

    storage::IEventRepository* repository_{nullptr};
    Entries entries_;
};

} // namespace snack::session
