// ==============================================================================
// Seraphis - Parameter performance tests (Phase 9)   [.perf]
// ==============================================================================
// Reference: specs/seraphis-phase9-parameters/spec.md
//            specs/seraphis-phase9-parameters/plan.md   (sec. 7.0, sec. 7.8, sec. 7.9)
//            specs/seraphis-phase9-parameters/tasks.md  (T027 writes this TU,
//                                                        T028 pins its figures)
//
// CRITERIA OWNED BY THIS TU (plan sec. 7.0's test-file map):
//   SC-008  the parameter-push CPU budget, at the threshold plan sec. 7.8 pins from
//           measurement (ceil(worst x 1.05))
//   SC-009  the full-polyphony budget driven from plan sec. 7.9's exhaustively
//           enumerated 91-row non-default parameter table
//
// EVERY CASE HERE IS TAGGED "[.perf]" - hidden by default, run explicitly with
//   seraphis_tests.exe "[.perf]"
//
// COMPILE FLAGS: this TU MUST NOT be listed under "-fno-fast-math
//   -fno-finite-math-only" in plugins/seraphis/tests/CMakeLists.txt - those
//   flags move the figures its baselines are pinned to. Same rule as
//   dsp/tests/CMakeLists.txt:735-740. It therefore injects NO non-finite value
//   and names no std::isnan / std::isinf / std::numeric_limits infinity: the
//   macOS leg builds with -ffast-math, which folds them. Finiteness is checked
//   on the IEEE-754 exponent field instead (isFiniteValue below).
//
// ==============================================================================
// BASELINE STATUS (2026-08-02) - ALL FIVE BASELINES PINNED AND GATING
// ==============================================================================
//   T027 wrote the STRUCTURE: the three SC-008 arms, the SC-009 subject, the
//   91-row table, the optimization barriers and the non-zero-elapsed clauses.
//   T028 ran them and checked in four of the five measured numbers. The fifth,
//   SC-009's, was ESCALATED (it could not be pinned at ceil(worst x 1.05) on a
//   machine that was not cold) and was closed on 2026-08-02 by the seven-run
//   COLD-MACHINE dataset below. Outcome:
//
//     SC-008 arms 1-3  PINNED (T028) from the six-run dataset in BASELINE
//                      PROVENANCE below, as ceil(worst x kBaselineHeadroom).
//                      The 2026-08-02 cold dataset re-verifies every one of
//                      them UNDER its pin, so no SC-008 pin moved.
//     SC-009           PINNED (2026-08-02), CEILING-DERIVED. The cold dataset's
//                      worst is 2 230 830 ns (20.9141 %), but ceil(worst x 1.05)
//                      = 2 342 372 collides with the 25 % ceiling through
//                      kRegressionFactor, so the baseline is the largest value
//                      the static_asserts admit - floor(25 % / 1.15) = 2 318 840
//                      - per the FR-057 amendment of 2026-08-02. See
//                      "SC-009: HOW ITS BASELINE WAS PINNED" below.
//
//   The two flags stay separate because they were pinned from different datasets
//   on different dates, and the provenance of a gate is part of the gate. The
//   invariant that forced the split is unchanged and is now satisfied by both:
//   a placeholder must not be able to pass or fail anything, and evidence must.
//   Nothing here is relaxed: the 25 % ceiling, kRegressionFactor and
//   kBaselineHeadroom are all exactly what T028 left them.
//
//   What gates on every run, pinned or not:
//     - FR-057's ABSOLUTE ceilings (0.05 % / 0.50 % of the block budget) and
//       SC-009's 25 %-of-one-core ceiling. These are SPEC constants, not
//       measurements, so they bind from the first run.
//     - the strictly-non-zero elapsed time of every arm (a 0 ns arm FAILS).
//     - every static_assert tying a baseline to its reference.
//   NO ARM IS COMPILED OUT (plan sec. 7.8's closing sentence): all four measurements
//   run in every configuration; only the SC-009 baseline COMPARISON is deferred.
//
// ==============================================================================
// BASELINE PROVENANCE - the T028 dataset (2026-08-01)
// ==============================================================================
//   windows-x64-release, MSVC 19.4x, SIX consecutive whole-suite runs of
//   `seraphis_tests.exe "[.perf]"` on the same machine and session. Each figure
//   is itself a best-of-16 (bestNsPerCall(kPushTrials/kChainTrials, ...)), so
//   "worst" below is the worst of six best-of-16 estimates - Phase 7's shape
//   (dsp/tests/unit/systems/seraphis_perf_test.cpp's BASELINE PROVENANCE).
//   Run 6 is the verification run taken AFTER the first five were pinned; it is
//   included because it raised the arm-2 worst, and a provenance table that
//   quietly drops an inconvenient observation is not provenance.
//
//   ns/block                run1      run2      run3      run4      run5      run6   worst
//   arm 1 steady state     76.65     74.75     82.65     91.95     87.80     87.35   91.95
//   arm 2 push, poly 8     31012     30562     32972     32064     32082     33506   33506
//   arm 2 push, poly 16    31402     32794     33418     33470     28024     28508   33470
//   arm 2 WORSE of the two 31402     32794     33418     33470     32082     33506   33506
//   arm 3 subdivided     1453600   1521170   1488035   1519245   1343680   1372500 1521170
//   arm 3 undivided      1399900   1436965   1394065   1514460   1267675   1228760 1514460
//   SC-009 poly 8        2344335   2231654   2582570   2427689   2312406   2204830 2582570
//   SC-009 poly 16 (n/g) 5052111   4416297   4980703   4422009   4524776   4301120 5052111
//
//   arm 3's subdivided/undivided ratio per run: 1.038, 1.059, 1.067, 1.003,
//   1.060, 1.117 - i.e. the 64-sample control-chunk grid costs <= 11.7 % of
//   whole-block wall time, so plan sec. 3.5.4's "coarsen the grid to 128 samples"
//   remedy is NOT needed and sec. 7.6's positive control keeps the grid it was
//   constructed on. (The ratio is noisy because BOTH figures are whole-block
//   renders of a ~13 % -of-core chain; it is reported, never gated.)
//
//   SC-008 arm 2 DOES NOT BREACH FR-057's 0.50 %: the worst push is
//   33506 ns/block = 33.506 us against the 53333.3 ns = 53.333 us ceiling, i.e.
//   0.3141 % of one core, with the full 0xFFFF spectral fan-out
//   over a quiescent pool (4096 std::log2 + 64 isValidSpectralState scans + 64
//   128-float comparisons). The one-directional remedy plan sec. 7.8 pre-declared -
//   bounding the per-block fan-out to kSpectralFanOutVoicesPerBlock and adopting
//   spec amendment A9 - is therefore NOT adopted, and SC-013 clause 3 keeps its
//   "on the first block after every voice has become quiescent" wording.
//
// ==============================================================================
// SC-009: WHY ITS BASELINE COULD NOT BE PINNED IN T028 (kept - it is the reason
// the cold dataset below exists, and it is what the pin is measured against)
// ==============================================================================
//   The RAW CRITERION PASSED on all six T028 runs: 2582570 ns worst <=
//   2666666.7 ns, i.e. 24.21 % of one core against the 25 % ceiling - but with
//   only 3.2 % margin. The baseline discipline is what it failed:
//
//     kMaxAdmissibleFullPolyNs = 25 % / 1.15            = 2318840.6 ns (21.74 %)
//     a baseline is admissible only if ceil(measured x 1.05) <= that,
//     i.e. only if measured <= 2208419.6 ns                            (20.70 %)
//     WORST measured of six                            = 2582570   ns (24.21 %)
//     ceil(worst x 1.05) - THE SHIPPABLE BASELINE      = 2711699   ns (25.42 %) X
//     best  measured of six                            = 2204830   ns (20.67 %)
//     ceil(best  x 1.05)                               = 2315072   ns (21.70 %) ok
//
//   The discipline pins ceil(WORST x 1.05), and that value failed BOTH SC-009
//   static_asserts below, which is why T028 escalated rather than patched. The
//   shape of the finding was "on the boundary", not "wildly over": exactly ONE
//   of the six runs landed under the admissibility threshold, and the
//   run-to-run spread (20.67 %-24.21 %) was itself larger than the margin the
//   discipline needs.
//
//   THE MECHANISM IS THE ONE THIS FILE ALREADY NAMES. The 91-row table drives
//   Atmosphere Freeze Mix (row 1007) to 1.0 with the atmosphere frozen, so
//   renderFreezeLayer's `settledDry` bypass cannot engage - the note at the
//   freeze precondition below prices that at 1.048 % -> 1.440 % per voice, i.e.
//   +3.1 points at 8 voices. Phase 7's ~19 % + 3.1 = ~22.1 %. Phase 9 adds no
//   DSP to the chain; it drives the surface that was already there to its
//   non-default values, which is what SC-009 asks for.
//
// ==============================================================================
// SC-009: HOW ITS BASELINE WAS PINNED (COLD-MACHINE DATASET, 2026-08-02)
// ==============================================================================
//   T028's escalation named three admissible remedies - the shipped voice
//   count, Phase 9's own push cost, or a phase-owner ruling recorded as a spec
//   amendment. The third was taken, and it was taken only after the measurement
//   precondition SC-008's own discipline states ("best-of-16 per subject, >= 8
//   trials, IDLE MACHINE", spec.md:2154-2157) was actually met for the first
//   time.
//
//   THE DATASET: seven consecutive whole-suite runs of
//   `seraphis_tests.exe "[.perf]"` on 2026-08-02 on a FRESH-BOOT, IDLE machine
//   (windows-x64-release, MSVC 19.4x), all seven EXIT=0. Same shape as every
//   table in this file: each figure is a best-of-16.
//
//   ns/block                run1      run2      run3      run4      run5      run6      run7   worst
//   arm 1 steady state     82.40     79.35     75.70     73.85     80.95     75.55     75.70   82.40
//   arm 2 push, poly 8     27632     28226     31720     29448     28586     28788     29494   31720
//   arm 2 push, poly 16    28040     32496     32142     32342     27670     28842     29148   32496
//   arm 2 WORSE of the two 28040     32496     32142     32342     28586     28842     29494   32496
//   arm 3 subdivided     1378910   1243930   1216720   1264260   1258460   1214800   1311280 1378910
//   arm 3 undivided      1226200   1181670   1292840   1183500   1267830   1243860   1282280 1292840
//   SC-009 poly 8        2136070   2150320   2206890   2215600   2230830   2123410   2189100 2230830
//   SC-009 poly 16 (n/g) 4184068   4161380   4187040   4203890   4163810   4193910   4173690 4203890
//
//   SC-009 poly 8 as % of one core: 20.0256, 20.1593, 20.6896, 20.7712,
//   20.9141, 19.9069, 20.5228 - median 20.5228 %, worst 20.9141 %, a 1.01-point
//   band. The 16-voice non-gating figure is 39.01-39.41 %, i.e. ~1.9x the
//   8-voice cost and 1.57x the ceiling, which is the number FR-058 clause 1
//   writes into the roadmap amendment and the reason the gate is 8.
//
//   THE MACHINE STATE WAS VALIDATED, NOT ASSUMED. Phase 7's own SC-001 case
//   (dsp_systems_tests.exe "SeraphisEngine_FullPolyCpuBudget"), which contains
//   NO Phase 9 code, was run three times in the same cold session: 20.0104 %,
//   19.5613 %, 17.6045 %, all three PASSING their own Phase 7 gate and all
//   inside/below the 18.34 %-20.07 % band Phase 7 recorded. That is the control
//   the earlier failing pass lacked: on a HOT machine the same control read
//   23.623/24.784/24.4679 % (a +27 to +32 % whole-machine slowdown) while
//   SC-009 read a 28.30 % median - which is how the hot breaches were
//   established as thermal/power degradation rather than a Phase 9 cost.
//
//   THE ARITHMETIC OF THE PIN, and why it is CEILING-DERIVED:
//     WORST measured of seven                          = 2230830   ns (20.9141 %)
//     ceil(worst x 1.05) - the discipline's first pick = 2342372   ns (21.9598 %)
//       vs kMaxAdmissibleFullPolyNs = 25 % / 1.15      = 2318840.6 ns (21.7391 %) X
//       and 2342372 x 1.15 = 2693728 ns vs the 25 % ceiling 2666666.7 ns       X
//     floor(25 % / 1.15) - THE PINNED BASELINE         = 2318840   ns (21.7391 %) ok
//       and 2318840 x 1.15 = 2666666 ns <= 2666666.7 ns                        ok
//
//   So ceil(worst x 1.05) STILL collides with the ceiling, by 1.0 % - far
//   closer than T028's 16.9 %, but a collision. Per FR-057's amendment of
//   2026-08-02 (spec.md), the ceiling is the binding constraint, and the
//   baseline is pinned at the largest value the static_asserts admit. THIS IS A
//   GENUINE BOUND, NOT A FICTION: the cold worst (2230830) is 3.8 % UNDER the
//   pinned baseline and 16.3 % under the gate it produces, so a regression has
//   to eat the whole cold margin before the gate fires - the gate is weaker than
//   ceil(worst x 1.05) would have been, and that weakness is disclosed here
//   rather than hidden by rounding.
//
//   WHAT WAS NOT DONE, and is still not available: raising the 25 % ceiling,
//   raising kRegressionFactor or kBaselineHeadroom, weakening either
//   static_assert, dropping the voice count below 8, or pinning from a
//   best-of-N rather than the worst.
//
// ==============================================================================
// T029 VERIFICATION DATASET (2026-08-01) - four MORE consecutive runs
// ==============================================================================
//   Taken because a compliance pass reported BOTH SC-008 arm 1 and SC-009 as
//   FAILING, twice, with figures far outside everything above: arm 1 at 127.2
//   and 138.25 / 147.75 ns against a 111.55 ns gate, and SC-009 at 3 076 718 /
//   3 280 679 / 3 704 324 ns (28.84 %, 30.76 %, 34.73 %) against the 25 %
//   ceiling. Those SC-009 figures are 19 %-43 % above the WORST of the six T028
//   runs, so either the phase regressed between T028 and that pass, or that pass
//   was not measured on an idle machine. Four fresh consecutive runs decide it:
//
//   ns/block                run1      run2      run3      run4    worst   T028 worst
//   arm 1 steady state     91.75     86.90     89.05     79.75    91.75      91.95
//   arm 2 push (worse)     32066     32620     32494     32698    32698      33506
//   arm 3 subdivided     1523280   1292800   1460580   1439260  1523280    1521170
//   arm 3 undivided      1339600   1265730   1381390   1530620  1530620    1514460
//   SC-009 poly 8        2285060   2283880   2367650   2328180  2367650    2582570
//   SC-009 poly16 (n/g)  4502060   4392840   4552990   4475630  4552990    5052111
//
//   Every arm gates GREEN on every run, and every figure lands inside or below
//   the T028 band - SC-009's worst of four (22.20 %) is better than five of the
//   six T028 runs. NOTHING REGRESSED. The compliance pass's numbers are a loaded
//   machine, which is the one condition SC-008's own measurement discipline
//   ("best-of-16 per subject, >= 8 trials, IDLE MACHINE", spec.md:2154-2157)
//   states as a precondition rather than as advice, and re-measuring on an idle
//   machine is therefore the prescribed response to them - not a threshold edit.
//
//   THE ESCALATION WAS NOT CLOSED BY THIS DATASET EITHER.
//   ceil(worst x 1.05) for SC-009 is still 2 486 033 ns (23.31 %), still over the
//   2 318 840.6 ns (21.74 %) admissibility bound, so the SC-009 baseline stayed
//   unpinned and reported-not-gating after T029. What this dataset settled is only
//   the narrower question the compliance pass raised: whether the 25 % ceiling
//   itself holds. It does, on 10 of 10 recorded runs - and on 17 of 17 once the
//   2026-08-02 cold dataset above is counted, which is the dataset that DID close
//   the escalation.
//
// ==============================================================================
// THE TWO FACTORS ARE DIFFERENT NUMBERS. DO NOT CONFLATE THEM.
// ==============================================================================
//   kRegressionFactor = 1.15  - the RUN-TIME gate and the static_assert
//                               multiplier (Phase 7's number, and the same
//                               reasoning: 1.5 cannot fit a 25 % ceiling and
//                               1.05 is a flake generator on a wall clock).
//   kBaselineHeadroom = 1.05  - a RECORDING CONVENTION applied ONCE when a
//                               measured figure is transcribed into a baseline
//                               (ceil(worst x 1.05)). It gates nothing.
//   Copied deliberately from dsp/tests/unit/systems/seraphis_perf_test.cpp:58-69,
//   which names them separately for the same reason.
//
// ==============================================================================
// WHY THE SC-008 SUBJECTS ARE BUILT HERE AND NOT DRIVEN THROUGH Processor
// ==============================================================================
//   plan sec. 7.8: SC-008 is "measured directly, never by subtracting two
//   whole-chain renders" - 0.05 % of a 512/48 kHz block is 5.3 us against a
//   ~180 us/block whole-chain spread (Phase 7 recorded 18.34 %-20.07 % over ten
//   best-of-16 runs), i.e. the noise is ~34x the quantity. Subtraction is
//   therefore not an available technique, and neither is timing process():
//     - Processor::process() returns at `data.numSamples <= 0`
//       (processor.cpp:652-654) BEFORE the pre-slice push block, so a zero-sample
//       call cannot isolate it either;
//     - pushVoiceParams / pushMacroSurfaces / pushAetherParamsIfDirty /
//       pushSpectralStatesIfPending / updateSyncedTravelRate / pushGlobalParams
//       are all PRIVATE (processor.h:248-285) and this TU adds no friend seam -
//       the one declared friend (detail::SeraphisParamSmootherBypassProbe,
//       processor.h:122-136) is DEFINED by the SC-005 TU, so defining it here
//       would be an ODR violation inside one binary.
//
//   ARMS 1 AND 2 ARE THEREFORE A TRANSCRIPTION of the pre-slice push block,
//   assembled from the SAME public components the processor uses - the real
//   parameter packs (std::atomic storage), the real Krate::DSP::OnePoleSmoother
//   class-(b) smoothers, the real SeraphisMacroMatrix, the real
//   SeraphisEngine::applyVoiceParams / applySpectralStates and the real
//   Seraphis::applyAetherParams. Only the ~40 tracker scalars and the control
//   flow around them are re-typed here. THAT IS A FIDELITY OBLIGATION, NOT A
//   FREE HAND: every function below cites the processor.cpp lines it mirrors,
//   and any edit to the pre-slice block must be mirrored here or this criterion
//   silently measures a different subject.
//
//   ARM 3 (the class-(b) settling arm) is the exception and needs no
//   transcription: it measures WHOLE-BLOCK wall time, which is exactly what
//   ProcessorFixture::processBlock() already exercises through the real
//   process().
//
// ==============================================================================
// WHY SC-009's SUBJECT IS A HAND-BUILT PAIR AND NOT Processor (plan sec. 7.9)
// ==============================================================================
//   RA-1 row (c) needs numChannels = 16 and diffusionFftSize = 4096.
//   makeSeraphisReverbConfig fixes numChannels = 8 (seraphis_engine_config.h:71)
//   and diffusionFftSize = 1024 with the comment "MUST stay 1024 -> 1024-sample
//   latency" (:82), and that latency constancy is load-bearing for Phase 8
//   (processor.h:329-339). No Phase 9 parameter can change either field, so
//   Processor STRUCTURALLY cannot produce the scenario. The case lives in the
//   PLUGIN perf TU rather than dsp_systems_tests because it needs both the
//   hand-built pair AND Seraphis::applyAetherParams, which is plugin-side
//   (FR-049). Phase 7's precedent for a hand-built pair is
//   dsp/tests/unit/systems/seraphis_perf_test.cpp:1184.
//
// STACK RULE: sizeof(SeraphisEngine) is ~772 KB against MSVC's 1 MiB default
//   main-thread stack (seraphis_engine.h:264-287), so EVERY SeraphisEngine,
//   EVERY AetherReverb and EVERY ContinuousBody here is std::make_unique'd.
//   Never a plain local.
// ==============================================================================

