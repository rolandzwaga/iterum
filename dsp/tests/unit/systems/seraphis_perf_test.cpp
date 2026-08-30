// ==============================================================================
// Layer 3: System Tests - Seraphis CPU budgets (SC-001, SC-002), [.perf]
//                                    (specs/seraphis-phase7-voice-engine)
// ==============================================================================
// Constitution Principle XII: Test-First Development.
//
// Reference: specs/seraphis-phase7-voice-engine/spec.md   (SC-001, SC-002, FR-083,
//                                                          RA-1's normative scenario)
//            specs/seraphis-phase7-voice-engine/plan.md   (§6.2, §6.2.1, §6.3)
//            specs/seraphis-phase7-voice-engine/tasks.md  (T001 creates this TU,
//                                                          T014 fills it)
//
// SCOPE OF THIS TU: SC-001 (the full-poly composed-chain CPU budget) and SC-002
//   (per-voice composition overhead), plus the two figures T014 asks to be
//   RECORDED here for compliance.md: the worst-case per-chunk freeze-retry cost
//   (plan §9 R14) and sizeof(SeraphisEngine) (T005). Every case carries the
//   [.perf] tag (FR-083) so the TIMING is excluded from the default run and from
//   CI (.github/workflows/ci.yml excludes perf-tagged cases) - while every
//   static_assert below is still evaluated by every CI leg regardless of tags,
//   which is exactly why the absolute gate is placed there.
//
// Select it explicitly:
//   build/windows-x64-release/bin/Release/dsp_systems_tests.exe "[.perf]"
//   build/windows-x64-release/bin/Release/dsp_systems_tests.exe "SeraphisEngine_FullPolyCpuBudget"
//
// COMPILE FLAGS: none, deliberately. This TU must NEVER be listed under
//   "-fno-fast-math -fno-finite-math-only" in dsp/tests/CMakeLists.txt (only
//   unit/systems/seraphis_nonfinite_test.cpp is) - those flags move the figures
//   the baseline here is pinned to. It also injects no non-finite values and
//   contains no std::isnan / std::isinf / std::numeric_limits infinity: the
//   macOS leg builds with -ffast-math, which folds them. Finiteness is checked
//   on the IEEE-754 exponent field instead (isFiniteValue below).
//
// WHY ns/block AND NOT "% of one core":
//   A percent-of-core figure is not reproducible across dev machines or CI
//   runners - identical code passes or fails by hardware. The basis is therefore
//   NANOSECONDS PER 512-SAMPLE BLOCK AT 48 kHz, the constant every Seraphis perf
//   TU derives (harmonic_cloud_perf_test.cpp:73, continuous_body_perf_test.cpp:
//   108, atmosphere_engine_perf_test.cpp:437, aether_reverb_perf_test.cpp:135),
//   gated against a checked-in baseline as a relative regression bound. The
//   percent figure is REPORTED via WARN, never asserted.
//
// HOW THE ABSOLUTE 25 % CEILING IS STILL BOUND:
//   roadmap line 311 makes "<= 25 % of one core @ 48 kHz" a FUNCTIONAL
//   requirement and roadmap lines 498-499 make CPU budgets FRs generally, so a
//   purely relative gate would not discharge SC-001. The baseline therefore
//   carries BOTH compile-time clauses
//
//       static_assert(baseline * kRegressionFactor <= kReferenceNs, ...);
//       static_assert(baseline <= kMaxAdmissibleNs, ...);
//
//   alongside the run-time REQUIRE(measured <= baseline * kRegressionFactor).
//   The two COMPOSE: a baseline that would let `measured` exceed the reference
//   does not COMPILE, so the run-time REQUIRE transitively binds the absolute
//   figure on every machine and every run.
//
// ==============================================================================
// THE TWO FACTORS ARE DIFFERENT NUMBERS (plan §6.2.1). DO NOT CONFLATE THEM.
// ==============================================================================
//   kRegressionFactor = 1.15  - the RUN-TIME gate and the static_assert
//                               multiplier.
//   kBaselineHeadroom = 1.05  - a RECORDING CONVENTION, applied ONCE when a
//                               measured figure is transcribed into a baseline
//                               (ceil(worst x 1.05)). It is not a gate and
//                               appears in no assertion.
//
//   aether_reverb_perf_test.cpp carries BOTH (kRegressionFactor = 1.5 at :138,
//   the x1.05 recording pad at :325-:342) and an earlier draft of this plan
//   borrowed the wrong one. Phase 7 names them separately.
//
// WHY 1.15, AND NOT PHASE 6's 1.5 NOR THE 1.05 RECORDING HEADROOM:
//   - 1.5 CANNOT FIT THE CEILING. At RA-1's predicted 20.36 % of the block
//     budget a baseline recorded as ceil(20.36 % x 1.05) = 21.38 % gives
//     21.38 % x 1.5 = 32.1 % > 25 %, so
//     static_assert(baseline * kRegressionFactor <= kReferenceNs) would FAIL TO
//     COMPILE. The widest band a 25 % ceiling admits at that baseline is
//     25 / 21.38 = 1.169.
//   - 1.05 IS NOT A GATE. It compiles (21.38 % x 1.05 = 22.45 % <= 25 %) but
//     collapses the run-time regression band from 50 % to 5 % on a best-of-N
//     wall-clock measurement across machines - a flake generator, not a
//     regression detector.
//   - 1.15 IS THE CHOICE. 21.38 % x 1.15 = 24.59 % <= 25 %, i.e. the
//     static_assert holds with ~0.4 points of margin, and a 15 % run-time band
//     is defensible for a best-of-N timing on an idle machine. (Phase 6 could
//     afford 1.5 because its 5 % global ceiling left far more relative
//     headroom.)
//
//   THE ADMISSIBLE BASELINE CEILING IS 25 % / 1.15 = 21.74 % OF THE BLOCK
//   BUDGET (kMaxAdmissibleNs below). The margin is thin and the lever list is
//   named NOW rather than discovered when the measurement lands.
//
// ==============================================================================
// IF THE BASELINE static_assert FAILS: THE ONLY ADMISSIBLE RESPONSES
// ==============================================================================
//   1. RE-DERIVE THE SHIPPED VOICE COUNT (spec RQ-1 / Clarifications Q5). The
//      pool is compiled at kMaxVoices = 16 and only the DEFAULT polyphony is 8,
//      so lowering the shipped default costs no ABI change. RA-1's table:
//        16 -> 38.94 %,  12 -> 29.65 %,  10 -> 25.01 %,  8 -> 20.36 %.
//   2. REDUCE PHASE 7's OWN COMPOSITION COST - the voice sum, the spatial
//      stage, the carry FIFO copies, the output stage, the macro matrix. These
//      are the only terms Phase 7 owns; RA-1's per-component rows are not.
//
//   NEVER a Phase 2/4/5/6 CPU gate (spec N-10: "RA-1's tally uses the MEASURED
//   figures those phases recorded. Phase 7 may not raise any of them").
//   NEVER the 25 % ceiling - RQ-1 explicitly KEPT it while deviating on the
//   voice count.
//   NEVER a quiet widening of kRegressionFactor. Any change to it must be
//   accompanied by the re-derived arithmetic in the block above.
//
// ==============================================================================
// BASELINE PROVENANCE - READ BEFORE TOUCHING kBaselineFullPolyNsPerBlock
// ==============================================================================
//   STATUS: **DERIVED, NOT YET MEASURED.** T014 writes the failing test first;
//   the measurement is taken and transcribed at T016 by the procedure below.
//   The shipped constant is RA-1's arithmetic run through the recording
//   convention, and nothing else:
//
//       RA-1 prediction (8 voices frozen @ 2.322 %/voice + Aether (c) @ 1.787 %)
//         = 20.36 % of one core
//         = 0.2036 x 10 666 666.7 ns              = 2 171 733.3 ns/block
//       ceil(that x kBaselineHeadroom = 1.05)     = 2 280 320   ns/block
//                                                 = 21.38 % of one core
//
//   RA-1's per-component rows, for whoever re-derives this (spec.md:195-214, all
//   MEASURED figures, not budget ceilings):
//     HarmonicCloud, 64 partials + drift   29 642.8 ns  0.278 %
//     SpectralMorphEngine + EntropyProc.    9 300.0 ns  0.087 %
//     ContinuousBody, worst material       55 094.6 ns  0.517 %
//     AtmosphereEngine, frozen                      -   1.440 %
//     --> per voice, frozen                              2.322 %
//     AetherReverb, worst measured cfg (c) 190 584   ns  1.787 %
//   The 20.36 % total is BEFORE the output stage, the voice sum, the spatial
//   stage and the macro matrix, none of which carries a roadmap budget - so the
//   measured figure is expected to land ABOVE 20.36 % and the question the
//   measurement answers is whether it lands below the 21.74 % admissible
//   ceiling.
//
// REPLACEMENT PROCEDURE, and it is the only legal way this constant changes
// (transcribed from aether_reverb_perf_test.cpp:260-278):
//   1. Run  dsp_systems_tests.exe "SeraphisEngine_FullPolyCpuBudget"  at least
//      EIGHT consecutive times on an IDLE machine, on AC, Release.
//      KNOW WHAT THAT DOES NOT COVER: Phase 6 measured that eight runs taken
//      inside one quiet window gave a 2.1-2.7x-optimistic figure against the
//      same binary on the same machine ~40 min later, thermally soaked
//      (aether_reverb_perf_test.cpp:205-258, DATASET 1 vs DATASET 2). A gate cut
//      this way WILL fail on a loaded machine. That is an ACCEPTED RISK on a
//      [.perf] lane that CI does not run: wait and re-run on a quiet machine.
//      It is not a licence to loosen the constant.
//   2. Take the WORST across those runs, round up, pad by at most +5 % -
//      i.e. ceil(worst x kBaselineHeadroom).
//   3. baseline = min(that figure, kMaxAdmissibleNs).
//   4. Record the eight figures, the machine and the date in the table below,
//      and transcribe the result into compliance.md verbatim.
//   5. If step 3's cap BINDS - i.e. the measurement exceeds kMaxAdmissibleNs =
//      21.74 % - the phase is OVER BUDGET. Work the two-item lever list above.
//      Do not raise the baseline; it will not compile.
//   A measured figure may only ever move the baseline DOWN relative to the
//   figure it replaces, or fail the build.
//
//   MEASUREMENT TABLE - TAKEN, and the baseline below is now MEASURED:
//     Machine    : 13th Gen Intel(R) Core(TM) i9-13900HX, idle, on AC
//     Build      : MSVC Release, build/windows-x64-release
//     Trial shape: best-of-16 x 100 blocks
//     Date       : 2026-07-31
//     run        |    1     2     3     4     5     6     7     8     9    10
//     % of core  | 19.70 19.15 19.74 20.07 19.53 19.97 18.34 19.27 18.95 20.05
//     ns/block   | 2.102 2.043 2.106 2.141 2.083 2.130 1.956 2.056 2.021 2.138
//                | (x1e6)                                         max = 2.14061e6
//     worst = 2 140 610 ns/block (20.0682 %)
//     ceil(worst x kBaselineHeadroom = 1.05) = 2 247 641 ns/block (21.07 %)
//     kMaxAdmissibleNs = 2 318 840 (21.74 %)  ->  the cap does NOT bind
//     gate = 2 247 641 x 1.15 = 2 584 787 ns/block (24.23 %)  <=  25 % reference
//   The measured figure moved the baseline DOWN from the derived 2 280 320, as
//   step 5's rule requires.
//
//   A PREVIOUS COMPLIANCE PASS RECORDED 2.834e6 (26.57 %), 2.844e6 (26.66 %) and
//   2.711e6 (25.41 %) FOR THE SAME BINARY AND SCENARIO. Those were taken with
//   other work running on the machine, which is the failure mode this file's
//   step 1 already warns about in capitals ("a gate cut this way WILL fail on a
//   loaded machine ... wait and re-run on a quiet machine. It is not a licence
//   to loosen the constant."). They are recorded here rather than discarded: the
//   spread between a loaded and an idle machine on this scenario is ~33 %, which
//   is larger than kRegressionFactor, and that is a property of the [.perf] lane
//   rather than of the code. The ten runs above were taken back-to-back with
//   nothing else running.
//
// ==============================================================================
// WHAT SC-001 MEASURES - RA-1's NORMATIVE WORST-CASE SCENARIO, STATED IN CODE
// ==============================================================================
//   - polyphony 8 (FR-040's shipped default), ALL 8 VOICES SOUNDING, none idle.
//     Under FR-010's cloud-only envelope (Clarifications Q1) that is the STEADY
//     STATE, not a contrived peak: a released voice keeps rendering at full cost
//     until it is quiescent.
//   - all five macros at their FR-060 neutral (Gravity 0.5, the rest 0), applied
//     ONCE, so the sub-components sit at the FR-019 shipped voice defaults.
//   - cloud at 64 ACTIVE PARTIALS WITH DRIFT, morph + entropy live, spatial
//     stage active.
//   - body at its WORST MEASURED MATERIAL CONFIGURATION - all five materials are
//     measured in this TU and the worst is used, the
//     continuous_body_perf_test.cpp:936-940 idiom, in the configuration behind
//     specs/seraphis-phase4-continuous-body/compliance.md:19.
//   - atmosphere FROZEN, engaged through SeraphisEngine::setAtmosphereFreeze(true)
//     and ASSERTED with isFreezeCaptured() on every sounding voice BEFORE timing
//     starts. AtmosphereEngine::captureFreeze() is a documented no-op until the
//     ring holds a whole analysis window (atmosphere_engine.h:911-916), so an
//     un-asserted freeze would silently measure the cheaper UNFROZEN path -
//     1.048 % vs 1.440 % per voice, i.e. 3.1 points at 8 voices on a 25 %
//     ceiling.
//
//     THE CAPTURE ALONE IS NOT ENOUGH, AND THIS IS THE SUBTLER HALF. FR-019
//     ships setFreezeMix(0) ("freeze is a played technique, not a default
//     state"), and at a SETTLED target of 0 renderFreezeLayer BYPASSES both
//     freeze oscillators outright - `settledDry` at atmosphere_engine.h:2149-2152
//     fills zeros instead of calling processBlock. A scenario that captured the
//     freeze and left the mix at its default would therefore satisfy
//     isFreezeCaptured() on every voice while still measuring the unfrozen
//     cost - the exact defect the assertion exists to prevent, one level down.
//     SC-001 therefore ENGAGES the technique the way Phase 5 measured its own
//     frozen row: captureFreeze() *and* setFreezeMix(1.0)
//     (atmosphere_engine_perf_test.cpp:1176-1177), pushed through
//     SeraphisVoice's FR-030 forwarder, and getFreezeMix() is asserted alongside
//     isFreezeCaptured().
//   - AetherReverb at RA-1 row (c): numChannels = 16, shimmer + bloom + spectral
//     diffusion all on, diffusionFftSize = 4096, setSize(1), setDensity(1), 32
//     bloom resonators driven - matching aether_reverb_perf_test.cpp:329-330.
//   - measured on THE COMPOSED CHAIN:
//       SeraphisEngine::processStereoBlock
//         -> AetherReverb::processStereoBlock
//         -> SeraphisEngine::processOutputStage
//
//   WHY THE TIMED DRIVER IS THOSE THREE CALLS AND NOT renderSeraphisChain().
//   The helper (tests/test_helpers/seraphis_chain.h:213) calls
//   `macros.apply(engine)` on EVERY sub-slice, and the FR-058 table's
//   CloudRichness row writes its base of 0.60 (seraphis_macro_matrix.h:246-250).
//   N(r) = round(64^r), so 0.60 is 12 active partials, not 64 - the helper would
//   therefore overwrite the cloud pin on the first slice and time a
//   configuration that is NOT RA-1's. Its per-slice event bookkeeping, bloom
//   polling and std::vector scratch are also caller cost, not engine cost. The
//   three calls below ARE the chain SC-001 names; the macro matrix is applied
//   once, at neutral, before the pins, which is legal precisely because FR-059
//   makes apply() idempotent.
//
//   WHY isFreezeCaptured() IS ASSERTED ON THE 8 SOUNDING SLOTS AND NOT ALL 16.
//   setAtmosphereFreeze(true) arms all kMaxVoices slots (seraphis_engine.h:554)
//   and one is retried per control chunk, but a capture needs that voice's
//   ROLLING CAPTURE RING to hold a whole analysis window, and the ring is only
//   written by a voice that RENDERS. Slots 8..15 take advanceLifeOnly() in this
//   scenario (seraphis_engine.h:466), which writes no audio anywhere
//   (seraphis_voice.h:977-990), so their rings stay empty and their captures
//   stay no-ops for the whole run. Asserting on all 16 would fail by
//   construction and would say nothing about the path being measured.
//
// STACK RULE (plan §6.3): sizeof(SeraphisEngine) is ~772 KB against MSVC's 1 MiB
//   default main-thread stack, so EVERY SeraphisEngine and EVERY AetherReverb
//   here is std::make_unique'd. Never a plain local. A standalone SeraphisVoice
//   (~47 KB) is heap-allocated too, for the same reason applied with margin.
//
// FTZ/DAZ: dsp/tests/dsp_test_main.cpp calls enableFTZDAZ() before any case
//   runs, so every figure below is measured with denormals flushed BY THE
//   PROCESS - the environment the audio thread runs in.
//
// NO ALLOCATION-TRACKING INCLUDES HERE: this TU must not pull in
//   <allocation_operator_overrides.h> (duplicate-symbol link error against the
//   single owner in this binary). SC-007's allocation sweep lives in
//   seraphis_engine_test.cpp.
// ==============================================================================

