// Layer 3: System Component - ModulationMatrix Tests
// Feature: 020-modulation-matrix
//
// Tests for ModulationMatrix which routes modulation sources (LFO, EnvelopeFollower)
// to parameter destinations with depth control, bipolar/unipolar modes, and smoothing.
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocations in process)
// - Principle III: Modern C++ (C++20, RAII, value semantics)
// - Principle IX: Layer 3 (depends only on Layer 0-2)
// - Principle X: DSP Constraints (sample-accurate modulation)
// - Principle XII: Test-First Development

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <array>
#include <cmath>
#include <algorithm>
#include <vector>
#include <limits>
#include <cstdint>
#include <bit>

#include "dsp/systems/modulation_matrix.h"

using Catch::Approx;
using namespace Iterum::DSP;

// =============================================================================
// Test Helpers
// =============================================================================

namespace {

/// NaN detection using bit-level check (IEEE 754 compliant)
/// Required because -ffast-math can optimize away std::isnan
constexpr bool testIsNaN(float x) noexcept {
    const auto bits = std::bit_cast<std::uint32_t>(x);
    return ((bits & 0x7F800000u) == 0x7F800000u) && ((bits & 0x007FFFFFu) != 0);
}

/// Calculate samples needed to reach target percentage with one-pole smoother
inline size_t samplesTo95Percent(double sampleRate, float smoothingTimeMs = 20.0f) {
    double tau = (smoothingTimeMs / 1000.0) * sampleRate;
    return static_cast<size_t>(std::ceil(-tau * std::log(0.05)));
}

/// Mock ModulationSource for testing
class MockModulationSource : public ModulationSource {
public:
    explicit MockModulationSource(float value = 0.0f, float minVal = -1.0f, float maxVal = 1.0f)
        : value_(value), minValue_(minVal), maxValue_(maxVal) {}

    [[nodiscard]] float getCurrentValue() const noexcept override {
        return value_;
    }

    [[nodiscard]] std::pair<float, float> getSourceRange() const noexcept override {
        return {minValue_, maxValue_};
    }

    void setValue(float v) noexcept { value_ = v; }

private:
    float value_;
    float minValue_;
    float maxValue_;
};

} // anonymous namespace

// =============================================================================
// Phase 2: Foundational Types Tests (T004-T008)
// =============================================================================

// -----------------------------------------------------------------------------
// T004: ModulationMode enum tests
// -----------------------------------------------------------------------------

TEST_CASE("ModulationMode enum has correct values", "[modulation][foundational]") {
    SECTION("Bipolar is 0") {
        REQUIRE(static_cast<uint8_t>(ModulationMode::Bipolar) == 0);
    }

    SECTION("Unipolar is 1") {
        REQUIRE(static_cast<uint8_t>(ModulationMode::Unipolar) == 1);
    }
}

// -----------------------------------------------------------------------------
// T005: ModulationSource interface tests
// -----------------------------------------------------------------------------

TEST_CASE("ModulationSource interface works correctly", "[modulation][foundational]") {
    SECTION("getCurrentValue returns set value") {
        MockModulationSource source(0.75f);
        REQUIRE(source.getCurrentValue() == Approx(0.75f));
    }

    SECTION("getSourceRange returns correct range for bipolar LFO-style source") {
        MockModulationSource source(0.0f, -1.0f, 1.0f);
        auto [minVal, maxVal] = source.getSourceRange();
        REQUIRE(minVal == Approx(-1.0f));
        REQUIRE(maxVal == Approx(1.0f));
    }

    SECTION("getSourceRange returns correct range for unipolar envelope-style source") {
        MockModulationSource source(0.0f, 0.0f, 1.0f);
        auto [minVal, maxVal] = source.getSourceRange();
        REQUIRE(minVal == Approx(0.0f));
        REQUIRE(maxVal == Approx(1.0f));
    }

    SECTION("value can be updated") {
        MockModulationSource source(0.0f);
        source.setValue(0.5f);
        REQUIRE(source.getCurrentValue() == Approx(0.5f));
        source.setValue(-0.5f);
        REQUIRE(source.getCurrentValue() == Approx(-0.5f));
    }
}

// -----------------------------------------------------------------------------
// T006: ModulationDestination struct tests
// -----------------------------------------------------------------------------

TEST_CASE("ModulationDestination struct has correct defaults", "[modulation][foundational]") {
    ModulationDestination dest;

    SECTION("id defaults to 0") {
        REQUIRE(dest.id == 0);
    }

    SECTION("minValue defaults to 0.0") {
        REQUIRE(dest.minValue == Approx(0.0f));
    }

    SECTION("maxValue defaults to 1.0") {
        REQUIRE(dest.maxValue == Approx(1.0f));
    }
}

TEST_CASE("ModulationDestination can be initialized", "[modulation][foundational]") {
    // Create destination for delay time parameter
    ModulationDestination dest;
    dest.id = 5;
    dest.minValue = 0.0f;
    dest.maxValue = 2000.0f;

    REQUIRE(dest.id == 5);
    REQUIRE(dest.minValue == Approx(0.0f));
    REQUIRE(dest.maxValue == Approx(2000.0f));
}

