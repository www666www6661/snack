#include "ui/RichTextView.h"

#include "ui/RichTextPolicy.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

#if SNACK_HAS_WEBENGINE
#include <QWebChannel>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QWebEngineSettings>
#include <QWebEngineUrlRequestInfo>
#include <QWebEngineUrlRequestInterceptor>
#include <QWebEngineView>
#endif

namespace snack::ui {

namespace {

constexpr qsizetype maximumRenderedAgentMessage = 256 * 1024;

QString boundedMessageText(const QString& text) {
    if (text.size() <= maximumRenderedAgentMessage) {
        return text;
    }
    return QStringLiteral("[earlier output hidden]\n") +
           text.sliced(text.size() - maximumRenderedAgentMessage);
}

} // namespace

#if SNACK_HAS_WEBENGINE
class RendererBridge final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString documentJson READ documentJson NOTIFY documentChanged)
    Q_PROPERTY(QString themeJson READ themeJson NOTIFY themeChanged)

  public:
    using QObject::QObject;

    [[nodiscard]] QString documentJson() const { return documentJson_; }
    [[nodiscard]] QString themeJson() const { return themeJson_; }

    void setDocumentJson(QString value) {
        documentJson_ = std::move(value);
        emit documentChanged();
    }

    void setThemeJson(QString value) {
        themeJson_ = std::move(value);
        emit themeChanged();
    }

    Q_INVOKABLE void requestExternalLink(const QString& value) {
        const QUrl url(value, QUrl::StrictMode);
        if (RichTextPolicy::allowsExternalLink(url)) {
            emit externalLinkRequested(url);
        }
    }

  signals:
    void documentChanged();
    void themeChanged();
    void externalLinkRequested(const QUrl& url);

  private:
    QString documentJson_{QStringLiteral("[]")};
    QString themeJson_{QStringLiteral("{}")};
};

class PackagedResourceInterceptor final : public QWebEngineUrlRequestInterceptor {
    Q_OBJECT

  public:
    using QWebEngineUrlRequestInterceptor::QWebEngineUrlRequestInterceptor;

    void interceptRequest(QWebEngineUrlRequestInfo& info) override {
        if (!RichTextPolicy::allowsPackagedResource(info.requestUrl())) {
            const QUrl blockedUrl = info.requestUrl();
            info.block(true);
            emit resourceBlocked(blockedUrl);
        }
    }

  signals:
    void resourceBlocked(const QUrl& url);
};

class SafeWebEnginePage final : public QWebEnginePage {
    Q_OBJECT

  public:
    explicit SafeWebEnginePage(QWebEngineProfile* profile, QObject* parent)
        : QWebEnginePage(profile, parent) {}

  protected:
    bool acceptNavigationRequest(const QUrl& url, NavigationType, bool) override {
        return RichTextPolicy::allowsPackagedResource(url);
    }
};
#endif

