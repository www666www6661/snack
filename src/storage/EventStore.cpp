#include "storage/EventStore.h"

#include "domain/PromptTemplateEngine.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSqlError>
#include <QSqlQuery>
#include <QTimeZone>
#include <QVariant>

namespace snack::storage {
namespace {

constexpr int currentSchemaVersion = 8;

struct Migration {
    int version;
    QStringList statements;
};

QList<Migration> migrations() {
    return {{1,
             {QStringLiteral("CREATE TABLE IF NOT EXISTS schema_migrations ("
                             "version INTEGER PRIMARY KEY, applied_at INTEGER NOT NULL)"),
              QStringLiteral(
                  "CREATE TABLE IF NOT EXISTS conversations ("
                  "id TEXT PRIMARY KEY, title TEXT NOT NULL, working_directory TEXT NOT NULL, "
                  "agent_kind TEXT NOT NULL, status TEXT NOT NULL, native_session_id TEXT, "
                  "archived INTEGER NOT NULL DEFAULT 0, pinned INTEGER NOT NULL DEFAULT 0)"),
              QStringLiteral(
                  "CREATE TABLE IF NOT EXISTS events ("
                  "id TEXT PRIMARY KEY, conversation_id TEXT NOT NULL, turn_id TEXT, "
                  "sequence INTEGER NOT NULL, type TEXT NOT NULL, payload TEXT NOT NULL, "
                  "raw_payload TEXT NOT NULL, occurred_at INTEGER NOT NULL, "
                  "FOREIGN KEY(conversation_id) REFERENCES conversations(id) ON DELETE CASCADE, "
                  "UNIQUE(conversation_id, sequence))"),
              QStringLiteral("CREATE INDEX IF NOT EXISTS events_conversation_sequence "
                             "ON events(conversation_id, sequence)"),
              QStringLiteral("INSERT INTO schema_migrations(version, applied_at) "
                             "VALUES (1, CAST(strftime('%s','now') AS INTEGER) * 1000)")}},
            {2,
             {QStringLiteral("ALTER TABLE schema_migrations ADD COLUMN started_at INTEGER"),
              QStringLiteral("ALTER TABLE schema_migrations ADD COLUMN completed_at INTEGER"),
              QStringLiteral("UPDATE schema_migrations SET started_at = applied_at, "
                             "completed_at = applied_at WHERE version = 1"),
              QStringLiteral("INSERT INTO schema_migrations "
                             "(version, applied_at, started_at, completed_at) VALUES "
                             "(2, 0, CAST(strftime('%s','now') AS INTEGER) * 1000, NULL)"),
              QStringLiteral("CREATE INDEX IF NOT EXISTS conversations_working_directory "
                             "ON conversations(working_directory)"),
              QStringLiteral("UPDATE schema_migrations SET "
                             "applied_at = CAST(strftime('%s','now') AS INTEGER) * 1000, "
                             "completed_at = CAST(strftime('%s','now') AS INTEGER) * 1000 "
                             "WHERE version = 2")}},
            {3,
             {QStringLiteral("INSERT INTO schema_migrations "
                             "(version, applied_at, started_at, completed_at) VALUES "
                             "(3, 0, CAST(strftime('%s','now') AS INTEGER) * 1000, NULL)"),
              QStringLiteral("ALTER TABLE conversations ADD COLUMN native_thread_id TEXT"),
              QStringLiteral("UPDATE schema_migrations SET "
                             "applied_at = CAST(strftime('%s','now') AS INTEGER) * 1000, "
                             "completed_at = CAST(strftime('%s','now') AS INTEGER) * 1000 "
                             "WHERE version = 3")}},
            {4,
             {QStringLiteral("INSERT INTO schema_migrations "
                             "(version, applied_at, started_at, completed_at) VALUES "
                             "(4, 0, CAST(strftime('%s','now') AS INTEGER) * 1000, NULL)"),
              QStringLiteral(
                  "CREATE TABLE queued_messages ("
                  "id TEXT PRIMARY KEY, conversation_id TEXT NOT NULL, content TEXT NOT NULL, "
                  "attachments TEXT NOT NULL DEFAULT '[]', position INTEGER NOT NULL, "
                  "state TEXT NOT NULL, "
                  "FOREIGN KEY(conversation_id) REFERENCES conversations(id) ON DELETE CASCADE, "
                  "UNIQUE(conversation_id, position))"),
              QStringLiteral("UPDATE schema_migrations SET "
                             "applied_at = CAST(strftime('%s','now') AS INTEGER) * 1000, "
                             "completed_at = CAST(strftime('%s','now') AS INTEGER) * 1000 "
                             "WHERE version = 4")}},
            {5,
             {QStringLiteral("INSERT INTO schema_migrations "
                             "(version, applied_at, started_at, completed_at) VALUES "
                             "(5, 0, CAST(strftime('%s','now') AS INTEGER) * 1000, NULL)"),
              QStringLiteral("CREATE TABLE prompt_templates ("
                             "id TEXT PRIMARY KEY, name TEXT NOT NULL, content TEXT NOT NULL, "
                             "favorite INTEGER NOT NULL DEFAULT 1, position INTEGER NOT NULL)"),
              QStringLiteral("CREATE INDEX prompt_templates_order "
                             "ON prompt_templates(favorite DESC, position ASC, name ASC)"),
              QStringLiteral("UPDATE schema_migrations SET "
                             "applied_at = CAST(strftime('%s','now') AS INTEGER) * 1000, "
                             "completed_at = CAST(strftime('%s','now') AS INTEGER) * 1000 "
                             "WHERE version = 5")}},
            {6,
             {QStringLiteral("INSERT INTO schema_migrations "
                             "(version, applied_at, started_at, completed_at) VALUES "
                             "(6, 0, CAST(strftime('%s','now') AS INTEGER) * 1000, NULL)"),
              QStringLiteral("ALTER TABLE conversations ADD COLUMN "
                             "title_is_placeholder INTEGER NOT NULL DEFAULT 0"),
              QStringLiteral("UPDATE schema_migrations SET "
                             "applied_at = CAST(strftime('%s','now') AS INTEGER) * 1000, "
                             "completed_at = CAST(strftime('%s','now') AS INTEGER) * 1000 "
                             "WHERE version = 6")}},
            {7,
             {QStringLiteral("INSERT INTO schema_migrations "
                             "(version, applied_at, started_at, completed_at) VALUES "
                             "(7, 0, CAST(strftime('%s','now') AS INTEGER) * 1000, NULL)"),
              QStringLiteral("ALTER TABLE conversations ADD COLUMN "
                             "tags TEXT NOT NULL DEFAULT '[]'"),
              QStringLiteral("UPDATE schema_migrations SET "
                             "applied_at = CAST(strftime('%s','now') AS INTEGER) * 1000, "
                             "completed_at = CAST(strftime('%s','now') AS INTEGER) * 1000 "
                             "WHERE version = 7")}},
            {8,
             {QStringLiteral("INSERT INTO schema_migrations "
                             "(version, applied_at, started_at, completed_at) VALUES "
                             "(8, 0, CAST(strftime('%s','now') AS INTEGER) * 1000, NULL)"),
              QStringLiteral("ALTER TABLE conversations ADD COLUMN "
                             "model_id TEXT NOT NULL DEFAULT ''"),
              QStringLiteral("UPDATE schema_migrations SET "
                             "applied_at = CAST(strftime('%s','now') AS INTEGER) * 1000, "
                             "completed_at = CAST(strftime('%s','now') AS INTEGER) * 1000 "
                             "WHERE version = 8")}}};
}

QString compactJson(const QJsonObject& object) {
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

QJsonObject parseObject(const QString& value) {
    const auto document = QJsonDocument::fromJson(value.toUtf8());
    return document.isObject() ? document.object() : QJsonObject{};
}

QString compactStringList(const QStringList& values) {
    QJsonArray array;
    for (const QString& value : values) {
        array.append(value);
    }
    return QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact));
}

QStringList parseStringList(const QString& value) {
    const auto document = QJsonDocument::fromJson(value.toUtf8());
    QStringList result;
    if (!document.isArray()) {
        return result;
    }
    for (const QJsonValue& item : document.array()) {
        if (item.isString()) {
            result.append(item.toString());
        }
    }
    return result;
}

void setSqlError(QString* error, const QString& context, const QSqlError& sqlError) {
    if (error != nullptr) {
        *error = QStringLiteral("%1: %2").arg(context, sqlError.text());
    }
}

} // namespace