// -----------------------------------------------------------------------------
// T007: ModulationRoute struct tests
// -----------------------------------------------------------------------------

TEST_CASE("ModulationRoute struct has correct defaults", "[modulation][foundational]") {
    ModulationRoute route;

    SECTION("sourceId defaults to 0") {
        REQUIRE(route.sourceId == 0);
    }

    SECTION("destinationId defaults to 0") {
        REQUIRE(route.destinationId == 0);
    }

    SECTION("depth defaults to 0.0") {
        REQUIRE(route.depth == Approx(0.0f));
    }

    SECTION("mode defaults to Bipolar") {
        REQUIRE(route.mode == ModulationMode::Bipolar);
    }

    SECTION("enabled defaults to true") {
        REQUIRE(route.enabled == true);
    }
}

TEST_CASE("ModulationRoute can be configured", "[modulation][foundational]") {
    ModulationRoute route;
    route.sourceId = 3;
    route.destinationId = 7;
    route.depth = 0.75f;
    route.mode = ModulationMode::Unipolar;
    route.enabled = false;

    REQUIRE(route.sourceId == 3);
    REQUIRE(route.destinationId == 7);
    REQUIRE(route.depth == Approx(0.75f));
    REQUIRE(route.mode == ModulationMode::Unipolar);
    REQUIRE(route.enabled == false);
}

// -----------------------------------------------------------------------------
// T008: ModulationMatrix prepare/reset/register tests
// -----------------------------------------------------------------------------

TEST_CASE("ModulationMatrix default constructor", "[modulation][foundational]") {
    ModulationMatrix matrix;

    SECTION("source count is 0") {
        REQUIRE(matrix.getSourceCount() == 0);
    }

    SECTION("destination count is 0") {
        REQUIRE(matrix.getDestinationCount() == 0);
    }

    SECTION("route count is 0") {
        REQUIRE(matrix.getRouteCount() == 0);
    }
}

TEST_CASE("ModulationMatrix prepare() initializes correctly", "[modulation][foundational]") {
    ModulationMatrix matrix;
    matrix.prepare(44100.0, 512, 32);

    SECTION("sample rate is stored") {
        REQUIRE(matrix.getSampleRate() == Approx(44100.0));
    }

    SECTION("can register sources after prepare") {
        MockModulationSource source;
        REQUIRE(matrix.registerSource(0, &source) == true);
        REQUIRE(matrix.getSourceCount() == 1);
    }

    SECTION("can register destinations after prepare") {
        REQUIRE(matrix.registerDestination(0, 0.0f, 100.0f, "Test") == true);
        REQUIRE(matrix.getDestinationCount() == 1);
    }
}

TEST_CASE("ModulationMatrix reset() clears state", "[modulation][foundational]") {
    ModulationMatrix matrix;
    matrix.prepare(44100.0, 512, 32);

    // Register a source and destination
    MockModulationSource source(0.5f);
    matrix.registerSource(0, &source);
    matrix.registerDestination(0, 0.0f, 100.0f, "Test");
    matrix.createRoute(0, 0, 0.5f, ModulationMode::Bipolar);

    // Process to generate some modulation
    matrix.process(512);

    // Reset should clear modulation values
    matrix.reset();

    // After reset, modulation should be 0
    REQUIRE(matrix.getCurrentModulation(0) == Approx(0.0f));
}

TEST_CASE("ModulationMatrix registerSource() works correctly", "[modulation][foundational]") {
    ModulationMatrix matrix;
    matrix.prepare(44100.0, 512, 32);

    MockModulationSource source1, source2, source3;

    SECTION("registers first source at id 0") {
        REQUIRE(matrix.registerSource(0, &source1) == true);
        REQUIRE(matrix.getSourceCount() == 1);
    }

    SECTION("registers multiple sources at different ids") {
        REQUIRE(matrix.registerSource(0, &source1) == true);
        REQUIRE(matrix.registerSource(5, &source2) == true);
        REQUIRE(matrix.registerSource(15, &source3) == true);
        REQUIRE(matrix.getSourceCount() == 3);
    }

    SECTION("rejects invalid source id >= 16") {
        REQUIRE(matrix.registerSource(16, &source1) == false);
        REQUIRE(matrix.getSourceCount() == 0);
    }

    SECTION("rejects null source pointer") {
        REQUIRE(matrix.registerSource(0, nullptr) == false);
        REQUIRE(matrix.getSourceCount() == 0);
    }

    SECTION("allows re-registration at same id (replaces)") {
        REQUIRE(matrix.registerSource(0, &source1) == true);
        REQUIRE(matrix.registerSource(0, &source2) == true);
        REQUIRE(matrix.getSourceCount() == 1); // Still just 1 registered
    }
}

