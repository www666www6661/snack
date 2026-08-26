#pragma once

#include "agent/process/IProcessTransport.h"

#include <QProcess>

namespace snack::agent::process {

class QProcessTransport final : public IProcessTransport {
    Q_OBJECT

  public:
    explicit QProcessTransport(QObject* parent = nullptr);

    [[nodiscard]] bool isRunning() const override;
    void start(const LaunchSpec& launchSpec) override;
    qint64 write(const QByteArray& data) override;
    void closeWriteChannel() override;
    void terminate() override;
    void kill() override;

  private:
    QProcess process_;
};

} // namespace snack::agent::process
