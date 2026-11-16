# Aedan's Playground

A C++20 / Vulkan engine being built to host Erebus Cradle. The plan is in
[`ROADMAP.md`](ROADMAP.md); where the last session stopped is in
[`HANDOFF.md`](HANDOFF.md).

**Current state:** the application shell. A window opens, the main loop runs
with a `std::chrono` frame clock, start-up and shutdown are logged, and there
is a `Renderer` slot for the Vulkan renderer (roadmap Milestone 1). Nothing is
drawn yet.

## Layout

| Folder | Library | Contents |
|---|---|---|
| `core/` | `playground_core` | `Result`, `Log`, `Assert`, `FrameClock` |
| `platform/` | `playground_platform` | `Platform` (GLFW lifetime, Ctrl+C), `Window` |
| `engine/` | `playground_engine` | `Application` (main loop), `Renderer` (interface) |
| `app/` | `playground_app` | `main.cpp` |
| `tests/` | | doctest unit tests and application tests |
| `third_party/` | | vendored `tl::expected` |

Each layer may include only the layers below it.

## Building on Linux

Install the dependencies (Ubuntu 24.04 package names):

```bash
sudo apt-get install -y cmake ninja-build g++ libglfw3-dev libspdlog-dev doctest-dev xvfb
```

Then configure, build and test:

```bash
cmake --preset debug
```

```bash
cmake --build --preset debug
```

```bash
ctest --preset debug
```

Without a display, the window tests are reported as skipped. To run them
headless, wrap the test command in a virtual display:

```bash
xvfb-run -a ctest --preset debug
```

Other presets: `release`, and `asan` (AddressSanitizer + UBSan).

## Building on Windows

Needs Visual Studio 2022 and [vcpkg](https://github.com/microsoft/vcpkg) with
`VCPKG_ROOT` set. `vcpkg.json` lists the dependencies; vcpkg installs them on
the first configure.

```bash
cmake --preset windows
```

```bash
cmake --build --preset windows-debug
```

```bash
ctest --preset windows-debug
```

The Windows presets have not been run yet; the Linux ones have.

## Running

```bash
./build/debug/app/playground_app --verbose
```

| Option | Effect |
|---|---|
| `--frames N` | exit after N frames |
| `--verbose` | debug-level logging, including frame-time statistics every 5 s |
| `--log-file PATH` | where the log goes (default `playground.log`) |

Exit codes: 0 clean exit (window closed, Ctrl+C, or frame limit), 1 start-up
or runtime failure, 2 bad arguments.
