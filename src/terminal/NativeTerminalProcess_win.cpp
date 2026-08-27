#include "terminal/NativeTerminalProcess.h"

#include <QDir>

#include <qt_windows.h>

#include <atomic>
#include <cstddef>
#include <memory>
#include <thread>

namespace snack::terminal {
namespace {

[[nodiscard]] QString windowsError(const QString& context, DWORD errorCode = GetLastError()) {
    return QStringLiteral("%1 (Windows error %2)").arg(context).arg(errorCode);
}

void closeHandle(HANDLE& handle) {
    if (handle != nullptr && handle != INVALID_HANDLE_VALUE) {
        CloseHandle(handle);
    }
    handle = nullptr;
}

class NativeTerminalProcess final : public ITerminalProcess {
  public:
    using ITerminalProcess::ITerminalProcess;
    ~NativeTerminalProcess() override { closeTerminal(); }

    bool start(const QString& workingDirectory, int columns, int rows, QString* error) override;
    void writeInput(const QByteArray& bytes) override;
    void resizeTerminal(int columns, int rows) override;
    void closeTerminal() override;

  private:
    void readLoop();
    void waitLoop();

    std::thread reader_;
    std::thread waiter_;
    std::atomic_bool closing_{false};
    HPCON pseudoConsole_{nullptr};
    HANDLE inputWrite_{nullptr};
    HANDLE outputRead_{nullptr};
    HANDLE process_{nullptr};
};

bool NativeTerminalProcess::start(const QString& workingDirectory, int columns, int rows,
                                  QString* error) {
    if (process_ != nullptr) {
        if (error != nullptr) {
            *error = QStringLiteral("Terminal is already running");
        }
        return false;
    }

    HANDLE pseudoInputRead = nullptr;
    HANDLE pseudoOutputWrite = nullptr;
    if (!CreatePipe(&pseudoInputRead, &inputWrite_, nullptr, 0) ||
        !CreatePipe(&outputRead_, &pseudoOutputWrite, nullptr, 0)) {
        const DWORD errorCode = GetLastError();
        closeHandle(pseudoInputRead);
        closeHandle(pseudoOutputWrite);
        if (error != nullptr) {
            *error = windowsError(QStringLiteral("Cannot create ConPTY pipes"), errorCode);
        }
        closeTerminal();
        return false;
    }

    const COORD size{static_cast<SHORT>(columns), static_cast<SHORT>(rows)};
    const HRESULT consoleResult =
        CreatePseudoConsole(size, pseudoInputRead, pseudoOutputWrite, 0, &pseudoConsole_);
    closeHandle(pseudoInputRead);
    closeHandle(pseudoOutputWrite);
    if (FAILED(consoleResult)) {
        if (error != nullptr) {
            *error = QStringLiteral("CreatePseudoConsole failed: 0x%1")
                         .arg(static_cast<qulonglong>(consoleResult), 0, 16);
        }
        closeTerminal();
        return false;
    }

    SIZE_T attributeBytes = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attributeBytes);
    auto attributes = std::make_unique<std::byte[]>(attributeBytes);
    auto* attributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributes.get());
    if (!InitializeProcThreadAttributeList(attributeList, 1, 0, &attributeBytes)) {
        const DWORD errorCode = GetLastError();
        if (error != nullptr) {
            *error = windowsError(QStringLiteral("Cannot initialize ConPTY attributes"), errorCode);
        }
        closeTerminal();
        return false;
    }
    if (!UpdateProcThreadAttribute(attributeList, 0, PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE,
                                   pseudoConsole_, sizeof(pseudoConsole_), nullptr, nullptr)) {
        const DWORD errorCode = GetLastError();
        DeleteProcThreadAttributeList(attributeList);
        if (error != nullptr) {
            *error = windowsError(QStringLiteral("Cannot attach the ConPTY attribute"), errorCode);
        }
        closeTerminal();
        return false;
    }

    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    // Prevent console test hosts from donating their own standard handles to the shell.
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.lpAttributeList = attributeList;
    PROCESS_INFORMATION processInfo{};
    const QString shellPath =
        qEnvironmentVariable("COMSPEC", QStringLiteral("C:\\Windows\\System32\\cmd.exe"));
    const std::wstring application = QDir::toNativeSeparators(shellPath).toStdWString();
    std::wstring command = application;
    command.push_back(L'\0');
    const std::wstring directory = QDir::toNativeSeparators(workingDirectory).toStdWString();
    const BOOL created =
        CreateProcessW(application.c_str(), command.data(), nullptr, nullptr, FALSE,
                       EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT, nullptr,
                       directory.c_str(), &startup.StartupInfo, &processInfo);
    const DWORD createError = created ? ERROR_SUCCESS : GetLastError();
    DeleteProcThreadAttributeList(attributeList);
    if (!created) {
        if (error != nullptr) {
            *error = windowsError(QStringLiteral("Cannot start the ConPTY shell"), createError);
        }
        closeTerminal();
        return false;
    }

