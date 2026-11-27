#pragma once
// =============================================================================
//  playground/engine/Assets.h — loading files without stopping the game.
//
//  A file is loaded in two halves:
//
//    decode    on a WORKER thread (JobSystem): read the file, turn its bytes
//              into CPU-side data (parse a config, decompress an image). The
//              slow part, and none of it can touch the GPU or the game.
//              (Exception: a thread that blocks in waitForDecodes() helps
//              with the queue, so the main thread may decode while it waits,
//              e.g. on a loading screen. In normal frames, which only call
//              update(), the main thread never decodes.)
//    finalize  on the MAIN thread, in update(): turn that data into the
//              finished asset, e.g. upload it to the GPU. Vulkan objects and
//              the renderer belong to the main thread, so this is where they
//              are made. update() does at most `budget` of these per frame,
//              so a burst of loads spreads over frames instead of stalling one.
//
//  THREAD SAFETY, IN ONE RULE
//    Workers never touch the asset table. A worker reads its file, decodes
//    it, and hands the result to the main thread through one locked queue.
//    Everything else (load, get, state, unload, update) is main-thread
//    only; debug builds assert it. So the only shared data is that queue.
//
//  HANDLES AND CACHING
//    load<T>("textures/ship.png") returns an AssetHandle<T> at once, while
//    the file loads. Loading the same path again returns the same handle
//    (one copy in memory). A handle is an index plus a generation, like an
//    ecs::Entity: after unload() its slot can be reused, and old handles
//    find nothing instead of the new asset. Unloading an asset that is still
//    loading cancels it: its result is thrown away, and never finalized.
//
//  USE
//    AssetManager assets(jobs, "assets");
//    assets.registerType<TextAsset>(textAssetLoader());
//    AssetHandle<TextAsset> intro = assets.load<TextAsset>("story/intro.txt");
//    ... each frame: assets.update(); ...
//    if (const TextAsset* text = assets.get(intro)) show(text->text);
// =============================================================================

#include "playground/core/Assert.h"
#include "playground/core/JobSystem.h"
#include "playground/core/Result.h"
#include "playground/core/Types.h"

#include <cstddef>
#include <deque>
#include <filesystem>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace playground::engine {

enum class AssetState : u8 {
  Invalid,  // not a live handle (never loaded, unloaded, or stale)
  Loading,  // being read/decoded, or waiting for update() to finalize it
  Ready,    // get() returns it
  Failed,   // error() says why
};
[[nodiscard]] std::string_view describe(AssetState state) noexcept;

template <class T>
struct AssetHandle {
  static constexpr u32 kInvalidIndex = std::numeric_limits<u32>::max();
  u32 index = kInvalidIndex;
  u32 generation = 0;

  [[nodiscard]] constexpr bool valid() const noexcept { return index != kInvalidIndex; }
  friend constexpr bool operator==(const AssetHandle&, const AssetHandle&) = default;
};

// How one asset type is made. `decode` runs on worker threads, possibly
// several at once: it must not touch shared mutable state. `finalize` runs on
// the main thread.
template <class T, class Decoded>
struct AssetLoader {
  std::function<Result<Decoded>(std::span<const std::byte> file, const std::filesystem::path& path)> decode;
  std::function<Result<T>(Decoded&& decoded, const std::filesystem::path& path)> finalize;
};

// A ready-made type: a text file, as a string. Finalize is trivial (no GPU).
struct TextAsset {
  std::string text;
};
[[nodiscard]] AssetLoader<TextAsset, std::string> textAssetLoader();

// Reads a whole file. Errc::IoFailure, naming the path and the reason.
[[nodiscard]] Result<std::vector<std::byte>> readWholeFile(const std::filesystem::path& path);

class AssetManager {
 public:
  // Paths given to load() are relative to `root`.
  explicit AssetManager(JobSystem& jobs, std::filesystem::path root = ".");
  // Waits for files still being read or decoded (their jobs refer to this
  // manager), then drops them unfinalized.
  ~AssetManager();
  AssetManager(const AssetManager&) = delete;
  AssetManager& operator=(const AssetManager&) = delete;

