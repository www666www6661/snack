#pragma once

#include <QList>
#include <QString>

namespace snack::workspace {

struct DiffLine {
    enum class Kind { Context, Added, Removed };
    Kind kind{Kind::Context};
    QString text;
};

struct DiffHunk {
    int oldStart{0};
    int oldCount{0};
    int newStart{0};
    int newCount{0};
    QList<DiffLine> lines;
};

class WorkspaceDiff final {
  public:
    [[nodiscard]] static QList<DiffHunk>
    between(const QByteArray& baseline, const QByteArray& current, qsizetype maximumLines = 4000);
    [[nodiscard]] static QString unifiedText(const QString& relativePath,
                                             const QList<DiffHunk>& hunks);
};

} // namespace snack::workspace
