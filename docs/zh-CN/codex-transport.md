# Codex App Server 传输

## 当前状态

M2 前两段已在 Codex CLI `0.149.0` 上完成验证。当前代码已经建立独立的 CLI 探测、子进程传输、JSONL 协议解析、初始化握手、分页 `model/list` 和连接态 `CodexAdapter`。适配器会向 `SessionController` 发布逐模型能力；原生 Thread/Turn 尚未实现，主应用仍使用模拟适配器启动。

官方协议依据：[OpenAI Docs - Codex App Server](https://developers.openai.com/codex/app-server)。默认传输是 stdio 上逐行 JSON；线上消息省略 `jsonrpc: "2.0"`。每个连接必须先发送 `initialize` 请求，收到成功响应后再发送 `initialized` 通知。

## 模块边界

- `IProcessTransport` 隔离进程生命周期和字节流，协议测试使用假实现。
- `QProcessTransport` 封装异步 `QProcess`、独立 stdout/stderr 和退出错误。
- `CodexCliDiscovery` 查找 CLI，验证版本与 `app-server --help`，并生成平台启动参数。
- `CodexProtocol` 分类请求、响应、通知和非法消息，保留完整原始对象。
- `CodexAppServerClient` 负责 JSONL 分帧、初始化、请求 ID 关联、通知和 server request 转发。
- `CodexModelCatalog` 校验模型分页，并保留模型的推理强度说明、输入模态、人格支持和默认标记。
- `CodexAdapter` 读取全部分页后才发布完整 `CapabilitySet` 并报告连接就绪。

`model/list` 默认排除隐藏条目，并使用不透明 `nextCursor` 翻页；重复模型 ID 采用最后一条。缺少 `inputModalities` 时按官方兼容规则使用 `text` 与 `image`。未知的新推理强度 ID 保留在模型元数据中，但不映射为领域枚举。能力刷新后，如果当前模型或强度已失效，`SessionController` 会为下一轮切换到服务端声明的模型和强度默认值。

Windows 优先探测 `codex.cmd`，并通过 `cmd.exe /c call` 启动 npm 包装器；Linux 和 macOS 直接启动 `codex`。这条 Windows 路径已通过真实本机握手测试。

## 失效与资源边界

- 初始化默认 5 秒超时；进程提前退出、写失败或非法 JSON 都进入 `Failed`。
- 单个 JSONL 帧上限 4 MiB，防止无换行输出无限增长。
- stderr 诊断只保留最近 64 KiB，同时继续发出增量诊断信号。
- 未知通知和 server request 不会让连接失败，原始对象原样向上层提供。
- 未知或重复响应 ID 只产生协议警告，不错误关联到其他请求。

## 契约测试

`tests/fixtures/codex/app-server/0.149.0` 保存脱敏握手与分页模型 fixture、manifest 和由以下命令生成的相关 Schema 子集：

```text
codex app-server generate-json-schema --out <directory>
```

普通 CI 只运行假传输与 fixture，不依赖已安装 CLI，也不调用模型。测试覆盖分页、隐藏/默认模型、兼容默认值、请求错误、空目录和未支持 Turn 的安全结束。本机可选择执行仅包含 CLI 探测和初始化的烟雾测试：

```powershell
$env:SNACK_RUN_LIVE_CODEX_TEST = '1'
ctest --test-dir build/windows-llvm-mingw-debug -R snack_codex_tests --output-on-failure
```

升级支持的 Codex 版本时，必须重新生成 Schema、脱敏 fixture、更新 manifest，并运行旧版与新版契约测试。
