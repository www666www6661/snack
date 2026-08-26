#include "agent/process/QProcessTransport.h"

namespace snack::agent::process {

QProcessTransport::QProcessTransport(QObject* parent) : IProcessTransport(parent) {
    process_.setProcessChannelMode(QProcess::SeparateChannels);
    connect(&process_, &QProcess::started, this, &IProcessTransport::started);
    connect(&process_, &QProcess::readyReadStandardOutput, this,
            [this] { emit standardOutputReceived(process_.readAllStandardOutput()); });
    connect(&process_, &QProcess::readyReadStandardError, this,
            [this] { emit standardErrorReceived(process_.readAllStandardError()); });
    connect(&process_, &QProcess::finished, this,
            [this](int exitCode, QProcess::ExitStatus status) {
                const QByteArray standardOutput = process_.readAllStandardOutput();
                if (!standardOutput.isEmpty()) {
                    emit standardOutputReceived(standardOutput);
                }
                const QByteArray standardError = process_.readAllStandardError();
                if (!standardError.isEmpty()) {
                    emit standardErrorReceived(standardError);
                }
                emit finished(exitCode, status == QProcess::NormalExit ? ExitStatus::Normal
                                                                       : ExitStatus::Crashed);
            });
    connect(&process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        Error mappedError = Error::Unknown;
        switch (error) {
        case QProcess::FailedToStart:
            mappedError = Error::FailedToStart;
            break;
        case QProcess::Crashed:
            mappedError = Error::Crashed;
            break;
        case QProcess::Timedout:
            mappedError = Error::TimedOut;
            break;
        case QProcess::ReadError:
            mappedError = Error::Read;
            break;
        case QProcess::WriteError:
            mappedError = Error::Write;
            break;
        case QProcess::UnknownError:
            break;
        }
        emit errorOccurred(mappedError, process_.errorString());
    });
}

bool QProcessTransport::isRunning() const { return process_.state() != QProcess::NotRunning; }

void QProcessTransport::start(const LaunchSpec& launchSpec) {
    process_.setWorkingDirectory(launchSpec.workingDirectory);
    process_.start(launchSpec.program, launchSpec.arguments, QIODevice::ReadWrite);
}

qint64 QProcessTransport::write(const QByteArray& data) { return process_.write(data); }

void QProcessTransport::closeWriteChannel() { process_.closeWriteChannel(); }

void QProcessTransport::terminate() {
    if (isRunning()) {
        process_.terminate();
    }
}

void QProcessTransport::kill() {
    if (isRunning()) {
        process_.kill();
    }
}

} // namespace snack::agent::process
