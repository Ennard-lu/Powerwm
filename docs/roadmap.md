# powerwm 路线图

> 版本：2026-11（Phase 0）
> 原则：**每个阶段都必须能独立演示、独立测试、独立回滚。**

powerwm 不是一次性写成的项目。我们把它拆成 8 个阶段：前 3 个阶段建立“能出图、能合成”的地基，中间 3 个阶段建立“能日常使用”的桌面，最后 2 个阶段把它打磨成可发布的产品。

```text
P0 地基 ──► P1 点亮 ──► P2 画布 ──► P3 语言 ──► P4 秩序 ──► P5 肌肤 ──► P6 生态 ──► P7 磨砺
   工程化      出图        渲染        协议        窗口管理      桌面外壳      兼容性        1.0
```

---

## 0. 当前状态（2026-11）

| 项目 | 状态 |
| --- | --- |
| 仓库 | 尚无正式提交历史；目录为早期实验 |
| 构建 | CMake 4.x + GCC 16，目标 `powerwm_core` / `powerwm` / `powerwm_drm_bringup`，依赖 `libdrm`，启用 `-fno-exceptions` |
| 代码 | `pwm` 命名空间、libdrm/libgbm RAII 封装、原子提交点灯 demo、`std::meta` 属性查找实验 |
| 测试 | CTest 冒烟测试；Catch2 与正式夹具待引入 |
| CI/CD | 格式、Debug/Release/ASan+UBSan 构建与测试、标签发布骨架 |
| 文档 | 路线图、架构、规范、测试、CI/CD、ADR、Apache-2.0 许可证 |

**结论**：当前代码是“技术验证”，不是产品骨架。Phase 0 的第一件事是把验证代码整理成可长期演进的多目标工程。

---

## 1. 阶段总览

| 阶段 | 代号 | 预计投入 | 核心问题 | 演示形态 |
| --- | --- | --- | --- | --- |
| P0 | 地基 | 2–3 周 | 我们能否可持续地构建与验证？ | 一条命令构建 + CI 全绿 |
| P1 | 点亮 | 4–6 周 | 我们能否稳定接管显示与输入？ | TTY 启动，双缓冲 page flip 动画，VT 切换可恢复 |
| P2 | 画布 | 8–12 周 | 我们能否高效、正确地合成客户端画面？ | Vulkan 合成、DMA-BUF、损坏区域可视化 |
| P3 | 语言 | 14–20 周 | 我们能否自研协议栈并与真实客户端对话？ | 运行 foot/GTK/Qt 应用，剪贴板可用 |
| P4 | 秩序 | 6–8 周 | 我们能否把窗口摆好、管好？ | 平铺布局、工作区、键位、IPC |
| P5 | 肌肤 | 8–10 周 | 我们能否形成一个完整桌面？ | 面板、启动器、通知、锁屏、主题 |
| P6 | 生态 | 8–12 周 | 我们能否融入现有 Linux 生态？ | XWayland、截屏、portal、输入法 |
| P7 | 磨砺 | 持续 | 我们能否放心地每天使用？ | 7 天 dogfood、性能达标、可安装包 |

> 时间只是相对估算，用于排优先级，不作为承诺。硬件、驱动与协议兼容性会显著影响实际进度。

---

## 2. Phase 0 — 地基

**目标**：把“实验代码”变成“可以长期开发的工程”。

### 交付物

