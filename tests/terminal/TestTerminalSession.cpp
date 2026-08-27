#include "terminal/NativeTerminalProcess.h"
#include "terminal/TerminalOutputDecoder.h"
#include "terminal/TerminalSession.h"
#include "terminal/TerminalTextBuffer.h"

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
    void finish(int exitCode) { emit exited(exitCode); }
    void fail(const QString& message) { emit processError(message); }

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
    void decodesSplitUtf8AndAnsiSafely();
    void projectsCarriageReturnsAndBoundsText();
    void validatesDimensionsAndForwardsProcessState();
    void runsNativePseudoTerminal();
};

void TestTerminalSession::delegatesToFakeWithoutAgentEvents() {
    auto process = std::make_unique<FakeTerminalProcess>();
    auto* fake = process.get();
    snack::terminal::TerminalSession session(std::move(process));
    QSignalSpy output(&session, &snack::terminal::TerminalSession::outputReady);
    QSignalSpy screen(&session, &snack::terminal::TerminalSession::screenChanged);
    QString error;
    QVERIFY(session.start(QStringLiteral("C:/workspace"), 80, 24, &error));
    QCOMPARE(fake->cwd, QStringLiteral("C:/workspace"));
    QCOMPARE(fake->size, qMakePair(80, 24));
    session.writeInput("echo test\r");
    QCOMPARE(fake->input, QByteArray("echo test\r"));
    fake->sendOutput("result\r\n");
    QCOMPARE(output.count(), 1);
    QCOMPARE(session.scrollback(), QByteArray("result\r\n"));
    QCOMPARE(screen.count(), 1);
    QCOMPARE(session.screenText(), QStringLiteral("result\n"));
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

void TestTerminalSession::decodesSplitUtf8AndAnsiSafely() {
    snack::terminal::TerminalOutputDecoder decoder;
    QCOMPARE(decoder.decode(QByteArrayView("\x1b[3")), QString());
    QCOMPARE(decoder.decode(QByteArrayView("1mred\x1b[0m ")), QStringLiteral("red "));

    const QByteArray unicode = QStringLiteral("鲸鱼").toUtf8();
    QCOMPARE(decoder.decode(QByteArrayView(unicode.constData(), 2)), QString());
    QCOMPARE(decoder.decode(QByteArrayView(unicode.constData() + 2, unicode.size() - 2)),
             QStringLiteral("鲸鱼"));

    QCOMPARE(decoder.decode(QByteArrayView("\x1b]0;private")), QString());
    QCOMPARE(decoder.decode(QByteArrayView(" title\x07visible\x00\x7f")),
             QStringLiteral("visible"));
    QCOMPARE(decoder.decode(QByteArrayView("\x1bPprivate\x1b\\safe")), QStringLiteral("safe"));
    QVERIFY(!decoder.hasEncodingError());
}

void TestTerminalSession::projectsCarriageReturnsAndBoundsText() {
    snack::terminal::TerminalTextBuffer buffer;
    buffer.append(QStringLiteral("progress 10%\rprogress 20%\r\nok\b!\n"));
    QCOMPARE(buffer.text(), QStringLiteral("progress 20%\no!\n"));
    buffer.append(QStringLiteral("a\tb"));
    QCOMPARE(buffer.text(), QStringLiteral("progress 20%\no!\na       b"));
    buffer.clear();
    QCOMPARE(buffer.text(), QString());

    snack::terminal::TerminalTextBuffer bounded(6);
    bounded.append(QStringLiteral("1234\n5678"));
    QCOMPARE(bounded.text(), QStringLiteral("4\n5678"));
}

void TestTerminalSession::validatesDimensionsAndForwardsProcessState() {
    auto process = std::make_unique<FakeTerminalProcess>();
    auto* fake = process.get();
    snack::terminal::TerminalSession session(std::move(process));
    QSignalSpy exited(&session, &snack::terminal::TerminalSession::exited);
    QSignalSpy errors(&session, &snack::terminal::TerminalSession::processError);
    QString error;
    QVERIFY(!session.start(QStringLiteral("C:/workspace"), 0, 24, &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(!fake->started);
    QVERIFY(!session.start(QStringLiteral("C:/workspace"), 32768, 24, &error));
    QVERIFY(!fake->started);
    QVERIFY(session.start(QStringLiteral("C:/workspace"), 80, 24, &error));
    session.resizeTerminal(0, 40);
    QCOMPARE(fake->size, qMakePair(80, 24));
    fake->finish(7);
    fake->fail(QStringLiteral("terminal failure"));
    QCOMPARE(exited.count(), 1);
    QCOMPARE(exited.constFirst().constFirst().toInt(), 7);
    QCOMPARE(errors.count(), 1);
    QCOMPARE(errors.constFirst().constFirst().toString(), QStringLiteral("terminal failure"));
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
    session.resizeTerminal(132, 43);
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
