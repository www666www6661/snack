#include "storage/ContentStore.h"
#include "storage/EventStore.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>
#include <QTimeZone>

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

QStringList legacyV2Schema() {
    return {
        QStringLiteral("CREATE TABLE schema_migrations (version INTEGER PRIMARY KEY, "
                       "applied_at INTEGER NOT NULL, started_at INTEGER, completed_at INTEGER)"),
        QStringLiteral("INSERT INTO schema_migrations VALUES (1, 1, 1, 1)"),
        QStringLiteral("INSERT INTO schema_migrations VALUES (2, 2, 2, 2)"),
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
        QStringLiteral("CREATE INDEX conversations_working_directory "
                       "ON conversations(working_directory)"),
        QStringLiteral("INSERT INTO conversations VALUES "
                       "('11111111-1111-1111-1111-111111111111', 'V2 conversation', 'C:/v2', "
                       "'codex', 'idle', "
                       "'legacy-session', 0, 0)")};
}

} // namespace

class TestStorage final : public QObject {
    Q_OBJECT

  private slots:
    void eventStorePersistsOrderedEvents();
    void eventStoreDeletesConversationGraph();
    void contentStoreDeduplicatesAndVerifies();
    void contentStoreRejectsCorruption();
    void contentStoreRejectsInvalidHash();
    void eventStoreRejectsInvalidWrites();
    void eventStoreBacksUpAndMigratesLegacySchema();
    void eventStoreMigratesV2NativeIdentity();
    void eventStoreRollsBackIntoReadOnlyRecovery();
    void eventStoreOpensFutureSchemaReadOnly();
    void eventStoreRejectsIncompleteCurrentSchema();
    void contentStoreReportsFilesystemErrors();
};

