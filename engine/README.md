# erebus_engine — architecture blueprint

A C++20 framework for a Destiny-class first-person shooter: clustered forward
PBR, a decoupled fixed-timestep simulation, momentum-based movement, a gunplay
model that separates recoil from spread, and a data-oriented entity store.

**This is a separate tree from `../cpp/`, which is the game that currently
works.** Nothing here is wired into it. See *Relationship to the working build*
at the bottom, which is the part worth reading first if the actual goal is
better-looking output rather than a new engine.

---

## Status, stated honestly

| Area | State |
|---|---|
| Fixed/variable timestep loop | **Working**, tested |
| ECS (sparse set, generational handles) | **Working**, tested |
| Player movement controller | **Working**, tested |
| Weapon framework (hitscan, bloom, recoil) | **Working**, tested |
| Clustered light binning (CPU reference) | **Working**, tested |
| PBR vertex/fragment shaders | **Written**, not yet run — no device to run them on |
| Cluster assignment compute shader | **Written**, not yet run |
| Volumetric injection | **Stub with a full implementation plan** |
| Frame graph | **Interface only** |
| Vulkan / D3D12 backends | **Scaffold**: bring-up checklist, no device |

`tests/smoke.cpp` exercises everything in the first group and passes 32/32.
Build and run it:

```
cmake -S engine -B engine/build
cmake --build engine/build -j
./engine/build/erebus_engine_smoke
```

The GPU backend is deliberately a scaffold rather than plausible-looking
boilerplate. A Vulkan device that actually presents is ~2,500 lines before it
does anything useful, and untested boilerplate that *looks* finished is worse
than an explicit checklist — it costs a week to discover it never worked.
`src/render/vulkan/VulkanDevice.cpp` is that checklist, in bring-up order,
with the decisions and the traps.

---

## 1. Rendering pipeline

### Cook-Torrance PBR — `shaders/pbr.frag`

```
f = f_diffuse + f_specular
f_diffuse  = (1 - F)(1 - metallic) · albedo / π
f_specular = D · G · F / (4 · NdotL · NdotV)
```

- **D** — GGX / Trowbridge-Reitz. The long tail is what separates it from
  Blinn-Phong and most of what makes metal read as metal.
- **G** — Smith *height-correlated* (Heitz 2014), written in visibility form
  `V = G / (4·NdotL·NdotV)` so the denominator folds in and a division
  disappears. The separable Smith form is the usual shortcut and loses
  noticeable energy at high roughness.
- **F** — Schlick. `F0 = mix(0.04, albedo, metallic)`: dielectrics reflect ~4%
  at normal incidence, metals reflect their albedo and have no diffuse lobe.
  That one lerp is the whole of what "metallic workflow" means.

Three things that separate PBR from "PBR":

1. **The `(1 - F)` on the diffuse term.** Without it, grazing angles gain
   energy and everything looks waxy.
2. **Roughness clamped away from zero** (`0.045`). A perfectly smooth surface
   makes the GGX denominator explode into specular aliasing no TAA will hide.
3. **Linear space, always.** sRGB decode on sample (use `_SRGB` formats so the
   hardware does it free), tonemap *once* at composite. Every "why does my PBR
   look washed out" is one of these three.

IBL uses the split-sum approximation: an irradiance cube for diffuse, a
prefiltered cube with roughness in its mip chain for specular, and a 2D DFG
LUT. Fresnel for the IBL term is the roughness-aware variant — plain Schlick
with `F90 = 1` makes rough metal rim-light like a mirror.

### Clustered forward — `shaders/cluster_assign.comp`, `src/render/ClusteredLighting.cpp`

Divide the frustum into froxels (16 × 9 × 24 to start), bin every light into
the froxels it touches in one compute pass, and have the pixel shader iterate
only its own froxel's list. Cost goes from O(pixels × lights) to
O(pixels × lights-per-froxel), typically 2–8.

**Why not deferred**, given hundreds of lights is the classic deferred pitch:

