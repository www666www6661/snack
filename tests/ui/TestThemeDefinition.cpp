#include "ui/ThemeDefinition.h"

#include <QJsonObject>
#include <QTest>

class TestThemeDefinition final : public QObject {
    Q_OBJECT

  private slots:
    void inheritsMissingTokens();
    void rejectsExecutableStyleFields();
    void rejectsUnknownAndTransparentColors();
    void producesWidgetStyleSheet();
};

void TestThemeDefinition::inheritsMissingTokens() {
    const QJsonObject object{{QStringLiteral("schemaVersion"), 1},
                             {QStringLiteral("name"), QStringLiteral("Calm")},
                             {QStringLiteral("baseMode"), QStringLiteral("dark")},
                             {QStringLiteral("colors"), QJsonObject{{QStringLiteral("focus.ring"),
                                                                     QStringLiteral("#4fbf9f")}}}};
    QString error;
    const auto parsed = snack::ui::ThemeDefinition::fromJson(object, &error);
    QVERIFY2(parsed.has_value(), qPrintable(error));
    QCOMPARE(parsed->colors.value(QStringLiteral("focus.ring")), QColor(QStringLiteral("#4fbf9f")));
    QVERIFY(parsed->colors.contains(QStringLiteral("text.primary")));
}

void TestThemeDefinition::rejectsExecutableStyleFields() {
    const QJsonObject object{{QStringLiteral("schemaVersion"), 1},
                             {QStringLiteral("name"), QStringLiteral("Unsafe")},
                             {QStringLiteral("baseMode"), QStringLiteral("light")},
                             {QStringLiteral("qss"), QStringLiteral("QWidget {}")}};
    QString error;
    QVERIFY(!snack::ui::ThemeDefinition::fromJson(object, &error).has_value());
    QVERIFY(error.contains(QStringLiteral("forbidden")));
}

void TestThemeDefinition::rejectsUnknownAndTransparentColors() {
    QJsonObject object{{QStringLiteral("schemaVersion"), 1},
                       {QStringLiteral("name"), QStringLiteral("Unknown")},
                       {QStringLiteral("baseMode"), QStringLiteral("light")},
                       {QStringLiteral("colors"),
                        QJsonObject{{QStringLiteral("made.up"), QStringLiteral("#ffffff")}}}};
    QVERIFY(!snack::ui::ThemeDefinition::fromJson(object, nullptr).has_value());

    object.insert(QStringLiteral("colors"),
                  QJsonObject{{QStringLiteral("focus.ring"), QStringLiteral("#804fbf9f")}});
    QVERIFY(!snack::ui::ThemeDefinition::fromJson(object, nullptr).has_value());
}

void TestThemeDefinition::producesWidgetStyleSheet() {
    const QString styleSheet = snack::ui::ThemeDefinition::dark().styleSheet();
    QVERIFY(styleSheet.contains(QStringLiteral("QMainWindow")));
    QVERIFY(styleSheet.contains(QStringLiteral("#171917")));
    QVERIFY(!styleSheet.contains(QStringLiteral("#ff00ff")));
}

QTEST_APPLESS_MAIN(TestThemeDefinition)
#include "TestThemeDefinition.moc"
