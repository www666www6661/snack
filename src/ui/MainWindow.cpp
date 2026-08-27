#include "ui/MainWindow.h"

#include "domain/PromptTemplateEngine.h"
#include "ui/ComposerTextEdit.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScreen>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QStyle>
#include <QStyleHints>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

namespace snack::ui {
namespace {

constexpr qsizetype maximumToolOutput = 64 * 1024;
constexpr qsizetype maximumSummaryText = 32 * 1024;

QString boundedText(const QString& text, qsizetype maximumSize) {
    if (text.size() <= maximumSize) {
        return text;
    }
    return QStringLiteral("[earlier output hidden]\n") + text.sliced(text.size() - maximumSize);
}

QString compactJson(const QJsonValue& value) {
    if (value.isUndefined() || value.isNull()) {
        return {};
    }
    if (value.isObject()) {
        return QString::fromUtf8(QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact));
    }
    if (value.isArray()) {
        return QString::fromUtf8(QJsonDocument(value.toArray()).toJson(QJsonDocument::Compact));
    }
    return value.toVariant().toString();
}

bool createsUnreadAttention(domain::AgentEventType type) {
    switch (type) {
    case domain::AgentEventType::AgentMessageStart:
    case domain::AgentEventType::ToolStarted:
    case domain::AgentEventType::ApprovalRequested:
    case domain::AgentEventType::UserInputRequested:
    case domain::AgentEventType::WarningRaised:
    case domain::AgentEventType::ErrorRaised:
    case domain::AgentEventType::TurnFailed:
        return true;
    default:
        return false;
    }
}

QStringList conversationQueryTerms(const QString& query) {
    QStringList terms;
    QString current;
    bool quoted = false;
    for (const QChar character : query.trimmed()) {
        if (character == QLatin1Char('"')) {
            quoted = !quoted;
            continue;
        }
        if (character.isSpace() && !quoted) {
            if (!current.isEmpty()) {
                terms.append(current);
                current.clear();
            }
            continue;
        }
        current.append(character);
    }
    if (!current.isEmpty()) {
        terms.append(current);
    }
    return terms;
}

bool conversationMatchesQuery(const domain::Conversation& conversation, const QString& agentName,
                              const QString& query) {
    const QStringList terms = conversationQueryTerms(query);
    for (const QString& term : terms) {
        if (term.left(4).compare(QStringLiteral("tag:"), Qt::CaseInsensitive) == 0) {
            const QString requestedTag = term.sliced(4);
            const bool hasTag =
                !requestedTag.isEmpty() &&
                std::any_of(conversation.tags.cbegin(), conversation.tags.cend(),
                            [&requestedTag](const QString& tag) {
                                return tag.compare(requestedTag, Qt::CaseInsensitive) == 0;
                            });
            if (!hasTag) {
                return false;
            }
            continue;
        }
        if (term.left(6).compare(QStringLiteral("agent:"), Qt::CaseInsensitive) == 0) {
            QString conversationAgent;
            switch (conversation.agentKind) {
            case domain::AgentKind::Codex:
                conversationAgent = QStringLiteral("codex");
                break;
            case domain::AgentKind::Claude:
                conversationAgent = QStringLiteral("claude");
                break;
            case domain::AgentKind::Mock:
                conversationAgent = QStringLiteral("mock");
                break;
            }
            if (conversationAgent.compare(term.sliced(6), Qt::CaseInsensitive) != 0) {
                return false;
            }
            continue;
        }
        if (term.left(6).compare(QStringLiteral("model:"), Qt::CaseInsensitive) == 0) {
            const QString requestedModel = term.sliced(6);
            if (requestedModel.isEmpty() ||
                conversation.modelId.compare(requestedModel, Qt::CaseInsensitive) != 0) {
                return false;
            }
            continue;
        }
        if (term.left(7).compare(QStringLiteral("status:"), Qt::CaseInsensitive) == 0) {
            if (domain::enumName(conversation.status)
                    .compare(term.sliced(7), Qt::CaseInsensitive) != 0) {
                return false;
            }
            continue;
        }
        if (term.left(5).compare(QStringLiteral("path:"), Qt::CaseInsensitive) == 0) {
            const QString requestedPath = term.sliced(5);
            if (requestedPath.isEmpty() ||
                !conversation.workingDirectory.contains(requestedPath, Qt::CaseInsensitive)) {
                return false;
            }
            continue;
        }
        if (!conversation.title.contains(term, Qt::CaseInsensitive) &&
            !conversation.workingDirectory.contains(term, Qt::CaseInsensitive) &&
            !agentName.contains(term, Qt::CaseInsensitive) &&
            !conversation.tags.join(QLatin1Char(' ')).contains(term, Qt::CaseInsensitive)) {
            return false;
        }
    }
    return true;
}

} // namespace

MainWindow::MainWindow(session::SessionController* controller, app::AppSettings* settings,
                       QWidget* parent)
    : MainWindow(controller, settings, nullptr, QSystemTrayIcon::isSystemTrayAvailable(), parent) {}

MainWindow::MainWindow(session::SessionController* controller, app::AppSettings* settings,
                       bool closeToTrayEnabled, QWidget* parent)
    : MainWindow(controller, settings, nullptr, closeToTrayEnabled, parent) {}

MainWindow::MainWindow(session::SessionController* controller, app::AppSettings* settings,
                       app::SessionManager* sessions, bool closeToTrayEnabled, QWidget* parent)
    : QMainWindow(parent), controller_(controller), sessions_(sessions), settings_(settings),
      settingsSnapshot_(settings_->load()), closeToTrayEnabled_(closeToTrayEnabled) {
    Q_ASSERT(controller_ != nullptr);
    Q_ASSERT(settings_ != nullptr);
    buildUi();
    composer_->setPlainText(settings_->composerDraft(controller_->conversation().id));
    buildMenus();
    restoreWindowState();
    buildTray();

    connectControllerSignals();
    refreshConversationList();
    connect(qApp->styleHints(), &QStyleHints::colorSchemeChanged, this,
            [this](Qt::ColorScheme) { refreshSystemTheme(); });
    connect(qApp, &QCoreApplication::aboutToQuit, this, &MainWindow::shutdown);

    restoreTimeline();
    if (settingsSnapshot_.themeMode == app::ThemeMode::System) {
        refreshSystemTheme();
    } else {
        applyTheme(settingsSnapshot_.themeMode == app::ThemeMode::Dark ? ThemeDefinition::dark()
                                                                       : ThemeDefinition::light());
    }
    applyInterfaceScale(settingsSnapshot_.interfaceScale);
    rebuildCapabilityControls(controller_->nextTurnSettings());
    updateQueuedMessages(controller_->queuedMessages());
    controller_->open();
    QTimer::singleShot(0, this, &MainWindow::ensureWindowVisible);
}

void MainWindow::connectControllerSignals() {
    connect(controller_, &session::SessionController::eventRecorded, this,
            &MainWindow::appendEvent);
    connect(controller_, &session::SessionController::statusChanged, this,
            &MainWindow::updateStatus);
    connect(controller_, &session::SessionController::persistenceError, this,
            [this](const QString& error) {
                statusBar()->showMessage(tr("Storage error: %1").arg(error), 8000);
            });
    connect(controller_, &session::SessionController::nextTurnSettingsChanged, this,
            &MainWindow::rebuildCapabilityControls);
    connect(controller_, &session::SessionController::capabilitiesChanged, this,
            [this](const agent::CapabilitySet&) {
                rebuildCapabilityControls(controller_->nextTurnSettings());
                updateStatus(controller_->status());
            });
    connect(controller_, &session::SessionController::connectionDetailChanged, this,
            &MainWindow::updateConnectionDetail);
    connect(controller_, &session::SessionController::conversationTitleChanged, this,
            &MainWindow::updateConversationTitle);
    connect(controller_, &session::SessionController::queuedMessagesChanged, this,
            &MainWindow::updateQueuedMessages);
    connect(controller_, &session::SessionController::promptTemplatesChanged, this,
            [this] { rebuildPromptTemplateMenu(); });
}

void MainWindow::showStartupNotice(const QString& notice) {
    if (!notice.trimmed().isEmpty()) {
        startupNotice_ = notice.trimmed();
        sessionRow_->setToolTip(startupNotice_);
        statusBar()->showMessage(startupNotice_, 10000);
        refreshConnectionNotice();
    }
}

void MainWindow::refreshConversationList() {
    QString error;
    conversationCatalog_ = controller_->conversationCatalog(&error);
    const auto current = std::find_if(
        conversationCatalog_.cbegin(), conversationCatalog_.cend(),
        [this](const auto& value) { return value.id == controller_->conversation().id; });
    if (current == conversationCatalog_.cend()) {
        conversationCatalog_.prepend(controller_->conversation());
    }

    const QSignalBlocker blocker(conversationList_);
    conversationList_->clear();
    int currentRow = -1;
    const QString query = conversationSearch_->text().trimmed();
    for (const auto& conversation : conversationCatalog_) {
        if (conversation.archived && !settingsSnapshot_.showArchivedConversations) {
            continue;
        }
        QString agentName;
        switch (conversation.agentKind) {
        case domain::AgentKind::Codex:
            agentName = QStringLiteral("Codex");
            break;
        case domain::AgentKind::Claude:
            agentName = QStringLiteral("Claude");
            break;
        case domain::AgentKind::Mock:
            agentName = tr("Mock Agent");
            break;
        }
        if (!conversationMatchesQuery(conversation, agentName, query)) {
            continue;
        }
        const QString title =
            conversation.pinned ? tr("Pinned · %1").arg(conversation.title) : conversation.title;
        const QString displayTitle =
            unreadConversationIds_.contains(conversation.id) ? tr("Unread · %1").arg(title) : title;
        const QString taggedTitle =
            conversation.tags.isEmpty()
                ? displayTitle
                : QStringLiteral("%1  [%2]").arg(displayTitle, conversation.tags.join(" · "));
        const QString status = conversationStatusText(conversation.status);
        auto* item = new QListWidgetItem(
            conversation.archived
                ? tr("Archived · %1 · %2\n%3")
                      .arg(taggedTitle, agentName, conversation.workingDirectory)
                : tr("%1 · %2 · %3\n%4")
                      .arg(taggedTitle, agentName, status, conversation.workingDirectory),
            conversationList_);
        item->setData(Qt::UserRole, conversation.id);
        item->setData(Qt::UserRole + 1, conversation.archived);
        item->setToolTip(conversation.workingDirectory);
        if (conversation.id == controller_->conversation().id) {
            currentRow = conversationList_->count() - 1;
        }
    }
    conversationList_->setCurrentRow(currentRow);
    if (sessions_ != nullptr) {
        for (const QUuid& conversationId : sessions_->conversationIds()) {
            auto* openController = sessions_->controller(conversationId);
            connect(openController, &session::SessionController::statusChanged, this,
                    &MainWindow::refreshConversationList, Qt::UniqueConnection);
            if (observedControllers_.value(conversationId) != openController) {
                observedControllers_.insert(conversationId, openController);
                const QPointer<MainWindow> window(this);
                connect(openController, &session::SessionController::eventRecorded, openController,
                        [window, conversationId](const domain::AgentEvent& event) {
                            if (window.isNull() ||
                                conversationId == window->controller_->conversation().id ||
                                window->unreadConversationIds_.contains(conversationId) ||
                                !createsUnreadAttention(event.type)) {
                                return;
                            }
                            window->unreadConversationIds_.insert(conversationId);
                            window->refreshConversationList();
                        });
            }
        }
    }
    if (pinConversationAction_ != nullptr) {
        pinConversationAction_->setText(
            controller_->conversation().pinned ? tr("Unpin conversation") : tr("Pin conversation"));
    }
    if (markAllConversationsReadAction_ != nullptr) {
        markAllConversationsReadAction_->setEnabled(!unreadConversationIds_.isEmpty());
    }
    if (nextUnreadConversationAction_ != nullptr) {
        const bool hasActiveUnread = std::any_of(
            conversationCatalog_.cbegin(), conversationCatalog_.cend(), [this](const auto& value) {
                return !value.archived && unreadConversationIds_.contains(value.id);
            });
        nextUnreadConversationAction_->setEnabled(hasActiveUnread);
    }
    if (!error.isEmpty()) {
        statusBar()->showMessage(error, 8000);
    }
}

void MainWindow::activateConversation(QListWidgetItem* item) {
    if (item == nullptr || sessions_ == nullptr) {
        return;
    }
    if (item->data(Qt::UserRole + 1).toBool()) {
        statusBar()->showMessage(tr("Restore the archived conversation before opening it"), 5000);
        return;
    }
    activateConversationById(item->data(Qt::UserRole).toUuid());
}

void MainWindow::activateConversationById(const QUuid& conversationId) {
    if (sessions_ == nullptr || conversationId.isNull()) {
        return;
    }
    if (conversationId == controller_->conversation().id) {
        return;
    }
    const auto selected =
        std::find_if(conversationCatalog_.cbegin(), conversationCatalog_.cend(),
                     [&conversationId](const auto& value) { return value.id == conversationId; });
    if (selected == conversationCatalog_.cend()) {
        refreshConversationList();
        return;
    }

    persistComposerDraft();
    QString error;
    auto* nextController = sessions_->open(*selected, &error);
    if (nextController == nullptr) {
        statusBar()->showMessage(tr("Cannot open conversation: %1").arg(error), 8000);
        refreshConversationList();
        return;
    }

    bindConversation(nextController);
}

