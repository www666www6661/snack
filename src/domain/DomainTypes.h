#pragma once

#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QUuid>

#include <optional>

namespace snack::domain {

enum class AgentKind { Codex, Claude, Mock };
enum class ReasoningEffort { Minimal, Low, Medium, High, ExtraHigh, Maximum, Ultra };
enum class AccessLevel { Strict, Workspace, Full };
enum class ApprovalDecision { Accept, AcceptForSession, Decline, Cancel };
enum class QueuedMessageState { Pending };
enum class ConversationStatus {
    Dormant,
    Connecting,
    Idle,
    Running,
    WaitingApproval,
    WaitingInput,
    Disconnected,
    Failed,
    Closed
};
enum class AgentEventType {
    UserMessage,
    AgentMessageStart,
    AgentMessageDelta,
    AgentMessageComplete,
    ToolStarted,
    ToolOutputDelta,
    ToolCompleted,
    ReasoningStarted,
    ReasoningSummaryDelta,
    ReasoningCompleted,
    PlanUpdated,
    ApprovalRequested,
    ApprovalResolved,
    UserInputRequested,
    UserInputResolved,
    UsageUpdated,
    TurnStarted,
    TurnCompleted,
    TurnInterrupted,
    TurnFailed,
    CapabilityChanged,
    ConnectionChanged,
    WarningRaised,
    ErrorRaised,
    RawProtocolObserved
};

struct TurnSettingsSnapshot {
    AgentKind agentKind{AgentKind::Mock};
    QString modelId{QStringLiteral("mock-balanced")};
    ReasoningEffort reasoningEffort{ReasoningEffort::Medium};
    AccessLevel accessLevel{AccessLevel::Strict};
    QString workingDirectory;
    QString capabilityVersion{QStringLiteral("mock-v1")};

    [[nodiscard]] QJsonObject toJson() const;
    [[nodiscard]] static TurnSettingsSnapshot fromJson(const QJsonObject& object);
    bool operator==(const TurnSettingsSnapshot&) const = default;
};

struct Conversation {
    QUuid id{QUuid::createUuid()};
    QString title;
    bool titleIsPlaceholder{false};
    QString workingDirectory;
    AgentKind agentKind{AgentKind::Mock};
    QString modelId{QStringLiteral("")};
    ConversationStatus status{ConversationStatus::Dormant};
    QString nativeThreadId;
    QString nativeSessionId;
    bool archived{false};
    bool pinned{false};
    QStringList tags;
};

struct AgentEvent {
    QUuid id{QUuid::createUuid()};
    QUuid conversationId;
    QUuid turnId;
    quint64 sequence{0};
    AgentEventType type{AgentEventType::RawProtocolObserved};
    QJsonObject payload;
    QJsonObject rawPayload;
    QDateTime occurredAt{QDateTime::currentDateTimeUtc()};
};

struct QueuedMessage {
    QUuid id{QUuid::createUuid()};
    QUuid conversationId;
    QString content;
    QJsonArray attachments;
    qsizetype position{0};
    QueuedMessageState state{QueuedMessageState::Pending};

    bool operator==(const QueuedMessage&) const = default;
};

struct PromptTemplate {
    QUuid id{QUuid::createUuid()};
    QString name;
    QString content;
    bool favorite{true};
    qsizetype position{0};

    bool operator==(const PromptTemplate&) const = default;
};

[[nodiscard]] QString enumName(AgentKind value);
[[nodiscard]] QString enumName(ReasoningEffort value);
[[nodiscard]] QString enumName(AccessLevel value);
[[nodiscard]] QString enumName(ApprovalDecision value);
[[nodiscard]] QString enumName(QueuedMessageState value);
[[nodiscard]] QString enumName(ConversationStatus value);
[[nodiscard]] QString enumName(AgentEventType value);

[[nodiscard]] AgentKind agentKindFromString(const QString& value);
[[nodiscard]] ReasoningEffort reasoningEffortFromString(const QString& value);
[[nodiscard]] AccessLevel accessLevelFromString(const QString& value);
[[nodiscard]] ApprovalDecision approvalDecisionFromString(const QString& value);
[[nodiscard]] QueuedMessageState queuedMessageStateFromString(const QString& value);
[[nodiscard]] ConversationStatus conversationStatusFromString(const QString& value);
[[nodiscard]] AgentEventType agentEventTypeFromString(const QString& value);
[[nodiscard]] QString fallbackConversationTitle(const QString& prompt);
[[nodiscard]] std::optional<QStringList> normalizeConversationTags(const QStringList& tags,
                                                                   QString* error = nullptr);

} // namespace snack::domain

Q_DECLARE_METATYPE(snack::domain::AgentEvent)
Q_DECLARE_METATYPE(snack::domain::ConversationStatus)
Q_DECLARE_METATYPE(snack::domain::TurnSettingsSnapshot)
Q_DECLARE_METATYPE(snack::domain::QueuedMessage)
Q_DECLARE_METATYPE(snack::domain::PromptTemplate)
