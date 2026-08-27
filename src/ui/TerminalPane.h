#pragma once

#include <QWidget>

#include <memory>

class QLabel;

namespace snack::terminal {
class ITerminalProcess;
class TerminalSession;
} // namespace snack::terminal

namespace snack::ui {

class TerminalView;

class TerminalPane final : public QWidget {
    Q_OBJECT

  public:
    TerminalPane(QString workingDirectory, std::unique_ptr<terminal::ITerminalProcess> process,
                 QWidget* parent = nullptr);
    ~TerminalPane() override;

    [[nodiscard]] bool start(QString* error = nullptr);
    [[nodiscard]] QString workingDirectory() const { return workingDirectory_; }
    [[nodiscard]] bool isRunning() const { return running_; }
    [[nodiscard]] TerminalView* view() const { return view_; }

  private:
    QString workingDirectory_;
    terminal::TerminalSession* session_{nullptr};
    TerminalView* view_{nullptr};
    QLabel* status_{nullptr};
    bool running_{false};
};

} // namespace snack::ui
