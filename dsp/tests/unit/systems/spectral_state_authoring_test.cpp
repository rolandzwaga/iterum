// ==============================================================================
// Layer 3: System Tests - Seraphis Phase 11, T005
// spectral_state_authoring_test.cpp
// ==============================================================================
// Constitution Principle XII: Test-First Development.
//
// Reference: specs/seraphis-phase11-ui/spec.md   (SC-012 acceptance arm, SC-013)
//            specs/seraphis-phase11-ui/plan.md   (section 1)
//            specs/seraphis-phase11-ui/tasks.md  (T005)
//
// WHY THIS TU AND NOT unit/processors/spectral_state_test.cpp (tasks.md T005):
// that file is registered to `dsp_processors_tests` and cannot host a Layer-3
// render. Every arm below needs SpectralMorphEngine or HarmonicCloud, i.e.
// Layer 3, so the cases live here in `dsp_systems_tests`.
//
// T002 owns the three authoring mutators themselves
// (`dsp/include/krate/dsp/processors/spectral_state.h:533`, `:608`, `:711`) and
// their preservation table. This TU owns only the two claims T002 cannot make
// from Layer 2:
//   * SC-012's ACCEPTANCE arm  - an authored, valid state is accepted by
//     SpectralMorphEngine::setState (`spectral_morph_engine.h:299`) rather than
//     rejected wholesale by its `!isValidSpectralState` gate at `:303-305`;
//   * SC-013's four AUDIBILITY arms - setPartial's ratio, blendStates'
//     endpoints, tiltState's monotonicity/absoluteness and setPartial's
//     amplitude, all measured on a real HarmonicCloud render.
//
// This task ships NO production code. If an arm below cannot be made green
// without changing the mutators, that is a T002 defect and is fixed there.
//
// ------------------------------------------------------------------------------
// This TU deliberately does NOT include `allocation_operator_overrides.h`
// ------------------------------------------------------------------------------
// That header supplies the global operator new/delete replacements and must
// appear in EXACTLY ONE translation unit per binary; `dsp_systems_tests` already
// gets it from `unit/systems/selectable_oscillator_test.cpp`. A second inclusion
// here is a duplicate-symbol link error.
//
// ------------------------------------------------------------------------------
// NAMESPACE HAZARD - do not add signal_metrics.h or spectral_utils.h here
// ------------------------------------------------------------------------------
// There are TWO `calculateSpectralFlatness` overloads in this repo
// (`tests/test_helpers/signal_metrics.h:326` and
// `dsp/include/krate/dsp/primitives/spectral_utils.h:335`). This TU includes
// neither, and computes its own centroid, so no call here needs qualifying.
//
// ------------------------------------------------------------------------------
// No bit-exact float goldens (dsp/CLAUDE.md)
// ------------------------------------------------------------------------------
// Section (b)'s and (c)'s render comparisons go through `render_fingerprint.h`
// at its MEASURED tolerances (kSampleTolerance = 1e-4f, kMetricTolerance = 1e-5).
// Every other number below is a measured spectral quantity with a stated bound,
// never a digest over sample bits.
//
// ------------------------------------------------------------------------------
// Non-finite arguments are built from BIT PATTERNS, never std::numeric_limits
// ------------------------------------------------------------------------------
// The macOS leg builds -ffast-math, under which quiet_NaN()/infinity() fold to
// finite garbage. `nanFromBits()` / `infFromBits()` below launder the pattern
// through a `volatile` sink, which is the sanctioned form. This TU is NOT in
// dsp/tests/CMakeLists.txt's -fno-fast-math block and must not be added to it:
// the mutators' own guards are `detail::isNaN`/`detail::isInf` bit-pattern
// checks (`spectral_state.h:556`), and proving them in the FP mode the header
// actually ships in is the point.
// ==============================================================================

#include <krate/dsp/systems/harmonic_cloud.h>
#include <krate/dsp/systems/spectral_morph_engine.h>
#include <krate/dsp/processors/spectral_state.h>
#include <krate/dsp/primitives/fft.h>
#include <krate/dsp/core/window_functions.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "render_fingerprint.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <vector>

using Catch::Approx;
using namespace Krate::DSP;