void MainWindow::activateRelativeConversation(int offset) {
    if (sessions_ == nullptr || offset == 0) {
        return;
    }
    QList<QUuid> activeConversationIds;
    for (const auto& conversation : conversationCatalog_) {
        if (!conversation.archived) {
            activeConversationIds.append(conversation.id);
        }
    }
    if (activeConversationIds.size() < 2) {
        return;
    }
    const qsizetype currentIndex = activeConversationIds.indexOf(controller_->conversation().id);
    if (currentIndex < 0) {
        refreshConversationList();
        return;
    }
    const qsizetype count = activeConversationIds.size();
    const qsizetype nextIndex = (currentIndex + offset + count) % count;
    activateConversationById(activeConversationIds.at(nextIndex));
}

void MainWindow::createConversation() {
    if (sessions_ == nullptr) {
        return;
    }
    persistComposerDraft();
    const domain::AgentKind requestedKind = settingsSnapshot_.preferredAgentKind;
    QString error;
    auto* created = sessions_->create(controller_->conversation().workingDirectory, requestedKind,
                                      tr("New conversation"), &error);
    if (created == nullptr) {
        statusBar()->showMessage(tr("Cannot create conversation: %1").arg(error), 8000);
        return;
    }
    bindConversation(created);
    composer_->setFocus();
}

void MainWindow::archiveConversation() {
    if (sessions_ == nullptr) {
        return;
    }
    const QUuid archivedId = controller_->conversation().id;
    const QString workspace = controller_->conversation().workingDirectory;
    persistComposerDraft();
    QString error;
    const auto conversations = sessions_->catalog(&error);
    const auto next = std::find_if(
        conversations.cbegin(), conversations.cend(), [&archivedId](const auto& conversation) {
            return !conversation.archived && conversation.id != archivedId;
        });
    session::SessionController* nextController = nullptr;
    if (next != conversations.cend()) {
        nextController = sessions_->open(*next, &error);
    } else {
        nextController = sessions_->create(workspace, settingsSnapshot_.preferredAgentKind,
                                           tr("New conversation"), &error);
    }
    if (nextController == nullptr) {
        statusBar()->showMessage(tr("Cannot prepare a replacement conversation: %1").arg(error),
                                 8000);
        return;
    }
    disconnect(controller_, nullptr, this, nullptr);
    if (!sessions_->setArchived(archivedId, true, &error)) {
        connectControllerSignals();
        statusBar()->showMessage(tr("Cannot archive conversation: %1").arg(error), 8000);
        return;
    }
    controller_ = nullptr;
    bindConversation(nextController);
}

void MainWindow::restoreSelectedConversation() {
    if (sessions_ == nullptr || conversationList_->currentItem() == nullptr) {
        return;
    }
    const auto* item = conversationList_->currentItem();
    if (!item->data(Qt::UserRole + 1).toBool()) {
        statusBar()->showMessage(tr("The selected conversation is not archived"), 4000);
        return;
    }
    const QUuid conversationId = item->data(Qt::UserRole).toUuid();
    QString error;
    auto* restoredController = sessions_->restore(conversationId, &error);
    if (restoredController == nullptr) {
        statusBar()->showMessage(tr("Cannot restore conversation: %1").arg(error), 8000);
        refreshConversationList();
        return;
    }
    persistComposerDraft();
    bindConversation(restoredController);
}

void MainWindow::togglePinnedConversation() {
    if (sessions_ == nullptr) {
        return;
    }
    QString error;
    if (!sessions_->setPinned(controller_->conversation().id, !controller_->conversation().pinned,
                              &error)) {
        statusBar()->showMessage(tr("Cannot update pinned state: %1").arg(error), 8000);
        return;
    }
    refreshConversationList();
}

void MainWindow::openSelectedConversation() {
    activateConversation(conversationList_->currentItem());
}

void MainWindow::archiveSelectedConversation() {
    if (sessions_ == nullptr || conversationList_->currentItem() == nullptr) {
        return;
    }
    const QUuid conversationId = conversationList_->currentItem()->data(Qt::UserRole).toUuid();
    if (conversationId == controller_->conversation().id) {
        archiveConversation();
        return;
    }
    QString error;
    if (!sessions_->setArchived(conversationId, true, &error)) {
        statusBar()->showMessage(tr("Cannot archive conversation: %1").arg(error), 8000);
        return;
    }
    refreshConversationList();
}

void MainWindow::toggleSelectedPinnedConversation() {
    if (sessions_ == nullptr || conversationList_->currentItem() == nullptr) {
        return;
    }
    const QUuid conversationId = conversationList_->currentItem()->data(Qt::UserRole).toUuid();
    const auto selected =
        std::find_if(conversationCatalog_.cbegin(), conversationCatalog_.cend(),
                     [&conversationId](const auto& value) { return value.id == conversationId; });
    if (selected == conversationCatalog_.cend()) {
        refreshConversationList();
        return;
    }
    QString error;
    if (!sessions_->setPinned(conversationId, !selected->pinned, &error)) {
        statusBar()->showMessage(tr("Cannot update pinned state: %1").arg(error), 8000);
        return;
    }
    refreshConversationList();
}

void MainWindow::prepareConversationContextMenu() {
    const auto* item = conversationList_->currentItem();
    const bool hasSelection = item != nullptr;
    const bool archived = hasSelection && item->data(Qt::UserRole + 1).toBool();
    const QUuid conversationId = hasSelection ? item->data(Qt::UserRole).toUuid() : QUuid{};
    const auto selected =
        std::find_if(conversationCatalog_.cbegin(), conversationCatalog_.cend(),
                     [&conversationId](const auto& value) { return value.id == conversationId; });
    const bool pinned = selected != conversationCatalog_.cend() && selected->pinned;
    const bool isCurrent = hasSelection && conversationId == controller_->conversation().id;

    contextOpenAction_->setEnabled(hasSelection && !archived && !isCurrent);
    contextPinAction_->setEnabled(hasSelection);
    contextPinAction_->setText(pinned ? tr("Unpin conversation") : tr("Pin conversation"));
    contextEditTagsAction_->setEnabled(hasSelection && (isCurrent || sessions_ != nullptr));
    contextArchiveAction_->setEnabled(hasSelection && !archived);
    contextRestoreAction_->setEnabled(hasSelection && archived);
}

void MainWindow::bindConversation(session::SessionController* controller) {
    Q_ASSERT(controller != nullptr);
    if (controller_ != nullptr) {
        disconnect(controller_, nullptr, this, nullptr);
    }
    controller_ = controller;
    unreadConversationIds_.remove(controller_->conversation().id);
    resetConversationView();
    connectControllerSignals();

    const QUuid conversationId = controller_->conversation().id;
    const auto* runtime = sessions_ != nullptr ? sessions_->runtime(conversationId) : nullptr;
    startupNotice_ = runtime != nullptr && runtime->fellBack
                         ? tr("Using Mock Agent because %1").arg(runtime->detail)
                         : QString{};
    sessionRow_->setToolTip(startupNotice_);
    updateConversationTitle(controller_->conversation().title);
    composer_->setPlainText(settings_->composerDraft(conversationId));
    restoreTimeline();
    rebuildCapabilityControls(controller_->nextTurnSettings());
    updateQueuedMessages(controller_->queuedMessages());
    updateConnectionDetail(controller_->connectionDetail());
    updateStatus(controller_->status());

    settingsSnapshot_.lastConversationId = conversationId.toString(QUuid::WithoutBraces);
    settingsSnapshot_.lastWorkspace = controller_->conversation().workingDirectory;
    settings_->save(settingsSnapshot_);
    conversationSearch_->clear();
    refreshConversationList();
    controller_->open();
}

void MainWindow::resetConversationView() {
    timeline_->clear();
    approvalCards_.clear();
    userInputCards_.clear();
    toolCards_.clear();
    reasoningCards_.clear();
    activeAgentRow_ = -1;
    usageLabel_->clear();
    usageLabel_->hide();
    planList_->clear();
    planItemText_->clear();
    planItemText_->hide();
    planExplanation_->setText(tr("Agent plans will appear here."));
    streamedPlanText_.clear();
    taskDock_->hide();
    const QSignalBlocker blocker(composer_);
    composer_->clear();
}

void MainWindow::activateWindowForRequest(const std::optional<QString>& directory) {
    if (directory.has_value() &&
        directory.value() != controller_->conversation().workingDirectory) {
        statusBar()->showMessage(tr("Workspace request: %1").arg(directory.value()), 5000);
    }
    setWindowState((windowState() & ~Qt::WindowMinimized) | Qt::WindowActive);
    show();
    raise();
    activateWindow();
}

void MainWindow::closeEvent(QCloseEvent* event) {
    persistWindowState();
    if (closeToTrayEnabled_ && !quitRequested_ && !qGuiApp->isSavingSession()) {
        hide();
        event->ignore();
        return;
    }
    shutdown();
    event->accept();
}

void MainWindow::sendMessage() {
    QString error;
    const bool active = controller_->status() == domain::ConversationStatus::Running ||
                        controller_->status() == domain::ConversationStatus::WaitingApproval ||
                        controller_->status() == domain::ConversationStatus::WaitingInput;
    const bool queue = active && sendModeCombo_->currentData().toString() == QLatin1String("queue");
    const bool accepted = queue    ? controller_->queueMessage(composer_->toPlainText(), &error)
                          : active ? controller_->steerMessage(composer_->toPlainText(), &error)
                                   : controller_->sendMessage(composer_->toPlainText(), &error);
    if (!accepted) {
        statusBar()->showMessage(error, 4000);
        return;
    }
    composer_->clear();
    persistComposerDraft();
}

void MainWindow::queueComposerMessage() {
    const bool active = controller_->status() == domain::ConversationStatus::Running ||
                        controller_->status() == domain::ConversationStatus::WaitingApproval ||
                        controller_->status() == domain::ConversationStatus::WaitingInput;
    if (active) {
        const int queueIndex = sendModeCombo_->findData(QStringLiteral("queue"));
        if (queueIndex >= 0) {
            sendModeCombo_->setCurrentIndex(queueIndex);
        }
    }
    sendMessage();
}

void MainWindow::updateQueuedMessages(const QList<domain::QueuedMessage>& messages) {
    const QSignalBlocker blocker(queueList_);
    const QUuid selectedId = queueList_->currentItem() != nullptr
                                 ? queueList_->currentItem()->data(Qt::UserRole).toUuid()
                                 : QUuid{};
    queueList_->clear();
    int selectedRow = -1;
    for (const auto& message : messages) {
        auto* item = new QListWidgetItem(message.content, queueList_);
        item->setData(Qt::UserRole, message.id);
        item->setFlags(item->flags() | Qt::ItemIsEditable);
        if (message.id == selectedId) {
            selectedRow = static_cast<int>(message.position);
        }
    }
    if (selectedRow >= 0) {
        queueList_->setCurrentRow(selectedRow);
    } else if (!messages.isEmpty()) {
        queueList_->setCurrentRow(0);
    }
    queueFrame_->setVisible(!messages.isEmpty());
    updateQueueControls();
}

void MainWindow::updateQueueControls() {
    const qsizetype row = queueList_->currentRow();
    const bool selected = row >= 0;
    queueUpButton_->setEnabled(selected && row > 0);
    queueDownButton_->setEnabled(selected && row + 1 < queueList_->count());
    queueRemoveButton_->setEnabled(selected);
    const bool canSend = controller_->status() == domain::ConversationStatus::Idle ||
                         (controller_->status() == domain::ConversationStatus::Running &&
                          controller_->capabilities().supportsSteering);
    queueSendNowButton_->setEnabled(selected && canSend);
}

void MainWindow::editQueuedMessage(QListWidgetItem* item) {
    if (item == nullptr) {
        return;
    }
    QString error;
    if (!controller_->updateQueuedMessage(item->data(Qt::UserRole).toUuid(), item->text(),
                                          &error)) {
        statusBar()->showMessage(error, 4000);
        updateQueuedMessages(controller_->queuedMessages());
    }
}

void MainWindow::moveQueuedMessageUp() {
    if (auto* item = queueList_->currentItem(); item != nullptr) {
        QString error;
        if (!controller_->moveQueuedMessage(item->data(Qt::UserRole).toUuid(),
                                            queueList_->currentRow() - 1, &error)) {
            statusBar()->showMessage(error, 4000);
        }
    }
}

void MainWindow::moveQueuedMessageDown() {
    if (auto* item = queueList_->currentItem(); item != nullptr) {
        QString error;
        if (!controller_->moveQueuedMessage(item->data(Qt::UserRole).toUuid(),
                                            queueList_->currentRow() + 1, &error)) {
            statusBar()->showMessage(error, 4000);
        }
    }
}

void MainWindow::sendQueuedMessageNow() {
    if (auto* item = queueList_->currentItem(); item != nullptr) {
        QString error;
        if (!controller_->sendQueuedMessageNow(item->data(Qt::UserRole).toUuid(), &error)) {
            statusBar()->showMessage(error, 4000);
        }
    }
}

void MainWindow::cancelQueuedMessage() {
    if (auto* item = queueList_->currentItem(); item != nullptr) {
        QString error;
        if (!controller_->cancelQueuedMessage(item->data(Qt::UserRole).toUuid(), &error)) {
            statusBar()->showMessage(error, 4000);
        }
    }
}

void MainWindow::showPromptTemplateMenu() {
    rebuildPromptTemplateMenu();
    templateMenu_->popup(templateButton_->mapToGlobal(templateButton_->rect().topLeft()));
}

