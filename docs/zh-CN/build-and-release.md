# 构建与发布

## 1. 工具链

| 平台 | 编译器 | 目标 |
|---|---|---|
| Windows 10/11 | `clang-cl` + MSVC ABI + Windows SDK | x64 MSI |
| Ubuntu 22.04 / Debian 13 | Clang | amd64/arm64 `.deb` |
| macOS（Qt 官方范围） | Apple Clang | Intel/Apple Silicon `.app.zip` |

最低 Qt 为 6.8 LTS。正式包使用锁定且验证过的 Qt 6.8+ 版本。C++ 标准为 C++20。生成器优先 Ninja，构建系统为 CMake Presets。

## 2. Qt 模块

必需模块预估：Core、Gui、Widgets、Network、Sql、Concurrent、WebEngineWidgets、WebChannel、LinguistTools、Test。若后续增加模块，需说明用途与打包影响。

## 3. 依赖

第三方库通过 `FetchContent` 固定版本或提交。顶层 `cmake/Dependencies.cmake` 集中声明，关闭第三方示例和测试。正式构建使用缓存并生成 third-party notices。Qt由 SDK提供，不从源码随项目构建。

M3 Renderer 在 `src/resources/renderer/vendor` 中离线保存 markdown-it 14.1.0、DOMPurify 3.2.6、KaTeX 0.16.22 和 Mermaid 11.9.0 的发布资产。上游许可证文本与资源放在一起，打包时必须进入第三方声明。它们是 qrc 运行资源，不要求 Node.js 或 CDN。只有 Qt WebChannel 与 WebEngineWidgets 同时存在时 CMake 才启用 Renderer；否则配置阶段明确报告纯文本降级。

## 4. 建议 Presets

- `windows-clang-debug`, `windows-clang-release`
- `linux-clang-debug`, `linux-clang-release`
- `linux-arm64-release`
- `macos-intel-release`, `macos-arm64-release`
- `coverage-linux`

用户私有路径放在不提交的 `CMakeUserPresets.json`。

## 5. GitHub Actions

CI 只执行格式、构建、Qt Test、文档链接和覆盖率，不发布正式安装包，也不调用真实 Agent。建议 Linux Runner产生主覆盖率报告，Windows/macOS验证平台构建与测试。机密模型凭据不进入 CI。

## 6. 平台打包

### Windows

只发布 x64 MSI。支持当前用户安装、标准升级/卸载、开始菜单、可选桌面快捷方式和可选用户 PATH。稳定维护 ProductCode/UpgradeCode。卸载默认保留用户数据。

### Linux

只发布 amd64/arm64 `.deb`。为兼容 Ubuntu 22.04 与 Debian 13，发布构建使用受控兼容基线并部署许可允许的 Qt/WebEngine 运行资源。包安装 `snack` 命令、Desktop 文件、图标和许可证。

### macOS

发布未签名、未公证的 Intel 与 Apple Silicon `.app` 压缩包，不提供 DMG/PKG。发布说明明确 Gatekeeper限制与透明的手动打开方式，不自动移除 quarantine。

## 7. Qt 与第三方许可

主项目 MIT。Qt 优先按 LGPL动态链接合规分发，保留许可证和替换库所需信息；首版不默认静态链接 Qt。安装包和关于页提供第三方许可证清单。正式发布前进行独立许可核对。

## 8. 版本与发布流程

SemVer，从 `0.1.0` 开始，标签为 `v0.1.0`。数据库和备份 Schema独立版本化。手动发布清单：

1. 冻结依赖、CLI 兼容矩阵与 Qt版本。
2. 三平台测试、覆盖率和文档通过。
3. 在目标系统执行安装/升级/卸载与冒烟测试。
4. 生成许可证、SBOM/依赖清单、哈希与更新说明。
5. 手动创建 GitHub Release 并上传产物。

应用内“检查更新”只比较 GitHub Release 并打开网页，不下载或安装。

## 9. 本地开发前置

开发者需要 Clang、CMake、Ninja、Qt 6.8+（含 WebEngine）和 Git。本机还需至少一个可测试的 Codex/Claude CLI，但默认测试使用模拟协议。Windows 需 Visual Studio Build Tools 与 Windows SDK。

当前已验证的 Windows WebEngine 配置使用 Qt 官方 6.8.3 `msvc2022_64`、MSVC Build Tools 与 Ninja。此环境中的 Qt 官方 LLVM-MinGW 包不包含 WebEngine，因此只验证纯文本降级路径。该本机结果不代表 Ubuntu、Debian 或 macOS 已经通过；它们必须分别运行自己的工具链。
