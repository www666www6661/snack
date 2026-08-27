#include "workspace/IsolationCapability.h"

namespace snack::workspace {

IsolationCapability IsolationCapabilityDetector::detect(domain::AgentKind agentKind,
                                                        const agent::CapabilitySet& capabilities) {
    if (agentKind == domain::AgentKind::Codex &&
        capabilities.accessLevels.contains(domain::AccessLevel::Workspace)) {
        return {.mode = IsolationMode::AgentSandbox,
                .detail = QStringLiteral(
                    "Codex reports workspace sandbox policy; no isolated checkout capability is "
                    "advertised")};
    }
    return {.mode = IsolationMode::None,
            .detail = QStringLiteral("The Agent does not advertise an official isolation mode")};
}

} // namespace snack::workspace
