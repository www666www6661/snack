#include "agent/codex/CodexAdapter.h"
#include "agent/codex/CodexAppServerClient.h"
#include "agent/codex/CodexCliDiscovery.h"
#include "agent/codex/CodexModelCatalog.h"
#include "agent/codex/CodexProtocol.h"
#include "agent/codex/CodexThreadLifecycle.h"
#include "agent/codex/CodexTurnLifecycle.h"
#include "agent/process/IProcessTransport.h"
#include "agent/process/QProcessTransport.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

class FakeProcessTransport final : public snack::agent::process::IProcessTransport {
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

    void closeWriteChannel() override { ++closeWriteChannelCalls; }

    void terminate() override {
        ++terminateCalls;
        if (running) {
            running = false;
            emit finished(0, snack::agent::process::ExitStatus::Normal);
        }
    }

    void kill() override {
        ++killCalls;
        running = false;
    }

    void feedStandardOutput(const QByteArray& data) { emit standardOutputReceived(data); }
    void feedStandardError(const QByteArray& data) { emit standardErrorReceived(data); }

    void finish(int exitCode, snack::agent::process::ExitStatus status) {
        running = false;
        emit finished(exitCode, status);
    }

    snack::agent::process::LaunchSpec launchSpec;
    QList<QByteArray> writes;
    bool running{false};
    bool failWrites{false};
    int closeWriteChannelCalls{0};
    int terminateCalls{0};
    int killCalls{0};
};

class TestCodexAppServer final : public QObject {
    Q_OBJECT

  private slots:
    void initTestCase();
    void parsesProtocolEnvelopes();
    void discoversCliWithInjectedRunner();
    void probesExecutableWithDefaultRunner();
    void reportsMissingAndUnsupportedCli();
    void buildsPlatformLaunchSpec();
    void loadsVersionedSchemaContract();
    void parsesModelCatalogFixtures();
    void rejectsInvalidModelCatalog();
    void parsesThreadLifecycleResponses();
    void mapsThreadAccessLevels();
    void mapsAndParsesTurnLifecycle();
    void adapterPublishesPaginatedCapabilities();
    void adapterStreamsAndCompletesTurn();
    void adapterHandlesTurnFailuresAndStaleEvents();
    void adapterInterruptsAndDeclinesServerRequests();
    void adapterFinishesTurnWhenProcessDisconnects();
    void adapterHandlesCatalogFailures();
    void streamsQProcessIoAndReportsStartFailure();
    void completesHandshakeFromFragmentedFixture();
    void correlatesResponsesAndPreservesUnknownMessages();
    void rejectsMalformedAndOversizedFrames();
    void boundsDiagnosticsAndReportsEarlyExit();
    void handlesTimeoutCancellationAndWriteFailure();
    void guardsStateAndHandshakeErrors();
    void liveLocalHandshakeWhenEnabled();
};

void TestCodexAppServer::initTestCase() {
    qRegisterMetaType<snack::agent::codex::ConnectionState>();
    qRegisterMetaType<snack::agent::codex::ServerInfo>();
}

void TestCodexAppServer::parsesProtocolEnvelopes() {
    using snack::agent::codex::MessageKind;
    using snack::agent::codex::parseMessage;

    const auto request =
        parseMessage(R"({"method":"approval/respond","id":"server-1","params":{"ok":true}})");
    QCOMPARE(request.kind, MessageKind::Request);
    QCOMPARE(request.id.toString(), QStringLiteral("server-1"));
    QCOMPARE(request.params.toObject().value(QStringLiteral("ok")).toBool(), true);

    const auto response = parseMessage(R"({"id":4,"result":["a","b"]})");
    QCOMPARE(response.kind, MessageKind::Response);
    QCOMPARE(response.result.toArray().size(), 2);

    const auto notification = parseMessage(R"({"method":"future/event","params":{"x":1}})");
    QCOMPARE(notification.kind, MessageKind::Notification);
    QCOMPARE(notification.raw.value(QStringLiteral("method")).toString(),
             QStringLiteral("future/event"));

    QCOMPARE(parseMessage("not-json").kind, MessageKind::Invalid);
    QCOMPARE(parseMessage(R"({"id":1})").kind, MessageKind::Invalid);
    QCOMPARE(parseMessage(R"({"method":1,"id":1,"result":{}})").kind, MessageKind::Invalid);
}

void TestCodexAppServer::streamsQProcessIoAndReportsStartFailure() {
    using namespace snack::agent::process;
    QProcessTransport transport;
    QSignalSpy finishedSpy(&transport, &IProcessTransport::finished);
    QByteArray standardOutput;
    QByteArray standardError;
    connect(&transport, &IProcessTransport::standardOutputReceived, this,
            [&standardOutput](const QByteArray& data) { standardOutput.append(data); });
    connect(&transport, &IProcessTransport::standardErrorReceived, this,
            [&standardError](const QByteArray& data) { standardError.append(data); });

#ifdef Q_OS_WIN
    const LaunchSpec launch{
        .program = QDir(qEnvironmentVariable("SystemRoot", QStringLiteral("C:/Windows")))
                       .filePath(QStringLiteral("System32/more.com"))};
#else
    const LaunchSpec launch{
        .program = QStringLiteral("/bin/sh"),
        .arguments = {QStringLiteral("-c"),
                      QStringLiteral("printf diagnostic\\n >&2; while IFS= read -r line; do "
                                     "printf '%s\\n' \"$line\"; done")}};
#endif
    transport.start(launch);
    QTRY_VERIFY(transport.isRunning());
    const QByteArray input("transport-line\n");
    QCOMPARE(transport.write(input), input.size());
    transport.closeWriteChannel();
    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 2000);
    QVERIFY(standardOutput.contains("transport-line"));
#ifndef Q_OS_WIN
    QVERIFY(standardError.contains("diagnostic"));
#endif
    QCOMPARE(finishedSpy.constFirst().at(1).value<ExitStatus>(), ExitStatus::Normal);

#ifdef Q_OS_WIN
    QProcessTransport diagnosticTransport;
    QSignalSpy diagnosticFinishedSpy(&diagnosticTransport, &IProcessTransport::finished);
    connect(&diagnosticTransport, &IProcessTransport::standardErrorReceived, this,
            [&standardError](const QByteArray& data) { standardError.append(data); });
    diagnosticTransport.start(
        {.program = qEnvironmentVariable("COMSPEC", QStringLiteral("C:/Windows/System32/cmd.exe")),
         .arguments = {QStringLiteral("/d"), QStringLiteral("/s"), QStringLiteral("/c"),
                       QStringLiteral("echo diagnostic 1>&2")}});
    QTRY_COMPARE_WITH_TIMEOUT(diagnosticFinishedSpy.count(), 1, 2000);
    QVERIFY(standardError.contains("diagnostic"));
#endif

    QProcessTransport missingTransport;
    QSignalSpy errorSpy(&missingTransport, &IProcessTransport::errorOccurred);
    missingTransport.start({.program = QStringLiteral("snack-command-that-does-not-exist")});
    QTRY_COMPARE_WITH_TIMEOUT(errorSpy.count(), 1, 2000);
    QCOMPARE(errorSpy.constFirst().constFirst().value<Error>(), Error::FailedToStart);

    QProcessTransport terminatedTransport;
    QSignalSpy terminatedSpy(&terminatedTransport, &IProcessTransport::finished);
#ifdef Q_OS_WIN
    terminatedTransport.start(
        {.program = QDir(qEnvironmentVariable("SystemRoot", QStringLiteral("C:/Windows")))
                        .filePath(QStringLiteral("System32/more.com"))});
#else
    terminatedTransport.start({.program = QStringLiteral("/bin/sh"),
                               .arguments = {QStringLiteral("-c"), QStringLiteral("cat")}});
#endif
    QTRY_VERIFY(terminatedTransport.isRunning());
    terminatedTransport.terminate();
    QTest::qWait(50);
    if (terminatedTransport.isRunning()) {
        terminatedTransport.kill();
    }
    QTRY_COMPARE_WITH_TIMEOUT(terminatedSpy.count(), 1, 2000);

    QProcessTransport killedTransport;
    QSignalSpy killedSpy(&killedTransport, &IProcessTransport::finished);
#ifdef Q_OS_WIN
    killedTransport.start(
        {.program = QDir(qEnvironmentVariable("SystemRoot", QStringLiteral("C:/Windows")))
                        .filePath(QStringLiteral("System32/more.com"))});
#else
    killedTransport.start({.program = QStringLiteral("/bin/sh"),
                           .arguments = {QStringLiteral("-c"), QStringLiteral("cat")}});
#endif
    QTRY_VERIFY(killedTransport.isRunning());
    killedTransport.kill();
    QTRY_COMPARE_WITH_TIMEOUT(killedSpy.count(), 1, 2000);
}

