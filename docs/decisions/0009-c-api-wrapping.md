# ADR-0009: C API 必须 create/destroy + RAII 封装

- 状态：Accepted
- 日期：2026-11-24
- 决策者：powerwm 维护者
- 相关：[coding-style.md](../coding-style.md)、[ADR-0008](0008-no-exceptions.md)、[ADR-0007](0007-vulkan-vulkanpp.md)

## 背景

powerwm 会接触大量 C API：

- POSIX：`open`、`close`、`mmap`、`epoll_*`、`socket`、`timerfd_*`、`signalfd`、`memfd_create`；
- DRM/KMS：`drmMode*`、`drmIoctl`、`drmSetMaster`；
- libinput / libudev / libseat；
- Vulkan：`vkCreate*` / `vkDestroy*` / `vkAllocate*` / `vkFree*`；
- 未来的 PipeWire、XWayland 相关接口。

C API 的错误处理、生命周期与所有权不统一，容易产生 fd 泄漏、双重释放、悬垂句柄。项目已有 `pwm::raii` 模板，决定把它作为唯一入口。

## 决策

1. **任何 C API 资源必须封装**，业务代码不得直接调用裸 C API。
2. 封装类型约定：
   - 申请：`static auto create(...) -> result<raii<T, Args...>>`（或 `create_*`）；
   - 销毁：`void destroy(Args...) const noexcept`；
   - 有效性：`bool valid() const noexcept`。
3. 业务代码只持有 `raii<T, Args...>` 或专用 RAII 类型；裸句柄只存在于封装实现内部。
4. `destroy` 必须**幂等**、`noexcept`、不返回错误；析构只记录并尽力清理。
5. C 头文件（`<drm/*.h>`、`<vulkan/*.h>`、`<linux/*.h>`、POSIX 头）只允许出现在封装层：
   - `src/base/`
   - `src/platform/`
   - `src/render/vulkanpp/`
6. C 常量/枚举也必须在封装层转换为 `pwm` 类型，禁止把宏泄漏到业务层。
7. `raii` 不可拷贝；移动转移所有权；`release()` 只用于明确移交所有权的边界。
8. 每个封装至少测试：创建失败、正常销毁、重复销毁、移动后销毁。
9. CI 运行 `tools/check_policy.sh`，扫描裸 C API 与 C 头文件泄漏。
10. 依赖其他对象的资源由宿主创建；详见 [ADR-0012](0012-dependent-resource-creation.md)。

## 后果

### 正面

- 资源生命周期集中、可审计；fd/GPU 对象泄漏显著减少。
- 与无异常模型一致：`create` 返回 `result`，销毁不失败。
- 业务层代码更接近 C++ 值语义，可在 headless 测试中替换。
- 为未来替换后端（Vulkan/DRM/输入）保留边界。

### 负面 / 代价

- 封装层代码量增加，每个资源都需要一个类型。
- `raii` 模板对“创建参数”和“销毁参数”有约束，复杂资源需要专门类型。
- 某些 C API 有“先查询再分配”的两阶段模式，需要额外封装。
- 过度封装可能隐藏性能细节；需要在封装层保留零开销路径。

### 风险与缓解

| 风险 | 缓解 |
| --- | --- |
| 封装泄漏裸句柄 | 代码审查 + 策略脚本 |
| 析构顺序错误 | 明确成员顺序；依赖注入；测试 |
| `destroy` 重复调用 | 幂等实现 + 测试 |
| 封装层性能开销 | 内联、`noexcept`、避免额外分配 |

## 备选方案

| 方案 | 优点 | 缺点 | 为何未选 |
| --- | --- | --- | --- |
| 直接使用 C API | 简单、无样板 | 生命周期错误多、不可组合 | 不符合项目规则 |
| 只用 `unique_ptr` + deleter | 标准 | 无法表达多参数销毁/有效性 | 表达力不足 |
| 第三方 RAII 库 | 少写代码 | 依赖、风格不统一 | 现有 `raii` 已够用 |
| C++ 包装库（如 Vulkan-Hpp） | 官方/成熟 | 仍需二次封装；异常模型不一致 | 作为底层可用，但不替代规则 |

## 重新审视条件

- `raii` 模板无法表达某类资源（例如需要异步销毁的 GPU 对象），需要扩展或专用类型。
- 封装层成为性能瓶颈，且测量证明需要零封装路径。
