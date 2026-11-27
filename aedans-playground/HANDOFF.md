# Aedan's Playground — session handoff

Paste this file into a new session to resume. The plan is `ROADMAP.md`;
build, run and troubleshooting instructions are in `README.md`.

**Status:** the multithreading and asset streaming core is done. It adds:
- a thread pool, `JobSystem`: background tasks with futures, job counters and
  `parallelFor`;
- an `engine::AssetManager`: files are read and decoded on worker threads and
  finished (for example uploaded to the GPU) on the main thread, a budget per
  frame, behind generational `AssetHandle<T>`s with caching by path;
- a ThreadSanitizer build (`tsan` preset) that runs the threaded tests.

The demo creates the job system at start-up and loads `app/assets/welcome.txt`
through the asset manager.

- Repository `colleenneug/Claude-01`, branch `claude/relaxed-cray-5kc2fl`, no pull request open.
- **Long-term goal (user):** Erebus Cradle (the game in `cpp/`) will be moved
  onto this engine eventually; we are still far from that. Roadmap Milestone 5
  covers the port.
- Standing guidelines:
  - modern C++20 (smart pointers, RAII, `std::span`, designated initialisers);
  - no over-engineering;
  - clean, commented, production-ready code;
  - strict error handling and logging for graphics initialisation.
- Decisions:
  - C++20 with `tl::expected`;
  - Windows and Linux only;
  - namespace `playground`, targets `playground_*`;
  - a `Renderer` interface in the render layer;
  - our own sparse-set ECS;
  - storage buffers read by device address;
  - VMA vendored (v3.3.0) on Linux, vcpkg's on Windows;
  - the camera reaches shaders through a UBO at set 0, binding 0;
  - reverse-Z everywhere;
  - translucent sprites are sorted back to front, depth-tested but not
    depth-written;
  - **one locked queue for the job system (no work stealing until profiling
    asks for it)**;
  - **asset decode on workers, finalize on the main thread; workers never
    touch the asset table**.
- Numbering: the user's milestone numbers run ahead of `ROADMAP.md`'s.
- The user may be young or new to this: when asked to explain, keep it short
  and plain, one idea at a time.

---

## 1. Current Architecture State

```
core      Result, Log, Assert, FrameClock, FixedStep, Types, JobSystem + JobCounter (new)
platform  Platform, Window, Input
gpu       Context (Instance, Surface, GPU, Device, Allocator), Buffer, Image, Swapchain, DeviceHandle
ecs       Entity, ComponentPool<T>, World
render    Renderer, FrameData; VulkanRenderer (private)
engine    Application, Camera, AssetManager + AssetHandle<T> (new), ActionMap, WorldBounds,
          components, systems
app       main.cpp: JobSystem, AssetManager (welcome.txt), 25,000 sprites, player, camera
```

**JobSystem** (`core/JobSystem.h/.cpp`, namespace `playground`)
- `JobSystem(workers = 0)`: 0 means `hardware_concurrency − 1`, with at
  least 1. N `std::thread`s share one `std::deque<std::function<void()>>`
  guarded by a mutex and a condition variable.
- `submit(fn) → std::future<R>`, through a shared `std::packaged_task`;
  exceptions travel through the future.
- `run(fn, JobCounter&)`: fire and forget. An exception is logged; it does
  not propagate.
- `parallelFor(count, batch, fn(begin, end))`:
  - the caller runs batch 0;
  - the first exception is rethrown after all batches finish;
  - a single batch runs inline.
- `runOne()` pops and runs one queued job on the calling thread.
- `isWorkerThread()`.
- Destructor: workers drain the queue, then are joined. `push` after
  shutdown is a `PG_VERIFY` failure.

**JobCounter**
- `pending_` and `system_` are atomics.
- `wait()` *helps*: it runs queued jobs while its own are pending, so nested
  `parallelFor` cannot deadlock, even with one worker.
- When there is nothing to help with, `wait()` blocks with C++20
  `atomic::wait` on the **JobSystem's** `completions_` counter, not on the
  `JobCounter`. A job's decrement of `pending_` is its last touch of the
  counter, so a counter on the waiter's stack can be destroyed the moment
  `wait()` returns: no use-after-free. A test covers this (2,000 stack
  counters).