void TestCodexAppServer::discoversCliWithInjectedRunner() {
    using namespace snack::agent::codex;
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString executable = directory.filePath(QStringLiteral("codex.exe"));
    QFile file(executable);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.close();

    int calls = 0;
    const CodexCliDiscovery::CommandRunner runner = [&calls](
                                                        const snack::agent::process::LaunchSpec&,
                                                        int) {
        ++calls;
        if (calls == 1) {
            return CommandResult{
                .started = true, .exitCode = 0, .standardOutput = "codex-cli 0.149.0\n"};
        }
        return CommandResult{
            .started = true,
            .exitCode = 0,
            .standardOutput =
                "Usage: codex app-server [OPTIONS]\n  --listen <URL>\n  generate-json-schema\n"};
    };

    const CliInstallation installation = CodexCliDiscovery::probe(executable, 50, runner);
    QCOMPARE(installation.status, CliStatus::Available);
    QCOMPARE(installation.version, QStringLiteral("0.149.0"));
    QCOMPARE(calls, 2);
    QCOMPARE(CodexCliDiscovery::parseVersion("CODEX-CLI 1.2.3-beta.1"),
             QStringLiteral("1.2.3-beta.1"));
}

void TestCodexAppServer::probesExecutableWithDefaultRunner() {
    using namespace snack::agent::codex;
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
#ifdef Q_OS_WIN
    const QString executable = directory.filePath(QStringLiteral("codex.cmd"));
    const QByteArray script = "@echo off\r\n"
                              "if \"%~1\"==\"--version\" (\r\n"
                              "  echo codex-cli 9.9.9\r\n"
                              "  exit /b 0\r\n"
                              ")\r\n"
                              "if \"%~1\"==\"app-server\" (\r\n"
                              "  echo Usage: codex app-server [OPTIONS]\r\n"
                              "  echo --listen ^<URL^>\r\n"
                              "  exit /b 0\r\n"
                              ")\r\n"
                              "exit /b 2\r\n";
#else
    const QString executable = directory.filePath(QStringLiteral("codex"));
    const QByteArray script = "#!/bin/sh\n"
                              "if [ \"$1\" = \"--version\" ]; then\n"
                              "  echo codex-cli 9.9.9\n"
                              "  exit 0\n"
                              "fi\n"
                              "if [ \"$1\" = \"app-server\" ]; then\n"
                              "  echo 'Usage: codex app-server [OPTIONS]'\n"
                              "  echo '--listen <URL>'\n"
                              "  exit 0\n"
                              "fi\n"
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

    const CliInstallation installation = CodexCliDiscovery::probe(executable, 1000);
    QVERIFY2(installation.isUsable(), qPrintable(installation.detail));
    QCOMPARE(installation.version, QStringLiteral("9.9.9"));
}

void TestCodexAppServer::reportsMissingAndUnsupportedCli() {
    using namespace snack::agent::codex;
    const auto missing = CodexCliDiscovery::probe(QStringLiteral("Z:/missing/codex.exe"));
    QCOMPARE(missing.status, CliStatus::NotFound);
    QVERIFY(!missing.isUsable());

    QTemporaryDir directory;
    const QString executable = directory.filePath(QStringLiteral("codex.exe"));
    QFile file(executable);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.close();

    int calls = 0;
    const auto unsupported = CodexCliDiscovery::probe(
        executable, 50, [&calls](const snack::agent::process::LaunchSpec&, int) {
            ++calls;
            if (calls == 1) {
                return CommandResult{
                    .started = true, .exitCode = 0, .standardOutput = "codex-cli 0.1.0"};
            }
            return CommandResult{
                .started = true, .exitCode = 2, .standardError = "unknown subcommand app-server"};
        });
    QCOMPARE(unsupported.status, CliStatus::UnsupportedAppServer);

    const auto timedOut =
        CodexCliDiscovery::probe(executable, 50, [](const snack::agent::process::LaunchSpec&, int) {
            return CommandResult{.started = true, .timedOut = true};
        });
    QCOMPARE(timedOut.status, CliStatus::ProbeFailed);
}

void TestCodexAppServer::buildsPlatformLaunchSpec() {
    using namespace snack::agent::codex;
    const CliInstallation installation{.status = CliStatus::Available,
#ifdef Q_OS_WIN
                                       .executablePath = QStringLiteral("C:/Tools/codex.cmd"),
#else
                                       .executablePath = QStringLiteral("/usr/local/bin/codex"),
#endif
                                       .version = QStringLiteral("0.149.0")};
    const auto launch =
        CodexCliDiscovery::appServerLaunchSpec(installation, QStringLiteral("/workspace"));
    QCOMPARE(launch.workingDirectory, QStringLiteral("/workspace"));
#ifdef Q_OS_WIN
    QVERIFY(launch.program.endsWith(QStringLiteral("cmd.exe"), Qt::CaseInsensitive));
    QVERIFY(launch.arguments.join(QLatin1Char(' ')).contains(QStringLiteral("codex.cmd")));
    QVERIFY(launch.arguments.contains(QStringLiteral("app-server")));
#else
    QCOMPARE(launch.program, installation.executablePath);
    QCOMPARE(launch.arguments,
             QStringList({QStringLiteral("app-server"), QStringLiteral("--listen"),
                          QStringLiteral("stdio://")}));
#endif
}

void TestCodexAppServer::loadsVersionedSchemaContract() {
    const QString fixtureRoot =
        QStringLiteral(SNACK_TEST_FIXTURE_DIR).append(QStringLiteral("/codex/app-server/0.149.0"));
    QFile manifestFile(fixtureRoot + QStringLiteral("/manifest.json"));
    QVERIFY(manifestFile.open(QIODevice::ReadOnly));
    const QJsonObject manifest = QJsonDocument::fromJson(manifestFile.readAll()).object();
    QCOMPARE(manifest.value(QStringLiteral("cliVersion")).toString(), QStringLiteral("0.149.0"));
    QCOMPARE(manifest.value(QStringLiteral("schemas")).toArray().size(), 20);

    const QStringList schemaNames = {QStringLiteral("JSONRPCMessage.json"),
                                     QStringLiteral("InitializeParams.json"),
                                     QStringLiteral("InitializeResponse.json"),
                                     QStringLiteral("ModelListParams.json"),
                                     QStringLiteral("ModelListResponse.json"),
                                     QStringLiteral("ThreadStartParams.json"),
                                     QStringLiteral("ThreadStartResponse.json"),
                                     QStringLiteral("ThreadResumeParams.json"),
                                     QStringLiteral("ThreadResumeResponse.json"),
                                     QStringLiteral("ThreadStartedNotification.json"),
                                     QStringLiteral("TurnStartParams.json"),
                                     QStringLiteral("TurnStartResponse.json"),
                                     QStringLiteral("TurnStartedNotification.json"),
                                     QStringLiteral("TurnCompletedNotification.json"),
                                     QStringLiteral("ItemStartedNotification.json"),
                                     QStringLiteral("ItemCompletedNotification.json"),
                                     QStringLiteral("AgentMessageDeltaNotification.json"),
                                     QStringLiteral("TurnInterruptParams.json"),
                                     QStringLiteral("TurnInterruptResponse.json"),
                                     QStringLiteral("ErrorNotification.json")};
    for (const QString& schemaName : schemaNames) {
        QFile schemaFile(fixtureRoot + QStringLiteral("/schema/") + schemaName);
        QVERIFY2(schemaFile.open(QIODevice::ReadOnly), qPrintable(schemaFile.errorString()));
        QJsonParseError error;
        const QJsonDocument schema = QJsonDocument::fromJson(schemaFile.readAll(), &error);
        QCOMPARE(error.error, QJsonParseError::NoError);
        QVERIFY(schema.isObject());
        QVERIFY(!schema.object().value(QStringLiteral("title")).toString().isEmpty());
    }
}

static QJsonObject loadObjectFixture(const QString& name) {
    QFile file(QStringLiteral(SNACK_TEST_FIXTURE_DIR)
                   .append(QStringLiteral("/codex/app-server/0.149.0/"))
                   .append(name));
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QJsonDocument::fromJson(file.readAll()).object();
}

static void completeHandshake(FakeProcessTransport& transport) {
    transport.feedStandardOutput(
        R"({"id":1,"result":{"userAgent":"test","platformFamily":"windows","platformOs":"windows"}})"
        "\n");
}

static snack::agent::codex::ProtocolMessage lastRequest(const FakeProcessTransport& transport) {
    return snack::agent::codex::parseMessage(transport.writes.constLast().trimmed());
}

static void feedResult(FakeProcessTransport& transport, qint64 id, const QJsonObject& result) {
    const QJsonObject response{{QStringLiteral("id"), id}, {QStringLiteral("result"), result}};
    transport.feedStandardOutput(QJsonDocument(response).toJson(QJsonDocument::Compact) + '\n');
}

static void feedNotification(FakeProcessTransport& transport, const QString& method,
                             const QJsonObject& params) {
    const QJsonObject notification{{QStringLiteral("method"), method},
                                   {QStringLiteral("params"), params}};
    transport.feedStandardOutput(QJsonDocument(notification).toJson(QJsonDocument::Compact) + '\n');
}