#include "processor/processor.h"
#include "seraphis_test_fixture.h"

#include "engine/seraphis_engine_config.h"
#include "parameters/aether_params.h"
#include "parameters/atmosphere_params.h"
#include "parameters/body_params.h"
#include "parameters/cloud_params.h"
#include "parameters/dropdown_mappings.h"
#include "parameters/global_params.h"
#include "parameters/life_mod_params.h"
#include "parameters/macro_params.h"
#include "parameters/morph_params.h"
#include "plugin_ids.h"

#include <krate/dsp/effects/aether_reverb.h>
#include <krate/dsp/primitives/smoother.h>
#include <krate/dsp/processors/spectral_state.h>
#include <krate/dsp/systems/continuous_body.h>
#include <krate/dsp/systems/harmonic_cloud.h>
#include <krate/dsp/systems/seraphis_engine.h>
#include <krate/dsp/systems/seraphis_macro_matrix.h>
#include <krate/dsp/systems/seraphis_voice.h>
#include <krate/dsp/systems/spectral_morph_engine.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

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
using Krate::DSP::ContinuousBody;
using Krate::DSP::HarmonicCloud;
using Krate::DSP::makeFactoryState;
using Krate::DSP::OnePoleSmoother;
using Krate::DSP::SeraphisEngine;
using Krate::DSP::SeraphisEngineConfig;
using Krate::DSP::SeraphisMacroMatrix;
using Krate::DSP::SeraphisMacroTarget;
using Krate::DSP::SeraphisMacroValues;
using Krate::DSP::SeraphisVoice;
using Krate::DSP::SeraphisVoiceConfig;
using Krate::DSP::SeraphisVoiceParams;
using Krate::DSP::SpectralMorphEngine;
using Krate::DSP::SpectralState;
using Krate::DSP::SpectralStateId;

namespace {

// =============================================================================
// Measurement basis
// =============================================================================

constexpr double kSr48 = 48000.0;
constexpr std::size_t kBlockSize = 512;

/// Wall-clock budget of one 512-sample block at 48 kHz, in nanoseconds:
/// 10 666 666.7 ns - the constant every Seraphis perf TU derives.
constexpr double kBlockBudgetNs = (static_cast<double>(kBlockSize) / kSr48) * 1.0e9;

/// FR-057 clause 1, the STEADY-STATE ceiling: 0.05 % of the block budget,
/// i.e. 5333.33 ns/block.
constexpr double kSteadyStateCeilingNs = kBlockBudgetNs * 0.0005;
/// FR-057 clause 1, the WORST-CASE ceiling: 0.50 %, i.e. 53 333.3 ns/block.
constexpr double kWorstCaseCeilingNs = kBlockBudgetNs * 0.005;
/// SC-009 / roadmap line 311: 25 % of one core, i.e. 2 666 666.7 ns/block.
constexpr double kFullPolyCeilingNs = kBlockBudgetNs * 0.25;

/// THE RUN-TIME GATE and the static_assert multiplier.
constexpr double kRegressionFactor = 1.15;
/// RECORDING CONVENTION ONLY - applied once when a measurement is transcribed
/// into a baseline (ceil(worst x kBaselineHeadroom)). It appears in NO assertion.
constexpr double kBaselineHeadroom = 1.05;

/// The largest baseline each static_assert can accept.
constexpr double kMaxAdmissibleSteadyStateNs = kSteadyStateCeilingNs / kRegressionFactor;
constexpr double kMaxAdmissibleWorstCaseNs = kWorstCaseCeilingNs / kRegressionFactor;
constexpr double kMaxAdmissibleFullPolyNs = kFullPolyCeilingNs / kRegressionFactor;

// -----------------------------------------------------------------------------
// THE BASELINES. T028 pinned the four SC-008 figures as
// ceil(worst-of-six x kBaselineHeadroom) over the BASELINE PROVENANCE dataset;
// the 2026-08-02 cold dataset pinned SC-009's, ceiling-derived (banner). All
// five now GATE.
// -----------------------------------------------------------------------------

/// T028: PINNED. The four SC-008 baselines are measured and now GATE.
constexpr bool kSc008BaselinesPinned = true;

/// PINNED 2026-08-02 from the seven-run cold-machine dataset (banner). SC-009's
/// baseline comparison now GATES, alongside the ABSOLUTE 25 % ceiling and the
/// non-zero-elapsed clause, which gated on every run before and after.
constexpr bool kSc009BaselinePinned = true;

/// PINNED (T028). SC-008 steady-state arm, ns per process()-entry pass.
/// ceil(worst 91.95 x 1.05) = 97. The subject is a handful of scalar compares
/// plus 27 relaxed atomic loads, which is why it lands ~55x under FR-057's
/// 0.05 % ceiling: plan sec. 7.8 says out loud that the ceiling is near-vacuous here
/// and that THIS BASELINE is what actually gates.
constexpr double kBaselineSteadyStateNs = 97.0;

/// PINNED (T028). SC-008 worst-case arm, ns per full push sequence (the worse of
/// polyphony 8 and 16). ceil(worst 33506 x 1.05) = 35182, i.e. 0.3141 % of one
/// core against FR-057's 0.50 % ceiling. plan sec. 7.8 called 4096 std::log2 inside
/// 53.3 us "not obviously achievable"; measured, it is achieved with 37 % margin,
/// so the pre-declared per-block fan-out bound (+ spec amendment A9) is NOT
/// adopted and SC-013 clause 3 keeps its wording. Raising the 0.50 % ceiling was
/// never an available remedy and none was needed.
///
/// Note the two polyphony arms trade places run to run (poly 8 was worse on run
/// 6, poly 16 on runs 1-4): the fan-out over a QUIESCENT pool is kMaxVoices-bound
/// either way (applySpectralStates bounds on kMaxVoices, not getPolyphony), so
/// the difference between them is noise, which is why the gate is the worse of
/// the two and both are reported.
constexpr double kBaselineWorstCaseNs = 35182.0;

/// PINNED (T028). SC-008 class-(b) settling arm: whole-block wall time with a
/// class-(b) ID under continuous automation, so the block runs as eight
/// 64-sample sub-slices. ceil(worst 1521170 x 1.05) = 1597229. NO ABSOLUTE
/// CEILING IS ASSERTED HERE - the phase has no budget for whole-chain render
/// cost, that is Phase 7 SC-001's, which SC-009 re-measures (plan sec. 7.8).
constexpr double kBaselineSettlingNs = 1597229.0;

/// PINNED (T028). The same block rendered UNDIVIDED (every class-(b) smoother
/// settled), reported beside the settling figure in the same trial set.
/// ceil(worst 1514460 x 1.05) = 1590183. It is the denominator of the ratio the
/// report prints; measured, the subdivision costs <= 11.7 % (banner).
constexpr double kBaselineUndividedNs = 1590183.0;

/// PINNED 2026-08-02, CEILING-DERIVED - see "SC-009: HOW ITS BASELINE WAS
/// PINNED" in the banner for the seven-run cold dataset, the control that
/// validated the machine state, and the arithmetic.
///
/// This is NOT ceil(worst x kBaselineHeadroom). The cold worst is 2230830 ns
/// (20.9141 %) and ceil(worst x 1.05) = 2342372 still fails BOTH static_asserts
/// below, because 2342372 x kRegressionFactor = 2693728 ns overshoots the 25 %
/// ceiling by 1.0 %. Per FR-057's amendment of 2026-08-02, the ceiling is the
/// binding constraint and the baseline is the largest value the static_asserts
/// admit: floor(kFullPolyCeilingNs / kRegressionFactor) = floor(2318840.58) =
/// 2318840, whose gate is 2666666 ns - just inside the 2666666.7 ns ceiling.
///
/// It is a genuine bound, not a fiction: every one of the seven cold runs
/// (19.91 %-20.91 %) lands UNDER this baseline, so the gate has real teeth; it
/// is simply weaker than the recording convention would have made it, and that
/// is disclosed rather than rounded away. DO NOT raise it: 2318840 is already
/// the maximum both asserts accept, so any increase fails to compile.
constexpr double kBaselineFullPolyNs = 2318840.0;

static_assert(kBaselineSteadyStateNs * kRegressionFactor <= kSteadyStateCeilingNs,
              "SC-008 steady state: the baseline must be no weaker than FR-057's 0.05 % ceiling - "
              "if this fails, reduce the per-process tracker work; never raise the ceiling");
static_assert(kBaselineSteadyStateNs <= kMaxAdmissibleSteadyStateNs,
              "SC-008 steady state: a baseline above the admissible ceiling means the phase is "
              "over budget - work the lever list, never raise the baseline");
static_assert(kBaselineWorstCaseNs * kRegressionFactor <= kWorstCaseCeilingNs,
              "SC-008 worst case: the baseline must be no weaker than FR-057's 0.50 % ceiling - "
              "the ONLY admissible remedy is plan sec. 7.8's per-block fan-out bound (+ spec "
              "amendment "
              "A9), never a raised ceiling and never dropping sec. 3.4's identity guard");
static_assert(kBaselineWorstCaseNs <= kMaxAdmissibleWorstCaseNs,
              "SC-008 worst case: a baseline above the admissible ceiling means the phase is over "
              "budget - adopt the per-block fan-out bound, never raise the baseline");
static_assert(kBaselineFullPolyNs * kRegressionFactor <= kFullPolyCeilingNs,
              "SC-009: the baseline must be no weaker than the 25 % reference - if this fails, the "
              "lever is the shipped voice count or Phase 9's own push cost, NEVER the 25 % ceiling "
              "and never a Phase 2/4/5/6 gate");
static_assert(kBaselineFullPolyNs <= kMaxAdmissibleFullPolyNs,
              "SC-009: a baseline above 25 % / 1.15 means the phase is over budget - work the "
              "lever "
              "list, never raise the baseline");
static_assert(kBaselineSettlingNs > 0.0 && kBaselineUndividedNs > 0.0,
              "SC-008 arm 3: both figures are gated on their own baselines only (no absolute "
              "ceiling), so both must at least be positive");

// -----------------------------------------------------------------------------
// The recorded worsts, named rather than left in a comment (Phase 7's device,
// dsp/tests/unit/systems/seraphis_perf_test.cpp:368-382), so the recording
// convention has ONE source of truth and a baseline cannot drift away from the
// measurement it came from.
// -----------------------------------------------------------------------------
constexpr double kMeasuredWorstSteadyStateNs = 91.95;
constexpr double kMeasuredWorstPushNs = 33506.0;
constexpr double kMeasuredWorstSettlingNs = 1521170.0;
constexpr double kMeasuredWorstUndividedNs = 1514460.0;

/// A 0.1 % band rather than equality: ceil(x * 1.05) is not exactly
/// representable in binary floating point, and this clause exists to catch a
/// baseline that DRIFTED from its measurement, not to pin its last ULP.
constexpr bool baselineMatchesRecordedWorst(double baselineNs, double worstNs) noexcept {
    return (baselineNs >= worstNs * kBaselineHeadroom * 0.999)
           && (baselineNs <= worstNs * kBaselineHeadroom * 1.001 + 1.0);
}

static_assert(baselineMatchesRecordedWorst(kBaselineSteadyStateNs, kMeasuredWorstSteadyStateNs),
              "SC-008 arm 1: the shipped baseline must be ceil(the recorded worst x 1.05) - "
              "re-run the five-run procedure in BASELINE PROVENANCE and record the new dataset "
              "before touching it");
static_assert(baselineMatchesRecordedWorst(kBaselineWorstCaseNs, kMeasuredWorstPushNs),
              "SC-008 arm 2: the shipped baseline must be ceil(the recorded worst x 1.05)");
static_assert(baselineMatchesRecordedWorst(kBaselineSettlingNs, kMeasuredWorstSettlingNs),
              "SC-008 arm 3 (subdivided): the shipped baseline must be ceil(worst x 1.05)");
static_assert(baselineMatchesRecordedWorst(kBaselineUndividedNs, kMeasuredWorstUndividedNs),
              "SC-008 arm 3 (undivided): the shipped baseline must be ceil(worst x 1.05)");

/// The measurement itself must clear FR-057's ceilings with the regression
/// factor applied - the arithmetic that turns "measured" into "the phase is over
/// budget", which is exactly the clause SC-009 currently cannot satisfy.
static_assert(kMeasuredWorstSteadyStateNs * kBaselineHeadroom <= kMaxAdmissibleSteadyStateNs,
              "SC-008 arm 1: ceil(measured worst x 1.05) is above the admissible ceiling - the "
              "phase is OVER BUDGET. Reduce the per-process tracker work; never raise the "
              "ceiling");
static_assert(kMeasuredWorstPushNs * kBaselineHeadroom <= kMaxAdmissibleWorstCaseNs,
              "SC-008 arm 2: ceil(measured worst x 1.05) is above the admissible ceiling - adopt "
              "plan sec. 7.8's per-block fan-out bound plus spec amendment A9; never raise the "
              "0.50 % ceiling and never drop sec. 3.4's identity guard");

// =============================================================================
// Trial shape (plan sec. 7.8 / sec. 7.9: best-of-16, >= 8 trials, idle machine)
// =============================================================================
// Best-of-N: the minimum is the least OS-noise-contaminated estimate of the real
// cost, which is what a regression bound wants. MANY SHORT TRIALS, for the reason
// dsp/tests/unit/systems/seraphis_perf_test.cpp:428-439 records: on a hybrid part
// the dominant noise source is a whole trial migrating onto an E-core, a ~20 %
// step that best-of-N cannot reject when each trial is long.

constexpr int kPushTrials = 16;
constexpr int kSteadyStateCallsPerTrial = 2000;  ///< the subject is sub-microsecond
constexpr int kSteadyStateWarmupCalls = 4000;
constexpr int kWorstCaseCallsPerTrial = 50;  ///< 4096 std::log2 per call
constexpr int kWorstCaseWarmupCalls = 100;

constexpr int kChainTrials = 16;
constexpr int kChainBlocksPerTrial = 100;  ///< ~1.07 s of audio per trial
/// ~6.4 s: past the atmosphere's 4 s capture ring, the body's crossfade, the
/// cloud's attack, the reverb build-up and every smoother in the chain.
constexpr int kChainWarmupBlocks = 600;

constexpr int kSettlingTrials = 16;
constexpr int kSettlingBlocksPerTrial = 20;
constexpr int kSettlingWarmupBlocks = 200;

/// Material-selection probe: cheap, because it only has to RANK five figures.
constexpr int kMaterialProbeBlocks = 8;

/// FR-040's shipped default polyphony, and SC-009's pinned scenario value.
constexpr std::size_t kPolyphony = 8;

// --- Structural clauses: what these numbers describe -------------------------
static_assert(SeraphisEngine::kMaxVoices == 16,
              "SC-008's worst-case arm reports polyphony 8 AND 16");
static_assert(kPolyphony <= SeraphisEngine::kMaxVoices, "the scenario must fit the pool");
static_assert(SeraphisEngine::kControlChunkSamples == 64,
              "SC-008 arm 3 subdivides a 512-sample block into EIGHT 64-sample sub-slices");
static_assert(kBlockSize % SeraphisEngine::kControlChunkSamples == 0,
              "the measured block must be a whole number of control chunks");
static_assert(kBlockSize / SeraphisEngine::kControlChunkSamples == 8,
              "plan sec. 7.8 arm 3 names EIGHT sub-slices");
static_assert(AetherReverb::kMaxBloomResonators == 32, "RA-1 row (c) drives the bloom ceiling");
static_assert(ContinuousBody::kNumMaterials == 5, "row 800 is chosen from all five materials");
static_assert(SpectralMorphEngine::kMaxStates == 4, "the spectral fan-out writes four slots");
static_assert(SeraphisMacroMatrix::kNumTargets == 27, "plan sec. 7.9's MB route is 27 rows");
static_assert(SeraphisVoiceParams::kFieldCount == 37, "plan sec. 7.9's VP route is 37 rows");

/// Finite check WITHOUT std::isnan: the macOS leg builds with -ffast-math, which
/// folds it. Inspect the IEEE-754 exponent field instead.
[[nodiscard]] bool isFiniteValue(float v) noexcept {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &v, sizeof(bits));
    return (bits & 0x7F800000u) != 0x7F800000u;
}

