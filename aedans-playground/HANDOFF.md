# Aedan's Playground — session handoff (weekend of 2026-11-15)

Paste this whole file into a new session to resume. The full plan it refers to
is `aedans-playground/ROADMAP.md` in the same folder.

**One-line status:** planning is finished and committed; **no Playground engine
code exists yet.** Next session starts Milestone 0 (project skeleton).

- Repository: `colleenneug/Claude-01`
- Branch: `claude/relaxed-cray-5kc2fl` (pushed, up to date with origin, no pull request open)
- Last commit: `d065ae6` "Aedan's Playground: architecture roadmap"

Standing coding guidelines for this project: modern C++20 (smart pointers,
RAII, `std::span`, designated initialisers); no over-engineering, subsystems
decoupled and simple; clean, well-commented, production-ready code; strict
error handling and logging for graphics API initialisation.

---

## 1. Current Architecture State

### What the Playground is

A C++20 / Vulkan engine that will host the game Erebus Cradle. Today it is a
folder with two documents. Nothing compiles from it yet.

### What already works in the repository (not written this session)

| Tree | State | Verified this session |
|---|---|---|
| `cpp/` | The working game. ~22,500 lines of C++17 on OpenGL 4.1 with GLFW, GLEW, GLM and miniaudio. Campaign, hub, open space, co-op over its own UDP layer, content in `.cfg` files under `cpp/content/`. | Read, not built. |
| `engine/` | Earlier C++20 blueprint, namespace `erebus`. Fixed-timestep loop, sparse-set ECS, player controller, weapon model and CPU clustered-light binner. No third-party dependencies. | Built with CMake 3.28 / GCC 13.3 and `erebus_engine_smoke` ran with 0 failures. |
| `engine/src/render/vulkan/VulkanDevice.cpp` | A bring-up checklist in comments. No Vulkan device exists. | Read. |
| `engine/shaders/*` | PBR, cluster-assign and volumetric shaders, written but never run on a GPU. | Not run. |

### How the existing pieces connect

- In `cpp/`, gameplay fills a `std::vector<DrawItem>` through the abstract
  `SceneSource` (`cpp/src/Scene.h`, `cpp/src/Draw.h`), and
  `Renderer::renderFrame(scene, camera, time, dt)` draws it. This is the seam
  the port will follow.
- That seam leaks in two ways, which are the real work of the port:
  `cpp/src/Gl.h` is included by nearly every header, and ten gameplay files
  call GLFW directly for input.
- `engine/` is not wired into `cpp/` at all.

### Planned Playground layering (decided, not built)

```
game/      Erebus Cradle: missions, hub, space, HUD, networking
engine/    ecs · scene · physics · audio · assets · input
render/    frame passes · materials · render world
gpu/       Vulkan: device, swapchain, resources, commands
platform/  window · raw input events · clock · filesystem
core/      log · assert · result · memory · handles · math
```

Dependencies point downward only. Each layer is a static library with its own
public include folder.

### Decisions already made

- Vulkan only; no cross-API layer (the D3D12-shaped `RHI.h` in `engine/` is not carried over).
- Vulkan 1.3 baseline: dynamic rendering, synchronization2, bindless textures.
- Reverse-Z 32-bit float depth, infinite far plane; Y-flip by negative viewport height.
- C Vulkan API with our own thin RAII wrappers; no Vulkan-Hpp.
- RAII for long-lived GPU objects; handles plus a deferred deletion queue for the rest.
- Keep the existing sparse-set ECS; no EnTT.
- Fixed-timestep simulation with interpolated rendering, taken from `engine/`.
- Physics in two stages: existing box colliders behind a query interface, then Jolt behind the same interface.
- Networking stays in `game/`, using the game's own UDP code.
- Memory: arenas per frame and per mission, handle-addressed pools, `std::unique_ptr` for subsystems, no `std::shared_ptr`.
- Errors: fallible functions return a `Result`; every `VkResult` checked; validation errors assert in debug builds.
- Libraries from the start: GLFW, GLM, volk, vk-bootstrap, Vulkan Memory Allocator, glslc, spdlog, tl::expected, Dear ImGui, miniaudio, doctest.
- Libraries added later on need: Tracy, Jolt, cgltf or fastgltf, stb_image, KTX-Software, meshoptimizer, SPIRV-Reflect, enkiTS.
- Build: CMake presets plus a vcpkg manifest.

