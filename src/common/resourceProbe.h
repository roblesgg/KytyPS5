#pragma once

#include <atomic>
#include <cstdint>
#include <cstdlib>

// Diagnostic totals only. They never cache descriptors or change resource ownership.
namespace ResourceProbe {
struct Totals {
	uint64_t programs           = 0;
	uint64_t indirect_tables    = 0;
	uint64_t bounded_tables     = 0;
	uint64_t table_words        = 0;
	uint64_t candidate_calls    = 0;
	uint64_t unique_descriptors = 0;
	uint64_t dedup_comparisons  = 0;
	uint64_t max_candidates     = 0;
	uint64_t max_words          = 0;
};

inline bool Enabled() {
	static const bool enabled = [] {
		const char* value = std::getenv("KYTY_RESOURCE_PROBE");
		return value != nullptr && value[0] == '1' && value[1] == '\0';
	}();
	return enabled;
}

struct Counters {
	std::atomic<uint64_t> programs {0};
	std::atomic<uint64_t> indirect_tables {0};
	std::atomic<uint64_t> bounded_tables {0};
	std::atomic<uint64_t> table_words {0};
	std::atomic<uint64_t> candidate_calls {0};
	std::atomic<uint64_t> unique_descriptors {0};
	std::atomic<uint64_t> dedup_comparisons {0};
	std::atomic<uint64_t> max_candidates {0};
	std::atomic<uint64_t> max_words {0};
};

inline Counters& Data() {
	static Counters counters;
	return counters;
}

inline void Add(std::atomic<uint64_t>& counter, uint64_t value) {
	if (value != 0) counter.fetch_add(value, std::memory_order_relaxed);
}

inline void Max(std::atomic<uint64_t>& counter, uint64_t value) {
	if (value == 0) return;
	auto previous = counter.load(std::memory_order_relaxed);
	while (previous < value &&
	       !counter.compare_exchange_weak(previous, value, std::memory_order_relaxed)) {
	}
}

// One batch at the end of a materialization, not an atomic operation per descriptor.
inline void Submit(const Totals& totals) {
	if (!Enabled()) return;
	auto& data = Data();
	Add(data.programs, totals.programs);
	Add(data.indirect_tables, totals.indirect_tables);
	Add(data.bounded_tables, totals.bounded_tables);
	Add(data.table_words, totals.table_words);
	Add(data.candidate_calls, totals.candidate_calls);
	Add(data.unique_descriptors, totals.unique_descriptors);
	Add(data.dedup_comparisons, totals.dedup_comparisons);
	Max(data.max_candidates, totals.max_candidates);
	Max(data.max_words, totals.max_words);
}

// Relaxed snapshots are observational; a concurrent batch may straddle a report boundary.
// Maxima cover the entire session, not just the latest five-second window.
inline Totals Snapshot() {
	const auto& data = Data();
	return {
	    data.programs.load(std::memory_order_relaxed),
	    data.indirect_tables.load(std::memory_order_relaxed),
	    data.bounded_tables.load(std::memory_order_relaxed),
	    data.table_words.load(std::memory_order_relaxed),
	    data.candidate_calls.load(std::memory_order_relaxed),
	    data.unique_descriptors.load(std::memory_order_relaxed),
	    data.dedup_comparisons.load(std::memory_order_relaxed),
	    data.max_candidates.load(std::memory_order_relaxed),
	    data.max_words.load(std::memory_order_relaxed),
	};
}
} // namespace ResourceProbe
