#include "agent/claude/ClaudeAdapter.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMimeDatabase>
#include <QUuid>

#include <algorithm>
#include <utility>

namespace snack::agent::claude {
namespace {

constexpr qint64 maximumImageBytes = 20 * 1024 * 1024;

QList<ReasoningEffortCapability> claudeEffortCapabilities() {
    return {{QStringLiteral("low"), {}},
            {QStringLiteral("medium"), {}},
            {QStringLiteral("high"), {}},
            {QStringLiteral("xhigh"), {}},
            {QStringLiteral("max"), {}}};
}

ModelCapability claudeModel(const QString& id, const QString& displayName, bool isDefault = false) {
    return {.id = id,
            .displayName = displayName,
            .defaultReasoningEffortId = QStringLiteral("medium"),
            .supportedReasoningEfforts = claudeEffortCapabilities(),
            .inputModalities = {QStringLiteral("text"), QStringLiteral("image")},
            .isDefault = isDefault};
}

Qt::CaseSensitivity pathCaseSensitivity() {
#ifdef Q_OS_WIN
    return Qt::CaseInsensitive;
#else
    return Qt::CaseSensitive;
#endif
}

} // namespace

ClaudeAdapter::ClaudeAdapter(CliInstallation installation, process::IProcessTransport* transport,
                             QObject* parent)
    : IAgentAdapter(parent), installation_(std::move(installation)), client_(transport, this) {
    capabilities_.version = installation_.version.isEmpty()
                                ? QStringLiteral("claude-stream/unknown")
                                : QStringLiteral("claude-stream/%1").arg(installation_.version);
    capabilities_.models = {QStringLiteral("sonnet"), QStringLiteral("opus"),
                            QStringLiteral("haiku")};
    capabilities_.defaultModelId = QStringLiteral("sonnet");
    capabilities_.modelCapabilities = {
        claudeModel(QStringLiteral("sonnet"), QStringLiteral("Claude Sonnet"), true),
        claudeModel(QStringLiteral("opus"), QStringLiteral("Claude Opus")),
        claudeModel(QStringLiteral("haiku"), QStringLiteral("Claude Haiku")),
    };
    capabilities_.reasoningEfforts = {
        domain::ReasoningEffort::Low,     domain::ReasoningEffort::Medium,
        domain::ReasoningEffort::High,    domain::ReasoningEffort::ExtraHigh,
        domain::ReasoningEffort::Maximum,
    };
    capabilities_.accessLevels = {domain::AccessLevel::Strict, domain::AccessLevel::Workspace,
                                  domain::AccessLevel::Full};
    capabilities_.supportsSteering = false;
    capabilities_.supportsInterrupt = true;

    connect(&client_, &ClaudeStreamClient::initialized, this, &ClaudeAdapter::handleInitialized);
    connect(&client_, &ClaudeStreamClient::recordReceived, this, &ClaudeAdapter::handleRecord);
    connect(&client_, &ClaudeStreamClient::failureOccurred, this, &ClaudeAdapter::failConnection);
    connect(&client_, &ClaudeStreamClient::stateChanged, this, [this](StreamState state) {
        if (state != StreamState::Stopped || !closing_) {
            return;
        }
        const bool wasActive = connecting_ || connected_;
        connecting_ = false;
        connected_ = false;
        closing_ = false;
        if (wasActive) {
            emit connectionChanged(false, QStringLiteral("closed"));
        }
    });
}

domain::AgentKind ClaudeAdapter::kind() const { return domain::AgentKind::Claude; }

CapabilitySet ClaudeAdapter::capabilities() const { return capabilities_; }

void ClaudeAdapter::connectAgent(const AgentConnectionRequest& request) {
    if (connecting_ || connected_ || closing_) {
        return;
    }
    if (request.workingDirectory.trimmed().isEmpty()) {
        failConnection(QStringLiteral("Claude working directory is missing"));
        return;
    }
    if (!installation_.isUsable()) {
        failConnection(installation_.detail.isEmpty() ? QStringLiteral("Claude CLI is unavailable")
                                                      : installation_.detail);
        return;
    }

    connectionRequest_ = request;
    processSettings_ = request.settings;
    if (!capabilities_.models.contains(processSettings_.modelId)) {
        processSettings_.modelId = capabilities_.defaultModelId;
    }
    expectedSessionId_ = request.nativeThreadId.trimmed();
    const bool resume = !expectedSessionId_.isEmpty();
    if (!resume) {
        expectedSessionId_ = QUuid::createUuid().toString(QUuid::WithoutBraces);
    }

    connecting_ = true;
    const SessionLaunchOptions options{
        .workingDirectory = request.workingDirectory,
        .sessionId = expectedSessionId_,
        .modelId = processSettings_.modelId,
        .reasoningEffort = processSettings_.reasoningEffort,
        .accessLevel = processSettings_.accessLevel,
    };
    if (!client_.start(ClaudeCliDiscovery::sessionLaunchSpec(installation_, options, resume))) {
        failConnection(QStringLiteral("Claude process is still stopping"));
    }
}

void ClaudeAdapter::startTurn(const TurnRequest& request) {
    const auto reject = [this, &request](const QString& detail) {
        domain::AgentEvent event;
        event.turnId = request.turnId;
        event.type = domain::AgentEventType::TurnFailed;
        event.payload = {{QStringLiteral("message"), detail}};
        emit eventReceived(event);
        emit turnFinished(request.turnId, false, false);
    };
    if (!connected_ || client_.state() != StreamState::Ready) {
        reject(QStringLiteral("Claude is not connected to a native session"));
        return;
    }
    if (!activeTurn_.turnId.isNull()) {
        reject(QStringLiteral("A Claude turn is already active"));
        return;
    }
    if (request.turnId.isNull() ||
        (request.message.trimmed().isEmpty() && request.attachments.isEmpty())) {
        reject(QStringLiteral("Claude turn input is invalid"));
        return;
    }
    if (request.settings.agentKind != domain::AgentKind::Claude ||
        !capabilities_.models.contains(request.settings.modelId) ||
        !capabilities_.reasoningEfforts.contains(request.settings.reasoningEffort) ||
        !capabilities_.accessLevels.contains(request.settings.accessLevel)) {
        reject(QStringLiteral("Claude turn settings are unsupported"));
        return;
    }
    if (QDir::cleanPath(request.settings.workingDirectory)
            .compare(QDir::cleanPath(connectionRequest_.workingDirectory), pathCaseSensitivity()) !=
        0) {
        reject(QStringLiteral("Claude turn settings use an unexpected working directory"));
        return;
    }
    if (request.settings.modelId != processSettings_.modelId ||
        request.settings.reasoningEffort != processSettings_.reasoningEffort ||
        request.settings.accessLevel != processSettings_.accessLevel) {
        reject(QStringLiteral("Claude process settings must be applied before the next turn"));
        return;
    }

    QString error;
    const QJsonObject envelope = makeUserEnvelope(request, &error);
    if (envelope.isEmpty()) {
        reject(error);
        return;
    }

    activeTurn_ = request;
    eventMapper_.reset();
    nativeUserMessageUuid_ = request.turnId.toString(QUuid::WithoutBraces);
    if (!client_.sendEnvelope(envelope)) {
        if (!activeTurn_.turnId.isNull()) {
            finishActiveTurn(domain::AgentEventType::TurnFailed,
                             QStringLiteral("Failed to send Claude turn"), {}, false, false);
        }
        return;
    }
    emitActiveEvent(domain::AgentEventType::TurnStarted,
                    {{QStringLiteral("nativeUserMessageUuid"), nativeUserMessageUuid_},
                     {QStringLiteral("settings"), request.settings.toJson()}});
}

bool ClaudeAdapter::steerTurn(const SteerRequest& request) {
    Q_UNUSED(request)
    return false;
}

bool ClaudeAdapter::respondToApproval(const QString& requestId, domain::ApprovalDecision decision) {
    Q_UNUSED(requestId)
    Q_UNUSED(decision)
    return false;
}

bool ClaudeAdapter::respondToUserInput(const QString& requestId, const QJsonObject& answers) {
    const auto pending = pendingUserInputs_.constFind(requestId);
    if (pending == pendingUserInputs_.cend() || activeTurn_.turnId.isNull()) {
        return false;
    }
    const QJsonArray questions = pending->value(QStringLiteral("questions")).toArray();
    if (answers.size() != questions.size()) {
        return false;
    }
    for (const QJsonValue& questionValue : questions) {
        const QString id = questionValue.toObject().value(QStringLiteral("id")).toString();
        const QJsonValue answer = answers.value(id);
        if (!answer.isObject() || !answer.toObject().value(QStringLiteral("answers")).isArray() ||
            answer.toObject().value(QStringLiteral("answers")).toArray().isEmpty()) {
            return false;
        }
        for (const QJsonValue& value :
             answer.toObject().value(QStringLiteral("answers")).toArray()) {
            if (!value.isString()) {
                return false;
            }
        }
    }
    if (!client_.sendEnvelope(makeUserInputEnvelope(requestId, *pending, answers))) {
        return false;
    }
    pendingUserInputs_.remove(requestId);
    return true;
}

void ClaudeAdapter::interruptTurn() {}

void ClaudeAdapter::closeAgent() {
    if (closing_ || (!connecting_ && !connected_)) {
        return;
    }
    if (!activeTurn_.turnId.isNull()) {
        finishActiveTurn(domain::AgentEventType::TurnInterrupted,
                         QStringLiteral("Claude connection closed"), {}, true, false);
    }
    closing_ = true;
    if (client_.state() == StreamState::Stopped) {
        connecting_ = false;
        connected_ = false;
        closing_ = false;
        emit connectionChanged(false, QStringLiteral("closed"));
        return;
    }
    client_.stop();
}

QJsonObject ClaudeAdapter::makeUserEnvelope(const TurnRequest& request, QString* error) const {
    QJsonArray content;
    if (!request.message.isEmpty()) {
        content.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("text")},
                                   {QStringLiteral("text"), request.message}});
    }
    for (const QJsonValue& value : request.attachments) {
        const QJsonObject attachment = value.toObject();
        const QString path = attachment.value(QStringLiteral("path")).toString();
        if (path.isEmpty()) {
            if (error != nullptr) {
                *error = QStringLiteral("Claude attachment path is missing");
            }
            return {};
        }
        if (attachment.value(QStringLiteral("kind")) != QLatin1String("image")) {
            content.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("text")},
                                       {QStringLiteral("text"),
                                        QStringLiteral("Attached file reference: @%1").arg(path)}});
            continue;
        }

        QFile file(path);
        const QFileInfo info(file);
        if (!info.isFile() || info.size() > maximumImageBytes || !file.open(QIODevice::ReadOnly)) {
            if (error != nullptr) {
                *error = QStringLiteral("Claude image attachment is unavailable or exceeds 20 MiB");
            }
            return {};
        }
        const QByteArray bytes = file.readAll();
        const QString mediaType = QMimeDatabase().mimeTypeForFile(info).name();
        if (!mediaType.startsWith(QStringLiteral("image/"))) {
            if (error != nullptr) {
                *error = QStringLiteral("Claude image attachment has an unsupported media type");
            }
            return {};
        }
        content.append(QJsonObject{
            {QStringLiteral("type"), QStringLiteral("image")},
            {QStringLiteral("source"),
             QJsonObject{{QStringLiteral("type"), QStringLiteral("base64")},
                         {QStringLiteral("media_type"), mediaType},
                         {QStringLiteral("data"), QString::fromLatin1(bytes.toBase64())}}}});
    }
    if (content.isEmpty()) {
        if (error != nullptr) {
            *error = QStringLiteral("Claude turn has no input content");
        }
        return {};
    }

    return {
        {QStringLiteral("type"), QStringLiteral("user")},
        {QStringLiteral("uuid"), request.turnId.toString(QUuid::WithoutBraces)},
        {QStringLiteral("session_id"), expectedSessionId_},
        {QStringLiteral("parent_tool_use_id"), QJsonValue::Null},
        {QStringLiteral("message"), QJsonObject{{QStringLiteral("role"), QStringLiteral("user")},
                                                {QStringLiteral("content"), content}}}};
}

