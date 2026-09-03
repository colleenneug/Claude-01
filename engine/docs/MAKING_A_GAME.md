# Making a game with Forge

Three ways, and you will use all of them.

---

## 1. Place things in the editor

```
cmake -S engine -B engine/build && cmake --build engine/build
./engine/build/forge-editor
```

| | |
|---|---|
| Right-drag | Look |
| `W` `A` `S` `D` | Fly (while looking) |
| `Q` / `E` | Down / up |
| Middle-drag | Pan |
| Wheel | Dolly, or fly speed while looking |
| Left click | Select |
| `W` `R` `T` | Move / rotate / scale |
| `F` | Frame the selection |
| `Ctrl+D` | Duplicate |
| `Delete` | Delete |
| `Ctrl+S` | Save |
| `F5` | Play / stop |
| `Esc` | Stop playing, or clear the selection |

Pick something from **Place Actors**, drag its handles, and edit it in
**Details**. Everything in Details is reflected from the class, so anything
the engine knows about is editable there.

Without a display the editor runs headless, which is how the screenshots are
made and how it is tested:

```
./engine/build/forge-editor --headless --frames 30 --shot editor.png
./engine/build/forge-editor --list-classes
```

---

## 2. Wire up behaviour in a script graph

A script is an asset. Attach a `ScriptComponent` to an actor, point it at a
script, and the graph becomes that actor's behaviour — no C++ subclass.

Built by hand, a graph is nodes and links:

```cpp
auto g = std::make_unique<ScriptGraph>();
g->name = "OpenOnTouch";

const int ev    = g->addNode("Event.BeginOverlap", {0, 0});
const int print = g->addNode("Debug.Print",        {220, 0});
g->node(print)->literals["Text"] = ScriptValue::make(std::string("Touched"));
g->link(ev, "Then", print, "In");

assets.addScript(std::move(g));
```

The node library covers events, flow control (`Branch`, `Sequence`, `Delay`,
`Gate`, `DoOnce`, `ForLoop`), variables, maths, vectors, actor operations,
world queries including line traces, and printing. `Actor.SetProperty` reaches
**any reflected property by name**, so there is no node-per-property to write.

`graph->validate()` reports cycles among pure nodes and links to pins that no
longer exist — worth calling after loading a graph written by an older build.

---

## 3. Write a class

When behaviour carries state the rules care about, a class is clearer than a
graph. This is the whole of a collectible:

```cpp
class Orb : public Actor {
    FORGE_OBJECT(Orb, Actor)
public:
    Orb() {
        auto* mesh = addComponent<StaticMeshComponent>("Mesh");
        mesh->mesh = "Sphere";
        mesh->material = "GlowCyan";
        mesh->collisionResponse = (int)CollisionResponse::Overlap;
        mesh->mobility = (int)Mobility::Movable;
        setRoot(mesh);
        addTag("orb");
    }

    void beginPlay() override {
        onBeginOverlap.bind([this](Actor* other) {
            if (!other || !other->getClass()->isA("Character")) return;
            destroy();
        });
    }

    float value = 1.0f;
};

FORGE_CLASS_BEGIN(Orb)
    FORGE_DISPLAY("Orb")
    FORGE_CATEGORY("Skyforge")
    FORGE_PROP(value).range(0.0f, 10.0f).cat("Orb")
FORGE_CLASS_END()
FORGE_REGISTER(Orb)
```

That is all it takes to appear in **Place Actors**, be editable in
**Details**, and be saved into a level.

---

## Things worth knowing early

**Scale is relative to the mesh, not to a metre.** The starter `Cube` is one
unit; `Stairs` is 2 x 2 x 4. Scaling `Stairs` by `{4, 4.8, 8}` gives a
32-metre slab, not an 8-metre staircase. (This is written down because it
happened while building the sample and quietly swallowed half the course.)

**Anything that moves must be `Movable`.** A static collider goes into the
broadphase grid once; if the actor then moves, its collision stays where the
level file left it and the player walks on thin air.

**Give characters a collision capsule.** A `CharacterMovementComponent` sweeps
its own shape as a *query*, which is not a collider. `Character` adds a
`CapsuleComponent` for exactly this reason: without one, triggers never fire.

**A GameMode is where the rules go.** It chooses the pawn, picks a
`PlayerStart`, and is the natural home for scoring, win conditions and
respawning. Set it in World Settings.

---

## Shipping

A level is JSON. `forge-player` runs one with no editor around it:

```
./engine/build/forge-sample --write skyforge.flevel
./engine/build/forge-player skyforge.flevel
./engine/build/forge-player skyforge.flevel --headless --frames 600 --shot frame.png
```

---

## The sample

`samples/skyforge` is a complete game: a course of floating platforms with
orbs to collect, a goal that opens once you have them all, a moving platform,
a rotating hazard, a script-driven beacon, and respawning when you fall.

Its gameplay classes are a library of their own so the game can be both played
and **driven by a test** — `tests/TestGame.cpp` walks the pawn onto every orb,
pushes it off the edge to check it respawns, and finishes the course. An
engine is only an engine if you can make something with it, and that test is
the evidence.
