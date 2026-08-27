#pragma once

#include "terminal/ITerminalProcess.h"

#include <QWidget>

#include <functional>
#include <memory>

class QMainWindow;
class QTabWidget;
class QToolButton;

namespace snack::ui {

class TerminalTabs final : public QWidget {
    Q_OBJECT

  public:
    using ProcessFactory = std::function<std::unique_ptr<terminal::ITerminalProcess>()>;

    explicit TerminalTabs(QString workingDirectory, QWidget* parent = nullptr);
    TerminalTabs(QString workingDirectory, ProcessFactory processFactory,
                 QWidget* parent = nullptr);

    void setWorkingDirectory(const QString& workingDirectory);
    void applyInterfaceScale(double scale);
    [[nodiscard]] int terminalCount() const;

  public slots:
    void newTerminal();
    void closeCurrentTerminal();
    void detachCurrentTerminal();

  signals:
    void terminalDetached(QMainWindow* window);

  private:
    void closeTerminal(int index);
    void updateActions();

    QString workingDirectory_;
    ProcessFactory processFactory_;
    QTabWidget* tabs_{nullptr};
    QToolButton* newButton_{nullptr};
    QToolButton* detachButton_{nullptr};
    QToolButton* closeButton_{nullptr};
    int nextTerminalNumber_{1};
};

} // namespace snack::ui
