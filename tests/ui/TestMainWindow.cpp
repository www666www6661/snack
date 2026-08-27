#include "agent/FakeAgentAdapter.h"
#include "app/AppSettings.h"
#include "app/SessionManager.h"
#include "session/SessionController.h"
#include "storage/EventRepository.h"
#include "ui/MainWindow.h"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QDockWidget>
#include <QFrame>
#include <QInputDialog>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

class UiMemoryEventRepository final : public snack::storage::IEventRepository {
  public:
    bool saveConversation(const snack::domain::Conversation& conversation, QString*) override {
        const auto iterator =
            std::find_if(catalog.begin(), catalog.end(), [&conversation](const auto& existing) {
                return existing.id == conversation.id;
            });
        if (iterator == catalog.end()) {
            catalog.append(conversation);
        } else {
            *iterator = conversation;
        }
        return true;
    }

    std::optional<snack::domain::Conversation> conversationById(const QUuid& conversationId,
                                                                QString*) const override {
        const auto iterator = std::find_if(catalog.cbegin(), catalog.cend(),
                                           [&conversationId](const auto& conversation) {
                                               return conversation.id == conversationId;
                                           });
        return iterator == catalog.cend() ? std::nullopt
                                          : std::optional<snack::domain::Conversation>(*iterator);
    }

    QList<snack::domain::Conversation> conversations(QString*) const override {
        auto result = catalog;
        std::sort(result.begin(), result.end(), [](const auto& left, const auto& right) {
            if (left.archived != right.archived) {
                return !left.archived;
            }
            if (left.pinned != right.pinned) {
                return left.pinned;
            }
            const int titleOrder = left.title.compare(right.title, Qt::CaseInsensitive);
            return titleOrder != 0 ? titleOrder < 0 : left.id.toString() < right.id.toString();
        });
        return result;
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

    bool replaceQueuedMessages(const QUuid& conversationId,
                               const QList<snack::domain::QueuedMessage>& messages,
                               QString*) override {
        queues.insert(conversationId, messages);
        return true;
    }

    QList<snack::domain::QueuedMessage> queuedMessagesForConversation(const QUuid& conversationId,
                                                                      QString*) const override {
        return queues.value(conversationId);
    }

    bool savePromptTemplate(const snack::domain::PromptTemplate& promptTemplate,
                            QString*) override {
        templates.insert(promptTemplate.id, promptTemplate);
        return true;
    }

    bool deletePromptTemplate(const QUuid& templateId, QString*) override {
        return templates.remove(templateId) > 0;
    }

    QList<snack::domain::PromptTemplate> promptTemplates(QString*) const override {
        QList<snack::domain::PromptTemplate> result = templates.values();
        std::sort(result.begin(), result.end(), [](const auto& left, const auto& right) {
            return left.position < right.position;
        });
        return result;
    }

    QList<snack::domain::AgentEvent> events;
    QList<snack::domain::Conversation> catalog;
    QHash<QUuid, QList<snack::domain::QueuedMessage>> queues;
    QHash<QUuid, snack::domain::PromptTemplate> templates;
};

class TestMainWindow final : public QObject {
    Q_OBJECT

  private slots:
    void sendsAndRendersStreamingTurn();
    void updatesPlaceholderTitleAfterFirstSend();
    void renamesCurrentConversation();
    void restoresPersistedTimeline();
    void restoresToolReasoningAndPlanViews();
    void hidesToTrayWithoutClosingSession();
    void restoresWindowLayout();
    void interruptsRunningTurnFromSendButton();
    void editsAndControlsQueuedMessages();
    void supportsComposerShortcutsGrowthAndDrafts();
    void insertsAndManagesPromptTemplates();
    void reconnectsDisconnectedSession();
    void handlesApprovalCard();
    void handlesUserInputCardAndUsage();
    void cancelsQuitWhileAgentIsRunning();
    void opensAndSwitchesConversationFromRail();
    void keepsCurrentConversationWhenRailOpenFails();
    void filtersConversationRailLocally();
    void focusesConversationSearchWithShortcut();
    void createsConversationFromRail();
    void archivesAndRestoresConversation();
    void pinsAndReordersConversationRail();
    void operatesOnSelectedConversationFromContextMenu();
    void updatesConversationRailForBackgroundRuntimeStatus();
    void persistsArchivedConversationVisibility();
    void cyclesActiveConversationsInRepositoryOrder();
};

void TestMainWindow::opensAndSwitchesConversationFromRail() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    snack::app::AppSettings settings(directory.filePath(QStringLiteral("settings.ini")));
    UiMemoryEventRepository repository;
    snack::domain::Conversation first;
    first.title = QStringLiteral("First session");
    first.workingDirectory = directory.filePath(QStringLiteral("first"));
    snack::domain::Conversation second;
    second.title = QStringLiteral("Second session");
    second.workingDirectory = directory.filePath(QStringLiteral("second"));
    repository.catalog = {first, second};
    snack::domain::AgentEvent secondMessage;
    secondMessage.conversationId = second.id;
    secondMessage.sequence = 1;
    secondMessage.type = snack::domain::AgentEventType::UserMessage;
    secondMessage.payload = {{QStringLiteral("text"), QStringLiteral("Second timeline")}};
    repository.events.append(secondMessage);
    settings.saveComposerDraft(first.id, QStringLiteral("First draft"));
    settings.saveComposerDraft(second.id, QStringLiteral("Second draft"));

    const auto makeRuntime = [](snack::domain::AgentKind kind) {
        snack::agent::AgentRuntime runtime;
        runtime.requestedKind = kind;
        runtime.selectedKind = kind;
        runtime.adapter = std::make_unique<snack::agent::FakeAgentAdapter>(nullptr, 1);
        return runtime;
    };
    snack::app::SessionManager sessions(&repository, makeRuntime);
    QString error;
    auto* firstController = sessions.addPrepared(first, makeRuntime(first.agentKind), &error);
    QVERIFY2(firstController != nullptr, qPrintable(error));
    snack::ui::MainWindow window(firstController, &settings, &sessions, false);
    auto* list = window.findChild<QListWidget*>(QStringLiteral("conversationList"));
    auto* title = window.findChild<QLabel*>(QStringLiteral("conversationTitle"));
    auto* composer = window.findChild<QPlainTextEdit*>(QStringLiteral("composer"));
    auto* timeline = window.findChild<QListWidget*>(QStringLiteral("timeline"));
    QVERIFY(list != nullptr);
    QVERIFY(title != nullptr);
    QVERIFY(composer != nullptr);
    QVERIFY(timeline != nullptr);
    QCOMPARE(list->count(), 2);
    QCOMPARE(composer->toPlainText(), QStringLiteral("First draft"));

    QListWidgetItem* secondItem = nullptr;
    for (int row = 0; row < list->count(); ++row) {
        if (list->item(row)->data(Qt::UserRole).toUuid() == second.id) {
            secondItem = list->item(row);
        }
    }
    QVERIFY(secondItem != nullptr);
    emit list->itemClicked(secondItem);

    QCOMPARE(title->text(), QStringLiteral("Second session"));
    QCOMPARE(composer->toPlainText(), QStringLiteral("Second draft"));
    QCOMPARE(settings.composerDraft(first.id), QStringLiteral("First draft"));
    QCOMPARE(timeline->count(), 1);
    QVERIFY(timeline->item(0)->text().contains(QStringLiteral("Second timeline")));
    QCOMPARE(sessions.size(), qsizetype{2});
    QVERIFY(sessions.controller(second.id) != nullptr);
    QCOMPARE(settings.load().lastConversationId, second.id.toString(QUuid::WithoutBraces));
    QCOMPARE(list->currentItem()->data(Qt::UserRole).toUuid(), second.id);
}

