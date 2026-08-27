#pragma once

#include "storage/EventRepository.h"

#include <QSqlDatabase>

namespace snack::storage {

class EventStore final : public IEventRepository {
  public:
    enum class Mode { Closed, ReadWrite, RecoveryReadOnly };

    EventStore();
    ~EventStore() override;

    EventStore(const EventStore&) = delete;
    EventStore& operator=(const EventStore&) = delete;

    bool open(const QString& databasePath, QString* error);
    [[nodiscard]] bool isOpen() const;
    [[nodiscard]] Mode mode() const;
    [[nodiscard]] bool isReadOnlyRecovery() const;
    [[nodiscard]] QString migrationBackupPath() const;
    [[nodiscard]] QString recoveryError() const;

    bool saveConversation(const domain::Conversation& conversation, QString* error) override;
    [[nodiscard]] std::optional<domain::Conversation>
    conversationById(const QUuid& conversationId, QString* error) const override;
    [[nodiscard]] QList<domain::Conversation> conversations(QString* error) const override;
    bool appendEvent(const domain::AgentEvent& event, QString* error) override;
    [[nodiscard]] QList<domain::AgentEvent> eventsForConversation(const QUuid& conversationId,
                                                                  QString* error) const override;
    bool replaceQueuedMessages(const QUuid& conversationId,
                               const QList<domain::QueuedMessage>& messages,
                               QString* error) override;
    [[nodiscard]] QList<domain::QueuedMessage>
    queuedMessagesForConversation(const QUuid& conversationId, QString* error) const override;
    bool savePromptTemplate(const domain::PromptTemplate& promptTemplate, QString* error) override;
    bool deletePromptTemplate(const QUuid& templateId, QString* error) override;
    [[nodiscard]] QList<domain::PromptTemplate> promptTemplates(QString* error) const override;
    bool saveConversationView(const domain::SavedConversationView& view, QString* error) override;
    bool reorderConversationViews(const QList<QUuid>& viewIds, QString* error) override;
    bool deleteConversationView(const QUuid& viewId, QString* error) override;
    [[nodiscard]] QList<domain::SavedConversationView>
    conversationViews(QString* error) const override;

  private:
    [[nodiscard]] int schemaVersion(QString* error) const;
    bool createMigrationBackup(const QString& databasePath, int targetVersion, QString* error);
    bool applyMigrations(int currentVersion, QString* error);
    bool validateSchema(bool checkIntegrity, QString* error) const;
    bool enterRecoveryMode(const QString& reason, QString* error);
    bool ensureWritable(QString* error) const;
    bool execute(const QString& statement, QString* error);

    QString connectionName_;
    QSqlDatabase database_;
    Mode mode_{Mode::Closed};
    QString migrationBackupPath_;
    QString recoveryError_;
};

} // namespace snack::storage
