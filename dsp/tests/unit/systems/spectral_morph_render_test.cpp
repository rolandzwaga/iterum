// ==============================================================================
// Layer 3: System Tests - Seraphis Phase 3 rendered criteria
// ==============================================================================
// Constitution Principle XII: Test-First Development.
// Tests written BEFORE implementation per specs/seraphis-phase3-spectral-morph/
//
// Reference: specs/seraphis-phase3-spectral-morph/spec.md   (SC-014)
//            specs/seraphis-phase3-spectral-morph/plan.md   (§6, §0.1)
//            specs/seraphis-phase3-spectral-morph/tasks.md  (T019)
//
// Populated by T019 (SC-014), T020 (SC-009) and T021 (SC-001 cl.2, SC-004 m.3-4,
// SC-008 cl.3).
//
// ------------------------------------------------------------------------------
// NAMESPACE HAZARD (plan §0.1 item 4) - READ BEFORE ADDING AN INCLUDE
// ------------------------------------------------------------------------------
// There are TWO `calculateSpectralFlatness` functions in this repo:
//   * Krate::DSP::TestUtils::SignalMetrics::calculateSpectralFlatness
//     (tests/test_helpers/signal_metrics.h:326) - time-domain, caps the FFT at 4096;
//   * Krate::DSP::calculateSpectralFlatness
//     (dsp/include/krate/dsp/primitives/spectral_utils.h:335) - magnitude-domain.
// If this TU ever includes BOTH headers the two overloads are in scope under
// different namespaces and EVERY CALL MUST BE QUALIFIED. T019 and T020 include
// neither.
//
// ------------------------------------------------------------------------------
// This TU deliberately does NOT include `allocation_operator_overrides.h`
// ------------------------------------------------------------------------------
// That header supplies the global operator new/delete replacements and must
// appear in EXACTLY ONE translation unit per binary; `dsp_systems_tests` already
// gets it from `unit/systems/selectable_oscillator_test.cpp:388`. A second
// inclusion here is a duplicate-symbol link error (tasks.md:72-74).
//
// ------------------------------------------------------------------------------
// No bit-exact float goldens over rendered audio (dsp/CLAUDE.md, roadmap line 488)
// ------------------------------------------------------------------------------
// Every pinned render below is compared through `render_fingerprint.h` - four
// aggregate metrics plus 32 spaced sample checkpoints, at the measured
// cross-toolchain tolerances kSampleTolerance = 1.0e-4f / kMetricTolerance = 1.0e-5.
// ==============================================================================

#include <krate/dsp/systems/harmonic_cloud.h>
// T020 (SC-009) drives the cloud FROM the morph engine in the FR-086 shape, so
// this TU is the one place both Layer 3 headers meet. That is a TEST-ONLY
// composition: no production component in this phase includes both (Non-Goals).
#include <krate/dsp/systems/spectral_morph_engine.h>
#include <krate/dsp/processors/spectral_state.h>
#include <krate/dsp/primitives/fft.h>
#include <krate/dsp/core/math_constants.h>
#include <krate/dsp/core/window_functions.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
using Catch::Approx;
using namespace Krate::DSP;

// SC-014 clause 1's reference data: the PRE-amendment render of the same
// 216-cell grid, captured by T001 on commit 8d90d9ba before any edit to
// harmonic_cloud.h / core/random.h. See the provenance block in that header.
#include "harmonic_cloud_pre_amendment_fingerprints.h"

#include "render_fingerprint.h"

// SC-014 clause 3. The detector's threshold is a WITHIN-FRAME
// mean(|dx|) + 5*stddev(|dx|) (`artifact_detection.h:186-193`), so the clause is
// DIFFERENTIAL against a control render - never "0 detections over the render" -
// and it carries a mandatory positive control.
#include "artifact_detection.h"

// SC-014 clause 4 only needs frequencyToBin; this header pulls in fft.h and
// window_functions.h and nothing that would trip the flatness hazard above.
#include <spectral_analysis.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

namespace {

// =============================================================================
// Shared render basis
// =============================================================================

constexpr double kSampleRate = 48000.0;
constexpr float kSampleRateF = 48000.0f;
constexpr float kF0Hz = 110.0f;
constexpr std::size_t kRenderBlock = 512;

/// The T001 capture seed and render length (fingerprints header, "PER-CELL
/// RENDER RECIPE"). 96000 samples is 2.0 s at 48 kHz and is NOT a whole number
/// of 512-sample blocks - 187 full blocks plus a 256-sample remainder, which the
/// capture rendered as a short final block. `renderLeft` reproduces that shape.
constexpr std::uint32_t kGridSeed = 1u;
constexpr std::size_t kGridRenderSamples = 96000;

/// The T001 grid axes. The nesting order below IS the array index order of
/// `kPreAmendmentFingerprints`: driftDepthCents varies fastest, richness slowest.
constexpr std::array<float, 3> kRichnessAxis{0.0f, 0.5f, 1.0f};
constexpr std::array<float, 3> kGravityAxis{-1.0f, 0.0f, 1.0f};
constexpr std::array<float, 2> kInharmonicityAxis{0.0f, 0.05f};
constexpr std::array<float, 3> kTiltAxis{-12.0f, 0.0f, 12.0f};
constexpr std::array<float, 2> kMutationAxis{0.0f, 1.0f};
constexpr std::array<float, 2> kDriftAxis{0.0f, HarmonicCloud::kMaxDriftCents};

struct CloudConfig {
    float richness = 1.0f;
    float gravity = 0.0f;
    float inharmonicity = 0.0f;
    float tiltDb = 0.0f;
    float mutation = 0.0f;
    float driftCents = 0.0f;
};

/// @brief Apply the T001 setter sequence, IN THE CAPTURED ORDER.
///
/// The order is load-bearing for clause 1: `setSeed` runs BEFORE `prepare`, and
/// the five macros run in the order richness -> gravity -> inharmonicity -> tilt
/// -> mutation -> drift. Every setter early-returns on an unchanged value and
/// several of them reseed or raise dirty flags, so a reordering is not a no-op.
void configureCloud(HarmonicCloud& cloud, double sampleRate, const CloudConfig& cfg) noexcept {
    cloud.setSeed(kGridSeed);
    cloud.prepare(sampleRate);
    cloud.setFundamentalHz(kF0Hz);
    cloud.setRichness(cfg.richness);
    cloud.setSpectralGravity(cfg.gravity);
    cloud.setInharmonicity(cfg.inharmonicity);
    cloud.setSpectralTiltDb(cfg.tiltDb);
    cloud.setMutation(cfg.mutation);
    cloud.setDriftDepthCents(cfg.driftCents);
}

/// @brief Render `numSamples` in 512-sample blocks and return the LEFT channel.
///
/// NOTE LIFECYCLE (tasks.md:85-88): the caller must have called `noteOn()` and
/// must never call `noteOff()`. A freshly prepared HarmonicCloud has
/// `gate_ = false`, so `isQuiescent()` is true (`harmonic_cloud.h:844-853`) and
/// `processStereoBlock` takes the zero-fill early-out (`:848-855`) - without the
/// note every rendered criterion here would measure silence.
[[nodiscard]] std::vector<float> renderLeft(HarmonicCloud& cloud, std::size_t numSamples) {
    std::vector<float> left(numSamples, 0.0f);
    std::array<float, kRenderBlock> blockL{};
    std::array<float, kRenderBlock> blockR{};
    for (std::size_t done = 0; done < numSamples; done += kRenderBlock) {
        const std::size_t n = std::min(kRenderBlock, numSamples - done);
        cloud.processStereoBlock(blockL.data(), blockR.data(), n);
        std::copy_n(blockL.data(), n, left.data() + done);
    }
    return left;
}

// =============================================================================
// Clause 2 - the identity spectral target
// =============================================================================

/// `ratios[i] = i + 1`: exactly the parametric grid, so the FR-082 identity guard
/// (`harmonic_cloud.h:1254`, `targetRatio_[i] != n`) must take the UNMODIFIED
/// parametric arm - including its own `gravityIsZero` branch.
[[nodiscard]] std::array<float, HarmonicCloud::kMaxPartials> identityTargetRatios() noexcept {
    std::array<float, HarmonicCloud::kMaxPartials> ratios{};
    for (std::size_t i = 0; i < ratios.size(); ++i) {
        ratios[i] = static_cast<float>(i + 1);
    }
    return ratios;
}

/// `amplitudes[i] = exp2(-p(r) * log2(i + 1))` with `p(r)` Richness's rolloff
/// exponent (`harmonic_cloud.h:196-197`, `:1330-1331`):
/// `p(r) = kRichnessMinExponent + (kRichnessMaxExponent - kRichnessMinExponent)*r`
/// = `3.0 - 2.5*r`.
///
/// The exponential is evaluated through the SHARED `detail::kHarmonicCloudLog2N`
/// table (`harmonic_cloud.h:56`) using the identical expression
/// `recalculateAmplitudes` uses at `:1357`. That is deliberate: with a target
/// active the amplitude law becomes `targetAmp_[i] * tiltGain(i)` (`:1356`),
/// so supplying the same bits the parametric arm would have computed makes the
/// two renders identical rather than merely close - and the fingerprint
/// tolerance is then headroom, not the thing being relied on.
[[nodiscard]] std::array<float, HarmonicCloud::kMaxPartials> identityTargetAmplitudes(
    float richness) noexcept {
    const float exponent =
        HarmonicCloud::kRichnessMinExponent
        + (HarmonicCloud::kRichnessMaxExponent - HarmonicCloud::kRichnessMinExponent) * richness;
    std::array<float, HarmonicCloud::kMaxPartials> amplitudes{};
    for (std::size_t i = 0; i < amplitudes.size(); ++i) {
        amplitudes[i] = std::exp2(-exponent * Krate::DSP::detail::kHarmonicCloudLog2N[i]);
    }
    return amplitudes;
}

// =============================================================================
// Clause 3 - the differential click detector (shared with T021)
// =============================================================================

/// The detector's own merge gap (`artifact_detection.h:44`), needed by the
/// index windows below.
constexpr std::size_t kClickMergeGap = 5;

/// THE PINNED DETECTOR CONFIGURATION (tasks.md:1669-1673). The struct default
/// `sampleRate` is 44100 (`artifact_detection.h:39`) and MUST be overridden - a
/// mismatch silently mis-scales `timeSeconds` on every detection. Designated
/// initialisers throughout (Clang rejects narrowing in brace initialisation) and
/// the field order is the declaration order at `artifact_detection.h:38-45`.
[[nodiscard]] std::vector<TestUtils::ClickDetection> detectClicks(
    const std::vector<float>& buffer) {
    TestUtils::ClickDetectorConfig cfg{.sampleRate = kSampleRateF,
                                       .frameSize = 512,
                                       .hopSize = 256,
                                       .detectionThreshold = 5.0f,
                                       .energyThresholdDb = -60.0f,
                                       .mergeGap = kClickMergeGap};
    TestUtils::ClickDetector detector(cfg);
    detector.prepare();
    return detector.detect(buffer.data(), buffer.size());
}

[[nodiscard]] std::size_t countDetectionsNear(
    const std::vector<TestUtils::ClickDetection>& detections, std::size_t center,
    std::size_t halfWidth) noexcept {
    const std::size_t lo = (center > halfWidth) ? (center - halfWidth) : 0;
    const std::size_t hi = center + halfWidth;
    std::size_t count = 0;
    for (const TestUtils::ClickDetection& detection : detections) {
        if (detection.sampleIndex >= lo && detection.sampleIndex <= hi) {
            ++count;
        }
    }
    return count;
}

/// 4.0 s at 48 kHz, exactly 375 blocks of 512.
constexpr std::size_t kClearRenderSamples = 192000;
constexpr std::size_t kClearBlockIndex = 192;
constexpr std::size_t kClearSampleIndex = kClearBlockIndex * kRenderBlock;  // 98304 = 2.048 s

/// A step at sample `k` raises |dx| at `k` and `k + 1`, and the surviving merged
/// detection can sit either side, so the TIGHT window is a few merge gaps wide.
constexpr std::size_t kTightHalfWidth = 4 * kClickMergeGap;  // +-20 samples

/// The BROAD window is +-8 detector frames, i.e. every frame whose 512-sample
/// span can contain the event at hop 256.
constexpr std::size_t kBroadHalfWidth = 2048;

/// Broad-window allowance, DERIVED, not tuned. Phase 2 measured this detector's
/// false-detection floor on a click-free 30 s / 48 kHz HarmonicCloud render at
/// 126 (L) and 141 (R) detections, i.e. ~8.75e-5 detections per sample per
/// channel. Over the 4097-sample broad window on two channels the expected count
/// is ~0.72, so a Poisson excess of 5 above the control has probability < 5e-4.
/// A real click is caught by the TIGHT window, which admits none - this number
/// exists only so ordinary detector noise cannot red a click-free build.
/// If clause 3 ever fails, re-measure; do NOT raise this.
constexpr std::size_t kBroadWindowAllowance = 5;

/// Positive-control step height. The rendered signal's own mean |dx| at this
/// configuration is ~0.095 (sum over 64 partials of (2*pi*f_i/fs)^2 * a_i^2 is
/// ~9.1e-3 because the SineStack n^-1 rolloff exactly cancels the n frequency
/// growth), so a 1.0 step is ~10x the mean and ~5x above the 5-sigma threshold.
constexpr float kInjectedStepAmplitude = 1.0f;

struct StereoRender {
    std::vector<float> left;
    std::vector<float> right;
};

/// @brief Render the clause-3 basis, optionally clearing the target mid-render.
/// @param doClear `true` calls `clearSpectralTarget()` at `kClearSampleIndex`;
///        `false` re-supplies the SAME target instead, which is a no-op through
///        the FR-085 per-slot epsilon compare (`harmonic_cloud.h:789-795`) and
///        gives the control render an identical call sequence.
[[nodiscard]] StereoRender renderWithTargetEvent(
    bool doClear, const std::array<float, HarmonicCloud::kMaxPartials>& ratios,
    const std::array<float, HarmonicCloud::kMaxPartials>& amplitudes) {
    HarmonicCloud cloud;
    configureCloud(cloud, kSampleRate, CloudConfig{.richness = 1.0f,
                                                   .gravity = 0.0f,
                                                   .inharmonicity = 0.0f,
                                                   .tiltDb = 0.0f,
                                                   .mutation = 0.0f,
                                                   .driftCents = 0.0f});
    cloud.setStereoSpread(0.0f);
    cloud.setAttackTimeSec(HarmonicCloud::kMinAttackSec);
    cloud.setEnvelopeOffsetSpread(0.0f);
    cloud.setSpectralTarget(ratios.data(), amplitudes.data(), HarmonicCloud::kMaxPartials);
    cloud.noteOn();  // never noteOff()

    StereoRender out{std::vector<float>(kClearRenderSamples, 0.0f),
                     std::vector<float>(kClearRenderSamples, 0.0f)};
    std::array<float, kRenderBlock> blockL{};
    std::array<float, kRenderBlock> blockR{};
    const std::size_t numBlocks = kClearRenderSamples / kRenderBlock;
    for (std::size_t b = 0; b < numBlocks; ++b) {
        if (b == kClearBlockIndex) {
            if (doClear) {
                cloud.clearSpectralTarget();
            } else {
                cloud.setSpectralTarget(ratios.data(), amplitudes.data(),
                                        HarmonicCloud::kMaxPartials);
            }
        }
        cloud.processStereoBlock(blockL.data(), blockR.data(), kRenderBlock);
        const std::size_t offset = b * kRenderBlock;
        std::copy_n(blockL.data(), kRenderBlock, out.left.data() + offset);
        std::copy_n(blockR.data(), kRenderBlock, out.right.data() + offset);
    }
    return out;
}

// =============================================================================
// Clause 4 - the reset() / prepare() recompute path
// =============================================================================

/// Non-identity target: SineStack's ratios scaled by 1.5, so every partial sits
/// off the parametric grid and the FR-082 identity guard cannot mask a stale
/// recompute.
constexpr float kTargetRatioScale = 1.5f;

constexpr double kClause4RateBefore = 48000.0;
constexpr double kClause4RateAfter = 96000.0;
constexpr float kClause4RateAfterF = 96000.0f;

/// 0.512 s at 48 kHz - enough that the cloud is genuinely sounding (and its
/// smoothers settled) when `prepare()` re-enters `reset()`.
constexpr std::size_t kClause4PreSamples = 24576;

/// 1.536 s at 96 kHz, exactly 288 blocks of 512. The last 65536 samples are
/// analysed, so the window starts 0.853 s after the note - far past the 0.05 s
/// minimum attack and the 20 ms kNormGainSmoothMs settle.
constexpr std::size_t kClause4RenderSamples = 147456;

/// 65536-point analysis at 96 kHz: 1.4648 Hz per bin. The target partial spacing
/// is 110 * 1.5 = 165 Hz = 112.6 bins, so the Blackman-Harris main lobes (half
/// width 4 bins) never overlap and a +-8 bin peak search cannot stray onto a
/// neighbour.
constexpr std::size_t kFftSize = 65536;
constexpr std::size_t kPeakSearchHalfWidth = 8;

/// SC-014 clause 4's bound: every partial's RENDERED frequency within 0.1 % of
/// `f0 * targetRatio[i]`. At partial 1 (165 Hz) that is 0.165 Hz = 0.113 bins,
/// which is why the measurement below interpolates rather than reading the peak
/// bin: quadratic interpolation of the LOG magnitude of a Blackman-Harris-
/// windowed tone is accurate to far better than 0.01 bin (the window's main lobe
/// is near-Gaussian, which is exactly the condition that makes a log-parabola
/// fit exact).
constexpr double kFrequencyToleranceRel = 0.001;

/// Presence guard, relative to the strongest measured partial. The SineStack
/// amplitude law is n^-1 L2-normalised, so partial 64 sits ~36 dB below partial 1
/// before the +-2.5 dB MCF orbit-gain spread - comfortably above this floor.
/// Under the failing implementation (a `reset()` that CLEARS the per-slot masks
/// instead of setting them) every partial renders at HALF pitch, which leaves the
/// expected bins of partials 33..64 completely empty: the nearest real partial is
/// >100 bins away and Blackman-Harris sidelobes are -92 dB, so this guard is what
/// turns that failure into a loud one.
constexpr float kPresenceFloorDb = -60.0f;

struct PartialMeasurement {
    double frequencyHz = 0.0;
    float levelDb = 0.0f;
};

/// @brief Measure every partial's rendered frequency from one 65536-point transform.
[[nodiscard]] std::array<PartialMeasurement, HarmonicCloud::kMaxPartials> measurePartials(
    const std::vector<float>& mono, float sampleRate,
    const std::array<float, HarmonicCloud::kMaxPartials>& expectedHz) {
    REQUIRE(mono.size() >= kFftSize);

    std::vector<float> window(kFftSize, 0.0f);
    Window::generateBlackmanHarris(window.data(), kFftSize);

    const std::size_t analysisStart = mono.size() - kFftSize;
    std::vector<float> frame(kFftSize, 0.0f);
    for (std::size_t i = 0; i < kFftSize; ++i) {
        frame[i] = mono[analysisStart + i] * window[i];
    }

    FFT fft;
    fft.prepare(kFftSize);
    // Fail loudly rather than analyse a zero-size spectrum: `kMaxFFTSize = 8192`
    // (`fft.h:47`) is documentary and pffft handles 2^16, but a future tightening
    // of that bound must red here instead of silently measuring nothing.
    REQUIRE(fft.isPrepared());

    std::vector<Complex> spectrum(fft.numBins());
    fft.forward(frame.data(), spectrum.data());

    const std::size_t lastBin = fft.numBins() - 1;
    std::array<float, HarmonicCloud::kMaxPartials> peakMagnitude{};
    std::array<double, HarmonicCloud::kMaxPartials> peakFrequency{};

    for (std::size_t i = 0; i < HarmonicCloud::kMaxPartials; ++i) {
        const std::size_t center = TestUtils::frequencyToBin(expectedHz[i], sampleRate, kFftSize);
        const std::size_t lo =
            (center > kPeakSearchHalfWidth) ? (center - kPeakSearchHalfWidth) : 1;
        const std::size_t hi = std::min(center + kPeakSearchHalfWidth, lastBin - 1);
        REQUIRE(lo <= hi);

        std::size_t bestBin = lo;
        float bestMag = spectrum[lo].magnitude();
        for (std::size_t b = lo + 1; b <= hi; ++b) {
            const float mag = spectrum[b].magnitude();
            if (mag > bestMag) {
                bestMag = mag;
                bestBin = b;
            }
        }

        // Quadratic interpolation of the log magnitude around the peak bin.
        constexpr float kTinyMagnitude = 1.0e-30f;
        const float logLo = std::log(std::max(spectrum[bestBin - 1].magnitude(), kTinyMagnitude));
        const float logMid = std::log(std::max(spectrum[bestBin].magnitude(), kTinyMagnitude));
        const float logHi = std::log(std::max(spectrum[bestBin + 1].magnitude(), kTinyMagnitude));
        const float denominator = logLo - 2.0f * logMid + logHi;
        const float delta =
            (denominator != 0.0f) ? (0.5f * (logLo - logHi) / denominator) : 0.0f;

        peakMagnitude[i] = bestMag;
        peakFrequency[i] = (static_cast<double>(bestBin) + static_cast<double>(delta))
                           * static_cast<double>(sampleRate) / static_cast<double>(kFftSize);
    }

    float strongest = 0.0f;
    for (std::size_t i = 0; i < HarmonicCloud::kMaxPartials; ++i) {
        strongest = std::max(strongest, peakMagnitude[i]);
    }
    REQUIRE(strongest > 0.0f);

    std::array<PartialMeasurement, HarmonicCloud::kMaxPartials> out{};
    for (std::size_t i = 0; i < HarmonicCloud::kMaxPartials; ++i) {
        out[i].frequencyHz = peakFrequency[i];
        out[i].levelDb = (peakMagnitude[i] > 0.0f)
                             ? 20.0f * std::log10(peakMagnitude[i] / strongest)
                             : kPresenceFloorDb - 100.0f;
    }
    return out;
}

}  // namespace

