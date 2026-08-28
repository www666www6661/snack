# Claude 协议技术验证

核对日期：2026-08-28

## 1. 结论

M5 将 Claude Code `2.1.219` 冻结为最低版本，本机参考版本为 `2.1.245`。M6 可以使用
C++20/Qt 6，基于公开的双向 `stream-json` 消息类型、官方 CLI 会话参数和 Snack 内部 MCP
stdio 权限桥实现 Claude Adapter；不得复制 TypeScript SDK 未公开的 control envelope。

这是协议决策，不代表 Claude 功能已经发布。Anthropic 文档中的模型、推理强度和权限热切换
属于 Agent SDK 方法，而官方没有 C++ SDK。Snack 必须明确显示“下一轮生效并重启恢复”，不能
假装已经获得即时热切换能力。

## 2. 证据边界

本机 Windows 11 检查使用 Claude Code `2.1.245`，全程没有发送有效用户提示。实际检查包括
版本/参数解析、`--init-only`、stream EOF 与合成畸形输入、resume/fork 参数，以及 C++ MCP
握手。MCP 探针使用 `--bare`、内联 `--mcp-config` 和 `--strict-mcp-config`，只观察到
`initialize`、`notifications/initialized`、`tools/list`，没有改动用户 Claude/MCP 配置。

版本化 JSONL fixture 来自官方 SDK 类型和示例的最小合成数据，覆盖 `system/init` 前置启动
事件、开放能力集、partial/完整 assistant 消息、两个 result 分隔的 Turn、排队 UUID、图片块、
未知字段、畸形记录和跨 Session 拒绝。它们不是录制的模型输出。

本次没有执行真实模型 Turn、工具调用、权限请求、提问、中断、队列运行或 resume/fork 运行。
这些路径可能产生模型用量或依赖正在执行的 Turn，必须保持显式 opt-in；默认测试绝不启动
Claude。

## 3. 冻结的传输契约

- 使用 `claude -p`、双向 `stream-json`、verbose、partial message 和 user message replay 启动
  持久进程。
- 允许启动事件出现在 `system/init` 之前；以 `system/init.session_id` 和 capabilities 为权威，
  忽略未知 capability 名称。
- 每条出站用户消息分配 UUID；使用 result 的 `user_message_uuid` 关联请求，每个 result 作为
  一个 Turn 边界。
- 可编辑队列保留在 Snack，当前 Turn 结束后才发给 Claude，避免依赖未公开 control envelope
  取消队列。
- 图片只使用官方 base64 image content block；序列化前执行 MIME 与大小限制。
- 只按原生 Session ID 使用 `--resume`；`--fork-session` 必须与 resume/continue 同用。不得用
  GUI 历史拼接伪造原生上下文。
- 使用官方进程中断语义结束当前执行；必要时重启并恢复。中断的用户消息绝不自动重发。
- 只有 `interrupt_receipt_v1` 存在时才解析回执；只有同时存在
  `interrupt_cancel_queued_v1` 时才接受 cancelled。回执 payload 仅作为诊断契约，M6 不得
  自行发明原始 control envelope。

## 4. 权限桥

Snack 为每个会话持有仅 stdio 的本地 MCP 服务器，通过内联 strict MCP 配置传入，不注册用户
MCP，也不修改 Claude settings。每个请求按 request/tool-use identity 幂等处理，并绑定到当前
Agent Session；断连、畸形输入、过期身份和未知策略一律拒绝。

工具权限和 `AskUserQuestion` 映射到公共权限卡/问题卡。Claude 自动决定的调用不会经过桥，
因此仍须处理 result 中的权威 denial 列表和普通工具事件。探针工具对任何意外调用都返回拒绝。

## 5. 运行时控制降级

| GUI 控件 | 官方热切换入口 | 纯 C++ M6 行为 |
|---|---|---|
| 模型 | TypeScript `Query.setModel()` / `applyFlagSettings()` | 仅显示当前值和显式配置值；Turn 中修改标记为下一轮，随后用 `--model` 重启并 resume |
| 推理强度 | TypeScript `applyFlagSettings({effortLevel})` | 严格限定 `low`、`medium`、`high`、`xhigh`、`max`；排队后用 `--effort` 重启并 resume |
| 权限模式 | TypeScript `setPermissionMode()` / `applyFlagSettings()` | CLI 模式修改排到下一轮，用 `--permission-mode` 重启并 resume；当前待处理 GUI 权限卡立即使用最新桥策略 |

Agent 类型始终不可变；这些控件都不能把 Codex 会话换成 Claude，反之亦然。Snack 必须自行严格
校验：Claude 2.1.245 对未知 effort 只警告并静默回退，未知 model 在模型 Turn 前不会校验。

## 6. 最低版本与降级矩阵

| 门槛 | 文档版本 | Snack 决策 |
|---|---:|---|
| 权限 request ID/null callback | `2.1.199` | 权限桥身份模型必需 |
| `capabilities` 与 `interrupt_receipt_v1` | `2.1.205` | 必须按 capability 检测 |
| result `user_message_uuid` | `2.1.216` | 确定性 Turn 关联必需 |
| `interrupt_cancel_queued_v1` | `2.1.219` | 完成队列回执契约并确定最低版本 |

等于最低版本的预发布版不满足要求。更旧版本显示升级提示；更高版本仍须通过命令解析和严格
初始化。未知消息保留为 raw 诊断，但没有对应 capability 时不得启用功能。

## 7. M6 交接

M6 可以把可丢弃解析器和回执规则迁入 Claude Adapter，但必须保持公共 `IAgentAdapter` 契约，
不得在共享 UI 散落 Claude 分支。任何 opt-in 真实检查之前，必须先用 Fake Process 覆盖分帧、
init 顺序、Turn 关联、进程丢失、重启恢复、MCP 请求身份、等待请求清理、畸形终态和上述所有
降级路径。
