#include "terminal/TerminalSession.h"

#include <QSignalSpy>
#include <QTest>

class FakeTerminalProcess final : public snack::terminal::ITerminalProcess {
    Q_OBJECT

  public:
    bool start(const QString& workingDirectory, int columns, int rows, QString*) override {
        cwd = workingDirectory;
        size = {columns, rows};
        started = true;
        return true;
    }
    void writeInput(const QByteArray& bytes) override { input.append(bytes); }
    void resizeTerminal(int columns, int rows) override { size = {columns, rows}; }
    void closeTerminal() override { closed = true; }
    void sendOutput(const QByteArray& bytes) { emit outputReady(bytes); }

    QString cwd;
    QPair<int, int> size;
    QByteArray input;
    bool started{false};
    bool closed{false};
};

class TestTerminalSession final : public QObject {
    Q_OBJECT

  private slots:
    void delegatesToFakeWithoutAgentEvents();
    void boundsScrollback();
};

void TestTerminalSession::delegatesToFakeWithoutAgentEvents() {
    auto process = std::make_unique<FakeTerminalProcess>();
    auto* fake = process.get();
    snack::terminal::TerminalSession session(std::move(process));
    QSignalSpy output(&session, &snack::terminal::TerminalSession::outputReady);
    QString error;
    QVERIFY(session.start(QStringLiteral("C:/workspace"), 80, 24, &error));
    QCOMPARE(fake->cwd, QStringLiteral("C:/workspace"));
    QCOMPARE(fake->size, qMakePair(80, 24));
    session.writeInput("echo test\r");
    QCOMPARE(fake->input, QByteArray("echo test\r"));
    fake->sendOutput("result\r\n");
    QCOMPARE(output.count(), 1);
    QCOMPARE(session.scrollback(), QByteArray("result\r\n"));
    session.resizeTerminal(120, 40);
    QCOMPARE(fake->size, qMakePair(120, 40));
    session.close();
    QVERIFY(fake->closed);
}

void TestTerminalSession::boundsScrollback() {
    auto process = std::make_unique<FakeTerminalProcess>();
    auto* fake = process.get();
    snack::terminal::TerminalSession session(std::move(process));
    fake->sendOutput(QByteArray(5 * 1024 * 1024, 'x'));
    QCOMPARE(session.scrollback().size(), 4 * 1024 * 1024);
}

QTEST_APPLESS_MAIN(TestTerminalSession)
#include "TestTerminalSession.moc"
