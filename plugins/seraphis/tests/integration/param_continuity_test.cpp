// ==============================================================================
// Seraphis - Parameter continuity tests (Phase 9)
// ==============================================================================
// Reference: specs/seraphis-phase9-parameters/spec.md
//            specs/seraphis-phase9-parameters/plan.md   (§7.0, §7.6, §3.5)
//
// CRITERIA OWNED BY THIS TU (plan §7.0's test-file map):
//   SC-005  no zipper, no click - one automated ID at a time, all others at
//           their registered defaults, measured against the matched-regime
//           per-sample step bound of plan §7.6, including §1.5.3's third edge
//           combination (kBodyResonatorBypassId = on, under which IDs 804 and
//           811 are re-measured)
//
// THIS TU ALSO CARRIES, as the plan §7.0 map states:
//   - kContinuityMechanism[], the full class-(a)/(b) classification of all 101
//     in-scope IDs - the 85 of Phase 9 plan §3.5.3 plus the 16 Phase 10 effects
//     IDs ruled by specs/seraphis-phase10-effects/spec.md FR-038b
//   - the FR-059a probe definition
//
// COMPILE FLAGS: this TU IS listed under "-fno-fast-math
//   -fno-finite-math-only" in plugins/seraphis/tests/CMakeLists.txt - the step
//   statistic must not be reshaped by fast-math contraction.
//
// NO std::isnan / std::isinf / std::numeric_limits<>::infinity() ANYWHERE.
// Clause 4 is a BIT-PATTERN test (isFiniteBits below); the rule holds even on
// this TU, which carries -fno-fast-math, because the helper is the shape the
// rest of the repo uses and a divergent local spelling is how the rule rots.
//
// ==============================================================================
// THE RENDER GEOMETRY, AND WHY IT IS WHAT IT IS
// ==============================================================================
// The criterion asks for three things at once (spec SC-005 clauses 1-3):
//   (i)   64 equal automation steps across a ~2 s render at 48 kHz / block 512;
//   (ii)  a 20 ms test window centred on `step sample + reverb latency`;
//   (iii) ONE reference window PER MEASURED STEP, of the same 20 ms length,
//         from the SAME render, at least 50 ms CLEAR OF ANY STEP.
//
// (iii) is what fixes the step spacing, and it is incompatible with reading (i)
// as "64 steps inside 2 s of wall clock": that spacing is 31.25 ms, so NO point
// in the render is 50 ms clear of a step and the reference could not be drawn at
// all. The spacing is therefore derived from (iii), not assumed:
//
//   step k's output position      = k * kStepSamples + latency
//   reference window k is centred = step k's output position + kStepSamples / 2
//                                   (exactly midway to step k+1)
//   clearance on BOTH sides       = kStepSamples / 2 - kHalfWindow
//
// The clearance is latency-independent (both positions carry the same shift), so
// the minimum whole-block step interval that clears 50 ms on both sides is
// 12 blocks = 6144 samples = 128 ms: it gives 2592 samples = 54.0 ms of
// clearance, where 11 blocks gives 48.7 ms and fails (iii).
//
// The render is therefore 64 steps x 128 ms = 8.192 s of automation plus one
// trailing step interval so step 64's own reference window is inside the render.
// Every other property the criterion names is exact: 64 equal steps, block 512,
// 48 kHz, a 20 ms window per step, the same number of draws on both sides (64),
// and both windows of a pair only 64 ms apart - which is the MATCHED REGIME the
// criterion is named for. A step-free tail drawn at the FINAL parameter value
// would not be matched: for an ID whose sweep moves the output level, the
// reference would sit in a different level regime from the test windows and the
// 1.5 x bound would be measuring the level change, not the discontinuity.
//
// ==============================================================================
// WHY THE POSITIVE CONTROLS ARE A SECOND TEST_CASE, NOT TWO SECTIONs OF THE FIRST
// ==============================================================================
// Catch2 re-runs a TEST_CASE's whole body once per leaf SECTION. Two SECTIONs
// inside the 107-render sweep would render the entire parameter surface three
// times. The controls therefore live in their own TEST_CASE, where they ARE two
// explicitly named SECTIONs, and the sweep runs once.
//
// ==============================================================================
// KNOWN LIMITATION AT THIS POINT IN THE PHASE, recorded rather than hidden
// ==============================================================================
// IDs 100-104 are class (b) and their smoothers ship here, but the macro PUSH
// (MacroParams -> SeraphisMacroMatrix::setMacros) is FR-050's own task and is
// still deferred in processor.cpp's pushMacroSurfaces(). Until it lands those
// five rows exercise clause 4 and the smoothing machinery, but their clause-1-3
// statistic is measured on a signal the five knobs do not yet move. The
// geometry, the bound and the table row are all already correct, so the rows
// gain their teeth with no change here.
// ==============================================================================

#include "controller/controller.h"
#include "plugin_ids.h"
#include "processor/processor.h"
#include "seraphis_test_fixture.h"

#include <krate/dsp/systems/seraphis_engine.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <set>
#include <string>
#include <vector>

// =============================================================================
// FR-059a - the probe SC-005's positive control (b) needs.
//
// processor.h DECLARES `Seraphis::detail::SeraphisParamSmootherBypassProbe` and
// friends it; this TU is the ONLY place in the repository that DEFINES it, so a
// shipping build has no way to call it. Its sole capability is to set
// Processor::paramSmootherBypass_, which makes advanceParamSmoothers() snap
// instead of ramp - a deliberate un-smoothed write.
// =============================================================================
namespace Seraphis::detail {

struct SeraphisParamSmootherBypassProbe {
    static void setBypass(Seraphis::Processor& processor, bool on) noexcept {
        processor.paramSmootherBypass_ = on;
    }
};

}  // namespace Seraphis::detail

namespace {

using Steinberg::int32;
using Steinberg::Vst::ParamID;
using Fixture = SeraphisTest::ProcessorFixture;

// =============================================================================
// kContinuityMechanism[] - plan §3.5.3, checked in
// =============================================================================
// ONE ROW PER IN-SCOPE ID (101: 107 registered, less kSeedId and the five CFG
// IDs), each carrying a MANDATORY file:line citation for the mechanism that
// makes that ID continuous. Rows that share an identical citation in the plan's
// grouped table still get one row each here - the array is per-ID.
//
// THE REMEDY RULE IS ONE-DIRECTIONAL: if SC-005 finds a step on a class-(a) ID,
// that ID moves INTO class (b) - a plugin-side OnePoleSmoother on the pushed
// value, on the same absolute 64-sample grid. It never earns an exemption and
// the 1.5 x bound is never loosened. An ID may not be moved INTO class (a)
// without a file:line citation of the smoother that covers it.
// =============================================================================
struct ContinuityRow {
    /// (a) component-internal - the target component already smooths, ramps,
    /// gates or snapshots the pushed value, and Phase 9 adds nothing.
    /// (b) processor-side - Processor smooths the pushed plain value with a
    /// Krate::DSP::OnePoleSmoother at this row's own `smoothMs` and delivers it on the
    /// engine's absolute 64-sample control-chunk grid.
    enum class Class : std::uint8_t { ComponentInternal, ProcessorSmoothed };
    enum class Evidence : std::uint8_t {
        Smoother,
        Ramp,
        SnapshotAtBirth,
        CoefficientOnly,
        PhaseContinuous,
        Structural
    };

