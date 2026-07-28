// ==============================================================================
// Layer 3: System Tests - AtmosphereEngine, CPU budget (SC-004)
//                                        (specs/seraphis-phase5-atmosphere)
// ==============================================================================
// Constitution Principle XII: Test-First Development.
//
// Reference: specs/seraphis-phase5-atmosphere/spec.md
//            specs/seraphis-phase5-atmosphere/plan.md   (S15.4)
//            specs/seraphis-phase5-atmosphere/tasks.md  (T002 creates this TU;
//                                                        T017 adds the grain-sample
//                                                        cost probe, T018 the five
//                                                        SC-004 baselines)
//
// SCOPE OF THIS TU (plan.md S15's TU-ownership table):
//   SC-004  AtmosphereEngine_CpuBudget          - tagged "[.perf]" (T018)
//   FR-022's kMaxGrains micro-benchmark
//           AtmosphereEngine_GrainSampleCost    - tagged "[.perf]" (T017, below)
//   Nothing else. Both cases are [.perf] so CI does not run the TIMING
//   (.github/workflows/ci.yml excludes perf-tagged cases), but the
//   static_asserts below are evaluated by every CI leg regardless of tags -
//   which is exactly why the absolute gate is placed there.
//
// WHY ns/block AND NOT "% of one core":
//   A percent-of-core figure is not reproducible across dev machines or CI
//   runners - identical code passes or fails by hardware. SC-004 pins the
//   measurement basis to NANOSECONDS PER 512-SAMPLE BLOCK at 48 kHz, the basis
//   established by dsp/tests/unit/systems/harmonic_cloud_perf_test.cpp:69-101
//   and reused by continuous_body_perf_test.cpp:108-137, and gates against a
//   CHECKED-IN BASELINE as a relative regression bound (fail if > baseline
//   x 1.5). The percent-of-budget figure is REPORTED via WARN, never asserted.
//
// HOW THE ABSOLUTE ROADMAP FIGURE IS STILL BOUND (plan S15.4):
//   Each checked-in baseline carries BOTH compile-time clauses
//
//       static_assert(kBaselineX * kRegressionFactor <= kReferenceNs, ...);
//       static_assert(kBaselineX >= kReferenceNs / 50.0, ...);
//
//   alongside the run-time REQUIRE(measured <= kBaselineX * kRegressionFactor).
//   The first makes a too-weak baseline fail to COMPILE, so the run-time
//   REQUIRE transitively binds the reference on every machine. The second
//   catches a baseline recorded from a no-op or misconfigured run - there the
//   MEASUREMENT is wrong, not the threshold.
//
//   THE REFERENCE IS 1.5 %, NOT 1 %, SINCE 2026-07-28 (user budget decision at
//   SC-004 lever 6, option 1) = 160,000 ns/block. Configuration (c) - the
//   saturated 64-grain pool - is OUT-OF-REGION under that decision: still
//   measured, still regression-tracked against its own checked-in baseline, but
//   carrying no headroom clause and not gated against the reference. See the
//   BASELINE PROVENANCE block below, spec.md's dated SC-004 amendment, and
//   compliance.md section 1.
//
// TRIAL SHAPE (plan S15.4): best-of-25 x 500 blocks after 400 warm-up blocks.
//   Many short trials, because the dev machine is a hybrid part and the
//   dominant noise source is a whole trial migrating onto an E-core. Affinity
//   pinning was tried and REJECTED in both reference perf TUs.
//
// FIVE CONFIGURATIONS, each with its own baseline. (a) defaults, blur off,
//   freeze off; (b) defaults, blur on; (c) pool saturated (64 concurrent), blur
//   on, blurFftSize = 256; (d) freezeMix = 1.0 with the grain layer still
//   running; (e) configuration (b) plus setGrainEnvelope() called once per block
//   with an ALTERNATING GrainEnvelopeType, so the idempotence guard cannot elide
//   the 4096-entry envelope regeneration. (a), (b), (d) and (e) are gated
//   against the SAME 160,000 ns reference; (c) is out-of-region (above).
//
// IF A MEASUREMENT IS OVER BUDGET: REDUCE COST, NEVER RAISE THE BASELINE.
//   Raising the REFERENCE is not a code decision and was not taken here - it was
//   taken once, by the user, on 2026-07-28, from the five measured figures.
//
// WHY THIS TU IS NOT IN THE -fno-fast-math LIST: -fno-fast-math would change
//   the very figures the baselines are pinned to. See dsp/tests/CMakeLists.txt.
//
// ALLOCATION DETECTION: this TU must NOT include
//   <allocation_operator_overrides.h> - the single owner in dsp_systems_tests is
//   dsp/tests/unit/systems/selectable_oscillator_test.cpp:388, and a second
//   include is a duplicate-symbol link error. <allocation_detector.h> only.
//
// No std::isnan / std::isinf / numeric_limits infinity anywhere: the macOS leg
// builds with -ffast-math, which folds them. Finiteness is checked on the
// IEEE-754 exponent field instead.
//
// FTZ/DAZ: dsp_test_main.cpp:13 calls enableFTZDAZ() before any case runs, so
// every figure below is measured with denormals flushed BY THE PROCESS - the
// same environment the audio thread runs in.
//
// Run them explicitly (they are tag-excluded everywhere else):
//   build/windows-x64-release/bin/Release/dsp_systems_tests.exe "AtmosphereEngine_GrainSampleCost*"
//   build/windows-x64-release/bin/Release/dsp_systems_tests.exe "AtmosphereEngine_CpuBudget*"
// ==============================================================================
//
// ------------------------------------------------------------------------------
// MEASUREMENT RECORD - AtmosphereEngine_GrainSampleCost (T017, FR-022 / O-3)
// ------------------------------------------------------------------------------
//   Machine    : 13th Gen Intel(R) Core(TM) i9-13900HX, Windows 11 Pro 26200
//   Build      : MSVC Release, build/windows-x64-release
//   Trial shape: best-of-25 x 500 blocks after 400 warm-up blocks
//
//   MEASURED, three consecutive runs of this case on an otherwise idle machine:
//
//     run                                    |   1      2      3    | worst
//     captureSeconds = 30 (16.8 MB ring)     |  9.962  9.551  9.702 |  9.962
//     captureSeconds =  8 (4.19 MB ring)     |  9.376  9.596 10.320 | 10.320
//     ratio to the arithmetic ceiling, 30 s  |  3.060  2.934  2.980 |  3.060
//     ratio to the arithmetic ceiling,  8 s  |  2.880  2.948  3.170 |  3.170
//
//   arithmetic ceiling (106,667 ns / (64 x 512))    :  3.255 ns per grain-sample
//
//   RE-MEASURED TWICE SINCE, one run of this case each time:
//     T022 verification pass : 30 s 8.06287, 8 s 7.87372 ns, ratio 1.02402
//     2026-07-28 amendment   : 30 s 7.59286, 8 s 7.39566 ns, ratio 1.02666
//   Against the AMENDED ceiling (160,000 / (64 x 512) = 4.883 ns) the latest
//   pair is 1.555x and 1.515x over; against the withdrawn 3.255 ns ceiling the
//   T022 pair was 2.48x/2.42x. THE RATIO IS THE LOAD-BEARING NUMBER AND IT DOES
//   NOT MOVE: ~1.02 across a 4x difference in ring bytes, at every re-measure.
//
//   VERDICT: 64 CONCURRENT GRAINS DO NOT FIT, at either reference. The measured
//   cost is ~1.6x the amended ceiling (was ~2.9-3.2x the withdrawn one) at BOTH
//   ring sizes, so a saturated pool costs ~3 % of one core. That is FR-022's
//   stated failure condition, and FR-022's response - SC-004 lever (5) - was
//   MEASURED AND REFUSED below; the residual was carried by the 2026-07-28
//   budget decision instead, which puts the saturated pool out-of-region rather
//   than shrinking kMaxGrains. kMaxGrains STAYS AT 64.
//
//   AND THE REASON IS NOT THE ONE FR-022 EXPECTED. FR-022 argued the dominant
//   term would be memory: "up to ~128 independent, non-sequential read streams
//   into a multi-megabyte buffer - an L3/DRAM-miss workload". The measurement
//   refutes that. The 30 s ring is 4x the bytes of the 8 s ring and costs the
//   same per grain-sample to within the run-to-run spread (the 8 s figure is the
//   larger one in run 3), and a further probe at captureSeconds = 1 - a
//   256 KB/channel ring that fits in L2 - also landed inside that spread. The
//   grain path is INSTRUCTION-BOUND. Two consequences follow, and both are
//   load-bearing for T019 below:
//     1. There is no cache-shaped saving available. The optimisations that DID
//        land (T019's record) were all instruction-count ones.
//     2. Reducing kMaxGrains does not make a grain-sample cheaper. It only makes
//        fewer of them, and only in configurations that actually reach the cap.
// ------------------------------------------------------------------------------
//
// ------------------------------------------------------------------------------
// BASELINE PROVENANCE - AtmosphereEngine_CpuBudget (T018, SC-004)
// ------------------------------------------------------------------------------
//   Machine    : 13th Gen Intel(R) Core(TM) i9-13900HX, Windows 11 Pro 26200,
//                idle, on AC - the same machine as
//                harmonic_cloud_perf_test.cpp:106-109 and
//                continuous_body_perf_test.cpp:142-145, so the three phases'
//                figures are comparable and Phase 7 can add them up.
//   Build      : MSVC Release, build/windows-x64-release
//   Trial shape: best-of-25 x 500 blocks after 400 warm-up blocks, each preceded
//                by the conditioning pass documented at runBudgetTrials().
//   Measured   : eight consecutive runs, machine otherwise idle. Figures below
//                are ns per 512-sample block, as printed by this case's own WARN
//                block, transcribed verbatim.
//
//   SC-004 pins each baseline to the WORST (largest) of its eight figures,
//   rounded up, with NO PADDING - the baseline is a measurement, not an
//   allowance.
//
//   run                          |    1      2      3      4      5      6      7      8   |  min     max
//   (a) defaults, no blur/freeze |  84864  84468  82408  83223  82450  84129  86305  85966 |  82408   86305
//   (b) defaults, blur on        | 110297 108557 108755 106052 107648 107478 106274 111815 | 106052  111815
//   (c) saturated 64, blur fft256| 323119 319849 327996 333161 321300 343805 331937 331054 | 319849  343805
//   (d) freezeMix 1.0 + grains   | 133312 141611 133952 153651 130854 137630 134251 133305 | 130854  153651
//   (e) (b) + envelope churn     | 105465 108551 109956 104630 110919 111008 107514 106330 | 104630  111008
//
//   As a fraction of one core, worst of eight:
//       (a) 0.809 %   (b) 1.048 %   (c) 3.223 %   (d) 1.440 %   (e) 1.041 %
//
//   *** THE 2026-07-28 BUDGET DECISION, AND WHY THESE FIGURES ARE NOW BASELINES.
//
//   Until that date not one of the five constants below could be replaced by its
//   measurement: kMaxAdmissibleBaselineNs was 71,111 ns against a smallest
//   worst-of-eight of 86,305 ns, so any substitution broke
//   `static_assert(baseline * kRegressionFactor <= kReferenceNs)` and the TU
//   stopped compiling - that clause working, not failing. Per this block's own
//   standing instruction ("a replacement that has to go UP means the
//   configuration is over budget and SC-004's lever list applies instead -
//   reduce cost, never raise a baseline") the constants stayed at a placeholder,
//   the levers were spent (T019 below), the phase was re-measured, and the
//   residual was escalated at lever (6).
//
//   THE USER TOOK LEVER (6) ON 2026-07-28, option 1: Phase 5's per-voice
//   allowance is RAISED from 1 % to 1.5 % of one core - reference 160,000
//   ns/block - DERIVED FROM THE FIVE FIGURES IN THE TABLE ABOVE, and the
//   saturated-64 configuration (c) is declared OUT-OF-REGION: still measured and
//   regression-tracked, not gated against the ceiling. Phase 7 will re-derive its
//   polyphony tally from the real figures rather than from a 1 % nobody reached.
//
//   Consequently the constants below are MEASUREMENTS again, one per
//   configuration, by the rule
//       baseline = min( ceil(worst-of-eight x 1.05), 106,666 )
//   with 106,666 the largest whole nanosecond satisfying the headroom clause
//   against the amended reference. NOTHING HERE WAS RAISED BY CODE: the levers
//   are still spent, the 1.05 is measured run-to-run spread, and the ONE number
//   that moved is the reference the user moved.
//
//   NOTE ON THE GATE'S ARITHMETIC, because it is easy to misread the verdict,
//   and on the ONE thing the amendment costs.
//
//   Composed, `baseline * 1.5 <= reference` and `measured <= baseline * 1.5`
//   cap any MEASUREMENT at the reference itself, 160,000 ns = 1.5 % of one core.
//   The tighter bound only exists while SC-004's "a baseline is the measurement,
//   not an allowance" rule holds, because then measured == baseline <=
//   reference / 1.5. That rule STILL HOLDS for (a) - baseline 91,000, gate
//   136,500, a real 5 %-spread regression bound comfortably under the reference.
//
//   IT DOES NOT HOLD FOR (b), (d) AND (e), and that is the honest price of the
//   amendment: their worst-of-eight figures (111,815 / 153,651 / 111,008) sit
//   ABOVE reference / 1.5 = 106,666.67, so no baseline equal to the measurement
//   can satisfy the headroom clause. Their baselines are therefore the CAP, not
//   their measurements, which makes their runtime gate the reference itself
//   (106,666 x 1.5 = 159,999) and their regression headroom ~43 % instead of
//   ~5 %. What they still enforce, absolutely, is 1.5 % of one core. Recorded as
//   a deviation in spec.md's SC-004 amendment rather than left implicit; if a
//   later pass reduces (b)/(d)/(e) below 106,666 the baselines must go back to
//   being their measurements and the tight regression bound returns.
// ------------------------------------------------------------------------------
//
// ------------------------------------------------------------------------------
// T019 DECISION RECORD - WHICH SC-004 LEVER WAS SPENT (tasks.md T019)
// ------------------------------------------------------------------------------
//   VERDICT: LEVERS (1), (2) AND (4) AUDITED AND HELD. A SIXTH, UNLISTED LEVER
//   WAS FOUND AND SPENT IN FULL - four instruction-count defects on the hot
//   paths, worth 1.60x on (a), 1.61x on (b), 2.39x on (c), 1.99x on (d) and
//   2.77x on (e). LEVERS (3), (3b) AND (5) WERE MEASURED AND DELIBERATELY NOT
//   SPENT, because measurement says none of them closes a criterion. THE
//   RESIDUAL GAP WAS ESCALATED AT LEVER (6).
//
//   LEVER (6) WAS RESOLVED BY THE USER ON 2026-07-28, option 1 of the three
//   offered below: Phase 5's per-voice allowance is raised from 1 % to 1.5 % of
//   one core (reference 160,000 ns/block), DERIVED FROM the five measured
//   figures, with the saturated-64 configuration (c) declared out-of-region -
//   measured and regression-tracked, not gated against the ceiling. Phase 7
//   re-derives its polyphony tally from the real figures. Option 2 (drop or
//   share the freeze leg) and option 3 (shrink FR-073's operating rule, i.e.
//   lever (5) renamed) were NOT taken, so nothing below is withdrawn: no code
//   lever was spent to close this, and none may now be skipped on the grounds
//   that the reference moved.
//
//   BEFORE / AFTER, ns per 512-sample block, same machine, same trial shape:
//
//     configuration                     before      after (worst of 8)   factor
//     (a) defaults, no blur/freeze     137,666           86,305           1.60x
//     (b) defaults, blur on            179,926          111,815           1.61x
//     (c) saturated 64, blur fft 256   821,349          343,805           2.39x
//     (d) freezeMix 1.0 + grains       305,506          153,651           1.99x
//     (e) (b) + envelope churn         306,957          111,008           2.77x
//     per grain-sample, 8 s ring        16.74             10.32           1.62x
//     per grain-sample, 30 s ring       18.13              9.96           1.82x
//
//   THE AUDIT FIRST. Levers (1), (2) and (4) are audits, not code changes: each
//   asks whether a cost the design already excludes has crept back in. All three
//   held, so none of them had a saving left to give:
//
//     (1) FR-052's freeze hard-bypass DOES engage at a settled m == 0.
//         atmosphere_engine.h tests
//         freezeMixRamp_.isComplete() && getTarget() == 0.0f and fills zeros
//         rather than running the two oscillators. (The latency-matching delay
//         is still advanced with zeros; that is pinned as a decision, and it is
//         ~1 % of the oscillator cost the bypass saves - not a lever.)
//     (2) FR-005's control-step decimation DOES fire. runControlStep() is called
//         only at phase == 0 of the ABSOLUTE grid, i.e. once per
//         kControlChunkSamples = 64 samples, never per sample. The scheduler
//         push, the OU bank advance, the per-grain ratio refresh and the
//         1/sqrt(n) target are therefore paid 8 times per measured 512-sample
//         block rather than 512 times - the 64x error this lever exists to catch.
//     (4) The equal-power pan cos/sin ARE birth-time only, inside
//         tryBirthGrain(), reached once per SUCCESSFUL birth. There is no
//         per-sample transcendental anywhere on the grain path.
//
//   WHAT WAS ACTUALLY SPENT: FOUR INSTRUCTION-COUNT DEFECTS, THREE OF WHICH DO
//   NOT CHANGE THE RENDER AT ALL AND THE FOURTH OF WHICH CHANGES IT BY AT MOST
//   1e-20 PER SPECTRAL BIN. The whole DSP suite (all five layer targets, 22.9 M
//   assertions) is green before and after each of them.
//
//     (i)   RUNTIME `%` IN THREE RING-WRAP HOT LOOPS -> MASK / SPLIT RUNS.
//           `idx % v.size()` on a runtime value is a hardware integer divide,
//           ~20-26 cycles, and it does not vectorise. Three sites were on
//           per-sample or per-bin loops:
//             - SpectralFreezeOscillator's overlap-add ring, wrapped fftSize_
//               times per synthesised frame plus once per output sample. Its
//               length is 2 * fftSize_ and fftSize_ is snapped to a power of two
//               at prepare, so the wrap is now `& outputMask_` - exact.
//             - STFT::pushSamples, one divide per pushed sample per channel.
//               Replaced by at most two contiguous std::copy_n runs.
//             - STFT::analyze, one divide per FFT-size sample per frame.
//               Replaced by two contiguous windowing runs.
//           Bit-identical output. Worth ~2.1x on the blur stage and ~2.1x on the
//           freeze leg on its own.
//     (ii)  std::floor ON THE GRAIN INNER LOOP -> TRUNCATION.
//           roundss needs SSE4.1, which MSVC does not target on the default
//           /arch, so std::floor(float) compiles to a CRT CALL. There were three
//           per grain-sample (two ring reads plus the read-position carry). All
//           three operands are provably non-negative at the call site, where
//           truncation IS the floor, so the calls became one vcvttss2si each -
//           and the integer they produce is the one the indexing needed anyway.
//           Bit-identical. Worth ~1.35x on the grain layer on its own.
//     (iii) GrainEnvelope::lookup's std::clamp AND size_t CONVERSIONS.
//           std::clamp returns a const reference, so MSVC materialised both
//           bounds on the stack and cmov'd between three ADDRESSES; and x86-64
//           has no unsigned-64 <-> float instruction below AVX-512, so both
//           conversions expanded to a compare, a branch and a fix-up. Two ordered
//           comparisons and ptrdiff_t intermediates replace them. Value-identical
//           on the defined domain, and STRICTLY SAFER off it: a NaN phase used to
//           reach `static_cast<size_t>(NaN)` and index out of range.
//     (iv)  THE 4096-ENTRY ENVELOPE REGENERATION -> A prepare()-TIME BANK.
//           setGrainEnvelope() regenerated the window in place, one or two
//           transcendentals per entry, on the audio thread. Configuration (e)
//           does that once per block by construction, and it MEASURED at ~127,000
//           ns/block - 119 % of the entire per-voice budget, from one setter.
//           prepare() now generates all kEnvelopeTypeCount windows (98 KiB, 2.3 %
//           of the default ring) and the setter stores an enum. Every table is
//           bit-identical, FR-027's endpoint conditioning included. (e) - (b)
//           fell from ~127,000 ns to ~2,700.
//
//     A FIFTH, SMALLER ONE, listed for completeness because it DOES touch the
//     render: SpectralFreezeOscillator's per-bin `mag * std::cos(phase)` /
//     `mag * std::sin(phase)` pair became the SIMD reconstructCartesianBulk that
//     SpectralBuffer already uses for the identical expression. 2050 scalar
//     transcendentals per synthesis frame became one Highway-dispatched call.
//     The only behavioural difference is that bins with magnitude < 1e-20, which
//     the scalar loop forced to exactly (0, 0), are now multiplied like any other
//     bin - a difference of at most 1e-20 per bin, below the denormal floor
//     FTZ/DAZ flushes on the audio thread and ~140 dB under the oscillator's own
//     +/-2.0 output clamp. Its whole processor-layer suite is green.
//
//   THE LEVERS THAT WERE MEASURED AND NOT SPENT, WITH THE MEASUREMENT:
//
//     (3)  Drop PrepareConfig::blurFftSize's default to 512. NOT SPENT: it does
//          not reduce the blur cost. At 75 % overlap the stage's per-block cost
//          is (frames per block) x (cost per frame) = hop^-1 x O(N log N), i.e.
//          O(log N) per sample - halving N halves the per-frame cost and DOUBLES
//          the frame count. Measured directly: configuration (c) runs
//          blurFftSize = 256 against (b)'s 1024 and its blur term is not
//          proportionally cheaper. The lever would spend RA-3's latency figure
//          and the documented capability for no measured saving.
//     (3b) Swap ratioAtPitch() off semitonesToRatio (one powf) onto
//          centsToPitchRatio (one exp2). NOT SPENT: the call is per grain per
//          CONTROL STEP, not per sample - 8 control steps x 64 grains = 512 powf
//          per block at saturation, against ~2 million grain-sample operations in
//          the same block. It is under 1 % of configuration (c). This was
//          HarmonicCloud's largest lever because that component calls it per
//          partial per control step with no per-sample term to dwarf it.
//     (5)  Reduce kMaxGrains below 64. NOT SPENT, AND THIS IS THE LOAD-BEARING
//          ONE, so the reasoning is spelled out rather than asserted:
//            - FR-022 predicted the binding term would be memory. It is not:
//              see the MEASUREMENT RECORD above. The cost is instruction-bound,
//              so a smaller pool does not make a grain-sample cheaper.
//            - Four of the five configurations - (a), (b), (d), (e) - run at
//              FR-009's default of 4 grains/s x 4 s = 16 concurrent grains. The
//              cap never binds there, so lever (5) changes their figures by
//              exactly zero. Every one of them is over the gate.
//            - The fifth, (c), is the only one the cap touches. At the largest
//              pool that satisfies FR-022's arithmetic ceiling - kMaxGrains = 16,
//              from 106,667 / (512 x 10.32) = 20.2 - configuration (c) computes
//              to 16 x 512 x 10.32 + ~15,300 (its measured blur term) =
//              ~99,900 ns/block. Still 1.40x the then-admissible baseline of
//              71,111. The lever did not close the criterion it exists for.
//              (Under the amended reference the arithmetic is not re-run in the
//              lever's favour: (c) is out-of-region by decision, not by pool
//              size, and kMaxGrains stays 64 for the reasons immediately below.)
//            - And it would cost real capability: kMaxGrains = 16 makes FR-009's
//              OWN DEFAULT control table permanently pool-saturated (its mean
//              concurrent count is exactly 16), so the shipped default would sit
//              on FR-023's skip path, FR-073's documented operating region would
//              collapse to the default itself, and SC-003's D-17 precondition
//              table would move under it.
//          Spending a capability lever that buys no criterion is not "reduce
//          cost"; it is cost theatre. kMaxGrains stays at 64 and the miss is
//          reported, which is what FR-022 asks for when it calls the constant
//          "provisional AND MEASUREMENT-BACKED".
//
//     (6)  ESCALATE - TAKEN, AND RESOLVED 2026-07-28 (see the verdict at the top
//          of this block). This is the honest verdict, and the reason is
//          arithmetic rather than defeatist. After (i)-(iv) the cost centres measure,
//          per 512-sample block at 48 kHz:
//              grain layer at 16 concurrent grains  ~ 74,000 ns
//              blur stage (STFT <-> OverlapAdd)     ~ 23,000 ns
//              freeze leg (two oscillators)         ~ 35,000 ns
//          The gate on a MEASUREMENT was 71,111 ns at the time this was written
//          (see the note in the BASELINE PROVENANCE block). Configuration (d) is
//          required to run all three at
//          once - FR-052 has no symmetric bypass at m = 1, deliberately - so it
//          was asked to fit ~132,000 ns of measured work into 71,111. No ordering
//          of the remaining levers reaches that: the blur and freeze stages
//          TOGETHER, with the grain layer at zero, are already ~58,000 ns, and
//          the grain layer alone at the FR-009 default is over the gate on its
//          own. The component composition this phase specifies - a 64-slot
//          granulator plus a stereo STFT decoherence stage plus two
//          SpectralFreezeOscillators, the last of which is documented at
//          "< 0.5 % CPU ... 2048 FFT" PER INSTANCE
//          (processors/spectral_freeze_oscillator.h:21) - cannot fit 1 % of a
//          core, let alone the 0.667 % that criterion's structure actually gated
//          on. The roadmap's own RA-4 note already records that its per-phase
//          budgets sum to 45 % against a 25 % Phase 7 ceiling. That is a budget
//          decision, not a code decision, and it was escalated with all five
//          figures in specs/seraphis-phase5-atmosphere/compliance.md - where the
//          user took it on 2026-07-28. The three options offered there were
//          (1) raise the allowance to ~1.5 % and re-derive Phase 7's tally from
//          the five real figures; (2) drop or share the pure-freeze leg; (3)
//          accept (c) as out-of-region and shrink FR-073's rule. OPTION 1 WAS
//          TAKEN, together with (c) as out-of-region but WITHOUT shrinking
//          FR-073's operating rule and WITHOUT touching kMaxGrains.
//
//   IF A LATER PASS SPENDS (3), (3b) OR (5): every one of them CHANGES THE
//   RENDER, so it re-opens SC-002, SC-003, SC-005, SC-010 and SC-011 - not just
//   this TU. Re-run dsp_systems_tests in full, plus the [.perf] cases by name,
//   and record the decision by REPLACING this verdict. Do not append a second
//   one.
// ------------------------------------------------------------------------------