// =============================================================================
// SC-014 — the spectral target is neutral when it is the identity (T019)
// =============================================================================
TEST_CASE("HarmonicCloud_SpectralTargetIsNeutralWhenIdentity", "[spectral_morph][seraphis]") {
    // -------------------------------------------------------------------------
    // CLAUSE 1 — THE PHASE 2 REGRESSION GATE
    // -------------------------------------------------------------------------
    // `setSpectralTarget` is NEVER called here. The Phase 3 amendment to
    // `harmonic_cloud.h` is strictly additive and every branch it adds is guarded
    // by `hasTarget_`, so an untargeted cloud must render EXACTLY what Phase 2
    // shipped. The reference is the pre-amendment capture from T001.
    //
    // The other half of this clause is not expressible as an assertion: the
    // existing `harmonic_cloud_test.cpp` and `harmonic_cloud_spectral_test.cpp`
    // suites must pass UNEDITED. An edit to either is a failure of clause 1.
    SECTION("clause 1: the untargeted 216-cell grid still matches the pre-amendment render") {
        std::size_t index = 0;
        for (const float richness : kRichnessAxis) {
            for (const float gravity : kGravityAxis) {
                for (const float inharmonicity : kInharmonicityAxis) {
                    for (const float tiltDb : kTiltAxis) {
                        for (const float mutation : kMutationAxis) {
                            for (const float driftCents : kDriftAxis) {
                                HarmonicCloud cloud;
                                configureCloud(cloud, kSampleRate,
                                               CloudConfig{.richness = richness,
                                                           .gravity = gravity,
                                                           .inharmonicity = inharmonicity,
                                                           .tiltDb = tiltDb,
                                                           .mutation = mutation,
                                                           .driftCents = driftCents});
                                // The gate itself: no target was ever supplied.
                                REQUIRE_FALSE(cloud.hasSpectralTarget());
                                cloud.noteOn();  // never noteOff()

                                const std::vector<float> left =
                                    renderLeft(cloud, kGridRenderSamples);
                                const TestUtils::RenderFingerprint actual =
                                    TestUtils::fingerprintRender(std::span<const float>(left));
                                // The references are STORED fingerprints of a
                                // trajectory-accumulating render (drift,
                                // mutation, tilt cells), so both the checkpoint
                                // samples and the aggregate metrics move under
                                // a legal codegen change - the trajectories
                                // themselves diverge. MEASURED over the FULL
                                // 216-cell grid, 2026-08-06 (Catch2 -s sweep):
                                //   MSVC /fp:fast (the pinning toolchain):
                                //     worst metric 2.27e-7, worst sample 1.79e-7
                                //   g++ 13 -ffast-math + guard barrier:
                                //     worst metric 5.41e-4, worst sample 1.32e-3
                                //     (worst cells: tilt=12 / drift=50)
                                // Bounds ~2.5x the cross-toolchain worst. The
                                // injected stale-sweep-cache defect measures
                                // 0.38 sample error - still 100x outside.
                                constexpr double kStoredGoldenMetricTol = 1.5e-3;
                                constexpr float kStoredGoldenSampleTol = 3.5e-3f;
                                const TestUtils::FingerprintComparison comparison =
                                    TestUtils::compareFingerprints(
                                        actual,
                                        SeraphisPhase3TestData::kPreAmendmentFingerprints[index],
                                        kStoredGoldenMetricTol, kStoredGoldenSampleTol);

                                INFO("grid cell " << index << ": r=" << richness
                                                  << " g=" << gravity << " B=" << inharmonicity
                                                  << " tilt=" << tiltDb << " mut=" << mutation
                                                  << " drift=" << driftCents);
                                INFO("worst metric relative error "
                                     << comparison.worstMetricRelativeError << ", worst sample error "
                                     << comparison.worstSampleError << " (" << comparison.detail
                                     << ")");
                                REQUIRE(comparison.withinTolerance());
                                ++index;
                            }
                        }
                    }
                }
            }
        }
        REQUIRE(index == SeraphisPhase3TestData::kPreAmendmentFingerprints.size());
    }

    // -------------------------------------------------------------------------
    // CLAUSE 2 — IDENTITY TARGET
    // -------------------------------------------------------------------------
    // A target that reproduces the parametric laws exactly must render exactly
    // the parametric render. This is what pins the FR-082 ratio identity guard
    // (`harmonic_cloud.h:1254`, and specifically its fall-back to the
    // `gravityIsZero ? n : exp2(...)` arm rather than to the exp2 arm alone) and
    // the FR-083 amplitude substitution (`:1356`).
    //
    // 3 Richness x {gravity 0, +-1} x {B 0, 0.05} x {tilt 0, +-12} = 54 cells.
    // Mutation and drift are pinned at 0: they perturb the amplitude and the
    // detune every chunk and would leave nothing but the fingerprint tolerance
    // between the two renders.
    SECTION("clause 2: an identity target renders exactly the parametric render") {
        std::size_t cells = 0;
        for (const float richness : kRichnessAxis) {
            for (const float gravity : kGravityAxis) {
                for (const float inharmonicity : kInharmonicityAxis) {
                    for (const float tiltDb : kTiltAxis) {
                        const CloudConfig cfg{.richness = richness,
                                              .gravity = gravity,
                                              .inharmonicity = inharmonicity,
                                              .tiltDb = tiltDb,
                                              .mutation = 0.0f,
                                              .driftCents = 0.0f};

                        HarmonicCloud parametric;
                        configureCloud(parametric, kSampleRate, cfg);
                        REQUIRE_FALSE(parametric.hasSpectralTarget());
                        parametric.noteOn();
                        const std::vector<float> parametricLeft =
                            renderLeft(parametric, kGridRenderSamples);
                        const TestUtils::RenderFingerprint reference =
                            TestUtils::fingerprintRender(std::span<const float>(parametricLeft));

                        const std::array<float, HarmonicCloud::kMaxPartials> ratios =
                            identityTargetRatios();
                        const std::array<float, HarmonicCloud::kMaxPartials> amplitudes =
                            identityTargetAmplitudes(richness);

                        HarmonicCloud targeted;
                        configureCloud(targeted, kSampleRate, cfg);
                        targeted.setSpectralTarget(ratios.data(), amplitudes.data(),
                                                   HarmonicCloud::kMaxPartials);
                        REQUIRE(targeted.hasSpectralTarget());
                        targeted.noteOn();
                        const std::vector<float> targetedLeft =
                            renderLeft(targeted, kGridRenderSamples);
                        const TestUtils::RenderFingerprint actual =
                            TestUtils::fingerprintRender(std::span<const float>(targetedLeft));

                        const TestUtils::FingerprintComparison comparison =
                            TestUtils::compareFingerprints(actual, reference);
                        INFO("identity cell r=" << richness << " g=" << gravity
                                                << " B=" << inharmonicity << " tilt=" << tiltDb);
                        INFO("worst metric relative error "
                             << comparison.worstMetricRelativeError << ", worst sample error "
                             << comparison.worstSampleError << " (" << comparison.detail << ")");
                        REQUIRE(comparison.withinTolerance());
                        ++cells;
                    }
                }
            }
        }
        REQUIRE(cells == kRichnessAxis.size() * kGravityAxis.size() * kInharmonicityAxis.size()
                             * kTiltAxis.size());
    }

    // -------------------------------------------------------------------------
    // CLAUSE 3 — NO CLICK ON CLEAR
    // -------------------------------------------------------------------------
    // `clearSpectralTarget()` goes through the same dirty-flag path and the same
    // FR-014 amplitude smoother as every other configuration change, and it never
    // touches `sinState_`/`cosState_`, so it must be click-free. The transition
    // is a real one: ratios move from 1.5n back to the n grid and amplitudes from
    // the SineStack n^-1 shape to Richness's n^-0.5 rolloff.
    SECTION("clause 3: clearing a live target mid-render does not click") {
        const SpectralState sineStack = makeFactoryState(SpectralStateId::SineStack);
        constexpr int kFullPartialCount = static_cast<int>(HarmonicCloud::kMaxPartials);
        REQUIRE(sineStack.numPartials == kFullPartialCount);

        std::array<float, HarmonicCloud::kMaxPartials> ratios{};
        std::array<float, HarmonicCloud::kMaxPartials> amplitudes{};
        for (std::size_t i = 0; i < HarmonicCloud::kMaxPartials; ++i) {
            ratios[i] = kTargetRatioScale * sineStack.ratios[i];
            amplitudes[i] = sineStack.amplitudes[i];
        }

        const StereoRender cleared = renderWithTargetEvent(true, ratios, amplitudes);
        const StereoRender control = renderWithTargetEvent(false, ratios, amplitudes);

        const std::vector<TestUtils::ClickDetection> clearedLeft = detectClicks(cleared.left);
        const std::vector<TestUtils::ClickDetection> clearedRight = detectClicks(cleared.right);
        const std::vector<TestUtils::ClickDetection> controlLeft = detectClicks(control.left);
        const std::vector<TestUtils::ClickDetection> controlRight = detectClicks(control.right);

        // MANDATORY POSITIVE CONTROL, AND IT COMES FIRST. Without it this clause
        // cannot distinguish "no artifact" from "detector not wired up": a
        // `detect()` returning an empty vector unconditionally would satisfy every
        // assertion below.
        std::vector<float> injected = cleared.left;
        for (std::size_t i = kClearSampleIndex; i < injected.size(); ++i) {
            injected[i] += kInjectedStepAmplitude;
        }
        const std::size_t injectedNear =
            countDetectionsNear(detectClicks(injected), kClearSampleIndex, kTightHalfWidth);
        INFO("positive control detections at the event index: " << injectedNear);
        REQUIRE(injectedNear > 0);

        // Tight window: a click lands ON the event. The control render performs
        // the identical call sequence with a no-op re-supply in place of the
        // clear, so this is differential and not an absolute-zero bar.
        const std::size_t clearedTight =
            countDetectionsNear(clearedLeft, kClearSampleIndex, kTightHalfWidth)
            + countDetectionsNear(clearedRight, kClearSampleIndex, kTightHalfWidth);
        const std::size_t controlTight =
            countDetectionsNear(controlLeft, kClearSampleIndex, kTightHalfWidth)
            + countDetectionsNear(controlRight, kClearSampleIndex, kTightHalfWidth);
        INFO("tight window (+-" << kTightHalfWidth << "): cleared " << clearedTight << ", control "
                                << controlTight);
        REQUIRE(clearedTight <= controlTight);

        // Broad window: nothing smeared across the surrounding frames either.
        const std::size_t clearedBroad =
            countDetectionsNear(clearedLeft, kClearSampleIndex, kBroadHalfWidth)
            + countDetectionsNear(clearedRight, kClearSampleIndex, kBroadHalfWidth);
        const std::size_t controlBroad =
            countDetectionsNear(controlLeft, kClearSampleIndex, kBroadHalfWidth)
            + countDetectionsNear(controlRight, kClearSampleIndex, kBroadHalfWidth);
        INFO("broad window (+-" << kBroadHalfWidth << "): cleared " << clearedBroad << ", control "
                                << controlBroad << ", allowance " << kBroadWindowAllowance);
        REQUIRE(clearedBroad <= controlBroad + kBroadWindowAllowance);
    }

    // -------------------------------------------------------------------------
    // CLAUSE 4 — THE reset() / prepare() RECOMPUTE PATH
    // -------------------------------------------------------------------------
    // `prepare()` recomputes `nyquist_`/`invSampleRate_` and THEN calls `reset()`
    // (`harmonic_cloud.h:277-305`, `reset()` at `:303`), whose
    // `recalculateFrequencies()`/`recalculateAmplitudes()` at `:346-347` are
    // called directly and unconditionally. With a spectral target active those
    // two loops consult the FR-085 per-slot masks, so `reset()` marks everything
    // dirty first (`:343-344`). Under an amended `reset()` that CLEARS the masks
    // instead of setting them, every `epsilon_[i]` keeps the value derived from
    // the OLD sample rate and every partial renders at HALF pitch after a
    // 48 kHz -> 96 kHz change. THIS CLAUSE IS WHAT FAILS THERE.
    SECTION("clause 4: a sample-rate change recomputes every targeted partial") {
        const SpectralState sineStack = makeFactoryState(SpectralStateId::SineStack);
        constexpr int kFullPartialCount = static_cast<int>(HarmonicCloud::kMaxPartials);
        REQUIRE(sineStack.numPartials == kFullPartialCount);

        std::array<float, HarmonicCloud::kMaxPartials> ratios{};
        std::array<float, HarmonicCloud::kMaxPartials> amplitudes{};
        std::array<float, HarmonicCloud::kMaxPartials> expectedHz{};
        for (std::size_t i = 0; i < HarmonicCloud::kMaxPartials; ++i) {
            ratios[i] = kTargetRatioScale * sineStack.ratios[i];
            amplitudes[i] = sineStack.amplitudes[i];
            expectedHz[i] = kF0Hz * ratios[i];
        }
        // Precondition for the whole bin-mapping strategy: the highest partial is
        // 110 * 1.5 * 64 = 10560 Hz, far below both the 48 kHz Nyquist of the new
        // rate and the FR-015 fade start at 0.8 * Nyquist, so no partial is
        // anti-alias faded out of the measurement.
        REQUIRE(expectedHz[HarmonicCloud::kMaxPartials - 1]
                < HarmonicCloud::kAntiAliasFadeStart * 0.5f * kClause4RateAfterF);

        HarmonicCloud cloud;
        configureCloud(cloud, kClause4RateBefore,
                       CloudConfig{.richness = 1.0f,
                                   .gravity = 0.0f,
                                   .inharmonicity = 0.0f,
                                   .tiltDb = 0.0f,
                                   .mutation = 0.0f,
                                   .driftCents = 0.0f});
        cloud.setStereoSpread(0.0f);
        cloud.setAttackTimeSec(HarmonicCloud::kMinAttackSec);
        cloud.setEnvelopeOffsetSpread(0.0f);
        cloud.setSpectralTarget(ratios.data(), amplitudes.data(), HarmonicCloud::kMaxPartials);
        REQUIRE(cloud.hasSpectralTarget());
        cloud.noteOn();  // never noteOff()

        // Render at the OLD rate so the masks are consumed and every slot is
        // clean when prepare() re-enters reset().
        const std::vector<float> before = renderLeft(cloud, kClause4PreSamples);
        REQUIRE(before.size() == kClause4PreSamples);

        cloud.prepare(kClause4RateAfter);
        // prepare()/reset() must NOT drop the target - it is configuration, not
        // per-partial state.
        REQUIRE(cloud.hasSpectralTarget());
        // reset() sets gate_ = false, so the note has to be re-armed or the
        // render below is the quiescent zero-fill.
        cloud.noteOn();

        const std::vector<float> after = renderLeft(cloud, kClause4RenderSamples);

        const std::array<PartialMeasurement, HarmonicCloud::kMaxPartials> measured =
            measurePartials(after, kClause4RateAfterF, expectedHz);

        for (std::size_t i = 0; i < HarmonicCloud::kMaxPartials; ++i) {
            INFO("partial " << (i + 1) << ": expected " << expectedHz[i] << " Hz, measured "
                            << measured[i].frequencyHz << " Hz, level " << measured[i].levelDb
                            << " dB rel");

            // The bookkeeping accessor agrees by construction in BOTH the working
            // and the broken implementation - `frequencyHz_` is in Hz and does not
            // depend on the sample rate, while `epsilon_` (what actually renders)
            // does. It is asserted as a precondition, and it is precisely why the
            // rendered-frequency measurement below is mandatory and not redundant.
            REQUIRE(cloud.getPartialFrequencyHz(i) == Approx(expectedHz[i]).epsilon(1.0e-5));

            REQUIRE(measured[i].levelDb >= kPresenceFloorDb);

            const double relativeError = std::abs(measured[i].frequencyHz
                                                  - static_cast<double>(expectedHz[i]))
                                         / static_cast<double>(expectedHz[i]);
            REQUIRE(relativeError <= kFrequencyToleranceRel);
        }
    }
}

