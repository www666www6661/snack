#include "storage/ContentStore.h"
#include "storage/EventStore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>

namespace {

bool executeSql(const QString& databasePath, const QStringList& statements, QString* error) {
    const QString connectionName = QStringLiteral("snack-storage-fixture-%1")
                                       .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    bool succeeded = false;
    {
        QSqlDatabase database =
            QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        database.setDatabaseName(databasePath);
        if (!database.open()) {
            *error = database.lastError().text();
        } else {
            succeeded = true;
            for (const auto& statement : statements) {
                QSqlQuery query(database);
                if (!query.exec(statement)) {
                    *error = query.lastError().text();
                    succeeded = false;
                    break;
                }
            }
            database.close();
        }
        database = QSqlDatabase{};
    }
    QSqlDatabase::removeDatabase(connectionName);
    return succeeded;
}

QVariant queryScalar(const QString& databasePath, const QString& statement, QString* error) {
    const QString connectionName = QStringLiteral("snack-storage-inspect-%1")
                                       .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    QVariant result;
    {
        QSqlDatabase database =
            QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        database.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
        database.setDatabaseName(databasePath);
        if (!database.open()) {
            *error = database.lastError().text();
        } else {
            QSqlQuery query(database);
            if (!query.exec(statement) || !query.next()) {
                *error = query.lastError().text();
            } else {
                result = query.value(0);
            }
            database.close();
        }
        database = QSqlDatabase{};
    }
    QSqlDatabase::removeDatabase(connectionName);
    return result;
}

QStringList legacyV1Schema() {
    return {
        QStringLiteral("CREATE TABLE schema_migrations (version INTEGER PRIMARY KEY, "
                       "applied_at INTEGER NOT NULL)"),
        QStringLiteral("INSERT INTO schema_migrations VALUES (1, 1)"),
        QStringLiteral("CREATE TABLE conversations ("
                       "id TEXT PRIMARY KEY, title TEXT NOT NULL, working_directory TEXT NOT NULL, "
                       "agent_kind TEXT NOT NULL, status TEXT NOT NULL, native_session_id TEXT, "
                       "archived INTEGER NOT NULL DEFAULT 0, pinned INTEGER NOT NULL DEFAULT 0)"),
        QStringLiteral("CREATE TABLE events ("
                       "id TEXT PRIMARY KEY, conversation_id TEXT NOT NULL, turn_id TEXT, "
                       "sequence INTEGER NOT NULL, type TEXT NOT NULL, payload TEXT NOT NULL, "
                       "raw_payload TEXT NOT NULL, occurred_at INTEGER NOT NULL, "
                       "FOREIGN KEY(conversation_id) REFERENCES conversations(id) ON DELETE "
                       "CASCADE, UNIQUE(conversation_id, sequence))"),
        QStringLiteral("CREATE INDEX events_conversation_sequence "
                       "ON events(conversation_id, sequence)"),
        QStringLiteral(
            "INSERT INTO conversations VALUES "
            "('legacy', 'Legacy conversation', 'C:/legacy', 'mock', 'idle', NULL, 0, 0)")};
}

} // namespace

class TestStorage final : public QObject {
    Q_OBJECT

  private slots:
    void eventStorePersistsOrderedEvents();
    void contentStoreDeduplicatesAndVerifies();
    void contentStoreRejectsCorruption();
    void contentStoreRejectsInvalidHash();
    void eventStoreRejectsInvalidWrites();
    void eventStoreBacksUpAndMigratesLegacySchema();
    void eventStoreRollsBackIntoReadOnlyRecovery();
    void eventStoreOpensFutureSchemaReadOnly();
    void eventStoreRejectsIncompleteCurrentSchema();
    void contentStoreReportsFilesystemErrors();
};

void TestStorage::eventStorePersistsOrderedEvents() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    snack::storage::EventStore store;
    QString error;
    QVERIFY2(store.open(directory.filePath(QStringLiteral("events.sqlite3")), &error),
             qPrintable(error));
    QCOMPARE(store.mode(), snack::storage::EventStore::Mode::ReadWrite);
    QVERIFY(store.migrationBackupPath().isEmpty());

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

