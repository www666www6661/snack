# 协议能力矩阵

核对日期：2026-08-28

本机参考版本：Codex CLI `0.149.0`，Claude Code `2.1.245`

## 1. 结论

Codex 使用 `codex app-server` 的 stdio JSONL 传输。它是官方面向富客户端的 JSON-RPC 接口，当前文档覆盖认证、模型、线程、Turn、事件、审批、Diff、文件系统和会话恢复。

Claude 使用 `claude -p --input-format stream-json --output-format stream-json --verbose --include-partial-messages --replay-user-messages` 的持久进程。M5 已确认公开 stream、Session、图片、队列、能力和 MCP 桥契约。实时控制仍仅由 SDK 提供，官方没有 C++ SDK，因此 Snack 冻结为“下一轮重启并 resume”，绝不依赖未公开 control 消息。

## 2. 状态定义

- **Native**：有公开、适合 C++ 客户端直接使用的官方 CLI 协议。
- **Bridge**：通过官方支持的本地桥接机制实现，例如 Claude `--permission-prompt-tool` MCP。
- **Restart**：结束当前请求后重启 CLI，并恢复原生会话。
- **Probe**：官方能力存在，但 C++ 直连方式或稳定性需要技术验证。
- **Unavailable**：没有可靠官方能力，GUI 必须隐藏或禁用。

## 3. 核心能力

| 能力 | Codex app-server | Claude Code CLI | Snack 策略 |
|---|---|---|---|
| 初始化与能力协商 | Native：`initialize` | Native：`system/init.capabilities` | 使用能力而非仅比较版本 |
| 模型枚举 | Native：`model/list` | SDK-only：`supportedModels()`，CLI 无等价公开命令 | 仅显示当前值和显式配置值 |
| 新会话 | Native：`thread/start` | Native：新 `-p` 流 | 保存原生 ID |
| 恢复会话 | Native：`thread/resume` | Native：`--resume` | 不拼接历史冒充上下文 |
| 列出/读取历史 | Native：`thread/list`, `thread/read` | 有限：CLI 会话选择与本地记录；脚本接口较弱 | GUI 维护索引，原生上下文按 ID恢复 |
| 流式文本 | Native：item delta | Native：`stream-json` partial events | 增量渲染 |
| 图片输入 | Native：`image`/`localImage` | Native：streaming input 支持图片消息 | 按模型能力显示 |
| 执行中补充 | Native：`turn/steer` | 没有稳定 C++ 控制面 | 保留为 GUI 队列中的下一 Turn |
| 排队消息 | GUI + Native turn | 官方 stream 队列存在 | 发送前保留在 GUI，维持可编辑 |
| 中断 | Native：`turn/interrupt` | SDK 回执 + 官方进程信号 | 正常中断进程；不发明 control wire，不自动重发 |
| 热切换模型 | Native：下一 `turn/start.model` | SDK-only：`setModel()` | 标记下一轮，用 `--model` 重启并 resume |
| 热切换推理强度 | Native：`turn/start.effort` | SDK-only：`applyFlagSettings()` | 校验后用 `--effort` 重启并 resume |
| 热切换访问层级 | Native：per-turn sandbox/approval | SDK-only：`setPermissionMode()` | 用 `--permission-mode` 重启；待处理权限卡立即使用当前 GUI 策略 |
| 命令审批 | Native：server request | Bridge：C++ `--permission-prompt-tool` MCP 握手已验证 | 每 Session 本地桥，按请求 ID 幂等 |
| 文件审批 | Native：file-change request | Bridge/Probe：工具审批回调模型 | 不可用时依赖会话安全模式和 GUI 快照 |
| 用户澄清问题 | Native：`tool/requestUserInput` | Bridge/Probe：`AskUserQuestion` 进入工具审批回调 | 转为统一问题卡片 |
| 计划/待办 | Native：plan item/events | Native/Probe：Todo 工具事件 | 不确定时显示普通工具卡片，不虚构进度 |
| 工具调用 | Native：item events | Native：assistant/tool_use/tool_result | 未知工具使用通用卡片 |
| 推理流 | Native：summary/raw reasoning delta（模型允许时） | Native：thinking blocks/partial stream（配置允许时） | 只显示协议实际返回内容 |
| Token/上下文 | Native：token usage events | Native/Probe：result usage；SDK `getContextUsage()` | 无可靠字段时隐藏 |
| 费用 | 仅官方事件提供时 | CLI result 可返回估算费用 | 标注来源；产品不自行估算 |
| Diff 事件 | Native：`turn/diff/updated`, fileChange | 工具事件 + 文件监听 | 公共 Diff 模型统一计算 |
| 文件监听 | Native：`fs/watch` 可辅助 | GUI `QFileSystemWatcher`/扫描 | Agent 事件优先，文件监听补充 |
| 原生隔离改动 | 不作默认假设 | 不作默认假设 | 只有握手明确支持时启用，否则真实文件快照 |
| 会话分支 | Native：`thread/fork` | Native：`--fork-session` | 产品不提供历史消息重写；仅内部恢复用途 |
| 认证状态 | app-server auth endpoints/CLI | `claude auth status` JSON | 不读取或保存令牌 |
| Slash 命令 | 协议/CLI 能力 | SDK `supportedCommands()` 或 CLI 文档 | 无可靠枚举时仅显示 GUI 命令 |

