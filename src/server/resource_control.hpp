#pragma once

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__APPLE__)
#include <mach/mach.h>
#include <sys/sysctl.h>
#endif

namespace lab {

constexpr std::uint64_t kMemoryMiB = 1024ULL * 1024ULL;

struct SystemMemorySnapshot {
    std::uint64_t totalBytes = 0;
    std::uint64_t availableBytes = 0;
    std::string source = "unknown";
};

namespace resource_control_detail {

inline std::string trim(const std::string& value) {
    const std::size_t first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    const std::size_t last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

inline std::string readTextFile(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return "";
    std::ostringstream out;
    out << input.rdbuf();
    return trim(out.str());
}

inline std::uint64_t parseUnsigned(const std::string& text) {
    if (text.empty() || text == "max") return 0;
    std::size_t used = 0;
    try {
        const unsigned long long value = std::stoull(text, &used, 10);
        return used == text.size() ? static_cast<std::uint64_t>(value) : 0;
    } catch (const std::exception&) {
        return 0;
    }
}

#if defined(__linux__)
inline SystemMemorySnapshot linuxHostMemory() {
    std::ifstream input("/proc/meminfo");
    std::string key;
    std::uint64_t value = 0;
    std::string unit;
    SystemMemorySnapshot snapshot;
    snapshot.source = "Linux /proc/meminfo";
    while (input >> key >> value >> unit) {
        if (key == "MemTotal:") snapshot.totalBytes = value * 1024ULL;
        else if (key == "MemAvailable:") snapshot.availableBytes = value * 1024ULL;
    }
    return snapshot;
}

struct CgroupMemory {
    std::uint64_t limitBytes = 0;
    std::uint64_t currentBytes = 0;
    std::string source;
};

inline CgroupMemory readCgroupPair(const std::filesystem::path& folder,
                                   const char* limitName,
                                   const char* currentName,
                                   const std::string& source) {
    CgroupMemory memory;
    memory.limitBytes = parseUnsigned(readTextFile(folder / limitName));
    memory.currentBytes = parseUnsigned(readTextFile(folder / currentName));
    if (memory.limitBytes > 0 && memory.limitBytes < (1ULL << 60)) memory.source = source;
    else memory.limitBytes = 0;
    return memory;
}

inline CgroupMemory linuxCgroupMemory() {
    std::ifstream input("/proc/self/cgroup");
    std::string line;
    std::string v2Path;
    std::string v1Path;
    while (std::getline(input, line)) {
        const std::size_t first = line.find(':');
        const std::size_t second = first == std::string::npos ? std::string::npos : line.find(':', first + 1);
        if (second == std::string::npos) continue;
        const std::string controllers = line.substr(first + 1, second - first - 1);
        const std::string path = line.substr(second + 1);
        if (controllers.empty()) v2Path = path;
        if (controllers.find("memory") != std::string::npos) v1Path = path;
    }

    if (!v2Path.empty()) {
        std::filesystem::path folder = "/sys/fs/cgroup";
        if (v2Path != "/") folder /= v2Path.substr(1);
        CgroupMemory memory = readCgroupPair(folder, "memory.max", "memory.current", "cgroup v2");
        if (memory.limitBytes > 0) return memory;
    }
    if (!v1Path.empty()) {
        std::filesystem::path folder = "/sys/fs/cgroup/memory";
        if (v1Path != "/") folder /= v1Path.substr(1);
        CgroupMemory memory = readCgroupPair(folder, "memory.limit_in_bytes", "memory.usage_in_bytes", "cgroup v1");
        if (memory.limitBytes > 0) return memory;
    }
    return CgroupMemory{};
}
#endif

}  // namespace resource_control_detail

inline SystemMemorySnapshot systemMemorySnapshot() {
#ifdef _WIN32
    MEMORYSTATUSEX state{};
    state.dwLength = sizeof(state);
    if (!GlobalMemoryStatusEx(&state)) return SystemMemorySnapshot{};
    return SystemMemorySnapshot{
        static_cast<std::uint64_t>(state.ullTotalPhys),
        static_cast<std::uint64_t>(state.ullAvailPhys),
        "Windows physical memory",
    };
#elif defined(__APPLE__)
    std::uint64_t total = 0;
    std::size_t totalSize = sizeof(total);
    sysctlbyname("hw.memsize", &total, &totalSize, nullptr, 0);
    mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
    vm_statistics64_data_t vm{};
    std::uint64_t available = 0;
    if (host_statistics64(mach_host_self(), HOST_VM_INFO64,
                          reinterpret_cast<host_info64_t>(&vm), &count) == KERN_SUCCESS) {
        vm_size_t pageSize = 0;
        host_page_size(mach_host_self(), &pageSize);
        available = static_cast<std::uint64_t>(vm.free_count + vm.inactive_count + vm.speculative_count) * pageSize;
    }
    return SystemMemorySnapshot{total, available, "macOS host memory"};
#elif defined(__linux__)
    SystemMemorySnapshot snapshot = resource_control_detail::linuxHostMemory();
    const resource_control_detail::CgroupMemory cgroup = resource_control_detail::linuxCgroupMemory();
    if (cgroup.limitBytes > 0) {
        const std::uint64_t cgroupAvailable = cgroup.currentBytes >= cgroup.limitBytes
            ? 0
            : cgroup.limitBytes - cgroup.currentBytes;
        if (snapshot.totalBytes == 0 || cgroup.limitBytes < snapshot.totalBytes) snapshot.totalBytes = cgroup.limitBytes;
        if (snapshot.availableBytes == 0 || cgroupAvailable < snapshot.availableBytes) snapshot.availableBytes = cgroupAvailable;
        snapshot.source += " + " + cgroup.source;
    }
    return snapshot;
#else
    return SystemMemorySnapshot{};
#endif
}

inline std::string configureServerOomProtection() {
#if defined(__linux__)
    std::ofstream output("/proc/self/oom_score_adj", std::ios::trunc);
    if (output) {
        output << -250;
        output.flush();
        if (output) return "Linux server oom_score_adj set to -250";
    }
    return "Linux server oom_score_adj could not be lowered; worker prioritization remains active";
#else
    return "platform worker isolation active";
#endif
}

class ResourceExhaustedError : public std::runtime_error {
public:
    explicit ResourceExhaustedError(const std::string& message) : std::runtime_error(message) {}
};

class ComputeCancelledError : public std::runtime_error {
public:
    ComputeCancelledError() : std::runtime_error("task was cancelled while waiting for compute resources") {}
};

struct MemoryDecision {
    bool ready = false;
    bool impossible = false;
    std::uint64_t workerLimitBytes = 0;
    std::uint64_t reserveBytes = 0;
    SystemMemorySnapshot snapshot;
    std::string reason;
};

class AdaptiveMemoryController {
public:
    AdaptiveMemoryController(std::uint64_t maximumWorkerBytes, std::uint64_t reserveBytes)
        : maximumWorkerBytes_(maximumWorkerBytes), configuredReserveBytes_(reserveBytes) {}

