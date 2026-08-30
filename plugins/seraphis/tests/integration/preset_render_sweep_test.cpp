// ==============================================================================
// Seraphis - Factory preset render sweep (Phase 12)
// ==============================================================================
// Reference: specs/seraphis-phase12-presets-release/spec.md
//            (C-6.1, C-6.2, C-6.3, C-7, C-9, C-10, FR-024, FR-024a, FR-025,
//             FR-026, FR-027a, FR-027b, FR-028, FR-028a, SC-009, SC-010,
//             SC-010a, SC-011, SC-012, SC-015, SC-015a, SC-026, SC-028)
//            specs/seraphis-phase12-presets-release/plan.md (sections 2.2-2.4,
//             2.6, 4.2)
//            specs/seraphis-phase12-presets-release/tasks.md (T015, T017, T018,
//             T019)
//
// This TU carries every arm of the phase that RENDERS. The static gates - the
// ones that read the declared config and the committed tree - live in
// unit/preset/factory_preset_test.cpp and are not repeated here.
//
// Landed so far:
//   T015  Seraphis_PresetSweep_NoSilence            (FR-024  / SC-009)
//         Seraphis_PresetSweep_BoundedAndFinite     (FR-025  / SC-010)
//         Seraphis_PresetSweep_ChordBoundedAndFinite(FR-024a / SC-010a)
//   T017  Seraphis_PresetSweep_DecayMatchesRt60     (FR-026 case 3 / SC-011)
//         Seraphis_PresetSweep_FrozenPresetsHold    (FR-026 cases 1-2 / SC-012)
//   T018  Seraphis_PresetSweep_NoAudioThreadAllocation  (FR-028  / SC-015)
//         Seraphis_PresetSweep_ConcurrentLoadIsRtSafe   (FR-028a / SC-015a)
//   T019  Seraphis_PresetSweep_RendersAreReproducible   (FR-027a / SC-026)
//         Seraphis_PresetSweep_PresetsAreDistinct       (FR-027b / SC-028)
//         Seraphis_PresetSweep_DistinctnessNegativeControl  ([.measure])
//
// A SEVENTH RULE, added by T019 and specific to the two distinctness arms:
//
// 7. THE DISTINCTNESS METRIC IS LEVEL-NORMALISED, AND IT IS VALIDATED FROM BOTH
//    SIDES (plan OI-4). `rms`, `peak` and `meanAbs` of a RAW fingerprint are all
//    pure amplitude aggregates (render_fingerprint.h:63-88), so the obvious
//    metric scores two TIMBRALLY IDENTICAL presets that differ only in master
//    gain as far apart - exactly the failure C-10 exists to catch. Every buffer
//    is therefore scaled to unit RMS BEFORE fingerprinting, after which `rms`
//    carries no information and is excluded, the 32 raw checkpoints are excluded
//    (at a 3 s window they are dominated by instantaneous phase and would make a
//    max() pass unconditionally), and what remains are three SHAPE statistics -
//    SeraphisTest::fingerprintDistance (preset_test_support.h:656-664). The floor
//    is then justified from BOTH sides: above by the measured `observedMinimum`
//    over the 861 real pairs, below by an INJECTED level-only twin that MUST
//    score under it (the [.measure] negative control).
//
// A SIXTH RULE, added by T018 and specific to the two allocation arms:
//
// 6. THE TWO ALLOCATION ARMS USE DIFFERENT INSTRUMENTS BECAUSE THEY REACH
//    DIFFERENT PRECONDITIONS, AND NEITHER IMPLIES THE OTHER (spec SC-015a's
//    closing sentence). SC-015 is a QUIESCENT-LOAD claim measured with the
//    unfiltered, process-global TestHelpers::AllocationScope
//    (allocation_detector.h:111-131) - which cannot attribute an allocation to a
//    thread and therefore says nothing at all about a concurrent load. SC-015a
//    is the concurrent claim and MUST use the thread-scoped instrument
//    (TestHelpers::ThreadScopedAllocationScope, allocation_detector.h:149-184,
//    added by T013). Measuring SC-015a with the unfiltered scope is FORBIDDEN by
//    FR-028a: it would count the MESSAGE thread's own setState allocations as
//    audio-thread violations and fail regardless of RT safety.
//
// A FIFTH RULE, added by T017 and specific to the tail arms:
//
// 5. A PRESET'S TAIL BEHAVIOUR IS CLASSIFIED FROM THE THREE DECODED FREEZE
//    TOGGLES AND NOTHING ELSE (kAetherFreezeId 1204, kAtmosFreezeId 1008,
//    kFxSpectralFreezeId 1430, read through SweepTimeline::aetherFreeze /
//    ::atmosOrFxFreeze). A long RT60 is a decay RATE, not a sustain: a 60 s-RT60
//    preset belongs to the DECAY arm, whose bound scales with RT60, and never to
//    a hold arm. Classifying on a measured statistic instead would let a preset
//    that failed to decay reclassify itself as one that was meant to hold.
//
// FOUR CONSTRUCTION RULES, stated here rather than discovered in review:
//
// 1. ONE RENDER PER (PRESET, RATE), SHARED BY EVERY ARM THAT READS IT. The
//    single-note render is produced by renderPreset() and handed out through
//    singleNoteRender()'s cache, so SC-009 and SC-010 - which measure DIFFERENT
//    statistics of the SAME timeline - do not render it twice. At a worst-case
//    Total of 89 s (spec C-9) a duplicated render is minutes of CI wall clock
//    for no added coverage, and SC-027's budget is a phase criterion.
//
// 2. THE STIMULUS IS RESOLVED BEFORE ANY RENDER, AND A MISS IS A FAILURE.
//    Seraphis::PresetDefs::findDef returns nullptr on a miss and its declaration
//    binds every caller to treat that as a failure (tools/seraphis_preset_defs.h,
//    "MISS POLICY"). Downgrading a miss to "use pitch 60 / velocity 0.8" would
//    let an authored outlier stimulus go untested while every arm passed, so
//    each case resolves all 42 definitions FIRST and asserts 42/42 before a
//    single block is rendered.
//
// 3. PER-SAMPLE FACTS ARE COUNTED, NOT `REQUIRE`d. A REQUIRE per sample over a
//    ~3.9-million-sample render makes the sweep unusably slow and floods
//    Catch2's reporting. Finiteness and the peak bound accumulate into plain
//    counters that are asserted ONCE, after the loop.
//
// 4. NEVER std::isnan / std::isinf. The macOS leg builds with -ffast-math, under
//    which both fold away. isFiniteFloat() below uses the bit-pattern pair
//    Krate::DSP::detail::isNaN / isInf - the same pair processor.cpp:482 screens
//    untrusted stream floats with - which is why THIS TU is registered in the
//    -fno-fast-math block (plugins/seraphis/tests/CMakeLists.txt).
// ==============================================================================

#include "processor/processor.h"
#include "preset_test_support.h"   // T007 - ${CMAKE_CURRENT_SOURCE_DIR} (= plugins/
                                   // seraphis/tests) is on this target's include
                                   // path (tests/CMakeLists.txt:102).
#include "seraphis_preset_defs.h"  // ${CMAKE_SOURCE_DIR}/tools is on this target's
                                   // include path (tests/CMakeLists.txt:104).
#include "seraphis_test_fixture.h"

#include "public.sdk/source/common/memorystream.h"

#include <krate/dsp/core/db_utils.h>  // detail::isNaN (:54) / isInf (:175) - the
                                      // BIT-PATTERN pair; never std::isnan

// T018. Both headers reach this TU through the `test_helpers` INTERFACE target
// seraphis_tests links (tests/CMakeLists.txt:95), the same way
// integration/effects_perf_test.cpp:210 and unit/test_main.cpp:6 get them.
#include <allocation_detector.h>
#include <enable_ftz_daz.h>

// T019. Reached the same way (tests/CMakeLists.txt:95). preset_test_support.h
// already pulls it in for RenderFingerprint, but SC-026 calls fingerprintRender /
// compareFingerprints DIRECTLY, so this TU names the dependency itself.
#include <render_fingerprint.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <map>
#include <numbers>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {

// =============================================================================
// Constants
// =============================================================================

// FR-027: the sweep runs at both rates, so a preset that is stable only at the
// development rate fails. Per spec C-9 the 48 kHz render stops at H + 5 s.
constexpr double kRate44100 = 44100.0;
constexpr double kRate48000 = 48000.0;

// Fixed for every sweep render. The block-size-invariance claim belongs to
// integration/processor_audio_test.cpp:91-96 and is deliberately NOT
// re-litigated here (spec Edge Cases).
constexpr std::size_t kBlockSamples = 512;

// FR-024 / Q2: the default audition stimulus. A preset's FR-016a definition may
// override it; none of the shipped 42 does today, which is exactly why the
// override path must be exercised by resolution rather than assumed away.
constexpr Steinberg::int16 kDefaultPitch = 60;
constexpr float kDefaultVelocity = 0.8f;

// FR-024a: the chord render's three fixed intervals above the stimulus root.
constexpr std::array<int, 3> kChordIntervals{4, 7, 12};
constexpr int kMaxMidiPitch = 127;

// C-6.2's bounded arm, from the shipped constants rather than a new literal:
// kLimiterCeilingLin is effects_chain_test.cpp:332 and kCeilingAllowanceDb is
// :854 (both derived from TruePeakLimiter::kDefaultCeilingDb = -1.0f,
// dsp/include/krate/dsp/processors/true_peak_limiter.h:46).
constexpr float kLimiterCeilingLin = 0.8912509f;
constexpr float kCeilingAllowanceDb = 0.1f;
const float kPeakBound = kLimiterCeilingLin * std::pow(10.0f, kCeilingAllowanceDb / 20.0f);

// SC-009's floor: -60 dBFS, as a linear stereo RMS.
constexpr double kSustainRmsFloor = 1.0e-3;

// The denominator floor for a dBFS report. It only ever affects the PRINTED
// number - every gate above compares linear RMS against kSustainRmsFloor.
constexpr double kDbFloor = 1.0e-12;

// --- T017: the tail arms ------------------------------------------------- //

// SC-011's bound is min(0.5 * 60 * W / RT60, kDecayBoundCapDb). The 0.5 factor
// asks for HALF the drop a pure RT60 decay would produce over the window, which
// is what leaves room for the dry voice, the atmosphere bed and the effects
// chain to sit under the reverb without turning a correct preset red. The cap is
// the ORIGINAL bound and is unchanged: RT60 <= 6 s still demands >= 20 dB.
constexpr double kDecayBoundCapDb = 20.0;

// SC-012 arm 1 (Aether freeze, W = 60 s): max/min across the per-second series.
//
// THE DISCLOSED RELAXATION. Roadmap line 282 states +/-0.5 dB over 60 s for
// AetherReverb ALONE. This arm measures the WHOLE PLUGIN - eight voices, the
// atmosphere layer, the effects chain and the true-peak limiter - so the band is
// widened to +/-1.0 dB (a 2.0 dB max/min span). This is the phase's ONE
// disclosed relaxation; it is repeated in every failure message this arm emits
// so it can never be read back as the AetherReverb-alone criterion.
constexpr double kAetherFreezeBandDb = 2.0;

// SC-012 arm 2 (Atmos and/or FX freeze, W = 20 s): non-GROWTH, end to end and at
// every intermediate window. No conservation band is asserted - neither
// mechanism is promised energy conservation anywhere in the roadmap (Phase 5
// lines 248-254, Phase 10 lines 464-473), and with kAetherDecayDefault = 4.0 s
// the unfrozen components are still falling while the frozen layer holds.
constexpr double kNonGrowthAllowanceDb = 1.0;

// SC-012 ONLY (never SC-011): a window this quiet under an engaged freeze means
// the freeze is broken, which is exactly what these two arms exist to catch. It
// is reported as digital silence rather than divided by.
constexpr double kFrozenSilenceFloor = 1.0e-9;

// =============================================================================
// Small helpers
// =============================================================================

// FR-025's finiteness test, BY BIT PATTERN. See rule 4 in the file banner.
[[nodiscard]] bool isFiniteFloat(float value) {
    return !Krate::DSP::detail::isNaN(value) && !Krate::DSP::detail::isInf(value);
}

[[nodiscard]] std::string joinLines(const std::vector<std::string>& lines) {
    std::ostringstream os;
    bool first = true;
    for (const auto& line : lines) {
        if (!first) {
            os << " | ";
        }
        os << line;
        first = false;
    }
    return first ? std::string("(none)") : os.str();
}

[[nodiscard]] std::string rateLabel(double sampleRate) {
    std::ostringstream os;
    os << std::llround(sampleRate) << " Hz";
    return os.str();
}

[[nodiscard]] double toDbfs(double linear) {
    return 20.0 * std::log10(std::max(linear, kDbFloor));
}

/// A ratio of two linear RMS values in dB, with the DENOMINATOR floored at
/// kDbFloor and the numerator deliberately NOT floored (T017 / SC-011: decaying
/// all the way to digital silence must pass, and that floor is the whole
/// mechanism by which it does). Callers screen a non-positive numerator
/// themselves, so this is never asked for log10(0).
[[nodiscard]] double ratioDb(double numerator, double denominator) {
    return 20.0 * std::log10(numerator / std::max(denominator, kDbFloor));
}

