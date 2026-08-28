#pragma once

#include "agent/process/IProcessTransport.h"
#include "domain/DomainTypes.h"

#include <QByteArray>
#include <QString>

#include <functional>

namespace snack::agent::claude {

enum class CliStatus { Available, NotFound, UnsupportedVersion, UnsupportedProtocol, ProbeFailed };

struct CommandResult {
    bool started{false};
    bool timedOut{false};
    int exitCode{-1};
    QByteArray standardOutput;
    QByteArray standardError;
    QString error;
};

struct CliInstallation {
    CliStatus status{CliStatus::NotFound};
    QString executablePath;
    QString version;
    QString detail;

    [[nodiscard]] bool isUsable() const { return status == CliStatus::Available; }
};

struct SessionLaunchOptions {
    QString workingDirectory;
    QString sessionId;
    QString modelId;
    domain::ReasoningEffort reasoningEffort{domain::ReasoningEffort::Medium};
    domain::AccessLevel accessLevel{domain::AccessLevel::Strict};
    QString mcpConfigJson;
    QString permissionPromptTool;
};

class ClaudeCliDiscovery final {
  public:
    using CommandRunner =
        std::function<CommandResult(const process::LaunchSpec& launchSpec, int timeoutMs)>;

    [[nodiscard]] static CliInstallation probe(const QString& configuredExecutable = {},
                                               int timeoutMs = 3000,
                                               const CommandRunner& runner = {});
    [[nodiscard]] static process::LaunchSpec sessionLaunchSpec(const CliInstallation& installation,
                                                               const SessionLaunchOptions& options,
                                                               bool resume);
    [[nodiscard]] static QString parseVersion(const QByteArray& output);
    [[nodiscard]] static QString minimumSupportedVersion();
    [[nodiscard]] static bool isSupportedVersion(const QString& version);
    [[nodiscard]] static bool supportsStreamProtocol(const QByteArray& output);
    [[nodiscard]] static QString effortArgument(domain::ReasoningEffort effort);
    [[nodiscard]] static QString permissionModeArgument(domain::AccessLevel accessLevel);

  private:
    [[nodiscard]] static QString findExecutable(const QString& configuredExecutable);
    [[nodiscard]] static process::LaunchSpec commandFor(const QString& executable,
                                                        const QStringList& arguments);
    [[nodiscard]] static CommandResult runCommand(const process::LaunchSpec& launchSpec,
                                                  int timeoutMs);
};

} // namespace snack::agent::claude