static QJsonObject threadResult(const QString& threadId = QStringLiteral("0198-thread-snack"),
                                const QString& sessionId = QStringLiteral("0198-session-root"),
                                const QString& cwd = QStringLiteral("C:/workspace")) {
    QJsonObject result = loadObjectFixture(QStringLiteral("thread-start-success.json"));
    QJsonObject thread = result.value(QStringLiteral("thread")).toObject();
    thread.insert(QStringLiteral("id"), threadId);
    thread.insert(QStringLiteral("sessionId"), sessionId);
    thread.insert(QStringLiteral("cwd"), cwd);
    result.insert(QStringLiteral("thread"), thread);
    return result;
}

static QJsonObject turnObject(const QString& turnId, const QString& status,
                              const QString& errorMessage = {}) {
    QJsonObject turn{{QStringLiteral("id"), turnId},
                     {QStringLiteral("items"), QJsonArray{}},
                     {QStringLiteral("status"), status}};
    if (!errorMessage.isEmpty()) {
        turn.insert(QStringLiteral("error"),
                    QJsonObject{{QStringLiteral("message"), errorMessage}});
    }
    return turn;
}

static snack::agent::TurnRequest
codexTurnRequest(const QUuid& turnId, const QString& message = QStringLiteral("hello")) {
    return {.turnId = turnId,
            .message = message,
            .settings = {.agentKind = snack::domain::AgentKind::Codex,
                         .modelId = QStringLiteral("gpt-5.2-codex"),
                         .reasoningEffort = snack::domain::ReasoningEffort::High,
                         .accessLevel = snack::domain::AccessLevel::Workspace,
                         .workingDirectory = QStringLiteral("C:/workspace"),
                         .capabilityVersion = QStringLiteral("codex-app-server/0.149.0")}};
}

static void connectAdapter(snack::agent::codex::CodexAdapter& adapter,
                           FakeProcessTransport& transport) {
    adapter.connectAgent({.workingDirectory = QStringLiteral("C:/workspace"),
                          .settings = codexTurnRequest(QUuid::createUuid()).settings});
    QTRY_COMPARE(transport.writes.size(), 1);
    completeHandshake(transport);
    QJsonObject page = loadObjectFixture(QStringLiteral("model-list-page-1.json"));
    page.insert(QStringLiteral("nextCursor"), QJsonValue::Null);
    feedResult(transport, lastRequest(transport).id.toInteger(), page);
    feedResult(transport, lastRequest(transport).id.toInteger(), threadResult());
}

void TestCodexAppServer::parsesModelCatalogFixtures() {
    using namespace snack::agent::codex;
    QString error;
    const auto first =
        parseModelPage(loadObjectFixture(QStringLiteral("model-list-page-1.json")), &error);
    QVERIFY2(first.has_value(), qPrintable(error));
    QCOMPARE(first->models.size(), 2);
    QCOMPARE(first->nextCursor, QStringLiteral("page-2"));
    QVERIFY(first->hasNextPage);
    QCOMPARE(first->models.constFirst().supportedReasoningEfforts.constLast().id,
             QStringLiteral("xhigh"));
    QCOMPARE(reasoningEffortFromCodex(QStringLiteral("xhigh")),
             std::optional(snack::domain::ReasoningEffort::ExtraHigh));

    const auto second =
        parseModelPage(loadObjectFixture(QStringLiteral("model-list-page-2.json")), &error);
    QVERIFY2(second.has_value(), qPrintable(error));
    QVERIFY(!second->hasNextPage);
    QCOMPARE(second->models.constFirst().inputModalities,
             QStringList({QStringLiteral("text"), QStringLiteral("image")}));
    QVERIFY(!reasoningEffortFromCodex(QStringLiteral("future")).has_value());
    QCOMPARE(reasoningEffortFromCodex(QStringLiteral("minimal")),
             std::optional(snack::domain::ReasoningEffort::Minimal));
    QCOMPARE(reasoningEffortFromCodex(QStringLiteral("max")),
             std::optional(snack::domain::ReasoningEffort::Maximum));
    QCOMPARE(reasoningEffortFromCodex(QStringLiteral("ultra")),
             std::optional(snack::domain::ReasoningEffort::Ultra));
}

void TestCodexAppServer::rejectsInvalidModelCatalog() {
    using snack::agent::codex::parseModelPage;
    QString error;
    QVERIFY(!parseModelPage(QJsonArray{}, &error).has_value());
    QVERIFY(error.contains(QStringLiteral("object")));
    QVERIFY(!parseModelPage(QJsonObject{{QStringLiteral("data"), true}}, &error).has_value());
    QVERIFY(
        !parseModelPage(QJsonObject{{QStringLiteral("data"),
                                     QJsonArray{QJsonObject{{QStringLiteral("id"), "broken"}}}}},
                        &error)
             .has_value());
    QVERIFY(error.contains(QStringLiteral("field")));

    QJsonObject invalidModalities = loadObjectFixture(QStringLiteral("model-list-page-1.json"));
    QJsonArray models = invalidModalities.value(QStringLiteral("data")).toArray();
    QJsonObject model = models.at(0).toObject();
    model.insert(QStringLiteral("inputModalities"), QStringLiteral("text"));
    models.replace(0, model);
    invalidModalities.insert(QStringLiteral("data"), models);
    QVERIFY(!parseModelPage(invalidModalities, &error).has_value());

    QJsonObject invalidCursor{{QStringLiteral("data"), QJsonArray{}},
                              {QStringLiteral("nextCursor"), 42}};
    QVERIFY(!parseModelPage(invalidCursor, &error).has_value());

    QJsonObject invalidReasoning = loadObjectFixture(QStringLiteral("model-list-page-1.json"));
    models = invalidReasoning.value(QStringLiteral("data")).toArray();
    model = models.at(0).toObject();
    QJsonArray efforts = model.value(QStringLiteral("supportedReasoningEfforts")).toArray();
    QJsonObject effort = efforts.at(0).toObject();
    effort.remove(QStringLiteral("description"));
    efforts.replace(0, effort);
    model.insert(QStringLiteral("supportedReasoningEfforts"), efforts);
    models.replace(0, model);
    invalidReasoning.insert(QStringLiteral("data"), models);
    QVERIFY(!parseModelPage(invalidReasoning, &error).has_value());
}

void TestCodexAppServer::parsesThreadLifecycleResponses() {
    using snack::agent::codex::parseThreadLifecycleResponse;
    QString error;
    const auto parsed = parseThreadLifecycleResponse(threadResult(), &error);
    QVERIFY2(parsed.has_value(), qPrintable(error));
    QCOMPARE(parsed->id, QStringLiteral("0198-thread-snack"));
    QCOMPARE(parsed->sessionId, QStringLiteral("0198-session-root"));
    QCOMPARE(parsed->workingDirectory, QStringLiteral("C:/workspace"));
    QCOMPARE(parsed->raw.value(QStringLiteral("modelProvider")).toString(),
             QStringLiteral("openai"));

    QVERIFY(!parseThreadLifecycleResponse(QJsonArray{}, &error).has_value());
    QVERIFY(error.contains(QStringLiteral("object")));
    QVERIFY(!parseThreadLifecycleResponse(QJsonObject{}, &error).has_value());
    QJsonObject missingSession = threadResult();
    QJsonObject thread = missingSession.value(QStringLiteral("thread")).toObject();
    thread.remove(QStringLiteral("sessionId"));
    missingSession.insert(QStringLiteral("thread"), thread);
    QVERIFY(!parseThreadLifecycleResponse(missingSession, &error).has_value());
    QVERIFY(error.contains(QStringLiteral("sessionId")));
}

void TestCodexAppServer::mapsThreadAccessLevels() {
    using snack::agent::codex::threadAccessParameters;
    using snack::domain::AccessLevel;
    const auto strict = threadAccessParameters(AccessLevel::Strict);
    QCOMPARE(strict.value(QStringLiteral("approvalPolicy")).toString(),
             QStringLiteral("untrusted"));
    QCOMPARE(strict.value(QStringLiteral("sandbox")).toString(), QStringLiteral("read-only"));

    const auto workspace = threadAccessParameters(AccessLevel::Workspace);
    QCOMPARE(workspace.value(QStringLiteral("approvalPolicy")).toString(),
             QStringLiteral("on-request"));
    QCOMPARE(workspace.value(QStringLiteral("sandbox")).toString(),
             QStringLiteral("workspace-write"));

    const auto full = threadAccessParameters(AccessLevel::Full);
    QCOMPARE(full.value(QStringLiteral("approvalPolicy")).toString(), QStringLiteral("never"));
    QCOMPARE(full.value(QStringLiteral("sandbox")).toString(),
             QStringLiteral("danger-full-access"));
}

