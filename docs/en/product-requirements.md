# Product requirements

Status: Baseline 1.0  
Date: 2026-08-25  
Display name: 零食 (temporary)  
Engineering identifier: `snack`

## 1. Product objective

Snack is a cross-platform, local-first desktop front end for Codex CLI and Claude Code CLI. It turns structured agent events into a readable and controllable chat workflow while preserving native sessions, authentication, tools, safety boundaries, and extension ecosystems.

Success means that users can complete real coding work without living in terminal output, run multiple visible sessions in parallel, change supported runtime settings for the next turn, review file changes by file or hunk, recover native sessions and GUI state after restart, and always see an explicit degradation when an official protocol lacks a capability.

## 2. Users and primary journey

The primary user already has Codex CLI or Claude Code CLI installed and authenticated and works in local code directories.

1. Snack detects CLI paths, versions, authentication, and protocol capabilities.
2. The user selects a local directory, agent, and safety configuration.
3. The user sends text, files, folders, images, diffs, or history references.
4. The GUI streams messages, reasoning, plans, tools, approvals, and errors.
5. The user reviews changes and continues in Snack or an external editor.
6. Sessions continue in the tray and recover through native session IDs.

## 3. Functional requirements

### 3.1 Agent and workspace

- `FR-AGENT-001` A conversation is permanently bound to Codex or Claude and cannot switch agent families.
- `FR-AGENT-002` A conversation is permanently bound to one local working directory. WSL, SSH, Dev Containers, and remote workspaces are out of scope.
- `FR-AGENT-003` Authentication and credentials remain owned by the CLI. Snack stores no API keys and installs no CLI.
- `FR-AGENT-004` Detect executable path, version, authentication, and capabilities; allow a manually selected executable.
- `FR-AGENT-005` Define minimum CLI versions, but use handshake and capability data as the final feature authority.
- `FR-AGENT-006` Codex and Claude implement an internal common adapter contract. Runtime third-party adapters are not supported.

### 3.2 Runtime settings

- `FR-SET-001` Always show agent, model, reasoning effort, access level, and state in the conversation header.
- `FR-SET-002` Model changes remain within the current agent family.
- `FR-SET-003` Model, effort, and access changes affect only this conversation and start with the next message.
- `FR-SET-004` An in-flight request keeps its send-time settings. Every user message stores a settings snapshot.
- `FR-SET-005` Snack stores per-agent defaults for new conversations without modifying native CLI configuration.
- `FR-SET-006` If hot switching is unavailable, finish the current request and restart/resume the native session.
- `FR-SET-007` Show token, context, and cost data only when supplied by official events. Never estimate.

### 3.3 Composer and conversation

- `FR-MSG-001` Support multiline text, per-session drafts, send, stop, attachment previews, and context hints.
- `FR-MSG-002` Support workspace files, folders, images, screenshots, diffs, history references, and `@` file/symbol search.
- `FR-MSG-003` Folder references honor ignore rules and exclude VCS, dependencies, and build outputs by default.
- `FR-MSG-004` While an agent runs, offer steer-now and queued-send. Unsupported steer degrades to queueing.
- `FR-MSG-005` Queued messages can be edited, reordered, sent now, or canceled. Restart never sends them automatically.
- `FR-MSG-006` Provide favorite prompts, parameterized templates, and a `/` menu. Templates cannot run scripts.
- `FR-MSG-007` Sent messages and responses cannot be edited, retried, or regenerated.
- `FR-MSG-008` Generate a title through the bound agent; fall back to a safe truncation of the first prompt.

### 3.4 Rendering and events

- `FR-EVENT-001` Render typed events for messages, reasoning, plans, tools, commands, file changes, approvals, questions, errors, and usage.
- `FR-EVENT-002` Support Markdown, code blocks, tables, task lists, links, LaTeX, and Mermaid. Arbitrary HTML is forbidden.
- `FR-EVENT-003` Tool cards are collapsed by default and show summary, state, duration, and key output.
- `FR-EVENT-004` Show detailed reasoning only when the official protocol provides it. Never extract hidden reasoning.
- `FR-EVENT-005` Show plans both inline and in a dockable task panel. Users modify plans through messages, not direct edits.
- `FR-EVENT-006` Render single-choice, multiple-choice, confirmation, and text prompts as one-question interaction cards.
- `FR-EVENT-007` The raw agent protocol panel is read-only. There is no direct terminal control of the agent process.
- `FR-EVENT-008` Local service URLs are ordinary links opened in the system browser.

### 3.5 Files, diffs, and editors

- `FR-DIFF-001` Provide read-only file previews and a built-in diff reviewer, not a code editor.
- `FR-DIFF-002` Accept or reject by file or hunk. Binary changes are file-level only.
- `FR-DIFF-003` Prefer official isolated-change facilities. Otherwise snapshot real files and apply conflict-safe reverse patches.
- `FR-DIFF-004` Detect external edits and never overwrite them silently.
- `FR-DIFF-005` Warn about a dirty workspace, then use the confirmed current contents as the baseline.
- `FR-DIFF-006` Open files in VS Code, Cursor, the system editor, or a custom command with line/column positioning when supported.
- `FR-DIFF-007` Accepting updates the review baseline; it never stages or commits Git changes.

### 3.6 Workspace write lease

