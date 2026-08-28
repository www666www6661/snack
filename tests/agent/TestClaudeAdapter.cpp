#include "agent/claude/ClaudeCliDiscovery.h"
#include "agent/claude/ClaudeStreamClient.h"

#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

class TestClaudeAdapter final : public QObject {
    Q_OBJECT

  private slots:
    void discoversCliWithoutModelUse();
    void validatesSupportedVersions();
    void reportsProbeFailures();
    void probesExecutableWithDefaultRunner();
    void buildsSessionLaunchSpecs();
    void mapsRuntimeControls();
    void parsesAndInitializesFragmentedStream();
    void boundsFramesDiagnosticsAndWrites();
    void rejectsInvalidAndCrossSessionStreams();
    void handlesTimeoutExitAndShutdown();
};

class FakeClaudeProcessTransport final : public snack::agent::process::IProcessTransport {
  public:
    using IProcessTransport::IProcessTransport;

    [[nodiscard]] bool isRunning() const override { return running; }

    void start(const snack::agent::process::LaunchSpec& value) override {
        launchSpec = value;
        running = true;
        emit started();
    }

    qint64 write(const QByteArray& data) override {
        if (failWrites) {
            return -1;
        }
        writes.append(data);
        return data.size();
    }

    void closeWriteChannel() override { ++closeCalls; }

    void terminate() override {
        ++terminateCalls;
        if (running && !deferTermination) {
            running = false;
            emit finished(0, snack::agent::process::ExitStatus::Normal);
        }
    }

    void kill() override {
        ++killCalls;
        running = false;
        emit finished(-1, snack::agent::process::ExitStatus::Crashed);
    }

    void feedOutput(const QByteArray& data) { emit standardOutputReceived(data); }
    void feedError(const QByteArray& data) { emit standardErrorReceived(data); }

    void finish(int exitCode, snack::agent::process::ExitStatus status) {
        running = false;
        emit finished(exitCode, status);
    }

    snack::agent::process::LaunchSpec launchSpec;
    QList<QByteArray> writes;
    bool running{false};
    bool failWrites{false};
    bool deferTermination{false};
    int closeCalls{0};
    int terminateCalls{0};
    int killCalls{0};
};

namespace {

QByteArray supportedHelp() {
    return "Usage: claude [options]\n"
           "  --input-format <format> stream-json\n"
           "  --output-format <format> stream-json\n"
           "  --include-partial-messages\n"
           "  --replay-user-messages\n"
           "  --resume <session-id>\n"
           "  --session-id <uuid>\n"
           "  --model <model>\n"
           "  --effort <level>\n"
           "  --permission-mode <mode>\n";
}

QString createPlaceholderExecutable(QTemporaryDir& directory) {
    const QString executable = directory.filePath(QStringLiteral("claude.exe"));
    QFile file(executable);
    if (!file.open(QIODevice::WriteOnly)) {
        return {};
    }
    file.close();
    return executable;
}

} // namespace

void TestClaudeAdapter::discoversCliWithoutModelUse() {
    using namespace snack::agent::claude;
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString executable = createPlaceholderExecutable(directory);
    QVERIFY(!executable.isEmpty());

    QList<snack::agent::process::LaunchSpec> launches;
    const ClaudeCliDiscovery::CommandRunner runner =
        [&launches](const snack::agent::process::LaunchSpec& launchSpec, int) {
            launches.append(launchSpec);
            if (launchSpec.arguments.contains(QStringLiteral("--version"))) {
                return CommandResult{
                    .started = true, .exitCode = 0, .standardOutput = "Claude Code 2.1.245\n"};
            }
            if (launchSpec.arguments.contains(QStringLiteral("--help"))) {
                return CommandResult{
                    .started = true, .exitCode = 0, .standardOutput = supportedHelp()};
            }
            return CommandResult{.started = true, .exitCode = 0};
        };

    const CliInstallation installation = ClaudeCliDiscovery::probe(executable, 50, runner);
    QCOMPARE(installation.status, CliStatus::Available);
    QCOMPARE(installation.version, QStringLiteral("2.1.245"));
    QCOMPARE(launches.size(), 3);

    const QStringList probeArguments = launches.constLast().arguments;
    QVERIFY(probeArguments.contains(QStringLiteral("--init-only")));
    QVERIFY(probeArguments.contains(QStringLiteral("--safe-mode")));
    QVERIFY(probeArguments.contains(QStringLiteral("--strict-mcp-config")));
    QVERIFY(probeArguments.contains(QStringLiteral("--permission-prompt-tool")));
    QVERIFY(probeArguments.contains(QStringLiteral(R"({"mcpServers":{}})")));
    QVERIFY(!probeArguments.contains(QStringLiteral("--model")));
    QVERIFY(!probeArguments.contains(QStringLiteral("--resume")));

    QCOMPARE(ClaudeCliDiscovery::parseVersion("2.1.245 (Claude Code)\n"),
             QStringLiteral("2.1.245"));
    QCOMPARE(ClaudeCliDiscovery::parseVersion("CLAUDE CODE 3.2.1-beta.2+build.7\n"),
             QStringLiteral("3.2.1-beta.2+build.7"));
    QVERIFY(ClaudeCliDiscovery::parseVersion("unrelated 2.1.245").isEmpty());
}

