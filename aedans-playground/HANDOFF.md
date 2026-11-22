# Aedan's Playground — session handoff

Paste this file into a new session to resume. The plan is `ROADMAP.md`;
build, run and troubleshooting instructions are in `README.md`.

**Status:** the first triangle is on screen. A Vulkan renderer draws one
hardcoded triangle every frame, with validation on in debug builds and the
swapchain rebuilt on resize. That completes roadmap Milestone 1 (the user
calls it Milestone 3). Screenshot: `docs/first-triangle.png`. **Next:**
roadmap Milestone 2, renderer foundations: GPU memory (VMA), vertex and
index buffers, a depth buffer, a camera, and many meshes.

- Repository `colleenneug/Claude-01`, branch `claude/relaxed-cray-5kc2fl`, no pull request open.
- Standing guidelines: modern C++20 (smart pointers, RAII, `std::span`,
  designated initialisers); no over-engineering; clean, commented,
  production-ready code; strict error handling and logging for graphics
  initialisation.
- Decisions: C++20 with `tl::expected`; Windows and Linux only; the
  Playground lives in this repository; minimum GPU Vulkan 1.3 with descriptor
  indexing; namespace `playground`, targets `playground_*`; the `Renderer`
  interface lives in a `render` layer below the engine.
- Numbering: the user's milestone numbers run one or two ahead of
  `ROADMAP.md`'s. Same work, different count.
- The user may be young or new to this: when asked to explain, keep it short
  and plain; one simple idea at a time.

---

## 1. Current Architecture State

```
app/main.cpp      logging -> Application (platform, window) -> createVulkanRenderer -> setRenderer -> run
  engine/         Application: loop, renderer slot; destroys renderer -> window -> GLFW
  render/         Renderer (interface); VulkanRenderer (private class, src/); shaders/
  gpu/            Context (Instance, Surface, GPU choice, Device), Swapchain, DeviceHandle
  platform/       Platform (GLFW, Ctrl+C), Window (no GL context, setSize)
  core/           Result, Log, Assert, FrameClock
```

Vulkan types stop at `render`: its public headers
(`render/Renderer.h`, `render/VulkanRenderer.h`) have none, and
`playground_render` links `playground_gpu` PRIVATE, so engine and app build
without Vulkan include paths.

**One frame** (`VulkanRenderer::renderFrame`), 2 frames in flight:
1. Wait on this slot's fence (5 s timeout → "GPU probably hung" error).
2. `vkAcquireNextImageKHR` → `imageAcquired` semaphore. `OUT_OF_DATE` → mark
   stale and skip the frame; `SUBOPTIMAL` → draw, then rebuild.
3. Reset the fence only after a successful acquire (else the next wait hangs).
4. Record: sync2 barrier UNDEFINED → COLOR_ATTACHMENT_OPTIMAL; `vkCmdBeginRendering`
   (clear); bind pipeline; viewport with negative height (y up); scissor;
   `vkCmdDraw(3, 1, 0, 0)`; end; barrier → PRESENT_SRC_KHR.
5. `vkQueueSubmit2`: wait `imageAcquired` at COLOR_ATTACHMENT_OUTPUT, signal
   `renderFinished[imageIndex]` and the fence.
6. `vkQueuePresentKHR` waiting `renderFinished[imageIndex]`.

`renderFinished` semaphores are per swapchain **image**, not per frame in
flight (present holds them until the image is shown).

**Swapchain** (`gpu/src/Swapchain.cpp`): B8G8R8A8_SRGB or R8G8B8A8_SRGB with
SRGB_NONLINEAR (warns if neither); FIFO by default (vsync), MAILBOX then
IMMEDIATE with vsync off; extent from the surface or the clamped framebuffer
size; minImageCount + 1 within the max; CONCURRENT sharing when graphics and
present families differ; rebuilt with `oldSwapchain` after `vkDeviceWaitIdle`.
The pipeline is rebuilt only if the format changes.

