# Claude protocol spike

Reviewed: 2026-08-28

## 1. Outcome

M5 freezes Claude Code `2.1.219` as the minimum and `2.1.245` as the locally observed reference.
Snack can build the M6 adapter in C++20/Qt 6 from the public bidirectional `stream-json` message
types, official CLI session flags, and a private-to-Snack MCP stdio permission bridge. It must not
copy the TypeScript SDK's unpublished control envelope.

This is a protocol decision, not a released Claude integration. The live model, effort, and
permission setters documented by Anthropic are SDK methods without an official C++ SDK. Snack
therefore provides explicit next-turn restart-and-resume behavior instead of claiming instant parity.

## 2. Evidence boundary

The local Windows 11 reference checks used Claude Code `2.1.245` and never sent a valid user prompt.
They covered version and option parsing, `--init-only`, stream EOF and malformed synthetic input,
resume/fork flag parsing, and a C++ MCP handshake. The MCP probe used `--bare`, inline
`--mcp-config`, and `--strict-mcp-config`; it observed only `initialize`,
`notifications/initialized`, and `tools/list`. No user Claude/MCP configuration was changed.

Versioned JSONL fixtures are synthetic and derived from official SDK types and examples. They cover
startup events before `system/init`, open capability sets, partial and complete assistant messages,
two result-delimited turns, queued UUIDs, image blocks, unknown fields, malformed records, and
cross-session rejection. They are not represented as captured model output.

No live model turn, tool call, permission request, question, interrupt, queue execution, or
resume/fork execution was run. Those checks consume model usage or depend on a live turn and remain
explicit opt-in acceptance work. Default tests never start Claude.

## 3. Frozen transport contract

- Launch a persistent `claude -p` process with bidirectional `stream-json`, verbose output, partial
  messages, and replayed user messages.
- Accept startup events before `system/init`; use `system/init.session_id` and capabilities as the
  session authority, while ignoring unknown capability names.
- Assign a UUID to every outbound user message. Correlate a `result` with `user_message_uuid` and use
  each result as one turn boundary.
- Hold editable queued messages in Snack until the active turn ends. This avoids depending on the
  unpublished control envelope for queue cancellation.
- Send images only as documented base64 image content blocks and enforce MIME/size policy before
  serialization.
- Resume only with the native session ID and `--resume`; use `--fork-session` only with resume or
  continue. Never reconstruct native context from the GUI transcript.
- Interrupt the active process through the documented graceful process path, then restart and resume
  if needed. Never automatically resend an interrupted user message.
- Parse interrupt receipts only when `interrupt_receipt_v1` is present, and honor `cancelled` only
  when `interrupt_cancel_queued_v1` is also present. This payload contract is frozen for diagnostics;
  M6 does not gain permission to invent a raw control envelope.

## 4. Permission bridge

Snack owns a per-session, process-local MCP stdio server and passes it through inline strict MCP
configuration. It does not register a user MCP server or edit Claude settings. Every prompt is keyed
by its request/tool-use identity, handled idempotently, scoped to the bound Agent session, and denied
on disconnect, malformed input, stale identity, or unknown policy.

Tool permission prompts and `AskUserQuestion` map to the common permission/question cards. Automatic
Claude decisions do not pass through the bridge, so the result's authoritative denial list and
ordinary tool events must still be handled. The bridge probe tool always denies accidental calls.

## 5. Runtime-control degradation

| GUI control | Official live surface | Pure C++ M6 behavior |
|---|---|---|
| Model | TypeScript `Query.setModel()` / `applyFlagSettings()` | Show current and explicitly configured values; a change during a turn is labeled for the next turn, then restart and resume with `--model` |
| Effort | TypeScript `applyFlagSettings({effortLevel})` | Strictly allow `low`, `medium`, `high`, `xhigh`, or `max`; queue the change, then restart and resume with `--effort` |
| Permission mode | TypeScript `setPermissionMode()` / `applyFlagSettings()` | Queue CLI mode changes, then restart and resume with `--permission-mode`; a currently pending GUI bridge prompt uses the latest GUI policy immediately |

The Agent family remains immutable. None of these controls can turn a Codex session into Claude or a
Claude session into Codex. Unknown effort is rejected in Snack because Claude 2.1.245 only warns and
silently uses the default; an unknown model is not validated before a model turn.

## 6. Minimum and degradation matrix

| Gate | Documented version | Snack decision |
|---|---:|---|
| Permission request ID/null callback behavior | `2.1.199` | Required by the bridge identity model |
| `capabilities` and `interrupt_receipt_v1` | `2.1.205` | Capability detection is mandatory |
| Result `user_message_uuid` | `2.1.216` | Required for deterministic turn correlation |
| `interrupt_cancel_queued_v1` | `2.1.219` | Completes the frozen queue receipt contract and sets the baseline |

Exact-version prereleases below the stable `2.1.219` baseline do not qualify. Older versions receive
an upgrade message. Newer versions still must pass command parsing and strict initialization; unknown
messages remain raw diagnostics and never enable a feature without its capability.

## 7. M6 handoff

M6 may port the disposable parser and reconciliation rules into the Claude adapter, but must preserve
the common `IAgentAdapter` contract and keep Claude branches out of shared UI. It needs Fake Process
tests for framing, init ordering, turn correlation, process loss, restart/resume, MCP request identity,
pending-request cleanup, malformed terminal records, and every degradation above before any opt-in
live check is considered.
