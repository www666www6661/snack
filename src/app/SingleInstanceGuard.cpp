#include "app/SingleInstanceGuard.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLocalSocket>
#include <QStandardPaths>
#include <QThread>
#include <QVariant>
#include <QtEndian>

#include <utility>

namespace snack::app {

SingleInstanceGuard::SingleInstanceGuard(QString serverName, QObject* parent)
    : QObject(parent), serverName_(std::move(serverName)) {
    QString lockDirectory = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    if (lockDirectory.isEmpty()) {
        lockDirectory = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    }
    QDir().mkpath(lockDirectory);
    const QString lockName =
        QString::fromLatin1(
            QCryptographicHash::hash(serverName_.toUtf8(), QCryptographicHash::Sha256).toHex()) +
        QStringLiteral(".lock");
    lockFile_ = std::make_unique<QLockFile>(QDir(lockDirectory).filePath(lockName));
    server_.setSocketOptions(QLocalServer::UserAccessOption);
    connect(&server_, &QLocalServer::newConnection, this, &SingleInstanceGuard::acceptConnection);
}

SingleInstanceGuard::StartResult SingleInstanceGuard::start(const QStringList& arguments,
                                                            QString* error) {
    if (lockFile_->tryLock(0)) {
        QLocalServer::removeServer(serverName_);
        if (server_.listen(serverName_)) {
            return StartResult::Primary;
        }
        lockFile_->unlock();
        if (error != nullptr) {
            *error = server_.errorString();
        }
        return StartResult::Error;
    }

    const QByteArray message = activationMessage(arguments);
    QString sendError;
    for (int attempt = 0; attempt < 3; ++attempt) {
        if (sendToPrimary(message, &sendError)) {
            return StartResult::MessageSent;
        }
        QThread::msleep(50);
    }

    if (lockFile_->removeStaleLockFile() && lockFile_->tryLock(0)) {
        QLocalServer::removeServer(serverName_);
        if (server_.listen(serverName_)) {
            return StartResult::Primary;
        }
        lockFile_->unlock();
    }
    if (error != nullptr) {
        *error =
            QStringLiteral("Another instance owns the application lock but is not responding: %1")
                .arg(sendError);
    }
    return StartResult::Error;
}

void SingleInstanceGuard::acceptConnection() {
    while (QLocalSocket* socket = server_.nextPendingConnection()) {
        connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
        connect(socket, &QLocalSocket::readyRead, this, [this, socket] {
            if (socket->property("snackProcessed").toBool()) {
                return;
            }
            QByteArray buffer = socket->property("snackBuffer").toByteArray();
            buffer.append(socket->readAll());
            if (buffer.size() < static_cast<qsizetype>(sizeof(quint32))) {
                socket->setProperty("snackBuffer", buffer);
                return;
            }
            const auto payloadSize = qFromBigEndian<quint32>(buffer.constData());
            if (payloadSize > 64U * 1024U) {
                socket->disconnectFromServer();
                return;
            }
            socket->setProperty("snackBuffer", buffer);
            const qsizetype frameSize =
                static_cast<qsizetype>(sizeof(quint32)) + static_cast<qsizetype>(payloadSize);
            if (buffer.size() < frameSize) {
                return;
            }
            if (buffer.size() != frameSize) {
                socket->disconnectFromServer();
                return;
            }
            QJsonParseError parseError;
            const auto document =
                QJsonDocument::fromJson(buffer.mid(static_cast<qsizetype>(sizeof(quint32)),
                                                   static_cast<qsizetype>(payloadSize)),
                                        &parseError);
            if (parseError.error != QJsonParseError::NoError) {
                socket->disconnectFromServer();
                return;
            }
            const QJsonObject object = document.object();
            if (object.value(QStringLiteral("version")).toInt() != 1 ||
                object.value(QStringLiteral("action")).toString() != QLatin1String("activate")) {
                socket->disconnectFromServer();
                return;
            }
            std::optional<QString> directory;
            const QString candidate = object.value(QStringLiteral("directory")).toString();
            const QFileInfo info(candidate);
            if (!candidate.isEmpty() && info.exists() && info.isDir()) {
                directory = info.canonicalFilePath();
            }
            socket->setProperty("snackProcessed", true);
            emit activationRequested(directory);
            connect(socket, &QLocalSocket::bytesWritten, socket, [socket](qint64) {
                if (socket->bytesToWrite() == 0) {
                    socket->disconnectFromServer();
                }
            });
            if (socket->write("OK", 2) != 2) {
                socket->disconnectFromServer();
                return;
            }
            socket->flush();
            if (socket->bytesToWrite() == 0) {
                socket->disconnectFromServer();
            }
        });
    }
}

QByteArray SingleInstanceGuard::activationMessage(const QStringList& arguments) const {
    QJsonObject object{{QStringLiteral("version"), 1},
                       {QStringLiteral("action"), QStringLiteral("activate")}};
    if (arguments.size() > 1) {
        const QFileInfo info(arguments.at(1));
        if (info.exists() && info.isDir()) {
            object.insert(QStringLiteral("directory"), info.canonicalFilePath());
        }
    }
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

bool SingleInstanceGuard::sendToPrimary(const QByteArray& message, QString* error) {
    QLocalSocket socket;
    socket.connectToServer(serverName_, QIODevice::ReadWrite);
    if (!socket.waitForConnected(350)) {
        if (error != nullptr) {
            *error = QStringLiteral("connect failed (%1): %2")
                         .arg(static_cast<int>(socket.error()))
                         .arg(socket.errorString());
        }
        return false;
    }
    QByteArray frame(static_cast<qsizetype>(sizeof(quint32)), Qt::Uninitialized);
    qToBigEndian(static_cast<quint32>(message.size()), frame.data());
    frame.append(message);
    const qint64 bytesAccepted = socket.write(frame);
    if (bytesAccepted != frame.size()) {
        if (error != nullptr) {
            *error = QStringLiteral("write accepted %1 of %2 bytes (%3): %4")
                         .arg(bytesAccepted)
                         .arg(frame.size())
                         .arg(static_cast<int>(socket.error()))
                         .arg(socket.errorString());
        }
        return false;
    }
    if (socket.bytesToWrite() > 0 && !socket.waitForBytesWritten(350) &&
        socket.bytesToWrite() > 0) {
        if (error != nullptr) {
            *error = QStringLiteral("write flush failed (%1): %2")
                         .arg(static_cast<int>(socket.error()))
                         .arg(socket.errorString());
        }
        return false;
    }
    if (socket.bytesAvailable() == 0 && !socket.waitForReadyRead(700) &&
        socket.bytesAvailable() == 0) {
        if (error != nullptr) {
            *error = QStringLiteral("primary instance did not acknowledge the request: %1")
                         .arg(socket.errorString());
        }
        return false;
    }
    if (socket.readAll() != QByteArrayLiteral("OK")) {
        if (error != nullptr) {
            *error = QStringLiteral("primary instance returned an invalid acknowledgement");
        }
        return false;
    }
    socket.disconnectFromServer();
    return true;
}

} // namespace snack::app
