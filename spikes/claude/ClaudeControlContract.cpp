#include "ClaudeControlContract.h"

#include <QJsonArray>
#include <QSet>

namespace snack::spike::claude {
namespace {

QStringList uniqueStrings(const QJsonArray& values, bool* valid) {
    QStringList result;
    QSet<QString> seen;
    for (const QJsonValue& value : values) {
        const QString id = value.toString();
        if (!value.isString() || id.isEmpty() || seen.contains(id)) {
            *valid = false;
            return {};
        }
        seen.insert(id);
        result.append(id);
    }
    return result;
}

} // namespace

InterruptReceipt parseInterruptReceipt(const QJsonObject& payload) {
    if (!payload.value(QStringLiteral("still_queued")).isArray()) {
        return {.error = QStringLiteral("Interrupt receipt is missing still_queued")};
    }

    bool valid = true;
    const QStringList stillQueued =
        uniqueStrings(payload.value(QStringLiteral("still_queued")).toArray(), &valid);
    if (!valid) {
        return {.error = QStringLiteral("Interrupt receipt has invalid still_queued IDs")};
    }

    QStringList cancelled;
    if (payload.contains(QStringLiteral("cancelled"))) {
        if (!payload.value(QStringLiteral("cancelled")).isArray()) {
            return {.error = QStringLiteral("Interrupt receipt has an invalid cancelled field")};
        }
        cancelled = uniqueStrings(payload.value(QStringLiteral("cancelled")).toArray(), &valid);
        if (!valid) {
            return {.error = QStringLiteral("Interrupt receipt has invalid cancelled IDs")};
        }
    }

    for (const QString& id : stillQueued) {
        if (cancelled.contains(id)) {
            return {.error = QStringLiteral("Interrupt receipt puts one ID in both states")};
        }
    }
    return {.valid = true, .stillQueued = stillQueued, .cancelled = cancelled};
}

bool supportsInterruptReceipt(const QStringList& capabilities) {
    return capabilities.contains(QStringLiteral("interrupt_receipt_v1"));
}

bool supportsQueuedCancellation(const QStringList& capabilities) {
    return supportsInterruptReceipt(capabilities) &&
           capabilities.contains(QStringLiteral("interrupt_cancel_queued_v1"));
}

QueueReconciliation reconcileInterruptQueue(const QStringList& knownQueued,
                                            const QStringList& capabilities,
                                            const InterruptReceipt* receipt) {
    QueueReconciliation result;
    result.survivingKnown = knownQueued;
    if (!supportsInterruptReceipt(capabilities) || receipt == nullptr || !receipt->valid ||
        (!receipt->cancelled.isEmpty() && !supportsQueuedCancellation(capabilities))) {
        return result;
    }

    result.authoritative = true;
    result.survivingKnown.clear();
    for (const QString& id : receipt->stillQueued) {
        if (knownQueued.contains(id)) {
            result.survivingKnown.append(id);
        } else {
            result.unrecognized.append(id);
        }
    }
    for (const QString& id : receipt->cancelled) {
        if (knownQueued.contains(id)) {
            result.cancelledKnown.append(id);
        } else if (!result.unrecognized.contains(id)) {
            result.unrecognized.append(id);
        }
    }
    return result;
}

} // namespace snack::spike::claude
