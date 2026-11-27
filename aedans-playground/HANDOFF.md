# Aedan's Playground — session handoff

Paste this file into a new session to resume. The plan is `ROADMAP.md`;
build, run and troubleshooting instructions are in `README.md`.

**Status:** roadmap Milestone 2, part 1 is done. GPU memory goes through the
Vulkan Memory Allocator (`gpu::Allocator`, `gpu::Buffer`, staging uploads),
and every sprite is drawn in ONE instanced draw call from a persistently
mapped per-frame storage buffer read through its GPU address. The demo spawns
25,000 sprites by default (screenshot: `docs/instanced-25k.png`), with zero
validation messages.

- Repository `colleenneug/Claude-01`, branch `claude/relaxed-cray-5kc2fl`, no pull request open.
- Standing guidelines: modern C++20 (smart pointers, RAII, `std::span`,
  designated initialisers); no over-engineering; clean, commented,
  production-ready code; strict error handling and logging for graphics
  initialisation.
- Decisions: C++20 with `tl::expected`; Windows and Linux only; namespace
  `playground`, targets `playground_*`; `Renderer` interface in the render
  layer; own sparse-set ECS; raw input in `platform/`, actions in `engine/`;
  **shaders read storage buffers by device address (`GL_EXT_buffer_reference`),
  not descriptor sets**; VMA vendored (v3.3.0) on Linux, vcpkg's on Windows.
- Numbering: the user's milestone numbers run ahead of `ROADMAP.md`'s.
  Roadmap Milestone 2 part 2 (depth buffer, camera, debug names, pipeline
  cache) is next.
- The user may be young or new to this: when asked to explain, keep it short
  and plain, one idea at a time.

---

## 1. Current Architecture State

```
core      Result, Log, Assert, FrameClock, FixedStep, Types
platform  Platform, Window (GLFW callbacks), Input
gpu       Context = Instance, Surface, GPU, Device, Allocator (new, step 5);
          Buffer (new), Swapchain, DeviceHandle, VulkanCheck
ecs       Entity, ComponentPool<T>, World
render    Renderer, FrameData (+ fitView); VulkanRenderer (private): instanced sprites
engine    Application (loop, fixed step, input latch), ActionMap, WorldBounds, components, systems
app       main.cpp: 25,000 bouncing sprites (default) + the player
```

**GPU memory** (`gpu/Allocator.h`, `gpu/Buffer.h`):
- `Allocator` wraps `VmaAllocator` (BUFFER_DEVICE_ADDRESS flag; Vulkan
  functions taken statically from the linked loader). Bring-up step 5 in
  `Context::create`; declared after `Device`, so destroyed before it; logs an
  error naming the count and bytes of any allocation still alive at
  destruction. `stats()` = live allocations, bytes, block bytes.
- `Buffer`: RAII, move-only, three `MemoryKind`s:
  `HostVisible` (sequential-write, mapped for life), `DeviceLocal`,
  `HostReadback` (random-access/cached, mapped). `mapped()`, `write()`,
  `flush()`, `invalidate()`, `deviceAddress()` (with SHADER_DEVICE_ADDRESS
  usage). VMA allocation names = debug names.
- `submitAndWait(context, record)` (transient pool + fence, 5 s timeout),
  `copyBuffer`, `createDeviceLocalBuffer` (staging upload). Blocking: for
  loading and tests, not per frame.

**Sprite rendering, per frame** (`VulkanRenderer::renderFrame`): wait fence
→ acquire → **`writeSprites`**: grow this slot's buffer if needed (doubling
from 1,024; cap `kMaxSprites` = 4M) and write `SpriteGpu` records
(position, halfSize, (cos, sin), pad, colour: 48 bytes, std430) straight into
the mapping, then flush → record: push `DrawPushConstants{spriteList address,
viewHalf}` (16 bytes) and **`vkCmdDraw(6, N)`** → submit → present. One sprite
buffer per frame in flight. `sprite.vert` indexes
`draw.spriteList.sprites[gl_InstanceIndex]`.

**Measured** (25,000 sprites, release, validation off, Mesa llvmpipe = CPU
rasteriser, 1600×900):

| Renderer | Frame time | Simulation |
|---|---|---|
| Old, one draw per sprite, OLD sprite sizes (12–48 px) | ~112 ms | 71 of 120 steps/s (falling behind) |
| Old, one draw per sprite, NEW sprite sizes (5–16 px) | ~66 ms | ~115 steps/s |
| **New, one instanced draw, new sizes** | **~38 ms** | **120 steps/s** |

