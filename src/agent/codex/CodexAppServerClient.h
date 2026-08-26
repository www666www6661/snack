#pragma once

#include "agent/codex/CodexProtocol.h"
#include "agent/process/IProcessTransport.h"

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QTimer>

namespace snack::agent::codex {

enum class ConnectionState { Stopped, Starting, Initializing, Ready, Failed, Stopping };

struct ClientInfo {
    QString name{QStringLiteral("snack")};
    QString title{QStringLiteral("Snack")};
    QString version{QStringLiteral(SNACK_VERSION)};
};

struct ServerInfo {
    QString userAgent;
    QString codexHome;
    QString platformFamily;
    QString platformOs;
};

class CodexAppServerClient final : public QObject {
    Q_OBJECT

  public:
    static constexpr qsizetype defaultMaximumFrameBytes = 4 * 1024 * 1024;
    static constexpr qsizetype defaultMaximumDiagnosticBytes = 64 * 1024;
    static constexpr int defaultRequestTimeoutMs = 15'000;

    explicit CodexAppServerClient(process::IProcessTransport* transport, QObject* parent = nullptr,
                                  qsizetype maxFrameBytes = defaultMaximumFrameBytes,
                                  qsizetype maxDiagnosticBytes = defaultMaximumDiagnosticBytes,
                                  int requestTimeoutMs = defaultRequestTimeoutMs);

    [[nodiscard]] ConnectionState state() const;
    [[nodiscard]] ServerInfo serverInfo() const;
    [[nodiscard]] QByteArray diagnostics() const;

    bool start(const process::LaunchSpec& launchSpec, const ClientInfo& clientInfo = {},
               int handshakeTimeoutMs = 5000);
    [[nodiscard]] qint64 sendRequest(const QString& method, const QJsonObject& params = {});
    void sendNotification(const QString& method, const QJsonObject& params = {});
    [[nodiscard]] bool sendResponse(const QJsonValue& id, const QJsonValue& result);
    void sendErrorResponse(const QJsonValue& id, int code, const QString& message);
    void stop();

  signals:
    void stateChanged(snack::agent::codex::ConnectionState state);
    void handshakeCompleted(const snack::agent::codex::ServerInfo& serverInfo);
    void responseReceived(qint64 id, const QString& method, const QJsonValue& result);
    void requestFailed(qint64 id, const QString& method, int code, const QString& message);
    void notificationReceived(const QString& method, const QJsonValue& params,
                              const QJsonObject& raw);
    void serverRequestReceived(const QJsonValue& id, const QString& method,
                               const QJsonValue& params, const QJsonObject& raw);
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
    void processResponse(const ProtocolMessage& message);
    void expireRequest(qint64 id);
    [[nodiscard]] QString takePendingRequest(qint64 id);
    void clearPendingRequests();
    bool writeMessage(const QByteArray& message);
    void setState(ConnectionState state);
    void fail(const QString& detail);

    process::IProcessTransport* transport_{nullptr};
    QTimer handshakeTimer_;
    ConnectionState state_{ConnectionState::Stopped};
    ClientInfo clientInfo_;
    ServerInfo serverInfo_;
    QByteArray outputBuffer_;
    QByteArray diagnostics_;
    QHash<qint64, QString> pendingRequests_;
    QHash<qint64, QTimer*> requestTimers_;
    qint64 initializeRequestId_{0};
    qint64 nextRequestId_{1};
    qsizetype maxFrameBytes_;
    qsizetype maxDiagnosticBytes_;
    int requestTimeoutMs_;
};

} // namespace snack::agent::codex

Q_DECLARE_METATYPE(snack::agent::codex::ConnectionState)
Q_DECLARE_METATYPE(snack::agent::codex::ServerInfo)