  template <class T, class Decoded>
  void registerType(AssetLoader<T, Decoded> loader);

  // Starts loading (or returns the handle already loading/loaded for this
  // path). An invalid handle if T was never registered (a bug: asserts).
  template <class T>
  [[nodiscard]] AssetHandle<T> load(std::string_view path);

  template <class T>
  [[nodiscard]] AssetState state(AssetHandle<T> handle) const;
  // The asset once Ready; null before, after failure, or for a stale handle.
  template <class T>
  [[nodiscard]] const T* get(AssetHandle<T> handle) const;
  // Why it failed, or null.
  template <class T>
  [[nodiscard]] const Error* error(AssetHandle<T> handle) const;
  // Frees it now (cancelling it if still loading). Old handles go invalid.
  // For GPU assets, the caller must know the GPU is no longer using it.
  template <class T>
  void unload(AssetHandle<T> handle);

  // Main thread, once per frame: finalizes up to `budget` decoded assets
  // (failures count too). Returns how many it processed.
  usize update(usize budget = 8);

  // Blocks until no file is being read or decoded. Does not finalize: call
  // update() for that. For tests and loading screens. While it waits, the
  // calling thread helps decode (JobCounter::wait).
  void waitForDecodes();

  [[nodiscard]] usize waitingToFinalize() const;
  [[nodiscard]] bool idle() const;  // nothing decoding, nothing waiting to finalize

 private:
  struct StoreBase {
    virtual ~StoreBase() = default;
  };

  template <class T>
  struct Store final : StoreBase {
    struct Slot {
      std::string key;
      AssetState state = AssetState::Invalid;
      u32 generation = 0;
      std::optional<T> value;
      std::optional<Error> error;
    };
    std::vector<Slot> slots;
    std::vector<u32> freeSlots;
    std::unordered_map<std::string, u32> byKey;
    // Starts the worker half for one slot (set by registerType).
    std::function<void(u32 index, u32 generation, std::filesystem::path path)> start;

    [[nodiscard]] Slot* find(AssetHandle<T> handle) {
      if (handle.index >= slots.size()) return nullptr;
      Slot& slot = slots[handle.index];
      return slot.generation == handle.generation && slot.state != AssetState::Invalid ? &slot : nullptr;
    }
    [[nodiscard]] const Slot* find(AssetHandle<T> handle) const {
      return const_cast<Store*>(this)->find(handle);
    }
  };

  template <class T>
  [[nodiscard]] Store<T>* findStore() const;

  // Called from worker jobs: hand a main-thread step to update().
  void postToMainThread(std::function<void()> step);
  void assertMainThread() const noexcept;
  [[nodiscard]] std::string keyFor(std::string_view path) const;

  JobSystem& jobs_;
  std::filesystem::path root_;
  std::thread::id mainThread_;
  std::vector<std::unique_ptr<StoreBase>> stores_;  // indexed by asset type id

  JobCounter inFlight_;  // files being read or decoded
  mutable std::mutex completedMutex_;
  std::deque<std::function<void()>> completed_;  // decoded, waiting for update()
};

// --- Templates ---------------------------------------------------------------------

namespace detail {
u32 nextAssetTypeId() noexcept;
template <class T>
u32 assetTypeId() noexcept {
  static const u32 id = nextAssetTypeId();
  return id;
}
}  // namespace detail

template <class T>
AssetManager::Store<T>* AssetManager::findStore() const {
  const u32 id = detail::assetTypeId<T>();
  return id < stores_.size() ? static_cast<Store<T>*>(stores_[id].get()) : nullptr;
}

