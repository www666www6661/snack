#include "agent/claude/ClaudeEventMapper.h"

#include <QJsonArray>
#include <QJsonDocument>

namespace snack::agent::claude {
namespace {

QString compactValue(const QJsonValue& value) {
    if (value.isString()) {
        return value.toString();
    }
    if (value.isArray()) {
        QStringList text;
        for (const QJsonValue& blockValue : value.toArray()) {
            const QJsonObject block = blockValue.toObject();
            if (block.value(QStringLiteral("text")).isString()) {
                text.append(block.value(QStringLiteral("text")).toString());
            }
        }
        if (!text.isEmpty()) {
            return text.join(QLatin1Char('\n'));
        }
        return QString::fromUtf8(QJsonDocument(value.toArray()).toJson(QJsonDocument::Compact));
    }
    if (value.isObject()) {
        return QString::fromUtf8(QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact));
    }
    return {};
}

QJsonObject toolPayload(const QString& itemId, const QString& name, const QJsonObject& input) {
    return {{QStringLiteral("itemId"), itemId},
            {QStringLiteral("kind"), QStringLiteral("dynamicToolCall")},
            {QStringLiteral("tool"), name},
            {QStringLiteral("arguments"), input},
            {QStringLiteral("status"), QStringLiteral("running")}};
}

} // namespace

void ClaudeEventMapper::reset() {
    blocks_.clear();
    startedTools_.clear();
    completedTools_.clear();
    startedReasoning_.clear();
    messageItemId_.clear();
    streamedText_.clear();
    messageStarted_ = false;
    messageCompleted_ = false;
}

QList<MappedEvent> ClaudeEventMapper::consume(const StreamRecord& record) {
    QList<MappedEvent> events;
    const QJsonObject raw = sanitizedClaudePayload(record.payload);
    switch (record.kind) {
    case StreamRecordKind::PartialAssistant:
        consumePartial(record.payload, &events);
        break;
    case StreamRecordKind::Assistant:
        consumeAssistant(record.payload, &events);
        break;
    case StreamRecordKind::User:
        consumeUser(record.payload, &events);
        break;
    case StreamRecordKind::Result:
        appendUsage(record.payload.value(QStringLiteral("usage")).toObject(), &events, raw);
        break;
    case StreamRecordKind::SystemEvent:
    case StreamRecordKind::Unknown:
        events.append({.type = domain::AgentEventType::RawProtocolObserved, .rawPayload = raw});
        break;
    case StreamRecordKind::SystemInit:
    case StreamRecordKind::Malformed:
        break;
    }
    return events;
}

void ClaudeEventMapper::ensureMessageStarted(QList<MappedEvent>* events, const QJsonObject& raw) {
    if (messageStarted_) {
        return;
    }
    if (messageItemId_.isEmpty()) {
        messageItemId_ = QStringLiteral("claude-agent-message");
    }
    messageStarted_ = true;
    events->append({.type = domain::AgentEventType::AgentMessageStart,
                    .payload = {{QStringLiteral("itemId"), messageItemId_}},
                    .rawPayload = raw});
}

void ClaudeEventMapper::appendText(const QString& text, QList<MappedEvent>* events,
                                   const QJsonObject& raw) {
    if (text.isEmpty() || messageCompleted_) {
        return;
    }
    ensureMessageStarted(events, raw);
    streamedText_.append(text);
    events->append(
        {.type = domain::AgentEventType::AgentMessageDelta,
         .payload = {{QStringLiteral("itemId"), messageItemId_}, {QStringLiteral("text"), text}},
         .rawPayload = raw});
}

void ClaudeEventMapper::startTool(const ContentBlock& block, const QJsonObject& input,
                                  QList<MappedEvent>* events, const QJsonObject& raw) {
    if (block.itemId.isEmpty() || startedTools_.contains(block.itemId)) {
        return;
    }
    startedTools_.insert(block.itemId);
    events->append({.type = domain::AgentEventType::ToolStarted,
                    .payload = toolPayload(block.itemId, block.name, input),
                    .rawPayload = raw});
}

void ClaudeEventMapper::appendUsage(const QJsonObject& usage, QList<MappedEvent>* events,
                                    const QJsonObject& raw) const {
    if (usage.isEmpty()) {
        return;
    }
    const qint64 input = usage.value(QStringLiteral("input_tokens")).toInteger();
    const qint64 output = usage.value(QStringLiteral("output_tokens")).toInteger();
    const qint64 cacheRead = usage.value(QStringLiteral("cache_read_input_tokens")).toInteger();
    const qint64 cacheCreation =
        usage.value(QStringLiteral("cache_creation_input_tokens")).toInteger();
    events->append(
        {.type = domain::AgentEventType::UsageUpdated,
         .payload = {{QStringLiteral("total"),
                      QJsonObject{{QStringLiteral("inputTokens"), input},
                                  {QStringLiteral("cachedInputTokens"), cacheRead + cacheCreation},
                                  {QStringLiteral("outputTokens"), output},
                                  {QStringLiteral("reasoningOutputTokens"), 0},
                                  {QStringLiteral("totalTokens"),
                                   input + output + cacheRead + cacheCreation}}},
                     {QStringLiteral("modelContextWindow"), QJsonValue::Null}},
         .rawPayload = raw});
}

