#include "session/SessionRuntimeRegistry.h"

#include <algorithm>

namespace snack::session {

SessionRuntimeRegistry::SessionRuntimeRegistry(storage::IEventRepository* repository)
    : repository_(repository) {
    Q_ASSERT(repository_ != nullptr);
}

SessionRuntimeRegistry::~SessionRuntimeRegistry() { closeAll(); }

bool SessionRuntimeRegistry::add(domain::Conversation conversation, agent::AgentRuntime runtime,
                                 QString* error) {
    if (conversation.id.isNull()) {
        if (error != nullptr) {
            *error = QStringLiteral("Cannot add a session with an invalid conversation ID");
        }
        return false;
    }
    if (find(conversation.id) != entries_.end()) {
        if (error != nullptr) {
            *error = QStringLiteral("Conversation session already exists");
        }
        return false;
    }
    if (runtime.adapter == nullptr || runtime.adapter->kind() != runtime.selectedKind ||
        conversation.agentKind != runtime.selectedKind) {
        if (error != nullptr) {
            *error = QStringLiteral("Conversation and Agent runtime do not match");
        }
        return false;
    }

    auto entry = std::make_unique<Entry>();
    entry->agentRuntime = std::move(runtime);
    entry->sessionController = std::make_unique<SessionController>(
        std::move(conversation), entry->agentRuntime.adapter.get(), repository_);
    entries_.push_back(std::move(entry));
    return true;
}

bool SessionRuntimeRegistry::close(const QUuid& conversationId) {
    const auto iterator = find(conversationId);
    if (iterator == entries_.end()) {
        return false;
    }
    std::unique_ptr<Entry> closing = std::move(*iterator);
    entries_.erase(iterator);
    closing->sessionController->close();
    return true;
}

void SessionRuntimeRegistry::closeAll() {
    Entries closing = std::move(entries_);
    entries_.clear();
    for (auto iterator = closing.rbegin(); iterator != closing.rend(); ++iterator) {
        (*iterator)->sessionController->close();
    }
}

SessionController* SessionRuntimeRegistry::controller(const QUuid& conversationId) const {
    const auto iterator = find(conversationId);
    return iterator == entries_.end() ? nullptr : (*iterator)->sessionController.get();
}

agent::AgentRuntime* SessionRuntimeRegistry::runtime(const QUuid& conversationId) const {
    const auto iterator = find(conversationId);
    return iterator == entries_.end() ? nullptr : &(*iterator)->agentRuntime;
}

QList<QUuid> SessionRuntimeRegistry::conversationIds() const {
    QList<QUuid> result;
    result.reserve(static_cast<qsizetype>(entries_.size()));
    for (const auto& entry : entries_) {
        result.append(entry->sessionController->conversation().id);
    }
    return result;
}

qsizetype SessionRuntimeRegistry::size() const { return static_cast<qsizetype>(entries_.size()); }

SessionRuntimeRegistry::Entries::iterator
SessionRuntimeRegistry::find(const QUuid& conversationId) {
    return std::find_if(entries_.begin(), entries_.end(), [&conversationId](const auto& entry) {
        return entry->sessionController->conversation().id == conversationId;
    });
}

SessionRuntimeRegistry::Entries::const_iterator
SessionRuntimeRegistry::find(const QUuid& conversationId) const {
    return std::find_if(entries_.cbegin(), entries_.cend(), [&conversationId](const auto& entry) {
        return entry->sessionController->conversation().id == conversationId;
    });
}

} // namespace snack::session