void TestMainWindow::keepsCurrentConversationWhenRailOpenFails() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    snack::app::AppSettings settings(directory.filePath(QStringLiteral("settings.ini")));
    UiMemoryEventRepository repository;
    snack::domain::Conversation current;
    current.title = QStringLiteral("Current session");
    current.workingDirectory = directory.path();
    snack::domain::Conversation unavailable;
    unavailable.title = QStringLiteral("Unavailable Codex session");
    unavailable.workingDirectory = directory.path();
    unavailable.agentKind = snack::domain::AgentKind::Codex;
    repository.catalog = {current, unavailable};

    const auto mockRuntime = [](snack::domain::AgentKind requestedKind) {
        snack::agent::AgentRuntime runtime;
        runtime.requestedKind = requestedKind;
        runtime.selectedKind = snack::domain::AgentKind::Mock;
        runtime.detail = QStringLiteral("Codex unavailable");
        runtime.fellBack = requestedKind != snack::domain::AgentKind::Mock;
        runtime.adapter = std::make_unique<snack::agent::FakeAgentAdapter>(nullptr, 1);
        return runtime;
    };
    snack::app::SessionManager sessions(&repository, mockRuntime);
    QString error;
    auto* controller = sessions.addPrepared(current, mockRuntime(current.agentKind), &error);
    QVERIFY2(controller != nullptr, qPrintable(error));
    snack::ui::MainWindow window(controller, &settings, &sessions, false);
    auto* list = window.findChild<QListWidget*>(QStringLiteral("conversationList"));
    auto* title = window.findChild<QLabel*>(QStringLiteral("conversationTitle"));
    QVERIFY(list != nullptr);
    QVERIFY(title != nullptr);

    QListWidgetItem* unavailableItem = nullptr;
    for (int row = 0; row < list->count(); ++row) {
        if (list->item(row)->data(Qt::UserRole).toUuid() == unavailable.id) {
            unavailableItem = list->item(row);
        }
    }
    QVERIFY(unavailableItem != nullptr);
    emit list->itemClicked(unavailableItem);

    QCOMPARE(title->text(), QStringLiteral("Current session"));
    QCOMPARE(sessions.size(), qsizetype{1});
    QCOMPARE(list->currentItem()->data(Qt::UserRole).toUuid(), current.id);
    QVERIFY(window.statusBar()->currentMessage().contains(QStringLiteral("Codex unavailable")));
}

void TestMainWindow::filtersConversationRailLocally() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    snack::app::AppSettings settings(directory.filePath(QStringLiteral("settings.ini")));
    UiMemoryEventRepository repository;
    snack::domain::Conversation codex;
    codex.title = QStringLiteral("Review parser");
    codex.workingDirectory = QStringLiteral("C:/projects/compiler");
    codex.agentKind = snack::domain::AgentKind::Codex;
    snack::domain::Conversation mock;
    mock.title = QStringLiteral("Prototype UI");
    mock.workingDirectory = QStringLiteral("C:/projects/snack");
    repository.catalog = {codex, mock};

    snack::agent::FakeAgentAdapter adapter(nullptr, 1);
    snack::session::SessionController controller(mock, &adapter, &repository);
    snack::ui::MainWindow window(&controller, &settings, false);
    auto* search = window.findChild<QLineEdit*>(QStringLiteral("conversationSearch"));
    auto* list = window.findChild<QListWidget*>(QStringLiteral("conversationList"));
    QVERIFY(search != nullptr);
    QVERIFY(list != nullptr);
    QCOMPARE(list->count(), 2);

    search->setText(QStringLiteral("parser"));
    QCOMPARE(list->count(), 1);
    QCOMPARE(list->item(0)->data(Qt::UserRole).toUuid(), codex.id);
    search->setText(QStringLiteral("SNACK"));
    QCOMPARE(list->count(), 1);
    QCOMPARE(list->item(0)->data(Qt::UserRole).toUuid(), mock.id);
    search->setText(QStringLiteral("codex"));
    QCOMPARE(list->count(), 1);
    QCOMPARE(list->item(0)->data(Qt::UserRole).toUuid(), codex.id);
    search->clear();
    QCOMPARE(list->count(), 2);
    QTRY_COMPARE(controller.status(), snack::domain::ConversationStatus::Idle);
}

void TestMainWindow::focusesConversationSearchWithShortcut() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    snack::app::AppSettings settings(directory.filePath(QStringLiteral("settings.ini")));
    UiMemoryEventRepository repository;
    snack::domain::Conversation first;
    first.title = QStringLiteral("First session");
    first.workingDirectory = directory.path();
    snack::domain::Conversation second;
    second.title = QStringLiteral("Second session");
    second.workingDirectory = directory.path();
    repository.catalog = {first, second};

    snack::agent::FakeAgentAdapter adapter(nullptr, 1);
    snack::session::SessionController controller(first, &adapter, &repository);
    snack::ui::MainWindow window(&controller, &settings, false);
    auto* search = window.findChild<QLineEdit*>(QStringLiteral("conversationSearch"));
    auto* list = window.findChild<QListWidget*>(QStringLiteral("conversationList"));
    auto* action = window.findChild<QAction*>(QStringLiteral("searchConversationsAction"));
    QVERIFY(search != nullptr);
    QVERIFY(list != nullptr);
    QVERIFY(action != nullptr);
    QCOMPARE(action->shortcut(), QKeySequence(Qt::CTRL | Qt::Key_K));
    search->setText(QStringLiteral("Second"));
    QCOMPARE(list->count(), 1);

    action->trigger();

    QCOMPARE(window.focusWidget(), search);
    QVERIFY(search->text().isEmpty());
    QCOMPARE(list->count(), 2);
    QTRY_COMPARE(controller.status(), snack::domain::ConversationStatus::Idle);
}

void TestMainWindow::createsConversationFromRail() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    snack::app::AppSettings settings(directory.filePath(QStringLiteral("settings.ini")));
    auto snapshot = settings.load();
    snapshot.preferredAgentKind = snack::domain::AgentKind::Codex;
    settings.save(snapshot);
    UiMemoryEventRepository repository;
    snack::domain::Conversation first;
    first.title = QStringLiteral("Existing session");
    first.workingDirectory = directory.path();
    repository.catalog = {first};

    const auto mockFallback = [](snack::domain::AgentKind requestedKind) {
        snack::agent::AgentRuntime runtime;
        runtime.requestedKind = requestedKind;
        runtime.selectedKind = snack::domain::AgentKind::Mock;
        runtime.detail = QStringLiteral("Codex unavailable");
        runtime.fellBack = requestedKind == snack::domain::AgentKind::Codex;
        runtime.adapter = std::make_unique<snack::agent::FakeAgentAdapter>(nullptr, 1);
        return runtime;
    };
    snack::app::SessionManager sessions(&repository, mockFallback);
    QString error;
    auto* firstController = sessions.addPrepared(first, mockFallback(first.agentKind), &error);
    QVERIFY2(firstController != nullptr, qPrintable(error));
    snack::ui::MainWindow window(firstController, &settings, &sessions, false);
    auto* newButton = window.findChild<QPushButton*>(QStringLiteral("newConversationButton"));
    auto* newAction = window.findChild<QAction*>(QStringLiteral("newConversationAction"));
    auto* list = window.findChild<QListWidget*>(QStringLiteral("conversationList"));
    auto* title = window.findChild<QLabel*>(QStringLiteral("conversationTitle"));
    auto* notice = window.findChild<QFrame*>(QStringLiteral("connectionNoticeFrame"));
    QVERIFY(newButton != nullptr);
    QVERIFY(newAction != nullptr);
    QVERIFY(list != nullptr);
    QVERIFY(title != nullptr);
    QVERIFY(notice != nullptr);
    QVERIFY(newButton->isEnabled());
    QCOMPARE(newAction->shortcut(), QKeySequence::New);

    newButton->click();

    QCOMPARE(sessions.size(), qsizetype{2});
    QCOMPARE(title->text(), QStringLiteral("New conversation"));
    QCOMPARE(list->count(), 2);
    const QUuid createdId = list->currentItem()->data(Qt::UserRole).toUuid();
    QVERIFY(createdId != first.id);
    QCOMPARE(sessions.controller(createdId)->conversation().agentKind,
             snack::domain::AgentKind::Mock);
    QVERIFY(!notice->isHidden());
    QVERIFY(window.statusBar()->currentMessage().contains(QStringLiteral("Codex unavailable")));
    QCOMPARE(settings.load().lastConversationId, createdId.toString(QUuid::WithoutBraces));
}

