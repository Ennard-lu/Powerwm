# powerwm 测试与性能策略

> 目标：**每次合并都更可信**，而不是“看起来能跑”。
> 测试不是 Phase 7 的收尾工作；它是每个阶段退出标准的一部分。

---

## 1. 测试金字塔

```text
                 ┌─────────────────────┐
                 │  手工 dogfood / 兼容性 │  少、慢、真实硬件
                 ├─────────────────────┤
                 │  集成（headless）     │  协议、场景、输入、配置
                 ├─────────────────────┤
                 │  金图（golden image） │  渲染正确性
                 ├─────────────────────┤
                 │  单元测试             │  快、多、确定性
                 └─────────────────────┘
```

| 层级 | 范围 | 运行频率 | 目标耗时 |
| --- | --- | --- | --- |
| 单元 | 纯函数、几何、状态机、解析器 | 每次提交 | < 2 s |
| 集成 | headless 合成器 + 模拟客户端 | 每次 PR | < 60 s |
| 金图 | 渲染输出像素对比 | 每次 PR（可并行） | < 120 s |
| 模糊 | 协议/配置/IPC 解析 | 每日 + 发布前 | 小时级 |
| 基准 | 帧时间、分配、启动 | 每次 PR（阈值） | < 5 min |
| 兼容 | 真实应用/硬件矩阵 | 每周/发布前 | 手动 + 脚本 |

---

## 2. 工具与目录

```text
tests/
├── unit/            # Catch2，无平台依赖
├── integration/     # headless 合成器、模拟 Wayland 客户端
├── golden/          # 场景描述 + 期望 PNG + 差异阈值
├── fuzz/            # libFuzzer 入口
├── bench/           # Google Benchmark 或自建计时器
└── support/         # 测试夹具、假时钟、内存分配计数
```

- 测试框架：**Catch2 v3**（通过 `FetchContent` 固定版本）；需配置 `CATCH_CONFIG_DISABLE_EXCEPTIONS` 以兼容 `-fno-exceptions`。
- 当前 Phase 0 已接通 CTest 与一个无框架冒烟测试；Catch2 与正式夹具随后引入。
- 构建集成：CTest；`ctest --preset dev`。
- 基准：优先自建 `steady_clock` 计时 + 统计，避免重型依赖；需要时引入 Google Benchmark。
- 模糊：Clang/libFuzzer（仅测试构建；主构建仍用 GCC）。

---

## 3. 单元测试

### 必须覆盖的内容

- 几何：`point`、`size`、`rect`、`region` 的交/并/包含/裁剪/空区域。
- 布局：给定窗口集合与工作区，输出矩形满足不变量（无重叠、不越界、顺序稳定）。
- 状态机：焦点栈、工作区切换、键位模式、事务提交。
- 配置：合法/非法 TOML、默认值合并、热重载原子性。
- 协议参数校验：越界、空值、非法枚举。
- 错误路径：每个 `result` 的失败分支至少一个用例。

### 约定

- 测试名描述行为，不描述实现：`test_region_union_merges_overlap`。
- 一个测试只验证一个概念；避免“大杂烩”。
- 使用表驱动覆盖边界值。
- 禁止真实时间、真实文件系统、真实网络；通过接口注入。
- 随机测试固定种子，并在失败时打印种子。

```cpp
TEST_CASE("region union merges overlapping rects") {
    base::region r;
    r.add({.x = 0, .y = 0, .width = 10, .height = 10});
    r.add({.x = 5, .y = 5, .width = 10, .height = 10});

    REQUIRE(r.bounds() == base::rect{.x = 0, .y = 0, .width = 15, .height = 15});
    REQUIRE(r.contains({.x = 14, .y = 14, .width = 1, .height = 1}));
}
```

---

## 4. 集成测试（headless）

headless 后端必须满足：

- 不打开 `/dev/dri/*`，不依赖 GPU。
- 由测试显式推进时间与帧：`backend.tick(now)`。
- 可注入输入事件，查询场景树与协议事件。
- 渲染可输出到内存缓冲（用于金图），也可只做“逻辑帧”。
- 输出分辨率、刷新率、缩放可配置。

### 场景示例

