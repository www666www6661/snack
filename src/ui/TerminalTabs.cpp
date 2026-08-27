#include "ui/TerminalTabs.h"

#include "terminal/NativeTerminalProcess.h"
#include "ui/TerminalPane.h"

#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QMainWindow>
#include <QTabWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include <utility>

namespace snack::ui {

TerminalTabs::TerminalTabs(QString workingDirectory, QWidget* parent)
    : TerminalTabs(std::move(workingDirectory), terminal::createNativeTerminalProcess, parent) {}

TerminalTabs::TerminalTabs(QString workingDirectory, ProcessFactory processFactory, QWidget* parent)
    : QWidget(parent), workingDirectory_(std::move(workingDirectory)),
      processFactory_(std::move(processFactory)) {
    setObjectName(QStringLiteral("terminalTabs"));
    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);
    auto* toolbar = new QWidget(this);
    toolbar->setObjectName(QStringLiteral("terminalToolbar"));
    auto* toolbarLayout = new QHBoxLayout(toolbar);
    toolbarLayout->setContentsMargins(6, 4, 6, 4);
    auto* workingDirectoryLabel = new QLabel(toolbar);
    workingDirectoryLabel->setObjectName(QStringLiteral("terminalWorkingDirectory"));
    workingDirectoryLabel->setText(QFileInfo(workingDirectory_).fileName());
    workingDirectoryLabel->setToolTip(workingDirectory_);
    newButton_ = new QToolButton(toolbar);
    newButton_->setObjectName(QStringLiteral("newTerminalButton"));
    newButton_->setText(QStringLiteral("+"));
    newButton_->setToolTip(tr("New terminal"));
    detachButton_ = new QToolButton(toolbar);
    detachButton_->setObjectName(QStringLiteral("detachTerminalButton"));
    detachButton_->setText(QStringLiteral("^"));
    detachButton_->setToolTip(tr("Open current terminal in a new window"));
    closeButton_ = new QToolButton(toolbar);
    closeButton_->setObjectName(QStringLiteral("closeTerminalButton"));
    closeButton_->setText(QStringLiteral("x"));
    closeButton_->setToolTip(tr("Close current terminal"));
    toolbarLayout->addWidget(workingDirectoryLabel);
    toolbarLayout->addStretch(1);
    toolbarLayout->addWidget(newButton_);
    toolbarLayout->addWidget(detachButton_);
    toolbarLayout->addWidget(closeButton_);
    rootLayout->addWidget(toolbar);

    tabs_ = new QTabWidget(this);
    tabs_->setObjectName(QStringLiteral("terminalTabWidget"));
    tabs_->setTabsClosable(true);
    tabs_->setMovable(true);
    rootLayout->addWidget(tabs_, 1);

    connect(newButton_, &QToolButton::clicked, this, &TerminalTabs::newTerminal);
    connect(detachButton_, &QToolButton::clicked, this, &TerminalTabs::detachCurrentTerminal);
    connect(closeButton_, &QToolButton::clicked, this, &TerminalTabs::closeCurrentTerminal);
    connect(tabs_, &QTabWidget::tabCloseRequested, this, &TerminalTabs::closeTerminal);
    connect(tabs_, &QTabWidget::currentChanged, this, &TerminalTabs::updateActions);
    updateActions();
}

void TerminalTabs::setWorkingDirectory(const QString& workingDirectory) {
    workingDirectory_ = workingDirectory;
    auto* label = findChild<QLabel*>(QStringLiteral("terminalWorkingDirectory"));
    label->setText(QFileInfo(workingDirectory_).fileName());
    label->setToolTip(workingDirectory_);
}

int TerminalTabs::terminalCount() const { return tabs_->count(); }

void TerminalTabs::newTerminal() {
    auto* pane = new TerminalPane(workingDirectory_, processFactory_(), tabs_);
    const QString title = tr("Terminal %1").arg(nextTerminalNumber_++);
    const int index = tabs_->addTab(pane, title);
    tabs_->setCurrentIndex(index);
    QString error;
    if (!pane->start(&error)) {
        tabs_->setTabToolTip(index, error);
    }
    updateActions();
}

void TerminalTabs::closeCurrentTerminal() { closeTerminal(tabs_->currentIndex()); }

void TerminalTabs::detachCurrentTerminal() {
    const int index = tabs_->currentIndex();
    auto* pane = index >= 0 ? qobject_cast<TerminalPane*>(tabs_->widget(index)) : nullptr;
    if (pane == nullptr) {
        return;
    }
    const QString title = tabs_->tabText(index);
    tabs_->removeTab(index);
    auto* detached = new QMainWindow(window(), Qt::Window);
    detached->setObjectName(QStringLiteral("detachedTerminalWindow"));
    detached->setAttribute(Qt::WA_DeleteOnClose);
    detached->setWindowTitle(tr("%1 — Snack").arg(title));
    detached->setCentralWidget(pane);
    detached->resize(900, 560);
    detached->show();
    updateActions();
    emit terminalDetached(detached);
}

void TerminalTabs::closeTerminal(int index) {
    if (index < 0 || index >= tabs_->count()) {
        return;
    }
    QWidget* page = tabs_->widget(index);
    tabs_->removeTab(index);
    delete page;
    updateActions();
}

void TerminalTabs::updateActions() {
    const bool hasTerminal = tabs_->currentIndex() >= 0;
    detachButton_->setEnabled(hasTerminal);
    closeButton_->setEnabled(hasTerminal);
}

} // namespace snack::ui