**AssetManager** (`engine/Assets.h/.cpp`)
- `AssetLoader<T, Decoded>{decode, finalize}`:
  - `decode(span<const byte>, path) → Result<Decoded>` runs on workers,
    concurrently, and must touch no shared state;
  - `finalize(Decoded&&, path) → Result<T>` runs on the main thread.
- `registerType<T>(loader)` creates `Store<T>`: slots (key, state,
  generation, `optional<T>`, `optional<Error>`), a free list, and a `byKey`
  map keyed by `(root / path).lexically_normal()`.
- `load<T>(path)`:
  - returns the cached handle if the path is already loaded or loading;
  - otherwise claims a slot (state Loading) and calls `jobs.run(...)` with
    the counter `inFlight_`;
  - the job does `readWholeFile` + `decode`, then posts a closure to the
    locked `completed_` deque.
- `update(budget = 8)` pops up to `budget` closures and runs each outside the
  lock. A closure first checks that the slot's generation still matches and
  that it is still Loading (otherwise the asset was cancelled and is
  skipped), then marks it Failed with the error, or finalizes it and marks
  it Ready.
- `unload` destroys the value now, bumps the generation (which cancels an
  in-flight load) and frees the slot.
- `state` / `get` / `error`; `waitingToFinalize()`, `idle()`,
  `waitForDecodes()` (which helps decode while it waits).
- Destructor: waits on `inFlight_` (jobs capture `this`), then drops
  unfinalized results.
- Every main-thread method asserts the calling thread in debug builds.

**Build**
- Root `CMakeLists.txt`: `find_package(Threads)`, and an option
  `PLAYGROUND_TSAN`.
- `cmake/ProjectOptions.cmake` adds `-fsanitize=thread` and fails if TSan is
  combined with `PLAYGROUND_SANITIZERS` (ASan).
- Presets `tsan` (configure, build and test). The test preset runs label
  `threads` (job_tests, asset_tests).
- `app/CMakeLists.txt` copies `app/assets/` next to the executable after
  each build.

**Verified** (GCC 13.3, Clang 18.1, GCC + ASan + UBSan, GCC + TSan; Mesa
llvmpipe, Xvfb):

| Check | Result |
|---|---|
| Baseline at `5338dae` | 10/10 suites passed |
| After | **12/12 suites** (new `job_tests`, `asset_tests`) in debug, release, Clang and ASan; **TSan 2/2**, run 3×, 0 race reports; 0 warnings; 0 validation messages |
| TSan itself | a deliberate data race in a scratch program was caught (exit code 66), so a clean run means something |
| `job_tests` (10 cases) | parallelFor visits every index exactly once (sizes 0…100,003, batches 1…5,000); uses several threads; 2M-element sum matches; exception rethrown; futures carry values and exceptions; a counter waits for 1,000 jobs; nested parallelFor on 1 worker; 4 threads submitting at once; destructor drains the queue; 2,000 stack counters with no use-after-free |
| `asset_tests` (10 cases) | async load becomes Ready only after `update()`; cache by path (`data/./a.txt`); a missing file fails naming it; a decoder's message is kept; types are kept apart; decode on workers and finalize on the main thread during frames; update budget 3, then 7; unload cancels (finalize never runs) and a reload works; stale handle after slot reuse; 300 files concurrently; destroying the manager mid-load |
| `render_tests` (+1, 12 cases) | a 64 KiB file decoded on a worker, uploaded to a device-local `gpu::Buffer` in finalize on the main thread, read back byte for byte |
| App | logs `job system: 3 worker thread(s)` and `asset welcome.txt: Welcome to Aedan's Playground.` |

**Not yet run on a real GPU or on Windows.**

## 2. Exact Files & Classes Modified

New:

| File | Contents |
|---|---|
| `core/include/playground/core/JobSystem.h`, `core/src/JobSystem.cpp` | `JobCounter`, `JobSystem` |
| `engine/include/playground/engine/Assets.h`, `engine/src/Assets.cpp` | `AssetState`, `AssetHandle<T>`, `AssetLoader<T, Decoded>`, `TextAsset`, `textAssetLoader()`, `readWholeFile()`, `AssetManager` |
| `tests/job_tests.cpp` | 10 tests |
| `tests/asset_tests.cpp` | 10 tests |
| `app/assets/welcome.txt` | The demo's first asset |

