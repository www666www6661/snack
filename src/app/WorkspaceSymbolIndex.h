#pragma once

#include <QList>
#include <QString>

namespace snack::app {

struct WorkspaceSymbol {
    QString name;
    QString relativePath;
    int line{0};
};

class WorkspaceSymbolIndex final {
  public:
    [[nodiscard]] static QList<WorkspaceSymbol> build(const QString& workspace,
                                                      qsizetype maximumFiles = 2000,
                                                      qsizetype maximumSymbols = 10000,
                                                      qsizetype maximumBytesPerFile = 1024 * 1024);
};

} // namespace snack::app