void TestStorage::eventStoreDeletesConversationGraph() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    snack::storage::EventStore store;
    QString error;
    QVERIFY(store.open(directory.filePath(QStringLiteral("events.sqlite3")), &error));
    snack::domain::Conversation conversation;
    conversation.title = QStringLiteral("Delete me");
    conversation.workingDirectory = directory.path();
    QVERIFY(store.saveConversation(conversation, &error));
    snack::domain::AgentEvent event;
    event.conversationId = conversation.id;
    event.type = snack::domain::AgentEventType::UserMessage;
    event.payload.insert(QStringLiteral("text"), QStringLiteral("private text"));
    QVERIFY(store.appendEvent(event, &error));
    snack::domain::QueuedMessage queued;
    queued.conversationId = conversation.id;
    queued.content = QStringLiteral("queued private text");
    QVERIFY(store.replaceQueuedMessages(conversation.id, {queued}, &error));

    QVERIFY(store.deleteConversation(conversation.id, &error));
    QVERIFY(!store.conversationById(conversation.id, &error).has_value());
    QVERIFY(store.eventsForConversation(conversation.id, &error).isEmpty());
    QVERIFY(store.queuedMessagesForConversation(conversation.id, &error).isEmpty());
    QVERIFY(!store.deleteConversation(conversation.id, &error));
    QVERIFY(error.contains(QStringLiteral("no longer exists")));
    QVERIFY(!store.deleteConversation(QUuid{}, &error));
}

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
    conversation.titleIsPlaceholder = true;
    conversation.workingDirectory = directory.path();
    conversation.nativeThreadId = QStringLiteral("thread-123");
    conversation.nativeSessionId = QStringLiteral("session-root-123");
    conversation.modelId = QStringLiteral("mock-fast");
    conversation.tags = {QStringLiteral("backend"), QStringLiteral("urgent")};
    conversation.groupName = QStringLiteral("Engineering");
    conversation.createdAt = QDateTime::fromMSecsSinceEpoch(1000, QTimeZone::UTC);
    conversation.lastActivityAt = conversation.createdAt;
    QVERIFY2(store.saveConversation(conversation, &error), qPrintable(error));

    const auto restoredConversation = store.conversationById(conversation.id, &error);
    QVERIFY2(restoredConversation.has_value(), qPrintable(error));
    QCOMPARE(restoredConversation->nativeThreadId, QStringLiteral("thread-123"));
    QCOMPARE(restoredConversation->nativeSessionId, QStringLiteral("session-root-123"));
    QCOMPARE(restoredConversation->modelId, QStringLiteral("mock-fast"));
    QVERIFY(restoredConversation->titleIsPlaceholder);
    QCOMPARE(restoredConversation->tags,
             QStringList({QStringLiteral("backend"), QStringLiteral("urgent")}));
    QCOMPARE(restoredConversation->createdAt, conversation.createdAt);
    QCOMPARE(restoredConversation->lastActivityAt, conversation.lastActivityAt);
    QCOMPARE(restoredConversation->groupName, QStringLiteral("Engineering"));
    QVERIFY(!store.conversationById(QUuid::createUuid(), &error).has_value());

    snack::domain::Conversation pinned = conversation;
    pinned.id = QUuid::createUuid();
    pinned.title = QStringLiteral("Pinned");
    pinned.titleIsPlaceholder = false;
    pinned.pinned = true;
    QVERIFY(store.saveConversation(pinned, &error));
    snack::domain::Conversation archived = conversation;
    archived.id = QUuid::createUuid();
    archived.title = QStringLiteral("Archived");
    archived.titleIsPlaceholder = false;
    archived.archived = true;
    QVERIFY(store.saveConversation(archived, &error));
    const auto conversations = store.conversations(&error);
    QCOMPARE(conversations.size(), 3);
    QCOMPARE(conversations.at(0).id, pinned.id);
    QCOMPARE(conversations.at(1).id, conversation.id);
    QCOMPARE(conversations.at(2).id, archived.id);

    for (quint64 sequence = 1; sequence <= 2; ++sequence) {
        snack::domain::AgentEvent event;
        event.conversationId = conversation.id;
        event.sequence = sequence;
        event.type = sequence == 1 ? snack::domain::AgentEventType::UserMessage
                                   : snack::domain::AgentEventType::AgentMessageDelta;
        event.payload = {{QStringLiteral("text"), QString::number(sequence)}};
        event.occurredAt = conversation.createdAt.addSecs(static_cast<qint64>(sequence));
        QVERIFY2(store.appendEvent(event, &error), qPrintable(error));
    }

    const auto events = store.eventsForConversation(conversation.id, &error);
    QCOMPARE(events.size(), 2);
    QCOMPARE(events.at(0).sequence, quint64{1});
    QCOMPARE(events.at(1).payload.value(QStringLiteral("text")).toString(), QStringLiteral("2"));
    const auto activeConversation = store.conversationById(conversation.id, &error);
    QVERIFY(activeConversation.has_value());
    QCOMPARE(activeConversation->lastActivityAt, conversation.createdAt.addSecs(2));

    snack::domain::QueuedMessage firstQueued;
    firstQueued.conversationId = conversation.id;
    firstQueued.content = QStringLiteral("First queued message");
    firstQueued.attachments = {QStringLiteral("workspace://README.md")};
    snack::domain::QueuedMessage secondQueued;
    secondQueued.conversationId = conversation.id;
    secondQueued.content = QStringLiteral("Second queued message");
    secondQueued.position = 1;
    QVERIFY2(store.replaceQueuedMessages(conversation.id, {firstQueued, secondQueued}, &error),
             qPrintable(error));
    auto queued = store.queuedMessagesForConversation(conversation.id, &error);
    QCOMPARE(queued.size(), 2);
    QCOMPARE(queued.at(0).id, firstQueued.id);
    QCOMPARE(queued.at(0).attachments, firstQueued.attachments);
    QCOMPARE(queued.at(1).position, qsizetype{1});

    secondQueued.position = 0;
    secondQueued.content = QStringLiteral("Edited and moved");
    QVERIFY2(store.replaceQueuedMessages(conversation.id, {secondQueued}, &error),
             qPrintable(error));
    queued = store.queuedMessagesForConversation(conversation.id, &error);
    QCOMPARE(queued.size(), 1);
    QCOMPARE(queued.constFirst().content, QStringLiteral("Edited and moved"));

    snack::domain::PromptTemplate favorite;
    favorite.name = QStringLiteral("Review");
    favorite.content = QStringLiteral("Review {{path}}");
    favorite.position = 1;
    QVERIFY2(store.savePromptTemplate(favorite, &error), qPrintable(error));
    snack::domain::PromptTemplate other;
    other.name = QStringLiteral("Explain");
    other.content = QStringLiteral("Explain this code");
    other.favorite = false;
    QVERIFY2(store.savePromptTemplate(other, &error), qPrintable(error));
    auto templates = store.promptTemplates(&error);
    QCOMPARE(templates.size(), 2);
    QCOMPARE(templates.constFirst().id, favorite.id);
    favorite.content = QStringLiteral("Review {{path}} for {{focus}}");
    favorite.position = 0;
    QVERIFY2(store.savePromptTemplate(favorite, &error), qPrintable(error));
    templates = store.promptTemplates(&error);
    QCOMPARE(templates.constFirst().content, favorite.content);
    QVERIFY(store.deletePromptTemplate(other.id, &error));
    QCOMPARE(store.promptTemplates(&error).size(), 1);
    QVERIFY(!store.deletePromptTemplate(other.id, &error));
    snack::domain::PromptTemplate invalid;
    invalid.name = QStringLiteral("Invalid");
    invalid.content = QStringLiteral("Broken {{bad name}}");
    QVERIFY(!store.savePromptTemplate(invalid, &error));

    snack::domain::SavedConversationView activeCodex;
    activeCodex.name = QStringLiteral("  Active Codex  ");
    activeCodex.query = QStringLiteral(" agent:codex status:running ");
    activeCodex.showArchived = false;
    activeCodex.position = 1;
    QVERIFY2(store.saveConversationView(activeCodex, &error), qPrintable(error));
    snack::domain::SavedConversationView backend;
    backend.name = QStringLiteral("Backend");
    backend.query = QStringLiteral("tag:backend");
    backend.position = 0;
    QVERIFY2(store.saveConversationView(backend, &error), qPrintable(error));
    auto views = store.conversationViews(&error);
    QCOMPARE(views.size(), 2);
    QCOMPARE(views.at(0).id, backend.id);
    QCOMPARE(views.at(1).name, QStringLiteral("Active Codex"));
    QCOMPARE(views.at(1).query, QStringLiteral("agent:codex status:running"));
    QVERIFY(!views.at(1).showArchived);

    activeCodex.name = QStringLiteral("Agent work");
    activeCodex.position = 0;
    QVERIFY2(store.saveConversationView(activeCodex, &error), qPrintable(error));
    views = store.conversationViews(&error);
    QCOMPARE(views.constFirst().id, activeCodex.id);
    snack::domain::SavedConversationView duplicateName;
    duplicateName.name = QStringLiteral("agent WORK");
    QVERIFY(!store.saveConversationView(duplicateName, &error));
    snack::domain::SavedConversationView invalidView;
    invalidView.id = QUuid{};
    QVERIFY(!store.saveConversationView(invalidView, &error));
    QVERIFY2(store.reorderConversationViews({activeCodex.id, backend.id}, &error),
             qPrintable(error));
    views = store.conversationViews(&error);
    QCOMPARE(views.at(0).id, activeCodex.id);
    QCOMPARE(views.at(0).position, 0);
    QCOMPARE(views.at(1).id, backend.id);
    QCOMPARE(views.at(1).position, 1);
    QVERIFY(!store.reorderConversationViews({backend.id}, &error));
    QCOMPARE(store.conversationViews(&error).at(0).id, activeCodex.id);
    QVERIFY(store.deleteConversationView(backend.id, &error));
    QCOMPARE(store.conversationViews(&error).size(), 1);
    QVERIFY(!store.deleteConversationView(backend.id, &error));
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
             11);
    QCOMPARE(queryScalar(databasePath,
                         QStringLiteral("SELECT COUNT(*) FROM sqlite_master WHERE type = 'index' "
                                        "AND name = 'conversations_working_directory'"),
                         &error)
                 .toInt(),
             1);
    QCOMPARE(queryScalar(databasePath,
                         QStringLiteral("SELECT title_is_placeholder FROM conversations "
                                        "WHERE id = 'legacy'"),
                         &error)
                 .toInt(),
             0);
    QCOMPARE(queryScalar(databasePath,
                         QStringLiteral("SELECT tags FROM conversations WHERE id = 'legacy'"),
                         &error)
                 .toString(),
             QStringLiteral("[]"));
    QVERIFY(queryScalar(databasePath,
                        QStringLiteral("SELECT model_id FROM conversations WHERE id = 'legacy'"),
                        &error)
                .toString()
                .isEmpty());
    QCOMPARE(queryScalar(databasePath, QStringLiteral("SELECT COUNT(*) FROM saved_views"), &error)
                 .toInt(),
             0);
    QVERIFY(queryScalar(databasePath,
                        QStringLiteral("SELECT group_name FROM conversations WHERE id = 'legacy'"),
                        &error)
                .toString()
                .isEmpty());
    QVERIFY(queryScalar(databasePath,
                        QStringLiteral("SELECT created_at FROM conversations WHERE id = 'legacy'"),
                        &error)
                .toLongLong() > 0);
    QVERIFY(queryScalar(
                databasePath,
                QStringLiteral("SELECT last_activity_at FROM conversations WHERE id = 'legacy'"),
                &error)
                .toLongLong() > 0);
    QCOMPARE(queryScalar(databasePath,
                         QStringLiteral("SELECT COUNT(*) FROM pragma_table_info("
                                        "'conversations') WHERE name = 'native_thread_id'"),
                         &error)
                 .toInt(),
             1);
    QCOMPARE(queryScalar(store.migrationBackupPath(),
                         QStringLiteral("SELECT title FROM conversations WHERE id = 'legacy'"),
                         &error)
                 .toString(),
             QStringLiteral("Legacy conversation"));
}

