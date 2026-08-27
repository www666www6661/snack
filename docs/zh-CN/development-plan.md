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

状态：进行中。现有 Composer 基础已包含有界自动增高、逐会话草稿、steer/队列控件、可编辑持久队列、参数化提示词模板、固定快捷键、界面缩放和实时系统/浅色/深色主题选择。会话身份现已持久化占位标题状态、仅用首条提示词的安全回退替换一次，并支持显式规范化重命名，为会话栏提供稳定标签且不覆盖已有标题。

- 会话栏、搜索/分组/标签/保存视图、独立聊天窗口。
- Composer附件、`@` 索引、模板、`/` 菜单、steer/队列。
- WebEngine Markdown/LaTeX/Mermaid、安全渲染与主题。
- 托盘、隐私通知、固定快捷键、缩放、布局方案。

验收：可日常使用 Codex完成多会话任务；GUI线程在高输出下保持响应。

## M4：工作区、Diff 与终端（`0.2.0`）

- 文件树/只读预览、外部编辑器、文件监听与符号索引。
- 基线快照、原生隔离能力探测、逐 hunk Diff、冲突处理。
- WorkspaceWriteLease。
- 多标签 Shell、ConPTY/POSIX PTY、独立终端窗口。

验收：直接修改与外部冲突路径安全可恢复；终端不污染 Agent事件。

## M5：Claude 协议技术验证（不发布功能承诺）

- 验证双向 stream-json 长连接、`system/init.capabilities` 与多轮边界。
- 验证 session resume/fork、SIGINT/interrupt、队列和图片。
- 验证内部本地 `--permission-prompt-tool` 桥，不修改用户 MCP配置。
- 验证模型/effort/permission 热切换是否有稳定 C++官方通道。
- 冻结 Claude 最低版本与降级矩阵。

退出条件：没有终端屏幕解析；未公开控制报文不成为稳定依赖。

## M6：Claude 适配器（`0.3.0`）

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
