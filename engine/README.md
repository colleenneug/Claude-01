# Forge Engine

A game engine in the Unreal shape, written in C++17.

Placement is the same idea Unreal built on: a **World** holds **Actors**, an
actor is a tree of **Components**, every class describes its own properties
through **reflection**, and an **editor** reads that reflection to draw its
panels. Gameplay is a **GameMode**, a **PlayerController** and a **Pawn**.
Logic you would rather not write in C++ goes in a **visual script graph**.

Nothing here is bound to a GPU vendor or a windowing toolkit: the renderer sits
behind an RHI with a software rasteriser that needs nothing at all, and an
OpenGL 3.3 backend for when you have a card.

See `docs/` for the architecture, and `samples/skyforge` for a complete game
built with the engine.
