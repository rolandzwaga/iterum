// ==============================================================================
// Layer 4: Effect Tests - AetherReverb, spectral diffusion + geometry
//                                        (specs/seraphis-phase6-aether-space)
// ==============================================================================
// Constitution Principle XII: Test-First Development.
//
// Reference: specs/seraphis-phase6-aether-space/spec.md
//            specs/seraphis-phase6-aether-space/plan.md   (S1.1, S7.3, S9)
//            specs/seraphis-phase6-aether-space/tasks.md  (T001 creates this TU)
//
// SCOPE OF THIS TU (plan S1.1's TU-ownership table): SC-003, SC-007, SC-016.
//
// COMPILE FLAGS: this TU is capped at -O2 on GCC/Clang (dsp/tests/CMakeLists.txt,
//   the third set_source_files_properties block) for the same GCC 13+ -O3
//   recirculating-delay pathology as reverb_test.cpp / fdn_reverb_test.cpp.
//   -fno-fast-math is DELIBERATELY ABSENT - see the comment at the registration
//   site and in aether_reverb_test.cpp (plan R-5).
// ==============================================================================

#include <catch2/catch_all.hpp>

#include <krate/dsp/core/random.h>
#include <krate/dsp/effects/aether_reverb.h>
#include <krate/dsp/primitives/fft.h>
#include <krate/dsp/primitives/spectral_buffer.h>
#include <krate/dsp/primitives/stft.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <vector>

using Catch::Approx;
using Krate::DSP::AetherReverb;

// ------------------------------------------------------------------------------
// T001 smoke case: FR-065 / FR-084's zero-latency escape hatch - with the
// spectral stage disabled at prepare, neither the STFT set nor the dry-alignment
// delay is allocated and the engine reports exactly 0 samples of latency.
// ------------------------------------------------------------------------------
TEST_CASE("AetherReverb_SpectralSmoke", "[effects][aether]") {
    AetherReverb r;

    AetherReverb::PrepareConfig cfg;
    cfg.spectralDiffusionEnabled = false;
    r.prepare(48000.0, cfg);

    REQUIRE(r.isPrepared());
    REQUIRE(r.getLatencySamples() == 0u);
}

// ==============================================================================
// T002 fixtures - shared prologues P-1 and P-2 (plan S8.1)
// ==============================================================================

namespace {

/// @brief P-1 + P-2: prepare an engine with every life modulator silenced and
///        with maxDelaySeconds = 0.5f, which is what makes getMaxSizeScale()
///        the UNCLAMPED 4.0f (FR-012). The stages this TU's geometry case does
///        not exercise are switched off so the fixture allocates the FDN core
///        and nothing else.
void prepareGeometryEngine(AetherReverb& engine, std::size_t numChannels, double sampleRate,
                           float maxDelaySeconds = 0.5f) {
    AetherReverb::PrepareConfig cfg;
    cfg.numChannels = numChannels;
    cfg.maxDelaySeconds = maxDelaySeconds;  // P-2
    cfg.shimmerEnabled = false;
    cfg.bloomEnabled = false;
    cfg.spectralDiffusionEnabled = false;
    engine.prepare(sampleRate, cfg);

    // P-1: no breath on Size, no tide on Dimensionality, no per-line jitter.
    engine.setSizeBreathDepth(0.0f);
    engine.setDimensionalityTideDepth(0.0f);
    engine.setModDepth(0.0f);
}

/// @brief setSize(v), then ONE 64-sample zero block so the control step at
///        phase 0 has materialised the Size-scaled geometry.
///
/// reset() comes first deliberately. FR-009's smoother-initialisation rule only
/// SNAPS a setter that runs before any sample has been processed since the last
/// prepare()/reset(); without the reset the second and later points of a sweep
/// would be read 64 samples into a 300 ms ramp, which is a property of the
/// smoother rather than of the geometry this case is measuring.
void applySize(AetherReverb& engine, float v) {
    engine.reset();
    engine.setSize(v);

    std::array<float, AetherReverb::kControlChunkSamples> zeros{};
    std::array<float, AetherReverb::kControlChunkSamples> outL{};
    std::array<float, AetherReverb::kControlChunkSamples> outR{};
    engine.processStereoBlock(zeros.data(), zeros.data(), outL.data(), outR.data(), zeros.size());
}

/// @brief S(v) = 0.25 * 2^(4v), FR-012's mapping, recomputed independently of
///        the header so a changed exponent base is visible here.
[[nodiscard]] float sizeScaleRef(float v) noexcept {
    return 0.25f * std::exp2(4.0f * v);
}

/// @brief D = sum_i getEffectiveDelayLengthSamples(i) / sampleRate, recomputed
///        from the PUBLIC per-channel accessor - SC-003 clause 3(a)'s
///        stale-accessor catch depends on this being an independent path to the
///        same number that getModalDensityPerHz() reports.
[[nodiscard]] double modalDensityFromAccessors(const AetherReverb& engine, std::size_t numChannels,
                                               double sampleRate) noexcept {
    double sum = 0.0;
    for (std::size_t i = 0; i < numChannels; ++i) {
        sum += static_cast<double>(engine.getEffectiveDelayLengthSamples(i));
    }
    return sum / sampleRate;
}

}  // namespace

// ==============================================================================
// T002 - FR-011 tables, FR-012 Size mapping, FR-013 modal density,
//        SC-003 clauses 3(a) and 3(b), and the FR-012 maxSizeScale_ clamp path.
// ==============================================================================
TEST_CASE("AetherReverb_GeometryAndModalDensity", "[effects][aether]") {
    constexpr double kSr = 48000.0;

    // --------------------------------------------------------------------------
    // 1. FR-011 - runtime companion to the header's compile-time gcd fold. Both
    //    shipped tables are strictly ascending (channel index order IS length
    //    order, which FR-050's "four longest" subset and FR-018's even/odd
    //    output split rely on) and pairwise coprime.
    // --------------------------------------------------------------------------
    SECTION("FR-011: both reference tables are strictly ascending and pairwise coprime") {
        constexpr std::size_t k8 = 8;
        constexpr std::size_t k16 = 16;

        for (std::size_t i = 1; i < k8; ++i) {
            REQUIRE(AetherReverb::kRefDelays8[i] > AetherReverb::kRefDelays8[i - 1]);
        }
        for (std::size_t i = 0; i < k8; ++i) {
            for (std::size_t j = i + 1; j < k8; ++j) {
                INFO("kRefDelays8 pair (" << i << "," << j << ")");
                REQUIRE(std::gcd(AetherReverb::kRefDelays8[i], AetherReverb::kRefDelays8[j]) ==
                        std::size_t{1});
            }
        }

        for (std::size_t i = 1; i < k16; ++i) {
            REQUIRE(AetherReverb::kRefDelays16[i] > AetherReverb::kRefDelays16[i - 1]);
        }
        for (std::size_t i = 0; i < k16; ++i) {
            for (std::size_t j = i + 1; j < k16; ++j) {
                INFO("kRefDelays16 pair (" << i << "," << j << ")");
                REQUIRE(std::gcd(AetherReverb::kRefDelays16[i], AetherReverb::kRefDelays16[j]) ==
                        std::size_t{1});
            }
        }
    }

    // --------------------------------------------------------------------------
    // 2. FR-012 - Size endpoints at N = 8, 48 kHz. S(0) = 0.25, S(0.5) = 1.0,
    //    S(1) = 4.0, applied multiplicatively to the reference lengths, so the
    //    shortest line spans 241.75 .. 3868 samples and the longest
    //    1271.75 .. 20348 samples.
    // --------------------------------------------------------------------------
    SECTION("FR-012: Size endpoints scale the shortest and longest lines by S(v)") {
        AetherReverb engine;
        prepareGeometryEngine(engine, std::size_t{8}, kSr);

        // P-2: assert the UNCLAMPED scale before any Size sweep.
        REQUIRE(engine.getMaxSizeScale() == 4.0f);

        const std::array<float, 3> sizes = {0.0f, 0.5f, 1.0f};
        for (const float v : sizes) {
            applySize(engine, v);
            const float s = sizeScaleRef(v);
            INFO("size = " << v << ", S = " << s);
            REQUIRE(engine.getEffectiveDelayLengthSamples(0) ==
                    Approx(967.0f * s).epsilon(0.005));
            REQUIRE(engine.getEffectiveDelayLengthSamples(7) ==
                    Approx(5087.0f * s).epsilon(0.005));
        }
    }

    // --------------------------------------------------------------------------
    // 3. SC-003 clause 3(a) - the stale-accessor catch. getModalDensityPerHz()
    //    must be recomputed from the CURRENT Size-scaled lengths, never cached
    //    from the prepare-time geometry, so an independent recomputation from
    //    getEffectiveDelayLengthSamples() has to agree at every Size.
    // --------------------------------------------------------------------------
    SECTION("SC-003 clause 3(a): reported modal density tracks the current geometry") {
        AetherReverb engine;
        prepareGeometryEngine(engine, std::size_t{8}, kSr);
        REQUIRE(engine.getMaxSizeScale() == 4.0f);  // P-2

        const std::array<float, 5> sizes = {0.0f, 0.25f, 0.5f, 0.75f, 1.0f};
        for (const float v : sizes) {
            applySize(engine, v);
            const double recomputed = modalDensityFromAccessors(engine, std::size_t{8}, kSr);
            INFO("size = " << v << ", recomputed D = " << recomputed);
            REQUIRE(static_cast<double>(engine.getModalDensityPerHz()) ==
                    Approx(recomputed).epsilon(0.005));
        }
    }

    // --------------------------------------------------------------------------
    // 4. SC-003 clause 3(b) - D is linear in S, so the full-range ratio is
    //    S(1)/S(0) = 4.0 / 0.25 = 16.
    // --------------------------------------------------------------------------
    SECTION("SC-003 clause 3(b): D(size=1) / D(size=0) == 16") {
        AetherReverb engine;
        prepareGeometryEngine(engine, std::size_t{8}, kSr);
        REQUIRE(engine.getMaxSizeScale() == 4.0f);  // P-2

        applySize(engine, 0.0f);
        const double dMin = static_cast<double>(engine.getModalDensityPerHz());
        applySize(engine, 1.0f);
        const double dMax = static_cast<double>(engine.getModalDensityPerHz());

        REQUIRE(dMin > 0.0);
        REQUIRE(dMax / dMin == Approx(16.0).epsilon(0.01));
    }

    // --------------------------------------------------------------------------
    // 5. FR-013 - cross-check against the header's reachable-density table
    //    (banner item (4)): modes/Hz at 48 kHz for both shipped orders.
    // --------------------------------------------------------------------------
    SECTION("FR-013: reported density matches the header's table at both orders") {
        const std::array<float, 3> sizes = {0.0f, 0.5f, 1.0f};  // S = 0.25 / 1.0 / 4.0

        {
            AetherReverb engine;
            prepareGeometryEngine(engine, std::size_t{8}, kSr);
            REQUIRE(engine.getMaxSizeScale() == 4.0f);  // P-2

            const std::array<double, 3> expected8 = {0.106, 0.426, 1.702};
            for (std::size_t i = 0; i < 3; ++i) {
                applySize(engine, sizes[i]);
                INFO("N = 8, size = " << sizes[i]);
                REQUIRE(static_cast<double>(engine.getModalDensityPerHz()) ==
                        Approx(expected8[i]).epsilon(0.01));
            }
        }

        {
            AetherReverb engine;
            prepareGeometryEngine(engine, std::size_t{16}, kSr);
            REQUIRE(engine.getMaxSizeScale() == 4.0f);  // P-2

            const std::array<double, 3> expected16 = {0.210, 0.841, 3.363};
            for (std::size_t i = 0; i < 3; ++i) {
                applySize(engine, sizes[i]);
                INFO("N = 16, size = " << sizes[i]);
                REQUIRE(static_cast<double>(engine.getModalDensityPerHz()) ==
                        Approx(expected16[i]).epsilon(0.01));
            }
        }
    }

    // --------------------------------------------------------------------------
    // 6. FR-012 / Edge case 10 - the maxSizeScale_ CLAMP path. This is the only
    //    criterion in the phase that exercises it; everywhere else P-2 asserts
    //    the unclamped 4.0f first. At the range minimum maxDelaySeconds = 0.05:
    //      (0.05*48000 - 4) / (5087 * 1.005) = 2396 / 5112.4 = 0.4687
    // --------------------------------------------------------------------------
    SECTION("FR-012 edge case 10: an undersized buffer clamps getMaxSizeScale()") {
        AetherReverb engine;
        prepareGeometryEngine(engine, std::size_t{8}, kSr, /*maxDelaySeconds=*/0.05f);

        REQUIRE(engine.getMaxSizeScale() == Approx(0.47f).epsilon(0.02));
        REQUIRE(engine.getMaxSizeScale() < 4.0f);
    }
}

// ==============================================================================
// T005 fixtures - SC-003, normalised echo density (NED).
//
// SCOPE. This case owns SC-003 clause 1 (NED >= 0.80 over Size x
// Dimensionality), clause 2 (NED non-decreasing in Density, strictly lower at
// the sparse extreme than at the dense one) and clause 3(c) (the Size-scaled
// mean inter-arrival time at density = 0).
//
// Clauses 3(a) (getModalDensityPerHz() agrees with an independent
// recomputation from getEffectiveDelayLengthSamples()) and 3(b) (the
// D(size=1)/D(size=0) == 16 range) are DELIBERATELY NOT REPEATED here - they
// already landed above in AetherReverb_GeometryAndModalDensity (T002), and a
// second copy would give FR-012's mapping two places to be updated.
//
// NED is the metric ALREADY IMPLEMENTED for FDNReverb at
// dsp/tests/unit/effects/fdn_reverb_test.cpp:328-373 - 1 ms windows, RMS per
// window, fraction of windows whose RMS exceeds peak * 0.01 (-40 dB) - computed
// on the MONO SUM of the G-3 impulse response. Nothing about the metric is
// reinvented here; only the measurement WINDOW is derived rather than fixed.
// ==============================================================================

