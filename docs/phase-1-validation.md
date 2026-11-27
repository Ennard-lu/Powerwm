# Phase 1 validation checklist

> Status: implementation complete; hardware validation in progress. TTY3 passed 1200
> frames, 10 VT switches, and Wayland/KMS soak runs with steady resident memory.

## 1. Build and static checks

```bash
cmake --preset dev
cmake --build --preset dev
ctest --preset dev --output-on-failure
bash tools/check_policy.sh
python3 tools/check_comments.py
```

Expected: dev/release/asan build; `unit.smoke`, `unit.event_loop`, `unit.headless` pass;
policy and comment checks pass.

## 2. Read-only diagnostics

```bash
./build/dev/powerwm_probe
```

Expected: CRTC/connector/framebuffer/encoder counts, connected modes, property IDs. It must
not take over DRM.

Observed on this machine:

```text
crtcs=4 connectors=6 framebuffers=0 encoders=22
connector id=512 connected=true modes=2
  preferred 2880x1800@120Hz
```

## 3. Input probe

```bash
./build/dev/powerwm_input_probe
```

Within five seconds, press keys, move the pointer, and touch. Expected keyboard code/state,
pointer motion/button/axis, and touch slot/coordinates.

## 4. Seat session

Requires a TTY plus seatd or logind. On TTY3 with logind, pass the session id:

```bash
XDG_SESSION_ID=4 LIBSEAT_BACKEND=logind ./build/dev/powerwm_seat_probe
```

TTY3 result:

```text
seat enabled=false device=/dev/dri/card1 fd=6
seat enabled=true
```

Without a seat provider, `libseat_open_seat failed` is the expected fallback.

## 5. DRM animation and page flips

On TTY3:

```bash
XDG_SESSION_ID=4 LIBSEAT_BACKEND=logind ./build/dev/powerwm_drm_bringup 1200
```

The optional argument is frame count; default is 180 (~3 seconds).

Expected: smooth double-buffered animation, no tearing, VT restored on exit, libseat path
when available.

TTY3 result:

```text
presented 1200 frames
```

Ten VT switches between TTY2/TTY3 were exercised while running. Seat enable/disable and
resume worked, and all 1200 frames completed.

## 6. Hardware acceptance

| Item | Method | Pass condition |
| --- | --- | --- |
| Continuous output | 1-hour animation | no black screen, tearing, or crash |
| VT switching | 50 switches | output and input recover each time |
| Display hotplug | 10 HDMI/DP cycles | runtime restarts safely, no leaks |
| Idle CPU | 60 s sampling after animation | < 1% of one core |
| Resource release | inspect fds/DRM objects after exit | no leftovers |
| Client object churn | repeated shm pool/buffer creation | object table and RSS stay flat |
| Multi-output | two connected connectors | primary works, no crash |
| Input hotplug | plug/unplug keyboard/mouse | libinput discovers/removes devices |

## 7. Current results

| Item | Result |
| --- | --- |
| Build/unit tests | pass |
| `powerwm_probe` | pass, 2880x1800@120Hz detected |
| `powerwm_input_probe` | pass, 5-second run |
| `powerwm_seat_probe` | pass on TTY3 + logind |
| DRM animation | pass, 1200 frames |
| VT switching | pass, 10 cycles |
| Wayland + GTK4 run | pass, 900 frames after popup/enter changes |
| Wayland + GTK4 soak | pass, 3000 frames in 67 s, no protocol errors |
| Resident memory | pass, 21144 KB constant across 1500 GTK4 frames |
| Hotplug / 50 VT / 1 hour | pending |
| Vulkan DMA-BUF to KMS | pass, TTY3 presentation |

## 8. Known limitations

- Input is discovered and printed, not routed to scene focus.
- Output selection uses the first connected connector and first plane; CRTC/plane matching
  is incomplete.
- Hotplug restarts the output runtime instead of preserving client state.
- Vulkan pipeline/scene rendering is early; KMS presentation uses cleared DMA-BUF images.
- Multi-GPU, overlays, HDR, and VRR are not covered.

These are addressed in Phases 2–6.
