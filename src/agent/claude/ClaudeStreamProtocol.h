#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <optional>

namespace snack::agent::claude {

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
    StreamRecordKind kind{StreamRecordKind::Malformed};
    QJsonObject payload;
    QString error;
};

struct InitInfo {
    QString sessionId;
    QString cliVersion;
    QString workingDirectory;
    QString modelId;
    QString permissionMode;
    QStringList tools;
    QStringList capabilities;
};

[[nodiscard]] StreamRecord parseStreamRecord(const QByteArray& line);
[[nodiscard]] std::optional<InitInfo> parseInitInfo(const StreamRecord& record,
                                                    QString* error = nullptr);

} // namespace snack::agent::claude

Q_DECLARE_METATYPE(snack::agent::claude::StreamRecord)
Q_DECLARE_METATYPE(snack::agent::claude::InitInfo)
