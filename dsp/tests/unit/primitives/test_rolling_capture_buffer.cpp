// ==============================================================================
// Layer 1: Primitive Tests - Rolling Capture Buffer
// ==============================================================================
// Unit tests for RollingCaptureBuffer (spec 069 - Pattern Freeze Mode).
//
// Tests verify:
// - Continuous circular recording
// - Slice extraction at specified positions
// - Buffer ready state detection
// - Edge cases (wrap-around, boundary conditions)
//
// Constitution Compliance:
// - Principle VIII: Testing Discipline
// - Principle XII: Test-first development methodology
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <krate/dsp/primitives/rolling_capture_buffer.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <numeric>
#include <vector>

using namespace Krate::DSP;
using Catch::Approx;

// =============================================================================
// Lifecycle Tests
// =============================================================================

TEST_CASE("RollingCaptureBuffer prepares with correct capacity", "[primitives][capture_buffer][layer1]") {
    RollingCaptureBuffer buffer;

    // 1 second at 44100 Hz
    buffer.prepare(44100.0, 1.0f);

    REQUIRE(buffer.getCapacitySamples() >= 44100);
    REQUIRE(buffer.getSampleRate() == Approx(44100.0));
}

TEST_CASE("RollingCaptureBuffer reset clears content", "[primitives][capture_buffer][layer1]") {
    RollingCaptureBuffer buffer;
    buffer.prepare(44100.0, 1.0f);

    // Write some data
    for (int i = 0; i < 1000; ++i) {
        buffer.writeStereo(0.5f, -0.5f);
    }

    // Reset
    buffer.reset();

    // Buffer should not be ready after reset
    REQUIRE(buffer.isReady(100.0f) == false);
}

// =============================================================================
// Write and Read Tests
// =============================================================================

TEST_CASE("RollingCaptureBuffer records stereo samples", "[primitives][capture_buffer][layer1]") {
    RollingCaptureBuffer buffer;
    buffer.prepare(44100.0, 1.0f);

    // Write sequential values
    for (int i = 0; i < 100; ++i) {
        buffer.writeStereo(static_cast<float>(i) * 0.01f,
                          static_cast<float>(i) * -0.01f);
    }

    // Extract recent slice
    std::vector<float> sliceL(50);
    std::vector<float> sliceR(50);

    // Extract last 50 samples
    buffer.extractSlice(sliceL.data(), sliceR.data(), 50, 0);

    // Last sample written was 99 * 0.01 = 0.99
    // At offset 0, we should get the most recent samples (49 back to 0 from write head)
    REQUIRE(sliceL[49] == Approx(0.99f).margin(0.001f));
}

TEST_CASE("RollingCaptureBuffer wraps around correctly", "[primitives][capture_buffer][layer1]") {
    RollingCaptureBuffer buffer;
    buffer.prepare(44100.0, 0.1f);  // 100ms = 4410 samples

    const size_t capacity = buffer.getCapacitySamples();

    // Write more than capacity to force wraparound
    for (size_t i = 0; i < capacity + 1000; ++i) {
        buffer.writeStereo(1.0f, -1.0f);
    }

    // Should still be able to read valid data
    std::vector<float> sliceL(100);
    std::vector<float> sliceR(100);

    buffer.extractSlice(sliceL.data(), sliceR.data(), 100, 0);

    // All samples should be 1.0 and -1.0
    for (size_t i = 0; i < 100; ++i) {
        REQUIRE(sliceL[i] == Approx(1.0f));
        REQUIRE(sliceR[i] == Approx(-1.0f));
    }
}