void MainWindow::rebuildPromptTemplateMenu() {
    templateMenu_->clear();
    QString error;
    const auto templates = controller_->promptTemplates(&error);
    if (!error.isEmpty()) {
        statusBar()->showMessage(error, 5000);
    }
    if (templates.isEmpty()) {
        auto* empty = templateMenu_->addAction(tr("No saved templates"));
        empty->setEnabled(false);
    } else {
        for (const auto& promptTemplate : templates) {
            auto* action = templateMenu_->addAction(
                promptTemplate.favorite ? tr("Favorite: %1").arg(promptTemplate.name)
                                        : promptTemplate.name);
            action->setObjectName(QStringLiteral("promptTemplateAction_%1")
                                      .arg(promptTemplate.id.toString(QUuid::WithoutBraces)));
            connect(action, &QAction::triggered, this,
                    [this, templateId = promptTemplate.id] { insertPromptTemplate(templateId); });
        }
    }
    templateMenu_->addSeparator();
    auto* save = templateMenu_->addAction(tr("Save composer as template..."));
    save->setObjectName(QStringLiteral("savePromptTemplateAction"));
    save->setEnabled(!composer_->toPlainText().trimmed().isEmpty());
    connect(save, &QAction::triggered, this, &MainWindow::saveComposerAsTemplate);

    if (!templates.isEmpty()) {
        auto* removeMenu = templateMenu_->addMenu(tr("Remove template"));
        removeMenu->setObjectName(QStringLiteral("removePromptTemplateMenu"));
        for (const auto& promptTemplate : templates) {
            auto* remove = removeMenu->addAction(promptTemplate.name);
            remove->setObjectName(QStringLiteral("removePromptTemplateAction_%1")
                                      .arg(promptTemplate.id.toString(QUuid::WithoutBraces)));
            connect(remove, &QAction::triggered, this,
                    [this, templateId = promptTemplate.id] { removePromptTemplate(templateId); });
        }
    }
}

void MainWindow::insertPromptTemplate(const QUuid& templateId) {
    QString error;
    const auto templates = controller_->promptTemplates(&error);
    const auto iterator = std::find_if(
        templates.cbegin(), templates.cend(),
        [&templateId](const domain::PromptTemplate& value) { return value.id == templateId; });
    if (iterator == templates.cend()) {
        statusBar()->showMessage(error.isEmpty() ? tr("Prompt template no longer exists") : error,
                                 4000);
        return;
    }

    QString parseError;
    const QStringList parameters = domain::PromptTemplateEngine::parameters(*iterator, &parseError);
    if (!parseError.isEmpty()) {
        statusBar()->showMessage(parseError, 5000);
        return;
    }
    QHash<QString, QString> values;
    if (!parameters.isEmpty()) {
        QDialog dialog(this);
        dialog.setObjectName(QStringLiteral("promptTemplateParameterDialog"));
        dialog.setWindowTitle(tr("Fill template: %1").arg(iterator->name));
        auto* layout = new QFormLayout(&dialog);
        QHash<QString, QLineEdit*> editors;
        for (const QString& parameter : parameters) {
            auto* editor = new QLineEdit(&dialog);
            editor->setObjectName(QStringLiteral("promptTemplateParameter_%1").arg(parameter));
            layout->addRow(parameter, editor);
            editors.insert(parameter, editor);
        }
        auto* buttons =
            new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        layout->addRow(buttons);
        if (dialog.exec() != QDialog::Accepted) {
            return;
        }
        for (auto editor = editors.cbegin(); editor != editors.cend(); ++editor) {
            values.insert(editor.key(), editor.value()->text());
        }
    }
    const auto rendered = domain::PromptTemplateEngine::render(*iterator, values, &error);
    if (!rendered.has_value()) {
        statusBar()->showMessage(error, 5000);
        return;
    }
    composer_->textCursor().insertText(*rendered);
    composer_->setFocus();
}

void MainWindow::saveComposerAsTemplate() {
    bool accepted = false;
    const QString name =
        QInputDialog::getText(this, tr("Save prompt template"), tr("Template name"),
                              QLineEdit::Normal, {}, &accepted)
            .trimmed();
    if (!accepted) {
        return;
    }
    domain::PromptTemplate promptTemplate;
    promptTemplate.name = name;
    promptTemplate.content = composer_->toPlainText();
    promptTemplate.position = controller_->promptTemplates().size();
    QString error;
    if (!controller_->savePromptTemplate(promptTemplate, &error)) {
        statusBar()->showMessage(error, 5000);
        return;
    }
    statusBar()->showMessage(tr("Prompt template saved"), 3000);
}

void MainWindow::removePromptTemplate(const QUuid& templateId) {
    QString error;
    if (!controller_->deletePromptTemplate(templateId, &error)) {
        statusBar()->showMessage(error, 5000);
        return;
    }
    statusBar()->showMessage(tr("Prompt template removed"), 3000);
}

void MainWindow::stopTurn() {
    controller_->interrupt();
    statusBar()->showMessage(tr("Stopping the current turn"), 3000);
}

void MainWindow::renameConversation() {
    bool accepted = false;
    const QString title =
        QInputDialog::getText(this, tr("Rename conversation"), tr("Conversation title"),
                              QLineEdit::Normal, controller_->conversation().title, &accepted);
    if (!accepted) {
        return;
    }
    QString error;
    if (!controller_->renameConversation(title, &error)) {
        statusBar()->showMessage(error, 4000);
    }
}

void MainWindow::editConversationTags() {
    editConversationTagsFor(controller_->conversation().id, controller_->conversation().tags);
}

void MainWindow::editSelectedConversationTags() {
    if (conversationList_->currentItem() == nullptr) {
        return;
    }
    const QUuid conversationId = conversationList_->currentItem()->data(Qt::UserRole).toUuid();
    const auto selected =
        std::find_if(conversationCatalog_.cbegin(), conversationCatalog_.cend(),
                     [&conversationId](const auto& value) { return value.id == conversationId; });
    if (selected == conversationCatalog_.cend()) {
        refreshConversationList();
        return;
    }
    editConversationTagsFor(conversationId, selected->tags);
}

void MainWindow::editConversationTagsFor(const QUuid& conversationId, const QStringList& tags) {
    bool accepted = false;
    const QString value = QInputDialog::getText(
        this, tr("Edit conversation tags"), tr("Comma-separated tags (up to 8)"), QLineEdit::Normal,
        tags.join(QStringLiteral(", ")), &accepted);
    if (!accepted) {
        return;
    }

    QString error;
    const QStringList requestedTags = value.split(QLatin1Char(','));
    const bool saved =
        conversationId == controller_->conversation().id
            ? controller_->setTags(requestedTags, &error)
            : sessions_ != nullptr && sessions_->setTags(conversationId, requestedTags, &error);
    if (!saved) {
        if (error.isEmpty()) {
            error = QStringLiteral("Conversation is unavailable");
        }
        statusBar()->showMessage(tr("Cannot update conversation tags: %1").arg(error), 8000);
        return;
    }
    refreshConversationList();
}

void MainWindow::reconnectSession() { controller_->open(); }

void MainWindow::updateSessionSettings() {
    auto snapshot = controller_->nextTurnSettings();
    snapshot.modelId = modelCombo_->currentData().toString();
    snapshot.reasoningEffort =
        static_cast<domain::ReasoningEffort>(effortCombo_->currentData().toInt());
    snapshot.accessLevel = static_cast<domain::AccessLevel>(accessCombo_->currentData().toInt());
    QString error;
    if (!controller_->setNextTurnSettings(snapshot, &error)) {
        rebuildCapabilityControls(controller_->nextTurnSettings());
        statusBar()->showMessage(tr("Cannot update conversation settings: %1").arg(error), 8000);
        return;
    }
    if (controller_->status() == domain::ConversationStatus::Running) {
        statusBar()->showMessage(tr("Settings apply to the next message"), 3000);
    }
}

void MainWindow::applyLightTheme() {
    settingsSnapshot_.themeMode = app::ThemeMode::Light;
    applyTheme(ThemeDefinition::light());
}

void MainWindow::applySystemTheme() {
    settingsSnapshot_.themeMode = app::ThemeMode::System;
    refreshSystemTheme();
}

void MainWindow::applyDarkTheme() {
    settingsSnapshot_.themeMode = app::ThemeMode::Dark;
    applyTheme(ThemeDefinition::dark());
}

void MainWindow::focusConversationSearch() {
    conversationSearch_->clear();
    conversationSearch_->setFocus();
}

void MainWindow::activateFirstSearchResult() {
    for (int row = 0; row < conversationList_->count(); ++row) {
        auto* item = conversationList_->item(row);
        if (item->data(Qt::UserRole + 1).toBool()) {
            continue;
        }
        const QUuid targetId = item->data(Qt::UserRole).toUuid();
        if (targetId == controller_->conversation().id) {
            conversationSearch_->clear();
            composer_->setFocus();
            return;
        }
        activateConversation(item);
        if (controller_->conversation().id == targetId) {
            composer_->setFocus();
        }
        return;
    }
}

void MainWindow::leaveConversationSearch() {
    conversationSearch_->clear();
    composer_->setFocus();
}

void MainWindow::markAllConversationsRead() {
    if (unreadConversationIds_.isEmpty()) {
        return;
    }
    unreadConversationIds_.clear();
    refreshConversationList();
}

void MainWindow::activateNextUnreadConversation() {
    if (sessions_ == nullptr || unreadConversationIds_.isEmpty() ||
        conversationCatalog_.isEmpty()) {
        return;
    }
    qsizetype currentIndex = 0;
    for (qsizetype index = 0; index < conversationCatalog_.size(); ++index) {
        if (conversationCatalog_.at(index).id == controller_->conversation().id) {
            currentIndex = index;
            break;
        }
    }
    for (qsizetype offset = 1; offset <= conversationCatalog_.size(); ++offset) {
        const auto& candidate =
            conversationCatalog_.at((currentIndex + offset) % conversationCatalog_.size());
        if (!candidate.archived && unreadConversationIds_.contains(candidate.id)) {
            activateConversationById(candidate.id);
            return;
        }
    }
}

void MainWindow::activatePreviousConversation() { activateRelativeConversation(-1); }

void MainWindow::activateNextConversation() { activateRelativeConversation(1); }

void MainWindow::setShowArchivedConversations(bool visible) {
    conversationViewCombo_->setCurrentIndex(0);
    settingsSnapshot_.showArchivedConversations = visible;
    settings_->save(settingsSnapshot_);
    refreshConversationList();
}

void MainWindow::deleteConversationView() {
    const QUuid viewId = conversationViewCombo_->currentData().toUuid();
    if (viewId.isNull()) {
        return;
    }
    QMessageBox prompt(QMessageBox::Warning, tr("Delete conversation view?"),
                       tr("Delete the saved view “%1”? The current filter will remain active.")
                           .arg(conversationViewCombo_->currentText()),
                       QMessageBox::NoButton, this);
    auto* deleteButton = prompt.addButton(tr("Delete view"), QMessageBox::DestructiveRole);
    auto* cancelButton = prompt.addButton(QMessageBox::Cancel);
    prompt.setDefaultButton(cancelButton);
    prompt.setEscapeButton(cancelButton);
    prompt.exec();
    if (prompt.clickedButton() != deleteButton) {
        return;
    }
    QString error;
    if (!controller_->deleteConversationView(viewId, &error)) {
        statusBar()->showMessage(tr("Cannot delete conversation view: %1").arg(error), 8000);
        return;
    }
    rebuildConversationViews();
}

void MainWindow::renameConversationView() {
    const QUuid viewId = conversationViewCombo_->currentData().toUuid();
    if (viewId.isNull()) {
        return;
    }
    QString error;
    const auto views = controller_->conversationViews(&error);
    const auto selected = std::find_if(views.cbegin(), views.cend(),
                                       [&viewId](const auto& view) { return view.id == viewId; });
    if (!error.isEmpty() || selected == views.cend()) {
        statusBar()->showMessage(error.isEmpty() ? tr("Conversation view no longer exists") : error,
                                 8000);
        rebuildConversationViews();
        return;
    }
    bool accepted = false;
    const QString name =
        QInputDialog::getText(this, tr("Rename conversation view"), tr("View name"),
                              QLineEdit::Normal, selected->name, &accepted)
            .simplified();
    if (!accepted || name == selected->name) {
        return;
    }
    domain::SavedConversationView renamed = *selected;
    renamed.name = name;
    if (!controller_->saveConversationView(renamed, &error)) {
        statusBar()->showMessage(tr("Cannot rename conversation view: %1").arg(error), 8000);
        return;
    }
    rebuildConversationViews(viewId);
}

void MainWindow::saveConversationView() {
    bool accepted = false;
    const QString name = QInputDialog::getText(this, tr("Save conversation view"), tr("View name"),
                                               QLineEdit::Normal, {}, &accepted)
                             .simplified();
    if (!accepted) {
        return;
    }
    QString error;
    const auto views = controller_->conversationViews(&error);
    if (!error.isEmpty()) {
        statusBar()->showMessage(error, 8000);
        return;
    }
    domain::SavedConversationView view;
    view.name = name;
    view.query = conversationSearch_->text();
    view.showArchived = settingsSnapshot_.showArchivedConversations;
    view.position = views.size();
    if (!controller_->saveConversationView(view, &error)) {
        statusBar()->showMessage(tr("Cannot save conversation view: %1").arg(error), 8000);
        return;
    }
    rebuildConversationViews(view.id);
}