/// THE OPTIMIZATION BARRIER. Every arm drains its accumulator into this before it
/// returns, so no timed loop can be dead-coded away. `volatile` and never read
/// back by an assertion: the assertion is on the ELAPSED TIME, which plan sec. 7.8
/// requires to be strictly non-zero for exactly this reason.
volatile double gSink = 0.0;

/// Best-of-N driver over an invocation count. `fn` performs exactly one unit of
/// the subject. Taken by const reference, not by forwarding reference: it is
/// INVOKED, many times, never consumed.
template <typename Fn>
[[nodiscard]] double bestNsPerCall(int trials, int callsPerTrial, const Fn& fn) {
    double best = std::numeric_limits<double>::max();
    for (int trial = 0; trial < trials; ++trial) {
        const auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < callsPerTrial; ++i) {
            fn();
        }
        const auto end = std::chrono::steady_clock::now();
        const double elapsedNs = std::chrono::duration<double, std::nano>(end - start).count();
        best = std::min(best, elapsedNs / static_cast<double>(callsPerTrial));
    }
    return best;
}

/// ceil(measured x kBaselineHeadroom) - the recording convention, so T028 can
/// read the number to check in straight off the report rather than recomputing
/// it (and getting the factor wrong).
[[nodiscard]] double recordingBaselineFor(double measuredNs) noexcept {
    return std::ceil(measuredNs * kBaselineHeadroom);
}

/// One block of stereo scratch. Separate arrays: the perf lane never relies on
/// in-place support (ContinuousBody explicitly forbids it,
/// continuous_body.h:1155-1156).
struct Buffers {
    std::array<float, kBlockSize> inLeft{};
    std::array<float, kBlockSize> inRight{};
    std::array<float, kBlockSize> outLeft{};
    std::array<float, kBlockSize> outRight{};
};

// NO mutableVoice() HELPER HERE, deliberately. Phase 7's perf TU needed one
// (seraphis_perf_test.cpp:502-506) because its scenario pinned per-voice
// forwarders the macro table does not reach. Phase 9's whole point is that those
// values now arrive through applyVoiceParams / setTargetBase, so every write
// below goes through a Phase 9 route and no const_cast is required. If a future
// edit reaches for one, that is a sign the scenario stopped describing the
// shipping push path.

// =============================================================================
// PLAN sec. 7.9's 91-ROW NON-DEFAULT PARAMETER TABLE
// =============================================================================
// Checked in VERBATIM, one plain value per ID, all 91 rows. It is a SPEC
// ARTEFACT, not an implementer's choice. Rule: each row takes the MOST EXPENSIVE
// end of its range, except the three declared exception classes below.
//
// THE THREE DECLARED EXCEPTION CLASSES (plan sec. 7.9):
//   class 1 - NOT APPLICABLE, PROCESSOR-LOCAL (8 rows): 0, 100-104, 405, 406.
//             No DSP route. The five macros ARE set here, on the matrix
//             directly, at the FR-060 neutral, so the base overrides reach the
//             voices unmodified - the deep surface is this criterion's subject.
//   class 2 - PINNED BY THE SCENARIO (5 rows): 1 = 8; 1008 = on; 812 = off
//             (bypassing the resonators would remove the body engines from the
//             very chain this criterion budgets); 1201 = 1.0 and 1202 = 1.0,
//             which are RA-1 row (c)'s setSize(1) / setDensity(1).
//   class 3 - THE MOST-EXPENSIVE END COINCIDES WITH THE REGISTERED DEFAULT
//             (10 rows): 2, 208, 700, 803, 805, 808, 810, 811, 1204, 1217. Each
//             is defensible under this table's own rule - 208's shortest attack
//             is the busiest envelope, 2's `on` keeps the saturator live, 1204's
//             `off` keeps the shimmer and bloom returns live, 811's `on` keeps
//             the AGC estimator running, and the five unity rows keep their
//             signal paths at full contribution - but each IS the C-6 default,
//             so none of them is a non-default row and the class must be
//             declared. It costs the criterion nothing: SC-009 measures CPU, and
//             a row at the default is still at its most expensive end.
//
// Three row choices carry a note rather than being self-evident:
//   800 = Metal Plate as the modal material with the largest mode set. The case
//         RECORDS getActiveModeCount() for all five materials and takes the
//         maximum; if another material wins, this row changes and the
//         measurement is redone. That is a REQUIRE below, not a comment.
//   1204 = off, not on: freeze makes the shimmer and bloom returns inert, i.e.
//         CHEAPER. The criterion wants the worst case.
//   1206 = 0.0 and 809 = 0.0: minimum damping is the longest ring, i.e. the most
//         sustained work.
//
// Enum-valued rows carry the ENUM INDEX, converted through the same
// dropdown_mappings.h helpers the processor uses (toBodyMaterial etc.), never a
// hand-written cast:
//   403 Spline = 1 (SpectralMorphEngine::TravelMode)
//   409-412 Bell/Choir/Glass/Breath = 1/2/3/4 (SpectralStateId)
//   700 Standard = 0 (SeraphisVoice::EnvelopeMode)
//   800 Metal Plate = 2 (ContinuousBody::BodyMaterial)
//   1016 Blackman = 3 (GrainEnvelopeType)
//   3 = seed INDEX 3 into C-10's curated kSeedValues table, never a constant.
//   408 carries the state COUNT (4) itself, not the dropdown index.
// =============================================================================

/// Which of plan sec. 7.9's classes a row belongs to. `NonDefault` is the rule; the
/// other three are the declared exceptions and are COUNTED by static_assert
/// below, so a silent reclassification cannot happen.
enum class RowClass : std::uint8_t {
    NonDefault,            ///< the rule: the most expensive end of the range
    ProcessorLocal,        ///< class 1 (8 rows)
    ScenarioPinned,        ///< class 2 (5 rows)
    CoincidesWithDefault,  ///< class 3 (10 rows)
};

struct NonDefaultRow {
    Steinberg::Vst::ParamID id;
    double value;
    RowClass cls;
};

using Seraphis::kAetherBloomDecayId;
using Seraphis::kAetherBloomSendId;
using Seraphis::kAetherDampingId;
using Seraphis::kAetherDecayId;
using Seraphis::kAetherDensityId;
using Seraphis::kAetherDimensionalityId;
using Seraphis::kAetherFreezeId;
using Seraphis::kAetherMixId;
using Seraphis::kAetherModDepthId;
using Seraphis::kAetherModSmoothnessId;
using Seraphis::kAetherPreDelayId;
using Seraphis::kAetherShimmerFifthId;
using Seraphis::kAetherShimmerOctaveId;
using Seraphis::kAetherSizeBreathDepthId;
using Seraphis::kAetherSizeId;
using Seraphis::kAetherSpectralDiffusionId;
using Seraphis::kAetherTideDepthId;
using Seraphis::kAetherWidthId;
using Seraphis::kAtmosBlurId;
using Seraphis::kAtmosDecorrelationId;
using Seraphis::kAtmosDensityId;
using Seraphis::kAtmosDriftDepthId;
using Seraphis::kAtmosDriftRangeId;
using Seraphis::kAtmosDriftSmoothnessId;
using Seraphis::kAtmosFreezeId;
using Seraphis::kAtmosFreezeMixId;
using Seraphis::kAtmosGrainEnvelopeId;
using Seraphis::kAtmosGrainSecondsId;
using Seraphis::kAtmosJitterId;
using Seraphis::kAtmosLevelId;
using Seraphis::kAtmosPanSpreadId;
using Seraphis::kAtmosPitchId;
using Seraphis::kAtmosPitchSpreadId;
using Seraphis::kAtmosPositionId;
using Seraphis::kAtmosPositionSpreadId;
using Seraphis::kBodyCloudDampingId;
using Seraphis::kBodyCloudDecayId;
using Seraphis::kBodyCloudMixId;
using Seraphis::kBodyCloudSizeId;
using Seraphis::kBodyDampingId;
using Seraphis::kBodyDriveId;
using Seraphis::kBodyInputAgcId;
using Seraphis::kBodyKeyTrackingId;
using Seraphis::kBodyMaterialId;
using Seraphis::kBodyMixId;
using Seraphis::kBodyResonanceId;
using Seraphis::kBodyResonatorBypassId;
using Seraphis::kBodyWidthId;
using Seraphis::kCloudAttackId;
using Seraphis::kCloudDecayId;
using Seraphis::kCloudDriftDepthId;
using Seraphis::kCloudDriftSmoothnessId;
using Seraphis::kCloudEnvOffsetSpreadId;
using Seraphis::kCloudGravityId;
using Seraphis::kCloudInharmonicityId;
using Seraphis::kCloudMutationId;
using Seraphis::kCloudRichnessId;
using Seraphis::kCloudStereoSpreadId;
using Seraphis::kCloudTiltId;
using Seraphis::kEnvGrowthDurationId;
using Seraphis::kEnvModeId;
using Seraphis::kEnvReleaseMsId;
using Seraphis::kEnvStage0MsId;
using Seraphis::kEnvStage1MsId;
using Seraphis::kLifeSpatialCouplingId;
using Seraphis::kLifeSpatialDepthId;
using Seraphis::kLifeSpatialGrowthId;
using Seraphis::kLifeSpatialRateId;
using Seraphis::kLifeVoiceWidthId;
using Seraphis::kMacroBloomId;
using Seraphis::kMacroDissolveId;
using Seraphis::kMacroDreamId;
using Seraphis::kMacroEntropyId;
using Seraphis::kMacroGravityId;
using Seraphis::kMasterGainId;
using Seraphis::kMorphBloomId;
using Seraphis::kMorphEntropyId;
using Seraphis::kMorphPositionId;
using Seraphis::kMorphState0Id;
using Seraphis::kMorphState1Id;
using Seraphis::kMorphState2Id;
using Seraphis::kMorphState3Id;
using Seraphis::kMorphStateCountId;
using Seraphis::kMorphSyncId;
using Seraphis::kMorphSyncNoteId;
using Seraphis::kMorphTravelModeId;
using Seraphis::kMorphTravelRateId;
using Seraphis::kMorphWaypointIntervalId;
using Seraphis::kPolyphonyId;
using Seraphis::kSeedId;
using Seraphis::kSoftLimitId;

