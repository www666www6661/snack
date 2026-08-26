#include "domain/DomainTypes.h"

#include <QTest>

class TestDomainTypes final : public QObject {
    Q_OBJECT

  private slots:
    void settingsSnapshotRoundTrips();
    void reasoningEffortNamesRoundTrip();
    void conversationStatusNamesRoundTrip();
    void unknownValuesUseSafeDefaults();
    void eventTypeNamesRoundTrip();
    void approvalDecisionNamesRoundTrip();
};

void TestDomainTypes::settingsSnapshotRoundTrips() {
    snack::domain::TurnSettingsSnapshot original;
    original.agentKind = snack::domain::AgentKind::Codex;
    original.modelId = QStringLiteral("gpt-test");
    original.reasoningEffort = snack::domain::ReasoningEffort::High;
    original.accessLevel = snack::domain::AccessLevel::Workspace;
    original.workingDirectory = QStringLiteral("/tmp/project");
    original.capabilityVersion = QStringLiteral("v7");

    QCOMPARE(snack::domain::TurnSettingsSnapshot::fromJson(original.toJson()), original);
}

void TestDomainTypes::reasoningEffortNamesRoundTrip() {
    using enum snack::domain::ReasoningEffort;
    const QList<snack::domain::ReasoningEffort> values = {Minimal,   Low,     Medium, High,
                                                          ExtraHigh, Maximum, Ultra};
    for (const auto value : values) {
        QCOMPARE(snack::domain::reasoningEffortFromString(snack::domain::enumName(value)), value);
    }
    QCOMPARE(snack::domain::enumName(ExtraHigh), QStringLiteral("xhigh"));
    QCOMPARE(snack::domain::reasoningEffortFromString(QStringLiteral("extra-high")), ExtraHigh);
}

void TestDomainTypes::conversationStatusNamesRoundTrip() {
    using enum snack::domain::ConversationStatus;
    const QList<snack::domain::ConversationStatus> values = {
        Dormant,      Connecting,   Idle,   Running, WaitingApproval,
        WaitingInput, Disconnected, Failed, Closed};
    for (const auto value : values) {
        QCOMPARE(snack::domain::conversationStatusFromString(snack::domain::enumName(value)),
                 value);
    }
    QCOMPARE(snack::domain::conversationStatusFromString(QStringLiteral("future")), Dormant);
}

void TestDomainTypes::unknownValuesUseSafeDefaults() {
    QCOMPARE(snack::domain::agentKindFromString(QStringLiteral("other")),
             snack::domain::AgentKind::Mock);
    QCOMPARE(snack::domain::reasoningEffortFromString(QStringLiteral("maximum")),
             snack::domain::ReasoningEffort::Medium);
    QCOMPARE(snack::domain::accessLevelFromString(QStringLiteral("unsafe")),
             snack::domain::AccessLevel::Strict);
}

void TestDomainTypes::eventTypeNamesRoundTrip() {
    using enum snack::domain::AgentEventType;
    const QList<snack::domain::AgentEventType> values = {
        UserMessage,   AgentMessageDelta, ApprovalRequested,  ApprovalResolved,
        TurnCompleted, WarningRaised,     RawProtocolObserved};
    for (const auto value : values) {
        QCOMPARE(snack::domain::agentEventTypeFromString(snack::domain::enumName(value)), value);
    }
}

void TestDomainTypes::approvalDecisionNamesRoundTrip() {
    using enum snack::domain::ApprovalDecision;
    const QList<snack::domain::ApprovalDecision> values = {Accept, AcceptForSession, Decline,
                                                           Cancel};
    for (const auto value : values) {
        QCOMPARE(snack::domain::approvalDecisionFromString(snack::domain::enumName(value)), value);
    }
    QCOMPARE(snack::domain::approvalDecisionFromString(QStringLiteral("future")), Decline);
}

QTEST_APPLESS_MAIN(TestDomainTypes)
#include "TestDomainTypes.moc"
