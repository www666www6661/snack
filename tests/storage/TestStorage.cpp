#include "storage/ContentStore.h"
#include "storage/EventStore.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

class TestStorage final : public QObject {
    Q_OBJECT

  private slots:
    void eventStorePersistsOrderedEvents();
    void contentStoreDeduplicatesAndVerifies();
    void contentStoreRejectsCorruption();
    void contentStoreRejectsInvalidHash();
    void eventStoreRejectsInvalidWrites();
    void contentStoreReportsFilesystemErrors();
};

void TestStorage::eventStorePersistsOrderedEvents() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    snack::storage::EventStore store;
    QString error;
    QVERIFY2(store.open(directory.filePath(QStringLiteral("events.sqlite3")), &error),
             qPrintable(error));

    snack::domain::Conversation conversation;
    conversation.title = QStringLiteral("Test");
    conversation.workingDirectory = directory.path();
    QVERIFY2(store.saveConversation(conversation, &error), qPrintable(error));

    for (quint64 sequence = 1; sequence <= 2; ++sequence) {
        snack::domain::AgentEvent event;
        event.conversationId = conversation.id;
        event.sequence = sequence;
        event.type = sequence == 1 ? snack::domain::AgentEventType::UserMessage
                                   : snack::domain::AgentEventType::AgentMessageDelta;
        event.payload = {{QStringLiteral("text"), QString::number(sequence)}};
        QVERIFY2(store.appendEvent(event, &error), qPrintable(error));
    }

    const auto events = store.eventsForConversation(conversation.id, &error);
    QCOMPARE(events.size(), 2);
    QCOMPARE(events.at(0).sequence, quint64{1});
    QCOMPARE(events.at(1).payload.value(QStringLiteral("text")).toString(), QStringLiteral("2"));
}

void TestStorage::contentStoreDeduplicatesAndVerifies() {
    QTemporaryDir directory;
    snack::storage::FileContentStore store(directory.path());
    QString error;
    const QByteArray content("snack-content");
    const QString firstHash = store.put(content, &error);
    QVERIFY2(!firstHash.isEmpty(), qPrintable(error));
    QCOMPARE(store.put(content, &error), firstHash);
    QVERIFY(store.contains(firstHash));
    QCOMPARE(store.get(firstHash, &error), content);
}

void TestStorage::contentStoreRejectsCorruption() {
    QTemporaryDir directory;
    snack::storage::FileContentStore store(directory.path());
    QString error;
    const QString hash = store.put(QByteArray("original"), &error);
    QVERIFY(!hash.isEmpty());
    const QString blobPath =
        QDir(directory.path())
            .filePath(hash.left(2) + QLatin1Char('/') + hash.mid(2, 2) + QLatin1Char('/') + hash);
    QFile blob(blobPath);
    QVERIFY(blob.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QCOMPARE(blob.write("tampered"), qint64{8});
    blob.close();

    QVERIFY(store.get(hash, &error).isEmpty());
    QVERIFY(error.contains(QStringLiteral("SHA-256")));

    QCOMPARE(store.put(QByteArray("original"), &error), hash);
    QCOMPARE(store.get(hash, &error), QByteArray("original"));
}

void TestStorage::contentStoreRejectsInvalidHash() {
    QTemporaryDir directory;
    snack::storage::FileContentStore store(directory.path());
    QString error;
    const QString malicious =
        QStringLiteral("../../../../outside").leftJustified(64, QLatin1Char('a'));
    QVERIFY(!store.contains(malicious));
    QVERIFY(store.get(malicious, &error).isEmpty());
    QCOMPARE(error, QStringLiteral("Invalid content hash"));
}

void TestStorage::eventStoreRejectsInvalidWrites() {
    QTemporaryDir directory;
    snack::storage::EventStore store;
    QString error;
    const QString databasePath = directory.filePath(QStringLiteral("events.sqlite3"));
    QVERIFY(store.open(databasePath, &error));
    QVERIFY(store.open(databasePath, &error));

    snack::domain::Conversation conversation;
    conversation.title = QStringLiteral("Validation");
    conversation.workingDirectory = directory.path();
    QVERIFY(store.saveConversation(conversation, &error));
    snack::domain::AgentEvent first;
    first.conversationId = conversation.id;
    first.sequence = 1;
    QVERIFY(store.appendEvent(first, &error));
    snack::domain::AgentEvent duplicate;
    duplicate.conversationId = conversation.id;
    duplicate.sequence = 1;
    QVERIFY(!store.appendEvent(duplicate, &error));
    QVERIFY(error.contains(QStringLiteral("append")));

    snack::domain::AgentEvent orphan;
    orphan.conversationId = QUuid::createUuid();
    orphan.sequence = 1;
    QVERIFY(!store.appendEvent(orphan, &error));
    QCOMPARE(store.eventsForConversation(orphan.conversationId, &error).size(), 0);
}

void TestStorage::contentStoreReportsFilesystemErrors() {
    QTemporaryDir directory;
    const QString blockingFile = directory.filePath(QStringLiteral("not-a-directory"));
    QFile file(blockingFile);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("block");
    file.close();

    snack::storage::FileContentStore store(blockingFile);
    QString error;
    QVERIFY(store.put(QByteArray("content"), &error).isEmpty());
    QVERIFY(error.contains(QStringLiteral("directory")));
    const QString missingHash(64, QLatin1Char('a'));
    QVERIFY(store.get(missingHash, &error).isEmpty());
    QVERIFY(error.contains(QStringLiteral("read")));
}

QTEST_GUILESS_MAIN(TestStorage)
#include "TestStorage.moc"
