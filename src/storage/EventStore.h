#pragma once

#include "storage/EventRepository.h"

#include <QSqlDatabase>

namespace snack::storage {

class EventStore final : public IEventRepository {
  public:
    EventStore();
    ~EventStore() override;

    EventStore(const EventStore&) = delete;
    EventStore& operator=(const EventStore&) = delete;

    bool open(const QString& databasePath, QString* error);
    [[nodiscard]] bool isOpen() const;

    bool saveConversation(const domain::Conversation& conversation, QString* error) override;
    bool appendEvent(const domain::AgentEvent& event, QString* error) override;
    [[nodiscard]] QList<domain::AgentEvent> eventsForConversation(const QUuid& conversationId,
                                                                  QString* error) const override;

  private:
    bool applyMigrations(QString* error);
    bool execute(const QString& statement, QString* error);

    QString connectionName_;
    QSqlDatabase database_;
};

} // namespace snack::storage
