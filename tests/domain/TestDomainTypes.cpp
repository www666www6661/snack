#include "domain/DomainTypes.h"
#include "domain/PromptTemplateEngine.h"

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
    void queuedMessageStateNamesRoundTrip();
    void createsSafeFallbackConversationTitles();
    void normalizesConversationTags();
    void validatesAndRendersPromptTemplates();
    void rejectsInvalidPromptTemplates();
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
        UserMessage,           AgentMessageStart,  AgentMessageDelta, AgentMessageComplete,
        ToolStarted,           ToolOutputDelta,    ToolCompleted,     ReasoningStarted,
        ReasoningSummaryDelta, ReasoningCompleted, PlanUpdated,       ApprovalRequested,
        ApprovalResolved,      UserInputRequested, UserInputResolved, UsageUpdated,
        TurnStarted,           TurnCompleted,      TurnInterrupted,   TurnFailed,
        CapabilityChanged,     ConnectionChanged,  WarningRaised,     ErrorRaised,
        RawProtocolObserved};
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

void TestDomainTypes::queuedMessageStateNamesRoundTrip() {
    const auto pending = snack::domain::QueuedMessageState::Pending;
    QCOMPARE(snack::domain::enumName(pending), QStringLiteral("pending"));
    QCOMPARE(snack::domain::queuedMessageStateFromString(QStringLiteral("pending")), pending);
    QCOMPARE(snack::domain::queuedMessageStateFromString(QStringLiteral("future")), pending);
}

void TestDomainTypes::createsSafeFallbackConversationTitles() {
    QCOMPARE(snack::domain::fallbackConversationTitle(
                 QStringLiteral("  Build\n\tthe   conversation sidebar  ")),
             QStringLiteral("Build the conversation sidebar"));
    const QString unsafe = QStringLiteral("safe") + QChar(0x202E) + QStringLiteral(" hidden") +
                           QChar(0x0007) + QStringLiteral(" title");
    QCOMPARE(snack::domain::fallbackConversationTitle(unsafe), QStringLiteral("safe hidden title"));
    QCOMPARE(snack::domain::fallbackConversationTitle(QString(80, QLatin1Char('a'))),
             QString(69, QLatin1Char('a')) + QStringLiteral("..."));
    const QString whale = QString::fromUcs4(U"\U0001F40B");
    const QString emojiTitle = snack::domain::fallbackConversationTitle(
        QStringLiteral("Keep emoji ") + whale + QStringLiteral(" in the title"));
    QVERIFY(emojiTitle.contains(whale));
}

void TestDomainTypes::normalizesConversationTags() {
    QString error;
    const auto normalized = snack::domain::normalizeConversationTags(
        {QStringLiteral(" backend "), QStringLiteral("Needs   Review"), QStringLiteral("BACKEND"),
         QStringLiteral(""), QStringLiteral("ui") + QChar(0x202E)},
        &error);
    QVERIFY2(normalized.has_value(), qPrintable(error));
    QCOMPARE(*normalized, QStringList({QStringLiteral("backend"), QStringLiteral("Needs Review"),
                                       QStringLiteral("ui")}));

    QVERIFY(!snack::domain::normalizeConversationTags({QString(33, QLatin1Char('x'))}, &error)
                 .has_value());
    QVERIFY(error.contains(QStringLiteral("32")));
    QStringList tooMany;
    for (int index = 0; index < 9; ++index) {
        tooMany.append(QStringLiteral("tag-%1").arg(index));
    }
    QVERIFY(!snack::domain::normalizeConversationTags(tooMany, &error).has_value());
    QVERIFY(error.contains(QStringLiteral("8")));
}

void TestDomainTypes::validatesAndRendersPromptTemplates() {
    snack::domain::PromptTemplate promptTemplate;
    promptTemplate.name = QStringLiteral("Review change");
    promptTemplate.content = QStringLiteral("Review {{path}} for {{focus}}. Recheck {{path}}.");
    QString error;
    QCOMPARE(snack::domain::PromptTemplateEngine::parameters(promptTemplate, &error),
             QStringList({QStringLiteral("path"), QStringLiteral("focus")}));
    QVERIFY2(snack::domain::PromptTemplateEngine::validate(promptTemplate, &error),
             qPrintable(error));
    const auto rendered = snack::domain::PromptTemplateEngine::render(
        promptTemplate,
        {{QStringLiteral("path"), QStringLiteral("src/main.cpp")},
         {QStringLiteral("focus"), QStringLiteral("correctness")}},
        &error);
    QVERIFY2(rendered.has_value(), qPrintable(error));
    QCOMPARE(*rendered,
             QStringLiteral("Review src/main.cpp for correctness. Recheck src/main.cpp."));

    promptTemplate.content = QStringLiteral("{{first}} then {{second}} / {{主题}}");
    const auto literal = snack::domain::PromptTemplateEngine::render(
        promptTemplate,
        {{QStringLiteral("first"), QStringLiteral("{{second}}")},
         {QStringLiteral("second"), QStringLiteral("done")},
         {QStringLiteral("主题"), QStringLiteral("模板")}},
        &error);
    QVERIFY2(literal.has_value(), qPrintable(error));
    QCOMPARE(*literal, QStringLiteral("{{second}} then done / 模板"));
}

void TestDomainTypes::rejectsInvalidPromptTemplates() {
    snack::domain::PromptTemplate promptTemplate;
    promptTemplate.name = QStringLiteral("Invalid");
    promptTemplate.content = QStringLiteral("Broken {{bad name}} and }}");
    QString error;
    QVERIFY(!snack::domain::PromptTemplateEngine::validate(promptTemplate, &error));
    QVERIFY(error.contains(QStringLiteral("invalid")));

    promptTemplate.content = QStringLiteral("Use {{missing}}");
    QVERIFY(!snack::domain::PromptTemplateEngine::render(promptTemplate, {}, &error).has_value());
    QVERIFY(error.contains(QStringLiteral("missing")));
    promptTemplate.name.clear();
    QVERIFY(!snack::domain::PromptTemplateEngine::validate(promptTemplate, &error));
}

QTEST_APPLESS_MAIN(TestDomainTypes)
#include "TestDomainTypes.moc"