void TestClaudeAdapter::validatesSupportedVersions() {
    using snack::agent::claude::ClaudeCliDiscovery;

    QCOMPARE(ClaudeCliDiscovery::minimumSupportedVersion(), QStringLiteral("2.1.219"));
    QVERIFY(!ClaudeCliDiscovery::isSupportedVersion(QStringLiteral("2.1.218")));
    QVERIFY(!ClaudeCliDiscovery::isSupportedVersion(QStringLiteral("2.1.219-beta.1")));
    QVERIFY(ClaudeCliDiscovery::isSupportedVersion(QStringLiteral("2.1.219")));
    QVERIFY(ClaudeCliDiscovery::isSupportedVersion(QStringLiteral("2.1.219+packaged.1")));
    QVERIFY(ClaudeCliDiscovery::isSupportedVersion(QStringLiteral("2.1.220-alpha.1")));
    QVERIFY(ClaudeCliDiscovery::isSupportedVersion(QStringLiteral("3.0.0")));
    QVERIFY(!ClaudeCliDiscovery::isSupportedVersion(QStringLiteral("invalid")));
    QVERIFY(!ClaudeCliDiscovery::isSupportedVersion(QStringLiteral("999999999999999999999.0.0")));
}

void TestClaudeAdapter::reportsProbeFailures() {
    using namespace snack::agent::claude;
    QCOMPARE(ClaudeCliDiscovery::probe(QStringLiteral("Z:/missing/claude.exe")).status,
             CliStatus::NotFound);

    QTemporaryDir directory;
    const QString executable = createPlaceholderExecutable(directory);
    QVERIFY(!executable.isEmpty());

    const auto oldVersion = ClaudeCliDiscovery::probe(
        executable, 50, [](const snack::agent::process::LaunchSpec&, int) {
            return CommandResult{
                .started = true, .exitCode = 0, .standardOutput = "Claude Code 2.1.218"};
        });
    QCOMPARE(oldVersion.status, CliStatus::UnsupportedVersion);
    QVERIFY(oldVersion.detail.contains(QStringLiteral("2.1.219")));

    int missingOptionCalls = 0;
    const auto missingOption = ClaudeCliDiscovery::probe(
        executable, 50, [&missingOptionCalls](const snack::agent::process::LaunchSpec&, int) {
            ++missingOptionCalls;
            if (missingOptionCalls == 1) {
                return CommandResult{
                    .started = true, .exitCode = 0, .standardOutput = "2.1.245 (Claude Code)"};
            }
            return CommandResult{
                .started = true, .exitCode = 0, .standardOutput = "--input-format stream-json"};
        });
    QCOMPARE(missingOption.status, CliStatus::UnsupportedProtocol);
    QCOMPARE(missingOptionCalls, 2);

    int rejectedProbeCalls = 0;
    const auto rejectedProbe = ClaudeCliDiscovery::probe(
        executable, 50, [&rejectedProbeCalls](const snack::agent::process::LaunchSpec&, int) {
            ++rejectedProbeCalls;
            if (rejectedProbeCalls == 1) {
                return CommandResult{
                    .started = true, .exitCode = 0, .standardOutput = "Claude Code 2.1.245"};
            }
            if (rejectedProbeCalls == 2) {
                return CommandResult{
                    .started = true, .exitCode = 0, .standardOutput = supportedHelp()};
            }
            return CommandResult{.started = true, .exitCode = 1, .standardError = "unknown option"};
        });
    QCOMPARE(rejectedProbe.status, CliStatus::UnsupportedProtocol);
    QCOMPARE(rejectedProbeCalls, 3);
    QVERIFY(rejectedProbe.detail.contains(QStringLiteral("unknown option")));

    const auto timedOut = ClaudeCliDiscovery::probe(
        executable, 50, [](const snack::agent::process::LaunchSpec&, int) {
            return CommandResult{.started = true, .timedOut = true};
        });
    QCOMPARE(timedOut.status, CliStatus::ProbeFailed);
}

