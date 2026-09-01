// ==============================================================================
// Layer 3: System Tests - NoiseOrganism, CPU budget (SC-004), the T002 probe and
//          the T016 calibration pass    (specs/vorago-phase2-noise-organism)
// ==============================================================================
// Constitution Principle XII: Test-First Development.
//
// Reference: specs/vorago-phase2-noise-organism/spec.md   (FR-017, FR-018,
//                                                          FR-095, SC-004,
//                                                          SC-009, SC-019)
//            specs/vorago-phase2-noise-organism/plan.md   (S9, S11, S6.1-S6.4)
//            specs/vorago-phase2-noise-organism/tasks.md  (T001 creates this TU;
//                                                          T002 adds the stage
//                                                          probe; T016 adds the
//                                                          calibration case;
//                                                          T020 adds the five
//                                                          SC-004 baselines)
//
// SCOPE OF THIS TU: hidden, run-on-demand cases only. That is
//   NoiseOrganism_StageCostProbe        - tagged "[.perf]"          (T002)
//   NoiseOrganism_MeasureSourceDrive    - tagged "[.calibration]"   (T016)
//   NoiseOrganism_CpuBudget             - tagged "[.perf]"          (T020)
// and nothing else. Both [.perf] cases are hidden, so CI never runs the TIMING -
// but the SC-004 baselines' static_asserts are evaluated by every CI leg
// regardless of tags, which is exactly why the absolute gate is placed there.
//
// -----------------------------------------------------------------------------
// WHY THIS CASE EXISTS BEFORE THE COMPONENT DOES (plan S11, tasks.md T002)
// -----------------------------------------------------------------------------
// FR-095's budget is 186,666 ns per 512-sample block at 48 kHz - 1.75 % of one
// core (raised from 1 % on 2026-09-01 by user decision; see kBudgetNs).
// The plan's cost model for the SC-004 (c) reference configuration (4 slots x
// 3 resonators + 2 combs each, plus one dust engine) spans ~94,000-170,000 ns.
// That straddles the ceiling, so the feasibility of the configuration this phase
// is specified against is genuinely uncertain and is MEASURED HERE, standalone,
// before `NoiseOrganism` exists. Producing the per-stage breakdown now costs a
// day; discovering it at the end costs the phase.
//
// This case is a PROBE, NOT A GATE. It REQUIREs only that every figure is finite
// and strictly positive - a zero or a NaN means the measurement is broken, which
// is the one thing that would make the table lie. It does NOT assert the
// projection against the budget, because the response to a miss is a USER
// DECISION (FR-095 / OQ-CPU-POLICY), not a test failure:
//
//   *** STOP-AND-SURFACE RULE (FR-095, OQ-CPU-POLICY - NON-NEGOTIABLE) ***
//   If the projected SC-004 (c) figure printed below exceeds 186,666 ns, the
//   executor HALTS THE PHASE and surfaces to the user: the measured per-stage
//   table, the projection, and the three options from plan S11 -
//     A. a `StochasticFilter` hoisted-path amendment (est. 15,000-35,000 ns);
//     B. a `NoiseGenerator` enabled-only smoother path (currently excluded by
//        this spec's Non-Goals - needs a spec amendment);
//     C. a cap or budget change (user decision only).
//   NO IMPLEMENTING AGENT MAY lower kMaxSources, kMaxResonatorsPerSource,
//   kMaxCombsPerSource or kMaxDustGrains, raise the 1 % budget, or relax any
//   threshold. Under no circumstance may a threshold be relaxed to make a test
//   pass. The case emits the verdict block loudly via WARN precisely so the
//   decision is taken by a human, from measured numbers.
//
// -----------------------------------------------------------------------------
// WHY ns/block AND NOT "% of one core"
// -----------------------------------------------------------------------------
// A percent-of-core figure is not reproducible across dev machines or CI
// runners. The measurement basis is NANOSECONDS PER 512-SAMPLE BLOCK AT 48 kHz,
// the basis established by dsp/tests/unit/systems/harmonic_cloud_perf_test.cpp
// and reused by continuous_body_perf_test.cpp and
// atmosphere_engine_perf_test.cpp. The percent figure is REPORTED, never
// asserted.
//
// TRIAL SHAPE (tasks.md T002): best-of-25 x 500 blocks after 400 warm-up blocks,
// the atmosphere_engine_perf_test.cpp idiom. Many short trials, because the dev
// machine is a hybrid part and the dominant noise source is a whole trial
// migrating onto an E-core. Affinity pinning was tried and REJECTED in both
// reference perf TUs.
//
// -----------------------------------------------------------------------------
// THE SIX STAGES, AND WHY EACH IS SHAPED THE WAY IT IS (tasks.md T002 table)
// -----------------------------------------------------------------------------
//  1. NoiseGenerator          prepare(48000, 512), exactly one NoiseType enabled
//                             (Brown), process(buf, 512). One type is the shape
//                             a slot renders; the cost is dominated by the 13
//                             level smoothers + master smoother that step
//                             REGARDLESS of which single type is enabled
//                             (noise_generator.h:332-337, :387-576).
//  2. ResonatorBank           prepare(48000), 3 resonators enabled at
//                             70/140/260 Hz, decay 1.5 s, processBlock(buf, 512)
//                             - SC-004 (c)'s per-slot resonator count and the
//                             FR-016 low anchors.
//  3. TimeVaryingCombBank     prepare(48000, 50), setNumCombs(2), setCombDelay
//     PER-SAMPLE path         pushed every 64 samples and NO snapSmoothers().
//                             This is what the organism gets if S6.3's
//                             snapSmoothers() call is omitted: the 20 ms delay
//                             smoother (kDelaySmoothingMs) never settles, so
//                             processBlock's hoist guard
//                             (timevar_comb_bank.h:728-741) fails every block
//                             and the bank runs its per-sample path.
//  4. TimeVaryingCombBank     identical, but snapSmoothers() after each
//     HOISTED path            64-sample push. Rows 3 and 4 differ ONLY in that
//                             call, so their ratio is the measured value of plan
//                             S11's conclusion 1.
//  5. StochasticFilter        prepare(48000, 512), RandomMode::Walk,
//                             setChangeRate(0.03), setCutoffOctaveRange(1.0),
//                             processBlock(buf, 512). One std::tan + one
//                             reciprocal per sample (stochastic_filter.h ->
//                             svf.h) is the term the plan flags as
//                             unrefactorable inside this spec's Non-Goals.
//  6. dust stand-in           per sample: iterate ALL kMaxDustGrains = 24 POD
//                             slots with an `if (active)` test, up to 24
//                             GrainEnvelope::lookup(table, 2048, phase) calls,
//                             times a carrier sample - i.e. exactly the loop
//                             plan S6.1 renders. THE FULL 24-SLOT POOL IS
//                             ITERATED, NOT A MEAN-CONCURRENCY SUBSET: S6.1
//                             scans the fixed pool regardless of live count, so
//                             the cost does not scale down with concurrency, and
//                             a subset would measure a shape the design does not
//                             render (plan S11's corrected dust row).
//
// THE PROJECTION the probe prints is SC-004 (c):
//     4 x (NoiseGenerator + ResonatorBank + hoisted combs + StochasticFilter)
//   + 1 x dust
// against 186,666 ns. The per-sample-comb variant is printed beside it as the
// measured cost of NOT calling snapSmoothers(); it is evidence, not a candidate.
//
// -----------------------------------------------------------------------------
// SHARED OVERHEAD INSIDE THE TIMED REGION, DECLARED RATHER THAN HIDDEN
// -----------------------------------------------------------------------------
// ResonatorBank::processBlock and StochasticFilter::processBlock are IN-PLACE
// (resonator_bank.h:519, stochastic_filter.h:281). Re-processing their own
// output would close a feedback loop that is neither the shape the organism
// renders nor bounded (a 1.5 s-decay resonator bank fed its own output for
// 6.6 M samples can grow without limit), so each block refills its work buffer
// from a prefilled source. That std::copy sits inside the timed region for those
// two stages. Rather than net it out silently, the table carries a COPY-ONLY row
// measuring the same 512-float refill on its own, so the reader can see the term
// and confirm it is negligible against the stage figures. The projection uses
// the RAW figures, which therefore OVER-states cost slightly - the conservative
// direction for a stop-and-surface decision.
//
// The comb stages need no refill: processBlock(in, out, n) is out-of-place and
// must not alias (timevar_comb_bank.h:340-345). The generator and the dust loop
// produce their own output.
//
// -----------------------------------------------------------------------------
// HOUSE RULES OBSERVED HERE
// -----------------------------------------------------------------------------
// * WHY THIS TU IS NOT IN THE -fno-fast-math LIST: -fno-fast-math would move the
//   very figures this TU exists to report. See dsp/tests/CMakeLists.txt and
//   tasks.md T001.
// * No std::isnan / std::isinf / numeric_limits infinity anywhere: the macOS leg
//   builds with -ffast-math, which folds them. Finiteness is checked on the
//   IEEE-754 exponent field via detail::isFinite (core/db_utils.h:118).
// * ALLOCATION DETECTION: this TU must NOT include
//   <allocation_operator_overrides.h> - the single owner in dsp_systems_tests is
//   dsp/tests/unit/systems/selectable_oscillator_test.cpp:388, and a second
//   include is a duplicate-symbol link error. This TU includes neither header.
// * FTZ/DAZ: dsp_test_main.cpp calls enableFTZDAZ() before any case runs, so
//   every figure below is measured with denormals flushed BY THE PROCESS - the
//   same environment the audio thread runs in.
// * The probe's grain POD is named ProbeDustGrain, NOT DustGrain: T009's ODR
//   sweep requires `struct DustGrain` to have ZERO hits across dsp/ before the
//   real one is declared in noise_organism.h, and a stand-in in a test TU must
//   not consume that name.
//
// Run them explicitly (both are tag-excluded everywhere else):
//   build/windows-x64-release/bin/Release/dsp_systems_tests.exe "[.perf]"
//   build/windows-x64-release/bin/Release/dsp_systems_tests.exe "[.calibration]"
// ==============================================================================

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/core/grain_envelope.h>
#include <krate/dsp/core/random.h>
#include <krate/dsp/processors/noise_generator.h>
#include <krate/dsp/processors/perlin_noise_source.h>
#include <krate/dsp/processors/resonator_bank.h>
#include <krate/dsp/processors/stochastic_filter.h>
#include <krate/dsp/systems/noise_organism.h>
#include <krate/dsp/systems/timevar_comb_bank.h>

#include <audio_features.h>

#include <catch2/catch_all.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

using namespace Krate::DSP;

