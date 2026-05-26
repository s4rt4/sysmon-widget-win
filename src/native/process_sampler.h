#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include <windows.h>

// ProcessSampler enumerates all running processes and returns the top N
// ranked by (cpu_percent, working_set_bytes) descending.
//
// Two snapshots are needed to compute CPU% via GetProcessTimes delta, so
// call Sample() once at startup to prime the cache, then again on each
// refresh.
class ProcessSampler {
public:
    struct Entry {
        std::wstring name;
        DWORD pid = 0;
        double cpu_percent = 0.0;
        std::size_t working_set_bytes = 0;
    };

    std::vector<Entry> Sample(int top_n);

private:
    struct PrevSample {
        std::uint64_t kernel_time = 0;
        std::uint64_t user_time = 0;
        std::uint64_t wall_time = 0;
    };

    std::unordered_map<DWORD, PrevSample> prev_;
    int cpu_count_ = 0;  // cached on first call
};
