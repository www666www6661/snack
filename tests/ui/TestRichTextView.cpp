#include "ui/RichTextView.h"

#include <QEventLoop>
#include <QSignalSpy>
#include <QTest>
#include <QTimer>
#include <QWebEnginePage>
#include <QWebEngineView>

class TestRichTextView final : public QObject {
    Q_OBJECT

  private slots:
    void rendersSupportedMarkdownAndRejectsHostileContent();
    void validatesExternalLinksInCpp();
    void reappliesDocumentAfterReload();
};

namespace {

QVariant evaluate(QWebEnginePage* page, const QString& script) {
    QVariant result;
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    page->runJavaScript(script, [&result, &loop](const QVariant& value) {
        result = value;
        loop.quit();
    });
    timeout.start(10000);
    loop.exec();
    return result;
}

QWebEnginePage* rendererPage(snack::ui::RichTextView& view) {
    auto* webView = view.findChild<QWebEngineView*>(QStringLiteral("conversationWebView"));
    return webView != nullptr ? webView->page() : nullptr;
}

} // namespace

void TestRichTextView::rendersSupportedMarkdownAndRejectsHostileContent() {
    snack::ui::RichTextView view;
    QVERIFY(view.rendererAvailable());
    view.resize(900, 700);
    view.show();
    QWebEnginePage* page = rendererPage(view);
    QVERIFY(page != nullptr);
    QTRY_COMPARE_WITH_TIMEOUT(page->url(), QUrl(QStringLiteral("qrc:/renderer/index.html")), 10000);
    QTRY_VERIFY_WITH_TIMEOUT(
        evaluate(page, QStringLiteral("typeof window.snackBridge === 'object'")).toBool(), 10000);

    view.appendUserMessage(QStringLiteral(
        "<script>window.unsafe = true</script><img src=https://example.com/a onerror=alert(1)>"
        "<span style=color:red onclick=alert(1)>unsafe</span>"));
    view.startAgentMessage(QStringLiteral("Mock Agent"));
    view.appendAgentDelta(
        QStringLiteral("| A | B |\n|---|---|\n| 1 | 2 |\n\n- [x] done\n\n`code`\n\n"
                       "$$x^2$$\n\n```mermaid\ngraph TD; A-->B\n```\n\n"
                       "[safe](https://example.com/docs) [unsafe](javascript:alert(1))"));

    QTRY_VERIFY_WITH_TIMEOUT(
        evaluate(page, QStringLiteral("document.querySelectorAll('#conversation .message').length"))
                .toInt() == 2,
        10000);
    QVERIFY(evaluate(page, QStringLiteral("document.querySelector('#conversation table') !== null"))
                .toBool());
    QVERIFY(evaluate(page, QStringLiteral("document.querySelector('#conversation code') !== null"))
                .toBool());
    QVERIFY(
        evaluate(page, QStringLiteral("document.querySelector('#conversation .katex') !== null"))
            .toBool());
    QTRY_VERIFY_WITH_TIMEOUT(
        evaluate(page,
                 QStringLiteral("document.querySelector('#conversation .mermaid svg') !== null"))
            .toBool(),
        10000);
    QCOMPARE(evaluate(page, QStringLiteral("document.querySelectorAll('#conversation script, "
                                           "#conversation iframe, #conversation img, "
                                           "#conversation [onerror], #conversation [onclick], "
                                           "#conversation .message.user [style]').length"))
                 .toInt(),
             0);
    QVERIFY(!evaluate(page, QStringLiteral("Boolean(window.unsafe)")).toBool());
}

void TestRichTextView::validatesExternalLinksInCpp() {
    snack::ui::RichTextView view;
    view.show();
    QWebEnginePage* page = rendererPage(view);
    QVERIFY(page != nullptr);
    QTRY_VERIFY_WITH_TIMEOUT(
        evaluate(page, QStringLiteral("typeof window.snackBridge === 'object'")).toBool(), 10000);
    QSignalSpy externalLinks(&view, &snack::ui::RichTextView::externalLinkRequested);

    evaluate(page, QStringLiteral("window.snackBridge.requestExternalLink('javascript:alert(1)')"));
    evaluate(page, QStringLiteral("window.snackBridge.requestExternalLink('file:///tmp/secret')"));
    QTest::qWait(50);
    QCOMPARE(externalLinks.count(), 0);

    evaluate(page,
             QStringLiteral("window.snackBridge.requestExternalLink('https://example.com/docs')"));
    QTRY_COMPARE_WITH_TIMEOUT(externalLinks.count(), 1, 5000);
    QCOMPARE(externalLinks.at(0).at(0).toUrl(), QUrl(QStringLiteral("https://example.com/docs")));
}

void TestRichTextView::reappliesDocumentAfterReload() {
    snack::ui::RichTextView view;
    view.startAgentMessage(QStringLiteral("Mock Agent"));
    view.appendAgentDelta(QStringLiteral("persistent marker"));
    view.applyTheme(snack::ui::ThemeDefinition::dark());
    view.applyInterfaceScale(1.25);
    view.show();
    QWebEnginePage* page = rendererPage(view);
    QVERIFY(page != nullptr);
    QTRY_VERIFY_WITH_TIMEOUT(
        evaluate(page, QStringLiteral("typeof window.snackBridge === 'object'")).toBool(), 10000);
    auto* webView = view.findChild<QWebEngineView*>();
    QCOMPARE(webView->zoomFactor(), 1.25);

    page->triggerAction(QWebEnginePage::Reload);
    QTRY_VERIFY_WITH_TIMEOUT(
        evaluate(page, QStringLiteral("typeof window.snackBridge === 'object'")).toBool(), 10000);
    QTRY_VERIFY_WITH_TIMEOUT(
        evaluate(page, QStringLiteral("document.body !== null && "
                                      "document.body.innerText.includes('persistent marker')"))
            .toBool(),
        10000);
    QCOMPARE(
        evaluate(
            page,
            QStringLiteral(
                "getComputedStyle(document.documentElement).getPropertyValue('--canvas').trim()"))
            .toString(),
        QStringLiteral("#171917"));
}

QTEST_MAIN(TestRichTextView)
#include "TestRichTextView.moc"