// =============================================================================
// SC-009 — the composed engine -> cloud render rig (T020)
// =============================================================================
//
// Everything below renders the SHIPPED composition: SpectralMorphEngine drives
// HarmonicCloud through the FR-080 injection surface in the FR-086 shape --
// <= 64-sample slices, `engine.updateChunk(n)` -> `cloud.setSpectralTarget(
// engine.getOutputRatios(), engine.getOutputAmplitudes(), engine.getOutputCount())`
// -> `cloud.processStereoBlock(..., n)` per slice, zero-copy through the FR-008
// accessors. The drive shape is PART OF THE CRITERION (spec.md SC-009), not a
// convenience of the harness: a target supplied once per host block would freeze
// the morph at the host block size and the rendered evidence would be about a
// coarser path than the one that ships.
// =============================================================================

namespace {

/// FR-086's bound, and this criterion's cadence.
constexpr std::size_t kSliceSamples = HarmonicCloud::kControlChunkSamples;  // 64
static_assert(kSliceSamples == 64);

// -----------------------------------------------------------------------------
// The pinned render shape (spec.md SC-009, plan §9.2, tasks.md T020)
// -----------------------------------------------------------------------------
// setTravelRate(0.125) => slewCap = 0.125 * (numStates - 1) = 0.125 units/s, so
// the 1-unit journey occupies exactly 8 s. Three phases:
//
//   [0 s .. 2.5 s)    frozen at p = 0      <- endpoint-A transform lives here
//   [2.5 s .. 10.5 s) the 8 s journey
//   [10.5 s .. 13 s)  frozen at p = 1      <- endpoint-B transform lives here
//
// The endpoint transforms start 0.5 s INTO each frozen window (so the 20 ms
// kNormGainSmoothMs normalizer, the 2 ms FR-014 amplitude smoother and the
// 0.05 s minimum attack have all settled) and run 65536 samples = 1.365 s, which
// fits the 2.5 s window with 0.635 s to spare.
//
// WHY THE SHAPE IS PINNED AND NOT INCIDENTAL: a 65536-point transform is 17 % of
// the 8 s journey. A window taken WHILE the position moves smears across a
// moving spectrum -- for the Bell pairs the high slots travel hundreds of cents
// inside one window -- and the per-partial claim stops being measurable at all.
// Freezing is what makes the endpoint and bracketing numbers mean something.
constexpr float kTravelRateFast = 0.125f;
constexpr float kBloom = 0.5f;

constexpr std::size_t kFreezeSamples = 120000;          ///< 2.5 s at 48 kHz
constexpr std::size_t kAnalysisSettleSamples = 24000;   ///< 0.5 s at 48 kHz
constexpr std::size_t kJourneySamples = 384000;         ///< 8.0 s at 48 kHz
constexpr std::size_t kPairRenderSamples = 2 * kFreezeSamples + kJourneySamples;  // 624000

/// Clause 3's slow arm: setTravelRate(1/60) => 1/60 units/s => a 60 s journey.
constexpr float kTravelRateSlow = 1.0f / 60.0f;
constexpr std::size_t kSlowJourneySamples = 2880000;    ///< 60 s at 48 kHz
constexpr std::size_t kSlowRenderSamples = 2 * kFreezeSamples + kSlowJourneySamples;  // 3120000

/// The analysis window must fit inside a frozen phase with the settle in front.
static_assert(kAnalysisSettleSamples + kFftSize < kFreezeSamples);
/// Every phase boundary must land on a slice boundary, or the "<= 64-sample
/// slice" cadence would be broken by a short slice at each seam.
static_assert(kFreezeSamples % kSliceSamples == 0);
static_assert(kAnalysisSettleSamples % kSliceSamples == 0);
static_assert(kFftSize % kSliceSamples == 0);
static_assert(kJourneySamples % kSliceSamples == 0);
static_assert(kSlowJourneySamples % kSliceSamples == 0);

/// The bracketing render (deviation D16) freezes at p = 0.5. There is no hard
/// setTravelPosition() in this phase (Non-Goals: no retrigger, no position
/// setter), so the position is REACHED rather than assigned: at
/// kMaxTravelRate the shared slew cap is 1.0 unit/s, so p = 0.5 is attained
/// after 0.5 s and the limiter then assigns position_ = target exactly
/// (spectral_morph_engine.h:690-698). 1.0 s of approach leaves 2x the margin.
/// The frozen phase that follows is the full 2.5 s the criterion asks for.
constexpr std::size_t kBracketApproachSamples = 48000;   ///< 1.0 s at 48 kHz
constexpr std::size_t kBracketRenderSamples = kBracketApproachSamples + kFreezeSamples;
static_assert(kBracketApproachSamples % kSliceSamples == 0);

// -----------------------------------------------------------------------------
// Thresholds — every one of them from spec.md SC-009. NONE may be loosened.
// -----------------------------------------------------------------------------

/// Per-partial endpoint tolerance on NORMALIZED magnitudes, in dB. Phase 2 holds
/// per-partial magnitudes to 0.5 dB on the same 65536-point Blackman-Harris
/// transform (`harmonic_cloud_spectral_test.cpp:277`, `:690`); SC-009 doubles it
/// because two neighbouring states' partials can sit inside each other's skirts
/// near the top of the range.
constexpr float kEndpointMagnitudeToleranceDb = 1.0f;

/// "Non-silent throughout": RMS over EVERY non-overlapping 100 ms window. The
/// floor is the same -60 dBFS `ClickDetectorConfig::energyThresholdDb` SC-001
/// clause 2 uses, so "audible" means one thing across this spec.
constexpr float kRenderRmsFloorDbfs = -60.0f;
constexpr std::size_t kRmsWindowSamples = 4800;  ///< 100 ms at 48 kHz
static_assert(kPairRenderSamples % kRmsWindowSamples == 0);
static_assert(kSlowRenderSamples % kRmsWindowSamples == 0);
static_assert(kBracketRenderSamples % kRmsWindowSamples == 0);

/// Peak bound: 0.9 x HarmonicCloud::kOutputClamp, so an FR-017 normalization
/// failure fails LOUDLY instead of being swallowed by the clamp.
constexpr float kPeakBound = 0.9f * HarmonicCloud::kOutputClamp;  // 1.8
static_assert(kPeakBound < HarmonicCloud::kOutputClamp);

/// 4-term Blackman-Harris main-lobe half-width in bins. Summing POWER over the
/// whole main lobe removes scalloping loss -- the peak-bin magnitude of a
/// windowed tone varies by up to ~0.8 dB with its fractional bin offset, which
/// alone would eat most of the 1.0 dB budget. Copied from the Phase 2 recipe
/// (`harmonic_cloud_spectral_test.cpp:53-59`).
constexpr std::size_t kLobeHalfWidth = 4;

constexpr float kSpectrumFloorDb = -200.0f;

/// Linear RMS below which a 100 ms window is reported at a hard floor rather
/// than fed to log10. File-scope rather than function-local so no lambda has to
/// reach for an enclosing constant.
constexpr double kRmsFloorLinear = 1.0e-30;

/// Cloud seed. FR-016 draws each partial's initial MCF phase from the seeded
/// stream, so BOTH the per-partial orbit gain and the render's PEAK are
/// functions of it. This is the seed Phase 2 measured across the five Richness
/// settings, worst channel peak 0.50 / 0.56 / 0.60 / 0.67 / 0.92 -- the 0.92
/// figure is the r = 1, 64-partial, n^-0.5 case, the brightest parametric
/// spectrum the cloud produces and a fair proxy for Breath's n^-0.25
/// (`harmonic_cloud_spectral_test.cpp:67-72`).
/// IF A PEAK ASSERTION EVER FAILS, RE-PIN THIS SEED BY SWEEPING. The bound is
/// SC-009's and is not available for widening.
constexpr std::uint32_t kCloudSeed = 0x5E3A0014u;

/// Engine seed. Entropy is 0 for clauses 1 and 3, so no engine RNG stream
/// reaches the output; it is pinned anyway so the render stays reproducible if a
/// later change makes one live.
constexpr std::uint32_t kMorphSeed = 1u;

[[nodiscard]] float powerToDb(float power) noexcept {
    constexpr float kTiny = 1.0e-30f;
    return (power > kTiny) ? 10.0f * std::log10(power) : kSpectrumFloorDb;
}

[[nodiscard]] const char* stateLabel(SpectralStateId id) noexcept {
    switch (id) {
    case SpectralStateId::SineStack:
        return "SineStack";
    case SpectralStateId::Bell:
        return "Bell";
    case SpectralStateId::Choir:
        return "Choir";
    case SpectralStateId::Glass:
        return "Glass";
    case SpectralStateId::Breath:
        return "Breath";
    }
    return "?";
}

struct FactoryPair {
    SpectralStateId a = SpectralStateId::SineStack;
    SpectralStateId b = SpectralStateId::Bell;
};

/// All 10 unordered pairs of the five factory states (SC-009: "all 10 pairs").
constexpr std::array<FactoryPair, 10> kFactoryPairs{
    {{.a = SpectralStateId::SineStack, .b = SpectralStateId::Bell},
     {.a = SpectralStateId::SineStack, .b = SpectralStateId::Choir},
     {.a = SpectralStateId::SineStack, .b = SpectralStateId::Glass},
     {.a = SpectralStateId::SineStack, .b = SpectralStateId::Breath},
     {.a = SpectralStateId::Bell, .b = SpectralStateId::Choir},
     {.a = SpectralStateId::Bell, .b = SpectralStateId::Glass},
     {.a = SpectralStateId::Bell, .b = SpectralStateId::Breath},
     {.a = SpectralStateId::Choir, .b = SpectralStateId::Glass},
     {.a = SpectralStateId::Choir, .b = SpectralStateId::Breath},
     {.a = SpectralStateId::Glass, .b = SpectralStateId::Breath}}};

// -----------------------------------------------------------------------------
// Per-partial render gain — the MCF orbit correction, and why it is mandatory
// -----------------------------------------------------------------------------
// The kernel's steady state is `currentAmplitude -> targetAmplitude *
// antiAliasGain` and the rendered sample is `sinState * currentAmplitude`
// (`harmonic_oscillator_bank_simd.cpp:91-96`). `sinState` is NOT a unit
// sinusoid: for the Modified-Coupled-Form recurrence the closed-form orbit
// satisfies `A^2 * cos^2(w/2) = 1 + sin(w/2) * sin(2*phi)`, i.e. the orbit
// amplitude carries a phase-dependent term on top of the `1/cos(pi f/fs)` factor
// the anti-alias gain pre-compensates for. At f0 = 110 Hz / 48 kHz that term
// spans +1.60 / -2.55 dB at partial 64 -- more than TWICE SC-009's whole 1.0 dB
// budget, so an UNCORRECTED comparison would fail a correct implementation.
// Phase 2 measured the correction exact: 2.33 dB uncorrected, 0.000 dB corrected
// (`harmonic_cloud_spectral_test.cpp:104-125`).
//
// `A` is an invariant of the orbit while the frequency is constant, so it can be
// recovered from the state via `A^2 = s^2 + (c + k*s)^2 / q^2` with
// `k = sin(pi f/fs)`, `q = cos(pi f/fs)`, using only shipped FR-008 accessors.
// THE INVARIANCE IS WHY THE SNAPSHOT MUST BE TAKEN AT THE END OF THE FROZEN
// ANALYSIS WINDOW AND NOT AFTER THE WHOLE RENDER: the journey between the two
// windows changes every epsilon, so the post-render state says nothing about the
// orbit the p = 0 window was rendered with.
struct PartialSnapshot {
    std::array<float, HarmonicCloud::kMaxPartials> frequencyHz{};
    std::array<float, HarmonicCloud::kMaxPartials> renderGainDb{};
    std::array<bool, HarmonicCloud::kMaxPartials> antiAliasFaded{};
};

[[nodiscard]] PartialSnapshot snapshotPartials(const HarmonicCloud& cloud) {
    PartialSnapshot snap;
    const float fadeStartHz = HarmonicCloud::kAntiAliasFadeStart * 0.5f * kSampleRateF;
    for (std::size_t i = 0; i < HarmonicCloud::kMaxPartials; ++i) {
        const float fHz = cloud.getPartialFrequencyHz(i);
        snap.frequencyHz[i] = fHz;
        snap.antiAliasFaded[i] = (fHz >= fadeStartHz);

        const float halfAngle = kPi * fHz / kSampleRateF;
        const float k = std::sin(halfAngle);
        const float q = std::cos(halfAngle);
        const float s = cloud.getPartialSinState(i);
        const float c = cloud.getPartialCosState(i);
        const float cross = c + k * s;
        const float orbitAmplitude =
            (q > 0.0f) ? std::sqrt(s * s + (cross * cross) / (q * q)) : 0.0f;
        const float renderGain = cloud.getPartialAntiAliasGain(i) * orbitAmplitude;
        snap.renderGainDb[i] =
            (renderGain > 0.0f) ? 20.0f * std::log10(renderGain) : kSpectrumFloorDb;
    }
    return snap;
}

/// @brief Per-partial level in dB, orbit-corrected and NORMALIZED to partial 1.
///
/// Normalizing by partial 1 is what makes the criterion a claim about spectral
/// SHAPE: the FR-017 normalizer's global gain, the (offset-spread 0) envelope and
/// the mutation weight are all partial-independent here, so they cancel exactly
/// in the ratio and the unknown absolute gain never has to be modelled.
[[nodiscard]] std::array<float, HarmonicCloud::kMaxPartials> measureNormalizedDb(
    const std::vector<float>& mono, std::size_t start, const PartialSnapshot& snap) {
    REQUIRE(start + kFftSize <= mono.size());

    std::vector<float> window(kFftSize, 0.0f);
    Window::generateBlackmanHarris(window.data(), kFftSize);
    std::vector<float> frame(kFftSize, 0.0f);
    for (std::size_t i = 0; i < kFftSize; ++i) {
        frame[i] = mono[start + i] * window[i];
    }

    FFT fft;
    fft.prepare(kFftSize);
    // Fail loudly rather than analyse a zero-size spectrum: kMaxFFTSize = 8192
    // (fft.h:47) is documentary and pffft handles 2^16, but a future tightening
    // of that bound must red HERE instead of silently measuring nothing.
    REQUIRE(fft.isPrepared());

    std::vector<Complex> spectrum(fft.numBins());
    fft.forward(frame.data(), spectrum.data());

    const std::size_t lastBin = fft.numBins() - 1;
    std::array<float, HarmonicCloud::kMaxPartials> corrected{};
    for (std::size_t i = 0; i < HarmonicCloud::kMaxPartials; ++i) {
        const std::size_t center =
            TestUtils::frequencyToBin(snap.frequencyHz[i], kSampleRateF, kFftSize);
        const std::size_t lo = (center > kLobeHalfWidth) ? (center - kLobeHalfWidth) : 0;
        const std::size_t hi = std::min(center + kLobeHalfWidth, lastBin);

        float power = 0.0f;
        for (std::size_t b = lo; b <= hi; ++b) {
            const float mag = spectrum[b].magnitude();
            power += mag * mag;
        }
        corrected[i] = powerToDb(power) - snap.renderGainDb[i];
    }

    const float reference = corrected[0];
    for (std::size_t i = 0; i < HarmonicCloud::kMaxPartials; ++i) {
        corrected[i] -= reference;
    }
    return corrected;
}

/// @brief A state's own amplitude set, in the same normalized-to-partial-1 dB.
[[nodiscard]] std::array<float, HarmonicCloud::kMaxPartials> expectedNormalizedDb(
    const SpectralState& s) noexcept {
    std::array<float, HarmonicCloud::kMaxPartials> out{};
    const float reference = s.amplitudes[0];
    for (std::size_t i = 0; i < HarmonicCloud::kMaxPartials; ++i) {
        out[i] = (s.amplitudes[i] > 0.0f && reference > 0.0f)
                     ? 20.0f * std::log10(s.amplitudes[i] / reference)
                     : kSpectrumFloorDb;
    }
    return out;
}

// -----------------------------------------------------------------------------
// Whole-render hygiene: finiteness, peak, non-silence
// -----------------------------------------------------------------------------
struct RenderStats {
    bool allFinite = true;
    float peak = 0.0f;
    float worstWindowRmsDb = 0.0f;
    std::size_t windowCount = 0;
};

[[nodiscard]] RenderStats analyseRender(const std::vector<float>& left,
                                        const std::vector<float>& right) {
    RenderStats stats;
    stats.worstWindowRmsDb = 1.0e6f;

    // Bit-pattern finiteness (`db_utils.h:54`, `:174`) -- NEVER std::isnan: the
    // macOS leg builds -ffast-math and folds it away.
    const auto scan = [&stats](const std::vector<float>& channel) {
        for (const float v : channel) {
            if (Krate::DSP::detail::isNaN(v) || Krate::DSP::detail::isInf(v)) {
                stats.allFinite = false;
                continue;  // |NaN| would poison the peak below
            }
            stats.peak = std::max(stats.peak, std::abs(v));
        }
    };
    scan(left);
    scan(right);

    const auto toDb = [](double rms) {
        return (rms > kRmsFloorLinear) ? 20.0 * std::log10(rms) : -600.0;
    };

    const std::size_t windows = left.size() / kRmsWindowSamples;
    stats.windowCount = windows;
    for (std::size_t w = 0; w < windows; ++w) {
        const std::size_t base = w * kRmsWindowSamples;
        double sumL = 0.0;
        double sumR = 0.0;
        for (std::size_t i = 0; i < kRmsWindowSamples; ++i) {
            const double l = static_cast<double>(left[base + i]);
            const double r = static_cast<double>(right[base + i]);
            sumL += l * l;
            sumR += r * r;
        }
        const double n = static_cast<double>(kRmsWindowSamples);
        const double worst =
            std::min(toDb(std::sqrt(sumL / n)), toDb(std::sqrt(sumR / n)));
        stats.worstWindowRmsDb = std::min(stats.worstWindowRmsDb, static_cast<float>(worst));
    }
    return stats;
}

// -----------------------------------------------------------------------------
// The rig
// -----------------------------------------------------------------------------
struct ComposedRender {
    std::vector<float> left;
    std::vector<float> right;
    std::vector<float> mono;
    TestUtils::RenderFingerprint fingerprintLeft{};
    PartialSnapshot snapA{};  ///< captured at the END of the first analysis window
    PartialSnapshot snapB{};  ///< captured at the END of the second analysis window
    std::size_t outputCountA = 0;
    std::size_t outputCountB = 0;
    float positionA = 0.0f;
    float positionB = 0.0f;
};

/// @brief THE FR-086 SHAPE. The only place this TU advances the composition.
void driveSlices(SpectralMorphEngine& engine, HarmonicCloud& cloud, std::size_t numSamples,
                 ComposedRender& out) {
    std::array<float, kSliceSamples> sliceL{};
    std::array<float, kSliceSamples> sliceR{};
    for (std::size_t done = 0; done < numSamples; done += kSliceSamples) {
        const std::size_t n = std::min(kSliceSamples, numSamples - done);
        engine.updateChunk(n);
        cloud.setSpectralTarget(engine.getOutputRatios(), engine.getOutputAmplitudes(),
                                engine.getOutputCount());
        cloud.processStereoBlock(sliceL.data(), sliceR.data(), n);
        const auto count = static_cast<std::ptrdiff_t>(n);
        out.left.insert(out.left.end(), sliceL.begin(), sliceL.begin() + count);
        out.right.insert(out.right.end(), sliceR.begin(), sliceR.begin() + count);
    }
}

/// @brief The pinned cloud configuration (SC-009). Everything between the
/// injected amplitudes and the render depends on it, so none of it is a default.
void configureRenderCloud(HarmonicCloud& cloud, float driftCents, float mutation) {
    cloud.setSeed(kCloudSeed);
    cloud.prepare(kSampleRate);
    cloud.setFundamentalHz(kF0Hz);
    cloud.setRichness(1.0f);         // N(r) = clamp(round(64^1), 1, 64) = 64: no slot gated off
    cloud.setSpectralGravity(0.0f);  // warp factor == 1, so the injected ratio IS the ratio
    cloud.setInharmonicity(0.0f);    // stretch == 1
    cloud.setSpectralTiltDb(0.0f);   // tiltGain == 1, so amplitude == the injected amplitude
    cloud.setMutation(mutation);
    cloud.setDriftDepthCents(driftCents);
    cloud.setStereoSpread(0.0f);
    cloud.setAttackTimeSec(HarmonicCloud::kMinAttackSec);
    cloud.setDecayTimeSec(HarmonicCloud::kMinDecaySec);
    cloud.setEnvelopeOffsetSpread(0.0f);  // every partial shares one envelope => it cancels
}

/// @brief Configure the engine with the pair loaded and the fade already spent.
///
/// ORDER IS LOAD-BEARING. `setState` arms the FR-047 absorption fade whenever the
/// slot contributes (`spectral_morph_engine.h:310-312`), and that fade runs for
/// kStateChangeFadeSec = 2.0 s -- straight through the endpoint-A analysis
/// window, blending the constructor's SineStack default into state A. `prepare()`
/// calls `reset()`, which sets `fadeX_ = 1.0f` (`:251`), so configuring the slots
/// BEFORE prepare is what makes the p = 0 window a measurement of state A rather
/// than of a fade. The REQUIRE below is the guard on that.
void configureEngine(SpectralMorphEngine& engine, const SpectralState& a, const SpectralState& b,
                     float travelRate) {
    engine.setSeed(kMorphSeed);
    engine.setState(0, a);
    engine.setState(1, b);
    engine.setStateCount(2);
    engine.prepare(kSampleRate);
    REQUIRE(engine.isPrepared());
    REQUIRE_FALSE(engine.isStateFadeActive());
    engine.setBloom(kBloom);
    engine.setEntropy(0.0f);
    engine.setTravelMode(SpectralMorphEngine::TravelMode::External);
    engine.setTravelRate(travelRate);
    engine.setTargetPosition(0.0f);
}

void finalizeRender(ComposedRender& out) {
    out.mono.resize(out.left.size());
    for (std::size_t i = 0; i < out.left.size(); ++i) {
        out.mono[i] = 0.5f * (out.left[i] + out.right[i]);
    }
    out.fingerprintLeft = TestUtils::fingerprintRender(std::span<const float>(out.left));
}

/// @brief The three-phase journey render: freeze at p = 0, travel, freeze at p = 1.
[[nodiscard]] ComposedRender renderJourney(SpectralStateId idA, SpectralStateId idB,
                                           float travelRate, std::size_t journeySamples,
                                           float driftCents, float mutation) {
    const SpectralState a = makeFactoryState(idA);
    const SpectralState b = makeFactoryState(idB);
    REQUIRE(isValidSpectralState(a));
    REQUIRE(isValidSpectralState(b));

    SpectralMorphEngine engine;
    configureEngine(engine, a, b, travelRate);

    HarmonicCloud cloud;
    configureRenderCloud(cloud, driftCents, mutation);
    cloud.noteOn();  // NEVER noteOff(): a quiescent cloud zero-fills (harmonic_cloud.h:844-853)

    ComposedRender out;
    const std::size_t total = 2 * kFreezeSamples + journeySamples;
    out.left.reserve(total);
    out.right.reserve(total);

    // Phase 1, up to and including the endpoint-A analysis window.
    driveSlices(engine, cloud, kAnalysisSettleSamples + kFftSize, out);
    out.snapA = snapshotPartials(cloud);
    out.outputCountA = engine.getOutputCount();
    out.positionA = engine.getTravelPosition();

    // Remainder of the first frozen phase.
    driveSlices(engine, cloud, kFreezeSamples - kAnalysisSettleSamples - kFftSize, out);

    // The journey.
    engine.setTargetPosition(1.0f);
    driveSlices(engine, cloud, journeySamples, out);

    // Phase 3, up to and including the endpoint-B analysis window.
    driveSlices(engine, cloud, kAnalysisSettleSamples + kFftSize, out);
    out.snapB = snapshotPartials(cloud);
    out.outputCountB = engine.getOutputCount();
    out.positionB = engine.getTravelPosition();

    // Remainder of the second frozen phase.
    driveSlices(engine, cloud, kFreezeSamples - kAnalysisSettleSamples - kFftSize, out);

    REQUIRE(out.left.size() == total);
    finalizeRender(out);
    return out;
}

/// @brief DEVIATION D16 — the bracketing sample, from a SEPARATE render frozen at
/// p = 0.5, never from a window taken during travel.
[[nodiscard]] ComposedRender renderFrozenMidpoint(SpectralStateId idA, SpectralStateId idB) {
    const SpectralState a = makeFactoryState(idA);
    const SpectralState b = makeFactoryState(idB);

    SpectralMorphEngine engine;
    configureEngine(engine, a, b, SpectralMorphEngine::kMaxTravelRate);
    engine.setTargetPosition(0.5f);

    HarmonicCloud cloud;
    configureRenderCloud(cloud, 0.0f, 0.0f);
    cloud.noteOn();

    ComposedRender out;
    out.left.reserve(kBracketRenderSamples);
    out.right.reserve(kBracketRenderSamples);

    // Approach: the slew limiter walks p to 0.5 and then assigns it exactly.
    driveSlices(engine, cloud, kBracketApproachSamples, out);

    // Frozen phase: settle, then analyse.
    driveSlices(engine, cloud, kAnalysisSettleSamples + kFftSize, out);
    out.snapA = snapshotPartials(cloud);
    out.outputCountA = engine.getOutputCount();
    out.positionA = engine.getTravelPosition();

    driveSlices(engine, cloud, kFreezeSamples - kAnalysisSettleSamples - kFftSize, out);

    REQUIRE(out.left.size() == kBracketRenderSamples);
    finalizeRender(out);
    return out;
}

/// Window start offsets inside a journey render.
[[nodiscard]] constexpr std::size_t windowAStart() noexcept { return kAnalysisSettleSamples; }
[[nodiscard]] constexpr std::size_t windowBStart(std::size_t journeySamples) noexcept {
    return kFreezeSamples + journeySamples + kAnalysisSettleSamples;
}
constexpr std::size_t kBracketWindowStart = kBracketApproachSamples + kAnalysisSettleSamples;

/// @brief The hygiene clause, shared by all three clauses of SC-009.
void requireRenderHygiene(const RenderStats& stats, std::size_t expectedWindows) {
    INFO("peak " << stats.peak << " (bound " << kPeakBound << "), worst 100 ms window RMS "
                 << stats.worstWindowRmsDb << " dBFS (floor " << kRenderRmsFloorDbfs << "), "
                 << stats.windowCount << " windows");
    REQUIRE(stats.allFinite);
    REQUIRE(stats.windowCount == expectedWindows);
    REQUIRE(stats.peak < kPeakBound);
    REQUIRE(stats.worstWindowRmsDb >= kRenderRmsFloorDbfs);
}

/// @brief The endpoint clause: measured shape against the state's own amplitudes.
/// @return the number of partials excluded because the cloud anti-alias-faded them.
[[nodiscard]] std::size_t requireEndpointMatch(
    const std::array<float, HarmonicCloud::kMaxPartials>& measured, const SpectralState& state,
    const PartialSnapshot& snap, const char* label) {
    const std::array<float, HarmonicCloud::kMaxPartials> expected = expectedNormalizedDb(state);
    const auto count = static_cast<std::size_t>(state.numPartials);
    std::size_t excluded = 0;
    for (std::size_t i = 0; i < count; ++i) {
        if (snap.antiAliasFaded[i]) {
            // A silenced partial has no magnitude to compare (Phase 2's SC-010
            // pattern). The count is asserted by the caller.
            ++excluded;
            continue;
        }
        INFO(label << " partial " << (i + 1) << " at " << snap.frequencyHz[i] << " Hz: measured "
                   << measured[i] << " dB, expected " << expected[i] << " dB (tolerance "
                   << kEndpointMagnitudeToleranceDb << " dB)");
        REQUIRE(std::abs(measured[i] - expected[i]) <= kEndpointMagnitudeToleranceDb);
    }
    return excluded;
}

}  // namespace