#include <krate/dsp/systems/atmosphere_engine.h>

#include <catch2/catch_all.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <sstream>
#include <string>

using namespace Krate::DSP;

namespace {

// =============================================================================
// Measurement basis (SC-004, plan S15.4)
// =============================================================================

constexpr double kSr48 = 48000.0;
constexpr std::size_t kBlockSize = 512;

/// Wall-clock budget of one 512-sample block at 48 kHz, in nanoseconds.
constexpr double kBlockBudgetNs = (static_cast<double>(kBlockSize) / kSr48) * 1.0e9;

/// SC-004's per-voice reference, 1.5 % of one core = 160,000 ns/block exactly.
///
/// AMENDED 2026-07-28 by the user's lever-6 budget decision (option 1). The
/// withdrawn figure was the roadmap's 1 % = 106,667 ns/block; it was raised to
/// 1.5 % after the five measured worst-case figures in the BASELINE PROVENANCE
/// block showed the composition this phase specifies cannot fit 1 %. The
/// derivation is those five numbers and nothing else - see spec.md SC-004's
/// dated amendment blockquote and compliance.md section 1.
/// Written as the literal the decision names, and TIED to its derivation by the
/// clause below rather than computed from it: kBlockBudgetNs * 0.015 is
/// 159,999.999999999971 in binary, and a compile-time gate must not inherit that
/// residue.
constexpr double kReferenceNs = 160000.0;
static_assert(kReferenceNs >= kBlockBudgetNs * 0.0149 && kReferenceNs <= kBlockBudgetNs * 0.0151,
              "SC-004's amended reference is 1.5 % of one 512-sample block at 48 kHz");

/// THE ARITHMETIC CEILING, and the reason this case exists.
///
/// AMENDED 2026-07-28: derived from the raised reference, so it is now
/// 160,000 / (64 x 512) = 4.883 ns per grain-sample. Against the WITHDRAWN 1 %
/// reference it was 3.255 ns. Both are reported by the case; neither gates -
/// FR-022's measurement mandate is discharged by the MEASUREMENT RECORD above.
///
/// 106,667 ns spread over kMaxGrains x 512 grain-samples is 3.255 ns EACH. That
/// figure is NOT a cost model - it is the number a cost model has to come in
/// under. At saturation the engine runs 64 grains x two decorrelated read points
/// x two channels, i.e. up to ~128 independent NON-SEQUENTIAL streams into a
/// ring of up to 16.8 MB (RA-2's memory table: 30 s stereo at 48 kHz). No cache
/// on a consumer part holds that; a single L3 or DRAM miss is tens of
/// nanoseconds and therefore exceeds the WHOLE per-grain-sample allowance on its
/// own. Whether the real figure lands under 3.255 ns is a memory-system
/// question, not an instruction-count question, so it is measured rather than
/// argued - and the measurement is what decides O-3.
constexpr double kArithmeticCeilingNsPerGrainSample =
    kReferenceNs / static_cast<double>(AtmosphereEngine::kMaxGrains * kBlockSize);

// --- Structural clauses: what these numbers describe -------------------------
// If any of these moves, the measurement no longer describes what the task
// specified and the TU stops compiling rather than silently reporting a figure
// for a different configuration.

static_assert(kBlockSize % AtmosphereEngine::kControlChunkSamples == 0,
              "the measured block must be a whole number of control chunks");
static_assert(kBlockSize / AtmosphereEngine::kControlChunkSamples == 8,
              "SC-004 measures 8 control chunks per 512-sample block");
static_assert(AtmosphereEngine::kMaxGrains == 64,
              "the saturated pool is measured at 64 grains; if kMaxGrains moved under "
              "SC-004 lever (5), this number, the arithmetic ceiling above and FR-073's "
              "documented operating region move with it");

// =============================================================================
// SC-004: the five checked-in baselines (tasks.md T018)
// =============================================================================
// The three structural clauses immediately above are T018's as well as T017's -
// they are stated once rather than duplicated. kBlockSize / kControlChunkSamples
// == 8 says every measured block is exactly 8 control steps, and kMaxGrains == 64
// says configuration (c) really is measuring the saturated pool.

/// Relative regression bound applied to every checked-in baseline.
constexpr double kRegressionFactor = 1.5;

/// The largest baseline the HEADROOM clause can accept, ~106,666.67 ns/block
/// (160,000 / 1.5). Was ~71,111 before the 2026-07-28 amendment.
///
/// A measurement above it means the phase is OVER BUDGET, and the response is
/// SC-004's numbered lever list - (1) verify FR-052's freeze hard-bypass
/// engages; (2) verify FR-005's control-step decimation is firing; (3) drop
/// blurFftSize to 512; (4) hoist the equal-power pan cos/sin back to birth time;
/// (5) reduce kMaxGrains and shrink FR-073's operating region to match; (6) only
/// then escalate. NEVER a larger baseline, and never a smaller
/// kRegressionFactor renegotiated at implementation time. Lever (6) HAS now been
/// taken, once, by the user; that is what raised the reference, and it does not
/// license taking it again.
constexpr double kMaxAdmissibleBaselineNs = kReferenceNs / kRegressionFactor;

/// The largest WHOLE nanosecond at or below kMaxAdmissibleBaselineNs.
///
/// Written as a literal rather than as the quotient because
/// `(reference / 1.5) * 1.5 <= reference` is a floating-point round-trip, and a
/// compile-time gate must not depend on one. 106,666 x 1.5 = 159,999 exactly, in
/// binary as in decimal.
constexpr double kCappedBaselineNs = 106666.0;
static_assert(kCappedBaselineNs <= kMaxAdmissibleBaselineNs,
              "the capped baseline must still satisfy the headroom clause");

/// The smallest baseline the FLOOR clause can accept, ~2,133 ns/block.
///
/// THE FLOOR IS NOT DECORATIVE, and it is not a second spelling of the headroom
/// clause the way Phase 4's pair was (continuous_body_perf_test.cpp:242-269). It
/// catches the opposite failure: a baseline accidentally recorded from a no-op or
/// misconfigured run - a latched engine, an unprepared engine, a configuration
/// that never births a grain - satisfies the headroom clause TRIVIALLY (it is
/// tiny), compiles, and then makes the runtime REQUIRE fail spuriously on any
/// machine slower than the one that recorded it. When this clause fires, the
/// MEASUREMENT is wrong, not the threshold.
constexpr double kMinAdmissibleBaselineNs = kReferenceNs / 50.0;

// --- The five checked-in baselines, PINNED FROM THE MEASUREMENTS -------------
//
// AMENDED 2026-07-28. Before that date all five were the placeholder
// kProvisionalBaselineNs = 71,000, because every measured figure was above the
// then-admissible ceiling of 71,111 and SC-004 forbids raising a baseline. The
// user's lever-6 budget decision raised the REFERENCE to 1.5 % (option 1), which
// is the only thing that can license a larger baseline, and each constant below
// is now a MEASUREMENT again:
//
//     baseline = min( ceil(worst-of-eight x 1.05), kCappedBaselineNs )
//
// rounded up to the next whole 1,000 ns where the cap does not bind. The 1.05 is
// run-to-run spread on the recording machine, not allowance; the cap is where
// `baseline x 1.5 <= reference` binds instead.
//
//     cfg   worst-of-eight   x 1.05     capped?   baseline    runtime gate
//     (a)      86,305        90,620.25    no       91,000      136,500
//     (b)     111,815       117,405.75    YES     106,666      159,999
//     (c)     343,805       360,995.25   n/a      361,000      541,500   <- out-of-region
//     (d)     153,651       161,333.55    YES     106,666      159,999
//     (e)     111,008       116,558.40    YES     106,666      159,999
//
// CONFIGURATION (c) IS OUT-OF-REGION, by the same 2026-07-28 decision: a
// saturated 64-grain pool at the most expensive blur geometry is outside FR-073's
// documented operating rule (density x grainSeconds <= kMaxGrains), so it is
// still MEASURED and still REGRESSION-TRACKED against its own checked-in
// baseline, but it carries NO headroom clause and is not gated against the
// reference. That exemption is (c)'s alone: (a), (b), (d) and (e) keep the full
// two-clause structure against 160,000 ns.

/// (a) Defaults, blur off, freeze off - the roadmap's "default density"
/// (line 248): FR-009's density 4 grains/s x grainSeconds 4 s = 16 concurrent
/// (OQ-1). The cheapest configuration and the one the roadmap's headline claim
/// is actually about. Worst of eight: 86,305 ns (0.809 % of one core).
constexpr double kBaselineDefaultsNs = 91000.0;

/// (b) Defaults, blur on at the PrepareConfig default geometry (blurFftSize
/// 1024, 75 % overlap -> hop 256 -> 2 frame-pairs per 512-block), blur knob at
/// 1.0. FR-041 routes the grain bus through the STFT stage unconditionally, so
/// (b) - (a) is the honest cost of the blur stage plus full phase randomisation.
/// Worst of eight: 111,815 ns (1.048 % of one core); x 1.05 exceeds the cap, so
/// the cap binds and the regression headroom here is 43 % rather than 5 %.
constexpr double kBaselineBlurNs = kCappedBaselineNs;

/// (c) Pool SATURATED at kMaxGrains = 64, blur on, blurFftSize = 256 - the most
/// expensive blur geometry the control table allows (hop 64 -> 8 frame-pairs per
/// 512-block, one per control chunk). Worst of eight: 343,805 ns (3.223 % of one
/// core). OUT-OF-REGION per the 2026-07-28 decision: reported and regression-
/// tracked, NOT gated against the reference. It has no headroom clause below.
constexpr double kBaselineSaturatedBlurNs = 361000.0;

/// (d) freezeMix = 1.0 WITH THE GRAIN LAYER STILL RUNNING. FR-052 has no
/// symmetric bypass at m = 1 (atmosphere_engine.h:2002-2012 states why), so this
/// is grain + blur + freeze-drone, not a freeze-only figure - and it is the
/// number Phase 7's full-poly tally inherits for a frozen voice. Worst of eight:
/// 153,651 ns (1.440 % of one core), the largest in-region figure and therefore
/// the one that sets Phase 5's real per-voice cost.
constexpr double kBaselineFrozenNs = kCappedBaselineNs;

/// (e) Configuration (b) plus setGrainEnvelope() called once per block with an
/// ALTERNATING type, which is the one call pattern the idempotence guard
/// (atmosphere_engine.h:919-921) cannot elide: the full kEnvelopeTableSize =
/// 4096-entry regeneration runs on every single block. Worst of eight:
/// 111,008 ns (1.041 % of one core).
constexpr double kBaselineEnvelopeChurnNs = kCappedBaselineNs;

// --- The SC-004 compile-time clauses -----------------------------------------
// Evaluated by EVERY CI leg regardless of the "[.perf]" tag, which is exactly
// why the absolute gate is placed here rather than in the case body. Two clauses
// each for (a), (b), (d), (e); the FLOOR clause only for (c), which the
// 2026-07-28 decision exempts from the reference.

static_assert(kBaselineDefaultsNs * kRegressionFactor <= kReferenceNs,
              "SC-004 (a): baseline must be no weaker than the 1.5 % reference");
static_assert(kBaselineDefaultsNs >= kMinAdmissibleBaselineNs,
              "SC-004 (a): a baseline below reference/50 was recorded from a no-op or "
              "misconfigured run - the measurement, not the threshold, is wrong");

static_assert(kBaselineBlurNs * kRegressionFactor <= kReferenceNs,
              "SC-004 (b): baseline must be no weaker than the 1.5 % reference");
static_assert(kBaselineBlurNs >= kMinAdmissibleBaselineNs,
              "SC-004 (b): a baseline below reference/50 was recorded from a no-op or "
              "misconfigured run - the measurement, not the threshold, is wrong");

// (c) has NO headroom clause, deliberately: the 2026-07-28 budget decision puts
// the saturated 64-grain configuration OUT OF REGION. Its floor clause stays,
// because "out of region" excuses the ceiling, not a bogus measurement.
static_assert(kBaselineSaturatedBlurNs >= kMinAdmissibleBaselineNs,
              "SC-004 (c): a baseline below reference/50 was recorded from a no-op or "
              "misconfigured run - the measurement, not the threshold, is wrong");
static_assert(kBaselineSaturatedBlurNs > kMaxAdmissibleBaselineNs,
              "SC-004 (c): if the saturated configuration ever comes back UNDER the "
              "admissible ceiling it is no longer out-of-region - restore its headroom "
              "clause and delete this one");

static_assert(kBaselineFrozenNs * kRegressionFactor <= kReferenceNs,
              "SC-004 (d): baseline must be no weaker than the 1.5 % reference");
static_assert(kBaselineFrozenNs >= kMinAdmissibleBaselineNs,
              "SC-004 (d): a baseline below reference/50 was recorded from a no-op or "
              "misconfigured run - the measurement, not the threshold, is wrong");

static_assert(kBaselineEnvelopeChurnNs * kRegressionFactor <= kReferenceNs,
              "SC-004 (e): baseline must be no weaker than the 1.5 % reference");
static_assert(kBaselineEnvelopeChurnNs >= kMinAdmissibleBaselineNs,
              "SC-004 (e): a baseline below reference/50 was recorded from a no-op or "
              "misconfigured run - the measurement, not the threshold, is wrong");

/// The blur geometry configuration (c) is pinned to, asserted rather than
/// assumed: 256 is the control table's floor, and at 75 % overlap that is a
/// 64-sample hop, i.e. one frame-pair per control chunk and eight per measured
/// block. If kMinBlurFftSize moves, (c) is no longer the most expensive blur
/// geometry and its baseline describes something else.
constexpr std::size_t kSaturatedBlurFftSize = AtmosphereEngine::kMinBlurFftSize;
static_assert(kSaturatedBlurFftSize == 256,
              "SC-004 (c) measures the most expensive blur geometry, which is the control "
              "table's smallest FFT");
static_assert(kBlockSize / (kSaturatedBlurFftSize / 4) == 8,
              "SC-004 (c) measures 8 blur frame-pairs per 512-sample block");

/// The blur geometry configurations (b), (d) and (e) run at, READ FROM the
/// PrepareConfig default rather than hard-coded, so a moved default shows up as a
/// changed measurement rather than as a stale assertion. RA-3 pins this to 1024:
/// 21.3 ms of fixed layer latency at 48 kHz, and 2 frame-pairs per 512-block.
constexpr std::size_t kDefaultBlurFftSize = AtmosphereEngine::PrepareConfig{}.blurFftSize;
static_assert(kDefaultBlurFftSize == 1024,
              "SC-004 (b)/(d)/(e) measure RA-3's 1024-point blur FFT");
static_assert(kBlockSize / (kDefaultBlurFftSize / 4) == 2,
              "SC-004 (b)/(d)/(e) measure 2 blur frame-pairs per 512-sample block");

// =============================================================================
// Trial shape
// =============================================================================
// Best-of-N: the minimum is the least OS-noise-contaminated estimate of the real
// cost, which is what a regression bound wants.
//
// The shape is MANY SHORT trials (25 x 500 blocks), copied deliberately from
// harmonic_cloud_perf_test.cpp:191-193 and continuous_body_perf_test.cpp:311-313,
// and the reason is the dev machine's CPU: a 13th Gen Intel Core i9 is a HYBRID
// part. The dominant noise source is not scheduling jitter smeared across a
// trial, it is the whole trial being migrated onto an E-core, which is a ~20 %
// step in ns/block that best-of-N cannot reject when N is small and each trial
// is long enough (2000 blocks ~= 60 ms) to be migrated. Shortening each trial to
// 500 blocks (~15 ms) and taking 25 of them makes it very likely that at least
// one trial runs start-to-finish on a boosted P-core.

constexpr int kTrials = 25;
constexpr int kWarmupBlocks = 400;
constexpr int kBlocksPerTrial = 500;

/// Finite check WITHOUT std::isnan: the macOS leg builds with -ffast-math, which
/// folds std::isnan to false. Inspect the IEEE-754 exponent field instead.
[[nodiscard]] bool isFiniteValue(float v) noexcept
{
    std::uint32_t bits = 0;
    std::memcpy(&bits, &v, sizeof(bits));
    return (bits & 0x7F800000u) != 0x7F800000u;
}

// =============================================================================
// Buffers and excitation
// =============================================================================

/// One block of input plus its output scratch. IN-PLACE IS NOT SUPPORTED
/// (atmosphere_engine.h:153-154), so the output arrays are separate.
struct Buffers {
    std::array<float, kBlockSize> inLeft{};
    std::array<float, kBlockSize> inRight{};
    std::array<float, kBlockSize> outLeft{};
    std::array<float, kBlockSize> outRight{};
};

/// Deterministic decorrelated stereo noise, peak 0.25 (~-17 dBFS RMS), built
/// once and replayed every block.
///
/// Sustained broadband excitation, not an impulse: the ring must hold real audio
/// everywhere a grain can read, otherwise large stretches of it are zero pages
/// that the memory system serves far more cheaply than the real thing - which is
/// precisely the cost this case is trying to measure. Xorshift32 rather than
/// <random> so the sequence is identical on every toolchain
/// (std::uniform_real_distribution is not portable).
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
// The T017 configuration
// =============================================================================

/// The grain-layer worst case (tasks.md T017): pool SATURATED, decorrelation and
/// positionSpread at their maxima, so the read points are as scattered as the
/// component allows.
///
/// BLUR AND FREEZE ARE OFF, and that is what makes the figure a GRAIN-SAMPLE
/// cost rather than a block cost wearing a per-grain label: the blur STFT and
/// the freeze oscillator cost the same whether one grain is live or 64, so
/// dividing their cost by (activeCount x 512) would attribute a constant to a
/// variable and quietly understate the grain term as the population grows. Their
/// cost is measured, in full, by SC-004 configurations (b)-(e) in
/// AtmosphereEngine_CpuBudget (tasks.md T018).
///
/// The knobs, and why each is where it is:
///  - density  = kMaxDensity (20/s) and grainSeconds = kMaxGrainSeconds (30 s):
///    600 concurrent grains requested against a 64-slot pool, so the pool is
///    saturated for the entire measurement and FR-023's skip path is live.
///    30 s grains also make the population STABLE at 64 - one slot frees every
///    ~L'/64 seconds and is refilled within ~50 ms - so the divisor below is not
///    a moving target.
///  - decorrelation = 1.0: the right channel reads at ageL + up to 30 ms, i.e. a
///    SECOND independent stream per grain (atmosphere_engine.h:1512-1517). This
///    is the knob that doubles the stream count to ~128.
///  - positionSpread = 1.0 with positionSeconds mid-range: birth ages are drawn
///    over [0, 2 x positionSeconds] before the FR-025 window clamp
///    (atmosphere_engine.h:1563-1566), which spreads births across the whole
///    legal window at both capacities.
///  - every remaining control is pinned to its FR-009 default EXPLICITLY rather
///    than left implicit, so the configuration is reproducible from this file
///    alone if a default ever moves.
void configureSaturatedGrainLayer(AtmosphereEngine& engine, float captureSeconds) noexcept
{
    AtmosphereEngine::PrepareConfig cfg{};
    cfg.captureSeconds = captureSeconds;
    cfg.blurEnabled = false;
    cfg.freezeEnabled = false;
    cfg.maxBlockSamples = kBlockSize;
    engine.prepare(kSr48, cfg);

    engine.setSeed(20260728u);

    engine.setDensity(AtmosphereEngine::kMaxDensity);
    engine.setGrainSeconds(AtmosphereEngine::kMaxGrainSeconds);
    engine.setDecorrelation(1.0f);
    engine.setPositionSpread(1.0f);
    engine.setPositionSeconds(AtmosphereEngine::kMaxPositionSeconds * 0.5f);

    // FR-009 defaults, stated explicitly.
    engine.setJitter(0.5f);
    engine.setPitchSemitones(0.0f);
    engine.setPitchSpread(0.15f);
    engine.setDriftDepth(0.3f);
    engine.setDriftSmoothness(0.7f);
    engine.setDriftRangeSemitones(2.0f);
    engine.setPanSpread(0.7f);
    engine.setLevel(1.0f);
}

// =============================================================================
// Measurement
// =============================================================================

struct GrainCostMeasurement {
    double nsPerBlock = 0.0;
    double meanActiveGrains = 0.0;
    double nsPerGrainSample = 0.0;
    std::size_t minActiveGrains = 0;
    std::size_t maxActiveGrains = 0;
    std::uint64_t poolFullSkips = 0;
    std::size_t capacitySamples = 0;
    double sink = 0.0;
};

/// Best-of-N driver that also records the POPULATION the winning trial ran at.
///
/// The divisor of a per-grain-sample figure has to come from the same trial as
/// the numerator. Assuming a full 64 would overstate the population - and
/// therefore UNDERSTATE the per-grain-sample cost - by however much the pool
/// dipped between a retirement and the next trigger. One accessor read plus an
/// add per 512-sample block sits inside the timed region; at ~3 ns against a
/// block cost in the tens of microseconds that is below 1e-4 of the figure.
///
/// `runBlock` is taken by const reference, not by forwarding reference: it is
/// INVOKED, many times, never consumed, so there is nothing to forward.
template <typename BlockFn>
[[nodiscard]] GrainCostMeasurement bestTrial(const AtmosphereEngine& engine, int trials,
                                             int blocksPerTrial, const BlockFn& runBlock)
{
    GrainCostMeasurement out{};
    out.nsPerBlock = std::numeric_limits<double>::max();
    out.minActiveGrains = AtmosphereEngine::kMaxGrains;

    for (int trial = 0; trial < trials; ++trial) {
        std::uint64_t activeAccum = 0;

        const auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < blocksPerTrial; ++i) {
            runBlock();
            activeAccum += engine.getActiveGrainCount();
        }
        const auto end = std::chrono::steady_clock::now();

        const double elapsedNs = std::chrono::duration<double, std::nano>(end - start).count();
        const double nsPerBlock = elapsedNs / static_cast<double>(blocksPerTrial);

        // Population bounds, sampled at every trial BOUNDARY (25 samples spread
        // over the whole measurement) rather than per block: they exist to show
        // the pool stayed saturated for the whole measurement, which the winning
        // trial's mean alone cannot. Sampling them per block would put a branch
        // inside the timed region for no additional evidence.
        const std::size_t active = engine.getActiveGrainCount();
        out.minActiveGrains = std::min(out.minActiveGrains, active);
        out.maxActiveGrains = std::max(out.maxActiveGrains, active);

        if (nsPerBlock < out.nsPerBlock) {
            out.nsPerBlock = nsPerBlock;
            out.meanActiveGrains =
                static_cast<double>(activeAccum) / static_cast<double>(blocksPerTrial);
        }
    }
    return out;
}

