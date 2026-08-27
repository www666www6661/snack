#include "workspace/IsolationCapability.h"
#include "workspace/WorkspaceDiff.h"
#include "workspace/WorkspaceSnapshot.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

class TestWorkspaceSnapshot final : public QObject {
    Q_OBJECT

  private slots:
    void capturesContentAndDetectsChanges();
    void failsClosedAtBounds();
    void reportsOnlyAdvertisedIsolation();
    void producesBoundedNativeDiffHunks();
};

void TestWorkspaceSnapshot::capturesContentAndDetectsChanges() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QFile file(directory.filePath(QStringLiteral("file.txt")));
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("baseline");
    file.close();

    QString error;
    const auto snapshot =
        snack::workspace::WorkspaceSnapshot::capture(directory.path(), 10, 100, &error);
    QVERIFY2(snapshot.isComplete(), qPrintable(error));
    QCOMPARE(snapshot.paths(), QStringList{QStringLiteral("file.txt")});
    QCOMPARE(snapshot.entry(QStringLiteral("file.txt"))->content, QByteArray("baseline"));
    QVERIFY(snapshot.matchesCurrentFile(QStringLiteral("file.txt")));

    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write("changed");
    file.close();
    QVERIFY(!snapshot.matchesCurrentFile(QStringLiteral("file.txt")));
}

void TestWorkspaceSnapshot::failsClosedAtBounds() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QFile file(directory.filePath(QStringLiteral("large.txt")));
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("too large");
    file.close();
    QString error;
    QVERIFY(!snack::workspace::WorkspaceSnapshot::capture(directory.path(), 10, 2, &error)
                 .isComplete());
    QVERIFY(error.contains(QStringLiteral("byte limit")));
}

void TestWorkspaceSnapshot::reportsOnlyAdvertisedIsolation() {
    snack::agent::CapabilitySet codex;
    codex.accessLevels = {snack::domain::AccessLevel::Strict,
                          snack::domain::AccessLevel::Workspace};
    const auto sandbox = snack::workspace::IsolationCapabilityDetector::detect(
        snack::domain::AgentKind::Codex, codex);
    QCOMPARE(sandbox.mode, snack::workspace::IsolationMode::AgentSandbox);
    QVERIFY(sandbox.detail.contains(QStringLiteral("no isolated checkout")));

    const auto mock = snack::workspace::IsolationCapabilityDetector::detect(
        snack::domain::AgentKind::Mock, codex);
    QCOMPARE(mock.mode, snack::workspace::IsolationMode::None);
}

void TestWorkspaceSnapshot::producesBoundedNativeDiffHunks() {
    const auto hunks = snack::workspace::WorkspaceDiff::between(
        QByteArray("one\ntwo\nthree\n"), QByteArray("one\nchanged\nthree\n"));
    QCOMPARE(hunks.size(), 1);
    QCOMPARE(hunks.constFirst().oldStart, 1);
    const QString unified =
        snack::workspace::WorkspaceDiff::unifiedText(QStringLiteral("file.txt"), hunks);
    QVERIFY(unified.contains(QStringLiteral("-two")));
    QVERIFY(unified.contains(QStringLiteral("+changed")));
    QVERIFY(
        snack::workspace::WorkspaceDiff::between(QByteArray("same"), QByteArray("same")).isEmpty());
    QVERIFY(snack::workspace::WorkspaceDiff::between(QByteArray("a\nb"), QByteArray("a\nc"), 1)
                .isEmpty());
}

QTEST_GUILESS_MAIN(TestWorkspaceSnapshot)
#include "TestWorkspaceSnapshot.moc"