void TestClaudeAdapter::probesExecutableWithDefaultRunner() {
    using namespace snack::agent::claude;
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
#ifdef Q_OS_WIN
    const QString executable = directory.filePath(QStringLiteral("claude.cmd"));
    const QByteArray script = "@echo off\r\n"
                              "if \"%~1\"==\"--version\" (\r\n"
                              "  echo Claude Code 9.9.9\r\n"
                              "  exit /b 0\r\n"
                              ")\r\n"
                              "if \"%~1\"==\"--help\" (\r\n"
                              "  echo --input-format stream-json --output-format stream-json\r\n"
                              "  echo --include-partial-messages --replay-user-messages\r\n"
                              "  echo --resume --session-id --model --effort --permission-mode\r\n"
                              "  exit /b 0\r\n"
                              ")\r\n"
                              "if \"%~1\"==\"-p\" exit /b 0\r\n"
                              "exit /b 2\r\n";
#else
    const QString executable = directory.filePath(QStringLiteral("claude"));
    const QByteArray script = "#!/bin/sh\n"
                              "if [ \"$1\" = \"--version\" ]; then\n"
                              "  echo 'Claude Code 9.9.9'\n"
                              "  exit 0\n"
                              "fi\n"
                              "if [ \"$1\" = \"--help\" ]; then\n"
                              "  echo '--input-format stream-json --output-format stream-json'\n"
                              "  echo '--include-partial-messages --replay-user-messages'\n"
                              "  echo '--resume --session-id --model --effort --permission-mode'\n"
                              "  exit 0\n"
                              "fi\n"
                              "if [ \"$1\" = \"-p\" ]; then exit 0; fi\n"
                              "exit 2\n";
#endif
    QFile file(executable);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(script), script.size());
    file.close();
#ifndef Q_OS_WIN
    QVERIFY(file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                                QFileDevice::ExeOwner | QFileDevice::ReadGroup |
                                QFileDevice::ExeGroup | QFileDevice::ReadOther |
                                QFileDevice::ExeOther));
#endif

    const CliInstallation installation = ClaudeCliDiscovery::probe(executable, 1000);
    QVERIFY2(installation.isUsable(), qPrintable(installation.detail));
    QCOMPARE(installation.version, QStringLiteral("9.9.9"));
}

