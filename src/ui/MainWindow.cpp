#include "ui/MainWindow.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QDockWidget>
#include <QFrame>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScreen>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

namespace snack::ui {

MainWindow::MainWindow(session::SessionController* controller, app::AppSettings* settings,
                       QWidget* parent)
    : MainWindow(controller, settings, QSystemTrayIcon::isSystemTrayAvailable(), parent) {}

MainWindow::MainWindow(session::SessionController* controller, app::AppSettings* settings,
                       bool closeToTrayEnabled, QWidget* parent)
    : QMainWindow(parent), controller_(controller), settings_(settings),
      settingsSnapshot_(settings_->load()), closeToTrayEnabled_(closeToTrayEnabled) {
    Q_ASSERT(controller_ != nullptr);
    Q_ASSERT(settings_ != nullptr);
    buildUi();
    buildMenus();
    restoreWindowState();
    buildTray();

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
            });
    connect(controller_, &session::SessionController::connectionDetailChanged, this,
            &MainWindow::updateConnectionDetail);
    connect(qApp, &QCoreApplication::aboutToQuit, this, &MainWindow::shutdown);

    restoreTimeline();
    applyTheme(settingsSnapshot_.themeMode == app::ThemeMode::Dark ? ThemeDefinition::dark()
                                                                   : ThemeDefinition::light());
    applyInterfaceScale(settingsSnapshot_.interfaceScale);
    rebuildCapabilityControls(controller_->nextTurnSettings());
    controller_->open();
    QTimer::singleShot(0, this, &MainWindow::ensureWindowVisible);
}

void MainWindow::showStartupNotice(const QString& notice) {
    if (!notice.trimmed().isEmpty()) {
        startupNotice_ = notice;
        sessionRow_->setToolTip(startupNotice_);
        statusBar()->showMessage(startupNotice_, 10000);
    }
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
    if (controller_->status() == domain::ConversationStatus::Running ||
        controller_->status() == domain::ConversationStatus::WaitingApproval ||
        controller_->status() == domain::ConversationStatus::WaitingInput) {
        controller_->interrupt();
        statusBar()->showMessage(tr("Stopping the current turn"), 3000);
        return;
    }
    QString error;
    if (!controller_->sendMessage(composer_->toPlainText(), &error)) {
        statusBar()->showMessage(error, 4000);
        return;
    }
    composer_->clear();
}

void MainWindow::updateSessionSettings() {
    auto snapshot = controller_->nextTurnSettings();
    snapshot.modelId = modelCombo_->currentData().toString();
    snapshot.reasoningEffort =
        static_cast<domain::ReasoningEffort>(effortCombo_->currentData().toInt());
    snapshot.accessLevel = static_cast<domain::AccessLevel>(accessCombo_->currentData().toInt());
    controller_->setNextTurnSettings(snapshot);
    if (controller_->status() == domain::ConversationStatus::Running) {
        statusBar()->showMessage(tr("Settings apply to the next message"), 3000);
    }
}

void MainWindow::applyLightTheme() {
    settingsSnapshot_.themeMode = app::ThemeMode::Light;
    applyTheme(ThemeDefinition::light());
}