### Milestones

| # | Milestone | Exit test | Status |
|---|---|---|---|
| 0 | Skeleton | Window opens, logs, closes cleanly; tests run | **Next** |
| 1 | Vulkan bring-up | One triangle, zero validation messages, survives resize | Not started |
| 2 | Renderer foundations | Thousands of meshes with a fly camera | Not started |
| 3 | Engine core | ECS scene at a fixed simulation rate | Not started |
| 4 | Visual parity | A mission scene matches the `cpp/` build | Not started |
| 5 | Host Erebus Cradle | Mission 1 (HOME), then hub, then co-op | Not started |
| 6 | Better results | Clustered lights, volumetrics, TAA, contact shadows, reflections | Not started |

---

## 2. Exact Files & Classes Modified

Created this session:

| File | Contents |
|---|---|
| `aedans-playground/ROADMAP.md` | The architecture roadmap (385 lines). |
| `aedans-playground/HANDOFF.md` | This file. |

No source files, classes, shaders or build files were created or changed.
Nothing under `cpp/`, `engine/`, `src/`, `server/` or `docs/` was touched.

Files read to ground the plan, and worth re-reading before writing code:

- `engine/include/erebus/core/GameLoop.h`, `engine/src/core/GameLoop.cpp`
- `engine/include/erebus/render/RHI.h`
- `engine/src/render/vulkan/VulkanDevice.cpp`
- `engine/README.md`, `engine/CMakeLists.txt`
- `cpp/src/Scene.h`, `cpp/src/Draw.h`, `cpp/src/Renderer.h`
- `cpp/CMakeLists.txt`, `cpp/README.md`

---

## 3. Code Snippet of the Core Loop/Active Component

No code was finalised this session. The block below is the **existing, tested**
loop from `engine/src/core/GameLoop.cpp` (class `erebus::GameLoop`), which the
roadmap carries into the Playground unchanged at Milestone 3. It is the closest
thing to an active core loop.

```cpp
FrameStats GameLoop::tick() {
  const Clock::time_point now = Clock::now();

  // The first tick has no previous timestamp. Treating it as "zero elapsed"
  // rather than "elapsed since epoch" is what stops the first frame after a
  // level load from running a thousand catch-up steps.
  if (!started_) {
    previous_ = now;
    started_  = true;
  }

  const f64 real = std::chrono::duration<f64>(now - previous_).count();
  previous_ = now;

  stats_ = FrameStats{};
  stats_.realDeltaSeconds = real;

  // Clamp a single frame's contribution before it ever reaches the
  // accumulator. A breakpoint in a debugger produces an elapsed time of
  // minutes; without this the next tick tries to simulate all of it.
  const f32 clampedReal = std::min(static_cast<f32>(real), 0.25f);
  accumulator_ += clampedReal * timeScale_;

  u32 steps = 0;
  while (accumulator_ >= kFixedDelta) {
    if (steps >= maxSteps_) {
      // Catch-up cap reached. Drop the surplus: running slow is recoverable,
      // teleporting after a hitch is not.
      accumulator_ = 0.0f;
      stats_.clamped = true;
      break;
    }
    if (simulate_) simulate_(kFixedDelta);
    accumulator_ -= kFixedDelta;
    ++steps;
  }

  stats_.simulationSteps = steps;
  // How far past the last simulated state the display is. Render systems
  // interpolate previous->current transforms by this.
  stats_.interpolation = accumulator_ / kFixedDelta;

  if (render_) render_(stats_.interpolation, stats_);
  return stats_;
}

void GameLoop::run(const std::function<bool()>& shouldContinue) {
  while (running_) {
    if (shouldContinue && !shouldContinue()) break;
    tick();
  }
}
```