namespace {

constexpr double kEchoSampleRate = 48000.0;
constexpr std::size_t kEchoBlockSamples = 512;

/// 1 ms windows, the FDNReverb NED window (fdn_reverb_test.cpp:338).
constexpr double kNedWindowMs = 1.0;
constexpr std::size_t kNedWindowSamples =
    static_cast<std::size_t>(kEchoSampleRate * kNedWindowMs / 1000.0);

/// -40 dB relative to the peak window RMS (fdn_reverb_test.cpp:356).
constexpr double kNedThresholdFraction = 0.01;

/// @brief How many EARLY arrivals SC-003 clause 3(c) averages over.
///
/// WHY CLAUSE 3(c) CANNOT BE MEASURED OVER THE WHOLE NED WINDOW (measured, not
/// assumed - every figure below was produced by rendering this engine at 48 kHz,
/// N = 8, density = 0, and is reproduced in the WARN output of this test):
///
///   The mean gap between occupied 1 ms windows is bounded BELOW by the window
///   width. Over the derived NED window it degenerates to exactly
///   `1 ms / NED`: at size = 0.25 NED = 0.956 -> mean 1.046 ms, at size = 1.0
///   NED = 0.535 -> mean 1.860 ms. So the "ratio of the two means" is
///   identically `NED(0.25) / NED(1.0)`, and demanding that it equal S's ratio
///   of 8 demands `NED(size = 1, density = 0) <= 0.125` - a large room that is
///   98.5 % empty, i.e. precisely the sparse, metallic tail SC-003 exists to
///   forbid ("No metallic ringing at any size"). The clause as literally written
///   is therefore unsatisfiable by ANY correct FDN, for the same reason SC-003's
///   own preamble gives for the fixed 250 ms window: the measurement is coarser
///   than the thing it measures. The spec derived the WINDOW from the geometry
///   and left the RESOLUTION fixed; the resolution is the half that binds here.
///
///   The physics the clause is after is intact and is verified below: every one
///   of the eight Size-scaled line lengths changes by exactly 8.0000x between
///   size = 0.25 and size = 1.0, and the two impulse responses are the same
///   arrival pattern under an 8x time scaling (10.07 / 12.68 / 16.07 / 20.55 /
///   25.80 ms against 80.58 / 101.42 / 128.58 / 164.42 / 206.42 ms).
///
/// WHAT IS MEASURED INSTEAD - the spec's own words, "measured where it is
/// resolvable": the mean inter-arrival time of the first `kEarlyArrivalCount`
/// arrivals, detected as ONSETS (a 1 ms window above the -40 dB threshold whose
/// predecessor is below it) in the spec's unchanged 1 ms windows, with each
/// onset's time refined to the peak sample inside its window. Nothing else about
/// the metric moves: same bins, same -40 dB threshold, same density = 0, same
/// size pair, same +/- 15 % tolerance, same expected value (the ratio of the two
/// S values recomputed from FR-012 in this file).
///
/// WHY THREE: it is the largest count at which BOTH configurations resolve the
/// SAME arrival set at the spec's 1 ms resolution, which is the precondition for
/// comparing their arrival times at all. Measured: the fourth onset of the large
/// room is the second-order arrival at 2 * m_short = 161.21 ms, which in the
/// small room falls at 20.15 ms and shares its 1 ms window with the fourth line
/// tap at 20.55 ms. From K = 4 on, the two rooms are therefore averaging over
/// different events (measured ratios 7.68 at K = 4, 6.44 at K = 5, against 8).
/// At K = 3 the measured ratio is 8.0000 at every Dimensionality in {0, 0.35,
/// 0.5, 1} and every Decay in {0.5, 4, 20} s. The onset-vs-line-tap agreement
/// asserted in clause 3(c) is what makes this observable rather than assumed: it
/// fails loudly if a fourth event ever intrudes.
constexpr std::size_t kEarlyArrivalCount = 3;

/// @brief One SC-003 measurement point.
struct EchoSetup {
    float size = 0.5f;
    float dimensionality = 0.5f;
    float density = 0.70f;        ///< the FR-009 default
    std::size_t numChannels = 8;  ///< P-4
};

/// @brief P-1 + P-2 + P-3 + P-4 prologue for every SC-003 measurement.
///
/// P-1 silences all three life modulators (the FR-009 defaults 0.2 / 0.2 / 0.25
/// would wander every delay length, and therefore m_long and the derived
/// window, while the impulse response is being rendered). P-2 pins
/// maxDelaySeconds = 0.5 and asserts the UNCLAMPED getMaxSizeScale() == 4.0f
/// before any Size sweep. P-3 is setMix(1) so the dry spike is not the global
/// peak the -40 dB threshold is taken against. P-4 is the default order.
void prepareEchoEngine(AetherReverb& engine, const EchoSetup& s) {
    AetherReverb::PrepareConfig cfg;
    cfg.numChannels = s.numChannels;  // P-4
    cfg.maxBlockSamples = kEchoBlockSamples;
    cfg.maxDelaySeconds = 0.5f;  // P-2
    cfg.shimmerEnabled = false;  // stages SC-003 does not measure
    cfg.bloomEnabled = false;
    cfg.spectralDiffusionEnabled = false;
    engine.prepare(kEchoSampleRate, cfg);

    REQUIRE(engine.getMaxSizeScale() == 4.0f);  // P-2

    engine.setSizeBreathDepth(0.0f);  // P-1
    engine.setDimensionalityTideDepth(0.0f);
    engine.setModDepth(0.0f);
    engine.setMix(1.0f);  // P-3
    engine.setSize(s.size);
    engine.setDimensionality(s.dimensionality);
    engine.setDensity(s.density);
}

/// @brief Everything SC-003 requires to be RECORDED per configuration, plus the
///        two numbers the clauses assert on.
struct NedMeasurement {
    double ned = 0.0;                  ///< occupied / analysed, over [t_start, t_start + W]
    double meanInterArrivalMs = 0.0;   ///< clause 3(c)
    double tStartMs = 0.0;             ///< first occupied 1 ms window
    double windowMs = 0.0;             ///< W = max(250 ms, 3 * m_long)
    double mShortMs = 0.0;             ///< shortest Size-scaled line
    double mLongMs = 0.0;              ///< longest Size-scaled line
    double peakWindowRms = 0.0;
    std::size_t occupiedWindows = 0;
    std::size_t analysedWindows = 0;
    std::size_t excludedWindows = 0;  ///< windows before t_start, out of the denominator

    /// clause 3(c): the first kEarlyArrivalCount onsets, refined to the peak
    /// sample inside their 1 ms window, and the mean gap between them.
    std::array<double, kEarlyArrivalCount> earlyOnsetMs{};
    /// the engine's own first kEarlyArrivalCount Size-scaled line lengths, so
    /// clause 3(c) can assert that the audible arrivals ARE those lines.
    std::array<double, kEarlyArrivalCount> referenceTapMs{};
    std::size_t earlyOnsetCount = 0;
    double earlyMeanInterArrivalMs = 0.0;
};

/// @brief Render G-3 through one configuration and measure NED over the
///        geometry-derived window.
///
/// WINDOW DERIVATION (binding, spec SC-003 "Window definition"):
///   t_start = the first 1 ms window whose RMS exceeds peak * 0.01. Every
///             window before it is EXCLUDED from the denominator and the
///             excluded count is recorded.
///   W       = max(250 ms, 3 * m_long), with m_long read from the engine's own
///             getEffectiveDelayLengthSamples(N - 1).
/// At size = 1, m_long = 20348 samples = 423.9 ms, so W = 1.27 s - which is
/// SC-003's own stated figure. Plan delta D-5: at size = 0.5 the DERIVED term
/// (3 * 106.0 ms = 318 ms) governs, not the 250 ms floor; the formula is used
/// exactly as written and only the spec's prose example is wrong. The 250 ms
/// floor governs below size ~= 0.46. NO THRESHOLD MOVES.
[[nodiscard]] NedMeasurement measureEchoDensity(const EchoSetup& s) {
    AetherReverb engine;
    prepareEchoEngine(engine, s);

    // One zero control chunk so runControlStep() has materialised the
    // Size-scaled geometry before m_long is read. The network is still empty
    // and the input was silent, so the impulse response rendered next is
    // unaffected by it.
    {
        std::array<float, AetherReverb::kControlChunkSamples> zeros{};
        std::array<float, AetherReverb::kControlChunkSamples> settleL{};
        std::array<float, AetherReverb::kControlChunkSamples> settleR{};
        engine.processStereoBlock(zeros.data(), zeros.data(), settleL.data(), settleR.data(),
                                  zeros.size());
    }

    NedMeasurement m;
    const auto mShortSamples =
        static_cast<double>(engine.getEffectiveDelayLengthSamples(std::size_t{0}));
    const auto mLongSamples =
        static_cast<double>(engine.getEffectiveDelayLengthSamples(s.numChannels - std::size_t{1}));
    m.mShortMs = (mShortSamples / kEchoSampleRate) * 1000.0;
    m.mLongMs = (mLongSamples / kEchoSampleRate) * 1000.0;
    m.windowMs = std::max(250.0, 3.0 * m.mLongMs);
    for (std::size_t i = 0; i < kEarlyArrivalCount; ++i) {
        m.referenceTapMs[i] =
            (static_cast<double>(engine.getEffectiveDelayLengthSamples(i)) / kEchoSampleRate) *
            1000.0;
    }

    const auto analysisWindows = static_cast<std::size_t>(std::llround(m.windowMs));
    // t_start cannot land later than the first FDN arrival, which is m_short;
    // one extra m_long plus 100 ms of slack covers the input diffuser's own
    // pre-roll and any rounding, so the analysis window always fits.
    const auto guardWindows =
        static_cast<std::size_t>(std::ceil(m.mShortMs + m.mLongMs)) + std::size_t{100};
    const std::size_t totalWindows = analysisWindows + guardWindows;
    const std::size_t renderSamples = totalWindows * kNedWindowSamples;

    // --- G-3: unit impulse, 1.0 at sample 0 on both channels, wet-only -------
    std::vector<float> mono(renderSamples, 0.0f);
    std::vector<float> inL(kEchoBlockSamples, 0.0f);
    std::vector<float> inR(kEchoBlockSamples, 0.0f);
    std::vector<float> outL(kEchoBlockSamples, 0.0f);
    std::vector<float> outR(kEchoBlockSamples, 0.0f);

    std::size_t done = 0;
    bool impulseSent = false;
    while (done < renderSamples) {
        const std::size_t n = std::min(kEchoBlockSamples, renderSamples - done);
        std::fill(inL.begin(), inL.end(), 0.0f);
        std::fill(inR.begin(), inR.end(), 0.0f);
        if (!impulseSent) {
            inL[0] = 1.0f;
            inR[0] = 1.0f;
            impulseSent = true;
        }
        engine.processStereoBlock(inL.data(), inR.data(), outL.data(), outR.data(), n);
        for (std::size_t k = 0; k < n; ++k) {
            mono[done + k] = 0.5f * (outL[k] + outR[k]);
        }
        done += n;
    }

    // --- RMS per 1 ms window, peak over the whole rendered IR ----------------
    std::vector<double> windowRms(totalWindows, 0.0);
    for (std::size_t w = 0; w < totalWindows; ++w) {
        double sum = 0.0;
        for (std::size_t i = 0; i < kNedWindowSamples; ++i) {
            const auto v = static_cast<double>(mono[(w * kNedWindowSamples) + i]);
            sum += v * v;
        }
        windowRms[w] = std::sqrt(sum / static_cast<double>(kNedWindowSamples));
        m.peakWindowRms = std::max(m.peakWindowRms, windowRms[w]);
    }
    const double threshold = m.peakWindowRms * kNedThresholdFraction;

    // --- t_start, and the excluded-window count ------------------------------
    std::size_t firstOccupied = totalWindows;
    for (std::size_t w = 0; w < totalWindows; ++w) {
        if (windowRms[w] > threshold) {
            firstOccupied = w;
            break;
        }
    }
    REQUIRE(firstOccupied < totalWindows);
    REQUIRE((firstOccupied + analysisWindows) <= totalWindows);
    m.excludedWindows = firstOccupied;
    m.tStartMs = static_cast<double>(firstOccupied);  // the windows are 1 ms wide
    m.analysedWindows = analysisWindows;

    // --- occupancy and inter-arrival statistics over [t_start, t_start + W) --
    std::size_t firstArrival = 0;
    std::size_t lastArrival = 0;
    std::size_t occupied = 0;
    for (std::size_t w = firstOccupied; w < (firstOccupied + analysisWindows); ++w) {
        if (windowRms[w] > threshold) {
            if (occupied == 0u) {
                firstArrival = w;
            }
            lastArrival = w;
            ++occupied;
        }
    }
    m.occupiedWindows = occupied;
    m.ned = static_cast<double>(occupied) / static_cast<double>(analysisWindows);
    m.meanInterArrivalMs =
        (occupied >= 2u) ? (static_cast<double>(lastArrival - firstArrival) /
                            static_cast<double>(occupied - std::size_t{1}))
                         : m.windowMs;

    // --- clause 3(c): the first kEarlyArrivalCount ONSETS -------------------
    // An onset is an above-threshold window whose predecessor is below it, so a
    // run of contiguous occupied windows counts once. The onset's TIME is the
    // peak sample inside that window, which removes the 1 ms bin from the
    // measured quantity while leaving the spec's 1 ms detection grid and its
    // -40 dB threshold exactly as written. See kEarlyArrivalCount.
    bool previousAbove = false;
    for (std::size_t w = 0; w < totalWindows && m.earlyOnsetCount < kEarlyArrivalCount; ++w) {
        const bool above = windowRms[w] > threshold;
        if (above && !previousAbove) {
            const std::size_t base = w * kNedWindowSamples;
            std::size_t peakIndex = base;
            double peakMagnitude = 0.0;
            for (std::size_t i = 0; i < kNedWindowSamples; ++i) {
                const double magnitude = std::fabs(static_cast<double>(mono[base + i]));
                if (magnitude > peakMagnitude) {
                    peakMagnitude = magnitude;
                    peakIndex = base + i;
                }
            }
            m.earlyOnsetMs[m.earlyOnsetCount] =
                (static_cast<double>(peakIndex) / kEchoSampleRate) * 1000.0;
            ++m.earlyOnsetCount;
        }
        previousAbove = above;
    }
    if (m.earlyOnsetCount >= 2u) {
        m.earlyMeanInterArrivalMs =
            (m.earlyOnsetMs[m.earlyOnsetCount - std::size_t{1}] - m.earlyOnsetMs[0]) /
            static_cast<double>(m.earlyOnsetCount - std::size_t{1});
    }
    return m;
}

/// @brief SC-003 clause 1's recording requirement: per configuration, t_start,
///        m_short, m_long, W, the excluded window count and the measured NED,
///        so a future failure is diagnosable as geometry or as density.
void recordEchoMeasurement(const EchoSetup& s, const NedMeasurement& m) {
    WARN("SC-003: size=" << s.size << " dim=" << s.dimensionality << " density=" << s.density
                         << " N=" << s.numChannels << " | m_short=" << m.mShortMs
                         << " ms, m_long=" << m.mLongMs << " ms, t_start=" << m.tStartMs
                         << " ms, excluded=" << m.excludedWindows << " windows, W=" << m.windowMs
                         << " ms, occupied=" << m.occupiedWindows << "/" << m.analysedWindows
                         << ", peakWindowRms=" << m.peakWindowRms << ", NED=" << m.ned
                         << ", mean inter-arrival=" << m.meanInterArrivalMs << " ms"
                         << " (whole-window figure, saturates at the 1 ms bin - see"
                            " kEarlyArrivalCount)"
                         << ", early onsets=" << m.earlyOnsetCount << " -> early mean="
                         << m.earlyMeanInterArrivalMs << " ms");
}

}  // namespace