/// One full measurement at a given ring size.
///
/// CONDITIONING, which is an ADDITION to the pinned trial shape and not a change
/// to it. The 400 warm-up blocks are ~4.3 s of audio; the ring alone is 43.7 s
/// at captureSeconds = 30, and a grain lives up to 30 s on top of that. Starting
/// the trials after only the pinned warm-up would time an engine whose ring is
/// still 90 % untouched pages and whose live grains were all born under FR-014's
/// cold-ring rejection - i.e. clustered at small read ages, which is the
/// CHEAPEST population, not the worst case this figure is supposed to describe.
/// Worse, best-of-25 would then systematically select the earliest, least-mixed,
/// most cache-resident trial. So the engine is first driven until (a) the whole
/// ring has been written at least once and (b) a full maximum grain lifetime has
/// elapsed since, which guarantees every live grain was born against a FULL
/// ring. Only then does the pinned 400-block warm-up + best-of-25 x 500 run.
[[nodiscard]] GrainCostMeasurement measureGrainSampleCost(float captureSeconds)
{
    AtmosphereEngine engine;
    configureSaturatedGrainLayer(engine, captureSeconds);

    Buffers buf;
    fillExcitation(buf);
    double sink = 0.0;

    // Reading two samples per block is what stops the optimizer dead-coding the
    // render away; a real consumer reads the whole buffer, so this is not
    // artificial overhead.
    const auto renderBlock = [&]() noexcept {
        engine.processStereoBlock(buf.inLeft.data(), buf.inRight.data(), buf.outLeft.data(),
                                  buf.outRight.data(), kBlockSize);
        sink += static_cast<double>(buf.outLeft[0])
                + static_cast<double>(buf.outRight[kBlockSize - 1]);
    };

    const std::size_t capacity = engine.getCaptureCapacitySamples();
    const std::size_t ringBlocks = (capacity + kBlockSize - 1) / kBlockSize;
    const std::size_t lifetimeBlocks =
        static_cast<std::size_t>((static_cast<double>(AtmosphereEngine::kMaxGrainSeconds) * kSr48)
                                 / static_cast<double>(kBlockSize))
        + 1;
    for (std::size_t i = 0; i < ringBlocks + lifetimeBlocks; ++i) {
        renderBlock();
    }

    for (int i = 0; i < kWarmupBlocks; ++i) {
        renderBlock();
    }

    GrainCostMeasurement out = bestTrial(engine, kTrials, kBlocksPerTrial, renderBlock);
    out.sink = sink;
    out.poolFullSkips = engine.getSkippedTriggerCountPoolFull();
    out.capacitySamples = capacity;
    // Guarded so a configuration that somehow never births a grain reports 0
    // rather than an infinity - the population REQUIREs in the case body are
    // what diagnose that, and they cannot do so if the report already blew up.
    const double grainSamples = out.meanActiveGrains * static_cast<double>(kBlockSize);
    out.nsPerGrainSample = (grainSamples > 0.0) ? (out.nsPerBlock / grainSamples) : 0.0;
    return out;
}

