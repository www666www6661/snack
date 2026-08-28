#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QtTest>

class TestClaudeProtocolSpike : public QObject {
    Q_OBJECT

  private slots:
    void recordsSanitizedNoModelEvidence();
    void coversRequiredCliOptionsAndNegativeControl();
    void recordsOfficialCapabilityVersionGates();
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

QSet<QString> stringSet(const QJsonArray& values) {
    QSet<QString> result;
    for (const QJsonValue& value : values) {
        result.insert(value.toString());
    }
    return result;
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
        QStringLiteral("unknown-option-negative-control"),
    };
    for (const QString& id : expectedProbeIds) {
        QVERIFY2(probeIds.contains(id), qPrintable(id));
    }
    QVERIFY(hasUnknownOptionControl);
}

void TestClaudeProtocolSpike::recordsOfficialCapabilityVersionGates() {
    const QJsonObject manifest = loadManifest();
    QCOMPARE(manifest.value(QStringLiteral("minimumVersionCandidate")).toString(),
             QStringLiteral("2.1.219"));
    QCOMPARE(manifest.value(QStringLiteral("minimumVersionStatus")).toString(),
             QStringLiteral("candidate"));

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

QTEST_GUILESS_MAIN(TestClaudeProtocolSpike)
#include "TestClaudeProtocolSpike.moc"
