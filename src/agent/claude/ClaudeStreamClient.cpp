#include "agent/claude/ClaudeStreamClient.h"

#include <QJsonDocument>

namespace snack::agent::claude {
namespace {

qsizetype jsonlPayloadBytes(const QByteArray& buffer, qsizetype newline = -1) {
    qsizetype bytes = newline >= 0 ? newline : buffer.size();
    if (bytes > 0 && buffer.at(bytes - 1) == '\r') {
        --bytes;
    }
    return bytes;
}

} // namespace

ClaudeStreamClient::ClaudeStreamClient(process::IProcessTransport* transport, QObject* parent,
                                       qsizetype maxFrameBytes, qsizetype maxDiagnosticBytes,
                                       int shutdownTimeoutMs)
    : QObject(parent), transport_(transport), maxFrameBytes_(maxFrameBytes),
      maxDiagnosticBytes_(maxDiagnosticBytes), shutdownTimeoutMs_(shutdownTimeoutMs) {
    Q_ASSERT(transport_ != nullptr);
    Q_ASSERT(maxFrameBytes_ > 0);
    Q_ASSERT(maxDiagnosticBytes_ > 0);
    Q_ASSERT(shutdownTimeoutMs_ > 0);

    handshakeTimer_.setSingleShot(true);
    connect(&handshakeTimer_, &QTimer::timeout, this,
            [this] { fail(QStringLiteral("Claude stream initialization timed out")); });
    shutdownTimer_.setSingleShot(true);
    connect(&shutdownTimer_, &QTimer::timeout, this, [this] {
        if (!transport_->isRunning()) {
            if (state_ == StreamState::Stopping) {
                setState(StreamState::Stopped);
            }
            return;
        }
        emit protocolWarning(
            QStringLiteral("Claude process did not stop after %1 ms; forcing termination")
                .arg(shutdownTimeoutMs_));
        transport_->kill();
        if (!transport_->isRunning() && state_ == StreamState::Stopping) {
            setState(StreamState::Stopped);
        }
    });

    connect(transport_, &process::IProcessTransport::started, this,
            &ClaudeStreamClient::handleProcessStarted);
    connect(transport_, &process::IProcessTransport::standardOutputReceived, this,
            &ClaudeStreamClient::handleStandardOutput);
    connect(transport_, &process::IProcessTransport::standardErrorReceived, this,
            &ClaudeStreamClient::handleStandardError);
    connect(transport_, &process::IProcessTransport::finished, this,
            &ClaudeStreamClient::handleProcessFinished);
    connect(transport_, &process::IProcessTransport::errorOccurred, this,
            &ClaudeStreamClient::handleProcessError);
}

StreamState ClaudeStreamClient::state() const { return state_; }

InitInfo ClaudeStreamClient::initInfo() const { return initInfo_; }

QByteArray ClaudeStreamClient::diagnostics() const { return diagnostics_; }

bool ClaudeStreamClient::start(const process::LaunchSpec& launchSpec, int handshakeTimeoutMs) {
    if (state_ != StreamState::Stopped && state_ != StreamState::Failed) {
        emit protocolWarning(QStringLiteral("Claude stream connection is already active"));
        return false;
    }
    if (transport_->isRunning()) {
        emit protocolWarning(QStringLiteral("Claude process is still stopping"));
        return false;
    }
    if (handshakeTimeoutMs <= 0) {
        emit protocolWarning(QStringLiteral("Claude handshake timeout must be positive"));
        return false;
    }

    shutdownTimer_.stop();
    initInfo_ = {};
    observedSessionId_.clear();
    outputBuffer_.clear();
    diagnostics_.clear();
    setState(StreamState::Starting);
    handshakeTimer_.start(handshakeTimeoutMs);
    transport_->start(launchSpec);
    return true;
}

bool ClaudeStreamClient::sendEnvelope(const QJsonObject& envelope) {
    if (state_ != StreamState::Ready) {
        emit protocolWarning(QStringLiteral("Cannot send a Claude envelope before initialization"));
        return false;
    }
    if (envelope.isEmpty()) {
        emit protocolWarning(QStringLiteral("Cannot send an empty Claude envelope"));
        return false;
    }

    QByteArray frame = QJsonDocument(envelope).toJson(QJsonDocument::Compact);
    frame.append('\n');
    if (frame.size() - 1 > maxFrameBytes_) {
        emit protocolWarning(QStringLiteral("Cannot send an oversized Claude JSONL frame"));
        return false;
    }
    if (transport_->write(frame) == frame.size()) {
        return true;
    }
    fail(QStringLiteral("Failed to write to Claude stream"));
    return false;
}

void ClaudeStreamClient::stop() {
    if (state_ == StreamState::Stopped || state_ == StreamState::Stopping) {
        return;
    }
    handshakeTimer_.stop();
    setState(StreamState::Stopping);
    beginProcessShutdown();
}

void ClaudeStreamClient::handleProcessStarted() {
    if (state_ == StreamState::Starting) {
        setState(StreamState::AwaitingInit);
    }
}

void ClaudeStreamClient::handleStandardOutput(const QByteArray& data) {
    if (state_ == StreamState::Stopped || state_ == StreamState::Stopping ||
        state_ == StreamState::Failed) {
        return;
    }

    outputBuffer_.append(data);
    while (true) {
        const qsizetype newline = outputBuffer_.indexOf('\n');
        if (jsonlPayloadBytes(outputBuffer_, newline) > maxFrameBytes_) {
            fail(QStringLiteral("Claude emitted an oversized JSONL frame"));
            return;
        }
        if (newline < 0) {
            return;
        }

        QByteArray line = outputBuffer_.first(newline);
        outputBuffer_.remove(0, newline + 1);
        if (line.endsWith('\r')) {
            line.chop(1);
        }
        if (line.isEmpty()) {
            continue;
        }
        processLine(line);
        if (state_ == StreamState::Failed) {
            return;
        }
    }
}

void ClaudeStreamClient::handleStandardError(const QByteArray& data) {
    diagnostics_.append(data);
    if (diagnostics_.size() > maxDiagnosticBytes_) {
        diagnostics_ = diagnostics_.right(maxDiagnosticBytes_);
    }
    emit diagnosticReceived(QString::fromUtf8(data));
}

void ClaudeStreamClient::handleProcessFinished(int exitCode, process::ExitStatus status) {
    handshakeTimer_.stop();
    shutdownTimer_.stop();
    if (state_ == StreamState::Stopping) {
        setState(StreamState::Stopped);
        return;
    }
    if (state_ == StreamState::Failed || state_ == StreamState::Stopped) {
        return;
    }
    fail(QStringLiteral("Claude exited unexpectedly (code %1, %2)")
             .arg(exitCode)
             .arg(status == process::ExitStatus::Normal ? QStringLiteral("normal exit")
                                                        : QStringLiteral("crash")));
}

void ClaudeStreamClient::handleProcessError(process::Error error, const QString& detail) {
    Q_UNUSED(error)
    if (state_ != StreamState::Stopped && state_ != StreamState::Stopping) {
        fail(detail.isEmpty() ? QStringLiteral("Claude process error") : detail);
    }
}

void ClaudeStreamClient::processLine(const QByteArray& line) {
    const StreamRecord record = parseStreamRecord(line);
    if (record.kind == StreamRecordKind::Malformed) {
        fail(QStringLiteral("Invalid Claude stream record: %1").arg(record.error));
        return;
    }

    const QString sessionId = record.payload.value(QStringLiteral("session_id")).toString();
    if (!sessionId.isEmpty()) {
        if (!observedSessionId_.isEmpty() && observedSessionId_ != sessionId) {
            fail(QStringLiteral("Claude stream record belongs to a different session"));
            return;
        }
        observedSessionId_ = sessionId;
    }

    if (record.kind == StreamRecordKind::SystemInit) {
        if (state_ != StreamState::AwaitingInit) {
            fail(QStringLiteral("Claude emitted a duplicate or out-of-order system/init"));
            return;
        }
        QString error;
        const auto info = parseInitInfo(record, &error);
        if (!info.has_value()) {
            fail(QStringLiteral("Invalid Claude system/init: %1").arg(error));
            return;
        }
        if (!observedSessionId_.isEmpty() && observedSessionId_ != info->sessionId) {
            fail(QStringLiteral("Claude system/init changed the observed session"));
            return;
        }

        observedSessionId_ = info->sessionId;
        initInfo_ = *info;
        handshakeTimer_.stop();
        setState(StreamState::Ready);
        emit recordReceived(record);
        emit initialized(initInfo_);
        return;
    }

    if (state_ == StreamState::AwaitingInit && record.kind != StreamRecordKind::SystemEvent &&
        record.kind != StreamRecordKind::Unknown) {
        fail(QStringLiteral("Claude emitted a turn record before system/init"));
        return;
    }
    emit recordReceived(record);
}

void ClaudeStreamClient::beginProcessShutdown() {
    if (!transport_->isRunning()) {
        if (state_ == StreamState::Stopping) {
            setState(StreamState::Stopped);
        }
        return;
    }
    transport_->closeWriteChannel();
    transport_->terminate();
    if (transport_->isRunning()) {
        shutdownTimer_.start(shutdownTimeoutMs_);
    } else if (state_ == StreamState::Stopping) {
        setState(StreamState::Stopped);
    }
}

void ClaudeStreamClient::setState(StreamState state) {
    if (state_ == state) {
        return;
    }
    state_ = state;
    emit stateChanged(state_);
}

void ClaudeStreamClient::fail(const QString& detail) {
    if (state_ == StreamState::Failed) {
        return;
    }
    handshakeTimer_.stop();
    setState(StreamState::Failed);
    beginProcessShutdown();
    emit failureOccurred(detail);
}

} // namespace snack::agent::claude
