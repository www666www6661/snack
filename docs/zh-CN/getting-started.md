# 开发者入门

## 当前垂直切片

`0.1.0-alpha.1` 工程骨架包含 Qt Widgets 主窗口、单实例 IPC、设置、声明式主题校验、SQLite 事件仓库、SHA-256 内容存储、统一 Agent 接口、模拟 Agent、会话状态机和 Qt Test 测试。M2 已加入真实 Codex 的 CLI 探测、stdio JSONL 传输、初始化握手、分页模型能力、带身份持久化的原生 Thread 创建/恢复、文本 Turn 流与中断、工具与推理摘要卡片、行内及停靠计划、命令/文件审批卡片，并将 Codex 作为主应用默认 Agent。CLI 不可用时界面明确回退 Mock；普通测试仍不调用真实模型。

Agent 菜单中的偏好只影响下一会话。重启后，如果偏好 Agent 或工作目录与最后会话不同，零食会创建新的隔离会话，不会把现有原生 Thread ID切换给另一个 Agent。Codex CLI 的自定义路径可预先写入设置键 `agent/codexExecutable`；空值表示从系统 `PATH` 自动探测。

## 前置环境

- CMake 3.22 或更高版本。
- Ninja。
- C++20 Clang 工具链；Windows 使用 `clang-cl` 与 MSVC ABI。
- Qt 6.8 或更高版本，包含 Core、Gui、Widgets、Network、Sql、LinguistTools 和 Test。

Windows 还需要 Visual Studio Build Tools 与 Windows SDK。Qt、CMake 和编译器的本机位置写入不提交的 `CMakeUserPresets.json`，不要把个人路径写入公共 Preset。

### Windows 无管理员权限验证配置

Qt 官方还提供 `win64_llvm_mingw` 架构，可用于本地可移植性编译和测试，但它使用 MinGW ABI，不能代替正式 Windows 包要求的 `clang-cl + MSVC ABI` 验证。当前开发机使用 Qt 6.8.3 和 LLVM-MinGW 17.0.6 完成了这一验证。

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

Windows 构建默认会把所需 Qt DLL、插件和编译器运行库部署到 `src/snack.exe` 旁边，因此无需把 Qt 加入系统 `PATH` 即可直接启动。若不需要这项开发便利功能，可设置 `SNACK_DEPLOY_QT_AFTER_BUILD=OFF`。

需要生成用于打包或可移植性检查的干净安装目录时，执行：

```powershell
cmake --install build/windows-llvm-mingw-debug `
  --prefix "$PWD/build/windows-llvm-mingw-debug/install"
& "$PWD/build/windows-llvm-mingw-debug/install/bin/snack.exe"
```

只有连同部署步骤在可执行文件旁生成的文件一起保留时，程序才可直接运行；不能只复制裸 `snack.exe`。

## 构建与测试

Windows：

```powershell
cmake --preset windows-clang-debug
cmake --build --preset windows-clang-debug
ctest --test-dir build/windows-clang-debug --output-on-failure
```

Linux：

```bash
cmake --preset linux-clang-debug
cmake --build --preset linux-clang-debug
ctest --test-dir build/linux-clang-debug --output-on-failure
```

macOS：

```bash
cmake --preset macos-debug
cmake --build --preset macos-debug
ctest --test-dir build/macos-debug --output-on-failure
```

## 本地数据

应用使用 `QStandardPaths::AppLocalDataLocation` 保存 SQLite 数据库、内容存储和日志。模拟 Agent 不联网，也不读取 Codex 或 Claude 凭据。

升级现有数据库 Schema 前，零食会在数据库旁保留带时间戳的 `.pre-migration-*.bak` SQLite 快照。迁移失败或数据库由更高版本应用创建时，程序进入只读恢复模式：已有记录仍可读取，但 Agent 启动和所有事件写入都会被阻止。

## 测试规则

新增行为必须同步添加 Qt Test。CI 在三个桌面平台构建并运行测试，Linux 使用 LLVM source-based coverage 执行第一方代码 80% 行覆盖率门禁。CI 不调用真实 Agent。

2026-08-26 的 Windows LLVM-MinGW 基线：9/9 Debug 测试通过（包含运行库部署布局检查）；单实例 IPC 连续重复 30 次通过；第一方代码行覆盖率为 84.89%；本机真实 Codex CLI 初始化、`model/list` 与临时 `thread/start` 烟雾测试也通过，全程不调用模型。正式 M1 验收仍需完成 Windows `clang-cl`、Linux 和 macOS 构建。
