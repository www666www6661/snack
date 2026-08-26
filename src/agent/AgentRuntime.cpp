#include "agent/AgentRuntime.h"

#include "agent/FakeAgentAdapter.h"
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
                                         const QString& configuredCodexExecutable) {
    if (requestedKind != domain::AgentKind::Codex) {
        return createWithCodexInstallation(requestedKind, {});
    }
    return createWithCodexInstallation(requestedKind,
                                       codex::CodexCliDiscovery::probe(configuredCodexExecutable));
}

AgentRuntime
AgentRuntimeFactory::createWithCodexInstallation(domain::AgentKind requestedKind,
                                                 const codex::CliInstallation& installation) {
    if (requestedKind == domain::AgentKind::Mock) {
        return mockRuntime(requestedKind, QStringLiteral("Mock Agent selected"), false);
    }
    if (requestedKind == domain::AgentKind::Claude) {
        return mockRuntime(requestedKind, QStringLiteral("Claude adapter is not implemented yet"),
                           true);
    }
    if (!installation.isUsable()) {
        return mockRuntime(requestedKind,
                           installation.detail.isEmpty()
                               ? QStringLiteral("Codex CLI is unavailable")
                               : installation.detail,
                           true);
    }

    AgentRuntime runtime;
    runtime.requestedKind = requestedKind;
    runtime.selectedKind = domain::AgentKind::Codex;
    runtime.detail = installation.version.isEmpty()
                         ? QStringLiteral("Codex app-server is available")
                         : QStringLiteral("Codex CLI %1").arg(installation.version);
    runtime.transport = std::make_unique<process::QProcessTransport>();
    runtime.adapter = std::make_unique<codex::CodexAdapter>(installation, runtime.transport.get());
    return runtime;
}

} // namespace snack::agent
