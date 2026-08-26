#pragma once

#include "domain/DomainTypes.h"

#include <QList>

#include <optional>

namespace snack::storage {

class IEventRepository {
  public:
    virtual ~IEventRepository() = default;

    virtual bool saveConversation(const domain::Conversation& conversation, QString* error) = 0;
    [[nodiscard]] virtual std::optional<domain::Conversation>
    conversationById(const QUuid& conversationId, QString* error) const = 0;
    virtual bool appendEvent(const domain::AgentEvent& event, QString* error) = 0;
    [[nodiscard]] virtual QList<domain::AgentEvent>
    eventsForConversation(const QUuid& conversationId, QString* error) const = 0;
};

} // namespace snack::storage
