# UX specification

## 1. Principles

Conversation first; honest state; progressive disclosure; recoverable failures; and one writable chat surface per conversation.

## 2. Default window

At the 1920×1080 design baseline, use a 280 px conversation rail, a central chat with header/timeline/composer, an initially closed 520-680 px right dock, and an initially closed 280-360 px terminal dock. At 1280×800, collapse the rail and show one auxiliary panel as an overlay or replacement. The main window does not shrink below 1280×800.

## 3. Conversation header

Show agent, title, directory summary, connection/write-lease state, model, effort, access level, and overflow actions. Edits during execution display “applies to the next message.” Unsupported choices remain disabled with a reason.

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