    ParamID id = 0;
    Class cls = Class::ComponentInternal;
    Evidence why = Evidence::Structural;
    const char* citation = nullptr;  // file:line, MANDATORY
    /// FR-059(b) clause 2's PER-ID COLUMN - the second of the two forms that
    /// clause allows ("a single value shared by all class-(b) IDs OR a per-ID
    /// column of kContinuityMechanism[]"). SC-005's measurement forced the second
    /// form: see kAetherDepthSmoothMs' banner in processor.h for the sweep that
    /// did it. 0.0f on a class-(a) row, where the constant has no referent -
    /// asserted, not merely conventional, by the gate below.
    float smoothMs = 0.0f;
};

/// The two shipped class-(b) time constants, mirrored from processor.h so a
/// change there that is not reflected in the table below fails the gate rather
/// than silently invalidating the classification.
constexpr float kBodySmoothMs = Seraphis::kParamSmoothMs;          // 801, 802
constexpr float kDepthSmoothMs = Seraphis::kAetherDepthSmoothMs;   // 100-104, 1215, 1216

constexpr ContinuityRow::Class kA = ContinuityRow::Class::ComponentInternal;
constexpr ContinuityRow::Class kB = ContinuityRow::Class::ProcessorSmoothed;
using Why = ContinuityRow::Evidence;

/// Shared citation for the five macro knobs (IDs 100-104).
constexpr const char* kMacroCitation =
    "plugins/seraphis/src/processor/processor.h macroSm_[0..4] at kParamSmoothMs "
    "= 20 ms; the macro values reach BodyDamping / AetherSizeBreathDepth / "
    "AetherDimensionalityTideDepth, which are themselves class (b), so smoothing "
    "the five knobs covers every macro row uniformly "
    "(dsp/include/krate/dsp/systems/seraphis_macro_matrix.h:180, kRows)";

/// Shared citation for the eleven snapshot-at-birth atmosphere IDs.
constexpr const char* kAtmosBirthCitation =
    "dsp/include/krate/dsp/systems/atmosphere_engine.h:798-834 (\"SNAPSHOTTED at "
    "birth\" / \"Read at birth\"), :850-856, :952-961 - read or snapshotted at "
    "grain birth, so no live grain moves";

/// Shared citation for the five smoothed ContinuousBody controls.
constexpr const char* kBodySmootherCitation =
    "dsp/include/krate/dsp/systems/continuous_body.h:4277-4283 "
    "(keyTrackSmoother_ / mixSmoother_ / cloudMixSmoother_ / cloudSizeSmoother_ "
    "/ widthSmoother_); targets set at :1185, :1215, :1225, :1247, :1270";

/// Shared citation for the fourteen applyControl-routed AetherReverb IDs.
constexpr const char* kAetherApplyControlCitation =
    "dsp/include/krate/dsp/effects/aether_reverb.h:2950-2958 (applyControl = a "
    "clamp plus a smoother target), reached from :2208, :2211, :2214, :2239, "
    ":2244, :2247, :2254, :2280, :2285, :2295, :2301, :2310, :2333, :2336. "
    "1204 is EXCLUDED from this group on purpose: setFreeze (:2230) does not "
    "call applyControl and carries its own Ramp row";

/// Shared citation for the three PLUGIN-OWNED Phase 10 class-(b) rows (1410,
/// 1441, 1443). Unlike every Phase 9 class-(b) row, these three have no
/// component member to point at in EITHER class: the send return gain and the
/// two wander depth multipliers are quantities the plugin itself owns, so the
/// citation names the ruling that mandates the processor-side smoother and the
/// constant it runs at. Both halves are file:line and both are checkable today.
constexpr const char* kFxPluginOwnedCitation =
    "PLUGIN-OWNED - no component smoother exists in EITHER direction, so this "
    "row may not be class (a) (the table's one-directional remedy rule at "
    ":135-140 forbids an uncited class-(a) claim). "
    "specs/seraphis-phase10-effects/spec.md:1143-1151 (FR-038b clause 2) rules "
    "these three ProcessorSmoothed at kParamSmoothMs = 20 ms "
    "(plugins/seraphis/src/processor/processor.h:119), delivered on the "
    "engine's absolute 64-sample control-chunk grid exactly as Phase 9's nine "
    "are. For 1410 that smoother IS the FR-008/FR-009 return-gain ramp - one "
    "smoother, not two (spec.md:1148-1151). PENDING: the processor members "
    "land with the send and wander stages (tasks.md T013 / T017); this row "
    "states the REQUIRED mechanism, and the citation becomes a processor.h "
    "line once the member exists";

constexpr ContinuityRow kContinuityMechanism[] = {
    // --- Global (0-2); kSeedId (3) is EXEMPT from clauses 1-3 ----------------
    {.id = Seraphis::kMasterGainId, .cls = kA, .why = Why::Smoother,
     .citation="plugins/seraphis/src/processor/processor.cpp:509 (masterGain_.configure at "
     "kMasterGainSmoothMs = 20 ms) + :1108 (advanced ONCE PER OUTPUT SAMPLE inside "
     "renderSlice)"},
    {.id = Seraphis::kPolyphonyId, .cls = kA, .why = Why::Smoother,
     .citation="dsp/include/krate/dsp/systems/seraphis_engine.h:455 - setPolyphony's only "
     "level move is sumGain_.setTarget(sumGainForPolyphony(n)) into the "
     "kSumGainSmoothMs smoother (:219-244). That constant is 100 ms, NOT the "
     "20 ms of the master-gain family, and the difference is what makes this row "
     "class (a): the value is read once and HELD for a whole control chunk "
     "(:1079-1080) for SC-014's partition invariance, so it reaches the bus as a "
     "STAIRCASE, and at 20 ms the first stair of a polyphony 1 -> 2 change "
     "(sumGain 1.0 -> 0.7071) was 8.3 % of the bus level in ONE sample - "
     "measured on this very render at 2.651 x against the 1.5 x bound. The "
     "shrink loop at :445-454 is voices_[i].noteOff() ONLY - a musical release, "
     "not a retirement, so the orphan keeps rendering its release envelope"},
    {.id = Seraphis::kSoftLimitId, .cls = kA, .why = Why::Smoother,
     .citation="dsp/include/krate/dsp/processors/tape_saturator.h:248-252 - setOutputSaturation "
     "(seraphis_engine.h:647) forwards to TapeSaturator::setSaturation, whose "
     "post-prepare branch targets saturationSmoother_ rather than snapping. The "
     "prepare-time kDefaultSmoothingMs residual is a prepare artefact, recorded at "
     "plugins/seraphis/src/processor/processor.cpp:486-500"},

    // --- Macros (100-104) - CLASS (b) ----------------------------------------
    {.id = Seraphis::kMacroDreamId, .cls = kB, .why = Why::Smoother, .citation = kMacroCitation, .smoothMs = kDepthSmoothMs},
    {.id = Seraphis::kMacroBloomId, .cls = kB, .why = Why::Smoother, .citation = kMacroCitation, .smoothMs = kDepthSmoothMs},
    {.id = Seraphis::kMacroDissolveId, .cls = kB, .why = Why::Smoother, .citation = kMacroCitation, .smoothMs = kDepthSmoothMs},
    {.id = Seraphis::kMacroGravityId, .cls = kB, .why = Why::Smoother, .citation = kMacroCitation, .smoothMs = kDepthSmoothMs},
    {.id = Seraphis::kMacroEntropyId, .cls = kB, .why = Why::Smoother, .citation = kMacroCitation, .smoothMs = kDepthSmoothMs},

    // --- Harmonic Cloud (200-210) --------------------------------------------
    {.id = Seraphis::kCloudRichnessId, .cls = kA, .why = Why::Smoother,
     .citation="dsp/include/krate/dsp/systems/harmonic_cloud.h:164 - kernel-amplitude "
     "smoother, kAmpSmoothTimeSec = 0.002f"},
    {.id = Seraphis::kCloudInharmonicityId, .cls = kA, .why = Why::PhaseContinuous,
     .citation="dsp/include/krate/dsp/systems/harmonic_cloud.h:191 (kMaxInharmonicity) - a "
     "partial FREQUENCY control; the oscillator bank is phase-continuous, so a "
     "ratio step is not a sample discontinuity"},
    {.id = Seraphis::kCloudTiltId, .cls = kA, .why = Why::Smoother,
     .citation="dsp/include/krate/dsp/systems/harmonic_cloud.h:164 - kernel-amplitude "
     "smoother, kAmpSmoothTimeSec = 0.002f"},
    {.id = Seraphis::kCloudMutationId, .cls = kA, .why = Why::Smoother,
     .citation="dsp/include/krate/dsp/systems/harmonic_cloud.h:164 - kernel-amplitude "
     "smoother, kAmpSmoothTimeSec = 0.002f"},
    {.id = Seraphis::kCloudGravityId, .cls = kA, .why = Why::PhaseContinuous,
     .citation="dsp/include/krate/dsp/systems/harmonic_cloud.h:480-484 - the gravity clamp; "
     "a partial FREQUENCY control on a phase-continuous bank"},
    {.id = Seraphis::kCloudDriftDepthId, .cls = kA, .why = Why::PhaseContinuous,
     .citation="dsp/include/krate/dsp/systems/harmonic_cloud.h:214 (kMaxDriftCents) - a "
     "partial FREQUENCY control on a phase-continuous bank"},
    {.id = Seraphis::kCloudDriftSmoothnessId, .cls = kA, .why = Why::CoefficientOnly,
     .citation="dsp/include/krate/dsp/systems/harmonic_cloud.h:511-516 - setDriftSmoothness "
     "rewrites the detune AR(1) coefficients only and leaves the walk value "
     "untouched"},
    {.id = Seraphis::kCloudStereoSpreadId, .cls = kA, .why = Why::Smoother,
     .citation="dsp/include/krate/dsp/systems/harmonic_cloud.h:164 - the per-partial pan "
     "gains ride the same kernel-amplitude smoother"},
    {.id = Seraphis::kCloudAttackId, .cls = kA, .why = Why::Structural,
     .citation="dsp/include/krate/dsp/systems/harmonic_cloud.h:1605-1616 - an envelope TIME; "
     "\"no stage transition steps the value\", the envelope continues from where "
     "it stands"},
    {.id = Seraphis::kCloudDecayId, .cls = kA, .why = Why::Structural,
     .citation="dsp/include/krate/dsp/systems/harmonic_cloud.h:1605-1616 - an envelope TIME; "
     "the envelope value continues from where it stands"},
    {.id = Seraphis::kCloudEnvOffsetSpreadId, .cls = kA, .why = Why::Structural,
     .citation="dsp/include/krate/dsp/systems/harmonic_cloud.h:1605-1616 - a per-partial "
     "envelope-offset SPREAD, i.e. a time; the envelope value is never re-written"},

    // --- Spectral Morph (400-407); 408-412 are the EXEMPT CFG IDs ------------
    {.id = Seraphis::kMorphEntropyId, .cls = kA, .why = Why::Smoother,
     .citation="dsp/include/krate/dsp/systems/spectral_morph_engine.h:133 "
     "(kMaxAmpDeltaPerChunk = 0.025f) + :174-187 - the entropy amp/cents smoothers "
     "and the per-chunk amplitude bound"},
    {.id = Seraphis::kMorphBloomId, .cls = kA, .why = Why::Smoother,
     .citation="dsp/include/krate/dsp/systems/spectral_morph_engine.h:133 + :174-187 - the "
     "same per-chunk amplitude bound covers the bloom fraction"},
    {.id = Seraphis::kMorphPositionId, .cls = kA, .why = Why::Ramp,
     .citation="dsp/include/krate/dsp/systems/spectral_morph_engine.h:701-725 - advanceTravel "
     "slew-limits the position by travelRate_ * (numStates_ - 1) * dt"},
    {.id = Seraphis::kMorphTravelModeId, .cls = kA, .why = Why::Structural,
     .citation="dsp/include/krate/dsp/systems/spectral_morph_engine.h:345 (setTravelMode) - "
     "rate-domain; no value step"},
    {.id = Seraphis::kMorphTravelRateId, .cls = kA, .why = Why::Structural,
     .citation="dsp/include/krate/dsp/systems/spectral_morph_engine.h:358 (setTravelRate) - "
     "rate-domain; no value step"},
    {.id = Seraphis::kMorphSyncId, .cls = kA, .why = Why::Structural,
     .citation="processor-local: it only re-derives ID 404's pushed rate - "
     "plugins/seraphis/src/processor/processor.cpp:1528 "
     "(Processor::updateSyncedTravelRate)"},
    {.id = Seraphis::kMorphSyncNoteId, .cls = kA, .why = Why::Structural,
     .citation="processor-local: it only re-derives ID 404's pushed rate - "
     "plugins/seraphis/src/processor/processor.cpp:1528 "
     "(Processor::updateSyncedTravelRate)"},
    {.id = Seraphis::kMorphWaypointIntervalId, .cls = kA, .why = Why::Structural,
     .citation="dsp/include/krate/dsp/systems/spectral_morph_engine.h:385 "
     "(setWaypointInterval) - rate-domain; the spline's existing waypoints are not "
     "re-drawn"},

    // --- Life Modulators (600-604) -------------------------------------------
    {.id = Seraphis::kLifeSpatialDepthId, .cls = kA, .why = Why::Smoother,
     .citation="dsp/include/krate/dsp/systems/seraphis_voice.h:378-379 - the orbit output "
     "reaches the audio path ONLY through gainLSm_/gainRSm_, configured at "
     "kSpatialSmoothMs = 20 ms (:160) and advanced per OUTPUT SAMPLE (:1133-1134)"},
    {.id = Seraphis::kLifeSpatialRateId, .cls = kA, .why = Why::Smoother,
     .citation="dsp/include/krate/dsp/systems/seraphis_voice.h:378-379, :1124-1134 - same "
     "gainLSm_/gainRSm_ path"},
    {.id = Seraphis::kLifeSpatialCouplingId, .cls = kA, .why = Why::Smoother,
     .citation="dsp/include/krate/dsp/systems/seraphis_voice.h:378-379, :1124-1134 - same "
     "gainLSm_/gainRSm_ path"},
    {.id = Seraphis::kLifeSpatialGrowthId, .cls = kA, .why = Why::Smoother,
     .citation="dsp/include/krate/dsp/systems/seraphis_voice.h:378-379, :1124-1134 - same "
     "gainLSm_/gainRSm_ path"},
    {.id = Seraphis::kLifeVoiceWidthId, .cls = kA, .why = Why::Smoother,
     .citation="dsp/include/krate/dsp/systems/seraphis_voice.h:1130 (ms_.setWidth(widthPct_)) "
     "-> dsp/include/krate/dsp/processors/midside_processor.h:135 "
     "(widthSmoother_.setTarget) + :188 (advanced per sample)"},

    // --- Voice envelope (700-704) --------------------------------------------
    {.id = Seraphis::kEnvModeId, .cls = kA, .why = Why::Structural,
     .citation="dsp/include/krate/dsp/systems/seraphis_voice.h:567-578 - setEnvelopeMode "
     "rewrites stage TIMES and preserves the shadow; it never re-writes the "
     "envelope value"},
    {.id = Seraphis::kEnvGrowthDurationId, .cls = kA, .why = Why::Structural,
     .citation="dsp/include/krate/dsp/processors/growth_envelope.h:145 (setDuration) - a "
     "duration; the rise continues from where it stands"},
    {.id = Seraphis::kEnvStage0MsId, .cls = kA, .why = Why::Structural,
     .citation="dsp/include/krate/dsp/processors/multi_stage_envelope.h:150 (setStageTime) - "
     "a duration; the increment is recomputed and the current value is never "
     "re-written"},
    {.id = Seraphis::kEnvStage1MsId, .cls = kA, .why = Why::Structural,
     .citation="dsp/include/krate/dsp/processors/multi_stage_envelope.h:150 (setStageTime) - "
     "a duration; the current value is never re-written"},
    {.id = Seraphis::kEnvReleaseMsId, .cls = kA, .why = Why::Structural,
     .citation="dsp/include/krate/dsp/processors/multi_stage_envelope.h:206 (setReleaseTime) "
     "- a duration; the current value is never re-written"},

    // --- Continuous Body (800-812) -------------------------------------------
    {.id = Seraphis::kBodyMaterialId, .cls = kA, .why = Why::Ramp,
     .citation="dsp/include/krate/dsp/systems/continuous_body.h:1122-1157 - setMaterial "
     "self-guards on m == material_ and CROSSFADES"},
    {.id = Seraphis::kBodyResonanceId, .cls = kB, .why = Why::Smoother,
     .citation="resonance_ is stored RAW (dsp/include/krate/dsp/systems/continuous_body.h:"
     "1161-1168, member :4262) and read directly at the control step - it is absent "
     "from the smoother list at :4276-4286. Processor-side: resonanceSm_ at "
     "kParamSmoothMs, the ONE class-(b) VP row. THE SMOOTHER IS RETAINED BUT IS "
     "NOT LOAD-BEARING HERE, and that is recorded rather than hidden: "
     "continuous_body.h:2545-2558 states that a retune steps a decay slope and "
     "an instantaneous frequency and that neither is an output discontinuity, "
     "and the one gain path (engineDriveFor -> slot.driveLog10) rides the 50 ms "
     "kDriveSmoothMs smoother. MEASURED on this render: 1.044 x smoothed and "
     "1.045 x with FR-059a's probe SNAPPING it - which is why positive control "
     "(b) is taken on ID 1215, not on this row", .smoothMs=kBodySmoothMs},
    {.id = Seraphis::kBodyDampingId, .cls = kB, .why = Why::Smoother,
     .citation="damping_ is stored RAW (dsp/include/krate/dsp/systems/continuous_body.h:"
     "1170-1177, member :4263) and is likewise absent from :4276-4286. MB-routed, "
     "so the settling push is setTargetBase. Processor-side: bodyDampingSm_ (also "
     "measured flat at 1.167 x both smoothed and snapped)", .smoothMs=kBodySmoothMs},
    {.id = Seraphis::kBodyKeyTrackingId, .cls = kA, .why = Why::Smoother, .citation = kBodySmootherCitation},
    {.id = Seraphis::kBodyDriveId, .cls = kA, .why = Why::Smoother,
     .citation="TWO consumers, both cited. (i) engineDriveFor() -> slot.driveLog10.setTarget, "
     "the 50 ms kDriveSmoothMs log-domain smoother "
     "(dsp/include/krate/dsp/systems/continuous_body.h:169, :1740). (ii) "
     "cloudDriveGain() (:3297) = rmsGain_ * userDrive_, UNSMOOTHED, applied per "
     "sample at :3476 as bypassGain * cloudDrive * mono[s] - and bypassGain is "
     "EXACTLY 0 while no bypass is engaged (:3406-3411, :3459-3461), i.e. at ID "
     "812's registered default. Consumer (ii) is what the kBodyResonatorBypassId = "
     "on edge combination measures"},
    {.id = Seraphis::kBodyMixId, .cls = kA, .why = Why::Smoother, .citation = kBodySmootherCitation},
    {.id = Seraphis::kBodyCloudMixId, .cls = kA, .why = Why::Smoother, .citation = kBodySmootherCitation},
    {.id = Seraphis::kBodyCloudDecayId, .cls = kA, .why = Why::Smoother,
     .citation="the derived feedback gain rides fbLSmoother_/fbRSmoother_ "
     "(dsp/include/krate/dsp/systems/continuous_body.h:4285-4286, targets "
     ":1806-1807); setCloudDecaySec is :1230"},
    {.id = Seraphis::kBodyCloudSizeId, .cls = kA, .why = Why::Smoother, .citation = kBodySmootherCitation},
    {.id = Seraphis::kBodyCloudDampingId, .cls = kA, .why = Why::Smoother,
     .citation="cloudDampLog2Smoother_ (dsp/include/krate/dsp/systems/continuous_body.h:4282, "
     "target :1815); setCloudDamping is :1254"},
    {.id = Seraphis::kBodyWidthId, .cls = kA, .why = Why::Smoother, .citation = kBodySmootherCitation},
    {.id = Seraphis::kBodyInputAgcId, .cls = kA, .why = Why::Smoother,
     .citation="\"Absorbed by the drive smoother, so toggling is clickless\" "
     "(dsp/include/krate/dsp/systems/continuous_body.h:1274-1276) - THAT CITATION "
     "COVERS THE ENGINE PATH ONLY. agcEnabled_ selects rmsGain_ (member :4291), "
     "which feeds the same two consumers as ID 804: the driveLog10 smoother AND the "
     "unsmoothed cloudDriveGain() (:3297). Measured under the "
     "kBodyResonatorBypassId = on edge combination for that reason"},
    {.id = Seraphis::kBodyResonatorBypassId, .cls = kA, .why = Why::Ramp,
     .citation="dsp/include/krate/dsp/systems/continuous_body.h:1300-1322 - its own 10 ms "
     "equal-power ramp at the control step, plus the documented un-bypass "
     "waveguide re-tune"},

    // --- Granular Atmosphere (1000-1016) -------------------------------------
    {.id = Seraphis::kAtmosLevelId, .cls = kA, .why = Why::Smoother,
     .citation="levelSmoother_ - dsp/include/krate/dsp/systems/atmosphere_engine.h:509 "
     "(configure), :948 (target), :2233 (advanced per sample)"},
    {.id = Seraphis::kAtmosBlurId, .cls = kA, .why = Why::Smoother,
     .citation="blurSmoother_ - dsp/include/krate/dsp/systems/atmosphere_engine.h:510 "
     "(configure), :876 (target), :2045-2046 (advanced once per STFT hop)"},
    {.id = Seraphis::kAtmosDensityId, .cls = kA, .why = Why::SnapshotAtBirth, .citation = kAtmosBirthCitation},
    {.id = Seraphis::kAtmosGrainSecondsId, .cls = kA, .why = Why::SnapshotAtBirth, .citation = kAtmosBirthCitation},
    {.id = Seraphis::kAtmosDriftDepthId, .cls = kA, .why = Why::PhaseContinuous,
     .citation="driftLanes_.depth is live over the whole bank "
     "(dsp/include/krate/dsp/systems/atmosphere_engine.h:838, :1338, :1378) but it "
     "scales a PITCH; the grain oscillators are phase-continuous"},
    {.id = Seraphis::kAtmosPanSpreadId, .cls = kA, .why = Why::SnapshotAtBirth, .citation = kAtmosBirthCitation},
    {.id = Seraphis::kAtmosDecorrelationId, .cls = kA, .why = Why::SnapshotAtBirth, .citation = kAtmosBirthCitation},
    {.id = Seraphis::kAtmosFreezeMixId, .cls = kA, .why = Why::Ramp,
     .citation="freezeMixRamp_.setTarget(...), a LinearRamp - "
     "dsp/include/krate/dsp/systems/atmosphere_engine.h:884"},
    {.id = Seraphis::kAtmosFreezeId, .cls = kA, .why = Why::Ramp,
     .citation="engine-level latch; the release path is a one-hop fade arm - "
     "dsp/include/krate/dsp/systems/seraphis_engine.h:632-643"},
    {.id = Seraphis::kAtmosDriftSmoothnessId, .cls = kA, .why = Why::CoefficientOnly,
     .citation="dsp/include/krate/dsp/systems/atmosphere_engine.h:844-847 - "
     "updateDriftCoefficients() only; the walk value is untouched"},
    {.id = Seraphis::kAtmosDriftRangeId, .cls = kA, .why = Why::SnapshotAtBirth, .citation = kAtmosBirthCitation},
    {.id = Seraphis::kAtmosJitterId, .cls = kA, .why = Why::SnapshotAtBirth, .citation = kAtmosBirthCitation},
    {.id = Seraphis::kAtmosPositionId, .cls = kA, .why = Why::SnapshotAtBirth, .citation = kAtmosBirthCitation},
    {.id = Seraphis::kAtmosPositionSpreadId, .cls = kA, .why = Why::SnapshotAtBirth, .citation = kAtmosBirthCitation},
    {.id = Seraphis::kAtmosPitchId, .cls = kA, .why = Why::SnapshotAtBirth, .citation = kAtmosBirthCitation},
    {.id = Seraphis::kAtmosPitchSpreadId, .cls = kA, .why = Why::SnapshotAtBirth, .citation = kAtmosBirthCitation},
    {.id = Seraphis::kAtmosGrainEnvelopeId, .cls = kA, .why = Why::SnapshotAtBirth, .citation = kAtmosBirthCitation},

    // --- Aether Space (1200-1217) --------------------------------------------
    {.id = Seraphis::kAetherMixId, .cls = kA, .why = Why::Smoother, .citation = kAetherApplyControlCitation},
    {.id = Seraphis::kAetherSizeId, .cls = kA, .why = Why::Smoother, .citation = kAetherApplyControlCitation},
    {.id = Seraphis::kAetherDensityId, .cls = kA, .why = Why::Smoother, .citation = kAetherApplyControlCitation},
    {.id = Seraphis::kAetherDecayId, .cls = kA, .why = Why::Smoother, .citation = kAetherApplyControlCitation},
    {.id = Seraphis::kAetherFreezeId, .cls = kA, .why = Why::Ramp,
     .citation="dsp/include/krate/dsp/effects/aether_reverb.h:2230-2236 - setFreeze is a "
     "SELF-GUARDING latch onto the kFreezeLatchMs = 50 ms freezeRamp_ (:1388, "
     ":1928). It does NOT call applyControl"},
    {.id = Seraphis::kAetherDimensionalityId, .cls = kA, .why = Why::Smoother, .citation = kAetherApplyControlCitation},
    {.id = Seraphis::kAetherDampingId, .cls = kA, .why = Why::Smoother, .citation = kAetherApplyControlCitation},
    {.id = Seraphis::kAetherPreDelayId, .cls = kA, .why = Why::Smoother, .citation = kAetherApplyControlCitation},
    {.id = Seraphis::kAetherModDepthId, .cls = kA, .why = Why::Smoother, .citation = kAetherApplyControlCitation},
    {.id = Seraphis::kAetherModSmoothnessId, .cls = kA, .why = Why::CoefficientOnly,
     .citation="dsp/include/krate/dsp/effects/aether_reverb.h:2268-2273 - forwards to "
     "BrownianDrift::setSmoothness on 8 channels; it rewrites tau and leaves the "
     "walk value untouched"},
    {.id = Seraphis::kAetherShimmerOctaveId, .cls = kA, .why = Why::Smoother, .citation = kAetherApplyControlCitation},
    {.id = Seraphis::kAetherShimmerFifthId, .cls = kA, .why = Why::Smoother, .citation = kAetherApplyControlCitation},
    {.id = Seraphis::kAetherBloomSendId, .cls = kA, .why = Why::Smoother, .citation = kAetherApplyControlCitation},
    {.id = Seraphis::kAetherBloomDecayId, .cls = kA, .why = Why::Smoother, .citation = kAetherApplyControlCitation},
    {.id = Seraphis::kAetherSpectralDiffusionId, .cls = kA, .why = Why::Smoother, .citation = kAetherApplyControlCitation},
    {.id = Seraphis::kAetherSizeBreathDepthId, .cls = kB, .why = Why::Smoother,
     .citation="sizeBreathDepth_ is a DIRECT unsmoothed member store "
     "(dsp/include/krate/dsp/effects/aether_reverb.h:2318-2322) scaling a live "
     "[-1,+1] modulator added to Size BEFORE the S(v) mapping - a depth step is a "
     "delay-length step. MB-routed. Processor-side: breathDepthSm_ at "
     "kAetherDepthSmoothMs = AetherReverb::kSizeSmoothingMs (:2731), the "
     "component's own smoothing time for the quantity this depth modulates. THE "
     "ONE ROW SC-005 MEASURED A SURVIVING STEP ON: 1.817 x at 20 ms, 1.126 x at "
     "300 ms, and identical at 500 ms - the knee, not a fitted number. It is "
     "therefore also positive control (b)'s subject", .smoothMs=kDepthSmoothMs},
    {.id = Seraphis::kAetherTideDepthId, .cls = kB, .why = Why::Smoother,
     .citation="tideDepth_ is a DIRECT unsmoothed member store "
     "(dsp/include/krate/dsp/effects/aether_reverb.h:2326-2330) scaling a live "
     "[-1,+1] modulator added to Dimensionality before the [0,1] clamp. MB-routed. "
     "Processor-side: tideDepthSm_, on kAetherDepthSmoothMs with its sibling "
     "1215 (Dimensionality is a coefficient, not a read length, so this row "
     "measures 1.091 x at every time constant tried)", .smoothMs=kDepthSmoothMs},
    {.id = Seraphis::kAetherWidthId, .cls = kA, .why = Why::Smoother, .citation = kAetherApplyControlCitation},

    // --- Integrated Effects (1400-1443) - Phase 10, spec FR-038b -------------
    // 13 class (a) + 3 class (b). NO effects ID is exempt: the exemption exists
    // for kSeedId and the five CFG IDs, whose bound is unsatisfiable by
    // construction, and no Phase 10 ID is in that position.
    {.id = Seraphis::kFxSaturationId, .cls = kA, .why = Why::Smoother,
     .citation="dsp/include/krate/dsp/systems/seraphis_engine.h:670-676 "
     "(setOutputSaturation) forwards to TapeSaturator::setSaturation "
     "(dsp/include/krate/dsp/processors/tape_saturator.h:248-253), whose "
     "post-prepare branch targets saturationSmoother_ rather than snapping. "
     "That smoother is configured at kDefaultSmoothingMs = 5 ms (:88, :160) and "
     "read per OUTPUT SAMPLE at :355. Same mechanism as ID 2, which shares the "
     "one writer FR-021/D-2 rules the saturation amount has"},
    {.id = Seraphis::kFxDelayMixId, .cls = kB, .why = Why::Smoother, .citation = kFxPluginOwnedCitation, .smoothMs = kBodySmoothMs},
    {.id = Seraphis::kFxDelayTimeId, .cls = kA, .why = Why::Smoother,
     .citation="dsp/include/krate/dsp/effects/spectral_delay.h:426-428 (setBaseDelayMs -> "
     "baseDelaySmoother_.setTarget at :427), read once per spectral frame at "
     ":646"},
    {.id = Seraphis::kFxDelaySpreadId, .cls = kA, .why = Why::Smoother,
     .citation="dsp/include/krate/dsp/effects/spectral_delay.h:433-435 (setSpreadMs -> "
     "spreadSmoother_.setTarget at :434), read at :647"},
    {.id = Seraphis::kFxDelaySpreadDirectionId, .cls = kA, .why = Why::Structural,
     .citation="dsp/include/krate/dsp/effects/spectral_delay.h:439-441 - "
     "setSpreadDirection is a PLAIN ASSIGNMENT and the enum is read ONLY inside "
     "calculateBinDelayMs' switch (:587-597), so the change re-maps per-bin "
     "delay OFFSETS; continuity comes from the interpolated per-bin reads and "
     "the FFT overlap-add. NO SMOOTHER EXISTS AND NONE IS CITED"},
    {.id = Seraphis::kFxDelayFeedbackId, .cls = kA, .why = Why::Smoother,
     .citation="dsp/include/krate/dsp/effects/spectral_delay.h:461-463 (setFeedback -> "
     "feedbackSmoother_.setTarget at :462), read at :648"},
    {.id = Seraphis::kFxDelayTiltId, .cls = kA, .why = Why::Smoother,
     .citation="dsp/include/krate/dsp/effects/spectral_delay.h:469-471 (setFeedbackTilt "
     "-> tiltSmoother_.setTarget at :470), read at :649"},
    {.id = Seraphis::kFxDelayDiffusionId, .cls = kA, .why = Why::Smoother,
     .citation="dsp/include/krate/dsp/effects/spectral_delay.h:490-492 (setDiffusion -> "
     "diffusionSmoother_.setTarget at :491), read at :650"},
    {.id = Seraphis::kFxDelayWidthId, .cls = kA, .why = Why::Smoother,
     .citation="dsp/include/krate/dsp/effects/spectral_delay.h:513-515 (setStereoWidth -> "
     "stereoWidthSmoother_.setTarget at :514)"},
    {.id = Seraphis::kFxDelaySyncId, .cls = kA, .why = Why::Smoother,
     .citation="dsp/include/krate/dsp/effects/spectral_delay.h:524-527 - setTimeMode is a "
     "plain assignment, but the mode is consumed inside process(), which pushes "
     "the synced time through the SAME baseDelaySmoother_.setTarget(syncedMs) "
     "(:322-336) that ID 1411 uses, read at :646"},
    {.id = Seraphis::kFxDelaySyncNoteId, .cls = kA, .why = Why::Smoother,
     .citation="dsp/include/krate/dsp/effects/spectral_delay.h:532-535 - setNoteValue is "
     "a plain assignment; same path as 1418 (:330, :336, :646)"},
    {.id = Seraphis::kFxSpectralFreezeId, .cls = kA, .why = Why::Ramp,
     .citation="dsp/include/krate/dsp/effects/spectral_delay.h:479-481 - "
     "setFreezeEnabled is a plain assignment, but engagement CROSSFADES over "
     "kFreezeCrossfadeTimeMs = 75 ms (:906); the per-frame increment is derived "
     "at :210-212 and applied at :692-696. Smoother is the WRONG evidence here "
     "- there is no OnePoleSmoother on this path"},
    {.id = Seraphis::kFxWidthId, .cls = kA, .why = Why::Smoother,
     .citation="dsp/include/krate/dsp/processors/midside_processor.h:133-136 (setWidth -> "
     "widthSmoother_.setTarget at :135), advanced per SAMPLE inside process "
     "(:183-188). FR-010a: the smoother does not advance while the wander stage "
     "is skipped, which is why the skip itself is a stage-level decision and not "
     "a parameter one"},
    {.id = Seraphis::kFxWanderDepthId, .cls = kB, .why = Why::Smoother, .citation = kFxPluginOwnedCitation, .smoothMs = kBodySmoothMs},
    {.id = Seraphis::kFxWanderRateId, .cls = kA, .why = Why::CoefficientOnly,
     .citation="dsp/include/krate/dsp/processors/brownian_drift.h:152-155 - "
     "BrownianDrift::setSmoothness clamps and calls updateCoefficients(); it "
     "retunes the walk's correlation time and never steps the walk's output"},
    {.id = Seraphis::kFxAzimuthDepthId, .cls = kB, .why = Why::Smoother, .citation = kFxPluginOwnedCitation, .smoothMs = kBodySmoothMs},
};

/// The row count is an ASSERTION, not a claim in prose. An earlier revision of
/// the plan said "85 rows" over a table that enumerated 83 - IDs 1 and 2 had no
/// row, no class and no evidence, and ID 1 is the most plausible discontinuity
/// in the set.
static_assert(std::size(kContinuityMechanism) == 101,
              "SC-005: 107 registered, less kSeedId and the five CFG IDs");

/// Exempt from clauses 1-3; clause 4 still applies to all six.
///  - kSeedId: setSeed is documented configuration-time on BOTH consumers and
///    redraws all 64 scatter offsets in one chunk. A continuity bound is
///    unsatisfiable by construction; SC-020 asserts its audible effect instead.
///  - 408-412 (CFG): the configure-time gate exists precisely because mid-note
///    application is undefined - a sounding-voice push is REJECTED, so there is
///    nothing to be continuous about. SC-013 and SC-003's CFG clause cover them.
constexpr ParamID kExemptIds[] = {
    Seraphis::kSeedId,        Seraphis::kMorphStateCountId, Seraphis::kMorphState0Id,
    Seraphis::kMorphState1Id, Seraphis::kMorphState2Id,     Seraphis::kMorphState3Id,
};

/// Phase 9 plan §3.5.3: "Class (b) is exactly nine IDs." Phase 10's FR-038b
/// clause 2 adds three more - 1410, 1441, 1443, the plugin-owned quantities
/// with no component smoother in either direction.
constexpr ParamID kClassBIds[] = {
    Seraphis::kMacroDreamId,        Seraphis::kMacroBloomId,
    Seraphis::kMacroDissolveId,     Seraphis::kMacroGravityId,
    Seraphis::kMacroEntropyId,      Seraphis::kBodyResonanceId,
    Seraphis::kBodyDampingId,       Seraphis::kAetherSizeBreathDepthId,
    Seraphis::kAetherTideDepthId,   Seraphis::kFxDelayMixId,
    Seraphis::kFxWanderDepthId,     Seraphis::kFxAzimuthDepthId,
};
static_assert(std::size(kClassBIds) == 12,
              "plan 3.5.3's nine, of plain span D = 1.0, plus FR-038b clause 2's "
              "three: kFxDelayMixId, kFxWanderDepthId, kFxAzimuthDepthId");

// =============================================================================
// Render geometry (see the banner)
// =============================================================================
constexpr double kSampleRate = 48000.0;
constexpr int32 kBlock = 512;
constexpr std::size_t kBlockSamples = 512;

/// 64 equal automation steps, extreme to extreme.
constexpr std::size_t kNumSteps = 64;
/// The minimum whole-block step interval that clears 50 ms on BOTH sides of a
/// midway reference window (see the banner's arithmetic).
constexpr std::size_t kBlocksPerStep = 12;
constexpr std::size_t kStepSamples = kBlocksPerStep * kBlockSamples;  // 6144 = 128 ms
/// One trailing interval so step 64's own reference window is inside the render.
constexpr std::size_t kTotalBlocks = (kNumSteps + 1u) * kBlocksPerStep;  // 780
constexpr std::size_t kTotalSamples = kTotalBlocks * kBlockSamples;      // 399 360

constexpr std::size_t kHalfWindow = 480;                      // 10 ms at 48 kHz
constexpr std::size_t kWindowSamples = 2u * kHalfWindow;       // 20 ms
constexpr std::size_t kFiftyMsSamples = 2400;

static_assert(kStepSamples / 2u - kHalfWindow >= kFiftyMsSamples,
              "SC-005 clause 2: the reference window must be >= 50 ms clear of the "
              "step it follows");
static_assert(kStepSamples - (kStepSamples / 2u + kHalfWindow) >= kFiftyMsSamples,
              "SC-005 clause 2: the reference window must be >= 50 ms clear of the "
              "step that follows it");

/// SC-005 clause 3.
constexpr double kBoundFactor = 1.5;

constexpr Steinberg::int16 kTestNote = 60;  // C4, held for the whole render

// =============================================================================
// Helpers
// =============================================================================

/// Clause 4's finiteness test. BIT PATTERN, never std::isnan/std::isinf: the
/// macOS leg builds with -ffast-math, under which the compiler may assume finite
/// values and fold such a test away.
[[nodiscard]] bool isFiniteBits(float v) noexcept {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &v, sizeof(bits));
    return (bits & 0x7F800000u) != 0x7F800000u;
}

