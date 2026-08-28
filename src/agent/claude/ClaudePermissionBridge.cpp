#include "agent/claude/ClaudePermissionBridge.h"

#include "domain/DomainTypes.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QLocalSocket>
#include <QRandomGenerator>
#include <QUuid>

namespace snack::agent::claude {
namespace {

QString randomToken() {
    QString token;
    token.reserve(64);
    for (int index = 0; index < 4; ++index) {
        token.append(QString::number(QRandomGenerator::system()->generate64(), 16)
                         .rightJustified(16, QLatin1Char('0')));
    }
    return token;
}

void writeDecision(QLocalSocket* socket, const QString& requestId, const QJsonObject& decision) {
    if (socket == nullptr || socket->state() != QLocalSocket::ConnectedState) {
        return;
    }
    QByteArray frame = QJsonDocument(QJsonObject{{QStringLiteral("requestId"), requestId},
                                                 {QStringLiteral("decision"), decision}})
                           .toJson(QJsonDocument::Compact);
    frame.append('\n');
    socket->write(frame);
    socket->flush();
    socket->disconnectFromServer();
}

QString toolName(const QJsonObject& arguments) {
    QString name = arguments.value(QStringLiteral("tool_name")).toString();
    if (name.isEmpty()) {
        name = arguments.value(QStringLiteral("toolName")).toString();
    }
    return name;
}

} // namespace

ClaudePermissionBridge::ClaudePermissionBridge(QObject* parent) : QObject(parent) {
    server_.setSocketOptions(QLocalServer::UserAccessOption);
    connect(&server_, &QLocalServer::newConnection, this,
            &ClaudePermissionBridge::acceptConnections);
}

ClaudePermissionBridge::~ClaudePermissionBridge() { stop(); }

bool ClaudePermissionBridge::start(QString* error) {
    if (server_.isListening()) {
        return true;
    }
    serverName_ = QStringLiteral("snack-claude-permission-%1")
                      .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    token_ = randomToken();
    QLocalServer::removeServer(serverName_);
    if (!server_.listen(serverName_)) {
        if (error != nullptr) {
            *error = server_.errorString();
        }
        serverName_.clear();
        token_.clear();
        return false;
    }
    return true;
}

void ClaudePermissionBridge::stop() {
    denyAll(QStringLiteral("Claude permission bridge closed"));
    for (QLocalSocket* socket : buffers_.keys()) {
        if (socket != nullptr) {
            socket->abort();
            socket->deleteLater();
        }
    }
    buffers_.clear();
    if (server_.isListening()) {
        server_.close();
    }
    if (!serverName_.isEmpty()) {
        QLocalServer::removeServer(serverName_);
    }
    serverName_.clear();
    token_.clear();
}

bool ClaudePermissionBridge::isListening() const { return server_.isListening(); }

QString ClaudePermissionBridge::serverName() const { return serverName_; }

QString ClaudePermissionBridge::authenticationToken() const { return token_; }

QString ClaudePermissionBridge::permissionToolName() const {
    return QStringLiteral("mcp__snack_permission__permission");
}

QJsonObject ClaudePermissionBridge::mcpConfiguration(const QString& helperExecutable) const {
    if (!server_.isListening() || helperExecutable.trimmed().isEmpty()) {
        return {};
    }
    const QJsonArray arguments{QStringLiteral("--bridge-server"), serverName_,
                               QStringLiteral("--token"), token_};
    const QJsonObject server{{QStringLiteral("type"), QStringLiteral("stdio")},
                             {QStringLiteral("command"), helperExecutable},
                             {QStringLiteral("args"), arguments}};
    const QJsonObject servers{{QStringLiteral("snack_permission"), server}};
    return {{QStringLiteral("mcpServers"), servers}};
}

bool ClaudePermissionBridge::resolve(const QString& requestId, const QJsonObject& decision) {
    const QPointer<QLocalSocket> socket = pending_.take(requestId);
    if (socket.isNull() || decision.isEmpty()) {
        return false;
    }
    writeDecision(socket, requestId, decision);
    return true;
}

void ClaudePermissionBridge::denyAll(const QString& message) {
    const QJsonObject decision{{QStringLiteral("behavior"), QStringLiteral("deny")},
                               {QStringLiteral("message"), message}};
    const QStringList requestIds = pending_.keys();
    for (const QString& requestId : requestIds) {
        static_cast<void>(resolve(requestId, decision));
    }
}

void ClaudePermissionBridge::acceptConnections() {
    while (server_.hasPendingConnections()) {
        QLocalSocket* socket = server_.nextPendingConnection();
        if (socket == nullptr) {
            continue;
        }
        buffers_.insert(socket, {});
        connect(socket, &QLocalSocket::readyRead, this, [this, socket] { readSocket(socket); });
        connect(socket, &QLocalSocket::disconnected, this,
                [this, socket] { removeSocket(socket); });
    }
}

void ClaudePermissionBridge::readSocket(QLocalSocket* socket) {
    auto buffer = buffers_.find(socket);
    if (buffer == buffers_.end()) {
        return;
    }
    buffer->append(socket->readAll());
    const qsizetype newline = buffer->indexOf('\n');
    if ((newline < 0 && buffer->size() > maximumFrameBytes) ||
        (newline >= 0 && newline > maximumFrameBytes)) {
        rejectSocket(socket, QStringLiteral("Permission bridge request is oversized"));
        return;
    }
    if (newline < 0) {
        return;
    }

    const QJsonDocument document = QJsonDocument::fromJson(buffer->first(newline));
    if (!document.isObject()) {
        rejectSocket(socket, QStringLiteral("Permission bridge request is invalid"));
        return;
    }
    const QJsonObject request = document.object();
    const QString requestId = request.value(QStringLiteral("requestId")).toString();
    const QString token = request.value(QStringLiteral("token")).toString();
    const QJsonValue arguments = request.value(QStringLiteral("arguments"));
    if (token.size() != token_.size() || token != token_ || requestId.isEmpty() ||
        !arguments.isObject() || pending_.contains(requestId)) {
        rejectSocket(socket, QStringLiteral("Permission bridge authentication failed"));
        return;
    }
    pending_.insert(requestId, socket);
    emit permissionRequested(requestId, arguments.toObject());
}

void ClaudePermissionBridge::rejectSocket(QLocalSocket* socket, const QString& message) {
    emit protocolWarning(message);
    writeDecision(socket, {},
                  {{QStringLiteral("behavior"), QStringLiteral("deny")},
                   {QStringLiteral("message"), message}});
}

void ClaudePermissionBridge::removeSocket(QLocalSocket* socket) {
    buffers_.remove(socket);
    for (auto iterator = pending_.begin(); iterator != pending_.end();) {
        if (iterator.value() == socket) {
            iterator = pending_.erase(iterator);
        } else {
            ++iterator;
        }
    }
    socket->deleteLater();
}

QJsonObject claudePermissionDecision(domain::ApprovalDecision decision,
                                     const QJsonObject& arguments) {
    const QJsonObject input = arguments.value(QStringLiteral("input")).toObject();
    if (decision == domain::ApprovalDecision::Accept ||
        decision == domain::ApprovalDecision::AcceptForSession) {
        QJsonObject result{{QStringLiteral("behavior"), QStringLiteral("allow")},
                           {QStringLiteral("updatedInput"), input}};
        if (decision == domain::ApprovalDecision::AcceptForSession &&
            arguments.value(QStringLiteral("permission_suggestions")).isArray()) {
            result.insert(QStringLiteral("updatedPermissions"),
                          arguments.value(QStringLiteral("permission_suggestions")));
        }
        return result;
    }
    return {{QStringLiteral("behavior"), QStringLiteral("deny")},
            {QStringLiteral("message"), decision == domain::ApprovalDecision::Cancel
                                            ? QStringLiteral("User cancelled the active turn")
                                            : QStringLiteral("User denied this request")}};
}

QJsonObject claudeApprovalEventPayload(const QString& requestId, const QJsonObject& arguments) {
    const QString name = toolName(arguments);
    const QJsonObject input = arguments.value(QStringLiteral("input")).toObject();
    const bool command = name == QLatin1String("Bash") || name == QLatin1String("Shell");
    QJsonArray decisions{domain::enumName(domain::ApprovalDecision::Accept),
                         domain::enumName(domain::ApprovalDecision::Decline),
                         domain::enumName(domain::ApprovalDecision::Cancel)};
    if (!arguments.value(QStringLiteral("permission_suggestions")).toArray().isEmpty()) {
        decisions.insert(1, domain::enumName(domain::ApprovalDecision::AcceptForSession));
    }
    QJsonObject payload{
        {QStringLiteral("requestId"), requestId},
        {QStringLiteral("itemId"), requestId},
        {QStringLiteral("kind"),
         command ? QStringLiteral("commandExecution") : QStringLiteral("fileChange")},
        {QStringLiteral("reason"), arguments.value(QStringLiteral("reason")).toString()},
        {QStringLiteral("command"), input.value(QStringLiteral("command")).toString()},
        {QStringLiteral("cwd"), input.value(QStringLiteral("cwd")).toString()},
        {QStringLiteral("availableDecisions"), decisions}};
    const QString path = input.value(QStringLiteral("file_path")).toString();
    if (!path.isEmpty()) {
        payload.insert(QStringLiteral("grantRoot"), path);
        payload.insert(QStringLiteral("changes"),
                       QJsonArray{QJsonObject{{QStringLiteral("path"), path},
                                              {QStringLiteral("kind"), name}}});
    }
    return payload;
}

} // namespace snack::agent::claude
