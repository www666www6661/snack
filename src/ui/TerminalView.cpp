#include "ui/TerminalView.h"

#include <QApplication>
#include <QClipboard>
#include <QFont>
#include <QKeyEvent>
#include <QResizeEvent>
#include <QScrollBar>
#include <QTextCursor>

#include <algorithm>

namespace snack::ui {
namespace {

QByteArray specialKeySequence(int key) {
    switch (key) {
    case Qt::Key_Return:
    case Qt::Key_Enter:
        return QByteArrayLiteral("\r");
    case Qt::Key_Backspace:
        return QByteArrayLiteral("\x7f");
    case Qt::Key_Tab:
        return QByteArrayLiteral("\t");
    case Qt::Key_Backtab:
        return QByteArrayLiteral("\x1b[Z");
    case Qt::Key_Escape:
        return QByteArrayLiteral("\x1b");
    case Qt::Key_Up:
        return QByteArrayLiteral("\x1b[A");
    case Qt::Key_Down:
        return QByteArrayLiteral("\x1b[B");
    case Qt::Key_Right:
        return QByteArrayLiteral("\x1b[C");
    case Qt::Key_Left:
        return QByteArrayLiteral("\x1b[D");
    case Qt::Key_Home:
        return QByteArrayLiteral("\x1b[H");
    case Qt::Key_End:
        return QByteArrayLiteral("\x1b[F");
    case Qt::Key_Insert:
        return QByteArrayLiteral("\x1b[2~");
    case Qt::Key_Delete:
        return QByteArrayLiteral("\x1b[3~");
    case Qt::Key_PageUp:
        return QByteArrayLiteral("\x1b[5~");
    case Qt::Key_PageDown:
        return QByteArrayLiteral("\x1b[6~");
    default:
        return {};
    }
}

} // namespace

TerminalView::TerminalView(QWidget* parent) : QPlainTextEdit(parent) {
    setObjectName(QStringLiteral("terminalView"));
    setReadOnly(true);
    setUndoRedoEnabled(false);
    setLineWrapMode(QPlainTextEdit::NoWrap);
    QFont terminalFont(QStringLiteral("Cascadia Mono"));
    terminalFont.setStyleHint(QFont::Monospace);
    terminalFont.setFixedPitch(true);
    setFont(terminalFont);
    setFocusPolicy(Qt::StrongFocus);
    setAccessibleName(tr("Terminal output"));
}

void TerminalView::setScreenText(const QString& text) {
    if (toPlainText() == text) {
        return;
    }
    auto* scrollBar = verticalScrollBar();
    const bool followOutput = scrollBar->value() >= scrollBar->maximum();
    const int previousScroll = scrollBar->value();
    setPlainText(text);
    if (followOutput) {
        moveCursor(QTextCursor::End);
    } else {
        scrollBar->setValue(previousScroll);
    }
}

void TerminalView::keyPressEvent(QKeyEvent* event) {
    const auto modifiers = event->modifiers();
    const bool shiftControl =
        modifiers.testFlag(Qt::ControlModifier) && modifiers.testFlag(Qt::ShiftModifier);
    if (shiftControl && event->key() == Qt::Key_C) {
        copy();
        return;
    }
    if ((shiftControl && event->key() == Qt::Key_V) ||
        (modifiers == Qt::ShiftModifier && event->key() == Qt::Key_Insert)) {
        sendPaste();
        return;
    }

    QByteArray bytes;
    if (modifiers.testFlag(Qt::ControlModifier) && event->key() >= Qt::Key_A &&
        event->key() <= Qt::Key_Z) {
        bytes.append(static_cast<char>(event->key() - Qt::Key_A + 1));
    } else {
        bytes = specialKeySequence(event->key());
        if (bytes.isEmpty() && !event->text().isEmpty() &&
            !modifiers.testFlag(Qt::ControlModifier)) {
            bytes = event->text().toUtf8();
            if (modifiers.testFlag(Qt::AltModifier)) {
                bytes.prepend('\x1b');
            }
        }
    }
    if (!bytes.isEmpty()) {
        emit inputReady(bytes);
        event->accept();
        return;
    }
    QPlainTextEdit::keyPressEvent(event);
}

void TerminalView::resizeEvent(QResizeEvent* event) {
    QPlainTextEdit::resizeEvent(event);
    reportTerminalSize();
}

void TerminalView::sendPaste() {
    QString text = QApplication::clipboard()->text().left(1024 * 1024);
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    text.replace(u'\r', u'\n');
#if defined(Q_OS_WIN)
    text.replace(u'\n', u'\r');
#endif
    if (!text.isEmpty()) {
        emit inputReady(text.toUtf8());
    }
}

void TerminalView::reportTerminalSize() {
    const QFontMetrics metrics(font());
    const int cellWidth = std::max(1, metrics.horizontalAdvance(QLatin1Char('M')));
    const int cellHeight = std::max(1, metrics.lineSpacing());
    const int columns = std::max(1, viewport()->width() / cellWidth);
    const int rows = std::max(1, viewport()->height() / cellHeight);
    emit terminalSizeChanged(columns, rows);
}

} // namespace snack::ui
