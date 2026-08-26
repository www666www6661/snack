#pragma once

#include <QPlainTextEdit>

namespace snack::ui {

class ComposerTextEdit final : public QPlainTextEdit {
    Q_OBJECT

  public:
    explicit ComposerTextEdit(QWidget* parent = nullptr);

  signals:
    void sendRequested();
    void queueRequested();
    void stopRequested();
    void templateMenuRequested();

  protected:
    void changeEvent(QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

  private:
    void updateEditorHeight();
};

} // namespace snack::ui
