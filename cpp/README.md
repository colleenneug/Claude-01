# Erebus Cradle — native renderer

A standalone C++/OpenGL implementation of the cinematic, physically based
rendering pipeline requested for this project: PBR armour with anisotropic
reflections, layered vertex-blended terrain, three cascaded shadow maps off
a low sun, volumetric fog, camera-relative dust motes, image-based lighting,
ACES filmic tone mapping, bloom, and depth of field on aim.

**Scope, honestly stated:** this is the rendering pipeline and a small demo
scene with a free-fly FPS camera — not a port of the browser game's
missions, inventory, AI, or netcode. Porting *that* (`src/js/fps/*.js`,
~8,000 lines of gameplay code) is a separate, much larger project. What's
here is real, compiled, and screenshotted working code for exactly the eight
rendering rules that were asked for. See `../docs/NATIVE_RENDERER.md` for
how each one maps to the code.

## Building

Dependencies (Ubuntu/Debian package names):

```
sudo apt install cmake g++ pkg-config libglfw3-dev libglew-dev libglm-dev libgl1-mesa-dev
```

macOS (Homebrew): `brew install cmake glfw glew glm` — CMake will find
Apple's OpenGL framework automatically. Windows: install the same four
libraries via vcpkg (`vcpkg install glfw3 glew glm`) and point CMake at the
vcpkg toolchain file.

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/erebus_native
```

No texture, model, or asset files ship with this project — every material
shades procedurally from world position and normal (see
`shaders/pbr.frag`), the same "nothing to download, nothing to license"
approach the browser build takes with its canvas-baked textures.

## Controls

| Input | Action |
|---|---|
| Mouse | Look |
| WASD | Move |
| Left Shift | Sprint |
| Space / Left Ctrl | Up / down |
| Right mouse, held | Aim — pulls the FOV in and brings depth of field in on the background |
| Left click | Re-capture the mouse after Escape |
| Escape | Release the mouse |

The window title shows frame time, shadow draw calls, and the current aim
blend, refreshed twice a second.

## Verifying it without a display

Set `EREBUS_DUMP_FRAME=<path.ppm>` and `EREBUS_MAX_FRAMES=<n>` to render `n`
frames off-screen and dump the last one as a PPM, then exit — no window
manager or GPU required. This is how the renderer was actually tested while
building it (Xvfb + Mesa's llvmpipe software rasterizer, no physical GPU in
that environment):

```
Xvfb :99 -screen 0 1280x800x24 &
DISPLAY=:99 LIBGL_ALWAYS_SOFTWARE=1 EREBUS_DUMP_FRAME=/tmp/out.ppm EREBUS_MAX_FRAMES=10 ./build/erebus_native
```

## Project layout

```
CMakeLists.txt
src/
  main.cpp               window, input, frame loop
  Gl.h                    GLEW/GLFW/GLM include point + glCheck()
  Shader.{h,cpp}          program compile/link, cached uniform locations
  Camera.{h,cpp}          free-fly FPS camera
  Mesh.{h,cpp}            box/cylinder/sphere/terrain-plane generators
  Framebuffer.{h,cpp}     2D render target (HDR + optional depth texture)
  CascadedShadowMap.{h,cpp}  3 cascades, refit to the view frustum per frame
  Bloom.{h,cpp}           5-level downsample/tent-upsample bloom
  IBL.{h,cpp}             room capture -> prefiltered cubemap
  Scene.{h,cpp}           the demo world + a procedurally posed sentinel rig
  Renderer.{h,cpp}        orchestrates one frame, shadow pass through composite
shaders/
  pbr.vert / pbr.frag     the one material program every opaque object uses
  depth.vert / depth.frag cascade depth-only pass
  motes.vert / motes.frag camera-relative dust
  ibl_capture.*           the light-room -> cubemap capture pass
  fullscreen.vert         shared "big triangle" vertex stage for every post pass
  bright / downsample / upsample .frag   the bloom chain
  dof.frag, composite.frag
```
