# Aedan's Playground — session handoff

Paste this file into a new session to resume. The plan is `ROADMAP.md`;
build, run and troubleshooting instructions are in `README.md`.

**Status:** the demo is playable. Keyboard and mouse input flow from GLFW
callbacks through a per-frame snapshot and a fixed-step latch to a
data-driven action map; a player sprite moves with WASD/arrows and fires
with Space/left mouse. The game world has fixed bounds of its own, and the
window only views it (scaled to fit). Verified end to end with real X11
keystrokes.

- Repository `colleenneug/Claude-01`, branch `claude/relaxed-cray-5kc2fl`, no pull request open.
- Standing guidelines: modern C++20 (smart pointers, RAII, `std::span`,
  designated initialisers); no over-engineering; clean, commented,
  production-ready code; strict error handling and logging for graphics
  initialisation.
- Decisions: C++20 with `tl::expected`; Windows and Linux only; namespace
  `playground`, targets `playground_*`; `Renderer` interface in the render
  layer; own sparse-set ECS; raw input in `platform/`, actions in `engine/`.
- Numbering: the user's milestone numbers run ahead of `ROADMAP.md`'s.
  Roadmap Milestone 3 (engine core) is now done apart from frame arenas;
  roadmap Milestone 2 (GPU memory, buffers, depth, camera) is still not done.
- The user may be young or new to this: when asked to explain, keep it short
  and plain, one idea at a time.

---

## 1. Current Architecture State

```
app/main.cpp   Application + VulkanRenderer; ecs::World with 200 bouncers + player
               setFixedUpdate(step, input): snapshot -> input -> Fire -> movement -> bounce(bounds) -> lifetime -> flush
               setUpdate(time, input): Escape quits; FPS in title
               setExtract(frame, alpha): view = world bounds; playfield; interpolated sprites
  engine/      Application (loop, accumulator, InputLatch, hooks); ActionMap; WorldBounds;
               components (Transform, Velocity, Sprite, PlayerTag, Lifetime); systems
  ecs/         Entity, ComponentPool<T>, World
  render/      Renderer, FrameData (+ viewHalfExtent, fitView); VulkanRenderer (private)
  gpu/         Context, Swapchain, DeviceHandle, VulkanCheck
  platform/    Platform, Window (GLFW callbacks: key, mouse, cursor, focus), Input
  core/        Result, Log, Assert, FrameClock, FixedStep, Types
```

**The frame** (`Application::run`): events → stop check → clock tick **and
one input snapshot** (`window.input().takeSnapshot()`, then
`inputLatch.beginFrame(snapshot)`) → 0..n fixed steps, each
`fixedUpdate(step, latch.current())` then `latch.consumeEdges()` → `update(time,
snapshot)` → `extract(frame, alpha)` → render.

**Input** (`platform/Input.h`, no GLFW in it):
- `InputCollector`: `keyEvent`, `mouseButtonEvent`, `cursorMoved`,
  `focusLost`, `takeSnapshot`. Edges are set only on real state changes
  (duplicate downs and stray ups are ignored; GLFW key repeats are dropped in
  the callback). `focusLost()` moves every held key/button to `released`.
- `InputState`: `down/pressed/released` for `Key` and `MouseButton`
  (bitsets), cursor position. A tap within one frame = pressed + released,
  not down.
- `InputLatch`: `beginFrame` keeps held state from the newest snapshot and
  ORs in undelivered edges; `consumeEdges` after each step. Result: held keys
  in every step, each press/release in exactly one step, none lost on
  zero-step frames.
- `Window.cpp`: `registerInputCallbacks` (key, mouse button, cursor, focus);
  `translateKey` maps GLFW codes to `Key`. `Window::input()`, `isFocused()`.

**Actions** (`engine/Actions.h`): `ActionMap` with named button actions
(`std::variant<Key, MouseButton>` sources) and 2D axes (`AxisKeys`),
string_view lookup via transparent hashing; `defaults()` = `Move`
(WASD + arrows) and `Fire` (Space + left mouse). Axis values are clamped per
component and normalised to length ≤ 1. Querying an unbound action asserts in
debug builds.

**World and view**: `engine::WorldBounds{halfSize}` (demo: 800×450, centred).
`bounceSystem(world, bounds)` has no access to the window.
`FrameData::viewHalfExtent` = the world area to show; the renderer computes
`render::fitView(viewportHalf, required)` (scale to fit, no stretch) and
passes the result as the shader's `viewportHalf`.

**Verified this session** (GCC 13.3, Clang 18.1, GCC + ASan + UBSan; Mesa
llvmpipe under Xvfb):

