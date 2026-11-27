// Tests for the JobSystem: parallel loops, futures, counters, nested waits,
// concurrent submission. Run in the normal build, under ASan + UBSan, and
// under ThreadSanitizer (the `tsan` preset), which reports any data race.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "playground/core/JobSystem.h"

#include <atomic>
#include <numeric>
#include <set>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace playground;

TEST_CASE("the worker count defaults to the hardware, and can be set") {
  JobSystem automatic;
  CHECK(automatic.workerCount() >= 1);
  JobSystem three(3);
  CHECK(three.workerCount() == 3);
  CHECK_FALSE(three.isWorkerThread());  // the test's own thread is not a worker
}

TEST_CASE("parallelFor visits every index exactly once") {
  JobSystem jobs(4);
  for (const usize count : {usize{0}, usize{1}, usize{7}, usize{1000}, usize{100'003}}) {
    for (const usize batch : {usize{1}, usize{64}, usize{5000}}) {
      std::vector<std::atomic<int>> visits(count);
      jobs.parallelFor(count, batch, [&](usize begin, usize end) {
        for (usize i = begin; i < end; ++i) visits[i].fetch_add(1, std::memory_order_relaxed);
      });
      for (usize i = 0; i < count; ++i) {
        if (visits[i].load() != 1) {
          FAIL("index ", i, " visited ", visits[i].load(), " times (count ", count, ", batch ", batch, ")");
        }
      }
    }
  }
}

TEST_CASE("parallelFor really uses several threads, and the result matches a plain loop") {
  JobSystem jobs(4);
  constexpr usize kCount = 2'000'000;
  std::vector<u64> squares(kCount);
  std::mutex idsMutex;
  std::set<std::thread::id> threads;
  jobs.parallelFor(kCount, 10'000, [&](usize begin, usize end) {
    {
      const std::scoped_lock lock(idsMutex);
      threads.insert(std::this_thread::get_id());
    }
    for (usize i = begin; i < end; ++i) squares[i] = u64{i} * i;  // each batch writes its own slots
  });
  u64 parallelSum = 0;
  for (const u64 v : squares) parallelSum += v;
  u64 plainSum = 0;
  for (usize i = 0; i < kCount; ++i) plainSum += u64{i} * i;
  CHECK(parallelSum == plainSum);
  CHECK(threads.size() > 1);  // 200 batches on 4 workers + the caller
}

TEST_CASE("parallelFor rethrows a batch's exception on the calling thread") {
  JobSystem jobs(3);
  CHECK_THROWS_AS(jobs.parallelFor(1000, 10,
                                   [](usize begin, usize) {
                                     if (begin == 500) throw std::runtime_error("batch 50 failed");
                                   }),
                  std::runtime_error);
  // And the system still works afterwards.
  std::atomic<int> after{0};
  jobs.parallelFor(100, 10, [&](usize b, usize e) { after += static_cast<int>(e - b); });
  CHECK(after == 100);
}

TEST_CASE("submit returns a future with the result, or the exception") {
  JobSystem jobs(2);
  auto answer = jobs.submit([] { return 6 * 7; });
  auto text = jobs.submit([] { return std::string("from a worker"); });
  auto failing = jobs.submit([]() -> int { throw std::logic_error("no"); });
  CHECK(answer.get() == 42);
  CHECK(text.get() == "from a worker");
  CHECK_THROWS_AS(failing.get(), std::logic_error);
}

TEST_CASE("a counter waits for a whole group of jobs") {
  JobSystem jobs(4);
  JobCounter group;
  std::atomic<int> done{0};
  for (int i = 0; i < 1000; ++i) jobs.run([&] { done.fetch_add(1, std::memory_order_relaxed); }, group);
  group.wait();
  CHECK(group.done());
  CHECK(done.load() == 1000);
}

TEST_CASE("jobs can wait on nested work without deadlocking, even with one worker") {
  // Every outer job runs a parallelFor and waits for it. With one worker,
  // the only way this finishes is if waiting threads run queued jobs.
  JobSystem jobs(1);
  JobCounter outer;
  std::atomic<int> inner{0};
  for (int i = 0; i < 8; ++i) {
    jobs.run(
        [&] {
          jobs.parallelFor(100, 10, [&](usize b, usize e) { inner.fetch_add(static_cast<int>(e - b)); });
        },
        outer);
  }
  outer.wait();
  CHECK(inner.load() == 800);
}

TEST_CASE("many threads can submit at once") {
  JobSystem jobs(4);
  JobCounter group;
  std::atomic<int> done{0};
  std::vector<std::thread> submitters;
  for (int t = 0; t < 4; ++t) {
    submitters.emplace_back([&] {
      for (int i = 0; i < 500; ++i) jobs.run([&] { done.fetch_add(1, std::memory_order_relaxed); }, group);
    });
  }
  for (std::thread& t : submitters) t.join();
  group.wait();
  CHECK(done.load() == 2000);
}

TEST_CASE("the destructor finishes every queued job before returning") {
  std::atomic<int> ran{0};
  {
    JobSystem jobs(2);
    JobCounter group;
    for (int i = 0; i < 200; ++i) {
      jobs.run(
          [&] {
            std::this_thread::sleep_for(std::chrono::microseconds(50));
            ran.fetch_add(1);
          },
          group);
    }
    // No wait: the destructor must drain the queue.
  }
  CHECK(ran.load() == 200);
}

TEST_CASE("a waiting counter can be destroyed the moment it is done (no use after free)") {
  // Many short-lived counters on the stack, each waited on and dropped at
  // once: if a job touched its counter after the final decrement, ASan or
  // TSan would report it here.
  JobSystem jobs(4);
  for (int round = 0; round < 2000; ++round) {
    JobCounter group;
    int value = 0;
    jobs.run([&] { value = round; }, group);
    group.wait();
    REQUIRE(value == round);  // the write is visible after wait
  }
}