void TestClaudeAdapter::buildsSessionLaunchSpecs() {
    using namespace snack::agent::claude;
    const CliInstallation installation{.status = CliStatus::Available,
#ifdef Q_OS_WIN
                                       .executablePath = QStringLiteral("C:/Tools/claude.cmd"),
#else
                                       .executablePath = QStringLiteral("/usr/local/bin/claude"),
#endif
                                       .version = QStringLiteral("2.1.245")};
    const SessionLaunchOptions options{
        .workingDirectory = QStringLiteral("/workspace"),
        .sessionId = QStringLiteral("10000000-0000-4000-8000-000000000001"),
        .modelId = QStringLiteral("claude-opus-4-1"),
        .reasoningEffort = snack::domain::ReasoningEffort::ExtraHigh,
        .accessLevel = snack::domain::AccessLevel::Workspace,
        .mcpConfigJson = QStringLiteral(R"({"mcpServers":{"snack":{}}})"),
        .permissionPromptTool = QStringLiteral("mcp__snack__permission"),
    };

    const auto fresh = ClaudeCliDiscovery::sessionLaunchSpec(installation, options, false);
    QCOMPARE(fresh.workingDirectory, QStringLiteral("/workspace"));
    QVERIFY(fresh.arguments.contains(QStringLiteral("-p")));
    QVERIFY(fresh.arguments.contains(QStringLiteral("--session-id")));
    QVERIFY(!fresh.arguments.contains(QStringLiteral("--resume")));
    QVERIFY(fresh.arguments.contains(QStringLiteral("--include-partial-messages")));
    QVERIFY(fresh.arguments.contains(QStringLiteral("--replay-user-messages")));
    QVERIFY(fresh.arguments.contains(QStringLiteral("claude-opus-4-1")));
    QVERIFY(fresh.arguments.contains(QStringLiteral("xhigh")));
    QVERIFY(fresh.arguments.contains(QStringLiteral("acceptEdits")));
    QVERIFY(fresh.arguments.contains(QStringLiteral("--mcp-config")));
    QVERIFY(fresh.arguments.contains(QStringLiteral("--permission-prompt-tool")));
    QVERIFY(!fresh.arguments.contains(QStringLiteral("--strict-mcp-config")));
    QVERIFY(!fresh.arguments.contains(QStringLiteral("--safe-mode")));

    const auto resumed = ClaudeCliDiscovery::sessionLaunchSpec(installation, options, true);
    QVERIFY(resumed.arguments.contains(QStringLiteral("--resume")));
    QVERIFY(!resumed.arguments.contains(QStringLiteral("--session-id")));
#ifdef Q_OS_WIN
    QVERIFY(resumed.program.endsWith(QStringLiteral("cmd.exe"), Qt::CaseInsensitive));
    QVERIFY(resumed.arguments.contains(QStringLiteral("call")));
#else
    QCOMPARE(resumed.program, installation.executablePath);
#endif
}

void TestClaudeAdapter::mapsRuntimeControls() {
    using snack::agent::claude::ClaudeCliDiscovery;
    using snack::domain::AccessLevel;
    using snack::domain::ReasoningEffort;

    QCOMPARE(ClaudeCliDiscovery::effortArgument(ReasoningEffort::Minimal), QStringLiteral("low"));
    QCOMPARE(ClaudeCliDiscovery::effortArgument(ReasoningEffort::Low), QStringLiteral("low"));
    QCOMPARE(ClaudeCliDiscovery::effortArgument(ReasoningEffort::Medium), QStringLiteral("medium"));
    QCOMPARE(ClaudeCliDiscovery::effortArgument(ReasoningEffort::High), QStringLiteral("high"));
    QCOMPARE(ClaudeCliDiscovery::effortArgument(ReasoningEffort::ExtraHigh),
             QStringLiteral("xhigh"));
    QCOMPARE(ClaudeCliDiscovery::effortArgument(ReasoningEffort::Maximum), QStringLiteral("max"));
    QCOMPARE(ClaudeCliDiscovery::effortArgument(ReasoningEffort::Ultra), QStringLiteral("max"));

    QCOMPARE(ClaudeCliDiscovery::permissionModeArgument(AccessLevel::Strict),
             QStringLiteral("manual"));
    QCOMPARE(ClaudeCliDiscovery::permissionModeArgument(AccessLevel::Workspace),
             QStringLiteral("acceptEdits"));
    QCOMPARE(ClaudeCliDiscovery::permissionModeArgument(AccessLevel::Full),
             QStringLiteral("bypassPermissions"));
}

