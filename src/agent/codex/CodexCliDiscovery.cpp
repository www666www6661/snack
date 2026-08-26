#include "agent/codex/CodexCliDiscovery.h"

#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QVersionNumber>

namespace snack::agent::codex {

CliInstallation CodexCliDiscovery::probe(const QString& configuredExecutable, int timeoutMs,
                                         const CommandRunner& runner) {
    const QString executable = findExecutable(configuredExecutable);
    if (executable.isEmpty()) {
        return {.status = CliStatus::NotFound,
                .detail = QStringLiteral("Codex CLI executable was not found")};
    }

    const auto execute = runner ? runner : runCommand;
    const CommandResult versionResult =
        execute(commandFor(executable, {QStringLiteral("--version")}), timeoutMs);
    if (!versionResult.started || versionResult.timedOut || versionResult.exitCode != 0) {
        const QString detail = !versionResult.error.isEmpty()
                                   ? versionResult.error
                                   : QString::fromUtf8(versionResult.standardError).trimmed();
        return {.status = CliStatus::ProbeFailed,
                .executablePath = executable,
                .detail = detail.isEmpty() ? QStringLiteral("Codex version probe failed") : detail};
    }

    const QByteArray versionOutput = versionResult.standardOutput + versionResult.standardError;
    const QString version = parseVersion(versionOutput);
    if (version.isEmpty()) {
        return {.status = CliStatus::ProbeFailed,
                .executablePath = executable,
                .detail = QStringLiteral("Codex returned an unrecognized version")};
    }
    if (!isSupportedVersion(version)) {
        return {.status = CliStatus::UnsupportedVersion,
                .executablePath = executable,
                .version = version,
                .detail = QStringLiteral("Codex CLI %1 is unsupported; Snack requires %2 or newer")
                              .arg(version, minimumSupportedVersion())};
    }

    const CommandResult helpResult =
        execute(commandFor(executable, {QStringLiteral("app-server"), QStringLiteral("--help")}),
                timeoutMs);
    const QByteArray helpOutput = helpResult.standardOutput + helpResult.standardError;
    if (!helpResult.started || helpResult.timedOut) {
        return {.status = CliStatus::ProbeFailed,
                .executablePath = executable,
                .version = version,
                .detail = !helpResult.error.isEmpty()
                              ? helpResult.error
                              : QStringLiteral("Codex app-server probe failed")};
    }
    if (helpResult.exitCode != 0 || !supportsAppServer(helpOutput)) {
        return {.status = CliStatus::UnsupportedAppServer,
                .executablePath = executable,
                .version = version,
                .detail = QStringLiteral("This Codex CLI does not support app-server")};
    }

    return {.status = CliStatus::Available,
            .executablePath = executable,
            .version = version,
            .detail = QStringLiteral("Codex app-server is available")};
}

process::LaunchSpec CodexCliDiscovery::appServerLaunchSpec(const CliInstallation& installation,
                                                           const QString& workingDirectory) {
    auto launchSpec = commandFor(
        installation.executablePath,
        {QStringLiteral("app-server"), QStringLiteral("--listen"), QStringLiteral("stdio://")});
    launchSpec.workingDirectory = workingDirectory;
    return launchSpec;
}

QString CodexCliDiscovery::parseVersion(const QByteArray& output) {
    static const QRegularExpression expression(
        QStringLiteral(
            R"(codex-cli\s+([0-9]+\.[0-9]+\.[0-9]+(?:-[0-9A-Za-z]+(?:[.-][0-9A-Za-z]+)*)?(?:\+[0-9A-Za-z]+(?:[.-][0-9A-Za-z]+)*)?))"),
        QRegularExpression::CaseInsensitiveOption);
    return expression.match(QString::fromUtf8(output)).captured(1);
}

QString CodexCliDiscovery::minimumSupportedVersion() { return QStringLiteral("0.149.0"); }

bool CodexCliDiscovery::isSupportedVersion(const QString& version) {
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
    const QVersionNumber minimum(0, 149, 0);
    const int comparison = QVersionNumber::compare(candidate, minimum);
    return comparison > 0 || (comparison == 0 && match.captured(4).isEmpty());
}

bool CodexCliDiscovery::supportsAppServer(const QByteArray& output) {
    const QString text = QString::fromUtf8(output);
    return text.contains(QStringLiteral("Usage: codex app-server"), Qt::CaseInsensitive) &&
           (text.contains(QStringLiteral("--listen"), Qt::CaseInsensitive) ||
            text.contains(QStringLiteral("generate-json-schema"), Qt::CaseInsensitive));
}

QString CodexCliDiscovery::findExecutable(const QString& configuredExecutable) {
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
    const QStringList candidates = {QStringLiteral("codex.cmd"), QStringLiteral("codex.exe"),
                                    QStringLiteral("codex")};
#else
    const QStringList candidates = {QStringLiteral("codex")};
#endif
    for (const QString& candidate : candidates) {
        const QString executable = QStandardPaths::findExecutable(candidate);
        if (!executable.isEmpty()) {
            return executable;
        }
    }
    return {};
}

process::LaunchSpec CodexCliDiscovery::commandFor(const QString& executable,
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

CommandResult CodexCliDiscovery::runCommand(const process::LaunchSpec& launchSpec, int timeoutMs) {
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
                .error = QStringLiteral("Codex command timed out")};
    }
    return {.started = true,
            .exitCode = process.exitCode(),
            .standardOutput = process.readAllStandardOutput(),
            .standardError = process.readAllStandardError()};
}

} // namespace snack::agent::codex
