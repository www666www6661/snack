#pragma once

#include "workspace/WorkspaceSnapshot.h"

namespace snack::workspace {

class WorkspaceChangeReview final {
  public:
    [[nodiscard]] static bool acceptCurrent(WorkspaceSnapshot& snapshot,
                                            const QString& relativePath, QString* error = nullptr);
    [[nodiscard]] static bool rejectCurrent(const WorkspaceSnapshot& snapshot,
                                            const QString& relativePath,
                                            const QByteArray& expectedCurrentSha256,
                                            QString* error = nullptr);
};

} // namespace snack::workspace
