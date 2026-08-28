#include "agent/claude/ClaudeCliDiscovery.h"

#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QVersionNumber>

namespace snack::agent::claude {
namespace {

QString commandFailureDetail(const CommandResult& result, const QString& fallback) {
    if (!result.error.isEmpty()) {
        return result.error;
    }
    const QString standardError = QString::fromUtf8(result.standardError).trimmed();
    return standardError.isEmpty() ? fallback : standardError;
}

QStringList noModelProbeArguments() {
    return {
        QStringLiteral("-p"),
        QStringLiteral("--init-only"),
        QStringLiteral("--safe-mode"),
        QStringLiteral("--input-format"),
        QStringLiteral("stream-json"),
        QStringLiteral("--output-format"),
        QStringLiteral("stream-json"),
        QStringLiteral("--strict-mcp-config"),
        QStringLiteral("--mcp-config"),
        QStringLiteral(R"({"mcpServers":{}})"),
        QStringLiteral("--permission-prompt-tool"),
        QStringLiteral("mcp__snack_probe__permission"),
    };
}

} // namespace

CliInstallation ClaudeCliDiscovery::probe(const QString& configuredExecutable, int timeoutMs,
                                          const CommandRunner& runner) {
    const QString executable = findExecutable(configuredExecutable);
    if (executable.isEmpty()) {
        return {.status = CliStatus::NotFound,
                .detail = QStringLiteral("Claude CLI executable was not found")};
    }

    const auto execute = runner ? runner : runCommand;
    const CommandResult versionResult =
        execute(commandFor(executable, {QStringLiteral("--version")}), timeoutMs);
    if (!versionResult.started || versionResult.timedOut || versionResult.exitCode != 0) {
        return {.status = CliStatus::ProbeFailed,
                .executablePath = executable,
                .detail = commandFailureDetail(versionResult,
                                               QStringLiteral("Claude version probe failed"))};
    }

    const QString version =
        parseVersion(versionResult.standardOutput + versionResult.standardError);
    if (version.isEmpty()) {
        return {.status = CliStatus::ProbeFailed,
                .executablePath = executable,
                .detail = QStringLiteral("Claude returned an unrecognized version")};
    }
    if (!isSupportedVersion(version)) {
        return {
            .status = CliStatus::UnsupportedVersion,
            .executablePath = executable,
            .version = version,
            .detail = QStringLiteral("Claude Code %1 is unsupported; Snack requires %2 or newer")
                          .arg(version, minimumSupportedVersion()),
        };
    }

    const CommandResult helpResult =
        execute(commandFor(executable, {QStringLiteral("--help")}), timeoutMs);
    if (!helpResult.started || helpResult.timedOut) {
        return {.status = CliStatus::ProbeFailed,
                .executablePath = executable,
                .version = version,
                .detail =
                    commandFailureDetail(helpResult, QStringLiteral("Claude help probe failed"))};
    }
    const QByteArray helpOutput = helpResult.standardOutput + helpResult.standardError;
    if (helpResult.exitCode != 0 || !supportsStreamProtocol(helpOutput)) {
        return {.status = CliStatus::UnsupportedProtocol,
                .executablePath = executable,
                .version = version,
                .detail = QStringLiteral("This Claude CLI does not expose the required stream and "
                                         "session options")};
    }

    const CommandResult protocolResult =
        execute(commandFor(executable, noModelProbeArguments()), timeoutMs);
    if (!protocolResult.started || protocolResult.timedOut) {
        return {.status = CliStatus::ProbeFailed,
                .executablePath = executable,
                .version = version,
                .detail = commandFailureDetail(protocolResult,
                                               QStringLiteral("Claude protocol probe failed"))};
    }
    if (protocolResult.exitCode != 0) {
        return {.status = CliStatus::UnsupportedProtocol,
                .executablePath = executable,
                .version = version,
                .detail = commandFailureDetail(
                    protocolResult,
                    QStringLiteral("Claude rejected the no-model stream protocol probe"))};
    }

    return {.status = CliStatus::Available,
            .executablePath = executable,
            .version = version,
            .detail = QStringLiteral("Claude stream protocol is available")};
}

process::LaunchSpec ClaudeCliDiscovery::sessionLaunchSpec(const CliInstallation& installation,
                                                          const SessionLaunchOptions& options,
                                                          bool resume) {
    QStringList arguments = {
        QStringLiteral("-p"),
        QStringLiteral("--input-format"),
        QStringLiteral("stream-json"),
        QStringLiteral("--output-format"),
        QStringLiteral("stream-json"),
        QStringLiteral("--verbose"),
        QStringLiteral("--include-partial-messages"),
        QStringLiteral("--replay-user-messages"),
    };
    if (!options.sessionId.isEmpty()) {
        arguments.append(resume ? QStringLiteral("--resume") : QStringLiteral("--session-id"));
        arguments.append(options.sessionId);
    }
    if (!options.modelId.isEmpty()) {
        arguments.append({QStringLiteral("--model"), options.modelId});
    }
    arguments.append({QStringLiteral("--effort"), effortArgument(options.reasoningEffort),
                      QStringLiteral("--permission-mode"),
                      permissionModeArgument(options.accessLevel)});
    if (!options.mcpConfigJson.isEmpty()) {
        arguments.append({QStringLiteral("--mcp-config"), options.mcpConfigJson});
    }
    if (!options.permissionPromptTool.isEmpty()) {
        arguments.append(
            {QStringLiteral("--permission-prompt-tool"), options.permissionPromptTool});
    }

    process::LaunchSpec launchSpec = commandFor(installation.executablePath, arguments);
    launchSpec.workingDirectory = options.workingDirectory;
    return launchSpec;
}

QString ClaudeCliDiscovery::parseVersion(const QByteArray& output) {
    static const QRegularExpression expression(
        QStringLiteral(
            R"((?:Claude\s+Code\s+)([0-9]+\.[0-9]+\.[0-9]+(?:-[0-9A-Za-z]+(?:[.-][0-9A-Za-z]+)*)?(?:\+[0-9A-Za-z]+(?:[.-][0-9A-Za-z]+)*)?)|([0-9]+\.[0-9]+\.[0-9]+(?:-[0-9A-Za-z]+(?:[.-][0-9A-Za-z]+)*)?(?:\+[0-9A-Za-z]+(?:[.-][0-9A-Za-z]+)*)?)\s*\(Claude\s+Code\))"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = expression.match(QString::fromUtf8(output));
    return match.captured(1).isEmpty() ? match.captured(2) : match.captured(1);
}

QString ClaudeCliDiscovery::minimumSupportedVersion() { return QStringLiteral("2.1.219"); }

bool ClaudeCliDiscovery::isSupportedVersion(const QString& version) {
    static const QRegularExpression expression(QStringLiteral(
        R"(^([0-9]+)\.([0-9]+)\.([0-9]+)(?:-([0-9A-Za-z]+(?:[.-][0-9A-Za-z]+)*))?(?:\+[0-9A-Za-z]+(?:[.-][0-9A-Za-z]+)*)?$)"));
    const QRegularExpressionMatch match = expression.match(version);
    if (!match.hasMatch()) {
        return false;
    }

    bool majorValid = false;
    bool minorValid = false;
    bool patchValid = false;
    const int major = match.captured(1).toInt(&majorValid);
    const int minor = match.captured(2).toInt(&minorValid);
    const int patch = match.captured(3).toInt(&patchValid);
    if (!majorValid || !minorValid || !patchValid) {
        return false;
    }

    const QVersionNumber candidate(major, minor, patch);
    const QVersionNumber minimum(2, 1, 219);
    const int comparison = QVersionNumber::compare(candidate, minimum);
    return comparison > 0 || (comparison == 0 && match.captured(4).isEmpty());
}

bool ClaudeCliDiscovery::supportsStreamProtocol(const QByteArray& output) {
    const QString text = QString::fromUtf8(output);
    const QStringList requiredOptions = {
        QStringLiteral("--input-format"),
        QStringLiteral("--output-format"),
        QStringLiteral("--include-partial-messages"),
        QStringLiteral("--replay-user-messages"),
        QStringLiteral("--resume"),
        QStringLiteral("--session-id"),
        QStringLiteral("--model"),
        QStringLiteral("--effort"),
        QStringLiteral("--permission-mode"),
    };
    for (const QString& option : requiredOptions) {
        if (!text.contains(option, Qt::CaseInsensitive)) {
            return false;
        }
    }
    return text.contains(QStringLiteral("stream-json"), Qt::CaseInsensitive);
}

QString ClaudeCliDiscovery::effortArgument(domain::ReasoningEffort effort) {
    switch (effort) {
    case domain::ReasoningEffort::Minimal:
    case domain::ReasoningEffort::Low:
        return QStringLiteral("low");
    case domain::ReasoningEffort::Medium:
        return QStringLiteral("medium");
    case domain::ReasoningEffort::High:
        return QStringLiteral("high");
    case domain::ReasoningEffort::ExtraHigh:
        return QStringLiteral("xhigh");
    case domain::ReasoningEffort::Maximum:
    case domain::ReasoningEffort::Ultra:
        return QStringLiteral("max");
    }
    return QStringLiteral("medium");
}

QString ClaudeCliDiscovery::permissionModeArgument(domain::AccessLevel accessLevel) {
    switch (accessLevel) {
    case domain::AccessLevel::Strict:
        return QStringLiteral("manual");
    case domain::AccessLevel::Workspace:
        return QStringLiteral("acceptEdits");
    case domain::AccessLevel::Full:
        return QStringLiteral("bypassPermissions");
    }
    return QStringLiteral("manual");
}

QString ClaudeCliDiscovery::findExecutable(const QString& configuredExecutable) {
    const QString configured = configuredExecutable.trimmed();
    if (!configured.isEmpty()) {
        const QFileInfo info(configured);
        if (info.isAbsolute() || configured.contains('/') || configured.contains('\\')) {
            return info.exists() && info.isFile() ? QDir::cleanPath(info.absoluteFilePath())
                                                  : QString{};
        }
        return QStandardPaths::findExecutable(configured);
    }

#ifdef Q_OS_WIN
    const QStringList candidates = {QStringLiteral("claude.cmd"), QStringLiteral("claude.exe"),
                                    QStringLiteral("claude")};
#else
    const QStringList candidates = {QStringLiteral("claude")};
#endif
    for (const QString& candidate : candidates) {
        const QString executable = QStandardPaths::findExecutable(candidate);
        if (!executable.isEmpty()) {
            return executable;
        }
    }
    return {};
}

process::LaunchSpec ClaudeCliDiscovery::commandFor(const QString& executable,
                                                   const QStringList& arguments) {
#ifdef Q_OS_WIN
    const QString suffix = QFileInfo(executable).suffix();
    if (suffix.compare(QStringLiteral("cmd"), Qt::CaseInsensitive) == 0 ||
        suffix.compare(QStringLiteral("bat"), Qt::CaseInsensitive) == 0) {
        const QString commandInterpreter =
            qEnvironmentVariable("COMSPEC", QStringLiteral("C:\\Windows\\System32\\cmd.exe"));
        QStringList commandArguments = {QStringLiteral("/d"), QStringLiteral("/c"),
                                        QStringLiteral("call"),
                                        QDir::toNativeSeparators(executable)};
        commandArguments.append(arguments);
        return {.program = commandInterpreter, .arguments = commandArguments};
    }
#endif
    return {.program = executable, .arguments = arguments};
}

CommandResult ClaudeCliDiscovery::runCommand(const process::LaunchSpec& launchSpec, int timeoutMs) {
    QProcess process;
    process.setProcessChannelMode(QProcess::SeparateChannels);
    process.start(launchSpec.program, launchSpec.arguments, QIODevice::ReadOnly);
    if (!process.waitForStarted(timeoutMs)) {
        return {.error = process.errorString()};
    }
    if (!process.waitForFinished(timeoutMs)) {
        process.kill();
        process.waitForFinished();
        return {.started = true,
                .timedOut = true,
                .standardOutput = process.readAllStandardOutput(),
                .standardError = process.readAllStandardError(),
                .error = QStringLiteral("Claude command timed out")};
    }
    return {.started = true,
            .exitCode = process.exitCode(),
            .standardOutput = process.readAllStandardOutput(),
            .standardError = process.readAllStandardError()};
}

} // namespace snack::agent::claude
