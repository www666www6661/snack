#include "app/SessionManager.h"

#include <utility>

namespace snack::app {

SessionManager::SessionManager(storage::IEventRepository* repository, RuntimeFactory runtimeFactory)
    : runtimeFactory_(std::move(runtimeFactory)), repository_(repository), registry_(repository) {
    Q_ASSERT(repository_ != nullptr);
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

bool SessionManager::setArchived(const QUuid& conversationId, bool archived, QString* error) {
    if (conversationId.isNull()) {
        setError(error, QStringLiteral("Cannot update an invalid conversation ID"));
        return false;
    }

    std::optional<domain::Conversation> stored;
    if (auto* openController = registry_.controller(conversationId); openController != nullptr) {
        const domain::ConversationStatus status = openController->status();
        const bool active = status == domain::ConversationStatus::Connecting ||
                            status == domain::ConversationStatus::Running ||
                            status == domain::ConversationStatus::WaitingApproval ||
                            status == domain::ConversationStatus::WaitingInput;
        if (archived && active) {
            setError(error,
                     QStringLiteral("Cannot archive a conversation while Agent work is active"));
            return false;
        }
        stored = openController->conversation();
    } else {
        stored = repository_->conversationById(conversationId, error);
    }
    if (!stored.has_value()) {
        if (error == nullptr || error->isEmpty()) {
            setError(error, QStringLiteral("Conversation does not exist"));
        }
        return false;
    }
    if (stored->archived == archived) {
        return true;
    }

    if (auto* openController = registry_.controller(conversationId); openController != nullptr) {
        if (!openController->setArchived(archived, error)) {
            return false;
        }
        if (archived) {
            registry_.close(conversationId);
        }
        return true;
    }
    stored->archived = archived;
    return repository_->saveConversation(*stored, error);
}

bool SessionManager::setPinned(const QUuid& conversationId, bool pinned, QString* error) {
    if (conversationId.isNull()) {
        setError(error, QStringLiteral("Cannot update an invalid conversation ID"));
        return false;
    }
    if (auto* openController = registry_.controller(conversationId); openController != nullptr) {
        return openController->setPinned(pinned, error);
    }
    auto stored = repository_->conversationById(conversationId, error);
    if (!stored.has_value()) {
        if (error == nullptr || error->isEmpty()) {
            setError(error, QStringLiteral("Conversation does not exist"));
        }
        return false;
    }
    stored->pinned = pinned;
    return repository_->saveConversation(*stored, error);
}

bool SessionManager::setTags(const QUuid& conversationId, const QStringList& tags, QString* error) {
    if (conversationId.isNull()) {
        setError(error, QStringLiteral("Cannot update an invalid conversation ID"));
        return false;
    }
    if (auto* openController = registry_.controller(conversationId); openController != nullptr) {
        return openController->setTags(tags, error);
    }
    const auto normalized = domain::normalizeConversationTags(tags, error);
    if (!normalized.has_value()) {
        return false;
    }
    auto stored = repository_->conversationById(conversationId, error);
    if (!stored.has_value()) {
        if (error == nullptr || error->isEmpty()) {
            setError(error, QStringLiteral("Conversation does not exist"));
        }
        return false;
    }
    stored->tags = *normalized;
    return repository_->saveConversation(*stored, error);
}

bool SessionManager::setGroup(const QUuid& conversationId, const QString& groupName,
                              QString* error) {
    if (conversationId.isNull()) {
        setError(error, QStringLiteral("Cannot update an invalid conversation ID"));
        return false;
    }
    if (auto* openController = registry_.controller(conversationId); openController != nullptr) {
        return openController->setGroup(groupName, error);
    }
    const auto normalized = domain::normalizeConversationGroup(groupName, error);
    if (!normalized.has_value()) {
        return false;
    }
    auto stored = repository_->conversationById(conversationId, error);
    if (!stored.has_value()) {
        if (error == nullptr || error->isEmpty()) {
            setError(error, QStringLiteral("Conversation does not exist"));
        }
        return false;
    }
    stored->groupName = *normalized;
    return repository_->saveConversation(*stored, error);
}

session::SessionController* SessionManager::restore(const QUuid& conversationId, QString* error) {
    if (conversationId.isNull()) {
        setError(error, QStringLiteral("Cannot restore an invalid conversation ID"));
        return nullptr;
    }
    auto stored = repository_->conversationById(conversationId, error);
    if (!stored.has_value()) {
        if (error == nullptr || error->isEmpty()) {
            setError(error, QStringLiteral("Conversation does not exist"));
        }
        return nullptr;
    }
    if (!stored->archived) {
        return open(*stored, error);
    }

    agent::AgentRuntime runtime = runtimeFactory_(stored->agentKind);
    if (runtime.adapter == nullptr || runtime.selectedKind != stored->agentKind) {
        setError(error, runtime.detail.isEmpty()
                            ? QStringLiteral("The conversation Agent runtime is unavailable")
                            : runtime.detail);
        return nullptr;
    }
    stored->archived = false;
    stored->status = domain::ConversationStatus::Dormant;
    if (!repository_->saveConversation(*stored, error)) {
        return nullptr;
    }
    auto* controller = addPrepared(*stored, std::move(runtime), error);
    if (controller == nullptr) {
        stored->archived = true;
        QString rollbackError;
        repository_->saveConversation(*stored, &rollbackError);
    }
    return controller;
}

QList<domain::Conversation> SessionManager::catalog(QString* error) const {
    return repository_->conversations(error);
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
