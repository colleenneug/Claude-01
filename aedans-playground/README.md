# Aedan's Playground

A C++20 / Vulkan engine being built to host Erebus Cradle. The plan is in
[`ROADMAP.md`](ROADMAP.md); where the last session stopped is in
[`HANDOFF.md`](HANDOFF.md).

**Current state:** a window, a fixed-timestep main loop, a Vulkan renderer
that draws textured 3D meshes lit by the sun (with real-time shadows) and
by their surroundings (image-based lighting, with a skybox) into an HDR
image, then adds bloom and ACES tone mapping; 2D sprites; glTF 2.0 model
and HDR panorama loading; networking (ENet transport, bit-packed
quantized messages) with an authoritative headless dedicated server
(`--server`) and a windowed client (`--connect`) that predicts its own
player and interpolates everything else;
an entity component system (ECS), keyboard/mouse input, an orbit/fly
camera, a job system (worker threads) and an asset manager that loads files
in the background.

The demo has two scenes. The default is **3D**: glTF models (a tiled floor,
textured crates, a row of spheres from rough plastic to polished gold,
glowing lamps, and Khronos's Damaged Helmet on a pedestal) lit by a low sun
that casts shadows and a procedural sky they reflect, which you look
around with the mouse. `--model PATH` shows any glTF file instead, and `--environment
PATH.hdr` lights the scene with a photographed HDR panorama. `--scene sprites` is the **2D** demo: steer a white
square among 25,000 bouncing ones, all drawn in one instanced draw call.

![The 3D scene: sun shadows, glowing lamps with bloom, reflections, the Damaged Helmet](docs/shadows-bloom.png)

*The 3D scene, captured from the running app on Mesa's software Vulkan
driver: soft sun shadows from every object, lamp bulbs blooming, metals
reflecting the sky, ACES tone mapping. The helmet is "Damaged Helmet" from
the Khronos glTF samples (© 2018 ctxwing, CC BY 4.0; based on work © 2016
theblueturtle_, CC BY-NC 4.0: see
`app/assets/third_party/DamagedHelmet/LICENSE.md`). Earlier: reflections
only (`docs/ibl-scene.png`), before reflections (`docs/gltf-scene.png`),
milestones: the first triangle (`docs/first-triangle.png`),
the first ECS sprites (`docs/ecs-sprites.png`), 25,000 instanced sprites
(`docs/instanced-25k.png`), the tilted 2D view (`docs/perspective-3d.png`)
and the network client (`docs/network-client.png`: you in white, a bot in
orange, the server's 64 balls).*

**3D scene** (camera: `engine/CameraController.h`)

| Input | Orbit mode (the start) | Fly mode |
|---|---|---|
| Left-drag | spin around the target | |
| Right-drag | pan the target | look around |
| Wheel | zoom | change the fly speed |
| W A S D, arrows | tilt and spin | move where you look |
| Q / E | zoom out / in | down / up (also Ctrl / Space) |
| Shift | | 4x faster |
| Tab | switch to fly | switch to orbit |
| R | reset the view | reset the view |
| Escape | quit | quit |

**2D scene** (`--scene sprites`)

| Key | Action |
|---|---|
| W A S D, arrow keys | Move |
| Mouse | Aim |
| Space, left mouse button | Fire (towards the mouse) |
| Right mouse button | Remove the sprite under the mouse |
| C | Switch between the 2D (orthographic) and 3D (perspective) view |
| Escape | Quit |

## Layout

| Folder | Library | Contents |
|---|---|---|
| `core/` | `playground_core` | `Result`, `Log`, `Assert`, `FrameClock`, `FixedStep`, `JobSystem`, `InterruptGuard` (Ctrl+C) |
| `net/` | `playground_net` (`engine_net`) | `BitWriter`/`BitReader`, quantization, `Host`/`Peer`/`Packet` over ENet (hidden). Depends on core only |
| `platform/` | `playground_platform` | `Platform` (GLFW lifetime), `Window` |
| `gpu/` | `playground_gpu` | Vulkan: `Instance`, `Surface`, `selectPhysicalDevice`, `Device`, `Allocator` (VMA), `Buffer`, `Image` (+ `createTexture`, mipmaps), `Sampler`, `Context`, `Swapchain`, `DeviceHandle` |
| `ecs/` | `playground_ecs` | `Entity`, `ComponentPool`, `World` (create, destroy, add, get, `each<...>`) |
| `render/` | `playground_render` | `Renderer` (interface), `FrameData`, `Resources` (meshes, textures, materials: no Vulkan), `Environment` (sky and panorama descriptions), `Shadow` (shadow-map framing), the Vulkan renderer (`Mesh`, `ResourcePool`, `IblBaker`, `BrdfLut`, `ShadowMap`, `PostProcess`), the shaders |
| `engine/` | `playground_engine` | `Application` (main loop), `Camera`, `CameraController`, `AssetManager`, glTF (`decodeGltf`, `Model`, `gltfLoader`), HDR panoramas (`decodeHdr`, `environmentLoader`), components (`Transform`, `Velocity`, `Sprite`), systems, `pickSprite` |
| `game/` | `playground_game` | the networked game: `Protocol` (v3, with delta snapshots), `Simulation` (the movement rule both sides run), `InputQueue` (the server's rate limit), `Prediction`, `Interpolation`, `GameServer`, `GameClient`. No window, no GPU |
| `app/` | `playground_app` | `main.cpp`, the scenes (`ModelScene`, `SpriteScene`, `NetScene`), the dedicated server and headless bot (`Server`), `assets/` (copied next to the executable; `assets/third_party/` holds the one model we did not make, with its licence) |
| `tests/` | | doctest suites for core, jobs, assets, glTF, image-based-lighting maths, shadow maths, ECS, input, camera, application, GPU, renderer, meshes, environment lighting, shadows with post-processing, networking, the game's pieces, and a server with two clients over a bad simulated network; end-to-end keyboard, camera, dedicated-server and windowed-client tests |
| `tools/` | | `make_demo_assets.py`: writes the demo's glTF models |
| `third_party/` | | vendored `tl::expected` and Vulkan Memory Allocator. tinygltf, nlohmann/json and stb_image come from system packages (Linux) or vcpkg (Windows) |

Each layer may include only the layers below it. Vulkan types stop at the
render layer: its public headers have none, so the engine and the game build
without Vulkan headers.

## Vulkan bring-up, in order

`gpu::Context::create()` runs four steps; each logs what it found, and a
failure says which step failed and why.

| Step | File | What it does |
|---|---|---|
| 1. Instance | `gpu/src/Instance.cpp` | Checks the loader supports Vulkan 1.3 and every needed extension; turns on the validation layer in debug builds and routes its messages into the log |
| 2. Surface | `gpu/src/Surface.cpp` | Vulkan's handle on the window (needed to ask which GPU can present to it) |
| 3. Physical device | `gpu/src/PhysicalDevice.cpp` | Logs every GPU, rejects each one that lacks something (with every reason), scores the rest, picks the best |
| 4. Logical device | `gpu/src/Device.cpp` | Enables exactly the features step 3 checked, creates the device, fetches the graphics (and present) queue |

The requirements both step 3 and step 4 use live in one place,
`gpu/src/Requirements.h`.

## The ECS

An entity is just an ID. Data lives in components, one packed array per
component type, and behaviour lives in systems: plain functions that run over
every entity with the components they need.

```cpp
ecs::World world;
ecs::Entity ship = world.create();
world.add<engine::Transform>(ship, engine::Transform{.position = {0, 0}});
world.add<engine::Velocity>(ship, engine::Velocity{.linear = {40, 0}});

engine::movementSystem(world, step.deltaSeconds);   // every Transform + Velocity
world.flush();                       // apply destroys queued during the systems
```

- `world.each<A, B>(fn)` walks the smallest of the pools and looks the rest
  up, calling `fn(entity, a, b)`.
- `destroy()` is deferred to `flush()`, so a system can destroy what it is
  walking. `add()` and `remove()` during `each()` are refused (debug assert).
- Handles carry a generation: after an entity is destroyed and its slot
  reused, old handles see nothing.
- Speed (release build): `movementSystem` over 100,000 entities takes about
  0.46 ms.

## The frame: fixed simulation steps, smooth drawing

The simulation runs in fixed steps of exactly 1/120 s, so it behaves the same
at any frame rate (a test checks that 30 fps and 144 fps give bit-identical
positions). Each frame, `Application::run`:

1. adds the frame's real duration (clamped to 0.25 s) to an accumulator and
   runs as many fixed steps as it holds: `setFixedUpdate`, 0..n times
   (`core/FixedStep.h`). At most 8 steps run per frame; if the game cannot
   keep up, it slows down briefly rather than freezing;
2. runs `setUpdate` once, with the real frame time (the FPS counter);
3. runs `setExtract` with `alpha`, the leftover fraction of a step in
   [0, 1): `engine::extractSprites` draws each sprite `alpha` of the way from
   its previous position to its current one, so motion is smooth at any
   display rate;
4. renders that list. The renderer never touches the ECS.

In each fixed step the demo runs `snapshotTransforms` (previous = current)
first, then movement and bounce. A newly spawned entity has no previous
state yet and is drawn where it is, so it never streaks in from the origin;
`Transform::teleport()` moves an entity without blending across the jump.

## Input

Three layers, in `platform/Input.h` and `engine/Actions.h`:

- **`InputCollector`** is fed by the window's GLFW callbacks and gives one
  snapshot per frame. Each key and mouse button has `down`, `pressed` and
  `released`; a tap inside one frame shows as pressed and released, never
  lost. **Losing focus** (Alt+Tab) releases everything held, so no key sticks.
- **`InputLatch`** hands input to the fixed steps: every step in a frame
  sees the same held keys, but each press reaches exactly **one** step. A
  slow frame running four steps fires once, not four times; a fast frame
  running none keeps the press for the next step.
- **`ActionMap`** maps keys to actions as data: `Move` (a 2D axis, never
  longer than 1, so diagonals are not faster) and `Fire`. Game code asks
  `actions.pressed(input, actions::kFire)`, never "is Space down".

The fixed update receives the latched input; the per-frame update receives
the frame's own snapshot.

## World bounds and the view

The playing area is a `WorldBounds` owned by the game (1600×900 world
units), and `bounceSystem` reads only that, never the window. Resizing
changes the view, not the game.

## Camera

`engine::Camera` (`engine/Camera.h`) looks from `position` at `target` and
projects one of two ways:

- **Orthographic** (the default 2D view): `orthoHalfExtent` is the world
  area that must be visible, fitted to the window without stretching
  (`render::fitView`), with bars where the shapes differ.
- **Perspective** (`glm::perspective`): a 3D view; farther things look
  smaller. Press **C** in the demo to see the playfield tilted in 3D.

Both are converted to **reverse-Z** (depth 1 at the near plane, 0 at the far
plane), which spreads floating-point depth precision evenly. Each frame the
game puts `camera.viewProjection(viewport)` in `FrameData`, and the renderer
copies it into a per-frame **uniform buffer** that the vertex shader reads
(descriptor set 0, binding 0).

`camera.screenToWorld(cursor, windowSize)` turns a mouse position into the
point on the gameplay plane under it, by casting a ray through the inverse
view-projection, so it works in both projections. The demo uses it to aim
bullets and, with `engine::pickSprite`, to remove the sprite you right-click.

![The 3D (perspective) view](docs/perspective-3d.png)

## Drawing a frame

`render/src/VulkanRenderer.cpp`, once per frame:

1. **Wait** for the GPU to finish the frame that last used this slot (two
   frames in flight).
2. **Acquire** a swapchain image to draw into.
3. **Free** meshes and textures destroyed at least two frames ago.
4. **Write the frame's data** straight into this frame slot's buffers
   (host-visible, mapped once and kept mapped; they double when outgrown):
   the mesh draw list, the sprites (opaque first, then translucent ones
   sorted back to front), and the camera, lights and the sun's shadow
   matrix in the uniform buffer.
5. **Record**, when there are meshes or a sky (otherwise straight to step
   5d, as the 2D demo does):
   1. the **shadow map**: every shadow-casting mesh, depth only, from the
      sun;
   2. the **scene** into the HDR image (and the depth buffer): every mesh
      (one indexed draw each), then the sky where no mesh drew;
   3. **bloom**, if on: down and back up the bloom chain;
   4. the **swapchain image**: the composite (HDR + bloom, exposure, tone
      mapping, sRGB), then **all** sprites with at most two instanced
      draws, `vkCmdDraw(6 vertices, N instances)`: opaque sprites with
      depth writes, then translucent ones without (they still
      depth-test against the scene's depth). Then the image moves into
      the present layout.
6. **Submit** to the graphics queue.
7. **Present** the image to the window.

The vertex shader (`render/shaders/sprite.vert`) picks its sprite with
`gl_InstanceIndex`, reads it through a buffer reference
(`GL_EXT_buffer_reference`: the storage buffer is found by its 64-bit GPU
address), and multiplies by the camera matrix from the uniform buffer. The
quad's corners are constants in the shader.

**Depth.** A `D32_SFLOAT` depth buffer (reverse-Z: cleared to 0, test
`GREATER_OR_EQUAL`), rebuilt with the swapchain. A sprite's `layer` is its
world z, so higher layers are in front whatever the draw order; equal
layers keep draw order. Translucent sprites follow the standard rule for
transparency: sorted back to front, depth tested but not written, so they
blend with what is behind them and many of them on one plane do not
z-fight in the 3D view.

**Frame capture.** `requestCapture()` / `takeCapture()` copy a rendered
frame back to the CPU as RGBA; the renderer tests use it to check actual
pixel colours (depth order, translucency, the camera). The shaders are compiled by `glslc` during the build and
embedded in the executable (see `cmake/Shaders.cmake`), so there are no shader
files to ship. When the window is resized, or present reports the swapchain is
out of date, the swapchain is rebuilt at the new size.

## 3D meshes, textures and materials

The renderer draws **meshes** before sprites, sharing the camera and depth
buffer. Everything above the render layer uses plain data
(`render/Resources.h`): `MeshData` (vertices with position, normal,
tangent and UV, plus 32-bit indices), `ImageData` (RGBA8), and **handles**
back from `Renderer::createMesh` / `createTexture`. Each frame the game
lists `MeshInstance`s (mesh, material, transform) in `FrameData`.

- **Meshes** (`render/src/Mesh.h`): vertex and index buffers in device-local
  memory, uploaded once through a staging buffer. Indices are checked
  first: one past the last vertex is an error, not a GPU fault.
- **Textures** (`gpu::createTexture`): staging copy, then the GPU builds the
  full **mip chain** with linear blits, moving each level through
  TRANSFER_DST → TRANSFER_SRC → SHADER_READ_ONLY. Base colour is stored as
  sRGB (so mips average in linear light), normal and metallic-roughness
  maps as plain data. One trilinear sampler with anisotropic filtering
  (when the GPU has it) serves every texture.
- **Bindless**: every texture sits in one big descriptor array (set 1,
  4,096 slots, update-after-bind); a material names its textures by slot.
  Slots 0 and 1 hold the defaults (white, flat normal) for textures a
  material lacks.
- **Draws**: per frame, one 176-byte entry per mesh instance (transform,
  normal matrix, material) in a storage buffer read by GPU address; one
  `vkCmdDrawIndexed` per instance with `firstInstance` = its entry.
  Double-sided materials use a second pipeline without culling.
- **Shading** (`render/shaders/mesh.frag`): glTF's metallic-roughness model,
  GGX/Smith/Schlick specular, normal mapping, one directional sun, the
  environment's light (next section), and an optional flat ambient fill.
  No shadows yet.
- **Destroying** a mesh or texture makes its handle stale at once; the
  memory is freed after every frame that might still use it has finished.

### glTF 2.0 models

`engine/Gltf.h` plugs into the asset manager:

```cpp
assets.registerType<engine::Model>(engine::gltfLoader(renderer));
auto helmet = assets.load<engine::Model>("models/helmet.glb");
// once ready, each frame:
if (const engine::Model* model = assets.get(helmet)) model->extract(transform, frame);
```

On a worker, `decodeGltf` reads `.gltf` or `.glb` (tinygltf), decodes PNG
and JPEG images to RGBA (stb_image), converts triangle primitives
(positions, normals, tangents, UVs, any glTF component type and stride),
makes flat normals and tangents when a file lacks them, and flattens the
node tree. On the main thread, `uploadModel` creates the meshes and
textures. A `Model` destroys them when unloaded, so `AssetManager::clear()`
runs before the renderer shuts down. Files with extensions we lack marked
as *required* (Draco, meshopt, KTX2) fail with a message naming them.
Emissive colour and textures are read, with `KHR_materials_emissive_strength`.
Skins, animation, morph targets and alpha blending are not read yet (see
`engine/Gltf.h`).

The demo's models are our own, generated by `tools/make_demo_assets.py`
(Python standard library only), except the Damaged Helmet from the Khronos
samples, kept in `app/assets/third_party/` with its licence: **it may not
be used commercially** (remove it before anything built on this engine is
sold). Other Khronos samples (Avocado, BoxTextured,
NormalTangentMirrorTest) were checked with `--model` and render
correctly, mirrored tangents included; they are not in the repository.

## Reflections: image-based lighting and the sky

Real objects are lit by everything around them: the sky from above, the
ground from below, and shiny surfaces mirror their surroundings. The
renderer takes an **environment** (`render/Environment.h`), either a
`ProceduralSky` (colours for the zenith, horizon and ground, and a glow
round the sun; computed in a shader, so it needs no file and bakes in
milliseconds) or an `HdrImage` (a photographed panorama, equirectangular,
from a Radiance `.hdr` file), and **bakes** it on the GPU into three cube
maps (`render/src/IblBaker.cpp`, four compute shaders, one submission):

| Map | What it holds | Used for |
|---|---|---|
| environment | the sky itself, with mips | the skybox; the source of the other two |
| irradiance | for each direction, the light a matte surface facing it gets (cosine-weighted average of the hemisphere) | diffuse light on non-metals |
| specular | the sky as surfaces of each roughness reflect it: mip 0 sharp, the last fully rough (GGX-weighted) | reflections |

Each blurred map reads blurrier mips for samples that stand for larger
areas (filtered importance sampling), so a few dozen samples per texel give
smooth results; reads stop at 4x4 faces so sky and ground do not bleed into
each other. A fourth piece, the **split-sum BRDF table** (how much of the
reflection a surface returns, by viewing angle and roughness), does not
depend on the environment: it is computed once on the CPU
(`render/src/BrdfLut.cpp`) and uploaded as a half-float texture.

```cpp
renderer.setEnvironment(render::ProceduralSky{.sunDirection = sun}, render::EnvironmentQuality{});
// or, from a file, in the background:
assets.registerType<engine::EnvironmentAsset>(engine::environmentLoader(renderer));
assets.load<engine::EnvironmentAsset>("skies/meadow.hdr");
```

A renderer starts with a small procedural sky (`kFastEnvironment`, about
0.2 s to bake in a debug build on the CPU driver), so headless tests stay
fast; the demo bakes a sharper one, and panoramas use `kDetailedEnvironment`
(512-texel cube, 256 samples: about 0.2 s for a 3200x1600 panorama in a
release build on the CPU driver).

Per frame, `FrameData` chooses how it is used: `environmentIntensity`
(0 turns it off), `drawSky` (the sky pass: one triangle on the far plane,
drawn after the meshes so the depth test keeps it behind them, plus the
sun's disk from `FrameData::sun`), and `exposure` / `toneMap` (next
section).

With `--environment`, the scene's sun stays where the game puts it; it does
not follow the sun in the photograph.

## Shadows and HDR post-processing

**HDR target.** Meshes and the sky render into an offscreen
`R16G16B16A16_SFLOAT` image with the depth buffer, so light brighter than 1
(the sun on metal, a lamp, the sky) keeps its value. A composite pass then
draws it onto the swapchain. The HDR image, the bloom chain and the
descriptor sets that point at them are rebuilt with the swapchain on
resize; a test resizes four times and checks the number of live GPU
allocations never changes. Frames with no meshes and no sky (the 2D demo)
skip all of it and draw straight onto the swapchain.

**Sun shadows** (`FrameData::shadows`; `render/Shadow.h`,
`render/src/ShadowMap.cpp`, `render/shaders/shadow.vert`, `pbr.frag`):

- A 2048x2048 `D32_SFLOAT` shadow map is rendered from the sun each frame,
  depth only. Its orthographic box is fitted round a **bounding sphere** of
  the scene (every mesh drawn, or `shadows.bounds`) and its centre is
  **snapped to whole texels**, so shadow edges do not shimmer as things
  move.
- Against **acne** (surfaces shadowing themselves in stripes): hardware
  depth bias (`vkCmdSetDepthBias`, constant and slope; negative, because of
  reverse-Z) and a **normal offset** (the point looked up is moved out
  along the surface normal, by a number of shadow-map texels). Both are
  settings; tests check a lit floor under a low sun has no acne and that a
  box's shadow starts at its base (no **peter-panning**).
- `pbr.frag` samples a `sampler2DShadow` (the hardware compares depths and
  filters the results) at **3x3** points: soft edges. Near the edge of the
  map, and outside its depth range, the shadow **fades out**, so what the
  map does not cover is lit, never black.
- Shadows dim only the sun's light: the sky still lights shadowed
  surfaces. `MeshInstance::castsShadow` keeps a mesh out of the map.

**Bloom** (`FrameData::bloom`; `render/shaders/bloom.frag`): the bright
parts of the HDR image (above `threshold`, with a soft `knee`) go down a
half-size mip chain of up to six levels and back up, then are added to the
scene (`strength`). Down: 13-tap filtered halves (Jimenez's Call of Duty
method); the first step uses the **Karis average** (each group weighted by
1 / (1 + luminance)), so one tiny, extremely bright pixel cannot flicker
the whole glow. Up: 3x3 tent-filtered levels added onto each other.

**Composite and tone mapping** (`FrameData::exposure`, `toneMap`;
`render/shaders/tonemap.frag`): HDR + bloom, times exposure, then `None`
(clip), `Aces` (Stephen Hill's fit of ACES filmic: the film look, used by
the demo) or `PbrNeutral` (base colours stay true). **sRGB is encoded
exactly once**: an `_SRGB` swapchain encodes on write, so the shader writes
linear values; on a `_UNORM` swapchain the shader encodes (a specialization
constant, set per swapchain format; sprites do the same). A test renders
the same frame through both kinds and gets the same pixels.

**Emissive materials** (`Material::emissiveFactor`, `emissive` texture):
light a surface gives off itself; with bloom, bright enough emission
glows past its edges (the demo's lamp bulbs, the helmet's visor).

## Networking and the dedicated server

The `net/` library carries messages between game processes. It depends on
`core` only, and ENet (reliable UDP) is hidden inside `net/src/Host.cpp`:
no ENet or socket header reaches anything that includes `net/`.

**Transport** (`net/Host.h`). A `Host` is a server (`Host::listen(port)`;
port 0 picks a free one) or a client (`Host::client()`, then
`connect(host, port)`). `service(timeoutMs)` runs the network and returns
what happened: `Connected`, `Disconnected`, `Received`. A `Peer` is a
number the host gave a connection (never reused, so a stale one matches
nothing); a `Packet` owns its bytes and names its channel:

| Channel | Delivery | For |
|---|---|---|
| `Reliable` | arrives once, in order; resent until acknowledged | spawns, hits, state changes, chat |
| `Unreliable` | sent once, unsequenced; may be lost | input every tick, position snapshots |

**Bit packing** (`net/BitStream.h`). `BitWriter` appends values of 1 to 64
bits with no gaps (least significant bit first, by shifts and masks: no
struct casts, so the same bytes on every CPU); floats travel as their exact
bits; small numbers as varints; strings with a checked length.
`BitReader` never reads past its data: a short or hostile packet gives
zeros and a sticky `ok() == false`, so a message is decoded and then
checked once.

**Quantization** (`net/Quantize.h`). Floats with a known range as N-bit
steps (error at most half a step; clamped; NaN-safe), 2D/3D vectors, and
rotations as **smallest-three** quaternions: drop the largest component,
send its index and the other three in [-0.71, 0.71]: 32 bits instead of
128, within a quarter of a degree.

**A worse network, on purpose** (`Host::simulate(NetworkConditions)`).
For testing: everything a host sends waits `latencyMs` (plus up to
`jitterMs` more) in a queue before it goes, and `lossPercent` of the
unreliable packets are thrown away. Reliable packets are never lost (ENet
would resend them): a "lost" one arrives a round trip later instead, still
in order. `--net-latency MS` and `--net-loss PERCENT` turn it on for
either end.

**The game** (`game/`). The server is the **authority**: it alone decides
where everything is. Each client controls one player entity.

| Step | What happens |
|---|---|
| Connect | the client says **Hello** with its protocol version. Another version: **Reject** ("protocol version mismatch"), then goodbye. Silent for 3 s (a half-open connection): disconnected |
| Join | the server spawns the client's player and sends **Welcome** (its entity id and exact start) and a **Spawn** list of everything; every other client gets a Spawn of the newcomer. All reliable |
| Play | every 120 Hz tick the client sends its **Input** (unreliable, numbered, with the last 8 repeated, so a lost packet loses nothing). The server applies at most **one input a tick** on average to that player (a small bucket allows a burst of 3 after a hiccup), so sending inputs faster does not make anyone faster |
| Snapshots | 20 a second, one per client: the server tick, `lastAckedInputSeq` (the newest of that client's inputs applied) and exactly where that left its player, then the entities, quantized (about 6 bytes each) |
| Deltas | each Input also says the newest snapshot the client has; the server keeps its last 32 snapshots per client and sends only the differences from that one: ids that are gone, new entities, and changes as offsets in as few bits as they need. Unchanged entities cost nothing (a still world: 557 bytes → 20). If the server no longer has that snapshot, it sends a full one |
| Leave | the player is destroyed and everyone else gets a **Despawn** (reliable) |

**Prediction** (`game/Prediction.h`). The client moves its own player at
once, with the same rule as the server (`game/Simulation.h`) on the same
quantized input, and remembers `{input number, input, result}` in a ring.
When a snapshot says "inputs up to N applied, you are at P", it compares P
with what it predicted for N: if they differ (an input was lost or
capped), the server wins: it starts from P and **replays** every input
after N. Snapshots older than the newest seen are dropped. With a ping so
high the ring (2 s) fills, the oldest entries are overwritten and the
client simply takes the server's word. A correction is shown as a short
glide, not a snap.

**Interpolation** (`game/Interpolation.h`). Everything else is drawn about
0.1 s in the past, blended between the two snapshots either side, so it
moves smoothly even though snapshots arrive only 20 times a second and
some are lost.

**Dedicated server** (`app/Server.cpp`, `--server [--port N]`). The game's
120 Hz loop with no window and no GPU: neither GLFW nor Vulkan is started,
so it runs on a machine with no display. Waiting for the network until the
next tick is due is the loop's sleep. Ctrl+C or SIGTERM
(`core/Interrupt.h`) stops it in order: every client is told goodbye
first.

```bash
./build/debug/app/playground_app --server --port 27015
```

Then, in another terminal, play (WASD or the arrow keys; you are the white
square, other players are orange), here over a pretend bad connection:

```bash
./build/debug/app/playground_app --connect 127.0.0.1:27015 --net-latency 60 --net-loss 5
```

`--headless` with `--connect` runs a bot instead of a window: it walks a
circle for `--frames` ticks and reports, including how far its prediction
ended from the server's answer (it should be 0.000):

```bash
./build/debug/app/playground_app --connect 127.0.0.1:27015 --headless --frames 600
```

## GPU memory

`gpu::Allocator` wraps the Vulkan Memory Allocator (VMA), owned by
`gpu::Context` and destroyed just before the device; it logs an error for any
buffer still alive at that point. `gpu::Buffer` is an RAII buffer of one of
three kinds:

| Kind | Use |
|---|---|
| `HostVisible` | written by the CPU every frame; mapped for its whole life (the sprite buffers) |
| `DeviceLocal` | the GPU's own memory; filled once through a staging copy (`createDeviceLocalBuffer`) |
| `HostReadback` | GPU results read by the CPU (tests, later screenshots) |

Speed, 25,000 sprites, release build, validation off, on the software
renderer in the cloud container (a CPU doing the GPU's work, so pixel filling
dominates): one draw per sprite took about 66 ms a frame; one instanced draw
takes about 38 ms, with the 120 Hz simulation keeping up. The 3D scene
(about 15,500 lit triangles plus the textured floor, 1600x900) took about
30 ms with sun lighting only, about 36 ms with image-based lighting and
the sky (every pixel now draws something), and about 83 ms with the 2048²
shadow map, 3x3 shadow filtering, bloom, the composite pass and the
46,000-triangle helmet, all computed by the CPU. The 2D scene skips the
HDR path and stays at about 36 ms. Not yet measured on a real GPU.

## Worker threads: the job system

`JobSystem` (`core/JobSystem.h`) is a pool of worker threads, by default one
fewer than the CPU has cores (at least one), sharing one queue. Three ways to
use it:

| Call | Use |
|---|---|
| `submit(fn)` | One background task; returns a `std::future` with its result (or its exception). |
| `run(fn, counter)` | Fire-and-forget; `counter.wait()` blocks until all jobs on that counter are done. |
| `parallelFor(count, batch, fn)` | Splits `0..count` into batches; `fn(begin, end)` runs on the workers and on the calling thread, and returns when all are done. The first exception is rethrown. |

Waiting never just sleeps while there is work: `JobCounter::wait()` and
`parallelFor` run queued jobs on the waiting thread, so a job that waits for
other jobs (nested `parallelFor`) cannot deadlock the pool, even with one
worker. Destroying the `JobSystem` finishes every queued job, then joins the
threads.

## Loading files: the asset manager

`engine::AssetManager` (`engine/Assets.h`) loads files without stopping the
game. Each asset type registers a loader with two halves:

- **decode**, on a worker thread: read the file and turn its bytes into
  CPU data (parse it, decompress it). The slow part.
- **finalize**, on the main thread inside `assets.update()`: make the
  finished asset, for example by uploading it to the GPU, since Vulkan
  objects belong to the main thread. `update(budget)` finalizes at most
  `budget` assets per frame (8 by default), so a burst of loads is spread
  over several frames.

`load<T>("path")` returns an `AssetHandle<T>` at once; `state(handle)` is
`Loading`, then `Ready` (`get(handle)` returns it) or `Failed` (`error(handle)`
says why, naming the file). Loading the same path twice returns the same
handle. `unload(handle)` frees an asset; if it was still loading, its result
is thrown away. Handles carry a generation, like ECS entities, so an old
handle never finds a newer asset that reused its slot.

Workers never touch the asset table: they hand results to the main thread
through one locked queue. Every other call is main-thread only, and debug
builds assert it. The renderer test `an asset is read on a worker and
uploaded to GPU memory on the main thread` loads a 64 KiB file into a
device-local buffer this way and reads it back.

The demo loads `app/assets/welcome.txt` at start-up and logs it once it is
ready (`[game    ] asset welcome.txt: Welcome to Aedan's Playground.`).

## Getting it running on Windows

1. **Visual Studio 2022** with the "Desktop development with C++" workload.
2. **The Vulkan SDK** from <https://vulkan.lunarg.com/sdk/home> (Windows
   installer, default options). It provides the Vulkan headers and loader
   library, the validation layer, `glslc` and `vulkaninfo`, and sets the
   `VULKAN_SDK` environment variable that CMake uses to find it. Restart your
   terminal afterwards so the variable is visible.
3. **vcpkg**, if you do not have it:

   ```bash
   git clone https://github.com/microsoft/vcpkg C:/vcpkg
   ```

   ```bash
   C:/vcpkg/bootstrap-vcpkg.bat
   ```

   Then set the environment variable `VCPKG_ROOT` to `C:/vcpkg` and restart
   the terminal.
4. **Check your GPU driver supports Vulkan 1.3:** run `vulkaninfo --summary`
   (it came with the SDK) and look for `apiVersion = 1.3.x` or higher under
   your GPU. If it says 1.2 or lower, update the GPU driver.
5. **Build and run**, from the `aedans-playground` folder in a "Developer
   PowerShell for VS 2022":

   ```bash
   cmake --preset windows
   ```

   ```bash
   cmake --build --preset windows-debug
   ```

   ```bash
   ./build/windows/app/Debug/playground_app.exe
   ```

   The first configure takes a while: vcpkg builds GLFW, spdlog and doctest.
6. **Run the tests:**

   ```bash
   ctest --preset windows-debug
   ```

The Windows presets have not been run yet. If MSVC reports warnings that GCC
and Clang did not, they stop the build (warnings are errors); add
`-DPLAYGROUND_WARNINGS_AS_ERRORS=OFF` to the configure command to get past
them, and report them so they can be fixed.

## Getting it running on Linux

Ubuntu 24.04 package names. The Mesa driver line gives a software Vulkan
device for machines without a GPU (CI, cloud containers); with a real GPU,
its vendor's driver is what gets used.

```bash
sudo apt-get install -y cmake ninja-build g++ libglfw3-dev libglm-dev libspdlog-dev doctest-dev libenet-dev libtinygltf-dev nlohmann-json3-dev libstb-dev libvulkan-dev vulkan-validationlayers vulkan-tools mesa-vulkan-drivers glslc xvfb xdotool
```

```bash
cmake --preset debug
```

```bash
cmake --build --preset debug
```

```bash
./build/debug/app/playground_app
```

```bash
ctest --preset debug
```

`xdotool` is only for `keyboard_e2e`, which sends real keystrokes to the game;
without it that test is skipped.

Without a display, the window and GPU tests are reported as skipped. To run
them headless, wrap the command in a virtual display:

```bash
xvfb-run -a ctest --preset debug
```

Other presets: `release`, `asan` (AddressSanitizer + UBSan), and `tsan`
(ThreadSanitizer, which finds data races; it runs only the tests labelled
`threads`, the job and asset suites):

```bash
cmake --preset tsan
cmake --build --preset tsan
ctest --preset tsan
```

## What a good start looks like

The log goes to the console and to `playground.log` next to where you ran it.
Lines are written to the file as they happen (every line in debug builds,
info and above in release), so `tail -f playground.log` is live.
On this project's test machine (software Vulkan driver), start-up logs:

```
[gpu     ] creating Vulkan instance (validation requested)
[gpu     ] Vulkan loader supports version 1.3.275
[gpu     ] Vulkan instance created: API 1.3.0, validation ON
[gpu     ]   layers: VK_LAYER_KHRONOS_validation
[gpu     ]   extensions: VK_KHR_surface, VK_KHR_xcb_surface, VK_EXT_debug_utils
[gpu     ] window surface created (1600x900 pixels)
[gpu     ] found 1 Vulkan device(s):
[gpu     ]   #0 llvmpipe (LLVM 20.1.2, 256 bits) — CPU (software), Vulkan 1.4.318, driver llvmpipe Mesa 25.2.8
[gpu     ]      suitable, score 60 (graphics queue family 0, present queue family 0)
[gpu     ] selected llvmpipe (LLVM 20.1.2, 256 bits) (CPU (software))
[gpu     ] creating logical device on llvmpipe (LLVM 20.1.2, 256 bits)
[gpu     ] logical device created: extensions VK_KHR_swapchain; features dynamicRendering, ...
[gpu     ] graphics queue ready: family 0, also used for present
[gpu     ] GPU ready: llvmpipe (LLVM 20.1.2, 256 bits) with Vulkan 1.4.318
[gpu     ] swapchain created: 1600x900, 4 images, B8G8R8A8_SRGB, FIFO (vsync)
[render  ] sprite pipeline built for B8G8R8A8_SRGB
[render  ] Vulkan renderer ready: 2 frames in flight
[engine  ] renderer "vulkan" attached
```

followed by `[game    ] 3D scene: 4 object(s) loading; ...`,
`[game    ] asset welcome.txt: Welcome to Aedan's Playground.`, one
`[engine  ] model "..." uploaded` and one `[game    ] ... ready` line per
model, and the window shows the scene in the screenshot above. (With
`--scene sprites`: `[game    ] spawned 25000 sprites and the player; ...`
and the bouncing squares.)
Before the GPU lines, `[core    ] job system: N worker thread(s)` gives the
number of worker threads (one fewer than your CPU's cores).

On a desktop you will see your own GPU, `discrete GPU` or `integrated GPU`,
and on Windows `VK_KHR_win32_surface` instead of `VK_KHR_xcb_surface`. A
laptop with two GPUs lists both; the discrete one wins.

Closing the window shuts down in the reverse order — renderer (pipeline,
swapchain, device, surface, instance), window, GLFW — and ends with
`clean exit`.

## When it does not start

Every failure is one `critical` line naming the step and the reason.

| Message contains | Meaning | Fix |
|---|---|---|
| `GLFW could not initialise` | No display (remote session, container) | Run on a desktop, or under `xvfb-run -a` on Linux |
| `no Vulkan loader or driver was found` | No Vulkan driver installed | Install or update the GPU driver |
| `the Vulkan loader on this machine supports 1.2` | Driver too old | Update the GPU driver |
| `step 3 (choosing a GPU): no GPU meets the engine's requirements` | Every GPU lacks something; the message lists what, per GPU | Update the driver; if it still fails, the GPU is older than the engine's minimum (Vulkan 1.3 with descriptor indexing) |
| `validation was requested but VK_LAYER_KHRONOS_validation is not installed` (warning) | The program still runs, without validation | Install the Vulkan SDK (Windows) or `vulkan-validationlayers` (Linux) |
| Window opens but stays black or dark blue with no squares | The draw is not reaching the screen | Run with `--verbose` and send the log; check for `[gpu] [error]` lines |
| `N Vulkan validation error(s)` at exit, exit code 1 | A real bug: the engine used Vulkan incorrectly | Read the `[gpu] [error]` lines above it; each names the rule broken (`VUID-...`) |

## Options

| Option | Effect |
|---|---|
| `--scene models` / `--scene sprites` | the 3D scene (default) or the 2D one |
| `--model PATH` | show this glTF 2.0 file (`.gltf` or `.glb`) in the 3D scene |
| `--environment PATH` | light the 3D scene with this HDR panorama (`.hdr`) and show it as the sky |
| `--frames N` | exit after N frames |
| `--sprites N` | how many bouncing squares the 2D scene spawns (default 25000; implies `--scene sprites`) |
| `--server` | run a dedicated server: no window, no GPU, the 120 Hz simulation; Ctrl+C stops it. With `--frames N`: stop after N ticks |
| `--port N` | the server's UDP port (default 27015; 0: any free port, logged) |
| `--connect HOST[:PORT]` | play on a server: a window showing its world; WASD moves your player. Exit code 1 if rejected or dropped |
| `--headless` | with `--connect`: no window; a bot walks for `--frames` ticks (default 240 = 2 s), then logs a summary |
| `--net-latency MS` | testing: delay everything this end sends by MS (plus up to MS/5 jitter) |
| `--net-loss PERCENT` | testing: lose this share of the unreliable packets this end sends |
| `--verbose` | debug-level logging: frame-time statistics, Vulkan loader messages |
| `--log-file PATH` | where the log goes (default `playground.log`) |
| `--validation` / `--no-validation` | force Vulkan validation on or off (default: on in debug builds, off in release) |

Exit codes: 0 clean exit (window closed, Ctrl+C, or frame limit), 1 start-up
failure, runtime failure or any Vulkan validation error, 2 bad arguments.