void MainWindow::applyConversationView(int index) {
    deleteConversationViewButton_->setEnabled(index > 0);
    renameConversationViewAction_->setEnabled(index > 0);
    if (index <= 0) {
        return;
    }
    const QUuid viewId = conversationViewCombo_->itemData(index).toUuid();
    const auto views = controller_->conversationViews();
    const auto selected = std::find_if(views.cbegin(), views.cend(),
                                       [&viewId](const auto& view) { return view.id == viewId; });
    if (selected == views.cend()) {
        rebuildConversationViews();
        return;
    }
    conversationSearch_->setText(selected->query);
    showArchivedConversationsAction_->setChecked(selected->showArchived);
    const QSignalBlocker blocker(conversationViewCombo_);
    conversationViewCombo_->setCurrentIndex(conversationViewCombo_->findData(viewId));
    deleteConversationViewButton_->setEnabled(true);
    renameConversationViewAction_->setEnabled(true);
}

void MainWindow::rebuildConversationViews(const QUuid& selectedViewId) {
    QString error;
    const auto views = controller_->conversationViews(&error);
    const QSignalBlocker blocker(conversationViewCombo_);
    conversationViewCombo_->clear();
    conversationViewCombo_->addItem(tr("Current filter"));
    for (const auto& view : views) {
        conversationViewCombo_->addItem(view.name, view.id);
    }
    const int selectedIndex =
        selectedViewId.isNull() ? 0 : conversationViewCombo_->findData(selectedViewId);
    conversationViewCombo_->setCurrentIndex(std::max(0, selectedIndex));
    deleteConversationViewButton_->setEnabled(selectedIndex > 0);
    renameConversationViewAction_->setEnabled(selectedIndex > 0);
    if (!error.isEmpty()) {
        statusBar()->showMessage(error, 8000);
    }
}

void MainWindow::increaseScale() { applyInterfaceScale(settingsSnapshot_.interfaceScale + 0.1); }
void MainWindow::decreaseScale() { applyInterfaceScale(settingsSnapshot_.interfaceScale - 0.1); }
void MainWindow::resetScale() { applyInterfaceScale(1.0); }

void MainWindow::preferCodexAgent() { setPreferredAgent(domain::AgentKind::Codex); }

void MainWindow::preferMockAgent() { setPreferredAgent(domain::AgentKind::Mock); }

