# powerwm 文档

本目录保存 powerwm 的长期设计、规范与流程。代码会变，决策与约束应当可追溯。

## 阅读顺序

| 顺序 | 文档 | 适合谁 | 你将了解 |
| --- | --- | --- | --- |
| 0 | [README.zh-CN.md](README.zh-CN.md) | 所有人 | 中文版项目总览 |
| 1 | [roadmap.md](roadmap.md) | 所有人 | 项目要去哪、每个阶段交付什么、如何判断完成 |
| 2 | [architecture.md](architecture.md) | 贡献者 | 分层、线程模型、帧生命周期、目录规划 |
| 3 | [coding-style.md](coding-style.md) | 贡献者 | 如何写出一致、可维护的 C++26 |
| 4 | [testing.md](testing.md) | 贡献者 | 如何证明改动正确、性能是否退化 |
| 5 | [ci-cd.md](ci-cd.md) | 维护者 | 流水线如何运行、如何发布、如何排障 |
| 6 | [phase-1-validation.md](phase-1-validation.md) | 硬件验证者 | Phase 1 验收步骤与当前结果 |
| 7 | [decisions/](decisions/) | 所有人 | 关键选型的背景、取舍与后果 |

## 文档约定

- 文档使用中文为主，代码标识符、协议名、命令保持英文。
- 架构级决策必须新增 ADR，而不是只改聊天记录或提交信息。
- 文档中的性能目标必须写明测量条件；没有测量条件的数字视为口号。
- 过期文档要么更新，要么在文首标注 **Deprecated** 并指向替代文档。
- 图表优先使用 Mermaid，保证在 GitHub/GitLab 上可直接渲染。

## 目录

```text
docs/
├── README.md              # 本文件
├── roadmap.md             # 路线图与里程碑
├── architecture.md        # 系统架构
├── coding-style.md        # 编码规范
├── testing.md             # 测试与性能策略
├── phase-1-validation.md  # Phase 1 硬件验收
├── ci-cd.md               # 持续集成与发布
└── decisions/
    ├── README.md          # ADR 索引与模板
    ├── 0001-native-compositor.md
    ├── 0002-cxx26-toolchain.md
    ├── 0003-renderer-backend.md
    ├── 0004-config-and-ipc.md
    ├── 0005-license.md
    ├── 0006-own-wayland-protocol.md
    ├── 0007-vulkan-vulkanpp.md
    ├── 0008-no-exceptions.md
    ├── 0009-c-api-wrapping.md
    ├── 0010-apache-2-0.md
    ├── 0011-namespace-and-integers.md
    ├── 0012-dependent-resource-creation.md
    └── 0013-c-api-assumes-valid.md
```

## 文档维护规则

1. **先决策，后实现**：影响多个模块的选择先写 ADR。
2. **ADRs 不删除**：被替代时保留原文，状态改为 `Superseded by ADR-XXXX`。
3. **路线图按季度审视**：调整阶段顺序需要在 PR 中说明原因。
4. **规范可执行**：能用 clang-format、clang-tidy、CI 强制的内容，不靠口头约定。
