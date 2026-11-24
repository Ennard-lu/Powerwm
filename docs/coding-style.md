# powerwm C++ 编码规范

> 适用范围：`src/`、`include/`、`tests/`、`tools/` 中的全部 C++ 代码。
> 规范的目标不是“看起来整齐”，而是**降低长期维护成本、减少并发与生命周期错误、让性能可预测**。

强制执行方式：

- `clang-format`：格式，CI 阻断。
- `clang-tidy`：静态分析，Phase 0 起逐步接入；C++26 反射文件暂时豁免。
- 代码审查：命名、分层、错误处理、测试与性能证据。
- CI：Debug/Release/ASan+UBSan 构建与测试。

---

## 1. 语言基线

| 项目 | 约定 |
| --- | --- |
| 标准 | C++26（`-std=c++26`） |
| 主编译器 | GCC 16+ |
| 次编译器 | Clang，仅在支持所用 C++26 特性时 |
| 构建 | CMake ≥ 3.25 + Ninja |
| 平台 | Linux（x86_64 优先，aarch64 逐步支持） |

### 允许并鼓励的特性

- `std::expected`、`std::optional`、`std::span`、`std::string_view`、`std::ranges`、`std::chrono`。
- 概念（concepts）与 `requires` 约束，替代 SFINAE。
- `constexpr` / `consteval` / `if constexpr`。
- `std::print` / `std::format`（仅限 CLI、日志格式化后端、测试；热路径见 §8）。
- C++26 反射（`std::meta`）：**只能通过 `base/meta.hpp` 使用**，并保留非反射回退路径。
- 指定初始化器（designated initializers），用于配置与描述结构体。

### 谨慎使用

| 特性 | 规则 |
| --- | --- |
| 异常 | **全项目禁用**（`-fno-exceptions`）；运行期错误用 `pwm::result<T>` / 错误码。禁止 `throw` / `try` / `catch`。 |
| RTTI / `dynamic_cast` | 热路径禁用；后端/测试中如确有必要需说明。 |
| 宏 | 只用于编译期开关、断言、日志分类；不得封装复杂逻辑。 |
| 模板元编程 | 以可读性为先；能用 `if constexpr` 就不用标签分发。 |
| C++26 模块 | Phase 2 起评估，先在 `base` 做试点；不得半模块半头文件混用同一单元。 |
| 反射 | 不得让核心逻辑“没有反射就不能编译”。 |
| 协程 | 只用于异步 I/O 与协议会话；不得用协程隐藏同步阻塞调用。 |
| Vulkan | 只通过 `vulkanpp` 使用；业务层不得直接出现 `Vk*` 类型或 `vk*` 调用。 |

### 禁止

- `using namespace` 出现在头文件或全局作用域。
- 裸 `new` / `delete`（容器与 `make_unique` 除外）。
- 拥有所有权的裸指针。
- `printf` / `std::cout` / `std::cerr` 作为运行期日志。
- 在头文件中定义非 `inline` 的全局变量。
- 隐式生命周期开始（`malloc` + 强转）除非封装并注释。
- 依赖未定义行为的“优化”（严格别名违规、有符号溢出等）。

---

## 2. 命名

统一使用 `lower_snake_case`，与现有原型保持一致。

| 实体 | 规则 | 示例 |
| --- | --- | --- |
| 命名空间 | `lower_snake` | `pwm`, `pwm::platform::drm` |
| 类型（class/struct/enum/concept） | `lower_snake` | `output`, `frame_context`, `renderer` |
| 函数 / 方法 | `lower_snake` | `commit()`, `begin_frame()` |
| 变量 / 参数 | `lower_snake` | `output_count`, `damage_region` |
| 私有数据成员 | `lower_snake` + `_` | `fd_`, `pending_state_` |
| 公开结构体字段 | `lower_snake`（无后缀） | `width`, `height`, `format` |
| 常量 / `constexpr` | `lower_snake` | `max_outputs`, `default_refresh_hz` |
| 枚举值 | `lower_snake` | `error_code::not_found` |
| 模板参数 | `T`, `Ts...` 或语义名 | `template <typename T>` |
| 宏 | `PWM_UPPER_SNAKE` | `PWM_ASSERT`, `PWM_LOG` |
| 文件名 | `lower_snake.hpp/.cpp` | `page_flip.cpp`, `scene_graph.hpp` |

