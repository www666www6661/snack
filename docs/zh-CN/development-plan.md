# 开发计划

原则：先用 Codex 完成垂直链路，再实现 Claude；每个里程碑同时完成测试与双语文档。正式功能代码在产品/架构/设计基线确认后开始。

## M0：规格与设计基线（`0.0.x`）

交付：本目录双语文档、高保真静态设计稿、官方协议能力矩阵、风险清单。验收：文档链接通过，关键页面覆盖，用户确认基线。

## M1：工程骨架与测试基础（目标 `0.1.0-alpha.1`）

状态：实现完成，正式跨工具链验收仍待完成。首个垂直切片已经建立应用壳、设置与主题、单实例 IPC、托盘生命周期、窗口/布局恢复、带备份/只读恢复的版本化 SQLite 迁移、事件仓库、内容寻址存储、统一 Agent 接口、模拟流式会话和 Qt Test。2026-08-27 的 Windows Qt 6.8.3/LLVM-MinGW 严格警告基线通过 11/11 项 Debug 测试、10/10 项覆盖率测试、30 次 IPC 重复和 84.59% 第一方代码行覆盖率；这不替代 Windows `clang-cl + MSVC ABI`、Ubuntu、Debian 或 macOS 验证。

- CMake/Qt 6.8/C++20 项目、Clang Presets、Qt Test、LLVM coverage。
- 应用元数据、国际化、主题 Schema、日志、单实例、设置和模拟 Agent。
- SQLite迁移、EventStore、ContentStore接口。
- GitHub Actions：格式、构建、测试、80% 覆盖率门禁。

验收：三个桌面平台能启动空壳，模拟 Agent 完成一轮流式对话，崩溃后恢复 UI事件。

## M2：Codex 协议垂直链路（`0.1.0-alpha.2`）

状态：截至 2026-08-27，自动化契约与本机无模型验收完成。带有冻结最低版本 `0.149.0` 的 CLI 探测、可注入 `QProcess` 传输、有界 JSONL 分帧、初始化握手、`account/read`、请求 ID 关联与超时、分页 `model/list`、逐模型能力、`thread/start`/`thread/resume`/`thread/list`/`thread/read`、Thread/Session 身份持久化、逐轮模型/推理强度/访问层级覆盖、`turn/steer`/`turn/interrupt`、文本与工具流、推理摘要、行内/任务 Dock 计划、错误收口、命令/文件审批、`tool/requestUserInput`、Thread Token/上下文用量、重连/进程丢失清理、主应用 Codex/Mock 选择与 Codex CLI `0.149.0` 版本化 Schema/fixture 均已完成。

- `codex app-server` stdio transport、Schema生成与握手。
- model/list、thread start/resume/read/list、turn start/steer/interrupt。
- 流式消息、工具、推理、计划、错误、使用量和审批。
- CLI 检测、原生会话 ID、能力禁用与原始协议诊断。

验收证据：fixture 覆盖正常、失败、取消、中断、恢复、过期事件、畸形终态和等待请求清理路径；本机真实 CLI 已完成探测、初始化、认证状态读取、模型发现和临时 Thread 创建，全程不调用模型。真实 `turn/start`、审批、取消与恢复可能产生模型用量，因此保留为显式 opt-in 的人工检查；本次里程碑收口未执行这些检查，它们不属于默认安全测试门禁。

## M3：日常会话体验（`0.1.0-beta.1`）

状态：截至 2026-08-27，本机 Windows 实现与自动化验收完成。会话栏、历史会话 Agent 类型隔离、本地搜索/分组/标签/保存视图、归档/删除/导出、独立窗口、附件与工作区引用、模板、steer/持久队列、隐私通知、工作台布局、高输出合帧和安全富文本均已实现。Qt 官方 6.8.3 MSVC WebEngine 构建与官方 LLVM-MinGW 纯文本降级构建均通过 16/16 项严格警告 Debug 测试；插桩降级构建通过 15/15 项测试，第一方行覆盖率为 84.10%，翻译完成 232/232。以上仅是本机 Windows 证据，不代表 Ubuntu、Debian、macOS、打包或长期生产验证完成。

- 会话栏、搜索/分组/标签/保存视图、独立聊天窗口。
- Composer附件、`@` 索引、模板、`/` 菜单、steer/队列。
- WebEngine Markdown/LaTeX/Mermaid、安全渲染与主题。
- 托盘、隐私通知、固定快捷键、缩放、布局方案。

验收：可日常使用 Codex完成多会话任务；GUI线程在高输出下保持响应。

## M4：工作区、Diff 与终端（`0.2.0`）

状态：截至 2026-08-27，本机 Windows 实现与自动化验收完成。M4-A 已实现有界 Canonical 工作区浏览、安全预览/外部打开、文件监听和符号索引；M4-B 已实现有界基线与原生 Diff、写入前比较的拒绝修改、外部冲突保护、诚实的官方隔离能力报告和 Canonical 写租约；M4-C 已实现可注入终端会话、流式 UTF-8/ANSI 安全纯文本投影、Windows ConPTY 与 POSIX `forkpty` 后端、可交互移动标签、独立终端窗口、主题/缩放接入，以及证明 Shell 输出绝不成为 Agent 事件的可执行 SQLite 隔离测试。

