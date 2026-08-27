#include "ui/DesktopNotifier.h"

#include <QCoreApplication>
#include <QSystemTrayIcon>

namespace snack::ui {

SystemTrayDesktopNotifier::SystemTrayDesktopNotifier(QSystemTrayIcon* trayIcon)
    : trayIcon_(trayIcon) {}

void SystemTrayDesktopNotifier::show(DesktopNotificationKind kind) {
    if (trayIcon_ == nullptr || !trayIcon_->isVisible()) {
        return;
    }

    const QString message =
        kind == DesktopNotificationKind::TaskCompleted
            ? QCoreApplication::translate("DesktopNotifier", "A task completed")
            : QCoreApplication::translate("DesktopNotifier", "A task needs attention");
    trayIcon_->showMessage(QCoreApplication::translate("DesktopNotifier", "Snack"), message,
                           QSystemTrayIcon::Information, 5000);
}

} // namespace snack::ui