void ClaudeEventMapper::consumePartial(const QJsonObject& payload, QList<MappedEvent>* events) {
    const QJsonObject event = payload.value(QStringLiteral("event")).toObject();
    const QString eventType = event.value(QStringLiteral("type")).toString();
    const QJsonObject raw = sanitizedClaudePayload(payload);
    if (eventType == QLatin1String("message_start")) {
        const QJsonObject message = event.value(QStringLiteral("message")).toObject();
        messageItemId_ = message.value(QStringLiteral("id")).toString();
        appendUsage(message.value(QStringLiteral("usage")).toObject(), events, raw);
        return;
    }
    if (eventType == QLatin1String("message_delta")) {
        appendUsage(event.value(QStringLiteral("usage")).toObject(), events, raw);
        return;
    }

    const QJsonValue indexValue = event.value(QStringLiteral("index"));
    const qint64 index = indexValue.toInteger(-1);
    if (!indexValue.isDouble() || index < 0) {
        events->append({.type = domain::AgentEventType::WarningRaised,
                        .payload = {{QStringLiteral("message"),
                                     QStringLiteral("Ignored a Claude stream event with an invalid "
                                                    "content index")}},
                        .rawPayload = raw});
        return;
    }

    if (eventType == QLatin1String("content_block_start")) {
        const QJsonObject content = event.value(QStringLiteral("content_block")).toObject();
        ContentBlock block{.type = content.value(QStringLiteral("type")).toString(),
                           .itemId = content.value(QStringLiteral("id")).toString(),
                           .name = content.value(QStringLiteral("name")).toString(),
                           .text = content.value(QStringLiteral("text")).toString()};
        blocks_.insert(index, block);
        if (block.type == QLatin1String("text")) {
            ensureMessageStarted(events, raw);
            appendText(block.text, events, raw);
        } else if (block.type == QLatin1String("tool_use")) {
            startTool(block, content.value(QStringLiteral("input")).toObject(), events, raw);
        } else if (block.type == QLatin1String("thinking")) {
            const QString itemId = QStringLiteral("claude-reasoning-%1").arg(index);
            startedReasoning_.insert(itemId);
            events->append({.type = domain::AgentEventType::ReasoningStarted,
                            .payload = {{QStringLiteral("itemId"), itemId}}});
        }
        return;
    }

    auto block = blocks_.find(index);
    if (block == blocks_.end()) {
        events->append({.type = domain::AgentEventType::WarningRaised,
                        .payload = {{QStringLiteral("message"),
                                     QStringLiteral("Ignored a Claude delta for an unknown content "
                                                    "block")}},
                        .rawPayload = raw});
        return;
    }
    if (eventType == QLatin1String("content_block_delta")) {
        const QJsonObject delta = event.value(QStringLiteral("delta")).toObject();
        const QString deltaType = delta.value(QStringLiteral("type")).toString();
        if (deltaType == QLatin1String("text_delta") && block->type == QLatin1String("text")) {
            const QString text = delta.value(QStringLiteral("text")).toString();
            block->text.append(text);
            appendText(text, events, raw);
        } else if (deltaType == QLatin1String("input_json_delta") &&
                   block->type == QLatin1String("tool_use")) {
            block->partialJson.append(
                delta.value(QStringLiteral("partial_json")).toString().toUtf8());
        }
        return;
    }
    if (eventType == QLatin1String("content_block_stop")) {
        if (block->type == QLatin1String("thinking")) {
            const QString itemId = QStringLiteral("claude-reasoning-%1").arg(index);
            if (startedReasoning_.remove(itemId)) {
                events->append({.type = domain::AgentEventType::ReasoningCompleted,
                                .payload = {{QStringLiteral("itemId"), itemId},
                                            {QStringLiteral("summary"), QJsonArray{}}}});
            }
        }
        blocks_.erase(block);
    }
}

