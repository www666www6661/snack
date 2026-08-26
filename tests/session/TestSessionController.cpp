#include "agent/FakeAgentAdapter.h"
#include "session/SessionController.h"

#include <QJsonArray>
#include <QJsonDocument>
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
    void steersActiveTurn();
    void interruptsActiveTurn();
    void handlesApprovalLifecycle();
    void handlesUserInputLifecycleAndConcurrentWaitingStates();
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
    settings.reasoningEffort = snack::domain::ReasoningEffort::Low;
    controller.setNextTurnSettings(settings);
    QVERIFY(controller.sendMessage(QStringLiteral("snapshot")));

    const auto saved = snack::domain::TurnSettingsSnapshot::fromJson(
        repository.events_.constFirst().payload.value(QStringLiteral("settings")).toObject());
    QCOMPARE(saved.modelId, QStringLiteral("mock-fast"));
    QCOMPARE(saved.reasoningEffort, snack::domain::ReasoningEffort::Low);
}

void TestSessionController::steersActiveTurn() {
    MemoryEventRepository repository;
    snack::agent::FakeAgentAdapter adapter(nullptr, 1000);
    snack::session::SessionController controller(conversation(), &adapter, &repository);
    QString error;
    QVERIFY(!controller.steerMessage(QStringLiteral("too early"), &error));
    controller.open();
    QTRY_COMPARE(controller.status(), snack::domain::ConversationStatus::Idle);
    QVERIFY(controller.sendMessage(QStringLiteral("initial")));
    const QUuid turnId = repository.events_.constFirst().turnId;
    QVERIFY(!controller.steerMessage(QStringLiteral("   "), &error));
    QVERIFY(error.contains(QStringLiteral("empty")));
    QVERIFY(controller.steerMessage(QStringLiteral(" focus tests "), &error));
    QCOMPARE(adapter.lastSteerRequest().turnId, turnId);
    QCOMPARE(adapter.lastSteerRequest().message, QStringLiteral("focus tests"));
    QCOMPARE(repository.events_.constLast().type, snack::domain::AgentEventType::UserMessage);
    QVERIFY(repository.events_.constLast().payload.value(QStringLiteral("steered")).toBool());
    QCOMPARE(repository.events_.constLast().payload.value(QStringLiteral("text")).toString(),
             QStringLiteral("focus tests"));
    QCOMPARE(repository.events_.constLast()
                 .payload.value(QStringLiteral("settings"))
                 .toObject()
                 .value(QStringLiteral("modelId"))
                 .toString(),
             controller.nextTurnSettings().modelId);

    snack::domain::AgentEvent approval;
    approval.turnId = turnId;
    approval.type = snack::domain::AgentEventType::ApprovalRequested;
    approval.payload = {{QStringLiteral("requestId"), QStringLiteral("approval")}};
    adapter.eventReceived(approval);
    QVERIFY(!controller.steerMessage(QStringLiteral("blocked"), &error));
    QVERIFY(error.contains(QStringLiteral("cannot accept")));
    controller.interrupt();
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

void TestSessionController::handlesApprovalLifecycle() {
    using snack::domain::AgentEvent;
    using snack::domain::AgentEventType;
    using snack::domain::ApprovalDecision;
    using snack::domain::ConversationStatus;

    MemoryEventRepository repository;
    snack::agent::FakeAgentAdapter adapter(nullptr, 1000);
    snack::session::SessionController controller(conversation(), &adapter, &repository);
    controller.open();
    QTRY_COMPARE(controller.status(), ConversationStatus::Idle);
    QVERIFY(controller.sendMessage(QStringLiteral("needs approval")));
    const QUuid turnId = repository.events_.constFirst().turnId;

    AgentEvent request;
    request.turnId = turnId;
    request.type = AgentEventType::ApprovalRequested;
    request.payload = {{QStringLiteral("requestId"), QStringLiteral("approval-token")},
                       {QStringLiteral("kind"), QStringLiteral("commandExecution")},
                       {QStringLiteral("command"), QStringLiteral("git status")}};
    adapter.eventReceived(request);
    QCOMPARE(controller.status(), ConversationStatus::WaitingApproval);
    QCOMPARE(controller.pendingApprovalCount(), 1);

    adapter.eventReceived(request);
    QCOMPARE(controller.pendingApprovalCount(), 1);
    QCOMPARE(repository.events_.constLast().type, AgentEventType::WarningRaised);

    QString error;
    QVERIFY(controller.respondToApproval(QStringLiteral("approval-token"),
                                         ApprovalDecision::AcceptForSession, &error));
    QCOMPARE(adapter.lastApprovalRequestId(), QStringLiteral("approval-token"));
    QCOMPARE(adapter.lastApprovalDecision(), ApprovalDecision::AcceptForSession);
    QCOMPARE(controller.pendingApprovalCount(), 0);
    QCOMPARE(controller.status(), ConversationStatus::Running);
    QCOMPARE(repository.events_.constLast().type, AgentEventType::ApprovalResolved);
    QVERIFY(!controller.respondToApproval(QStringLiteral("approval-token"),
                                          ApprovalDecision::Decline, &error));
    QVERIFY(!error.isEmpty());

    request.payload.insert(QStringLiteral("requestId"), QStringLiteral("approval-token-2"));
    request.payload.insert(QStringLiteral("availableDecisions"),
                           QJsonArray{QStringLiteral("decline")});
    adapter.eventReceived(request);
    QCOMPARE(controller.status(), ConversationStatus::WaitingApproval);
    QVERIFY(!controller.respondToApproval(QStringLiteral("approval-token-2"),
                                          ApprovalDecision::Accept, &error));
    QVERIFY(error.contains(QStringLiteral("not available")));
    controller.interrupt();
    QCOMPARE(controller.status(), ConversationStatus::Idle);
    QCOMPARE(controller.pendingApprovalCount(), 0);
}

void TestSessionController::handlesUserInputLifecycleAndConcurrentWaitingStates() {
    using snack::domain::AgentEvent;
    using snack::domain::AgentEventType;
    using snack::domain::ApprovalDecision;
    using snack::domain::ConversationStatus;

    MemoryEventRepository repository;
    snack::agent::FakeAgentAdapter adapter(nullptr, 1000);
    snack::session::SessionController controller(conversation(), &adapter, &repository);
    controller.open();
    QTRY_COMPARE(controller.status(), ConversationStatus::Idle);
    QVERIFY(controller.sendMessage(QStringLiteral("ask me")));
    const QUuid turnId = repository.events_.constFirst().turnId;

    const auto inputRequest = [turnId](const QString& requestId, bool blocking) {
        AgentEvent event;
        event.turnId = turnId;
        event.type = AgentEventType::UserInputRequested;
        event.payload = {{QStringLiteral("requestId"), requestId},
                         {QStringLiteral("isBlocking"), blocking},
                         {QStringLiteral("questions"),
                          QJsonArray{QJsonObject{{QStringLiteral("id"), QStringLiteral("value")},
                                                 {QStringLiteral("isSecret"), true}}}}};
        return event;
    };
    adapter.eventReceived(inputRequest(QStringLiteral("nonblocking"), false));
    QCOMPARE(controller.status(), ConversationStatus::Running);
    QCOMPARE(controller.pendingInputCount(), 1);

    AgentEvent approval;
    approval.turnId = turnId;
    approval.type = AgentEventType::ApprovalRequested;
    approval.payload = {{QStringLiteral("requestId"), QStringLiteral("approval")}};
    adapter.eventReceived(approval);
    QCOMPARE(controller.status(), ConversationStatus::WaitingApproval);

    adapter.eventReceived(inputRequest(QStringLiteral("blocking"), true));
    QCOMPARE(controller.status(), ConversationStatus::WaitingInput);
    QCOMPARE(controller.pendingInputCount(), 2);
    adapter.eventReceived(inputRequest(QStringLiteral("blocking"), true));
    QCOMPARE(controller.pendingInputCount(), 2);
    QCOMPARE(repository.events_.constLast().type, AgentEventType::WarningRaised);

    const QJsonObject secretAnswers{
        {QStringLiteral("value"),
         QJsonObject{{QStringLiteral("answers"), QJsonArray{QStringLiteral("top-secret")}}}}};
    QString error;
    QVERIFY(controller.respondToUserInput(QStringLiteral("blocking"), secretAnswers, &error));
    QCOMPARE(adapter.lastUserInputRequestId(), QStringLiteral("blocking"));
    QCOMPARE(adapter.lastUserInputAnswers(), secretAnswers);
    QCOMPARE(controller.status(), ConversationStatus::WaitingApproval);
    QCOMPARE(repository.events_.constLast().type, AgentEventType::UserInputResolved);
    QCOMPARE(repository.events_.constLast().payload.keys(),
             QStringList({QStringLiteral("requestId"), QStringLiteral("resolution")}));

    QVERIFY(
        controller.respondToApproval(QStringLiteral("approval"), ApprovalDecision::Accept, &error));
    QCOMPARE(controller.status(), ConversationStatus::Running);
    QVERIFY(controller.respondToUserInput(QStringLiteral("nonblocking"), secretAnswers, &error));
    QCOMPARE(controller.pendingInputCount(), 0);
    QVERIFY(!controller.respondToUserInput(QStringLiteral("nonblocking"), secretAnswers, &error));

    for (const AgentEvent& event : repository.events_) {
        QVERIFY(
            !QJsonDocument(event.payload).toJson(QJsonDocument::Compact).contains("top-secret"));
    }
    adapter.eventReceived(inputRequest(QStringLiteral("interrupt-input"), true));
    QCOMPARE(controller.status(), ConversationStatus::WaitingInput);
    controller.interrupt();
    QCOMPARE(controller.status(), ConversationStatus::Idle);
    QCOMPARE(controller.pendingInputCount(), 0);
}

void TestSessionController::validatesStateAndNormalizesSettings() {
    MemoryEventRepository repository;
    snack::agent::FakeAgentAdapter adapter(nullptr, 50);
    snack::session::SessionController controller(conversation(), &adapter, &repository);
    QSignalSpy detailSpy(&controller, &snack::session::SessionController::connectionDetailChanged);
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
    QCOMPARE(controller.connectionDetail(), QStringLiteral("mock-v1"));
    QCOMPARE(detailSpy.count(), 1);

    settings = controller.nextTurnSettings();
    settings.modelId = QStringLiteral("missing-model");
    settings.reasoningEffort = snack::domain::ReasoningEffort::Ultra;
    controller.setNextTurnSettings(settings);
    QCOMPARE(controller.nextTurnSettings().modelId, QStringLiteral("mock-balanced"));
    QCOMPARE(controller.nextTurnSettings().reasoningEffort, snack::domain::ReasoningEffort::Medium);
    controller.setNextTurnSettings(controller.nextTurnSettings());
    QCOMPARE(settingsSpy.count(), 2);

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
    QSignalSpy capabilitiesSpy(&controller,
                               &snack::session::SessionController::capabilitiesChanged);

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
    QCOMPARE(controller.capabilities().defaultModelId, QStringLiteral("codex-default"));
    QCOMPARE(capabilitiesSpy.count(), 1);

    adapter.capabilitiesChanged(capabilities);
    QCOMPARE(settingsSpy.count(), 1);
    QCOMPARE(capabilitiesSpy.count(), 2);
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
