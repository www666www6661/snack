#pragma once

#include <QByteArray>
#include <QHash>
#include <QStringList>

#include <optional>

namespace snack::workspace {

struct SnapshotEntry {
    QByteArray content;
    QByteArray sha256;
};

class WorkspaceSnapshot final {
  public:
    [[nodiscard]] static WorkspaceSnapshot capture(const QString& workspace,
                                                   qsizetype maximumFiles = 5000,
                                                   qsizetype maximumTotalBytes = 32 * 1024 * 1024,
                                                   QString* error = nullptr);

    [[nodiscard]] bool isComplete() const { return complete_; }
    [[nodiscard]] QString workspace() const { return workspace_; }
    [[nodiscard]] QStringList paths() const;
    [[nodiscard]] std::optional<SnapshotEntry> entry(const QString& relativePath) const;
    [[nodiscard]] bool matchesCurrentFile(const QString& relativePath) const;
    void setEntry(const QString& relativePath, SnapshotEntry entry);

  private:
    QString workspace_;
    QHash<QString, SnapshotEntry> entries_;
    bool complete_{false};
};

} // namespace snack::workspace