- [x] 项目命名、愿景、路线图、架构、编码规范、测试策略、CI/CD 文档。
- [x] GitHub Actions：格式检查、Debug/Release 构建、ASan+UBSan 测试、标签发布骨架。
- [x] CMake Presets：`dev`、`release`、`asan`，统一本地与 CI。
- [x] `.clang-format`、`.clang-tidy`、`.editorconfig`、`.gitignore`。
- [x] 可复现开发容器 `containers/dev.Dockerfile`。
- [x] 许可证：Apache-2.0 + `LICENSE` + SPDX 头。
- [x] 命名空间迁移到 `pwm`；`bc::` → `pwm::`，`LIBDRMPP_*` → `PWM_*`。
- [x] 启用 `-fno-exceptions`；错误模型定为 `pwm::result<T>` / 错误码。
- [x] 定宽整数别名（`i32`/`u32`/`usize`）成为项目规范。
- [x] 目录重构第一阶段：`src/base`、`src/platform/drm`、`src/platform/gbm`、`src/app`、`tools`。
- [x] 多目标 CMake：`powerwm_core`（静态库）、`powerwm`（可执行）、`powerwm_tests`、`powerwm_drm_bringup`。
- [x] 应用骨架 `powerwm`：无参数打印版本/帮助，不触碰 DRM。
- [ ] 后续目录：`src/event`、`src/render`、`src/wayland`、`src/wm`、`src/shell`、`src/config`。
- [x] 接通 CTest 与冒烟测试；Catch2 v3 与正式测试夹具待引入。
- [ ] 把反射实验封装到 `base/meta.hpp`，保证非反射路径可编译。
- [ ] 建立 headless 后端骨架（无 DRM 也能跑测试）。
- [x] 错误类型与致命错误处理统一（`pwm::result` / `error_code` / `terminate_with_message`）。
- [ ] 日志系统与断言宏。
- [x] C API 封装检查脚本与 CI 门禁（`tools/check_policy.sh` + `Policy` job）。
- [x] 注释策略检查（`tools/check_comments.py`）：禁止解释性注释，只允许英文 API 注释。
- [x] 构建元数据模块：git commit / 分支 / dirty / UTC 构建时间 / 编译器 / 参数 / 特性。
- [x] 依赖资源由宿主创建（`device.create_dumb_buffer` 等）；统一尾置返回类型。
- [ ] 分支保护与必需检查配置。

### 退出标准

1. 全新克隆执行 `cmake --preset dev && cmake --build --preset dev && ctest --preset dev` 一次通过。
2. CI 在 Linux x86_64 上全绿，格式检查无豁免。
3. `powerwm` 可执行文件在无参数时打印版本与帮助并退出，不触碰 DRM。
4. 至少 20 个基础单元测试（几何、区间、结果类型、配置解析雏形）。
5. ADR-0001 ~ ADR-0011 状态为 `Accepted` / `Superseded`（许可证与实现决策已定案）。

### 风险

- C++26 模块与反射的编译器支持仍在演进：**核心逻辑不得依赖反射才能编译**。
- 目录重构期间避免功能开发，减少冲突。
- `-fno-exceptions` 下第三方测试框架/库需要确认兼容；Catch2 需配置 `CATCH_CONFIG_DISABLE_EXCEPTIONS` 或改用无异常断言后端。

---

## 3. Phase 1 — 点亮

**目标**：建立稳定的 DRM/KMS 输出与输入基础，能在真实硬件上持续出图。

### 交付物

- [x] DRM 设备枚举（`/dev/dri/card*`）与主输出选择（connected connector + 首个 mode）。
- [x] 原子模式设置：首选模式、connector/CRTC/plane 属性。
- [x] Page flip 完成事件与 `poll` 等待。
- [x] 双缓冲 page flip 动画，避免撕裂。
- [x] `libseat` 封装与设备打开探针。
- [x] `libseat` 接入 DRM 打开；VT disable 暂停提交，enable 后重新获取 master 并恢复。
- [ ] `libseat` 接入输入与统一多 fd 事件循环；挂起/恢复的完整电源管理。
- [ ] `libudev` 热插拔：连接/断开显示器时安全重建输出。
- [x] `libinput` 上下文与事件探针：键盘/指针/触摸事件标准化。
- [ ] 输入路由：焦点表面、抓取、快捷键拦截，并接入合成器事件循环。
- [x] 多 fd `poll` 事件循环基础与 `eventfd` 单元测试。
- [x] 将 DRM、seat、input 接入统一事件循环（点灯工具）。
- [ ] 输入路由与合成器场景/焦点模型。
- [ ] 备用后端：`headless`（计时器驱动）与 `nested`（未来可选）。
- [ ] 诊断：`--list-outputs`、`--list-inputs`、`--drm-debug`。

