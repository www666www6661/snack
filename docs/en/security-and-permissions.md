# Security and permission model

## 1. Trust boundaries

Snack trusts the current OS user, but not agent output, workspace content, Markdown, tool arguments, theme files, backups, or external URLs. CLIs and their extensions may use the network; “Snack collects no data” does not imply that an underlying CLI is offline.

## 2. Access levels

The GUI exposes stable semantics and adapters map them to official native policy:

| GUI level | Meaning |
|---|---|
| Strict | Confirm sensitive commands, writes, network, and out-of-root access |
| Workspace automatic | Allow ordinary in-workspace reads/writes; still confirm high-risk, network, or boundary crossing |
| Fully automatic | Use the highest native automation allowed; managed policy and mandatory confirmations still win |

The effective mapping is inspectable. If an agent cannot express an equivalent level, Snack explains the difference and chooses the stricter effective policy.

## 3. Approval state

Requests bind conversation, turn, native request, and tool/item IDs. Show once, session, always, deny, or cancel only when supported. Replayed requests after reconnection are idempotent by native request ID.

Persistent rules are structured by operation, argv/pattern, canonical path scope, network host/protocol/port, agent, and origin. They are not arbitrary text allowlists, and managed/high-risk policy may override them.

## 4. Mandatory constraints

- Workspace write leases override fully automatic mode.
- If read-only cannot be enforced, do not execute the read-only agent.
- Sensitive directories, elevation, bulk deletion, credential locations, and unknown boundary crossings may force confirmation.
- Spawn processes with argv arrays, never shell-concatenated user input.
- Inherit only necessary environment while retaining CLI authentication needs.
- Never modify user Codex/Claude extension configuration.

Review rejection is compare-before-write: the file hash must still equal the hash rendered to the user, and replacement is atomic. A mismatch is an external-edit conflict and never overwrites either side. Write leases use canonical, case-normalized workspace identity so path aliases cannot obtain parallel ownership; destruction releases an in-process lease, while a future cross-process lease must add an OS-backed recovery record before replacing this boundary.

## 5. Web content

Messages are not trusted HTML. Markdown parsing disables HTML and images, then DOMPurify removes scripts, event handlers, iframes, arbitrary user style, media/forms, and source attributes. KaTeX runs with `trust: false`; Mermaid uses strict mode without HTML labels and its generated SVG is sanitized again. A no-network CSP and C++ request interceptor independently reject remote subresources and local-file URLs. External navigation returns to C++ and allows only validated HTTP(S)/`mailto:` targets. User themes are schema-validated JSON color tokens, never QSS/CSS/JS.

The renderer cannot receive tool arguments, workspace paths, arbitrary commands, or an Agent handle through WebChannel. Its off-the-record profile persists no cookies or cache. Generic tests disable WebEngine and use the plain-text fallback; a dedicated MSVC/official-Qt suite renders hostile Markdown entirely from qrc, checks links at the C++ boundary, and never opens a browser or contacts a host.

## 6. Data protection

CLI credentials stay with the CLI. Sensitive configuration and persistent-grant secrets use Credential Manager, Keychain, or Secret Service. Chats, logs, and snapshots use normal local permissions and show protocol-returned values verbatim in-app. OS notifications are generic. Snack has no telemetry, crash upload, or diagnostic upload.

## 7. Backup and restore

Plain backups warn that they may contain source, paths, and secrets. Encrypted backups use established password derivation and authenticated encryption; passwords are neither saved nor recoverable. Restore rejects path traversal, symlink escape, archive bombs, duplicate entries, and oversized files, validates in a temporary area, then atomically replaces data.

## 8. Single-instance IPC

Only the current OS user may connect. The schema accepts a small set of actions such as activate and open a validated local directory. It never accepts arbitrary commands, environment variables, or prompts.

## 9. Threat verification

Test malicious Markdown/Mermaid/URLs, approval replay, symlink and case aliases, concurrent writers, corrupted migrations/backups, oversized frames and output floods, malicious themes/filenames/terminal escapes/archives, and WebEngine crashes.