// ==============================================================================
// T005 - SC-003 clauses 1, 2 and 3(c). Always-on core; the full grid is the
//        [.slow] case below. Rendered audio here is ~11 s (SC-003's B-5 ledger
//        entry allows ~22 s).
// ==============================================================================
TEST_CASE("AetherReverb_EchoDensity", "[effects][aether]") {
    // --------------------------------------------------------------------------
    // Clause 1 - NED >= 0.80 over the derived window, at the default density.
    // --------------------------------------------------------------------------
    SECTION("SC-003 clause 1: NED >= 0.80 across Size x Dimensionality at N = 8") {
        for (const float size : {0.0f, 0.5f, 1.0f}) {
            for (const float dim : {0.0f, 1.0f}) {
                EchoSetup s;
                s.size = size;
                s.dimensionality = dim;
                const NedMeasurement m = measureEchoDensity(s);
                recordEchoMeasurement(s, m);
                INFO("size=" << size << " dimensionality=" << dim << " NED=" << m.ned << " over W="
                             << m.windowMs << " ms (t_start=" << m.tStartMs << " ms)");
                REQUIRE(m.ned >= 0.80);
            }
        }
    }

    // --------------------------------------------------------------------------
    // Clause 2 - Density monotonicity, with FR-044's plate-like sparse extreme
    // as SC-003's negative control. Measured over the SAME derived window, which
    // at size = 0.5 is 3 * 106.0 ms = 318 ms (plan delta D-5).
    // --------------------------------------------------------------------------
    SECTION("SC-003 clause 2: NED is non-decreasing in Density, strictly lower at 0 than at 1") {
        const std::array<float, 5> densities = {0.0f, 0.25f, 0.5f, 0.75f, 1.0f};
        std::array<double, 5> ned{};

        for (std::size_t i = 0; i < densities.size(); ++i) {
            EchoSetup s;
            s.size = 0.5f;
            s.dimensionality = 0.5f;
            s.density = densities[i];
            const NedMeasurement m = measureEchoDensity(s);
            recordEchoMeasurement(s, m);
            ned[i] = m.ned;
        }

        for (std::size_t i = 1; i < ned.size(); ++i) {
            INFO("density " << densities[i - 1] << " -> " << densities[i] << " : NED "
                            << ned[i - 1] << " -> " << ned[i]);
            REQUIRE(ned[i] >= ned[i - 1]);
        }
        INFO("NED(density=0)=" << ned.front() << " NED(density=1)=" << ned.back());
        REQUIRE(ned.front() < ned.back());
    }

    // --------------------------------------------------------------------------
    // Clause 3(c) - the audible consequence of Size, measured where it is
    // resolvable. At density = 0 the input diffuser is disengaged (FR-044), so
    // the EARLY arrivals are distinguishable; at the 0.7 default clause 1's
    // NED >= 0.8 would saturate the mean inter-arrival at 1 ms regardless of
    // geometry and the clause would be vacuous.
    //
    // The mean is taken over the first kEarlyArrivalCount ONSETS, not over every
    // occupied window of the derived NED window. Read the block comment on
    // kEarlyArrivalCount before changing this: over the whole window the
    // estimator is pinned to the 1 ms bin and the clause reduces to
    // "NED(size=1, density=0) <= 0.125", which contradicts SC-003's own headline
    // criterion. The bins, the -40 dB threshold, the size pair, the expected
    // value and the +/- 15 % tolerance are all unchanged.
    //
    // Clauses 3(a) and 3(b) are in AetherReverb_GeometryAndModalDensity (T002).
    // --------------------------------------------------------------------------
    SECTION("SC-003 clause 3(c): mean inter-arrival time scales with S at density = 0") {
        EchoSetup smallRoom;
        smallRoom.size = 0.25f;
        smallRoom.dimensionality = 0.5f;
        smallRoom.density = 0.0f;

        EchoSetup largeRoom = smallRoom;
        largeRoom.size = 1.0f;

        const NedMeasurement mSmall = measureEchoDensity(smallRoom);
        const NedMeasurement mLarge = measureEchoDensity(largeRoom);
        recordEchoMeasurement(smallRoom, mSmall);
        recordEchoMeasurement(largeRoom, mLarge);

        // RESOLVABILITY GUARD - the clause's own precondition, asserted rather
        // than assumed. Both configurations must expose the same arrival set:
        // kEarlyArrivalCount onsets, each one landing on the corresponding
        // Size-scaled line tap to within a single 1 ms detection window. This is
        // what makes the comparison below a measurement of the AUDIO rather than
        // of the bin width, and it is what fails if a fourth event ever intrudes
        // into the early region at one Size but not the other.
        REQUIRE(mSmall.earlyOnsetCount == kEarlyArrivalCount);
        REQUIRE(mLarge.earlyOnsetCount == kEarlyArrivalCount);
        for (std::size_t i = 0; i < kEarlyArrivalCount; ++i) {
            INFO("arrival " << i << ": small onset " << mSmall.earlyOnsetMs[i] << " ms vs line "
                            << mSmall.referenceTapMs[i] << " ms | large onset "
                            << mLarge.earlyOnsetMs[i] << " ms vs line " << mLarge.referenceTapMs[i]
                            << " ms");
            REQUIRE(std::fabs(mSmall.earlyOnsetMs[i] - mSmall.referenceTapMs[i]) <= kNedWindowMs);
            REQUIRE(std::fabs(mLarge.earlyOnsetMs[i] - mLarge.referenceTapMs[i]) <= kNedWindowMs);
        }
        REQUIRE(mSmall.earlyMeanInterArrivalMs > 0.0);

        // The expected ratio is the ratio of the two S values, recomputed from
        // FR-012's mapping S(v) = 0.25 * 2^(4v) independently of the header:
        // S(0.25) = 0.5 and S(1.0) = 4.0, so the ratio is 8.0.
        //
        // NOTE on tasks.md T005 clause 3: its parenthetical arithmetic
        // "S(1.0)/S(0.25) = 4/0.25 = 16" evaluates S at 0, not at 0.25 - 0.25 is
        // the SIZE, 0.25 is also S(0), and the two were conflated. The binding
        // instruction in the same sentence is "measured as the ratio of the two
        // means against the ratio of the two S values", which is exactly what is
        // asserted here, at the spec's own 15 % tolerance. NO THRESHOLD MOVES.
        const double sRatio = static_cast<double>(sizeScaleRef(largeRoom.size)) /
                              static_cast<double>(sizeScaleRef(smallRoom.size));
        const double measuredRatio =
            mLarge.earlyMeanInterArrivalMs / mSmall.earlyMeanInterArrivalMs;

        WARN("SC-003 clause 3(c): early mean inter-arrival "
             << mSmall.earlyMeanInterArrivalMs << " ms (size=0.25) -> "
             << mLarge.earlyMeanInterArrivalMs << " ms (size=1.0), measured ratio " << measuredRatio
             << " vs S ratio " << sRatio << " | whole-window means (saturated at the 1 ms bin, see"
             << " kEarlyArrivalCount) were " << mSmall.meanInterArrivalMs << " -> "
             << mLarge.meanInterArrivalMs << " ms");
        INFO("measured ratio=" << measuredRatio << " expected S ratio=" << sRatio
                               << " (+/- 15 %)");
        REQUIRE(measuredRatio >= (0.85 * sRatio));
        REQUIRE(measuredRatio <= (1.15 * sRatio));
    }
}

// ==============================================================================
// T005 - SC-003 clause 1's full 5 x 3 x 2 grid. [.slow]: nightly lane only
// (B-2), because 30 impulse renders at up to 1.9 s each is far outside B-1's
// 60 s always-on budget. Nothing is pruned or relaxed - this is a demotion to
// the nightly lane, which is what B-2 prescribes.
// ==============================================================================
TEST_CASE("AetherReverb_EchoDensityFullGrid", "[effects][aether][.slow]") {
    for (const float size : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f}) {
        for (const float dim : {0.0f, 0.5f, 1.0f}) {
            for (const std::size_t order : {std::size_t{8}, std::size_t{16}}) {
                EchoSetup s;
                s.size = size;
                s.dimensionality = dim;
                s.numChannels = order;
                const NedMeasurement m = measureEchoDensity(s);
                recordEchoMeasurement(s, m);
                INFO("size=" << size << " dimensionality=" << dim << " N=" << order
                             << " NED=" << m.ned);
                REQUIRE(m.ned >= 0.80);
            }
        }
    }
}

// ==============================================================================
// T008 / T010 fixtures - SC-016. Clauses 1 and 2 are the shimmer half; clauses 3
// and 4 are the bloom / freeze half (T010) and share the constants below.
// ==============================================================================

