#pragma once

#include <atomic>
#include <cstddef>
#include <functional>
#include <random>
#include <thread>
#include <vector>

#include "mpmc_queue.hpp"
#include "task.hpp"
#include "workstealing_deque.hpp"

namespace tg {

namespace detail {
struct Worker {
  std::thread thread;
  WorkStealingDeque<Task*> lrq;
  std::mt19937 rdgen{std::random_device{}()};
  std::size_t id{};
};

}  // namespace detail

class ThreadPool {
 public:
  explicit ThreadPool(std::size_t workers = static_cast<std::size_t>(
                          std::thread::hardware_concurrency()));

  ~ThreadPool();

  void Submit(Task task);

  void Start();

  void Stop();

  // Returns an ownership thread local pointer to the current ThreadPool
  static ThreadPool* Current() { return thread_local_instance_; }

  // Returns a count of workers
  [[nodiscard]] size_t Size() const noexcept { return workers_.size(); }

 private:
  void StartWorkerThreads(size_t workers);

  void WorkerRoutine();

  void FindRunnable(std::size_t worker_id);

  void StealWork(std::size_t thief, std::size_t victim);

 private:
  static inline thread_local ThreadPool* thread_local_instance_{nullptr};

  std::vector<detail::Worker> workers_{};
  UnboundedMPMCBlockingQueue<Task> tasks_{};
  std::atomic_bool stopped_{false};
};

ThreadPool::ThreadPool(std::size_t workers) : workers_(workers) {}

ThreadPool::~ThreadPool() {
  assert(stopped_);
  for (auto& t : workers_) {
    t.thread.join();
  }
}

void ThreadPool::Submit(Task task) {
  tasks_.Push(std::move(task));
}

void ThreadPool::Start() {
  StartWorkerThreads(workers_.size());
}

void ThreadPool::Stop() {
  stopped_.store(true, std::memory_order_relaxed);
  tasks_.Close();
}

void ThreadPool::StartWorkerThreads(std::size_t workers) {
  for (size_t i = 0; i < workers; ++i) {
    workers_[i].id = i;
    workers_[i].thread = std::thread([this] {
      thread_local_instance_ = this;
      WorkerRoutine();
    });
  }
}

void ThreadPool::FindRunnable(std::size_t worker_id) {
  auto& worker = workers_[worker_id];
  // local run queue
  {
    auto task_opt = worker.lrq.Pop();
    if (task_opt.has_value()) {
      auto& task = task_opt.value();
      (*task)();
      return;
    }
  }

  // global run queue
  {
    auto tasks_opt = tasks_.PopBatch(workers_.size());
    if (tasks_opt.has_value()) {
      auto& tasks = tasks_opt.value();
      for (auto& task : tasks) {
        worker.lrq.Emplace(&task);
      }
      return;
    }
  }
}

void ThreadPool::StealWork(std::size_t thief, std::size_t victim) {
  auto item = workers_[victim].lrq.Steal();
  if (item.has_value()) {
    workers_[thief].lrq.Emplace(item.value());
  }
}

void ThreadPool::WorkerRoutine() {
  while (true) {
    if (stopped_.load(std::memory_order_relaxed)) {
      break;
    }
    auto task_opt = tasks_.Pop();
    if (task_opt.has_value()) {
      auto& task = task_opt.value();
      task();
    }
  }
}

}  // namespace tg
