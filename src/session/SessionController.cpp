#include "session/SessionController.h"

#include <QJsonArray>

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
    capabilities_ = adapter_->capabilities();
    nextTurnSettings_.agentKind = adapter_->kind();
    nextTurnSettings_.workingDirectory = conversation_.workingDirectory;
    nextTurnSettings_.capabilityVersion = capabilities_.version;
    nextTurnSettings_ = normalizeSettings(nextTurnSettings_);

    connect(adapter_, &agent::IAgentAdapter::connectionChanged, this,
            [this](bool connected, const QString& detail) {
                if (connectionDetail_ != detail) {
                    connectionDetail_ = detail;
                    emit connectionDetailChanged(connectionDetail_);
                }
                if (!connected) {
                    pendingApprovals_.clear();
                    pendingUserInputs_.clear();
                }
                setStatus(connected ? domain::ConversationStatus::Idle
                                    : domain::ConversationStatus::Disconnected);
            });
    connect(adapter_, &agent::IAgentAdapter::eventReceived, this,
            &SessionController::handleAdapterEvent);
    connect(adapter_, &agent::IAgentAdapter::capabilitiesChanged, this,
            &SessionController::handleCapabilitiesChanged);
    connect(adapter_, &agent::IAgentAdapter::nativeIdentityChanged, this,
            &SessionController::handleNativeIdentityChanged);
    connect(adapter_, &agent::IAgentAdapter::turnFinished, this, [this](const QUuid& turnId, bool) {
        if (turnId == activeTurnId_) {
            activeTurnId_ = QUuid{};
            activeTurnSettings_ = {};
            pendingApprovals_.clear();
            pendingUserInputs_.clear();
            setStatus(domain::ConversationStatus::Idle);
        }
    });
}

void SessionController::handleNativeIdentityChanged(const QString& threadId,
                                                    const QString& sessionId) {
    if (threadId.isEmpty() || sessionId.isEmpty() ||
        (conversation_.nativeThreadId == threadId && conversation_.nativeSessionId == sessionId)) {
        return;
    }
    conversation_.nativeThreadId = threadId;
    conversation_.nativeSessionId = sessionId;
    QString error;
    if (!repository_->saveConversation(conversation_, &error)) {
        emit persistenceError(error);
        return;
    }
    emit nativeIdentityChanged(threadId, sessionId);
}

void SessionController::handleCapabilitiesChanged(const agent::CapabilitySet& capabilities) {
    capabilities_ = capabilities;
    emit capabilitiesChanged(capabilities_);
    const domain::TurnSettingsSnapshot normalized = normalizeSettings(nextTurnSettings_);

    if (normalized == nextTurnSettings_) {
        return;
    }
    nextTurnSettings_ = normalized;
    emit nextTurnSettingsChanged(nextTurnSettings_);
}

domain::TurnSettingsSnapshot
SessionController::normalizeSettings(const domain::TurnSettingsSnapshot& settings) const {
    domain::TurnSettingsSnapshot normalized = settings;
    normalized.agentKind = adapter_->kind();
    normalized.workingDirectory = conversation_.workingDirectory;
    normalized.capabilityVersion = capabilities_.version;

    if (!capabilities_.models.isEmpty() && !capabilities_.models.contains(normalized.modelId)) {
        normalized.modelId = capabilities_.models.contains(capabilities_.defaultModelId)
                                 ? capabilities_.defaultModelId
                                 : capabilities_.models.constFirst();
    }

    if (const auto* model = findModel(capabilities_, normalized.modelId); model != nullptr) {
        const QString currentEffort = domain::enumName(normalized.reasoningEffort);
        const auto supportsCurrent = std::any_of(
            model->supportedReasoningEfforts.cbegin(), model->supportedReasoningEfforts.cend(),
            [&currentEffort](const agent::ReasoningEffortCapability& effort) {
                return effort.id == currentEffort;
            });
        if (!supportsCurrent) {
            auto replacement = knownEffort(capabilities_, model->defaultReasoningEffortId);
            for (auto iterator = model->supportedReasoningEfforts.cbegin();
                 !replacement.has_value() && iterator != model->supportedReasoningEfforts.cend();
                 ++iterator) {
                replacement = knownEffort(capabilities_, iterator->id);
            }
            if (replacement.has_value()) {
                normalized.reasoningEffort = *replacement;
            }
        }
    } else if (!capabilities_.reasoningEfforts.isEmpty() &&
               !capabilities_.reasoningEfforts.contains(normalized.reasoningEffort)) {
        normalized.reasoningEffort = capabilities_.reasoningEfforts.constFirst();
    }

    if (!capabilities_.accessLevels.isEmpty() &&
        !capabilities_.accessLevels.contains(normalized.accessLevel)) {
        normalized.accessLevel = capabilities_.accessLevels.contains(domain::AccessLevel::Strict)
                                     ? domain::AccessLevel::Strict
                                     : capabilities_.accessLevels.constFirst();
    }
    return normalized;
}