So instancing alone ≈ 1.7× on this CPU rasteriser (fill-rate bound); the
rest of 112 → 38 came from smaller sprites (the demo now sizes sprites from
the count). The old renderer was measured by building commit `5da6f36` in a
temporary worktree with only the sizing change.

**Verified** (GCC 13.3, Clang 18.1, GCC + ASan + UBSan; Xvfb):
- Baseline at `5da6f36`: 9/9 suites passed.
- After: 9/9 suites in all three builds; gpu_tests 11 cases (4 new),
  render_tests 7 (3 new); 0 warnings.
- 25,000 sprites with validation on: 0 validation messages, no GPU memory
  leak at allocator destruction, sprite buffers grew once (1,024 → 32,768
  per slot, 1.5 MiB each).
- **Not yet run on a real GPU or on Windows.**

## 2. Exact Files & Classes Modified

New:

| File | Contents |
|---|---|
| `third_party/vma/vk_mem_alloc.h`, `third_party/vma/README.md` | VMA v3.3.0, unmodified (MIT; SHA-256 recorded) |
| `gpu/include/playground/gpu/Allocator.h`, `gpu/src/Allocator.cpp` | `AllocatorStats`, `Allocator` |
| `gpu/include/playground/gpu/Buffer.h`, `gpu/src/Buffer.cpp` | `MemoryKind`, `BufferDesc`, `Buffer`, `submitAndWait`, `copyBuffer`, `createDeviceLocalBuffer` |
| `gpu/src/VmaImplementation.cpp` | The one `VMA_IMPLEMENTATION` unit, built with `-w` |
| `docs/instanced-25k.png` | Screenshot, 25,000 sprites |

Changed:

| File | Change |
|---|---|
| `CMakeLists.txt` | `find_package(VulkanMemoryAllocator CONFIG QUIET)`; `playground_vma` target (package or vendored header; `VMA_STATIC_VULKAN_FUNCTIONS=1`, `VMA_DYNAMIC_VULKAN_FUNCTIONS=0`) |
| `vcpkg.json` | `vulkan-memory-allocator` |
| `gpu/CMakeLists.txt` | Allocator, Buffer, VmaImplementation sources; links `playground_vma` |
| `gpu/include/playground/gpu/Context.h`, `gpu/src/Context.cpp` | Step 5 allocator; `allocator()` accessor; member after `device_` |
| `render/shaders/sprite.vert` | Buffer reference + `gl_InstanceIndex`; push constant = address + viewHalf |
| `render/src/VulkanRenderer.h`, `render/src/VulkanRenderer.cpp` | `SpriteGpu`, `DrawPushConstants` (replace `SpritePushConstants`); per-frame `gpu::Buffer sprites`; `writeSprites`; one instanced draw; `kInitialSpriteCapacity`, `kMaxSprites`; `spriteCapacity()`, `lastSpriteCount()`, `lastDrawCalls()` |
| `app/main.cpp` | Default `--sprites` 25,000; sprite size derived from the count |
| `tests/gpu_tests.cpp` | +4: allocation counting and no leaks, mapped buffer + device address, staging round trip byte for byte, misuse errors |
| `tests/render_tests.cpp` | +3: 25,000 sprites in one draw, buffer growth without shrinking, zero-sprite frame |
| `README.md` | GPU memory section, instanced drawing, measured speed, new screenshot |

## 3. Code Snippet of the Core Loop/Active Component

`render/src/VulkanRenderer.cpp`:

