#include "agent/FakeAgentAdapter.h"
#include "app/AppSettings.h"
#include "session/SessionController.h"
#include "storage/EventRepository.h"
#include "ui/MainWindow.h"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QDockWidget>
#include <QFrame>
#include <QJsonArray>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

class UiMemoryEventRepository final : public snack::storage::IEventRepository {
  public:
    bool saveConversation(const snack::domain::Conversation&, QString*) override { return true; }

    std::optional<snack::domain::Conversation> conversationById(const QUuid&,
                                                                QString*) const override {
        return std::nullopt;
    }

    bool appendEvent(const snack::domain::AgentEvent& event, QString*) override {
        events.append(event);
        return true;
    }

    QList<snack::domain::AgentEvent> eventsForConversation(const QUuid& conversationId,
                                                           QString*) const override {
        QList<snack::domain::AgentEvent> result;
        for (const auto& event : events) {
            if (event.conversationId == conversationId) {
                result.append(event);
            }
        }
        return result;
    }

    QList<snack::domain::AgentEvent> events;
};

class TestMainWindow final : public QObject {
    Q_OBJECT

  private slots:
    void sendsAndRendersStreamingTurn();
    void restoresPersistedTimeline();
    void hidesToTrayWithoutClosingSession();
    void restoresWindowLayout();
    void interruptsRunningTurnFromSendButton();
    void handlesApprovalCard();
    void cancelsQuitWhileAgentIsRunning();
};

void TestMainWindow::sendsAndRendersStreamingTurn() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    snack::app::AppSettings settings(directory.filePath(QStringLiteral("settings.ini")));
    UiMemoryEventRepository repository;
    snack::agent::FakeAgentAdapter adapter(nullptr, 1);
    snack::domain::Conversation conversation;
    conversation.title = QStringLiteral("UI test");
    conversation.workingDirectory = directory.path();
    snack::session::SessionController controller(conversation, &adapter, &repository);
    snack::ui::MainWindow window(&controller, &settings);

    auto* composer = window.findChild<QPlainTextEdit*>(QStringLiteral("composer"));
    auto* sendButton = window.findChild<QPushButton*>(QStringLiteral("sendButton"));
    auto* timeline = window.findChild<QListWidget*>(QStringLiteral("timeline"));
    auto* modelCombo = window.findChild<QComboBox*>(QStringLiteral("modelCombo"));
    auto* effortCombo = window.findChild<QComboBox*>(QStringLiteral("effortCombo"));
    auto* sessionRow = window.findChild<QLabel*>(QStringLiteral("sessionRow"));
    auto* statusLabel = window.findChild<QLabel*>(QStringLiteral("statusLabel"));
    QVERIFY(composer != nullptr);
    QVERIFY(sendButton != nullptr);
    QVERIFY(timeline != nullptr);
    QVERIFY(modelCombo != nullptr);
    QVERIFY(effortCombo != nullptr);
    QVERIFY(sessionRow != nullptr);
    QVERIFY(statusLabel != nullptr);
    QTRY_VERIFY(sendButton->isEnabled());
    QCOMPARE(modelCombo->count(), 2);
    QCOMPARE(modelCombo->currentData().toString(), QStringLiteral("mock-balanced"));
    QVERIFY(sessionRow->text().contains(QStringLiteral("Mock Agent")));
    QCOMPARE(statusLabel->toolTip(), QStringLiteral("mock-v1"));
    window.showStartupNotice(QStringLiteral("Fallback reason"));
    QCOMPARE(sessionRow->toolTip(), QStringLiteral("Fallback reason"));

    sendButton->click();
    QCOMPARE(controller.status(), snack::domain::ConversationStatus::Idle);

    composer->setPlainText(QStringLiteral("Build the foundation"));
    sendButton->click();
    QCOMPARE(controller.status(), snack::domain::ConversationStatus::Running);
    modelCombo->setCurrentIndex(0);
    QCOMPARE(controller.nextTurnSettings().modelId, QStringLiteral("mock-fast"));
    QTRY_COMPARE_WITH_TIMEOUT(controller.status(), snack::domain::ConversationStatus::Idle, 1000);
    QVERIFY(timeline->count() >= 2);
    QVERIFY(timeline->item(0)->text().contains(QStringLiteral("Build the foundation")));
    QVERIFY(timeline->item(1)->text().contains(QStringLiteral("模拟 Agent")));
    QVERIFY(repository.events.size() >= 8);

    const snack::agent::CapabilitySet oneModel{
        .version = QStringLiteral("dynamic-v2"),
        .models = {QStringLiteral("dynamic-model")},
        .defaultModelId = QStringLiteral("dynamic-model"),
        .modelCapabilities = {{.id = QStringLiteral("dynamic-model"),
                               .displayName = QStringLiteral("Dynamic Model"),
                               .defaultReasoningEffortId = QStringLiteral("high"),
                               .supportedReasoningEfforts = {{QStringLiteral("high"), {}}}}},
        .reasoningEfforts = {snack::domain::ReasoningEffort::High},
        .accessLevels = {snack::domain::AccessLevel::Workspace}};
    adapter.capabilitiesChanged(oneModel);
    QCOMPARE(modelCombo->count(), 1);
    QCOMPARE(modelCombo->currentText(), QStringLiteral("Dynamic Model"));
    QCOMPARE(effortCombo->count(), 1);
    QCOMPARE(effortCombo->currentData().toInt(),
             static_cast<int>(snack::domain::ReasoningEffort::High));

    auto* preferMock = window.findChild<QAction*>(QStringLiteral("preferMockAction"));
    QVERIFY(preferMock != nullptr);
    preferMock->trigger();
    QCOMPARE(settings.load().preferredAgentKind, snack::domain::AgentKind::Mock);

    QVERIFY(QMetaObject::invokeMethod(&window, "applyDarkTheme"));
    QVERIFY(QMetaObject::invokeMethod(&window, "increaseScale"));
    window.activateWindowForRequest(std::nullopt);
    window.activateWindowForRequest(QDir::tempPath());
    window.close();
    QCOMPARE(controller.status(), snack::domain::ConversationStatus::Closed);
    const auto savedSettings = settings.load();
    QCOMPARE(savedSettings.themeMode, snack::app::ThemeMode::Dark);
    QCOMPARE(savedSettings.interfaceScale, 1.1);
    QCOMPARE(savedSettings.preferredAgentKind, snack::domain::AgentKind::Mock);
}