#include <catch2/catch_all.hpp>

#include <krate/dsp/core/env_curve.h>
#include <krate/dsp/core/random.h>
#include <krate/dsp/effects/aether_reverb.h>
#include <krate/dsp/primitives/envelope_utils.h>
#include <krate/dsp/processors/growth_envelope.h>
#include <krate/dsp/processors/midside_processor.h>
#include <krate/dsp/processors/multi_stage_envelope.h>
#include <krate/dsp/processors/orbit_modulator.h>
#include <krate/dsp/processors/spectral_state.h>
#include <krate/dsp/systems/atmosphere_engine.h>
#include <krate/dsp/systems/continuous_body.h>
#include <krate/dsp/systems/harmonic_cloud.h>
#include <krate/dsp/systems/seraphis_engine.h>
#include <krate/dsp/systems/seraphis_macro_matrix.h>
#include <krate/dsp/systems/seraphis_voice.h>
#include <krate/dsp/systems/spectral_morph_engine.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <sstream>
#include <string>

using Krate::DSP::AetherReverb;
using Krate::DSP::AtmosphereEngine;
using Krate::DSP::ContinuousBody;
using Krate::DSP::deriveStreamSeed;
using Krate::DSP::GrowthEnvelope;
using Krate::DSP::HarmonicCloud;
using Krate::DSP::makeFactoryState;
using Krate::DSP::MidSideProcessor;
using Krate::DSP::MultiStageEnvelope;
using Krate::DSP::OrbitModulator;
using Krate::DSP::PitchMode;
using Krate::DSP::RetriggerMode;
using Krate::DSP::SeraphisEngine;
using Krate::DSP::SeraphisEngineConfig;
using Krate::DSP::SeraphisMacroMatrix;
using Krate::DSP::SeraphisMacroValues;
using Krate::DSP::SeraphisVoice;
using Krate::DSP::SeraphisVoiceConfig;
using Krate::DSP::SpectralMorphEngine;
using Krate::DSP::SpectralStateId;

