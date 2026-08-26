#include "agent/codex/CodexAdapter.h"

#include <QJsonObject>
#include <QSet>
#include <QTimer>

#include <algorithm>
#include <utility>

namespace snack::agent::codex {

CodexAdapter::CodexAdapter(CliInstallation installation, process::IProcessTransport* transport,
                           QObject* parent)
    : IAgentAdapter(parent), installation_(std::move(installation)), client_(transport, this) {
    capabilities_.version = installation_.version.isEmpty()
                                ? QStringLiteral("codex-app-server/unknown")
                                : QStringLiteral("codex-app-server/%1").arg(installation_.version);
    capabilities_.accessLevels = {domain::AccessLevel::Strict, domain::AccessLevel::Workspace,
                                  domain::AccessLevel::Full};
    capabilities_.supportsSteering = false;
    capabilities_.supportsInterrupt = false;

    connect(&client_, &CodexAppServerClient::handshakeCompleted, this,
            [this](const ServerInfo&) { requestModelPage(); });
    connect(&client_, &CodexAppServerClient::responseReceived, this, &CodexAdapter::handleResponse);
    connect(&client_, &CodexAppServerClient::requestFailed, this,
            &CodexAdapter::handleRequestFailure);
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

void CodexAdapter::connectAgent(const QString& workingDirectory) {
    if (connecting_ || connected_ || closing_) {
        return;
    }
    workingDirectory_ = workingDirectory;
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
        if (!installation_.isUsable()) {
            failConnection(installation_.detail.isEmpty()
                               ? QStringLiteral("Codex app-server is unavailable")
                               : installation_.detail);
            return;
        }
        client_.start(CodexCliDiscovery::appServerLaunchSpec(installation_, workingDirectory_));
    });
}

void CodexAdapter::startTurn(const TurnRequest& request) {
    domain::AgentEvent event;
    event.turnId = request.turnId;
    event.type = domain::AgentEventType::TurnFailed;
    event.payload = {
        {QStringLiteral("message"),
         QStringLiteral("Codex turn execution is not available in this protocol slice")}};
    emit eventReceived(event);
    emit turnFinished(request.turnId, false);
}

void CodexAdapter::interruptTurn() {}

void CodexAdapter::closeAgent() {
    if (closing_) {
        return;
    }
    if (!connecting_ && !connected_) {
        return;
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
    if (!connecting_ || id != modelRequestId_ || method != QLatin1String("model/list")) {
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
}

void CodexAdapter::handleRequestFailure(qint64 id, const QString& method, int code,
                                        const QString& message) {
    if (connecting_ && id == modelRequestId_ && method == QLatin1String("model/list")) {
        failConnection(
            QStringLiteral("Codex model catalog request failed (%1): %2").arg(code).arg(message));
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

    connecting_ = false;
    connected_ = true;
    emit capabilitiesChanged(capabilities_);
    emit connectionChanged(true, capabilities_.version);
}

void CodexAdapter::failConnection(const QString& detail) {
    if (!connecting_ && !connected_) {
        return;
    }
    connecting_ = false;
    connected_ = false;
    if (client_.state() != ConnectionState::Stopped && client_.state() != ConnectionState::Failed) {
        client_.stop();
    }
    emit connectionChanged(false, detail);
}

} // namespace snack::agent::codex
