#include "ClaudeControlContract.h"
#include "ClaudeStreamContract.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSet>
#include <QTemporaryDir>
#include <QtTest>

class TestClaudeProtocolSpike : public QObject {
    Q_OBJECT

  private slots:
    void recordsSanitizedNoModelEvidence();
    void coversRequiredCliOptionsAndNegativeControl();
    void recordsOfficialCapabilityVersionGates();
    void parsesInitAfterStartupEvents();
    void separatesLongLivedStreamTurns();
    void tracksQueuedImageTurns();
    void rejectsMalformedAndCrossSessionRecords();
    void reconcilesInterruptReceiptsByCapability();
    void rejectsAmbiguousInterruptReceipts();
    void servesMinimalPermissionMcpContract();
    void recordsLocalPermissionBridgeHandshake();
    void freezesPureCppRuntimeControlDegradation();
    void recordsStrictStartupControlValidation();
    void gatesMinimumClaudeVersion_data();
    void gatesMinimumClaudeVersion();
};

namespace {

QJsonObject loadManifest() {
    QFile file(QStringLiteral(SNACK_TEST_FIXTURE_DIR)
                   .append(QStringLiteral("/claude/2.1.245/manifest.json")));
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QJsonDocument::fromJson(file.readAll()).object();
}

QJsonArray loadArrayFixture(const QString& name) {
    QFile file(QStringLiteral(SNACK_TEST_FIXTURE_DIR)
                   .append(QStringLiteral("/claude/2.1.245/"))
                   .append(name));
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QJsonDocument::fromJson(file.readAll()).array();
}

QSet<QString> stringSet(const QJsonArray& values) {
    QSet<QString> result;
    for (const QJsonValue& value : values) {
        result.insert(value.toString());
    }
    return result;
}

QList<snack::spike::claude::StreamRecord> loadJsonLines(const QString& name) {
    QFile file(QStringLiteral(SNACK_TEST_FIXTURE_DIR)
                   .append(QStringLiteral("/claude/2.1.245/"))
                   .append(name));
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    QList<snack::spike::claude::StreamRecord> records;
    for (const QByteArray& line : file.readAll().split('\n')) {
        if (!line.trimmed().isEmpty()) {
            records.append(snack::spike::claude::parseStreamRecord(line));
        }
    }
    return records;
}

} // namespace

void TestClaudeProtocolSpike::recordsSanitizedNoModelEvidence() {
    const QJsonObject manifest = loadManifest();
    QVERIFY(!manifest.isEmpty());
    QCOMPARE(manifest.value(QStringLiteral("schemaVersion")).toInt(), 1);
    QCOMPARE(manifest.value(QStringLiteral("cliVersion")).toString(), QStringLiteral("2.1.245"));
    QCOMPARE(manifest.value(QStringLiteral("evidenceType")).toString(),
             QStringLiteral("local-no-model-probe"));
    QVERIFY(manifest.value(QStringLiteral("sanitized")).toBool());
    QVERIFY(!manifest.value(QStringLiteral("liveModelUsed")).toBool(true));
    QVERIFY(!manifest.value(QStringLiteral("userConfigurationModified")).toBool(true));
    QCOMPARE(manifest.value(QStringLiteral("streamFixtureOrigin")).toString(),
             QStringLiteral("synthetic-official-example-derived"));

    const QByteArray serialized = QJsonDocument(manifest).toJson(QJsonDocument::Compact).toLower();
    const QList<QByteArray> forbidden = {"c:\\\\users",  "d:\\\\projects", "oauth_token",
                                         "access_token", "api_key",        "authorization"};
    for (const QByteArray& marker : forbidden) {
        QVERIFY2(!serialized.contains(marker), marker.constData());
    }
}