/// SC-011's per-preset bound: half the drop the stored RT60 predicts over the
/// window, capped at kDecayBoundCapDb.
///
/// Worked values at W = 10 s: RT60 <= 6 s -> 20 dB (the cap, i.e. the original
/// bound unchanged); RT60 = 18 s -> 16.67 dB; RT60 = 30 s -> 10 dB;
/// RT60 = 60 s -> 5 dB.
[[nodiscard]] double decayBoundDb(double rt60Seconds, double windowSeconds) {
    if (!(rt60Seconds > 0.0)) {
        // Unreachable for a shipped preset - kAetherDecayId's range starts at
        // 0.5 s - and it resolves to the STRICTEST bound rather than a lenient
        // one, so a hypothetical zero could never buy a preset a free pass.
        return kDecayBoundCapDb;
    }
    return std::min(0.5 * 60.0 * windowSeconds / rt60Seconds, kDecayBoundCapDb);
}

/// The four statistics both SC-012 arms read off one per-second series.
struct TailSummary {
    double first = 0.0;
    double last = 0.0;
    double min = 0.0;
    double max = 0.0;
    std::size_t quietestIndex = 0;  ///< which window hit `min`, for the message
    bool silent = false;            ///< any window below kFrozenSilenceFloor
};

[[nodiscard]] TailSummary summariseTail(const std::vector<double>& series) {
    TailSummary s;
    if (series.empty()) {
        s.silent = true;
        return s;
    }
    s.first = series.front();
    s.last = series.back();
    s.min = series.front();
    s.max = series.front();
    for (std::size_t i = 0; i < series.size(); ++i) {
        if (series[i] < s.min) {
            s.min = series[i];
            s.quietestIndex = i;
        }
        s.max = std::max(s.max, series[i]);
    }
    s.silent = (s.min < kFrozenSilenceFloor);
    return s;
}

// =============================================================================
// The library, parsed once
// =============================================================================
// Every case iterates the same 42 files; re-reading and re-parsing the
// containers per case is pure I/O with no added coverage (their structural
// validity is T009's criterion, not this TU's).

[[nodiscard]] const std::vector<SeraphisTest::PresetFile>& library() {
    static const std::vector<SeraphisTest::PresetFile> kLibrary = [] {
        std::vector<SeraphisTest::PresetFile> parsed;
        for (const auto& path : SeraphisTest::allPresetFiles()) {
            parsed.push_back(SeraphisTest::parseVstPreset(path));
        }
        return parsed;
    }();
    return kLibrary;
}

[[nodiscard]] std::string presetLabel(const SeraphisTest::PresetFile& pf) {
    return pf.category + "/" + pf.stem;
}

// =============================================================================
// Stimulus resolution (FR-024 / FR-024a) - rule 2 in the file banner
// =============================================================================

struct Stimulus {
    Steinberg::int16 pitch = kDefaultPitch;
    float velocity = kDefaultVelocity;
};

/// @return false when the definition table carries no entry for this file, which
///         every caller MUST treat as a failure - never as "use the default".
[[nodiscard]] bool resolveStimulus(const SeraphisTest::PresetFile& pf, Stimulus& out) {
    const Seraphis::PresetDefs::SeraphisPresetDef* def =
        Seraphis::PresetDefs::findDef(pf.category, pf.stem);
    if (def == nullptr) {
        return false;
    }
    if (def->stimulus.has_value()) {
        out.pitch = def->stimulus->pitch;
        out.velocity = def->stimulus->velocity;
    } else {
        out.pitch = kDefaultPitch;
        out.velocity = kDefaultVelocity;
    }
    return true;
}

/// Every file whose (category, stem) has no definition-table entry.
///
/// Returned rather than asserted so the caller can name ALL the unmatched
/// presets in one message instead of aborting on the first.
[[nodiscard]] std::vector<std::string> unmatchedDefinitions(
    const std::vector<SeraphisTest::PresetFile>& files) {
    std::vector<std::string> unmatched;
    for (const auto& pf : files) {
        Stimulus stimulus;
        if (!resolveStimulus(pf, stimulus)) {
            unmatched.push_back(presetLabel(pf) + " (" + pf.path.string() + ")");
        }
    }
    return unmatched;
}

// =============================================================================
// The shared render (rule 1 in the file banner)
// =============================================================================

enum class RenderLength : std::uint8_t {
    WholeTimeline,  ///< [0, Total] - the 44 100 Hz single-note render
    HoldPlusFive,   ///< [0, H + 5 s] - the 48 000 Hz render (spec C-9)
    ToHold          ///< [0, H] - FR-024a's chord render, which needs no tail, and
                    ///< (T019) FR-027a's reproducibility pair plus the negative
                    ///< control's twin, both of which measure inside [0, H] only
};

struct RenderSpec {
    double sampleRate = kRate44100;
    RenderLength length = RenderLength::WholeTimeline;
    bool chord = false;           ///< FR-024a: three extra NoteOns at t = 0
    bool retainSustain = false;   ///< keep the [A+1, A+4) buffers (FR-027b, T019)
    bool retainFull = false;      ///< keep the WHOLE captured render (FR-027a, T019)

    /// T019, negative control ONLY: a normalized kMasterGainId (ID 0) value
    /// delivered as an ordinary automation point at block 0, sample offset 0 -
    /// i.e. through the same host-facing path a preset definition's own value
    /// reaches the processor by. It lands AFTER setState and BEFORE the first
    /// process(), where masterGain_ is SNAPPED rather than ramped
    /// (processor.cpp:1573-1583), so the twin's very first output sample already
    /// carries the new level and the render is a pure scaling from t = 0.
    std::optional<double> masterGainOverride;
};

/// One preset's render plus every statistic the arms of this phase read.
///
/// The FULL render is not retained: at a worst-case Total of 89 s it is ~31 MB
/// per preset, and holding 42 of those to satisfy two arms that each need a
/// handful of scalars would cost gigabytes. What survives is the scalars plus,
/// at 44 100 Hz only, the 3 s sustain window FR-027b's pairwise distinctness arm
/// is specified to reuse ("do not re-render", tasks T019 case 2).
struct SweepRender {
    std::string failure;  ///< EMPTY on success; why the render did not happen otherwise

    SeraphisTest::SweepTimeline timeline{};
    double renderedSeconds = 0.0;

    double sustainRms = 0.0;            ///< over [A + 1.0, A + 4.0)
    std::size_t nonFiniteSamples = 0;   ///< over the WHOLE render, both channels
    std::size_t overPeakSamples = 0;    ///< |s| > kPeakBound, whole render
    float peakAbs = 0.0f;
    bool canariesIntact = false;

    std::vector<float> sustainL, sustainR;

    /// T019 / FR-027a: the WHOLE captured render, retained ONLY when
    /// RenderSpec::retainFull is set - which SC-026's arm alone sets, and only for
    /// its [0, H] length. Retaining this for the WholeTimeline render would be the
    /// ~31 MB per preset the "scalars survive, samples do not" rule above exists
    /// to avoid; at [0, H] (H <= 17 s under FR-008a's authoring ceiling) it is
    /// ~6 MB, held for one preset at a time and released before the next.
    std::vector<float> fullL, fullR;

    /// T017 - one stereo RMS per WHOLE second over the tail window
    /// [Settle, Settle + W), and therefore floor(W) entries: 10 unfrozen, 20
    /// under an Atmos/FX freeze, 60 under an Aether freeze. EMPTY for any render
    /// that stops before Settle (the 48 kHz HoldPlusFive render and the chord's
    /// ToHold render both do), which is why both tail arms are 44 100 Hz arms
    /// over the WholeTimeline render and assert the series length before reading
    /// it. Measurement begins at Settle, NEVER at the NoteOff: the settling
    /// allowance (C-6.1: 0.5 s unfrozen, 2.0 s frozen) is part of the criterion.
    std::vector<double> tailRms;
};

/// Scan one channel over the whole render, accumulating rather than asserting
/// (rule 3 in the file banner).
void scanChannel(std::span<const float> samples, SweepRender& out) {
    for (const float sample : samples) {
        if (!isFiniteFloat(sample)) {
            ++out.nonFiniteSamples;
            continue;  // std::abs of a NaN is not a peak, and would poison peakAbs
        }
        const float magnitude = std::abs(sample);
        if (magnitude > kPeakBound) {
            ++out.overPeakSamples;
        }
        out.peakAbs = std::max(out.peakAbs, magnitude);
    }
}

[[nodiscard]] SweepRender renderPreset(const SeraphisTest::PresetFile& pf,
                                       const RenderSpec& spec) {
    SweepRender out;

    if (!pf.parseError.empty()) {
        out.failure = pf.parseError;
        return out;
    }

    // The timeline comes from the preset's OWN decoded state (C-6.1) - nothing
    // here is a hardcoded per-preset constant.
    SeraphisTest::DecodedPresetState decoded;
    std::string why;
    if (!SeraphisTest::decodePresetState(pf.comp, decoded, why)) {
        out.failure = "decode failed: " + why;
        return out;
    }
    out.timeline = SeraphisTest::makeTimeline(decoded);

    Stimulus stimulus;
    if (!resolveStimulus(pf, stimulus)) {
        // Unreachable once the caller has run unmatchedDefinitions() first, and
        // stated as a failure anyway rather than silently defaulting.
        out.failure = "no Seraphis::PresetDefs entry for this (category, stem)";
        return out;
    }

    SeraphisTest::ProcessorFixture fx;
    if (fx.prepare(spec.sampleRate, static_cast<Steinberg::int32>(kBlockSamples))
        != Steinberg::kResultOk) {
        out.failure = "fixture failed to prepare at " + rateLabel(spec.sampleRate);
        return out;
    }

    // MemoryStream's two-argument constructor BORROWS the buffer and starts at
    // cursor 0 (memorystream.cpp:28-37), so the local mutable copy is what keeps
    // this free of a const_cast on `pf.comp`.
    std::vector<std::uint8_t> loaded = pf.comp;
    Steinberg::MemoryStream fromFile(loaded.data(), static_cast<Steinberg::TSize>(loaded.size()));
    if (fx.proc->setState(&fromFile) != Steinberg::kResultOk) {
        out.failure = "Processor::setState rejected the stream";
        return out;
    }

    double endSeconds = out.timeline.total;
    if (spec.length == RenderLength::HoldPlusFive) {
        endSeconds = out.timeline.H + 5.0;
    } else if (spec.length == RenderLength::ToHold) {
        endSeconds = out.timeline.H;
    }

    const std::size_t endSamples = SeraphisTest::sampleIndex(endSeconds, spec.sampleRate);
    const std::size_t numBlocks = (endSamples + kBlockSamples - 1u) / kBlockSamples;
    if (numBlocks == 0) {
        out.failure = "computed a zero-block render (Total = " + std::to_string(endSeconds) + " s)";
        return out;
    }

    // The NoteOff instant, and the block that contains it. Samples past
    // `endSeconds` are rendered (the block grid does not divide the timeline) but
    // never measured.
    const std::size_t noteOffSample = SeraphisTest::sampleIndex(out.timeline.H, spec.sampleRate);
    const std::size_t noteOffBlock = noteOffSample / kBlockSamples;
    const bool sendNoteOff = (spec.length != RenderLength::ToHold);

    fx.reserveCapture(numBlocks * kBlockSamples);
    fx.renderBlocks(numBlocks, kBlockSamples,
                    [&fx, &stimulus, &spec, sendNoteOff, noteOffBlock, noteOffSample](
                        std::size_t b, Krate::Test::EventList&, SeraphisTest::ParameterChanges&) {
                        if (b == 0) {
                            // T019: the level-only twin's ONE authored difference,
                            // delivered as a block-0 automation point (see
                            // RenderSpec::masterGainOverride). Absent for every
                            // other render in this TU, so no other arm's timeline
                            // moves.
                            if (spec.masterGainOverride.has_value()) {
                                fx.setParam(Seraphis::kMasterGainId, *spec.masterGainOverride);
                            }
                            fx.pushEvent(Steinberg::Vst::Event::kNoteOnEvent, stimulus.pitch,
                                         stimulus.velocity, 0);
                            if (spec.chord) {
                                for (const int interval : kChordIntervals) {
                                    const int pitch =
                                        std::min(static_cast<int>(stimulus.pitch) + interval,
                                                 kMaxMidiPitch);
                                    fx.pushEvent(Steinberg::Vst::Event::kNoteOnEvent,
                                                 static_cast<Steinberg::int16>(pitch),
                                                 stimulus.velocity, 0);
                                }
                            }
                        }
                        // FR-024: the hold [0, H] carries NO NoteOff - that hold IS
                        // the roadmap's NoteOn-only stuck-note condition (line 608).
                        if (sendNoteOff && b == noteOffBlock) {
                            const auto offset = static_cast<Steinberg::int32>(
                                noteOffSample - (b * kBlockSamples));
                            fx.pushEvent(Steinberg::Vst::Event::kNoteOffEvent, stimulus.pitch, 0.0f,
                                         offset);
                        }
                    });

    out.renderedSeconds =
        static_cast<double>(numBlocks * kBlockSamples) / spec.sampleRate;
    out.canariesIntact = fx.checkCanaries();
    scanChannel(fx.capturedL, out);
    scanChannel(fx.capturedR, out);

    const std::size_t susFirst = SeraphisTest::sampleIndex(out.timeline.susBegin, spec.sampleRate);
    const std::size_t susLast = SeraphisTest::sampleIndex(out.timeline.susEnd, spec.sampleRate);
    out.sustainRms = SeraphisTest::rmsOver(fx.capturedL, fx.capturedR, susFirst, susLast);

    // T017 - the tail series SC-011 and SC-012 read. Computed HERE, off the same
    // buffers, because the tail arms are specified to reuse this render rather
    // than produce a second one (rule 1 in the file banner): at a worst-case
    // Total of 89 s a duplicated Aether-freeze render is minutes of CI wall clock
    // for no added coverage. Only the [0, Total] render reaches Settle at all -
    // asking the other two lengths for a window they never rendered would yield a
    // silently SHORT series rather than an error.
    if (spec.length == RenderLength::WholeTimeline) {
        const std::size_t tailFirst =
            SeraphisTest::sampleIndex(out.timeline.settle, spec.sampleRate);
        const std::size_t tailLast =
            SeraphisTest::sampleIndex(out.timeline.settle + out.timeline.W, spec.sampleRate);
        out.tailRms = SeraphisTest::perSecondRms(fx.capturedL, fx.capturedR, tailFirst, tailLast,
                                                 spec.sampleRate);
    }

    if (spec.retainFull) {
        // FR-027a compares two renders sample-window for sample-window, so the
        // whole capture is handed back verbatim - no window, no statistic.
        out.fullL = fx.capturedL;
        out.fullR = fx.capturedR;
    }

    if (spec.retainSustain) {
        const std::size_t available = std::min(fx.capturedL.size(), fx.capturedR.size());
        const std::size_t first = std::min(susFirst, available);
        const std::size_t last = std::min(susLast, available);
        if (first < last) {
            out.sustainL.assign(fx.capturedL.begin() + static_cast<std::ptrdiff_t>(first),
                                fx.capturedL.begin() + static_cast<std::ptrdiff_t>(last));
            out.sustainR.assign(fx.capturedR.begin() + static_cast<std::ptrdiff_t>(first),
                                fx.capturedR.begin() + static_cast<std::ptrdiff_t>(last));
        }
    }

    return out;
}