void TestCodexAppServer::mapsAndParsesTurnLifecycle() {
    using namespace snack::agent::codex;

    const QUuid guiTurnId = QUuid::createUuid();
    const auto request = codexTurnRequest(guiTurnId, QStringLiteral("stream this"));
    const QJsonObject params = makeTurnStartParameters(QStringLiteral("thread-1"),
                                                       QStringLiteral("C:/workspace"), request);
    QCOMPARE(params.value(QStringLiteral("threadId")).toString(), QStringLiteral("thread-1"));
    QCOMPARE(params.value(QStringLiteral("clientUserMessageId")).toString(),
             guiTurnId.toString(QUuid::WithoutBraces));
    QCOMPARE(params.value(QStringLiteral("model")).toString(), QStringLiteral("gpt-5.2-codex"));
    QCOMPARE(params.value(QStringLiteral("effort")).toString(), QStringLiteral("high"));
    QCOMPARE(params.value(QStringLiteral("approvalPolicy")).toString(),
             QStringLiteral("on-request"));
    QCOMPARE(params.value(QStringLiteral("sandboxPolicy"))
                 .toObject()
                 .value(QStringLiteral("type"))
                 .toString(),
             QStringLiteral("workspaceWrite"));
    QCOMPARE(params.value(QStringLiteral("input"))
                 .toArray()
                 .at(0)
                 .toObject()
                 .value(QStringLiteral("text"))
                 .toString(),
             QStringLiteral("stream this"));
    QCOMPARE(turnAccessParameters(snack::domain::AccessLevel::Strict)
                 .value(QStringLiteral("sandboxPolicy"))
                 .toObject()
                 .value(QStringLiteral("type"))
                 .toString(),
             QStringLiteral("readOnly"));
    QCOMPARE(turnAccessParameters(snack::domain::AccessLevel::Full)
                 .value(QStringLiteral("sandboxPolicy"))
                 .toObject()
                 .value(QStringLiteral("type"))
                 .toString(),
             QStringLiteral("dangerFullAccess"));
    QCOMPARE(makeTurnInterruptParameters(QStringLiteral("thread-1"), QStringLiteral("turn-1"))
                 .value(QStringLiteral("turnId"))
                 .toString(),
             QStringLiteral("turn-1"));

    QString error;
    const auto response = parseTurnStartResponse(
        QJsonObject{{QStringLiteral("turn"),
                     turnObject(QStringLiteral("turn-1"), QStringLiteral("inProgress"))}},
        &error);
    QVERIFY2(response.has_value(), qPrintable(error));
    QCOMPARE(response->id, QStringLiteral("turn-1"));
    const auto completed = parseTurnNotification(
        QJsonObject{
            {QStringLiteral("threadId"), QStringLiteral("thread-1")},
            {QStringLiteral("turn"), turnObject(QStringLiteral("turn-1"), QStringLiteral("failed"),
                                                QStringLiteral("provider failed"))}},
        &error);
    QVERIFY2(completed.has_value(), qPrintable(error));
    QCOMPARE(completed->turn.errorMessage, QStringLiteral("provider failed"));

    const QJsonObject itemParams{
        {QStringLiteral("threadId"), QStringLiteral("thread-1")},
        {QStringLiteral("turnId"), QStringLiteral("turn-1")},
        {QStringLiteral("item"),
         QJsonObject{{QStringLiteral("id"), QStringLiteral("item-1")},
                     {QStringLiteral("type"), QStringLiteral("agentMessage")},
                     {QStringLiteral("text"), QStringLiteral("answer")}}}};
    const auto item = parseItemNotification(itemParams, &error);
    QVERIFY2(item.has_value(), qPrintable(error));
    QCOMPARE(item->text, QStringLiteral("answer"));
    const auto delta =
        parseAgentMessageDelta(QJsonObject{{QStringLiteral("threadId"), QStringLiteral("thread-1")},
                                           {QStringLiteral("turnId"), QStringLiteral("turn-1")},
                                           {QStringLiteral("itemId"), QStringLiteral("item-1")},
                                           {QStringLiteral("delta"), QStringLiteral("part")}},
                               &error);
    QVERIFY2(delta.has_value(), qPrintable(error));
    QCOMPARE(delta->delta, QStringLiteral("part"));
    const auto turnError = parseTurnErrorNotification(
        QJsonObject{{QStringLiteral("threadId"), QStringLiteral("thread-1")},
                    {QStringLiteral("turnId"), QStringLiteral("turn-1")},
                    {QStringLiteral("error"),
                     QJsonObject{{QStringLiteral("message"), QStringLiteral("retry")}}},
                    {QStringLiteral("willRetry"), true}},
        &error);
    QVERIFY2(turnError.has_value(), qPrintable(error));
    QVERIFY(turnError->willRetry);

    QVERIFY(!parseTurnStartResponse(QJsonArray{}, &error).has_value());
    QVERIFY(!parseTurnStartResponse(QJsonObject{}, &error).has_value());
    QVERIFY(!parseTurnStartResponse(
                 QJsonObject{{QStringLiteral("turn"),
                              turnObject(QStringLiteral("turn-1"), QStringLiteral("future"))}},
                 &error)
                 .has_value());
    QVERIFY(!parseTurnNotification(QJsonObject{}, &error).has_value());
    QVERIFY(!parseItemNotification(QJsonObject{}, &error).has_value());
    QVERIFY(!parseAgentMessageDelta(QJsonObject{}, &error).has_value());
    QVERIFY(!parseTurnErrorNotification(QJsonObject{}, &error).has_value());
}

void TestCodexAppServer::adapterPublishesPaginatedCapabilities() {
    using namespace snack::agent::codex;
    FakeProcessTransport transport;
    CodexAdapter adapter({.status = CliStatus::Available,
                          .executablePath = QStringLiteral("codex"),
                          .version = QStringLiteral("0.149.0")},
                         &transport);
    QSignalSpy capabilitySpy(&adapter, &CodexAdapter::capabilitiesChanged);
    QSignalSpy connectionSpy(&adapter, &CodexAdapter::connectionChanged);
    adapter.connectAgent({.workingDirectory = QStringLiteral("C:/workspace"),
                          .settings = {.modelId = QStringLiteral("gpt-5.1-codex-mini"),
                                       .accessLevel = snack::domain::AccessLevel::Workspace}});
    adapter.connectAgent({.workingDirectory = QStringLiteral("C:/ignored")});
    QTRY_COMPARE(transport.writes.size(), 1);
    QCOMPARE(transport.launchSpec.workingDirectory, QStringLiteral("C:/workspace"));

    completeHandshake(transport);
    QCOMPARE(lastRequest(transport).method, QStringLiteral("model/list"));
    QCOMPARE(lastRequest(transport).params.toObject().value(QStringLiteral("limit")).toInt(), 100);
    QCOMPARE(capabilitySpy.count(), 0);
    const qint64 firstId = lastRequest(transport).id.toInteger();
    feedResult(transport, firstId, loadObjectFixture(QStringLiteral("model-list-page-1.json")));
    QCOMPARE(capabilitySpy.count(), 0);
    QCOMPARE(lastRequest(transport).params.toObject().value(QStringLiteral("cursor")).toString(),
             QStringLiteral("page-2"));

    const qint64 secondId = lastRequest(transport).id.toInteger();
    feedResult(transport, secondId, loadObjectFixture(QStringLiteral("model-list-page-2.json")));
    QCOMPARE(capabilitySpy.count(), 1);
    QCOMPARE(connectionSpy.count(), 0);
    QCOMPARE(lastRequest(transport).method, QStringLiteral("thread/start"));
    const QJsonObject startParams = lastRequest(transport).params.toObject();
    QCOMPARE(startParams.value(QStringLiteral("cwd")).toString(), QStringLiteral("C:/workspace"));
    QCOMPARE(startParams.value(QStringLiteral("model")).toString(),
             QStringLiteral("gpt-5.1-codex-mini"));
    QCOMPARE(startParams.value(QStringLiteral("approvalPolicy")).toString(),
             QStringLiteral("on-request"));
    QCOMPARE(startParams.value(QStringLiteral("sandbox")).toString(),
             QStringLiteral("workspace-write"));
    const auto capabilities = adapter.capabilities();
    QCOMPARE(capabilities.models,
             QStringList({QStringLiteral("gpt-5.2-codex"), QStringLiteral("gpt-5.1-codex-mini")}));
    QCOMPARE(capabilities.defaultModelId, QStringLiteral("gpt-5.2-codex"));
    QCOMPARE(capabilities.modelCapabilities.size(), 2);
    QCOMPARE(capabilities.modelCapabilities.constLast().inputModalities,
             QStringList({QStringLiteral("text"), QStringLiteral("image")}));
    QCOMPARE(
        capabilities.reasoningEfforts,
        QList({snack::domain::ReasoningEffort::Minimal, snack::domain::ReasoningEffort::Low,
               snack::domain::ReasoningEffort::Medium, snack::domain::ReasoningEffort::High,
               snack::domain::ReasoningEffort::ExtraHigh, snack::domain::ReasoningEffort::Maximum,
               snack::domain::ReasoningEffort::Ultra}));

    QSignalSpy identitySpy(&adapter, &CodexAdapter::nativeIdentityChanged);
    feedResult(transport, lastRequest(transport).id.toInteger(), threadResult());
    QCOMPARE(identitySpy.count(), 1);
    QCOMPARE(identitySpy.constFirst().at(0).toString(), QStringLiteral("0198-thread-snack"));
    QCOMPARE(identitySpy.constFirst().at(1).toString(), QStringLiteral("0198-session-root"));
    QCOMPARE(connectionSpy.count(), 1);
    QVERIFY(connectionSpy.constFirst().constFirst().toBool());

    adapter.closeAgent();
    QCOMPARE(connectionSpy.count(), 2);
    QVERIFY(!connectionSpy.constLast().constFirst().toBool());

    const qsizetype writesBeforeReconnect = transport.writes.size();
    adapter.connectAgent({.workingDirectory = QStringLiteral("C:/second-workspace")});
    QTRY_COMPARE(transport.writes.size(), writesBeforeReconnect + 1);
    QCOMPARE(transport.launchSpec.workingDirectory, QStringLiteral("C:/second-workspace"));
    completeHandshake(transport);
    QJsonObject singlePage = loadObjectFixture(QStringLiteral("model-list-page-1.json"));
    singlePage.insert(QStringLiteral("nextCursor"), QJsonValue::Null);
    feedResult(transport, lastRequest(transport).id.toInteger(), singlePage);
    QCOMPARE(capabilitySpy.count(), 2);
    QCOMPARE(lastRequest(transport).method, QStringLiteral("thread/resume"));
    QCOMPARE(lastRequest(transport).params.toObject().value(QStringLiteral("threadId")).toString(),
             QStringLiteral("0198-thread-snack"));
    feedResult(transport, lastRequest(transport).id.toInteger(),
               threadResult(QStringLiteral("0198-thread-snack"),
                            QStringLiteral("0198-session-root"),
                            QStringLiteral("C:/second-workspace")));
    QCOMPARE(connectionSpy.count(), 3);
    QVERIFY(connectionSpy.constLast().constFirst().toBool());
    adapter.closeAgent();
}

