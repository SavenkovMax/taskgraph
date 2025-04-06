#include <iostream>

#include <chrono>
using namespace std::chrono_literals;

#include "../include/graph.hpp"
#include "../include/threadpool.hpp"
#include "../include/waitgroup.hpp"

int main() {
  tg::ThreadPool tp(4);
  tp.Start();
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
  K.Gather(D, E);
  L.Gather(F, G);
  M.Gather(K, L);

  graph.RunVia(tp).get();

  tp.Stop();
}
