# Aedan's Playground — session handoff

Paste this file into a new session to resume. The plan it refers to is
`aedans-playground/ROADMAP.md`; build instructions are in `README.md`.

**Status:** the application shell (roadmap Milestone 0) is built, tested and
committed. The window opens, the loop runs, shutdown is clean, and there is a
`Renderer` slot. Nothing is drawn yet. **Next: the Vulkan renderer (roadmap
Milestone 1), starting with Vulkan instance creation.**

- Repository `colleenneug/Claude-01`, branch `claude/relaxed-cray-5kc2fl`, no pull request open.
- Standing guidelines: modern C++20 (smart pointers, RAII, `std::span`,
  designated initialisers); no over-engineering; clean, commented,
  production-ready code; strict error handling and logging for graphics
  initialisation.
- Decisions accepted: C++20 with `tl::expected`; Windows and Linux only;
  Playground lives in this repository; minimum GPU is Vulkan 1.3 with
  descriptor indexing; namespace `playground`, targets `playground_*`.

---

## 1. Current Architecture State

```
app/main.cpp                 parses options, starts logging, creates the Application, runs it
  └─ engine/  Application    owns Platform -> Window -> Renderer; runs the loop
              Renderer       abstract interface: name, onFramebufferResized, renderFrame
       └─ platform/ Platform GLFW init/terminate (RAII), GLFW errors -> log, Ctrl+C flag
                    Window   RAII GLFW window, GLFW_NO_API, resize flag, no GLFW in header
            └─ core/ Result  tl::expected<T, Error>; Error = {Errc, sentence}
                     Log     spdlog behind std::format; console + file; Session (RAII)
                     Assert  PG_ASSERT (debug), PG_VERIFY (always), fatal()
                     FrameClock  steady_clock deltas, clamped to 0.25 s
```

Each layer is a static library with its own include folder; each may include
only the layers below it. spdlog and GLFW are private dependencies: no public
header includes them.

**The loop** (`Application::run`), once per frame:
1. Events. With a renderer attached and the window visible, events are polled
   and the loop runs flat out; the renderer's present will set the pace. With
   no renderer, or while minimised, the loop sleeps in the OS event wait until
   the next 60 fps deadline.
2. Stop check, in priority order: Ctrl+C or SIGTERM, then `requestQuit()`,
   then the window closing.
3. `FrameClock::tick()` produces a `FrameTime`: delta (clamped), raw delta,
   elapsed time, frame index.
4. `UpdateFn(frameTime)`. Keeps running while minimised, because co-op
   clients must keep simulating.
5. Render, if a renderer is attached and the window is not minimised. A
   pending resize is delivered first. A renderer error stops the loop, and
   `run()` returns it.
6. Stop after `maxFrames`, if set.

**Shutdown** (`Application::shutdown`, also called by the destructor) destroys
the renderer, then the window, then GLFW, logging each step. The renderer goes
first so that a Vulkan surface never outlives its window.

**Verified this session** (GCC 13.3 and Clang 18.1, Debug; plus ASan + UBSan):

| Check | Result |
|---|---|
| Build with `-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Werror` | 0 warnings, GCC and Clang |
| `xvfb-run -a ctest`: core_tests, app_tests, app_smoke, app_no_display | 4/4 pass, all three builds |
| `ctest` with no display | core and no-display tests pass; window tests reported as skipped |
| Idle pacing | 120 frames in 1.98 s (60 fps), no busy spin |
| Ctrl+C during a run | "interrupted", clean shutdown, exit code 0 |
| No display | exit code 1, log line "start-up failed (platform failure): GLFW could not initialise: X11: The DISPLAY environment variable is missing" |
| Sanitizers | no reports, including the failure path |

**Vulkan works in the cloud container.** Mesa's software driver (llvmpipe)
provides a Vulkan 1.4 device with every feature the roadmap requires (dynamic
rendering, synchronization2, timeline semaphores, descriptor indexing, runtime
descriptor arrays, partially bound descriptors, buffer device address). The
validation layer is installed, and GLFW reports the surface extensions
`VK_KHR_surface` and `VK_KHR_xcb_surface` under Xvfb. Milestone 1's
"zero validation messages" test can therefore run in the container.

