#include "terminal/NativeTerminalProcess.h"

#include <QFile>

#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>
#if defined(Q_OS_MACOS)
#include <util.h>
#else
#include <pty.h>
#endif

#include <atomic>
#include <chrono>
#include <thread>

namespace snack::terminal {
namespace {

[[nodiscard]] QString posixError(const QString& context, int errorCode = errno) {
    return QStringLiteral("%1: %2").arg(context, QString::fromLocal8Bit(std::strerror(errorCode)));
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
    std::atomic_bool processExited_{false};
    int masterFd_{-1};
    pid_t processId_{-1};
};

bool NativeTerminalProcess::start(const QString& workingDirectory, int columns, int rows,
                                  QString* error) {
    if (processId_ > 0) {
        if (error != nullptr) {
            *error = QStringLiteral("Terminal is already running");
        }
        return false;
    }

    const QByteArray directory = QFile::encodeName(workingDirectory);
    const QByteArray shell =
        qEnvironmentVariableIsSet("SHELL") ? qgetenv("SHELL") : QByteArrayLiteral("/bin/sh");
    winsize size{static_cast<unsigned short>(rows), static_cast<unsigned short>(columns), 0, 0};
    processId_ = forkpty(&masterFd_, nullptr, nullptr, &size);
    if (processId_ < 0) {
        const int errorCode = errno;
        processId_ = -1;
        if (error != nullptr) {
            *error = posixError(QStringLiteral("Cannot create a POSIX PTY"), errorCode);
        }
        return false;
    }
    if (processId_ == 0) {
        if (::chdir(directory.constData()) != 0) {
            _exit(126);
        }
        execl(shell.constData(), shell.constData(), static_cast<char*>(nullptr));
        _exit(127);
    }

    const int flags = fcntl(masterFd_, F_GETFL, 0);
    if (flags < 0 || fcntl(masterFd_, F_SETFL, flags | O_NONBLOCK) < 0) {
        const int errorCode = errno;
        if (error != nullptr) {
            *error = posixError(QStringLiteral("Cannot configure the POSIX PTY"), errorCode);
        }
        closeTerminal();
        return false;
    }

    closing_ = false;
    processExited_ = false;
    reader_ = std::thread([this] { readLoop(); });
    waiter_ = std::thread([this] { waitLoop(); });
    return true;
}

void NativeTerminalProcess::writeInput(const QByteArray& bytes) {
    qsizetype offset = 0;
    while (masterFd_ >= 0 && offset < bytes.size()) {
        const ssize_t count = ::write(masterFd_, bytes.constData() + offset,
                                      static_cast<size_t>(bytes.size() - offset));
        if (count > 0) {
            offset += static_cast<qsizetype>(count);
            continue;
        }
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            pollfd descriptor{masterFd_, POLLOUT, 0};
            if (::poll(&descriptor, 1, 100) >= 0) {
                continue;
            }
        }
        if (!closing_) {
            emit processError(posixError(QStringLiteral("POSIX PTY write failed")));
        }
        return;
    }
}

void NativeTerminalProcess::resizeTerminal(int columns, int rows) {
    if (masterFd_ < 0 || columns <= 0 || rows <= 0) {
        return;
    }
    winsize size{static_cast<unsigned short>(rows), static_cast<unsigned short>(columns), 0, 0};
    if (ioctl(masterFd_, TIOCSWINSZ, &size) < 0 && !closing_) {
        emit processError(posixError(QStringLiteral("POSIX PTY resize failed")));
    }
}

void NativeTerminalProcess::readLoop() {
    QByteArray buffer(8192, Qt::Uninitialized);
    while (!closing_) {
        pollfd descriptor{masterFd_, POLLIN, 0};
        const int ready = ::poll(&descriptor, 1, 100);
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (!closing_) {
                emit processError(posixError(QStringLiteral("POSIX PTY poll failed")));
            }
            break;
        }
        if (ready == 0) {
            continue;
        }
        if ((descriptor.revents & POLLIN) != 0) {
            const ssize_t count =
                ::read(masterFd_, buffer.data(), static_cast<size_t>(buffer.size()));
            if (count > 0) {
                emit outputReady(buffer.first(static_cast<qsizetype>(count)));
                continue;
            }
            if (count < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) {
                continue;
            }
        }
        if ((descriptor.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
            break;
        }
    }
}

void NativeTerminalProcess::waitLoop() {
    int status = 0;
    pid_t result = -1;
    do {
        result = waitpid(processId_, &status, 0);
    } while (result < 0 && errno == EINTR);
    processExited_ = true;
    if (!closing_) {
        const int exitCode = result > 0 && WIFEXITED(status) ? WEXITSTATUS(status) : 128;
        emit exited(exitCode);
    }
}

void NativeTerminalProcess::closeTerminal() {
    closing_ = true;
    if (processId_ > 0 && !processExited_) {
        kill(processId_, SIGHUP);
        if (waiter_.joinable()) {
            for (int attempt = 0; attempt < 20 && !processExited_; ++attempt) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            if (!processExited_) {
                kill(processId_, SIGKILL);
            }
        } else {
            kill(processId_, SIGKILL);
            int status = 0;
            while (waitpid(processId_, &status, 0) < 0 && errno == EINTR) {
            }
            processExited_ = true;
        }
    }
    if (waiter_.joinable()) {
        waiter_.join();
    }
    if (reader_.joinable()) {
        reader_.join();
    }
    if (masterFd_ >= 0) {
        ::close(masterFd_);
        masterFd_ = -1;
    }
    processId_ = -1;
}

} // namespace

std::unique_ptr<ITerminalProcess> createNativeTerminalProcess() {
    return std::make_unique<NativeTerminalProcess>();
}

QString nativeTerminalBackendName() { return QStringLiteral("POSIX PTY"); }

} // namespace snack::terminal
