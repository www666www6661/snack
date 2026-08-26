#pragma once

#include "domain/DomainTypes.h"

#include <QJsonValue>
#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

namespace snack::agent::codex {

struct CodexReasoningEffort {
    QString id;
    QString description;
};

struct CodexModelInfo {
    QString id;
    QString model;
    QString displayName;
    QString description;
    QString defaultReasoningEffortId;
    QList<CodexReasoningEffort> supportedReasoningEfforts;
    QStringList inputModalities;
    bool hidden{false};
    bool supportsPersonality{false};
    bool isDefault{false};
};

struct CodexModelPage {
    QList<CodexModelInfo> models;
    QString nextCursor;
    bool hasNextPage{false};
};

[[nodiscard]] std::optional<CodexModelPage> parseModelPage(const QJsonValue& result,
                                                           QString* error = nullptr);
[[nodiscard]] std::optional<domain::ReasoningEffort> reasoningEffortFromCodex(const QString& id);

} // namespace snack::agent::codex
