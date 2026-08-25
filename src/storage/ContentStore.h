#pragma once

#include <QByteArray>
#include <QString>

namespace snack::storage {

class IContentStore {
  public:
    virtual ~IContentStore() = default;
    [[nodiscard]] virtual QString put(const QByteArray& content, QString* error) = 0;
    [[nodiscard]] virtual QByteArray get(const QString& hash, QString* error) const = 0;
    [[nodiscard]] virtual bool contains(const QString& hash) const = 0;
};

class FileContentStore final : public IContentStore {
  public:
    explicit FileContentStore(QString rootDirectory);

    [[nodiscard]] QString put(const QByteArray& content, QString* error) override;
    [[nodiscard]] QByteArray get(const QString& hash, QString* error) const override;
    [[nodiscard]] bool contains(const QString& hash) const override;

  private:
    [[nodiscard]] QString pathForHash(const QString& hash) const;

    QString rootDirectory_;
};

} // namespace snack::storage
