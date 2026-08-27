#include "app/WorkspacePathPolicy.h"

#include <QDir>
#include <QFileInfo>

namespace snack::app {
namespace {

void setError(QString* error, const QString& value) {
    if (error != nullptr) {
        *error = value;
    }
}

bool sameOrChildPath(const QString& root, const QString& path) {
    const Qt::CaseSensitivity sensitivity =
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
        Qt::CaseInsensitive;
#else
        Qt::CaseSensitive;
#endif
    if (path.compare(root, sensitivity) == 0) {
        return true;
    }
    return path.startsWith(root + QLatin1Char('/'), sensitivity);
}

} // namespace

std::optional<QString> WorkspacePathPolicy::resolveExisting(const QString& workspace,
                                                            const QString& candidate,
                                                            QString* error) {
    const QString rootPath = QFileInfo(workspace).canonicalFilePath();
    if (rootPath.isEmpty() || !QFileInfo(rootPath).isDir()) {
        setError(error, QStringLiteral("Workspace root does not exist"));
        return std::nullopt;
    }

    const QFileInfo candidateInfo(QDir(workspace).filePath(candidate));
    const QString candidatePath = candidateInfo.canonicalFilePath();
    if (candidatePath.isEmpty()) {
        setError(error, QStringLiteral("Workspace path does not exist"));
        return std::nullopt;
    }

    const QString normalizedRoot = QDir::cleanPath(QDir::fromNativeSeparators(rootPath));
    const QString normalizedCandidate = QDir::cleanPath(QDir::fromNativeSeparators(candidatePath));
    if (!sameOrChildPath(normalizedRoot, normalizedCandidate)) {
        setError(error, QStringLiteral("Workspace path escapes the workspace root"));
        return std::nullopt;
    }
    return QDir::toNativeSeparators(candidatePath);
}

QString WorkspacePathPolicy::identityKey(const QString& existingPath) {
    QString key =
        QDir::cleanPath(QDir::fromNativeSeparators(QFileInfo(existingPath).canonicalFilePath()));
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
    key = key.toCaseFolded();
#endif
    return key;
}

} // namespace snack::app
