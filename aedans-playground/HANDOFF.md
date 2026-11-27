# Aedan's Playground — session handoff

Paste this file into a new session to resume. The plan is `ROADMAP.md`;
build, run and troubleshooting instructions are in `README.md`.

**Status:** Stage 3, prompt 7 is done: **networking**. It's built on the
renderer milestones (PBR, image-based lighting, shadows, HDR
post-processing; start of this milestone: commit `8f0d485`).
- **`net/` library** (target `playground_net`, alias `engine_net`):
  - an ENet transport behind pimpl: `Host`, `Peer`, `Packet`;
  - two channels: `Reliable` (reliable and ordered) and `Unreliable`
    (unsequenced);
  - a bit-level `BitWriter`/`BitReader` (endian-safe, no struct casts,
    overrun-proof);
  - quantization: bounded floats, vectors, smallest-three quaternions.
- **Dedicated server:** `--server [--port N]` runs the 120 Hz fixed-step
  simulation headless. GLFW and Vulkan are never started.
  - It welcomes clients (reliable) and sends quantized 20 Hz snapshots
    (unreliable).
  - Ctrl+C or SIGTERM ends it cleanly, after telling every client goodbye.
  - `--connect HOST[:PORT]` is a headless test client.
- Ctrl+C handling moved into `core` (`InterruptGuard`); the GLFW
  `Platform` uses it too.

- Repository `colleenneug/Claude-01`, branch `claude/relaxed-cray-5kc2fl`, no pull request open.
- **Long-term goal (user):** Erebus Cradle (the game in `cpp/`) will be moved
  onto this engine eventually. Its co-op runs over its own UDP layer;
  `net/` is where that moves to. Roadmap Milestone 5 covers the port.
