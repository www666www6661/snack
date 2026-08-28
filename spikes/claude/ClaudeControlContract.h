#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace snack::spike::claude {

struct InterruptReceipt {
    bool valid = false;
    QStringList stillQueued;
    QStringList cancelled;
    QString error;
};

struct QueueReconciliation {
    bool authoritative = false;
    QStringList survivingKnown;
    QStringList cancelledKnown;
    QStringList unrecognized;
};

[[nodiscard]] InterruptReceipt parseInterruptReceipt(const QJsonObject& payload);
[[nodiscard]] bool supportsInterruptReceipt(const QStringList& capabilities);
[[nodiscard]] bool supportsQueuedCancellation(const QStringList& capabilities);
[[nodiscard]] QueueReconciliation reconcileInterruptQueue(const QStringList& knownQueued,
                                                          const QStringList& capabilities,
                                                          const InterruptReceipt* receipt);

} // namespace snack::spike::claude
