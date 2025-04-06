#pragma once

#include <algorithm>
#include <cmath>
#include <future>
#include <memory>
#include <stdexcept>
#include <unordered_map>
#include <vector>

#include "threadpool.hpp"
#include "task.hpp"
#include "waitgroup.hpp"

namespace tg {

// The class represents the task graph node.
// Task is not copyable, it is made only movable in order to support move-only lambdas.
class TaskNode {
 public:
  template <typename F>
  TaskNode(F&& function);

  // Non-copyable
  TaskNode(const TaskNode&) = delete;
  TaskNode& operator=(const TaskNode&) = delete;

  // Move-constructable
  TaskNode(TaskNode&& other) noexcept;

  // Move-assignable
  TaskNode& operator=(TaskNode&& other) noexcept;

  // Creates a precedence links from this to other tasks
  template <typename... Tasks>
  void Precede(Tasks&&... tasks);

  // Creates a gathering links from other tasks to this task
  template <typename... Tasks>
  void Gather(Tasks&&... tasks);

  std::vector<TaskNode*>& GetSuccessors() noexcept { return successors_; }

  const std::vector<TaskNode*>& GetSuccessors() const noexcept {
    return successors_;
  }

  std::size_t Dependencies() const noexcept { return dependencies_; }

  void Run() { func_(); }

 private:
  Task func_;

  std::size_t dependencies_{};
  std::vector<TaskNode*> successors_{};
};

// ----------------------------------------------------------------------

template <typename F>
TaskNode::TaskNode(F&& function) : func_(std::forward<F>(function)) {}

TaskNode::TaskNode(TaskNode&& other) noexcept
    : func_(std::move(other.func_)),
      dependencies_(other.dependencies_),
      successors_(std::move(other.successors_)) {}

TaskNode& TaskNode::operator=(TaskNode&& other) noexcept {
  if (this == &other) {
    return *this;
  }
  func_ = std::move(other.func_);
  dependencies_ = other.dependencies_;
  successors_ = std::move(other.successors_);
  return *this;
}

template <typename... Tasks>
void TaskNode::Precede(Tasks&&... tasks) {
  (++tasks.dependencies_, ...);
  (successors_.push_back(&std::forward<Tasks>(tasks)), ...);
}

template <typename... Tasks>
void TaskNode::Gather(Tasks&&... tasks) {
  dependencies_ += sizeof...(tasks);
  (tasks.successors_.push_back(this), ...);
}

// ----------------------------------------------------------------------

// ----------------------------------------------------------------------

class TaskGraph {
 public:
  explicit TaskGraph() = default;

  // Non-copyable
  TaskGraph(const TaskGraph&) = delete;
  TaskGraph& operator=(const TaskGraph&) = delete;

  // Movable
  TaskGraph(TaskGraph&& other) noexcept;
  TaskGraph& operator=(TaskGraph&& other) noexcept;

  template <typename F>
  TaskNode& Emplace(F&& func);

  std::future<void> RunVia(ThreadPool& tp);

 private:
  using PromisePtr = std::shared_ptr<std::promise<void>>;

  void OnTaskCompletion(TaskNode* node, ThreadPool& tp, PromisePtr promise);
  void ScheduleTask(TaskNode* node, ThreadPool& tp, PromisePtr promise);

 private:
  std::vector<std::unique_ptr<TaskNode>> tasks_{};

  std::atomic<std::size_t> tasks_remaining_{};

  std::vector<TaskNode*> ready_queue_{};
  std::unordered_map<TaskNode*, std::atomic<std::size_t>> ref_count_{};
};

// ----------------------------------------------------------------------

TaskGraph::TaskGraph(TaskGraph&& other) noexcept
    : tasks_(std::move(other.tasks_)) {}

TaskGraph& TaskGraph::operator=(TaskGraph&& other) noexcept {
  if (this == &other) {
    return *this;
  }

  tasks_ = std::move(other.tasks_);

  return *this;
}

template <typename F>
TaskNode& TaskGraph::Emplace(F&& func) {
  tasks_.push_back(std::make_unique<TaskNode>(std::forward<F>(func)));
  tasks_remaining_.fetch_add(1, std::memory_order_relaxed);
  return *(tasks_.back());
}

void TaskGraph::OnTaskCompletion(TaskNode* node, ThreadPool& tp,
                                 PromisePtr promise) {
  for (TaskNode* successor : node->GetSuccessors()) {
    if (ref_count_[successor].fetch_sub(1, std::memory_order_acq_rel) == 1) {
      ScheduleTask(successor, tp, promise);
    }
  }
  if (tasks_remaining_.fetch_sub(1, std::memory_order_relaxed) == 1) {
    promise->set_value();
  }
}

void TaskGraph::ScheduleTask(TaskNode* node, ThreadPool& tp,
                             PromisePtr promise) {
  tp.Submit([this, node, &tp, promise] {
    node->Run();
    OnTaskCompletion(node, tp, promise);
  });
}

std::future<void> TaskGraph::RunVia(ThreadPool& tp) {
  for (auto& task : tasks_) {
    auto t = task.get();
    ref_count_.emplace(t, t->Dependencies());
  }
  for (auto& [task, dep] : ref_count_) {
    if (dep == 0) {
      ready_queue_.push_back(task);
    }
  }
  if (ready_queue_.empty()) {
    throw std::runtime_error("The task graph contains a cycle!");
  }

  PromisePtr promise_ptr = std::make_shared<std::promise<void>>();
  std::future<void> future_result = promise_ptr->get_future();

  for (auto& task : ready_queue_) {
    ScheduleTask(task, tp, promise_ptr);
  }

  return future_result;
}

// ----------------------------------------------------------------------

}  // namespace tg