1. 创建两个 `xdg_toplevel`，提交 buffer，断言场景节点与焦点。
2. 发送 `wl_keyboard.key`，断言快捷键与客户端事件顺序。
3. 提交局部 damage，断言输出 damage 被正确合并。
4. 客户端断开，断言资源释放、焦点回退、无悬垂回调。
5. VT 切换模拟（会话失活/恢复），断言输出停止/恢复且不丢状态。

### 模拟客户端

- 测试客户端基于**自研 wire 层**实现，不链接 libwayland-client；服务端与客户端共享编解码代码，必须用独立用例验证边界。
- 真实兼容性通过外部客户端矩阵验证：`foot`、`weston-terminal`、GTK4、Qt6、`wayland-info` 等（它们可以自行链接 libwayland，但 powerwm 本体不链接）。
- 测试客户端作为子进程运行，隔离事件循环；通过 socket/共享内存/管道断言行为。
- 覆盖：握手、对象 ID、版本协商、非法消息、fd 传递、客户端异常断开。
- 协程测试：取消、超时、跨挂起点生命周期、同一连接多请求并发。

---

## 5. 金图测试

### 流程

1. 测试用固定场景描述（JSON/TOML）构建场景。
2. headless 后端以固定尺寸、固定 DPI、固定时间渲染。
3. 输出 PNG，与 `tests/golden/<case>/expected.png` 比较。
4. 允许每像素小误差与整体差异阈值；差异图输出到 `build/golden-diff/`。
5. 更新基线必须走显式命令并在 PR 中附差异图。

### 规则

- 渲染必须确定性：固定时钟、固定随机种子、固定字体/纹理资源。
- 禁用依赖驱动的浮点差异：使用软件渲染或宽松阈值。
- 金图用例覆盖：圆角、阴影、透明度、缩放、裁剪、sRGB、多输出。
- 不为“让测试通过”而降低阈值；先判断是 bug 还是预期变化。

---

## 5.1 Vulkan 与 headless 渲染测试

- headless 后端使用 **lavapipe / llvmpipe** 提供确定性 Vulkan 实现；CI 不依赖真实 GPU。
- 金图测试固定：实例/设备选择、图像格式、颜色空间、采样数、时间步长、随机种子。
- 验证项：DMA-BUF 导入/导出、修饰符回退、sRGB、缩放、裁剪、圆角/阴影/透明度。
- Vulkan validation layer 在 debug/CI 中开启；发现错误即失败。
- 真实 GPU 冒烟单独跑：验证 KMS 扫描输出、外部内存扩展、实际修饰符组合。
- 性能基线使用 Vulkan timestamp query，不使用 CPU 估算。

---

## 6. 模糊测试

### 目标

任何来自客户端的字节流都不能让合成器崩溃、越界或无限循环。

### 范围

| 目标 | 输入 | 入口 |
| --- | --- | --- |
| 配置解析 | 任意 TOML | `fuzz_config` |
| IPC 解析 | 任意 JSON 行 | `fuzz_ipc` |
| 协议消息 | 任意 Wayland 消息字节 | `fuzz_wayland_message` |
| 协议对象状态机 | 合法/非法请求序列 | `fuzz_wayland_object` |
| 协程连接 | 断包、半包、fd 洪泛、取消 | `fuzz_wayland_connection` |
| EDID/模式解析 | 任意 blob | `fuzz_edid` |
| 区域/几何运算 | 任意矩形集合 | `fuzz_region` |

### 要求

- 使用 libFuzzer，持续运行至少 24 小时无 crash。
- 发现 crash 必须加入回归语料库（`tests/fuzz/corpus/`）。
- 模糊构建启用 ASan + UBSan。
- 崩溃去重，按堆栈归类。
- 协议 wire 与对象状态机是最高优先级 fuzz 目标：禁止越界读、无限循环、fd 泄漏。

---

## 7. 基准与性能预算

### 基准场景

| 场景 | 描述 | 关注指标 |
| --- | --- | --- |
| `idle` | 无客户端、无动画 | CPU 唤醒次数、功耗 |
| `static` | 10 个静态窗口 | 首帧、稳态帧时间 |
| `anim` | 100 个动画表面 | FPS、P99 帧时间、分配 |
| `damage` | 单个小区域持续更新 | GPU 工作量、提交面积 |
| `blur` | 全屏模糊背景 | GPU 时间、降级触发 |
| `startup` | 冷启动到首帧 | 时间、内存峰值 |
| `clients` | 50 个客户端连接/断开 | 泄漏、句柄数、延迟 |

