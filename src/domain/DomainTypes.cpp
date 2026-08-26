#include "domain/DomainTypes.h"

namespace snack::domain {
namespace {

template <typename Enum> struct EnumName {
    Enum value;
    const char* name;
};

constexpr EnumName<AgentKind> agentKinds[] = {
    {AgentKind::Codex, "codex"}, {AgentKind::Claude, "claude"}, {AgentKind::Mock, "mock"}};
constexpr EnumName<ReasoningEffort> reasoningEfforts[] = {
    {ReasoningEffort::Minimal, "minimal"}, {ReasoningEffort::Low, "low"},
    {ReasoningEffort::Medium, "medium"},   {ReasoningEffort::High, "high"},
    {ReasoningEffort::ExtraHigh, "xhigh"}, {ReasoningEffort::Maximum, "max"},
    {ReasoningEffort::Ultra, "ultra"}};
constexpr EnumName<AccessLevel> accessLevels[] = {{AccessLevel::Strict, "strict"},
                                                  {AccessLevel::Workspace, "workspace"},
                                                  {AccessLevel::Full, "full"}};
constexpr EnumName<ApprovalDecision> approvalDecisions[] = {
    {ApprovalDecision::Accept, "accept"},
    {ApprovalDecision::AcceptForSession, "acceptForSession"},
    {ApprovalDecision::Decline, "decline"},
    {ApprovalDecision::Cancel, "cancel"}};
constexpr EnumName<AgentEventType> eventTypes[] = {
    {AgentEventType::UserMessage, "user-message"},
    {AgentEventType::AgentMessageStart, "agent-message-start"},
    {AgentEventType::AgentMessageDelta, "agent-message-delta"},
    {AgentEventType::AgentMessageComplete, "agent-message-complete"},
    {AgentEventType::ToolStarted, "tool-started"},
    {AgentEventType::ToolOutputDelta, "tool-output-delta"},
    {AgentEventType::ToolCompleted, "tool-completed"},
    {AgentEventType::ReasoningStarted, "reasoning-started"},
    {AgentEventType::ReasoningSummaryDelta, "reasoning-summary-delta"},
    {AgentEventType::ReasoningCompleted, "reasoning-completed"},
    {AgentEventType::PlanUpdated, "plan-updated"},
    {AgentEventType::ApprovalRequested, "approval-requested"},
    {AgentEventType::ApprovalResolved, "approval-resolved"},
    {AgentEventType::TurnStarted, "turn-started"},
    {AgentEventType::TurnCompleted, "turn-completed"},
    {AgentEventType::TurnInterrupted, "turn-interrupted"},
    {AgentEventType::TurnFailed, "turn-failed"},
    {AgentEventType::CapabilityChanged, "capability-changed"},
    {AgentEventType::ConnectionChanged, "connection-changed"},
    {AgentEventType::WarningRaised, "warning-raised"},
    {AgentEventType::ErrorRaised, "error-raised"},
    {AgentEventType::RawProtocolObserved, "raw-protocol-observed"}};

template <typename Enum, qsizetype Size>
QString enumToString(Enum value, const EnumName<Enum> (&values)[Size]) {
    for (const auto& item : values) {
        if (item.value == value) {
            return QString::fromLatin1(item.name);
        }
    }
    return QStringLiteral("unknown");
}

template <typename Enum, qsizetype Size>
Enum enumFromString(const QString& value, const EnumName<Enum> (&values)[Size], Enum fallback) {
    for (const auto& item : values) {
        if (value == QLatin1String(item.name)) {
            return item.value;
        }
    }
    return fallback;
}

} // namespace

QJsonObject TurnSettingsSnapshot::toJson() const {
    return {{QStringLiteral("agentKind"), enumName(agentKind)},
            {QStringLiteral("modelId"), modelId},
            {QStringLiteral("reasoningEffort"), enumName(reasoningEffort)},
            {QStringLiteral("accessLevel"), enumName(accessLevel)},
            {QStringLiteral("workingDirectory"), workingDirectory},
            {QStringLiteral("capabilityVersion"), capabilityVersion}};
}

TurnSettingsSnapshot TurnSettingsSnapshot::fromJson(const QJsonObject& object) {
    TurnSettingsSnapshot result;
    result.agentKind = agentKindFromString(object.value(QStringLiteral("agentKind")).toString());
    result.modelId = object.value(QStringLiteral("modelId")).toString(result.modelId);
    result.reasoningEffort =
        reasoningEffortFromString(object.value(QStringLiteral("reasoningEffort")).toString());
    result.accessLevel =
        accessLevelFromString(object.value(QStringLiteral("accessLevel")).toString());
    result.workingDirectory = object.value(QStringLiteral("workingDirectory")).toString();
    result.capabilityVersion =
        object.value(QStringLiteral("capabilityVersion")).toString(result.capabilityVersion);
    return result;
}

QString enumName(AgentKind value) { return enumToString(value, agentKinds); }
QString enumName(ReasoningEffort value) { return enumToString(value, reasoningEfforts); }
QString enumName(AccessLevel value) { return enumToString(value, accessLevels); }
QString enumName(ApprovalDecision value) { return enumToString(value, approvalDecisions); }

QString enumName(ConversationStatus value) {
    switch (value) {
    case ConversationStatus::Dormant:
        return QStringLiteral("dormant");
    case ConversationStatus::Connecting:
        return QStringLiteral("connecting");
    case ConversationStatus::Idle:
        return QStringLiteral("idle");
    case ConversationStatus::Running:
        return QStringLiteral("running");
    case ConversationStatus::WaitingApproval:
        return QStringLiteral("waiting-approval");
    case ConversationStatus::WaitingInput:
        return QStringLiteral("waiting-input");
    case ConversationStatus::Disconnected:
        return QStringLiteral("disconnected");
    case ConversationStatus::Failed:
        return QStringLiteral("failed");
    case ConversationStatus::Closed:
        return QStringLiteral("closed");
    }
    return QStringLiteral("unknown");
}

QString enumName(AgentEventType value) { return enumToString(value, eventTypes); }

AgentKind agentKindFromString(const QString& value) {
    return enumFromString(value, agentKinds, AgentKind::Mock);
}

ReasoningEffort reasoningEffortFromString(const QString& value) {
    if (value == QLatin1String("extra-high")) {
        return ReasoningEffort::ExtraHigh;
    }
    return enumFromString(value, reasoningEfforts, ReasoningEffort::Medium);
}

AccessLevel accessLevelFromString(const QString& value) {
    return enumFromString(value, accessLevels, AccessLevel::Strict);
}

ApprovalDecision approvalDecisionFromString(const QString& value) {
    return enumFromString(value, approvalDecisions, ApprovalDecision::Decline);
}

ConversationStatus conversationStatusFromString(const QString& value) {
    if (value == QLatin1String("connecting")) {
        return ConversationStatus::Connecting;
    }
    if (value == QLatin1String("idle")) {
        return ConversationStatus::Idle;
    }
    if (value == QLatin1String("running")) {
        return ConversationStatus::Running;
    }
    if (value == QLatin1String("waiting-approval")) {
        return ConversationStatus::WaitingApproval;
    }
    if (value == QLatin1String("waiting-input")) {
        return ConversationStatus::WaitingInput;
    }
    if (value == QLatin1String("disconnected")) {
        return ConversationStatus::Disconnected;
    }
    if (value == QLatin1String("failed")) {
        return ConversationStatus::Failed;
    }
    if (value == QLatin1String("closed")) {
        return ConversationStatus::Closed;
    }
    return ConversationStatus::Dormant;
}

AgentEventType agentEventTypeFromString(const QString& value) {
    return enumFromString(value, eventTypes, AgentEventType::RawProtocolObserved);
}

} // namespace snack::domain