### 退出标准

1. 在至少 2 种 GPU（Intel/AMD 或 NVIDIA proprietary 之外的开源驱动）上 60 Hz 连续运行 1 小时无撕裂、无黑屏。
2. VT 切换 50 次后仍能恢复输出与输入。
3. 拔插 HDMI 10 次无崩溃、无泄漏。
4. 空闲时 CPU 占用 < 1%（单核），无周期性唤醒。
5. 所有 DRM 资源在退出时释放，`drmModeGetResources` 无残留。

### 风险

- 驱动对原子提交的宽松/严格程度不同：需要能力探测与降级路径。
- NVIDIA proprietary 驱动行为差异：列为“尽力支持”，不作为 1.0 阻塞项。
- 多 GPU / 多平面叠加：Phase 1 只做单 GPU、单主平面，其余延后。

---

## 4. Phase 2 — 画布

**目标**：建立 Vulkan 渲染器与场景图，为合成提供正确、可测量的基础。

### 交付物

- `vulkanpp`：Vulkan 实例、物理/逻辑设备、队列、命令池、同步对象的 `create`/`destroy` + RAII 封装。
- Vulkan 合成后端：离屏渲染到可导出图像，经 DMA-BUF 交给 KMS 扫描输出。
- 平面渲染管线：纹理、变换、裁剪、圆角、阴影、简单模糊（可选）。
- 场景图：`node` / `surface_node` / `container_node` / `effect_node`，显式父子与变换。
- 事务模型：双缓冲场景状态，提交时原子交换，避免半更新帧。
- 损坏跟踪：表面损坏、场景损坏、输出损坏三级合并。
- DMA-BUF 导入/导出、修饰符协商、格式回退。
- 客户端缓冲生命周期：release、fence、缓存与复用。
- 渲染统计：CPU/GPU 帧时间、提交次数、掉帧计数。
- headless Vulkan 后端（lavapipe）用于 CI 与金图。
- 基准场景：N 个动画表面、全屏视频、模糊背景。

### 退出标准

1. 100 个 256×256 动画表面在 1080p@60 下稳定 60 FPS（参考硬件：集显）。
2. 仅局部更新时，GPU 工作量与损坏面积近似成正比。
3. 稳态帧堆分配为 0（以计数分配器验证）。
4. 场景事务在任意提交点都不会产生撕裂或半更新。
5. headless 后端可渲染确定帧并输出 PNG，用于金图测试。
6. 所有 Vulkan 对象经 `vulkanpp` 管理，退出时无 validation layer 报错。

### 风险

- Vulkan 驱动在格式/修饰符/外部内存上的差异：建立能力矩阵与自动回退。
- 直接 KMS 扫描输出需要 `VK_KHR_external_memory_fd`、`VK_EXT_external_memory_dma_buf`、`VK_EXT_image_drm_format_modifier` 等扩展；缺失时回退到线性图像或软件路径。
- 模糊等效果成本高：默认关闭，提供质量档位与帧预算降级。
- Vulkan 初始化/验证层拖慢启动：release 关闭验证层，debug 可开关。

---

## 5. Phase 3 — 语言

**目标**：**不使用 libwayland**，自行实现 Wayland wire protocol、对象模型与协程协议引擎，让真实客户端能够显示、输入、复制粘贴。

> 这是整个项目风险最高的阶段。原计划的 8–12 周调整为 **14–20 周**，并拆成 3A/3B/3C 三个可独立验收的子阶段。

### 3A — Wire 与协程运行时（4–6 周）

