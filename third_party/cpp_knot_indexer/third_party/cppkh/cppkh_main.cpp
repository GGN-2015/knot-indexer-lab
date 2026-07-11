#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <new>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <unordered_map>
#include <utility>
#include <vector>

#if defined(KH_THREAD_BACKEND_BOOST)
#include <boost/thread.hpp>
#define KH_THREAD_BACKEND_NAME "boost"
#elif defined(KH_THREAD_BACKEND_STD)
#include <mutex>
#include <thread>
#define KH_THREAD_BACKEND_NAME "std"
#elif defined(KH_THREAD_BACKEND_PTHREAD)
#include <pthread.h>
#define KH_THREAD_BACKEND_NAME "pthread"
#elif defined(KH_THREAD_BACKEND_SINGLE)
#define KH_THREAD_BACKEND_NAME "single"
#elif defined(KH_THREAD_BACKEND_WIN32)
#define KH_THREAD_BACKEND_NAME "win32"
#elif defined(_WIN32)
#define KH_THREAD_BACKEND_WIN32
#define KH_THREAD_BACKEND_NAME "win32"
#else
#define KH_THREAD_BACKEND_PTHREAD
#include <pthread.h>
#define KH_THREAD_BACKEND_NAME "pthread"
#endif

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace kh {

class BigInt {
    static const uint32_t BASE = 1000000000u;
    int sign_ = 0;
    std::vector<uint32_t> d_;

    void trim() {
        while (!d_.empty() && d_.back() == 0) d_.pop_back();
        if (d_.empty()) sign_ = 0;
    }

    static int cmp_abs(const BigInt& a, const BigInt& b) {
        if (a.d_.size() != b.d_.size()) return a.d_.size() < b.d_.size() ? -1 : 1;
        for (int i = static_cast<int>(a.d_.size()) - 1; i >= 0; --i) {
            if (a.d_[i] != b.d_[i]) return a.d_[i] < b.d_[i] ? -1 : 1;
        }
        return 0;
    }

    static BigInt add_abs(const BigInt& a, const BigInt& b) {
        BigInt r;
        r.sign_ = 1;
        uint64_t carry = 0;
        size_t n = std::max(a.d_.size(), b.d_.size());
        r.d_.resize(n);
        for (size_t i = 0; i < n; ++i) {
            uint64_t cur = carry;
            if (i < a.d_.size()) cur += a.d_[i];
            if (i < b.d_.size()) cur += b.d_[i];
            r.d_[i] = static_cast<uint32_t>(cur % BASE);
            carry = cur / BASE;
        }
        if (carry) r.d_.push_back(static_cast<uint32_t>(carry));
        r.trim();
        return r;
    }

    static BigInt sub_abs(const BigInt& a, const BigInt& b) {
        BigInt r;
        r.sign_ = 1;
        r.d_.resize(a.d_.size());
        int64_t carry = 0;
        for (size_t i = 0; i < a.d_.size(); ++i) {
            int64_t cur = static_cast<int64_t>(a.d_[i]) - carry - (i < b.d_.size() ? b.d_[i] : 0);
            if (cur < 0) {
                cur += BASE;
                carry = 1;
            } else {
                carry = 0;
            }
            r.d_[i] = static_cast<uint32_t>(cur);
        }
        r.trim();
        return r;
    }

    void shift_base_add(uint32_t x) {
        if (sign_ == 0 && x == 0) return;
        d_.insert(d_.begin(), x);
        sign_ = 1;
        trim();
    }

public:
    BigInt() = default;
    BigInt(long long v) { *this = v; }

    BigInt& operator=(long long v) {
        d_.clear();
        if (v == 0) {
            sign_ = 0;
            return *this;
        }
        sign_ = v < 0 ? -1 : 1;
        unsigned long long x = v < 0 ? static_cast<unsigned long long>(-v) : static_cast<unsigned long long>(v);
        while (x) {
            d_.push_back(static_cast<uint32_t>(x % BASE));
            x /= BASE;
        }
        return *this;
    }

    bool isZero() const { return sign_ == 0; }
    int sign() const { return sign_; }
    BigInt abs() const { BigInt r = *this; if (r.sign_ < 0) r.sign_ = 1; return r; }
    bool isOneAbs() const { return d_.size() == 1 && d_[0] == 1; }

    std::string str() const {
        if (sign_ == 0) return "0";
        std::ostringstream out;
        if (sign_ < 0) out << '-';
        out << d_.back();
        for (int i = static_cast<int>(d_.size()) - 2; i >= 0; --i) {
            std::string s = std::to_string(d_[i]);
            out << std::string(9 - s.size(), '0') << s;
        }
        return out.str();
    }

    friend bool operator==(const BigInt& a, const BigInt& b) {
        return a.sign_ == b.sign_ && a.d_ == b.d_;
    }
    friend bool operator!=(const BigInt& a, const BigInt& b) { return !(a == b); }
    friend bool operator<(const BigInt& a, const BigInt& b) {
        if (a.sign_ != b.sign_) return a.sign_ < b.sign_;
        if (a.sign_ == 0) return false;
        int c = cmp_abs(a, b);
        return a.sign_ > 0 ? c < 0 : c > 0;
    }
    friend bool operator>(const BigInt& a, const BigInt& b) { return b < a; }
    friend bool operator<=(const BigInt& a, const BigInt& b) { return !(b < a); }
    friend bool operator>=(const BigInt& a, const BigInt& b) { return !(a < b); }

    BigInt operator-() const {
        BigInt r = *this;
        r.sign_ = -r.sign_;
        return r;
    }

    friend BigInt operator+(const BigInt& a, const BigInt& b) {
        if (a.sign_ == 0) return b;
        if (b.sign_ == 0) return a;
        if (a.sign_ == b.sign_) {
            BigInt r = add_abs(a, b);
            r.sign_ = a.sign_;
            return r;
        }
        int c = cmp_abs(a, b);
        if (c == 0) return BigInt(0);
        BigInt r = c > 0 ? sub_abs(a, b) : sub_abs(b, a);
        r.sign_ = c > 0 ? a.sign_ : b.sign_;
        return r;
    }

    friend BigInt operator-(const BigInt& a, const BigInt& b) { return a + (-b); }

    friend BigInt operator*(const BigInt& a, const BigInt& b) {
        if (a.sign_ == 0 || b.sign_ == 0) return BigInt(0);
        BigInt r;
        r.sign_ = a.sign_ * b.sign_;
        r.d_.assign(a.d_.size() + b.d_.size(), 0);
        for (size_t i = 0; i < a.d_.size(); ++i) {
            uint64_t carry = 0;
            for (size_t j = 0; j < b.d_.size() || carry; ++j) {
                uint64_t cur = r.d_[i + j] + carry;
                if (j < b.d_.size()) cur += static_cast<uint64_t>(a.d_[i]) * b.d_[j];
                r.d_[i + j] = static_cast<uint32_t>(cur % BASE);
                carry = cur / BASE;
            }
        }
        r.trim();
        return r;
    }

    BigInt mul_small(uint32_t m) const {
        if (sign_ == 0 || m == 0) return BigInt(0);
        BigInt r;
        r.sign_ = sign_;
        r.d_.resize(d_.size());
        uint64_t carry = 0;
        for (size_t i = 0; i < d_.size(); ++i) {
            uint64_t cur = carry + static_cast<uint64_t>(d_[i]) * m;
            r.d_[i] = static_cast<uint32_t>(cur % BASE);
            carry = cur / BASE;
        }
        if (carry) r.d_.push_back(static_cast<uint32_t>(carry));
        return r;
    }

    int mod_small(uint32_t m) const {
        uint64_t rem = 0;
        for (int i = static_cast<int>(d_.size()) - 1; i >= 0; --i) {
            rem = (rem * BASE + d_[i]) % m;
        }
        return sign_ < 0 && rem ? static_cast<int>(m - rem) : static_cast<int>(rem);
    }

    BigInt div_small(uint32_t m, uint32_t* remainder = nullptr) const {
        if (m == 0) throw std::runtime_error("division by zero");
        if (sign_ == 0) {
            if (remainder) *remainder = 0;
            return BigInt(0);
        }
        BigInt r;
        r.sign_ = sign_;
        r.d_.assign(d_.size(), 0);
        uint64_t rem = 0;
        for (int i = static_cast<int>(d_.size()) - 1; i >= 0; --i) {
            uint64_t cur = d_[i] + rem * BASE;
            r.d_[i] = static_cast<uint32_t>(cur / m);
            rem = cur % m;
        }
        r.trim();
        if (remainder) *remainder = static_cast<uint32_t>(rem);
        return r;
    }

    static std::pair<BigInt, BigInt> divmod(const BigInt& aa, const BigInt& bb) {
        if (bb.sign_ == 0) throw std::runtime_error("division by zero");
        if (aa.sign_ == 0) return std::make_pair(BigInt(0), BigInt(0));
        BigInt a = aa.abs(), b = bb.abs();
        if (cmp_abs(a, b) < 0) return std::make_pair(BigInt(0), aa);
        BigInt q, r;
        q.sign_ = 1;
        q.d_.assign(a.d_.size(), 0);
        for (int i = static_cast<int>(a.d_.size()) - 1; i >= 0; --i) {
            r.shift_base_add(a.d_[i]);
            uint32_t lo = 0, hi = BASE - 1, x = 0;
            while (lo <= hi) {
                uint32_t mid = lo + (hi - lo) / 2;
                BigInt prod = b.mul_small(mid);
                if (prod <= r) {
                    x = mid;
                    lo = mid + 1;
                } else {
                    if (mid == 0) break;
                    hi = mid - 1;
                }
            }
            q.d_[i] = x;
            if (x) r = r - b.mul_small(x);
        }
        q.sign_ = aa.sign_ * bb.sign_;
        q.trim();
        r.sign_ = aa.sign_;
        r.trim();
        return std::make_pair(q, r);
    }

    friend BigInt operator/(const BigInt& a, const BigInt& b) { return divmod(a, b).first; }
    friend BigInt operator%(const BigInt& a, const BigInt& b) { return divmod(a, b).second; }
};

static const BigInt BI_ZERO(0), BI_ONE(1), BI_MINUS_ONE(-1);

static BigInt coeffProduct(const BigInt& a, const BigInt& b) {
    if (a.isZero() || b.isZero()) return BI_ZERO;
    if (a == BI_ONE) return b;
    if (b == BI_ONE) return a;
    if (a == BI_MINUS_ONE) return -b;
    if (b == BI_MINUS_ONE) return -a;
    return a * b;
}

struct Options {
    int matrixThreads = 1;
    bool reorderCrossings = true;
    bool progress = true;
    bool simplifyPD = true;
    bool profile = false;
};

static Options g_options;
typedef std::vector<std::vector<int> > PDCode;

struct ProfileCounter {
    uint64_t ns = 0;
    uint64_t calls = 0;
};

struct ProfileStats {
    uint64_t totalNs = 0;
    ProfileCounter kh;
    ProfileCounter generateFast;
    ProfileCounter complexCompose;
    ProfileCounter complexReduce;
    ProfileCounter deLoop;
    ProfileCounter deLoopSetup;
    ProfileCounter deLoopPrev;
    ProfileCounter deLoopNext;
    ProfileCounter deLoopPrevTerms;
    ProfileCounter deLoopNextTerms;
    ProfileCounter matrixReduce;
    ProfileCounter matrixCompose;
    ProfileCounter blockReduction;
    ProfileCounter blockReductionApply;
    ProfileCounter lcccReduce;
    ProfileCounter lcccComposeVertical;
    ProfileCounter lcccComposeHorizontal;
    ProfileCounter cobComposeVertical;
    ProfileCounter cobComposeHorizontal;

    void reset() {
        *this = ProfileStats();
    }
};

static ProfileStats g_profile;

static uint64_t profileNowNs() {
#ifdef _WIN32
    static LARGE_INTEGER freq = []() {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        return f;
    }();
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    return static_cast<uint64_t>((static_cast<long double>(counter.QuadPart) * 1000000000.0L) /
                                 static_cast<long double>(freq.QuadPart));
#else
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
#endif
}

struct ProfileScope {
    ProfileCounter* counter;
    uint64_t start;

    explicit ProfileScope(ProfileCounter& c) : counter(g_options.profile ? &c : nullptr), start(0) {
        if (counter) {
            counter->calls++;
            start = profileNowNs();
        }
    }

    ~ProfileScope() {
        if (counter) counter->ns += profileNowNs() - start;
    }
};

#define KH_JOIN2(a, b) a##b
#define KH_JOIN(a, b) KH_JOIN2(a, b)
#define KH_PROFILE(counterName) ProfileScope KH_JOIN(khProfileScope_, __LINE__)(g_profile.counterName)

static std::string profileMs(uint64_t ns) {
    std::ostringstream out;
    out.setf(std::ios::fixed);
    out.precision(3);
    out << (static_cast<double>(ns) / 1000000.0);
    return out.str();
}

static void appendProfileCounter(std::ostream& out, const char* name, const ProfileCounter& c) {
    out << ' ' << name << "Ms=" << profileMs(c.ns)
        << ' ' << name << "Calls=" << c.calls;
}

static void printProfile() {
    if (!g_options.profile) return;
    std::cerr << "CPPKH_PROFILE totalMs=" << profileMs(g_profile.totalNs)
              << " khMs=" << profileMs(g_profile.kh.ns);
    appendProfileCounter(std::cerr, "generateFast", g_profile.generateFast);
    appendProfileCounter(std::cerr, "complexCompose", g_profile.complexCompose);
    appendProfileCounter(std::cerr, "complexReduce", g_profile.complexReduce);
    appendProfileCounter(std::cerr, "deLoop", g_profile.deLoop);
    appendProfileCounter(std::cerr, "deLoopSetup", g_profile.deLoopSetup);
    appendProfileCounter(std::cerr, "deLoopPrev", g_profile.deLoopPrev);
    appendProfileCounter(std::cerr, "deLoopNext", g_profile.deLoopNext);
    std::cerr << " deLoopPrevTermCount=" << g_profile.deLoopPrevTerms.ns
              << " deLoopPrevTermCalls=" << g_profile.deLoopPrevTerms.calls
              << " deLoopNextTermCount=" << g_profile.deLoopNextTerms.ns
              << " deLoopNextTermCalls=" << g_profile.deLoopNextTerms.calls;
    appendProfileCounter(std::cerr, "matrixReduce", g_profile.matrixReduce);
    appendProfileCounter(std::cerr, "matrixCompose", g_profile.matrixCompose);
    appendProfileCounter(std::cerr, "blockReduction", g_profile.blockReduction);
    appendProfileCounter(std::cerr, "blockReductionApply", g_profile.blockReductionApply);
    appendProfileCounter(std::cerr, "lcccReduce", g_profile.lcccReduce);
    appendProfileCounter(std::cerr, "lcccComposeVertical", g_profile.lcccComposeVertical);
    appendProfileCounter(std::cerr, "lcccComposeHorizontal", g_profile.lcccComposeHorizontal);
    appendProfileCounter(std::cerr, "cobComposeVertical", g_profile.cobComposeVertical);
    appendProfileCounter(std::cerr, "cobComposeHorizontal", g_profile.cobComposeHorizontal);
    std::cerr << "\n";
}

static int detectHardwareThreads() {
#ifdef _WIN32
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    return std::max(1, static_cast<int>(info.dwNumberOfProcessors));
#elif defined(KH_THREAD_BACKEND_STD)
    unsigned int n = std::thread::hardware_concurrency();
    return n == 0 ? 1 : static_cast<int>(n);
#elif defined(KH_THREAD_BACKEND_BOOST)
    unsigned int n = boost::thread::hardware_concurrency();
    return n == 0 ? 1 : static_cast<int>(n);
#elif !defined(_WIN32)
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    return n > 0 ? static_cast<int>(n) : 1;
#else
    return 1;
#endif
}

static int requestedMatrixThreads() {
    // JavaKh's own -P path is experimental and slower on the benchmark set.
    // The attempted row-parallel matrix path in this port also loses to the
    // single-threaded algorithm and can stress shared caches, so the core
    // computation intentionally stays serial for correctness and speed.
    return 1;
}

class SimpleMutex {
#if defined(KH_THREAD_BACKEND_BOOST)
    boost::mutex m_;
public:
    void lock() { m_.lock(); }
    void unlock() { m_.unlock(); }
#elif defined(KH_THREAD_BACKEND_STD)
    std::mutex m_;
public:
    void lock() { m_.lock(); }
    void unlock() { m_.unlock(); }
#elif defined(KH_THREAD_BACKEND_PTHREAD)
    pthread_mutex_t m_;
public:
    SimpleMutex() { pthread_mutex_init(&m_, NULL); }
    ~SimpleMutex() { pthread_mutex_destroy(&m_); }
    void lock() { pthread_mutex_lock(&m_); }
    void unlock() { pthread_mutex_unlock(&m_); }
#elif defined(KH_THREAD_BACKEND_WIN32)
    CRITICAL_SECTION cs_;
public:
    SimpleMutex() { InitializeCriticalSection(&cs_); }
    ~SimpleMutex() { DeleteCriticalSection(&cs_); }
    void lock() { EnterCriticalSection(&cs_); }
    void unlock() { LeaveCriticalSection(&cs_); }
#else
public:
    void lock() {}
    void unlock() {}
#endif
};

class SimpleLock {
    SimpleMutex& m_;
public:
    explicit SimpleLock(SimpleMutex& m) : m_(m) { m_.lock(); }
    ~SimpleLock() { m_.unlock(); }
};

#if !defined(KH_THREAD_BACKEND_SINGLE)
struct ParallelJob {
    int next = 0;
    int n = 0;
    int chunk = 1;
    SimpleMutex lock;
    std::function<void(int,int)> fn;
};

static void parallelWorkerLoop(ParallelJob* job) {
    while (true) {
        int begin;
        {
            SimpleLock guard(job->lock);
            begin = job->next;
            job->next += job->chunk;
        }
        if (begin >= job->n) break;
        job->fn(begin, std::min(job->n, begin + job->chunk));
    }
}

#if defined(KH_THREAD_BACKEND_PTHREAD)
static void* pthreadParallelWorker(void* ptr) {
    parallelWorkerLoop(static_cast<ParallelJob*>(ptr));
    return NULL;
}
#elif defined(KH_THREAD_BACKEND_WIN32)
static DWORD WINAPI win32ParallelWorker(LPVOID ptr) {
    parallelWorkerLoop(static_cast<ParallelJob*>(ptr));
    return 0;
}
#endif
#endif

static void parallelForWithThreads(int n, int threads, int minParallelRows, const std::function<void(int,int)>& fn) {
    threads = std::min(std::max(1, threads), std::max(1, n));
    if (threads <= 1 || n < minParallelRows) {
        fn(0, n);
        return;
    }
#if defined(KH_THREAD_BACKEND_BOOST)
    ParallelJob job;
    job.n = n;
    job.chunk = std::max(1, (n + threads * 4 - 1) / (threads * 4));
    job.fn = fn;
    std::vector<std::unique_ptr<boost::thread> > handles;
    handles.reserve(threads);
    for (int i = 0; i < threads; ++i) {
        handles.push_back(std::unique_ptr<boost::thread>(new boost::thread([&job]() { parallelWorkerLoop(&job); })));
    }
    for (size_t i = 0; i < handles.size(); ++i) handles[i]->join();
#elif defined(KH_THREAD_BACKEND_STD)
    ParallelJob job;
    job.n = n;
    job.chunk = std::max(1, (n + threads * 4 - 1) / (threads * 4));
    job.fn = fn;
    std::vector<std::thread> handles;
    handles.reserve(threads);
    for (int i = 0; i < threads; ++i) handles.push_back(std::thread([&job]() { parallelWorkerLoop(&job); }));
    for (size_t i = 0; i < handles.size(); ++i) handles[i].join();
#elif defined(KH_THREAD_BACKEND_PTHREAD)
    ParallelJob job;
    job.n = n;
    job.chunk = std::max(1, (n + threads * 4 - 1) / (threads * 4));
    job.fn = fn;
    std::vector<pthread_t> handles(threads);
    int created = 0;
    for (int i = 0; i < threads; ++i) {
        if (pthread_create(&handles[i], NULL, pthreadParallelWorker, &job) == 0) ++created;
        else break;
    }
    if (created == 0) {
        fn(0, n);
        return;
    }
    for (int i = 0; i < created; ++i) pthread_join(handles[i], NULL);
#elif defined(KH_THREAD_BACKEND_WIN32)
    threads = std::min(threads, 64);
    ParallelJob job;
    job.n = n;
    job.chunk = std::max(1, (n + threads * 4 - 1) / (threads * 4));
    job.fn = fn;
    std::vector<HANDLE> handles;
    handles.reserve(threads);
    for (int i = 0; i < threads; ++i) {
        HANDLE h = CreateThread(NULL, 0, win32ParallelWorker, &job, 0, NULL);
        if (h) handles.push_back(h);
    }
    if (handles.empty()) {
        fn(0, n);
        return;
    }
    WaitForMultipleObjects(static_cast<DWORD>(handles.size()), handles.data(), TRUE, INFINITE);
    for (HANDLE h : handles) CloseHandle(h);
#else
    fn(0, n);
#endif
}

static void parallelFor(int n, int minParallelRows, const std::function<void(int,int)>& fn) {
    parallelForWithThreads(n, requestedMatrixThreads(), minParallelRows, fn);
}

static uint64_t hashMix(uint64_t h, uint64_t v) {
    v += 0x9e3779b97f4a7c15ULL;
    v = (v ^ (v >> 30)) * 0xbf58476d1ce4e5b9ULL;
    v = (v ^ (v >> 27)) * 0x94d049bb133111ebULL;
    v ^= (v >> 31);
    h ^= v + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
    return h;
}

static uint64_t hashInt(uint64_t h, int v) {
    return hashMix(h, static_cast<uint64_t>(static_cast<int64_t>(v)));
}

template <typename T, typename Alloc>
static uint64_t hashIntVector(uint64_t h, const std::vector<T, Alloc>& values) {
    h = hashMix(h, values.size());
    for (T v : values) h = hashInt(h, static_cast<int>(v));
    return h;
}

typedef int8_t Small;

class SmallArena {
    struct Chunk {
        std::unique_ptr<unsigned char[]> data;
        size_t capacity = 0;
        explicit Chunk(size_t n) : data(new unsigned char[n]), capacity(n) {}
    };

    static const size_t defaultChunkSize = 4u * 1024u * 1024u;
    std::vector<Chunk> chunks;
    size_t chunkIndex = 0;
    size_t offset = 0;

public:
    void reset() {
        chunkIndex = 0;
        offset = 0;
    }

    void* allocate(size_t bytes, size_t alignment) {
        if (bytes == 0) bytes = 1;
        if (alignment == 0) alignment = 1;
        while (true) {
            if (chunkIndex >= chunks.size()) {
                size_t capacity = std::max(defaultChunkSize, bytes + alignment);
                chunks.push_back(Chunk(capacity));
                offset = 0;
            }
            Chunk& chunk = chunks[chunkIndex];
            uintptr_t base = reinterpret_cast<uintptr_t>(chunk.data.get());
            uintptr_t ptr = base + offset;
            uintptr_t aligned = (ptr + alignment - 1) & ~(static_cast<uintptr_t>(alignment) - 1);
            size_t alignedOffset = static_cast<size_t>(aligned - base);
            if (alignedOffset + bytes <= chunk.capacity) {
                offset = alignedOffset + bytes;
                return reinterpret_cast<void*>(aligned);
            }
            ++chunkIndex;
            offset = 0;
        }
    }
};

static SmallArena g_smallArena;

template <typename T>
struct ArenaAllocator {
    typedef T value_type;

    ArenaAllocator() noexcept {}
    template <typename U> ArenaAllocator(const ArenaAllocator<U>&) noexcept {}

    T* allocate(std::size_t n) {
        if (n > static_cast<std::size_t>(-1) / sizeof(T)) throw std::bad_alloc();
        return static_cast<T*>(g_smallArena.allocate(n * sizeof(T), alignof(T)));
    }

    void deallocate(T*, std::size_t) noexcept {}

    template <typename U>
    struct rebind { typedef ArenaAllocator<U> other; };
};

template <typename T, typename U>
static bool operator==(const ArenaAllocator<T>&, const ArenaAllocator<U>&) { return true; }

template <typename T, typename U>
static bool operator!=(const ArenaAllocator<T>&, const ArenaAllocator<U>&) { return false; }

typedef std::vector<Small, ArenaAllocator<Small> > SmallVec;
typedef std::vector<int, ArenaAllocator<int> > ArenaIntVec;

template <size_t InlineN>
struct IntBuffer {
    size_t n = 0;
    std::array<int, InlineN> inlineData;
    std::vector<int> heapData;

    explicit IntBuffer(size_t count = 0, int value = 0) : n(count) {
        if (n <= InlineN) std::fill(inlineData.begin(), inlineData.begin() + static_cast<std::ptrdiff_t>(n), value);
        else heapData.assign(n, value);
    }

    size_t size() const { return n; }

    int& operator[](size_t i) {
        return heapData.empty() ? inlineData[i] : heapData[i];
    }

    const int& operator[](size_t i) const {
        return heapData.empty() ? inlineData[i] : heapData[i];
    }
};

typedef IntBuffer<256> WorkInts;

struct Cap;
typedef std::shared_ptr<Cap> CapPtr;

struct Cap {
    int n = 0;
    int ncycles = 0;
    std::vector<int> pairings;
    mutable std::string keyCache;
    mutable bool hashReady = false;
    mutable uint64_t hashCache = 0;

    Cap() = default;
    Cap(int nn, int cycles) : n(nn), ncycles(cycles), pairings(nn, 0) {}

    const std::string& key() const {
        if (!keyCache.empty()) return keyCache;
        std::ostringstream out;
        out << n << ':' << ncycles << ':';
        for (size_t i = 0; i < pairings.size(); ++i) out << pairings[i] << ',';
        keyCache = out.str();
        return keyCache;
    }

    bool equals(const Cap& c) const {
        return n == c.n && ncycles == c.ncycles && pairings == c.pairings;
    }

    uint64_t hash() const {
        if (hashReady) return hashCache;
        uint64_t h = 1469598103934665603ULL;
        h = hashInt(h, n);
        h = hashInt(h, ncycles);
        h = hashIntVector(h, pairings);
        hashCache = h;
        hashReady = true;
        return hashCache;
    }

    CapPtr compose(int start, const CapPtr& c, int cstart, int nc, std::vector<int>* joins = nullptr) const;
};

static SimpleMutex g_capMutex;
static std::unordered_map<uint64_t, std::vector<CapPtr> > g_capCache;

static CapPtr cacheCapNoLock(const Cap& cap) {
    uint64_t h = cap.hash();
    std::vector<CapPtr>& bucket = g_capCache[h];
    for (size_t i = 0; i < bucket.size(); ++i) {
        if (bucket[i]->equals(cap)) return bucket[i];
    }
    CapPtr p = std::make_shared<Cap>(cap);
    bucket.push_back(p);
    return p;
}

static CapPtr cacheCap(const Cap& cap) {
    if (requestedMatrixThreads() <= 1) {
        return cacheCapNoLock(cap);
    } else {
        SimpleLock lock(g_capMutex);
        return cacheCapNoLock(cap);
    }
}

static CapPtr compose2Cap(const Cap& a, int start, const Cap& c, int cstart, int nc, std::vector<int>& joins) {
    Cap ret(a.n + c.n - 2 * nc, a.ncycles + c.ncycles);
    for (int i = 0; i < a.n - nc; i++) {
        int ii = (i + start + nc) % a.n;
        ret.pairings[i] = (a.pairings[ii] - start - nc + 2 * a.n) % a.n;
        if (ret.pairings[i] >= a.n - nc) ret.pairings[i] = -1 - (ret.pairings[i] - a.n + nc);
    }
    std::vector<int> thisjoins(nc);
    for (int i = 0; i < nc; i++) {
        int ii = (i + start) % a.n;
        thisjoins[i] = (a.pairings[ii] - start - nc + 2 * a.n) % a.n;
        if (thisjoins[i] >= a.n - nc) thisjoins[i] = -1 - (thisjoins[i] - a.n + nc);
    }
    for (int i = 0; i < c.n - nc; i++) {
        int ii = (i + cstart + nc) % c.n;
        int j = i + a.n - nc;
        ret.pairings[j] = (c.pairings[ii] - cstart - nc + 2 * c.n) % c.n;
        if (ret.pairings[j] >= c.n - nc) ret.pairings[j] = -1 - (c.n - ret.pairings[j] - 1);
        else ret.pairings[j] += a.n - nc;
    }
    std::vector<int> cjoins(nc);
    for (int i = 0; i < nc; i++) {
        int ii = (cstart + nc - 1 - i + c.n) % c.n;
        cjoins[i] = (c.pairings[ii] - cstart - nc + 2 * c.n) % c.n;
        if (cjoins[i] >= c.n - nc) cjoins[i] = -1 - (c.n - cjoins[i] - 1);
        else cjoins[i] += a.n - nc;
    }
    std::vector<char> joinsdone(nc, 0);
    for (int i = 0; i < a.n - nc; i++) {
        if (ret.pairings[i] < 0) {
            while (ret.pairings[i] < 0) {
                int j = -1 - ret.pairings[i];
                joinsdone[j] = 1;
                ret.pairings[i] = cjoins[j];
                if (ret.pairings[i] < 0) {
                    j = -1 - ret.pairings[i];
                    joinsdone[j] = 1;
                    ret.pairings[i] = thisjoins[j];
                }
            }
            ret.pairings[ret.pairings[i]] = i;
        }
    }
    for (int i = a.n - nc; i < ret.n; i++) {
        if (ret.pairings[i] < 0) {
            while (ret.pairings[i] < 0) {
                int j = -1 - ret.pairings[i];
                joinsdone[j] = 1;
                ret.pairings[i] = thisjoins[j];
                if (ret.pairings[i] < 0) {
                    j = -1 - ret.pairings[i];
                    joinsdone[j] = 1;
                    ret.pairings[i] = cjoins[j];
                }
            }
            ret.pairings[ret.pairings[i]] = i;
        }
    }
    std::fill(joins.begin(), joins.end(), 0);
    for (int i = 0; i < nc; i++) if (joinsdone[i]) joins[i] = -1;
    int nnew = 0;
    for (int i = 0; i < nc; i++) if (!joinsdone[i]) {
        int j = i;
        do {
            joinsdone[j] = 1;
            joins[j] = nnew;
            j = -1 - thisjoins[j];
            joinsdone[j] = 1;
            joins[j] = nnew;
            j = -1 - cjoins[j];
        } while (j != i);
        ret.ncycles++;
        nnew++;
    }
    return cacheCap(ret);
}

CapPtr Cap::compose(int start, const CapPtr& c, int cstart, int nc, std::vector<int>* joins) const {
    std::vector<int> local(nc);
    CapPtr ret = compose2Cap(*this, start, *c, cstart, nc, local);
    if (joins) *joins = local;
    return ret;
}

struct Cobordism;
typedef std::shared_ptr<Cobordism> CobPtr;

struct Cobordism {
    int n = 0;
    int hpower = 0;
    CapPtr top, bottom;
    int nbc = 0;
    int offtop = 0, offbot = 0;
    SmallVec component;
    int ncc = 0;
    SmallVec connectedComponent;
    SmallVec dots;
    SmallVec genus;
    mutable bool mapsReady = false;
    mutable std::string keyCache;
    mutable bool hashReady = false;
    mutable uint64_t hashCache = 0;
    mutable ArenaIntVec boundaryOffset;
    mutable SmallVec boundaryItems;
    mutable ArenaIntVec edgeOffset;
    mutable SmallVec edgeItems;

    Cobordism() = default;
    Cobordism(const CapPtr& t, const CapPtr& b) : top(t), bottom(b) {
        if (b->n != t->n) throw std::runtime_error("cobordism cap size mismatch");
        n = t->n;
        component.assign(n, static_cast<Small>(-1));
        nbc = 0;
        for (int i = 0; i < n; ++i) if (component[i] == -1) {
            int j = i;
            do {
                component[j] = static_cast<Small>(nbc);
                j = top->pairings[j];
                component[j] = static_cast<Small>(nbc);
                j = bottom->pairings[j];
            } while (j != i);
            ++nbc;
        }
        offtop = nbc;
        nbc += top->ncycles;
        offbot = nbc;
        nbc += bottom->ncycles;
        connectedComponent.assign(nbc, 0);
    }

    const std::string& key() const {
        if (!keyCache.empty()) return keyCache;
        std::ostringstream out;
        out << top->key() << '|' << bottom->key() << '|' << n << '|' << hpower << '|';
        for (size_t i = 0; i < component.size(); ++i) out << static_cast<int>(component[i]) << ',';
        out << '|';
        for (size_t i = 0; i < connectedComponent.size(); ++i) out << static_cast<int>(connectedComponent[i]) << ',';
        out << '|';
        for (size_t i = 0; i < dots.size(); ++i) out << static_cast<int>(dots[i]) << ',';
        out << '|';
        for (size_t i = 0; i < genus.size(); ++i) out << static_cast<int>(genus[i]) << ',';
        keyCache = out.str();
        return keyCache;
    }

    void reverseMaps() const {
        if (mapsReady) return;
        boundaryOffset.assign(ncc + 1, 0);
        for (int i = 0; i < nbc; ++i) ++boundaryOffset[connectedComponent[i] + 1];
        for (int i = 1; i <= ncc; ++i) boundaryOffset[i] += boundaryOffset[i - 1];
        boundaryItems.resize(nbc);
        WorkInts boundaryCursor(ncc);
        for (int i = 0; i < ncc; ++i) boundaryCursor[i] = boundaryOffset[i];
        for (int i = 0; i < nbc; ++i) {
            int cc = connectedComponent[i];
            boundaryItems[boundaryCursor[cc]++] = static_cast<Small>(i);
        }

        edgeOffset.assign(offtop + 1, 0);
        for (int i = 0; i < n; ++i) ++edgeOffset[component[i] + 1];
        for (int i = 1; i <= offtop; ++i) edgeOffset[i] += edgeOffset[i - 1];
        edgeItems.resize(n);
        WorkInts edgeCursor(offtop);
        for (int i = 0; i < offtop; ++i) edgeCursor[i] = edgeOffset[i];
        for (int i = 0; i < n; ++i) {
            int c = component[i];
            edgeItems[edgeCursor[c]++] = static_cast<Small>(i);
        }
        mapsReady = true;
    }

    int boundaryBegin(int i) const { return boundaryOffset[i]; }
    int boundaryEnd(int i) const { return boundaryOffset[i + 1]; }
    int boundaryCount(int i) const { return boundaryOffset[i + 1] - boundaryOffset[i]; }
    int boundaryAt(int pos) const { return boundaryItems[pos]; }
    int edgeBegin(int i) const { return edgeOffset[i]; }
    int edgeEnd(int i) const { return edgeOffset[i + 1]; }
    int edgeCount(int i) const { return edgeOffset[i + 1] - edgeOffset[i]; }
    int edgeAt(int pos) const { return edgeItems[pos]; }

    bool isIsomorphism() const {
        if (!top->equals(*bottom)) return false;
        if (top->ncycles != 0) throw std::runtime_error("isomorphism check before delooping");
        if (nbc != ncc || hpower != 0) return false;
        for (int i = 0; i < nbc; ++i) {
            if (connectedComponent[i] != i || dots[i] != 0 || genus[i] != 0) return false;
        }
        return true;
    }

    bool equals(const Cobordism& c) const {
        return n == c.n
            && hpower == c.hpower
            && (top.get() == c.top.get() || top->equals(*c.top))
            && (bottom.get() == c.bottom.get() || bottom->equals(*c.bottom))
            && nbc == c.nbc
            && offtop == c.offtop
            && offbot == c.offbot
            && component == c.component
            && ncc == c.ncc
            && connectedComponent == c.connectedComponent
            && dots == c.dots
            && genus == c.genus;
    }

    uint64_t hash() const {
        if (hashReady) return hashCache;
        uint64_t h = 1469598103934665603ULL;
        h = hashMix(h, top->hash());
        h = hashMix(h, bottom->hash());
        h = hashInt(h, n);
        h = hashInt(h, hpower);
        h = hashInt(h, nbc);
        h = hashInt(h, offtop);
        h = hashInt(h, offbot);
        h = hashIntVector(h, component);
        h = hashInt(h, ncc);
        h = hashIntVector(h, connectedComponent);
        h = hashIntVector(h, dots);
        h = hashIntVector(h, genus);
        hashCache = h;
        hashReady = true;
        return hashCache;
    }

    CobPtr composeVertical(const CobPtr& cc) const { return composeVerticalPtr(cc.get()); }
    CobPtr composeVerticalPtr(const Cobordism* cc) const;
    CobPtr composeHorizontal(int start, const CobPtr& cc, int cstart, int nc) const;
};

template <typename... Args>
static CobPtr makeCob(Args&&... args) {
    return std::make_shared<Cobordism>(std::forward<Args>(args)...);
}

static SimpleMutex g_cobMutex;
static std::unordered_map<uint64_t, std::vector<CobPtr> > g_cobCache;

static CobPtr cacheCobNoLock(const CobPtr& cob) {
#ifdef KH_DISABLE_COB_CACHE
    return cob;
#else
    uint64_t h = cob->hash();
    std::vector<CobPtr>& bucket = g_cobCache[h];
    for (size_t i = 0; i < bucket.size(); ++i) {
        if (bucket[i]->equals(*cob)) return bucket[i];
    }
    bucket.push_back(cob);
    return cob;
#endif
}

static CobPtr cacheCob(const CobPtr& cob) {
    if (requestedMatrixThreads() <= 1) {
        return cacheCobNoLock(cob);
    } else {
        SimpleLock lock(g_cobMutex);
        return cacheCobNoLock(cob);
    }
}

static void flushCobCache() {
#ifndef KH_DISABLE_COB_CACHE
    g_cobCache.clear();
#endif
}

static std::vector<int> counting(int n) {
    std::vector<int> r(n);
    for (int i = 0; i < n; ++i) r[i] = i;
    return r;
}

static SmallVec countingSmall(int n) {
    SmallVec r(n);
    for (int i = 0; i < n; ++i) r[i] = static_cast<Small>(i);
    return r;
}

static CobPtr isomorphism(const CapPtr& c) {
    if (c->ncycles != 0) throw std::runtime_error("cycles in cap not supported by isomorphism");
    CobPtr ret = makeCob(c, c);
    ret->ncc = ret->nbc;
    ret->connectedComponent = countingSmall(ret->nbc);
    ret->dots.assign(ret->ncc, 0);
    ret->genus.assign(ret->ncc, 0);
    return cacheCob(ret);
}

template <typename MidBuffer, typename DotBuffer>
static void mergeRet(SmallVec& retcc, MidBuffer& mid, DotBuffer& rdots, int from, int to) {
    if (from == to) return;
    for (size_t i = 0; i < retcc.size(); ++i) if (retcc[i] == from) retcc[i] = static_cast<Small>(to);
    for (size_t i = 0; i < mid.size(); ++i) if (mid[i] == from) mid[i] = to;
    if (from >= 0 && to >= 0) rdots[to] += rdots[from];
}

CobPtr Cobordism::composeVerticalPtr(const Cobordism* cc) const {
    KH_PROFILE(cobComposeVertical);
    if (!top->equals(*cc->bottom)) throw std::runtime_error("vertical cobordism source mismatch");
    reverseMaps();
    cc->reverseMaps();
    CobPtr ret = makeCob(cc->top, bottom);
    ret->hpower = hpower + cc->hpower;
    ret->connectedComponent = countingSmall(ret->nbc);
    WorkInts midConComp(top->ncycles);
    for (int i = 0; i < top->ncycles; ++i) midConComp[i] = -2 - i;
    WorkInts rdots(ncc + cc->ncc + ret->nbc + top->ncycles + 4, 0);
    WorkInts mdots(ncc + cc->ncc + ret->nbc + top->ncycles + 4, 0);
    WorkInts udots(ncc + cc->ncc + ret->nbc + 4, 0);
    WorkInts ugenus(ncc + cc->ncc + ret->nbc + 4, 0);
    int unconnected = 0;

    for (int i = 0; i < ncc; ++i) {
        int reti = -1;
        for (int jj = boundaryBegin(i); jj < boundaryEnd(i); ++jj) {
            int k = boundaryAt(jj);
            if (k < offtop) {
                for (int l = edgeBegin(k); l < edgeEnd(k); ++l) {
                    reti = ret->connectedComponent[ret->component[edgeAt(l)]];
                    if (reti >= 0) break;
                }
            } else if (k < offbot) {
                reti = midConComp[k - offtop];
            } else {
                reti = ret->connectedComponent[k - offbot + ret->offbot];
            }
            if (reti >= 0) break;
        }
        if (reti == -1) {
            ugenus[unconnected] = genus[i];
            udots[unconnected++] = dots[i];
            continue;
        } else if (reti >= 0) rdots[reti] += dots[i];
        else mdots[-2 - reti] += dots[i];

        for (int jj = boundaryBegin(i); jj < boundaryEnd(i); ++jj) {
            int bc = boundaryAt(jj);
            if (bc < offtop) {
                for (int kk = edgeBegin(bc); kk < edgeEnd(bc); ++kk) {
                    int test = ret->connectedComponent[ret->component[edgeAt(kk)]];
                    if (test != reti) mergeRet(ret->connectedComponent, midConComp, rdots, test, reti);
                }
            } else if (bc < offbot) {
                int mtest = midConComp[bc - offtop];
                if (mtest != reti) {
                    if (mtest >= 0) mergeRet(ret->connectedComponent, midConComp, rdots, mtest, reti);
                    else for (size_t kk = 0; kk < midConComp.size(); ++kk) if (midConComp[kk] == mtest) midConComp[kk] = reti;
                    if (reti >= 0) rdots[reti] += (mtest >= 0 ? 0 : mdots[-2 - mtest]);
                    else if (mtest >= 0) mdots[-2 - reti] += rdots[mtest];
                    else mdots[-2 - reti] += mdots[-2 - mtest];
                }
            } else {
                int rtest = ret->connectedComponent[bc - offbot + ret->offbot];
                if (rtest != reti) mergeRet(ret->connectedComponent, midConComp, rdots, rtest, reti);
            }
        }
    }

    for (int i = 0; i < cc->ncc; ++i) {
        int reti = -1;
        for (int jj = cc->boundaryBegin(i); jj < cc->boundaryEnd(i); ++jj) {
            int k = cc->boundaryAt(jj);
            if (k < cc->offtop) {
                for (int l = cc->edgeBegin(k); l < cc->edgeEnd(k); ++l) {
                    reti = ret->connectedComponent[ret->component[cc->edgeAt(l)]];
                    if (reti >= 0) break;
                }
            } else if (k < cc->offbot) {
                reti = ret->connectedComponent[k - cc->offtop + ret->offtop];
            } else {
                reti = midConComp[k - cc->offbot];
            }
            if (reti >= 0) break;
        }
        if (reti == -1) {
            ugenus[unconnected] = cc->genus[i];
            udots[unconnected++] = cc->dots[i];
            continue;
        } else if (reti >= 0) rdots[reti] += cc->dots[i];
        else mdots[-2 - reti] += cc->dots[i];

        for (int jj = cc->boundaryBegin(i); jj < cc->boundaryEnd(i); ++jj) {
            int bc = cc->boundaryAt(jj);
            if (bc < cc->offtop) {
                for (int kk = cc->edgeBegin(bc); kk < cc->edgeEnd(bc); ++kk) {
                    int test = ret->connectedComponent[ret->component[cc->edgeAt(kk)]];
                    if (test != reti) mergeRet(ret->connectedComponent, midConComp, rdots, test, reti);
                }
            } else if (bc < cc->offbot) {
                int rtest = ret->connectedComponent[bc - cc->offtop + ret->offtop];
                if (rtest != reti) mergeRet(ret->connectedComponent, midConComp, rdots, rtest, reti);
            } else {
                int mtest = midConComp[bc - cc->offbot];
                if (mtest != reti) {
                    if (mtest >= 0) mergeRet(ret->connectedComponent, midConComp, rdots, mtest, reti);
                    else for (size_t kk = 0; kk < midConComp.size(); ++kk) if (midConComp[kk] == mtest) midConComp[kk] = reti;
                    if (reti >= 0) rdots[reti] += (mtest >= 0 ? 0 : mdots[-2 - mtest]);
                    else if (mtest >= 0) mdots[-2 - reti] += rdots[mtest];
                    else mdots[-2 - reti] += mdots[-2 - mtest];
                }
            }
        }
    }

    ret->ncc = 0;
    for (int i = 0; i < ret->nbc; ++i) {
        if (ret->connectedComponent[i] > ret->ncc) {
            int old = ret->connectedComponent[i];
            for (int k = i; k < ret->nbc; ++k) {
                if (ret->connectedComponent[k] == old) ret->connectedComponent[k] = ret->ncc;
                else if (ret->connectedComponent[k] == ret->ncc) ret->connectedComponent[k] = old;
            }
            for (size_t k = 0; k < midConComp.size(); ++k) {
                if (midConComp[k] == old) midConComp[k] = ret->ncc;
                else if (midConComp[k] == ret->ncc) midConComp[k] = old;
            }
            std::swap(rdots[ret->ncc], rdots[old]);
            ret->ncc++;
        } else if (ret->connectedComponent[i] == ret->ncc) {
            ret->ncc++;
        }
    }

    ret->reverseMaps();
    WorkInts rgenus(ret->ncc, 0);
    for (int i = 0; i < ret->ncc; ++i) {
        int b = ret->boundaryCount(i);
        int x = 0;
        for (int j = 0; j < ncc; ++j) {
            bool used = false;
            for (int kk = boundaryBegin(j); kk < boundaryEnd(j) && !used; ++kk) {
                int bc = boundaryAt(kk);
                if (bc < offtop) {
                    for (int l = edgeBegin(bc); l < edgeEnd(bc); ++l)
                        if (ret->connectedComponent[ret->component[edgeAt(l)]] == i) { used = true; break; }
                } else if (bc < offbot) used = midConComp[bc - offtop] == i;
                else used = ret->connectedComponent[bc - offbot + ret->offbot] == i;
            }
            if (used) {
                x += 2 - 2 * genus[j] - boundaryCount(j);
                int njoins = 0;
                for (int l = boundaryBegin(j); l < boundaryEnd(j); ++l) {
                    int bc = boundaryAt(l);
                    if (bc < offtop) njoins += edgeCount(bc);
                    else break;
                }
                x -= njoins / 2;
            }
        }
        for (int j = 0; j < cc->ncc; ++j) {
            bool used = false;
            for (int kk = cc->boundaryBegin(j); kk < cc->boundaryEnd(j) && !used; ++kk) {
                int bc = cc->boundaryAt(kk);
                if (bc < cc->offtop) {
                    for (int l = cc->edgeBegin(bc); l < cc->edgeEnd(bc); ++l)
                        if (ret->connectedComponent[ret->component[cc->edgeAt(l)]] == i) { used = true; break; }
                } else if (bc < cc->offbot) used = ret->connectedComponent[bc - cc->offtop + ret->offtop] == i;
                else used = midConComp[bc - cc->offbot] == i;
            }
            if (used) x += 2 - 2 * cc->genus[j] - cc->boundaryCount(j);
        }
        int g = 2 - b - x;
        if (g % 2 != 0 || g < 0) throw std::runtime_error("invalid vertical genus");
        rgenus[i] = g / 2;
    }

    for (int i = 0; i < static_cast<int>(midConComp.size()); ++i) {
        bool found = false;
        int g = 1;
        for (size_t j = 0; j < midConComp.size(); ++j) if (midConComp[j] == -2 - i) { found = true; g++; }
        if (found) {
            for (int j = 0; j < ncc; ++j)
                for (int k = offtop; k < offbot; ++k)
                    if (midConComp[k - offtop] == -2 - i && connectedComponent[k] == j) { g += genus[j] - 1; break; }
            for (int j = 0; j < cc->ncc; ++j)
                for (int k = cc->offbot; k < cc->nbc; ++k)
                    if (midConComp[k - cc->offbot] == -2 - i && cc->connectedComponent[k] == j) { g += cc->genus[j] - 1; break; }
            ugenus[unconnected] = g;
            udots[unconnected++] = mdots[i];
        }
    }

    int rncc = ret->ncc;
    ret->ncc += unconnected;
    ret->dots.assign(ret->ncc, 0);
    ret->genus.assign(ret->ncc, 0);
    for (int i = 0; i < rncc; ++i) {
        ret->dots[i] = rdots[i];
        ret->genus[i] = rgenus[i];
    }
    for (int i = 0; i < unconnected; ++i) {
        ret->dots[rncc + i] = udots[i];
        ret->genus[rncc + i] = ugenus[i];
    }
    if (unconnected > 0) ret->mapsReady = false;
    return cacheCob(ret);
}

CobPtr Cobordism::composeHorizontal(int start, const CobPtr& cc, int cstart, int nc) const {
    KH_PROFILE(cobComposeHorizontal);
    std::vector<int> tjoins(nc), bjoins(nc);
    CapPtr rtop = top->compose(start, cc->top, cstart, nc, &tjoins);
    CapPtr rbot = bottom->compose(start, cc->bottom, cstart, nc, &bjoins);
    CobPtr ret = makeCob(rtop, rbot);
    ret->hpower = hpower + cc->hpower;
    ret->connectedComponent = countingSmall(ret->nbc);
    WorkInts midConComp(nc);
    for (int i = 0; i < nc; ++i) midConComp[i] = -2 - i;
    WorkInts rdots(ret->nbc + nc + 2, 0), mdots(nc, 0), udots(ncc + cc->ncc + 2, 0), ugenus(ncc + cc->ncc + 2, 0);
    int unconnected = 0;

    for (int i = 0; i < ncc; ++i) {
        int reti = -1;
        for (int j = offtop; j < nbc; ++j) if (connectedComponent[j] == i) {
            int retj = j < offbot ? j - offtop + ret->offtop : j - offbot + ret->offbot;
            reti = ret->connectedComponent[retj];
            break;
        }
        if (reti == -1) {
            for (int j = 0; j < n; ++j) {
                int thisj = (j + start + nc) % n;
                if (connectedComponent[component[thisj]] == i) {
                    reti = j < n - nc ? ret->connectedComponent[ret->component[j]] : midConComp[j - n + nc];
                    if (reti >= 0) break;
                }
            }
        }
        if (reti >= 0) rdots[reti] += dots[i];
        else if (reti < -1) mdots[-2 - reti] += dots[i];
        if (reti != -1) {
            for (int j = 0; j < offtop; ++j) if (connectedComponent[j] == i) {
                for (int k = 0; k < n; ++k) if (component[k] == j) {
                    int retk = (k - start - nc + 2 * n) % n;
                    int tmp = retk < n - nc ? ret->connectedComponent[ret->component[retk]] : midConComp[retk - n + nc];
                    if (tmp != reti) {
                        if (tmp >= 0) mergeRet(ret->connectedComponent, midConComp, rdots, tmp, reti);
                        else for (int l = 0; l < nc; ++l) if (midConComp[l] == tmp) midConComp[l] = reti;
                        if (tmp >= 0) {
                            if (reti < 0) mdots[-2 - reti] += rdots[tmp];
                        } else if (reti >= 0) rdots[reti] += mdots[-2 - tmp];
                        else mdots[-2 - reti] += mdots[-2 - tmp];
                    }
                }
            }
        }
        if (reti >= 0) {
            for (int j = offtop; j < nbc; ++j) if (connectedComponent[j] == i) {
                int retj = j < offbot ? j - offtop + ret->offtop : j - offbot + ret->offbot;
                if (ret->connectedComponent[retj] != reti) {
                    int tmp = ret->connectedComponent[retj];
                    mergeRet(ret->connectedComponent, midConComp, rdots, tmp, reti);
                }
            }
        }
        if (reti == -1) {
            ugenus[unconnected] = genus[i];
            udots[unconnected++] = dots[i];
        }
    }

    for (int i = 0; i < cc->ncc; ++i) {
        int reti = -1;
        for (int j = cc->offtop; j < cc->nbc; ++j) if (cc->connectedComponent[j] == i) {
            int retj = j < cc->offbot ? j - cc->offtop + offbot - offtop + ret->offtop : j - cc->offbot + nbc - offbot + ret->offbot;
            reti = ret->connectedComponent[retj];
            break;
        }
        if (reti == -1) {
            for (int j = 0; j < cc->n; ++j) {
                int thisj = (j + cstart + nc) % cc->n;
                if (cc->connectedComponent[cc->component[thisj]] == i) {
                    reti = j < cc->n - nc ? ret->connectedComponent[ret->component[j + n - nc]] : midConComp[cc->n - j - 1];
                    if (reti >= 0) break;
                }
            }
        }
        if (reti >= 0) rdots[reti] += cc->dots[i];
        else if (reti < -1) mdots[-2 - reti] += cc->dots[i];
        if (reti != -1) {
            for (int j = 0; j < cc->offtop; ++j) if (cc->connectedComponent[j] == i) {
                for (int k = 0; k < cc->n; ++k) if (cc->component[k] == j) {
                    int retk = (k - cstart - nc + 2 * cc->n) % cc->n;
                    int tmp = retk < cc->n - nc ? ret->connectedComponent[ret->component[retk + n - nc]] : midConComp[cc->n - retk - 1];
                    if (tmp != reti) {
                        if (tmp >= 0) mergeRet(ret->connectedComponent, midConComp, rdots, tmp, reti);
                        else for (int l = 0; l < nc; ++l) if (midConComp[l] == tmp) midConComp[l] = reti;
                        if (tmp >= 0) {
                            if (reti < 0) mdots[-2 - reti] += rdots[tmp];
                        } else if (reti >= 0) rdots[reti] += mdots[-2 - tmp];
                        else mdots[-2 - reti] += mdots[-2 - tmp];
                    }
                }
            }
        }
        if (reti >= 0) {
            for (int j = cc->offtop; j < cc->nbc; ++j) if (cc->connectedComponent[j] == i) {
                int retj = j < cc->offbot ? j - cc->offtop + offbot - offtop + ret->offtop : j - cc->offbot + (nbc - offbot) + ret->offbot;
                if (ret->connectedComponent[retj] != reti) {
                    int tmp = ret->connectedComponent[retj];
                    mergeRet(ret->connectedComponent, midConComp, rdots, tmp, reti);
                }
            }
        }
        if (reti == -1) {
            ugenus[unconnected] = cc->genus[i];
            udots[unconnected++] = cc->dots[i];
        }
    }

    for (int i = 0; i < nc; ++i) {
        bool found = false;
        for (int j = 0; j < nc; ++j) if (tjoins[j] == i) {
            int reti = ret->offbot - 1 - i;
            if (midConComp[j] >= 0) ret->connectedComponent[reti] = midConComp[j];
            else {
                ret->connectedComponent[reti] = ret->nbc - midConComp[j];
                rdots[ret->nbc - midConComp[j]] = mdots[-2 - midConComp[j]];
                midConComp[j] = ret->nbc - midConComp[j];
            }
            found = true;
            break;
        }
        if (!found) break;
    }
    for (int i = 0; i < nc; ++i) {
        bool found = false;
        for (int j = 0; j < nc; ++j) if (bjoins[j] == i) {
            int reti = ret->nbc - 1 - i;
            if (midConComp[j] >= 0) ret->connectedComponent[reti] = midConComp[j];
            else {
                ret->connectedComponent[reti] = ret->nbc - midConComp[j];
                rdots[ret->nbc - midConComp[j]] = mdots[-2 - midConComp[j]];
                midConComp[j] = ret->nbc - midConComp[j];
            }
            found = true;
            break;
        }
        if (!found) break;
    }

    WorkInts rgenus(ret->nbc + nc + 2, 0);
    for (int i = 0; i < static_cast<int>(rgenus.size()); ++i) {
        int b = 0;
        for (int j = 0; j < ret->nbc; ++j) if (ret->connectedComponent[j] == i) ++b;
        if (b == 0) continue;
        int x = 0;
        for (int j = 0; j < ncc; ++j) {
            bool used = false;
            for (int k = 0; k < nbc && !used; ++k) if (connectedComponent[k] == j) {
                if (k < offtop) {
                    for (int l = 0; l < n; ++l) if (component[l] == k) {
                        int retl = (l - start - nc + 2 * n) % n;
                        if (retl < n - nc) used = ret->connectedComponent[ret->component[retl]] == i;
                        else used = midConComp[retl - n + nc] == i;
                        if (used) break;
                    }
                } else if (k < offbot) used = ret->connectedComponent[k - offtop + ret->offtop] == i;
                else used = ret->connectedComponent[k - offbot + ret->offbot] == i;
            }
            if (used) {
                x += 2 - 2 * genus[j];
                for (int l = 0; l < nbc; ++l) if (connectedComponent[l] == j) --x;
                int njoins = 0;
                for (int l = 0; l < nc; ++l) if (connectedComponent[component[(l + start) % n]] == j) ++njoins;
                x -= njoins;
            }
        }
        for (int j = 0; j < cc->ncc; ++j) {
            bool used = false;
            for (int k = 0; k < cc->nbc && !used; ++k) if (cc->connectedComponent[k] == j) {
                if (k < cc->offtop) {
                    for (int l = 0; l < cc->n; ++l) if (cc->component[l] == k) {
                        int retl = (l - cstart - nc + 2 * cc->n) % cc->n;
                        if (retl < cc->n - nc) used = ret->connectedComponent[ret->component[retl + n - nc]] == i;
                        else used = midConComp[cc->n - retl - 1] == i;
                        if (used) break;
                    }
                } else if (k < cc->offbot) used = ret->connectedComponent[k - cc->offtop + offbot - offtop + ret->offtop] == i;
                else used = ret->connectedComponent[k - cc->offbot + nbc - offbot + ret->offbot] == i;
            }
            if (used) {
                x += 2 - 2 * cc->genus[j];
                for (int l = 0; l < cc->nbc; ++l) if (cc->connectedComponent[l] == j) --x;
            }
        }
        int g = 2 - b - x;
        if (g % 2 != 0 || g < 0) throw std::runtime_error("invalid horizontal genus");
        rgenus[i] = g / 2;
    }

    ret->ncc = 0;
    for (int i = 0; i < ret->nbc; ++i) {
        if (ret->connectedComponent[i] > ret->ncc) {
            int old = ret->connectedComponent[i];
            for (int k = i; k < ret->nbc; ++k) {
                if (ret->connectedComponent[k] == old) ret->connectedComponent[k] = ret->ncc;
                else if (ret->connectedComponent[k] == ret->ncc) ret->connectedComponent[k] = old;
            }
            std::swap(rdots[ret->ncc], rdots[old]);
            std::swap(rgenus[ret->ncc], rgenus[old]);
            ret->ncc++;
        } else if (ret->connectedComponent[i] == ret->ncc) ret->ncc++;
    }
    std::vector<std::pair<int,int> > sortarr;
    for (int i = 0; i < unconnected; ++i) sortarr.push_back(std::make_pair(ugenus[i], udots[i]));
    std::sort(sortarr.begin(), sortarr.end());
    int rncc = ret->ncc;
    ret->ncc += unconnected;
    ret->dots.assign(ret->ncc, 0);
    ret->genus.assign(ret->ncc, 0);
    for (int i = 0; i < rncc; ++i) {
        ret->dots[i] = rdots[i];
        ret->genus[i] = rgenus[i];
    }
    for (int i = 0; i < unconnected; ++i) {
        ret->genus[rncc + i] = sortarr[i].first;
        ret->dots[rncc + i] = sortarr[i].second;
    }
    return cacheCob(ret);
}

struct TermList {
    typedef std::pair<CobPtr, BigInt> Term;
    size_t n = 0;
    Term first;
    std::vector<Term, ArenaAllocator<Term> > rest;

    bool empty() const { return n == 0; }
    size_t size() const { return n; }
    Term& front() { return first; }
    const Term& front() const { return first; }

    Term& operator[](size_t i) {
        return i == 0 ? first : rest[i - 1];
    }

    const Term& operator[](size_t i) const {
        return i == 0 ? first : rest[i - 1];
    }

    void reserve(size_t count) {
        if (count > 1) rest.reserve(count - 1);
    }

    void push_back(const Term& term) {
        if (n == 0) first = term;
        else rest.push_back(term);
        ++n;
    }

    void push_back(Term&& term) {
        if (n == 0) first = std::move(term);
        else rest.push_back(std::move(term));
        ++n;
    }

    void eraseIndex(size_t i) {
        if (i >= n) return;
        if (n == 1) {
            first = Term();
            n = 0;
            rest.clear();
            return;
        }
        if (i == 0) {
            first = std::move(rest.front());
            rest.erase(rest.begin());
        } else {
            rest.erase(rest.begin() + static_cast<std::ptrdiff_t>(i - 1));
        }
        --n;
    }
};

struct LCCC {
    TermList terms;
    bool alreadyReduced = false;

    LCCC() = default;
    LCCC(const CobPtr& cc, const BigInt& c) { addTerm(cc, c); }
    bool isZero() const { return terms.empty(); }
    int numberOfTerms() const { return static_cast<int>(terms.size()); }
    CobPtr firstTerm() const { return terms.front().first; }
    BigInt firstCoefficient() const { return terms.front().second; }
    CapPtr source() const { return firstTerm()->top; }
    CapPtr target() const { return firstTerm()->bottom; }

    void addTerm(const CobPtr& cc, const BigInt& c) {
        alreadyReduced = false;
        if (c.isZero()) return;
        for (size_t i = 0; i < terms.size(); ++i) {
            if (terms[i].first.get() == cc.get()) {
                BigInt n = terms[i].second + c;
                if (n.isZero()) terms.eraseIndex(i);
                else terms[i].second = n;
                return;
            }
        }
        uint64_t hash = cc->hash();
        for (size_t i = 0; i < terms.size(); ++i) {
            if (terms[i].first->hash() == hash && terms[i].first->equals(*cc)) {
                BigInt n = terms[i].second + c;
                if (n.isZero()) terms.eraseIndex(i);
                else terms[i].second = n;
                return;
            }
        }
        terms.push_back(std::make_pair(cc, c));
    }

    void add(const LCCC& other) {
        alreadyReduced = false;
        for (size_t i = 0; i < other.terms.size(); ++i) addTerm(other.terms[i].first, other.terms[i].second);
    }

    static LCCC composeCobLeft(const Cobordism& left, const LCCC& other) {
        LCCC ret;
        if (other.isZero()) return ret;
        if (other.terms.size() == 1) {
            ret.terms.push_back(std::make_pair(left.composeVerticalPtr(other.terms[0].first.get()), other.terms[0].second));
            return ret;
        }
        ret.terms.reserve(other.terms.size());
        for (size_t i = 0; i < other.terms.size(); ++i) {
            ret.addTerm(left.composeVerticalPtr(other.terms[i].first.get()), other.terms[i].second);
        }
        return ret;
    }

    static LCCC composeCobRight(const LCCC& left, const Cobordism& right) {
        LCCC ret;
        if (left.isZero()) return ret;
        if (left.terms.size() == 1) {
            ret.terms.push_back(std::make_pair(left.terms[0].first->composeVerticalPtr(&right), left.terms[0].second));
            return ret;
        }
        ret.terms.reserve(left.terms.size());
        for (size_t i = 0; i < left.terms.size(); ++i) {
            ret.addTerm(left.terms[i].first->composeVerticalPtr(&right), left.terms[i].second);
        }
        return ret;
    }

    LCCC multiplied(const BigInt& c) const {
        LCCC r;
        if (c.isZero()) return r;
        r.terms.reserve(terms.size());
        for (size_t i = 0; i < terms.size(); ++i) r.terms.push_back(std::make_pair(terms[i].first, coeffProduct(terms[i].second, c)));
        r.alreadyReduced = alreadyReduced;
        return r;
    }

    void multiplyInPlace(const BigInt& c) {
        if (c == BI_ONE) return;
        alreadyReduced = false;
        if (c == BI_MINUS_ONE) {
            for (size_t i = 0; i < terms.size(); ++i) terms[i].second = -terms[i].second;
        } else {
            for (size_t i = 0; i < terms.size(); ++i) terms[i].second = coeffProduct(terms[i].second, c);
        }
    }

    LCCC composeVertical(const LCCC& other) const {
        KH_PROFILE(lcccComposeVertical);
        LCCC ret;
        if (isZero() || other.isZero()) return ret;
        if (terms.size() == 1 && other.terms.size() == 1) {
            BigInt coeff = coeffProduct(terms[0].second, other.terms[0].second);
            if (!coeff.isZero()) {
                ret.terms.push_back(std::make_pair(terms[0].first->composeVerticalPtr(other.terms[0].first.get()), coeff));
            }
            return ret;
        }
        ret.terms.reserve(terms.size() * other.terms.size());
        for (size_t i = 0; i < terms.size(); ++i) {
            for (size_t j = 0; j < other.terms.size(); ++j) {
                ret.addTerm(terms[i].first->composeVerticalPtr(other.terms[j].first.get()), coeffProduct(terms[i].second, other.terms[j].second));
            }
        }
        return ret;
    }

    LCCC composeHorizontal(int start, const CobPtr& cc, int cstart, int nc, bool reverse) const {
        KH_PROFILE(lcccComposeHorizontal);
        LCCC ret;
        if (terms.size() == 1) {
            CobPtr composition = reverse ? cc->composeHorizontal(cstart, terms[0].first, start, nc)
                                         : terms[0].first->composeHorizontal(start, cc, cstart, nc);
            ret.terms.push_back(std::make_pair(composition, terms[0].second));
            return ret;
        }
        for (size_t i = 0; i < terms.size(); ++i) {
            CobPtr composition = reverse ? cc->composeHorizontal(cstart, terms[i].first, start, nc)
                                         : terms[i].first->composeHorizontal(start, cc, cstart, nc);
            ret.addTerm(composition, terms[i].second);
        }
        return ret;
    }

    LCCC reduce() const {
        KH_PROFILE(lcccReduce);
        if (isZero()) return LCCC();
        if (alreadyReduced) return *this;
        LCCC ret;
        for (size_t ti = 0; ti < terms.size(); ++ti) {
            CobPtr cc = terms[ti].first;
            BigInt num = terms[ti].second;
            bool canonical = cc->hpower == 0 && cc->ncc == cc->nbc;
            if (canonical) {
                for (int i = 0; i < cc->nbc; ++i) {
                    if (cc->connectedComponent[i] != i || cc->genus[i] != 0 || cc->dots[i] > 1) {
                        canonical = false;
                        break;
                    }
                }
                if (canonical) {
                    if (ret.isZero()) ret.terms.push_back(std::make_pair(cc, num));
                    else ret.addTerm(cc, num);
                    continue;
                }
            }
            cc->reverseMaps();
            SmallVec dots(cc->nbc, 0);
            SmallVec genus(cc->nbc, 0);
            WorkInts moreWork(cc->ncc);
            int moreWorkCount = 0;
            bool kill = false;
            for (int i = 0; i < cc->ncc; ++i) {
                int bcCount = cc->boundaryCount(i);
                if (cc->genus[i] + cc->dots[i] > 1) kill = true;
                else if (bcCount == 0) {
                    if (cc->genus[i] == 1) num = num.mul_small(2);
                    else if (cc->dots[i] == 0) kill = true;
                } else if (bcCount == 1) {
                    dots[cc->boundaryAt(cc->boundaryBegin(i))] = static_cast<Small>(cc->dots[i] + cc->genus[i]);
                    if (cc->genus[i] == 1) num = num.mul_small(2);
                } else {
                    if (cc->genus[i] + cc->dots[i] == 1) {
                        if (cc->genus[i] == 1) num = num.mul_small(2);
                        for (int p = cc->boundaryBegin(i); p < cc->boundaryEnd(i); ++p) dots[cc->boundaryAt(p)] = 1;
                    } else {
                        moreWork[moreWorkCount++] = i;
                    }
                }
                if (kill) break;
            }
            if (kill) continue;
            SmallVec connected = countingSmall(cc->nbc);
            if (moreWorkCount == 0) {
                CobPtr newcc = makeCob(cc->top, cc->bottom);
                newcc->connectedComponent = connected;
                newcc->ncc = newcc->nbc;
                newcc->genus = genus;
                newcc->dots = dots;
                CobPtr cached = cacheCob(newcc);
                if (ret.isZero()) ret.terms.push_back(std::make_pair(cached, num));
                else ret.addTerm(cached, num);
                continue;
            }
            std::vector<SmallVec, ArenaAllocator<SmallVec> > neckCutting;
            neckCutting.push_back(dots);
            for (int wi = 0; wi < moreWorkCount; ++wi) {
                int concomp = moreWork[wi];
                int nbc = cc->boundaryCount(concomp);
                std::vector<SmallVec, ArenaAllocator<SmallVec> > next;
                next.reserve(neckCutting.size() * static_cast<size_t>(nbc));
                for (size_t j = 0; j < neckCutting.size(); ++j) {
                    SmallVec base = neckCutting[j];
                    for (int p = cc->boundaryBegin(concomp); p < cc->boundaryEnd(concomp); ++p) base[cc->boundaryAt(p)] = 1;
                    for (int k = 0; k < nbc; ++k) {
                        SmallVec variant = base;
                        variant[cc->boundaryAt(cc->boundaryBegin(concomp) + k)] = 0;
                        next.push_back(std::move(variant));
                    }
                }
                neckCutting.swap(next);
            }
            for (size_t i = 0; i < neckCutting.size(); ++i) {
                CobPtr newcc = makeCob(cc->top, cc->bottom);
                newcc->connectedComponent = connected;
                newcc->ncc = newcc->nbc;
                newcc->genus = genus;
                newcc->dots = neckCutting[i];
                ret.addTerm(cacheCob(newcc), num);
            }
        }
        ret.alreadyReduced = true;
        return ret;
    }
};

struct SmoothingColumn {
    int n = 0;
    std::vector<CapPtr> smoothings;
    std::vector<int> numbers;
    SmoothingColumn() = default;
    explicit SmoothingColumn(int nn) : n(nn), smoothings(nn), numbers(nn, 0) {}
    bool nonNull() const {
        for (size_t i = 0; i < smoothings.size(); ++i) if (!smoothings[i]) return false;
        return true;
    }
};

typedef std::pair<int, LCCC> MatrixEntry;
typedef std::vector<MatrixEntry, ArenaAllocator<MatrixEntry> > MatrixRow;

static MatrixRow::iterator findRowEntry(MatrixRow& row, int column) {
    return std::lower_bound(row.begin(), row.end(), column,
        [](const std::pair<int, LCCC>& entry, int key) { return entry.first < key; });
}

static MatrixRow::const_iterator findRowEntry(const MatrixRow& row, int column) {
    return std::lower_bound(row.begin(), row.end(), column,
        [](const std::pair<int, LCCC>& entry, int key) { return entry.first < key; });
}

static void putRowEntry(MatrixRow& row, int column, const LCCC& lc) {
    if (lc.isZero()) return;
    MatrixRow::iterator it = findRowEntry(row, column);
    if (it != row.end() && it->first == column) it->second = lc;
    else row.insert(it, std::make_pair(column, lc));
}

static void addRowEntry(MatrixRow& row, int column, const LCCC& lc) {
    if (lc.isZero()) return;
    MatrixRow::iterator it = findRowEntry(row, column);
    if (it == row.end() || it->first != column) {
        row.insert(it, std::make_pair(column, lc));
    } else {
        it->second.add(lc);
        if (it->second.isZero()) row.erase(it);
    }
}

static void appendSortedRowEntry(MatrixRow& row, int column, const LCCC& lc) {
    if (lc.isZero()) return;
    if (row.empty() || row.back().first < column) row.push_back(std::make_pair(column, lc));
    else addRowEntry(row, column, lc);
}

static void appendSortedRowEntry(MatrixRow& row, int column, LCCC&& lc) {
    if (lc.isZero()) return;
    if (row.empty() || row.back().first < column) row.push_back(std::make_pair(column, std::move(lc)));
    else addRowEntry(row, column, lc);
}

static void addRowEntry(MatrixRow& row, int column, LCCC&& lc) {
    if (lc.isZero()) return;
    MatrixRow::iterator it = findRowEntry(row, column);
    if (it == row.end() || it->first != column) {
        row.insert(it, std::make_pair(column, std::move(lc)));
    } else {
        it->second.add(lc);
        if (it->second.isZero()) row.erase(it);
    }
}

static void addSortedRow(MatrixRow& row, const MatrixRow& other) {
    if (other.empty()) return;
    if (row.empty()) {
        row = other;
        return;
    }
    MatrixRow merged;
    merged.reserve(row.size() + other.size());
    size_t i = 0, j = 0;
    while (i < row.size() || j < other.size()) {
        if (j == other.size() || (i < row.size() && row[i].first < other[j].first)) {
            merged.push_back(row[i++]);
        } else if (i == row.size() || other[j].first < row[i].first) {
            merged.push_back(other[j++]);
        } else {
            LCCC sum = row[i].second;
            sum.add(other[j].second);
            if (!sum.isZero()) merged.push_back(std::make_pair(row[i].first, sum));
            ++i;
            ++j;
        }
    }
    row.swap(merged);
}

static void decrementIndexesAbove(MatrixRow& row, int column) {
    for (size_t i = 0; i < row.size(); ++i) {
        if (row[i].first > column) --row[i].first;
    }
}

struct CobMatrix {
    SmoothingColumn source, target;
    std::vector<MatrixRow> entries;

    CobMatrix() = default;
    CobMatrix(const SmoothingColumn& s, const SmoothingColumn& t) : source(s), target(t), entries(t.n) {}

    void putEntry(int i, int j, const LCCC& lc) {
        putRowEntry(entries[i], j, lc);
    }

    void addEntry(int i, int j, const LCCC& lc) {
        addRowEntry(entries[i], j, lc);
    }

    std::vector<LCCC> unpackRow(int i) const {
        std::vector<LCCC> row(source.n);
        for (auto& kv : entries[i]) row[kv.first] = kv.second;
        return row;
    }

    CobMatrix compose(const CobMatrix& cm) const {
        KH_PROFILE(matrixCompose);
        CobMatrix ret(cm.source, target);
        auto work = [&](int begin, int end) {
            for (int i = begin; i < end; ++i) {
                MatrixRow result;
                for (auto& ij : entries[i]) {
                    int j = ij.first;
                    auto& cmrow = cm.entries[j];
                    for (auto& jk : cmrow) {
                        LCCC lc = ij.second.composeVertical(jk.second);
                        if (!lc.isZero()) addRowEntry(result, jk.first, std::move(lc));
                    }
                }
                ret.entries[i].swap(result);
            }
        };
        parallelFor(target.n, 8, work);
        return ret;
    }

    void add(const CobMatrix& that) {
        for (size_t i = 0; i < entries.size(); ++i) {
            addSortedRow(entries[i], that.entries[i]);
        }
    }

    void reduce() {
        KH_PROFILE(matrixReduce);
        auto work = [&](int begin, int end) {
            for (int j = begin; j < end; ++j) {
                MatrixRow& row = entries[j];
                bool allReduced = true;
                for (size_t idx = 0; idx < row.size(); ++idx) {
                    if (!row[idx].second.alreadyReduced) {
                        allReduced = false;
                        break;
                    }
                }
                if (allReduced) continue;
                MatrixRow reduced;
                reduced.reserve(row.size());
                for (size_t idx = 0; idx < row.size(); ++idx) {
                    if (row[idx].second.alreadyReduced) {
                        reduced.push_back(std::move(row[idx]));
                        continue;
                    }
                    LCCC rlc = row[idx].second.reduce();
                    if (!rlc.isZero()) reduced.push_back(std::make_pair(row[idx].first, std::move(rlc)));
                }
                row.swap(reduced);
            }
        };
        parallelFor(static_cast<int>(entries.size()), 16, work);
    }

    bool isZero() const {
        for (auto& row : entries) if (!row.empty()) return false;
        return true;
    }

    CobMatrix extractColumn(int column) {
        SmoothingColumn sc(1);
        sc.smoothings[0] = source.smoothings[column];
        sc.numbers[0] = source.numbers[column];
        source.smoothings.erase(source.smoothings.begin() + column);
        source.numbers.erase(source.numbers.begin() + column);
        source.n--;
        CobMatrix result(sc, target);
        for (size_t a = 0; a < entries.size(); ++a) {
            MatrixRow::iterator it = findRowEntry(entries[a], column);
            if (it != entries[a].end() && it->first == column) {
                putRowEntry(result.entries[a], 0, it->second);
                entries[a].erase(it);
            }
            decrementIndexesAbove(entries[a], column);
        }
        return result;
    }

    CobMatrix extractRow(int row) {
        SmoothingColumn sc(1);
        sc.smoothings[0] = target.smoothings[row];
        sc.numbers[0] = target.numbers[row];
        target.smoothings.erase(target.smoothings.begin() + row);
        target.numbers.erase(target.numbers.begin() + row);
        target.n--;
        CobMatrix result(source, sc);
        result.entries[0] = entries[row];
        entries.erase(entries.begin() + row);
        return result;
    }

    CobMatrix extractColumns(const std::vector<int>& columns) {
        int oldSourceN = source.n;
        std::vector<int> selectedIndex(oldSourceN, -1);
        std::vector<char> selected(oldSourceN, 0);
        for (size_t i = 0; i < columns.size(); ++i) {
            selectedIndex[columns[i]] = static_cast<int>(i);
            selected[columns[i]] = 1;
        }
        std::vector<int> removedBefore(oldSourceN + 1, 0);
        for (int c = 0; c < oldSourceN; ++c) removedBefore[c + 1] = removedBefore[c] + (selected[c] ? 1 : 0);
        SmoothingColumn sc(static_cast<int>(columns.size()));
        for (size_t i = 0; i < columns.size(); ++i) {
            sc.smoothings[i] = source.smoothings[columns[i]];
            sc.numbers[i] = source.numbers[columns[i]];
        }
        std::vector<CapPtr> newSmoothings;
        std::vector<int> newNumbers;
        newSmoothings.reserve(oldSourceN - columns.size());
        newNumbers.reserve(oldSourceN - columns.size());
        for (int c = 0; c < oldSourceN; ++c) {
            if (!selected[c]) {
                newSmoothings.push_back(source.smoothings[c]);
                newNumbers.push_back(source.numbers[c]);
            }
        }
        source.smoothings.swap(newSmoothings);
        source.numbers.swap(newNumbers);
        source.n -= static_cast<int>(columns.size());
        CobMatrix result(sc, target);
        for (size_t r = 0; r < entries.size(); ++r) {
            MatrixRow remaining;
            remaining.reserve(entries[r].size());
            for (size_t e = 0; e < entries[r].size(); ++e) {
                int oldColumn = entries[r][e].first;
                if (selected[oldColumn]) {
                    putRowEntry(result.entries[r], selectedIndex[oldColumn], entries[r][e].second);
                } else {
                    entries[r][e].first = oldColumn - removedBefore[oldColumn];
                    remaining.push_back(std::move(entries[r][e]));
                }
            }
            entries[r].swap(remaining);
        }
        return result;
    }

    CobMatrix extractRows(const std::vector<int>& rows) {
        int oldTargetN = target.n;
        std::vector<int> selectedIndex(oldTargetN, -1);
        std::vector<char> selected(oldTargetN, 0);
        for (size_t i = 0; i < rows.size(); ++i) {
            selectedIndex[rows[i]] = static_cast<int>(i);
            selected[rows[i]] = 1;
        }
        SmoothingColumn sc(static_cast<int>(rows.size()));
        for (size_t i = 0; i < rows.size(); ++i) {
            sc.smoothings[i] = target.smoothings[rows[i]];
            sc.numbers[i] = target.numbers[rows[i]];
        }
        std::vector<CapPtr> newSmoothings;
        std::vector<int> newNumbers;
        newSmoothings.reserve(oldTargetN - rows.size());
        newNumbers.reserve(oldTargetN - rows.size());
        for (int r = 0; r < oldTargetN; ++r) {
            if (!selected[r]) {
                newSmoothings.push_back(target.smoothings[r]);
                newNumbers.push_back(target.numbers[r]);
            }
        }
        target.smoothings.swap(newSmoothings);
        target.numbers.swap(newNumbers);
        target.n -= static_cast<int>(rows.size());
        CobMatrix result(source, sc);
        std::vector<MatrixRow> remaining;
        remaining.reserve(oldTargetN - rows.size());
        for (int r = 0; r < oldTargetN; ++r) {
            if (selected[r]) result.entries[selectedIndex[r]] = std::move(entries[r]);
            else remaining.push_back(std::move(entries[r]));
        }
        entries.swap(remaining);
        return result;
    }
};

struct IntMatrix {
    int rows = 0, columns = 0;
    std::vector<std::vector<BigInt> > matrix;
    IntMatrix* prev = nullptr;
    IntMatrix* next = nullptr;
    std::vector<int>* source = nullptr;
    std::vector<int>* target = nullptr;

    IntMatrix() = default;
    IntMatrix(int r, int c) : rows(r), columns(c), matrix(r, std::vector<BigInt>(c, BI_ZERO)) {}
    explicit IntMatrix(const CobMatrix& cm) : rows(cm.target.n), columns(cm.source.n), matrix(rows, std::vector<BigInt>(columns, BI_ZERO)) {
        for (int i = 0; i < rows; ++i) {
            for (auto& kv : cm.entries[i]) {
                if (kv.second.numberOfTerms() == 1) matrix[i][kv.first] = kv.second.firstCoefficient();
            }
        }
    }

    bool isDiagonal() const {
        for (int i = 0; i < rows; ++i) for (int j = 0; j < columns; ++j)
            if (!matrix[i][j].isZero() && i != j) return false;
        return true;
    }
    int rowNonZeros(int i) const {
        int r = 0; for (int j = 0; j < columns; ++j) if (!matrix[i][j].isZero()) ++r; return r;
    }
    int columnNonZeros(int i) const {
        int r = 0; for (int j = 0; j < rows; ++j) if (!matrix[j][i].isZero()) ++r; return r;
    }
    void swapRows2(int a, int b) { std::swap(matrix[a], matrix[b]); }
    void swapColumns2(int a, int b) { for (int i = 0; i < rows; ++i) std::swap(matrix[i][a], matrix[i][b]); }
    void swapRows(int a, int b) {
        swapRows2(a,b);
        if (next) next->swapColumns2(a,b);
        if (target) std::swap((*target)[a], (*target)[b]);
    }
    void swapColumns(int a, int b) {
        swapColumns2(a,b);
        if (prev) prev->swapRows2(a,b);
        if (source) std::swap((*source)[a], (*source)[b]);
    }
    void addRow(int a, int b, const BigInt& n) { for (int i = 0; i < columns; ++i) matrix[a][i] = matrix[a][i] + matrix[b][i] * n; }
    void addColumn(int a, int b, const BigInt& n) { for (int i = 0; i < rows; ++i) matrix[i][a] = matrix[i][a] + matrix[i][b] * n; }
    void multRow(int a, const BigInt& n) { for (int i = 0; i < columns; ++i) matrix[a][i] = matrix[a][i] * n; }
    void multColumn(int a, const BigInt& n) { for (int i = 0; i < rows; ++i) matrix[i][a] = matrix[i][a] * n; }
    int zeroRowsToEnd() {
        int nz = rows;
        for (int i = 0; i < nz; ++i) while (i < nz && rowNonZeros(i) == 0) swapRows(i, --nz);
        return nz;
    }
    int zeroColumnsToEnd() {
        int nz = columns;
        for (int i = 0; i < nz; ++i) while (i < nz && columnNonZeros(i) == 0) swapColumns(i, --nz);
        return nz;
    }
    void toSmithForm() {
        for (int row = 0, col = 0; row < rows && col < columns; ++row, ++col) {
            while (row < rows && rowNonZeros(row) == 0) row++;
            while (col < columns && columnNonZeros(col) == 0) col++;
            if (row >= rows || col >= columns) break;
            if (row > col) { swapRows(row, col); row = col; }
            else if (col > row) { swapColumns(row, col); col = row; }
            while (rowNonZeros(row) != 1 || columnNonZeros(col) != 1 || matrix[row][col] <= BI_ZERO) {
                for (int j = row; j < rows; ++j) if (matrix[j][col] < BI_ZERO) multRow(j, BI_MINUS_ONE);
                while (columnNonZeros(col) != 1 || matrix[row][col].isZero()) {
                    BigInt min(-1); int idxmin = -1;
                    for (int j = row; j < rows; ++j) if ((matrix[j][col] < min || min == BI_MINUS_ONE) && matrix[j][col] > BI_ZERO) { min = matrix[j][col]; idxmin = j; }
                    if (idxmin != row) swapRows(row, idxmin);
                    for (int j = row + 1; j < rows; ++j) if (!matrix[j][col].isZero()) addRow(j, row, -(matrix[j][col] / min));
                }
                for (int j = col + 1; j < columns; ++j) if (matrix[row][j] < BI_ZERO) multColumn(j, BI_MINUS_ONE);
                while (rowNonZeros(row) != 1 || matrix[row][col].isZero()) {
                    BigInt min(-1); int idxmin = -1;
                    for (int j = col; j < columns; ++j) if ((matrix[row][j] < min || min == BI_MINUS_ONE) && matrix[row][j] > BI_ZERO) { min = matrix[row][j]; idxmin = j; }
                    if (idxmin != col) swapColumns(col, idxmin);
                    for (int j = col + 1; j < columns; ++j) if (!matrix[row][j].isZero()) addColumn(j, col, -(matrix[row][j] / min));
                }
            }
        }
        zeroRowsToEnd();
        zeroColumnsToEnd();
    }
};

struct Komplex {
    int ncolumns = 0;
    std::vector<SmoothingColumn> columns;
    std::vector<CobMatrix> matrices;
    int startnum = 0;

    Komplex() = default;
    explicit Komplex(int n) : ncolumns(n), columns(n) {}
    Komplex(const std::vector<std::vector<int> >& pd, const std::vector<int>& xsigns, int nfixed);

    void reduce();
    void deLoop(int colnum);
    bool reductionLemma(int i);
    void reductionLemma(int i, int j, int k, const BigInt& n);
    struct Isomorphism {
        int row = 0;
        int column = 0;
        BigInt coefficient;
        Isomorphism() = default;
        Isomorphism(int r, int c, const BigInt& n) : row(r), column(c), coefficient(n) {}
    };
    std::vector<Isomorphism> findBlock(const CobMatrix& m) const;
    void blockReductionLemma(int i);
    void blockReductionLemma(int i, const std::vector<Isomorphism>& block);
    Komplex compose(int start, const Komplex& kom, int kstart, int nc) const;
    std::string KhForZ();
};

static std::vector<std::vector<int> > pascalTriangle;
static void fillPascal(int n) {
    if (static_cast<int>(pascalTriangle.size()) > n) return;
    pascalTriangle.assign(n + 1, std::vector<int>());
    for (int i = 0; i <= n; ++i) {
        pascalTriangle[i].assign(i + 1, 1);
        for (int j = 1; j < i; ++j) pascalTriangle[i][j] = pascalTriangle[i - 1][j - 1] + pascalTriangle[i - 1][j];
    }
}

Komplex::Komplex(const std::vector<std::vector<int> >& pd, const std::vector<int>& xsigns, int nfixed) {
    ncolumns = static_cast<int>(pd.size()) + 1;
    columns.assign(ncolumns, SmoothingColumn());
    startnum = 0;
    for (size_t i = 0; i < pd.size(); ++i) if (xsigns[i] == -1) startnum--;
    fillPascal(static_cast<int>(pd.size()));
    for (int i = 0; i < ncolumns; ++i) columns[i] = SmoothingColumn(pascalTriangle[pd.size()][i]);
    for (int i = 0; i < ncolumns - 1; ++i) matrices.push_back(CobMatrix(columns[i], columns[i + 1]));

    int total = 1 << pd.size();
    std::vector<int> numsmoothings(ncolumns, 0);
    std::vector<std::vector<std::vector<int> > > crossing2cycles(total, std::vector<std::vector<int> >(pd.size(), std::vector<int>(2, -1)));
    std::vector<CapPtr> smoothings(total);
    std::vector<int> whichColumn(total), whichRow(total);
    for (int i = 0; i < total; ++i) {
        std::vector<std::vector<int> > smoothing(pd.size() * 2, std::vector<int>(2, 0));
        int num1 = 0;
        for (size_t j = 0; j < pd.size(); ++j) {
            if (((i >> j) & 1) == 0) {
                smoothing[2*j][0] = pd[j][0]; smoothing[2*j][1] = pd[j][1];
                smoothing[2*j+1][0] = pd[j][2]; smoothing[2*j+1][1] = pd[j][3];
            } else {
                smoothing[2*j][0] = pd[j][0]; smoothing[2*j][1] = pd[j][3];
                smoothing[2*j+1][0] = pd[j][1]; smoothing[2*j+1][1] = pd[j][2];
                num1++;
            }
        }
        std::vector<std::vector<int> > rsmoothing(pd.size() * 4, std::vector<int>(2, -1));
        for (size_t j = 0; j < smoothing.size(); ++j) {
            if (rsmoothing[smoothing[j][0]][0] == -1) rsmoothing[smoothing[j][0]][0] = static_cast<int>(j);
            else rsmoothing[smoothing[j][0]][1] = static_cast<int>(j);
            if (rsmoothing[smoothing[j][1]][0] == -1) rsmoothing[smoothing[j][1]][0] = static_cast<int>(j);
            else rsmoothing[smoothing[j][1]][1] = static_cast<int>(j);
        }
        std::vector<char> done(4 * pd.size(), 0);
        int ncycles = 0;
        std::vector<int> pairings(nfixed, 0);
        for (int j = 0; j < static_cast<int>(rsmoothing.size()) && rsmoothing[j][0] != -1; ++j) {
            if (done[j]) continue;
            int dst = j;
            do {
                int a;
                if (dst < nfixed) a = 0;
                else {
                    int b0 = smoothing[rsmoothing[dst][0]][0] == dst ? 1 : 0;
                    int b1 = smoothing[rsmoothing[dst][1]][0] == dst ? 1 : 0;
                    if (done[smoothing[rsmoothing[dst][0]][b0]]) {
                        if (done[smoothing[rsmoothing[dst][1]][b1]]) a = smoothing[rsmoothing[dst][0]][b0] == j ? 0 : 1;
                        else a = 1;
                    } else a = 0;
                    crossing2cycles[i][rsmoothing[dst][1] / 2][rsmoothing[dst][1] % 2] = j;
                }
                crossing2cycles[i][rsmoothing[dst][0] / 2][rsmoothing[dst][0] % 2] = j;
                done[dst] = 1;
                dst = smoothing[rsmoothing[dst][a]][0] == dst ? smoothing[rsmoothing[dst][a]][1] : smoothing[rsmoothing[dst][a]][0];
            } while (dst >= nfixed && dst != j);
            if (dst == j) ncycles++;
            else { pairings[j] = dst; pairings[dst] = j; }
            done[dst] = 1;
        }
        std::vector<int> remap(rsmoothing.size(), -1);
        for (size_t j = 0, k = 0; j < pd.size(); ++j) for (int l = 0; l < 2; ++l) {
            int& v = crossing2cycles[i][j][l];
            if (v < nfixed) v = -1 - v;
            else if (remap[v] != -1) v = remap[v];
            else { remap[v] = static_cast<int>(k); v = static_cast<int>(k++); }
        }
        Cap c(nfixed, ncycles);
        c.pairings = pairings;
        CapPtr cp = cacheCap(c);
        whichColumn[i] = num1;
        whichRow[i] = numsmoothings[num1];
        columns[num1].smoothings[numsmoothings[num1]] = cp;
        for (size_t j = 0; j < pd.size(); ++j) {
            int& q = columns[num1].numbers[numsmoothings[num1]];
            if ((i & (1 << j)) == 0) q += xsigns[j] == 1 ? 1 : -2;
            else q += xsigns[j] == 1 ? 2 : -1;
        }
        numsmoothings[num1]++;
        smoothings[i] = cp;
        if (i != 0) {
            for (size_t j = 0; j < pd.size(); ++j) if ((i & (1 << j)) != 0) {
                int k = i ^ (1 << j);
                CobPtr cc = makeCob(smoothings[k], cp);
                std::fill(cc->connectedComponent.begin(), cc->connectedComponent.end(), -1);
                cc->ncc = 0;
                for (size_t l = 0; l < pd.size(); ++l) {
                    if (l != j) {
                        for (int m = 0; m < 2; ++m) {
                            int x = crossing2cycles[i][l][m];
                            if (x < 0) x = cc->component[-1 - x]; else x += cc->offbot;
                            int y = crossing2cycles[k][l][m];
                            if (y < 0) y = cc->component[-1 - y]; else y += cc->offtop;
                            int cci;
                            if (cc->connectedComponent[y] != -1) cci = cc->connectedComponent[y];
                            else if (cc->connectedComponent[x] != -1) cci = cc->connectedComponent[x];
                            else cci = cc->ncc++;
                            cc->connectedComponent[y] = cci;
                            cc->connectedComponent[x] = cci;
                        }
                    } else {
                        int num[4] = { crossing2cycles[i][l][0], crossing2cycles[i][l][1], crossing2cycles[k][l][0], crossing2cycles[k][l][1] };
                        for (int x = 0; x < 4; ++x) {
                            if (num[x] >= 0) num[x] += x < 2 ? cc->offbot : cc->offtop;
                            else num[x] = cc->component[-1 - num[x]];
                        }
                        int cci = -1;
                        for (int x = 0; x < 4; ++x) if (cc->connectedComponent[num[x]] != -1) { cci = cc->connectedComponent[num[x]]; break; }
                        if (cci == -1) cci = cc->ncc++;
                        for (int x = 0; x < 4; ++x) if (cc->connectedComponent[num[x]] != cci) {
                            int y = cc->connectedComponent[num[x]];
                            if (y != -1) for (int z = 0; z < cc->nbc; ++z) if (cc->connectedComponent[z] == y) cc->connectedComponent[z] = cci;
                            cc->connectedComponent[num[x]] = cci;
                        }
                    }
                }
                cc->ncc = 0;
                for (int l = 0; l < cc->nbc; ++l) {
                    if (cc->connectedComponent[l] > cc->ncc) {
                        int x = cc->connectedComponent[l];
                        for (int m = l; m < cc->nbc; ++m) {
                            if (cc->connectedComponent[m] == x) cc->connectedComponent[m] = cc->ncc;
                            else if (cc->connectedComponent[m] == cc->ncc) cc->connectedComponent[m] = x;
                        }
                        cc->ncc++;
                    } else if (cc->connectedComponent[l] == cc->ncc) cc->ncc++;
                }
                cc->dots.assign(cc->ncc, 0);
                cc->genus.assign(cc->ncc, 0);
                int coeff = 1;
                for (size_t l = j + 1; l < pd.size(); ++l) if ((i & (1 << l)) != 0) coeff = -coeff;
                LCCC lc(cacheCob(cc), BigInt(coeff));
                matrices[num1 - 1].putEntry(whichRow[i], whichRow[k], lc);
            }
        }
    }
}

void Komplex::deLoop(int colnum) {
    KH_PROFILE(deLoop);
    uint64_t deLoopSetupStart = g_options.profile ? profileNowNs() : 0;
    CobMatrix* prevMatrix = colnum != 0 ? &matrices[colnum - 1] : nullptr;
    CobMatrix* nextMatrix = colnum != ncolumns - 1 ? &matrices[colnum] : nullptr;
    int size = 0;
    for (int i = 0; i < columns[colnum].n; ++i) size += 1 << columns[colnum].smoothings[i]->ncycles;
    SmoothingColumn newsc(size);
    CobMatrix prev, next;
    if (prevMatrix) prev = CobMatrix(columns[colnum - 1], newsc);
    if (nextMatrix) next = CobMatrix(newsc, columns[colnum + 1]);
    std::vector<int> nextOffset;
    std::vector<std::pair<int, const LCCC*> > nextColumnEntries;
    if (nextMatrix) {
        nextOffset.assign(columns[colnum].n + 1, 0);
        for (size_t row = 0; row < nextMatrix->entries.size(); ++row) {
            for (size_t e = 0; e < nextMatrix->entries[row].size(); ++e) {
                int col = nextMatrix->entries[row][e].first;
                if (col >= 0 && col < columns[colnum].n) {
                    nextOffset[col + 1]++;
                }
            }
        }
        for (size_t col = 1; col < nextOffset.size(); ++col) nextOffset[col] += nextOffset[col - 1];
        nextColumnEntries.resize(nextOffset.back());
        std::vector<int> cursor = nextOffset;
        for (size_t row = 0; row < nextMatrix->entries.size(); ++row) {
            for (size_t e = 0; e < nextMatrix->entries[row].size(); ++e) {
                int col = nextMatrix->entries[row][e].first;
                if (col >= 0 && col < columns[colnum].n) {
                    int pos = cursor[col]++;
                    nextColumnEntries[pos] = std::make_pair(static_cast<int>(row), &nextMatrix->entries[row][e].second);
                }
            }
        }
    }
    if (g_options.profile) {
        g_profile.deLoopSetup.calls++;
        g_profile.deLoopSetup.ns += profileNowNs() - deLoopSetupStart;
    }
    int newn = 0;
    for (int i = 0; i < columns[colnum].n; ++i) {
        CapPtr oldsm = columns[colnum].smoothings[i];
        Cap newsmValue(oldsm->n, 0);
        newsmValue.pairings = oldsm->pairings;
        CapPtr newsm = cacheCap(newsmValue);
        Cobordism prevccBase(oldsm, newsm);
        prevccBase.ncc = prevccBase.nbc;
        prevccBase.connectedComponent = countingSmall(prevccBase.nbc);
        prevccBase.dots.assign(prevccBase.ncc, 0);
        prevccBase.genus.assign(prevccBase.ncc, 0);
        Cobordism nextccBase(newsm, oldsm);
        nextccBase.ncc = nextccBase.nbc;
        nextccBase.connectedComponent = countingSmall(nextccBase.nbc);
        nextccBase.dots.assign(nextccBase.ncc, 0);
        nextccBase.genus.assign(nextccBase.ncc, 0);
        Cobordism prevcc = prevccBase;
        Cobordism nextcc = nextccBase;
        for (int j = 0; j < (1 << oldsm->ncycles); ++j) {
            int nmod = 0;
            for (int k = 0; k < oldsm->ncycles; ++k) {
                if ((j & (1 << k)) == 0) {
                    nmod++;
                    prevcc.dots[prevcc.offtop + k] = 1;
                    nextcc.dots[nextcc.offbot + k] = 0;
                } else {
                    nmod--;
                    prevcc.dots[prevcc.offtop + k] = 0;
                    nextcc.dots[nextcc.offbot + k] = 1;
                }
            }
            newsc.smoothings[newn] = newsm;
            newsc.numbers[newn] = columns[colnum].numbers[i] + nmod;
            if (prevMatrix) {
                uint64_t partStart = g_options.profile ? profileNowNs() : 0;
                if (oldsm->ncycles != 0) {
                    MatrixRow row;
                    for (auto& kv : prevMatrix->entries[i]) {
                        if (g_options.profile) {
                            g_profile.deLoopPrevTerms.calls++;
                            g_profile.deLoopPrevTerms.ns += kv.second.terms.size();
                        }
                        LCCC composition = LCCC::composeCobLeft(prevcc, kv.second);
                        appendSortedRowEntry(row, kv.first, std::move(composition));
                    }
                    prev.entries[newn] = row;
                } else {
                    prev.entries[newn] = prevMatrix->entries[i];
                }
                if (g_options.profile) {
                    g_profile.deLoopPrev.calls++;
                    g_profile.deLoopPrev.ns += profileNowNs() - partStart;
                }
            }
            if (nextMatrix) {
                uint64_t partStart = g_options.profile ? profileNowNs() : 0;
                if (oldsm->ncycles != 0) {
                    for (int idx = nextOffset[i]; idx < nextOffset[i + 1]; ++idx) {
                        int k = nextColumnEntries[idx].first;
                        if (g_options.profile) {
                            g_profile.deLoopNextTerms.calls++;
                            g_profile.deLoopNextTerms.ns += nextColumnEntries[idx].second->terms.size();
                        }
                        LCCC composition = LCCC::composeCobRight(*nextColumnEntries[idx].second, nextcc);
                        appendSortedRowEntry(next.entries[k], newn, std::move(composition));
                    }
                } else {
                    for (int idx = nextOffset[i]; idx < nextOffset[i + 1]; ++idx) {
                        int k = nextColumnEntries[idx].first;
                        appendSortedRowEntry(next.entries[k], newn, *nextColumnEntries[idx].second);
                    }
                }
                if (g_options.profile) {
                    g_profile.deLoopNext.calls++;
                    g_profile.deLoopNext.ns += profileNowNs() - partStart;
                }
            }
            newn++;
        }
    }
    columns[colnum] = newsc;
    if (prevMatrix) { prev.target = newsc; matrices[colnum - 1] = prev; }
    if (nextMatrix) { next.source = newsc; matrices[colnum] = next; }
}

bool Komplex::reductionLemma(int i) {
    bool found, found2 = false, ret = false;
    int count = 0;
    do {
        found = false;
        CobMatrix& m = matrices[i];
        for (int j = 0; j < static_cast<int>(m.entries.size()) && !found; ++j) {
            for (auto& kv : m.entries[j]) {
                int k = kv.first;
                LCCC& lc = kv.second;
                if (lc.numberOfTerms() == 1) {
                    if (!columns[i].smoothings[k]->equals(*columns[i + 1].smoothings[j])) continue;
                    CobPtr cc = lc.firstTerm();
                    BigInt n = lc.firstCoefficient();
                    if (!(n == BI_ONE || n == BI_MINUS_ONE)) continue;
                    if (!cc->isIsomorphism()) continue;
                    found2 = found = true;
                    ++count;
                    reductionLemma(i, j, k, n);
                    break;
                }
            }
        }
        if (found) ret = true;
        if (!found && found2) {
            matrices[i].reduce();
            found = true;
            found2 = false;
        }
    } while (found);
    (void)count;
    return ret;
}

void Komplex::reductionLemma(int i, int j, int k, const BigInt& n) {
    CapPtr isoSource = columns[i].smoothings[k];
    CapPtr isoTarget = columns[i + 1].smoothings[j];
    CobMatrix m = std::move(matrices[i]);
    CobMatrix delta = m.extractRow(j);
    delta.extractColumn(k);
    CobMatrix gamma = m.extractColumn(k);
    BigInt coeff = (n == BI_ONE ? BI_MINUS_ONE : BI_ONE);
    for (size_t row = 0; row < gamma.entries.size(); ++row) {
        for (size_t e = 0; e < gamma.entries[row].size(); ++e) {
            if (gamma.entries[row][e].first == 0) gamma.entries[row][e].second.multiplyInPlace(coeff);
        }
    }
    CobMatrix gpd = gamma.compose(delta);
    gpd.add(m);
    matrices[i] = gpd;
    columns[i] = delta.source;
    columns[i + 1] = gamma.target;
    if (i != 0) {
        matrices[i - 1].extractRow(k);
    }
    if (i != ncolumns - 2) {
        matrices[i + 1].extractColumn(j);
    }
}

std::vector<Komplex::Isomorphism> Komplex::findBlock(const CobMatrix& m) const {
    std::vector<Isomorphism> isos;
    std::vector<char> disallowedColumns(m.source.n, 0);
    std::vector<char> isomorphismColumns(m.source.n, 0);
    std::vector<int> potentialDisallowedColumns;
    for (int j = 0; j < static_cast<int>(m.entries.size()); ++j) {
        bool foundIsomorphismOnRow = false;
        bool rowForbidden = false;
        bool hasRowCandidate = false;
        Isomorphism rowCandidate;
        potentialDisallowedColumns.clear();
        potentialDisallowedColumns.reserve(m.entries[j].size());
        for (const auto& kv : m.entries[j]) {
            int k = kv.first;
            const LCCC& lc = kv.second;
            if (isomorphismColumns[k]) rowForbidden = true;
            if (disallowedColumns[k]) continue;
            if (foundIsomorphismOnRow) {
                disallowedColumns[k] = 1;
            } else {
                potentialDisallowedColumns.push_back(k);
                if (!hasRowCandidate && lc.numberOfTerms() == 1) {
                    CobPtr cc = lc.firstTerm();
                    BigInt n = lc.firstCoefficient();
                    if (!(n == BI_ONE || n == BI_MINUS_ONE)) continue;
                    if (!cc->isIsomorphism()) continue;
                    rowCandidate = Isomorphism(j, k, n);
                    hasRowCandidate = true;
                    foundIsomorphismOnRow = true;
                }
            }
        }
        if (!rowForbidden && hasRowCandidate) {
            isos.push_back(rowCandidate);
            isomorphismColumns[rowCandidate.column] = 1;
            for (size_t p = 0; p < potentialDisallowedColumns.size(); ++p) {
                disallowedColumns[potentialDisallowedColumns[p]] = 1;
            }
        }
    }
    return isos;
}

void Komplex::blockReductionLemma(int i, const std::vector<Isomorphism>& block) {
    KH_PROFILE(blockReductionApply);
    if (block.empty()) return;
    std::vector<int> rows, cols;
    std::vector<BigInt> coeffs;
    rows.reserve(block.size());
    cols.reserve(block.size());
    coeffs.reserve(block.size());
    for (const Isomorphism& iso : block) {
        rows.push_back(iso.row);
        cols.push_back(iso.column);
        coeffs.push_back(iso.coefficient);
    }
    CobMatrix m = std::move(matrices[i]);
    CobMatrix delta = m.extractRows(rows);
    delta.extractColumns(cols);
    CobMatrix gamma = m.extractColumns(cols);
    for (size_t k = 0; k < block.size(); ++k) {
        BigInt coeff = (coeffs[k] == BI_ONE ? BI_MINUS_ONE : BI_ONE);
        for (size_t row = 0; row < gamma.entries.size(); ++row) {
            MatrixRow::iterator it = findRowEntry(gamma.entries[row], static_cast<int>(k));
            if (it != gamma.entries[row].end() && it->first == static_cast<int>(k)) {
                it->second.multiplyInPlace(coeff);
            }
        }
    }
    CobMatrix gpd = gamma.compose(delta);
    gpd.add(m);
    matrices[i] = gpd;
    columns[i] = delta.source;
    columns[i + 1] = gamma.target;
    if (i != 0) matrices[i - 1].extractRows(cols);
    if (i != ncolumns - 2) matrices[i + 1].extractColumns(rows);
}

void Komplex::blockReductionLemma(int i) {
    KH_PROFILE(blockReduction);
    while (true) {
        std::vector<Isomorphism> block = findBlock(matrices[i]);
        if (block.empty()) break;
        blockReductionLemma(i, block);
        matrices[i].reduce();
    }
}

void Komplex::reduce() {
    KH_PROFILE(complexReduce);
    for (int i = 0; i < ncolumns; ++i) {
        deLoop(i);
        if (i > 0) {
            matrices[i - 1].reduce();
            blockReductionLemma(i - 1);
        }
    }
}

Komplex Komplex::compose(int start, const Komplex& kom, int kstart, int nc) const {
    KH_PROFILE(complexCompose);
    Komplex ret(ncolumns + kom.ncolumns - 1);
    ret.startnum = startnum + kom.startnum;
    std::vector<int> colsizes(ret.ncolumns, 0);
    for (int i = 0; i < ret.ncolumns; ++i)
        for (int j = 0; j <= i && j < ncolumns; ++j)
            if (i - j < kom.ncolumns) colsizes[i] += columns[j].n * kom.columns[i - j].n;
    for (int i = 0; i < ret.ncolumns; ++i) ret.columns[i] = SmoothingColumn(colsizes[i]);
    std::vector<std::vector<int> > startnum2(ncolumns, std::vector<int>(kom.ncolumns, 0));
    for (int i = 0; i < ret.ncolumns; ++i) {
        int sn = 0;
        for (int j = 0; j <= i && j < ncolumns; ++j) if (i - j < kom.ncolumns) {
            int kk = i - j;
            startnum2[j][kk] = sn;
            for (int l = 0; l < columns[j].n; ++l) for (int m = 0; m < kom.columns[kk].n; ++m) {
                ret.columns[i].smoothings[sn] = columns[j].smoothings[l]->compose(start, kom.columns[kk].smoothings[m], kstart, nc);
                ret.columns[i].numbers[sn] = columns[j].numbers[l] + kom.columns[kk].numbers[m];
                sn++;
            }
        }
    }
    for (int i = 0; i < ret.ncolumns - 1; ++i) {
        CobMatrix newMatrix(ret.columns[i], ret.columns[i + 1]);
        for (int j = 0; j <= i && j < ncolumns; ++j) if (i - j < kom.ncolumns) {
            int kk = i - j;
            if (j < ncolumns - 1) {
                const CobMatrix& matrixJ = matrices[j];
                for (int m = 0; m < kom.columns[kk].n; ++m) {
                    CobPtr komcc = isomorphism(kom.columns[kk].smoothings[m]);
                    for (int nrow = 0; nrow < columns[j + 1].n; ++nrow) {
                        for (auto& kv : matrixJ.entries[nrow]) {
                            LCCC composition = kv.second.composeHorizontal(start, komcc, kstart, nc, false);
                            if (!composition.isZero()) {
                                if (kk % 2 == 0) composition = composition.multiplied(BI_MINUS_ONE);
                                newMatrix.putEntry(startnum2[j + 1][kk] + nrow * kom.columns[kk].n + m,
                                                   startnum2[j][kk] + kv.first * kom.columns[kk].n + m,
                                                   composition);
                            }
                        }
                    }
                }
            }
            if (kk < kom.ncolumns - 1) {
                const CobMatrix& komMatrixK = kom.matrices[kk];
                for (int l = 0; l < columns[j].n; ++l) {
                    CobPtr thiscc = isomorphism(columns[j].smoothings[l]);
                    for (int nrow = 0; nrow < kom.columns[kk + 1].n; ++nrow) {
                        for (auto& kv : komMatrixK.entries[nrow]) {
                            LCCC composition = kv.second.composeHorizontal(kstart, thiscc, start, nc, true);
                            if (!composition.isZero()) {
                                newMatrix.putEntry(startnum2[j][kk + 1] + l * kom.columns[kk + 1].n + nrow,
                                                   startnum2[j][kk] + l * kom.columns[kk].n + kv.first,
                                                   composition);
                            }
                        }
                    }
                }
            }
        }
        ret.matrices.push_back(newMatrix);
    }
    return ret;
}

std::string Komplex::KhForZ() {
    std::vector<IntMatrix> mats;
    mats.reserve(matrices.size());
    std::vector<std::vector<int> > colcopy(ncolumns);
    for (int i = 0; i < ncolumns; ++i) colcopy[i] = columns[i].numbers;
    for (size_t i = 0; i < matrices.size(); ++i) {
        mats.push_back(IntMatrix(matrices[i]));
    }
    for (size_t i = 0; i < mats.size(); ++i) {
        mats[i].source = &colcopy[i];
        mats[i].target = &colcopy[i + 1];
        if (i > 0) mats[i - 1].next = &mats[i];
    }
    std::string ret;
    for (int i = 0; i < ncolumns; ++i) {
        std::vector<int> degrees;
        std::vector<int> nentries;
        std::vector<std::vector<int> > retvals;
        int last = INT32_MIN;
        int n = 0;
        if (i != 0) n = std::min(mats[i - 1].rows, mats[i - 1].columns);
        while (true) {
            int minv = INT32_MAX;
            for (int j = 0; j < columns[i].n; ++j)
                if (colcopy[i][j] > last && colcopy[i][j] < minv) minv = colcopy[i][j];
            if (minv == INT32_MAX) break;
            degrees.push_back(minv);
            retvals.push_back(std::vector<int>());
            std::vector<BigInt> nums(n);
            int nnum = 0;
            for (int j = 0; j < n; ++j)
                if (colcopy[i][j] == minv && !mats[i - 1].matrix[j][j].isZero()) nums[nnum++] = mats[i - 1].matrix[j][j].abs();
            nentries.push_back(nnum);
            if (nnum != 0) {
                bool done = false;
                int pk = 1;
                while (!done) {
                    int p = 0;
                    pk++;
                    for (;; pk++) {
                        for (int j = 2; j <= pk; ++j) if (pk % j == 0) { p = j; break; }
                        int tmp = pk;
                        while (tmp > 1) {
                            if (tmp % p == 0) tmp /= p;
                            else break;
                        }
                        if (tmp == 1) break;
                    }
                    for (int j = 0; j < nnum; ++j) {
                        uint32_t rem = 0;
                        BigInt div = nums[j].div_small(pk, &rem);
                        if (rem == 0 && nums[j].mod_small(pk * p) != 0) {
                            retvals.back().push_back(pk);
                            nums[j] = div;
                        }
                    }
                    done = true;
                    for (int j = 0; j < nnum; ++j) if (!(nums[j] == BI_ONE)) { done = false; break; }
                }
            }
            last = minv;
        }
        if (i != ncolumns - 1) {
            mats[i].toSmithForm();
            if (!mats[i].isDiagonal()) throw std::runtime_error("matrix is not diagonal");
        }
        for (size_t j = 0; j < degrees.size(); ++j) {
            int nzeros = 0;
            if (i != ncolumns - 1) {
                for (int k = 0; k < columns[i].n; ++k)
                    if (colcopy[i][k] == degrees[j] && (k >= mats[i].rows || k >= mats[i].columns || mats[i].matrix[k][k].isZero())) nzeros++;
            } else {
                for (int k = 0; k < columns[i].n; ++k) if (colcopy[i][k] == degrees[j]) nzeros++;
            }
            nzeros -= nentries[j];
            for (int k = 0; k < nzeros; ++k) retvals[j].push_back(0);
            if (!retvals[j].empty()) {
                std::sort(retvals[j].begin(), retvals[j].end());
                if (!ret.empty()) ret += " + ";
                ret += "q^" + std::to_string(degrees[j]) + "*t^" + std::to_string(i + startnum) + "*Z[";
                for (size_t k = 0; k < retvals[j].size(); ++k) {
                    ret += std::to_string(retvals[j][k]);
                    ret += (k + 1 == retvals[j].size()) ? "]" : ",";
                }
            }
        }
    }
    return ret;
}

static int chooseXingRecursive(const std::vector<int>& edges, const std::vector<std::vector<int> >& pd,
                               std::vector<char>& in, std::vector<char>& done, int depth, std::vector<int>& retmax) {
    int nedges = static_cast<int>(edges.size());
    int best = -1, nconbest = -1;
    std::vector<int> rbest(depth, 0);
    for (size_t i = 0; i < pd.size(); ++i) if (!done[i]) {
        int ncon = 0;
        for (int j = 0; j < 4; ++j) if (in[pd[i][j]]) ncon++;
        if (ncon == 0 && nedges != 0) continue;
        if (ncon < nconbest) continue;
        int start;
        for (start = 0; start < nedges; ++start) {
            bool found = false;
            for (int k = 0; k < 4; ++k) if (pd[i][k] == edges[start]) { found = true; break; }
            if (!found) break;
        }
        if (start == nedges) start = 0;
        for (int k = 0; k < nedges; ++k) {
            bool found = false;
            for (int l = 0; l < 4; ++l) if (pd[i][l] == edges[(start + k) % nedges]) { found = true; start = (start + k) % nedges; break; }
            if (found) break;
        }
        int kstart = 0;
        if (nedges != 0) for (kstart = 0; kstart < 4; ++kstart) if (pd[i][kstart] == edges[start]) break;
        bool good = true;
        for (int k = 0; k < ncon; ++k) if (pd[i][(kstart + 4 - k) % 4] != edges[(start + k) % nedges]) { good = false; break; }
        if (!good) continue;
        std::vector<int> getn(depth, 0);
        if (depth != 0) {
            kstart = (kstart + 4 - ncon + 1) % 4;
            std::vector<int> newedges(nedges + 4 - 2 * ncon);
            int j = 0;
            for (; j < nedges - ncon; ++j) newedges[j] = edges[(start + ncon + j) % nedges];
            for (int k = 0; k < 4 - ncon; ++k, ++j) newedges[j] = pd[i][(kstart + ncon + k) % 4];
            done[i] = 1;
            std::vector<char> previn(4);
            for (int k = 0; k < 4; ++k) { previn[k] = in[pd[i][k]]; in[pd[i][k]] = 1; }
            chooseXingRecursive(newedges, pd, in, done, depth - 1, getn);
            done[i] = 0;
            for (int k = 0; k < 4; ++k) in[pd[i][k]] = previn[k];
        }
        bool better = false;
        if (ncon > nconbest) better = true;
        else if (ncon == nconbest) for (int j = 0; j < depth; ++j) {
            if (getn[j] > rbest[j]) { better = true; break; }
            if (getn[j] < rbest[j]) break;
        }
        if (better) { nconbest = ncon; rbest = getn; best = static_cast<int>(i); }
    }
    if (best == -1) throw std::runtime_error("could not choose crossing");
    retmax[0] = nconbest;
    for (int i = 0; i < depth; ++i) retmax[i + 1] = rbest[i];
    return best;
}

static int takeNextCrossing(const std::vector<int>&, const std::vector<std::vector<int> >& pd,
                            std::vector<char>&, std::vector<char>& done, int, std::vector<int>&) {
    for (size_t i = 0; i < pd.size(); ++i) if (!done[i]) return static_cast<int>(i);
    throw std::runtime_error("no crossing left");
}

static std::vector<int> getSigns(const std::vector<std::vector<int> >& pd) {
    std::vector<int> xsigns(pd.size());
    for (size_t i = 0; i < pd.size(); ++i) {
        if (pd[i][1] - pd[i][3] == 1 || pd[i][3] - pd[i][1] > 1) xsigns[i] = 1;
        else if (pd[i][3] - pd[i][1] == 1 || pd[i][1] - pd[i][3] > 1) xsigns[i] = -1;
        else throw std::runtime_error("error finding crossing signs");
    }
    return xsigns;
}

static bool sanityPD(const PDCode& pd) {
    std::map<int, int> counts;
    for (size_t i = 0; i < pd.size(); ++i) {
        if (pd[i].size() != 4) return false;
        for (int v : pd[i]) counts[v]++;
    }
    for (std::map<int, int>::const_iterator it = counts.begin(); it != counts.end(); ++it) {
        if (it->second != 2) return false;
    }
    return true;
}

static int uniqueCount(const std::vector<int>& crossing) {
    return static_cast<int>(std::set<int>(crossing.begin(), crossing.end()).size());
}

static std::vector<int> valueSet(const PDCode& pd) {
    std::set<int> values;
    for (size_t i = 0; i < pd.size(); ++i) {
        for (int v : pd[i]) values.insert(v);
    }
    return std::vector<int>(values.begin(), values.end());
}

static bool containsValue(const std::vector<int>& xs, int v) {
    return std::find(xs.begin(), xs.end(), v) != xs.end();
}

static void addUndirectedVectorEdge(std::map<int, std::vector<int> >& graph, int a, int b) {
    std::vector<int>& ga = graph[a];
    if (std::find(ga.begin(), ga.end(), b) == ga.end()) ga.push_back(b);
    std::vector<int>& gb = graph[b];
    if (std::find(gb.begin(), gb.end(), a) == gb.end()) gb.push_back(a);
}

static void addUndirectedSetEdge(std::map<int, std::set<int> >& graph, int a, int b) {
    graph[a].insert(b);
    graph[b].insert(a);
}

static std::map<int, std::vector<int> > pdAdjacencyVector(const PDCode& pd) {
    std::map<int, std::vector<int> > graph;
    for (size_t i = 0; i < pd.size(); ++i) {
        addUndirectedVectorEdge(graph, pd[i][0], pd[i][2]);
        addUndirectedVectorEdge(graph, pd[i][1], pd[i][3]);
    }
    return graph;
}

static PDCode renumberR1Order(PDCode pd) {
    if (pd.empty()) return pd;
    std::vector<int> values = valueSet(pd);
    std::map<int, std::vector<int> > graph = pdAdjacencyVector(pd);
    std::vector<int> visitOrder;
    for (int value : values) {
        if (containsValue(visitOrder, value)) continue;
        if (graph.find(value) == graph.end()) throw std::runtime_error("invalid PD graph during R1 renumbering");
        visitOrder.push_back(value);
        while (true) {
            int top = visitOrder.back();
            std::vector<int> neighbors = graph[top];
            std::sort(neighbors.begin(), neighbors.end());
            bool advanced = false;
            for (int nxt : neighbors) {
                if (!containsValue(visitOrder, nxt)) {
                    visitOrder.push_back(nxt);
                    advanced = true;
                    break;
                }
            }
            if (!advanced) break;
        }
    }
    std::map<int, int> newId;
    for (size_t i = 0; i < visitOrder.size(); ++i) newId[visitOrder[i]] = static_cast<int>(i);
    for (size_t i = 0; i < pd.size(); ++i) {
        for (int& v : pd[i]) v = newId[v];
    }
    return pd;
}

static PDCode eraseR1(PDCode pd) {
    if (!sanityPD(pd)) throw std::runtime_error("invalid PD code: every arc label must appear exactly twice");
    bool hasR1 = true;
    while (hasR1) {
        hasR1 = false;
        for (size_t i = 0; i < pd.size(); ++i) {
            if (uniqueCount(pd[i]) <= 3) {
                std::vector<int> crossing = pd[i];
                pd.erase(pd.begin() + static_cast<std::ptrdiff_t>(i));
                std::vector<int> singles;
                for (int v : crossing) {
                    if (std::count(crossing.begin(), crossing.end(), v) == 1) singles.push_back(v);
                }
                if (singles.size() == 2) {
                    for (size_t r = 0; r < pd.size(); ++r) {
                        for (int& v : pd[r]) {
                            if (v == singles[0]) v = singles[1];
                        }
                    }
                }
                hasR1 = true;
                break;
            }
        }
    }
    return renumberR1Order(pd);
}

static std::map<int, std::set<int> > baseGraph(const PDCode& pd) {
    std::map<int, std::set<int> > graph;
    for (size_t i = 0; i < pd.size(); ++i) {
        int crossingNode = -static_cast<int>(i) - 1;
        for (int v : pd[i]) addUndirectedSetEdge(graph, v, crossingNode);
    }
    return graph;
}

static int graphComponentCount(const PDCode& pd) {
    std::map<int, std::set<int> > graph = baseGraph(pd);
    std::set<int> visited;
    int count = 0;
    for (std::map<int, std::set<int> >::const_iterator it = graph.begin(); it != graph.end(); ++it) {
        int start = it->first;
        if (visited.count(start)) continue;
        ++count;
        std::vector<int> stack(1, start);
        visited.insert(start);
        while (!stack.empty()) {
            int node = stack.back();
            stack.pop_back();
            const std::set<int>& neighbors = graph[node];
            for (std::set<int>::const_iterator jt = neighbors.begin(); jt != neighbors.end(); ++jt) {
                if (!visited.count(*jt)) {
                    visited.insert(*jt);
                    stack.push_back(*jt);
                }
            }
        }
    }
    return count;
}

static bool isNugatory(const PDCode& pd, size_t index) {
    if (uniqueCount(pd[index]) != 4) throw std::runtime_error("nugatory check requires R1-free PD code");
    PDCode bad = pd;
    bad.erase(bad.begin() + static_cast<std::ptrdiff_t>(index));
    return graphComponentCount(bad) > graphComponentCount(pd);
}

static int findNugatory(const PDCode& pd) {
    for (size_t i = 0; i < pd.size(); ++i) {
        if (isNugatory(pd, i)) return static_cast<int>(i);
    }
    return -1;
}

static int rawArcValue(int v) {
    return v;
}

static void addPreNextEdge(std::map<int, int>& pre, std::map<int, int>& nxt, int v1, int v2) {
    int raw1 = rawArcValue(v1);
    int raw2 = rawArcValue(v2);
    int preVal;
    int nxtVal;
    if (std::abs(raw1 - raw2) == 1) {
        preVal = raw1 < raw2 ? v1 : v2;
        nxtVal = raw1 < raw2 ? v2 : v1;
    } else {
        preVal = raw1 < raw2 ? v2 : v1;
        nxtVal = raw1 < raw2 ? v1 : v2;
    }
    pre[nxtVal] = preVal;
    nxt[preVal] = nxtVal;
}

static std::pair<std::map<int, int>, std::map<int, int> > getPreNext(const PDCode& pd) {
    if (!sanityPD(pd)) throw std::runtime_error("invalid PD code while computing pre/next maps");
    std::map<int, int> pre;
    std::map<int, int> nxt;
    for (size_t i = 0; i < pd.size(); ++i) {
        if (uniqueCount(pd[i]) > 2) {
            addPreNextEdge(pre, nxt, pd[i][0], pd[i][2]);
            addPreNextEdge(pre, nxt, pd[i][1], pd[i][3]);
        } else {
            std::vector<int> values(pd[i].begin(), pd[i].end());
            std::sort(values.begin(), values.end());
            values.erase(std::unique(values.begin(), values.end()), values.end());
            if (values.size() != 2) throw std::runtime_error("invalid two-value crossing in pre/next maps");
            pre[values[0]] = values[1];
            nxt[values[0]] = values[1];
            pre[values[1]] = values[0];
            nxt[values[1]] = values[0];
        }
    }
    std::vector<int> nums = valueSet(pd);
    for (int num : nums) {
        if (!pre.count(num)) {
            if (!nxt.count(num)) throw std::runtime_error("broken PD pre/next map");
            pre[num] = nxt[num];
        }
        if (!nxt.count(num)) nxt[num] = pre[num];
    }
    return std::make_pair(pre, nxt);
}

static PDCode replaceArcValue(PDCode pd, int from, int to) {
    for (size_t i = 0; i < pd.size(); ++i) {
        for (int& v : pd[i]) {
            if (v == from) v = to;
        }
    }
    return pd;
}

static PDCode renumberFullDfs(PDCode pd) {
    if (pd.empty()) return pd;
    std::vector<int> nums = valueSet(pd);
    std::map<int, std::set<int> > graph;
    for (size_t i = 0; i < pd.size(); ++i) {
        addUndirectedSetEdge(graph, pd[i][0], pd[i][2]);
        addUndirectedSetEdge(graph, pd[i][1], pd[i][3]);
    }
    std::set<int> visited;
    std::map<int, int> newNum;
    std::function<void(int)> dfs = [&](int num) {
        if (visited.count(num)) return;
        int id = static_cast<int>(visited.size());
        visited.insert(num);
        newNum[num] = id;
        if (!graph.count(num)) throw std::runtime_error("invalid PD graph during renumbering");
        for (std::set<int>::const_iterator it = graph[num].begin(); it != graph[num].end(); ++it) dfs(*it);
    };
    for (int num : nums) {
        if (!visited.count(num)) dfs(num);
    }
    if (newNum.size() != nums.size()) throw std::runtime_error("PD renumbering failed");
    for (size_t i = 0; i < pd.size(); ++i) {
        for (int& v : pd[i]) v = newNum[v];
    }
    return pd;
}

static PDCode eraseOneNugatory(PDCode pd, size_t index) {
    if (uniqueCount(pd[index]) != 4) throw std::runtime_error("nugatory erase requires R1-free PD code");
    int ax = pd[index][0];
    int bx = pd[index][1];
    int cx = pd[index][2];
    int dx = pd[index][3];
    std::map<int, int> nxt = getPreNext(pd).second;
    std::vector<int> loop(1, ax);
    size_t guard = valueSet(pd).size() + 1;
    while (true) {
        if (!nxt.count(loop.back())) throw std::runtime_error("broken loop while erasing nugatory crossing");
        int next = nxt[loop.back()];
        loop.push_back(next);
        if (next == ax) {
            loop.pop_back();
            break;
        }
        if (loop.size() > guard) throw std::runtime_error("failed to close PD loop while erasing nugatory crossing");
    }
    std::set<int> loopSet(loop.begin(), loop.end());
    if (!loopSet.count(ax) || !loopSet.count(bx) || !loopSet.count(cx) || !loopSet.count(dx)) {
        throw std::runtime_error("nugatory crossing arcs are not in one component");
    }
    PDCode bad = pd;
    bad.erase(bad.begin() + static_cast<std::ptrdiff_t>(index));
    bad = replaceArcValue(bad, ax, cx);
    bad = replaceArcValue(bad, dx, bx);
    return renumberFullDfs(bad);
}

static PDCode simplifyPDCode(PDCode pd) {
    pd = eraseR1(pd);
    while (true) {
        int index = findNugatory(pd);
        if (index < 0) break;
        pd = eraseOneNugatory(pd, static_cast<size_t>(index));
    }
    return pd;
}

static int komplexSize(const Komplex& k) {
    int total = 0;
    for (auto& c : k.columns) total += c.n;
    return total;
}

static Komplex generateFast(const std::vector<std::vector<int> >& pd, const std::vector<int>& xsigns) {
    KH_PROFILE(generateFast);
    if (pd.empty()) {
        Komplex kom(1);
        kom.columns[0] = SmoothingColumn(1);
        kom.columns[0].smoothings[0] = cacheCap(Cap(0, 1));
        kom.reduce();
        return kom;
    }
    std::vector<char> in(pd.size() * 4, 0), done(pd.size(), 0);
    std::vector<std::vector<int> > pd1(1, std::vector<int>{0,1,2,3});
    Komplex kplus(pd1, std::vector<int>{1}, 4);
    Komplex kminus(pd1, std::vector<int>{-1}, 4);
    std::vector<int> edges;
    int firstdepth = pd.size() > 4 ? 3 : static_cast<int>(pd.size()) - 1;
    std::vector<int> firstdummy(firstdepth + 1, 0);
    int first = g_options.reorderCrossings ? chooseXingRecursive(edges, pd, in, done, firstdepth, firstdummy)
                                           : takeNextCrossing(edges, pd, in, done, firstdepth, firstdummy);
    Komplex kom = xsigns[first] == 1 ? kplus : kminus;
    edges = pd[first];
    int nedges = 4;
    done[first] = 1;
    for (int i = 0; i < 4; ++i) in[pd[first][i]] = 1;
    for (size_t i = 1; i < pd.size(); ++i) {
        int depth = static_cast<int>(pd.size() - i - 1);
        if (depth > 3) depth = 3;
        std::vector<int> dummy(depth + 1, 0);
        int best = g_options.reorderCrossings ? chooseXingRecursive(edges, pd, in, done, depth, dummy)
                                              : takeNextCrossing(edges, pd, in, done, depth, dummy);
        int nbest = 0;
        for (int j = 0; j < 4; ++j) if (in[pd[best][j]]) nbest++;
        int start;
        for (start = 0; start < nedges; ++start) {
            bool found = false;
            for (int j = 0; j < 4; ++j) if (pd[best][j] == edges[start]) { found = true; break; }
            if (!found) break;
        }
        if (start == nedges) start = 0;
        for (int j = 0; j < nedges; ++j) {
            bool found = false;
            for (int k = 0; k < 4; ++k) if (pd[best][k] == edges[(start + j) % nedges]) { found = true; start = (start + j) % nedges; break; }
            if (found) break;
        }
        int kstart = -1;
        for (int j = 0; j < 4; ++j) if (pd[best][j] == edges[start]) { kstart = j; break; }
        for (int j = 0; j < nbest; ++j) if (pd[best][(-j + kstart + 4) % 4] != edges[(start + j) % nedges])
            throw std::runtime_error("ordered crossing prefix is not a simply connected tangle");
        kstart = (kstart + 4 - nbest + 1) % 4;
        if (g_options.progress) std::cerr << "add crossing " << (i + 1) << "/" << pd.size() << "  girth: " << nedges << "\n";
        kom = kom.compose(start, xsigns[best] == 1 ? kplus : kminus, kstart, nbest);
        flushCobCache();
        kom.reduce();
        if (g_options.progress) std::cerr << "size: " << komplexSize(kom) << "\n";
        std::vector<int> newedges(nedges + 4 - 2 * nbest);
        int n = 0;
        for (; n < nedges - nbest; ++n) newedges[n] = edges[(start + nbest + n) % nedges];
        for (int j = 0; j < 4 - nbest; ++j, ++n) newedges[n] = pd[best][(kstart + nbest + j) % 4];
        edges.swap(newedges);
        nedges = static_cast<int>(edges.size());
        done[best] = 1;
        for (int j = 0; j < 4; ++j) in[pd[best][j]] = 1;
        flushCobCache();
    }
    return kom;
}

static std::vector<std::vector<int> > parsePDLine(const std::string& line) {
    std::vector<std::vector<int> > pd;
    size_t pos = 0;
    while (true) {
        pos = line.find("X[", pos);
        if (pos == std::string::npos) break;
        pos += 2;
        std::vector<int> nums;
        while (pos < line.size() && line[pos] != ']') {
            while (pos < line.size() && !(line[pos] == '-' || std::isdigit(static_cast<unsigned char>(line[pos])))) ++pos;
            if (pos >= line.size() || line[pos] == ']') break;
            int sign = 1;
            if (line[pos] == '-') { sign = -1; ++pos; }
            int v = 0;
            while (pos < line.size() && std::isdigit(static_cast<unsigned char>(line[pos]))) v = v * 10 + (line[pos++] - '0');
            nums.push_back(sign * v - 1);
        }
        if (nums.size() == 4) pd.push_back(nums);
    }
    if (!pd.empty()) return pd;
    pos = 0;
    while (true) {
        pos = line.find('[', pos);
        if (pos == std::string::npos) break;
        ++pos;
        std::vector<int> nums;
        while (pos < line.size() && line[pos] != ']') {
            while (pos < line.size() && !(line[pos] == '-' || std::isdigit(static_cast<unsigned char>(line[pos])) || line[pos] == ']')) ++pos;
            if (pos >= line.size() || line[pos] == ']') break;
            int sign = 1;
            if (line[pos] == '-') { sign = -1; ++pos; }
            int v = 0;
            while (pos < line.size() && std::isdigit(static_cast<unsigned char>(line[pos]))) v = v * 10 + (line[pos++] - '0');
            nums.push_back(sign * v - 1);
        }
        if (nums.size() == 4) pd.push_back(nums);
    }
    return pd;
}

static std::vector<std::pair<std::string, std::vector<std::vector<int> > > > parsePDDocument(const std::string& text, const std::string& labelPrefix) {
    std::vector<std::pair<std::string, std::vector<std::vector<int> > > > result;
    size_t pos = 0;
    int index = 0;
    while (true) {
        size_t start = text.find("PD[", pos);
        if (start == std::string::npos) break;
        int depth = 0;
        size_t end = std::string::npos;
        for (size_t i = start + 2; i < text.size(); ++i) {
            if (text[i] == '[') ++depth;
            else if (text[i] == ']') {
                --depth;
                if (depth == 0) {
                    end = i;
                    break;
                }
            }
        }
        if (end == std::string::npos) throw std::runtime_error("unterminated PD[...] block in " + labelPrefix);
        std::string block = text.substr(start, end - start + 1);
        std::string label = labelPrefix;
        if (index > 0) label += "#" + std::to_string(index + 1);
        result.push_back(std::make_pair(label, parsePDLine(block)));
        ++index;
        pos = end + 1;
    }
    if (result.empty()) {
        std::istringstream lines(text);
        std::string line;
        while (std::getline(lines, line)) {
            std::vector<std::vector<int> > pd = parsePDLine(line);
            if (pd.empty()) continue;
            std::string label = labelPrefix;
            size_t colon = line.find(':');
            if (colon != std::string::npos) {
                std::string lineLabel = line.substr(0, colon);
                size_t first = lineLabel.find_first_not_of(" \t\r\n");
                size_t last = lineLabel.find_last_not_of(" \t\r\n");
                if (first != std::string::npos && last != std::string::npos) label += ":" + lineLabel.substr(first, last - first + 1);
            } else if (!result.empty()) {
                label += "#" + std::to_string(result.size() + 1);
            }
            result.push_back(std::make_pair(label, pd));
        }
    }
    return result;
}

static std::vector<std::pair<std::string, std::vector<std::vector<int> > > > readPDFile(const std::string& path) {
    std::ifstream in(path.c_str(), std::ios::in | std::ios::binary);
    if (!in) throw std::runtime_error("cannot open " + path);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    std::vector<std::pair<std::string, std::vector<std::vector<int> > > > result = parsePDDocument(buffer.str(), path);
    if (result.size() == 1) result[0].first = path;
    return result;
}

static bool isDirectory(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0 && (st.st_mode & S_IFDIR);
}

static bool hasPdExtension(const std::string& name) {
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](char c){ return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); });
    return lower.size() >= 4 && (lower.substr(lower.size() - 4) == ".txt" || lower.substr(lower.size() - 3) == ".pd");
}

