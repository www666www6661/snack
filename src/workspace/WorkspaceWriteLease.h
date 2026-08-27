#pragma once

#include <QString>

namespace snack::workspace {

class WorkspaceWriteLease final {
  public:
    WorkspaceWriteLease() = default;
    ~WorkspaceWriteLease();
    WorkspaceWriteLease(const WorkspaceWriteLease&) = delete;
    WorkspaceWriteLease& operator=(const WorkspaceWriteLease&) = delete;

    [[nodiscard]] bool acquire(const QString& workspace, const QString& ownerId,
                               QString* error = nullptr);
    [[nodiscard]] bool transfer(const QString& nextOwnerId, QString* error = nullptr);
    void release();
    [[nodiscard]] bool held() const { return held_; }
    [[nodiscard]] QString ownerId() const { return ownerId_; }

  private:
    QString workspaceKey_;
    QString ownerId_;
    bool held_{false};
};

} // namespace snack::workspace