- 消息头、参数编解码、版本/opcode、错误消息。
- `SCM_RIGHTS` fd 传递、缓冲区边界、部分读写。
- 对象表、客户端 ID 分配、接口/版本、销毁与迟到事件防护。
- Wayland XML 解析器 + C++26 描述符生成器（替代 `wayland-scanner`）。
- `pwm::event_loop`：epoll、定时器、signal、协程调度、取消。
- 自研测试客户端（基于同一 wire 层，不依赖 libwayland-client）。
- wire fuzz：任意字节流不得越界/崩溃/死循环。

**退出标准**：

1. 与真实 libwayland 客户端完成 `wl_display` / `wl_registry` / `wl_callback` 握手。
2. fd 传递双向正确，1000 次压力测试无泄漏。
3. 非法消息返回 protocol error，不崩溃。
4. 24 小时 wire fuzz 无 crash。
5. `ldd powerwm` 中不出现 `libwayland-*`。

### 3B — 核心合成协议（6–8 周）

- `wl_compositor`、`wl_surface`、`wl_region`、`wl_shm`。
- `xdg_wm_base` / `xdg_surface` / `xdg_toplevel` / `xdg_popup`。
- `wl_seat` / 键盘 / 指针 / 触摸；`wl_output`。
- `wl_data_device_manager` 与剪贴板。
- `linux-dmabuf-v1` 与 buffer release。
- 表面状态机：pending/current、commit、frame callback。

**退出标准**：

1. `foot`、`weston-terminal`、GTK4、Qt6 应用可显示与输入。
2. 剪贴板双向可用，客户端崩溃后不残留。
3. 多输出坐标与缩放正确。
4. `wayland-info` 无报错。

### 3C — 桌面协议完善（4–6 周）

- `wp_viewporter`、`wp_fractional_scale_v1`、`wp_presentation_time`。
- `xdg_decoration`、`xdg_activation`、`zwlr_layer_shell_v1`。
- `wp_primary_selection`、`zwp_primary_selection`。
- 指针约束、相对指针、快捷键抑制、idle inhibit。
- 协议错误诊断与 fuzz 覆盖扩展。

**退出标准**：

1. 面板/锁屏/背景可通过 layer-shell 工作。
2. 高 DPI 与 fractional scale 下无模糊/错位。
3. 协议 fuzz 覆盖至少 12 个对象，无 crash。
4. 客户端异常断开不会导致合成器崩溃或资源泄漏。

### 协议范围（按优先级）

| 优先级 | 协议 | 用途 |
| --- | --- | --- |
| P0 | `wl_compositor`, `wl_surface`, `wl_region`, `wl_shm` | 表面与共享内存 |
| P0 | `wl_seat`, `wl_keyboard`, `wl_pointer`, `wl_touch` | 输入 |
| P0 | `xdg_wm_base`, `xdg_surface`, `xdg_toplevel`, `xdg_popup` | 桌面窗口 |
| P0 | `wl_output`, `zxdg_output_manager_v1` | 输出信息 |
| P0 | `wl_data_device_manager` | 剪贴板/拖放 |
| P1 | `linux-dmabuf-v1`, `zwp_linux_dmabuf_feedback_v1` | 零拷贝缓冲 |
| P1 | `wp_viewporter`, `wp_fractional_scale_v1` | 缩放与高 DPI |
| P1 | `wp_presentation_time` | 呈现反馈 |
| P1 | `xdg_decoration`, `xdg_activation` | 装饰协商与激活 |
| P1 | `zwlr_layer_shell_v1` | 面板/背景/锁屏 |
| P1 | `wp_primary_selection`, `zwp_primary_selection` | 中键选择 |
| P2 | `wp_pointer_constraints`, `wp_relative_pointer` | 游戏/远控 |
| P2 | `wp_keyboard_shortcuts_inhibit` | 虚拟机/游戏 |
| P2 | `wp_idle_inhibit`, `ext_idle_notify` | 电源管理 |
| P2 | `wp_text_input_v3`, `wp_input_method` | 输入法 |
| P2 | `ext_session_lock_v1` | 锁屏 |
| P2 | `wlr_screencopy`, `ext_image_copy_capture` | 截屏/录屏 |
| P2 | `wlr_foreign_toplevel_management` | 任务栏/切换器 |
| P3 | `wlr_gamma_control`, `wlr_output_management` | 色彩与输出配置 |

