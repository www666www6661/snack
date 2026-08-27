#pragma once

#include <QJsonObject>
#include <QString>

#include <optional>

namespace snack::agent::codex {

struct CodexAccountInfo {
    QString type;
    QString planType;
    bool hasAccount{false};
    bool requiresOpenaiAuth{false};

    [[nodiscard]] bool canRun() const { return hasAccount || !requiresOpenaiAuth; }
};

[[nodiscard]] QJsonObject accountReadParameters();
[[nodiscard]] std::optional<CodexAccountInfo> parseAccountReadResponse(const QJsonValue& value,
                                                                       QString* error = nullptr);

} // namespace snack::agent::codex
