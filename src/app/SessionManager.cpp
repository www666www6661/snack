#include "app/SessionManager.h"

#include <utility>

namespace snack::app {

SessionManager::SessionManager(storage::IEventRepository* repository, RuntimeFactory runtimeFactory)
    : runtimeFactory_(std::move(runtimeFactory)), registry_(repository) {
    Q_ASSERT(runtimeFactory_);
}

session::SessionController* SessionManager::addPrepared(domain::Conversation conversation,
                                                        agent::AgentRuntime runtime,
                                                        QString* error) {
    const QUuid conversationId = conversation.id;
    if (!registry_.add(std::move(conversation), std::move(runtime), error)) {
        return nullptr;
    }
    return registry_.controller(conversationId);
}

session::SessionController* SessionManager::open(domain::Conversation conversation,
                                                 QString* error) {
    if (conversation.id.isNull()) {
        setError(error, QStringLiteral("Cannot open a session with an invalid conversation ID"));
        return nullptr;
    }
    if (auto* existing = registry_.controller(conversation.id); existing != nullptr) {
        return existingCompatibleController(conversation, error);
    }

    agent::AgentRuntime runtime = runtimeFactory_(conversation.agentKind);
    if (runtime.adapter == nullptr || runtime.selectedKind != conversation.agentKind) {
        setError(error, runtime.detail.isEmpty()
                            ? QStringLiteral("The conversation Agent runtime is unavailable")
                            : runtime.detail);
        return nullptr;
    }
    conversation.status = domain::ConversationStatus::Dormant;
    return addPrepared(std::move(conversation), std::move(runtime), error);
}

session::SessionController* SessionManager::create(const QString& workingDirectory,
                                                   domain::AgentKind requestedKind,
                                                   const QString& placeholderTitle,
                                                   QString* error) {
    if (workingDirectory.trimmed().isEmpty()) {
        setError(error, QStringLiteral("Cannot create a session without a working directory"));
        return nullptr;
    }

    agent::AgentRuntime runtime = runtimeFactory_(requestedKind);
    if (runtime.adapter == nullptr) {
        setError(error, runtime.detail.isEmpty()
                            ? QStringLiteral("The requested Agent runtime is unavailable")
                            : runtime.detail);
        return nullptr;
    }

    domain::Conversation conversation;
    conversation.title = placeholderTitle.trimmed();
    if (conversation.title.isEmpty()) {
        conversation.title = QStringLiteral("New conversation");
    }
    conversation.titleIsPlaceholder = true;
    conversation.workingDirectory = workingDirectory;
    conversation.agentKind = runtime.selectedKind;
    conversation.status = domain::ConversationStatus::Dormant;
    return addPrepared(std::move(conversation), std::move(runtime), error);
}

bool SessionManager::close(const QUuid& conversationId) { return registry_.close(conversationId); }

void SessionManager::closeAll() { registry_.closeAll(); }

session::SessionController* SessionManager::controller(const QUuid& conversationId) const {
    return registry_.controller(conversationId);
}

agent::AgentRuntime* SessionManager::runtime(const QUuid& conversationId) const {
    return registry_.runtime(conversationId);
}

QList<QUuid> SessionManager::conversationIds() const { return registry_.conversationIds(); }

qsizetype SessionManager::size() const { return registry_.size(); }

void SessionManager::setError(QString* error, const QString& message) {
    if (error != nullptr) {
        *error = message;
    }
}

session::SessionController*
SessionManager::existingCompatibleController(const domain::Conversation& conversation,
                                             QString* error) const {
    auto* existing = registry_.controller(conversation.id);
    Q_ASSERT(existing != nullptr);
    if (existing->conversation().agentKind != conversation.agentKind) {
        setError(error, QStringLiteral("The open session uses a different Agent type"));
        return nullptr;
    }
    return existing;
}

} // namespace snack::app