[[nodiscard]] bool allFinite(const std::vector<float>& x) noexcept {
    return std::ranges::all_of(x, [](float v) { return isFiniteBits(v); });
}

/// max |x[i] - x[i-1]| over [first, first + count), in double so the statistic
/// is not itself quantised by the float format it measures.
[[nodiscard]] double maxPerSampleDelta(const std::vector<float>& x, std::size_t first,
                                       std::size_t count) {
    double worst = 0.0;
    const std::size_t begin = (first == 0u) ? 1u : first;
    const std::size_t end = std::min(first + count, x.size());
    for (std::size_t i = begin; i < end; ++i) {
        const double d = static_cast<double>(x[i]) - static_cast<double>(x[i - 1u]);
        const double a = (d < 0.0) ? -d : d;
        worst = std::max(a, worst);
    }
    return worst;
}

struct Render {
    std::vector<float> left, right;
    std::size_t latencySamples = 0;
};

struct ParamPoint {
    ParamID id;
    double normalized;
};

struct Stats {
    double maxTest = 0.0;
    double maxRef = 0.0;
    /// Where the LARGEST reference statistic came from - positive control (a)
    /// injects into exactly that window, so 2 x its own maximum is guaranteed to
    /// exceed 1.5 x max(reference).
    std::size_t worstRefStart = 0;
    int worstRefChannel = 0;
};

