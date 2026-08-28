# Claude adapter

M6 promotes the M5 protocol decisions into the production C++20/Qt 6 Agent runtime. The adapter
uses only the public Claude Code CLI stream, session flags, and MCP permission-tool integration. It
does not parse terminal presentation, install an SDK sidecar, modify user MCP settings, or depend on
an unpublished control envelope.

## Discovery and startup

Snack discovers `claude.cmd`, `claude.exe`, or `claude` on Windows and `claude` on POSIX. A custom
path can be stored separately from the Codex path. Discovery performs three no-model checks:

1. Parse `claude --version` and require `2.1.219` or newer. A prerelease of exactly `2.1.219` is
   rejected.
2. Confirm the public stream, session, model, effort, and permission options in `--help`.
3. Run an empty `--init-only --safe-mode --strict-mcp-config` stream probe with an empty inline MCP
   configuration. It sends no user message and cannot start a model turn.

Production sessions use a persistent process:

```text
claude -p
  --input-format stream-json
  --output-format stream-json
  --verbose
  --include-partial-messages
  --replay-user-messages
```

A new conversation receives a Snack-generated UUID through `--session-id`; a restored conversation
uses `--resume`. Model, effort, permission mode, working directory, inline permission MCP, and
permission-tool name are added as explicit arguments. Production does not use `--safe-mode`,
`--bare`, or `--strict-mcp-config`, so Claude's normal extensions and user MCP servers remain
available.

## Transport and identity

`ClaudeStreamClient` owns the injected process transport. It accepts fragmented LF or CRLF JSONL,
limits an individual frame to 4 MiB, retains at most 64 KiB of stderr diagnostics, enforces an init
timeout, and force-kills a process that ignores shutdown. Informational or unknown startup events may
precede `system/init`; turn records may not. A malformed frame, duplicate init, oversized frame, or
record from another Session fails closed.

The init Session ID and canonical working directory must match the requested values. Snack persists
the Claude Session ID as both common native identity fields because Claude has one public Session
identity rather than Codex's separate Thread and Session values. Result events are associated with a
GUI Turn only through the required `user_message_uuid`; a stale result cannot close another Turn.

## Events and input

Visible text deltas and final assistant messages are reconciled so partial and final streams do not
duplicate output. Text, tools, tool results, usage, warnings, terminal Turn state, and unknown
forward-compatible events map to the common `AgentEvent` model. Image attachments are read with a
20 MiB bound and sent as base64 image blocks; ordinary files remain explicit path references.

Claude thinking and signature content is never stored as raw protocol data or displayed as
chain-of-thought. Snack emits only reasoning-started/reasoning-completed state and inserts a
`redacted` marker in sanitized diagnostics.

`AskUserQuestion` maps to the shared blocking question card. Single-choice questions retain their
options; multi-select questions use a free-text fallback because the common card currently has no
multi-select control. Validated answers return as a standard `tool_result` in the same Session.

## Permission bridge

Each Session owns a random local socket name and 256-bit token. The inline MCP configuration starts
the bundled `snack_claude_permission_server` C++ helper. The helper implements MCP stdio
`initialize`, `tools/list`, `tools/call`, and `ping`, then forwards only permission calls to the GUI
over the authenticated local socket.

Permission calls become the same command/file approval cards used by Codex. Allow-once returns
`behavior: allow` with the original input. Allow-for-session is offered only when Claude supplies
permission suggestions and returns them as `updatedPermissions`. Deny and cancel return explicit
deny decisions. Frames are bounded to 1 MiB, IDs are one-shot, and every pending request is denied
when its Turn, Session, or bridge closes. The bridge never reads or writes Claude's user MCP
configuration.

## Runtime controls and interrupt

Claude has no public C++ live setter. Changes made while a Turn runs remain next-Turn settings. Before
that next Turn is sent, Snack stops the process and starts the same Session with `--resume` plus the
new `--model`, `--effort`, and `--permission-mode` values. The original GUI Turn ID and message stay
queued until the resumed init is validated. No message can accidentally run under the old settings.

The mappings are:

| Snack | Claude CLI |
| --- | --- |
| low / medium / high / xhigh / max | `--effort` value with the same name |
| legacy minimal | low |
| legacy ultra | max |
| strict | manual |
| workspace | acceptEdits |
| full | bypassPermissions |

Interrupt uses the same documented process-and-resume fallback: terminate the active Claude
process, close the GUI Turn as interrupted, then resume the Session. Snack does not send an inferred
private interrupt control message. Steering remains unavailable for Claude.

## Test boundary

Default Qt tests use injected transports, synthetic public-example-derived JSONL, temporary local
sockets, and the bundled MCP helper. They cover discovery, framing, init, multi-turn correlation,
image encoding failures, visible streaming, thinking redaction, tool results, questions, permission
decisions, restart/resume controls, interrupt, timeouts, process loss, and cleanup. They never send a
valid prompt to an installed Claude CLI and never consume model usage. A real model turn and live
permission invocation remain explicit opt-in manual checks.