namespace {

// =============================================================================
// Measurement basis (SC-004, plan S11)
// =============================================================================

constexpr double kSr48 = 48000.0;
constexpr std::size_t kBlockSize = 512;

/// The organism's control chunk (plan S5.1). Comb parameters are pushed once per
/// chunk, which is what makes the smoother-settling question decidable at all.
constexpr std::size_t kControlChunk = 64;
static_assert(kBlockSize % kControlChunk == 0,
              "the comb stages push parameters once per control chunk and must "
              "divide the block exactly");

/// Wall-clock period of one 512-sample block at 48 kHz, in nanoseconds.
constexpr double kBlockPeriodNs = (static_cast<double>(kBlockSize) / kSr48) * 1.0e9;

/// FR-095's per-voice budget: 1.75 % of one core = 186,666 ns/block.
///
/// Written as the literal FR-095 names and TIED to its derivation by the clause
/// below rather than computed from it: kBlockPeriodNs * 0.0175 is
/// 186,666.666... and a printed ceiling must not inherit that residue.
///
/// RAISED FROM 1 % (106,666 ns) ON 2026-09-01 BY EXPLICIT USER DECISION.
///
/// SET FROM A MEASURED DISTRIBUTION, NOT A SINGLE SAMPLE. The first figure put
/// to the user was 142,794 ns, and it was NOT reproducible: five isolated runs
/// of NoiseOrganism_CpuBudget on an otherwise idle machine gave 159,023.6 /
/// 153,616.8 / 158,896.0 / 154,703.6 / 163,491.8 ns -- centre ~158,000
/// (1.48 % of a core), run-to-run spread 6.4 %. The 142,794 sample had been
/// taken at the cool end of a session whose absolute timings drifted ~14 %
/// upward under sustained benchmarking. 1.75 % covers the observed MAXIMUM
/// (163,492) with ~14 % margin.
///
/// Read that spread before trusting any absolute figure here: a 6.4 % run-to-run
/// spread with 14 % session drift is why every perf test in this repo is
/// excluded from CI, and why a number measured once is not evidence.
///
/// This is the one change this file's stop-and-surface rule reserves to the
/// user, and it was taken as plan-S11 option C after the table below was put to
/// them together with the per-stage breakdown. The measured SC-004 (c) figure
/// was 142,794 ns/block, 1.34x the old ceiling. Recorded here so the decision
/// is never mistaken later for a threshold an agent relaxed:
///
///   * The alternatives were priced first. Only ONE cap reduction actually fit:
///     kMaxSources 4 -> 2 (~86,477 ns). Every smaller one still missed --
///     slots 4 -> 3 (~114,640 ns), dust pool 24 -> 12 (~128,510 ns),
///     resonators 3 -> 2 (~133,426 ns). Halving the slot count would have
///     redefined the SC-004 (c) reference configuration itself.
///   * Option A (hoisting StochasticFilter, the largest slot cost at 10,074 ns
///     x 4 = 30 % of the total) was projected to save ~30,700 ns by analogy with
///     the comb bank's own hoisted path (15,251 -> 3,665 ns, a 4.16x cut). That
///     would have landed at ~104,600 ns -- inside 1 % by only ~2 %, which is not
///     a margin worth building a budget on.
///   * So the ceiling, not the instrument, was the thing that was wrong: 1 % of
///     one core was set before the cost of four slots of per-slot stochastic
///     filtering was known. 1.75 % keeps ~14 % headroom over the observed
///     MAXIMUM (163 492 ns) with the full specified capability intact (4 slots,
///     3 resonators, 2 combs, 24 dust grains). It was briefly set to 1.5 % from
///     a single 142 794 ns sample; that sample was not reproducible and the
///     distribution above is what the number is actually set from.
///
/// This does NOT reopen the rule below. Agents still may not raise this number,
/// lower any cap, or relax any threshold; only the user may, and only with the
/// measured table in hand.
constexpr double kBudgetNs = 186666.0;
static_assert(kBudgetNs >= kBlockPeriodNs * 0.0174 && kBudgetNs <= kBlockPeriodNs * 0.0176,
              "FR-095's budget is 1.75 % of one 512-sample block at 48 kHz");

// Trial shape, pinned by tasks.md T002.
constexpr int kTrials = 25;
constexpr int kBlocksPerTrial = 500;
constexpr int kWarmupBlocks = 400;

// SC-004 (c)'s reference configuration.
constexpr double kNumSources = 4.0;  ///< kMaxSources
constexpr std::size_t kResonatorsPerSource = 3;
constexpr std::size_t kCombsPerSource = 2;

// The dust engine's fixed pool and envelope table (plan S1.3, S6.1).
constexpr std::size_t kMaxDustGrains = 24;
constexpr std::size_t kDustEnvelopeTableSize = 2048;

// =============================================================================
// Best-of-N driver
// =============================================================================

/// Pinned warm-up, then best-of-`kTrials` x `kBlocksPerTrial`; returns the
/// winning trial's ns per 512-sample block.
///
/// `runBlock` is taken by const reference, not by forwarding reference: it is
/// INVOKED, many times, never consumed, so there is nothing to forward.
template <typename BlockFn>
[[nodiscard]] double bestTrialNs(const BlockFn& runBlock)
{
    for (int i = 0; i < kWarmupBlocks; ++i) {
        runBlock();
    }

    double best = -1.0;
    for (int trial = 0; trial < kTrials; ++trial) {
        const auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < kBlocksPerTrial; ++i) {
            runBlock();
        }
        const auto end = std::chrono::steady_clock::now();

        const double elapsedNs = std::chrono::duration<double, std::nano>(end - start).count();
        const double nsPerBlock = elapsedNs / static_cast<double>(kBlocksPerTrial);
        if (best < 0.0 || nsPerBlock < best) {
            best = nsPerBlock;
        }
    }
    return best;
}

/// Deterministic white-noise fill, so every stage sees the same excitation on
/// every machine and every run.
void fillWhite(float* buffer, std::size_t numSamples, std::uint32_t seed) noexcept
{
    Xorshift32 rng{seed};
    for (std::size_t i = 0; i < numSamples; ++i) {
        buffer[i] = rng.nextFloat() * 0.25f;
    }
}

// =============================================================================
// Stage 0 - the shared refill term, measured on its own
// =============================================================================

[[nodiscard]] double measureBufferRefill(double& sink)
{
    std::array<float, kBlockSize> src{};
    std::array<float, kBlockSize> work{};
    fillWhite(src.data(), kBlockSize, 0x5EEDu);

    return bestTrialNs([&]() noexcept {
        std::copy(src.begin(), src.end(), work.begin());
        sink += static_cast<double>(work[0]) + static_cast<double>(work[kBlockSize - 1]);
    });
}

// =============================================================================
// Stage 1 - NoiseGenerator, exactly one type enabled
// =============================================================================

[[nodiscard]] double measureNoiseGenerator(double& sink)
{
    NoiseGenerator gen;
    gen.prepare(static_cast<float>(kSr48), kBlockSize);
    // Every other type is disabled by construction - prepare() targets every
    // level smoother at 0 (noise_generator.h:140-144) - so this is "exactly one".
    gen.setNoiseEnabled(NoiseType::Brown, true);
    gen.setNoiseLevel(NoiseType::Brown, -12.0f);

    std::array<float, kBlockSize> out{};

    return bestTrialNs([&]() noexcept {
        gen.process(out.data(), kBlockSize);
        sink += static_cast<double>(out[0]) + static_cast<double>(out[kBlockSize - 1]);
    });
}

// =============================================================================
// Stage 2 - ResonatorBank, 3 enabled at the FR-016 low anchors
// =============================================================================

[[nodiscard]] double measureResonatorBank(double& sink)
{
    ResonatorBank bank;
    bank.prepare(kSr48);

    constexpr std::array<float, kResonatorsPerSource> kAnchorsHz{70.0f, 140.0f, 260.0f};
    for (std::size_t i = 0; i < kResonatorsPerSource; ++i) {
        bank.setEnabled(i, true);
        bank.setFrequency(i, kAnchorsHz[i]);
        bank.setDecay(i, 1.5f);
    }

    std::array<float, kBlockSize> src{};
    std::array<float, kBlockSize> work{};
    fillWhite(src.data(), kBlockSize, 0xA11CEu);

    return bestTrialNs([&]() noexcept {
        std::copy(src.begin(), src.end(), work.begin());
        bank.processBlock(work.data(), kBlockSize);
        sink += static_cast<double>(work[0]) + static_cast<double>(work[kBlockSize - 1]);
    });
}

// =============================================================================
// Stages 3 and 4 - TimeVaryingCombBank, per-sample vs hoisted
// =============================================================================

/// Both comb arms, differing ONLY in `snapAfterPush`.
///
/// The two delay targets alternate on every push. That is load-bearing for the
/// per-sample arm: a repeated identical target would let the 20 ms delay
/// smoother settle, `isComplete()` would start returning true, and the arm would
/// silently become a second measurement of the hoisted path. Both arms push the
/// same trajectory, so their ratio isolates snapSmoothers() and nothing else.
[[nodiscard]] double measureCombBank(bool snapAfterPush, double& sink)
{
    TimeVaryingCombBank combs;
    combs.prepare(kSr48, 50.0f);
    combs.setNumCombs(kCombsPerSource);
    // setModDepth / setRandomModulation are left at their 0.0f library defaults
    // (timevar_comb_bank.h:414, :416) - FR-042 pins them there, and a non-zero
    // modDepth_ disables the hoisted path outright (:731).

    std::array<float, kBlockSize> src{};
    std::array<float, kBlockSize> out{};
    fillWhite(src.data(), kBlockSize, 0xC0FFEEu);

    int pushIndex = 0;

    return bestTrialNs([&]() noexcept {
        for (std::size_t off = 0; off < kBlockSize; off += kControlChunk) {
            const bool even = (pushIndex++ % 2) == 0;
            combs.setCombDelay(0, even ? 16.0f : 18.0f);
            combs.setCombDelay(1, even ? 23.0f : 21.0f);
            if (snapAfterPush) {
                combs.snapSmoothers();
            }
            combs.processBlock(src.data() + off, out.data() + off, kControlChunk);
        }
        sink += static_cast<double>(out[0]) + static_cast<double>(out[kBlockSize - 1]);
    });
}

// =============================================================================
// Stage 5 - StochasticFilter
// =============================================================================

[[nodiscard]] double measureStochasticFilter(double& sink)
{
    StochasticFilter filter;
    filter.prepare(kSr48, kBlockSize);
    filter.setMode(RandomMode::Walk);
    filter.setChangeRate(0.03f);  // kDefaultWanderRateHz
    filter.setCutoffOctaveRange(1.0f);

    std::array<float, kBlockSize> src{};
    std::array<float, kBlockSize> work{};
    fillWhite(src.data(), kBlockSize, 0xBEEF1234u);

    return bestTrialNs([&]() noexcept {
        std::copy(src.begin(), src.end(), work.begin());
        filter.processBlock(work.data(), kBlockSize);
        sink += static_cast<double>(work[0]) + static_cast<double>(work[kBlockSize - 1]);
    });
}

// =============================================================================
// Stage 6 - the dust stand-in loop (plan S6.1)
// =============================================================================

/// POD stand-in for the grain slot plan S1.3 declares.
///
/// NOT named DustGrain: T009's ODR sweep requires that name to have zero hits
/// across dsp/ before noise_organism.h declares it.
struct ProbeDustGrain {
    float phase = 0.0f;
    float phaseIncrement = 0.0f;
    float gain = 0.0f;
    bool active = false;
};

struct DustProbeResult {
    double nsPerBlock = 0.0;
    std::size_t liveAtEnd = 0;
};

/// The S6.1 allocation policy: prefer a free slot, scanning forward from the
/// cursor; otherwise steal the LARGEST-phase grain (the one nearest its own Hann
/// zero, so the truncation step is the smallest available).
[[nodiscard]] std::size_t acquireGrain(const std::array<ProbeDustGrain, kMaxDustGrains>& grains,
                                       std::size_t cursor) noexcept
{
    for (std::size_t k = 0; k < kMaxDustGrains; ++k) {
        const std::size_t idx = (cursor + k) % kMaxDustGrains;
        if (!grains[idx].active) {
            return idx;
        }
    }
    std::size_t victim = 0;
    float largestPhase = -1.0f;
    for (std::size_t i = 0; i < kMaxDustGrains; ++i) {
        if (grains[i].phase > largestPhase) {
            largestPhase = grains[i].phase;
            victim = i;
        }
    }
    return victim;
}

/// Measured at the FR-035 density ceiling, where mean concurrency equals
/// kMaxDustGrains exactly:
///     density              = 20,000 imp/s (the FR-035 ceiling)
///     grainCeilingMs       = 1000 * 24 / 20000 = 1.2 ms
///     dustGrainMsEffective = min(clamp(40, 5, 200), 1.2) = 1.2 ms
///     mean concurrency     = 20000 * 1.2 / 1000 = 24
/// so the pool is saturated and the loop really does perform ~24 envelope
/// lookups per sample - the worst-case shape S6.1 renders, and the one the
/// stop-and-surface decision must be taken against.
///
/// The trigger train and the carrier are PRECOMPUTED outside the timed region:
/// this stage measures the pool scan, the envelope lookups, the allocation
/// policy and the carrier multiply. The generator that produces the velvet
/// trigger has its own row above and is not double-counted here.
[[nodiscard]] DustProbeResult measureDustLoop(double& sink)
{
    std::array<float, kDustEnvelopeTableSize> envelope{};
    GrainEnvelope::generate(envelope.data(), kDustEnvelopeTableSize, GrainEnvelopeType::Hann);

    constexpr float kDensityHz = 20000.0f;
    constexpr float kGrainMs = 1000.0f * static_cast<float>(kMaxDustGrains) / kDensityHz;
    constexpr float kPhaseIncrement = 1.0f / (kGrainMs * 0.001f * static_cast<float>(kSr48));
    // dustGrainGain = 1 / sqrt(max(1, expectedConcurrency)), FR-036.
    const float grainGain = 1.0f / std::sqrt(static_cast<float>(kMaxDustGrains));

    // Velvet-shaped trigger train: a Bernoulli arrival per sample at
    // density/sampleRate, carrying the +/-1 polarity S6.1 reads as the grain
    // sign (noise_generator.h:521-534).
    std::array<float, kBlockSize> trigger{};
    {
        Xorshift32 rng{0xD05Fu};
        const float p = kDensityHz / static_cast<float>(kSr48);
        for (std::size_t i = 0; i < kBlockSize; ++i) {
            // Drawn unconditionally so the RNG advances once per sample whether
            // or not this sample fires -- the trigger pattern must not depend on
            // how many earlier samples happened to fire.
            const float sign = (rng.nextUnipolar() < 0.5f) ? -1.0f : 1.0f;
            trigger[i]       = (rng.nextUnipolar() < p) ? sign : 0.0f;
        }
    }

    std::array<float, kBlockSize> carrier{};
    fillWhite(carrier.data(), kBlockSize, 0x0C4881Eu);

    std::array<ProbeDustGrain, kMaxDustGrains> grains{};
    std::array<float, kBlockSize> out{};
    std::size_t cursor = 0;

    DustProbeResult result{};
    result.nsPerBlock = bestTrialNs([&]() noexcept {
        for (std::size_t s = 0; s < kBlockSize; ++s) {
            if (trigger[s] != 0.0f) {
                const std::size_t idx = acquireGrain(grains, cursor);
                cursor = (cursor + 1) % kMaxDustGrains;
                ProbeDustGrain& g = grains[idx];
                g.phase = 0.0f;
                g.phaseIncrement = kPhaseIncrement;
                g.gain = (trigger[s] > 0.0f ? 1.0f : -1.0f) * grainGain;
                g.active = true;
            }

            float env = 0.0f;
            for (ProbeDustGrain& g : grains) {
                if (g.active) {
                    env += GrainEnvelope::lookup(envelope.data(), kDustEnvelopeTableSize, g.phase)
                           * g.gain;
                    g.phase += g.phaseIncrement;
                    if (g.phase >= 1.0f) {
                        g.active = false;
                    }
                }
            }
            out[s] = carrier[s] * env;
        }
        sink += static_cast<double>(out[0]) + static_cast<double>(out[kBlockSize - 1]);
    });

    // Population, sampled once after the measurement rather than per block: it
    // exists to show the pool really was saturated, which is a property of the
    // configuration, and a per-block scan would put work inside the timed region
    // for no additional evidence.
    for (const ProbeDustGrain& g : grains) {
        if (g.active) {
            ++result.liveAtEnd;
        }
    }
    return result;
}

