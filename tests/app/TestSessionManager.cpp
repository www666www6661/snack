#include "app/SessionManager.h"

#include "storage/EventStore.h"

#include <QTemporaryDir>
#include <QTest>

#include <memory>

namespace {

class TestAdapter final : public snack::agent::IAgentAdapter {
  public:
    explicit TestAdapter(snack::domain::AgentKind kind) : kind_(kind) {}

    [[nodiscard]] snack::domain::AgentKind kind() const override { return kind_; }
    [[nodiscard]] snack::agent::CapabilitySet capabilities() const override { return {}; }
    void connectAgent(const snack::agent::AgentConnectionRequest&) override {}
    void startTurn(const snack::agent::TurnRequest&) override {}
    bool steerTurn(const snack::agent::SteerRequest&) override { return false; }
    bool respondToApproval(const QString&, snack::domain::ApprovalDecision) override {
        return false;
    }
    bool respondToUserInput(const QString&, const QJsonObject&) override { return false; }
    void interruptTurn() override {}
    void closeAgent() override {}

  private:
    snack::domain::AgentKind kind_;
};

snack::domain::Conversation conversation(snack::domain::AgentKind kind) {
    snack::domain::Conversation result;
    result.title = QStringLiteral("Conversation");
    result.workingDirectory = QStringLiteral("/workspace");
    result.agentKind = kind;
    return result;
}

snack::agent::AgentRuntime runtime(snack::domain::AgentKind kind) {
    snack::agent::AgentRuntime result;
    result.requestedKind = kind;
    result.selectedKind = kind;
    result.adapter = std::make_unique<TestAdapter>(kind);
    return result;
}

} // namespace

class TestSessionManager final : public QObject {
    Q_OBJECT

  private slots:
    void adoptsPreparedRuntimeAndReusesOpenSession();
    void opensMultipleConversationsOnDemand();
    void rejectsUnavailableAndConflictingAgentTypes();
    void validatesBeforeCreatingRuntime();
    void createsNewConversationWithExplicitFallback();
    void rejectsInvalidNewConversationRequests();
    void archivesIdleConversationAndClosesRuntime();
    void rejectsArchivingActiveConversation();
    void restoresArchivedConversationMetadata();
    void keepsConversationArchivedWhenRestoreRuntimeIsUnavailable();
};

void TestSessionManager::adoptsPreparedRuntimeAndReusesOpenSession() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    snack::storage::EventStore repository;
    QString error;
    QVERIFY(repository.open(directory.filePath(QStringLiteral("events.sqlite3")), &error));
    int factoryCalls = 0;
    snack::app::SessionManager manager(&repository, [&factoryCalls](snack::domain::AgentKind kind) {
        ++factoryCalls;
        return runtime(kind);
    });
    const auto value = conversation(snack::domain::AgentKind::Mock);

    auto* prepared = manager.addPrepared(value, runtime(value.agentKind), &error);
    QVERIFY2(prepared != nullptr, qPrintable(error));
    QCOMPARE(manager.open(value, &error), prepared);
    QCOMPARE(factoryCalls, 0);
    QCOMPARE(manager.conversationIds(), QList<QUuid>({value.id}));
}

void TestSessionManager::opensMultipleConversationsOnDemand() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    snack::storage::EventStore repository;
    QString error;
    QVERIFY(repository.open(directory.filePath(QStringLiteral("events.sqlite3")), &error));
    int factoryCalls = 0;
    snack::app::SessionManager manager(&repository, [&factoryCalls](snack::domain::AgentKind kind) {
        ++factoryCalls;
        return runtime(kind);
    });
    const auto codex = conversation(snack::domain::AgentKind::Codex);
    auto mock = conversation(snack::domain::AgentKind::Mock);
    mock.status = snack::domain::ConversationStatus::Running;

    QVERIFY(manager.open(codex, &error) != nullptr);
    QVERIFY(manager.open(mock, &error) != nullptr);
    QCOMPARE(factoryCalls, 2);
    QCOMPARE(manager.size(), qsizetype{2});
    QCOMPARE(manager.conversationIds(), QList<QUuid>({codex.id, mock.id}));
    QCOMPARE(manager.controller(mock.id)->status(), snack::domain::ConversationStatus::Dormant);
    QVERIFY(manager.close(codex.id));
    QCOMPARE(manager.size(), qsizetype{1});
}