    MemoryDecision decision() const {
        constexpr std::uint64_t minimumWorkerBytes = 128ULL * kMemoryMiB;
        constexpr std::uint64_t automaticWorkerCap = 4ULL * 1024ULL * kMemoryMiB;
        MemoryDecision decision;
        decision.snapshot = systemMemorySnapshot();
        if (decision.snapshot.totalBytes == 0 || decision.snapshot.availableBytes == 0) {
            decision.impossible = true;
            decision.reason = "cannot determine available system memory; use --worker-memory-mb and --memory-reserve-mb";
            return decision;
        }

        decision.reserveBytes = configuredReserveBytes_ > 0
            ? configuredReserveBytes_
            : std::max<std::uint64_t>(512ULL * kMemoryMiB, decision.snapshot.totalBytes / 5);
        if (maximumWorkerBytes_ > 0 && maximumWorkerBytes_ < minimumWorkerBytes) {
            decision.impossible = true;
            decision.reason = "the configured worker memory limit is below 128 MiB";
            return decision;
        }
        if (decision.snapshot.totalBytes <= decision.reserveBytes + minimumWorkerBytes) {
            decision.impossible = true;
            decision.reason = "the effective memory limit is too small to preserve the server reserve";
            return decision;
        }
        if (decision.snapshot.availableBytes <= decision.reserveBytes + minimumWorkerBytes) {
            decision.reason = "waiting for available memory above the server reserve";
            return decision;
        }

        std::uint64_t limit = decision.snapshot.availableBytes - decision.reserveBytes;
        limit = std::min(limit, maximumWorkerBytes_ > 0 ? maximumWorkerBytes_ : automaticWorkerCap);
        limit -= limit % kMemoryMiB;
        if (limit < minimumWorkerBytes) {
            decision.reason = "available worker memory is below 128 MiB";
            return decision;
        }
        decision.ready = true;
        decision.workerLimitBytes = limit;
        return decision;
    }

