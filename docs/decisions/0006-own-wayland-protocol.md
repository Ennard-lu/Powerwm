# ADR-0006: 自研 Wayland wire protocol + 协程协议引擎

- 状态：Accepted
- 日期：2026-11-24
- 决策者：powerwm 维护者
- 相关：[architecture.md](../architecture.md)、[roadmap.md](../roadmap.md)、[ADR-0002](0002-cxx26-toolchain.md)

## 背景

Wayland 合成器通常直接使用 `libwayland-server`。它提供：

- `wl_display` / `wl_resource` / 全局对象管理；
- wire 消息编解码、对象 ID、版本协商；
- `wl_event_loop`（基于 epoll）与 C 回调；
- `wayland-scanner` 生成的协议胶水代码；
- fd 传递与客户端生命周期。

但 libwayland 是 C API，回调驱动，状态分散在全局对象中；与项目的无异常、RAII、协程、错误码模型存在摩擦。powerwm 希望完全掌控协议层与异步生命周期。

## 决策

1. **不链接、不包含任何 `libwayland-*`**。
2. 自研 wire protocol：
   - 消息头（object id / opcode / size）、参数编解码、版本；
   - `SCM_RIGHTS` fd 传递；
   - 部分读写、缓冲区边界、错误消息。
3. 自研对象模型：
   - 客户端对象表、服务端 ID 分配、接口/版本；
   - 销毁语义、迟到事件防护、protocol error。
4. 自研协议描述生成：
   - 解析 wayland-protocols XML；
   - 生成 C++26 描述符/分发表；
   - 不使用 `wayland-scanner`。
5. 自研事件循环与协程：
   - `pwm::event_loop` 基于 epoll；
   - `pwm::event::task<T>` 惰性启动、错误码传播、可取消；
   - 每个客户端一个 `client_session` 协程。
6. **测试策略**：自研 wire 测试客户端 + 真实外部客户端（foot、GTK、Qt、wayland-info）黑盒验证。

## 后果

### 正面

- 完全掌控协议状态、生命周期、错误传播与调度。
- 协程把“读消息 → 等待缓冲 → 发送事件”写成线性代码，避免回调地狱。
- 无异常模型统一：协议错误、取消、I/O 失败都是 `result<T>`。
- 不受 libwayland ABI/版本约束，可为性能与控制流做专门优化。

### 负面 / 代价

- **工作量显著增加**：协议层从“使用库”变成“实现库 + 使用库”。
- 兼容性风险高：任何 wire 细节偏差都可能导致客户端无法连接或事件丢失。
- 需要自研 XML 生成器、测试客户端、fuzz 基础设施。
- 调试工具（`WAYLAND_DEBUG` 等）不能直接复用，需要自建日志/抓包。
- 协议层成为最大的安全攻击面，必须持续 fuzz 与审计。

### 风险与缓解

| 风险 | 缓解 |
| --- | --- |
| wire 不兼容 | 以真实客户端为黑盒规范；逐项对照 wayland.xml；fuzz |
| fd 泄漏 | `SCM_RIGHTS` 压力测试；fd 计数断言 |
| 对象生命周期错误 | 对象表不变量、销毁后事件丢弃、ASan |
| 协程取消/重入 | 取消压力测试、跨线程恢复断言 |
| 生成器缺陷 | 生成结果与 XML 对照；生成器自身测试 |

## 备选方案

| 方案 | 优点 | 缺点 | 为何未选 |
| --- | --- | --- | --- |
| libwayland-server | 成熟、兼容性好、开发快 | C 回调、全局状态、与无异常/协程模型冲突 | 与项目工程规则冲突 |
| wlroots | 更高层封装 | 仍依赖 libwayland，抽象约束强 | 不满足自研协议目标 |
| Rust smithay | 现代、安全 | 语言不符 | 技术栈不符 |
| libwayland + C++ 包装 | 折中 | 仍受 C 生命周期/回调限制 | 无法达到协程化目标 |

## 重新审视条件

- 自研协议层长期无法覆盖日常客户端，且维护成本显著超过收益。
- 关键协议（输入法、屏幕共享）在自研栈上无法可靠实现。
- 项目目标转为“快速交付桌面”，不再要求自研协议。
