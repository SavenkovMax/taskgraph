#pragma once

#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <new>
#include <optional>
#include <type_traits>

namespace tg {

#ifdef __cpp_lib_hardware_interference_size
inline constexpr size_t kCacheLineSize =
    std::hardware_destructive_interference_size;
#else
inline constexpr size_t kCacheLineSize = 64;
#endif

// Lock-free single-producer multiple-consumer (SPMC) queue.
// Only the queue owner can perform Pop and Push operations where the queue behaves like a stack.
// Other can only steal data from the queue, they see a FIFO queue. All threads must have finished using
// the queue before it is destructed.
template <typename T, size_t Cap>
class BoundedTaskQueue {

  static_assert(std::is_pointer_v<T>, "T must be a pointer type");
  static_assert(Cap && (!(Cap & (Cap - 1))), "Capacity must be a power of 2.");

 public:
  // Constructs the queue with a given capacity, a capacity must be a power of 2.
  explicit BoundedTaskQueue() = default;

  // Move/Copy is not supported
  BoundedTaskQueue(const BoundedTaskQueue&) = delete;
  BoundedTaskQueue& operator=(const BoundedTaskQueue&) = delete;

  // Query the size at an instance of call
  size_t Size() const noexcept;

  // Returns the queue capacity
  constexpr size_t Capacity() const noexcept;

  // Checks whether the queue is empty at an instance of call
  bool Empty() const noexcept;

  // Pushes an item to the queue. Only the owner thread can insert an item to the queue.
  bool TryPush(T item) noexcept;

  // Pops out an item from the queue. Only the owner thread can pop out an item from the queue.
  // The return can be a std::nullopt if this operation fails (empty deque).
  std::optional<T> Pop() noexcept;

  // Tries to steal an item from the queue. Any threads can try to steal an item from the queue.
  // The return can be std::nullopt if this operation failed (not necessarily empty).
  std::optional<T> TrySteal() noexcept;

 private:
  constexpr static std::int32_t buffer_mask_{Cap - 1};

  alignas(kCacheLineSize) std::atomic<std::int32_t> top_{0};
  alignas(kCacheLineSize) std::atomic<std::int32_t> bottom_{0};
  alignas(kCacheLineSize) std::atomic<T> buffer_[Cap];
};

template <typename T, size_t Cap>
size_t BoundedTaskQueue<T, Cap>::Size() const noexcept {
  auto b = bottom_.load(std::memory_order_relaxed);
  auto t = top_.load(std::memory_order_relaxed);
  return static_cast<size_t>(b >= t ? b - t : 0);
}

template <typename T, size_t Cap>
constexpr size_t BoundedTaskQueue<T, Cap>::Capacity() const noexcept {
  return Cap;
}

template <typename T, size_t Cap>
bool BoundedTaskQueue<T, Cap>::Empty() const noexcept {
  return !Size();
}

template <typename T, size_t Cap>
bool BoundedTaskQueue<T, Cap>::TryPush(T item) noexcept {
  auto b = bottom_.load(std::memory_order_relaxed);
  auto t = top_.load(std::memory_order_relaxed);

  if (Cap < static_cast<size_t>((b - t) + 1)) {
    // The queue is full, try push operation fails.
    return false;
  }

  buffer_[b & buffer_mask_].store(item, std::memory_order_relaxed);

  std::atomic_thread_fence(std::memory_order_release);
  bottom_.store(b + 1, std::memory_order_relaxed);
  return true;
}

template <typename T, size_t Cap>
std::optional<T> BoundedTaskQueue<T, Cap>::Pop() noexcept {
  auto b = bottom_.load(std::memory_order_relaxed) - 1;

  bottom_.store(b, std::memory_order_relaxed);  // stealers can no longer steal

  std::atomic_thread_fence(std::memory_order_seq_cst);
  auto t = top_.load(std::memory_order_relaxed);

  if (t <= b) {
    // Non-empty queue
    if (t == b) {
      // The last item could get stolen, by a stealer that loaded bottom before our write above
      if (!top_.compare_exchange_strong(t, t + 1, std::memory_order_seq_cst,
                                        std::memory_order_relaxed)) {
        // Failed race, thief got the last item.
        bottom_.store(b + 1, std::memory_order_relaxed);
        return std::nullopt;
      }
      bottom_.store(b + 1, std::memory_order_relaxed);
    }
    // Can delay load until after acquiring slot as only this thread can Push, this load is not
    // required to be atomic as we are the exclusive writer
    return buffer_[b & buffer_mask_].load(std::memory_order_relaxed);
  } else {
    bottom_.store(b + 1, std::memory_order_relaxed);
    return std::nullopt;
  }
}

template <typename T, size_t Cap>
std::optional<T> BoundedTaskQueue<T, Cap>::TrySteal() noexcept {
  auto t = top_.load(std::memory_order_acquire);
  std::atomic_thread_fence(std::memory_order_seq_cst);
  auto b = bottom_.load(std::memory_order_acquire);

  if (t < b) {
    // Must load *before* acquiring the slot as slot may be overwritten immediately after acquiring.
    // This load is NOT required to be atomic even-though it may race with an overrite as we only
    // return the value if we win the race below garanteeing we had no race during our read. If we
    // loose the race then 'value' could be corrupt due to read-during-write race but as T is trivially
    // destructible this does not matter.

    T value = buffer_[t & buffer_mask_].load(std::memory_order_consume);
    if (!top_.compare_exchange_strong(t, t + 1, std::memory_order_seq_cst,
                                      std::memory_order_relaxed)) {
      // Failed race.
      return std::nullopt;
    }
    return value;
  } else {
    // The deque is empty.
    return std::nullopt;
  }
}

}  // namespace tg
