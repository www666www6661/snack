#include "app/AppSettings.h"

#include <QTemporaryDir>
#include <QTest>

class TestAppSettings final : public QObject {
    Q_OBJECT

  private slots:
    void usesSafeDefaults();
    void persistsValues();
    void clampsInterfaceScale();
    void persistsConversationDrafts();
    void persistsDetachedWindowGeometry();
};

void TestAppSettings::usesSafeDefaults() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    snack::app::AppSettings settings(directory.filePath(QStringLiteral("settings.ini")));

    const auto snapshot = settings.load();
    QCOMPARE(snapshot.themeMode, snack::app::ThemeMode::System);
    QCOMPARE(snapshot.locale, QStringLiteral("system"));
    QCOMPARE(snapshot.interfaceScale, 1.0);
    QVERIFY(snapshot.showArchivedConversations);
    QCOMPARE(snapshot.preferredAgentKind, snack::domain::AgentKind::Codex);
    QVERIFY(snapshot.codexExecutable.isEmpty());
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
        snapshot.showArchivedConversations = false;
        snapshot.preferredAgentKind = snack::domain::AgentKind::Mock;
        snapshot.codexExecutable = QStringLiteral("custom-codex");
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
    QVERIFY(!snapshot.showArchivedConversations);
    QCOMPARE(snapshot.preferredAgentKind, snack::domain::AgentKind::Mock);
    QCOMPARE(snapshot.codexExecutable, QStringLiteral("custom-codex"));
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

    QSettings raw(path, QSettings::IniFormat);
    raw.setValue(QStringLiteral("agent/preferred"), QStringLiteral("future-agent"));
    raw.sync();
    QCOMPARE(settings.load().preferredAgentKind, snack::domain::AgentKind::Codex);
}

void TestAppSettings::persistsConversationDrafts() {
    QTemporaryDir directory;
    const QString path = directory.filePath(QStringLiteral("settings.ini"));
    const QUuid first = QUuid::createUuid();
    const QUuid second = QUuid::createUuid();
    {
        snack::app::AppSettings settings(path);
        settings.saveComposerDraft(first, QStringLiteral("first\nmultiline draft"));
        settings.saveComposerDraft(second, QStringLiteral("second draft"));
        QCOMPARE(settings.composerDraft(first), QStringLiteral("first\nmultiline draft"));
        QVERIFY(settings.composerDraft(QUuid{}).isEmpty());
        settings.saveComposerDraft(QUuid{}, QStringLiteral("ignored"));
    }

    snack::app::AppSettings reloaded(path);
    QCOMPARE(reloaded.composerDraft(first), QStringLiteral("first\nmultiline draft"));
    QCOMPARE(reloaded.composerDraft(second), QStringLiteral("second draft"));
    reloaded.saveComposerDraft(first, {});
    QVERIFY(reloaded.composerDraft(first).isEmpty());
    QCOMPARE(reloaded.composerDraft(second), QStringLiteral("second draft"));
}

void TestAppSettings::persistsDetachedWindowGeometry() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("settings.ini"));
    const QUuid conversationId = QUuid::createUuid();
    {
        snack::app::AppSettings settings(path);
        QVERIFY(settings.detachedWindowGeometry(QUuid{}).isEmpty());
        settings.saveDetachedWindowGeometry(QUuid{}, QByteArrayLiteral("ignored"));
        settings.saveDetachedWindowGeometry(conversationId, QByteArrayLiteral("geometry"));
    }
    snack::app::AppSettings reloaded(path);
    QCOMPARE(reloaded.detachedWindowGeometry(conversationId), QByteArrayLiteral("geometry"));
    reloaded.saveDetachedWindowGeometry(conversationId, {});
    QVERIFY(reloaded.detachedWindowGeometry(conversationId).isEmpty());
}

QTEST_APPLESS_MAIN(TestAppSettings)
#include "TestAppSettings.moc"
