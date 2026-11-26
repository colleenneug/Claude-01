# Aedan's Playground — session handoff

Paste this file into a new session to resume. The plan is `ROADMAP.md`;
build, run and troubleshooting instructions are in `README.md`.

**Status:** the simulation now runs in fixed 1/120 s steps, separate from
rendering, and sprites are drawn interpolated between steps. The log file is
written live. The ECS demo (200 bouncing, spinning sprites) runs on top.
Screenshot of the demo: `docs/ecs-sprites.png`.

- Repository `colleenneug/Claude-01`, branch `claude/relaxed-cray-5kc2fl`, no pull request open.
- Standing guidelines: modern C++20 (smart pointers, RAII, `std::span`,
  designated initialisers); no over-engineering; clean, commented,
  production-ready code; strict error handling and logging for graphics
  initialisation.
- Decisions: C++20 with `tl::expected`; Windows and Linux only; the
  Playground lives in this repository; minimum GPU Vulkan 1.3 with descriptor
  indexing; namespace `playground`, targets `playground_*`; the `Renderer`
  interface lives in the render layer below the engine; the ECS is our own
  sparse set (the old `engine/` blueprint is untouched reference).
- Numbering: the user's milestone numbers run ahead of `ROADMAP.md`'s. This
  session finished the fixed-timestep part of roadmap Milestone 3. Roadmap
  Milestone 2 (GPU memory, vertex buffers, depth, camera) is still not done.
- The user may be young or new to this: when asked to explain, keep it short
  and plain, one idea at a time.

---

## 1. Current Architecture State

```
app/main.cpp   logging -> Application -> createVulkanRenderer -> setRenderer
               -> ecs::World + spawnBouncingSprites
               -> setFixedUpdate (snapshot, movement, bounce, flush)
               -> setUpdate (FPS in title) -> setExtract (interpolated sprites) -> run
  engine/      Application (loop, fixed-step accumulator, hooks); Components; Systems
  ecs/         Entity, ComponentPool<T>, World            (depends only on core)
  render/      Renderer interface, FrameData (public, no Vulkan); VulkanRenderer (private)
  gpu/         Context, Swapchain, DeviceHandle, VulkanCheck
  platform/    Platform (GLFW, Ctrl+C), Window
  core/        Result, Log, Assert, FrameClock, FixedStep, Types
```

**The frame** (`engine::Application::run`):
1. Events (poll; sleep to 60 fps if no renderer or minimised).
2. Stop check (Ctrl+C/SIGTERM, `requestQuit`, window closed).
3. `FrameClock::tick()` → `FrameTime`.
4. **Simulate:** `FixedStepAccumulator::advance(rawDeltaSeconds)` →
   `FixedStepPlan{steps, alpha, droppedSeconds}`; the `FixedUpdateFn` runs
   `steps` times with `FixedStep{deltaSeconds = 1/120, index}`.
5. **Update:** `UpdateFn(FrameTime)` once.
6. **Extract:** `ExtractFn(FrameData&, alpha)` (rendered frames only).
7. **Render:** `renderer->renderFrame(time, frameData)`.
8. Frame limit.

**Fixed-step accumulator** (`core/FixedStep.h`, pure arithmetic, no clock):
step 1/120 s; each frame's delta clamped to `maxFrameSeconds` (0.25 s, from
`ApplicationConfig::maxDeltaSeconds`) before it is added; at most
`maxStepsPerFrame` (8) steps per frame, with whole steps beyond the cap
dropped and the fraction kept; `alpha = accumulated / step` in [0, 1). The
periodic debug stats line reports steps per second, and a warning reports
dropped time.

**Interpolation** (`engine::Transform`): `previousPosition`,
`previousRotation`, `hasPrevious`. `snapshotTransforms` (first in every fixed
step) copies current → previous and sets `hasPrevious`. `extractSprites(world,
frame, alpha)` draws `glm::mix(previous, current, alpha)`; rotation is blended
the short way round (`std::remainder` by 2π), so wrapped angles do not spin
backwards. While `hasPrevious` is false (a freshly spawned entity, made any
way, before its first snapshot) it is drawn at its current state: no streak
from (0, 0). `Transform::teleport(pos, rot)` sets both states.

