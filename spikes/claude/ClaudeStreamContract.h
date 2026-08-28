#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace snack::spike::claude {

enum class StreamRecordKind {
    SystemInit,
    SystemEvent,
    User,
    Assistant,
    PartialAssistant,
    Result,
    Unknown,
    Malformed
};

struct StreamRecord {
    StreamRecordKind kind = StreamRecordKind::Malformed;
    QJsonObject payload;
    QString error;
};

[[nodiscard]] StreamRecord parseStreamRecord(const QByteArray& line);
[[nodiscard]] bool containsImage(const StreamRecord& record);

class StreamContractState {
  public:
    bool consume(const StreamRecord& record);

    [[nodiscard]] QString sessionId() const;
    [[nodiscard]] QStringList capabilities() const;
    [[nodiscard]] QStringList pendingUserMessages() const;
    [[nodiscard]] QStringList completedUserMessages() const;

  private:
    QString sessionId_;
    QStringList capabilities_;
    QStringList pendingUserMessages_;
    QStringList completedUserMessages_;
};

} // namespace snack::spike::claude
