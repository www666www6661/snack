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
        const QString plainText = outputDecoder_.decode(QByteArrayView(bytes));
        if (!plainText.isEmpty()) {
            textBuffer_.append(plainText);
            emit screenChanged(textBuffer_.text());
        }
        emit outputReady(bytes);
    });
    connect(process_.get(), &ITerminalProcess::exited, this, &TerminalSession::exited);
    connect(process_.get(), &ITerminalProcess::processError, this, &TerminalSession::processError);
}

bool TerminalSession::start(const QString& workingDirectory, int columns, int rows,
                            QString* error) {
    constexpr int maximumDimension = 32767;
    if (columns <= 0 || rows <= 0 || columns > maximumDimension || rows > maximumDimension) {
        if (error != nullptr) {
            *error = QStringLiteral("Terminal dimensions must be between 1 and 32767");
        }
        return false;
    }
    return process_->start(workingDirectory, columns, rows, error);
}

void TerminalSession::writeInput(const QByteArray& bytes) { process_->writeInput(bytes); }

void TerminalSession::resizeTerminal(int columns, int rows) {
    constexpr int maximumDimension = 32767;
    if (columns > 0 && rows > 0 && columns <= maximumDimension && rows <= maximumDimension) {
        process_->resizeTerminal(columns, rows);
    }
}

void TerminalSession::close() { process_->closeTerminal(); }

} // namespace snack::terminal
