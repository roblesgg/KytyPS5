#include "common/perfStats.h"

#include <cassert>
#include <chrono>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

int main() {
  const auto start = PerfStats::Clock::now();
  {
    PerfStats::Session session;
    std::vector<std::jthread> workers;
    for (int i = 0; i < 4; ++i) {
      workers.emplace_back([] {
        for (int j = 0; j < 1000; ++j) {
          PerfStats::Scope work(PerfStats::Kind::GpuWork);
          PerfStats::Flip();
        }
      });
    }
    workers.clear();
    assert(PerfStats::Data().flips.load() == 4000);
    auto &work = PerfStats::Data()
                     .timings[static_cast<size_t>(PerfStats::Kind::GpuWork)];
    assert(work.calls.load() == 4000);
    assert(work.active.load() == 0);
    {
      PerfStats::Scope idle(PerfStats::Kind::GpuIdle);
      std::this_thread::sleep_for(std::chrono::milliseconds(5200));
    }
  }
  // stop_token must wake the reporter promptly instead of waiting for the next
  // period.
  assert(PerfStats::Clock::now() - start < std::chrono::seconds(9));
#ifdef _WIN32
  std::ifstream file("C:/KYTY/_perf.txt");
#else
  std::ifstream file("_perf.txt");
#endif
  assert(file.good());
  const std::string contents((std::istreambuf_iterator<char>(file)),
                             std::istreambuf_iterator<char>());
  assert(contents.find("total_flips=4000") != std::string::npos);
  assert(contents.find("gpu_idle_ms=0.00/0 total_ms=0.00 active=1") !=
         std::string::npos);
  assert(contents.find("session end") != std::string::npos);
  return 0;
}
