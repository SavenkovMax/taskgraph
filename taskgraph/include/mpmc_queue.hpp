#pragma once

#include <cassert>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <optional>

namespace tg {

// Unbounded MPMC Blocking Queue
template <typename T>
class UnboundedMPMCBlockingQueue {
 public:
  void Push(T value);

  void PushBatch(std::vector<T> batch);

  std::optional<T> Pop();

  std::optional<std::vector<T>> PopBatch(std::size_t workers);

  void Close();

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

template <typename T>
void UnboundedMPMCBlockingQueue<T>::Push(T value) {
  std::lock_guard lock(mutex_);
  if (closed_) {
    return;
  }
  buffer_.push_back(std::move(value));
  not_empty_.notify_one();
}

template <typename T>
void UnboundedMPMCBlockingQueue<T>::PushBatch(std::vector<T> batch) {

}

template <typename T>
std::optional<T> UnboundedMPMCBlockingQueue<T>::Pop() {
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

template <typename T>
std::optional<std::vector<T>> UnboundedMPMCBlockingQueue<T>::PopBatch(std::size_t workers) {
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

  std::size_t tasks = (buffer_.size() / workers) + 1;
  std::vector<T> batch;
  batch.reserve(tasks);
  for (std::size_t i = 0; i < tasks; ++i) {
    batch.push_back(std::move(PopLocked()));
  }

  return batch;
}

template <typename T>
void UnboundedMPMCBlockingQueue<T>::Close() {
  std::unique_lock lock(mutex_);
  closed_ = true;
  lock.unlock();
  not_empty_.notify_all();
}

}  // namespace tg
