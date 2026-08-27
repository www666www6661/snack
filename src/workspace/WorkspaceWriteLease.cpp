#include "workspace/WorkspaceWriteLease.h"

#include "app/WorkspacePathPolicy.h"

#include <QHash>
#include <QMutex>
#include <QMutexLocker>

namespace snack::workspace {
namespace {

QMutex leaseMutex;
QHash<QString, QString> leaseOwners;

void setError(QString* error, const QString& value) {
    if (error != nullptr) {
        *error = value;
    }
}

} // namespace

WorkspaceWriteLease::~WorkspaceWriteLease() { release(); }

bool WorkspaceWriteLease::acquire(const QString& workspace, const QString& ownerId,
                                  QString* error) {
    if (held_ || ownerId.trimmed().isEmpty()) {
        setError(error, QStringLiteral("A write lease requires one non-empty owner"));
        return false;
    }
    const QString key = app::WorkspacePathPolicy::identityKey(workspace);
    if (key.isEmpty()) {
        setError(error, QStringLiteral("The workspace does not exist"));
        return false;
    }
    QMutexLocker lock(&leaseMutex);
    if (leaseOwners.contains(key)) {
        setError(error, QStringLiteral("Another session owns the workspace write lease"));
        return false;
    }
    leaseOwners.insert(key, ownerId);
    workspaceKey_ = key;
    ownerId_ = ownerId;
    held_ = true;
    return true;
}

bool WorkspaceWriteLease::transfer(const QString& nextOwnerId, QString* error) {
    if (!held_ || nextOwnerId.trimmed().isEmpty()) {
        setError(error, QStringLiteral("Cannot transfer an unowned write lease"));
        return false;
    }
    QMutexLocker lock(&leaseMutex);
    if (leaseOwners.value(workspaceKey_) != ownerId_) {
        setError(error, QStringLiteral("The write lease ownership no longer matches"));
        held_ = false;
        return false;
    }
    ownerId_ = nextOwnerId;
    leaseOwners.insert(workspaceKey_, ownerId_);
    return true;
}

void WorkspaceWriteLease::release() {
    if (!held_) {
        return;
    }
    QMutexLocker lock(&leaseMutex);
    if (leaseOwners.value(workspaceKey_) == ownerId_) {
        leaseOwners.remove(workspaceKey_);
    }
    workspaceKey_.clear();
    ownerId_.clear();
    held_ = false;
}

} // namespace snack::workspace