void TestMainWindow::archivesAndRestoresConversation() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    snack::app::AppSettings settings(directory.filePath(QStringLiteral("settings.ini")));
    auto snapshot = settings.load();
    snapshot.preferredAgentKind = snack::domain::AgentKind::Mock;
    settings.save(snapshot);
    UiMemoryEventRepository repository;
    snack::domain::Conversation first;
    first.title = QStringLiteral("Archive me");
    first.workingDirectory = directory.path();
    snack::domain::Conversation second;
    second.title = QStringLiteral("Keep me");
    second.workingDirectory = directory.path();
    repository.catalog = {first, second};

    const auto makeRuntime = [](snack::domain::AgentKind kind) {
        snack::agent::AgentRuntime runtime;
        runtime.requestedKind = kind;
        runtime.selectedKind = kind;
        runtime.adapter = std::make_unique<snack::agent::FakeAgentAdapter>(nullptr, 1);
        return runtime;
    };
    snack::app::SessionManager sessions(&repository, makeRuntime);
    QString error;
    auto* firstController = sessions.addPrepared(first, makeRuntime(first.agentKind), &error);
    QVERIFY2(firstController != nullptr, qPrintable(error));
    snack::ui::MainWindow window(firstController, &settings, &sessions, false);
    auto* archive = window.findChild<QAction*>(QStringLiteral("archiveConversationAction"));
    auto* restore = window.findChild<QAction*>(QStringLiteral("restoreConversationAction"));
    auto* list = window.findChild<QListWidget*>(QStringLiteral("conversationList"));
    auto* title = window.findChild<QLabel*>(QStringLiteral("conversationTitle"));
    QVERIFY(archive != nullptr);
    QVERIFY(restore != nullptr);
    QVERIFY(list != nullptr);
    QVERIFY(title != nullptr);
    QTRY_COMPARE(firstController->status(), snack::domain::ConversationStatus::Idle);

    archive->trigger();
    QCOMPARE(title->text(), QStringLiteral("Keep me"));
    QVERIFY(sessions.controller(first.id) == nullptr);
    QCOMPARE(list->count(), 2);

    QListWidgetItem* archivedItem = nullptr;
    for (int row = 0; row < list->count(); ++row) {
        if (list->item(row)->data(Qt::UserRole).toUuid() == first.id) {
            archivedItem = list->item(row);
        }
    }
    QVERIFY(archivedItem != nullptr);
    QVERIFY(archivedItem->data(Qt::UserRole + 1).toBool());
    list->setCurrentItem(archivedItem);
    restore->trigger();

    QCOMPARE(title->text(), QStringLiteral("Archive me"));
    QVERIFY(sessions.controller(first.id) != nullptr);
    QVERIFY(!list->currentItem()->data(Qt::UserRole + 1).toBool());
    QCOMPARE(repository.events.size(), 0);
}

void TestMainWindow::pinsAndReordersConversationRail() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    snack::app::AppSettings settings(directory.filePath(QStringLiteral("settings.ini")));
    UiMemoryEventRepository repository;
    snack::domain::Conversation first;
    first.title = QStringLiteral("Zulu");
    first.workingDirectory = directory.path();
    snack::domain::Conversation second;
    second.title = QStringLiteral("Alpha");
    second.workingDirectory = directory.path();
    repository.catalog = {first, second};

    const auto makeRuntime = [](snack::domain::AgentKind kind) {
        snack::agent::AgentRuntime runtime;
        runtime.requestedKind = kind;
        runtime.selectedKind = kind;
        runtime.adapter = std::make_unique<snack::agent::FakeAgentAdapter>(nullptr, 1);
        return runtime;
    };
    snack::app::SessionManager sessions(&repository, makeRuntime);
    QString error;
    auto* controller = sessions.addPrepared(first, makeRuntime(first.agentKind), &error);
    QVERIFY(controller != nullptr);
    snack::ui::MainWindow window(controller, &settings, &sessions, false);
    auto* pin = window.findChild<QAction*>(QStringLiteral("pinConversationAction"));
    auto* list = window.findChild<QListWidget*>(QStringLiteral("conversationList"));
    QVERIFY(pin != nullptr);
    QVERIFY(list != nullptr);
    QCOMPARE(list->item(0)->data(Qt::UserRole).toUuid(), second.id);

    pin->trigger();
    QVERIFY(controller->conversation().pinned);
    QCOMPARE(list->item(0)->data(Qt::UserRole).toUuid(), first.id);
    QVERIFY(list->item(0)->text().contains(QStringLiteral("Pinned")));
    QCOMPARE(pin->text(), QStringLiteral("Unpin conversation"));
    pin->trigger();
    QVERIFY(!controller->conversation().pinned);
    QCOMPARE(list->item(0)->data(Qt::UserRole).toUuid(), second.id);
    QCOMPARE(pin->text(), QStringLiteral("Pin conversation"));
}

void TestMainWindow::operatesOnSelectedConversationFromContextMenu() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    snack::app::AppSettings settings(directory.filePath(QStringLiteral("settings.ini")));
    UiMemoryEventRepository repository;
    snack::domain::Conversation first;
    first.title = QStringLiteral("Current");
    first.workingDirectory = directory.path();
    snack::domain::Conversation second;
    second.title = QStringLiteral("Selected");
    second.workingDirectory = directory.path();
    repository.catalog = {first, second};

    const auto makeRuntime = [](snack::domain::AgentKind kind) {
        snack::agent::AgentRuntime runtime;
        runtime.requestedKind = kind;
        runtime.selectedKind = kind;
        runtime.adapter = std::make_unique<snack::agent::FakeAgentAdapter>(nullptr, 1);
        return runtime;
    };
    snack::app::SessionManager sessions(&repository, makeRuntime);
    QString error;
    auto* controller = sessions.addPrepared(first, makeRuntime(first.agentKind), &error);
    QVERIFY2(controller != nullptr, qPrintable(error));
    snack::ui::MainWindow window(controller, &settings, &sessions, false);
    auto* list = window.findChild<QListWidget*>(QStringLiteral("conversationList"));
    auto* open = window.findChild<QAction*>(QStringLiteral("contextOpenConversationAction"));
    auto* pin = window.findChild<QAction*>(QStringLiteral("contextPinConversationAction"));
    auto* archive = window.findChild<QAction*>(QStringLiteral("contextArchiveConversationAction"));
    auto* restore = window.findChild<QAction*>(QStringLiteral("contextRestoreConversationAction"));
    auto* title = window.findChild<QLabel*>(QStringLiteral("conversationTitle"));
    QVERIFY(list != nullptr);
    QVERIFY(open != nullptr);
    QVERIFY(pin != nullptr);
    QVERIFY(archive != nullptr);
    QVERIFY(restore != nullptr);
    QVERIFY(title != nullptr);

    const auto selectConversation = [list](const QUuid& id) {
        for (int row = 0; row < list->count(); ++row) {
            if (list->item(row)->data(Qt::UserRole).toUuid() == id) {
                list->setCurrentRow(row);
                return;
            }
        }
    };
    selectConversation(second.id);
    QVERIFY(QMetaObject::invokeMethod(&window, "prepareConversationContextMenu"));
    QVERIFY(open->isEnabled());
    QVERIFY(pin->isEnabled());
    QVERIFY(archive->isEnabled());
    QVERIFY(!restore->isEnabled());

    pin->trigger();
    QVERIFY(repository.conversationById(second.id, nullptr)->pinned);
    QCOMPARE(sessions.size(), qsizetype{1});
    selectConversation(second.id);
    QVERIFY(QMetaObject::invokeMethod(&window, "prepareConversationContextMenu"));
    QCOMPARE(pin->text(), QStringLiteral("Unpin conversation"));
    archive->trigger();
    QVERIFY(repository.conversationById(second.id, nullptr)->archived);
    QCOMPARE(title->text(), QStringLiteral("Current"));
    QCOMPARE(sessions.size(), qsizetype{1});

    selectConversation(second.id);
    QVERIFY(QMetaObject::invokeMethod(&window, "prepareConversationContextMenu"));
    QVERIFY(!open->isEnabled());
    QVERIFY(!archive->isEnabled());
    QVERIFY(restore->isEnabled());
    restore->trigger();
    QCOMPARE(title->text(), QStringLiteral("Selected"));
    QVERIFY(!repository.conversationById(second.id, nullptr)->archived);
    QCOMPARE(sessions.size(), qsizetype{2});

    selectConversation(first.id);
    QVERIFY(QMetaObject::invokeMethod(&window, "prepareConversationContextMenu"));
    QVERIFY(open->isEnabled());
    open->trigger();
    QCOMPARE(title->text(), QStringLiteral("Current"));
}

