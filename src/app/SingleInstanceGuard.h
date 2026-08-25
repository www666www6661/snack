#pragma once

#include <QLocalServer>
#include <QLockFile>

#include <memory>
#include <optional>

namespace snack::app {

class SingleInstanceGuard final : public QObject {
    Q_OBJECT

  public:
    enum class StartResult { Primary, MessageSent, Error };

    explicit SingleInstanceGuard(QString serverName, QObject* parent = nullptr);
    [[nodiscard]] StartResult start(const QStringList& arguments, QString* error = nullptr);

  signals:
    void activationRequested(const std::optional<QString>& directory);

  private slots:
    void acceptConnection();

  private:
    [[nodiscard]] QByteArray activationMessage(const QStringList& arguments) const;
    bool sendToPrimary(const QByteArray& message, QString* error);

    QString serverName_;
    std::unique_ptr<QLockFile> lockFile_;
    QLocalServer server_;
};

} // namespace snack::app

Q_DECLARE_METATYPE(std::optional<QString>)