namespace {

// =============================================================================
// The render basis
// =============================================================================

constexpr double kSampleRate = 48000.0;
constexpr float kSampleRateF = 48000.0f;

/// FR-086's bound, and the cadence every render below drives at.
constexpr std::size_t kSliceSamples = HarmonicCloud::kControlChunkSamples; // 64
static_assert(kSliceSamples == 64);

/// tasks.md T005 pins the transform at 4096 points.
constexpr std::size_t kFftSize = 4096;
constexpr float kBinHz = kSampleRateF / static_cast<float>(kFftSize); // 11.71875

/// THE FUNDAMENTAL IS NOT A TASTE CHOICE. 187.5 Hz is exactly 16 analysis bins,
/// so EVERY harmonic n*f0 lands exactly ON a bin centre and the perfect fifth
/// section (a) authors (1.5 * f0 = 281.25 Hz) lands exactly on bin 24. That
/// removes the QIFFT interpolation bias entirely: with a symmetric main lobe the
/// parabolic offset is 0, and section (a)'s 5-cent and 2-cent bounds are then
/// measuring the mutator rather than the estimator. At an arbitrary f0 the
/// Blackman-Harris QIFFT bias alone is a few hundredths of a bin, i.e. several
/// cents at the fundamental, and the 5-cent bound would be estimator noise.
constexpr float kF0Hz = 16.0f * kBinHz;
static_assert(kF0Hz == 187.5f);
static_assert(kF0Hz >= HarmonicCloud::kMinFundamentalHz
              && kF0Hz <= HarmonicCloud::kMaxFundamentalHz);

/// The top partial sits at 64 * 187.5 = 12 kHz, well below the FR-015
/// anti-alias fade start (0.8 * Nyquist = 19.2 kHz), so no render below is
/// measuring a faded partial.
static_assert(64.0f * kF0Hz < HarmonicCloud::kAntiAliasFadeStart * 0.5f * kSampleRateF);

/// 0.5 s. Covers the 50 ms kMinAttackSec envelope, the 20 ms kNormGainSmoothMs
/// normalizer and the 2 ms FR-014 amplitude smoother many times over.
constexpr std::size_t kSettleSamples = 24000;
constexpr std::size_t kRenderSamples = kSettleSamples + kFftSize; // 28096
static_assert(kSettleSamples % kSliceSamples == 0);
static_assert(kFftSize % kSliceSamples == 0);

/// FR-016 draws each partial's initial MCF phase from this stream, so the
/// per-partial orbit gain is a function of it. Every render in this TU uses the
/// SAME seed and the SAME per-partial frequencies wherever two renders are
/// compared, which is what makes the orbit factor cancel exactly in a
/// render-to-render dB or cents difference rather than having to be modelled.
constexpr std::uint32_t kCloudSeed = 0x5E3A0014u;
constexpr std::uint32_t kMorphSeed = 1u;

/// 4-term Blackman-Harris main-lobe half-width in bins. Summing POWER over the
/// whole main lobe removes scalloping loss. Copied from the Phase 2/3 recipe
/// (`harmonic_cloud_spectral_test.cpp:53-59`).
constexpr int kLobeHalfWidth = 4;

/// Peak search half-width. Harmonics are 16 bins apart at this f0, so +-5 bins
/// can never capture a neighbour's lobe.
constexpr int kPeakSearchHalfWidth = 5;

constexpr float kSpectrumFloorDb = -300.0f;

// =============================================================================
// Non-finite arguments from bit patterns (see the banner)
// =============================================================================

[[nodiscard]] float floatFromBits(std::uint32_t bits) noexcept {
    volatile std::uint32_t sink = bits;
    const std::uint32_t laundered = sink;
    float value = 0.0f;
    std::memcpy(&value, &laundered, sizeof(value));
    return value;
}

[[nodiscard]] float nanFromBits() noexcept { return floatFromBits(0x7FC00000u); }
[[nodiscard]] float infFromBits() noexcept { return floatFromBits(0x7F800000u); }

/// Whole-state equality, MEMBER-WISE and never `memcmp` over the object
/// representation. A `float` has no unique object representation (-0.0f and
/// +0.0f compare equal but differ in bits; two NaN payloads compare unequal but
/// can encode the same value), so a byte compare asks a different question from
/// the one every call site here means. Every subject below is a state that
/// satisfies isValidSpectralState, which rejects non-finite outright, so the two
/// coincide in practice - this spelling is the one that stays correct if that
/// ever stops holding. std::array's operator== is element-wise, so the two
/// 64-entry arrays and the name need no loop.
[[nodiscard]] bool statesEqual(const SpectralState& a, const SpectralState& b) noexcept {
    return a.ratios == b.ratios && a.amplitudes == b.amplitudes && a.name == b.name
           && a.tiltDbPerOct == b.tiltDbPerOct && a.inharmonicity == b.inharmonicity
           && a.numPartials == b.numPartials;
}

// =============================================================================
// Spectral analysis
// =============================================================================

struct Spectrum {
    std::vector<float> magnitude;
};

[[nodiscard]] Spectrum analyseWindow(const std::vector<float>& mono, std::size_t start) {
    REQUIRE(start + kFftSize <= mono.size());

    std::vector<float> window(kFftSize, 0.0f);
    Window::generateBlackmanHarris(window.data(), kFftSize);

    std::vector<float> frame(kFftSize, 0.0f);
    for (std::size_t i = 0; i < kFftSize; ++i) {
        frame[i] = mono[start + i] * window[i];
    }

    FFT fft;
    fft.prepare(kFftSize);
    REQUIRE(fft.isPrepared());

    std::vector<Complex> bins(fft.numBins());
    fft.forward(frame.data(), bins.data());

    Spectrum spectrum;
    spectrum.magnitude.resize(bins.size());
    for (std::size_t b = 0; b < bins.size(); ++b) {
        spectrum.magnitude[b] = bins[b].magnitude();
    }
    return spectrum;
}

[[nodiscard]] float magnitudeDb(float magnitude) noexcept {
    return (magnitude > 1.0e-30f) ? 20.0f * std::log10(magnitude) : kSpectrumFloorDb;
}

/// @brief Peak frequency of the lobe nearest @p expectedHz, in Hz.
///
/// QIFFT: locate the largest bin inside a +-kPeakSearchHalfWidth window, then
/// interpolate the peak's sub-bin offset parabolically on the LOG magnitude
/// (Smith's form, p = 0.5*(a - c)/(a - 2b + c)). At this TU's f0 every unmoved
/// partial sits exactly on a bin, so p is ~0 and the estimate is unbiased.
[[nodiscard]] float peakFrequencyHz(const Spectrum& spectrum, float expectedHz) {
    const auto binCount = static_cast<int>(spectrum.magnitude.size());
    REQUIRE(binCount >= 3);

    const int centre = static_cast<int>(std::lround(expectedHz / kBinHz));
    const int lo = std::max(1, centre - kPeakSearchHalfWidth);
    const int hi = std::min(binCount - 2, centre + kPeakSearchHalfWidth);
    REQUIRE(lo <= hi);

    int best = lo;
    for (int b = lo; b <= hi; ++b) {
        if (spectrum.magnitude[static_cast<std::size_t>(b)]
            > spectrum.magnitude[static_cast<std::size_t>(best)]) {
            best = b;
        }
    }

    // Widened ONCE, then indexed - never `static_cast<std::size_t>(best + 1)`,
    // which does the arithmetic in `int` and widens the result. `lo >= 1` and
    // `hi <= binCount - 2` above are what make both neighbours in range.
    const auto bestIndex = static_cast<std::size_t>(best);
    const float a = magnitudeDb(spectrum.magnitude[bestIndex - 1u]);
    const float b = magnitudeDb(spectrum.magnitude[bestIndex]);
    const float c = magnitudeDb(spectrum.magnitude[bestIndex + 1u]);

    const float denominator = a - 2.0f * b + c;
    float offset = 0.0f;
    if (denominator != 0.0f) {
        offset = std::clamp(0.5f * (a - c) / denominator, -0.5f, 0.5f);
    }
    return (static_cast<float>(best) + offset) * kBinHz;
}

/// @brief Main-lobe POWER around @p expectedHz, in dB.
[[nodiscard]] double lobePowerDb(const Spectrum& spectrum, float expectedHz) {
    const auto binCount = static_cast<int>(spectrum.magnitude.size());
    const int centre = static_cast<int>(std::lround(expectedHz / kBinHz));
    const int lo = std::max(0, centre - kLobeHalfWidth);
    const int hi = std::min(binCount - 1, centre + kLobeHalfWidth);

    double power = 0.0;
    for (int b = lo; b <= hi; ++b) {
        const double m = static_cast<double>(spectrum.magnitude[static_cast<std::size_t>(b)]);
        power += m * m;
    }
    return (power > 1.0e-30) ? 10.0 * std::log10(power) : static_cast<double>(kSpectrumFloorDb);
}

/// @brief Power-weighted spectral centroid in Hz. Bin 0 (DC) is excluded.
[[nodiscard]] double spectralCentroidHz(const Spectrum& spectrum) {
    double numerator = 0.0;
    double denominator = 0.0;
    for (std::size_t b = 1; b < spectrum.magnitude.size(); ++b) {
        const double m = static_cast<double>(spectrum.magnitude[b]);
        const double power = m * m;
        numerator += power * static_cast<double>(b) * static_cast<double>(kBinHz);
        denominator += power;
    }
    return (denominator > 0.0) ? numerator / denominator : 0.0;
}

[[nodiscard]] float centsBetween(float fromHz, float toHz) noexcept {
    if (fromHz <= 0.0f || toHz <= 0.0f) {
        return 0.0f;
    }
    return 1200.0f * std::log2(toHz / fromHz);
}

// =============================================================================
// The rig: one state -> one render
// =============================================================================

/// @brief Load @p s into BOTH slots and park the journey at position 0.
///
/// ORDER IS LOAD-BEARING, exactly as Phase 3's own render harness records
/// (`spectral_morph_render_test.cpp`, `configureEngine`): setState arms the
/// FR-047 absorption fade whenever the slot contributes
/// (`spectral_morph_engine.h:318-320`), and that fade runs for
/// kStateChangeFadeSec = 2.0 s. `prepare()` calls `reset()`, which sets
/// `fadeX_ = 1.0f` (`:259`), so configuring the slots BEFORE prepare is what
/// makes the analysis window a measurement of @p s rather than of a fade. The
/// REQUIRE_FALSE below is the guard on that.
///
/// Both slots carry the same state, so the interpolation is the identity for
/// every travel position and the render is a pure function of @p s.
void configureEngineForState(SpectralMorphEngine& engine, const SpectralState& state) {
    engine.setSeed(kMorphSeed);
    engine.setState(0, state);
    engine.setState(1, state);
    engine.setStateCount(2);
    engine.prepare(kSampleRate);
    REQUIRE(engine.isPrepared());
    REQUIRE_FALSE(engine.isStateFadeActive());
    engine.setBloom(0.0f);
    engine.setEntropy(0.0f); // EntropyProcessor's exact-zero pass-through
    engine.setTravelMode(SpectralMorphEngine::TravelMode::External);
    engine.setTargetPosition(0.0f);
    REQUIRE(engine.getTravelPosition() == 0.0f);
}

/// @brief The pinned cloud configuration.
///
/// Every parametric axis is neutralised so the rendered spectrum IS the state:
/// gravity 0 makes the warp factor exactly 1, inharmonicity 0 makes stretch
/// exactly 1, tilt 0 makes tiltGain exactly 1, and richness 1 keeps all 64 slots
/// active. `setDriftDepthCents(0.0f)` is T005's stated precondition - it is what
/// stops BrownianDrift contributing any detune, which sections (a) and (d)
/// measure in cents.
void configureCloudForRender(HarmonicCloud& cloud) {
    cloud.setSeed(kCloudSeed);
    cloud.prepare(kSampleRate);
    cloud.setFundamentalHz(kF0Hz);
    cloud.setRichness(1.0f);
    cloud.setSpectralGravity(0.0f);
    cloud.setInharmonicity(0.0f);
    cloud.setSpectralTiltDb(0.0f);
    cloud.setMutation(0.0f);
    cloud.setDriftDepthCents(0.0f); // T005's precondition for every FFT arm
    cloud.setStereoSpread(0.0f);
    cloud.setAttackTimeSec(HarmonicCloud::kMinAttackSec);
    cloud.setDecayTimeSec(HarmonicCloud::kMinDecaySec);
    cloud.setEnvelopeOffsetSpread(0.0f);
}

struct StateRender {
    std::vector<float> mono;
    Spectrum spectrum;
    TestUtils::RenderFingerprint fingerprint{};
};

/// @brief Render @p state in the FR-086 shape and analyse the steady state.
[[nodiscard]] StateRender renderState(const SpectralState& state) {
    REQUIRE(isValidSpectralState(state));

    SpectralMorphEngine engine;
    configureEngineForState(engine, state);

    HarmonicCloud cloud;
    configureCloudForRender(cloud);
    cloud.noteOn(); // NEVER noteOff(): a quiescent cloud zero-fills

    StateRender out;
    out.mono.reserve(kRenderSamples);

    std::array<float, kSliceSamples> left{};
    std::array<float, kSliceSamples> right{};
    for (std::size_t done = 0; done < kRenderSamples; done += kSliceSamples) {
        const std::size_t n = std::min(kSliceSamples, kRenderSamples - done);
        engine.updateChunk(n);
        cloud.setSpectralTarget(engine.getOutputRatios(), engine.getOutputAmplitudes(),
                                engine.getOutputCount());
        cloud.processStereoBlock(left.data(), right.data(), n);
        for (std::size_t i = 0; i < n; ++i) {
            out.mono.push_back(0.5f * (left[i] + right[i]));
        }
    }
    REQUIRE(out.mono.size() == kRenderSamples);

    // Hygiene, so a silent or poisoned render fails HERE rather than as a
    // baffling cents/dB number downstream. Bit-pattern finiteness, never
    // std::isnan (-ffast-math folds it away). Scanned in PLAIN CODE with three
    // assertions at the end, never one REQUIRE per sample: this rig renders
    // sixteen buffers of 28 096 samples and a per-sample macro would emit ~900k
    // Catch2 assertions.
    bool allFinite = true;
    float peak = 0.0f;
    for (const float v : out.mono) {
        if (Krate::DSP::detail::isNaN(v) || Krate::DSP::detail::isInf(v)) {
            allFinite = false;
            continue; // |NaN| would poison the peak below
        }
        peak = std::max(peak, std::abs(v));
    }
    REQUIRE(allFinite);
    REQUIRE(peak > 1.0e-3f);
    REQUIRE(peak < HarmonicCloud::kOutputClamp);

    out.spectrum = analyseWindow(out.mono, kSettleSamples);
    out.fingerprint = TestUtils::fingerprintRender(std::span<const float>(out.mono));
    return out;
}

// =============================================================================
// SC-012 acceptance: the engine-only rig
// =============================================================================

/// Chunk length used to run the engine forward with no cloud attached. The
/// FR-086 <= 64-sample bound governs driving a HarmonicCloud, not the engine on
/// its own, so a long chunk here is legal and keeps the fade cheap to spend.
constexpr std::size_t kEngineChunkSamples = 2048;

/// kStateChangeFadeSec = 2.0 s at 48 kHz is 47 chunks of 2048. 96 is a full 2x.
constexpr int kFadeSpendChunks = 96;

struct EngineOutputs {
    std::array<float, SpectralState::kStatePartials> ratios{};
    std::array<float, SpectralState::kStatePartials> amplitudes{};
    std::size_t count = 0;
};

[[nodiscard]] EngineOutputs snapshotOutputs(const SpectralMorphEngine& engine) {
    EngineOutputs out;
    const float* ratios = engine.getOutputRatios();
    const float* amplitudes = engine.getOutputAmplitudes();
    for (std::size_t i = 0; i < SpectralState::kStatePartials; ++i) {
        out.ratios[i] = ratios[i];
        out.amplitudes[i] = amplitudes[i];
    }
    out.count = engine.getOutputCount();
    return out;
}

/// tasks.md T005: "park the journey on slot 0 ... updateChunk until
/// getTravelPosition() == 0". Always runs at least one chunk, so the output
/// arrays are the product of a real pipeline pass and not only of reset()'s
/// refreshOutputs().
void parkAtZero(SpectralMorphEngine& engine) {
    for (int i = 0; i < 256; ++i) {
        engine.updateChunk(kEngineChunkSamples);
        if (engine.getTravelPosition() == 0.0f) {
            break;
        }
    }
    REQUIRE(engine.getTravelPosition() == 0.0f);
}

/// Ratios run up to kMaxStateRatio = 128, amplitudes to 1. Both bounds are far
/// above the arithmetic these comparisons actually see: at position 0 with
/// completion 0 the interpolation is `x * 1 + y * 0`, so a correct build lands
/// bit-identically and these margins are headroom, not the claim.
constexpr float kRatioMatchMargin = 1.0e-4f;
constexpr float kAmplitudeMatchMargin = 1.0e-6f;

[[nodiscard]] bool outputsMatch(const EngineOutputs& lhs, const EngineOutputs& rhs) noexcept {
    if (lhs.count != rhs.count) {
        return false;
    }
    for (std::size_t i = 0; i < SpectralState::kStatePartials; ++i) {
        if (std::abs(lhs.ratios[i] - rhs.ratios[i]) > kRatioMatchMargin) {
            return false;
        }
        if (std::abs(lhs.amplitudes[i] - rhs.amplitudes[i]) > kAmplitudeMatchMargin) {
            return false;
        }
    }
    return true;
}

/// @brief A deliberately odd 3-partial state that NO authored row can equal.
///
/// It is what slot 0 holds before the live push, so "the output arrays moved"
/// is a real observation on every row rather than a coincidence of the table.
[[nodiscard]] SpectralState makeOddState() noexcept {
    SpectralState s{};
    s.numPartials = 3;
    s.ratios[0] = 0.5f; // exactly kMinStateRatio
    s.ratios[1] = 7.0f;
    s.ratios[2] = 90.0f;
    s.amplitudes[0] = 1.0f;
    s.amplitudes[1] = 0.5f;
    s.amplitudes[2] = 0.25f;
    s.name[0] = 'O';
    s.name[1] = 'd';
    s.name[2] = 'd';
    normalizeSpectralState(s);
    return s;
}

// =============================================================================
// The authored-state table
// =============================================================================
//
// The analogue of T002's preservation table, rebuilt here because that table
// lives in a Layer-2 TU compiled into a different executable. Rows whose
// post-call state is INVALID are kept in the table on purpose and skipped by the
// acceptance loop: rejection is the correct behaviour for them, and their
// presence is what proves the filter is exercised rather than vacuous.

struct AuthoredRow {
    std::string label;
    SpectralState state;
};

struct PartialEdit {
    SpectralStateId id;
    std::size_t index;
    float ratio;
    float amplitude;
    const char* why;
};

struct BlendPair {
    SpectralStateId a;
    SpectralStateId b;
};

[[nodiscard]] std::vector<AuthoredRow> buildAuthoredRows() {
    std::vector<AuthoredRow> rows;

    constexpr std::array<SpectralStateId, 5> kIds{SpectralStateId::SineStack, SpectralStateId::Bell,
                                                  SpectralStateId::Choir, SpectralStateId::Glass,
                                                  SpectralStateId::Breath};

    // --- setPartial ---------------------------------------------------------
    constexpr std::array<PartialEdit, 16> kEdits{
        {{.id = SpectralStateId::SineStack,
          .index = 0,
          .ratio = 1.5f,
          .amplitude = 0.5f,
          .why = "perfect fifth on the fundamental"},
         {.id = SpectralStateId::SineStack,
          .index = 0,
          .ratio = 0.5f,
          .amplitude = 1.0f,
          .why = "exactly kMinStateRatio"},
         {.id = SpectralStateId::SineStack,
          .index = 0,
          .ratio = -5.0f,
          .amplitude = 2.0f,
          .why = "both arguments clamp"},
         {.id = SpectralStateId::SineStack,
          .index = 0,
          .ratio = 1000.0f,
          .amplitude = 0.0f,
          .why = "ratio clamps down into the monotone window; amplitude exactly 0"},
         {.id = SpectralStateId::SineStack,
          .index = 10,
          .ratio = 11.5f,
          .amplitude = 0.1f,
          .why = "interior slot, inside the window"},
         {.id = SpectralStateId::SineStack,
          .index = 63,
          .ratio = 100.0f,
          .amplitude = 0.9f,
          .why = "last slot, upper edge is kMaxStateRatio"},
         {.id = SpectralStateId::SineStack,
          .index = 63,
          .ratio = 500.0f,
          .amplitude = 1.0f,
          .why = "last slot, ratio clamps to kMaxStateRatio then to the window"},
         {.id = SpectralStateId::Bell,
          .index = 5,
          .ratio = 10.0f,
          .amplitude = 0.3f,
          .why = "Bell's inharmonic ladder, interior"},
         {.id = SpectralStateId::Bell,
          .index = 23,
          .ratio = 60.0f,
          .amplitude = 0.2f,
          .why = "Bell's last AUTHORED slot (numPartials == 24)"},
         {.id = SpectralStateId::Bell,
          .index = 0,
          .ratio = 0.75f,
          .amplitude = 0.8f,
          .why = "below the fundamental"},
         {.id = SpectralStateId::Choir,
          .index = 20,
          .ratio = 21.2f,
          .amplitude = 0.7f,
          .why = "interior of a 64-partial state"},
         {.id = SpectralStateId::Choir,
          .index = 1,
          .ratio = 1.0f,
          .amplitude = 0.05f,
          .why = "ratio collapses onto the neighbour and must clamp, not swap"},
         {.id = SpectralStateId::Glass,
          .index = 1,
          .ratio = 2.05f,
          .amplitude = 0.4f,
          .why = "Glass's stretched ladder"},
         {.id = SpectralStateId::Glass,
          .index = 40,
          .ratio = 41.0f,
          .amplitude = 1.0f,
          .why = "amplitude exactly 1"},
         {.id = SpectralStateId::Breath,
          .index = 30,
          .ratio = 31.5f,
          .amplitude = 0.6f,
          .why = "ratio clamps to the upper window edge"},
         {.id = SpectralStateId::Breath,
          .index = 0,
          .ratio = 1.0f,
          .amplitude = 0.25f,
          .why = "amplitude only; ratio unchanged"}}};

    for (const PartialEdit& edit : kEdits) {
        SpectralState s = makeFactoryState(edit.id);
        setPartial(s, edit.index, edit.ratio, edit.amplitude);
        rows.push_back({std::string("setPartial: ") + edit.why, s});
    }

    // Rejection rows: the mutator is a no-op, so the row is the factory state
    // and the engine must still accept it.
    {
        SpectralState s = makeFactoryState(SpectralStateId::SineStack);
        setPartial(s, 0, nanFromBits(), 0.5f);
        rows.push_back({"setPartial: NaN ratio (bit pattern) rejected", s});
    }
    {
        SpectralState s = makeFactoryState(SpectralStateId::SineStack);
        setPartial(s, 0, 1.5f, infFromBits());
        rows.push_back({"setPartial: Inf amplitude (bit pattern) rejected", s});
    }
    {
        SpectralState s = makeFactoryState(SpectralStateId::Bell);
        setPartial(s, 64, 2.0f, 0.5f);
        rows.push_back({"setPartial: index == kStatePartials rejected", s});
    }
    {
        SpectralState s = makeFactoryState(SpectralStateId::Bell);
        setPartial(s, static_cast<std::size_t>(-1), 2.0f, 0.5f);
        rows.push_back({"setPartial: index == SIZE_MAX rejected", s});
    }
    {
        SpectralState s = makeFactoryState(SpectralStateId::Bell);
        setPartial(s, 24, 40.0f, 0.5f);
        rows.push_back({"setPartial: index at numPartials (unauthored slot) rejected", s});
    }

    // --- tiltState ----------------------------------------------------------
    constexpr std::array<float, 7> kTilts{-30.0f, -12.0f, -6.0f, 0.0f, 6.0f, 12.0f, 30.0f};
    for (const SpectralStateId id : kIds) {
        for (const float db : kTilts) {
            SpectralState s = makeFactoryState(id);
            tiltState(s, db);
            rows.push_back({"tiltState: " + std::to_string(db) + " dB/oct", s});
        }
    }
    {
        SpectralState s = makeFactoryState(SpectralStateId::Glass);
        tiltState(s, nanFromBits());
        rows.push_back({"tiltState: NaN dB/oct (bit pattern) rejected", s});
    }
    {
        SpectralState s = makeFactoryState(SpectralStateId::Glass);
        tiltState(s, infFromBits());
        rows.push_back({"tiltState: Inf dB/oct (bit pattern) rejected", s});
    }

    // --- blendStates --------------------------------------------------------
    constexpr std::array<BlendPair, 6> kPairs{
        {{.a = SpectralStateId::SineStack, .b = SpectralStateId::Bell},
         {.a = SpectralStateId::SineStack, .b = SpectralStateId::Breath},
         {.a = SpectralStateId::Bell, .b = SpectralStateId::Glass},
         {.a = SpectralStateId::Choir, .b = SpectralStateId::Breath},
         {.a = SpectralStateId::Glass, .b = SpectralStateId::Choir},
         {.a = SpectralStateId::Breath, .b = SpectralStateId::SineStack}}};
    constexpr std::array<float, 7> kBlendPositions{-1.0f, 0.0f, 0.25f, 0.5f, 0.75f, 1.0f, 2.0f};
    for (const BlendPair& pair : kPairs) {
        const SpectralState a = makeFactoryState(pair.a);
        const SpectralState b = makeFactoryState(pair.b);
        for (const float t : kBlendPositions) {
            rows.push_back({"blendStates: t = " + std::to_string(t), blendStates(a, b, t)});
        }
    }
    {
        const SpectralState a = makeFactoryState(SpectralStateId::Choir);
        const SpectralState b = makeFactoryState(SpectralStateId::Glass);
        rows.push_back({"blendStates: NaN t (bit pattern) returns a", blendStates(a, b, nanFromBits())});
        rows.push_back({"blendStates: Inf t (bit pattern) returns a", blendStates(a, b, infFromBits())});
    }

    // --- rows whose SOURCE is invalid somewhere OTHER than the edited index --
    // The mutators leave these byte-unchanged (spectral_state.h:539, :716), so
    // the post-call state is still invalid and the acceptance loop skips it.
    {
        SpectralState bad = makeFactoryState(SpectralStateId::SineStack);
        bad.amplitudes[5] = 1.5f;
        setPartial(bad, 0, 1.5f, 0.5f);
        rows.push_back({"INVALID SOURCE: amplitudes[5] out of range", bad});
    }
    {
        SpectralState bad = makeFactoryState(SpectralStateId::Bell);
        bad.name.fill('x'); // no NUL anywhere in the field
        tiltState(bad, -6.0f);
        rows.push_back({"INVALID SOURCE: name has no NUL", bad});
    }
    {
        SpectralState bad = makeFactoryState(SpectralStateId::Choir);
        bad.ratios[31] = bad.ratios[29]; // descending pair at index 30/31
        setPartial(bad, 0, 1.25f, 0.5f);
        rows.push_back({"INVALID SOURCE: non-monotone ratio pair at index 30", bad});
    }

    return rows;
}

} // namespace

