#pragma once

#include <QPlainTextEdit>

namespace snack::ui {

class TerminalView final : public QPlainTextEdit {
    Q_OBJECT

  public:
    explicit TerminalView(QWidget* parent = nullptr);

    void setScreenText(const QString& text);

  signals:
    void inputReady(const QByteArray& bytes);
    void terminalSizeChanged(int columns, int rows);

  protected:
    void keyPressEvent(QKeyEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

  private:
    void sendPaste();
    void reportTerminalSize();
};

} // namespace snack::ui