void TestClaudeAdapter::parsesAndInitializesFragmentedStream() {
    using namespace snack::agent::claude;
    qRegisterMetaType<StreamState>();
    qRegisterMetaType<StreamRecord>();
    qRegisterMetaType<InitInfo>();

    FakeClaudeProcessTransport transport;
    ClaudeStreamClient client(&transport);
    QSignalSpy stateSpy(&client, &ClaudeStreamClient::stateChanged);
    QSignalSpy initializedSpy(&client, &ClaudeStreamClient::initialized);
    QSignalSpy recordSpy(&client, &ClaudeStreamClient::recordReceived);

    QVERIFY(client.start({.program = QStringLiteral("claude")}, 1000));
    QCOMPARE(client.state(), StreamState::AwaitingInit);
    QCOMPARE(stateSpy.count(), 2);

    const QByteArray startup =
        R"({"type":"system","subtype":"informational","session_id":"session-1","future":true})";
    const QByteArray init =
        R"({"type":"system","subtype":"init","session_id":"session-1","claude_code_version":"2.1.245","cwd":"/workspace","model":"claude-opus-4-1","permissionMode":"manual","tools":["Read","Read"],"capabilities":["interrupt_receipt_v1","future_v9"]})";
    transport.feedOutput(startup + "\r\n" + init.first(70));
    QCOMPARE(recordSpy.count(), 1);
    QCOMPARE(client.state(), StreamState::AwaitingInit);
    transport.feedOutput(init.sliced(70) + "\n");

    QCOMPARE(client.state(), StreamState::Ready);
    QCOMPARE(initializedSpy.count(), 1);
    QCOMPARE(recordSpy.count(), 2);
    const InitInfo info = initializedSpy.constFirst().constFirst().value<InitInfo>();
    QCOMPARE(info.sessionId, QStringLiteral("session-1"));
    QCOMPARE(info.cliVersion, QStringLiteral("2.1.245"));
    QCOMPARE(info.modelId, QStringLiteral("claude-opus-4-1"));
    QCOMPARE(info.tools, QStringList({QStringLiteral("Read")}));
    QCOMPARE(info.capabilities,
             QStringList({QStringLiteral("interrupt_receipt_v1"), QStringLiteral("future_v9")}));
    QCOMPARE(client.initInfo().workingDirectory, QStringLiteral("/workspace"));

    QVERIFY(client.sendEnvelope({{QStringLiteral("type"), QStringLiteral("user")},
                                 {QStringLiteral("session_id"), QStringLiteral("session-1")}}));
    QCOMPARE(transport.writes.size(), 1);
    QVERIFY(transport.writes.constFirst().endsWith('\n'));
}

void TestClaudeAdapter::boundsFramesDiagnosticsAndWrites() {
    using namespace snack::agent::claude;
    FakeClaudeProcessTransport transport;
    ClaudeStreamClient client(&transport, nullptr, 160, 8, 100);
    QSignalSpy warningSpy(&client, &ClaudeStreamClient::protocolWarning);
    QSignalSpy failureSpy(&client, &ClaudeStreamClient::failureOccurred);

    QVERIFY(client.start({.program = QStringLiteral("claude")}, 1000));
    transport.feedError("0123456789abcdef");
    QCOMPARE(client.diagnostics(), QByteArray("89abcdef"));

    const QByteArray init =
        R"({"type":"system","subtype":"init","session_id":"s","claude_code_version":"2.1.245","cwd":"/w","model":"m","permissionMode":"manual"})";
    QVERIFY(init.size() < 160);
    transport.feedOutput(init + "\n");
    QCOMPARE(client.state(), StreamState::Ready);

    QVERIFY(!client.sendEnvelope({}));
    QVERIFY(!client.sendEnvelope({{QStringLiteral("type"), QString(200, QLatin1Char('x'))}}));
    QCOMPARE(warningSpy.count(), 2);
    QVERIFY(failureSpy.isEmpty());

    transport.feedOutput(QByteArray(161, 'x'));
    QCOMPARE(client.state(), StreamState::Failed);
    QCOMPARE(failureSpy.count(), 1);
    QVERIFY(failureSpy.constFirst().constFirst().toString().contains(QStringLiteral("oversized")));
}

