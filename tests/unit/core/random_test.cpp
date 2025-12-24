// Tests for Xorshift32 PRNG
// Layer 0: Core Utilities
// Feature: 013-noise-generator

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <cmath>
#include <set>
#include <array>

#include "dsp/core/random.h"

using Catch::Approx;
using namespace Iterum::DSP;

TEST_CASE("Xorshift32 next() produces non-zero values", "[random][US1]") {
    Xorshift32 rng(12345);

    // Generate several values - none should be zero with a good seed
    for (int i = 0; i < 1000; ++i) {
        uint32_t value = rng.next();
        // Note: xorshift32 can produce 0 in its sequence, but rarely
        // This test just ensures it's generating values
        CHECK(value != 0);
    }
}

TEST_CASE("Xorshift32 different seeds produce different sequences", "[random][US1]") {
    Xorshift32 rng1(12345);
    Xorshift32 rng2(54321);

    // Generate first 100 values from each
    std::array<uint32_t, 100> seq1, seq2;
    for (int i = 0; i < 100; ++i) {
        seq1[i] = rng1.next();
        seq2[i] = rng2.next();
    }

    // Sequences should be different
    bool allSame = true;
    for (int i = 0; i < 100; ++i) {
        if (seq1[i] != seq2[i]) {
            allSame = false;
            break;
        }
    }
    REQUIRE_FALSE(allSame);
}

TEST_CASE("Xorshift32 same seed produces same sequence", "[random][US1]") {
    Xorshift32 rng1(99999);
    Xorshift32 rng2(99999);

    for (int i = 0; i < 100; ++i) {
        REQUIRE(rng1.next() == rng2.next());
    }
}

TEST_CASE("Xorshift32 seed of 0 is handled safely", "[random][US1][edge]") {
    // Seed of 0 would cause xorshift to produce only zeros
    // Implementation should handle this by using a fallback seed
    Xorshift32 rng(0);

    bool hasNonZero = false;
    for (int i = 0; i < 100; ++i) {
        if (rng.next() != 0) {
            hasNonZero = true;
            break;
        }
    }
    REQUIRE(hasNonZero);
}

TEST_CASE("Xorshift32 nextFloat() returns values in [-1.0, 1.0] range", "[random][US1]") {
    Xorshift32 rng(42);

    float minVal = 1.0f;
    float maxVal = -1.0f;

    for (int i = 0; i < 10000; ++i) {
        float value = rng.nextFloat();
        REQUIRE(value >= -1.0f);
        REQUIRE(value <= 1.0f);

        minVal = std::min(minVal, value);
        maxVal = std::max(maxVal, value);
    }

    // Should cover a good portion of the range
    REQUIRE(minVal < -0.9f);
    REQUIRE(maxVal > 0.9f);
}

TEST_CASE("Xorshift32 nextUnipolar() returns values in [0.0, 1.0] range", "[random][US1]") {
    Xorshift32 rng(42);

    float minVal = 1.0f;
    float maxVal = 0.0f;

    for (int i = 0; i < 10000; ++i) {
        float value = rng.nextUnipolar();
        REQUIRE(value >= 0.0f);
        REQUIRE(value <= 1.0f);

        minVal = std::min(minVal, value);
        maxVal = std::max(maxVal, value);
    }

    // Should cover a good portion of the range
    REQUIRE(minVal < 0.1f);
    REQUIRE(maxVal > 0.9f);
}

TEST_CASE("Xorshift32 seed() method reseeds generator", "[random][US1]") {
    Xorshift32 rng(12345);

    // Generate some values
    for (int i = 0; i < 50; ++i) {
        (void)rng.next();
    }

    // Reseed with same seed
    rng.seed(12345);

    // Should produce same sequence as fresh generator
    Xorshift32 fresh(12345);
    for (int i = 0; i < 100; ++i) {
        REQUIRE(rng.next() == fresh.next());
    }
}

TEST_CASE("Xorshift32 period does not repeat quickly", "[random][US1]") {
    Xorshift32 rng(1);

    // Store first value
    uint32_t firstValue = rng.next();

    // Generate many values and check we don't see the first value too soon
    // Full period is 2^32-1, but we just check we don't repeat within 100000 values
    constexpr int checkCount = 100000;
    int repeatIndex = -1;

    for (int i = 1; i < checkCount; ++i) {
        if (rng.next() == firstValue) {
            // Could be coincidence - check next value too
            Xorshift32 check(1);
            (void)check.next(); // Skip first
            if (rng.next() == check.next()) {
                repeatIndex = i;
                break;
            }
        }
    }

    // Should not have found a repeat
    REQUIRE(repeatIndex == -1);
}

TEST_CASE("Xorshift32 distribution is approximately uniform", "[random][US1]") {
    Xorshift32 rng(12345);

    // Count values falling in 10 bins
    std::array<int, 10> bins{};
    constexpr int samples = 100000;

    for (int i = 0; i < samples; ++i) {
        float value = rng.nextUnipolar();
        int bin = static_cast<int>(value * 10);
        if (bin >= 10) bin = 9;  // Handle edge case of exactly 1.0
        bins[bin]++;
    }

    // Each bin should have roughly samples/10 = 10000 values
    // Allow 20% deviation for statistical variance
    int expected = samples / 10;
    int tolerance = expected / 5;  // 20%

    for (int i = 0; i < 10; ++i) {
        CHECK(bins[i] > expected - tolerance);
        CHECK(bins[i] < expected + tolerance);
    }
}
