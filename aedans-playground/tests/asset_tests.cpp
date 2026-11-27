// Tests for the AssetManager: async loading, caching, failures, the
// main-thread finalize budget, cancellation, stale handles, and a stress
// load. No GPU (the GPU path is in render_tests). Also run under
// ThreadSanitizer (label "threads").

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "playground/core/JobSystem.h"
#include "playground/engine/Assets.h"

#include <atomic>
#include <filesystem>
#include <format>
#include <fstream>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

using namespace playground;
using engine::AssetHandle;
using engine::AssetLoader;
using engine::AssetManager;
using engine::AssetState;
using engine::TextAsset;

namespace {

// A fresh folder per test, with helpers to put files in it.
struct TempFolder {
  std::filesystem::path path;
  explicit TempFolder(const std::string& name)
      : path(std::filesystem::temp_directory_path() / "playground_asset_tests" / name) {
    std::filesystem::remove_all(path);
    std::filesystem::create_directories(path);
  }
  void write(const std::string& file, const std::string& contents) const {
    std::filesystem::create_directories((path / file).parent_path());
    std::ofstream(path / file, std::ios::binary) << contents;
  }
};

// Loads everything requested so far: wait for the workers, then finalize all.
void finishAll(AssetManager& assets) {
  assets.waitForDecodes();
  while (assets.update(1000) > 0) {
  }
}

}  // namespace

TEST_CASE("a file loads in the background and becomes ready after update()") {
  TempFolder folder("basic");
  folder.write("hello.txt", "hello, world");
  JobSystem jobs(2);
  AssetManager assets(jobs, folder.path);
  assets.registerType<TextAsset>(engine::textAssetLoader());

  const AssetHandle<TextAsset> handle = assets.load<TextAsset>("hello.txt");
  REQUIRE(handle.valid());
  CHECK(assets.state(handle) == AssetState::Loading);
  CHECK(assets.get(handle) == nullptr);  // not yet

  assets.waitForDecodes();
  CHECK(assets.state(handle) == AssetState::Loading);  // decoded, but not finalized until update()
  CHECK(assets.update() == 1);
  CHECK(assets.state(handle) == AssetState::Ready);
  REQUIRE(assets.get(handle) != nullptr);
  CHECK(assets.get(handle)->text == "hello, world");
  CHECK(assets.idle());
}

TEST_CASE("the same file is loaded once: same path, same handle") {
  TempFolder folder("cache");
  folder.write("data/a.txt", "A");
  JobSystem jobs(2);
  AssetManager assets(jobs, folder.path);
  assets.registerType<TextAsset>(engine::textAssetLoader());

  const auto first = assets.load<TextAsset>("data/a.txt");
  const auto again = assets.load<TextAsset>("data/./a.txt");  // another spelling of the same file
  CHECK(first == again);
  finishAll(assets);
  CHECK(assets.load<TextAsset>("data/a.txt") == first);  // still cached once ready
}

TEST_CASE("a missing file fails with a message naming it") {
  TempFolder folder("missing");
  JobSystem jobs(2);
  AssetManager assets(jobs, folder.path);
  assets.registerType<TextAsset>(engine::textAssetLoader());

  const auto handle = assets.load<TextAsset>("nope/not-here.txt");
  finishAll(assets);
  CHECK(assets.state(handle) == AssetState::Failed);
  CHECK(assets.get(handle) == nullptr);
  REQUIRE(assets.error(handle) != nullptr);
  CHECK(assets.error(handle)->code == Errc::IoFailure);
  CHECK(assets.error(handle)->message.find("not-here.txt") != std::string::npos);
}

namespace {
struct Number {
  int value = 0;
};
AssetLoader<Number, int> numberLoader() {
  return {
      .decode = [](std::span<const std::byte> file, const std::filesystem::path&) -> Result<int> {
        const std::string text(reinterpret_cast<const char*>(file.data()), file.size());
        try {
          return std::stoi(text);
        } catch (...) {
          return fail(Errc::InvalidArgument, "'{}' is not a number", text);
        }
      },
      .finalize = [](int&& n, const std::filesystem::path&) -> Result<Number> { return Number{n}; },
  };
}
}  // namespace