void TestMainWindow::updatesConversationRailForBackgroundRuntimeStatus() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    snack::app::AppSettings settings(directory.filePath(QStringLiteral("settings.ini")));
    UiMemoryEventRepository repository;
    snack::domain::Conversation first;
    first.title = QStringLiteral("Current");
    first.workingDirectory = directory.path();
    snack::domain::Conversation second;
    second.title = QStringLiteral("Background");
    second.workingDirectory = directory.path();
    repository.catalog = {first, second};

    const auto makeRuntime = [](snack::domain::AgentKind kind) {
        snack::agent::AgentRuntime runtime;
        runtime.requestedKind = kind;
        runtime.selectedKind = kind;
        runtime.adapter = std::make_unique<snack::agent::FakeAgentAdapter>(nullptr, 100);
        return runtime;
    };
    snack::app::SessionManager sessions(&repository, makeRuntime);
    QString error;
    auto* firstController = sessions.addPrepared(first, makeRuntime(first.agentKind), &error);
    auto* secondController = sessions.addPrepared(second, makeRuntime(second.agentKind), &error);
    QVERIFY2(firstController != nullptr, qPrintable(error));
    QVERIFY2(secondController != nullptr, qPrintable(error));
    snack::ui::MainWindow window(firstController, &settings, &sessions, false);
    auto* list = window.findChild<QListWidget*>(QStringLiteral("conversationList"));
    QVERIFY(list != nullptr);
    secondController->open();
    QTRY_COMPARE(secondController->status(), snack::domain::ConversationStatus::Idle);

    QString sendError;
    QVERIFY2(secondController->sendMessage(QStringLiteral("Background work"), &sendError),
             qPrintable(sendError));
    QCOMPARE(secondController->status(), snack::domain::ConversationStatus::Running);

    const auto backgroundItem = [list, id = second.id] {
        for (int row = 0; row < list->count(); ++row) {
            if (list->item(row)->data(Qt::UserRole).toUuid() == id) {
                return list->item(row);
            }
        }
        return static_cast<QListWidgetItem*>(nullptr);
    };
    QVERIFY(backgroundItem() != nullptr);
    QVERIFY(backgroundItem()->text().contains(QStringLiteral("Running")));
    QCOMPARE(window.findChild<QLabel*>(QStringLiteral("conversationTitle"))->text(),
             QStringLiteral("Current"));
    QTRY_COMPARE_WITH_TIMEOUT(secondController->status(), snack::domain::ConversationStatus::Idle,
                              1000);
    QTRY_VERIFY(backgroundItem() != nullptr &&
                backgroundItem()->text().contains(QStringLiteral("Idle")));
}

void TestMainWindow::persistsArchivedConversationVisibility() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString settingsPath = directory.filePath(QStringLiteral("settings.ini"));
    snack::app::AppSettings settings(settingsPath);
    UiMemoryEventRepository repository;
    snack::domain::Conversation active;
    active.title = QStringLiteral("Active");
    active.workingDirectory = directory.path();
    snack::domain::Conversation archived;
    archived.title = QStringLiteral("Archived");
    archived.workingDirectory = directory.path();
    archived.archived = true;
    repository.catalog = {active, archived};

    snack::agent::FakeAgentAdapter adapter(nullptr, 1);
    snack::session::SessionController controller(active, &adapter, &repository);
    {
        snack::ui::MainWindow window(&controller, &settings, false);
        auto* list = window.findChild<QListWidget*>(QStringLiteral("conversationList"));
        auto* action =
            window.findChild<QAction*>(QStringLiteral("showArchivedConversationsAction"));
        QVERIFY(list != nullptr);
        QVERIFY(action != nullptr);
        QVERIFY(action->isChecked());
        QCOMPARE(list->count(), 2);

        action->setChecked(false);
        QCOMPARE(list->count(), 1);
        QCOMPARE(list->item(0)->data(Qt::UserRole).toUuid(), active.id);
        QVERIFY(!settings.load().showArchivedConversations);
    }

    snack::ui::MainWindow restored(&controller, &settings, false);
    auto* restoredList = restored.findChild<QListWidget*>(QStringLiteral("conversationList"));
    auto* restoredAction =
        restored.findChild<QAction*>(QStringLiteral("showArchivedConversationsAction"));
    QVERIFY(restoredList != nullptr);
    QVERIFY(restoredAction != nullptr);
    QVERIFY(!restoredAction->isChecked());
    QCOMPARE(restoredList->count(), 1);
}

void TestMainWindow::cyclesActiveConversationsInRepositoryOrder() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    snack::app::AppSettings settings(directory.filePath(QStringLiteral("settings.ini")));
    UiMemoryEventRepository repository;
    snack::domain::Conversation alpha;
    alpha.title = QStringLiteral("Alpha");
    alpha.workingDirectory = directory.path();
    snack::domain::Conversation beta;
    beta.title = QStringLiteral("Beta");
    beta.workingDirectory = directory.path();
    snack::domain::Conversation gamma;
    gamma.title = QStringLiteral("Gamma");
    gamma.workingDirectory = directory.path();
    snack::domain::Conversation archived;
    archived.title = QStringLiteral("Between");
    archived.workingDirectory = directory.path();
    archived.archived = true;
    repository.catalog = {gamma, archived, beta, alpha};

    const auto makeRuntime = [](snack::domain::AgentKind kind) {
        snack::agent::AgentRuntime runtime;
        runtime.requestedKind = kind;
        runtime.selectedKind = kind;
        runtime.adapter = std::make_unique<snack::agent::FakeAgentAdapter>(nullptr, 1);
        return runtime;
    };
    snack::app::SessionManager sessions(&repository, makeRuntime);
    QString error;
    auto* betaController = sessions.addPrepared(beta, makeRuntime(beta.agentKind), &error);
    QVERIFY2(betaController != nullptr, qPrintable(error));
    snack::ui::MainWindow window(betaController, &settings, &sessions, false);
    auto* next = window.findChild<QAction*>(QStringLiteral("nextConversationAction"));
    auto* previous = window.findChild<QAction*>(QStringLiteral("previousConversationAction"));
    auto* title = window.findChild<QLabel*>(QStringLiteral("conversationTitle"));
    auto* composer = window.findChild<QPlainTextEdit*>(QStringLiteral("composer"));
    QVERIFY(next != nullptr);
    QVERIFY(previous != nullptr);
    QVERIFY(title != nullptr);
    QVERIFY(composer != nullptr);
    QCOMPARE(next->shortcut(), QKeySequence::NextChild);
    QCOMPARE(previous->shortcut(), QKeySequence::PreviousChild);

    composer->setPlainText(QStringLiteral("Beta draft"));
    next->trigger();
    QCOMPARE(title->text(), QStringLiteral("Gamma"));
    QCOMPARE(settings.composerDraft(beta.id), QStringLiteral("Beta draft"));
    next->trigger();
    QCOMPARE(title->text(), QStringLiteral("Alpha"));
    previous->trigger();
    QCOMPARE(title->text(), QStringLiteral("Gamma"));
    QVERIFY(title->text() != archived.title);
    QCOMPARE(sessions.size(), qsizetype{3});
}

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
    auto* stopButton = window.findChild<QPushButton*>(QStringLiteral("stopButton"));
    auto* timeline = window.findChild<QListWidget*>(QStringLiteral("timeline"));
    auto* modelCombo = window.findChild<QComboBox*>(QStringLiteral("modelCombo"));
    auto* effortCombo = window.findChild<QComboBox*>(QStringLiteral("effortCombo"));
    auto* sessionRow = window.findChild<QLabel*>(QStringLiteral("sessionRow"));
    auto* statusLabel = window.findChild<QLabel*>(QStringLiteral("statusLabel"));
    auto* connectionNoticeFrame =
        window.findChild<QFrame*>(QStringLiteral("connectionNoticeFrame"));
    auto* connectionNoticeTitle =
        window.findChild<QLabel*>(QStringLiteral("connectionNoticeTitle"));
    auto* connectionNoticeDetail =
        window.findChild<QLabel*>(QStringLiteral("connectionNoticeDetail"));
    auto* systemThemeAction = window.findChild<QAction*>(QStringLiteral("systemThemeAction"));
    auto* lightThemeAction = window.findChild<QAction*>(QStringLiteral("lightThemeAction"));
    auto* darkThemeAction = window.findChild<QAction*>(QStringLiteral("darkThemeAction"));
    QVERIFY(composer != nullptr);
    QVERIFY(sendButton != nullptr);
    QVERIFY(stopButton != nullptr);
    QVERIFY(timeline != nullptr);
    QVERIFY(modelCombo != nullptr);
    QVERIFY(effortCombo != nullptr);
    QVERIFY(sessionRow != nullptr);
    QVERIFY(statusLabel != nullptr);
    QVERIFY(connectionNoticeFrame != nullptr);
    QVERIFY(connectionNoticeTitle != nullptr);
    QVERIFY(connectionNoticeDetail != nullptr);
    QVERIFY(systemThemeAction != nullptr);
    QVERIFY(lightThemeAction != nullptr);
    QVERIFY(darkThemeAction != nullptr);
    QVERIFY(systemThemeAction->isChecked());
    QTRY_VERIFY(sendButton->isEnabled());
    QCOMPARE(modelCombo->count(), 2);
    QCOMPARE(modelCombo->currentData().toString(), QStringLiteral("mock-balanced"));
    QVERIFY(sessionRow->text().contains(QStringLiteral("Mock Agent")));
    QCOMPARE(statusLabel->toolTip(), QStringLiteral("mock-v1"));
    window.showStartupNotice(QStringLiteral("Fallback reason"));
    QCOMPARE(sessionRow->toolTip(), QStringLiteral("Fallback reason"));
    QVERIFY(!connectionNoticeFrame->isHidden());
    QCOMPARE(connectionNoticeTitle->text(), QStringLiteral("Agent fallback active"));
    QCOMPARE(connectionNoticeDetail->text(), QStringLiteral("Fallback reason"));

    sendButton->click();
    QCOMPARE(controller.status(), snack::domain::ConversationStatus::Idle);

    composer->setPlainText(QStringLiteral("Build the foundation"));
    sendButton->click();
    QCOMPARE(controller.status(), snack::domain::ConversationStatus::Running);
    modelCombo->setCurrentIndex(0);
    QCOMPARE(controller.nextTurnSettings().modelId, QStringLiteral("mock-fast"));
    QTRY_COMPARE_WITH_TIMEOUT(controller.status(), snack::domain::ConversationStatus::Idle, 1000);
    QVERIFY(!connectionNoticeFrame->isHidden());
    QCOMPARE(connectionNoticeDetail->text(), QStringLiteral("Fallback reason"));
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

    darkThemeAction->trigger();
    QVERIFY(darkThemeAction->isChecked());
    QVERIFY(!systemThemeAction->isChecked());
    systemThemeAction->trigger();
    QVERIFY(systemThemeAction->isChecked());
    lightThemeAction->trigger();
    QVERIFY(lightThemeAction->isChecked());
    darkThemeAction->trigger();
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

