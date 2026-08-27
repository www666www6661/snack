#pragma once

#include <QFileSystemWatcher>
#include <QObject>
#include <QTimer>

namespace snack::app {

class WorkspaceWatcher final : public QObject {
    Q_OBJECT

  public:
    explicit WorkspaceWatcher(QObject* parent = nullptr);
    void setWorkspace(const QString& workspace);

  signals:
    void workspaceChanged();

  private:
    void rebuildPaths();

    QFileSystemWatcher watcher_;
    QTimer debounce_;
    QString workspace_;
};

} // namespace snack::app
