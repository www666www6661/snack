# UX specification

## 1. Principles

Conversation first; honest state; progressive disclosure; recoverable failures; and one writable chat surface per conversation.

## 2. Default window

At the 1920×1080 design baseline, use a 280 px conversation rail, a central chat with header/timeline/composer, an initially closed 520-680 px right dock, and an initially closed 280-360 px terminal dock. At 1280×800, collapse the rail and show one auxiliary panel as an overlay or replacement. The main window does not shrink below 1280×800.

## 3. Conversation header

Show agent, title, directory summary, connection/write-lease state, model, effort, access level, and overflow actions. Edits during execution display “applies to the next message.” Unsupported choices remain disabled with a reason.

A new conversation begins with a localized placeholder title. After the first accepted message, the current M3 fallback replaces it with at most 72 Unicode code points from the normalized prompt, removes control and formatting characters, and persists the result before updating both header and conversation rail. Existing or user-defined titles are never replaced. The File menu and `F2` rename the current conversation through the same normalization and persistence boundary, including while a turn is active. Agent-generated titles can supersede the fallback only when a future common capability explicitly supports them.

The M3 conversation rail is backed by the repository catalog rather than placeholder rows. Selecting another entry persists the outgoing draft, opens or reuses that conversation through `SessionManager`, disconnects the previous view binding, restores the selected timeline, queue, draft, capabilities, and connection state, and persists the new active conversation. If its original Agent runtime is unavailable, the current conversation remains selected and the interface reports the failure instead of crossing Agent types.

The rail follows standard Qt list navigation: arrow keys move selection without switching conversations, while `Enter` or double-click activates the selected row through the same guarded navigation path. Archived rows remain non-activatable.

Conversation search filters the already-loaded catalog locally and case-insensitively across title, workspace path, Agent type, and tags. Whitespace-separated terms combine with AND semantics, while double quotes preserve whitespace inside one term. A `tag:name` term requires an exact case-insensitive tag match. An `agent:codex`, `agent:claude`, or `agent:mock` term requires that stable Agent kind regardless of the UI language. `model:id` requires an exact case-insensitive match against the persisted next-turn model ID and never opens a runtime to discover it. `status:` accepts the stable values `dormant`, `connecting`, `idle`, `running`, `waiting-approval`, `waiting-input`, `disconnected`, `failed`, and `closed`. `path:"directory name"` performs a case-insensitive workspace-path substring match, including paths with spaces. Structured terms can be combined with each other and with ordinary text. Filtering never opens, closes, reconnects, or otherwise mutates a session; clearing the query restores the repository-defined order.

`Ctrl/Cmd+K` clears the current rail filter and focuses the conversation search field without touching any session runtime.

While search owns focus, `Enter` opens the first non-archived result through the normal navigation path and `Esc` clears the query and returns focus to the Composer. An archived-only result set is never opened by `Enter`.

The rail button and `Ctrl/Cmd+N` create a new conversation in the current workspace using the saved preferred Agent. Creation switches through the same view-binding path as navigation, focuses the composer, and records the actual selected Agent. A new conversation may visibly fall back to Mock when its preferred Agent is unavailable because no historical native identity exists yet.

Archiving the current idle conversation closes its runtime and selects the next active catalog entry; when none exists, a replacement conversation is created first. Active Agent work cannot be archived. Archived rows remain visible, cannot be opened directly, and can be restored through the File menu without losing history.

Pinning is a metadata-only toggle available for the current conversation. It does not reconnect or recreate the runtime; the repository order is immediately reapplied so pinned rows lead their active or archived section.

Conversation tags are edited as a comma-separated metadata list from the File menu. Tags are trimmed, control characters are removed, duplicates collapse case-insensitively, and the result sorts deterministically; each conversation accepts at most eight tags of at most 32 Unicode code points each. Invalid edits preserve the previous list. Tags appear on rail rows and participate in local case-insensitive search without reconnecting a runtime.

Each rail row exposes a context menu for opening, pinning, archiving, or restoring that selected conversation. Actions operate on the row rather than silently switching first; archiving a background conversation leaves the current chat bound and uses the same active-work guard as the File menu.

The rail context menu applies the same tag editor to the selected background or archived row. Editing a closed row updates repository metadata directly and does not open, restore, or switch to that conversation.

Active rows include a text status such as Connecting, Idle, Running, Waiting for approval/input, Disconnected, or Failed. Status changes from every open runtime refresh the rail in place, so background work remains visible without switching conversations.

A background conversation becomes locally unread when an Agent response, tool, approval/input request, warning, or error begins. Streaming deltas do not repeatedly refresh the rail; opening that conversation clears the in-memory unread marker. The current conversation is never marked unread.

The View menu exposes Mark all conversations read only while unread markers exist. Clearing them is an in-memory presentation action: it neither switches conversations nor interrupts background work.

Open next unread conversation follows repository order from the current row, wraps at the end, skips archived rows, and uses the same strict navigation boundary. Opening each target clears only that conversation's unread marker.