void TestMainWindow::updatesPlaceholderTitleAfterFirstSend() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    snack::app::AppSettings settings(directory.filePath(QStringLiteral("settings.ini")));
    UiMemoryEventRepository repository;
    snack::agent::FakeAgentAdapter adapter(nullptr, 1);
    snack::domain::Conversation conversation;
    conversation.title = QStringLiteral("Mock conversation");
    conversation.titleIsPlaceholder = true;
    conversation.workingDirectory = directory.path();
    snack::session::SessionController controller(conversation, &adapter, &repository);
    snack::ui::MainWindow window(&controller, &settings, false);

    auto* composer = window.findChild<QPlainTextEdit*>(QStringLiteral("composer"));
    auto* sendButton = window.findChild<QPushButton*>(QStringLiteral("sendButton"));
    auto* title = window.findChild<QLabel*>(QStringLiteral("conversationTitle"));
    auto* sessionRow = window.findChild<QLabel*>(QStringLiteral("sessionRow"));
    QVERIFY(composer != nullptr);
    QVERIFY(sendButton != nullptr);
    QVERIFY(title != nullptr);
    QVERIFY(sessionRow != nullptr);
    QTRY_VERIFY(sendButton->isEnabled());

    composer->setPlainText(QStringLiteral("Name this conversation from its first prompt"));
    sendButton->click();
    QCOMPARE(title->text(), QStringLiteral("Name this conversation from its first prompt"));
    QVERIFY(sessionRow->text().contains(title->text()));
}

void TestMainWindow::renamesCurrentConversation() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    snack::app::AppSettings settings(directory.filePath(QStringLiteral("settings.ini")));
    UiMemoryEventRepository repository;
    snack::agent::FakeAgentAdapter adapter(nullptr, 1);
    snack::domain::Conversation conversation;
    conversation.title = QStringLiteral("Before rename");
    conversation.workingDirectory = directory.path();
    snack::session::SessionController controller(conversation, &adapter, &repository);
    snack::ui::MainWindow window(&controller, &settings, false);

    auto* renameAction = window.findChild<QAction*>(QStringLiteral("renameConversationAction"));
    auto* title = window.findChild<QLabel*>(QStringLiteral("conversationTitle"));
    auto* sessionRow = window.findChild<QLabel*>(QStringLiteral("sessionRow"));
    QVERIFY(renameAction != nullptr);
    QVERIFY(title != nullptr);
    QVERIFY(sessionRow != nullptr);
    QCOMPARE(renameAction->shortcut(), QKeySequence(Qt::Key_F2));

    QTimer::singleShot(0, [] {
        auto* dialog = qobject_cast<QInputDialog*>(QApplication::activeModalWidget());
        QVERIFY(dialog != nullptr);
        QCOMPARE(dialog->textValue(), QStringLiteral("Before rename"));
        dialog->setTextValue(QStringLiteral("After rename"));
        dialog->accept();
    });
    renameAction->trigger();
    QCOMPARE(controller.conversation().title, QStringLiteral("After rename"));
    QCOMPARE(title->text(), QStringLiteral("After rename"));
    QVERIFY(sessionRow->text().contains(QStringLiteral("After rename")));
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
    snack::domain::AgentEvent inputEvent;
    inputEvent.conversationId = conversation.id;
    inputEvent.turnId = approvalEvent.turnId;
    inputEvent.sequence = 5;
    inputEvent.type = snack::domain::AgentEventType::UserInputRequested;
    inputEvent.payload = {
        {QStringLiteral("requestId"), QStringLiteral("restored-input")},
        {QStringLiteral("isBlocking"), true},
        {QStringLiteral("questions"),
         QJsonArray{QJsonObject{{QStringLiteral("id"), QStringLiteral("name")},
                                {QStringLiteral("header"), QStringLiteral("Name")},
                                {QStringLiteral("question"), QStringLiteral("Your name?")},
                                {QStringLiteral("options"), QJsonValue::Null}}}}};
    repository.events.append(inputEvent);
    snack::domain::AgentEvent usageEvent;
    usageEvent.conversationId = conversation.id;
    usageEvent.turnId = approvalEvent.turnId;
    usageEvent.sequence = 6;
    usageEvent.type = snack::domain::AgentEventType::UsageUpdated;
    usageEvent.payload = {
        {QStringLiteral("total"), QJsonObject{{QStringLiteral("inputTokens"), 10},
                                              {QStringLiteral("cachedInputTokens"), 2},
                                              {QStringLiteral("outputTokens"), 5},
                                              {QStringLiteral("reasoningOutputTokens"), 1},
                                              {QStringLiteral("totalTokens"), 16}}},
        {QStringLiteral("modelContextWindow"), QJsonValue::Null}};
    repository.events.append(usageEvent);

    snack::session::SessionController controller(conversation, &adapter, &repository);
    snack::ui::MainWindow window(&controller, &settings);
    auto* timeline = window.findChild<QListWidget*>(QStringLiteral("timeline"));
    QVERIFY(timeline != nullptr);
    QCOMPARE(timeline->count(), 4);
    QVERIFY(timeline->item(0)->text().contains(QStringLiteral("Persisted question")));
    QVERIFY(timeline->item(1)->text().contains(QStringLiteral("Persisted answer")));
    auto* restoredApproval = window.findChild<QPushButton*>(QStringLiteral("approvalAcceptButton"));
    QVERIFY(restoredApproval != nullptr);
    QVERIFY(!restoredApproval->isEnabled());
    auto* restoredInput = window.findChild<QPushButton*>(QStringLiteral("userInputSubmitButton"));
    auto* restoredUsage = window.findChild<QLabel*>(QStringLiteral("tokenUsageLabel"));
    QVERIFY(restoredInput != nullptr);
    QVERIFY(!restoredInput->isEnabled());
    QVERIFY(restoredUsage != nullptr);
    QVERIFY(restoredUsage->text().contains(QStringLiteral("16")));
}