/// One SC-005 render: the automated ID stepped extreme-to-extreme in 64 equal
/// steps on the 128 ms grid, every other ID left at its registered default apart
/// from `pinned` (the edge combinations), with one note held throughout.
[[nodiscard]] Render renderAutomation(ParamID automated, const std::vector<ParamPoint>& pinned,
                                      bool bypassSmoothers) {
    auto fx = std::make_unique<Fixture>();
    REQUIRE(fx->prepare(kSampleRate, kBlock) == Steinberg::kResultOk);

    // FR-059a. The ONLY caller of the probe in the repository.
    if (bypassSmoothers) {
        Seraphis::detail::SeraphisParamSmootherBypassProbe::setBypass(*fx->proc, true);
    }

    Render out;
    out.latencySamples = static_cast<std::size_t>(fx->proc->getLatencySamples());
    fx->reserveCapture(kTotalSamples);

    fx->renderBlocks(
        kTotalBlocks, kBlockSamples,
        [&](std::size_t b, Krate::Test::EventList&, SeraphisTest::ParameterChanges& pc) {
            if (b == 0u) {
                for (const ParamPoint& p : pinned) {
                    pc.addQueue(p.id).addTestPoint(0, p.normalized);
                }
                // The LOW extreme, delivered before the first measured step.
                pc.addQueue(automated).addTestPoint(0, 0.0);
                fx->pushEvent(Steinberg::Vst::Event::kNoteOnEvent, kTestNote, 0.8f, 0);
                return;
            }
            if (b % kBlocksPerStep != 0u) {
                return;
            }
            const std::size_t step = b / kBlocksPerStep;  // 1 ... kNumSteps
            if (step > kNumSteps) {
                return;
            }
            const double value =
                static_cast<double>(step) / static_cast<double>(kNumSteps);  // -> 1.0
            pc.addQueue(automated).addTestPoint(0, value);
        });

    out.left = std::move(fx->capturedL);
    out.right = std::move(fx->capturedR);
    return out;
}

