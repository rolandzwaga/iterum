// ==============================================================================
// Layer 1: DSP Primitive Tests - Spectral Buffer
// ==============================================================================
// Test-First Development (Constitution Principle XII)
// Tests written before implementation.
//
// Tests for: src/dsp/primitives/spectral_buffer.h
// Contract: specs/007-fft-processor/contracts/fft_processor.h
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "dsp/primitives/spectral_buffer.h"

#include <array>
#include <cmath>
#include <numbers>

using namespace Iterum::DSP;
using Catch::Approx;

// ==============================================================================
// Test Constants
// ==============================================================================

constexpr float kPi = 3.14159265358979323846f;

// ==============================================================================
// SpectralBuffer::prepare() Tests (T051)
// ==============================================================================

TEST_CASE("SpectralBuffer prepare allocates correct size", "[spectral][prepare][US5]") {
    SpectralBuffer buffer;

    SECTION("prepare with FFT size 1024") {
        buffer.prepare(1024);
        REQUIRE(buffer.isPrepared());
        REQUIRE(buffer.numBins() == 513);  // N/2+1
    }

    SECTION("prepare with FFT size 512") {
        buffer.prepare(512);
        REQUIRE(buffer.isPrepared());
        REQUIRE(buffer.numBins() == 257);
    }

    SECTION("prepare with FFT size 2048") {
        buffer.prepare(2048);
        REQUIRE(buffer.isPrepared());
        REQUIRE(buffer.numBins() == 1025);
    }
}

// ==============================================================================
// getMagnitude()/setMagnitude() Tests (T052)
// ==============================================================================

TEST_CASE("SpectralBuffer magnitude access", "[spectral][magnitude][US5]") {
    SpectralBuffer buffer;
    buffer.prepare(1024);

    SECTION("getMagnitude of zero bin is 0") {
        REQUIRE(buffer.getMagnitude(0) == Approx(0.0f));
    }

    SECTION("setMagnitude preserves phase") {
        // Set initial value with known phase
        buffer.setCartesian(10, 3.0f, 4.0f);  // magnitude=5, phase=atan2(4,3)
        float originalPhase = buffer.getPhase(10);

        buffer.setMagnitude(10, 10.0f);

        REQUIRE(buffer.getMagnitude(10) == Approx(10.0f).margin(0.01f));
        REQUIRE(buffer.getPhase(10) == Approx(originalPhase).margin(0.01f));
    }

    SECTION("setMagnitude with zero phase") {
        buffer.setCartesian(5, 1.0f, 0.0f);  // phase = 0
        buffer.setMagnitude(5, 5.0f);

        REQUIRE(buffer.getReal(5) == Approx(5.0f).margin(0.01f));
        REQUIRE(buffer.getImag(5) == Approx(0.0f).margin(0.01f));
    }
}

// ==============================================================================
// getPhase()/setPhase() Tests (T053)
// ==============================================================================

TEST_CASE("SpectralBuffer phase access", "[spectral][phase][US5]") {
    SpectralBuffer buffer;
    buffer.prepare(1024);

    SECTION("getPhase of pure real is 0") {
        buffer.setCartesian(10, 5.0f, 0.0f);
        REQUIRE(buffer.getPhase(10) == Approx(0.0f).margin(0.01f));
    }

    SECTION("getPhase of pure imaginary is pi/2") {
        buffer.setCartesian(10, 0.0f, 5.0f);
        REQUIRE(buffer.getPhase(10) == Approx(kPi / 2.0f).margin(0.01f));
    }

    SECTION("setPhase preserves magnitude") {
        buffer.setCartesian(10, 3.0f, 4.0f);  // magnitude = 5
        float originalMag = buffer.getMagnitude(10);

        buffer.setPhase(10, 0.0f);  // Set phase to 0

        REQUIRE(buffer.getMagnitude(10) == Approx(originalMag).margin(0.01f));
        REQUIRE(buffer.getReal(10) == Approx(5.0f).margin(0.01f));
        REQUIRE(buffer.getImag(10) == Approx(0.0f).margin(0.01f));
    }

    SECTION("setPhase to pi/2") {
        buffer.setCartesian(10, 5.0f, 0.0f);  // magnitude = 5, phase = 0
        buffer.setPhase(10, kPi / 2.0f);

        REQUIRE(buffer.getReal(10) == Approx(0.0f).margin(0.01f));
        REQUIRE(buffer.getImag(10) == Approx(5.0f).margin(0.01f));
    }
}