void TestMainWindow::restoresToolReasoningAndPlanViews() {
    using snack::domain::AgentEvent;
    using snack::domain::AgentEventType;

    QTemporaryDir directory;
    snack::app::AppSettings settings(directory.filePath(QStringLiteral("settings.ini")));
    UiMemoryEventRepository repository;
    snack::agent::FakeAgentAdapter adapter(nullptr, 1);
    snack::domain::Conversation conversation;
    conversation.title = QStringLiteral("Restored agent activity");
    conversation.workingDirectory = directory.path();

    quint64 sequence = 0;
    const auto append = [&repository, &conversation, &sequence](AgentEventType type,
                                                                const QJsonObject& payload) {
        AgentEvent event;
        event.conversationId = conversation.id;
        event.turnId = QUuid::createUuid();
        event.sequence = ++sequence;
        event.type = type;
        event.payload = payload;
        repository.events.append(event);
    };
    append(AgentEventType::ToolStarted,
           {{QStringLiteral("itemId"), QStringLiteral("tool-1")},
            {QStringLiteral("kind"), QStringLiteral("commandExecution")},
            {QStringLiteral("command"), QStringLiteral("cmake --build build")},
            {QStringLiteral("cwd"), directory.path()},
            {QStringLiteral("status"), QStringLiteral("inProgress")}});
    append(AgentEventType::ToolOutputDelta,
           {{QStringLiteral("itemId"), QStringLiteral("tool-1")},
            {QStringLiteral("text"), QString(70000, QLatin1Char('a'))}});
    append(AgentEventType::ToolCompleted,
           {{QStringLiteral("itemId"), QStringLiteral("tool-1")},
            {QStringLiteral("kind"), QStringLiteral("commandExecution")},
            {QStringLiteral("command"), QStringLiteral("cmake --build build")},
            {QStringLiteral("cwd"), directory.path()},
            {QStringLiteral("status"), QStringLiteral("failed")},
            {QStringLiteral("aggregatedOutput"),
             QString(70000, QLatin1Char('b')) + QStringLiteral("final-tail")}});
    append(AgentEventType::ReasoningStarted,
           {{QStringLiteral("itemId"), QStringLiteral("reasoning-1")}});
    append(AgentEventType::ReasoningSummaryDelta,
           {{QStringLiteral("itemId"), QStringLiteral("reasoning-1")},
            {QStringLiteral("text"), QStringLiteral("Draft summary")}});
    append(AgentEventType::ReasoningCompleted,
           {{QStringLiteral("itemId"), QStringLiteral("reasoning-1")},
            {QStringLiteral("summary"), QJsonArray{QStringLiteral("Final public summary")}}});
    append(AgentEventType::PlanUpdated,
           {{QStringLiteral("itemId"), QStringLiteral("plan-1")},
            {QStringLiteral("textDelta"), QStringLiteral("Draft plan")}});
    append(AgentEventType::PlanUpdated, {{QStringLiteral("itemId"), QStringLiteral("plan-1")},
                                         {QStringLiteral("text"), QStringLiteral("Final plan")},
                                         {QStringLiteral("final"), true}});
    append(AgentEventType::PlanUpdated,
           {{QStringLiteral("explanation"), QStringLiteral("Fix and verify")},
            {QStringLiteral("plan"),
             QJsonArray{QJsonObject{{QStringLiteral("step"), QStringLiteral("Inspect")},
                                    {QStringLiteral("status"), QStringLiteral("completed")}},
                        QJsonObject{{QStringLiteral("step"), QStringLiteral("Fix")},
                                    {QStringLiteral("status"), QStringLiteral("inProgress")}},
                        QJsonObject{{QStringLiteral("step"), QStringLiteral("Test")},
                                    {QStringLiteral("status"), QStringLiteral("pending")}}}}});

    snack::session::SessionController controller(conversation, &adapter, &repository);
    snack::ui::MainWindow window(&controller, &settings, false);
    auto* toolCard = window.findChild<QFrame*>(QStringLiteral("toolCard"));
    auto* toolOutput = window.findChild<QPlainTextEdit*>(QStringLiteral("toolOutput"));
    auto* toolStatus = window.findChild<QLabel*>(QStringLiteral("toolStatus"));
    auto* reasoningSummary = window.findChild<QLabel*>(QStringLiteral("reasoningSummary"));
    auto* reasoningStatus = window.findChild<QLabel*>(QStringLiteral("reasoningStatus"));
    auto* taskDock = window.findChild<QDockWidget*>(QStringLiteral("taskDock"));
    auto* planList = window.findChild<QListWidget*>(QStringLiteral("planList"));
    auto* planItemText = window.findChild<QLabel*>(QStringLiteral("planItemText"));
    QVERIFY(toolCard != nullptr);
    QVERIFY(toolOutput != nullptr);
    QVERIFY(toolStatus != nullptr);
    QVERIFY(reasoningSummary != nullptr);
    QVERIFY(reasoningStatus != nullptr);
    QVERIFY(taskDock != nullptr);
    QVERIFY(planList != nullptr);
    QVERIFY(planItemText != nullptr);
    QVERIFY(toolOutput->toPlainText().size() < 66000);
    QVERIFY(toolOutput->toPlainText().startsWith(QStringLiteral("[earlier output hidden]")));
    QVERIFY(toolOutput->toPlainText().endsWith(QStringLiteral("final-tail")));
    QCOMPARE(toolStatus->text(), QStringLiteral("failed"));
    QCOMPARE(reasoningSummary->text(), QStringLiteral("Final public summary"));
    QCOMPARE(reasoningStatus->text(), QStringLiteral("Completed"));
    QCOMPARE(planItemText->text(), QStringLiteral("Final plan"));
    QCOMPARE(planList->count(), 3);
    QCOMPARE(planList->item(0)->data(Qt::UserRole).toString(), QStringLiteral("completed"));
    QCOMPARE(planList->item(1)->data(Qt::UserRole).toString(), QStringLiteral("inProgress"));
    QVERIFY(!taskDock->isHidden());
    window.close();
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
    auto* stopButton = window.findChild<QPushButton*>(QStringLiteral("stopButton"));
    QTRY_COMPARE(controller.status(), snack::domain::ConversationStatus::Idle);
    composer->setPlainText(QStringLiteral("Stop this turn"));
    sendButton->click();
    QCOMPARE(controller.status(), snack::domain::ConversationStatus::Running);
    QCOMPARE(sendButton->text(), QStringLiteral("Steer"));
    QVERIFY(sendButton->isEnabled());
    QVERIFY(!stopButton->isHidden());
    QVERIFY(stopButton->isEnabled());

    composer->setPlainText(QStringLiteral("Focus the failing test"));
    sendButton->click();
    QCOMPARE(adapter.lastSteerRequest().message, QStringLiteral("Focus the failing test"));
    QVERIFY(composer->toPlainText().isEmpty());
    QCOMPARE(repository.events.constLast().type, snack::domain::AgentEventType::UserMessage);
    QVERIFY(repository.events.constLast().payload.value(QStringLiteral("steered")).toBool());

    stopButton->click();
    QCOMPARE(controller.status(), snack::domain::ConversationStatus::Idle);
    QCOMPARE(repository.events.constLast().type, snack::domain::AgentEventType::TurnInterrupted);
    QCOMPARE(sendButton->text(), QStringLiteral("Send"));
    window.close();
}

void TestMainWindow::editsAndControlsQueuedMessages() {
    QTemporaryDir directory;
    snack::app::AppSettings settings(directory.filePath(QStringLiteral("settings.ini")));
    UiMemoryEventRepository repository;
    snack::agent::FakeAgentAdapter adapter(nullptr, 1000);
    snack::domain::Conversation conversation;
    conversation.title = QStringLiteral("Queue UI");
    conversation.workingDirectory = directory.path();
    snack::session::SessionController controller(conversation, &adapter, &repository);
    snack::ui::MainWindow window(&controller, &settings, false);

    auto* composer = window.findChild<QPlainTextEdit*>(QStringLiteral("composer"));
    auto* sendButton = window.findChild<QPushButton*>(QStringLiteral("sendButton"));
    auto* sendMode = window.findChild<QComboBox*>(QStringLiteral("sendModeCombo"));
    auto* queueFrame = window.findChild<QFrame*>(QStringLiteral("queueFrame"));
    auto* queueList = window.findChild<QListWidget*>(QStringLiteral("queueList"));
    auto* queueUp = window.findChild<QPushButton*>(QStringLiteral("queueUpButton"));
    auto* queueSendNow = window.findChild<QPushButton*>(QStringLiteral("queueSendNowButton"));
    auto* queueRemove = window.findChild<QPushButton*>(QStringLiteral("queueRemoveButton"));
    QVERIFY(composer != nullptr);
    QVERIFY(sendButton != nullptr);
    QVERIFY(sendMode != nullptr);
    QVERIFY(queueFrame != nullptr);
    QVERIFY(queueList != nullptr);
    QVERIFY(queueUp != nullptr);
    QVERIFY(queueSendNow != nullptr);
    QVERIFY(queueRemove != nullptr);

    QTRY_COMPARE(controller.status(), snack::domain::ConversationStatus::Idle);
    composer->setPlainText(QStringLiteral("Long first turn"));
    sendButton->click();
    QCOMPARE(sendMode->currentData().toString(), QStringLiteral("steer"));
    sendMode->setCurrentIndex(sendMode->findData(QStringLiteral("queue")));
    QCOMPARE(sendButton->text(), QStringLiteral("Queue"));

    composer->setPlainText(QStringLiteral("Queued one"));
    sendButton->click();
    composer->setPlainText(QStringLiteral("Queued two"));
    sendButton->click();
    QCOMPARE(queueList->count(), 2);
    QVERIFY(!queueFrame->isHidden());
    QCOMPARE(controller.queuedMessages().size(), 2);

    queueList->item(1)->setText(QStringLiteral("Edited two"));
    QCOMPARE(controller.queuedMessages().at(1).content, QStringLiteral("Edited two"));
    queueList->setCurrentRow(1);
    queueUp->click();
    QCOMPARE(controller.queuedMessages().constFirst().content, QStringLiteral("Edited two"));

    queueRemove->click();
    QCOMPARE(queueList->count(), 1);
    QCOMPARE(controller.queuedMessages().constFirst().content, QStringLiteral("Queued one"));
    queueSendNow->click();
    QVERIFY(controller.queuedMessages().isEmpty());
    QVERIFY(queueFrame->isHidden());
    QCOMPARE(adapter.lastSteerRequest().message, QStringLiteral("Queued one"));
    window.findChild<QPushButton*>(QStringLiteral("stopButton"))->click();
    window.close();
}

