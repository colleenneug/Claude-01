# Aedan's Playground — session handoff

Paste this file into a new session to resume. The plan is `ROADMAP.md`;
build, run and troubleshooting instructions are in `README.md`.

**Status:** roadmap Milestone 2, part 2 is done. A reverse-Z `D32_SFLOAT`
depth buffer, an `engine::Camera` (orthographic or perspective) fed to the
shader through a per-frame uniform buffer, and `screenToWorld` mouse
picking. The demo aims bullets at the mouse, removes the sprite you
right-click, and switches to a tilted 3D view with C. Renderer tests now
read pixels back from the GPU to check depth order, translucency and the
camera. Screenshots: `docs/instanced-25k.png`, `docs/perspective-3d.png`.

- Repository `colleenneug/Claude-01`, branch `claude/relaxed-cray-5kc2fl`, no pull request open.
- **Long-term goal (user, this session):** Erebus Cradle (the game in
  `cpp/`) will be moved onto this engine eventually; we are still far from
  that. Roadmap Milestone 5 covers the port.
- Standing guidelines: modern C++20 (smart pointers, RAII, `std::span`,
  designated initialisers); no over-engineering; clean, commented,
  production-ready code; strict error handling and logging for graphics
  initialisation.
- Decisions: C++20 with `tl::expected`; Windows and Linux only; namespace
  `playground`, targets `playground_*`; `Renderer` interface in the render
  layer; own sparse-set ECS; storage buffers read by device address; VMA
  vendored (v3.3.0) on Linux, vcpkg's on Windows; **the camera reaches
  shaders through a uniform buffer + descriptor set (set 0, binding 0)**;
  **reverse-Z everywhere**; **translucent sprites: sorted back to front,
  depth-tested, not depth-written**.
- Numbering: the user's milestone numbers run ahead of `ROADMAP.md`'s.
- The user may be young or new to this: when asked to explain, keep it short
  and plain, one idea at a time.

---

## 1. Current Architecture State

```
core      Result, Log, Assert, FrameClock, FixedStep, Types
platform  Platform, Window (+ windowSize()), Input
gpu       Context (Instance, Surface, GPU, Device, Allocator), Buffer, Image (new),
          Swapchain (+ TRANSFER_SRC for capture), DeviceHandle (+ descriptor types)
ecs       Entity, ComponentPool<T>, World
render    Renderer, FrameData (viewProjection, viewportSize, sprite depth; reverseZ,
          pixelViewProjection, fitView); VulkanRenderer (private)
engine    Application, Camera (new), ActionMap, WorldBounds, components, systems (+ pickSprite)
app       main.cpp: 25,000 sprites, player, camera, aiming, picking
```

**Camera** (`engine/Camera.h/.cpp`): `projection` (Orthographic |
Perspective), `position`, `target`, `up`, `orthoHalfExtent` (fitted with
`render::fitView`), `fovY`, `nearPlane` (1), `farPlane` (10000).
`view()` = `glm::lookAtRH`; `projectionMatrix(viewport)` =
`render::reverseZ() * glm::orthoRH_ZO(...)` or `* glm::perspectiveRH_ZO(...)`;
`viewProjection(viewport)`; `screenRay` (unproject NDC depth 1 and 0 through
the inverse view-projection; screen y down, NDC y up); `screenToWorld(screen,
viewport, planeZ = 0)` (ray–plane intersection, empty if parallel or behind);
`worldToScreen`.

**Per frame** (`VulkanRenderer::renderFrame`): wait fence → acquire →
`writeSprites` (opaque sprites, alpha = 1, first in game order; then
translucent ones stable-sorted by depth, back to front, sort skipped if
already sorted; counts kept in the frame slot) → `writeFrameUniforms`
(`FrameUniforms{viewProjection}` = `frameData.viewProjection` or
`pixelViewProjection(extent)`) → `prepareCapture` → record:
- colour image UNDEFINED → COLOR_ATTACHMENT; depth image UNDEFINED →
  DEPTH_ATTACHMENT (source = previous frame's depth tests, since one depth
  image is shared by both frames in flight);