[[nodiscard]] std::string reportGrainCost(const char* label, float captureSeconds,
                                          const GrainCostMeasurement& m)
{
    const double ringMiB =
        (static_cast<double>(m.capacitySamples) * 2.0 * 4.0) / (1024.0 * 1024.0);
    std::ostringstream os;
    os << "FR-022 grain-sample cost (" << label << "), AtmosphereEngine, grain layer only:\n"
       << "  captureSeconds  : " << captureSeconds << " s  -> ring " << m.capacitySamples
       << " samples (" << ringMiB << " MiB stereo float)\n"
       << "  measured        : " << m.nsPerBlock << " ns/block (512 samples @ 48 kHz)\n"
       << "  mean population : " << m.meanActiveGrains
       << " grains (winning trial, per block); at trial boundaries "
       << m.minActiveGrains << " .. " << m.maxActiveGrains << "\n"
       << "  pool-full skips : " << m.poolFullSkips << "  (FR-023's path; must be > 0)\n"
       << "  PER GRAIN-SAMPLE: " << m.nsPerGrainSample << " ns\n"
       << "  ceiling         : " << kArithmeticCeilingNsPerGrainSample << " ns  (" << kReferenceNs
       << " ns / (64 x 512))\n"
       << "  fraction of it  : " << (m.nsPerGrainSample / kArithmeticCeilingNsPerGrainSample)
       << "  (>= 1 means the saturated pool cannot fit the 1.5 % budget at kMaxGrains = "
       << AtmosphereEngine::kMaxGrains
       << "; it does not, which is why the saturated configuration (c) is OUT-OF-REGION "
          "per the 2026-07-28 decision rather than a reason to shrink the pool)\n"
       << "  % of one core   : " << ((m.nsPerBlock / kBlockBudgetNs) * 100.0) << " %";
    return os.str();
}

