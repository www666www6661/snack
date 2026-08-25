#include "ui/MainWindow.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QDockWidget>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScreen>
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
    connect(qApp, &QCoreApplication::aboutToQuit, this, &MainWindow::shutdown);

    restoreTimeline();
    applyTheme(settingsSnapshot_.themeMode == app::ThemeMode::Dark ? ThemeDefinition::dark()
                                                                   : ThemeDefinition::light());
    applyInterfaceScale(settingsSnapshot_.interfaceScale);
    controller_->open();
    QTimer::singleShot(0, this, &MainWindow::ensureWindowVisible);
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
    auto* sessionRow = new QLabel(tr("●  Mock Agent\n    Project foundation"), sidebar);
    sessionRow->setContentsMargins(8, 14, 8, 14);
    sidebarLayout->addWidget(brand);
    sidebarLayout->addSpacing(10);
    sidebarLayout->addWidget(newConversation);
    sidebarLayout->addWidget(search);
    sidebarLayout->addWidget(sessionRow);
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
    modelCombo_ = new QComboBox(header);
    modelCombo_->setObjectName(QStringLiteral("modelCombo"));
    effortCombo_ = new QComboBox(header);
    effortCombo_->setObjectName(QStringLiteral("effortCombo"));
    accessCombo_ = new QComboBox(header);
    accessCombo_->setObjectName(QStringLiteral("accessCombo"));

    modelCombo_->addItem(tr("Mock Fast"), QStringLiteral("mock-fast"));
    modelCombo_->addItem(tr("Mock Balanced"), QStringLiteral("mock-balanced"));
    modelCombo_->setCurrentIndex(1);
    effortCombo_->addItem(tr("Low"), static_cast<int>(domain::ReasoningEffort::Low));
    effortCombo_->addItem(tr("Medium"), static_cast<int>(domain::ReasoningEffort::Medium));
    effortCombo_->addItem(tr("High"), static_cast<int>(domain::ReasoningEffort::High));
    effortCombo_->setCurrentIndex(1);
    accessCombo_->addItem(tr("Strict confirmation"), static_cast<int>(domain::AccessLevel::Strict));
    accessCombo_->addItem(tr("Workspace automatic"),
                          static_cast<int>(domain::AccessLevel::Workspace));
    accessCombo_->addItem(tr("Full automatic"), static_cast<int>(domain::AccessLevel::Full));

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
        timeline_->addItem(tr("Mock Agent\n"));
        activeAgentRow_ = timeline_->count() - 1;
        break;
    case domain::AgentEventType::AgentMessageDelta:
        if (activeAgentRow_ >= 0) {
            auto* item = timeline_->item(activeAgentRow_);
            item->setText(item->text() + event.payload.value(QStringLiteral("text")).toString());
        }
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
    sendButton_->setEnabled(idle);
    sendButton_->setText(status == domain::ConversationStatus::Running ? tr("Running")
                                                                       : tr("Send"));
}

void MainWindow::restoreTimeline() {
    QString error;
    const auto events = controller_->restoredEvents(&error);
    for (const auto& event : events) {
        appendEvent(event);
    }
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