QJsonObject ClaudeAdapter::makeUserInputEnvelope(const QString& requestId,
                                                 const QJsonObject& request,
                                                 const QJsonObject& answers) const {
    QJsonObject responseAnswers;
    for (const QJsonValue& questionValue : request.value(QStringLiteral("questions")).toArray()) {
        const QJsonObject question = questionValue.toObject();
        const QJsonArray values = answers.value(question.value(QStringLiteral("id")).toString())
                                      .toObject()
                                      .value(QStringLiteral("answers"))
                                      .toArray();
        QStringList text;
        for (const QJsonValue& value : values) {
            text.append(value.toString());
        }
        responseAnswers.insert(question.value(QStringLiteral("question")).toString(),
                               text.join(QStringLiteral(", ")));
    }
    const QString content =
        QString::fromUtf8(QJsonDocument(QJsonObject{{QStringLiteral("answers"), responseAnswers}})
                              .toJson(QJsonDocument::Compact));
    return {{QStringLiteral("type"), QStringLiteral("user")},
            {QStringLiteral("uuid"), QUuid::createUuid().toString(QUuid::WithoutBraces)},
            {QStringLiteral("session_id"), expectedSessionId_},
            {QStringLiteral("parent_tool_use_id"), QJsonValue::Null},
            {QStringLiteral("message"),
             QJsonObject{
                 {QStringLiteral("role"), QStringLiteral("user")},
                 {QStringLiteral("content"),
                  QJsonArray{QJsonObject{{QStringLiteral("type"), QStringLiteral("tool_result")},
                                         {QStringLiteral("tool_use_id"), requestId},
                                         {QStringLiteral("content"), content}}}}}}};
}