**Shaders**: `render/shaders/triangle.vert` has the three positions and
colours as constant arrays indexed by `gl_VertexIndex`; `triangle.frag`
outputs the interpolated colour. Vertex output location 0 → fragment input
location 0; fragment output location 0 → colour attachment 0. Compiled by
`glslc --target-env=vulkan1.3 -Werror -O -mfmt=num` at build time
(`cmake/Shaders.cmake`) and `#include`d into `const u32[]` arrays.

**Pipeline**: no vertex input, triangle list, dynamic viewport and scissor,
no culling, no blending, no depth, empty layout,
`VkPipelineRenderingCreateInfo` with the swapchain format (no VkRenderPass).

**Verified this session** (GCC 13.3, Clang 18.1, GCC + ASan + UBSan, Mesa
llvmpipe under Xvfb):

| Check | Result |
|---|---|
| Build, warnings as errors | 0 warnings, GCC and Clang |
| `xvfb-run -a ctest`: core, app, gpu, render, app_smoke, app_no_display | 6/6 in all three builds |
| Screenshot of the running app | triangle visible: red top, green bottom-left, blue bottom-right; background (25,25,48) = sRGB of the linear clear colour |
| Real window resize to 480×360 | swapchain rebuilt at 480×360, no validation errors |
| Replacing the renderer mid-run | no validation errors |
| Ctrl+C | clean shutdown, exit 0 |
| ASan leak check | still catches a deliberately planted leak in our code |

**Not yet run on a real GPU, and not on Windows.**

## 2. Exact Files & Classes Modified

New:

| File | Contents |
|---|---|
| `gpu/include/playground/gpu/DeviceHandle.h` | `DeviceHandle<Handle, Destroy>`; aliases `Semaphore`, `Fence`, `ImageView`, `ShaderModule`, `PipelineLayout`, `Pipeline`, `CommandPool` |
| `gpu/include/playground/gpu/Swapchain.h`, `gpu/src/Swapchain.cpp` | `SwapchainDesc`, `Swapchain`, `describe(VkFormat)`, `describe(VkPresentModeKHR)` |
| `render/include/playground/render/VulkanRenderer.h` | `VulkanRendererDesc`, `createVulkanRenderer()`, `GpuValidationCounts`, `gpuValidationCounts()` (no Vulkan types) |
| `render/src/VulkanRenderer.h`, `render/src/VulkanRenderer.cpp` | `VulkanRenderer` (private), `kFramesInFlight` |
| `render/shaders/triangle.vert`, `render/shaders/triangle.frag` | The hardcoded triangle |
| `render/CMakeLists.txt` | `playground_render`; compiles the shaders |
| `cmake/Shaders.cmake` | `playground_compile_shader(target shader)` |
| `tests/render_tests.cpp` | 4 tests: frames presented with no validation errors, real resize, renderer replacement, public factory |
| `docs/first-triangle.png` | Screenshot of the running app |

Moved: `engine/include/playground/engine/Renderer.h` →
`render/include/playground/render/Renderer.h` (namespace `render`).

Changed: root `CMakeLists.txt` (glslc component, Shaders.cmake, render
subdirectory); `gpu/CMakeLists.txt` (Swapchain.cpp); `engine/CMakeLists.txt`
(links render); `app/CMakeLists.txt` (links engine only);
`engine/.../Application.h/.cpp` and `tests/app_tests.cpp` (`render::Renderer`);
`platform/.../Window.h/.cpp` (`setSize`); `app/main.cpp` (creates and
attaches the renderer, no hand-ordered teardown); `tests/CMakeLists.txt`
(render_tests; sanitizer-build preload, see §4); `README.md`.

## 3. Code Snippet of the Core Loop/Active Component

`VulkanRenderer::recordCommands` in `render/src/VulkanRenderer.cpp`:

