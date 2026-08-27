#include "workspace/WorkspaceSnapshot.h"

#include "app/WorkspaceFileIndex.h"
#include "app/WorkspacePathPolicy.h"

#include <QCryptographicHash>
#include <QFile>

namespace snack::workspace {

WorkspaceSnapshot WorkspaceSnapshot::capture(const QString& workspace, qsizetype maximumFiles,
                                             qsizetype maximumTotalBytes, QString* error) {
    WorkspaceSnapshot snapshot;
    const auto root =
        app::WorkspacePathPolicy::resolveExisting(workspace, QStringLiteral("."), error);
    if (!root.has_value() || maximumFiles <= 0 || maximumTotalBytes < 0) {
        return snapshot;
    }
    snapshot.workspace_ = *root;
    qsizetype totalBytes = 0;
    const QStringList files = app::WorkspaceFileIndex::files(workspace, maximumFiles + 1);
    if (files.size() > maximumFiles) {
        if (error != nullptr) {
            *error = QStringLiteral("Workspace snapshot exceeds its file limit");
        }
        return snapshot;
    }
    for (const QString& relativePath : files) {
        const auto path = app::WorkspacePathPolicy::resolveExisting(workspace, relativePath, error);
        if (!path.has_value()) {
            return snapshot;
        }
        QFile file(*path);
        if (!file.open(QIODevice::ReadOnly)) {
            if (error != nullptr) {
                *error = file.errorString();
            }
            return snapshot;
        }
        const QByteArray content = file.readAll();
        totalBytes += content.size();
        if (totalBytes > maximumTotalBytes) {
            if (error != nullptr) {
                *error = QStringLiteral("Workspace snapshot exceeds its byte limit");
            }
            return snapshot;
        }
        snapshot.entries_.insert(relativePath, {.content = content,
                                                .sha256 = QCryptographicHash::hash(
                                                    content, QCryptographicHash::Sha256)});
    }
    snapshot.complete_ = true;
    return snapshot;
}

QStringList WorkspaceSnapshot::paths() const {
    QStringList result = entries_.keys();
    result.sort(Qt::CaseInsensitive);
    return result;
}

std::optional<SnapshotEntry> WorkspaceSnapshot::entry(const QString& relativePath) const {
    const auto iterator = entries_.constFind(relativePath);
    return iterator == entries_.cend() ? std::nullopt : std::optional<SnapshotEntry>(*iterator);
}

bool WorkspaceSnapshot::matchesCurrentFile(const QString& relativePath) const {
    const auto baseline = entry(relativePath);
    const auto resolved = app::WorkspacePathPolicy::resolveExisting(workspace_, relativePath);
    if (!baseline.has_value() || !resolved.has_value()) {
        return false;
    }
    QFile file(*resolved);
    return file.open(QIODevice::ReadOnly) &&
           QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256) == baseline->sha256;
}

} // namespace snack::workspace
