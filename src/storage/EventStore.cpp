#include "storage/EventStore.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSqlError>
#include <QSqlQuery>
#include <QTimeZone>
#include <QVariant>

namespace snack::storage {
namespace {

QString compactJson(const QJsonObject& object) {
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

QJsonObject parseObject(const QString& value) {
    const auto document = QJsonDocument::fromJson(value.toUtf8());
    return document.isObject() ? document.object() : QJsonObject{};
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
    if (database_.isOpen()) {
        return true;
    }

    const QFileInfo databaseInfo(databasePath);
    if (!QDir().mkpath(databaseInfo.absolutePath())) {
        if (error != nullptr) {
            *error = QStringLiteral("Cannot create database directory: %1")
                         .arg(databaseInfo.absolutePath());
        }
        return false;
    }

    database_ = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName_);
    database_.setDatabaseName(databasePath);
    if (!database_.open()) {
        setSqlError(error, QStringLiteral("Cannot open event database"), database_.lastError());
        return false;
    }

    if (!execute(QStringLiteral("PRAGMA foreign_keys = ON"), error) ||
        !execute(QStringLiteral("PRAGMA journal_mode = WAL"), error) ||
        !execute(QStringLiteral("PRAGMA synchronous = NORMAL"), error)) {
        return false;
    }
    return applyMigrations(error);
}

bool EventStore::isOpen() const { return database_.isOpen(); }

bool EventStore::saveConversation(const domain::Conversation& conversation, QString* error) {
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "INSERT INTO conversations "
        "(id, title, working_directory, agent_kind, status, native_session_id, archived, pinned) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?) "
        "ON CONFLICT(id) DO UPDATE SET title = excluded.title, "
        "working_directory = excluded.working_directory, agent_kind = excluded.agent_kind, "
        "status = excluded.status, native_session_id = excluded.native_session_id, "
        "archived = excluded.archived, pinned = excluded.pinned"));
    query.addBindValue(conversation.id.toString(QUuid::WithoutBraces));
    query.addBindValue(conversation.title);
    query.addBindValue(conversation.workingDirectory);
    query.addBindValue(domain::enumName(conversation.agentKind));
    query.addBindValue(domain::enumName(conversation.status));
    query.addBindValue(conversation.nativeSessionId);
    query.addBindValue(conversation.archived);
    query.addBindValue(conversation.pinned);
    if (!query.exec()) {
        setSqlError(error, QStringLiteral("Cannot save conversation"), query.lastError());
        return false;
    }
    return true;
}

bool EventStore::appendEvent(const domain::AgentEvent& event, QString* error) {
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

bool EventStore::applyMigrations(QString* error) {
    if (!database_.transaction()) {
        setSqlError(error, QStringLiteral("Cannot start database migration"),
                    database_.lastError());
        return false;
    }

    const QStringList statements = {
        QStringLiteral("CREATE TABLE IF NOT EXISTS schema_migrations ("
                       "version INTEGER PRIMARY KEY, applied_at INTEGER NOT NULL)"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS conversations ("
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
        QStringLiteral("INSERT OR IGNORE INTO schema_migrations(version, applied_at) "
                       "VALUES (1, CAST(strftime('%s','now') AS INTEGER) * 1000)")};

    for (const auto& statement : statements) {
        if (!execute(statement, error)) {
            database_.rollback();
            return false;
        }
    }
    if (!database_.commit()) {
        setSqlError(error, QStringLiteral("Cannot commit database migration"),
                    database_.lastError());
        return false;
    }
    return true;
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
