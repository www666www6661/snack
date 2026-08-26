#include "agent/AgentRuntime.h"

#include <QTest>

class TestAgentRuntime final : public QObject {
    Q_OBJECT

  private slots:
    void createsCodexRuntimeWhenAvailable();
    void fallsBackWhenCodexIsUnavailable();
    void preservesUnsupportedCodexVersionDetail();
    void honorsExplicitMockSelection();
    void degradesUnimplementedClaudeSelection();
};

void TestAgentRuntime::createsCodexRuntimeWhenAvailable() {
    const auto installation =
        snack::agent::codex::CliInstallation{.status = snack::agent::codex::CliStatus::Available,
                                             .executablePath = QStringLiteral("codex"),
                                             .version = QStringLiteral("0.149.0")};
    auto runtime = snack::agent::AgentRuntimeFactory::createWithCodexInstallation(
        snack::domain::AgentKind::Codex, installation);

    QCOMPARE(runtime.requestedKind, snack::domain::AgentKind::Codex);
    QCOMPARE(runtime.selectedKind, snack::domain::AgentKind::Codex);
    QVERIFY(!runtime.fellBack);
    QVERIFY(runtime.transport != nullptr);
    QVERIFY(runtime.adapter != nullptr);
    QCOMPARE(runtime.adapter->kind(), snack::domain::AgentKind::Codex);
    QVERIFY(runtime.detail.contains(QStringLiteral("0.149.0")));
}

void TestAgentRuntime::fallsBackWhenCodexIsUnavailable() {
    auto runtime = snack::agent::AgentRuntimeFactory::createWithCodexInstallation(
        snack::domain::AgentKind::Codex, {.status = snack::agent::codex::CliStatus::NotFound,
                                          .detail = QStringLiteral("not installed")});

    QCOMPARE(runtime.requestedKind, snack::domain::AgentKind::Codex);
    QCOMPARE(runtime.selectedKind, snack::domain::AgentKind::Mock);
    QVERIFY(runtime.fellBack);
    QVERIFY(runtime.transport == nullptr);
    QCOMPARE(runtime.adapter->kind(), snack::domain::AgentKind::Mock);
    QCOMPARE(runtime.detail, QStringLiteral("not installed"));
}

void TestAgentRuntime::preservesUnsupportedCodexVersionDetail() {
    const QString detail =
        QStringLiteral("Codex CLI 0.148.0 is unsupported; Snack requires 0.149.0 or newer");
    auto runtime = snack::agent::AgentRuntimeFactory::createWithCodexInstallation(
        snack::domain::AgentKind::Codex,
        {.status = snack::agent::codex::CliStatus::UnsupportedVersion,
         .executablePath = QStringLiteral("codex"),
         .version = QStringLiteral("0.148.0"),
         .detail = detail});

    QCOMPARE(runtime.selectedKind, snack::domain::AgentKind::Mock);
    QVERIFY(runtime.fellBack);
    QCOMPARE(runtime.detail, detail);
}

void TestAgentRuntime::honorsExplicitMockSelection() {
    auto runtime = snack::agent::AgentRuntimeFactory::createWithCodexInstallation(
        snack::domain::AgentKind::Mock, {});

    QCOMPARE(runtime.selectedKind, snack::domain::AgentKind::Mock);
    QVERIFY(!runtime.fellBack);
    QVERIFY(runtime.transport == nullptr);
    QCOMPARE(runtime.adapter->kind(), snack::domain::AgentKind::Mock);
}

void TestAgentRuntime::degradesUnimplementedClaudeSelection() {
    auto runtime = snack::agent::AgentRuntimeFactory::createWithCodexInstallation(
        snack::domain::AgentKind::Claude, {});

    QCOMPARE(runtime.selectedKind, snack::domain::AgentKind::Mock);
    QVERIFY(runtime.fellBack);
    QVERIFY(runtime.detail.contains(QStringLiteral("Claude")));
}

QTEST_GUILESS_MAIN(TestAgentRuntime)
#include "TestAgentRuntime.moc"
