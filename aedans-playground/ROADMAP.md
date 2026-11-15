# Aedan's Playground — architecture roadmap

A small C++20 / Vulkan engine, built from scratch to learn engine architecture,
memory management and data-oriented design — and to host **Erebus Cradle**, so
the game stops being limited by its OpenGL 4.1 renderer.

This document is strategy only. There is no engine code in this folder yet.

---

## 1. Where we are starting from

The Playground is not a blank page. Two trees in this repository already hold
most of the hard-won decisions:

| Tree | What it is | What the Playground takes from it |
|---|---|---|
| `cpp/` | The working game. ~22,500 lines of C++17 on OpenGL 4.1, GLFW, GLM and miniaudio. Campaign, hub, open space, co-op over its own UDP layer, content in `.cfg` files. | The game itself, its content files, its network protocol, its audio, and its look (the reference image the new renderer has to match before it tries to beat it). |
| `engine/` | A C++20 blueprint. Fixed-timestep loop, sparse-set ECS, player controller, weapon model and a CPU clustered-light binner are written and pass 32/32 tests. The Vulkan backend is a bring-up checklist, not a device. | The tested, GPU-free code moves over as-is. The Vulkan checklist becomes the plan for Milestone 1. |

Two facts about `cpp/` shape the whole plan:

1. **It already has a render seam.** Gameplay fills a list of `DrawItem`s
   through `SceneSource`, and `Renderer::renderFrame` turns that list into a
   frame. The new renderer has to accept the same kind of list. That is what
   makes hosting the game a port and not a rewrite.
2. **The seam leaks.** `Gl.h` is included by nearly every header, and ten
   gameplay files poll GLFW keys directly. Those two leaks are the real work of
   the port, and they are why Input is an engine subsystem here and not a
   detail of the window.

---

## 2. Principles

These settle most arguments before they start.

- **Vulkan only, no cross-API layer.** `engine/` sketched an RHI that could sit
  over Vulkan and D3D12. The Playground drops that. One backend, wrapped just
  enough that nothing above the renderer includes a Vulkan header.
- **Dependencies point downward only.** Game → engine systems → renderer →
  platform → core. Nothing in a lower layer knows a higher one exists.
- **Subsystems talk through plain data.** The renderer is handed a list of
  things to draw. Physics is handed transforms and returns transforms. No
  subsystem holds a pointer into another.
- **Build the simple version first, and name the trigger for the complex one.**
  A hard-coded list of render passes until manual barriers hurt; one thread
  until a profile shows a gap; AABB collision until a mission needs more.
- **Nothing counts as working until it has run.** `engine/` was honest that its
  shaders had never met a device. The same rule holds here: every milestone has
  an exit test that is observed, not assumed.

---

## 3. Layering

```
┌──────────────────────────────────────────────────────────────┐
│  game/        Erebus Cradle: missions, hub, space, HUD, net  │
├──────────────────────────────────────────────────────────────┤
│  engine/      ecs · scene · physics · audio · assets · input │
├──────────────────────────────────────────────────────────────┤
│  render/      frame passes · materials · render world        │
│  gpu/         Vulkan: device, swapchain, resources, commands │
├──────────────────────────────────────────────────────────────┤
│  platform/    window · raw input events · clock · filesystem │
├──────────────────────────────────────────────────────────────┤
│  core/        log · assert · result · memory · handles · math│
└──────────────────────────────────────────────────────────────┘
```

Each box is a static library with its own public include folder, so a wrong
`#include` is a build error and not a code-review comment. `core` and the
gameplay-facing parts of `engine` have no GPU dependency, which keeps them
testable on a machine with no graphics card.

---

## 4. Subsystems

### 4.1 Core

The layer everything else stands on. Small, and written first.

- **Logging.** Severity levels, a category per subsystem, output to console and
  a file. Vulkan validation messages are routed into the same log, so there is
  one place to look.
