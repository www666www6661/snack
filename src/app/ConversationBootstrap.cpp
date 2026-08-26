#include "app/ConversationBootstrap.h"

#include <QDir>

#include <utility>

namespace snack::app {

ConversationBootstrapResult
prepareConversation(const std::optional<domain::Conversation>& storedConversation,
                    const QString& workingDirectory, domain::AgentKind selectedAgent,
                    const QString& newConversationTitle) {
    const Qt::CaseSensitivity pathCaseSensitivity =
#ifdef Q_OS_WIN
        Qt::CaseInsensitive;
#else
        Qt::CaseSensitive;
#endif
    if (storedConversation.has_value() && storedConversation->agentKind == selectedAgent &&
        QDir::cleanPath(storedConversation->workingDirectory)
                .compare(QDir::cleanPath(workingDirectory), pathCaseSensitivity) == 0) {
        domain::Conversation restored = *storedConversation;
        restored.status = domain::ConversationStatus::Dormant;
        return {.conversation = std::move(restored), .restored = true};
    }

    domain::Conversation conversation;
    conversation.title = newConversationTitle;
    conversation.workingDirectory = workingDirectory;
    conversation.agentKind = selectedAgent;
    conversation.status = domain::ConversationStatus::Dormant;
    return {.conversation = std::move(conversation), .restored = false};
}

} // namespace snack::app
