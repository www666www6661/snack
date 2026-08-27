# Visual design system

## 1. Direction

The default is calm, spacious, and reading-oriented like the ChatGPT desktop app, without becoming a generic web shell. Native Qt menus, windows, docking, and keyboard behavior preserve desktop character. Branding uses a neutral placeholder.

## 2. Tokens

Declarative JSON themes compile into Qt palette/QSS and WebEngine CSS. Token groups cover canvas/sidebar/raised/overlay surfaces; primary/secondary/disabled/link text; subtle/strong borders and focus; running/waiting/success/warning/danger states; user/tool/reasoning messages; diff states; terminal colors; spacing, radii, and overlay shadow. Safety color pairs have enforced minimum contrast.

The System theme is a live preference rather than a one-time startup guess. Snack follows Qt's current platform color scheme, reapplies the built-in light or dark token set when that scheme changes, and stops following immediately when the user explicitly selects Light or Dark. The View menu keeps these three modes mutually exclusive and persists the selected mode.

## 3. Typography

Default to installed system UI and monospace fonts. Users select UI, chat, and code/terminal families separately. Global zoom is the base scale; code and terminal sizes are additional preferences. Design uses weights 400 and 500 and avoids oversized bold text.

## 4. Components

- `ConversationRow`: title, directory summary, state shape, and time; secondary actions appear on hover.
- `SessionHeader`: compact selectors, not oversized pills.
- `MessageBlock`: agent content is unboxed; user content has a quiet surface distinction.
- `ToolCard`: one boundary, no nested cards, state and duration on one line.
- `ApprovalCard`: clear risk, target, and decision zones; deny remains visually available.
- `Composer`: stable writing surface, removable attachment rows, one high-emphasis send action.
- `DiffView`: restrained color areas with line numbers and symbols.
- `DockPanel`: quiet title bar and layout-defined boundaries instead of heavy shadows.

## 5. Motion

Use only initial reveal, streaming caret, tool-state transitions, panel expansion, and waiting indicators. No looping decoration. Honor reduced motion. Follow streaming only while the user remains at the bottom.

## 6. Theme schema

Themes contain `schemaVersion`, `name`, `author`, `baseMode`, `colors`, `typography`, `spacing`, and `radii`. URLs, files, icons, QSS, CSS, and JavaScript are forbidden. Missing fields inherit the base theme; invalid types or ranges reject import.

## 7. Mockup set

`design/mockups/` covers default chat, tool/approval states, diff/task workbench, onboarding, new conversation, settings, and backup. Export 1920×1080 PNGs to `design/exports/`, using light as the primary board and a dark key-chat variant.

## 8. Qt mapping

Use Widgets for shell, lists, menus, cards, and settings; one WebEngine document per visible chat; a native custom item view for diffs; and a native terminal widget. Redesign any effect that requires one WebView per message, arbitrary CSS, or untestable transparent windows.