EventStore::EventStore()
    : connectionName_(QStringLiteral("snack-event-store-%1")
                          .arg(QUuid::createUuid().toString(QUuid::WithoutBraces))) {}

EventStore::~EventStore() {
    if (database_.isValid()) {
        database_.close();
        database_ = QSqlDatabase{};
    }
    QSqlDatabase::removeDatabase(connectionName_);
}

bool EventStore::open(const QString& databasePath, QString* error) {
    if (error != nullptr) {
        error->clear();
    }
    if (database_.isOpen()) {
        return true;
    }

    mode_ = Mode::Closed;
    migrationBackupPath_.clear();
    recoveryError_.clear();

    const QFileInfo databaseInfo(databasePath);
    if (!QDir().mkpath(databaseInfo.absolutePath())) {
        if (error != nullptr) {
            *error = QStringLiteral("Cannot create database directory: %1")
                         .arg(databaseInfo.absolutePath());
        }
        return false;
    }

    const bool databaseNeedsSafetyBackup = databasePath != QLatin1String(":memory:") &&
                                           databaseInfo.exists() && databaseInfo.size() > 0;

    database_ = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName_);
    database_.setDatabaseName(databasePath);
    if (!database_.open()) {
        setSqlError(error, QStringLiteral("Cannot open event database"), database_.lastError());
        return false;
    }

    if (!execute(QStringLiteral("PRAGMA foreign_keys = ON"), error)) {
        database_.close();
        return false;
    }

    QString versionError;
    const int version = schemaVersion(&versionError);
    if (version < 0) {
        return enterRecoveryMode(versionError, error);
    }
    if (version > currentSchemaVersion) {
        return enterRecoveryMode(
            QStringLiteral("Database schema version %1 is newer than supported version %2")
                .arg(version)
                .arg(currentSchemaVersion),
            error);
    }
    if (version == currentSchemaVersion && !validateSchema(false, &versionError)) {
        return enterRecoveryMode(versionError, error);
    }
    if (!execute(QStringLiteral("PRAGMA journal_mode = WAL"), error) ||
        !execute(QStringLiteral("PRAGMA synchronous = NORMAL"), error)) {
        database_.close();
        return false;
    }
    if (version < currentSchemaVersion && databaseNeedsSafetyBackup &&
        !createMigrationBackup(databasePath, currentSchemaVersion, &versionError)) {
        return enterRecoveryMode(versionError, error);
    }
    if (version < currentSchemaVersion && !applyMigrations(version, &versionError)) {
        return enterRecoveryMode(versionError, error);
    }
    if (version < currentSchemaVersion && !validateSchema(true, &versionError)) {
        return enterRecoveryMode(versionError, error);
    }

    mode_ = Mode::ReadWrite;
    return true;
}

