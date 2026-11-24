# ADR-0004: TOML 配置 + JSON Lines IPC

- 状态：Accepted
- 日期：2026-11-24
- 决策者：powerwm 维护者
- 相关：[architecture.md](../architecture.md)、[coding-style.md](../coding-style.md)

## 背景

powerwm 需要：

- 人类可读、可版本控制、错误定位清晰的配置。
- 键位、窗口规则、布局、输出、主题的声明式描述。
- 运行时查询/控制（脚本、状态栏、快捷键、调试）。
- 热重载，且失败时不破坏当前会话。
- 不引入重量级运行时或破坏合成器稳定性。

候选配置格式：TOML、YAML、INI、Lua、自定义 DSL。候选 IPC：D-Bus、Unix socket + JSON、Wayland 私有协议、gRPC。

## 决策

### 配置

1. 使用 **TOML** 作为唯一用户配置格式。
2. 解析器优先使用成熟库（如 `toml++`，通过 `FetchContent` 固定版本），并封装在 `config/` 内。
3. 配置分层：内置默认值 → 系统配置 → 用户配置 → 运行时覆盖。
4. 热重载是原子的：解析 + 校验成功后才替换；失败保留旧配置，并报告文件/行/列/期望值。
5. 1.0 前**不嵌入脚本运行时**；复杂逻辑通过 IPC + 外部脚本完成。

### IPC

1. 使用 **Unix domain socket**（默认 `$XDG_RUNTIME_DIR/powerwm-$WAYLAND_DISPLAY.sock`，权限 `0600`）。
2. 协议为 **JSON Lines**：一行一个请求/响应/事件。
3. 请求包含 `id`、`command`、`args`；响应包含 `id`、`ok`、`result` 或 `error`。
4. 只暴露白名单动作；不接受任意代码执行。
5. 事件订阅用于状态栏、调试工具与测试。

示例：

```json
{"id":1,"command":"get_outputs"}
{"id":1,"ok":true,"result":[{"name":"DP-1","width":2560,"height":1440,"scale":1.25}]}
{"id":2,"command":"focus_workspace","args":{"name":"2"}}
{"event":"workspace_changed","data":{"name":"2","output":"DP-1"}}
```

## 后果

### 正面

- TOML 语法简单，适合键位/规则等嵌套但有限的结构；错误定位容易。
- 配置即数据，便于反射映射与测试。
- JSON Lines 易被 shell/Python/Rust/Go 脚本消费，无需代码生成。
- Unix socket 权限模型简单，天然限定同用户。
- 不嵌入脚本运行时，减少攻击面与崩溃风险。

### 负面 / 代价

- TOML 不适合表达复杂逻辑；用户可能需要外部脚本。
- JSON Lines 无内置 schema/版本协商，需要自行设计兼容策略。
- 与 D-Bus 生态（portal、通知）仍需额外适配。
- 热重载的“原子替换”需要仔细处理跨模块状态。

### 风险与缓解

| 风险 | 缓解 |
| --- | --- |
| 配置格式破坏性变更 | `config_version` 字段 + 迁移提示；0.x 阶段写入 CHANGELOG |
| IPC 被滥用 | 权限 0600、动作白名单、参数校验、速率限制 |
| 热重载半更新 | 先构建完整新状态，再单点交换；失败回滚 |
| 解析器依赖过重 | 封装在 `config/`，限制头文件暴露；固定版本 |

## 备选方案

| 方案 | 优点 | 缺点 | 为何未选 |
| --- | --- | --- | --- |
| YAML | 表达力强、生态广 | 语法陷阱多、错误定位差 | 不适合作为唯一配置格式 |
| Lua | 灵活、可编程 | 嵌入运行时、安全与稳定性成本 | 1.0 前不引入脚本 |
| INI | 简单 | 嵌套/数组表达弱 | 不足以描述规则与主题 |
| D-Bus | 标准、工具多 | 样板多、依赖重、调试复杂 | 核心控制用更轻的 socket |
| gRPC | 强类型、跨语言 | 依赖重、过度设计 | 规模不匹配 |

## 重新审视条件

- 用户配置复杂度证明 TOML 无法表达，且外部脚本方案体验不可接受。
- IPC 需要跨用户/系统级控制，Unix socket 权限模型不再适用。
- D-Bus 成为生态集成的硬性要求（例如 portal 深度集成），需要补充而非替换。