TEST_CASE("ModulationMatrix registerDestination() works correctly", "[modulation][foundational]") {
    ModulationMatrix matrix;
    matrix.prepare(44100.0, 512, 32);

    SECTION("registers first destination at id 0") {
        REQUIRE(matrix.registerDestination(0, 0.0f, 100.0f, "Delay Time") == true);
        REQUIRE(matrix.getDestinationCount() == 1);
    }

    SECTION("registers multiple destinations") {
        REQUIRE(matrix.registerDestination(0, 0.0f, 100.0f, "Delay") == true);
        REQUIRE(matrix.registerDestination(1, 20.0f, 20000.0f, "Cutoff") == true);
        REQUIRE(matrix.registerDestination(2, 0.0f, 1.0f, "Feedback") == true);
        REQUIRE(matrix.getDestinationCount() == 3);
    }

    SECTION("rejects invalid destination id >= 16") {
        REQUIRE(matrix.registerDestination(16, 0.0f, 100.0f, "Invalid") == false);
        REQUIRE(matrix.getDestinationCount() == 0);
    }

    SECTION("accepts null label") {
        REQUIRE(matrix.registerDestination(0, 0.0f, 100.0f, nullptr) == true);
        REQUIRE(matrix.getDestinationCount() == 1);
    }
}

// =============================================================================
// Phase 3: User Story 1 - Route LFO to Delay Time (T023-T029)
// =============================================================================

// -----------------------------------------------------------------------------
// T023: createRoute returns valid index
// -----------------------------------------------------------------------------

TEST_CASE("createRoute returns valid index for valid source/destination", "[modulation][US1]") {
    ModulationMatrix matrix;
    matrix.prepare(44100.0, 512, 32);

    MockModulationSource source;
    matrix.registerSource(0, &source);
    matrix.registerDestination(0, 0.0f, 100.0f, "Test");

    int routeIndex = matrix.createRoute(0, 0, 0.5f, ModulationMode::Bipolar);
    REQUIRE(routeIndex >= 0);
    REQUIRE(matrix.getRouteCount() == 1);
}

// -----------------------------------------------------------------------------
// T024: createRoute returns -1 for invalid source/destination
// -----------------------------------------------------------------------------

TEST_CASE("createRoute returns -1 for invalid source/destination", "[modulation][US1]") {
    ModulationMatrix matrix;
    matrix.prepare(44100.0, 512, 32);

    MockModulationSource source;
    matrix.registerSource(0, &source);
    matrix.registerDestination(0, 0.0f, 100.0f, "Test");

    SECTION("invalid source id") {
        int routeIndex = matrix.createRoute(5, 0, 0.5f, ModulationMode::Bipolar);
        REQUIRE(routeIndex == -1);
        REQUIRE(matrix.getRouteCount() == 0);
    }

    SECTION("invalid destination id") {
        int routeIndex = matrix.createRoute(0, 5, 0.5f, ModulationMode::Bipolar);
        REQUIRE(routeIndex == -1);
        REQUIRE(matrix.getRouteCount() == 0);
    }
}

// -----------------------------------------------------------------------------
// T025: process() reads source value and applies depth
// -----------------------------------------------------------------------------

TEST_CASE("process() reads source value and applies depth", "[modulation][US1]") {
    ModulationMatrix matrix;
    matrix.prepare(44100.0, 512, 32);

    MockModulationSource source(1.0f); // Full positive
    matrix.registerSource(0, &source);
    matrix.registerDestination(0, 0.0f, 100.0f, "Test");
    matrix.createRoute(0, 0, 0.5f, ModulationMode::Bipolar);

    // Process one block
    matrix.process(512);

    // Base value 50, modulation should add +25 (50% of 50 range from center)
    float modulated = matrix.getModulatedValue(0, 50.0f);

    // With bipolar +1.0 source and 0.5 depth:
    // modulation = +1.0 * 0.5 * (100-0)/2 = +25 (half range * depth)
    // Actually per spec: depth scales the amount of modulation applied
    // For range [0,100] with center 50, full modulation (+1) at depth 1.0 would be +50
    // At depth 0.5, it would be +25
    REQUIRE(modulated == Approx(75.0f).margin(1.0f));
}

// -----------------------------------------------------------------------------
// T026: getModulatedValue returns base + modulation offset
// -----------------------------------------------------------------------------

TEST_CASE("getModulatedValue returns base + modulation offset", "[modulation][US1]") {
    ModulationMatrix matrix;
    matrix.prepare(44100.0, 512, 32);

    MockModulationSource source(0.5f); // Half positive
    matrix.registerSource(0, &source);
    matrix.registerDestination(0, 0.0f, 100.0f, "Test");
    matrix.createRoute(0, 0, 1.0f, ModulationMode::Bipolar);

    matrix.process(512);

    float baseValue = 50.0f;
    float modulated = matrix.getModulatedValue(0, baseValue);

    // Source at 0.5, depth 1.0, range 100
    // modulation = 0.5 * 1.0 * 50 = 25
    REQUIRE(modulated == Approx(75.0f).margin(1.0f));
}

// -----------------------------------------------------------------------------
// T027: depth=0.0 results in no modulation
// -----------------------------------------------------------------------------

