#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <thread>

#ifndef KYTY_PERF_DETAIL
#define KYTY_PERF_DETAIL 0
#endif

// Diagnostic wall-clock timings, not hardware GPU utilization. Nested scopes overlap.
namespace PerfStats {
using Clock = std::chrono::steady_clock;
enum class Kind : size_t {
	GpuWork,
	GpuIdle,
	Shader,
	Pipeline,
	HostWait,
	Draw,
	Compute,
	Bindings,
	ShaderLookup,
	PipelineLookup,
	BufferSync,
	BufferRead,
	TextureLookup,
	Maintenance,
	Count
};
inline constexpr size_t KindCount = static_cast<size_t>(Kind::Count);
struct Counter {
	std::atomic<uint64_t> ns {0};
	std::atomic<uint64_t> calls {0};
	std::atomic<uint64_t> active {0};
};
struct Counters {
	std::array<Counter, KindCount> timings;
	std::atomic<uint64_t>          flips {0};
};
inline Counters& Data() {
	static Counters data;
	return data;
}
class Scope {
public:
	explicit Scope(Kind kind): m_counter(nullptr) {
		// Remove high-frequency diagnostic clocks/atomics from normal comparisons.
		if constexpr (!KYTY_PERF_DETAIL) {
			if (kind > Kind::HostWait) {
				return;
			}
		}
		m_counter = &Data().timings[static_cast<size_t>(kind)];
		m_start   = Clock::now();
		m_counter->active.fetch_add(1, std::memory_order_relaxed);
	}
	~Scope() {
		if (m_counter == nullptr) {
			return;
		}
		const auto ns =
		    std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - m_start).count();
		m_counter->ns.fetch_add(static_cast<uint64_t>(ns), std::memory_order_relaxed);
		m_counter->calls.fetch_add(1, std::memory_order_relaxed);
		m_counter->active.fetch_sub(1, std::memory_order_relaxed);
	}
	Scope(const Scope&)            = delete;
	Scope& operator=(const Scope&) = delete;

private:
	Counter*          m_counter;
	Clock::time_point m_start;
};
inline void Flip() {
	Data().flips.fetch_add(1, std::memory_order_relaxed);
}

// Owned by Main: the reporter stops and joins before the shared counters are destroyed.
class Session {
public:
	Session() {
		(void)Data();
#ifdef _WIN32
		(void)fopen_s(&m_file, "C:/KYTY/_perf-detail.txt", "a");
#else
		m_file = std::fopen("_perf-detail.txt", "a");
#endif
		Write(KYTY_PERF_DETAIL ? "[perf] session start DETAIL-v3; "
		                       : "[perf] session start LITE-v3; "
		                         "fine-grained timers disabled; ");
		Write("completed-scope wall times in ms; nested/thread "
		      "timings "
		      "overlap; "
		      "active scopes are unfinished; fps counts guest flip groups, not repeated display "
		      "frames\n");
		if (m_file == nullptr) {
			std::fputs("[perf] WARNING: cannot open _perf-detail.txt; using console only\n",
			           stderr);
		}
		m_start = m_previous = Clock::now();
		m_thread             = std::jthread([this](std::stop_token token) {
			std::mutex                  mutex;
			std::condition_variable_any condition;
			std::unique_lock            lock(mutex);
			while (
			    !condition.wait_for(lock, token, std::chrono::seconds(5), [] { return false; })) {
				if (token.stop_requested()) {
					break;
				}
				Report();
			}
		});
	}
	~Session() {
		m_thread.request_stop();
		m_thread.join();
		Report();
		Write("[perf] session end\n");
		if (m_file != nullptr) {
			std::fclose(m_file);
		}
	}
	Session(const Session&)            = delete;
	Session& operator=(const Session&) = delete;

private:
	void Write(const char* text) {
		std::fputs(text, stdout);
		std::fflush(stdout);
		if (m_file != nullptr) {
			std::fputs(text, m_file);
			std::fflush(m_file);
		}
	}
	void Report() {
		const auto   now     = Clock::now();
		const double seconds = std::chrono::duration<double>(now - m_previous).count();
		const auto   flips   = Data().flips.load(std::memory_order_relaxed);
		char         line[256];
		std::snprintf(line, sizeof(line),
		              "[perf] t=%.1fs window=%.3fs fps=%.2f flips=%llu total_flips=%llu",
		              std::chrono::duration<double>(now - m_start).count(), seconds,
		              seconds > 0 ? static_cast<double>(flips - m_flips) / seconds : 0.0,
		              static_cast<unsigned long long>(flips - m_flips),
		              static_cast<unsigned long long>(flips));
		Write(line);
		constexpr std::array<const char*, KindCount> names {
		    "gpu_work",    "gpu_idle",    "shader",         "pipeline",      "host_wait",
		    "draw",        "compute",     "bindings",       "shader_lookup", "pipeline_lookup",
		    "buffer_sync", "buffer_read", "texture_lookup", "maintenance"};
		for (size_t i = 0; i < KindCount; ++i) {
			auto&      counter = Data().timings[i];
			const auto ns      = counter.ns.load(std::memory_order_relaxed);
			const auto calls   = counter.calls.load(std::memory_order_relaxed);
			std::snprintf(
			    line, sizeof(line), " %s_ms=%.2f/%llu total_ms=%.2f active=%llu", names[i],
			    static_cast<double>(ns - m_ns[i]) / 1.0e6,
			    static_cast<unsigned long long>(calls - m_calls[i]),
			    static_cast<double>(ns) / 1.0e6,
			    static_cast<unsigned long long>(counter.active.load(std::memory_order_relaxed)));
			Write(line);
			m_ns[i]    = ns;
			m_calls[i] = calls;
		}
		Write("\n");
		m_flips    = flips;
		m_previous = now;
	}
	FILE*                           m_file = nullptr;
	Clock::time_point               m_start;
	Clock::time_point               m_previous;
	std::array<uint64_t, KindCount> m_ns {};
	std::array<uint64_t, KindCount> m_calls {};
	uint64_t                        m_flips = 0;
	std::jthread                    m_thread;
};
} // namespace PerfStats
