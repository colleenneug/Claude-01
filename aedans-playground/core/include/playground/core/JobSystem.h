#pragma once
// =============================================================================
//  playground/core/JobSystem.h — running work on all CPU cores.
//
//  A fixed pool of worker threads, created once, taking jobs from one shared
//  queue. Creating a thread per task costs far more than most tasks; a pool
//  pays that once.
//
//  THREE WAYS TO USE IT
//
//    auto future = jobs.submit([] { return decode(file); });   // one task, a result later
//    ... future.get() ...                                        // (rethrows its exception)
//
//    JobCounter done;                                            // a group of tasks
//    for (auto& chunk : chunks) jobs.run([&] { work(chunk); }, done);
//    done.wait();                                                // until all have finished
//
//    jobs.parallelFor(count, 1024, [&](usize begin, usize end) { // split a loop
//      for (usize i = begin; i < end; ++i) process(i);
//    });                                                         // returns when all are done
//
//  WAITING HELPS
//    JobCounter::wait() (and so parallelFor) does not just sleep: while the
//    group is unfinished it takes jobs off the queue and runs them. So a job
//    may itself call parallelFor or wait on a counter without deadlocking,
//    even when every worker is busy waiting: someone is always running jobs.
//
//  RULES FOR JOBS
//    - Jobs run on any thread, in any order, at the same time as each other:
//      anything two jobs share must be read-only or synchronised.
//    - Exceptions are allowed: submit() delivers them through the future;
//      parallelFor rethrows the first one on the calling thread; run() logs
//      and drops them (use submit when the result matters).
//    - The destructor finishes every job already queued, then joins the
//      workers. Destroy the JobSystem after everything that submits to it.
//
//  One queue with a lock is the simplest correct design. Per-worker queues
//  with work stealing scale better past ~16 cores and many tiny jobs; switch
//  only when a profile shows the lock is the bottleneck.
// =============================================================================

#include "playground/core/Types.h"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <exception>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace playground {

class JobSystem;

// Counts unfinished jobs in a group. wait() returns when it reaches zero.
class JobCounter {
 public:
  JobCounter() = default;
  JobCounter(const JobCounter&) = delete;
  JobCounter& operator=(const JobCounter&) = delete;

  [[nodiscard]] bool done() const noexcept { return pending_.load(std::memory_order_acquire) == 0; }
  [[nodiscard]] u32 pending() const noexcept { return pending_.load(std::memory_order_acquire); }

  // Runs other queued jobs until the group is done (see WAITING HELPS).
  void wait();

 private:
  friend class JobSystem;
  void add(u32 n) noexcept { pending_.fetch_add(n, std::memory_order_relaxed); }

  std::atomic<u32> pending_{0};
  // The system whose queue wait() helps with. Atomic: run() may set it from
  // several threads at once (always to the same value, but that is still a
  // data race for a plain pointer).
  std::atomic<JobSystem*> system_{nullptr};
};

class JobSystem {
 public:
  // `workers` = 0 picks one per hardware thread, minus one for the main
  // thread, and at least one.
  explicit JobSystem(u32 workers = 0);
  ~JobSystem();
  JobSystem(const JobSystem&) = delete;
  JobSystem& operator=(const JobSystem&) = delete;

  [[nodiscard]] u32 workerCount() const noexcept { return static_cast<u32>(workers_.size()); }
  // True on one of this system's worker threads.
  [[nodiscard]] bool isWorkerThread() const noexcept;

  // Runs `fn` on a worker; the future holds its result or its exception.
  template <class Fn>
  [[nodiscard]] auto submit(Fn&& fn) -> std::future<std::invoke_result_t<std::decay_t<Fn>>> {
    using R = std::invoke_result_t<std::decay_t<Fn>>;
    // packaged_task is move-only and std::function needs copies, so it is
    // held by a shared_ptr.
    auto task = std::make_shared<std::packaged_task<R()>>(std::forward<Fn>(fn));
    std::future<R> future = task->get_future();
    push([task] { (*task)(); });
    return future;
  }

  // Runs `fn` on a worker as part of `counter`'s group.
  void run(std::function<void()> fn, JobCounter& counter);

  // Calls fn(begin, end) over [0, count) in batches of about `batchSize`,
  // spread across the workers and the calling thread. Returns when every
  // batch has run; rethrows the first exception one threw.
  template <class Fn>
  void parallelFor(usize count, usize batchSize, Fn&& fn);

  // Runs one queued job on the calling thread, if there is one. Returns
  // whether it did. (What JobCounter::wait does while it waits.)
  bool runOne();

 private:
  friend class JobCounter;

  void push(std::function<void()> job);
  void workerLoop();
  // Called after a counted job's final decrement. Wakes every wait()er.
  void signalCompletion() noexcept;

  // Bumped after every counted job finishes. Waiters sleep on THIS, not on
  // the counter: once a job's decrement may have reached zero, the waiter
  // may already have returned and destroyed the counter, so nothing may
  // touch the counter after it (not even to notify). The JobSystem outlives
  // every counter, so notifying here is always safe.
  std::atomic<u64> completions_{0};

  std::vector<std::thread> workers_;
  std::deque<std::function<void()>> queue_;
  mutable std::mutex mutex_;
  std::condition_variable wake_;
  bool stopping_ = false;
};

// --- parallelFor ----------------------------------------------------------------

template <class Fn>
void JobSystem::parallelFor(usize count, usize batchSize, Fn&& fn) {
  if (count == 0) return;
  const usize batch = batchSize == 0 ? 1 : batchSize;
  const usize batches = (count + batch - 1) / batch;

  // One batch: no point in another thread.
  if (batches == 1) {
    fn(usize{0}, count);
    return;
  }

  std::exception_ptr firstError;
  std::mutex errorMutex;
  JobCounter counter;
  for (usize b = 1; b < batches; ++b) {  // batch 0 runs here, below
    const usize begin = b * batch;
    const usize end = begin + batch < count ? begin + batch : count;
    run(
        [&fn, &firstError, &errorMutex, begin, end] {
          try {
            fn(begin, end);
          } catch (...) {
            const std::scoped_lock lock(errorMutex);
            if (!firstError) firstError = std::current_exception();
          }
        },
        counter);
  }
  try {
    fn(usize{0}, batch < count ? batch : count);  // the caller does a share too
  } catch (...) {
    const std::scoped_lock lock(errorMutex);
    if (!firstError) firstError = std::current_exception();
  }
  counter.wait();  // every job references locals above: they must all finish first
  if (firstError) std::rethrow_exception(firstError);
}

}  // namespace playground
