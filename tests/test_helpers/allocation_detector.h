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
#include <cstdlib>

namespace TestHelpers {

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

    // Record an allocation (called by overridden new)
    void recordAllocation() {
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
