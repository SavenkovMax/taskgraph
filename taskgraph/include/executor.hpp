#pragma once

#include <atomic>
#include <cstddef>
#include <functional>
#include <future>
#include <random>
#include <ratio>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include "graph.hpp"
#include "semaphore.hpp"
#include "workstealing_deque.hpp"
#include "xoroshiro128.hpp"

namespace tg {

namespace detail {

class OneShot {
 public:
  // Stores a copy of the TaskNode
  OneShot(TaskNode* task) : task_(task) {}

  auto get_future() { return promise_.get_future(); }

  void operator()() && {
    try {
      task_->Run();
      promise_.set_value();
    } catch (...) {
      promise_.set_exception(std::current_exception());
    }
  }

 private:
  std::promise<void> promise_;
  TaskNode* task_;
};

struct SemaDeque {
  tg::Semaphore sem{0};
  WorkStealingDeque<TaskNode*> tasks;
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
  explicit Executor(const size_t workers = static_cast<size_t>(
                        std::thread::hardware_concurrency()));

  ~Executor();

  std::future<void> Run(TaskGraph& graph);

  // Returns an ownership thread local pointer to the current Executor
  static Executor* Current() { return thread_local_instance_; }

  // Returns a count of workers
  [[nodiscard]] size_t Size() const noexcept { return threads_.size(); }

 private:
  void OnTaskCompletion(TaskNode* task);
  void BuildTaskGraphContext(TaskGraph& graph,
                             detail::TaskGraphContext& context);
  void Execute(TaskNode* task);
  auto Enqueue(TaskNode* task);

 private:
  static inline thread_local Executor* thread_local_instance_{nullptr};

  std::atomic<std::int64_t> in_flight_{0};
  size_t count_{0};
  std::vector<detail::SemaDeque> deques_;
  std::vector<std::jthread> threads_;
  detail::TaskGraphContext graph_context_{};
};

Executor::Executor(const size_t workers) : deques_(workers) {
  for (std::size_t i = 0; i < workers; ++i) {
    threads_.emplace_back([&, id = i](std::stop_token tok) {
      jump(id);  // Get a different random stream
      do {
        // Wait to be signalled
        deques_[id].sem.AcquireMany();

        std::size_t spin = 0;

        do {
          // Prioritise our work otherwise steal
          std::size_t t = spin++ < 100 || !deques_[id].tasks.Empty()
                              ? id
                              : xoroshiro128() % deques_.size();

          if (std::optional one_shot = deques_[t].tasks.Steal()) {
            in_flight_.fetch_sub(1, std::memory_order_release);
            auto fn = *one_shot;
            fn->Run();
            OnTaskCompletion(fn);
          }

          // Loop until all the work is done.
        } while (in_flight_.load(std::memory_order_acquire) > 0);

      } while (!tok.stop_requested());
    });
  }
}

Executor::~Executor() {
  for (auto& t : threads_) {
    t.request_stop();
  }
  for (auto& d : deques_) {
    d.sem.Release();
  }
}

auto Executor::Enqueue(TaskNode* task) {
  auto one_shot = detail::OneShot(task);
  auto future = one_shot.get_future();

  Execute(task);

  return future;
}

void Executor::Execute(TaskNode* task) {
  size_t i = count_++ % deques_.size();

  in_flight_.fetch_add(1, std::memory_order_relaxed);
  deques_[i].tasks.Emplace(task);
  deques_[i].sem.Release();
}

void Executor::OnTaskCompletion(TaskNode* task) {
  for (TaskNode* successor : task->GetSuccessors()) {
    if (graph_context_.tasks_dependencies[successor].fetch_sub(
            1, std::memory_order_acq_rel) == 1) {
      Enqueue(successor);
    }
  }
  if (graph_context_.tasks_remaining.fetch_sub(1, std::memory_order_relaxed) ==
      1) {
    graph_context_.promise.set_value();
  }
}

void Executor::BuildTaskGraphContext(TaskGraph& graph,
                                     detail::TaskGraphContext& context) {
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
    Enqueue(task);
  }

  return future;
}

}  // namespace tg