/// The single-note render of one preset at one rate, produced ONCE per process
/// and shared by every arm that reads it (rule 1 in the file banner).
///
/// EXTENSION POINT: the tail arms (SC-011 / SC-012) and the distinctness arm
/// (SC-028) are specified to read THIS render rather than produce their own -
/// they add fields to SweepRender and statistics to renderPreset(), never a
/// second render of the same timeline. T017 took that route: both tail arms read
/// `SweepRender::tailRms`, which renderPreset() fills, and neither renders a
/// single extra block.
[[nodiscard]] const SweepRender& singleNoteRender(const SeraphisTest::PresetFile& pf,
                                                  double sampleRate) {
    static std::map<std::string, SweepRender> cache;

    const bool isDevelopmentRate = (std::llround(sampleRate) == std::llround(kRate44100));
    const std::string key = presetLabel(pf) + "@" + rateLabel(sampleRate);

    const auto found = cache.find(key);
    if (found != cache.end()) {
        return found->second;
    }

    RenderSpec spec;
    spec.sampleRate = sampleRate;
    spec.length = isDevelopmentRate ? RenderLength::WholeTimeline : RenderLength::HoldPlusFive;
    spec.chord = false;
    // Only the 44 100 Hz window is reused downstream (FR-027b is a 44 100 Hz
    // arm), so retaining the 48 kHz one would double the sweep's resident memory
    // for nothing.
    spec.retainSustain = isDevelopmentRate;

    return cache.emplace(key, renderPreset(pf, spec)).first->second;
}

}  // namespace

// =============================================================================
// T015 - SC-009: every preset makes sound
// =============================================================================
// CRITERION OWNED BY THIS CASE: FR-024 / SC-009 - the stereo RMS over the
// preset's OWN sustain window Sus = [A + 1.0 s, A + 4.0 s] is above -60 dBFS for
// 100 % of presets, at BOTH rates.
//
// THE +1.0 s OFFSET IS LOAD-BEARING, NOT PADDING. A Growth-mode preset is
// legitimately near-silent for seconds (growth duration default 10.0 s,
// life_mod_params.h:65), so a window opening at t = 0 would fail CORRECT presets.
// `A` is decoded per preset (C-6.1), never a fixed guess.
TEST_CASE("Seraphis_PresetSweep_NoSilence", "[seraphis][preset][sweep][long]") {
    const std::vector<SeraphisTest::PresetFile>& files = library();
    INFO("presets root: " << SeraphisTest::factoryPresetRoot().string());
    INFO("presets discovered: " << files.size());
    REQUIRE(!files.empty());

    // Rule 2: resolve every stimulus BEFORE any render. A null findDef() is a
    // failure, never a silent fall back to pitch 60 / velocity 0.8.
    const std::vector<std::string> unmatched = unmatchedDefinitions(files);
    INFO("presets with no Seraphis::PresetDefs entry: " << joinLines(unmatched));
    REQUIRE(unmatched.empty());

    std::vector<std::string> renderFailures;
    std::vector<std::string> silent;

    for (const double sampleRate : {kRate44100, kRate48000}) {
        for (const auto& pf : files) {
            const SweepRender& render = singleNoteRender(pf, sampleRate);
            const std::string where = presetLabel(pf) + " @ " + rateLabel(sampleRate);

            if (!render.failure.empty()) {
                renderFailures.push_back(where + ": " + render.failure);
                continue;
            }

            if (!(render.sustainRms > kSustainRmsFloor)) {
                std::ostringstream os;
                os << where << ": A = " << render.timeline.A << " s, sustain window ["
                   << render.timeline.susBegin << ", " << render.timeline.susEnd
                   << ") s, RMS = " << toDbfs(render.sustainRms) << " dBFS (floor "
                   << toDbfs(kSustainRmsFloor) << " dBFS)";
                silent.push_back(os.str());
            }
        }
    }

    INFO("presets whose render did not happen: " << joinLines(renderFailures));
    REQUIRE(renderFailures.empty());

    INFO("presets at or below the SC-009 sustain floor: " << joinLines(silent));
    REQUIRE(silent.empty());
}

// =============================================================================
// T015 - SC-010: every preset stays bounded
// =============================================================================
// CRITERION OWNED BY THIS CASE: FR-025 / SC-010 - over the WHOLE render
// [0, Total], the NoteOn-only hold [0, H] included, 0 non-finite samples (bit
// pattern) and peak <= kLimiterCeilingLin x 10^(0.1/20) ~= 0.9016, at both rates.
//
// THE HOLD IS THE POINT. [0, H] carries no NoteOff, so it is the roadmap's
// NoteOn-only stuck-note / runaway condition (line 608) and Membrum's
// infinite-ring pattern (plugins/membrum/tests/unit/processor/
// test_kit_switch_infinite_ring.cpp:54-57) - except that no arm of this phase
// asserts silence, because with an 8-voice organism, a 60 s Aether tail and
// first-class freeze toggles, "silent" is not a property this instrument has
// (spec C-6.3).
//
// checkCanaries() rides along: the guard words either side of both output
// buffers (seraphis_test_fixture.h:161-162, :371-374) turn an out-of-bounds
// write into a failure in the SAME pass, at no extra render cost.
TEST_CASE("Seraphis_PresetSweep_BoundedAndFinite", "[seraphis][preset][sweep]") {
    const std::vector<SeraphisTest::PresetFile>& files = library();
    INFO("presets root: " << SeraphisTest::factoryPresetRoot().string());
    INFO("peak bound: " << kPeakBound << " (kLimiterCeilingLin " << kLimiterCeilingLin
                        << " x 10^(" << kCeilingAllowanceDb << "/20))");
    REQUIRE(!files.empty());

    const std::vector<std::string> unmatched = unmatchedDefinitions(files);
    INFO("presets with no Seraphis::PresetDefs entry: " << joinLines(unmatched));
    REQUIRE(unmatched.empty());

    std::vector<std::string> renderFailures;
    std::vector<std::string> nonFinite;
    std::vector<std::string> overPeak;
    std::vector<std::string> trampled;

    for (const double sampleRate : {kRate44100, kRate48000}) {
        for (const auto& pf : files) {
            const SweepRender& render = singleNoteRender(pf, sampleRate);
            const std::string where = presetLabel(pf) + " @ " + rateLabel(sampleRate);

            if (!render.failure.empty()) {
                renderFailures.push_back(where + ": " + render.failure);
                continue;
            }

            if (render.nonFiniteSamples != 0) {
                nonFinite.push_back(where + ": " + std::to_string(render.nonFiniteSamples)
                                    + " non-finite sample(s) over "
                                    + std::to_string(render.renderedSeconds) + " s");
            }
            if (render.overPeakSamples != 0) {
                std::ostringstream os;
                os << where << ": " << render.overPeakSamples << " sample(s) above " << kPeakBound
                   << ", peak = " << render.peakAbs;
                overPeak.push_back(os.str());
            }
            if (!render.canariesIntact) {
                trampled.push_back(where);
            }
        }
    }

    INFO("presets whose render did not happen: " << joinLines(renderFailures));
    REQUIRE(renderFailures.empty());

    INFO("presets producing non-finite samples: " << joinLines(nonFinite));
    REQUIRE(nonFinite.empty());

    INFO("presets exceeding the limiter ceiling: " << joinLines(overPeak));
    REQUIRE(overPeak.empty());

    INFO("presets that wrote outside their output buffers: " << joinLines(trampled));
    REQUIRE(trampled.empty());
}

// =============================================================================
// T015 - SC-010a: the 4-note chord stays bounded too
// =============================================================================
// CRITERION OWNED BY THIS CASE: FR-024a / SC-010a - a held 4-note chord (root =
// the resolved stimulus pitch, plus +4, +7, +12, all at the stimulus velocity),
// 44 100 Hz only, rendered to H with NO NoteOff, asserting ONLY the C-6.2
// bounded arm.
//
// WHAT THIS CASE DELIBERATELY DOES NOT ASSERT: SC-009's sustain floor and every
// tail arm. It is the one arm of the phase that renders more than one voice, and
// its whole purpose is to put FR-008's polyphony <= 8 and the multi-voice
// limiter sum through actual audio WITHOUT paying chord-render cost on the 60 s
// tail arms (spec C-9). Adding an arm here would buy the cost the design bought
// its way out of.
//
// This render is NOT cached: this case is the only consumer of a chord render,
// so a cache entry would be written once and read by nobody.
TEST_CASE("Seraphis_PresetSweep_ChordBoundedAndFinite", "[seraphis][preset][sweep][long]") {
    const std::vector<SeraphisTest::PresetFile>& files = library();
    INFO("presets root: " << SeraphisTest::factoryPresetRoot().string());
    INFO("chord intervals above the stimulus root: +4, +7, +12 semitones");
    INFO("peak bound: " << kPeakBound);
    REQUIRE(!files.empty());

    const std::vector<std::string> unmatched = unmatchedDefinitions(files);
    INFO("presets with no Seraphis::PresetDefs entry: " << joinLines(unmatched));
    REQUIRE(unmatched.empty());

    std::vector<std::string> renderFailures;
    std::vector<std::string> nonFinite;
    std::vector<std::string> overPeak;
    std::vector<std::string> trampled;

    for (const auto& pf : files) {
        RenderSpec spec;
        spec.sampleRate = kRate44100;  // FR-024a: 44 100 Hz only
        spec.length = RenderLength::ToHold;
        spec.chord = true;
        spec.retainSustain = false;

        const SweepRender render = renderPreset(pf, spec);
        const std::string where = presetLabel(pf) + " (chord @ " + rateLabel(kRate44100) + ")";

        if (!render.failure.empty()) {
            renderFailures.push_back(where + ": " + render.failure);
            continue;
        }

        if (render.nonFiniteSamples != 0) {
            nonFinite.push_back(where + ": " + std::to_string(render.nonFiniteSamples)
                                + " non-finite sample(s) over "
                                + std::to_string(render.renderedSeconds) + " s");
        }
        if (render.overPeakSamples != 0) {
            std::ostringstream os;
            os << where << ": " << render.overPeakSamples << " sample(s) above " << kPeakBound
               << ", peak = " << render.peakAbs;
            overPeak.push_back(os.str());
        }
        if (!render.canariesIntact) {
            trampled.push_back(where);
        }
    }

    INFO("presets whose chord render did not happen: " << joinLines(renderFailures));
    REQUIRE(renderFailures.empty());

    INFO("chord renders producing non-finite samples: " << joinLines(nonFinite));
    REQUIRE(nonFinite.empty());

    INFO("chord renders exceeding the limiter ceiling: " << joinLines(overPeak));
    REQUIRE(overPeak.empty());

    INFO("chord renders that wrote outside their output buffers: " << joinLines(trampled));
    REQUIRE(trampled.empty());
}

