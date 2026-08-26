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
| Diff | add/delete/rename/binary, hunk rejection, external conflict, dirty baseline |
| Write lease | acquire/release/transfer, crash cleanup, symlink aliases |
| Storage | WAL recovery, disk full, missing blob, references, migration failure |
| Backup | plain/encrypted, wrong password, tamper, traversal, rollback, remap |
| Protocol | partial frame, invalid JSON, duplicate ID, unknown event, request timeout, late response, shutdown reconnect, flood, process death |
| WebEngine | hostile Markdown, blocked resources, renderer crash, URL validation |
| Terminal | Unicode, ANSI, resize, process exit, detach, true-exit recreation |

## 4. Coverage

Use LLVM source-based coverage, merge with `llvm-profdata`, and report with `llvm-cov`. Linux is the primary aggregate gate; platform runners cover platform-only modules. Every exclusion requires review justification.

## 5. Fixtures and privacy

Sanitize paths, repository names, tokens, and source before committing fixtures. Never commit real Codex/Claude configuration, credentials, or user sessions.

## 6. Definition of done

A requirement is complete only when implementation matches both language documents, success/failure/cancel/recovery paths have tests, coverage remains at least 80%, formatting and warnings pass, degradation UI is tested, and protocol fixtures/migrations are versioned.
