#include "process_sampler.h"

#include <algorithm>

#include <psapi.h>
#include <tlhelp32.h>

namespace {
std::uint64_t FileTimeToU64(const FILETIME& ft) {
    return (static_cast<std::uint64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
}
}  // namespace

std::vector<ProcessSampler::Entry> ProcessSampler::Sample(int top_n) {
    if (cpu_count_ == 0) {
        SYSTEM_INFO si{};
        GetSystemInfo(&si);
        cpu_count_ = static_cast<int>(si.dwNumberOfProcessors);
        if (cpu_count_ < 1) cpu_count_ = 1;
    }

    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) {
        return {};
    }

    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    if (!Process32FirstW(snap, &pe)) {
        CloseHandle(snap);
        return {};
    }

    FILETIME ft_now{};
    GetSystemTimeAsFileTime(&ft_now);
    const std::uint64_t now = FileTimeToU64(ft_now);

    std::vector<Entry> entries;
    std::unordered_map<DWORD, PrevSample> next_prev;
    entries.reserve(128);

    do {
        // Skip System Idle (PID 0) and System (PID 4) pseudo-processes.
        if (pe.th32ProcessID == 0 || pe.th32ProcessID == 4) {
            continue;
        }

        // Prefer combined limited-info + VM_READ so GetProcessMemoryInfo
        // works; fall back to limited-info-only when the OS denies VM_READ
        // (e.g. protected processes). In that case we still get times.
        HANDLE proc = OpenProcess(
            PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE, pe.th32ProcessID);
        bool has_mem_access = (proc != nullptr);
        if (!proc) {
            proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe.th32ProcessID);
        }
        if (!proc) {
            continue;
        }

        FILETIME creation{}, exit_t{}, kernel{}, user{};
        if (!GetProcessTimes(proc, &creation, &exit_t, &kernel, &user)) {
            CloseHandle(proc);
            continue;
        }

        std::size_t rss = 0;
        if (has_mem_access) {
            PROCESS_MEMORY_COUNTERS pmc{};
            if (GetProcessMemoryInfo(proc, &pmc, sizeof(pmc))) {
                rss = pmc.WorkingSetSize;
            }
        }
        CloseHandle(proc);

        const std::uint64_t k = FileTimeToU64(kernel);
        const std::uint64_t u = FileTimeToU64(user);

        PrevSample current{k, u, now};
        next_prev[pe.th32ProcessID] = current;

        double cpu_percent = 0.0;
        auto it = prev_.find(pe.th32ProcessID);
        if (it != prev_.end()) {
            const std::uint64_t wall_delta = now - it->second.wall_time;
            const std::uint64_t cpu_delta =
                (k + u) - (it->second.kernel_time + it->second.user_time);
            if (wall_delta > 0) {
                // CPU% is CPU time delta / wall delta / core count.
                cpu_percent = 100.0 * static_cast<double>(cpu_delta) /
                              static_cast<double>(wall_delta) /
                              static_cast<double>(cpu_count_);
                if (cpu_percent > 100.0) cpu_percent = 100.0;
                if (cpu_percent < 0.0)   cpu_percent = 0.0;
            }
        }

        Entry entry;
        entry.name = pe.szExeFile;
        entry.pid = pe.th32ProcessID;
        entry.cpu_percent = cpu_percent;
        entry.working_set_bytes = rss;
        entries.push_back(std::move(entry));
    } while (Process32NextW(snap, &pe));

    CloseHandle(snap);
    prev_ = std::move(next_prev);

    // Rank by CPU%, tie-break by RSS.
    std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
        if (a.cpu_percent != b.cpu_percent) return a.cpu_percent > b.cpu_percent;
        return a.working_set_bytes > b.working_set_bytes;
    });

    if (top_n > 0 && static_cast<int>(entries.size()) > top_n) {
        entries.resize(top_n);
    }
    return entries;
}