static std::vector<std::string> listInputFiles(const std::string& dir) {
    std::vector<std::string> files;
    DIR* d = opendir(dir.c_str());
    if (!d) throw std::runtime_error("cannot open directory " + dir);
    while (dirent* ent = readdir(d)) {
        std::string name = ent->d_name;
        if (name == "." || name == ".." || !hasPdExtension(name)) continue;
        std::string path = dir;
        if (!path.empty() && path[path.size() - 1] != '/' && path[path.size() - 1] != '\\') path += "/";
        path += name;
        if (!isDirectory(path)) files.push_back(path);
    }
    closedir(d);
    std::sort(files.begin(), files.end());
    return files;
}

static std::string computePD(const std::vector<std::vector<int> >& pd) {
    flushCobCache();
    g_smallArena.reset();
    PDCode working = g_options.simplifyPD ? simplifyPDCode(pd) : pd;
    Komplex k = generateFast(working, getSigns(working));
    ProfileScope khScope(g_profile.kh);
    return k.KhForZ();
}

static std::string formatPDCode(const PDCode& pd) {
    std::ostringstream out;
    out << "PD[";
    for (size_t i = 0; i < pd.size(); ++i) {
        if (i) out << ",";
        out << "X[";
        for (int j = 0; j < 4; ++j) {
            if (j) out << ",";
            out << (pd[i][j] + 1);
        }
        out << "]";
    }
    out << "]";
    return out.str();
}