namespace {

// =============================================================================
// Measurement basis
// =============================================================================

constexpr double kSr48 = 48000.0;
constexpr std::size_t kBlockSize = 512;

/// Wall-clock budget of one 512-sample block at 48 kHz, in nanoseconds.
/// 10 666 666.7 ns - the constant every Seraphis perf TU derives.
constexpr double kBlockBudgetNs = (static_cast<double>(kBlockSize) / kSr48) * 1.0e9;

/// The roadmap line-311 ceiling, KEPT IN FULL by RQ-1: 25 % of one core.
/// 2 666 666.7 ns/block.
constexpr double kReferenceNs = kBlockBudgetNs * 0.25;

/// THE RUN-TIME GATE and the static_assert multiplier. See the "TWO FACTORS"
/// block in this file's banner for why it is 1.15 and not 1.5 or 1.05.
constexpr double kRegressionFactor = 1.15;

/// RECORDING CONVENTION ONLY - applied once when a measurement is transcribed
/// into a baseline (ceil(worst x kBaselineHeadroom)). It appears in NO
/// assertion; it is named so it can never be mistaken for the gate.
constexpr double kBaselineHeadroom = 1.05;

/// The largest baseline the static_asserts can accept: 25 % / 1.15 = 21.74 % of
/// the block budget. A measurement above this means the phase is OVER BUDGET and
/// the response is the two-item lever list in the banner, NEVER a raised
/// baseline.
constexpr double kMaxAdmissibleNs = kReferenceNs / kRegressionFactor;

/// SC-001's checked-in baseline. **MEASURED 2026-07-31** - see BASELINE
/// PROVENANCE for the ten-run dataset. ceil(worst x kBaselineHeadroom) =
/// ceil(2 140 610 x 1.05) = 21.07 % of one core.
constexpr double kBaselineFullPolyNsPerBlock = 2247641.0;

static_assert(kBaselineFullPolyNsPerBlock * kRegressionFactor <= kReferenceNs,
              "SC-001: the baseline must be no weaker than the 25 % reference - if this fails, "
              "re-derive the shipped voice count (RQ-1) or reduce Phase 7's own composition cost; "
              "never a Phase 2/4/5/6 gate (N-10), never the ceiling, never kRegressionFactor");
static_assert(kBaselineFullPolyNsPerBlock <= kMaxAdmissibleNs,
              "SC-001: a baseline above kMaxAdmissibleNs (21.74 % of one core) means the phase is "
              "over budget - work the lever list, never raise the baseline");
/// The WORST of the ten-run dataset in BASELINE PROVENANCE. Named so the
/// recording convention stays a single source of truth rather than a number that
/// only lives in a comment - the derived stand-in it replaces
/// (0.2036 x kBlockBudgetNs x kBaselineHeadroom) is gone, as step 5 requires.
constexpr double kMeasuredWorstFullPolyNs = 2140610.0;

/// A 0.1 % band rather than equality: the product is not exactly representable
/// in binary floating point, and the clause is here to catch a baseline that
/// DRIFTED away from the recorded measurement, not to pin its last ULP.
static_assert(kBaselineFullPolyNsPerBlock
                      >= kMeasuredWorstFullPolyNs * kBaselineHeadroom * 0.999
                  && kBaselineFullPolyNsPerBlock
                         <= kMeasuredWorstFullPolyNs * kBaselineHeadroom * 1.001,
              "SC-001: the shipped baseline must be ceil(the recorded worst x kBaselineHeadroom) - "
              "re-run the eight-run procedure in this file's banner and record the new dataset "
              "before touching it");
/// The measurement itself must clear the 25 % ceiling with the regression factor
/// applied, which is the arithmetic step 5 turns into "the phase is over budget".
static_assert(kMeasuredWorstFullPolyNs * kBaselineHeadroom <= kMaxAdmissibleNs,
              "SC-001: ceil(measured worst x 1.05) is above kMaxAdmissibleNs (21.74 % of one "
              "core) - the phase is OVER BUDGET. Work the lever list (shipped voice count, or "
              "Phase 7's own composition cost); never raise the baseline");

/// SC-002's bound: one SeraphisVoice::processStereoBlock costs at most 110 % of
/// the arithmetic sum of its eight standalone sub-components. The 10 % allowance
/// covers the carry-FIFO copies, the per-sample envelope multiply, the spatial
/// stage's two multiplies and the morph->cloud handoff, and nothing else.
constexpr double kCompositionOverheadBound = 1.10;

/// FR-040's shipped default polyphony, and the count RQ-1 selected from RA-1's
/// table. This is SC-001's scenario, not a tuning knob.
constexpr std::size_t kPolyphony = 8;

// --- Structural clauses: what these numbers describe -------------------------
// If any of these moves, the measurement no longer describes what SC-001 /
// SC-002 specified and the TU stops compiling rather than silently reporting a
// figure for a different configuration.

static_assert(SeraphisEngine::kMaxVoices == 16,
              "RQ-1 compiles a capacity of 16 so a re-derived shipped voice count costs no ABI "
              "change; SC-001 gates at the shipped DEFAULT of 8");
static_assert(kPolyphony <= SeraphisEngine::kMaxVoices, "the scenario must fit the pool");
static_assert(SeraphisVoice::kControlChunkSamples == 64,
              "FR-007's absolute control grid; the per-chunk cadences below mirror it");
static_assert(SeraphisEngine::kControlChunkSamples == SeraphisVoice::kControlChunkSamples,
              "the engine and the voice must share one control grid");
static_assert(kBlockSize % SeraphisVoice::kControlChunkSamples == 0,
              "the measured block must be a whole number of control chunks");
static_assert(AetherReverb::kMaxBloomResonators == 32,
              "RA-1 row (c) drives the kMaxBloomResonators ceiling");
static_assert(SeraphisEngine::kBloomPartialCap
                  == static_cast<std::size_t>(AetherReverb::kMaxBloomResonators),
              "FR-071's Layer-3 duplicate of the Layer-4 cap must still agree with it");
static_assert(HarmonicCloud::kMaxPartials == 64,
              "RA-1's cloud row is the 64-partial configuration");
static_assert(ContinuousBody::kNumMaterials == 5,
              "SC-001 measures all five materials and uses the worst");

// =============================================================================
// Trial shape
// =============================================================================
// Best-of-N: the minimum is the least OS-noise-contaminated estimate of the real
// cost, which is what a regression bound wants.
//
// MANY SHORT TRIALS, copied from continuous_body_perf_test.cpp:288-313 and
// aether_reverb_perf_test.cpp:398-416. The dev machine is a HYBRID part: the
// dominant noise source is not jitter smeared across a trial, it is the WHOLE
// TRIAL being migrated onto an E-core, a ~20 % step that best-of-N cannot reject
// when N is small and each trial is long enough to be migrated. Short trials
// make it likely at least one runs start-to-finish on a boosted P-core, which is
// the figure the baseline wants to describe. Affinity pinning was tried and
// REJECTED in harmonic_cloud_perf_test.cpp: not portable, and on a hybrid part
// it selects a core by index without knowing its type.
//
// SC-001 and SC-002 both specify ">= 8 trials"; 16 is used, comfortably above
// the floor while keeping the lane's wall clock sane.

constexpr int kChainTrials = 16;
constexpr int kChainBlocksPerTrial = 100;  ///< ~1.07 s of audio per trial
/// ~6.4 s: past the atmosphere's 4 s capture ring, the body's crossfade, the
/// cloud's attack, the reverb build-up and every smoother in the chain.
constexpr int kChainWarmupBlocks = 600;

constexpr int kSubjectTrials = 16;
constexpr int kSubjectBlocksPerTrial = 200;
/// ~4.3 s, enough for the atmosphere ring and the body's cloud to reach steady
/// state on every subject.
constexpr int kSubjectWarmupBlocks = 400;

/// Material-selection probe: cheap, because it only has to RANK five figures.
constexpr int kMaterialTrials = 8;
constexpr int kMaterialBlocksPerTrial = 200;
constexpr int kMaterialWarmupBlocks = 200;

/// R14: how many arm/capture cycles the freeze-retry burst is measured over, and
/// the safety stop on one cycle (the fan-out completes in ~16 control chunks).
constexpr int kFreezeCycles = 5;
constexpr int kFreezeChunkCap = 4096;

/// Finite check WITHOUT std::isnan: the macOS leg builds with -ffast-math, which
/// folds it. Inspect the IEEE-754 exponent field instead.
[[nodiscard]] bool isFiniteValue(float v) noexcept
{
    std::uint32_t bits = 0;
    std::memcpy(&bits, &v, sizeof(bits));
    return (bits & 0x7F800000u) != 0x7F800000u;
}

/// Best-of-N driver. `runBlock` performs exactly one 512-sample block of work.
/// Taken by const reference, not by forwarding reference: it is INVOKED, many
/// times, never consumed, so there is nothing to forward.
template <typename BlockFn>
[[nodiscard]] double bestNsPerBlock(int trials, int blocksPerTrial, const BlockFn& runBlock)
{
    double best = std::numeric_limits<double>::max();
    for (int trial = 0; trial < trials; ++trial) {
        const auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < blocksPerTrial; ++i) {
            runBlock();
        }
        const auto end = std::chrono::steady_clock::now();

        const double elapsedNs = std::chrono::duration<double, std::nano>(end - start).count();
        best = std::min(best, elapsedNs / static_cast<double>(blocksPerTrial));
    }
    return best;
}

/// Non-const access to one pooled voice.
///
/// SeraphisEngine deliberately exposes no non-const voice accessor - FR-085's
/// getVoice() is a const reference and the only write path is
/// `friend class SeraphisMacroMatrix` (seraphis_engine.h:738) - while SC-001's
/// scenario pins per-voice FR-030 forwarders (the cloud's 64 partials + drift
/// and the worst body material configuration) that the macro table does not
/// reach. The engine object itself is non-const, so the cast is well defined; it
/// is confined to this one helper rather than sprinkled through the cases. Same
/// helper, same reasoning, as seraphis_engine_test.cpp:199-202.
[[nodiscard]] SeraphisVoice& mutableVoice(SeraphisEngine& engine, std::size_t v)
{
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast) - see the comment above
    return const_cast<SeraphisVoice&>(engine.getVoice(v));
}

// =============================================================================
// Buffers and deterministic excitation
// =============================================================================

/// One block of stereo scratch. Separate arrays: the perf lane never relies on
/// in-place support (ContinuousBody explicitly forbids it,
/// continuous_body.h:1155-1156).
struct Buffers {
    std::array<float, kBlockSize> inLeft{};
    std::array<float, kBlockSize> inRight{};
    std::array<float, kBlockSize> outLeft{};
    std::array<float, kBlockSize> outRight{};
};

/// Deterministic decorrelated stereo noise at ~-12 dBFS, built once and replayed
/// every block. Only the SC-002 sub-components that take audio input use it
/// (ContinuousBody, AtmosphereEngine, MidSideProcessor); SeraphisEngine,
/// SeraphisVoice and HarmonicCloud are generators.
///
/// Xorshift32 rather than <random> so the sequence is identical on every
/// toolchain (std::uniform_real_distribution is not portable).
void fillExcitation(Buffers& buf) noexcept
{
    std::uint32_t state = 0x9E3779B9u;
    const auto next = [&state]() noexcept {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        // [-0.25, 0.25): 24 mantissa bits scaled, no division by a magic float.
        return (static_cast<float>(state >> 8) * (1.0f / 16777216.0f) - 0.5f) * 0.5f;
    };
    for (std::size_t i = 0; i < kBlockSize; ++i) {
        buf.inLeft[i] = next();
        buf.inRight[i] = next();
    }
}

