#include "playground/core/JobSystem.h"

#include "playground/core/Assert.h"
#include "playground/core/Log.h"

#include <algorithm>
#include <exception>

namespace playground {
namespace {

// Set for the lifetime of each worker thread: which system it belongs to.
thread_local const JobSystem* tWorkerOf = nullptr;

}  // namespace

// --- JobCounter ---------------------------------------------------------------

void JobCounter::wait() {
  while (!done()) {
    JobSystem* system = system_.load(std::memory_order_acquire);
    if (system == nullptr) {  // jobs added but the system not yet visible: cannot last
      std::this_thread::yield();
      continue;
    }
    // Help: run someone's job rather than sleep.
    if (system->runOne()) continue;
    // Nothing queued: the rest of this group is running on other threads.
    // Sleep until some job finishes. Read the completion count BEFORE
    // checking ours, so a finish between the two still wakes us. This cannot
    // deadlock: a thread only blocks once the queue is empty, so every
    // unfinished job is already running somewhere, and threads that queue
    // more work help with it themselves.
    const u64 seen = system->completions_.load(std::memory_order_acquire);
    if (done()) return;
    system->completions_.wait(seen, std::memory_order_acquire);
  }
}

// --- JobSystem ------------------------------------------------------------------

JobSystem::JobSystem(u32 workers) {
  u32 count = workers;
  if (count == 0) {
    const u32 hardware = std::thread::hardware_concurrency();  // 0 if unknown
    count = hardware > 1 ? hardware - 1 : 1;                   // leave one for the main thread
  }
  workers_.reserve(count);
  for (u32 i = 0; i < count; ++i) workers_.emplace_back([this] { workerLoop(); });
  log::info(log::Category::Core, "job system: {} worker thread(s)", count);
}

JobSystem::~JobSystem() {
  {
    const std::scoped_lock lock(mutex_);
    stopping_ = true;
  }
  wake_.notify_all();
  for (std::thread& worker : workers_) worker.join();  // each drains the queue before it exits
}

bool JobSystem::isWorkerThread() const noexcept { return tWorkerOf == this; }

void JobSystem::push(std::function<void()> job) {
  {
    const std::scoped_lock lock(mutex_);
    PG_VERIFY(!stopping_, "job submitted to a JobSystem that is shutting down");
    queue_.push_back(std::move(job));
  }
  wake_.notify_one();
}

void JobSystem::run(std::function<void()> fn, JobCounter& counter) {
  counter.system_.store(this, std::memory_order_release);
  counter.add(1);
  push([this, fn = std::move(fn), &counter] {
    try {
      fn();
    } catch (const std::exception& e) {
      log::error(log::Category::Core, "a job threw and the exception was dropped: {}", e.what());
    } catch (...) {
      log::error(log::Category::Core, "a job threw a non-standard exception; dropped");
    }
    // The LAST touch of the counter: once this may have reached zero, its
    // owner can return from wait() and destroy it.
    counter.pending_.fetch_sub(1, std::memory_order_acq_rel);
    signalCompletion();
  });
}

void JobSystem::signalCompletion() noexcept {
  completions_.fetch_add(1, std::memory_order_acq_rel);
  completions_.notify_all();
}

bool JobSystem::runOne() {
  std::function<void()> job;
  {
    const std::scoped_lock lock(mutex_);
    if (queue_.empty()) return false;
    job = std::move(queue_.front());
    queue_.pop_front();
  }
  job();
  return true;
}

void JobSystem::workerLoop() {
  tWorkerOf = this;
  for (;;) {
    std::function<void()> job;
    {
      std::unique_lock lock(mutex_);
      wake_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
      if (queue_.empty()) return;  // stopping, and nothing left to drain
      job = std::move(queue_.front());
      queue_.pop_front();
    }
    job();
  }
}

}  // namespace playground