    std::uint64_t waitForWorkerLimit(std::chrono::steady_clock::time_point deadline,
                                     const std::function<bool()>& cancelled) const {
        while (true) {
            if (cancelled && cancelled()) throw ComputeCancelledError();
            if (std::chrono::steady_clock::now() >= deadline) {
                throw ResourceExhaustedError("timed out while waiting for sufficient worker memory");
            }
            const MemoryDecision current = decision();
            if (current.impossible) throw ResourceExhaustedError(current.reason);
            if (current.ready) return current.workerLimitBytes;
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
    }

    bool emergencyPressure() const {
        const MemoryDecision current = decision();
        if (current.snapshot.availableBytes == 0) return false;
        const std::uint64_t reserve = current.reserveBytes > 0
            ? current.reserveBytes
            : configuredReserveBytes_;
        return reserve > 0 && current.snapshot.availableBytes < reserve / 2;
    }

    std::string status() const {
        const MemoryDecision current = decision();
        std::ostringstream out;
        out << "Memory source: " << current.snapshot.source
            << ", effective total " << current.snapshot.totalBytes / kMemoryMiB << " MiB"
            << ", available " << current.snapshot.availableBytes / kMemoryMiB << " MiB"
            << ", server reserve " << current.reserveBytes / kMemoryMiB << " MiB"
            << ", worker limit ";
        if (current.ready) out << current.workerLimitBytes / kMemoryMiB << " MiB";
        else out << "not currently available (" << current.reason << ")";
        return out.str();
    }

private:
    std::uint64_t maximumWorkerBytes_ = 0;
    std::uint64_t configuredReserveBytes_ = 0;
};

class ComputeQueue {
public:
    class Lease {
    public:
        Lease() = default;
        explicit Lease(ComputeQueue* owner) : owner_(owner) {}
        Lease(const Lease&) = delete;
        Lease& operator=(const Lease&) = delete;
        Lease(Lease&& other) noexcept : owner_(other.owner_) { other.owner_ = nullptr; }
        Lease& operator=(Lease&& other) noexcept {
            if (this != &other) {
                release();
                owner_ = other.owner_;
                other.owner_ = nullptr;
            }
            return *this;
        }
        ~Lease() { release(); }

    private:
        ComputeQueue* owner_ = nullptr;
        void release() {
            if (!owner_) return;
            owner_->release();
            owner_ = nullptr;
        }
    };

    explicit ComputeQueue(const AdaptiveMemoryController& memory) : memory_(memory) {}

    Lease acquire(std::chrono::steady_clock::time_point deadline,
                  const std::function<bool()>& cancelled,
                  const std::function<void()>& admitted) {
        auto waiter = std::make_shared<int>(0);
        std::unique_lock<std::mutex> lock(mutex_);
        if (waiters_.size() >= 16) {
            throw ResourceExhaustedError("the compute queue already contains 16 waiting tasks");
        }
        waiters_.push_back(waiter);
        while (true) {
            if (cancelled && cancelled()) {
                eraseWaiter(waiter);
                condition_.notify_all();
                throw ComputeCancelledError();
            }
            if (std::chrono::steady_clock::now() >= deadline) {
                eraseWaiter(waiter);
                condition_.notify_all();
                throw ResourceExhaustedError("timed out in the compute queue");
            }
            if (!active_ && !waiters_.empty() && waiters_.front() == waiter) {
                const MemoryDecision current = memory_.decision();
                if (current.impossible) {
                    waiters_.pop_front();
                    condition_.notify_all();
                    throw ResourceExhaustedError(current.reason);
                }
                if (current.ready) {
                    active_ = true;
                    waiters_.pop_front();
                    lock.unlock();
                    if (admitted) admitted();
                    return Lease(this);
                }
            }
            condition_.wait_for(lock, std::chrono::milliseconds(250));
        }
    }

private:
    const AdaptiveMemoryController& memory_;
    std::mutex mutex_;
    std::condition_variable condition_;
    std::deque<std::shared_ptr<int>> waiters_;
    bool active_ = false;

    void eraseWaiter(const std::shared_ptr<int>& waiter) {
        const auto found = std::find(waiters_.begin(), waiters_.end(), waiter);
        if (found != waiters_.end()) waiters_.erase(found);
    }

    void release() {
        std::lock_guard<std::mutex> lock(mutex_);
        active_ = false;
        condition_.notify_all();
    }
};

}  // namespace lab