bool EventStore::isOpen() const { return database_.isOpen(); }
EventStore::Mode EventStore::mode() const { return mode_; }
bool EventStore::isReadOnlyRecovery() const { return mode_ == Mode::RecoveryReadOnly; }
QString EventStore::migrationBackupPath() const { return migrationBackupPath_; }
QString EventStore::recoveryError() const { return recoveryError_; }

bool EventStore::saveConversation(const domain::Conversation& conversation, QString* error) {
    if (!ensureWritable(error)) {
        return false;
    }
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "INSERT INTO conversations "
        "(id, title, title_is_placeholder, working_directory, agent_kind, model_id, status, "
        "native_thread_id, native_session_id, archived, pinned, tags) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?) "
        "ON CONFLICT(id) DO UPDATE SET title = excluded.title, "
        "title_is_placeholder = excluded.title_is_placeholder, "
        "working_directory = excluded.working_directory, agent_kind = excluded.agent_kind, "
        "model_id = excluded.model_id, status = excluded.status, "
        "native_thread_id = excluded.native_thread_id, "
        "native_session_id = excluded.native_session_id, archived = excluded.archived, "
        "pinned = excluded.pinned, tags = excluded.tags"));
    query.addBindValue(conversation.id.toString(QUuid::WithoutBraces));
    query.addBindValue(conversation.title);
    query.addBindValue(conversation.titleIsPlaceholder);
    query.addBindValue(conversation.workingDirectory);
    query.addBindValue(domain::enumName(conversation.agentKind));
    query.addBindValue(conversation.modelId.isNull() ? QStringLiteral("") : conversation.modelId);
    query.addBindValue(domain::enumName(conversation.status));
    query.addBindValue(conversation.nativeThreadId);
    query.addBindValue(conversation.nativeSessionId);
    query.addBindValue(conversation.archived);
    query.addBindValue(conversation.pinned);
    query.addBindValue(compactStringList(conversation.tags));
    if (!query.exec()) {
        setSqlError(error, QStringLiteral("Cannot save conversation"), query.lastError());
        return false;
    }
    return true;
}

