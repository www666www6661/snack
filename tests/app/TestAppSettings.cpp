#include "app/AppSettings.h"

#include <QTemporaryDir>
#include <QTest>

class TestAppSettings final : public QObject {
    Q_OBJECT

  private slots:
    void usesSafeDefaults();
    void persistsValues();
    void clampsInterfaceScale();
};

void TestAppSettings::usesSafeDefaults() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    snack::app::AppSettings settings(directory.filePath(QStringLiteral("settings.ini")));

    const auto snapshot = settings.load();
    QCOMPARE(snapshot.themeMode, snack::app::ThemeMode::System);
    QCOMPARE(snapshot.locale, QStringLiteral("system"));
    QCOMPARE(snapshot.interfaceScale, 1.0);
    QVERIFY(snapshot.mainWindowGeometry.isEmpty());
    QVERIFY(snapshot.mainWindowState.isEmpty());
}

void TestAppSettings::persistsValues() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("settings.ini"));
    {
        snack::app::AppSettings settings(path);
        snack::app::AppSettingsSnapshot snapshot;
        snapshot.themeMode = snack::app::ThemeMode::Dark;
        snapshot.locale = QStringLiteral("zh_CN");
        snapshot.interfaceScale = 1.4;
        snapshot.lastWorkspace = QStringLiteral("workspace");
        snapshot.mainWindowGeometry = QByteArrayLiteral("geometry");
        snapshot.mainWindowState = QByteArrayLiteral("state");
        settings.save(snapshot);
    }

    const snack::app::AppSettings reloaded(path);
    const auto snapshot = reloaded.load();
    QCOMPARE(snapshot.themeMode, snack::app::ThemeMode::Dark);
    QCOMPARE(snapshot.locale, QStringLiteral("zh_CN"));
    QCOMPARE(snapshot.interfaceScale, 1.4);
    QCOMPARE(snapshot.lastWorkspace, QStringLiteral("workspace"));
    QCOMPARE(snapshot.mainWindowGeometry, QByteArrayLiteral("geometry"));
    QCOMPARE(snapshot.mainWindowState, QByteArrayLiteral("state"));
}

void TestAppSettings::clampsInterfaceScale() {
    QTemporaryDir directory;
    const QString path = directory.filePath(QStringLiteral("settings.ini"));
    snack::app::AppSettings settings(path);
    snack::app::AppSettingsSnapshot snapshot;
    snapshot.interfaceScale = 8.0;
    settings.save(snapshot);
    QCOMPARE(settings.load().interfaceScale, 2.0);
}

QTEST_APPLESS_MAIN(TestAppSettings)
#include "TestAppSettings.moc"