Facts about it: `kFixedDelta` is 1/120 s; `maxSteps_` defaults to 8;
`setTimeScale(0)` pauses simulation while rendering continues; `simulate_` and
`render_` are `std::function` callbacks passed to the constructor.

The planned per-frame order in the Playground is:
input snapshot → fixed updates (zero or more) → extract render world → render.

---

## 4. Known Bugs & Incomplete Tasks

No bugs were introduced; no code changed. What is open:

**Not built**

- Everything in the Playground. All seven milestones are ahead.

**Not verified**

- Whether Vulkan code can run in the cloud container used for these sessions.
  `apt-cache` showed no install candidate for `mesa-vulkan-drivers` or
  `libvulkan-dev`, but the package lists had not been refreshed, so this is
  unresolved either way. Until it is settled, GPU milestones need confirming on
  a real machine.
- `glslc`, `glslangValidator` and `vulkaninfo` are not installed in the container.
- The `cpp/` game was not built this session.
- Whether vcpkg is available in the container (the Windows build of `cpp/` uses it).

**Decisions waiting on the owner** (roadmap recommendations in brackets)

1. Language level: strict C++20 with `tl::expected`, or C++23 for `std::expected`? [C++20]
2. Is macOS a target? `cpp/` targets OpenGL 4.1, the newest macOS supports;
   Vulkan there means MoltenVK and a feature check before Milestone 1. [Windows and Linux only]
3. Does the Playground stay in this repository or move to its own? [stay here]
4. Minimum GPU: Vulkan 1.3 with descriptor indexing. [accept]

**Not yet decided at all**

- The Playground's C++ namespace and CMake target prefix. Proposed: `playground`.
- Whether tested code from `engine/` is moved or copied into the Playground.

---

## 5. Immediate Next Steps for Next Weekend

Milestone 0, in this order. Exit test: a window opens, logs its start-up,
closes cleanly, and the test target runs.

1. **Answer the four open decisions in §4** (or accept the recommendations).
2. **`aedans-playground/CMakeLists.txt`** — root project, C++20 with extensions
   off, warnings on, one static library per layer. Start with `core` and
   `platform` only.
3. **`aedans-playground/CMakePresets.json`** — `debug`, `release`, and a debug
   preset with address and undefined-behaviour sanitizers.
4. **`aedans-playground/vcpkg.json`** — manifest with only what Milestone 0
   needs: `glfw3`, `glm`, `spdlog`, `tl-expected`, `doctest`. Vulkan packages
   wait for Milestone 1.
5. **`core/include/playground/core/Log.h`** + `core/src/Log.cpp` — the first
   real code. Thin wrapper over spdlog: severity levels, a category per
   subsystem, console and file sinks. Vulkan validation output will be routed
   here in Milestone 1.
6. **`core/include/playground/core/Assert.h`** — assert with
   `std::source_location`, a debugger break, and a fatal path that logs,
   flushes and exits.
7. **`core/include/playground/core/Result.h`** — `Result<T>` alias over
   `tl::expected<T, Error>`, where `Error` carries a code and a message.
8. **`platform/include/playground/platform/Window.h`** + `platform/src/Window.cpp`
   — RAII GLFW window created with `GLFW_NO_API`, returning `Result`. No GLFW
   type appears in the header.
9. **`app/main.cpp`** — initialise logging, create the window, pump events
   until close, shut down in reverse order.
10. **`tests/core_tests.cpp`** — doctest target covering `Result` and the
    assert-free parts of `core`.

Then, before Milestone 1 begins: settle how Vulkan code will be verified
(try installing a software Vulkan driver in the container; otherwise plan to
confirm on a real machine), and install the Vulkan SDK pieces (`glslc`,
validation layers).

**To resume, tell the next session:** "Read `aedans-playground/HANDOFF.md` and
`aedans-playground/ROADMAP.md` on branch `claude/relaxed-cray-5kc2fl`, then
start Milestone 0 at step 2."
