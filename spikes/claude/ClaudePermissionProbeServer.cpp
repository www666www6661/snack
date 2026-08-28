#include <QCommandLineParser>
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <cstdio>

namespace {

QJsonObject responseFor(const QJsonObject& request) {
    const QJsonValue id = request.value(QStringLiteral("id"));
    const QString method = request.value(QStringLiteral("method")).toString();
    QJsonObject result;
    if (method == QLatin1String("initialize")) {
        const QString protocolVersion = request.value(QStringLiteral("params"))
                                            .toObject()
                                            .value(QStringLiteral("protocolVersion"))
                                            .toString(QStringLiteral("2025-06-18"));
        result = {
            {QStringLiteral("protocolVersion"), protocolVersion},
            {QStringLiteral("capabilities"), QJsonObject{{QStringLiteral("tools"), QJsonObject{}}}},
            {QStringLiteral("serverInfo"),
             QJsonObject{{QStringLiteral("name"), QStringLiteral("snack-claude-permission-probe")},
                         {QStringLiteral("version"), QStringLiteral("0.1.0")}}}};
    } else if (method == QLatin1String("tools/list")) {
        result = {{QStringLiteral("tools"),
                   QJsonArray{QJsonObject{
                       {QStringLiteral("name"), QStringLiteral("permission")},
                       {QStringLiteral("description"),
                        QStringLiteral("Synthetic permission bridge probe; never approves tools")},
                       {QStringLiteral("inputSchema"),
                        QJsonObject{{QStringLiteral("type"), QStringLiteral("object")},
                                    {QStringLiteral("additionalProperties"), true}}}}}}};
    } else if (method == QLatin1String("tools/call")) {
        result = {{QStringLiteral("content"),
                   QJsonArray{QJsonObject{
                       {QStringLiteral("type"), QStringLiteral("text")},
                       {QStringLiteral("text"),
                        QStringLiteral(R"({"behavior":"deny","message":"Probe only"})")}}}},
                  {QStringLiteral("isError"), false}};
    } else if (method == QLatin1String("ping")) {
        result = {};
    } else {
        return {{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
                {QStringLiteral("id"), id},
                {QStringLiteral("error"),
                 QJsonObject{{QStringLiteral("code"), -32601},
                             {QStringLiteral("message"), QStringLiteral("Method not found")}}}};
    }
    return {{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
            {QStringLiteral("id"), id},
            {QStringLiteral("result"), result}};
}

} // namespace

int main(int argc, char* argv[]) {
    QCoreApplication application(argc, argv);
    QCommandLineParser parser;
    parser.addHelpOption();
    const QCommandLineOption eventLogOption(QStringLiteral("event-log"),
                                            QStringLiteral("Write normalized method names"),
                                            QStringLiteral("path"));
    parser.addOption(eventLogOption);
    parser.process(application);

    QFile input;
    QFile output;
    if (!input.open(stdin, QIODevice::ReadOnly) || !output.open(stdout, QIODevice::WriteOnly)) {
        return 2;
    }
    QFile eventLog(parser.value(eventLogOption));
    if (!eventLog.fileName().isEmpty() &&
        !eventLog.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        return 3;
    }

    while (!input.atEnd()) {
        const QByteArray line = input.readLine().trimmed();
        if (line.isEmpty()) {
            continue;
        }
        const QJsonDocument document = QJsonDocument::fromJson(line);
        if (!document.isObject()) {
            continue;
        }
        const QJsonObject request = document.object();
        const QString method = request.value(QStringLiteral("method")).toString();
        if (!method.isEmpty() && eventLog.isOpen()) {
            eventLog.write(method.toUtf8());
            eventLog.write("\n");
            eventLog.flush();
        }
        if (!request.contains(QStringLiteral("id"))) {
            continue;
        }
        output.write(QJsonDocument(responseFor(request)).toJson(QJsonDocument::Compact));
        output.write("\n");
        output.flush();
    }
    return 0;
}