TEST_CASE("RollingCaptureBuffer extractSlice with offset", "[primitives][capture_buffer][layer1]") {
    RollingCaptureBuffer buffer;
    buffer.prepare(44100.0, 1.0f);

    // Write ramp signal
    for (int i = 0; i < 1000; ++i) {
        float value = static_cast<float>(i);
        buffer.writeStereo(value, -value);
    }

    // Extract with offset into the past
    std::vector<float> sliceL(100);
    std::vector<float> sliceR(100);

    // Offset 500 means start 500 samples before current write position
    buffer.extractSlice(sliceL.data(), sliceR.data(), 100, 500);

    // At offset 500, first sample should be around (1000 - 500 - 100) = 400
    // This is complex due to circular buffer, but the slice should be contiguous
    // and values should be monotonically increasing within the slice
    for (size_t i = 1; i < 100; ++i) {
        REQUIRE(sliceL[i] > sliceL[i - 1]);  // Increasing
        REQUIRE(sliceR[i] < sliceR[i - 1]);  // Decreasing (negative)
    }
}

// =============================================================================
// Ready State Tests
// =============================================================================

TEST_CASE("RollingCaptureBuffer isReady detects sufficient data", "[primitives][capture_buffer][layer1]") {
    RollingCaptureBuffer buffer;
    buffer.prepare(44100.0, 1.0f);

    // Initially not ready
    REQUIRE(buffer.isReady(100.0f) == false);

    // Write exactly the required amount (100ms at 44100Hz = 4410 samples)
    const size_t requiredSamples = static_cast<size_t>(44100.0 * 0.1);
    for (size_t i = 0; i < requiredSamples; ++i) {
        buffer.writeStereo(0.5f, 0.5f);
    }

    // Now should be ready for 100ms
    REQUIRE(buffer.isReady(100.0f) == true);

    // But not ready for more than written
    REQUIRE(buffer.isReady(200.0f) == false);
}

TEST_CASE("RollingCaptureBuffer isReady with full buffer", "[primitives][capture_buffer][layer1]") {
    RollingCaptureBuffer buffer;
    buffer.prepare(44100.0, 0.5f);  // 500ms buffer

    // Fill entire buffer
    const size_t capacity = buffer.getCapacitySamples();
    for (size_t i = 0; i < capacity; ++i) {
        buffer.writeStereo(0.5f, 0.5f);
    }

    // Should be ready for any time up to buffer duration
    REQUIRE(buffer.isReady(100.0f) == true);
    REQUIRE(buffer.isReady(250.0f) == true);
    REQUIRE(buffer.isReady(500.0f) == true);
}

TEST_CASE("RollingCaptureBuffer getSamplesWritten tracks correctly", "[primitives][capture_buffer][layer1]") {
    RollingCaptureBuffer buffer;
    buffer.prepare(44100.0, 1.0f);

    REQUIRE(buffer.getSamplesWritten() == 0);

    for (int i = 0; i < 500; ++i) {
        buffer.writeStereo(0.0f, 0.0f);
    }

    REQUIRE(buffer.getSamplesWritten() == 500);
}

// =============================================================================
// Edge Cases
// =============================================================================

TEST_CASE("RollingCaptureBuffer handles zero-length extraction", "[primitives][capture_buffer][layer1][edge]") {
    RollingCaptureBuffer buffer;
    buffer.prepare(44100.0, 1.0f);

    // Write some data
    for (int i = 0; i < 100; ++i) {
        buffer.writeStereo(0.5f, 0.5f);
    }

    // Zero-length extraction should not crash
    float dummyL = 0.0f, dummyR = 0.0f;
    buffer.extractSlice(&dummyL, &dummyR, 0, 0);

    // No assertion needed - just verify no crash
    REQUIRE(true);
}

