# Claude protocol spike evidence

This directory documents disposable M5 probes. It does not define the production Claude adapter or
grant undocumented wire messages compatibility status.

## Safe probe boundary

The `2.1.245` evidence manifest was recorded on 2026-08-28 with no user message and no model turn.
Every successful startup used `--init-only` or an immediate stream EOF. Streaming error probes used
only malformed synthetic envelopes. The process also used `--safe-mode`, an empty
`--strict-mcp-config`, and a one-second `MCP_TIMEOUT`; no user Claude or MCP configuration was
written. A deliberately unknown option was the negative parser control.

The local commands established these facts:

- `--init-only` exits successfully without output.
- `--permission-prompt-tool` is accepted by Claude Code 2.1.245 even though this installed build
  omits it from top-level help.
- Invalid JSON and a user envelope without a message fail before a model turn.
- An empty object and stream EOF exit successfully without output.
- `--resume` and `--resume` plus `--fork-session` are accepted on the `--init-only` path.

Only normalized categories are committed in
`tests/fixtures/claude/2.1.245/manifest.json`. Raw output, executable paths, authentication state,
tokens, user configuration, repository names, and session identifiers are excluded.

## Evidence policy

The official CLI, headless mode, streaming input, TypeScript SDK, and user-input documentation URLs
are recorded in the manifest. Capability announcements take precedence over version checks. M5
freezes `2.1.219` as the minimum based on the documented request-ID, result-correlation, interrupt
receipt, and queue-cancellation gates; the locally verified reference remains `2.1.245`.

The versioned JSONL stream fixtures are synthetic and derived from the public SDK types and
examples. They exercise a startup event before `system/init`, open capability sets, multi-turn result
boundaries, queued UUIDs, image blocks, unknown fields, malformed input, and cross-session rejection.
They are not represented as captured model output.

The interrupt fixture freezes only the public `SDKControlInterruptResponse` payload semantics. It
requires capability gating, treats receipts as the pre-result queue snapshot, keeps unknown UUIDs
diagnostic-only, and rejects duplicate or contradictory IDs. It intentionally does not freeze a raw
control envelope: the official C++ surface and fallback decision remain open for the next M5 step.

## Permission bridge handshake

The C++20/Qt 6 probe server implements the minimum MCP stdio initialization and tool-list contract.
Claude Code 2.1.245 connected to it successfully on the no-message `--init-only --bare` path using an
inline `--mcp-config`, `--strict-mcp-config`, and `--permission-prompt-tool`. The only observed MCP
methods were `initialize`, `notifications/initialized`, and `tools/list`; Claude exited with code 0.
No user MCP setting was read or written.

This establishes process launch, MCP negotiation, and permission-tool discovery from C++. It does
not claim that a live permission prompt was invoked: triggering one requires a model turn and remains
an explicit opt-in check. The probe tool denies any accidental call.

## Runtime controls

The local CLI accepts valid `--model`, `--effort`, and `--permission-mode` values on `--init-only`.
Validation is not uniform: an unknown permission mode is a parser error, an unknown effort only warns
and silently falls back, and an unknown model is not validated before a turn. Snack must therefore
validate every GUI selection before launch.

The official live setters are exposed by the TypeScript Agent SDK in streaming-input mode. There is
no official C++ SDK or fully published C++ control-wire schema. M5 therefore rejects direct private
control messages. In M6, a pure C++ session queues model, effort, and permission-mode changes for the
next turn, then restarts and resumes with official CLI flags. A currently displayed MCP permission
prompt can still apply the GUI bridge policy immediately. This is an explicit degradation from the
requested ChatGPT-like hot switching, not a claim of full parity.

Default builds and tests parse sanitized fixtures only. Any probe that can send a valid user message
or invoke a model must be a separately documented, explicit opt-in and is never part of the default
test gate.