### 初始预算（参考硬件：近 5 年集显，1080p@60）

| 指标 | 预算 | 测量方式 |
| --- | --- | --- |
| 输入到呈现 P99 | < 16.7 ms | 时间戳注入 + presentation feedback |
| CPU 帧时间 P99 | < 8 ms | `steady_clock` 包围帧处理 |
| GPU 帧时间 P99 | < 8 ms | Vulkan timestamp query / 驱动统计 |
| 稳态堆分配 | 0 次/帧 | 全局分配计数钩子 |
| 空闲 CPU | < 1% 单核 | `pidstat` 60 s |
| 常驻内存（20 客户端） | < 300 MB | RSS |
| 冷启动到首帧 | < 500 ms | 进程启动时间戳 |
| 配置热重载 | < 100 ms | 注入文件变更 |

> 预算不是营销数字。任何放宽都必须在 PR 中说明硬件、场景与原因。

### 防回归

- CI 运行轻量基准（`anim`、`damage`），与基线比较；超过阈值（如 10%）失败。
- 基准结果存入构建产物；趋势图在 Phase 7 接入。
- 性能测试与功能测试分开，避免 CI 波动造成假失败；关键阈值使用多次运行的中位数。

---

## 8. 内存与资源测试

- 以计数分配器验证“稳态零分配”。
- ASan：越界、use-after-free、double-free。
- LSan：退出时泄漏；对已知全局缓存使用抑制文件并注明原因。
- UBSan：未定义行为。
- TSan：仅在引入多线程后启用，单独 CI 任务。
- 文件描述符：测试前后计数，客户端反复连接/断开不得增长。
- DRM 对象：headless 无法验证真实 KMS，Phase 1 增加硬件冒烟测试。
- Vulkan 对象：开启 validation layer 的 debug 构建中，创建/销毁必须完全配对；使用 `vulkanpp` 统计句柄数。
- 无异常构建：所有测试目标也必须在 `-fno-exceptions` 下编译运行；Catch2 使用 `CATCH_CONFIG_DISABLE_EXCEPTIONS` 或改用无异常断言。

---

## 9. CI 集成

| 任务 | 内容 | 阻断 |
| --- | --- | --- |
| `format` | clang-format 检查 | 是 |
| `policy` | 禁止异常/libwayland/裸 C API/`int32_t`/解释性注释 | 是 |
| `build` | Debug/Release/`-fno-exceptions` 构建 | 是 |
| `test` | 单元 + 集成 + 金图 | 是 |
| `sanitizers` | ASan + UBSan 构建与测试 | 是 |
| `bench` | 轻量基准阈值 | 是（Phase 2 起） |
| `fuzz` | 每日/发布前 30–60 min | 发布阻断 |
| `hardware` | 真实 GPU 冒烟 | 发布阻断（Phase 1 起） |

---

## 10. 不稳定测试政策

- 禁止 `sleep` 等待；用条件变量/事件循环推进。
- 禁止依赖执行顺序；每个测试独立初始化。
- 标记 `[flaky]` 只能作为临时措施，必须关联 issue 并在两周内修复或删除。
- CI 重跑不能作为“修复”；同一测试连续两次失败即视为真失败。

---

## 11. 覆盖率

- 目标不是 100%，而是**关键路径无空白**。
- 门槛（Phase 0 起逐步提高）：
  - `base`、`config`、`wm/layout`：≥ 80% 行覆盖；
  - 协议胶水与平台后端：以集成测试覆盖为主，不强制行覆盖；
  - 热路径：以基准与不变量测试为主。
- 覆盖率报告作为 CI 产物，不作为唯一质量指标。

---

## 12. 发布前检查清单

- [ ] 全部单元/集成/金图测试通过。
- [ ] ASan/UBSan 全绿；无新增抑制。
- [ ] 24 小时 fuzz 无 crash。
- [ ] 性能预算达标；无 > 10% 回归。
- [ ] 真实硬件冒烟：多输出、VT 切换、热插拔、挂起/恢复。
- [ ] 应用矩阵：终端、浏览器、编辑器、聊天、视频、游戏。
- [ ] 7 天 dogfood 记录与缺陷清零（1.0）。
