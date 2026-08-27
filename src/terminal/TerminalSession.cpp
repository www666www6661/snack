#include "terminal/TerminalSession.h"

namespace snack::terminal {

TerminalSession::TerminalSession(std::unique_ptr<ITerminalProcess> process, QObject* parent)
    : QObject(parent), process_(std::move(process)) {
    Q_ASSERT(process_ != nullptr);
    connect(process_.get(), &ITerminalProcess::outputReady, this, [this](const QByteArray& bytes) {
        scrollback_.append(bytes);
        constexpr qsizetype maximumScrollback = 4 * 1024 * 1024;
        if (scrollback_.size() > maximumScrollback) {
            scrollback_ = scrollback_.last(maximumScrollback);
        }
        emit outputReady(bytes);
    });
    connect(process_.get(), &ITerminalProcess::exited, this, &TerminalSession::exited);
    connect(process_.get(), &ITerminalProcess::processError, this, &TerminalSession::processError);
}

bool TerminalSession::start(const QString& workingDirectory, int columns, int rows,
                            QString* error) {
    if (columns <= 0 || rows <= 0) {
        if (error != nullptr) {
            *error = QStringLiteral("Terminal dimensions must be positive");
        }
        return false;
    }
    return process_->start(workingDirectory, columns, rows, error);
}

void TerminalSession::writeInput(const QByteArray& bytes) { process_->writeInput(bytes); }

void TerminalSession::resizeTerminal(int columns, int rows) {
    if (columns > 0 && rows > 0) {
        process_->resizeTerminal(columns, rows);
    }
}

void TerminalSession::close() { process_->closeTerminal(); }

} // namespace snack::terminal