// =============================================================================
// T017 - SC-011: an unfrozen tail decays at least as fast as its RT60 predicts
// =============================================================================
// CRITERION OWNED BY THIS CASE: FR-026 case 3 / SC-011 - for every preset with
// ALL THREE freeze toggles OFF, at 44 100 Hz, over the W = 10 s tail window
// [Settle, Settle + 10):
//
//     dropDb = 20 * log10( rms(first second) / max(rms(final second), 1e-12) )
//     dropDb >= min(0.5 * 60 * W / RT60, 20.0)
//
// THIS ARM CARRIES NO DIGITAL-SILENCE GUARD, DELIBERATELY. The bound is
// ONE-SIDED: decaying FASTER than the reverb predicts always passes, including
// all the way to digital silence - and the 1e-12 denominator floor is the whole
// mechanism by which a fully-decayed tail passes instead of dividing by zero.
// The numerator is NOT floored. A guard here would fail correct dry-dominant
// presets (kAetherDecayId's legal range starts at 0.5 s, so a 20 dB drop inside
// 9 s of window separation is ordinary) and would contradict the formula sitting
// in the same case. SC-009 has already rejected any preset that is silent during
// its own sustain window, which is where "the preset makes no sound" is caught.
//
// CLASSIFICATION IS FROM THE TOGGLES ONLY (rule 5 in the file banner): a 60 s
// RT60 is a decay RATE and belongs HERE, with a bound of 5 dB, not to a hold arm.
TEST_CASE("Seraphis_PresetSweep_DecayMatchesRt60", "[seraphis][preset][sweep]") {
    const std::vector<SeraphisTest::PresetFile>& files = library();
    INFO("presets root: " << SeraphisTest::factoryPresetRoot().string());
    INFO("bound: dropDb >= min(0.5 * 60 * W / RT60, " << kDecayBoundCapDb << ") over W = 10 s");
    REQUIRE(!files.empty());

    const std::vector<std::string> unmatched = unmatchedDefinitions(files);
    INFO("presets with no Seraphis::PresetDefs entry: " << joinLines(unmatched));
    REQUIRE(unmatched.empty());

    std::vector<std::string> renderFailures;
    std::vector<std::string> shortSeries;
    std::vector<std::string> belowBound;
    std::size_t qualifying = 0;

    // The tightest margin in the run, reported unconditionally so the phase has a
    // number rather than a pass/fail bit.
    bool haveMargin = false;
    double worstMarginDb = 0.0;
    std::string worstMarginWhere;

    for (const auto& pf : files) {
        const SweepRender& render = singleNoteRender(pf, kRate44100);
        const std::string where = presetLabel(pf) + " @ " + rateLabel(kRate44100);

        if (!render.failure.empty()) {
            renderFailures.push_back(where + ": " + render.failure);
            continue;
        }

        // Rule 5: the three decoded toggles, and nothing measured.
        if (render.timeline.aetherFreeze || render.timeline.atmosOrFxFreeze) {
            continue;
        }
        ++qualifying;

        if (render.tailRms.size() < 2u) {
            shortSeries.push_back(where + ": tail series holds "
                                  + std::to_string(render.tailRms.size())
                                  + " one-second window(s), needs >= 2 (Settle = "
                                  + std::to_string(render.timeline.settle) + " s, W = "
                                  + std::to_string(render.timeline.W) + " s)");
            continue;
        }

        const double rFirst = render.tailRms.front();
        const double rFinal = render.tailRms.back();
        const double bound = decayBoundDb(render.timeline.RT60, render.timeline.W);

        if (!(rFirst > 0.0)) {
            // A REPORTING guard, not a threshold: 20*log10(0 / x) is -inf, which
            // already fails every bound this arm can produce. Naming the
            // condition beats printing "-inf" in the failure list.
            belowBound.push_back(where + ": first tail window is exactly zero, so no drop can be"
                                         " measured (RT60 = "
                                  + std::to_string(render.timeline.RT60) + " s)");
            continue;
        }

        const double dropDb = ratioDb(rFirst, rFinal);
        const double marginDb = dropDb - bound;
        if (!haveMargin || marginDb < worstMarginDb) {
            haveMargin = true;
            worstMarginDb = marginDb;
            worstMarginWhere = where;
        }

        if (!(dropDb >= bound)) {
            std::ostringstream os;
            os << where << ": RT60 = " << render.timeline.RT60 << " s, W = " << render.timeline.W
               << " s, first window " << toDbfs(rFirst) << " dBFS, final window " << toDbfs(rFinal)
               << " dBFS, drop = " << dropDb << " dB, required >= " << bound << " dB";
            belowBound.push_back(os.str());
        }
    }

    INFO("presets whose render did not happen: " << joinLines(renderFailures));
    REQUIRE(renderFailures.empty());

    INFO("presets whose tail series was too short to measure: " << joinLines(shortSeries));
    REQUIRE(shortSeries.empty());

    // Non-vacuity. T011's coverage ledger already requires each of 1008 / 1204 /
    // 1430 to be OFF in at least one preset, but that does not by itself produce
    // a preset with all THREE off - which is the population this arm measures.
    INFO("presets with all three freeze toggles OFF: " << qualifying);
    REQUIRE(qualifying > 0u);

    if (haveMargin) {
        std::ostringstream os;
        os << "SC-011 tightest margin over " << qualifying << " unfrozen preset(s): "
           << worstMarginDb << " dB above its own bound, held by " << worstMarginWhere;
        WARN(os.str());
    }

    INFO("presets decaying slower than their own RT60 bound: " << joinLines(belowBound));
    REQUIRE(belowBound.empty());
}

// =============================================================================
// T017 - SC-012: an engaged freeze holds
// =============================================================================
// CRITERION OWNED BY THIS CASE: FR-026 cases 1-2 / SC-012, both arms at
// 44 100 Hz, both starting at Settle = H + Rel + 2.0 s (the frozen settling
// allowance, C-6.1).
//
//   Arm 2 (runs on ALL THREE LEGS) - kAtmosFreezeId and/or kFxSpectralFreezeId
//   ON with kAetherFreezeId OFF, W = 20 s:
//       (a) 20*log10(rFinal / r0) <= 1.0 dB     non-growing, end to end
//       (b) 20*log10(rMax   / r0) <= 1.0 dB     no intermediate growth either
//   The spec's literal wording - "final <= loudest + 1.0 dB" - is TAUTOLOGICAL:
//   loudest >= final by construction, so it passes for any behaviour whatsoever.
//   OI-2 repairs it WITHOUT touching the threshold number by comparing against
//   the FIRST window instead of the loudest. Nothing is relaxed; an arm that
//   could not fail is replaced by two that can.
//
//   Arm 1 (WINDOWS LEG ONLY, spec C-9) - kAetherFreezeId ON, W = 60 s:
//       20*log10(rMax / rMin) <= 2.0 dB, i.e. a +/-1.0 dB band.
//   THE 60 s OBSERVATION DURATION IS ROADMAP LINE 282's AND IS NOT REDUCED: a
//   20 s window cannot distinguish a 60 s-RT60 tail from a conserving one.
//
// THE WINDOWS GUARD IS INSIDE THE TEST_CASE, NEVER AROUND IT, so Catch2's
// `test cases:` count is identical on all three legs and SC-021's delta stays
// meaningful.
//
// THE DIGITAL-SILENCE GUARD APPLIES TO THESE TWO ARMS AND ONLY THESE TWO: a
// window below 1e-9 under an engaged freeze means the freeze is broken, which is
// precisely what these arms catch. SC-011 above must NOT have it.
TEST_CASE("Seraphis_PresetSweep_FrozenPresetsHold", "[seraphis][preset][sweep]") {
    const std::vector<SeraphisTest::PresetFile>& files = library();
    INFO("presets root: " << SeraphisTest::factoryPresetRoot().string());
    // Stated once for both arms - and it also keeps kAetherFreezeBandDb ODR-used
    // on the legs where arm 1 is compiled out, so the constant cannot become an
    // unused-variable warning there.
    INFO("SC-012 allowances: Atmos/FX non-growth " << kNonGrowthAllowanceDb
         << " dB (all legs); Aether-freeze band " << kAetherFreezeBandDb
         << " dB, i.e. +/-1.0 dB, WIDENED from roadmap line 282's +/-0.5 dB because this phase"
            " measures the whole plugin rather than AetherReverb alone (Windows leg, spec C-9)");
    REQUIRE(!files.empty());

    const std::vector<std::string> unmatched = unmatchedDefinitions(files);
    INFO("presets with no Seraphis::PresetDefs entry: " << joinLines(unmatched));
    REQUIRE(unmatched.empty());

    // ---------------------------------------------------------------------- //
    // Arm 2 - Atmos and/or FX spectral freeze, Aether freeze OFF. All legs.
    // ---------------------------------------------------------------------- //
    {
        std::vector<std::string> renderFailures;
        std::vector<std::string> shortSeries;
        std::vector<std::string> silent;
        std::vector<std::string> grewEndToEnd;   // clause (a)
        std::vector<std::string> grewInterior;   // clause (b)
        std::vector<std::string> margins;
        std::size_t qualifying = 0;

        for (const auto& pf : files) {
            const SweepRender& render = singleNoteRender(pf, kRate44100);
            const std::string where = presetLabel(pf) + " (Atmos/FX freeze)";

            if (!render.failure.empty()) {
                renderFailures.push_back(where + ": " + render.failure);
                continue;
            }
            if (render.timeline.aetherFreeze || !render.timeline.atmosOrFxFreeze) {
                continue;
            }
            ++qualifying;

            if (render.tailRms.size() < 2u) {
                shortSeries.push_back(where + ": tail series holds "
                                      + std::to_string(render.tailRms.size())
                                      + " one-second window(s), needs >= 2 (W = "
                                      + std::to_string(render.timeline.W) + " s)");
                continue;
            }

            const TailSummary tail = summariseTail(render.tailRms);
            if (tail.silent) {
                std::ostringstream os;
                os << where << ": frozen preset produced digital silence - window "
                   << tail.quietestIndex << " of " << render.tailRms.size() << " measured "
                   << toDbfs(tail.min) << " dBFS, below the " << toDbfs(kFrozenSilenceFloor)
                   << " dBFS floor";
                silent.push_back(os.str());
                continue;
            }

            const double finalDb = ratioDb(tail.last, tail.first);
            const double peakDb = ratioDb(tail.max, tail.first);

            {
                std::ostringstream os;
                os << where << ": final " << finalDb << " dB (margin "
                   << (kNonGrowthAllowanceDb - finalDb) << " dB), loudest " << peakDb
                   << " dB (margin " << (kNonGrowthAllowanceDb - peakDb) << " dB) vs the "
                   << kNonGrowthAllowanceDb << " dB allowance";
                margins.push_back(os.str());
            }

            if (!(finalDb <= kNonGrowthAllowanceDb)) {
                std::ostringstream os;
                os << where << ": SC-012 clause (a) - final window is " << finalDb
                   << " dB above the first, allowance " << kNonGrowthAllowanceDb << " dB";
                grewEndToEnd.push_back(os.str());
            }
            if (!(peakDb <= kNonGrowthAllowanceDb)) {
                std::ostringstream os;
                os << where << ": SC-012 clause (b) - loudest window is " << peakDb
                   << " dB above the first, allowance " << kNonGrowthAllowanceDb << " dB";
                grewInterior.push_back(os.str());
            }
        }

        INFO("presets whose render did not happen: " << joinLines(renderFailures));
        REQUIRE(renderFailures.empty());

        INFO("Atmos/FX-freeze presets whose tail series was too short: " << joinLines(shortSeries));
        REQUIRE(shortSeries.empty());

        // Non-vacuity: T011's coverage ledger requires 1008 and 1430 each ON in at
        // least one preset, but this arm's population is narrower than either
        // (Aether freeze must also be OFF), so it asserts its own count.
        INFO("presets with Atmos and/or FX freeze ON and Aether freeze OFF: " << qualifying);
        REQUIRE(qualifying > 0u);

        // Unconditional so the measured margins reach the run log, which is where
        // compliance.md's SC-012 section is transcribed from.
        WARN("SC-012 arm 2 measured margins | " + joinLines(margins));

        INFO("Atmos/FX-freeze presets that fell to digital silence: " << joinLines(silent));
        REQUIRE(silent.empty());

        INFO("clause (a) - Atmos/FX-freeze presets whose tail GREW end to end: "
             << joinLines(grewEndToEnd));
        REQUIRE(grewEndToEnd.empty());

        // Clause (b), PROMOTED to gating in this same task (tasks.md T017): the
        // margins above are recorded in compliance.md, and an arm that reports
        // without gating cannot fail.
        INFO("clause (b) - Atmos/FX-freeze presets with interior tail growth: "
             << joinLines(grewInterior));
        REQUIRE(grewInterior.empty());
    }

    // ---------------------------------------------------------------------- //
    // Arm 1 - Aether freeze. WINDOWS LEG ONLY (spec C-9): the 60 s band is the
    // single heaviest render in the phase, and FR-008a's ceiling plus this arm
    // are what keep SC-027's budget reachable on the two slower legs.
    // ---------------------------------------------------------------------- //
#ifdef _WIN32
    {
        const std::string kRelaxation =
            "SC-012 arm 1 band is +/-1.0 dB (max/min span <= 2.0 dB), WIDENED from roadmap line"
            " 282's +/-0.5 dB because this arm measures the WHOLE PLUGIN - 8 voices, the"
            " atmosphere layer, the effects chain and the true-peak limiter - not AetherReverb"
            " alone. This is the phase's ONE disclosed relaxation and is never the"
            " AetherReverb-alone criterion.";

        std::vector<std::string> renderFailures;
        std::vector<std::string> shortSeries;
        std::vector<std::string> silent;
        std::vector<std::string> outOfBand;
        std::vector<std::string> margins;
        std::size_t qualifying = 0;

        for (const auto& pf : files) {
            const SweepRender& render = singleNoteRender(pf, kRate44100);
            const std::string where = presetLabel(pf) + " (Aether freeze)";

            if (!render.failure.empty()) {
                renderFailures.push_back(where + ": " + render.failure);
                continue;
            }
            if (!render.timeline.aetherFreeze) {
                continue;
            }
            ++qualifying;

            // 60 one-second windows, from W = 60.0 (preset_test_support.h:563).
            if (render.tailRms.size() < 2u) {
                shortSeries.push_back(where + ": tail series holds "
                                      + std::to_string(render.tailRms.size())
                                      + " one-second window(s), needs >= 2 (W = "
                                      + std::to_string(render.timeline.W) + " s)");
                continue;
            }

            const TailSummary tail = summariseTail(render.tailRms);
            if (tail.silent) {
                std::ostringstream os;
                os << where << ": frozen preset produced digital silence - window "
                   << tail.quietestIndex << " of " << render.tailRms.size() << " measured "
                   << toDbfs(tail.min) << " dBFS, below the " << toDbfs(kFrozenSilenceFloor)
                   << " dBFS floor";
                silent.push_back(os.str());
                continue;
            }

            const double bandDb = ratioDb(tail.max, tail.min);
            {
                std::ostringstream os;
                os << where << ": band " << bandDb << " dB over " << render.tailRms.size()
                   << " s (margin " << (kAetherFreezeBandDb - bandDb) << " dB)";
                margins.push_back(os.str());
            }

            if (!(bandDb <= kAetherFreezeBandDb)) {
                std::ostringstream os;
                os << where << ": band = " << bandDb << " dB over " << render.tailRms.size()
                   << " one-second windows (loudest " << toDbfs(tail.max) << " dBFS, quietest "
                   << toDbfs(tail.min) << " dBFS), allowance " << kAetherFreezeBandDb << " dB. "
                   << kRelaxation;
                outOfBand.push_back(os.str());
            }
        }

        INFO("presets whose render did not happen: " << joinLines(renderFailures));
        REQUIRE(renderFailures.empty());

        INFO("Aether-freeze presets whose tail series was too short: " << joinLines(shortSeries));
        REQUIRE(shortSeries.empty());

        // Non-vacuity: T011's ledger requires 1204 ON in at least one preset, and
        // this arm re-asserts it rather than inheriting it silently.
        INFO("presets with Aether freeze ON: " << qualifying);
        REQUIRE(qualifying > 0u);

        WARN("SC-012 arm 1 measured bands | " + joinLines(margins));

        INFO("Aether-freeze presets that fell to digital silence: " << joinLines(silent));
        REQUIRE(silent.empty());

        INFO(kRelaxation);
        INFO("Aether-freeze presets outside their band: " << joinLines(outOfBand));
        REQUIRE(outOfBand.empty());
    }
#else
    SUCCEED("SC-012 Aether-freeze arm is Windows-leg only (spec C-9); Atmos/FX arm ran above");
#endif
}