static std::string simplifiedPDString(const PDCode& pd) {
    PDCode working = g_options.simplifyPD ? simplifyPDCode(pd) : pd;
    return formatPDCode(working);
}

} // namespace kh

#if defined(CPPKH_SHARED_LIBRARY)
#if defined(_WIN32)
#define CPPKH_API extern "C" __declspec(dllexport)
#else
#define CPPKH_API extern "C" __attribute__((visibility("default")))
#endif

static thread_local std::string g_cppkhLastError;

static char* cppkhDuplicateString(const std::string& value) {
    char* out = static_cast<char*>(std::malloc(value.size() + 1));
    if (!out) {
        g_cppkhLastError = "out of memory";
        return nullptr;
    }
    std::memcpy(out, value.c_str(), value.size() + 1);
    return out;
}

struct CppkhOptionsGuard {
    kh::Options oldOptions;
    CppkhOptionsGuard() : oldOptions(kh::g_options) {}
    ~CppkhOptionsGuard() { kh::g_options = oldOptions; }
};

CPPKH_API const char* cppkh_version() {
    return "cppkh/1";
}

CPPKH_API const char* cppkh_last_error() {
    return g_cppkhLastError.c_str();
}

CPPKH_API void cppkh_free(char* value) {
    std::free(value);
}