// =============================================================================
// The five SC-004 configurations (T018)
// =============================================================================

/// A no-op hook. Used for the four configurations that have nothing to do
/// between blocks, or nothing to do after conditioning.
struct NoOp {
    void operator()() const noexcept {}
};

/// Everything a gate or a precondition needs, kept together so no measurement
/// can be reported without the evidence that it measured what it claims.
///
/// This WRAPS GrainCostMeasurement rather than extending it: that struct and its
/// bestTrial() driver belong to T017's case, and adding fields to them for T018's
/// benefit would make one case's data model depend on the other's.
struct BudgetMeasurement {
    GrainCostMeasurement m{};
    float appliedBlur = 0.0f;        ///< the SMOOTHED value the blur stage actually read
    bool freezeCaptured = false;     ///< the drone holds a real spectrum, not zeros
    std::size_t latencySamples = 0;  ///< blurFftSize_ when blur is on, else 0
};

/// FR-009's control-table defaults, stated EXPLICITLY rather than left implicit.
///
/// Every one of these is the documented default in atmosphere_engine.h:735-925,
/// so this call is a no-op against a freshly prepared engine - which is the
/// point: the five configurations stay reproducible from this file alone if a
/// default ever moves, and a moved default shows up as a changed measurement
/// rather than silently redefining "defaults".
///
/// The seed is the one T017 uses, so the two cases exercise the same RNG
/// trajectory and their figures are comparable.
void applyDefaultControls(AtmosphereEngine& engine) noexcept
{
    engine.setSeed(20260728u);

    engine.setGrainSeconds(4.0f);   // FR-009 default; with density 4/s -> 16 concurrent
    engine.setDensity(4.0f);        // FR-009 default
    engine.setJitter(0.5f);
    engine.setPositionSeconds(1.0f);
    engine.setPositionSpread(0.3f);
    engine.setPitchSemitones(0.0f);
    engine.setPitchSpread(0.15f);
    engine.setDriftDepth(0.3f);
    engine.setDriftSmoothness(0.7f);
    engine.setDriftRangeSemitones(2.0f);
    engine.setPanSpread(0.7f);
    engine.setDecorrelation(0.5f);
    engine.setBlur(0.0f);
    engine.setFreezeMix(0.0f);
    engine.setLevel(1.0f);
}

