# Forge Engine

A game engine in the Unreal shape, written in C++17.

A **World** holds **Actors**; an actor is a tree of **Components**; every class
describes its own properties through **reflection**; and the **editor** reads
that reflection rather than knowing about any class in particular. Gameplay is
a **GameMode**, a **PlayerController** and a **Pawn**. Logic you would rather
not write in C++ goes in a **visual script graph**.

Nothing here is bound to a GPU vendor or a windowing toolkit. The renderer is
a software rasteriser that needs nothing at all, with an OpenGL backend for
presentation when a display exists — so the engine builds, runs and is tested
identically on a workstation and on a headless build machine.

---

## Build

```
cmake -S engine -B engine/build -DCMAKE_BUILD_TYPE=Release
cmake --build engine/build
ctest --test-dir engine/build
```

The only optional dependencies are OpenGL and GLFW, used for opening a window.
Without them everything still builds and renders — to files instead of a
screen.

```
./engine/build/forge-editor                     # the editor
./engine/build/forge-sample                     # the sample game
./engine/build/forge-player level.flevel        # play a level standalone

./engine/build/forge-editor --headless --frames 30 --shot editor.png
./engine/build/forge-sample --write skyforge.flevel
```

---

## What is here

- **Reflection** driving the details panel, serialisation and the script graph
  from one declaration per property, with per-class defaults so a level stores
  only what differs
- **Physics**: oriented boxes, spheres and capsules; a grid broadphase;
  raycasts and overlap queries; a capsule sweep that slides, climbs stairs and
  reports what it is standing on
- **Rendering**: shadow-mapped, physically based shading with fog, ACES tone
  mapping, frustum culling and screen-to-world picking
- **Gameplay**: GameMode, PlayerController, Pawn, Character, character and
  projectile movement, spring-arm cameras, trigger volumes, action and axis
  input mapping
- **Visual scripting**: an event-driven node graph with pull-based data
  evaluation and latent nodes
- **The editor**: viewport with fly camera and transform gizmos, world
  outliner, reflection-driven details panel, content browser, output log, and
  non-destructive Play-In-Editor
- **Content** generated rather than loaded — meshes from shape parameters,
  textures from a kind and its colours — so a project carries no binary assets

Levels, assets and scripts are all JSON.

---

## Documentation

- [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) — how it fits together and why
- [`docs/MAKING_A_GAME.md`](docs/MAKING_A_GAME.md) — placing, scripting and
  writing classes, and the traps worth knowing early
- [`samples/skyforge`](samples/skyforge) — a complete game, played by
  `tests/TestGame.cpp`

---

## Tests

Six suites. They do not merely construct objects: characters fall, land, jump,
are stopped by walls and climb stairs; overlap events are asserted to fire
exactly once; levels round-trip through text; scripts branch, tick, wait and
react; frames are rendered and their **pixels** checked, including that
shadows darken what is behind a caster and that a gold sphere is not black;
and the sample game is played to completion.

```
ctest --test-dir engine/build --output-on-failure
```