void TestCodexAppServer::adapterStreamsAndCompletesTurn() {
    using namespace snack::agent::codex;
    using snack::domain::AgentEvent;
    using snack::domain::AgentEventType;

    FakeProcessTransport transport;
    CodexAdapter adapter({.status = CliStatus::Available,
                          .executablePath = QStringLiteral("codex"),
                          .version = QStringLiteral("0.149.0")},
                         &transport);
    connectAdapter(adapter, transport);
    QVERIFY(adapter.capabilities().supportsInterrupt);

    QSignalSpy eventSpy(&adapter, &CodexAdapter::eventReceived);
    QSignalSpy finishedSpy(&adapter, &CodexAdapter::turnFinished);
    const QUuid guiTurnId = QUuid::createUuid();
    adapter.startTurn(codexTurnRequest(guiTurnId, QStringLiteral("Say hello")));
    const auto startRequest = lastRequest(transport);
    QCOMPARE(startRequest.method, QStringLiteral("turn/start"));
    const QJsonObject startParams = startRequest.params.toObject();
    QCOMPARE(startParams.value(QStringLiteral("threadId")).toString(),
             QStringLiteral("0198-thread-snack"));
    QCOMPARE(startParams.value(QStringLiteral("cwd")).toString(), QStringLiteral("C:/workspace"));
    QCOMPARE(startParams.value(QStringLiteral("effort")).toString(), QStringLiteral("high"));

    const QString nativeTurnId = QStringLiteral("turn-stream-1");
    feedResult(transport, startRequest.id.toInteger(),
               QJsonObject{{QStringLiteral("turn"),
                            turnObject(nativeTurnId, QStringLiteral("inProgress"))}});
    feedNotification(
        transport, QStringLiteral("turn/started"),
        {{QStringLiteral("threadId"), QStringLiteral("0198-thread-snack")},
         {QStringLiteral("turn"), turnObject(nativeTurnId, QStringLiteral("inProgress"))}});
    feedNotification(transport, QStringLiteral("item/started"),
                     {{QStringLiteral("threadId"), QStringLiteral("0198-thread-snack")},
                      {QStringLiteral("turnId"), nativeTurnId},
                      {QStringLiteral("startedAtMs"), 1},
                      {QStringLiteral("item"),
                       QJsonObject{{QStringLiteral("id"), QStringLiteral("message-1")},
                                   {QStringLiteral("type"), QStringLiteral("agentMessage")},
                                   {QStringLiteral("text"), QString()}}}});
    for (const QString& text : {QStringLiteral("Hel"), QStringLiteral("lo")}) {
        feedNotification(transport, QStringLiteral("item/agentMessage/delta"),
                         {{QStringLiteral("threadId"), QStringLiteral("0198-thread-snack")},
                          {QStringLiteral("turnId"), nativeTurnId},
                          {QStringLiteral("itemId"), QStringLiteral("message-1")},
                          {QStringLiteral("delta"), text}});
    }
    feedNotification(transport, QStringLiteral("item/completed"),
                     {{QStringLiteral("threadId"), QStringLiteral("0198-thread-snack")},
                      {QStringLiteral("turnId"), nativeTurnId},
                      {QStringLiteral("completedAtMs"), 2},
                      {QStringLiteral("item"),
                       QJsonObject{{QStringLiteral("id"), QStringLiteral("message-1")},
                                   {QStringLiteral("type"), QStringLiteral("agentMessage")},
                                   {QStringLiteral("text"), QStringLiteral("Hello")}}}});
    feedNotification(
        transport, QStringLiteral("turn/completed"),
        {{QStringLiteral("threadId"), QStringLiteral("0198-thread-snack")},
         {QStringLiteral("turn"), turnObject(nativeTurnId, QStringLiteral("completed"))}});

    const QList<AgentEventType> expected = {
        AgentEventType::TurnStarted,          AgentEventType::AgentMessageStart,
        AgentEventType::AgentMessageDelta,    AgentEventType::AgentMessageDelta,
        AgentEventType::AgentMessageComplete, AgentEventType::TurnCompleted};
    QCOMPARE(eventSpy.count(), expected.size());
    for (qsizetype index = 0; index < expected.size(); ++index) {
        const AgentEvent event = eventSpy.at(index).constFirst().value<AgentEvent>();
        QCOMPARE(event.turnId, guiTurnId);
        QCOMPARE(event.type, expected.at(index));
        QVERIFY(!event.rawPayload.isEmpty());
    }
    QCOMPARE(eventSpy.at(2)
                 .constFirst()
                 .value<AgentEvent>()
                 .payload.value(QStringLiteral("text"))
                 .toString(),
             QStringLiteral("Hel"));
    QCOMPARE(finishedSpy.count(), 1);
    QCOMPARE(finishedSpy.constFirst().constFirst().toUuid(), guiTurnId);
    QVERIFY(!finishedSpy.constFirst().at(1).toBool());

    feedNotification(
        transport, QStringLiteral("turn/completed"),
        {{QStringLiteral("threadId"), QStringLiteral("0198-thread-snack")},
         {QStringLiteral("turn"), turnObject(nativeTurnId, QStringLiteral("completed"))}});
    QCOMPARE(eventSpy.count(), expected.size());
    QCOMPARE(finishedSpy.count(), 1);
    adapter.closeAgent();
}

void TestCodexAppServer::adapterHandlesTurnFailuresAndStaleEvents() {
    using namespace snack::agent::codex;
    using snack::domain::AgentEvent;
    using snack::domain::AgentEventType;

    FakeProcessTransport transport;
    CodexAdapter adapter({.status = CliStatus::Available,
                          .executablePath = QStringLiteral("codex"),
                          .version = QStringLiteral("0.149.0")},
                         &transport);
    connectAdapter(adapter, transport);
    QSignalSpy eventSpy(&adapter, &CodexAdapter::eventReceived);
    QSignalSpy finishedSpy(&adapter, &CodexAdapter::turnFinished);

    const QUuid failedRequestId = QUuid::createUuid();
    adapter.startTurn(codexTurnRequest(failedRequestId));
    const qint64 requestId = lastRequest(transport).id.toInteger();
    transport.feedStandardOutput(
        (QStringLiteral(R"({"id":%1,"error":{"code":429,"message":"rate limited"}})")
             .arg(requestId) +
         QLatin1Char('\n'))
            .toUtf8());
    QCOMPARE(eventSpy.count(), 1);
    QCOMPARE(eventSpy.constFirst().constFirst().value<AgentEvent>().type,
             AgentEventType::TurnFailed);
    QCOMPARE(finishedSpy.count(), 1);

    eventSpy.clear();
    finishedSpy.clear();
    const QUuid retryTurnId = QUuid::createUuid();
    adapter.startTurn(codexTurnRequest(retryTurnId));
    const auto retryRequest = lastRequest(transport);
    const QString nativeTurnId = QStringLiteral("turn-retry-1");
    feedResult(transport, retryRequest.id.toInteger(),
               QJsonObject{{QStringLiteral("turn"),
                            turnObject(nativeTurnId, QStringLiteral("inProgress"))}});
    feedNotification(
        transport, QStringLiteral("turn/started"),
        {{QStringLiteral("threadId"), QStringLiteral("wrong-thread")},
         {QStringLiteral("turn"), turnObject(nativeTurnId, QStringLiteral("inProgress"))}});
    feedNotification(transport, QStringLiteral("turn/started"),
                     {{QStringLiteral("threadId"), QStringLiteral("0198-thread-snack")},
                      {QStringLiteral("turn"),
                       turnObject(QStringLiteral("wrong-turn"), QStringLiteral("inProgress"))}});
    feedNotification(transport, QStringLiteral("error"),
                     {{QStringLiteral("threadId"), QStringLiteral("0198-thread-snack")},
                      {QStringLiteral("turnId"), nativeTurnId},
                      {QStringLiteral("error"),
                       QJsonObject{{QStringLiteral("message"), QStringLiteral("retrying")}}},
                      {QStringLiteral("willRetry"), true}});
    QCOMPARE(eventSpy.count(), 3);
    for (const auto& arguments : eventSpy) {
        QCOMPARE(arguments.constFirst().value<AgentEvent>().type, AgentEventType::WarningRaised);
    }
    feedNotification(transport, QStringLiteral("error"),
                     {{QStringLiteral("threadId"), QStringLiteral("0198-thread-snack")},
                      {QStringLiteral("turnId"), nativeTurnId},
                      {QStringLiteral("error"), QJsonObject{{QStringLiteral("message"),
                                                             QStringLiteral("provider stopped")}}},
                      {QStringLiteral("willRetry"), false}});
    QCOMPARE(eventSpy.count(), 4);
    QCOMPARE(eventSpy.constLast().constFirst().value<AgentEvent>().type,
             AgentEventType::TurnFailed);
    QCOMPARE(finishedSpy.count(), 1);
    QCOMPARE(finishedSpy.constFirst().constFirst().toUuid(), retryTurnId);
    adapter.closeAgent();
}

