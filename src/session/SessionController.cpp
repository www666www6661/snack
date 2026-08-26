#include "session/SessionController.h"

#include <algorithm>
#include <optional>
#include <utility>

namespace snack::session {
namespace {

const agent::ModelCapability* findModel(const agent::CapabilitySet& capabilities,
                                        const QString& modelId) {
    const auto iterator = std::find_if(
        capabilities.modelCapabilities.cbegin(), capabilities.modelCapabilities.cend(),
        [&modelId](const agent::ModelCapability& model) { return model.id == modelId; });
    return iterator == capabilities.modelCapabilities.cend() ? nullptr : &*iterator;
}

std::optional<domain::ReasoningEffort> knownEffort(const agent::CapabilitySet& capabilities,
                                                   const QString& effortId) {
    const auto iterator =
        std::find_if(capabilities.reasoningEfforts.cbegin(), capabilities.reasoningEfforts.cend(),
                     [&effortId](domain::ReasoningEffort effort) {
                         return domain::enumName(effort) == effortId;
                     });
    if (iterator == capabilities.reasoningEfforts.cend()) {
        return std::nullopt;
    }
    return *iterator;
}

} // namespace

SessionController::SessionController(domain::Conversation conversation,
                                     agent::IAgentAdapter* adapter,
                                     storage::IEventRepository* repository, QObject* parent)
    : QObject(parent), conversation_(std::move(conversation)), adapter_(adapter),
      repository_(repository) {
    Q_ASSERT(adapter_ != nullptr);
    Q_ASSERT(repository_ != nullptr);
    nextTurnSettings_.agentKind = adapter_->kind();
    nextTurnSettings_.workingDirectory = conversation_.workingDirectory;
    nextTurnSettings_.capabilityVersion = adapter_->capabilities().version;

    connect(adapter_, &agent::IAgentAdapter::connectionChanged, this,
            [this](bool connected, const QString&) {
                setStatus(connected ? domain::ConversationStatus::Idle
                                    : domain::ConversationStatus::Disconnected);
            });
    connect(adapter_, &agent::IAgentAdapter::eventReceived, this,
            &SessionController::handleAdapterEvent);
    connect(adapter_, &agent::IAgentAdapter::capabilitiesChanged, this,
            &SessionController::handleCapabilitiesChanged);
    connect(adapter_, &agent::IAgentAdapter::turnFinished, this, [this](const QUuid& turnId, bool) {
        if (turnId == activeTurnId_) {
            activeTurnId_ = QUuid{};
            setStatus(domain::ConversationStatus::Idle);
        }
    });
}

void SessionController::handleCapabilitiesChanged(const agent::CapabilitySet& capabilities) {
    domain::TurnSettingsSnapshot normalized = nextTurnSettings_;
    normalized.capabilityVersion = capabilities.version;

    if (!capabilities.models.isEmpty() && !capabilities.models.contains(normalized.modelId)) {
        normalized.modelId = capabilities.models.contains(capabilities.defaultModelId)
                                 ? capabilities.defaultModelId
                                 : capabilities.models.constFirst();
    }

    if (const auto* model = findModel(capabilities, normalized.modelId); model != nullptr) {
        const QString currentEffort = domain::enumName(normalized.reasoningEffort);
        const auto supportsCurrent = std::any_of(
            model->supportedReasoningEfforts.cbegin(), model->supportedReasoningEfforts.cend(),
            [&currentEffort](const agent::ReasoningEffortCapability& effort) {
                return effort.id == currentEffort;
            });
        if (!supportsCurrent) {
            auto replacement = knownEffort(capabilities, model->defaultReasoningEffortId);
            for (auto iterator = model->supportedReasoningEfforts.cbegin();
                 !replacement.has_value() && iterator != model->supportedReasoningEfforts.cend();
                 ++iterator) {
                replacement = knownEffort(capabilities, iterator->id);
            }
            if (replacement.has_value()) {
                normalized.reasoningEffort = *replacement;
            }
        }
    } else if (!capabilities.reasoningEfforts.isEmpty() &&
               !capabilities.reasoningEfforts.contains(normalized.reasoningEffort)) {
        normalized.reasoningEffort = capabilities.reasoningEfforts.constFirst();
    }

    if (!capabilities.accessLevels.isEmpty() &&
        !capabilities.accessLevels.contains(normalized.accessLevel)) {
        normalized.accessLevel = capabilities.accessLevels.contains(domain::AccessLevel::Strict)
                                     ? domain::AccessLevel::Strict
                                     : capabilities.accessLevels.constFirst();
    }

    if (normalized == nextTurnSettings_) {
        return;
    }
    nextTurnSettings_ = normalized;
    emit nextTurnSettingsChanged(nextTurnSettings_);
}

const domain::Conversation& SessionController::conversation() const { return conversation_; }
domain::ConversationStatus SessionController::status() const { return conversation_.status; }
domain::TurnSettingsSnapshot SessionController::nextTurnSettings() const {
    return nextTurnSettings_;
}

QList<domain::AgentEvent> SessionController::restoredEvents(QString* error) {
    const auto events = repository_->eventsForConversation(conversation_.id, error);
    if (!events.isEmpty()) {
        nextSequence_ = events.constLast().sequence + 1;
    }
    return events;
}

void SessionController::open() {
    if (conversation_.status != domain::ConversationStatus::Dormant &&
        conversation_.status != domain::ConversationStatus::Disconnected &&
        conversation_.status != domain::ConversationStatus::Failed) {
        return;
    }

    QString error;
    if (!repository_->saveConversation(conversation_, &error)) {
        emit persistenceError(error);
        setStatus(domain::ConversationStatus::Failed);
        return;
    }
    setStatus(domain::ConversationStatus::Connecting);
    adapter_->connectAgent(conversation_.workingDirectory);
}

bool SessionController::sendMessage(const QString& message, QString* error) {
    const QString trimmed = message.trimmed();
    if (trimmed.isEmpty()) {
        if (error != nullptr) {
            *error = QStringLiteral("Message cannot be empty");
        }
        return false;
    }
    if (conversation_.status != domain::ConversationStatus::Idle) {
        if (error != nullptr) {
            *error = QStringLiteral("Session is not idle");
        }
        return false;
    }

    activeTurnId_ = QUuid::createUuid();
    domain::AgentEvent userEvent;
    userEvent.turnId = activeTurnId_;
    userEvent.type = domain::AgentEventType::UserMessage;
    userEvent.payload = {{QStringLiteral("text"), trimmed},
                         {QStringLiteral("settings"), nextTurnSettings_.toJson()}};
    recordEvent(userEvent);
    setStatus(domain::ConversationStatus::Running);
    adapter_->startTurn({activeTurnId_, trimmed, nextTurnSettings_});
    return true;
}

void SessionController::interrupt() {
    if (conversation_.status == domain::ConversationStatus::Running) {
        adapter_->interruptTurn();
    }
}

void SessionController::close() {
    adapter_->closeAgent();
    activeTurnId_ = QUuid{};
    setStatus(domain::ConversationStatus::Closed);
}

void SessionController::setNextTurnSettings(const domain::TurnSettingsSnapshot& settings) {
    domain::TurnSettingsSnapshot normalized = settings;
    normalized.agentKind = adapter_->kind();
    normalized.workingDirectory = conversation_.workingDirectory;
    normalized.capabilityVersion = adapter_->capabilities().version;
    if (normalized == nextTurnSettings_) {
        return;
    }
    nextTurnSettings_ = normalized;
    emit nextTurnSettingsChanged(nextTurnSettings_);
}

void SessionController::handleAdapterEvent(domain::AgentEvent event) {
    if (event.turnId != activeTurnId_) {
        domain::AgentEvent warning;
        warning.turnId = activeTurnId_;
        warning.type = domain::AgentEventType::WarningRaised;
        warning.payload = {
            {QStringLiteral("message"), QStringLiteral("Ignored stale agent event")}};
        warning.rawPayload = event.payload;
        recordEvent(warning);
        return;
    }
    recordEvent(std::move(event));
}

void SessionController::recordEvent(domain::AgentEvent event) {
    event.id = event.id.isNull() ? QUuid::createUuid() : event.id;
    event.conversationId = conversation_.id;
    event.sequence = nextSequence_++;
    if (!event.occurredAt.isValid()) {
        event.occurredAt = QDateTime::currentDateTimeUtc();
    }

    QString error;
    if (!repository_->appendEvent(event, &error)) {
        emit persistenceError(error);
    }
    emit eventRecorded(event);
}

void SessionController::setStatus(domain::ConversationStatus status) {
    if (conversation_.status == status) {
        return;
    }
    conversation_.status = status;
    QString error;
    if (!repository_->saveConversation(conversation_, &error)) {
        emit persistenceError(error);
    }
    emit statusChanged(status);
}

} // namespace snack::session