const domain::Conversation& SessionController::conversation() const { return conversation_; }
domain::ConversationStatus SessionController::status() const { return conversation_.status; }
domain::TurnSettingsSnapshot SessionController::nextTurnSettings() const {
    return nextTurnSettings_;
}
const agent::CapabilitySet& SessionController::capabilities() const { return capabilities_; }
QString SessionController::connectionDetail() const { return connectionDetail_; }
qsizetype SessionController::pendingApprovalCount() const { return pendingApprovals_.size(); }
qsizetype SessionController::pendingInputCount() const { return pendingUserInputs_.size(); }

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
    adapter_->connectAgent({.workingDirectory = conversation_.workingDirectory,
                            .nativeThreadId = conversation_.nativeThreadId,
                            .settings = nextTurnSettings_});
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
    activeTurnSettings_ = nextTurnSettings_;
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

bool SessionController::steerMessage(const QString& message, QString* error) {
    const QString trimmed = message.trimmed();
    if (trimmed.isEmpty()) {
        if (error != nullptr) {
            *error = QStringLiteral("Steer message cannot be empty");
        }
        return false;
    }
    if (conversation_.status != domain::ConversationStatus::Running || activeTurnId_.isNull() ||
        !capabilities_.supportsSteering) {
        if (error != nullptr) {
            *error = QStringLiteral("The active turn cannot accept steering");
        }
        return false;
    }
    if (!adapter_->steerTurn({activeTurnId_, trimmed})) {
        if (error != nullptr) {
            *error = QStringLiteral("Failed to send steer message");
        }
        return false;
    }
    domain::AgentEvent event;
    event.turnId = activeTurnId_;
    event.type = domain::AgentEventType::UserMessage;
    event.payload = {{QStringLiteral("text"), trimmed},
                     {QStringLiteral("steered"), true},
                     {QStringLiteral("settings"), activeTurnSettings_.toJson()}};
    recordEvent(event);
    return true;
}

bool SessionController::respondToApproval(const QString& requestId,
                                          domain::ApprovalDecision decision, QString* error) {
    if (!pendingApprovals_.contains(requestId)) {
        if (error != nullptr) {
            *error = QStringLiteral("Approval request is no longer active");
        }
        return false;
    }
    const QJsonArray availableDecisions =
        pendingApprovals_.value(requestId).value(QStringLiteral("availableDecisions")).toArray();
    if (!availableDecisions.isEmpty() && !availableDecisions.contains(domain::enumName(decision))) {
        if (error != nullptr) {
            *error = QStringLiteral("Approval decision is not available for this request");
        }
        return false;
    }
    if (!adapter_->respondToApproval(requestId, decision)) {
        if (error != nullptr) {
            *error = QStringLiteral("Failed to send approval response");
        }
        return false;
    }

    pendingApprovals_.remove(requestId);
    domain::AgentEvent resolved;
    resolved.turnId = activeTurnId_;
    resolved.type = domain::AgentEventType::ApprovalResolved;
    resolved.payload = {{QStringLiteral("requestId"), requestId},
                        {QStringLiteral("decision"), domain::enumName(decision)},
                        {QStringLiteral("resolution"), QStringLiteral("answered")}};
    recordEvent(resolved);
    recomputeActiveStatus();
    return true;
}

