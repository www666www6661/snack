#include "ui/ThemeDefinition.h"

#include <QJsonValue>
#include <QStringList>

namespace snack::ui {
namespace {

const QStringList colorTokens = {
    QStringLiteral("surface.canvas"),     QStringLiteral("surface.sidebar"),
    QStringLiteral("surface.raised"),     QStringLiteral("surface.overlay"),
    QStringLiteral("text.primary"),       QStringLiteral("text.secondary"),
    QStringLiteral("text.disabled"),      QStringLiteral("text.link"),
    QStringLiteral("border.subtle"),      QStringLiteral("border.strong"),
    QStringLiteral("focus.ring"),         QStringLiteral("state.running"),
    QStringLiteral("state.waiting"),      QStringLiteral("state.success"),
    QStringLiteral("state.warning"),      QStringLiteral("state.danger"),
    QStringLiteral("message.user"),       QStringLiteral("message.tool"),
    QStringLiteral("message.reasoning"),  QStringLiteral("diff.added"),
    QStringLiteral("diff.removed"),       QStringLiteral("diff.modified"),
    QStringLiteral("diff.conflict"),      QStringLiteral("terminal.background"),
    QStringLiteral("terminal.foreground")};

void setError(QString* error, const QString& value) {
    if (error != nullptr) {
        *error = value;
    }
}

QColor color(const ThemeDefinition& theme, const QString& token) {
    return theme.colors.value(token, QColor(QStringLiteral("#ff00ff")));
}

} // namespace

ThemeDefinition ThemeDefinition::light() {
    ThemeDefinition theme;
    theme.name = QStringLiteral("Snack Light");
    theme.author = QStringLiteral("Snack");
    theme.baseMode = QStringLiteral("light");
    theme.colors = {{QStringLiteral("surface.canvas"), QColor(QStringLiteral("#f7f6f2"))},
                    {QStringLiteral("surface.sidebar"), QColor(QStringLiteral("#efeee9"))},
                    {QStringLiteral("surface.raised"), QColor(QStringLiteral("#ffffff"))},
                    {QStringLiteral("surface.overlay"), QColor(QStringLiteral("#fffefa"))},
                    {QStringLiteral("text.primary"), QColor(QStringLiteral("#262622"))},
                    {QStringLiteral("text.secondary"), QColor(QStringLiteral("#6d6b63"))},
                    {QStringLiteral("text.disabled"), QColor(QStringLiteral("#aaa79e"))},
                    {QStringLiteral("text.link"), QColor(QStringLiteral("#176b57"))},
                    {QStringLiteral("border.subtle"), QColor(QStringLiteral("#dedcd4"))},
                    {QStringLiteral("border.strong"), QColor(QStringLiteral("#bdb9ae"))},
                    {QStringLiteral("focus.ring"), QColor(QStringLiteral("#16866b"))},
                    {QStringLiteral("state.running"), QColor(QStringLiteral("#137c62"))},
                    {QStringLiteral("state.waiting"), QColor(QStringLiteral("#a66b14"))},
                    {QStringLiteral("state.success"), QColor(QStringLiteral("#237a45"))},
                    {QStringLiteral("state.warning"), QColor(QStringLiteral("#a65e0c"))},
                    {QStringLiteral("state.danger"), QColor(QStringLiteral("#b43b35"))},
                    {QStringLiteral("message.user"), QColor(QStringLiteral("#e8eee9"))},
                    {QStringLiteral("message.tool"), QColor(QStringLiteral("#f1f0eb"))},
                    {QStringLiteral("message.reasoning"), QColor(QStringLiteral("#f3f0e7"))},
                    {QStringLiteral("diff.added"), QColor(QStringLiteral("#dcefe2"))},
                    {QStringLiteral("diff.removed"), QColor(QStringLiteral("#f5deda"))},
                    {QStringLiteral("diff.modified"), QColor(QStringLiteral("#f3e9c8"))},
                    {QStringLiteral("diff.conflict"), QColor(QStringLiteral("#f0d8b8"))},
                    {QStringLiteral("terminal.background"), QColor(QStringLiteral("#1d1f1d"))},
                    {QStringLiteral("terminal.foreground"), QColor(QStringLiteral("#e9ebe7"))}};
    return theme;
}

ThemeDefinition ThemeDefinition::dark() {
    ThemeDefinition theme = light();
    theme.name = QStringLiteral("Snack Dark");
    theme.baseMode = QStringLiteral("dark");
    theme.colors.insert(QStringLiteral("surface.canvas"), QColor(QStringLiteral("#171917")));
    theme.colors.insert(QStringLiteral("surface.sidebar"), QColor(QStringLiteral("#1e211f")));
    theme.colors.insert(QStringLiteral("surface.raised"), QColor(QStringLiteral("#252825")));
    theme.colors.insert(QStringLiteral("surface.overlay"), QColor(QStringLiteral("#2c302c")));
    theme.colors.insert(QStringLiteral("text.primary"), QColor(QStringLiteral("#eceee9")));
    theme.colors.insert(QStringLiteral("text.secondary"), QColor(QStringLiteral("#a7aaa3")));
    theme.colors.insert(QStringLiteral("text.disabled"), QColor(QStringLiteral("#70746e")));
    theme.colors.insert(QStringLiteral("text.link"), QColor(QStringLiteral("#70c9ad")));
    theme.colors.insert(QStringLiteral("border.subtle"), QColor(QStringLiteral("#343934")));
    theme.colors.insert(QStringLiteral("border.strong"), QColor(QStringLiteral("#525852")));
    theme.colors.insert(QStringLiteral("focus.ring"), QColor(QStringLiteral("#58b99b")));
    theme.colors.insert(QStringLiteral("message.user"), QColor(QStringLiteral("#26362f")));
    theme.colors.insert(QStringLiteral("message.tool"), QColor(QStringLiteral("#222622")));
    theme.colors.insert(QStringLiteral("message.reasoning"), QColor(QStringLiteral("#2b2922")));
    return theme;
}

std::optional<ThemeDefinition> ThemeDefinition::fromJson(const QJsonObject& object,
                                                         QString* error) {
    if (object.value(QStringLiteral("schemaVersion")).toInt(-1) != 1) {
        setError(error, QStringLiteral("Unsupported or missing theme schemaVersion"));
        return std::nullopt;
    }

    const QString baseMode = object.value(QStringLiteral("baseMode")).toString();
    if (baseMode != QLatin1String("light") && baseMode != QLatin1String("dark")) {
        setError(error, QStringLiteral("baseMode must be light or dark"));
        return std::nullopt;
    }

    const QString name = object.value(QStringLiteral("name")).toString().trimmed();
    if (name.isEmpty() || name.size() > 80) {
        setError(error, QStringLiteral("Theme name must contain 1 to 80 characters"));
        return std::nullopt;
    }

    for (const auto& forbidden :
         {QStringLiteral("url"), QStringLiteral("qss"), QStringLiteral("css"),
          QStringLiteral("javascript"), QStringLiteral("icons"), QStringLiteral("files")}) {
        if (object.contains(forbidden)) {
            setError(error, QStringLiteral("Theme contains forbidden field: %1").arg(forbidden));
            return std::nullopt;
        }
    }

    ThemeDefinition theme =
        baseMode == QLatin1String("dark") ? ThemeDefinition::dark() : ThemeDefinition::light();
    theme.name = name;
    theme.author = object.value(QStringLiteral("author")).toString().left(80);
    const QJsonValue colorsValue = object.value(QStringLiteral("colors"));
    if (!colorsValue.isUndefined() && !colorsValue.isObject()) {
        setError(error, QStringLiteral("colors must be an object"));
        return std::nullopt;
    }

    const QJsonObject colors = colorsValue.toObject();
    for (auto it = colors.constBegin(); it != colors.constEnd(); ++it) {
        if (!colorTokens.contains(it.key())) {
            setError(error, QStringLiteral("Unknown color token: %1").arg(it.key()));
            return std::nullopt;
        }
        if (!it.value().isString()) {
            setError(error, QStringLiteral("Color token must be a string: %1").arg(it.key()));
            return std::nullopt;
        }
        const QColor parsed(it.value().toString());
        if (!parsed.isValid() || parsed.alpha() != 255) {
            setError(error,
                     QStringLiteral("Color must be an opaque valid color: %1").arg(it.key()));
            return std::nullopt;
        }
        theme.colors.insert(it.key(), parsed);
    }
    return theme;
}

QString ThemeDefinition::styleSheet() const {
    return QStringLiteral(R"(
QMainWindow, QWidget#centralWorkbench { background: %1; color: %2; }
QWidget#sessionSidebar { background: %3; border-right: 1px solid %4; }
QFrame#sessionHeader { background: %1; border-bottom: 1px solid %4; }
QListWidget#timeline { background: %1; border: none; outline: none; }
QListWidget#timeline::item { padding: 9px 12px; margin: 3px 18px; border-radius: 8px; }
QListWidget#timeline::item:selected { background: %5; color: %2; }
QPlainTextEdit#composer { background: %6; border: 1px solid %4; border-radius: 12px; padding: 10px; }
QPushButton#sendButton { background: %7; color: white; border: none; border-radius: 9px; padding: 8px 18px; }
QPushButton#sendButton:disabled { background: %8; }
QComboBox, QLineEdit { background: %6; border: 1px solid %4; border-radius: 6px; padding: 5px 8px; }
QDockWidget { color: %2; }
QStatusBar { border-top: 1px solid %4; color: %9; }
)")
        .arg(color(*this, QStringLiteral("surface.canvas")).name(),
             color(*this, QStringLiteral("text.primary")).name(),
             color(*this, QStringLiteral("surface.sidebar")).name(),
             color(*this, QStringLiteral("border.subtle")).name(),
             color(*this, QStringLiteral("message.user")).name(),
             color(*this, QStringLiteral("surface.raised")).name(),
             color(*this, QStringLiteral("state.running")).name(),
             color(*this, QStringLiteral("text.disabled")).name(),
             color(*this, QStringLiteral("text.secondary")).name());
}

} // namespace snack::ui
