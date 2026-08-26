#pragma once

#include "agent/process/IProcessTransport.h"

#include <QByteArray>
#include <QString>

#include <functional>

namespace snack::agent::codex {

enum class CliStatus { Available, NotFound, UnsupportedVersion, UnsupportedAppServer, ProbeFailed };

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

class CodexCliDiscovery final {
  public:
    using CommandRunner =
        std::function<CommandResult(const process::LaunchSpec& launchSpec, int timeoutMs)>;

    [[nodiscard]] static CliInstallation probe(const QString& configuredExecutable = {},
                                               int timeoutMs = 3000,
                                               const CommandRunner& runner = {});
    [[nodiscard]] static process::LaunchSpec
    appServerLaunchSpec(const CliInstallation& installation, const QString& workingDirectory = {});
    [[nodiscard]] static QString parseVersion(const QByteArray& output);
    [[nodiscard]] static QString minimumSupportedVersion();
    [[nodiscard]] static bool isSupportedVersion(const QString& version);
    [[nodiscard]] static bool supportsAppServer(const QByteArray& output);

  private:
    [[nodiscard]] static QString findExecutable(const QString& configuredExecutable);
    [[nodiscard]] static process::LaunchSpec commandFor(const QString& executable,
                                                        const QStringList& arguments);
    [[nodiscard]] static CommandResult runCommand(const process::LaunchSpec& launchSpec,
                                                  int timeoutMs);
};

} // namespace snack::agent::codex