// =============================================================================
// Reporting
// =============================================================================

[[nodiscard]] std::string row(const std::string& label, double nsPerBlock)
{
    std::ostringstream os;
    os << std::left << std::setw(46) << label << std::right << std::fixed << std::setprecision(1)
       << std::setw(12) << nsPerBlock << " ns/block   " << std::setprecision(4) << std::setw(9)
       << (100.0 * nsPerBlock / kBlockPeriodNs) << " % of one core";
    return os.str();
}

}  // namespace

// =============================================================================
// T002 - the stage-cost probe (plan S11, FR-095 / OQ-CPU-POLICY)
// =============================================================================

TEST_CASE("NoiseOrganism_StageCostProbe", "[.perf]")
{
    double sink = 0.0;

    const double refillNs = measureBufferRefill(sink);
    const double noiseGenNs = measureNoiseGenerator(sink);
    const double resonatorNs = measureResonatorBank(sink);
    const double combsPerSampleNs = measureCombBank(false, sink);
    const double combsHoistedNs = measureCombBank(true, sink);
    const double filterNs = measureStochasticFilter(sink);
    const DustProbeResult dust = measureDustLoop(sink);

    // The sink is read so no stage can be dead-coded away. It is not a result.
    REQUIRE(detail::isFinite(sink));

    // -------------------------------------------------------------------------
    // The only assertions this case makes: a probe, not a gate. A zero or a
    // non-finite figure means the MEASUREMENT is broken, which is the one thing
    // that would make the table below lie.
    // -------------------------------------------------------------------------
    for (const double ns : {refillNs, noiseGenNs, resonatorNs, combsPerSampleNs, combsHoistedNs,
                            filterNs, dust.nsPerBlock}) {
        REQUIRE(detail::isFinite(ns));
        REQUIRE(ns > 0.0);
    }

    // -------------------------------------------------------------------------
    // SC-004 (c) projection:
    //   4 x (NoiseGenerator + ResonatorBank + hoisted combs + StochasticFilter)
    // + 1 x dust
    // -------------------------------------------------------------------------
    const double perSlotHoistedNs = noiseGenNs + resonatorNs + combsHoistedNs + filterNs;
    const double perSlotPerSampleNs = noiseGenNs + resonatorNs + combsPerSampleNs + filterNs;
    const double projectedHoistedNs = kNumSources * perSlotHoistedNs + dust.nsPerBlock;
    const double projectedPerSampleNs = kNumSources * perSlotPerSampleNs + dust.nsPerBlock;

    UNSCOPED_INFO(row("1. NoiseGenerator (Brown only)", noiseGenNs));
    UNSCOPED_INFO(row("2. ResonatorBank (3 @ 70/140/260 Hz, 1.5 s)", resonatorNs));
    UNSCOPED_INFO(row("3. TimeVaryingCombBank, 2 combs, PER-SAMPLE", combsPerSampleNs));
    UNSCOPED_INFO(row("4. TimeVaryingCombBank, 2 combs, HOISTED", combsHoistedNs));
    UNSCOPED_INFO(row("5. StochasticFilter (Walk, 0.03 Hz, 1 oct)", filterNs));
    UNSCOPED_INFO(row("6. dust loop, full 24-slot pool", dust.nsPerBlock));
    UNSCOPED_INFO(row("   (shared 512-float refill, stages 2 and 5)", refillNs));
    UNSCOPED_INFO(row("== SC-004 (c) PROJECTION, hoisted combs", projectedHoistedNs));

    std::ostringstream os;
    os << "\n"
       << "=================================================================================\n"
       << "  NoiseOrganism T002 STAGE-COST PROBE - measured before the component exists\n"
       << "  (specs/vorago-phase2-noise-organism, plan S11, FR-095 / OQ-CPU-POLICY)\n"
       << "  48 kHz, 512-sample blocks, best-of-" << kTrials << " x " << kBlocksPerTrial
       << " blocks after " << kWarmupBlocks << " warm-up\n"
       << "=================================================================================\n"
       << row("1. NoiseGenerator (Brown only)", noiseGenNs) << "\n"
       << row("2. ResonatorBank (3 @ 70/140/260 Hz, 1.5 s)", resonatorNs) << "\n"
       << row("3. TimeVaryingCombBank, 2 combs, PER-SAMPLE", combsPerSampleNs) << "\n"
       << row("4. TimeVaryingCombBank, 2 combs, HOISTED", combsHoistedNs) << "\n"
       << row("5. StochasticFilter (Walk, 0.03 Hz, 1 oct)", filterNs) << "\n"
       << row("6. dust loop, full 24-slot pool", dust.nsPerBlock) << "\n"
       << "---------------------------------------------------------------------------------\n"
       << row("   shared 512-float refill (inside 2 and 5)", refillNs) << "\n"
       << "   dust pool live slots at end of measurement : " << dust.liveAtEnd << " / "
       << kMaxDustGrains << "   (saturated pool = 24)\n"
       << "   snapSmoothers() saving on the comb stage   : " << std::fixed << std::setprecision(2)
       << (combsHoistedNs > 0.0 ? combsPerSampleNs / combsHoistedNs : 0.0) << "x\n"
       << "---------------------------------------------------------------------------------\n"
       << row("per slot (hoisted combs)", perSlotHoistedNs) << "\n"
       << row("SC-004 (c) = 4 x slot + dust  [HOISTED]", projectedHoistedNs) << "\n"
       << row("SC-004 (c) = 4 x slot + dust  [PER-SAMPLE]", projectedPerSampleNs) << "\n"
       << row("FR-095 BUDGET (1.75 % of one core)", kBudgetNs) << "\n"
       << "=================================================================================\n";

    if (projectedHoistedNs > kBudgetNs) {
        os << "  *** OVER BUDGET BY " << std::fixed << std::setprecision(1)
           << (projectedHoistedNs - kBudgetNs) << " ns (" << std::setprecision(2)
           << (projectedHoistedNs / kBudgetNs) << "x the ceiling).\n"
           << "  *** FR-095 / OQ-CPU-POLICY STOP-AND-SURFACE APPLIES. The executor HALTS\n"
           << "  *** the phase and puts the table above, this projection, and the three\n"
           << "  *** plan-S11 options to the USER:\n"
           << "  ***   A. StochasticFilter hoisted-path amendment (est. 15,000-35,000 ns),\n"
           << "  ***      mirroring the TimeVaryingCombBank change already blessed here.\n"
           << "  ***   B. NoiseGenerator enabled-only smoother path - excluded by this\n"
           << "  ***      spec's Non-Goals; needs a spec amendment and a five-consumer\n"
           << "  ***      no-change guarantee.\n"
           << "  ***   C. A cap or budget change - USER DECISION ONLY.\n"
           << "  *** NO AGENT MAY lower kMaxSources / kMaxResonatorsPerSource /\n"
           << "  *** kMaxCombsPerSource / kMaxDustGrains, raise the budget, or relax any\n"
           << "  *** threshold to make this fit. Reduce cost, never move the line.\n"
           << "=================================================================================\n";
    } else {
        os << "  WITHIN BUDGET: " << std::fixed << std::setprecision(1)
           << (kBudgetNs - projectedHoistedNs) << " ns of headroom (" << std::setprecision(2)
           << (100.0 * projectedHoistedNs / kBudgetNs) << " % of the FR-095 ceiling).\n"
           << "  The reference configuration is feasible; proceed to T003. Note this\n"
           << "  projection EXCLUDES control-rate work (plan S11 estimates < 2,000\n"
           << "  ns/block) and the organism's own mix tail.\n"
           << "=================================================================================\n";
    }

    WARN(os.str());
}

// =============================================================================
// T016 - the calibration pass (FR-017, FR-018, plan S9, S6.3)
// =============================================================================
// FOUR CONSTANT TABLES IN noise_organism.h ARE MEASURED, NOT AUTHORED, AND THIS
// IS THE CASE THAT MEASURES THEM:
//     kSourceDriveDb[kNumNoiseTypes]   (FR-017, plan S9.1)   - part 1
//     kModelTrimDb[4]                  (FR-017, plan S9.1)   - part 2
//     kMakeupSlopeDb                   (FR-018, plan S9.2)   - part 3
//     kMaxCombDelayStepSamples         (FR-063, plan S6.3)   - part 4
// It stays checked in permanently. That is the whole point: a guessed table
// cannot be shipped when the procedure that produced it is executable, and a
// later change to NoiseGenerator's colour filters, to ResonatorBank's Q law or
// to TimeVaryingCombBank's interpolation can be re-measured in one command
// rather than re-derived from an argument.
//
// It is TAGGED [.calibration] - hidden, never run by the default suite or by CI.
// It renders minutes of audio and it is not a gate on anything: the GATES are
// SC-019 (a)'s +/-3 dB-vs-White window (noise_organism_spectral_test.cpp) and
// SC-001 (c)'s dBFS window, which fail on a placeholder table.
//
//   build/windows-x64-release/bin/Release/dsp_systems_tests.exe "[.calibration]"
//
// -----------------------------------------------------------------------------
// THE ONE ASSERTION THIS CASE MAKES, AND WHY IT IS AN ASSERTION (plan S9.1)
// -----------------------------------------------------------------------------
// NoiseGenerator clamps setNoiseLevel's argument to
// [kMinLevelDb, kMaxLevelDb] = [-96, +12] (noise_generator.h:262, :104-105), so
// at kSourceReferenceDb = kDefaultLevelDb = -20 the largest drive that can
// actually be APPLIED is +32 dB. A measured table can exceed that, and if it
// does, silently clamping would push the type outside SC-019 (a)'s window with
// no explanation anywhere. So the case CHECKs every SELECTABLE type (the 12 of
// FR-012 - ModulationNoise is excluded because it is unreachable) and then
// REQUIREs that none was out of range, after printing all four tables, so the
// failure carries the measured evidence with it.
//
// *** THE ASSERTION HAS ALREADY FIRED ONCE, AND WHAT IT CAUGHT IS WHY IT IS
// *** WORTH KEEPING (2026-09-01). Measured at the LIBRARY floor defaults,
// TapeHiss needed +63.690 dB of drive and Asperity +72.000 dB: under the zero
// sidechain FR-013 mandates, both sat at tapeHissFloorDb_ = -60
// (noise_generator.h:645) and asperityFloorDb_ = -72 (:651), multiplicative
// attenuations INSIDE the type, downstream of the level smoother, that no level
// argument can undo - Asperity's +72.0000 WAS its floor, to four decimals. Both
// types then rendered below SC-019 (a)'s -60 dBFS non-silence floor.
// The cause was a MISSING FR-013 forward, not an unresolvable conflict: FR-013
// requires setTapeHissParams / setAsperityParams to be forwarded and
// applySlotConfiguration did not. renderBareType now applies the same forwards
// the organism does (NoiseOrganism::kSignalDependentFloorDb, mirrored above),
// both drives fall inside range (+3.690 / +0.000), and the assertion passes on
// the measurement rather than on a relaxation. STILL FORBIDDEN if it fires
// again: relaxing SC-019 (a)'s window, dropping a type, or moving a constant off
// its measurement. Per plan S9.1 the remaining choices - lower
// kSourceReferenceDb, or exclude a type - belong to the USER.
//
// -----------------------------------------------------------------------------
// FIXTURE NOTES, EACH OF WHICH IS A MEASURED DECISION, NOT A CONVENTION
// -----------------------------------------------------------------------------
// * 60 s, not plan S9.1's 5 s, for part 1. VinylCrackle fires at
//   kDefaultCrackleDensity = 3 clicks/s (noise_generator.h:109), so 5 s is a
//   15-event sample: the same instance measures -61.478 dBFS at 5 s and
//   -52.056 dBFS at 60 s. That 9.4 dB estimator error alone put the type outside
//   the generator's range. At 60 s the largest 60 s -> 300 s movement across the
//   whole roster is 0.14 dB, and VinylCrackle spans only +27.00..+27.42 over
//   seeds 1-4.
// * Part 2 measures in the SC-004 (c) REFERENCE CHAIN (one isolated slot, the
//   FR-016 defaults plus that configuration's 3 resonators, wander ON), which is
//   plan S9.1's and tasks.md T016's "at the FR-016 defaults". It previously
//   measured in the chain-neutralised fixture instead; that was a defect, not a
//   convention - for a composed model the chain IS the model, so neutralising it
//   measured a configuration that never plays and charged FilteredWind a
//   +25.945 dB trim. The consequence was a mix dominated ~20 dB by one slot whose
//   band-pass sweeps +/-3.5 octaves, i.e. SC-001 (a) at 9.46 dB against a 3.0 dB
//   limit, SC-002 (c) at 0.398 against 0.06, and SC-009 (b) at 16.06 dB against
//   an 11.20 dB threshold. See referenceChainMeanSquare below and kModelTrimDb's
//   comment in noise_organism.h.
// * Part 3 writes setFrequency BEFORE setQ, because after FR-099 setFrequency
//   re-derives Q from the configured decay (resonator_bank.h:333) - the same
//   order updateResonatorControl uses. The fit is POOLED across the four FR-016
//   anchors with x and y demeaned WITHIN each anchor: that removes the
//   anchor-dependent offset a non-white source produces and leaves only the
//   slope, which is the only thing kMakeupSlopeDb is.
// * Part 4 reports TWO measurements because the one plan S6.3 specified turns
//   out not to bind. The SC-009 (b) envelope sweep passes at EVERY step out to
//   512 samples per control step - comb-delay motion is a timbral/phase effect
//   the bank's own fractional-delay read already handles, and it does not move a
//   25 ms envelope at any rate. So the bound is set by the REACHABLE demand
//   instead, measured on a real PerlinNoiseSource at the fastest legal rate and
//   widest legal span. Both figures are printed; the second is the one the
//   constant is derived from.
//
// HOUSE RULES: no std::isnan/std::isinf anywhere (the macOS leg builds
// -ffast-math); finiteness is detail::isFinite, the exponent-field test
// (core/db_utils.h:118). Every figure is a MEASURED dB or sample count compared
// against a MEASURED window - there is no bit-exact float golden here.
// =============================================================================