**Logging** (`core/Log`): `Config::flushLevel`, default `kDefaultFlushLevel`
= Trace in debug builds (every line flushed), Info in release. `tail -f`
is live.

**Verified this session** (GCC 13.3, Clang 18.1, GCC + ASan + UBSan; Mesa
llvmpipe under Xvfb):

| Check | Result |
|---|---|
| Baseline before changes | 7/7 suites passed |
| Build, warnings as errors | 0 warnings, GCC and Clang |
| `xvfb-run -a ctest` | 7/7 suites in all three builds (core 22 cases, ecs 20, app 12, gpu 7, render 4, plus the two app runs) |
| Real run, 5 s | 754 frames (150.8 fps) and exactly 600 simulation steps (120.0 per second) |
| Live log | "entering main loop" readable in the file while running (previously ~30 s late) |
| Determinism | 120 steps at 30 fps and at 144 fps give bit-identical positions |
| Validation | 0 messages |

**Not yet run on a real GPU, and not on Windows.**

## 2. Exact Files & Classes Modified

New:

| File | Contents |
|---|---|
| `core/include/playground/core/FixedStep.h`, `core/src/FixedStep.cpp` | `FixedStep`, `FixedStepPlan`, `FixedStepAccumulator` |

Changed:

| File | Change |
|---|---|
| `core/include/playground/core/Log.h`, `core/src/Log.cpp` | `kDefaultFlushLevel`, `Config::flushLevel`, `flush_on(config.flushLevel)` |
| `core/CMakeLists.txt` | `FixedStep.cpp` |
| `engine/include/playground/engine/Application.h`, `engine/src/Application.cpp` | `FixedUpdateFn`, `setFixedUpdate`; `ExtractFn` takes `f64 alpha`; `ApplicationConfig::fixedStepSeconds`, `maxFixedStepsPerFrame` (validated in `create`); accumulator in `run`; `fixedStepsRun()`, `lastAlpha()`; stats include steps and dropped time |
| `engine/include/playground/engine/Components.h` | `Transform::previousPosition`, `previousRotation`, `hasPrevious`, `teleport()` |
| `engine/include/playground/engine/Systems.h`, `engine/src/Systems.cpp` | `snapshotTransforms`; `extractSprites(world, frame, alpha)`; `interpolatePosition`, `interpolateRotation` |
| `app/main.cpp` | Systems moved into `setFixedUpdate`; FPS counter in `setUpdate`; extract passes `alpha` |
| `tests/core_tests.cpp` | +8: accumulator (60 fps → 2 steps, 240 Hz alternation, carry-over, clamp, cap, conservation over uneven frames, edge cases); live log flush |
| `tests/ecs_tests.cpp` | +6: snapshot, blend by alpha, no spawn streak, teleport, rotation wrap, 30 vs 144 fps determinism |
| `tests/app_tests.cpp` | +3: exact contiguous steps and time bookkeeping, per-frame order (steps → update → extract) and alpha range, bad settings rejected |
| `tests/render_tests.cpp` | Extract lambda takes alpha |
| `README.md` | Frame pipeline section, live log note |

## 3. Code Snippet of the Core Loop/Active Component

The accumulator, `core/src/FixedStep.cpp`:

```cpp
FixedStepPlan FixedStepAccumulator::advance(f64 frameSeconds) noexcept {
  FixedStepPlan plan;

  // Guard 1: clamp the frame before it reaches the accumulator.
  const f64 frame = std::max(frameSeconds, 0.0);
  const f64 clamped = std::min(frame, maxFrameSeconds_);
  plan.droppedSeconds = frame - clamped;
  accumulated_ += clamped;

  while (accumulated_ >= stepSeconds_ && plan.steps < maxStepsPerFrame_) {
    accumulated_ -= stepSeconds_;
    ++plan.steps;
  }

  // Guard 2: whole steps still owed after the cap are dropped; the fraction is kept.
  if (accumulated_ >= stepSeconds_) {
    const f64 leftover = std::fmod(accumulated_, stepSeconds_);
    plan.droppedSeconds += accumulated_ - leftover;
    accumulated_ = leftover;
  }

  plan.alpha = std::clamp(accumulated_ / stepSeconds_, 0.0, 1.0);
  return plan;
}
```