CPPKH_API char* cppkh_compute_pd_ex(const char* pd_code, int simplify_pd, int reorder_crossings) {
    try {
        g_cppkhLastError.clear();
        if (!pd_code) throw std::runtime_error("pd_code is null");
        std::vector<std::pair<std::string, kh::PDCode> > parsed = kh::parsePDDocument(pd_code, "ctypes");
        if (parsed.empty()) throw std::runtime_error("no PD code found");
        if (parsed.size() != 1) throw std::runtime_error("cppkh_compute_pd_ex expects exactly one PD code");

        CppkhOptionsGuard guard;
        kh::g_options.progress = false;
        kh::g_options.profile = false;
        kh::g_options.simplifyPD = simplify_pd != 0;
        kh::g_options.reorderCrossings = reorder_crossings != 0;
        std::string result = kh::computePD(parsed[0].second);
        return cppkhDuplicateString(result);
    } catch (const std::exception& e) {
        g_cppkhLastError = e.what();
        return nullptr;
    } catch (...) {
        g_cppkhLastError = "unknown error";
        return nullptr;
    }
}

CPPKH_API char* cppkh_compute_pd(const char* pd_code) {
    return cppkh_compute_pd_ex(pd_code, 1, 1);
}

CPPKH_API char* cppkh_compute_pd_batch_ex(const char* pd_codes, int simplify_pd, int reorder_crossings) {
    try {
        g_cppkhLastError.clear();
        if (!pd_codes) throw std::runtime_error("pd_codes is null");
        std::vector<std::pair<std::string, kh::PDCode> > parsed = kh::parsePDDocument(pd_codes, "ctypes-batch");
        if (parsed.empty()) throw std::runtime_error("no PD code found");

        CppkhOptionsGuard guard;
        kh::g_options.progress = false;
        kh::g_options.profile = false;
        kh::g_options.simplifyPD = simplify_pd != 0;
        kh::g_options.reorderCrossings = reorder_crossings != 0;

        std::ostringstream out;
        for (size_t i = 0; i < parsed.size(); ++i) {
            if (i) out << "\n";
            out << kh::computePD(parsed[i].second);
        }
        return cppkhDuplicateString(out.str());
    } catch (const std::exception& e) {
        g_cppkhLastError = e.what();
        return nullptr;
    } catch (...) {
        g_cppkhLastError = "unknown error";
        return nullptr;
    }
}

