#pragma once

#include "domain/DomainTypes.h"

#include <QByteArray>
#include <QList>

namespace snack::app {

enum class ConversationExportFormat { Markdown, Json };

class ConversationExporter final {
  public:
    [[nodiscard]] static QByteArray render(const domain::Conversation& conversation,
                                           const QList<domain::AgentEvent>& events,
                                           ConversationExportFormat format);
    static bool write(const QString& filePath, const domain::Conversation& conversation,
                      const QList<domain::AgentEvent>& events, ConversationExportFormat format,
                      QString* error = nullptr);
};

} // namespace snack::app
