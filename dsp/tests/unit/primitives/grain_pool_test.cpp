// Layer 1: DSP Primitive Tests - Grain Pool
// Part of Granular Delay feature (spec 034)

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <krate/dsp/primitives/grain_pool.h>

#include <array>
#include <set>

using namespace Krate::DSP;
using Catch::Approx;

// =============================================================================
// Grain Struct Tests
// =============================================================================

TEST_CASE("Grain struct default initialization", "[primitives][grain][layer1]") {

    SECTION("default values are sensible") {
        Grain grain{};

        REQUIRE(grain.readPosition == 0.0f);
        REQUIRE(grain.playbackRate == 1.0f);
        REQUIRE(grain.envelopePhase == 0.0f);
        REQUIRE(grain.envelopeIncrement == 0.0f);
        REQUIRE(grain.amplitude == 1.0f);
        REQUIRE(grain.panL == 1.0f);
        REQUIRE(grain.panR == 1.0f);
        REQUIRE(grain.active == false);
        REQUIRE(grain.reverse == false);
        REQUIRE(grain.startSample == 0);
    }

    SECTION("fields can be assigned") {
        Grain grain{};
        grain.readPosition = 100.5f;
        grain.playbackRate = 2.0f;
        grain.envelopePhase = 0.5f;
        grain.active = true;
        grain.reverse = true;
        grain.startSample = 1000;

        REQUIRE(grain.readPosition == 100.5f);
        REQUIRE(grain.playbackRate == 2.0f);
        REQUIRE(grain.envelopePhase == 0.5f);
        REQUIRE(grain.active == true);
        REQUIRE(grain.reverse == true);
        REQUIRE(grain.startSample == 1000);
    }
}

// =============================================================================
// GrainPool Lifecycle Tests
// =============================================================================

TEST_CASE("GrainPool prepare and reset lifecycle", "[primitives][grain][layer1]") {
    GrainPool pool;

    SECTION("prepare initializes pool") {
        pool.prepare(44100.0);

        // Should have no active grains after prepare
        REQUIRE(pool.activeCount() == 0);
    }

    SECTION("reset clears all grains") {
        pool.prepare(44100.0);

        // Acquire some grains
        (void)pool.acquireGrain(0);
        (void)pool.acquireGrain(1);
        (void)pool.acquireGrain(2);
        REQUIRE(pool.activeCount() == 3);

        // Reset should clear all
        pool.reset();
        REQUIRE(pool.activeCount() == 0);
    }

    SECTION("maxGrains returns 64") {
        REQUIRE(GrainPool::maxGrains() == 64);
    }
}

// =============================================================================
// GrainPool Acquire/Release Tests
// =============================================================================

TEST_CASE("GrainPool acquireGrain allocates grains", "[primitives][grain][layer1]") {
    GrainPool pool;
    pool.prepare(44100.0);

    SECTION("acquireGrain returns valid grain pointer") {
        Grain* grain = pool.acquireGrain(0);

        REQUIRE(grain != nullptr);
        REQUIRE(grain->active == true);
        REQUIRE(grain->startSample == 0);
    }

    SECTION("multiple acquires return different grains") {
        std::set<Grain*> grains;

        for (size_t i = 0; i < 10; ++i) {
            Grain* grain = pool.acquireGrain(i);
            REQUIRE(grain != nullptr);
            grains.insert(grain);
        }

        // All grains should be unique
        REQUIRE(grains.size() == 10);
    }

    SECTION("activeCount tracks acquisitions") {
        REQUIRE(pool.activeCount() == 0);

        (void)pool.acquireGrain(0);
        REQUIRE(pool.activeCount() == 1);

        (void)pool.acquireGrain(1);
        REQUIRE(pool.activeCount() == 2);

        (void)pool.acquireGrain(2);
        REQUIRE(pool.activeCount() == 3);
    }
}

TEST_CASE("GrainPool releaseGrain frees grains", "[primitives][grain][layer1]") {
    GrainPool pool;
    pool.prepare(44100.0);

    SECTION("releaseGrain decrements active count") {
        Grain* grain1 = pool.acquireGrain(0);
        Grain* grain2 = pool.acquireGrain(1);
        REQUIRE(pool.activeCount() == 2);

        pool.releaseGrain(grain1);
        REQUIRE(pool.activeCount() == 1);

        pool.releaseGrain(grain2);
        REQUIRE(pool.activeCount() == 0);
    }

    SECTION("released grain is marked inactive") {
        Grain* grain = pool.acquireGrain(0);
        REQUIRE(grain->active == true);

        pool.releaseGrain(grain);
        REQUIRE(grain->active == false);
    }

    SECTION("released grain can be reacquired") {
        Grain* grain1 = pool.acquireGrain(0);
        pool.releaseGrain(grain1);

        // After release, next acquire should get a grain
        Grain* grain2 = pool.acquireGrain(1);
        REQUIRE(grain2 != nullptr);
        REQUIRE(pool.activeCount() == 1);
    }

    SECTION("releaseGrain handles nullptr gracefully") {
        // Should not crash
        pool.releaseGrain(nullptr);
        REQUIRE(pool.activeCount() == 0);
    }

    SECTION("double release does not underflow count") {
        Grain* grain = pool.acquireGrain(0);
        pool.releaseGrain(grain);
        pool.releaseGrain(grain);  // Double release

        REQUIRE(pool.activeCount() == 0);  // Should still be 0, not underflow
    }
}

