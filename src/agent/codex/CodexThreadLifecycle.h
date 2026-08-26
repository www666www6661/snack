#pragma once

#include "domain/DomainTypes.h"

#include <QJsonObject>
#include <QJsonValue>
#include <QString>

#include <optional>

namespace snack::agent::codex {

struct CodexThreadInfo {
    QString id;
    QString sessionId;
    QString workingDirectory;
    QJsonObject raw;
};

[[nodiscard]] std::optional<CodexThreadInfo> parseThreadLifecycleResponse(const QJsonValue& result,
                                                                          QString* error = nullptr);
[[nodiscard]] QJsonObject threadAccessParameters(domain::AccessLevel accessLevel);

} // namespace snack::agent::codex