[[nodiscard]] std::size_t testWindowStart(std::size_t step, std::size_t latency) {
    return step * kStepSamples + latency - kHalfWindow;
}

[[nodiscard]] std::size_t refWindowStart(std::size_t step, std::size_t latency) {
    return step * kStepSamples + latency + (kStepSamples / 2u) - kHalfWindow;
}

[[nodiscard]] Stats measure(const Render& r) {
    Stats s{};
    for (std::size_t k = 1; k <= kNumSteps; ++k) {
        const std::size_t t0 = testWindowStart(k, r.latencySamples);
        const std::size_t q0 = refWindowStart(k, r.latencySamples);

        const double tl = maxPerSampleDelta(r.left, t0, kWindowSamples);
        const double tr = maxPerSampleDelta(r.right, t0, kWindowSamples);
        s.maxTest = std::max({s.maxTest, tl, tr});

        const double ql = maxPerSampleDelta(r.left, q0, kWindowSamples);
        const double qr = maxPerSampleDelta(r.right, q0, kWindowSamples);
        if (ql > s.maxRef) {
            s.maxRef = ql;
            s.worstRefStart = q0;
            s.worstRefChannel = 0;
        }
        if (qr > s.maxRef) {
            s.maxRef = qr;
            s.worstRefStart = q0;
            s.worstRefChannel = 1;
        }
    }
    return s;
}

