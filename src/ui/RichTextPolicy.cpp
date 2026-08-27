#include "ui/RichTextPolicy.h"

#include <QDir>

namespace snack::ui {

bool RichTextPolicy::allowsPackagedResource(const QUrl& url) {
    if (!url.isValid() || url.scheme() != QLatin1String("qrc") || !url.host().isEmpty() ||
        !url.userInfo().isEmpty() || url.hasQuery() || url.hasFragment()) {
        return false;
    }
    const QString path = url.path();
    if (QDir::cleanPath(path) != path) {
        return false;
    }
    return path.startsWith(QLatin1String("/renderer/")) ||
           path == QLatin1String("/qtwebchannel/qwebchannel.js");
}

bool RichTextPolicy::allowsExternalLink(const QUrl& url) {
    if (!url.isValid() || (url.hasFragment() && url.scheme().isEmpty()) ||
        !url.userInfo().isEmpty()) {
        return false;
    }
    const QString scheme = url.scheme().toCaseFolded();
    if (scheme == QLatin1String("http") || scheme == QLatin1String("https")) {
        return !url.host().isEmpty();
    }
    return scheme == QLatin1String("mailto") && !url.path().trimmed().isEmpty();
}

} // namespace snack::ui