void TestCodexAppServer::adapterInterruptsAndDeclinesServerRequests() {
    using namespace snack::agent::codex;
    using snack::domain::AgentEvent;
    using snack::domain::AgentEventType;

    FakeProcessTransport transport;
    CodexAdapter adapter({.status = CliStatus::Available,
                          .executablePath = QStringLiteral("codex"),
                          .version = QStringLiteral("0.149.0")},
                         &transport);
    connectAdapter(adapter, transport);
    QSignalSpy eventSpy(&adapter, &CodexAdapter::eventReceived);
    QSignalSpy finishedSpy(&adapter, &CodexAdapter::turnFinished);

    const QUuid guiTurnId = QUuid::createUuid();
    adapter.startTurn(codexTurnRequest(guiTurnId));
    const auto startRequest = lastRequest(transport);
    const qsizetype writesBeforeInterrupt = transport.writes.size();
    adapter.interruptTurn();
    adapter.interruptTurn();
    QCOMPARE(transport.writes.size(), writesBeforeInterrupt);

    const QString nativeTurnId = QStringLiteral("turn-interrupt-1");
    feedResult(transport, startRequest.id.toInteger(),
               QJsonObject{{QStringLiteral("turn"),
                            turnObject(nativeTurnId, QStringLiteral("inProgress"))}});
    const auto interruptRequest = lastRequest(transport);
    QCOMPARE(interruptRequest.method, QStringLiteral("turn/interrupt"));
    QCOMPARE(interruptRequest.params.toObject().value(QStringLiteral("turnId")).toString(),
             nativeTurnId);
    const qsizetype writesWithInterrupt = transport.writes.size();
    adapter.interruptTurn();
    QCOMPARE(transport.writes.size(), writesWithInterrupt);

    transport.feedStandardOutput(
        R"({"method":"approval/request","id":"approval-1","params":{"kind":"command"}})"
        "\n");
    const auto unsupportedResponse = parseMessage(transport.writes.constLast().trimmed());
    QCOMPARE(unsupportedResponse.kind, MessageKind::Response);
    QCOMPARE(unsupportedResponse.id.toString(), QStringLiteral("approval-1"));
    QCOMPARE(unsupportedResponse.error.value(QStringLiteral("code")).toInt(), -32601);
    QCOMPARE(eventSpy.count(), 1);
    QCOMPARE(eventSpy.constFirst().constFirst().value<AgentEvent>().type,
             AgentEventType::WarningRaised);

    feedResult(transport, interruptRequest.id.toInteger(), {});
    QCOMPARE(finishedSpy.count(), 0);
    feedNotification(
        transport, QStringLiteral("turn/started"),
        {{QStringLiteral("threadId"), QStringLiteral("0198-thread-snack")},
         {QStringLiteral("turn"), turnObject(nativeTurnId, QStringLiteral("inProgress"))}});
    QCOMPARE(transport.writes.size(), writesWithInterrupt + 1);
    feedNotification(
        transport, QStringLiteral("turn/completed"),
        {{QStringLiteral("threadId"), QStringLiteral("0198-thread-snack")},
         {QStringLiteral("turn"), turnObject(nativeTurnId, QStringLiteral("interrupted"))}});
    QCOMPARE(finishedSpy.count(), 1);
    QVERIFY(finishedSpy.constFirst().at(1).toBool());
    QCOMPARE(eventSpy.constLast().constFirst().value<AgentEvent>().type,
             AgentEventType::TurnInterrupted);
    const qsizetype writesAfterFinish = transport.writes.size();
    adapter.interruptTurn();
    QCOMPARE(transport.writes.size(), writesAfterFinish);
    adapter.closeAgent();
}

void TestCodexAppServer::adapterFinishesTurnWhenProcessDisconnects() {
    using namespace snack::agent::codex;
    using snack::domain::AgentEvent;
    using snack::domain::AgentEventType;

    FakeProcessTransport transport;
    CodexAdapter adapter({.status = CliStatus::Available,
                          .executablePath = QStringLiteral("codex"),
                          .version = QStringLiteral("0.149.0")},
                         &transport);
    QSignalSpy connectionSpy(&adapter, &CodexAdapter::connectionChanged);
    connectAdapter(adapter, transport);
    QSignalSpy eventSpy(&adapter, &CodexAdapter::eventReceived);
    QSignalSpy finishedSpy(&adapter, &CodexAdapter::turnFinished);

    const QUuid guiTurnId = QUuid::createUuid();
    adapter.startTurn(codexTurnRequest(guiTurnId));
    feedResult(transport, lastRequest(transport).id.toInteger(),
               QJsonObject{{QStringLiteral("turn"), turnObject(QStringLiteral("turn-disconnect-1"),
                                                               QStringLiteral("inProgress"))}});
    transport.finish(9, snack::agent::process::ExitStatus::Crashed);
    QCOMPARE(eventSpy.count(), 1);
    QCOMPARE(eventSpy.constFirst().constFirst().value<AgentEvent>().type,
             AgentEventType::TurnFailed);
    QCOMPARE(finishedSpy.count(), 1);
    QCOMPARE(finishedSpy.constFirst().constFirst().toUuid(), guiTurnId);
    QCOMPARE(connectionSpy.count(), 2);
    QVERIFY(!connectionSpy.constLast().constFirst().toBool());
}

