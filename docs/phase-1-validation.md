# Phase 1 验收清单

> 状态：**实现完成，硬件验收待执行**  
> 目标：确认 DRM/KMS 输出、page flip、seat/VT、输入、热插拔与 headless 路径在真实环境可用。

## 1. 构建与静态检查

```bash
cmake --preset dev
cmake --build --preset dev
ctest --preset dev --output-on-failure
bash tools/check_policy.sh
python3 tools/check_comments.py
```

期望：

- `dev` / `release` / `asan` 全部构建成功；
- `unit.smoke`、`unit.event_loop`、`unit.headless` 全部通过；
- 策略与注释检查通过。

## 2. 只读诊断

```bash
./build/dev/powerwm_probe
```

期望：

- 打印 CRTC / connector / framebuffer / encoder 数量；
- connected connector 显示 preferred mode；
- 不接管 DRM，不影响当前桌面。

本机示例：

```text
crtcs=4 connectors=6 framebuffers=0 encoders=22
connector id=512 connected=true modes=2
  preferred 2880x1800@120Hz
```

## 3. 输入探针

```bash
./build/dev/powerwm_input_probe
```

在 5 秒内按键、移动鼠标、触摸：

- 键盘输出 key code 与 pressed/released；
- 指针输出 motion / button / axis；
- 触摸输出 slot 与坐标。

## 4. Seat 会话

需要 TTY + `seatd` 或 `logind`：

```bash
./build/dev/powerwm_seat_probe
```

期望：

- 打印 `seat enabled=true`；
- 成功打开 `/dev/dri/card*`；
- 切换 VT 时打印 enable/disable 变化；
- 切回原 VT 后恢复。

当前无 seat provider 的环境会输出：

```text
error: libseat_open_seat failed
```

这属于预期降级。

## 5. DRM 动画与 page flip

在 TTY 中运行：

```bash
sudo ./build/dev/powerwm_drm_bringup
```

期望：

- 输出双缓冲渐变动画约 3 秒；
- 无撕裂；
- 进程退出后 VT 恢复；
- 若 seat 可用，日志显示通过 libseat 打开设备；否则回退直接 DRM。

## 6. 硬件验收项

| 项目 | 方法 | 通过标准 |
| --- | --- | --- |
| 连续出图 | 运行 1 小时动画 | 无黑屏、无撕裂、无崩溃 |
| VT 切换 | 连续切换 50 次 | 每次恢复输出与输入 |
| 显示器热插拔 | 拔插 HDMI/DP 10 次 | 自动重启输出运行时，无泄漏 |
| 空闲 CPU | 停止动画后采样 60s | 单核 < 1% |
| 资源释放 | 退出后检查 fd / DRM 对象 | 无残留、无泄漏 |
| 多输出 | 接入 2 个 connected connector | 至少主输出可用，不崩溃 |
| 输入热插拔 | 插入/拔出键鼠 | libinput 自动发现/移除，不崩溃 |

## 7. 当前环境结果

| 项目 | 结果 |
| --- | --- |
| 构建 / 单测 | 通过 |
| `powerwm_probe` | 通过，检测到 2880x1800@120Hz |
| `powerwm_input_probe` | 通过，5 秒无事件退出 |
| `powerwm_seat_probe` | 失败：无 seat provider（预期降级） |
| DRM 动画 | 未执行，避免打断当前桌面会话 |
| VT / 热插拔 / 1 小时 | 待 TTY 硬件验证 |

## 8. 已知限制

- 输入尚未路由到场景/焦点，只做发现与打印。
- 输出选择使用首个 connected connector 与首个 plane，尚未做完整的 CRTC/plane 兼容性匹配。
- 热插拔通过重启输出运行时实现，尚未做到无缝保留客户端状态。
- Vulkan 渲染器尚未接入，page flip 使用 dumb buffer 测试图案。
- 多 GPU / 多平面叠加未覆盖。

这些限制进入 Phase 2/3 后逐步消除。
