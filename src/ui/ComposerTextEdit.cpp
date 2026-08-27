#include "ui/ComposerTextEdit.h"

#include <QAbstractTextDocumentLayout>
#include <QEvent>
#include <QKeyEvent>
#include <QTextDocument>

#include <algorithm>

namespace snack::ui {

ComposerTextEdit::ComposerTextEdit(QWidget* parent) : QPlainTextEdit(parent) {
    connect(document()->documentLayout(), &QAbstractTextDocumentLayout::documentSizeChanged, this,
            [this] { updateEditorHeight(); });
    updateEditorHeight();
}

void ComposerTextEdit::changeEvent(QEvent* event) {
    QPlainTextEdit::changeEvent(event);
    if (event->type() == QEvent::FontChange) {
        updateEditorHeight();
    }
}

void ComposerTextEdit::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_At && event->modifiers() == Qt::NoModifier) {
        emit workspaceReferenceRequested();
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Slash && event->modifiers() == Qt::NoModifier &&
        toPlainText().isEmpty()) {
        emit templateMenuRequested();
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Escape) {
        emit stopRequested();
        event->accept();
        return;
    }
    const bool enter = event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter;
    if (!enter) {
        QPlainTextEdit::keyPressEvent(event);
        return;
    }
    if (event->modifiers().testFlag(Qt::ShiftModifier)) {
        QPlainTextEdit::keyPressEvent(event);
        return;
    }
    if (event->modifiers().testAnyFlags(Qt::ControlModifier | Qt::MetaModifier)) {
        emit queueRequested();
    } else if (event->modifiers() == Qt::NoModifier || event->modifiers() == Qt::KeypadModifier) {
        emit sendRequested();
    } else {
        QPlainTextEdit::keyPressEvent(event);
        return;
    }
    event->accept();
}

void ComposerTextEdit::updateEditorHeight() {
    const int lineHeight = fontMetrics().lineSpacing();
    const int minimumEditorHeight = lineHeight * 2 + 32;
    const int maximumEditorHeight = lineHeight * 8 + 40;
    const int documentHeight =
        static_cast<int>(document()->documentLayout()->documentSize().height());
    const int frameHeight = frameWidth() * 2 + contentsMargins().top() + contentsMargins().bottom();
    setFixedHeight(
        std::clamp(documentHeight + frameHeight + 16, minimumEditorHeight, maximumEditorHeight));
}

} // namespace snack::ui
