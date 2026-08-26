#include "agent/FakeAgentAdapter.h"

namespace snack::agent {

FakeAgentAdapter::FakeAgentAdapter(QObject* parent, int chunkDelayMs)
    : IAgentAdapter(parent), chunkDelayMs_(chunkDelayMs) {
    timer_.setSingleShot(true);
    connect(&timer_, &QTimer::timeout, this, &FakeAgentAdapter::emitNextChunk);
}

domain::AgentKind FakeAgentAdapter::kind() const { return domain::AgentKind::Mock; }

CapabilitySet FakeAgentAdapter::capabilities() const {
    return {.version = QStringLiteral("mock-v1"),
            .models = {QStringLiteral("mock-fast"), QStringLiteral("mock-balanced")},
            .defaultModelId = QStringLiteral("mock-balanced"),
            .modelCapabilities = {{.id = QStringLiteral("mock-fast"),
                                   .displayName = QStringLiteral("Mock Fast"),
                                   .defaultReasoningEffortId = QStringLiteral("low"),
                                   .supportedReasoningEfforts = {{QStringLiteral("low"), {}},
                                                                 {QStringLiteral("medium"), {}}}},
                                  {.id = QStringLiteral("mock-balanced"),
                                   .displayName = QStringLiteral("Mock Balanced"),
                                   .defaultReasoningEffortId = QStringLiteral("medium"),
                                   .supportedReasoningEfforts = {{QStringLiteral("low"), {}},
                                                                 {QStringLiteral("medium"), {}},
                                                                 {QStringLiteral("high"), {}}},
                                   .isDefault = true}},
            .reasoningEfforts = {domain::ReasoningEffort::Low, domain::ReasoningEffort::Medium,
                                 domain::ReasoningEffort::High},
            .accessLevels = {domain::AccessLevel::Strict, domain::AccessLevel::Workspace,
                             domain::AccessLevel::Full},
            .supportsSteering = true,
            .supportsInterrupt = true};
}

AgentConnectionRequest FakeAgentAdapter::lastConnectionRequest() const {
    return lastConnectionRequest_;
}

void FakeAgentAdapter::connectAgent(const AgentConnectionRequest& request) {
    lastConnectionRequest_ = request;
    QTimer::singleShot(0, this, [this] {
        connected_ = true;
        emit connectionChanged(true, QStringLiteral("mock-v1"));
    });
}

void FakeAgentAdapter::startTurn(const TurnRequest& request) {
    if (!connected_ || timer_.isActive() || !activeRequest_.turnId.isNull()) {
        return;
    }

    activeRequest_ = request;
    chunks_ = {QStringLiteral("模拟 Agent 已收到你的消息。"),
               QStringLiteral("\n\n当前工程骨架已经支持流式事件、"),
               QStringLiteral("设置快照和本地事件恢复。")};
    chunkIndex_ = 0;
    emitEvent(domain::AgentEventType::TurnStarted,
              {{QStringLiteral("settings"), request.settings.toJson()}});
    emitEvent(domain::AgentEventType::AgentMessageStart);
    timer_.start(chunkDelayMs_);
}

void FakeAgentAdapter::interruptTurn() {
    if (activeRequest_.turnId.isNull()) {
        return;
    }
    timer_.stop();
    emitEvent(domain::AgentEventType::TurnInterrupted);
    const QUuid turnId = activeRequest_.turnId;
    activeRequest_ = TurnRequest{};
    chunks_.clear();
    emit turnFinished(turnId, true);
}

void FakeAgentAdapter::closeAgent() {
    interruptTurn();
    if (connected_) {
        connected_ = false;
        emit connectionChanged(false, QStringLiteral("closed"));
    }
}

void FakeAgentAdapter::emitNextChunk() {
    if (activeRequest_.turnId.isNull()) {
        return;
    }
    if (chunkIndex_ < chunks_.size()) {
        emitEvent(domain::AgentEventType::AgentMessageDelta,
                  {{QStringLiteral("text"), chunks_.at(chunkIndex_)}});
        ++chunkIndex_;
        timer_.start(chunkDelayMs_);
        return;
    }

    emitEvent(domain::AgentEventType::AgentMessageComplete);
    emitEvent(domain::AgentEventType::TurnCompleted);
    const QUuid turnId = activeRequest_.turnId;
    activeRequest_ = TurnRequest{};
    chunks_.clear();
    emit turnFinished(turnId, false);
}

void FakeAgentAdapter::emitEvent(domain::AgentEventType type, const QJsonObject& payload) {
    domain::AgentEvent event;
    event.turnId = activeRequest_.turnId;
    event.type = type;
    event.payload = payload;
    emit eventReceived(event);
}

} // namespace snack::agent
