#include "agent/AgentRuntime.h"

#include "agent/FakeAgentAdapter.h"
#include "agent/claude/ClaudeAdapter.h"
#include "agent/codex/CodexAdapter.h"
#include "agent/process/QProcessTransport.h"

namespace snack::agent {
namespace {

AgentRuntime mockRuntime(domain::AgentKind requestedKind, const QString& detail, bool fellBack) {
    AgentRuntime runtime;
    runtime.requestedKind = requestedKind;
    runtime.selectedKind = domain::AgentKind::Mock;
    runtime.detail = detail;
    runtime.fellBack = fellBack;
    runtime.adapter = std::make_unique<FakeAgentAdapter>();
    return runtime;
}

} // namespace

AgentRuntime AgentRuntimeFactory::create(domain::AgentKind requestedKind,
                                         const QString& configuredCodexExecutable,
                                         const QString& configuredClaudeExecutable) {
    if (requestedKind == domain::AgentKind::Codex) {
        return createWithInstallations(
            requestedKind, codex::CodexCliDiscovery::probe(configuredCodexExecutable), {});
    }
    if (requestedKind == domain::AgentKind::Claude) {
        return createWithInstallations(
            requestedKind, {}, claude::ClaudeCliDiscovery::probe(configuredClaudeExecutable));
    }
    return createWithInstallations(requestedKind, {}, {});
}

AgentRuntime
AgentRuntimeFactory::createWithCodexInstallation(domain::AgentKind requestedKind,
                                                 const codex::CliInstallation& installation) {
    return createWithInstallations(requestedKind, installation, {});
}

AgentRuntime
AgentRuntimeFactory::createWithInstallations(domain::AgentKind requestedKind,
                                             const codex::CliInstallation& codexInstallation,
                                             const claude::CliInstallation& claudeInstallation) {
    if (requestedKind == domain::AgentKind::Mock) {
        return mockRuntime(requestedKind, QStringLiteral("Mock Agent selected"), false);
    }
    if (requestedKind == domain::AgentKind::Claude) {
        if (!claudeInstallation.isUsable()) {
            return mockRuntime(requestedKind,
                               claudeInstallation.detail.isEmpty()
                                   ? QStringLiteral("Claude CLI is unavailable")
                                   : claudeInstallation.detail,
                               true);
        }
        AgentRuntime runtime;
        runtime.requestedKind = requestedKind;
        runtime.selectedKind = domain::AgentKind::Claude;
        runtime.detail = claudeInstallation.version.isEmpty()
                             ? QStringLiteral("Claude stream protocol is available")
                             : QStringLiteral("Claude Code %1").arg(claudeInstallation.version);
        runtime.transport = std::make_unique<process::QProcessTransport>();
        runtime.adapter =
            std::make_unique<claude::ClaudeAdapter>(claudeInstallation, runtime.transport.get());
        return runtime;
    }
    if (!codexInstallation.isUsable()) {
        return mockRuntime(requestedKind,
                           codexInstallation.detail.isEmpty()
                               ? QStringLiteral("Codex CLI is unavailable")
                               : codexInstallation.detail,
                           true);
    }

    AgentRuntime runtime;
    runtime.requestedKind = requestedKind;
    runtime.selectedKind = domain::AgentKind::Codex;
    runtime.detail = codexInstallation.version.isEmpty()
                         ? QStringLiteral("Codex app-server is available")
                         : QStringLiteral("Codex CLI %1").arg(codexInstallation.version);
    runtime.transport = std::make_unique<process::QProcessTransport>();
    runtime.adapter =
        std::make_unique<codex::CodexAdapter>(codexInstallation, runtime.transport.get());
    return runtime;
}

} // namespace snack::agent
