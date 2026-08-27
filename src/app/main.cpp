#include "agent/AgentRuntime.h"
#include "app/AppSettings.h"
#include "app/ConversationBootstrap.h"
#include "app/Logging.h"
#include "app/SessionManager.h"
#include "app/SingleInstanceGuard.h"
#include "storage/EventStore.h"
#include "ui/MainWindow.h"

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QIcon>
#include <QLocale>
#include <QMessageBox>
#include <QStandardPaths>
#include <QSystemTrayIcon>
#include <QTranslator>

#include <utility>

int main(int argc, char* argv[]) {
    QApplication application(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral(SNACK_ORGANIZATION));
    QCoreApplication::setOrganizationDomain(QStringLiteral("github.com/www666www6661"));
    QCoreApplication::setApplicationName(QStringLiteral("snack"));
    QApplication::setApplicationDisplayName(QStringLiteral("零食"));
    QCoreApplication::setApplicationVersion(QStringLiteral(SNACK_VERSION));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/icons/snack.svg")));

    const QString dataDirectory =
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QString loggingError;
    snack::app::Logging::install(QDir(dataDirectory).filePath(QStringLiteral("logs")),
                                 &loggingError);
    if (!loggingError.isEmpty()) {
        qWarning() << loggingError;
    }

    snack::app::SingleInstanceGuard singleInstance(QStringLiteral(SNACK_APPLICATION_ID));
    QString instanceError;
    const auto startResult = singleInstance.start(application.arguments(), &instanceError);
    if (startResult == snack::app::SingleInstanceGuard::StartResult::MessageSent) {
        return 0;
    }
    if (startResult == snack::app::SingleInstanceGuard::StartResult::Error) {
        QMessageBox::critical(nullptr, QObject::tr("Snack"),
                              QObject::tr("Cannot start the application: %1").arg(instanceError));
        return 1;
    }

    snack::app::AppSettings settings;
    auto settingsSnapshot = settings.load();
    QTranslator translator;
    const QString locale = settingsSnapshot.locale == QLatin1String("system")
                               ? QLocale::system().name()
                               : settingsSnapshot.locale;
    if (locale.startsWith(QLatin1String("zh"))) {
        if (translator.load(QStringLiteral(":/i18n/snack_zh_CN.qm"))) {
            application.installTranslator(&translator);
        } else {
            qWarning() << "Cannot load built-in Simplified Chinese translation";
        }
    }

    QString workspace = settingsSnapshot.lastWorkspace;
    if (application.arguments().size() > 1) {
        const QFileInfo requested(application.arguments().at(1));
        if (requested.exists() && requested.isDir()) {
            workspace = requested.canonicalFilePath();
        }
    }
    if (workspace.isEmpty() || !QFileInfo::exists(workspace)) {
        workspace = QDir::currentPath();
    }

    snack::agent::AgentRuntime agentRuntime = snack::agent::AgentRuntimeFactory::create(
        settingsSnapshot.preferredAgentKind, settingsSnapshot.codexExecutable);

    snack::storage::EventStore eventStore;
    QString storageError;
    if (!eventStore.open(QDir(dataDirectory).filePath(QStringLiteral("snack.sqlite3")),
                         &storageError)) {
        QMessageBox::critical(nullptr, QObject::tr("Snack"),
                              QObject::tr("Cannot open local data: %1").arg(storageError));
        return 2;
    }
    if (eventStore.isReadOnlyRecovery()) {
        QString recoveryMessage =
            QCoreApplication::translate(
                "main", "A database migration failed. Existing data was opened read-only and "
                        "no Agent work will be started.\n\nReason: %1")
                .arg(eventStore.recoveryError());
        if (!eventStore.migrationBackupPath().isEmpty()) {
            recoveryMessage += QCoreApplication::translate("main", "\n\nSafety backup: %1")
                                   .arg(QDir::toNativeSeparators(eventStore.migrationBackupPath()));
        }
        QMessageBox::warning(nullptr, QCoreApplication::translate("main", "Database recovery mode"),
                             recoveryMessage);
    }

    std::optional<snack::domain::Conversation> restoredConversation;
    const QUuid restoredId(settingsSnapshot.lastConversationId);
    if (!restoredId.isNull()) {
        QString restoreError;
        restoredConversation = eventStore.conversationById(restoredId, &restoreError);
        if (!restoreError.isEmpty()) {
            qWarning() << restoreError;
        }
    }
    const QString newConversationTitle =
        agentRuntime.selectedKind == snack::domain::AgentKind::Codex
            ? QCoreApplication::translate("main", "Codex conversation")
            : QCoreApplication::translate("main", "Mock conversation");
    snack::domain::Conversation conversation =
        snack::app::prepareConversation(restoredConversation, workspace, agentRuntime.selectedKind,
                                        newConversationTitle)
            .conversation;

    settingsSnapshot.lastWorkspace = workspace;
    settingsSnapshot.lastConversationId = conversation.id.toString(QUuid::WithoutBraces);
    settings.save(settingsSnapshot);

    snack::app::SessionManager sessions(
        &eventStore,
        [codexExecutable = settingsSnapshot.codexExecutable](snack::domain::AgentKind kind) {
            return snack::agent::AgentRuntimeFactory::create(kind, codexExecutable);
        });
    QString sessionError;
    auto* controller = sessions.addPrepared(conversation, std::move(agentRuntime), &sessionError);
    if (controller == nullptr) {
        QMessageBox::critical(
            nullptr, QObject::tr("Snack"),
            QObject::tr("Cannot create the conversation session: %1").arg(sessionError));
        return 3;
    }
    const auto* currentRuntime = sessions.runtime(conversation.id);
    Q_ASSERT(currentRuntime != nullptr);

    const bool closeToTrayEnabled = QSystemTrayIcon::isSystemTrayAvailable();
    application.setQuitOnLastWindowClosed(!closeToTrayEnabled);
    snack::ui::MainWindow window(controller, &settings, closeToTrayEnabled);
    if (currentRuntime->fellBack) {
        window.showStartupNotice(QCoreApplication::translate("main", "Using Mock Agent because %1")
                                     .arg(currentRuntime->detail));
    }
    QObject::connect(&singleInstance, &snack::app::SingleInstanceGuard::activationRequested,
                     &window, &snack::ui::MainWindow::activateWindowForRequest);

    window.show();
    return application.exec();
}
