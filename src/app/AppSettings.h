#pragma once

#include <QByteArray>
#include <QSettings>

#include <memory>

namespace snack::app {

enum class ThemeMode { System, Light, Dark };

struct AppSettingsSnapshot {
    ThemeMode themeMode{ThemeMode::System};
    QString locale{QStringLiteral("system")};
    double interfaceScale{1.0};
    QString lastWorkspace;
    QString lastConversationId;
    QByteArray mainWindowGeometry;
    QByteArray mainWindowState;
};

class AppSettings final {
  public:
    AppSettings();
    explicit AppSettings(const QString& iniFilePath);

    [[nodiscard]] AppSettingsSnapshot load() const;
    void save(const AppSettingsSnapshot& snapshot);

  private:
    explicit AppSettings(std::unique_ptr<QSettings> settings);
    std::unique_ptr<QSettings> settings_;
};

[[nodiscard]] QString themeModeName(ThemeMode mode);
[[nodiscard]] ThemeMode themeModeFromString(const QString& value);

} // namespace snack::app
