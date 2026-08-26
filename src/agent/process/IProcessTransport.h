#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QStringList>

namespace snack::agent::process {

enum class ExitStatus { Normal, Crashed };
enum class Error { FailedToStart, Crashed, TimedOut, Read, Write, Unknown };

struct LaunchSpec {
    QString program;
    QStringList arguments;
    QString workingDirectory;
};

class IProcessTransport : public QObject {
    Q_OBJECT

  public:
    using QObject::QObject;
    ~IProcessTransport() override = default;

    [[nodiscard]] virtual bool isRunning() const = 0;
    virtual void start(const LaunchSpec& launchSpec) = 0;
    virtual qint64 write(const QByteArray& data) = 0;
    virtual void closeWriteChannel() = 0;
    virtual void terminate() = 0;
    virtual void kill() = 0;

  signals:
    void started();
    void standardOutputReceived(const QByteArray& data);
    void standardErrorReceived(const QByteArray& data);
    void finished(int exitCode, snack::agent::process::ExitStatus status);
    void errorOccurred(snack::agent::process::Error error, const QString& detail);
};

} // namespace snack::agent::process

Q_DECLARE_METATYPE(snack::agent::process::ExitStatus)
Q_DECLARE_METATYPE(snack::agent::process::Error)
