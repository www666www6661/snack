#include "agent/codex/CodexThreadLifecycle.h"

namespace snack::agent::codex {
namespace {

std::optional<CodexThreadInfo> fail(QString* error, const QString& detail) {
    if (error != nullptr) {
        *error = detail;
    }
    return std::nullopt;
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
    const QJsonObject thread = threadValue.toObject();
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
