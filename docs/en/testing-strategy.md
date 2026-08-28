# Testing strategy

## 1. Gates

Use Qt Test only. Require at least 80% total first-party line coverage. CI must pass compilation, all tests, coverage, `clang-format`, and baseline warnings. Exclude Qt, third-party code, MOC/UIC/RCC, translations, and generated installer code. Tests ship in the same milestone as their feature.

## 2. Test levels

- Unit: event mapping, request correlation, capabilities, state machines, canonical paths, permission rules, theme schema, diff hunks, hashes, retention, versions, and manifests.
- Component: session, event store, permissions, write leases, diffs, backup, terminals, and IPC with temporary files/databases and fake clocks/services.
- Protocol contracts: versioned Codex JSON Schema and fixtures; Claude init, assistant, partial stream, tools, result, retry, resume, capability, and approval-bridge fixtures. Unknown fields are retained; missing optionals never crash. CI never calls a live model.
- GUI: onboarding, creation, streaming, cards, queues, diffs, layouts, detached windows, themes, localization, zoom, and renderer recovery.
- Platform: ConPTY/Credential Manager/MSI; PTY/Secret Service/deb/Wayland/X11; PTY/Keychain/unsigned-app guidance; tray, notifications, watchers, DPI, and WebEngine.

## 3. High-risk matrix

| Area | Required paths |
|---|---|
| Permissions | once/session/always/deny, replay, timeout, disconnect, managed override |
| Session | close terminality, late connection/capability/identity/event signals, reconnect recovery |
| Diff | add/delete/rename/binary, hunk rejection, external conflict, dirty baseline |
| Write lease | acquire/release/transfer, crash cleanup, symlink aliases |
| Storage | WAL recovery, disk full, missing blob, references, migration failure |
| Backup | plain/encrypted, wrong password, tamper, traversal, rollback, remap |
| Protocol | partial frame, LF/CRLF limits, invalid JSON, duplicate ID, unknown event preservation/context/privacy, pending server-request terminal response/write failure, request timeout, late response, shutdown reconnect, terminate/kill fallback, flood, process death |
| WebEngine | hostile Markdown, blocked resources, renderer crash, URL validation |
| Terminal | Unicode, ANSI, resize, process exit, detach, true-exit recreation |

## 4. Coverage

Use LLVM source-based coverage, merge with `llvm-profdata`, and report with `llvm-cov`. Linux is the primary aggregate gate; platform runners cover platform-only modules. Every exclusion requires review justification.

## 5. Fixtures and privacy

Sanitize paths, repository names, tokens, and source before committing fixtures. Never commit real Codex/Claude configuration, credentials, or user sessions.

## 6. Definition of done

A requirement is complete only when implementation matches both language documents, success/failure/cancel/recovery paths have tests, coverage remains at least 80%, formatting and warnings pass, degradation UI is tested, and protocol fixtures/migrations are versioned.

M3 responsiveness tests use a high-output fake Agent that emits thousands of text, tool, reasoning, and plan deltas in short batches. A separate GUI timer must continue advancing while the turn runs, and every rendered stream must remain within its documented presentation bound. This gate never starts Codex or Claude and never invokes a model.

M4 terminal component tests inject a fake process for input, Unicode/ANSI projection, resize, exit, error, tab, detach, and close paths. An explicit temporary-SQLite test proves that terminal output changes the terminal screen while leaving the conversation's Agent event list empty. A platform smoke test may start only the local shell through ConPTY or PTY; it never starts Codex or Claude and never invokes a model.

M5 Claude protocol tests compile disposable C++ stream/control parsers and a deny-only MCP stdio probe server. Default tests consume sanitized official-example-derived fixtures and may start only that local probe server. A manual no-model check may start Claude with `--init-only`, `--bare`, inline strict MCP configuration, and no user message. Live turns, tools, permission prompts, questions, interrupts, queues, and resume/fork execution require explicit opt-in and never belong to the default gate.
