#include "terminal/ITerminalProcess.h"
#include "ui/TerminalPane.h"
#include "ui/TerminalTabs.h"
#include "ui/TerminalView.h"

#include <QMainWindow>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

struct FakeTerminalState {
    QString workingDirectory;
    QPair<int, int> size;
    QByteArray input;
    bool started{false};
    bool closed{false};
};

class FakeUiTerminalProcess final : public snack::terminal::ITerminalProcess {
    Q_OBJECT

  public:
    explicit FakeUiTerminalProcess(std::shared_ptr<FakeTerminalState> state)
        : state_(std::move(state)) {}

    bool start(const QString& workingDirectory, int columns, int rows, QString*) override {
        state_->workingDirectory = workingDirectory;
        state_->size = {columns, rows};
        state_->started = true;
        return true;
    }
    void writeInput(const QByteArray& bytes) override { state_->input.append(bytes); }
    void resizeTerminal(int columns, int rows) override { state_->size = {columns, rows}; }
    void closeTerminal() override { state_->closed = true; }
    void sendOutput(const QByteArray& bytes) { emit outputReady(bytes); }

  private:
    std::shared_ptr<FakeTerminalState> state_;
};

class TestTerminalView final : public QObject {
    Q_OBJECT

  private slots:
    void rendersLiteralPlainText();
    void encodesTextControlAndNavigationKeys();
    void reportsPositiveTerminalSize();
    void managesFakeTabsAndDetachedWindow();
};

void TestTerminalView::rendersLiteralPlainText() {
    snack::ui::TerminalView view;
    const QString hostile = QStringLiteral("<script>alert('terminal')</script> & text");
    view.setScreenText(hostile);
    QCOMPARE(view.toPlainText(), hostile);
    QVERIFY(view.isReadOnly());
    QCOMPARE(view.lineWrapMode(), QPlainTextEdit::NoWrap);
}

void TestTerminalView::encodesTextControlAndNavigationKeys() {
    snack::ui::TerminalView view;
    view.show();
    view.setFocus();
    QSignalSpy input(&view, &snack::ui::TerminalView::inputReady);

    QTest::keyClicks(&view, QStringLiteral("hi"));
    QTest::keyClick(&view, Qt::Key_Return);
    QTest::keyClick(&view, Qt::Key_C, Qt::ControlModifier);
    QTest::keyClick(&view, Qt::Key_Up);

    QCOMPARE(input.count(), 5);
    QCOMPARE(input.at(0).at(0).toByteArray(), QByteArrayLiteral("h"));
    QCOMPARE(input.at(1).at(0).toByteArray(), QByteArrayLiteral("i"));
    QCOMPARE(input.at(2).at(0).toByteArray(), QByteArrayLiteral("\r"));
    QCOMPARE(input.at(3).at(0).toByteArray(), QByteArray(1, '\x03'));
    QCOMPARE(input.at(4).at(0).toByteArray(), QByteArrayLiteral("\x1b[A"));
}

void TestTerminalView::reportsPositiveTerminalSize() {
    snack::ui::TerminalView view;
    QSignalSpy sizes(&view, &snack::ui::TerminalView::terminalSizeChanged);
    view.resize(640, 320);
    view.show();
    QTRY_VERIFY(!sizes.isEmpty());
    const QList<QVariant> latest = sizes.constLast();
    QVERIFY(latest.at(0).toInt() > 0);
    QVERIFY(latest.at(1).toInt() > 0);
}

void TestTerminalView::managesFakeTabsAndDetachedWindow() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QList<std::shared_ptr<FakeTerminalState>> states;
    QList<FakeUiTerminalProcess*> processes;
    const auto factory = [&states, &processes]() {
        auto state = std::make_shared<FakeTerminalState>();
        auto process = std::make_unique<FakeUiTerminalProcess>(state);
        processes.append(process.get());
        states.append(std::move(state));
        return process;
    };
    snack::ui::TerminalTabs tabs(directory.path(), factory);
    tabs.resize(720, 360);
    tabs.show();
    QCOMPARE(tabs.terminalCount(), 0);

    tabs.newTerminal();
    QCOMPARE(tabs.terminalCount(), 1);
    QVERIFY(states.at(0)->started);
    QCOMPARE(states.at(0)->workingDirectory, directory.path());
    auto* pane = tabs.findChild<snack::ui::TerminalPane*>();
    QVERIFY(pane != nullptr);
    processes.at(0)->sendOutput("\x1b[31mterminal text\x1b[0m\r\n");
    QTRY_COMPARE(pane->view()->toPlainText(), QStringLiteral("terminal text\n"));
    QTest::keyClicks(pane->view(), QStringLiteral("echo"));
    QCOMPARE(states.at(0)->input, QByteArrayLiteral("echo"));

    tabs.newTerminal();
    QCOMPARE(tabs.terminalCount(), 2);
    QVERIFY(states.at(1)->started);
    tabs.closeCurrentTerminal();
    QCOMPARE(tabs.terminalCount(), 1);
    QVERIFY(states.at(1)->closed);

    QSignalSpy detached(&tabs, &snack::ui::TerminalTabs::terminalDetached);
    tabs.detachCurrentTerminal();
    QCOMPARE(tabs.terminalCount(), 0);
    QCOMPARE(detached.count(), 1);
    auto* detachedWindow = qvariant_cast<QMainWindow*>(detached.constFirst().constFirst());
    QVERIFY(detachedWindow != nullptr);
    QCOMPARE(detachedWindow->centralWidget(), pane);
    detachedWindow->close();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QVERIFY(states.at(0)->closed);
}

QTEST_MAIN(TestTerminalView)
#include "TestTerminalView.moc"