Changed:

| File | Change |
|---|---|
| `CMakeLists.txt` | `find_package(Threads REQUIRED)`; option `PLAYGROUND_TSAN` |
| `cmake/ProjectOptions.cmake` | ThreadSanitizer flags; refuses TSan + ASan together |
| `CMakePresets.json` | `tsan` configure, build and test presets (test = label `threads`) |
| `core/CMakeLists.txt` | `JobSystem.cpp`; links `Threads::Threads` publicly |
| `engine/CMakeLists.txt` | `Assets.cpp` |
| `app/CMakeLists.txt` | Copies `app/assets/` next to the executable after each build |
| `app/main.cpp` | Creates the `JobSystem` first (destroyed last) and the `AssetManager` after the renderer; loads `welcome.txt`; `assets.update()` each frame; logs the text (or a warning if missing) |
| `tests/CMakeLists.txt` | `job_tests`, `asset_tests` (label `threads`) |
| `tests/render_tests.cpp` | +1: an asset decoded on a worker and uploaded to the GPU on the main thread |
| `README.md` | Job system and asset manager sections; `tsan` preset; layout table; start-up log |

## 3. Code Snippet of the Core Loop/Active Component

`core/include/playground/core/JobSystem.h`, `parallelFor`:

```cpp
template <class Fn>
void JobSystem::parallelFor(usize count, usize batchSize, Fn&& fn) {
  if (count == 0) return;
  const usize batch = batchSize == 0 ? 1 : batchSize;
  const usize batches = (count + batch - 1) / batch;
  if (batches == 1) { fn(usize{0}, count); return; }      // one batch: no other thread

  std::exception_ptr firstError;
  std::mutex errorMutex;
  JobCounter counter;
  for (usize b = 1; b < batches; ++b) {                    // batch 0 runs here, below
    const usize begin = b * batch;
    const usize end = begin + batch < count ? begin + batch : count;
    run([&fn, &firstError, &errorMutex, begin, end] {
          try { fn(begin, end); }
          catch (...) { const std::scoped_lock lock(errorMutex); if (!firstError) firstError = std::current_exception(); }
        }, counter);
  }
  try { fn(usize{0}, batch < count ? batch : count); }    // the caller does a share too
  catch (...) { const std::scoped_lock lock(errorMutex); if (!firstError) firstError = std::current_exception(); }
  counter.wait();                                          // jobs reference the locals above
  if (firstError) std::rethrow_exception(firstError);
}
```

`core/src/JobSystem.cpp`, the helping wait (safe to destroy the counter right after):

```cpp
void JobCounter::wait() {
  while (!done()) {
    JobSystem* system = system_.load(std::memory_order_acquire);
    if (system == nullptr) { std::this_thread::yield(); continue; }
    if (system->runOne()) continue;                          // help instead of sleeping
    const u64 seen = system->completions_.load(std::memory_order_acquire);
    if (done()) return;                                      // finished between the checks
    system->completions_.wait(seen, std::memory_order_acquire);  // block on the SYSTEM's counter
  }
}
// A job's last two steps: counter.pending_.fetch_sub(1) (its last touch of the
// counter), then completions_.fetch_add(1) + completions_.notify_all().
```

`engine/include/playground/engine/Assets.h`, one load: decode on a worker, finalize on the main thread:

```cpp
raw->start = [this, raw, shared](u32 index, u32 generation, std::filesystem::path path) {
  jobs_.run([this, raw, shared, index, generation, path = std::move(path)] {
    Result<Decoded> decoded = [&]() -> Result<Decoded> {         // worker: touches nothing shared
      auto bytes = readWholeFile(path);
      if (!bytes) return tl::unexpected(std::move(bytes.error()));
      return shared->decode(*bytes, path);
    }();
    auto result = std::make_shared<Result<Decoded>>(std::move(decoded));
    postToMainThread([raw, shared, index, generation, path, result] {  // runs in update()
      auto& slot = raw->slots[index];
      if (slot.generation != generation || slot.state != AssetState::Loading) return;  // cancelled
      if (!*result) { slot.state = AssetState::Failed; slot.error = std::move(result->error()); return; }
      Result<T> finished = shared->finalize(std::move(**result), path);              // e.g. GPU upload
      if (!finished) { slot.state = AssetState::Failed; slot.error = std::move(finished.error()); return; }
      slot.value.emplace(std::move(*finished));
      slot.state = AssetState::Ready;
    });
  }, inFlight_);
};
```

