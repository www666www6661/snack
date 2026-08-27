#pragma once

#include <QUrl>

namespace snack::ui {

class RichTextPolicy {
  public:
    [[nodiscard]] static bool allowsPackagedResource(const QUrl& url);
    [[nodiscard]] static bool allowsExternalLink(const QUrl& url);
};

} // namespace snack::ui
