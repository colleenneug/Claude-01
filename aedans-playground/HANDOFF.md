# Aedan's Playground — session handoff

Paste this file into a new session to resume. The plan is `ROADMAP.md`;
build, run and troubleshooting instructions are in `README.md`.

**Status:** an entity component system (ECS) now drives what is on screen.
The demo spawns 200 entities with Transform, Velocity and Sprite components;
systems move them and bounce them off the window edges; an extract step
copies them into a plain draw list; the Vulkan renderer draws each as a
rotated, alpha-blended square. Screenshot: `docs/ecs-sprites.png`.

- Repository `colleenneug/Claude-01`, branch `claude/relaxed-cray-5kc2fl`, no pull request open.
- Standing guidelines: modern C++20 (smart pointers, RAII, `std::span`,
  designated initialisers); no over-engineering; clean, commented,
  production-ready code; strict error handling and logging for graphics
  initialisation.
- Decisions: C++20 with `tl::expected`; Windows and Linux only; the
  Playground lives in this repository; minimum GPU Vulkan 1.3 with descriptor
  indexing; namespace `playground`, targets `playground_*`; the `Renderer`
  interface lives in the render layer below the engine; the ECS is a fresh
  sparse-set implementation (the `engine/` blueprint is left untouched as
  reference, not moved).
- Numbering: the user calls this "Milestone 4". In `ROADMAP.md` it is the
  ECS part of Milestone 3 (engine core). Roadmap Milestone 2 (GPU memory,
  vertex buffers, depth, camera) has NOT been done yet; the user skipped
  ahead to the ECS, and sprites are drawn without buffers (push constants).
- The user may be young or new to this: when asked to explain, keep it short
  and plain, one idea at a time.

---

## 1. Current Architecture State

```
app/main.cpp   logging -> Application -> createVulkanRenderer -> setRenderer
               -> ecs::World + spawnBouncingSprites -> setUpdate (systems) -> setExtract -> run
  engine/      Application (loop, renderer slot, update + extract hooks);
               Components (Transform, Velocity, Sprite); Systems (movement, bounce, extract)
  ecs/         Entity, ComponentPool<T>, World            (depends only on core)
  render/      Renderer interface, FrameData/SpriteInstance (public, no Vulkan);
               VulkanRenderer + sprite shaders (private)
  gpu/         Context (instance, surface, GPU, device), Swapchain, DeviceHandle, VulkanCheck
  platform/    Platform (GLFW, Ctrl+C), Window
  core/        Result, Log, Assert, FrameClock, Types
```

Dependencies point down only: core ← platform ← gpu ← render ← engine ← app;
ecs depends on core; engine on render + ecs. render links gpu PRIVATE, so no
Vulkan header reaches engine or app.

**The frame** (`engine::Application::run`):
1. Events (poll; or sleep to 60 fps when there is no renderer or the window is minimised).
2. Stop check: Ctrl+C/SIGTERM, `requestQuit()`, window closed.
3. `FrameClock::tick()` → `FrameTime` (delta clamped to 0.25 s).
4. **Update** hook: in main, `movementSystem` → `bounceSystem` → `world.flush()` → FPS in title.
5. **Extract** hook (only on rendered frames): `FrameData` cleared, then `extractSprites` fills it.
6. **Render**: `renderer->renderFrame(time, frameData)`.
7. Frame limit (`--frames N`).

**The ECS** (`ecs/`):
- `Entity{u32 index; u32 generation}`. `World::alive()` checks the slot is in
  use and the generation matches.
- One `ComponentPool<T>` per component type, a sparse set: `sparse[index] →
  slot`, `dense[slot] → Entity`, `data[slot] → T` (packed). `contains()`
  compares the full Entity stored in `dense`, so stale handles see nothing.
  The old `engine/` blueprint only compared the index; that bug is fixed here.
- Pools are found by a per-type integer ID (`detail::componentTypeId<T>()`,
  assigned on first use) indexing a `vector<unique_ptr<PoolBase>>`: no hash map.
- `each<Ts...>(fn)`: one type walks the packed arrays directly; several
  types walk the smallest pool and probe the rest; `fn(Entity, Ts&...)`.
- `destroy()` is deferred to `flush()` (safe inside `each`); `add()` /
  `remove()` / `flush()` inside `each` hit a debug `PG_ASSERT` (iteration
  counter); `create()` is allowed anytime.
- `Component` concept: object type, not const, nothrow move-constructible
  and move-assignable.

**Sprites on the GPU** (`render/src/VulkanRenderer.cpp`): one pipeline, no
vertex buffer. Per sprite: `vkCmdPushConstants` of `SpritePushConstants` (48
bytes: position, halfSize, (cos, sin), viewportHalf, colour; offsets
`static_assert`ed against `sprite.vert`), then `vkCmdDraw(6)`. Standard alpha
blending. World units are pixels, origin at the window centre, +y up
(negative-height viewport).

