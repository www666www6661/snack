#pragma once

#include "terminal/ITerminalProcess.h"

#include <memory>

namespace snack::terminal {

class TerminalSession final : public QObject {
    Q_OBJECT

  public:
    explicit TerminalSession(std::unique_ptr<ITerminalProcess> process, QObject* parent = nullptr);

    [[nodiscard]] bool start(const QString& workingDirectory, int columns = 100, int rows = 30,
                             QString* error = nullptr);
    void writeInput(const QByteArray& bytes);
    void resizeTerminal(int columns, int rows);
    void close();
    [[nodiscard]] QByteArray scrollback() const { return scrollback_; }

  signals:
    void outputReady(const QByteArray& bytes);
    void exited(int exitCode);
    void processError(const QString& message);

  private:
    std::unique_ptr<ITerminalProcess> process_;
    QByteArray scrollback_;
};

} // namespace snack::terminal
