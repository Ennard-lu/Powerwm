# 架构决策记录（ADR）

ADR 记录**重要的、难以回退的**技术选择。它不是教程，而是为了回答未来的三个问题：

1. 当时我们面对什么问题？
2. 为什么选了这个方案，而不是其他方案？
3. 这个选择带来了什么后果，什么情况下应该重新考虑？

## 索引

| 编号 | 标题 | 状态 | 日期 |
| --- | --- | --- | --- |
| [0001](0001-native-compositor.md) | 原生 DRM/KMS 合成器，不基于 wlroots | Accepted（协议部分被 0006 替代） | 2026-11-24 |
| [0002](0002-cxx26-toolchain.md) | C++26 + GCC 16 + CMake/Ninja | Accepted（异常部分被 0008 替代） | 2026-11-24 |
| [0003](0003-renderer-backend.md) | 渲染器先 GLES3/EGL，Vulkan 后续 | Superseded by 0007 | 2026-11-24 |
| [0004](0004-config-and-ipc.md) | TOML 配置 + JSON Lines IPC | Accepted | 2026-11-24 |
| [0005](0005-license.md) | 许可证选择（待定） | Superseded by 0010 | 2026-11-24 |
| [0006](0006-own-wayland-protocol.md) | 自研 Wayland wire protocol + 协程协议引擎 | Accepted | 2026-11-24 |
| [0007](0007-vulkan-vulkanpp.md) | Vulkan + 内部 vulkanpp 封装 | Accepted | 2026-11-24 |
| [0008](0008-no-exceptions.md) | 禁用异常，使用错误码 | Accepted | 2026-11-24 |
| [0009](0009-c-api-wrapping.md) | C API 必须 create/destroy + RAII 封装 | Accepted | 2026-11-24 |
| [0010](0010-apache-2-0.md) | 采用 Apache License 2.0 | Accepted | 2026-11-24 |
| [0011](0011-namespace-and-integers.md) | 命名空间 pwm 与定宽整数别名 | Accepted | 2026-11-24 |
| [0012](0012-dependent-resource-creation.md) | 依赖资源由宿主创建 | Accepted | 2026-11-24 |

## 状态定义

- `Proposed`：讨论中，尚未实施。
- `Accepted`：已采纳，代码与文档应遵循。
- `Deprecated`：不再推荐，但尚未被替代。
- `Superseded by ADR-XXXX`：已被新决策替代，保留原文。
- `Rejected`：曾被考虑但未采纳，记录原因以免重复讨论。

## 模板

```markdown
# ADR-XXXX: 标题

- 状态：Proposed | Accepted | Deprecated | Superseded by ADR-XXXX | Rejected
- 日期：YYYY-MM-DD
- 决策者：
- 相关：

## 背景

我们面对什么问题？有哪些约束（硬件、时间、团队、生态）？

## 决策

我们选择了什么？用可执行的语言描述边界与不变量。

## 后果

### 正面

### 负面 / 代价

### 风险与缓解

## 备选方案

| 方案 | 优点 | 缺点 | 为何未选 |
| --- | --- | --- | --- |

## 重新审视条件

出现什么信号时，应该重新讨论这个决策？
```

## 写作规则

- 一个 ADR 只做一个决策；不要把“选语言”和“选构建系统”混在一起。
- 不修改已接受 ADR 的结论；如需变更，新增 ADR 并标记替代关系。
- 后果必须包含负面项；没有代价的决策通常意味着分析不充分。
- 性能相关决策必须写明测量条件与基线。