namespace {

// =============================================================================
// T018 - shared machinery for the two allocation arms
// =============================================================================

/// SC-015: blocks rendered inside ONE measured AllocationScope, per preset.
/// 64 x 512 = 32 768 samples ~= 0.743 s at 44 100 Hz, so the 42-preset sequence
/// measures ~31 s of audio in total. Long enough that setState's deferred
/// republish (requestPushAllSurfaces, processor.cpp:1844) is serviced INSIDE the
/// measured window - the work this criterion is actually about - and short
/// enough that the arm costs seconds rather than minutes against SC-027.
constexpr std::size_t kSequentialBlocks = 64;

/// SC-015a's stated ceiling: 8 600 x 512 ~= 100 s of audio at 44 100 Hz. It is
/// an upper BOUND, not the expected count - the loop exits as soon as the
/// message thread has finished all 42 loads (see kBlocksBetweenLoads). Stating a
/// bound at all is what keeps the arm from being able to spin forever if the
/// message thread ever failed to reach its `stop`.
constexpr std::size_t kConcurrentBlocks = 8600;

/// SC-015a: the audio thread must have rendered at least this many FURTHER
/// blocks before the message thread issues its next setState.
///
/// Without a pacing rule all 42 loads land inside the first block or two and the
/// arm silently degrades into "setState was called from another thread once",
/// which is NOT the interleaving FR-028a exists to reach (spec:921-926). 42 x 4
/// = 168 blocks minimum, two orders of magnitude inside kConcurrentBlocks, so
/// the bound above is never the thing that ends the run.
constexpr std::size_t kBlocksBetweenLoads = 4;

/// A MUTABLE copy of every preset's `Comp` chunk, in `files` order.
///
/// Steinberg::MemoryStream's two-argument constructor BORROWS its buffer
/// (memorystream.cpp:28-37), so a mutable copy is what keeps both arms free of a
/// const_cast on `PresetFile::comp`. Taken ONCE, up front, so that the
/// concurrent arm's message thread does a setState per iteration and not a
/// vector copy.
///
/// Files that failed to parse are reported through `unreadable` rather than
/// skipped silently: every caller asserts that list is empty BEFORE indexing,
/// which is what keeps `chunks[i]` aligned with `files[i]`.
[[nodiscard]] std::vector<std::vector<std::uint8_t>> componentChunks(
    const std::vector<SeraphisTest::PresetFile>& files, std::vector<std::string>& unreadable) {
    std::vector<std::vector<std::uint8_t>> chunks;
    chunks.reserve(files.size());
    for (const auto& pf : files) {
        if (!pf.parseError.empty()) {
            unreadable.push_back(presetLabel(pf) + ": " + pf.parseError);
            continue;
        }
        chunks.push_back(pf.comp);
    }
    return chunks;
}

}  // namespace

// =============================================================================
// T018 - SC-015: sequential preset switching allocates nothing
// =============================================================================
// CRITERION OWNED BY THIS CASE: FR-028 / SC-015 - loading all 42 presets in
// sequence into a RUNNING, WARMED processor, with setState strictly BETWEEN
// process() calls, yields 0 allocations inside TestHelpers::AllocationScope
// around the render calls. Threshold: 0 allocations, 42/42.
//
// THIS IS THE QUIESCENT-LOAD CLAIM AND NOTHING MORE (spec SC-015, FR-028).
// AllocationScope wraps a process-global singleton counter with NO thread filter
// (allocation_detector.h:48-104, :111-131); it cannot attribute an allocation to
// a thread and therefore cannot, on its own, say anything about a CONCURRENT
// load. That is SC-015a's job, below, with a different instrument. A green
// result here does not imply a green result there.
//
// THREE PRECONDITIONS, ALL EQUALLY LOAD-BEARING:
//
//   (i)   setState runs OUTSIDE the scope, strictly between process() calls.
//         Inside, it would measure the MESSAGE thread's own decode allocations
//         and fail regardless of the processor's RT safety.
//   (ii)  capturedL/capturedR are CLEARED (never shrunk, never reassigned)
//         before each measured scope. ProcessorFixture::renderBlocks APPENDS
//         (seraphis_test_fixture.h:403-406) and re-requests capacity as
//         reserveCapture(capturedL.size() + numBlocks * blockSize) (:391) - it
//         NEVER clears. Without the clear every measured scope re-reserves a
//         larger buffer and the arm reports a FALSE audio-thread allocation
//         attributable entirely to the harness.
//   (iii) A warm-up render precedes the loop, with the capture vectors
//         pre-reserved at exactly one render's worth. The fixture documents this
//         precondition itself (seraphis_test_fixture.h:15-19): its containers
//         grow on demand and are then reused, so "allocation-free" is a property
//         of a WARM fixture only.
//
// The held note is deliberate: a note-free render exercises the idle path, and
// an allocation-free claim about silence is not the claim FR-028 makes. One
// NoteOn is pushed in the warm-up and never released, so voices are live across
// all 42 measured scopes - and each setState therefore lands on a processor that
// is actually sounding, which is the case that has somewhere to allocate.
TEST_CASE("Seraphis_PresetSweep_NoAudioThreadAllocation", "[seraphis][preset][sweep]") {
    const std::vector<SeraphisTest::PresetFile>& files = library();
    INFO("presets root: " << SeraphisTest::factoryPresetRoot().string());
    INFO("presets discovered: " << files.size());
    INFO("blocks per measured scope: " << kSequentialBlocks << " x " << kBlockSamples
                                       << " samples @ " << rateLabel(kRate44100));
    REQUIRE(!files.empty());

    std::vector<std::string> unreadable;
    // Non-const: MemoryStream's two-argument constructor takes a mutable buffer
    // (it BORROWS rather than copies), so borrowing here is what keeps the
    // measured loop free of a per-iteration vector copy.
    std::vector<std::vector<std::uint8_t>> chunks = componentChunks(files, unreadable);
    INFO("presets whose container could not be parsed: " << joinLines(unreadable));
    REQUIRE(unreadable.empty());
    // Only now do chunks[i] and files[i] denote the same preset.
    REQUIRE(chunks.size() == files.size());

    SeraphisTest::ProcessorFixture fx;
    REQUIRE(fx.prepare(kRate44100, static_cast<Steinberg::int32>(kBlockSamples))
            == Steinberg::kResultOk);

    // Precondition (iii). ONE render's worth of capacity, then a warm-up render
    // that both grows the capture vectors to that size and warms every internal
    // container the render path touches.
    fx.reserveCapture(kSequentialBlocks * kBlockSamples);
    fx.renderBlocks(kSequentialBlocks, kBlockSamples,
                    [&fx](std::size_t b, Krate::Test::EventList&,
                          SeraphisTest::ParameterChanges&) {
                        if (b == 0) {
                            fx.pushEvent(Steinberg::Vst::Event::kNoteOnEvent, kDefaultPitch,
                                         kDefaultVelocity, 0);
                        }
                        // No NoteOff, ever: the note is held across all 42
                        // measured scopes.
                    });

    std::vector<std::string> rejected;
    std::vector<std::string> allocating;

    for (std::size_t i = 0; i < chunks.size(); ++i) {
        // Precondition (i): the load happens HERE, outside the scope below, and
        // between two process() calls.
        std::vector<std::uint8_t>& loaded = chunks[i];
        Steinberg::MemoryStream fromFile(loaded.data(),
                                         static_cast<Steinberg::TSize>(loaded.size()));
        if (fx.proc->setState(&fromFile) != Steinberg::kResultOk) {
            rejected.push_back(presetLabel(files[i]));
            continue;
        }

        // Precondition (ii): size -> 0, CAPACITY RETAINED.
        fx.capturedL.clear();
        fx.capturedR.clear();

        // READING FORM (normative in this repo - effects_perf_test.cpp:852-859,
        // ui_perf_test.cpp:501-507): AllocationScope::getAllocationCount()
        // returns a member assigned only in the DESTRUCTOR
        // (allocation_detector.h:117-123), so reading THAT object inside the
        // scope yields a value-initialized 0 and passes unconditionally. The
        // count is taken from the LIVE atomic while tracking is still on, stored
        // in a local, and asserted after the scope has closed - Catch2's REQUIRE
        // is itself an allocating expression and must never run inside.
        std::size_t allocations = 0;
        {
            TestHelpers::AllocationScope scope;
            fx.renderBlocks(kSequentialBlocks, kBlockSamples);
            allocations = TestHelpers::AllocationDetector::instance().getAllocationCount();
        }

        if (allocations != 0u) {
            allocating.push_back(presetLabel(files[i]) + ": " + std::to_string(allocations)
                                 + " allocation(s) over " + std::to_string(kSequentialBlocks)
                                 + " rendered block(s)");
        }
    }

    INFO("presets rejected by Processor::setState: " << joinLines(rejected));
    REQUIRE(rejected.empty());

    INFO("presets that allocated on the render path: " << joinLines(allocating));
    REQUIRE(allocating.empty());

    REQUIRE(fx.checkCanaries());

    // LIVENESS PROBE - a SEPARATE scope, never nested inside a measured one.
    // Nesting is silently wrong in both directions: the inner ctor's
    // startTracking() RESETS the outer count (allocation_detector.h:53-56) and
    // the inner dtor's stopTracking() switches tracking off for the outer scope
    // too (:59-62). Without this probe every zero above would be vacuous on a
    // detector that counts nothing - which is exactly what happens if
    // allocation_operator_overrides.h ever stops being linked into this binary
    // (unit/test_main.cpp:15).
    std::size_t probe = 0;
    {
        TestHelpers::AllocationScope scope;
        // `volatile` is load-bearing: [expr.new]/10 lets a compiler elide an
        // otherwise-unobserved new/delete pair even when the global allocation
        // functions are replaced.
        int* volatile deliberate = new int(7);
        probe = TestHelpers::AllocationDetector::instance().getAllocationCount();
        delete deliberate;
    }
    REQUIRE(probe >= 1u);
}