**Verified this session** (GCC 13.3, Clang 18.1, GCC + ASan + UBSan; Mesa
llvmpipe under Xvfb):

| Check | Result |
|---|---|
| Build, warnings as errors | 0 warnings, GCC and Clang |
| `xvfb-run -a ctest`: core, ecs, app, gpu, render, app_smoke, app_no_display | 7/7 in all three builds |
| Screenshot of the demo | 200 rotated, translucent squares; two screenshots 0.5 s apart differ in ~350k pixels (they move) |
| ECS speed, release | `movementSystem` over 100,000 entities: 0.46 ms (debug: 4.8 ms) |
| Validation | 0 messages |

**Not yet run on a real GPU, and not on Windows.**

## 2. Exact Files & Classes Modified

New:

| File | Contents |
|---|---|
| `ecs/include/playground/ecs/Entity.h` | `Entity`, `kNullEntity`, `std::hash<Entity>` |
| `ecs/include/playground/ecs/ComponentPool.h` | `Component` concept, `PoolBase`, `ComponentPool<T>` |
| `ecs/include/playground/ecs/World.h`, `ecs/src/World.cpp` | `World` (create, destroy, flush, alive, add, remove, get, tryGet, has, count, each), `detail::componentTypeId<T>()` |
| `ecs/CMakeLists.txt` | `playground_ecs` (links core) |
| `engine/include/playground/engine/Components.h` | `Transform`, `Velocity`, `Sprite` |
| `engine/include/playground/engine/Systems.h`, `engine/src/Systems.cpp` | `movementSystem`, `bounceSystem`, `extractSprites` |
| `render/include/playground/render/FrameData.h` | `SpriteInstance`, `FrameData` |
| `render/shaders/sprite.vert`, `render/shaders/sprite.frag` | The push-constant quad |
| `tests/ecs_tests.cpp` | 14 tests: lifetime, deferred destroy, generations/stale handles, add/replace/remove, swap-and-pop integrity, 1/2/3-component queries, destroy during iteration, the three systems, 100k-entity timing |
| `docs/ecs-sprites.png` | Screenshot of the demo |

Deleted: `render/shaders/triangle.vert`, `render/shaders/triangle.frag`
(the triangle pipeline became the sprite pipeline).

Changed:

| File | Change |
|---|---|
| `render/include/playground/render/Renderer.h` | `renderFrame(const FrameTime&, const FrameData&)` |
| `render/src/VulkanRenderer.h/.cpp` | Sprite pipeline: push-constant range, alpha blending, per-sprite push + draw |
| `render/include/playground/render/VulkanRenderer.h` | Comments only |
| `render/CMakeLists.txt` | Sprite shaders; links `glm::glm` PUBLIC |
| `engine/include/playground/engine/Application.h`, `engine/src/Application.cpp` | `ExtractFn`, `setExtract()`, `frameData_` reused each frame |
| `engine/CMakeLists.txt` | `Systems.cpp`; links `playground::ecs` |
| `CMakeLists.txt` | `find_package(glm CONFIG REQUIRED)`; `add_subdirectory(ecs)` |
| `vcpkg.json` | `glm` |
| `app/main.cpp` | `--sprites N`; `spawnBouncingSprites`; systems in the update hook; `extractSprites` in the extract hook |
| `tests/app_tests.cpp` | `FakeRenderer::renderFrame` takes `FrameData` |
| `tests/render_tests.cpp` | The frame test now draws two sprites (rotation, blending) |
| `tests/CMakeLists.txt` | `ecs_tests` |
| `README.md` | ECS section, sprite drawing, `--sprites`, new screenshot |

## 3. Code Snippet of the Core Loop/Active Component

The game loop's ECS wiring, `app/main.cpp`:

```cpp
ecs::World world;
spawnBouncingSprites(world, options->sprites, halfSizeOf(application.window().framebufferSize()));

TitleFpsCounter fpsCounter(application.window());
application.setUpdate([&](const FrameTime& time) {
  const auto deltaSeconds = static_cast<f32>(time.deltaSeconds);
  engine::movementSystem(world, deltaSeconds);
  engine::bounceSystem(world, halfSizeOf(application.window().framebufferSize()));
  world.flush();  // apply any destroys the systems queued
  fpsCounter.update(time);
});
application.setExtract([&world](render::FrameData& frame) { engine::extractSprites(world, frame); });
```

The system that moves things, `engine/src/Systems.cpp`:

```cpp
void movementSystem(ecs::World& world, f32 deltaSeconds) {
  world.each<Transform, Velocity>([deltaSeconds](ecs::Entity, Transform& transform, const Velocity& velocity) {
    transform.position += velocity.linear * deltaSeconds;
    transform.rotation += velocity.angular * deltaSeconds;
  });
}
```