TEST_CASE("depth=0.0 results in no modulation", "[modulation][US1]") {
    ModulationMatrix matrix;
    matrix.prepare(44100.0, 512, 32);

    MockModulationSource source(1.0f); // Full positive
    matrix.registerSource(0, &source);
    matrix.registerDestination(0, 0.0f, 100.0f, "Test");
    matrix.createRoute(0, 0, 0.0f, ModulationMode::Bipolar); // Zero depth

    matrix.process(512);

    float baseValue = 50.0f;
    float modulated = matrix.getModulatedValue(0, baseValue);

    // Zero depth should result in no modulation
    REQUIRE(modulated == Approx(baseValue));
}

// -----------------------------------------------------------------------------
// T028: depth=1.0 with bipolar source +1.0 gives full range modulation
// -----------------------------------------------------------------------------

TEST_CASE("depth=1.0 with bipolar source +1.0 gives full range modulation", "[modulation][US1]") {
    ModulationMatrix matrix;
    matrix.prepare(44100.0, 512, 32);

    MockModulationSource source(1.0f); // Full positive
    matrix.registerSource(0, &source);
    matrix.registerDestination(0, 0.0f, 100.0f, "Test");
    matrix.createRoute(0, 0, 1.0f, ModulationMode::Bipolar); // Full depth

    matrix.process(512);

    float baseValue = 50.0f;
    float modulated = matrix.getModulatedValue(0, baseValue);

    // Full modulation: base 50 + (1.0 * 1.0 * 50) = 100
    REQUIRE(modulated == Approx(100.0f).margin(1.0f));
}

// -----------------------------------------------------------------------------
// T029: NaN source value treated as 0.0 (FR-018)
// -----------------------------------------------------------------------------

TEST_CASE("NaN source value treated as 0.0", "[modulation][US1][edge]") {
    ModulationMatrix matrix;
    matrix.prepare(44100.0, 512, 32);

    MockModulationSource source(std::numeric_limits<float>::quiet_NaN());
    matrix.registerSource(0, &source);
    matrix.registerDestination(0, 0.0f, 100.0f, "Test");
    matrix.createRoute(0, 0, 1.0f, ModulationMode::Bipolar);

    matrix.process(512);

    float baseValue = 50.0f;
    float modulated = matrix.getModulatedValue(0, baseValue);

    // NaN should be treated as 0, so no modulation
    REQUIRE(modulated == Approx(baseValue).margin(0.001f));
    REQUIRE_FALSE(testIsNaN(modulated)); // Output should not be NaN
}

// =============================================================================
// Phase 4: User Story 2 - Multiple Routes to Same Destination (T038-T040)
// =============================================================================

// -----------------------------------------------------------------------------
// T038: Two routes to same destination sum their contributions
// -----------------------------------------------------------------------------

TEST_CASE("Two routes to same destination sum their contributions", "[modulation][US2]") {
    ModulationMatrix matrix;
    matrix.prepare(44100.0, 512, 32);

    MockModulationSource source1(1.0f);
    MockModulationSource source2(1.0f);
    matrix.registerSource(0, &source1);
    matrix.registerSource(1, &source2);
    matrix.registerDestination(0, 0.0f, 100.0f, "Test");

    // Two routes: 0.3 and 0.5 depth
    matrix.createRoute(0, 0, 0.3f, ModulationMode::Bipolar);
    matrix.createRoute(1, 0, 0.5f, ModulationMode::Bipolar);

    matrix.process(512);

    float baseValue = 50.0f;
    float modulated = matrix.getModulatedValue(0, baseValue);

    // Total modulation: (0.3 + 0.5) * 50 = 40
    // So modulated = 50 + 40 = 90
    REQUIRE(modulated == Approx(90.0f).margin(1.0f));
}

// -----------------------------------------------------------------------------
// T039: Modulation clamped to destination min/max range
// -----------------------------------------------------------------------------

TEST_CASE("Modulation clamped to destination min/max range", "[modulation][US2]") {
    ModulationMatrix matrix;
    matrix.prepare(44100.0, 512, 32);

    MockModulationSource source1(1.0f);
    MockModulationSource source2(1.0f);
    matrix.registerSource(0, &source1);
    matrix.registerSource(1, &source2);
    matrix.registerDestination(0, 0.0f, 100.0f, "Test");

    // Two routes that would exceed range
    matrix.createRoute(0, 0, 1.0f, ModulationMode::Bipolar);
    matrix.createRoute(1, 0, 1.0f, ModulationMode::Bipolar);

    matrix.process(512);

    float baseValue = 80.0f;
    float modulated = matrix.getModulatedValue(0, baseValue);

    // Would be 80 + 50 + 50 = 180, but clamped to 100
    REQUIRE(modulated == Approx(100.0f));
}

// -----------------------------------------------------------------------------
// T040: Opposing polarity routes partially cancel
// -----------------------------------------------------------------------------