void MainWindow::applyDarkTheme() {
    settingsSnapshot_.themeMode = app::ThemeMode::Dark;
    applyTheme(ThemeDefinition::dark());
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
    newConversation->setEnabled(false);
    auto* search = new QPlainTextEdit(sidebar);
    search->setPlaceholderText(tr("Search conversations"));
    search->setMaximumHeight(42);
    sessionRow_ = new QLabel(
        tr("●  %1\n    %2").arg(agentDisplayName(), controller_->conversation().title), sidebar);
    sessionRow_->setObjectName(QStringLiteral("sessionRow"));
    sessionRow_->setContentsMargins(8, 14, 8, 14);
    sidebarLayout->addWidget(brand);
    sidebarLayout->addSpacing(10);
    sidebarLayout->addWidget(newConversation);
    sidebarLayout->addWidget(search);
    sidebarLayout->addWidget(sessionRow_);
    sidebarLayout->addStretch();

    auto* conversation = new QWidget(central);
    auto* conversationLayout = new QVBoxLayout(conversation);
    conversationLayout->setContentsMargins(0, 0, 0, 12);
    conversationLayout->setSpacing(0);

    auto* header = new QFrame(conversation);
    header->setObjectName(QStringLiteral("sessionHeader"));
    auto* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(22, 12, 22, 12);
    titleLabel_ = new QLabel(controller_->conversation().title, header);
    statusLabel_ = new QLabel(tr("Dormant"), header);
    statusLabel_->setObjectName(QStringLiteral("statusLabel"));
    modelCombo_ = new QComboBox(header);
    modelCombo_->setObjectName(QStringLiteral("modelCombo"));
    effortCombo_ = new QComboBox(header);
    effortCombo_->setObjectName(QStringLiteral("effortCombo"));
    accessCombo_ = new QComboBox(header);
    accessCombo_->setObjectName(QStringLiteral("accessCombo"));

    headerLayout->addWidget(titleLabel_);
    headerLayout->addWidget(statusLabel_);
    headerLayout->addStretch();
    headerLayout->addWidget(modelCombo_);
    headerLayout->addWidget(effortCombo_);
    headerLayout->addWidget(accessCombo_);

    timeline_ = new QListWidget(conversation);
    timeline_->setObjectName(QStringLiteral("timeline"));
    timeline_->setWordWrap(true);
    timeline_->setSelectionMode(QAbstractItemView::NoSelection);

    auto* composerFrame = new QWidget(conversation);
    auto* composerLayout = new QHBoxLayout(composerFrame);
    composerLayout->setContentsMargins(70, 10, 70, 0);
    composer_ = new QPlainTextEdit(composerFrame);
    composer_->setObjectName(QStringLiteral("composer"));
    composer_->setPlaceholderText(tr("Ask the agent about this workspace..."));
    composer_->setMaximumHeight(120);
    sendButton_ = new QPushButton(tr("Send"), composerFrame);
    sendButton_->setObjectName(QStringLiteral("sendButton"));
    composerLayout->addWidget(composer_, 1);
    composerLayout->addWidget(sendButton_, 0, Qt::AlignBottom);

    conversationLayout->addWidget(header);
    conversationLayout->addWidget(timeline_, 1);
    conversationLayout->addWidget(composerFrame);
    rootLayout->addWidget(sidebar);
    rootLayout->addWidget(conversation, 1);
    setCentralWidget(central);

    auto* taskDock = new QDockWidget(tr("Tasks"), this);
    taskDock->setObjectName(QStringLiteral("taskDock"));
    taskDock->setWidget(new QLabel(tr("Agent plans will appear here."), taskDock));
    addDockWidget(Qt::RightDockWidgetArea, taskDock);
    taskDock->hide();

    auto* terminalDock = new QDockWidget(tr("Terminal"), this);
    terminalDock->setObjectName(QStringLiteral("terminalDock"));
    terminalDock->setWidget(
        new QLabel(tr("Terminal integration is scheduled for M4."), terminalDock));
    addDockWidget(Qt::BottomDockWidgetArea, terminalDock);
    terminalDock->hide();

    connect(sendButton_, &QPushButton::clicked, this, &MainWindow::sendMessage);
    connect(modelCombo_, &QComboBox::currentIndexChanged, this, &MainWindow::updateSessionSettings);
    connect(effortCombo_, &QComboBox::currentIndexChanged, this,
            &MainWindow::updateSessionSettings);
    connect(accessCombo_, &QComboBox::currentIndexChanged, this,
            &MainWindow::updateSessionSettings);
}

void MainWindow::buildMenus() {
    auto* fileMenu = menuBar()->addMenu(tr("File"));
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
    auto* lightAction = viewMenu->addAction(tr("Light theme"));
    auto* darkAction = viewMenu->addAction(tr("Dark theme"));
    viewMenu->addSeparator();
    auto* zoomIn = viewMenu->addAction(tr("Zoom in"));
    auto* zoomOut = viewMenu->addAction(tr("Zoom out"));
    auto* resetZoom = viewMenu->addAction(tr("Actual size"));
    zoomIn->setShortcut(QKeySequence::ZoomIn);
    zoomOut->setShortcut(QKeySequence::ZoomOut);
    resetZoom->setShortcut(
        QKeySequence::fromString(QStringLiteral("Ctrl+0"), QKeySequence::PortableText));
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
    case domain::AgentEventType::ApprovalRequested:
        appendApprovalRequest(event);
        break;
    case domain::AgentEventType::ApprovalResolved:
        resolveApprovalCard(event);
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

void MainWindow::applyTheme(const ThemeDefinition& theme) {
    qApp->setStyleSheet(theme.styleSheet());
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
    const bool idle = status == domain::ConversationStatus::Idle;
    const bool active = status == domain::ConversationStatus::Running ||
                        status == domain::ConversationStatus::WaitingApproval ||
                        status == domain::ConversationStatus::WaitingInput;
    sendButton_->setEnabled(idle || active);
    sendButton_->setText(active ? tr("Stop") : tr("Send"));
    if (!active) {
        for (auto iterator = approvalCards_.begin(); iterator != approvalCards_.end(); ++iterator) {
            for (QPushButton* button : iterator->buttons) {
                button->setEnabled(false);
            }
        }
    }
}

void MainWindow::updateConnectionDetail(const QString& detail) {
    statusLabel_->setToolTip(detail);
    if (!startupNotice_.isEmpty()) {
        statusBar()->showMessage(startupNotice_, 10000);
    } else if (!detail.isEmpty()) {
        statusBar()->showMessage(detail, 5000);
    }
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
    switch (controller_->status()) {
    case domain::ConversationStatus::Connecting:
    case domain::ConversationStatus::Running:
    case domain::ConversationStatus::WaitingApproval:
    case domain::ConversationStatus::WaitingInput:
        return true;
    default:
        return false;
    }
}

} // namespace snack::ui
