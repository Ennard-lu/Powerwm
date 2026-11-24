# Changelog

本文件记录 powerwm 的重要变更。格式参考 [Keep a Changelog](https://keepachangelog.com/zh-CN/1.1.0/)，
版本号遵循预发布阶段的 `0.x.y` 约定（1.0 后使用 SemVer）。

## [Unreleased]

### Added

- Phase 0 工程化文档：路线图、架构、编码规范、测试策略、CI/CD、ADR。
- CMake Presets：`dev`、`release`、`asan`。
- GitHub Actions：格式检查、策略扫描、Debug/Release/ASan+UBSan 构建与测试、标签发布骨架。
- 可复现开发容器 `containers/dev.Dockerfile`（GCC 16 + Vulkan）。
- CTest 冒烟测试与正式测试目录结构。
- `.clang-format`、`.clang-tidy`、`.editorconfig`、`.gitignore`。
- `tools/check_policy.sh`：无异常、无 libwayland、C API 封装、整数别名检查。
- `tools/check_comments.py`：禁止解释性注释，只允许英文 API 文档注释与 SPDX。
- `cmake/PowerWMBuildInfo.cmake`：生成 git commit、分支、构建时间、编译器与参数元数据。
- `powerwm --build-info`：打印构建元数据。
- Phase 1 起点：主输出选择、原子模式设置、双缓冲 page flip 动画、`poll` 事件等待封装。
- 依赖 DRM device 的资源改由 `device::create_*` 创建；统一使用 `auto foo() -> T` 函数声明风格。
- 英文 README；中文 README 移至 `docs/README.zh-CN.md`。
- Apache-2.0 `LICENSE` 与 SPDX 文件头。
- ADR-0006 ~ ADR-0012：自研协议栈、Vulkan/vulkanpp、无异常、C API 封装、许可证、命名空间、依赖资源创建。

### Changed

- 全仓库 C++ 代码按新 `.clang-format` 统一格式（不改变行为）。
- 目录重构：`helpers → base`、`libdrmpp → platform/drm`、`libgbmpp → platform/gbm`、应用入口 → `app/`、点灯工具 → `tools/`。
- `powerwm` 应用骨架与 `powerwm_core` 静态库拆分完成。
- `main()` 拆分为小型具名函数；业务代码函数 ≤ 60 行、嵌套 ≤ 5 层。
- 移除未使用的 `counted` 与 `placeholder`。
- 根命名空间从 `bc` 迁移为 `pwm`；`LIBDRMPP_*` 宏迁移为 `PWM_*`。
- 定宽整数别名成为项目规范：`pwm::i32` / `u32` / `usize` 等。
- 启用 `-fno-exceptions`；错误模型定为错误码 + `pwm::result<T>`。
- 渲染与协议方向更新：Vulkan + `vulkanpp`；自研 Wayland wire protocol + 协程，不依赖 libwayland。

### Known limitations

- 仍为 DRM/KMS 点灯原型；`powerwm` 仅应用骨架，不可日常使用。
- 未实现 Wayland 协议、窗口管理、Vulkan 渲染器、输入路由。
- 渲染器、协议栈、窗口管理尚未实现。