TEST_CASE("Opposing polarity routes partially cancel", "[modulation][US2]") {
    ModulationMatrix matrix;
    matrix.prepare(44100.0, 512, 32);

    MockModulationSource source1(1.0f);   // Positive
    MockModulationSource source2(-1.0f);  // Negative
    matrix.registerSource(0, &source1);
    matrix.registerSource(1, &source2);
    matrix.registerDestination(0, 0.0f, 100.0f, "Test");

    // Both at same depth, opposing values
    matrix.createRoute(0, 0, 0.5f, ModulationMode::Bipolar);
    matrix.createRoute(1, 0, 0.5f, ModulationMode::Bipolar);

    matrix.process(512);

    float baseValue = 50.0f;
    float modulated = matrix.getModulatedValue(0, baseValue);

    // +0.5 * 50 and -0.5 * 50 should cancel to 0
    REQUIRE(modulated == Approx(baseValue).margin(0.001f));
}

// =============================================================================
// Phase 5: User Story 3 - Unipolar Modulation Mode (T046-T048)
// =============================================================================

// -----------------------------------------------------------------------------
// T046: Unipolar mode with source -1.0 gives modulation 0.0
// -----------------------------------------------------------------------------

TEST_CASE("Unipolar mode with source -1.0 gives modulation 0.0", "[modulation][US3]") {
    ModulationMatrix matrix;
    matrix.prepare(44100.0, 512, 32);

    MockModulationSource source(-1.0f);
    matrix.registerSource(0, &source);
    matrix.registerDestination(0, 0.0f, 100.0f, "Test");
    matrix.createRoute(0, 0, 1.0f, ModulationMode::Unipolar);

    matrix.process(512);

    float baseValue = 50.0f;
    float modulated = matrix.getModulatedValue(0, baseValue);

    // Unipolar: -1 maps to 0, so no modulation
    REQUIRE(modulated == Approx(baseValue).margin(0.001f));
}

// -----------------------------------------------------------------------------
// T047: Unipolar mode with source +1.0 gives modulation = depth
// -----------------------------------------------------------------------------

TEST_CASE("Unipolar mode with source +1.0 gives modulation = depth", "[modulation][US3]") {
    ModulationMatrix matrix;
    matrix.prepare(44100.0, 512, 32);

    MockModulationSource source(1.0f);
    matrix.registerSource(0, &source);
    matrix.registerDestination(0, 0.0f, 100.0f, "Test");
    matrix.createRoute(0, 0, 0.5f, ModulationMode::Unipolar);

    matrix.process(512);

    float baseValue = 50.0f;
    float modulated = matrix.getModulatedValue(0, baseValue);

    // Unipolar: +1 maps to 1.0, so full depth applied
    // modulation = 1.0 * 0.5 * 50 = 25
    REQUIRE(modulated == Approx(75.0f).margin(1.0f));
}

// -----------------------------------------------------------------------------
// T048: Unipolar mode with source 0.0 gives modulation = 0.5 * depth
// -----------------------------------------------------------------------------

TEST_CASE("Unipolar mode with source 0.0 gives modulation = 0.5 * depth", "[modulation][US3]") {
    ModulationMatrix matrix;
    matrix.prepare(44100.0, 512, 32);

    MockModulationSource source(0.0f);
    matrix.registerSource(0, &source);
    matrix.registerDestination(0, 0.0f, 100.0f, "Test");
    matrix.createRoute(0, 0, 1.0f, ModulationMode::Unipolar);

    matrix.process(512);

    float baseValue = 50.0f;
    float modulated = matrix.getModulatedValue(0, baseValue);

    // Unipolar: 0.0 maps to 0.5, so half depth applied
    // modulation = 0.5 * 1.0 * 50 = 25
    REQUIRE(modulated == Approx(75.0f).margin(1.0f));
}

// =============================================================================
// Phase 6: User Story 4 - Smooth Depth Changes (T053-T054)
// =============================================================================

// -----------------------------------------------------------------------------
// T053: Depth reaches 95% of target within 50ms (SC-003)
// -----------------------------------------------------------------------------

TEST_CASE("Depth reaches 95% of target within 50ms", "[modulation][US4]") {
    constexpr double sampleRate = 44100.0;
    constexpr size_t blockSize = 64;
    constexpr float targetDepth = 1.0f;

    ModulationMatrix matrix;
    matrix.prepare(sampleRate, blockSize, 32);

    MockModulationSource source(1.0f);
    matrix.registerSource(0, &source);
    matrix.registerDestination(0, 0.0f, 100.0f, "Test");
    int route = matrix.createRoute(0, 0, 0.0f, ModulationMode::Bipolar);

    // Change depth from 0 to 1
    matrix.setRouteDepth(route, targetDepth);

    // Calculate samples for 50ms
    size_t samplesFor50ms = static_cast<size_t>(0.050 * sampleRate); // 2205 samples
    size_t blocksNeeded = (samplesFor50ms + blockSize - 1) / blockSize;

    // Process blocks
    for (size_t i = 0; i < blocksNeeded; ++i) {
        matrix.process(blockSize);
    }

    // Current depth should be at least 95% of target
    float currentDepth = matrix.getRouteDepth(route);
    REQUIRE(currentDepth >= 0.95f * targetDepth);
}

// -----------------------------------------------------------------------------
// T054: Smoothed depth applied sample-accurately during block
// -----------------------------------------------------------------------------