void TestMainWindow::restoresPersistedTimeline() {
    QTemporaryDir directory;
    snack::app::AppSettings settings(directory.filePath(QStringLiteral("settings.ini")));
    UiMemoryEventRepository repository;
    snack::agent::FakeAgentAdapter adapter(nullptr, 1);
    snack::domain::Conversation conversation;
    conversation.title = QStringLiteral("Restored UI");
    conversation.workingDirectory = directory.path();

    snack::domain::AgentEvent userEvent;
    userEvent.conversationId = conversation.id;
    userEvent.sequence = 1;
    userEvent.type = snack::domain::AgentEventType::UserMessage;
    userEvent.payload = {{QStringLiteral("text"), QStringLiteral("Persisted question")}};
    repository.events.append(userEvent);
    snack::domain::AgentEvent startEvent;
    startEvent.conversationId = conversation.id;
    startEvent.sequence = 2;
    startEvent.type = snack::domain::AgentEventType::AgentMessageStart;
    repository.events.append(startEvent);
    snack::domain::AgentEvent deltaEvent;
    deltaEvent.conversationId = conversation.id;
    deltaEvent.sequence = 3;
    deltaEvent.type = snack::domain::AgentEventType::AgentMessageDelta;
    deltaEvent.payload = {{QStringLiteral("text"), QStringLiteral("Persisted answer")}};
    repository.events.append(deltaEvent);
    snack::domain::AgentEvent approvalEvent;
    approvalEvent.conversationId = conversation.id;
    approvalEvent.turnId = QUuid::createUuid();
    approvalEvent.sequence = 4;
    approvalEvent.type = snack::domain::AgentEventType::ApprovalRequested;
    approvalEvent.payload = {{QStringLiteral("requestId"), QStringLiteral("restored-approval")},
                             {QStringLiteral("kind"), QStringLiteral("fileChange")},
                             {QStringLiteral("grantRoot"), QStringLiteral("/outside")}};
    repository.events.append(approvalEvent);

    snack::session::SessionController controller(conversation, &adapter, &repository);
    snack::ui::MainWindow window(&controller, &settings);
    auto* timeline = window.findChild<QListWidget*>(QStringLiteral("timeline"));
    QVERIFY(timeline != nullptr);
    QCOMPARE(timeline->count(), 3);
    QVERIFY(timeline->item(0)->text().contains(QStringLiteral("Persisted question")));
    QVERIFY(timeline->item(1)->text().contains(QStringLiteral("Persisted answer")));
    auto* restoredApproval = window.findChild<QPushButton*>(QStringLiteral("approvalAcceptButton"));
    QVERIFY(restoredApproval != nullptr);
    QVERIFY(!restoredApproval->isEnabled());
}

