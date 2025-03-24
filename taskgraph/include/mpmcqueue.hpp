#pragma once

#include <cassert>
#include <deque>
#include <mutex>
#include <condition_variable>
#include <optional>

namespace tg {

  // Unbounded MPMC Blocking Queue
  template <typename T>
  class UnboundedMPMCBlockingQueue {
  public:
    void Push(T value) {
      std::lock_guard lock(mutex_);
      if (closed_) {
        return;
      }
      buffer_.push_back(std::move(value));
      not_empty_.notify_one();
    }

    std::optional<T> Pop() {
      std::unique_lock lock(mutex_);

      if (closed_ && buffer_.empty()) {
        return std::nullopt;
      }

      while (buffer_.empty()) {
        if (closed_) {
          return std::nullopt;
        }
        not_empty_.wait(lock);
      } 
      
      return PopLocked();
    }

    void Close() {
      std::unique_lock lock(mutex_);
      closed_ = true;
      lock.unlock();
      not_empty_.notify_all();
    }


  private:
    T PopLocked() {
      assert(!buffer_.empty());
      T front{std::move(buffer_.front())};
      buffer_.pop_front();
      return front;
    }

  private:
    std::deque<T> buffer_{};
    std::mutex mutex_;
    std::condition_variable not_empty_;
    bool closed_{false};
  };

} // namespace tg