/// Clauses 1-4 for one render. `label` names the row in a failure message.
void checkContinuity(const Render& r, const std::string& label, bool inScope) {
    INFO(label);
    REQUIRE(r.left.size() == kTotalSamples);
    REQUIRE(r.right.size() == kTotalSamples);

    // Clause 4 - ALL 107 IDs, no exemptions.
    CHECK(allFinite(r.left));
    CHECK(allFinite(r.right));

    if (!inScope) {
        return;
    }

    const Stats s = measure(r);
    // Non-vacuity: a bound of 0 would pass for a silent render.
    REQUIRE(s.maxRef > 0.0);
    INFO("max test statistic " << s.maxTest << " against bound "
                              << (kBoundFactor * s.maxRef) << " (1.5 x max reference "
                              << s.maxRef << ")");
    CHECK(s.maxTest <= kBoundFactor * s.maxRef);
}

}  // namespace

// =============================================================================
// The table's own gates (no render - cheap, and it runs first)
// =============================================================================

TEST_CASE("Seraphis_ContinuityMechanism_CoversEveryInScopeId",
          "[seraphis][params][continuity]") {
    Seraphis::Controller controller;
    REQUIRE(controller.initialize(nullptr) == Steinberg::kResultOk);

    std::set<ParamID> registered;
    const int32 count = controller.getParameterCount();
    REQUIRE(count == 107);
    for (int32 i = 0; i < count; ++i) {
        Steinberg::Vst::ParameterInfo info{};
        REQUIRE(controller.getParameterInfo(i, info) == Steinberg::kResultOk);
        INFO("duplicate registration of parameter ID " << info.id);
        CHECK(registered.insert(info.id).second);
    }

    // --- the table's own set, with no ID twice -------------------------------
    std::set<ParamID> table;
    for (const ContinuityRow& row : kContinuityMechanism) {
        INFO("duplicate row for parameter ID " << row.id << " in kContinuityMechanism[]");
        CHECK(table.insert(row.id).second);
        // EVERY row carries a file:line citation. A row with no evidence is a
        // classification asserted from memory, which is what this gate exists to
        // stop - IDs 1 and 2 were missing entirely from an earlier revision.
        INFO("parameter ID " << row.id << " has no file:line citation");
        REQUIRE(row.citation != nullptr);
        const std::string citation(row.citation);
        CHECK(citation.find(':') != std::string::npos);
        CHECK(citation.size() > 16u);
    }

    // --- (registered) - {kSeedId} - {408..412}, asserted BOTH ways ------------
    std::set<ParamID> expected = registered;
    for (const ParamID id : kExemptIds) {
        INFO("exempt ID " << id << " must itself be registered");
        CHECK(expected.erase(id) == 1u);
    }
    CHECK(expected.size() == std::size(kContinuityMechanism));

    for (const ParamID id : expected) {
        INFO("registered in-scope ID " << id << " has no kContinuityMechanism[] row");
        CHECK(table.count(id) == 1u);
    }
    for (const ParamID id : table) {
        INFO("kContinuityMechanism[] row " << id << " is not a registered in-scope ID");
        CHECK(expected.count(id) == 1u);
    }
    CHECK(table == expected);

    // --- class (b) is EXACTLY the twelve IDs kClassBIds names ----------------
    std::set<ParamID> classB;
    for (const ContinuityRow& row : kContinuityMechanism) {
        if (row.cls == ContinuityRow::Class::ProcessorSmoothed) {
            classB.insert(row.id);
        }
    }
    const std::set<ParamID> classBExpected(std::begin(kClassBIds), std::end(kClassBIds));
    CHECK(classB == classBExpected);

    // --- FR-059(b) clause 2: the per-ID time-constant column ------------------
    // Clause 2 requires the time constant to be a STATED number, in one of two
    // forms. This TU ships the second (a per-ID column), so the column must be
    // populated on exactly the class-(b) rows, must name one of the two shipped
    // constants, and must be 0 where it has no referent. Without these three the
    // column would be decoration a wrong value could pass through.
    for (const ContinuityRow& row : kContinuityMechanism) {
        INFO("parameter ID " << row.id << " smoothMs = " << row.smoothMs);
        if (row.cls == ContinuityRow::Class::ProcessorSmoothed) {
            CHECK((row.smoothMs == kBodySmoothMs || row.smoothMs == kDepthSmoothMs));
            CHECK(row.smoothMs > 0.0f);
        } else {
            CHECK(row.smoothMs == 0.0f);
        }
    }
    // And the split itself, so a silent collapse back to one constant fails here
    // rather than at the next SC-005 run: the two aether depths and the five
    // macros carry the LONGER number; the two body coefficients and Phase 10's
    // three plugin-owned rows (FR-038b clause 2) the shorter.
    CHECK(kDepthSmoothMs > kBodySmoothMs);
    for (const ContinuityRow& row : kContinuityMechanism) {
        if (row.id == Seraphis::kAetherSizeBreathDepthId
            || row.id == Seraphis::kAetherTideDepthId || row.id == Seraphis::kMacroDreamId
            || row.id == Seraphis::kMacroBloomId || row.id == Seraphis::kMacroDissolveId
            || row.id == Seraphis::kMacroGravityId || row.id == Seraphis::kMacroEntropyId) {
            INFO("ID " << row.id << " must carry the aether-depth time constant");
            CHECK(row.smoothMs == kDepthSmoothMs);
        }
        if (row.id == Seraphis::kBodyResonanceId || row.id == Seraphis::kBodyDampingId
            || row.id == Seraphis::kFxDelayMixId || row.id == Seraphis::kFxWanderDepthId
            || row.id == Seraphis::kFxAzimuthDepthId) {
            INFO("ID " << row.id << " must carry the body time constant");
            CHECK(row.smoothMs == kBodySmoothMs);
        }
    }
}