// =============================================================================
// T018 - SC-015a: a CONCURRENT preset load is RT-safe and correct
// =============================================================================
// CRITERION OWNED BY THIS CASE: FR-028a / SC-015a - a message thread calling
// setState in a loop over all 42 presets WHILE the audio thread renders
// continuously. Three assertions:
//   (a) 0 non-finite samples (bit pattern) and peak <= kPeakBound throughout;
//   (b) 42/42 setState calls return kResultOk, and the post-join getState()
//       memcmp-equals ONE OF the 42 committed chunks;
//   (c) the THREAD-SCOPED allocation count is 0.
//
// (c) IS MEASURED WITH TestHelpers::ThreadScopedAllocationScope, NEVER WITH THE
// UNFILTERED AllocationScope. FR-028a forbids the latter outright: the global
// counter would attribute the MESSAGE thread's own setState decode allocations
// to the audio thread and fail regardless of the processor's RT safety. The
// filter is default-off and restored on scope exit (allocation_detector.h:
// 149-184, T013), so this case cannot leak it into whatever Catch2 runs next.
//
// THE AUDIO THREAD MUST NOT CALL renderBlocks(). A continuous capture-ful loop
// APPENDS forever (seraphis_test_fixture.h:403-406) and would reallocate INSIDE
// the very scope that must report zero, with unbounded memory growth. It calls
// processBlock() (:343-352), which touches neither capturedL nor capturedR, and
// scans fx.audioL()/fx.audioR() (:357-358) per block into plain counters.
// Nothing in the measured loop grows.
//
// NO CATCH2 MACRO EXECUTES INSIDE THE SCOPE, ON EITHER THREAD. Catch2 is not
// thread-safe and REQUIRE allocates; every observation is accumulated into a
// plain variable and asserted after the join. For the same reason the count is
// read from the LIVE atomic as the last statement inside the scope rather than
// from the destructor-latched accessor (the normative reading form - see the
// SC-015 case above).
//
// THE SHIPPED PUSH BEHAVIOUR IS LEFT ON. setState() publishes through
// requestPushAllSurfaces() unless gDisablePresetLoadPush is set
// (processor.cpp:1844); disabling it would remove the very message/audio
// handshake this arm exists to stress.
TEST_CASE("Seraphis_PresetSweep_ConcurrentLoadIsRtSafe", "[seraphis][preset][sweep]") {
    const std::vector<SeraphisTest::PresetFile>& files = library();
    INFO("presets root: " << SeraphisTest::factoryPresetRoot().string());
    INFO("peak bound: " << kPeakBound);
    INFO("audio-thread block ceiling: " << kConcurrentBlocks << " (~"
                                        << (static_cast<double>(kConcurrentBlocks * kBlockSamples)
                                            / kRate44100)
                                        << " s), pacing " << kBlocksBetweenLoads
                                        << " block(s) between loads");
    REQUIRE(!files.empty());

    std::vector<std::string> unreadable;
    std::vector<std::vector<std::uint8_t>> chunks = componentChunks(files, unreadable);
    INFO("presets whose container could not be parsed: " << joinLines(unreadable));
    REQUIRE(unreadable.empty());
    REQUIRE(chunks.size() == files.size());

    SeraphisTest::ProcessorFixture fx;
    REQUIRE(fx.prepare(kRate44100, static_cast<Steinberg::int32>(kBlockSamples))
            == Steinberg::kResultOk);

    // Load preset 0 and start a held note on THIS thread, before any concurrency
    // exists, so the audio thread's very first block is already rendering a
    // sounding voice under a real preset rather than an idle default.
    {
        std::vector<std::uint8_t>& first = chunks.front();
        Steinberg::MemoryStream seed(first.data(), static_cast<Steinberg::TSize>(first.size()));
        REQUIRE(fx.proc->setState(&seed) == Steinberg::kResultOk);
    }
    fx.pushEvent(Steinberg::Vst::Event::kNoteOnEvent, kDefaultPitch, kDefaultVelocity, 0);
    REQUIRE(fx.processBlock(static_cast<Steinberg::int32>(kBlockSamples)) == Steinberg::kResultOk);

    std::atomic<bool> stop{false};
    std::atomic<bool> audioReady{false};
    std::atomic<bool> audioFinished{false};
    std::atomic<std::size_t> blocksDone{0};

    // Written on the audio thread, read after the join - which is the
    // synchronisation point that makes the plain types legal here.
    std::size_t nonFiniteSamples = 0;
    std::size_t overPeakSamples = 0;
    std::size_t blocksOk = 0;
    std::size_t blocksRendered = 0;
    std::size_t allocations = 0;
    std::size_t probe = 0;
    float peakAbs = 0.0f;
    bool canariesIntact = false;

    // THE THREAD ENTRY IS A WRAPPER, AND THE WORK IS THIS LAMBDA.
    // An exception escaping a std::thread's entry function calls std::terminate:
    // the suite dies with no Catch2 failure, no preset named and no assertion
    // count - which is exactly the outcome an RT-safety test must not have. The
    // wrapper below turns any throw into a recorded string that fails the case
    // after join(), the same way every other observation here is accumulated on
    // the audio thread and asserted on the message thread (rule 3).
    // A FIXED BUFFER, not a std::string: assigning a std::string allocates, and
    // an allocation that throws INSIDE the catch handler below would escape the
    // thread entry and call std::terminate - the exact failure this wrapper
    // exists to prevent. std::snprintf into a fixed array cannot throw.
    std::array<char, 256> audioThreadError{};
    const auto audioBody = [&] {
        // FIRST TOUCH of the TLS opt-in, deliberately BEFORE the scope opens:
        // the flag must already be materialised when recordAllocation() reads it
        // from inside operator new (allocation_detector.h:41, :88).
        TestHelpers::tAllocationTrackThisThread = true;
        // MXCSR is FRESH per thread on Windows, so the FTZ/DAZ mode main() set
        // (unit/test_main.cpp:28) does NOT carry over to this thread. Without
        // this call a decaying Aether tail denormals its way to a 100x slowdown.
        enableFTZDAZ();
        // One capture-free warm-up block on THIS thread before anything is
        // measured.
        fx.processBlock(static_cast<Steinberg::int32>(kBlockSamples));

        // Rule 3 in the file banner, on the audio thread: accumulate, never
        // assert. Capturing by reference costs nothing and allocates nothing.
        const auto scanSample = [&](float sample) {
            if (!isFiniteFloat(sample)) {
                ++nonFiniteSamples;
                return;  // std::abs of a NaN is not a peak, and would poison peakAbs
            }
            const float magnitude = std::abs(sample);
            if (magnitude > kPeakBound) {
                ++overPeakSamples;
            }
            peakAbs = std::max(peakAbs, magnitude);
        };

        {
            TestHelpers::ThreadScopedAllocationScope scope;
            // Published from inside the scope so the message thread cannot issue
            // a setState that the instrument is not yet watching.
            audioReady.store(true, std::memory_order_release);

            for (std::size_t b = 0; b < kConcurrentBlocks && !stop.load(std::memory_order_acquire);
                 ++b) {
                if (fx.processBlock(static_cast<Steinberg::int32>(kBlockSamples))
                    == Steinberg::kResultOk) {
                    ++blocksOk;
                }
                ++blocksRendered;

                const float* left = fx.audioL();
                const float* right = fx.audioR();
                for (std::size_t i = 0; i < kBlockSamples; ++i) {
                    scanSample(left[i]);
                    scanSample(right[i]);
                }

                blocksDone.store(b + 1u, std::memory_order_release);
            }

            allocations = TestHelpers::AllocationDetector::instance().getAllocationCount();
        }

        canariesIntact = fx.checkCanaries();
        audioFinished.store(true, std::memory_order_release);

        // LIVENESS PROBE, on THIS thread and in its OWN scope: it proves the
        // thread filter ADMITS the opted-in thread, without which the zero above
        // would be vacuous - a filter that counted nothing anywhere would report
        // exactly the same result.
        {
            TestHelpers::ThreadScopedAllocationScope probeScope;
            int* volatile deliberate = new int(11);
            probe = TestHelpers::AllocationDetector::instance().getAllocationCount();
            delete deliberate;
        }
    };

    std::thread audioThread([&] {
        try {
            audioBody();
        } catch (const std::exception& e) {
            std::snprintf(audioThreadError.data(), audioThreadError.size(), "%s", e.what());
        } catch (...) {
            std::snprintf(audioThreadError.data(), audioThreadError.size(), "%s",
                          "unknown (non-std::exception) throw");
        }
        // Re-published UNCONDITIONALLY: a throw before audioBody reached its own
        // stores would otherwise leave the message thread spinning on a flag
        // that can never be set, turning a test failure into a hang.
        audioReady.store(true, std::memory_order_release);
        audioFinished.store(true, std::memory_order_release);
    });

    // ---- the MESSAGE thread is this one; no Catch2 macro until after join ----
    while (!audioReady.load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }

    std::vector<std::size_t> failedLoads;
    std::size_t pacingTarget = 0;
    for (std::size_t i = 0; i < chunks.size(); ++i) {
        pacingTarget += kBlocksBetweenLoads;
        // Wait for real rendering to happen between loads. The audioFinished
        // check is the deadlock escape: if the audio thread ever stopped early
        // this loop still completes instead of spinning forever.
        while (blocksDone.load(std::memory_order_acquire) < pacingTarget
               && !audioFinished.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }

        std::vector<std::uint8_t>& chunk = chunks[i];
        Steinberg::MemoryStream fromFile(chunk.data(),
                                         static_cast<Steinberg::TSize>(chunk.size()));
        if (fx.proc->setState(&fromFile) != Steinberg::kResultOk) {
            failedLoads.push_back(i);
        }
    }

    stop.store(true, std::memory_order_release);
    audioThread.join();

    // ---- (a0) the audio thread finished by returning, not by throwing ----- //
    // FIRST, before every other clause: a throw would have left blocksRendered,
    // peakAbs and `allocations` partially written, and asserting on those first
    // would report an arbitrary downstream symptom instead of the cause.
    INFO("audio thread threw: " << audioThreadError.data());
    REQUIRE(audioThreadError[0] == '\0');

    // ---- (b) every load succeeded --------------------------------------- //
    {
        std::vector<std::string> failures;
        failures.reserve(failedLoads.size());
        for (const std::size_t i : failedLoads) {
            failures.push_back(presetLabel(files[i]));
        }
        INFO("presets rejected by a concurrent Processor::setState: " << joinLines(failures));
        REQUIRE(failures.empty());
    }

    // Non-vacuity: the audio thread really did render across the loads.
    INFO("blocks rendered concurrently: " << blocksRendered << " of at most " << kConcurrentBlocks);
    REQUIRE(blocksRendered >= chunks.size() * kBlocksBetweenLoads);
    REQUIRE(blocksOk == blocksRendered);

    // ---- (b) the surviving state is one of the 42 ------------------------ //
    Steinberg::MemoryStream saved;
    REQUIRE(fx.proc->getState(&saved) == Steinberg::kResultOk);
    const char* raw = saved.getData();
    const auto savedSize = static_cast<std::size_t>(saved.getSize());
    REQUIRE(raw != nullptr);

    bool matchesACommittedChunk = false;
    for (const auto& chunk : chunks) {
        if (chunk.size() == savedSize && std::memcmp(raw, chunk.data(), savedSize) == 0) {
            matchesACommittedChunk = true;
            break;
        }
    }
    // A SERIALIZED-STATE-STREAM comparison - the explicitly sanctioned carve-out
    // from the no-bit-exact-float-golden rule (spec FR-027a). Nothing here is a
    // render digest: both sides are bytes this same getState()/setState() pair
    // produced, and the claim is only "the processor ended on a state it was
    // actually given".
    INFO("post-join getState() produced " << savedSize << " bytes");
    REQUIRE(matchesACommittedChunk);

    // ---- (a) output stayed finite and bounded ---------------------------- //
    INFO("non-finite samples on the concurrent render: " << nonFiniteSamples);
    REQUIRE(nonFiniteSamples == 0u);

    INFO("samples above " << kPeakBound << ": " << overPeakSamples << ", peak = " << peakAbs);
    REQUIRE(overPeakSamples == 0u);

    REQUIRE(canariesIntact);

    // ---- (c) the audio thread allocated nothing -------------------------- //
    INFO("thread-scoped audio-thread allocations: " << allocations);
    REQUIRE(allocations == 0u);

    // ...and the instrument that reported that zero can count.
    REQUIRE(probe >= 1u);
}