// =============================================================================
// SC-012 - the ACCEPTANCE arm
// =============================================================================
//
// SpectralMorphEngine::setState rejects an invalid state wholesale
// (`spectral_morph_engine.h:303-305`), so a mutator that left a state one
// epsilon outside isValidSpectralState would be SILENTLY INERT rather than
// audibly wrong. This case proves the other direction: every authored state the
// mutators produce IS accepted, i.e. the engine's output arrays move to it.
//
// SEAMS THAT MUST NOT BE USED HERE, and why:
//   * getStateCount() returns numStates_ (`:450`), which only setStateCount
//     writes (`:325-335`). setState never touches it, so it cannot witness
//     acceptance.
//   * isStateFadeActive() alone is not a witness either: setState returns early
//     on an identical state (`:309-312`) and arms the fade only
//     `if (slotContributes(slot))` (`:318`).
// The witness used instead is the FR-008 zero-copy output arrays themselves.

TEST_CASE("SpectralState_AuthoredStates_AreAcceptedByMorphEngine", "[spectral_state][phase11]") {
    const std::vector<AuthoredRow> rows = buildAuthoredRows();
    REQUIRE(rows.size() >= 40u);

    const SpectralState slotOne = makeFactoryState(SpectralStateId::Bell);
    const SpectralState odd = makeOddState();
    REQUIRE(isValidSpectralState(odd));

    std::size_t accepted = 0;
    std::size_t skippedAsInvalid = 0;

    for (const AuthoredRow& row : rows) {
        if (!isValidSpectralState(row.state)) {
            // Rejection is CORRECT for these; T002 owns the byte-unchanged claim.
            ++skippedAsInvalid;
            continue;
        }
        INFO("row: " << row.label);

        // The reference: an engine that has held the authored state in slot 0
        // since before prepare(), so no fade is ever armed and its parked output
        // IS "the new state" as the engine renders it.
        SpectralMorphEngine reference;
        reference.setSeed(kMorphSeed);
        reference.setState(0, row.state);
        reference.setState(1, slotOne);
        reference.setStateCount(2);
        reference.prepare(kSampleRate);
        REQUIRE_FALSE(reference.isStateFadeActive());
        reference.setBloom(0.0f);
        reference.setEntropy(0.0f);
        reference.setTravelMode(SpectralMorphEngine::TravelMode::External);
        reference.setTargetPosition(0.0f);
        parkAtZero(reference);
        const EngineOutputs expected = snapshotOutputs(reference);

        // The subject: slot 0 holds `odd`, the authored state is pushed LIVE.
        SpectralMorphEngine subject;
        subject.setSeed(kMorphSeed);
        subject.setState(0, odd);
        subject.setState(1, slotOne);
        subject.setStateCount(2);
        subject.prepare(kSampleRate);
        subject.setBloom(0.0f);
        subject.setEntropy(0.0f);
        subject.setTravelMode(SpectralMorphEngine::TravelMode::External);
        subject.setTargetPosition(0.0f);
        parkAtZero(subject);

        const EngineOutputs before = snapshotOutputs(subject);
        subject.setState(0, row.state);
        for (int chunk = 0; chunk < kFadeSpendChunks; ++chunk) {
            subject.updateChunk(kEngineChunkSamples);
        }
        const EngineOutputs after = snapshotOutputs(subject);

        INFO("count before/after/expected: " << before.count << " / " << after.count << " / "
                                             << expected.count);
        INFO("ratios[0] before/after/expected: " << before.ratios[0] << " / " << after.ratios[0]
                                                 << " / " << expected.ratios[0]);
        CHECK(outputsMatch(after, expected));

        // ... and it genuinely MOVED. `odd` is a 3-partial state no row can
        // equal, so this branch is taken on every accepted row; the guard is
        // kept so the case can never pass vacuously if that ever stops holding.
        if (!outputsMatch(before, expected)) {
            CHECK_FALSE(outputsMatch(before, after));
        }
        ++accepted;
    }

    INFO("accepted " << accepted << " of " << rows.size() << " rows, " << skippedAsInvalid
                     << " skipped as invalid");
    CHECK(accepted >= 40u);
    // The invalid-source rows must actually be reached, or the filter above is
    // vacuous and the case proves less than it claims.
    CHECK(skippedAsInvalid >= 3u);
}

