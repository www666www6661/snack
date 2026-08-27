#include "ui/TerminalView.h"

#include <QSignalSpy>
#include <QTest>

class TestTerminalView final : public QObject {
    Q_OBJECT

  private slots:
    void rendersLiteralPlainText();
    void encodesTextControlAndNavigationKeys();
    void reportsPositiveTerminalSize();
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

QTEST_MAIN(TestTerminalView)
#include "TestTerminalView.moc"
