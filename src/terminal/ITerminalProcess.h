#pragma once

#include <QObject>

namespace snack::terminal {

class ITerminalProcess : public QObject {
    Q_OBJECT

  public:
    using QObject::QObject;
    ~ITerminalProcess() override = default;

    [[nodiscard]] virtual bool start(const QString& workingDirectory, int columns, int rows,
                                     QString* error = nullptr) = 0;
    virtual void writeInput(const QByteArray& bytes) = 0;
    virtual void resizeTerminal(int columns, int rows) = 0;
    virtual void closeTerminal() = 0;

  signals:
    void outputReady(const QByteArray& bytes);
    void exited(int exitCode);
    void processError(const QString& message);
};

} // namespace snack::terminal