```cpp
Status VulkanRenderer::writeSprites(Frame& frame, const FrameData& frameData) {
  const usize count = frameData.sprites.size();
  if (count > kMaxSprites) {
    return fail(Errc::InvalidArgument, "{} sprites in one frame; the renderer's limit is {}", count, kMaxSprites);
  }
  lastSpriteCount_ = static_cast<u32>(count);
  lastDrawCalls_ = count > 0 ? 1u : 0u;

  // Grow this frame's buffer if too small: double until it fits.
  const u64 needed = std::max<u64>(count, kInitialSpriteCapacity) * sizeof(SpriteGpu);
  if (frame.sprites.size() < needed) {
    u64 capacity = std::max<u64>(frame.sprites.size() / sizeof(SpriteGpu), kInitialSpriteCapacity);
    while (capacity * sizeof(SpriteGpu) < needed) capacity *= 2;
    auto grown = gpu::Buffer::create(context_.allocator(),
        {.sizeBytes = capacity * sizeof(SpriteGpu),
         .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
         .memory = gpu::MemoryKind::HostVisible, .debugName = "sprite instances"});
    if (!grown) return tl::unexpected(std::move(grown.error()));
    frame.sprites = std::move(*grown);  // safe: this slot's fence was waited on
  }
  if (count == 0) return {};

  auto* out = reinterpret_cast<SpriteGpu*>(frame.sprites.mapped().data());
  for (usize i = 0; i < count; ++i) {
    const SpriteInstance& sprite = frameData.sprites[i];
    out[i] = SpriteGpu{.position = sprite.position, .halfSize = 0.5f * sprite.size,
                       .rotation = {std::cos(sprite.rotation), std::sin(sprite.rotation)},
                       .unused = {0.0f, 0.0f}, .colour = sprite.colour};
  }
  return frame.sprites.flush(0, count * sizeof(SpriteGpu));
}

// in recordCommands():
if (!frame.sprites.empty()) {
  const DrawPushConstants push{.spriteList = sprites,
                               .viewHalf = fitView({0.5f * width, 0.5f * height}, frame.viewHalfExtent)};
  vkCmdPushConstants(commands, pipelineLayout_.get(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push), &push);
  vkCmdDraw(commands, 6, static_cast<u32>(frame.sprites.size()), 0, 0);  // every sprite, one draw
}
```

`render/shaders/sprite.vert` (core):

```glsl
layout(buffer_reference, std430, buffer_reference_align = 16) readonly buffer SpriteList { Sprite sprites[]; };
layout(push_constant) uniform Draw { SpriteList spriteList; vec2 viewHalf; } draw;

void main() {
  Sprite sprite = draw.spriteList.sprites[gl_InstanceIndex];
  vec2 local = kCorners[gl_VertexIndex] * sprite.halfSize;
  vec2 rotated = vec2(local.x * sprite.rotation.x - local.y * sprite.rotation.y,
                      local.x * sprite.rotation.y + local.y * sprite.rotation.x);
  gl_Position = vec4((rotated + sprite.position) / draw.viewHalf, 0.0, 1.0);
  outColour = sprite.colour;
}
```

## 4. Known Bugs & Incomplete Tasks

No known bugs.

**Not verified**
- A real GPU: the speed-up from removing 25,000 draw calls should be larger
  there (CPU-side recording and validation cost), but it is unmeasured.
- Windows: vcpkg's VMA package path (`GPUOpen::VulkanMemoryAllocator`) has
  never been configured; MSVC warnings; GLM 1.0.

**Limitations, acceptable for now**
- Sprite buffers grow but never shrink (a spike to 1M sprites keeps ~48 MiB
  per frame slot until exit).
- CPU-side per frame: extraction (ECS → FrameData) and writeSprites copy all
  sprites twice; writing `SpriteGpu` straight from the ECS would save one
  pass. Not needed yet.
- `submitAndWait` creates a command pool and fence per call: fine for
  loading, wasteful if called often.
- With 25,000 sprites and validation on, the debug build runs ~13 fps on the
  software renderer (simulation and validation, not drawing).
- Carried over: cursor position not mapped to world space; bindings in code;
  bullets fire straight up; scale not interpolated; swapchain recreation
  waits for idle; fences, not timeline semaphores; no Vulkan debug names; no
  pipeline cache; GCC sanitizer builds preload the Mesa driver for GPU tests.

## 5. Immediate Next Steps for Next Weekend

Roadmap Milestone 2, part 2:
1. **Vulkan debug names** on every object (`vkSetDebugUtilsObjectNameEXT`):
   a `gpu::setDebugName(device, objectType, handle, name)` helper in a new
   `gpu/include/playground/gpu/DebugNames.h`, a no-op when validation is off;
   use it for buffers (`BufferDesc::debugName`), the swapchain, pipeline,
   command buffers and queues. Makes RenderDoc captures readable.
2. **Depth buffer** (D32_SFLOAT, reverse-Z), an image from the Allocator
   (`gpu::Image`, the counterpart of `gpu::Buffer`), recreated with the
   swapchain; sprites get a depth (layer) value.
3. **Camera**: view/projection in push constants (GLM,
   `GLM_FORCE_DEPTH_ZERO_TO_ONE`), replacing `viewHalf`; follow the player.
4. **Pipeline cache** saved to disk.
5. Optional: cursor → world (`render::screenToWorld`) and aiming.

**To resume, tell the next session:** "Read `aedans-playground/HANDOFF.md` on
branch `claude/relaxed-cray-5kc2fl`, confirm `xvfb-run -a ctest --preset
debug` passes (reinstall the packages in `README.md` if the setup script did
not; `xdotool` is needed for `keyboard_e2e`), then start step 1."
