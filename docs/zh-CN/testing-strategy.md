# 测试策略

## 1. 门槛

- 测试框架统一使用 Qt Test。
- 第一方代码总体行覆盖率至少 80%。
- CI 必须通过编译、全部测试、覆盖率、`clang-format` 和基础编译器警告检查。
- Qt、第三方库、MOC/UIC/RCC、翻译和安装器生成代码不计入覆盖率。
- 测试与功能同一里程碑交付；缺少测试的功能不视为完成。

## 2. 测试金字塔

### 单元测试

事件映射、JSON-RPC关联、能力协商、状态机、路径规范化、权限规则、主题 Schema、Diff hunk、内容哈希、保留策略、版本比较和备份清单。

### 组件测试

`SessionController`、`EventStore`、`PermissionManager`、`WorkspaceWriteLeaseManager`、`DiffManager`、`BackupService`、`TerminalManager` 与单实例 IPC。使用临时目录、临时 SQLite、假时钟和假系统服务。

### 协议契约测试

- Codex：从每个支持版本生成 JSON Schema，并保存创建、恢复、Turn、steer、interrupt、审批、Diff、推理、错误和断连 fixture。
- Claude：保存 `system/init`、assistant、stream_event、tool_use/result、result、重试、能力列表、恢复和审批桥 fixture。
- 未知字段必须被忽略并保留在 raw payload；缺失可选字段不得崩溃。
- CI 不调用真实模型。

### GUI 测试

首次引导、新建会话、流式消息、工具卡、权限卡、问题卡、队列、Diff、布局、独立窗口、主题、国际化、缩放与崩溃重建。业务状态优先测试 ViewModel；关键 Widget 再用 Qt Test事件验证。

### 平台集成测试

Windows ConPTY/Credential Manager/MSI，Linux PTY/Secret Service/.deb/Wayland/X11，macOS PTY/Keychain/未签名 app提示。托盘、通知、文件监听、高 DPI和 WebEngine 每个平台验证。

## 3. 高风险测试表

| 区域 | 必测路径 |
|---|---|
| 权限 | once/session/always/deny、重放、超时、断连、组织策略覆盖 |
| 会话 | 关闭终态、迟到连接/能力/身份/事件信号、重连恢复 |
| Diff | 新增/删除/重命名/二进制、hunk 拒绝、外部冲突、脏基线 |
| 写租约 | 抢占、释放、转移、崩溃残留、符号链接别名 |
| 存储 | WAL恢复、磁盘满、Blob 丢失、引用计数、迁移失败 |
| 备份 | 普通/加密、错密码、篡改、路径穿越、回滚、跨平台映射 |
| 协议 | 半帧、LF/CRLF 上限、非法 JSON、重复 ID、未知事件保留/上下文/隐私、请求超时、迟到响应、退出期间重连、terminate/kill 降级、输出洪泛、进程退出 |
| WebEngine | 恶意 Markdown、远程资源阻止、渲染进程崩溃、链接校验 |
| 终端 | Unicode、ANSI、resize、进程退出、标签拖出、真正退出重建 |

## 4. 覆盖率实现

使用 LLVM source-based coverage：测试构建添加 coverage instrumentation，`llvm-profdata` 合并，`llvm-cov` 生成报告。三平台报告可分别发布；Linux 作为主要覆盖率门禁，平台专属模块由对应 Runner补充。新增排除项必须在评审中说明。

## 5. Fixture 与敏感数据

录制 fixture 前替换用户路径、仓库名、令牌和源码。fixture Schema和示例必须可公开。禁止提交真实 `~/.codex`、`~/.claude`、认证文件或用户会话。

## 6. 每个需求的完成定义

1. 实现与双语文档一致。
2. 正常、失败、取消和恢复路径有测试。
3. 覆盖率不低于 80%。
4. 没有新增格式错误或基础警告。
5. 能力缺失时的禁用/降级文案已测试。
6. 相关协议 fixture 和迁移已版本化。