// =============================================================================
// The pinned body configuration (SC-001 / SC-002's "worst material")
// =============================================================================
// The values are continuous_body_perf_test.cpp:427-443's configureBody verbatim
// - the configuration behind the 55 094.6 ns/block figure RA-1 carries from
// specs/seraphis-phase4-continuous-body/compliance.md:19. They are PINNED rather
// than inherited from FR-019, exactly as spec.md:640 requires ("SC-001/SC-002
// pin the worst MEASURED material configuration explicitly rather than
// inheriting it"), which is why two of them (damping 0.20 vs FR-019's 0.25,
// cloudMix 0.50 vs 0.25) deliberately differ from the shipped voice defaults.

constexpr float kBodyNoteHz = 220.0f;
constexpr float kBodyKeyTracking = 1.0f;
constexpr float kBodyResonance = 0.8f;
constexpr float kBodyDamping = 0.2f;
constexpr float kBodyDrive = 1.0f;
constexpr float kBodyMix = 1.0f;
constexpr float kBodyCloudMix = 0.5f;  ///< > 0: FR-053a's bypass must NOT engage
constexpr float kBodyCloudDecaySec = 4.0f;
constexpr float kBodyCloudSize = 1.0f;
constexpr float kBodyCloudDamping = 0.3f;
constexpr float kBodyWidth = 1.0f;

// Double-braced (the continuous_body.h:506-508 idiom): std::array wraps a
// C-array member, and the explicit inner brace keeps Clang's -Wmissing-braces
// silent. Order MUST match BodyMaterial's enumerator order.
constexpr std::array<ContinuousBody::BodyMaterial, ContinuousBody::kNumMaterials> kMaterials = {{
    ContinuousBody::BodyMaterial::Glass,
    ContinuousBody::BodyMaterial::Strings,
    ContinuousBody::BodyMaterial::MetalPlate,
    ContinuousBody::BodyMaterial::Chamber,
    ContinuousBody::BodyMaterial::Ice,
}};

constexpr std::array<const char*, ContinuousBody::kNumMaterials> kMaterialNames = {{
    "Glass", "Strings", "MetalPlate", "Chamber", "Ice",
}};

/// A STANDALONE ContinuousBody in the pinned configuration.
/// setMaterial precedes prepare(), as continuous_body_perf_test.cpp:429-430 does,
/// so no crossfade is armed and the figure is a steady-state one.
void configurePinnedBody(ContinuousBody& body, ContinuousBody::BodyMaterial m) noexcept
{
    body.setMaterial(m);
    body.prepare(kSr48);

    body.setNoteFrequencyHz(kBodyNoteHz);
    body.setKeyTracking(kBodyKeyTracking);
    body.setResonance(kBodyResonance);
    body.setDamping(kBodyDamping);
    body.setDrive(kBodyDrive);
    body.setMix(kBodyMix);
    body.setCloudMix(kBodyCloudMix);
    body.setCloudDecaySec(kBodyCloudDecaySec);
    body.setCloudSize(kBodyCloudSize);
    body.setCloudDamping(kBodyCloudDamping);
    body.setWidth(kBodyWidth);
}

/// The same configuration pushed through SeraphisVoice's FR-030 body forwarders
/// (seraphis_voice.h:652-662). The note frequency is NOT set here - it arrives
/// via noteOn(), which is the only path that retunes both the body and the cloud
/// (seraphis_voice.h:507-508).
void applyPinnedBody(SeraphisVoice& voice, ContinuousBody::BodyMaterial m) noexcept
{
    voice.setMaterial(m);
    voice.setKeyTracking(kBodyKeyTracking);
    voice.setResonance(kBodyResonance);
    voice.setDamping(kBodyDamping);
    voice.setDrive(kBodyDrive);
    voice.setMix(kBodyMix);
    voice.setCloudMix(kBodyCloudMix);
    voice.setCloudDecaySec(kBodyCloudDecaySec);
    voice.setCloudSize(kBodyCloudSize);
    voice.setCloudDamping(kBodyCloudDamping);
    voice.setWidth(kBodyWidth);
}

/// Steady-state ns/block for one material, standalone.
[[nodiscard]] double measureMaterialNsPerBlock(ContinuousBody::BodyMaterial m, double& sinkOut)
{
    auto body = std::make_unique<ContinuousBody>();
    configurePinnedBody(*body, m);

    Buffers buf;
    fillExcitation(buf);
    double sink = 0.0;

    // Reading two samples per block is what stops the optimizer dead-coding the
    // render away; a real consumer reads the whole buffer.
    const auto renderBlock = [&]() noexcept {
        body->processStereoBlock(buf.inLeft.data(), buf.inRight.data(), buf.outLeft.data(),
                                 buf.outRight.data(), kBlockSize);
        sink += static_cast<double>(buf.outLeft[0])
                + static_cast<double>(buf.outRight[kBlockSize - 1]);
    };

    for (int i = 0; i < kMaterialWarmupBlocks; ++i) {
        renderBlock();
    }
    const double ns = bestNsPerBlock(kMaterialTrials, kMaterialBlocksPerTrial, renderBlock);
    sinkOut += sink;
    return ns;
}

/// All five figures plus the argmax - the continuous_body_perf_test.cpp:936-940
/// idiom. The worst material is CHOSEN FROM THE MEASUREMENT rather than by hand,
/// so the scenario can never be pointed at an accidentally cheap case.
struct MaterialSurvey {
    std::array<double, ContinuousBody::kNumMaterials> nsPerBlock{};
    std::size_t worstIndex = 0;
    double sink = 0.0;
};

[[nodiscard]] MaterialSurvey surveyMaterials()
{
    MaterialSurvey out{};
    for (std::size_t i = 0; i < ContinuousBody::kNumMaterials; ++i) {
        out.nsPerBlock[i] = measureMaterialNsPerBlock(kMaterials[i], out.sink);
    }
    const auto worst = std::max_element(out.nsPerBlock.begin(), out.nsPerBlock.end());
    out.worstIndex = static_cast<std::size_t>(worst - out.nsPerBlock.begin());
    return out;
}

// =============================================================================
// The pinned cloud configuration (RA-1's "64 partials + drift" row)
// =============================================================================
// harmonic_cloud_perf_test.cpp:257-264's configureCloud, i.e. the SC-007
// configuration the 29 642.8 ns/block figure was measured in: N(1) = 64
// partials, mutation at its ceiling, drift depth at kMaxDriftCents so the
// per-partial ratio path is genuinely on the hot path (a depth of 0 multiplies
// by a constant 1), stereo spread at 1 so the equal-power placement is live.
//
// These four are pinned AFTER SeraphisMacroMatrix::apply(): the FR-058 table
// writes CloudRichness = 0.60 (12 partials), CloudDriftDepthCents = 0 and
// CloudStereoSpread = 0.35 from its bases, so applying the matrix again inside
// the timed region would silently demote the configuration.

constexpr float kPinRichness = 1.0f;   ///< N(1) = round(64^1) = 64 partials
constexpr float kPinMutation = 1.0f;
constexpr float kPinStereoSpread = 1.0f;

void applyPinnedCloud(SeraphisVoice& voice) noexcept
{
    voice.setRichness(kPinRichness);
    voice.setMutation(kPinMutation);
    voice.setDriftDepthCents(HarmonicCloud::kMaxDriftCents);
    voice.setStereoSpread(kPinStereoSpread);
}

// =============================================================================
// AetherReverb configuration (c)
// =============================================================================

/// RA-1 row (c) / aether_reverb_perf_test.cpp:623-641's specWorst, at the block
/// size this chain is measured on.
[[nodiscard]] AetherReverb::PrepareConfig aetherConfigC() noexcept
{
    // Designated initialisers only - no narrowing in brace init.
    return AetherReverb::PrepareConfig{
        .numChannels = std::size_t{16},
        .maxBlockSamples = kBlockSize,
        .maxDelaySeconds = 0.50f,
        .shimmerEnabled = true,
        .shimmerMode = PitchMode::Granular,
        .bloomEnabled = true,
        .spectralDiffusionEnabled = true,
        .diffusionFftSize = std::size_t{4096},
        .seed = std::uint32_t{1},
    };
}

/// A harmonic series on 110 Hz into one bloom voice - aether_reverb_perf_test.cpp
/// :531-540's driveBloom. 32 partials tops out at 3520 Hz, well inside the
/// kBloomMaxFreqFraction clamp, so no partial folds onto another and the bank
/// really does hold 32 distinct resonators.
void driveBloom(AetherReverb& reverb) noexcept
{
    std::array<float, static_cast<std::size_t>(AetherReverb::kMaxBloomResonators)> partials{};
    for (std::size_t i = 0; i < partials.size(); ++i) {
        partials[i] = 110.0f * static_cast<float>(i + 1u);
    }
    reverb.bloomNoteOn(std::int32_t{0}, partials.data(), partials.size());
}

/// prepare() + the control history that defines configuration (c). Everything
/// not set here stays at its FR-009 default on purpose.
void buildAetherC(AetherReverb& reverb) noexcept
{
    reverb.prepare(kSr48, aetherConfigC());
    reverb.setSize(1.0f);     // FR-012: the longest lines the geometry can reach
    reverb.setDensity(1.0f);  // FR-040: every DiffusionNetwork stage enabled
    reverb.setShimmerOctaveSend(1.0f);
    reverb.setShimmerFifthSend(1.0f);
    reverb.setBloomSend(1.0f);
    reverb.setBloomDecay(1.0f);
    reverb.setSpectralDiffusion(0.5f);
    driveBloom(reverb);
}

// =============================================================================
// Engaging the freeze technique
// =============================================================================