std::optional<domain::Conversation> EventStore::conversationById(const QUuid& conversationId,
                                                                 QString* error) const {
    QSqlQuery query(database_);
    query.prepare(
        QStringLiteral("SELECT title, title_is_placeholder, working_directory, agent_kind, "
                       "model_id, status, native_thread_id, native_session_id, archived, pinned, "
                       "tags "
                       "FROM conversations WHERE id = ?"));
    query.addBindValue(conversationId.toString(QUuid::WithoutBraces));
    if (!query.exec()) {
        setSqlError(error, QStringLiteral("Cannot load conversation"), query.lastError());
        return std::nullopt;
    }
    if (!query.next()) {
        return std::nullopt;
    }

    domain::Conversation conversation;
    conversation.id = conversationId;
    conversation.title = query.value(0).toString();
    conversation.titleIsPlaceholder = query.value(1).toBool();
    conversation.workingDirectory = query.value(2).toString();
    conversation.agentKind = domain::agentKindFromString(query.value(3).toString());
    conversation.modelId = query.value(4).toString();
    conversation.status = domain::conversationStatusFromString(query.value(5).toString());
    conversation.nativeThreadId = query.value(6).toString();
    conversation.nativeSessionId = query.value(7).toString();
    conversation.archived = query.value(8).toBool();
    conversation.pinned = query.value(9).toBool();
    conversation.tags = parseStringList(query.value(10).toString());
    return conversation;
}

QList<domain::Conversation> EventStore::conversations(QString* error) const {
    QList<domain::Conversation> result;
    QSqlQuery query(database_);
    if (!query.exec(QStringLiteral(
            "SELECT id, title, title_is_placeholder, working_directory, agent_kind, model_id, "
            "status, native_thread_id, native_session_id, archived, pinned, tags "
            "FROM conversations "
            "ORDER BY archived ASC, pinned DESC, title COLLATE NOCASE ASC, id ASC"))) {
        setSqlError(error, QStringLiteral("Cannot load conversations"), query.lastError());
        return result;
    }
    while (query.next()) {
        domain::Conversation conversation;
        conversation.id = QUuid(query.value(0).toString());
        conversation.title = query.value(1).toString();
        conversation.titleIsPlaceholder = query.value(2).toBool();
        conversation.workingDirectory = query.value(3).toString();
        conversation.agentKind = domain::agentKindFromString(query.value(4).toString());
        conversation.modelId = query.value(5).toString();
        conversation.status = domain::conversationStatusFromString(query.value(6).toString());
        conversation.nativeThreadId = query.value(7).toString();
        conversation.nativeSessionId = query.value(8).toString();
        conversation.archived = query.value(9).toBool();
        conversation.pinned = query.value(10).toBool();
        conversation.tags = parseStringList(query.value(11).toString());
        result.append(std::move(conversation));
    }
    return result;
}

