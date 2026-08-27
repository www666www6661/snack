#include "app/ConversationExporter.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>

namespace snack::app {
namespace {

QJsonObject conversationJson(const domain::Conversation& conversation) {
    QJsonArray tags;
    for (const QString& tag : conversation.tags) {
        tags.append(tag);
    }
    return {{QStringLiteral("id"), conversation.id.toString(QUuid::WithoutBraces)},
            {QStringLiteral("title"), conversation.title},
            {QStringLiteral("workingDirectory"), conversation.workingDirectory},
            {QStringLiteral("agent"), domain::enumName(conversation.agentKind)},
            {QStringLiteral("model"), conversation.modelId},
            {QStringLiteral("status"), domain::enumName(conversation.status)},
            {QStringLiteral("nativeThreadId"), conversation.nativeThreadId},
            {QStringLiteral("nativeSessionId"), conversation.nativeSessionId},
            {QStringLiteral("archived"), conversation.archived},
            {QStringLiteral("pinned"), conversation.pinned},
            {QStringLiteral("tags"), tags},
            {QStringLiteral("group"), conversation.groupName},
            {QStringLiteral("createdAt"), conversation.createdAt.toString(Qt::ISODateWithMs)},
            {QStringLiteral("lastActivityAt"),
             conversation.lastActivityAt.toString(Qt::ISODateWithMs)}};
}

QJsonObject eventJson(const domain::AgentEvent& event) {
    return {{QStringLiteral("id"), event.id.toString(QUuid::WithoutBraces)},
            {QStringLiteral("turnId"), event.turnId.toString(QUuid::WithoutBraces)},
            {QStringLiteral("sequence"), static_cast<qint64>(event.sequence)},
            {QStringLiteral("type"), domain::enumName(event.type)},
            {QStringLiteral("payload"), event.payload},
            {QStringLiteral("rawPayload"), event.rawPayload},
            {QStringLiteral("occurredAt"), event.occurredAt.toString(Qt::ISODateWithMs)}};
}

QString singleLine(QString text) {
    text.replace(QLatin1Char('\r'), QLatin1Char(' '));
    text.replace(QLatin1Char('\n'), QLatin1Char(' '));
    return text;
}

QString jsonBlock(const QJsonObject& object) {
    return QStringLiteral("````json\n%1\n````\n\n")
        .arg(QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Indented)).trimmed());
}

QByteArray renderMarkdown(const domain::Conversation& conversation,
                          const QList<domain::AgentEvent>& events) {
    QString output = QStringLiteral("# %1\n\n").arg(singleLine(conversation.title));
    output += QStringLiteral("- Agent: `%1`\n- Workspace: `%2`\n- Model: `%3`\n"
                             "- Created: `%4`\n- Last activity: `%5`\n\n")
                  .arg(domain::enumName(conversation.agentKind), conversation.workingDirectory,
                       conversation.modelId, conversation.createdAt.toString(Qt::ISODateWithMs),
                       conversation.lastActivityAt.toString(Qt::ISODateWithMs));
    bool agentMessageOpen = false;
    for (const auto& event : events) {
        switch (event.type) {
        case domain::AgentEventType::UserMessage:
            output += QStringLiteral("## You\n\n%1\n\n")
                          .arg(event.payload.value(QStringLiteral("text")).toString());
            agentMessageOpen = false;
            break;
        case domain::AgentEventType::AgentMessageStart:
            output += QStringLiteral("## Agent\n\n");
            agentMessageOpen = true;
            break;
        case domain::AgentEventType::AgentMessageDelta:
            if (!agentMessageOpen) {
                output += QStringLiteral("## Agent\n\n");
                agentMessageOpen = true;
            }
            output += event.payload.value(QStringLiteral("text")).toString();
            break;
        case domain::AgentEventType::AgentMessageComplete:
            output += QStringLiteral("\n\n");
            agentMessageOpen = false;
            break;
        default:
            if (agentMessageOpen) {
                output += QStringLiteral("\n\n");
                agentMessageOpen = false;
            }
            output += QStringLiteral("### %1 · %2\n\n")
                          .arg(domain::enumName(event.type),
                               event.occurredAt.toString(Qt::ISODateWithMs));
            output += jsonBlock(event.payload);
            break;
        }
    }
    if (agentMessageOpen) {
        output += QStringLiteral("\n");
    }
    return output.toUtf8();
}

} // namespace

QByteArray ConversationExporter::render(const domain::Conversation& conversation,
                                        const QList<domain::AgentEvent>& events,
                                        ConversationExportFormat format) {
    if (format == ConversationExportFormat::Markdown) {
        return renderMarkdown(conversation, events);
    }
    QJsonArray exportedEvents;
    for (const auto& event : events) {
        exportedEvents.append(eventJson(event));
    }
    const QJsonObject root{{QStringLiteral("formatVersion"), 1},
                           {QStringLiteral("conversation"), conversationJson(conversation)},
                           {QStringLiteral("events"), exportedEvents}};
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

bool ConversationExporter::write(const QString& filePath, const domain::Conversation& conversation,
                                 const QList<domain::AgentEvent>& events,
                                 ConversationExportFormat format, QString* error) {
    if (filePath.trimmed().isEmpty()) {
        if (error != nullptr) {
            *error = QStringLiteral("Export path is empty");
        }
        return false;
    }
    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error != nullptr) {
            *error = file.errorString();
        }
        return false;
    }
    const QByteArray data = render(conversation, events, format);
    if (file.write(data) != data.size() || !file.commit()) {
        if (error != nullptr) {
            *error = file.errorString();
        }
        file.cancelWriting();
        return false;
    }
    return true;
}

} // namespace snack::app
