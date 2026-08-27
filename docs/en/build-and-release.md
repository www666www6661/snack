# Build and release

## 1. Toolchains

| Platform | Compiler | Artifact |
|---|---|---|
| Windows 10/11 | `clang-cl`, MSVC ABI, Windows SDK | x64 MSI |
| Ubuntu 22.04 / Debian 13 | Clang | amd64/arm64 `.deb` |
| macOS in Qt support range | Apple Clang | Intel/Apple Silicon `.app.zip` |

Minimum Qt is 6.8 LTS. Release packages pin a validated Qt 6.8+ build. Use C++20, CMake Presets, and preferably Ninja.

## 2. Qt modules

Expected modules: Core, Gui, Widgets, Network, Sql, Concurrent, WebEngineWidgets, WebChannel, LinguistTools, and Test. Every new module requires a documented purpose and packaging impact.

## 3. Dependencies

Pin third-party versions or commits with `FetchContent` in a central `cmake/Dependencies.cmake`, disable upstream examples/tests, cache sources for release builds, and generate notices. Qt comes from an SDK rather than being built by the project.

The M3 renderer vendors release assets for markdown-it 14.1.0, DOMPurify 3.2.6, KaTeX 0.16.22, and Mermaid 11.9.0 under `src/resources/renderer/vendor`. Their upstream license texts are stored beside the assets and must be copied into packaged third-party notices. These files are runtime qrc inputs; Node.js and a CDN are not required. CMake enables this renderer only when both Qt WebChannel and WebEngineWidgets are present and otherwise reports the plain-text fallback at configure time.

## 4. Presets

Provide Windows Clang debug/release, Linux Clang debug/release, Linux ARM64 release, macOS Intel/ARM release, and Linux coverage presets. Keep machine-specific paths in untracked `CMakeUserPresets.json`.

## 5. GitHub Actions

CI runs formatting, compilation, Qt Test, documentation links, and coverage only. It does not publish final installers or call live agents. Linux produces the primary coverage report; Windows and macOS verify platform builds/tests. Model credentials never enter CI.

## 6. Packaging

- Windows: x64 MSI with per-user install, upgrade/uninstall, Start menu, optional desktop icon, optional user PATH, stable upgrade identity, and retained user data by default.
- Linux: amd64/arm64 `.deb` only. Build against a controlled compatibility baseline and deploy distributable Qt/WebEngine runtime assets for Ubuntu 22.04 and Debian 13.
- macOS: unsigned and unnotarized Intel and Apple Silicon `.app` archives. No DMG/PKG. Document Gatekeeper behavior and never automate quarantine removal.

## 7. Licensing

Snack is MIT. Distribute Qt dynamically under the selected LGPL compliance plan, retain notices and replacement information, and avoid static Qt linking by default. Include third-party notices in packages and the About page. Perform a release-specific license review.

## 8. Versioning and release

Use SemVer from `0.1.0` and tags such as `v0.1.0`. Database and backup schemas version independently. A manual release freezes dependencies/CLI matrix/Qt, passes all tests and packaging smoke tests, generates notices/dependency inventory/hashes/notes, then uploads artifacts to a manually created GitHub Release. In-app update check only opens that page.

## 9. Developer prerequisites

Clang, CMake, Ninja, Qt 6.8+ with WebEngine, and Git. At least one local CLI is useful for integration testing, but default tests use protocol simulators. Windows also requires Visual Studio Build Tools and the Windows SDK.

The verified local Windows WebEngine profile uses official Qt 6.8.3 `msvc2022_64` with MSVC Build Tools and Ninja. The official Qt LLVM-MinGW package does not contain WebEngine in this setup and therefore verifies the fallback path only. This local result makes no claim for Ubuntu, Debian, or macOS until their own toolchains run.