bool EventStore::appendEvent(const domain::AgentEvent& event, QString* error) {
    if (!ensureWritable(error)) {
        return false;
    }
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "INSERT INTO events "
        "(id, conversation_id, turn_id, sequence, type, payload, raw_payload, occurred_at) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?)"));
    query.addBindValue(event.id.toString(QUuid::WithoutBraces));
    query.addBindValue(event.conversationId.toString(QUuid::WithoutBraces));
    if (event.turnId.isNull()) {
        query.addBindValue(QVariant{});
    } else {
        query.addBindValue(event.turnId.toString(QUuid::WithoutBraces));
    }
    query.addBindValue(static_cast<qulonglong>(event.sequence));
    query.addBindValue(domain::enumName(event.type));
    query.addBindValue(compactJson(event.payload));
    query.addBindValue(compactJson(event.rawPayload));
    query.addBindValue(event.occurredAt.toMSecsSinceEpoch());
    if (!query.exec()) {
        setSqlError(error, QStringLiteral("Cannot append event"), query.lastError());
        return false;
    }
    return true;
}

QList<domain::AgentEvent> EventStore::eventsForConversation(const QUuid& conversationId,
                                                            QString* error) const {
    QList<domain::AgentEvent> events;
    QSqlQuery query(database_);
    query.prepare(
        QStringLiteral("SELECT id, turn_id, sequence, type, payload, raw_payload, occurred_at "
                       "FROM events WHERE conversation_id = ? ORDER BY sequence ASC"));
    query.addBindValue(conversationId.toString(QUuid::WithoutBraces));
    if (!query.exec()) {
        setSqlError(error, QStringLiteral("Cannot load events"), query.lastError());
        return events;
    }

    while (query.next()) {
        domain::AgentEvent event;
        event.id = QUuid(query.value(0).toString());
        event.conversationId = conversationId;
        event.turnId = QUuid(query.value(1).toString());
        event.sequence = query.value(2).toULongLong();
        event.type = domain::agentEventTypeFromString(query.value(3).toString());
        event.payload = parseObject(query.value(4).toString());
        event.rawPayload = parseObject(query.value(5).toString());
        event.occurredAt =
            QDateTime::fromMSecsSinceEpoch(query.value(6).toLongLong(), QTimeZone::UTC);
        events.append(event);
    }
    return events;
}

bool EventStore::replaceQueuedMessages(const QUuid& conversationId,
                                       const QList<domain::QueuedMessage>& messages,
                                       QString* error) {
    if (!ensureWritable(error)) {
        return false;
    }
    for (qsizetype index = 0; index < messages.size(); ++index) {
        const auto& message = messages.at(index);
        if (message.id.isNull() || message.conversationId != conversationId ||
            message.content.trimmed().isEmpty() || message.position != index) {
            if (error != nullptr) {
                *error = QStringLiteral("Cannot replace queued messages: invalid queue entry");
            }
            return false;
        }
    }
    if (!database_.transaction()) {
        setSqlError(error, QStringLiteral("Cannot start queued message transaction"),
                    database_.lastError());
        return false;
    }

    QSqlQuery remove(database_);
    remove.prepare(QStringLiteral("DELETE FROM queued_messages WHERE conversation_id = ?"));
    remove.addBindValue(conversationId.toString(QUuid::WithoutBraces));
    if (!remove.exec()) {
        setSqlError(error, QStringLiteral("Cannot replace queued messages"), remove.lastError());
        database_.rollback();
        return false;
    }

    for (const auto& message : messages) {
        QSqlQuery insert(database_);
        insert.prepare(QStringLiteral("INSERT INTO queued_messages "
                                      "(id, conversation_id, content, attachments, position, "
                                      "state) VALUES (?, ?, ?, ?, ?, ?)"));
        insert.addBindValue(message.id.toString(QUuid::WithoutBraces));
        insert.addBindValue(conversationId.toString(QUuid::WithoutBraces));
        insert.addBindValue(message.content);
        insert.addBindValue(
            QString::fromUtf8(QJsonDocument(message.attachments).toJson(QJsonDocument::Compact)));
        insert.addBindValue(message.position);
        insert.addBindValue(domain::enumName(message.state));
        if (!insert.exec()) {
            setSqlError(error, QStringLiteral("Cannot replace queued messages"),
                        insert.lastError());
            database_.rollback();
            return false;
        }
    }
    if (!database_.commit()) {
        setSqlError(error, QStringLiteral("Cannot commit queued messages"), database_.lastError());
        database_.rollback();
        return false;
    }
    return true;
}