void ClaudeAdapter::handleInitialized(const InitInfo& info) {
    if (!connecting_) {
        return;
    }
    if (info.sessionId != expectedSessionId_) {
        failConnection(QStringLiteral("Claude initialized an unexpected session"));
        return;
    }
    if (QDir::cleanPath(info.workingDirectory)
            .compare(QDir::cleanPath(connectionRequest_.workingDirectory), pathCaseSensitivity()) !=
        0) {
        failConnection(QStringLiteral("Claude initialized in an unexpected working directory"));
        return;
    }

    const auto model =
        std::find(capabilities_.models.cbegin(), capabilities_.models.cend(), info.modelId);
    if (!info.modelId.isEmpty() && model == capabilities_.models.cend()) {
        capabilities_.models.append(info.modelId);
        capabilities_.modelCapabilities.append(claudeModel(info.modelId, info.modelId));
    }
    capabilities_.version = QStringLiteral("claude-stream/%1").arg(info.cliVersion);
    connecting_ = false;
    connected_ = true;
    emit capabilitiesChanged(capabilities_);
    emit nativeIdentityChanged(info.sessionId, info.sessionId);
    emit connectionChanged(true, capabilities_.version);
}

void ClaudeAdapter::handleRecord(const StreamRecord& record) {
    if (!connected_ || activeTurn_.turnId.isNull()) {
        return;
    }
    for (const MappedEvent& event : eventMapper_.consume(record)) {
        if (event.type == domain::AgentEventType::UserInputRequested) {
            const QString requestId = event.payload.value(QStringLiteral("requestId")).toString();
            if (!requestId.isEmpty() && !pendingUserInputs_.contains(requestId)) {
                pendingUserInputs_.insert(requestId, event.payload);
            }
        }
        emitActiveEvent(event.type, event.payload, event.rawPayload);
    }
    if (record.kind != StreamRecordKind::Result) {
        return;
    }

    const QString userMessageUuid =
        record.payload.value(QStringLiteral("user_message_uuid")).toString();
    if (userMessageUuid != nativeUserMessageUuid_) {
        emitActiveEvent(domain::AgentEventType::WarningRaised,
                        {{QStringLiteral("message"),
                          QStringLiteral("Ignored a Claude result for an unknown user message")}},
                        sanitizedClaudePayload(record.payload));
        return;
    }
    const bool failed = record.payload.value(QStringLiteral("is_error")).toBool() ||
                        record.payload.value(QStringLiteral("subtype")) != QLatin1String("success");
    if (failed) {
        QString message = record.payload.value(QStringLiteral("result")).toString();
        if (message.isEmpty()) {
            message = QStringLiteral("Claude turn failed");
        }
        finishActiveTurn(domain::AgentEventType::TurnFailed, message,
                         sanitizedClaudePayload(record.payload), false, false);
        return;
    }
    finishActiveTurn(domain::AgentEventType::TurnCompleted, {},
                     sanitizedClaudePayload(record.payload), false, true);
}