CPPKH_API char* cppkh_compute_pd_batch(const char* pd_codes) {
    return cppkh_compute_pd_batch_ex(pd_codes, 1, 1);
}

CPPKH_API char* cppkh_simplify_pd(const char* pd_code) {
    try {
        g_cppkhLastError.clear();
        if (!pd_code) throw std::runtime_error("pd_code is null");
        std::vector<std::pair<std::string, kh::PDCode> > parsed = kh::parsePDDocument(pd_code, "ctypes");
        if (parsed.empty()) throw std::runtime_error("no PD code found");
        if (parsed.size() != 1) throw std::runtime_error("cppkh_simplify_pd expects exactly one PD code");
        CppkhOptionsGuard guard;
        kh::g_options.simplifyPD = true;
        std::string result = kh::simplifiedPDString(parsed[0].second);
        return cppkhDuplicateString(result);
    } catch (const std::exception& e) {
        g_cppkhLastError = e.what();
        return nullptr;
    } catch (...) {
        g_cppkhLastError = "unknown error";
        return nullptr;
    }
}
#endif

#ifndef CPPKH_SHARED_LIBRARY
static void usage() {
    std::cout << "Usage: cppkh [--pd-file FILE] [--pd-dir DIR] [--pd-code CODE] [--ordered] [--threads N|auto] [--quiet] [--profile] [--no-simplify-pd] [--print-simplified-pd]\n";
    std::cout << "Thread backend: " << KH_THREAD_BACKEND_NAME << "\n";
    std::cout << "Detected CPU threads: " << kh::detectHardwareThreads() << "\n";
    std::cout << "PD simplification: R1 removal then nugatory crossing removal is enabled by default.\n";
}