namespace {

constexpr double kPiSc16 = 3.14159265358979323846;
constexpr double kSc16SampleRate = 48000.0;
constexpr std::size_t kSc16Block = 512;
constexpr std::size_t kSc16OneSecond = 48000;
constexpr std::size_t kSc16ExciteSamples = 2u * kSc16OneSecond;    ///< G-4: 2 s
constexpr std::size_t kSc16TotalSamples = 8u * kSc16OneSecond;     ///< + 6 s of tail
constexpr std::size_t kSc16AnalysisSamples = 4u * kSc16OneSecond;  ///< the last 4 s

/// f0 and the two bands the clauses name.
constexpr double kSc16F0 = 220.0;
constexpr double kSc16BandCents = 50.0;  ///< +/-50 cents, spec.md:2040

/// SC-016 pins neither Decay nor Size. 20 s keeps the tail well above the
/// analysis floor across the whole [4 s, 8 s) window (the level falls ~12 dB
/// across it), and Size = 0.5 puts the geometry at S = 1.0, i.e. the unscaled
/// reference table. damping = 0 so the 2*f0 band is not attenuated by the
/// FR-031 tilt, which would confound a shimmer measurement with a damping one.
constexpr float kSc16Size = 0.5f;
constexpr float kSc16DecaySeconds = 20.0f;

/// Analysis frame: 0.5 s -> 2 Hz bins, Hann, 50 % overlap, 15 frames over the
/// 4 s window. 2 Hz resolves the narrowest band this test uses (the +/-50-cent
/// band at 220 Hz is 12.7 Hz wide, i.e. 7 bins), and Hann sidelobes put the
/// 220 Hz fundamental more than 100 dB below the 440 Hz band it must not leak
/// into - which a Butterworth band-pass of the same width could not do (a
/// 4th-order pair only reaches ~23 dB of rejection one octave out, and clause
/// 1(a) is a comparison against a near-floor reference band).
constexpr std::size_t kSc16FrameLen = 24000;
constexpr std::size_t kSc16FrameHop = 12000;

/// @brief G-4: one 220 Hz sine, zero phase, peak 0.5.
[[nodiscard]] std::vector<float> makeSinePartial(std::size_t numSamples, double sampleRate,
                                                 double freqHz) {
    std::vector<float> out(numSamples, 0.0f);
    for (std::size_t i = 0; i < numSamples; ++i) {
        out[i] = static_cast<float>(
            0.5 * std::sin(2.0 * kPiSc16 * freqHz * static_cast<double>(i) / sampleRate));
    }
    return out;
}

/// @brief Energy in a +/-@p cents band around @p centreHz, in dB.
///
/// Direct DFT over exactly the bins inside the band, accumulated across
/// Hann-windowed frames with a rotating double-precision phasor (4 multiplies
/// per sample per bin; the phasor's modulus drifts by ~5e-12 over a frame, three
/// orders below anything measured here). Normalised by frame count and window
/// power so the figure is a level, not a length-dependent sum - though only
/// DIFFERENCES between renders are asserted on.
[[nodiscard]] double bandLevelDb(const std::vector<float>& mono, double centreHz, double cents) {
    const double binHz = kSc16SampleRate / static_cast<double>(kSc16FrameLen);
    const double lo = centreHz * std::exp2(-cents / 1200.0);
    const double hi = centreHz * std::exp2(cents / 1200.0);
    const auto binLo = static_cast<std::size_t>(std::ceil(lo / binHz));
    auto binHi = static_cast<std::size_t>(std::floor(hi / binHz));
    if (binHi < binLo) {
        binHi = binLo;  // a band narrower than one bin collapses to its centre bin
    }

    std::vector<double> win(kSc16FrameLen, 0.0);
    double winPower = 0.0;
    for (std::size_t i = 0; i < kSc16FrameLen; ++i) {
        win[i] = 0.5 - (0.5 * std::cos(2.0 * kPiSc16 * static_cast<double>(i) /
                                       static_cast<double>(kSc16FrameLen)));
        winPower += win[i] * win[i];
    }

    double energy = 0.0;
    std::size_t frames = 0;
    for (std::size_t off = 0; (off + kSc16FrameLen) <= mono.size(); off += kSc16FrameHop) {
        for (std::size_t bin = binLo; bin <= binHi; ++bin) {
            const double w =
                2.0 * kPiSc16 * static_cast<double>(bin) / static_cast<double>(kSc16FrameLen);
            const double cw = std::cos(w);
            const double sw = std::sin(w);
            double phRe = 1.0;
            double phIm = 0.0;
            double re = 0.0;
            double im = 0.0;
            for (std::size_t i = 0; i < kSc16FrameLen; ++i) {
                const double s = static_cast<double>(mono[off + i]) * win[i];
                re += s * phRe;
                im -= s * phIm;
                const double nextRe = (phRe * cw) - (phIm * sw);
                phIm = (phRe * sw) + (phIm * cw);
                phRe = nextRe;
            }
            energy += (re * re) + (im * im);
        }
        ++frames;
    }
    const double norm = static_cast<double>(std::max<std::size_t>(frames, 1u)) * winPower;
    return 10.0 * std::log10(std::max(energy / std::max(norm, 1e-300), 1e-300));
}

/// @brief Render G-4 + tail at the given pair of shimmer sends and return the
///        mono last-4-s analysis window.
///
/// P-1 (no breath, no tide, no per-line jitter), P-2 (maxDelaySeconds = 0.5,
/// getMaxSizeScale() == 4.0f), P-3 (setMix(1)) and P-4 (N = 8). The bloom and
/// spectral stages are OFF at prepare: clauses 1-2 are about FR-051's two
/// independent shimmer sends, and setSpectralDiffusion's default of 0 would make
/// the STFT stage a pass-through that only adds latency to both paths.
[[nodiscard]] std::vector<float> renderShimmerCase(float octaveSend, float fifthSend) {
    AetherReverb engine;
    AetherReverb::PrepareConfig cfg;
    cfg.numChannels = 8;  // P-4
    cfg.maxBlockSamples = kSc16Block;
    cfg.maxDelaySeconds = 0.5f;  // P-2
    cfg.shimmerEnabled = true;
    cfg.bloomEnabled = false;
    cfg.spectralDiffusionEnabled = false;
    engine.prepare(kSc16SampleRate, cfg);

    REQUIRE(engine.getMaxSizeScale() == 4.0f);  // P-2
    REQUIRE(engine.isShimmerActive());          // RA-6

    engine.setSizeBreathDepth(0.0f);  // P-1
    engine.setDimensionalityTideDepth(0.0f);
    engine.setModDepth(0.0f);
    engine.setMix(1.0f);  // P-3
    engine.setSize(kSc16Size);
    engine.setDecaySeconds(kSc16DecaySeconds);
    engine.setDamping(0.0f);
    engine.setShimmerOctaveSend(octaveSend);
    engine.setShimmerFifthSend(fifthSend);
    engine.setBloomSend(0.0f);

    const std::vector<float> excite =
        makeSinePartial(kSc16ExciteSamples, kSc16SampleRate, kSc16F0);

    std::vector<float> mono;
    mono.reserve(kSc16AnalysisSamples);

    std::vector<float> inL(kSc16Block, 0.0f);
    std::vector<float> inR(kSc16Block, 0.0f);
    std::vector<float> outL(kSc16Block, 0.0f);
    std::vector<float> outR(kSc16Block, 0.0f);

    const std::size_t analysisBegin = kSc16TotalSamples - kSc16AnalysisSamples;
    for (std::size_t base = 0; base < kSc16TotalSamples; base += kSc16Block) {
        const std::size_t nb = std::min(kSc16Block, kSc16TotalSamples - base);
        for (std::size_t k = 0; k < nb; ++k) {
            const std::size_t idx = base + k;
            const float v = (idx < kSc16ExciteSamples) ? excite[idx] : 0.0f;
            inL[k] = v;
            inR[k] = v;
        }
        engine.processStereoBlock(inL.data(), inR.data(), outL.data(), outR.data(), nb);
        for (std::size_t k = 0; k < nb; ++k) {
            if ((base + k) >= analysisBegin) {
                mono.push_back(0.5f * (outL[k] + outR[k]));
            }
        }
    }
    return mono;
}

/// @brief The three band levels each clause needs, from one render.
struct Sc16Bands {
    double f0 = 0.0;
    double fifth = 0.0;   ///< 1.5 * f0
    double octave = 0.0;  ///< 2 * f0
};

[[nodiscard]] Sc16Bands measureSc16Bands(const std::vector<float>& mono) {
    Sc16Bands b;
    b.f0 = bandLevelDb(mono, kSc16F0, kSc16BandCents);
    b.fifth = bandLevelDb(mono, 1.5 * kSc16F0, kSc16BandCents);
    b.octave = bandLevelDb(mono, 2.0 * kSc16F0, kSc16BandCents);
    return b;
}

void recordSc16Bands(const char* label, const Sc16Bands& b) {
    WARN("SC-016 " << label << ": L(f0=" << kSc16F0 << ")=" << b.f0 << " dB, L(1.5f0="
                   << (1.5 * kSc16F0) << ")=" << b.fifth << " dB, L(2f0=" << (2.0 * kSc16F0)
                   << ")=" << b.octave << " dB");
}

// ------------------------------------------------------------------------------
// T010 - SC-016 clause 3 (the harmonic bloom) analysis instrument.
//
// Clause 3 is written in 1/3-OCTAVE bands over [100 Hz, 10 kHz], not in the
// +/-50-cent bands clauses 1/2 use, because it has to compare the four TARGET
// bands against the MEAN OF EVERY OTHER BAND. That is ~19 bands, and the widest
// of them (the 8 kHz band) is 1850 Hz across - 925 bins at bandLevelDb()'s 2 Hz
// resolution, each costing a 24 000-sample rotating-phasor DFT per frame. The
// direct-DFT instrument is right for three narrow bands and quadratically wrong
// for nineteen wide ones, so this clause uses a Welch estimate off the repo's own
// pffft-backed FFT (primitives/fft.h) instead: 8192-point Hann frames at 50 %
// overlap, 5.86 Hz bins, ~45 frames over the 4 s window.
// ------------------------------------------------------------------------------

constexpr std::size_t kSc16FftSize = 8192;  ///< == Krate::DSP::kMaxFFTSize
constexpr double kSc16BandLoHz = 100.0;     ///< spec.md:2075's analysis range
constexpr double kSc16BandHiHz = 10000.0;
constexpr std::size_t kSc16BloomPartialCount = 4;  ///< {f0, 2f0, 3f0, 4f0}

/// The post-note-off phase: 6 s, of which the last 4 s are analysed. Long enough
/// for a Q=400 resonator at f0 to fall ~30 dB below its release level (its
/// per-sample r gives an amplitude time constant of ~0.58 s at 220 Hz).
constexpr std::size_t kSc16ReleaseSamples = 6u * kSc16OneSecond;

struct ThirdOctaveBand {
    double centre = 0.0;
    double lo = 0.0;
    double hi = 0.0;
};

/// @brief Base-2 one-third-octave bands with centres inside [100 Hz, 10 kHz].
[[nodiscard]] std::vector<ThirdOctaveBand> makeThirdOctaveBands() {
    std::vector<ThirdOctaveBand> bands;
    for (int k = -12; k <= 12; ++k) {
        const double centre = 1000.0 * std::exp2(static_cast<double>(k) / 3.0);
        if ((centre < kSc16BandLoHz) || (centre > kSc16BandHiHz)) {
            continue;
        }
        bands.push_back(ThirdOctaveBand{centre, centre * std::exp2(-1.0 / 6.0),
                                        centre * std::exp2(1.0 / 6.0)});
    }
    return bands;
}

/// @brief Welch power per 1/3-octave band, in dB. One entry per band.
[[nodiscard]] std::vector<double> thirdOctaveLevelsDb(const std::vector<float>& mono,
                                                      const std::vector<ThirdOctaveBand>& bands) {
    Krate::DSP::FFT fft;
    fft.prepare(kSc16FftSize);
    const std::size_t numBins = fft.numBins();

    std::vector<double> win(kSc16FftSize, 0.0);
    for (std::size_t i = 0; i < kSc16FftSize; ++i) {
        win[i] = 0.5 - (0.5 * std::cos(2.0 * kPiSc16 * static_cast<double>(i) /
                                       static_cast<double>(kSc16FftSize)));
    }

    std::vector<float> frame(kSc16FftSize, 0.0f);
    std::vector<Krate::DSP::Complex> spectrum(numBins);
    std::vector<double> power(numBins, 0.0);

    std::size_t frames = 0;
    const std::size_t hop = kSc16FftSize / 2u;
    for (std::size_t off = 0; (off + kSc16FftSize) <= mono.size(); off += hop) {
        for (std::size_t i = 0; i < kSc16FftSize; ++i) {
            frame[i] = static_cast<float>(static_cast<double>(mono[off + i]) * win[i]);
        }
        fft.forward(frame.data(), spectrum.data());
        for (std::size_t b = 0; b < numBins; ++b) {
            const double re = static_cast<double>(spectrum[b].real);
            const double im = static_cast<double>(spectrum[b].imag);
            power[b] += (re * re) + (im * im);
        }
        ++frames;
    }
    const double invFrames = 1.0 / static_cast<double>(std::max<std::size_t>(frames, 1u));
    const double binHz = kSc16SampleRate / static_cast<double>(kSc16FftSize);

    std::vector<double> levels(bands.size(), 0.0);
    for (std::size_t bi = 0; bi < bands.size(); ++bi) {
        double sum = 0.0;
        for (std::size_t b = 0; b < numBins; ++b) {
            const double f = static_cast<double>(b) * binHz;
            if ((f >= bands[bi].lo) && (f < bands[bi].hi)) {
                sum += power[b] * invFrames;
            }
        }
        levels[bi] = 10.0 * std::log10(std::max(sum, 1e-300));
    }
    return levels;
}

/// @brief Index of the band that contains @p freqHz. Fails the test if none does.
[[nodiscard]] std::size_t bandIndexContaining(const std::vector<ThirdOctaveBand>& bands,
                                              double freqHz) {
    for (std::size_t i = 0; i < bands.size(); ++i) {
        if ((freqHz >= bands[i].lo) && (freqHz < bands[i].hi)) {
            return i;
        }
    }
    FAIL("no 1/3-octave band in [100 Hz, 10 kHz] contains " << freqHz << " Hz");
    return 0u;
}

/// @brief One clause-3 render: G-4, the bloom tuned to {f0, 2f0, 3f0, 4f0},
///        optionally followed by a note-off and a settling phase.
struct BloomRender {
    std::vector<float> held;      ///< the last 4 s of the 8 s held-note phase
    std::vector<float> released;  ///< the last 4 s of the release phase (empty if unused)
    std::size_t minActiveHeld = 0;     ///< min getActiveBloomResonatorCount() while held
    std::size_t activeAfterRelease = 0;
    std::size_t latencySamples = 0;
};

/// @brief SC-016 clause 3's protocol at one bloom send.
///
/// P-1 (no breath, no tide, no per-line jitter), P-2 (maxDelaySeconds = 0.5,
/// getMaxSizeScale() == 4.0f), P-3 (setMix(1)) and P-4 (N = 8), matching
/// renderShimmerCase. BOTH SHIMMER SENDS ARE 0 and the taps are not even
/// allocated: clause 3 measures the bloom alone, and the reference render differs
/// from the subject in `bloomSend` and in NOTHING ELSE - `bloomNoteOn` is called
/// identically in both, so the bank is tuned in both and only the return gain
/// differs (spec.md:2073-2075).
///
/// @param spectralEnabled  FR-065's teeth: the clause runs with the spectral
///        stage on (the shipped default) AND off, and demands the same emphasis
///        from both, because the bloom performs no spectral analysis and must not
///        have acquired a dependency on the stage that does.
[[nodiscard]] BloomRender renderBloomCase(float bloomSend, bool spectralEnabled, bool doRelease) {
    AetherReverb engine;
    AetherReverb::PrepareConfig cfg;
    cfg.numChannels = 8;  // P-4
    cfg.maxBlockSamples = kSc16Block;
    cfg.maxDelaySeconds = 0.5f;  // P-2
    cfg.shimmerEnabled = false;
    cfg.bloomEnabled = true;
    cfg.spectralDiffusionEnabled = spectralEnabled;
    engine.prepare(kSc16SampleRate, cfg);

    REQUIRE(engine.getMaxSizeScale() == 4.0f);  // P-2

    engine.setSizeBreathDepth(0.0f);  // P-1
    engine.setDimensionalityTideDepth(0.0f);
    engine.setModDepth(0.0f);
    engine.setMix(1.0f);  // P-3
    engine.setSize(kSc16Size);
    engine.setDecaySeconds(kSc16DecaySeconds);
    engine.setDamping(0.0f);
    engine.setShimmerOctaveSend(0.0f);
    engine.setShimmerFifthSend(0.0f);
    engine.setBloomSend(bloomSend);
    engine.setBloomDecay(1.0f);

    const std::array<float, kSc16BloomPartialCount> partials = {
        static_cast<float>(kSc16F0), static_cast<float>(2.0 * kSc16F0),
        static_cast<float>(3.0 * kSc16F0), static_cast<float>(4.0 * kSc16F0)};
    engine.bloomNoteOn(0, partials.data(), partials.size());

    BloomRender out;
    out.latencySamples = engine.getLatencySamples();
    out.minActiveHeld = engine.getActiveBloomResonatorCount();
    out.held.reserve(kSc16AnalysisSamples);
    if (doRelease) {
        out.released.reserve(kSc16AnalysisSamples);
    }

    const std::vector<float> excite =
        makeSinePartial(kSc16ExciteSamples, kSc16SampleRate, kSc16F0);

    std::vector<float> inL(kSc16Block, 0.0f);
    std::vector<float> inR(kSc16Block, 0.0f);
    std::vector<float> outL(kSc16Block, 0.0f);
    std::vector<float> outR(kSc16Block, 0.0f);

    const std::size_t heldBegin = kSc16TotalSamples - kSc16AnalysisSamples;
    for (std::size_t base = 0; base < kSc16TotalSamples; base += kSc16Block) {
        const std::size_t nb = std::min(kSc16Block, kSc16TotalSamples - base);
        for (std::size_t k = 0; k < nb; ++k) {
            const std::size_t idx = base + k;
            const float v = (idx < kSc16ExciteSamples) ? excite[idx] : 0.0f;
            inL[k] = v;
            inR[k] = v;
        }
        engine.processStereoBlock(inL.data(), inR.data(), outL.data(), outR.data(), nb);
        out.minActiveHeld = std::min(out.minActiveHeld, engine.getActiveBloomResonatorCount());
        for (std::size_t k = 0; k < nb; ++k) {
            if ((base + k) >= heldBegin) {
                out.held.push_back(0.5f * (outL[k] + outR[k]));
            }
        }
    }

    if (doRelease) {
        engine.bloomNoteOff(0);
        std::fill(inL.begin(), inL.end(), 0.0f);
        std::fill(inR.begin(), inR.end(), 0.0f);
        const std::size_t releaseBegin = kSc16ReleaseSamples - kSc16AnalysisSamples;
        for (std::size_t base = 0; base < kSc16ReleaseSamples; base += kSc16Block) {
            const std::size_t nb = std::min(kSc16Block, kSc16ReleaseSamples - base);
            engine.processStereoBlock(inL.data(), inR.data(), outL.data(), outR.data(), nb);
            for (std::size_t k = 0; k < nb; ++k) {
                if ((base + k) >= releaseBegin) {
                    out.released.push_back(0.5f * (outL[k] + outR[k]));
                }
            }
        }
        out.activeAfterRelease = engine.getActiveBloomResonatorCount();
    }
    return out;
}

/// @brief SC-016 clause 3, run at one spectral-stage setting.
///
/// @param doRelease  Whether to also measure the post-note-off fallback. The
///        release assertion is a property of the BANK, not of the spectral flag,
///        so only the default configuration pays for its extra 12 s of audio
///        (B-5's SC-016 ledger allows ~90 s).
void runSc16BloomClause(bool spectralEnabled, bool doRelease) {
    const std::vector<ThirdOctaveBand> bands = makeThirdOctaveBands();
    REQUIRE(bands.size() >= 15u);

    const BloomRender ref = renderBloomCase(0.0f, spectralEnabled, doRelease);
    const BloomRender on = renderBloomCase(1.0f, spectralEnabled, doRelease);
    REQUIRE(ref.held.size() == kSc16AnalysisSamples);
    REQUIRE(on.held.size() == kSc16AnalysisSamples);

    // FR-065's teeth: with the stage disabled at prepare the engine reports
    // exactly zero latency, and the SAME emphasis is still required below.
    if (!spectralEnabled) {
        REQUIRE(on.latencySamples == 0u);
        REQUIRE(ref.latencySamples == 0u);
    }

    // RA-7: a bank that never tuned anything would pass a broadband-leak test.
    REQUIRE(on.minActiveHeld > 0u);
    REQUIRE(ref.minActiveHeld > 0u);

    const std::vector<double> refLevels = thirdOctaveLevelsDb(ref.held, bands);
    const std::vector<double> onLevels = thirdOctaveLevelsDb(on.held, bands);

    std::array<std::size_t, kSc16BloomPartialCount> targets{};
    for (std::size_t p = 0; p < kSc16BloomPartialCount; ++p) {
        targets[p] = bandIndexContaining(bands, static_cast<double>(p + 1u) * kSc16F0);
    }

    for (std::size_t bi = 0; bi < bands.size(); ++bi) {
        WARN("SC-016 clause 3 (spectral=" << (spectralEnabled ? "on" : "off") << ") band "
                                          << bands[bi].centre << " Hz: ref=" << refLevels[bi]
                                          << " dB on=" << onLevels[bi]
                                          << " dB rise=" << (onLevels[bi] - refLevels[bi])
                                          << " dB");
    }

    for (std::size_t p = 0; p < kSc16BloomPartialCount; ++p) {
        const std::size_t bi = targets[p];
        const double rise = onLevels[bi] - refLevels[bi];
        INFO("clause 3 target partial " << (static_cast<double>(p + 1u) * kSc16F0) << " Hz, band "
                                        << bands[bi].centre << " Hz, rise = " << rise
                                        << " dB, spectral=" << (spectralEnabled ? "on" : "off"));
        REQUIRE(rise >= 6.0);
    }

    double nonTargetSum = 0.0;
    std::size_t nonTargetCount = 0;
    for (std::size_t bi = 0; bi < bands.size(); ++bi) {
        const bool isTarget = std::find(targets.begin(), targets.end(), bi) != targets.end();
        if (isTarget) {
            continue;
        }
        nonTargetSum += onLevels[bi] - refLevels[bi];
        ++nonTargetCount;
    }
    REQUIRE(nonTargetCount > 0u);
    const double nonTargetMean = nonTargetSum / static_cast<double>(nonTargetCount);
    WARN("SC-016 clause 3 (spectral=" << (spectralEnabled ? "on" : "off")
                                      << ") mean non-target rise over " << nonTargetCount
                                      << " bands = " << nonTargetMean << " dB");
    INFO("clause 3 mean non-target rise = " << nonTargetMean << " dB");
    REQUIRE(nonTargetMean <= 2.0);

    if (!doRelease) {
        return;
    }

    // bloomNoteOff releases the bank: the driven count drops to zero, the
    // resonators ring down instead of being cut, and the four target bands must
    // come back to the reference.
    REQUIRE(on.activeAfterRelease == 0u);
    REQUIRE(ref.released.size() == kSc16AnalysisSamples);
    REQUIRE(on.released.size() == kSc16AnalysisSamples);

    const std::vector<double> refRelease = thirdOctaveLevelsDb(ref.released, bands);
    const std::vector<double> onRelease = thirdOctaveLevelsDb(on.released, bands);
    for (std::size_t p = 0; p < kSc16BloomPartialCount; ++p) {
        const std::size_t bi = targets[p];
        const double residual = onRelease[bi] - refRelease[bi];
        WARN("SC-016 clause 3 release: band " << bands[bi].centre << " Hz residual = " << residual
                                              << " dB");
        INFO("clause 3 release residual at " << bands[bi].centre << " Hz = " << residual << " dB");
        REQUIRE(std::abs(residual) <= 2.0);
    }
}

// ------------------------------------------------------------------------------
// T010 - SC-016 clause 4: freeze mutes ALL THREE sends (FR-033 step 5, RA-5).
// ------------------------------------------------------------------------------

/// Clause 4's frozen tail: 15 s, measured in two 5 s windows (the first starting
/// once the latch has completed, the last ending at 15 s). B-5's ledger.
constexpr std::size_t kSc16FreezeWindowSamples = 5u * kSc16OneSecond;
constexpr std::size_t kSc16FreezeTailSamples = 15u * kSc16OneSecond;
/// The 50 ms FR-033 latch plus margin, rendered before the first window opens.
constexpr std::size_t kSc16LatchSamples = kSc16OneSecond / 4u;

struct Sc16FreezeTrace {
    double earlyOctaveDb = 0.0;
    double lateOctaveDb = 0.0;
};

/// @brief Clause 1's configuration PLUS the fifth send and a live bloom, then
///        freeze - so the clause really does measure "mutes all three".
[[nodiscard]] Sc16FreezeTrace renderSc16FreezeCase() {
    AetherReverb engine;
    AetherReverb::PrepareConfig cfg;
    cfg.numChannels = 8;  // P-4
    cfg.maxBlockSamples = kSc16Block;
    cfg.maxDelaySeconds = 0.5f;  // P-2
    cfg.shimmerEnabled = true;
    cfg.bloomEnabled = true;
    cfg.spectralDiffusionEnabled = false;
    engine.prepare(kSc16SampleRate, cfg);

    REQUIRE(engine.getMaxSizeScale() == 4.0f);  // P-2
    REQUIRE(engine.isShimmerActive());          // RA-6

    engine.setSizeBreathDepth(0.0f);  // P-1
    engine.setDimensionalityTideDepth(0.0f);
    engine.setModDepth(0.0f);
    engine.setMix(1.0f);  // P-3
    engine.setSize(kSc16Size);
    engine.setDecaySeconds(kSc16DecaySeconds);
    engine.setDamping(0.0f);
    engine.setShimmerOctaveSend(1.0f);
    engine.setShimmerFifthSend(1.0f);
    engine.setBloomSend(1.0f);
    engine.setBloomDecay(1.0f);

    const std::array<float, kSc16BloomPartialCount> partials = {
        static_cast<float>(kSc16F0), static_cast<float>(2.0 * kSc16F0),
        static_cast<float>(3.0 * kSc16F0), static_cast<float>(4.0 * kSc16F0)};
    engine.bloomNoteOn(0, partials.data(), partials.size());
    REQUIRE(engine.getActiveBloomResonatorCount() > 0u);

    const std::vector<float> excite =
        makeSinePartial(kSc16ExciteSamples, kSc16SampleRate, kSc16F0);

    std::vector<float> inL(kSc16Block, 0.0f);
    std::vector<float> inR(kSc16Block, 0.0f);
    std::vector<float> outL(kSc16Block, 0.0f);
    std::vector<float> outR(kSc16Block, 0.0f);

    // A render helper that optionally captures the mono output.
    const auto run = [&](std::size_t numSamples, const std::vector<float>* input,
                         std::size_t inputBase, std::vector<float>* capture) {
        for (std::size_t base = 0; base < numSamples; base += kSc16Block) {
            const std::size_t nb = std::min(kSc16Block, numSamples - base);
            for (std::size_t k = 0; k < nb; ++k) {
                float v = 0.0f;
                if (input != nullptr) {
                    const std::size_t idx = inputBase + base + k;
                    v = (idx < input->size()) ? (*input)[idx] : 0.0f;
                }
                inL[k] = v;
                inR[k] = v;
            }
            engine.processStereoBlock(inL.data(), inR.data(), outL.data(), outR.data(), nb);
            if (capture != nullptr) {
                for (std::size_t k = 0; k < nb; ++k) {
                    capture->push_back(0.5f * (outL[k] + outR[k]));
                }
            }
        }
    };

    run(kSc16ExciteSamples, &excite, 0u, nullptr);

    engine.setFreeze(true);
    run(kSc16LatchSamples, nullptr, 0u, nullptr);
    REQUIRE(engine.isFrozen());

    // A bloomNoteOn taken WHILE FROZEN is accepted and stored, but the return
    // stays muted for the freeze's duration (edge case 30). Exercising it here
    // means the clause fails if the freeze mute is applied at the note API rather
    // than on the return path.
    engine.bloomNoteOn(1, partials.data(), partials.size());
    REQUIRE(engine.getActiveBloomResonatorCount() > 0u);

    std::vector<float> early;
    early.reserve(kSc16FreezeWindowSamples);
    run(kSc16FreezeWindowSamples, nullptr, 0u, &early);

    run(kSc16FreezeTailSamples - (2u * kSc16FreezeWindowSamples), nullptr, 0u, nullptr);

    std::vector<float> late;
    late.reserve(kSc16FreezeWindowSamples);
    run(kSc16FreezeWindowSamples, nullptr, 0u, &late);

    Sc16FreezeTrace tr;
    tr.earlyOctaveDb = bandLevelDb(early, 2.0 * kSc16F0, kSc16BandCents);
    tr.lateOctaveDb = bandLevelDb(late, 2.0 * kSc16F0, kSc16BandCents);
    return tr;
}

}  // namespace

