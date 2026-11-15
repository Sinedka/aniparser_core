#pragma once

#include <deque>
#include <functional>
#include <mutex>

// Register updates before producing an initial snapshot, without allowing a
// fast existing refresh to deliver its update ahead of that snapshot.
// No callback runs under this mutex; reentrant calls append to the same FIFO.
class CallbackQueue {
  public:
  void initial(std::function<void()> callback) {
    {
      std::lock_guard lock(mutex_);
      callbacks_.push_front(std::move(callback));
      ready_ = true;
    }
    drain();
  }
  void post(std::function<void()> callback) {
    {
      std::lock_guard lock(mutex_);
      callbacks_.push_back(std::move(callback));
    }
    drain();
  }

  private:
  void drain() {
    std::unique_lock lock(mutex_);
    if (!ready_ || draining_) return;
    draining_ = true;
    while (!callbacks_.empty()) {
      auto callback = std::move(callbacks_.front());
      callbacks_.pop_front();
      lock.unlock();
      try {
        callback();
      } catch (...) {}
      lock.lock();
    }
    draining_ = false;
  }
  std::mutex mutex_;
  std::deque<std::function<void()>> callbacks_;
  bool ready_ = false;
  bool draining_ = false;
};