The query at the heart of it, `World::each` in `ecs/include/playground/ecs/World.h`:

```cpp
template <Component... Ts, class Fn>
void each(Fn&& fn) {
  const std::tuple<ComponentPool<Ts>*...> pools{findPool<Ts>()...};
  if (((std::get<ComponentPool<Ts>*>(pools) == nullptr) || ...)) return;  // a type nobody has
  IterationGuard guard(iterating_);

  if constexpr (sizeof...(Ts) == 1) {
    auto* p = std::get<0>(pools);                      // one type: walk the packed arrays
    const std::span<const Entity> entities = p->entities();
    const auto components = p->components();
    for (usize i = 0; i < entities.size(); ++i) fn(entities[i], components[i]);
  } else {
    const std::array<const PoolBase*, sizeof...(Ts)> bases{std::get<ComponentPool<Ts>*>(pools)...};
    const PoolBase* smallest = bases[0];               // several: walk the smallest, probe the rest
    for (const PoolBase* b : bases) if (b->size() < smallest->size()) smallest = b;
    const std::span<const Entity> entities = smallest->entities();
    for (usize i = 0; i < entities.size(); ++i) {
      const Entity e = entities[i];
      if ((std::get<ComponentPool<Ts>*>(pools)->contains(e) && ...)) {
        fn(e, std::get<ComponentPool<Ts>*>(pools)->getUnchecked(e)...);
      }
    }
  }
}
```

## 4. Known Bugs & Incomplete Tasks

No known bugs in our code.

**Not verified**
- A real GPU and Windows (presets, vcpkg, Vulkan SDK, MSVC warnings). `glm`
  was added to `vcpkg.json` this session; vcpkg's GLM is 1.0 (Ubuntu's is
  0.9.9.8). Only `vec2`/`vec4`/`abs` are used, which both have.
- Minimise/restore on a real desktop (Xvfb has no window manager).
- No automated pixel check; screenshots were checked by hand and by script.

**Known limitations, acceptable for now**
- **Variable timestep.** Systems run once per frame with the clamped frame
  delta. The roadmap's fixed timestep with interpolation (the tested
  `erebus::GameLoop` in `engine/`) is not ported yet; movement is therefore
  frame-rate-dependent in edge cases (bounces, future physics).
- **One draw call per sprite** (push constants). Fine for hundreds;
  thousands need one instanced draw from a buffer, which needs roadmap
  Milestone 2 (VMA).
- **Log file lines below warning are buffered**, so `tail -f playground.log`
  lags until a warning, a flush or exit. Consider `flush_every` or flushing
  at info level in debug builds.
- **No input layer**: nothing responds to keyboard or mouse yet.
- **`bounceSystem` uses the sprite size as a collider** until there is a
  collider component.
- **Component type IDs are per run** (assigned on first use): never save
  them to disk.
- Carried over: swapchain recreation uses `vkDeviceWaitIdle`; fences rather
  than timeline semaphores; no Vulkan debug names; no pipeline cache.
- In GCC sanitizer builds the Mesa driver is preloaded for GPU tests (Mesa
  leak-report artefact; see `tests/CMakeLists.txt`); running
  `build/asan/app/playground_app` by hand without the preload shows a
  ~112-byte report from the driver. Expected.

## 5. Immediate Next Steps for Next Weekend

Recommended order (user to choose between A and B first):

**A. Finish roadmap Milestone 3 (engine core), smallest steps:**
1. **Fixed timestep.** Port `erebus::GameLoop` into
   `engine/include/playground/engine/FixedStep.h` (or into
   `Application::run`): systems run in fixed 1/120 s steps; `Transform`
   gains a previous position so extraction interpolates by the leftover
   fraction. Add `ApplicationConfig::fixedStepSeconds`; the update hook
   becomes the fixed step.
2. **Input actions.** `platform/Input.h`: key/mouse state per frame with
   pressed/released edges, from GLFW callbacks in `Window.cpp`; then an
   action map (`Move`, `Fire`) as data. First use: a player-controlled
   sprite with a `PlayerTag` component and an `inputSystem`.
3. Frame arenas (roadmap §6) only when a profile shows allocation cost.

**B. Roadmap Milestone 2 (renderer foundations):** VMA (`gpu/Allocator.h`),
`gpu::Buffer`, a staging upload, then instanced sprites from one storage
buffer (one draw for all sprites), a depth buffer, a camera.

**To resume, tell the next session:** "Read `aedans-playground/HANDOFF.md` on
branch `claude/relaxed-cray-5kc2fl`, confirm `xvfb-run -a ctest --preset
debug` passes (reinstall the packages in `README.md` if the setup script did
not), then start step A1, the fixed timestep."