- **Asserts and fatal errors.** An assert that reports file and line through
  `std::source_location` and breaks into the debugger. A fatal path that logs,
  flushes and exits cleanly.
- **Result type.** Fallible functions return `Result<T>` (value or error with a
  message). No exceptions cross a subsystem boundary. See §5.
- **Handles.** A 32-bit index plus a generation counter, used for entities,
  meshes, textures and anything else that can be destroyed while something
  still refers to it. A stale handle is detected; a stale pointer is a crash.
- **Memory.** See §6. This is the main learning goal, so it gets its own
  section.
- **Math.** GLM, configured once in a single header for Vulkan's conventions.

### 4.2 Platform — window, events, time, files

- Creates the window and the Vulkan surface.
- Turns OS events into the engine's own event structs. No GLFW type leaves this
  layer.
- A monotonic clock, and a filesystem helper that resolves paths relative to
  the executable (the game already relies on this for `content/` and
  `shaders/`).

### 4.3 Input

A deliberately separate subsystem, because of the leak described in §1.

- **Raw layer:** key, mouse and gamepad state for this frame, with
  pressed/released edges.
- **Action layer:** gameplay asks "is `Fire` held", "what is the `Move` axis",
  never "is `W` down". Bindings are data, which gives rebinding and gamepad
  support for the cost of one table.
- Sampled once per frame and handed to the simulation as a value, so the fixed
  timestep sees a consistent snapshot and recorded input can be replayed in a
  test.

### 4.4 GPU layer — Vulkan

The long pole of the project, and the highest risk. Built in the order
`engine/src/render/vulkan/VulkanDevice.cpp` already lays out:

1. Instance, with validation layers and the debug messenger on from the very
   first run.
2. Surface and physical-device selection, with an explicit list of required
   features and a logged reason for every rejected device.
3. Logical device and queues. One graphics queue to begin with.
4. Memory allocator.
5. Swapchain, including recreation on resize and minimise.
6. Per-frame resources: command pool, upload ring, deletion queue.

Decisions made once, up front:

| Decision | Choice | Why |
|---|---|---|
| Baseline | Vulkan 1.3 | Dynamic rendering and synchronization2 are core, which removes render-pass and framebuffer objects entirely. |
| Binding model | Bindless: one large texture array, materials are an index | Retrofitting it later means touching every shader. |
| Depth | Reverse-Z, 32-bit float, infinite far plane | Open space spans tens of thousands of units; standard depth runs out of precision there. |
| Clip space | Y-flip via negative viewport height | One line in one place, and GLM projections stay unmodified. |
| Resource lifetime | RAII for long-lived objects (device, swapchain); handles plus a deferred deletion queue for per-frame resources | A GPU resource cannot be freed the moment its owner dies: a frame in flight may still use it. |
| API style | The C API with thin RAII wrappers of our own | C++20 designated initialisers make `Vk…CreateInfo` structs readable, and the C API is what every tutorial, RenderDoc capture and validation message speaks. |

### 4.5 Renderer

Sits on the GPU layer and owns what a frame is.

- **Render world.** Each frame, visible things are copied out of the ECS into a
  flat array of draw records (the successor of `DrawItem`). The renderer never
  reads gameplay state, which is also what makes a render thread possible later
  without redesign.
- **Passes.** Start as a plain ordered list: shadows → depth prepass → opaque →
  transparent → post → UI. A frame graph is introduced only when transient
  targets and barrier bookkeeping become error-prone, expected around
  Milestone 4.
- **Materials.** The game has no texture assets: every surface is shaded
  procedurally from position and normal. So the first material system is small,
  a handful of shader variants and a parameter block, and a texture-based path
  is added only when assets arrive.
- **Shaders.** GLSL compiled to SPIR-V at build time by CMake. A shader error
  is a build error. The existing `cpp/shaders/*.frag` are GLSL 4.10 and port
  with mechanical changes (explicit locations and bindings).

