#include "app/WorkspaceFileIndex.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

class TestWorkspaceFileIndex final : public QObject {
    Q_OBJECT

  private slots:
    void indexesWorkspaceWithExclusionsAndLimit();
};

void TestWorkspaceFileIndex::indexesWorkspaceWithExclusionsAndLimit() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QDir root(directory.path());
    QVERIFY(root.mkpath(QStringLiteral("src")));
    QVERIFY(root.mkpath(QStringLiteral(".git")));
    QVERIFY(root.mkpath(QStringLiteral("node_modules/pkg")));
    const auto touch = [&root](const QString& path) {
        QFile file(root.filePath(path));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("test");
    };
    touch(QStringLiteral("README.md"));
    touch(QStringLiteral("src/main.cpp"));
    touch(QStringLiteral(".git/config"));
    touch(QStringLiteral("node_modules/pkg/index.js"));

    QCOMPARE(snack::app::WorkspaceFileIndex::files(directory.path()),
             QStringList({QStringLiteral("README.md"), QStringLiteral("src/main.cpp")}));
    QCOMPARE(snack::app::WorkspaceFileIndex::files(directory.path(), 1).size(), 1);
    QVERIFY(snack::app::WorkspaceFileIndex::files(QStringLiteral("missing")).isEmpty());
    QVERIFY(snack::app::WorkspaceFileIndex::files(directory.path(), 0).isEmpty());
}

QTEST_GUILESS_MAIN(TestWorkspaceFileIndex)
#include "TestWorkspaceFileIndex.moc"
