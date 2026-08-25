# 数据与事件模型

## 1. 标识与时间

所有 GUI 实体使用随机 UUID。原生 Agent ID单独存储，不能充当数据库主键。时间使用 UTC 毫秒存储，展示时转换为本地时区。工作区以 canonical path key去重，保留 display path。

## 2. 核心实体

| 实体 | 关键字段 |
|---|---|
| `Workspace` | `id`, `canonicalPath`, `displayPath`, `gitState`, `lastOpenedAt` |
| `Conversation` | `id`, `workspaceId`, `agentKind`, `title`, `nativeSessionId`, `status`, `archived`, `pinned` |
| `AgentRuntime` | `conversationId`, `cliPath`, `cliVersion`, `protocolVersion`, `capabilities` |
| `Turn` | `id`, `nativeTurnId`, `conversationId`, `status`, `settingsSnapshot`, `startedAt`, `finishedAt` |
| `AgentEvent` | `id`, `conversationId`, `turnId`, `sequence`, `type`, `payload`, `rawRef` |
| `QueuedMessage` | `id`, `conversationId`, `content`, `attachments`, `position`, `state` |
| `PermissionRule` | `id`, `scope`, `operation`, `matcher`, `decision`, `secretRef` |
| `DiffSet` | `id`, `conversationId`, `baselineId`, `source`, `reviewState` |
| `FileSnapshot` | `contentHash`, `path`, `metadata`, `createdAt`, `protectedReason` |
| `TerminalTab` | `id`, `conversationId`, `name`, `cwd`, `shell`, `windowState` |

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

事件序号在单会话内严格递增。原生事件映射失败时写入 `RawProtocolObserved` 与解析警告，不丢弃原始内容。

## 4. 设置快照

每个 Turn 保存不可变的 `TurnSettingsSnapshot`：Agent、模型 ID、推理强度、访问层级、沙箱/审批映射、工作目录、隔离修改状态和协议能力版本。运行中 UI设置只更新 `nextTurnSettings`。

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