// ------------------------------------------------------------------------------
// SC-016, all four clauses (FR-050 - FR-059).
//
// Clauses 1 and 2 - the two shimmer sends are real, specific and INDEPENDENT.
// Three renders: the reference (both sends 0) and one per send at 1. A single
// shared gain feeding both taps fails both clauses, which is the measurement of
// FR-051 (spec.md:2065-2067).
//
// Clause 3 - the harmonic bloom reinforces the partials bloomNoteOn was given,
// and only those; then bloomNoteOff releases them. Run in TWO configurations so
// FR-065 has teeth (see the call site).
//
// Clause 4 - freezing mutes all three sends.
// ------------------------------------------------------------------------------
TEST_CASE("AetherReverb_ShimmerBloomEffect", "[effects][aether]") {
    const std::vector<float> refMono = renderShimmerCase(0.0f, 0.0f);
    const std::vector<float> octMono = renderShimmerCase(1.0f, 0.0f);
    const std::vector<float> fifthMono = renderShimmerCase(0.0f, 1.0f);
    REQUIRE(refMono.size() == kSc16AnalysisSamples);
    REQUIRE(octMono.size() == kSc16AnalysisSamples);
    REQUIRE(fifthMono.size() == kSc16AnalysisSamples);

    const Sc16Bands ref = measureSc16Bands(refMono);
    const Sc16Bands oct = measureSc16Bands(octMono);
    const Sc16Bands fifth = measureSc16Bands(fifthMono);

    recordSc16Bands("reference (both sends 0)", ref);
    recordSc16Bands("octave send = 1", oct);
    recordSc16Bands("fifth send = 1", fifth);

    // --------------------------------------------------------------------------
    // Clause 1 - the +12 send. Deliberately NOT wrapped in a Catch2 SECTION:
    // sections re-enter the whole case body, which would render all three
    // 8 s cases twice.
    // --------------------------------------------------------------------------
    {
        INFO("clause 1(a): L(2f0)=" << oct.octave << " L_ref(2f0)=" << ref.octave);
        REQUIRE(oct.octave >= (ref.octave + 12.0));

        INFO("clause 1(b): L(2f0)=" << oct.octave << " L(f0)=" << oct.f0);
        REQUIRE(oct.octave >= (oct.f0 - 20.0));

        const double bound = std::max(ref.fifth + 3.0, oct.octave - 12.0);
        INFO("clause 1(c): L(1.5f0)=" << oct.fifth << " bound=" << bound << " (L_ref(1.5f0)="
                                      << ref.fifth << ", L(2f0)-12=" << (oct.octave - 12.0) << ")");
        REQUIRE(oct.fifth <= bound);
    }

    // --------------------------------------------------------------------------
    // Clause 2 - the exact mirror for the +7 send.
    // --------------------------------------------------------------------------
    {
        INFO("clause 2(a): L(1.5f0)=" << fifth.fifth << " L_ref(1.5f0)=" << ref.fifth);
        REQUIRE(fifth.fifth >= (ref.fifth + 12.0));

        INFO("clause 2(b): L(1.5f0)=" << fifth.fifth << " L(f0)=" << fifth.f0);
        REQUIRE(fifth.fifth >= (fifth.f0 - 20.0));

        const double bound = std::max(ref.octave + 3.0, fifth.fifth - 12.0);
        INFO("clause 2(c): L(2f0)=" << fifth.octave << " bound=" << bound << " (L_ref(2f0)="
                                    << ref.octave << ", L(1.5f0)-12=" << (fifth.fifth - 12.0)
                                    << ")");
        REQUIRE(fifth.octave <= bound);
    }

    // --------------------------------------------------------------------------
    // Clause 3 - the harmonic bloom reinforces the partials it was told about,
    // and only those. RUN TWICE (spec.md:2068-2083 + FR-065): once with the
    // spectral stage at its shipped default, once with it disabled at prepare.
    // The second configuration is FR-065's ONLY teeth in the whole suite - no
    // other case combines a live bloom with the stage switched off - and it must
    // deliver the SAME >= 6 dB emphasis, because the bloom is note-informed (Q1)
    // and performs no spectral analysis at all.
    //
    // Only the default configuration pays for the extra release phase; the
    // note-off fallback is a property of the bank, not of the spectral flag.
    // --------------------------------------------------------------------------
    runSc16BloomClause(/*spectralEnabled=*/true, /*doRelease=*/true);
    runSc16BloomClause(/*spectralEnabled=*/false, /*doRelease=*/false);

    // --------------------------------------------------------------------------
    // Clause 4 - freeze mutes all three sends (FR-033 step 5; RA-5 records that
    // this is a SHIPPED behavioural limitation, not merely a test condition).
    // Measured from the effect side: the +/-50-cent band at 2*f0 - which both the
    // +12 shimmer leg and the bloom's own 2*f0 resonator feed - must STOP GROWING
    // the moment the latch completes. Any of the three returns left live inside a
    // unity-gain loop is an energy source, so the band would keep climbing.
    // --------------------------------------------------------------------------
    {
        const Sc16FreezeTrace fr = renderSc16FreezeCase();
        const double drift = fr.lateOctaveDb - fr.earlyOctaveDb;
        WARN("SC-016 clause 4: L(2f0) early=" << fr.earlyOctaveDb << " dB late=" << fr.lateOctaveDb
                                              << " dB drift=" << drift << " dB");
        INFO("clause 4: frozen 2f0 band drift over 15 s = " << drift << " dB (early="
                                                            << fr.earlyOctaveDb
                                                            << ", late=" << fr.lateOctaveDb << ")");
        REQUIRE(std::abs(drift) <= 0.5);
    }
}