// =============================================================================
// SC-009 — audible A/B renders for each factory state pair (T020)
// =============================================================================
TEST_CASE("SpectralMorph_FactoryPairRenders", "[spectral_morph][seraphis]") {
    // -------------------------------------------------------------------------
    // CLAUSE 1 — SPECTRAL CORRECTNESS
    // -------------------------------------------------------------------------
    // Per pair: one 13.0 s three-phase journey render plus one separate 3.5 s
    // render frozen at p = 0.5 (deviation D16). The journey render carries the
    // hygiene clause and the two endpoint spectra; the frozen render carries the
    // bracketing clause.
    SECTION("clause 1: every pair renders cleanly and hits both endpoint spectra") {
        for (const FactoryPair& pair : kFactoryPairs) {
            const SpectralState stateA = makeFactoryState(pair.a);
            const SpectralState stateB = makeFactoryState(pair.b);
            INFO("pair " << stateLabel(pair.a) << " -> " << stateLabel(pair.b));

            TestUtils::RenderFingerprint journeyFingerprint;
            std::size_t excluded = 0;

            {
                const ComposedRender render =
                    renderJourney(pair.a, pair.b, kTravelRateFast, kJourneySamples, 0.0f, 0.0f);
                journeyFingerprint = render.fingerprintLeft;

                requireRenderHygiene(analyseRender(render.left, render.right),
                                     kPairRenderSamples / kRmsWindowSamples);

                // The travel actually happened, and both endpoints were attained.
                // At p = 1 exactly the engine takes its a == b copy path
                // (spectral_morph_engine.h:617-631), which is what makes the
                // second endpoint state B rather than a lerp that nearly is.
                REQUIRE(render.positionA == Approx(0.0f).margin(1.0e-6));
                REQUIRE(render.positionB == Approx(1.0f).margin(1.0e-6));
                const std::size_t expectedCountA =
                    static_cast<std::size_t>(std::max(stateA.numPartials, stateB.numPartials));
                const std::size_t expectedCountB = static_cast<std::size_t>(stateB.numPartials);
                REQUIRE(render.outputCountA == expectedCountA);
                REQUIRE(render.outputCountB == expectedCountB);

                const std::array<float, HarmonicCloud::kMaxPartials> measuredA =
                    measureNormalizedDb(render.mono, windowAStart(), render.snapA);
                const std::array<float, HarmonicCloud::kMaxPartials> measuredB =
                    measureNormalizedDb(render.mono, windowBStart(kJourneySamples), render.snapB);

                excluded += requireEndpointMatch(measuredA, stateA, render.snapA, "endpoint A");
                excluded += requireEndpointMatch(measuredB, stateB, render.snapB, "endpoint B");
            }

            // ---------------------------------------------------------------
            // Bracketing (deviation D16) — from the separately frozen p = 0.5
            // render, scoped to i < min(A.numPartials, B.numPartials).
            // ---------------------------------------------------------------
            // WHY THE SCOPE: for the Bell pairs (24 authored partials against 64)
            // the slots at i >= 24 interpolate a FR-041 CONTINUATION ratio against
            // a real one at nonzero amplitude, so "bracketed by the endpoints" is
            // not a meaningful claim about them -- there is no authored
            // counterpart in one of the two states.
            //
            // WHY THE BRACKET IS WIDENED BY THE BLOOM STAGGER: the measured
            // quantity is a RATIO to partial 1, and at bloom 0.5 partial 1's own
            // completion (u_0 = u / 0.7) differs from partial i's. Numerator and
            // denominator are therefore convex combinations at DIFFERENT weights,
            // so the mediant argument that would pin the ratio between a_i/a_1 and
            // b_i/b_1 does not apply; what does hold is
            //   min(a_i, b_i) / max(a_1, b_1) <= ratio <= max(a_i, b_i) / min(a_1, b_1)
            // which is exactly "bracketed by the two, allowing the FR-051
            // stagger". Narrowing it would be asserting something false.
            {
                const ComposedRender frozen = renderFrozenMidpoint(pair.a, pair.b);
                requireRenderHygiene(analyseRender(frozen.left, frozen.right),
                                     kBracketRenderSamples / kRmsWindowSamples);
                REQUIRE(frozen.positionA == Approx(0.5f).margin(1.0e-6));

                const std::array<float, HarmonicCloud::kMaxPartials> mid =
                    measureNormalizedDb(frozen.mono, kBracketWindowStart, frozen.snapA);

                const auto shared =
                    static_cast<std::size_t>(std::min(stateA.numPartials, stateB.numPartials));
                // Bell's 24 authored partials are the sparsest factory state, so
                // every pair shares at least that many slots.
                REQUIRE(shared >= std::size_t{24});
                const float refLo = std::min(stateA.amplitudes[0], stateB.amplitudes[0]);
                const float refHi = std::max(stateA.amplitudes[0], stateB.amplitudes[0]);
                REQUIRE(refLo > 0.0f);

                std::size_t bracketed = 0;
                std::size_t bracketExcluded = 0;
                for (std::size_t i = 0; i < shared; ++i) {
                    if (frozen.snapA.antiAliasFaded[i]) {
                        ++bracketExcluded;
                        continue;
                    }
                    const float lo = std::min(stateA.amplitudes[i], stateB.amplitudes[i]);
                    const float hi = std::max(stateA.amplitudes[i], stateB.amplitudes[i]);
                    REQUIRE(lo > 0.0f);
                    const float lowerDb =
                        20.0f * std::log10(lo / refHi) - kEndpointMagnitudeToleranceDb;
                    const float upperDb =
                        20.0f * std::log10(hi / refLo) + kEndpointMagnitudeToleranceDb;

                    INFO("midpoint partial " << (i + 1) << " at " << frozen.snapA.frequencyHz[i]
                                             << " Hz: measured " << mid[i] << " dB, bracket ["
                                             << lowerDb << ", " << upperDb << "] dB");
                    REQUIRE(mid[i] >= lowerDb);
                    REQUIRE(mid[i] <= upperDb);
                    ++bracketed;
                }
                // Every shared slot is accounted for: either bracketed or
                // excluded, never silently dropped.
                REQUIRE(bracketed + bracketExcluded == shared);
                excluded += bracketExcluded;

                // REGRESSION PIN, NOT A CORRECTNESS PROOF. Bit-exact float
                // goldens over rendered audio are forbidden (dsp/CLAUDE.md), so
                // the pin is a fingerprint comparison at render_fingerprint.h's
                // published tolerances against an INDEPENDENTLY CONSTRUCTED
                // second render of the identical configuration. It pins
                // reproducibility (uninitialised state, seed leakage, order
                // dependence between the engine and the cloud); the CORRECTNESS
                // content of this criterion is the endpoint and bracketing
                // assertions above, never this comparison.
                const TestUtils::RenderFingerprint repeat =
                    renderFrozenMidpoint(pair.a, pair.b).fingerprintLeft;
                const TestUtils::FingerprintComparison comparison =
                    TestUtils::compareFingerprints(repeat, frozen.fingerprintLeft);
                INFO("frozen-midpoint regression pin: worst metric relative error "
                     << comparison.worstMetricRelativeError << ", worst sample error "
                     << comparison.worstSampleError << " (" << comparison.detail << ")");
                REQUIRE(comparison.withinTolerance());
            }

            // Every bracketed and endpoint partial sits below the anti-alias fade
            // start, so the exclusion count is DERIVED, not observed: the highest
            // ratio any of these assertions touches is Bell's ratio_24 = 117.67,
            // i.e. 12.94 kHz at f0 = 110 Hz, against a fade start of
            // 0.8 * 24 kHz = 19.2 kHz. A nonzero count here means the interpolated
            // ratios left the range the criterion was derived over.
            INFO("anti-alias exclusions across both endpoints and the midpoint: " << excluded);
            REQUIRE(excluded == 0);

            // Regression pin for the journey render (see the note above).
            const TestUtils::RenderFingerprint repeatJourney =
                renderJourney(pair.a, pair.b, kTravelRateFast, kJourneySamples, 0.0f, 0.0f)
                    .fingerprintLeft;
            const TestUtils::FingerprintComparison journeyComparison =
                TestUtils::compareFingerprints(repeatJourney, journeyFingerprint);
            INFO("journey regression pin: worst metric relative error "
                 << journeyComparison.worstMetricRelativeError << ", worst sample error "
                 << journeyComparison.worstSampleError << " (" << journeyComparison.detail << ")");
            REQUIRE(journeyComparison.withinTolerance());
        }
    }

    // -------------------------------------------------------------------------
    // CLAUSE 2 — ROBUSTNESS UNDER THE CLOUD'S OWN LIFE MODULATION
    // -------------------------------------------------------------------------
    // Same 10 pairs, same drive shape, with the cloud's drift depth at maximum
    // and mutation at 1.0. This clause asserts ONLY finiteness, the peak bound
    // and non-silence, and makes NO spectral-shape claim -- deliberately.
    // +-50 cents of independent per-partial drift is nearly twice the 27.3-cent
    // spacing of adjacent high partials, so the rendered spectrum is EXPECTED to
    // smear and reorder there; asserting a shape match against it would be
    // asserting something false.
    SECTION("clause 2: every pair stays finite, bounded and audible under max drift and mutation") {
        for (const FactoryPair& pair : kFactoryPairs) {
            INFO("pair " << stateLabel(pair.a) << " -> " << stateLabel(pair.b) << " at drift "
                         << HarmonicCloud::kMaxDriftCents << " cents, mutation 1.0");

            TestUtils::RenderFingerprint fingerprint;
            {
                const ComposedRender render =
                    renderJourney(pair.a, pair.b, kTravelRateFast, kJourneySamples,
                                  HarmonicCloud::kMaxDriftCents, 1.0f);
                fingerprint = render.fingerprintLeft;
                requireRenderHygiene(analyseRender(render.left, render.right),
                                     kPairRenderSamples / kRmsWindowSamples);
                REQUIRE(render.positionB == Approx(1.0f).margin(1.0e-6));
            }

            // Regression pin (see clause 1) — reproducibility, not correctness.
            const TestUtils::RenderFingerprint repeat =
                renderJourney(pair.a, pair.b, kTravelRateFast, kJourneySamples,
                              HarmonicCloud::kMaxDriftCents, 1.0f)
                    .fingerprintLeft;
            const TestUtils::FingerprintComparison comparison =
                TestUtils::compareFingerprints(repeat, fingerprint);
            INFO("modulated regression pin: worst metric relative error "
                 << comparison.worstMetricRelativeError << ", worst sample error "
                 << comparison.worstSampleError << " (" << comparison.detail << ")");
            REQUIRE(comparison.withinTolerance());
        }
    }

    // -------------------------------------------------------------------------
    // CLAUSE 3 — THE SLOW-TRAVEL ARM
    // -------------------------------------------------------------------------
    // THIS IS THE CLAUSE THAT FAILS UNDER A STALE-BASELINE setSpectralTarget
    // (deviation D14). At 1/60 journeys per second the per-chunk ratio motion is
    // ~0.05-0.5 cent for the low partials and 0.006 cent for partial 24 -- all
    // permanently below kTargetRatioEpsilonCents = 0.05. If the FR-085 dirty test
    // compared the incoming target against the STORED target instead of against
    // the COMMITTED one (`harmonic_cloud.h:779-795`), the baseline would advance
    // with the input while the recompute was skipped, so most partials would
    // freeze at their p = 0 frequency for the whole 60 s journey and the p = 1
    // endpoint would still be state A. The endpoint assertion below is what
    // catches that; the 8 s arm of clause 1 does not, because at that rate the
    // per-chunk motion clears the epsilon.
    SECTION("clause 3: a 60 s journey still arrives at state B") {
        constexpr SpectralStateId kSlowA = SpectralStateId::SineStack;
        constexpr SpectralStateId kSlowB = SpectralStateId::Bell;
        const SpectralState stateA = makeFactoryState(kSlowA);
        const SpectralState stateB = makeFactoryState(kSlowB);
        INFO("slow arm " << stateLabel(kSlowA) << " -> " << stateLabel(kSlowB) << " over 60 s");

        TestUtils::RenderFingerprint fingerprint;
        std::size_t excluded = 0;
        {
            const ComposedRender render =
                renderJourney(kSlowA, kSlowB, kTravelRateSlow, kSlowJourneySamples, 0.0f, 0.0f);
            fingerprint = render.fingerprintLeft;

            requireRenderHygiene(analyseRender(render.left, render.right),
                                 kSlowRenderSamples / kRmsWindowSamples);
            REQUIRE(render.positionA == Approx(0.0f).margin(1.0e-6));
            REQUIRE(render.positionB == Approx(1.0f).margin(1.0e-6));
            const std::size_t expectedCountB = static_cast<std::size_t>(stateB.numPartials);
            REQUIRE(render.outputCountB == expectedCountB);

            const std::array<float, HarmonicCloud::kMaxPartials> measuredA =
                measureNormalizedDb(render.mono, windowAStart(), render.snapA);
            const std::array<float, HarmonicCloud::kMaxPartials> measuredB =
                measureNormalizedDb(render.mono, windowBStart(kSlowJourneySamples), render.snapB);

            excluded += requireEndpointMatch(measuredA, stateA, render.snapA, "slow endpoint A");
            excluded += requireEndpointMatch(measuredB, stateB, render.snapB, "slow endpoint B");
        }
        INFO("anti-alias exclusions across the slow arm's endpoints: " << excluded);
        REQUIRE(excluded == 0);

        // Regression pin (see clause 1) — reproducibility, not correctness.
        const TestUtils::RenderFingerprint repeat =
            renderJourney(kSlowA, kSlowB, kTravelRateSlow, kSlowJourneySamples, 0.0f, 0.0f)
                .fingerprintLeft;
        const TestUtils::FingerprintComparison comparison =
            TestUtils::compareFingerprints(repeat, fingerprint);
        INFO("slow-arm regression pin: worst metric relative error "
             << comparison.worstMetricRelativeError << ", worst sample error "
             << comparison.worstSampleError << " (" << comparison.detail << ")");
        REQUIRE(comparison.withinTolerance());
    }
}

