#pragma once

#include <thread>
#include <atomic>
#include <vector>
#include <functional>

#include "mpmcqueue.hpp"
#include "task.hpp"

namespace tg {
  
  class ThreadPool {
  public:
    explicit ThreadPool(size_t workers = std::thread::hardware_concurrency()) {
      workers_.reserve(workers);
    }

    ~ThreadPool() {
      for (auto& t : workers_) {
        t.join();
      }
    }

    void Start() {
      StartWorkerThreads(workers_.capacity());
    }

    void Submit(Task task) {
      tasks_.Push(std::move(task));
    }

    void Stop() {
      stopped_.store(true, std::memory_order_relaxed);
      tasks_.Close();
    }

    // Returns an ownership thread local pointer to the current ThreadPool
    [[nodiscard]] static ThreadPool* Current() {
      return thread_local_instance_;
    }

    // Returns a count of workers
    [[nodiscard]] size_t Size() const noexcept {
      return workers_.size();
    }

  private:
    void StartWorkerThreads(size_t workers) {
      for (size_t i = 0; i < workers; ++i) {
        workers_.emplace_back([this]() {
          thread_local_instance_ = this;
          WorkerRoutine();
        });
      }
    }

    void WorkerRoutine() {
      while (true) {
        if (stopped_.load(std::memory_order_relaxed)) {
          break;
        }
        auto task_opt = tasks_.Pop();
        if (task_opt.has_value()) {
          auto task = task_opt.value();
          task();
        }
      }
    }

  private:
    static inline thread_local ThreadPool* thread_local_instance_{nullptr};

    std::vector<std::thread> workers_{};
    UnboundedMPMCBlockingQueue<Task> tasks_{};
    std::atomic_bool stopped_{false};
  };

} // namespace tg