### 协议引擎风险与依赖

- **wire 兼容性**：以真实 libwayland 客户端为“黑盒规范”，任何偏差都可能导致连接失败或事件丢失。
- **fd 传递**：`SCM_RIGHTS` 必须处理部分发送、辅助数据截断、fd 泄漏；需要专门压力测试。
- **对象生命周期**：客户端可在任意时刻销毁对象；迟到事件必须被安全丢弃。
- **XML 生成器**：生成器自身是新的信任边界；生成结果需与 wayland-protocols 逐项比对。
- **协程调度**：饥饿、取消、重入、跨线程恢复是主要 bug 来源；用不变量测试与 fuzz 覆盖。
- **测试客户端**：自研 wire 层同时用于服务端与测试客户端，避免“自己验证自己”的盲区；仍需真实客户端矩阵。
- **libwayland 仅作为外部客户端/工具使用**：`wayland-info`、`weston-terminal` 等测试工具可以链接 libwayland，但 powerwm 本体不链接、不包含其头文件。

### 不在本阶段范围

- XWayland、portal、截屏、输入法（Phase 6）。
- 完整 wlr-protocol 兼容；只实现有真实用例的协议。
- 多 GPU / HDR / VRR。

---

## 6. Phase 4 — 秩序

**目标**：实现窗口管理策略，让桌面“可用”而不只是“能显示”。

### 交付物

- 平铺布局引擎：`master-stack`、`dwindle`、`spiral`，布局可插拔。
- 浮动层：对话框、固定尺寸窗口、画中画。
- 工作区：每输出独立工作区、跨输出移动、持久化。
- 焦点模型：点击聚焦、焦点跟随鼠标（可选）、焦点栈、紧急提示。
- 窗口规则：按 app_id/title/role 匹配（浮动、工作区、大小、装饰）。
- 键位系统：模式（normal/resize/passthrough）、和弦、重复、配置热重载。
- 窗口装饰：服务端装饰与客户端装饰协商。
- IPC：Unix socket + JSON 行协议，支持查询状态、切换工作区、执行动作。
- 布局动画：移动/缩放/透明度，统一由合成器时钟驱动。
- 多显示器：主输出、镜像、扩展、输出热插拔后的窗口迁移。

### 退出标准

1. 日常操作（启动、切换、移动、关闭、全屏、浮动）无需鼠标即可完成。
2. 工作区切换与窗口移动在 120 Hz 下动画不掉帧。
3. 规则与键位热重载 < 100 ms，失败时保留上一份配置并给出错误位置。
4. IPC 可被脚本稳定驱动；并发请求不破坏状态。
5. 连续 8 小时混合负载无崩溃、无焦点丢失。

### 风险

- 平铺策略容易陷入“配置地狱”：先固定 3 种布局，插件化延后。
- 焦点/工作区状态机复杂：用属性测试覆盖不变量。
- 动画与输入延迟冲突：动画永不阻塞输入处理。

---

## 7. Phase 5 — 肌肤

**目标**：形成统一、美观、可主题化的桌面外壳。

### 交付物

- 面板：工作区、窗口标题、状态区、托盘（StatusNotifier 后续）。
- 启动器：应用索引（desktop entries）、模糊搜索、键盘优先。
- 通知守护：`org.freedesktop.Notifications`、历史、勿扰、操作按钮。
- OSD：音量、亮度、布局切换提示。
- 锁屏：`ext_session_lock_v1`、PAM 集成、模糊背景。
- 壁纸：静态/动态、每输出独立、缓存。
- 主题引擎：颜色、圆角、阴影、字体、间距 token；支持暗色/亮色。
- 动画系统：缓动、弹簧、统一时间轴、低电量/低性能降级。
- 截图/录屏 UI（与 Phase 6 后端配合）。

