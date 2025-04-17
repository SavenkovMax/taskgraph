#include <iostream>

#include <chrono>
using namespace std::chrono_literals;

#include "../include/executor.hpp"
#include "../include/graph.hpp"

#include <mutex>
#include <string>

std::mutex m;


void PrintAtomically() {
  std::lock_guard lock(m);
  std::cout << "Hello\n";
}

int main() {
  tg::Executor exe(4);
  tg::TaskGraph graph;

  auto& A = graph.Emplace([]{
      PrintAtomically();
  });

  auto& B = graph.Emplace([] {
    PrintAtomically();
  });

  auto& C = graph.Emplace([] {
    PrintAtomically();
  });

  auto& D = graph.Emplace([] {
    PrintAtomically();
  });

  A.Precede(B, C);
  D.Succeed(B, C);

  exe.Run(graph).get();
}