bool SessionController::respondToUserInput(const QString& requestId, const QJsonObject& answers,
                                           QString* error) {
    if (!pendingUserInputs_.contains(requestId)) {
        if (error != nullptr) {
            *error = QStringLiteral("User input request is no longer active");
        }
        return false;
    }
    if (!adapter_->respondToUserInput(requestId, answers)) {
        if (error != nullptr) {
            *error = QStringLiteral("Failed to send user input response");
        }
        return false;
    }

    pendingUserInputs_.remove(requestId);
    domain::AgentEvent resolved;
    resolved.turnId = activeTurnId_;
    resolved.type = domain::AgentEventType::UserInputResolved;
    resolved.payload = {{QStringLiteral("requestId"), requestId},
                        {QStringLiteral("resolution"), QStringLiteral("answered")}};
    recordEvent(resolved);
    recomputeActiveStatus();
    return true;
}

void SessionController::interrupt() {
    if (conversation_.status == domain::ConversationStatus::Running ||
        conversation_.status == domain::ConversationStatus::WaitingApproval ||
        conversation_.status == domain::ConversationStatus::WaitingInput) {
        adapter_->interruptTurn();
    }
}

void SessionController::close() {
    adapter_->closeAgent();
    activeTurnId_ = QUuid{};
    activeTurnSettings_ = {};
    pendingApprovals_.clear();
    pendingUserInputs_.clear();
    setStatus(domain::ConversationStatus::Closed);
}

void SessionController::setNextTurnSettings(const domain::TurnSettingsSnapshot& settings) {
    const domain::TurnSettingsSnapshot normalized = normalizeSettings(settings);
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
    if (event.type == domain::AgentEventType::ApprovalRequested) {
        const QString requestId = event.payload.value(QStringLiteral("requestId")).toString();
        if (requestId.isEmpty() || pendingApprovals_.contains(requestId)) {
            domain::AgentEvent warning;
            warning.turnId = activeTurnId_;
            warning.type = domain::AgentEventType::WarningRaised;
            warning.payload = {{QStringLiteral("message"),
                                QStringLiteral("Ignored invalid or duplicate approval request")}};
            warning.rawPayload = event.rawPayload;
            recordEvent(warning);
            return;
        }
        pendingApprovals_.insert(requestId, event.payload);
        recomputeActiveStatus();
    } else if (event.type == domain::AgentEventType::ApprovalResolved) {
        const QString requestId = event.payload.value(QStringLiteral("requestId")).toString();
        if (pendingApprovals_.remove(requestId) > 0) {
            recomputeActiveStatus();
        }
    } else if (event.type == domain::AgentEventType::UserInputRequested) {
        const QString requestId = event.payload.value(QStringLiteral("requestId")).toString();
        if (requestId.isEmpty() || pendingUserInputs_.contains(requestId)) {
            domain::AgentEvent warning;
            warning.turnId = activeTurnId_;
            warning.type = domain::AgentEventType::WarningRaised;
            warning.payload = {{QStringLiteral("message"),
                                QStringLiteral("Ignored invalid or duplicate user input request")}};
            warning.rawPayload = event.rawPayload;
            recordEvent(warning);
            return;
        }
        pendingUserInputs_.insert(requestId, event.payload);
        recomputeActiveStatus();
    } else if (event.type == domain::AgentEventType::UserInputResolved) {
        const QString requestId = event.payload.value(QStringLiteral("requestId")).toString();
        if (pendingUserInputs_.remove(requestId) > 0) {
            recomputeActiveStatus();
        }
    }
    recordEvent(std::move(event));
}

void SessionController::recomputeActiveStatus() {
    if (activeTurnId_.isNull()) {
        return;
    }
    const bool hasBlockingInput = std::any_of(
        pendingUserInputs_.cbegin(), pendingUserInputs_.cend(), [](const QJsonObject& payload) {
            return payload.value(QStringLiteral("isBlocking")).toBool();
        });
    if (hasBlockingInput) {
        setStatus(domain::ConversationStatus::WaitingInput);
    } else if (!pendingApprovals_.isEmpty()) {
        setStatus(domain::ConversationStatus::WaitingApproval);
    } else {
        setStatus(domain::ConversationStatus::Running);
    }
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