### 4.6 Simulation loop

Taken from `engine/` unchanged: a fixed-timestep simulation with the render
interpolating between the last two states. Each frame runs
input → fixed updates (zero or more) → extract → render.

### 4.7 ECS

Keep the sparse-set ECS already written and tested in `engine/`. Writing one is
the single best exercise in data-oriented design, and it is done.

Not everything goes in it. Entities that exist in numbers and are processed in
bulk (hostiles, projectiles, pickups, lights, net-replicated players) are
components. One-off things (the player's loadout, the mission script, the hub
UI) stay ordinary objects. Forcing singletons into an ECS is the common
over-engineering mistake.

### 4.8 Physics

Two stages, so the port is not blocked on a physics engine.

- **Stage 1:** move the game's current AABB colliders and the tested kinematic
  player controller behind a small query interface (sweep, raycast, overlap).
  Add a uniform grid when the open-world planets make the O(n) scan show up in
  a profile.
- **Stage 2:** put Jolt behind the same interface for rigid bodies, triangle
  meshes and ragdolls. The player stays kinematic: shooter movement is tuned by
  hand, not simulated.

### 4.9 Audio

Keep miniaudio and move `Audio.cpp` over nearly as it stands. Later, enable
miniaudio's spatialiser for positional sound; that is a build flag, not a new
dependency.

### 4.10 Assets and content

- **Content:** the `.cfg` format and its parser move over untouched. It is the
  game's best property, since new missions need no rebuild, and nothing here
  should weaken it.
- **Assets:** nothing to load at first, since everything is procedural. When
  authored models arrive, glTF is the interchange format, with an offline step
  that bakes meshes and textures into GPU-ready files so load time stays a
  `memcpy`.

### 4.11 Networking

The game's own UDP client, server and protocol are GL-free and already tested
by scripts in `cpp/tools/`. They move into `game/`, not into the engine: the
protocol is specific to Erebus Cradle, and a generic netcode layer is exactly
the kind of subsystem this project should not build on speculation.

### 4.12 Developer tooling

Built early, because it pays for itself during Vulkan bring-up.

- An in-engine debug overlay: frame timings, toggles, entity inspector.
- CPU and GPU profiling zones.
- Debug names on every GPU object, so a RenderDoc capture is readable.
- A unit-test target for everything with no GPU dependency.
- A headless smoke test that renders N frames and exits non-zero on any
  validation message.

---

## 5. Error handling and logging for graphics initialisation

Graphics start-up is where a machine we have never seen fails in a way we
cannot reproduce, so the policy is strict.

- **Every Vulkan call's `VkResult` is checked.** No exceptions (in either
  sense). A helper logs the call, the result code as text, and the call site.
- **Initialisation returns `Result`, and failure is a sentence.** Not
  "device creation failed" but "no GPU supports descriptor indexing; found:
  Intel HD 4000 (Vulkan 1.0)". Device selection logs every device it saw and
  why each was rejected.
- **Validation is on in every debug build**, and a validation error is an
  assert, not a line that scrolls past.
- **Three outcomes are told apart**: recoverable and expected (swapchain out of
  date → recreate), unrecoverable but clean (missing feature → message to the
  user, exit), and bug (validation error → break).
- **Start-up logs the environment**: GPU, driver version, enabled extensions,
  chosen surface format and present mode. A bug report then carries its own
  context.
- **Teardown is the reverse of bring-up and verified**: the allocator reports
  leaked allocations at exit, and a leak fails the smoke test.

---

## 6. Memory strategy