| Check | Result |
|---|---|
| Baseline at `3a128f3` before changes | 7/7 suites passed |
| Build, warnings as errors | 0 warnings, GCC and Clang |
| `xvfb-run -a ctest` | 9/9 suites in all three builds: core, ecs (24 cases), **input (13, new)**, app (14), gpu, render, **keyboard_e2e (new)**, app_smoke, app_no_display |
| Real keystrokes (xdotool → X11 → GLFW) | hold D 1 s → player at x≈366–372, y=0; two Space taps → exactly 2 bullets; Escape → "quit requested", exit 0 |
| Window resized to 900×900 | world drawn 506 px tall, centred (rows 197–702), exactly as `fitView` predicts; swapchain recreated, 0 validation messages |
| Repeatability of keyboard_e2e | 12/12 standalone and 6/6 full-suite runs after the focus check was added |

**Not yet run on a real GPU, and not on Windows.**

## 2. Exact Files & Classes Modified

New:

| File | Contents |
|---|---|
| `platform/include/playground/platform/Input.h`, `platform/src/Input.cpp` | `Key`, `MouseButton`, `describe(Key)`, `InputState`, `InputCollector`, `InputLatch` |
| `engine/include/playground/engine/Actions.h`, `engine/src/Actions.cpp` | `actions::kMove`, `actions::kFire`, `InputSource`, `AxisKeys`, `ActionMap` |
| `engine/include/playground/engine/WorldBounds.h` | `WorldBounds` |
| `tests/input_tests.cpp` | 13 tests: edge transitions, in-frame tap, duplicate/stray events, mouse, focus loss (×2), latch multi-step consumption, zero-step carry-over, tap across zero-step frames, Move axis (normalised, cancelling, not doubled), Fire sources, custom bindings, key names |
| `tests/e2e/keyboard.sh` | Real-keystroke test driven by xdotool (skips with 77 without a display or xdotool) |

Changed:

| File | Change |
|---|---|
| `platform/include/playground/platform/Window.h`, `platform/src/Window.cpp` | `input()`, `isFocused()`; `State` holds an `InputCollector` and focus flag; `registerInputCallbacks`; `translateKey` |
| `platform/CMakeLists.txt` | `Input.cpp` |
| `engine/include/playground/engine/Application.h`, `engine/src/Application.cpp` | `FixedUpdateFn(step, input)`, `UpdateFn(time, input)`; one snapshot per frame; `InputLatch` around the fixed steps; stale events dropped at the start of `run()` |
| `engine/include/playground/engine/Components.h` | `PlayerTag{speed}`, `Lifetime{secondsLeft}` |
| `engine/include/playground/engine/Systems.h`, `engine/src/Systems.cpp` | `inputSystem`, `lifetimeSystem`; `bounceSystem(world, const WorldBounds&)` |
| `engine/CMakeLists.txt` | `Actions.cpp` |
| `render/include/playground/render/FrameData.h` | `FrameData::viewHalfExtent`, `fitView()`; comments say world units |
| `render/src/VulkanRenderer.cpp`, `render/shaders/sprite.vert` | Push the fitted view instead of the raw viewport half-size |
| `app/main.cpp` | `WorldBounds` 800×450; `ActionMap::defaults()`; `spawnPlayer`, `spawnBullet`; systems in fixed update; Escape quits; playfield sprite; view = bounds; exit summary line "player ended at x=…, y=…; N bullets fired" |
| `tests/ecs_tests.cpp` | +4: inputSystem, lifetimeSystem, world-bounds bouncing independent of window, `fitView` |
| `tests/app_tests.cpp` | +2 real-loop tests (press reaches one fixed step and one frame update; held key reaches all steps; focus loss releases); callbacks take input |
| `tests/render_tests.cpp` | Update callback takes input |
| `tests/CMakeLists.txt` | `input_tests`; `keyboard_e2e` (defined before the sanitizer block, which passes the Mesa preload to the game only via `PLAYGROUND_APP_PRELOAD`) |
| `README.md` | Controls, Input, World bounds and the view; `glslc`, `libglm-dev`, `xdotool` in the Linux package list |

## 3. Code Snippet of the Core Loop/Active Component

The latch, `platform/src/Input.cpp`:

```cpp
void InputLatch::beginFrame(const InputState& frame) noexcept {
  // Held state and cursor: always the newest. Edges: the newest plus any
  // the previous frame did not get to deliver (it ran no fixed step).
  InputState next = frame;
  next.keys_.pressed |= state_.keys_.pressed;
  next.keys_.released |= state_.keys_.released;
  next.mouse_.pressed |= state_.mouse_.pressed;
  next.mouse_.released |= state_.mouse_.released;
  state_ = next;
}

void InputLatch::consumeEdges() noexcept {
  state_.keys_.pressed.reset();
  state_.keys_.released.reset();
  state_.mouse_.pressed.reset();
  state_.mouse_.released.reset();
}
```

