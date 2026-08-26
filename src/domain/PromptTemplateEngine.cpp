#include "domain/PromptTemplateEngine.h"

#include <QRegularExpression>

namespace snack::domain {
namespace {

void setError(QString* error, const QString& message) {
    if (error != nullptr) {
        *error = message;
    }
}

} // namespace

QStringList PromptTemplateEngine::parameters(const PromptTemplate& promptTemplate, QString* error) {
    static const QRegularExpression parameterName(
        QStringLiteral("^[\\p{L}][\\p{L}\\p{N}_-]{0,31}$"),
        QRegularExpression::UseUnicodePropertiesOption);
    QStringList result;
    qsizetype offset = 0;
    while (offset < promptTemplate.content.size()) {
        const qsizetype opening = promptTemplate.content.indexOf(QStringLiteral("{{"), offset);
        const qsizetype strayClosing = promptTemplate.content.indexOf(QStringLiteral("}}"), offset);
        if (strayClosing >= 0 && (opening < 0 || strayClosing < opening)) {
            setError(error, QStringLiteral("Template contains an unmatched closing delimiter"));
            return {};
        }
        if (opening < 0) {
            break;
        }
        const qsizetype closing = promptTemplate.content.indexOf(QStringLiteral("}}"), opening + 2);
        if (closing < 0) {
            setError(error, QStringLiteral("Template contains an unmatched opening delimiter"));
            return {};
        }
        const QString name = promptTemplate.content.sliced(opening + 2, closing - opening - 2);
        if (!parameterName.match(name).hasMatch()) {
            setError(error, QStringLiteral("Template parameter name is invalid: %1").arg(name));
            return {};
        }
        if (!result.contains(name)) {
            result.append(name);
            if (result.size() > 20) {
                setError(error, QStringLiteral("Template cannot contain more than 20 parameters"));
                return {};
            }
        }
        offset = closing + 2;
    }
    if (promptTemplate.content.indexOf(QStringLiteral("}}"), offset) >= 0) {
        setError(error, QStringLiteral("Template contains an unmatched closing delimiter"));
        return {};
    }
    return result;
}

std::optional<QString> PromptTemplateEngine::render(const PromptTemplate& promptTemplate,
                                                    const QHash<QString, QString>& values,
                                                    QString* error) {
    QString parseError;
    const QStringList names = parameters(promptTemplate, &parseError);
    if (!parseError.isEmpty()) {
        setError(error, parseError);
        return std::nullopt;
    }
    for (const QString& name : names) {
        if (!values.contains(name)) {
            setError(error, QStringLiteral("Template parameter is missing: %1").arg(name));
            return std::nullopt;
        }
    }

    QString result;
    result.reserve(promptTemplate.content.size());
    qsizetype offset = 0;
    while (offset < promptTemplate.content.size()) {
        const qsizetype opening = promptTemplate.content.indexOf(QStringLiteral("{{"), offset);
        if (opening < 0) {
            result.append(promptTemplate.content.sliced(offset));
            break;
        }
        result.append(promptTemplate.content.sliced(offset, opening - offset));
        const qsizetype closing = promptTemplate.content.indexOf(QStringLiteral("}}"), opening + 2);
        const QString name = promptTemplate.content.sliced(opening + 2, closing - opening - 2);
        result.append(values.value(name));
        offset = closing + 2;
    }
    return result;
}

bool PromptTemplateEngine::validate(const PromptTemplate& promptTemplate, QString* error) {
    const QString name = promptTemplate.name.trimmed();
    if (promptTemplate.id.isNull() || name.isEmpty() || name.size() > 80) {
        setError(error, QStringLiteral("Template name must contain 1 to 80 characters"));
        return false;
    }
    if (promptTemplate.content.trimmed().isEmpty() || promptTemplate.content.size() > 65536) {
        setError(error, QStringLiteral("Template content must contain 1 to 65536 characters"));
        return false;
    }
    QString parseError;
    const QStringList parsedParameters = parameters(promptTemplate, &parseError);
    Q_UNUSED(parsedParameters)
    if (!parseError.isEmpty()) {
        setError(error, parseError);
        return false;
    }
    return true;
}

} // namespace snack::domain