- MSAA works, because shading happens at forward rasterisation.
- Transparents use the same light list as opaques. In deferred they need a
  whole second lighting path — and emissive particles, the thing that makes a
  firefight read, are transparent.
- Per-material BRDF variation (cloth, skin, clearcoat) is free; a G-buffer has
  to encode every parameter any material might want.

The cost is a depth prepass to avoid shading overdraw, which you want anyway.

**Z is divided exponentially**, not uniformly:

```
slice(z) = floor( log2(z) · scale + bias )
scale = numSlices / log2(far/near)
bias  = -numSlices · log2(near) / log2(far/near)
```

Uniform slices put nearly every froxel in the far distance where there is
nothing, and lump the whole near field — muzzle flash, shields, grenades —
into one. The CPU (`ClusterGrid::sliceForDepth`) and the shader
(`clusterIndexForFragment`) must use the *identical* formula; if they drift,
lights pop at froxel boundaries and it is miserable to debug on the GPU. The
CPU reference binner exists precisely so that can be unit-tested.

Attenuation is inverse-square with a smooth window that forces the
contribution to exactly zero at the culling radius. Raw `1/d²` never reaches
zero, so influence extends past the radius the binner used — and pops.

### Volumetrics — `shaders/volumetric_scatter.comp`

Froxel-based (Wronski 2014), reusing the same grid the light clustering built,
so the per-froxel light list is already computed. Three passes: inject
(density + in-scattering per froxel), integrate (march front-to-back
accumulating transmittance), apply (sample at pixel depth, blend).

A per-pixel raymarch at 1080p × 64 steps is 133M samples. The froxel volume at
160×90×64 is 921K, temporally reprojected and jittered. An order of magnitude
cheaper, and transparents and particles get the same volumetric response free
because they sample the same volume.

Use Henyey-Greenstein with `g ≈ 0.7`. Isotropic scattering gives uniform haze
and no shafts at all — the forward lobe *is* the effect.

### Global illumination

Slots between shadows and opaque; stubbed in `FrameGraph.h`.

- **DDGI** (irradiance probe volumes) is the target: a grid of probes, each a
  small octahedral irradiance map, a few rays per probe per frame, temporally
  accumulated. Fully dynamic. Needs ray tracing, or a voxel/screen-space
  fallback for the rays.
- **Screen-space GI** as a floor. Cheap, no build step, and wrong in the way
  screen space is always wrong — it cannot light from what is off screen.
  Acceptable as a fallback, not as the plan.

### The frame, in order

```
depth prepass        → depth            (Hi-Z; kills shading overdraw)
cluster assignment   → light grid       (compute)
shadow cascades      → cascade atlas
[GI probe update]    → probe irradiance
opaque forward       → HDR, motion
volumetric inject    → scattering volume
volumetric integrate → integrated volume
transparents         → HDR              (same light grid)
TAA resolve          → history
bloom down/upsample  → bloom chain
composite + tonemap  → backbuffer       (ACES, then sRGB encode)
```

Motion vectors are written from the opaque pass from day one — TAA and motion
blur both need them, and retrofitting means touching every vertex shader and
every pipeline layout in the project.

---

## 2. Simulation

### Frame pacing — `core/GameLoop.h`

```
accumulate real elapsed time
while (accumulator >= FIXED_DT) { simulate(FIXED_DT); accumulator -= FIXED_DT; }
render(alpha = accumulator / FIXED_DT)
```

`FIXED_DT = 1/120`. Chosen over 1/60 because character controllers that sweep
against geometry lose contact precision as dt grows: at 60 Hz a player
sprinting at 9.4 m/s moves 15.7 cm per step, enough to clip the lip of a 15 cm
stair.

`alpha` is the fraction of a step the renderer is *ahead* of the simulation.
`Transform` stores both current and previous position so render systems can
interpolate by it — without that, a 60 Hz simulation displayed at 144 Hz
judders, because three of every five frames show a state up to 16 ms stale.