constexpr std::array<NonDefaultRow, 91> kNonDefaultTable = {{
    // --- Global (0-99) -------------------------------------------------------
    {kMasterGainId, 0.0, RowClass::ProcessorLocal},   // class 1
    {kPolyphonyId, 8.0, RowClass::ScenarioPinned},    // class 2
    {kSoftLimitId, 1.0, RowClass::CoincidesWithDefault},  // class 3: on
    {kSeedId, 3.0, RowClass::NonDefault},             // seed INDEX 3
    // --- Macros (100-104): class 1, set on the matrix at the FR-060 neutral ---
    {kMacroDreamId, 0.0, RowClass::ProcessorLocal},
    {kMacroBloomId, 0.0, RowClass::ProcessorLocal},
    {kMacroDissolveId, 0.0, RowClass::ProcessorLocal},
    {kMacroGravityId, 0.5, RowClass::ProcessorLocal},
    {kMacroEntropyId, 0.0, RowClass::ProcessorLocal},
    // --- Harmonic Cloud (200-210) --------------------------------------------
    {kCloudRichnessId, 1.0, RowClass::NonDefault},           // 200: N(1) = 64 partials
    {kCloudInharmonicityId, 0.1, RowClass::NonDefault},      // 201
    {kCloudTiltId, 12.0, RowClass::NonDefault},              // 202
    {kCloudMutationId, 1.0, RowClass::NonDefault},           // 203
    {kCloudGravityId, 1.0, RowClass::NonDefault},            // 204
    {kCloudDriftDepthId, 50.0, RowClass::NonDefault},        // 205: kMaxDriftCents
    {kCloudDriftSmoothnessId, 0.0, RowClass::NonDefault},    // 206
    {kCloudStereoSpreadId, 1.0, RowClass::NonDefault},       // 207
    {kCloudAttackId, 0.05, RowClass::CoincidesWithDefault},  // 208: class 3
    {kCloudDecayId, 60.0, RowClass::NonDefault},             // 209
    {kCloudEnvOffsetSpreadId, 1.0, RowClass::NonDefault},    // 210
    // --- Spectral Morph / Entropy (400-412) ----------------------------------
    {kMorphEntropyId, 1.0, RowClass::NonDefault},          // 400
    {kMorphBloomId, 0.6, RowClass::NonDefault},            // 401: kMaxBloomFraction
    {kMorphPositionId, 3.0, RowClass::NonDefault},         // 402
    {kMorphTravelModeId, 1.0, RowClass::NonDefault},       // 403: Spline
    {kMorphTravelRateId, 1.0, RowClass::NonDefault},       // 404: kMaxTravelRate
    {kMorphSyncId, 0.0, RowClass::ProcessorLocal},         // 405: class 1
    {kMorphSyncNoteId, 0.0, RowClass::ProcessorLocal},     // 406: class 1
    {kMorphWaypointIntervalId, 0.5, RowClass::NonDefault}, // 407: kMinInterval
    {kMorphStateCountId, 4.0, RowClass::NonDefault},       // 408: the COUNT, not an index
    {kMorphState0Id, 1.0, RowClass::NonDefault},           // 409: Bell
    {kMorphState1Id, 2.0, RowClass::NonDefault},           // 410: Choir
    {kMorphState2Id, 3.0, RowClass::NonDefault},           // 411: Glass
    {kMorphState3Id, 4.0, RowClass::NonDefault},           // 412: Breath
    // --- Life Modulators (600-604) -------------------------------------------
    {kLifeSpatialDepthId, 1.0, RowClass::NonDefault},     // 600
    {kLifeSpatialRateId, 0.5, RowClass::NonDefault},      // 601: kMaxRate
    {kLifeSpatialCouplingId, 1.0, RowClass::NonDefault},  // 602
    {kLifeSpatialGrowthId, 1.0, RowClass::NonDefault},    // 603
    {kLifeVoiceWidthId, 150.0, RowClass::NonDefault},     // 604: kMaxVoiceWidthPct
    // --- Voice Envelope (700-704) --------------------------------------------
    {kEnvModeId, 0.0, RowClass::CoincidesWithDefault},  // 700: Standard, class 3
    {kEnvGrowthDurationId, 60.0, RowClass::NonDefault}, // 701: kMaxDuration
    {kEnvStage0MsId, 1.0, RowClass::NonDefault},        // 702
    {kEnvStage1MsId, 1.0, RowClass::NonDefault},        // 703
    {kEnvReleaseMsId, 10000.0, RowClass::NonDefault},   // 704: kMaxStageTimeMs
    // --- Continuous Body (800-812) -------------------------------------------
    {kBodyMaterialId, 2.0, RowClass::NonDefault},                 // 800: Metal Plate
    {kBodyResonanceId, 1.0, RowClass::NonDefault},                // 801
    {kBodyDampingId, 0.0, RowClass::NonDefault},                  // 802: longest ring
    {kBodyKeyTrackingId, 1.0, RowClass::CoincidesWithDefault},    // 803: class 3
    {kBodyDriveId, 4.0, RowClass::NonDefault},                    // 804
    {kBodyMixId, 1.0, RowClass::CoincidesWithDefault},            // 805: class 3
    {kBodyCloudMixId, 1.0, RowClass::NonDefault},                 // 806
    {kBodyCloudDecayId, 30.0, RowClass::NonDefault},              // 807
    {kBodyCloudSizeId, 1.0, RowClass::CoincidesWithDefault},      // 808: class 3
    {kBodyCloudDampingId, 0.0, RowClass::NonDefault},             // 809: longest ring
    {kBodyWidthId, 1.0, RowClass::CoincidesWithDefault},          // 810: class 3
    {kBodyInputAgcId, 1.0, RowClass::CoincidesWithDefault},       // 811: on, class 3
    {kBodyResonatorBypassId, 0.0, RowClass::ScenarioPinned},      // 812: off, class 2
    // --- Granular Atmosphere (1000-1016) -------------------------------------
    {kAtmosLevelId, 2.0, RowClass::NonDefault},              // 1000: kMaxLevel
    {kAtmosBlurId, 1.0, RowClass::NonDefault},               // 1001
    {kAtmosDensityId, 20.0, RowClass::NonDefault},           // 1002: kMaxDensity
    {kAtmosGrainSecondsId, 30.0, RowClass::NonDefault},      // 1003: kMaxGrainSeconds
    {kAtmosDriftDepthId, 1.0, RowClass::NonDefault},         // 1004
    {kAtmosPanSpreadId, 1.0, RowClass::NonDefault},          // 1005
    {kAtmosDecorrelationId, 1.0, RowClass::NonDefault},      // 1006
    {kAtmosFreezeMixId, 1.0, RowClass::NonDefault},          // 1007
    {kAtmosFreezeId, 1.0, RowClass::ScenarioPinned},         // 1008: on, class 2
    {kAtmosDriftSmoothnessId, 1.0, RowClass::NonDefault},    // 1009
    {kAtmosDriftRangeId, 12.0, RowClass::NonDefault},        // 1010: kMaxDriftRangeSemitones
    {kAtmosJitterId, 1.0, RowClass::NonDefault},             // 1011
    {kAtmosPositionId, 30.0, RowClass::NonDefault},          // 1012: kMaxPositionSeconds
    {kAtmosPositionSpreadId, 1.0, RowClass::NonDefault},     // 1013
    {kAtmosPitchId, 24.0, RowClass::NonDefault},             // 1014: kMaxPitchSemitones
    {kAtmosPitchSpreadId, 1.0, RowClass::NonDefault},        // 1015
    {kAtmosGrainEnvelopeId, 3.0, RowClass::NonDefault},      // 1016: Blackman
    // --- Aether Space (1200-1217) --------------------------------------------
    {kAetherMixId, 1.0, RowClass::NonDefault},                    // 1200
    {kAetherSizeId, 1.0, RowClass::ScenarioPinned},               // 1201: RA-1 (c), class 2
    {kAetherDensityId, 1.0, RowClass::ScenarioPinned},            // 1202: RA-1 (c), class 2
    {kAetherDecayId, 60.0, RowClass::NonDefault},                 // 1203: kDecayMaxSeconds
    {kAetherFreezeId, 0.0, RowClass::CoincidesWithDefault},       // 1204: off, class 3
    {kAetherDimensionalityId, 1.0, RowClass::NonDefault},         // 1205
    {kAetherDampingId, 0.0, RowClass::NonDefault},                // 1206: longest ring
    {kAetherPreDelayId, 200.0, RowClass::NonDefault},             // 1207: kMaxPreDelayMs
    {kAetherModDepthId, 1.0, RowClass::NonDefault},               // 1208
    {kAetherModSmoothnessId, 1.0, RowClass::NonDefault},          // 1209
    {kAetherShimmerOctaveId, 1.0, RowClass::NonDefault},          // 1210
    {kAetherShimmerFifthId, 1.0, RowClass::NonDefault},           // 1211
    {kAetherBloomSendId, 1.0, RowClass::NonDefault},              // 1212
    {kAetherBloomDecayId, 1.0, RowClass::NonDefault},             // 1213
    {kAetherSpectralDiffusionId, 1.0, RowClass::NonDefault},      // 1214
    {kAetherSizeBreathDepthId, 1.0, RowClass::NonDefault},        // 1215
    {kAetherTideDepthId, 1.0, RowClass::NonDefault},              // 1216
    {kAetherWidthId, 1.0, RowClass::CoincidesWithDefault},        // 1217: class 3
}};

/// Count of rows in one class - so the three declared exception classes are
/// COMPILE-TIME checked against plan sec. 7.9's stated sizes rather than re-audited.
[[nodiscard]] constexpr std::size_t countRows(RowClass cls) noexcept {
    std::size_t n = 0;
    for (const NonDefaultRow& r : kNonDefaultTable) {
        if (r.cls == cls) {
            ++n;
        }
    }
    return n;
}

static_assert(kNonDefaultTable.size() == 91,
              "SC-009: the table is EXHAUSTIVE over the 91-parameter surface");
static_assert(countRows(RowClass::ProcessorLocal) == 8,
              "plan sec. 7.9 class 1: exactly 8 processor-local rows (0, 100-104, 405, 406)");
static_assert(countRows(RowClass::ScenarioPinned) == 5,
              "plan sec. 7.9 class 2: exactly 5 scenario-pinned rows (1, 812, 1008, 1201, 1202)");
static_assert(countRows(RowClass::CoincidesWithDefault) == 10,
              "plan sec. 7.9 class 3: exactly 10 rows whose most-expensive end coincides with the "
              "registered default (2, 208, 700, 803, 805, 808, 810, 811, 1204, 1217)");
static_assert(countRows(RowClass::NonDefault) == 91 - 8 - 5 - 10,
              "every remaining row follows the table's own most-expensive-end rule");

/// Strictly increasing IDs - which proves there is no duplicate row, and no ID
/// silently overwriting another's value through valueFor().
[[nodiscard]] constexpr bool idsStrictlyIncreasing() noexcept {
    for (std::size_t i = 1; i < kNonDefaultTable.size(); ++i) {
        if (!(kNonDefaultTable[i - 1].id < kNonDefaultTable[i].id)) {
            return false;
        }
    }
    return true;
}
static_assert(idsStrictlyIncreasing(),
              "SC-009: the table must be sorted and duplicate-free, or valueFor() silently returns "
              "the first of two rows for one ID");

/// The table is the ONE source of every applied value. Every caller below reads
/// through this function; nothing re-types a literal at a use site.
[[nodiscard]] constexpr double valueFor(Steinberg::Vst::ParamID id) noexcept {
    for (const NonDefaultRow& r : kNonDefaultTable) {
        if (r.id == id) {
            return r.value;
        }
    }
    return 0.0;  // unreachable: every ID named below is in the table
}

[[nodiscard]] constexpr float tableFloat(Steinberg::Vst::ParamID id) noexcept {
    return static_cast<float>(valueFor(id));
}
[[nodiscard]] constexpr int tableInt(Steinberg::Vst::ParamID id) noexcept {
    return static_cast<int>(valueFor(id));
}
/// Toggles are stored as 1.0 / 0.0; the half-way test avoids an equality compare
/// on a float, which is what the zero-warning gate wants.
[[nodiscard]] constexpr bool tableBool(Steinberg::Vst::ParamID id) noexcept {
    return valueFor(id) >= 0.5;
}

// A handful of spot clauses, so a mis-transcribed row fails the BUILD rather
// than quietly changing what is measured.
static_assert(tableInt(kPolyphonyId) == 8, "class 2: polyphony is pinned to the scenario's 8");
static_assert(tableInt(kMorphStateCountId) == 4, "408 carries the COUNT, not a dropdown index");
static_assert(tableInt(kBodyMaterialId) == 2, "800 = Metal Plate (BodyMaterial index 2)");
static_assert(tableInt(kAtmosGrainEnvelopeId) == 3, "1016 = Blackman (GrainEnvelopeType index 3)");
static_assert(!tableBool(kBodyResonatorBypassId),
              "class 2: 812 is OFF - bypassing the resonators would remove the body engines from "
              "the very chain SC-009 budgets");
static_assert(tableBool(kAtmosFreezeId), "class 2: 1008 is ON - the atmosphere is FROZEN");
static_assert(!tableBool(kAetherFreezeId),
              "1204 is OFF: freeze makes the shimmer and bloom returns inert, i.e. cheaper");
static_assert(tableInt(kSeedId) >= 0
                  && static_cast<std::size_t>(tableInt(kSeedId)) < Seraphis::kSeedValues.size(),
              "row 3 is an INDEX into C-10's curated seed table");

// =============================================================================
// Applying the table through the four DSP routes + the direct ENG setters
// =============================================================================

