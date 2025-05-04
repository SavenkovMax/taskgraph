#pragma once

#include <memory>
#include <vector>

#include "task.hpp"

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

  // Creates a succeeding links from other tasks to this task
  template <typename... Tasks>
  void Succeed(Tasks&&... tasks);

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
void TaskNode::Succeed(Tasks&&... tasks) {
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

  size_t Size() const noexcept { return tasks_.size(); }

  std::vector<TaskNode*> GetTasks() const noexcept;

 private:
  std::vector<std::unique_ptr<TaskNode>> tasks_{};
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
  return *(tasks_.back());
}

std::vector<TaskNode*> TaskGraph::GetTasks() const noexcept {
  std::vector<TaskNode*> tasks;
  for (size_t i = 0; i < tasks_.size(); ++i) {
    tasks.push_back(tasks_[i].get());
  }
  return tasks;
}

// ----------------------------------------------------------------------

}  // namespace tg