void TestMainWindow::hidesToTrayWithoutClosingSession() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    snack::app::AppSettings settings(directory.filePath(QStringLiteral("settings.ini")));
    UiMemoryEventRepository repository;
    snack::agent::FakeAgentAdapter adapter(nullptr, 1);
    snack::domain::Conversation conversation;
    conversation.title = QStringLiteral("Tray test");
    conversation.workingDirectory = directory.path();
    snack::session::SessionController controller(conversation, &adapter, &repository);
    snack::ui::MainWindow window(&controller, &settings, true);

    window.show();
    QTRY_VERIFY(window.isVisible());
    QTRY_COMPARE(controller.status(), snack::domain::ConversationStatus::Idle);
    window.close();
    QTRY_VERIFY(!window.isVisible());
    QCOMPARE(controller.status(), snack::domain::ConversationStatus::Idle);

    const auto saved = settings.load();
    QVERIFY(!saved.mainWindowGeometry.isEmpty());
    QVERIFY(!saved.mainWindowState.isEmpty());

    window.activateWindowForRequest(std::nullopt);
    QTRY_VERIFY(window.isVisible());
    window.hide();
    controller.close();
}

void TestMainWindow::restoresWindowLayout() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString settingsPath = directory.filePath(QStringLiteral("settings.ini"));
    snack::app::AppSettings settings(settingsPath);
    UiMemoryEventRepository firstRepository;
    snack::agent::FakeAgentAdapter firstAdapter(nullptr, 1);
    snack::domain::Conversation firstConversation;
    firstConversation.title = QStringLiteral("Layout source");
    firstConversation.workingDirectory = directory.path();

    {
        snack::session::SessionController controller(firstConversation, &firstAdapter,
                                                     &firstRepository);
        snack::ui::MainWindow window(&controller, &settings, false);
        auto* taskDock = window.findChild<QDockWidget*>(QStringLiteral("taskDock"));
        QVERIFY(taskDock != nullptr);
        taskDock->setFloating(true);
        taskDock->show();
        window.resize(1360, 840);
        window.show();
        QTest::qWait(20);
        window.close();
    }

    const auto saved = settings.load();
    QVERIFY(!saved.mainWindowGeometry.isEmpty());
    QVERIFY(!saved.mainWindowState.isEmpty());

    UiMemoryEventRepository secondRepository;
    snack::agent::FakeAgentAdapter secondAdapter(nullptr, 1);
    snack::domain::Conversation secondConversation;
    secondConversation.title = QStringLiteral("Layout target");
    secondConversation.workingDirectory = directory.path();
    snack::session::SessionController secondController(secondConversation, &secondAdapter,
                                                       &secondRepository);
    snack::ui::MainWindow restored(&secondController, &settings, false);
    auto* restoredTaskDock = restored.findChild<QDockWidget*>(QStringLiteral("taskDock"));
    QVERIFY(restoredTaskDock != nullptr);
    QVERIFY(restoredTaskDock->isFloating());
    QCOMPARE(restored.saveState(1), saved.mainWindowState);
    restored.close();
}

void TestMainWindow::interruptsRunningTurnFromSendButton() {
    QTemporaryDir directory;
    snack::app::AppSettings settings(directory.filePath(QStringLiteral("settings.ini")));
    UiMemoryEventRepository repository;
    snack::agent::FakeAgentAdapter adapter(nullptr, 100);
    snack::domain::Conversation conversation;
    conversation.title = QStringLiteral("Interrupt UI");
    conversation.workingDirectory = directory.path();
    snack::session::SessionController controller(conversation, &adapter, &repository);
    snack::ui::MainWindow window(&controller, &settings, false);

    auto* composer = window.findChild<QPlainTextEdit*>(QStringLiteral("composer"));
    auto* sendButton = window.findChild<QPushButton*>(QStringLiteral("sendButton"));
    QTRY_COMPARE(controller.status(), snack::domain::ConversationStatus::Idle);
    composer->setPlainText(QStringLiteral("Stop this turn"));
    sendButton->click();
    QCOMPARE(controller.status(), snack::domain::ConversationStatus::Running);
    QCOMPARE(sendButton->text(), QStringLiteral("Stop"));
    QVERIFY(sendButton->isEnabled());

    sendButton->click();
    QCOMPARE(controller.status(), snack::domain::ConversationStatus::Idle);
    QCOMPARE(repository.events.constLast().type, snack::domain::AgentEventType::TurnInterrupted);
    QCOMPARE(sendButton->text(), QStringLiteral("Send"));
    window.close();
}