// =============================================================================
// Voice Stealing Tests (FR-005)
// =============================================================================

TEST_CASE("GrainPool voice stealing when exhausted", "[primitives][grain][layer1][FR-005]") {
    GrainPool pool;
    pool.prepare(44100.0);

    SECTION("can acquire up to 64 grains") {
        for (size_t i = 0; i < 64; ++i) {
            Grain* grain = pool.acquireGrain(i);
            REQUIRE(grain != nullptr);
            REQUIRE(grain->active == true);
        }
        REQUIRE(pool.activeCount() == 64);
    }

    SECTION("acquiring 65th grain steals oldest") {
        // Fill pool completely
        std::array<Grain*, 64> grains{};
        for (size_t i = 0; i < 64; ++i) {
            grains[i] = pool.acquireGrain(i);
        }

        // First grain (oldest) should be at sample 0
        REQUIRE(grains[0]->startSample == 0);

        // Now acquire one more - should steal oldest
        Grain* stolen = pool.acquireGrain(100);
        REQUIRE(stolen != nullptr);

        // Active count should still be 64 (stole, didn't add)
        REQUIRE(pool.activeCount() == 64);

        // The stolen grain should have the new start sample
        REQUIRE(stolen->startSample == 100);
    }

    SECTION("voice stealing picks oldest grain") {
        // Acquire 64 grains with different start times
        for (size_t i = 0; i < 64; ++i) {
            (void)pool.acquireGrain(i * 100);  // 0, 100, 200, ...
        }

        // Request one more at sample 10000
        Grain* stolen = pool.acquireGrain(10000);
        REQUIRE(stolen != nullptr);

        // Should have stolen the grain from sample 0 (oldest)
        // and reassigned it to sample 10000
        REQUIRE(stolen->startSample == 10000);
    }
}

// =============================================================================
// activeGrains() Tests
// =============================================================================

TEST_CASE("GrainPool activeGrains returns active grains", "[primitives][grain][layer1]") {
    GrainPool pool;
    pool.prepare(44100.0);

    SECTION("returns empty span when no grains active") {
        auto grains = pool.activeGrains();
        REQUIRE(grains.empty());
    }

    SECTION("returns correct number of active grains") {
        (void)pool.acquireGrain(0);
        (void)pool.acquireGrain(1);
        (void)pool.acquireGrain(2);

        auto grains = pool.activeGrains();
        REQUIRE(grains.size() == 3);
    }

    SECTION("all returned grains are active") {
        (void)pool.acquireGrain(0);
        (void)pool.acquireGrain(1);
        (void)pool.acquireGrain(2);

        for (Grain* grain : pool.activeGrains()) {
            REQUIRE(grain->active == true);
        }
    }

    SECTION("released grains are not in active list") {
        Grain* g1 = pool.acquireGrain(0);
        (void)pool.acquireGrain(1);
        (void)pool.acquireGrain(2);

        pool.releaseGrain(g1);

        auto grains = pool.activeGrains();
        REQUIRE(grains.size() == 2);

        // g1 should not be in the list
        for (Grain* grain : grains) {
            REQUIRE(grain != g1);
        }
    }
}

// =============================================================================
// Max Grains Constraint Test (SC-008)
// =============================================================================

TEST_CASE("GrainPool max 64 grains constraint (SC-008)", "[primitives][grain][layer1][SC-008]") {
    GrainPool pool;
    pool.prepare(44100.0);

    // Acquire all 64 grains
    for (size_t i = 0; i < 64; ++i) {
        Grain* grain = pool.acquireGrain(i);
        REQUIRE(grain != nullptr);
    }

    // Pool should be at capacity
    REQUIRE(pool.activeCount() == 64);

    // Acquire more - voice stealing should keep count at 64
    for (size_t i = 0; i < 100; ++i) {
        Grain* grain = pool.acquireGrain(64 + i);
        REQUIRE(grain != nullptr);
        REQUIRE(pool.activeCount() == 64);  // Never exceeds 64
    }

    // activeGrains should return exactly 64
    auto grains = pool.activeGrains();
    REQUIRE(grains.size() == 64);
}

// =============================================================================
// Stress Test
// =============================================================================

TEST_CASE("GrainPool stress test - rapid acquire/release", "[primitives][grain][layer1]") {
    GrainPool pool;
    pool.prepare(44100.0);

    // Rapidly acquire and release grains
    for (size_t cycle = 0; cycle < 100; ++cycle) {
        // Acquire 32 grains
        std::array<Grain*, 32> grains{};
        for (size_t i = 0; i < 32; ++i) {
            grains[i] = pool.acquireGrain(cycle * 32 + i);
            REQUIRE(grains[i] != nullptr);
        }

        // Release half of them
        for (size_t i = 0; i < 16; ++i) {
            pool.releaseGrain(grains[i]);
        }

        // Count should be correct
        REQUIRE(pool.activeCount() == 16);

        // Release the rest
        for (size_t i = 16; i < 32; ++i) {
            pool.releaseGrain(grains[i]);
        }

        REQUIRE(pool.activeCount() == 0);
    }
}