- **The helmet** (`app/assets/third_party/DamagedHelmet/`, kept at the
  user's request) is CC BY 4.0 on top of CC BY-NC 4.0. It is credited, and
  **non-commercial only**: remove it before anything built on this engine
  is sold.
- **Environment:** the cloud environment's setup script now also installs
  `libenet-dev`. Not yet proven in a fresh session; if configure cannot
  find `enet/enet.h` or `tiny_gltf.h`, install the packages listed in
  `README.md`.
- Standing guidelines:
  - modern C++20 (smart pointers, RAII, `std::span`, designated initialisers);
  - no over-engineering;
  - clean, commented, production-ready code;
  - strict error handling and logging for graphics initialisation.
- Decisions:
  - C++20 with `tl::expected`;
  - Windows and Linux only;
  - no Vulkan types above render;
  - reverse-Z everywhere;
  - asset decode on workers;
  - bindless textures;
  - tone mapping only in the composite pass;
  - FrameData effects off by default;
  - **`net/` depends on `core` only, with ENet PRIVATE: no ENet or
    winsock types in public headers**;
  - **`Peer` is a never-reused id, not a pointer**;
  - **`Packet` owns a copy of its bytes**;
  - **one thread per `Host`**;
  - **bit order is least-significant first; values move by shifts, never
    struct casts**;
  - **decoders check `ok()` once after a whole message**;
  - **the server's network wait is its tick sleep**;
  - **game messages live in `app/Protocol.h`, not the engine**.
- The user's milestone numbers run ahead of `ROADMAP.md`'s.
- The user may be young or new to this: when asked to explain, keep it short
  and plain, one idea at a time.

---

## 1. Current Architecture State

```
core      Result, Log (+ "net" category), Assert, FrameClock, FixedStep, Types, JobSystem,
          InterruptGuard (new: Ctrl+C / SIGTERM as a stop request)
net       BitWriter, BitReader, Quantize (quantize/dequantize, vec2/vec3, smallest-three quaternions),
   (new)  Host (pimpl over ENet), Peer, Packet, Event, Channel, PeerInfo
platform  Platform (GLFW; holds an InterruptGuard), Window, Input
gpu       Context, Buffer, Image, Sampler, Swapchain, DeviceHandle
render    Renderer, FrameData, Resources, Environment, Shadow; VulkanRenderer + Mesh, ResourcePool,
          IblBaker, BrdfLut, PostProcess, ShadowMap
engine    Application, Camera, CameraController, AssetManager, Gltf, EnvironmentAsset, systems
app       main.cpp (+ --server, --port, --connect), Server (new), Protocol (new), ModelScene, SpriteScene
```

**`net/BitStream`:**
- `BitWriter::writeBits(value, count ≤ 64)` works 8 bits at a time:
  `bytes[pos/8] |= chunk << (pos % 8)`. Debug builds assert that high bits
  are zero; release builds mask them off.
- Typed helpers: `writeBool`, `U8`/`U16`/`U32`/`U64`, `writeI32` (bit_cast
  to u32), `writeF32` (bit_cast), `writeVarU32` (7-bit groups plus a
  continuation bit), `writeString` (VarU32 length, capped at `maxLength`),
  `writeBytes`, `alignToByte`, `take()`.
- `BitReader` mirrors it. On overrun, more than 64 bits, a varint longer
  than 5 groups or over 32 bits, a string length past the end or over
  `maxLength`, or a short `readBytes`, `fail()` sets a sticky flag and the
  read position jumps to the end; the read returns 0 or empty.

**`net/Quantize`:**
- `quantize(v, min, max, bits)` = `llround(clamp((v − min)/(max − min), 0,
  1) × (2^bits − 1))`, done in f64; NaN gives 0. `quantizationError()` is
  half a step.
- Smallest-three quaternions:
  - normalise, find the largest |component| (order x, y, z, w), flip the
    sign so it is positive;
  - write its index (2 bits) and the other three in ±(1/√2 + 1e-4) at N
    bits;
  - reading rebuilds the largest as `sqrt(1 − Σ²)` and normalises.

**`net/Host`** (`Host.cpp` is the only file with `<enet/enet.h>`):
- `Impl` holds the `ENetHost*`, maps between `ENetPeer*` and id (both
  ways), and the next id.
- ENet's `enet_initialize`/`enet_deinitialize` are reference-counted under
  a mutex.
- `listen` uses `ENET_HOST_ANY` on a port (0 means any free port; ENet
  reports the bound one). `client` creates a host with no address. Both
  use 2 channels.
- `service(timeout)` waits once for the first event, then drains with
  timeout 0:
  - CONNECT: remember the peer;
  - RECEIVE: copy the bytes, map the channel id to a `Channel`, destroy
    the packet;
  - DISCONNECT: forget the peer.
- `send`: Reliable → `ENET_PACKET_FLAG_RELIABLE`, Unreliable →
  `ENET_PACKET_FLAG_UNSEQUENCED`; channel id = enum value. If
  `enet_peer_send` fails, the packet is destroyed.
- Also: `broadcast` (connected peers only), `flush`, `disconnect`,
  `disconnectAll`, `port`, `connectedPeers`, `knownPeers` (any state, for
  waiting out goodbyes) and `info` (address, RTT).

**Server** (`app/Server.cpp`):
- `runServer`: an `InterruptGuard`, `Host::listen`, 64 bouncing entities
  (a fixed seed) in an `ecs::World`, `FrameClock`, `FixedStepAccumulator`.
- Each loop:
  - `service(min(time to the next tick, 8 ms))`. On connect: log it and
    send a Welcome (reliable). On disconnect: log it.
  - Then the steps: movement, bounce, flush; every 6th tick broadcast a
    Snapshot (unreliable); `flush()`.
  - Every 5 s, a status line.
- On stopping: `disconnectAll`, then service until `knownPeers() == 0` or
  500 ms.
- `runClient`: connect, then service every 8 ms for N / 120 s. Decode with
  `peekKind`, `decodeWelcome` or `decodeSnapshot`, counting the malformed
  ones. Then a summary line, a goodbye, and exit 0 only if welcomed and
  never dropped.
- `main.cpp` takes these branches straight after logging starts, before
  the JobSystem, the Application or anything of GLFW.

**Protocol** (`app/Protocol.h`):
- A 4-bit kind first.
- Welcome: u16 version (1), u16 tick rate, u32 tick, varint entity count.
- Snapshot: u32 tick, varint count (≤ 128, checked before reserving),
  then per entity a varint id, 2×16-bit position in ±1024, and a 10-bit
  angle over [0, 2π] (wrapped).

**Verified** (GCC 13.3 debug and release, Clang 18.1, GCC + ASan + UBSan,
GCC + TSan; Mesa llvmpipe, Xvfb):

| Check | Result |
|---|---|
| Baseline at `8f0d485` | 19/19 suites passed |
| After | **21/21 suites** (new `net_tests`, `server_e2e`) in debug, release, Clang and ASan+UBSan (**no UBSan runtime errors in the bit packer**); TSan 3/3, 3 runs, 0 race reports, and `net_tests` under TSan: 0 reports; 0 warnings everywhere |
| `net_tests` (12, 0.04 s) | exact layout (`0b101`,`0xFF` → `FD 07`; `0x12345678` → `78 56 34 12`); every width 1..64 three times at every alignment; i32 extremes, floats bit-exact (−0, ∞, NaN), varints at 7-bit edges, strings, bytes, alignment; varint sizes (100 → 8 bits, u32 max → 40); past the end → zeros, sticky failure, `readBits(65)`, empty data; hostile string lengths, over-long varints, short byte reads (zeroed), a message cut by one byte; quantization error ≤ half a step (+ one f32 step at 20–24 bits), exact ends, clamping, NaN, 1-bit; vec2/vec3 bit counts and error; **1,000 random rotations: 32 bits each, worst error < 0.25°**; identity and sign-flipped unnormalised input; **loopback**: client ↔ server on 127.0.0.1, reliable client → server and unreliable server → client on the right channels, bit-packed payloads decoded, `info()` address, polite disconnect seen by the server, stale peer refused; **100 reliable messages all arrive in order**; a port in use refused, naming it |
| `server_e2e` (no `DISPLAY`) | server on a free port; client for 2 s: welcomed, ≥ 20 snapshots (got ~40), 64 entities each, 0 malformed; the server logs the connect and the goodbye; Ctrl+C → "interrupted", exit 0; no GLFW, Vulkan or GPU line in the server's log |
| By hand | the server reacts to SIGINT within ~8 ms (`strace`) |

**Not yet run on Windows** (ENet via vcpkg's `unofficial::enet::enet`, ws2_32).

## 2. Exact Files & Classes Modified

New:

| File | Contents |
|---|---|
| `net/CMakeLists.txt` | `playground_net` (aliases `playground::net`, `engine_net`): PUBLIC core + glm, PRIVATE `playground_enet` |
| `net/include/playground/net/BitStream.h`, `net/src/BitStream.cpp` | `BitWriter`, `BitReader` |
| `net/include/playground/net/Quantize.h`, `net/src/Quantize.cpp` | `quantize`, `dequantize`, `quantizationError`, `writeQuantized`/`readQuantized`, `writeVec2/3`/`readVec2/3`, `writeQuaternion`/`readQuaternion` |
| `net/include/playground/net/Host.h`, `net/src/Host.cpp` | `Channel`, `Peer`, `Packet`, `EventType`, `Event`, `PeerInfo`, `Host` (+ `Host::Impl`) |
| `core/include/playground/core/Interrupt.h`, `core/src/Interrupt.cpp` | `InterruptGuard` |
| `app/Protocol.h`, `app/Protocol.cpp` | `protocol::Welcome`, `Snapshot`, `encode`, `peekKind`, `decodeWelcome`, `decodeSnapshot` |
| `app/Server.h`, `app/Server.cpp` | `ServerOptions`, `ClientOptions`, `runServer`, `runClient` |
| `tests/net_tests.cpp`, `tests/e2e/server.sh` | 12 tests; the dedicated-server end-to-end test |

Changed:

| File | Change |
|---|---|
| `vcpkg.json` | `enet` |
| `CMakeLists.txt` | `playground_enet` interface target (vcpkg `unofficial::enet::enet`, or `find_path`/`find_library`; ws2_32 and winmm on Windows); `add_subdirectory(net)` |
| `core/CMakeLists.txt`, `core/include/playground/core/Log.h`, `core/src/Log.cpp` | `Interrupt.cpp`; `Category::Net` ("net") |
| `platform/include/playground/platform/Platform.h`, `platform/src/Platform.cpp` | Signal handling replaced by an `InterruptGuard` member |
| `app/main.cpp`, `app/CMakeLists.txt` | `--server`, `--port`, `--connect`; headless branches before any GLFW; links `playground::net` |
| `tests/CMakeLists.txt` | `net_tests` (label `net`), `server_e2e` (label `net`, timeout 60 s) |
| `README.md` | Networking section, options, layout, packages (`libenet-dev`) |

## 3. Code Snippet of the Core Loop/Active Component

`app/Server.cpp`, the dedicated server's loop:

```cpp
while (!InterruptGuard::interruptRequested() && (options.maxTicks == 0 || tick < options.maxTicks)) {
  // 1. The network, waiting until (about) the next tick is due: the wait IS the sleep.
  const f64 untilNextTick = std::max(0.0, steps.stepSeconds() - steps.accumulatedSeconds());
  const auto waitMs = static_cast<u32>(std::clamp(untilNextTick * 1000.0, 0.0, 8.0));
  for (const net::Event& e : host->service(waitMs)) { /* welcome on connect; log goodbyes */ }

  // 2. The simulation: fixed 1/120 s steps, a snapshot every 6th tick (20 Hz).
  const FixedStepPlan plan = steps.advance(clock.tick().rawDeltaSeconds);
  for (u32 i = 0; i < plan.steps; ++i) {
    engine::movementSystem(world, dt);
    engine::bounceSystem(world, bounds);
    world.flush();
    if (++tick % protocol::kSnapshotEveryTicks == 0 && host->connectedPeers() > 0) {
      host->broadcast(net::Packet(net::Channel::Unreliable, protocol::encode(snapshotOf(world, tick))));
    }
  }
  host->flush();
}
disconnectEveryone(*host, 500);  // goodbye to every client, then wait for their acknowledgements
```

`net/src/BitStream.cpp`, writing bits without struct casts:

```cpp
while (count > 0) {  // up to eight bits at a time: the rest of the current byte
  const usize byteIndex = bits_ / 8;
  const u32 offset = static_cast<u32>(bits_ % 8);
  if (byteIndex == bytes_.size()) bytes_.push_back(std::byte{0});
  const u32 take = std::min(8u - offset, count);
  const u32 chunk = static_cast<u32>(value & ((u64{1} << take) - 1));
  bytes_[byteIndex] |= static_cast<std::byte>(chunk << offset);
  value >>= take;
  count -= take;
  bits_ += take;
}
```

`net/src/Quantize.cpp`, smallest-three:

```cpp
u32 largest = 0;                                   // drop the biggest component...
for (u32 i = 1; i < 4; ++i) if (std::fabs(c[i]) > std::fabs(c[largest])) largest = i;
const f32 sign = c[largest] < 0.0f ? -1.0f : 1.0f; // ...made positive (q and -q: the same rotation)
out.writeBits(largest, 2);
for (u32 i = 0; i < 4; ++i)
  if (i != largest) writeQuantized(out, c[i] * sign, -kSmallestThreeRange, kSmallestThreeRange, bitsPerComponent);
// reading: the dropped one is sqrt(1 - sum of the others' squares)
```

## 4. Known Bugs & Incomplete Tasks

No known bugs.

**Found and fixed this session**
- **Shutdown goodbyes:** "disconnect everyone, then wait while anyone is
  connected" returned at once, because a peer saying goodbye stops being
  "connected" before the goodbye is even sent. The client exited, and the
  server would only have noticed after ENet's ~30 s timeout. It now waits
  until each peer's Disconnected event (`Host::knownPeers()`).
- The quantization test's bound ignored float precision: at 20+ bits over
  ±1000, f32 is coarser than the step. The bound now includes it, and the
  header says so.
- A measuring mistake, not a bug: a detached server *looked* slow to exit
  (1.6 s) because the container's init reaps zombie processes late;
  `strace` shows it exits ~8 ms after Ctrl+C. The end-to-end test `wait`s
  on its own child.

**Not verified**
- Windows (vcpkg ENet; `ws2_32`/`winmm` linking; MSVC warnings).
- A real network: only loopback (127.0.0.1) so far. No packet loss,
  latency or reordering has been simulated; ENet's reliability is trusted,
  not yet tested under loss.
- The updated environment setup script (`libenet-dev`), until a fresh
  session runs it.

**Limitations, acceptable for now**
- **No replication yet:** the graphical game is not a client. Snapshots
  are a demo of the pipeline (the test client decodes and counts them);
  nothing interpolates or predicts. The server simulates its own bouncing
  entities, not players.
- No input from clients (the server ignores received packets), no
  connection handshake beyond ENet's (no version check enforced: the
  client logs the version but does not leave on a mismatch), no
  authentication or encryption.
- Snapshots are full, not delta-compressed, and capped at 128 entities
  (about 1 KB).
- `Host` is single-threaded by design: networking on another thread
  needs a queue (not built).
- IPv4 only (ENet 1.3).
- Carried over (render): one shadow map (no cascades); lamps do not light
  their surroundings; `setEnvironment` stalls; glTF without skins or
  animation; no frustum culling; uploads `submitAndWait` per resource; the
  ECS doesn't hold 3D objects; no Vulkan debug names; no pipeline cache.

## 5. Immediate Next Steps for Next Weekend

1. **The game as a client:** `--connect` in the windowed game. Draw the
   server's entities from snapshots, interpolating between the last two
   (about 100 ms behind) so 20 Hz looks smooth.
2. **Client input → server:** an unreliable input message every tick
   (sequence-numbered, last few inputs repeated against loss); the server
   moves a player entity per client. Spawn and despawn over reliable
   messages.
3. **Prediction and reconciliation** for the local player: apply input
   at once, correct when the server's state arrives.
4. **A protocol handshake:** reject other versions with a reason; time
   out half-open connections; a test with simulated packet loss and
   latency (ENet has per-peer throttling, but a loss shim between two
   hosts would test our code).
5. **Delta snapshots** (only what changed since the last acknowledged
   one) once entity counts grow.
6. Later, rendering steps still open (point lights, cascaded shadows, 3D
   in the ECS), roadmap Milestone 4 (visual parity with `cpp/`) and
   Milestone 5 (moving Erebus Cradle, including its co-op, onto the
   engine).

**To resume, tell the next session:** "Read `aedans-playground/HANDOFF.md` on
branch `claude/relaxed-cray-5kc2fl`, confirm `xvfb-run -a ctest --preset
debug` passes (if configure cannot find `enet/enet.h` or tiny_gltf.h,
install the packages in `README.md`; `xdotool` is needed for the e2e
tests), then start step 1."