**Spiral of death.** If a step costs more than `FIXED_DT`, the accumulator
grows faster than it drains. `maxStepsPerFrame` caps catch-up and the surplus
is *discarded*. Discarding is correct: letting the accumulator grow means that
the moment a hitch ends you simulate a hundred steps at once and everything
teleports.

This is the single-threaded reference shape. The production arrangement runs
`simulate()` for frame N on the simulation thread while the render thread
records frame N−1 from a double-buffered snapshot. The interface is already
compatible: nothing in `simulate()` may touch GPU resources, nothing in
`render()` may mutate simulation state.

### Movement — `game/PlayerController.h`

"Floaty yet snappy" is not one parameter. It is two settings pulling opposite
ways:

- **Snappy** — ground acceleration an order of magnitude above gravity, high
  ground friction. Top speed in ~100 ms, stopped in ~100 ms.
- **Floaty** — airborne, acceleration collapses to ~10% and friction goes to
  zero. Horizontal velocity is *preserved*, not driven. What you entered a
  jump with is most of what you leave it with.

Mushy games get the first half wrong. Twitchy games get the second half wrong,
and then the air is just the ground with a different mesh.

The accelerator is Quake's, still right thirty years on:

```
addSpeed   = wishSpeed - dot(velocity, wishDir)
accelSpeed = min(accel · wishSpeed · dt, addSpeed)
velocity  += wishDir · accelSpeed
```

The property that matters: acceleration is clamped by the deficit **along the
wish direction**, not by total speed. Velocity perpendicular to the input is
never reduced by acceleration — only friction removes it. That asymmetry is
the entire reason air-strafing and slide-hopping are expressive rather than
noise, and replacing it with `velocity = wishDir * speed` kills the movement
dead.

Air control is damped by current speed: near walk speed you get close to full
strafe authority, well above it the authority falls away. A fast jump commits,
a slow one steers. No single constant serves both.

Two grace windows, because a player's sense of "I pressed jump at the edge" is
generous and the simulation's is not: **coyote time** (120 ms after leaving
ground) and **jump buffering** (140 ms before landing). Both must be open, and
both are consumed, or one press jumps twice.

Friction has a `stopSpeed` floor. Pure exponential decay never reaches zero;
below the floor it drains at a constant rate instead, so the character
actually stops.

### Gunplay — `game/WeaponSystem.h`

Three things that are routinely conflated and must not be:

1. **Recoil** — a deterministic transform on the *camera*. A pattern. The
   player learns and counters it, so it must be identical for a given shot
   index or there is nothing to learn.
2. **Bloom** — a random cone on the *shot*. Uncounterable; the only response
   is to stop firing. This is what makes sustained fire worse than tapping.
3. **Sway** — low-frequency drift, cosmetic at hip fire, meaningful scoped.

Too much (2) and too little (1) feels unfair: shots go where the reticle did
not predict and skill does not change it. Too little of both feels weightless.

Recoil is a **critically damped spring** (`ζ = 1`: fastest return without
overshoot; overshoot reads as the gun fighting the player), integrated
semi-implicitly — at `k = 260`, `dt = 1/120`, explicit Euler is on the edge of
instability and a stiffer weapon pushes it over.

The spring pulls toward **debt**, not zero. Debt is the fraction of each kick
never given back (`1 - recoveryFraction`), so the sight line climbs over a
burst and stays climbed until the player pulls it down. A recoil that returns
exactly to where it started is a gun with no pattern to learn.

Bloom decays only after a **settle delay**. Without it, a 540 rpm weapon
(111 ms between shots) decays meaningfully between every pair of shots and
sustained fire never blooms at all.

The cone samples radius as `sqrt(u)`, not `u`. Uniform radius bunches pellets
at the centre because area grows as r²; `sqrt` gives a uniform areal
distribution, which is what a shotgun pattern actually looks like.

`reticleRadiusPixels()` derives the reticle from the same bloom value the
shots use. A fixed reticle sprite while the cone grows is the single most
common cause of "my shots don't go where I aim".

Randomness is a **hash of the shot index**, not a PRNG. A PRNG carries state,
and state is what makes a replay or a server reconciliation diverge.

