#include "app/WorkspaceExternalOpener.h"

#include "app/WorkspacePathPolicy.h"

namespace snack::app {

bool WorkspaceExternalOpener::open(const QString& workspace, const QString& relativePath,
                                   const OpenUrl& openUrl, QString* error) {
    const auto resolved = WorkspacePathPolicy::resolveExisting(workspace, relativePath, error);
    if (!resolved.has_value()) {
        return false;
    }
    if (!openUrl || !openUrl(QUrl::fromLocalFile(*resolved))) {
        if (error != nullptr) {
            *error = QStringLiteral("The operating system could not open the file");
        }
        return false;
    }
    return true;
}

} // namespace snack::app