void TestClaudeProtocolSpike::coversRequiredCliOptionsAndNegativeControl() {
    const QJsonObject manifest = loadManifest();
    const QSet<QString> options =
        stringSet(manifest.value(QStringLiteral("requiredOptions")).toArray());
    const QSet<QString> expectedOptions = {
        QStringLiteral("--init-only"),
        QStringLiteral("--input-format=stream-json"),
        QStringLiteral("--output-format=stream-json"),
        QStringLiteral("--permission-prompt-tool"),
        QStringLiteral("--safe-mode"),
        QStringLiteral("--strict-mcp-config"),
    };
    for (const QString& option : expectedOptions) {
        QVERIFY2(options.contains(option), qPrintable(option));
    }

    QSet<QString> probeIds;
    bool hasUnknownOptionControl = false;
    for (const QJsonValue& value : manifest.value(QStringLiteral("probes")).toArray()) {
        const QJsonObject probe = value.toObject();
        const QString id = probe.value(QStringLiteral("id")).toString();
        QVERIFY(!id.isEmpty());
        QVERIFY(!probeIds.contains(id));
        probeIds.insert(id);
        QVERIFY(probe.contains(QStringLiteral("exitCode")));
        QVERIFY(!probe.value(QStringLiteral("resultCategory")).toString().isEmpty());
        if (id == QLatin1String("unknown-option-negative-control")) {
            hasUnknownOptionControl = probe.value(QStringLiteral("exitCode")).toInt() != 0;
        }
    }

    const QSet<QString> expectedProbeIds = {
        QStringLiteral("init-only"),
        QStringLiteral("permission-prompt-tool-parse"),
        QStringLiteral("stream-invalid-json"),
        QStringLiteral("stream-missing-type"),
        QStringLiteral("stream-missing-message"),
        QStringLiteral("stream-eof"),
        QStringLiteral("resume-init-only"),
        QStringLiteral("resume-fork-init-only"),
        QStringLiteral("unknown-option-negative-control"),
    };
    for (const QString& id : expectedProbeIds) {
        QVERIFY2(probeIds.contains(id), qPrintable(id));
    }
    QVERIFY(hasUnknownOptionControl);
}

void TestClaudeProtocolSpike::recordsOfficialCapabilityVersionGates() {
    const QJsonObject manifest = loadManifest();
    QCOMPARE(manifest.value(QStringLiteral("minimumVersion")).toString(),
             QStringLiteral("2.1.219"));
    QCOMPARE(manifest.value(QStringLiteral("minimumVersionStatus")).toString(),
             QStringLiteral("frozen"));

    const QJsonArray sources = manifest.value(QStringLiteral("sourceUrls")).toArray();
    QVERIFY(sources.size() >= 5);
    for (const QJsonValue& value : sources) {
        QVERIFY(value.toString().startsWith(QStringLiteral("https://code.claude.com/docs/")));
    }

    QSet<QString> capabilities;
    for (const QJsonValue& value : manifest.value(QStringLiteral("versionGates")).toArray()) {
        const QJsonObject gate = value.toObject();
        capabilities.insert(gate.value(QStringLiteral("capability")).toString());
        QVERIFY(!gate.value(QStringLiteral("minimumCliVersion")).toString().isEmpty());
        QVERIFY(sources.contains(gate.value(QStringLiteral("sourceUrl"))));
    }
    QVERIFY(capabilities.contains(QStringLiteral("permission-prompt-tool-interaction-guard")));
    QVERIFY(capabilities.contains(QStringLiteral("interrupt-receipt-v1")));
    QVERIFY(capabilities.contains(QStringLiteral("interrupt-cancel-queued-v1")));
}

void TestClaudeProtocolSpike::parsesInitAfterStartupEvents() {
    using namespace snack::spike::claude;
    const QList<StreamRecord> records = loadJsonLines(QStringLiteral("system-init.jsonl"));
    QCOMPARE(records.size(), 2);
    QCOMPARE(records.at(0).kind, StreamRecordKind::SystemEvent);
    QCOMPARE(records.at(1).kind, StreamRecordKind::SystemInit);
    QVERIFY(records.at(1).payload.contains(QStringLiteral("future_field")));

    StreamContractState state;
    for (const StreamRecord& record : records) {
        QVERIFY(state.consume(record));
    }
    QCOMPARE(state.sessionId(), QStringLiteral("10000000-0000-4000-8000-000000000001"));
    QCOMPARE(state.capabilities(), QStringList({QStringLiteral("interrupt_receipt_v1"),
                                                QStringLiteral("interrupt_cancel_queued_v1"),
                                                QStringLiteral("future_capability_v9")}));
}

void TestClaudeProtocolSpike::separatesLongLivedStreamTurns() {
    using namespace snack::spike::claude;
    const QList<StreamRecord> records = loadJsonLines(QStringLiteral("multi-turn.jsonl"));
    QCOMPARE(records.size(), 8);

    StreamContractState state;
    int resultCount = 0;
    int partialCount = 0;
    for (const StreamRecord& record : records) {
        QVERIFY(state.consume(record));
        resultCount += record.kind == StreamRecordKind::Result ? 1 : 0;
        partialCount += record.kind == StreamRecordKind::PartialAssistant ? 1 : 0;
    }
    QCOMPARE(resultCount, 2);
    QCOMPARE(partialCount, 1);
    QCOMPARE(state.completedUserMessages(),
             QStringList({QStringLiteral("20000000-0000-4000-8000-000000000001"),
                          QStringLiteral("20000000-0000-4000-8000-000000000002")}));
    QVERIFY(state.pendingUserMessages().isEmpty());
}

