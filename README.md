# Snack

[简体中文](README.zh-CN.md)

Snack (temporary display name: 零食) is a local-first desktop workbench for Codex CLI and Claude Code CLI. It aims to provide a calm, conversation-first GUI without replacing the agents' native authentication, session storage, safety controls, or extension ecosystems.

## Status

M6 is complete for local Windows automated acceptance. Snack now provides production Codex and Claude adapters behind the common Agent interface, including Claude session resume, bounded streaming, event mapping, questions, an authenticated local MCP permission bridge, image attachments, and next-turn model/effort/access changes through documented process restart and `--resume`. Default tests remain model-free; cross-platform and live-model validation are still tracked separately.

## Product constraints

- One conversation is bound to one local working directory and one agent.
- Codex and Claude sessions never switch agent families after creation.
- Agent integration uses official structured protocols only; terminal-screen scraping is out of scope.
- Authentication and credentials remain managed by the installed CLIs.
- All application data is local. Snack does not collect telemetry or upload crash reports.
- Windows 10/11, Debian 13, and Ubuntu 22.04 LTS are guaranteed targets. macOS follows the support range of the bundled Qt version.

## Documentation

- [Product requirements](docs/en/product-requirements.md)
- [UX specification](docs/en/ux-specification.md)
- [Protocol capability matrix](docs/en/protocol-capability-matrix.md)
- [System architecture](docs/en/architecture.md)
- [Data and event model](docs/en/data-and-event-model.md)
- [Security and permissions](docs/en/security-and-permissions.md)
- [Testing strategy](docs/en/testing-strategy.md)
- [Build and release](docs/en/build-and-release.md)
- [Development plan](docs/en/development-plan.md)
- [Visual design system](docs/en/design-system.md)
- [Developer getting started](docs/en/getting-started.md)
- [Claude protocol spike](docs/en/claude-protocol-spike.md)
- [Claude adapter](docs/en/claude-adapter.md)

## Source and license

Repository: https://github.com/www666www6661/snack

Snack is licensed under the [MIT License](LICENSE). Codex and Claude are products and trademarks of their respective owners. This project is not an official OpenAI or Anthropic product.