void TestStorage::eventStoreBacksUpAndMigratesLegacySchema() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString databasePath = directory.filePath(QStringLiteral("legacy.sqlite3"));
    QString error;
    QVERIFY2(executeSql(databasePath, legacyV1Schema(), &error), qPrintable(error));

    snack::storage::EventStore store;
    QVERIFY2(store.open(databasePath, &error), qPrintable(error));
    QCOMPARE(store.mode(), snack::storage::EventStore::Mode::ReadWrite);
    QVERIFY(!store.migrationBackupPath().isEmpty());
    QVERIFY(QFileInfo::exists(store.migrationBackupPath()));
    QCOMPARE(queryScalar(databasePath, QStringLiteral("SELECT MAX(version) FROM schema_migrations"),
                         &error)
                 .toInt(),
             2);
    QCOMPARE(queryScalar(databasePath,
                         QStringLiteral("SELECT COUNT(*) FROM sqlite_master WHERE type = 'index' "
                                        "AND name = 'conversations_working_directory'"),
                         &error)
                 .toInt(),
             1);
    QCOMPARE(queryScalar(store.migrationBackupPath(),
                         QStringLiteral("SELECT title FROM conversations WHERE id = 'legacy'"),
                         &error)
                 .toString(),
             QStringLiteral("Legacy conversation"));
}

void TestStorage::eventStoreRollsBackIntoReadOnlyRecovery() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString databasePath = directory.filePath(QStringLiteral("broken.sqlite3"));
    QString error;
    const QStringList brokenV1 = {
        QStringLiteral("CREATE TABLE schema_migrations (version INTEGER PRIMARY KEY, "
                       "applied_at INTEGER NOT NULL)"),
        QStringLiteral("INSERT INTO schema_migrations VALUES (1, 1)"),
        QStringLiteral("CREATE TABLE sentinel (value TEXT NOT NULL)"),
        QStringLiteral("INSERT INTO sentinel VALUES ('preserved')")};
    QVERIFY2(executeSql(databasePath, brokenV1, &error), qPrintable(error));

    snack::storage::EventStore store;
    QVERIFY(store.open(databasePath, &error));
    QVERIFY(store.isReadOnlyRecovery());
    QVERIFY(store.recoveryError().contains(QStringLiteral("SQL statement failed")));
    QVERIFY(QFileInfo::exists(store.migrationBackupPath()));
    QCOMPARE(queryScalar(databasePath, QStringLiteral("SELECT MAX(version) FROM schema_migrations"),
                         &error)
                 .toInt(),
             1);
    QCOMPARE(queryScalar(databasePath,
                         QStringLiteral("SELECT COUNT(*) FROM pragma_table_info("
                                        "'schema_migrations') WHERE name = 'started_at'"),
                         &error)
                 .toInt(),
             0);
    QCOMPARE(
        queryScalar(databasePath, QStringLiteral("SELECT value FROM sentinel"), &error).toString(),
        QStringLiteral("preserved"));
    QCOMPARE(queryScalar(store.migrationBackupPath(), QStringLiteral("SELECT value FROM sentinel"),
                         &error)
                 .toString(),
             QStringLiteral("preserved"));

    snack::domain::Conversation conversation;
    QVERIFY(!store.saveConversation(conversation, &error));
    QVERIFY(error.contains(QStringLiteral("read-only recovery")));
}

void TestStorage::eventStoreOpensFutureSchemaReadOnly() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString databasePath = directory.filePath(QStringLiteral("future.sqlite3"));
    QString error;
    const QStringList futureSchema = {
        QStringLiteral("CREATE TABLE schema_migrations (version INTEGER PRIMARY KEY, "
                       "applied_at INTEGER NOT NULL)"),
        QStringLiteral("INSERT INTO schema_migrations VALUES (99, 1)")};
    QVERIFY2(executeSql(databasePath, futureSchema, &error), qPrintable(error));
    QFile databaseFile(databasePath);
    QVERIFY(databaseFile.open(QIODevice::ReadOnly));
    const QByteArray originalBytes = databaseFile.readAll();
    databaseFile.close();

    snack::storage::EventStore store;
    QVERIFY(store.open(databasePath, &error));
    QVERIFY(store.isReadOnlyRecovery());
    QVERIFY(store.recoveryError().contains(QStringLiteral("newer")));
    QVERIFY(store.migrationBackupPath().isEmpty());
    QVERIFY(databaseFile.open(QIODevice::ReadOnly));
    QCOMPARE(databaseFile.readAll(), originalBytes);
}

void TestStorage::eventStoreRejectsIncompleteCurrentSchema() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString databasePath = directory.filePath(QStringLiteral("incomplete.sqlite3"));
    QString error;
    const QStringList incompleteSchema = {
        QStringLiteral("CREATE TABLE schema_migrations (version INTEGER PRIMARY KEY, "
                       "applied_at INTEGER NOT NULL, started_at INTEGER, completed_at INTEGER)"),
        QStringLiteral("INSERT INTO schema_migrations VALUES (2, 1, 1, 1)")};
    QVERIFY2(executeSql(databasePath, incompleteSchema, &error), qPrintable(error));

    snack::storage::EventStore store;
    QVERIFY(store.open(databasePath, &error));
    QVERIFY(store.isReadOnlyRecovery());
    QVERIFY(store.recoveryError().contains(QStringLiteral("required objects")));
    QVERIFY(store.migrationBackupPath().isEmpty());
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