/// The knobs configuration (c) overrides to SATURATE the 64-slot pool.
///
/// Identical to T017's configureSaturatedGrainLayer() knob set above,
/// deliberately: (c) and the grain-sample micro-benchmark must be
/// measuring the same grain population, otherwise T017's ns/grain-sample figure
/// cannot be used to reason about (c)'s ns/block figure.
///
///  - density kMaxDensity (20/s) x grainSeconds kMaxGrainSeconds (30 s) requests
///    600 concurrent grains against a 64-slot pool, so the pool is saturated for
///    the whole measurement and FR-023's skip-never-steal path is live. The
///    30 s lifetime also makes the population STABLE at 64 rather than a moving
///    target.
///  - decorrelation 1.0: the right channel reads at ageL + up to 30 ms, a second
///    independent ring stream per grain (atmosphere_engine.h:1511-1517).
///  - positionSpread 1.0 with positionSeconds mid-range spreads birth ages over
///    the whole legal window (:1563-1566).
void applySaturatingControls(AtmosphereEngine& engine) noexcept
{
    engine.setDensity(AtmosphereEngine::kMaxDensity);
    engine.setGrainSeconds(AtmosphereEngine::kMaxGrainSeconds);
    engine.setDecorrelation(1.0f);
    engine.setPositionSpread(1.0f);
    engine.setPositionSeconds(AtmosphereEngine::kMaxPositionSeconds * 0.5f);
}

/// Conditioning + the pinned warm-up + the pinned best-of-25 x 500 trials.
///
/// CONDITIONING IS AN ADDITION TO THE PINNED TRIAL SHAPE, NOT A CHANGE TO IT
/// (the same argument T017's measureGrainSampleCost() above makes).
/// The 400 warm-up blocks are ~4.3 s of audio; the ring alone is 8 s at the
/// default capture length and a grain lives up to 30 s on top of that. Starting
/// the trials after only the pinned warm-up would time an engine whose ring is
/// still mostly untouched pages and whose live grains were all born under
/// FR-014's cold-ring rejection - i.e. clustered at small read ages, the
/// CHEAPEST population rather than the steady state the baseline describes.
/// Worse, best-of-25 would then systematically select the earliest, least-mixed,
/// most cache-resident trial. So the engine is first driven until the whole ring
/// has been written at least once AND a full grain lifetime has elapsed since,
/// which guarantees every live grain was born against a full ring.
///
/// @param beforeBlock Invoked INSIDE the timed region, immediately before each
///        render. Configuration (e)'s per-block setGrainEnvelope() lives here;
///        every other configuration passes NoOp.
/// @param afterConditioning Invoked once, after conditioning and BEFORE the
///        warm-up, so anything it arms has settled by the time the trials start.
///        Configuration (d)'s captureFreeze() + setFreezeMix(1.0) live here: the
///        capture is a no-op until the ring holds a whole freeze window, and the
///        FR-052 ramp is 100 ms (~10 blocks) against a 400-block warm-up.
template <typename BeforeBlockFn, typename AfterConditioningFn>
[[nodiscard]] BudgetMeasurement runBudgetTrials(AtmosphereEngine& engine,
                                                const BeforeBlockFn& beforeBlock,
                                                const AfterConditioningFn& afterConditioning)
{
    Buffers buf;
    fillExcitation(buf);
    double sink = 0.0;

    // Reading two samples per block is what stops the optimizer dead-coding the
    // render away; a real consumer reads the whole buffer, so this is not
    // artificial overhead.
    const auto renderBlock = [&]() noexcept {
        beforeBlock();
        engine.processStereoBlock(buf.inLeft.data(), buf.inRight.data(), buf.outLeft.data(),
                                  buf.outRight.data(), kBlockSize);
        sink += static_cast<double>(buf.outLeft[0])
                + static_cast<double>(buf.outRight[kBlockSize - 1]);
    };

    const std::size_t capacity = engine.getCaptureCapacitySamples();
    const std::size_t ringBlocks = (capacity + kBlockSize - 1) / kBlockSize;
    const std::size_t lifetimeBlocks =
        static_cast<std::size_t>((static_cast<double>(engine.getGrainSeconds()) * kSr48)
                                 / static_cast<double>(kBlockSize))
        + 1;
    for (std::size_t i = 0; i < ringBlocks + lifetimeBlocks; ++i) {
        renderBlock();
    }

    afterConditioning();

    for (int i = 0; i < kWarmupBlocks; ++i) {
        renderBlock();
    }

    BudgetMeasurement out{};
    out.m = bestTrial(engine, kTrials, kBlocksPerTrial, renderBlock);
    out.m.sink = sink;
    out.m.poolFullSkips = engine.getSkippedTriggerCountPoolFull();
    out.m.capacitySamples = capacity;
    out.appliedBlur = engine.getAppliedBlur();
    out.freezeCaptured = engine.isFreezeCaptured();
    out.latencySamples = engine.getLatencySamples();
    return out;
}

/// The PrepareConfig every configuration starts from.
///
/// maxBlockSamples is pinned to kBlockSize rather than left at its 2048 default:
/// it declares the host block size to the blur output FIFO, and declaring 2048
/// while rendering 512 would size the FIFO for a block that never arrives. Every
/// other field is the documented default unless a configuration names it.
[[nodiscard]] AtmosphereEngine::PrepareConfig defaultPrepareConfig() noexcept
{
    AtmosphereEngine::PrepareConfig cfg{};
    cfg.captureSeconds = 8.0f;  // FR-009 default; ~4.19 MB stereo ring
    cfg.maxBlockSamples = kBlockSize;
    return cfg;
}

/// (a) Defaults, blur off, freeze off - 16 concurrent grains and nothing else.
[[nodiscard]] BudgetMeasurement measureDefaultsNoBlurNoFreeze()
{
    AtmosphereEngine engine;
    AtmosphereEngine::PrepareConfig cfg = defaultPrepareConfig();
    cfg.blurEnabled = false;
    cfg.freezeEnabled = false;
    engine.prepare(kSr48, cfg);
    applyDefaultControls(engine);
    return runBudgetTrials(engine, NoOp{}, NoOp{});
}

