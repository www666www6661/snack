#pragma once

#include "domain/DomainTypes.h"

#include <QHash>

#include <optional>

namespace snack::domain {

class PromptTemplateEngine final {
  public:
    [[nodiscard]] static QStringList parameters(const PromptTemplate& promptTemplate,
                                                QString* error = nullptr);
    [[nodiscard]] static std::optional<QString> render(const PromptTemplate& promptTemplate,
                                                       const QHash<QString, QString>& values,
                                                       QString* error = nullptr);
    [[nodiscard]] static bool validate(const PromptTemplate& promptTemplate,
                                       QString* error = nullptr);
};

} // namespace snack::domain
