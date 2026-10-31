#include "thread_pool.hpp"

#include <stdexcept>

namespace wb {

ThreadPool::ThreadPool(int workers) {
  if (workers < 1) throw std::invalid_argument("ThreadPool needs at least one worker");
  threads_.reserve(static_cast<std::size_t>(workers));
  for (int i = 0; i < workers; ++i) threads_.emplace_back([this, i] { loop(i); });
}

ThreadPool::~ThreadPool() {
  {
    std::lock_guard<std::mutex> lk(m_);
    stop_ = true;
  }
  cvStart_.notify_all();
  for (auto& t : threads_) t.join();
}

void ThreadPool::loop(int index) {
  unsigned long long seen = 0;
  while (true) {
    const std::function<void(int)>* task = nullptr;
    {
      std::unique_lock<std::mutex> lk(m_);
      cvStart_.wait(lk, [&] { return stop_ || generation_ != seen; });
      if (stop_) return;
      seen = generation_;
      task = task_;
    }
    try {
      (*task)(index);
    } catch (...) {
      std::lock_guard<std::mutex> lk(m_);
      if (!error_) error_ = std::current_exception();
    }
    {
      std::lock_guard<std::mutex> lk(m_);
      if (--remaining_ == 0) cvDone_.notify_one();
    }
  }
}

void ThreadPool::runOnAll(const std::function<void(int)>& task) {
  std::unique_lock<std::mutex> lk(m_);
  task_ = &task;
  remaining_ = static_cast<int>(threads_.size());
  error_ = nullptr;
  ++generation_;
  cvStart_.notify_all();
  cvDone_.wait(lk, [&] { return remaining_ == 0; });
  task_ = nullptr;
  if (error_) {
    std::exception_ptr e = error_;
    error_ = nullptr;
    std::rethrow_exception(e);
  }
}

}  // namespace wb
