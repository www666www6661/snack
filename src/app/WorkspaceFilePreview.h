#pragma once

#include <QString>

namespace snack::app {

struct WorkspaceFilePreview {
    QString text;
    bool truncated{false};
};

class WorkspaceFilePreviewReader final {
  public:
    [[nodiscard]] static WorkspaceFilePreview read(const QString& workspace,
                                                   const QString& relativePath,
                                                   qsizetype maximumBytes = 512 * 1024,
                                                   QString* error = nullptr);
};

} // namespace snack::app
