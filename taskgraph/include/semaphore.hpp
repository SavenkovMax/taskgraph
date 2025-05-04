#pragma once

#include <atomic>
#include <cassert>

namespace tg {

namespace detail {

#if defined(_WIN32)

#include <windows.h>
#undef min
#undef max

class Semaphore {
 public:
  Semaphore(int initial_count = 0) {
    assert(initial_count >= 0);
    h_sema_ = CreateSemaphore(NULL, initial_count, MAXLONG, NULL);
  }
  ~Semaphore() { CloseHandle(h_sema_); }

  void Wait() { WaitForSingleObject(h_sema_, INFINITE); }

  void Signal(int count = 1) { ReleaseSemaphore(h_sema_, count, NULL); }

 private:
  HANDLE h_sema_;

  Semaphore(const Semaphore& other) = delete;
  Semaphore& operator=(const Semaphore& other) = delete;
};

#elif defined(__unix__)

#include <semaphore.h>

class Semaphore {
 public:
  Semaphore(int initial_count = 0) {
    assert(initial_count >= 0);
    sem_init(&sema_, 0, initial_count);
  }

  ~Semaphore() { sem_destroy(&sema_); }

  void Wait() {
    int rc;
    do {
      rc = sem_wait(&sema_);
    } while (rc == -1 && errno == EINTR);
  }

  void Signal(int count) {
    while (count-- > 0) {
      sem_post(&sema_);
    }
  }

 private:
  sem_t sema_;

  Semaphore(const Semaphore& other) = delete;
  Semaphore& operator=(const Semaphore& other) = delete;
};

#endif

}  // namespace detail

class Semaphore {
 public:
  explicit Semaphore(std::ptrdiff_t desired) : count_(desired) {
    assert(desired >= 0);
  }

  void Release(std::ptrdiff_t update = 1) {
    auto old_count = count_.fetch_add(update, std::memory_order_release);
    auto to_release = -old_count < update ? -old_count : update;
    if (to_release > 0) {
      sema_.Signal(to_release);
    }
  }

  void AcquireMany() {
    for (std::ptrdiff_t spin = 0; spin < 10'000; ++spin) {
        auto old = count_.load(std::memory_order_relaxed);
        if (old > 0 && count_.compare_exchange_strong(old, 0, std::memory_order_acquire)) {
      return;
        }
        std::atomic_signal_fence(std::memory_order_acquire);
  }
  auto old = count_.load(std::memory_order_relaxed);
  for (;;) {
    if (old <= 0) {
      if (count_.compare_exchange_strong(old, old - 1,
                                         std::memory_order_acq_rel,
                                         std::memory_order_relaxed)) {
        sema_.Wait();
        return;
      } else if (count_.compare_exchange_strong(old, 0,
                                                std::memory_order_acq_rel,
                                                std::memory_order_relaxed)) {
        return;
      }
    }
  }
}

private: 
std::atomic<std::ptrdiff_t> count_;
detail::Semaphore sema_;
};

}  // namespace tg