void TestMainWindow::supportsComposerShortcutsGrowthAndDrafts() {
    QTemporaryDir directory;
    const QString settingsPath = directory.filePath(QStringLiteral("settings.ini"));
    snack::app::AppSettings settings(settingsPath);
    UiMemoryEventRepository repository;
    snack::agent::FakeAgentAdapter adapter(nullptr, 1000);
    snack::domain::Conversation conversation;
    conversation.title = QStringLiteral("Composer UX");
    conversation.workingDirectory = directory.path();
    settings.saveComposerDraft(conversation.id, QStringLiteral("restored draft"));

    snack::session::SessionController controller(conversation, &adapter, &repository);
    snack::ui::MainWindow window(&controller, &settings, false);
    auto* composer = window.findChild<QPlainTextEdit*>(QStringLiteral("composer"));
    auto* sendButton = window.findChild<QPushButton*>(QStringLiteral("sendButton"));
    auto* focusAction = window.findChild<QAction*>(QStringLiteral("focusComposerAction"));
    QVERIFY(composer != nullptr);
    QVERIFY(sendButton != nullptr);
    QVERIFY(focusAction != nullptr);
    QCOMPARE(composer->toPlainText(), QStringLiteral("restored draft"));
    QCOMPARE(focusAction->shortcut(), QKeySequence(Qt::CTRL | Qt::Key_L));

    window.show();
    QTRY_COMPARE(controller.status(), snack::domain::ConversationStatus::Idle);
    composer->setPlainText(QStringLiteral("debounced draft"));
    QTRY_COMPARE_WITH_TIMEOUT(settings.composerDraft(conversation.id),
                              QStringLiteral("debounced draft"), 1000);
    composer->clear();
    composer->setFocus();
    QTest::keyClicks(composer, QStringLiteral("x"));
    QCOMPARE(composer->toPlainText(), QStringLiteral("x"));
    composer->clear();
    QCoreApplication::processEvents();
    const int singleLineHeight = composer->height();
    composer->setPlainText(QStringList(12, QStringLiteral("line")).join(QLatin1Char('\n')));
    QCoreApplication::processEvents();
    QVERIFY(composer->height() > singleLineHeight);
    QVERIFY(composer->height() <= composer->fontMetrics().lineSpacing() * 8 + 40);

    composer->clear();
    composer->setFocus();
    QTest::keyClick(composer, Qt::Key_Return, Qt::ShiftModifier);
    QCOMPARE(composer->toPlainText(), QStringLiteral("\n"));
    QCOMPARE(controller.status(), snack::domain::ConversationStatus::Idle);

    composer->setPlainText(QStringLiteral("Keyboard send"));
    QTest::keyClick(composer, Qt::Key_Return);
    QCOMPARE(controller.status(), snack::domain::ConversationStatus::Running);
    QVERIFY(composer->toPlainText().isEmpty());
    QVERIFY(settings.composerDraft(conversation.id).isEmpty());

    composer->setPlainText(QStringLiteral("Keyboard queue"));
    QTest::keyClick(composer, Qt::Key_Return, Qt::ControlModifier);
    QCOMPARE(controller.queuedMessages().size(), 1);
    QCOMPARE(controller.queuedMessages().constFirst().content, QStringLiteral("Keyboard queue"));
    QVERIFY(composer->toPlainText().isEmpty());

    sendButton->setFocus();
    focusAction->trigger();
    QVERIFY(composer->hasFocus());
    QTest::keyClick(composer, Qt::Key_Escape);
    QTRY_COMPARE(controller.status(), snack::domain::ConversationStatus::Idle);

    composer->setPlainText(QStringLiteral("saved on close"));
    window.close();
    QCOMPARE(settings.composerDraft(conversation.id), QStringLiteral("saved on close"));
}

void TestMainWindow::insertsAndManagesPromptTemplates() {
    QTemporaryDir directory;
    snack::app::AppSettings settings(directory.filePath(QStringLiteral("settings.ini")));
    UiMemoryEventRepository repository;
    snack::domain::PromptTemplate quick;
    quick.name = QStringLiteral("Explain");
    quick.content = QStringLiteral("Explain this code");
    quick.position = 0;
    repository.templates.insert(quick.id, quick);
    snack::domain::PromptTemplate parameterized;
    parameterized.name = QStringLiteral("Review");
    parameterized.content = QStringLiteral("Review {{path}} for {{focus}}");
    parameterized.position = 1;
    repository.templates.insert(parameterized.id, parameterized);

    snack::agent::FakeAgentAdapter adapter(nullptr, 1000);
    snack::domain::Conversation conversation;
    conversation.title = QStringLiteral("Template UI");
    conversation.workingDirectory = directory.path();
    snack::session::SessionController controller(conversation, &adapter, &repository);
    snack::ui::MainWindow window(&controller, &settings, false);
    auto* composer = window.findChild<QPlainTextEdit*>(QStringLiteral("composer"));
    auto* templateButton = window.findChild<QPushButton*>(QStringLiteral("templateButton"));
    auto* templateMenu = window.findChild<QMenu*>(QStringLiteral("templateMenu"));
    QVERIFY(composer != nullptr);
    QVERIFY(templateButton != nullptr);
    QVERIFY(templateMenu != nullptr);

    const QString quickActionName =
        QStringLiteral("promptTemplateAction_%1").arg(quick.id.toString(QUuid::WithoutBraces));
    auto* quickAction = window.findChild<QAction*>(quickActionName);
    QVERIFY(quickAction != nullptr);
    quickAction->trigger();
    QCOMPARE(composer->toPlainText(), QStringLiteral("Explain this code"));

    composer->clear();
    composer->setFocus();
    QTest::keyClick(composer, Qt::Key_Slash);
    QTRY_VERIFY(templateMenu->isVisible());
    QVERIFY(composer->toPlainText().isEmpty());
    templateMenu->hide();

    const QString parameterActionName = QStringLiteral("promptTemplateAction_%1")
                                            .arg(parameterized.id.toString(QUuid::WithoutBraces));
    auto* parameterAction = window.findChild<QAction*>(parameterActionName);
    QVERIFY(parameterAction != nullptr);
    QTimer::singleShot(0, [] {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        QVERIFY(dialog != nullptr);
        auto* path = dialog->findChild<QLineEdit*>(QStringLiteral("promptTemplateParameter_path"));
        auto* focus =
            dialog->findChild<QLineEdit*>(QStringLiteral("promptTemplateParameter_focus"));
        QVERIFY(path != nullptr);
        QVERIFY(focus != nullptr);
        path->setText(QStringLiteral("src/main.cpp"));
        focus->setText(QStringLiteral("correctness"));
        dialog->accept();
    });
    parameterAction->trigger();
    QCOMPARE(composer->toPlainText(), QStringLiteral("Review src/main.cpp for correctness"));

    composer->setPlainText(QStringLiteral("Summarize {{topic}}"));
    templateButton->click();
    QTRY_VERIFY(templateMenu->isVisible());
    templateMenu->hide();
    auto* saveAction = window.findChild<QAction*>(QStringLiteral("savePromptTemplateAction"));
    QVERIFY(saveAction != nullptr);
    QVERIFY(saveAction->isEnabled());
    QTimer::singleShot(0, [] {
        auto* dialog = qobject_cast<QInputDialog*>(QApplication::activeModalWidget());
        QVERIFY(dialog != nullptr);
        dialog->setTextValue(QStringLiteral("Summarize"));
        dialog->accept();
    });
    saveAction->trigger();
    QCOMPARE(repository.templates.size(), 3);

    const QString removeActionName = QStringLiteral("removePromptTemplateAction_%1")
                                         .arg(quick.id.toString(QUuid::WithoutBraces));
    auto* removeAction = window.findChild<QAction*>(removeActionName);
    QVERIFY(removeAction != nullptr);
    removeAction->trigger();
    QVERIFY(!repository.templates.contains(quick.id));
    window.close();
}

