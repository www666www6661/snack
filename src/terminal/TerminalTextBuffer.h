#pragma once

#include <QString>

namespace snack::terminal {

class TerminalTextBuffer final {
  public:
    explicit TerminalTextBuffer(qsizetype maximumCharacters = 2 * 1024 * 1024);

    void append(const QString& text);
    void clear();
    [[nodiscard]] QString text() const { return history_ + line_; }

  private:
    void appendCharacter(QChar character);
    void trim();

    qsizetype maximumCharacters_;
    QString history_;
    QString line_;
    qsizetype cursorColumn_{0};
    bool carriageReturnPending_{false};
};

} // namespace snack::terminal
