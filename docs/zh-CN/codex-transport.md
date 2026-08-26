# Codex App Server 传输

## 当前状态

M2 的连接、Thread、文本 Turn、审批、用户提问、用量与活动事件垂直链路已在 Codex CLI `0.149.0` 上完成 fixture 验证，该版本也是当前最低支持版本。当前代码已经建立独立的 CLI 探测、子进程传输、JSONL 协议解析、初始化握手、分页 `model/list`、原生 `thread/start`/`thread/resume`、`turn/start`、文本流、工具执行、推理摘要、计划、`turn/interrupt`、命令/文件审批响应、问题回答和 Token/上下文显示。主应用默认探测并启动 Codex；CLI 不可用或版本过旧时明确回退到 Mock Agent。

官方协议依据：[OpenAI Docs - Codex App Server](https://developers.openai.com/codex/app-server)。默认传输是 stdio 上逐行 JSON；线上消息省略 `jsonrpc: "2.0"`。每个连接必须先发送 `initialize` 请求，收到成功响应后再发送 `initialized` 通知。

## 模块边界

- `IProcessTransport` 隔离进程生命周期和字节流，协议测试使用假实现。
- `QProcessTransport` 封装异步 `QProcess`、独立 stdout/stderr 和退出错误。
- `CodexCliDiscovery` 查找 CLI，验证版本与 `app-server --help`，并生成平台启动参数。
- `CodexProtocol` 分类请求、响应、通知和非法消息，保留完整原始对象。
- `CodexAppServerClient` 负责 JSONL 分帧、初始化、请求 ID 关联、通知和 server request 转发。
- `CodexModelCatalog` 校验模型分页，并保留模型的推理强度说明、输入模态、人格支持和默认标记。
- `CodexThreadLifecycle` 校验 `thread.id`、`thread.sessionId` 与 cwd，并定义经过审计的访问层级映射。
- `CodexTurnLifecycle` 构造逐轮设置覆盖，严格解析 Turn、Item、文本 Delta 与 Error 通知。
- `CodexApprovalLifecycle` 校验命令与文件审批的路由字段，保留展示元数据，并构造与版本 Schema 一致的决策载荷。
- `CodexUserInputLifecycle` 校验一至三个问题及其回答映射，并严格解析 Thread Token 用量通知。
- `CodexAdapter` 读取全部分页、发布完整 `CapabilitySet`，创建或恢复原生 Thread，并将一个 GUI Turn 串行关联到一个原生 Turn。

`model/list` 默认排除隐藏条目，并使用不透明 `nextCursor` 翻页；重复模型 ID 采用最后一条。缺少 `inputModalities` 时按官方兼容规则使用 `text` 与 `image`。未知的新推理强度 ID 保留在模型元数据中，但不映射为领域枚举。能力刷新后，如果当前模型或强度已失效，`SessionController` 会为下一轮切换到服务端声明的模型和强度默认值。

新会话调用 `thread/start`；持久化记录已有 `nativeThreadId` 时调用 `thread/resume`。Snack 在 SQLite Schema v3 中分别保存响应的 `thread.id` 和 `thread.sessionId`，绝不相互推导。恢复响应返回不同 Thread ID 时安全失败。访问映射为：严格 = `untrusted` + `read-only`，工作区 = `on-request` + `workspace-write`，完全 = `never` + `danger-full-access`。

app-server 进程断开时，活动 Turn 会先按失败收口，再把会话置为 `Disconnected`。标题栏随后提供显式“重新连接”，启动新的 app-server 进程并恢复已持久化的原生 Thread。如果旧进程仍在异步退出，新启动会带明确诊断被拒绝，会话回到 `Disconnected` 而不会永久停在 `Connecting`；进程结束后用户可以再次重试。优雅退出最长等待两秒，随后传输层会强制终止无响应进程，避免残留子进程永久阻止重连。重连绝不会重试被中断的 Turn，也不会发送恢复出来的排队消息；两者都需要新的用户操作。

主窗口把断线诊断保存在持久诊断条中，不依赖定时消失的状态栏消息。重连期间继续显示上一次失败原因，只有恢复的 app-server 连接报告成功后才清除。启动时回退 Mock 也使用同一诊断条，但只要该回退运行时仍绑定当前会话，说明就始终可见。

每轮 `turn/start` 发送文本输入和 GUI Turn UUID，并按该轮不可变快照覆盖 `model`、`effort`、`cwd`、`approvalPolicy` 与 `sandboxPolicy`。`turn/started`、`item/started`、`item/agentMessage/delta`、`item/completed`、`error` 与 `turn/completed` 被映射为统一领域事件。适配器同时校验原生 Thread/Turn ID、合并最终消息缺失的尾部文本、忽略重复终态，并保证 `turnFinished` 只发送一次。用户可以在原生 Turn ID 返回前取消；请求会延迟到 ID 可用时发送。`turn/interrupt` 的响应不代表终态，仍由 `turn/completed` 的 `interrupted` 状态收口。

`turn/steer` 可以向同一活动 Turn 补充一条文本指令。零食把原生 ID 作为 `expectedTurnId`，为消息生成独立客户端 ID，并且每个会话同一时刻只允许一个 steer 请求等待响应。原生 Turn ID 返回前提交的 steer 只暂存在内存中，并在 `turn/start` 确认 Turn 仍在运行后立即发送；若启动结果已经终结则直接丢弃。拒绝或 Turn ID 不匹配只产生警告，不会结束活动 Turn，也不会重发。公共 Session API 把成功受理的 steer 持久化为用户消息，并携带原 Turn 的设置快照。Composer 在运行中分别显示“引导”和“停止”；审批或回答获得焦点时退化为只能排队。

M3 Composer 把有序、可编辑的消息队列持久化到 SQLite。当前进程中的 Turn 正常完成后会发送下一条；中断或应用重启会让所有排队消息继续保持待发送。恢复后的消息必须由用户“立即发送”，或等本进程中新 Turn 正常完成后才会继续，因此恢复流程不会静默重放任务。

`thread/list` 限定当前工作目录与 app-server 来源，按更新时间排序并使用不透明的前向游标；`thread/read` 可为显式选择的原生 ID 请求完整 Turn。两条结果路径都会先校验 Thread、Session 与 cwd 身份，再通过类型化异步 Adapter 信号发布。非法分页或协议错误独立报告，绝不替换当前会话绑定的原生身份。

命令、文件变更、MCP、动态工具、协作、搜索、图片查看与上下文压缩 Item 映射为统一且可持久化的工具生命周期。命令输出与 MCP 进度持续写入同一张时间线卡片；文件 Patch 更新刷新变更文件摘要。最终 `item/completed` 对状态、输出、结果、错误、退出码和耗时具有权威性。历史卡片可由事件日志恢复，界面显示的工具输出最多保留最近 64 KiB。

当前切片只展示协议提供的推理摘要。`summaryTextDelta` 流入推理卡片，最终 summary 数组覆盖草稿；`reasoning/textDelta` 不会作为可见推理渲染或持久化。Plan Item Delta 可填充行内计划文本，完成 Item 覆盖草稿；`turn/plan/updated` 驱动可停靠任务列表，并显示 `pending`、`inProgress` 与 `completed` 三态。

命令与文件审批以 app-server 主动发起的 `item/commandExecution/requestApproval` 和 `item/fileChange/requestApproval` 请求到达。适配器校验原生 Thread、Turn、Item 和 JSON-RPC 请求身份后，才产生持久化的 `ApprovalRequested` 事件。界面展示命令、cwd、原因、文件授权根目录或网络 host/protocol，并提供 `accept`、`acceptForSession`、`decline` 与 `cancel`。成功响应会记录 `ApprovalResolved`；`serverRequest/resolved`、Turn 结束、中断与断连也会清理等待状态。恢复出来但未回答的历史卡片保持禁用，因为其来源 app-server 进程已经不存在。并发请求分别寻址，重复原生请求 ID 不会产生重复卡片或重复响应。

`tool/requestUserInput` 会转为统一问题卡片，支持选项、Other、自由文本和密码输入。阻塞请求使会话进入 `WaitingInput`，非阻塞请求保持运行；可见状态优先级依次为阻塞提问、审批、运行。回答只返回原生请求来源；持久化的解决事件仅含请求身份和结果，绝不包含回答值，密码控件提交后立即清空。恢复出来的未回答卡片标记过期并只读。

`thread/tokenUsage/updated` 提供的 `last`、`total` 与 `modelContextWindow` 会映射为 `UsageUpdated`。标题栏展示总 Token/上下文占用，提示中细分输入、缓存、输出与推理；上下文窗口缺失时隐藏比例。零食不会估算费用，也不会修正服务端 Token 算术。

Windows 优先探测 `codex.cmd`，并通过 `cmd.exe /c call` 启动 npm 包装器；Linux 和 macOS 直接启动 `codex`。这条 Windows 路径已通过真实本机握手测试。

探测会先解析 CLI SemVer，再检查 `app-server`。低于 `0.149.0` 的版本以及 `0.149.0` 的预发布版会被拒绝，GUI 回退诊断保留检测版本和最低要求。更高版本不会仅凭版本号放行：仍须暴露预期的 app-server 命令、完成初始化、返回合法模型目录，并通过严格的运行时响应校验。由此允许前向兼容的新增字段，同时在必需字段缺失时安全失败。

`AgentRuntimeFactory` 根据应用偏好构造不可变的会话适配器及其进程传输。Agent 菜单只设置“下一会话”偏好，不替换当前适配器；启动恢复也仅接受 Agent 类型和工作目录都相同的记录，因此 Codex、Claude 与 Mock 的原生身份不会混入同一会话。Codex 能力目录返回后，主窗口按模型元数据重建模型与推理强度控件，访问层级也只显示适配器声明的选项。运行中修改这些控件只影响下一轮。

## 失效与资源边界

- 初始化默认 5 秒超时；进程提前退出、写失败或非法 JSON 都进入 `Failed`。
- 初始化后的每个请求都有 15 秒响应上限。模型目录和 Thread 生命周期请求超时会让连接失败；`turn/start` 只让当前 GUI Turn 失败；历史查询、steer 与 interrupt 只报告局部失败并释放请求槽位。成功、协议错误、关闭、进程失败和重启都会取消对应计时器。超时后的迟到响应按未知 ID 隔离，不能满足后续请求。
- JSONL payload 无论分隔符是否已经到达都以 4 MiB 为上限。分隔符中的 LF 与可选 CR 始终不计入长度，包括 CRLF 跨读取分片的情况；恰好达到上限的 payload 可以通过，再多一个 payload 字节则安全失败。
- stderr 诊断只保留最近 64 KiB，同时继续发出增量诊断信号。
- 未知通知不会让连接失败。未支持的 server request 会收到明确的 JSON-RPC `-32601`；格式错误的审批请求收到 `-32602`。两条路径都会产生警告，避免 app-server 请求静默悬挂。
- 未知或重复响应 ID 只产生协议警告，不错误关联到其他请求。
- 活跃 Turn 期间进程异常退出会产生一次 `TurnFailed` 和一次 `turnFinished`，避免会话永久停留在 Running。
- 连接启动是带确认结果的操作。已有活动连接或传输层仍在退出时，启动会被拒绝且不会重置协议状态；适配器把该拒绝转换为局部连接失败。
- 停止与失败路径都会先关闭标准输入并请求进程退出。单次两秒计时器会强制终止仍存活的进程；正常退出会取消计时器，失败连接则保留原始诊断，同时恢复为可再次启动。

## 契约测试

`tests/fixtures/codex/app-server/0.149.0` 保存脱敏握手、模型、Thread 与 Turn fixture、manifest 和由以下命令生成的相关 Schema 子集：

```text
codex app-server generate-json-schema --out <directory>
```

普通 CI 只运行假传输与 fixture，不依赖已安装 CLI，也不调用模型。测试覆盖分页、Thread list/read 解析、创建/恢复身份、动态逐轮设置、文本与工具流、同 Turn steer 成功/失败/ID 不匹配、最终 Item 权威覆盖、命令非零退出、文件/MCP 结果、推理摘要隐私、计划三态、有界历史输出、审批决策、过期与重复事件、完整/未完成帧上限、分片 CRLF 边界、请求/通知错误与超时、计时器清理、迟到响应隔离、旧进程退出期间的重连拒绝与恢复、正常退出计时器取消、进程强制终止、中断竞态、未支持 server request 与进程断开。本机可选择执行 CLI 探测、初始化、模型目录与临时 Thread 创建的烟雾测试，全程不调用模型；真实 `turn/start` 必须由开发者另行明确启用，避免测试意外产生模型调用：

```powershell
$env:SNACK_RUN_LIVE_CODEX_TEST = '1'
ctest --test-dir build/windows-llvm-mingw-debug -R snack_codex_tests --output-on-failure
```

升级支持的 Codex 版本时，必须重新生成 Schema、脱敏 fixture、更新 manifest，并运行旧版与新版契约测试。