// =============================================================================
// SC-013 - the four AUDIBILITY arms
// =============================================================================

TEST_CASE("SpectralState_AuthoringMutators_AreAudible", "[spectral_state][phase11]") {

    SECTION("(a) setPartial moves the edited partial's pitch and no other's") {
        const SpectralState base = makeFactoryState(SpectralStateId::SineStack);
        REQUIRE(base.ratios[0] == Approx(1.0f).margin(1.0e-6f));

        SpectralState edited = base;
        // THE INDEX IS PINNED AT 0 AND MUST STAY THERE. setPartial's monotone
        // window caps the upper edge at ratios[index+1] / kAuthorSpacing, and a
        // perfect fifth 1.5*(k+1) exceeds it for every k >= 1 (at k = 1: 3.000
        // vs 2.952), so any other index CLAMPS and cannot move a fifth. That is
        // correct clamp-not-swap behaviour, not a bug to route around.
        setPartial(edited, 0, 1.5f, base.amplitudes[0]);
        REQUIRE(edited.ratios[0] == Approx(1.5f).margin(1.0e-6f));
        REQUIRE(edited.amplitudes[0] == Approx(base.amplitudes[0]).margin(1.0e-6f));
        REQUIRE(isValidSpectralState(edited));

        const StateRender before = renderState(base);
        const StateRender after = renderState(edited);

        const float peakBefore = peakFrequencyHz(before.spectrum, kF0Hz * base.ratios[0]);
        const float peakAfter = peakFrequencyHz(after.spectrum, kF0Hz * edited.ratios[0]);
        const float moved = centsBetween(peakBefore, peakAfter);
        INFO("partial 0: " << peakBefore << " Hz -> " << peakAfter << " Hz = " << moved
                           << " cents");
        // 1200 * log2(1.5) = 701.955 cents.
        CHECK(moved == Approx(701.955f).margin(5.0f));

        for (std::size_t i = 1; i < SpectralState::kStatePartials; ++i) {
            const float expectedHz = kF0Hz * base.ratios[i];
            const float unmovedBefore = peakFrequencyHz(before.spectrum, expectedHz);
            const float unmovedAfter = peakFrequencyHz(after.spectrum, expectedHz);
            const float drift = centsBetween(unmovedBefore, unmovedAfter);
            INFO("partial " << i << " (expected " << expectedHz << " Hz): " << unmovedBefore
                            << " -> " << unmovedAfter << " = " << drift << " cents");
            CHECK(std::abs(drift) <= 2.0f);
        }
    }

    SECTION("(b) blendStates' endpoints render as the endpoints") {
        const SpectralState a = makeFactoryState(SpectralStateId::SineStack);
        const SpectralState b = makeFactoryState(SpectralStateId::Breath);
        const SpectralState atZero = blendStates(a, b, 0.0f);
        const SpectralState atOne = blendStates(a, b, 1.0f);
        const SpectralState atHalf = blendStates(a, b, 0.5f);

        // The exact-endpoint short-circuit (spectral_state.h:629-635) is what
        // makes the render comparison below a bit-for-bit claim rather than an
        // approximate one; T002 owns the byte assertion, repeated here because
        // the render claim is meaningless without it.
        REQUIRE(statesEqual(atZero, a));
        REQUIRE(statesEqual(atOne, b));
        REQUIRE(isValidSpectralState(atHalf));

        const StateRender renderA = renderState(a);
        const StateRender renderB = renderState(b);
        const StateRender renderZero = renderState(atZero);
        const StateRender renderOne = renderState(atOne);
        const StateRender renderHalf = renderState(atHalf);

        const TestUtils::FingerprintComparison zeroVsA =
            TestUtils::compareFingerprints(renderZero.fingerprint, renderA.fingerprint);
        INFO("t = 0 vs a: " << zeroVsA.detail << " (worst metric "
                            << zeroVsA.worstMetricRelativeError << ", worst sample "
                            << zeroVsA.worstSampleError << ")");
        CHECK(zeroVsA.withinTolerance());

        const TestUtils::FingerprintComparison oneVsB =
            TestUtils::compareFingerprints(renderOne.fingerprint, renderB.fingerprint);
        INFO("t = 1 vs b: " << oneVsB.detail << " (worst metric "
                            << oneVsB.worstMetricRelativeError << ", worst sample "
                            << oneVsB.worstSampleError << ")");
        CHECK(oneVsB.withinTolerance());

        const double centroidA = spectralCentroidHz(renderA.spectrum);
        const double centroidB = spectralCentroidHz(renderB.spectrum);
        const double centroidHalf = spectralCentroidHz(renderHalf.spectrum);
        INFO("centroids: a = " << centroidA << " Hz, t=0.5 = " << centroidHalf
                               << " Hz, b = " << centroidB << " Hz");
        // SineStack rolls off as n^-1 and Breath as n^-0.25, so a is the darker
        // endpoint by construction. The REQUIRE is the precondition for the
        // "strictly between" claim, not the claim itself.
        REQUIRE(centroidA < centroidB);
        CHECK(centroidHalf > centroidA);
        CHECK(centroidHalf < centroidB);
    }

    SECTION("(c) tiltState is monotone in dB/oct and absolute") {
        const SpectralState source = makeFactoryState(SpectralStateId::SineStack);
        REQUIRE(source.tiltDbPerOct == 0.0f);

        constexpr std::size_t kTiltCount = 5;
        constexpr std::array<float, kTiltCount> kTilts{-12.0f, -6.0f, 0.0f, 6.0f, 12.0f};
        std::array<double, kTiltCount> centroids{};
        for (std::size_t k = 0; k < kTilts.size(); ++k) {
            // A FRESH COPY of the same source each time: tiltState is ABSOLUTE,
            // so chaining would test a different contract.
            SpectralState s = source;
            tiltState(s, kTilts[k]);
            REQUIRE(isValidSpectralState(s));
            REQUIRE(s.tiltDbPerOct == Approx(kTilts[k]).margin(1.0e-6f));
            centroids[k] = spectralCentroidHz(renderState(s).spectrum);
        }
        for (std::size_t k = 1; k < kTilts.size(); ++k) {
            INFO("tilt " << kTilts[k - 1] << " dB/oct -> " << centroids[k - 1] << " Hz; tilt "
                         << kTilts[k] << " dB/oct -> " << centroids[k] << " Hz");
            CHECK(centroids[k] > centroids[k - 1]);
        }

        // Absoluteness: two applications are byte-identical to one.
        SpectralState once = source;
        tiltState(once, -6.0f);
        SpectralState twice = source;
        tiltState(twice, -6.0f);
        tiltState(twice, -6.0f);
        CHECK(statesEqual(once, twice));
        CHECK(once.tiltDbPerOct == twice.tiltDbPerOct);

        const StateRender renderOnce = renderState(once);
        const StateRender renderTwice = renderState(twice);
        const TestUtils::FingerprintComparison absolute =
            TestUtils::compareFingerprints(renderTwice.fingerprint, renderOnce.fingerprint);
        INFO("twice vs once: " << absolute.detail);
        CHECK(absolute.withinTolerance());
    }

    SECTION("(d) setPartial's amplitude argument is live") {
        // Breath is the source because its FUNDAMENTAL is the quietest of the
        // five factory states (the FR-021 law suppresses it by 1 - 0.9), so the
        // FR-017 normalizer - whose input is the whole a_i set - compensates
        // least for a change confined to partial 0. On SineStack the same edit
        // measures ~7.2 dB against the 6 dB bound; on Breath it measures ~9 dB.
        // The bound is SC-013's and is not available for widening either way.
        const SpectralState base = makeFactoryState(SpectralStateId::Breath);
        REQUIRE(base.ratios[0] == Approx(1.0f).margin(1.0e-6f));

        SpectralState quiet = base;
        setPartial(quiet, 0, base.ratios[0], 0.25f);
        SpectralState mid = base;
        setPartial(mid, 0, base.ratios[0], 0.5f);
        SpectralState loud = base;
        setPartial(loud, 0, base.ratios[0], 1.0f);

        REQUIRE(quiet.amplitudes[0] == Approx(0.25f).margin(1.0e-6f));
        REQUIRE(mid.amplitudes[0] == Approx(0.5f).margin(1.0e-6f));
        REQUIRE(loud.amplitudes[0] == Approx(1.0f).margin(1.0e-6f));
        // The ratio argument was the state's own value, so nothing moved in pitch.
        REQUIRE(quiet.ratios[0] == base.ratios[0]);
        REQUIRE(loud.ratios[0] == base.ratios[0]);

        const StateRender renderQuiet = renderState(quiet);
        const StateRender renderMid = renderState(mid);
        const StateRender renderLoud = renderState(loud);

        const float fundamentalHz = kF0Hz * base.ratios[0];
        const double dbQuiet = lobePowerDb(renderQuiet.spectrum, fundamentalHz);
        const double dbMid = lobePowerDb(renderMid.spectrum, fundamentalHz);
        const double dbLoud = lobePowerDb(renderLoud.spectrum, fundamentalHz);
        INFO("partial 0 lobe power: 0.25 -> " << dbQuiet << " dB, 0.50 -> " << dbMid
                                              << " dB, 1.00 -> " << dbLoud << " dB");

        // Monotone in the amplitude argument ...
        CHECK(dbMid > dbQuiet);
        CHECK(dbLoud > dbMid);
        // ... and the full move clears 6 dB.
        CHECK((dbLoud - dbQuiet) >= 6.0);

        // The pitch did NOT move. Both renders share the seed, the frequencies
        // and therefore the MCF orbit, so this difference is the mutator's
        // alone.
        const float pitchQuiet = peakFrequencyHz(renderQuiet.spectrum, fundamentalHz);
        const float pitchLoud = peakFrequencyHz(renderLoud.spectrum, fundamentalHz);
        const float pitchDrift = centsBetween(pitchQuiet, pitchLoud);
        INFO("partial 0 pitch: " << pitchQuiet << " Hz -> " << pitchLoud << " Hz = " << pitchDrift
                                 << " cents");
        CHECK(std::abs(pitchDrift) < 2.0f);
    }
}
