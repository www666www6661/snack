#pragma once

#include "domain/DomainTypes.h"

#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QMetaType>
#include <QString>

#include <optional>

namespace snack::agent::codex {

struct CodexThreadInfo {
    QString id;
    QString sessionId;
    QString workingDirectory;
    QJsonObject raw;
};

struct CodexThreadPage {
    QList<CodexThreadInfo> threads;
    QString nextCursor;
    QString backwardsCursor;
};

[[nodiscard]] std::optional<CodexThreadInfo> parseThreadLifecycleResponse(const QJsonValue& result,
                                                                          QString* error = nullptr);
[[nodiscard]] QJsonObject threadAccessParameters(domain::AccessLevel accessLevel);
[[nodiscard]] QJsonObject makeThreadListParameters(const QString& workingDirectory,
                                                   const QString& cursor = {}, quint32 limit = 100);
[[nodiscard]] QJsonObject makeThreadReadParameters(const QString& threadId,
                                                   bool includeTurns = true);
[[nodiscard]] std::optional<CodexThreadPage> parseThreadListResponse(const QJsonValue& result,
                                                                     QString* error = nullptr);

} // namespace snack::agent::codex

Q_DECLARE_METATYPE(snack::agent::codex::CodexThreadInfo)
Q_DECLARE_METATYPE(snack::agent::codex::CodexThreadPage)
