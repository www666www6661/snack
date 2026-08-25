#include "app/SingleInstanceGuard.h"

#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>
#include <QUuid>

#include <chrono>
#include <future>
#include <utility>

class TestSingleInstance final : public QObject {
    Q_OBJECT

  private slots:
    void forwardsValidatedWorkspaceToPrimary();
};

void TestSingleInstance::forwardsValidatedWorkspaceToPrimary() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString serverName =
        QStringLiteral("snack-test-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));

    snack::app::SingleInstanceGuard primary(serverName);
    QString error;
    QCOMPARE(primary.start({QStringLiteral("snack")}, &error),
             snack::app::SingleInstanceGuard::StartResult::Primary);
    QVERIFY2(error.isEmpty(), qPrintable(error));

    bool activated = false;
    std::optional<QString> receivedDirectory;
    connect(&primary, &snack::app::SingleInstanceGuard::activationRequested, this,
            [&](const std::optional<QString>& value) {
                activated = true;
                receivedDirectory = value;
            });

    const QString directoryPath = directory.path();
    auto secondaryResult = std::async(std::launch::async, [serverName, directoryPath] {
        snack::app::SingleInstanceGuard secondary(serverName);
        QString secondaryError;
        const auto result =
            secondary.start({QStringLiteral("snack"), directoryPath}, &secondaryError);
        return std::pair{result, secondaryError};
    });
    QTRY_VERIFY_WITH_TIMEOUT(
        secondaryResult.wait_for(std::chrono::milliseconds{0}) == std::future_status::ready, 2000);
    const auto [result, secondaryError] = secondaryResult.get();
    QVERIFY2(result == snack::app::SingleInstanceGuard::StartResult::MessageSent,
             qPrintable(secondaryError));
    QTRY_VERIFY_WITH_TIMEOUT(activated, 1000);
    QVERIFY(receivedDirectory.has_value());
    QCOMPARE(receivedDirectory.value(), QFileInfo(directory.path()).canonicalFilePath());
}

QTEST_GUILESS_MAIN(TestSingleInstance)
#include "TestSingleInstance.moc"
