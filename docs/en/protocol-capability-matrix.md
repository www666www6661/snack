# Protocol capability matrix

Reviewed: 2026-08-26

Locally observed versions: Codex CLI `0.149.0`, Claude Code `2.1.245`

## 1. Decision

Codex uses `codex app-server` over the default stdio JSONL transport. It is the official JSON-RPC interface for rich clients and documents authentication, models, threads, turns, events, approvals, diffs, filesystem operations, and recovery.

Claude uses a persistent `claude -p --input-format stream-json --output-format stream-json --verbose --include-partial-messages` process. The CLI officially exposes bidirectional streams, session IDs, resume, permission modes, and capability announcements. However, the full live control surface is primarily exposed through the official TypeScript and Python Agent SDKs, and there is no C++ SDK today. Snack will not bind to undocumented control messages. The Claude milestone therefore begins with a protocol spike and degrades missing controls to restart-and-resume or disabled UI.

## 2. Status terms

- **Native**: a public official CLI protocol suitable for a direct C++ client.
- **Bridge**: an officially supported local bridge, such as Claude `--permission-prompt-tool` MCP.
- **Restart**: finish the active request, restart the CLI, and resume the native session.
- **Probe**: the capability exists officially, but the stable C++ access path requires validation.
- **Unavailable**: no reliable official capability; the GUI hides or disables it.

## 3. Core matrix

| Capability | Codex app-server | Claude Code CLI | Snack behavior |
|---|---|---|---|
| Initialize/capabilities | Native: `initialize` | Native: `system/init.capabilities` | Capabilities outrank version checks |
| Model discovery | Native: `model/list` | Probe: Agent SDK `supportedModels()`; no equivalent public CLI command | Show only reliable values |
| Start session | Native: `thread/start` | Native: new print stream | Persist native ID |
| Resume | Native: `thread/resume` | Native: `--resume` | Never synthesize context from GUI history |
| List/read history | Native: `thread/list`, `thread/read` | Limited scripted surface | GUI indexes records; context resumes natively |
| Streaming text | Native item deltas | Native partial `stream-json` events | Incremental rendering |
| Image input | Native image/localImage items | Native streaming input image content | Gate by model capability |
| Mid-turn steer | Native: `turn/steer` | Probe: streaming input supports queues/interrupts; controls live in SDK | Degrade to queueing |
| Queued messages | GUI plus native turns | Native streaming queue | GUI keeps editable queue |
| Interrupt | Native: `turn/interrupt` | Probe: SDK `interrupt()` and documented process signals | Prefer control; otherwise terminate cleanly and resume |
| Live model change | Native per-turn model | Probe: SDK `setModel()` | Restart when no stable C++ route exists |
| Live effort change | Native per-turn effort | Probe: `--effort` and SDK thinking controls | Restart fallback |
| Live access change | Native per-turn sandbox/approval | Probe: SDK `setPermissionMode()` | Restart fallback |
| Command approval | Native server request | Bridge: `--permission-prompt-tool` or validated official control | Idempotent local bridge |
| File approval | Native file-change request | Bridge/Probe through tool approvals | Fall back to safety mode and GUI snapshots |
| Clarifying question | Native `tool/requestUserInput` | Bridge/Probe via `AskUserQuestion` | Normalize to prompt cards |
| Plans/todos | Native plan items/events | Native/Probe todo tool events | Never invent progress |
| Tool events | Native item events | Native tool_use/tool_result messages | Generic card fallback |
| Reasoning stream | Native summaries/raw deltas when available | Native thinking/partial blocks when enabled | Show only returned content |
| Token/context | Native usage events | Native/Probe result usage and SDK context query | Hide absent values |
| Cost | Only official event data | Result may include estimated cost | Label source; never calculate product-side |
| Diff events | Native `turn/diff/updated`, fileChange | Tool events plus file watching | Normalize in GUI diff model |
| File watching | Native `fs/watch` can assist | GUI watcher/scanner | Agent events first, watcher second |
| Isolated changes | Not assumed | Not assumed | Enable only after explicit capability proof |
| Session fork | Native `thread/fork` | Native `--fork-session` | Internal recovery use only |
| Authentication status | app-server auth endpoints/CLI | `claude auth status` JSON | Never read or store tokens |
| Slash commands | Protocol/CLI capability | SDK `supportedCommands()` or documented CLI commands | Show GUI commands if enumeration is unreliable |

## 4. Codex transport constraints

- Use `codex app-server` stdio, one JSON message per line.
- Send `initialize`, then the `initialized` notification before any other request.
- Correlate requests, responses, and server requests by ID and retain raw messages.
- Model threads and turns separately. Per-turn settings can override model, effort, cwd, approval, and sandbox policy.
- Generate version-specific JSON Schema for contract tests with `codex app-server generate-json-schema`.
- Drive model, effort, input-modality, and personality controls from the complete paginated `model/list` result; use the advertised defaults when a selection disappears.
- The documented WebSocket transport remains experimental; v1 does not use it.

## 5. Claude transport constraints

- Keep stdin open in print mode with bidirectional `stream-json` for multi-turn use.
- Treat `system/init` as the authority for capabilities, model, tools, MCP, and plugin state.
- Treat `result` as the turn boundary carrying session ID and available usage/cost metadata.
- Evaluate `--permission-prompt-tool` for the approval bridge. The internal bridge is not a user extension manager and never edits user configuration.
- Do not treat the TypeScript SDK's internal CLI control wire format as a stable public C++ API. If a feature requires undocumented messages, degrade it.
- Resume with `--resume <session-id>` after process replacement. Never resend an interrupted request automatically.

## 6. Minimum-version policy

Exact minimum versions are frozen after the Codex and Claude protocol spikes. A version qualifies only when capability discovery, create/resume, streaming, cancellation, errors, and correctly scoped approvals all pass contract tests. Older versions receive an upgrade message, never screen scraping. Unknown newer versions may run only after a successful handshake.

## 7. Official sources

- OpenAI Docs: https://developers.openai.com/codex/app-server
- Claude Code CLI reference: https://code.claude.com/docs/en/cli-reference
- Claude Code non-interactive mode: https://code.claude.com/docs/en/headless
- Claude Agent SDK streaming input: https://code.claude.com/docs/en/agent-sdk/streaming-vs-single-mode
- Claude Agent SDK user input and approvals: https://code.claude.com/docs/en/agent-sdk/user-input
- Claude Agent SDK TypeScript control surface: https://code.claude.com/docs/en/agent-sdk/typescript

These pages evolve with the CLIs. Every release refreshes schemas or event fixtures and records the versions actually verified.
