#pragma once

#include "agent/IAgentAdapter.h"
#include "agent/codex/CodexAppServerClient.h"
#include "agent/codex/CodexCliDiscovery.h"
#include "agent/codex/CodexModelCatalog.h"

#include <QSet>

namespace snack::agent::codex {

class CodexAdapter final : public IAgentAdapter {
    Q_OBJECT

  public:
    CodexAdapter(CliInstallation installation, process::IProcessTransport* transport,
                 QObject* parent = nullptr);

    [[nodiscard]] domain::AgentKind kind() const override;
    [[nodiscard]] CapabilitySet capabilities() const override;
    void connectAgent(const QString& workingDirectory) override;
    void startTurn(const TurnRequest& request) override;
    void interruptTurn() override;
    void closeAgent() override;

  private:
    void requestModelPage(const QString& cursor = {});
    void handleResponse(qint64 id, const QString& method, const QJsonValue& result);
    void handleRequestFailure(qint64 id, const QString& method, int code, const QString& message);
    void appendModels(const QList<CodexModelInfo>& models);
    void finishModelDiscovery();
    void failConnection(const QString& detail);

    CliInstallation installation_;
    CodexAppServerClient client_;
    CapabilitySet capabilities_;
    QList<CodexModelInfo> models_;
    QSet<QString> requestedCursors_;
    QString workingDirectory_;
    qint64 modelRequestId_{0};
    bool connecting_{false};
    bool connected_{false};
    bool closing_{false};
};

} // namespace snack::agent::codex