int main(int argc, char** argv) {
    try {
        std::vector<std::string> files;
        std::vector<std::pair<std::string, std::vector<std::vector<int> > > > jobs;
        bool printSimplifiedPD = false;
        for (int i = 1; i < argc; ++i) {
            std::string a = argv[i];
            if (a == "--help" || a == "-h") { usage(); return 0; }
            else if (a == "--pd-file" || a == "-f") {
                if (++i >= argc) throw std::runtime_error("--pd-file needs a path");
                files.push_back(argv[i]);
            } else if (a == "--pd-dir" || a == "-d") {
                if (++i >= argc) throw std::runtime_error("--pd-dir needs a path");
                std::vector<std::string> more = kh::listInputFiles(argv[i]);
                files.insert(files.end(), more.begin(), more.end());
            } else if (a == "--pd-code" || a == "-c") {
                if (++i >= argc) throw std::runtime_error("--pd-code needs a PD[...] string");
                std::vector<std::pair<std::string, std::vector<std::vector<int> > > > parsed = kh::parsePDDocument(argv[i], "command-line");
                jobs.insert(jobs.end(), parsed.begin(), parsed.end());
            } else if (a == "--ordered" || a == "-O") kh::g_options.reorderCrossings = false;
            else if (a == "--quiet" || a == "-q") kh::g_options.progress = false;
            else if (a == "--profile") kh::g_options.profile = true;
            else if (a == "--no-simplify-pd" || a == "--raw-pd") kh::g_options.simplifyPD = false;
            else if (a == "--simplify-pd") kh::g_options.simplifyPD = true;
            else if (a == "--print-simplified-pd") printSimplifiedPD = true;
            else if (a == "--threads" || a == "-j") {
                if (++i >= argc) throw std::runtime_error("--threads needs a number");
                std::string v = argv[i];
                if (v == "auto" || v == "AUTO" || v == "0") kh::g_options.matrixThreads = 0;
                else kh::g_options.matrixThreads = std::max(1, std::atoi(argv[i]));
            } else if (!a.empty() && a[0] != '-') {
                if (kh::isDirectory(a)) {
                    std::vector<std::string> more = kh::listInputFiles(a);
                    files.insert(files.end(), more.begin(), more.end());
                } else files.push_back(a);
            } else throw std::runtime_error("unknown option: " + a);
        }
        if (files.empty() && jobs.empty()) files.push_back("PD.txt");

        for (size_t i = 0; i < files.size(); ++i) {
            auto parsed = kh::readPDFile(files[i]);
            jobs.insert(jobs.end(), parsed.begin(), parsed.end());
        }
        if (jobs.empty()) throw std::runtime_error("no PD code found");
        bool label = jobs.size() > 1;
        for (size_t i = 0; i < jobs.size(); ++i) {
            uint64_t profileStart = 0;
            if (kh::g_options.profile) {
                kh::g_profile.reset();
                profileStart = kh::profileNowNs();
            }
            if (label) std::cout << jobs[i].first << "\t";
            if (printSimplifiedPD) std::cout << kh::simplifiedPDString(jobs[i].second) << "\n";
            else std::cout << "\"" << kh::computePD(jobs[i].second) << "\"\n";
            if (kh::g_options.profile) {
                kh::g_profile.totalNs = kh::profileNowNs() - profileStart;
                kh::printProfile();
            }
        }
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
}
#endif