/// (b) Defaults, blur on.
///
/// blurFftSize stays at the PrepareConfig default 1024 (RA-3's 21.3 ms fixed
/// layer latency), and the blur KNOB is driven to 1.0. Both halves matter:
/// FR-041 routes the grain bus through the STFT stage unconditionally so the
/// transform cost is paid at any knob value, but the per-bin phase perturbation
/// is only fully exercised at 1.0 - and "blur on" that measured blur = 0 would
/// be measuring FR-041's transparent path under a misleading label.
///
/// freezeEnabled stays at its default true with freezeMix at its default 0, so
/// the freeze leg sits in FR-052's settled-dry hard bypass. That is not free -
/// the bypass still advances the latency-matching delay with zeros, a pinned
/// decision (atmosphere_engine.h:2014-2018) - and leaving it in is what makes
/// this "defaults, blur on" rather than a hand-trimmed configuration.
[[nodiscard]] BudgetMeasurement measureDefaultsBlur()
{
    AtmosphereEngine engine;
    AtmosphereEngine::PrepareConfig cfg = defaultPrepareConfig();
    cfg.blurEnabled = true;
    engine.prepare(kSr48, cfg);
    applyDefaultControls(engine);
    engine.setBlur(1.0f);
    return runBudgetTrials(engine, NoOp{}, NoOp{});
}

/// (c) Pool saturated at 64 concurrent, blur on, blurFftSize = 256.
[[nodiscard]] BudgetMeasurement measureSaturatedBlur()
{
    AtmosphereEngine engine;
    AtmosphereEngine::PrepareConfig cfg = defaultPrepareConfig();
    cfg.blurEnabled = true;
    cfg.blurFftSize = kSaturatedBlurFftSize;
    engine.prepare(kSr48, cfg);
    applyDefaultControls(engine);
    applySaturatingControls(engine);
    engine.setBlur(1.0f);
    return runBudgetTrials(engine, NoOp{}, NoOp{});
}

/// (d) freezeMix = 1.0 with the grain layer still running.
///
/// Built on configuration (b), so (d) - (b) is attributable to the freeze leg
/// and nothing else. captureFreeze() is issued AFTER conditioning because it is
/// a documented no-op until the ring holds a whole freeze window
/// (atmosphere_engine.h:865-873) - and a never-captured oscillator early-outs to
/// zeros (spectral_freeze_oscillator.h:327-330), which would make this
/// configuration silently cheaper than the drone it claims to measure. The
/// freezeCaptured precondition in the case body is what proves it did not.
[[nodiscard]] BudgetMeasurement measureFrozenWithGrains()
{
    AtmosphereEngine engine;
    AtmosphereEngine::PrepareConfig cfg = defaultPrepareConfig();
    cfg.blurEnabled = true;
    engine.prepare(kSr48, cfg);
    applyDefaultControls(engine);
    engine.setBlur(1.0f);

    const auto armFreeze = [&engine]() noexcept {
        engine.captureFreeze();
        engine.setFreezeMix(1.0f);
    };
    return runBudgetTrials(engine, NoOp{}, armFreeze);
}

/// (e) Configuration (b) plus one setGrainEnvelope() per block, ALTERNATING.
///
/// Hann and Blackman are chosen because they differ as enumerators - which is
/// all the idempotence guard tests - and because both are pure closed-form
/// generators over the whole table, so the regeneration cost is the table cost
/// and not a shape-dependent one. Trapezoid, by contrast, is piecewise and
/// would make the figure depend on the attack/release ratios.
///
/// The toggle is captured by reference and mutated from a non-mutable lambda:
/// legal, and it keeps the call inside the timed region without giving
/// runBudgetTrials any state of its own.
[[nodiscard]] BudgetMeasurement measureEnvelopeChurn()
{
    AtmosphereEngine engine;
    AtmosphereEngine::PrepareConfig cfg = defaultPrepareConfig();
    cfg.blurEnabled = true;
    engine.prepare(kSr48, cfg);
    applyDefaultControls(engine);
    engine.setBlur(1.0f);

    std::size_t toggle = 0;
    const auto churnEnvelope = [&engine, &toggle]() noexcept {
        const bool odd = ((toggle++ & std::size_t{1}) != std::size_t{0});
        engine.setGrainEnvelope(odd ? GrainEnvelopeType::Blackman : GrainEnvelopeType::Hann);
    };
    return runBudgetTrials(engine, churnEnvelope, NoOp{});
}

[[nodiscard]] std::string reportBudget(const char* label, double baselineNs,
                                       const BudgetMeasurement& b)
{
    std::ostringstream os;
    os << "SC-004 (" << label << ") AtmosphereEngine:\n"
       << "  measured        : " << b.m.nsPerBlock << " ns/block (512 samples @ 48 kHz)\n"
       << "  block budget    : " << kBlockBudgetNs << " ns\n"
       << "  % of one core   : " << ((b.m.nsPerBlock / kBlockBudgetNs) * 100.0)
       << " %  (amended ceiling 2026-07-28: <= 1.5 % per voice)\n"
       << "  reference       : " << kReferenceNs << " ns/block  (1.5 % of one core)\n"
       << "  checked-in base : " << baselineNs << " ns/block  (gate: x" << kRegressionFactor
       << " = " << (baselineNs * kRegressionFactor) << " ns/block)\n"
       << "  population      : mean " << b.m.meanActiveGrains
       << " grains in the winning trial; at trial boundaries " << b.m.minActiveGrains << " .. "
       << b.m.maxActiveGrains << "\n"
       << "  pool-full skips : " << b.m.poolFullSkips << "  (FR-023's path)\n"
       << "  applied blur    : " << b.appliedBlur << ", layer latency " << b.latencySamples
       << " samples, freeze captured: " << (b.freezeCaptured ? "yes" : "no");
    return os.str();
}

}  // namespace

// =============================================================================
// FR-022 / O-3: what one grain-sample actually costs
// =============================================================================
// REPORTS. DOES NOT GATE - deliberately, and this is the whole point of the
// case. SC-004's gates are the five checked-in baselines in
// AtmosphereEngine_CpuBudget; a second cost gate here would either duplicate
// them or contradict them. What this case produces is the ONE number those
// baselines cannot express: cost per grain-sample, which is the only form in
// which "is kMaxGrains = 64 affordable?" is a well-posed question. FR-022
// declares 64 PROVISIONAL and measurement-backed precisely so that this
// measurement, and not a preference, decides whether tasks.md T019 step 5
// (reduce kMaxGrains) is spent.
//
// The two REQUIREs below are PRECONDITIONS on the measurement, not cost bounds:
// a figure divided by a population that never saturated, or measured on a render
// the optimizer removed, is not a measurement at all.
// =============================================================================

TEST_CASE("AtmosphereEngine_GrainSampleCost", "[.perf]")
{
    // Worst case first: the 30 s ring (RA-2: 16.8 MB per voice at 48 kHz), where
    // the ~128 read streams are scattered over the largest working set the
    // component can be configured with.
    const GrainCostMeasurement worst =
        measureGrainSampleCost(AtmosphereEngine::kMaxCaptureSeconds);

    // The 8 s DEFAULT, for contrast. Everything else is identical, so the
    // difference between the two figures is attributable to the working set
    // (4.19 MB vs 16.8 MB) and nothing else - which is exactly the question a
    // cache-miss-dominated cost model asks.
    const GrainCostMeasurement dflt = measureGrainSampleCost(8.0f);

    WARN(reportGrainCost("worst case - 30 s ring, decorrelation 1.0, positionSpread 1.0",
                         AtmosphereEngine::kMaxCaptureSeconds, worst));
    WARN(reportGrainCost("default - 8 s ring, same everything else", 8.0f, dflt));

    {
        std::ostringstream os;
        os << "FR-022 / O-3 verdict input - the number tasks.md T019 step 5 turns on:\n"
           << "  30 s ring : " << worst.nsPerGrainSample << " ns per grain-sample\n"
           << "   8 s ring : " << dflt.nsPerGrainSample << " ns per grain-sample\n"
           << "  ratio     : "
           << (dflt.nsPerGrainSample > 0.0 ? worst.nsPerGrainSample / dflt.nsPerGrainSample : 0.0)
           << "  (a ratio well above 1 is the memory system talking, not the ALU)\n"
           << "  ceiling   : " << kArithmeticCeilingNsPerGrainSample << " ns per grain-sample\n"
           << "  Paste both figures into this file's MEASUREMENT RECORD block verbatim.";
        WARN(os.str());
    }

    // --- Preconditions on the measurement, evaluated after both reports so that
    //     neither figure is hidden by an aborting REQUIRE. -------------------
    for (const GrainCostMeasurement* m : {&worst, &dflt}) {
        INFO("ring capacity " << m->capacitySamples << " samples");

        // Guards against the whole loop being optimized out (a zero-cost "pass")
        // and against a figure measured on an engine that had already blown up.
        REQUIRE(isFiniteValue(static_cast<float>(m->sink)));

        // The pool must have been saturated for the WHOLE measurement, otherwise
        // the divisor is a population the configuration never sustained. The
        // bound is 90 % of kMaxGrains rather than equality because a slot is
        // legitimately empty between a retirement and the next scheduler trigger
        // (~50 ms at density 20/s), and FR-023 forbids stealing to fill it.
        REQUIRE(m->poolFullSkips > 0u);
        REQUIRE(m->meanActiveGrains >= 0.9 * static_cast<double>(AtmosphereEngine::kMaxGrains));
        REQUIRE(m->minActiveGrains >= (AtmosphereEngine::kMaxGrains * 9u) / 10u);
        REQUIRE(m->nsPerGrainSample > 0.0);
    }
}

// =============================================================================
// SC-004: CPU <= 1.5 % of one core per voice (amended 2026-07-28; roadmap line
//         248 said 1 %, and that wording is withdrawn - see spec.md SC-004)
// =============================================================================
// Five configurations, five checked-in baselines, ONE reference for four of
// them. (a), (b), (d) and (e) are all gated against the same 160,000 ns, so the
// budget still cannot be met by choosing a flattering default: the frozen case
// (d) faces exactly the ceiling the 16-grain default (a) does. The saturated
// 64-grain case (c) is OUT-OF-REGION by the same decision - it is measured and
// gated against its OWN checked-in baseline x 1.5 as a regression bound, and
// against nothing else.
//
// The order below is deliberate and copies continuous_body_perf_test.cpp:874-880:
// every measurement and every report happens BEFORE any gate. A REQUIRE aborts
// the case, so gating each configuration where it is measured would let the first
// over-budget configuration hide the other four - which is exactly how the Phase
// 4 case once reported a single number while four configurations went unmeasured.
// =============================================================================