How the loop uses it, `engine/src/Application.cpp`:

```cpp
const FrameTime time = clock_.tick();

// 4. Simulate: as many fixed steps as real time has paid for.
const FixedStepPlan plan = accumulator_.advance(time.rawDeltaSeconds);
for (u32 i = 0; i < plan.steps; ++i) {
  if (fixedUpdate_) fixedUpdate_(FixedStep{.deltaSeconds = accumulator_.stepSeconds(), .index = fixedStepsRun_});
  ++fixedStepsRun_;
}
lastAlpha_ = plan.alpha;

// 5. Update, once per frame.
if (update_) update_(time);

// 6 and 7. Extract (interpolated by alpha) and render.
if (renderer_ && !window_->isMinimized()) {
  if (Status frame = renderFrame(time, plan.alpha); !frame) { /* log, stop, return the error */ }
}
```

The game side, `app/main.cpp`:

```cpp
application.setFixedUpdate([&](const FixedStep& step) {
  engine::snapshotTransforms(world);
  engine::movementSystem(world, static_cast<f32>(step.deltaSeconds));
  engine::bounceSystem(world, halfSizeOf(application.window().framebufferSize()));
  world.flush();
});
application.setUpdate([&fpsCounter](const FrameTime& time) { fpsCounter.update(time); });
application.setExtract(
    [&world](render::FrameData& frame, f64 alpha) { engine::extractSprites(world, frame, alpha); });
```

## 4. Known Bugs & Incomplete Tasks

No known bugs.

**Not verified**
- A real GPU; Windows (presets, vcpkg incl. GLM 1.0, Vulkan SDK, MSVC warnings).
- Minimise/restore on a real desktop (Xvfb has no window manager).
- No automated pixel check; screenshots checked by hand and by script.

**Known limitations, acceptable for now**
- `Transform::scale` is not interpolated (nothing animates scale yet).
- `bounceSystem` reads the window size inside the fixed step; resizing the
  window mid-game moves the walls, so the simulation is deterministic only
  for a fixed window size. Game-world bounds should not come from the window.
- One draw call per sprite (push constants); thousands need roadmap M2 (VMA,
  instanced draw from a buffer).
- No input layer yet.
- In release builds, debug/trace lines (if enabled with `--verbose`) are
  still buffered by design.
- Carried over: swapchain recreation uses `vkDeviceWaitIdle`; fences, not
  timeline semaphores; no Vulkan debug names; no pipeline cache; GCC
  sanitizer builds preload the Mesa driver for GPU tests (Mesa leak-report
  artefact, see `tests/CMakeLists.txt`).

## 5. Immediate Next Steps for Next Weekend

1. **Input** (`platform/include/playground/platform/Input.h`, `platform/src/Input.cpp`):
   per-frame key and mouse state with pressed/released edges, fed by GLFW
   callbacks registered in `Window.cpp`, sampled once per frame and handed to
   the fixed steps as a snapshot (so every step in a frame sees the same
   input). Then an action map (`Move`, `Fire`) as data. First use: a
   `PlayerTag` sprite steered with WASD by an `inputSystem` in the fixed step.
2. **Game-world bounds** instead of the window size in `bounceSystem`
   (a `WorldBounds` value owned by the game), so resizing is purely visual.
3. Then roadmap Milestone 2: VMA (`gpu/Allocator.h`), `gpu::Buffer`, staging
   upload, instanced sprites from one storage buffer, depth, camera.

**To resume, tell the next session:** "Read `aedans-playground/HANDOFF.md` on
branch `claude/relaxed-cray-5kc2fl`, confirm `xvfb-run -a ctest --preset
debug` passes (reinstall the packages in `README.md` if the setup script did
not), then start step 1, input."
