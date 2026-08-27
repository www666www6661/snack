#include "app/WorkspaceWatcher.h"

#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

class TestWorkspaceWatcher final : public QObject {
    Q_OBJECT

  private slots:
    void coalescesWorkspaceChanges();
};

void TestWorkspaceWatcher::coalescesWorkspaceChanges() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    snack::app::WorkspaceWatcher watcher;
    QSignalSpy changes(&watcher, &snack::app::WorkspaceWatcher::workspaceChanged);
    watcher.setWorkspace(directory.path());

    QFile first(directory.filePath(QStringLiteral("first.txt")));
    QVERIFY(first.open(QIODevice::WriteOnly));
    first.write("first");
    first.close();
    QFile second(directory.filePath(QStringLiteral("second.txt")));
    QVERIFY(second.open(QIODevice::WriteOnly));
    second.write("second");
    second.close();

    QTRY_COMPARE_WITH_TIMEOUT(changes.count(), 1, 5000);
}

QTEST_GUILESS_MAIN(TestWorkspaceWatcher)
#include "TestWorkspaceWatcher.moc"