void TestCodexAppServer::adapterHandlesCatalogFailures() {
    using namespace snack::agent::codex;
    FakeProcessTransport unavailableTransport;
    CodexAdapter unavailable(
        {.status = CliStatus::NotFound, .detail = QStringLiteral("Codex was not found")},
        &unavailableTransport);
    QSignalSpy unavailableSpy(&unavailable, &CodexAdapter::connectionChanged);
    unavailable.connectAgent({.workingDirectory = QStringLiteral("/workspace")});
    QTRY_COMPARE(unavailableSpy.count(), 1);
    QVERIFY(!unavailableSpy.constFirst().constFirst().toBool());
    QCOMPARE(unavailableTransport.writes.size(), 0);

    FakeProcessTransport missingCwdTransport;
    CodexAdapter missingCwd(
        {.status = CliStatus::Available, .executablePath = QStringLiteral("codex")},
        &missingCwdTransport);
    QSignalSpy missingCwdSpy(&missingCwd, &CodexAdapter::connectionChanged);
    missingCwd.connectAgent({});
    QTRY_COMPARE(missingCwdSpy.count(), 1);
    QVERIFY(missingCwdSpy.constFirst().at(1).toString().contains(QStringLiteral("directory")));
    QCOMPARE(missingCwdTransport.writes.size(), 0);

    FakeProcessTransport errorTransport;
    CodexAdapter requestError(
        {.status = CliStatus::Available, .executablePath = QStringLiteral("codex")},
        &errorTransport);
    QSignalSpy requestErrorSpy(&requestError, &CodexAdapter::connectionChanged);
    requestError.connectAgent({.workingDirectory = QStringLiteral("/workspace")});
    QTRY_COMPARE(errorTransport.writes.size(), 1);
    completeHandshake(errorTransport);
    const qint64 requestId = lastRequest(errorTransport).id.toInteger();
    errorTransport.feedStandardOutput(
        (QStringLiteral(R"({"id":%1,"error":{"code":-32001,"message":"catalog denied"}})")
             .arg(requestId) +
         QLatin1Char('\n'))
            .toUtf8());
    QCOMPARE(requestErrorSpy.count(), 1);
    QVERIFY(requestErrorSpy.constFirst().at(1).toString().contains(QStringLiteral("denied")));

    FakeProcessTransport emptyTransport;
    CodexAdapter empty({.status = CliStatus::Available, .executablePath = QStringLiteral("codex")},
                       &emptyTransport);
    QSignalSpy emptySpy(&empty, &CodexAdapter::connectionChanged);
    empty.connectAgent({.workingDirectory = QStringLiteral("/workspace")});
    QTRY_COMPARE(emptyTransport.writes.size(), 1);
    completeHandshake(emptyTransport);
    feedResult(emptyTransport, lastRequest(emptyTransport).id.toInteger(),
               QJsonObject{{QStringLiteral("data"), QJsonArray{}},
                           {QStringLiteral("nextCursor"), QJsonValue::Null}});
    QCOMPARE(emptySpy.count(), 1);
    QVERIFY(emptySpy.constFirst().at(1).toString().contains(QStringLiteral("no visible")));

    FakeProcessTransport cursorTransport;
    CodexAdapter repeatedCursor(
        {.status = CliStatus::Available, .executablePath = QStringLiteral("codex")},
        &cursorTransport);
    QSignalSpy cursorSpy(&repeatedCursor, &CodexAdapter::connectionChanged);
    repeatedCursor.connectAgent({.workingDirectory = QStringLiteral("/workspace")});
    QTRY_COMPARE(cursorTransport.writes.size(), 1);
    completeHandshake(cursorTransport);
    const QJsonObject firstPage = loadObjectFixture(QStringLiteral("model-list-page-1.json"));
    feedResult(cursorTransport, lastRequest(cursorTransport).id.toInteger(), firstPage);
    feedResult(cursorTransport, lastRequest(cursorTransport).id.toInteger(), firstPage);
    QCOMPARE(cursorSpy.count(), 1);
    QVERIFY(cursorSpy.constFirst().at(1).toString().contains(QStringLiteral("repeated cursor")));

    FakeProcessTransport threadTransport;
    CodexAdapter threadError(
        {.status = CliStatus::Available, .executablePath = QStringLiteral("codex")},
        &threadTransport);
    QSignalSpy threadErrorSpy(&threadError, &CodexAdapter::connectionChanged);
    threadError.connectAgent({.workingDirectory = QStringLiteral("/workspace")});
    QTRY_COMPARE(threadTransport.writes.size(), 1);
    completeHandshake(threadTransport);
    QJsonObject singlePage = firstPage;
    singlePage.insert(QStringLiteral("nextCursor"), QJsonValue::Null);
    feedResult(threadTransport, lastRequest(threadTransport).id.toInteger(), singlePage);
    QCOMPARE(lastRequest(threadTransport).method, QStringLiteral("thread/start"));
    const qint64 threadRequestId = lastRequest(threadTransport).id.toInteger();
    threadTransport.feedStandardOutput(
        (QStringLiteral(R"({"id":%1,"error":{"code":-32002,"message":"thread denied"}})")
             .arg(threadRequestId) +
         QLatin1Char('\n'))
            .toUtf8());
    QCOMPARE(threadErrorSpy.count(), 1);
    QVERIFY(threadErrorSpy.constFirst().at(1).toString().contains(QStringLiteral("thread denied")));

    FakeProcessTransport mismatchTransport;
    CodexAdapter mismatch(
        {.status = CliStatus::Available, .executablePath = QStringLiteral("codex")},
        &mismatchTransport);
    QSignalSpy mismatchSpy(&mismatch, &CodexAdapter::connectionChanged);
    mismatch.connectAgent({.workingDirectory = QStringLiteral("/workspace"),
                           .nativeThreadId = QStringLiteral("expected-thread")});
    QTRY_COMPARE(mismatchTransport.writes.size(), 1);
    completeHandshake(mismatchTransport);
    feedResult(mismatchTransport, lastRequest(mismatchTransport).id.toInteger(), singlePage);
    QCOMPARE(lastRequest(mismatchTransport).method, QStringLiteral("thread/resume"));
    feedResult(mismatchTransport, lastRequest(mismatchTransport).id.toInteger(),
               threadResult(QStringLiteral("different-thread")));
    QCOMPARE(mismatchSpy.count(), 1);
    QVERIFY(mismatchSpy.constFirst().at(1).toString().contains(QStringLiteral("unexpected")));

    FakeProcessTransport cwdTransport;
    CodexAdapter cwdMismatch(
        {.status = CliStatus::Available, .executablePath = QStringLiteral("codex")}, &cwdTransport);
    QSignalSpy cwdSpy(&cwdMismatch, &CodexAdapter::connectionChanged);
    cwdMismatch.connectAgent({.workingDirectory = QStringLiteral("/expected"),
                              .nativeThreadId = QStringLiteral("expected-thread")});
    QTRY_COMPARE(cwdTransport.writes.size(), 1);
    completeHandshake(cwdTransport);
    feedResult(cwdTransport, lastRequest(cwdTransport).id.toInteger(), singlePage);
    feedResult(cwdTransport, lastRequest(cwdTransport).id.toInteger(),
               threadResult(QStringLiteral("expected-thread"), QStringLiteral("session-root"),
                            QStringLiteral("/different")));
    QCOMPARE(cwdSpy.count(), 1);
    QVERIFY(cwdSpy.constFirst().at(1).toString().contains(QStringLiteral("directory")));
}

void TestCodexAppServer::completesHandshakeFromFragmentedFixture() {
    using namespace snack::agent::codex;
    FakeProcessTransport transport;
    CodexAppServerClient client(&transport);
    QSignalSpy handshakeSpy(&client, &CodexAppServerClient::handshakeCompleted);
    QSignalSpy notificationSpy(&client, &CodexAppServerClient::notificationReceived);

    client.start({.program = QStringLiteral("codex"), .arguments = {QStringLiteral("app-server")}});
    QCOMPARE(client.state(), ConnectionState::Initializing);
    QCOMPARE(transport.writes.size(), 1);
    const auto initialize = parseMessage(transport.writes.constFirst().trimmed());
    QCOMPARE(initialize.kind, MessageKind::Request);
    QCOMPARE(initialize.method, QStringLiteral("initialize"));
    QCOMPARE(initialize.params.toObject()
                 .value(QStringLiteral("clientInfo"))
                 .toObject()
                 .value(QStringLiteral("name"))
                 .toString(),
             QStringLiteral("snack"));

    QFile fixture(
        QStringLiteral(SNACK_TEST_FIXTURE_DIR)
            .append(QStringLiteral("/codex/app-server/0.149.0/initialize-success.jsonl")));
    QVERIFY2(fixture.open(QIODevice::ReadOnly), qPrintable(fixture.errorString()));
    const QByteArray data = fixture.readAll();
    const qsizetype split = data.size() / 2;
    transport.feedStandardOutput(data.first(split));
    transport.feedStandardOutput(data.sliced(split));

    QCOMPARE(client.state(), ConnectionState::Ready);
    QCOMPARE(handshakeSpy.count(), 1);
    QCOMPARE(notificationSpy.count(), 1);
    QCOMPARE(client.serverInfo().platformFamily, QStringLiteral("windows"));
    QCOMPARE(transport.writes.size(), 2);
    const auto initialized = parseMessage(transport.writes.constLast().trimmed());
    QCOMPARE(initialized.kind, MessageKind::Notification);
    QCOMPARE(initialized.method, QStringLiteral("initialized"));
}

void TestCodexAppServer::correlatesResponsesAndPreservesUnknownMessages() {
    using namespace snack::agent::codex;
    FakeProcessTransport transport;
    CodexAppServerClient client(&transport);
    QSignalSpy responseSpy(&client, &CodexAppServerClient::responseReceived);
    QSignalSpy failedRequestSpy(&client, &CodexAppServerClient::requestFailed);
    QSignalSpy warningSpy(&client, &CodexAppServerClient::protocolWarning);
    QSignalSpy requestSpy(&client, &CodexAppServerClient::serverRequestReceived);

    client.start({.program = QStringLiteral("codex")});
    transport.feedStandardOutput(
        R"({"id":1,"result":{"userAgent":"test","platformFamily":"linux","platformOs":"linux"}})"
        "\n");
    const qint64 modelRequest = client.sendRequest(QStringLiteral("model/list"));
    QVERIFY(modelRequest > 1);
    transport.feedStandardOutput(R"({"id":999,"result":{}})"
                                 "\n");
    QCOMPARE(warningSpy.count(), 1);
    transport.feedStandardOutput(R"({"id":9223372036854775808,"result":{}})"
                                 "\n");
    QCOMPARE(warningSpy.count(), 2);
    transport.feedStandardOutput(
        (QStringLiteral(R"({"id":%1,"result":{"data":[{"id":"model"}]}})").arg(modelRequest) +
         QLatin1Char('\n'))
            .toUtf8());
    QCOMPARE(responseSpy.count(), 1);
    QCOMPARE(responseSpy.at(0).at(1).toString(), QStringLiteral("model/list"));

    const qint64 failingRequest = client.sendRequest(QStringLiteral("thread/read"));
    transport.feedStandardOutput(
        (QStringLiteral(R"({"id":%1,"error":{"code":404,"message":"missing"}})")
             .arg(failingRequest) +
         QLatin1Char('\n'))
            .toUtf8());
    QCOMPARE(failedRequestSpy.count(), 1);
    QCOMPARE(failedRequestSpy.at(0).at(2).toInt(), 404);

    transport.feedStandardOutput(
        R"({"method":"approval/request","id":"approval-1","params":{"kind":"command"},"future":true})"
        "\n");
    QCOMPARE(requestSpy.count(), 1);
    QCOMPARE(requestSpy.at(0).at(3).toJsonObject().value(QStringLiteral("future")).toBool(), true);
}