// =============================================================================
// SC-005 - no zipper, no click
// =============================================================================

TEST_CASE("Seraphis_ParameterAutomation_IsClickFree", "[seraphis][params][continuity]") {
    // The in-scope set is the table's own; the exempt six carry clause 4 only.
    std::set<ParamID> inScope;
    for (const ContinuityRow& row : kContinuityMechanism) {
        inScope.insert(row.id);
    }

    Seraphis::Controller controller;
    REQUIRE(controller.initialize(nullptr) == Steinberg::kResultOk);
    const int32 count = controller.getParameterCount();
    REQUIRE(count == 107);

    std::vector<ParamID> registered;
    registered.reserve(static_cast<std::size_t>(count));
    for (int32 i = 0; i < count; ++i) {
        Steinberg::Vst::ParameterInfo info{};
        REQUIRE(controller.getParameterInfo(i, info) == Steinberg::kResultOk);
        registered.push_back(info.id);
    }

    // --- one automated ID at a time, everything else at its default ----------
    for (const ParamID id : registered) {
        const Render r = renderAutomation(id, {}, false);
        checkContinuity(r, "automating parameter ID " + std::to_string(id),
                        inScope.count(id) == 1u);
    }

    // --- edge combination 1: kAtmosGrainSecondsId = 30 s ---------------------
    // The grain length exceeds the shipped 4 s capture ring. AtmosphereEngine
    // owns that case; the plugin must not second-guess it, and the criterion
    // must include the combination rather than only the one-ID-at-a-time render
    // in which it is reached on the very last step. Density is the automated ID
    // because it is what drives the grain STREAM against that grain length.
    {
        const Render r = renderAutomation(Seraphis::kAtmosDensityId,
                                          {{.id = Seraphis::kAtmosGrainSecondsId, .normalized = 1.0}}, false);
        checkContinuity(r, "edge: kAtmosGrainSecondsId = 30 s, automating 1002", true);
    }

    // --- edge combination 2: kAetherDecayId = 60 s + kAetherFreezeId = on ----
    // Freeze is the "infinite" mode and the two are independent controls; both
    // must be reachable simultaneously without energy growth.
    {
        const Render r = renderAutomation(
            Seraphis::kAetherMixId,
            {{.id = Seraphis::kAetherDecayId, .normalized = 1.0},
             {.id = Seraphis::kAetherFreezeId, .normalized = 1.0}},
            false);
        checkContinuity(r, "edge: kAetherDecayId = 60 s + freeze on, automating 1200", true);
    }

    // --- edge combination 3: kBodyResonatorBypassId = on ---------------------
    // REQUIRED, not decorative. IDs 804 and 811 both reach the UNSMOOTHED
    // cloudDriveGain() consumer, which is multiplied by `bypassGain` - EXACTLY 0
    // at ID 812's registered default. The one-ID-at-a-time construction above
    // therefore CANNOT reach that consumer, so 804's and 811's class-(a)
    // classification would otherwise be asserted only over the path it does
    // reach. If a step is found here, both move to class (b) - subject, like
    // every class-(b) ID, to the same absolute 64-sample grid.
    for (const ParamID id : {Seraphis::kBodyDriveId, Seraphis::kBodyInputAgcId}) {
        const Render r =
            renderAutomation(id, {{.id = Seraphis::kBodyResonatorBypassId, .normalized = 1.0}}, false);
        checkContinuity(r,
                        "edge: kBodyResonatorBypassId = on, automating "
                            + std::to_string(id),
                        true);
    }
}

