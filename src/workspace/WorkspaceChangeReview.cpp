#include "workspace/WorkspaceChangeReview.h"

#include "app/WorkspacePathPolicy.h"

#include <QCryptographicHash>
#include <QFile>
#include <QSaveFile>

namespace snack::workspace {
namespace {

std::optional<QByteArray> currentContent(const WorkspaceSnapshot& snapshot,
                                         const QString& relativePath, QString* error) {
    const auto path =
        app::WorkspacePathPolicy::resolveExisting(snapshot.workspace(), relativePath, error);
    if (!path.has_value()) {
        return std::nullopt;
    }
    QFile file(*path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error != nullptr) {
            *error = file.errorString();
        }
        return std::nullopt;
    }
    return file.readAll();
}

} // namespace

bool WorkspaceChangeReview::acceptCurrent(WorkspaceSnapshot& snapshot, const QString& relativePath,
                                          QString* error) {
    const auto content = currentContent(snapshot, relativePath, error);
    if (!content.has_value()) {
        return false;
    }
    snapshot.setEntry(relativePath,
                      {.content = *content,
                       .sha256 = QCryptographicHash::hash(*content, QCryptographicHash::Sha256)});
    return true;
}

bool WorkspaceChangeReview::rejectCurrent(const WorkspaceSnapshot& snapshot,
                                          const QString& relativePath,
                                          const QByteArray& expectedCurrentSha256, QString* error) {
    const auto baseline = snapshot.entry(relativePath);
    const auto content = currentContent(snapshot, relativePath, error);
    if (!baseline.has_value() || !content.has_value()) {
        return false;
    }
    if (QCryptographicHash::hash(*content, QCryptographicHash::Sha256) != expectedCurrentSha256) {
        if (error != nullptr) {
            *error = QStringLiteral("The file changed externally after the diff was created");
        }
        return false;
    }
    const auto path =
        app::WorkspacePathPolicy::resolveExisting(snapshot.workspace(), relativePath, error);
    if (!path.has_value()) {
        return false;
    }
    QSaveFile output(*path);
    if (!output.open(QIODevice::WriteOnly) ||
        output.write(baseline->content) != baseline->content.size() || !output.commit()) {
        if (error != nullptr) {
            *error = output.errorString();
        }
        return false;
    }
    return true;
}

} // namespace snack::workspace