/// Route VP (37 rows). A transcription of Processor::buildVoiceParams()
/// (processor.cpp:1270-1331), reading the TABLE where that function reads the
/// atomics - field for field, in the same order.
[[nodiscard]] SeraphisVoiceParams tableVoiceParams() noexcept {
    SeraphisVoiceParams p{};

    // -- HarmonicCloud (206, 209, 210) ---------------------------------------
    p.cloudDriftSmoothness = tableFloat(kCloudDriftSmoothnessId);
    p.cloudDecaySec = tableFloat(kCloudDecayId);
    p.cloudEnvOffsetSpread = tableFloat(kCloudEnvOffsetSpreadId);

    // -- SpectralMorphEngine (401, 403, 404, 407) ----------------------------
    p.morphBloom = tableFloat(kMorphBloomId);
    p.morphTravelMode = Seraphis::toTravelMode(tableInt(kMorphTravelModeId));
    p.morphTravelRate = tableFloat(kMorphTravelRateId);
    p.morphWaypointSeconds = valueFor(kMorphWaypointIntervalId);

    // -- Spatial / life modulators (601, 602, 603) ---------------------------
    p.spatialRateHz = tableFloat(kLifeSpatialRateId);
    p.spatialCoupling = tableFloat(kLifeSpatialCouplingId);
    p.spatialGrowth = tableFloat(kLifeSpatialGrowthId);

    // -- Voice envelope (700, 701) -------------------------------------------
    p.envMode = Seraphis::toEnvelopeMode(tableInt(kEnvModeId));
    p.envGrowthDurationSec = tableFloat(kEnvGrowthDurationId);

    // -- ContinuousBody (800, 801, 803-812) ----------------------------------
    p.bodyMaterial = Seraphis::toBodyMaterial(tableInt(kBodyMaterialId));
    p.bodyResonance = tableFloat(kBodyResonanceId);
    p.bodyKeyTracking = tableFloat(kBodyKeyTrackingId);
    p.bodyDrive = tableFloat(kBodyDriveId);
    p.bodyMix = tableFloat(kBodyMixId);
    p.bodyCloudMix = tableFloat(kBodyCloudMixId);
    p.bodyCloudDecaySec = tableFloat(kBodyCloudDecayId);
    p.bodyCloudSize = tableFloat(kBodyCloudSizeId);
    p.bodyCloudDamping = tableFloat(kBodyCloudDampingId);
    p.bodyWidth = tableFloat(kBodyWidthId);
    p.bodyInputAgc = tableBool(kBodyInputAgcId);
    p.bodyResonatorBypass = tableBool(kBodyResonatorBypassId);

    // -- AtmosphereEngine (1002, 1003, 1005-1007, 1009-1016) -----------------
    p.atmosDensity = tableFloat(kAtmosDensityId);
    p.atmosGrainSeconds = tableFloat(kAtmosGrainSecondsId);
    p.atmosPanSpread = tableFloat(kAtmosPanSpreadId);
    p.atmosDecorrelation = tableFloat(kAtmosDecorrelationId);
    p.atmosFreezeMix = tableFloat(kAtmosFreezeMixId);
    p.atmosDriftSmoothness = tableFloat(kAtmosDriftSmoothnessId);
    p.atmosDriftRangeSemis = tableFloat(kAtmosDriftRangeId);
    p.atmosJitter = tableFloat(kAtmosJitterId);
    p.atmosPositionSeconds = tableFloat(kAtmosPositionId);
    p.atmosPositionSpread = tableFloat(kAtmosPositionSpreadId);
    p.atmosPitchSemitones = tableFloat(kAtmosPitchId);
    p.atmosPitchSpread = tableFloat(kAtmosPitchSpreadId);
    p.atmosGrainEnvelope = Seraphis::toGrainEnvelopeType(tableInt(kAtmosGrainEnvelopeId));

    return p;
}

/// Route MB (27 rows). The single mapping from one of C-6's 27 macro targets to
/// the table row that owns its base - the same pairing
/// Processor::baseValueForTarget() makes against the atomics
/// (processor.cpp:1336-1384).
[[nodiscard]] constexpr Steinberg::Vst::ParamID idForTarget(SeraphisMacroTarget t) noexcept {
    using Target = SeraphisMacroTarget;
    switch (t) {
        // -- Voice-owned (19) -------------------------------------------------
        case Target::CloudInharmonicity:   return kCloudInharmonicityId;   // 201
        case Target::CloudMutation:        return kCloudMutationId;        // 203
        case Target::CloudSpectralGravity: return kCloudGravityId;         // 204
        case Target::CloudRichness:        return kCloudRichnessId;        // 200
        case Target::CloudSpectralTiltDb:  return kCloudTiltId;            // 202
        case Target::CloudStereoSpread:    return kCloudStereoSpreadId;    // 207
        case Target::CloudAttackTimeSec:   return kCloudAttackId;          // 208
        case Target::CloudDriftDepthCents: return kCloudDriftDepthId;      // 205
        case Target::MorphEntropy:         return kMorphEntropyId;         // 400
        case Target::MorphTargetPosition:  return kMorphPositionId;        // 402
        case Target::BodyDamping:          return kBodyDampingId;          // 802
        case Target::AtmosLevel:           return kAtmosLevelId;           // 1000
        case Target::AtmosBlur:            return kAtmosBlurId;            // 1001
        case Target::AtmosDriftDepth:      return kAtmosDriftDepthId;      // 1004
        case Target::SpatialDepth:         return kLifeSpatialDepthId;     // 600
        case Target::VoiceWidth:           return kLifeVoiceWidthId;       // 604
        case Target::EnvStage0Ms:          return kEnvStage0MsId;          // 702
        case Target::EnvStage1Ms:          return kEnvStage1MsId;          // 703
        case Target::EnvReleaseMs:         return kEnvReleaseMsId;         // 704
        // -- Aether-owned (8) -------------------------------------------------
        case Target::AetherMix:                     return kAetherMixId;              // 1200
        case Target::AetherSize:                    return kAetherSizeId;             // 1201
        case Target::AetherWidth:                   return kAetherWidthId;            // 1217
        case Target::AetherShimmerOctaveSend:       return kAetherShimmerOctaveId;    // 1210
        case Target::AetherShimmerFifthSend:        return kAetherShimmerFifthId;     // 1211
        case Target::AetherBloomSend:               return kAetherBloomSendId;        // 1212
        case Target::AetherSizeBreathDepth:         return kAetherSizeBreathDepthId;  // 1215
        case Target::AetherDimensionalityTideDepth: return kAetherTideDepthId;        // 1216
        case Target::Count:
        default:
            return kMasterGainId;  // unreachable: the loops below stop at Count
    }
}

/// Route MB. Sets all 27 bases from the table, then puts the five macros at the
/// FR-060 neutral (class 1) so the bases reach the voices UNMODIFIED.
void applyTableMacroBases(SeraphisMacroMatrix& macros) noexcept {
    macros.setMacros(SeraphisMacroValues{});  // the FR-060 neutral, seraphis_macro_matrix.h:122-128
    for (std::size_t t = 0; t < SeraphisMacroMatrix::kNumTargets; ++t) {
        const auto target = static_cast<SeraphisMacroTarget>(t);
        macros.setTargetBase(target, tableFloat(idForTarget(target)));
    }
}

/// Route AE (10 rows). Fills the pack the plugin-side FR-049 free function reads.
void fillTableAetherParams(Seraphis::AetherParams& p) noexcept {
    constexpr auto kRelaxed = std::memory_order_relaxed;
    p.density.store(tableFloat(kAetherDensityId), kRelaxed);                      // 1202
    p.decaySeconds.store(tableFloat(kAetherDecayId), kRelaxed);                   // 1203
    p.freeze.store(tableBool(kAetherFreezeId), kRelaxed);                         // 1204
    p.dimensionality.store(tableFloat(kAetherDimensionalityId), kRelaxed);        // 1205
    p.damping.store(tableFloat(kAetherDampingId), kRelaxed);                      // 1206
    p.preDelayMs.store(tableFloat(kAetherPreDelayId), kRelaxed);                  // 1207
    p.modDepth.store(tableFloat(kAetherModDepthId), kRelaxed);                    // 1208
    p.modSmoothness.store(tableFloat(kAetherModSmoothnessId), kRelaxed);          // 1209
    p.bloomDecay.store(tableFloat(kAetherBloomDecayId), kRelaxed);                // 1213
    p.spectralDiffusion.store(tableFloat(kAetherSpectralDiffusionId), kRelaxed);  // 1214
}

/// Route CFG (5 rows). The four slots, from the table's factory-state indices.
[[nodiscard]] std::array<SpectralState, 4> tableSpectralSlots() {
    return {{
        makeFactoryState(static_cast<SpectralStateId>(tableInt(kMorphState0Id))),  // 409 Bell
        makeFactoryState(static_cast<SpectralStateId>(tableInt(kMorphState1Id))),  // 410 Choir
        makeFactoryState(static_cast<SpectralStateId>(tableInt(kMorphState2Id))),  // 411 Glass
        makeFactoryState(static_cast<SpectralStateId>(tableInt(kMorphState3Id))),  // 412 Breath
    }};
}

/// Route ENG (4 rows: 1, 2, 3, 1008). The direct SeraphisEngine setters, exactly
/// as Processor::pushGlobalParams() drives them (processor.cpp:1070-1116) -
/// including the soft-limit mapping (on -> kOutputSaturation, off -> 0) and
/// C-10's rule that ID 3 carries an INDEX and never the seed constant.
void applyTableEngValues(SeraphisEngine& engine, AetherReverb& reverb) noexcept {
    engine.setPolyphony(static_cast<std::size_t>(tableInt(kPolyphonyId)));  // 1
    engine.setOutputSaturation(tableBool(kSoftLimitId) ? SeraphisEngine::kOutputSaturation
                                                       : 0.0f);            // 2
    const std::uint32_t seed =
        Seraphis::kSeedValues[static_cast<std::size_t>(tableInt(kSeedId))];  // 3
    engine.setSeed(seed);
    reverb.setSeed(seed);
    engine.setAtmosphereFreeze(tableBool(kAtmosFreezeId));  // 1008
}

// =============================================================================
// SC-008 arms 1 and 2: the transcribed pre-slice push block
// =============================================================================

/// The nine class-(b) smoothers, in processor.h:431-441's order and with its
/// seeds: ID 801 (VP), ID 802 (MB), IDs 1215/1216 (MB) and IDs 100-104 (macros).
/// Configured with the SAME two constants the processor uses
/// (Seraphis::kParamSmoothMs / kAetherDepthSmoothMs), so the settled-check this
/// arm times is the settled-check the processor runs.
struct ClassBSmoothers {
    OnePoleSmoother resonance{0.7f};     // ID 801
    OnePoleSmoother bodyDamping{0.25f};  // ID 802
    OnePoleSmoother breathDepth{0.20f};  // ID 1215
    OnePoleSmoother tideDepth{0.20f};    // ID 1216
    std::array<OnePoleSmoother, 5> macro{
        {OnePoleSmoother{0.0f}, OnePoleSmoother{0.0f}, OnePoleSmoother{0.0f},
         OnePoleSmoother{0.5f}, OnePoleSmoother{0.0f}}};

    void configureAll(float sampleRate) noexcept {
        resonance.configure(Seraphis::kParamSmoothMs, sampleRate);
        bodyDamping.configure(Seraphis::kParamSmoothMs, sampleRate);
        breathDepth.configure(Seraphis::kAetherDepthSmoothMs, sampleRate);
        tideDepth.configure(Seraphis::kAetherDepthSmoothMs, sampleRate);
        for (OnePoleSmoother& s : macro) {
            s.configure(Seraphis::kAetherDepthSmoothMs, sampleRate);
        }
    }
};

/// One instance of every parameter pack, exactly as Processor holds them
/// (processor.h:347-356). Non-copyable (std::atomic members), so it is always
/// held by value inside a heap-allocated subject.
struct PushPacks {
    Seraphis::GlobalParams global{};
    Seraphis::MacroParams macro{};
    Seraphis::CloudParams cloud{};
    Seraphis::MorphParams morph{};
    Seraphis::LifeModParams life{};
    Seraphis::BodyParams body{};
    Seraphis::AtmosphereParams atmos{};
    Seraphis::AetherParams aether{};
};

/// The build half of SC-008 arm 2's "build a SeraphisVoiceParams FROM THE
/// ATOMICS": a transcription of Processor::buildVoiceParams()
/// (processor.cpp:1270-1331), field for field and in the same order, including
/// FR-059(b)'s one class-(b) `VP` row (ID 801 reads the SMOOTHER, not the
/// atomic) and the four dropdown_mappings.h conversions.
///
/// WHAT THE PACKS HOLD DOES NOT CHANGE WHAT THIS COSTS - 37 relaxed loads, four
/// index conversions and 592 idempotent setter calls, whatever the values are -
/// so arm 2 leaves them at their registered defaults. The one place cost IS
/// value-dependent is the spectral fan-out, and that payload comes from plan
/// sec. 7.9's table.
[[nodiscard]] SeraphisVoiceParams buildVoiceParamsFromPacks(const PushPacks& packs,
                                                            const ClassBSmoothers& sm) noexcept {
    constexpr auto kRelaxed = std::memory_order_relaxed;
    SeraphisVoiceParams p{};

    p.cloudDriftSmoothness = packs.cloud.driftSmoothness.load(kRelaxed);
    p.cloudDecaySec = packs.cloud.decaySec.load(kRelaxed);
    p.cloudEnvOffsetSpread = packs.cloud.envOffsetSpread.load(kRelaxed);

    p.morphBloom = packs.morph.bloom.load(kRelaxed);
    p.morphTravelMode = Seraphis::toTravelMode(packs.morph.travelMode.load(kRelaxed));
    p.morphTravelRate = packs.morph.travelRate.load(kRelaxed);
    p.morphWaypointSeconds = static_cast<double>(packs.morph.waypointSeconds.load(kRelaxed));

    p.spatialRateHz = packs.life.spatialRateHz.load(kRelaxed);
    p.spatialCoupling = packs.life.spatialCoupling.load(kRelaxed);
    p.spatialGrowth = packs.life.spatialGrowth.load(kRelaxed);

    p.envMode = Seraphis::toEnvelopeMode(packs.life.envMode.load(kRelaxed));
    p.envGrowthDurationSec = packs.life.growthDurationSec.load(kRelaxed);

    p.bodyMaterial = Seraphis::toBodyMaterial(packs.body.material.load(kRelaxed));
    p.bodyResonance = sm.resonance.getCurrentValue();  // FR-059(b): the ONE class-(b) VP row
    p.bodyKeyTracking = packs.body.keyTracking.load(kRelaxed);
    p.bodyDrive = packs.body.drive.load(kRelaxed);
    p.bodyMix = packs.body.mix.load(kRelaxed);
    p.bodyCloudMix = packs.body.cloudMix.load(kRelaxed);
    p.bodyCloudDecaySec = packs.body.cloudDecaySec.load(kRelaxed);
    p.bodyCloudSize = packs.body.cloudSize.load(kRelaxed);
    p.bodyCloudDamping = packs.body.cloudDamping.load(kRelaxed);
    p.bodyWidth = packs.body.width.load(kRelaxed);
    p.bodyInputAgc = packs.body.inputAgc.load(kRelaxed);
    p.bodyResonatorBypass = packs.body.resonatorBypass.load(kRelaxed);

    p.atmosDensity = packs.atmos.density.load(kRelaxed);
    p.atmosGrainSeconds = packs.atmos.grainSeconds.load(kRelaxed);
    p.atmosPanSpread = packs.atmos.panSpread.load(kRelaxed);
    p.atmosDecorrelation = packs.atmos.decorrelation.load(kRelaxed);
    p.atmosFreezeMix = packs.atmos.freezeMix.load(kRelaxed);
    p.atmosDriftSmoothness = packs.atmos.driftSmoothness.load(kRelaxed);
    p.atmosDriftRangeSemis = packs.atmos.driftRangeSemitones.load(kRelaxed);
    p.atmosJitter = packs.atmos.jitter.load(kRelaxed);
    p.atmosPositionSeconds = packs.atmos.positionSeconds.load(kRelaxed);
    p.atmosPositionSpread = packs.atmos.positionSpread.load(kRelaxed);
    p.atmosPitchSemitones = packs.atmos.pitchSemitones.load(kRelaxed);
    p.atmosPitchSpread = packs.atmos.pitchSpread.load(kRelaxed);
    p.atmosGrainEnvelope = Seraphis::toGrainEnvelopeType(packs.atmos.grainEnvelope.load(kRelaxed));

    return p;
}

/// FR-056 / C-3 amendment 2's change threshold on the DERIVED travel rate: 0.1 %
/// of kMinTravelRate. The processor's own constant is TU-local
/// (processor.cpp:283-284), so it is re-derived here from the same DSP constant
/// rather than re-typed as a literal.
constexpr float kSyncedRateEpsilon = SpectralMorphEngine::kMinTravelRate * 1.0e-3f;

