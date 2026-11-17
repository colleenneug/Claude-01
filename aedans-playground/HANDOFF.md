# Aedan's Playground — session handoff

Paste this file into a new session to resume. The plan is `ROADMAP.md`;
build, run and troubleshooting instructions are in `README.md`.

**Status:** the application shell and the first half of Vulkan bring-up are
built, tested and committed: instance (with validation), window surface,
physical-device selection, logical device, graphics queue. Nothing is drawn
yet. **Next: the swapchain**, then the first triangle (the rest of roadmap
Milestone 1).

- Repository `colleenneug/Claude-01`, branch `claude/relaxed-cray-5kc2fl`, no pull request open.
- Standing guidelines: modern C++20 (smart pointers, RAII, `std::span`,
  designated initialisers); no over-engineering; clean, commented,
  production-ready code; strict error handling and logging for graphics
  initialisation.
- Decisions: C++20 with `tl::expected`; Windows and Linux only; the
  Playground lives in this repository; minimum GPU Vulkan 1.3 with descriptor
  indexing; namespace `playground`, targets `playground_*`.
- Numbering: the user calls this work "Milestone 2"; in `ROADMAP.md` it is
  the first half of Milestone 1. Same work, different count.

---

## 1. Current Architecture State

```
app/main.cpp      logging -> Application (platform, window) -> gpu::Context -> run
                  shutdown: gpu::Context, then Application::shutdown(), then log
  engine/         Application: loop, renderer slot (still empty)
  gpu/            Context = Instance -> Surface -> PhysicalDeviceInfo -> Device
  platform/       Platform (GLFW, Ctrl+C), Window (no GL context)
  core/           Result, Log, Assert, FrameClock
```

**Vulkan bring-up** (`gpu::Context::create`), each step logged, each failure
prefixed with its step ("step 3 (choosing a GPU): ..."):

1. **Instance** (`Instance.cpp`). Requires loader ≥ 1.3, checks every
   requested extension (names the missing ones), enables
   `VK_LAYER_KHRONOS_validation` + `VK_EXT_debug_utils` when validation is on
   (default in debug builds), and chains a messenger into `vkCreateInstance` so
   create/destroy messages are caught. If the layer is not installed it warns
   and continues without it.
2. **Surface** (`Surface.cpp`) via `glfwCreateWindowSurface`.
3. **Physical device** (`PhysicalDevice.cpp`). Logs every GPU (name, type,
   Vulkan version, driver name/version); rejects any lacking Vulkan 1.3,
   `VK_KHR_swapchain`, the required features, a graphics queue, present
   support for this window, or surface formats/present modes, listing every
   reason; scores discrete 1000 > integrated 500 > virtual 200 > CPU 10, +50
   for one queue family doing both graphics and present. A CPU device is
   accepted with a warning.
4. **Logical device** (`Device.cpp`). One queue per distinct family; enables
   exactly the features from `Requirements.h`; fetches graphics and present
   queues; destructor waits for idle.

`gpu/src/Requirements.h` holds the required device extensions and features
(dynamicRendering, synchronization2, timelineSemaphore, descriptorIndexing,
runtimeDescriptorArray, descriptorBindingPartiallyBound,
shaderSampledImageArrayNonUniformIndexing, bufferDeviceAddress) and is used
by both step 3 (check) and step 4 (enable).

**Validation messages** go to the log under `[gpu]` (error → error, warning →
warn, info → debug, verbose → trace) and are counted in process-wide
counters (`gpu::validationCounts()`). `main` exits 1 if any validation error
occurred during the run, including teardown, so `app_smoke` fails on one.

**Ownership today:** `main.cpp` owns the `gpu::Context` in a
`std::optional` and resets it before `Application::shutdown()` (the surface
must die before the window). This is temporary: it moves into a
`VulkanRenderer` in the renderer slot once there is a swapchain to present
with. Until then no renderer is attached, so the loop paces itself at 60 fps.

**Verified this session** (GCC 13.3, Clang 18.1, and GCC with ASan + UBSan):