补充规则：

- **名字必须自明**：函数名以动词开头（`open_device`, `commit_frame`），类型名用名词（`device`, `atomic_request`）。
- 禁止无语义名称：`from`、`process`、`handle`、`data`、`tmp`、`util`、`helper`、`misc`、`do_it`。
- 布尔值用 `is_`、`has_`、`can_` 前缀：`is_mapped`, `has_damage`, `can_scanout`。
- 避免无意义缩写；保留领域通用缩写：`id`、`fd`、`drm`、`gpu`、`crtc`、`edid`。
- 获取器不加 `get_`：`width()`、`outputs()`。
- 工厂函数用 `open_*` / `create_*`，并返回 `result<T>` 或 `std::optional<T>`；不要用 `from`。
- 类型名不用 `I` 前缀、不用 `Impl` 后缀暴露给上层（`pimpl` 内部可用）。
- 单位写入名字：`timeout_ms`、`width_px`、`scale_factor`。
- 所有权写入名字：`owned_*`、`borrowed_*` 仅在接口不明显时使用。

---

## 3. 文件与头文件

### 头文件顺序

```cpp
#pragma once

#include "pwm/render/renderer.hpp"

#include <pwm/base/result.hpp>
#include <pwm/scene/node.hpp>

#include <drm/drm.h>

#include <expected>
#include <span>
#include <string_view>
```

- 一律使用 `#pragma once`。
- 能前置声明就前置声明，减少头文件耦合；但不要为了前置声明牺牲可读性。
- 头文件必须自包含：单独包含即可编译。
- 不在头文件里写 `using namespace`、匿名命名空间、非 `inline` 定义。
- 导出符号最小化；默认内部链接/隐藏可见性，只有明确公共 API 才进入 `include/pwm/`。

### 实现文件

- 优先匿名命名空间放内部辅助函数。
- 不在 `.cpp` 里 `#include` 另一个 `.cpp`。
- 一个文件只承担一个主要职责；超过 ~800 行考虑拆分（生成代码除外）。

---

## 4. 格式

由根目录 [`.clang-format`](../.clang-format) 决定，关键点：

- 4 空格缩进，不使用 Tab。
- 行宽 100。
- 指针/引用靠左：`T*`、`const T&`。
- 左花括号不换行。
- 命名空间不缩进。
- 短函数不压成一行（`AllowShortFunctionsOnASingleLine: None`）。
- 提交前执行：

```bash
clang-format -i $(git ls-files '*.cpp' '*.hpp')
# 或
cmake --build --preset dev --target format
```

> 不要手工调整格式；有争议就改 `.clang-format`，不要在 PR 里局部对抗。

---

## 5. 类型与 API 设计

### 基本原则

- **值语义优先**：小对象按值传递/返回；大对象用 `const&` 或 `std::span`。
- **RAII 覆盖所有资源**：fd、mmap、DRM 对象、Wayland 协议对象、Vulkan 对象。
- **规则零/五**：优先让编译器生成；需要自定义析构时，显式处理拷贝/移动。
- **`explicit`**：单参数构造函数默认加，除非有意隐式转换。
- **`[[nodiscard]]`**：工厂、查询、可能失败的函数必须加。
- **`noexcept`**：析构、移动、`swap`、简单查询必须加。
- **`final`**：具体实现类加，防止意外继承。

### 5.1 定宽整数与原生类型

- 业务代码统一使用 `pwm` 提供的别名，不直接写 `int32_t` / `uint32_t` / `size_t`：

| 别名 | 底层类型 | 用途 |
| --- | --- | --- |
| `pwm::i8/i16/i32/i64` | 有符号定宽 | 协议字段、句柄、计数 |
| `pwm::u8/u16/u32/u64` | 无符号定宽 | 位域、ID、尺寸 |
| `pwm::f32/f64` | 浮点 | 缩放、动画、几何 |
| `pwm::usize/isize` | 平台宽度 | 容器大小、索引 |