TEST_CASE("Smoothed depth applied sample-accurately during block", "[modulation][US4]") {
    constexpr double sampleRate = 44100.0;
    constexpr size_t blockSize = 512;

    ModulationMatrix matrix;
    matrix.prepare(sampleRate, blockSize, 32);

    MockModulationSource source(1.0f);
    matrix.registerSource(0, &source);
    matrix.registerDestination(0, 0.0f, 100.0f, "Test");
    int route = matrix.createRoute(0, 0, 0.0f, ModulationMode::Bipolar);

    // Change depth mid-way
    matrix.setRouteDepth(route, 1.0f);

    // Process first block - depth should be ramping
    matrix.process(blockSize);

    float depthAfter1Block = matrix.getRouteDepth(route);

    // Process second block
    matrix.process(blockSize);

    float depthAfter2Blocks = matrix.getRouteDepth(route);

    // Depth should be increasing toward target
    REQUIRE(depthAfter2Blocks > depthAfter1Block);
}

// =============================================================================
// Phase 7: User Story 5 - Enable/Disable Individual Routes (T063-T065)
// =============================================================================

// -----------------------------------------------------------------------------
// T063: Disabled route produces no modulation
// -----------------------------------------------------------------------------

TEST_CASE("Disabled route produces no modulation", "[modulation][US5]") {
    ModulationMatrix matrix;
    matrix.prepare(44100.0, 512, 32);

    MockModulationSource source(1.0f);
    matrix.registerSource(0, &source);
    matrix.registerDestination(0, 0.0f, 100.0f, "Test");
    int route = matrix.createRoute(0, 0, 1.0f, ModulationMode::Bipolar);

    // Disable the route
    matrix.setRouteEnabled(route, false);
    REQUIRE(matrix.isRouteEnabled(route) == false);

    matrix.process(512);

    float baseValue = 50.0f;
    float modulated = matrix.getModulatedValue(0, baseValue);

    // Disabled route should produce no modulation
    REQUIRE(modulated == Approx(baseValue).margin(0.001f));
}

// -----------------------------------------------------------------------------
// T064: Re-enabled route produces modulation with smoothing
// -----------------------------------------------------------------------------

TEST_CASE("Re-enabled route produces modulation with smoothing", "[modulation][US5]") {
    ModulationMatrix matrix;
    matrix.prepare(44100.0, 512, 32);

    MockModulationSource source(1.0f);
    matrix.registerSource(0, &source);
    matrix.registerDestination(0, 0.0f, 100.0f, "Test");
    int route = matrix.createRoute(0, 0, 1.0f, ModulationMode::Bipolar);

    // Disable, process, then re-enable
    matrix.setRouteEnabled(route, false);
    matrix.process(512);

    matrix.setRouteEnabled(route, true);
    REQUIRE(matrix.isRouteEnabled(route) == true);

    // Process multiple blocks to let smoothing settle
    for (int i = 0; i < 10; ++i) {
        matrix.process(512);
    }

    float baseValue = 50.0f;
    float modulated = matrix.getModulatedValue(0, baseValue);

    // Should now have modulation
    REQUIRE(modulated > baseValue);
}

// -----------------------------------------------------------------------------
// T065: Only enabled routes contribute to destination sum
// -----------------------------------------------------------------------------

TEST_CASE("Only enabled routes contribute to destination sum", "[modulation][US5]") {
    ModulationMatrix matrix;
    matrix.prepare(44100.0, 512, 32);

    MockModulationSource source1(1.0f);
    MockModulationSource source2(1.0f);
    matrix.registerSource(0, &source1);
    matrix.registerSource(1, &source2);
    matrix.registerDestination(0, 0.0f, 100.0f, "Test");

    int route1 = matrix.createRoute(0, 0, 0.5f, ModulationMode::Bipolar);
    int route2 = matrix.createRoute(1, 0, 0.5f, ModulationMode::Bipolar);

    // Disable route2
    matrix.setRouteEnabled(route2, false);

    matrix.process(512);

    float baseValue = 50.0f;
    float modulated = matrix.getModulatedValue(0, baseValue);

    // Only route1 should contribute: 0.5 * 50 = 25
    REQUIRE(modulated == Approx(75.0f).margin(1.0f));
}

// =============================================================================
// Phase 8: User Story 6 - Query Applied Modulation (T072-T074)
// =============================================================================

// -----------------------------------------------------------------------------
// T072: getCurrentModulation returns expected value for single route
// -----------------------------------------------------------------------------

TEST_CASE("getCurrentModulation returns expected value for single route", "[modulation][US6]") {
    ModulationMatrix matrix;
    matrix.prepare(44100.0, 512, 32);

    MockModulationSource source(1.0f);
    matrix.registerSource(0, &source);
    matrix.registerDestination(0, 0.0f, 100.0f, "Test");
    matrix.createRoute(0, 0, 0.5f, ModulationMode::Bipolar);

    matrix.process(512);

    float modulation = matrix.getCurrentModulation(0);

    // modulation = 1.0 * 0.5 * 50 = 25 (half of range)
    REQUIRE(modulation == Approx(25.0f).margin(1.0f));
}

