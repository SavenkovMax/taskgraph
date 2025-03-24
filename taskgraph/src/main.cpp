#include <iostream>
#include "../include/waitgroup.hpp"
#include "../include/threadpool.hpp"

#include <chrono>
using namespace std::chrono_literals;

int main() {
  int x = 5;
  int y = 3;
  tg::details::UniqueFunction task([x, y] {
    std::cout << "Hi\n";
  });
  task();
 /* tg::ThreadPool tp(4);
  tp.Start();
  std::cout << "ThreadPool created\n";
  tg::WaitGroup wg;
  wg.Add(8);
  for (int i = 0; i < 8; ++i) {
    tp.Submit([&wg]() {
      std::this_thread::sleep_for(1s);
      wg.Done();
    });
  }
  wg.Wait();
  std::cout << "Work is done\n";
  tp.Stop(); */
}