- 唯一例外是 `src/base/types.hpp` 中的别名定义本身，以及必须匹配 C ABI 的封装边界。
- `pwm::handle_t` 用于 fd/句柄；`pwm::returned_t` 用于 C 返回值。
- 单位写入名字：`timeout_ms`、`width_px`、`scale_factor`。
- 禁止在协议结构体中使用 `int`/`long` 等平台相关宽度。

### 5.2 接口形态

```cpp
namespace pwm::platform::drm {

struct output_info {
    std::string name;
    pwm::u32 connector_id;
    pwm::u32 width_px;
    pwm::u32 height_px;
    pwm::u32 refresh_hz;
};

class output {
public:
    output(output&&) noexcept;
    output& operator=(output&&) noexcept;
    output(const output&) = delete;
    output& operator=(const output&) = delete;
    ~output();

    [[nodiscard]] auto info() const noexcept -> const output_info&;
    [[nodiscard]] auto commit(const frame& frame) -> result<void>;

private:
    pwm::handle_t fd_;
    output_info info_;
};

}
```

- 纯逻辑用自由函数，状态机用类/结构体。
- 继承只用于**接口**（`renderer`、`backend`）或协议对象；不要用继承复用实现。
- 优先组合；`pimpl` 仅在需要稳定 ABI 或隔离重依赖时使用。
- 公共 API 的返回类型尽量显式；内部实现可用 `auto` 简化。
- 不要把 `bool` 作为唯一错误信号；失败必须携带原因。

---

## 6. 错误处理（无异常）

### 统一类型

```cpp
namespace pwm {

enum class error_code : pwm::u16 {
    invalid_argument,
    not_found,
    permission_denied,
    io,
    protocol,
    unsupported,
    resource_exhausted,
    cancelled,
    internal,
};

struct error {
    error_code code;
    std::string_view message{};
    std::source_location where{};
};

template <typename T = void>
using result = std::expected<T, error>;

}
```

规则：

1. **全项目禁用异常**：`-fno-exceptions`；禁止 `throw`、`try`、`catch`。
2. **可恢复错误** → `result<T>`；不要用 `-1`/`nullptr` 哨兵。
3. **缺失值** → `std::optional<T>`；不要用魔法值。
4. **不变量破坏** → `PWM_ASSERT`（记录后终止），不要静默继续。
5. **客户端错误** → 协议错误并断开该客户端，合成器继续运行。
6. **硬件错误** → 尝试恢复；无法恢复则安全退出并恢复会话。
7. **取消** → `error_code::cancelled`，不是异常。
8. 错误路径避免动态分配；`message` 使用静态文本或有界上下文。

```cpp
auto file_descriptor::create(const char* path, pwm::i32 flags)
    -> result<raii<file_descriptor>> {
    const pwm::handle_t fd = ::open(path, flags | O_CLOEXEC);
    if (fd < 0) {
        return std::unexpected(error{
            .code = error_code::io,
            .message = "open failed",
        });
    }
    return raii{without_valid_check, file_descriptor{fd}};
}

auto open_output(const char* path) -> result<output> {
    auto fd = file_descriptor::create(path, O_RDWR);
    if (!fd) {
        return std::unexpected(fd.error());
    }
    return output::create(std::move(*fd));
}
```

- 禁止 `catch (...)`（也无异常可捕）。
- 禁止在析构函数中报告错误；析构只能记录并尽力清理。
- `std::expected` 的 `value()` 在失败时会终止；调用前必须检查，优先 `value_or` / `and_then` / `transform` / `error()`。

---

## 7. 所有权与生命周期

| 场景 | 用法 |
| --- | --- |
| 独占资源 | `std::unique_ptr<T>` 或自定义 RAII 句柄 |
| 共享所有权（确有必要） | `std::shared_ptr<T>`，并在注释中说明为何共享 |
| 非拥有引用 | `T&`、`T*`（可为空时）、`std::span`、`std::string_view` |
| 数组/缓冲区 | `std::span<T>`、`std::vector<T>` |
| C 句柄 | `pwm::raii<T,...>`，禁止裸 `close/free` |