void TestClaudeProtocolSpike::tracksQueuedImageTurns() {
    using namespace snack::spike::claude;
    const QList<StreamRecord> records = loadJsonLines(QStringLiteral("queue-image.jsonl"));
    QCOMPARE(records.size(), 4);
    QVERIFY(containsImage(records.at(1)));

    StreamContractState state;
    QVERIFY(state.consume(records.at(0)));
    QVERIFY(state.consume(records.at(1)));
    QCOMPARE(state.pendingUserMessages().size(), 2);
    QVERIFY(state.consume(records.at(2)));
    QCOMPARE(state.pendingUserMessages(),
             QStringList({QStringLiteral("30000000-0000-4000-8000-000000000002")}));
    QVERIFY(state.consume(records.at(3)));
    QVERIFY(state.pendingUserMessages().isEmpty());
}

void TestClaudeProtocolSpike::rejectsMalformedAndCrossSessionRecords() {
    using namespace snack::spike::claude;
    const StreamRecord malformed = parseStreamRecord(QByteArrayLiteral("not-json"));
    QCOMPARE(malformed.kind, StreamRecordKind::Malformed);
    QVERIFY(!malformed.error.isEmpty());

    StreamContractState state;
    QVERIFY(state.consume(parseStreamRecord(
        QByteArrayLiteral(R"({"type":"user","uuid":"one","session_id":"session-a"})"))));
    QVERIFY(!state.consume(
        parseStreamRecord(QByteArrayLiteral(R"({"type":"result","session_id":"session-b"})"))));

    const StreamRecord unknown =
        parseStreamRecord(QByteArrayLiteral(R"({"type":"future-event","new_field":true})"));
    QCOMPARE(unknown.kind, StreamRecordKind::Unknown);
    QVERIFY(unknown.payload.value(QStringLiteral("new_field")).toBool());
}

void TestClaudeProtocolSpike::reconcilesInterruptReceiptsByCapability() {
    using namespace snack::spike::claude;
    const QJsonArray cases = loadArrayFixture(QStringLiteral("interrupt-receipts.json"));
    QCOMPARE(cases.size(), 4);

    for (const QJsonValue& value : cases) {
        const QJsonObject fixture = value.toObject();
        const QStringList capabilities =
            stringSet(fixture.value(QStringLiteral("capabilities")).toArray()).values();
        const QStringList knownQueued =
            stringSet(fixture.value(QStringLiteral("knownQueued")).toArray()).values();
        const bool hasReceipt = fixture.value(QStringLiteral("receipt")).isObject();
        const InterruptReceipt receipt =
            hasReceipt ? parseInterruptReceipt(fixture.value(QStringLiteral("receipt")).toObject())
                       : InterruptReceipt{};
        const QueueReconciliation result =
            reconcileInterruptQueue(knownQueued, capabilities, hasReceipt ? &receipt : nullptr);

        QCOMPARE(result.authoritative,
                 fixture.value(QStringLiteral("expectedAuthoritative")).toBool());
        QCOMPARE(QSet<QString>(result.survivingKnown.begin(), result.survivingKnown.end()),
                 stringSet(fixture.value(QStringLiteral("expectedSurvivingKnown")).toArray()));
        QCOMPARE(QSet<QString>(result.cancelledKnown.begin(), result.cancelledKnown.end()),
                 stringSet(fixture.value(QStringLiteral("expectedCancelledKnown")).toArray()));
        QCOMPARE(QSet<QString>(result.unrecognized.begin(), result.unrecognized.end()),
                 stringSet(fixture.value(QStringLiteral("expectedUnrecognized")).toArray()));
    }
}

void TestClaudeProtocolSpike::rejectsAmbiguousInterruptReceipts() {
    using namespace snack::spike::claude;
    const InterruptReceipt missing = parseInterruptReceipt({});
    QVERIFY(!missing.valid);
    const InterruptReceipt duplicate =
        parseInterruptReceipt({{QStringLiteral("still_queued"),
                                QJsonArray{QStringLiteral("one"), QStringLiteral("one")}}});
    QVERIFY(!duplicate.valid);
    const InterruptReceipt overlap =
        parseInterruptReceipt({{QStringLiteral("still_queued"), QJsonArray{QStringLiteral("one")}},
                               {QStringLiteral("cancelled"), QJsonArray{QStringLiteral("one")}}});
    QVERIFY(!overlap.valid);

    QVERIFY(!supportsInterruptReceipt({}));
    QVERIFY(supportsInterruptReceipt({QStringLiteral("interrupt_receipt_v1")}));
    QVERIFY(!supportsQueuedCancellation({QStringLiteral("interrupt_receipt_v1")}));
    QVERIFY(!supportsQueuedCancellation({QStringLiteral("interrupt_cancel_queued_v1")}));
    QVERIFY(supportsQueuedCancellation(
        {QStringLiteral("interrupt_receipt_v1"), QStringLiteral("interrupt_cancel_queued_v1")}));
}