// ==============================================================================
// Cartesian Access Tests (T054)
// ==============================================================================

TEST_CASE("SpectralBuffer Cartesian access", "[spectral][cartesian][US5]") {
    SpectralBuffer buffer;
    buffer.prepare(1024);

    SECTION("setCartesian and getReal/getImag") {
        buffer.setCartesian(50, 3.5f, -2.5f);

        REQUIRE(buffer.getReal(50) == Approx(3.5f));
        REQUIRE(buffer.getImag(50) == Approx(-2.5f));
    }

    SECTION("out of bounds access returns 0") {
        REQUIRE(buffer.getReal(1000) == Approx(0.0f));
        REQUIRE(buffer.getImag(1000) == Approx(0.0f));
        REQUIRE(buffer.getMagnitude(1000) == Approx(0.0f));
    }
}

// ==============================================================================
// reset() Tests (T055)
// ==============================================================================

TEST_CASE("SpectralBuffer reset clears all bins", "[spectral][reset][US5]") {
    SpectralBuffer buffer;
    buffer.prepare(1024);

    // Set some values
    buffer.setCartesian(0, 1.0f, 0.0f);
    buffer.setCartesian(100, 5.0f, 3.0f);
    buffer.setCartesian(512, 2.0f, 1.0f);

    buffer.reset();

    SECTION("all bins are zero after reset") {
        for (size_t i = 0; i < buffer.numBins(); ++i) {
            REQUIRE(buffer.getReal(i) == Approx(0.0f));
            REQUIRE(buffer.getImag(i) == Approx(0.0f));
        }
    }
}

// ==============================================================================
// Cartesian↔Polar Round-Trip Tests (T056)
// ==============================================================================

TEST_CASE("SpectralBuffer Cartesian-Polar round-trip", "[spectral][roundtrip][US5]") {
    SpectralBuffer buffer;
    buffer.prepare(1024);

    SECTION("set Cartesian, read Polar, set Polar, verify Cartesian") {
        const float origReal = 3.0f;
        const float origImag = 4.0f;

        buffer.setCartesian(10, origReal, origImag);

        float mag = buffer.getMagnitude(10);
        float phase = buffer.getPhase(10);

        // Clear and set via polar
        buffer.setCartesian(10, 0.0f, 0.0f);
        buffer.setMagnitude(10, mag);
        buffer.setPhase(10, phase);

        REQUIRE(buffer.getReal(10) == Approx(origReal).margin(0.01f));
        REQUIRE(buffer.getImag(10) == Approx(origImag).margin(0.01f));
    }

    SECTION("multiple conversions preserve accuracy") {
        buffer.setCartesian(20, -5.0f, 12.0f);  // magnitude = 13

        for (int i = 0; i < 10; ++i) {
            float mag = buffer.getMagnitude(20);
            float phase = buffer.getPhase(20);
            buffer.setMagnitude(20, mag);
            buffer.setPhase(20, phase);
        }

        REQUIRE(buffer.getMagnitude(20) == Approx(13.0f).margin(0.01f));
    }
}

// ==============================================================================
// Real-Time Safety Tests (T097)
// ==============================================================================

TEST_CASE("SpectralBuffer accessors are noexcept", "[spectral][realtime][US6]") {
    SpectralBuffer buffer;

    SECTION("getMagnitude is noexcept") {
        static_assert(noexcept(buffer.getMagnitude(0)));
    }

    SECTION("getPhase is noexcept") {
        static_assert(noexcept(buffer.getPhase(0)));
    }

    SECTION("setMagnitude is noexcept") {
        static_assert(noexcept(buffer.setMagnitude(0, 1.0f)));
    }

    SECTION("setPhase is noexcept") {
        static_assert(noexcept(buffer.setPhase(0, 0.0f)));
    }

    SECTION("reset is noexcept") {
        static_assert(noexcept(buffer.reset()));
    }
}