| Kind of memory | Lifetime | Allocator |
|---|---|---|
| Scratch data built and discarded within a frame (draw lists, sort keys, string formatting) | One frame | Linear arena, reset at frame start. Allocation is a pointer bump; freeing is free. |
| Level geometry, colliders, spawn tables | One mission | Linear arena, released whole when the mission unloads. |
| Many objects of one size, created and destroyed often (projectiles, particles, net messages) | Arbitrary | Fixed-size pool with a free list, addressed by handle. |
| Component data | Entity lifetime | Dense arrays inside the ECS's sparse sets. |
| Long-lived subsystem objects | Program | Ordinary `std::unique_ptr`, created at start-up in a fixed order. |
| GPU memory | Resource lifetime plus frames in flight | VMA, behind handles, freed through the deletion queue. |

Ownership rules that follow from this:

- `std::unique_ptr` for "this subsystem owns that object".
- Handles for anything shared or destroyable. `std::shared_ptr` is not used:
  shared ownership hides lifetime, and lifetime is what this project is trying
  to make visible.
- `std::span` for every "here is some contiguous data" parameter, so no API
  cares which allocator the data came from.
- The standard library's `std::pmr` containers can sit on top of our arenas,
  which gives `vector` and `string` on frame memory without writing containers.

---

## 7. Third-party libraries

Chosen for being small, widely used and easy to remove. Each one sits behind a
single engine header, so replacing it touches one file.

### Adopt from the start

| Need | Library | Notes |
|---|---|---|
| Window, surface, raw input | **GLFW** | Already used by `cpp/`. SDL3 is the alternative if controller rumble or mobile ever matter. |
| Math | **GLM** | Already used by `cpp/`. Configure depth range 0..1 in one place. |
| Vulkan function loading | **volk** | Loads device-level function pointers directly, skipping the loader's dispatch. |
| Instance, device and swapchain selection | **vk-bootstrap** | Removes several hundred lines of boilerplate from the riskiest stage. Can be replaced by hand-written code later as a learning exercise. |
| GPU memory | **Vulkan Memory Allocator (VMA)** | Not worth hand-rolling. Also reports leaks at shutdown. |
| Shader compilation | **glslc** (from the Vulkan SDK) | Run by CMake at build time. |
| Logging | **spdlog** | Fast, has sinks for console and file. |
| Result type | **tl::expected** | A single header with the same interface as C++23's `std::expected`, so moving to the standard type later is a one-line alias. |
| Debug UI | **Dear ImGui** | Ships Vulkan and GLFW backends. |
| Audio | **miniaudio** | Already vendored in `cpp/third_party/`. |
| Tests | **doctest** | Single header, fast to compile. |

### Add when the milestone needs it

| Need | Library | Trigger |
|---|---|---|
| Profiling | **Tracy** | Milestone 2, as soon as there is a frame worth measuring. |
| Physics | **Jolt Physics** | First mission that needs rigid bodies or mesh collision. |
| Model loading | **cgltf** or **fastgltf** | First authored model. |
| Image loading | **stb_image** | First authored texture. |
| Compressed textures | **KTX-Software** (KTX2 / Basis) | When texture memory or load time becomes a measured problem. |
| Mesh optimisation | **meshoptimizer** | With the offline asset step. |
| Shader reflection | **SPIRV-Reflect** | Only if hand-maintained binding layouts start drifting from the shaders. |
| Job system | **enkiTS** | When a profile shows a frame that one thread cannot finish. |

### Deliberately not adopted

- **EnTT** — excellent, but we already have a tested ECS and writing one is a
  stated goal.
- **Vulkan-Hpp** — long compile times, and a second vocabulary on top of the
  one every reference uses.
- **A networking library** — the game's own UDP layer works and is tested.
- **A D3D12 or Metal backend** — see §2.

### Build and dependency management

CMake with presets (debug, release, and a debug build with sanitizers).
Dependencies through a **vcpkg manifest**: the Windows build of `cpp/` already
uses vcpkg, and a manifest pins versions so every machine builds the same
thing. Single-header libraries are vendored into `third_party/`.

---

## 8. Milestones

Renderer first, because Vulkan is the highest risk and the game's gameplay is
already proven. Each milestone ends in something that has been run and seen.

