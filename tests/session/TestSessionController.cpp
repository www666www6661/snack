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

    QList<snack::domain::Conversation> conversations(QString*) const override {
        return conversation_.title.isEmpty() ? QList<snack::domain::Conversation>{}
                                             : QList<snack::domain::Conversation>{conversation_};
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

    bool replaceQueuedMessages(const QUuid& conversationId,
                               const QList<snack::domain::QueuedMessage>& messages,
                               QString*) override {
        queues_.insert(conversationId, messages);
        return true;
    }

    QList<snack::domain::QueuedMessage> queuedMessagesForConversation(const QUuid& conversationId,
                                                                      QString*) const override {
        return queues_.value(conversationId);
    }

    bool savePromptTemplate(const snack::domain::PromptTemplate& promptTemplate,
                            QString*) override {
        templates_.insert(promptTemplate.id, promptTemplate);
        return true;
    }

    bool deletePromptTemplate(const QUuid& templateId, QString*) override {
        return templates_.remove(templateId) > 0;
    }

    QList<snack::domain::PromptTemplate> promptTemplates(QString*) const override {
        return templates_.values();
    }

    snack::domain::Conversation conversation_;
    QList<snack::domain::AgentEvent> events_;
    QHash<QUuid, QList<snack::domain::QueuedMessage>> queues_;
    QHash<QUuid, snack::domain::PromptTemplate> templates_;
};

class RejectingAgentAdapter final : public snack::agent::IAgentAdapter {
  public:
    using IAgentAdapter::IAgentAdapter;

    [[nodiscard]] snack::domain::AgentKind kind() const override {
        return snack::domain::AgentKind::Codex;
    }
    [[nodiscard]] snack::agent::CapabilitySet capabilities() const override { return {}; }
    void connectAgent(const snack::agent::AgentConnectionRequest&) override {
        emit connectionChanged(false, QStringLiteral("Codex app-server process is still stopping"));
    }
    void startTurn(const snack::agent::TurnRequest&) override {}
    bool steerTurn(const snack::agent::SteerRequest&) override { return false; }
    bool respondToApproval(const QString&, snack::domain::ApprovalDecision) override {
        return false;
    }
    bool respondToUserInput(const QString&, const QJsonObject&) override { return false; }
    void interruptTurn() override {}
    void closeAgent() override {}
};

class TestSessionController final : public QObject {
    Q_OBJECT

  private slots:
    void streamsAndPersistsTurn();
    void replacesPlaceholderTitleFromFirstMessage();
    void renamesCurrentConversation();
    void archivesConversationMetadata();
    void pinsConversationMetadata();
    void updatesConversationTags();
    void exposesConversationCatalog();
    void snapshotsSettingsPerTurn();
    void steersActiveTurn();
    void persistsEditsAndDispatchesQueuedMessages();
    void doesNotAutoDispatchRestoredOrInterruptedQueue();
    void reconnectsWithoutReplayingQueuedMessages();
    void returnsToDisconnectedWhenConnectRejected();
    void managesPromptTemplates();
    void interruptsActiveTurn();
    void handlesApprovalLifecycle();
    void handlesUserInputLifecycleAndConcurrentWaitingStates();
    void validatesStateAndNormalizesSettings();
    void ignoresLateAdapterSignalsAfterClose();
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
    QCOMPARE(controller.conversation().title, QStringLiteral("Test session"));

    QVERIFY(eventSpy.count() >= 8);
    QCOMPARE(repository.events_.constFirst().type, snack::domain::AgentEventType::UserMessage);
    QCOMPARE(repository.events_.constLast().type, snack::domain::AgentEventType::TurnCompleted);
    for (qsizetype index = 0; index < repository.events_.size(); ++index) {
        QCOMPARE(repository.events_.at(index).sequence, static_cast<quint64>(index + 1));
    }
}

void TestSessionController::replacesPlaceholderTitleFromFirstMessage() {
    MemoryEventRepository repository;
    snack::agent::FakeAgentAdapter adapter(nullptr, 1);
    auto value = conversation();
    value.title = QStringLiteral("Mock conversation");
    value.titleIsPlaceholder = true;
    snack::session::SessionController controller(value, &adapter, &repository);
    QSignalSpy titleSpy(&controller, &snack::session::SessionController::conversationTitleChanged);

    controller.open();
    QTRY_COMPARE(controller.status(), snack::domain::ConversationStatus::Idle);
    QVERIFY(controller.sendMessage(QStringLiteral("  Build\n a useful conversation rail  ")));
    QCOMPARE(controller.conversation().title, QStringLiteral("Build a useful conversation rail"));
    QVERIFY(!controller.conversation().titleIsPlaceholder);
    QCOMPARE(repository.conversation_.title, controller.conversation().title);
    QCOMPARE(titleSpy.count(), 1);

    QTRY_COMPARE_WITH_TIMEOUT(controller.status(), snack::domain::ConversationStatus::Idle, 1000);
    QVERIFY(controller.sendMessage(QStringLiteral("Do not replace the established title")));
    QCOMPARE(controller.conversation().title, QStringLiteral("Build a useful conversation rail"));
    QCOMPARE(titleSpy.count(), 1);
}