QList<domain::QueuedMessage> EventStore::queuedMessagesForConversation(const QUuid& conversationId,
                                                                       QString* error) const {
    QList<domain::QueuedMessage> messages;
    QSqlQuery query(database_);
    query.prepare(
        QStringLiteral("SELECT id, content, attachments, position, state FROM queued_messages "
                       "WHERE conversation_id = ? ORDER BY position ASC"));
    query.addBindValue(conversationId.toString(QUuid::WithoutBraces));
    if (!query.exec()) {
        setSqlError(error, QStringLiteral("Cannot load queued messages"), query.lastError());
        return messages;
    }
    while (query.next()) {
        domain::QueuedMessage message;
        message.id = QUuid(query.value(0).toString());
        message.conversationId = conversationId;
        message.content = query.value(1).toString();
        const QJsonDocument attachments = QJsonDocument::fromJson(query.value(2).toByteArray());
        message.attachments = attachments.isArray() ? attachments.array() : QJsonArray{};
        message.position = query.value(3).toLongLong();
        message.state = domain::queuedMessageStateFromString(query.value(4).toString());
        messages.append(message);
    }
    return messages;
}

bool EventStore::savePromptTemplate(const domain::PromptTemplate& promptTemplate, QString* error) {
    if (!ensureWritable(error) || !domain::PromptTemplateEngine::validate(promptTemplate, error)) {
        return false;
    }
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "INSERT INTO prompt_templates (id, name, content, favorite, position) VALUES (?, ?, ?, ?, "
        "?) "
        "ON CONFLICT(id) DO UPDATE SET name = excluded.name, content = excluded.content, "
        "favorite = excluded.favorite, position = excluded.position"));
    query.addBindValue(promptTemplate.id.toString(QUuid::WithoutBraces));
    query.addBindValue(promptTemplate.name.trimmed());
    query.addBindValue(promptTemplate.content);
    query.addBindValue(promptTemplate.favorite);
    query.addBindValue(promptTemplate.position);
    if (!query.exec()) {
        setSqlError(error, QStringLiteral("Cannot save prompt template"), query.lastError());
        return false;
    }
    return true;
}

bool EventStore::deletePromptTemplate(const QUuid& templateId, QString* error) {
    if (!ensureWritable(error)) {
        return false;
    }
    if (templateId.isNull()) {
        if (error != nullptr) {
            *error = QStringLiteral("Cannot delete prompt template: invalid ID");
        }
        return false;
    }
    QSqlQuery query(database_);
    query.prepare(QStringLiteral("DELETE FROM prompt_templates WHERE id = ?"));
    query.addBindValue(templateId.toString(QUuid::WithoutBraces));
    if (!query.exec()) {
        setSqlError(error, QStringLiteral("Cannot delete prompt template"), query.lastError());
        return false;
    }
    if (query.numRowsAffected() != 1) {
        if (error != nullptr) {
            *error = QStringLiteral("Prompt template no longer exists");
        }
        return false;
    }
    return true;
}

QList<domain::PromptTemplate> EventStore::promptTemplates(QString* error) const {
    QList<domain::PromptTemplate> templates;
    QSqlQuery query(database_);
    if (!query.exec(QStringLiteral("SELECT id, name, content, favorite, position "
                                   "FROM prompt_templates "
                                   "ORDER BY favorite DESC, position ASC, name ASC"))) {
        setSqlError(error, QStringLiteral("Cannot load prompt templates"), query.lastError());
        return templates;
    }
    while (query.next()) {
        domain::PromptTemplate promptTemplate;
        promptTemplate.id = QUuid(query.value(0).toString());
        promptTemplate.name = query.value(1).toString();
        promptTemplate.content = query.value(2).toString();
        promptTemplate.favorite = query.value(3).toBool();
        promptTemplate.position = query.value(4).toLongLong();
        templates.append(promptTemplate);
    }
    return templates;
}

