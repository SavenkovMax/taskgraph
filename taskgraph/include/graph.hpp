#pragma once

#include <memory>
#include <vector>
#include "task.hpp"

namespace tg {

  class TaskGraph {
  public:
    explicit TaskGraph() = default;

    template <typename F>
    Task& Emplace(F&& func) {
      tasks_.push_back(std::make_unique<Task>(std::forward<F>(func)));
      return *(tasks_.back());
    }

  private:
    std::vector<std::unique_ptr<Task>> tasks_{};
  };

} // namespace tg