/// A TRANSCRIPTION of the pre-slice push block. See this file's banner for why
/// the subject is transcribed rather than driven through Processor, and for the
/// fidelity obligation that comes with it.
struct SteadyStateSubject {
    PushPacks packs{};
    ClassBSmoothers sm{};
    SeraphisMacroMatrix macros{};

    // The ~40 trackers, mirroring processor.h:386-466.
    std::array<float, SeraphisMacroMatrix::kNumTargets> lastPushedBase{};
    bool lastPushedBaseValid = false;
    SeraphisMacroValues lastPushedMacros{};
    bool lastPushedMacrosValid = false;
    std::size_t voiceParamGeneration = 0;
    std::size_t lastAppliedVoiceParamGeneration = 0;
    std::size_t aetherParamGeneration = 0;
    std::size_t lastAppliedAetherParamGeneration = 0;
    bool spectralStatesPending = false;
    bool wasVoiceClassBSettling = false;
    std::size_t lastPushedPolyphony = 8;
    bool lastPushedSoftLimit = true;
    bool lastPushedSoftLimitValid = true;
    int lastPushedSeedIndex = 0;
    bool lastPushedFreeze = false;
    bool lastPushedFreezeValid = true;
    float lastSyncedTravelRate = -1.0f;

    Steinberg::Vst::ProcessContext context{};

    /// processor.cpp:1336-1384, value for value.
    [[nodiscard]] float baseValueForTarget(SeraphisMacroTarget target) const noexcept {
        using Target = SeraphisMacroTarget;
        constexpr auto kRelaxed = std::memory_order_relaxed;
        switch (target) {
            case Target::CloudInharmonicity:   return packs.cloud.inharmonicity.load(kRelaxed);
            case Target::CloudMutation:        return packs.cloud.mutation.load(kRelaxed);
            case Target::CloudSpectralGravity: return packs.cloud.gravity.load(kRelaxed);
            case Target::CloudRichness:        return packs.cloud.richness.load(kRelaxed);
            case Target::CloudSpectralTiltDb:  return packs.cloud.tiltDbPerOct.load(kRelaxed);
            case Target::CloudStereoSpread:    return packs.cloud.stereoSpread.load(kRelaxed);
            case Target::CloudAttackTimeSec:   return packs.cloud.attackSec.load(kRelaxed);
            case Target::CloudDriftDepthCents: return packs.cloud.driftDepthCents.load(kRelaxed);
            case Target::MorphEntropy:         return packs.morph.entropy.load(kRelaxed);
            case Target::MorphTargetPosition:  return packs.morph.position.load(kRelaxed);
            // class (b): the base carries the SMOOTHER, not the atomic.
            case Target::BodyDamping:          return sm.bodyDamping.getCurrentValue();
            case Target::AtmosLevel:           return packs.atmos.level.load(kRelaxed);
            case Target::AtmosBlur:            return packs.atmos.blur.load(kRelaxed);
            case Target::AtmosDriftDepth:      return packs.atmos.driftDepth.load(kRelaxed);
            case Target::SpatialDepth:         return packs.life.spatialDepth.load(kRelaxed);
            case Target::VoiceWidth:           return packs.life.voiceWidthPercent.load(kRelaxed);
            case Target::EnvStage0Ms:          return packs.life.stage0Ms.load(kRelaxed);
            case Target::EnvStage1Ms:          return packs.life.stage1Ms.load(kRelaxed);
            case Target::EnvReleaseMs:         return packs.life.releaseMs.load(kRelaxed);
            case Target::AetherMix:            return packs.aether.mix.load(kRelaxed);
            case Target::AetherSize:           return packs.aether.size.load(kRelaxed);
            case Target::AetherWidth:          return packs.aether.width.load(kRelaxed);
            case Target::AetherShimmerOctaveSend:
                return packs.aether.shimmerOctave.load(kRelaxed);
            case Target::AetherShimmerFifthSend:
                return packs.aether.shimmerFifth.load(kRelaxed);
            case Target::AetherBloomSend:      return packs.aether.bloomSend.load(kRelaxed);
            // class (b), both of them.
            case Target::AetherSizeBreathDepth: return sm.breathDepth.getCurrentValue();
            case Target::AetherDimensionalityTideDepth: return sm.tideDepth.getCurrentValue();
            case Target::Count:
            default:
                return 0.0f;
        }
    }

    /// processor.cpp:1354/1376-1379's per-target class-(b) predicate. PER-TARGET,
    /// never one flag for all 27.
    [[nodiscard]] bool targetClassBUnsettled(SeraphisMacroTarget target) const noexcept {
        switch (target) {
            case SeraphisMacroTarget::BodyDamping:          return !sm.bodyDamping.isComplete();
            case SeraphisMacroTarget::AetherSizeBreathDepth: return !sm.breathDepth.isComplete();
            case SeraphisMacroTarget::AetherDimensionalityTideDepth:
                return !sm.tideDepth.isComplete();
            default:
                return false;
        }
    }

    [[nodiscard]] bool anyMacroSmootherUnsettled() const noexcept {
        for (const OnePoleSmoother& s : sm.macro) {
            if (!s.isComplete()) {
                return true;
            }
        }
        return false;
    }

    /// The five knobs as the class-(b) smoothers currently hold them - never
    /// straight off the atomics (processor.cpp:329-338).
    [[nodiscard]] SeraphisMacroValues readSmoothedMacros() const noexcept {
        SeraphisMacroValues m{};
        m.dream = sm.macro[0].getCurrentValue();
        m.bloom = sm.macro[1].getCurrentValue();
        m.dissolve = sm.macro[2].getCurrentValue();
        m.gravity = sm.macro[3].getCurrentValue();
        m.entropy = sm.macro[4].getCurrentValue();
        return m;
    }

    /// processor.cpp:323-327. SeraphisMacroValues has no operator==.
    [[nodiscard]] static bool macrosEqual(const SeraphisMacroValues& a,
                                          const SeraphisMacroValues& b) noexcept {
        return a.dream == b.dream && a.bloom == b.bloom && a.dissolve == b.dissolve
               && a.gravity == b.gravity && a.entropy == b.entropy;
    }

    /// processor.cpp:1565-1602, with morph sync ON and a CONSTANT tempo - the
    /// arm's stated steady state, and the more expensive of the two (the whole
    /// derivation runs; only the generation bump is skipped).
    void updateSyncedTravelRate() noexcept {
        constexpr auto kRelaxed = std::memory_order_relaxed;
        if (!packs.morph.sync.load(kRelaxed)) {
            if (lastSyncedTravelRate >= 0.0f) {
                lastSyncedTravelRate = -1.0f;
                ++voiceParamGeneration;
            }
            return;
        }
        if ((context.state & Steinberg::Vst::ProcessContext::kTempoValid) == 0
            || !(context.tempo > 0.0)) {
            if (lastSyncedTravelRate >= 0.0f) {
                lastSyncedTravelRate = -1.0f;
                ++voiceParamGeneration;
            }
            return;
        }
        double barBeats = 4.0;
        if ((context.state & Steinberg::Vst::ProcessContext::kTimeSigValid) != 0
            && context.timeSigNumerator > 0 && context.timeSigDenominator > 0) {
            barBeats = static_cast<double>(context.timeSigNumerator)
                       * (4.0 / static_cast<double>(context.timeSigDenominator));
        }
        const int rawIndex = packs.morph.syncNote.load(kRelaxed);
        const int hi = static_cast<int>(Seraphis::kSyncNoteBeats.size()) - 1;
        const auto idx = static_cast<std::size_t>((rawIndex < 0) ? 0
                                                                 : ((rawIndex > hi) ? hi
                                                                                    : rawIndex));
        const double beats =
            Seraphis::kSyncNoteBeats[idx] * (Seraphis::kSyncNoteIsBarDenominated[idx] ? barBeats
                                                                                      : 1.0);
        const auto rate = static_cast<float>(
            std::clamp(context.tempo / (60.0 * beats),
                       static_cast<double>(SpectralMorphEngine::kMinTravelRate),
                       static_cast<double>(SpectralMorphEngine::kMaxTravelRate)));
        if (lastSyncedTravelRate < 0.0f
            || std::fabs(rate - lastSyncedTravelRate) > kSyncedRateEpsilon) {
            lastSyncedTravelRate = rate;
            ++voiceParamGeneration;
        }
    }

    /// ONE process()-entry pass, in process()'s own order (processor.cpp:680-824):
    /// the force-push consume, the handoff consume, pushGlobalParams' four
    /// trackers, the synced-rate comparison, the two generation compares, the
    /// pending flag, the nine smoother targets, the one settled slice's advance,
    /// pushVoiceParams' settled-check + generation compare, and pushMacroSurfaces'
    /// macro compare + 27-target base loop. NOTHING ELSE - no render, no
    /// applyVoiceParams, no setTargetBase: in the steady state every one of those
    /// is exactly what does NOT run.
    ///
    /// @return how many of the surfaces WOULD have been pushed - zero in the
    ///         steady state, and the value the optimization barrier consumes.
    [[nodiscard]] std::size_t runEntryPass() noexcept {
        constexpr auto kRelaxed = std::memory_order_relaxed;
        std::size_t pushes = 0;

        // --- pushGlobalParams() (processor.cpp:1070-1116) --------------------
        const std::size_t poly =
            Seraphis::clampPolyphony(packs.global.polyphony.load(kRelaxed));
        if (poly != lastPushedPolyphony) {
            lastPushedPolyphony = poly;
            ++pushes;
        }
        const bool soft = packs.global.softLimit.load(kRelaxed);
        if (!lastPushedSoftLimitValid || soft != lastPushedSoftLimit) {
            lastPushedSoftLimit = soft;
            lastPushedSoftLimitValid = true;
            ++pushes;
        }
        const int seedIndex = packs.global.seedIndex.load(kRelaxed);
        if (seedIndex != lastPushedSeedIndex) {
            lastPushedSeedIndex = seedIndex;
            ++pushes;
        }
        const bool freeze = packs.atmos.freeze.load(kRelaxed);
        if (!lastPushedFreezeValid || freeze != lastPushedFreeze) {
            lastPushedFreeze = freeze;
            lastPushedFreezeValid = true;
            ++pushes;
        }

        // --- the pre-slice block (processor.cpp:697-716) ---------------------
        updateSyncedTravelRate();                                 // FR-056
        if (aetherParamGeneration != lastAppliedAetherParamGeneration) {  // FR-044
            lastAppliedAetherParamGeneration = aetherParamGeneration;
            ++pushes;
        }
        if (spectralStatesPending) {                              // FR-046
            ++pushes;
        }
        // setParamSmootherTargets() - nine setTarget() calls, unconditional.
        sm.resonance.setTarget(packs.body.resonance.load(kRelaxed));
        sm.bodyDamping.setTarget(packs.body.damping.load(kRelaxed));
        sm.breathDepth.setTarget(packs.aether.sizeBreathDepth.load(kRelaxed));
        sm.tideDepth.setTarget(packs.aether.tideDepth.load(kRelaxed));
        sm.macro[0].setTarget(packs.macro.dream.load(kRelaxed));
        sm.macro[1].setTarget(packs.macro.bloom.load(kRelaxed));
        sm.macro[2].setTarget(packs.macro.dissolve.load(kRelaxed));
        sm.macro[3].setTarget(packs.macro.gravity.load(kRelaxed));
        sm.macro[4].setTarget(packs.macro.entropy.load(kRelaxed));

        // --- the slice loop, settled: ONE slice per block --------------------
        // advanceParamSmoothers(n) then the two pushes (processor.cpp:818-820).
        sm.resonance.advanceSamples(kBlockSize);
        sm.bodyDamping.advanceSamples(kBlockSize);
        sm.breathDepth.advanceSamples(kBlockSize);
        sm.tideDepth.advanceSamples(kBlockSize);
        for (OnePoleSmoother& s : sm.macro) {
            s.advanceSamples(kBlockSize);
        }

        // pushVoiceParams() (processor.cpp:1397-1407)
        const bool settling = !sm.resonance.isComplete();
        if (voiceParamGeneration != lastAppliedVoiceParamGeneration || settling
            || wasVoiceClassBSettling) {
            lastAppliedVoiceParamGeneration = voiceParamGeneration;
            wasVoiceClassBSettling = settling;
            ++pushes;
        }

        // pushMacroSurfaces() (processor.cpp:1422-1465)
        const SeraphisMacroValues macroValues = readSmoothedMacros();
        if (!lastPushedMacrosValid || !macrosEqual(macroValues, lastPushedMacros)
            || anyMacroSmootherUnsettled()) {
            lastPushedMacros = macroValues;
            lastPushedMacrosValid = true;
            ++pushes;
        }
        for (std::size_t t = 0; t < SeraphisMacroMatrix::kNumTargets; ++t) {
            const auto target = static_cast<SeraphisMacroTarget>(t);
            const float value = baseValueForTarget(target);
            if (lastPushedBaseValid && value == lastPushedBase[t]
                && !targetClassBUnsettled(target)) {
                continue;  // ON CHANGE ONLY
            }
            lastPushedBase[t] = value;
            ++pushes;
        }
        lastPushedBaseValid = true;
        return pushes;
    }

    /// Drive the pass until every tracker has converged, so the timed pass really
    /// is the steady state the criterion names and not the first-block force-push.
    void settle() noexcept {
        for (int i = 0; i < 64; ++i) {
            (void)runEntryPass();
        }
    }
};

/// SC-008 arm 2's subject: the FULL push sequence, all four DSP routes.
struct WorstCaseSubject {
    std::unique_ptr<SeraphisEngine> engine;
    std::unique_ptr<AetherReverb> reverb;
    SeraphisMacroMatrix macros{};
    Seraphis::AetherParams aether{};
    std::array<SpectralState, 4> slots{};
    PushPacks packs{};
    ClassBSmoothers sm{};
};

[[nodiscard]] std::unique_ptr<WorstCaseSubject> buildWorstCaseSubject(std::size_t polyphony) {
    auto s = std::make_unique<WorstCaseSubject>();
    s->engine = std::make_unique<SeraphisEngine>();
    s->reverb = std::make_unique<AetherReverb>();
    s->sm.configureAll(static_cast<float>(kSr48));

    s->engine->prepare(kSr48, SeraphisEngineConfig{.voice = SeraphisVoiceConfig{},
                                                   .polyphony = polyphony,
                                                   .seed = std::uint32_t{1}});
    // The SHIPPED reverb config, deliberately: arm 2 measures the push the PLUGIN
    // makes, and applyAetherParams' one loop-shaped setter (setModSmoothness over
    // kMaxChannels / 2) is config-sensitive.
    s->reverb->prepare(kSr48, Seraphis::makeSeraphisReverbConfig(Seraphis::kMaxBlockSamples));

    s->slots = tableSpectralSlots();
    fillTableAetherParams(s->aether);
    return s;
}

// =============================================================================
// SC-009's subject: the hand-built RA-1 row (c) pair
// =============================================================================

/// RA-1 row (c) / dsp/tests/unit/systems/seraphis_perf_test.cpp:702-716's
/// aetherConfigC, at the block size this chain is measured on. numChannels = 16
/// and diffusionFftSize = 4096 are the two fields makeSeraphisReverbConfig
/// structurally cannot produce, and they are why this case does not use it.
[[nodiscard]] AetherReverb::PrepareConfig aetherConfigC() noexcept {
    // Designated initialisers only - no narrowing in brace init.
    return AetherReverb::PrepareConfig{
        .numChannels = std::size_t{16},
        .maxBlockSamples = kBlockSize,
        .maxDelaySeconds = 0.50f,
        .shimmerEnabled = true,
        .shimmerMode = Krate::DSP::PitchMode::Granular,
        .bloomEnabled = true,
        .spectralDiffusionEnabled = true,
        .diffusionFftSize = std::size_t{4096},
        .seed = std::uint32_t{1},
    };
}

