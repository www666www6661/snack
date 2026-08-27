#pragma once

#include <QStringList>

namespace snack::app {

class WorkspaceFileIndex final {
  public:
    [[nodiscard]] static QStringList files(const QString& workspace, qsizetype limit = 2000);
};

} // namespace snack::app
