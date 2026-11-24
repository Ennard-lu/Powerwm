# ADR-0007: Vulkan + 内部 vulkanpp 封装

- 状态：Accepted
- 日期：2026-11-24
- 决策者：powerwm 维护者
- 相关：[architecture.md](../architecture.md)、[ADR-0003](0003-renderer-backend.md)、[ADR-0008](0008-no-exceptions.md)、[ADR-0009](0009-c-api-wrapping.md)

## 背景

原 ADR-0003 选择 EGL/GLES3 作为首选渲染后端、Vulkan 后续。项目现在决定：

- 渲染后端直接采用 **Vulkan**；
- 通过内部 `vulkanpp` 封装层使用 Vulkan C API；
- 不使用 EGL/GLES 作为渲染路径。

`vulkanpp` 在本文中按**项目内部模块**理解：位于 `src/render/vulkanpp/`，对 `VkInstance`、`VkDevice`、`VkQueue`、`VkCommandPool`、`VkImage`、`VkSemaphore`、`VkFence` 等提供 `create`/`destroy` + RAII。若后续决定采用外部同名库，需新增 ADR 记录来源、版本与集成方式。

## 决策

1. 唯一硬件渲染后端：**Vulkan**。
2. 所有 Vulkan C API 由 `vulkanpp` 封装：
   - 业务层不出现 `Vk*` 类型或 `vk*` 函数；
   - 创建失败返回 `result<raii<T>>`；
   - 销毁幂等、`noexcept`。
3. 呈现路径：
   - Vulkan 渲染到可导出 `VkImage`；
   - 通过 `VK_KHR_external_memory_fd` / `VK_EXT_external_memory_dma_buf` 导出 DMA-BUF；
   - 使用 `VK_EXT_image_drm_format_modifier` 协商修饰符；
   - 交给 DRM/KMS 扫描输出。
4. 客户端缓冲：DMA-BUF 导入为 `VkImage`；不兼容时请求重绘或走软件回退。
5. headless 测试使用 **lavapipe/llvmpipe**，CI 不依赖真实 GPU。
6. 不使用 EGL/GLES/GBM 作为渲染路径；GBM 仅可作为早期点灯或特殊缓冲分配的临时工具。

## 后果

### 正面

- Vulkan 提供显式同步、内存控制与现代 GPU 特性，符合高性能目标。
- 统一 RAII/错误码模型，避免 GL 全局状态与隐式错误。
- 显式 DMA-BUF 导出/导入路径，便于零拷贝合成。
- 可通过 validation layer 在 debug/CI 中捕获资源错误。

### 负面 / 代价

- Vulkan 初始化与样板代码显著多于 GLES；`vulkanpp` 自身需要设计。
- 驱动在外部内存、修饰符、格式上的支持差异大。
- 早期 bring-up 更慢；调试复杂度更高。
- 需要维护 headless/lavapipe 与真实 GPU 两套验证路径。

### 风险与缓解

| 风险 | 缓解 |
| --- | --- |
| 外部内存扩展缺失 | 能力探测；线性图像/软件回退 |
| 修饰符不兼容 | 协商 + 回退；金图测试 |
| `vulkanpp` 抽象泄漏 | 只暴露项目类型；C API 严格封装 |
| 验证层性能开销 | release 关闭，debug 可开关 |
| 启动延迟 | 延迟初始化、缓存设备/管线信息 |

## 备选方案

| 方案 | 优点 | 缺点 | 为何未选 |
| --- | --- | --- | --- |
| EGL/GLES3 | 驱动覆盖广、开发快 | 全局状态、显式控制弱 | 项目要求 Vulkan |
| Vulkan + Vulkan-Hpp | 官方 C++ 绑定 | 仍是薄封装，异常/RAII 模型不完全匹配 | 无法满足 C API 封装规则 |
| wgpu/Dawn | 跨平台 | 运行时/依赖重，控制力弱 | 与原生目标不符 |
| 软件渲染 | 确定性好 | 性能不足 | 仅测试/回退 |

## 重新审视条件

- Vulkan 在目标硬件/发行版上无法满足帧预算或稳定性要求。
- 外部内存/修饰符路径在主流驱动上不可用，且回退成本过高。
- `vulkanpp` 维护成本超过收益，或外部库能显著降低复杂度。
