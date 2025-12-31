// Layer 2: DSP Processor Tests - Grain Scheduler
// Part of Granular Delay feature (spec 034)

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "dsp/processors/grain_scheduler.h"

#include <array>
#include <cmath>

using namespace Iterum::DSP;
using Catch::Approx;

// =============================================================================
// GrainScheduler Lifecycle Tests
// =============================================================================

TEST_CASE("GrainScheduler prepare and reset lifecycle", "[processors][scheduler][layer2]") {
    GrainScheduler scheduler;

    SECTION("prepare initializes scheduler") {
        scheduler.prepare(44100.0);
        REQUIRE(scheduler.getDensity() == Approx(10.0f));  // Default density
    }

    SECTION("reset clears state") {
        scheduler.prepare(44100.0);
        scheduler.setDensity(50.0f);

        // Process some samples
        for (int i = 0; i < 1000; ++i) {
            scheduler.process();
        }

        scheduler.reset();

        // After reset, first call should trigger (or be close to triggering)
        // We're testing that reset doesn't leave scheduler in a bad state
        int triggerCount = 0;
        for (int i = 0; i < 10000; ++i) {
            if (scheduler.process()) {
                ++triggerCount;
            }
        }
        // Should have triggered at least a few times at 50 grains/sec over 10000 samples
        REQUIRE(triggerCount > 0);
    }
}

// =============================================================================
// Density Control Tests
// =============================================================================

TEST_CASE("GrainScheduler density control", "[processors][scheduler][layer2]") {
    GrainScheduler scheduler;
    scheduler.prepare(44100.0);

    SECTION("setDensity changes trigger rate") {
        scheduler.setDensity(10.0f);
        REQUIRE(scheduler.getDensity() == Approx(10.0f));

        scheduler.setDensity(50.0f);
        REQUIRE(scheduler.getDensity() == Approx(50.0f));
    }

    SECTION("density is clamped to minimum 0.1") {
        scheduler.setDensity(0.0f);
        REQUIRE(scheduler.getDensity() >= 0.1f);

        scheduler.setDensity(-10.0f);
        REQUIRE(scheduler.getDensity() >= 0.1f);
    }

    SECTION("higher density produces more triggers") {
        scheduler.seed(12345);

        // Count triggers at low density
        scheduler.setDensity(5.0f);
        scheduler.reset();
        int lowCount = 0;
        for (int i = 0; i < 44100; ++i) {  // 1 second
            if (scheduler.process()) ++lowCount;
        }

        // Count triggers at high density
        scheduler.seed(12345);
        scheduler.setDensity(50.0f);
        scheduler.reset();
        int highCount = 0;
        for (int i = 0; i < 44100; ++i) {  // 1 second
            if (scheduler.process()) ++highCount;
        }

        // High density should produce more triggers
        REQUIRE(highCount > lowCount);
    }
}

// =============================================================================
// Trigger Rate Tests
// =============================================================================

TEST_CASE("GrainScheduler trigger rate accuracy", "[processors][scheduler][layer2]") {
    GrainScheduler scheduler;
    scheduler.prepare(44100.0);

    SECTION("10 grains/sec produces ~10 triggers per second") {
        scheduler.setDensity(10.0f);
        scheduler.seed(42);
        scheduler.reset();

        int triggerCount = 0;
        const int numSamples = 44100 * 10;  // 10 seconds for averaging

        for (int i = 0; i < numSamples; ++i) {
            if (scheduler.process()) ++triggerCount;
        }

        float triggersPerSecond = static_cast<float>(triggerCount) / 10.0f;

        // Allow 20% tolerance due to stochastic jitter
        REQUIRE(triggersPerSecond >= 8.0f);
        REQUIRE(triggersPerSecond <= 12.0f);
    }

    SECTION("100 grains/sec produces ~100 triggers per second") {
        scheduler.setDensity(100.0f);
        scheduler.seed(42);
        scheduler.reset();

        int triggerCount = 0;
        const int numSamples = 44100 * 5;  // 5 seconds

        for (int i = 0; i < numSamples; ++i) {
            if (scheduler.process()) ++triggerCount;
        }

        float triggersPerSecond = static_cast<float>(triggerCount) / 5.0f;

        // Allow 20% tolerance
        REQUIRE(triggersPerSecond >= 80.0f);
        REQUIRE(triggersPerSecond <= 120.0f);
    }
}