TEST_CASE("RollingCaptureBuffer clamps extraction length", "[primitives][capture_buffer][layer1][edge]") {
    RollingCaptureBuffer buffer;
    buffer.prepare(44100.0, 0.1f);  // 100ms buffer

    // Fill buffer
    const size_t capacity = buffer.getCapacitySamples();
    for (size_t i = 0; i < capacity; ++i) {
        buffer.writeStereo(1.0f, 1.0f);
    }

    // Try to extract more than capacity
    std::vector<float> sliceL(capacity * 2);
    std::vector<float> sliceR(capacity * 2);

    // Should not crash, extracts up to available data
    buffer.extractSlice(sliceL.data(), sliceR.data(), capacity * 2, 0);

    // At least capacity samples should be valid
    size_t validCount = 0;
    for (size_t i = 0; i < capacity; ++i) {
        if (sliceL[i] == Approx(1.0f)) ++validCount;
    }
    REQUIRE(validCount == capacity);
}

TEST_CASE("RollingCaptureBuffer handles offset beyond buffer", "[primitives][capture_buffer][layer1][edge]") {
    RollingCaptureBuffer buffer;
    buffer.prepare(44100.0, 0.1f);

    // Write some data
    for (int i = 0; i < 1000; ++i) {
        buffer.writeStereo(0.5f, 0.5f);
    }

    // Offset beyond written data - should wrap or clamp
    std::vector<float> sliceL(100);
    std::vector<float> sliceR(100);

    // Large offset - should still extract something valid (wraps in circular buffer)
    buffer.extractSlice(sliceL.data(), sliceR.data(), 100, 10000);

    // Should not crash, values should be defined
    // After wraparound, we might get either valid data or zeros
    REQUIRE(true);  // Just verify no crash
}

// =============================================================================
// Real-Time Safety Tests
// =============================================================================

TEST_CASE("RollingCaptureBuffer writeStereo is noexcept", "[primitives][capture_buffer][layer1][realtime]") {
    // Compile-time check
    RollingCaptureBuffer buffer;
    static_assert(noexcept(buffer.writeStereo(0.0f, 0.0f)),
                  "writeStereo() must be noexcept");
}

TEST_CASE("RollingCaptureBuffer extractSlice is noexcept", "[primitives][capture_buffer][layer1][realtime]") {
    RollingCaptureBuffer buffer;
    float L, R;
    static_assert(noexcept(buffer.extractSlice(&L, &R, 1, 0)),
                  "extractSlice() must be noexcept");
}

// =============================================================================
// Multi-Slice Extraction Tests (for Pattern Mode)
// =============================================================================

TEST_CASE("RollingCaptureBuffer supports multiple slice extractions", "[primitives][capture_buffer][layer1]") {
    RollingCaptureBuffer buffer;
    buffer.prepare(44100.0, 1.0f);

    // Write unique values for each sample
    for (int i = 0; i < 10000; ++i) {
        buffer.writeStereo(static_cast<float>(i), 0.0f);
    }

    // Extract multiple non-overlapping slices
    std::vector<float> slice1L(100), slice1R(100);
    std::vector<float> slice2L(100), slice2R(100);
    std::vector<float> slice3L(100), slice3R(100);

    buffer.extractSlice(slice1L.data(), slice1R.data(), 100, 0);
    buffer.extractSlice(slice2L.data(), slice2R.data(), 100, 200);
    buffer.extractSlice(slice3L.data(), slice3R.data(), 100, 400);

    // Verify each slice has internally consistent values (monotonically increasing)
    for (size_t i = 1; i < 100; ++i) {
        REQUIRE(slice1L[i] > slice1L[i - 1]);
        REQUIRE(slice2L[i] > slice2L[i - 1]);
        REQUIRE(slice3L[i] > slice3L[i - 1]);
    }

    // Verify slices are from different time periods
    // slice3 (offset 400) should have older values than slice1 (offset 0)
    REQUIRE(slice3L[0] < slice1L[0]);
}

// =============================================================================
// Available Samples Query Test
// =============================================================================

