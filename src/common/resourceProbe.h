#pragma once

#include <atomic>
#include <cstddef>
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
#ifdef _MSC_VER
		char*       value  = nullptr;
		std::size_t length = 0;
		if (_dupenv_s(&value, &length, "KYTY_RESOURCE_PROBE") != 0) return false;
		const bool result = value != nullptr && length == 2 && value[0] == '1';
		std::free(value);
		return result;
#else
		const char* value = std::getenv("KYTY_RESOURCE_PROBE");
		return value != nullptr && value[0] == '1' && value[1] == '\0';
#endif
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

// BDA observations are separate so descriptor materializations retain their existing batch.
struct BdaTotals {
	uint64_t bda_calls           = 0;
	uint64_t bda_mapped_ranges   = 0;
	// Counts loop entries, including repeated visits and non-intersecting predecessors.
	uint64_t bda_entries_visited = 0;
};

struct BdaCounters {
	std::atomic<uint64_t> bda_calls {0};
	std::atomic<uint64_t> bda_mapped_ranges {0};
	std::atomic<uint64_t> bda_entries_visited {0};
};

inline BdaCounters& BdaData() {
	static BdaCounters counters;
	return counters;
}

// Publish one local batch per PrepareBda invocation, never from the buffer traversal.
inline void SubmitBda(const BdaTotals& totals) {
	if (!Enabled()) return;
	auto& data = BdaData();
	Add(data.bda_calls, totals.bda_calls);
	Add(data.bda_mapped_ranges, totals.bda_mapped_ranges);
	Add(data.bda_entries_visited, totals.bda_entries_visited);
}

inline BdaTotals SnapshotBda() {
	const auto& data = BdaData();
	return {
	    data.bda_calls.load(std::memory_order_relaxed),
	    data.bda_mapped_ranges.load(std::memory_order_relaxed),
	    data.bda_entries_visited.load(std::memory_order_relaxed),
	};
}

// Attempts select a RefreshFlatBuffer path, regardless of its eventual result.
struct SrtTotals {
	uint64_t srt_native_attempts      = 0;
	uint64_t srt_interpreted_attempts = 0;
};

struct SrtCounters {
	std::atomic<uint64_t> srt_native_attempts {0};
	std::atomic<uint64_t> srt_interpreted_attempts {0};
};

inline SrtCounters& SrtData() {
	static SrtCounters counters;
	return counters;
}

// One increment per complete-plan refresh, never per DWORD or evaluated instruction.
inline void RecordSrtAttempt(bool native) {
	if (!Enabled()) return;
	auto& data    = SrtData();
	auto& counter = native ? data.srt_native_attempts : data.srt_interpreted_attempts;
	Add(counter, 1u);
}

inline SrtTotals SnapshotSrt() {
	const auto& data = SrtData();
	return {
	    data.srt_native_attempts.load(std::memory_order_relaxed),
	    data.srt_interpreted_attempts.load(std::memory_order_relaxed),
	};
}

// Counts maintenance entry and suspend-point queue/processing boundaries only.
struct MaintenanceTotals {
	uint64_t gc_runs                  = 0;
	uint64_t suspend_points_enqueued  = 0;
	uint64_t suspend_points_processed = 0;
};

struct MaintenanceCounters {
	std::atomic<uint64_t> gc_runs {0};
	std::atomic<uint64_t> suspend_points_enqueued {0};
	std::atomic<uint64_t> suspend_points_processed {0};
};

inline MaintenanceCounters& MaintenanceData() {
	static MaintenanceCounters counters;
	return counters;
}

inline void RecordGcRun() {
	if (Enabled()) Add(MaintenanceData().gc_runs, 1u);
}

inline void RecordSuspendPointEnqueued() {
	if (Enabled()) Add(MaintenanceData().suspend_points_enqueued, 1u);
}

inline void RecordSuspendPointProcessed() {
	if (Enabled()) Add(MaintenanceData().suspend_points_processed, 1u);
}

inline MaintenanceTotals SnapshotMaintenance() {
	const auto& data = MaintenanceData();
	return {
	    data.gc_runs.load(std::memory_order_relaxed),
	    data.suspend_points_enqueued.load(std::memory_order_relaxed),
	    data.suspend_points_processed.load(std::memory_order_relaxed),
	};
}
} // namespace ResourceProbe
