# Codex App Server 传输

## 当前状态

M2 的连接、Thread 与文本 Turn 垂直链路已在 Codex CLI `0.149.0` 上完成 fixture 验证。当前代码已经建立独立的 CLI 探测、子进程传输、JSONL 协议解析、初始化握手、分页 `model/list`、原生 `thread/start`/`thread/resume`、`turn/start`、文本流映射和 `turn/interrupt`。`CodexAdapter` 发布模型能力后，只有服务端返回有效原生 Thread 身份才进入就绪；主应用仍使用模拟适配器启动，审批、工具、推理与计划事件也尚未接入产品 UI。

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
- `CodexAdapter` 读取全部分页、发布完整 `CapabilitySet`，创建或恢复原生 Thread，并将一个 GUI Turn 串行关联到一个原生 Turn。

`model/list` 默认排除隐藏条目，并使用不透明 `nextCursor` 翻页；重复模型 ID 采用最后一条。缺少 `inputModalities` 时按官方兼容规则使用 `text` 与 `image`。未知的新推理强度 ID 保留在模型元数据中，但不映射为领域枚举。能力刷新后，如果当前模型或强度已失效，`SessionController` 会为下一轮切换到服务端声明的模型和强度默认值。

新会话调用 `thread/start`；持久化记录已有 `nativeThreadId` 时调用 `thread/resume`。Snack 在 SQLite Schema v3 中分别保存响应的 `thread.id` 和 `thread.sessionId`，绝不相互推导。恢复响应返回不同 Thread ID 时安全失败。访问映射为：严格 = `untrusted` + `read-only`，工作区 = `on-request` + `workspace-write`，完全 = `never` + `danger-full-access`。

每轮 `turn/start` 发送文本输入和 GUI Turn UUID，并按该轮不可变快照覆盖 `model`、`effort`、`cwd`、`approvalPolicy` 与 `sandboxPolicy`。`turn/started`、`item/started`、`item/agentMessage/delta`、`item/completed`、`error` 与 `turn/completed` 被映射为统一领域事件。适配器同时校验原生 Thread/Turn ID、合并最终消息缺失的尾部文本、忽略重复终态，并保证 `turnFinished` 只发送一次。用户可以在原生 Turn ID 返回前取消；请求会延迟到 ID 可用时发送。`turn/interrupt` 的响应不代表终态，仍由 `turn/completed` 的 `interrupted` 状态收口。

Windows 优先探测 `codex.cmd`，并通过 `cmd.exe /c call` 启动 npm 包装器；Linux 和 macOS 直接启动 `codex`。这条 Windows 路径已通过真实本机握手测试。

## 失效与资源边界

- 初始化默认 5 秒超时；进程提前退出、写失败或非法 JSON 都进入 `Failed`。
- 单个 JSONL 帧上限 4 MiB，防止无换行输出无限增长。
- stderr 诊断只保留最近 64 KiB，同时继续发出增量诊断信号。
- 未知通知不会让连接失败。当前未实现的审批等 server request 会收到明确的 JSON-RPC `-32601` 错误并产生警告事件，避免 app-server 请求悬挂。
- 未知或重复响应 ID 只产生协议警告，不错误关联到其他请求。
- 活跃 Turn 期间进程异常退出会产生一次 `TurnFailed` 和一次 `turnFinished`，避免会话永久停留在 Running。

## 契约测试

`tests/fixtures/codex/app-server/0.149.0` 保存脱敏握手、模型、Thread 与 Turn fixture、manifest 和由以下命令生成的相关 Schema 子集：

```text
codex app-server generate-json-schema --out <directory>
```

普通 CI 只运行假传输与 fixture，不依赖已安装 CLI，也不调用模型。测试覆盖分页、创建/恢复身份、动态逐轮设置、正常文本流、分片与最终文本合并、请求/通知错误、错误 Thread/Turn、重复完成、中断竞态、server request 降级与进程断开。本机可选择执行 CLI 探测、初始化、模型目录与临时 Thread 创建的烟雾测试，全程不调用模型；真实 `turn/start` 必须由开发者另行明确启用，避免测试意外产生模型调用：

```powershell
$env:SNACK_RUN_LIVE_CODEX_TEST = '1'
ctest --test-dir build/windows-llvm-mingw-debug -R snack_codex_tests --output-on-failure
```

升级支持的 Codex 版本时，必须重新生成 Schema、脱敏 fixture、更新 manifest，并运行旧版与新版契约测试。
