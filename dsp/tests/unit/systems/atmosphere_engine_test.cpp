// ==============================================================================
// Layer 3: System Tests - AtmosphereEngine, main TU
//                                        (specs/seraphis-phase5-atmosphere)
// ==============================================================================
// Constitution Principle XII: Test-First Development.
//
// Reference: specs/seraphis-phase5-atmosphere/spec.md
//            specs/seraphis-phase5-atmosphere/plan.md   (S15 test plan)
//            specs/seraphis-phase5-atmosphere/tasks.md  (T002 creates this TU;
//                                                        later tasks fill it)
//
// SCOPE OF THIS TU (plan.md S15's TU-ownership table):
//   SC-001  AtmosphereEngine_NoAllocationAfterPrepare      (S15.1)
//   SC-002  AtmosphereEngine_GrainLiveness                 (S15.2)
//   SC-008  AtmosphereEngine_BoundedUnderStress            (S15.6)
//   SC-009  AtmosphereEngine_SampleRateIndependence        (S15.6)
//   SC-010  AtmosphereEngine_SeedDeterminism               (S15.6)
//   SC-011  AtmosphereEngine_BlockPartitionInvariance      (S15.6)
//   the FR-level cases of S15.8 (lifecycle/guards, control-table clamps,
//   capture + cold ring, skip-never-steal, forced envelope endpoints,
//   population gain, pan + decorrelation, blur-disabled-is-free, freeze
//   capture/release, silence latch + reset, seed-zero-is-valid)
//   PLUS the two clauses that must run UNDER fast-math:
//     - SC-014's fourth clause, AtmosphereEngine_NonFiniteGuardSurvivesFastMath
//     - a copy of SC-012's sub-case 6 (non-finite age argument to
//       RollingCaptureBuffer::readStereoLinear)
//
// WHY THIS TU IS DELIBERATELY ABSENT FROM THE -fno-fast-math LIST:
//   dsp/tests/CMakeLists.txt lists ONLY atmosphere_engine_nonfinite_test.cpp
//   under "-fno-fast-math -fno-finite-math-only". That omission here is
//   LOAD-BEARING, not an oversight. The guards Phase 5 relies on - the
//   ITERUM_NOINLINE finiteness helper (plan S13.2) and the ordered-comparison
//   age clamp in RollingCaptureBuffer::readStereoLinear (plan S2) - exist
//   specifically to survive /fp:fast (MSVC) and -ffast-math (the macOS leg),
//   which is the ONLY configuration the shipped header is ever compiled in.
//   Proving them in a -fno-fast-math TU proves nothing about a shipped build.
//   DO NOT add this file to that list.
//
// NON-FINITE VALUES IN THIS TU: never std::isnan / std::isinf /
//   std::numeric_limits<float>::quiet_NaN() / infinity() - those fold to finite
//   garbage under -ffast-math. Build them from bit patterns through a volatile
//   sink (plan S15.7's makeNonFinite) and classify with
//   Krate::DSP::detail::isNaN (core/db_utils.h:54-57) and
//   Krate::DSP::detail::isInf (:175-178).
//
// ALLOCATION DETECTION: include <allocation_detector.h> ONLY. This TU must NOT
//   include <allocation_operator_overrides.h> - the single owner of the global
//   operator new/delete override in dsp_systems_tests is
//   dsp/tests/unit/systems/selectable_oscillator_test.cpp:388, and a second
//   include is a duplicate-symbol link error.
//
// NO BIT-EXACT FLOAT GOLDENS: SC-010 uses
//   tests/test_helpers/render_fingerprint.h (measured tolerances), never an FNV
//   digest over sample bits (node tools/lint-float-bit-goldens.js gates this).
// ==============================================================================

#include <catch2/catch_all.hpp>

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/core/pitch_utils.h>
#include <krate/dsp/core/random.h>
#include <krate/dsp/primitives/rolling_capture_buffer.h>
// THE ONE PLACE brownian_drift.h IS INCLUDED (plan S3, S15.2). The ENGINE
// header must NOT include it - FR-030 reproduces the OU recurrence as SoA lanes
// and instantiates no BrownianDrift - so the equivalence gate below is the only
// thing that may reach for the real component. node tools/lint-layers.js gates
// the header; this include is a TEST-side dependency and is layer-legal.
#include <krate/dsp/processors/brownian_drift.h>
#include <krate/dsp/processors/grain_scheduler.h>
#include <krate/dsp/systems/atmosphere_engine.h>

// SC-010 / FR-070's determinism comparisons go through this and NOTHING else:
// four aggregate metrics plus 32 spaced sample checkpoints, at the measured
// cross-toolchain tolerances kSampleTolerance = 1.0e-4f (:49) and
// kMetricTolerance = 1.0e-5 (:52). An FNV digest over the sample bits would
// demand bit-identical floating-point math from MSVC, GCC and Apple Clang - see
// the file banner - and `node tools/lint-float-bit-goldens.js` gates it.
#include "render_fingerprint.h"

// SC-001's allocation counter. THIS HEADER ONLY - never
// <allocation_operator_overrides.h>. The global operator new/delete
// replacements that make the counter observe anything are defined ONCE per test
// binary, and for dsp_systems_tests the single owner is
// dsp/tests/unit/systems/selectable_oscillator_test.cpp:388. A second
// definition in this TU is a duplicate-symbol link error, and
// node tools/lint-allocation-operator-overrides.js gates it.
#include <allocation_detector.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

// =============================================================================
// SC-012 sub-case 6, duplicated here ON PURPOSE (plan S15.7, task T003)
// =============================================================================
// The same assertions live in
// dsp/tests/unit/primitives/test_rolling_capture_buffer.cpp, which IS in the
// -fno-fast-math -fno-finite-math-only list (dsp/tests/CMakeLists.txt:468).
// This TU is NOT - see the "WHY THIS TU IS DELIBERATELY ABSENT" note at the top
// of the file. readStereoLinear's age clamp is written as two ordered
// comparisons precisely so it survives /fp:fast and -ffast-math; if a compiler
// folds `!(age >= 0.0f)` away, THIS copy is where it surfaces, while the
// -fno-fast-math copy keeps passing.

namespace {

/// Build a non-finite float from its bit pattern through a volatile sink.
/// std::numeric_limits<float>::quiet_NaN() / infinity() fold to finite garbage
/// under -ffast-math, so they are never used. 0x7FC00000 = quiet NaN,
/// 0x7F800000 = +Inf, 0xFF800000 = -Inf.
[[nodiscard]] float makeNonFinite(std::uint32_t bits) noexcept {
    volatile std::uint32_t b = bits;  // defeats constant folding
    const std::uint32_t materialized = b;  // the volatile READ is the sink
    float f = 0.0f;
    std::memcpy(&f, &materialized, sizeof(f));
    return f;
}

// =============================================================================
// T006 helpers - the FR-025 shadow model
// =============================================================================

/// The result of restating FR-025's birth arithmetic (plan S9.4c) in the test.
/// This is written out from the SPEC's formulas, so it is an independent
/// statement of the rule rather than a call into the implementation.
struct LivenessMath {
    double ratioMin = 1.0;
    double ratioMax = 1.0;
    double wUp = 0.0;      ///< (rMax - 1)+ : the rate at which the age SHRINKS
    double wDown = 0.0;    ///< (1 - rMin)+ : the rate at which the age GROWS
    double w = 0.0;        ///< wUp + wDown - the SUM (Clarification Q1), never the maximum
    double lifetime = 0.0; ///< L' after FR-025 truncation
    double ageLo = 0.0;
    double ageHi = 0.0;
    bool valid = false;    ///< false when the ring is too short for this configuration
};

/// @param capacity   getCaptureCapacitySamples()
/// @param staticSemis  the grain's s (pitchSpread must be 0 for this to be known)
/// @param driftSemis   the grain's d (driftRangeSemitones at its birth)
/// @param decorrAge    dR in samples (0 in every clause that asserts a closed form)
[[nodiscard]] LivenessMath livenessMath(double capacity, float staticSemis, float driftSemis,
                                        double decorrAge, float grainSeconds, double sampleRate) {
    using Krate::DSP::AtmosphereEngine;
    constexpr float kAbs = AtmosphereEngine::kMaxAbsGrainSemitones;

    // The +/-36 clamp lands on the ENVELOPE ENDPOINTS, not only on s.
    const float semisLo = std::clamp(staticSemis - driftSemis, -kAbs, kAbs);
    const float semisHi = std::clamp(staticSemis + driftSemis, -kAbs, kAbs);

    LivenessMath math;
    math.ratioMin = static_cast<double>(Krate::DSP::semitonesToRatio(semisLo));
    math.ratioMax = static_cast<double>(Krate::DSP::semitonesToRatio(semisHi));
    math.wUp = std::max(math.ratioMax - 1.0, 0.0);
    math.wDown = std::max(1.0 - math.ratioMin, 0.0);
    math.w = math.wUp + math.wDown;

    const double guard = static_cast<double>(AtmosphereEngine::kMinAgeSamples);
    const double requested = std::round(static_cast<double>(grainSeconds) * sampleRate);
    // `guard` TWICE: once as FR-025's young-side g, once as FR-014's admission
    // margin, which once the ring is full is an old-side bound of C - g. See
    // AtmosphereEngine::tryBirthGrain()'s headroom for why the two rules are not
    // jointly satisfiable otherwise.
    const double headroom = capacity - 2.0 - guard - guard - decorrAge;
    if (headroom <= 2.0) {
        return math;
    }
    // D-12's reserved ceiling slack. Writing this from the spec's unreserved
    // floor((C - 2 - g)/w) fails a CORRECT implementation by one sample.
    const double slack = headroom - 2.0;
    math.lifetime = (math.w * requested > slack) ? std::floor(slack / math.w) : requested;
    if (math.lifetime < 2.0) {
        return math;
    }
    math.ageLo = std::ceil(math.wUp * math.lifetime) + guard;
    math.ageHi = capacity - 2.0 - guard - std::ceil(math.wDown * math.lifetime) - decorrAge;
    math.valid = true;
    return math;
}

/// Fixed-size stereo block plumbing, so each clause below states its
/// configuration rather than its buffer management.
struct BlockRenderer {
    std::vector<float> inLeft;
    std::vector<float> inRight;
    std::vector<float> outLeft;
    std::vector<float> outRight;

    BlockRenderer(std::size_t blockSize, float left, float right)
        : inLeft(blockSize, left), inRight(blockSize, right), outLeft(blockSize, 0.0f),
          outRight(blockSize, 0.0f) {}

    void render(Krate::DSP::AtmosphereEngine& engine) {
        engine.processStereoBlock(inLeft.data(), inRight.data(), outLeft.data(), outRight.data(),
                                  inLeft.size());
    }

    [[nodiscard]] bool outputIsExactlyZero() const {
        for (std::size_t i = 0; i < outLeft.size(); ++i) {
            if (outLeft[i] != 0.0f || outRight[i] != 0.0f) {
                return false;
            }
        }
        return true;
    }
};

/// Render until a grain has been born (plus `extraBlocks` more so the grain
/// actually ages), or until `maxBlocks` is exhausted.
void renderUntilBirth(Krate::DSP::AtmosphereEngine& engine, BlockRenderer& blocks,
                      std::size_t maxBlocks, std::size_t extraBlocks) {
    std::size_t blocksAfterBirth = 0;
    for (std::size_t b = 0; b < maxBlocks && blocksAfterBirth < extraBlocks; ++b) {
        blocks.render(engine);
        if (engine.getTotalGrainsBorn() > 0) {
            ++blocksAfterBirth;
        }
    }
}

// =============================================================================
// T008 helpers - the output stage
// =============================================================================

/// Bit test for a denormal: exponent field 0 with a NON-ZERO mantissa (an
/// exponent field of 0 with a zero mantissa is +/-0, which is exactly what
/// FR-064 asks for). Written as a bit test because std::fpclassify /
/// std::isnormal are unusable in a TU compiled WITH fast-math - see the file
/// banner - and because it is the same exponent-field form
/// Krate::DSP::detail::isNaN uses (core/db_utils.h:54-57).
[[nodiscard]] bool isDenormal(float value) noexcept {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return ((bits & 0x7F800000u) == 0u) && ((bits & 0x007FFFFFu) != 0u);
}

/// WHITE-noise stereo block plumbing, with the SAME noise on both channels.
///
/// White specifically, and identical on both channels, for two independent
/// reasons that both matter to the clauses below:
///   1. Two grains sum COHERENTLY only when their read positions coincide to
///      within the source's correlation length. For white noise that is one
///      sample, so across a birth window hundreds of milliseconds wide the
///      incoherent-sum assumption the 1/sqrt(n) law rests on holds robustly. A
///      tone or DC would make every grain coherent with every other and the
///      output would scale as n rather than as sqrt(n).
///   2. With identical input channels, any difference between the two OUTPUT
///      channels is something the engine did - the per-grain pan or the
///      per-grain L/R read-age decorrelation - and nothing else.
struct NoiseRenderer {
    Krate::DSP::Xorshift32 rng;
    std::vector<float> inLeft;
    std::vector<float> inRight;
    std::vector<float> outLeft;
    std::vector<float> outRight;

    NoiseRenderer(std::size_t blockSize, std::uint32_t seed)
        : rng(seed), inLeft(blockSize, 0.0f), inRight(blockSize, 0.0f),
          outLeft(blockSize, 0.0f), outRight(blockSize, 0.0f) {}

    void render(Krate::DSP::AtmosphereEngine& engine) {
        for (std::size_t i = 0; i < inLeft.size(); ++i) {
            const float value = rng.nextFloat() * 0.5f;
            inLeft[i] = value;
            inRight[i] = value;
        }
        engine.processStereoBlock(inLeft.data(), inRight.data(), outLeft.data(), outRight.data(),
                                  inLeft.size());
    }
};

/// What one noise render measured. Everything here is cheap to maintain per
/// block, and every field is used by at least one clause as a
/// NON-VACUOUSNESS guard rather than as the assertion itself.
struct NoiseRenderStats {
    double rmsLeft = 0.0;
    double rmsRight = 0.0;
    double correlation = 0.0;  ///< inter-channel Pearson coefficient over the window
    double maxAbsDelta = 0.0;  ///< max |L - R| over the window
    std::size_t maxActive = 0;
    std::uint64_t born = 0;
    bool exactlyZero = true;
    bool anyDenormal = false;
};

/// Render `seconds` of white noise and measure the output.
NoiseRenderStats renderNoise(Krate::DSP::AtmosphereEngine& engine, NoiseRenderer& noise,
                             double seconds, double sampleRate) {
    NoiseRenderStats stats;
    const auto blockSize = static_cast<double>(noise.inLeft.size());
    const auto blocks = static_cast<std::size_t>((seconds * sampleRate) / blockSize);

    double sumLL = 0.0;
    double sumRR = 0.0;
    double sumLR = 0.0;
    std::size_t count = 0;

    for (std::size_t b = 0; b < blocks; ++b) {
        noise.render(engine);
        for (std::size_t i = 0; i < noise.outLeft.size(); ++i) {
            const double left = static_cast<double>(noise.outLeft[i]);
            const double right = static_cast<double>(noise.outRight[i]);
            sumLL += left * left;
            sumRR += right * right;
            sumLR += left * right;
            stats.maxAbsDelta = std::max(stats.maxAbsDelta, std::abs(left - right));
            if (noise.outLeft[i] != 0.0f || noise.outRight[i] != 0.0f) {
                stats.exactlyZero = false;
            }
            if (isDenormal(noise.outLeft[i]) || isDenormal(noise.outRight[i])) {
                stats.anyDenormal = true;
            }
        }
        count += noise.outLeft.size();
        stats.maxActive = std::max(stats.maxActive, engine.getActiveGrainCount());
    }

    if (count > 0) {
        stats.rmsLeft = std::sqrt(sumLL / static_cast<double>(count));
        stats.rmsRight = std::sqrt(sumRR / static_cast<double>(count));
    }
    const double denominator = std::sqrt(sumLL * sumRR);
    stats.correlation = (denominator > 0.0) ? (sumLR / denominator) : 0.0;
    stats.born = engine.getTotalGrainsBorn();
    return stats;
}

/// dBFS of an RMS, floored so a silent window is a finite number rather than
/// -inf (which cannot even be formed here - see the file banner).
[[nodiscard]] double rmsToDb(double rms) {
    return 20.0 * std::log10(std::max(rms, 1e-12));
}

}  // namespace