Qt 官方 6.8.3 LLVM-MinGW 降级构建与 Qt 官方 6.8.3 `msvc2022_64` WebEngine 构建在本机 Windows 11 上均通过 20/20 项严格警告 Debug 测试；LLVM-MinGW 插桩构建通过 19/19 项测试，第一方行覆盖率为 83.00%；翻译完成 247/247，124 个 C++ 源文件/头文件全部通过格式门禁。ConPTY 已通过真实本机 Shell 冒烟测试。POSIX 后端已实现并通过契约测试，但尚未在 Ubuntu 22.04 或 Debian 13 实机运行；以上状态也不代表 Windows 10、macOS、安装包或长期生产验证完成。

- 文件树/只读预览、外部编辑器、文件监听与符号索引。
- 基线快照、原生隔离能力探测、逐 hunk Diff、冲突处理。
- WorkspaceWriteLease。
- 多标签 Shell、ConPTY/POSIX PTY、独立终端窗口。

验收：直接修改与外部冲突路径安全可恢复；终端不污染 Agent事件。

## M5：Claude 协议技术验证（不发布功能承诺）

状态：截至 2026-08-28，官方文档契约与本机 Windows 无模型验收完成。Claude Code 最低版本冻结为 `2.1.219`，本机参考版本为 `2.1.245`。可丢弃 C++/Qt stream 与 interrupt 解析器已覆盖 init 顺序、开放 capability、多 Turn result 边界、UUID 队列、图片、畸形/跨 Session 输入、回执门控和运行时控制降级。只会拒绝的 C++ MCP stdio 服务器同时通过 Qt 契约测试，以及真实 Claude `--init-only --bare --strict-mcp-config --permission-prompt-tool` 握手；没有读取或修改用户 MCP 配置。

Qt 官方 6.8.3 LLVM-MinGW fallback 与 MSVC WebEngine 构建在本机 Windows 11 上均通过 21/21 项严格警告测试；插桩 fallback 构建通过 20/20 项测试，第一方行覆盖率为 83.14%。翻译保持 247/247，130 个 C++ 源文件/头文件通过格式门禁，版本化 Claude JSON/JSONL 有效且已脱敏，双语文档链接有效。本次没有执行真实模型 Turn、工具、权限请求、提问、中断、队列运行或 resume/fork 运行。以上不代表 Windows 10、Linux、macOS、打包或生产 Claude Adapter 已验证；M6 必须从已记录的降级矩阵开始。

- 验证双向 stream-json 长连接、`system/init.capabilities` 与多轮边界。
- 验证 session resume/fork、SIGINT/interrupt、队列和图片。
- 验证内部本地 `--permission-prompt-tool` 桥，不修改用户 MCP配置。
- 验证模型/effort/permission 热切换是否有稳定 C++官方通道。
- 冻结 Claude 最低版本与降级矩阵。

退出条件：没有终端屏幕解析；未公开控制报文不成为稳定依赖。

## M6：Claude 适配器（`0.3.0`）

状态：截至 2026-08-28，本机 Windows 实现与无模型自动化验收完成。正式运行时已实现 CLI 发现、有界双向流、新建/恢复 Session 身份、连续 Turn 关联、可见文本流、thinking 脱敏、工具、用量、提问、图片附件、逐 Session 认证 MCP 权限桥、进程丢失清理和主应用运行时选择。Codex/Claude 公共契约覆盖 Agent 类型不可变、公共访问层级、无效操作拒绝、工作目录缺失安全以及幂等清理。

Qt 官方 6.8.3 LLVM-MinGW fallback 与 MSVC WebEngine 构建在本机 Windows 11 上均通过 23/23 项严格警告测试；插桩 fallback 构建通过 22/22 项测试，第一方行覆盖率为 82.85%。翻译完成 248/248，145 个 C++ 源文件/头文件全部通过格式门禁，63 个 JSON/JSONL fixture 文件均可解析，36 个 Markdown 文件的本地链接全部有效。本次没有执行真实模型 Turn 或真实 Claude 权限调用。Claude 设置调整明确采用下一轮进程重启加官方 `--resume`，不宣称真正热切换；Claude steering 仍不可用。以上状态不代表 Windows 10、Ubuntu、Debian、macOS、安装包或长期生产验证完成。

使用 M5确认的能力实现会话、流式事件、审批、问题、恢复和设置降级。公共 UI不得出现 Claude 特例分支散落。运行 Codex/Claude公共契约套件。

## M7：数据生命周期与发布（`0.4.0`）

- 30天/10GB清理、存储统计、普通/加密完整备份与完全替换恢复。
- Windows/Linux/macOS平台集成、MSI、deb、app.zip。
- 许可证、升级迁移、安装/卸载测试和发布清单。

## M8：稳定化（目标 `1.0.0`）

性能、长期运行、协议兼容、恢复、DPI、多显示器、国际化和安全回归。清理所有未决 P0/P1缺陷；完成正式兼容矩阵。

## 风险优先级

1. Claude C++ 稳定控制面不足。
2. 直接文件修改的 hunk拒绝与外部冲突。
3. Qt WebEngine内存、渲染崩溃与安全边界。
4. 跨平台终端，尤其 Windows ConPTY与 Linux ARM64。
5. Debian 13/Ubuntu 22.04 同一 deb兼容与 Qt WebEngine分发。
6. 多会话输出、文件监听和 SQLite写入的背压。

每项风险先做可丢弃技术验证，再固化公共接口。