template <class T, class Decoded>
void AssetManager::registerType(AssetLoader<T, Decoded> loader) {
  assertMainThread();
  PG_VERIFY(loader.decode && loader.finalize, "an asset loader needs both decode and finalize");
  const u32 id = detail::assetTypeId<T>();
  if (id >= stores_.size()) stores_.resize(static_cast<usize>(id) + 1);
  auto store = std::make_unique<Store<T>>();
  Store<T>* raw = store.get();

  // The loader is shared by every load of this type: decode may run on
  // several workers at once (it must be thread-safe), finalize only here.
  auto shared = std::make_shared<AssetLoader<T, Decoded>>(std::move(loader));

  raw->start = [this, raw, shared](u32 index, u32 generation, std::filesystem::path path) {
    jobs_.run(
        [this, raw, shared, index, generation, path = std::move(path)] {
          // --- worker thread: read and decode. Touches nothing shared. ---
          Result<Decoded> decoded = [&]() -> Result<Decoded> {
            auto bytes = readWholeFile(path);
            if (!bytes) return tl::unexpected(std::move(bytes.error()));
            return shared->decode(*bytes, path);
          }();
          // std::function must be copyable; the decoded data may not be.
          auto result = std::make_shared<Result<Decoded>>(std::move(decoded));

          // --- main thread, in update(): finalize, unless cancelled. ---
          postToMainThread([raw, shared, index, generation, path, result] {
            auto& slot = raw->slots[index];
            if (slot.generation != generation || slot.state != AssetState::Loading) return;  // unloaded meanwhile
            if (!*result) {
              slot.state = AssetState::Failed;
              slot.error = std::move(result->error());
              return;
            }
            Result<T> finished = shared->finalize(std::move(**result), path);
            if (!finished) {
              slot.state = AssetState::Failed;
              slot.error = std::move(finished.error());
              return;
            }
            slot.value.emplace(std::move(*finished));
            slot.state = AssetState::Ready;
          });
        },
        inFlight_);
  };
  stores_[id] = std::move(store);
}

template <class T>
AssetHandle<T> AssetManager::load(std::string_view path) {
  assertMainThread();
  Store<T>* store = findStore<T>();
  PG_ASSERT(store != nullptr, "load<T>() for an asset type that was never registered");
  if (store == nullptr) return {};

  std::string key = keyFor(path);
  if (const auto it = store->byKey.find(key); it != store->byKey.end()) {
    return AssetHandle<T>{.index = it->second, .generation = store->slots[it->second].generation};
  }

  u32 index = 0;
  if (!store->freeSlots.empty()) {
    index = store->freeSlots.back();
    store->freeSlots.pop_back();
  } else {
    index = static_cast<u32>(store->slots.size());
    store->slots.emplace_back();
  }
  auto& slot = store->slots[index];
  slot.key = key;
  slot.state = AssetState::Loading;
  slot.value.reset();
  slot.error.reset();
  store->byKey.emplace(key, index);
  store->start(index, slot.generation, std::filesystem::path(key));
  return AssetHandle<T>{.index = index, .generation = slot.generation};
}

template <class T>
AssetState AssetManager::state(AssetHandle<T> handle) const {
  assertMainThread();
  const Store<T>* store = findStore<T>();
  const auto* slot = store ? store->find(handle) : nullptr;
  return slot ? slot->state : AssetState::Invalid;
}

template <class T>
const T* AssetManager::get(AssetHandle<T> handle) const {
  assertMainThread();
  const Store<T>* store = findStore<T>();
  const auto* slot = store ? store->find(handle) : nullptr;
  return slot && slot->state == AssetState::Ready ? &*slot->value : nullptr;
}

template <class T>
const Error* AssetManager::error(AssetHandle<T> handle) const {
  assertMainThread();
  const Store<T>* store = findStore<T>();
  const auto* slot = store ? store->find(handle) : nullptr;
  return slot && slot->error ? &*slot->error : nullptr;
}

template <class T>
void AssetManager::unload(AssetHandle<T> handle) {
  assertMainThread();
  Store<T>* store = findStore<T>();
  auto* slot = store ? store->find(handle) : nullptr;
  if (slot == nullptr) return;
  store->byKey.erase(slot->key);
  slot->key.clear();
  slot->value.reset();  // destroys the asset now
  slot->error.reset();
  slot->state = AssetState::Invalid;
  ++slot->generation;   // old handles, and a load still in flight, no longer match
  store->freeSlots.push_back(handle.index);
}

}  // namespace playground::engine
