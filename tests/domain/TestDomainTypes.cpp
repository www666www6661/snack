#include "domain/DomainTypes.h"

#include <QTest>

class TestDomainTypes final : public QObject {
    Q_OBJECT

  private slots:
    void settingsSnapshotRoundTrips();
    void unknownValuesUseSafeDefaults();
    void eventTypeNamesRoundTrip();
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
        UserMessage, AgentMessageDelta, TurnCompleted, WarningRaised, RawProtocolObserved};
    for (const auto value : values) {
        QCOMPARE(snack::domain::agentEventTypeFromString(snack::domain::enumName(value)), value);
    }
}

QTEST_APPLESS_MAIN(TestDomainTypes)
#include "TestDomainTypes.moc"
