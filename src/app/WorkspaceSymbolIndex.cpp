#include "app/WorkspaceSymbolIndex.h"

#include "app/WorkspaceFileIndex.h"
#include "app/WorkspaceFilePreview.h"

#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>

namespace snack::app {

QList<WorkspaceSymbol> WorkspaceSymbolIndex::build(const QString& workspace, qsizetype maximumFiles,
                                                   qsizetype maximumSymbols,
                                                   qsizetype maximumBytesPerFile) {
    if (maximumFiles <= 0 || maximumSymbols <= 0 || maximumBytesPerFile <= 0) {
        return {};
    }
    static const QRegularExpression declaration(QStringLiteral(
        R"(^\s*(?:class|struct|enum(?:\s+class)?|def|fn|function)\s+([A-Za-z_][A-Za-z0-9_]*))"));
    static const QSet<QString> extensions = {
        QStringLiteral("c"),  QStringLiteral("cc"),  QStringLiteral("cpp"), QStringLiteral("h"),
        QStringLiteral("hh"), QStringLiteral("hpp"), QStringLiteral("py"),  QStringLiteral("rs"),
        QStringLiteral("js"), QStringLiteral("ts")};

    QList<WorkspaceSymbol> symbols;
    const QStringList files = WorkspaceFileIndex::files(workspace, maximumFiles);
    for (const QString& relativePath : files) {
        if (!extensions.contains(QFileInfo(relativePath).suffix().toCaseFolded())) {
            continue;
        }
        QString error;
        const auto preview =
            WorkspaceFilePreviewReader::read(workspace, relativePath, maximumBytesPerFile, &error);
        if (!error.isEmpty()) {
            continue;
        }
        const QStringList lines = preview.text.split(QLatin1Char('\n'));
        for (qsizetype index = 0; index < lines.size() && symbols.size() < maximumSymbols;
             ++index) {
            const auto match = declaration.match(lines.at(index));
            if (match.hasMatch()) {
                symbols.append({.name = match.captured(1),
                                .relativePath = relativePath,
                                .line = static_cast<int>(index + 1)});
            }
        }
        if (symbols.size() >= maximumSymbols) {
            break;
        }
    }
    return symbols;
}

} // namespace snack::app