// =============================================================================
// SC-009 root-cause regression — the MCF orbit energy across an injected sweep
// =============================================================================
// THIS IS THE TEST THAT FAILS ON THE DEFECT SC-009 CAUGHT, and it names the
// mechanism instead of only observing the consequence.
//
// The kernel's recurrence (`harmonic_oscillator_bank_simd.cpp:103-105`)
// `s' = s + eps*c`, `c' = c - eps*s'` conserves `E = s^2 + c^2 + eps*s*c` for a
// FIXED eps, and a partial's rendered peak is `targetAmplitude * fade * sqrt(E)`
// because the orbit peak `sqrt(E)/cos(w/2)` and FR-015's `cos(w/2)` correction
// cancel. Rewriting eps at a fixed state moves E by `(eps_new - eps_old)*s*c`,
// and `|s| ~ 1/cos(w/2)` is 10x at the `kMaxEpsilon` clamp — so a partial swept
// through the near-Nyquist region accumulates a large energy error, silently,
// because FR-015 has already faded it out while it happens. It becomes audible
// only when the sweep brings the partial back down.
//
// `Bell -> Breath` is the worst case in the SC-009 grid and the pair that
// actually failed: Bell's FR-041 fill puts slots 25..64 at ratios 118..240, i.e.
// 13.0..26.4 kHz at the pinned f0 = 110 Hz — above Nyquist at the top — and the
// journey then walks every one of them down to Breath's 25..64 (2.8..7.0 kHz)
// over ~7,500 control chunks. MEASURED before the fix: per-partial render-gain
// errors of +11.3 dB (partial 55) and -12.7 dB (partial 59), whole-render RMS
// 3.8 dB above the FR-017 target (0.531 against 0.343 per channel) and the peak
// pinned at kOutputClamp. After it: worst per-partial drift 0.0012 dB.
//
// The assertion is on the CHANGE in each partial's render gain, not on its
// absolute value, because the absolute value is Phase 2's FR-016 phase draw
// (`E = 1 + (eps/2)*sin(4*pi*phase)` right after `redrawPhases()`, i.e. the
// +1.60 / -2.55 dB spread `harmonic_cloud_spectral_test.cpp:104-125` measured)
// and is not this fix's business. Preserving E is.
TEST_CASE("SpectralMorph_OrbitEnergySurvivesTheSweep", "[spectral_morph][seraphis]") {
    // 0.5 dB sits between the measured worst drift with the fix in place
    // (0.0012 dB, i.e. 400x of margin) and the 4.82 dB this same metric reports
    // with the rescale disabled — verified by disabling it, not assumed — so the
    // test is neither brittle nor satisfiable by a partial fix.
    // It is NOT a tolerance to widen:
    // the quantity it bounds is conserved exactly in exact arithmetic, so any
    // growth is float rounding over the sweep and must be re-measured, not
    // accommodated.
    constexpr float kMaxRenderGainDriftDb = 0.5f;

    const SpectralState stateA = makeFactoryState(SpectralStateId::Bell);
    const SpectralState stateB = makeFactoryState(SpectralStateId::Breath);

    SpectralMorphEngine engine;
    configureEngine(engine, stateA, stateB, kTravelRateFast);

    HarmonicCloud cloud;
    configureRenderCloud(cloud, 0.0f, 0.0f);
    cloud.noteOn();

    ComposedRender out;
    out.left.reserve(kPairRenderSamples);
    out.right.reserve(kPairRenderSamples);

    // One slice, so the injected target has been applied once and every partial
    // is on the orbit whose energy the rest of the render must preserve.
    driveSlices(engine, cloud, kSliceSamples, out);
    const PartialSnapshot before = snapshotPartials(cloud);

    driveSlices(engine, cloud, kFreezeSamples - kSliceSamples, out);
    engine.setTargetPosition(1.0f);
    driveSlices(engine, cloud, kJourneySamples, out);
    driveSlices(engine, cloud, kFreezeSamples, out);
    REQUIRE(out.left.size() == kPairRenderSamples);
    REQUIRE(engine.getTravelPosition() == Approx(1.0f).margin(1.0e-6));

    const PartialSnapshot after = snapshotPartials(cloud);

    // Non-vacuity: the sweep really did drive partials into the region where the
    // defect lives. Bell's filled slot 64 sits above Nyquist at f0 = 110 Hz.
    const float nyquistHz = 0.5f * kSampleRateF;
    bool reachedNyquist = false;
    for (std::size_t i = 0; i < HarmonicCloud::kMaxPartials; ++i) {
        reachedNyquist = reachedNyquist || (before.frequencyHz[i] >= nyquistHz);
    }
    REQUIRE(reachedNyquist);

    float worstDrift = 0.0f;
    std::size_t worstIndex = 0;
    for (std::size_t i = 0; i < HarmonicCloud::kMaxPartials; ++i) {
        // A partial the FR-015 fade has silenced at EITHER end has no orbit to
        // compare: `renderGainDb` folds the fade in, so it is -inf there by
        // construction rather than by drift.
        if (before.antiAliasFaded[i] || after.antiAliasFaded[i]) {
            continue;
        }
        const float drift = std::abs(after.renderGainDb[i] - before.renderGainDb[i]);
        if (drift > worstDrift) {
            worstDrift = drift;
            worstIndex = i;
        }
    }
    INFO("worst render-gain drift " << worstDrift << " dB at partial " << (worstIndex + 1)
                                    << " (bound " << kMaxRenderGainDriftDb << " dB): "
                                    << before.renderGainDb[worstIndex] << " dB -> "
                                    << after.renderGainDb[worstIndex] << " dB");
    REQUIRE(worstDrift <= kMaxRenderGainDriftDb);
}