/// The mix half of "atmosphere frozen", pushed through SeraphisVoice's FR-030
/// forwarder on every slot.
///
/// WITHOUT THIS THE WHOLE FROZEN SCENARIO IS A NO-OP AT THE COST LEVEL. FR-019
/// ships setFreezeMix(0), and renderFreezeLayer's `settledDry` branch
/// (atmosphere_engine.h:2149-2152) skips BOTH SpectralFreezeOscillator::processBlock
/// calls whenever the ramp has settled on a target of 0 - so a captured freeze at
/// the default mix costs nothing and isFreezeCaptured() would still be true.
/// Phase 5 measured its own frozen row as captureFreeze() + setFreezeMix(1.0)
/// (atmosphere_engine_perf_test.cpp:1176-1177), and that is the row RA-1 carries
/// at 1.440 %/voice.
///
/// It is ALSO what lets a release complete: SpectralFreezeOscillator::unfreeze()
/// only ARMS a one-hop fade (:306-311) and clears frozen_ inside processBlock
/// (:365), which the settledDry branch never calls. At mix 0 a released voice
/// would report isFreezeCaptured() forever.
void engageFreezeMix(SeraphisEngine& engine) noexcept
{
    for (std::size_t v = 0; v < SeraphisEngine::kMaxVoices; ++v) {
        mutableVoice(engine, v).setFreezeMix(1.0f);  // atmosphere_engine.h:882
    }
}

// =============================================================================
// The SC-001 subject
// =============================================================================

/// Everything SC-001's scenario needs, assembled in the order the criterion
/// states it. The engine and the reverb are heap-allocated (plan §6.3).
struct ChainSubject {
    std::unique_ptr<SeraphisEngine> engine;
    std::unique_ptr<AetherReverb> reverb;
    SeraphisMacroMatrix macros{};
};

/// @param worstMaterial the argmax of surveyMaterials(), never a hand-picked one.
[[nodiscard]] ChainSubject buildChainSubject(ContinuousBody::BodyMaterial worstMaterial)
{
    ChainSubject s{};
    s.engine = std::make_unique<SeraphisEngine>();
    s.reverb = std::make_unique<AetherReverb>();

    // 1. prepare at the shipped polyphony and the FR-014 shipped voice config.
    s.engine->prepare(kSr48, SeraphisEngineConfig{.voice = SeraphisVoiceConfig{},
                                                  .polyphony = kPolyphony,
                                                  .seed = std::uint32_t{1}});

    // 2. all five macros at their FR-060 neutral, applied ONCE. A default
    //    SeraphisMacroValues IS the neutral (dream/bloom/dissolve/entropy 0,
    //    gravity 0.5, seraphis_macro_matrix.h:122-128); it is written out anyway
    //    so the scenario reads as the criterion states it.
    s.macros.setMacros(SeraphisMacroValues{});
    s.macros.apply(*s.engine);

    // 3. the two RA-1 rows the macro table does not reach, pinned AFTER apply().
    for (std::size_t v = 0; v < SeraphisEngine::kMaxVoices; ++v) {
        SeraphisVoice& voice = mutableVoice(*s.engine, v);
        applyPinnedCloud(voice);
        applyPinnedBody(voice, worstMaterial);
    }

    // 4. all kPolyphony voices sounding, none idle: distinct notes around A3
    //    (MIDI 57 = 220 Hz), so the allocator hands out one slot each rather
    //    than retriggering one (voice_allocator.h:237-244). No note-off is ever
    //    issued.
    for (std::size_t v = 0; v < kPolyphony; ++v) {
        s.engine->noteOn(static_cast<std::uint8_t>(57u + v), std::uint8_t{100});
    }

    // 5. the Layer 4 half, at RA-1 row (c).
    buildAetherC(*s.reverb);
    return s;
}

// =============================================================================
// The SC-002 subjects
// =============================================================================
// Eight standalone sub-components plus the composed SeraphisVoice, all in the
// pinned shared configuration: 64 partials + drift, FR-019 shipped voice
// defaults elsewhere, worst body material, atmosphere at default density and
// UNFROZEN, Standard envelope gated on, spatial depth 0.5, 512-sample blocks at
// 48 kHz.
//
// Every standalone configurator below mirrors SeraphisVoice::prepare's FR-019
// block (seraphis_voice.h:268-343) call for call, because the voice's own
// sub-components are private and the denominator has to describe the SAME
// configuration as the numerator.

constexpr float kSc002SpatialDepth = 0.5f;   ///< SC-002's pinned spatial depth
constexpr std::uint32_t kSc002Seed = 1u;     ///< arbitrary; cost is seed-independent
constexpr std::size_t kChunk = SeraphisVoice::kControlChunkSamples;
constexpr std::size_t kChunksPerBlock = kBlockSize / kChunk;

/// FR-019's envelope block (seraphis_voice.h:320-342), gated on.
void configureStandardEnvelope(MultiStageEnvelope& mse) noexcept
{
    mse.prepare(static_cast<float>(kSr48));
    mse.setNumStages(SeraphisVoice::kEnvelopeStages);
    mse.setSustainPoint(SeraphisVoice::kEnvelopeSustainPoint);
    mse.setStage(0, 1.0f, 2000.0f, SeraphisVoice::kStageCurve);
    mse.setStage(1, 0.7f, 4000.0f, SeraphisVoice::kStageCurve);
    mse.setStage(2, 0.7f, 0.0f, SeraphisVoice::kStageCurve);
    mse.setStage(3, 0.0f, 0.0f, SeraphisVoice::kStageCurve);
    mse.setReleaseTime(8000.0f);
    mse.setRetriggerMode(RetriggerMode::Legato);
    mse.gate(true);
}

/// The SC-002 SeraphisVoice: FR-019 defaults from prepare(), then the same two
/// pins SC-001 applies, spatial depth 0.5, and a note at kBodyNoteHz.
[[nodiscard]] std::unique_ptr<SeraphisVoice> buildVoiceSubject(
    ContinuousBody::BodyMaterial worstMaterial)
{
    auto voice = std::make_unique<SeraphisVoice>();
    voice->setSeed(kSc002Seed);
    voice->prepare(kSr48, SeraphisVoiceConfig{});
    applyPinnedCloud(*voice);
    applyPinnedBody(*voice, worstMaterial);
    voice->setSpatialDepth(kSc002SpatialDepth);
    voice->noteOn(kBodyNoteHz, 1.0f);
    return voice;
}

/// A per-subject measurement plus the non-vacuity data the case asserts on.
struct SubjectResult {
    double nsPerBlock = 0.0;
    double sink = 0.0;
};

[[nodiscard]] SubjectResult measureVoiceSubject(ContinuousBody::BodyMaterial worstMaterial)
{
    auto voice = buildVoiceSubject(worstMaterial);
    Buffers buf;
    double sink = 0.0;

    const auto renderBlock = [&]() noexcept {
        voice->processStereoBlock(buf.outLeft.data(), buf.outRight.data(), kBlockSize);
        sink += static_cast<double>(buf.outLeft[0])
                + static_cast<double>(buf.outRight[kBlockSize - 1]);
    };
    for (int i = 0; i < kSubjectWarmupBlocks; ++i) {
        renderBlock();
    }
    return SubjectResult{bestNsPerBlock(kSubjectTrials, kSubjectBlocksPerTrial, renderBlock), sink};
}

/// (1) HarmonicCloud, per-chunk cadence, generator.
[[nodiscard]] SubjectResult measureCloudSubject()
{
    auto cloud = std::make_unique<HarmonicCloud>();
    cloud->prepare(kSr48);
    cloud->setSeed(deriveStreamSeed(kSc002Seed, SeraphisVoice::kCloudSalt));
    // FR-019 defaults (seraphis_voice.h:269-277) with the SC-001/SC-002 pins on
    // top - richness, mutation, drift and spread.
    cloud->setRichness(kPinRichness);
    cloud->setInharmonicity(0.030f);
    cloud->setSpectralTiltDb(0.0f);
    cloud->setMutation(kPinMutation);
    cloud->setSpectralGravity(0.20f);
    cloud->setDriftDepthCents(HarmonicCloud::kMaxDriftCents);
    cloud->setStereoSpread(kPinStereoSpread);
    cloud->setAttackTimeSec(0.05f);
    cloud->setDecayTimeSec(0.5f);
    cloud->setFundamentalHz(kBodyNoteHz);
    cloud->noteOn();

    Buffers buf;
    double sink = 0.0;
    const auto renderBlock = [&]() noexcept {
        for (std::size_t c = 0; c < kChunksPerBlock; ++c) {
            const std::size_t off = c * kChunk;
            cloud->processStereoBlock(buf.outLeft.data() + off, buf.outRight.data() + off, kChunk);
        }
        sink += static_cast<double>(buf.outLeft[0])
                + static_cast<double>(buf.outRight[kBlockSize - 1]);
    };
    for (int i = 0; i < kSubjectWarmupBlocks; ++i) {
        renderBlock();
    }
    return SubjectResult{bestNsPerBlock(kSubjectTrials, kSubjectBlocksPerTrial, renderBlock), sink};
}

/// (2) SpectralMorphEngine (which carries EntropyProcessor). One updateChunk per
/// control chunk, the cadence seraphis_voice.h:887 drives.
[[nodiscard]] SubjectResult measureMorphSubject()
{
    auto morph = std::make_unique<SpectralMorphEngine>();
    morph->prepare(kSr48);
    morph->setSeed(deriveStreamSeed(kSc002Seed, SeraphisVoice::kMorphSalt));
    // FR-019a's two-state set plus FR-019's morph rows (seraphis_voice.h:256-282).
    morph->setState(0, makeFactoryState(SpectralStateId::SineStack));
    morph->setState(1, makeFactoryState(SpectralStateId::Glass));
    morph->setStateCount(2);
    morph->setTravelMode(SpectralMorphEngine::TravelMode::External);
    morph->setTargetPosition(0.0f);
    morph->setEntropy(0.20f);
    morph->setBloom(0.0f);
    morph->setTravelRate(SpectralMorphEngine::kMinTravelRate);

    double sink = 0.0;
    const auto renderBlock = [&]() noexcept {
        for (std::size_t c = 0; c < kChunksPerBlock; ++c) {
            morph->updateChunk(kChunk);
        }
        // Reading the output is what a real consumer does (the voice hands it
        // straight to setSpectralTarget) and it stops the optimizer dead-coding
        // the pipeline away.
        const float* amps = morph->getOutputAmplitudes();
        sink += static_cast<double>(amps[0]);
    };
    for (int i = 0; i < kSubjectWarmupBlocks; ++i) {
        renderBlock();
    }
    return SubjectResult{bestNsPerBlock(kSubjectTrials, kSubjectBlocksPerTrial, renderBlock), sink};
}