void TestClaudeAdapter::rejectsInvalidAndCrossSessionStreams() {
    using namespace snack::agent::claude;
    const QByteArray init =
        R"({"type":"system","subtype":"init","session_id":"session-a","claude_code_version":"2.1.245","cwd":"/w","model":"m","permissionMode":"manual"})";

    FakeClaudeProcessTransport malformedTransport;
    ClaudeStreamClient malformedClient(&malformedTransport);
    QSignalSpy malformedFailure(&malformedClient, &ClaudeStreamClient::failureOccurred);
    QVERIFY(malformedClient.start({.program = QStringLiteral("claude")}));
    malformedTransport.feedOutput("{}\n");
    QCOMPARE(malformedClient.state(), StreamState::Failed);
    QCOMPARE(malformedFailure.count(), 1);

    FakeClaudeProcessTransport earlyTurnTransport;
    ClaudeStreamClient earlyTurnClient(&earlyTurnTransport);
    QVERIFY(earlyTurnClient.start({.program = QStringLiteral("claude")}));
    earlyTurnTransport.feedOutput(R"({"type":"assistant","session_id":"session-a","message":{}})"
                                  "\n");
    QCOMPARE(earlyTurnClient.state(), StreamState::Failed);

    FakeClaudeProcessTransport crossTransport;
    ClaudeStreamClient crossClient(&crossTransport);
    QSignalSpy crossFailure(&crossClient, &ClaudeStreamClient::failureOccurred);
    QVERIFY(crossClient.start({.program = QStringLiteral("claude")}));
    crossTransport.feedOutput(init + "\n");
    QCOMPARE(crossClient.state(), StreamState::Ready);
    crossTransport.feedOutput(R"({"type":"result","session_id":"session-b","subtype":"success"})"
                              "\n");
    QCOMPARE(crossClient.state(), StreamState::Failed);
    QCOMPARE(crossFailure.count(), 1);

    FakeClaudeProcessTransport invalidInitTransport;
    ClaudeStreamClient invalidInitClient(&invalidInitTransport);
    QVERIFY(invalidInitClient.start({.program = QStringLiteral("claude")}));
    invalidInitTransport.feedOutput(R"({"type":"system","subtype":"init","session_id":"s"})"
                                    "\n");
    QCOMPARE(invalidInitClient.state(), StreamState::Failed);
}

void TestClaudeAdapter::handlesTimeoutExitAndShutdown() {
    using namespace snack::agent::claude;

    FakeClaudeProcessTransport timeoutTransport;
    ClaudeStreamClient timeoutClient(&timeoutTransport, nullptr, 1024, 1024, 20);
    QSignalSpy timeoutFailure(&timeoutClient, &ClaudeStreamClient::failureOccurred);
    QVERIFY(timeoutClient.start({.program = QStringLiteral("claude")}, 10));
    QTRY_COMPARE_WITH_TIMEOUT(timeoutFailure.count(), 1, 100);
    QCOMPARE(timeoutClient.state(), StreamState::Failed);

    FakeClaudeProcessTransport exitTransport;
    ClaudeStreamClient exitClient(&exitTransport);
    QSignalSpy exitFailure(&exitClient, &ClaudeStreamClient::failureOccurred);
    QVERIFY(exitClient.start({.program = QStringLiteral("claude")}));
    exitTransport.finish(2, snack::agent::process::ExitStatus::Normal);
    QCOMPARE(exitFailure.count(), 1);
    QVERIFY(exitFailure.constFirst().constFirst().toString().contains(QStringLiteral("code 2")));

    FakeClaudeProcessTransport shutdownTransport;
    shutdownTransport.deferTermination = true;
    ClaudeStreamClient shutdownClient(&shutdownTransport, nullptr, 1024, 1024, 10);
    QSignalSpy warningSpy(&shutdownClient, &ClaudeStreamClient::protocolWarning);
    QVERIFY(shutdownClient.start({.program = QStringLiteral("claude")}));
    shutdownClient.stop();
    QCOMPARE(shutdownTransport.closeCalls, 1);
    QCOMPARE(shutdownTransport.terminateCalls, 1);
    QTRY_COMPARE_WITH_TIMEOUT(shutdownTransport.killCalls, 1, 100);
    QCOMPARE(warningSpy.count(), 1);
    QCOMPARE(shutdownClient.state(), StreamState::Stopped);

    FakeClaudeProcessTransport writeTransport;
    ClaudeStreamClient writeClient(&writeTransport);
    QVERIFY(writeClient.start({.program = QStringLiteral("claude")}));
    writeTransport.feedOutput(
        R"({"type":"system","subtype":"init","session_id":"s","claude_code_version":"2.1.245","cwd":"/w","model":"m","permissionMode":"manual"})"
        "\n");
    writeTransport.failWrites = true;
    QVERIFY(!writeClient.sendEnvelope({{QStringLiteral("type"), QStringLiteral("user")}}));
    QCOMPARE(writeClient.state(), StreamState::Failed);
}

QTEST_MAIN(TestClaudeAdapter)

#include "TestClaudeAdapter.moc"
