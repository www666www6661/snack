#include "agent/claude/ClaudeAdapter.h"
#include "agent/codex/CodexAdapter.h"
#include "agent/process/IProcessTransport.h"

#include <QSignalSpy>
#include <QTest>

#include <memory>

class ContractProcessTransport final : public snack::agent::process::IProcessTransport {
  public:
    using IProcessTransport::IProcessTransport;

    [[nodiscard]] bool isRunning() const override { return false; }
    void start(const snack::agent::process::LaunchSpec&) override { ++startCalls; }
    qint64 write(const QByteArray&) override { return -1; }
    void closeWriteChannel() override {}
    void terminate() override {}
    void kill() override {}

    int startCalls{0};
};

class TestAgentAdapterContract final : public QObject {
    Q_OBJECT

  private slots:
    void codexAndClaudeShareSafetyContract();
};

namespace {

void verifySafetyContract(snack::agent::IAgentAdapter* adapter,
                          snack::domain::AgentKind expectedKind,
                          ContractProcessTransport* transport) {
    QCOMPARE(adapter->kind(), expectedKind);
    const snack::agent::CapabilitySet capabilities = adapter->capabilities();
    QCOMPARE(capabilities.accessLevels,
             QList({snack::domain::AccessLevel::Strict, snack::domain::AccessLevel::Workspace,
                    snack::domain::AccessLevel::Full}));
    QVERIFY(capabilities.supportsInterrupt);
    QVERIFY(!adapter->steerTurn({QUuid::createUuid(), QStringLiteral("invalid steer")}));
    QVERIFY(!adapter->respondToApproval(QStringLiteral("unknown"),
                                        snack::domain::ApprovalDecision::Accept));
    QVERIFY(!adapter->respondToUserInput(QStringLiteral("unknown"), {}));

    QSignalSpy eventSpy(adapter, &snack::agent::IAgentAdapter::eventReceived);
    QSignalSpy finishedSpy(adapter, &snack::agent::IAgentAdapter::turnFinished);
    const QUuid turnId = QUuid::createUuid();
    snack::domain::TurnSettingsSnapshot settings;
    settings.agentKind = expectedKind;
    settings.workingDirectory = QStringLiteral("/workspace");
    adapter->startTurn({turnId, QStringLiteral("must reject before connect"), settings, {}});
    QCOMPARE(eventSpy.count(), 1);
    QCOMPARE(eventSpy.constFirst().constFirst().value<snack::domain::AgentEvent>().type,
             snack::domain::AgentEventType::TurnFailed);
    QCOMPARE(finishedSpy.count(), 1);
    QCOMPARE(finishedSpy.constFirst().at(0).toUuid(), turnId);
    QCOMPARE(finishedSpy.constFirst().at(1).toBool(), false);
    QCOMPARE(finishedSpy.constFirst().at(2).toBool(), false);

    QSignalSpy connectionSpy(adapter, &snack::agent::IAgentAdapter::connectionChanged);
    adapter->connectAgent({.settings = settings});
    QTRY_COMPARE_WITH_TIMEOUT(connectionSpy.count(), 1, 1000);
    QCOMPARE(connectionSpy.constFirst().at(0).toBool(), false);
    QVERIFY(!connectionSpy.constFirst().at(1).toString().isEmpty());
    QCOMPARE(transport->startCalls, 0);

    adapter->interruptTurn();
    adapter->closeAgent();
    QCOMPARE(connectionSpy.count(), 1);
}

} // namespace

void TestAgentAdapterContract::codexAndClaudeShareSafetyContract() {
    ContractProcessTransport codexTransport;
    snack::agent::codex::CodexAdapter codex({.status = snack::agent::codex::CliStatus::Available,
                                             .executablePath = QStringLiteral("codex"),
                                             .version = QStringLiteral("0.149.0")},
                                            &codexTransport);
    verifySafetyContract(&codex, snack::domain::AgentKind::Codex, &codexTransport);

    ContractProcessTransport claudeTransport;
    snack::agent::claude::ClaudeAdapter claude(
        {.status = snack::agent::claude::CliStatus::Available,
         .executablePath = QStringLiteral("claude"),
         .version = QStringLiteral("2.1.245")},
        &claudeTransport, nullptr, QStringLiteral("permission-helper"));
    verifySafetyContract(&claude, snack::domain::AgentKind::Claude, &claudeTransport);
}

QTEST_GUILESS_MAIN(TestAgentAdapterContract)

#include "TestAgentAdapterContract.moc"
