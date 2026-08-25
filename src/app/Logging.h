#pragma once

#include <QString>

namespace snack::app {

class Logging final {
  public:
    static bool install(const QString& logDirectory, QString* error = nullptr);
};

} // namespace snack::app