TEST_CASE("AtmosphereEngine_CpuBudget", "[.perf]")
{
    // --- 1. All five measurements ------------------------------------------
    const BudgetMeasurement cfgA = measureDefaultsNoBlurNoFreeze();
    const BudgetMeasurement cfgB = measureDefaultsBlur();
    const BudgetMeasurement cfgC = measureSaturatedBlur();
    const BudgetMeasurement cfgD = measureFrozenWithGrains();
    const BudgetMeasurement cfgE = measureEnvelopeChurn();

    // --- 2. All five reports, then the summary the compliance document needs -
    WARN(reportBudget("a - defaults, blur off, freeze off; 4 grains/s x 4 s = 16 concurrent",
                      kBaselineDefaultsNs, cfgA));
    WARN(reportBudget("b - defaults, blur on at blurFftSize 1024, blur knob 1.0",
                      kBaselineBlurNs, cfgB));
    WARN(reportBudget("c - pool saturated at 64, blur on, blurFftSize 256 (8 frames/block)",
                      kBaselineSaturatedBlurNs, cfgC));
    WARN(reportBudget("d - freezeMix 1.0 WITH the grain layer still running",
                      kBaselineFrozenNs, cfgD));
    WARN(reportBudget("e - configuration (b) + alternating setGrainEnvelope once per block",
                      kBaselineEnvelopeChurnNs, cfgE));

    const std::array<const char*, 5> configNames = {{
        "(a) defaults, blur off, freeze off ",
        "(b) defaults, blur on              ",
        "(c) saturated 64, blur fft 256     ",
        "(d) freezeMix 1.0 + grain layer    ",
        "(e) (b) + envelope churn per block ",
    }};
    const std::array<double, 5> measuredNs = {{
        cfgA.m.nsPerBlock, cfgB.m.nsPerBlock, cfgC.m.nsPerBlock,
        cfgD.m.nsPerBlock, cfgE.m.nsPerBlock,
    }};
    const std::array<double, 5> baselinesNs = {{
        kBaselineDefaultsNs, kBaselineBlurNs, kBaselineSaturatedBlurNs,
        kBaselineFrozenNs, kBaselineEnvelopeChurnNs,
    }};

    {
        std::ostringstream os;
        os << "SC-004 - THE FIVE FIGURES, ns per 512-sample block @ 48 kHz.\n"
           << "RA-4: the roadmap's per-phase budgets sum to 45 % against its own 25 % Phase 7\n"
           << "ceiling, so Phase 7 needs five REAL numbers from this phase, not a ceiling\n"
           << "nobody approached. Copy all five into the phase's compliance document and into\n"
           << "this file's BASELINE PROVENANCE table VERBATIM.\n";
        for (std::size_t i = 0; i < configNames.size(); ++i) {
            os << "  " << configNames[i] << " : " << measuredNs[i] << " ns/block  ("
               << ((measuredNs[i] / kBlockBudgetNs) * 100.0) << " % of one core; baseline "
               << baselinesNs[i] << ", gate " << (baselinesNs[i] * kRegressionFactor) << ")\n";
        }
        const double blurStageNs = cfgB.m.nsPerBlock - cfgA.m.nsPerBlock;
        const double freezeLegNs = cfgD.m.nsPerBlock - cfgB.m.nsPerBlock;
        const double envelopeRegenNs = cfgE.m.nsPerBlock - cfgB.m.nsPerBlock;
        os << "  reference                      : " << kReferenceNs
           << " ns/block (1.5 % of one core, amended 2026-07-28)\n"
           << "  largest admissible baseline    : " << kMaxAdmissibleBaselineNs << " ns/block\n"
           << "  derived: blur stage (b) - (a)  : " << blurStageNs << " ns/block\n"
           << "  derived: freeze leg (d) - (b)  : " << freezeLegNs << " ns/block\n"
           << "  derived: envelope regen (e)-(b): " << envelopeRegenNs
           << " ns/block  (one 4096-entry regeneration per block)";
        WARN(os.str());
    }

    // Over-budget REPORT, not a gate - the enforced gates are the relative ones
    // at the end. A measurement above kMaxAdmissibleBaselineNs means no baseline
    // EQUAL TO THE MEASUREMENT can be checked in for that configuration, because
    // baseline x 1.5 would exceed the reference; the baseline is capped instead
    // and the configuration loses its tight regression bound while keeping the
    // absolute one. See kMaxAdmissibleBaselineNs for the prescribed response:
    // reduce cost, never raise the baseline.
    //
    // Configuration (c) is exempt from this report: the 2026-07-28 decision puts
    // it out-of-region, so it is expected above the line by design, and a warning
    // that always fires is noise rather than signal.
    constexpr std::size_t kSaturatedIndex = 2;
    for (std::size_t i = 0; i < configNames.size(); ++i) {
        if (i == kSaturatedIndex) {
            continue;
        }
        if (measuredNs[i] > kMaxAdmissibleBaselineNs) {
            WARN("SC-004 above the admissible-baseline line, configuration "
                 << configNames[i] << ": " << measuredNs[i]
                 << " ns/block exceeds the largest admissible baseline ("
                 << kMaxAdmissibleBaselineNs
                 << " ns/block), so its checked-in baseline is the CAP rather than its "
                    "measurement and its regression bound is loose. Still inside the "
                 << kReferenceNs
                 << " ns reference. Spend an SC-004 lever (tasks.md T019) and re-measure - do "
                    "NOT raise a baseline.");
        }
    }

    // --- 3. Preconditions: did each configuration measure what it claims? ---
    // Deliberately before the gates and after the reports. A figure measured on
    // a render the optimizer removed, on a pool that never saturated, or on a
    // freeze leg that was silently filling zeros is not a measurement at all.
    {
        const std::array<const BudgetMeasurement*, 5> all = {{&cfgA, &cfgB, &cfgC, &cfgD, &cfgE}};
        for (std::size_t i = 0; i < all.size(); ++i) {
            INFO("configuration " << configNames[i]);
            // Guards against the whole loop being optimized out (a zero-cost
            // "pass") and against a figure measured on an engine that blew up.
            REQUIRE(isFiniteValue(static_cast<float>(all[i]->m.sink)));
            REQUIRE(all[i]->m.nsPerBlock > 0.0);
        }
    }

    // (a), (b), (d), (e) all run FR-009's defaults: density 4/s x grainSeconds
    // 4 s = 16 concurrent in the steady state. The bound is half of that rather
    // than 16 itself because jitter 0.5 makes the interonset a random variable,
    // so the instantaneous population fluctuates about its mean - but a
    // configuration that had latched, never filled its ring, or never birthed a
    // grain would sit far below this and must not be reported as "16 concurrent".
    {
        constexpr double kExpectedDefaultPopulation = 16.0;
        const std::array<const BudgetMeasurement*, 4> defaults = {{&cfgA, &cfgB, &cfgD, &cfgE}};
        const std::array<const char*, 4> defaultNames = {{
            configNames[0], configNames[1], configNames[3], configNames[4],
        }};
        for (std::size_t i = 0; i < defaults.size(); ++i) {
            INFO("default-density configuration " << defaultNames[i] << ", mean population "
                                                  << defaults[i]->m.meanActiveGrains);
            REQUIRE(defaults[i]->m.meanActiveGrains >= 0.5 * kExpectedDefaultPopulation);
            REQUIRE(defaults[i]->m.meanActiveGrains
                    <= static_cast<double>(AtmosphereEngine::kMaxGrains));
        }
    }

    // (c) is the SATURATED configuration, and a "saturated pool" figure measured
    // on a pool that never filled would be the default-density figure wearing a
    // different label. The bound is 90 % of kMaxGrains rather than equality
    // because a slot is legitimately empty between a retirement and the next
    // scheduler trigger, and FR-023 forbids stealing to fill it.
    {
        INFO("saturated configuration, mean population " << cfgC.m.meanActiveGrains
                                                         << ", pool-full skips "
                                                         << cfgC.m.poolFullSkips);
        constexpr double kSaturatedFloor = 0.9 * static_cast<double>(AtmosphereEngine::kMaxGrains);
        REQUIRE(cfgC.m.poolFullSkips > 0u);
        REQUIRE(cfgC.m.meanActiveGrains >= kSaturatedFloor);
        REQUIRE(cfgC.m.minActiveGrains >= (AtmosphereEngine::kMaxGrains * 9u) / 10u);
    }

    // Blur really was on where it is claimed to be, and off where it is not.
    // getAppliedBlur() reports the SMOOTHED value the stage actually read, not
    // setBlur()'s target, so a blur stage that was never pumped shows up here.
    // The layer latency is the second, independent witness: FR-046 reports
    // blurFftSize_ when blur is enabled and 0 when it is not.
    REQUIRE(cfgA.latencySamples == std::size_t{0});
    REQUIRE(cfgB.latencySamples == kDefaultBlurFftSize);
    REQUIRE(cfgC.latencySamples == kSaturatedBlurFftSize);
    REQUIRE(cfgD.latencySamples == kDefaultBlurFftSize);
    REQUIRE(cfgE.latencySamples == kDefaultBlurFftSize);
    REQUIRE(cfgB.appliedBlur > 0.99f);
    REQUIRE(cfgC.appliedBlur > 0.99f);
    REQUIRE(cfgD.appliedBlur > 0.99f);
    REQUIRE(cfgE.appliedBlur > 0.99f);

    // (d) must have measured a real drone. A never-captured oscillator early-outs
    // to zeros (spectral_freeze_oscillator.h:327-330), which would make the
    // "grain + freeze worst case" cheaper than the grain layer alone.
    REQUIRE(cfgD.freezeCaptured);
    REQUIRE_FALSE(cfgA.freezeCaptured);  // freeze disabled at prepare (FR-054)

    // --- 4. THE GATES ------------------------------------------------------
    // One runtime form for all five: measured <= its own baseline x 1.5. What
    // differs is what each BASELINE is bound to at compile time - (a), (b), (d)
    // and (e) carry the headroom clause against the 160,000 ns reference, so
    // their runtime gate transitively enforces it; (c) carries only the floor
    // clause, so its gate is a pure regression bound (2026-07-28 decision).
    for (std::size_t i = 0; i < configNames.size(); ++i) {
        INFO("SC-004 configuration "
             << configNames[i] << ": measured " << measuredNs[i] << " ns/block against baseline "
             << baselinesNs[i] << " x " << kRegressionFactor
             << (i == kSaturatedIndex ? " (out-of-region: regression bound only)"
                                      : " (also bound to the 160,000 ns reference)"));
        REQUIRE(measuredNs[i] <= baselinesNs[i] * kRegressionFactor);
    }
}
