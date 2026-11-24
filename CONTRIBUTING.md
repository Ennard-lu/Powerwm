# 贡献指南

powerwm 是一个长期项目。感谢你愿意参与。请在动手前花 10 分钟阅读以下内容。

## 1. 开始之前

1. 阅读 [README.md](README.md) 与 [docs/roadmap.md](docs/roadmap.md)，确认你的改动属于当前阶段。
2. 搜索现有 issue；没有就新开一个，说明问题、动机与初步方案。
3. 架构级变更（分层、线程模型、协议范围、依赖选择）必须先写 ADR，放在 `docs/decisions/`。
4. 小型修复可以直接提 PR，但请在描述中说明测试方式。

## 2. 开发环境

推荐使用可复现容器：

```bash
docker build -t powerwm-dev -f containers/dev.Dockerfile .
docker run --rm -it -v "$PWD:/workspace" -w /workspace powerwm-dev bash
```

本机构建：

```bash
cmake --preset dev
cmake --build --preset dev
ctest --preset dev --output-on-failure
```

要求：

- GCC 16+（C++26 反射）
- CMake ≥ 3.25
- Ninja
- libdrm 开发包、Vulkan loader/驱动（后续阶段会增加 libinput、libudev、libseat 等）
- **不安装、不链接 libwayland-***
- 全项目 `-fno-exceptions`

## 3. 提交规范

使用 Conventional Commits：

```text
feat(scene): add damage tracking for transformed nodes
fix(input): release pointer grab on client disconnect
docs(roadmap): split Phase 3 protocol table by priority
ci: cache ccache between builds
```

- 一个提交只做一件事。
- 说明**为什么**，而不只是“改了什么”。
- 破坏性变更使用 `BREAKING CHANGE:` footer。

## 4. PR 清单

- [ ] 改动范围与 issue/路线图一致。
- [ ] `clang-format` 通过。
- [ ] `bash tools/check_policy.sh` 通过（无异常、无 libwayland、C API 封装、整数别名）。
- [ ] `python3 tools/check_comments.py` 通过（无解释性注释，API 注释为英文）。
- [ ] 函数 ≤ 80 行、嵌套 ≤ 5 层；名字自明。
- [ ] Debug 与 Release 构建通过。
- [ ] 新增/修改行为有测试；bug 修复有回归用例。
- [ ] 错误路径已考虑：客户端错误不会拖垮合成器。
- [ ] 热路径没有新增分配、格式化、锁。
- [ ] 公开 API 有文档、`[[nodiscard]]`、失败条件说明。
- [ ] 架构变更附带 ADR。
- [ ] 性能相关附带测量数据（硬件、场景、方法）。
- [ ] 文档/配置示例已同步更新。

## 5. 代码审查

审查者重点关注：

1. **正确性**：生命周期、错误路径、并发边界、协议语义。
2. **分层**：依赖方向是否被破坏；抽象是否放在正确的层。
3. **可测试性**：能否在 headless 环境确定性验证。
4. **性能**：是否引入热路径分配、额外拷贝、无界增长。
5. **可维护性**：命名、注释、测试可读性。

## 6. 行为准则

- 对事不对人；用数据和代码说话。
- 欢迎新手问题；但请先阅读文档与搜索 issue。
- 不提交他人的私有代码、密钥、未授权素材。
- 性能/兼容性结论必须可复现。

## 7. 许可证

项目采用 **Apache License 2.0**（见 [`LICENSE`](LICENSE)）。贡献即表示你同意以 Apache-2.0 授权你的贡献，无需签署 CLA。

- 源文件使用 `SPDX-License-Identifier: Apache-2.0`。
- 引入第三方代码前必须确认许可证兼容，并保留原始版权/许可声明。
- 不要提交来源不明或与 Apache-2.0 不兼容的代码。