规则：

- 非拥有指针不得跨异步边界存活，除非有明确的生命周期令牌/回调注销机制。
- 回调注册必须返回可注销的句柄；对象销毁时自动注销。
- Wayland 资源销毁必须幂等；迟到事件不得访问已销毁对象。
- GPU 正在使用的缓冲不得释放；使用 fence/page flip 完成事件回收。
- 禁止 `std::shared_ptr` 环；需要环时用 `weak_ptr` 并说明。

### 7.1 C API 封装契约（硬性）

任何 C API 资源都必须满足以下形式，才能进入业务代码：

```cpp
namespace pwm::platform {

struct file_descriptor {
    pwm::handle_t fd = -1;

    [[nodiscard]] static auto create(const char* path, pwm::i32 flags)
        -> result<raii<file_descriptor>>;

    [[nodiscard]] bool valid() const noexcept { return fd >= 0; }

    void destroy() const noexcept { ::close(fd); }
};

}
```

强制规则：

1. **命名**：资源申请统一 `create`（或 `create_*`），释放统一 `destroy`；禁止 `init`/`fini`、`new`/`free` 混用。
2. **RAII**：业务代码只持有 `raii<T, Args...>` 或专用 RAII 类型；裸句柄只允许存在于封装实现内部。
3. **所有权**：`raii` 不可拷贝；移动必须转移所有权；`release()` 只能用于明确移交所有权的边界。
4. **幂等销毁**：`destroy()` 必须能处理已释放/无效状态，禁止在析构中抛错或返回错误。
5. **头文件隔离**：`<drm/*.h>`、`<vulkan/*.h>`、`<linux/*.h>`、POSIX 头只允许出现在 `base/`、`platform/`、`vulkanpp/` 的封装实现中。
6. **常量与枚举**：C API 的常量/枚举也必须在封装层转换为 `pwm` 类型，禁止把 C 宏泄漏到业务层。
7. **错误码**：`create` 失败返回 `result<raii<T>>`；不要用无效句柄 + 断言代替可恢复错误。
8. **Vulkan**：`vkCreate*`/`vkDestroy*`、`vkAllocate*`/`vkFree*`、队列、命令池、同步对象全部由 `vulkanpp` 封装；业务层不出现 `Vk*`。
9. **测试**：每个封装至少覆盖创建失败、重复销毁、移动后销毁、正常销毁。

> 代码审查中看到裸 C 调用，默认拒绝；除非它就在上述封装层内，并有对应 RAII 类型与测试。

---

## 8. 并发与协程

默认**单线程**。这不是偷懒，而是把正确性放在第一位。

- 场景、窗口、协议状态只在主线程读写。
- 工作线程只处理无共享状态的任务（解码、磁盘、网络），结果通过事件循环投递。
- 禁止在热路径使用 `std::mutex`；如必须加锁，在头文件注释锁顺序。
- 使用 `std::jthread` + `std::stop_token` 管理线程；禁止 `detach`。
- 跨线程共享只用 `std::atomic` 或消息队列；不要共享容器。
- 时间使用 `std::chrono::steady_clock`；不要用系统时钟做动画。
- 所有等待必须有超时或取消路径。

```cpp
auto worker = std::jthread([&](std::stop_token stop) {
    while (!stop.stop_requested()) {
        auto job = queue.pop(stop);
        if (!job) break;
        auto result = run(*job);
        loop.post([this, result = std::move(result)] { on_done(result); });
    }
});
```

### 8.1 协程规范

- 只使用项目提供的 `pwm::event::task<T>`；不要直接暴露 `std::coroutine_handle`。
- `task<T>` 必须**惰性启动**，由事件循环或父任务持有；禁止 detach。
- 错误随 `task<T>` 内部的 `result<T>` 传播；`co_await` 失败时短路，不使用异常。
- 挂起期间不得持有指向栈对象的引用；需要跨挂起点的数据必须由协程帧或 `shared_ptr` 拥有。
- 所有 `co_await` 的 I/O 必须注册到事件循环，并提供取消路径；取消返回 `error_code::cancelled`。
- 禁止在协程中调用阻塞 I/O、`sleep`、锁等待；需要时投递到工作线程。
- 协程恢复线程必须与创建它的事件循环一致；跨线程恢复视为 bug。
- 每个客户端连接一个会话协程；连接断开时取消整条协程链并释放对象。