int EventStore::schemaVersion(QString* error) const {
    QSqlQuery tableQuery(database_);
    if (!tableQuery.exec(QStringLiteral(
            "SELECT 1 FROM sqlite_master WHERE type = 'table' AND name = 'schema_migrations'"))) {
        setSqlError(error, QStringLiteral("Cannot inspect database schema"),
                    tableQuery.lastError());
        return -1;
    }
    if (!tableQuery.next()) {
        return 0;
    }

    QSqlQuery versionQuery(database_);
    if (!versionQuery.exec(QStringLiteral("SELECT COALESCE(MAX(version), 0) "
                                          "FROM schema_migrations")) ||
        !versionQuery.next()) {
        setSqlError(error, QStringLiteral("Cannot read database schema version"),
                    versionQuery.lastError());
        return -1;
    }
    return versionQuery.value(0).toInt();
}

bool EventStore::createMigrationBackup(const QString& databasePath, int targetVersion,
                                       QString* error) {
    const QString timestamp =
        QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-HHmmsszzz"));
    const QString uniqueSuffix = QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
    migrationBackupPath_ = QStringLiteral("%1.pre-migration-v%2-%3-%4.bak")
                               .arg(databasePath)
                               .arg(targetVersion)
                               .arg(timestamp, uniqueSuffix);

    QSqlQuery query(database_);
    query.prepare(QStringLiteral("VACUUM INTO ?"));
    query.addBindValue(migrationBackupPath_);
    if (!query.exec()) {
        setSqlError(error, QStringLiteral("Cannot create pre-migration database backup"),
                    query.lastError());
        migrationBackupPath_.clear();
        return false;
    }
    return true;
}

bool EventStore::applyMigrations(int currentVersion, QString* error) {
    if (!database_.transaction()) {
        setSqlError(error, QStringLiteral("Cannot start database migration"),
                    database_.lastError());
        return false;
    }

    for (const auto& migration : migrations()) {
        if (migration.version <= currentVersion) {
            continue;
        }
        for (const auto& statement : migration.statements) {
            if (!execute(statement, error)) {
                database_.rollback();
                return false;
            }
        }
    }
    if (!database_.commit()) {
        setSqlError(error, QStringLiteral("Cannot commit database migration"),
                    database_.lastError());
        database_.rollback();
        return false;
    }
    return true;
}

