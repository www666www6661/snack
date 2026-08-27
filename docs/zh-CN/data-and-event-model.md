# 数据与事件模型

## 1. 标识与时间

所有 GUI 实体使用随机 UUID。原生 Agent ID单独存储，不能充当数据库主键。时间使用 UTC 毫秒存储，展示时转换为本地时区。工作区以 canonical path key去重，保留 display path。

## 2. 核心实体

| 实体 | 关键字段 |
|---|---|
| `Workspace` | `id`, `canonicalPath`, `displayPath`, `gitState`, `lastOpenedAt` |
| `Conversation` | `id`, `workspaceId`, `agentKind`, `title`, `titleIsPlaceholder`, `nativeSessionId`, `status`, `archived`, `pinned` |
| `AgentRuntime` | `conversationId`, `cliPath`, `cliVersion`, `protocolVersion`, `capabilities` |
| `Turn` | `id`, `nativeTurnId`, `conversationId`, `status`, `settingsSnapshot`, `startedAt`, `finishedAt` |
| `AgentEvent` | `id`, `conversationId`, `turnId`, `sequence`, `type`, `payload`, `rawRef` |
| `QueuedMessage` | `id`, `conversationId`, `content`, `attachments`, `position`, `state` |
| `PermissionRule` | `id`, `scope`, `operation`, `matcher`, `decision`, `secretRef` |
| `DiffSet` | `id`, `conversationId`, `baselineId`, `source`, `reviewState` |
| `FileSnapshot` | `contentHash`, `path`, `metadata`, `createdAt`, `protectedReason` |
| `TerminalTab` | `id`, `conversationId`, `name`, `cwd`, `shell`, `windowState` |

M3 会话目录通过 Repository 边界读取完整会话元数据。结果顺序确定：活动会话先于已归档会话，每个区段内置顶项优先，随后按标题不区分大小写排序，最后用 UUID 打破同名顺序。该查询不会打开、恢复或修改 Agent 会话。

## 3. 统一事件

`AgentEvent.type` 至少包括：

- `UserMessage`, `AgentMessageStart`, `AgentMessageDelta`, `AgentMessageComplete`
- `ReasoningStart`, `ReasoningDelta`, `ReasoningComplete`
- `TurnStarted`, `TurnCompleted`, `TurnInterrupted`, `TurnFailed`
- `PlanUpdated`, `PlanStepUpdated`
- `ToolStarted`, `ToolProgress`, `ToolCompleted`, `ToolFailed`
- `CommandStarted`, `CommandOutput`, `CommandCompleted`
- `FileChangeProposed`, `FileChanged`, `DiffUpdated`
- `ApprovalRequested`, `ApprovalResolved`
- `UserPromptRequested`, `UserPromptResolved`
- `UsageUpdated`, `CapabilityChanged`, `ConnectionChanged`
- `WarningRaised`, `ErrorRaised`, `RawProtocolObserved`

事件序号在单会话内严格递增。原生事件映射失败时写入 `RawProtocolObserved` 与解析警告，不丢弃原始内容。活动原生 Thread/Turn 内合法但未知的通知或未来 Item 类型也保存为 `RawProtocolObserved`；保存前先校验上下文身份，避免过期原生 Turn 污染当前 GUI Turn。明确属于隐私边界的协议内容（例如 Codex 原始 reasoning text）不会进入该降级路径。

Codex 文本 Turn 使用 GUI `QUuid` 作为领域 `turnId`，并把 app-server 的 Turn ID 保存到事件 payload 的 `nativeTurnId`，两者不互作数据库主键。`item/agentMessage/delta` 的文本写入 `AgentMessageDelta.payload.text`；原始 JSON-RPC 通知保存在 `rawPayload`。终态只接受一次，`completed`、`interrupted`、`failed` 分别映射到对应领域事件。

## 4. 设置快照

每个 Turn 保存不可变的 `TurnSettingsSnapshot`：Agent、模型 ID、推理强度、访问层级、沙箱/审批映射、工作目录、隔离修改状态和协议能力版本。运行中 UI设置只更新 `nextTurnSettings`。

`CapabilitySet` 同时保存可见模型 ID 和逐模型展示信息、默认强度、支持的强度 ID/说明、输入模态、人格支持及服务端默认模型。领域枚举暂不认识的新强度 ID 仍保留用于展示，但不会被误当作可执行设置。

## 5. SQLite 逻辑表

建议初始表：

`schema_migrations`, `workspaces`, `conversations`, `conversation_tags`, `groups`, `saved_views`, `turns`, `events`, `queued_messages`, `attachments`, `permission_rules`, `diff_sets`, `diff_files`, `snapshots`, `terminal_tabs`, `layouts`, `themes`, `prompt_templates`, `maintenance_runs`。

事件 payload 使用带 Schema 版本的 JSON；高频可查询字段提升为列。外键启用，写入使用事务与 WAL。数据库不存储大型文件内容。

## 6. 内容寻址存储

目录按哈希前缀分片，Blob 清单保存大小、压缩、MIME、引用计数和完整性哈希。写入流程为临时文件、校验、原子重命名、数据库引用提交。未引用 Blob 只能由维护任务清理。

## 7. Diff 与归属

`DiffSource` 为 `nativeIsolated`、`agentDirect` 或 `external`。Agent 事件与文件监听共同确定归属；冲突时标记 `ambiguous`，禁止自动拒绝。每个 hunk 具有稳定哈希，接受后建立新基线，拒绝前验证前置内容。

## 8. 保留与备份

原始日志和已审查快照默认 30 天；总容量 10 GB。未审查、冲突恢复、活动会话和备份事务数据带 `protectedReason`，不自动删除。备份清单记录应用版本、Schema 版本、平台、路径映射和每个文件哈希。

## 9. 迁移规则

迁移只向前执行，每步幂等并记录开始/完成。升级前自动备份数据库。迁移失败进入只读恢复模式；不得用空数据库覆盖旧数据。

当前数据库 Schema 版本为 5。会话分别持久化 `native_thread_id` 与 `native_session_id`，Codex 恢复不会用一个身份推导另一个。Composer 待发送内容存放在有序的 `queued_messages` 表中，编辑、排序、取消与重启都不会丢失。收藏提示词模板作为有序纯文本存放在 `prompt_templates`；参数替换仅在内存中进行，绝不执行代码。现有数据库存在待执行升级时，`EventStore` 会先在数据库旁创建后缀为 `.pre-migration-v<版本>-<时间>-<标识>.bak` 的 SQLite 一致性快照。全部待执行步骤位于同一事务中；任一语句或提交失败都会回滚事务，并以只读方式重新打开原数据库，所有写 API 随后安全失败。由更高版本零食创建的数据库也会在不修改文件的前提下进入只读模式。成功升级产生的安全备份会保留，供用户明确恢复，后续再纳入维护清理策略。

尚未发送的 Composer 草稿属于私有 UI 状态，不是领域事件。`AppSettings` 按会话 UUID 保存一份原样文本，成功发送或明确清空后移除对应键；加载草稿绝不会触发 Turn 或排队操作。