/// (3) ContinuousBody, per-chunk cadence, in the pinned worst-material config.
[[nodiscard]] SubjectResult measureBodySubject(ContinuousBody::BodyMaterial worstMaterial)
{
    auto body = std::make_unique<ContinuousBody>();
    configurePinnedBody(*body, worstMaterial);
    body->setSeed(deriveStreamSeed(kSc002Seed, SeraphisVoice::kBodySalt));

    Buffers buf;
    fillExcitation(buf);
    double sink = 0.0;
    const auto renderBlock = [&]() noexcept {
        for (std::size_t c = 0; c < kChunksPerBlock; ++c) {
            const std::size_t off = c * kChunk;
            body->processStereoBlock(buf.inLeft.data() + off, buf.inRight.data() + off,
                                     buf.outLeft.data() + off, buf.outRight.data() + off, kChunk);
        }
        sink += static_cast<double>(buf.outLeft[0])
                + static_cast<double>(buf.outRight[kBlockSize - 1]);
    };
    for (int i = 0; i < kSubjectWarmupBlocks; ++i) {
        renderBlock();
    }
    return SubjectResult{bestNsPerBlock(kSubjectTrials, kSubjectBlocksPerTrial, renderBlock), sink};
}

/// (4) AtmosphereEngine, per-chunk cadence, at the FR-014 shipped capture
/// configuration and the FR-019 control values, UNFROZEN.
[[nodiscard]] SubjectResult measureAtmosphereSubject()
{
    const SeraphisVoiceConfig vc{};  // the FR-014 shipped voice config
    auto atmos = std::make_unique<AtmosphereEngine>();
    atmos->prepare(kSr48, AtmosphereEngine::PrepareConfig{.captureSeconds = vc.captureSeconds,
                                                          .blurEnabled = vc.blurEnabled,
                                                          .freezeEnabled = vc.freezeEnabled,
                                                          .blurFftSize = vc.blurFftSize,
                                                          .freezeFftSize = vc.freezeFftSize,
                                                          .maxBlockSamples = vc.maxBlockSamples});
    atmos->setSeed(deriveStreamSeed(kSc002Seed, SeraphisVoice::kAtmosSalt));
    atmos->setLevel(0.5f);
    atmos->setBlur(0.0f);
    atmos->setDensity(4.0f);
    atmos->setGrainSeconds(4.0f);
    atmos->setDriftDepth(0.3f);
    atmos->setPanSpread(0.7f);
    atmos->setDecorrelation(0.5f);
    atmos->setFreezeMix(0.0f);

    Buffers buf;
    fillExcitation(buf);
    double sink = 0.0;
    const auto renderBlock = [&]() noexcept {
        for (std::size_t c = 0; c < kChunksPerBlock; ++c) {
            const std::size_t off = c * kChunk;
            atmos->processStereoBlock(buf.inLeft.data() + off, buf.inRight.data() + off,
                                      buf.outLeft.data() + off, buf.outRight.data() + off, kChunk);
        }
        sink += static_cast<double>(buf.outLeft[0])
                + static_cast<double>(buf.outRight[kBlockSize - 1]);
    };
    for (int i = 0; i < kSubjectWarmupBlocks; ++i) {
        renderBlock();
    }
    return SubjectResult{bestNsPerBlock(kSubjectTrials, kSubjectBlocksPerTrial, renderBlock), sink};
}

/// (5) MultiStageEnvelope: one process() per SAMPLE, the cadence
/// seraphis_voice.h:906-911 drives.
[[nodiscard]] SubjectResult measureEnvelopeSubject()
{
    MultiStageEnvelope mse;
    configureStandardEnvelope(mse);

    double sink = 0.0;
    const auto renderBlock = [&]() noexcept {
        float acc = 0.0f;
        for (std::size_t s = 0; s < kBlockSize; ++s) {
            acc += mse.process();
        }
        sink += static_cast<double>(acc);
    };
    for (int i = 0; i < kSubjectWarmupBlocks; ++i) {
        renderBlock();
    }
    return SubjectResult{bestNsPerBlock(kSubjectTrials, kSubjectBlocksPerTrial, renderBlock), sink};
}

/// (6) GrowthEnvelope: one processBlock per control chunk plus the held read,
/// the cadence seraphis_voice.h:897-898 drives IN GROWTH MODE.
///
/// SC-002 pins the voice in **Standard** mode, in which SeraphisVoice never
/// advances growth_ - so this subject appears in the DENOMINATOR while
/// contributing nothing to the numerator. That is the criterion as written
/// ("the arithmetic sum of exactly these eight sub-components", FR-002's eight),
/// and it biases the ratio DOWNWARD. Recorded rather than quietly corrected: the
/// measured figure is reported separately in the case's WARN so compliance.md
/// carries both the bounded ratio and the ratio excluding this term.
///
/// trigger() is issued once and the FR-019 duration of 10 s elapses inside the
/// warm-up, so most of the timed region measures the HELD (Complete) state.
/// That is deliberate and is the honest steady state: a sustained Growth-mode
/// voice sits at the top of the rise, and GrowthEnvelope has no release
/// (growth_envelope.h - it holds). trigger() is a documented no-op while Rising
/// or Complete, so a per-trial re-arm is not reachable without a reset() inside
/// the timed region, which would measure the reset instead.
[[nodiscard]] SubjectResult measureGrowthSubject()
{
    GrowthEnvelope growth;
    growth.prepare(kSr48);
    growth.setDuration(10.0f);  // FR-019's shipped duration
    growth.trigger();

    double sink = 0.0;
    const auto renderBlock = [&]() noexcept {
        float acc = 0.0f;
        for (std::size_t c = 0; c < kChunksPerBlock; ++c) {
            growth.processBlock(kChunk);
            acc += growth.getCurrentValue();
        }
        sink += static_cast<double>(acc);
    };
    for (int i = 0; i < kSubjectWarmupBlocks; ++i) {
        renderBlock();
    }
    return SubjectResult{bestNsPerBlock(kSubjectTrials, kSubjectBlocksPerTrial, renderBlock), sink};
}

/// (7) OrbitModulator: one processBlock per control chunk plus both axis reads,
/// the cadence seraphis_voice.h:1013-1015 drives.
[[nodiscard]] SubjectResult measureOrbitSubject()
{
    OrbitModulator orbit;
    orbit.prepare(kSr48);
    orbit.setSeed(deriveStreamSeed(kSc002Seed, SeraphisVoice::kOrbitSalt));
    orbit.setDepth(kSc002SpatialDepth);
    orbit.setRate(0.1f);      // FR-019 (unchanged)
    orbit.setCoupling(0.0f);  // FR-019 (unchanged)
    orbit.setGrowth(0.0f);    // FR-019 (unchanged)

    double sink = 0.0;
    const auto renderBlock = [&]() noexcept {
        float acc = 0.0f;
        for (std::size_t c = 0; c < kChunksPerBlock; ++c) {
            orbit.processBlock(kChunk);
            acc += orbit.getCurrentValue() + orbit.getY();
        }
        sink += static_cast<double>(acc);
    };
    for (int i = 0; i < kSubjectWarmupBlocks; ++i) {
        renderBlock();
    }
    return SubjectResult{bestNsPerBlock(kSubjectTrials, kSubjectBlocksPerTrial, renderBlock), sink};
}

/// (8) MidSideProcessor: one setWidth + one process per control chunk, the
/// cadence seraphis_voice.h:1029/1037 drives. The width table spans exactly the
/// excursion the FR-025 stage produces at SC-002's spatial depth
/// (100 +- 0.5 * kVoiceWidthSpanPct), so the smoother is genuinely in motion
/// rather than parked on a constant target.
[[nodiscard]] SubjectResult measureMidSideSubject()
{
    MidSideProcessor ms;
    ms.prepare(static_cast<float>(kSr48), kChunk);

    std::array<float, kChunksPerBlock> widths{};
    for (std::size_t c = 0; c < kChunksPerBlock; ++c) {
        const float phase = 6.283185307179586f * static_cast<float>(c)
                            / static_cast<float>(kChunksPerBlock);
        widths[c] = 100.0f
                    + kSc002SpatialDepth * SeraphisVoice::kVoiceWidthSpanPct * std::sin(phase);
    }

    Buffers buf;
    fillExcitation(buf);
    double sink = 0.0;
    const auto renderBlock = [&]() noexcept {
        for (std::size_t c = 0; c < kChunksPerBlock; ++c) {
            const std::size_t off = c * kChunk;
            ms.setWidth(widths[c]);
            ms.process(buf.inLeft.data() + off, buf.inRight.data() + off, buf.outLeft.data() + off,
                       buf.outRight.data() + off, kChunk);
        }
        sink += static_cast<double>(buf.outLeft[0])
                + static_cast<double>(buf.outRight[kBlockSize - 1]);
    };
    for (int i = 0; i < kSubjectWarmupBlocks; ++i) {
        renderBlock();
    }
    return SubjectResult{bestNsPerBlock(kSubjectTrials, kSubjectBlocksPerTrial, renderBlock), sink};
}

// =============================================================================
// Reporting
// =============================================================================

[[nodiscard]] std::string reportChain(double measuredNs, double baselineNs)
{
    std::ostringstream os;
    os << "SC-001 full-poly composed chain (polyphony 8, all sounding, atmosphere FROZEN,\n"
       << "        worst measured body material, 64 partials + drift, AetherReverb config (c)):\n"
       << "  block budget    : " << kBlockBudgetNs << " ns  (512 samples @ 48 kHz)\n"
       << "  reference (25 %): " << kReferenceNs << " ns/block  (roadmap line 311, KEPT by RQ-1)\n"
       << "  measured        : " << measuredNs << " ns/block  ("
       << ((measuredNs / kBlockBudgetNs) * 100.0) << " % of one core)\n"
       << "  RA-1 prediction : 20.36 % of one core (before the voice sum, the spatial stage,\n"
       << "                    the output stage and the macro matrix)\n"
       << "  checked-in base : " << baselineNs << " ns/block  (MEASURED 2026-07-31; gate: x"
       << kRegressionFactor << " = " << (baselineNs * kRegressionFactor) << " ns/block)\n"
       << "  admissible base : " << kMaxAdmissibleNs << " ns/block  (25 % / 1.15 = 21.74 %)\n"
       << "  headroom vs ref : " << ((measuredNs / kReferenceNs) * 100.0) << " % of the reference";
    return os.str();
}

}  // namespace

// =============================================================================
// SC-001: full-poly CPU budget on the composed chain
// =============================================================================