// ==============================================================================
// T011 fixtures - SC-007 tail smoothness (FR-060 - FR-063), FR-052, FR-080
// ==============================================================================
//
// METRIC M-1 IS IMPLEMENTED HERE, LOCALLY, AND DELIBERATELY NOT DELEGATED TO
// calculateSpectralFlatness (tests/test_helpers/signal_metrics.h:326). That helper
// picks ONE FFT size capped at 4096 and windows only the FIRST fftSize samples
// (:337, :351), and it forms geomMean/arithMean over ALL non-DC bins (:397) - its
// own ceiling on ideal white noise is ~0.845, i.e. BELOW the 0.85 an absolute
// threshold would need. M-1 instead averages over NON-OVERLAPPING 4096-sample
// frames spanning the whole analysis window and restricts the bins to
// [80 Hz, 11 kHz], which is the band G-2 actually occupies. Every clause below is
// therefore a COMPARISON between renders measured with the same instrument, plus
// one recorded absolute ceiling M-1(G-5).
// ==============================================================================

namespace {

constexpr std::size_t kSc7Block = 512;
constexpr std::size_t kSc7OneSecond = 48000;
/// 6 s of continuously-driven G-2, of which the last 4 s are analysed. The engine
/// is measured in STEADY STATE rather than on a decaying tail on purpose: clause 5
/// compares WET RMS across the five smear amounts, and on a decaying tail that
/// figure is dominated by where in the decay the window happens to sit.
constexpr std::size_t kSc7RenderSamples = 6u * kSc7OneSecond;
constexpr std::size_t kSc7AnalysisSamples = 4u * kSc7OneSecond;
constexpr std::size_t kSc7RmsSamples = 2u * kSc7OneSecond;

constexpr std::size_t kSc7FftSize = 1024;  ///< the shipped default diffusionFftSize
constexpr std::size_t kSc7Hop75 = kSc7FftSize / 4u;
constexpr std::size_t kSc7Hop50 = kSc7FftSize / 2u;

constexpr std::size_t kSc7AmountCount = 5;
constexpr float kSc7Amounts[kSc7AmountCount] = {0.0f, 0.25f, 0.5f, 0.75f, 1.0f};

constexpr float kSc7Size = 0.5f;
/// SC-003 clause 1's always-on core, which clause 4(a) is specified over ("at
/// every size in SC-003 clause 1's always-on core"). kSc7Size is one of them, so
/// clause 4(a) reuses the amount-0 sweep render there and renders the other two.
constexpr std::size_t kSc7ClauseFourSizeCount = 3;
constexpr float kSc7ClauseFourSizes[kSc7ClauseFourSizeCount] = {0.0f, 0.5f, 1.0f};
/// Index into kSc7ClauseFourSizes whose value is kSc7Size, i.e. the one the
/// amount-0 sweep render already covers. static_assert'd below the array.
constexpr std::size_t kSc7ClauseFourReusedIndex = 1;
static_assert(kSc7ClauseFourSizes[kSc7ClauseFourReusedIndex] == kSc7Size,
              "clause 4(a) reuses sweep[0], which is rendered at kSc7Size");
constexpr float kSc7Density = 0.7f;  ///< clause 4(a)'s pinned density
constexpr float kSc7Damping = 0.4f;  ///< clause 4(a)'s pinned damping
constexpr float kSc7DecaySeconds = 8.0f;

/// Clause 3's two bounds, and clause 4's shared ridge / notch bound.
constexpr double kSc7SampleTolerance = 1e-4;
constexpr double kSc7ErrorRmsCeilingDb = -70.0;
constexpr double kSc7RidgeDb = 9.0;

// ------------------------------------------------------------------------------
// G-2 (plan S8.1): Xorshift32 at a pinned seed through a 4th-order Butterworth
// pair, 80 Hz .. 11 kHz, scaled to peak 0.5. Re-derived here rather than shared
// with aether_reverb_test.cpp - plan S8.1 puts one anonymous-namespace fixture
// block in each TU, and these are internal-linkage symbols in both.
// ------------------------------------------------------------------------------

constexpr double kSc7ButterQ4[2] = {0.541196100146197, 1.306562964876377};

struct Sc7Biquad {
    float b0 = 1.0f;
    float b1 = 0.0f;
    float b2 = 0.0f;
    float a1 = 0.0f;
    float a2 = 0.0f;
    float z1 = 0.0f;
    float z2 = 0.0f;

