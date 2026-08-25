# 视觉设计系统

## 1. 方向

默认风格接近 ChatGPT 桌面应用的安静、留白和阅读节奏，但不是网页套壳。Qt 原生菜单、窗口、停靠与键盘行为保留桌面感。临时品牌只使用中性占位图标。

## 2. 设计 Token

主题是声明式 JSON，内部转换为 Qt Palette/QSS和 WebEngine CSS。Token 分为：

- `surface.canvas`, `surface.sidebar`, `surface.raised`, `surface.overlay`
- `text.primary`, `text.secondary`, `text.disabled`, `text.link`
- `border.subtle`, `border.strong`, `focus.ring`
- `state.running`, `state.waiting`, `state.success`, `state.warning`, `state.danger`
- `message.user`, `message.tool`, `message.reasoning`
- `diff.added`, `diff.removed`, `diff.modified`, `diff.conflict`
- `terminal.background`, `terminal.foreground`, ANSI 16色
- `space.1` 至 `space.8`, `radius.small/medium/large`, `shadow.overlay`

安全相关前景/背景组合必须满足内部最低对比规则，主题不能把危险与普通操作渲染成不可区分。

## 3. 排版

默认跟随系统字体；用户分别选择 UI、聊天和等宽字体。建议层级：窗口标题、会话标题、正文、辅助文字、代码。全局缩放为基础倍率，代码/终端字号为附加设置。设计稿只使用 400/500 字重，避免大面积粗体。

## 4. 组件

- `ConversationRow`：标题、目录摘要、状态形状、时间；悬停才显示次要操作。
- `SessionHeader`：紧凑下拉控件，不使用大号胶囊标签。
- `MessageBlock`：Agent 消息无厚重卡片；用户消息使用轻微表面差。
- `ToolCard`：单层边界，不嵌套卡片；状态与耗时在同一行。
- `ApprovalCard`：风险区域、目标、决策分区清晰；拒绝不是视觉弱项。
- `Composer`：输入面大而稳定，附件作为可移除行，主发送操作唯一高强调。
- `DiffView`：颜色面积克制，行号和符号共同表达增删。
- `DockPanel`：标题栏轻量，内容边界由布局而非大量阴影表达。

## 5. 动效

只保留：初次加载淡入/位移、流式光标、工具状态过渡、面板展开和等待指示。无循环装饰动画；支持系统减少动态效果。滚动跟随仅在用户位于底部时启用。

## 6. 自定义主题 Schema

主题文件包含 `schemaVersion`, `name`, `author`, `baseMode`, `colors`, `typography`, `spacing`, `radii`。不允许 URL、文件、图标、QSS、CSS或 JS。字段缺失继承内置主题；类型或范围错误阻止导入。

## 7. 高保真稿清单

`design/mockups/` 中的设计板覆盖：默认聊天、工具与权限状态、Diff/任务工作台、首次引导、新建会话、设置与备份。导出的 1920×1080 PNG放在 `design/exports/`。浅色为主设计稿，关键聊天界面同时提供深色版本。

## 8. Qt 映射

主壳、列表、菜单、卡片和设置使用 Widgets；聊天消息使用单个 WebEngine 文档；Diff优先原生自绘 Item View；终端使用原生渲染 Widget。任何设计效果若要求每条消息独立 WebView、任意 CSS 或不可测试的透明窗口，应调整设计而不是突破架构边界。
