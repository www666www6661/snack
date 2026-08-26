#pragma once

#include "domain/DomainTypes.h"

#include <QObject>
#include <QStringList>

namespace snack::agent {

struct ReasoningEffortCapability {
    QString id;
    QString description;
};

struct ModelCapability {
    QString id;
    QString displayName;
    QString description;
    QString defaultReasoningEffortId;
    QList<ReasoningEffortCapability> supportedReasoningEfforts;
    QStringList inputModalities;
    bool supportsPersonality{false};
    bool isDefault{false};
};

struct CapabilitySet {
    QString version;
    QStringList models;
    QString defaultModelId;
    QList<ModelCapability> modelCapabilities;
    QList<domain::ReasoningEffort> reasoningEfforts;
    QList<domain::AccessLevel> accessLevels;
    bool supportsSteering{false};
    bool supportsInterrupt{true};
};

struct AgentConnectionRequest {
    QString workingDirectory;
    QString nativeThreadId;
    domain::TurnSettingsSnapshot settings;
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
    virtual void connectAgent(const AgentConnectionRequest& request) = 0;
    virtual void startTurn(const TurnRequest& request) = 0;
    virtual bool respondToApproval(const QString& requestId, domain::ApprovalDecision decision) = 0;
    virtual void interruptTurn() = 0;
    virtual void closeAgent() = 0;

  signals:
    void connectionChanged(bool connected, const QString& detail);
    void capabilitiesChanged(const snack::agent::CapabilitySet& capabilities);
    void nativeIdentityChanged(const QString& threadId, const QString& sessionId);
    void eventReceived(const snack::domain::AgentEvent& event);
    void turnFinished(const QUuid& turnId, bool interrupted);
};

} // namespace snack::agent

Q_DECLARE_METATYPE(snack::agent::CapabilitySet)