void TestCodexAppServer::rejectsMalformedAndOversizedFrames() {
    using namespace snack::agent::codex;
    FakeProcessTransport malformedTransport;
    CodexAppServerClient malformedClient(&malformedTransport);
    QSignalSpy malformedFailure(&malformedClient, &CodexAppServerClient::failureOccurred);
    malformedClient.start({.program = QStringLiteral("codex")});
    malformedTransport.feedStandardOutput("{broken}\n");
    QCOMPARE(malformedClient.state(), ConnectionState::Failed);
    QCOMPARE(malformedFailure.count(), 1);

    FakeProcessTransport oversizedTransport;
    CodexAppServerClient oversizedClient(&oversizedTransport, nullptr, 24, 16);
    oversizedClient.start({.program = QStringLiteral("codex")});
    oversizedTransport.feedStandardOutput(QByteArray(25, 'x'));
    QCOMPARE(oversizedClient.state(), ConnectionState::Failed);
}

void TestCodexAppServer::boundsDiagnosticsAndReportsEarlyExit() {
    using namespace snack::agent::codex;
    FakeProcessTransport transport;
    CodexAppServerClient client(&transport, nullptr, 1024, 8);
    QSignalSpy diagnosticSpy(&client, &CodexAppServerClient::diagnosticReceived);
    QSignalSpy failureSpy(&client, &CodexAppServerClient::failureOccurred);
    client.start({.program = QStringLiteral("codex")});
    transport.feedStandardError("0123456789");
    QCOMPARE(client.diagnostics(), QByteArray("23456789"));
    QCOMPARE(diagnosticSpy.count(), 1);
    transport.finish(2, snack::agent::process::ExitStatus::Normal);
    QCOMPARE(client.state(), ConnectionState::Failed);
    QCOMPARE(failureSpy.count(), 1);
    QVERIFY(failureSpy.constFirst().constFirst().toString().contains(QStringLiteral("code 2")));
}

void TestCodexAppServer::handlesTimeoutCancellationAndWriteFailure() {
    using namespace snack::agent::codex;
    FakeProcessTransport timeoutTransport;
    CodexAppServerClient timeoutClient(&timeoutTransport);
    timeoutClient.start({.program = QStringLiteral("codex")}, {}, 1);
    QTRY_COMPARE(timeoutClient.state(), ConnectionState::Failed);
    QCOMPARE(timeoutTransport.terminateCalls, 1);

    FakeProcessTransport cancelledTransport;
    CodexAppServerClient cancelledClient(&cancelledTransport);
    cancelledClient.start({.program = QStringLiteral("codex")});
    cancelledClient.stop();
    QCOMPARE(cancelledClient.state(), ConnectionState::Stopped);
    QCOMPARE(cancelledTransport.closeWriteChannelCalls, 1);

    FakeProcessTransport writeTransport;
    writeTransport.failWrites = true;
    CodexAppServerClient writeClient(&writeTransport);
    writeClient.start({.program = QStringLiteral("codex")});
    QCOMPARE(writeClient.state(), ConnectionState::Failed);
    QCOMPARE(writeTransport.terminateCalls, 1);
}

void TestCodexAppServer::guardsStateAndHandshakeErrors() {
    using namespace snack::agent::codex;
    FakeProcessTransport guardedTransport;
    CodexAppServerClient guardedClient(&guardedTransport);
    QSignalSpy warningSpy(&guardedClient, &CodexAppServerClient::protocolWarning);
    QCOMPARE(guardedClient.sendRequest(QStringLiteral("too-early")), 0);
    guardedClient.sendNotification(QStringLiteral("too-early"));
    guardedClient.stop();
    QCOMPARE(guardedTransport.closeWriteChannelCalls, 0);
    guardedClient.start({.program = QStringLiteral("codex")});
    guardedClient.start({.program = QStringLiteral("codex")});
    QCOMPARE(warningSpy.count(), 3);
    guardedTransport.feedStandardOutput(
        R"({"id":1,"error":{"code":-32000,"message":"not initialized"}})"
        "\n");
    QCOMPARE(guardedClient.state(), ConnectionState::Failed);
    guardedTransport.running = true;
    guardedClient.start({.program = QStringLiteral("codex")});
    QCOMPARE(warningSpy.count(), 4);
    guardedTransport.running = false;

    FakeProcessTransport invalidResultTransport;
    CodexAppServerClient invalidResultClient(&invalidResultTransport);
    invalidResultClient.start({.program = QStringLiteral("codex")});
    invalidResultTransport.feedStandardOutput(R"({"id":1,"result":[]})"
                                              "\n");
    QCOMPARE(invalidResultClient.state(), ConnectionState::Failed);

    FakeProcessTransport readyTransport;
    CodexAppServerClient readyClient(&readyTransport);
    readyClient.start({.program = QStringLiteral("codex")});
    readyTransport.feedStandardOutput(
        "\n\r\n"
        R"({"id":1,"result":{"userAgent":"test","codexHome":"/tmp","platformFamily":"unix","platformOs":"linux"}})"
        "\r\n");
    QCOMPARE(readyClient.state(), ConnectionState::Ready);
    const qsizetype writesBeforeNotification = readyTransport.writes.size();
    readyClient.sendNotification(QStringLiteral("client/ping"), {{QStringLiteral("value"), true}});
    QCOMPARE(readyTransport.writes.size(), writesBeforeNotification + 1);
    QCOMPARE(parseMessage(readyTransport.writes.constLast().trimmed()).method,
             QStringLiteral("client/ping"));
}

void TestCodexAppServer::liveLocalHandshakeWhenEnabled() {
    using namespace snack::agent::codex;
    if (qEnvironmentVariable("SNACK_RUN_LIVE_CODEX_TEST") != QStringLiteral("1")) {
        QSKIP("Set SNACK_RUN_LIVE_CODEX_TEST=1 to probe the installed Codex CLI");
    }

    const CliInstallation installation = CodexCliDiscovery::probe();
    QVERIFY2(installation.isUsable(), qPrintable(installation.detail));
    snack::agent::process::QProcessTransport transport;
    CodexAppServerClient client(&transport);
    QSignalSpy handshakeSpy(&client, &CodexAppServerClient::handshakeCompleted);
    QSignalSpy responseSpy(&client, &CodexAppServerClient::responseReceived);
    client.start(CodexCliDiscovery::appServerLaunchSpec(installation, QDir::currentPath()));
    QTRY_COMPARE_WITH_TIMEOUT(client.state(), ConnectionState::Ready, 5000);
    QCOMPARE(handshakeSpy.count(), 1);
    const qint64 requestId = client.sendRequest(
        QStringLiteral("model/list"),
        {{QStringLiteral("limit"), 20}, {QStringLiteral("includeHidden"), false}});
    QVERIFY(requestId > 0);
    QTRY_COMPARE_WITH_TIMEOUT(responseSpy.count(), 1, 5000);
    QString parseError;
    const auto page = parseModelPage(responseSpy.constFirst().at(2).toJsonValue(), &parseError);
    QVERIFY2(page.has_value(), qPrintable(parseError));
    QVERIFY(!page->models.isEmpty());
    responseSpy.clear();
    const qint64 threadRequestId =
        client.sendRequest(QStringLiteral("thread/start"),
                           {{QStringLiteral("cwd"), QDir::currentPath()},
                            {QStringLiteral("model"), page->models.constFirst().id},
                            {QStringLiteral("approvalPolicy"), QStringLiteral("untrusted")},
                            {QStringLiteral("sandbox"), QStringLiteral("read-only")},
                            {QStringLiteral("ephemeral"), true}});
    QVERIFY(threadRequestId > requestId);
    QTRY_COMPARE_WITH_TIMEOUT(responseSpy.count(), 1, 5000);
    const auto thread =
        parseThreadLifecycleResponse(responseSpy.constFirst().at(2).toJsonValue(), &parseError);
    QVERIFY2(thread.has_value(), qPrintable(parseError));
    QVERIFY(!thread->id.isEmpty());
    QVERIFY(!thread->sessionId.isEmpty());
    client.stop();
    QTRY_COMPARE_WITH_TIMEOUT(client.state(), ConnectionState::Stopped, 3000);
}

QTEST_GUILESS_MAIN(TestCodexAppServer)
#include "TestCodexAppServer.moc"
