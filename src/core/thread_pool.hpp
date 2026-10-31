// Bounded fork-join worker pool (Task D). A fixed number of std::threads is created once and
// reused; thread creation is therefore not part of the timed compute region.
#pragma once
#include <condition_variable>
#include <exception>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace wb {

class ThreadPool {
 public:
  explicit ThreadPool(int workers);  // workers >= 1
  ~ThreadPool();
  ThreadPool(const ThreadPool&) = delete;
  ThreadPool& operator=(const ThreadPool&) = delete;

  int workers() const { return static_cast<int>(threads_.size()); }

  // Runs task(workerIndex) once on every worker and blocks until all have finished.
  // If any task throws, the first exception is rethrown here after all workers finish.
  void runOnAll(const std::function<void(int)>& task);

 private:
  void loop(int index);

  std::vector<std::thread> threads_;
  std::mutex m_;
  std::condition_variable cvStart_, cvDone_;
  const std::function<void(int)>* task_ = nullptr;
  unsigned long long generation_ = 0;
  int remaining_ = 0;
  bool stop_ = false;
  std::exception_ptr error_;
};

}  // namespace wb
