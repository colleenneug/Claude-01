#include "playground/engine/Assets.h"

#include "playground/core/Log.h"

#include <atomic>
#include <cstring>
#include <fstream>
#include <system_error>

namespace playground::engine {

using log::Category;

namespace detail {
u32 nextAssetTypeId() noexcept {
  static std::atomic<u32> next{0};
  return next.fetch_add(1, std::memory_order_relaxed);
}
}  // namespace detail

std::string_view describe(AssetState state) noexcept {
  switch (state) {
    case AssetState::Invalid: return "invalid";
    case AssetState::Loading: return "loading";
    case AssetState::Ready:   return "ready";
    case AssetState::Failed:  return "failed";
  }
  return "invalid";
}

Result<std::vector<std::byte>> readWholeFile(const std::filesystem::path& path) {
  std::error_code ec;
  if (!std::filesystem::exists(path, ec)) {
    return fail(Errc::IoFailure, "asset file '{}' does not exist", path.generic_string());
  }
  if (!std::filesystem::is_regular_file(path, ec)) {
    return fail(Errc::IoFailure, "asset path '{}' is not a file", path.generic_string());
  }
  const auto size = std::filesystem::file_size(path, ec);
  if (ec) return fail(Errc::IoFailure, "could not get the size of '{}': {}", path.generic_string(), ec.message());

  std::ifstream file(path, std::ios::binary);
  if (!file) return fail(Errc::IoFailure, "could not open '{}' for reading", path.generic_string());
  std::vector<std::byte> bytes(static_cast<usize>(size));
  if (size > 0 && !file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size))) {
    return fail(Errc::IoFailure, "could not read all {} bytes of '{}'", size, path.generic_string());
  }
  return bytes;
}

AssetLoader<TextAsset, std::string> textAssetLoader() {
  return {
      .decode = [](std::span<const std::byte> file, const std::filesystem::path&) -> Result<std::string> {
        return std::string(reinterpret_cast<const char*>(file.data()), file.size());
      },
      .finalize = [](std::string&& text, const std::filesystem::path&) -> Result<TextAsset> {
        return TextAsset{.text = std::move(text)};
      },
  };
}

AssetManager::AssetManager(JobSystem& jobs, std::filesystem::path root)
    : jobs_(jobs), root_(std::move(root)), mainThread_(std::this_thread::get_id()) {}

AssetManager::~AssetManager() {
  // Jobs still reading or decoding capture `this`: let them finish first.
  inFlight_.wait();
  const usize dropped = waitingToFinalize();
  if (dropped > 0) log::debug(Category::Engine, "asset manager: {} decoded asset(s) dropped at shutdown", dropped);
}

void AssetManager::assertMainThread() const noexcept {
  PG_ASSERT(std::this_thread::get_id() == mainThread_,
            "AssetManager used off the main thread (only decode runs on workers)");
}

std::string AssetManager::keyFor(std::string_view path) const {
  // One spelling per file, so "a/./b.txt" and "a/b.txt" share a cache entry.
  return (root_ / std::filesystem::path(path)).lexically_normal().generic_string();
}

void AssetManager::postToMainThread(std::function<void()> step) {
  const std::scoped_lock lock(completedMutex_);
  completed_.push_back(std::move(step));
}

usize AssetManager::update(usize budget) {
  assertMainThread();
  usize processed = 0;
  while (processed < budget) {
    std::function<void()> step;
    {
      const std::scoped_lock lock(completedMutex_);
      if (completed_.empty()) break;
      step = std::move(completed_.front());
      completed_.pop_front();
    }
    step();  // outside the lock: finalize may be slow (a GPU upload)
    ++processed;
  }
  return processed;
}

void AssetManager::waitForDecodes() { inFlight_.wait(); }

usize AssetManager::waitingToFinalize() const {
  const std::scoped_lock lock(completedMutex_);
  return completed_.size();
}

bool AssetManager::idle() const { return inFlight_.done() && waitingToFinalize() == 0; }

}  // namespace playground::engine