void ClaudeEventMapper::consumeAssistant(const QJsonObject& payload, QList<MappedEvent>* events) {
    const QJsonObject message = payload.value(QStringLiteral("message")).toObject();
    const QJsonObject raw = sanitizedClaudePayload(payload);
    if (messageItemId_.isEmpty()) {
        messageItemId_ = message.value(QStringLiteral("id")).toString();
    }

    QString finalText;
    for (const QJsonValue& blockValue : message.value(QStringLiteral("content")).toArray()) {
        const QJsonObject blockObject = blockValue.toObject();
        const QString type = blockObject.value(QStringLiteral("type")).toString();
        if (type == QLatin1String("text")) {
            finalText.append(blockObject.value(QStringLiteral("text")).toString());
        } else if (type == QLatin1String("tool_use")) {
            const ContentBlock block{.type = type,
                                     .itemId = blockObject.value(QStringLiteral("id")).toString(),
                                     .name = blockObject.value(QStringLiteral("name")).toString()};
            startTool(block, blockObject.value(QStringLiteral("input")).toObject(), events, raw);
        }
    }
    if (!finalText.isEmpty() && !messageCompleted_) {
        if (finalText.startsWith(streamedText_)) {
            appendText(finalText.sliced(streamedText_.size()), events, raw);
        } else if (streamedText_.isEmpty()) {
            appendText(finalText, events, raw);
        } else {
            events->append(
                {.type = domain::AgentEventType::WarningRaised,
                 .payload = {{QStringLiteral("message"),
                              QStringLiteral("Claude final text differs from its streamed "
                                             "text")}},
                 .rawPayload = raw});
        }
        ensureMessageStarted(events, raw);
        messageCompleted_ = true;
        events->append({.type = domain::AgentEventType::AgentMessageComplete,
                        .payload = {{QStringLiteral("itemId"), messageItemId_},
                                    {QStringLiteral("text"), finalText}},
                        .rawPayload = raw});
    }
    appendUsage(message.value(QStringLiteral("usage")).toObject(), events, raw);
}

void ClaudeEventMapper::consumeUser(const QJsonObject& payload, QList<MappedEvent>* events) {
    const QJsonObject raw = sanitizedClaudePayload(payload);
    const QJsonValue content =
        payload.value(QStringLiteral("message")).toObject().value(QStringLiteral("content"));
    if (!content.isArray()) {
        return;
    }
    for (const QJsonValue& blockValue : content.toArray()) {
        const QJsonObject block = blockValue.toObject();
        if (block.value(QStringLiteral("type")) != QLatin1String("tool_result")) {
            continue;
        }
        const QString itemId = block.value(QStringLiteral("tool_use_id")).toString();
        if (itemId.isEmpty() || completedTools_.contains(itemId)) {
            continue;
        }
        completedTools_.insert(itemId);
        QJsonObject eventPayload{
            {QStringLiteral("itemId"), itemId},
            {QStringLiteral("kind"), QStringLiteral("dynamicToolCall")},
            {QStringLiteral("status"), block.value(QStringLiteral("is_error")).toBool()
                                           ? QStringLiteral("failed")
                                           : QStringLiteral("completed")},
            {QStringLiteral("result"), block.value(QStringLiteral("content"))}};
        const QString output = compactValue(block.value(QStringLiteral("content")));
        if (!output.isEmpty()) {
            eventPayload.insert(QStringLiteral("aggregatedOutput"), output);
        }
        events->append({.type = domain::AgentEventType::ToolCompleted,
                        .payload = eventPayload,
                        .rawPayload = raw});
    }
}

QJsonObject sanitizedClaudePayload(const QJsonObject& payload) {
    QJsonObject result = payload;
    if (result.value(QStringLiteral("type")) == QLatin1String("stream_event")) {
        QJsonObject event = result.value(QStringLiteral("event")).toObject();
        QJsonObject delta = event.value(QStringLiteral("delta")).toObject();
        const QString deltaType = delta.value(QStringLiteral("type")).toString();
        if (deltaType == QLatin1String("thinking_delta") ||
            deltaType == QLatin1String("signature_delta")) {
            delta.remove(QStringLiteral("thinking"));
            delta.remove(QStringLiteral("signature"));
            delta.insert(QStringLiteral("redacted"), true);
            event.insert(QStringLiteral("delta"), delta);
        }
        QJsonObject content = event.value(QStringLiteral("content_block")).toObject();
        if (content.value(QStringLiteral("type")) == QLatin1String("thinking")) {
            content.remove(QStringLiteral("thinking"));
            content.remove(QStringLiteral("signature"));
            content.insert(QStringLiteral("redacted"), true);
            event.insert(QStringLiteral("content_block"), content);
        }
        result.insert(QStringLiteral("event"), event);
    }

    QJsonObject message = result.value(QStringLiteral("message")).toObject();
    if (!message.isEmpty() && message.value(QStringLiteral("content")).isArray()) {
        QJsonArray content;
        for (const QJsonValue& value : message.value(QStringLiteral("content")).toArray()) {
            QJsonObject block = value.toObject();
            if (block.value(QStringLiteral("type")) == QLatin1String("thinking")) {
                block.remove(QStringLiteral("thinking"));
                block.remove(QStringLiteral("signature"));
                block.insert(QStringLiteral("redacted"), true);
            }
            content.append(block);
        }
        message.insert(QStringLiteral("content"), content);
        result.insert(QStringLiteral("message"), message);
    }
    return result;
}

} // namespace snack::agent::claude