void MainWindow::requestQuit() {
    if (!confirmQuit()) {
        return;
    }
    quitRequested_ = true;
    if (trayIcon_ != nullptr) {
        trayIcon_->hide();
    }
    close();
    qApp->quit();
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
    if (watched == conversationSearch_ && event->type() == QEvent::KeyPress) {
        const auto* keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->key() == Qt::Key_Escape) {
            leaveConversationSearch();
            return true;
        }
        if (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter) {
            activateFirstSearchResult();
            return true;
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::buildUi() {
    setWindowTitle(tr("Snack"));
    setMinimumSize(1280, 800);
    resize(1440, 900);

    auto* central = new QWidget(this);
    central->setObjectName(QStringLiteral("centralWorkbench"));
    auto* rootLayout = new QHBoxLayout(central);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    auto* sidebar = new QWidget(central);
    sidebar->setObjectName(QStringLiteral("sessionSidebar"));
    sidebar->setFixedWidth(280);
    auto* sidebarLayout = new QVBoxLayout(sidebar);
    sidebarLayout->setContentsMargins(16, 18, 16, 18);
    auto* brand = new QLabel(tr("SNACK  /  零食"), sidebar);
    auto* newConversation = new QPushButton(tr("New conversation"), sidebar);
    newConversation->setObjectName(QStringLiteral("newConversationButton"));
    newConversation->setEnabled(sessions_ != nullptr);
    auto* viewRow = new QWidget(sidebar);
    auto* viewRowLayout = new QHBoxLayout(viewRow);
    viewRowLayout->setContentsMargins(0, 0, 0, 0);
    conversationViewCombo_ = new QComboBox(viewRow);
    conversationViewCombo_->setObjectName(QStringLiteral("conversationViewCombo"));
    saveConversationViewButton_ = new QPushButton(tr("Save view"), viewRow);
    saveConversationViewButton_->setObjectName(QStringLiteral("saveConversationViewButton"));
    auto* manageConversationViewButton = new QToolButton(viewRow);
    manageConversationViewButton->setObjectName(QStringLiteral("manageConversationViewButton"));
    manageConversationViewButton->setText(tr("Manage"));
    manageConversationViewButton->setPopupMode(QToolButton::InstantPopup);
    auto* manageConversationViewMenu = new QMenu(manageConversationViewButton);
    renameConversationViewAction_ =
        manageConversationViewMenu->addAction(tr("Rename saved view..."));
    renameConversationViewAction_->setObjectName(QStringLiteral("renameConversationViewAction"));
    renameConversationViewAction_->setEnabled(false);
    manageConversationViewButton->setMenu(manageConversationViewMenu);
    deleteConversationViewButton_ = new QPushButton(tr("Delete"), viewRow);
    deleteConversationViewButton_->setObjectName(QStringLiteral("deleteConversationViewButton"));
    deleteConversationViewButton_->setEnabled(false);
    viewRowLayout->addWidget(conversationViewCombo_, 1);
    viewRowLayout->addWidget(saveConversationViewButton_);
    viewRowLayout->addWidget(manageConversationViewButton);
    viewRowLayout->addWidget(deleteConversationViewButton_);
    conversationSearch_ = new QLineEdit(sidebar);
    conversationSearch_->setObjectName(QStringLiteral("conversationSearch"));
    conversationSearch_->setPlaceholderText(tr("Search conversations or tag:name"));
    conversationSearch_->setToolTip(
        tr("Filters: tag:name, agent:name, model:id, status:name, path:\"directory\""));
    conversationSearch_->setClearButtonEnabled(true);
    conversationSearch_->installEventFilter(this);
    sessionRow_ = new QLabel(
        tr("●  %1\n    %2").arg(agentDisplayName(), controller_->conversation().title), sidebar);
    sessionRow_->setObjectName(QStringLiteral("sessionRow"));
    sessionRow_->setContentsMargins(8, 14, 8, 14);
    conversationList_ = new QListWidget(sidebar);
    conversationList_->setObjectName(QStringLiteral("conversationList"));
    conversationList_->setContextMenuPolicy(Qt::CustomContextMenu);
    conversationContextMenu_ = new QMenu(conversationList_);
    conversationContextMenu_->setObjectName(QStringLiteral("conversationContextMenu"));
    contextOpenAction_ = conversationContextMenu_->addAction(tr("Open conversation"));
    contextOpenAction_->setObjectName(QStringLiteral("contextOpenConversationAction"));
    contextPinAction_ = conversationContextMenu_->addAction(tr("Pin conversation"));
    contextPinAction_->setObjectName(QStringLiteral("contextPinConversationAction"));
    contextEditTagsAction_ = conversationContextMenu_->addAction(tr("Edit conversation tags..."));
    contextEditTagsAction_->setObjectName(QStringLiteral("contextEditConversationTagsAction"));
    conversationContextMenu_->addSeparator();
    contextArchiveAction_ = conversationContextMenu_->addAction(tr("Archive conversation"));
    contextArchiveAction_->setObjectName(QStringLiteral("contextArchiveConversationAction"));
    contextRestoreAction_ = conversationContextMenu_->addAction(tr("Restore conversation"));
    contextRestoreAction_->setObjectName(QStringLiteral("contextRestoreConversationAction"));
    sidebarLayout->addWidget(brand);
    sidebarLayout->addSpacing(10);
    sidebarLayout->addWidget(newConversation);
    sidebarLayout->addWidget(viewRow);
    sidebarLayout->addWidget(conversationSearch_);
    sidebarLayout->addWidget(sessionRow_);
    sidebarLayout->addWidget(conversationList_, 1);

    auto* conversation = new QWidget(central);
    auto* conversationLayout = new QVBoxLayout(conversation);
    conversationLayout->setContentsMargins(0, 0, 0, 12);
    conversationLayout->setSpacing(0);

    auto* header = new QFrame(conversation);
    header->setObjectName(QStringLiteral("sessionHeader"));
    auto* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(22, 12, 22, 12);
    titleLabel_ = new QLabel(controller_->conversation().title, header);
    titleLabel_->setObjectName(QStringLiteral("conversationTitle"));
    statusLabel_ = new QLabel(tr("Dormant"), header);
    statusLabel_->setObjectName(QStringLiteral("statusLabel"));
    reconnectButton_ = new QPushButton(tr("Reconnect"), header);
    reconnectButton_->setObjectName(QStringLiteral("reconnectButton"));
    reconnectButton_->hide();
    usageLabel_ = new QLabel(header);
    usageLabel_->setObjectName(QStringLiteral("tokenUsageLabel"));
    usageLabel_->hide();
    modelCombo_ = new QComboBox(header);
    modelCombo_->setObjectName(QStringLiteral("modelCombo"));
    effortCombo_ = new QComboBox(header);
    effortCombo_->setObjectName(QStringLiteral("effortCombo"));
    accessCombo_ = new QComboBox(header);
    accessCombo_->setObjectName(QStringLiteral("accessCombo"));

    headerLayout->addWidget(titleLabel_);
    headerLayout->addWidget(statusLabel_);
    headerLayout->addWidget(usageLabel_);
    headerLayout->addStretch();
    headerLayout->addWidget(modelCombo_);
    headerLayout->addWidget(effortCombo_);
    headerLayout->addWidget(accessCombo_);

    connectionNoticeFrame_ = new QFrame(conversation);
    connectionNoticeFrame_->setObjectName(QStringLiteral("connectionNoticeFrame"));
    auto* connectionNoticeLayout = new QHBoxLayout(connectionNoticeFrame_);
    connectionNoticeLayout->setContentsMargins(22, 10, 22, 10);
    auto* connectionNoticeText = new QWidget(connectionNoticeFrame_);
    auto* connectionNoticeTextLayout = new QVBoxLayout(connectionNoticeText);
    connectionNoticeTextLayout->setContentsMargins(0, 0, 0, 0);
    connectionNoticeTextLayout->setSpacing(2);
    connectionNoticeTitle_ = new QLabel(connectionNoticeText);
    connectionNoticeTitle_->setObjectName(QStringLiteral("connectionNoticeTitle"));
    connectionNoticeDetail_ = new QLabel(connectionNoticeText);
    connectionNoticeDetail_->setObjectName(QStringLiteral("connectionNoticeDetail"));
    connectionNoticeDetail_->setWordWrap(true);
    connectionNoticeDetail_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    connectionNoticeTextLayout->addWidget(connectionNoticeTitle_);
    connectionNoticeTextLayout->addWidget(connectionNoticeDetail_);
    connectionNoticeLayout->addWidget(connectionNoticeText, 1);
    connectionNoticeLayout->addWidget(reconnectButton_);
    connectionNoticeFrame_->hide();

    timeline_ = new QListWidget(conversation);
    timeline_->setObjectName(QStringLiteral("timeline"));
    timeline_->setWordWrap(true);
    timeline_->setSelectionMode(QAbstractItemView::NoSelection);

    auto* composerFrame = new QWidget(conversation);
    auto* composerLayout = new QVBoxLayout(composerFrame);
    composerLayout->setContentsMargins(70, 10, 70, 0);
    queueFrame_ = new QFrame(composerFrame);
    queueFrame_->setObjectName(QStringLiteral("queueFrame"));
    auto* queueLayout = new QVBoxLayout(queueFrame_);
    auto* queueHeader = new QHBoxLayout();
    queueHeader->addWidget(new QLabel(tr("Queued messages"), queueFrame_));
    queueHeader->addStretch();
    queueUpButton_ = new QPushButton(tr("Up"), queueFrame_);
    queueUpButton_->setObjectName(QStringLiteral("queueUpButton"));
    queueDownButton_ = new QPushButton(tr("Down"), queueFrame_);
    queueDownButton_->setObjectName(QStringLiteral("queueDownButton"));
    queueSendNowButton_ = new QPushButton(tr("Send now"), queueFrame_);
    queueSendNowButton_->setObjectName(QStringLiteral("queueSendNowButton"));
    queueRemoveButton_ = new QPushButton(tr("Remove"), queueFrame_);
    queueRemoveButton_->setObjectName(QStringLiteral("queueRemoveButton"));
    queueHeader->addWidget(queueUpButton_);
    queueHeader->addWidget(queueDownButton_);
    queueHeader->addWidget(queueSendNowButton_);
    queueHeader->addWidget(queueRemoveButton_);
    queueList_ = new QListWidget(queueFrame_);
    queueList_->setObjectName(QStringLiteral("queueList"));
    queueList_->setMaximumHeight(130);
    queueLayout->addLayout(queueHeader);
    queueLayout->addWidget(queueList_);
    queueFrame_->hide();

    auto* composerRow = new QWidget(composerFrame);
    auto* composerRowLayout = new QHBoxLayout(composerRow);
    composerRowLayout->setContentsMargins(0, 0, 0, 0);
    composer_ = new ComposerTextEdit(composerRow);
    composer_->setObjectName(QStringLiteral("composer"));
    composer_->setPlaceholderText(tr("Ask the agent about this workspace..."));
    templateButton_ = new QPushButton(QStringLiteral("/"), composerRow);
    templateButton_->setObjectName(QStringLiteral("templateButton"));
    templateButton_->setToolTip(tr("Prompt templates"));
    templateMenu_ = new QMenu(templateButton_);
    templateMenu_->setObjectName(QStringLiteral("templateMenu"));
    sendModeCombo_ = new QComboBox(composerRow);
    sendModeCombo_->setObjectName(QStringLiteral("sendModeCombo"));
    sendModeCombo_->hide();
    sendButton_ = new QPushButton(tr("Send"), composerRow);
    sendButton_->setObjectName(QStringLiteral("sendButton"));
    stopButton_ = new QPushButton(tr("Stop"), composerRow);
    stopButton_->setObjectName(QStringLiteral("stopButton"));
    stopButton_->hide();
    composerRowLayout->addWidget(templateButton_, 0, Qt::AlignBottom);
    composerRowLayout->addWidget(composer_, 1);
    composerRowLayout->addWidget(sendModeCombo_, 0, Qt::AlignBottom);
    composerRowLayout->addWidget(stopButton_, 0, Qt::AlignBottom);
    composerRowLayout->addWidget(sendButton_, 0, Qt::AlignBottom);
    composerLayout->addWidget(queueFrame_);
    composerLayout->addWidget(composerRow);

    conversationLayout->addWidget(header);
    conversationLayout->addWidget(connectionNoticeFrame_);
    conversationLayout->addWidget(timeline_, 1);
    conversationLayout->addWidget(composerFrame);
    rootLayout->addWidget(sidebar);
    rootLayout->addWidget(conversation, 1);
    setCentralWidget(central);

    taskDock_ = new QDockWidget(tr("Tasks"), this);
    taskDock_->setObjectName(QStringLiteral("taskDock"));
    auto* planPanel = new QWidget(taskDock_);
    auto* planLayout = new QVBoxLayout(planPanel);
    planExplanation_ = new QLabel(tr("Agent plans will appear here."), planPanel);
    planExplanation_->setObjectName(QStringLiteral("planExplanation"));
    planExplanation_->setWordWrap(true);
    planItemText_ = new QLabel(planPanel);
    planItemText_->setObjectName(QStringLiteral("planItemText"));
    planItemText_->setWordWrap(true);
    planItemText_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    planItemText_->hide();
    planList_ = new QListWidget(planPanel);
    planList_->setObjectName(QStringLiteral("planList"));
    planList_->setWordWrap(true);
    planList_->setSelectionMode(QAbstractItemView::NoSelection);
    planLayout->addWidget(planExplanation_);
    planLayout->addWidget(planItemText_);
    planLayout->addWidget(planList_, 1);
    taskDock_->setWidget(planPanel);
    addDockWidget(Qt::RightDockWidgetArea, taskDock_);
    taskDock_->hide();

    auto* terminalDock = new QDockWidget(tr("Terminal"), this);
    terminalDock->setObjectName(QStringLiteral("terminalDock"));
    terminalDock->setWidget(
        new QLabel(tr("Terminal integration is scheduled for M4."), terminalDock));
    addDockWidget(Qt::BottomDockWidgetArea, terminalDock);
    terminalDock->hide();

    connect(sendButton_, &QPushButton::clicked, this, &MainWindow::sendMessage);
    connect(stopButton_, &QPushButton::clicked, this, &MainWindow::stopTurn);
    connect(reconnectButton_, &QPushButton::clicked, this, &MainWindow::reconnectSession);
    connect(newConversation, &QPushButton::clicked, this, &MainWindow::createConversation);
    connect(saveConversationViewButton_, &QPushButton::clicked, this,
            &MainWindow::saveConversationView);
    connect(renameConversationViewAction_, &QAction::triggered, this,
            &MainWindow::renameConversationView);
    connect(deleteConversationViewButton_, &QPushButton::clicked, this,
            &MainWindow::deleteConversationView);
    connect(conversationViewCombo_, &QComboBox::currentIndexChanged, this,
            &MainWindow::applyConversationView);
    connect(conversationList_, &QListWidget::itemClicked, this, &MainWindow::activateConversation);
    connect(conversationList_, &QListWidget::itemActivated, this,
            &MainWindow::activateConversation);
    connect(conversationList_, &QListWidget::customContextMenuRequested, this,
            [this](const QPoint& position) {
                auto* item = conversationList_->itemAt(position);
                if (item == nullptr) {
                    return;
                }
                conversationList_->setCurrentItem(item);
                prepareConversationContextMenu();
                conversationContextMenu_->popup(
                    conversationList_->viewport()->mapToGlobal(position));
            });
    connect(contextOpenAction_, &QAction::triggered, this, &MainWindow::openSelectedConversation);
    connect(contextPinAction_, &QAction::triggered, this,
            &MainWindow::toggleSelectedPinnedConversation);
    connect(contextEditTagsAction_, &QAction::triggered, this,
            &MainWindow::editSelectedConversationTags);
    connect(contextArchiveAction_, &QAction::triggered, this,
            &MainWindow::archiveSelectedConversation);
    connect(contextRestoreAction_, &QAction::triggered, this,
            &MainWindow::restoreSelectedConversation);
    connect(conversationSearch_, &QLineEdit::textChanged, this, [this] {
        conversationViewCombo_->setCurrentIndex(0);
        refreshConversationList();
    });
    connect(composer_, &ComposerTextEdit::sendRequested, this, &MainWindow::sendMessage);
    connect(composer_, &ComposerTextEdit::queueRequested, this, &MainWindow::queueComposerMessage);
    connect(composer_, &ComposerTextEdit::stopRequested, this, &MainWindow::stopTurn);
    connect(composer_, &ComposerTextEdit::templateMenuRequested, this,
            &MainWindow::showPromptTemplateMenu);
    connect(templateButton_, &QPushButton::clicked, this, &MainWindow::showPromptTemplateMenu);
    connect(templateMenu_, &QMenu::aboutToShow, this, &MainWindow::rebuildPromptTemplateMenu);
    draftSaveTimer_ = new QTimer(this);
    draftSaveTimer_->setSingleShot(true);
    draftSaveTimer_->setInterval(350);
    connect(composer_, &QPlainTextEdit::textChanged, draftSaveTimer_, qOverload<>(&QTimer::start));
    connect(draftSaveTimer_, &QTimer::timeout, this, &MainWindow::persistComposerDraft);
    connect(sendModeCombo_, &QComboBox::currentIndexChanged, this,
            [this] { updateStatus(controller_->status()); });
    connect(queueList_, &QListWidget::itemChanged, this, &MainWindow::editQueuedMessage);
    connect(queueList_, &QListWidget::currentRowChanged, this, &MainWindow::updateQueueControls);
    connect(queueUpButton_, &QPushButton::clicked, this, &MainWindow::moveQueuedMessageUp);
    connect(queueDownButton_, &QPushButton::clicked, this, &MainWindow::moveQueuedMessageDown);
    connect(queueSendNowButton_, &QPushButton::clicked, this, &MainWindow::sendQueuedMessageNow);
    connect(queueRemoveButton_, &QPushButton::clicked, this, &MainWindow::cancelQueuedMessage);
    connect(modelCombo_, &QComboBox::currentIndexChanged, this, &MainWindow::updateSessionSettings);
    connect(effortCombo_, &QComboBox::currentIndexChanged, this,
            &MainWindow::updateSessionSettings);
    connect(accessCombo_, &QComboBox::currentIndexChanged, this,
            &MainWindow::updateSessionSettings);
    rebuildPromptTemplateMenu();
    rebuildConversationViews();
}

void MainWindow::buildMenus() {
    auto* fileMenu = menuBar()->addMenu(tr("File"));
    auto* newConversationAction = fileMenu->addAction(tr("New conversation"));
    newConversationAction->setObjectName(QStringLiteral("newConversationAction"));
    newConversationAction->setShortcut(QKeySequence::New);
    newConversationAction->setEnabled(sessions_ != nullptr);
    connect(newConversationAction, &QAction::triggered, this, &MainWindow::createConversation);
    fileMenu->addSeparator();
    auto* renameAction = fileMenu->addAction(tr("Rename conversation..."));
    renameAction->setObjectName(QStringLiteral("renameConversationAction"));
    renameAction->setShortcut(QKeySequence(Qt::Key_F2));
    connect(renameAction, &QAction::triggered, this, &MainWindow::renameConversation);
    auto* editTagsAction = fileMenu->addAction(tr("Edit conversation tags..."));
    editTagsAction->setObjectName(QStringLiteral("editConversationTagsAction"));
    connect(editTagsAction, &QAction::triggered, this, &MainWindow::editConversationTags);
    auto* archiveAction = fileMenu->addAction(tr("Archive conversation"));
    archiveAction->setObjectName(QStringLiteral("archiveConversationAction"));
    archiveAction->setEnabled(sessions_ != nullptr);
    connect(archiveAction, &QAction::triggered, this, &MainWindow::archiveConversation);
    auto* restoreAction = fileMenu->addAction(tr("Restore selected conversation"));
    restoreAction->setObjectName(QStringLiteral("restoreConversationAction"));
    restoreAction->setEnabled(sessions_ != nullptr);
    connect(restoreAction, &QAction::triggered, this, &MainWindow::restoreSelectedConversation);
    pinConversationAction_ = fileMenu->addAction(tr("Pin conversation"));
    pinConversationAction_->setObjectName(QStringLiteral("pinConversationAction"));
    pinConversationAction_->setEnabled(sessions_ != nullptr);
    connect(pinConversationAction_, &QAction::triggered, this,
            &MainWindow::togglePinnedConversation);
    fileMenu->addSeparator();
    auto* quitAction = fileMenu->addAction(tr("Quit"));
    quitAction->setObjectName(QStringLiteral("quitAction"));
    quitAction->setShortcut(QKeySequence::Quit);
    connect(quitAction, &QAction::triggered, this, &MainWindow::requestQuit);

    auto* agentMenu = menuBar()->addMenu(tr("Agent"));
    auto* agentGroup = new QActionGroup(agentMenu);
    agentGroup->setExclusive(true);
    auto* codexAction = agentMenu->addAction(tr("Use Codex for new conversations"));
    codexAction->setObjectName(QStringLiteral("preferCodexAction"));
    codexAction->setCheckable(true);
    auto* mockAction = agentMenu->addAction(tr("Use Mock Agent for new conversations"));
    mockAction->setObjectName(QStringLiteral("preferMockAction"));
    mockAction->setCheckable(true);
    agentGroup->addAction(codexAction);
    agentGroup->addAction(mockAction);
    codexAction->setChecked(settingsSnapshot_.preferredAgentKind == domain::AgentKind::Codex);
    mockAction->setChecked(settingsSnapshot_.preferredAgentKind == domain::AgentKind::Mock);
    connect(codexAction, &QAction::triggered, this, &MainWindow::preferCodexAgent);
    connect(mockAction, &QAction::triggered, this, &MainWindow::preferMockAgent);

    auto* viewMenu = menuBar()->addMenu(tr("View"));
    auto* themeGroup = new QActionGroup(viewMenu);
    themeGroup->setExclusive(true);
    auto* systemAction = viewMenu->addAction(tr("System theme"));
    auto* lightAction = viewMenu->addAction(tr("Light theme"));
    auto* darkAction = viewMenu->addAction(tr("Dark theme"));
    systemAction->setObjectName(QStringLiteral("systemThemeAction"));
    lightAction->setObjectName(QStringLiteral("lightThemeAction"));
    darkAction->setObjectName(QStringLiteral("darkThemeAction"));
    for (QAction* action : {systemAction, lightAction, darkAction}) {
        action->setCheckable(true);
        themeGroup->addAction(action);
    }
    systemAction->setChecked(settingsSnapshot_.themeMode == app::ThemeMode::System);
    lightAction->setChecked(settingsSnapshot_.themeMode == app::ThemeMode::Light);
    darkAction->setChecked(settingsSnapshot_.themeMode == app::ThemeMode::Dark);
    viewMenu->addSeparator();
    auto* searchConversations = viewMenu->addAction(tr("Search conversations"));
    searchConversations->setObjectName(QStringLiteral("searchConversationsAction"));
    searchConversations->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_K));
    connect(searchConversations, &QAction::triggered, this, &MainWindow::focusConversationSearch);
    showArchivedConversationsAction_ = viewMenu->addAction(tr("Show archived conversations"));
    showArchivedConversationsAction_->setObjectName(
        QStringLiteral("showArchivedConversationsAction"));
    showArchivedConversationsAction_->setCheckable(true);
    showArchivedConversationsAction_->setChecked(settingsSnapshot_.showArchivedConversations);
    connect(showArchivedConversationsAction_, &QAction::toggled, this,
            &MainWindow::setShowArchivedConversations);
    markAllConversationsReadAction_ = viewMenu->addAction(tr("Mark all conversations read"));
    markAllConversationsReadAction_->setObjectName(
        QStringLiteral("markAllConversationsReadAction"));
    markAllConversationsReadAction_->setEnabled(false);
    connect(markAllConversationsReadAction_, &QAction::triggered, this,
            &MainWindow::markAllConversationsRead);
    nextUnreadConversationAction_ = viewMenu->addAction(tr("Open next unread conversation"));
    nextUnreadConversationAction_->setObjectName(QStringLiteral("nextUnreadConversationAction"));
    nextUnreadConversationAction_->setEnabled(false);
    connect(nextUnreadConversationAction_, &QAction::triggered, this,
            &MainWindow::activateNextUnreadConversation);
    auto* previousConversation = viewMenu->addAction(tr("Previous conversation"));
    previousConversation->setObjectName(QStringLiteral("previousConversationAction"));
    previousConversation->setShortcut(QKeySequence::PreviousChild);
    previousConversation->setEnabled(sessions_ != nullptr);
    connect(previousConversation, &QAction::triggered, this,
            &MainWindow::activatePreviousConversation);
    auto* nextConversation = viewMenu->addAction(tr("Next conversation"));
    nextConversation->setObjectName(QStringLiteral("nextConversationAction"));
    nextConversation->setShortcut(QKeySequence::NextChild);
    nextConversation->setEnabled(sessions_ != nullptr);
    connect(nextConversation, &QAction::triggered, this, &MainWindow::activateNextConversation);
    auto* focusComposer = viewMenu->addAction(tr("Focus composer"));
    focusComposer->setObjectName(QStringLiteral("focusComposerAction"));
    focusComposer->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_L));
    connect(focusComposer, &QAction::triggered, composer_, qOverload<>(&QWidget::setFocus));
    viewMenu->addSeparator();
    auto* zoomIn = viewMenu->addAction(tr("Zoom in"));
    auto* zoomOut = viewMenu->addAction(tr("Zoom out"));
    auto* resetZoom = viewMenu->addAction(tr("Actual size"));
    zoomIn->setShortcut(QKeySequence::ZoomIn);
    zoomOut->setShortcut(QKeySequence::ZoomOut);
    resetZoom->setShortcut(
        QKeySequence::fromString(QStringLiteral("Ctrl+0"), QKeySequence::PortableText));
    connect(systemAction, &QAction::triggered, this, &MainWindow::applySystemTheme);
    connect(lightAction, &QAction::triggered, this, &MainWindow::applyLightTheme);
    connect(darkAction, &QAction::triggered, this, &MainWindow::applyDarkTheme);
    connect(zoomIn, &QAction::triggered, this, &MainWindow::increaseScale);
    connect(zoomOut, &QAction::triggered, this, &MainWindow::decreaseScale);
    connect(resetZoom, &QAction::triggered, this, &MainWindow::resetScale);
}