void TestSessionController::renamesCurrentConversation() {
    MemoryEventRepository repository;
    snack::agent::FakeAgentAdapter adapter(nullptr, 1);
    snack::session::SessionController controller(conversation(), &adapter, &repository);
    QSignalSpy titleSpy(&controller, &snack::session::SessionController::conversationTitleChanged);
    QString error;

    controller.open();
    QTRY_COMPARE(controller.status(), snack::domain::ConversationStatus::Idle);
    QVERIFY(!controller.renameConversation(QStringLiteral(" \t\n "), &error));
    QVERIFY(error.contains(QStringLiteral("empty")));
    QVERIFY(controller.renameConversation(QStringLiteral("  Renamed\n conversation  "), &error));
    QCOMPARE(controller.conversation().title, QStringLiteral("Renamed conversation"));
    QVERIFY(!controller.conversation().titleIsPlaceholder);
    QCOMPARE(repository.conversation_.title, QStringLiteral("Renamed conversation"));
    QCOMPARE(titleSpy.count(), 1);
    QVERIFY(controller.renameConversation(QStringLiteral("Renamed conversation"), &error));
    QCOMPARE(titleSpy.count(), 1);

    controller.close();
    QVERIFY(!controller.renameConversation(QStringLiteral("Too late"), &error));
    QVERIFY(error.contains(QStringLiteral("closed")));
}

void TestSessionController::archivesConversationMetadata() {
    MemoryEventRepository repository;
    snack::agent::FakeAgentAdapter adapter(nullptr, 1);
    snack::session::SessionController controller(conversation(), &adapter, &repository);
    QString error;

    QVERIFY(controller.setArchived(true, &error));
    QVERIFY(controller.conversation().archived);
    QVERIFY(repository.conversation_.archived);
    QVERIFY(controller.setArchived(true, &error));
    QVERIFY(controller.setArchived(false, &error));
    QVERIFY(!controller.conversation().archived);
    QVERIFY(!repository.conversation_.archived);
}

void TestSessionController::pinsConversationMetadata() {
    MemoryEventRepository repository;
    snack::agent::FakeAgentAdapter adapter(nullptr, 1);
    snack::session::SessionController controller(conversation(), &adapter, &repository);
    QString error;

    QVERIFY(controller.setPinned(true, &error));
    QVERIFY(controller.conversation().pinned);
    QVERIFY(repository.conversation_.pinned);
    QVERIFY(controller.setPinned(false, &error));
    QVERIFY(!controller.conversation().pinned);
    QVERIFY(!repository.conversation_.pinned);
}

void TestSessionController::updatesConversationTags() {
    MemoryEventRepository repository;
    snack::agent::FakeAgentAdapter adapter(nullptr, 1);
    snack::session::SessionController controller(conversation(), &adapter, &repository);
    QString error;

    QVERIFY(controller.setTags(
        {QStringLiteral(" UI "), QStringLiteral("backend"), QStringLiteral("ui")}, &error));
    QCOMPARE(controller.conversation().tags,
             QStringList({QStringLiteral("backend"), QStringLiteral("UI")}));
    QCOMPARE(repository.conversation_.tags, controller.conversation().tags);
    const QStringList previous = controller.conversation().tags;
    QVERIFY(!controller.setTags({QString(33, QLatin1Char('x'))}, &error));
    QCOMPARE(controller.conversation().tags, previous);
}

