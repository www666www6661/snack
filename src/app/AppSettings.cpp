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
    result.preferredAgentKind = preferredAgentKindFromString(
        settings_->value(QStringLiteral("agent/preferred"), QStringLiteral("codex")).toString());
    result.codexExecutable = settings_->value(QStringLiteral("agent/codexExecutable")).toString();
    result.mainWindowGeometry =
        settings_->value(QStringLiteral("window/mainGeometry")).toByteArray();
    result.mainWindowState = settings_->value(QStringLiteral("window/mainState")).toByteArray();
    return result;
}

void AppSettings::save(const AppSettingsSnapshot& snapshot) {
    settings_->setValue(QStringLiteral("appearance/theme"), themeModeName(snapshot.themeMode));
    settings_->setValue(QStringLiteral("appearance/locale"), snapshot.locale);
    settings_->setValue(QStringLiteral("appearance/interfaceScale"),
                        std::clamp(snapshot.interfaceScale, 0.8, 2.0));
    settings_->setValue(QStringLiteral("session/lastWorkspace"), snapshot.lastWorkspace);
    settings_->setValue(QStringLiteral("session/lastConversationId"), snapshot.lastConversationId);
    settings_->setValue(QStringLiteral("agent/preferred"),
                        domain::enumName(snapshot.preferredAgentKind));
    settings_->setValue(QStringLiteral("agent/codexExecutable"), snapshot.codexExecutable);
    settings_->setValue(QStringLiteral("window/mainGeometry"), snapshot.mainWindowGeometry);
    settings_->setValue(QStringLiteral("window/mainState"), snapshot.mainWindowState);
    settings_->sync();
}

QString AppSettings::composerDraft(const QUuid& conversationId) const {
    if (conversationId.isNull()) {
        return {};
    }
    return settings_
        ->value(
            QStringLiteral("composerDrafts/%1").arg(conversationId.toString(QUuid::WithoutBraces)))
        .toString();
}

void AppSettings::saveComposerDraft(const QUuid& conversationId, const QString& draft) {
    if (conversationId.isNull()) {
        return;
    }
    const QString key =
        QStringLiteral("composerDrafts/%1").arg(conversationId.toString(QUuid::WithoutBraces));
    if (draft.isEmpty()) {
        settings_->remove(key);
    } else {
        settings_->setValue(key, draft);
    }
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

domain::AgentKind preferredAgentKindFromString(const QString& value) {
    if (value == QLatin1String("mock")) {
        return domain::AgentKind::Mock;
    }
    if (value == QLatin1String("claude")) {
        return domain::AgentKind::Claude;
    }
    return domain::AgentKind::Codex;
}

} // namespace snack::app
