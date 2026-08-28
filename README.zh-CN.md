# 零食 / Snack

[English](README.md)

Snack（临时显示名称：零食）是一款面向 Codex CLI 与 Claude Code CLI 的本地优先桌面工作台。它希望在不替代 Agent 原生认证、会话存储、安全控制和扩展生态的前提下，提供安静、以对话为中心的 GUI。

## 当前状态

M6 已完成本机 Windows 自动化验收。Snack 现已在公共 Agent 接口后提供正式 Codex 与 Claude 适配器，包括 Claude Session 恢复、有界流传输、事件映射、提问、带本地认证的 MCP 权限桥、图片附件，以及通过公开的进程重启与 `--resume` 实现的下一轮模型/推理强度/访问层级调整。默认测试仍不调用模型；跨平台与真实模型验证继续单独跟踪。

## 产品约束

- 每个会话绑定一个本地工作目录和一种 Agent。
- 会话创建后不能在 Codex 与 Claude 之间切换。
- Agent 仅通过官方结构化协议接入，不解析终端屏幕文本。
- 登录和凭据继续由本机 CLI 管理。
- 应用数据全部保存在本地；Snack 不收集遥测，也不上传崩溃报告。
- 保证支持 Windows 10/11、Debian 13 和 Ubuntu 22.04 LTS；macOS 跟随所使用 Qt 版本的官方支持范围。

## 文档

- [产品需求](docs/zh-CN/product-requirements.md)
- [UX 与交互规格](docs/zh-CN/ux-specification.md)
- [协议能力矩阵](docs/zh-CN/protocol-capability-matrix.md)
- [系统架构](docs/zh-CN/architecture.md)
- [数据与事件模型](docs/zh-CN/data-and-event-model.md)
- [安全与权限](docs/zh-CN/security-and-permissions.md)
- [测试策略](docs/zh-CN/testing-strategy.md)
- [构建与发布](docs/zh-CN/build-and-release.md)
- [开发计划](docs/zh-CN/development-plan.md)
- [视觉设计系统](docs/zh-CN/design-system.md)
- [开发者入门](docs/zh-CN/getting-started.md)
- [Claude 协议技术验证](docs/zh-CN/claude-protocol-spike.md)

## 源码与许可证

仓库：https://github.com/www666www6661/snack

Snack 使用 [MIT License](LICENSE)。Codex 与 Claude 是其各自权利方的产品和商标。本项目不是 OpenAI 或 Anthropic 的官方产品。