RichTextView::RichTextView(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("richTextView"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

#if SNACK_HAS_WEBENGINE
    if (qEnvironmentVariableIsSet("SNACK_DISABLE_WEBENGINE")) {
        auto* unavailableLabel =
            new QLabel(tr("Rich rendering is disabled for this process."), this);
        unavailableLabel->setObjectName(QStringLiteral("richRenderingDisabled"));
        layout->addWidget(unavailableLabel);
        hide();
        return;
    }

    profile_ = new QWebEngineProfile(this);
    profile_->setHttpCacheType(QWebEngineProfile::NoCache);
    profile_->setPersistentCookiesPolicy(QWebEngineProfile::NoPersistentCookies);
    auto* interceptor = new PackagedResourceInterceptor(profile_);
    profile_->setUrlRequestInterceptor(interceptor);

    page_ = new SafeWebEnginePage(profile_, this);
    page_->settings()->setAttribute(QWebEngineSettings::JavascriptCanOpenWindows, false);
    page_->settings()->setAttribute(QWebEngineSettings::LocalContentCanAccessFileUrls, false);
    page_->settings()->setAttribute(QWebEngineSettings::LocalContentCanAccessRemoteUrls, false);
    page_->settings()->setAttribute(QWebEngineSettings::PluginsEnabled, false);
    page_->settings()->setAttribute(QWebEngineSettings::PdfViewerEnabled, false);

    bridge_ = new RendererBridge(this);
    auto* channel = new QWebChannel(page_);
    channel->registerObject(QStringLiteral("snackRenderer"), bridge_);
    page_->setWebChannel(channel);

    webView_ = new QWebEngineView(this);
    webView_->setObjectName(QStringLiteral("conversationWebView"));
    webView_->setPage(page_);
    webView_->setContextMenuPolicy(Qt::NoContextMenu);
    layout->addWidget(webView_);

    connect(bridge_, &RendererBridge::externalLinkRequested, this,
            [this](const QUrl& url) { emit externalLinkRequested(url); });
    connect(interceptor, &PackagedResourceInterceptor::resourceBlocked, this,
            &RichTextView::remoteResourceBlocked);
    connect(page_, &QWebEnginePage::renderProcessTerminated, this,
            [this](QWebEnginePage::RenderProcessTerminationStatus, int) {
                QTimer::singleShot(0, this, [this] {
                    recoveryPending_ = true;
                    page_->load(QUrl(QStringLiteral("qrc:/renderer/index.html")));
                });
            });
    connect(page_, &QWebEnginePage::loadFinished, this, [this](bool succeeded) {
        if (!recoveryPending_ || !succeeded) {
            return;
        }
        recoveryPending_ = false;
        publishDocument();
        emit rendererRecovered();
    });
    page_->load(QUrl(QStringLiteral("qrc:/renderer/index.html")));
#else
    unavailableLabel_ = new QLabel(tr("Rich rendering is unavailable in this Qt build."), this);
    unavailableLabel_->setObjectName(QStringLiteral("richRenderingUnavailable"));
    unavailableLabel_->setWordWrap(true);
    layout->addWidget(unavailableLabel_);
    hide();
#endif
}

RichTextView::~RichTextView() {
#if SNACK_HAS_WEBENGINE
    // QObject deletes children in insertion order; WebEngine requires the profile to outlive its
    // page.
    delete webView_;
    webView_ = nullptr;
    delete page_;
    page_ = nullptr;
    delete profile_;
    profile_ = nullptr;
#endif
}

bool RichTextView::rendererAvailable() const {
#if SNACK_HAS_WEBENGINE
    return webView_ != nullptr;
#else
    return false;
#endif
}

void RichTextView::resetDocument() {
    messages_ = {};
    activeAgentMessage_ = -1;
    publishDocument();
}

void RichTextView::appendUserMessage(const QString& text) {
    messages_.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("user")},
                                 {QStringLiteral("author"), tr("You")},
                                 {QStringLiteral("text"), text}});
    activeAgentMessage_ = -1;
    publishDocument();
}

void RichTextView::startAgentMessage(const QString& agentName) {
    messages_.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("agent")},
                                 {QStringLiteral("agent"), agentName},
                                 {QStringLiteral("author"), agentName},
                                 {QStringLiteral("text"), QString{}}});
    activeAgentMessage_ = static_cast<int>(messages_.size() - 1);
    publishDocument();
}

void RichTextView::appendAgentDelta(const QString& text) {
    if (activeAgentMessage_ < 0 || activeAgentMessage_ >= messages_.size() || text.isEmpty()) {
        return;
    }
    QJsonObject message = messages_.at(activeAgentMessage_).toObject();
    message.insert(QStringLiteral("text"),
                   boundedMessageText(message.value(QStringLiteral("text")).toString() + text));
    messages_[activeAgentMessage_] = message;
    publishDocument();
}

void RichTextView::applyInterfaceScale(double scale) {
#if SNACK_HAS_WEBENGINE
    if (webView_ != nullptr) {
        webView_->setZoomFactor(std::clamp(scale, 0.8, 2.0));
    }
#else
    Q_UNUSED(scale)
#endif
}

void RichTextView::applyTheme(const ThemeDefinition& theme) {
#if SNACK_HAS_WEBENGINE
    if (bridge_ == nullptr) {
        return;
    }
    const auto colorName = [&theme](const QString& token) {
        return theme.colors.value(token).name(QColor::HexRgb);
    };
    const QJsonObject colors{
        {QStringLiteral("canvas"), colorName(QStringLiteral("surface.canvas"))},
        {QStringLiteral("raised"), colorName(QStringLiteral("surface.raised"))},
        {QStringLiteral("text"), colorName(QStringLiteral("text.primary"))},
        {QStringLiteral("secondary"), colorName(QStringLiteral("text.secondary"))},
        {QStringLiteral("link"), colorName(QStringLiteral("text.link"))},
        {QStringLiteral("border"), colorName(QStringLiteral("border.subtle"))},
        {QStringLiteral("user"), colorName(QStringLiteral("message.user"))}};
    bridge_->setThemeJson(QString::fromUtf8(QJsonDocument(colors).toJson(QJsonDocument::Compact)));
#else
    Q_UNUSED(theme)
#endif
}

void RichTextView::publishDocument() {
#if SNACK_HAS_WEBENGINE
    if (bridge_ != nullptr) {
        bridge_->setDocumentJson(
            QString::fromUtf8(QJsonDocument(messages_).toJson(QJsonDocument::Compact)));
    }
#endif
}

} // namespace snack::ui

#if SNACK_HAS_WEBENGINE
#include "RichTextView.moc"
#endif
