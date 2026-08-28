#include <QCommandLineParser>
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QUuid>

#include <cstdio>

namespace {

QJsonObject bridgeDecision(const QString& serverName, const QString& token,
                           const QJsonObject& arguments) {
    QLocalSocket socket;
    socket.connectToServer(serverName, QIODevice::ReadWrite);
    if (!socket.waitForConnected(3000)) {
        return {{QStringLiteral("behavior"), QStringLiteral("deny")},
                {QStringLiteral("message"), QStringLiteral("Snack permission bridge unavailable")}};
    }
    const QString requestId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QByteArray frame = QJsonDocument(QJsonObject{{QStringLiteral("token"), token},
                                                 {QStringLiteral("requestId"), requestId},
                                                 {QStringLiteral("arguments"), arguments}})
                           .toJson(QJsonDocument::Compact);
    frame.append('\n');
    if (socket.write(frame) != frame.size() ||
        (socket.bytesToWrite() > 0 && !socket.waitForBytesWritten(3000))) {
        return {
            {QStringLiteral("behavior"), QStringLiteral("deny")},
            {QStringLiteral("message"), QStringLiteral("Snack permission bridge write failed")}};
    }
    while (!socket.canReadLine() && socket.waitForReadyRead(300'000)) {
    }
    const QJsonObject response = QJsonDocument::fromJson(socket.readLine()).object();
    const QJsonObject decision = response.value(QStringLiteral("decision")).toObject();
    if (response.value(QStringLiteral("requestId")).toString() != requestId || decision.isEmpty()) {
        return {{QStringLiteral("behavior"), QStringLiteral("deny")},
                {QStringLiteral("message"), QStringLiteral("Snack permission bridge timed out")}};
    }
    return decision;
}

QJsonObject resultFor(const QJsonObject& request, const QString& serverName, const QString& token) {
    const QString method = request.value(QStringLiteral("method")).toString();
    if (method == QLatin1String("initialize")) {
        const QString protocolVersion = request.value(QStringLiteral("params"))
                                            .toObject()
                                            .value(QStringLiteral("protocolVersion"))
                                            .toString(QStringLiteral("2025-06-18"));
        return {
            {QStringLiteral("protocolVersion"), protocolVersion},
            {QStringLiteral("capabilities"), QJsonObject{{QStringLiteral("tools"), QJsonObject{}}}},
            {QStringLiteral("serverInfo"),
             QJsonObject{{QStringLiteral("name"), QStringLiteral("snack-claude-permission")},
                         {QStringLiteral("version"), QStringLiteral(SNACK_VERSION)}}}};
    }
    if (method == QLatin1String("tools/list")) {
        const QJsonObject schema{{QStringLiteral("type"), QStringLiteral("object")},
                                 {QStringLiteral("additionalProperties"), true}};
        const QJsonObject tool{{QStringLiteral("name"), QStringLiteral("permission")},
                               {QStringLiteral("description"),
                                QStringLiteral("Ask the Snack GUI to approve a Claude tool")},
                               {QStringLiteral("inputSchema"), schema}};
        return {{QStringLiteral("tools"), QJsonArray{tool}}};
    }
    if (method == QLatin1String("tools/call")) {
        const QJsonObject params = request.value(QStringLiteral("params")).toObject();
        QJsonObject decision;
        if (params.value(QStringLiteral("name")) != QLatin1String("permission")) {
            decision = {{QStringLiteral("behavior"), QStringLiteral("deny")},
                        {QStringLiteral("message"), QStringLiteral("Unknown permission tool")}};
        } else {
            decision = bridgeDecision(serverName, token,
                                      params.value(QStringLiteral("arguments")).toObject());
        }
        const QJsonObject content{
            {QStringLiteral("type"), QStringLiteral("text")},
            {QStringLiteral("text"),
             QString::fromUtf8(QJsonDocument(decision).toJson(QJsonDocument::Compact))}};
        return {{QStringLiteral("content"), QJsonArray{content}},
                {QStringLiteral("isError"), false}};
    }
    if (method == QLatin1String("ping")) {
        return {};
    }
    return {};
}

} // namespace

int main(int argc, char* argv[]) {
    QCoreApplication application(argc, argv);
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOption({QStringLiteral("bridge-server"), QStringLiteral("Snack bridge server"),
                      QStringLiteral("name")});
    parser.addOption(
        {QStringLiteral("token"), QStringLiteral("Snack bridge token"), QStringLiteral("token")});
    parser.process(application);
    const QString serverName = parser.value(QStringLiteral("bridge-server"));
    const QString token = parser.value(QStringLiteral("token"));
    if (serverName.isEmpty() || token.isEmpty()) {
        return 2;
    }

    QFile input;
    QFile output;
    if (!input.open(stdin, QIODevice::ReadOnly) || !output.open(stdout, QIODevice::WriteOnly)) {
        return 3;
    }
    while (!input.atEnd()) {
        const QByteArray line = input.readLine().trimmed();
        const QJsonDocument document = QJsonDocument::fromJson(line);
        if (!document.isObject()) {
            continue;
        }
        const QJsonObject request = document.object();
        if (!request.contains(QStringLiteral("id"))) {
            continue;
        }
        QJsonObject response{{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
                             {QStringLiteral("id"), request.value(QStringLiteral("id"))}};
        const QString method = request.value(QStringLiteral("method")).toString();
        if (method == QLatin1String("initialize") || method == QLatin1String("tools/list") ||
            method == QLatin1String("tools/call") || method == QLatin1String("ping")) {
            response.insert(QStringLiteral("result"), resultFor(request, serverName, token));
        } else {
            response.insert(
                QStringLiteral("error"),
                QJsonObject{{QStringLiteral("code"), -32601},
                            {QStringLiteral("message"), QStringLiteral("Method not found")}});
        }
        output.write(QJsonDocument(response).toJson(QJsonDocument::Compact));
        output.write("\n");
        output.flush();
    }
    return 0;
}