### 退出标准

1. 首次登录即可获得完整桌面，不依赖第三方面板。
2. 所有 shell 元素使用同一套设计 token，主题切换即时生效。
3. 动画在 60/120/144 Hz 下时间一致，不因刷新率改变速度。
4. 锁屏无法被普通客户端绕过；解锁失败有速率限制。
5. 无障碍基础：键盘可达、焦点可见、缩放跟随系统设置。

### 风险

- 外壳范围容易膨胀：面板/启动器/通知为 1.0 必需，托盘与插件延后。
- 字体与图标依赖：提供内置回退，不强制特定主题包。

---

## 8. Phase 6 — 生态

**目标**：兼容真实 Linux 桌面生态，减少“换 WM 就要换习惯”的摩擦。

### 交付物

- XWayland 集成（按需启动、缩放、剪贴板、拖放）。
- Portal：`xdg-desktop-portal-powerwm`（截屏、屏幕共享、文件选择）。
- 截屏/录屏：`ext_image_copy_capture`、`wlr_screencopy`、PipeWire 输出。
- 输入法：`text-input-v3` + `input-method-v2`，与 fcitx5/ibus 联调。
- 游戏相关：指针约束、相对指针、键盘锁定、VRR（实验）。
- 色彩：gamma、ICC（基础）、夜间模式；HDR 作为实验特性。
- 电源：idle 检测、DPMS、盖子/电源键策略（通过 logind）。
- 多 GPU / 输出热插拔增强。
- 打包：Arch AUR、Debian、Fedora COPR；Flatpak 不作为合成器分发方式。

### 退出标准

1. 常见应用矩阵（浏览器、终端、编辑器、聊天、视频、游戏）可用。
2. 屏幕共享在主流会议软件中可用（经 portal + PipeWire）。
3. 中文/日文/韩文输入法可正常候选、上屏、预编辑。
4. XWayland 应用缩放正确，无明显模糊或错位。
5. 至少 3 个发行版可一键安装。

### 风险

- Portal 与 xdg-desktop-portal 版本耦合：锁定最低版本并做能力探测。
- NVIDIA + XWayland 组合问题多：明确支持矩阵。
- HDR 生态未成熟：只做实验开关，不影响默认路径。

---

## 9. Phase 7 — 磨砺

**目标**：从“能用”到“敢每天用”，并准备好 1.0。

### 工作流

- **稳定性**：7 天 dogfood；崩溃自动收集（本地、可选上报）；恢复策略。
- **性能**：建立 CI 性能基准与预算，回归即失败；帧时间火焰图。
- **安全**：协议输入全量校验；fuzz 持续运行；最小权限；seccomp 评估。
- **可访问性**：屏幕阅读器协议、键盘导航、对比度、缩放。
- **文档**：用户手册、配置参考、迁移指南、贡献指南。
- **发布工程**：可复现构建、签名、校验和、升级/回滚说明。
- **代码质量**：死代码清理、API 稳定化、依赖审计。

### 1.0 退出标准

1. 连续 7 天日常使用，无 P0/P1 缺陷。
2. 性能预算全部达标（见 [testing.md](testing.md)）。
3. 支持矩阵内的 GPU/发行版完成安装与升级测试。
4. 配置向后兼容策略明确；破坏性变更有一版本弃用期。
5. 安全审计完成，已知高危问题清零。
6. 文档覆盖安装、配置、键位、IPC、排障。

---

## 10. 版本里程碑

