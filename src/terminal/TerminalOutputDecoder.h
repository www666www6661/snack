#pragma once

#include <QByteArrayView>
#include <QStringConverter>

namespace snack::terminal {

class TerminalOutputDecoder final {
  public:
    TerminalOutputDecoder();

    [[nodiscard]] QString decode(QByteArrayView bytes);
    [[nodiscard]] bool hasEncodingError() const { return utf8Decoder_.hasError(); }

  private:
    enum class EscapeState {
        Text,
        Escape,
        ControlSequence,
        OperatingSystemCommand,
        OperatingSystemCommandEscape,
        ControlString,
        ControlStringEscape,
    };

    EscapeState state_{EscapeState::Text};
    QStringDecoder utf8Decoder_;
};

} // namespace snack::terminal
