#pragma once

#include "terminal/ITerminalProcess.h"

#include <memory>

namespace snack::terminal {

[[nodiscard]] std::unique_ptr<ITerminalProcess> createNativeTerminalProcess();
[[nodiscard]] QString nativeTerminalBackendName();

} // namespace snack::terminal
