#pragma once

#include <atomic>
#include <cstdint>
#include <cstddef>
#include <cstdlib>

namespace Libs::Graphics {

// Experimental runtime switch: default OFF, exact value "1" enables the cache.
inline bool BdaSweepCacheEnabled() {
    static const bool enabled = [] {
#ifdef _MSC_VER
        char* value = nullptr;
        std::size_t length = 0;
        if (_dupenv_s(&value, &length, "KYTY_BDA_SWEEP_CACHE") != 0) return false;
        const bool result = value != nullptr && length == 2 && value[0] == '1';
        std::free(value);
        return result;
#else
        const char* value = std::getenv("KYTY_BDA_SWEEP_CACHE");
        return value != nullptr && value[0] == '1' && value[1] == '\0';
#endif
    }();
    return enabled;
}

// The guard starts before taking tracker locks and ends after flush/protection callbacks.
// Multiple and nested writers are supported; an odd/even seqlock is not sufficient here.
class CpuWriteEpoch final {
public:
    struct Snapshot {
        uint64_t generation = 0;
        bool quiescent = false;
    };

    class Guard final {
    public:
        explicit Guard(CpuWriteEpoch& epoch, bool enabled = true) noexcept
            : m_epoch(enabled ? &epoch : nullptr) {
            if (m_epoch != nullptr) m_epoch->m_writers.fetch_add(1);
        }
        ~Guard() {
            if (m_epoch != nullptr) {
                m_epoch->m_generation.fetch_add(1);
                m_epoch->m_writers.fetch_sub(1);
            }
        }
        Guard(const Guard&) = delete;
        Guard& operator=(const Guard&) = delete;

    private:
        CpuWriteEpoch* m_epoch;
    };

    [[nodiscard]] Snapshot Read() const noexcept {
        const auto before = m_generation.load();
        const auto writers = m_writers.load();
        const auto after = m_generation.load();
        return {after, writers == 0 && before == after};
    }

private:
    // Sequential consistency supplies a single order for the two independent atomics.
    // Do not weaken this protocol without updating its concurrent-interleaving proof.
    std::atomic<uint64_t> m_generation {0};
    std::atomic<uint64_t> m_writers {0};
};

static_assert(std::atomic<uint64_t>::is_always_lock_free);

class BdaSweepCache final {
public:
    struct Stamp {
        CpuWriteEpoch::Snapshot cpu;
        uint64_t buffers = 0;
        uint64_t mappings = 0;
    };

    [[nodiscard]] bool CanSkip(const Stamp& current) const noexcept {
        return m_valid && current.cpu.quiescent && Same(m_stamp, current);
    }

    void CompleteSweep(const Stamp& before, const Stamp& after) noexcept {
        // A write behind the traversal can dirty a buffer already visited. Never accept
        // a baseline merely because the writer has finished by the end of the sweep.
        m_valid = before.cpu.quiescent && after.cpu.quiescent && Same(before, after);
        m_stamp = after;
    }

private:
    static bool Same(const Stamp& first, const Stamp& second) noexcept {
        return first.cpu.generation == second.cpu.generation &&
               first.buffers == second.buffers && first.mappings == second.mappings;
    }

    Stamp m_stamp {};
    bool m_valid = false;
};

} // namespace Libs::Graphics
