#include "app/ConversationExporter.h"

#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QTest>

class TestConversationExporter final : public QObject {
    Q_OBJECT

  private slots:
    void rendersMarkdownAndJson();
    void writesAtomicallyAndRejectsInvalidPaths();
};

void TestConversationExporter::rendersMarkdownAndJson() {
    snack::domain::Conversation conversation;
    conversation.title = QStringLiteral("Exported conversation");
    conversation.workingDirectory = QStringLiteral("C:/workspace");
    conversation.agentKind = snack::domain::AgentKind::Codex;
    conversation.modelId = QStringLiteral("gpt-test");
    snack::domain::AgentEvent user;
    user.conversationId = conversation.id;
    user.sequence = 1;
    user.type = snack::domain::AgentEventType::UserMessage;
    user.payload = {{QStringLiteral("text"), QStringLiteral("Explain the parser")}};
    snack::domain::AgentEvent start = user;
    start.id = QUuid::createUuid();
    start.sequence = 2;
    start.type = snack::domain::AgentEventType::AgentMessageStart;
    start.payload = {};
    snack::domain::AgentEvent delta = start;
    delta.id = QUuid::createUuid();
    delta.sequence = 3;
    delta.type = snack::domain::AgentEventType::AgentMessageDelta;
    delta.payload = {{QStringLiteral("text"), QStringLiteral("The parser streams text.")}};
    delta.rawPayload = {{QStringLiteral("private"), QStringLiteral("explicit JSON export")}};

    const auto markdown = snack::app::ConversationExporter::render(
        conversation, {user, start, delta}, snack::app::ConversationExportFormat::Markdown);
    QVERIFY(markdown.contains("# Exported conversation"));
    QVERIFY(markdown.contains("## You\n\nExplain the parser"));
    QVERIFY(markdown.contains("## Agent\n\nThe parser streams text."));
    QVERIFY(!markdown.contains("explicit JSON export"));

    const auto json = snack::app::ConversationExporter::render(
        conversation, {user, start, delta}, snack::app::ConversationExportFormat::Json);
    const auto document = QJsonDocument::fromJson(json);
    QVERIFY(document.isObject());
    QCOMPARE(document.object().value(QStringLiteral("formatVersion")).toInt(), 1);
    QCOMPARE(document.object()
                 .value(QStringLiteral("conversation"))
                 .toObject()
                 .value(QStringLiteral("agent"))
                 .toString(),
             QStringLiteral("codex"));
    QCOMPARE(document.object().value(QStringLiteral("events")).toArray().size(), 3);
    QCOMPARE(document.object()
                 .value(QStringLiteral("events"))
                 .toArray()
                 .at(2)
                 .toObject()
                 .value(QStringLiteral("rawPayload"))
                 .toObject()
                 .value(QStringLiteral("private"))
                 .toString(),
             QStringLiteral("explicit JSON export"));
}

void TestConversationExporter::writesAtomicallyAndRejectsInvalidPaths() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    snack::domain::Conversation conversation;
    conversation.title = QStringLiteral("Written export");
    QString error;
    const QString path = directory.filePath(QStringLiteral("conversation.md"));
    QVERIFY(snack::app::ConversationExporter::write(
        path, conversation, {}, snack::app::ConversationExportFormat::Markdown, &error));
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QVERIFY(file.readAll().startsWith("# Written export"));
    QVERIFY(!snack::app::ConversationExporter::write(
        {}, conversation, {}, snack::app::ConversationExportFormat::Json, &error));
    QCOMPARE(error, QStringLiteral("Export path is empty"));
    QVERIFY(!snack::app::ConversationExporter::write(
        directory.path(), conversation, {}, snack::app::ConversationExportFormat::Json, &error));
    QVERIFY(!error.isEmpty());
}

QTEST_GUILESS_MAIN(TestConversationExporter)
#include "TestConversationExporter.moc"