- `FR-LOCK-001` Only one agent conversation may hold write access to a working directory at a time.
- `FR-LOCK-002` A conversation acquires `WorkspaceWriteLease` on its first write need and holds it until explicit release, transfer, or close.
- `FR-LOCK-003` Other conversations run read-only. If the protocol cannot enforce read-only, execution is blocked.
- `FR-LOCK-004` Transfer stops active writes and resolves conflicts. User shells and external editors are outside enforcement and are tracked as external edits.

### 3.7 Permissions

- `FR-PERM-001` New conversations choose a strict, workspace-auto, or fully automatic access level.
- `FR-PERM-002` Approval cards offer once, session, always, and deny where the native protocol permits.
- `FR-PERM-003` Persistent rules are modeled by operation, command pattern, path, and network target and can be reviewed or disabled.
- `FR-PERM-004` High-risk operations may force confirmation. Snack never bypasses native or managed policy.
- `FR-PERM-005` Tool arguments and protocol logs are shown verbatim. Snack never proactively copies keychain values into logs.

### 3.8 Sessions and windows

- `FR-SESSION-001` Support create, rename, delete, pin, archive, tags, full-text search, groups, and saved filtered views.
- `FR-SESSION-002` Filter by directory, agent, model, status, tag, and time.
- `FR-SESSION-003` Export Markdown/JSON. Copying a record never fabricates a new native timeline.
- `FR-SESSION-004` Multiple sessions run concurrently and expose idle, running, approval, question, complete, failed, and disconnected states.
- `FR-SESSION-005` The main window shows one full chat; other conversations may open in separate windows.
- `FR-SESSION-006` Use dockable, floating, saved workbench layouts while keeping the default conversation-first.
- `FR-SESSION-007` Run as a single application instance and forward later launches over same-user local IPC.
- `FR-SESSION-008` `snack <directory>` opens the most recently active matching conversation or a prefilled new-conversation page.

### 3.9 Shell terminal

- `FR-TERM-001` Each conversation owns a separate local shell area with multiple tabs. Shell processes are not agent processes.
- `FR-TERM-002` Support ANSI/VT, PTY/ConPTY, copy, paste, search, fonts, and zoom.
- `FR-TERM-003` A tab may detach into its own window. Split panes are out of scope.
- `FR-TERM-004` Shells continue while the app is in the tray and stop on true exit.
- `FR-TERM-005` Restart recreates tabs and shells without rerunning commands or persisting scrollback.

### 3.10 Persistence and backup

- `FR-DATA-001` Store GUI event indexes, logs, layouts, permissions, templates, and pending diffs; native sessions own model context.
- `FR-DATA-002` Store metadata in SQLite and large objects in compressed, deduplicated content-addressed storage.
- `FR-DATA-003` Chats and logs use ordinary local storage. Sensitive configuration and persistent grants use the OS keychain.
- `FR-DATA-004` Default retention is 30 days for raw logs and reviewed snapshots, with a 10 GB cap. Pending review data is never auto-deleted.
- `FR-DATA-005` Create manual plain or password-encrypted full backups, excluding CLI credentials and keychain secrets.
- `FR-DATA-006` Restore replaces all GUI data after an automatic safety backup and supports rollback and path remapping.
- `FR-DATA-007` Restart reconnects previously running or waiting native sessions but never automatically resends interrupted or queued messages.

### 3.11 Desktop behavior

- `FR-DESKTOP-001` Closing hides to the system tray; true exit confirms and terminates active agents and shells cleanly.
- `FR-DESKTOP-002` OS notifications contain generic state only and reveal no title, path, or message text.
- `FR-DESKTOP-003` Provide fixed cross-platform shortcuts and global UI zoom. Custom shortcuts and Vim mode are out of scope.
- `FR-DESKTOP-004` Provide light, dark, system, and schema-validated JSON themes.
- `FR-DESKTOP-005` Let users select installed UI, chat, and monospace fonts independently.
- `FR-DESKTOP-006` Ship Simplified Chinese and English with an extensible Qt localization framework.

## 4. Non-functional requirements

- `NFR-001` C++20, Qt 6.8 LTS+, Qt Widgets, Qt WebEngine, and CMake.
- `NFR-002` Guaranteed: Windows 10/11, Debian 13, Ubuntu 22.04 LTS. macOS follows Qt support.
- `NFR-003` Target architectures: Windows x64, Linux x64/ARM64, macOS Intel/Apple Silicon.
- `NFR-004` Main-window minimum 1280×800; design baseline 1920×1080; validate 125%-200% DPI.
- `NFR-005` Protocol I/O, indexing, diffs, snapshots, and large database work never block the GUI thread.
- `NFR-006` No Snack telemetry, crash uploads, or microphone permission.
- `NFR-007` At least 80% total first-party line coverage; tests, formatting, and baseline warnings must pass.
- `NFR-008` Keep dependencies small, pin them with CMake `FetchContent`, and publish third-party notices.

## 5. Explicitly out of scope

Direct model APIs; remote/WSL/container workspaces; a full editor, Git client, web preview, or port manager; MCP/Skills/plugin management; runtime third-party adapters; voice; app PIN or biometrics; formal accessibility certification; automatic updating or diagnostics upload; arbitrary HTML or theme code; terminal-screen parsing.

## 6. Distribution boundary

- SemVer starting at `0.1.0`.
- Windows x64 MSI.
- Linux amd64/arm64 `.deb`.
- Unsigned, unnotarized Intel and Apple Silicon macOS `.app` archives.
- Public source and CMake build instructions under the MIT License.