TEST_CASE("RollingCaptureBuffer getAvailableSamples", "[primitives][capture_buffer][layer1]") {
    RollingCaptureBuffer buffer;
    buffer.prepare(44100.0, 0.5f);  // 500ms

    REQUIRE(buffer.getAvailableSamples() == 0);

    for (int i = 0; i < 1000; ++i) {
        buffer.writeStereo(0.0f, 0.0f);
    }

    REQUIRE(buffer.getAvailableSamples() == 1000);

    // Writing more than capacity caps at capacity
    const size_t capacity = buffer.getCapacitySamples();
    for (size_t i = 0; i < capacity * 2; ++i) {
        buffer.writeStereo(0.0f, 0.0f);
    }

    REQUIRE(buffer.getAvailableSamples() == capacity);
}

// =============================================================================
// SC-012 - readStereoLinear (specs/seraphis-phase5-atmosphere, RA-1 / FR-080..084)
// =============================================================================
// Pins BOTH anchoring conventions rather than assuming they agree:
//   extractSlice anchors from the END of the slice (rolling_capture_buffer.h
//   :161-162), so out[i] has age offsetSamples + lengthSamples - 1 - i. The two
//   agree on "same offset" ONLY at lengthSamples == 1.
//
// Sub-case 6 (non-finite ages) is deliberately duplicated in
// dsp/tests/unit/systems/atmosphere_engine_test.cpp, which is NOT in the
// -fno-fast-math list: a guard that only works under -fno-fast-math is a guard
// that never works in a shipped build.

namespace {

/// Build a non-finite float from its bit pattern through a volatile sink.
/// std::numeric_limits<float>::quiet_NaN() / infinity() fold to finite garbage
/// under -ffast-math (the macOS leg), so they are never used in this repo's
/// tests. 0x7FC00000 = quiet NaN, 0x7F800000 = +Inf, 0xFF800000 = -Inf.
[[nodiscard]] float makeNonFinite(std::uint32_t bits) noexcept {
    volatile std::uint32_t b = bits;  // defeats constant folding
    const std::uint32_t materialized = b;  // the volatile READ is the sink
    float f = 0.0f;
    std::memcpy(&f, &materialized, sizeof(f));
    return f;
}

/// prepare(48000, 1.0) -> capacity 65536, then write a ramp whose every sample
/// is distinguishable (step 1e-4, far above the float ULP at these magnitudes).
void prepareAndFillRamp(RollingCaptureBuffer& buffer, size_t numSamples) {
    buffer.prepare(48000.0, 1.0f);
    for (size_t i = 0; i < numSamples; ++i) {
        const float value = static_cast<float>(i) * 1e-4f;
        buffer.writeStereo(value, -value);
    }
}

}  // namespace

