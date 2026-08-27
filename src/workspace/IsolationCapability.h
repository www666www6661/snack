#pragma once

#include "agent/IAgentAdapter.h"

namespace snack::workspace {

enum class IsolationMode {
    None,
    AgentSandbox,
    IsolatedCheckout,
};

struct IsolationCapability {
    IsolationMode mode{IsolationMode::None};
    QString detail;
};

class IsolationCapabilityDetector final {
  public:
    [[nodiscard]] static IsolationCapability detect(domain::AgentKind agentKind,
                                                    const agent::CapabilitySet& capabilities);
};

} // namespace snack::workspace
