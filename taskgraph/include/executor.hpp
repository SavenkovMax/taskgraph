#pragma once

#include <atomic>
#include <cstddef>
#include <future>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <vector>

#include "graph.hpp"
#include "mpmc_queue.hpp"
#include "workstealing_deque.hpp"

namespace tg {

namespace detail {

struct Worker {
  constexpr static size_t kTaskQueueSize = 256;

  std::atomic_flag stopped_{};
  size_t id{};
  std::thread thread;
  BoundedTaskQueue<TaskNode*, kTaskQueueSize> lrq{};
};

struct TaskGraphContext {
  std::atomic<size_t> tasks_remaining{};
  std::vector<TaskNode*> ready_queue{};
  std::unordered_map<TaskNode*, std::atomic<size_t>> tasks_dependencies{};
  std::promise<void> promise;
};

}  // namespace detail

class Executor {
 public:
  explicit Executor(size_t workers = static_cast<size_t>(
                        std::thread::hardware_concurrency()));

  ~Executor();

  void Submit(TaskNode* task);

  std::future<void> Run(TaskGraph& graph);

  // Returns an ownership thread local pointer to the current ThreadPool
  static Executor* Current() { return thread_local_instance_; }

  // Returns a count of workers
  size_t Size() const noexcept { return workers_.size(); }


 private:
  void StartWorkerThreads(size_t workers);
  void WorkerRoutine(size_t worker_id);
  void ScheduleTask(TaskNode* task);
  void OnTaskCompletion(TaskNode* task);
  void BuildTaskGraphContext(TaskGraph& graph, detail::TaskGraphContext& context);

  // Work-stealing

  void FindRunnable(size_t worker_id);

  void StealWork(size_t thief, size_t victim);

 private:
  static inline thread_local Executor* thread_local_instance_{nullptr};

  std::vector<detail::Worker> workers_{};
  UnboundedMPMCBlockingQueue<TaskNode*> tasks_{};
  // TODO : std::variant for different execution contexts???
  detail::TaskGraphContext graph_context_{};
};

Executor::Executor(size_t workers) : workers_(workers) {
  StartWorkerThreads(workers_.size());
}

Executor::~Executor() {
  for (size_t i = 0; i < workers_.size(); ++i) {
    workers_[i].stopped_.test_and_set(std::memory_order_release);
  }
  tasks_.Close();
  for (auto& t : workers_) {
    t.thread.join();
  }
}

void Executor::Submit(TaskNode* task) {
  tasks_.Push(task);
}

void Executor::StartWorkerThreads(size_t workers) {
  for (size_t i = 0; i < workers; ++i) {
    workers_[i].id = i;
    workers_[i].thread = std::thread([this, i] {
      thread_local_instance_ = this;
      WorkerRoutine(i);
    });
  }
}

void Executor::WorkerRoutine(size_t worker_id) {
  while (!workers_[worker_id].stopped_.test(std::memory_order_acquire)) {
    auto task_opt = tasks_.Pop();
    if (task_opt.has_value()) {
      auto& task = task_opt.value();
      task->Run();
      OnTaskCompletion(task);
    }
  }
}

void Executor::ScheduleTask(TaskNode* task) {
  tasks_.Push(task);
}

void Executor::FindRunnable(size_t worker_id) {
  //auto& worker = workers_[worker_id];
  //// local run queue
  //{
  //  auto task_opt = worker.lrq.Pop();
  //  if (task_opt.has_value()) {
  //    auto& task = task_opt.value();
  //    (*task)();
  //    return;
  //  }
  //}

  //// global run queue
  //{
  //  auto tasks_opt = tasks_.PopBatch(workers_.size());
  //  if (tasks_opt.has_value()) {
  //    auto& tasks = tasks_opt.value();
  //    for (auto& task : tasks) {
  //      worker.lrq.TryPush(&task);
  //    }
  //    return;
  //  }
  //}
}

void Executor::StealWork(size_t thief, size_t victim) {
  auto item = workers_[victim].lrq.TrySteal();
  if (item.has_value()) {
    workers_[thief].lrq.TryPush(item.value());
  }
}

void Executor::OnTaskCompletion(TaskNode* task) {
  for (TaskNode* successor : task->GetSuccessors()) {
    if (graph_context_.tasks_dependencies[successor].fetch_sub(
            1, std::memory_order_acq_rel) == 1) {
      ScheduleTask(successor);
    }
  }
  if (graph_context_.tasks_remaining.fetch_sub(1, std::memory_order_relaxed) ==
      1) {
    graph_context_.promise.set_value();
  }
}

void Executor::BuildTaskGraphContext(TaskGraph& graph, detail::TaskGraphContext& context) {
  context.promise = std::promise<void>();
  context.ready_queue.clear();
  context.tasks_dependencies.clear();
  context.tasks_remaining.store(graph.Size(), std::memory_order_relaxed);

  for (auto& task : graph.GetTasks()) {
    context.tasks_dependencies.emplace(task, task->Dependencies());
  }
  for (auto& [task, dep] : graph_context_.tasks_dependencies) {
    if (dep == 0) {
      context.ready_queue.push_back(task);
    }
  }
  if (context.ready_queue.empty()) {
    throw std::logic_error("The task graph contains a cycle!");
  }
}

std::future<void> Executor::Run(TaskGraph& graph) {
  auto& context = graph_context_;
  BuildTaskGraphContext(graph, context);

  std::future<void> future = context.promise.get_future();

  for (auto& task : context.ready_queue) {
    ScheduleTask(task);
  }

  return future;
}

}  // namespace tg