namespace {

/// All four tables live in NoiseOrganism's PRIVATE section, so this case cannot
/// diff its measurement against the transcribed value and deliberately does not
/// try: widening the class's public surface to let a test read a calibration
/// constant would be a worse trade than re-reading the printed block. The case
/// therefore EMITS each table in copy-pasteable form and the human transcribing
/// it does the comparison. The automated guard against a stale or guessed table
/// is elsewhere and is stronger: SC-019 (a)'s +/-3 dB-vs-White window and
/// SC-001 (c)'s dBFS window are measured through the shipped constants.
///
/// kSourceReferenceDb is likewise private; the header defines it as exactly
/// NoiseGenerator::kDefaultLevelDb (noise_organism.h, "Source calibration"), so
/// the range arithmetic below uses that public constant and stays in step by
/// construction rather than by a copied literal.
constexpr float kSourceReferenceDb = NoiseGenerator::kDefaultLevelDb;

/// Measurement window for part 1. See the FIXTURE NOTES above for why it is 60 s
/// rather than plan S9.1's 5 s.
constexpr double kDriveMeasureSeconds = 60.0;

/// Seeds. The drive table is measured on the generator's own opt-in seed 1
/// (FR-080); the organism fixtures reuse the spectral TU's seed so the figures
/// here and SC-019 (a)'s are the same renders.
constexpr std::uint32_t kDriveSeed    = 1u;
constexpr std::uint32_t kOrganismSeed = 0x5EEDBEEFu;

const char* noiseTypeName(NoiseType type)
{
    switch (type) {
        case NoiseType::White: return "White";
        case NoiseType::Pink: return "Pink";
        case NoiseType::TapeHiss: return "TapeHiss";
        case NoiseType::VinylCrackle: return "VinylCrackle";
        case NoiseType::Asperity: return "Asperity";
        case NoiseType::Brown: return "Brown";
        case NoiseType::Blue: return "Blue";
        case NoiseType::Violet: return "Violet";
        case NoiseType::Grey: return "Grey";
        case NoiseType::Velvet: return "Velvet";
        case NoiseType::VinylRumble: return "VinylRumble";
        case NoiseType::ModulationNoise: return "ModulationNoise";
        case NoiseType::RadioStatic: return "RadioStatic";
    }
    return "?";
}

const char* modelName(NoiseOrganismModel model)
{
    switch (model) {
        case NoiseOrganismModel::Direct: return "Direct";
        case NoiseOrganismModel::FilteredWind: return "FilteredWind";
        case NoiseOrganismModel::GranularDust: return "GranularDust";
        case NoiseOrganismModel::MetallicHiss: return "MetallicHiss";
    }
    return "?";
}

/// The 12 FR-012-selectable types, in declaration order. ModulationNoise is
/// absent BY CONSTRUCTION: it is floor-less and renders exactly 0.0f under the
/// zero sidechain the organism gives it (noise_generator.h:553-558), so FR-012
/// snaps it to TapeHiss and no caller can reach it.
constexpr std::array<NoiseType, 12> kSelectableTypes{
    NoiseType::White,       NoiseType::Pink,        NoiseType::TapeHiss,
    NoiseType::VinylCrackle, NoiseType::Asperity,   NoiseType::Brown,
    NoiseType::Blue,        NoiseType::Violet,      NoiseType::Grey,
    NoiseType::Velvet,      NoiseType::VinylRumble, NoiseType::RadioStatic};

/// Mirrors of NoiseOrganism::kSignalDependentFloorDb / kSignalDependentSensitivity
/// (private). FR-013's forwards for the two floored signal-dependent types: with
/// a zero sidechain `modulation == floorGain`, so the floor is a constant
/// attenuation inside the type that no setNoiseLevel argument can undo.
constexpr float kSignalDependentFloorDb    = 0.0f;
constexpr float kSignalDependentSensitivity = 1.0f;

/// @brief Part 1's render: a BARE NoiseGenerator, exactly one type enabled at
/// NoiseGenerator's own default level, opt-in seeded (FR-080) so the figure is
/// reproducible rather than dependent on how many times reset() has run.
[[nodiscard]] std::vector<float> renderBareType(NoiseType type, double seconds)
{
    NoiseGenerator generator;
    generator.prepare(static_cast<float>(kSr48), kBlockSize);
    // AFTER prepare: prepare() ends in reset() (noise_generator.h:182), and on an
    // instance that has not latched a seed reset() scrambles the RNG (:189).
    generator.setSeed(kDriveSeed);
    // The FR-013 forwards the organism itself makes in applySlotConfiguration,
    // reproduced here so the drive table is measured on the configuration the
    // organism actually renders with. Without them TapeHiss and Asperity are
    // measured at the library floors (-60 / -72 dB, noise_generator.h:645, :651)
    // and their drive comes out beyond setNoiseLevel's +12 dB ceiling.
    // The two values MIRROR NoiseOrganism::kSignalDependentFloorDb /
    // kSignalDependentSensitivity, which are private - the same relationship
    // this case already has with the four tables it emits for transcription.
    generator.setTapeHissParams(kSignalDependentFloorDb, kSignalDependentSensitivity);
    generator.setAsperityParams(kSignalDependentFloorDb, kSignalDependentSensitivity);
    generator.setNoiseEnabled(type, true);
    generator.setNoiseLevel(type, NoiseGenerator::kDefaultLevelDb);

    const auto numSamples = static_cast<std::size_t>(seconds * kSr48);
    std::vector<float> out(numSamples, 0.0f);
    for (std::size_t i = 0; i < numSamples; i += kBlockSize) {
        generator.process(out.data() + i, std::min(kBlockSize, numSamples - i));
    }
    return out;
}

/// @brief rmsDbfs through the shared helper, so every figure in this case is
/// measured the same way SC-001/SC-019 measure theirs
/// (tests/test_helpers/audio_features.h:37).
[[nodiscard]] double rmsDbfsOf(const std::vector<float>& audio)
{
    return Krate::Test::extractAudioFeatures(audio, kSr48).rmsDbfs;
}

/// @brief Render an organism in 512-sample blocks.
[[nodiscard]] std::vector<float> renderOrganism(NoiseOrganism& organism, std::size_t numSamples)
{
    std::vector<float> out(numSamples, 0.0f);
    for (std::size_t i = 0; i < numSamples; i += kBlockSize) {
        organism.processBlock(out.data() + i, std::min(kBlockSize, numSamples - i));
    }
    return out;
}

/// Fixture constants for part 2's kModelTrimDb measurement.
constexpr std::size_t kReferenceResonatorCount = 3;    ///< SC-004 (c)'s count
constexpr double      kTrimDiscardSeconds      = 3.0;
constexpr double      kTrimMeasureSeconds      = 600.0;
constexpr std::size_t kTrimSeedCount           = 6;

/// @brief One isolated slot in the SC-004 (c) REFERENCE CHAIN: the FR-016
/// defaults plus the reference configuration's 3 resonators, wander left ON.
/// Returns the MEAN SQUARE of a `kTrimMeasureSeconds` render taken after a 3 s
/// discard, streamed so nothing holds the render.
///
/// WHY THIS FIXTURE AND NOT THE CHAIN-NEUTRALISED ONE. For a composed model the
/// chain IS the model (FR-020/FR-022 make a band-pass the definition of
/// FilteredWind; FR-040/FR-042 a 0.75-feedback comb the definition of
/// MetallicHiss), so opening the chain filter to 0.45*fs measures a
/// configuration that never plays: it charged FilteredWind a +25.945 dB trim,
/// which left that slot ~20 dB above every other slot in the FR-016 chain and
/// made the whole organism's level track that one slot's +/-3.5-octave band-pass
/// sweep. See kModelTrimDb's comment in noise_organism.h for the measured
/// consequences (SC-001 (a) 9.46 dB, SC-002 (c) 0.398, SC-009 (b) 16.06 dB).
/// The chain-neutral fixture remains correct for part 1 above, which calibrates
/// a genuinely source-side constant on a bare NoiseGenerator.
///
/// WHY WANDER IS LEFT ON, AND WHY THE RENDER IS 600 s. The quantity being
/// equalised is the level of the RUNNING organism. A FilteredWind slot parked at
/// its base cutoff sits ~5 dB below its own long-run mean (the band-pass is then
/// furthest from the lines the resonator stage leaves), so a wander-off
/// measurement under-trims that model by that much. The price is that the figure
/// is a sample of a stochastic trajectory, so it is averaged over
/// kTrimSeedCount seeds as well as over 600 s; at that length the seed-to-seed
/// spread of the worst model is ~1.5 dB and the ensemble mean is stable to
/// ~0.05 dB.
[[nodiscard]] double referenceChainMeanSquare(NoiseOrganismModel model,
                                              std::uint32_t      seed)
{
    NoiseOrganism organism;
    organism.setSeed(seed);
    organism.prepare(kSr48,
                     NoiseOrganism::PrepareConfig{.maxBlockSamples = std::size_t{512},
                                                  .maxCombDelayMs  = 50.0f,
                                                  .numSources      = std::size_t{1}});
    organism.setNumResonators(0, kReferenceResonatorCount);
    organism.setSourceModel(0, model);

    std::vector<float> block(kBlockSize, 0.0f);
    const auto discard = static_cast<std::size_t>(kTrimDiscardSeconds * kSr48);
    for (std::size_t i = 0; i < discard; i += kBlockSize) {
        organism.processBlock(block.data(), kBlockSize);
    }
    double      sumSquares = 0.0;
    std::size_t counted    = 0;
    const auto  measured = static_cast<std::size_t>(kTrimMeasureSeconds * kSr48);
    for (std::size_t i = 0; i < measured; i += kBlockSize) {
        organism.processBlock(block.data(), kBlockSize);
        for (const float sample : block) {
            sumSquares += static_cast<double>(sample) * static_cast<double>(sample);
            ++counted;
        }
    }
    return sumSquares / static_cast<double>(counted);
}

/// @brief referenceChainMeanSquare averaged over kTrimSeedCount seeds, in dBFS.
[[nodiscard]] double referenceChainCellRmsDbfs(NoiseOrganismModel model)
{
    double sum = 0.0;
    for (std::size_t k = 0; k < kTrimSeedCount; ++k) {
        sum += referenceChainMeanSquare(
            model, kOrganismSeed + static_cast<std::uint32_t>(k) * 0x9E3779B9u);
    }
    return 10.0 * std::log10(sum / static_cast<double>(kTrimSeedCount) + 1e-30);
}

/// @brief SC-009 (b)'s statistic: max |env[k] - env[k-1]| over a 25 ms-frame RMS
/// envelope in dB, skipping `skipSamples` of settle.
[[nodiscard]] double frameEnvelopeMaxDeltaDb(const std::vector<float>& audio,
                                             std::size_t skipSamples)
{
    const auto frame = static_cast<std::size_t>(0.025 * kSr48);
    double previous  = 0.0;
    bool   havePrevious = false;
    double worst        = 0.0;
    for (std::size_t begin = skipSamples; begin + frame <= audio.size(); begin += frame) {
        double sumSquares = 0.0;
        for (std::size_t i = begin; i < begin + frame; ++i) {
            sumSquares += static_cast<double>(audio[i]) * static_cast<double>(audio[i]);
        }
        const double db =
            20.0 * std::log10(std::sqrt(sumSquares / static_cast<double>(frame)) + 1e-30);
        if (havePrevious) {
            worst = std::max(worst, std::fabs(db - previous));
        }
        previous     = db;
        havePrevious = true;
    }
    return worst;
}

/// @brief Part 4 (i): the worst-case comb bank driven at exactly `stepSamples` of
/// delay motion per 64-sample control step, which is the fastest trajectory the
/// S6.3 limiter permits at that bound. `stepSamples == 0` gives the delay-static
/// reference the SC-009 (b) threshold is 1.5x of.
///
/// The delay is driven as a TRIANGLE across the full +/-50 % excursion rather
/// than from a lane: a lane would spend most of its time moving slower than the
/// bound, and what is under test is the bound itself.
[[nodiscard]] double combSweepMaxDeltaDb(float stepSamples, const std::vector<float>& excitation)
{
    constexpr float kFundamentalHz = 60.0f;   // the FR-016 default
    constexpr float kSpread        = 0.35f;   // the FR-016 default
    constexpr float kWanderPct     = 50.0f;   // setCombWander's clamp ceiling
    constexpr std::size_t kNumCombs = NoiseOrganism::kMaxCombsPerSource;

    std::array<float, kNumCombs> base{};
    for (std::size_t n = 0; n < kNumCombs; ++n) {
        const float hz =
            kFundamentalHz * std::sqrt(1.0f + static_cast<float>(n) * kSpread);
        base[n] = std::clamp(1000.0f / hz, 1.0f, 50.0f);
    }

    TimeVaryingCombBank combs;
    combs.prepare(kSr48, 50.0f);
    combs.setNumCombs(kNumCombs);
    for (std::size_t n = 0; n < kNumCombs; ++n) {
        combs.setCombDelay(n, base[n]);
        combs.setCombFeedback(n, NoiseOrganism::kCombFeedbackCap);  // 0.9, the worst case
        combs.setCombGain(n, 0.0f);
        combs.setCombDamping(n, 0.0f);
    }
    combs.snapSmoothers();

    std::vector<float> out(excitation.size(), 0.0f);
    const float maxStepMs = stepSamples * 1000.0f / static_cast<float>(kSr48);
    std::array<float, kNumCombs> applied = base;
    std::array<int, kNumCombs>   direction{1, -1, 1, -1};

    for (std::size_t off = 0; off < excitation.size(); off += kControlChunk) {
        const std::size_t n = std::min(kControlChunk, excitation.size() - off);
        if (stepSamples > 0.0f) {
            for (std::size_t c = 0; c < kNumCombs; ++c) {
                const float lo = base[c] * (1.0f - 0.01f * kWanderPct);
                const float hi = base[c] * (1.0f + 0.01f * kWanderPct);
                float next = applied[c] + static_cast<float>(direction[c]) * maxStepMs;
                if (next >= hi) { next = hi; direction[c] = -1; }
                if (next <= lo) { next = lo; direction[c] = 1; }
                applied[c] = std::clamp(next, 1.0f, 50.0f);
                combs.setCombDelay(c, applied[c]);
            }
        }
        // The same call the organism makes once per slot per control step, and
        // the reason the limiter has to exist at all (plan S6.3).
        combs.snapSmoothers();
        combs.processBlock(excitation.data() + off, out.data() + off, n);
    }
    return frameEnvelopeMaxDeltaDb(out, static_cast<std::size_t>(1.0 * kSr48));
}

/// @brief Part 4 (ii): the largest per-control-step comb-delay demand REACHABLE
/// through the public API, in samples. Measured on a real PerlinNoiseSource at
/// PerlinNoiseSource::kMaxRate, setCombWander's 50 % clamp ceiling and the
/// largest legal base delay (50 ms - what a 20 Hz fundamental yields).
[[nodiscard]] double reachableCombDemandSamples(double sampleRate, std::uint32_t numSeeds)
{
    constexpr float kBaseMs    = 50.0f;
    constexpr float kWanderPct = 50.0f;
    double worst = 0.0;
    for (std::uint32_t seed = 1; seed <= numSeeds; ++seed) {
        PerlinNoiseSource lane;
        lane.prepare(sampleRate);
        lane.setSeed(seed);
        lane.setRate(5.0f);  // PerlinNoiseSource::kMaxRate
        lane.setOctaves(1);
        lane.setDepth(1.0f);
        float previousMs = kBaseMs;
        const auto steps =
            static_cast<std::size_t>(120.0 * sampleRate / static_cast<double>(kControlChunk));
        for (std::size_t k = 0; k < steps; ++k) {
            lane.processBlock(kControlChunk);
            const float p = std::clamp(lane.getCurrentValue(), -1.0f, 1.0f);
            const float targetMs =
                std::clamp(kBaseMs * (1.0f + 0.01f * kWanderPct * p), 1.0f, 50.0f);
            worst = std::max(worst, std::fabs(static_cast<double>(targetMs) -
                                              static_cast<double>(previousMs)) *
                                        sampleRate / 1000.0);
            previousMs = targetMs;
        }
    }
    return worst;
}

/// @brief One copy-pasteable C++ initialiser line for a measured dB constant.
[[nodiscard]] std::string sourceLine(double value, const std::string& trailingComment)
{
    std::ostringstream os;
    os << "      " << std::fixed << std::setprecision(4) << std::showpos << std::setw(10) << value
       << std::noshowpos << "f,   // " << trailingComment;
    return os.str();
}

}  // namespace

