#include "ui/RichTextPolicy.h"

#include <QTest>

class TestRichTextPolicy final : public QObject {
    Q_OBJECT

  private slots:
    void allowsOnlyPackagedRendererResources();
    void allowsOnlyExplicitExternalLinks();
};

void TestRichTextPolicy::allowsOnlyPackagedRendererResources() {
    using snack::ui::RichTextPolicy;

    QVERIFY(
        RichTextPolicy::allowsPackagedResource(QUrl(QStringLiteral("qrc:/renderer/index.html"))));
    QVERIFY(RichTextPolicy::allowsPackagedResource(
        QUrl(QStringLiteral("qrc:/qtwebchannel/qwebchannel.js"))));
    QVERIFY(!RichTextPolicy::allowsPackagedResource(QUrl(QStringLiteral("https://example.com/a"))));
    QVERIFY(!RichTextPolicy::allowsPackagedResource(QUrl(QStringLiteral("file:///tmp/a"))));
    QVERIFY(!RichTextPolicy::allowsPackagedResource(
        QUrl(QStringLiteral("qrc://host/renderer/index.html"))));
    QVERIFY(!RichTextPolicy::allowsPackagedResource(
        QUrl(QStringLiteral("qrc:/renderer/../secret.txt"))));
    QVERIFY(!RichTextPolicy::allowsPackagedResource(
        QUrl(QStringLiteral("qrc:/renderer/index.html?remote=1"))));
}

void TestRichTextPolicy::allowsOnlyExplicitExternalLinks() {
    using snack::ui::RichTextPolicy;

    QVERIFY(RichTextPolicy::allowsExternalLink(QUrl(QStringLiteral("https://example.com/docs"))));
    QVERIFY(RichTextPolicy::allowsExternalLink(QUrl(QStringLiteral("http://localhost:8080"))));
    QVERIFY(RichTextPolicy::allowsExternalLink(QUrl(QStringLiteral("mailto:dev@example.com"))));
    QVERIFY(!RichTextPolicy::allowsExternalLink(QUrl(QStringLiteral("https:///missing-host"))));
    QVERIFY(!RichTextPolicy::allowsExternalLink(
        QUrl(QStringLiteral("https://user:secret@example.com"))));
    QVERIFY(!RichTextPolicy::allowsExternalLink(QUrl(QStringLiteral("javascript:alert(1)"))));
    QVERIFY(!RichTextPolicy::allowsExternalLink(QUrl(QStringLiteral("data:text/html,unsafe"))));
    QVERIFY(!RichTextPolicy::allowsExternalLink(QUrl(QStringLiteral("file:///tmp/secret"))));
    QVERIFY(!RichTextPolicy::allowsExternalLink(QUrl(QStringLiteral("relative/path"))));
}

QTEST_APPLESS_MAIN(TestRichTextPolicy)
#include "TestRichTextPolicy.moc"
