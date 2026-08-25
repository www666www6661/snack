# Data and event model

## 1. Identity and time

GUI entities use random UUIDs. Native agent IDs are separate values, never database primary keys. Store UTC milliseconds and render local time. Deduplicate workspaces by canonical path while retaining the user's display path.

## 2. Core entities

| Entity | Important fields |
|---|---|
| `Workspace` | `id`, `canonicalPath`, `displayPath`, `gitState`, `lastOpenedAt` |
| `Conversation` | `id`, `workspaceId`, `agentKind`, `title`, `nativeSessionId`, `status`, `archived`, `pinned` |
| `AgentRuntime` | `conversationId`, `cliPath`, `cliVersion`, `protocolVersion`, `capabilities` |
| `Turn` | `id`, `nativeTurnId`, `conversationId`, `status`, `settingsSnapshot`, timestamps |
| `AgentEvent` | `id`, `conversationId`, `turnId`, `sequence`, `type`, `payload`, `rawRef` |
| `QueuedMessage` | `id`, `conversationId`, `content`, `attachments`, `position`, `state` |
| `PermissionRule` | `id`, `scope`, `operation`, `matcher`, `decision`, `secretRef` |
| `DiffSet` | `id`, `conversationId`, `baselineId`, `source`, `reviewState` |
| `FileSnapshot` | `contentHash`, `path`, `metadata`, `createdAt`, `protectedReason` |
| `TerminalTab` | `id`, `conversationId`, `name`, `cwd`, `shell`, `windowState` |

## 3. Unified events

Event types include user and agent message lifecycle, reasoning lifecycle, turn lifecycle, plans, tools, commands, file changes and diffs, approvals, user prompts, usage, capability/connection changes, warnings, errors, and `RawProtocolObserved`.

Sequence numbers increase strictly within one conversation. A mapping failure stores the raw protocol event and a parse warning rather than dropping content.

## 4. Settings snapshots

Every turn stores an immutable `TurnSettingsSnapshot`: agent, model ID, effort, access level, mapped sandbox/approval settings, cwd, isolated-change state, and protocol capability version. UI edits during a turn update only `nextTurnSettings`.

## 5. SQLite model

Initial logical tables: `schema_migrations`, `workspaces`, `conversations`, `conversation_tags`, `groups`, `saved_views`, `turns`, `events`, `queued_messages`, `attachments`, `permission_rules`, `diff_sets`, `diff_files`, `snapshots`, `terminal_tabs`, `layouts`, `themes`, `prompt_templates`, and `maintenance_runs`.

Event payloads are versioned JSON with query-critical fields promoted to columns. Enable foreign keys, transactions, and WAL. Large file contents never live in SQLite.

## 6. Content-addressed storage

Shard blobs by hash prefix and record size, compression, MIME type, reference count, and integrity hash. Write through a temporary file, verify, atomically rename, then commit the database reference. Maintenance removes only unreferenced blobs.

## 7. Diff attribution

`DiffSource` is `nativeIsolated`, `agentDirect`, or `external`. Combine agent events and file watching; mark uncertainty as `ambiguous` and disable automatic rejection. Hunks use stable hashes. Accepting creates a new baseline; rejecting validates preimage content before patching.

## 8. Retention and backup

Raw logs and reviewed snapshots default to 30 days with a 10 GB cap. Pending reviews, conflict recovery, active sessions, and restore transactions carry a `protectedReason` and are never auto-deleted. Backup manifests contain app/schema versions, platform, path mappings, and hashes.

## 9. Migrations

Migrations are forward-only, idempotent, and record start/completion. Back up the database before upgrading. Failure enters read-only recovery mode and never replaces old data with an empty database.