The View menu can hide or show archived rows. This is a persistent local view preference, defaults to visible for compatibility, and never changes archive metadata or runtime ownership.

The conversation rail can save the current query and archived-row visibility under a named view. Choosing a saved view reapplies both values locally and refreshes the catalog without opening, switching, reconnecting, or otherwise mutating any conversation runtime. Manual search or visibility edits return the selector to Current filter while preserving the saved definition.

Deleting the selected saved view requires explicit confirmation. The definition is removed, but its currently applied query and archived-row visibility remain active as Current filter so deletion never causes an unexpected catalog expansion or session action.

The selected saved view can be renamed from the Manage menu. Renaming changes only the view name while preserving its query, archived-row visibility, and position. A name conflict or persistence failure keeps the original definition and current selection without touching any conversation runtime.

`Ctrl+Tab` and `Ctrl+Shift+Tab` cycle forward and backward through active conversations in repository order, wrap at either end, skip archived rows, and save the outgoing draft before opening the target through the normal strict Agent boundary.

A persistent notice directly below the header reports an Agent fallback, connection loss, or reconnect attempt. Fallback notices remain visible while the fallback Agent is active. Connection failures preserve the original diagnostic and expose Reconnect in the notice; retrying keeps the previous cause visible until the native handshake succeeds. The status bar may echo the message but is never its only location. `Closed` is terminal: delayed connection, capability, native-identity, or event signals from an asynchronously exiting Agent cannot reopen the conversation, replace its diagnostic, or append timeline data.

## 4. Timeline

Use a quiet user-message surface and a broad reading column for the agent. Tool cards show one-line summary, state, and duration before expansion. Collapse completed reasoning and label whether it is detailed reasoning or a summary. Approval cards outrank tools and explain action, target, reason, and risk. Prompt cards submit one question. Plans show current step inline and full detail in the task dock. Interruption and unknown-result states persist.

## 5. Composer

Provide attachment, `@`, template, and `/` entry points; an auto-growing editor; send mode; and send/stop controls. While running, choose steer-now or queue. Show an editable, reorderable queue above the composer. Approval or question requests take focus priority.

The current M3 composer grows with wrapped content up to a bounded reading height and follows the fixed keyboard map below. Draft text is stored locally under the conversation UUID after a short idle debounce and again when the window closes. Successful send or queue acceptance clears the draft immediately; drafts never become events or agent input until the user sends them.

The `/` button and a leading `/` keystroke open favorite prompt templates. Templates are stored as plain text and use only explicit `{{parameter}}` placeholders; insertion asks for each unique value, substitutes text literally, and leaves the rendered prompt in the composer for review. The menu can save the current composer text as a template or remove an existing template. It never runs scripts, commands, or substitutions from the workspace.

## 6. Key flows

- New conversation: directory → dirty-workspace warning → agent availability → model/effort/access → actual change mode/read-only capability → create. Preserve form state on failure.
- Approval: state what, where, why, and risk before decisions. Confirm persistent-rule scope separately.
- Diff: group files by pending/conflict/accepted/rejected, place hunk controls next to hunks, and disable rejection on external conflict.
- Recovery: non-blocking recovery center with connecting/recovered/interrupted/failed. A disconnected conversation exposes Reconnect in its header; it resumes native context without retrying the interrupted turn or replaying queued messages. Interrupted turns offer open, not retry.
- Exit: window close goes to tray; true exit lists agent and shell work and explains interruption/no auto-resend.

## 7. Panels and windows

Dockable surfaces: conversations, files, diff, tasks, permissions, raw protocol, terminal, and details. Built-in layouts: Focus, Review, Terminal Debug, and Multi-session Monitor. Detached chat and terminal windows restore safely to visible screens.

## 8. Navigation

Support groups, pinning, tags, archive, and saved views. State uses shape/text as well as color. Search covers messages, responses, tool summaries, and paths; raw logs are optional.

## 9. Fixed shortcuts

| Action | Windows/Linux | macOS |
|---|---|---|
| New conversation | `Ctrl+N` | `Cmd+N` |
| Search | `Ctrl+K` | `Cmd+K` |
| Next / previous conversation | `Ctrl+Tab` / `Ctrl+Shift+Tab` | `Ctrl+Tab` / `Ctrl+Shift+Tab` |
| Focus composer | `Ctrl+L` | `Cmd+L` |
| Send / newline | `Enter` / `Shift+Enter` | same |
| Queue send | `Ctrl+Enter` | `Cmd+Enter` |
| Stop | `Esc` when composer is idle | `Esc` |
| Diff | `Ctrl+Shift+D` | `Cmd+Shift+D` |
| Terminal | `Ctrl+J` | `Cmd+J` |
| Zoom | `Ctrl +/-/0` | `Cmd +/-/0` |

Do not intercept normal shell combinations while a terminal owns focus.

## 10. Copy and notifications

Localize GUI copy and preserve original CLI errors. OS notifications use generic text only. Destructive prompts name the action and target instead of asking a vague “Are you sure?”
