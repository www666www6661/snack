# Development plan

Build a complete Codex vertical slice before Claude. Every milestone ships tests and both language documents. Product code begins only after the specification, architecture, and visual baseline are accepted.

## M0: specification and design (`0.0.x`)

Deliver these bilingual documents, high-fidelity static designs, official capability matrix, and risk register. Validate links and page coverage and obtain baseline approval.

## M1: skeleton and test foundation (`0.1.0-alpha.1`)

Status: implementation complete; formal cross-toolchain acceptance remains open. The first vertical slice provides the application shell, settings and themes, single-instance IPC, tray lifecycle, window/layout restoration, versioned SQLite migration with backup/read-only recovery, the event repository, content-addressed storage, the common Agent interface, a fake streaming conversation, and Qt Test suites. The 2026-08-27 strict-warning Windows Qt 6.8.3/LLVM-MinGW baseline passes 11/11 Debug tests, 10/10 coverage tests, 30 IPC repetitions, and 84.59% first-party line coverage; this does not replace Windows `clang-cl + MSVC ABI`, Ubuntu, Debian, or macOS validation.

CMake/Qt 6.8/C++20, Clang presets, Qt Test, LLVM coverage, metadata, localization, theme schema, logging, single instance, settings, mock agent, SQLite migrations, event/content interfaces, and GitHub Actions gates. Acceptance: the shell starts on all desktop platforms, a mock agent streams one turn, and UI events recover after a crash.

## M2: Codex vertical slice (`0.1.0-alpha.2`)

Status: complete for automated-contract and no-model local acceptance as of 2026-08-27. CLI discovery with a frozen `0.149.0` minimum, injectable `QProcess` transport, bounded JSONL framing, initialization, `account/read`, request correlation and timeouts, paginated `model/list`, model-specific capabilities, `thread/start`/`thread/resume`/`thread/list`/`thread/read`, persisted native identities, per-turn model/effort/access overrides, `turn/steer`/`turn/interrupt`, text and tool streaming, reasoning summaries, inline/task-dock plans, error closure, command/file approvals, `tool/requestUserInput`, thread token/context usage, reconnect/process-loss cleanup, main-application Codex/Mock selection, and versioned Codex CLI `0.149.0` schemas/fixtures are complete.

Acceptance evidence includes fixture coverage for normal, failure, cancellation, interruption, recovery, stale-event, malformed-terminal, and pending-request cleanup paths. A real installed CLI passes discovery, initialization, authentication-state reading, model discovery, and ephemeral thread creation without invoking a model. Live `turn/start`, approval, cancellation, and recovery remain explicit opt-in manual checks because they can consume model usage; they were not run for this milestone close and do not belong to the default safe test gate.

## M3: daily conversation UX (`0.1.0-beta.1`)

Status: in progress. The existing composer foundation includes bounded auto-growth, per-conversation drafts, steer/queue controls, editable persistent queues, parameterized prompt templates, fixed shortcuts, interface zoom, and a live system/light/dark theme selector. Conversation identity now persists placeholder-title state, replaces it once with a safe first-prompt fallback, and supports explicit normalized renaming, preparing stable labels for the conversation rail without overwriting established titles.

Navigation/search/groups/tags/views, detached chat, attachments and `@`, templates and `/`, steer/queue, safe WebEngine Markdown/LaTeX/Mermaid, themes, tray, private notifications, shortcuts, zoom, and layouts. Acceptance: daily multi-session Codex use remains responsive under heavy output.

## M4: workspace, diff, terminal (`0.2.0`)

Read-only files and external editors, watching/indexing, snapshots and official-isolation detection, hunk review and conflicts, write leases, multi-tab ConPTY/POSIX shells, and detached terminal windows. Acceptance: direct edits and external conflicts recover safely and terminal output never enters agent events.

## M5: Claude protocol spike

Validate long-lived bidirectional stream-json, init capabilities and turn boundaries, resume/fork, interrupt/queue/images, the local `--permission-prompt-tool` bridge without user MCP changes, and stable C++ paths for live model/effort/permission changes. Freeze minimum version and degradation matrix. No screen scraping or undocumented control-wire dependency may exit this milestone.

## M6: Claude adapter (`0.3.0`)

Implement only M5-confirmed session, event, approval, prompt, recovery, and settings behavior. Run the shared Codex/Claude contract suite and prevent scattered Claude branches in common UI.

## M7: lifecycle and distribution (`0.4.0`)

30-day/10 GB maintenance, storage reporting, plain/encrypted backup and replace restore, platform integration, MSI, deb, app archive, licenses, migration, installer tests, and manual release checklist.

## M8: stabilization (`1.0.0`)

Performance, long-running sessions, protocol compatibility, recovery, DPI, multi-monitor, localization, and security regression. Close every P0/P1 issue and publish the final compatibility matrix.

## Highest risks

Stable Claude control from C++; hunk rejection with external edits; WebEngine memory/crash/security; cross-platform terminals; one deb baseline for Debian 13 and Ubuntu 22.04 with WebEngine; and backpressure across multiple agents, watchers, and SQLite. Run disposable spikes before freezing interfaces.
