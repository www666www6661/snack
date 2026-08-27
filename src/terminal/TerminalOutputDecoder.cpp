#include "terminal/TerminalOutputDecoder.h"

#include <QByteArray>

namespace snack::terminal {

TerminalOutputDecoder::TerminalOutputDecoder() : utf8Decoder_(QStringDecoder::Utf8) {}

QString TerminalOutputDecoder::decode(QByteArrayView bytes) {
    QByteArray plainBytes;
    plainBytes.reserve(bytes.size());

    for (const char value : bytes) {
        const auto byte = static_cast<unsigned char>(value);
        switch (state_) {
        case EscapeState::Text:
            if (byte == 0x1b) {
                state_ = EscapeState::Escape;
            } else if (byte == '\b' || byte == '\t' || byte == '\n' || byte == '\r' ||
                       byte >= 0x20) {
                if (byte != 0x7f) {
                    plainBytes.append(value);
                }
            }
            break;
        case EscapeState::Escape:
            if (byte == '[') {
                state_ = EscapeState::ControlSequence;
            } else if (byte == ']') {
                state_ = EscapeState::OperatingSystemCommand;
            } else if (byte == 'P' || byte == '^' || byte == '_') {
                state_ = EscapeState::ControlString;
            } else if (byte == 0x1b || (byte >= 0x20 && byte <= 0x2f)) {
                state_ = EscapeState::Escape;
            } else {
                state_ = EscapeState::Text;
            }
            break;
        case EscapeState::ControlSequence:
            if (byte >= 0x40 && byte <= 0x7e) {
                state_ = EscapeState::Text;
            } else if (byte == 0x1b) {
                state_ = EscapeState::Escape;
            }
            break;
        case EscapeState::OperatingSystemCommand:
            if (byte == 0x07) {
                state_ = EscapeState::Text;
            } else if (byte == 0x1b) {
                state_ = EscapeState::OperatingSystemCommandEscape;
            }
            break;
        case EscapeState::OperatingSystemCommandEscape:
            if (byte == '\\') {
                state_ = EscapeState::Text;
            } else if (byte != 0x1b) {
                state_ = EscapeState::OperatingSystemCommand;
            }
            break;
        case EscapeState::ControlString:
            if (byte == 0x1b) {
                state_ = EscapeState::ControlStringEscape;
            }
            break;
        case EscapeState::ControlStringEscape:
            if (byte == '\\') {
                state_ = EscapeState::Text;
            } else if (byte != 0x1b) {
                state_ = EscapeState::ControlString;
            }
            break;
        }
    }

    return utf8Decoder_.decode(QByteArrayView(plainBytes));
}

} // namespace snack::terminal