TEST_CASE("RollingCaptureBuffer_ReadStereoLinearFastMath", "[atmosphere]") {
    Krate::DSP::RollingCaptureBuffer buffer;
    buffer.prepare(48000.0, 1.0f);
    REQUIRE(buffer.getCapacitySamples() == 65536);

    for (std::size_t i = 0; i < 1000; ++i) {
        const float value = static_cast<float>(i) * 1e-4f;
        buffer.writeStereo(value, -value);
    }

    const float nanAge = makeNonFinite(0x7FC00000u);
    const float posInfAge = makeNonFinite(0x7F800000u);
    const float negInfAge = makeNonFinite(0xFF800000u);

    // Age 0 == the most recent sample.
    float youngestL = 0.0f;
    float youngestR = 0.0f;
    buffer.readStereoLinear(0.0f, youngestL, youngestR);
    REQUIRE(youngestL != 0.0f);  // guard against a vacuous comparison

    // The largest legal finite age.
    const float maxAge = static_cast<float>(buffer.getAvailableSamples() - 2);
    float oldestL = 0.0f;
    float oldestR = 0.0f;
    buffer.readStereoLinear(maxAge, oldestL, oldestR);

    // NaN: `!(age >= 0.0f)` is taken because an unordered compare is false.
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

// =============================================================================
// T004 - FR-003 / FR-004: lifecycle and entry guards
// =============================================================================

TEST_CASE("AtmosphereEngine_LifecycleAndGuards", "[atmosphere]") {
    using Krate::DSP::AtmosphereEngine;

    constexpr std::size_t kBlock = 512;
    const std::vector<float> inL(kBlock, 0.5f);
    const std::vector<float> inR(kBlock, -0.25f);

    SECTION("an un-prepared engine renders exactly 0.0f") {
        AtmosphereEngine engine;
        std::vector<float> outL(kBlock, -7.0f);
        std::vector<float> outR(kBlock, -7.0f);

        engine.processStereoBlock(inL.data(), inR.data(), outL.data(), outR.data(), kBlock);

        for (std::size_t i = 0; i < kBlock; ++i) {
            REQUIRE(outL[i] == 0.0f);
            REQUIRE(outR[i] == 0.0f);
        }
    }

    SECTION("any null pointer writes nothing and returns") {
        AtmosphereEngine engine;
        engine.prepare(48000.0, AtmosphereEngine::PrepareConfig{});

        // Sentinel: if the guard runs AFTER any write, one of these moves.
        std::vector<float> outL(kBlock, -7.0f);
        std::vector<float> outR(kBlock, -7.0f);

        engine.processStereoBlock(nullptr, inR.data(), outL.data(), outR.data(), kBlock);
        engine.processStereoBlock(inL.data(), nullptr, outL.data(), outR.data(), kBlock);
        engine.processStereoBlock(inL.data(), inR.data(), nullptr, outR.data(), kBlock);
        engine.processStereoBlock(inL.data(), inR.data(), outL.data(), nullptr, kBlock);

        for (std::size_t i = 0; i < kBlock; ++i) {
            REQUIRE(outL[i] == -7.0f);
            REQUIRE(outR[i] == -7.0f);
        }
    }

    SECTION("numSamples == 0 is a no-op and advances no control step") {
        AtmosphereEngine engine;
        engine.prepare(48000.0, AtmosphereEngine::PrepareConfig{});

        const std::uint64_t born = engine.getTotalGrainsBorn();
        const std::uint64_t skipPoolFull = engine.getSkippedTriggerCountPoolFull();
        const std::uint64_t skipRingCold = engine.getSkippedTriggerCountRingCold();

        std::vector<float> outL(kBlock, -7.0f);
        std::vector<float> outR(kBlock, -7.0f);

        for (int call = 0; call < 1000; ++call) {
            engine.processStereoBlock(inL.data(), inR.data(), outL.data(), outR.data(), 0);
        }

        // A control grid anchored to a block counter rather than to an absolute
        // sample counter would have ticked 1000 times here.
        REQUIRE(engine.getTotalGrainsBorn() == born);
        REQUIRE(engine.getSkippedTriggerCountPoolFull() == skipPoolFull);
        REQUIRE(engine.getSkippedTriggerCountRingCold() == skipRingCold);

        for (std::size_t i = 0; i < kBlock; ++i) {
            REQUIRE(outL[i] == -7.0f);
            REQUIRE(outR[i] == -7.0f);
        }
    }

    SECTION("a second prepare fully reconfigures") {
        AtmosphereEngine engine;

        AtmosphereEngine::PrepareConfig first;
        first.captureSeconds = 4.0f;
        first.blurEnabled = true;
        first.blurFftSize = 1024;
        engine.prepare(48000.0, first);

        // RollingCaptureBuffer rounds the capacity UP to a power of two
        // (rolling_capture_buffer.h:83): nextPowerOf2(48000 * 4) == 262144.
        REQUIRE(engine.getCaptureCapacitySamples() == 262144u);
        REQUIRE(engine.getLatencySamples() == 1024u);

        AtmosphereEngine::PrepareConfig second;
        second.captureSeconds = 2.0f;
        second.blurEnabled = true;
        second.blurFftSize = 2048;
        engine.prepare(44100.0, second);

        // nextPowerOf2(44100 * 2) == 131072.
        REQUIRE(engine.getCaptureCapacitySamples() == 131072u);
        REQUIRE(engine.getLatencySamples() == 2048u);
    }
}

// =============================================================================
// T004 - FR-009: the control table
// =============================================================================
// Both previously deferred clauses have now landed:
//   - the FR-064 `level = 0` exact-silence clause, with the output stage (T008);
//   - the blur smoother-cadence settling clause (plan S11.2, deviation D-15),
//     with the blur pump (T012). It is the LAST SECTION below.

TEST_CASE("AtmosphereEngine_ControlTableClamps", "[atmosphere]") {
    using Krate::DSP::AtmosphereEngine;

    // Non-finite arguments built from bit patterns through a volatile sink:
    // this TU is compiled WITH fast-math on purpose (see the file banner), so
    // std::numeric_limits<float>::quiet_NaN() / infinity() are unusable here.
    const float nanValue = makeNonFinite(0x7FC00000u);
    const float posInf = makeNonFinite(0x7F800000u);
    const float negInf = makeNonFinite(0xFF800000u);

    AtmosphereEngine engine;
    engine.prepare(48000.0, AtmosphereEngine::PrepareConfig{});

    // Every clamped result below is an EXACT float (a clamp returns one of its
    // three arguments unchanged), so `==` is the right comparison, not Approx.
    auto checkControl = [&](const char* name, auto setter, auto getter, float lo, float hi,
                            float def, float probe) {
        INFO("control: " << name);

        setter(probe);
        REQUIRE(getter() == probe);

        setter(lo - 1.0f);
        REQUIRE(getter() == lo);

        setter(hi + 1.0f);
        REQUIRE(getter() == hi);

        // A non-finite argument lands on the FIELD'S DEFAULT, not on a clamp
        // bound and not on the previous value.
        setter(probe);
        setter(nanValue);
        REQUIRE(getter() == def);

        setter(probe);
        setter(posInf);
        REQUIRE(getter() == def);

        setter(probe);
        setter(negInf);
        REQUIRE(getter() == def);
    };

    SECTION("a freshly prepared engine carries every FR-009 default") {
        REQUIRE(engine.getGrainSeconds() == 4.0f);
        REQUIRE(engine.getDensity() == 4.0f);
        REQUIRE(engine.getJitter() == 0.5f);
        REQUIRE(engine.getPositionSeconds() == 1.0f);
        REQUIRE(engine.getPositionSpread() == 0.3f);
        REQUIRE(engine.getPitchSemitones() == 0.0f);
        REQUIRE(engine.getPitchSpread() == 0.15f);
        REQUIRE(engine.getDriftDepth() == 0.3f);
        REQUIRE(engine.getDriftSmoothness() == 0.7f);
        REQUIRE(engine.getDriftRangeSemitones() == 2.0f);
        REQUIRE(engine.getPanSpread() == 0.7f);
        REQUIRE(engine.getDecorrelation() == 0.5f);
        REQUIRE(engine.getBlur() == 0.0f);
        REQUIRE(engine.getFreezeMix() == 0.0f);
        REQUIRE(engine.getLevel() == 1.0f);
        REQUIRE(engine.getGrainEnvelope() == Krate::DSP::GrainEnvelopeType::Hann);
        REQUIRE(engine.getSeed() == AtmosphereEngine::kDefaultSeed);
    }

    SECTION("every setter clamps to its range and sanitises non-finites") {
        checkControl(
            "grainSeconds", [&](float v) { engine.setGrainSeconds(v); },
            [&] { return engine.getGrainSeconds(); }, 0.05f, 30.0f, 4.0f, 12.0f);
        checkControl(
            "density", [&](float v) { engine.setDensity(v); },
            [&] { return engine.getDensity(); }, 0.1f, 20.0f, 4.0f, 7.0f);
        checkControl(
            "jitter", [&](float v) { engine.setJitter(v); }, [&] { return engine.getJitter(); },
            0.0f, 1.0f, 0.5f, 0.25f);
        checkControl(
            "positionSeconds", [&](float v) { engine.setPositionSeconds(v); },
            [&] { return engine.getPositionSeconds(); }, 0.0f, 30.0f, 1.0f, 5.0f);
        checkControl(
            "positionSpread", [&](float v) { engine.setPositionSpread(v); },
            [&] { return engine.getPositionSpread(); }, 0.0f, 1.0f, 0.3f, 0.75f);
        checkControl(
            "pitchSemitones", [&](float v) { engine.setPitchSemitones(v); },
            [&] { return engine.getPitchSemitones(); }, -24.0f, 24.0f, 0.0f, 7.0f);
        checkControl(
            "pitchSpread", [&](float v) { engine.setPitchSpread(v); },
            [&] { return engine.getPitchSpread(); }, 0.0f, 1.0f, 0.15f, 0.5f);
        checkControl(
            "driftDepth", [&](float v) { engine.setDriftDepth(v); },
            [&] { return engine.getDriftDepth(); }, 0.0f, 1.0f, 0.3f, 0.875f);
        checkControl(
            "driftSmoothness", [&](float v) { engine.setDriftSmoothness(v); },
            [&] { return engine.getDriftSmoothness(); }, 0.0f, 1.0f, 0.7f, 0.25f);
        checkControl(
            "driftRangeSemitones", [&](float v) { engine.setDriftRangeSemitones(v); },
            [&] { return engine.getDriftRangeSemitones(); }, 0.0f, 12.0f, 2.0f, 5.0f);
        checkControl(
            "panSpread", [&](float v) { engine.setPanSpread(v); },
            [&] { return engine.getPanSpread(); }, 0.0f, 1.0f, 0.7f, 0.25f);
        checkControl(
            "decorrelation", [&](float v) { engine.setDecorrelation(v); },
            [&] { return engine.getDecorrelation(); }, 0.0f, 1.0f, 0.5f, 0.125f);
        checkControl(
            "blur", [&](float v) { engine.setBlur(v); }, [&] { return engine.getBlur(); }, 0.0f,
            1.0f, 0.0f, 0.5f);
        checkControl(
            "freezeMix", [&](float v) { engine.setFreezeMix(v); },
            [&] { return engine.getFreezeMix(); }, 0.0f, 1.0f, 0.0f, 0.5f);
        checkControl(
            "level", [&](float v) { engine.setLevel(v); }, [&] { return engine.getLevel(); }, 0.0f,
            2.0f, 1.0f, 1.5f);
    }

    SECTION("the density floor is the same bound GrainScheduler enforces") {
        engine.setDensity(0.01f);
        REQUIRE(engine.getDensity() == 0.1f);

        // grain_scheduler.h:47 does std::max(0.1f, grainsPerSecond). The control
        // table matches it exactly, so the table and the component agree instead
        // of the component silently overriding the table.
        Krate::DSP::GrainScheduler scheduler;
        scheduler.prepare(48000.0);
        scheduler.setDensity(0.01f);
        REQUIRE(engine.getDensity() == scheduler.getDensity());
    }

    SECTION("PrepareConfig FFT sizes are clamped, then snapped DOWN, then re-clamped") {
        AtmosphereEngine snapped;
        AtmosphereEngine::PrepareConfig config;
        config.blurEnabled = true;
        config.freezeEnabled = true;

        // Non-power-of-two inside the range: bit_floor.
        config.blurFftSize = 3000;
        config.freezeFftSize = 3000;
        snapped.prepare(48000.0, config);
        REQUIRE(snapped.getLatencySamples() == 2048u);
        REQUIRE(snapped.getFreezeFftSize() == 2048u);

        // Below the lower bound: the clamp runs FIRST, so 100 becomes 256 and
        // there is nothing left to snap.
        config.blurFftSize = 100;
        config.freezeFftSize = 100;
        snapped.prepare(48000.0, config);
        REQUIRE(snapped.getLatencySamples() == 256u);
        REQUIRE(snapped.getFreezeFftSize() == 256u);

        // Above the upper bound: blur clamps to 4096 (already a power of two);
        // freeze clamps to 8192... but 8000 is below it, so freeze snaps to 4096.
        config.blurFftSize = 8000;
        config.freezeFftSize = 8000;
        snapped.prepare(48000.0, config);
        REQUIRE(snapped.getLatencySamples() == 4096u);
        REQUIRE(snapped.getFreezeFftSize() == 4096u);
    }

    SECTION("a freshly prepared engine is silent - the ring is empty") {
        AtmosphereEngine fresh;
        fresh.prepare(48000.0, AtmosphereEngine::PrepareConfig{});

        constexpr std::size_t kBlock = 512;
        const std::vector<float> inL(kBlock, 1.0f);
        const std::vector<float> inR(kBlock, -1.0f);
        std::vector<float> outL(kBlock, -7.0f);
        std::vector<float> outR(kBlock, -7.0f);

        fresh.processStereoBlock(inL.data(), inR.data(), outL.data(), outR.data(), kBlock);

        for (std::size_t i = 0; i < kBlock; ++i) {
            REQUIRE(outL[i] == 0.0f);
            REQUIRE(outR[i] == 0.0f);
        }
    }

    // -------------------------------------------------------------------------
    // FR-064 (deferred from T004): level = 0 is EXACT silence, and nothing that
    // leaves the engine is ever a denormal.
    // -------------------------------------------------------------------------
    SECTION("FR-064: level = 0 renders exact silence with no denormals") {
        constexpr double kSampleRate = 48000.0;

        AtmosphereEngine::PrepareConfig config;
        config.captureSeconds = 8.0f;
        config.blurEnabled = false;
        config.freezeEnabled = false;

        AtmosphereEngine live;
        live.prepare(kSampleRate, config);
        live.setDensity(8.0f);
        live.setGrainSeconds(2.0f);
        live.setPositionSeconds(0.5f);
        live.setPositionSpread(0.5f);

        NoiseRenderer noise(512, 31337u);
        renderNoise(live, noise, 3.0, kSampleRate);

        // The clause is about the TRIM, so it is worthless unless the engine is
        // loud with grains in flight first.
        const NoiseRenderStats loud = renderNoise(live, noise, 0.5, kSampleRate);
        CAPTURE(loud.rmsLeft, loud.maxActive, loud.born);
        REQUIRE(loud.born > 0u);
        REQUIRE(live.getActiveGrainCount() > 0u);
        REQUIRE(loud.rmsLeft > 1e-4);

        live.setLevel(0.0f);
        REQUIRE(live.getLevel() == 0.0f);
        // 100 ms against a 20 ms one-pole: OnePoleSmoother::process() snaps to
        // target below kCompletionThreshold (primitives/smoother.h:199-202), so
        // the trim reaches EXACTLY 0 rather than merely approaching it.
        renderNoise(live, noise, 0.1, kSampleRate);

        const NoiseRenderStats silent = renderNoise(live, noise, 0.5, kSampleRate);
        REQUIRE(silent.exactlyZero);
        REQUIRE_FALSE(silent.anyDenormal);
        // The grains are still alive and still reading the ring: this is the
        // trim closing, not an engine that stopped.
        REQUIRE(live.getActiveGrainCount() > 0u);
    }

    // -------------------------------------------------------------------------
    // FR-009 SMOOTHER CADENCE, deferred from T004 (plan S11.2, deviation D-15)
    // -------------------------------------------------------------------------
    // THE ONLY GUARD IN THE PHASE AGAINST A ONE-LINE DEFECT.
    // blurSmoother_.advanceSamples(blurHopSize_) must run ONCE PER FRAME-PAIR,
    // outside pumpBlur()'s per-channel loop. Moved inside it, the smoother
    // advances 2 * hop per hop of audio - halving the time constant - and hands
    // L and R values one hop apart within the same frame. SC-005 sweeps SETTLED
    // blur values, where the two implementations are indistinguishable, so
    // nothing else in this phase can see it.
    //
    // WHAT kBlurSmoothMs = 50 ms ACTUALLY MEANS, AND WHY THE REFERENCE IS NOT
    // 2400 SAMPLES. calculateOnePolCoefficient documents its argument as the
    // time to reach ~99 % and derives tau = smoothTimeMs / 5
    // (primitives/smoother.h:71-93: coeff = exp(-5000 / (ms * sampleRate))), so
    // a 50 ms OnePoleSmoother has a 10 ms time constant and reaches 1 - 1/e in
    // 480 samples at 48 kHz, not 2400. Writing this clause against 2400 would
    // fail a CORRECT implementation, so the reference is derived from the
    // coefficient the engine itself uses - which also makes it exact regardless
    // of constexprExp's accuracy, since both sides use the same value.
    //
    // GEOMETRY IS CHOSEN TO KEEP THE QUANTISATION INSIDE THE BUDGET. The
    // smoother only moves when a frame is drained, so the crossing can only be
    // observed at a hop boundary. blurFftSize = 256 is the minimum
    // (kMinBlurFftSize), giving the finest legal hop of 64 samples: the
    // reference 480 rounds up to 512, i.e. +6.7 %, inside the 10 % band with
    // room to spare. The D-15 defect lands at 256 samples (-46.7 %), nowhere
    // near it.
    SECTION("FR-009 cadence: the blur smoother advances once per hop of audio") {
        constexpr double kCadenceSampleRate = 48000.0;
        constexpr std::size_t kBlurFft = AtmosphereEngine::kMinBlurFftSize;  // 256
        constexpr std::size_t kHop = kBlurFft / 4;                           // 64, 75 % overlap
        constexpr std::size_t kWarmupBlocks = 64;   // 4096 samples: STFT in steady state
        constexpr std::size_t kProbeBlocks = 40;    // 2560 samples of observation
        constexpr std::size_t kExactBlock = 12;     // the closed-form probe point

        AtmosphereEngine::PrepareConfig cadenceConfig;
        cadenceConfig.captureSeconds = 1.0f;
        cadenceConfig.blurEnabled = true;
        cadenceConfig.freezeEnabled = false;
        cadenceConfig.blurFftSize = kBlurFft;
        cadenceConfig.maxBlockSamples = kHop;

        AtmosphereEngine cadence;
        cadence.prepare(kCadenceSampleRate, cadenceConfig);
        REQUIRE(cadence.getLatencySamples() == kBlurFft);
        REQUIRE(cadence.getBlur() == 0.0f);
        REQUIRE(cadence.getAppliedBlur() == 0.0f);

        // Silence in, because the cadence depends on PUSHED SAMPLE COUNTS and
        // nothing else: pumpBlur runs on every chunk whether or not a grain is
        // alive. A grain population would only add noise to the measurement.
        const std::vector<float> quietIn(kHop, 0.0f);
        std::vector<float> quietOutLeft(kHop, 0.0f);
        std::vector<float> quietOutRight(kHop, 0.0f);
        auto pumpOneHop = [&] {
            cadence.processStereoBlock(quietIn.data(), quietIn.data(), quietOutLeft.data(),
                                       quietOutRight.data(), kHop);
        };

        // Warm-up. After the first kBlurFft samples the STFT emits exactly one
        // frame per hop-sized block, so from here on "blocks" and "frames" are
        // the same count. The smoother is untouched throughout: target and
        // current are both 0, so advanceSamples() early-returns on isComplete().
        for (std::size_t b = 0; b < kWarmupBlocks; ++b) {
            pumpOneHop();
        }
        REQUIRE(cadence.getAppliedBlur() == 0.0f);

        // THE STEP.
        cadence.setBlur(1.0f);
        REQUIRE(cadence.getBlur() == 1.0f);
        REQUIRE(cadence.getAppliedBlur() == 0.0f);  // nothing has been pumped yet

        std::array<float, kProbeBlocks + 1> applied{};
        applied[0] = cadence.getAppliedBlur();
        for (std::size_t b = 1; b <= kProbeBlocks; ++b) {
            pumpOneHop();
            applied[b] = cadence.getAppliedBlur();
        }

        const auto coefficient = static_cast<double>(Krate::DSP::calculateOnePolCoefficient(
            AtmosphereEngine::kBlurSmoothMs, static_cast<float>(kCadenceSampleRate)));
        REQUIRE(coefficient > 0.0);
        REQUIRE(coefficient < 1.0);
        const double referenceSamples = -1.0 / std::log(coefficient);

        // --- Clause A: the CLOSED FORM at a fixed number of hops -------------
        // current = 1 - coeff^n after n samples of advance. The correct cadence
        // gives n = kExactBlock * kHop = 768; the D-15 defect gives 1536, i.e.
        // 0.798 against 0.959 - a 0.16 gap against a 1e-3 tolerance.
        const double exactSamples = static_cast<double>(kExactBlock * kHop);
        const double expectedApplied = 1.0 - std::pow(coefficient, exactSamples);
        const double defectApplied = 1.0 - std::pow(coefficient, 2.0 * exactSamples);
        CAPTURE(coefficient, referenceSamples, expectedApplied, defectApplied,
                applied[kExactBlock]);
        REQUIRE(std::abs(static_cast<double>(applied[kExactBlock]) - expectedApplied) < 1e-3);

        // --- Clause B: the 1 - 1/e settling time -----------------------------
        const auto threshold = static_cast<float>(1.0 - (1.0 / 2.718281828459045));
        std::size_t crossingBlock = 0;
        for (std::size_t b = 1; b <= kProbeBlocks; ++b) {
            if (applied[b] >= threshold) {
                crossingBlock = b;
                break;
            }
        }
        REQUIRE(crossingBlock > 0u);  // it must actually settle within the probe

        const auto measuredSamples = static_cast<double>(crossingBlock * kHop);
        const double relativeError =
            std::abs(measuredSamples - referenceSamples) / referenceSamples;
        CAPTURE(crossingBlock, measuredSamples, relativeError);
        REQUIRE(relativeError <= 0.10);
    }
}

// =============================================================================
// SC-002 - AtmosphereEngine_GrainLiveness
// =============================================================================
// EVERY CLAUSE CARRIES REQUIRE(getTotalGrainsBorn() > 0) BEFORE ANY OTHER
// ASSERTION. reset() seeds minObservedAge_ = captureCapacity_ and
// maxObservedAge_ = 0, so a cell in which no grain is ever admitted satisfies
// `min >= 64` and `max <= C - 2` VACUOUSLY - and cells that were structurally
// birth-free existed in this very sweep before the FR-014 admission margin was
// corrected to FR-014's +g and the birth window widened at the old end to match.
//
// Both observed-age extremes are CUMULATIVE since reset(), so one read at the
// end of a cell is exactly equivalent to "sampled every block" - the engine has
// already taken the extremum over every block for us.

TEST_CASE("AtmosphereEngine_GrainLiveness", "[atmosphere]") {
    using Krate::DSP::AtmosphereEngine;

    SECTION("the liveness invariant holds across the configuration sweep") {
        constexpr double kSampleRate = 48000.0;
        constexpr std::size_t kBlock = 512;
        constexpr std::size_t kBlocksAfterBirth = 64;  // ~0.68 s of ageing per cell

        const std::array<float, 5> grainSecondsValues{0.05f, 1.0f, 5.0f, 15.0f, 30.0f};
        const std::array<float, 5> pitchValues{-24.0f, -12.0f, 0.0f, 12.0f, 24.0f};
        const std::array<float, 3> captureValues{1.0f, 8.0f, 30.0f};
        const std::array<float, 2> driftValues{0.0f, 1.0f};
        const std::array<float, 2> decorrValues{0.0f, 1.0f};

        BlockRenderer blocks(kBlock, 0.35f, -0.2f);
        AtmosphereEngine engine;

        for (const float captureSeconds : captureValues) {
            AtmosphereEngine::PrepareConfig config;
            config.captureSeconds = captureSeconds;
            config.blurEnabled = false;
            config.freezeEnabled = false;
            engine.prepare(kSampleRate, config);

            const double capacity = static_cast<double>(engine.getCaptureCapacitySamples());
            // A birth needs available >= ceil(a0 + dR) + 2 and a0 + dR <= C - 2,
            // so the ring is certainly deep enough after C samples; 4 s of slack
            // then covers the wait for the next scheduler trigger. The TIGHTEST
            // cell - captureSeconds = 1 (C = 65536), grainSeconds = 30,
            // pitchSemitones = +24 - admits only once the ring is COMPLETELY
            // full (~1.37 s at 48 kHz), because a0 clamps into [65530, 65534].
            const std::size_t maxBlocks =
                static_cast<std::size_t>(capacity / static_cast<double>(kBlock)) +
                static_cast<std::size_t>(4.0 * kSampleRate / static_cast<double>(kBlock));

            for (const float grainSeconds : grainSecondsValues) {
                for (const float pitch : pitchValues) {
                    for (const float drift : driftValues) {
                        for (const float decorr : decorrValues) {
                            engine.reset();
                            engine.setGrainSeconds(grainSeconds);
                            engine.setPitchSemitones(pitch);
                            engine.setDriftDepth(drift);
                            // Swept freely here: the invariant must hold with
                            // the L/R read-age offset in play.
                            engine.setDecorrelation(decorr);

                            renderUntilBirth(engine, blocks, maxBlocks, kBlocksAfterBirth);

                            CAPTURE(captureSeconds, grainSeconds, pitch, drift, decorr, capacity);
                            REQUIRE(engine.getTotalGrainsBorn() > 0u);
                            REQUIRE(engine.getMinObservedGrainAgeSamples() >=
                                    static_cast<float>(AtmosphereEngine::kMinAgeSamples));
                            REQUIRE(static_cast<double>(engine.getMaxObservedGrainAgeSamples()) <=
                                    capacity - 2.0);
                        }
                    }
                }
            }
        }
    }

    SECTION("drift-free lifetimes match the closed form exactly") {
        constexpr double kSampleRate = 48000.0;
        constexpr std::size_t kBlock = 512;
        constexpr std::size_t kBlocksAfterBirth = 64;

        const std::array<float, 2> captureValues{1.0f, 8.0f};
        const std::array<float, 5> pitchValues{-24.0f, -12.0f, 0.0f, 12.0f, 24.0f};
        const std::array<float, 4> grainSecondsValues{0.05f, 1.0f, 5.0f, 30.0f};

        BlockRenderer blocks(kBlock, 0.3f, -0.3f);
        AtmosphereEngine engine;

        for (const float captureSeconds : captureValues) {
            AtmosphereEngine::PrepareConfig config;
            config.captureSeconds = captureSeconds;
            config.blurEnabled = false;
            config.freezeEnabled = false;
            engine.prepare(kSampleRate, config);

            const double capacity = static_cast<double>(engine.getCaptureCapacitySamples());
            const std::size_t maxBlocks =
                static_cast<std::size_t>(capacity / static_cast<double>(kBlock)) +
                static_cast<std::size_t>(4.0 * kSampleRate / static_cast<double>(kBlock));

            for (const float pitch : pitchValues) {
                for (const float grainSeconds : grainSecondsValues) {
                    engine.reset();
                    engine.setGrainSeconds(grainSeconds);
                    engine.setPitchSemitones(pitch);
                    engine.setPitchSpread(0.0f);
                    engine.setPositionSpread(0.0f);
                    // D-1: dR enters the headroom, so the closed form is exact
                    // ONLY at dR = 0.
                    engine.setDecorrelation(0.0f);
                    engine.setDriftDepth(0.0f);
                    // The RANGE must be zeroed too, not just the depth: the
                    // ratio envelope [rMin, rMax] is built from the range alone
                    // (plan S9.4a), so a non-zero range still widens w at depth
                    // 0 and w would no longer collapse to |1 - r|.
                    engine.setDriftRangeSemitones(0.0f);

                    renderUntilBirth(engine, blocks, maxBlocks, kBlocksAfterBirth);

                    CAPTURE(captureSeconds, grainSeconds, pitch, capacity);
                    REQUIRE(engine.getTotalGrainsBorn() > 0u);

                    // With pitchSpread = 0 and positionSpread = 0 every grain in
                    // the cell shares one s, one r and one a0, so the last-birth
                    // introspection describes them all.
                    const float expectedRatio = Krate::DSP::semitonesToRatio(pitch);
                    REQUIRE(engine.getLastBornGrainRatioAtBirth() == expectedRatio);

                    const double ratio = static_cast<double>(expectedRatio);
                    const double w = std::abs(1.0 - ratio);  // d = 0 => the sum collapses
                    const double requested =
                        std::round(static_cast<double>(grainSeconds) * kSampleRate);
                    // C - 2 - g(young, FR-025) - g(old, FR-014) - 2(D-12's
                    // reserved ceiling slack) = C - 132 at g = 64.
                    constexpr double kClosedFormOffset =
                        2.0 + static_cast<double>(AtmosphereEngine::kMinAgeSamples) +
                        static_cast<double>(AtmosphereEngine::kMinAgeSamples) + 2.0;
                    double expectedLifetime = requested;
                    if (w > 0.0 && (w * requested) > (capacity - kClosedFormOffset)) {
                        expectedLifetime = std::floor((capacity - kClosedFormOffset) / w);
                    }
                    REQUIRE(engine.getLastBornGrainLifetimeSamples() ==
                            static_cast<std::uint64_t>(expectedLifetime));

                    // Shadow model a(t) = a0 + (1 - r)*t, exact because r is
                    // constant for the grain's whole life at d = 0.
                    const double birthAge =
                        static_cast<double>(engine.getLastBornGrainBirthAgeSamples());
                    const double span = expectedLifetime - 1.0;
                    const double shadowLo = birthAge + (std::min(0.0, 1.0 - ratio) * span);
                    const double shadowHi = birthAge + (std::max(0.0, 1.0 - ratio) * span);
                    REQUIRE(static_cast<double>(engine.getMinObservedGrainAgeSamples()) >=
                            shadowLo - 2.0);
                    REQUIRE(static_cast<double>(engine.getMaxObservedGrainAgeSamples()) <=
                            shadowHi + 2.0);
                    REQUIRE(engine.getMinObservedGrainAgeSamples() >=
                            static_cast<float>(AtmosphereEngine::kMinAgeSamples));
                    REQUIRE(static_cast<double>(engine.getMaxObservedGrainAgeSamples()) <=
                            capacity - 2.0);
                }
            }
        }
    }

    SECTION("drift-on lifetimes use the SUM of the one-sided excursions") {
        constexpr double kSampleRate = 48000.0;
        constexpr std::size_t kBlock = 512;
        constexpr std::size_t kBlocksAfterBirth = 64;
        constexpr float kGrainSeconds = 30.0f;

        struct Cell {
            float captureSeconds;
            float pitch;
            float driftRange;
        };
        const std::array<Cell, 8> cells{{
            // THE STRADDLING CELL. rMin = 0.8909 < 1 < 1.1225 = rMax, so the sum
            // (0.2316) is nearly double the maximum (0.1225). It is the ONLY
            // configuration in which the two candidate definitions of w differ:
            // a sweep of non-straddling envelopes alone passes a maximum-based
            // implementation, which under-truncates by ~2x.
            {8.0f, 0.0f, 2.0f},
            {1.0f, 0.0f, 2.0f},
            {8.0f, 0.0f, 12.0f},  // rMin = 0.5, rMax = 2, w = 1.5 - the worked check
            {1.0f, 0.0f, 12.0f},
            {8.0f, -12.0f, 2.0f},
            {8.0f, 12.0f, 2.0f},
            {8.0f, -12.0f, 12.0f},
            {8.0f, 12.0f, 12.0f},
        }};

        BlockRenderer blocks(kBlock, 0.25f, -0.15f);

        for (const Cell& cell : cells) {
            AtmosphereEngine engine;
            AtmosphereEngine::PrepareConfig config;
            config.captureSeconds = cell.captureSeconds;
            config.blurEnabled = false;
            config.freezeEnabled = false;
            engine.prepare(kSampleRate, config);

            const double capacity = static_cast<double>(engine.getCaptureCapacitySamples());
            const std::size_t maxBlocks =
                static_cast<std::size_t>(capacity / static_cast<double>(kBlock)) +
                static_cast<std::size_t>(4.0 * kSampleRate / static_cast<double>(kBlock));

            engine.setGrainSeconds(kGrainSeconds);
            engine.setPitchSemitones(cell.pitch);
            engine.setPitchSpread(0.0f);
            engine.setPositionSpread(0.0f);
            engine.setDecorrelation(0.0f);
            engine.setDriftRangeSemitones(cell.driftRange);
            engine.setDriftDepth(1.0f);

            renderUntilBirth(engine, blocks, maxBlocks, kBlocksAfterBirth);

            CAPTURE(cell.captureSeconds, cell.pitch, cell.driftRange, capacity);
            REQUIRE(engine.getTotalGrainsBorn() > 0u);

            const LivenessMath math =
                livenessMath(capacity, cell.pitch, cell.driftRange, 0.0, kGrainSeconds, kSampleRate);
            REQUIRE(math.valid);
            CAPTURE(math.ratioMin, math.ratioMax, math.wUp, math.wDown, math.w, math.lifetime);
            REQUIRE(engine.getLastBornGrainLifetimeSamples() ==
                    static_cast<std::uint64_t>(math.lifetime));

            // D-12's WINDOW-NON-EMPTINESS CLAUSE, which no other clause would
            // see. Without the reserved ceiling slack the two ceils can sum to
            // headroom + 1, inverting the window; std::clamp(a0, lo, hi) with
            // lo > hi is a precondition violation - undefined behaviour that can
            // still return a plausible a0 and pass every bound-style assertion
            // on the machine it runs on.
            REQUIRE(math.ageLo <= math.ageHi);
            const double birthAge = static_cast<double>(engine.getLastBornGrainBirthAgeSamples());
            CAPTURE(birthAge, math.ageLo, math.ageHi);
            REQUIRE(birthAge >= math.ageLo);
            REQUIRE(birthAge <= math.ageHi);

            // Shadow model is FR-025's BOUND, not a closed form: the ratio moves
            // inside [rMin, rMax] for the grain's whole life.
            const double span = math.lifetime - 1.0;
            const double shadowLo = birthAge - (math.wUp * span);
            const double shadowHi = birthAge + (math.wDown * span);
            REQUIRE(static_cast<double>(engine.getMinObservedGrainAgeSamples()) >= shadowLo - 2.0);
            REQUIRE(static_cast<double>(engine.getMaxObservedGrainAgeSamples()) <= shadowHi + 2.0);
            REQUIRE(engine.getMinObservedGrainAgeSamples() >=
                    static_cast<float>(AtmosphereEngine::kMinAgeSamples));
            REQUIRE(static_cast<double>(engine.getMaxObservedGrainAgeSamples()) <= capacity - 2.0);

            // The worked check from the plan, spelled out so a silent change of
            // definition cannot hide behind the generic shadow model above.
            //
            // tasks.md:441 works this as floor(524220/1.5) = 349480 from a
            // headroom of C - 2 - g - 2. The headroom now also carries FR-014's
            // OLD-side g (see AtmosphereEngine::tryBirthGrain()), so the divisor
            // input is C - 2 - 2g - 2 = 524156 and the figure is
            // floor(524156/1.5) = 349437 - 43 samples, 0.9 ms at 48 kHz, out of
            // a 7.28 s grain. It is spelled out arithmetically rather than as a
            // literal so the two guards remain visible.
            if (cell.captureSeconds == 8.0f && cell.pitch == 0.0f && cell.driftRange == 12.0f) {
                constexpr double kG = static_cast<double>(AtmosphereEngine::kMinAgeSamples);
                REQUIRE(engine.getCaptureCapacitySamples() == 524288u);
                REQUIRE(math.w == Catch::Approx(1.5).epsilon(1e-9));
                REQUIRE(engine.getLastBornGrainLifetimeSamples() ==
                        static_cast<std::uint64_t>(std::floor((524288.0 - 2.0 - kG - kG - 2.0) / 1.5)));
                REQUIRE(engine.getLastBornGrainLifetimeSamples() == 349437u);
            }
            // The straddling cell: assert the SUM, and assert it differs from
            // the maximum, so the two definitions cannot coincide by accident.
            if (cell.captureSeconds == 8.0f && cell.pitch == 0.0f && cell.driftRange == 2.0f) {
                REQUIRE(math.wUp > 0.0);
                REQUIRE(math.wDown > 0.0);
                REQUIRE(math.w > std::max(math.wUp, math.wDown) * 1.5);
            }
        }
    }

    SECTION("pitch settings are snapshotted at birth, never read live") {
        constexpr double kSampleRate = 48000.0;
        constexpr std::size_t kBlock = 512;
        constexpr float kGrainSeconds = 30.0f;

        AtmosphereEngine::PrepareConfig config;
        config.captureSeconds = 30.0f;
        config.blurEnabled = false;
        config.freezeEnabled = false;

        AtmosphereEngine engine;
        engine.prepare(kSampleRate, config);
        const double capacity = static_cast<double>(engine.getCaptureCapacitySamples());

        engine.setGrainSeconds(kGrainSeconds);
        // ~1 birth per 10 s, so the in-flight window is long enough to observe
        // before a NEWER grain overwrites the last-birth introspection.
        engine.setDensity(0.1f);
        engine.setPitchSemitones(0.0f);
        engine.setPitchSpread(0.0f);
        engine.setPositionSpread(0.0f);
        engine.setDecorrelation(0.0f);
        engine.setDriftRangeSemitones(2.0f);
        engine.setDriftDepth(1.0f);

        BlockRenderer blocks(kBlock, 0.2f, -0.2f);

        const auto blocksFor = [&](double seconds) {
            return static_cast<std::size_t>(seconds * kSampleRate / static_cast<double>(kBlock));
        };

        // --- Birth the long grain.
        for (std::size_t b = 0; b < blocksFor(25.0) && engine.getTotalGrainsBorn() == 0; ++b) {
            blocks.render(engine);
        }
        REQUIRE(engine.getTotalGrainsBorn() == 1u);

        const std::uint64_t lifetimeAtBirth = engine.getLastBornGrainLifetimeSamples();
        const double birthAge = static_cast<double>(engine.getLastBornGrainBirthAgeSamples());
        // The lane's walk state is zeroed at birth without re-seeding its
        // stream, so the grain starts at EXACTLY its snapshotted static pitch.
        REQUIRE(engine.getLastBornGrainRatioAtBirth() == 1.0f);

        const LivenessMath atBirth =
            livenessMath(capacity, 0.0f, 2.0f, 0.0, kGrainSeconds, kSampleRate);
        REQUIRE(atBirth.valid);
        REQUIRE(lifetimeAtBirth == static_cast<std::uint64_t>(atBirth.lifetime));

        // --- Widen the pitch envelope WHILE the grain is in flight.
        engine.setPitchSemitones(24.0f);
        engine.setDriftRangeSemitones(12.0f);

        for (std::size_t b = 0; b < blocksFor(5.0); ++b) {
            blocks.render(engine);
        }

        // Nothing about the in-flight grain moved.
        REQUIRE(engine.getTotalGrainsBorn() == 1u);
        REQUIRE(engine.getLastBornGrainLifetimeSamples() == lifetimeAtBirth);

        // Its ages stay inside the envelope computed from the values in force AT
        // ITS BIRTH - not the widened ones. Checked HERE and not at the end of
        // the section, because the second grain born below has a completely
        // different (and much older) birth age, which folds into the same
        // cumulative extremes.
        const double span = atBirth.lifetime - 1.0;
        CAPTURE(birthAge, atBirth.wUp, atBirth.wDown, span);
        REQUIRE(static_cast<double>(engine.getMinObservedGrainAgeSamples()) >=
                birthAge - (atBirth.wUp * span) - 2.0);
        REQUIRE(static_cast<double>(engine.getMaxObservedGrainAgeSamples()) <=
                birthAge + (atBirth.wDown * span) + 2.0);

        // --- The widened settings appear only in the NEXT grain born. At +24
        //     semitones with a 12-semitone range the birth window climbs to the
        //     very top of the ring, so this waits for the ring to fill.
        for (std::size_t b = 0; b < blocksFor(120.0) && engine.getTotalGrainsBorn() < 2; ++b) {
            blocks.render(engine);
        }
        REQUIRE(engine.getTotalGrainsBorn() == 2u);
        // s = 24, [semisLo, semisHi] = [12, 36], lane zeroed => r = 2^2 = 4.
        REQUIRE(engine.getLastBornGrainRatioAtBirth() == Catch::Approx(4.0).epsilon(1e-6));
        REQUIRE(engine.getLastBornGrainLifetimeSamples() != lifetimeAtBirth);
    }

    SECTION("drift-lane equivalence") {
        // The LAST slot, so an off-by-one in the kDriftSaltBase + i derivation
        // (or a bank that only steps its live lanes) cannot pass by accident.
        constexpr std::size_t kSlot = AtmosphereEngine::kMaxGrains - 1;  // 63
        constexpr std::uint32_t kSeed = 1234u;
        constexpr std::size_t kChunk = AtmosphereEngine::kControlChunkSamples;  // 64
        constexpr std::size_t kChunks = 600;  // >= the 200 the gate requires
        constexpr float kSmoothness = 0.7f;

        AtmosphereEngine engine;
        engine.prepare(48000.0, AtmosphereEngine::PrepareConfig{});
        engine.setSeed(kSeed);
        engine.setDriftSmoothness(kSmoothness);
        engine.setDriftDepth(1.0f);
        // A low density and a short grain keep the pool cold - see the window
        // precondition asserted at the end of this section.
        engine.setDensity(1.0f);
        engine.setGrainSeconds(0.2f);

        // The reference is a REAL BrownianDrift on the same derived stream,
        // driven with the SAME chunk schedule the engine uses: one
        // processBlock(kControlChunkSamples) per engine control step. Anything
        // else - "once per block" by numSamples, say - lands the two on
        // different points of the same walk.
        Krate::DSP::BrownianDrift reference;
        reference.prepare(48000.0);
        reference.setSeed(
            Krate::DSP::deriveStreamSeed(kSeed, AtmosphereEngine::kDriftSaltBase + kSlot));
        reference.setSmoothness(kSmoothness);
        reference.setDepth(1.0f);
        // setSeed() only stores + seeds; initState() (which zeroes the walk and
        // snaps the 150 ms output smoother) runs in prepare() and reset()
        // (brownian_drift.h:133-135 -> :243). prepare() ran BEFORE the seed was
        // configured, so this reset() is what puts the reference in the same
        // post-reset state the engine's resetDriftLanes() leaves lane 63 in.
        reference.reset();

        const std::vector<float> inL(kChunk, 0.25f);
        const std::vector<float> inR(kChunk, -0.25f);
        std::vector<float> outL(kChunk, 0.0f);
        std::vector<float> outR(kChunk, 0.0f);

        float maxDiff = 0.0f;
        std::size_t worstChunk = 0;
        float maxAbsReference = 0.0f;

        for (std::size_t chunk = 0; chunk < kChunks; ++chunk) {
            engine.processStereoBlock(inL.data(), inR.data(), outL.data(), outR.data(), kChunk);
            reference.processBlock(kChunk);

            const float expected = reference.getCurrentValue();
            const float actual = engine.getDriftLaneValue(kSlot);
            const float diff = std::abs(actual - expected);
            if (diff > maxDiff) {
                maxDiff = diff;
                worstChunk = chunk;
            }
            maxAbsReference = std::max(maxAbsReference, std::abs(expected));
        }

        // WINDOW PRECONDITION (FR-030). A grain birth ZEROES its lane's walk
        // state without re-seeding the stream, whereas BrownianDrift::reset()
        // re-seeds (brownian_drift.h:133-135 -> :243). Any birth on slot 63
        // therefore desynchronises the two permanently, and the equivalence
        // above would be measuring the wrong thing. FR-020's round-robin only
        // reaches slot 63 after kMaxGrains - 1 births.
        REQUIRE(engine.getTotalGrainsBorn() <
                static_cast<std::uint64_t>(AtmosphereEngine::kMaxGrains - 1));

        // Guard against a vacuous pass: two lanes stuck at 0 also agree to 1e-6.
        // At smoothness 0.7 the OU step gain is g = 0.5*sqrt(1-a^2) ~= 4e-3, so
        // over the 1200 OU steps this window performs the walk reaches ~0.14
        // RMS - orders of magnitude above this floor.
        CAPTURE(maxAbsReference);
        REQUIRE(maxAbsReference > 0.001f);

        CAPTURE(worstChunk);
        CAPTURE(maxDiff);
        REQUIRE(maxDiff <= 1e-6f);
    }
}

// =============================================================================
// T006 - FR-020, FR-022, FR-023: skip, never steal
// =============================================================================

TEST_CASE("AtmosphereEngine_SkipNeverSteal", "[atmosphere]") {
    using Krate::DSP::AtmosphereEngine;
    constexpr double kSampleRate = 48000.0;

    SECTION("the pool saturates at kMaxGrains and no grain is ever stolen") {
        constexpr std::size_t kBlock = 512;
        constexpr std::size_t kBlocks = 940;  // ~10 s at 48 kHz

        AtmosphereEngine::PrepareConfig config;
        config.captureSeconds = 30.0f;
        config.blurEnabled = false;
        config.freezeEnabled = false;

        AtmosphereEngine engine;
        engine.prepare(kSampleRate, config);
        // 20 grains/s x 30 s = 600 concurrent requested against a pool of 64:
        // deliberately far outside the documented
        // `density * grainSeconds <= kMaxGrains` operating region.
        engine.setDensity(20.0f);
        engine.setGrainSeconds(30.0f);

        BlockRenderer blocks(kBlock, 0.3f, -0.3f);

        bool countBounded = true;
        bool identityHeld = true;
        for (std::size_t b = 0; b < kBlocks; ++b) {
            blocks.render(engine);

            if (engine.getActiveGrainCount() > AtmosphereEngine::kMaxGrains) {
                countBounded = false;
            }
            // `retired` is counted INDEPENDENTLY at the swap-remove site, never
            // derived as (born - active). Derived, this identity is a tautology
            // that a STEALING implementation also satisfies, so it could not
            // detect FR-023 failing.
            if (engine.getTotalGrainsRetired() +
                    static_cast<std::uint64_t>(engine.getActiveGrainCount()) !=
                engine.getTotalGrainsBorn()) {
                identityHeld = false;
            }
        }

        REQUIRE(engine.getTotalGrainsBorn() > 0u);
        REQUIRE(countBounded);
        REQUIRE(identityHeld);
        REQUIRE(engine.getActiveGrainCount() == AtmosphereEngine::kMaxGrains);
        REQUIRE(engine.getSkippedTriggerCountPoolFull() > 0u);
        // 30 s grains inside a 10 s render: nothing has retired yet, so the
        // pool-full skip is the ONLY thing that can have capped the population.
        REQUIRE(engine.getTotalGrainsRetired() == 0u);
    }

    SECTION("slots are allocated round-robin, not first-free") {
        // 64-sample blocks: shorter than the smallest possible interonset
        // (20 grains/s at +/-25 % jitter is 1800 samples), so a birth can never
        // be missed by sampling the birth counter once per block.
        constexpr std::size_t kBlock = 64;
        constexpr std::size_t kMaxBlocks = 9000;  // ~12 s at 48 kHz
        constexpr std::size_t kBirthsToWatch = 2 * AtmosphereEngine::kMaxGrains;  // 128

        AtmosphereEngine::PrepareConfig config;
        config.captureSeconds = 8.0f;
        config.blurEnabled = false;
        config.freezeEnabled = false;

        AtmosphereEngine engine;
        engine.prepare(kSampleRate, config);
        engine.setDensity(20.0f);
        engine.setGrainSeconds(1.0f);  // ~20 concurrent, so slots keep freeing up
        engine.setPositionSpread(0.0f);

        BlockRenderer blocks(kBlock, 0.3f, -0.3f);

        std::array<bool, AtmosphereEngine::kMaxGrains> slotSeen{};
        std::uint64_t previousBorn = 0;
        std::size_t birthsWatched = 0;
        bool maskAgrees = true;

        for (std::size_t b = 0; b < kMaxBlocks && birthsWatched < kBirthsToWatch; ++b) {
            blocks.render(engine);

            const std::uint64_t born = engine.getTotalGrainsBorn();
            REQUIRE(born - previousBorn <= 1u);
            if (born > previousBorn) {
                const std::size_t slot = engine.getLastBornGrainSlot();
                REQUIRE(slot < AtmosphereEngine::kMaxGrains);
                slotSeen[slot] = true;
                ++birthsWatched;
                previousBorn = born;
            }

            if (static_cast<std::size_t>(std::popcount(engine.getActiveSlotMask())) !=
                engine.getActiveGrainCount()) {
                maskAgrees = false;
            }
        }

        REQUIRE(birthsWatched == kBirthsToWatch);
        REQUIRE(maskAgrees);
        // A first-free allocator concentrates every birth on the low
        // `density * grainSeconds` slots - ~20 of 64 here - so the upper lanes
        // never see a grain and this loop fails on the first of them.
        for (std::size_t slot = 0; slot < AtmosphereEngine::kMaxGrains; ++slot) {
            CAPTURE(slot);
            REQUIRE(slotSeen[slot]);
        }
    }
}

// =============================================================================
// T006 - FR-010 ... FR-014 (capture, cold ring) and FR-062 (no dry path)
// =============================================================================

TEST_CASE("AtmosphereEngine_CaptureAndColdRing", "[atmosphere]") {
    using Krate::DSP::AtmosphereEngine;
    constexpr double kSampleRate = 48000.0;

    SECTION("no grain is born before the ring holds ceil(a0 + dR) + g samples") {
        // Every source of birth-time randomness is switched off, so a0 is
        // exactly positionSeconds * sampleRate and the admission threshold is a
        // single known integer.
        constexpr std::size_t kBlock = 480;
        constexpr std::size_t kColdBlocks = 100;  // exactly 48 000 samples

        AtmosphereEngine::PrepareConfig config;
        config.captureSeconds = 8.0f;
        config.blurEnabled = false;
        config.freezeEnabled = false;

        AtmosphereEngine engine;
        engine.prepare(kSampleRate, config);
        engine.setPositionSeconds(1.0f);  // a0 = 48 000 samples
        engine.setPositionSpread(0.0f);
        engine.setPitchSemitones(0.0f);
        engine.setPitchSpread(0.0f);
        engine.setDriftRangeSemitones(0.0f);
        engine.setDriftDepth(0.0f);
        engine.setDecorrelation(0.0f);  // dR = 0
        engine.setGrainSeconds(1.0f);
        engine.setDensity(20.0f);  // a trigger every ~2400 samples

        BlockRenderer blocks(kBlock, 0.5f, -0.5f);
        for (std::size_t b = 0; b < kColdBlocks; ++b) {
            blocks.render(engine);
        }

        // available = 48 000 < ceil(a0 + dR) + kMinAgeSamples = 48 064 (FR-014).
        REQUIRE(engine.getTotalGrainsBorn() == 0u);
        REQUIRE(engine.getSkippedTriggerCountRingCold() > 0u);
        REQUIRE(engine.getSkippedTriggerCountPoolFull() == 0u);

        // 4800 more samples: at least one trigger past the threshold. THE MARGIN
        // IS +g = +64, which is FR-014 verbatim. It is reachable only because
        // tryBirthGrain() also spends g on the OLD end of the birth window; see
        // the headroom there.
        for (std::size_t b = 0; b < 10; ++b) {
            blocks.render(engine);
        }
        REQUIRE(engine.getTotalGrainsBorn() > 0u);
        REQUIRE(engine.getLastBornGrainBirthAgeSamples() == 48000.0f);
        REQUIRE(engine.getSkippedTriggerCountPoolFull() == 0u);
    }

    SECTION("a grain reads audio written in the same block (self-granulation)") {
        constexpr std::size_t kBlock = 512;
        constexpr std::size_t kWarmBlocks = 40;  // 20 480 samples of silence

        AtmosphereEngine::PrepareConfig config;
        config.captureSeconds = 8.0f;
        config.blurEnabled = false;
        config.freezeEnabled = false;

        AtmosphereEngine engine;
        engine.prepare(kSampleRate, config);
        // positionSeconds ~1 ms gives a0 = 48 samples, which FR-025's window
        // clamps UP to kMinAgeSamples = 64. With w = 0 the ratio is exactly 1,
        // so the age stays at exactly 64 for the grain's whole life: the read
        // point is a fixed 64 samples behind the write head.
        engine.setPositionSeconds(0.001f);
        engine.setPositionSpread(0.0f);
        engine.setPitchSemitones(0.0f);
        engine.setPitchSpread(0.0f);
        engine.setDriftRangeSemitones(0.0f);
        engine.setDriftDepth(0.0f);
        engine.setDecorrelation(0.0f);
        engine.setPanSpread(0.0f);
        engine.setGrainSeconds(1.0f);
        engine.setDensity(20.0f);

        BlockRenderer silence(kBlock, 0.0f, 0.0f);
        bool silentThroughout = true;
        for (std::size_t b = 0; b < kWarmBlocks; ++b) {
            silence.render(engine);
            if (!silence.outputIsExactlyZero()) {
                silentThroughout = false;
            }
        }
        REQUIRE(silentThroughout);  // silence in, silence out: the ring holds only zeros
        REQUIRE(engine.getTotalGrainsBorn() > 0u);
        REQUIRE(engine.getActiveGrainCount() > 0u);
        REQUIRE(engine.getLastBornGrainBirthAgeSamples() ==
                static_cast<float>(AtmosphereEngine::kMinAgeSamples));

        BlockRenderer step(kBlock, 1.0f, 1.0f);
        step.render(engine);

        // The first 64 output samples of the step block read the PREVIOUS
        // block, which was silence...
        bool leadingSilence = true;
        for (std::size_t i = 0; i < AtmosphereEngine::kMinAgeSamples; ++i) {
            if (step.outLeft[i] != 0.0f || step.outRight[i] != 0.0f) {
                leadingSilence = false;
            }
        }
        REQUIRE(leadingSilence);
        // ...and from sample 64 onwards the live grains read audio written
        // EARLIER IN THIS SAME BLOCK. That is the self-granulation FR-012 asks
        // for: the capture write precedes every ring read for that sample.
        REQUIRE(step.outLeft[AtmosphereEngine::kMinAgeSamples] > 0.0f);
        REQUIRE(step.outRight[AtmosphereEngine::kMinAgeSamples] > 0.0f);
    }

    SECTION("no input reaches the output except through a grain (FR-062)") {
        constexpr std::size_t kBlock = 512;

        AtmosphereEngine::PrepareConfig config;
        config.captureSeconds = 8.0f;
        config.blurEnabled = false;
        config.freezeEnabled = false;

        AtmosphereEngine engine;
        engine.prepare(kSampleRate, config);
        // a0 clamps to the OLDEST legal age (C - 2), so no grain can be admitted
        // until the ring is completely full - 10.9 s at this geometry - and a
        // 0.1 grains/s trigger rate pushes the first birth further still.
        engine.setPositionSeconds(30.0f);
        engine.setPositionSpread(0.0f);
        engine.setPitchSemitones(0.0f);
        engine.setPitchSpread(0.0f);
        engine.setDriftRangeSemitones(0.0f);
        engine.setDriftDepth(0.0f);
        engine.setDecorrelation(0.0f);
        engine.setGrainSeconds(1.0f);
        engine.setDensity(0.1f);

        // FULL-SCALE input for 2 s. Any dry leak, however small, is a non-zero
        // sample - this is an exact-zero assertion, not a threshold.
        BlockRenderer blocks(kBlock, 1.0f, -1.0f);
        const auto blocksFor = [&](double seconds) {
            return static_cast<std::size_t>(seconds * kSampleRate / static_cast<double>(kBlock));
        };

        bool exactlyZero = true;
        for (std::size_t b = 0; b < blocksFor(2.0); ++b) {
            blocks.render(engine);
            if (!blocks.outputIsExactlyZero()) {
                exactlyZero = false;
            }
        }
        REQUIRE(exactlyZero);
        REQUIRE(engine.getTotalGrainsBorn() == 0u);

        // Not vacuous: the very same configuration DOES eventually produce
        // grains, so the silence above is the absence of a dry path rather than
        // an engine that never runs.
        for (std::size_t b = 0; b < blocksFor(30.0) && engine.getTotalGrainsBorn() == 0; ++b) {
            blocks.render(engine);
        }
        REQUIRE(engine.getTotalGrainsBorn() > 0u);
    }

    // The companion to the clause above. That one proves no DRY path reaches the
    // output; this one proves no WET path bypasses the FR-061 level trim: with
    // level = 0 and grains demonstrably live and reading a hot ring, every
    // output sample is exactly 0.0f.
    SECTION("level = 0 silences the wet path exactly - nothing bypasses the trim") {
        constexpr std::size_t kBlock = 512;

        AtmosphereEngine::PrepareConfig config;
        config.captureSeconds = 8.0f;
        config.blurEnabled = false;
        config.freezeEnabled = false;

        AtmosphereEngine engine;
        engine.prepare(kSampleRate, config);
        engine.setPositionSeconds(0.25f);
        engine.setPositionSpread(0.0f);
        engine.setGrainSeconds(2.0f);
        engine.setDensity(8.0f);

        // FULL-SCALE DC, so every live grain reads a large value out of the ring
        // and any leak past the trim would be a large sample, not a small one.
        BlockRenderer blocks(kBlock, 0.75f, -0.75f);
        const auto blocksFor = [&](double seconds) {
            return static_cast<std::size_t>(seconds * kSampleRate / static_cast<double>(kBlock));
        };

        for (std::size_t b = 0; b < blocksFor(2.0); ++b) {
            blocks.render(engine);
        }
        REQUIRE(engine.getTotalGrainsBorn() > 0u);
        REQUIRE(engine.getActiveGrainCount() > 0u);
        REQUIRE_FALSE(blocks.outputIsExactlyZero());  // the wet path IS producing audio

        engine.setLevel(0.0f);
        for (std::size_t b = 0; b < blocksFor(0.1); ++b) {  // 100 ms against 20 ms
            blocks.render(engine);
        }

        bool exactlyZero = true;
        for (std::size_t b = 0; b < blocksFor(1.0); ++b) {
            blocks.render(engine);
            if (!blocks.outputIsExactlyZero()) {
                exactlyZero = false;
            }
        }
        REQUIRE(exactlyZero);
        REQUIRE(engine.getActiveGrainCount() > 0u);
    }
}

// =============================================================================
// T007 helpers - one grain, rendered in complete isolation, sample-exactly
// =============================================================================
// The assertions below are about the ENVELOPE, so every other source of
// variation is switched off and the grain's output is made to BE the envelope:
//
//   - the input is DC 1.0 on both channels, so every interpolated ring read
//     returns exactly 1.0 (a + frac*(b - a) with a == b == 1);
//   - panSpread = 0 puts both pan gains at cos/sin(pi/4) = 0.70710678
//     (tryBirthGrain()'s step (f), the GrainProcessor pan law), a constant
//     factor;
//   - decorrelation = 0, so the R channel does not read a second point;
//   - pitchSpread / positionSpread / driftDepth / driftRangeSemitones = 0, so
//     r is constant for the grain's whole life and no birth draw affects it;
//   - blur and freeze are DISABLED in PrepareConfig, so nothing downstream of
//     the grain bus can smear a zero into a non-zero.
//
// Hence outLeft[n] == envelope(phase(n)) * 0.70710678 exactly, and an
// "output sample is 0" assertion is an "envelope entry is 0" assertion.

namespace {

/// One grain's L-channel output, captured with sample-exact alignment.
struct IsolatedGrain {
    std::vector<float> samples;        ///< exactly L' entries: ages 0 .. L'-1
    float previousSample = 0.0f;       ///< the output sample immediately BEFORE the birth
    float maxAbs = 0.0f;               ///< max |y| over the grain
    std::uint64_t lifetime = 0;        ///< getLastBornGrainLifetimeSamples()
    std::uint64_t bornDuringWarmup = 0;
    std::uint64_t born = 0;
    bool retired = false;
};

/// @brief Render until exactly one grain has been born, aged and retired.
///
/// The first `warmupSamples` are rendered in 512-sample blocks (cheap); from
/// there the render is ONE SAMPLE PER CALL, which is what makes the alignment
/// exact: renderGrainChunk() calls tryBirthGrain() BEFORE the accumulation for
/// the same sample, so the call on which getTotalGrainsBorn() first becomes 1
/// is precisely the grain's age-0 sample. Symmetrically, retirement is the
/// INTEGER compare `++grain.ageSamples >= grain.lifetime` at the bottom of the
/// same loop, so the call on which getTotalGrainsRetired() first becomes 1 is
/// the grain's age-(L'-1) sample. Block partitioning does not change the render
/// (processStereoBlock partitions on an ABSOLUTE control grid, and SC-011
/// measures exactly that), so the two phases are equivalent.
[[nodiscard]] IsolatedGrain renderIsolatedGrain(Krate::DSP::AtmosphereEngine& engine,
                                                std::size_t warmupSamples,
                                                std::size_t maxSamples) {
    constexpr std::size_t kBulkBlock = 512;
    IsolatedGrain grain;

    // GrainScheduler reloads interonsetSamples_ = sampleRate / density
    // (processors/grain_scheduler.h:100-103) and its very first process() call
    // always triggers (:74-76) onto a cold ring, so at density 0.1 the first
    // ACCEPTED birth cannot happen before sample sampleRate / 0.1. The caller
    // passes a warm-up strictly below that; bornDuringWarmup turns that into an
    // assertion rather than an assumption.
    BlockRenderer bulk(kBulkBlock, 1.0f, 1.0f);
    for (std::size_t b = 0; b < warmupSamples / kBulkBlock; ++b) {
        bulk.render(engine);
    }
    grain.bornDuringWarmup = engine.getTotalGrainsBorn();

    const float inLeft = 1.0f;
    const float inRight = 1.0f;
    float outLeft = 0.0f;
    float outRight = 0.0f;
    bool started = false;
    for (std::size_t i = 0; i < maxSamples && !grain.retired; ++i) {
        const float previous = outLeft;
        engine.processStereoBlock(&inLeft, &inRight, &outLeft, &outRight, 1);
        if (!started) {
            if (engine.getTotalGrainsBorn() == 0u) {
                continue;
            }
            started = true;
            grain.previousSample = previous;
        }
        grain.samples.push_back(outLeft);
        grain.maxAbs = std::max(grain.maxAbs, std::abs(outLeft));
        grain.retired = engine.getTotalGrainsRetired() > 0u;
    }

    grain.lifetime = engine.getLastBornGrainLifetimeSamples();
    grain.born = engine.getTotalGrainsBorn();
    return grain;
}

/// Everything that is not the envelope, switched off. See the block comment
/// above for why each line is here.
void configureIsolatedGrain(Krate::DSP::AtmosphereEngine& engine,
                            Krate::DSP::GrainEnvelopeType type, float grainSeconds,
                            float pitchSemitones) {
    engine.setGrainEnvelope(type);
    engine.setGrainSeconds(grainSeconds);
    engine.setDensity(0.1f);  // kMinDensity: 10 s between triggers at 48 kHz
    engine.setJitter(0.0f);
    engine.setPositionSeconds(0.0f);  // clamps UP to the FR-025 window's floor
    engine.setPositionSpread(0.0f);
    engine.setPitchSemitones(pitchSemitones);
    engine.setPitchSpread(0.0f);
    engine.setDriftDepth(0.0f);
    engine.setDriftRangeSemitones(0.0f);
    engine.setPanSpread(0.0f);
    engine.setDecorrelation(0.0f);
    engine.setBlur(0.0f);
    engine.setFreezeMix(0.0f);
    engine.setLevel(1.0f);
}

}  // namespace

// =============================================================================
// FR-027 / plan S9.6 (D-4b, D-13): forced endpoints + the L'-1 denominator
// =============================================================================
// TWO CHANGES, NEITHER SUFFICIENT ALONE, ARE UNDER TEST:
//
//   1. regenerateEnvelope() forces envelopeTable_[0] and a TAIL RUN of
//      kEnvelopeTailZeroEntries = 2 entries to 0 after every
//      GrainEnvelope::generate(). Five of the six shipped types already end at
//      exactly 0, but Exponential's release is exp(-t*4)
//      (core/grain_envelope.h:144-150) and ends at ~0.0183.
//   2. The envelope phase denominator is L' - 1, so the LAST emitted sample has
//      phase EXACTLY 1.0, indexFloat = 4095, and the lookup returns the forced
//      tail entry. Under the rejected 1/L' denominator the maximum phase is
//      (L'-1)/L' < 1 and table[4095] is NEVER READ, so forcing it fixes nothing.
//
// WHAT EACH CLAUSE CATCHES (measured against the real tables, 48 kHz):
//
//   * grainSeconds = 0.05 gives L' = 2400 and a table-index step of
//     Delta = 4095/2399 = 1.7070 entries per sample. Under the 1/L' denominator
//     the final lookup lands at index0 = 4093, frac = 0.293, so Exponential's
//     last emitted sample is 0.018855 * 0.707 = 0.01334 of full envelope scale.
//     The "last sample is 0" clause is therefore the discriminator against the
//     denominator bug, and 0.05 s is the cell that discriminates.
//   * grainSeconds = 30 with pitchSemitones = +24 truncates (FR-025) to
//     L' = 21822, so Delta = 4095/21821 = 0.1877 <= 1 and the forced tail RUN
//     makes the last TWO samples exactly 0. With a one-entry run instead,
//     Exponential's second-to-last sample would be 0.188 * 0.018668 = 0.00351.
//     That clause is therefore the discriminator against
//     kEnvelopeTailZeroEntries = 1.
//
// WHY THE FIRST TWO SAMPLES ARE NOT BOTH ASSERTED TO BE ZERO (deviation from
// the task's literal wording, recorded here rather than silently dropped):
// only table[0] is forced at the head, and GrainEnvelope::lookup maps age 1 to
// indexFloat = Delta (core/grain_envelope.h:175). At 0.05 s, Delta = 1.707 lands
// BETWEEN table[1] and table[2], so the second sample is 0.01686 for
// Exponential and 3.7e-6 for Hann - neither within 1e-6, on a CORRECT
// implementation. At grainSeconds = 30 it lands at 0.1877 * table[1], which is
// 0.00186 for Exponential. Making it zero would need a forced HEAD RUN of
// ceil(Delta) + 1 = 3 entries, i.e. a truncated attack, which plan S9.6 does
// not specify and FR-027 does not ask for. The head is therefore pinned at its
// real guarantee - the FIRST sample is exactly 0 - and the onset is covered by
// the same terminal-step bound applied at the tail.
TEST_CASE("AtmosphereEngine_EnvelopeEndpointsForced", "[atmosphere]") {
    using Krate::DSP::AtmosphereEngine;
    using Krate::DSP::GrainEnvelopeType;

    constexpr double kSampleRate = 48000.0;
    constexpr float kZeroTolerance = 1e-6f;
    // Plan S9.6 derives ~0.010 of one grain's amplitude for the worst
    // (Exponential) terminal step; the measured worst cell here is
    // grainSeconds = 0.05, where it is 0.01334. 0.02 is that with margin.
    constexpr float kTerminalStepFraction = 0.02f;
    constexpr std::size_t kTerminalWindow = 8;
    // The first ACCEPTED trigger is at sample 480 000 (= 48 000 / 0.1), because
    // the trigger at sample 0 always lands on a cold ring. The bulk phase stops
    // well short of it and renderIsolatedGrain turns that into an assertion.
    constexpr std::size_t kWarmupSamples = 400000;
    // 80 128 samples to the birth, then at most L' = 21 822 more.
    constexpr std::size_t kMaxSingleSamples = 200000;

    struct EnvelopeCase {
        GrainEnvelopeType type;
        const char* name;
    };
    const std::array<EnvelopeCase, 6> envelopes{{
        {GrainEnvelopeType::Hann, "Hann"},
        {GrainEnvelopeType::Trapezoid, "Trapezoid"},
        {GrainEnvelopeType::Sine, "Sine"},
        {GrainEnvelopeType::Blackman, "Blackman"},
        {GrainEnvelopeType::Linear, "Linear"},
        // The type that does NOT end at 0 by itself, and the only one whose
        // failure would be audible rather than merely non-zero.
        {GrainEnvelopeType::Exponential, "Exponential"},
    }};

    struct LengthCase {
        float grainSeconds;
        float pitchSemitones;
        const char* name;
    };
    const std::array<LengthCase, 2> lengths{{
        // Delta = 1.707 entries/sample: only the LAST sample can be forced to 0.
        {0.05f, 0.0f, "grainSeconds=0.05 (L'=2400, Delta=1.707)"},
        // r = 4 exactly, so w = 3 and FR-025 truncates 30 s to
        // floor((65536 - 2 - 64 - 2)/3) = 21822 samples - which also puts the
        // whole grain between two 10 s triggers, so it stays isolated.
        // Delta = 0.188 entries/sample: the forced tail RUN is observable.
        {30.0f, 24.0f, "grainSeconds=30 (truncated to L'=21822, Delta=0.188)"},
    }};

    AtmosphereEngine::PrepareConfig config;
    config.captureSeconds = 1.0f;  // C = 65536 at 48 kHz
    config.blurEnabled = false;
    config.freezeEnabled = false;
    config.maxBlockSamples = 512;

    for (const auto& length : lengths) {
        for (const auto& envelope : envelopes) {
            AtmosphereEngine engine;
            engine.prepare(kSampleRate, config);
            configureIsolatedGrain(engine, envelope.type, length.grainSeconds,
                                   length.pitchSemitones);

            const IsolatedGrain grain =
                renderIsolatedGrain(engine, kWarmupSamples, kMaxSingleSamples);

            INFO("envelope = " << envelope.name << ", length = " << length.name);
            CAPTURE(grain.lifetime, grain.samples.size(), grain.maxAbs, grain.born,
                    grain.bornDuringWarmup);

            // --- The render really is ONE isolated grain, aligned exactly.
            REQUIRE(grain.bornDuringWarmup == 0u);
            REQUIRE(grain.born == 1u);
            REQUIRE(grain.retired);
            REQUIRE(grain.lifetime >= 2u);  // FR-026: L' - 1 is a denominator
            REQUIRE(grain.samples.size() == static_cast<std::size_t>(grain.lifetime));
            REQUIRE(grain.samples.size() > kTerminalWindow);
            REQUIRE(grain.previousSample == 0.0f);  // nothing was alive before the birth
            // Not vacuous: the envelope peaks at 1 and the pan gain is
            // cos(pi/4) = 0.7071, so a silent grain would fail here first.
            REQUIRE(grain.maxAbs > 0.5f);

            // --- FR-027, head: age 0 maps to phase 0 maps to table[0], forced.
            REQUIRE(grain.samples.front() == 0.0f);

            // --- FR-027 + D-4b, tail: age L'-1 maps to phase EXACTLY 1.0 only
            //     because the denominator is L'-1. This is the clause that fails
            //     at ~0.0133 for Exponential at grainSeconds = 0.05 if the
            //     denominator is L'.
            REQUIRE(std::abs(grain.samples.back()) <= kZeroTolerance);

            // --- D-13, tail RUN: when the table-index step is at most one entry
            //     per sample, the run of kEnvelopeTailZeroEntries = 2 forced
            //     entries makes the last TWO samples exactly 0 as well. At
            //     Delta > 1 the second-to-last sample lands below the forced run
            //     (table[4093] at grainSeconds = 0.05) and is bounded by the
            //     terminal-step clause instead.
            const double indexStep =
                static_cast<double>(AtmosphereEngine::kEnvelopeTableSize - 1) /
                static_cast<double>(grain.lifetime - 1);
            CAPTURE(indexStep);
            if (indexStep <= 1.0) {
                REQUIRE(std::abs(grain.samples[grain.samples.size() - 2]) <= kZeroTolerance);
            }

            // --- SC-003's real concern: the grain must not terminate on a step.
            float maxTerminalStep = 0.0f;
            for (std::size_t k = grain.samples.size() - kTerminalWindow; k < grain.samples.size();
                 ++k) {
                maxTerminalStep =
                    std::max(maxTerminalStep, std::abs(grain.samples[k] - grain.samples[k - 1]));
            }
            CAPTURE(maxTerminalStep);
            REQUIRE(maxTerminalStep < kTerminalStepFraction * grain.maxAbs);

            // --- And the engine is silent again immediately afterwards: the
            //     grain is gone, not merely quiet.
            const float dcLeft = 1.0f;
            const float dcRight = 1.0f;
            float postLeft = 1.0f;
            float postRight = 1.0f;
            engine.processStereoBlock(&dcLeft, &dcRight, &postLeft, &postRight, 1);
            REQUIRE(postLeft == 0.0f);
            REQUIRE(postRight == 0.0f);
        }
    }
}

// =============================================================================
// T008 - FR-028 / FR-034: the 1/sqrt(n) population gain
// =============================================================================
// WHY WHITE NOISE AND NOT A TONE. The 1/sqrt(n) law is only the right
// compensation for an INCOHERENT sum. Two grains add coherently when their read
// positions coincide to within the source's correlation length; for white noise
// that is one sample, so across a birth window ~2 s wide coincidences are
// vanishingly rare and the sum's variance really is ~n. Driven with DC or a
// tone every grain would be coherent with every other, the sum would scale as n
// rather than sqrt(n), and this case would be measuring the input rather than
// the engine.
//
// WHY THE SECOND CLAUSE EXISTS. A birth-time snapshot of 1/sqrt(n) - the
// obvious wrong implementation - passes the first clause outright, because in a
// steady population every grain was born into the same crowd. It fails only
// when the crowd THINS under grains that are still in flight, which is what the
// second clause constructs. Plan S9.9 records the audible failure it stands
// for: a grain born into a crowd staying quiet for its whole 30 s life.
TEST_CASE("AtmosphereEngine_PopulationGain", "[atmosphere]") {
    using Krate::DSP::AtmosphereEngine;

    constexpr double kSampleRate = 48000.0;
    constexpr std::size_t kBlock = 512;
    constexpr double kWarmupSeconds = 6.0;
    constexpr double kMeasureSeconds = 20.0;
    constexpr double kToleranceDb = 1.0;

    // Everything that is NOT the population, held still. Each line removes a
    // variance source from the MEASUREMENT without touching the law under test:
    // the 1/sqrt(n) gain reads the live grain count and nothing else.
    const auto configure = [](AtmosphereEngine& engine) {
        engine.setSeed(20260728u);
        engine.setGrainSeconds(4.0f);
        // Trapezoid rather than the Hann default: its envelope sits at 1 for
        // 80 % of the grain (core/grain_envelope.h:61-73), so sum(env^2)/n
        // concentrates far more tightly than Hann's does and a 4-grain
        // population gives a stable window RMS instead of a noisy one.
        engine.setGrainEnvelope(Krate::DSP::GrainEnvelopeType::Trapezoid);
        // panSpread = 0 puts both gains at cos(pi/4) for every grain, and
        // decorrelation = 0 makes R read the same ring point as L. With the same
        // noise on both input channels the two output channels then carry the
        // same signal, so one channel's RMS is the whole story and the pan draw
        // contributes no variance at all.
        engine.setPanSpread(0.0f);
        engine.setDecorrelation(0.0f);
        // A wide birth window - read ages spread over ~2 s - so grains stay
        // mutually incoherent as the population grows.
        engine.setPositionSeconds(1.0f);
        engine.setPositionSpread(1.0f);
        engine.setPitchSpread(0.15f);
        engine.setLevel(1.0f);
    };

    AtmosphereEngine::PrepareConfig config;
    config.captureSeconds = 8.0f;
    config.blurEnabled = false;
    config.freezeEnabled = false;

    // --- Clause 1: the output RMS does not move with the density.
    const std::array<float, 3> densities{1.0f, 4.0f, 16.0f};
    std::array<double, 3> measuredDb{};
    std::array<std::size_t, 3> measuredActive{};

    for (std::size_t k = 0; k < densities.size(); ++k) {
        AtmosphereEngine engine;
        engine.prepare(kSampleRate, config);
        configure(engine);
        engine.setDensity(densities[k]);

        NoiseRenderer noise(kBlock, 9001u);
        renderNoise(engine, noise, kWarmupSeconds, kSampleRate);
        const NoiseRenderStats stats = renderNoise(engine, noise, kMeasureSeconds, kSampleRate);

        CAPTURE(densities[k], stats.rmsLeft, stats.maxActive, stats.born);
        REQUIRE(stats.born > 0u);
        REQUIRE(stats.rmsLeft > 1e-4);
        // The two channels really are one signal here, which is what makes the
        // single-channel RMS above sufficient.
        REQUIRE(stats.maxAbsDelta <= 1e-6);

        measuredDb[k] = rmsToDb(stats.rmsLeft);
        measuredActive[k] = stats.maxActive;
    }

    // NON-VACUOUSNESS: the live population really does span ~16x across the
    // sweep, so an implementation applying a CONSTANT gain would be ~12 dB out
    // at the top of it rather than within 1 dB.
    CAPTURE(measuredActive[0], measuredActive[1], measuredActive[2]);
    REQUIRE(measuredActive[1] > measuredActive[0]);
    REQUIRE(measuredActive[2] > measuredActive[0] * 4);

    for (std::size_t k = 1; k < densities.size(); ++k) {
        CAPTURE(densities[k], measuredDb[0], measuredDb[k]);
        REQUIRE(std::abs(measuredDb[k] - measuredDb[0]) <= kToleranceDb);
    }

    // --- Clause 2: a grain born into a crowd does not stay quiet as the crowd
    //     thins. The gain tracks the LIVE population, so dropping the density
    //     must return the level to the density = 1 reference measured above.
    AtmosphereEngine thinning;
    thinning.prepare(kSampleRate, config);
    configure(thinning);
    thinning.setDensity(16.0f);

    NoiseRenderer noise(kBlock, 9001u);
    renderNoise(thinning, noise, kWarmupSeconds, kSampleRate);
    const NoiseRenderStats crowded = renderNoise(thinning, noise, 4.0, kSampleRate);
    CAPTURE(crowded.maxActive, crowded.rmsLeft, crowded.born);
    REQUIRE(crowded.maxActive >= 32u);

    thinning.setDensity(1.0f);
    // Longer than one grain lifetime, so every grain born into the crowd has
    // retired and the population has settled at its new size before the
    // measurement window opens.
    renderNoise(thinning, noise, 8.0, kSampleRate);

    const NoiseRenderStats thinned = renderNoise(thinning, noise, kMeasureSeconds, kSampleRate);
    CAPTURE(thinned.maxActive, thinned.rmsLeft, thinned.born, measuredDb[0]);
    REQUIRE(thinned.born > crowded.born);
    REQUIRE(thinned.maxActive * 2 < crowded.maxActive);
    REQUIRE(std::abs(rmsToDb(thinned.rmsLeft) - measuredDb[0]) <= kToleranceDb);
}

// =============================================================================
// T008 - FR-032 / FR-033: per-grain equal-power pan and L/R decorrelation
// =============================================================================
// The engine has NO width control (FR-060): its stereo image is exactly these
// two mechanisms and nothing else.

TEST_CASE("AtmosphereEngine_PanAndDecorrelation", "[atmosphere]") {
    using Krate::DSP::AtmosphereEngine;

    constexpr double kSampleRate = 48000.0;
    constexpr std::size_t kBlock = 512;

    AtmosphereEngine::PrepareConfig config;
    config.captureSeconds = 8.0f;
    config.blurEnabled = false;
    config.freezeEnabled = false;

    const auto configure = [](AtmosphereEngine& engine) {
        engine.setSeed(4242u);
        engine.setGrainSeconds(2.0f);
        engine.setDensity(8.0f);
        engine.setPositionSeconds(1.0f);
        engine.setPositionSpread(0.8f);
        engine.setPitchSpread(0.15f);
        engine.setPanSpread(0.0f);
        engine.setLevel(1.0f);
    };

    SECTION("panSpread = 0 and decorrelation = 0 give two identical channels") {
        AtmosphereEngine engine;
        engine.prepare(kSampleRate, config);
        configure(engine);
        engine.setDecorrelation(0.0f);

        NoiseRenderer noise(kBlock, 555u);
        renderNoise(engine, noise, 4.0, kSampleRate);
        const NoiseRenderStats stats = renderNoise(engine, noise, 6.0, kSampleRate);

        CAPTURE(stats.rmsLeft, stats.rmsRight, stats.maxAbsDelta, stats.maxActive, stats.born);
        REQUIRE(stats.born > 0u);
        // Not a comparison of two silences.
        REQUIRE(stats.rmsLeft > 1e-4);
        // The only difference left between the channels is cos(pi/4) against
        // sin(pi/4) - the same value, up to at most an ULP of the library's two
        // results.
        REQUIRE(stats.maxAbsDelta <= 1e-6);
    }

    SECTION("decorrelation drops the inter-channel correlation") {
        AtmosphereEngine correlated;
        correlated.prepare(kSampleRate, config);
        configure(correlated);
        correlated.setDecorrelation(0.0f);

        NoiseRenderer noiseA(kBlock, 555u);
        renderNoise(correlated, noiseA, 4.0, kSampleRate);
        const NoiseRenderStats without = renderNoise(correlated, noiseA, 6.0, kSampleRate);

        AtmosphereEngine decorrelated;
        decorrelated.prepare(kSampleRate, config);
        configure(decorrelated);
        decorrelated.setDecorrelation(1.0f);

        NoiseRenderer noiseB(kBlock, 555u);
        renderNoise(decorrelated, noiseB, 4.0, kSampleRate);
        const NoiseRenderStats with = renderNoise(decorrelated, noiseB, 6.0, kSampleRate);

        CAPTURE(without.correlation, with.correlation, without.rmsLeft, with.rmsLeft);
        REQUIRE(without.born > 0u);
        REQUIRE(with.born > 0u);
        REQUIRE(without.rmsLeft > 1e-4);
        REQUIRE(with.rmsLeft > 1e-4);

        // Identical channels: the coefficient is 1 up to the accumulation's own
        // rounding.
        REQUIRE(without.correlation > 0.99);
        // Each grain's R channel reads at ageL + dR with dR drawn up to 30 ms.
        // For white noise anything past a single sample of shift is already an
        // independent signal, so only the grains whose dR draw landed within a
        // sample of 0 stay correlated - which is a vanishing fraction.
        REQUIRE(with.correlation < 0.5);
        REQUIRE(with.correlation < without.correlation);
    }

    SECTION("every drawn pan pair satisfies the equal-power law") {
        constexpr std::size_t kSmallBlock = 64;
        constexpr std::size_t kBirthsToWatch = 200;
        constexpr std::size_t kMaxBlocks = 30000;  // ~40 s at 48 kHz

        const std::array<float, 3> panSpreads{0.0f, 0.5f, 1.0f};

        for (const float panSpread : panSpreads) {
            AtmosphereEngine engine;
            engine.prepare(kSampleRate, config);
            configure(engine);
            engine.setPanSpread(panSpread);
            // 20 triggers/s with 0.2 s grains is ~4 concurrent, so the pool
            // never fills and every trigger that clears the ring test becomes a
            // birth with a fresh pan draw.
            engine.setDensity(20.0f);
            engine.setGrainSeconds(0.2f);
            engine.setPositionSeconds(0.5f);

            BlockRenderer blocks(kSmallBlock, 0.4f, -0.4f);
            std::uint64_t previousBorn = 0;
            std::size_t births = 0;
            float worstLawError = 0.0f;
            float minPanL = 2.0f;
            float maxPanL = -2.0f;

            for (std::size_t b = 0; b < kMaxBlocks && births < kBirthsToWatch; ++b) {
                blocks.render(engine);

                const std::uint64_t born = engine.getTotalGrainsBorn();
                if (born == previousBorn) {
                    continue;
                }
                // 64-sample blocks are shorter than the smallest possible
                // interonset (20 grains/s at +/-25 % jitter is 1800 samples), so
                // no birth can be missed by reading the introspection once per
                // block - which is what makes "every drawn pair" literal.
                REQUIRE(born - previousBorn == 1u);
                previousBorn = born;
                ++births;

                float panL = 0.0f;
                float panR = 0.0f;
                engine.getLastBornGrainPanGains(panL, panR);
                worstLawError = std::max(worstLawError,
                                         std::abs((panL * panL) + (panR * panR) - 1.0f));
                minPanL = std::min(minPanL, panL);
                maxPanL = std::max(maxPanL, panL);
            }

            CAPTURE(panSpread, births, worstLawError, minPanL, maxPanL);
            REQUIRE(births == kBirthsToWatch);
            // FR-032's law, on REAL DRAWN VALUES rather than on the formula.
            REQUIRE(worstLawError <= 1e-6f);

            if (panSpread == 0.0f) {
                // Every grain lands dead centre.
                REQUIRE(minPanL == maxPanL);
            } else {
                // Not vacuous: the draw really moves the image, so the law is
                // asserted across a spread of pan positions rather than across
                // 200 copies of one.
                REQUIRE(maxPanL - minPanL > 0.1f);
            }
        }
    }
}

// =============================================================================
// T012 - FR-045 / FR-046: blur disabled costs nothing at all
// =============================================================================
// Two independent claims, and the second is the one that has teeth.
//
// (1) getLatencySamples() reads 0 and setBlur() cannot change it. RA-3 makes the
//     layer latency a property of the PREPARED CONFIGURATION, never of the knob,
//     so a caller compensating a parallel dry path can compute the offset once
//     at prepare() time and never touch it again.
//
// (2) With blur disabled the whole stage is INERT, not merely quiet: no STFT, no
//     OverlapAdd, no SpectralBuffer, no FIFO and no pull scratch is allocated,
//     and the pump is never entered. The bit-identity check below is what proves
//     the second half - an implementation that still ran the STFT round trip and
//     merely forced blur to 0 would pass every other clause in this phase, and
//     would fail here on the FFT's own numerical noise.
//
// BIT-IDENTITY IS THE RIGHT COMPARISON HERE AND NOWHERE ELSE IN THIS PHASE.
// Both renders execute the same instruction sequence in the same binary on the
// same inputs, so this asserts nothing about cross-toolchain floating point -
// it asserts that one code path was taken twice. Every comparison in this phase
// that spans two DIFFERENT computations goes through render_fingerprint.h at
// measured tolerances instead (dsp/CLAUDE.md).

TEST_CASE("AtmosphereEngine_BlurDisabledIsFree", "[atmosphere]") {
    using Krate::DSP::AtmosphereEngine;

    constexpr double kSampleRate = 48000.0;
    constexpr std::size_t kBlock = 512;
    constexpr std::size_t kBlocks = 400;  // 204 800 samples = 4.27 s
    constexpr std::uint32_t kNoiseSeed = 0x5E2A0012u;
    constexpr std::uint32_t kEngineSeed = 24680u;

    AtmosphereEngine::PrepareConfig disabled;
    disabled.captureSeconds = 8.0f;
    disabled.blurEnabled = false;
    disabled.freezeEnabled = false;
    disabled.blurFftSize = 1024;

    // One render at a given blur amount, capturing every output sample. The
    // noise stream is re-seeded per call, so the two engines see bit-identical
    // input as well as bit-identical settings.
    auto render = [&](float blurAmount, std::vector<float>& outLeft,
                      std::vector<float>& outRight) -> std::uint64_t {
        AtmosphereEngine engine;
        engine.prepare(kSampleRate, disabled);
        engine.setSeed(kEngineSeed);
        engine.setGrainSeconds(1.0f);
        engine.setDensity(10.0f);
        engine.setPositionSeconds(0.25f);
        engine.setPositionSpread(0.3f);
        engine.setBlur(blurAmount);

        Krate::DSP::Xorshift32 noise(kNoiseSeed);
        std::vector<float> inLeft(kBlock, 0.0f);
        std::vector<float> inRight(kBlock, 0.0f);
        std::vector<float> blockLeft(kBlock, 0.0f);
        std::vector<float> blockRight(kBlock, 0.0f);
        outLeft.assign(kBlock * kBlocks, 0.0f);
        outRight.assign(kBlock * kBlocks, 0.0f);

        for (std::size_t b = 0; b < kBlocks; ++b) {
            for (std::size_t i = 0; i < kBlock; ++i) {
                const float value = noise.nextFloat() * 0.5f;
                inLeft[i] = value;
                inRight[i] = value;
            }
            engine.processStereoBlock(inLeft.data(), inRight.data(), blockLeft.data(),
                                      blockRight.data(), kBlock);
            const auto offset = static_cast<std::ptrdiff_t>(b * kBlock);
            std::copy(blockLeft.begin(), blockLeft.end(), outLeft.begin() + offset);
            std::copy(blockRight.begin(), blockRight.end(), outRight.begin() + offset);
        }
        return engine.getTotalGrainsBorn();
    };

    SECTION("FR-046: latency is 0, and FR-045: setBlur changes nothing at all") {
        AtmosphereEngine probe;
        probe.prepare(kSampleRate, disabled);
        REQUIRE(probe.getLatencySamples() == 0u);

        // The setter still clamps and stores - it is not disabled, it is unread.
        probe.setBlur(1.0f);
        REQUIRE(probe.getBlur() == 1.0f);
        REQUIRE(probe.getLatencySamples() == 0u);

        std::vector<float> zeroLeft;
        std::vector<float> zeroRight;
        std::vector<float> oneLeft;
        std::vector<float> oneRight;
        const std::uint64_t bornZero = render(0.0f, zeroLeft, zeroRight);
        const std::uint64_t bornOne = render(1.0f, oneLeft, oneRight);

        // NON-VACUOUSNESS: two silent renders are also bit-identical.
        double sumSquares = 0.0;
        for (const float value : zeroLeft) {
            sumSquares += static_cast<double>(value) * static_cast<double>(value);
        }
        const double rms = std::sqrt(sumSquares / static_cast<double>(zeroLeft.size()));
        CAPTURE(bornZero, bornOne, rms);
        REQUIRE(bornZero > 0u);
        REQUIRE(bornOne == bornZero);
        REQUIRE(rms > 1e-3);

        std::size_t firstDifference = zeroLeft.size();
        for (std::size_t i = 0; i < zeroLeft.size(); ++i) {
            if (zeroLeft[i] != oneLeft[i] || zeroRight[i] != oneRight[i]) {
                firstDifference = i;
                break;
            }
        }
        CAPTURE(firstDifference);
        REQUIRE(firstDifference == zeroLeft.size());
    }

    SECTION("FR-045: a re-prepare without blur allocates strictly less than with it") {
        AtmosphereEngine::PrepareConfig withBlur = disabled;
        withBlur.blurEnabled = true;

        // Both engines start from the SAME warm state - one prepare with blur
        // on - so the measurement below compares the SECOND prepare and nothing
        // else. Without the warm-up prepare the difference would be dominated by
        // the capture ring and the envelope table, which both configurations
        // allocate identically.
        AtmosphereEngine keepsBlur;
        AtmosphereEngine dropsBlur;
        keepsBlur.prepare(kSampleRate, withBlur);
        dropsBlur.prepare(kSampleRate, withBlur);

        // The count is read from the detector singleton while the scope is still
        // open: AllocationScope latches its own count in its DESTRUCTOR
        // (tests/test_helpers/allocation_detector.h:81-83), so
        // scope.getAllocationCount() reads 0 until the object dies.
        std::size_t withBlurCount = 0;
        std::size_t withoutBlurCount = 0;
        {
            [[maybe_unused]] const TestHelpers::AllocationScope scope;
            keepsBlur.prepare(kSampleRate, withBlur);
            withBlurCount = TestHelpers::AllocationDetector::instance().getAllocationCount();
        }
        {
            [[maybe_unused]] const TestHelpers::AllocationScope scope;
            dropsBlur.prepare(kSampleRate, disabled);
            withoutBlurCount = TestHelpers::AllocationDetector::instance().getAllocationCount();
        }

        CAPTURE(withBlurCount, withoutBlurCount);
        // Non-vacuous: re-preparing the blur stage really does reach the heap
        // (STFT::prepare and OverlapAdd::prepare each take a Window::generate
        // return-by-value, stft.h:72 and :221), so "strictly less" is a real
        // comparison rather than 0 < 0 being asserted the other way round.
        REQUIRE(withBlurCount > 0u);
        REQUIRE(withoutBlurCount < withBlurCount);

        REQUIRE(keepsBlur.getLatencySamples() == 1024u);
        REQUIRE(dropsBlur.getLatencySamples() == 0u);
    }
}

// =============================================================================
// T014 - FR-050 .. FR-054: the pure-freeze leg's capture contract
// =============================================================================
//
// WHAT THIS CASE IS AND IS NOT. It measures the CAPTURE contract: the no-op
// early-out, the armed drone, the release fade, and the freeze-disabled
// inertness clause. It deliberately runs with blur DISABLED, because FR-052's
// delay-matched leg only exists when blur is enabled and an uncompensated
// 1024-sample offset presents as a step at the crossfade - which is SC-007's
// business, in the spectral TU (AtmosphereEngine_FreezeStability). Folding the
// delay in here would put two independent questions behind one failure.
//
// The exact-zero clause in the release section rests on an identity, not on a
// tolerance: at a settled freezeMix of 1.0 the crossfade weight `1.0f - m` is
// EXACTLY 0.0f (LinearRamp::process clamps overshoot to the target,
// primitives/smoother.h:380-383), so the wet leg contributes nothing and the
// output IS the freeze leg. Once SpectralFreezeOscillator clears frozen_ it
// fills zeros (processors/spectral_freeze_oscillator.h:327-330), and
// detail::flushDenormal leaves an exact zero alone.
//
TEST_CASE("AtmosphereEngine_FreezeCaptureAndRelease", "[atmosphere]") {
    using Krate::DSP::AtmosphereEngine;

    constexpr double kRate = 48000.0;
    constexpr std::size_t kBlock = 512;
    constexpr std::size_t kFreezeFft = 2048;
    /// spectral_freeze_oscillator.h:117 - hopSize_ = fftSize / 4 - and
    /// unfreeze() fades over exactly one hop (:299).
    constexpr std::size_t kFreezeHop = kFreezeFft / 4;
    /// The 100 ms kFreezeMixRampMs LinearRamp at 48 kHz.
    constexpr std::size_t kMixRampSamples = 4800;

    constexpr std::uint32_t kEngineSeed = 0x5E2A0014u;
    constexpr std::uint32_t kNoiseSeed = 0x5E2A0015u;

    /// 102 400 samples = 2.13 s: more than one 2048-sample freeze window AND
    /// well past the 0.25 s birth read age, so the grain bus is dense before
    /// anything is captured.
    constexpr std::size_t kWarmBlocks = 200;
    /// 1 024 samples - deliberately SHORT of one whole freeze window.
    constexpr std::size_t kColdBlocks = 2;
    /// 30 720 samples = 0.64 s, comfortably past kMixRampSamples.
    constexpr std::size_t kHoldBlocks = 60;

    static_assert(kWarmBlocks * kBlock > kFreezeFft,
                  "the warm-up must leave the ring holding more than one whole freeze window");
    static_assert(kColdBlocks * kBlock < kFreezeFft,
                  "the cold clause is only a no-op test if the ring is SHORT of a whole window");
    static_assert(kHoldBlocks * kBlock > kMixRampSamples,
                  "the hold must outlast the crossfade ramp or the measurement is on a ramp");

    AtmosphereEngine::PrepareConfig enabled;
    enabled.captureSeconds = 8.0f;
    enabled.blurEnabled = false;
    enabled.freezeEnabled = true;
    enabled.freezeFftSize = kFreezeFft;
    enabled.maxBlockSamples = kBlock;

    AtmosphereEngine::PrepareConfig disabled = enabled;
    disabled.freezeEnabled = false;

    /// The setter history applied IDENTICALLY to every engine below, so the
    /// only difference between a pair is the freeze calls under test.
    auto configure = [&](AtmosphereEngine& engine) {
        engine.setSeed(kEngineSeed);
        engine.setGrainSeconds(1.0f);
        engine.setDensity(10.0f);
        engine.setJitter(0.4f);
        engine.setPositionSeconds(0.25f);
        engine.setPositionSpread(0.2f);
        engine.setBlur(0.0f);
        engine.setLevel(1.0f);
    };

    /// Render `numBlocks` of white noise (identical on both channels, so any
    /// stereo difference in the output is the engine's) and APPEND the output.
    /// The noise generator is passed in by reference, so two engines fed from
    /// two generators seeded alike see bit-identical input.
    auto renderBlocks = [&](AtmosphereEngine& engine, Krate::DSP::Xorshift32& noise,
                            std::size_t numBlocks, std::vector<float>& sinkLeft,
                            std::vector<float>& sinkRight) {
        std::vector<float> inLeft(kBlock, 0.0f);
        std::vector<float> inRight(kBlock, 0.0f);
        std::vector<float> outLeft(kBlock, 0.0f);
        std::vector<float> outRight(kBlock, 0.0f);
        for (std::size_t b = 0; b < numBlocks; ++b) {
            for (std::size_t i = 0; i < kBlock; ++i) {
                const float value = noise.nextFloat() * 0.5f;
                inLeft[i] = value;
                inRight[i] = value;
            }
            engine.processStereoBlock(inLeft.data(), inRight.data(), outLeft.data(),
                                      outRight.data(), kBlock);
            sinkLeft.insert(sinkLeft.end(), outLeft.begin(), outLeft.end());
            sinkRight.insert(sinkRight.end(), outRight.begin(), outRight.end());
        }
    };

    auto rmsOver = [](const std::vector<float>& buffer, std::size_t first,
                      std::size_t last) -> double {
        if (last <= first || last > buffer.size()) {
            return 0.0;
        }
        double sumSquares = 0.0;
        for (std::size_t i = first; i < last; ++i) {
            sumSquares += static_cast<double>(buffer[i]) * static_cast<double>(buffer[i]);
        }
        return std::sqrt(sumSquares / static_cast<double>(last - first));
    };

    /// Index of the first sample at which two renders disagree, or the length
    /// when they are bit-identical.
    auto firstDifference = [](const std::vector<float>& aLeft, const std::vector<float>& aRight,
                              const std::vector<float>& bLeft,
                              const std::vector<float>& bRight) -> std::size_t {
        const std::size_t count = std::min(aLeft.size(), bLeft.size());
        for (std::size_t i = 0; i < count; ++i) {
            if (aLeft[i] != bLeft[i] || aRight[i] != bRight[i]) {
                return i;
            }
        }
        return count;
    };

    SECTION("FR-051: a capture before the ring holds a whole window is a NO-OP") {
        AtmosphereEngine attempted;
        AtmosphereEngine control;
        attempted.prepare(kRate, enabled);
        control.prepare(kRate, enabled);
        configure(attempted);
        configure(control);

        std::vector<float> attemptedLeft;
        std::vector<float> attemptedRight;
        std::vector<float> controlLeft;
        std::vector<float> controlRight;
        Krate::DSP::Xorshift32 noiseA(kNoiseSeed);
        Krate::DSP::Xorshift32 noiseB(kNoiseSeed);

        renderBlocks(attempted, noiseA, kColdBlocks, attemptedLeft, attemptedRight);
        renderBlocks(control, noiseB, kColdBlocks, controlLeft, controlRight);

        // The engine snapped the requested size, and the capture length comes
        // from the OSCILLATOR (spectral_freeze_oscillator.h:426-428) - a capture
        // taken at the requested length instead would silently discard the
        // newest audio (:222-223).
        REQUIRE(attempted.getFreezeFftSize() == kFreezeFft);

        attempted.captureFreeze();
        // A PARTIAL capture would have armed the oscillator on a zero-padded
        // window - a different spectrum, not a quieter one. The no-op leaves it
        // unarmed, and isFreezeCaptured() is what tells the two apart: both
        // branches of captureFreeze() are otherwise silent.
        REQUIRE_FALSE(attempted.isFreezeCaptured());

        // freezeMix stays at 0 on BOTH engines, so what is compared below is
        // ordinary grain audio that is NOT multiplied by zero. An early capture
        // that touched any engine state shows up here.
        renderBlocks(attempted, noiseA, kWarmBlocks, attemptedLeft, attemptedRight);
        renderBlocks(control, noiseB, kWarmBlocks, controlLeft, controlRight);

        // NON-VACUOUSNESS: two silent renders are also bit-identical.
        const double rms = rmsOver(controlLeft, 0, controlLeft.size());
        const std::uint64_t born = control.getTotalGrainsBorn();
        CAPTURE(rms, born);
        REQUIRE(born > 0u);
        REQUIRE(rms > 1e-4);

        REQUIRE(attemptedLeft.size() == controlLeft.size());
        const std::size_t divergence =
            firstDifference(attemptedLeft, attemptedRight, controlLeft, controlRight);
        CAPTURE(divergence, attemptedLeft.size());
        REQUIRE(divergence == attemptedLeft.size());

        // ...and the early-out was about AVAILABILITY, not a permanent disable:
        // the identical call on the now-warm ring arms the drone.
        attempted.captureFreeze();
        REQUIRE(attempted.isFreezeCaptured());
    }

    SECTION("FR-050: after a valid capture a settled freezeMix = 1 is non-silent") {
        AtmosphereEngine engine;
        engine.prepare(kRate, enabled);
        configure(engine);

        std::vector<float> dryLeft;
        std::vector<float> dryRight;
        Krate::DSP::Xorshift32 noise(kNoiseSeed);
        renderBlocks(engine, noise, kWarmBlocks, dryLeft, dryRight);

        const double dryRms = rmsOver(dryLeft, 0, dryLeft.size());
        CAPTURE(rmsToDb(dryRms), engine.getTotalGrainsBorn());
        REQUIRE(engine.getTotalGrainsBorn() > 0u);
        REQUIRE(dryRms > 1e-4);

        engine.captureFreeze();
        REQUIRE(engine.isFreezeCaptured());
        engine.setFreezeMix(1.0f);

        std::vector<float> wetLeft;
        std::vector<float> wetRight;
        renderBlocks(engine, noise, kHoldBlocks, wetLeft, wetRight);

        // Measured PAST the ramp, so this is the drone and not a crossfade.
        const double wetRmsLeft = rmsOver(wetLeft, kMixRampSamples, wetLeft.size());
        const double wetRmsRight = rmsOver(wetRight, kMixRampSamples, wetRight.size());
        CAPTURE(rmsToDb(wetRmsLeft), rmsToDb(wetRmsRight));
        REQUIRE(rmsToDb(wetRmsLeft) > -60.0);
        REQUIRE(rmsToDb(wetRmsRight) > -60.0);
    }

    SECTION("FR-053: releaseFreeze() fades the drone out within one hop") {
        AtmosphereEngine engine;
        engine.prepare(kRate, enabled);
        configure(engine);

        std::vector<float> warmLeft;
        std::vector<float> warmRight;
        Krate::DSP::Xorshift32 noise(kNoiseSeed);
        renderBlocks(engine, noise, kWarmBlocks, warmLeft, warmRight);

        engine.captureFreeze();
        REQUIRE(engine.isFreezeCaptured());
        engine.setFreezeMix(1.0f);

        std::vector<float> holdLeft;
        std::vector<float> holdRight;
        renderBlocks(engine, noise, kHoldBlocks, holdLeft, holdRight);
        const double holdRms = rmsOver(holdLeft, kMixRampSamples, holdLeft.size());
        CAPTURE(rmsToDb(holdRms));
        REQUIRE(rmsToDb(holdRms) > -60.0);

        engine.releaseFreeze();

        std::vector<float> tailLeft;
        std::vector<float> tailRight;
        renderBlocks(engine, noise, std::size_t{8}, tailLeft, tailRight);  // 4096 samples

        // The fade itself must be audible, or "silent afterwards" is a
        // statement about a leg that was already silent.
        const double fadeRms = rmsOver(tailLeft, 0, kFreezeHop);
        CAPTURE(rmsToDb(fadeRms));
        REQUIRE(rmsToDb(fadeRms) > -60.0);

        // unfreeze() sets unfadeSamplesRemaining_ = hopSize_ (:299) and
        // processBlock reaches frozen_ = false only on the sample AFTER that
        // counter hits zero (:346-357). Two hops is a generous bound on "within
        // one hop"; from there the output is EXACTLY zero - see the case banner.
        std::size_t firstNonZero = tailLeft.size();
        for (std::size_t i = 2u * kFreezeHop; i < tailLeft.size(); ++i) {
            if (tailLeft[i] != 0.0f || tailRight[i] != 0.0f) {
                firstNonZero = i;
                break;
            }
        }
        CAPTURE(firstNonZero, tailLeft.size());
        REQUIRE(firstNonZero == tailLeft.size());
        REQUIRE_FALSE(engine.isFreezeCaptured());
    }

    SECTION("FR-054: with freeze disabled every freeze entry point is inert") {
        AtmosphereEngine touched;
        AtmosphereEngine control;
        touched.prepare(kRate, disabled);
        control.prepare(kRate, disabled);
        configure(touched);
        configure(control);

        REQUIRE(touched.getFreezeFftSize() == 0u);
        REQUIRE_FALSE(touched.isFreezeCaptured());

        std::vector<float> touchedLeft;
        std::vector<float> touchedRight;
        std::vector<float> controlLeft;
        std::vector<float> controlRight;
        Krate::DSP::Xorshift32 noiseA(kNoiseSeed);
        Krate::DSP::Xorshift32 noiseB(kNoiseSeed);

        renderBlocks(touched, noiseA, kWarmBlocks, touchedLeft, touchedRight);
        renderBlocks(control, noiseB, kWarmBlocks, controlLeft, controlRight);

        // Every freeze entry point, on an engine that allocated no leg at all.
        touched.captureFreeze();
        touched.setFreezeMix(1.0f);
        touched.releaseFreeze();
        REQUIRE_FALSE(touched.isFreezeCaptured());
        // The setter still CLAMPS AND STORES - it is unread, not disabled -
        // which is the same shape setBlur has when blur is off.
        REQUIRE(touched.getFreezeMix() == 1.0f);

        renderBlocks(touched, noiseA, kHoldBlocks, touchedLeft, touchedRight);
        renderBlocks(control, noiseB, kHoldBlocks, controlLeft, controlRight);

        // NON-VACUOUSNESS first: a stored freezeMix of 1 that DID reach the
        // crossfade would mute the engine, and two silent renders agree.
        const double rms = rmsOver(controlLeft, 0, controlLeft.size());
        const double touchedRms = rmsOver(touchedLeft, 0, touchedLeft.size());
        CAPTURE(rms, touchedRms, control.getTotalGrainsBorn());
        REQUIRE(control.getTotalGrainsBorn() > 0u);
        REQUIRE(rms > 1e-4);
        REQUIRE(touchedRms > 1e-4);

        REQUIRE(touchedLeft.size() == controlLeft.size());
        const std::size_t divergence =
            firstDifference(touchedLeft, touchedRight, controlLeft, controlRight);
        CAPTURE(divergence, touchedLeft.size());
        REQUIRE(divergence == touchedLeft.size());
    }

    SECTION("FR-054: a prepare without freeze allocates strictly less than with it") {
        // BOTH ENGINES ARE COLD. An earlier revision of this section warmed each
        // engine with a first prepare(kRate, enabled) and measured the SECOND
        // one, on the theory that a warm re-prepare isolates the freeze leg from
        // the capture ring. That probe measures nothing:
        // SpectralFreezeOscillator::prepare reaches the heap only through
        // resize()/assign() at an UNCHANGED size (spectral_freeze_oscillator.h
        // :127-158), so a warm re-prepare legitimately allocates ZERO times and
        // the comparison collapsed to 0 < 0 - it failed on its own
        // non-vacuousness guard rather than on the engine. (The blur analogue in
        // AtmosphereEngine_BlurDisabledIsFree survives that shape only because
        // STFT/OverlapAdd::prepare each take a Window::generate return-by-value,
        // stft.h:72 and :221, which allocates on every call regardless of size.)
        //
        // A cold pair is the discriminating measurement instead: the two configs
        // differ ONLY in freezeEnabled, so every shared allocation (capture ring,
        // envelope table, grain scratch) is made the same number of times on both
        // legs and the entire difference IS the freeze leg. It is dominated by
        // the shared allocations in BYTES, but the assertion is on the count and
        // is strict, so that costs nothing.
        AtmosphereEngine keepsFreeze;
        AtmosphereEngine dropsFreeze;

        // The count is read from the detector singleton while the scope is
        // still open: AllocationScope latches its own count in its DESTRUCTOR
        // (tests/test_helpers/allocation_detector.h:81-83).
        std::size_t withFreezeCount = 0;
        std::size_t withoutFreezeCount = 0;
        {
            [[maybe_unused]] const TestHelpers::AllocationScope scope;
            keepsFreeze.prepare(kRate, enabled);
            withFreezeCount = TestHelpers::AllocationDetector::instance().getAllocationCount();
        }
        {
            [[maybe_unused]] const TestHelpers::AllocationScope scope;
            dropsFreeze.prepare(kRate, disabled);
            withoutFreezeCount = TestHelpers::AllocationDetector::instance().getAllocationCount();
        }

        CAPTURE(withFreezeCount, withoutFreezeCount);
        // Non-vacuous in BOTH directions: the probe counts the shared cold-prepare
        // allocations on the disabled leg (so it is demonstrably wired up), and
        // the enabled leg must exceed it (so the two oscillators really were
        // built). 0 < 0 cannot pass either clause.
        REQUIRE(withoutFreezeCount > 0u);
        REQUIRE(withoutFreezeCount < withFreezeCount);

        REQUIRE(keepsFreeze.getFreezeFftSize() == kFreezeFft);
        REQUIRE(dropsFreeze.getFreezeFftSize() == 0u);
    }
}

// =============================================================================
// T008 - FR-007: the silence ramp, the latch, and reset() as the one re-entry
// =============================================================================

TEST_CASE("AtmosphereEngine_SilenceLatchAndReset", "[atmosphere]") {
    using Krate::DSP::AtmosphereEngine;

    constexpr double kSampleRate = 48000.0;
    constexpr std::size_t kBlock = 512;
    const double rampSamples =
        static_cast<double>(AtmosphereEngine::kSilenceRampMs) * 0.001 * kSampleRate;

    AtmosphereEngine::PrepareConfig config;
    config.captureSeconds = 8.0f;
    config.blurEnabled = false;
    config.freezeEnabled = false;

    AtmosphereEngine engine;
    engine.prepare(kSampleRate, config);
    engine.setDensity(8.0f);
    engine.setGrainSeconds(2.0f);
    engine.setPositionSeconds(0.5f);
    engine.setPositionSpread(0.5f);
    engine.setPanSpread(0.0f);
    engine.setDecorrelation(0.0f);

    NoiseRenderer noise(kBlock, 8080u);
    renderNoise(engine, noise, 3.0, kSampleRate);
    const NoiseRenderStats loud = renderNoise(engine, noise, 1.0, kSampleRate);

    CAPTURE(loud.rmsLeft, loud.maxActive, loud.born);
    REQUIRE(loud.born > 0u);
    REQUIRE(engine.getActiveGrainCount() > 0u);
    REQUIRE(loud.rmsLeft > 1e-4);

    // --- The ramp, located EXACTLY: one sample per call, so the last audible
    //     sample is an index rather than a block.
    engine.silence();

    Krate::DSP::Xorshift32 rampNoise(1234u);
    constexpr std::size_t kRampProbeSamples = 4800;  // 100 ms - 10x the ramp
    std::size_t lastNonZero = 0;
    bool anyNonZero = false;

    for (std::size_t i = 0; i < kRampProbeSamples; ++i) {
        const float value = rampNoise.nextFloat() * 0.5f;
        const float inLeft = value;
        const float inRight = value;
        float outLeft = 0.0f;
        float outRight = 0.0f;
        engine.processStereoBlock(&inLeft, &inRight, &outLeft, &outRight, 1);
        if (outLeft != 0.0f || outRight != 0.0f) {
            lastNonZero = i;
            anyNonZero = true;
        }
    }

    REQUIRE(anyNonZero);
    CAPTURE(lastNonZero, rampSamples);
    // A FADE, not a hard mute: the engine is still producing audio well past
    // the ramp's halfway point.
    REQUIRE(static_cast<double>(lastNonZero) > rampSamples * 0.5);
    // And it is over at the end of the ramp. The decrement happens AFTER the
    // multiply, so the crossing sample can be the one at index rampSamples; the
    // slack covers the accumulation of 480 float subtractions of 1/480 and
    // nothing more.
    REQUIRE(static_cast<double>(lastNonZero) <= rampSamples + 4.0);

    // --- The latch itself.
    REQUIRE(engine.getActiveGrainCount() == 0u);
    // totalRetired_ is incremented BEFORE activeCount_ is zeroed, so the FR-072
    // identity holds through the latch as well as through ordinary retirement.
    REQUIRE(engine.getTotalGrainsRetired() == engine.getTotalGrainsBorn());

    const std::uint64_t bornAtLatch = engine.getTotalGrainsBorn();
    const std::uint64_t poolSkipAtLatch = engine.getSkippedTriggerCountPoolFull();
    const std::uint64_t coldSkipAtLatch = engine.getSkippedTriggerCountRingCold();
    REQUIRE(bornAtLatch > 0u);

    // --- Across the latched span: exact zeros, and nothing advances. A latched
    //     engine costs the zero-fill and nothing else - no capture, no
    //     scheduler tick, no ageing, no counter movement.
    const NoiseRenderStats latched = renderNoise(engine, noise, 2.0, kSampleRate);
    REQUIRE(latched.exactlyZero);
    REQUIRE_FALSE(latched.anyDenormal);
    REQUIRE(engine.getTotalGrainsBorn() == bornAtLatch);
    REQUIRE(engine.getSkippedTriggerCountPoolFull() == poolSkipAtLatch);
    REQUIRE(engine.getSkippedTriggerCountRingCold() == coldSkipAtLatch);
    REQUIRE(engine.getActiveGrainCount() == 0u);

    // --- A second silence() while latched is a no-op: in particular it does
    //     not restart the ramp, which would let audio back out.
    engine.silence();
    const NoiseRenderStats stillLatched = renderNoise(engine, noise, 1.0, kSampleRate);
    REQUIRE(stillLatched.exactlyZero);
    REQUIRE(engine.getTotalGrainsBorn() == bornAtLatch);
    REQUIRE(engine.getSkippedTriggerCountPoolFull() == poolSkipAtLatch);
    REQUIRE(engine.getSkippedTriggerCountRingCold() == coldSkipAtLatch);

    // --- reset() is the ONE re-entry. There is no resume().
    engine.reset();
    REQUIRE(engine.getTotalGrainsBorn() == 0u);
    REQUIRE(engine.getTotalGrainsRetired() == 0u);

    // The ring was cleared too, so the engine renders again only once it has
    // refilled past the birth read age.
    renderNoise(engine, noise, 3.0, kSampleRate);
    const NoiseRenderStats revived = renderNoise(engine, noise, 2.0, kSampleRate);

    CAPTURE(revived.rmsLeft, revived.born, revived.maxActive);
    REQUIRE(revived.born > 0u);
    REQUIRE(engine.getActiveGrainCount() > 0u);
    REQUIRE(rmsToDb(revived.rmsLeft) > -60.0);
}

// =============================================================================
// T010 helpers - determinism (SC-010, FR-070) and partition invariance (SC-011)
// =============================================================================

namespace {

/// One rendered stereo span, kept as TWO channel-contiguous buffers rather than
/// one interleaved one. Interleaving would put an L/R alternation into
/// `totalVariation` - render_fingerprint.h's sharpest metric, the sum of
/// |x[i] - x[i-1]| (:76) - and that alternation, not the engine's waveform,
/// would then dominate the number the comparison is made on.
struct StereoRender {
    std::vector<float> left;
    std::vector<float> right;
};

/// @brief Render `seconds` of DETERMINISTIC white noise and keep every sample.
///
/// The stimulus is regenerated from `noiseSeed` on every call, so two renders
/// see bit-identical input and any difference between them is something the
/// ENGINE did. White noise specifically: it exercises every ring read with a
/// signal whose autocorrelation length is one sample, so a shifted read point
/// (the failure mode a seed change or a mis-partitioned control grid produces)
/// shows up as a completely different waveform rather than as a phase nudge.
[[nodiscard]] StereoRender renderStereoNoise(Krate::DSP::AtmosphereEngine& engine, double seconds,
                                             double sampleRate, std::size_t blockSize,
                                             std::uint32_t noiseSeed) {
    Krate::DSP::Xorshift32 rng(noiseSeed);
    const auto blocks =
        static_cast<std::size_t>((seconds * sampleRate) / static_cast<double>(blockSize));

    std::vector<float> inLeft(blockSize, 0.0f);
    std::vector<float> inRight(blockSize, 0.0f);
    std::vector<float> outLeft(blockSize, 0.0f);
    std::vector<float> outRight(blockSize, 0.0f);

    StereoRender render;
    render.left.reserve(blocks * blockSize);
    render.right.reserve(blocks * blockSize);

    for (std::size_t b = 0; b < blocks; ++b) {
        for (std::size_t i = 0; i < blockSize; ++i) {
            inLeft[i] = rng.nextFloat() * 0.5f;
            inRight[i] = rng.nextFloat() * 0.5f;
        }
        engine.processStereoBlock(inLeft.data(), inRight.data(), outLeft.data(), outRight.data(),
                                  blockSize);
        render.left.insert(render.left.end(), outLeft.begin(), outLeft.end());
        render.right.insert(render.right.end(), outRight.begin(), outRight.end());
    }
    return render;
}

/// Both channels' fingerprints of one render.
struct StereoFingerprint {
    Krate::DSP::TestUtils::RenderFingerprint left;
    Krate::DSP::TestUtils::RenderFingerprint right;
};

[[nodiscard]] StereoFingerprint fingerprintStereo(const StereoRender& render) {
    StereoFingerprint fingerprint;
    fingerprint.left = Krate::DSP::TestUtils::fingerprintRender(render.left);
    fingerprint.right = Krate::DSP::TestUtils::fingerprintRender(render.right);
    return fingerprint;
}

/// Everything a determinism clause needs from one render, with the RENDER ITSELF
/// dropped: a 20 s stereo span is 7.7 MB, and the clauses below only ever
/// compare fingerprints.
struct DeterminismProbe {
    StereoFingerprint fingerprint;
    std::uint64_t born = 0;
    std::uint32_t grainRngState = 0;
};

[[nodiscard]] DeterminismProbe probeRender(Krate::DSP::AtmosphereEngine& engine, double seconds,
                                           double sampleRate, std::size_t blockSize,
                                           std::uint32_t noiseSeed) {
    const StereoRender render =
        renderStereoNoise(engine, seconds, sampleRate, blockSize, noiseSeed);
    DeterminismProbe probe;
    probe.fingerprint = fingerprintStereo(render);
    probe.born = engine.getTotalGrainsBorn();
    probe.grainRngState = engine.getGrainRngState();
    return probe;
}

/// @brief The determinism configuration - EVERY source of randomness switched on.
///
/// tryBirthGrain() takes exactly four draws off `grainRng_` in a fixed order -
/// position spread, static detune, pan, decorrelation - and `GrainScheduler`
/// draws from its OWN private stream for jitter. Each of the five settings below
/// is what makes one of those draws observable in the output: with any of them
/// at 0 the corresponding draw still happens but is multiplied away, and a seed
/// change would move a stream nothing listens to.
void configureDeterminism(Krate::DSP::AtmosphereEngine& engine, std::uint32_t seed) {
    engine.setSeed(seed);
    engine.setGrainSeconds(2.0f);
    engine.setDensity(8.0f);           // ~16 concurrent grains at 2 s lifetimes
    engine.setJitter(0.5f);            // -> the scheduler's own stream
    engine.setPositionSeconds(1.0f);
    engine.setPositionSpread(0.5f);    // -> draw 1
    engine.setPitchSemitones(0.0f);
    engine.setPitchSpread(0.15f);      // -> draw 2
    engine.setDriftDepth(0.5f);
    engine.setDriftSmoothness(0.7f);
    engine.setDriftRangeSemitones(2.0f);
    engine.setPanSpread(0.7f);         // -> draw 3
    engine.setDecorrelation(0.5f);     // -> draw 4
    engine.setLevel(1.0f);
}

}  // namespace

// =============================================================================
// SC-010 / FR-071 - AtmosphereEngine_SeedDeterminism
// =============================================================================
// FOUR CLAUSES, AND THE THREE THAT ARE NOT THE OBVIOUS ONE ARE WHY THIS CASE
// HAS TEETH:
//
//   1. positive - the same seed and the same setter history reproduce the
//      render within render_fingerprint.h's measured tolerances.
//   2. NEGATIVE - a different seed does NOT. Without it clause 1 passes on a
//      silent engine, on an engine that ignores its seed, and on an engine
//      whose grains never get admitted.
//   3. FR-044 - the blur stage draws from its own stream, asserted on the grain
//      stream's STATE rather than on the birth count (see the clause).
//   4. FR-006 - reset() reproduces the post-prepare render. This is the clause
//      that fails if reset() omits the explicit scheduler_.seed() call:
//      GrainScheduler::reset() and prepare() never touch rng_
//      (processors/grain_scheduler.h:33-42), only seed() does (:97), so the
//      jitter stream would resume mid-sequence.
//
// NEVER A BIT-EXACT DIGEST. Every comparison here is compareFingerprints() at
// kSampleTolerance = 1.0e-4f / kMetricTolerance = 1.0e-5; an FNV hash over the
// sample bits is guaranteed red on the Linux and macOS legs and is gated by
// node tools/lint-float-bit-goldens.js.

TEST_CASE("AtmosphereEngine_SeedDeterminism", "[atmosphere]") {
    using Krate::DSP::AtmosphereEngine;
    using Krate::DSP::TestUtils::compareFingerprints;

    constexpr double kSampleRate = 48000.0;
    constexpr std::size_t kBlock = 512;
    constexpr double kRenderSeconds = 20.0;
    constexpr std::uint32_t kNoiseSeed = 20260728u;
    constexpr std::uint32_t kSeedA = 7654321u;
    constexpr std::uint32_t kSeedB = 987654321u;

    AtmosphereEngine::PrepareConfig config;
    config.captureSeconds = 8.0f;
    config.blurEnabled = false;
    config.freezeEnabled = false;

    // --- The reference render, plus the NON-VACUOUSNESS guard every clause
    //     below leans on: a silent engine reproduces itself perfectly, and an
    //     engine whose grains are never admitted reproduces itself perfectly
    //     too.
    AtmosphereEngine engineA;
    engineA.prepare(kSampleRate, config);
    configureDeterminism(engineA, kSeedA);
    const DeterminismProbe probeA =
        probeRender(engineA, kRenderSeconds, kSampleRate, kBlock, kNoiseSeed);

    CAPTURE(probeA.born, probeA.fingerprint.left.rms, probeA.fingerprint.right.rms);
    REQUIRE(probeA.born > 0u);
    REQUIRE(probeA.fingerprint.left.rms > 1e-4);
    REQUIRE(probeA.fingerprint.right.rms > 1e-4);

    // -------------------------------------------------------------------------
    // Clause 1: the same seed reproduces the render.
    // -------------------------------------------------------------------------
    {
        AtmosphereEngine engineB;
        engineB.prepare(kSampleRate, config);
        configureDeterminism(engineB, kSeedA);
        const DeterminismProbe probeB =
            probeRender(engineB, kRenderSeconds, kSampleRate, kBlock, kNoiseSeed);

        REQUIRE(probeB.born == probeA.born);
        REQUIRE(probeB.grainRngState == probeA.grainRngState);

        const auto left = compareFingerprints(probeB.fingerprint.left, probeA.fingerprint.left);
        const auto right = compareFingerprints(probeB.fingerprint.right, probeA.fingerprint.right);
        CAPTURE(left.worstMetricRelativeError, left.worstSampleError, left.detail);
        CAPTURE(right.worstMetricRelativeError, right.worstSampleError, right.detail);
        REQUIRE(left.withinTolerance());
        REQUIRE(right.withinTolerance());
    }

    // -------------------------------------------------------------------------
    // Clause 2 (the NEGATIVE half): a different seed does NOT reproduce it.
    // -------------------------------------------------------------------------
    {
        AtmosphereEngine engineC;
        engineC.prepare(kSampleRate, config);
        configureDeterminism(engineC, kSeedB);
        const DeterminismProbe probeC =
            probeRender(engineC, kRenderSeconds, kSampleRate, kBlock, kNoiseSeed);

        CAPTURE(probeC.born, probeC.fingerprint.left.rms);
        REQUIRE(probeC.born > 0u);
        REQUIRE(probeC.fingerprint.left.rms > 1e-4);

        const auto left = compareFingerprints(probeC.fingerprint.left, probeA.fingerprint.left);
        const auto right = compareFingerprints(probeC.fingerprint.right, probeA.fingerprint.right);
        CAPTURE(left.worstMetricRelativeError, left.worstSampleError);
        CAPTURE(right.worstMetricRelativeError, right.worstSampleError);
        REQUIRE_FALSE(left.withinTolerance());
        REQUIRE_FALSE(right.withinTolerance());
    }

    // -------------------------------------------------------------------------
    // Clause 3 (FR-044): the blur stage NEVER draws from the grain-birth stream.
    // -------------------------------------------------------------------------
    // ASSERTED ON THE GRAIN STREAM'S STATE, NOT ON THE BIRTH COUNT, and the
    // difference is the whole point. Birth TIMING comes from GrainScheduler's
    // own private Xorshift32 rng_{12345} (processors/grain_scheduler.h:110,
    // drawn at :82), not from grainRng_. An implementation in which the blur
    // stage consumed from grainRng_ would therefore leave the NUMBER of births
    // completely unchanged and shift only each grain's position, detune, pan and
    // decorrelation - so a count-only comparison passes a genuinely broken
    // implementation. getGrainRngState() (core/random.h:79) is the only thing
    // that sees it.
    //
    // Both engines are prepared with blurEnabled = true, so the blur geometry is
    // live in both and only the blur AMOUNT differs. Since T012 landed the pump
    // the stage really does draw - one nextFloat() per bin per channel per frame,
    // unconditionally, so both legs have consumed hundreds of thousands of
    // values from blurRng_ by the end of a 20 s render. That all of that left
    // grainRng_ at exactly the same state, and produced exactly the same births,
    // is what proves the two streams are genuinely separate (FR-044).
    {
        AtmosphereEngine::PrepareConfig blurConfig = config;
        blurConfig.blurEnabled = true;
        blurConfig.blurFftSize = 1024;

        AtmosphereEngine dry;
        dry.prepare(kSampleRate, blurConfig);
        configureDeterminism(dry, kSeedA);
        dry.setBlur(0.0f);
        const DeterminismProbe dryProbe =
            probeRender(dry, kRenderSeconds, kSampleRate, kBlock, kNoiseSeed);

        AtmosphereEngine smeared;
        smeared.prepare(kSampleRate, blurConfig);
        configureDeterminism(smeared, kSeedA);
        smeared.setBlur(1.0f);
        const DeterminismProbe smearedProbe =
            probeRender(smeared, kRenderSeconds, kSampleRate, kBlock, kNoiseSeed);

        // Neither leg is silent, so "the grain streams agree" is a statement
        // about two engines that actually granulated.
        CAPTURE(dryProbe.born, smearedProbe.born, dryProbe.fingerprint.left.rms,
                smearedProbe.fingerprint.left.rms);
        REQUIRE(dryProbe.born > 0u);
        REQUIRE(dryProbe.fingerprint.left.rms > 1e-4);
        REQUIRE(smearedProbe.fingerprint.left.rms > 1e-4);

        REQUIRE(smearedProbe.grainRngState == dryProbe.grainRngState);
        REQUIRE(smearedProbe.born == dryProbe.born);
    }

    // -------------------------------------------------------------------------
    // Clause 4 (FR-006): reset() is a true rewind.
    // -------------------------------------------------------------------------
    // engineA has already rendered 20 s, so its scheduler jitter stream, grain
    // stream and 64 drift-lane streams are all deep into their sequences. If
    // reset() re-seeds every one of them - INCLUDING the explicit
    // scheduler_.seed(), which neither GrainScheduler::reset() nor its prepare()
    // performs - the second render is the first one again.
    {
        engineA.reset();
        REQUIRE(engineA.getTotalGrainsBorn() == 0u);
        REQUIRE(engineA.getActiveGrainCount() == 0u);

        const DeterminismProbe probeReset =
            probeRender(engineA, kRenderSeconds, kSampleRate, kBlock, kNoiseSeed);

        REQUIRE(probeReset.born == probeA.born);
        REQUIRE(probeReset.grainRngState == probeA.grainRngState);

        const auto left = compareFingerprints(probeReset.fingerprint.left, probeA.fingerprint.left);
        const auto right =
            compareFingerprints(probeReset.fingerprint.right, probeA.fingerprint.right);
        CAPTURE(left.worstMetricRelativeError, left.worstSampleError, left.detail);
        CAPTURE(right.worstMetricRelativeError, right.worstSampleError, right.detail);
        REQUIRE(left.withinTolerance());
        REQUIRE(right.withinTolerance());
    }
}

// =============================================================================
// SC-011 / FR-005 - AtmosphereEngine_BlockPartitionInvariance
// =============================================================================
// The render must not depend on how the caller cuts it into blocks. FR-005's
// control grid is anchored to the engine's ABSOLUTE sample counter, so a
// 64-sample chunk split 36 + 28 by a block boundary produces exactly the same
// control step as an unsplit 64. Two implementations fail this by orders of
// magnitude rather than by a rounding: one that runs its control step at every
// block start, and one that advances the drift bank "once per block" by
// numSamples (a lane value read after a 4096-sample advance is 4096 samples
// further along its walk than the same value read under 64-sample partitions,
// and that value scales a grain's pitch).
//
// BIT-IDENTICAL IS NOT REQUIRED AND NOT ASSERTED - the thresholds are an RMS
// difference at or below -100 dBFS and a maximum per-sample difference at or
// below 1e-5.
//
// DEVIATION FROM THE PLAN'S LITERAL "ONE 4096-SAMPLE CALL", RECORDED RATHER
// THAN TAKEN SILENTLY: the reference issues only 4096-sample calls, but it
// issues TWELVE of them (49 152 samples, 1.02 s) instead of one. At 48 kHz a
// single 4096-sample span is 85 ms, and the control table caps density at
// 20 grains/s (FR-009), so ONE such span can contain at most two births - too
// few to guarantee the required coverage below, which is the whole reason the
// clause exists. Twelve spans carry ~20 births. Nothing else changes: the
// reference still never issues a call of any other size.
TEST_CASE("AtmosphereEngine_BlockPartitionInvariance", "[atmosphere]") {
    using Krate::DSP::AtmosphereEngine;

    constexpr double kSampleRate = 48000.0;
    constexpr std::size_t kReferenceCall = 4096;
    constexpr std::size_t kWindows = 12;
    constexpr std::size_t kMeasured = kReferenceCall * kWindows;  // 49 152 samples
    constexpr std::size_t kPrerollBlock = 480;  // 10 ms; divides 24 000 exactly
    constexpr std::size_t kPreroll = 24000;     // 0.5 s, IDENTICAL in every run
    constexpr std::size_t kChunk = AtmosphereEngine::kControlChunkSamples;  // 64

    static_assert(kPreroll % kPrerollBlock == 0, "the pre-roll must divide into whole blocks");
    static_assert(kPreroll % kChunk == 0,
                  "the measured span must OPEN on a control-grid boundary, so a local sample "
                  "index and the engine's absolute sampleCounter_ agree modulo 64 - the "
                  "birth-coverage clause below reads local indices");

    // --- The stimulus, generated ONCE. Every run below must see bit-identical
    //     input, or the comparison measures the noise generator's block
    //     alignment rather than the engine's control grid.
    const std::size_t totalSamples = kPreroll + kMeasured;
    std::vector<float> stimulusL(totalSamples, 0.0f);
    std::vector<float> stimulusR(totalSamples, 0.0f);
    {
        Krate::DSP::Xorshift32 rng(13579u);
        for (std::size_t i = 0; i < totalSamples; ++i) {
            stimulusL[i] = rng.nextFloat() * 0.5f;
            stimulusR[i] = rng.nextFloat() * 0.5f;
        }
    }

    AtmosphereEngine::PrepareConfig config;
    config.captureSeconds = 8.0f;
    config.blurEnabled = false;
    config.freezeEnabled = false;
    config.maxBlockSamples = kReferenceCall;

    // Everything that carries state ACROSS a control chunk is switched on: the
    // drift bank (which must move, or its carry-over is untested), the scheduler
    // jitter, and all four per-grain birth draws.
    const auto configure = [](AtmosphereEngine& engine) {
        engine.setSeed(24680u);
        engine.setGrainSeconds(1.0f);
        engine.setDensity(20.0f);  // the control table's maximum: ~20 births/s
        engine.setJitter(0.5f);
        engine.setPositionSeconds(0.25f);
        engine.setPositionSpread(0.5f);
        engine.setPitchSpread(0.15f);
        engine.setDriftDepth(1.0f);
        engine.setDriftSmoothness(0.7f);
        engine.setDriftRangeSemitones(2.0f);
        engine.setPanSpread(0.7f);
        engine.setDecorrelation(0.5f);
        engine.setLevel(1.0f);
    };

    std::vector<float> prerollL(kPrerollBlock, 0.0f);
    std::vector<float> prerollR(kPrerollBlock, 0.0f);

    /// Render the measured span in `partition`-sample calls (the final call is
    /// the remainder) and return the total births. When `birthSamples` is given
    /// - only for the one-sample partitioning, where a call boundary IS a sample
    /// boundary - the local index of every birth is recorded.
    const auto run = [&](std::size_t partition, std::vector<float>& outLeft,
                         std::vector<float>& outRight,
                         std::vector<std::size_t>* birthSamples) -> std::uint64_t {
        AtmosphereEngine engine;
        engine.prepare(kSampleRate, config);
        configure(engine);

        // Pre-roll, byte-for-byte identical in every run: it only fills the ring
        // so the measured span opens with a live population. It is setup, not
        // part of what is compared.
        for (std::size_t s = 0; s < kPreroll; s += kPrerollBlock) {
            engine.processStereoBlock(stimulusL.data() + s, stimulusR.data() + s, prerollL.data(),
                                      prerollR.data(), kPrerollBlock);
        }

        outLeft.assign(kMeasured, 0.0f);
        outRight.assign(kMeasured, 0.0f);

        std::uint64_t previousBorn = engine.getTotalGrainsBorn();
        std::size_t done = 0;
        while (done < kMeasured) {
            const std::size_t n = std::min(partition, kMeasured - done);
            engine.processStereoBlock(stimulusL.data() + kPreroll + done,
                                      stimulusR.data() + kPreroll + done, outLeft.data() + done,
                                      outRight.data() + done, n);
            done += n;
            if (birthSamples != nullptr && engine.getTotalGrainsBorn() > previousBorn) {
                birthSamples->push_back(done - 1);
                previousBorn = engine.getTotalGrainsBorn();
            }
        }
        return engine.getTotalGrainsBorn();
    };

    // --- The reference: 4096-sample calls only.
    std::vector<float> referenceL;
    std::vector<float> referenceR;
    const std::uint64_t referenceBorn = run(kReferenceCall, referenceL, referenceR, nullptr);

    double referenceSquares = 0.0;
    for (std::size_t i = 0; i < kMeasured; ++i) {
        referenceSquares += (static_cast<double>(referenceL[i]) * referenceL[i]) +
                            (static_cast<double>(referenceR[i]) * referenceR[i]);
    }
    const double referenceRms =
        std::sqrt(referenceSquares / static_cast<double>(2 * kMeasured));

    // NON-VACUOUSNESS: a -100 dBFS difference between two silences is trivially
    // satisfied, so the reference must be loud and must have granulated.
    CAPTURE(referenceBorn, referenceRms, rmsToDb(referenceRms));
    REQUIRE(referenceBorn > 0u);
    REQUIRE(rmsToDb(referenceRms) > -60.0);

    // --- Every partitioning, against that reference.
    const std::array<std::size_t, 7> partitions{{1u, 7u, 64u, 65u, 511u, 512u, 1000u}};
    std::vector<std::size_t> birthSamples;

    for (const std::size_t partition : partitions) {
        std::vector<float> testL;
        std::vector<float> testR;
        const std::uint64_t born =
            run(partition, testL, testR, (partition == 1u) ? &birthSamples : nullptr);

        double diffSquares = 0.0;
        double maxDiff = 0.0;
        for (std::size_t i = 0; i < kMeasured; ++i) {
            const double dl = static_cast<double>(testL[i]) - static_cast<double>(referenceL[i]);
            const double dr = static_cast<double>(testR[i]) - static_cast<double>(referenceR[i]);
            diffSquares += (dl * dl) + (dr * dr);
            maxDiff = std::max(maxDiff, std::max(std::abs(dl), std::abs(dr)));
        }
        const double diffRms = std::sqrt(diffSquares / static_cast<double>(2 * kMeasured));

        CAPTURE(partition, born, referenceBorn, diffRms, rmsToDb(diffRms), maxDiff);
        // The engine must not merely AGREE - it must have done the same work.
        // A partitioning that skipped grains would agree on silence.
        REQUIRE(born == referenceBorn);
        REQUIRE(rmsToDb(diffRms) <= -100.0);
        REQUIRE(maxDiff <= 1e-5);
    }

    // --- REQUIRED COVERAGE: at least one grain born INSIDE a partial control
    //     chunk, so FR-030's carry-over path is exercised rather than assumed.
    //     The measured span opens on a control-grid boundary (static_assert
    //     above), so a local index that is not a multiple of 64 is a birth the
    //     one-sample partitioning cut a chunk around.
    std::size_t offGridBirths = 0;
    for (const std::size_t sampleIndex : birthSamples) {
        if ((sampleIndex % kChunk) != 0) {
            ++offGridBirths;
        }
    }
    CAPTURE(birthSamples.size(), offGridBirths);
    // ~20 births are expected at 20 grains/s over 1.02 s; 8 is that with a wide
    // margin, and it is what makes "at least one landed off the grid" a real
    // statement rather than a coin flip.
    REQUIRE(birthSamples.size() >= 8u);
    REQUIRE(offGridBirths > 0u);
}

// =============================================================================
// FR-070 edge - AtmosphereEngine_SeedZeroIsValid
// =============================================================================
// setSeed(0) IS A VALID, DISTINCT ENGINE SEED. Xorshift32::seed() silently
// substitutes its own default for 0 (core/random.h:73-75), so an engine that
// handed raw seed values to its RNGs would put setSeed(0) and any other seed
// that happened to hash to 0 on ONE stream. deriveStreamSeed (:102-111) is what
// prevents it: a lowbias32 finaliser with its own explicit non-zero
// substitution, so base 0 and base 1 land on different, non-zero streams.

TEST_CASE("AtmosphereEngine_SeedZeroIsValid", "[atmosphere]") {
    using Krate::DSP::AtmosphereEngine;
    using Krate::DSP::TestUtils::compareFingerprints;

    constexpr double kSampleRate = 48000.0;
    constexpr std::size_t kBlock = 512;
    constexpr double kRenderSeconds = 8.0;
    constexpr std::uint32_t kNoiseSeed = 424242u;

    // The mechanism, stated directly, so a failure below is immediately either
    // "the derivation collapsed" or "the engine did not use the derivation".
    REQUIRE(Krate::DSP::deriveStreamSeed(0u, AtmosphereEngine::kGrainSalt) != 0u);
    REQUIRE(Krate::DSP::deriveStreamSeed(0u, AtmosphereEngine::kGrainSalt) !=
            Krate::DSP::deriveStreamSeed(1u, AtmosphereEngine::kGrainSalt));

    AtmosphereEngine::PrepareConfig config;
    config.captureSeconds = 8.0f;
    config.blurEnabled = false;
    config.freezeEnabled = false;

    AtmosphereEngine zeroSeeded;
    zeroSeeded.prepare(kSampleRate, config);
    configureDeterminism(zeroSeeded, 0u);
    REQUIRE(zeroSeeded.getSeed() == 0u);  // stored verbatim; only the DERIVATION substitutes
    const DeterminismProbe zeroProbe =
        probeRender(zeroSeeded, kRenderSeconds, kSampleRate, kBlock, kNoiseSeed);

    AtmosphereEngine oneSeeded;
    oneSeeded.prepare(kSampleRate, config);
    configureDeterminism(oneSeeded, 1u);
    REQUIRE(oneSeeded.getSeed() == 1u);
    const DeterminismProbe oneProbe =
        probeRender(oneSeeded, kRenderSeconds, kSampleRate, kBlock, kNoiseSeed);

    // Not a comparison of two silences: seed 0 and seed 1 both granulate.
    CAPTURE(zeroProbe.born, oneProbe.born, zeroProbe.fingerprint.left.rms,
            oneProbe.fingerprint.left.rms);
    REQUIRE(zeroProbe.born > 0u);
    REQUIRE(oneProbe.born > 0u);
    REQUIRE(zeroProbe.fingerprint.left.rms > 1e-4);
    REQUIRE(oneProbe.fingerprint.left.rms > 1e-4);

    const auto left = compareFingerprints(zeroProbe.fingerprint.left, oneProbe.fingerprint.left);
    const auto right = compareFingerprints(zeroProbe.fingerprint.right, oneProbe.fingerprint.right);
    CAPTURE(left.worstMetricRelativeError, left.worstSampleError);
    CAPTURE(right.worstMetricRelativeError, right.worstSampleError);
    REQUIRE_FALSE(left.withinTolerance());
    REQUIRE_FALSE(right.withinTolerance());
}

// =============================================================================
// T011 helpers - SC-001 / SC-008
// =============================================================================

namespace {

/// Finiteness for THIS TU, which is compiled WITH fast-math (see the file
/// banner): std::isnan / std::isinf / std::isfinite are folded away here and
/// std::numeric_limits<float>::infinity() does not survive to be compared
/// against. These are the two Layer 0 exponent-field bit tests the whole phase
/// uses - Krate::DSP::detail::isNaN (core/db_utils.h:54-57) and
/// Krate::DSP::detail::isInf (:175-178). FR-008 forbids writing another one, so
/// this is a COMPOSITION of the existing pair and not a new bit test.
[[nodiscard]] bool sampleIsFinite(float value) noexcept {
    return !Krate::DSP::detail::isNaN(value) && !Krate::DSP::detail::isInf(value);
}

/// Block index at which `seconds` elapses, at 48 kHz in 512-sample blocks. Both
/// cases below pin their call schedule in BLOCK INDICES, so the schedule is a
/// property of the test rather than of how fast the machine runs.
[[nodiscard]] constexpr std::size_t blockAt(double seconds) noexcept {
    return static_cast<std::size_t>((seconds * 48000.0) / 512.0);
}

}  // namespace

// =============================================================================
// SC-001 - AtmosphereEngine_NoAllocationAfterPrepare
// =============================================================================
// Roadmap line 246: "zero allocation after prepare". FR-003 makes prepare() the
// ONLY non-RT-safe method; every other entry point must be allocation-free,
// lock-free, exception-free and I/O-free (FR-008).
//
// THE WORST CASE IS THE POINT. captureSeconds = 30 with grainSeconds = 30 and
// density = 20 puts the mean concurrent count at 20 x 30 = 600 against a pool
// of kMaxGrains = 64 (C-8), so the run sustains BOTH skip paths: FR-014's
// cold-ring rejection while the ring fills, then FR-023's skip-never-steal for
// the rest of the render. A pool that STOLE instead of skipping would still be
// allocation-free, which is exactly why the precondition below is on
// getSkippedTriggerCountPoolFull() specifically and not on a conflated counter:
// without it this case can pass having exercised only FR-014's path and never
// FR-023's - the path this worst case exists to stress.
//
// WHY poolFullBeforeReset IS LATCHED RATHER THAN READ AT THE END. reset()
// (FR-006) zeroes both skip counters, and re-saturating afterwards costs the
// ring-refill window plus 64 / 20 = 3.2 s of births. Asserting the LIVE counter
// after a reset() at t = 4 s and a silence() at t = 9 s would be a timing
// lottery, so the value is captured immediately before reset() runs.
//
// WHY THE COUNT IS READ FROM THE DETECTOR SINGLETON AND NOT FROM THE SCOPE.
// TestHelpers::AllocationScope latches its count in its DESTRUCTOR
// (tests/test_helpers/allocation_detector.h:81-83), so scope.getAllocationCount()
// reads 0 for as long as the scope object is still reachable. The live count is
// read from AllocationDetector::instance() (:48-50) while the scope is open -
// the same counter over the same tracking window, just readable in time to use.
//
// EVERY ASSERTION IS OUTSIDE THE TRACKED REGION. Catch2's REQUIRE / CAPTURE
// machinery allocates, so the render loop records into plain locals and every
// check runs after the scope has closed.
//
// The engine, the I/O buffers and the noise generator are all constructed
// BEFORE the scope opens - prepare() is allowed to allocate, and does.

TEST_CASE("AtmosphereEngine_NoAllocationAfterPrepare", "[atmosphere]") {
    using Krate::DSP::AtmosphereEngine;
    using Krate::DSP::GrainEnvelopeType;

    constexpr double kSampleRate = 48000.0;
    constexpr std::size_t kBlock = 512;
    constexpr std::size_t kTotalBlocks = 938;            // 480 256 samples = 10.005 s
    constexpr std::size_t kResetBlock = blockAt(4.0);    // 375, i.e. t = 4.000 s exactly
    constexpr std::size_t kSilenceBlock = blockAt(9.0);  // 843, t = 8.992 s
    // kSilenceRampMs = 10 ms = 480 samples, so the latch completes inside the
    // two blocks 843-844. Everything from block 846 on must be exactly 0.0f.
    constexpr std::size_t kLatchedTailFirstBlock = 846;

    AtmosphereEngine::PrepareConfig config;
    config.captureSeconds = 30.0f;  // C = 2 097 152 samples = 43.69 s at 48 kHz
    config.blurEnabled = true;
    config.freezeEnabled = true;

    AtmosphereEngine engine;
    engine.prepare(kSampleRate, config);
    REQUIRE(engine.getCaptureCapacitySamples() == 2097152u);
    REQUIRE(engine.getFreezeFftSize() == 2048u);

    std::vector<float> inLeft(kBlock, 0.0f);
    std::vector<float> inRight(kBlock, 0.0f);
    std::vector<float> outLeft(kBlock, 0.0f);
    std::vector<float> outRight(kBlock, 0.0f);
    Krate::DSP::Xorshift32 noise(20260728u);

    std::size_t liveAllocationCount = 0;
    std::uint64_t poolFullBeforeReset = 0;
    std::uint64_t bornBeforeReset = 0;
    std::uint64_t bornAtEnd = 0;
    std::size_t maxActive = 0;
    bool freezeArmed = false;
    bool releaseObserved = false;
    bool latchedTailExactlyZero = true;
    bool allSamplesFinite = true;

    {
        // Constructing this calls AllocationDetector::startTracking()
        // (allocation_detector.h:77-79). From here to the closing brace nothing
        // may reach the heap.
        [[maybe_unused]] const TestHelpers::AllocationScope scope;

        for (std::size_t b = 0; b < kTotalBlocks; ++b) {
            // ---------------------------------------------------------------
            // The pinned call schedule. Blocks 1-18 are the FULL setter sweep
            // (all inside t < 0.2 s), and it is also what establishes the worst
            // case:
            //   - grainSeconds 30 + density 20 => 600 mean concurrent against a
            //     pool of 64, so FR-023's path is sustained for the whole run;
            //   - pitchSemitones -7 with driftRangeSemitones 4 keeps the entire
            //     ratio envelope BELOW 1 (semisHi <= -1.8), so FR-025's
            //     wUp = (rMax - 1)+ is 0 and aLo stays at kMinAgeSamples. With
            //     the default upward-straddling envelope aLo would be ~355 000
            //     samples (7.4 s of ring), no grain could be born before
            //     t = 7.4 s, the pool would never saturate before the reset at
            //     t = 4 s, and the precondition below would be unsatisfiable;
            //   - positionSeconds 0.05 keeps the cold-ring window short
            //     (admission needs ~4 300 samples), so births start at
            //     t ~ 0.09 s and 64 of them at 20 grains/s saturate the pool at
            //     t ~ 3.3 s, comfortably before the reset at t = 4.0 s.
            // setGrainEnvelope is called TWICE with two DIFFERENT types: its
            // idempotence guard skips regenerateEnvelope() entirely for a
            // repeated value, and regenerateEnvelope() writing 4096 entries IN
            // PLACE (never resize) is one of the things this case exists to
            // prove.
            // ---------------------------------------------------------------
            switch (b) {
                case 1: engine.setGrainSeconds(30.0f); break;
                case 2: engine.setDensity(20.0f); break;
                case 3: engine.setPitchSemitones(-7.0f); break;
                case 4: engine.setDriftRangeSemitones(4.0f); break;
                case 5: engine.setPitchSpread(0.1f); break;
                case 6: engine.setPositionSeconds(0.05f); break;
                case 7: engine.setPositionSpread(0.2f); break;
                case 8: engine.setJitter(1.0f); break;
                case 9: engine.setDriftDepth(1.0f); break;
                case 10: engine.setDriftSmoothness(0.9f); break;
                case 11: engine.setPanSpread(1.0f); break;
                case 12: engine.setDecorrelation(1.0f); break;
                case 13: engine.setBlur(1.0f); break;
                case 14: engine.setFreezeMix(0.5f); break;
                case 15: engine.setLevel(1.0f); break;
                case 16: engine.setGrainEnvelope(GrainEnvelopeType::Blackman); break;
                case 17: engine.setGrainEnvelope(GrainEnvelopeType::Exponential); break;
                case 18: engine.setSeed(987654321u); break;
                // Freeze arm / release, t in [1, 3] s. By then the ring holds
                // far more than the 2048-sample freeze window, so captureFreeze()
                // takes its extractSlice + FFT path rather than its no-op
                // early-out; isFreezeCaptured() is what proves which.
                case 94:
                    engine.captureFreeze();
                    freezeArmed = engine.isFreezeCaptured();
                    break;
                case 188:
                    engine.releaseFreeze();
                    releaseObserved = true;
                    break;
                case 281:
                    engine.captureFreeze();
                    freezeArmed = freezeArmed && engine.isFreezeCaptured();
                    break;
                default: break;
            }

            if (b == kResetBlock) {
                poolFullBeforeReset = engine.getSkippedTriggerCountPoolFull();
                bornBeforeReset = engine.getTotalGrainsBorn();
                engine.reset();
            }
            if (b == kSilenceBlock) {
                engine.silence();
            }

            for (std::size_t i = 0; i < kBlock; ++i) {
                inLeft[i] = noise.nextFloat();
                inRight[i] = noise.nextFloat();
            }
            engine.processStereoBlock(inLeft.data(), inRight.data(), outLeft.data(),
                                      outRight.data(), kBlock);

            for (std::size_t i = 0; i < kBlock; ++i) {
                if (!sampleIsFinite(outLeft[i]) || !sampleIsFinite(outRight[i])) {
                    allSamplesFinite = false;
                }
                if (b >= kLatchedTailFirstBlock &&
                    (outLeft[i] != 0.0f || outRight[i] != 0.0f)) {
                    latchedTailExactlyZero = false;
                }
            }
            maxActive = std::max(maxActive, engine.getActiveGrainCount());
        }

        bornAtEnd = engine.getTotalGrainsBorn();
        liveAllocationCount = TestHelpers::AllocationDetector::instance().getAllocationCount();
    }

    // --- The criterion ------------------------------------------------------
    CAPTURE(liveAllocationCount, poolFullBeforeReset, bornBeforeReset, bornAtEnd, maxActive);
    REQUIRE(liveAllocationCount == 0);

    // --- Preconditions: the worst case really was reached -------------------
    // FR-023's skip-never-steal path, proved through the counter that CANNOT be
    // satisfied by FR-014's cold-ring path (FR-072 keeps the two separate).
    REQUIRE(poolFullBeforeReset > 0);
    REQUIRE(maxActive == AtmosphereEngine::kMaxGrains);
    REQUIRE(bornBeforeReset >= static_cast<std::uint64_t>(AtmosphereEngine::kMaxGrains));
    // reset() zeroed the counters at t = 4 s and the engine granulated again
    // afterwards, so the post-reset 5 s were not a silent coast.
    REQUIRE(bornAtEnd > 0);
    // captureFreeze() took its real path both times, and releaseFreeze() ran.
    REQUIRE(freezeArmed);
    REQUIRE(releaseObserved);
    // FR-007's latch is inside the tracked region: no allocation on the way in,
    // and nothing but exact zeros on the way out.
    REQUIRE(latchedTailExactlyZero);
    REQUIRE(allSamplesFinite);
}

// =============================================================================
// SC-008 - AtmosphereEngine_BoundedUnderStress
// =============================================================================
// Peak |y| over a TEN-MINUTE render of full-scale white noise, at the control
// table's maxima, must stay below 4.0, and every sample must be finite.
//
// WHY 4.0 IS THE STATISTICAL BOUND AND NOT THE ANALYTIC ONE.
// FR-028's 1/sqrt(n) normalisation bounds n COHERENT grains at
// sqrt(n) * level = sqrt(64) * 1.0 = 8.0, so 4.0 is NOT implied by the
// normalisation law. The real argument is incoherence: grains are born at
// independent ring positions (FR-029), with independent pitches (FR-031),
// independent drift lanes (FR-030) and independent L/R read-age offsets
// (FR-033), so they sum incoherently. With 1/sqrt(n) applied ONCE on the summed
// stereo bus (FR-028) - and with FR-034's per-grain amplitude term deleted, so
// every grain contributes with unit weight - the sum has approximately unit
// variance regardless of n, and its peak over N samples grows as about
// sigma * sqrt(2 ln N), i.e. ~5.1 sigma over the 28 800 000 samples rendered
// here. 4.0 is therefore a genuine statistical bound with margin against a
// runaway, while 8.0 is the coherent worst case that no realistic configuration
// reaches.
//
// IF A MEASURED RUN APPROACHES 4.0, INVESTIGATE COHERENCE - all grains reading
// the same ring position at r = 1 is the failure this threshold watches for.
// DO NOT RAISE THE THRESHOLD.
//
// level = 1.0 IS PINNED rather than defaulted into: it multiplies the threshold
// directly, and FR-009's maximum of 2.0 would double the bound with no defect
// present. There is no width control to pin (FR-060 / N-9).
//
// positionSeconds = 15 with positionSpread = 0.5 is pinned for the same reason
// in the other direction: it spreads births over 15 s of ring, which is what
// makes the incoherence assumption above true of the configuration actually
// measured. A small position with a small spread would put every grain on
// nearly the same audio and would measure a different, easier thing.

TEST_CASE("AtmosphereEngine_BoundedUnderStress", "[atmosphere]") {
    using Krate::DSP::AtmosphereEngine;

    constexpr double kSampleRate = 48000.0;
    constexpr std::size_t kBlock = 512;
    constexpr std::size_t kTotalBlocks = 56250;  // 28 800 000 samples = 600 s
    constexpr float kPeakBound = 4.0f;

    // Freeze is captured once the ring is comfortably warm, then crossfaded
    // 0 -> 1 -> 0 -> ... every 5 s for the rest of the render.
    constexpr std::size_t kFreezeCaptureBlock = blockAt(30.0);  // 2812
    constexpr std::size_t kFreezeTogglePeriod = blockAt(5.0);   // 468

    AtmosphereEngine::PrepareConfig config;
    config.captureSeconds = 30.0f;
    config.blurEnabled = true;
    config.freezeEnabled = true;

    AtmosphereEngine engine;
    engine.prepare(kSampleRate, config);

    engine.setDensity(20.0f);       // FR-009 maximum
    engine.setGrainSeconds(30.0f);  // FR-009 maximum
    engine.setJitter(1.0f);
    engine.setPositionSeconds(15.0f);
    engine.setPositionSpread(0.5f);
    engine.setDriftDepth(1.0f);     // FR-009 maximum
    engine.setDriftSmoothness(0.5f);
    engine.setPanSpread(1.0f);
    engine.setDecorrelation(1.0f);  // FR-009 maximum
    engine.setBlur(1.0f);           // FR-009 maximum
    engine.setLevel(1.0f);          // PINNED - see the note above
    engine.setSeed(1357911u);

    std::vector<float> inLeft(kBlock, 0.0f);
    std::vector<float> inRight(kBlock, 0.0f);
    std::vector<float> outLeft(kBlock, 0.0f);
    std::vector<float> outRight(kBlock, 0.0f);
    // Independent noise per channel: the adversarial input, not a correlated
    // pair the engine might accidentally cancel.
    Krate::DSP::Xorshift32 noise(99887766u);

    float peak = 0.0f;
    std::uint64_t nonFiniteSamples = 0;
    std::size_t firstNonFiniteBlock = kTotalBlocks;
    std::size_t maxActive = 0;
    bool freezeArmed = false;
    bool freezeUp = false;

    for (std::size_t b = 0; b < kTotalBlocks; ++b) {
        if (b == kFreezeCaptureBlock) {
            engine.captureFreeze();
            freezeArmed = engine.isFreezeCaptured();
            engine.setFreezeMix(1.0f);
            freezeUp = true;
        } else if (b > kFreezeCaptureBlock &&
                   ((b - kFreezeCaptureBlock) % kFreezeTogglePeriod) == 0) {
            freezeUp = !freezeUp;
            engine.setFreezeMix(freezeUp ? 1.0f : 0.0f);
        }

        for (std::size_t i = 0; i < kBlock; ++i) {
            inLeft[i] = noise.nextFloat();  // full scale, [-1, 1]
            inRight[i] = noise.nextFloat();
        }
        engine.processStereoBlock(inLeft.data(), inRight.data(), outLeft.data(), outRight.data(),
                                  kBlock);

        for (std::size_t i = 0; i < kBlock; ++i) {
            const float left = outLeft[i];
            const float right = outRight[i];
            if (!sampleIsFinite(left) || !sampleIsFinite(right)) {
                ++nonFiniteSamples;
                if (firstNonFiniteBlock == kTotalBlocks) {
                    firstNonFiniteBlock = b;
                }
                continue;  // a non-finite value must not poison the peak
            }
            peak = std::max(peak, std::max(std::abs(left), std::abs(right)));
        }
        maxActive = std::max(maxActive, engine.getActiveGrainCount());
    }

    const std::uint64_t born = engine.getTotalGrainsBorn();
    const std::uint64_t poolFull = engine.getSkippedTriggerCountPoolFull();
    CAPTURE(peak, nonFiniteSamples, firstNonFiniteBlock, maxActive, born, poolFull);

    // Non-vacuousness FIRST: a silent engine passes every bound below.
    REQUIRE(born > 0);
    REQUIRE(maxActive == AtmosphereEngine::kMaxGrains);
    REQUIRE(poolFull > 0);
    REQUIRE(freezeArmed);
    REQUIRE(peak > 0.05f);

    // The criterion.
    REQUIRE(nonFiniteSamples == 0);
    REQUIRE(peak < kPeakBound);
}

// =============================================================================
// T015 helpers - SC-014's FOURTH CLAUSE
// =============================================================================

namespace {

/// @brief 220 Hz fundamental plus partials 2x..9x at 1/n amplitude, sine, zero
///        phase, both channels identical, trimmed to a peak below 0.5.
///
/// A file-local copy of the generator the nonfinite TU uses for the same
/// injection, so the two renders differ ONLY in their compile flags - which is
/// the whole point of this clause. Phase is formed from the ABSOLUTE sample
/// index in double, never accumulated, so a multi-block render carries no drift
/// and the two engines below see bit-identical input.
///
/// sum(1/n, n=1..9) = 2.829 bounds |sum| absolutely; the truncated sawtooth
/// series peaks near 1.85, so the 0.25 trim keeps the input below 0.47.
void fastMathHarmonicStack(float* outLeft, float* outRight, std::size_t numSamples,
                           std::size_t startSample, double sampleRate) noexcept {
    constexpr int kPartials = 9;
    constexpr double kFundamental = 220.0;
    constexpr double kTwoPiDouble = 6.283185307179586476925286766559;
    constexpr float kTrim = 0.25f;

    for (std::size_t i = 0; i < numSamples; ++i) {
        const double t = static_cast<double>(startSample + i) / sampleRate;
        double sum = 0.0;
        for (int n = 1; n <= kPartials; ++n) {
            const double nd = static_cast<double>(n);
            sum += std::sin(kTwoPiDouble * kFundamental * nd * t) / nd;
        }
        const float value = static_cast<float>(sum) * kTrim;
        outLeft[i] = value;
        outRight[i] = value;
    }
}

/// Normalised cross-product over [first, last). Un-centred deliberately: a
/// non-finite sample propagates into the result, so a buffer that failed the
/// finiteness clause cannot quietly pass the ring-preservation clause. Returns 0
/// for an empty or silent window, which FAILS the >= 0.99 gate.
[[nodiscard]] double fastMathWindowCorrelation(const std::vector<float>& a,
                                               const std::vector<float>& b, std::size_t first,
                                               std::size_t last) {
    if (last <= first || last > a.size() || last > b.size()) {
        return 0.0;
    }
    double sumAA = 0.0;
    double sumBB = 0.0;
    double sumAB = 0.0;
    for (std::size_t i = first; i < last; ++i) {
        const double x = static_cast<double>(a[i]);
        const double y = static_cast<double>(b[i]);
        sumAA += x * x;
        sumBB += y * y;
        sumAB += x * y;
    }
    const double denominator = std::sqrt(sumAA * sumBB);
    return (denominator > 0.0) ? (sumAB / denominator) : 0.0;
}

[[nodiscard]] double fastMathWindowRms(const std::vector<float>& v, std::size_t first,
                                       std::size_t last) {
    if (last <= first || last > v.size()) {
        return 0.0;
    }
    double sumSquares = 0.0;
    for (std::size_t i = first; i < last; ++i) {
        const double x = static_cast<double>(v[i]);
        sumSquares += x * x;
    }
    return std::sqrt(sumSquares / static_cast<double>(last - first));
}

[[nodiscard]] bool everySampleFinite(const std::vector<float>& v) {
    for (const float sample : v) {
        if (!sampleIsFinite(sample)) {
            return false;
        }
    }
    return true;
}

}  // namespace

// =============================================================================
// SC-014, FOURTH CLAUSE - AtmosphereEngine_NonFiniteGuardSurvivesFastMath
// =============================================================================
// THIS CASE IS THE ONE WITH TEETH, AND IT IS HERE RATHER THAN IN THE NONFINITE
// TU FOR EXACTLY ONE REASON (plan S15.7, S16.1(2)).
//
// SC-014's clauses (a)-(c) live in dsp/tests/unit/systems/
// atmosphere_engine_nonfinite_test.cpp, which dsp/tests/CMakeLists.txt compiles
// with -fno-fast-math -fno-finite-math-only. That is the one configuration the
// shipped header will NEVER be compiled in: this repo builds MSVC with /fp:fast
// and the macOS leg with -ffast-math (via the VST3 SDK's global flags). The two
// guards that make FR-063 work exist specifically to survive that:
//
//   1. AtmosphereEngine::isFinite is ITERUM_NOINLINE (plan S13.2,
//      primitives/smoother.h:37-45 - "Required to prevent branch elimination
//      with NaN checks under /fp:fast"), because core/db_utils.h:44-52 requires
//      -fno-fast-math of every source file using detail::isNaN / detail::isInf
//      and a header cannot impose that on its consumers.
//   2. RollingCaptureBuffer::readStereoLinear's age clamp is two ORDERED
//      COMPARISONS rather than any FP-classification predicate (plan S2, S13.2),
//      so -ffinite-math-only has nothing to fold.
//
// SC-013's scripted grep over the header sees which SYMBOLS were named. It
// cannot see whether the branch survived optimisation. If either guard is folded
// away, THIS case fails while the -fno-fast-math copy keeps passing - so do not
// "tidy" it by moving it next to its siblings, and do not add this TU to the
// -fno-fast-math list.
//
// It repeats clause (a)'s input injection and clause (b)'s ring-preservation
// check VERBATIM. makeNonFinite's volatile + memcpy construction (:116) works
// identically here - defeating constant folding is exactly its purpose - so the
// injection really does reach the engine on both legs. Clause (c)'s ClickDetector
// half is NOT repeated: it measures the granulation's own artifact floor, which
// is not a property of the finiteness guards, and it would drag this TU into a
// dependency on artifact_detection.h for no added coverage.

TEST_CASE("AtmosphereEngine_NonFiniteGuardSurvivesFastMath", "[atmosphere]") {
    using Krate::DSP::AtmosphereEngine;

    constexpr double kSampleRate = 48000.0;
    constexpr std::size_t kBlock = 512;
    constexpr std::size_t kWarmBlocks = 188;  // 96 256 samples = 2.005 s
    constexpr std::size_t kTailBlocks = 60;   // 30 720 samples = 0.640 s
    constexpr std::size_t kTotalBlocks = kWarmBlocks + 1 + kTailBlocks;
    constexpr std::size_t kInjectSample = kWarmBlocks * kBlock;

    // The ring-preservation window, 47 blocks = 0.501 s, starting at the
    // injection. positionSeconds = 1.0 with positionSpread = 0, pitch 0,
    // pitchSpread 0 and driftRangeSemitones 0 pins every grain's ratio at
    // exactly 1.0, so the age moves by 1 - r == 0 per sample and every grain
    // reads at a CONSTANT age of 48 000 samples. Output at sample u therefore
    // reproduces ring content written at u - 48 000, and this whole window reads
    // [48 256, 72 320) - written before the injection at 96 256.
    constexpr std::size_t kWindowFirst = kInjectSample;
    constexpr std::size_t kWindowLast = kInjectSample + 47 * kBlock;

    AtmosphereEngine::PrepareConfig config;
    config.captureSeconds = 8.0f;
    config.blurEnabled = false;
    config.freezeEnabled = false;

    AtmosphereEngine injected;
    AtmosphereEngine reference;
    for (AtmosphereEngine* engine : {&injected, &reference}) {
        engine->prepare(kSampleRate, config);
        engine->setDensity(8.0f);
        engine->setGrainSeconds(0.5f);
        engine->setPositionSeconds(1.0f);
        engine->setPositionSpread(0.0f);
        engine->setPitchSemitones(0.0f);
        engine->setPitchSpread(0.0f);
        engine->setDriftDepth(0.0f);
        engine->setDriftRangeSemitones(0.0f);
        engine->setPanSpread(0.5f);
        engine->setDecorrelation(0.0f);
        engine->setLevel(1.0f);
    }

    const std::size_t capacityBefore = injected.getCaptureCapacitySamples();
    REQUIRE(capacityBefore > 0u);
    REQUIRE(reference.getCaptureCapacitySamples() == capacityBefore);

    std::vector<float> cleanLeft(kBlock, 0.0f);
    std::vector<float> cleanRight(kBlock, 0.0f);
    std::vector<float> dirtyLeft(kBlock, 0.0f);
    std::vector<float> dirtyRight(kBlock, 0.0f);
    std::vector<float> blockOutLeft(kBlock, 0.0f);
    std::vector<float> blockOutRight(kBlock, 0.0f);

    std::vector<float> injectedLeft;
    std::vector<float> injectedRight;
    std::vector<float> referenceLeft;
    std::vector<float> referenceRight;
    injectedLeft.reserve(kTotalBlocks * kBlock);
    injectedRight.reserve(kTotalBlocks * kBlock);
    referenceLeft.reserve(kTotalBlocks * kBlock);
    referenceRight.reserve(kTotalBlocks * kBlock);

    bool injectedInputWasNonFinite = false;

    for (std::size_t b = 0; b < kTotalBlocks; ++b) {
        if (b <= kWarmBlocks) {
            fastMathHarmonicStack(cleanLeft.data(), cleanRight.data(), kBlock, b * kBlock,
                                  kSampleRate);
        } else {
            std::fill(cleanLeft.begin(), cleanLeft.end(), 0.0f);
            std::fill(cleanRight.begin(), cleanRight.end(), 0.0f);
        }

        const float* inLeft = cleanLeft.data();
        const float* inRight = cleanRight.data();
        if (b == kWarmBlocks) {
            // 0x7FC00000 = quiet NaN, 0x7F800000 = +Inf, 0xFF800000 = -Inf.
            // Scattered across several 64-sample control chunks plus one
            // contiguous run covering a WHOLE chunk, so both the per-sample
            // substitution and the whole-chunk slow path are exercised.
            dirtyLeft = cleanLeft;
            dirtyRight = cleanRight;
            dirtyLeft[100] = makeNonFinite(0x7FC00000u);
            dirtyRight[101] = makeNonFinite(0x7F800000u);
            dirtyLeft[200] = makeNonFinite(0xFF800000u);
            dirtyRight[200] = makeNonFinite(0xFF800000u);
            dirtyLeft[201] = makeNonFinite(0x7F800000u);
            dirtyRight[201] = makeNonFinite(0x7FC00000u);
            for (std::size_t i = 320; i < 384; ++i) {
                dirtyLeft[i] = makeNonFinite(0x7FC00000u);
                dirtyRight[i] = makeNonFinite(0xFF800000u);
            }
            dirtyLeft[460] = makeNonFinite(0x7F800000u);
            dirtyRight[461] = makeNonFinite(0xFF800000u);
            for (std::size_t i = 0; i < kBlock; ++i) {
                if (!sampleIsFinite(dirtyLeft[i]) || !sampleIsFinite(dirtyRight[i])) {
                    injectedInputWasNonFinite = true;
                }
            }
            inLeft = dirtyLeft.data();
            inRight = dirtyRight.data();
        }

        injected.processStereoBlock(inLeft, inRight, blockOutLeft.data(), blockOutRight.data(),
                                    kBlock);
        injectedLeft.insert(injectedLeft.end(), blockOutLeft.begin(), blockOutLeft.end());
        injectedRight.insert(injectedRight.end(), blockOutRight.begin(), blockOutRight.end());

        reference.processStereoBlock(cleanLeft.data(), cleanRight.data(), blockOutLeft.data(),
                                     blockOutRight.data(), kBlock);
        referenceLeft.insert(referenceLeft.end(), blockOutLeft.begin(), blockOutLeft.end());
        referenceRight.insert(referenceRight.end(), blockOutRight.begin(), blockOutRight.end());
    }

    // --- Non-vacuousness FIRST. THE INJECTION MUST HAVE SURVIVED THE COMPILER:
    //     if -ffast-math had folded makeNonFinite's construction to finite
    //     garbage this flag would be false, and the whole case would be
    //     measuring nothing.
    const std::uint64_t born = injected.getTotalGrainsBorn();
    const double referenceRms = fastMathWindowRms(referenceLeft, kWindowFirst, kWindowLast);
    CAPTURE(born, referenceRms);
    REQUIRE(injectedInputWasNonFinite);
    REQUIRE(born > 0u);
    REQUIRE(reference.getTotalGrainsBorn() == born);
    REQUIRE(rmsToDb(referenceRms) > -60.0);

    // --- Clause (a), under /fp:fast and -ffast-math: EVERY output sample
    //     finite. This is where a folded-away ITERUM_NOINLINE isFinite surfaces.
    REQUIRE(everySampleFinite(injectedLeft));
    REQUIRE(everySampleFinite(injectedRight));
    REQUIRE(everySampleFinite(referenceLeft));
    REQUIRE(everySampleFinite(referenceRight));

    // --- Clause (b), under the same flags: the ring is PRESERVED, so grains
    //     still reproduce the pre-injection audio.
    const double correlationLeft =
        fastMathWindowCorrelation(injectedLeft, referenceLeft, kWindowFirst, kWindowLast);
    const double correlationRight =
        fastMathWindowCorrelation(injectedRight, referenceRight, kWindowFirst, kWindowLast);
    CAPTURE(correlationLeft, correlationRight);
    REQUIRE(correlationLeft >= 0.99);
    REQUIRE(correlationRight >= 0.99);

    REQUIRE(injected.getCaptureCapacitySamples() == capacityBefore);
    REQUIRE(reference.getCaptureCapacitySamples() == capacityBefore);
}

// =============================================================================
// T016 helpers - SC-009
// =============================================================================

namespace {

/// The three rates SC-009 sweeps, in this order. Index 0 (44.1 kHz) is the
/// cross-rate REFERENCE every clause-1 comparison is made against; it is also
/// the rate whose power-of-two ring rounding differs from the other two, which
/// is what gives clause 2 its teeth (RA-2).
constexpr std::array<double, 3> kSampleRateSweep{44100.0, 48000.0, 96000.0};

constexpr float kRateSweepCaptureSeconds = 8.0f;
constexpr float kRateSweepPositionSeconds = 0.5f;
constexpr float kRateSweepPositionSpread = 0.2f;
constexpr std::uint32_t kRateSweepSeed = 20260728u;
constexpr std::uint32_t kRateSweepNoiseSeed = 4242u;

/// 128 samples, not 512. The mean concurrent count is a TIME average of a
/// quantity that steps between floor(density*grainSeconds) and its ceiling once
/// per interonset interval, and it is only observable once per
/// processStereoBlock. A 128-sample block puts >= 30 observations inside every
/// oscillation period at every rate below, so the 5 % threshold is measuring
/// the engine rather than the block grid's aliasing against the grain grid.
constexpr std::size_t kRateSweepBlock = 128;
constexpr double kRateSweepRenderSeconds = 8.0;
/// Births start at ~positionSeconds*(1+spread) = 0.6 s and the population is in
/// steady state one grain lifetime later, so measuring from 3 s clears the
/// longest configuration below (grainSeconds = 2.0) with margin.
constexpr double kRateSweepMeasureFromSeconds = 3.0;

/// One clause-1 configuration. Every field is in SECONDS, grains/second or
/// semitones - never in samples - so the configuration itself carries no rate
/// dependence for the engine to be judged against.
///
/// `grainSeconds * density` is deliberately NON-INTEGER in all three entries.
/// The live population is exactly that product, so an integer product puts a
/// birth and a retirement on the same sample and the count resolves to n or
/// n+1 by whichever side of the tie the float interonset lands on at that rate
/// - a 1-in-16 (6.25 %) systematic step across rates that would trip the 5 %
/// threshold with nothing wrong. A non-integer product makes the count
/// genuinely oscillate at every rate and the time average equal the product.
struct RateSweepConfig {
    const char* name;
    float grainSeconds;
    float density;
    float pitchSemitones;
};

struct RateSweepMeasurement {
    std::size_t capacity = 0;
    std::uint64_t lifetimeSamples = 0;
    double lifetimeSeconds = 0.0;
    double meanConcurrent = 0.0;
    double rmsDb = -240.0;
    std::uint64_t born = 0;
    std::size_t maxActive = 0;
    bool allFinite = true;
};

/// Pin every control the engine has, so the ONLY thing that differs between the
/// three renders is the sample rate.
///
/// jitter = 0 removes GrainScheduler's stochastic reload (grain_scheduler.h:82,
/// which only draws when `jitter_ > 0`), so trigger timing is a pure function
/// of sampleRate/density and the concurrency comparison is not measuring an RNG
/// sequence. pitchSpread / driftRange / driftDepth = 0 make every grain's ratio
/// envelope the single point semitonesToRatio(pitchSemitones), which is what
/// makes `w` computable in the test and the lifetime expectation exact.
/// decorrelation = 0 keeps dR out of FR-025's arithmetic (the closed forms
/// below are written for dR = 0), and panSpread = 0 puts both channels on the
/// same equal-power gain so the RMS comparison has no pan variance in it.
void applyRateSweepSettings(Krate::DSP::AtmosphereEngine& engine, const RateSweepConfig& cfg) {
    engine.setGrainSeconds(cfg.grainSeconds);
    engine.setDensity(cfg.density);
    engine.setJitter(0.0f);
    engine.setPositionSeconds(kRateSweepPositionSeconds);
    engine.setPositionSpread(kRateSweepPositionSpread);
    engine.setPitchSemitones(cfg.pitchSemitones);
    engine.setPitchSpread(0.0f);
    engine.setDriftDepth(0.0f);
    engine.setDriftSmoothness(0.7f);
    engine.setDriftRangeSemitones(0.0f);
    engine.setPanSpread(0.0f);
    engine.setDecorrelation(0.0f);
    engine.setBlur(0.0f);
    engine.setFreezeMix(0.0f);
    engine.setLevel(1.0f);
    engine.setSeed(kRateSweepSeed);
}

/// Render one clause-1 configuration at one rate and measure the three SC-009
/// quantities. Blur and freeze are disabled at prepare() so the layer has no
/// latency (getLatencySamples() == 0) and the measurement window means the same
/// thing at all three rates.
RateSweepMeasurement measureRateSweep(double sampleRate, const RateSweepConfig& cfg) {
    using Krate::DSP::AtmosphereEngine;

    AtmosphereEngine::PrepareConfig prepareConfig;
    prepareConfig.captureSeconds = kRateSweepCaptureSeconds;
    prepareConfig.blurEnabled = false;
    prepareConfig.freezeEnabled = false;

    AtmosphereEngine engine;
    engine.prepare(sampleRate, prepareConfig);
    applyRateSweepSettings(engine, cfg);

    NoiseRenderer noise(kRateSweepBlock, kRateSweepNoiseSeed);

    const auto blockSamples = static_cast<double>(kRateSweepBlock);
    const auto totalBlocks =
        static_cast<std::size_t>((kRateSweepRenderSeconds * sampleRate) / blockSamples);
    const auto firstMeasuredBlock =
        static_cast<std::size_t>((kRateSweepMeasureFromSeconds * sampleRate) / blockSamples);

    RateSweepMeasurement measurement;
    double sumSquares = 0.0;
    double sumActive = 0.0;
    std::size_t sampleCount = 0;
    std::size_t activeObservations = 0;

    for (std::size_t b = 0; b < totalBlocks; ++b) {
        noise.render(engine);
        const std::size_t active = engine.getActiveGrainCount();
        measurement.maxActive = std::max(measurement.maxActive, active);
        if (b < firstMeasuredBlock) {
            continue;
        }
        for (std::size_t i = 0; i < kRateSweepBlock; ++i) {
            const float left = noise.outLeft[i];
            const float right = noise.outRight[i];
            if (!sampleIsFinite(left) || !sampleIsFinite(right)) {
                measurement.allFinite = false;
                continue;
            }
            sumSquares += (static_cast<double>(left) * static_cast<double>(left)) +
                          (static_cast<double>(right) * static_cast<double>(right));
        }
        sampleCount += 2 * kRateSweepBlock;
        sumActive += static_cast<double>(active);
        ++activeObservations;
    }

    measurement.capacity = engine.getCaptureCapacitySamples();
    measurement.lifetimeSamples = engine.getLastBornGrainLifetimeSamples();
    measurement.lifetimeSeconds = static_cast<double>(measurement.lifetimeSamples) / sampleRate;
    measurement.born = engine.getTotalGrainsBorn();
    if (activeObservations > 0) {
        measurement.meanConcurrent = sumActive / static_cast<double>(activeObservations);
    }
    if (sampleCount > 0) {
        measurement.rmsDb = rmsToDb(std::sqrt(sumSquares / static_cast<double>(sampleCount)));
    }
    return measurement;
}

/// Clause 2's measurement: the TRUNCATING configuration, whose lifetime is a
/// function of that rate's own ring capacity rather than of grainSeconds.
struct TruncationMeasurement {
    std::size_t capacity = 0;
    std::uint64_t lifetimeSamples = 0;
    double lifetimeSeconds = 0.0;
    double expectedSamples = 0.0;
    double expectedSeconds = 0.0;
    double requestedSamples = 0.0;
    std::uint64_t born = 0;
};

/// grainSeconds = 30 at +12 semitones: r == 2 exactly, so wUp = 1, wDown = 0,
/// w = 1, and w*L = 30*sampleRate is one to two orders of magnitude above the
/// ring, i.e. truncation binds hard at every rate. The expectation comes from
/// livenessMath() - the test's own restatement of FR-025 / plan S9.4(c), which
/// at decorrAge = 0 reduces to floor((C - 2 - 2g - 2)/w) - evaluated against
/// THAT RATE'S getCaptureCapacitySamples().
///
/// The first birth cannot happen until the ring is completely full: the birth
/// window is [ceil(1*L') + g, C - 2 - g], and FR-014 then needs
/// ceil(a0) + g <= available, i.e. available == C. That is ~11.9 s at 44.1 kHz
/// and ~10.9 s at 48/96 kHz, so the render cap below is generous rather than
/// tight, and the loop stops at the first birth.
TruncationMeasurement measureTruncatedLifetime(double sampleRate) {
    using Krate::DSP::AtmosphereEngine;

    constexpr std::size_t kBlock = 512;
    constexpr double kMaxRenderSeconds = 16.0;
    constexpr float kTruncatingGrainSeconds = 30.0f;
    constexpr float kTruncatingPitchSemitones = 12.0f;

    AtmosphereEngine::PrepareConfig prepareConfig;
    prepareConfig.captureSeconds = kRateSweepCaptureSeconds;
    prepareConfig.blurEnabled = false;
    prepareConfig.freezeEnabled = false;

    AtmosphereEngine engine;
    engine.prepare(sampleRate, prepareConfig);
    const RateSweepConfig truncating{"truncating", kTruncatingGrainSeconds, 4.0f,
                                     kTruncatingPitchSemitones};
    applyRateSweepSettings(engine, truncating);
    // The birth age is clamped into FR-025's window anyway; 1.0 s is the FR-009
    // default and states plainly that the clamp - not the knob - is what
    // decides where a truncated grain reads.
    engine.setPositionSeconds(1.0f);
    engine.setPositionSpread(0.3f);

    NoiseRenderer noise(kBlock, kRateSweepNoiseSeed);
    const auto maxBlocks =
        static_cast<std::size_t>((kMaxRenderSeconds * sampleRate) / static_cast<double>(kBlock));
    for (std::size_t b = 0; b < maxBlocks && engine.getTotalGrainsBorn() == 0; ++b) {
        noise.render(engine);
    }

    TruncationMeasurement measurement;
    measurement.capacity = engine.getCaptureCapacitySamples();
    measurement.lifetimeSamples = engine.getLastBornGrainLifetimeSamples();
    measurement.lifetimeSeconds = static_cast<double>(measurement.lifetimeSamples) / sampleRate;
    measurement.born = engine.getTotalGrainsBorn();
    measurement.requestedSamples =
        std::round(static_cast<double>(kTruncatingGrainSeconds) * sampleRate);

    const LivenessMath math =
        livenessMath(static_cast<double>(measurement.capacity), kTruncatingPitchSemitones, 0.0f,
                     0.0, kTruncatingGrainSeconds, sampleRate);
    measurement.expectedSamples = math.valid ? math.lifetime : 0.0;
    measurement.expectedSeconds = measurement.expectedSamples / sampleRate;
    return measurement;
}

}  // namespace

// =============================================================================
// SC-009 - AtmosphereEngine_SampleRateIndependence
// =============================================================================
// Every time constant in the engine is specified in SECONDS (FR-009's control
// table) and every internal quantity derived from one is a function of the
// prepared sample rate. The criterion has three parts, and the second exists
// because ONE of those quantities genuinely is not rate-invariant.
//
// CLAUSE 1 - non-truncating sweep. A stated PRECONDITION, not a hope: only
//   configurations satisfying FR-025's no-truncation condition
//   w*L <= C - 2 - 2g - 2 are swept here, and that is asserted from the test's
//   own livenessMath() shadow model before anything is compared. Under it the
//   grain lifetime is the requested `grainSeconds` verbatim, so lifetime is
//   compared to the REQUEST (0.5 %), while mean concurrent count (5 %) and
//   output RMS (1.0 dB) are compared ACROSS rates against the 44.1 kHz render.
//
// CLAUSE 2 - truncating, rate-aware. RA-2 is load-bearing here rather than a
//   footnote: RollingCaptureBuffer::prepare rounds capacity UP to the next
//   power of two (rolling_capture_buffer.h:83, :210-220), so captureSeconds = 8
//   buys 524 288 samples at BOTH 44.1 and 48 kHz - 11.89 s of ring at the
//   former and 10.92 s at the latter, an 8.8 % spread in C/sampleRate. FR-025
//   truncates in SAMPLES against that same C, so wherever truncation binds the
//   grain lifetime in seconds is rate-dependent BY CONSTRUCTION. A single
//   rate-invariant expectation would report that 8.8 % as a defect. The
//   expectation is therefore recomputed from each rate's own
//   getCaptureCapacitySamples(), and the spread itself is asserted to be real
//   (> 5 %) so that this clause cannot silently degenerate into clause 1.
//
// ALLOCATION CLAUSE (D-18) - stated as the property it protects. An earlier
//   draft required a re-prepare() at a new rate to allocate EXACTLY as many
//   times as a fresh prepare at that rate. That fails deterministically on
//   CORRECT code: every re-prepare path reuses capacity - bufferL_/bufferR_
//   .resize (rolling_capture_buffer.h:86-87), data_/mags_/phases_.resize
//   (spectral_buffer.h:63-65), eleven resize()s in
//   SpectralFreezeOscillator::prepare, and a fixed-size envelopeTable_.assign -
//   all of which allocate ZERO times when the geometry is unchanged, which is
//   exactly what this case holds constant. So:
//     1. 0 < secondPrepareCount <= freshPrepareCount - the count is BRACKETED
//        from both sides. Above the fresh one means a buffer is being
//        reallocated rather than reused; at zero means nothing grew, and this
//        transition (48 -> 96 kHz) DOUBLES the capture ring, so a re-prepare
//        that allocates nothing has kept the 48 kHz ring. That lower bound is
//        the half of the criterion's intent an equality was reaching for.
//     2. ZERO allocations across a full render at the new rate immediately
//        afterwards - the property that actually matters. Re-prepare must leave
//        nothing undersized, and an undersized buffer surfaces as an
//        audio-thread allocation on the very first block. Same instrument as
//        SC-001, pointed at the re-prepare path.
//     3. GEOMETRY EQUALITY against the fresh engine - capture capacity and
//        reported latency. This is what the count-equality was a proxy for, and
//        unlike the count it is actually a property of the re-prepared engine
//        rather than of how many times its allocator happened to be called.
//   The engine must then be silent-but-usable (FR-014's cold ring): the first
//   block after the re-prepare is exactly zero, and the render is non-silent
//   once the ring has refilled.
//
// WHY THE ALLOCATION COUNT IS READ FROM THE DETECTOR SINGLETON.
//   TestHelpers::AllocationScope latches its count in its DESTRUCTOR
//   (tests/test_helpers/allocation_detector.h:81-83), so scope.getAllocationCount()
//   reads 0 while the scope object is still reachable. The live count comes
//   from AllocationDetector::instance() (:48-50) inside the scope - same
//   counter, same window, readable in time to use. Every REQUIRE/CAPTURE runs
//   OUTSIDE the tracked region, because Catch2's machinery allocates.

TEST_CASE("AtmosphereEngine_SampleRateIndependence", "[atmosphere]") {
    using Krate::DSP::AtmosphereEngine;

    // -------------------------------------------------------------------
    // Clause 1 - the non-truncating sweep
    // -------------------------------------------------------------------
    const std::array<RateSweepConfig, 3> sweep{
        RateSweepConfig{"unison", 2.0f, 7.75f, 0.0f},          // w = 0
        RateSweepConfig{"down 5 semitones", 1.5f, 5.5f, -5.0f},  // w = wDown = 0.2509
        RateSweepConfig{"up 7 semitones", 0.6f, 11.5f, 7.0f},    // w = wUp   = 0.4983
    };

    for (const RateSweepConfig& cfg : sweep) {
        std::array<RateSweepMeasurement, 3> measured{};
        for (std::size_t r = 0; r < kSampleRateSweep.size(); ++r) {
            measured[r] = measureRateSweep(kSampleRateSweep[r], cfg);
        }

        for (std::size_t r = 0; r < kSampleRateSweep.size(); ++r) {
            const double rate = kSampleRateSweep[r];
            const RateSweepMeasurement& m = measured[r];
            CAPTURE(cfg.name, rate, m.capacity, m.lifetimeSamples, m.lifetimeSeconds,
                    m.meanConcurrent, m.rmsDb, m.born, m.maxActive);

            // --- The sweep PRECONDITION, asserted rather than assumed. -----
            // livenessMath() is the test's own restatement of FR-025 /
            // plan S9.4(c); `lifetime == requested` IS the condition
            // w*L <= C - 2 - 2g - 2 (with dR = 0), so this is the criterion's
            // "only configurations satisfying ... are swept here" made
            // checkable instead of asserted in prose.
            const LivenessMath math =
                livenessMath(static_cast<double>(m.capacity), cfg.pitchSemitones, 0.0f, 0.0,
                             cfg.grainSeconds, rate);
            const double requested = std::round(static_cast<double>(cfg.grainSeconds) * rate);
            REQUIRE(math.valid);
            REQUIRE(std::abs(math.lifetime - requested) < 0.5);

            // --- Non-vacuousness: a silent engine passes every bound below.
            REQUIRE(m.born > 0);
            REQUIRE(m.allFinite);
            REQUIRE(m.maxActive > 0);
            REQUIRE(m.maxActive < AtmosphereEngine::kMaxGrains);  // no FR-023 skipping here
            REQUIRE(m.rmsDb > -80.0);

            // --- Clause 1, lifetime: within 0.5 % of the REQUESTED seconds.
            const double lifetimeError = std::abs(m.lifetimeSeconds -
                                                  static_cast<double>(cfg.grainSeconds)) /
                                         static_cast<double>(cfg.grainSeconds);
            CAPTURE(lifetimeError);
            REQUIRE(lifetimeError < 0.005);
        }

        // --- Clause 1, cross-rate: concurrency and level -------------------
        // The reference is the 44.1 kHz render (index 0). The expected mean is
        // density * grainSeconds at every rate; asserting it against the
        // reference MEASUREMENT rather than against the product is what makes
        // this a rate-INDEPENDENCE check rather than a second copy of the
        // scheduler's arithmetic.
        const RateSweepMeasurement& reference = measured[0];
        const double expectedConcurrent =
            static_cast<double>(cfg.density) * static_cast<double>(cfg.grainSeconds);
        const double referenceConcurrentError =
            std::abs(reference.meanConcurrent - expectedConcurrent) / expectedConcurrent;
        CAPTURE(cfg.name, reference.meanConcurrent, expectedConcurrent, referenceConcurrentError,
                reference.rmsDb);
        REQUIRE(reference.meanConcurrent > 1.0);
        // Anchors the reference itself, so "all three rates equally wrong" is
        // not a pass. density * grainSeconds IS the live population: a grain is
        // born every sampleRate/density samples and lives grainSeconds.
        REQUIRE(referenceConcurrentError < 0.05);

        for (std::size_t r = 1; r < kSampleRateSweep.size(); ++r) {
            const double rate = kSampleRateSweep[r];
            const double concurrentError =
                std::abs(measured[r].meanConcurrent - reference.meanConcurrent) /
                reference.meanConcurrent;
            const double rmsErrorDb = std::abs(measured[r].rmsDb - reference.rmsDb);
            CAPTURE(cfg.name, rate, measured[r].meanConcurrent, concurrentError,
                    measured[r].rmsDb, rmsErrorDb);
            REQUIRE(concurrentError < 0.05);
            REQUIRE(rmsErrorDb < 1.0);
        }
    }

    // -------------------------------------------------------------------
    // Clause 2 - truncating, rate-aware
    // -------------------------------------------------------------------
    std::array<TruncationMeasurement, 3> truncated{};
    for (std::size_t r = 0; r < kSampleRateSweep.size(); ++r) {
        truncated[r] = measureTruncatedLifetime(kSampleRateSweep[r]);
    }

    for (std::size_t r = 0; r < kSampleRateSweep.size(); ++r) {
        const double rate = kSampleRateSweep[r];
        const TruncationMeasurement& t = truncated[r];
        CAPTURE(rate, t.capacity, t.lifetimeSamples, t.lifetimeSeconds, t.expectedSamples,
                t.expectedSeconds, t.requestedSamples, t.born);

        REQUIRE(t.born > 0);
        REQUIRE(t.expectedSamples > 0.0);
        // Truncation really binds - otherwise this is clause 1 in disguise.
        REQUIRE(t.expectedSamples < t.requestedSamples - 0.5);

        const double lifetimeError =
            std::abs(t.lifetimeSeconds - t.expectedSeconds) / t.expectedSeconds;
        CAPTURE(lifetimeError);
        REQUIRE(lifetimeError < 0.005);
    }

    // The 8.8 % spread that makes a single rate-invariant expectation a FALSE
    // FAILURE, asserted so this clause can never quietly become rate-invariant:
    // 44.1 and 48 kHz share one ring CAPACITY (352 800 and 384 000 both round
    // up to 524 288) and therefore differ in ring SECONDS, while 96 kHz rounds
    // to twice the capacity for very nearly the same seconds as 48 kHz.
    CAPTURE(truncated[0].capacity, truncated[1].capacity, truncated[2].capacity,
            truncated[0].expectedSeconds, truncated[1].expectedSeconds,
            truncated[2].expectedSeconds);
    REQUIRE(truncated[0].capacity == truncated[1].capacity);
    REQUIRE(truncated[2].capacity == truncated[1].capacity * std::size_t{2});
    const double capacitySpread =
        std::abs(truncated[0].expectedSeconds - truncated[1].expectedSeconds) /
        truncated[1].expectedSeconds;
    CAPTURE(capacitySpread);
    REQUIRE(capacitySpread > 0.05);

    // -------------------------------------------------------------------
    // Allocation clause (D-18)
    // -------------------------------------------------------------------
    constexpr double kOldRate = 48000.0;
    constexpr double kNewRate = 96000.0;
    constexpr std::size_t kAllocBlock = 512;
    constexpr double kAllocRenderSeconds = 3.0;

    AtmosphereEngine::PrepareConfig allocConfig;
    allocConfig.captureSeconds = kRateSweepCaptureSeconds;
    allocConfig.blurEnabled = true;   // exercises the STFT / OverlapAdd / FIFO paths
    allocConfig.freezeEnabled = true; // ... and both freeze oscillators + the delay-match ring

    // Both engines are CONSTRUCTED outside every tracked scope: prepare() is
    // allowed to allocate and does, and construction is not what is measured.
    AtmosphereEngine freshEngine;
    AtmosphereEngine reusedEngine;
    reusedEngine.prepare(kOldRate, allocConfig);
    applyRateSweepSettings(reusedEngine, sweep[0]);

    NoiseRenderer allocNoise(kAllocBlock, kRateSweepNoiseSeed);
    const auto allocBlocks =
        static_cast<std::size_t>((kAllocRenderSeconds * kNewRate) / static_cast<double>(kAllocBlock));
    const std::size_t lateFirstBlock = allocBlocks / 2;

    std::size_t freshPrepareCount = 0;
    {
        [[maybe_unused]] const TestHelpers::AllocationScope scope;
        freshEngine.prepare(kNewRate, allocConfig);
        freshPrepareCount = TestHelpers::AllocationDetector::instance().getAllocationCount();
    }

    std::size_t secondPrepareCount = 0;
    {
        [[maybe_unused]] const TestHelpers::AllocationScope scope;
        reusedEngine.prepare(kNewRate, allocConfig);
        secondPrepareCount = TestHelpers::AllocationDetector::instance().getAllocationCount();
    }

    std::size_t renderAllocationCount = 0;
    bool firstBlockExactlyZero = true;
    bool renderAllFinite = true;
    double lateSumSquares = 0.0;
    std::size_t lateSampleCount = 0;
    std::uint64_t bornAfterReprepare = 0;
    {
        [[maybe_unused]] const TestHelpers::AllocationScope scope;
        for (std::size_t b = 0; b < allocBlocks; ++b) {
            allocNoise.render(reusedEngine);
            for (std::size_t i = 0; i < kAllocBlock; ++i) {
                const float left = allocNoise.outLeft[i];
                const float right = allocNoise.outRight[i];
                if (!sampleIsFinite(left) || !sampleIsFinite(right)) {
                    renderAllFinite = false;
                    continue;
                }
                if (b == 0 && (left != 0.0f || right != 0.0f)) {
                    firstBlockExactlyZero = false;
                }
                if (b >= lateFirstBlock) {
                    lateSumSquares += (static_cast<double>(left) * static_cast<double>(left)) +
                                      (static_cast<double>(right) * static_cast<double>(right));
                }
            }
            if (b >= lateFirstBlock) {
                lateSampleCount += 2 * kAllocBlock;
            }
        }
        bornAfterReprepare = reusedEngine.getTotalGrainsBorn();
        renderAllocationCount = TestHelpers::AllocationDetector::instance().getAllocationCount();
    }

    const double lateRms =
        (lateSampleCount > 0) ? std::sqrt(lateSumSquares / static_cast<double>(lateSampleCount))
                              : 0.0;
    CAPTURE(freshPrepareCount, secondPrepareCount, renderAllocationCount, bornAfterReprepare,
            lateRms);

    // A fresh prepare at a rate the engine has never seen MUST allocate; without
    // this the ordering check below is satisfied by 0 <= 0.
    REQUIRE(freshPrepareCount > 0);
    // (1) the ordering check.
    REQUIRE(secondPrepareCount <= freshPrepareCount);
    // (1b) ... and the re-prepare must have GROWN something. 48 -> 96 kHz
    //      doubles the capture ring (asserted below), so a re-prepare that
    //      allocates nothing at all has kept the 48 kHz ring and is exactly the
    //      "left undersized" failure the equality in the criterion was reaching
    //      for. Together with (1) this brackets the count from both sides -
    //      strictly more than `<=` alone says, and it is the half of the
    //      criterion's intent that an equality on a reuse path cannot express.
    REQUIRE(secondPrepareCount > 0);
    // (2) the property that matters: nothing was left undersized for the new
    //     rate, so the audio thread never reaches the heap.
    REQUIRE(renderAllocationCount == 0);
    REQUIRE(renderAllFinite);
    // FR-014's cold ring: silent immediately after the re-prepare...
    REQUIRE(firstBlockExactlyZero);
    // ... and usable once it has refilled.
    REQUIRE(bornAfterReprepare > 0);
    REQUIRE(lateRms > 1.0e-4);
    // (3) THE GEOMETRY EQUALITY, which is what the criterion's count-equality
    //     was a proxy for: after the re-prepare the reused engine must be
    //     indistinguishable from the fresh one in every geometry it exposes.
    //     An allocation COUNT cannot say this (a correct reuse path allocates
    //     fewer times by construction); the geometries can, and they are the
    //     thing a skimped re-prepare would get wrong.
    REQUIRE(reusedEngine.getCaptureCapacitySamples() ==
            freshEngine.getCaptureCapacitySamples());
    REQUIRE(reusedEngine.getLatencySamples() == freshEngine.getLatencySamples());
}