- `vkCmdBeginRendering` with colour (clear) + depth (clear 0, store
  DONT_CARE);
- bind descriptor set 0 (this slot's UBO), push the sprite buffer address
  (8 bytes);
- **opaque pipeline** (depth write on) `vkCmdDraw(6, opaqueCount, 0, 0)`;
  **translucent pipeline** (depth write off) `vkCmdDraw(6, blendedCount, 0,
  opaqueCount)`; at most 2 draws;
- if capturing: copy the image into a HostReadback buffer, memory barrier to
  HOST_READ; then → PRESENT.

**Shader** (`sprite.vert`): `Sprite{position, halfSize, rotation(cos,sin),
depth(x = world z), colour}` (48 bytes, std430) by buffer reference +
`gl_InstanceIndex`; `uniform Frame { mat4 viewProjection; }` at set 0,
binding 0; `gl_Position = viewProjection * vec4(xy, depth.x, 1)`.

**Demo** (`app/main.cpp`): layers playfield −1, crowd 0 (translucent),
bullets 1, player 2. Fire aims at `camera.screenToWorld(mouse, windowSize)`
(straight up if off the playfield). Right-click → `pickSprite` → destroy
(never the player). C toggles orthographic ↔ perspective (eye (0, −1150,
950), target (0, −60, 0)). A debug log line records each shot's aim.

**Verified** (GCC 13.3, Clang 18.1, GCC + ASan + UBSan; Mesa llvmpipe, Xvfb):

| Check | Result |
|---|---|
| Baseline at `abf570d` | 9/9 suites passed |
| After | **10/10 suites** (new `camera_tests`) in all three builds; 0 warnings; 0 validation messages |
| Camera maths (`camera_tests`, 11 cases) | ortho fit, square-window fit, no size change with distance, reverse-Z near = 1 / far = 0 / higher layer larger, perspective centre and foreshortening, screen ↔ world round trips in both projections, misses |
| Pixels read back (`render_tests`, 11 cases) | red layer-1 sprite drawn FIRST is in front of blue layer 0; equal depth → later wins; translucent blue (front, given first) over red after sorting; translucent magenta hidden behind opaque green; camera matrix moves a sprite to the screen centre; depth buffer rebuilt on resize |
| Real mouse (xdotool) | cursor at pixel (1400, 450) → world (600, 0), direction (1, 0); bullets land right of the player and level |
| 3D view | perspective renders; an early version z-fought (stripes) among translucent same-plane sprites, fixed by the opaque/translucent split |
| Speed (release, 25,000 sprites, llvmpipe) | ~40.5 ms/frame (was ~38 ms before depth), 120 steps/s |

**Not yet run on a real GPU or on Windows.**

## 2. Exact Files & Classes Modified

New:

| File | Contents |
|---|---|
| `engine/include/playground/engine/Camera.h`, `engine/src/Camera.cpp` | `Projection`, `Ray`, `Camera` |
| `gpu/include/playground/gpu/Image.h`, `gpu/src/Image.cpp` | `kDepthFormat`, `ImageDesc`, `Image` |
| `tests/camera_tests.cpp` | 11 tests |
| `docs/perspective-3d.png` | The 3D view |

Changed:

| File | Change |
|---|---|
| `gpu/include/playground/gpu/DeviceHandle.h` | `DescriptorSetLayout`, `DescriptorPool` |
| `gpu/src/PhysicalDevice.cpp` | GPUs without D32_SFLOAT depth attachments are rejected |
| `gpu/include/playground/gpu/Swapchain.h`, `gpu/src/Swapchain.cpp` | TRANSFER_SRC usage when supported; `canCopyFrom()` |
| `gpu/CMakeLists.txt` | `Image.cpp` |
| `render/include/playground/render/FrameData.h` | `SpriteInstance::depth`; `FrameData::viewProjection` (optional), `viewportSize`; `reverseZ()`, `pixelViewProjection()`; `viewHalfExtent` removed |
| `render/shaders/sprite.vert` | Uniform block (set 0) with the view-projection; depth from `depth.x`; push constant = sprite address only |
| `render/src/VulkanRenderer.h`, `render/src/VulkanRenderer.cpp` | Depth image; descriptor set layout/pool/sets + per-frame UBOs; `FrameUniforms`; opaque and translucent pipelines; partition + stable sort; up to 2 draws; `CapturedFrame`, `requestCapture()`, `takeCapture()`, `depthBuffer()` |
| `platform/include/playground/platform/Window.h`, `platform/src/Window.cpp` | `windowSize()` |
| `engine/src/Application.cpp` | Fills `FrameData::viewportSize` before extraction |
| `engine/include/playground/engine/Components.h` | `Sprite::layer` |
| `engine/include/playground/engine/Systems.h`, `engine/src/Systems.cpp` | `extractSprites` passes `layer` as depth; `pickSprite` |
| `engine/CMakeLists.txt` | `Camera.cpp` |
| `app/main.cpp` | Camera + C toggle, layers, aim at mouse, right-click removal, aim debug log |
| `tests/render_tests.cpp` | +4 (depth order, translucency, camera UBO, capture without frame); resize checks the depth buffer |
| `tests/gpu_tests.cpp` | +1 (depth image) |
| `tests/ecs_tests.cpp` | +2 (picking) |
| `tests/CMakeLists.txt` | `camera_tests` |
| `README.md` | Controls, Camera section, depth, translucency, frame capture, 3D screenshot |

## 3. Code Snippet of the Core Loop/Active Component

`engine/src/Camera.cpp`, projection and mouse picking:

```cpp
glm::mat4 Camera::projectionMatrix(glm::vec2 viewportSize) const {
  const glm::vec2 viewport = safeViewport(viewportSize);
  glm::mat4 clipFromView(1.0f);
  if (projection == Projection::Orthographic) {
    const glm::vec2 half = render::fitView(0.5f * viewport, orthoHalfExtent);
    clipFromView = glm::orthoRH_ZO(-half.x, half.x, -half.y, half.y, nearPlane, farPlane);
  } else {
    clipFromView = glm::perspectiveRH_ZO(fovY, viewport.x / viewport.y, nearPlane, farPlane);
  }
  return render::reverseZ() * clipFromView;
}

Ray Camera::screenRay(glm::vec2 screen, glm::vec2 viewportSize) const {
  const glm::vec2 viewport = safeViewport(viewportSize);
  const float ndcX = 2.0f * screen.x / viewport.x - 1.0f;
  const float ndcY = 1.0f - 2.0f * screen.y / viewport.y;
  const glm::mat4 inverse = glm::inverse(viewProjection(viewport));
  const glm::vec4 nearClip = inverse * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);  // reverse-Z: near = 1
  const glm::vec4 farClip = inverse * glm::vec4(ndcX, ndcY, 0.0f, 1.0f);
  const glm::vec3 nearPoint = glm::vec3(nearClip) / nearClip.w;
  const glm::vec3 farPoint = glm::vec3(farClip) / farClip.w;
  return {.origin = nearPoint, .direction = farPoint - nearPoint};
}

std::optional<glm::vec3> Camera::screenToWorld(glm::vec2 screen, glm::vec2 viewportSize, f32 planeZ) const {
  const Ray ray = screenRay(screen, viewportSize);
  if (std::fabs(ray.direction.z) < 1e-6f) return std::nullopt;  // looking along the plane
  const float t = (planeZ - ray.origin.z) / ray.direction.z;
  if (t < 0.0f) return std::nullopt;                             // plane behind the camera
  return ray.origin + t * ray.direction;
}
```

`render/src/VulkanRenderer.cpp`, the draw:

```cpp
if (frameSlot.opaqueCount + frameSlot.blendedCount > 0) {
  vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, opaquePipeline_.get());
  vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_.get(), 0, 1,
                          &frameSlot.descriptorSet, 0, nullptr);          // set 0: the camera UBO
  const DrawPushConstants push{.spriteList = sprites};
  vkCmdPushConstants(commands, pipelineLayout_.get(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push), &push);
  if (frameSlot.opaqueCount > 0) vkCmdDraw(commands, 6, frameSlot.opaqueCount, 0, 0);
  if (frameSlot.blendedCount > 0) {
    vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, blendedPipeline_.get());  // no depth writes
    vkCmdDraw(commands, 6, frameSlot.blendedCount, 0, frameSlot.opaqueCount);
  }
}
```

## 4. Known Bugs & Incomplete Tasks

No known bugs.

**Not verified**
- A real GPU; Windows (vcpkg VMA package, MSVC warnings, GLM 1.0 — this
  session uses `glm::orthoRH_ZO` / `perspectiveRH_ZO` / `lookAtRH`, present
  in both 0.9.9 and 1.0).
- High-DPI displays: aiming uses window coordinates and `windowSize()`
  consistently, but window and framebuffer sizes have always been equal in
  the test environment.

**Limitations, acceptable for now**
- Opaque sprites on the same layer that overlap would z-fight in the 3D
  view (coplanar, same depth). Nothing in the demo does this (the crowd is
  translucent). Fix when needed: give each opaque sprite a tiny depth
  offset, or treat same-layer opaque sprites as translucent.
- Translucent sorting is by world z (layer), not by distance from the
  camera: right for a 2D plane, approximate for true 3D scenes.
- One depth buffer shared by both frames in flight (correct: the barrier
  orders them), so frame N+1's depth clear waits for frame N's depth tests.
- Frame capture needs swapchain TRANSFER_SRC support (almost universal);
  otherwise `takeCapture` reports Unsupported.
- The 3D view's camera position is hand-placed; no orbit or zoom controls.
- Carried over: sprite buffers never shrink; extraction + writeSprites copy
  sprites twice; `submitAndWait` makes a pool per call; key bindings in code;
  scale not interpolated; swapchain recreation waits for idle; fences, not
  timeline semaphores; no Vulkan debug names; no pipeline cache; GCC
  sanitizer builds preload the Mesa driver for GPU tests.

## 5. Immediate Next Steps for Next Weekend

1. **Vulkan debug names** (`vkSetDebugUtilsObjectNameEXT`): a
   `gpu::setDebugName(device, objectType, handle, name)` helper in a new
   `gpu/include/playground/gpu/DebugNames.h`, a no-op without validation;
   apply to buffers, images, pipelines, descriptor sets, command buffers,
   queues. Readable RenderDoc captures.
2. **Pipeline cache** saved to disk (`VkPipelineCache`, loaded at start,
   written at shutdown), with a header check so a driver update discards it.
3. **Textures**: `gpu::Image` + staging upload of pixels (`stb_image`), a
   sampler, and a bindless texture array (descriptor indexing is already a
   required feature); `Sprite` gains a texture index.
4. Camera controls for the 3D view (orbit, zoom) and a follow-the-player
   option.
5. Later: roadmap Milestone 4 (visual parity with `cpp/`) and Milestone 5
   (moving Erebus Cradle onto the engine).

**To resume, tell the next session:** "Read `aedans-playground/HANDOFF.md` on
branch `claude/relaxed-cray-5kc2fl`, confirm `xvfb-run -a ctest --preset
debug` passes (reinstall the packages in `README.md` if the setup script did
not; `xdotool` is needed for `keyboard_e2e`), then start step 1."
