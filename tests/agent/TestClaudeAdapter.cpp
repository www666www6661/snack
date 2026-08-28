#include "agent/claude/ClaudeAdapter.h"
#include "agent/claude/ClaudeCliDiscovery.h"
#include "agent/claude/ClaudeStreamClient.h"

#include <QFile>
#include <QJsonDocument>
#include <QLocalSocket>
#include <QProcess>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

namespace domain = snack::domain;

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
    void adapterConnectsNewAndResumedSessions();
    void adapterSendsAndCorrelatesMultipleTurns();
    void adapterRejectsInvalidLifecycleRequests();
    void mapsVisibleStreamEventsAndRedactsThinking();
    void mapsAndAnswersClaudeQuestions();
    void bridgesClaudePermissionRequests();
    void servesPermissionMcpContract();
    void restartsAndResumesForSettingsAndInterrupts();
    void encodesBoundedClaudeImageAttachments();
};

class FakeClaudeProcessTransport final : public snack::agent::process::IProcessTransport {
  public:
    using IProcessTransport::IProcessTransport;

    [[nodiscard]] bool isRunning() const override { return running; }

    void start(const snack::agent::process::LaunchSpec& value) override {
        ++startCalls;
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
    int startCalls{0};
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

void TestClaudeAdapter::adapterConnectsNewAndResumedSessions() {
    using namespace snack::agent;
    using namespace snack::agent::claude;
    const CliInstallation installation{.status = CliStatus::Available,
                                       .executablePath = QStringLiteral("claude"),
                                       .version = QStringLiteral("2.1.245")};

    FakeClaudeProcessTransport newTransport;
    ClaudeAdapter newAdapter(installation, &newTransport);
    QSignalSpy identitySpy(&newAdapter, &IAgentAdapter::nativeIdentityChanged);
    QSignalSpy connectionSpy(&newAdapter, &IAgentAdapter::connectionChanged);
    QSignalSpy capabilitySpy(&newAdapter, &IAgentAdapter::capabilitiesChanged);
    domain::TurnSettingsSnapshot settings;
    settings.agentKind = domain::AgentKind::Claude;
    settings.modelId = QStringLiteral("sonnet");
    settings.reasoningEffort = domain::ReasoningEffort::High;
    settings.accessLevel = domain::AccessLevel::Workspace;
    settings.workingDirectory = QStringLiteral("/workspace");

    newAdapter.connectAgent(
        {.workingDirectory = QStringLiteral("/workspace"), .settings = settings});
    QVERIFY(newTransport.launchSpec.arguments.contains(QStringLiteral("--session-id")));
    QVERIFY(!newTransport.launchSpec.arguments.contains(QStringLiteral("--resume")));
    const qsizetype sessionIndex =
        newTransport.launchSpec.arguments.indexOf(QStringLiteral("--session-id"));
    QVERIFY(sessionIndex >= 0);
    const QString sessionId = newTransport.launchSpec.arguments.at(sessionIndex + 1);
    newTransport.feedOutput(
        QStringLiteral(
            R"({"type":"system","subtype":"init","session_id":"%1","claude_code_version":"2.1.245","cwd":"/workspace","model":"claude-sonnet-current","permissionMode":"acceptEdits","tools":[],"capabilities":["interrupt_receipt_v1"]}
)")
            .arg(sessionId)
            .toUtf8());

    QCOMPARE(connectionSpy.count(), 1);
    QCOMPARE(connectionSpy.constFirst().at(0).toBool(), true);
    QCOMPARE(identitySpy.count(), 1);
    QCOMPARE(identitySpy.constFirst().at(0).toString(), sessionId);
    QCOMPARE(identitySpy.constFirst().at(1).toString(), sessionId);
    QCOMPARE(capabilitySpy.count(), 1);
    QVERIFY(newAdapter.capabilities().models.contains(QStringLiteral("claude-sonnet-current")));
    QVERIFY(!newAdapter.capabilities().supportsSteering);

    FakeClaudeProcessTransport resumedTransport;
    ClaudeAdapter resumedAdapter(installation, &resumedTransport);
    resumedAdapter.connectAgent({.workingDirectory = QStringLiteral("/workspace"),
                                 .nativeThreadId = QStringLiteral("resume-session"),
                                 .settings = settings});
    QVERIFY(resumedTransport.launchSpec.arguments.contains(QStringLiteral("--resume")));
    QVERIFY(!resumedTransport.launchSpec.arguments.contains(QStringLiteral("--session-id")));
    QVERIFY(resumedTransport.launchSpec.arguments.contains(QStringLiteral("resume-session")));
}

void TestClaudeAdapter::adapterSendsAndCorrelatesMultipleTurns() {
    using namespace snack::agent;
    using namespace snack::agent::claude;
    const CliInstallation installation{.status = CliStatus::Available,
                                       .executablePath = QStringLiteral("claude"),
                                       .version = QStringLiteral("2.1.245")};
    FakeClaudeProcessTransport transport;
    ClaudeAdapter adapter(installation, &transport);
    QSignalSpy eventSpy(&adapter, &IAgentAdapter::eventReceived);
    QSignalSpy finishedSpy(&adapter, &IAgentAdapter::turnFinished);
    domain::TurnSettingsSnapshot settings;
    settings.agentKind = domain::AgentKind::Claude;
    settings.modelId = QStringLiteral("sonnet");
    settings.reasoningEffort = domain::ReasoningEffort::Medium;
    settings.accessLevel = domain::AccessLevel::Strict;
    settings.workingDirectory = QStringLiteral("/workspace");
    adapter.connectAgent({.workingDirectory = QStringLiteral("/workspace"),
                          .nativeThreadId = QStringLiteral("session-1"),
                          .settings = settings});
    transport.feedOutput(
        R"({"type":"system","subtype":"init","session_id":"session-1","claude_code_version":"2.1.245","cwd":"/workspace","model":"sonnet","permissionMode":"manual"})"
        "\n");

    const QUuid firstTurn = QUuid::createUuid();
    adapter.startTurn({firstTurn, QStringLiteral("First"), settings, {}});
    QCOMPARE(transport.writes.size(), 1);
    const QJsonObject firstEnvelope = QJsonDocument::fromJson(transport.writes.at(0)).object();
    QCOMPARE(firstEnvelope.value(QStringLiteral("type")).toString(), QStringLiteral("user"));
    QCOMPARE(firstEnvelope.value(QStringLiteral("uuid")).toString(),
             firstTurn.toString(QUuid::WithoutBraces));
    QCOMPARE(firstEnvelope.value(QStringLiteral("session_id")).toString(),
             QStringLiteral("session-1"));
    QCOMPARE(firstEnvelope.value(QStringLiteral("message"))
                 .toObject()
                 .value(QStringLiteral("content"))
                 .toArray()
                 .at(0)
                 .toObject()
                 .value(QStringLiteral("text"))
                 .toString(),
             QStringLiteral("First"));

    transport.feedOutput(
        R"({"type":"result","subtype":"success","session_id":"session-1","user_message_uuid":"unknown","is_error":false,"result":"stale"})"
        "\n");
    QCOMPARE(finishedSpy.count(), 0);
    transport.feedOutput(
        QStringLiteral(
            R"({"type":"result","subtype":"success","session_id":"session-1","user_message_uuid":"%1","is_error":false,"result":"one"}
)")
            .arg(firstTurn.toString(QUuid::WithoutBraces))
            .toUtf8());
    QCOMPARE(finishedSpy.count(), 1);
    QCOMPARE(finishedSpy.constFirst().at(0).toUuid(), firstTurn);
    QCOMPARE(finishedSpy.constFirst().at(2).toBool(), true);

    const QUuid secondTurn = QUuid::createUuid();
    adapter.startTurn({secondTurn, QStringLiteral("Second"), settings, {}});
    QCOMPARE(transport.writes.size(), 2);
    transport.feedOutput(
        QStringLiteral(
            R"({"type":"result","subtype":"error_during_execution","session_id":"session-1","user_message_uuid":"%1","is_error":true,"result":"failed synthetic turn"}
)")
            .arg(secondTurn.toString(QUuid::WithoutBraces))
            .toUtf8());
    QCOMPARE(finishedSpy.count(), 2);
    QCOMPARE(finishedSpy.at(1).at(0).toUuid(), secondTurn);
    QCOMPARE(finishedSpy.at(1).at(2).toBool(), false);

    QList<domain::AgentEventType> types;
    for (const QList<QVariant>& arguments : eventSpy) {
        types.append(arguments.constFirst().value<domain::AgentEvent>().type);
    }
    QVERIFY(types.contains(domain::AgentEventType::TurnStarted));
    QVERIFY(types.contains(domain::AgentEventType::WarningRaised));
    QVERIFY(types.contains(domain::AgentEventType::TurnCompleted));
    QVERIFY(types.contains(domain::AgentEventType::TurnFailed));
}

void TestClaudeAdapter::adapterRejectsInvalidLifecycleRequests() {
    using namespace snack::agent;
    using namespace snack::agent::claude;
    const CliInstallation installation{.status = CliStatus::Available,
                                       .executablePath = QStringLiteral("claude"),
                                       .version = QStringLiteral("2.1.245")};
    FakeClaudeProcessTransport transport;
    ClaudeAdapter adapter(installation, &transport);
    QSignalSpy connectionSpy(&adapter, &IAgentAdapter::connectionChanged);
    QSignalSpy finishedSpy(&adapter, &IAgentAdapter::turnFinished);

    domain::TurnSettingsSnapshot settings;
    settings.agentKind = domain::AgentKind::Claude;
    settings.modelId = QStringLiteral("sonnet");
    settings.workingDirectory = QStringLiteral("/workspace");
    adapter.startTurn({QUuid::createUuid(), QStringLiteral("early"), settings, {}});
    QCOMPARE(finishedSpy.count(), 1);

    adapter.connectAgent({.workingDirectory = QStringLiteral("/workspace"),
                          .nativeThreadId = QStringLiteral("expected"),
                          .settings = settings});
    transport.feedOutput(
        R"({"type":"system","subtype":"init","session_id":"wrong","claude_code_version":"2.1.245","cwd":"/workspace","model":"sonnet","permissionMode":"manual"})"
        "\n");
    QVERIFY(!connectionSpy.isEmpty());
    QCOMPARE(connectionSpy.constLast().at(0).toBool(), false);
    QVERIFY(connectionSpy.constLast().at(1).toString().contains(QStringLiteral("session")));

    FakeClaudeProcessTransport closeTransport;
    ClaudeAdapter closeAdapter(installation, &closeTransport);
    QSignalSpy closeConnectionSpy(&closeAdapter, &IAgentAdapter::connectionChanged);
    closeAdapter.connectAgent({.workingDirectory = QStringLiteral("/workspace"),
                               .nativeThreadId = QStringLiteral("session-2"),
                               .settings = settings});
    closeTransport.feedOutput(
        R"({"type":"system","subtype":"init","session_id":"session-2","claude_code_version":"2.1.245","cwd":"/workspace","model":"sonnet","permissionMode":"manual"})"
        "\n");
    closeAdapter.closeAgent();
    QCOMPARE(closeConnectionSpy.count(), 2);
    QCOMPARE(closeConnectionSpy.constLast().at(0).toBool(), false);
    QCOMPARE(closeConnectionSpy.constLast().at(1).toString(), QStringLiteral("closed"));
}

void TestClaudeAdapter::mapsVisibleStreamEventsAndRedactsThinking() {
    using namespace snack::agent::claude;
    ClaudeEventMapper mapper;
    mapper.reset();

    QList<MappedEvent> mapped;
    const QList<QByteArray> lines = {
        R"({"type":"stream_event","event":{"type":"message_start","message":{"id":"msg-1","usage":{"input_tokens":10,"output_tokens":0}}}})",
        R"({"type":"stream_event","event":{"type":"content_block_start","index":0,"content_block":{"type":"thinking","thinking":"private thought","signature":"secret"}}})",
        R"({"type":"stream_event","event":{"type":"content_block_delta","index":0,"delta":{"type":"thinking_delta","thinking":"more private thought"}}})",
        R"({"type":"stream_event","event":{"type":"content_block_stop","index":0}})",
        R"({"type":"stream_event","event":{"type":"content_block_start","index":1,"content_block":{"type":"text","text":""}}})",
        R"({"type":"stream_event","event":{"type":"content_block_delta","index":1,"delta":{"type":"text_delta","text":"Hello "}}})",
        R"({"type":"stream_event","event":{"type":"content_block_delta","index":1,"delta":{"type":"text_delta","text":"world"}}})",
        R"({"type":"stream_event","event":{"type":"content_block_stop","index":1}})",
        R"({"type":"stream_event","event":{"type":"content_block_start","index":2,"content_block":{"type":"tool_use","id":"tool-1","name":"Read","input":{}}}})",
        R"({"type":"assistant","message":{"id":"msg-1","content":[{"type":"thinking","thinking":"complete private thought","signature":"secret"},{"type":"text","text":"Hello world"},{"type":"tool_use","id":"tool-1","name":"Read","input":{"file_path":"README.md"}}],"usage":{"input_tokens":10,"output_tokens":4,"cache_read_input_tokens":3}}})",
        R"({"type":"user","message":{"role":"user","content":[{"type":"tool_result","tool_use_id":"tool-1","content":"file output","is_error":false}]}})",
        R"({"type":"result","subtype":"success","usage":{"input_tokens":10,"output_tokens":4,"cache_read_input_tokens":3}})",
    };
    for (const QByteArray& line : lines) {
        const StreamRecord record = parseStreamRecord(line);
        QCOMPARE_NE(record.kind, StreamRecordKind::Malformed);
        mapped.append(mapper.consume(record));
    }

    QList<domain::AgentEventType> types;
    QString visibleText;
    bool toolStarted = false;
    bool toolCompleted = false;
    bool usageMapped = false;
    for (const MappedEvent& event : mapped) {
        types.append(event.type);
        if (event.type == domain::AgentEventType::AgentMessageDelta) {
            visibleText.append(event.payload.value(QStringLiteral("text")).toString());
        } else if (event.type == domain::AgentEventType::ToolStarted) {
            toolStarted =
                event.payload.value(QStringLiteral("itemId")) == QLatin1String("tool-1") &&
                event.payload.value(QStringLiteral("tool")) == QLatin1String("Read");
        } else if (event.type == domain::AgentEventType::ToolCompleted) {
            toolCompleted = event.payload.value(QStringLiteral("aggregatedOutput")) ==
                            QLatin1String("file output");
        } else if (event.type == domain::AgentEventType::UsageUpdated) {
            const QJsonObject total = event.payload.value(QStringLiteral("total")).toObject();
            usageMapped = total.value(QStringLiteral("totalTokens")).toInteger() == 17;
        }

        const QByteArray serialized =
            QJsonDocument(event.rawPayload).toJson(QJsonDocument::Compact);
        QVERIFY(!serialized.contains("private thought"));
        QVERIFY(!serialized.contains("more private thought"));
        QVERIFY(!serialized.contains("complete private thought"));
        QVERIFY(!serialized.contains("secret"));
    }

    QCOMPARE(visibleText, QStringLiteral("Hello world"));
    QVERIFY(types.contains(domain::AgentEventType::AgentMessageStart));
    QCOMPARE(types.count(domain::AgentEventType::AgentMessageComplete), 1);
    QVERIFY(types.contains(domain::AgentEventType::ReasoningStarted));
    QVERIFY(types.contains(domain::AgentEventType::ReasoningCompleted));
    QVERIFY(toolStarted);
    QVERIFY(toolCompleted);
    QVERIFY(usageMapped);

    const QJsonObject sanitized =
        sanitizedClaudePayload(QJsonDocument::fromJson(lines.at(9)).object());
    const QByteArray serialized = QJsonDocument(sanitized).toJson(QJsonDocument::Compact);
    QVERIFY(serialized.contains("redacted"));
    QVERIFY(!serialized.contains("complete private thought"));
    QVERIFY(!serialized.contains("signature"));
}

void TestClaudeAdapter::mapsAndAnswersClaudeQuestions() {
    using namespace snack::agent;
    using namespace snack::agent::claude;

    ClaudeEventMapper mapper;
    mapper.reset();
    const StreamRecord questionRecord = parseStreamRecord(
        R"({"type":"assistant","message":{"id":"msg-q","content":[{"type":"tool_use","id":"ask-1","name":"AskUserQuestion","input":{"questions":[{"header":"Choice","question":"Which path?","multiSelect":false,"options":[{"label":"A","description":"First"},{"label":"B","description":"Second"}]},{"header":"Details","question":"Why?","multiSelect":true,"options":[]}]}}]}})");
    const QList<MappedEvent> mapped = mapper.consume(questionRecord);
    QCOMPARE(mapped.size(), 1);
    QCOMPARE(mapped.constFirst().type, domain::AgentEventType::UserInputRequested);
    const QJsonObject request = mapped.constFirst().payload;
    QCOMPARE(request.value(QStringLiteral("requestId")).toString(), QStringLiteral("ask-1"));
    QVERIFY(request.value(QStringLiteral("isBlocking")).toBool());
    const QJsonArray questions = request.value(QStringLiteral("questions")).toArray();
    QCOMPARE(questions.size(), 2);
    QCOMPARE(questions.at(0).toObject().value(QStringLiteral("options")).toArray().size(), 2);
    QVERIFY(questions.at(1).toObject().value(QStringLiteral("options")).isNull());

    const CliInstallation installation{.status = CliStatus::Available,
                                       .executablePath = QStringLiteral("claude"),
                                       .version = QStringLiteral("2.1.245")};
    FakeClaudeProcessTransport transport;
    ClaudeAdapter adapter(installation, &transport);
    QSignalSpy eventSpy(&adapter, &IAgentAdapter::eventReceived);
    domain::TurnSettingsSnapshot settings;
    settings.agentKind = domain::AgentKind::Claude;
    settings.modelId = QStringLiteral("sonnet");
    settings.reasoningEffort = domain::ReasoningEffort::Medium;
    settings.accessLevel = domain::AccessLevel::Strict;
    settings.workingDirectory = QStringLiteral("/workspace");
    adapter.connectAgent({.workingDirectory = QStringLiteral("/workspace"),
                          .nativeThreadId = QStringLiteral("session-q"),
                          .settings = settings});
    transport.feedOutput(
        R"({"type":"system","subtype":"init","session_id":"session-q","claude_code_version":"2.1.245","cwd":"/workspace","model":"sonnet","permissionMode":"manual"})"
        "\n");
    adapter.startTurn({QUuid::createUuid(), QStringLiteral("Ask me"), settings, {}});
    QCOMPARE(transport.writes.size(), 1);
    transport.feedOutput(
        R"({"type":"assistant","session_id":"session-q","message":{"id":"msg-q","content":[{"type":"tool_use","id":"ask-1","name":"AskUserQuestion","input":{"questions":[{"header":"Choice","question":"Which path?","multiSelect":false,"options":[{"label":"A","description":"First"},{"label":"B","description":"Second"}]},{"header":"Details","question":"Why?","multiSelect":true,"options":[]}]}}]}})"
        "\n");

    QVERIFY(std::any_of(eventSpy.cbegin(), eventSpy.cend(), [](const QList<QVariant>& arguments) {
        return arguments.constFirst().value<domain::AgentEvent>().type ==
               domain::AgentEventType::UserInputRequested;
    }));
    const QJsonObject answers{
        {QStringLiteral("question-0"),
         QJsonObject{{QStringLiteral("answers"), QJsonArray{QStringLiteral("A")}}}},
        {QStringLiteral("question-1"),
         QJsonObject{{QStringLiteral("answers"), QJsonArray{QStringLiteral("Because")}}}},
    };
    QVERIFY(!adapter.respondToUserInput(QStringLiteral("ask-1"), {}));
    QVERIFY(adapter.respondToUserInput(QStringLiteral("ask-1"), answers));
    QVERIFY(!adapter.respondToUserInput(QStringLiteral("ask-1"), answers));
    QCOMPARE(transport.writes.size(), 2);
    const QJsonObject response = QJsonDocument::fromJson(transport.writes.constLast()).object();
    const QJsonObject message = response.value(QStringLiteral("message")).toObject();
    const QJsonObject toolResult =
        message.value(QStringLiteral("content")).toArray().at(0).toObject();
    QCOMPARE(toolResult.value(QStringLiteral("tool_use_id")).toString(), QStringLiteral("ask-1"));
    const QJsonObject answerContent =
        QJsonDocument::fromJson(toolResult.value(QStringLiteral("content")).toString().toUtf8())
            .object()
            .value(QStringLiteral("answers"))
            .toObject();
    QCOMPARE(answerContent.value(QStringLiteral("Which path?")).toString(), QStringLiteral("A"));
    QCOMPARE(answerContent.value(QStringLiteral("Why?")).toString(), QStringLiteral("Because"));
}

void TestClaudeAdapter::bridgesClaudePermissionRequests() {
    using namespace snack::agent;
    using namespace snack::agent::claude;
    const CliInstallation installation{.status = CliStatus::Available,
                                       .executablePath = QStringLiteral("claude"),
                                       .version = QStringLiteral("2.1.245")};
    FakeClaudeProcessTransport transport;
    ClaudeAdapter adapter(installation, &transport, nullptr,
                          QStringLiteral(SNACK_CLAUDE_PERMISSION_SERVER));
    QSignalSpy eventSpy(&adapter, &IAgentAdapter::eventReceived);
    domain::TurnSettingsSnapshot settings;
    settings.agentKind = domain::AgentKind::Claude;
    settings.modelId = QStringLiteral("sonnet");
    settings.reasoningEffort = domain::ReasoningEffort::Medium;
    settings.accessLevel = domain::AccessLevel::Strict;
    settings.workingDirectory = QStringLiteral("/workspace");
    adapter.connectAgent({.workingDirectory = QStringLiteral("/workspace"),
                          .nativeThreadId = QStringLiteral("session-p"),
                          .settings = settings});

    const qsizetype configIndex =
        transport.launchSpec.arguments.indexOf(QStringLiteral("--mcp-config"));
    QVERIFY(configIndex >= 0);
    const QJsonObject config =
        QJsonDocument::fromJson(transport.launchSpec.arguments.at(configIndex + 1).toUtf8())
            .object();
    const QJsonObject permissionServer = config.value(QStringLiteral("mcpServers"))
                                             .toObject()
                                             .value(QStringLiteral("snack_permission"))
                                             .toObject();
    QCOMPARE(permissionServer.value(QStringLiteral("command")).toString(),
             QStringLiteral(SNACK_CLAUDE_PERMISSION_SERVER));
    const QJsonArray helperArguments = permissionServer.value(QStringLiteral("args")).toArray();
    const QString serverName = helperArguments.at(1).toString();
    const QString token = helperArguments.at(3).toString();
    QVERIFY(!serverName.isEmpty());
    QCOMPARE(token.size(), 64);
    QVERIFY(transport.launchSpec.arguments.contains(QStringLiteral("--permission-prompt-tool")));
    QVERIFY(!transport.launchSpec.arguments.contains(QStringLiteral("--strict-mcp-config")));

    transport.feedOutput(
        R"({"type":"system","subtype":"init","session_id":"session-p","claude_code_version":"2.1.245","cwd":"/workspace","model":"sonnet","permissionMode":"manual"})"
        "\n");
    adapter.startTurn({QUuid::createUuid(), QStringLiteral("Run a command"), settings, {}});

    QLocalSocket socket;
    socket.connectToServer(serverName);
    QVERIFY(socket.waitForConnected(1000));
    const QJsonObject permissionArguments{
        {QStringLiteral("tool_name"), QStringLiteral("Bash")},
        {QStringLiteral("input"),
         QJsonObject{{QStringLiteral("command"), QStringLiteral("cmake --build .")},
                     {QStringLiteral("cwd"), QStringLiteral("/workspace")}}},
        {QStringLiteral("reason"), QStringLiteral("Build the project")},
        {QStringLiteral("permission_suggestions"),
         QJsonArray{QJsonObject{{QStringLiteral("type"), QStringLiteral("allow")}}}},
    };
    QByteArray frame =
        QJsonDocument(QJsonObject{{QStringLiteral("token"), token},
                                  {QStringLiteral("requestId"), QStringLiteral("permission-1")},
                                  {QStringLiteral("arguments"), permissionArguments}})
            .toJson(QJsonDocument::Compact);
    frame.append('\n');
    QCOMPARE(socket.write(frame), frame.size());
    QTRY_COMPARE_WITH_TIMEOUT(socket.bytesToWrite(), qint64{0}, 1000);
    QTRY_VERIFY_WITH_TIMEOUT(
        std::any_of(eventSpy.cbegin(), eventSpy.cend(),
                    [](const QList<QVariant>& arguments) {
                        return arguments.constFirst().value<domain::AgentEvent>().type ==
                               domain::AgentEventType::ApprovalRequested;
                    }),
        1000);
    QVERIFY(adapter.respondToApproval(QStringLiteral("permission-1"),
                                      domain::ApprovalDecision::AcceptForSession));
    QVERIFY(!adapter.respondToApproval(QStringLiteral("permission-1"),
                                       domain::ApprovalDecision::Accept));
    QTRY_VERIFY_WITH_TIMEOUT(socket.canReadLine(), 1000);
    const QJsonObject response = QJsonDocument::fromJson(socket.readLine()).object();
    const QJsonObject decision = response.value(QStringLiteral("decision")).toObject();
    QCOMPARE(decision.value(QStringLiteral("behavior")).toString(), QStringLiteral("allow"));
    QCOMPARE(decision.value(QStringLiteral("updatedInput"))
                 .toObject()
                 .value(QStringLiteral("command"))
                 .toString(),
             QStringLiteral("cmake --build ."));
    QVERIFY(decision.value(QStringLiteral("updatedPermissions")).isArray());

    const QJsonObject deny =
        claudePermissionDecision(domain::ApprovalDecision::Decline, permissionArguments);
    QCOMPARE(deny.value(QStringLiteral("behavior")).toString(), QStringLiteral("deny"));
    const QJsonObject filePayload = claudeApprovalEventPayload(
        QStringLiteral("file-1"),
        {{QStringLiteral("tool_name"), QStringLiteral("Write")},
         {QStringLiteral("input"),
          QJsonObject{{QStringLiteral("file_path"), QStringLiteral("/workspace/a.cpp")}}}});
    QCOMPARE(filePayload.value(QStringLiteral("kind")).toString(), QStringLiteral("fileChange"));
    QCOMPARE(filePayload.value(QStringLiteral("grantRoot")).toString(),
             QStringLiteral("/workspace/a.cpp"));
}

void TestClaudeAdapter::servesPermissionMcpContract() {
    using namespace snack::agent::claude;
    ClaudePermissionBridge bridge;
    QVERIFY(bridge.start());
    QSignalSpy permissionSpy(&bridge, &ClaudePermissionBridge::permissionRequested);

    QProcess process;
    process.start(QStringLiteral(SNACK_CLAUDE_PERMISSION_SERVER),
                  {QStringLiteral("--bridge-server"), bridge.serverName(),
                   QStringLiteral("--token"), bridge.authenticationToken()});
    QVERIFY(process.waitForStarted(2000));
    const auto transact = [&process](const QJsonObject& request) {
        QByteArray requestFrame = QJsonDocument(request).toJson(QJsonDocument::Compact);
        requestFrame.append('\n');
        if (process.write(requestFrame) != requestFrame.size() ||
            !process.waitForBytesWritten(1000)) {
            return QJsonObject{};
        }
        while (!process.canReadLine() && process.waitForReadyRead(1000)) {
        }
        return QJsonDocument::fromJson(process.readLine()).object();
    };

    const QJsonObject initialize = transact(
        {{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
         {QStringLiteral("id"), 1},
         {QStringLiteral("method"), QStringLiteral("initialize")},
         {QStringLiteral("params"),
          QJsonObject{{QStringLiteral("protocolVersion"), QStringLiteral("2025-06-18")}}}});
    QCOMPARE(initialize.value(QStringLiteral("result"))
                 .toObject()
                 .value(QStringLiteral("protocolVersion"))
                 .toString(),
             QStringLiteral("2025-06-18"));
    const QJsonObject tools = transact({{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
                                        {QStringLiteral("id"), 2},
                                        {QStringLiteral("method"), QStringLiteral("tools/list")}});
    QCOMPARE(tools.value(QStringLiteral("result"))
                 .toObject()
                 .value(QStringLiteral("tools"))
                 .toArray()
                 .size(),
             1);

    QByteArray callFrame =
        QJsonDocument(
            QJsonObject{
                {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
                {QStringLiteral("id"), 3},
                {QStringLiteral("method"), QStringLiteral("tools/call")},
                {QStringLiteral("params"),
                 QJsonObject{{QStringLiteral("name"), QStringLiteral("permission")},
                             {QStringLiteral("arguments"),
                              QJsonObject{{QStringLiteral("tool_name"), QStringLiteral("Bash")},
                                          {QStringLiteral("input"),
                                           QJsonObject{{QStringLiteral("command"),
                                                        QStringLiteral("echo safe")}}}}}}}})
            .toJson(QJsonDocument::Compact);
    callFrame.append('\n');
    QCOMPARE(process.write(callFrame), callFrame.size());
    QVERIFY(process.waitForBytesWritten(1000));
    QTRY_COMPARE_WITH_TIMEOUT(permissionSpy.count(), 1, 2000);
    const QString requestId = permissionSpy.constFirst().at(0).toString();
    QVERIFY(bridge.resolve(requestId,
                           {{QStringLiteral("behavior"), QStringLiteral("deny")},
                            {QStringLiteral("message"), QStringLiteral("Synthetic denial")}}));
    QTRY_VERIFY_WITH_TIMEOUT(process.canReadLine(), 2000);
    const QJsonObject callResponse = QJsonDocument::fromJson(process.readLine()).object();
    const QString decisionText = callResponse.value(QStringLiteral("result"))
                                     .toObject()
                                     .value(QStringLiteral("content"))
                                     .toArray()
                                     .at(0)
                                     .toObject()
                                     .value(QStringLiteral("text"))
                                     .toString();
    const QJsonObject decision = QJsonDocument::fromJson(decisionText.toUtf8()).object();
    QCOMPARE(decision.value(QStringLiteral("behavior")).toString(), QStringLiteral("deny"));

    process.closeWriteChannel();
    QVERIFY(process.waitForFinished(2000));
    QCOMPARE(process.exitCode(), 0);
}

void TestClaudeAdapter::restartsAndResumesForSettingsAndInterrupts() {
    using namespace snack::agent;
    using namespace snack::agent::claude;
    const CliInstallation installation{.status = CliStatus::Available,
                                       .executablePath = QStringLiteral("claude"),
                                       .version = QStringLiteral("2.1.245")};
    FakeClaudeProcessTransport transport;
    ClaudeAdapter adapter(installation, &transport, nullptr,
                          QStringLiteral(SNACK_CLAUDE_PERMISSION_SERVER));
    QSignalSpy connectionSpy(&adapter, &IAgentAdapter::connectionChanged);
    QSignalSpy eventSpy(&adapter, &IAgentAdapter::eventReceived);
    QSignalSpy finishedSpy(&adapter, &IAgentAdapter::turnFinished);
    domain::TurnSettingsSnapshot settings;
    settings.agentKind = domain::AgentKind::Claude;
    settings.modelId = QStringLiteral("sonnet");
    settings.reasoningEffort = domain::ReasoningEffort::Medium;
    settings.accessLevel = domain::AccessLevel::Strict;
    settings.workingDirectory = QStringLiteral("/workspace");
    adapter.connectAgent({.workingDirectory = QStringLiteral("/workspace"),
                          .nativeThreadId = QStringLiteral("session-r"),
                          .settings = settings});
    transport.feedOutput(
        R"({"type":"system","subtype":"init","session_id":"session-r","claude_code_version":"2.1.245","cwd":"/workspace","model":"sonnet","permissionMode":"manual"})"
        "\n");
    QCOMPARE(connectionSpy.count(), 1);

    domain::TurnSettingsSnapshot changed = settings;
    changed.modelId = QStringLiteral("opus");
    changed.reasoningEffort = domain::ReasoningEffort::ExtraHigh;
    changed.accessLevel = domain::AccessLevel::Full;
    const QUuid changedTurn = QUuid::createUuid();
    adapter.startTurn({changedTurn, QStringLiteral("Use new settings"), changed, {}});
    QCOMPARE(transport.startCalls, 2);
    QCOMPARE(transport.terminateCalls, 1);
    QVERIFY(transport.writes.isEmpty());
    QVERIFY(transport.launchSpec.arguments.contains(QStringLiteral("--resume")));
    QVERIFY(transport.launchSpec.arguments.contains(QStringLiteral("session-r")));
    QVERIFY(transport.launchSpec.arguments.contains(QStringLiteral("opus")));
    QVERIFY(transport.launchSpec.arguments.contains(QStringLiteral("xhigh")));
    QVERIFY(transport.launchSpec.arguments.contains(QStringLiteral("bypassPermissions")));
    transport.feedOutput(
        R"({"type":"system","subtype":"init","session_id":"session-r","claude_code_version":"2.1.245","cwd":"/workspace","model":"opus","permissionMode":"bypassPermissions"})"
        "\n");
    QCOMPARE(connectionSpy.count(), 1);
    QCOMPARE(transport.writes.size(), 1);
    const QJsonObject sent = QJsonDocument::fromJson(transport.writes.constFirst()).object();
    QCOMPARE(sent.value(QStringLiteral("uuid")).toString(),
             changedTurn.toString(QUuid::WithoutBraces));

    adapter.interruptTurn();
    QCOMPARE(finishedSpy.count(), 1);
    QCOMPARE(finishedSpy.constFirst().at(0).toUuid(), changedTurn);
    QCOMPARE(finishedSpy.constFirst().at(1).toBool(), true);
    QCOMPARE(transport.startCalls, 3);
    QCOMPARE(transport.terminateCalls, 2);
    QVERIFY(transport.launchSpec.arguments.contains(QStringLiteral("--resume")));
    transport.feedOutput(
        R"({"type":"system","subtype":"init","session_id":"session-r","claude_code_version":"2.1.245","cwd":"/workspace","model":"opus","permissionMode":"bypassPermissions"})"
        "\n");
    QCOMPARE(connectionSpy.count(), 1);

    const QUuid nextTurn = QUuid::createUuid();
    adapter.startTurn({nextTurn, QStringLiteral("Continue"), changed, {}});
    QCOMPARE(transport.writes.size(), 2);
    const QString result =
        QStringLiteral(
            R"({"type":"result","subtype":"success","session_id":"session-r","user_message_uuid":"%1","is_error":false,"result":"done"})")
            .arg(nextTurn.toString(QUuid::WithoutBraces));
    transport.feedOutput(result.toUtf8() + '\n');
    QCOMPARE(finishedSpy.count(), 2);
    QCOMPARE(finishedSpy.at(1).at(0).toUuid(), nextTurn);
    QCOMPARE(finishedSpy.at(1).at(2).toBool(), true);

    int interruptedEvents = 0;
    for (const QList<QVariant>& arguments : eventSpy) {
        interruptedEvents += arguments.constFirst().value<domain::AgentEvent>().type ==
                                     domain::AgentEventType::TurnInterrupted
                                 ? 1
                                 : 0;
    }
    QCOMPARE(interruptedEvents, 1);
}

void TestClaudeAdapter::encodesBoundedClaudeImageAttachments() {
    using namespace snack::agent;
    using namespace snack::agent::claude;
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString imagePath = directory.filePath(QStringLiteral("pixel.png"));
    const QByteArray imageBytes("synthetic-png-bytes");
    QFile image(imagePath);
    QVERIFY(image.open(QIODevice::WriteOnly));
    QCOMPARE(image.write(imageBytes), imageBytes.size());
    image.close();

    const CliInstallation installation{.status = CliStatus::Available,
                                       .executablePath = QStringLiteral("claude"),
                                       .version = QStringLiteral("2.1.245")};
    FakeClaudeProcessTransport transport;
    ClaudeAdapter adapter(installation, &transport, nullptr,
                          QStringLiteral(SNACK_CLAUDE_PERMISSION_SERVER));
    QSignalSpy finishedSpy(&adapter, &IAgentAdapter::turnFinished);
    domain::TurnSettingsSnapshot settings;
    settings.agentKind = domain::AgentKind::Claude;
    settings.modelId = QStringLiteral("sonnet");
    settings.reasoningEffort = domain::ReasoningEffort::Medium;
    settings.accessLevel = domain::AccessLevel::Strict;
    settings.workingDirectory = QStringLiteral("/workspace");
    adapter.connectAgent({.workingDirectory = QStringLiteral("/workspace"),
                          .nativeThreadId = QStringLiteral("session-image"),
                          .settings = settings});
    transport.feedOutput(
        R"({"type":"system","subtype":"init","session_id":"session-image","claude_code_version":"2.1.245","cwd":"/workspace","model":"sonnet","permissionMode":"manual"})"
        "\n");

    const QUuid turnId = QUuid::createUuid();
    adapter.startTurn(
        {turnId, QStringLiteral("Inspect"), settings,
         QJsonArray{QJsonObject{{QStringLiteral("kind"), QStringLiteral("image")},
                                {QStringLiteral("path"), imagePath}},
                    QJsonObject{{QStringLiteral("kind"), QStringLiteral("file")},
                                {QStringLiteral("path"), QStringLiteral("/workspace/note.txt")}}}});
    QCOMPARE(transport.writes.size(), 1);
    const QJsonArray content = QJsonDocument::fromJson(transport.writes.constFirst())
                                   .object()
                                   .value(QStringLiteral("message"))
                                   .toObject()
                                   .value(QStringLiteral("content"))
                                   .toArray();
    QCOMPARE(content.size(), 3);
    const QJsonObject source = content.at(1).toObject().value(QStringLiteral("source")).toObject();
    QCOMPARE(source.value(QStringLiteral("type")).toString(), QStringLiteral("base64"));
    QCOMPARE(source.value(QStringLiteral("media_type")).toString(), QStringLiteral("image/png"));
    QCOMPARE(QByteArray::fromBase64(source.value(QStringLiteral("data")).toString().toLatin1()),
             imageBytes);
    QVERIFY(content.at(2)
                .toObject()
                .value(QStringLiteral("text"))
                .toString()
                .contains(QStringLiteral("note.txt")));

    adapter.interruptTurn();
    transport.feedOutput(
        R"({"type":"system","subtype":"init","session_id":"session-image","claude_code_version":"2.1.245","cwd":"/workspace","model":"sonnet","permissionMode":"manual"})"
        "\n");
    const QUuid invalidTurn = QUuid::createUuid();
    const QJsonObject missingAttachment{
        {QStringLiteral("kind"), QStringLiteral("image")},
        {QStringLiteral("path"), directory.filePath(QStringLiteral("missing.png"))}};
    adapter.startTurn(
        {invalidTurn, QStringLiteral("Missing image"), settings, QJsonArray{missingAttachment}});
    QCOMPARE(finishedSpy.count(), 2);
    QCOMPARE(finishedSpy.constLast().at(0).toUuid(), invalidTurn);
    QCOMPARE(finishedSpy.constLast().at(2).toBool(), false);
}

QTEST_MAIN(TestClaudeAdapter)

#include "TestClaudeAdapter.moc"