### 8.2 线程与协程的边界

| 场景 | 规则 |
| --- | --- |
| 场景/窗口/协议状态 | 主线程独占 |
| 协程恢复 | 主事件循环线程 |
| 工作线程 | 无共享状态；结果通过 `loop.post` 回投递 |
| 跨线程数据 | `std::atomic` 或消息队列；禁止共享容器 |
| 取消 | `stop_token` / `error_code::cancelled`；不使用异常 |

---

## 9. 性能约定

### 帧预算（1080p@60 的初始目标）

| 项目 | 预算 |
| --- | --- |
| 输入处理（单事件） | < 0.2 ms |
| 场景更新 + 布局 | < 1.0 ms |
| CPU 渲染提交 | < 3.0 ms |
| GPU 渲染 | < 8.0 ms |
| 总帧时间 P99 | < 16.7 ms |
| 稳态堆分配 | 0 次/帧 |
| 空闲唤醒 | 0 次/秒（无动画、无输入时） |

> 预算会随硬件与场景调整，但必须在 [testing.md](testing.md) 中记录测量条件。

### 规则

- 热路径禁止：字符串格式化、动态分配、文件 I/O、锁、异常。
- 容器预先 `reserve`；帧间复用缓冲；使用对象池/arena。
- 优先连续内存与索引，而非指针图；仅在测量证明需要时做 SoA。
- 避免 `std::function` 热路径；用模板回调或 `function_ref`。
- `[[likely]]` / `[[unlikely]]` 只用于有数据支撑的分支。
- 优化前先 profile；PR 中附 `perf`/Tracy 截图或数据。
- 任何“更快”的声明都必须说明基线、硬件、场景与测量方法。

---

## 10. 日志与断言

```cpp
PWM_LOG_DEBUG("drm", "connector {} picked mode {}x{}@{}",
              connector_id, width, height, refresh_hz);
```

- 运行期统一走 `base::log`，禁止 `std::cout` / `printf`。
- 级别：`trace`、`debug`、`info`、`warn`、`error`。
- 分类：模块名（`drm`、`input`、`scene`、`wayland`、`wm`、`config`）。
- 热路径默认不打印；需要时通过环境变量开启。
- 日志中不要输出敏感路径、剪贴板内容、按键序列。
- `PWM_ASSERT(cond)` 只用于“绝不应该发生”的不变量；可恢复错误用 `result`。

---

## 11. 测试要求

- 新增/修改行为必须有测试；bug 修复必须先有失败用例。
- 纯逻辑（几何、布局、状态机、配置）必须有单元测试。
- 协议对象与输入路由优先用 headless 集成测试。
- 测试必须确定性：注入时钟、固定随机种子、禁止 `sleep` 等待。
- 测试命名：`test_<unit>_<behavior>`，例如 `test_region_union_merges_overlap`。
- 详见 [testing.md](testing.md)。

---

## 12. 函数规模、注释与文档

### 12.1 函数规模与嵌套

- 业务函数目标 **≤ 60 行**，硬上限 **80 行**（不含签名与空行）。
- 嵌套层级（`if` / `for` / `while` / `switch` / lambda）最多 **5 层**。
- `main()` 只做组合与生命周期管理，不承载业务逻辑。
- 优先早返回、提炼具名函数、使用 `std::ranges` 算法降低嵌套。
- 长 lambda 视为函数；超过 20 行必须提炼为具名函数或类型。
- 复杂条件提炼为具名布尔函数，例如 `can_scanout_directly()`，而不是长表达式加注释。
- 当前 `clang-tidy` 配置启用 `readability-function-size`：
  - `LineThreshold: 80`
  - `NestingThreshold: 5`
  - `StatementThreshold: 60`
  - `BranchThreshold: 20`

### 12.2 注释

