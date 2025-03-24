#pragma once

#include <algorithm>
#include <vector>
#include <stdexcept>
#include <string>

namespace tg {

  namespace details {

    // std::function replacement for containing move-only lambda functions
    class UniqueFunction {
      private:
        struct IRunnable {
          virtual ~IRunnable() = default;
          virtual void Run() = 0;
        };
        
        template <typename F>
        struct Runnable : public IRunnable {
          F func;
          Runnable(F&& func) : func(std::move(func)) {}
          ~Runnable() override = default;

          virtual void Run() override {
            func();
          }
        };

      public:
        template <typename F>
        UniqueFunction(F&& func) : fptr_(new Runnable<decltype(func)>(std::forward<F>(func))) {}

        ~UniqueFunction() {
          delete fptr_;
        }

        // Non-copyable
        UniqueFunction(const UniqueFunction&) = delete;
        UniqueFunction& operator=(const UniqueFunction&) = delete;

        // Movable
        UniqueFunction(UniqueFunction&& other) : fptr_(other.fptr_) {
          other.fptr_ = nullptr;
        }

        UniqueFunction& operator=(UniqueFunction&& other) {
          if (this == &other) {
            return *this;
          }

          delete fptr_;
          fptr_ = other.fptr_;
          other.fptr_ = nullptr;
          return *this;
        }


        void operator()() {
          fptr_->Run();
        }

      private:
        IRunnable* fptr_;
    };

  } // namespace details

  class Task {
  public:
    template <typename F>
    Task(F&& function) : func_(std::forward<F>(function)) {}

    // Non-copyable
    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;

    // Movable
    Task(Task&& other) 
      : func_(std::move(other.func_)),
        predecessors_(std::move(other.predecessors_)),
        successors_(std::move(other.successors_)) {

        }

    Task& operator=(Task&& other) {
      if (this == &other) {
        return *this;
      }
      func_ = std::move(other.func_);
      predecessors_ = std::move(other.predecessors_);
      successors_ = std::move(other.successors_);
      return *this;
    }

    void SetName(std::string task_name) {
      name_ = std::move(task_name);
    }

    std::string GetName() const {
      return name_;
    }
   
    template <typename... Tasks>
    void Precede(Tasks&&... tasks) {
      (predecessors_.push_back(std::forward<Tasks>(tasks)), ...);
    }

    template <typename... Tasks>
    void Succeed(Tasks&&... tasks) {
      (successors_.push_back(std::forward<Tasks>(tasks)), ...);
    }

    void RemovePredecessor(Task* task) {
      if (std::find(predecessors_.cbegin(), predecessors_.cend(), task) == predecessors_.cend()) {
        throw std::runtime_error("Task with the name");
      }
      predecessors_.erase(
          std::remove(predecessors_.begin(), predecessors_.end(), task),
          predecessors_.end()
      );
    }

    size_t Predecessors() const {
      return predecessors_.size();
    }

    size_t Successors() const {
      return successors_.size();
    }

    void operator()() {
      func_();
    }

  private:
    details::UniqueFunction func_;
    std::string name_{};

    std::vector<Task*> predecessors_{};
    std::vector<Task*> successors_{};
  };

} // namespace tg
