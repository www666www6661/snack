# Codex App Server transport

## Current status

The M2 connection, thread, text-turn, approval, user-input, usage, and activity slices are fixture-validated against Codex CLI `0.149.0`. They establish CLI discovery, subprocess transport, JSONL parsing, initialization, paginated `model/list`, native `thread/start`/`thread/resume`, `turn/start`, streamed text, tool execution, reasoning summaries, plans, `turn/interrupt`, command/file approval responses, question responses, and token/context display. The main application probes and starts Codex by default, with an explicit Mock Agent fallback when the CLI is unavailable.

Protocol source: [OpenAI Docs - Codex App Server](https://developers.openai.com/codex/app-server). The default transport is newline-delimited JSON over stdio, with the `jsonrpc: "2.0"` member omitted on the wire. Every connection sends one `initialize` request and waits for its successful response before sending the `initialized` notification.

## Module boundaries

- `IProcessTransport` isolates process lifetime and byte streams so protocol tests can inject a fake.
- `QProcessTransport` wraps asynchronous `QProcess`, separate stdout/stderr, exits, and errors.
- `CodexCliDiscovery` finds the CLI, validates version and `app-server --help`, and builds platform launch arguments.
- `CodexProtocol` classifies requests, responses, notifications, and invalid messages while preserving each raw object.
- `CodexAppServerClient` owns JSONL framing, initialization, request ID correlation, notifications, and server-request forwarding.
- `CodexModelCatalog` validates model pages and preserves model-specific effort descriptions, modalities, personality support, and default markers.
- `CodexThreadLifecycle` validates `thread.id`, `thread.sessionId`, and cwd and defines the audited access-level mapping.
- `CodexTurnLifecycle` builds per-turn overrides and strictly parses turn, item, text-delta, and error notifications.
- `CodexApprovalLifecycle` validates command and file approval routing, preserves display metadata, and builds the versioned decision payload.
- `CodexUserInputLifecycle` validates one-to-three question requests and answer maps, and strictly parses thread token-usage notifications.
- `CodexAdapter` publishes the complete catalog, starts or resumes the native thread, and serially correlates one GUI turn with one native turn.

`model/list` is requested with hidden entries excluded. Pagination follows the opaque `nextCursor`; duplicate IDs are replaced by their latest entry. A missing `inputModalities` field uses the documented `text` and `image` compatibility default. Unknown future effort IDs stay in the model metadata but are not mapped to a domain enum. If the selected model or effort disappears after a capability refresh, `SessionController` switches to the advertised model and effort defaults for the next turn.

A new conversation calls `thread/start`; a conversation with a persisted `nativeThreadId` calls `thread/resume`. Snack stores the returned `thread.id` and `thread.sessionId` separately in SQLite schema v3 and never derives one from the other. A resume response with a different thread ID fails closed. Access maps as follows: Strict = `untrusted` + `read-only`, Workspace = `on-request` + `workspace-write`, Full = `never` + `danger-full-access`.

Each `turn/start` sends text input and the GUI turn UUID while overriding `model`, `effort`, `cwd`, `approvalPolicy`, and `sandboxPolicy` from its immutable settings snapshot. `turn/started`, `item/started`, `item/agentMessage/delta`, `item/completed`, `error`, and `turn/completed` map to domain events. The adapter validates native thread/turn IDs, appends any final text suffix missing from deltas, ignores duplicate terminal notifications, and emits `turnFinished` once. An interrupt requested before the native turn ID arrives is deferred until that ID is known. A successful `turn/interrupt` response is not terminal; the subsequent `turn/completed` with status `interrupted` closes the turn.

Once the native active-turn ID is known, `turn/steer` can add one text instruction to that same turn. Snack sends the ID as `expectedTurnId`, generates a distinct client message ID, and permits only one outstanding steer request per conversation. A rejected or mismatched steer produces a warning without terminating the active turn; it is never silently replayed. The shared Session API persists an accepted steer as a user message with the original turn settings snapshot. The M3 composer will provide the steer-now versus editable-queue choice; this M2 layer deliberately does not infer queue behavior.

`thread/list` is scoped to the current workspace and app-server source, ordered by update time, and uses opaque forward cursors. `thread/read` can request complete turns for an explicitly selected native ID. Both result paths validate thread, session, and cwd identities before publishing typed asynchronous adapter signals. Malformed pages and protocol errors are reported separately and never replace the currently bound conversation identity.

Command, file-change, MCP, dynamic, collaboration, search, image-view, and compaction items map to a common persisted tool lifecycle. Command output and MCP progress stream into one timeline card; file patch updates refresh its changed-file summary. The final `item/completed` object is authoritative for status, output, result, error, exit code, and duration. Restored cards reconstruct from the event log, and visible tool output is capped to the latest 64 KiB.

Reasoning items expose only the protocol-provided summary in this slice. `summaryTextDelta` streams into a reasoning card and the final summary array replaces the draft; `reasoning/textDelta` is deliberately not rendered or persisted as visible reasoning. Plan-item deltas can populate the inline plan text, while the completed plan item replaces the draft. `turn/plan/updated` drives the dockable task list with `pending`, `inProgress`, and `completed` states.

Command and file approvals arrive as server-initiated `item/commandExecution/requestApproval` and `item/fileChange/requestApproval` requests. The adapter verifies the native thread, turn, item, and JSON-RPC request identity before emitting a persisted `ApprovalRequested` event. The UI renders the command, cwd, reason, file grant root, or network host/protocol and offers `accept`, `acceptForSession`, `decline`, and `cancel`. A successful response records `ApprovalResolved`; `serverRequest/resolved`, turn completion, interruption, and disconnect also clear pending state. Restored unanswered cards are disabled because their originating app-server process no longer exists. Multiple concurrent requests remain independently addressable, and duplicate native request IDs never create duplicate cards or responses.

`tool/requestUserInput` becomes a common question card with option, Other, free-form, and password controls. A blocking request takes the session to `WaitingInput`; a non-blocking request leaves it running. Blocking input takes precedence over approval in the visible state, then approval, then running. Responses are returned only to the originating native request. Persisted resolution events contain request identity and outcome but never answer values, and password controls are cleared immediately after submission. Restored unanswered cards are expired and read-only.

`thread/tokenUsage/updated` maps supplied `last`, `total`, and `modelContextWindow` values into `UsageUpdated`. The header shows total/context consumption and an input/cache/output/reasoning tooltip; a missing context window hides the ratio. Snack neither infers costs nor repairs token arithmetic.

Windows discovery prefers `codex.cmd` and starts npm wrappers through `cmd.exe /c call`; Linux and macOS start `codex` directly. The Windows path is verified with a live local handshake.

`AgentRuntimeFactory` creates an immutable per-conversation adapter and owns its process transport. The Agent menu changes only the next-conversation preference; it never swaps the current adapter. Startup restores a stored conversation only when both its Agent kind and workspace match, preventing Codex, Claude, and Mock identities from sharing a conversation. Once the Codex catalog arrives, the main window rebuilds model and effort controls from model metadata and shows only advertised access levels. Changes made during a running turn apply to the next turn.

## Failure and resource boundaries

- Initialization times out after five seconds by default. Early exit, write failure, and invalid JSON enter `Failed`.
- A JSONL frame is limited to 4 MiB so missing newlines cannot grow memory without bound.
- Only the latest 64 KiB of stderr diagnostics is retained, while incremental diagnostic signals still stream.
- Unknown notifications remain forward-compatible. Unsupported server requests receive an explicit JSON-RPC `-32601` response; malformed approval requests receive `-32602`. Both paths produce a warning so the app-server request cannot hang silently.
- Unknown or duplicate response IDs emit warnings and are never associated with another request.
- An unexpected process exit during an active turn emits exactly one `TurnFailed` and one `turnFinished`, preventing a permanently Running session.

## Contract tests

`tests/fixtures/codex/app-server/0.149.0` contains sanitized handshake, model, thread, and turn fixtures, a manifest, and the relevant schema subset generated by:

```text
codex app-server generate-json-schema --out <directory>
```

Normal CI uses only fake transports and fixtures, requires no installed CLI, and never invokes a model. It covers pagination, thread list/read parsing, start/resume identity, dynamic per-turn settings, text and tool streaming, same-turn steer success/failure/mismatch, final-item authority, non-zero command exits, file/MCP results, reasoning-summary privacy, plan states, bounded restored output, approval decisions, stale and duplicate events, request and notification failures, interrupt races, unsupported server requests, and process loss. A local smoke test can opt into CLI discovery, initialization, model discovery, and an ephemeral thread start without invoking a model. A real `turn/start` must remain separately and explicitly enabled so tests cannot accidentally consume a model call:

```powershell
$env:SNACK_RUN_LIVE_CODEX_TEST = '1'
ctest --test-dir build/windows-llvm-mingw-debug -R snack_codex_tests --output-on-failure
```

When adding a supported Codex version, regenerate the schemas, sanitize the fixtures, update the manifest, and run both old and new contract suites.
