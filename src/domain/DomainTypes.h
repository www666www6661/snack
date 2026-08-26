#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QString>
#include <QUuid>

namespace snack::domain {

enum class AgentKind { Codex, Claude, Mock };
enum class ReasoningEffort { Minimal, Low, Medium, High, ExtraHigh, Maximum, Ultra };
enum class AccessLevel { Strict, Workspace, Full };
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
    QString workingDirectory;
    AgentKind agentKind{AgentKind::Mock};
    ConversationStatus status{ConversationStatus::Dormant};
    QString nativeThreadId;
    QString nativeSessionId;
    bool archived{false};
    bool pinned{false};
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

[[nodiscard]] QString enumName(AgentKind value);
[[nodiscard]] QString enumName(ReasoningEffort value);
[[nodiscard]] QString enumName(AccessLevel value);
[[nodiscard]] QString enumName(ConversationStatus value);
[[nodiscard]] QString enumName(AgentEventType value);

[[nodiscard]] AgentKind agentKindFromString(const QString& value);
[[nodiscard]] ReasoningEffort reasoningEffortFromString(const QString& value);
[[nodiscard]] AccessLevel accessLevelFromString(const QString& value);
[[nodiscard]] ConversationStatus conversationStatusFromString(const QString& value);
[[nodiscard]] AgentEventType agentEventTypeFromString(const QString& value);

} // namespace snack::domain

Q_DECLARE_METATYPE(snack::domain::AgentEvent)
Q_DECLARE_METATYPE(snack::domain::ConversationStatus)
Q_DECLARE_METATYPE(snack::domain::TurnSettingsSnapshot)