void TestSessionManager::rejectsUnavailableAndConflictingAgentTypes() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    snack::storage::EventStore repository;
    QString error;
    QVERIFY(repository.open(directory.filePath(QStringLiteral("events.sqlite3")), &error));
    snack::app::SessionManager manager(&repository, [](snack::domain::AgentKind requestedKind) {
        auto result = runtime(snack::domain::AgentKind::Mock);
        result.requestedKind = requestedKind;
        result.detail = QStringLiteral("Requested Agent is unavailable");
        result.fellBack = true;
        return result;
    });
    const auto codex = conversation(snack::domain::AgentKind::Codex);
    QVERIFY(manager.open(codex, &error) == nullptr);
    QCOMPARE(error, QStringLiteral("Requested Agent is unavailable"));
    QCOMPARE(manager.size(), qsizetype{0});

    auto mock = conversation(snack::domain::AgentKind::Mock);
    QVERIFY(manager.addPrepared(mock, runtime(mock.agentKind), &error) != nullptr);
    mock.agentKind = snack::domain::AgentKind::Codex;
    QVERIFY(manager.open(mock, &error) == nullptr);
    QCOMPARE(error, QStringLiteral("The open session uses a different Agent type"));
}

void TestSessionManager::validatesBeforeCreatingRuntime() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    snack::storage::EventStore repository;
    QString error;
    QVERIFY(repository.open(directory.filePath(QStringLiteral("events.sqlite3")), &error));
    int factoryCalls = 0;
    snack::app::SessionManager manager(&repository, [&factoryCalls](snack::domain::AgentKind kind) {
        ++factoryCalls;
        return runtime(kind);
    });
    auto invalid = conversation(snack::domain::AgentKind::Mock);
    invalid.id = QUuid{};

    QVERIFY(manager.open(invalid, &error) == nullptr);
    QCOMPARE(factoryCalls, 0);
    QVERIFY(error.contains(QStringLiteral("invalid conversation ID")));
}

void TestSessionManager::createsNewConversationWithExplicitFallback() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    snack::storage::EventStore repository;
    QString error;
    QVERIFY(repository.open(directory.filePath(QStringLiteral("events.sqlite3")), &error));
    snack::app::SessionManager manager(&repository, [](snack::domain::AgentKind requestedKind) {
        auto result = runtime(snack::domain::AgentKind::Mock);
        result.requestedKind = requestedKind;
        result.detail = QStringLiteral("Codex unavailable");
        result.fellBack = true;
        return result;
    });

    auto* created = manager.create(QStringLiteral("C:/workspace"), snack::domain::AgentKind::Codex,
                                   QStringLiteral(" New conversation "), &error);
    QVERIFY2(created != nullptr, qPrintable(error));
    QCOMPARE(created->conversation().title, QStringLiteral("New conversation"));
    QVERIFY(created->conversation().titleIsPlaceholder);
    QCOMPARE(created->conversation().workingDirectory, QStringLiteral("C:/workspace"));
    QCOMPARE(created->conversation().agentKind, snack::domain::AgentKind::Mock);
    QCOMPARE(created->status(), snack::domain::ConversationStatus::Dormant);
    QVERIFY(manager.runtime(created->conversation().id)->fellBack);
}

void TestSessionManager::rejectsInvalidNewConversationRequests() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    snack::storage::EventStore repository;
    QString error;
    QVERIFY(repository.open(directory.filePath(QStringLiteral("events.sqlite3")), &error));
    int factoryCalls = 0;
    snack::app::SessionManager manager(&repository, [&factoryCalls](snack::domain::AgentKind kind) {
        ++factoryCalls;
        auto result = runtime(kind);
        result.adapter.reset();
        result.detail = QStringLiteral("Runtime unavailable");
        return result;
    });

    QVERIFY(manager.create(QStringLiteral("  "), snack::domain::AgentKind::Mock,
                           QStringLiteral("New conversation"), &error) == nullptr);
    QCOMPARE(factoryCalls, 0);
    QVERIFY(error.contains(QStringLiteral("working directory")));
    QVERIFY(manager.create(QStringLiteral("C:/workspace"), snack::domain::AgentKind::Mock,
                           QString{}, &error) == nullptr);
    QCOMPARE(factoryCalls, 1);
    QCOMPARE(error, QStringLiteral("Runtime unavailable"));
}