namespace {

// =============================================================================
// T019 - shared machinery for the reproducibility and distinctness arms
// =============================================================================

/// C-10 / OI-4's ABSOLUTE floor: 2 % relative on the strictest of the three shape
/// aggregates. It replaces the plan's first-draft 1e-3, which was a statement
/// about TOOLCHAIN NOISE (100x the reproducibility tolerance) rather than about
/// audible difference - 0.1 % relative is ~0.009 dB on a level metric and is not
/// distinctness by any listening standard.
constexpr double kDistinctnessFloorAbsolute = 0.02;

/// The measured floor is `max(kDistinctnessFloorAbsolute, 0.5 * observedMinimum)`
/// (plan 2.6). `observedMinimum` is a RUN OUTPUT, not a constant: the arm below
/// measures it over all 861 pairs and reports it for compliance.md. If it ever
/// lands below kDistinctnessFloorAbsolute the two presets involved are
/// RE-AUTHORED (T014 section 4's seed spread is the cheapest lever) - the floor is
/// never lowered to accommodate them.
constexpr double kObservedMinimumFactor = 0.5;

/// The denominator floor of the unit-RMS normalisation. Only reachable for a
/// buffer that is already digital silence, which SC-009 has rejected first.
constexpr double kRmsNormalisationFloor = 1.0e-12;

/// The negative control's expected peak ratio: 3 dB down, i.e. 10^(-3/20).
///
/// The twin is authored as `u' = u / sqrt(2)` and kMasterGainId denormalizes
/// LINEARLY (`clamp(u * 2, 0, 2)`, global_params.h:91-96), so the gain ratio the
/// processor actually applies is exactly 1/sqrt(2) = 0.707107 - 0.11 % from the
/// figure below, i.e. comfortably inside the 1 % band and NOT the thing that band
/// exists to catch.
constexpr double kLevelTwinPeakRatio = 0.7079;

/// The pre-check's tolerance, RELATIVE to kLevelTwinPeakRatio. What it exists to
/// catch is the control being INVALID: master gain is applied pre-limiter - the
/// per-output-sample multiply is processor.cpp:2428-2432 and "a post-limiter
/// multiply is FORBIDDEN" sits at :2425, both re-read this session (tasks.md T019
/// cites :2392-2404 / :2397, which is the same block before the file moved). So
/// if the saturator or the true-peak limiter was engaged at the original level the
/// twin is not a pure scaling and the whole control means nothing. On a breach the
/// answer is a preset with more headroom, NEVER a wider band.
constexpr double kLevelTwinRatioTolerance = 0.01;

/// The candidate-selection rule for the negative control, and the ONLY reason it
/// exists: the pre-check above needs a preset whose sustain-window peak has real
/// headroom under the limiter ceiling (kPeakBound ~= 0.9016). 0.5 sits ~5 dB below
/// it, so neither the tape saturator nor the true-peak limiter is anywhere near
/// engagement and the twin is a pure scaling. Selection is FIRST-MATCH over
/// allPresetFiles()'s SORTED order (preset_test_support.h:127-140), so which
/// preset is chosen is reproducible across legs and filesystems, and is reported.
constexpr float kNegativeControlPeakCeiling = 0.5f;

/// `L ++ R`, scaled - the buffer every fingerprint in T019 is taken over.
///
/// The CONCATENATION (rather than an interleave) is what FR-027a specifies, and it
/// matters for `totalVariation`: one channel's samples stay adjacent, so the
/// metric tracks each channel's waveform shape instead of the L-R difference.
/// `scale` is 1.0 for SC-026 (which compares two renders of the SAME preset, where
/// level is signal, not nuisance) and `1 / rms` for the two distinctness arms
/// (where level is exactly the nuisance - rule 7 in the file banner).
[[nodiscard]] std::vector<float> concatenateChannels(std::span<const float> l,
                                                     std::span<const float> r, double scale) {
    std::vector<float> out;
    out.reserve(l.size() + r.size());
    for (const float sample : l) {
        out.push_back(static_cast<float>(static_cast<double>(sample) * scale));
    }
    for (const float sample : r) {
        out.push_back(static_cast<float>(static_cast<double>(sample) * scale));
    }
    return out;
}

/// Largest |sample| across both channels, non-finite samples SKIPPED (rule 4 in
/// the file banner: std::abs of a NaN is not a peak and would poison the result).
[[nodiscard]] float peakOver(std::span<const float> l, std::span<const float> r) {
    float peak = 0.0f;
    const auto scan = [&peak](std::span<const float> channel) {
        for (const float sample : channel) {
            if (!isFiniteFloat(sample)) {
                continue;
            }
            const float magnitude = std::abs(sample);
            peak = std::max(peak, magnitude);
        }
    };
    scan(l);
    scan(r);
    return peak;
}

/// The 32 raw checkpoints' worst absolute difference - REPORTED ONLY, never
/// gated. Rule 7 in the file banner says why: at a 3 s sustain window these are
/// dominated by instantaneous phase, so two near-identical presets differing by a
/// few cents of drift score near-maximum here. Folding that into the gate's max()
/// would make the arm pass unconditionally. It is still the first thing worth
/// looking at when a pair IS flagged, so it rides along in the diagnostics.
[[nodiscard]] double worstCheckpointDelta(const Krate::DSP::TestUtils::RenderFingerprint& a,
                                          const Krate::DSP::TestUtils::RenderFingerprint& b) {
    double worst = 0.0;
    for (std::size_t k = 0; k < Krate::DSP::TestUtils::kRenderCheckpoints; ++k) {
        const double delta = std::abs(static_cast<double>(a.checkpoints[k])
                                      - static_cast<double>(b.checkpoints[k]));
        worst = std::max(worst, delta);
    }
    return worst;
}

/// One preset's unit-RMS-normalised sustain-window fingerprint, plus the raw
/// statistics the negative control's pre-check needs.
struct NormalisedSustain {
    Krate::DSP::TestUtils::RenderFingerprint fingerprint{};
    double rawRms = 0.0;   ///< BEFORE normalisation
    float rawPeak = 0.0f;  ///< BEFORE normalisation - the pre-check's subject
};

[[nodiscard]] NormalisedSustain normaliseSustain(std::span<const float> l,
                                                 std::span<const float> r) {
    NormalisedSustain out;
    out.rawRms = SeraphisTest::rmsOver(l, r, 0, std::max(l.size(), r.size()));
    out.rawPeak = peakOver(l, r);
    const std::vector<float> normalised =
        concatenateChannels(l, r, 1.0 / std::max(out.rawRms, kRmsNormalisationFloor));
    out.fingerprint = Krate::DSP::TestUtils::fingerprintRender(normalised);
    return out;
}

}  // namespace

// =============================================================================
// T019 - SC-026: two renders of one preset agree
// =============================================================================
// CRITERION OWNED BY THIS CASE: FR-027a / SC-026 - for 100 % of presets, two
// renders of [0, H] at 44 100 Hz IN ONE PROCESS, each from a freshly prepared
// Processor driven through the IDENTICAL event schedule, satisfy
// compareFingerprints(a, b).withinTolerance(): worst aggregate-metric relative
// error <= kMetricTolerance = 1e-5 (render_fingerprint.h:52) and worst checkpoint
// error <= kSampleTolerance = 1e-4f (:49), both at :95-97.
//
// NO FLOAT BIT DIGEST, AND NO INTEGER DIGEST DERIVED FROM FLOAT BITS. That is a
// clause of FR-027a, not a stylistic preference, and it is why the comparison is
// aggregate-metric-plus-checkpoint rather than a hash: an FNV over sample bytes
// would be red on the Linux and macOS legs the moment a transcendental's last bit
// moved (roadmap line 664; the ci.yml:162-166 lint enforces it).
//
// TWO RENDERS ARE THE CRITERION, so this is the one arm in the TU that
// deliberately does NOT reuse singleNoteRender()'s cache (rule 1 in the file
// banner). It renders [0, H] rather than [0, Total] because that is what FR-027a
// asks for and because it is the difference between minutes and tens of minutes
// of CI wall clock against SC-027's 6-minute budget.
//
// THE WINDOWS GUARD IS INSIDE THE TEST_CASE, NEVER AROUND IT (the shape T017's
// Aether arm uses), so Catch2's `test cases:` count is identical on all three legs
// and SC-021's delta stays meaningful. Per spec C-9 / CQ-8 Option C this is the
// second of the two heaviest arms tagged out of macOS/Linux.
TEST_CASE("Seraphis_PresetSweep_RendersAreReproducible", "[seraphis][preset][sweep][long]") {
#ifdef _WIN32
    const std::vector<SeraphisTest::PresetFile>& files = library();
    INFO("presets root: " << SeraphisTest::factoryPresetRoot().string());
    INFO("presets discovered: " << files.size());
    INFO("tolerances: worst metric relative error <= "
         << Krate::DSP::TestUtils::kMetricTolerance << ", worst checkpoint error <= "
         << Krate::DSP::TestUtils::kSampleTolerance);
    REQUIRE(!files.empty());

    const std::vector<std::string> unmatched = unmatchedDefinitions(files);
    INFO("presets with no Seraphis::PresetDefs entry: " << joinLines(unmatched));
    REQUIRE(unmatched.empty());

    std::vector<std::string> renderFailures;
    std::vector<std::string> diverged;
    std::size_t compared = 0;

    // The tightest pair in the run, reported unconditionally so the phase has a
    // number rather than a pass/fail bit.
    double worstMetricError = 0.0;
    float worstSampleError = 0.0f;
    std::string worstWhere;

    for (const auto& pf : files) {
        RenderSpec spec;
        spec.sampleRate = kRate44100;
        spec.length = RenderLength::ToHold;  // [0, H], FR-027a's cost bound
        spec.chord = false;
        spec.retainSustain = false;
        spec.retainFull = true;

        // Two SEPARATE renderPreset() calls, hence two separate ProcessorFixtures,
        // each of which constructs its own Seraphis::Processor and prepares it at
        // (44 100, 512) before loading the SAME `comp` chunk.
        const SweepRender first = renderPreset(pf, spec);
        const SweepRender second = renderPreset(pf, spec);
        const std::string where = presetLabel(pf) + " @ " + rateLabel(kRate44100);

        if (!first.failure.empty() || !second.failure.empty()) {
            renderFailures.push_back(where + ": "
                                     + (first.failure.empty() ? second.failure : first.failure));
            continue;
        }
        if (first.fullL.empty() || first.fullR.empty() || second.fullL.empty()
            || second.fullR.empty()) {
            renderFailures.push_back(where + ": a render retained no samples");
            continue;
        }
        if (first.fullL.size() != second.fullL.size()
            || first.fullR.size() != second.fullR.size()) {
            // A LENGTH difference is not a tolerance question - two identical
            // drives that produced different sample counts is a harness fault, and
            // saying so beats letting compareFingerprints average it away.
            renderFailures.push_back(where + ": render lengths differ ("
                                     + std::to_string(first.fullL.size()) + " vs "
                                     + std::to_string(second.fullL.size()) + " samples/channel)");
            continue;
        }
        ++compared;

        const std::vector<float> a = concatenateChannels(first.fullL, first.fullR, 1.0);
        const std::vector<float> b = concatenateChannels(second.fullL, second.fullR, 1.0);
        const Krate::DSP::TestUtils::FingerprintComparison cmp =
            Krate::DSP::TestUtils::compareFingerprints(
                Krate::DSP::TestUtils::fingerprintRender(a),
                Krate::DSP::TestUtils::fingerprintRender(b));

        if (cmp.worstMetricRelativeError > worstMetricError
            || cmp.worstSampleError > worstSampleError) {
            worstMetricError = std::max(worstMetricError, cmp.worstMetricRelativeError);
            worstSampleError = std::max(worstSampleError, cmp.worstSampleError);
            worstWhere = where;
        }

        if (!cmp.withinTolerance()) {
            std::ostringstream os;
            os << where << ": worst metric relative error " << cmp.worstMetricRelativeError
               << " (limit " << Krate::DSP::TestUtils::kMetricTolerance
               << "), worst checkpoint error " << cmp.worstSampleError << " (limit "
               << Krate::DSP::TestUtils::kSampleTolerance << ") - " << cmp.detail;
            diverged.push_back(os.str());
        }
    }

    INFO("presets whose render did not happen: " << joinLines(renderFailures));
    REQUIRE(renderFailures.empty());

    // 100 % of presets, stated as a count rather than inherited from an empty
    // failure list: a loop that compared nothing also produces no failures.
    INFO("presets compared: " << compared << " of " << files.size());
    REQUIRE(compared == files.size());

    {
        std::ostringstream os;
        os << "SC-026 worst over " << compared << " preset(s): metric relative error "
           << worstMetricError << " (limit " << Krate::DSP::TestUtils::kMetricTolerance
           << "), checkpoint error " << worstSampleError << " (limit "
           << Krate::DSP::TestUtils::kSampleTolerance << "), worst held by " << worstWhere;
        WARN(os.str());
    }

    INFO("presets whose two renders disagreed: " << joinLines(diverged));
    REQUIRE(diverged.empty());
#else
    SUCCEED("SC-026 is a Windows-leg-only arm (spec C-9 / CQ-8 Option C - the second of the two"
            " heaviest arms tagged out of macOS/Linux); the single-render arms FR-024..FR-026 run"
            " unaffected on this leg");
#endif
}