**Projectiles** are entities integrated in the fixed step and need continuous
collision: a 90 m/s rocket moves 75 cm per 120 Hz step, more than thin cover
is thick. Sweep, never point-test.

---

## 3. Memory and data

### Sparse-set ECS — `ecs/`

```
sparse[entityIndex] → denseIndex
dense [denseIndex]  → entity
data  [denseIndex]  → T          (packed, no holes)
```

A system is a straight walk over `data`: contiguous, prefetcher-friendly, no
indirection, no branch for absent components. An array of `GameObject*` with
virtual `Update()` spends most of its time on cache misses and indirect branch
mispredictions, and no micro-optimisation inside `Update()` recovers it.

**Generational handles.** Index-only handles have an invisible use-after-free:
entity 41 dies, slot 41 is recycled, and a stale handle now addresses a live
unrelated object. No crash — a projectile homes onto a door. The generation
counter makes the stale handle compare unequal. `tests/smoke.cpp` asserts
exactly this.

**Removal is swap-and-pop**, so no system may hold an index across a
structural change. `create()`/`destroy()` are therefore *deferred* and applied
by `World::flush()` at a sync point once per step. That is also what makes
eventual job-system parallelisation tractable: systems only ever read and
write component data during a step, never topology.

**Sparse set, not archetypes.** The trade: sparse set gives O(1) add/remove
and cheap random access but a multi-component query walks the smallest pool
and probes the others; archetypes give perfectly packed multi-component
iteration but structural changes move whole rows between chunks. For a shooter
— most churn is projectile spawn/despawn, most queries are one or two
components wide — sparse set wins on simplicity and loses nothing measurable.
Revisit if you grow four-pool joins in a hot path.

**Alignment is a contract, not a style choice.** `Vec3` is 16 bytes, not 12:
an array of 12-byte vectors puts three quarters of its elements on unaligned
addresses. Components must be trivially copyable — `ComponentStorage`
`static_assert`s it, because swap-and-pop relocates by assignment and a
non-trivial move turns an O(n) system into a profiler mystery.

**Smart pointers where they belong and nowhere else.** `unique_ptr` owns
component pools and RHI backends — things with one owner and a virtual
destructor. Component data is raw, packed and owned by its pool. A
`shared_ptr<Transform>` per entity would be a refcount and a cache miss per
access, which is the entire problem this design exists to avoid.

---

## Relationship to the working build

`../cpp/` is a complete, playable game. It already has: Cook-Torrance PBR
(`cpp/shaders/pbr.frag`), cascaded shadow maps, HDR with ACES tonemapping,
bloom, depth of field, analytic volumetric fog, an IBL probe, a procedural
sky, and a full mission/campaign/hub loop. It is OpenGL 4.1 and forward-shaded
with a single directional light plus emissive geometry.

So if the goal is **"make the graphics better"**, this blueprint is not the
shortest path, and it would be dishonest to imply otherwise. A Vulkan + ECS
rewrite is a different project that starts from zero gameplay. Ranked by
visual gain per unit of risk, the things to do to the build that *works* are:

1. **Real point lights.** The largest single gain. Today every light in the
   game is emissive geometry that illuminates nothing — the Cradle's strips,
   the transport's engines, every muzzle flash. Clustered forward (the binning
   code in this tree ports directly to GL 4.3 compute, or to a CPU-side
   uniform array at low light counts) turns all of them into actual sources.
2. **Screen-space reflections**, or at minimum a box-projected probe per room.
   The metal in the station currently reflects one static cube.
3. **Contact shadows** (a short screen-space ray). Cheap, and it fixes the
   detached look where small geometry meets the floor — the thing cascades at
   station scale cannot resolve.
4. **Froxel volumetrics** replacing the analytic fog, per §1 above. This is
   what would put god rays through the Cradle's cupola.
5. **TAA**, which needs the motion vectors the shader here already writes.

Items 1, 4 and 5 are exactly the systems designed in this tree, which is why
the design work is not wasted either way — but they can land in the OpenGL
renderer incrementally, against a game you can still play after each one.