// =============================================================================
// T021 — SC-001 cl.2, SC-004 m.3, SC-004 m.4, SC-008 cl.3
// =============================================================================

namespace {

/// THE PINNED SEED SET (tasks.md:79-84), declared ONCE for this TU. SC-001
/// clause 2 uses its first four; SC-004 averages over all eight.
constexpr std::array<std::uint32_t, 8> kSeeds{1u, 7u, 13u, 29u, 101u, 257u, 1009u, 65537u};

// -----------------------------------------------------------------------------
// SC-001 clause 2 — click-free travel, measured differentially
// -----------------------------------------------------------------------------
// The detector's threshold is a WITHIN-FRAME mean(|dx|) + 5*stddev(|dx|)
// (`artifact_detection.h:186-193`), so it has a nonzero FALSE-DETECTION FLOOR on
// perfectly click-free material: Phase 2 measured 126 (L) / 141 (R) detections
// over a 30 s / 48 kHz HarmonicCloud render that contained no discontinuity at
// all. "Zero detections over a 20 s render" is therefore not a criterion any
// correct implementation could satisfy, and the clause is DIFFERENTIAL: every
// moving render is paired with a TRAVEL-FROZEN, DRIFT-LIVE control at the
// journey midpoint, built from the identical seed, entropy, cloud and engine
// configuration, so the floor appears on BOTH sides and cancels.
//
// "DRIFT-LIVE" is the point of the pairing: the control freezes ONLY the travel
// target. The engine's entropy machinery (two 64-lane OU banks, the ratio
// scatter redraws and the FR-073 lifecycle) keeps advancing exactly as it does
// in the moving arm, so what the comparison isolates is the TRAVEL, not the
// stochastic modulation that runs underneath it.
//
// THE DETECTOR CONFIGURATION IS THE ONE PINNED FOR THIS PHASE and is shared with
// SC-014 clause 3: `detectClicks` above builds it with
// sampleRate = 48000, frameSize = 512, hopSize = 256, detectionThreshold = 5.0,
// energyThresholdDb = -60.0, mergeGap = 5. The struct default sampleRate is
// 44100 (`artifact_detection.h:39`) and MUST be overridden -- a mismatch silently
// mis-scales `timeSeconds` on every detection.

/// 20 s at 48 kHz.
constexpr std::size_t kTravelRenderSamples = 960000;

/// At `kMaxTravelRate` the shared slew cap is `rate * (numStates - 1)` =
/// 1.0 unit/s (`spectral_morph_engine.h:355-356`), so a full 0 -> 1 journey
/// occupies exactly 1 s. A 20 s render that set the target once would travel for
/// 1 s and sit frozen for 19, which is not what this clause measures — the
/// target is therefore FLIPPED at each 1 s leg boundary so the position is in
/// motion across the whole measured window. A flip reverses the position's
/// DERIVATIVE and never its value, so it introduces no discontinuity of its own;
/// what it does do is put 20 traversals of the SineStack -> Bell interpolation,
/// in both directions, inside one detector run.
constexpr std::size_t kTravelLegSamples = 48000;

/// Pre-roll, rendered and DISCARDED, identical in length for both arms so the
/// note attack, the 20 ms `kNormGainSmoothMs` normalizer and the FR-014
/// amplitude smoother are all settled before either buffer starts. It is also
/// what puts the frozen arm AT the journey midpoint: there is no position setter
/// in this phase (Non-Goals), so p = 0.5 is REACHED — 0.5 units at 1.0 unit/s
/// takes 0.5 s, and the limiter then assigns `position_ = target` exactly
/// (`spectral_morph_engine.h:690-698`). 1.0 s leaves 2x the margin.
constexpr std::size_t kTravelPreRollSamples = 48000;

constexpr float kTravelFrozenPosition = 0.5f;  ///< the journey midpoint

static_assert(kTravelRenderSamples % kTravelLegSamples == 0);
static_assert(kTravelLegSamples % kSliceSamples == 0);
static_assert(kTravelPreRollSamples % kSliceSamples == 0);

/// SC-001 clause 2's two entropy arms.
constexpr std::array<float, 2> kTravelEntropyArms{0.0f, 1.0f};

/// SEED COUNT — THE ONE KNOB THIS CLAUSE IS ALLOWED TO TURN.
/// If the measured 4-seed spread exceeds the pass rule's own band
/// (+-15 % + 5), the response is to RAISE THIS TO 8 (the pinned set has eight
/// entries for exactly this reason) and re-measure the medians. It is NEVER to
/// widen `kTravelClickSlope` / `kTravelClickOffset`.
constexpr std::size_t kTravelSeedCount = 4;
static_assert(kTravelSeedCount <= kSeeds.size());

// -----------------------------------------------------------------------------
// THE PASS RULE — PROVENANCE
// -----------------------------------------------------------------------------
// `moving <= kTravelClickSlope * frozen + kTravelClickOffset`, applied per
// (seed, entropy, channel) pair AND to the per-entropy-arm/channel median over
// the seeds.
//
// STATUS OF THESE TWO NUMBERS: they are the figures the criterion was written
// with (spec.md SC-001 clause 2, tasks.md:1674). tasks.md:1675-1678 requires
// them to be RE-DERIVED from the measured 4-seed spread and the observed numbers
// written in here. This test reports EVERY per-pair count and EVERY median
// through WARN, unconditionally, so that re-derivation is a read of one run's
// log and never a second run.
//
// WHEN THE MEASUREMENT ARRIVES, EDIT THIS BLOCK — NOT THE THRESHOLD:
//   * spread within +-15 % + 5  -> record the observed medians and per-arm
//                                  spread here as the provenance for the shipped
//                                  slope/offset;
//   * spread wider              -> set kTravelSeedCount = 8 and re-measure.
// A moving count that exceeds the rule is a FINDING ABOUT THE TRAVEL PATH — a
// real discontinuity in the interpolated ratios or amplitudes — not a threshold
// to widen.
constexpr double kTravelClickSlope = 1.15;
constexpr double kTravelClickOffset = 5.0;

/// @brief One arm of an SC-001 clause 2 pair.
/// @param moving `true` travels continuously for the whole measured window;
///        `false` is the travel-frozen, drift-live control at p = 0.5.
[[nodiscard]] ComposedRender renderTravelArm(std::uint32_t seed, float entropy, bool moving) {
    const SpectralState stateA = makeFactoryState(SpectralStateId::SineStack);
    const SpectralState stateB = makeFactoryState(SpectralStateId::Bell);

    SpectralMorphEngine engine;
    // configureEngine loads the slots BEFORE prepare() (see its own comment: that
    // ordering is what keeps the FR-047 absorption fade out of the measured
    // window) and pins bloom = 0.5 and TravelMode::External, which is exactly
    // this clause's configuration. Seed, entropy and target are overridden
    // afterwards: all three are configuration-time setters that refresh the
    // outputs without advancing anything (`spectral_morph_engine.h:265-270`,
    // `:340`, `:347`), and no chunk has been advanced yet.
    configureEngine(engine, stateA, stateB, SpectralMorphEngine::kMaxTravelRate);
    engine.setSeed(seed);
    engine.setEntropy(entropy);
    engine.setTargetPosition(moving ? 1.0f : kTravelFrozenPosition);

    HarmonicCloud cloud;
    configureRenderCloud(cloud, 0.0f, 0.0f);
    cloud.noteOn();  // NEVER noteOff(): a quiescent cloud zero-fills

    ComposedRender preRoll;
    preRoll.left.reserve(kTravelPreRollSamples);
    preRoll.right.reserve(kTravelPreRollSamples);
    driveSlices(engine, cloud, kTravelPreRollSamples, preRoll);
    if (!moving) {
        // EXACT, and it can be: the limiter assigns `position_ = target` outright
        // once the remaining delta falls inside one chunk's cap
        // (`spectral_morph_engine.h:696-698`), and p = 0.5 is reached after 0.5 s
        // — half the pre-roll, so the snap has 375 chunks of slack.
        REQUIRE(engine.getTravelPosition() == Approx(kTravelFrozenPosition).margin(1.0e-6));
    } else {
        // NOT exact, deliberately. A full 0 -> 1 journey takes the WHOLE 1 s
        // pre-roll, so whether the final snap lands on the last chunk or one
        // chunk later depends on float accumulation over 750 additions of
        // `cap = 64/48000` — a residual of one cap, 1.3e-3. The moving arm only
        // has to BE TRAVELLING; nothing downstream reads the position.
        constexpr float kOneChunkOfTravel = static_cast<float>(kSliceSamples) / kSampleRateF;
        REQUIRE(engine.getTravelPosition() >= 1.0f - (2.0f * kOneChunkOfTravel));
    }

    ComposedRender out;
    out.left.reserve(kTravelRenderSamples);
    out.right.reserve(kTravelRenderSamples);

    const std::size_t legs = kTravelRenderSamples / kTravelLegSamples;
    bool towardZero = true;  // the pre-roll left the moving arm at p = 1
    for (std::size_t leg = 0; leg < legs; ++leg) {
        if (moving) {
            engine.setTargetPosition(towardZero ? 0.0f : 1.0f);
            towardZero = !towardZero;
        }
        driveSlices(engine, cloud, kTravelLegSamples, out);
    }
    REQUIRE(out.left.size() == kTravelRenderSamples);
    REQUIRE(out.right.size() == kTravelRenderSamples);

    if (!moving) {
        // The control really did stay put for the whole 20 s.
        REQUIRE(engine.getTravelPosition() == Approx(kTravelFrozenPosition).margin(1.0e-6));
    }
    return out;
}

struct ClickCounts {
    std::size_t left = 0;
    std::size_t right = 0;
};

[[nodiscard]] ClickCounts countClicks(const ComposedRender& render) {
    return ClickCounts{.left = detectClicks(render.left).size(),
                       .right = detectClicks(render.right).size()};
}

[[nodiscard]] double medianOf(std::vector<double> values) {
    REQUIRE_FALSE(values.empty());
    std::sort(values.begin(), values.end());
    const std::size_t n = values.size();
    return ((n % 2) == 1) ? values[n / 2] : 0.5 * (values[(n / 2) - 1] + values[n / 2]);
}

[[nodiscard]] double clickBound(double frozen) noexcept {
    return kTravelClickSlope * frozen + kTravelClickOffset;
}

}  // namespace