void MainWindow::appendEvent(const domain::AgentEvent& event) {
    switch (event.type) {
    case domain::AgentEventType::UserMessage:
        timeline_->addItem(
            tr("You\n%1").arg(event.payload.value(QStringLiteral("text")).toString()));
        activeAgentRow_ = -1;
        break;
    case domain::AgentEventType::AgentMessageStart:
        timeline_->addItem(tr("%1\n").arg(agentDisplayName()));
        activeAgentRow_ = timeline_->count() - 1;
        break;
    case domain::AgentEventType::AgentMessageDelta:
        if (activeAgentRow_ >= 0) {
            auto* item = timeline_->item(activeAgentRow_);
            item->setText(item->text() + event.payload.value(QStringLiteral("text")).toString());
        }
        break;
    case domain::AgentEventType::ToolStarted:
        appendToolStarted(event);
        break;
    case domain::AgentEventType::ToolOutputDelta:
        appendToolProgress(event);
        break;
    case domain::AgentEventType::ToolCompleted:
        completeTool(event);
        break;
    case domain::AgentEventType::ReasoningStarted:
        appendReasoningStarted(event);
        break;
    case domain::AgentEventType::ReasoningSummaryDelta:
        appendReasoningDelta(event);
        break;
    case domain::AgentEventType::ReasoningCompleted:
        completeReasoning(event);
        break;
    case domain::AgentEventType::PlanUpdated:
        updatePlan(event);
        break;
    case domain::AgentEventType::ApprovalRequested:
        appendApprovalRequest(event);
        break;
    case domain::AgentEventType::ApprovalResolved:
        resolveApprovalCard(event);
        break;
    case domain::AgentEventType::UserInputRequested:
        appendUserInputRequest(event);
        break;
    case domain::AgentEventType::UserInputResolved:
        resolveUserInputCard(event);
        break;
    case domain::AgentEventType::UsageUpdated:
        updateUsage(event.payload);
        break;
    case domain::AgentEventType::TurnInterrupted:
        timeline_->addItem(tr("Turn interrupted"));
        activeAgentRow_ = -1;
        break;
    case domain::AgentEventType::WarningRaised:
    case domain::AgentEventType::ErrorRaised:
        timeline_->addItem(event.payload.value(QStringLiteral("message")).toString());
        break;
    default:
        break;
    }
    timeline_->scrollToBottom();
}

void MainWindow::appendApprovalRequest(const domain::AgentEvent& event) {
    const QString requestId = event.payload.value(QStringLiteral("requestId")).toString();
    if (requestId.isEmpty() || approvalCards_.contains(requestId)) {
        return;
    }

    auto* item = new QListWidgetItem(timeline_);
    auto* card = new QFrame(timeline_);
    card->setObjectName(QStringLiteral("approvalCard"));
    auto* layout = new QVBoxLayout(card);
    layout->setContentsMargins(14, 12, 14, 12);

    const bool commandApproval =
        event.payload.value(QStringLiteral("kind")).toString() == QLatin1String("commandExecution");
    auto* title =
        new QLabel(commandApproval ? tr("Command approval") : tr("File change approval"), card);
    title->setObjectName(QStringLiteral("approvalTitle"));
    layout->addWidget(title);

    QStringList details;
    const QJsonObject network =
        event.payload.value(QStringLiteral("networkApprovalContext")).toObject();
    if (!network.isEmpty()) {
        details.append(tr("Network: %1 (%2)")
                           .arg(network.value(QStringLiteral("host")).toString(),
                                network.value(QStringLiteral("protocol")).toString()));
    } else if (commandApproval) {
        details.append(event.payload.value(QStringLiteral("command")).toString());
    } else {
        const QString grantRoot = event.payload.value(QStringLiteral("grantRoot")).toString();
        if (!grantRoot.isEmpty()) {
            details.append(tr("Write access: %1").arg(grantRoot));
        }
        QStringList changedFiles;
        const QJsonArray changes = event.payload.value(QStringLiteral("changes")).toArray();
        for (const QJsonValue& changeValue : changes) {
            const QJsonObject change = changeValue.toObject();
            const QString path = change.value(QStringLiteral("path")).toString();
            if (!path.isEmpty()) {
                changedFiles.append(path);
            }
        }
        if (!changedFiles.isEmpty()) {
            details.append(tr("Proposed files: %1").arg(changedFiles.join(QStringLiteral(", "))));
        }
    }
    const QString cwd = event.payload.value(QStringLiteral("cwd")).toString();
    if (!cwd.isEmpty()) {
        details.append(tr("Working directory: %1").arg(cwd));
    }
    const QString reason = event.payload.value(QStringLiteral("reason")).toString();
    if (!reason.isEmpty()) {
        details.append(tr("Reason: %1").arg(reason));
    }
    auto* detail = new QLabel(details.join(QLatin1Char('\n')), card);
    detail->setObjectName(QStringLiteral("approvalDetail"));
    detail->setTextInteractionFlags(Qt::TextSelectableByMouse);
    detail->setWordWrap(true);
    layout->addWidget(detail);

    auto* stateLabel = new QLabel(
        restoringTimeline_ ? tr("Expired approval") : tr("Waiting for your decision"), card);
    stateLabel->setObjectName(QStringLiteral("approvalStatus"));
    layout->addWidget(stateLabel);

    auto* actions = new QHBoxLayout();
    const QJsonArray availableDecisions =
        event.payload.value(QStringLiteral("availableDecisions")).toArray();
    const auto addDecision = [this, card, actions, requestId,
                              availableDecisions](const QString& text, const QString& objectName,
                                                  domain::ApprovalDecision decision) {
        auto* button = new QPushButton(text, card);
        button->setObjectName(objectName);
        button->setEnabled(!restoringTimeline_ &&
                           (availableDecisions.isEmpty() ||
                            availableDecisions.contains(domain::enumName(decision))));
        connect(button, &QPushButton::clicked, this, [this, requestId, decision] {
            QString error;
            if (!controller_->respondToApproval(requestId, decision, &error)) {
                statusBar()->showMessage(error, 5000);
            }
        });
        actions->addWidget(button);
        return button;
    };

    ApprovalCardState state;
    state.status = stateLabel;
    state.buttons = {addDecision(tr("Allow once"), QStringLiteral("approvalAcceptButton"),
                                 domain::ApprovalDecision::Accept),
                     addDecision(tr("Allow for session"), QStringLiteral("approvalSessionButton"),
                                 domain::ApprovalDecision::AcceptForSession),
                     addDecision(tr("Deny"), QStringLiteral("approvalDeclineButton"),
                                 domain::ApprovalDecision::Decline),
                     addDecision(tr("Cancel turn"), QStringLiteral("approvalCancelButton"),
                                 domain::ApprovalDecision::Cancel)};
    layout->addLayout(actions);

    approvalCards_.insert(requestId, state);
    item->setSizeHint(card->sizeHint());
    timeline_->setItemWidget(item, card);
}

void MainWindow::resolveApprovalCard(const domain::AgentEvent& event) {
    const QString requestId = event.payload.value(QStringLiteral("requestId")).toString();
    const auto iterator = approvalCards_.find(requestId);
    if (iterator == approvalCards_.end()) {
        return;
    }
    for (QPushButton* button : iterator->buttons) {
        button->setEnabled(false);
    }
    const QString decision = event.payload.value(QStringLiteral("decision")).toString();
    const QString resolution = event.payload.value(QStringLiteral("resolution")).toString();
    iterator->status->setText(decision.isEmpty() ? tr("Approval closed: %1").arg(resolution)
                                                 : tr("Decision sent: %1").arg(decision));
}

void MainWindow::appendUserInputRequest(const domain::AgentEvent& event) {
    const QString requestId = event.payload.value(QStringLiteral("requestId")).toString();
    const QJsonArray questions = event.payload.value(QStringLiteral("questions")).toArray();
    if (requestId.isEmpty() || questions.isEmpty() || userInputCards_.contains(requestId)) {
        return;
    }

    auto* item = new QListWidgetItem(timeline_);
    auto* card = new QFrame(timeline_);
    card->setObjectName(QStringLiteral("userInputCard"));
    auto* layout = new QVBoxLayout(card);
    layout->setContentsMargins(14, 12, 14, 12);
    auto* title = new QLabel(tr("Agent needs your input"), card);
    title->setObjectName(QStringLiteral("userInputTitle"));
    layout->addWidget(title);

    UserInputCardState state;
    for (const QJsonValue& value : questions) {
        const QJsonObject question = value.toObject();
        const QString questionId = question.value(QStringLiteral("id")).toString();
        auto* header = new QLabel(question.value(QStringLiteral("header")).toString(), card);
        header->setObjectName(QStringLiteral("userInputQuestionHeader"));
        auto* prompt = new QLabel(question.value(QStringLiteral("question")).toString(), card);
        prompt->setObjectName(QStringLiteral("userInputQuestion"));
        prompt->setWordWrap(true);
        layout->addWidget(header);
        layout->addWidget(prompt);

        QuestionInputState input{.questionId = questionId};
        const QJsonArray options = question.value(QStringLiteral("options")).toArray();
        const bool allowsOther = question.value(QStringLiteral("isOther")).toBool();
        const bool secret = question.value(QStringLiteral("isSecret")).toBool();
        if (!options.isEmpty()) {
            input.options = new QComboBox(card);
            input.options->setObjectName(QStringLiteral("userInputOption_%1").arg(questionId));
            for (const QJsonValue& optionValue : options) {
                const QJsonObject option = optionValue.toObject();
                input.options->addItem(option.value(QStringLiteral("label")).toString(),
                                       option.value(QStringLiteral("label")));
                const int optionIndex = input.options->count() - 1;
                input.options->setItemData(optionIndex,
                                           option.value(QStringLiteral("description")).toString(),
                                           Qt::ToolTipRole);
            }
            layout->addWidget(input.options);
            if (allowsOther) {
                input.options->addItem(tr("Other..."), QStringLiteral("__snack_other__"));
                input.text = new QLineEdit(card);
                input.text->setObjectName(QStringLiteral("userInputOther_%1").arg(questionId));
                input.text->setPlaceholderText(tr("Enter another answer"));
                input.text->setEchoMode(secret ? QLineEdit::Password : QLineEdit::Normal);
                input.text->hide();
                layout->addWidget(input.text);
                connect(input.options, &QComboBox::currentIndexChanged, input.text,
                        [combo = input.options, text = input.text](int) {
                            text->setVisible(combo->currentData().toString() ==
                                             QLatin1String("__snack_other__"));
                        });
            }
        } else {
            input.text = new QLineEdit(card);
            input.text->setObjectName(QStringLiteral("userInputText_%1").arg(questionId));
            input.text->setEchoMode(secret ? QLineEdit::Password : QLineEdit::Normal);
            layout->addWidget(input.text);
        }
        if (restoringTimeline_) {
            if (input.options != nullptr) {
                input.options->setEnabled(false);
            }
            if (input.text != nullptr) {
                input.text->setEnabled(false);
            }
        }
        state.questions.append(input);
    }

    state.status = new QLabel(
        restoringTimeline_ ? tr("Expired question") : tr("Waiting for your answer"), card);
    state.status->setObjectName(QStringLiteral("userInputStatus"));
    state.submit = new QPushButton(tr("Submit answers"), card);
    state.submit->setObjectName(QStringLiteral("userInputSubmitButton"));
    state.submit->setEnabled(!restoringTimeline_);
    layout->addWidget(state.status);
    layout->addWidget(state.submit, 0, Qt::AlignLeft);
    userInputCards_.insert(requestId, state);

    connect(state.submit, &QPushButton::clicked, this, [this, requestId] {
        const auto iterator = userInputCards_.find(requestId);
        if (iterator == userInputCards_.end()) {
            return;
        }
        QJsonObject answers;
        for (const QuestionInputState& input : iterator->questions) {
            QString answer;
            if (input.options != nullptr &&
                input.options->currentData().toString() != QLatin1String("__snack_other__")) {
                answer = input.options->currentData().toString();
            } else if (input.text != nullptr) {
                answer = input.text->text();
            }
            answers.insert(input.questionId,
                           QJsonObject{{QStringLiteral("answers"), QJsonArray{answer}}});
        }
        QString error;
        if (!controller_->respondToUserInput(requestId, answers, &error)) {
            statusBar()->showMessage(error, 5000);
        }
    });

    item->setSizeHint(card->sizeHint());
    timeline_->setItemWidget(item, card);
}