void TestClaudeProtocolSpike::servesMinimalPermissionMcpContract() {
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    const QString eventLog = temporaryDirectory.filePath(QStringLiteral("events.txt"));

    QProcess server;
    server.start(QStringLiteral(SNACK_PERMISSION_PROBE_SERVER),
                 {QStringLiteral("--event-log"), eventLog}, QIODevice::ReadWrite);
    QVERIFY(server.waitForStarted());
    server.write(
        R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-06-18"}})"
        "\n");
    server.write(R"({"jsonrpc":"2.0","method":"notifications/initialized"})"
                 "\n");
    server.write(R"({"jsonrpc":"2.0","id":2,"method":"tools/list"})"
                 "\n");
    server.closeWriteChannel();
    QVERIFY(server.waitForFinished());
    QCOMPARE(server.exitStatus(), QProcess::NormalExit);
    QCOMPARE(server.exitCode(), 0);

    QList<QJsonObject> responses;
    for (const QByteArray& line : server.readAllStandardOutput().split('\n')) {
        if (!line.trimmed().isEmpty()) {
            responses.append(QJsonDocument::fromJson(line).object());
        }
    }
    QCOMPARE(responses.size(), 2);
    QCOMPARE(responses.at(0).value(QStringLiteral("id")).toInt(), 1);
    QCOMPARE(responses.at(0)
                 .value(QStringLiteral("result"))
                 .toObject()
                 .value(QStringLiteral("serverInfo"))
                 .toObject()
                 .value(QStringLiteral("name"))
                 .toString(),
             QStringLiteral("snack-claude-permission-probe"));
    QCOMPARE(responses.at(1).value(QStringLiteral("id")).toInt(), 2);
    const QJsonArray tools = responses.at(1)
                                 .value(QStringLiteral("result"))
                                 .toObject()
                                 .value(QStringLiteral("tools"))
                                 .toArray();
    QCOMPARE(tools.size(), 1);
    QCOMPARE(tools.at(0).toObject().value(QStringLiteral("name")).toString(),
             QStringLiteral("permission"));

    QFile logFile(eventLog);
    QVERIFY(logFile.open(QIODevice::ReadOnly));
    QByteArray methods = logFile.readAll();
    methods.replace("\r\n", "\n");
    QCOMPARE(methods, QByteArray("initialize\nnotifications/initialized\ntools/list\n"));
}

void TestClaudeProtocolSpike::recordsLocalPermissionBridgeHandshake() {
    const QJsonObject bridge = loadManifest().value(QStringLiteral("permissionBridge")).toObject();
    QCOMPARE(bridge.value(QStringLiteral("transport")).toString(), QStringLiteral("mcp-stdio"));
    QCOMPARE(bridge.value(QStringLiteral("serverImplementation")).toString(),
             QStringLiteral("C++20-Qt6"));
    QCOMPARE(bridge.value(QStringLiteral("configurationScope")).toString(),
             QStringLiteral("inline-strict-only"));
    QCOMPARE(bridge.value(QStringLiteral("claudeExitCode")).toInt(-1), 0);
    QVERIFY(bridge.value(QStringLiteral("handshakeVerified")).toBool());
    QVERIFY(!bridge.value(QStringLiteral("permissionPromptInvoked")).toBool(true));
    QVERIFY(!bridge.value(QStringLiteral("liveModelUsed")).toBool(true));
    QVERIFY(!bridge.value(QStringLiteral("userConfigurationModified")).toBool(true));
    QCOMPARE(
        stringSet(bridge.value(QStringLiteral("observedMethods")).toArray()),
        QSet<QString>({QStringLiteral("initialize"), QStringLiteral("notifications/initialized"),
                       QStringLiteral("tools/list")}));
}