// =============================================================================
// SC-001 clause 2 — travel produces no rendered discontinuity (T021)
// =============================================================================
TEST_CASE("SpectralMorph_TravelIsContinuous_Rendered", "[spectral_morph][seraphis]") {
    for (const float entropy : kTravelEntropyArms) {
        std::vector<double> movingLeft;
        std::vector<double> movingRight;
        std::vector<double> frozenLeft;
        std::vector<double> frozenRight;

        for (std::size_t s = 0; s < kTravelSeedCount; ++s) {
            const std::uint32_t seed = kSeeds[s];
            const ComposedRender moving = renderTravelArm(seed, entropy, true);
            const ComposedRender frozen = renderTravelArm(seed, entropy, false);

            // -----------------------------------------------------------------
            // MANDATORY POSITIVE CONTROL, AND IT COMES FIRST.
            // -----------------------------------------------------------------
            // Without it this clause cannot distinguish "travel is continuous"
            // from "the detector is not wired up": a `detect()` that returned an
            // empty vector unconditionally would satisfy every differential
            // assertion below, and would satisfy them with room to spare.
            if (s == 0) {
                std::vector<float> injected = moving.left;
                const std::size_t injectionIndex = kTravelRenderSamples / 2;
                for (std::size_t i = injectionIndex; i < injected.size(); ++i) {
                    injected[i] += kInjectedStepAmplitude;
                }
                const std::size_t injectedNear = countDetectionsNear(
                    detectClicks(injected), injectionIndex, kTightHalfWidth);
                INFO("positive control at entropy " << entropy
                                                    << ": detections at the injected step = "
                                                    << injectedNear);
                REQUIRE(injectedNear > 0);
            }

            const ClickCounts movingCounts = countClicks(moving);
            const ClickCounts frozenCounts = countClicks(frozen);

            // REPORTED UNCONDITIONALLY: these eight numbers per entropy arm ARE
            // the measurement tasks.md:1675-1678 asks the pass rule to be
            // re-derived from.
            WARN("SC-001 cl.2 seed " << seed << ", entropy " << entropy << ": moving L "
                                     << movingCounts.left << " / R " << movingCounts.right
                                     << ", frozen L " << frozenCounts.left << " / R "
                                     << frozenCounts.right << " (bound L "
                                     << clickBound(static_cast<double>(frozenCounts.left))
                                     << ", bound R "
                                     << clickBound(static_cast<double>(frozenCounts.right)) << ")");

            {
                INFO("per-pair rule, LEFT: seed " << seed << ", entropy " << entropy << ": moving "
                                                  << movingCounts.left << " vs frozen "
                                                  << frozenCounts.left);
                REQUIRE(static_cast<double>(movingCounts.left)
                        <= clickBound(static_cast<double>(frozenCounts.left)));
            }
            {
                INFO("per-pair rule, RIGHT: seed " << seed << ", entropy " << entropy << ": moving "
                                                   << movingCounts.right << " vs frozen "
                                                   << frozenCounts.right);
                REQUIRE(static_cast<double>(movingCounts.right)
                        <= clickBound(static_cast<double>(frozenCounts.right)));
            }

            movingLeft.push_back(static_cast<double>(movingCounts.left));
            movingRight.push_back(static_cast<double>(movingCounts.right));
            frozenLeft.push_back(static_cast<double>(frozenCounts.left));
            frozenRight.push_back(static_cast<double>(frozenCounts.right));
        }

        const double medianMovingLeft = medianOf(movingLeft);
        const double medianMovingRight = medianOf(movingRight);
        const double medianFrozenLeft = medianOf(frozenLeft);
        const double medianFrozenRight = medianOf(frozenRight);

        WARN("SC-001 cl.2 MEDIANS over " << kTravelSeedCount << " seeds at entropy " << entropy
                                         << ": moving L " << medianMovingLeft << " / R "
                                         << medianMovingRight << ", frozen L " << medianFrozenLeft
                                         << " / R " << medianFrozenRight << ", bounds L "
                                         << clickBound(medianFrozenLeft) << " / R "
                                         << clickBound(medianFrozenRight));

        {
            INFO("median rule, LEFT at entropy " << entropy << ": " << medianMovingLeft << " vs "
                                                 << clickBound(medianFrozenLeft));
            REQUIRE(medianMovingLeft <= clickBound(medianFrozenLeft));
        }
        {
            INFO("median rule, RIGHT at entropy " << entropy << ": " << medianMovingRight << " vs "
                                                  << clickBound(medianFrozenRight));
            REQUIRE(medianMovingRight <= clickBound(medianFrozenRight));
        }
    }
}

namespace {

// -----------------------------------------------------------------------------
// SC-004 metrics 3 and 4 — the shared entropy sweep
// -----------------------------------------------------------------------------
// THE SIGNAL IS PINNED, AND THE PIN IS THE WHOLE POINT: numStates = 2 with
// SINESTACK IN BOTH SLOTS, External, travel frozen at p = 0, bloom = 0. Under
// that configuration the CLEAN spectrum is position-independent and
// bloom-independent — every slot interpolates SineStack against SineStack — so
// the only thing that can move either metric is the entropy processor itself.
// Any other pair would let the travel and the bloom stagger contribute to the
// flatness and the criterion would stop being about entropy.

constexpr std::size_t kEntropyRenderSamples = 480000;   ///< 10.0 s at 48 kHz
constexpr std::size_t kEntropySettleSamples = 24000;    ///< first 0.5 s DISCARDED
constexpr std::size_t kFlatnessWindows = 6;             ///< >= 6, non-overlapping

static_assert(kEntropyRenderSamples % kSliceSamples == 0);
static_assert(kEntropySettleSamples % kSliceSamples == 0);
static_assert(kEntropySettleSamples + (kFlatnessWindows * kFftSize) <= kEntropyRenderSamples);

/// Flatness bin range, fixed and NEVER signal-dependent (see the sweep below).
constexpr std::size_t kFlatnessLoBin = 2;
constexpr std::size_t kFlatnessHiBin = 16384;
static_assert(kFlatnessHiBin <= (kFftSize / 2) + 1);

/// Guards `log(0)`. A 65536-point transform of a real render has no exactly-zero
/// bin, so this floor is unreachable in practice; it exists so that a pathological
/// all-zero render produces a finite number the assertions can red on, instead of
/// a NaN that compares false against everything.
constexpr double kFlatnessMagnitudeFloor = 1.0e-30;

/// The >= 11 entropy settings. SC-004's header requires 0, 1 AND EVERY FR-071
/// INTERVAL ENDPOINT — 0.25, 0.35, 0.50, 0.60, 0.75, 0.85 — on the grid, for all
/// four metrics and not only for metrics 1–2. This array is therefore the SAME
/// grid metrics 1–2 sweep (`kSc004Entropies`,
/// `dsp/tests/unit/systems/spectral_morph_engine_test.cpp:1969-1970`), transcribed
/// rather than shared because that array lives in another TU. Keeping the two
/// identical is what makes "SC-004 measured on one grid" true; an earlier draft
/// used a round 0.1-spaced grid here and silently omitted 0.25, 0.35 and 0.85,
/// i.e. three of the six endpoints where a stage engages or saturates.
///
/// The static_asserts below pin every endpoint, so dropping one is a compile
/// error rather than a quiet coverage loss.
constexpr std::array<float, 11> kEntropySettings{0.00f, 0.10f, 0.15f, 0.25f, 0.35f, 0.45f,
                                                 0.50f, 0.60f, 0.75f, 0.85f, 1.00f};
constexpr std::size_t kEntropyIndexZero = 0;
constexpr std::size_t kEntropyIndexGate = 8;
constexpr std::size_t kEntropyIndexOne = 10;
static_assert(kEntropySettings.size() >= 11);
static_assert(kEntropySettings[kEntropyIndexZero] == 0.0f);
static_assert(kEntropySettings[kEntropyIndexGate] == 0.75f);
static_assert(kEntropySettings[kEntropyIndexOne] == 1.0f);

/// Every FR-071 interval endpoint is on the grid (SC-004's header, verbatim).
[[nodiscard]] constexpr bool gridContains(float e) noexcept {
    for (const float v : kEntropySettings) {
        if (v == e) {
            return true;
        }
    }
    return false;
}
static_assert(gridContains(EntropyProcessor::kStage2Lo));  // 0.25
static_assert(gridContains(EntropyProcessor::kStage1Hi));  // 0.35
static_assert(gridContains(EntropyProcessor::kStage3Lo));  // 0.50
static_assert(gridContains(EntropyProcessor::kStage2Hi));  // 0.60
static_assert(gridContains(EntropyProcessor::kStage4Lo));  // 0.75
static_assert(gridContains(EntropyProcessor::kStage3Hi));  // 0.85

/// SC-004 m.3's gate. A FIRST MEASUREMENT BELOW 1.25 IS A FINDING ABOUT THE
/// FR-072 CENT CONSTANTS BEING TOO SMALL (`kMaxAmpJitter`,
/// `kMaxDecoherenceCents`, `kMaxScatterCents`), to be answered by raising them
/// inside FR-074's 12-cent budget — which then requires re-running T014's
/// static_asserts and T012's SC-016 derivation. IT IS NEVER ANSWERED BY LOWERING
/// THIS RATIO.
constexpr double kFlatnessRiseRatio = 1.25;

/// SC-004 m.4's gate, in dB. A spread above this is a finding about the
/// COMPOSITION — the entropy stages are not level-neutral — not a threshold to
/// widen.
constexpr double kLevelSpreadBoundDb = 3.0;

struct EntropyMeasurement {
    double flatness = 0.0;    ///< 0 when the caller did not ask for it
    double meanSquare = 0.0;  ///< broadband stereo, after the 0.5 s discard
};

// -----------------------------------------------------------------------------
// WHY THE FLATNESS IS COMPUTED INLINE — THE TWO-FUNCTION SWEEP (plan §0.1 item 4)
// -----------------------------------------------------------------------------
// There are exactly two spectral-flatness functions in this repo and NEITHER is
// usable for this row:
//
//   1. `Krate::DSP::TestUtils::SignalMetrics::calculateSpectralFlatness`
//      (`tests/test_helpers/signal_metrics.h:326`) is TIME-DOMAIN: it applies its
//      own Hann window and CAPS THE FFT AT 4096 (`:337`). This row needs a
//      65536-point transform — at 4096 points the bin spacing is 11.7 Hz against
//      a 110 Hz partial spacing, and the +-7-cent scatter this metric is supposed
//      to see (0.45 Hz at partial 1) is far inside one bin.
//
//   2. `Krate::DSP::calculateSpectralFlatness`
//      (`dsp/include/krate/dsp/primitives/spectral_utils.h:335`) is
//      magnitude-domain with no FFT-size cap — the right shape — but it SKIPS
//      BINS <= 1e-10f AND DIVIDES THE ARITHMETIC MEAN BY `validBins` RATHER THAN
//      `numBins` (`:345-357`). On a near-silent high-bin region its denominator
//      therefore SHRINKS WITH THE SIGNAL, so the number it returns is not
//      comparable across entropy settings — which is the one and only comparison
//      this row makes.
//
// So the metric is computed here, over a FIXED bin range [2, 16384) with NO
// skipping: `flatness = exp(mean_k log m_k) / mean_k m_k`.
//
// NOTE (plan §0.1 item 4, and the header block at the top of this TU): this TU
// includes NEITHER of those two headers, so no qualification hazard is created.
[[nodiscard]] double measureFlatness(const std::vector<float>& mono) {
    std::vector<float> window(kFftSize, 0.0f);
    Window::generateBlackmanHarris(window.data(), kFftSize);

    FFT fft;
    fft.prepare(kFftSize);
    REQUIRE(fft.isPrepared());

    std::vector<Complex> spectrum(fft.numBins());
    std::vector<float> frame(kFftSize, 0.0f);
    std::vector<double> magnitudeSum(fft.numBins(), 0.0);

    // >= 6 NON-OVERLAPPING windows, bin-wise magnitude average. Averaging
    // MAGNITUDES (not one long transform) is what makes the number a statement
    // about the sustained spectrum rather than about one 1.365 s slice of the
    // OU walks.
    for (std::size_t w = 0; w < kFlatnessWindows; ++w) {
        const std::size_t start = kEntropySettleSamples + (w * kFftSize);
        REQUIRE(start + kFftSize <= mono.size());
        for (std::size_t i = 0; i < kFftSize; ++i) {
            frame[i] = mono[start + i] * window[i];
        }
        fft.forward(frame.data(), spectrum.data());
        for (std::size_t b = 0; b < spectrum.size(); ++b) {
            magnitudeSum[b] += static_cast<double>(spectrum[b].magnitude());
        }
    }

    REQUIRE(kFlatnessHiBin <= magnitudeSum.size());
    const double invWindows = 1.0 / static_cast<double>(kFlatnessWindows);
    double logSum = 0.0;
    double linearSum = 0.0;
    for (std::size_t b = kFlatnessLoBin; b < kFlatnessHiBin; ++b) {
        const double m = magnitudeSum[b] * invWindows;
        linearSum += m;
        logSum += std::log(std::max(m, kFlatnessMagnitudeFloor));
    }
    const double bins = static_cast<double>(kFlatnessHiBin - kFlatnessLoBin);
    const double arithmeticMean = linearSum / bins;
    REQUIRE(arithmeticMean > 0.0);
    return std::exp(logSum / bins) / arithmeticMean;
}

/// @brief ONE (seed, entropy) render of the SC-004 m.3 / m.4 basis.
/// @param computeFlatness `false` skips only the ANALYSIS. The render itself is
///        produced by the identical call sequence either way, which is what lets
///        SC-004 m.4 claim it measures "exactly the m.3 renders" without paying
///        for six 65536-point transforms it has no use for.
[[nodiscard]] EntropyMeasurement renderEntropyCase(std::uint32_t seed, float entropy,
                                                   bool computeFlatness) {
    const SpectralState sineStack = makeFactoryState(SpectralStateId::SineStack);

    SpectralMorphEngine engine;
    // SineStack in BOTH slots. See configureEngine's own comment for why the
    // slots are loaded before prepare(); the REQUIRE_FALSE(isStateFadeActive())
    // inside it is the guard that no FR-047 absorption fade reaches the render.
    configureEngine(engine, sineStack, sineStack, SpectralMorphEngine::kMaxTravelRate);
    engine.setSeed(seed);
    engine.setBloom(0.0f);
    engine.setEntropy(entropy);
    engine.setTargetPosition(0.0f);  // travel frozen at p = 0

    HarmonicCloud cloud;
    configureRenderCloud(cloud, 0.0f, 0.0f);  // T020's cloud pin
    cloud.noteOn();                           // NEVER noteOff()

    ComposedRender out;
    out.left.reserve(kEntropyRenderSamples);
    out.right.reserve(kEntropyRenderSamples);
    driveSlices(engine, cloud, kEntropyRenderSamples, out);
    REQUIRE(out.left.size() == kEntropyRenderSamples);
    // The travel really was frozen for the whole render: anything else and both
    // metrics would be measuring the journey instead of the entropy.
    REQUIRE(engine.getTravelPosition() == Approx(0.0f).margin(1.0e-6));

    EntropyMeasurement result;

    // Broadband stereo mean square, first 0.5 s discarded so the 20 ms
    // kNormGainSmoothMs smoother has settled (SC-004 m.4).
    double sum = 0.0;
    for (std::size_t i = kEntropySettleSamples; i < kEntropyRenderSamples; ++i) {
        const double l = static_cast<double>(out.left[i]);
        const double r = static_cast<double>(out.right[i]);
        sum += (l * l) + (r * r);
    }
    const double n = 2.0 * static_cast<double>(kEntropyRenderSamples - kEntropySettleSamples);
    result.meanSquare = sum / n;

    if (computeFlatness) {
        finalizeRender(out);  // builds the mono sum the transform runs on
        result.flatness = measureFlatness(out.mono);
    }
    return result;
}

/// @brief Average one metric over the eight pinned seeds at one entropy setting.
struct SweepPoint {
    double flatness = 0.0;
    double levelDbfs = 0.0;
};

[[nodiscard]] SweepPoint sweepAtEntropy(float entropy, bool computeFlatness) {
    double flatnessSum = 0.0;
    double meanSquareSum = 0.0;
    for (const std::uint32_t seed : kSeeds) {
        const EntropyMeasurement m = renderEntropyCase(seed, entropy, computeFlatness);
        flatnessSum += m.flatness;
        meanSquareSum += m.meanSquare;
    }
    const double seeds = static_cast<double>(kSeeds.size());
    const double meanSquare = meanSquareSum / seeds;
    SweepPoint point;
    point.flatness = flatnessSum / seeds;
    // RMS in dBFS is 10*log10(mean square) — the sqrt and the 20 cancel.
    point.levelDbfs = (meanSquare > 0.0) ? 10.0 * std::log10(meanSquare) : -600.0;
    return point;
}

}  // namespace

