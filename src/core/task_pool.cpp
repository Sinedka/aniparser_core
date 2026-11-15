#include <algorithm>
#include <iostream>

#include "core.hpp"

TaskPool::TaskPool(std::size_t workers, std::size_t queue_limit)
  : queue_limit_(std::max<std::size_t>(queue_limit, 1)) {
  workers = std::max<std::size_t>(workers, 1);
  workers_.reserve(workers);
  try {
    for (std::size_t i = 0; i < workers; ++i)
      workers_.emplace_back([this] { worker_loop(); });
    std::cerr << "[api queue] started pool=" << this
              << " workers=" << workers_.size() << " capacity=" << queue_limit_
              << '\n';
  } catch (...) {
    {
      std::lock_guard lock(mutex_);
      stopping_ = true;
    }
    condition_.notify_all();
    for (auto &worker : workers_)
      if (worker.joinable()) worker.join();
    throw;
  }
}

TaskPool::~TaskPool() { shutdown(); }

bool TaskPool::submit(std::function<void()> task) {
  if (!task) return false;
  {
    std::unique_lock lock(mutex_);
    std::cerr << "[api queue] submit pool=" << this << " stopping=" << stopping_
              << " queued=" << queue_.size() << '\n';
    if (stopping_ || queue_.size() >= queue_limit_) return false;
    queue_.push_back(std::move(task));
    std::cerr << "[api queue] accepted task pool=" << this
              << " queued=" << queue_.size() << '\n';
  }
  condition_.notify_one();
  return true;
}

void TaskPool::shutdown(bool discard_pending) {
  std::deque<std::function<void()>> discarded;
  {
    std::lock_guard lock(mutex_);
    if (stopping_) return;
    stopping_ = true;
    if (discard_pending) discarded.swap(queue_);
  }
  discarded.clear(); // Destroy task captures outside the queue mutex.
  std::cerr << "[api queue] shutdown requested pool=" << this << '\n';
  condition_.notify_all();
  for (auto &worker : workers_)
    if (worker.joinable()) worker.join();
  workers_.clear();
}

void TaskPool::worker_loop() {
  for (;;) {
    std::function<void()> task;
    {
      std::unique_lock lock(mutex_);
      condition_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
      if (stopping_ && queue_.empty()) return;
      task = std::move(queue_.front());
      queue_.pop_front();
    }
    try {
      task();
    } catch (...) {
      // A failed task must not terminate a worker or prevent later requests.
    }
  }
}
