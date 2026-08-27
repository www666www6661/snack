#include "workspace/WorkspaceDiff.h"

#include <algorithm>

namespace snack::workspace {

QList<DiffHunk> WorkspaceDiff::between(const QByteArray& baseline, const QByteArray& current,
                                       qsizetype maximumLines) {
    const QStringList oldLines = QString::fromUtf8(baseline).split(QLatin1Char('\n'));
    const QStringList newLines = QString::fromUtf8(current).split(QLatin1Char('\n'));
    if (oldLines == newLines || maximumLines <= 0 ||
        oldLines.size() + newLines.size() > maximumLines) {
        return {};
    }

    qsizetype prefix = 0;
    while (prefix < oldLines.size() && prefix < newLines.size() &&
           oldLines.at(prefix) == newLines.at(prefix)) {
        ++prefix;
    }
    qsizetype suffix = 0;
    while (suffix < oldLines.size() - prefix && suffix < newLines.size() - prefix &&
           oldLines.at(oldLines.size() - suffix - 1) == newLines.at(newLines.size() - suffix - 1)) {
        ++suffix;
    }

    const qsizetype contextStart = std::max<qsizetype>(0, prefix - 3);
    const qsizetype oldEnd = oldLines.size() - suffix;
    const qsizetype newEnd = newLines.size() - suffix;
    DiffHunk hunk{
        .oldStart = static_cast<int>(contextStart + 1),
        .oldCount = static_cast<int>(oldEnd - contextStart + std::min<qsizetype>(3, suffix)),
        .newStart = static_cast<int>(contextStart + 1),
        .newCount = static_cast<int>(newEnd - contextStart + std::min<qsizetype>(3, suffix))};
    for (qsizetype index = contextStart; index < prefix; ++index) {
        hunk.lines.append({.kind = DiffLine::Kind::Context, .text = oldLines.at(index)});
    }
    for (qsizetype index = prefix; index < oldEnd; ++index) {
        hunk.lines.append({.kind = DiffLine::Kind::Removed, .text = oldLines.at(index)});
    }
    for (qsizetype index = prefix; index < newEnd; ++index) {
        hunk.lines.append({.kind = DiffLine::Kind::Added, .text = newLines.at(index)});
    }
    for (qsizetype index = 0; index < std::min<qsizetype>(3, suffix); ++index) {
        hunk.lines.append({.kind = DiffLine::Kind::Context, .text = oldLines.at(oldEnd + index)});
    }
    return {hunk};
}

QString WorkspaceDiff::unifiedText(const QString& relativePath, const QList<DiffHunk>& hunks) {
    if (hunks.isEmpty()) {
        return {};
    }
    QString result = QStringLiteral("--- a/%1\n+++ b/%1\n").arg(relativePath);
    for (const DiffHunk& hunk : hunks) {
        result += QStringLiteral("@@ -%1,%2 +%3,%4 @@\n")
                      .arg(hunk.oldStart)
                      .arg(hunk.oldCount)
                      .arg(hunk.newStart)
                      .arg(hunk.newCount);
        for (const DiffLine& line : hunk.lines) {
            QChar prefix = QLatin1Char(' ');
            if (line.kind == DiffLine::Kind::Added) {
                prefix = QLatin1Char('+');
            } else if (line.kind == DiffLine::Kind::Removed) {
                prefix = QLatin1Char('-');
            }
            result += prefix + line.text + QLatin1Char('\n');
        }
    }
    return result;
}

} // namespace snack::workspace