// =============================================================================
// SC-004 metric 3 — spectral flatness rises with entropy (T021)
// =============================================================================
TEST_CASE("EntropyProcessor_FlatnessRisesWithEntropy", "[spectral_morph][seraphis]") {
    std::array<double, kEntropySettings.size()> flatness{};

    for (std::size_t i = 0; i < kEntropySettings.size(); ++i) {
        const SweepPoint point = sweepAtEntropy(kEntropySettings[i], true);
        flatness[i] = point.flatness;
        WARN("SC-004 m.3 entropy " << kEntropySettings[i] << ": flatness " << flatness[i]
                                   << " (mean over " << kSeeds.size() << " seeds)");
        INFO("entropy " << kEntropySettings[i] << " flatness " << flatness[i]);
        // Flatness is a ratio of two positive means over a fixed bin range, so
        // it is in (0, 1] by construction. A value outside that says the
        // measurement itself is broken and every comparison below is meaningless.
        REQUIRE(flatness[i] > 0.0);
        REQUIRE(flatness[i] <= 1.0);
    }

    // -------------------------------------------------------------------------
    // THE GATE, ENFORCED OVER [0, 0.75] ONLY.
    // -------------------------------------------------------------------------
    // Stage 4 (FR-073 death / rebirth) ramps in over [0.75, 1] and REMOVES
    // partials, which can flatten or sharpen the average spectrum depending on
    // which slots die — so the criterion deliberately makes no monotonicity claim
    // up there. What it does require is that the top of the range never falls
    // BELOW the bottom.
    {
        INFO("flatness(0) = " << flatness[kEntropyIndexZero] << ", flatness(0.75) = "
                              << flatness[kEntropyIndexGate] << ", required >= "
                              << (kFlatnessRiseRatio * flatness[kEntropyIndexZero]));
        REQUIRE(flatness[kEntropyIndexGate]
                >= kFlatnessRiseRatio * flatness[kEntropyIndexZero]);
    }
    {
        INFO("flatness(1) = " << flatness[kEntropyIndexOne] << ", flatness(0) = "
                              << flatness[kEntropyIndexZero]);
        REQUIRE(flatness[kEntropyIndexOne] >= flatness[kEntropyIndexZero]);
    }
}

// =============================================================================
// SC-004 metric 4 — entropy is level-neutral (T021)
// =============================================================================
// EXACTLY THE m.3 RENDERS: same states, same cloud pin, same seeds, same 10 s,
// same 0.5 s discard. Only the analysis differs.
TEST_CASE("EntropyProcessor_IsLevelNeutral", "[spectral_morph][seraphis]") {
    std::array<double, kEntropySettings.size()> levels{};

    for (std::size_t i = 0; i < kEntropySettings.size(); ++i) {
        const SweepPoint point = sweepAtEntropy(kEntropySettings[i], false);
        levels[i] = point.levelDbfs;
        // EVERY VALUE REPORTED (SC-004 m.4).
        WARN("SC-004 m.4 entropy " << kEntropySettings[i] << ": broadband stereo RMS "
                                   << levels[i] << " dBFS (mean over " << kSeeds.size()
                                   << " seeds, first 0.5 s discarded)");
    }

    double minDb = levels[0];
    double maxDb = levels[0];
    std::size_t minIndex = 0;
    std::size_t maxIndex = 0;
    for (std::size_t i = 1; i < levels.size(); ++i) {
        if (levels[i] < minDb) {
            minDb = levels[i];
            minIndex = i;
        }
        if (levels[i] > maxDb) {
            maxDb = levels[i];
            maxIndex = i;
        }
    }
    const double spread = maxDb - minDb;

    WARN("SC-004 m.4 SPREAD: " << spread << " dB (max " << maxDb << " dBFS at entropy "
                               << kEntropySettings[maxIndex] << ", min " << minDb
                               << " dBFS at entropy " << kEntropySettings[minIndex] << ", bound "
                               << kLevelSpreadBoundDb << " dB)");

    // Non-vacuity: a silent render would have a spread of 0 and would sail
    // through the gate below.
    INFO("quietest setting " << kEntropySettings[minIndex] << " at " << minDb << " dBFS");
    REQUIRE(minDb > kRenderRmsFloorDbfs);

    INFO("level spread " << spread << " dB against the " << kLevelSpreadBoundDb << " dB bound");
    REQUIRE(spread <= kLevelSpreadBoundDb);
}

namespace {

// -----------------------------------------------------------------------------
// SC-008 clause 3 — the metadata fields cannot reach the audio path
// -----------------------------------------------------------------------------
// STRUCTURALLY GUARANTEED BY DEVIATION D10: `setState` stores the slot SANITIZED
// — only the FR-041-filled log2(ratio) array, the zero-padded amplitude array and
// the count (`spectral_morph_engine.h:284-308`). `tiltDbPerOct`, `inharmonicity`
// and `name` are never copied anywhere. It is asserted anyway because FR-013
// otherwise has NO criterion that would fail if a future change started reading
// them, and because "structurally impossible" is a claim about today's code.
constexpr std::size_t kMetadataRenderSamples = 96000;  ///< 2.0 s at 48 kHz
static_assert(kMetadataRenderSamples % kSliceSamples == 0);

/// @brief Overwrite every metadata field with a value that is legal (so the
/// state still passes `isValidSpectralState` and is actually accepted) and as far
/// from the factory value as FR-012 allows.
[[nodiscard]] SpectralState withOverwrittenMetadata(SpectralState s) {
    s.tiltDbPerOct = SpectralState::kMaxStateTiltDbPerOct;    // +12 dB/oct
    s.inharmonicity = SpectralState::kMaxStateInharmonicity;  // 0.1
    s.name.fill('\0');
    constexpr char kLabel[] = "OVERWRITTEN";
    for (std::size_t i = 0; (i + 1) < SpectralState::kStateNameBytes && kLabel[i] != '\0'; ++i) {
        s.name[i] = kLabel[i];
    }
    return s;
}

[[nodiscard]] ComposedRender renderMetadataArm(const SpectralState& a, const SpectralState& b) {
    SpectralMorphEngine engine;
    configureEngine(engine, a, b, kTravelRateFast);
    // Travel, so the FR-041 interpolation runs on every chunk rather than
    // re-emitting one frozen slot: if a metadata field ever leaked, the most
    // likely place is the interpolation, not the load.
    engine.setTargetPosition(1.0f);

    HarmonicCloud cloud;
    configureRenderCloud(cloud, 0.0f, 0.0f);
    cloud.noteOn();  // NEVER noteOff()

    ComposedRender out;
    out.left.reserve(kMetadataRenderSamples);
    out.right.reserve(kMetadataRenderSamples);
    driveSlices(engine, cloud, kMetadataRenderSamples, out);
    REQUIRE(out.left.size() == kMetadataRenderSamples);
    return out;
}

/// @brief Index of the first bitwise difference, or `size` when there is none.
/// `std::memcmp` and not `==`: `-0.0f == 0.0f` is true while the bit patterns
/// differ, and this criterion says BITWISE IDENTICAL.
[[nodiscard]] std::size_t firstBitwiseDifference(const std::vector<float>& a,
                                                 const std::vector<float>& b) {
    REQUIRE(a.size() == b.size());
    for (std::size_t i = 0; i < a.size(); ++i) {
        // NOLINTNEXTLINE(bugprone-suspicious-memory-comparison) - intentional bit-exact check
        if (std::memcmp(&a[i], &b[i], sizeof(float)) != 0) {
            return i;
        }
    }
    return a.size();
}

}  // namespace

// =============================================================================
// SC-008 clause 3 — a state's metadata never reaches the audio (T021)
// =============================================================================
TEST_CASE("SpectralState_MetadataNeverReachesAudio", "[spectral_morph][seraphis]") {
    const SpectralState pristineA = makeFactoryState(SpectralStateId::SineStack);
    const SpectralState pristineB = makeFactoryState(SpectralStateId::Bell);
    const SpectralState taggedA = withOverwrittenMetadata(pristineA);
    const SpectralState taggedB = withOverwrittenMetadata(pristineB);

    // The mutated states must still be ACCEPTED — `setState` returns writing
    // nothing on an invalid state (`spectral_morph_engine.h:295-297`), which
    // would leave the second engine holding the constructor default and make the
    // renders differ for entirely the wrong reason.
    REQUIRE(isValidSpectralState(taggedA));
    REQUIRE(isValidSpectralState(taggedB));

    // The metadata really is different — otherwise this test asserts nothing.
    REQUIRE(taggedA.tiltDbPerOct != pristineA.tiltDbPerOct);
    REQUIRE(taggedA.inharmonicity != pristineA.inharmonicity);
    REQUIRE(taggedA.name != pristineA.name);
    REQUIRE(taggedB.tiltDbPerOct != pristineB.tiltDbPerOct);
    REQUIRE(taggedB.inharmonicity != pristineB.inharmonicity);
    REQUIRE(taggedB.name != pristineB.name);
    // ...and the part that IS allowed to reach the audio is untouched, so a
    // difference downstream can only come from the metadata.
    REQUIRE(taggedA.ratios == pristineA.ratios);
    REQUIRE(taggedA.amplitudes == pristineA.amplitudes);
    REQUIRE(taggedA.numPartials == pristineA.numPartials);
    REQUIRE(taggedB.ratios == pristineB.ratios);
    REQUIRE(taggedB.amplitudes == pristineB.amplitudes);
    REQUIRE(taggedB.numPartials == pristineB.numPartials);

    const ComposedRender pristine = renderMetadataArm(pristineA, pristineB);
    const ComposedRender tagged = renderMetadataArm(taggedA, taggedB);

    const std::size_t leftDiff = firstBitwiseDifference(pristine.left, tagged.left);
    const std::size_t rightDiff = firstBitwiseDifference(pristine.right, tagged.right);

    INFO("first bitwise difference: left at " << leftDiff << ", right at " << rightDiff
                                              << " (render length " << kMetadataRenderSamples
                                              << ")");
    REQUIRE(leftDiff == kMetadataRenderSamples);
    REQUIRE(rightDiff == kMetadataRenderSamples);

    // Non-vacuity: the renders compared above are not two buffers of silence.
    const RenderStats stats = analyseRender(pristine.left, pristine.right);
    INFO("metadata render peak " << stats.peak << ", worst 100 ms window RMS "
                                 << stats.worstWindowRmsDb << " dBFS");
    REQUIRE(stats.allFinite);
    REQUIRE(stats.peak > 0.0f);
    REQUIRE(stats.worstWindowRmsDb >= kRenderRmsFloorDbfs);
}
