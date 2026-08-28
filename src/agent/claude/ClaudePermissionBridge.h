#pragma once

#include "domain/DomainTypes.h"

#include <QHash>
#include <QJsonObject>
#include <QLocalServer>
#include <QObject>
#include <QPointer>

class QLocalSocket;

namespace snack::agent::claude {

class ClaudePermissionBridge final : public QObject {
    Q_OBJECT

  public:
    static constexpr qsizetype maximumFrameBytes = 1024 * 1024;

    explicit ClaudePermissionBridge(QObject* parent = nullptr);
    ~ClaudePermissionBridge() override;

    bool start(QString* error = nullptr);
    void stop();
    [[nodiscard]] bool isListening() const;
    [[nodiscard]] QString serverName() const;
    [[nodiscard]] QString authenticationToken() const;
    [[nodiscard]] QString permissionToolName() const;
    [[nodiscard]] QJsonObject mcpConfiguration(const QString& helperExecutable) const;
    [[nodiscard]] bool resolve(const QString& requestId, const QJsonObject& decision);
    void denyAll(const QString& message);

  signals:
    void permissionRequested(const QString& requestId, const QJsonObject& arguments);
    void protocolWarning(const QString& detail);

  private:
    void acceptConnections();
    void readSocket(QLocalSocket* socket);
    void rejectSocket(QLocalSocket* socket, const QString& message);
    void removeSocket(QLocalSocket* socket);

    QLocalServer server_;
    QString serverName_;
    QString token_;
    QHash<QLocalSocket*, QByteArray> buffers_;
    QHash<QString, QPointer<QLocalSocket>> pending_;
};

[[nodiscard]] QJsonObject claudePermissionDecision(domain::ApprovalDecision decision,
                                                   const QJsonObject& arguments);
[[nodiscard]] QJsonObject claudeApprovalEventPayload(const QString& requestId,
                                                     const QJsonObject& arguments);

} // namespace snack::agent::claude