bool EventStore::validateSchema(bool checkIntegrity, QString* error) const {
    QSqlQuery objectsQuery(database_);
    if (!objectsQuery.exec(QStringLiteral(
            "SELECT COUNT(*) FROM sqlite_master WHERE "
            "(type = 'table' AND name IN ('schema_migrations', 'conversations', 'events', "
            "'queued_messages', 'prompt_templates')) OR "
            "(type = 'index' AND name IN ('events_conversation_sequence', "
            "'conversations_working_directory', 'prompt_templates_order'))")) ||
        !objectsQuery.next() || objectsQuery.value(0).toInt() != 8) {
        if (error != nullptr) {
            *error = QStringLiteral("Database schema validation failed: required objects are "
                                    "missing");
        }
        return false;
    }

    QSqlQuery completionQuery(database_);
    if (!completionQuery.exec(QStringLiteral("SELECT COUNT(*) FROM schema_migrations "
                                             "WHERE version IN (2, 3, 4, 5, 6, 7, 8) "
                                             "AND completed_at IS NOT NULL")) ||
        !completionQuery.next() || completionQuery.value(0).toInt() != 7) {
        if (error != nullptr) {
            *error = QStringLiteral("Database schema validation failed: migrations are incomplete");
        }
        return false;
    }
    QSqlQuery modelColumnQuery(database_);
    if (!modelColumnQuery.exec(
            QStringLiteral("SELECT COUNT(*) FROM pragma_table_info('conversations') "
                           "WHERE name = 'model_id' AND \"notnull\" = 1")) ||
        !modelColumnQuery.next() || modelColumnQuery.value(0).toInt() != 1) {
        if (error != nullptr) {
            *error = QStringLiteral(
                "Database schema validation failed: conversation model column is missing");
        }
        return false;
    }
    QSqlQuery tagsColumnQuery(database_);
    if (!tagsColumnQuery.exec(
            QStringLiteral("SELECT COUNT(*) FROM pragma_table_info('conversations') "
                           "WHERE name = 'tags' AND \"notnull\" = 1")) ||
        !tagsColumnQuery.next() || tagsColumnQuery.value(0).toInt() != 1) {
        if (error != nullptr) {
            *error = QStringLiteral(
                "Database schema validation failed: conversation tags column is missing");
        }
        return false;
    }
    QSqlQuery identityColumnQuery(database_);
    if (!identityColumnQuery.exec(
            QStringLiteral("SELECT COUNT(*) FROM pragma_table_info('conversations') "
                           "WHERE name = 'native_thread_id'")) ||
        !identityColumnQuery.next() || identityColumnQuery.value(0).toInt() != 1) {
        if (error != nullptr) {
            *error = QStringLiteral("Database schema validation failed: native thread identity "
                                    "column is missing");
        }
        return false;
    }
    QSqlQuery titleColumnQuery(database_);
    if (!titleColumnQuery.exec(
            QStringLiteral("SELECT COUNT(*) FROM pragma_table_info('conversations') "
                           "WHERE name = 'title_is_placeholder' AND \"notnull\" = 1")) ||
        !titleColumnQuery.next() || titleColumnQuery.value(0).toInt() != 1) {
        if (error != nullptr) {
            *error = QStringLiteral("Database schema validation failed: title placeholder "
                                    "column is missing");
        }
        return false;
    }
    if (!checkIntegrity) {
        return true;
    }

    QSqlQuery integrityQuery(database_);
    if (!integrityQuery.exec(QStringLiteral("PRAGMA quick_check(1)")) || !integrityQuery.next() ||
        integrityQuery.value(0).toString() != QLatin1String("ok")) {
        if (error != nullptr) {
            *error = QStringLiteral("Database integrity check failed after migration");
        }
        return false;
    }

    QSqlQuery foreignKeyQuery(database_);
    if (!foreignKeyQuery.exec(QStringLiteral("PRAGMA foreign_key_check"))) {
        setSqlError(error, QStringLiteral("Cannot validate database foreign keys"),
                    foreignKeyQuery.lastError());
        return false;
    }
    if (foreignKeyQuery.next()) {
        if (error != nullptr) {
            *error = QStringLiteral("Database foreign key check failed after migration");
        }
        return false;
    }
    return true;
}

bool EventStore::enterRecoveryMode(const QString& reason, QString* error) {
    database_.close();
    database_.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
    if (!database_.open()) {
        setSqlError(error, QStringLiteral("Cannot reopen database for read-only recovery"),
                    database_.lastError());
        mode_ = Mode::Closed;
        return false;
    }
    if (!execute(QStringLiteral("PRAGMA foreign_keys = ON"), error)) {
        database_.close();
        mode_ = Mode::Closed;
        return false;
    }

    mode_ = Mode::RecoveryReadOnly;
    recoveryError_ = reason;
    if (error != nullptr) {
        *error = reason;
    }
    return true;
}

bool EventStore::ensureWritable(QString* error) const {
    if (mode_ == Mode::ReadWrite) {
        return true;
    }
    if (error != nullptr) {
        *error =
            mode_ == Mode::RecoveryReadOnly
                ? QStringLiteral("Database is in read-only recovery mode: %1").arg(recoveryError_)
                : QStringLiteral("Database is not open for writing");
    }
    return false;
}

bool EventStore::execute(const QString& statement, QString* error) {
    QSqlQuery query(database_);
    if (!query.exec(statement)) {
        setSqlError(error, QStringLiteral("SQL statement failed"), query.lastError());
        return false;
    }
    return true;
}

} // namespace snack::storage