## 4. Known Bugs & Incomplete Tasks

No known bugs.

**Found and fixed this session**
- `JobCounter::system_` was a plain pointer written by several submitting
  threads. It is now atomic.
- `wait()` used to spin with `yield`. It now blocks with `atomic::wait`.
- A notify after the final decrement could touch a counter its waiter had
  already destroyed. The notify moved to `JobSystem::completions_`.
- `waitForDecodes()` helps, so the main thread could run a decode. This was
  caught by the GPU asset test. It is documented, and the decode-on-worker
  tests now run frames with `update()` only, as the game does.

**Not verified**
- A real GPU; Windows (MSVC warnings, `std::atomic::wait` support in
  MSVC's library, the vcpkg build).
- TSan with the GPU tests (only the `threads` label runs under TSan; Mesa
  itself is not TSan-instrumented and would be noisy).

**Limitations, acceptable for now**
- One locked queue: fine for tens of thousands of jobs per second; for many
  tiny jobs, per-worker deques with stealing would scale better. Measure
  first.
- No job priorities: a burst of asset loads queues ahead of a
  `parallelFor`'s batches. The caller of `parallelFor` helps, so it never
  stalls, but it may run more of the work itself.
- `run()` swallows exceptions (logs them); use `submit()` to see them.
- No reference counting on assets: `unload` frees immediately, so the
  caller must know nothing (including the GPU) still uses it.
- No hot reload, no dependencies between assets, no streaming by distance.
  Only `TextAsset` ships; GPU types are registered by the caller.
- The renderer's public API has no texture or mesh upload yet, so the app
  cannot load GPU assets itself (render keeps Vulkan private). Textures
  (next steps) will add the first one.
- The ECS is not parallel yet: `movementSystem` and the rest are
  single-threaded.
- The asset folder is found from `argv[0]`. If the app is launched through
  `PATH` with no directory, it looks in `./assets`, logs a warning, and the
  game still runs.
- Carried over: same-layer opaque sprites would z-fight in 3D; translucent
  sort by layer, not camera distance; sprite buffers never shrink;
  `submitAndWait` makes a pool per call; fences, not timeline semaphores; no
  Vulkan debug names; no pipeline cache; GCC sanitizer builds preload the
  Mesa driver for GPU tests.

## 5. Immediate Next Steps for Next Weekend

1. **Textures through the asset manager**:
   - `stb_image` decode on workers (vendored and inspected);
   - finalize = a `gpu::Image` staging upload;
   - a sampler and a bindless texture array (descriptor indexing is already a
     required feature);
   - `Sprite` gains a texture index;
   - the render layer exposes a Vulkan-free `TextureHandle` plus a
     `Renderer::createTexture(pixels)` so the app can register a texture
     loader.
2. **Parallel ECS systems**:
   - `World::parallelEach<Ts...>` over a pool's dense array, using
     `JobSystem::parallelFor`;
   - make `movementSystem` / `bounceSystem` parallel;
   - measure 25,000 → 250,000 sprites (release, llvmpipe) before and after;
   - add a TSan test.
3. **Vulkan debug names** (`vkSetDebugUtilsObjectNameEXT`) via a
   `gpu::setDebugName` helper, a no-op without validation.
4. **Pipeline cache** saved to disk, with a header check.
5. Later: camera controls for the 3D view; roadmap Milestone 4 (visual parity
   with `cpp/`) and Milestone 5 (moving Erebus Cradle onto the engine).

**To resume, tell the next session:** "Read `aedans-playground/HANDOFF.md` on
branch `claude/relaxed-cray-5kc2fl`, confirm `xvfb-run -a ctest --preset
debug` passes (reinstall the packages in `README.md` if the setup script did
not; `xdotool` is needed for `keyboard_e2e`), then start step 1."
