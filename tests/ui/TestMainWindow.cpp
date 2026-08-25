#include "agent/FakeAgentAdapter.h"
#include "app/AppSettings.h"
#include "session/SessionController.h"
#include "storage/EventRepository.h"
#include "ui/MainWindow.h"

#include <QComboBox>
#include <QDir>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTest>

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

QTEST_MAIN(TestMainWindow)
#include "TestMainWindow.moc"
