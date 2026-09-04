# Architecture

Forge is laid out the way Unreal is, because that layout has held up: a
**World** of **Actors**, an actor as a tree of **Components**, every class
describing its own properties through **reflection**, and an **editor** that
reads that reflection rather than knowing about any class in particular.

```
                     ClassRegistry  (reflection)
                            |
        +-------------------+-------------------+
        |                   |                   |
   LevelSerializer      Details panel      Script graph
   (writes only what    (widget per        (Get/Set nodes
    differs from the     property type)     for properties)
    class default)
```

Every arrow in that diagram exists because of one declaration per property.
Adding `FORGE_PROP(radius).range(0.5f, 200.0f)` to a class makes it editable,
saved, scriptable and undoable, with no other edit anywhere.

---

## Modules

| Directory | What lives there |
|---|---|
| `math/` | Vectors, quaternions, rotators, matrices, transforms, bounds, intersection tests, noise |
| `core/` | Reflection, JSON, logging, delegates |
| `scene/` | Actor, Component, World, level serialisation |
| `physics/` | Colliders, broadphase, raycasts, the capsule sweep |
| `components/` | Meshes, lights, cameras, spring arms, movement, scripts, collision capsules |
| `gameplay/` | GameMode, PlayerController, Pawn, Character, input mapping |
| `assets/` | Meshes, materials, textures, the content library |
| `script/` | The visual scripting graph, its runtime and its node library |
| `render/` | The software rasteriser, scene collection, framebuffers, windowing |
| `ui/` | Immediate-mode widgets, used only by the editor |
| `editor/` | The editor application and its panels |

Dependencies run down that list. `math` knows about nothing; `editor` knows
about everything. Nothing lower ever includes something higher.

---

## Conventions

Fixed once, because most engine bugs are two files disagreeing about them.

- Right-handed, **Y up**, **-Z forward**. A camera looks down its own -Z.
- `Rotator` is degrees, applied **yaw, then pitch, then roll**.
- Matrices are **column-major** and multiply column vectors: `M * v`.
- Angles in any public API are degrees. Radians never leave a file.
- Colour is **linear** everywhere. sRGB conversion happens once, on the way
  to a display or an image file.

---

## Reflection

A class declares itself:

```cpp
class PointLightComponent : public LightComponent {
    FORGE_OBJECT(PointLightComponent, LightComponent)
public:
    float radius = 12.0f;
};

// in the .cpp
FORGE_CLASS_BEGIN(PointLightComponent)
    FORGE_DISPLAY("Point Light")
    FORGE_PROP(radius).range(0.5f, 200.0f).cat("Light")
        .tooltip("Beyond this the light contributes nothing.")
FORGE_CLASS_END()
FORGE_REGISTER(PointLightComponent)
```

Properties bind through **pointer-to-member**, so they are type-checked at
compile time and never depend on `offsetof` against a polymorphic layout.

Defaults come from a **class default object** built lazily on first use — not
from the declaring class. This matters: `Character` narrows `size` in its
constructor, and serialisation has to compare against the narrowed value or
every instance writes it out again. Building it lazily rather than at
registration keeps constructors (which add components) out of static
initialisation.

Serialisation writes **only what differs** from that default. A level file
stays readable, and changing a class default updates every level already
saved.

---

## The world

```
World
 +- actors            owned, destroyed at a frame boundary
 +- PhysicsScene      colliders, broadphase, queries
 +- WorldSettings     sky, fog, sun, gravity, which GameMode runs
 +- timers
```

An actor's lifecycle:

| Hook | When |
|---|---|
| `onConstruct` | Whenever a property changes — in the editor too. The construction script. |
| `beginPlay` | Once, when the level starts |
| `tick` | Every frame while playing |
| `endPlay` | On destroy, or when the level ends |

`onConstruct` running in the editor is why dragging a slider rebuilds the
actor live rather than only at play time.

Destruction is deferred to the end of the frame while playing, so a script may
safely destroy something it is iterating over. Outside of play it is
immediate, because the outliner should update on the same frame as the delete.

**World transforms are lazy.** A component marks itself and its subtree dirty
when its local transform changes; the world matrix is rebuilt only when
something asks. Moving a root with fifty children costs one flag write.

---

## Physics

Hand-rolled, because the constraint is one build with no package manager, and
an engine that cannot tell you what you are standing on is not an engine.

Shapes are **oriented boxes, spheres and capsules**. Every test happens in the
box's own space: transform the query by the inverse rotation and the box is
axis-aligned again, so an oriented box costs no more than an axis-aligned one.
Scale is baked into the extents when a collider refreshes, keeping the stored
rotation rigid so distances survive it.

- **Broadphase** is a uniform grid on the ground plane. Levels are mostly
  horizontal, so a 2D grid gives nearly all the win of a 3D one. Static
  colliders live in the grid; movers are scanned linearly, so the grid never
  has to be rebuilt mid-frame.
- **The capsule sweep** substeps so no step exceeds half a radius (a fast body
  must not tunnel through a thin wall), depenetrates against everything it
  touches, slides, and steps up over ledges under `stepHeight`. It reports
  whether it is grounded, which is what an honest jump needs.
- **Overlap events** are diffed against the previous frame, so Begin and End
  each fire exactly once.

A character's movement component sweeps its own shape as a *query*. That is
not a collider, so a `CapsuleComponent` gives the character an actual presence
— without it, nothing in the world can detect the player.

---

## Rendering

The renderer sits behind a `RenderScene`: the world is walked once into a flat
list of draw items and lights, and the renderer never touches the actor graph.
That is what lets the same frame be rendered from the sun's point of view for
the shadow map.

The implementation is a **software rasteriser**. This is not a fallback for
its own sake — it means the engine builds and renders identically on a
workstation, a build machine and a test run, and it means the renderer can be
checked by asserting on pixels instead of by opening a window.

```
shadow pass   depth only, from the sun, orthographic, fitted around the
              camera rather than the level
main pass     transform -> near-plane clip -> rasterise with a depth buffer
              and perspective-correct interpolation
shading       Lambert diffuse + GGX specular, sun plus point and spot lights
              with a windowed inverse-square falloff, hemisphere ambient,
              and an ambient specular term so metals are not black
resolve       fog, exposure, ACES tone mapping, vignette
```

Shadows use **normal-offset sampling**. A depth bias alone cannot fix acne at
a glancing angle: within one shadow texel the true depth varies by more than
any bias small enough to keep contact shadows.

`Window` is the only thing that knows about a display. The OpenGL backend
uploads each finished frame and blits it; the headless one writes PNGs.

---

## Visual scripting

A script is a graph. Event nodes start execution, white exec wires say what
happens next, coloured data wires carry values.

- **Impure** nodes have exec pins, run when execution reaches them, and name
  which exec output to follow.
- **Pure** nodes have no exec pins and are evaluated on demand when something
  downstream reads them.

Data is **pulled, not pushed**: reading an input walks up the wire and
evaluates whatever pure nodes it finds, so nothing is computed that nothing
asked for.

Latent nodes (`Delay`) resume through a world timer that resolves the owning
actor **by id**, not by holding a pointer — so a script whose actor died while
it waited does not resume into freed memory.

---

## The editor

Play-In-Editor keeps **two worlds**. Pressing Play snapshots the edited world
to JSON and builds a second one from it; Stop throws that away. The edited
world is never touched, so playing cannot disturb the level — and because the
snapshot round-trips through the same format a saved level uses, every play
session exercises the serialiser.

The details panel contains **no per-class code**. It walks the selected
object's reflected properties, groups them by their declared category, and
picks a widget from each property's type.