// =============================================================================
// Scheduling Mode Tests
// =============================================================================

TEST_CASE("GrainScheduler scheduling modes", "[processors][scheduler][layer2]") {
    GrainScheduler scheduler;
    scheduler.prepare(44100.0);

    SECTION("default mode is asynchronous") {
        REQUIRE(scheduler.getMode() == SchedulingMode::Asynchronous);
    }

    SECTION("setMode changes mode") {
        scheduler.setMode(SchedulingMode::Synchronous);
        REQUIRE(scheduler.getMode() == SchedulingMode::Synchronous);

        scheduler.setMode(SchedulingMode::Asynchronous);
        REQUIRE(scheduler.getMode() == SchedulingMode::Asynchronous);
    }

    SECTION("synchronous mode has regular intervals") {
        scheduler.setMode(SchedulingMode::Synchronous);
        scheduler.setDensity(10.0f);  // 4410 samples between triggers at 44100 Hz
        scheduler.reset();

        // Collect trigger times
        std::array<int, 10> triggerTimes{};
        int triggerIndex = 0;

        for (int i = 0; i < 50000 && triggerIndex < 10; ++i) {
            if (scheduler.process()) {
                triggerTimes[triggerIndex++] = i;
            }
        }

        // Check intervals are approximately equal
        for (size_t i = 1; i < 9; ++i) {
            int interval = triggerTimes[i + 1] - triggerTimes[i];
            int expected = 44100 / 10;  // 4410 samples

            // Should be very close for synchronous mode
            REQUIRE(std::abs(interval - expected) < 10);
        }
    }

    SECTION("asynchronous mode has stochastic variation") {
        scheduler.setMode(SchedulingMode::Asynchronous);
        scheduler.setDensity(10.0f);
        scheduler.seed(12345);
        scheduler.reset();

        // Collect trigger intervals
        std::array<int, 20> intervals{};
        int lastTrigger = 0;
        int intervalIndex = 0;

        for (int i = 0; i < 100000 && intervalIndex < 20; ++i) {
            if (scheduler.process()) {
                if (lastTrigger > 0) {
                    intervals[intervalIndex++] = i - lastTrigger;
                }
                lastTrigger = i;
            }
        }

        // Check that intervals have variation (not all the same)
        int minInterval = intervals[0];
        int maxInterval = intervals[0];
        for (int interval : intervals) {
            minInterval = std::min(minInterval, interval);
            maxInterval = std::max(maxInterval, interval);
        }

        // Should have some variation (at least 10% of mean)
        REQUIRE(maxInterval > minInterval);
    }
}

// =============================================================================
// Jitter Control Tests (Phase 2.1)
// =============================================================================