TEST_CASE("bad data fails with the decoder's message; types are kept apart") {
  TempFolder folder("decode");
  folder.write("good.num", "42");
  folder.write("bad.num", "forty-two");
  JobSystem jobs(2);
  AssetManager assets(jobs, folder.path);
  assets.registerType<Number>(numberLoader());
  assets.registerType<TextAsset>(engine::textAssetLoader());

  const auto good = assets.load<Number>("good.num");
  const auto bad = assets.load<Number>("bad.num");
  const auto asText = assets.load<TextAsset>("good.num");  // the same file as another type
  finishAll(assets);

  REQUIRE(assets.get(good) != nullptr);
  CHECK(assets.get(good)->value == 42);
  CHECK(assets.state(bad) == AssetState::Failed);
  REQUIRE(assets.error(bad) != nullptr);
  CHECK(assets.error(bad)->message == "'forty-two' is not a number");
  REQUIRE(assets.get(asText) != nullptr);
  CHECK(assets.get(asText)->text == "42");
}

TEST_CASE("in normal frames, decode runs on workers and finalize on the main thread") {
  TempFolder folder("threads");
  for (int i = 0; i < 20; ++i) folder.write(std::format("f{}.txt", i), "x");
  JobSystem jobs(3);
  AssetManager assets(jobs, folder.path);

  std::mutex idsMutex;
  std::set<std::thread::id> decodeThreads;
  std::set<std::thread::id> finalizeThreads;
  assets.registerType<TextAsset>(AssetLoader<TextAsset, std::string>{
      .decode = [&](std::span<const std::byte> file, const std::filesystem::path&) -> Result<std::string> {
        const std::scoped_lock lock(idsMutex);
        decodeThreads.insert(std::this_thread::get_id());
        return std::string(reinterpret_cast<const char*>(file.data()), file.size());
      },
      .finalize = [&](std::string&& text, const std::filesystem::path&) -> Result<TextAsset> {
        finalizeThreads.insert(std::this_thread::get_id());  // main thread only: no lock needed
        return TextAsset{std::move(text)};
      },
  });
  for (int i = 0; i < 20; ++i) (void)assets.load<TextAsset>(std::format("f{}.txt", i));
  // Frames: only update(), never waitForDecodes() (a waiting thread helps
  // decode; a frame does not wait).
  while (!assets.idle()) {
    (void)assets.update();
    std::this_thread::yield();
  }

  CHECK(decodeThreads.count(std::this_thread::get_id()) == 0);  // never decoded here
  REQUIRE(finalizeThreads.size() == 1);
  CHECK(*finalizeThreads.begin() == std::this_thread::get_id());  // always finalized here
}

TEST_CASE("update() finalizes at most `budget` assets per call") {
  TempFolder folder("budget");
  for (int i = 0; i < 10; ++i) folder.write(std::format("f{}.txt", i), "x");
  JobSystem jobs(2);
  AssetManager assets(jobs, folder.path);
  assets.registerType<TextAsset>(engine::textAssetLoader());
  std::vector<AssetHandle<TextAsset>> handles;
  for (int i = 0; i < 10; ++i) handles.push_back(assets.load<TextAsset>(std::format("f{}.txt", i)));

  assets.waitForDecodes();
  CHECK(assets.waitingToFinalize() == 10);
  CHECK(assets.update(3) == 3);  // a frame's worth
  int ready = 0;
  for (const auto& h : handles) ready += assets.state(h) == AssetState::Ready ? 1 : 0;
  CHECK(ready == 3);
  CHECK(assets.update(100) == 7);  // the rest
  CHECK(assets.idle());
}

