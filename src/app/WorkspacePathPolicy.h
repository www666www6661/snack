#pragma once

#include <QString>

#include <optional>

namespace snack::app {

class WorkspacePathPolicy final {
  public:
    [[nodiscard]] static std::optional<QString>
    resolveExisting(const QString& workspace, const QString& candidate, QString* error = nullptr);
    [[nodiscard]] static QString identityKey(const QString& existingPath);
};

} // namespace snack::app