How the loop uses it, `engine/src/Application.cpp`:

```cpp
const FrameTime time = clock_.tick();
const platform::InputState frameInput = window_->input().takeSnapshot();
inputLatch_.beginFrame(frameInput);

const FixedStepPlan plan = accumulator_.advance(time.rawDeltaSeconds);
for (u32 i = 0; i < plan.steps; ++i) {
  if (fixedUpdate_) {
    fixedUpdate_(FixedStep{.deltaSeconds = accumulator_.stepSeconds(), .index = fixedStepsRun_},
                 inputLatch_.current());
  }
  inputLatch_.consumeEdges();  // presses and releases reach one step only
  ++fixedStepsRun_;
}
lastAlpha_ = plan.alpha;
if (update_) update_(time, frameInput);
```

The game side, `app/main.cpp`:

```cpp
application.setFixedUpdate([&](const FixedStep& step, const platform::InputState& input) {
  const auto deltaSeconds = static_cast<f32>(step.deltaSeconds);
  engine::snapshotTransforms(world);
  engine::inputSystem(world, actions, input);
  if (actions.pressed(input, engine::actions::kFire)) {
    spawnBullet(world, world.get<engine::Transform>(player).position);
    ++bulletsFired;
  }
  engine::movementSystem(world, deltaSeconds);
  engine::bounceSystem(world, bounds);
  engine::lifetimeSystem(world, deltaSeconds);
  world.flush();
});
```

## 4. Known Bugs & Incomplete Tasks

No known bugs.

**Watch**
- `keyboard_e2e` failed once, early, in a GCC full-suite run whose output was
  not captured; the likely cause (keystrokes sent before the window had
  focus, with no window manager) is now guarded: the script confirms focus
  before typing and keeps the game's output (`keyboard_e2e.log.out`) for any
  future failure. It has passed every run since (12 standalone, 6 full suite,
  plus the Clang and sanitizer suites).

**Not verified**
- A real GPU; Windows (presets, vcpkg incl. GLM 1.0, Vulkan SDK, MSVC warnings).
- A real focus change through GLFW's focus callback: the guard logic is
  tested through `InputCollector::focusLost()` (unit and real-loop tests), but
  Xvfb has no window manager to take focus away. Alt+Tab on a desktop is the
  remaining check.
- Gamepads: not supported yet.

**Limitations, acceptable for now**
- `xdotool` was added to the cloud environment's setup script at the end of
  this session; until a fresh container proves it, a container without it
  skips `keyboard_e2e` (install with `apt-get install -y xdotool`).
- Cursor position is in window pixels; there is no window → world mapping
  yet (needed for aiming at the mouse).
- Bindings live in code (`ActionMap::defaults()`); loading them from a
  settings file is not written.
- Bullets fire straight up; the player has no facing direction.
- Carried over: `Transform::scale` not interpolated; one draw call per
  sprite; swapchain recreation waits for idle; fences, not timeline
  semaphores; no Vulkan debug names; no pipeline cache; GCC sanitizer builds
  preload the Mesa driver for GPU tests (driver leak-report artefact).

## 5. Immediate Next Steps for Next Weekend

1. **Cursor in world space**: `render::screenToWorld(cursor, viewportPixels,
   viewHalfExtent)` next to `fitView` in `render/FrameData.h`, with tests;
   then aim bullets at the mouse (needs a facing or aim direction on the
   player).
2. **Roadmap Milestone 2, renderer foundations**, starting with GPU memory:
   - `gpu/include/playground/gpu/Allocator.h` + `gpu/src/Allocator.cpp`:
     Vulkan Memory Allocator (vcpkg `vulkan-memory-allocator`; on Ubuntu,
     `libvma-dev` if packaged, else vendor the single header, pinned), owned
     by `gpu::Context`.
   - `gpu::Buffer` (RAII, VMA-backed) and a staging upload.
   - Draw every sprite with **one instanced draw** from a per-frame storage
     buffer instead of one push-constant draw per sprite; then a depth
     buffer and a camera.
3. Optional: bindings from a file in the project's existing `.cfg` style.

**To resume, tell the next session:** "Read `aedans-playground/HANDOFF.md` on
branch `claude/relaxed-cray-5kc2fl`, confirm `xvfb-run -a ctest --preset
debug` passes (reinstall the packages in `README.md` if the setup script did
not; `xdotool` is needed for `keyboard_e2e`), then start step 1."