void MainWindow::resolveUserInputCard(const domain::AgentEvent& event) {
    const auto iterator =
        userInputCards_.find(event.payload.value(QStringLiteral("requestId")).toString());
    if (iterator == userInputCards_.end()) {
        return;
    }
    iterator->submit->setEnabled(false);
    for (const QuestionInputState& input : iterator->questions) {
        if (input.options != nullptr) {
            input.options->setEnabled(false);
        }
        if (input.text != nullptr) {
            input.text->clear();
            input.text->setEnabled(false);
        }
    }
    const QString resolution = event.payload.value(QStringLiteral("resolution")).toString();
    iterator->status->setText(resolution == QLatin1String("answered")
                                  ? tr("Answers sent")
                                  : tr("Question closed: %1").arg(resolution));
}

void MainWindow::updateUsage(const QJsonObject& payload) {
    const QJsonObject total = payload.value(QStringLiteral("total")).toObject();
    if (total.isEmpty()) {
        return;
    }
    const qint64 totalTokens = total.value(QStringLiteral("totalTokens")).toInteger();
    const QJsonValue contextValue = payload.value(QStringLiteral("modelContextWindow"));
    QString text = tr("Tokens: %1").arg(QLocale().toString(totalTokens));
    if (!contextValue.isNull() && contextValue.toInteger() > 0) {
        const qint64 contextWindow = contextValue.toInteger();
        text = tr("Tokens: %1 / %2 (%3%)")
                   .arg(QLocale().toString(totalTokens), QLocale().toString(contextWindow),
                        QString::number(100.0 * static_cast<double>(totalTokens) /
                                            static_cast<double>(contextWindow),
                                        'f', 1));
    }
    usageLabel_->setText(text);
    usageLabel_->setToolTip(
        tr("Input: %1\nCached input: %2\nOutput: %3\nReasoning output: %4")
            .arg(QLocale().toString(total.value(QStringLiteral("inputTokens")).toInteger()),
                 QLocale().toString(total.value(QStringLiteral("cachedInputTokens")).toInteger()),
                 QLocale().toString(total.value(QStringLiteral("outputTokens")).toInteger()),
                 QLocale().toString(
                     total.value(QStringLiteral("reasoningOutputTokens")).toInteger())));
    usageLabel_->show();
}

QString MainWindow::toolTitle(const QJsonObject& payload) const {
    const QString kind = payload.value(QStringLiteral("kind")).toString();
    if (kind == QLatin1String("commandExecution")) {
        return tr("Command");
    }
    if (kind == QLatin1String("fileChange")) {
        return tr("File changes");
    }
    if (kind == QLatin1String("mcpToolCall")) {
        return tr("MCP tool: %1 / %2")
            .arg(payload.value(QStringLiteral("server")).toString(),
                 payload.value(QStringLiteral("tool")).toString());
    }
    if (kind == QLatin1String("dynamicToolCall")) {
        return tr("Tool: %1").arg(payload.value(QStringLiteral("tool")).toString());
    }
    if (kind == QLatin1String("collabToolCall")) {
        return tr("Collaboration tool");
    }
    if (kind == QLatin1String("webSearch")) {
        return tr("Web search");
    }
    if (kind == QLatin1String("imageView")) {
        return tr("Image view");
    }
    if (kind == QLatin1String("contextCompaction")) {
        return tr("Context compaction");
    }
    return tr("Tool execution");
}

QString MainWindow::toolDetails(const QJsonObject& payload) const {
    QStringList details;
    const QString kind = payload.value(QStringLiteral("kind")).toString();
    if (kind == QLatin1String("commandExecution")) {
        details.append(payload.value(QStringLiteral("command")).toString());
        const QString cwd = payload.value(QStringLiteral("cwd")).toString();
        if (!cwd.isEmpty()) {
            details.append(tr("Working directory: %1").arg(cwd));
        }
    } else if (kind == QLatin1String("fileChange")) {
        for (const QJsonValue& value : payload.value(QStringLiteral("changes")).toArray()) {
            const QJsonObject change = value.toObject();
            QString changeKind = change.value(QStringLiteral("kind")).toString();
            if (changeKind.isEmpty()) {
                changeKind = change.value(QStringLiteral("kind"))
                                 .toObject()
                                 .value(QStringLiteral("type"))
                                 .toString();
            }
            details.append(QStringLiteral("%1 (%2)").arg(
                change.value(QStringLiteral("path")).toString(), changeKind));
        }
    } else if (kind == QLatin1String("mcpToolCall")) {
        details.append(
            tr("Arguments: %1").arg(compactJson(payload.value(QStringLiteral("arguments")))));
    } else if (kind == QLatin1String("dynamicToolCall")) {
        const QString nameSpace = payload.value(QStringLiteral("namespace")).toString();
        if (!nameSpace.isEmpty()) {
            details.append(tr("Namespace: %1").arg(nameSpace));
        }
        details.append(
            tr("Arguments: %1").arg(compactJson(payload.value(QStringLiteral("arguments")))));
    } else if (kind == QLatin1String("webSearch")) {
        details.append(payload.value(QStringLiteral("query")).toString());
    } else if (kind == QLatin1String("imageView")) {
        details.append(payload.value(QStringLiteral("path")).toString());
    } else {
        const QString prompt = payload.value(QStringLiteral("prompt")).toString();
        if (!prompt.isEmpty()) {
            details.append(prompt);
        }
    }
    details.removeAll(QString{});
    return details.join(QLatin1Char('\n'));
}

QString MainWindow::conversationStatusText(domain::ConversationStatus status) const {
    switch (status) {
    case domain::ConversationStatus::Dormant:
        return tr("Dormant");
    case domain::ConversationStatus::Connecting:
        return tr("Connecting");
    case domain::ConversationStatus::Idle:
        return tr("Idle");
    case domain::ConversationStatus::Running:
        return tr("Running");
    case domain::ConversationStatus::WaitingApproval:
        return tr("Waiting for approval");
    case domain::ConversationStatus::WaitingInput:
        return tr("Waiting for input");
    case domain::ConversationStatus::Disconnected:
        return tr("Disconnected");
    case domain::ConversationStatus::Failed:
        return tr("Failed");
    case domain::ConversationStatus::Closed:
        return tr("Closed");
    }
    return tr("Unknown");
}

void MainWindow::appendToolStarted(const domain::AgentEvent& event) {
    const QString itemId = event.payload.value(QStringLiteral("itemId")).toString();
    if (itemId.isEmpty() || toolCards_.contains(itemId)) {
        return;
    }

    auto* item = new QListWidgetItem(timeline_);
    auto* card = new QFrame(timeline_);
    card->setObjectName(QStringLiteral("toolCard"));
    auto* layout = new QVBoxLayout(card);
    layout->setContentsMargins(14, 12, 14, 12);
    auto* title = new QLabel(toolTitle(event.payload), card);
    title->setObjectName(QStringLiteral("toolTitle"));
    auto* detail = new QLabel(toolDetails(event.payload), card);
    detail->setObjectName(QStringLiteral("toolDetail"));
    detail->setWordWrap(true);
    detail->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto* output = new QPlainTextEdit(card);
    output->setObjectName(QStringLiteral("toolOutput"));
    output->setReadOnly(true);
    output->setMaximumHeight(150);
    output->hide();
    auto* status =
        new QLabel(event.payload.value(QStringLiteral("status")).toString(tr("Running")), card);
    status->setObjectName(QStringLiteral("toolStatus"));
    layout->addWidget(title);
    layout->addWidget(detail);
    layout->addWidget(output);
    layout->addWidget(status);
    toolCards_.insert(
        itemId, {.item = item, .card = card, .detail = detail, .status = status, .output = output});
    item->setSizeHint(card->sizeHint());
    timeline_->setItemWidget(item, card);
}

void MainWindow::appendToolProgress(const domain::AgentEvent& event) {
    const QString itemId = event.payload.value(QStringLiteral("itemId")).toString();
    const auto iterator = toolCards_.find(itemId);
    if (iterator == toolCards_.end()) {
        return;
    }
    if (event.payload.value(QStringLiteral("changes")).isArray()) {
        QJsonObject details = event.payload;
        details.insert(QStringLiteral("kind"), QStringLiteral("fileChange"));
        iterator->detail->setText(toolDetails(details));
    }
    const QString text = event.payload.value(QStringLiteral("text")).toString();
    if (!text.isEmpty()) {
        iterator->output->setPlainText(
            boundedText(iterator->output->toPlainText() + text, maximumToolOutput));
        iterator->output->show();
    }
    iterator->item->setSizeHint(iterator->card->sizeHint());
}

void MainWindow::completeTool(const domain::AgentEvent& event) {
    const QString itemId = event.payload.value(QStringLiteral("itemId")).toString();
    if (!toolCards_.contains(itemId)) {
        appendToolStarted(event);
    }
    const auto iterator = toolCards_.find(itemId);
    if (iterator == toolCards_.end()) {
        return;
    }
    iterator->detail->setText(toolDetails(event.payload));
    const QString status = event.payload.value(QStringLiteral("status")).toString();
    iterator->status->setText(status.isEmpty() ? tr("Completed") : status);

    QString output = event.payload.value(QStringLiteral("aggregatedOutput")).toString();
    if (output.isEmpty()) {
        output = compactJson(event.payload.value(QStringLiteral("result")));
    }
    if (output.isEmpty()) {
        output = compactJson(event.payload.value(QStringLiteral("error")));
    }
    if (output.isEmpty()) {
        output = compactJson(event.payload.value(QStringLiteral("contentItems")));
    }
    if (!output.isEmpty()) {
        iterator->output->setPlainText(boundedText(output, maximumToolOutput));
        iterator->output->show();
    }
    iterator->item->setSizeHint(iterator->card->sizeHint());
}

void MainWindow::appendReasoningStarted(const domain::AgentEvent& event) {
    const QString itemId = event.payload.value(QStringLiteral("itemId")).toString();
    if (itemId.isEmpty() || reasoningCards_.contains(itemId)) {
        return;
    }
    auto* item = new QListWidgetItem(timeline_);
    auto* card = new QFrame(timeline_);
    card->setObjectName(QStringLiteral("reasoningCard"));
    auto* layout = new QVBoxLayout(card);
    layout->setContentsMargins(14, 12, 14, 12);
    auto* title = new QLabel(tr("Reasoning summary"), card);
    auto* summary = new QLabel(card);
    summary->setObjectName(QStringLiteral("reasoningSummary"));
    summary->setWordWrap(true);
    summary->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto* status = new QLabel(tr("Thinking"), card);
    status->setObjectName(QStringLiteral("reasoningStatus"));
    layout->addWidget(title);
    layout->addWidget(summary);
    layout->addWidget(status);
    reasoningCards_.insert(itemId,
                           {.item = item, .card = card, .summary = summary, .status = status});
    item->setSizeHint(card->sizeHint());
    timeline_->setItemWidget(item, card);
}

void MainWindow::appendReasoningDelta(const domain::AgentEvent& event) {
    const QString itemId = event.payload.value(QStringLiteral("itemId")).toString();
    const auto iterator = reasoningCards_.find(itemId);
    if (iterator == reasoningCards_.end()) {
        return;
    }
    iterator->summary->setText(boundedText(
        iterator->summary->text() + event.payload.value(QStringLiteral("text")).toString(),
        maximumSummaryText));
    iterator->item->setSizeHint(iterator->card->sizeHint());
}

void MainWindow::completeReasoning(const domain::AgentEvent& event) {
    const QString itemId = event.payload.value(QStringLiteral("itemId")).toString();
    if (!reasoningCards_.contains(itemId)) {
        appendReasoningStarted(event);
    }
    const auto iterator = reasoningCards_.find(itemId);
    if (iterator == reasoningCards_.end()) {
        return;
    }
    QStringList summaryParts;
    for (const QJsonValue& value : event.payload.value(QStringLiteral("summary")).toArray()) {
        summaryParts.append(value.toString());
    }
    iterator->summary->setText(
        boundedText(summaryParts.join(QStringLiteral("\n\n")), maximumSummaryText));
    iterator->status->setText(tr("Completed"));
    iterator->item->setSizeHint(iterator->card->sizeHint());
}