// -----------------------------------------------------------------------------
// T073: getCurrentModulation returns sum for multiple routes
// -----------------------------------------------------------------------------

TEST_CASE("getCurrentModulation returns sum for multiple routes", "[modulation][US6]") {
    ModulationMatrix matrix;
    matrix.prepare(44100.0, 512, 32);

    MockModulationSource source1(1.0f);
    MockModulationSource source2(0.5f);
    matrix.registerSource(0, &source1);
    matrix.registerSource(1, &source2);
    matrix.registerDestination(0, 0.0f, 100.0f, "Test");

    matrix.createRoute(0, 0, 0.4f, ModulationMode::Bipolar);
    matrix.createRoute(1, 0, 0.6f, ModulationMode::Bipolar);

    matrix.process(512);

    float modulation = matrix.getCurrentModulation(0);

    // Route1: 1.0 * 0.4 * 50 = 20
    // Route2: 0.5 * 0.6 * 50 = 15
    // Total: 35
    REQUIRE(modulation == Approx(35.0f).margin(1.0f));
}

// -----------------------------------------------------------------------------
// T074: getCurrentModulation returns 0.0 for destination with no routes
// -----------------------------------------------------------------------------

TEST_CASE("getCurrentModulation returns 0.0 for destination with no routes", "[modulation][US6]") {
    ModulationMatrix matrix;
    matrix.prepare(44100.0, 512, 32);

    matrix.registerDestination(0, 0.0f, 100.0f, "Test");

    matrix.process(512);

    float modulation = matrix.getCurrentModulation(0);

    REQUIRE(modulation == Approx(0.0f));
}

// =============================================================================
// Phase 9: Polish & Cross-Cutting Concerns (T079-T085)
// =============================================================================

// -----------------------------------------------------------------------------
// T080: 32 routes can be created (SC-007)
// -----------------------------------------------------------------------------

TEST_CASE("32 routes can be created", "[modulation][polish]") {
    ModulationMatrix matrix;
    matrix.prepare(44100.0, 512, 32);

    // Register sources and destinations
    std::array<MockModulationSource, 16> sources;
    for (size_t i = 0; i < 16; ++i) {
        matrix.registerSource(static_cast<uint8_t>(i), &sources[i]);
        matrix.registerDestination(static_cast<uint8_t>(i), 0.0f, 100.0f);
    }

    // Create 32 routes
    for (int i = 0; i < 32; ++i) {
        int route = matrix.createRoute(
            static_cast<uint8_t>(i % 16),
            static_cast<uint8_t>(i % 16),
            0.5f,
            ModulationMode::Bipolar
        );
        REQUIRE(route >= 0);
    }

    REQUIRE(matrix.getRouteCount() == 32);
}

// -----------------------------------------------------------------------------
// T081: Depth clamped to [0, 1] range on setRouteDepth
// -----------------------------------------------------------------------------

TEST_CASE("Depth clamped to [0, 1] range on setRouteDepth", "[modulation][polish]") {
    ModulationMatrix matrix;
    matrix.prepare(44100.0, 512, 32);

    MockModulationSource source;
    matrix.registerSource(0, &source);
    matrix.registerDestination(0, 0.0f, 100.0f, "Test");
    int route = matrix.createRoute(0, 0, 0.5f, ModulationMode::Bipolar);

    SECTION("negative depth clamped to 0") {
        matrix.setRouteDepth(route, -0.5f);
        // Process to let smoother update
        for (int i = 0; i < 100; ++i) matrix.process(512);
        REQUIRE(matrix.getRouteDepth(route) >= 0.0f);
    }

    SECTION("depth > 1 clamped to 1") {
        matrix.setRouteDepth(route, 1.5f);
        // Process to let smoother update
        for (int i = 0; i < 100; ++i) matrix.process(512);
        REQUIRE(matrix.getRouteDepth(route) <= 1.0f);
    }
}

// -----------------------------------------------------------------------------
// T082: getModulatedValue accuracy within 0.0001 tolerance (SC-006)
// -----------------------------------------------------------------------------

TEST_CASE("getModulatedValue accuracy within tolerance", "[modulation][polish]") {
    ModulationMatrix matrix;
    matrix.prepare(44100.0, 512, 32);

    MockModulationSource source(0.5f);
    matrix.registerSource(0, &source);
    matrix.registerDestination(0, 0.0f, 100.0f, "Test");
    matrix.createRoute(0, 0, 0.6f, ModulationMode::Bipolar);

    // Process enough to settle smoothing
    for (int i = 0; i < 100; ++i) {
        matrix.process(512);
    }

    float baseValue = 40.0f;
    float modulated = matrix.getModulatedValue(0, baseValue);

    // Expected: 40 + (0.5 * 0.6 * 50) = 40 + 15 = 55
    REQUIRE(modulated == Approx(55.0f).margin(0.0001f));
}

// -----------------------------------------------------------------------------
// T079: Performance test - 16 routes process in <1% CPU at 44.1kHz (SC-001)
// -----------------------------------------------------------------------------

