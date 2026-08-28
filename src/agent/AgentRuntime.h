#pragma once

#include "agent/IAgentAdapter.h"
#include "agent/claude/ClaudeCliDiscovery.h"
#include "agent/codex/CodexCliDiscovery.h"
#include "agent/process/IProcessTransport.h"

#include <memory>

namespace snack::agent {

struct AgentRuntime {
    std::unique_ptr<process::IProcessTransport> transport;
    std::unique_ptr<IAgentAdapter> adapter;
    domain::AgentKind requestedKind{domain::AgentKind::Codex};
    domain::AgentKind selectedKind{domain::AgentKind::Mock};
    QString detail;
    bool fellBack{false};
};

class AgentRuntimeFactory final {
  public:
    [[nodiscard]] static AgentRuntime create(domain::AgentKind requestedKind,
                                             const QString& configuredCodexExecutable = {},
                                             const QString& configuredClaudeExecutable = {});
    [[nodiscard]] static AgentRuntime
    createWithCodexInstallation(domain::AgentKind requestedKind,
                                const codex::CliInstallation& installation);
    [[nodiscard]] static AgentRuntime
    createWithInstallations(domain::AgentKind requestedKind,
                            const codex::CliInstallation& codexInstallation,
                            const claude::CliInstallation& claudeInstallation);
};

} // namespace snack::agent