void TestSessionController::exposesConversationCatalog() {
    MemoryEventRepository repository;
    repository.conversation_ = conversation();
    snack::agent::FakeAgentAdapter adapter(nullptr, 1);
    snack::session::SessionController controller(repository.conversation_, &adapter, &repository);

    const auto catalog = controller.conversationCatalog();
    QCOMPARE(catalog.size(), 1);
    QCOMPARE(catalog.constFirst().id, repository.conversation_.id);
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

void TestSessionController::persistsEditsAndDispatchesQueuedMessages() {
    MemoryEventRepository repository;
    snack::agent::FakeAgentAdapter adapter(nullptr, 10);
    const auto value = conversation();
    snack::session::SessionController controller(value, &adapter, &repository);
    QSignalSpy queueSpy(&controller, &snack::session::SessionController::queuedMessagesChanged);
    controller.open();
    QTRY_COMPARE(controller.status(), snack::domain::ConversationStatus::Idle);
    QVERIFY(controller.sendMessage(QStringLiteral("first")));
    QVERIFY(controller.queueMessage(QStringLiteral(" second ")));
    QVERIFY(controller.queueMessage(QStringLiteral("third")));
    QCOMPARE(controller.queuedMessages().size(), 2);
    QCOMPARE(repository.queues_.value(value.id).at(0).content, QStringLiteral("second"));

    const QUuid thirdId = controller.queuedMessages().at(1).id;
    QVERIFY(controller.updateQueuedMessage(thirdId, QStringLiteral(" edited third ")));
    QVERIFY(controller.moveQueuedMessage(thirdId, 0));
    QCOMPARE(controller.queuedMessages().constFirst().content, QStringLiteral("edited third"));
    QCOMPARE(controller.queuedMessages().constFirst().position, qsizetype{0});

    QTRY_COMPARE_WITH_TIMEOUT(controller.status(), snack::domain::ConversationStatus::Idle, 1500);
    QVERIFY(controller.queuedMessages().isEmpty());
    QVERIFY(repository.queues_.value(value.id).isEmpty());
    QStringList sentMessages;
    for (const auto& event : repository.events_) {
        if (event.type == snack::domain::AgentEventType::UserMessage &&
            !event.payload.value(QStringLiteral("steered")).toBool()) {
            sentMessages.append(event.payload.value(QStringLiteral("text")).toString());
        }
    }
    QCOMPARE(sentMessages, QStringList({QStringLiteral("first"), QStringLiteral("edited third"),
                                        QStringLiteral("second")}));
    QVERIFY(queueSpy.count() >= 6);
}

void TestSessionController::doesNotAutoDispatchRestoredOrInterruptedQueue() {
    MemoryEventRepository repository;
    snack::agent::FakeAgentAdapter adapter(nullptr, 1000);
    const auto value = conversation();
    snack::domain::QueuedMessage restored;
    restored.conversationId = value.id;
    restored.content = QStringLiteral("restored draft");
    repository.queues_.insert(value.id, {restored});

    snack::session::SessionController controller(value, &adapter, &repository);
    QCOMPARE(controller.queuedMessages().size(), 1);
    controller.open();
    QTRY_COMPARE(controller.status(), snack::domain::ConversationStatus::Idle);
    QTest::qWait(20);
    QVERIFY(repository.events_.isEmpty());

    QString error;
    QVERIFY(controller.sendQueuedMessageNow(restored.id, &error));
    QCOMPARE(controller.status(), snack::domain::ConversationStatus::Running);
    QVERIFY(controller.queueMessage(QStringLiteral("keep after interrupt"), &error));
    const QUuid pendingId = controller.queuedMessages().constFirst().id;
    controller.interrupt();
    QTRY_COMPARE(controller.status(), snack::domain::ConversationStatus::Idle);
    QCOMPARE(controller.queuedMessages().size(), 1);
    QCOMPARE(controller.queuedMessages().constFirst().content,
             QStringLiteral("keep after interrupt"));
    QVERIFY(controller.cancelQueuedMessage(pendingId, &error));
    QVERIFY(repository.queues_.value(value.id).isEmpty());

    QVERIFY(controller.sendMessage(QStringLiteral("failing turn"), &error));
    QVERIFY(controller.queueMessage(QStringLiteral("keep after failure"), &error));
    const QUuid failedTurnId = repository.events_.constLast().turnId;
    snack::domain::AgentEvent failed;
    failed.turnId = failedTurnId;
    failed.type = snack::domain::AgentEventType::TurnFailed;
    adapter.eventReceived(failed);
    adapter.turnFinished(failedTurnId, false, false);
    QCOMPARE(controller.status(), snack::domain::ConversationStatus::Idle);
    QCOMPARE(controller.queuedMessages().size(), 1);
    QCOMPARE(controller.queuedMessages().constFirst().content,
             QStringLiteral("keep after failure"));
    adapter.closeAgent();
}

void TestSessionController::reconnectsWithoutReplayingQueuedMessages() {
    MemoryEventRepository repository;
    snack::agent::FakeAgentAdapter adapter;
    auto value = conversation();
    value.nativeThreadId = QStringLiteral("native-thread-1");
    value.nativeSessionId = QStringLiteral("native-session-1");
    snack::domain::QueuedMessage queued;
    queued.conversationId = value.id;
    queued.content = QStringLiteral("wait for explicit send");
    repository.queues_.insert(value.id, {queued});

    snack::session::SessionController controller(value, &adapter, &repository);
    controller.open();
    QTRY_COMPARE(controller.status(), snack::domain::ConversationStatus::Idle);
    adapter.closeAgent();
    QCOMPARE(controller.status(), snack::domain::ConversationStatus::Disconnected);

    controller.open();
    QCOMPARE(controller.status(), snack::domain::ConversationStatus::Connecting);
    QTRY_COMPARE(controller.status(), snack::domain::ConversationStatus::Idle);
    QCOMPARE(adapter.lastConnectionRequest().nativeThreadId, QStringLiteral("native-thread-1"));
    QCOMPARE(controller.queuedMessages().size(), 1);
    QVERIFY(repository.events_.isEmpty());
    adapter.closeAgent();
}

void TestSessionController::returnsToDisconnectedWhenConnectRejected() {
    MemoryEventRepository repository;
    RejectingAgentAdapter adapter;
    snack::session::SessionController controller(conversation(), &adapter, &repository);
    QSignalSpy statusSpy(&controller, &snack::session::SessionController::statusChanged);

    controller.open();

    QCOMPARE(controller.status(), snack::domain::ConversationStatus::Disconnected);
    QCOMPARE(controller.connectionDetail(),
             QStringLiteral("Codex app-server process is still stopping"));
    QCOMPARE(statusSpy.count(), 2);
    QCOMPARE(statusSpy.constFirst().constFirst().value<snack::domain::ConversationStatus>(),
             snack::domain::ConversationStatus::Connecting);
    QCOMPARE(statusSpy.constLast().constFirst().value<snack::domain::ConversationStatus>(),
             snack::domain::ConversationStatus::Disconnected);
}

void TestSessionController::managesPromptTemplates() {
    MemoryEventRepository repository;
    snack::agent::FakeAgentAdapter adapter;
    snack::session::SessionController controller(conversation(), &adapter, &repository);
    QSignalSpy templatesSpy(&controller,
                            &snack::session::SessionController::promptTemplatesChanged);
    snack::domain::PromptTemplate promptTemplate;
    promptTemplate.name = QStringLiteral("  Review  ");
    promptTemplate.content = QStringLiteral("Review {{path}}");
    QString error;
    QVERIFY(controller.savePromptTemplate(promptTemplate, &error));
    QCOMPARE(controller.promptTemplates().size(), 1);
    QCOMPARE(controller.promptTemplates().constFirst().name, QStringLiteral("Review"));
    QCOMPARE(templatesSpy.count(), 1);
    QVERIFY(controller.deletePromptTemplate(promptTemplate.id, &error));
    QVERIFY(controller.promptTemplates().isEmpty());
    QCOMPARE(templatesSpy.count(), 2);
    QVERIFY(!controller.deletePromptTemplate(promptTemplate.id, &error));
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

void TestSessionController::ignoresLateAdapterSignalsAfterClose() {
    using snack::domain::AgentEvent;
    using snack::domain::AgentEventType;
    using snack::domain::ConversationStatus;

    MemoryEventRepository repository;
    snack::agent::FakeAgentAdapter adapter(nullptr, 1);
    snack::session::SessionController controller(conversation(), &adapter, &repository);
    controller.open();
    QTRY_COMPARE(controller.status(), ConversationStatus::Idle);
    controller.close();
    QCOMPARE(controller.status(), ConversationStatus::Closed);

    const QString closedDetail = controller.connectionDetail();
    const QString capabilityVersion = controller.capabilities().version;
    QSignalSpy statusSpy(&controller, &snack::session::SessionController::statusChanged);
    QSignalSpy detailSpy(&controller, &snack::session::SessionController::connectionDetailChanged);
    QSignalSpy capabilitySpy(&controller, &snack::session::SessionController::capabilitiesChanged);
    QSignalSpy identitySpy(&controller, &snack::session::SessionController::nativeIdentityChanged);
    QSignalSpy eventSpy(&controller, &snack::session::SessionController::eventRecorded);

    adapter.connectionChanged(false, QStringLiteral("late disconnect"));
    adapter.connectionChanged(true, QStringLiteral("late ready"));
    auto lateCapabilities = adapter.capabilities();
    lateCapabilities.version = QStringLiteral("late-capabilities");
    adapter.capabilitiesChanged(lateCapabilities);
    adapter.nativeIdentityChanged(QStringLiteral("late-thread"), QStringLiteral("late-session"));
    AgentEvent lateEvent;
    lateEvent.turnId = QUuid::createUuid();
    lateEvent.type = AgentEventType::ErrorRaised;
    adapter.eventReceived(lateEvent);

    QCOMPARE(controller.status(), ConversationStatus::Closed);
    QCOMPARE(controller.connectionDetail(), closedDetail);
    QCOMPARE(controller.capabilities().version, capabilityVersion);
    QVERIFY(controller.conversation().nativeThreadId.isEmpty());
    QVERIFY(controller.conversation().nativeSessionId.isEmpty());
    QVERIFY(repository.events_.isEmpty());
    QCOMPARE(statusSpy.count(), 0);
    QCOMPARE(detailSpy.count(), 0);
    QCOMPARE(capabilitySpy.count(), 0);
    QCOMPARE(identitySpy.count(), 0);
    QCOMPARE(eventSpy.count(), 0);
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