| 版本 | 对应阶段 | 内容 |
| --- | --- | --- |
| `v0.0.x` | P0 | 工程化、CI、测试骨架、无功能保证 |
| `v0.1` | P1 | 稳定 DRM/KMS 输出、输入、VT 切换 |
| `v0.2` | P2 | Vulkan/vulkanpp 渲染器、场景图、损坏跟踪 |
| `v0.3` | P3 | 自研 wire protocol、协程协议引擎、真实客户端可用 |
| `v0.4` | P4 | 平铺布局、工作区、IPC |
| `v0.5` | P5 | 面板、启动器、通知、锁屏、主题 |
| `v0.6`–`v0.9` | P6 | XWayland、portal、输入法、色彩、打包 |
| `v1.0` | P7 | 稳定、可日常、可安装、有支持矩阵 |

预发布阶段遵循 SemVer 的精神：`0.x` 不承诺 ABI/配置兼容，但每次破坏性变更必须写入 `CHANGELOG.md`。

---

## 11. 明确不做（1.0 之前）

- 不做 X11 窗口管理器（不替换 i3/openbox 的 X11 模式）。
- 不基于 wlroots，也不提供 wlroots 插件 ABI。
- 不链接 libwayland，也不提供 libwayland 兼容层/ABI。
- 不使用 EGL/GLES 渲染路径（headless 软件光栅仅用于测试）。
- 不支持 Windows/macOS/移动端。
- 不追求“支持所有 Wayland 协议”，只实现有真实用例的协议。
- 不在 1.0 前承诺稳定插件 ABI 或脚本运行时。
- 不把 HDR、VRR、多 GPU 作为 1.0 阻塞项。

---

## 12. 风险登记

| 风险 | 概率 | 影响 | 缓解 |
| --- | --- | --- | --- |
| C++26 反射/模块工具链不稳定 | 中 | 高 | 反射隔离在 `base/meta.hpp`；核心路径提供非反射回退；CI 固定 GCC 16 |
| 自研 Wayland wire 协议不兼容 | 高 | 高 | 以真实客户端为黑盒规范；wire 单元测试 + 24h fuzz；自研测试客户端 |
| 协议 XML 生成器缺陷 | 中 | 高 | 生成结果与 wayland-protocols 逐项比对；生成器自身测试 |
| 协程调度/取消缺陷 | 中 | 高 | 不变量测试、取消压力测试、跨线程恢复断言 |
| Wayland 协议范围失控 | 高 | 中 | 按真实客户端需求排序；每阶段设协议白名单 |
| Vulkan 外部内存/修饰符兼容性 | 高 | 高 | 能力矩阵、线性图像回退、headless lavapipe 测试 |
| GPU 驱动差异导致渲染异常 | 高 | 高 | 能力探测 + 回退；headless 金图；硬件矩阵 |
| 禁用异常后第三方库不兼容 | 中 | 中 | 选型时验证 `-fno-exceptions`；Catch2 配置专用宏；封装层隔离 |
| 输入/焦点状态机缺陷 | 中 | 高 | 属性测试 + 不变量断言 + 真实客户端回归 |
| 性能回归悄无声息 | 中 | 中 | CI 基准阈值；帧时间统计常驻 |
| 单人长期维护疲劳 | 中 | 高 | 小步交付、文档化、自动化、定期 dogfood |
| 安全漏洞（协议/权限） | 低 | 高 | fuzz、输入校验、最小权限、审计 |
| 发行版打包碎片化 | 中 | 中 | 先支持 2–3 个主流发行版；容器化构建 |

---

## 13. 审视节奏

- **每周**：更新进行中任务的阻塞点；CI 失败清零。
- **每阶段结束**：对照退出标准做演示（demo or die），未达标不开下一阶段。
- **每季度**：重排路线图优先级；新增/废弃 ADR；复核性能预算。
- **每个大版本**：写复盘：哪些估算错了、哪些抽象泄漏了、下阶段如何避免。

> 路线图不是合同，而是**减少决策疲劳的工具**。任何偏离都应记录原因，而不是悄悄发生。
