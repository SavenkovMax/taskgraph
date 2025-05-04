#include <iostream>

#include <chrono>
using namespace std::chrono_literals;

#include "../include/executor.hpp"
#include "../include/graph.hpp"

int main() {
  tg::Executor exe(4);
  tg::TaskGraph graph;

  auto& A = graph.Emplace([] { std::cout << "A is completed\n"; });
  auto& B = graph.Emplace([] { std::cout << "B is completed\n"; });
  auto& C = graph.Emplace([] { std::cout << "C is completed\n"; });
  auto& D = graph.Emplace([] { std::cout << "D is completed\n"; });
  auto& E = graph.Emplace([] { std::cout << "E is completed\n"; });
  auto& F = graph.Emplace([] {
    std::this_thread::sleep_for(3s);
    std::cout << "F is completed\n";
  });
  auto& G = graph.Emplace([] { std::cout << "G is completed\n"; });
  auto& K = graph.Emplace([] { std::cout << "K is completed\n"; });
  auto& L = graph.Emplace([] { std::cout << "L is completed\n"; });
  auto& M = graph.Emplace([] { std::cout << "M is completed\n"; });

  A.Precede(B, C);
  B.Precede(D, E);
  C.Precede(F, G);
  K.Succeed(D, E);
  L.Succeed(F, G);
  M.Succeed(K, L);

  exe.Run(graph).get();
}