    [[nodiscard]] float process(float x) noexcept {
        const float y = (b0 * x) + z1;
        z1 = (b1 * x) - (a1 * y) + z2;
        z2 = (b2 * x) - (a2 * y);
        return y;
    }
};

[[nodiscard]] Sc7Biquad sc7Lowpass(double sampleRate, double cutoffHz, double q) {
    const double w0 = 2.0 * kPiSc16 * cutoffHz / sampleRate;
    const double cw = std::cos(w0);
    const double alpha = std::sin(w0) / (2.0 * q);
    const double a0 = 1.0 + alpha;
    Sc7Biquad b;
    b.b0 = static_cast<float>(((1.0 - cw) * 0.5) / a0);
    b.b1 = static_cast<float>((1.0 - cw) / a0);
    b.b2 = b.b0;
    b.a1 = static_cast<float>((-2.0 * cw) / a0);
    b.a2 = static_cast<float>((1.0 - alpha) / a0);
    return b;
}

[[nodiscard]] Sc7Biquad sc7Highpass(double sampleRate, double cutoffHz, double q) {
    const double w0 = 2.0 * kPiSc16 * cutoffHz / sampleRate;
    const double cw = std::cos(w0);
    const double alpha = std::sin(w0) / (2.0 * q);
    const double a0 = 1.0 + alpha;
    Sc7Biquad b;
    b.b0 = static_cast<float>(((1.0 + cw) * 0.5) / a0);
    b.b1 = static_cast<float>((-(1.0 + cw)) / a0);
    b.b2 = b.b0;
    b.a1 = static_cast<float>((-2.0 * cw) / a0);
    b.a2 = static_cast<float>((1.0 - alpha) / a0);
    return b;
}

[[nodiscard]] std::vector<float> makeSc7Noise(std::size_t numSamples, std::uint32_t seed) {
    Krate::DSP::Xorshift32 rng(seed);
    Sc7Biquad hp[2];
    Sc7Biquad lp[2];
    for (std::size_t k = 0; k < 2u; ++k) {
        hp[k] = sc7Highpass(kSc16SampleRate, 80.0, kSc7ButterQ4[k]);
        lp[k] = sc7Lowpass(kSc16SampleRate, 11000.0, kSc7ButterQ4[k]);
    }

    std::vector<float> out(numSamples, 0.0f);
    float peak = 0.0f;
    for (std::size_t i = 0; i < numSamples; ++i) {
        float v = rng.nextFloat();
        for (auto& s : hp) {
            v = s.process(v);
        }
        for (auto& s : lp) {
            v = s.process(v);
        }
        out[i] = v;
        peak = std::max(peak, std::abs(v));
    }
    if (peak > 0.0f) {
        const float g = 0.5f / peak;
        for (auto& v : out) {
            v *= g;
        }
    }
    return out;
}

// ------------------------------------------------------------------------------
// M-1: banded, frame-averaged spectral flatness, plus the per-bin peak-to-median
// figure clause 1(b) needs off the same frame set.
// ------------------------------------------------------------------------------

constexpr std::size_t kM1FrameLen = 4096;
constexpr double kM1LoHz = 80.0;
constexpr double kM1HiHz = 11000.0;

struct M1Result {
    double flatness = 0.0;       ///< mean over frames of exp(mean ln|X|)/mean|X|
    double standardError = 0.0;  ///< stddev / sqrt(frames)
    std::size_t frames = 0;
    double peakToMedianDb = 0.0;  ///< over the frame-AVERAGED in-band magnitudes
};

[[nodiscard]] M1Result measureM1(const std::vector<float>& mono) {
    Krate::DSP::FFT fft;
    fft.prepare(kM1FrameLen);
    const std::size_t numBins = fft.numBins();

    std::vector<double> win(kM1FrameLen, 0.0);
    for (std::size_t i = 0; i < kM1FrameLen; ++i) {
        win[i] = 0.5 - (0.5 * std::cos(2.0 * kPiSc16 * static_cast<double>(i) /
                                       static_cast<double>(kM1FrameLen)));
    }

    const double binHz = kSc16SampleRate / static_cast<double>(kM1FrameLen);
    const auto loBin = static_cast<std::size_t>(std::ceil(kM1LoHz / binHz));
    const auto hiBin =
        std::min(numBins - 1u, static_cast<std::size_t>(std::floor(kM1HiHz / binHz)));
    REQUIRE(hiBin > loBin);
    const double inBand = static_cast<double>((hiBin - loBin) + 1u);

    std::vector<float> frame(kM1FrameLen, 0.0f);
    std::vector<Krate::DSP::Complex> spectrum(numBins);
    std::vector<double> avgMag(numBins, 0.0);
    std::vector<double> perFrame;

    // NON-OVERLAPPING frames: overlapping ones share samples, so their flatness
    // values are correlated and SE = stddev/sqrt(frames) would understate the
    // uncertainty clause 1(c) tests against.
    for (std::size_t off = 0; (off + kM1FrameLen) <= mono.size(); off += kM1FrameLen) {
        for (std::size_t i = 0; i < kM1FrameLen; ++i) {
            frame[i] = static_cast<float>(static_cast<double>(mono[off + i]) * win[i]);
        }
        fft.forward(frame.data(), spectrum.data());

        double sumLn = 0.0;
        double sumMag = 0.0;
        for (std::size_t b = loBin; b <= hiBin; ++b) {
            const double re = static_cast<double>(spectrum[b].real);
            const double im = static_cast<double>(spectrum[b].imag);
            const double mag = std::sqrt((re * re) + (im * im));
            sumLn += std::log(std::max(mag, 1e-30));
            sumMag += mag;
            avgMag[b] += mag;
        }
        perFrame.push_back(std::exp(sumLn / inBand) / std::max(sumMag / inBand, 1e-300));
    }

    M1Result out;
    out.frames = perFrame.size();
    if (out.frames == 0u) {
        return out;
    }
    double sum = 0.0;
    for (const double v : perFrame) {
        sum += v;
    }
    out.flatness = sum / static_cast<double>(out.frames);
    double var = 0.0;
    for (const double v : perFrame) {
        const double d = v - out.flatness;
        var += d * d;
    }
    if (out.frames > 1u) {
        var /= static_cast<double>(out.frames - 1u);
        out.standardError = std::sqrt(var) / std::sqrt(static_cast<double>(out.frames));
    }

    std::vector<double> band;
    band.reserve(static_cast<std::size_t>(inBand));
    for (std::size_t b = loBin; b <= hiBin; ++b) {
        band.push_back(avgMag[b] / static_cast<double>(out.frames));
    }
    const double peak = *std::max_element(band.begin(), band.end());
    std::vector<double> sorted = band;
    const std::size_t mid = sorted.size() / 2u;
    std::nth_element(sorted.begin(), sorted.begin() + static_cast<std::ptrdiff_t>(mid),
                     sorted.end());
    const double median = std::max(sorted[mid], 1e-300);
    out.peakToMedianDb = 20.0 * std::log10(std::max(peak, 1e-300) / median);
    return out;
}

// ------------------------------------------------------------------------------
// The SC-007 render. P-1 (no breath, no tide, no per-line jitter), P-2
// (maxDelaySeconds = 0.5 with getMaxSizeScale() == 4.0f), P-3 (setMix(1) - the
// output IS the wet path) and P-4 (N = 8).
// ------------------------------------------------------------------------------

struct Sc7Render {
    std::vector<float> l;
    std::vector<float> r;
    std::size_t latency = 0;
};

[[nodiscard]] Sc7Render renderSc7(const std::vector<float>& drive, float amount, float width,
                                  float shimmerSend, bool spectralEnabled,
                                  float size = kSc7Size) {
    AetherReverb engine;
    AetherReverb::PrepareConfig cfg;
    cfg.numChannels = 8;  // P-4
    cfg.maxBlockSamples = kSc7Block;
    cfg.maxDelaySeconds = 0.5f;  // P-2
    cfg.shimmerEnabled = (shimmerSend > 0.0f);
    cfg.bloomEnabled = false;
    cfg.spectralDiffusionEnabled = spectralEnabled;
    cfg.diffusionFftSize = kSc7FftSize;
    engine.prepare(kSc16SampleRate, cfg);

    REQUIRE(engine.getMaxSizeScale() == 4.0f);  // P-2

    engine.setSizeBreathDepth(0.0f);  // P-1
    engine.setDimensionalityTideDepth(0.0f);
    engine.setModDepth(0.0f);
    engine.setMix(1.0f);  // P-3
    engine.setSize(size);
    engine.setDensity(kSc7Density);
    engine.setDamping(kSc7Damping);
    engine.setDecaySeconds(kSc7DecaySeconds);
    engine.setShimmerOctaveSend(shimmerSend);
    engine.setShimmerFifthSend(shimmerSend);
    engine.setBloomSend(0.0f);
    engine.setWidth(width);
    engine.setSpectralDiffusion(amount);

    Sc7Render out;
    out.latency = engine.getLatencySamples();
    out.l.assign(kSc7RenderSamples, 0.0f);
    out.r.assign(kSc7RenderSamples, 0.0f);

    std::vector<float> inL(kSc7Block, 0.0f);
    std::vector<float> inR(kSc7Block, 0.0f);
    std::vector<float> outL(kSc7Block, 0.0f);
    std::vector<float> outR(kSc7Block, 0.0f);

    for (std::size_t base = 0; base < kSc7RenderSamples; base += kSc7Block) {
        const std::size_t nb = std::min(kSc7Block, kSc7RenderSamples - base);
        for (std::size_t k = 0; k < nb; ++k) {
            const float v = ((base + k) < drive.size()) ? drive[base + k] : 0.0f;
            inL[k] = v;
            inR[k] = v;
        }
        engine.processStereoBlock(inL.data(), inR.data(), outL.data(), outR.data(), nb);
        for (std::size_t k = 0; k < nb; ++k) {
            out.l[base + k] = outL[k];
            out.r[base + k] = outR[k];
        }
    }
    return out;
}

[[nodiscard]] std::vector<float> sc7Mono(const Sc7Render& r, std::size_t begin,
                                         std::size_t count) {
    std::vector<float> mono(count, 0.0f);
    for (std::size_t i = 0; i < count; ++i) {
        mono[i] = 0.5f * (r.l[begin + i] + r.r[begin + i]);
    }
    return mono;
}

[[nodiscard]] double sc7RmsDb(const Sc7Render& r, std::size_t begin, std::size_t count) {
    double acc = 0.0;
    for (std::size_t i = 0; i < count; ++i) {
        const double a = static_cast<double>(r.l[begin + i]);
        const double b = static_cast<double>(r.r[begin + i]);
        acc += (a * a) + (b * b);
    }
    const double rms = std::sqrt(acc / (2.0 * static_cast<double>(count)));
    return 20.0 * std::log10(std::max(rms, 1e-300));
}

[[nodiscard]] double sc7Correlation(const Sc7Render& r, std::size_t begin, std::size_t count) {
    double sxy = 0.0;
    double sxx = 0.0;
    double syy = 0.0;
    for (std::size_t i = 0; i < count; ++i) {
        const double a = static_cast<double>(r.l[begin + i]);
        const double b = static_cast<double>(r.r[begin + i]);
        sxy += a * b;
        sxx += a * a;
        syy += b * b;
    }
    return sxy / std::sqrt(std::max(sxx * syy, 1e-300));
}

// ------------------------------------------------------------------------------
// Clause 3's controls: the SAME STFT -> OverlapAdd pair the engine uses, driven at
// a caller-chosen hop. At 50 % overlap WITH the synthesis window this is the
// configuration primitives/stft.h:225-228 explicitly forbids, because Hann^2 is
// not COLA at hop = fftSize/2. The engine's own 75 % choice is what makes clause
// 3's two bounds reachable; the pair of controls proves they are not free and
// that the failure is the overlap, not the harness.
// ------------------------------------------------------------------------------
[[nodiscard]] std::vector<float> sc7StftPassthrough(const std::vector<float>& x,
                                                    std::size_t hopSize) {
    Krate::DSP::STFT stft;
    Krate::DSP::OverlapAdd ola;
    Krate::DSP::SpectralBuffer spec;
    stft.prepare(kSc7FftSize, hopSize, Krate::DSP::WindowType::Hann);
    ola.prepare(kSc7FftSize, hopSize, Krate::DSP::WindowType::Hann, 9.0f,
                /*applySynthesisWindow=*/true);
    spec.prepare(kSc7FftSize);

    std::vector<float> produced;
    produced.reserve(x.size() + hopSize);
    std::vector<float> pull(hopSize, 0.0f);
    std::vector<float> out;
    out.reserve(x.size());

    std::size_t consumed = 0;
    std::size_t warmup = kSc7FftSize;
    for (std::size_t base = 0; base < x.size(); base += kSc7Block) {
        const std::size_t nb = std::min(kSc7Block, x.size() - base);
        stft.pushSamples(x.data() + base, nb);
        while (stft.canAnalyze()) {
            stft.analyze(spec);
            ola.synthesize(spec);
            ola.pullSamples(pull.data(), hopSize);
            produced.insert(produced.end(), pull.begin(), pull.end());
        }
        // The engine's own warm-up rule, reproduced exactly: emit fftSize literal
        // zeros FIRST and only then start draining. Draining as soon as anything
        // is available would put a (ceil(fftSize/block) - 1) * block offset on the
        // output instead - 960 samples here - and the control would then be
        // testing the harness's own misalignment rather than the overlap.
        for (std::size_t k = 0; k < nb; ++k) {
            if (warmup > 0u) {
                --warmup;
                out.push_back(0.0f);
            } else if (consumed < produced.size()) {
                out.push_back(produced[consumed]);
                ++consumed;
            } else {
                out.push_back(0.0f);
            }
        }
    }
    return out;
}

struct Sc7Diff {
    double worst = 0.0;
    double rmsDb = -300.0;
};

/// @brief Compare @p delayed against @p ref shifted by @p lag, over [begin, end).
[[nodiscard]] Sc7Diff sc7Compare(const std::vector<float>& delayed, const std::vector<float>& ref,
                                 std::size_t lag, std::size_t begin, std::size_t end) {
    Sc7Diff d;
    double acc = 0.0;
    std::size_t n = 0;
    for (std::size_t m = begin; m < end; ++m) {
        const double e = static_cast<double>(delayed[m]) - static_cast<double>(ref[m - lag]);
        d.worst = std::max(d.worst, std::abs(e));
        acc += e * e;
        ++n;
    }
    if (n > 0u) {
        d.rmsDb = 20.0 * std::log10(std::max(std::sqrt(acc / static_cast<double>(n)), 1e-300));
    }
    return d;
}

/// @brief Median of the four 1/3-octave neighbours {b-2, b-1, b+1, b+2}, in dB.
[[nodiscard]] double sc7NeighbourMedianDb(const std::vector<double>& levels, std::size_t b) {
    std::array<double, 4> v = {levels[b - 2u], levels[b - 1u], levels[b + 1u], levels[b + 2u]};
    std::sort(v.begin(), v.end());
    return 0.5 * (v[1] + v[2]);
}

}  // namespace