// =============================================================================
// SC-005's two MANDATORY positive controls
// =============================================================================
// SC-005's control (b) requires the probe to snap "ONE class-(b) smoother" and
// does not name which. ID 1215 (kAetherSizeBreathDepthId) is the subject of both
// controls, and the choice is MEASURED, not editorial.
//
// An earlier revision took ID 801 on the reasoning that "resonance_ feeds the
// modal gain recompute at the control step". THAT REASONING IS WRONG, and the
// component says so itself: continuous_body.h:2545-2558 records that a retune
// steps `radius_` (a decay slope) and `epsilon_` (an instantaneous frequency),
// that "neither is a discontinuity in the output, because sinState_/cosState_
// carry through untouched", and that `inputGain_` - "the only coefficient whose
// step WOULD show as an amplitude step" - cannot move on a retune. The one gain
// path resonance_ does reach (engineDriveFor -> slot.driveLog10) rides the 50 ms
// kDriveSmoothMs smoother. Measured on this render, ID 801 scores 1.044 x
// smoothed and 1.045 x SNAPPED: the probe is wired correctly and the parameter
// simply has no step to bypass, so the control was structurally incapable of
// PASSING - the mirror image of the defect §7.6 exists to rule out.
//
// ID 1215 is the class-(b) row whose consumer is a discontinuity by
// construction: sizeBreathDepth_ scales a live [-1,+1] modulator into the
// SMOOTHED Size before the exponential S(v) mapping, and the product IS the
// delay-line read length (aether_reverb.h:3036-3055, consumed raw at :4256).
// Measured: 1.126 x smoothed against 2.215 x snapped, a 5.7 x separation of the
// raw statistics against a 1.5 x bound.
// =============================================================================

TEST_CASE("Seraphis_ParameterAutomation_IsClickFree_PositiveControls",
          "[seraphis][params][continuity]") {
    const Render smoothed = renderAutomation(Seraphis::kAetherSizeBreathDepthId, {}, false);
    const Stats base = measure(smoothed);
    REQUIRE(base.maxRef > 0.0);
    REQUIRE(base.maxTest > 0.0);
    const double bound = kBoundFactor * base.maxRef;

    SECTION("detector wiring") {
        // The same statistic over a NON-STEP window, with a deliberately
        // injected one-sample step of 2 x that window's OWN maxPerSampleDelta,
        // must EXCEED the bound. The window chosen is the one that produced
        // max(reference), so its own maximum IS base.maxRef and the injected
        // step is 2 x base.maxRef against a 1.5 x base.maxRef bound - the
        // control cannot pass by accident of which window was picked.
        const std::vector<float>& src =
            (base.worstRefChannel == 0) ? smoothed.left : smoothed.right;
        REQUIRE(base.worstRefStart >= 1u);
        REQUIRE(base.worstRefStart + kWindowSamples <= src.size());

        // The copy carries ONE EXTRA LEADING SAMPLE and is measured from index 1,
        // so it sees exactly the deltas measure() saw - including the delta
        // across the window's own first sample. Without the extra sample the
        // copy's statistic would be <= the original's and the equality below
        // (which is what makes "2 x its OWN maximum" equal "2 x max(reference)")
        // would not hold.
        const auto from = static_cast<std::ptrdiff_t>(base.worstRefStart - 1u);
        const auto to = static_cast<std::ptrdiff_t>(base.worstRefStart + kWindowSamples);
        std::vector<float> injected(src.begin() + from, src.begin() + to);
        REQUIRE(injected.size() == kWindowSamples + 1u);
        REQUIRE(maxPerSampleDelta(injected, 1, kWindowSamples) == base.maxRef);

        const std::size_t mid = 1u + (kWindowSamples / 2u);
        injected[mid] = injected[mid - 1u] + static_cast<float>(2.0 * base.maxRef);

        const double detected = maxPerSampleDelta(injected, 1, kWindowSamples);
        INFO("injected one-sample step of " << (2.0 * base.maxRef) << " gave " << detected
                                            << " against bound " << bound);
        CHECK(detected > bound);
    }

    SECTION("criterion wiring") {
        // With FR-059a's probe forcing advanceParamSmoothers() to SNAP, the
        // class-(b) smoother on ID 1215 delivers 100 % of every automation step
        // in one push instead of at most 1 - e^(-64/2880) = 2.20 % per control
        // chunk at kAetherDepthSmoothMs = 300 ms. MEASURED separation of the raw
        // statistics: 0.018664 snapped against 0.003258 smoothed = 5.7 x, and
        // the snapped render's own ratio against its own reference is 2.215 x,
        // so it MUST FAIL clause 3 - which is what this section asserts.
        //
        // A RATIO NEAR 1.075 x IS THE SIGNATURE OF AN UN-HOISTED
        // setParamSmootherTargets(): the slice loop then reads a stale target,
        // never subdivides, and advanceSamples(512) delivers 93.0 % of the step
        // in one push. A RATIO NEAR 1.000 x is the signature of a subject with
        // no step to bypass at all (see the banner: that is what ID 801 did).
        // The remedy for the first is the ORDERING and for the second the
        // SUBJECT; for neither is it a looser bound.
        const Render bypassed =
            renderAutomation(Seraphis::kAetherSizeBreathDepthId, {}, true);
        const Stats probe = measure(bypassed);
        REQUIRE(probe.maxRef > 0.0);

        const double ratio = probe.maxTest / base.maxTest;
        WARN("SC-005 positive control (b): bypassed max test statistic "
             << probe.maxTest << " vs smoothed " << base.maxTest << " -> ratio " << ratio
             << " (measured design point 5.7 x; the bound is 1.5 x)");

        INFO("bypassed/smoothed ratio " << ratio);
        CHECK(ratio > kBoundFactor);

        INFO("bypassed max test statistic " << probe.maxTest << " must EXCEED "
                                            << (kBoundFactor * probe.maxRef));
        CHECK(probe.maxTest > kBoundFactor * probe.maxRef);
    }
}

