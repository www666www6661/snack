#include "app/WorkspaceFileIndex.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QSet>

#include <algorithm>

namespace snack::app {

QStringList WorkspaceFileIndex::files(const QString& workspace, qsizetype limit) {
    if (limit <= 0) {
        return {};
    }
    const QDir root(workspace);
    if (!root.exists()) {
        return {};
    }
    static const QSet<QString> excluded = {
        QStringLiteral(".git"),        QStringLiteral("node_modules"), QStringLiteral(".cache"),
        QStringLiteral("build"),       QStringLiteral("dist"),         QStringLiteral("target"),
        QStringLiteral("__pycache__"), QStringLiteral(".venv")};
    QStringList result;
    QDirIterator iterator(root.absolutePath(), QDir::Files | QDir::NoDotAndDotDot,
                          QDirIterator::Subdirectories);
    while (iterator.hasNext() && result.size() < limit) {
        const QString absolutePath = iterator.next();
        const QString relativePath = root.relativeFilePath(absolutePath);
        const QStringList parts = relativePath.split(QLatin1Char('/'), Qt::SkipEmptyParts);
        if (std::any_of(parts.cbegin(), parts.cend(), [](const QString& part) {
                return excluded.contains(part.toCaseFolded());
            })) {
            continue;
        }
        result.append(QDir::fromNativeSeparators(relativePath));
    }
    result.sort(Qt::CaseInsensitive);
    return result;
}

} // namespace snack::app