| Check | Result |
|---|---|
| Build, warnings as errors | 0 warnings, GCC and Clang |
| `xvfb-run -a ctest`: core, app, gpu, app_smoke, app_no_display | 5/5 pass in all three builds |
| `ctest` without a display | core and no-display pass; app, gpu and smoke skipped |
| Validation messenger | an injected error and warning are logged and counted |
| A real run, validation on | 0 validation messages; teardown device → surface → instance → window → GLFW |
| `--no-validation` | instance created with validation off |
| Validation layer hidden (`VK_LOADER_LAYERS_DISABLE=~all~`) | warning, runs without validation, exit 0 |

All on Mesa llvmpipe (software Vulkan 1.4) under Xvfb. **Not yet run on a
real GPU, and not on Windows.**

## 2. Exact Files & Classes Modified

New this session, under `aedans-playground/`:

| File | Contents |
|---|---|
| `gpu/include/playground/gpu/VulkanCheck.h`, `gpu/src/VulkanCheck.cpp` | `check(VkResult, call)` → `Status`; `resultName`, `resultHint`, `versionString` |
| `gpu/include/playground/gpu/Instance.h`, `gpu/src/Instance.cpp` | `InstanceDesc`, `Instance`, `ValidationCounts`, `validationCounts()`, `resetValidationCounts()`, `kRequiredApiVersion`, `kValidationByDefault` |
| `gpu/include/playground/gpu/Surface.h`, `gpu/src/Surface.cpp` | `Surface` |
| `gpu/src/Requirements.h` | `detail::kRequiredDeviceExtensions`, `detail::FeatureChain`, `requestEngineFeatures`, `missingFeatures` |
| `gpu/include/playground/gpu/PhysicalDevice.h`, `gpu/src/PhysicalDevice.cpp` | `QueueFamilies`, `PhysicalDeviceInfo`, `selectPhysicalDevice()`, `describe(VkPhysicalDeviceType)` |
| `gpu/include/playground/gpu/Device.h`, `gpu/src/Device.cpp` | `Device` |
| `gpu/include/playground/gpu/Context.h`, `gpu/src/Context.cpp` | `ContextDesc`, `Context` |
| `gpu/CMakeLists.txt` | `playground_gpu`: PUBLIC platform + `Vulkan::Vulkan`, PRIVATE glfw |
| `tests/gpu_tests.cpp` | 7 tests: `check()`, missing extension, messenger counting, validation off, full bring-up, repeated bring-up, step-named failure |

Changed:

| File | Change |
|---|---|
| `CMakeLists.txt` | `find_package(Vulkan 1.3 REQUIRED)`; `add_subdirectory(gpu)` |
| `cmake/ProjectOptions.cmake` | `-Wno-missing-field-initializers` (GCC 13 warns on every partial designated initialiser of a Vulkan struct) |
| `core/include/playground/core/Result.h` | `Errc::GpuFailure` |
| `app/main.cpp` | creates and destroys `gpu::Context`; `--validation` / `--no-validation`; exit 1 on validation errors |
| `app/CMakeLists.txt` | links `playground::gpu` |
| `tests/CMakeLists.txt` | `gpu_tests` (skip code 77, labels display;gpu) |
| `README.md` | Windows and Linux setup, expected log, failure table |

## 3. Code Snippet of the Core Loop/Active Component

`gpu::Context::create` in `gpu/src/Context.cpp`, the bring-up sequence:

```cpp
Result<Context> Context::create(platform::Platform& platform, platform::Window& window, const ContextDesc& desc) {
  if (!platform.vulkanSupported()) {
    return fail(Errc::Unsupported,
                "no Vulkan loader or driver was found on this machine; install or update the GPU driver");
  }

  // 1. Instance, with the extensions GLFW needs to make a window surface.
  const auto platformExtensions = platform.requiredVulkanInstanceExtensions();
  auto instance = Instance::create({
      .applicationName = desc.applicationName,
      .enableValidation = desc.enableValidation,
      .requiredExtensions = {platformExtensions.begin(), platformExtensions.end()},
  });
  if (!instance) return tl::unexpected(atStep(1, "Vulkan instance", std::move(instance.error())));

  // 2. Surface.
  auto surface = Surface::create(instance->handle(), window);
  if (!surface) return tl::unexpected(atStep(2, "window surface", std::move(surface.error())));

  // 3. Physical device.
  auto physicalDevice = selectPhysicalDevice(instance->handle(), surface->handle());
  if (!physicalDevice) return tl::unexpected(atStep(3, "choosing a GPU", std::move(physicalDevice.error())));

  // 4. Logical device and queues.
  auto device = Device::create(*physicalDevice);
  if (!device) return tl::unexpected(atStep(4, "logical device", std::move(device.error())));

  log::info(Category::Gpu, "GPU ready: {} with Vulkan {}", physicalDevice->name,
            versionString(physicalDevice->apiVersion));
  return Context(std::move(*instance), std::move(*surface), std::move(*physicalDevice), std::move(*device));
}
```

