#include "agent/FakeAgentAdapter.h"
#include "session/SessionController.h"

#include <QSignalSpy>
#include <QTest>

class MemoryEventRepository : public snack::storage::IEventRepository {
  public:
    bool saveConversation(const snack::domain::Conversation& conversation, QString*) override {
        conversation_ = conversation;
        return true;
    }

    bool appendEvent(const snack::domain::AgentEvent& event, QString*) override {
        events_.append(event);
        return true;
    }

    std::optional<snack::domain::Conversation> conversationById(const QUuid& conversationId,
                                                                QString*) const override {
        return conversation_.id == conversationId
                   ? std::optional<snack::domain::Conversation>(conversation_)
                   : std::nullopt;
    }

    QList<snack::domain::AgentEvent> eventsForConversation(const QUuid& conversationId,
                                                           QString*) const override {
        QList<snack::domain::AgentEvent> result;
        for (const auto& event : events_) {
            if (event.conversationId == conversationId) {
                result.append(event);
            }
        }
        return result;
    }

    snack::domain::Conversation conversation_;
    QList<snack::domain::AgentEvent> events_;
};

class TestSessionController final : public QObject {
    Q_OBJECT

  private slots:
    void streamsAndPersistsTurn();
    void snapshotsSettingsPerTurn();
    void interruptsActiveTurn();
    void validatesStateAndNormalizesSettings();
    void followsDynamicCapabilities();
    void persistsNativeIdentityForResume();
    void reportsPersistenceFailure();
};

static snack::domain::Conversation conversation() {
    snack::domain::Conversation value;
    value.title = QStringLiteral("Test session");
    value.workingDirectory = QStringLiteral("/test/workspace");
    return value;
}

void TestSessionController::streamsAndPersistsTurn() {
    MemoryEventRepository repository;
    snack::agent::FakeAgentAdapter adapter(nullptr, 1);
    snack::session::SessionController controller(conversation(), &adapter, &repository);
    QSignalSpy eventSpy(&controller, &snack::session::SessionController::eventRecorded);

    controller.open();
    QTRY_COMPARE(controller.status(), snack::domain::ConversationStatus::Idle);
    QVERIFY(controller.sendMessage(QStringLiteral("hello")));
    QTRY_COMPARE_WITH_TIMEOUT(controller.status(), snack::domain::ConversationStatus::Idle, 1000);

    QVERIFY(eventSpy.count() >= 8);
    QCOMPARE(repository.events_.constFirst().type, snack::domain::AgentEventType::UserMessage);
    QCOMPARE(repository.events_.constLast().type, snack::domain::AgentEventType::TurnCompleted);
    for (qsizetype index = 0; index < repository.events_.size(); ++index) {
        QCOMPARE(repository.events_.at(index).sequence, static_cast<quint64>(index + 1));
    }
}

void TestSessionController::snapshotsSettingsPerTurn() {
    MemoryEventRepository repository;
    snack::agent::FakeAgentAdapter adapter(nullptr, 1);
    snack::session::SessionController controller(conversation(), &adapter, &repository);
    controller.open();
    QTRY_COMPARE(controller.status(), snack::domain::ConversationStatus::Idle);

    auto settings = controller.nextTurnSettings();
    settings.modelId = QStringLiteral("mock-fast");
    settings.reasoningEffort = snack::domain::ReasoningEffort::High;
    controller.setNextTurnSettings(settings);
    QVERIFY(controller.sendMessage(QStringLiteral("snapshot")));

    const auto saved = snack::domain::TurnSettingsSnapshot::fromJson(
        repository.events_.constFirst().payload.value(QStringLiteral("settings")).toObject());
    QCOMPARE(saved.modelId, QStringLiteral("mock-fast"));
    QCOMPARE(saved.reasoningEffort, snack::domain::ReasoningEffort::High);
}

void TestSessionController::interruptsActiveTurn() {
    MemoryEventRepository repository;
    snack::agent::FakeAgentAdapter adapter(nullptr, 50);
    snack::session::SessionController controller(conversation(), &adapter, &repository);
    controller.open();
    QTRY_COMPARE(controller.status(), snack::domain::ConversationStatus::Idle);
    QVERIFY(controller.sendMessage(QStringLiteral("stop")));
    controller.interrupt();
    QTRY_COMPARE(controller.status(), snack::domain::ConversationStatus::Idle);
    QCOMPARE(repository.events_.constLast().type, snack::domain::AgentEventType::TurnInterrupted);
}

void TestSessionController::validatesStateAndNormalizesSettings() {
    MemoryEventRepository repository;
    snack::agent::FakeAgentAdapter adapter(nullptr, 50);
    snack::session::SessionController controller(conversation(), &adapter, &repository);
    QString error;
    QVERIFY(!controller.sendMessage(QStringLiteral("too early"), &error));
    QVERIFY(error.contains(QStringLiteral("not idle")));

    controller.open();
    controller.open();
    QTRY_COMPARE(controller.status(), snack::domain::ConversationStatus::Idle);
    QVERIFY(!controller.sendMessage(QStringLiteral("   "), &error));
    QVERIFY(error.contains(QStringLiteral("empty")));

    QSignalSpy settingsSpy(&controller,
                           &snack::session::SessionController::nextTurnSettingsChanged);
    auto settings = controller.nextTurnSettings();
    settings.agentKind = snack::domain::AgentKind::Claude;
    settings.workingDirectory = QStringLiteral("/wrong");
    settings.capabilityVersion = QStringLiteral("wrong");
    settings.modelId = QStringLiteral("mock-fast");
    controller.setNextTurnSettings(settings);
    QCOMPARE(settingsSpy.count(), 1);
    QCOMPARE(controller.nextTurnSettings().agentKind, snack::domain::AgentKind::Mock);
    QCOMPARE(controller.nextTurnSettings().workingDirectory,
             controller.conversation().workingDirectory);
    QCOMPARE(controller.nextTurnSettings().capabilityVersion, QStringLiteral("mock-v1"));
    controller.setNextTurnSettings(controller.nextTurnSettings());
    QCOMPARE(settingsSpy.count(), 1);

    QVERIFY(controller.sendMessage(QStringLiteral("first"), &error));
    QVERIFY(!controller.sendMessage(QStringLiteral("second"), &error));
    controller.close();
    QCOMPARE(controller.status(), snack::domain::ConversationStatus::Closed);
}