TEST_CASE("NoiseOrganism_MeasureSourceDrive", "[.calibration]")
{
    std::ostringstream os;
    os << "\n"
       << "=================================================================================\n"
       << "  NoiseOrganism T016 CALIBRATION PASS   (measured 48 kHz, 512-sample blocks)\n"
       << "  specs/vorago-phase2-noise-organism, plan S9 / S6.3, FR-017 / FR-018 / FR-063\n"
       << "=================================================================================\n";

    // -------------------------------------------------------------------------
    // Part 1 - kSourceDriveDb[kNumNoiseTypes]   (FR-017, plan S9.1)
    // -------------------------------------------------------------------------
    std::array<double, kNumNoiseTypes> measuredRmsDb{};
    for (std::size_t t = 0; t < kNumNoiseTypes; ++t) {
        measuredRmsDb[t] =
            rmsDbfsOf(renderBareType(static_cast<NoiseType>(t), kDriveMeasureSeconds));
        REQUIRE(detail::isFinite(static_cast<float>(measuredRmsDb[t])));
    }
    const double whiteRmsDb = measuredRmsDb[static_cast<std::size_t>(NoiseType::White)];

    os << "\n-- 1. kSourceDriveDb  (bare NoiseGenerator, setSeed(" << kDriveSeed << "), one type at "
       << std::fixed << std::setprecision(1) << NoiseGenerator::kDefaultLevelDb << " dB, "
       << std::setprecision(0) << kDriveMeasureSeconds << " s, White = " << std::setprecision(4)
       << whiteRmsDb << " dBFS) --\n";

    std::array<double, kNumNoiseTypes> measuredDriveDb{};
    std::vector<std::string> outOfRange;
    std::ostringstream driveTable;
    driveTable << "  copy-pasteable kSourceDriveDb (NoiseType declaration order):\n";
    for (std::size_t t = 0; t < kNumNoiseTypes; ++t) {
        const auto type = static_cast<NoiseType>(t);
        measuredDriveDb[t] = whiteRmsDb - measuredRmsDb[t];
        const double applied = static_cast<double>(kSourceReferenceDb) + measuredDriveDb[t];
        const bool selectable =
            std::find(kSelectableTypes.begin(), kSelectableTypes.end(), type) !=
            kSelectableTypes.end();
        const bool inRange = applied >= static_cast<double>(NoiseGenerator::kMinLevelDb) &&
                             applied <= static_cast<double>(NoiseGenerator::kMaxLevelDb);

        os << "  " << std::left << std::setw(17) << noiseTypeName(type) << std::right << std::fixed
           << std::setprecision(4) << std::setw(11) << measuredRmsDb[t] << " dBFS   drive "
           << std::showpos << std::setw(10) << measuredDriveDb[t] << "   applied " << std::setw(10)
           << applied << std::noshowpos << (selectable ? "" : "   [not selectable]")
           << ((selectable && !inRange) ? "   *** OUT OF GENERATOR RANGE ***" : "") << "\n";

        if (selectable && !inRange) {
            // NOT named `detail`: that would shadow Krate::DSP::detail, the
            // namespace this TU's finiteness checks come from (db_utils.h:118).
            std::ostringstream escalation;
            escalation << noiseTypeName(type) << " needs drive " << std::fixed
                       << std::setprecision(3) << measuredDriveDb[t] << " dB -> applied "
                       << applied << " dB, outside NoiseGenerator's ["
                       << NoiseGenerator::kMinLevelDb << ", " << NoiseGenerator::kMaxLevelDb
                       << "]";
            outOfRange.push_back(escalation.str());
        }

        // ModulationNoise is the one entry that is NOT a measurement: it renders
        // exact silence under the zero sidechain (noise_generator.h:553-558), so
        // its "drive" is an artefact of audio_features.h's -160 dBFS sentinel.
        // FR-012 makes it unreachable, so the emitted table pins it at 0.
        std::ostringstream comment;
        comment << std::left << std::setw(16) << noiseTypeName(type) << std::right << std::fixed
                << std::setprecision(4) << std::setw(10) << measuredRmsDb[t] << " dBFS";
        if (!selectable) {
            comment << "   NOT MEASURABLE - unreachable through FR-012";
            driveTable << sourceLine(0.0, comment.str()) << "\n";
        } else {
            if (!inRange) {
                comment << "   *** OUT OF GENERATOR RANGE ***";
            }
            driveTable << sourceLine(measuredDriveDb[t], comment.str()) << "\n";
        }
    }
    os << driveTable.str();

    // -------------------------------------------------------------------------
    // Part 2 - kModelTrimDb[4]   (FR-017, plan S9.1)
    // -------------------------------------------------------------------------
    const double referenceCellDb =
        referenceChainCellRmsDbfs(NoiseOrganismModel::Direct);
    // *** PART 2 IS A RESIDUAL, NOT AN ABSOLUTE, AND THE DIFFERENCE MATTERS. ***
    // Parts 1, 3 and 4 measure BARE components (a NoiseGenerator, a
    // ResonatorBank, a TimeVaryingCombBank, a PerlinNoiseSource), so their
    // figures are independent of what the header currently holds. Part 2 renders
    // through NoiseOrganism, which has ALREADY applied kModelTrimDb at its mix
    // stage, so what comes out is the DELTA still needed:
    //     kModelTrimDb[model] <- kModelTrimDb[model] + delta
    // On a converged table every delta is ~0, which is the check that the
    // transcribed values are still right. Pasting the deltas as absolutes would
    // wipe the calibration, so they are printed as deltas and labelled as such.
    os << "\n-- 2. kModelTrimDb  (SC-004 (c) reference chain, wander on, Direct reference "
       << std::fixed << std::setprecision(4) << referenceCellDb << " dBFS) --\n"
       << "  RESIDUAL, measured through the trim the header already applies.\n"
       << "  ADD each delta to the current kModelTrimDb entry; ~0 means converged.\n";
    for (const NoiseOrganismModel model :
         {NoiseOrganismModel::Direct, NoiseOrganismModel::FilteredWind,
          NoiseOrganismModel::GranularDust, NoiseOrganismModel::MetallicHiss}) {
        const double cellDb = (model == NoiseOrganismModel::Direct)
                                  ? referenceCellDb
                                  : referenceChainCellRmsDbfs(model);
        REQUIRE(detail::isFinite(static_cast<float>(cellDb)));
        // Direct is the reference and its trim is 0 BY CONSTRUCTION, not by
        // measurement: SC-018's "getSourceGain never leaves 1.0" arm depends on
        // the Direct path being exactly untrimmed.
        const double delta =
            (model == NoiseOrganismModel::Direct) ? 0.0 : (referenceCellDb - cellDb);
        os << "      " << std::left << std::setw(16) << modelName(model) << std::right
           << std::fixed << std::setprecision(4) << std::setw(11) << cellDb << " dBFS   delta "
           << std::showpos << std::setw(10) << delta << std::noshowpos
           << (model == NoiseOrganismModel::Direct ? "   (the reference, 0 by construction)" : "")
           << "\n";
    }

    // -------------------------------------------------------------------------
    // Part 3 - resonatorMakeupDb's slope   (FR-018, plan S9.2)
    // -------------------------------------------------------------------------
    constexpr std::array<float, NoiseOrganism::kMaxResonatorsPerSource> kAnchorsHz{
        70.0f, 140.0f, 260.0f, 500.0f};
    constexpr std::array<float, 5> kSweptQ{1.0f, 3.0f, 10.0f, 30.0f, 100.0f};

    os << "\n-- 3. kMakeupSlopeDb  (bare ResonatorBank, one resonator, Brown source, 5 s "
          "with 0.5 s discarded; pooled LS of dB vs log10(Q), demeaned within each anchor) --\n";

    double sumXY = 0.0;
    double sumXX = 0.0;
    for (const float anchorHz : kAnchorsHz) {
        std::array<double, kSweptQ.size()> cellDb{};
        for (std::size_t j = 0; j < kSweptQ.size(); ++j) {
            ResonatorBank bank;
            bank.prepare(kSr48);
            bank.setEnabled(0, true);
            // Frequency FIRST: after FR-099 setFrequency re-derives Q from the
            // configured decay (resonator_bank.h:333), so the explicit Q must be
            // written last - exactly the order updateResonatorControl uses.
            bank.setFrequency(0, anchorHz);
            bank.setQ(0, kSweptQ[j]);
            bank.setGain(0, 0.0f);

            std::vector<float> buffer = renderBareType(NoiseType::Brown, 5.0);
            for (std::size_t i = 0; i < buffer.size(); i += kBlockSize) {
                bank.processBlock(buffer.data() + i, std::min(kBlockSize, buffer.size() - i));
            }
            const auto discard = static_cast<std::size_t>(0.5 * kSr48);
            cellDb[j] = rmsDbfsOf(std::vector<float>(buffer.begin() +
                                                         static_cast<std::ptrdiff_t>(discard),
                                                     buffer.end()));
            REQUIRE(detail::isFinite(static_cast<float>(cellDb[j])));
        }

        double meanX = 0.0;
        double meanY = 0.0;
        for (std::size_t j = 0; j < kSweptQ.size(); ++j) {
            meanX += std::log10(static_cast<double>(kSweptQ[j]));
            meanY += cellDb[j];
        }
        meanX /= static_cast<double>(kSweptQ.size());
        meanY /= static_cast<double>(kSweptQ.size());

        os << "  anchor " << std::fixed << std::setprecision(0) << std::setw(4) << anchorHz
           << " Hz :";
        for (std::size_t j = 0; j < kSweptQ.size(); ++j) {
            os << "  Q" << std::setprecision(0) << std::setw(3) << kSweptQ[j] << " "
               << std::setprecision(3) << std::setw(9) << cellDb[j];
            const double dx = std::log10(static_cast<double>(kSweptQ[j])) - meanX;
            sumXY += dx * (cellDb[j] - meanY);
            sumXX += dx * dx;
        }
        os << "\n";
    }
    REQUIRE(sumXX > 0.0);
    const double measuredSlopeDb = -(sumXY / sumXX);  // dB of make-up per decade of Q
    // The header's own reference Q, recomputed here from the public rt60ToQ and
    // the FR-016 anchor/decay rather than read from the private constant, so an
    // anchor or decay default that moved would show up in this figure.
    const double makeupQRef = static_cast<double>(rt60ToQ(kAnchorsHz[0], 1.5f));
    os << sourceLine(measuredSlopeDb, "kMakeupSlopeDb  (dB of make-up per decade of Q)") << "\n"
       << "  reference Q = rt60ToQ(" << std::fixed << std::setprecision(0) << kAnchorsHz[0]
       << " Hz, 1.5 s) = " << std::setprecision(4) << makeupQRef
       << ";  implied make-up at kMaxResonatorQ = "
       << (measuredSlopeDb * std::log10(static_cast<double>(kMaxResonatorQ) / makeupQRef))
       << " dB\n";

    // -------------------------------------------------------------------------
    // Part 4 - kMaxCombDelayStepSamples   (FR-063, plan S6.3)
    // -------------------------------------------------------------------------
    // White rather than the FR-016 default Brown: Brown's low-frequency content
    // makes a 25 ms-frame RMS estimate an order of magnitude noisier. Measured
    // over a matched 60 s window, the delay-static maxDelta is 6.86 dB on Brown
    // against 0.73 dB on White (0.65 dB at the 30 s window used here). A noisier
    // estimator RAISES the 1.5x threshold and makes the sweep LESS able to reject
    // a step, so White is the conservative excitation, not the convenient one.
    // Both were swept in full on 2026-09-01 and every step passed on both.
    const std::vector<float> combExcitation = renderBareType(NoiseType::White, 30.0);
    const double staticMaxDeltaDb = combSweepMaxDeltaDb(0.0f, combExcitation);
    REQUIRE(staticMaxDeltaDb > 0.0);
    const double sc009ThresholdDb = 1.5 * staticMaxDeltaDb;

    os << "\n-- 4. kMaxCombDelayStepSamples  (feedback " << std::fixed << std::setprecision(2)
       << NoiseOrganism::kCombFeedbackCap << ", " << NoiseOrganism::kMaxCombsPerSource
       << " combs, FR-016 base delays, White excitation, 30 s) --\n"
       << "  (i) SC-009 (b) envelope sweep: delay-static maxDelta " << std::setprecision(4)
       << staticMaxDeltaDb << " dB, threshold (1.5x) " << sc009ThresholdDb << " dB\n";

    double largestPassingStep = 0.0;
    for (const float step : {0.25f, 1.0f, 4.0f, 8.0f, 16.0f, 24.0f, 32.0f, 64.0f, 128.0f}) {
        const double delta = combSweepMaxDeltaDb(step, combExcitation);
        REQUIRE(detail::isFinite(static_cast<float>(delta)));
        const bool pass = delta <= sc009ThresholdDb;
        os << "      step " << std::fixed << std::setprecision(2) << std::setw(8) << step
           << " samples : maxDelta " << std::setprecision(4) << std::setw(9) << delta << " dB   "
           << (pass ? "pass" : "FAIL") << "\n";
        if (pass) {
            largestPassingStep = std::max(largestPassingStep, static_cast<double>(step));
        }
    }

    const double demandSamples = reachableCombDemandSamples(kSr48, 16u);
    REQUIRE(demandSamples > 0.0);
    os << "  (ii) largest REACHABLE demand (Perlin at kMaxRate, 50 % span, 50 ms base, 16 seeds "
          "x 120 s): "
       << std::fixed << std::setprecision(4) << demandSamples << " samples/control step\n"
       << "  largest step passing (i): " << std::setprecision(2) << largestPassingStep
       << "   ->  the bound is set by (ii), because (i) does not bind anywhere reachable\n"
       << "  kMaxCombDelayStepSamples must be >= " << std::setprecision(4) << demandSamples
       << " for the limiter to stay TRANSPARENT to every legal trajectory (SC-002's\n"
       << "  'realised excursion is at least 25 % of the configured span' arm depends on it),\n"
       << "  and should be the smallest swept step above it that still passes (i).\n";

    os << "=================================================================================\n";
    if (!outOfRange.empty()) {
        os << "  *** " << outOfRange.size()
           << " SELECTABLE TYPE(S) CANNOT BE CALIBRATED THROUGH setNoiseLevel:\n";
        for (const std::string& line : outOfRange) {
            os << "  ***   " << line << "\n";
        }
        os << "  *** Plan S9.1's escalation applies: the choice - lower kSourceReferenceDb,\n"
           << "  *** exclude the type, or amend FR-013 so the organism opens that type's\n"
           << "  *** noise floor - belongs to the USER. NO AGENT MAY relax SC-019 (a)'s\n"
           << "  *** +/-3 dB window, drop a type, or move a constant off its measurement.\n"
           << "=================================================================================\n";
    }
    WARN(os.str());

    // -------------------------------------------------------------------------
    // Sanity on the fit itself, so a broken measurement cannot be transcribed as
    // a plausible-looking constant. Both bounds are structural, not tuned: a
    // bandpass admits broadband power in proportion to its noise bandwidth
    // (ENBW ~ f0/Q), so slot RMS must FALL with Q - the make-up slope must be
    // positive - and a decade of Q cannot cost more than a decade of power.
    // -------------------------------------------------------------------------
    CAPTURE(measuredSlopeDb);
    CHECK(measuredSlopeDb > 0.0);
    CHECK(measuredSlopeDb <= 20.0);

    // -------------------------------------------------------------------------
    // Plan S9.1's loud failure. Last, so all four tables above are printed first
    // and the failure carries its evidence.
    // -------------------------------------------------------------------------
    for (const std::string& line : outOfRange) {
        UNSCOPED_INFO(line);
    }
    REQUIRE(outOfRange.empty());
}