**代码中禁止解释性质的注释。**

“解释性质”包括但不限于：

- 复述代码在做什么：`// loop over outputs`。
- 设计/背景说明：`// we use atomic commits because ...`。
- 临时提醒、TODO、FIXME、作者标记。
- 命名空间结束标记：`} // namespace pwm`。
- 被注释掉的代码。
- 中文或其他非英文注释。

代码中只允许两类注释：

1. `SPDX-License-Identifier` 元数据。
2. **英文 API 结构化文档注释**：`///`、`//!`、`/** ... */`、`/*! ... */`。

API 注释只写调用者必须知道的契约：

- 参数含义与有效范围。
- 所有权与生命周期。
- 失败条件与返回错误。
- 线程/协程约束。
- 前置条件与不变量。

```cpp
/// Commits one frame to the output.
///
/// The caller must keep the frame resources alive until the page flip completes.
/// Only one commit may be in flight per output.
///
/// @return An error when the driver rejects the commit.
[[nodiscard]] auto commit(const frame& frame) -> result<void>;
```

规则：

- API 注释必须是英文。
- 注释不能替代命名；如果函数名需要注释才能理解，先改名。
- `.cpp` 文件除 SPDX 外不写注释；契约写在头文件。
- 配置文件、脚本、Dockerfile、YAML 同样不写解释性注释。
- `docs/` 与 README 可以用中文；它们不是代码。
- 由 `tools/check_comments.py` 在 CI 中强制：非 API 注释、非英文注释直接失败。

---

## 13. 提交与 PR

### 提交信息

Conventional Commits：

```text
<type>(<scope>): <summary>

<body: 为什么、取舍、影响>

<footer: BREAKING CHANGE / Refs>
```

`type`：`feat`、`fix`、`perf`、`refactor`、`docs`、`test`、`build`、`ci`、`chore`、`revert`。
`scope`：模块名，如 `drm`、`input`、`scene`、`wayland`、`wm`、`ci`。

### PR 要求

- 一个 PR 只做一件事；尽量 < 400 行有效变更。
- 描述包含：问题、方案、替代方案、测试方式、风险。
- 架构变更附 ADR 或 ADR 草案。
- 性能相关附数据。
- 不提交生成物、构建目录、编辑器配置。
- 合并前 CI 全绿；至少一名 reviewer 批准。

### 审查清单

- [ ] 分层与依赖方向正确？
- [ ] 错误路径有测试？客户端错误不会拖垮合成器？
- [ ] 资源生命周期清晰？没有悬垂引用/双重释放？
- [ ] 热路径没有新增分配、格式化、锁？
- [ ] 命名、单位、所有权是否自解释？
- [ ] 公开 API 有文档与 `[[nodiscard]]`？
- [ ] 配置/协议变更是否向后兼容或有迁移说明？

---

## 14. 警告与静态分析

Phase 0 目标警告集（清理后启用 `-Werror`）：

```text
-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion
-Wshadow -Wnon-virtual-dtor -Wold-style-cast -Woverloaded-virtual
-Wnull-dereference -Wdouble-promotion -Wformat=2
```

- 禁止无理由 `#pragma GCC diagnostic ignored`；必须写明原因与范围。
- `clang-tidy` 初始启用：`bugprone-*`、`performance-*`、`modernize-*`（排除与反射冲突的检查）。
- 对 C++26 反射文件，静态分析暂时豁免，但必须经过 code review 与单元测试。
- `tools/check_policy.sh` 在 CI 中阻断：异常、libwayland、裸 C API、C 头泄漏、`int32_t` 等。

---

## 15. 兼容性与弃用

- `0.x` 阶段不承诺 ABI/配置兼容，但破坏性变更必须写入 `CHANGELOG.md`。
- 弃用 API 使用 `[[deprecated("use X instead")]]`，至少保留一个次版本。
- 配置键弃用需在启动时警告，并给出替代键。
- 协议实现只宣告已支持的版本；不要“宣告了但没实现”。

---

**一句话总结**：让编译器、CI 和类型系统替你记住规则；把人的注意力留给设计与取舍。
