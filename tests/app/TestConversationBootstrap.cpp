#include "app/ConversationBootstrap.h"

#include <QTest>

class TestConversationBootstrap final : public QObject {
    Q_OBJECT

  private slots:
    void restoresMatchingConversation();
    void isolatesDifferentAgents();
    void isolatesDifferentWorkspaces();
};

static snack::domain::Conversation storedConversation() {
    snack::domain::Conversation conversation;
    conversation.title = QStringLiteral("Stored");
    conversation.workingDirectory = QStringLiteral("C:/workspace");
    conversation.agentKind = snack::domain::AgentKind::Codex;
    conversation.status = snack::domain::ConversationStatus::Running;
    conversation.nativeThreadId = QStringLiteral("thread-existing");
    return conversation;
}

void TestConversationBootstrap::restoresMatchingConversation() {
    const auto stored = storedConversation();
    const auto result = snack::app::prepareConversation(stored, QStringLiteral("C:/workspace"),
                                                        snack::domain::AgentKind::Codex,
                                                        QStringLiteral("New conversation"));

    QVERIFY(result.restored);
    QCOMPARE(result.conversation.id, stored.id);
    QCOMPARE(result.conversation.nativeThreadId, QStringLiteral("thread-existing"));
    QCOMPARE(result.conversation.status, snack::domain::ConversationStatus::Dormant);
    QVERIFY(!result.conversation.titleIsPlaceholder);
}

void TestConversationBootstrap::isolatesDifferentAgents() {
    const auto stored = storedConversation();
    const auto result = snack::app::prepareConversation(stored, QStringLiteral("C:/workspace"),
                                                        snack::domain::AgentKind::Mock,
                                                        QStringLiteral("Mock conversation"));

    QVERIFY(!result.restored);
    QVERIFY(result.conversation.id != stored.id);
    QCOMPARE(result.conversation.agentKind, snack::domain::AgentKind::Mock);
    QVERIFY(result.conversation.nativeThreadId.isEmpty());
    QVERIFY(result.conversation.titleIsPlaceholder);
}

void TestConversationBootstrap::isolatesDifferentWorkspaces() {
    const auto stored = storedConversation();
    const auto result = snack::app::prepareConversation(stored, QStringLiteral("C:/other"),
                                                        snack::domain::AgentKind::Codex,
                                                        QStringLiteral("Codex conversation"));

    QVERIFY(!result.restored);
    QCOMPARE(result.conversation.workingDirectory, QStringLiteral("C:/other"));
    QCOMPARE(result.conversation.title, QStringLiteral("Codex conversation"));
    QVERIFY(result.conversation.titleIsPlaceholder);
}

QTEST_GUILESS_MAIN(TestConversationBootstrap)
#include "TestConversationBootstrap.moc"
