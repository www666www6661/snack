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
#include <QJsonDocument>
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
#include <QSystemTrayIcon>
#include <QTimer>
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

} // namespace

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
