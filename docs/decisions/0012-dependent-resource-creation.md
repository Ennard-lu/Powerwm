# ADR-0012: Dependent resources are created by their owner

- 状态：Accepted
- 日期：2026-11-24
- 决策者：powerwm 维护者
- 相关：[ADR-0009](0009-c-api-wrapping.md)、[coding-style.md](../coding-style.md)

## 背景

早期 DRM 封装使用如下形式创建依赖 device 的资源：

```cpp
dumb_buffer::create(device, format);
framebuffer::create(device, buffer);
```

这种 API 把“创建逻辑”放在依赖类型上，但资源实际属于 device，并且销毁、映射、提交都需要 device 句柄。调用者很容易：

- 传入错误的 device；
- 在 device 销毁后继续使用资源；
- 在不同层级重复保存 device 引用；
- 让依赖类型的构造函数与宿主生命周期脱节。

## 决策

依赖其他对象的资源由**宿主对象**创建：

```cpp
device.create_dumb_buffer(format);
device.create_framebuffer(buffer);
device.create_property_blob(data);
```

规则：

1. 依赖类型（`dumb_buffer`、`framebuffer`、`property_blob`）的构造函数私有。
2. `device` 是 `friend`，只有宿主可以构造依赖资源。
3. 依赖资源保存创建时的宿主句柄，`destroy()`、`map()` 不再要求调用者重复传入 device。
4. 不再提供 `dependent::create(owner, ...)` 或 `dependent::from(owner, ...)`。
5. 宿主必须比依赖资源活得更久；该契约通过头文件英文 API 注释说明。
6. 宿主无关的资源（如 `atomic_request`）可以继续使用 `create()`。

## 后果

### 正面

- 所有权与生命周期边界清晰：谁创建、谁销毁、依赖谁。
- 调用点更短，无法传入错误的 device。
- 依赖类型的构造函数不能被绕过，资源始终携带正确的宿主句柄。
- 便于后续加入 GPU/DRM 资源审计与句柄计数。

### 负面 / 代价

- 依赖类型必须保存宿主句柄，增加少量内存。
- 宿主销毁后调用依赖资源的 `destroy()` 仍是未定义行为，只能靠 RAII 顺序与契约避免。
- `device` 需要成为依赖类型的 `friend`，耦合度略增。

### 风险与缓解

| 风险 | 缓解 |
| --- | --- |
| 宿主先于资源销毁 | RAII 声明顺序；API 注释；测试覆盖 |
| 忘记把新资源加入宿主 | code review；封装层测试 |
| 依赖类型被复制到其他宿主 | 构造函数私有；复制只复制句柄 |

## 备选方案

| 方案 | 优点 | 缺点 | 为何未选 |
| --- | --- | --- | --- |
| `dependent::create(owner, ...)` | 调用直观 | 所有权错位，易传错 owner | 正是要移除的形态 |
| Builder | 可扩展 | 样板多 | 规模不匹配 |
| 全局资源工厂 | 集中管理 | 全局状态、测试困难 | 与单线程 RAII 模型冲突 |
| 依赖类型保存 `device&` | 类型安全 | 引用生命周期难审计 | 保存句柄更简单 |

## 重新审视条件

- 依赖资源需要在多个 device 之间迁移。
- 异步销毁/延迟回收使“宿主必须更长寿”不再成立。
- 资源数量增长到需要集中式句柄表管理。
