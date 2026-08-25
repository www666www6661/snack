# Development plan

Build a complete Codex vertical slice before Claude. Every milestone ships tests and both language documents. Product code begins only after the specification, architecture, and visual baseline are accepted.

## M0: specification and design (`0.0.x`)

Deliver these bilingual documents, high-fidelity static designs, official capability matrix, and risk register. Validate links and page coverage and obtain baseline approval.

## M1: skeleton and test foundation (`0.1.0-alpha.1`)

Status: in progress. The first vertical slice now provides the application shell, settings and themes, single-instance IPC, tray lifecycle, window/layout restoration, the SQLite event repository, content-addressed storage, the common Agent interface, a fake streaming conversation, and Qt Test suites. A strict-warning Windows Qt 6.8.3/LLVM-MinGW build now passes 8/8 tests, 30 IPC repetitions, and 83.86% first-party line coverage; this does not replace formal `clang-cl + MSVC ABI` verification. M1 acceptance still requires migration failure recovery and builds with all three release toolchains.

CMake/Qt 6.8/C++20, Clang presets, Qt Test, LLVM coverage, metadata, localization, theme schema, logging, single instance, settings, mock agent, SQLite migrations, event/content interfaces, and GitHub Actions gates. Acceptance: the shell starts on all desktop platforms, a mock agent streams one turn, and UI events recover after a crash.

## M2: Codex vertical slice (`0.1.0-alpha.2`)

App-server stdio and schema handshake; models, thread start/resume/read/list; turn start/steer/interrupt; streamed messages/tools/reasoning/plans/errors/usage/approvals; CLI detection, native IDs, capability disabling, and raw diagnostics. Acceptance includes live local Codex and complete fixture failures.

## M3: daily conversation UX (`0.1.0-beta.1`)

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
