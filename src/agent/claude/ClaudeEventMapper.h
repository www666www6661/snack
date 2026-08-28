#pragma once

#include "agent/claude/ClaudeStreamProtocol.h"
#include "domain/DomainTypes.h"

#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QSet>

namespace snack::agent::claude {

struct MappedEvent {
    domain::AgentEventType type{domain::AgentEventType::RawProtocolObserved};
    QJsonObject payload;
    QJsonObject rawPayload;
};

class ClaudeEventMapper final {
  public:
    void reset();
    [[nodiscard]] QList<MappedEvent> consume(const StreamRecord& record);

  private:
    struct ContentBlock {
        QString type;
        QString itemId;
        QString name;
        QString text;
        QByteArray partialJson;
    };

    void ensureMessageStarted(QList<MappedEvent>* events, const QJsonObject& raw);
    void appendText(const QString& text, QList<MappedEvent>* events, const QJsonObject& raw);
    void startTool(const ContentBlock& block, const QJsonObject& input, QList<MappedEvent>* events,
                   const QJsonObject& raw);
    void appendUsage(const QJsonObject& usage, QList<MappedEvent>* events,
                     const QJsonObject& raw) const;
    void consumePartial(const QJsonObject& payload, QList<MappedEvent>* events);
    void consumeAssistant(const QJsonObject& payload, QList<MappedEvent>* events);
    void consumeUser(const QJsonObject& payload, QList<MappedEvent>* events);

    QHash<qint64, ContentBlock> blocks_;
    QSet<QString> startedTools_;
    QSet<QString> completedTools_;
    QSet<QString> startedReasoning_;
    QString messageItemId_;
    QString streamedText_;
    bool messageStarted_{false};
    bool messageCompleted_{false};
};

[[nodiscard]] QJsonObject sanitizedClaudePayload(const QJsonObject& payload);

} // namespace snack::agent::claude
