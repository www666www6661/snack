#include "app/WorkspaceExternalOpener.h"
#include "app/WorkspaceFileIndex.h"
#include "app/WorkspaceFilePreview.h"
#include "app/WorkspacePathPolicy.h"
#include "app/WorkspaceSymbolIndex.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

class TestWorkspaceFileIndex final : public QObject {
    Q_OBJECT

  private slots:
    void indexesWorkspaceWithExclusionsAndLimit();
    void resolvesOnlyExistingWorkspacePaths();
    void normalizesPathAliases();
    void readsBoundedTextPreviews();
    void opensOnlyValidatedFilesThroughInjectedBoundary();
    void buildsBoundedSourceSymbolIndex();
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

void TestWorkspaceFileIndex::resolvesOnlyExistingWorkspacePaths() {
    QTemporaryDir directory;
    QTemporaryDir outside;
    QVERIFY(directory.isValid());
    QVERIFY(outside.isValid());
    QDir root(directory.path());
    QVERIFY(root.mkpath(QStringLiteral("src")));
    QFile source(root.filePath(QStringLiteral("src/main.cpp")));
    QVERIFY(source.open(QIODevice::WriteOnly));
    source.write("int main() {}\n");
    source.close();

    QString error;
    const auto resolved = snack::app::WorkspacePathPolicy::resolveExisting(
        directory.path(), QStringLiteral("src/../src/main.cpp"), &error);
    QVERIFY2(resolved.has_value(), qPrintable(error));
    QCOMPARE(QFileInfo(*resolved).canonicalFilePath(), source.fileName());
    QVERIFY(!snack::app::WorkspacePathPolicy::resolveExisting(
                 directory.path(), outside.filePath(QStringLiteral("missing")), &error)
                 .has_value());

    QFile outsideFile(outside.filePath(QStringLiteral("secret.txt")));
    QVERIFY(outsideFile.open(QIODevice::WriteOnly));
    outsideFile.write("secret");
    outsideFile.close();
    QVERIFY(!snack::app::WorkspacePathPolicy::resolveExisting(directory.path(),
                                                              outsideFile.fileName(), &error)
                 .has_value());
    QVERIFY(error.contains(QStringLiteral("escapes")));
}

void TestWorkspaceFileIndex::normalizesPathAliases() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QDir root(directory.path());
    QVERIFY(root.mkpath(QStringLiteral("src")));
    QCOMPARE(
        snack::app::WorkspacePathPolicy::identityKey(root.filePath(QStringLiteral("src"))),
        snack::app::WorkspacePathPolicy::identityKey(root.filePath(QStringLiteral("src/../src"))));
#if defined(Q_OS_WIN)
    QCOMPARE(snack::app::WorkspacePathPolicy::identityKey(root.filePath(QStringLiteral("SRC"))),
             snack::app::WorkspacePathPolicy::identityKey(root.filePath(QStringLiteral("src"))));
#endif
}

void TestWorkspaceFileIndex::readsBoundedTextPreviews() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QFile textFile(directory.filePath(QStringLiteral("notes.txt")));
    QVERIFY(textFile.open(QIODevice::WriteOnly));
    textFile.write("abcdef");
    textFile.close();

    QString error;
    const auto preview = snack::app::WorkspaceFilePreviewReader::read(
        directory.path(), QStringLiteral("notes.txt"), 4, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(preview.text, QStringLiteral("abcd"));
    QVERIFY(preview.truncated);

    QFile binaryFile(directory.filePath(QStringLiteral("binary.dat")));
    QVERIFY(binaryFile.open(QIODevice::WriteOnly));
    binaryFile.write(QByteArray("a\0b", 3));
    binaryFile.close();
    const auto binaryPreview = snack::app::WorkspaceFilePreviewReader::read(
        directory.path(), QStringLiteral("binary.dat"), 10, &error);
    QVERIFY(binaryPreview.text.isEmpty());
    QVERIFY(error.contains(QStringLiteral("Binary")));
}

void TestWorkspaceFileIndex::opensOnlyValidatedFilesThroughInjectedBoundary() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QFile file(directory.filePath(QStringLiteral("open-me.txt")));
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("open");
    file.close();

    QList<QUrl> opened;
    QString error;
    QVERIFY(snack::app::WorkspaceExternalOpener::open(
        directory.path(), QStringLiteral("open-me.txt"),
        [&opened](const QUrl& url) {
            opened.append(url);
            return true;
        },
        &error));
    QCOMPARE(opened, QList<QUrl>{QUrl::fromLocalFile(file.fileName())});
    QVERIFY(!snack::app::WorkspaceExternalOpener::open(
        directory.path(), QStringLiteral("missing.txt"),
        [&opened](const QUrl& url) {
            opened.append(url);
            return true;
        },
        &error));
    QCOMPARE(opened.size(), 1);
}

void TestWorkspaceFileIndex::buildsBoundedSourceSymbolIndex() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QFile source(directory.filePath(QStringLiteral("sample.cpp")));
    QVERIFY(source.open(QIODevice::WriteOnly));
    source.write("class Alpha {};\nstruct Beta {};\nvoid ignored();\n");
    source.close();

    const auto symbols = snack::app::WorkspaceSymbolIndex::build(directory.path());
    QCOMPARE(symbols.size(), 2);
    QCOMPARE(symbols.at(0).name, QStringLiteral("Alpha"));
    QCOMPARE(symbols.at(0).line, 1);
    QCOMPARE(symbols.at(1).name, QStringLiteral("Beta"));
    QCOMPARE(snack::app::WorkspaceSymbolIndex::build(directory.path(), 10, 1).size(), 1);
    QVERIFY(snack::app::WorkspaceSymbolIndex::build(directory.path(), 0).isEmpty());
}

QTEST_GUILESS_MAIN(TestWorkspaceFileIndex)
#include "TestWorkspaceFileIndex.moc"
