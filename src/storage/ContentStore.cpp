#include "storage/ContentStore.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>

#include <utility>

namespace snack::storage {
namespace {

bool isValidHash(const QString& hash) {
    static const QRegularExpression pattern(QStringLiteral("^[0-9a-f]{64}$"));
    return pattern.match(hash).hasMatch();
}

} // namespace

FileContentStore::FileContentStore(QString rootDirectory)
    : rootDirectory_(std::move(rootDirectory)) {}

QString FileContentStore::put(const QByteArray& content, QString* error) {
    const QString hash =
        QString::fromLatin1(QCryptographicHash::hash(content, QCryptographicHash::Sha256).toHex());
    const QString targetPath = pathForHash(hash);
    if (QFileInfo::exists(targetPath)) {
        QFile existing(targetPath);
        if (existing.open(QIODevice::ReadOnly) && existing.readAll() == content) {
            return hash;
        }
    }

    if (!QDir().mkpath(QFileInfo(targetPath).absolutePath())) {
        if (error != nullptr) {
            *error = QStringLiteral("Cannot create content store directory");
        }
        return {};
    }

    QSaveFile file(targetPath);
    if (!file.open(QIODevice::WriteOnly) || file.write(content) != content.size() ||
        !file.commit()) {
        if (error != nullptr) {
            *error = QStringLiteral("Cannot write content blob: %1").arg(file.errorString());
        }
        return {};
    }
    return hash;
}

QByteArray FileContentStore::get(const QString& hash, QString* error) const {
    if (!isValidHash(hash)) {
        if (error != nullptr) {
            *error = QStringLiteral("Invalid content hash");
        }
        return {};
    }
    QFile file(pathForHash(hash));
    if (!file.open(QIODevice::ReadOnly)) {
        if (error != nullptr) {
            *error = QStringLiteral("Cannot read content blob: %1").arg(file.errorString());
        }
        return {};
    }
    const QByteArray content = file.readAll();
    const QString actualHash =
        QString::fromLatin1(QCryptographicHash::hash(content, QCryptographicHash::Sha256).toHex());
    if (actualHash != hash) {
        if (error != nullptr) {
            *error = QStringLiteral("Content blob failed SHA-256 verification");
        }
        return {};
    }
    return content;
}

bool FileContentStore::contains(const QString& hash) const {
    return isValidHash(hash) && QFileInfo::exists(pathForHash(hash));
}

QString FileContentStore::pathForHash(const QString& hash) const {
    if (!isValidHash(hash)) {
        return {};
    }
    return QDir(rootDirectory_)
        .filePath(hash.left(2) + QLatin1Char('/') + hash.mid(2, 2) + QLatin1Char('/') + hash);
}

} // namespace snack::storage