/// A harmonic series on 110 Hz into one bloom voice - the 32-resonator half of
/// RA-1 row (c). 32 partials top out at 3520 Hz, well inside the
/// kBloomMaxFreqFraction clamp, so the bank really does hold 32 distinct
/// resonators (seraphis_perf_test.cpp:718-729).
void driveBloom(AetherReverb& reverb) noexcept {
    std::array<float, static_cast<std::size_t>(AetherReverb::kMaxBloomResonators)> partials{};
    for (std::size_t i = 0; i < partials.size(); ++i) {
        partials[i] = 110.0f * static_cast<float>(i + 1u);
    }
    reverb.bloomNoteOn(std::int32_t{0}, partials.data(), partials.size());
}

struct ChainSubject {
    std::unique_ptr<SeraphisEngine> engine;
    std::unique_ptr<AetherReverb> reverb;
    SeraphisMacroMatrix macros{};
    Seraphis::AetherParams aether{};
    std::array<SpectralState, 4> slots{};
};

/// Everything SC-009's scenario needs, assembled in the order the criterion
/// states it. NOTE WHAT IS **NOT** HAND-SET: setSize(1) and setDensity(1) are
/// RA-1 row (c)'s, and they arrive through the TABLE - 1201 via the MB route's
/// AetherSize base and 1202 via applyAetherParams - as do the shimmer sends
/// (1210/1211), the bloom send and decay (1212/1213) and the spectral-diffusion
/// amount (1214). The table genuinely produces configuration (c); nothing here
/// re-types those values.
[[nodiscard]] std::unique_ptr<ChainSubject> buildChainSubject() {
    auto s = std::make_unique<ChainSubject>();
    s->engine = std::make_unique<SeraphisEngine>();
    s->reverb = std::make_unique<AetherReverb>();

    // 1. prepare both halves. Polyphony comes from the table's class-2 row.
    s->engine->prepare(kSr48,
                       SeraphisEngineConfig{
                           .voice = SeraphisVoiceConfig{},
                           .polyphony = static_cast<std::size_t>(tableInt(kPolyphonyId)),
                           .seed = std::uint32_t{1}});
    s->reverb->prepare(kSr48, aetherConfigC());

    // 2. Route CFG, FIRST and while the pool is QUIESCENT: setSpectralState /
    //    setSpectralStateCount are configure-time gated (seraphis_voice.h:765-783)
    //    and a sounding voice would reject them.
    s->slots = tableSpectralSlots();
    s->engine->applySpectralStates(s->slots.data(), tableInt(kMorphStateCountId));

    // 3. Route MB - the 27 bases, then apply() at the FR-060 neutral so the bases
    //    reach the voices unmodified, and the Aether-owned half into the reverb.
    applyTableMacroBases(s->macros);
    s->macros.apply(*s->engine);
    Seraphis::applyAetherTargets(*s->reverb, s->macros.computeAetherTargets());

    // 4. Route VP - the 37 broadcast fields.
    s->engine->applyVoiceParams(tableVoiceParams());

    // 5. Route AE - the ten non-macro reverb controls.
    fillTableAetherParams(s->aether);
    Seraphis::applyAetherParams(*s->reverb, s->aether);

    // 6. The four direct ENG values.
    applyTableEngValues(*s->engine, *s->reverb);

    // 7. The 32-resonator bloom bank of RA-1 row (c).
    driveBloom(*s->reverb);

    // 8. All eight voices sounding, none idle: distinct notes around A3
    //    (MIDI 57 = 220 Hz), so the allocator hands out one slot each rather than
    //    retriggering one. No note-off is ever issued.
    for (std::size_t v = 0; v < kPolyphony; ++v) {
        s->engine->noteOn(static_cast<std::uint8_t>(57u + v), std::uint8_t{100});
    }
    return s;
}

/// Row 800's justification, MEASURED rather than asserted in prose: the active
/// mode count of every material, at the lowest note the scenario plays (220 Hz,
/// where FR-043's Nyquist truncation removes the fewest modes).
struct MaterialSurvey {
    std::array<int, ContinuousBody::kNumMaterials> modeCount{};
    std::size_t worstIndex = 0;
};

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

constexpr float kSurveyNoteHz = 220.0f;  ///< MIDI 57, the lowest note the scenario plays

[[nodiscard]] MaterialSurvey surveyMaterialModeCounts() {
    MaterialSurvey out{};
    Buffers buf;
    for (std::size_t i = 0; i < ContinuousBody::kNumMaterials; ++i) {
        auto body = std::make_unique<ContinuousBody>();
        // setMaterial BEFORE prepare, so no crossfade is armed and the count is
        // the steady-state one (seraphis_perf_test.cpp:584-590).
        body->setMaterial(kMaterials[i]);
        body->prepare(kSr48);
        body->setNoteFrequencyHz(kSurveyNoteHz);
        body->setKeyTracking(1.0f);
        // A few blocks so the control step has run and the slot is configured;
        // getActiveModeCount() reads the SOUNDING slot.
        for (int b = 0; b < kMaterialProbeBlocks; ++b) {
            body->processStereoBlock(buf.inLeft.data(), buf.inRight.data(), buf.outLeft.data(),
                                     buf.outRight.data(), kBlockSize);
        }
        out.modeCount[i] = body->getActiveModeCount();
    }
    const auto worst = std::max_element(out.modeCount.begin(), out.modeCount.end());
    out.worstIndex = static_cast<std::size_t>(worst - out.modeCount.begin());
    return out;
}

// =============================================================================
// Reporting
// =============================================================================

/// One report shape for every arm, so T028 can read the number to check in
/// straight off the output. `ceilingNs <= 0` means "no absolute ceiling here"
/// (SC-008 arm 3), which is stated rather than omitted.
[[nodiscard]] std::string reportArm(const char* name, double measuredNs, double baselineNs,
                                    double ceilingNs, bool baselinePinned) {
    std::ostringstream os;
    os << name << ":\n"
       << "  block budget    : " << kBlockBudgetNs << " ns  (512 samples @ 48 kHz)\n";
    if (ceilingNs > 0.0) {
        os << "  ceiling         : " << ceilingNs << " ns  ("
           << ((ceilingNs / kBlockBudgetNs) * 100.0) << " % of one core)\n";
    } else {
        os << "  ceiling         : none - this arm is gated on its baseline only\n";
    }
    os << "  measured        : " << measuredNs << " ns  ("
       << ((measuredNs / kBlockBudgetNs) * 100.0) << " % of one core)\n"
       << "  baseline in use : " << baselineNs << " ns  ("
       << (baselinePinned ? "PINNED (T028) - gating"
                          : "NOT PINNED - reported, not gating; see the banner")
       << ")\n"
       << "  gate would be   : " << (baselineNs * kRegressionFactor) << " ns  (x"
       << kRegressionFactor << ")\n"
       << "  THIS RUN WOULD   : " << recordingBaselineFor(measuredNs)
       << " ns   <- ceil(measured x " << kBaselineHeadroom
       << "); the shipped baseline is ceil(WORST-OF-FIVE x " << kBaselineHeadroom
       << ") - see BASELINE PROVENANCE, and never re-pin from one run";
    return os.str();
}

/// The baseline clause, in ONE place: it gates where the number is pinned and
/// reports where it is not. All five baselines are pinned as of 2026-08-02, so
/// the reporting branch is currently unreachable; it is kept because it is what
/// makes an unpinned placeholder unable to pass or fail anything, which is the
/// invariant a future deferred baseline would need. The absolute ceilings never
/// route through here - they are asserted directly at every call site, on every
/// run.
void checkAgainstBaseline(const char* name, double measuredNs, double baselineNs,
                          bool baselinePinned) {
    if (baselinePinned) {
        INFO(name << ": measured " << measuredNs << " ns vs gate "
                  << (baselineNs * kRegressionFactor) << " ns");
        REQUIRE(measuredNs <= baselineNs * kRegressionFactor);
    } else {
        WARN(std::string(name)
             + ": baseline NOT PINNED - reported, not gated. A placeholder must not be able "
               "to pass or fail anything; the arm still ran and every absolute ceiling still "
               "gated on this run.");
    }
}

}  // namespace

// =============================================================================
// SC-008: the parameter-push CPU budget (FR-057)
// =============================================================================

TEST_CASE("Seraphis_ParameterPush_CpuBudget", "[.perf]") {
    // -------------------------------------------------------------------------
    // ARM 1 - STEADY STATE. Every generation counter unchanged, every class-(b)
    // smoother settled, the tempo constant: the tracker comparisons, the
    // settled-check and the synced-rate comparison, and nothing else.
    // -------------------------------------------------------------------------
    double steadyStateNs = 0.0;
    {
        auto subject = std::make_unique<SteadyStateSubject>();
        subject->sm.configureAll(static_cast<float>(kSr48));

        // Morph sync ON with a CONSTANT tempo: the arm's stated steady state, and
        // the more expensive of the two branches (the whole rate derivation runs;
        // only the generation bump is skipped).
        subject->packs.morph.sync.store(true, std::memory_order_relaxed);
        subject->context = Steinberg::Vst::ProcessContext{};
        subject->context.tempo = 120.0;
        subject->context.timeSigNumerator = 4;
        subject->context.timeSigDenominator = 4;
        subject->context.state =
            static_cast<Steinberg::uint32>(Steinberg::Vst::ProcessContext::kTempoValid)
            | static_cast<Steinberg::uint32>(Steinberg::Vst::ProcessContext::kTimeSigValid);

        subject->settle();

        // THE PRECONDITION, ASSERTED RATHER THAN ASSUMED: a pass that still
        // pushes something is not the steady state, and would measure the wrong
        // subject.
        REQUIRE(subject->runEntryPass() == 0u);
        for (int i = 0; i < kSteadyStateWarmupCalls; ++i) {
            (void)subject->runEntryPass();
        }

        std::size_t pushAccumulator = 0;
        const auto onePass = [&]() noexcept { pushAccumulator += subject->runEntryPass(); };
        steadyStateNs = bestNsPerCall(kPushTrials, kSteadyStateCallsPerTrial, onePass);
        gSink = gSink + static_cast<double>(pushAccumulator);  // optimization barrier
    }

    WARN(reportArm("SC-008 arm 1 - steady-state process() entry pass", steadyStateNs,
                   kBaselineSteadyStateNs, kSteadyStateCeilingNs, kSc008BaselinesPinned));
    // An arm reporting 0 ns FAILS (plan sec. 7.8).
    REQUIRE(steadyStateNs > 0.0);
    // FR-057's absolute ceiling gates on every run, placeholder or not.
    REQUIRE(steadyStateNs <= kSteadyStateCeilingNs);
    checkAgainstBaseline("SC-008 arm 1", steadyStateNs, kBaselineSteadyStateNs,
                         kSc008BaselinesPinned);

    // -------------------------------------------------------------------------
    // ARM 2 - WORST CASE. The full sequence: build a SeraphisVoiceParams from the
    // atomics + applyVoiceParams, 27 x setTargetBase, applyAetherParams, and ONE
    // applySpectralStates with the FULL 0xFFFF MASK OVER A QUIESCENT POOL - the
    // genuine whole-pool fan-out (16 x 4 = 64 buildSanitized calls = 4096
    // std::log2, plus 64 isValidSpectralState scans, plus 64 128-float array
    // comparisons; seraphis_engine.h:800-804).
    //
    // Reported at polyphony 8 AND 16; THE GATE IS THE WORSE.
    // -------------------------------------------------------------------------
    std::array<double, 2> worstCaseNs{};
    constexpr std::array<std::size_t, 2> kMeasuredPolyphonies = {{8u, 16u}};

    for (std::size_t k = 0; k < kMeasuredPolyphonies.size(); ++k) {
        auto subject = buildWorstCaseSubject(kMeasuredPolyphonies[k]);
        REQUIRE(subject->engine->getPolyphony() == kMeasuredPolyphonies[k]);

        double sink = 0.0;
        const auto onePush = [&]() noexcept {
            // (a) the 37-field broadcast, BUILT FROM THE ATOMICS exactly as
            //     Processor::buildVoiceParams does, then applied to all 16 slots.
            subject->engine->applyVoiceParams(
                buildVoiceParamsFromPacks(subject->packs, subject->sm));
            // (b) the 27 macro bases.
            for (std::size_t t = 0; t < SeraphisMacroMatrix::kNumTargets; ++t) {
                const auto target = static_cast<SeraphisMacroTarget>(t);
                subject->macros.setTargetBase(target, tableFloat(idForTarget(target)));
            }
            // (c) the ten non-macro reverb controls.
            Seraphis::applyAetherParams(*subject->reverb, subject->aether);
            // (d) the whole-pool spectral fan-out. The default argument IS 0xFFFF
            //     (seraphis_engine.h:811-812); it is spelled out so the arm cannot
            //     silently become a masked retry.
            subject->engine->applySpectralStates(subject->slots.data(),
                                                 tableInt(kMorphStateCountId),
                                                 static_cast<std::uint16_t>(0xFFFFu));
            sink += static_cast<double>(subject->macros.getTargetBase(
                SeraphisMacroTarget::CloudRichness));
        };

        for (int i = 0; i < kWorstCaseWarmupCalls; ++i) {
            onePush();
        }
        worstCaseNs[k] = bestNsPerCall(kPushTrials, kWorstCaseCallsPerTrial, onePush);
        gSink = gSink + sink;  // optimization barrier

        // The fan-out has to have LANDED, or the arm timed a pool of rejections
        // rather than 4096 std::log2. A quiescent pool accepts, so every voice
        // must carry the four slots and none may have counted a rejection.
        for (std::size_t v = 0; v < SeraphisEngine::kMaxVoices; ++v) {
            INFO("whole-pool fan-out precondition, voice " << v);
            REQUIRE(subject->engine->getVoice(v).getRejectedConfigureTimeCallCount() == 0u);
        }

        std::ostringstream os;
        os << "SC-008 arm 2 - worst-case push at polyphony " << kMeasuredPolyphonies[k] << ": "
           << worstCaseNs[k] << " ns  (" << ((worstCaseNs[k] / kBlockBudgetNs) * 100.0)
           << " % of one core)";
        WARN(os.str());
        REQUIRE(worstCaseNs[k] > 0.0);
    }

    const double worstPushNs = std::max(worstCaseNs[0], worstCaseNs[1]);
    WARN(reportArm("SC-008 arm 2 - worst-case push (the WORSE of polyphony 8 and 16)", worstPushNs,
                   kBaselineWorstCaseNs, kWorstCaseCeilingNs, kSc008BaselinesPinned));
    // FR-057 clause 1's 0.50 %. If this breaches, the ONE-DIRECTIONAL remedy is
    // plan sec. 7.8's per-block fan-out bound plus spec amendment A9 - never a raised
    // ceiling, never dropping sec. 3.4's identity guard.
    REQUIRE(worstPushNs <= kWorstCaseCeilingNs);
    checkAgainstBaseline("SC-008 arm 2", worstPushNs, kBaselineWorstCaseNs, kSc008BaselinesPinned);

    // -------------------------------------------------------------------------
    // ARM 3 - CLASS-(b) SETTLING. A 512-sample block rendered while a class-(b)
    // ID is under CONTINUOUS automation, so the smoother is never settled and
    // process() runs the block as EIGHT 64-sample sub-slices (plan sec. 3.5.4). This
    // measures WHOLE-BLOCK WALL TIME through the real Processor, because the
    // subject is the sub-slice overhead of the chain and not the pushes alone.
    //
    // Reported against the SAME block rendered UNDIVIDED in the same trial set.
    // Both arms deliver an automation point for the same ID every block; only the
    // VALUE differs (moving vs constant), so the difference is the subdivision
    // and nothing else. NO ABSOLUTE CEILING IS ASSERTED HERE.
    // -------------------------------------------------------------------------
    double settlingNs = 0.0;
    double undividedNs = 0.0;
    {
        // ID 801 (Body Resonance) is the ONE class-(b) VP row (processor.h:431).
        constexpr Steinberg::Vst::ParamID kClassBId = Seraphis::kBodyResonanceId;

        const auto measure = [&](bool automate) {
            SeraphisTest::ProcessorFixture fx;
            REQUIRE(fx.prepare(kSr48, static_cast<Steinberg::int32>(kBlockSize))
                    == Steinberg::kResultOk);

            // Eight sounding voices, so the block being timed is a real render.
            for (int v = 0; v < static_cast<int>(kPolyphony); ++v) {
                fx.pushEvent(Steinberg::Vst::Event::kNoteOnEvent,
                             static_cast<Steinberg::int16>(57 + v), 0.8f);
            }
            REQUIRE(fx.processBlock(static_cast<Steinberg::int32>(kBlockSize))
                    == Steinberg::kResultOk);

            int block = 0;
            const auto renderBlock = [&]() {
                // The automated arm moves the value every block, so the 20 ms
                // smoother is never settled; the reference arm re-sends the SAME
                // value, which leaves it settled and the block undivided.
                const double phase = 6.283185307179586
                                     * (static_cast<double>(block) / 16.0);
                const double value = automate ? (0.5 + 0.45 * std::sin(phase)) : 0.5;
                fx.setParam(kClassBId, value);
                (void)fx.processBlock(static_cast<Steinberg::int32>(kBlockSize));
                ++block;
            };

            for (int i = 0; i < kSettlingWarmupBlocks; ++i) {
                renderBlock();
            }
            const double ns = bestNsPerCall(kSettlingTrials, kSettlingBlocksPerTrial, renderBlock);
            // Optimization barrier: the render's own output, which no compiler can
            // elide across the virtual process() call, drained into the sink.
            gSink = gSink + static_cast<double>(fx.audioL()[0])
                    + static_cast<double>(fx.audioR()[kBlockSize - 1]);
            REQUIRE(fx.checkCanaries());
            return ns;
        };

        settlingNs = measure(true);
        undividedNs = measure(false);
    }

    {
        std::ostringstream os;
        os << "SC-008 arm 3 - class-(b) settling, WHOLE-BLOCK wall time:\n"
           << "  subdivided (8 x 64) : " << settlingNs << " ns/block\n"
           << "  undivided  (1 x 512): " << undividedNs << " ns/block\n"
           << "  ratio               : " << (settlingNs / undividedNs) << " x\n"
           << "  (no absolute ceiling: the phase has no budget for whole-chain render cost - that "
              "is Phase 7 SC-001's, which SC-009 re-measures. If the ratio threatens SC-009's "
              "25 %, the remedy is plan sec. 3.5.4's - coarsen the grid to 128 samples and "
              "re-check "
              "SC-005's positive control - never per-block delivery, never a looser SC-005.)";
        WARN(os.str());
    }
    WARN(reportArm("SC-008 arm 3 - subdivided block", settlingNs, kBaselineSettlingNs, -1.0,
                   kSc008BaselinesPinned));
    WARN(reportArm("SC-008 arm 3 - undivided reference block", undividedNs, kBaselineUndividedNs,
                   -1.0, kSc008BaselinesPinned));
    REQUIRE(settlingNs > 0.0);
    REQUIRE(undividedNs > 0.0);
    checkAgainstBaseline("SC-008 arm 3 (subdivided)", settlingNs, kBaselineSettlingNs,
                         kSc008BaselinesPinned);
    checkAgainstBaseline("SC-008 arm 3 (undivided)", undividedNs, kBaselineUndividedNs,
                         kSc008BaselinesPinned);

    // The barrier must have been written with something real.
    REQUIRE(isFiniteValue(static_cast<float>(gSink)));
}

