#include "ui/TerminalPane.h"

#include "terminal/TerminalSession.h"
#include "ui/TerminalView.h"

#include <QLabel>
#include <QVBoxLayout>

namespace snack::ui {

TerminalPane::TerminalPane(QString workingDirectory,
                           std::unique_ptr<terminal::ITerminalProcess> process, QWidget* parent)
    : QWidget(parent), workingDirectory_(std::move(workingDirectory)) {
    setObjectName(QStringLiteral("terminalPane"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    view_ = new TerminalView(this);
    status_ = new QLabel(this);
    status_->setObjectName(QStringLiteral("terminalStatus"));
    status_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    status_->hide();
    layout->addWidget(view_, 1);
    layout->addWidget(status_);

    session_ = new terminal::TerminalSession(std::move(process), this);
    connect(view_, &TerminalView::inputReady, this, [this](const QByteArray& bytes) {
        if (running_) {
            session_->writeInput(bytes);
        }
    });
    connect(view_, &TerminalView::terminalSizeChanged, session_,
            &terminal::TerminalSession::resizeTerminal);
    connect(session_, &terminal::TerminalSession::screenChanged, view_,
            &TerminalView::setScreenText);
    connect(session_, &terminal::TerminalSession::exited, this, [this](int exitCode) {
        running_ = false;
        status_->setText(tr("Process exited with code %1").arg(exitCode));
        status_->show();
    });
    connect(session_, &terminal::TerminalSession::processError, this,
            [this](const QString& message) {
                status_->setText(message);
                status_->show();
            });
}

TerminalPane::~TerminalPane() { session_->close(); }

bool TerminalPane::start(QString* error) {
    if (running_) {
        return true;
    }
    if (!session_->start(workingDirectory_, 100, 30, error)) {
        status_->setText(error != nullptr ? *error : tr("Cannot start terminal"));
        status_->show();
        return false;
    }
    running_ = true;
    status_->hide();
    view_->setFocus();
    return true;
}

} // namespace snack::ui