void MainWindow::updatePlan(const domain::AgentEvent& event) {
    const QJsonValue planValue = event.payload.value(QStringLiteral("plan"));
    if (planValue.isArray()) {
        planList_->clear();
        for (const QJsonValue& value : planValue.toArray()) {
            const QJsonObject step = value.toObject();
            const QString status = step.value(QStringLiteral("status")).toString();
            const QString marker = status == QLatin1String("completed")    ? QStringLiteral("[x]")
                                   : status == QLatin1String("inProgress") ? QStringLiteral("[>]")
                                                                           : QStringLiteral("[ ]");
            auto* item = new QListWidgetItem(
                QStringLiteral("%1 %2").arg(marker, step.value(QStringLiteral("step")).toString()),
                planList_);
            item->setData(Qt::UserRole, status);
        }
        const QString explanation = event.payload.value(QStringLiteral("explanation")).toString();
        planExplanation_->setText(explanation.isEmpty() ? tr("Current plan") : explanation);
    }
    if (event.payload.contains(QStringLiteral("text"))) {
        streamedPlanText_ = event.payload.value(QStringLiteral("text")).toString();
    } else {
        streamedPlanText_.append(event.payload.value(QStringLiteral("textDelta")).toString());
    }
    if (!streamedPlanText_.isEmpty()) {
        streamedPlanText_ = boundedText(streamedPlanText_, maximumSummaryText);
        planItemText_->setText(streamedPlanText_);
        planItemText_->show();
    }
    if (planList_->count() > 0 || !streamedPlanText_.isEmpty()) {
        taskDock_->show();
    }
}

void MainWindow::applyTheme(const ThemeDefinition& theme) {
    qApp->setStyleSheet(theme.styleSheet());
}

void MainWindow::refreshSystemTheme() {
    if (settingsSnapshot_.themeMode != app::ThemeMode::System) {
        return;
    }
    const bool dark = qApp->styleHints()->colorScheme() == Qt::ColorScheme::Dark;
    applyTheme(dark ? ThemeDefinition::dark() : ThemeDefinition::light());
}

void MainWindow::applyInterfaceScale(double scale) {
    settingsSnapshot_.interfaceScale = std::clamp(scale, 0.8, 2.0);
    QFont font = qApp->font();
    font.setPointSizeF(10.0 * settingsSnapshot_.interfaceScale);
    qApp->setFont(font);
    statusBar()->showMessage(
        tr("Interface scale: %1%").arg(settingsSnapshot_.interfaceScale * 100.0, 0, 'f', 0), 1500);
}

void MainWindow::updateStatus(domain::ConversationStatus status) {
    statusLabel_->setText(domain::enumName(status));
    refreshConnectionNotice();
    const bool idle = status == domain::ConversationStatus::Idle;
    const bool active = status == domain::ConversationStatus::Running ||
                        status == domain::ConversationStatus::WaitingApproval ||
                        status == domain::ConversationStatus::WaitingInput;
    const bool canSteer = status == domain::ConversationStatus::Running &&
                          controller_->capabilities().supportsSteering;
    QString selectedMode = sendModeCombo_->currentData().toString();
    {
        const QSignalBlocker blocker(sendModeCombo_);
        sendModeCombo_->clear();
        if (canSteer) {
            sendModeCombo_->addItem(tr("Steer now"), QStringLiteral("steer"));
        }
        if (active) {
            sendModeCombo_->addItem(tr("Queue"), QStringLiteral("queue"));
        }
        const int selectedIndex = sendModeCombo_->findData(selectedMode);
        if (selectedIndex >= 0) {
            sendModeCombo_->setCurrentIndex(selectedIndex);
        }
    }
    sendModeCombo_->setVisible(active);
    sendButton_->setEnabled(idle || active);
    sendButton_->setText(!active ? tr("Send")
                         : sendModeCombo_->currentData().toString() == QLatin1String("steer")
                             ? tr("Steer")
                             : tr("Queue"));
    stopButton_->setVisible(active);
    stopButton_->setEnabled(active);
    updateQueueControls();
    if (!active) {
        for (auto iterator = approvalCards_.begin(); iterator != approvalCards_.end(); ++iterator) {
            for (QPushButton* button : iterator->buttons) {
                button->setEnabled(false);
            }
        }
        for (auto iterator = userInputCards_.begin(); iterator != userInputCards_.end();
             ++iterator) {
            iterator->submit->setEnabled(false);
            for (const QuestionInputState& input : iterator->questions) {
                if (input.options != nullptr) {
                    input.options->setEnabled(false);
                }
                if (input.text != nullptr) {
                    input.text->setEnabled(false);
                }
            }
        }
    }
}

void MainWindow::updateConnectionDetail(const QString& detail) {
    statusLabel_->setToolTip(detail);
    refreshConnectionNotice();
    if (!startupNotice_.isEmpty()) {
        statusBar()->showMessage(startupNotice_, 10000);
    } else if (!detail.isEmpty()) {
        statusBar()->showMessage(detail, 5000);
    }
}

void MainWindow::refreshConnectionNotice() {
    const domain::ConversationStatus status = controller_->status();
    const bool failed = status == domain::ConversationStatus::Disconnected ||
                        status == domain::ConversationStatus::Failed;
    const bool reconnecting = status == domain::ConversationStatus::Connecting &&
                              !controller_->connectionDetail().isEmpty();
    const bool fallback = !startupNotice_.isEmpty();
    if (!failed && !reconnecting && !fallback) {
        connectionNoticeFrame_->hide();
        reconnectButton_->hide();
        return;
    }

    connectionNoticeFrame_->setProperty("severity", failed ? QStringLiteral("danger")
                                                           : QStringLiteral("warning"));
    connectionNoticeTitle_->setText(failed         ? tr("Agent connection unavailable")
                                    : reconnecting ? tr("Reconnecting agent")
                                                   : tr("Agent fallback active"));
    connectionNoticeDetail_->setText((failed || reconnecting) ? controller_->connectionDetail()
                                                              : startupNotice_);
    reconnectButton_->setVisible(failed);
    connectionNoticeFrame_->style()->unpolish(connectionNoticeFrame_);
    connectionNoticeFrame_->style()->polish(connectionNoticeFrame_);
    connectionNoticeFrame_->show();
}

void MainWindow::updateConversationTitle(const QString& title) {
    titleLabel_->setText(title);
    sessionRow_->setText(tr("●  %1\n    %2").arg(agentDisplayName(), title));
    if (conversationList_ != nullptr) {
        refreshConversationList();
    }
}

void MainWindow::persistComposerDraft() {
    settings_->saveComposerDraft(controller_->conversation().id, composer_->toPlainText());
}

void MainWindow::rebuildCapabilityControls(const domain::TurnSettingsSnapshot& settings) {
    const QSignalBlocker modelBlocker(modelCombo_);
    const QSignalBlocker effortBlocker(effortCombo_);
    const QSignalBlocker accessBlocker(accessCombo_);
    const agent::CapabilitySet& capabilities = controller_->capabilities();

    modelCombo_->clear();
    for (const agent::ModelCapability& model : capabilities.modelCapabilities) {
        modelCombo_->addItem(model.displayName.isEmpty() ? model.id : model.displayName, model.id);
    }
    if (modelCombo_->count() == 0) {
        for (const QString& modelId : capabilities.models) {
            modelCombo_->addItem(modelId, modelId);
        }
    }
    modelCombo_->setCurrentIndex(modelCombo_->findData(settings.modelId));
    modelCombo_->setEnabled(modelCombo_->count() > 0);

    const auto selectedModel = std::find_if(
        capabilities.modelCapabilities.cbegin(), capabilities.modelCapabilities.cend(),
        [&settings](const agent::ModelCapability& model) { return model.id == settings.modelId; });
    effortCombo_->clear();
    if (selectedModel != capabilities.modelCapabilities.cend()) {
        for (const agent::ReasoningEffortCapability& effort :
             selectedModel->supportedReasoningEfforts) {
            const auto known = std::find_if(capabilities.reasoningEfforts.cbegin(),
                                            capabilities.reasoningEfforts.cend(),
                                            [&effort](domain::ReasoningEffort value) {
                                                return domain::enumName(value) == effort.id;
                                            });
            if (known != capabilities.reasoningEfforts.cend()) {
                effortCombo_->addItem(effort.id, static_cast<int>(*known));
            }
        }
    }
    if (effortCombo_->count() == 0) {
        for (domain::ReasoningEffort effort : capabilities.reasoningEfforts) {
            effortCombo_->addItem(domain::enumName(effort), static_cast<int>(effort));
        }
    }
    effortCombo_->setCurrentIndex(
        effortCombo_->findData(static_cast<int>(settings.reasoningEffort)));
    effortCombo_->setEnabled(effortCombo_->count() > 0);

    accessCombo_->clear();
    for (domain::AccessLevel access : capabilities.accessLevels) {
        QString name;
        switch (access) {
        case domain::AccessLevel::Strict:
            name = tr("Strict confirmation");
            break;
        case domain::AccessLevel::Workspace:
            name = tr("Workspace automatic");
            break;
        case domain::AccessLevel::Full:
            name = tr("Full automatic");
            break;
        }
        accessCombo_->addItem(name, static_cast<int>(access));
    }
    accessCombo_->setCurrentIndex(accessCombo_->findData(static_cast<int>(settings.accessLevel)));
    accessCombo_->setEnabled(accessCombo_->count() > 0);
}

void MainWindow::setPreferredAgent(domain::AgentKind kind) {
    settingsSnapshot_.preferredAgentKind = kind;
    settings_->save(settingsSnapshot_);
    statusBar()->showMessage(tr("Agent choice applies to the next conversation"), 5000);
}

QString MainWindow::agentDisplayName() const {
    switch (controller_->conversation().agentKind) {
    case domain::AgentKind::Codex:
        return tr("Codex");
    case domain::AgentKind::Claude:
        return tr("Claude");
    case domain::AgentKind::Mock:
        return tr("Mock Agent");
    }
    return tr("Agent");
}

void MainWindow::restoreTimeline() {
    QString error;
    const auto events = controller_->restoredEvents(&error);
    restoringTimeline_ = true;
    for (const auto& event : events) {
        appendEvent(event);
    }
    restoringTimeline_ = false;
    if (!error.isEmpty()) {
        statusBar()->showMessage(error, 8000);
    }
}

void MainWindow::buildTray() {
    if (!closeToTrayEnabled_) {
        return;
    }

    trayIcon_ = new QSystemTrayIcon(qApp->windowIcon(), this);
    trayIcon_->setToolTip(tr("Snack coding agent"));
    auto* menu = new QMenu(this);
    auto* openAction = menu->addAction(tr("Open Snack"));
    menu->addSeparator();
    auto* quitAction = menu->addAction(tr("Quit Snack"));
    trayIcon_->setContextMenu(menu);

    connect(openAction, &QAction::triggered, this,
            [this] { activateWindowForRequest(std::nullopt); });
    connect(quitAction, &QAction::triggered, this, &MainWindow::requestQuit);
    connect(trayIcon_, &QSystemTrayIcon::activated, this,
            [this](QSystemTrayIcon::ActivationReason reason) {
                if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick) {
                    activateWindowForRequest(std::nullopt);
                }
            });
    trayIcon_->show();
}

void MainWindow::persistWindowState() {
    persistComposerDraft();
    settingsSnapshot_.mainWindowGeometry = saveGeometry();
    settingsSnapshot_.mainWindowState = saveState(1);
    settings_->save(settingsSnapshot_);
}

void MainWindow::restoreWindowState() {
    if (!settingsSnapshot_.mainWindowGeometry.isEmpty()) {
        restoreGeometry(settingsSnapshot_.mainWindowGeometry);
    }
    if (!settingsSnapshot_.mainWindowState.isEmpty()) {
        restoreState(settingsSnapshot_.mainWindowState, 1);
    }
}

void MainWindow::ensureWindowVisible() {
    constexpr int minimumVisibleWidth = 128;
    constexpr int minimumVisibleHeight = 80;
    const QRect frame = frameGeometry();
    for (const QScreen* screen : QGuiApplication::screens()) {
        const QRect visible = frame.intersected(screen->availableGeometry());
        if (visible.width() >= minimumVisibleWidth && visible.height() >= minimumVisibleHeight) {
            return;
        }
    }

    const QScreen* screen = QGuiApplication::primaryScreen();
    if (screen == nullptr) {
        return;
    }
    const QRect available = screen->availableGeometry();
    move(available.center() - rect().center());
}

void MainWindow::shutdown() {
    if (shutdownComplete_) {
        return;
    }
    shutdownComplete_ = true;
    persistWindowState();
    controller_->close();
}

bool MainWindow::confirmQuit() {
    if (!hasActiveWork()) {
        return true;
    }
    QMessageBox prompt(QMessageBox::Warning, tr("Quit Snack?"),
                       tr("The active Agent task will be interrupted and will not be sent again "
                          "automatically."),
                       QMessageBox::NoButton, this);
    auto* quitButton = prompt.addButton(tr("Quit"), QMessageBox::AcceptRole);
    auto* cancelButton = prompt.addButton(QMessageBox::Cancel);
    prompt.setDefaultButton(cancelButton);
    prompt.setEscapeButton(cancelButton);
    prompt.exec();
    return prompt.clickedButton() == quitButton;
}

bool MainWindow::hasActiveWork() const {
    const auto isActive = [](domain::ConversationStatus status) {
        return status == domain::ConversationStatus::Connecting ||
               status == domain::ConversationStatus::Running ||
               status == domain::ConversationStatus::WaitingApproval ||
               status == domain::ConversationStatus::WaitingInput;
    };
    if (sessions_ == nullptr) {
        return isActive(controller_->status());
    }
    for (const QUuid& id : sessions_->conversationIds()) {
        const auto* openController = sessions_->controller(id);
        if (openController != nullptr && isActive(openController->status())) {
            return true;
        }
    }
    return false;
}

} // namespace snack::ui
