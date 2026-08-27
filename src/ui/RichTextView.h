#pragma once

#include "ui/ThemeDefinition.h"

#include <QJsonArray>
#include <QWidget>

class QLabel;
#if SNACK_HAS_WEBENGINE
class QWebEngineView;
class QWebEngineProfile;
#endif

namespace snack::ui {

#if SNACK_HAS_WEBENGINE
class RendererBridge;
class SafeWebEnginePage;
#endif

class RichTextView final : public QWidget {
    Q_OBJECT

  public:
    explicit RichTextView(QWidget* parent = nullptr);
    ~RichTextView() override;

    [[nodiscard]] bool rendererAvailable() const;
    void resetDocument();
    void appendUserMessage(const QString& text);
    void startAgentMessage(const QString& agentName);
    void appendAgentDelta(const QString& text);
    void applyTheme(const ThemeDefinition& theme);
    void applyInterfaceScale(double scale);

  signals:
    void externalLinkRequested(const QUrl& url);
    void remoteResourceBlocked(const QUrl& url);
    void rendererRecovered();

  private:
    void publishDocument();

    QJsonArray messages_;
    int activeAgentMessage_{-1};
#if SNACK_HAS_WEBENGINE
    RendererBridge* bridge_{nullptr};
    QWebEngineProfile* profile_{nullptr};
    SafeWebEnginePage* page_{nullptr};
    QWebEngineView* webView_{nullptr};
    bool recoveryPending_{false};
#else
    QLabel* unavailableLabel_{nullptr};
#endif
};

} // namespace snack::ui