void TestSessionController::followsDynamicCapabilities() {
    using snack::domain::AccessLevel;
    using snack::domain::ReasoningEffort;
    MemoryEventRepository repository;
    snack::agent::FakeAgentAdapter adapter(nullptr, 1);
    snack::session::SessionController controller(conversation(), &adapter, &repository);
    QSignalSpy settingsSpy(&controller,
                           &snack::session::SessionController::nextTurnSettingsChanged);

    const snack::agent::CapabilitySet capabilities{
        .version = QStringLiteral("codex-catalog-v2"),
        .models = {QStringLiteral("codex-small"), QStringLiteral("codex-default")},
        .defaultModelId = QStringLiteral("codex-default"),
        .modelCapabilities = {{.id = QStringLiteral("codex-small"),
                               .defaultReasoningEffortId = QStringLiteral("low"),
                               .supportedReasoningEfforts = {{QStringLiteral("low"), {}}}},
                              {.id = QStringLiteral("codex-default"),
                               .defaultReasoningEffortId = QStringLiteral("high"),
                               .supportedReasoningEfforts = {{QStringLiteral("high"), {}},
                                                             {QStringLiteral("xhigh"), {}}},
                               .isDefault = true}},
        .reasoningEfforts = {ReasoningEffort::Low, ReasoningEffort::High,
                             ReasoningEffort::ExtraHigh},
        .accessLevels = {AccessLevel::Workspace}};
    adapter.capabilitiesChanged(capabilities);

    QCOMPARE(settingsSpy.count(), 1);
    QCOMPARE(controller.nextTurnSettings().capabilityVersion, QStringLiteral("codex-catalog-v2"));
    QCOMPARE(controller.nextTurnSettings().modelId, QStringLiteral("codex-default"));
    QCOMPARE(controller.nextTurnSettings().reasoningEffort, ReasoningEffort::High);
    QCOMPARE(controller.nextTurnSettings().accessLevel, AccessLevel::Workspace);

    adapter.capabilitiesChanged(capabilities);
    QCOMPARE(settingsSpy.count(), 1);
}

void TestSessionController::persistsNativeIdentityForResume() {
    MemoryEventRepository repository;
    snack::agent::FakeAgentAdapter adapter(nullptr, 1);
    auto resumableConversation = conversation();
    resumableConversation.nativeThreadId = QStringLiteral("existing-thread");
    resumableConversation.nativeSessionId = QStringLiteral("existing-session");
    snack::session::SessionController controller(resumableConversation, &adapter, &repository);
    QSignalSpy identitySpy(&controller, &snack::session::SessionController::nativeIdentityChanged);

    controller.open();
    QCOMPARE(adapter.lastConnectionRequest().nativeThreadId, QStringLiteral("existing-thread"));
    QCOMPARE(adapter.lastConnectionRequest().workingDirectory,
             resumableConversation.workingDirectory);

    adapter.nativeIdentityChanged(QStringLiteral("resumed-thread"),
                                  QStringLiteral("server-session-root"));
    QCOMPARE(identitySpy.count(), 1);
    QCOMPARE(controller.conversation().nativeThreadId, QStringLiteral("resumed-thread"));
    QCOMPARE(controller.conversation().nativeSessionId, QStringLiteral("server-session-root"));
    QCOMPARE(repository.conversation_.nativeThreadId, QStringLiteral("resumed-thread"));
    QCOMPARE(repository.conversation_.nativeSessionId, QStringLiteral("server-session-root"));

    adapter.nativeIdentityChanged(QString(), QStringLiteral("invalid"));
    adapter.nativeIdentityChanged(QStringLiteral("resumed-thread"),
                                  QStringLiteral("server-session-root"));
    QCOMPARE(identitySpy.count(), 1);
}

void TestSessionController::reportsPersistenceFailure() {
    class FailingRepository final : public MemoryEventRepository {
      public:
        bool saveConversation(const snack::domain::Conversation&, QString* error) override {
            *error = QStringLiteral("save failed");
            return false;
        }
    } repository;
    snack::agent::FakeAgentAdapter adapter(nullptr, 1);
    snack::session::SessionController controller(conversation(), &adapter, &repository);
    QSignalSpy errorSpy(&controller, &snack::session::SessionController::persistenceError);
    controller.open();
    QCOMPARE(controller.status(), snack::domain::ConversationStatus::Failed);
    QVERIFY(errorSpy.count() >= 1);
}

QTEST_GUILESS_MAIN(TestSessionController)
#include "TestSessionController.moc"
