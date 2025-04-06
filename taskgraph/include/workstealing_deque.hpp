#pragma once

#include <atomic>
#include <cassert>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

// The implementation of the deque described in the papers, "Correct and Efficient
// Work-Stealing for Weak Memory Models," and "Dynamic Circular Work-Stealing Deque".

namespace tg {

template <typename T>
concept Simple = std::default_initializable<T> && std::is_trivially_destructible_v<T>;

namespace detail {

// Class represents wrapper around a c-style array of atomic objects that provides modulo load/stores.
// Capacity must be a power of 2.
template <Simple T>
class RingBuffer {
 public:
  RingBuffer(std::int64_t capacity) : cap_(capacity), mask_(capacity - 1) {
    assert(capacity && (!(capacity & (capacity - 1))) && "Capacity must be a power of 2.");
  }

  std::int64_t Capacity() const noexcept { return cap_; }

  void Store(std::int64_t i, T&& val) noexcept requires std::is_nothrow_move_assignable_v<T> {
    buffer_[i & mask_] = std::move(val);
  }

  T Load(std::int64_t i) const noexcept requires std::is_nothrow_move_constructible_v<T> {
    return buffer_[i & mask_];
  }

  // Allocates and returns a new ring buffer, copies elements in range [b, t) into the new buffer
  RingBuffer<T>* Resize(std::int64_t b, std::int64_t t) const {
    auto buffer = new RingBuffer(2 * cap_);
    for (std::int64_t i = b; i < t; ++i) {
      buffer->Store(i, Load(i));
    }
    return buffer;
  }

 private:
  std::int64_t cap_;
  std::int64_t mask_;

  std::unique_ptr<T[]> buffer_ = std::make_unique_for_overwrite<T[]>(cap_);
};

}  // namespace detail

#ifdef __cpp_lib_hardware_interference_size
inline constexpr std::size_t kCacheLineSize = std::hardware_destructive_interference_size;
#else
inline constexpr std::size_t kCacheLineSize = 64;
#endif

// Lock-free single-producer multiple-consumer (SPMC) deque.
// Only the deque owner can perform Pop and Push operations where the deque behaves like a stack.
// Other can only steal data from the deque, they see a FIFO queue. All threads must have finished using
// the deque before it is destructed. T must be default initializable, trivially destructible and have
// nothrow move constructor/assignments operators.
template <Simple T>
class WorkStealingDeque {
 public:
  // Constructs the deque with a given capacity, the capacity must be a power of 2.
  explicit WorkStealingDeque(std::int64_t capacity = 1024);

  // Move/Copy is not supported
  WorkStealingDeque(const WorkStealingDeque&) = delete;
  WorkStealingDeque& operator=(const WorkStealingDeque&) = delete;

  // Query the size at an instance of call
  [[nodiscard]] std::size_t Size() const noexcept;

  // Query the capacity at an instance of call
  [[nodiscard]] std::int64_t Capacity() const noexcept;

  // Checks whether the deque is empty at an instance of call
  [[nodiscard]] bool Empty() const noexcept;

  // Emplaces an item to the deque. Only the owner thread can insert an item to the deque.
  // The operation can trigger the deque to resize its capacity if more space is required.
  // Provides the strong exception guarantee.
  template <typename... Args>
  void Emplace(Args&&... args);

  // Pops out an item from the deque. Only the owner thread can pop out an item from the deque.
  // The return can be a std::nullopt if this operation fails (empty deque).
  std::optional<T> Pop() noexcept;

  // Steals an item from the deque. Any threads can try to steal an item from the deque.
  // The return can be std::nullopt if this operation failed (not necessarily empty).
  std::optional<T> Steal() noexcept;

  // Destruct the deque, all threads must have finished using the deque.
  ~WorkStealingDeque() noexcept;

 private:
  alignas(kCacheLineSize) std::atomic<std::int64_t> top_{0};
  alignas(kCacheLineSize) std::atomic<std::int64_t> bottom_{0};
  alignas(kCacheLineSize) std::atomic<detail::RingBuffer<T>*> buffer_;
};

template <Simple T>
WorkStealingDeque<T>::WorkStealingDeque(std::int64_t capacity)
    : buffer_(new detail::RingBuffer<T>(capacity)) {}

template <Simple T>
std::size_t WorkStealingDeque<T>::Size() const noexcept {
  std::int64_t b = bottom_.load(std::memory_order_relaxed);
  std::int64_t t = top_.load(std::memory_order_relaxed);
  return static_cast<std::size_t>(b >= t ? b - t : 0);
}

template <Simple T>
std::int64_t WorkStealingDeque<T>::Capacity() const noexcept {
  return buffer_.load(std::memory_order_relaxed)->Capacity();
}

template <Simple T>
bool WorkStealingDeque<T>::Empty() const noexcept {
  return !Size();
}

template <Simple T>
template <typename... Args>
void WorkStealingDeque<T>::Emplace(Args&&... args) {
  // Construct an object before acquiring slot in-case constructor throws
  T object(std::forward<Args>(args)...);

  std::int64_t b = bottom_.load(std::memory_order_relaxed);
  std::int64_t t = top_.load(std::memory_order_relaxed);
  detail::RingBuffer<T>* buffer = buffer_.load(std::memory_order_relaxed);

  if (buffer->Capacity() < (b - t) + 1) {
    // Deque is full, build a new one
    std::exchange(buffer, buffer->Resize(b, t));
    buffer_.store(buffer, std::memory_order_relaxed);
  }

  buffer->Store(b, std::move(object));

  std::atomic_thread_fence(std::memory_order_release);
  bottom_.store(b + 1, std::memory_order_relaxed);
}

template <Simple T>
std::optional<T> WorkStealingDeque<T>::Pop() noexcept {
  std::int64_t b = bottom_.load(std::memory_order_relaxed) - 1;
  detail::RingBuffer<T>* buffer = buffer_.load(std::memory_order_relaxed);

  bottom_.store(b, std::memory_order_relaxed);  // stealers can no longer steal

  std::atomic_thread_fence(std::memory_order_seq_cst);
  std::int64_t t = top_.load(std::memory_order_relaxed);

  if (t <= b) {
    // Non-empty deque
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
    return buffer->Load(b);
  } else {
    bottom_.store(b + 1, std::memory_order_relaxed);
    return std::nullopt;
  }
}

template <Simple T>
std::optional<T> WorkStealingDeque<T>::Steal() noexcept {
  std::int64_t t = top_.load(std::memory_order_acquire);
  std::atomic_thread_fence(std::memory_order_seq_cst);
  std::int64_t b = bottom_.load(std::memory_order_acquire);

  if (t < b) {
    // Must load *before* acquiring the slot as slot may be overwritten immediately after acquiring.
    // This load is NOT required to be atomic even-though it may race with an overrite as we only
    // return the value if we win the race below garanteeing we had no race during our read. If we
    // loose the race then 'value' could be corrupt due to read-during-write race but as T is trivially
    // destructible this does not matter.

    T value = buffer_.load(std::memory_order_consume)->Load(t);
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

template <Simple T>
WorkStealingDeque<T>::~WorkStealingDeque() noexcept {
  delete buffer_.load();
}

}  // namespace tg