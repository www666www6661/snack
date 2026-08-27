#pragma once

#include "app/AppSettings.h"
#include "session/SessionController.h"
#include "ui/ThemeDefinition.h"

#include <QHash>
#include <QMainWindow>

#include <optional>

class QComboBox;
class QDockWidget;
class QFrame;
class QLabel;
class QLineEdit;
class QListWidgetItem;
class QListWidget;
class QMenu;
class QPlainTextEdit;
class QPushButton;
class QSystemTrayIcon;
class QTimer;

namespace snack::ui {

class ComposerTextEdit;

class MainWindow final : public QMainWindow {
    Q_OBJECT

  public:
    MainWindow(session::SessionController* controller, app::AppSettings* settings,
               QWidget* parent = nullptr);
    MainWindow(session::SessionController* controller, app::AppSettings* settings,
               bool closeToTrayEnabled, QWidget* parent = nullptr);

    void activateWindowForRequest(const std::optional<QString>& directory);
    void showStartupNotice(const QString& notice);

  protected:
    void closeEvent(QCloseEvent* event) override;

  private slots:
    void sendMessage();
    void queueComposerMessage();
    void stopTurn();
    void reconnectSession();
    void updateQueuedMessages(const QList<domain::QueuedMessage>& messages);
    void updateQueueControls();
    void editQueuedMessage(QListWidgetItem* item);
    void moveQueuedMessageUp();
    void moveQueuedMessageDown();
    void sendQueuedMessageNow();
    void cancelQueuedMessage();
    void showPromptTemplateMenu();
    void saveComposerAsTemplate();
    void updateSessionSettings();
    void applyLightTheme();
    void applyDarkTheme();
    void increaseScale();
    void decreaseScale();
    void resetScale();
    void requestQuit();
    void preferCodexAgent();
    void preferMockAgent();

  private:
    void buildUi();
    void buildMenus();
    void appendEvent(const domain::AgentEvent& event);
    void appendApprovalRequest(const domain::AgentEvent& event);
    void resolveApprovalCard(const domain::AgentEvent& event);
    void appendUserInputRequest(const domain::AgentEvent& event);
    void resolveUserInputCard(const domain::AgentEvent& event);
    void updateUsage(const QJsonObject& payload);
    void appendToolStarted(const domain::AgentEvent& event);
    void appendToolProgress(const domain::AgentEvent& event);
    void completeTool(const domain::AgentEvent& event);
    void appendReasoningStarted(const domain::AgentEvent& event);
    void appendReasoningDelta(const domain::AgentEvent& event);
    void completeReasoning(const domain::AgentEvent& event);
    void updatePlan(const domain::AgentEvent& event);
    [[nodiscard]] QString toolTitle(const QJsonObject& payload) const;
    [[nodiscard]] QString toolDetails(const QJsonObject& payload) const;
    void applyTheme(const ThemeDefinition& theme);
    void applyInterfaceScale(double scale);
    void updateStatus(domain::ConversationStatus status);
    void updateConnectionDetail(const QString& detail);
    void refreshConnectionNotice();
    void updateConversationTitle(const QString& title);
    void persistComposerDraft();
    void rebuildPromptTemplateMenu();
    void insertPromptTemplate(const QUuid& templateId);
    void removePromptTemplate(const QUuid& templateId);
    void rebuildCapabilityControls(const domain::TurnSettingsSnapshot& settings);
    void setPreferredAgent(domain::AgentKind kind);
    [[nodiscard]] QString agentDisplayName() const;
    void restoreTimeline();
    void buildTray();
    void persistWindowState();
    void restoreWindowState();
    void ensureWindowVisible();
    void shutdown();
    [[nodiscard]] bool confirmQuit();
    [[nodiscard]] bool hasActiveWork() const;

    session::SessionController* controller_{nullptr};
    app::AppSettings* settings_{nullptr};
    app::AppSettingsSnapshot settingsSnapshot_;
    QListWidget* timeline_{nullptr};
    ComposerTextEdit* composer_{nullptr};
    QTimer* draftSaveTimer_{nullptr};
    QPushButton* sendButton_{nullptr};
    QPushButton* stopButton_{nullptr};
    QPushButton* reconnectButton_{nullptr};
    QPushButton* templateButton_{nullptr};
    QMenu* templateMenu_{nullptr};
    QFrame* queueFrame_{nullptr};
    QListWidget* queueList_{nullptr};
    QComboBox* sendModeCombo_{nullptr};
    QPushButton* queueUpButton_{nullptr};
    QPushButton* queueDownButton_{nullptr};
    QPushButton* queueSendNowButton_{nullptr};
    QPushButton* queueRemoveButton_{nullptr};
    QLabel* statusLabel_{nullptr};
    QFrame* connectionNoticeFrame_{nullptr};
    QLabel* connectionNoticeTitle_{nullptr};
    QLabel* connectionNoticeDetail_{nullptr};
    QLabel* titleLabel_{nullptr};
    QLabel* usageLabel_{nullptr};
    QLabel* sessionRow_{nullptr};
    QComboBox* modelCombo_{nullptr};
    QComboBox* effortCombo_{nullptr};
    QComboBox* accessCombo_{nullptr};
    QSystemTrayIcon* trayIcon_{nullptr};
    QDockWidget* taskDock_{nullptr};
    QLabel* planExplanation_{nullptr};
    QLabel* planItemText_{nullptr};
    QListWidget* planList_{nullptr};
    QString startupNotice_;
    struct ApprovalCardState {
        QLabel* status{nullptr};
        QList<QPushButton*> buttons;
    };
    struct ToolCardState {
        QListWidgetItem* item{nullptr};
        QFrame* card{nullptr};
        QLabel* detail{nullptr};
        QLabel* status{nullptr};
        QPlainTextEdit* output{nullptr};
    };
    struct QuestionInputState {
        QString questionId;
        QComboBox* options{nullptr};
        QLineEdit* text{nullptr};
    };
    struct UserInputCardState {
        QLabel* status{nullptr};
        QPushButton* submit{nullptr};
        QList<QuestionInputState> questions;
    };
    struct ReasoningCardState {
        QListWidgetItem* item{nullptr};
        QFrame* card{nullptr};
        QLabel* summary{nullptr};
        QLabel* status{nullptr};
    };
    QHash<QString, ApprovalCardState> approvalCards_;
    QHash<QString, UserInputCardState> userInputCards_;
    QHash<QString, ToolCardState> toolCards_;
    QHash<QString, ReasoningCardState> reasoningCards_;
    QString streamedPlanText_;
    int activeAgentRow_{-1};
    bool restoringTimeline_{false};
    bool closeToTrayEnabled_{false};
    bool quitRequested_{false};
    bool shutdownComplete_{false};
};

} // namespace snack::ui