void ClaudeAdapter::finishActiveTurn(domain::AgentEventType type, const QString& message,
                                     const QJsonObject& raw, bool interrupted, bool completed) {
    if (activeTurn_.turnId.isNull()) {
        return;
    }
    QJsonObject payload;
    if (!message.isEmpty()) {
        payload.insert(QStringLiteral("message"), message);
    }
    emitActiveEvent(type, payload, raw);
    const QUuid turnId = activeTurn_.turnId;
    activeTurn_ = {};
    nativeUserMessageUuid_.clear();
    pendingUserInputs_.clear();
    emit turnFinished(turnId, interrupted, completed);
}

void ClaudeAdapter::emitActiveEvent(domain::AgentEventType type, const QJsonObject& payload,
                                    const QJsonObject& raw) {
    domain::AgentEvent event;
    event.turnId = activeTurn_.turnId;
    event.type = type;
    event.payload = payload;
    event.rawPayload = raw;
    emit eventReceived(event);
}

void ClaudeAdapter::failConnection(const QString& detail) {
    if (closing_) {
        return;
    }
    if (!activeTurn_.turnId.isNull()) {
        finishActiveTurn(domain::AgentEventType::TurnFailed, detail, {}, false, false);
    }
    const bool wasActive = connecting_ || connected_;
    connecting_ = false;
    connected_ = false;
    if (client_.state() != StreamState::Failed && client_.state() != StreamState::Stopped) {
        client_.stop();
    }
    if (wasActive || !detail.isEmpty()) {
        emit connectionChanged(false, detail);
    }
}

} // namespace snack::agent::claude
