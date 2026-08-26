#include "agent/codex/CodexModelCatalog.h"

#include <QJsonArray>
#include <QJsonObject>

#include <utility>

namespace snack::agent::codex {
namespace {

bool fail(QString* error, const QString& detail) {
    if (error != nullptr) {
        *error = detail;
    }
    return false;
}

bool parseReasoningEfforts(const QJsonValue& value, QList<CodexReasoningEffort>* efforts,
                           QString* error) {
    if (!value.isArray()) {
        return fail(error, QStringLiteral("Model supportedReasoningEfforts must be an array"));
    }
    for (const QJsonValue& itemValue : value.toArray()) {
        if (!itemValue.isObject()) {
            return fail(error, QStringLiteral("Model reasoning effort must be an object"));
        }
        const QJsonObject item = itemValue.toObject();
        const QJsonValue idValue = item.value(QStringLiteral("reasoningEffort"));
        const QJsonValue descriptionValue = item.value(QStringLiteral("description"));
        if (!idValue.isString() || idValue.toString().isEmpty()) {
            return fail(error, QStringLiteral("Model reasoning effort id is missing"));
        }
        if (!descriptionValue.isString()) {
            return fail(error, QStringLiteral("Model reasoning effort description is missing"));
        }
        efforts->append({.id = idValue.toString(), .description = descriptionValue.toString()});
    }
    return true;
}

bool parseInputModalities(const QJsonObject& object, QStringList* modalities, QString* error) {
    const QJsonValue value = object.value(QStringLiteral("inputModalities"));
    if (value.isUndefined()) {
        *modalities = {QStringLiteral("text"), QStringLiteral("image")};
        return true;
    }
    if (!value.isArray()) {
        return fail(error, QStringLiteral("Model inputModalities must be an array"));
    }
    for (const QJsonValue& modality : value.toArray()) {
        if (!modality.isString() || modality.toString().isEmpty()) {
            return fail(error, QStringLiteral("Model input modality must be a string"));
        }
        modalities->append(modality.toString());
    }
    return true;
}

std::optional<CodexModelInfo> parseModel(const QJsonValue& value, QString* error) {
    if (!value.isObject()) {
        fail(error, QStringLiteral("Model catalog entry must be an object"));
        return std::nullopt;
    }
    const QJsonObject object = value.toObject();
    const QStringList requiredStrings = {
        QStringLiteral("id"), QStringLiteral("model"), QStringLiteral("displayName"),
        QStringLiteral("description"), QStringLiteral("defaultReasoningEffort")};
    for (const QString& field : requiredStrings) {
        if (!object.value(field).isString()) {
            fail(error, QStringLiteral("Model catalog entry field %1 must be a string").arg(field));
            return std::nullopt;
        }
    }
    if (!object.value(QStringLiteral("hidden")).isBool() ||
        !object.value(QStringLiteral("isDefault")).isBool()) {
        fail(error, QStringLiteral("Model catalog visibility fields must be booleans"));
        return std::nullopt;
    }
    const QJsonValue supportsPersonality = object.value(QStringLiteral("supportsPersonality"));
    if (!supportsPersonality.isUndefined() && !supportsPersonality.isBool()) {
        fail(error, QStringLiteral("Model supportsPersonality must be a boolean"));
        return std::nullopt;
    }

    CodexModelInfo model;
    model.id = object.value(QStringLiteral("id")).toString();
    model.model = object.value(QStringLiteral("model")).toString();
    model.displayName = object.value(QStringLiteral("displayName")).toString();
    model.description = object.value(QStringLiteral("description")).toString();
    model.defaultReasoningEffortId =
        object.value(QStringLiteral("defaultReasoningEffort")).toString();
    model.hidden = object.value(QStringLiteral("hidden")).toBool();
    model.supportsPersonality = object.value(QStringLiteral("supportsPersonality")).toBool();
    model.isDefault = object.value(QStringLiteral("isDefault")).toBool();

    if (model.id.isEmpty() || model.model.isEmpty() || model.displayName.isEmpty() ||
        model.defaultReasoningEffortId.isEmpty()) {
        fail(error, QStringLiteral("Model catalog entry is missing required fields"));
        return std::nullopt;
    }
    if (!parseReasoningEfforts(object.value(QStringLiteral("supportedReasoningEfforts")),
                               &model.supportedReasoningEfforts, error) ||
        !parseInputModalities(object, &model.inputModalities, error)) {
        return std::nullopt;
    }
    return model;
}

} // namespace

std::optional<CodexModelPage> parseModelPage(const QJsonValue& result, QString* error) {
    if (!result.isObject()) {
        fail(error, QStringLiteral("model/list result must be an object"));
        return std::nullopt;
    }
    const QJsonObject object = result.toObject();
    const QJsonValue data = object.value(QStringLiteral("data"));
    if (!data.isArray()) {
        fail(error, QStringLiteral("model/list result data must be an array"));
        return std::nullopt;
    }

    CodexModelPage page;
    for (const QJsonValue& modelValue : data.toArray()) {
        auto model = parseModel(modelValue, error);
        if (!model.has_value()) {
            return std::nullopt;
        }
        page.models.append(std::move(*model));
    }

    const QJsonValue nextCursor = object.value(QStringLiteral("nextCursor"));
    if (!nextCursor.isUndefined() && !nextCursor.isNull() && !nextCursor.isString()) {
        fail(error, QStringLiteral("model/list nextCursor must be a string or null"));
        return std::nullopt;
    }
    page.nextCursor = nextCursor.toString();
    page.hasNextPage = !page.nextCursor.isEmpty();
    return page;
}

std::optional<domain::ReasoningEffort> reasoningEffortFromCodex(const QString& id) {
    if (id == QLatin1String("minimal")) {
        return domain::ReasoningEffort::Minimal;
    }
    if (id == QLatin1String("low")) {
        return domain::ReasoningEffort::Low;
    }
    if (id == QLatin1String("medium")) {
        return domain::ReasoningEffort::Medium;
    }
    if (id == QLatin1String("high")) {
        return domain::ReasoningEffort::High;
    }
    if (id == QLatin1String("xhigh")) {
        return domain::ReasoningEffort::ExtraHigh;
    }
    if (id == QLatin1String("max")) {
        return domain::ReasoningEffort::Maximum;
    }
    if (id == QLatin1String("ultra")) {
        return domain::ReasoningEffort::Ultra;
    }
    return std::nullopt;
}

} // namespace snack::agent::codex
