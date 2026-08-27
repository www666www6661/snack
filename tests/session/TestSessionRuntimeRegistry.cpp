#include "session/SessionRuntimeRegistry.h"

#include "storage/EventStore.h"

#include <QTemporaryDir>
#include <QTest>

#include <memory>

namespace {

struct TrackingState {
    QStringList closedAdapters;
};

class TrackingAdapter final : public snack::agent::IAgentAdapter {
  public:
    TrackingAdapter(QString name, std::shared_ptr<TrackingState> state)
        : name_(std::move(name)), state_(std::move(state)) {}

    [[nodiscard]] snack::domain::AgentKind kind() const override {
        return snack::domain::AgentKind::Mock;
    }
    [[nodiscard]] snack::agent::CapabilitySet capabilities() const override { return {}; }
    void connectAgent(const snack::agent::AgentConnectionRequest&) override {
        emit connectionChanged(true, QStringLiteral("ready"));
    }
    void startTurn(const snack::agent::TurnRequest&) override {}
    bool steerTurn(const snack::agent::SteerRequest&) override { return false; }
    bool respondToApproval(const QString&, snack::domain::ApprovalDecision) override {
        return false;
    }
    bool respondToUserInput(const QString&, const QJsonObject&) override { return false; }
    void interruptTurn() override {}
    void closeAgent() override { state_->closedAdapters.append(name_); }

  private:
    QString name_;
    std::shared_ptr<TrackingState> state_;
};

snack::domain::Conversation conversation(const QString& title) {
    snack::domain::Conversation result;
    result.title = title;
    result.workingDirectory = QStringLiteral("/workspace/") + title;
    result.agentKind = snack::domain::AgentKind::Mock;
    return result;
}

snack::agent::AgentRuntime runtime(const QString& name,
                                   const std::shared_ptr<TrackingState>& state) {
    snack::agent::AgentRuntime result;
    result.requestedKind = snack::domain::AgentKind::Mock;
    result.selectedKind = snack::domain::AgentKind::Mock;
    result.adapter = std::make_unique<TrackingAdapter>(name, state);
    return result;
}

} // namespace

class TestSessionRuntimeRegistry final : public QObject {
    Q_OBJECT

  private slots:
    void ownsFindsAndClosesSessions();
    void closesAllSessionsInReverseOrder();
    void doesNotCloseAnAlreadyClosedControllerTwice();
    void rejectsInvalidRuntimeEntries();
};

void TestSessionRuntimeRegistry::ownsFindsAndClosesSessions() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    snack::storage::EventStore repository;
    QString error;
    QVERIFY2(repository.open(directory.filePath(QStringLiteral("events.sqlite3")), &error),
             qPrintable(error));
    auto state = std::make_shared<TrackingState>();
    snack::session::SessionRuntimeRegistry registry(&repository);
    const auto first = conversation(QStringLiteral("first"));
    const auto second = conversation(QStringLiteral("second"));

    QVERIFY(registry.add(first, runtime(QStringLiteral("first"), state), &error));
    QVERIFY(registry.add(second, runtime(QStringLiteral("second"), state), &error));
    QCOMPARE(registry.size(), qsizetype{2});
    QCOMPARE(registry.conversationIds(), QList<QUuid>({first.id, second.id}));
    QVERIFY(registry.controller(first.id) != nullptr);
    QCOMPARE(registry.controller(first.id)->conversation().title, QStringLiteral("first"));
    QVERIFY(registry.runtime(second.id) != nullptr);
    QCOMPARE(registry.runtime(second.id)->selectedKind, snack::domain::AgentKind::Mock);
    QVERIFY(registry.controller(QUuid::createUuid()) == nullptr);

    bool removedBeforeClosedSignal = false;
    connect(
        registry.controller(first.id), &snack::session::SessionController::statusChanged,
        [&registry, &first, &removedBeforeClosedSignal](snack::domain::ConversationStatus status) {
            if (status == snack::domain::ConversationStatus::Closed) {
                removedBeforeClosedSignal = registry.controller(first.id) == nullptr;
            }
        });
    QVERIFY(registry.close(first.id));
    QVERIFY(removedBeforeClosedSignal);
    QCOMPARE(state->closedAdapters, QStringList({QStringLiteral("first")}));
    QCOMPARE(registry.size(), qsizetype{1});
    QVERIFY(!registry.close(first.id));
}

void TestSessionRuntimeRegistry::closesAllSessionsInReverseOrder() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    snack::storage::EventStore repository;
    QString error;
    QVERIFY(repository.open(directory.filePath(QStringLiteral("events.sqlite3")), &error));
    auto state = std::make_shared<TrackingState>();
    {
        snack::session::SessionRuntimeRegistry registry(&repository);
        QVERIFY(registry.add(conversation(QStringLiteral("first")),
                             runtime(QStringLiteral("first"), state), &error));
        QVERIFY(registry.add(conversation(QStringLiteral("second")),
                             runtime(QStringLiteral("second"), state), &error));
    }
    QCOMPARE(state->closedAdapters,
             QStringList({QStringLiteral("second"), QStringLiteral("first")}));
}

void TestSessionRuntimeRegistry::doesNotCloseAnAlreadyClosedControllerTwice() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    snack::storage::EventStore repository;
    QString error;
    QVERIFY(repository.open(directory.filePath(QStringLiteral("events.sqlite3")), &error));
    auto state = std::make_shared<TrackingState>();
    snack::session::SessionRuntimeRegistry registry(&repository);
    const auto value = conversation(QStringLiteral("already closed"));
    QVERIFY(registry.add(value, runtime(QStringLiteral("already closed"), state), &error));

    registry.controller(value.id)->close();
    QCOMPARE(state->closedAdapters, QStringList({QStringLiteral("already closed")}));
    registry.closeAll();
    QCOMPARE(state->closedAdapters, QStringList({QStringLiteral("already closed")}));
    QCOMPARE(registry.size(), qsizetype{0});
}

void TestSessionRuntimeRegistry::rejectsInvalidRuntimeEntries() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    snack::storage::EventStore repository;
    QString error;
    QVERIFY(repository.open(directory.filePath(QStringLiteral("events.sqlite3")), &error));
    auto state = std::make_shared<TrackingState>();
    snack::session::SessionRuntimeRegistry registry(&repository);
    const auto valid = conversation(QStringLiteral("valid"));
    QVERIFY(registry.add(valid, runtime(QStringLiteral("valid"), state), &error));
    QVERIFY(!registry.add(valid, runtime(QStringLiteral("duplicate"), state), &error));
    QVERIFY(error.contains(QStringLiteral("already exists")));

    auto invalidId = conversation(QStringLiteral("invalid"));
    invalidId.id = QUuid{};
    QVERIFY(!registry.add(invalidId, runtime(QStringLiteral("invalid"), state), &error));
    QVERIFY(error.contains(QStringLiteral("invalid conversation ID")));

    auto mismatch = conversation(QStringLiteral("mismatch"));
    mismatch.agentKind = snack::domain::AgentKind::Codex;
    QVERIFY(!registry.add(mismatch, runtime(QStringLiteral("mismatch"), state), &error));
    QVERIFY(error.contains(QStringLiteral("do not match")));

    snack::agent::AgentRuntime emptyRuntime;
    auto empty = conversation(QStringLiteral("empty"));
    QVERIFY(!registry.add(empty, std::move(emptyRuntime), &error));
    QCOMPARE(registry.size(), qsizetype{1});
}

QTEST_GUILESS_MAIN(TestSessionRuntimeRegistry)
#include "TestSessionRuntimeRegistry.moc"
