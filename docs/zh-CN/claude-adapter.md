# Claude 适配器

M6 将 M5 的协议结论实现为正式 C++20/Qt 6 Agent 运行时。适配器只使用 Claude Code CLI
公开的流、Session 参数和 MCP 权限工具集成；不解析终端显示，不安装 SDK sidecar，不修改用户
MCP 设置，也不依赖未公开的 control 报文。

## 发现与启动

Windows 自动发现 `claude.cmd`、`claude.exe` 或 `claude`，POSIX 自动发现 `claude`；自定义
Claude 路径与 Codex 路径分开保存。发现过程执行三项无模型检查：

1. 解析 `claude --version`，要求不低于 `2.1.219`；恰好等于最低版本的预发布版不满足要求。
2. 从 `--help` 确认公开的流、Session、模型、effort 和权限参数。
3. 使用空内联 MCP 运行不发送用户消息的
   `--init-only --safe-mode --strict-mcp-config` 空流探针，不可能启动模型 Turn。

正式 Session 使用持久进程：

```text
claude -p
  --input-format stream-json
  --output-format stream-json
  --verbose
  --include-partial-messages
  --replay-user-messages
```

新会话通过 `--session-id` 使用 Snack 生成的 UUID，恢复会话使用 `--resume`。模型、effort、
权限模式、工作目录、内联权限 MCP 和权限工具名均为显式参数。正式启动不使用
`--safe-mode`、`--bare` 或 `--strict-mcp-config`，因此保留 Claude 的正常扩展和用户 MCP。

## 传输与身份

`ClaudeStreamClient` 使用可注入进程传输。它接受分片 LF/CRLF JSONL，单帧上限为 4 MiB，
stderr 诊断最多保留 64 KiB，具有 init 超时，并会强制终止拒绝关闭的进程。`system/init` 前
允许 informational 或未知启动事件，但不允许 Turn 事件。畸形帧、重复 init、超大帧或其他
Session 的记录都会失败关闭。

init 返回的 Session ID 和规范化工作目录必须与请求一致。Claude 只有一个公开 Session
身份，不像 Codex 分为 Thread 与 Session，因此 Snack 将同一 Claude Session ID 持久化到两个
公共原生身份字段。结果只能通过必需的 `user_message_uuid` 关联 GUI Turn；过期结果不能关闭
另一个 Turn。

## 事件与输入

可见文本 delta 与最终 assistant 消息会进行一致性合并，避免 partial/final 双通道重复显示。
文本、工具、工具结果、用量、警告、Turn 终态和未知前向兼容事件都映射到公共 `AgentEvent`。
图片附件按 20 MiB 上限读取并发送为 base64 图片块；普通文件保持显式路径引用。

Claude thinking 和签名内容不会作为 raw 协议数据持久化，也不会作为思维链显示。Snack 只发出
推理开始/结束状态，并在已脱敏诊断中写入 `redacted` 标记。

`AskUserQuestion` 复用公共阻塞问题卡。单选题保留选项；公共问题卡暂不支持多选，因此多选题
降级为自由文本。校验后的答案作为标准 `tool_result` 写回同一 Session。

## 权限桥

每个 Session 都有随机本地 socket 名和 256 位 token。内联 MCP 配置启动随应用分发的 C++
`snack_claude_permission_server`。helper 实现 MCP stdio 的 `initialize`、`tools/list`、
`tools/call` 和 `ping`，只把权限调用通过已认证本地 socket 转发给 GUI。

权限调用复用 Codex 的命令/文件审批卡。单次允许返回 `behavior: allow` 和原始输入；仅当
Claude 提供权限建议时才显示“本 Session 允许”，并通过 `updatedPermissions` 返回建议。拒绝和
取消返回明确 deny。帧上限为 1 MiB，请求 ID 只能使用一次；Turn、Session 或权限桥关闭时，
所有待处理请求默认拒绝。权限桥不会读取或写入 Claude 用户 MCP 配置。

## 运行时控制与中断

Claude 没有公开 C++ 实时 setter。Turn 运行中修改的设置属于下一轮；发送下一轮之前，Snack
停止进程，并用同一 Session 的 `--resume` 加新的 `--model`、`--effort` 和
`--permission-mode` 重启。恢复 init 校验完成前，原 GUI Turn ID 和消息保持排队，因此消息
不会误用旧设置运行。

映射如下：

| Snack | Claude CLI |
| --- | --- |
| low / medium / high / xhigh / max | 同名 `--effort` 值 |
| 旧值 minimal | low |
| 旧值 ultra | max |
| strict | manual |
| workspace | acceptEdits |
| full | bypassPermissions |

中断使用同一公开降级路径：终止当前 Claude 进程，将 GUI Turn 关闭为 interrupted，然后恢复
Session。Snack 不发送推测得到的私有 interrupt control 报文。Claude 不支持 steering。

## 测试边界

默认 Qt 测试使用注入传输、由公开示例派生的合成 JSONL、临时本地 socket 和随应用分发的 MCP
helper，覆盖发现、分帧、init、多轮关联、图片编码失败、可见流、thinking 脱敏、工具结果、
提问、权限决定、设置重启恢复、中断、超时、进程丢失和清理。测试绝不向已安装 Claude CLI
发送有效 prompt，也不会产生模型用量。真实模型 Turn 和真实权限调用仍是显式 opt-in 人工检查。