| # | Milestone | Scope | Exit test |
|---|---|---|---|
| 0 | **Skeleton** | Folder layout, CMake presets, vcpkg manifest, logging, asserts, `Result`, test target. | A window opens, logs its start-up, and closes cleanly. Tests run. |
| 1 | **Vulkan bring-up** | Instance, validation, device selection, swapchain, one triangle with dynamic rendering. | Triangle on screen with zero validation messages; survives resize, minimise and alt-tab; clean shutdown with no leaks. |
| 2 | **Renderer foundations** | VMA, buffers and images, upload path, deletion queue, depth, camera, shader build step, pipeline cache, debug overlay, profiler. | Thousands of procedural meshes with a fly camera, at a measured frame time. |
| 3 | **Engine core** | Fixed-timestep loop, input actions, ECS, arenas and pools, render-world extraction. Mostly moved from `engine/`. | An ECS-driven scene simulating at a fixed rate while rendering uncapped; existing 32 tests still pass. |
| 4 | **Visual parity** | HDR target, the procedural PBR materials, cascaded shadows, IBL, bloom, depth of field, fog, composite, dust motes, HUD text. | One mission scene rendered in both builds, compared side by side. |
| 5 | **Host Erebus Cradle** | `game/` module on the engine. Port by seam: draw lists, then input, then collision; audio, content, profile and networking move nearly untouched. | Mission 1 (HOME) playable start to finish on the Playground; then the hub; then a two-client co-op session. |
| 6 | **Better results** | The gains OpenGL 4.1 could not give: real clustered point lights, froxel volumetrics, TAA, contact shadows, screen-space reflections. Independent of each other, done in that order. | Each one toggled on and off in the overlay, with a before/after capture. |

After Milestone 6, in no fixed order and each only on a concrete need: Jolt,
authored assets, a job system, a frame graph if Milestone 4 did not already
force it.

**The `cpp/` build stays working throughout.** It is the reference image for
Milestone 4 and the fallback until Milestone 5 passes. Nothing is deleted from
it until the Playground build plays the whole campaign.

---

## 9. Risks

| Risk | Mitigation |
|---|---|
| Vulkan bring-up stalls with nothing on screen for weeks. | vk-bootstrap and VMA for the first pass; validation on from the first line; Milestone 1 is one triangle and nothing else. |
| The port turns into a rewrite. | Port along the existing `SceneSource` / `DrawItem` seam; gameplay logic is moved, not redesigned. ECS adoption is gradual: hostiles and projectiles first, the rest only if it helps. |
| The new renderer looks worse than the old one for a long time. | Milestone 4 is parity, measured against captures, before any new technique. |
| The engine grows features the game never uses. | Every subsystem beyond Milestone 3 needs a named mission or feature in Erebus Cradle that requires it. |
| Vulkan code cannot be exercised where it is written. | Decide at Milestone 1 how GPU code is verified: a software Vulkan driver (Mesa lavapipe) for headless smoke tests if it can be installed, otherwise every GPU milestone is confirmed on a real machine before it is called done. |

---

## 10. Open decisions

Recommendations given; each is cheap to change now and expensive later.

1. **Language level.** Stay on C++20 with `tl::expected`, as specified. Moving
   to C++23 would give `std::expected` directly; the only reason not to is
   compiler support on the Windows toolchain.
2. **Target platforms.** Windows and Linux. `cpp/` targets OpenGL 4.1, which is
   the newest version macOS supports; if macOS matters, Vulkan there goes
   through MoltenVK and the required-feature list in §4.4 needs checking
   against it before Milestone 1.
3. **Location.** This folder, in this repository, so the Playground can build
   the game's sources and content in place. A separate repository only becomes
   worthwhile if the engine is ever used for a second game.
4. **Minimum GPU.** Vulkan 1.3 with descriptor indexing covers discrete GPUs
   from roughly 2016 onward and recent integrated ones. Older hardware keeps
   the `cpp/` build.
