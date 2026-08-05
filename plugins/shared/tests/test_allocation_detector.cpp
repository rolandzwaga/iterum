// ==============================================================================
// AllocationDetector thread filter (Seraphis Phase 12 - T013 / FR-028a)
// ==============================================================================
// Covers the additive, default-OFF per-thread opt-in added to
// tests/test_helpers/allocation_detector.h:
//
//   * clause 1 - with the filter OFF (the default, and the state every one of
//     the existing consumers runs in), an allocation on ANY thread is still
//     counted. This is the inertness proof: if this case ever goes red, the
//     "additive" claim is false and every pre-existing allocation criterion in
//     dsp_systems_tests / dsp_effects_tests / seraphis_tests / membrum_tests /
//     innexus_tests is silently weakened.
//   * clause 2 - with a ThreadScopedAllocationScope alive, an allocation on a
//     DIFFERENT thread contributes NOTHING to the counter, while an allocation
//     on the opted-in thread does.
//   * clause 3 - the scope restores both the filter flag and the thread's
//     opt-in on exit, so a test that uses it cannot leak the filter into the
//     next test case in the same binary.
//
// This TU owns the global allocation-operator replacements for shared_tests.
// They live in ONE shared header so the matched set and its visibility cannot
// drift per-TU; a second include in this binary is a duplicate-symbol link
// error, and a hand-rolled copy is caught by
// tools/lint-allocation-operator-overrides.js. Verified before this file was
// added: `grep -rn "allocation_operator_overrides" plugins/shared/tests`
// returned nothing, so shared_tests had no other owner. WITHOUT the overrides
// AllocationDetector counts nothing at all and every assertion below would pass
// vacuously at 0.
//
// Allocations are made through DIRECT calls to ::operator new / ::operator
// delete rather than through new-expressions: [expr.new] lets an implementation
// omit an allocation made by a new-expression, and an elided allocation would
// make clause 1 flaky at -O2 for reasons that have nothing to do with the
// filter.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>

#include <allocation_detector.h>

#include <cstddef>
#include <cstring>
#include <new>
#include <thread>

// Global allocation-operator replacements - exactly one TU per test binary.
#include <allocation_operator_overrides.h>

namespace {

constexpr std::size_t kProbeBytes = 1024;

// Allocate and free once, returning how much the detector's counter moved.
// A delta (not an absolute read) is mandatory here: the counter is a
// process-wide singleton and std::thread's own construction allocates on the
// SPAWNING thread, so an absolute read on the worker would already include the
// parent's bookkeeping and could never be 0.
[[nodiscard]] std::size_t allocationDeltaHere()
{
    auto& detector = TestHelpers::AllocationDetector::instance();
    const std::size_t before = detector.getAllocationCount();

    void* raw = ::operator new(kProbeBytes);
    std::memset(raw, 0, kProbeBytes);
    ::operator delete(raw);

    return detector.getAllocationCount() - before;
}

} // namespace

TEST_CASE("TestHelpers_AllocationDetector_ThreadFilterIsOptIn", "[test_helpers]")
{
    auto& detector = TestHelpers::AllocationDetector::instance();

    // The filter is OFF unless something opted in. Asserted first so a leaked
    // filter from an earlier case fails here, naming the real cause, instead of
    // corrupting clause 1's numbers.
    REQUIRE_FALSE(detector.threadFilterEnabled());

    SECTION("clause 1: filter off (default) counts allocations on every thread")
    {
        // AllocationScope latches its count in its DESTRUCTOR
        // (allocation_detector.h AllocationScope), so the counter is sampled
        // from the singleton while the scope is still OPEN.
        std::size_t mainDelta = 0;
        std::size_t workerDelta = 0;

        {
            [[maybe_unused]] const TestHelpers::AllocationScope scope;

            std::thread worker([&workerDelta]() { workerDelta = allocationDeltaHere(); });
            worker.join();

            mainDelta = allocationDeltaHere();
        }

        INFO("filter off: main-thread delta = " << mainDelta
             << ", worker-thread delta = " << workerDelta);
        REQUIRE(mainDelta > 0);
        REQUIRE(workerDelta > 0);
        REQUIRE_FALSE(detector.threadFilterEnabled());
    }

    SECTION("clause 2: ThreadScopedAllocationScope counts only the opted-in thread")
    {
        std::size_t optedInDelta = 0;
        std::size_t otherThreadDelta = 0;
        std::size_t scopeCount = 0;

        {
            [[maybe_unused]] const TestHelpers::ThreadScopedAllocationScope scope;

            REQUIRE(detector.threadFilterEnabled());

            // The worker never opts in, so EVERY allocation on it - including
            // any the standard library makes on its behalf - is suppressed.
            std::thread worker([&otherThreadDelta]() { otherThreadDelta = allocationDeltaHere(); });
            worker.join();

            optedInDelta = allocationDeltaHere();
            scopeCount = TestHelpers::AllocationDetector::instance().getAllocationCount();
        }

        INFO("filter on: opted-in delta = " << optedInDelta
             << ", other-thread delta = " << otherThreadDelta);
        REQUIRE(otherThreadDelta == 0);
        REQUIRE(optedInDelta > 0);
        REQUIRE(scopeCount > 0);
    }

    SECTION("clause 3: the scope restores the filter and the thread opt-in on exit")
    {
        {
            [[maybe_unused]] const TestHelpers::ThreadScopedAllocationScope scope;
            REQUIRE(detector.threadFilterEnabled());
            REQUIRE(TestHelpers::tAllocationTrackThisThread);
        }

        REQUIRE_FALSE(detector.threadFilterEnabled());
        REQUIRE_FALSE(TestHelpers::tAllocationTrackThisThread);

        // ...and the pre-existing, unfiltered behaviour is back.
        std::size_t mainDelta = 0;
        {
            [[maybe_unused]] const TestHelpers::AllocationScope scope;
            mainDelta = allocationDeltaHere();
        }
        REQUIRE(mainDelta > 0);
    }
}
