#pragma once

#include "domain/DomainTypes.h"

#include <optional>

namespace snack::app {

struct ConversationBootstrapResult {
    domain::Conversation conversation;
    bool restored{false};
};

[[nodiscard]] ConversationBootstrapResult
prepareConversation(const std::optional<domain::Conversation>& storedConversation,
                    const QString& workingDirectory, domain::AgentKind selectedAgent,
                    const QString& newConversationTitle);

} // namespace snack::app