TEST_CASE("RollingCaptureBuffer_ReadStereoLinear", "[rolling_capture_buffer]") {
    SECTION("Length 1 slice matches readStereoLinear at the same offset") {
        RollingCaptureBuffer buffer;
        prepareAndFillRamp(buffer, 1000);
        REQUIRE(buffer.getCapacitySamples() == 65536);

        constexpr float kAge = 100.0f;

        float l = 0.0f;
        float r = 0.0f;
        buffer.readStereoLinear(kAge, l, r);

        float sliceL = 0.0f;
        float sliceR = 0.0f;
        buffer.extractSlice(&sliceL, &sliceR, /*lengthSamples=*/1,
                            /*offsetSamples=*/100);

        // Integer age -> frac == 0 -> no interpolation, so this is exact.
        REQUIRE(l == sliceL);
        REQUIRE(r == sliceR);
    }

    SECTION("Length > 1 slice follows the end-anchored identity") {
        RollingCaptureBuffer buffer;
        prepareAndFillRamp(buffer, 1000);

        constexpr size_t kLength = 8;
        constexpr size_t kOffset = 5;

        std::vector<float> outL(kLength, 0.0f);
        std::vector<float> outR(kLength, 0.0f);
        buffer.extractSlice(outL.data(), outR.data(), kLength, kOffset);

        // extractSlice(outL, outR, L, O)[i] == readStereoLinear(O + L - 1 - i)
        // i = 0 -> age 12 (the OLDEST sample of the slice, not age O)
        float firstL = 0.0f;
        float firstR = 0.0f;
        buffer.readStereoLinear(
            static_cast<float>(kOffset + kLength - 1 - 0), firstL, firstR);
        REQUIRE(outL[0] == firstL);
        REQUIRE(outR[0] == firstR);

        // i = L - 1 -> age 5 == O (the only index where "same offset" holds)
        float lastL = 0.0f;
        float lastR = 0.0f;
        buffer.readStereoLinear(
            static_cast<float>(kOffset + kLength - 1 - (kLength - 1)), lastL,
            lastR);
        REQUIRE(outL[kLength - 1] == lastL);
        REQUIRE(outR[kLength - 1] == lastR);
    }

    SECTION("Fractional age interpolates between the two neighbours") {
        RollingCaptureBuffer buffer;
        prepareAndFillRamp(buffer, 1000);

        float l10 = 0.0f;
        float r10 = 0.0f;
        buffer.readStereoLinear(10.0f, l10, r10);

        float l11 = 0.0f;
        float r11 = 0.0f;
        buffer.readStereoLinear(11.0f, l11, r11);

        float lHalf = 0.0f;
        float rHalf = 0.0f;
        buffer.readStereoLinear(10.5f, lHalf, rHalf);

        REQUIRE(lHalf == Approx(0.5f * (l10 + l11)).margin(1e-6));
        REQUIRE(rHalf == Approx(0.5f * (r10 + r11)).margin(1e-6));
    }

    SECTION("Degenerate buffers yield (0,0) and read nothing out of bounds") {
        // (a) default-constructed, never prepared: capacity_ == mask_ == 0 and
        //     both vectors are EMPTY, so any `& mask_` index would be an
        //     out-of-bounds read (visible under ASan/valgrind).
        {
            RollingCaptureBuffer buffer;
            float l = 1.0f;
            float r = 1.0f;
            buffer.readStereoLinear(0.0f, l, r);
            REQUIRE(l == 0.0f);
            REQUIRE(r == 0.0f);

            l = 1.0f;
            r = 1.0f;
            buffer.readStereoLinear(1.0f, l, r);
            REQUIRE(l == 0.0f);
            REQUIRE(r == 0.0f);
        }

        // (b) prepared but empty: getAvailableSamples() == 0, so a bare
        //     `available - 2` would wrap to ~2^64 and the clamp would become a
        //     no-op in exactly the case it exists for.
        {
            RollingCaptureBuffer buffer;
            buffer.prepare(48000.0, 1.0f);
            REQUIRE(buffer.getAvailableSamples() == 0);

            float l = 1.0f;
            float r = 1.0f;
            buffer.readStereoLinear(0.0f, l, r);
            REQUIRE(l == 0.0f);
            REQUIRE(r == 0.0f);

            l = 1.0f;
            r = 1.0f;
            buffer.readStereoLinear(1.0f, l, r);
            REQUIRE(l == 0.0f);
            REQUIRE(r == 0.0f);
        }

        // (c) exactly one sample written: still < 2, so still (0,0).
        {
            RollingCaptureBuffer buffer;
            buffer.prepare(48000.0, 1.0f);
            buffer.writeStereo(0.75f, -0.75f);
            REQUIRE(buffer.getAvailableSamples() == 1);

            float l = 1.0f;
            float r = 1.0f;
            buffer.readStereoLinear(0.0f, l, r);
            REQUIRE(l == 0.0f);
            REQUIRE(r == 0.0f);

            l = 1.0f;
            r = 1.0f;
            buffer.readStereoLinear(1.0f, l, r);
            REQUIRE(l == 0.0f);
            REQUIRE(r == 0.0f);
        }
    }

    SECTION("Wraparound keeps every legal age on the correct side of the head") {
        RollingCaptureBuffer buffer;
        buffer.prepare(48000.0, 1.0f);
        const size_t capacity = buffer.getCapacitySamples();
        REQUIRE(capacity == 65536);

        const size_t totalWritten = 2 * capacity;
        for (size_t i = 0; i < totalWritten; ++i) {
            const float value = static_cast<float>(i) * 1e-4f;
            buffer.writeStereo(value, -value);
        }
        REQUIRE(buffer.getAvailableSamples() == capacity);

        // Age a addresses the sample written at ordinal (totalWritten - 1 - a).
        // The largest legal age is capacity - 2 (the interpolation needs the
        // neighbour at floor(age) + 1 to stay on the live side of the head).
        const size_t maxAge = capacity - 2;

        float oldestL = 0.0f;
        float oldestR = 0.0f;
        buffer.readStereoLinear(static_cast<float>(maxAge), oldestL, oldestR);

        const float expectedOldest =
            static_cast<float>(totalWritten - 1 - maxAge) * 1e-4f;
        REQUIRE(oldestL == Approx(expectedOldest).margin(1e-5));
        REQUIRE(oldestR == Approx(-expectedOldest).margin(1e-5));

        // Sweep every legal age: the ramp must be reproduced exactly and must
        // decrease strictly as age grows. A sample from the wrong side of the
        // write head shows up as either a value mismatch or a monotonicity
        // break (the ramp jumps by ~6.55 across the head).
        size_t valueMismatches = 0;
        size_t monotonicityBreaks = 0;
        float previousL = 0.0f;
        for (size_t age = 0; age <= maxAge; ++age) {
            float l = 0.0f;
            float r = 0.0f;
            buffer.readStereoLinear(static_cast<float>(age), l, r);

            const float expected =
                static_cast<float>(totalWritten - 1 - age) * 1e-4f;
            if (std::fabs(l - expected) > 1e-5f) {
                ++valueMismatches;
            }
            if (std::fabs(r + expected) > 1e-5f) {
                ++valueMismatches;
            }
            if (age > 0 && !(l < previousL)) {
                ++monotonicityBreaks;
            }
            previousL = l;
        }
        REQUIRE(valueMismatches == 0);
        REQUIRE(monotonicityBreaks == 0);
    }

    SECTION("Non-finite ages land on the clamp bounds, not on (0,0)") {
        RollingCaptureBuffer buffer;
        prepareAndFillRamp(buffer, 1000);

        const float nanAge = makeNonFinite(0x7FC00000u);
        const float posInfAge = makeNonFinite(0x7F800000u);
        const float negInfAge = makeNonFinite(0xFF800000u);

        // Age 0 == the most recent sample.
        float youngestL = 0.0f;
        float youngestR = 0.0f;
        buffer.readStereoLinear(0.0f, youngestL, youngestR);
        REQUIRE(youngestL != 0.0f);  // guard against a vacuous comparison

        // The largest legal finite age.
        const float maxAge =
            static_cast<float>(buffer.getAvailableSamples() - 2);
        float oldestL = 0.0f;
        float oldestR = 0.0f;
        buffer.readStereoLinear(maxAge, oldestL, oldestR);

        // NaN: `!(age >= 0)` is taken because an unordered compare is false.
        float l = 0.0f;
        float r = 0.0f;
        buffer.readStereoLinear(nanAge, l, r);
        REQUIRE(l == youngestL);
        REQUIRE(r == youngestR);

        // -Inf: taken by the same first comparison.
        l = 0.0f;
        r = 0.0f;
        buffer.readStereoLinear(negInfAge, l, r);
        REQUIRE(l == youngestL);
        REQUIRE(r == youngestR);

        // +Inf: taken by the second comparison, `age > maxAge`.
        l = 0.0f;
        r = 0.0f;
        buffer.readStereoLinear(posInfAge, l, r);
        REQUIRE(l == oldestL);
        REQUIRE(r == oldestR);
    }
}
