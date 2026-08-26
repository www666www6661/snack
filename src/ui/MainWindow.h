#pragma once

#include "app/AppSettings.h"
#include "session/SessionController.h"
#include "ui/ThemeDefinition.h"

#include <QMainWindow>

#include <optional>

class QComboBox;
class QLabel;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QSystemTrayIcon;

namespace snack::ui {

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
    void applyTheme(const ThemeDefinition& theme);
    void applyInterfaceScale(double scale);
    void updateStatus(domain::ConversationStatus status);
    void updateConnectionDetail(const QString& detail);
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
    QPlainTextEdit* composer_{nullptr};
    QPushButton* sendButton_{nullptr};
    QLabel* statusLabel_{nullptr};
    QLabel* titleLabel_{nullptr};
    QLabel* sessionRow_{nullptr};
    QComboBox* modelCombo_{nullptr};
    QComboBox* effortCombo_{nullptr};
    QComboBox* accessCombo_{nullptr};
    QSystemTrayIcon* trayIcon_{nullptr};
    QString startupNotice_;
    int activeAgentRow_{-1};
    bool closeToTrayEnabled_{false};
    bool quitRequested_{false};
    bool shutdownComplete_{false};
};

} // namespace snack::ui