void TestStorage::eventStoreMigratesV2NativeIdentity() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString databasePath = directory.filePath(QStringLiteral("v2.sqlite3"));
    QString error;
    QVERIFY2(executeSql(databasePath, legacyV2Schema(), &error), qPrintable(error));

    snack::storage::EventStore store;
    QVERIFY2(store.open(databasePath, &error), qPrintable(error));
    QVERIFY(!store.migrationBackupPath().isEmpty());
    QCOMPARE(queryScalar(databasePath, QStringLiteral("SELECT MAX(version) FROM schema_migrations"),
                         &error)
                 .toInt(),
             11);
    const auto restored = store.conversationById(
        QUuid(QStringLiteral("11111111-1111-1111-1111-111111111111")), &error);
    QVERIFY2(restored.has_value(), qPrintable(error));
    QCOMPARE(restored->nativeSessionId, QStringLiteral("legacy-session"));
    QVERIFY(restored->nativeThreadId.isEmpty());
    QVERIFY(!restored->titleIsPlaceholder);
    QVERIFY(restored->tags.isEmpty());
    QVERIFY(restored->modelId.isEmpty());

    QCOMPARE(queryScalar(databasePath,
                         QStringLiteral("SELECT native_session_id FROM conversations "
                                        "WHERE id = '11111111-1111-1111-1111-111111111111'"),
                         &error)
                 .toString(),
             QStringLiteral("legacy-session"));
    QVERIFY(queryScalar(databasePath,
                        QStringLiteral("SELECT native_thread_id FROM conversations "
                                       "WHERE id = '11111111-1111-1111-1111-111111111111'"),
                        &error)
                .isNull());
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
    snack::domain::SavedConversationView view;
    view.name = QStringLiteral("Unavailable");
    QVERIFY(!store.saveConversationView(view, &error));
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
        QStringLiteral("INSERT INTO schema_migrations VALUES (11, 1, 1, 1)")};
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
