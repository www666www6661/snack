#pragma once

#include "agent/claude/ClaudeStreamProtocol.h"
#include "agent/process/IProcessTransport.h"

#include <QByteArray>
#include <QObject>
#include <QTimer>

namespace snack::agent::claude {

enum class StreamState { Stopped, Starting, AwaitingInit, Ready, Failed, Stopping };

class ClaudeStreamClient final : public QObject {
    Q_OBJECT

  public:
    static constexpr qsizetype defaultMaximumFrameBytes = 4 * 1024 * 1024;
    static constexpr qsizetype defaultMaximumDiagnosticBytes = 64 * 1024;
    static constexpr int defaultShutdownTimeoutMs = 2'000;

    explicit ClaudeStreamClient(process::IProcessTransport* transport, QObject* parent = nullptr,
                                qsizetype maxFrameBytes = defaultMaximumFrameBytes,
                                qsizetype maxDiagnosticBytes = defaultMaximumDiagnosticBytes,
                                int shutdownTimeoutMs = defaultShutdownTimeoutMs);

    [[nodiscard]] StreamState state() const;
    [[nodiscard]] InitInfo initInfo() const;
    [[nodiscard]] QByteArray diagnostics() const;

    bool start(const process::LaunchSpec& launchSpec, int handshakeTimeoutMs = 5000);
    [[nodiscard]] bool sendEnvelope(const QJsonObject& envelope);
    void stop();

  signals:
    void stateChanged(snack::agent::claude::StreamState state);
    void initialized(const snack::agent::claude::InitInfo& info);
    void recordReceived(const snack::agent::claude::StreamRecord& record);
    void diagnosticReceived(const QString& text);
    void protocolWarning(const QString& detail);
    void failureOccurred(const QString& detail);

  private:
    void handleProcessStarted();
    void handleStandardOutput(const QByteArray& data);
    void handleStandardError(const QByteArray& data);
    void handleProcessFinished(int exitCode, process::ExitStatus status);
    void handleProcessError(process::Error error, const QString& detail);
    void processLine(const QByteArray& line);
    void beginProcessShutdown();
    void setState(StreamState state);
    void fail(const QString& detail);

    process::IProcessTransport* transport_{nullptr};
    QTimer handshakeTimer_;
    QTimer shutdownTimer_;
    StreamState state_{StreamState::Stopped};
    InitInfo initInfo_;
    QString observedSessionId_;
    QByteArray outputBuffer_;
    QByteArray diagnostics_;
    qsizetype maxFrameBytes_;
    qsizetype maxDiagnosticBytes_;
    int shutdownTimeoutMs_;
};

} // namespace snack::agent::claude

Q_DECLARE_METATYPE(snack::agent::claude::StreamState)
