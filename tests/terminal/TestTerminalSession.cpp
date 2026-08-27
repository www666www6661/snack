#include "terminal/NativeTerminalProcess.h"
#include "terminal/TerminalSession.h"

#include <QDir>
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
    void runsNativePseudoTerminal();
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

void TestTerminalSession::runsNativePseudoTerminal() {
#if defined(Q_OS_WIN)
    QCOMPARE(snack::terminal::nativeTerminalBackendName(), QStringLiteral("ConPTY"));
#else
    QCOMPARE(snack::terminal::nativeTerminalBackendName(), QStringLiteral("POSIX PTY"));
#endif
    snack::terminal::TerminalSession session(snack::terminal::createNativeTerminalProcess());
    QSignalSpy output(&session, &snack::terminal::TerminalSession::outputReady);
    QSignalSpy exited(&session, &snack::terminal::TerminalSession::exited);
    QSignalSpy processErrors(&session, &snack::terminal::TerminalSession::processError);
    QString error;
    QVERIFY2(session.start(QDir::tempPath(), 80, 24, &error), qPrintable(error));
#if defined(Q_OS_WIN)
    session.writeInput("echo snack-native\r\nexit\r\n");
#else
    session.writeInput("echo snack-native\nexit\n");
#endif
    QTRY_VERIFY_WITH_TIMEOUT(!output.isEmpty() || !processErrors.isEmpty(), 10000);
    QVERIFY2(processErrors.isEmpty(),
             processErrors.isEmpty()
                 ? ""
                 : qPrintable(processErrors.constFirst().constFirst().toString()));
    QTRY_VERIFY_WITH_TIMEOUT(session.scrollback().contains("snack-native"), 10000);
    QTRY_COMPARE_WITH_TIMEOUT(exited.count(), 1, 10000);
}

QTEST_GUILESS_MAIN(TestTerminalSession)
#include "TestTerminalSession.moc"