TEST_CASE("SeraphisEngine_FullPolyCpuBudget", "[.perf][systems][seraphis]")
{
    // -------------------------------------------------------------------------
    // 0. Choose the worst body material FROM THE MEASUREMENT
    //    (continuous_body_perf_test.cpp:936-940), never by hand.
    // -------------------------------------------------------------------------
    const MaterialSurvey survey = surveyMaterials();
    REQUIRE(isFiniteValue(static_cast<float>(survey.sink)));
    {
        std::ostringstream os;
        os << "SC-001 body-material survey (standalone ContinuousBody, the phase-4 perf "
              "configuration):\n";
        for (std::size_t i = 0; i < ContinuousBody::kNumMaterials; ++i) {
            os << "  " << kMaterialNames[i] << " : " << survey.nsPerBlock[i] << " ns/block\n";
        }
        os << "  worst material  : " << kMaterialNames[survey.worstIndex]
           << "  (the one SC-001 and SC-002 pin)";
        WARN(os.str());
    }
    REQUIRE(survey.nsPerBlock[survey.worstIndex] > 0.0);

    const ContinuousBody::BodyMaterial worstMaterial = kMaterials[survey.worstIndex];

    // -------------------------------------------------------------------------
    // 1. Build the scenario and settle it.
    // -------------------------------------------------------------------------
    ChainSubject subject = buildChainSubject(worstMaterial);
    Buffers buf;
    double chainSink = 0.0;

    // The composed chain, exactly as SC-001 names it. NOTHING ELSE is inside the
    // timed region - see the "WHY THE TIMED DRIVER IS THOSE THREE CALLS" note in
    // this file's banner. Reading two samples per block is what stops the
    // optimizer dead-coding the render away; a real consumer reads the whole
    // buffer, so it is not artificial overhead.
    const auto run = [&]() noexcept {
        subject.engine->processStereoBlock(buf.inLeft.data(), buf.inRight.data(), kBlockSize);
        subject.reverb->processStereoBlock(buf.inLeft.data(), buf.inRight.data(),
                                           buf.outLeft.data(), buf.outRight.data(), kBlockSize);
        subject.engine->processOutputStage(buf.outLeft.data(), buf.outRight.data(), kBlockSize);
        chainSink += static_cast<double>(buf.outLeft[0])
                     + static_cast<double>(buf.outRight[kBlockSize - 1]);
    };

    for (int i = 0; i < kChainWarmupBlocks; ++i) {
        run();
    }

    // -------------------------------------------------------------------------
    // 2. Engage the freeze and PROVE it took, before anything is timed.
    // -------------------------------------------------------------------------
    // setAtmosphereFreeze(true) only ARMS: one slot is retried per control chunk
    // (seraphis_engine.h:835-848) and a capture is a documented no-op until that
    // voice's ring holds a whole analysis window (atmosphere_engine.h:911-916).
    // The fan-out therefore needs ~kMaxVoices control chunks; the loop below
    // gives it far more and then asserts rather than assuming.
    //
    // The mix goes up FIRST, so the freeze layer is already out of its
    // settledDry bypass by the time the first capture lands - see
    // engageFreezeMix.
    engageFreezeMix(*subject.engine);
    subject.engine->setAtmosphereFreeze(true);
    REQUIRE(subject.engine->getAtmosphereFreeze());

    std::size_t capturedVoices = 0;
    for (int i = 0; (i < 200) && (capturedVoices < kPolyphony); ++i) {
        run();
        capturedVoices = 0;
        for (std::size_t v = 0; v < kPolyphony; ++v) {
            if (subject.engine->getVoice(v).isFreezeCaptured()) {
                ++capturedVoices;
            }
        }
    }

    // The 3.1-point clause, BOTH HALVES: without the capture the frozen label is
    // false, and without the mix the frozen path is bypassed at zero cost
    // (atmosphere_engine.h:2149-2152) while the capture assertion still passes.
    for (std::size_t v = 0; v < kPolyphony; ++v) {
        INFO("frozen-atmosphere precondition, voice " << v);
        REQUIRE(subject.engine->getVoice(v).isFreezeCaptured());
        REQUIRE(subject.engine->getVoice(v).atmos().getFreezeMix() == Catch::Approx(1.0f));
    }

    // -------------------------------------------------------------------------
    // 3. The rest of the scenario's preconditions. A figure for a configuration
    //    that is not the one named would be the wrong number wearing the right
    //    label, so each is checked rather than assumed.
    // -------------------------------------------------------------------------
    REQUIRE(subject.engine->getPolyphony() == kPolyphony);
    REQUIRE(subject.engine->getActiveVoiceCount() == kPolyphony);
    REQUIRE(subject.engine->getRenderingVoiceCount() >= kPolyphony);
    REQUIRE(subject.engine->getNonFiniteRecoveryCount() == 0u);
    for (std::size_t v = 0; v < kPolyphony; ++v) {
        INFO("64-partial cloud precondition, voice " << v);
        REQUIRE(subject.engine->getVoice(v).cloud().getActivePartialCount()
                == HarmonicCloud::kMaxPartials);
        REQUIRE(subject.engine->getVoice(v).stateFinite());
    }
    // AetherReverb configuration (c): the 32-resonator ceiling really is driven,
    // and the spectral stage really is running @4096 (its latency is the
    // observable that proves the FFT size).
    REQUIRE(subject.reverb->getActiveBloomResonatorCount()
            == static_cast<std::size_t>(AetherReverb::kMaxBloomResonators));
    REQUIRE(subject.reverb->getLatencySamples() == std::size_t{4096});
    REQUIRE(subject.reverb->isShimmerActive());
    REQUIRE(subject.reverb->getNonFiniteRecoveryCount() == 0u);
    // The macros are at the FR-060 neutral and were applied ONCE (see the
    // "WHY THE TIMED DRIVER IS THOSE THREE CALLS" note in the banner).
    REQUIRE(subject.macros.getMacros().dream == Catch::Approx(0.0f));
    REQUIRE(subject.macros.getMacros().bloom == Catch::Approx(0.0f));
    REQUIRE(subject.macros.getMacros().dissolve == Catch::Approx(0.0f));
    REQUIRE(subject.macros.getMacros().gravity == Catch::Approx(0.5f));
    REQUIRE(subject.macros.getMacros().entropy == Catch::Approx(0.0f));

    // -------------------------------------------------------------------------
    // 4. Measure, report, then gate. THE REPORT COMES BEFORE THE GATE: a REQUIRE
    //    aborts the case, and the figure is what compliance.md needs even (in
    //    fact, especially) when it fails.
    // -------------------------------------------------------------------------
    const double measuredNs = bestNsPerBlock(kChainTrials, kChainBlocksPerTrial, run);

    REQUIRE(isFiniteValue(static_cast<float>(chainSink)));
    REQUIRE(subject.engine->getNonFiniteRecoveryCount() == 0u);
    REQUIRE(subject.reverb->getNonFiniteRecoveryCount() == 0u);

    WARN(reportChain(measuredNs, kBaselineFullPolyNsPerBlock));

    INFO("SC-001: if this fails, the levers are the shipped voice count (RQ-1) or Phase 7's own "
         "composition cost - never a Phase 2/4/5/6 gate (N-10), never the 25 % ceiling, never "
         "kRegressionFactor");
    REQUIRE(measuredNs <= kBaselineFullPolyNsPerBlock * kRegressionFactor);
}

// =============================================================================
// SC-002: per-voice composition overhead
// =============================================================================

TEST_CASE("SeraphisVoice_CompositionOverhead", "[.perf][systems][seraphis]")
{
    const MaterialSurvey survey = surveyMaterials();
    REQUIRE(isFiniteValue(static_cast<float>(survey.sink)));
    const ContinuousBody::BodyMaterial worstMaterial = kMaterials[survey.worstIndex];

    // The eight sub-components FR-002 enumerates, each standalone in the same
    // pinned configuration, each best-of-N in its own right. The ratio is
    // computed from these AGGREGATES, never from single runs.
    const SubjectResult cloud = measureCloudSubject();
    const SubjectResult morph = measureMorphSubject();
    const SubjectResult body = measureBodySubject(worstMaterial);
    const SubjectResult atmos = measureAtmosphereSubject();
    const SubjectResult mse = measureEnvelopeSubject();
    const SubjectResult growth = measureGrowthSubject();
    const SubjectResult orbit = measureOrbitSubject();
    const SubjectResult midside = measureMidSideSubject();

    const SubjectResult voice = measureVoiceSubject(worstMaterial);

    const double sumNs = cloud.nsPerBlock + morph.nsPerBlock + body.nsPerBlock + atmos.nsPerBlock
                         + mse.nsPerBlock + growth.nsPerBlock + orbit.nsPerBlock
                         + midside.nsPerBlock;
    // The same sum WITHOUT GrowthEnvelope, which the Standard-mode voice never
    // advances (see measureGrowthSubject's note). Reported, not gated - SC-002's
    // denominator is the eight.
    const double sumWithoutGrowthNs = sumNs - growth.nsPerBlock;

    // Non-vacuity: every subject must have produced real work and a finite sink,
    // otherwise the ratio is a division of noise by noise.
    const std::array<double, 9> allSinks = {{cloud.sink, morph.sink, body.sink, atmos.sink,
                                             mse.sink, growth.sink, orbit.sink, midside.sink,
                                             voice.sink}};
    for (std::size_t i = 0; i < allSinks.size(); ++i) {
        INFO("subject sink index " << i);
        REQUIRE(isFiniteValue(static_cast<float>(allSinks[i])));
    }
    REQUIRE(sumNs > 0.0);
    REQUIRE(voice.nsPerBlock > 0.0);

    const double ratio = voice.nsPerBlock / sumNs;

    {
        std::ostringstream os;
        os << "SC-002 per-voice composition overhead (512-sample blocks @ 48 kHz, best-of-"
           << kSubjectTrials << " x " << kSubjectBlocksPerTrial << ",\n"
           << "        64 partials + drift, FR-019 defaults, worst body material ("
           << kMaterialNames[survey.worstIndex] << "), atmosphere UNFROZEN,\n"
           << "        Standard envelope gated on, spatial depth " << kSc002SpatialDepth << "):\n"
           << "  HarmonicCloud       : " << cloud.nsPerBlock << " ns/block\n"
           << "  SpectralMorphEngine : " << morph.nsPerBlock << " ns/block\n"
           << "  ContinuousBody      : " << body.nsPerBlock << " ns/block\n"
           << "  AtmosphereEngine    : " << atmos.nsPerBlock << " ns/block\n"
           << "  MultiStageEnvelope  : " << mse.nsPerBlock << " ns/block\n"
           << "  GrowthEnvelope      : " << growth.nsPerBlock
           << " ns/block  (in the denominator by FR-002's eight; the\n"
           << "                        Standard-mode voice never advances it, so this term biases\n"
           << "                        the ratio DOWNWARD - see the ratio excluding it below)\n"
           << "  OrbitModulator      : " << orbit.nsPerBlock << " ns/block\n"
           << "  MidSideProcessor    : " << midside.nsPerBlock << " ns/block\n"
           << "  --- sum of eight    : " << sumNs << " ns/block\n"
           << "  SeraphisVoice       : " << voice.nsPerBlock << " ns/block\n"
           << "  ratio (SC-002)      : " << ratio << "   (bound <= " << kCompositionOverheadBound
           << ")\n"
           << "  ratio excl. growth  : " << (voice.nsPerBlock / sumWithoutGrowthNs)
           << "   (reported for compliance.md, NOT the gate)";
        WARN(os.str());
    }

    INFO("SC-002: the 10 % allowance covers the carry-FIFO copies, the per-sample envelope "
         "multiply, the spatial stage's two multiplies and the morph->cloud handoff, and nothing "
         "else");
    REQUIRE(ratio <= kCompositionOverheadBound);
}

