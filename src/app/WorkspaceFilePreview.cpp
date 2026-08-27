#include "app/WorkspaceFilePreview.h"

#include "app/WorkspacePathPolicy.h"

#include <QFile>

namespace snack::app {
namespace {

void setError(QString* error, const QString& value) {
    if (error != nullptr) {
        *error = value;
    }
}

} // namespace

WorkspaceFilePreview WorkspaceFilePreviewReader::read(const QString& workspace,
                                                      const QString& relativePath,
                                                      qsizetype maximumBytes, QString* error) {
    if (maximumBytes <= 0) {
        setError(error, QStringLiteral("Preview byte limit must be positive"));
        return {};
    }
    const auto resolved = WorkspacePathPolicy::resolveExisting(workspace, relativePath, error);
    if (!resolved.has_value()) {
        return {};
    }
    QFile file(*resolved);
    if (!file.open(QIODevice::ReadOnly)) {
        setError(error, file.errorString());
        return {};
    }
    QByteArray bytes = file.read(maximumBytes + 1);
    const bool truncated = bytes.size() > maximumBytes;
    if (truncated) {
        bytes.truncate(maximumBytes);
    }
    if (bytes.contains('\0')) {
        setError(error, QStringLiteral("Binary files are not shown in the text preview"));
        return {};
    }
    return {.text = QString::fromUtf8(bytes), .truncated = truncated};
}

} // namespace snack::app
