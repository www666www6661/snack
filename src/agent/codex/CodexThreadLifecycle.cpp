#include "agent/codex/CodexThreadLifecycle.h"

#include <QJsonArray>

namespace snack::agent::codex {
namespace {

std::optional<CodexThreadInfo> fail(QString* error, const QString& detail) {
    if (error != nullptr) {
        *error = detail;
    }
    return std::nullopt;
}

std::optional<CodexThreadInfo> parseThreadObject(const QJsonValue& value, QString* error) {
    if (!value.isObject()) {
        return fail(error, QStringLiteral("Thread must be an object"));
    }
    const QJsonObject thread = value.toObject();
    const QJsonValue idValue = thread.value(QStringLiteral("id"));
    const QJsonValue sessionIdValue = thread.value(QStringLiteral("sessionId"));
    const QJsonValue cwdValue = thread.value(QStringLiteral("cwd"));
    if (!idValue.isString() || idValue.toString().isEmpty()) {
        return fail(error, QStringLiteral("Codex thread id is missing"));
    }
    if (!sessionIdValue.isString() || sessionIdValue.toString().isEmpty()) {
        return fail(error, QStringLiteral("Codex thread sessionId is missing"));
    }
    if (!cwdValue.isString() || cwdValue.toString().isEmpty()) {
        return fail(error, QStringLiteral("Codex thread cwd is missing"));
    }
    return CodexThreadInfo{.id = idValue.toString(),
                           .sessionId = sessionIdValue.toString(),
                           .workingDirectory = cwdValue.toString(),
                           .raw = thread};
}

bool readCursor(const QJsonObject& object, const QString& name, QString* value, QString* error) {
    const QJsonValue cursor = object.value(name);
    if (cursor.isUndefined() || cursor.isNull()) {
        value->clear();
        return true;
    }
    if (!cursor.isString()) {
        if (error != nullptr) {
            *error = QStringLiteral("Thread page %1 must be a string or null").arg(name);
        }
        return false;
    }
    *value = cursor.toString();
    return true;
}

} // namespace

std::optional<CodexThreadInfo> parseThreadLifecycleResponse(const QJsonValue& result,
                                                            QString* error) {
    if (!result.isObject()) {
        return fail(error, QStringLiteral("Thread lifecycle result must be an object"));
    }
    const QJsonValue threadValue = result.toObject().value(QStringLiteral("thread"));
    if (!threadValue.isObject()) {
        return fail(error, QStringLiteral("Thread lifecycle result is missing thread"));
    }
    return parseThreadObject(threadValue, error);
}

QJsonObject makeThreadListParameters(const QString& workingDirectory, const QString& cursor,
                                     quint32 limit) {
    QJsonObject result{{QStringLiteral("cwd"), workingDirectory},
                       {QStringLiteral("limit"), static_cast<qint64>(limit)},
                       {QStringLiteral("archived"), false},
                       {QStringLiteral("sourceKinds"), QJsonArray{QStringLiteral("appServer")}},
                       {QStringLiteral("sortKey"), QStringLiteral("updated_at")},
                       {QStringLiteral("sortDirection"), QStringLiteral("desc")}};
    if (!cursor.isEmpty()) {
        result.insert(QStringLiteral("cursor"), cursor);
    }
    return result;
}

QJsonObject makeThreadReadParameters(const QString& threadId, bool includeTurns) {
    return {{QStringLiteral("threadId"), threadId}, {QStringLiteral("includeTurns"), includeTurns}};
}

std::optional<CodexThreadPage> parseThreadListResponse(const QJsonValue& result, QString* error) {
    if (!result.isObject()) {
        if (error != nullptr) {
            *error = QStringLiteral("Thread list result must be an object");
        }
        return std::nullopt;
    }
    const QJsonObject object = result.toObject();
    if (!object.value(QStringLiteral("data")).isArray()) {
        if (error != nullptr) {
            *error = QStringLiteral("Thread list result is missing data");
        }
        return std::nullopt;
    }
    CodexThreadPage page;
    if (!readCursor(object, QStringLiteral("nextCursor"), &page.nextCursor, error) ||
        !readCursor(object, QStringLiteral("backwardsCursor"), &page.backwardsCursor, error)) {
        return std::nullopt;
    }
    for (const QJsonValue& value : object.value(QStringLiteral("data")).toArray()) {
        const auto thread = parseThreadObject(value, error);
        if (!thread.has_value()) {
            return std::nullopt;
        }
        page.threads.append(*thread);
    }
    return page;
}

QJsonObject threadAccessParameters(domain::AccessLevel accessLevel) {
    switch (accessLevel) {
    case domain::AccessLevel::Strict:
        return {{QStringLiteral("approvalPolicy"), QStringLiteral("untrusted")},
                {QStringLiteral("sandbox"), QStringLiteral("read-only")}};
    case domain::AccessLevel::Workspace:
        return {{QStringLiteral("approvalPolicy"), QStringLiteral("on-request")},
                {QStringLiteral("sandbox"), QStringLiteral("workspace-write")}};
    case domain::AccessLevel::Full:
        return {{QStringLiteral("approvalPolicy"), QStringLiteral("never")},
                {QStringLiteral("sandbox"), QStringLiteral("danger-full-access")}};
    }
    return {};
}

} // namespace snack::agent::codex
