# Developer Getting Started

## Current vertical slice

The `0.1.0-alpha.1` foundation includes a Qt Widgets shell, single-instance IPC, settings, declarative theme validation, an SQLite event repository, a SHA-256 content store, the common Agent interface, a fake streaming Agent, the session state machine, and Qt Test coverage. M2 is complete for automated-contract and no-model local acceptance: real Codex CLI discovery, stdio JSONL transport, initialization and authentication-state validation, paginated model capabilities, native thread start/resume with persisted identity, text turns and interruption, tool and reasoning-summary cards, inline and docked plans, command/file approval cards, user-input requests, usage reporting, and Codex as the main application's default Agent. The UI explicitly falls back to Mock when the CLI is unavailable, while normal tests still never invoke a real model.

The Agent menu preference applies only to the next conversation. On restart, a different preferred Agent or workspace creates an isolated conversation instead of moving an existing native thread ID across Agents. A custom Codex CLI path can be placed in the `agent/codexExecutable` settings key; an empty value probes the system `PATH`.

## Prerequisites

- CMake 3.22 or newer.
- Ninja.
- A C++20 Clang toolchain; Windows uses `clang-cl` with the MSVC ABI.
- Qt 6.8 or newer with Core, Gui, Widgets, Network, Sql, LinguistTools, and Test.

Windows also requires Visual Studio Build Tools and the Windows SDK. Put local Qt, CMake, and compiler paths in the untracked `CMakeUserPresets.json`; do not add personal paths to shared presets.

### Non-admin Windows verification profile

Qt also publishes the official `win64_llvm_mingw` architecture. It is useful for local portability builds and tests, but its MinGW ABI does not replace the release requirement for `clang-cl + MSVC ABI`. The current development machine uses this profile with Qt 6.8.3 and LLVM-MinGW 17.0.6.

```powershell
$qtRoot = 'D:\Qt\6.8.3\llvm-mingw_64'
$llvmRoot = 'D:\Qt\Tools\llvm-mingw1706_64'
$env:Path = "$llvmRoot\bin;$qtRoot\bin;$env:Path"

cmake -S . -B build/windows-llvm-mingw-debug -G Ninja `
  -DCMAKE_BUILD_TYPE=Debug `
  "-DCMAKE_CXX_COMPILER=$llvmRoot\bin\clang++.exe" `
  "-DCMAKE_PREFIX_PATH=$qtRoot" `
  -DSNACK_BUILD_TESTS=ON `
  -DSNACK_WARNINGS_AS_ERRORS=ON
cmake --build build/windows-llvm-mingw-debug --parallel
ctest --test-dir build/windows-llvm-mingw-debug --output-on-failure
```

Windows builds deploy the required Qt DLLs, plugins, and compiler runtime beside `src/snack.exe` by default, so the executable can be launched directly without adding Qt to the system `PATH`. Set `SNACK_DEPLOY_QT_AFTER_BUILD=OFF` to skip this development convenience step.

To create a clean install tree for packaging or portability checks:

```powershell
cmake --install build/windows-llvm-mingw-debug `
  --prefix "$PWD/build/windows-llvm-mingw-debug/install"
& "$PWD/build/windows-llvm-mingw-debug/install/bin/snack.exe"
```

The raw executable is portable only together with the files generated beside it by the deployment step. Do not copy `snack.exe` by itself.

## Build and test

Windows:

```powershell
cmake --preset windows-clang-debug
cmake --build --preset windows-clang-debug
ctest --test-dir build/windows-clang-debug --output-on-failure
```

Linux:

```bash
cmake --preset linux-clang-debug
cmake --build --preset linux-clang-debug
ctest --test-dir build/linux-clang-debug --output-on-failure
```

macOS:

```bash
cmake --preset macos-debug
cmake --build --preset macos-debug
ctest --test-dir build/macos-debug --output-on-failure
```

## Local data

The application stores its SQLite database, content blobs, and logs under `QStandardPaths::AppLocalDataLocation`. The fake Agent does not use the network or read Codex or Claude credentials.

Before upgrading an existing database schema, Snack keeps a timestamped `.pre-migration-*.bak` SQLite snapshot beside the database. A failed migration or a database created by a newer app version opens in read-only recovery mode: existing records remain available, while Agent startup and all event writes are blocked.

## Test policy

Every behavior change includes Qt Test coverage. CI builds and tests on all three desktop platforms, while Linux enforces at least 80% first-party line coverage using LLVM source-based coverage. CI never calls a real Agent.

Windows Qt 6.8.3/LLVM-MinGW baseline on 2026-08-27: 11/11 Debug tests pass, including the deployed runtime layout check; 10/10 instrumented tests pass; the single-instance IPC test passes 30 consecutive repetitions; all 119 Simplified Chinese source strings are translated; and first-party line coverage is 84.59%. The live local Codex CLI discovery, initialization, `account/read`, `model/list`, and ephemeral `thread/start` smoke test also passes without invoking a model. Live model turns remain opt-in and were not run. This local result does not claim validation with Windows `clang-cl`, Ubuntu, Debian, or macOS toolchains.
