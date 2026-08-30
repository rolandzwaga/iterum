#pragma once
// ==============================================================================
// Allocation Detector
// ==============================================================================
// Detects memory allocations during test execution.
// Used to verify real-time safety of audio processing code.
// See specs/TESTING-GUIDE.md for usage guidance.
//
// IMPORTANT: This is a simplified detector for testing purposes.
// In production, use platform-specific tools like:
// - Valgrind (Linux)
// - Instruments (macOS)
// - Application Verifier (Windows)
// ==============================================================================

#include <atomic>
#include <cstddef>
#include <cstdlib>

namespace TestHelpers {

// ==============================================================================
// Per-thread opt-in (default OFF)
// ==============================================================================
/// Set for a thread whose allocations should still be counted while the
/// detector's thread filter is enabled. The filter itself is OFF by default, so
/// this flag is inert for every caller that does not deliberately turn the
/// filter on: with the filter off, recordAllocation() never reads it.
///
/// Allocation-free on first touch because this header is only ever linked into
/// test EXECUTABLES (local-exec / initial-exec TLS -> static TLS area, allocated
/// at thread creation).
/// DO NOT use from a dynamically loaded module (.so/.dll loaded at run time):
/// general-dynamic TLS allocates on first touch via __tls_get_addr, and
/// allocation_operator_overrides.h:66-94 calls recordAllocation() from operator
/// new itself - that would be re-entrancy.
///
/// It is deliberately a mutable namespace-scope variable: a thread opts itself
/// in by assignment, and thread_local storage duration is the whole point.
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
inline thread_local bool tAllocationTrackThisThread = false;

// ==============================================================================
// Allocation Tracking
// ==============================================================================
// Thread-safe counter for tracking allocations

class AllocationDetector {
public:
    AllocationDetector() = default;

    // Start tracking allocations
    void startTracking() {
        allocationCount_.store(0, std::memory_order_relaxed);
        tracking_.store(true, std::memory_order_release);
    }

    // Stop tracking and return count
    size_t stopTracking() {
        tracking_.store(false, std::memory_order_release);
        return allocationCount_.load(std::memory_order_acquire);
    }

    // Check if currently tracking
    bool isTracking() const {
        return tracking_.load(std::memory_order_acquire);
    }

    // Get current count without stopping
    size_t getAllocationCount() const {
        return allocationCount_.load(std::memory_order_acquire);
    }

    // Restrict counting to threads that set tAllocationTrackThisThread.
    // DEFAULT OFF - every pre-existing usage is unchanged by this switch.
    void setThreadFilterEnabled(bool enabled) noexcept {
        threadFilter_.store(enabled, std::memory_order_release);
    }

    [[nodiscard]] bool threadFilterEnabled() const noexcept {
        return threadFilter_.load(std::memory_order_acquire);
    }

    // Record an allocation (called by overridden new)
    void recordAllocation() {
        // Filter off (the default) => the thread-local is never read and the
        // behaviour below is byte-for-byte the pre-filter behaviour.
        if (threadFilter_.load(std::memory_order_acquire) && !tAllocationTrackThisThread) return;
        if (tracking_.load(std::memory_order_acquire)) {
            allocationCount_.fetch_add(1, std::memory_order_relaxed);
        }
    }

    // Singleton access
    static AllocationDetector& instance() {
        static AllocationDetector detector;
        return detector;
    }

private:
    std::atomic<bool> tracking_{false};
    std::atomic<size_t> allocationCount_{0};
    std::atomic<bool> threadFilter_{false};
};

// ==============================================================================
// RAII Tracking Scope
// ==============================================================================
// Automatically starts/stops tracking within a scope

class AllocationScope {
public:
    AllocationScope() {
        AllocationDetector::instance().startTracking();
    }

    ~AllocationScope() {
        count_ = AllocationDetector::instance().stopTracking();
    }

    size_t getAllocationCount() const {
        return count_;
    }

    bool hadAllocations() const {
        return count_ > 0;
    }

private:
    size_t count_ = 0;
};

// ==============================================================================
// RAII Thread-Scoped Tracking Scope
// ==============================================================================
// Like AllocationScope, but counts ONLY allocations made on the thread that
// constructed it. Every other thread's allocations are ignored for the lifetime
// of the scope.
//
// Both the detector's filter flag and this thread's opt-in are RESTORED on
// destruction, so a scope cannot leak the filter into whatever runs next in the
// same binary - which is what keeps the default-off guarantee true for the
// existing AllocationScope consumers.
//
// Like AllocationScope, the count is latched in the DESTRUCTOR: to read it while
// the scope is still open, call
// AllocationDetector::instance().getAllocationCount().

class ThreadScopedAllocationScope {
public:
    ThreadScopedAllocationScope() {
        auto& detector = AllocationDetector::instance();
        priorFilterEnabled_ = detector.threadFilterEnabled();
        priorThreadOptIn_ = tAllocationTrackThisThread;
        tAllocationTrackThisThread = true;
        detector.setThreadFilterEnabled(true);
        detector.startTracking();
    }

    ~ThreadScopedAllocationScope() {
        auto& detector = AllocationDetector::instance();
        count_ = detector.stopTracking();
        detector.setThreadFilterEnabled(priorFilterEnabled_);
        tAllocationTrackThisThread = priorThreadOptIn_;
    }

    ThreadScopedAllocationScope(const ThreadScopedAllocationScope&) = delete;
    ThreadScopedAllocationScope& operator=(const ThreadScopedAllocationScope&) = delete;
    ThreadScopedAllocationScope(ThreadScopedAllocationScope&&) = delete;
    ThreadScopedAllocationScope& operator=(ThreadScopedAllocationScope&&) = delete;

    size_t getAllocationCount() const {
        return count_;
    }

    bool hadAllocations() const {
        return count_ > 0;
    }

private:
    bool priorFilterEnabled_ = false;
    bool priorThreadOptIn_ = false;
    size_t count_ = 0;
};

} // namespace TestHelpers

// ==============================================================================
// Global Operator Overrides (Optional)
// ==============================================================================
// Uncomment these to enable automatic allocation tracking.
// NOTE: This can interfere with some testing frameworks.
// Use with caution and only when specifically testing real-time safety.
//
// #ifdef ENABLE_ALLOCATION_TRACKING
//
// void* operator new(std::size_t size) {
//     TestHelpers::AllocationDetector::instance().recordAllocation();
//     void* p = std::malloc(size);
//     if (!p) throw std::bad_alloc();
//     return p;
// }
//
// void* operator new[](std::size_t size) {
//     TestHelpers::AllocationDetector::instance().recordAllocation();
//     void* p = std::malloc(size);
//     if (!p) throw std::bad_alloc();
//     return p;
// }
//
// void operator delete(void* p) noexcept {
//     std::free(p);
// }
//
// void operator delete[](void* p) noexcept {
//     std::free(p);
// }
//
// void operator delete(void* p, std::size_t) noexcept {
//     std::free(p);
// }
//
// void operator delete[](void* p, std::size_t) noexcept {
//     std::free(p);
// }
//
// #endif // ENABLE_ALLOCATION_TRACKING