// =============================================================================
// T020 - SC-004 (a)-(e): the five CPU baselines
// =============================================================================
// WHAT IS GATED, AND BY WHICH CLAUSE (spec.md SC-004, plan S15's SC-004 row,
// tasks.md T020). Five configurations, each with its OWN checked-in baseline:
//
//   (a) default        2 slots, Direct, 2 resonators + 2 combs each
//   (b)                4 slots, Direct, 3 resonators + 2 combs each
//   (c) REFERENCE      4 slots, one each of Direct / FilteredWind /
//                      GranularDust / MetallicHiss, 3 resonators + 2 combs
//                      each, everything else at the FR-016 defaults, dust at
//                      100 imp/s x 40 ms (mean concurrency 4 of
//                      kMaxDustGrains = 24, ~17 %, so FR-034's steal-oldest is
//                      a genuine backstop and not the normal path)
//   (d) OUT-OF-REGION  every cap maxed: 4 slots x 4 resonators x 4 combs, all
//                      GranularDust at the FR-035 concurrency ceiling
//   (e) ALL-DORMANT    (c) with every slot setSourceDormant(true)
//
// (a), (b) and (c) are gated at `baseline x 1.5` AND carry the two DIFFERENT
// compile-time clauses of atmosphere_engine_perf_test.cpp:34-42:
//
//     static_assert(kBaselineX * kCpuRegressionFactor <= kBudgetNs, ...);  // ceiling
//     static_assert(kBaselineX >= kBudgetNs / 50.0,                ...);  // floor
//
// The floor is NOT the ceiling restated. Its documented purpose is catching a
// baseline recorded from a no-op or misconfigured run: processBlock on an
// un-prepare()d organism fills silence and advances nothing (the guard ladder in
// noise_organism.h's processBlock), so a baseline taken from one of those would
// be a few hundred ns, would satisfy every ceiling clause forever, and would
// gate nothing. Writing `baseline * 1.5 <= kBudgetNs` twice - once rearranged
// as `baseline <= kBudgetNs / 1.5` - would ship no anti-no-op guard at all, which is
// the defect plan S15's SC-004 row calls out by name.
//
// (d) and (e) are regression-tracked against their OWN baselines only and are
// NOT gated against the FR-095 reference - the atmosphere_engine_perf_test.cpp:44-50
// convention for an out-of-region configuration. (d) sits deliberately outside
// the in-region envelope FR-095 names (<= 4 slots x 3 resonators x 2 combs,
// <= 1 dust slot, dust concurrency <= 50 % of the FR-035 ceiling), which (a)-(c)
// sit inside. (e) additionally asserts a MEASURED SAVING against (c), so
// FR-071's "source runs, chain skipped" dormancy claim is a number rather than
// a claim.
//
// -----------------------------------------------------------------------------
// *** STOP-AND-SURFACE (FR-095 / OQ-CPU-POLICY, tasks.md T020) - NON-NEGOTIABLE
// -----------------------------------------------------------------------------
// If (c) misses 186,666 ns the executor HALTS and surfaces the measured
// ns/512-block figure together with T002's per-stage breakdown (source,
// resonators, combs, StochasticFilter, dust). NO IMPLEMENTING AGENT MAY lower
// kMaxSources / kMaxResonatorsPerSource / kMaxCombsPerSource / kMaxDustGrains,
// raise the budget, or relax a threshold to make a figure fit. Reduce cost,
// never move the line. The precedent - Seraphis's AtmosphereEngine going
// 1 % -> 1.5 % - is CONTEXT, NOT PERMISSION: that was an explicit user decision
// taken from measured numbers on 2026-07-28.
//
// -----------------------------------------------------------------------------
// BASELINE PROVENANCE - READ BEFORE TOUCHING A NUMBER BELOW
// -----------------------------------------------------------------------------
// The five constants below are PROVISIONAL and are labelled as such one by one.
// They are NOT measurements of this component: they are projections from the
// T002 stage probe measured on 2026-09-01 (the run recorded by this TU's
// NoiseOrganism_StageCostProbe), because T020 authored this case before any run
// of it existed. This case therefore PRINTS a copy-pasteable baseline block with
// the figures it has just measured, exactly as NoiseOrganism_MeasureSourceDrive
// prints its four calibration tables, and the human transcribing them does the
// replacement. Transcribing a MEASURED figure over a projection is always
// correct; RAISING one to make a REQUIRE pass is the forbidden move above.
//
// The T002 stage figures the projections are built from (ns per 512-block,
// 48 kHz, best-of-25 x 500 after 400 warm-up):
//     NoiseGenerator, one type enabled                    5,009.6
//     ResonatorBank, 3 resonators                         8,091.6
//     TimeVaryingCombBank, 2 combs, HOISTED               4,229.6
//     StochasticFilter (Walk, 0.03 Hz, 1 octave)         11,623.0
//     dust pool, SATURATED 24-slot scan                  29,856.8
//     => per slot, 3 resonators + 2 combs                28,953.8
//     => SC-004 (c) projection                          145,672.0   (1.37x over)
//
// THE PROJECTION FOR (b) AND (c) ALREADY EXCEEDS THE CEILING CLAUSE. There is no
// baseline that both encodes the projection and compiles: an honest
// kCpuBaselineNsB = 115,815 fails `baseline * 1.5 <= kBudgetNs` at COMPILE time and
// takes the whole dsp_systems_tests target down with it, which would prevent the
// very measurement the decision needs. So (b) and (c) are pinned at the
// arithmetic maximum the ceiling clause admits and are labelled NOT A
// MEASUREMENT; if the component measures above that, the run-time REQUIRE fails
// and THAT failure is the stop-and-surface trigger, carrying the measured figure
// with it. Do not raise them to make the failure go away.
//
// -----------------------------------------------------------------------------
// TWO PROPERTIES OF THE COMPONENT THAT MAKE THESE FIGURES READABLE
// -----------------------------------------------------------------------------
// 1. THE SOURCE STAGE RUNS FOR ALL kMaxSources SLOTS IN EVERY CONFIGURATION.
//    renderChunk loops over kMaxSources, not getNumSources(), and the source
//    render sits ABOVE the chainActive() early-out (noise_organism.h's
//    renderChunk, step 1): FR-071 requires a dormant slot's colour-filter state
//    and RNG to be exactly where an always-awake slot's would be, and FR-072's
//    dropped slots are silenced by the same gate. So (a)'s "2 slots" pays four
//    NoiseGenerators and two chains, and (e) pays four sources and no chains.
//    That is the design, not an accident, and it is what the (e)-vs-(c) saving
//    measures the size of.
// 2. THE COMB BANKS RUN THEIR HOISTED PATH. updateCombControl writes
//    setCombDelay and then calls combs.snapSmoothers() once per slot per control
//    step, which is what lets processBlock take the hoisted path
//    (timevar_comb_bank.h:728-741). T002 measured that call worth 4.03x on the
//    comb stage alone. A regression that drops it shows up here as roughly a 3x
//    figure on (b)/(c) long before anything sounds wrong.
// =============================================================================

