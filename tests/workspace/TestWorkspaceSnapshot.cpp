#include "workspace/WorkspaceSnapshot.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

class TestWorkspaceSnapshot final : public QObject {
    Q_OBJECT

  private slots:
    void capturesContentAndDetectsChanges();
    void failsClosedAtBounds();
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

QTEST_GUILESS_MAIN(TestWorkspaceSnapshot)
#include "TestWorkspaceSnapshot.moc"
