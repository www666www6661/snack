#pragma once

#include "domain/DomainTypes.h"

#include <QObject>
#include <QStringList>

namespace snack::agent {

struct CapabilitySet {
    QString version;
    QStringList models;
    QList<domain::ReasoningEffort> reasoningEfforts;
    QList<domain::AccessLevel> accessLevels;
    bool supportsSteering{false};
    bool supportsInterrupt{true};
};

struct TurnRequest {
    QUuid turnId;
    QString message;
    domain::TurnSettingsSnapshot settings;
};

class IAgentAdapter : public QObject {
    Q_OBJECT

  public:
    using QObject::QObject;
    ~IAgentAdapter() override = default;

    [[nodiscard]] virtual domain::AgentKind kind() const = 0;
    [[nodiscard]] virtual CapabilitySet capabilities() const = 0;
    virtual void connectAgent(const QString& workingDirectory) = 0;
    virtual void startTurn(const TurnRequest& request) = 0;
    virtual void interruptTurn() = 0;
    virtual void closeAgent() = 0;

  signals:
    void connectionChanged(bool connected, const QString& detail);
    void eventReceived(const snack::domain::AgentEvent& event);
    void turnFinished(const QUuid& turnId, bool interrupted);
};

} // namespace snack::agent