TEST_CASE("GrainScheduler jitter control", "[processors][scheduler][layer2][jitter]") {
    GrainScheduler scheduler;
    scheduler.prepare(44100.0);
    scheduler.setMode(SchedulingMode::Asynchronous);
    scheduler.setDensity(20.0f);  // ~2200 samples per grain

    SECTION("setJitter stores jitter amount") {
        scheduler.setJitter(0.0f);
        REQUIRE(scheduler.getJitter() == Approx(0.0f));

        scheduler.setJitter(0.5f);
        REQUIRE(scheduler.getJitter() == Approx(0.5f));

        scheduler.setJitter(1.0f);
        REQUIRE(scheduler.getJitter() == Approx(1.0f));
    }

    SECTION("jitter is clamped to 0-1 range") {
        scheduler.setJitter(-0.5f);
        REQUIRE(scheduler.getJitter() >= 0.0f);

        scheduler.setJitter(1.5f);
        REQUIRE(scheduler.getJitter() <= 1.0f);
    }

    SECTION("zero jitter produces regular intervals in async mode") {
        scheduler.setJitter(0.0f);  // No jitter - should be like sync mode
        scheduler.seed(42);
        scheduler.reset();

        // Collect trigger intervals
        std::array<int, 10> intervals{};
        int lastTrigger = 0;
        int intervalIndex = 0;

        for (int i = 0; i < 50000 && intervalIndex < 10; ++i) {
            if (scheduler.process()) {
                if (lastTrigger > 0) {
                    intervals[intervalIndex++] = i - lastTrigger;
                }
                lastTrigger = i;
            }
        }

        // With zero jitter, all intervals should be nearly identical
        int expected = 44100 / 20;  // 2205 samples
        for (int interval : intervals) {
            REQUIRE(std::abs(interval - expected) < 10);  // Very small tolerance
        }
    }

    SECTION("high jitter produces large variation") {
        scheduler.setJitter(1.0f);  // Maximum jitter
        scheduler.seed(42);
        scheduler.reset();

        // Collect trigger intervals
        std::array<int, 20> intervals{};
        int lastTrigger = 0;
        int intervalIndex = 0;

        for (int i = 0; i < 100000 && intervalIndex < 20; ++i) {
            if (scheduler.process()) {
                if (lastTrigger > 0) {
                    intervals[intervalIndex++] = i - lastTrigger;
                }
                lastTrigger = i;
            }
        }

        // Calculate variance
        int minInterval = intervals[0];
        int maxInterval = intervals[0];
        for (int interval : intervals) {
            minInterval = std::min(minInterval, interval);
            maxInterval = std::max(maxInterval, interval);
        }

        // With high jitter, range should be significant
        // Expected interval ~2205, with full jitter should vary by ±50%
        int expectedRange = 44100 / 20;  // Base interval
        REQUIRE((maxInterval - minInterval) > expectedRange * 0.5f);
    }
}

// =============================================================================
// Reproducibility Tests
// =============================================================================

TEST_CASE("GrainScheduler seed produces reproducible sequence", "[processors][scheduler][layer2]") {
    GrainScheduler scheduler1;
    GrainScheduler scheduler2;

    scheduler1.prepare(44100.0);
    scheduler2.prepare(44100.0);

    scheduler1.setDensity(25.0f);
    scheduler2.setDensity(25.0f);

    SECTION("same seed produces same triggers") {
        scheduler1.seed(42);
        scheduler2.seed(42);
        scheduler1.reset();
        scheduler2.reset();

        std::array<int, 100> triggers1{};
        std::array<int, 100> triggers2{};
        int count1 = 0, count2 = 0;

        for (int i = 0; i < 20000; ++i) {
            if (scheduler1.process() && count1 < 100) {
                triggers1[count1++] = i;
            }
            if (scheduler2.process() && count2 < 100) {
                triggers2[count2++] = i;
            }
        }

        // Should have same number of triggers
        REQUIRE(count1 == count2);

        // Trigger times should match exactly
        for (int i = 0; i < count1; ++i) {
            REQUIRE(triggers1[i] == triggers2[i]);
        }
    }

    SECTION("different seeds produce different triggers") {
        scheduler1.seed(42);
        scheduler2.seed(999);
        scheduler1.reset();
        scheduler2.reset();

        bool foundDifference = false;
        int triggers1 = 0, triggers2 = 0;

        for (int i = 0; i < 10000; ++i) {
            bool t1 = scheduler1.process();
            bool t2 = scheduler2.process();

            if (t1) ++triggers1;
            if (t2) ++triggers2;

            // Check for different trigger pattern
            if (triggers1 > 5 && triggers2 > 5 && triggers1 != triggers2) {
                foundDifference = true;
            }
        }

        // At some point, counts should diverge
        REQUIRE(foundDifference);
    }
}