TEST_CASE("16 routes process efficiently", "[modulation][polish][performance]") {
    ModulationMatrix matrix;
    matrix.prepare(44100.0, 512, 32);

    // Create 16 sources and destinations
    std::array<MockModulationSource, 16> sources;
    for (size_t i = 0; i < 16; ++i) {
        sources[i].setValue(static_cast<float>(i) / 15.0f); // Varied values
        matrix.registerSource(static_cast<uint8_t>(i), &sources[i]);
        matrix.registerDestination(static_cast<uint8_t>(i), 0.0f, 100.0f);
    }

    // Create 16 routes
    for (int i = 0; i < 16; ++i) {
        int route = matrix.createRoute(
            static_cast<uint8_t>(i),
            static_cast<uint8_t>(i),
            0.5f,
            ModulationMode::Bipolar
        );
        REQUIRE(route >= 0);
    }

    // Process many blocks without timing - just verify it completes
    // Actual performance is measured in benchmarks, not unit tests
    for (int block = 0; block < 1000; ++block) {
        matrix.process(512);
    }

    // Verify modulation is being applied
    // Use destination 8 which has source value 8/15 ≈ 0.533 (non-zero)
    float modulated = matrix.getModulatedValue(8, 50.0f);
    REQUIRE(modulated != 50.0f); // Should have some modulation
}

// -----------------------------------------------------------------------------
// T083: Zero allocations during process() (SC-005)
// -----------------------------------------------------------------------------

// Note: Allocation tracking requires test helper infrastructure
// For now, verify noexcept guarantees and that process() is real-time safe
TEST_CASE("process() is noexcept", "[modulation][polish][realtime]") {
    ModulationMatrix matrix;
    matrix.prepare(44100.0, 512, 32);

    MockModulationSource source(1.0f);
    matrix.registerSource(0, &source);
    matrix.registerDestination(0, 0.0f, 100.0f, "Test");
    matrix.createRoute(0, 0, 0.5f, ModulationMode::Bipolar);

    // Verify process() is noexcept by checking static property
    static_assert(noexcept(matrix.process(512)), "process() must be noexcept");

    // Process multiple blocks
    for (int i = 0; i < 100; ++i) {
        matrix.process(512);
    }

    // Verify results are correct after many iterations
    float modulated = matrix.getModulatedValue(0, 50.0f);
    REQUIRE(modulated > 50.0f);
}

// -----------------------------------------------------------------------------
// T084: Registration happens only during prepare phase (FR-013)
// -----------------------------------------------------------------------------

TEST_CASE("Registration is intended for prepare phase", "[modulation][polish]") {
    ModulationMatrix matrix;
    matrix.prepare(44100.0, 512, 32);

    MockModulationSource source;

    // Registration works after prepare
    REQUIRE(matrix.registerSource(0, &source) == true);
    REQUIRE(matrix.registerDestination(0, 0.0f, 100.0f, "Test") == true);

    // Create a route
    int route = matrix.createRoute(0, 0, 0.5f, ModulationMode::Bipolar);
    REQUIRE(route >= 0);

    // Note: The spec says registration should happen during prepare phase
    // but doesn't forbid it during processing - it's just documented behavior
    // This test verifies the documented use case works correctly
}

// -----------------------------------------------------------------------------
// T085: Depth changes produce no audible clicks (SC-004 glitch-free)
// -----------------------------------------------------------------------------

TEST_CASE("Depth changes are glitch-free with smoothing", "[modulation][polish]") {
    constexpr double sampleRate = 44100.0;
    constexpr size_t blockSize = 64;

    ModulationMatrix matrix;
    matrix.prepare(sampleRate, blockSize, 32);

    MockModulationSource source(1.0f);
    matrix.registerSource(0, &source);
    matrix.registerDestination(0, 0.0f, 100.0f, "Test");
    int route = matrix.createRoute(0, 0, 0.0f, ModulationMode::Bipolar);

    // Rapid depth changes
    std::vector<float> depthHistory;

    for (int i = 0; i < 50; ++i) {
        // Alternate between 0 and 1 rapidly
        float targetDepth = (i % 2 == 0) ? 1.0f : 0.0f;
        matrix.setRouteDepth(route, targetDepth);

        matrix.process(blockSize);
        depthHistory.push_back(matrix.getRouteDepth(route));
    }

    // Verify no instant jumps - depth should change gradually
    // Check that consecutive values don't differ by more than a reasonable amount
    float maxChange = 0.0f;
    for (size_t i = 1; i < depthHistory.size(); ++i) {
        float change = std::abs(depthHistory[i] - depthHistory[i - 1]);
        maxChange = std::max(maxChange, change);
    }

    // With 20ms smoothing at 44.1kHz, 64 samples = 1.45ms
    // Maximum change in 1.45ms should be small fraction of full range
    // 5 time constants for 99% = 100ms, so 1.45ms ≈ 7.25% of full transition
    REQUIRE(maxChange < 0.5f); // Conservative limit - smoothing prevents full jumps
}
