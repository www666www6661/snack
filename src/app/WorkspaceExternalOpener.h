#pragma once

#include <QUrl>

#include <functional>

namespace snack::app {

class WorkspaceExternalOpener final {
  public:
    using OpenUrl = std::function<bool(const QUrl&)>;

    [[nodiscard]] static bool open(const QString& workspace, const QString& relativePath,
                                   const OpenUrl& openUrl, QString* error = nullptr);
};

} // namespace snack::app
