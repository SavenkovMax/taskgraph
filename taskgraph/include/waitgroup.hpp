#pragma once

#include <condition_variable>
#include <mutex>

namespace tg {

class WaitGroup {
 public:
  void Add(size_t count) {
    std::lock_guard lock(mutex_);
    counter_ += count;
  }

  void Done() {
    std::unique_lock lock(mutex_);
    if (--counter_ == 0) {
      has_work_.notify_all();
    }
  }

  void Wait() {
    std::unique_lock lock(mutex_);

    while (counter_ > 0) {
      has_work_.wait(lock);
    }
  }

 private:
  size_t counter_{};
  std::mutex mutex_;
  std::condition_variable has_work_;
};

}  // namespace tg