// =============================================================================
// Recorded observables for compliance.md (T014's second half)
// =============================================================================
// Neither of these is a criterion; both are figures T014 asks to be MEASURED AND
// RECORDED here so compliance.md carries them alongside SC-001.
//
//   * sizeof(SeraphisEngine) / sizeof(SeraphisVoice) - T005's figures, which the
//     plan §6.3 stack rule is derived from. They are per-toolchain, so the run
//     that produces compliance.md must print them rather than copy the g++ ones
//     recorded in seraphis_engine.h:159-164.
//   * plan §9 R14 - the worst SINGLE 64-sample control chunk during the freeze
//     fan-out. The risk is that all 16 capture rings fill from the same reset(),
//     so the availability test flips for every slot on the same chunk: an
//     un-staggered retry would land 16 x 2 = 32 FFT(2048) inside one 1.33 ms
//     chunk. §3.4 step 2 services kFreezeRetriesPerChunk = 1 slot per chunk, so
//     the priced worst case is TWO FFT(2048) - one per channel - and the fan-out
//     spreads over >= kMaxVoices chunks. The metric is a MAXIMUM, so best-of-N
//     is applied the only way that makes sense: repeat the whole arm/capture
//     cycle and take the SMALLEST per-cycle maximum, which is the
//     least-contaminated estimate of the true worst chunk (the
//     aether_reverb_perf_test.cpp:852-856 argument).

TEST_CASE("SeraphisEngine_PerfObservables", "[.perf][systems][seraphis]")
{
    // -------------------------------------------------------------------------
    // sizeof - T005
    // -------------------------------------------------------------------------
    {
        std::ostringstream os;
        os << "T005 sizes on THIS toolchain (plan §6.3's stack rule is derived from them):\n"
           << "  sizeof(SeraphisVoice)  : " << sizeof(SeraphisVoice) << " B  (bound "
           << SeraphisVoice::kVoiceSizeBound << " B)\n"
           << "  sizeof(SeraphisEngine) : " << sizeof(SeraphisEngine) << " B  (bound "
           << SeraphisEngine::kEngineSizeBound << " B)\n"
           << "  voices_ share          : "
           << (static_cast<double>(sizeof(SeraphisVoice) * SeraphisEngine::kMaxVoices) * 100.0
               / static_cast<double>(sizeof(SeraphisEngine)))
           << " % of the engine object\n"
           << "  => every SeraphisEngine in every TU is heap-allocated; MSVC's default "
              "main-thread stack is 1 MiB";
        WARN(os.str());
    }
    STATIC_REQUIRE(sizeof(SeraphisVoice) <= SeraphisVoice::kVoiceSizeBound);
    STATIC_REQUIRE(sizeof(SeraphisEngine) <= SeraphisEngine::kEngineSizeBound);

    // -------------------------------------------------------------------------
    // R14 - the freeze fan-out burst, engine only
    // -------------------------------------------------------------------------
    // Engine only, not the composed chain: R14 is about the ENGINE's staggered
    // retry, and the reverb's per-chunk cost would swamp the two FFTs the
    // measurement exists to price.
    const MaterialSurvey survey = surveyMaterials();
    REQUIRE(isFiniteValue(static_cast<float>(survey.sink)));
    ChainSubject subject = buildChainSubject(kMaterials[survey.worstIndex]);

    std::array<float, kChunk> outL{};
    std::array<float, kChunk> outR{};
    double sink = 0.0;

    // Exactly one control chunk per call: the engine starts at sampleCounter_ 0
    // and only ever advances by kChunk here, so no call straddles a boundary and
    // every timed call contains exactly one runPreRenderControlStep.
    const auto renderChunk = [&]() noexcept {
        subject.engine->processStereoBlock(outL.data(), outR.data(), kChunk);
        sink += static_cast<double>(outL[0]) + static_cast<double>(outR[kChunk - 1]);
    };

    // The freeze layer must be out of its settledDry bypass before the first arm,
    // and it must STAY out: SpectralFreezeOscillator::unfreeze() clears frozen_
    // inside processBlock, which the bypass never calls, so at the FR-019 mix of
    // 0 the release between cycles would never complete and every cycle after the
    // first would measure a fan-out that had nothing to do. See engageFreezeMix.
    engageFreezeMix(*subject.engine);

    // Fill the rings: a capture is a no-op until a whole analysis window is
    // available, so a fan-out measured on a cold pool would price nothing.
    const int warmupChunks = kChainWarmupBlocks * static_cast<int>(kChunksPerBlock);
    for (int i = 0; i < warmupChunks; ++i) {
        renderChunk();
    }

    // A quiet reference for the same call, unfrozen and un-armed.
    const double quietChunkNs = bestNsPerBlock(kChainTrials, kChainBlocksPerTrial, renderChunk);

    double bestWorstChunkNs = std::numeric_limits<double>::max();
    double meanWorstChunkNs = 0.0;
    std::size_t chunksToFanOut = 0;
    bool everCappedOut = false;

    std::size_t worstDrainChunks = 0;
    bool drainCompleted = true;

    for (int cycle = 0; cycle < kFreezeCycles; ++cycle) {
        // Release, then RENDER OUT the one-hop unfreeze fade before re-arming.
        // setAtmosphereFreeze(false) fans releaseFreeze() out inline (:557-560),
        // but that only ARMS the fade (spectral_freeze_oscillator.h:306-311);
        // frozen_ - and therefore isFreezeCaptured() - clears only when the fade
        // finishes inside processBlock (:365). Re-arming immediately would leave
        // `captured == kPolyphony` on entry to the loop below, which would exit
        // at zero chunks and report a worst-chunk figure of 0 ns for every cycle
        // after the first.
        subject.engine->setAtmosphereFreeze(false);
        std::size_t stillCaptured = kPolyphony;
        std::size_t drainChunks = 0;
        while ((stillCaptured > 0u) && (drainChunks < static_cast<std::size_t>(kFreezeChunkCap))) {
            renderChunk();
            ++drainChunks;
            stillCaptured = 0;
            for (std::size_t v = 0; v < kPolyphony; ++v) {
                if (subject.engine->getVoice(v).isFreezeCaptured()) {
                    ++stillCaptured;
                }
            }
        }
        drainCompleted = drainCompleted && (stillCaptured == 0u);
        worstDrainChunks = std::max(worstDrainChunks, drainChunks);

        subject.engine->setAtmosphereFreeze(true);  // ARM ONLY (:554)

        double worstNs = 0.0;
        std::size_t chunks = 0;
        std::size_t captured = 0;
        while (captured < kPolyphony) {
            if (chunks >= static_cast<std::size_t>(kFreezeChunkCap)) {
                everCappedOut = true;
                break;
            }
            const auto start = std::chrono::steady_clock::now();
            renderChunk();
            const auto end = std::chrono::steady_clock::now();
            worstNs = std::max(
                worstNs, std::chrono::duration<double, std::nano>(end - start).count());
            ++chunks;

            captured = 0;
            for (std::size_t v = 0; v < kPolyphony; ++v) {
                if (subject.engine->getVoice(v).isFreezeCaptured()) {
                    ++captured;
                }
            }
        }
        bestWorstChunkNs = std::min(bestWorstChunkNs, worstNs);
        meanWorstChunkNs += worstNs / static_cast<double>(kFreezeCycles);
        chunksToFanOut = std::max(chunksToFanOut, chunks);
    }

    // Structural preconditions - a burst figure taken from a fan-out that never
    // completed, or from a cycle whose release never took, is not a measurement
    // of anything.
    REQUIRE(drainCompleted);
    REQUIRE_FALSE(everCappedOut);
    REQUIRE(chunksToFanOut > 0u);
    REQUIRE(bestWorstChunkNs > 0.0);
    REQUIRE(isFiniteValue(static_cast<float>(sink)));
    REQUIRE(subject.engine->getNonFiniteRecoveryCount() == 0u);

    {
        const double chunkDeadlineNs = (static_cast<double>(kChunk) / kSr48) * 1.0e9;
        std::ostringstream os;
        os << "plan §9 R14 - freeze fan-out burst (engine only, polyphony " << kPolyphony
           << ", best-of-" << kFreezeCycles << " arm/capture cycles):\n"
           << "  worst single chunk : " << bestWorstChunkNs << " ns  ("
           << ((bestWorstChunkNs / chunkDeadlineNs) * 100.0) << " % of the 64-sample deadline "
           << chunkDeadlineNs << " ns)\n"
           << "  mean of the maxima : " << meanWorstChunkNs << " ns\n"
           << "  quiet chunk (ref)  : " << quietChunkNs << " ns\n"
           << "  chunks to fan out  : " << chunksToFanOut << "  (kFreezeRetriesPerChunk = "
           << SeraphisEngine::kFreezeRetriesPerChunk << ", so >= kMaxVoices chunks by design)\n"
           << "  chunks to release  : " << worstDrainChunks
           << "  (the one-hop unfreeze fade, untimed)\n"
           << "  priced worst case  : 2 x FFT(freezeFftSize) - one per channel - in ONE chunk;\n"
           << "                       an un-staggered fan-out would be 16 x 2 = 32 in one chunk";
        WARN(os.str());
    }
}
