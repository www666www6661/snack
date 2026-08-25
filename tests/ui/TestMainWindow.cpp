#include "agent/FakeAgentAdapter.h"
#include "app/AppSettings.h"
#include "session/SessionController.h"
#include "storage/EventRepository.h"
#include "ui/MainWindow.h"

#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QDockWidget>
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
    QVERIFY(composer != nullptr);
    QVERIFY(sendButton != nullptr);
    QVERIFY(timeline != nullptr);
    QVERIFY(modelCombo != nullptr);
    QTRY_VERIFY(sendButton->isEnabled());

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

    QVERIFY(QMetaObject::invokeMethod(&window, "applyDarkTheme"));
    QVERIFY(QMetaObject::invokeMethod(&window, "increaseScale"));
    window.activateWindowForRequest(std::nullopt);
    window.activateWindowForRequest(QDir::tempPath());
    window.close();
    QCOMPARE(controller.status(), snack::domain::ConversationStatus::Closed);
    const auto savedSettings = settings.load();
    QCOMPARE(savedSettings.themeMode, snack::app::ThemeMode::Dark);
    QCOMPARE(savedSettings.interfaceScale, 1.1);
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

    snack::session::SessionController controller(conversation, &adapter, &repository);
    snack::ui::MainWindow window(&controller, &settings);
    auto* timeline = window.findChild<QListWidget*>(QStringLiteral("timeline"));
    QVERIFY(timeline != nullptr);
    QCOMPARE(timeline->count(), 2);
    QVERIFY(timeline->item(0)->text().contains(QStringLiteral("Persisted question")));
    QVERIFY(timeline->item(1)->text().contains(QStringLiteral("Persisted answer")));
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