    CloseHandle(processInfo.hThread);
    process_ = processInfo.hProcess;
    closing_ = false;
    reader_ = std::thread([this] { readLoop(); });
    waiter_ = std::thread([this] { waitLoop(); });
    return true;
}

void NativeTerminalProcess::writeInput(const QByteArray& bytes) {
    qsizetype offset = 0;
    while (inputWrite_ != nullptr && offset < bytes.size()) {
        DWORD written = 0;
        const DWORD remaining = static_cast<DWORD>(bytes.size() - offset);
        if (!WriteFile(inputWrite_, bytes.constData() + offset, remaining, &written, nullptr)) {
            if (!closing_) {
                emit processError(windowsError(QStringLiteral("ConPTY write failed")));
            }
            return;
        }
        if (written == 0) {
            emit processError(QStringLiteral("ConPTY write made no progress"));
            return;
        }
        offset += static_cast<qsizetype>(written);
    }
}

void NativeTerminalProcess::resizeTerminal(int columns, int rows) {
    if (pseudoConsole_ == nullptr || columns <= 0 || rows <= 0) {
        return;
    }
    const HRESULT result = ResizePseudoConsole(
        pseudoConsole_, COORD{static_cast<SHORT>(columns), static_cast<SHORT>(rows)});
    if (FAILED(result) && !closing_) {
        emit processError(QStringLiteral("ResizePseudoConsole failed: 0x%1")
                              .arg(static_cast<qulonglong>(result), 0, 16));
    }
}

void NativeTerminalProcess::readLoop() {
    QByteArray buffer(8192, Qt::Uninitialized);
    while (!closing_) {
        DWORD bytesRead = 0;
        if (!ReadFile(outputRead_, buffer.data(), static_cast<DWORD>(buffer.size()), &bytesRead,
                      nullptr)) {
            if (!closing_) {
                emit processError(windowsError(QStringLiteral("ConPTY read failed")));
            }
            break;
        }
        if (bytesRead == 0) {
            break;
        }
        emit outputReady(buffer.first(static_cast<qsizetype>(bytesRead)));
    }
}

void NativeTerminalProcess::waitLoop() {
    WaitForSingleObject(process_, INFINITE);
    DWORD exitCode = 0;
    GetExitCodeProcess(process_, &exitCode);
    if (!closing_) {
        emit exited(static_cast<int>(exitCode));
    }
}

void NativeTerminalProcess::closeTerminal() {
    closing_ = true;
    if (process_ != nullptr && WaitForSingleObject(process_, 0) == WAIT_TIMEOUT) {
        TerminateProcess(process_, 1);
    }
    closeHandle(inputWrite_);
    if (reader_.joinable()) {
        CancelSynchronousIo(reader_.native_handle());
    }
    closeHandle(outputRead_);
    if (waiter_.joinable()) {
        waiter_.join();
    }
    if (reader_.joinable()) {
        reader_.join();
    }
    closeHandle(process_);
    if (pseudoConsole_ != nullptr) {
        ClosePseudoConsole(pseudoConsole_);
        pseudoConsole_ = nullptr;
    }
}

} // namespace

std::unique_ptr<ITerminalProcess> createNativeTerminalProcess() {
    return std::make_unique<NativeTerminalProcess>();
}

QString nativeTerminalBackendName() { return QStringLiteral("ConPTY"); }

} // namespace snack::terminal
