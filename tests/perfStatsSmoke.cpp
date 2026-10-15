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
    ResourceProbe::RecordGcRun();
    ResourceProbe::RecordGcRun();
    ResourceProbe::RecordGcRun();
    ResourceProbe::RecordSuspendPointEnqueued();
    ResourceProbe::RecordSuspendPointEnqueued();
    ResourceProbe::RecordSuspendPointProcessed();
    const auto maintenance = ResourceProbe::SnapshotMaintenance();
    assert(maintenance.gc_runs == (ResourceProbe::Enabled() ? 3u : 0u));
    assert(maintenance.suspend_points_enqueued ==
           (ResourceProbe::Enabled() ? 2u : 0u));
    assert(maintenance.suspend_points_processed ==
           (ResourceProbe::Enabled() ? 1u : 0u));
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
#if !KYTY_PERF_DETAIL
    {
      PerfStats::Scope draw(PerfStats::Kind::Draw);
      PerfStats::Scope sync(PerfStats::Kind::BufferSync);
    }
    assert(PerfStats::Data()
               .timings[static_cast<size_t>(PerfStats::Kind::Draw)]
               .calls.load() == 0);
    assert(PerfStats::Data()
               .timings[static_cast<size_t>(PerfStats::Kind::BufferSync)]
               .calls.load() == 0);
#endif
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
  std::ifstream file("C:/KYTY/_perf-detail.txt");
#else
  std::ifstream file("_perf-detail.txt");
#endif
  assert(file.good());
  const std::string contents((std::istreambuf_iterator<char>(file)),
                             std::istreambuf_iterator<char>());
  assert(contents.find("total_flips=4000") != std::string::npos);
  assert(contents.find("gpu_idle_ms=0.00/0 total_ms=0.00 active=1") !=
         std::string::npos);
  assert(contents.find("session end") != std::string::npos);
  if (ResourceProbe::Enabled()) {
    const std::string events =
        "gc_runs=3 suspend_points_enqueued=2 suspend_points_processed=1";
    const auto first = contents.find(events);
    assert(first != std::string::npos);
    assert(contents.find(events, first + events.size()) == std::string::npos);
    assert(contents.find(
               "gc_runs=0 suspend_points_enqueued=0 suspend_points_processed=0") !=
           std::string::npos);
  } else {
    assert(contents.find("[maintenance-probe]") == std::string::npos);
  }
  return 0;
}
