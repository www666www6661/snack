#include "agent/codex/CodexAdapter.h"

#include <QDir>
#include <QJsonObject>
#include <QSet>
#include <QTimer>

#include <algorithm>
#include <utility>

namespace snack::agent::codex {
namespace {

bool isToolItemType(const QString& type) {
    static const QSet<QString> types = {
        QStringLiteral("commandExecution"), QStringLiteral("fileChange"),
        QStringLiteral("mcpToolCall"),      QStringLiteral("dynamicToolCall"),
        QStringLiteral("collabToolCall"),   QStringLiteral("webSearch"),
        QStringLiteral("imageView"),        QStringLiteral("contextCompaction")};
    return types.contains(type);
}

QJsonObject itemEventPayload(const QJsonObject& item) {
    QJsonObject payload = item;
    payload.insert(QStringLiteral("itemId"), item.value(QStringLiteral("id")));
    payload.insert(QStringLiteral("kind"), item.value(QStringLiteral("type")));
    return payload;
}

QJsonObject withoutReasoningContent(const QJsonObject& raw) {
    QJsonObject sanitized = raw;
    QJsonObject params = sanitized.value(QStringLiteral("params")).toObject();
    QJsonObject item = params.value(QStringLiteral("item")).toObject();
    item.remove(QStringLiteral("content"));
    params.insert(QStringLiteral("item"), item);
    sanitized.insert(QStringLiteral("params"), params);
    return sanitized;
}

} // namespace

CodexAdapter::CodexAdapter(CliInstallation installation, process::IProcessTransport* transport,
                           QObject* parent)
    : IAgentAdapter(parent), installation_(std::move(installation)), client_(transport, this) {
    capabilities_.version = installation_.version.isEmpty()
                                ? QStringLiteral("codex-app-server/unknown")
                                : QStringLiteral("codex-app-server/%1").arg(installation_.version);
    capabilities_.accessLevels = {domain::AccessLevel::Strict, domain::AccessLevel::Workspace,
                                  domain::AccessLevel::Full};
    capabilities_.supportsSteering = false;
    capabilities_.supportsInterrupt = true;

    connect(&client_, &CodexAppServerClient::handshakeCompleted, this,
            [this](const ServerInfo&) { requestModelPage(); });
    connect(&client_, &CodexAppServerClient::responseReceived, this, &CodexAdapter::handleResponse);
    connect(&client_, &CodexAppServerClient::requestFailed, this,
            &CodexAdapter::handleRequestFailure);
    connect(&client_, &CodexAppServerClient::notificationReceived, this,
            &CodexAdapter::handleNotification);
    connect(&client_, &CodexAppServerClient::serverRequestReceived, this,
            &CodexAdapter::handleServerRequest);
    connect(&client_, &CodexAppServerClient::failureOccurred, this, &CodexAdapter::failConnection);
    connect(&client_, &CodexAppServerClient::stateChanged, this, [this](ConnectionState state) {
        if (state == ConnectionState::Stopped && closing_) {
            const bool wasActive = connecting_ || connected_;
            connecting_ = false;
            connected_ = false;
            closing_ = false;
            if (wasActive) {
                emit connectionChanged(false, QStringLiteral("closed"));
            }
        }
    });
}

domain::AgentKind CodexAdapter::kind() const { return domain::AgentKind::Codex; }

CapabilitySet CodexAdapter::capabilities() const { return capabilities_; }

void CodexAdapter::connectAgent(const AgentConnectionRequest& request) {
    if (connecting_ || connected_ || closing_) {
        return;
    }
    connectionRequest_ = request;
    if (connectionRequest_.nativeThreadId.isEmpty()) {
        connectionRequest_.nativeThreadId = nativeThreadId_;
    }
    models_.clear();
    requestedCursors_.clear();
    capabilities_.models.clear();
    capabilities_.defaultModelId.clear();
    capabilities_.modelCapabilities.clear();
    capabilities_.reasoningEfforts.clear();
    connecting_ = true;

    QTimer::singleShot(0, this, [this] {
        if (!connecting_ || closing_) {
            return;
        }
        if (connectionRequest_.workingDirectory.trimmed().isEmpty()) {
            failConnection(QStringLiteral("Codex working directory is missing"));
            return;
        }
        if (!installation_.isUsable()) {
            failConnection(installation_.detail.isEmpty()
                               ? QStringLiteral("Codex app-server is unavailable")
                               : installation_.detail);
            return;
        }
        client_.start(CodexCliDiscovery::appServerLaunchSpec(installation_,
                                                             connectionRequest_.workingDirectory));
    });
}

void CodexAdapter::startTurn(const TurnRequest& request) {
    const auto reject = [this, &request](const QString& detail) {
        domain::AgentEvent event;
        event.turnId = request.turnId;
        event.type = domain::AgentEventType::TurnFailed;
        event.payload = {{QStringLiteral("message"), detail}};
        emit eventReceived(event);
        emit turnFinished(request.turnId, false);
    };
    if (!connected_ || client_.state() != ConnectionState::Ready || nativeThreadId_.isEmpty()) {
        reject(QStringLiteral("Codex is not connected to a native thread"));
        return;
    }
    if (!activeTurn_.turnId.isNull()) {
        reject(QStringLiteral("A Codex turn is already active"));
        return;
    }
    if (request.turnId.isNull() || request.message.trimmed().isEmpty()) {
        reject(QStringLiteral("Codex turn input is invalid"));
        return;
    }
    if (request.settings.agentKind != domain::AgentKind::Codex ||
        !capabilities_.models.contains(request.settings.modelId)) {
        reject(QStringLiteral("Codex turn settings select an unsupported agent or model"));
        return;
    }
    const auto model = std::find_if(capabilities_.modelCapabilities.cbegin(),
                                    capabilities_.modelCapabilities.cend(),
                                    [&request](const ModelCapability& candidate) {
                                        return candidate.id == request.settings.modelId;
                                    });
    const QString effort = domain::enumName(request.settings.reasoningEffort);
    if (model == capabilities_.modelCapabilities.cend() ||
        std::none_of(model->supportedReasoningEfforts.cbegin(),
                     model->supportedReasoningEfforts.cend(),
                     [&effort](const ReasoningEffortCapability& candidate) {
                         return candidate.id == effort;
                     })) {
        reject(QStringLiteral("The selected Codex model does not support this reasoning effort"));
        return;
    }
    const Qt::CaseSensitivity pathCaseSensitivity =
#ifdef Q_OS_WIN
        Qt::CaseInsensitive;
#else
        Qt::CaseSensitive;
#endif
    if (QDir::cleanPath(request.settings.workingDirectory)
            .compare(QDir::cleanPath(connectionRequest_.workingDirectory), pathCaseSensitivity) !=
        0) {
        reject(QStringLiteral("Codex turn settings use an unexpected working directory"));
        return;
    }

    activeTurn_ = request;
    nativeTurnId_.clear();
    startedAgentMessages_.clear();
    completedAgentMessages_.clear();
    streamedAgentText_.clear();
    startedToolItems_.clear();
    completedToolItems_.clear();
    startedReasoningItems_.clear();
    completedReasoningItems_.clear();
    completedPlanItems_.clear();
    reasoningSummaries_.clear();
    activeItems_.clear();
    pendingApprovals_.clear();
    approvalTokenByNativeKey_.clear();
    turnStartedEmitted_ = false;
    interruptRequested_ = false;
    interruptSent_ = false;
    interruptRequestId_ = 0;
    turnRequestId_ = client_.sendRequest(
        QStringLiteral("turn/start"),
        makeTurnStartParameters(nativeThreadId_, connectionRequest_.workingDirectory, request));
    if (turnRequestId_ == 0 && !activeTurn_.turnId.isNull()) {
        finishActiveTurn(domain::AgentEventType::TurnFailed,
                         QStringLiteral("Failed to request a Codex turn"), QStringLiteral("failed"),
                         {}, false);
    }
}

bool CodexAdapter::respondToApproval(const QString& requestId, domain::ApprovalDecision decision) {
    const auto iterator = pendingApprovals_.find(requestId);
    if (iterator == pendingApprovals_.end() || activeTurn_.turnId.isNull()) {
        return false;
    }
    if (!iterator->availableDecisions.isEmpty() &&
        !iterator->availableDecisions.contains(domain::enumName(decision))) {
        return false;
    }
    if (!client_.sendResponse(iterator->nativeRequestId, approvalResponse(decision))) {
        return false;
    }
    approvalTokenByNativeKey_.remove(nativeRequestKey(iterator->nativeRequestId));
    pendingApprovals_.erase(iterator);
    return true;
}

void CodexAdapter::interruptTurn() {
    if (activeTurn_.turnId.isNull() || interruptRequested_) {
        return;
    }
    interruptRequested_ = true;
    sendInterruptRequest();
}

void CodexAdapter::closeAgent() {
    if (closing_) {
        return;
    }
    if (!connecting_ && !connected_) {
        return;
    }
    if (!activeTurn_.turnId.isNull()) {
        finishActiveTurn(domain::AgentEventType::TurnInterrupted,
                         QStringLiteral("Codex connection closed"), QStringLiteral("interrupted"),
                         {}, true);
    }
    closing_ = true;
    if (client_.state() == ConnectionState::Stopped) {
        connecting_ = false;
        connected_ = false;
        closing_ = false;
        emit connectionChanged(false, QStringLiteral("closed"));
        return;
    }
    client_.stop();
}

void CodexAdapter::requestModelPage(const QString& cursor) {
    if (requestedCursors_.contains(cursor)) {
        failConnection(QStringLiteral("Codex model catalog returned a repeated cursor"));
        return;
    }
    requestedCursors_.insert(cursor);
    QJsonObject params{{QStringLiteral("limit"), 100}, {QStringLiteral("includeHidden"), false}};
    if (!cursor.isEmpty()) {
        params.insert(QStringLiteral("cursor"), cursor);
    }
    modelRequestId_ = client_.sendRequest(QStringLiteral("model/list"), params);
    if (modelRequestId_ == 0) {
        failConnection(QStringLiteral("Failed to request the Codex model catalog"));
    }
}

void CodexAdapter::handleResponse(qint64 id, const QString& method, const QJsonValue& result) {
    if (connecting_) {
        if (id == threadRequestId_ && method == threadRequestMethod_) {
            finishThreadLifecycle(result);
            return;
        }
        if (id != modelRequestId_ || method != QLatin1String("model/list")) {
            return;
        }
        QString error;
        const auto page = parseModelPage(result, &error);
        if (!page.has_value()) {
            failConnection(QStringLiteral("Invalid Codex model catalog: %1").arg(error));
            return;
        }
        appendModels(page->models);
        if (page->hasNextPage) {
            requestModelPage(page->nextCursor);
            return;
        }
        finishModelDiscovery();
        return;
    }

    if (!connected_ || activeTurn_.turnId.isNull()) {
        return;
    }
    if (id == turnRequestId_ && method == QLatin1String("turn/start")) {
        turnRequestId_ = 0;
        finishTurnStart(result);
        return;
    }
    if (id == interruptRequestId_ && method == QLatin1String("turn/interrupt")) {
        interruptRequestId_ = 0;
    }
}

void CodexAdapter::handleRequestFailure(qint64 id, const QString& method, int code,
                                        const QString& message) {
    if (connecting_ && id == modelRequestId_ && method == QLatin1String("model/list")) {
        failConnection(
            QStringLiteral("Codex model catalog request failed (%1): %2").arg(code).arg(message));
        return;
    }
    if (connecting_ && id == threadRequestId_ && method == threadRequestMethod_) {
        failConnection(
            QStringLiteral("Codex %1 request failed (%2): %3").arg(method).arg(code).arg(message));
        return;
    }
    if (connected_ && !activeTurn_.turnId.isNull() && id == turnRequestId_ &&
        method == QLatin1String("turn/start")) {
        turnRequestId_ = 0;
        finishActiveTurn(
            domain::AgentEventType::TurnFailed,
            QStringLiteral("Codex turn/start request failed (%1): %2").arg(code).arg(message),
            QStringLiteral("failed"), {}, false);
        return;
    }
    if (connected_ && !activeTurn_.turnId.isNull() && id == interruptRequestId_ &&
        method == QLatin1String("turn/interrupt")) {
        interruptRequestId_ = 0;
        interruptRequested_ = false;
        interruptSent_ = false;
        warnActive(
            QStringLiteral("Codex turn/interrupt request failed (%1): %2").arg(code).arg(message));
    }
}

void CodexAdapter::appendModels(const QList<CodexModelInfo>& models) {
    for (const CodexModelInfo& model : models) {
        const auto existing =
            std::find_if(models_.begin(), models_.end(),
                         [&model](const auto& item) { return item.id == model.id; });
        if (existing == models_.end()) {
            models_.append(model);
        } else {
            *existing = model;
        }
    }
}

void CodexAdapter::finishModelDiscovery() {
    QSet<domain::ReasoningEffort> reasoningEfforts;
    for (const CodexModelInfo& model : std::as_const(models_)) {
        if (model.hidden) {
            continue;
        }
        capabilities_.models.append(model.id);
        capabilities_.modelCapabilities.append(
            {.id = model.id,
             .displayName = model.displayName,
             .description = model.description,
             .defaultReasoningEffortId = model.defaultReasoningEffortId,
             .supportedReasoningEfforts =
                 [&model] {
                     QList<ReasoningEffortCapability> result;
                     for (const CodexReasoningEffort& effort : model.supportedReasoningEfforts) {
                         result.append({.id = effort.id, .description = effort.description});
                     }
                     return result;
                 }(),
             .inputModalities = model.inputModalities,
             .supportsPersonality = model.supportsPersonality,
             .isDefault = model.isDefault});
        if (model.isDefault) {
            capabilities_.defaultModelId = model.id;
        }
        for (const CodexReasoningEffort& effort : model.supportedReasoningEfforts) {
            const auto mapped = reasoningEffortFromCodex(effort.id);
            if (mapped.has_value()) {
                reasoningEfforts.insert(*mapped);
            }
        }
    }

    if (capabilities_.models.isEmpty()) {
        failConnection(QStringLiteral("Codex returned no visible models"));
        return;
    }
    if (capabilities_.defaultModelId.isEmpty()) {
        capabilities_.defaultModelId = capabilities_.models.constFirst();
    }
    const QList<domain::ReasoningEffort> canonicalOrder = {
        domain::ReasoningEffort::Minimal,   domain::ReasoningEffort::Low,
        domain::ReasoningEffort::Medium,    domain::ReasoningEffort::High,
        domain::ReasoningEffort::ExtraHigh, domain::ReasoningEffort::Maximum,
        domain::ReasoningEffort::Ultra};
    for (domain::ReasoningEffort effort : canonicalOrder) {
        if (reasoningEfforts.contains(effort)) {
            capabilities_.reasoningEfforts.append(effort);
        }
    }

    emit capabilitiesChanged(capabilities_);
    requestThreadLifecycle();
}

void CodexAdapter::requestThreadLifecycle() {
    QJsonObject params = threadAccessParameters(connectionRequest_.settings.accessLevel);
    params.insert(QStringLiteral("cwd"), connectionRequest_.workingDirectory);
    const QString requestedModel = connectionRequest_.settings.modelId;
    params.insert(QStringLiteral("model"), capabilities_.models.contains(requestedModel)
                                               ? requestedModel
                                               : capabilities_.defaultModelId);

    if (connectionRequest_.nativeThreadId.isEmpty()) {
        threadRequestMethod_ = QStringLiteral("thread/start");
    } else {
        threadRequestMethod_ = QStringLiteral("thread/resume");
        params.insert(QStringLiteral("threadId"), connectionRequest_.nativeThreadId);
    }
    threadRequestId_ = client_.sendRequest(threadRequestMethod_, params);
    if (threadRequestId_ == 0) {
        failConnection(QStringLiteral("Failed to request Codex thread lifecycle"));
    }
}

void CodexAdapter::finishThreadLifecycle(const QJsonValue& result) {
    QString error;
    const auto thread = parseThreadLifecycleResponse(result, &error);
    if (!thread.has_value()) {
        failConnection(QStringLiteral("Invalid Codex thread response: %1").arg(error));
        return;
    }
    if (!connectionRequest_.nativeThreadId.isEmpty() &&
        thread->id != connectionRequest_.nativeThreadId) {
        failConnection(QStringLiteral("Codex resumed an unexpected thread"));
        return;
    }
    const Qt::CaseSensitivity pathCaseSensitivity =
#ifdef Q_OS_WIN
        Qt::CaseInsensitive;
#else
        Qt::CaseSensitive;
#endif
    if (QDir::cleanPath(thread->workingDirectory)
            .compare(QDir::cleanPath(connectionRequest_.workingDirectory), pathCaseSensitivity) !=
        0) {
        failConnection(QStringLiteral("Codex thread uses an unexpected working directory"));
        return;
    }

    nativeThreadId_ = thread->id;
    nativeSessionId_ = thread->sessionId;
    connectionRequest_.nativeThreadId = nativeThreadId_;
    connecting_ = false;
    connected_ = true;
    emit nativeIdentityChanged(nativeThreadId_, nativeSessionId_);
    emit connectionChanged(true, capabilities_.version);
}

void CodexAdapter::finishTurnStart(const QJsonValue& result) {
    QString error;
    const auto turn = parseTurnStartResponse(result, &error);
    if (!turn.has_value()) {
        finishActiveTurn(domain::AgentEventType::TurnFailed,
                         QStringLiteral("Invalid Codex turn response: %1").arg(error),
                         QStringLiteral("failed"), {}, false);
        return;
    }
    if (!nativeTurnId_.isEmpty() && nativeTurnId_ != turn->id) {
        finishActiveTurn(domain::AgentEventType::TurnFailed,
                         QStringLiteral("Codex started an unexpected native turn"),
                         QStringLiteral("failed"), turn->raw, false);
        return;
    }
    nativeTurnId_ = turn->id;
    if (turn->status == QLatin1String("completed")) {
        finishActiveTurn(domain::AgentEventType::TurnCompleted, {}, turn->status, turn->raw, false);
        return;
    }
    if (turn->status == QLatin1String("interrupted")) {
        finishActiveTurn(domain::AgentEventType::TurnInterrupted, {}, turn->status, turn->raw,
                         true);
        return;
    }
    if (turn->status == QLatin1String("failed")) {
        finishActiveTurn(domain::AgentEventType::TurnFailed, turn->errorMessage, turn->status,
                         turn->raw, false);
        return;
    }
    sendInterruptRequest();
}

void CodexAdapter::handleNotification(const QString& method, const QJsonValue& params,
                                      const QJsonObject& raw) {
    if (!connected_ || activeTurn_.turnId.isNull()) {
        return;
    }

    if (method == QLatin1String("serverRequest/resolved")) {
        handleServerRequestResolved(params, raw);
        return;
    }

    if (method == QLatin1String("turn/started") || method == QLatin1String("turn/completed")) {
        QString error;
        const auto notification = parseTurnNotification(params, &error);
        if (!notification.has_value()) {
            finishActiveTurn(domain::AgentEventType::TurnFailed,
                             QStringLiteral("Invalid Codex %1 notification: %2").arg(method, error),
                             QStringLiteral("failed"), raw, false);
            return;
        }
        if (!acceptNativeContext(notification->threadId, notification->turn.id, raw)) {
            return;
        }
        if (method == QLatin1String("turn/started")) {
            if (!turnStartedEmitted_) {
                turnStartedEmitted_ = true;
                emitActiveEvent(domain::AgentEventType::TurnStarted,
                                {{QStringLiteral("nativeTurnId"), nativeTurnId_},
                                 {QStringLiteral("settings"), activeTurn_.settings.toJson()}},
                                raw);
            }
            sendInterruptRequest();
            return;
        }

        if (notification->turn.status == QLatin1String("completed")) {
            finishActiveTurn(domain::AgentEventType::TurnCompleted, {}, notification->turn.status,
                             raw, false);
        } else if (notification->turn.status == QLatin1String("interrupted")) {
            finishActiveTurn(domain::AgentEventType::TurnInterrupted, {}, notification->turn.status,
                             raw, true);
        } else if (notification->turn.status == QLatin1String("failed")) {
            finishActiveTurn(domain::AgentEventType::TurnFailed, notification->turn.errorMessage,
                             notification->turn.status, raw, false);
        } else {
            finishActiveTurn(domain::AgentEventType::TurnFailed,
                             QStringLiteral("Codex completed notification remained in progress"),
                             notification->turn.status, raw, false);
        }
        return;
    }

    if (method == QLatin1String("item/started") || method == QLatin1String("item/completed")) {
        QString error;
        const auto notification = parseItemNotification(params, &error);
        if (!notification.has_value()) {
            warnActive(
                QStringLiteral("Ignored invalid Codex %1 notification: %2").arg(method, error),
                raw);
            return;
        }
        if (!acceptNativeContext(notification->threadId, notification->turnId, raw)) {
            return;
        }
        activeItems_.insert(notification->itemId, notification->rawItem);
        if (isToolItemType(notification->itemType)) {
            if (!startedToolItems_.contains(notification->itemId)) {
                startedToolItems_.insert(notification->itemId);
                emitActiveEvent(domain::AgentEventType::ToolStarted,
                                itemEventPayload(notification->rawItem), raw);
            }
            if (method == QLatin1String("item/completed") &&
                !completedToolItems_.contains(notification->itemId)) {
                completedToolItems_.insert(notification->itemId);
                emitActiveEvent(domain::AgentEventType::ToolCompleted,
                                itemEventPayload(notification->rawItem), raw);
            }
            return;
        }
        if (notification->itemType == QLatin1String("reasoning")) {
            if (!startedReasoningItems_.contains(notification->itemId)) {
                startedReasoningItems_.insert(notification->itemId);
                reasoningSummaries_.insert(notification->itemId, {});
                emitActiveEvent(domain::AgentEventType::ReasoningStarted,
                                {{QStringLiteral("itemId"), notification->itemId}},
                                withoutReasoningContent(raw));
            }
            if (method == QLatin1String("item/completed") &&
                !completedReasoningItems_.contains(notification->itemId)) {
                completedReasoningItems_.insert(notification->itemId);
                emitActiveEvent(domain::AgentEventType::ReasoningCompleted,
                                {{QStringLiteral("itemId"), notification->itemId},
                                 {QStringLiteral("summary"),
                                  notification->rawItem.value(QStringLiteral("summary"))}},
                                withoutReasoningContent(raw));
            }
            return;
        }
        if (notification->itemType == QLatin1String("plan")) {
            if (method == QLatin1String("item/completed") &&
                !completedPlanItems_.contains(notification->itemId)) {
                completedPlanItems_.insert(notification->itemId);
                emitActiveEvent(domain::AgentEventType::PlanUpdated,
                                {{QStringLiteral("itemId"), notification->itemId},
                                 {QStringLiteral("text"), notification->text},
                                 {QStringLiteral("final"), true}},
                                raw);
            }
            return;
        }
        if (notification->itemType != QLatin1String("agentMessage")) {
            return;
        }

        const auto startMessage = [this, &notification, &raw] {
            if (startedAgentMessages_.contains(notification->itemId)) {
                return;
            }
            startedAgentMessages_.insert(notification->itemId);
            streamedAgentText_.insert(notification->itemId, {});
            emitActiveEvent(domain::AgentEventType::AgentMessageStart,
                            {{QStringLiteral("itemId"), notification->itemId}}, raw);
        };
        startMessage();
        if (method == QLatin1String("item/started") ||
            completedAgentMessages_.contains(notification->itemId)) {
            return;
        }

        QString& streamed = streamedAgentText_[notification->itemId];
        if (notification->text.startsWith(streamed)) {
            const QString suffix = notification->text.sliced(streamed.size());
            if (!suffix.isEmpty()) {
                streamed.append(suffix);
                emitActiveEvent(domain::AgentEventType::AgentMessageDelta,
                                {{QStringLiteral("itemId"), notification->itemId},
                                 {QStringLiteral("text"), suffix}},
                                raw);
            }
        } else if (notification->text != streamed) {
            warnActive(QStringLiteral("Codex final agent message differs from streamed text"), raw);
        }
        completedAgentMessages_.insert(notification->itemId);
        emitActiveEvent(domain::AgentEventType::AgentMessageComplete,
                        {{QStringLiteral("itemId"), notification->itemId},
                         {QStringLiteral("text"), notification->text}},
                        raw);
        return;
    }

    const auto itemDeltaParams = [this, &params,
                                  &raw](const QString& eventName) -> std::optional<QJsonObject> {
        if (!params.isObject()) {
            warnActive(
                QStringLiteral("Ignored invalid Codex %1: params must be an object").arg(eventName),
                raw);
            return std::nullopt;
        }
        const QJsonObject object = params.toObject();
        const QString threadId = object.value(QStringLiteral("threadId")).toString();
        const QString turnId = object.value(QStringLiteral("turnId")).toString();
        const QString itemId = object.value(QStringLiteral("itemId")).toString();
        if (threadId.isEmpty() || turnId.isEmpty() || itemId.isEmpty()) {
            warnActive(QStringLiteral("Ignored invalid Codex %1: missing context").arg(eventName),
                       raw);
            return std::nullopt;
        }
        if (!acceptNativeContext(threadId, turnId, raw)) {
            return std::nullopt;
        }
        return object;
    };

    if (method == QLatin1String("item/commandExecution/outputDelta") ||
        method == QLatin1String("item/mcpToolCall/progress") ||
        method == QLatin1String("item/fileChange/patchUpdated")) {
        const auto notification = itemDeltaParams(method);
        if (!notification.has_value()) {
            return;
        }
        const QString itemId = notification->value(QStringLiteral("itemId")).toString();
        if (completedToolItems_.contains(itemId)) {
            return;
        }
        if (!startedToolItems_.contains(itemId)) {
            QJsonObject item = activeItems_.value(itemId);
            item.insert(QStringLiteral("id"), itemId);
            item.insert(QStringLiteral("type"),
                        method == QLatin1String("item/commandExecution/outputDelta")
                            ? QStringLiteral("commandExecution")
                        : method == QLatin1String("item/mcpToolCall/progress")
                            ? QStringLiteral("mcpToolCall")
                            : QStringLiteral("fileChange"));
            activeItems_.insert(itemId, item);
            startedToolItems_.insert(itemId);
            emitActiveEvent(domain::AgentEventType::ToolStarted, itemEventPayload(item), raw);
        }
        QJsonObject payload{{QStringLiteral("itemId"), itemId}};
        if (method == QLatin1String("item/fileChange/patchUpdated")) {
            const QJsonArray changes = notification->value(QStringLiteral("changes")).toArray();
            payload.insert(QStringLiteral("changes"), changes);
            activeItems_[itemId].insert(QStringLiteral("changes"), changes);
        } else {
            payload.insert(QStringLiteral("text"),
                           method == QLatin1String("item/mcpToolCall/progress")
                               ? notification->value(QStringLiteral("message"))
                               : notification->value(QStringLiteral("delta")));
        }
        emitActiveEvent(domain::AgentEventType::ToolOutputDelta, payload, raw);
        return;
    }

    if (method == QLatin1String("item/reasoning/summaryPartAdded") ||
        method == QLatin1String("item/reasoning/summaryTextDelta")) {
        const auto notification = itemDeltaParams(method);
        if (!notification.has_value()) {
            return;
        }
        const QString itemId = notification->value(QStringLiteral("itemId")).toString();
        if (completedReasoningItems_.contains(itemId)) {
            return;
        }
        if (!startedReasoningItems_.contains(itemId)) {
            startedReasoningItems_.insert(itemId);
            reasoningSummaries_.insert(itemId, {});
            emitActiveEvent(domain::AgentEventType::ReasoningStarted,
                            {{QStringLiteral("itemId"), itemId}}, raw);
        }
        if (method == QLatin1String("item/reasoning/summaryPartAdded")) {
            return;
        }
        const QJsonValue summaryIndexValue = notification->value(QStringLiteral("summaryIndex"));
        const qint64 summaryIndex = summaryIndexValue.toInteger(-1);
        if (!summaryIndexValue.isDouble() || summaryIndex < 0) {
            warnActive(QStringLiteral("Ignored invalid Codex reasoning summary index"), raw);
            return;
        }
        QStringList& parts = reasoningSummaries_[itemId];
        while (parts.size() <= summaryIndex) {
            parts.append(QString{});
        }
        const QString delta = notification->value(QStringLiteral("delta")).toString();
        parts[summaryIndex].append(delta);
        if (!delta.isEmpty()) {
            emitActiveEvent(domain::AgentEventType::ReasoningSummaryDelta,
                            {{QStringLiteral("itemId"), itemId},
                             {QStringLiteral("summaryIndex"), summaryIndex},
                             {QStringLiteral("text"), delta}},
                            raw);
        }
        return;
    }

    if (method == QLatin1String("item/reasoning/textDelta")) {
        (void)itemDeltaParams(method);
        return;
    }

    if (method == QLatin1String("item/plan/delta")) {
        const auto notification = itemDeltaParams(method);
        if (!notification.has_value()) {
            return;
        }
        const QString itemId = notification->value(QStringLiteral("itemId")).toString();
        if (!completedPlanItems_.contains(itemId)) {
            emitActiveEvent(
                domain::AgentEventType::PlanUpdated,
                {{QStringLiteral("itemId"), itemId},
                 {QStringLiteral("textDelta"), notification->value(QStringLiteral("delta"))}},
                raw);
        }
        return;
    }

    if (method == QLatin1String("turn/plan/updated")) {
        if (!params.isObject()) {
            warnActive(QStringLiteral("Ignored invalid Codex turn plan: params must be an object"),
                       raw);
            return;
        }
        const QJsonObject notification = params.toObject();
        if (!acceptNativeContext(notification.value(QStringLiteral("threadId")).toString(),
                                 notification.value(QStringLiteral("turnId")).toString(), raw)) {
            return;
        }
        const QJsonValue plan = notification.value(QStringLiteral("plan"));
        if (!plan.isArray()) {
            warnActive(QStringLiteral("Ignored invalid Codex turn plan: plan must be an array"),
                       raw);
            return;
        }
        emitActiveEvent(
            domain::AgentEventType::PlanUpdated,
            {{QStringLiteral("plan"), plan},
             {QStringLiteral("explanation"), notification.value(QStringLiteral("explanation"))}},
            raw);
        return;
    }

    if (method == QLatin1String("item/agentMessage/delta")) {
        QString error;
        const auto notification = parseAgentMessageDelta(params, &error);
        if (!notification.has_value()) {
            warnActive(QStringLiteral("Ignored invalid Codex agent message delta: %1").arg(error),
                       raw);
            return;
        }
        if (!acceptNativeContext(notification->threadId, notification->turnId, raw) ||
            completedAgentMessages_.contains(notification->itemId)) {
            return;
        }
        if (!startedAgentMessages_.contains(notification->itemId)) {
            startedAgentMessages_.insert(notification->itemId);
            streamedAgentText_.insert(notification->itemId, {});
            emitActiveEvent(domain::AgentEventType::AgentMessageStart,
                            {{QStringLiteral("itemId"), notification->itemId}}, raw);
        }
        if (!notification->delta.isEmpty()) {
            streamedAgentText_[notification->itemId].append(notification->delta);
            emitActiveEvent(domain::AgentEventType::AgentMessageDelta,
                            {{QStringLiteral("itemId"), notification->itemId},
                             {QStringLiteral("text"), notification->delta}},
                            raw);
        }
        return;
    }

    if (method == QLatin1String("error")) {
        QString error;
        const auto notification = parseTurnErrorNotification(params, &error);
        if (!notification.has_value()) {
            warnActive(QStringLiteral("Ignored invalid Codex error notification: %1").arg(error),
                       raw);
            return;
        }
        if (!acceptNativeContext(notification->threadId, notification->turnId, raw)) {
            return;
        }
        if (notification->willRetry) {
            warnActive(notification->message.isEmpty()
                           ? QStringLiteral("Codex reported a retryable turn error")
                           : notification->message,
                       raw);
        } else {
            finishActiveTurn(domain::AgentEventType::TurnFailed, notification->message,
                             QStringLiteral("failed"), raw, false);
        }
    }
}

void CodexAdapter::handleServerRequest(const QJsonValue& id, const QString& method,
                                       const QJsonValue& params, const QJsonObject& raw) {
    const bool isApproval = method == QLatin1String("item/commandExecution/requestApproval") ||
                            method == QLatin1String("item/fileChange/requestApproval");
    if (!isApproval) {
        client_.sendErrorResponse(
            id, -32601, QStringLiteral("Snack does not support this Codex server request"));
        warnActive(QStringLiteral("Declined unsupported Codex server request: %1").arg(method),
                   raw);
        return;
    }

    QString error;
    auto request = parseApprovalRequest(id, method, params, &error);
    if (!request.has_value()) {
        client_.sendErrorResponse(id, -32602,
                                  QStringLiteral("Invalid Codex approval request: %1").arg(error));
        warnActive(QStringLiteral("Declined invalid Codex approval request: %1").arg(error), raw);
        return;
    }
    if (!connected_ || activeTurn_.turnId.isNull() ||
        !acceptNativeContext(request->threadId, request->turnId, raw)) {
        (void)client_.sendResponse(id, approvalResponse(domain::ApprovalDecision::Decline));
        return;
    }

    const QString nativeKey = nativeRequestKey(id);
    if (approvalTokenByNativeKey_.contains(nativeKey)) {
        warnActive(QStringLiteral("Ignored duplicate Codex approval request id"), raw);
        return;
    }

    const QString requestId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QJsonObject item = activeItems_.value(request->itemId);
    if (request->command.isEmpty()) {
        request->command = item.value(QStringLiteral("command")).toString();
    }
    if (request->cwd.isEmpty()) {
        request->cwd = item.value(QStringLiteral("cwd")).toString();
    }
    if (request->commandActions.isEmpty()) {
        request->commandActions = item.value(QStringLiteral("commandActions")).toArray();
    }
    pendingApprovals_.insert(requestId, *request);
    approvalTokenByNativeKey_.insert(nativeKey, requestId);
    QJsonObject payload = approvalEventPayload(requestId, *request);
    payload.insert(QStringLiteral("changes"), item.value(QStringLiteral("changes")));
    emitActiveEvent(domain::AgentEventType::ApprovalRequested, payload, raw);
}

void CodexAdapter::handleServerRequestResolved(const QJsonValue& params, const QJsonObject& raw) {
    if (!params.isObject()) {
        warnActive(QStringLiteral("Ignored invalid serverRequest/resolved notification"), raw);
        return;
    }
    const QJsonObject object = params.toObject();
    if (object.value(QStringLiteral("threadId")).toString() != nativeThreadId_) {
        warnActive(QStringLiteral("Ignored resolved request for a different thread"), raw);
        return;
    }
    const QJsonValue nativeId = object.value(QStringLiteral("requestId"));
    const QString nativeKey = nativeRequestKey(nativeId);
    const auto tokenIterator = approvalTokenByNativeKey_.find(nativeKey);
    if (tokenIterator == approvalTokenByNativeKey_.end()) {
        return;
    }
    const QString requestId = *tokenIterator;
    approvalTokenByNativeKey_.erase(tokenIterator);
    pendingApprovals_.remove(requestId);
    emitActiveEvent(domain::AgentEventType::ApprovalResolved,
                    {{QStringLiteral("requestId"), requestId},
                     {QStringLiteral("resolution"), QStringLiteral("cleared")}},
                    raw);
}

bool CodexAdapter::acceptNativeContext(const QString& threadId, const QString& turnId,
                                       const QJsonObject& raw) {
    if (threadId != nativeThreadId_) {
        warnActive(QStringLiteral("Ignored Codex event for a different thread"), raw);
        return false;
    }
    if (nativeTurnId_.isEmpty()) {
        nativeTurnId_ = turnId;
        sendInterruptRequest();
        return true;
    }
    if (turnId != nativeTurnId_) {
        warnActive(QStringLiteral("Ignored Codex event for a different turn"), raw);
        return false;
    }
    return true;
}

void CodexAdapter::sendInterruptRequest() {
    if (!interruptRequested_ || interruptSent_ || activeTurn_.turnId.isNull() ||
        nativeTurnId_.isEmpty() || interruptRequestId_ != 0) {
        return;
    }
    interruptRequestId_ =
        client_.sendRequest(QStringLiteral("turn/interrupt"),
                            makeTurnInterruptParameters(nativeThreadId_, nativeTurnId_));
    interruptSent_ = interruptRequestId_ != 0;
    if (interruptRequestId_ == 0 && !activeTurn_.turnId.isNull()) {
        interruptRequested_ = false;
        warnActive(QStringLiteral("Failed to request Codex turn interruption"));
    }
}

void CodexAdapter::emitActiveEvent(domain::AgentEventType type, const QJsonObject& payload,
                                   const QJsonObject& raw) {
    if (activeTurn_.turnId.isNull()) {
        return;
    }
    domain::AgentEvent event;
    event.turnId = activeTurn_.turnId;
    event.type = type;
    event.payload = payload;
    event.rawPayload = raw;
    emit eventReceived(event);
}

void CodexAdapter::warnActive(const QString& message, const QJsonObject& raw) {
    emitActiveEvent(domain::AgentEventType::WarningRaised, {{QStringLiteral("message"), message}},
                    raw);
}

void CodexAdapter::finishActiveTurn(domain::AgentEventType type, const QString& message,
                                    const QString& nativeStatus, const QJsonObject& raw,
                                    bool interrupted) {
    if (activeTurn_.turnId.isNull()) {
        return;
    }
    const QList<QString> pendingRequestIds = pendingApprovals_.keys();
    for (const QString& requestId : pendingRequestIds) {
        emitActiveEvent(domain::AgentEventType::ApprovalResolved,
                        {{QStringLiteral("requestId"), requestId},
                         {QStringLiteral("resolution"), QStringLiteral("turn-ended")}},
                        raw);
    }
    pendingApprovals_.clear();
    approvalTokenByNativeKey_.clear();
    QJsonObject payload{{QStringLiteral("nativeTurnId"), nativeTurnId_},
                        {QStringLiteral("nativeStatus"), nativeStatus}};
    if (!message.isEmpty()) {
        payload.insert(QStringLiteral("message"), message);
    }
    emitActiveEvent(type, payload, raw);

    const QUuid turnId = activeTurn_.turnId;
    activeTurn_ = {};
    nativeTurnId_.clear();
    startedAgentMessages_.clear();
    completedAgentMessages_.clear();
    streamedAgentText_.clear();
    startedToolItems_.clear();
    completedToolItems_.clear();
    startedReasoningItems_.clear();
    completedReasoningItems_.clear();
    completedPlanItems_.clear();
    reasoningSummaries_.clear();
    activeItems_.clear();
    turnRequestId_ = 0;
    interruptRequestId_ = 0;
    turnStartedEmitted_ = false;
    interruptRequested_ = false;
    interruptSent_ = false;
    emit turnFinished(turnId, interrupted);
}

void CodexAdapter::failConnection(const QString& detail) {
    if (!connecting_ && !connected_) {
        return;
    }
    connecting_ = false;
    connected_ = false;
    if (!activeTurn_.turnId.isNull()) {
        finishActiveTurn(domain::AgentEventType::TurnFailed, detail, QStringLiteral("failed"), {},
                         false);
    }
    if (client_.state() != ConnectionState::Stopped && client_.state() != ConnectionState::Failed) {
        client_.stop();
    }
    emit connectionChanged(false, detail);
}

} // namespace snack::agent::codex