// =============================================================================
// SC-009: the full-polyphony budget, with the whole 91-row surface non-default
// =============================================================================

TEST_CASE("Seraphis_FullPoly_CpuBudget_WithFullSurface", "[.perf]") {
    // -------------------------------------------------------------------------
    // 0. Row 800 is CHOSEN FROM THE MEASUREMENT, never by hand. If another
    //    material carries more modes, the table row changes and the measurement
    //    is redone - which is what this REQUIRE forces.
    // -------------------------------------------------------------------------
    const MaterialSurvey survey = surveyMaterialModeCounts();
    {
        std::ostringstream os;
        os << "SC-009 row 800 justification - ContinuousBody::getActiveModeCount() at "
           << kSurveyNoteHz << " Hz:\n";
        for (std::size_t i = 0; i < ContinuousBody::kNumMaterials; ++i) {
            os << "  " << kMaterialNames[i] << " : " << survey.modeCount[i] << " modes\n";
        }
        os << "  largest mode set : " << kMaterialNames[survey.worstIndex];
        WARN(os.str());
    }
    REQUIRE(survey.modeCount[survey.worstIndex] > 0);
    INFO("plan sec. 7.9 row 800 = Metal Plate as the modal material with the largest mode set. If "
         "another material wins, CHANGE THE ROW and redo the measurement - do not relax this.");
    REQUIRE(kMaterials[survey.worstIndex] == Seraphis::toBodyMaterial(tableInt(kBodyMaterialId)));

    // -------------------------------------------------------------------------
    // 1. Build the scenario, with the whole 91-row surface applied through the
    //    four DSP routes plus the direct ENG setters.
    // -------------------------------------------------------------------------
    auto subject = buildChainSubject();
    Buffers buf;
    double chainSink = 0.0;

    // The composed chain, exactly as SC-009 names it. NOTHING ELSE is inside the
    // timed region - the push cost is SC-008's subject, not this one. Reading two
    // samples per block is what stops the optimizer dead-coding the render away;
    // a real consumer reads the whole buffer.
    const auto run = [&]() noexcept {
        subject->engine->processStereoBlock(buf.inLeft.data(), buf.inRight.data(), kBlockSize);
        subject->reverb->processStereoBlock(buf.inLeft.data(), buf.inRight.data(),
                                            buf.outLeft.data(), buf.outRight.data(), kBlockSize);
        subject->engine->processOutputStage(buf.outLeft.data(), buf.outRight.data(), kBlockSize);
        chainSink += static_cast<double>(buf.outLeft[0])
                     + static_cast<double>(buf.outRight[kBlockSize - 1]);
    };

    for (int i = 0; i < kChainWarmupBlocks; ++i) {
        run();
    }

    // -------------------------------------------------------------------------
    // 2. The FROZEN atmosphere, ASSERTED before anything is timed.
    //    setAtmosphereFreeze(true) (table row 1008) only ARMS: one slot is retried
    //    per control chunk and a capture is a documented no-op until that voice's
    //    ring holds a whole analysis window. The loop below gives it far more than
    //    the ~16 control chunks the fan-out needs and then ASSERTS rather than
    //    assuming - an un-asserted freeze silently measures the cheaper UNFROZEN
    //    path (1.048 % vs 1.440 % per voice, i.e. 3.1 points at 8 voices on a
    //    25 % ceiling).
    //
    //    The MIX half needs no separate engagement here: table row 1007
    //    (Atmosphere Freeze Mix) is 1.0 and reaches every voice through the VP
    //    route, so renderFreezeLayer's `settledDry` bypass cannot engage. Both
    //    halves are asserted anyway, because "captured" alone would still pass
    //    with a bypassed layer.
    // -------------------------------------------------------------------------
    REQUIRE(subject->engine->getAtmosphereFreeze());
    std::size_t capturedVoices = 0;
    for (int i = 0; (i < 200) && (capturedVoices < kPolyphony); ++i) {
        run();
        capturedVoices = 0;
        for (std::size_t v = 0; v < kPolyphony; ++v) {
            if (subject->engine->getVoice(v).isFreezeCaptured()) {
                ++capturedVoices;
            }
        }
    }
    for (std::size_t v = 0; v < kPolyphony; ++v) {
        INFO("frozen-atmosphere precondition, voice " << v);
        REQUIRE(subject->engine->getVoice(v).isFreezeCaptured());
        REQUIRE(subject->engine->getVoice(v).atmos().getFreezeMix() == Catch::Approx(1.0f));
    }

    // -------------------------------------------------------------------------
    // 3. The rest of the scenario's preconditions. A figure for a configuration
    //    that is not the one named would be the wrong number wearing the right
    //    label, so each is checked rather than assumed.
    // -------------------------------------------------------------------------
    REQUIRE(subject->engine->getPolyphony() == kPolyphony);
    REQUIRE(subject->engine->getActiveVoiceCount() == kPolyphony);
    REQUIRE(subject->engine->getRenderingVoiceCount() >= kPolyphony);
    for (std::size_t v = 0; v < kPolyphony; ++v) {
        INFO("full-surface precondition, voice " << v);
        // Route VP landed (the body's material and the atmosphere's grain window).
        REQUIRE(subject->engine->getVoice(v).body().getMaterial()
                == Seraphis::toBodyMaterial(tableInt(kBodyMaterialId)));
        // Route CFG landed on a quiescent pool - all four slots, zero rejections.
        REQUIRE(subject->engine->getVoice(v).getRejectedConfigureTimeCallCount() == 0u);
        // Route MB landed: richness 1.0 is N(1) = 64 active partials.
        REQUIRE(subject->engine->getVoice(v).cloud().getActivePartialCount()
                == HarmonicCloud::kMaxPartials);
    }
    REQUIRE(subject->macros.getTargetBase(SeraphisMacroTarget::CloudRichness)
            == Catch::Approx(tableFloat(kCloudRichnessId)));

    // -------------------------------------------------------------------------
    // 4. The measurement.
    // -------------------------------------------------------------------------
    const double measuredNs = bestNsPerCall(kChainTrials, kChainBlocksPerTrial, run);
    gSink = gSink + chainSink;  // optimization barrier

    {
        std::ostringstream os;
        os << "SC-009 full-poly composed chain, WHOLE 91-ROW SURFACE NON-DEFAULT\n"
           << "        (polyphony " << kPolyphony
           << ", all sounding, atmosphere FROZEN, body = "
           << kMaterialNames[survey.worstIndex] << ", AetherReverb config (c)):\n"
           << "  block budget    : " << kBlockBudgetNs << " ns  (512 samples @ 48 kHz)\n"
           << "  reference (25 %): " << kFullPolyCeilingNs << " ns/block\n"
           << "  measured        : " << measuredNs << " ns/block  ("
           << ((measuredNs / kBlockBudgetNs) * 100.0) << " % of one core)\n"
           << "  MARGIN RECORDED : the SHIPPED plugin prepares a CHEAPER reverb "
              "(numChannels 8, diffusionFftSize 1024, seraphis_engine_config.h:71,:82); "
              "RA-1 row (c) is a deliberate worst case above it.\n"
           << "  admissible base : " << kMaxAdmissibleFullPolyNs << " ns/block  (25 % / "
           << kRegressionFactor << " = "
           << ((kMaxAdmissibleFullPolyNs / kBlockBudgetNs) * 100.0) << " % of one core)\n"
           << "  baseline in use : " << kBaselineFullPolyNs
           << " ns/block  (PINNED 2026-08-02, CEILING-DERIVED - gating)\n"
           << "  gate            : " << (kBaselineFullPolyNs * kRegressionFactor)
           << " ns/block  (x" << kRegressionFactor << ")\n"
           << "  this run would  : " << recordingBaselineFor(measuredNs)
           << " ns/block   <- ceil(measured x " << kBaselineHeadroom << ")\n"
           << "  STATUS          : "
           << ((recordingBaselineFor(measuredNs) <= kMaxAdmissibleFullPolyNs)
                   ? "this run's derived baseline would be admissible. The shipped baseline "
                     "is still the CEILING-DERIVED 2318840 pinned 2026-08-02 (banner) - "
                     "never re-pin from one run."
                   : "this run's derived baseline exceeds the admissible ceiling. The gate "
                     "below is what decides; see \"SC-009: HOW ITS BASELINE WAS PINNED\" in "
                     "this file's banner. Do NOT raise the ceiling.");
        WARN(os.str());
    }

    REQUIRE(measuredNs > 0.0);
    // SC-009's 25 % ceiling. If this fails, the lever is the shipped voice count
    // or Phase 9's own push cost - NEVER the 25 % ceiling, never a Phase 2/4/5/6
    // gate.
    REQUIRE(measuredNs <= kFullPolyCeilingNs);
    checkAgainstBaseline("SC-009", measuredNs, kBaselineFullPolyNs, kSc009BaselinePinned);

    // -------------------------------------------------------------------------
    // 5. THE 16-VOICE FIGURE: MEASURED AND RECORDED, NON-GATING (plan sec. 7.9).
    //    Same surface, same reverb, polyphony raised to kMaxVoices with all 16
    //    sounding. It is written into the roadmap amendment; it gates nothing
    //    here, so no ceiling and no baseline is asserted against it.
    // -------------------------------------------------------------------------
    {
        auto poly16 = buildChainSubject();
        poly16->engine->setPolyphony(SeraphisEngine::kMaxVoices);
        for (std::size_t v = kPolyphony; v < SeraphisEngine::kMaxVoices; ++v) {
            poly16->engine->noteOn(static_cast<std::uint8_t>(57u + v), std::uint8_t{100});
        }
        REQUIRE(poly16->engine->getPolyphony() == SeraphisEngine::kMaxVoices);

        Buffers buf16;
        double sink16 = 0.0;
        const auto run16 = [&]() noexcept {
            poly16->engine->processStereoBlock(buf16.inLeft.data(), buf16.inRight.data(),
                                               kBlockSize);
            poly16->reverb->processStereoBlock(buf16.inLeft.data(), buf16.inRight.data(),
                                               buf16.outLeft.data(), buf16.outRight.data(),
                                               kBlockSize);
            poly16->engine->processOutputStage(buf16.outLeft.data(), buf16.outRight.data(),
                                               kBlockSize);
            sink16 += static_cast<double>(buf16.outLeft[0])
                      + static_cast<double>(buf16.outRight[kBlockSize - 1]);
        };
        for (int i = 0; i < kChainWarmupBlocks; ++i) {
            run16();
        }
        REQUIRE(poly16->engine->getActiveVoiceCount() == SeraphisEngine::kMaxVoices);

        const double ns16 = bestNsPerCall(kChainTrials, kChainBlocksPerTrial, run16);
        gSink = gSink + sink16;  // optimization barrier

        std::ostringstream os;
        os << "SC-009 NON-GATING 16-voice figure: " << ns16 << " ns/block  ("
           << ((ns16 / kBlockBudgetNs) * 100.0) << " % of one core). Recorded for the roadmap "
              "amendment; it gates nothing.";
        WARN(os.str());
        REQUIRE(ns16 > 0.0);
    }

    REQUIRE(isFiniteValue(static_cast<float>(gSink)));
}
