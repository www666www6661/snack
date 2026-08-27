#pragma once

#include "terminal/ITerminalProcess.h"
#include "terminal/TerminalOutputDecoder.h"
#include "terminal/TerminalTextBuffer.h"

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
    [[nodiscard]] QString screenText() const { return textBuffer_.text(); }

  signals:
    void outputReady(const QByteArray& bytes);
    void screenChanged(const QString& text);
    void exited(int exitCode);
    void processError(const QString& message);

  private:
    std::unique_ptr<ITerminalProcess> process_;
    QByteArray scrollback_;
    TerminalOutputDecoder outputDecoder_;
    TerminalTextBuffer textBuffer_;
};

} // namespace snack::terminal