TEST_CASE("unloading a file that is still loading cancels it: never finalized") {
  TempFolder folder("cancel");
  folder.write("big.txt", std::string(1 << 20, 'z'));
  JobSystem jobs(2);
  AssetManager assets(jobs, folder.path);
  std::atomic<int> finalized{0};
  auto loader = engine::textAssetLoader();
  auto innerFinalize = loader.finalize;
  loader.finalize = [&](std::string&& text, const std::filesystem::path& p) {
    ++finalized;
    return innerFinalize(std::move(text), p);
  };
  assets.registerType<TextAsset>(std::move(loader));

  const auto handle = assets.load<TextAsset>("big.txt");
  assets.unload(handle);  // before it is finished
  CHECK(assets.state(handle) == AssetState::Invalid);
  finishAll(assets);
  CHECK(finalized.load() == 0);  // its result was thrown away, not uploaded
  CHECK(assets.get(handle) == nullptr);

  const auto reloaded = assets.load<TextAsset>("big.txt");  // a fresh load works
  CHECK(reloaded != handle);
  finishAll(assets);
  CHECK(assets.state(reloaded) == AssetState::Ready);
  CHECK(finalized.load() == 1);
}

TEST_CASE("after unload, old handles find nothing even when the slot is reused") {
  TempFolder folder("stale");
  folder.write("one.txt", "one");
  folder.write("two.txt", "two");
  JobSystem jobs(2);
  AssetManager assets(jobs, folder.path);
  assets.registerType<TextAsset>(engine::textAssetLoader());

  const auto one = assets.load<TextAsset>("one.txt");
  finishAll(assets);
  assets.unload(one);
  const auto two = assets.load<TextAsset>("two.txt");
  finishAll(assets);
  CHECK(two.index == one.index);  // the slot was reused...
  CHECK(assets.get(one) == nullptr);  // ...but the old handle does not see the new asset
  CHECK(assets.state(one) == AssetState::Invalid);
  REQUIRE(assets.get(two) != nullptr);
  CHECK(assets.get(two)->text == "two");
}

TEST_CASE("hundreds of files load concurrently, each with the right contents") {
  TempFolder folder("stress");
  constexpr int kFiles = 300;
  for (int i = 0; i < kFiles; ++i) folder.write(std::format("dir{}/file{}.txt", i % 7, i), std::format("contents {}", i));
  JobSystem jobs(4);
  AssetManager assets(jobs, folder.path);
  assets.registerType<TextAsset>(engine::textAssetLoader());

  std::vector<AssetHandle<TextAsset>> handles;
  for (int i = 0; i < kFiles; ++i) handles.push_back(assets.load<TextAsset>(std::format("dir{}/file{}.txt", i % 7, i)));
  // Finalize as results arrive, a few per "frame", the way the game does.
  while (!assets.idle()) {
    (void)assets.update(16);
    std::this_thread::yield();
  }
  for (int i = 0; i < kFiles; ++i) {
    const TextAsset* text = assets.get(handles[static_cast<usize>(i)]);
    REQUIRE(text != nullptr);
    CHECK(text->text == std::format("contents {}", i));
  }
}

TEST_CASE("destroying the manager while files are loading is safe") {
  TempFolder folder("shutdown");
  for (int i = 0; i < 50; ++i) folder.write(std::format("f{}.txt", i), std::string(4096, 'q'));
  JobSystem jobs(4);
  {
    AssetManager assets(jobs, folder.path);
    assets.registerType<TextAsset>(engine::textAssetLoader());
    for (int i = 0; i < 50; ++i) (void)assets.load<TextAsset>(std::format("f{}.txt", i));
    // No waiting: the destructor must wait for the jobs that refer to it.
  }
  JobCounter probe;  // and the job system is still healthy
  std::atomic<int> ran{0};
  jobs.run([&] { ++ran; }, probe);
  probe.wait();
  CHECK(ran.load() == 1);
}
