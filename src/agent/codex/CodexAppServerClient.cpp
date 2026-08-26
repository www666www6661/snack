#include "agent/codex/CodexAppServerClient.h"

#include <cmath>
#include <optional>

namespace snack::agent::codex {
namespace {

std::optional<qint64> numericId(const QJsonValue& value) {
    if (!value.isDouble()) {
        return std::nullopt;
    }
    const double number = value.toDouble();
    constexpr double maxExactJsonInteger = 9'007'199'254'740'991.0;
    if (!std::isfinite(number) || std::floor(number) != number || number < 1.0 ||
        number > maxExactJsonInteger) {
        return std::nullopt;
    }
    return static_cast<qint64>(number);
}

} // namespace

CodexAppServerClient::CodexAppServerClient(process::IProcessTransport* transport, QObject* parent,
                                           qsizetype maxFrameBytes, qsizetype maxDiagnosticBytes)
    : QObject(parent), transport_(transport), maxFrameBytes_(maxFrameBytes),
      maxDiagnosticBytes_(maxDiagnosticBytes) {
    Q_ASSERT(transport_ != nullptr);
    Q_ASSERT(maxFrameBytes_ > 0);
    Q_ASSERT(maxDiagnosticBytes_ > 0);
    handshakeTimer_.setSingleShot(true);
    connect(&handshakeTimer_, &QTimer::timeout, this,
            [this] { fail(QStringLiteral("Codex app-server initialization timed out")); });
    connect(transport_, &process::IProcessTransport::started, this,
            &CodexAppServerClient::handleProcessStarted);
    connect(transport_, &process::IProcessTransport::standardOutputReceived, this,
            &CodexAppServerClient::handleStandardOutput);
    connect(transport_, &process::IProcessTransport::standardErrorReceived, this,
            &CodexAppServerClient::handleStandardError);
    connect(transport_, &process::IProcessTransport::finished, this,
            &CodexAppServerClient::handleProcessFinished);
    connect(transport_, &process::IProcessTransport::errorOccurred, this,
            &CodexAppServerClient::handleProcessError);
}

ConnectionState CodexAppServerClient::state() const { return state_; }

ServerInfo CodexAppServerClient::serverInfo() const { return serverInfo_; }

QByteArray CodexAppServerClient::diagnostics() const { return diagnostics_; }

void CodexAppServerClient::start(const process::LaunchSpec& launchSpec,
                                 const ClientInfo& clientInfo, int handshakeTimeoutMs) {
    if (state_ != ConnectionState::Stopped && state_ != ConnectionState::Failed) {
        emit protocolWarning(QStringLiteral("Codex app-server connection is already active"));
        return;
    }
    if (transport_->isRunning()) {
        emit protocolWarning(QStringLiteral("Codex app-server process is still stopping"));
        return;
    }
    clientInfo_ = clientInfo;
    serverInfo_ = {};
    outputBuffer_.clear();
    diagnostics_.clear();
    pendingRequests_.clear();
    initializeRequestId_ = 0;
    nextRequestId_ = 1;
    setState(ConnectionState::Starting);
    handshakeTimer_.start(handshakeTimeoutMs);
    transport_->start(launchSpec);
}

qint64 CodexAppServerClient::sendRequest(const QString& method, const QJsonObject& params) {
    if (state_ != ConnectionState::Ready) {
        emit protocolWarning(QStringLiteral("Cannot send request before initialization"));
        return 0;
    }
    const qint64 id = nextRequestId_++;
    pendingRequests_.insert(id, method);
    if (!writeMessage(encodeRequest(id, method, params))) {
        pendingRequests_.remove(id);
        return 0;
    }
    return id;
}

void CodexAppServerClient::sendNotification(const QString& method, const QJsonObject& params) {
    if (state_ != ConnectionState::Ready) {
        emit protocolWarning(QStringLiteral("Cannot send notification before initialization"));
        return;
    }
    writeMessage(encodeNotification(method, params));
}

bool CodexAppServerClient::sendResponse(const QJsonValue& id, const QJsonValue& result) {
    if (state_ != ConnectionState::Ready) {
        emit protocolWarning(QStringLiteral("Cannot send response before initialization"));
        return false;
    }
    if (id.isUndefined() || id.isNull() || (!id.isString() && !id.isDouble())) {
        emit protocolWarning(
            QStringLiteral("Cannot respond to a server request with an invalid id"));
        return false;
    }
    return writeMessage(encodeResponse(id, result));
}

void CodexAppServerClient::sendErrorResponse(const QJsonValue& id, int code,
                                             const QString& message) {
    if (state_ != ConnectionState::Ready) {
        emit protocolWarning(QStringLiteral("Cannot send response before initialization"));
        return;
    }
    if (id.isUndefined() || id.isNull() || (!id.isString() && !id.isDouble())) {
        emit protocolWarning(
            QStringLiteral("Cannot respond to a server request with an invalid id"));
        return;
    }
    writeMessage(encodeErrorResponse(id, code, message));
}

void CodexAppServerClient::stop() {
    if (state_ == ConnectionState::Stopped || state_ == ConnectionState::Stopping) {
        return;
    }
    handshakeTimer_.stop();
    setState(ConnectionState::Stopping);
    if (transport_->isRunning()) {
        transport_->closeWriteChannel();
        transport_->terminate();
    } else {
        setState(ConnectionState::Stopped);
    }
}

void CodexAppServerClient::handleProcessStarted() {
    if (state_ != ConnectionState::Starting) {
        return;
    }
    setState(ConnectionState::Initializing);
    const QJsonObject clientInfo{{QStringLiteral("name"), clientInfo_.name},
                                 {QStringLiteral("title"), clientInfo_.title},
                                 {QStringLiteral("version"), clientInfo_.version}};
    initializeRequestId_ = nextRequestId_++;
    pendingRequests_.insert(initializeRequestId_, QStringLiteral("initialize"));
    if (!writeMessage(encodeRequest(initializeRequestId_, QStringLiteral("initialize"),
                                    {{QStringLiteral("clientInfo"), clientInfo}}))) {
        pendingRequests_.remove(initializeRequestId_);
    }
}

void CodexAppServerClient::handleStandardOutput(const QByteArray& data) {
    if (state_ == ConnectionState::Stopped || state_ == ConnectionState::Stopping ||
        state_ == ConnectionState::Failed) {
        return;
    }
    outputBuffer_.append(data);
    while (true) {
        const qsizetype newline = outputBuffer_.indexOf('\n');
        if (newline < 0) {
            if (outputBuffer_.size() > maxFrameBytes_) {
                fail(QStringLiteral("Codex app-server emitted an oversized JSONL frame"));
            }
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
        if (line.size() > maxFrameBytes_) {
            fail(QStringLiteral("Codex app-server emitted an oversized JSONL frame"));
            return;
        }
        processLine(line);
        if (state_ == ConnectionState::Failed) {
            return;
        }
    }
}

void CodexAppServerClient::handleStandardError(const QByteArray& data) {
    diagnostics_.append(data);
    if (diagnostics_.size() > maxDiagnosticBytes_) {
        diagnostics_ = diagnostics_.right(maxDiagnosticBytes_);
    }
    emit diagnosticReceived(QString::fromUtf8(data));
}

void CodexAppServerClient::handleProcessFinished(int exitCode, process::ExitStatus status) {
    handshakeTimer_.stop();
    if (state_ == ConnectionState::Stopping) {
        setState(ConnectionState::Stopped);
        return;
    }
    if (state_ == ConnectionState::Failed || state_ == ConnectionState::Stopped) {
        return;
    }
    fail(QStringLiteral("Codex app-server exited unexpectedly (code %1, %2)")
             .arg(exitCode)
             .arg(status == process::ExitStatus::Normal ? QStringLiteral("normal exit")
                                                        : QStringLiteral("crash")));
}

void CodexAppServerClient::handleProcessError(process::Error error, const QString& detail) {
    Q_UNUSED(error)
    if (state_ != ConnectionState::Stopped && state_ != ConnectionState::Stopping) {
        fail(detail.isEmpty() ? QStringLiteral("Codex app-server process error") : detail);
    }
}

void CodexAppServerClient::processLine(const QByteArray& line) {
    const ProtocolMessage message = parseMessage(line);
    switch (message.kind) {
    case MessageKind::Response:
        processResponse(message);
        break;
    case MessageKind::Notification:
        emit notificationReceived(message.method, message.params, message.raw);
        break;
    case MessageKind::Request:
        emit serverRequestReceived(message.id, message.method, message.params, message.raw);
        break;
    case MessageKind::Invalid:
        fail(QStringLiteral("Invalid Codex app-server message: %1").arg(message.errorDetail));
        break;
    }
}

void CodexAppServerClient::processResponse(const ProtocolMessage& message) {
    const auto id = numericId(message.id);
    if (!id.has_value() || !pendingRequests_.contains(*id)) {
        emit protocolWarning(QStringLiteral("Received a response with an unknown request id"));
        return;
    }
    const QString method = pendingRequests_.take(*id);
    if (message.raw.contains(QStringLiteral("error"))) {
        const int code = message.error.value(QStringLiteral("code")).toInt();
        const QString errorMessage = message.error.value(QStringLiteral("message")).toString();
        if (*id == initializeRequestId_) {
            fail(QStringLiteral("Codex initialization failed: %1").arg(errorMessage));
            return;
        }
        emit requestFailed(*id, method, code, errorMessage);
        return;
    }

    if (*id == initializeRequestId_) {
        if (!message.result.isObject()) {
            fail(QStringLiteral("Codex initialization returned an invalid result"));
            return;
        }
        const QJsonObject result = message.result.toObject();
        serverInfo_ = {.userAgent = result.value(QStringLiteral("userAgent")).toString(),
                       .codexHome = result.value(QStringLiteral("codexHome")).toString(),
                       .platformFamily = result.value(QStringLiteral("platformFamily")).toString(),
                       .platformOs = result.value(QStringLiteral("platformOs")).toString()};
        if (!writeMessage(encodeNotification(QStringLiteral("initialized"), {}))) {
            return;
        }
        handshakeTimer_.stop();
        setState(ConnectionState::Ready);
        emit handshakeCompleted(serverInfo_);
        return;
    }

    emit responseReceived(*id, method, message.result);
}

bool CodexAppServerClient::writeMessage(const QByteArray& message) {
    QByteArray frame = message;
    frame.append('\n');
    if (transport_->write(frame) == frame.size()) {
        return true;
    }
    fail(QStringLiteral("Failed to write to Codex app-server"));
    return false;
}

void CodexAppServerClient::setState(ConnectionState state) {
    if (state_ == state) {
        return;
    }
    state_ = state;
    emit stateChanged(state_);
}

void CodexAppServerClient::fail(const QString& detail) {
    if (state_ == ConnectionState::Failed) {
        return;
    }
    handshakeTimer_.stop();
    setState(ConnectionState::Failed);
    if (transport_->isRunning()) {
        transport_->closeWriteChannel();
        transport_->terminate();
    }
    emit failureOccurred(detail);
}

} // namespace snack::agent::codex
