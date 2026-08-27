#include "app/WorkspaceWatcher.h"

#include <QDirIterator>
#include <QFileInfo>

namespace snack::app {

WorkspaceWatcher::WorkspaceWatcher(QObject* parent) : QObject(parent) {
    debounce_.setSingleShot(true);
    debounce_.setInterval(150);
    connect(&watcher_, &QFileSystemWatcher::directoryChanged, this, [this] { debounce_.start(); });
    connect(&watcher_, &QFileSystemWatcher::fileChanged, this, [this] { debounce_.start(); });
    connect(&debounce_, &QTimer::timeout, this, [this] {
        rebuildPaths();
        emit workspaceChanged();
    });
}

void WorkspaceWatcher::setWorkspace(const QString& workspace) {
    const QString canonical = QFileInfo(workspace).canonicalFilePath();
    if (workspace_ == canonical) {
        return;
    }
    workspace_ = canonical;
    debounce_.stop();
    rebuildPaths();
}

void WorkspaceWatcher::rebuildPaths() {
    const QStringList oldPaths = watcher_.directories() + watcher_.files();
    if (!oldPaths.isEmpty()) {
        watcher_.removePaths(oldPaths);
    }
    if (workspace_.isEmpty()) {
        return;
    }

    QStringList paths{workspace_};
    QDirIterator iterator(workspace_, QDir::Dirs | QDir::NoDotAndDotDot,
                          QDirIterator::Subdirectories);
    while (iterator.hasNext() && paths.size() < 2000) {
        const QString path = iterator.next();
        const QString name = QFileInfo(path).fileName().toCaseFolded();
        if (name == QLatin1String(".git") || name == QLatin1String("node_modules") ||
            name == QLatin1String("build") || name == QLatin1String(".cache")) {
            continue;
        }
        paths.append(path);
    }
    watcher_.addPaths(paths);
}

} // namespace snack::app