namespace {

/// The five SC-004 configurations.
enum class CpuBudgetConfig : std::uint8_t { A, B, C, D, E };

/// The precedent's regression bound (atmosphere_engine_perf_test.cpp:38).
constexpr double kCpuRegressionFactor = 1.5;

/// The largest baseline the ceiling clause admits: 186,666 / 1.5 = 124,444.0,
/// rounded DOWN so the clause holds with a margin rather than on a tie.
constexpr double kCpuCeilingAdmittedNs = 124444.0;
static_assert(kCpuCeilingAdmittedNs * kCpuRegressionFactor <= kBudgetNs,
              "the admitted maximum must itself satisfy the ceiling clause");

// -----------------------------------------------------------------------------
// The five baselines. PROVISIONAL - see BASELINE PROVENANCE above.
// -----------------------------------------------------------------------------

/// (a) PROVISIONAL, T002 projection: 4 x NoiseGenerator (5,009.6) + 2 x chain at
/// 2 resonators (8,091.6 x 2/3 + 4,229.6 + 11,623.0 = 21,247.9) = 62,532.
constexpr double kCpuBaselineNsA = 62532.0;

/// (b) NOT A MEASUREMENT: the T002 projection is 4 x 28,953.8 = 115,815 ns,
/// which no baseline can encode without failing the ceiling clause at compile
/// time. Pinned at the admitted maximum; a measurement above it fails the
/// run-time REQUIRE, which is the stop-and-surface trigger.
constexpr double kCpuBaselineNsB = kCpuCeilingAdmittedNs;

/// (c) NOT A MEASUREMENT, for the same reason as (b): the T002 projection is
/// 145,672 ns, 1.37x the FR-095 ceiling.
constexpr double kCpuBaselineNsC = kCpuCeilingAdmittedNs;

/// (d) PROVISIONAL, T002 projection, per slot: NoiseGenerator 5,009.6 (the
/// velvet trigger train) + carrier oscillator ~5,000 + saturated dust pool
/// 29,856.8 + ResonatorBank at 4 of 3 (10,788.8) + 4 combs at 2 of 2 (8,459.2)
/// + StochasticFilter 11,623.0 = 70,737 ns, x 4 slots = 282,950.
/// Out-of-region: tracked against this baseline alone, never against kBudgetNs.
constexpr double kCpuBaselineNsD = 282950.0;

/// (e) MEASURED 2026-09-01, transcribed over the T002 projection.
///
/// The projection was ~35,000 ns - 3 x NoiseGenerator (5,009.6) + the dust slot's
/// generator + carrier + pool at mean concurrency 4 (~15,000, well under the
/// saturated 29,856.8). It was wrong by 1.6x: the measurement is ~56,300 ns, and
/// 56,138 exceeded `projection * 1.5` so the regression clause failed against a
/// number that had never been measured. Transcribing a measured figure over a
/// projection is the sanctioned move here (raising one to make a REQUIRE pass is
/// not, and is not what this is: the dormancy SAVING it exists to police is
/// unaffected at 88,775.8 ns, 62.17 % of (c)).
///
/// Taken from a DISTRIBUTION, not one sample, after the budget above was nearly
/// set from an unrepresentative single measurement: four isolated runs gave
/// 56,138.0 / 56,489.0 / 55,626.4 / 56,969.8 ns, a 2.4 % spread. The value below
/// is the lowest of them, so the clause stays as tight as the data allows -
/// the observed maximum still clears it by 1.48x.
///
/// Out-of-region like (d); its load-bearing assertion is the MEASURED SAVING
/// against (c), below.
constexpr double kCpuBaselineNsE = 56138.0;

// -----------------------------------------------------------------------------
// The two compile-time clauses, per gated configuration (a), (b), (c).
// (d) and (e) carry NEITHER, deliberately: they are out-of-region and a ceiling
// clause on them would assert exactly the thing SC-004 says they do not gate.
// -----------------------------------------------------------------------------
static_assert(kCpuBaselineNsA * kCpuRegressionFactor <= kBudgetNs,
              "SC-004 (a) baseline exceeds the 1 % budget");
static_assert(kCpuBaselineNsA >= kBudgetNs / 50.0,
              "SC-004 (a) baseline looks like a no-op run");
static_assert(kCpuBaselineNsB * kCpuRegressionFactor <= kBudgetNs,
              "SC-004 (b) baseline exceeds the 1 % budget");
static_assert(kCpuBaselineNsB >= kBudgetNs / 50.0,
              "SC-004 (b) baseline looks like a no-op run");
static_assert(kCpuBaselineNsC * kCpuRegressionFactor <= kBudgetNs,
              "SC-004 (c) baseline exceeds the 1 % budget");
static_assert(kCpuBaselineNsC >= kBudgetNs / 50.0,
              "SC-004 (c) baseline looks like a no-op run");

/// Pinned seed for every configuration, so the figures are reproducible and the
/// wander lanes sit on the same trajectories run to run (FR-005).
constexpr std::uint32_t kCpuSeed = 0x5EEDBEEFu;

/// (c)'s dust settings - the FR-016 defaults, restated so the fixture is
/// explicit: 100 imp/s x 40 ms = mean concurrency 4 of kMaxDustGrains = 24.
constexpr float kCpuReferenceDustDensity = 100.0f;
constexpr float kCpuReferenceDustGrainMs = 40.0f;

/// (d)'s dust settings - the FR-035 ceiling, the exact shape T002's dust row
/// measured, so the (d) figure and the probe's row are comparable:
///     effectiveDensity = clamp(20000, 100, 20000)              = 20000 imp/s
///     grainCeilingMs   = 1000 * kMaxDustGrains / 20000         = 1.2 ms
///     effectiveGrainMs = min(clamp(40, 5, 200), 1.2)           = 1.2 ms
///     mean concurrency = 20000 * 1.2 / 1000                    = 24 = the pool
constexpr float kCpuCeilingDustDensity = 20000.0f;
constexpr float kCpuCeilingDustGrainMs =
    1000.0f * static_cast<float>(NoiseOrganism::kMaxDustGrains) / kCpuCeilingDustDensity;

/// Blocks rendered OUTSIDE the timed region before the fixture is inspected.
/// A model or noise-type write arms FR-013's 50 ms duck and a dormancy write a
/// 50 ms gate ramp, so both the applied-state getters and the steady-state cost
/// need the ramps landed first. 200 blocks is 2.13 s at 48 kHz - two orders of
/// magnitude past either ramp. bestTrialNs runs its own 400 warm-up blocks on
/// top of this.
constexpr int kCpuSettleBlocks = 200;

[[nodiscard]] const char* cpuConfigLabel(CpuBudgetConfig config)
{
    switch (config) {
        // Kept at or under row()'s 46-character label column so the ns figures
        // line up with the T002 probe's table in the same run output.
        case CpuBudgetConfig::A: return "(a) 2 slots, Direct, 2 res + 2 combs";
        case CpuBudgetConfig::B: return "(b) 4 slots, Direct, 3 res + 2 combs";
        case CpuBudgetConfig::C: return "(c) ref: 4 slots, 4 models, 3 res + 2 combs";
        case CpuBudgetConfig::D: return "(d) max caps: 4 slots dust, 4 res + 4 combs";
        case CpuBudgetConfig::E: return "(e) all dormant: (c) with every slot dormant";
    }
    return "?";
}

/// @brief Build one SC-004 fixture. Everything not named here stays at the
/// FR-016 defaults on purpose - a configuration is defined by its DEPARTURES,
/// and re-stating a default here would hide a default that moved.
void configureCpuFixture(NoiseOrganism& organism, CpuBudgetConfig config)
{
    const std::size_t slots =
        (config == CpuBudgetConfig::A) ? std::size_t{2} : NoiseOrganism::kMaxSources;

    // Seeded BEFORE prepare, which re-applies the seed last (its NoiseGenerator's
    // prepare ends in a reset() that scrambles the RNG).
    organism.setSeed(kCpuSeed);
    organism.prepare(kSr48,
                     NoiseOrganism::PrepareConfig{.maxBlockSamples = kBlockSize,
                                                  .maxCombDelayMs  = 50.0f,
                                                  .numSources      = slots});
    organism.setNumSources(slots);

    if (config == CpuBudgetConfig::A) {
        return;  // the FR-016 defaults ARE configuration (a)
    }

    for (std::size_t slot = 0; slot < NoiseOrganism::kMaxSources; ++slot) {
        if (config == CpuBudgetConfig::D) {
            organism.setNumResonators(slot, NoiseOrganism::kMaxResonatorsPerSource);
            organism.setNumCombs(slot, NoiseOrganism::kMaxCombsPerSource);
            organism.setSourceModel(slot, NoiseOrganismModel::GranularDust);
            organism.setDustDensity(slot, kCpuCeilingDustDensity);
            organism.setDustGrainMs(slot, kCpuReferenceDustGrainMs);  // capped to 1.2 ms
            continue;
        }

        // 3 resonators; the comb count stays at the FR-016 default of 2.
        organism.setNumResonators(slot, kResonatorsPerSource);

        if (config == CpuBudgetConfig::B) {
            continue;  // every slot stays Direct
        }

        // (c) and (e): one slot of each model, in enum order.
        switch (slot) {
            case 0:
                organism.setSourceModel(slot, NoiseOrganismModel::Direct);
                break;
            case 1:
                organism.setSourceModel(slot, NoiseOrganismModel::FilteredWind);
                break;
            case 2:
                organism.setSourceModel(slot, NoiseOrganismModel::GranularDust);
                organism.setDustDensity(slot, kCpuReferenceDustDensity);
                organism.setDustGrainMs(slot, kCpuReferenceDustGrainMs);
                break;
            default:
                organism.setSourceModel(slot, NoiseOrganismModel::MetallicHiss);
                break;
        }

        if (config == CpuBudgetConfig::E) {
            organism.setSourceDormant(slot, true);
        }
    }
}

/// @brief Assert the fixture really is the configuration its baseline is named
/// for, AFTER the settle render (a model write is ducked, so the applied-state
/// getters report the OLD model until the duck's swap sample).
///
/// This is not ceremony: every figure below is meaningless if the shape drifted,
/// and the shapes are built from FR-016 defaults a later phase can move.
void verifyCpuFixture(const NoiseOrganism& organism, CpuBudgetConfig config)
{
    CAPTURE(cpuConfigLabel(config));
    REQUIRE(organism.isPrepared());

    const std::size_t expectedSlots =
        (config == CpuBudgetConfig::A) ? std::size_t{2} : NoiseOrganism::kMaxSources;
    REQUIRE(organism.getNumSources() == expectedSlots);

    for (std::size_t slot = 0; slot < NoiseOrganism::kMaxSources; ++slot) {
        CAPTURE(slot);
        switch (config) {
            case CpuBudgetConfig::A:
                REQUIRE(organism.getNumResonators(slot) == std::size_t{2});
                REQUIRE(organism.getNumCombs(slot) == std::size_t{2});
                REQUIRE(organism.getSourceModel(slot) == NoiseOrganismModel::Direct);
                break;
            case CpuBudgetConfig::B:
                REQUIRE(organism.getNumResonators(slot) == kResonatorsPerSource);
                REQUIRE(organism.getNumCombs(slot) == kCombsPerSource);
                REQUIRE(organism.getSourceModel(slot) == NoiseOrganismModel::Direct);
                break;
            // Braced deliberately: the expectedModel declaration below would
            // otherwise cross the `case CpuBudgetConfig::D` label, which is a
            // compile error, not a warning.
            case CpuBudgetConfig::C:
            case CpuBudgetConfig::E: {
                REQUIRE(organism.getNumResonators(slot) == kResonatorsPerSource);
                REQUIRE(organism.getNumCombs(slot) == kCombsPerSource);

                // One slot of each model, in enum order - the shape SC-004 (c)
                // names. Asserted AFTER the settle render, because a model write
                // is ducked and getSourceModel reports the OLD value until the
                // duck's swap sample (FR-013).
                // The SC-004 (c) slot roster, indexed rather than chained.
                static constexpr std::array<NoiseOrganismModel, 4> kSlotModels{
                    NoiseOrganismModel::Direct, NoiseOrganismModel::FilteredWind,
                    NoiseOrganismModel::GranularDust, NoiseOrganismModel::MetallicHiss};
                const NoiseOrganismModel expectedModel =
                    kSlotModels[std::min(slot, kSlotModels.size() - 1)];
                REQUIRE(organism.getSourceModel(slot) == expectedModel);

                if (slot == 2) {
                    REQUIRE(std::fabs(organism.getDustDensity(slot) - kCpuReferenceDustDensity) <
                            1.0e-3f);
                    REQUIRE(std::fabs(organism.getDustGrainMs(slot) - kCpuReferenceDustGrainMs) <
                            1.0e-3f);
                }
                if (config == CpuBudgetConfig::E) {
                    // Dormant AND settled: the gate has reached exactly zero, so
                    // the measurement times the steady dormant path and not a
                    // 50 ms fade (FR-073).
                    REQUIRE(organism.isSourceDormant(slot));
                    REQUIRE(organism.getSourceGain(slot) == 0.0f);
                }
                break;
            }
            case CpuBudgetConfig::D:
                REQUIRE(organism.getNumResonators(slot) ==
                        NoiseOrganism::kMaxResonatorsPerSource);
                REQUIRE(organism.getNumCombs(slot) == NoiseOrganism::kMaxCombsPerSource);
                REQUIRE(organism.getSourceModel(slot) == NoiseOrganismModel::GranularDust);
                REQUIRE(std::fabs(organism.getDustDensity(slot) - kCpuCeilingDustDensity) <
                        1.0e-3f);
                // The FR-035 bidirectional rule capped the requested 40 ms.
                REQUIRE(std::fabs(organism.getDustGrainMs(slot) - kCpuCeilingDustGrainMs) <
                        1.0e-3f);
                break;
        }
    }

    if (config != CpuBudgetConfig::E) {
        // Every other configuration must be AUDIBLE at measurement time: a
        // silenced organism is the no-op run the floor clause exists to catch,
        // and it would measure a shape nothing renders.
        REQUIRE(organism.getSourceGain(0) > 0.0f);
    }
}

/// @brief ns per 512-sample block for one configuration, best-of-25 x 500 after
/// 400 warm-up blocks (the atmosphere_engine_perf_test.cpp:22-70 idiom).
[[nodiscard]] double measureCpuConfig(CpuBudgetConfig config, double& sink)
{
    NoiseOrganism organism;
    configureCpuFixture(organism, config);

    std::array<float, kBlockSize> out{};
    for (int i = 0; i < kCpuSettleBlocks; ++i) {
        organism.processBlock(out.data(), kBlockSize);
    }
    verifyCpuFixture(organism, config);

    return bestTrialNs([&]() noexcept {
        organism.processBlock(out.data(), kBlockSize);
        // Read so nothing can be dead-coded away. Not a result.
        sink += static_cast<double>(out[0]) + static_cast<double>(out[kBlockSize - 1]);
    });
}

/// @brief One copy-pasteable baseline line, so a measured figure replaces a
/// projection by transcription rather than by arithmetic.
[[nodiscard]] std::string baselineLine(const std::string& name, double measuredNs)
{
    std::ostringstream os;
    os << "      constexpr double " << std::left << std::setw(18) << name << std::right << " = "
       << std::fixed << std::setprecision(1) << std::setw(10) << measuredNs << ";";
    return os.str();
}

/// One reported configuration: what was measured, what it is compared against,
/// and whether the FR-095 reference applies to it at all.
struct CpuBudgetRow {
    CpuBudgetConfig config;
    double          measuredNs;
    double          baselineNs;
    bool            gatedAgainstBudget;
    const char*     constantName;
};

}  // namespace