**Packages installed in the container this session** (they disappear when the
container is reclaimed unless saved in the environment's setup script):

```bash
apt-get update && apt-get install -y libglfw3-dev libglm-dev libspdlog-dev doctest-dev mesa-vulkan-drivers vulkan-tools libvulkan-dev vulkan-validationlayers glslc
```

`xvfb-run` was already installed.

## 2. Exact Files & Classes Modified

All new, under `aedans-playground/`:

| File | Contents |
|---|---|
| `CMakeLists.txt` | Root project: C++20, options for warnings-as-errors, sanitizers and tests; finds glfw3, spdlog and doctest |
| `cmake/ProjectOptions.cmake` | `playground_configure_target()`: warning flags per compiler; global sanitizer flags |
| `CMakePresets.json` | `debug`, `release`, `asan` (Ninja, system packages); `windows` (VS 2022 + vcpkg) |
| `vcpkg.json` | glfw3, spdlog, doctest; baseline `8f363bdc…` |
| `README.md` | Layout, build and run instructions |
| `third_party/tl/expected.hpp`, `README.md` | tl::expected v1.3.1, unmodified, CC0, with its SHA-256 recorded |
| `core/include/playground/core/Types.h` | `u8`…`u64`, `i8`…`i64`, `f32`, `f64`, `usize` |
| `core/include/playground/core/Result.h` | `Errc`, `describe(Errc)`, `Error`, `Result<T>`, `Status`, `fail()` |
| `core/include/playground/core/Log.h`, `core/src/Log.cpp` | `log::Level`, `Category`, `Config`, `Session`, `init()`, `trace()`…`critical()`, `flush()` |
| `core/include/playground/core/Assert.h`, `core/src/Assert.cpp` | `PG_ASSERT`, `PG_VERIFY`, `fatal()`, `debuggerAttached()` |
| `core/include/playground/core/FrameClock.h`, `core/src/FrameClock.cpp` | `FrameTime`, `FrameClock` |
| `platform/include/playground/platform/Platform.h`, `platform/src/Platform.cpp` | `Platform` |
| `platform/include/playground/platform/Window.h`, `platform/src/Window.cpp` | `Extent2D`, `WindowDesc`, `Window` |
| `engine/include/playground/engine/Renderer.h` | `Renderer` (abstract) |
| `engine/include/playground/engine/Application.h`, `engine/src/Application.cpp` | `ApplicationConfig`, `UpdateFn`, `StopReason`, `describe(StopReason)`, `Application` |
| `app/main.cpp` | Option parsing, `TitleFpsCounter` (frame rate in the title bar) |
| `tests/core_tests.cpp` | 14 tests: Result, FrameClock, Log, Assert |
| `tests/app_tests.cpp` | 9 tests with a `FakeRenderer`: frame limit, update order, quit, renderer error, shutdown order, single Platform, bad config |
| `tests/CMakeLists.txt` | CTest: core_tests, app_tests (skip code 77), app_smoke, app_no_display |
| `CMakeLists.txt` files in `core/`, `platform/`, `engine/`, `app/` | One library or executable each |

Also changed: the repository's `.gitignore` now ignores `aedans-playground/build/`.

## 3. Code Snippet of the Core Loop/Active Component

The heart of `Application::run()` in `engine/src/Application.cpp`:

```cpp
while (true) {
  // 1. Events.
  const bool idle = !renderer_ || window_->isMinimized();
  if (idle) {
    waitForEventsUntil(nextIdleFrame);
    nextIdleFrame = std::max(nextIdleFrame + idleInterval, Clock::now());
  } else {
    platform_->pollEvents();
  }

  // 2. Stop?
  if (const StopReason reason = pendingStopReason(); reason != StopReason::None) {
    stopReason_ = reason;
    break;
  }

  // 3. Clock.
  const FrameTime time = clock_.tick();

  // 4. Update.
  if (update_) update_(time);

  // 5. Render.
  if (renderer_ && !window_->isMinimized()) {
    if (Status frame = renderFrame(time); !frame) {
      log::error(Category::Engine, "renderer \"{}\" failed on frame {}: {}", renderer_->name(), time.frameIndex,
                 frame.error().message);
      stopReason_ = StopReason::RendererError;
      result = std::move(frame);
      ++framesRun_;
      break;
    }
  }

  ++framesRun_;
  recordFrameStats(time);

  // 6. Frame limit.
  if (config_.maxFrames != 0 && framesRun_ >= config_.maxFrames) {
    stopReason_ = StopReason::FrameLimit;
    break;
  }
}
```

The renderer contract it calls (`engine/include/playground/engine/Renderer.h`):

```cpp
class Renderer {
 public:
  virtual ~Renderer() = default;
  [[nodiscard]] virtual std::string_view name() const noexcept = 0;
  [[nodiscard]] virtual Status onFramebufferResized(platform::Extent2D size) = 0;  // never empty
  [[nodiscard]] virtual Status renderFrame(const FrameTime& time) = 0;
};
```

## 4. Known Bugs & Incomplete Tasks

No known bugs.

**Not verified**
- **The Windows build.** The `windows` presets and `vcpkg.json` have never
  been run. vcpkg will supply GLFW 3.4 rather than 3.3; only APIs present in
  both versions are used, but MSVC may raise warnings GCC and Clang did not,
  and `/WX` turns those into errors. Turn off `PLAYGROUND_WARNINGS_AS_ERRORS`
  if that blocks a first build, and report the warnings.
- **Resize delivery to the renderer.** The code path exists, but no test
  covers it: Xvfb has no window manager to resize a window with. It will be
  exercised by Milestone 1's swapchain recreation on a real desktop.

**Deliberately not done yet**
- **No renderer**, so nothing is drawn; `main.cpp` has a comment where the
  Vulkan renderer attaches.
- **No input layer.** Gameplay cannot read the keyboard or mouse yet (roadmap §4.3).
- **The update step is variable-rate.** The fixed-timestep loop from
  `engine/` (`erebus::GameLoop`) replaces it at roadmap Milestone 3, inside
  `Application::run`.
- **Container packages not saved.** The apt packages above must be reinstalled
  in a new container unless they are added to the environment's setup script.

**Open, not yet decided**
- Whether tested code from `engine/` is moved or copied into the Playground
  (matters at Milestone 3).

## 5. Immediate Next Steps for Next Weekend

Roadmap Milestone 1: one triangle on screen with zero validation messages,
surviving resize and minimise, with a clean shutdown. Order:

1. **Dependencies.** Add `vulkan-headers`, `vulkan-loader`, `volk`,
   `vk-bootstrap` and `vulkan-memory-allocator` to `vcpkg.json`, and
   `find_package(Vulkan REQUIRED)` to the root `CMakeLists.txt`. On Linux,
   check which of volk, vk-bootstrap and VMA are packaged; vendor or fetch
   the rest, pinned.
2. **New layer `gpu/`** (`playground_gpu`, depends on platform). First file:
   `gpu/include/playground/gpu/VulkanCheck.h` — a helper that turns a
   `VkResult` into a `Status` naming the call, the result code as text and the
   call site.
3. **`gpu/src/Instance.cpp`** — `Instance::create()` returning
   `Result<Instance>`: Vulkan 1.3, the GLFW surface extensions from
   `Platform::requiredVulkanInstanceExtensions()`, the validation layer and
   debug messenger in debug builds, and validation messages routed into
   `log::Category::Gpu`. In debug builds a validation error is a `PG_ASSERT`
   failure.
4. Then surface, physical-device selection (logging why each rejected device
   failed), logical device and queues, swapchain with recreation, per-frame
   command buffers and synchronisation, and the triangle shader compiled by
   `glslc` at build time.
5. **`gpu/VulkanRenderer`** implementing `engine::Renderer`, attached in
   `app/main.cpp` where the comment marks it.
6. Extend the tests: a Vulkan smoke test that renders N frames under Xvfb on
   llvmpipe and fails on any validation message.

**To resume, tell the next session:** "Read `aedans-playground/HANDOFF.md` on
branch `claude/relaxed-cray-5kc2fl`, reinstall the packages it lists, confirm
`xvfb-run -a ctest --preset debug` passes, then start Milestone 1 at step 1."