void TestSessionManager::archivesIdleConversationAndClosesRuntime() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    snack::storage::EventStore repository;
    QString error;
    QVERIFY(repository.open(directory.filePath(QStringLiteral("events.sqlite3")), &error));
    snack::app::SessionManager manager(&repository,
                                       [](snack::domain::AgentKind kind) { return runtime(kind); });
    const auto value = conversation(snack::domain::AgentKind::Mock);
    auto* controller = manager.addPrepared(value, runtime(value.agentKind), &error);
    QVERIFY(controller != nullptr);

    QVERIFY(manager.setArchived(value.id, true, &error));
    QCOMPARE(manager.size(), qsizetype{0});
    const auto stored = repository.conversationById(value.id, &error);
    QVERIFY(stored.has_value());
    QVERIFY(stored->archived);
}

void TestSessionManager::rejectsArchivingActiveConversation() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    snack::storage::EventStore repository;
    QString error;
    QVERIFY(repository.open(directory.filePath(QStringLiteral("events.sqlite3")), &error));
    snack::app::SessionManager manager(&repository,
                                       [](snack::domain::AgentKind kind) { return runtime(kind); });
    auto value = conversation(snack::domain::AgentKind::Mock);
    value.status = snack::domain::ConversationStatus::Running;
    QVERIFY(manager.addPrepared(value, runtime(value.agentKind), &error) != nullptr);

    QVERIFY(!manager.setArchived(value.id, true, &error));
    QVERIFY(error.contains(QStringLiteral("Agent work is active")));
    QCOMPARE(manager.size(), qsizetype{1});
}

void TestSessionManager::restoresArchivedConversationMetadata() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    snack::storage::EventStore repository;
    QString error;
    QVERIFY(repository.open(directory.filePath(QStringLiteral("events.sqlite3")), &error));
    snack::app::SessionManager manager(&repository,
                                       [](snack::domain::AgentKind kind) { return runtime(kind); });
    auto value = conversation(snack::domain::AgentKind::Codex);
    value.archived = true;
    QVERIFY(repository.saveConversation(value, &error));

    auto* restored = manager.restore(value.id, &error);
    QVERIFY2(restored != nullptr, qPrintable(error));
    const auto catalog = manager.catalog(&error);
    QCOMPARE(catalog.size(), 1);
    QCOMPARE(catalog.constFirst().id, value.id);
    QVERIFY(!catalog.constFirst().archived);
    QCOMPARE(manager.size(), qsizetype{1});
}

void TestSessionManager::keepsConversationArchivedWhenRestoreRuntimeIsUnavailable() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    snack::storage::EventStore repository;
    QString error;
    QVERIFY(repository.open(directory.filePath(QStringLiteral("events.sqlite3")), &error));
    snack::app::SessionManager manager(&repository, [](snack::domain::AgentKind requestedKind) {
        auto result = runtime(snack::domain::AgentKind::Mock);
        result.requestedKind = requestedKind;
        result.detail = QStringLiteral("Codex unavailable");
        result.fellBack = true;
        return result;
    });
    auto value = conversation(snack::domain::AgentKind::Codex);
    value.archived = true;
    QVERIFY(repository.saveConversation(value, &error));

    QVERIFY(manager.restore(value.id, &error) == nullptr);
    QCOMPARE(error, QStringLiteral("Codex unavailable"));
    const auto stored = repository.conversationById(value.id, &error);
    QVERIFY(stored.has_value());
    QVERIFY(stored->archived);
    QCOMPARE(manager.size(), qsizetype{0});
}

QTEST_GUILESS_MAIN(TestSessionManager)
#include "TestSessionManager.moc"
