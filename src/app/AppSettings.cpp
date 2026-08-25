#include "app/AppSettings.h"

#include <algorithm>
#include <utility>

namespace snack::app {

AppSettings::AppSettings() : AppSettings(std::make_unique<QSettings>()) {}

AppSettings::AppSettings(const QString& iniFilePath)
    : AppSettings(std::make_unique<QSettings>(iniFilePath, QSettings::IniFormat)) {}

AppSettings::AppSettings(std::unique_ptr<QSettings> settings) : settings_(std::move(settings)) {}

AppSettingsSnapshot AppSettings::load() const {
    AppSettingsSnapshot result;
    result.themeMode = themeModeFromString(
        settings_->value(QStringLiteral("appearance/theme"), QStringLiteral("system")).toString());
    result.locale =
        settings_->value(QStringLiteral("appearance/locale"), QStringLiteral("system")).toString();
    result.interfaceScale = std::clamp(
        settings_->value(QStringLiteral("appearance/interfaceScale"), 1.0).toDouble(), 0.8, 2.0);
    result.lastWorkspace = settings_->value(QStringLiteral("session/lastWorkspace")).toString();
    result.lastConversationId =
        settings_->value(QStringLiteral("session/lastConversationId")).toString();
    return result;
}

void AppSettings::save(const AppSettingsSnapshot& snapshot) {
    settings_->setValue(QStringLiteral("appearance/theme"), themeModeName(snapshot.themeMode));
    settings_->setValue(QStringLiteral("appearance/locale"), snapshot.locale);
    settings_->setValue(QStringLiteral("appearance/interfaceScale"),
                        std::clamp(snapshot.interfaceScale, 0.8, 2.0));
    settings_->setValue(QStringLiteral("session/lastWorkspace"), snapshot.lastWorkspace);
    settings_->setValue(QStringLiteral("session/lastConversationId"), snapshot.lastConversationId);
    settings_->sync();
}

QString themeModeName(ThemeMode mode) {
    switch (mode) {
    case ThemeMode::System:
        return QStringLiteral("system");
    case ThemeMode::Light:
        return QStringLiteral("light");
    case ThemeMode::Dark:
        return QStringLiteral("dark");
    }
    return QStringLiteral("system");
}

ThemeMode themeModeFromString(const QString& value) {
    if (value == QLatin1String("light")) {
        return ThemeMode::Light;
    }
    if (value == QLatin1String("dark")) {
        return ThemeMode::Dark;
    }
    return ThemeMode::System;
}

} // namespace snack::app
