#include "app/Logging.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QMutex>
#include <QTextStream>

#include <cstdio>

namespace snack::app {
namespace {

QFile logFile;
QMutex logMutex;

void messageHandler(QtMsgType type, const QMessageLogContext& context, const QString& message) {
    QMutexLocker locker(&logMutex);
    const char* level = "INFO";
    switch (type) {
    case QtDebugMsg:
        level = "DEBUG";
        break;
    case QtInfoMsg:
        level = "INFO";
        break;
    case QtWarningMsg:
        level = "WARN";
        break;
    case QtCriticalMsg:
        level = "ERROR";
        break;
    case QtFatalMsg:
        level = "FATAL";
        break;
    }

    if (logFile.isOpen()) {
        QTextStream stream(&logFile);
        stream << QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs) << ' ' << level << ' '
               << (context.category != nullptr ? context.category : "default") << ' ' << message
               << Qt::endl;
    }
    std::fprintf(stderr, "%s %s\n", level, message.toLocal8Bit().constData());
}

} // namespace

bool Logging::install(const QString& logDirectory, QString* error) {
    if (!QDir().mkpath(logDirectory)) {
        if (error != nullptr) {
            *error = QStringLiteral("Cannot create log directory: %1").arg(logDirectory);
        }
        return false;
    }
    logFile.setFileName(QDir(logDirectory).filePath(QStringLiteral("snack.log")));
    if (logFile.size() > 2 * 1024 * 1024) {
        QFile::remove(logFile.fileName() + QStringLiteral(".1"));
        QFile::rename(logFile.fileName(), logFile.fileName() + QStringLiteral(".1"));
    }
    if (!logFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        if (error != nullptr) {
            *error = logFile.errorString();
        }
        return false;
    }
    qInstallMessageHandler(messageHandler);
    return true;
}

} // namespace snack::app
