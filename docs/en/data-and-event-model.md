# Data and event model

## 1. Identity and time

GUI entities use random UUIDs. Native agent IDs are separate values, never database primary keys. Store UTC milliseconds and render local time. Deduplicate workspaces by canonical path while retaining the user's display path.

## 2. Core entities

| Entity | Important fields |
|---|---|
| `Workspace` | `id`, `canonicalPath`, `displayPath`, `gitState`, `lastOpenedAt` |
| `Conversation` | `id`, `workspaceId`, `agentKind`, `modelId`, `title`, `titleIsPlaceholder`, `nativeSessionId`, `status`, `archived`, `pinned`, `tags` |
| `AgentRuntime` | `conversationId`, `cliPath`, `cliVersion`, `protocolVersion`, `capabilities` |
| `Turn` | `id`, `nativeTurnId`, `conversationId`, `status`, `settingsSnapshot`, timestamps |
| `AgentEvent` | `id`, `conversationId`, `turnId`, `sequence`, `type`, `payload`, `rawRef` |
| `QueuedMessage` | `id`, `conversationId`, `content`, `attachments`, `position`, `state` |
| `SavedConversationView` | `id`, `name`, `query`, `showArchived`, `position` |
| `PermissionRule` | `id`, `scope`, `operation`, `matcher`, `decision`, `secretRef` |
| `DiffSet` | `id`, `conversationId`, `baselineId`, `source`, `reviewState` |
| `FileSnapshot` | `contentHash`, `path`, `metadata`, `createdAt`, `protectedReason` |
| `TerminalTab` | `id`, `conversationId`, `name`, `cwd`, `shell`, `windowState` |

The M3 conversation catalog reads complete conversation metadata through the repository boundary. Results are deterministic: active conversations precede archived conversations, pinned entries lead within each section, and titles then sort case-insensitively with UUID as the final tie-breaker. This query does not open, resume, or mutate an Agent session.

## 3. Unified events

Event types include user and agent message lifecycle, reasoning lifecycle, turn lifecycle, plans, tools, commands, file changes and diffs, approvals, user prompts, usage, capability/connection changes, warnings, errors, and `RawProtocolObserved`.

Sequence numbers increase strictly within one conversation. A mapping failure stores the raw protocol event and a parse warning rather than dropping content. A valid but unknown notification or future item type within the active native Thread/Turn is stored as `RawProtocolObserved`; contextual identity is checked first so a stale native turn cannot contaminate the current GUI turn. Explicitly private protocol content, such as Codex raw reasoning text, remains excluded rather than entering this fallback.

A Codex text turn keeps the GUI `QUuid` as its domain `turnId` and stores the app-server turn ID as `nativeTurnId` in event payloads; neither substitutes for the other as a database key. `item/agentMessage/delta` text becomes `AgentMessageDelta.payload.text`, while the original JSON-RPC notification remains in `rawPayload`. Terminal status is accepted once and maps `completed`, `interrupted`, and `failed` to their corresponding domain events.

## 4. Settings snapshots

Every turn stores an immutable `TurnSettingsSnapshot`: agent, model ID, effort, access level, mapped sandbox/approval settings, cwd, isolated-change state, and protocol capability version. UI edits during a turn update only `nextTurnSettings`. The conversation catalog separately persists the normalized next-turn model ID so closed and restored conversations remain filterable without reconstructing settings from historical events.

`CapabilitySet` carries the visible model IDs plus per-model display metadata, default effort, supported effort IDs/descriptions, input modalities, personality support, and the server-selected default model. Raw future effort IDs remain available for display even when the current domain enum cannot execute them.

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

The store currently uses schema version 11. Conversations persist `native_thread_id` and `native_session_id` separately so Codex resume never reconstructs one identity from the other. The non-null `conversations.model_id` column stores the normalized next-turn model selection; legacy rows migrate to an empty value and adopt the runtime default when next opened. M3 conversation tags are stored as a JSON string array in the non-null `conversations.tags` column; legacy rows migrate to an empty array. The normalized optional conversation group lives in `conversations.group_name` and legacy rows remain ungrouped. `conversations.created_at` records creation time and `last_activity_at` advances atomically with each persisted event without regressing for an older event timestamp. Existing rows derive both values from their event history or the migration time when no events exist. Saved conversation filters live in `saved_views` with a case-insensitively unique name, the original query, archived-row visibility, and deterministic position; applying one never requires an Agent runtime. Pending composer messages live in the ordered `queued_messages` table and survive edits, reordering, cancellation, and restarts. Favorite prompt templates live in `prompt_templates` as ordered plain text; parameter substitution is performed in memory and never executes code. Before any pending upgrade of an existing file, `EventStore` creates a consistent SQLite snapshot beside the database with a `.pre-migration-v<version>-<timestamp>-<id>.bak` suffix. All pending steps execute in one transaction. A statement or commit failure rolls the transaction back and reopens the original database read-only; write APIs then fail closed. Databases created by a newer Snack version follow the same read-only path without being modified. Successful safety backups are retained for explicit user recovery and later maintenance policy.

Unsent composer drafts are private UI state rather than domain events. `AppSettings` stores one exact text value per conversation UUID and removes the key after a successful send or explicit clear; loading a draft never triggers a turn or queue operation.