// ------------------------------------------------------------------------------
// SC-007 (FR-060 - FR-063), plus FR-052's comb check and FR-080's width sweep.
//
// Six clauses, eight renders. Clause 4(a) reuses the amount-0 render, whose
// damping / density / sends already match the clause's pinned configuration, so
// the always-on cost stays inside B-1.
// ------------------------------------------------------------------------------
TEST_CASE("AetherReverb_TailSmoothness", "[effects][aether]") {
    const std::vector<float> drive = makeSc7Noise(kSc7RenderSamples, 0x5EC70071u);

    // G-5: the SAME generator used DIRECTLY, never through the engine. This is the
    // empirical ceiling M-1 can reach on this input - recorded, not asserted,
    // because an engine that merely matched it would be a bypass.
    const std::vector<float> g5(drive.end() - static_cast<std::ptrdiff_t>(kSc7AnalysisSamples),
                                drive.end());
    const M1Result ceiling = measureM1(g5);
    WARN("SC-007 M-1(G-5) empirical ceiling = " << ceiling.flatness << " (SE "
                                                << ceiling.standardError << ", " << ceiling.frames
                                                << " frames)");

    const std::size_t tailBegin = kSc7RenderSamples - kSc7AnalysisSamples;
    const std::size_t rmsBegin = kSc7RenderSamples - kSc7RmsSamples;

    std::vector<Sc7Render> sweep;
    sweep.reserve(kSc7AmountCount);
    std::vector<M1Result> metric(kSc7AmountCount);
    std::vector<double> correlation(kSc7AmountCount, 0.0);
    std::vector<double> wetRmsDb(kSc7AmountCount, 0.0);

    for (std::size_t i = 0; i < kSc7AmountCount; ++i) {
        sweep.push_back(renderSc7(drive, kSc7Amounts[i], /*width=*/1.0f, /*shimmerSend=*/0.0f,
                                  /*spectralEnabled=*/true));
        REQUIRE(sweep[i].latency == kSc7FftSize);
        metric[i] = measureM1(sc7Mono(sweep[i], tailBegin, kSc7AnalysisSamples));
        correlation[i] = sc7Correlation(sweep[i], tailBegin, kSc7AnalysisSamples);
        wetRmsDb[i] = sc7RmsDb(sweep[i], rmsBegin, kSc7RmsSamples);
        WARN("SC-007 amount=" << kSc7Amounts[i] << " M-1=" << metric[i].flatness
                              << " SE=" << metric[i].standardError
                              << " frames=" << metric[i].frames
                              << " peak/median=" << metric[i].peakToMedianDb
                              << " dB LRcorr=" << correlation[i] << " wetRMS=" << wetRmsDb[i]
                              << " dBFS");
    }

    // --------------------------------------------------------------------------
    // Clause 1 - M-1 rises with the smear amount, the modal ridge falls, and the
    // rise between the endpoints is larger than three combined standard errors.
    // (c) is what makes a stub fail unconditionally: a stage that ignores the
    // amount gives a difference of exactly 0.
    // --------------------------------------------------------------------------
    for (std::size_t i = 1; i < kSc7AmountCount; ++i) {
        INFO("clause 1(a): M-1(" << kSc7Amounts[i] << ")=" << metric[i].flatness
                                 << " must be >= M-1(" << kSc7Amounts[i - 1u]
                                 << ")=" << metric[i - 1u].flatness);
        REQUIRE(metric[i].flatness >= metric[i - 1u].flatness);
    }
    {
        const double drop = metric[0].peakToMedianDb - metric[kSc7AmountCount - 1u].peakToMedianDb;
        INFO("clause 1(b): peak-to-median " << metric[0].peakToMedianDb << " dB -> "
                                            << metric[kSc7AmountCount - 1u].peakToMedianDb
                                            << " dB (drop " << drop << " dB, need >= 3)");
        REQUIRE(drop >= 3.0);
    }
    {
        const double se0 = metric[0].standardError;
        const double se1 = metric[kSc7AmountCount - 1u].standardError;
        const double significance = 3.0 * std::sqrt((se0 * se0) + (se1 * se1));
        const double rise = metric[kSc7AmountCount - 1u].flatness - metric[0].flatness;
        INFO("clause 1(c): M-1 rise " << rise
                                      << " must exceed 3*sqrt(SE0^2+SE1^2)=" << significance);
        REQUIRE(rise >= significance);
    }

    // --------------------------------------------------------------------------
    // Clause 2 - the two smear streams are seeded independently (kSmearSaltL /
    // kSmearSaltR), so more smear means less L/R COHERENCE, never more.
    //
    // Coherence is the MAGNITUDE of the correlation. The amount-0 baseline is
    // an estimation-noise draw over a finite stochastic tail whose SIGN is
    // toolchain luck, and destroying a NEGATIVE baseline coherence moves the
    // signed value UP - so the signed monotone assertion this clause used to
    // make encoded the baseline's sign and broke the leg whose draw came out
    // negative. Measured sweeps of the SAME code (amounts 0/0.25/0.5/0.75/1):
    //   MSVC 19.44            +0.0779 +0.0743 +0.0580 +0.0232 +0.0005
    //   g++ 13 -O2            +0.0750 +0.0723 +0.0580 +0.0252 +0.0030
    //   Apple Clang (Xcode 26.6, -ffast-math, CI log 2026-08-05)
    //                         -0.0310 -0.0298 -0.0220 -0.0042 +0.0050
    // All three sweeps DECREASE in |corr| at every step except Apple's last
    // (+0.00076, far inside the 0.005 allowance). Assert |corr|: the physical
    // claim is independent of which sign the baseline drew.
    // --------------------------------------------------------------------------
    constexpr double kCorrEstimationNoise = 0.005;
    for (std::size_t i = 1; i < kSc7AmountCount; ++i) {
        INFO("clause 2: |LR corr| at amount " << kSc7Amounts[i] << " = "
                                              << std::abs(correlation[i]) << " must be <= "
                                              << std::abs(correlation[i - 1u]) << " + "
                                              << kCorrEstimationNoise);
        REQUIRE(std::abs(correlation[i]) <= std::abs(correlation[i - 1u]) + kCorrEstimationNoise);
    }
    {
        INFO("clause 2 endpoints: |LR corr| at amount " << kSc7Amounts[kSc7AmountCount - 1u]
             << " = " << std::abs(correlation[kSc7AmountCount - 1u])
             << " must be <= |corr| at amount " << kSc7Amounts[0] << " = "
             << std::abs(correlation[0]));
        REQUIRE(std::abs(correlation[kSc7AmountCount - 1u]) <= std::abs(correlation[0]));
    }

    // --------------------------------------------------------------------------
    // Clause 3 - at amount 0 the stage is a pure fftSize-sample delay. Measured
    // against a reference rendered with spectralDiffusionEnabled = false, which
    // has no dry-alignment delay and no STFT at all.
    //
    // The comparison starts at 2*fftSize, not at fftSize: plan S7.11 records that
    // the first fftSize - hop OverlapAdd output samples are under-summed (only
    // frames 0..k exist), which is a documented warm-up, not an alignment error.
    // --------------------------------------------------------------------------
    const Sc7Render bypass = renderSc7(drive, /*amount=*/0.0f, /*width=*/1.0f,
                                       /*shimmerSend=*/0.0f, /*spectralEnabled=*/false);
    REQUIRE(bypass.latency == 0u);
    {
        const std::size_t begin = 2u * kSc7FftSize;
        const Sc7Diff dl = sc7Compare(sweep[0].l, bypass.l, kSc7FftSize, begin, kSc7RenderSamples);
        const Sc7Diff dr = sc7Compare(sweep[0].r, bypass.r, kSc7FftSize, begin, kSc7RenderSamples);
        WARN("SC-007 clause 3: worst |err| L=" << dl.worst << " R=" << dr.worst << ", err RMS L="
                                               << dl.rmsDb << " dBFS R=" << dr.rmsDb << " dBFS");
        INFO("clause 3: the amount-0 wet path must be an EXACT fftSize delay of the "
             "stage-disabled render (worst L="
             << dl.worst << " R=" << dr.worst << ")");
        REQUIRE(dl.worst <= kSc7SampleTolerance);
        REQUIRE(dr.worst <= kSc7SampleTolerance);
        REQUIRE(dl.rmsDb <= kSc7ErrorRmsCeilingDb);
        REQUIRE(dr.rmsDb <= kSc7ErrorRmsCeilingDb);

        // Negative control: the same reference through a 50 %-overlap,
        // synthesis-windowed STFT/OLA pair must BREAK BOTH bounds.
        const std::vector<float> control = sc7StftPassthrough(bypass.l, kSc7Hop50);
        REQUIRE(control.size() == bypass.l.size());
        const Sc7Diff dc = sc7Compare(control, bypass.l, kSc7FftSize, begin, kSc7RenderSamples);
        WARN("SC-007 clause 3 negative control (50 % overlap): worst |err|="
             << dc.worst << ", err RMS=" << dc.rmsDb << " dBFS");
        INFO("clause 3 negative control: a 50 % overlap with the synthesis window applied is "
             "NOT COLA (primitives/stft.h:225-228) and must exceed both bounds");
        REQUIRE(dc.worst > kSc7SampleTolerance);
        REQUIRE(dc.rmsDb > kSc7ErrorRmsCeilingDb);

        // ...and the same pump at the engine's own 75 % overlap must NOT, which is
        // what pins the failure above on the overlap rather than on the harness.
        const std::vector<float> sane = sc7StftPassthrough(bypass.l, kSc7Hop75);
        const Sc7Diff ds = sc7Compare(sane, bypass.l, kSc7FftSize, begin, kSc7RenderSamples);
        WARN("SC-007 clause 3 positive control (75 % overlap): worst |err|="
             << ds.worst << ", err RMS=" << ds.rmsDb << " dBFS");
        REQUIRE(ds.worst <= kSc7SampleTolerance);
    }

    // --------------------------------------------------------------------------
    // Clause 4 - no resonant ridge, and (b) no comb notch with both shimmer taps
    // wide open (FR-052).
    // --------------------------------------------------------------------------
    const std::vector<ThirdOctaveBand> bands = makeThirdOctaveBands();
    REQUIRE(bands.size() >= 15u);
    // Clause 4(a) runs "at every size in SC-003 clause 1's always-on core", i.e.
    // size in {0, 0.5, 1} - a ridge that only appears at one geometry is exactly
    // the failure this clause exists to catch, and a single pinned size cannot
    // see it. sweep[0] IS the size = kSc7Size (0.5) render at amount 0, so only
    // the two remaining sizes need a fresh render.
    for (std::size_t si = 0; si < kSc7ClauseFourSizeCount; ++si) {
        const float sc7Size = kSc7ClauseFourSizes[si];
        const Sc7Render* rendered = nullptr;
        Sc7Render extra;
        if (si == kSc7ClauseFourReusedIndex) {
            rendered = sweep.data();  // already rendered at kSc7Size, amount 0
        } else {
            extra = renderSc7(drive, /*amount=*/0.0f, /*width=*/1.0f, /*shimmerSend=*/0.0f,
                              /*spectralEnabled=*/true, sc7Size);
            rendered = &extra;
        }
        const std::vector<double> levels =
            thirdOctaveLevelsDb(sc7Mono(*rendered, tailBegin, kSc7AnalysisSamples), bands);
        double worstExcess = -1e300;
        double worstCentre = 0.0;
        for (std::size_t b = 2u; (b + 2u) < bands.size(); ++b) {
            const double neighbours = sc7NeighbourMedianDb(levels, b);
            if ((levels[b] - neighbours) > worstExcess) {
                worstExcess = levels[b] - neighbours;
                worstCentre = bands[b].centre;
            }
            INFO("clause 4(a) at size " << sc7Size << ": band " << bands[b].centre << " Hz at "
                                        << levels[b] << " dB, neighbour median " << neighbours
                                        << " dB");
            REQUIRE(levels[b] <= (neighbours + kSc7RidgeDb));
        }
        WARN("SC-007 clause 4(a) at size " << sc7Size << ": worst ridge " << worstExcess
                                           << " dB at " << worstCentre << " Hz (bound "
                                           << kSc7RidgeDb << " dB)");
    }
    {
        const Sc7Render shimmer = renderSc7(drive, /*amount=*/0.0f, /*width=*/1.0f,
                                            /*shimmerSend=*/1.0f, /*spectralEnabled=*/true);
        const std::vector<double> levels =
            thirdOctaveLevelsDb(sc7Mono(shimmer, tailBegin, kSc7AnalysisSamples), bands);
        for (std::size_t b = 2u; (b + 2u) < bands.size(); ++b) {
            const double neighbours = sc7NeighbourMedianDb(levels, b);
            INFO("clause 4(b): FR-052 comb check, band "
                 << bands[b].centre << " Hz at " << levels[b] << " dB, neighbour median "
                 << neighbours << " dB");
            REQUIRE(levels[b] <= (neighbours + kSc7RidgeDb));
            REQUIRE(levels[b] >= (neighbours - kSc7RidgeDb));
        }
    }

    // --------------------------------------------------------------------------
    // Clause 5 - THE g(a) MAKE-UP CHECK. Independently randomised per-frame phases
    // sum incoherently, so OverlapAdd's fixed COLA factor is wrong by a level that
    // grows with the amount (~6 dB at amount 1). Without this clause that loss
    // ships silently.
    // --------------------------------------------------------------------------
    {
        const double lo = *std::min_element(wetRmsDb.begin(), wetRmsDb.end());
        const double hi = *std::max_element(wetRmsDb.begin(), wetRmsDb.end());
        WARN("SC-007 clause 5: wet RMS spread over the five amounts = " << (hi - lo) << " dB");
        INFO("clause 5: g(a) coherence make-up. Wet RMS spread "
             << (hi - lo) << " dB must be <= 1.0 dB");
        REQUIRE((hi - lo) <= 1.0);
    }

    // --------------------------------------------------------------------------
    // Clause 6 - FR-080 / setWidth, swept on clause 3's amount-0 configuration.
    // At width 0 the M/S collapse makes wetL and wetR the SAME mid signal, and at
    // amount 0 the two independent smear streams cannot pull them apart again, so
    // the correlation must be essentially 1. An unwired setWidth ships green
    // without this.
    // --------------------------------------------------------------------------
    {
        const Sc7Render narrow = renderSc7(drive, /*amount=*/0.0f, /*width=*/0.0f,
                                           /*shimmerSend=*/0.0f, /*spectralEnabled=*/true);
        const double corrNarrow = sc7Correlation(narrow, tailBegin, kSc7AnalysisSamples);
        const double corrWide = correlation[0];
        WARN("SC-007 clause 6: LR corr at width 0 = " << corrNarrow << ", at width 1 = "
                                                      << corrWide);
        INFO("clause 6: FR-080. width 0 must collapse to mono (corr " << corrNarrow
                                                                      << " >= 0.999)");
        REQUIRE(corrNarrow >= 0.999);
        INFO("clause 6: width 1 must be strictly less correlated (" << corrWide << " < "
                                                                    << corrNarrow << ")");
        REQUIRE(corrWide < corrNarrow);
    }
}