void TestMainWindow::reconnectsDisconnectedSession() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    snack::app::AppSettings settings(directory.filePath(QStringLiteral("settings.ini")));
    UiMemoryEventRepository repository;
    snack::agent::FakeAgentAdapter adapter;
    snack::domain::Conversation conversation;
    conversation.title = QStringLiteral("Reconnect test");
    conversation.workingDirectory = directory.path();
    conversation.nativeThreadId = QStringLiteral("native-thread-1");
    conversation.nativeSessionId = QStringLiteral("native-session-1");
    snack::session::SessionController controller(conversation, &adapter, &repository);
    snack::ui::MainWindow window(&controller, &settings, false);

    auto* reconnectButton = window.findChild<QPushButton*>(QStringLiteral("reconnectButton"));
    auto* sendButton = window.findChild<QPushButton*>(QStringLiteral("sendButton"));
    auto* reconnectNoticeFrame = window.findChild<QFrame*>(QStringLiteral("connectionNoticeFrame"));
    auto* reconnectNoticeTitle = window.findChild<QLabel*>(QStringLiteral("connectionNoticeTitle"));
    auto* reconnectNoticeDetail =
        window.findChild<QLabel*>(QStringLiteral("connectionNoticeDetail"));
    QVERIFY(reconnectButton != nullptr);
    QVERIFY(sendButton != nullptr);
    QVERIFY(reconnectNoticeFrame != nullptr);
    QVERIFY(reconnectNoticeTitle != nullptr);
    QVERIFY(reconnectNoticeDetail != nullptr);
    QTRY_COMPARE(controller.status(), snack::domain::ConversationStatus::Idle);
    QVERIFY(reconnectButton->isHidden());
    QVERIFY(reconnectNoticeFrame->isHidden());

    adapter.closeAgent();
    QCOMPARE(controller.status(), snack::domain::ConversationStatus::Disconnected);
    QVERIFY(!reconnectNoticeFrame->isHidden());
    QCOMPARE(reconnectNoticeTitle->text(), QStringLiteral("Agent connection unavailable"));
    QCOMPARE(reconnectNoticeDetail->text(), QStringLiteral("closed"));
    QVERIFY(!reconnectButton->isHidden());
    QVERIFY(!sendButton->isEnabled());

    reconnectButton->click();
    QCOMPARE(reconnectNoticeTitle->text(), QStringLiteral("Reconnecting agent"));
    QCOMPARE(reconnectNoticeDetail->text(), QStringLiteral("closed"));
    QTRY_COMPARE(controller.status(), snack::domain::ConversationStatus::Idle);
    QVERIFY(reconnectButton->isHidden());
    QVERIFY(reconnectNoticeFrame->isHidden());
    QVERIFY(sendButton->isEnabled());
    QCOMPARE(adapter.lastConnectionRequest().nativeThreadId, QStringLiteral("native-thread-1"));
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
    auto* stopButton = window.findChild<QPushButton*>(QStringLiteral("stopButton"));
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
    QCOMPARE(sendButton->text(), QStringLiteral("Queue"));
    QVERIFY(sendButton->isEnabled());
    QVERIFY(!stopButton->isHidden());
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

    stopButton->click();
    QCOMPARE(controller.status(), ConversationStatus::Idle);
    window.close();
}

void TestMainWindow::handlesUserInputCardAndUsage() {
    using snack::domain::AgentEvent;
    using snack::domain::AgentEventType;
    using snack::domain::ConversationStatus;

    QTemporaryDir directory;
    snack::app::AppSettings settings(directory.filePath(QStringLiteral("settings.ini")));
    UiMemoryEventRepository repository;
    snack::agent::FakeAgentAdapter adapter(nullptr, 1000);
    snack::domain::Conversation conversation;
    conversation.title = QStringLiteral("Input UI");
    conversation.workingDirectory = directory.path();
    snack::session::SessionController controller(conversation, &adapter, &repository);
    snack::ui::MainWindow window(&controller, &settings, false);

    auto* composer = window.findChild<QPlainTextEdit*>(QStringLiteral("composer"));
    auto* sendButton = window.findChild<QPushButton*>(QStringLiteral("sendButton"));
    auto* stopButton = window.findChild<QPushButton*>(QStringLiteral("stopButton"));
    QTRY_COMPARE(controller.status(), ConversationStatus::Idle);
    composer->setPlainText(QStringLiteral("Ask a question"));
    sendButton->click();
    const QUuid turnId = repository.events.constFirst().turnId;

    AgentEvent usage;
    usage.turnId = turnId;
    usage.type = AgentEventType::UsageUpdated;
    usage.payload = {
        {QStringLiteral("last"), QJsonObject{{QStringLiteral("totalTokens"), 160}}},
        {QStringLiteral("total"), QJsonObject{{QStringLiteral("inputTokens"), 1000},
                                              {QStringLiteral("cachedInputTokens"), 200},
                                              {QStringLiteral("outputTokens"), 300},
                                              {QStringLiteral("reasoningOutputTokens"), 100},
                                              {QStringLiteral("totalTokens"), 1400}}},
        {QStringLiteral("modelContextWindow"), 200000}};
    adapter.eventReceived(usage);
    auto* usageLabel = window.findChild<QLabel*>(QStringLiteral("tokenUsageLabel"));
    QVERIFY(usageLabel != nullptr);
    QVERIFY(usageLabel->text().contains(QStringLiteral("1,400")) ||
            usageLabel->text().contains(QStringLiteral("1400")));
    QVERIFY(usageLabel->text().contains(QStringLiteral("0.7%")));
    QVERIFY(usageLabel->toolTip().contains(QStringLiteral("1,000")) ||
            usageLabel->toolTip().contains(QStringLiteral("1000")));

    AgentEvent request;
    request.turnId = turnId;
    request.type = AgentEventType::UserInputRequested;
    request.payload = {
        {QStringLiteral("requestId"), QStringLiteral("ui-input")},
        {QStringLiteral("isBlocking"), true},
        {QStringLiteral("questions"),
         QJsonArray{
             QJsonObject{{QStringLiteral("id"), QStringLiteral("scope")},
                         {QStringLiteral("header"), QStringLiteral("Scope")},
                         {QStringLiteral("question"), QStringLiteral("Which scope?")},
                         {QStringLiteral("isOther"), true},
                         {QStringLiteral("options"),
                          QJsonArray{QJsonObject{
                              {QStringLiteral("label"), QStringLiteral("Core")},
                              {QStringLiteral("description"), QStringLiteral("Core only")}}}}},
             QJsonObject{{QStringLiteral("id"), QStringLiteral("token")},
                         {QStringLiteral("header"), QStringLiteral("Token")},
                         {QStringLiteral("question"), QStringLiteral("Provide token")},
                         {QStringLiteral("isSecret"), true},
                         {QStringLiteral("options"), QJsonValue::Null}}}}};
    adapter.eventReceived(request);
    QCOMPARE(controller.status(), ConversationStatus::WaitingInput);

    auto* card = window.findChild<QFrame*>(QStringLiteral("userInputCard"));
    auto* options = window.findChild<QComboBox*>(QStringLiteral("userInputOption_scope"));
    auto* other = window.findChild<QLineEdit*>(QStringLiteral("userInputOther_scope"));
    auto* secret = window.findChild<QLineEdit*>(QStringLiteral("userInputText_token"));
    auto* submit = window.findChild<QPushButton*>(QStringLiteral("userInputSubmitButton"));
    auto* inputStatus = window.findChild<QLabel*>(QStringLiteral("userInputStatus"));
    QVERIFY(card != nullptr);
    QVERIFY(options != nullptr);
    QVERIFY(other != nullptr);
    QVERIFY(secret != nullptr);
    QVERIFY(submit != nullptr);
    QVERIFY(inputStatus != nullptr);
    QCOMPARE(secret->echoMode(), QLineEdit::Password);
    options->setCurrentIndex(options->count() - 1);
    QVERIFY(!other->isHidden());
    other->setText(QStringLiteral("Custom"));
    secret->setText(QStringLiteral("ui-secret"));
    submit->click();

    QCOMPARE(adapter.lastUserInputRequestId(), QStringLiteral("ui-input"));
    QCOMPARE(adapter.lastUserInputAnswers()
                 .value(QStringLiteral("scope"))
                 .toObject()
                 .value(QStringLiteral("answers"))
                 .toArray()
                 .at(0)
                 .toString(),
             QStringLiteral("Custom"));
    QCOMPARE(controller.status(), ConversationStatus::Running);
    QVERIFY(!submit->isEnabled());
    QVERIFY(secret->text().isEmpty());
    QVERIFY(inputStatus->text().contains(QStringLiteral("sent"), Qt::CaseInsensitive));
    stopButton->click();
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
