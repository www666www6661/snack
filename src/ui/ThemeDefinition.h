#pragma once

#include <QColor>
#include <QHash>
#include <QJsonObject>
#include <QString>

#include <optional>

namespace snack::ui {

struct ThemeDefinition {
    int schemaVersion{1};
    QString name;
    QString author;
    QString baseMode{QStringLiteral("light")};
    QHash<QString, QColor> colors;

    [[nodiscard]] static ThemeDefinition light();
    [[nodiscard]] static ThemeDefinition dark();
    [[nodiscard]] static std::optional<ThemeDefinition> fromJson(const QJsonObject& object,
                                                                 QString* error);
    [[nodiscard]] QString styleSheet() const;
};

} // namespace snack::ui