void TestClaudeProtocolSpike::freezesPureCppRuntimeControlDegradation() {
    const QJsonArray controls = loadArrayFixture(QStringLiteral("runtime-controls.json"));
    QCOMPARE(controls.size(), 3);

    QSet<QString> ids;
    for (const QJsonValue& value : controls) {
        const QJsonObject control = value.toObject();
        ids.insert(control.value(QStringLiteral("id")).toString());
        QVERIFY(!control.value(QStringLiteral("startupFlag")).toString().isEmpty());
        QVERIFY(!control.value(QStringLiteral("stableCppDirectSurface")).toBool(true));
        QVERIFY(!control.value(QStringLiteral("undocumentedWireAllowed")).toBool(true));
        QCOMPARE(control.value(QStringLiteral("runningTurnPolicy")).toString(),
                 QStringLiteral("defer-to-next-turn"));
        QCOMPARE(control.value(QStringLiteral("idlePolicy")).toString(),
                 QStringLiteral("restart-and-resume"));

        const QJsonArray officialLiveSurfaces =
            control.value(QStringLiteral("officialLiveSurfaces")).toArray();
        QVERIFY(!officialLiveSurfaces.isEmpty());
        for (const QJsonValue& surfaceValue : officialLiveSurfaces) {
            const QJsonObject surface = surfaceValue.toObject();
            QCOMPARE(surface.value(QStringLiteral("language")).toString(),
                     QStringLiteral("TypeScript"));
            QVERIFY(surface.value(QStringLiteral("streamingInputOnly")).toBool());
        }
    }
    QCOMPARE(ids, QSet<QString>({QStringLiteral("model"), QStringLiteral("effort"),
                                 QStringLiteral("permission-mode")}));

    const QJsonObject permission = controls.at(2).toObject();
    QCOMPARE(permission.value(QStringLiteral("pendingPromptPolicy")).toString(),
             QStringLiteral("bridge-policy-immediate"));
}

void TestClaudeProtocolSpike::recordsStrictStartupControlValidation() {
    const QJsonObject probe = loadManifest().value(QStringLiteral("runtimeFlagProbe")).toObject();
    QVERIFY(!probe.value(QStringLiteral("liveModelUsed")).toBool(true));
    QCOMPARE(probe.value(QStringLiteral("combinedValidFlags")).toString(),
             QStringLiteral("accepted-empty-success"));
    QCOMPARE(probe.value(QStringLiteral("invalidEffort")).toString(),
             QStringLiteral("warning-default-exit-0"));
    QCOMPARE(probe.value(QStringLiteral("invalidPermissionMode")).toString(),
             QStringLiteral("parser-error-exit-1"));
    QCOMPARE(probe.value(QStringLiteral("invalidModel")).toString(),
             QStringLiteral("deferred-validation-empty-success"));
    QCOMPARE(stringSet(probe.value(QStringLiteral("locallyAdvertisedEffortValues")).toArray()),
             QSet<QString>({QStringLiteral("low"), QStringLiteral("medium"), QStringLiteral("high"),
                            QStringLiteral("xhigh"), QStringLiteral("max")}));
    QCOMPARE(stringSet(probe.value(QStringLiteral("permissionModes")).toArray()),
             QSet<QString>({QStringLiteral("acceptEdits"), QStringLiteral("auto"),
                            QStringLiteral("bypassPermissions"), QStringLiteral("manual"),
                            QStringLiteral("dontAsk"), QStringLiteral("plan")}));
}

void TestClaudeProtocolSpike::gatesMinimumClaudeVersion_data() {
    QTest::addColumn<QString>("version");
    QTest::addColumn<bool>("accepted");

    QTest::newRow("older") << QStringLiteral("2.1.218") << false;
    QTest::newRow("minimum-prerelease") << QStringLiteral("2.1.219-beta.1") << false;
    QTest::newRow("minimum") << QStringLiteral("2.1.219") << true;
    QTest::newRow("reference") << QStringLiteral("2.1.245") << true;
    QTest::newRow("newer-prerelease") << QStringLiteral("2.1.246-beta.1") << true;
    QTest::newRow("metadata") << QStringLiteral("2.1.245+local") << true;
    QTest::newRow("missing-patch") << QStringLiteral("2.1") << false;
    QTest::newRow("garbage") << QStringLiteral("claude") << false;
}

void TestClaudeProtocolSpike::gatesMinimumClaudeVersion() {
    QFETCH(QString, version);
    QFETCH(bool, accepted);
    using namespace snack::spike::claude;
    QCOMPARE(minimumSupportedVersion(), QStringLiteral("2.1.219"));
    QCOMPARE(meetsMinimumVersion(version), accepted);
}

QTEST_GUILESS_MAIN(TestClaudeProtocolSpike)
#include "TestClaudeProtocolSpike.moc"