// =============================================================================
// T019 - SC-028: no two presets sound the same
// =============================================================================
// CRITERION OWNED BY THIS CASE: FR-027b / SC-028 - all 861 = C(42, 2) unordered
// preset pairs are separated by more than the measured floor, over the SAME
// sustain window Sus = [A + 1, A + 4) SC-009 measures, at 44 100 Hz. Threshold:
// 0 pairs at or below the floor.
//
// NOT ONE EXTRA BLOCK IS RENDERED. The 42 buffers are the ones renderPreset()
// already retained for this arm (RenderSpec::retainSustain, set by
// singleNoteRender at the development rate); the added work is one 1/rms scale
// and one fingerprintRender pass per preset, then O(n^2) over 42 small structs.
//
// THE METRIC IS LEVEL-NORMALISED - see rule 7 in the file banner, and
// SeraphisTest::fingerprintDistance (preset_test_support.h:656-664), which is
// where `rms` and the checkpoints are excluded and why. The unit-RMS assertion
// below is not decoration: it is what proves the normalisation actually happened,
// without which this arm would silently degrade into the amplitude-dominated
// metric OI-4 rejected.
//
// THE FLOOR IS A MEASUREMENT, NOT A GUESS (C-10's measure-then-pin rule):
// kDistinctnessFloor = max(0.02, 0.5 x observedMinimum), with observedMinimum
// reported for transcription into compliance.md. If observedMinimum ever lands at
// or below 0.02 the two presets involved are RE-AUTHORED, never accommodated.
TEST_CASE("Seraphis_PresetSweep_PresetsAreDistinct", "[seraphis][preset][sweep]") {
    const std::vector<SeraphisTest::PresetFile>& files = library();
    INFO("presets root: " << SeraphisTest::factoryPresetRoot().string());
    INFO("presets discovered: " << files.size());
    INFO("metric: max over {peak, meanAbs, totalVariation} of the relative difference between two"
         " UNIT-RMS-NORMALISED sustain-window fingerprints (rms and the 32 raw checkpoints are"
         " excluded - file banner rule 7)");
    REQUIRE(!files.empty());

    const std::vector<std::string> unmatched = unmatchedDefinitions(files);
    INFO("presets with no Seraphis::PresetDefs entry: " << joinLines(unmatched));
    REQUIRE(unmatched.empty());

    std::vector<std::string> renderFailures;
    std::vector<std::string> noWindow;
    std::vector<std::string> notNormalised;
    std::vector<std::string> labels;
    std::vector<Krate::DSP::TestUtils::RenderFingerprint> fingerprints;

    for (const auto& pf : files) {
        const SweepRender& render = singleNoteRender(pf, kRate44100);
        const std::string where = presetLabel(pf) + " @ " + rateLabel(kRate44100);

        if (!render.failure.empty()) {
            renderFailures.push_back(where + ": " + render.failure);
            continue;
        }
        if (render.sustainL.empty() || render.sustainR.empty()) {
            noWindow.push_back(where + ": the sustain window ["
                               + std::to_string(render.timeline.susBegin) + ", "
                               + std::to_string(render.timeline.susEnd)
                               + ") s retained no samples");
            continue;
        }

        const NormalisedSustain normalised = normaliseSustain(render.sustainL, render.sustainR);

        // The normalisation is verified, not assumed: after scaling by 1/rms the
        // fingerprint's own rms is 1 BY CONSTRUCTION, so a value that is not 1 says
        // the scale never reached the buffer and the whole arm is measuring level
        // again.
        if (!(std::abs(normalised.fingerprint.rms - 1.0) <= 1.0e-3)) {
            std::ostringstream os;
            os << where << ": normalised fingerprint rms = " << normalised.fingerprint.rms
               << ", expected 1 (raw rms was " << normalised.rawRms << ")";
            notNormalised.push_back(os.str());
            continue;
        }

        labels.push_back(presetLabel(pf));
        fingerprints.push_back(normalised.fingerprint);
    }

    INFO("presets whose render did not happen: " << joinLines(renderFailures));
    REQUIRE(renderFailures.empty());

    INFO("presets with no retained sustain window: " << joinLines(noWindow));
    REQUIRE(noWindow.empty());

    INFO("presets whose buffer was not unit-RMS after normalisation: " << joinLines(notNormalised));
    REQUIRE(notNormalised.empty());

    REQUIRE(fingerprints.size() == files.size());

    const std::size_t pairCount = fingerprints.size() * (fingerprints.size() - 1u) / 2u;
    INFO("unordered pairs: " << pairCount << " over " << fingerprints.size() << " presets");
    REQUIRE(pairCount == 861u);  // C(42, 2) - C-2 / FR-004's 7 categories x 6

    double observedMinimum = 0.0;
    bool haveMinimum = false;
    std::size_t closestA = 0;
    std::size_t closestB = 0;

    // The WHOLE distribution is retained, not only its minimum: C-10's
    // measure-then-pin rule is recorded in compliance.md as min / median / max,
    // and a section that carries only the minimum cannot say whether the library
    // is broadly spread or merely has one lucky outlier holding the floor up.
    std::vector<double> distances;
    distances.reserve(pairCount);

    for (std::size_t i = 0; i < fingerprints.size(); ++i) {
        for (std::size_t j = i + 1u; j < fingerprints.size(); ++j) {
            const double d = SeraphisTest::fingerprintDistance(fingerprints[i], fingerprints[j]);
            distances.push_back(d);
            if (!haveMinimum || d < observedMinimum) {
                haveMinimum = true;
                observedMinimum = d;
                closestA = i;
                closestB = j;
            }
        }
    }
    REQUIRE(haveMinimum);
    REQUIRE(distances.size() == pairCount);

    std::vector<double> sortedDistances = distances;
    std::sort(sortedDistances.begin(), sortedDistances.end());
    const double medianDistance = sortedDistances[sortedDistances.size() / 2u];
    const double maximumDistance = sortedDistances.back();

    const double distinctnessFloor =
        std::max(kDistinctnessFloorAbsolute, kObservedMinimumFactor * observedMinimum);

    // A SECOND pass rather than one fused loop: the floor is not known until every
    // pair has been seen, so nothing can be judged during the first pass.
    std::size_t belowFloor = 0;
    std::vector<std::string> offenders;
    for (std::size_t i = 0; i < fingerprints.size(); ++i) {
        for (std::size_t j = i + 1u; j < fingerprints.size(); ++j) {
            const double d = SeraphisTest::fingerprintDistance(fingerprints[i], fingerprints[j]);
            if (!(d > distinctnessFloor)) {
                ++belowFloor;
                if (offenders.size() < 8u) {  // enough to diagnose, not a wall of text
                    std::ostringstream os;
                    os << labels[i] << " vs " << labels[j] << ": d = " << d;
                    offenders.push_back(os.str());
                }
            }
        }
    }

    // Unconditional: compliance.md's FR-027b section is transcribed from this
    // line, and a floor recorded without its observed minimum is not a
    // measure-then-pin record.
    {
        std::ostringstream os;
        os << "SC-028 over " << pairCount << " pair(s): observedMinimum = " << observedMinimum
           << " held by " << labels[closestA] << " vs " << labels[closestB]
           << " | median = " << medianDistance << " | max = " << maximumDistance
           << " | pinned floor = max(" << kDistinctnessFloorAbsolute << ", "
           << kObservedMinimumFactor << " x observedMinimum) = " << distinctnessFloor
           << " | closest pair's worst raw checkpoint delta (REPORTED, never gated) = "
           << worstCheckpointDelta(fingerprints[closestA], fingerprints[closestB]);
        WARN(os.str());
    }

    INFO("closest pair: " << labels[closestA] << " vs " << labels[closestB] << ", d = "
                          << observedMinimum << ", floor = " << distinctnessFloor);
    INFO("pairs at or below the floor (first 8): " << joinLines(offenders));
    REQUIRE(belowFloor == 0u);
}

// =============================================================================
// T019 - the INJECTED negative control: a level-only twin is NOT distinct
// =============================================================================
// This case gates nothing about the shipped library. It validates the FLOOR ITSELF
// from the other side, which the arm above structurally cannot do: SC-028 shows
// only that no REAL pair sits under the floor, and a metric that returned a huge
// number for every input whatsoever would satisfy it just as well.
//
// So a near-duplicate is INJECTED: the same preset, with exactly ONE authored
// difference - kMasterGainId (ID 0) 3 dB down. It is timbrally identical by
// construction, and it MUST be reported as non-distinct. The raw-fingerprint
// metric OI-4 rejected scores this twin LARGE and passes; the normalised metric
// scores it ~0.
//
// THE PRE-CHECK IS THE VALIDITY CONDITION, NOT A FORMALITY. Master gain is applied
// pre-limiter (processor.cpp:2428-2432), so the twin is a pure scaling only while
// the saturator and true-peak limiter are out of the picture. If the pre-check
// fails, the control is INVALID and the answer is a preset with more headroom -
// never a weaker check. Candidate selection is therefore explicit about headroom
// (kNegativeControlPeakCeiling) and first-match over the SORTED library, so the
// chosen preset is reproducible and is reported.
//
// The leading `.` in the tag excludes it from the default run: it is a
// measurement, and SC-027's 6-minute budget belongs to the gating arms.
//
// COST NOTE: the ORIGINAL side reuses singleNoteRender()'s cached [0, Total]
// buffers and the twin adds exactly ONE [0, H] render (plan 2.6). Those two
// lengths are identical over the sustain window by construction - the NoteOff sits
// at H = A + 5 s, one full second AFTER the window closes at A + 4 s - so the only
// difference between the two sustain buffers being compared is the authored gain.
TEST_CASE("Seraphis_PresetSweep_DistinctnessNegativeControl", "[.measure][seraphis][preset]") {
    const std::vector<SeraphisTest::PresetFile>& files = library();
    INFO("presets root: " << SeraphisTest::factoryPresetRoot().string());
    INFO("candidate rule: first preset in sorted order whose sustain-window peak is in (0, "
         << kNegativeControlPeakCeiling << "] - i.e. with headroom under the " << kPeakBound
         << " limiter ceiling, so a -3 dB twin is a PURE SCALING");
    REQUIRE(!files.empty());

    const std::vector<std::string> unmatched = unmatchedDefinitions(files);
    INFO("presets with no Seraphis::PresetDefs entry: " << joinLines(unmatched));
    REQUIRE(unmatched.empty());

    // ---- pick the subject ------------------------------------------------- //
    std::vector<std::string> rejectedCandidates;
    std::size_t chosen = files.size();
    NormalisedSustain original;

    for (std::size_t i = 0; i < files.size() && chosen == files.size(); ++i) {
        const SweepRender& render = singleNoteRender(files[i], kRate44100);
        const std::string where = presetLabel(files[i]);

        if (!render.failure.empty()) {
            rejectedCandidates.push_back(where + ": " + render.failure);
            continue;
        }
        if (render.sustainL.empty() || render.sustainR.empty()) {
            rejectedCandidates.push_back(where + ": no retained sustain window");
            continue;
        }

        const NormalisedSustain candidate = normaliseSustain(render.sustainL, render.sustainR);
        if (!(candidate.rawPeak > 0.0f) || candidate.rawPeak > kNegativeControlPeakCeiling) {
            rejectedCandidates.push_back(where + ": sustain peak "
                                         + std::to_string(candidate.rawPeak) + " is outside (0, "
                                         + std::to_string(kNegativeControlPeakCeiling) + "]");
            continue;
        }

        chosen = i;
        original = candidate;
    }

    INFO("candidates passed over: " << joinLines(rejectedCandidates));
    // No headroom anywhere in the library would make this control unconstructible
    // - which is a finding to state, not a reason to relax the pre-check.
    REQUIRE(chosen < files.size());

    const SeraphisTest::PresetFile& subject = files[chosen];
    INFO("negative-control subject: " << presetLabel(subject) << ", sustain peak "
                                      << original.rawPeak << ", sustain rms " << original.rawRms);

    // ---- author the twin: kMasterGainId, and nothing else ----------------- //
    // The subject's OWN normalized master gain is read back from its decoded
    // state rather than assumed: handleGlobalParamChange stores
    // clamp(u * 2, 0, 2) (global_params.h:91-96), so u = linear / 2.
    SeraphisTest::DecodedPresetState decoded;
    std::string why;
    // The decode runs BEFORE the INFO that reports it: Catch2 streams an INFO's
    // message eagerly, so an INFO placed above the call would capture `why` while
    // it is still empty and say nothing on failure.
    const bool decodedOk = SeraphisTest::decodePresetState(subject.comp, decoded, why);
    INFO("decode of " << presetLabel(subject) << ": " << why);
    REQUIRE(decodedOk);

    const double storedLinearGain =
        static_cast<double>(decoded.global.masterGain.load(std::memory_order_relaxed));
    const double storedNormalized = storedLinearGain / 2.0;
    INFO("subject master gain: linear " << storedLinearGain << ", normalized " << storedNormalized);
    REQUIRE(storedNormalized > 0.0);

    const double twinNormalized = storedNormalized / std::numbers::sqrt2;

    RenderSpec spec;
    spec.sampleRate = kRate44100;
    spec.length = RenderLength::ToHold;  // covers Sus = [A+1, A+4); see the cost note
    spec.chord = false;
    spec.retainSustain = true;
    spec.retainFull = false;
    spec.masterGainOverride = twinNormalized;

    const SweepRender twinRender = renderPreset(subject, spec);
    INFO("twin render: " << twinRender.failure);
    REQUIRE(twinRender.failure.empty());
    REQUIRE(!twinRender.sustainL.empty());
    REQUIRE(!twinRender.sustainR.empty());

    const NormalisedSustain twin = normaliseSustain(twinRender.sustainL, twinRender.sustainR);

    // ---- pre-check: the twin really is a PURE SCALING --------------------- //
    const double measuredRatio = static_cast<double>(twin.rawPeak)
                                 / static_cast<double>(original.rawPeak);
    {
        std::ostringstream os;
        os << "negative control on " << presetLabel(subject) << ": original sustain peak "
           << original.rawPeak << ", twin sustain peak " << twin.rawPeak << ", ratio "
           << measuredRatio << " vs the expected " << kLevelTwinPeakRatio << " (+/- "
           << (kLevelTwinRatioTolerance * 100.0) << " %); applied gain ratio is exactly"
              " 1/sqrt(2) = 0.707107";
        WARN(os.str());
    }
    INFO("if this fails the control is INVALID (the limiter or saturator was engaged) - pick a"
         " preset with more headroom, do NOT weaken the check");
    REQUIRE(std::abs(measuredRatio - kLevelTwinPeakRatio)
            <= kLevelTwinRatioTolerance * kLevelTwinPeakRatio);

    // ---- the claim: a level-only twin is NOT distinct --------------------- //
    const double d = SeraphisTest::fingerprintDistance(original.fingerprint, twin.fingerprint);

    // Compared against the ABSOLUTE floor, which is the SMALLEST value
    // kDistinctnessFloor can take (it is defined as max(absolute, 0.5 x
    // observedMinimum)). Passing here therefore implies passing against whatever
    // floor SC-028's run pins, and does not require this case to re-measure the
    // 861 pairs to know that number.
    {
        std::ostringstream os;
        os << "SC-028 negative control: d(original, level-only twin) = " << d
           << ", must sit below the absolute floor " << kDistinctnessFloorAbsolute
           << " (the smallest value kDistinctnessFloor can take). Subject: "
           << presetLabel(subject);
        WARN(os.str());
    }
    INFO("d(original, twin) = " << d << ", absolute floor = " << kDistinctnessFloorAbsolute);
    REQUIRE(d < kDistinctnessFloorAbsolute);
}