## 4. Codex 传输约束

- 首选 `codex app-server` 默认 stdio：一行一个 JSON 消息。
- 初始化顺序固定为 `initialize` 请求后发送 `initialized` 通知。
- 请求、响应与 server request 按 ID关联；事件存储必须保留原始消息。
- 线程和 Turn 分离建模；`turn/start` 可以覆盖 `model`、`effort`、`cwd`、审批与沙箱策略。
- 使用 CLI 生成的 JSON Schema 建立版本化契约测试：`codex app-server generate-json-schema`。
- 模型、推理强度、输入模态和人格控件必须由完整分页 `model/list` 驱动；选择失效时使用服务端声明的默认值。
- WebSocket 传输在官方文档中仍为实验且不适合本地生产集成，Snack 首版不使用。

## 5. Claude 传输约束

- 使用 `-p` 非交互模式与双向 `stream-json`，保持 stdin 开放以支持多轮输入。
- `system/init` 是能力、模型、工具、MCP 和插件状态的权威启动事件。
- `result` 是每轮完成边界，并包含会话 ID、使用量及可能的费用字段。
- 使用内联 strict、每 Session 独立的 C++ MCP stdio `--permission-prompt-tool`；不修改用户配置。
- 不把 TypeScript SDK 的内部 CLI 控制报文当作稳定公共 C++ API。若技术验证只能通过未公开报文实现，则该能力降级。
- 进程结束后使用 `--resume <session-id>`；中断请求绝不自动重发。

## 6. 最低版本政策

Codex 最低版本为 `0.149.0`，Claude 最低版本为 `2.1.219`；等于最低版本的预发布版不满足要求。本机 Claude 参考版本为 `2.1.245`。旧版本显示升级提示，不做屏幕解析；新版本仍须通过命令解析、初始化、capability 和严格运行时校验。完整证据与降级矩阵见 [Claude 协议技术验证](claude-protocol-spike.md)。版本合格条件：

1. 能力握手可用且可记录。
2. 会话创建、恢复、流式事件、取消和错误路径通过契约测试。
3. 权限请求能可靠关联会话、轮次和工具请求。
4. 低版本收到明确升级提示，不尝试屏幕解析。
5. 未识别新版本在握手成功时允许运行，否则安全失败。

## 7. 官方资料

- OpenAI Docs: https://developers.openai.com/codex/app-server
- Claude Code CLI reference: https://code.claude.com/docs/en/cli-reference
- Claude Code non-interactive mode: https://code.claude.com/docs/en/headless
- Claude Agent SDK streaming input: https://code.claude.com/docs/en/agent-sdk/streaming-vs-single-mode
- Claude Agent SDK user input and approvals: https://code.claude.com/docs/en/agent-sdk/user-input
- Claude Agent SDK TypeScript control surface: https://code.claude.com/docs/en/agent-sdk/typescript

官方页面会随 CLI 更新。每次发布前重新生成/录制契约样本，并在发布说明中记录实际验证版本。
