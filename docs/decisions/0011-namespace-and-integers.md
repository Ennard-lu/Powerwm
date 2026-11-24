# ADR-0011: 命名空间 pwm 与定宽整数别名

- 状态：Accepted
- 日期：2026-11-24
- 决策者：powerwm 维护者
- 相关：[coding-style.md](../coding-style.md)、[architecture.md](../architecture.md)

## 背景

项目名 `powerwm` 较长，作为根命名空间会在大量代码中造成噪声。底层代码需要明确的整数宽度（协议字段、KMS 属性、GPU 格式），直接使用 `int32_t` / `uint32_t` / `size_t` 既冗长又容易混用符号。

现有原型已有 `bc` 命名空间与 `bc::i32` 等别名，但 `bc` 无语义，且与目标命名不一致。

## 决策

1. 根命名空间为 **`pwm`**：
   - `pwm::event`
   - `pwm::platform::drm`
   - `pwm::platform::input`
   - `pwm::render`
   - `pwm::vulkanpp`
   - `pwm::scene`
   - `pwm::wayland`
   - `pwm::wm`
   - `pwm::shell`
   - `pwm::config`
   - `pwm::ipc`
2. 定宽别名集中在 `src/base/types.hpp`：
   - `i8/i16/i32/i64`
   - `u8/u16/u32/u64`
   - `f32/f64`
   - `usize/isize`
   - `handle_t`、`returned_t`
3. 业务代码使用 `pwm::i32` 等别名；唯一例外是别名定义本身与必须匹配 C ABI 的封装边界。
4. 宏前缀统一为 `PWM_`（如 `PWM_ASSERT`、`PWM_LOG`）。
5. 包含路径使用 `include/pwm/`，头文件写作 `#include <pwm/...>`。

## 后果

### 正面

- 命名短、可读性高，减少视觉噪声。
- 整数宽度显式，协议/DRM/Vulkan 边界不易混用。
- 与现有 `i32/u32` 风格连续，迁移成本低。
- 统一宏前缀，避免 `LIBDRMPP_*` 等历史名称泄漏。

### 负面 / 代价

- `pwm` 可能与第三方库命名冲突（需要时用完整限定或别名）。
- 定宽别名不是标准类型，阅读者需要熟悉项目约定。
- C ABI 边界仍会出现 `std::int32_t` 等，需要注释说明。

## 备选方案

| 方案 | 优点 | 缺点 | 为何未选 |
| --- | --- | --- | --- |
| `powerwm::` | 语义明确 | 冗长 | 代码噪声大 |
| 保持 `bc::` | 迁移成本低 | 无语义、历史包袱 | 不符合项目命名 |
| 直接用 `int32_t` | 标准、无需别名 | 冗长、易混用宽度 | 可读性差 |
| 自定义 `Int32` 类型 | 强类型 | 运算符/互操作样板多 | 过度设计 |

## 重新审视条件

- `pwm` 与关键依赖发生不可调和的命名冲突。
- 定宽别名在实践中导致 ABI/格式化问题，且无法通过封装隔离。