TEST_CASE("NoiseOrganism_CpuBudget", "[.perf]")
{
    double sink = 0.0;

    const double nsA = measureCpuConfig(CpuBudgetConfig::A, sink);
    const double nsB = measureCpuConfig(CpuBudgetConfig::B, sink);
    const double nsC = measureCpuConfig(CpuBudgetConfig::C, sink);
    const double nsD = measureCpuConfig(CpuBudgetConfig::D, sink);
    const double nsE = measureCpuConfig(CpuBudgetConfig::E, sink);

    // The sink is read so no configuration can be dead-coded away. Not a result.
    REQUIRE(detail::isFinite(sink));
    for (const double ns : {nsA, nsB, nsC, nsD, nsE}) {
        REQUIRE(detail::isFinite(ns));
        REQUIRE(ns > 0.0);
    }

    // -------------------------------------------------------------------------
    // Report first, assert second: on a miss the failure must carry its evidence
    // (the T016 case is written the same way, for the same reason).
    // -------------------------------------------------------------------------
    const double savingNs       = nsC - nsE;
    const double savingFraction = (nsC > 0.0) ? (savingNs / nsC) : 0.0;

    UNSCOPED_INFO(row(cpuConfigLabel(CpuBudgetConfig::A), nsA));
    UNSCOPED_INFO(row(cpuConfigLabel(CpuBudgetConfig::B), nsB));
    UNSCOPED_INFO(row(cpuConfigLabel(CpuBudgetConfig::C), nsC));
    UNSCOPED_INFO(row(cpuConfigLabel(CpuBudgetConfig::D), nsD));
    UNSCOPED_INFO(row(cpuConfigLabel(CpuBudgetConfig::E), nsE));
    UNSCOPED_INFO(row("FR-095 BUDGET (1.75 % of one core)", kBudgetNs));

    const std::array<CpuBudgetRow, 5> reported{
        CpuBudgetRow{CpuBudgetConfig::A, nsA, kCpuBaselineNsA, true, "kCpuBaselineNsA"},
        CpuBudgetRow{CpuBudgetConfig::B, nsB, kCpuBaselineNsB, true, "kCpuBaselineNsB"},
        CpuBudgetRow{CpuBudgetConfig::C, nsC, kCpuBaselineNsC, true, "kCpuBaselineNsC"},
        CpuBudgetRow{CpuBudgetConfig::D, nsD, kCpuBaselineNsD, false, "kCpuBaselineNsD"},
        CpuBudgetRow{CpuBudgetConfig::E, nsE, kCpuBaselineNsE, false, "kCpuBaselineNsE"}};

    std::ostringstream os;
    os << "\n"
       << "=================================================================================\n"
       << "  NoiseOrganism SC-004 CPU BUDGET   (48 kHz, 512-sample blocks)\n"
       << "  specs/vorago-phase2-noise-organism, FR-095 / SC-004, tasks.md T020\n"
       << "  best-of-" << kTrials << " x " << kBlocksPerTrial << " blocks after " << kWarmupBlocks
       << " warm-up, " << kCpuSettleBlocks << " settle blocks before each\n"
       << "=================================================================================\n";

    for (const CpuBudgetRow& r : reported) {
        os << row(cpuConfigLabel(r.config), r.measuredNs) << "\n"
           << "        baseline " << std::fixed << std::setprecision(1) << std::setw(10)
           << r.baselineNs << "   gate (x" << std::setprecision(1) << kCpuRegressionFactor << ") "
           << std::setprecision(1) << std::setw(10) << (r.baselineNs * kCpuRegressionFactor)
           << "   "
           << (r.measuredNs <= r.baselineNs * kCpuRegressionFactor ? "within baseline"
                                                                   : "*** OVER BASELINE ***")
           << (r.gatedAgainstBudget ? "" : "   [out-of-region: own baseline only]") << "\n";
    }

    os << "---------------------------------------------------------------------------------\n"
       << row("FR-095 BUDGET (1.75 % of one core)", kBudgetNs) << "\n"
       << "  FR-071 dormancy saving, (c) - (e)             " << std::fixed << std::setprecision(1)
       << std::setw(12) << savingNs << " ns/block   " << std::setprecision(2)
       << (100.0 * savingFraction) << " % of (c)\n"
       << "---------------------------------------------------------------------------------\n"
       << "  copy-pasteable baselines, MEASURED on this machine and this build:\n";
    for (const CpuBudgetRow& r : reported) {
        os << baselineLine(r.constantName, r.measuredNs) << "\n";
    }
    os << "  Transcribing a measured figure OVER a projection is always correct.\n"
       << "  RAISING one so a REQUIRE passes is forbidden (FR-095 / OQ-CPU-POLICY).\n"
       << "  A transcribed (a)/(b)/(c) baseline must still satisfy BOTH compile-time\n"
       << "  clauses; one that cannot is the stop-and-surface case, not a licence to\n"
       << "  weaken the clause.\n"
       << "=================================================================================\n";

    if (nsC > kBudgetNs) {
        os << "  *** SC-004 (c) IS OVER THE FR-095 BUDGET BY " << std::fixed
           << std::setprecision(1) << (nsC - kBudgetNs) << " ns (" << std::setprecision(2)
           << (nsC / kBudgetNs) << "x the ceiling).\n"
           << "  *** FR-095 / OQ-CPU-POLICY STOP-AND-SURFACE APPLIES. HALT and put to the\n"
           << "  *** USER this table plus NoiseOrganism_StageCostProbe's per-stage\n"
           << "  *** breakdown (source, resonators, combs, StochasticFilter, dust), with\n"
           << "  *** the three plan-S11 options:\n"
           << "  ***   A. StochasticFilter hoisted-path amendment (est. 15,000-35,000 ns).\n"
           << "  ***   B. NoiseGenerator enabled-only smoother path - excluded by this\n"
           << "  ***      spec's Non-Goals; needs a spec amendment.\n"
           << "  ***   C. A cap or budget change - USER DECISION ONLY.\n"
           << "  *** NO AGENT MAY lower kMaxSources / kMaxResonatorsPerSource /\n"
           << "  *** kMaxCombsPerSource / kMaxDustGrains, raise the budget, or relax any\n"
           << "  *** threshold to make this fit. Reduce cost, never move the line.\n"
           << "=================================================================================\n";
    } else {
        os << "  SC-004 (c) IS WITHIN BUDGET: " << std::fixed << std::setprecision(1)
           << (kBudgetNs - nsC) << " ns of headroom (" << std::setprecision(2)
           << (100.0 * nsC / kBudgetNs) << " % of the FR-095 ceiling).\n"
           << "=================================================================================\n";
    }

    WARN(os.str());

    // -------------------------------------------------------------------------
    // (a), (b), (c): the run-time half of the gate. The compile-time clauses
    // above bind each baseline to 186,666 ns, so these REQUIREs bind the
    // MEASUREMENT to the reference transitively, on every machine.
    // -------------------------------------------------------------------------
    CAPTURE(nsA, kCpuBaselineNsA);
    REQUIRE(nsA <= kCpuBaselineNsA * kCpuRegressionFactor);
    CAPTURE(nsB, kCpuBaselineNsB);
    REQUIRE(nsB <= kCpuBaselineNsB * kCpuRegressionFactor);
    CAPTURE(nsC, kCpuBaselineNsC, kBudgetNs);
    REQUIRE(nsC <= kCpuBaselineNsC * kCpuRegressionFactor);

    // -------------------------------------------------------------------------
    // (d) and (e): regression bound against their OWN baselines only. No budget
    // clause - both are out-of-region by SC-004's own words, and asserting one
    // here would gate what the criterion says is not gated.
    // -------------------------------------------------------------------------
    CAPTURE(nsD, kCpuBaselineNsD);
    REQUIRE(nsD <= kCpuBaselineNsD * kCpuRegressionFactor);
    CAPTURE(nsE, kCpuBaselineNsE);
    REQUIRE(nsE <= kCpuBaselineNsE * kCpuRegressionFactor);

    // -------------------------------------------------------------------------
    // FR-071's dormancy claim, as a NUMBER. (e) is (c) with every chain skipped
    // and every source still running, so the saving is the four chains.
    //
    // The 25 % floor is deliberately far below the expected saving and is
    // derived, not tuned: in T002's table the resonator + comb + filter stages
    // are 23,944 of a 28,954 ns slot - 82 % of per-slot cost - and (e) skips four
    // of them, so a correct implementation saves well over half. A floor near the
    // expectation would fail on machine noise; a floor of zero would let a
    // dormancy that skips NOTHING pass. 25 % rejects that regression while
    // leaving roughly three times the headroom over measurement noise.
    // -------------------------------------------------------------------------
    CAPTURE(nsC, nsE, savingNs, savingFraction);
    REQUIRE(nsE < nsC);
    REQUIRE(savingFraction >= 0.25);
}