```cpp
transitionImage(commands, image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_NONE,
                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);

const VkRenderingAttachmentInfo colour{
    .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
    .imageView = swapchain_->imageView(imageIndex),
    .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
    .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
    .clearValue = {.color = {.float32 = {/* desc_.clearColour */}}},
};
const VkRenderingInfo rendering{
    .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
    .renderArea = {.offset = {0, 0}, .extent = extent},
    .layerCount = 1,
    .colorAttachmentCount = 1,
    .pColorAttachments = &colour,
};
vkCmdBeginRendering(commands, &rendering);
vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_.get());

const VkViewport viewport{.x = 0.0f, .y = height, .width = width, .height = -height,
                          .minDepth = 0.0f, .maxDepth = 1.0f};   // y up
const VkRect2D scissor{.offset = {0, 0}, .extent = extent};
vkCmdSetViewport(commands, 0, 1, &viewport);
vkCmdSetScissor(commands, 0, 1, &scissor);

vkCmdDraw(commands, 3, 1, 0, 0);   // the triangle

vkCmdEndRendering(commands);
transitionImage(commands, image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE);
```

## 4. Known Bugs & Incomplete Tasks

No known bugs in our code.

**Worked around, not ours:** with ASan, Mesa's software driver produces a
~112-byte LeakSanitizer report once the Vulkan loader unloads it at
`vkDestroyInstance` (its presentation thread's allocations were referenced
only from the unloaded library). Proven: keeping the driver loaded removes
the report. In GCC sanitizer builds, `tests/CMakeLists.txt` preloads
`libasan.so` and `libvulkan_lvp.so` for gpu_tests, render_tests and
app_smoke. A deliberately planted leak in our code is still reported. Running
`build/asan/app/playground_app` by hand without that preload shows the
report; it is expected.

**Not verified**
- A real GPU, two-GPU laptops, separate graphics/present queue families.
- Windows (presets, vcpkg, Vulkan SDK, MSVC warnings).
- An automated pixel check. The triangle was verified by screenshot; a test
  that reads back the swapchain image and checks colours is not written.
- Minimise and restore on a real desktop (Xvfb has no window manager).

**Known simplifications, acceptable for now**
- Swapchain recreation waits for the whole device to go idle
  (`vkDeviceWaitIdle`) and reuses no semaphores. Fine at window-resize rates.
  Strictly, `vkDeviceWaitIdle` does not cover pending presents; the clean
  fix is `VK_EXT_swapchain_maintenance1` present fences, later.
- Fences, not timeline semaphores, for CPU waits. The roadmap wants timeline
  semaphores; switch when there is more than one queue.
- No debug names on Vulkan objects (RenderDoc shows numbered handles).
- No pipeline cache on disk.

## 5. Immediate Next Steps for Next Weekend

Roadmap Milestone 2: renderer foundations. Order:

1. **GPU memory.** Add Vulkan Memory Allocator (vcpkg `vulkan-memory-allocator`;
   on Ubuntu `libvma-dev` if packaged, else vendor the single header, pinned).
   New `gpu/Allocator.h/.cpp`: RAII `VmaAllocator` owned by the Context, and
   `gpu::Buffer` / `gpu::Image` RAII types.
2. **Upload path.** A staging buffer and a one-shot command buffer to copy
   data to device-local memory.
3. **Vertex and index buffers.** Move the triangle's corners out of the shader
   into a vertex buffer; add `VkVertexInputBindingDescription` /
   attribute descriptions. Then a cube.
4. **Depth buffer** (reverse-Z, D32_SFLOAT), rebuilt with the swapchain.
5. **Camera**: GLM (`GLM_FORCE_DEPTH_ZERO_TO_ONE`), view-projection via push
   constants. Keep the negative-viewport y-flip.
6. **Debug names** on every Vulkan object, and a pipeline cache.
7. **Tests**: add a pixel read-back test (render, copy the image to a
   host-visible buffer, check the centre colour) now that buffers exist.

**To resume, tell the next session:** "Read `aedans-playground/HANDOFF.md` on
branch `claude/relaxed-cray-5kc2fl`, confirm `xvfb-run -a ctest --preset
debug` passes (reinstall the packages in `README.md` if the setup script did
not), then start Milestone 2 at step 1."
