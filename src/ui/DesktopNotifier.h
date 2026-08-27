#pragma once

class QSystemTrayIcon;

namespace snack::ui {

enum class DesktopNotificationKind {
    TaskCompleted,
    TaskNeedsAttention,
};

class IDesktopNotifier {
  public:
    virtual ~IDesktopNotifier() = default;
    virtual void show(DesktopNotificationKind kind) = 0;
};

class SystemTrayDesktopNotifier final : public IDesktopNotifier {
  public:
    explicit SystemTrayDesktopNotifier(QSystemTrayIcon* trayIcon);

    void show(DesktopNotificationKind kind) override;

  private:
    QSystemTrayIcon* trayIcon_{nullptr};
};

} // namespace snack::ui