`Context` declares its members `instance_, surface_, physicalDevice_, device_`
in that order, so they are destroyed device → surface → instance. Its move
assignment is deleted because a defaulted one would assign in declaration
order and destroy the old instance before the old device.

## 4. Known Bugs & Incomplete Tasks

No known bugs.

**Not verified**
- **A real GPU.** Only Mesa's software driver has run this code. Discrete
  and integrated GPUs, two-GPU laptops, and separate graphics/present queue
  families are untested.
- **Windows.** Neither the presets, vcpkg, the Vulkan SDK path, nor MSVC's
  warnings have been tried.

**Deliberately not done yet**
- **No swapchain**, so nothing is presented and no renderer is attached.
- **No debug names** on Vulkan objects (`vkSetDebugUtilsObjectNameEXT`); the
  roadmap wants them on every object, for RenderDoc. Add with the swapchain.
- **No memory allocator.** VMA arrives with the first buffer or image.
- **`main.cpp` owns the GPU context**, and gets the destruction order right
  by hand. Moves into a `VulkanRenderer` with the swapchain.
- **`app/` includes a Vulkan header** (through `gpu/Context.h`). Goes away
  when the context moves into the renderer.
- **Layering question.** `engine::Renderer` lives in the engine layer, so a
  `VulkanRenderer` implementing it would make gpu/render depend on engine —
  the opposite of the roadmap's diagram. Decide before writing
  `VulkanRenderer`: move the `Renderer` interface down (e.g. into a small
  `render` layer), or accept the dependency.

## 5. Immediate Next Steps for Next Weekend

1. Decide the layering question above.
2. **`gpu/include/playground/gpu/Swapchain.h` + `gpu/src/Swapchain.cpp`**:
   choose surface format (prefer `B8G8R8A8_SRGB` + `SRGB_NONLINEAR`), present
   mode (FIFO always exists; MAILBOX if available and wanted), extent from the
   framebuffer, image count; create the swapchain and image views; recreate on
   `VK_ERROR_OUT_OF_DATE_KHR` / `VK_SUBOPTIMAL_KHR` and on
   `Renderer::onFramebufferResized`. Handle `imageSharingMode` CONCURRENT when
   graphics and present families differ.
3. **Per-frame resources** (frames in flight = 2): command pool and buffer,
   acquire semaphore, render-finished semaphore, and a timeline semaphore for
   CPU waits.
4. **`VulkanRenderer`** implementing `engine::Renderer`: owns `gpu::Context`
   and the swapchain; `renderFrame` acquires, records a dynamic-rendering pass
   that clears to a colour, transitions with synchronization2 barriers,
   submits, presents. Attach it in `main.cpp` and delete the hand-ordered
   teardown there.
5. Then the triangle: `shaders/triangle.vert/.frag`, compiled by `glslc` from
   CMake (`find_package(Vulkan COMPONENTS glslc)`), a graphics pipeline with
   dynamic rendering, one draw.
6. Extend `gpu_tests`: render N frames with zero validation messages; resize
   path via `onFramebufferResized`.

**To resume, tell the next session:** "Read `aedans-playground/HANDOFF.md` on
branch `claude/relaxed-cray-5kc2fl`, confirm `xvfb-run -a ctest --preset
debug` passes (reinstall the packages in `README.md` if the setup script did
not), then decide the layering question and start the swapchain."
