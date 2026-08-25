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

namespace snack::ui {

class MainWindow final : public QMainWindow {
    Q_OBJECT

  public:
    MainWindow(session::SessionController* controller, app::AppSettings* settings,
               QWidget* parent = nullptr);

    void activateWindowForRequest(const std::optional<QString>& directory);

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

  private:
    void buildUi();
    void buildMenus();
    void appendEvent(const domain::AgentEvent& event);
    void applyTheme(const ThemeDefinition& theme);
    void applyInterfaceScale(double scale);
    void updateStatus(domain::ConversationStatus status);
    void restoreTimeline();

    session::SessionController* controller_{nullptr};
    app::AppSettings* settings_{nullptr};
    app::AppSettingsSnapshot settingsSnapshot_;
    QListWidget* timeline_{nullptr};
    QPlainTextEdit* composer_{nullptr};
    QPushButton* sendButton_{nullptr};
    QLabel* statusLabel_{nullptr};
    QLabel* titleLabel_{nullptr};
    QComboBox* modelCombo_{nullptr};
    QComboBox* effortCombo_{nullptr};
    QComboBox* accessCombo_{nullptr};
    int activeAgentRow_{-1};
};

} // namespace snack::ui