void TestMainWindow::handlesApprovalCard() {
    using snack::domain::AgentEvent;
    using snack::domain::AgentEventType;
    using snack::domain::ApprovalDecision;
    using snack::domain::ConversationStatus;

    QTemporaryDir directory;
    snack::app::AppSettings settings(directory.filePath(QStringLiteral("settings.ini")));
    UiMemoryEventRepository repository;
    snack::agent::FakeAgentAdapter adapter(nullptr, 1000);
    snack::domain::Conversation conversation;
    conversation.title = QStringLiteral("Approval UI");
    conversation.workingDirectory = directory.path();
    snack::session::SessionController controller(conversation, &adapter, &repository);
    snack::ui::MainWindow window(&controller, &settings, false);

    auto* composer = window.findChild<QPlainTextEdit*>(QStringLiteral("composer"));
    auto* sendButton = window.findChild<QPushButton*>(QStringLiteral("sendButton"));
    QTRY_COMPARE(controller.status(), ConversationStatus::Idle);
    composer->setPlainText(QStringLiteral("Run a command"));
    sendButton->click();
    QCOMPARE(controller.status(), ConversationStatus::Running);

    AgentEvent request;
    request.turnId = repository.events.constFirst().turnId;
    request.type = AgentEventType::ApprovalRequested;
    request.payload = {{QStringLiteral("requestId"), QStringLiteral("ui-approval")},
                       {QStringLiteral("kind"), QStringLiteral("commandExecution")},
                       {QStringLiteral("command"), QStringLiteral("git status")},
                       {QStringLiteral("cwd"), directory.path()},
                       {QStringLiteral("reason"), QStringLiteral("Inspect the workspace")},
                       {QStringLiteral("availableDecisions"),
                        QJsonArray{QStringLiteral("accept"), QStringLiteral("decline")}}};
    adapter.eventReceived(request);

    QCOMPARE(controller.status(), ConversationStatus::WaitingApproval);
    QCOMPARE(sendButton->text(), QStringLiteral("Stop"));
    auto* card = window.findChild<QFrame*>(QStringLiteral("approvalCard"));
    auto* allow = window.findChild<QPushButton*>(QStringLiteral("approvalAcceptButton"));
    auto* allowSession = window.findChild<QPushButton*>(QStringLiteral("approvalSessionButton"));
    auto* approvalStatus = window.findChild<QLabel*>(QStringLiteral("approvalStatus"));
    QVERIFY(card != nullptr);
    QVERIFY(allow != nullptr);
    QVERIFY(allowSession != nullptr);
    QVERIFY(approvalStatus != nullptr);
    QVERIFY(allow->isEnabled());
    QVERIFY(!allowSession->isEnabled());
    allow->click();

    QCOMPARE(adapter.lastApprovalRequestId(), QStringLiteral("ui-approval"));
    QCOMPARE(adapter.lastApprovalDecision(), ApprovalDecision::Accept);
    QCOMPARE(controller.status(), ConversationStatus::Running);
    QVERIFY(!allow->isEnabled());
    QVERIFY(approvalStatus->text().contains(QStringLiteral("accept")));

    sendButton->click();
    QCOMPARE(controller.status(), ConversationStatus::Idle);
    window.close();
}

void TestMainWindow::cancelsQuitWhileAgentIsRunning() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    snack::app::AppSettings settings(directory.filePath(QStringLiteral("settings.ini")));
    UiMemoryEventRepository repository;
    snack::agent::FakeAgentAdapter adapter(nullptr, 20);
    snack::domain::Conversation conversation;
    conversation.title = QStringLiteral("Quit confirmation test");
    conversation.workingDirectory = directory.path();
    snack::session::SessionController controller(conversation, &adapter, &repository);
    snack::ui::MainWindow window(&controller, &settings, false);

    auto* composer = window.findChild<QPlainTextEdit*>(QStringLiteral("composer"));
    auto* sendButton = window.findChild<QPushButton*>(QStringLiteral("sendButton"));
    QVERIFY(composer != nullptr);
    QVERIFY(sendButton != nullptr);
    QTRY_VERIFY(sendButton->isEnabled());
    composer->setPlainText(QStringLiteral("Keep running"));
    sendButton->click();
    QCOMPARE(controller.status(), snack::domain::ConversationStatus::Running);

    bool promptFound = false;
    QTimer::singleShot(0, [&promptFound] {
        auto* prompt = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        promptFound = prompt != nullptr;
        if (prompt != nullptr) {
            prompt->reject();
        }
    });
    QVERIFY(QMetaObject::invokeMethod(&window, "requestQuit"));
    QVERIFY(promptFound);
    QVERIFY(controller.status() != snack::domain::ConversationStatus::Closed);
    QTRY_COMPARE_WITH_TIMEOUT(controller.status(), snack::domain::ConversationStatus::Idle, 1000);
    window.close();
}

QTEST_MAIN(TestMainWindow)
#include "TestMainWindow.moc"
