#include "terminal/TerminalTextBuffer.h"

#include <algorithm>

namespace snack::terminal {

TerminalTextBuffer::TerminalTextBuffer(qsizetype maximumCharacters)
    : maximumCharacters_(std::max<qsizetype>(1, maximumCharacters)) {}

void TerminalTextBuffer::append(const QString& text) {
    for (const QChar character : text) {
        if (character == u'\r') {
            cursorColumn_ = 0;
            carriageReturnPending_ = true;
        } else if (character == u'\n') {
            history_.append(line_);
            history_.append(u'\n');
            line_.clear();
            cursorColumn_ = 0;
            carriageReturnPending_ = false;
        } else if (character == u'\b') {
            cursorColumn_ = std::max<qsizetype>(0, cursorColumn_ - 1);
        } else if (character == u'\t') {
            if (carriageReturnPending_) {
                line_.clear();
                carriageReturnPending_ = false;
            }
            const qsizetype nextTabStop = ((cursorColumn_ / 8) + 1) * 8;
            while (cursorColumn_ < nextTabStop) {
                appendCharacter(u' ');
            }
        } else {
            if (carriageReturnPending_) {
                line_.clear();
                carriageReturnPending_ = false;
            }
            appendCharacter(character);
        }
    }
    trim();
}

void TerminalTextBuffer::clear() {
    history_.clear();
    line_.clear();
    cursorColumn_ = 0;
    carriageReturnPending_ = false;
}

void TerminalTextBuffer::appendCharacter(QChar character) {
    if (cursorColumn_ < line_.size()) {
        line_[cursorColumn_] = character;
    } else {
        if (cursorColumn_ > line_.size()) {
            line_.append(QString(cursorColumn_ - line_.size(), u' '));
        }
        line_.append(character);
    }
    ++cursorColumn_;
}

void TerminalTextBuffer::trim() {
    qsizetype excess = history_.size() + line_.size() - maximumCharacters_;
    if (excess <= 0) {
        return;
    }
    const qsizetype historyRemoval = std::min(excess, history_.size());
    history_.remove(0, historyRemoval);
    excess -= historyRemoval;
    if (excess > 0) {
        line_.remove(0, excess);
        cursorColumn_ = std::max<qsizetype>(0, cursorColumn_ - excess);
    }
}

} // namespace snack::terminal
