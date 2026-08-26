#include "agent/codex/CodexUserInputLifecycle.h"

#include <QJsonDocument>
#include <QSet>

#include <cmath>

namespace snack::agent::codex {
namespace {

void setError(QString* error, const QString& message) {
    if (error != nullptr) {
        *error = message;
    }
}

bool validRequestId(const QJsonValue& id) {
    return (id.isString() && !id.toString().isEmpty()) || id.isDouble();
}

bool validTokenCount(const QJsonValue& value) {
    return value.isDouble() && std::isfinite(value.toDouble()) && value.toDouble() >= 0.0 &&
           std::floor(value.toDouble()) == value.toDouble();
}

std::optional<QJsonObject> parseUsageBreakdown(const QJsonValue& value, QString* error) {
    if (!value.isObject()) {
        setError(error, QStringLiteral("token usage breakdown must be an object"));
        return std::nullopt;
    }
    const QJsonObject object = value.toObject();
    static const QStringList required = {
        QStringLiteral("inputTokens"), QStringLiteral("cachedInputTokens"),
        QStringLiteral("outputTokens"), QStringLiteral("reasoningOutputTokens"),
        QStringLiteral("totalTokens")};
    QJsonObject result;
    for (const QString& name : required) {
        if (!validTokenCount(object.value(name))) {
            setError(error,
                     QStringLiteral("token usage field is missing or invalid: %1").arg(name));
            return std::nullopt;
        }
        result.insert(name, object.value(name));
    }
    const QJsonValue cacheWrite = object.value(QStringLiteral("cacheWriteInputTokens"));
    if (!cacheWrite.isUndefined() && !validTokenCount(cacheWrite)) {
        setError(error, QStringLiteral("cacheWriteInputTokens is invalid"));
        return std::nullopt;
    }
    result.insert(QStringLiteral("cacheWriteInputTokens"),
                  cacheWrite.isUndefined() ? QJsonValue(0) : cacheWrite);
    return result;
}

} // namespace

std::optional<CodexUserInputRequest>
parseUserInputRequest(const QJsonValue& id, const QJsonValue& params, QString* error) {
    if (!validRequestId(id)) {
        setError(error, QStringLiteral("user input request id is invalid"));
        return std::nullopt;
    }
    if (!params.isObject()) {
        setError(error, QStringLiteral("user input params must be an object"));
        return std::nullopt;
    }
    const QJsonObject object = params.toObject();
    CodexUserInputRequest request;
    request.nativeRequestId = id;
    request.threadId = object.value(QStringLiteral("threadId")).toString();
    request.turnId = object.value(QStringLiteral("turnId")).toString();
    request.itemId = object.value(QStringLiteral("itemId")).toString();
    if (request.threadId.isEmpty() || request.turnId.isEmpty() || request.itemId.isEmpty()) {
        setError(error, QStringLiteral("user input routing fields are missing"));
        return std::nullopt;
    }
    if (!object.value(QStringLiteral("isBlocking")).isBool()) {
        setError(error, QStringLiteral("isBlocking must be a boolean"));
        return std::nullopt;
    }
    request.isBlocking = object.value(QStringLiteral("isBlocking")).toBool();
    if (!object.value(QStringLiteral("questions")).isArray()) {
        setError(error, QStringLiteral("questions must be an array"));
        return std::nullopt;
    }
    const QJsonArray questions = object.value(QStringLiteral("questions")).toArray();
    if (questions.isEmpty() || questions.size() > 3) {
        setError(error, QStringLiteral("questions must contain between one and three items"));
        return std::nullopt;
    }

    QSet<QString> questionIds;
    for (const QJsonValue& value : questions) {
        if (!value.isObject()) {
            setError(error, QStringLiteral("question must be an object"));
            return std::nullopt;
        }
        QJsonObject question = value.toObject();
        const QString questionId = question.value(QStringLiteral("id")).toString();
        if (questionId.isEmpty() || questionIds.contains(questionId) ||
            question.value(QStringLiteral("header")).toString().isEmpty() ||
            question.value(QStringLiteral("question")).toString().isEmpty()) {
            setError(error, QStringLiteral("question fields are missing or duplicated"));
            return std::nullopt;
        }
        questionIds.insert(questionId);
        for (const QString& booleanName : {QStringLiteral("isOther"), QStringLiteral("isSecret")}) {
            if (question.contains(booleanName) && !question.value(booleanName).isBool()) {
                setError(error, QStringLiteral("%1 must be a boolean").arg(booleanName));
                return std::nullopt;
            }
        }
        const QJsonValue optionsValue = question.value(QStringLiteral("options"));
        if (!optionsValue.isUndefined() && !optionsValue.isNull() && !optionsValue.isArray()) {
            setError(error, QStringLiteral("question options must be an array or null"));
            return std::nullopt;
        }
        for (const QJsonValue& optionValue : optionsValue.toArray()) {
            const QJsonObject option = optionValue.toObject();
            if (!optionValue.isObject() ||
                option.value(QStringLiteral("label")).toString().isEmpty() ||
                !option.contains(QStringLiteral("description")) ||
                !option.value(QStringLiteral("description")).isString()) {
                setError(error, QStringLiteral("question option is invalid"));
                return std::nullopt;
            }
        }
        question.insert(QStringLiteral("isOther"),
                        question.value(QStringLiteral("isOther")).toBool(false));
        question.insert(QStringLiteral("isSecret"),
                        question.value(QStringLiteral("isSecret")).toBool(false));
        if (optionsValue.isUndefined()) {
            question.insert(QStringLiteral("options"), QJsonValue::Null);
        }
        request.questions.append(question);
    }
    return request;
}

QJsonObject userInputEventPayload(const QString& requestId, const CodexUserInputRequest& request) {
    return {{QStringLiteral("requestId"), requestId},
            {QStringLiteral("itemId"), request.itemId},
            {QStringLiteral("isBlocking"), request.isBlocking},
            {QStringLiteral("questions"), request.questions}};
}

bool validateUserInputAnswers(const CodexUserInputRequest& request, const QJsonObject& answers,
                              QString* error) {
    QSet<QString> expected;
    for (const QJsonValue& question : request.questions) {
        expected.insert(question.toObject().value(QStringLiteral("id")).toString());
    }
    if (answers.size() != expected.size()) {
        setError(error, QStringLiteral("answers must cover every question"));
        return false;
    }
    for (auto iterator = answers.constBegin(); iterator != answers.constEnd(); ++iterator) {
        const QJsonObject answer = iterator.value().toObject();
        if (!expected.contains(iterator.key()) || !iterator.value().isObject() ||
            !answer.value(QStringLiteral("answers")).isArray()) {
            setError(error, QStringLiteral("answer entry is invalid"));
            return false;
        }
        for (const QJsonValue& value : answer.value(QStringLiteral("answers")).toArray()) {
            if (!value.isString()) {
                setError(error, QStringLiteral("answer values must be strings"));
                return false;
            }
        }
    }
    return true;
}

QJsonObject userInputResponse(const QJsonObject& answers) {
    return {{QStringLiteral("answers"), answers}};
}

std::optional<QJsonObject> parseThreadTokenUsage(const QJsonValue& params, QString* threadId,
                                                 QString* turnId, QString* error) {
    if (!params.isObject()) {
        setError(error, QStringLiteral("token usage params must be an object"));
        return std::nullopt;
    }
    const QJsonObject object = params.toObject();
    *threadId = object.value(QStringLiteral("threadId")).toString();
    *turnId = object.value(QStringLiteral("turnId")).toString();
    if (threadId->isEmpty() || turnId->isEmpty() ||
        !object.value(QStringLiteral("tokenUsage")).isObject()) {
        setError(error, QStringLiteral("token usage routing fields are missing"));
        return std::nullopt;
    }
    const QJsonObject usage = object.value(QStringLiteral("tokenUsage")).toObject();
    const auto last = parseUsageBreakdown(usage.value(QStringLiteral("last")), error);
    const auto total = parseUsageBreakdown(usage.value(QStringLiteral("total")), error);
    if (!last.has_value() || !total.has_value()) {
        return std::nullopt;
    }
    const QJsonValue contextWindow = usage.value(QStringLiteral("modelContextWindow"));
    if (!contextWindow.isNull() && !validTokenCount(contextWindow)) {
        setError(error, QStringLiteral("modelContextWindow is invalid"));
        return std::nullopt;
    }
    return QJsonObject{{QStringLiteral("last"), *last},
                       {QStringLiteral("total"), *total},
                       {QStringLiteral("modelContextWindow"), contextWindow}};
}

} // namespace snack::agent::codex
