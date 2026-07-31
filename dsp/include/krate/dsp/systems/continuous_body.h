#pragma once

// ==============================================================================
// Layer 3: Systems - ContinuousBody
// ==============================================================================
// Seraphis Phase 4: a CONTINUOUSLY-EXCITED resonant body - glass, string, metal
// plate, chamber, ice - driven by a sustained stereo stream rather than struck
// by an impulse. Membrum already models struck bodies; the new DSP work here is
// the continuous-excitation adapter (input RMS tracking -> resonator drive
// compensation, feedback-safe damping floors) so a permanently-driven high-Q
// bank never runs away.
//
// Spec: specs/seraphis-phase4-continuous-body/spec.md
// Plan: specs/seraphis-phase4-continuous-body/plan.md
//
// LAYER: 3 (systems). Dependencies are Layers 0-3 only; no Layer 4 include, no
// plugin include, no arch-guarded krate include.
//
// The same-layer include of <krate/dsp/systems/timevar_comb_bank.h> is LEGAL:
// tools/lint-layers.js:74 flags only strictly-upward includes, and
// systems/poly_synth_engine.h:40-41 is the in-tree precedent. timevar_comb_bank.h
// itself includes only Layers 0-1, so no cycle exists.
//
// CONSTANT SCOPING (FR-008): every named constant below is `static constexpr`
// INSIDE the class, never at namespace scope. That is what lets kMinWidth /
// kMaxWidth / kDefaultWidth coexist with DiffusionNetwork's namespace-scope
// constants of the same name, and kNumCombs with everything else in Krate::DSP.
// ==============================================================================

#include <krate/dsp/core/crossfade_utils.h>  // L0 equalPowerGains, crossfadeIncrement
#include <krate/dsp/core/db_utils.h>         // L0 detail::flushDenormal (R-6 cloud feedback write)
#include <krate/dsp/core/math_constants.h>   // L0 kPi, kTwoPi, kHalfPi
#include <krate/dsp/core/random.h>           // L0 Xorshift32, deriveStreamSeed
#include <krate/dsp/primitives/dc_blocker.h>            // L1
#include <krate/dsp/primitives/delay_line.h>            // L1
#include <krate/dsp/primitives/one_pole.h>              // L1
#include <krate/dsp/primitives/smoother.h>              // L1 OnePoleSmoother
#include <krate/dsp/processors/diffusion_network.h>     // L2
#include <krate/dsp/processors/envelope_follower.h>     // L2
#include <krate/dsp/processors/modal_resonator_bank.h>  // L2 (also supplies DampingLaw)
#include <krate/dsp/processors/waveguide_string.h>      // L2
#include <krate/dsp/systems/timevar_comb_bank.h>        // L3 - same layer, see banner

#include <algorithm>
#include <array>
#include <cassert>  // prepare()-time sanity only (debug builds); never on the audio path
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>  // std::memcpy - the bit-pattern finiteness check (FR-007)
#include <numbers>  // kScatterD mirrors modal_resonator_bank.h:604-605 exactly

// NOTE ON core/dsp_utils.h: deliberately NOT included. Nothing in this component
// calls softClip - FR-037's guard is a std::clamp, and every soft clip on the
// path lives inside ModalResonatorBank / WaveguideString. A header-only Layer 3
// file that pulls dsp_utils.h rebuilds a large part of the tree.
//
// NOTE ON core/db_utils.h: deliberately INCLUDED. detail::flushDenormal
// (db_utils.h:168) is mandated on the decay-cloud feedback write (risk R-6) and
// must not resolve only transitively through primitives/smoother.h:28. It is a
// DENORMAL guard, never a finiteness guard: `(x > -k && x < k) ? 0.0f : x`
// returns NaN and +/-Inf unchanged, because both comparisons are false for NaN.

namespace Krate {
namespace DSP {

/// @brief Continuously-excited resonant body with five materials, three engines,
///        a crossfading material selector and a per-voice slow decay cloud.
///
/// Stereo in, stereo out, block processing, RT-safe after prepare().
class ContinuousBody {
public:
    // =========================================================================
    // Types (FR-010, FR-011)
    //
    // The name `BodyMaterial` - NOT `Material` - is mandatory:
    // `Krate::DSP::Material` already exists at namespace scope
    // (processors/modal_resonator.h:81).
    // =========================================================================

    enum class BodyMaterial : std::uint8_t { Glass = 0, Strings, MetalPlate, Chamber, Ice };
    enum class Engine : std::uint8_t { Modal = 0, Waveguide, Comb };

    static constexpr std::size_t kNumMaterials = 5;
    static constexpr std::size_t kNumEngines = 3;

    /// Engine slots (plan section 6.1). Two are required and sufficient:
    /// FR-024a's collapse rule caps simultaneously-advanced engines at two for
    /// any sequence of setMaterial() calls.
    static constexpr std::size_t kNumSlots = 2;

    // =========================================================================
    // Class-scoped constants (FR-008) - the complete set
    // =========================================================================

    // --- structure -----------------------------------------------------------
    static constexpr std::size_t kControlChunkSamples = 64;    // A-5, matches HarmonicCloud:144
    static constexpr int kModeCountCeiling = 32;               // A-3 / OQ-2, fixed
    static constexpr std::size_t kNumCombs = 6;                // FR-013a Chamber
    static constexpr float kNyquistHeadroomOct = 1.0f;         // FR-043 (configure at 2*f_body)
    static constexpr float kBankNyquistGuard = 0.49f;          // mirrors modal_resonator_bank.h:594
    /// The bank's own golden-ratio scatter displacement constant, reproduced
    /// VERBATIM from `modal_resonator_bank.h:604-605` so FR-043's prefix
    /// boundary is computed with exactly the warp the bank will apply at
    /// `:756` (`f_w *= 1 + C*sin(k*kScatterD)`, `C = scatter*0.10`, `:730`).
    /// Deterministic - no RNG anywhere in the scatter path.
    static constexpr float kScatterD =
        std::numbers::pi_v<float> * (std::numbers::phi_v<float> - 1.0f);
    /// The bank's own amplitude cull, mirrored from
    /// `modal_resonator_bank.h:595` (`kAmplitudeThresholdLinear`, -80 dB) so
    /// FR-032's `G-hat` sums exactly the modes the bank will actually run. The
    /// bank's copy is private, hence the mirror rather than a reference.
    static constexpr float kBankAmplitudeFloor = 1.0e-4f;

    // --- ranges and DEFAULTS (FR-009, complete) ------------------------------
    // FR-006 requires every float setter to substitute the FR-009 Default when
    // its argument is non-finite, so every Default column value is named here.
    static constexpr float kMinNoteHz = 20.0f;
    static constexpr float kMaxNoteHz = 8000.0f;
    static constexpr float kDefaultNoteHz = 220.0f;

    static constexpr float kMinResonance = 0.0f;
    static constexpr float kMaxResonance = 1.0f;
    static constexpr float kDefaultResonance = 0.7f;

    static constexpr float kMinDamping = 0.0f;
    static constexpr float kMaxDamping = 1.0f;
    static constexpr float kDefaultDamping = 0.0f;

    static constexpr float kMinKeyTracking = 0.0f;
    static constexpr float kMaxKeyTracking = 1.0f;
    static constexpr float kDefaultKeyTracking = 1.0f;

    static constexpr float kMinUserDrive = 0.0f;
    static constexpr float kMaxUserDrive = 4.0f;
    static constexpr float kDefaultUserDrive = 1.0f;

    static constexpr float kMinMix = 0.0f;
    static constexpr float kMaxMix = 1.0f;
    static constexpr float kDefaultMix = 1.0f;

    static constexpr float kMinCloudMix = 0.0f;
    static constexpr float kMaxCloudMix = 1.0f;
    static constexpr float kDefaultCloudMix = 0.25f;

    static constexpr float kMinCloudDecaySec = 0.1f;
    static constexpr float kMaxCloudDecaySec = 30.0f;
    static constexpr float kDefaultCloudDecaySec = 4.0f;

    static constexpr float kMinCloudSize = 0.0f;
    static constexpr float kMaxCloudSize = 1.0f;
    static constexpr float kDefaultCloudSize = 1.0f;

    static constexpr float kMinCloudDamping = 0.0f;
    static constexpr float kMaxCloudDamping = 1.0f;
    static constexpr float kDefaultCloudDamping = 0.3f;

    static constexpr float kMinWidth = 0.0f;
    static constexpr float kMaxWidth = 1.0f;
    static constexpr float kDefaultWidth = 1.0f;

    static constexpr BodyMaterial kDefaultMaterial = BodyMaterial::Glass;
    static constexpr bool kDefaultAgcEnabled = true;
    static constexpr bool kDefaultResonatorBypass = false;
    static constexpr std::uint32_t kDefaultSeed = 1u;

    // --- smoothing times (FR-009) --------------------------------------------
    static constexpr float kPitchSmoothMs = 20.0f;
    static constexpr float kDriveSmoothMs = 50.0f;  // log10 domain
    static constexpr float kMixSmoothMs = 20.0f;
    static constexpr float kCloudSmoothMs = 50.0f;
    static constexpr float kMaterialCrossfadeMs = 500.0f;
    static constexpr float kSlotReleaseMs = 10.0f;

    // --- damping / resonance law (FR-035, FR-036) ----------------------------
    static constexpr float kResonanceScaleAtZero = 40.0f;
    static constexpr float kMinB1 = 0.23f;  // T60 = 6.91/0.23 = 30.0 s
    static constexpr float kMaxB1 = 30.0f;  // T60 = 0.23 s
    static constexpr float kDampingB3Scale = 32.0f;
    static constexpr float kMaxCombFeedback = 0.995f;
    /// Ceiling on FR-036's comb `damping_eff`. See `combDampingEff` for the
    /// measurement: at exactly 1.0 the comb's one-pole damper freezes on the
    /// unit circle and the material stops resonating altogether.
    static constexpr float kMaxCombDamping = 0.95f;
    static constexpr float kWgT60Min = 0.05f;
    static constexpr float kWgT60Max = 10.0f;  // waveguide_string.h:144 hard ceiling
    static constexpr float kWgDampingSMax = 0.45f;
    /// Strings' engine-assignment literals (FR-013a, FR-022c). They are NOT
    /// profile fields: `WaveguideString::noteOn` FREEZES stiffness and pick
    /// position (`waveguide_string.h:283-284`), so they can only ever be applied
    /// on the material-assignment path, never at a control step.
    static constexpr float kWgStiffness = 0.15f;
    static constexpr float kWgPickPosition = 0.22f;
    /// Chamber's comb spread (FR-013a). The bank's inharmonic law is
    /// `f[n] = fundamental * sqrt(1 + n*spread)` (`timevar_comb_bank.h:789-794`),
    /// which is what FR-036's comb feedback solve takes `tau_n` from.
    static constexpr float kCombSpread = 0.45f;
    /// Chamber's comb stereo spread (FR-013a, FR-022b).
    ///
    /// INERT ON THIS COMPONENT'S PATH, and set anyway. The bank stores it in
    /// `stereoSpread_` and it is read only by `recalculatePanPositions`, whose
    /// pan gains are consumed only inside `processStereo` - and A-1 makes the
    /// resonator core MONO, so `ContinuousBody` drives the bank through
    /// `processBlock`/`process(float)` and never reaches them. It is applied
    /// because FR-013a tabulates it as part of Chamber's definition and FR-022b
    /// lists the call: a profile field that the code silently declines to apply
    /// is a profile field that will be wrong the first time the path changes
    /// (Phase 7 spatialisation, or a future stereo comb tap). Chamber's audible
    /// width still comes from the decay cloud.
    static constexpr float kCombStereoSpread = 0.6f;
    /// The comb bank's own fundamental clamp (`timevar_comb_bank.h:91`, `:94`,
    /// applied at `:521`). Mirrored so `tau_n` - and therefore `fb_n` and
    /// `G-hat` - are derived from the frequency the bank will actually use.
    static constexpr float kCombMinFundamentalHz = 20.0f;
    static constexpr float kCombMaxFundamentalHz = 1000.0f;
    /// @brief The DRY gain the comb bank's own output carries, subtracted in
    ///        `advanceSlot` so the engine contributes only what it resonated.
    ///
    /// **FR-060 / A-4: the body is a mix stage, and `setMix(1)` is defined as
    /// "body+cloud only" - the input passed through unchanged is what `setMix(0)`
    /// means.** Both non-modal engines violate that on their own: `FeedbackComb`
    /// is `y = x + g*LP(y[n-D])` (`comb_filter.h:352`) and the bank sums six of
    /// them at unity gain (`timevar_comb_bank.h:360` `gainLinear = 1.0f`,
    /// accumulated at `:646`; this component never calls `setCombGain`), so the
    /// bank returns `6*x` plus the resonated part. `WaveguideString::process`
    /// taps the summing junction, `softClip(feedback + excitation)`
    /// (`:178-181`), so it returns `x` plus the resonated part. The modal bank
    /// has no such term - `processBlock` writes the mode sum alone
    /// (`modal_resonator_bank.h:382-396`) - which is why only the two non-modal
    /// materials were affected.
    ///
    /// MEASURED, and the direct cause of two SC-003(b) failures: at
    /// `f_body = 220` Hz with band-limited noise (the SC-003 excitation) the
    /// leaked dry term dominated the top of the spectrum, where the material's
    /// own damping law has no purchase. Spectral centroid of the last 1 s, with
    /// the excitation's own centroid at 10999 Hz for reference:
    ///   - Strings: `d = 0` 8200 Hz -> `d = 1` 8182 Hz (**-0.2 %**, criterion
    ///     -5 %) - the Damping knob was inaudible;
    ///   - Chamber: `d = 0` 10173 Hz -> `d = 1` 11000 Hz (**+8.1 %**) - the
    ///     Damping knob ran BACKWARDS, converging on the raw excitation, because
    ///     damping removes the resonated (low) part and leaves the dry (flat)
    ///     part behind.
    /// Spectral flatness told the same story: Chamber 0.79 and Strings 0.58
    /// against Glass 0.20, i.e. those two materials were mostly un-resonated
    /// input.
    ///
    /// The subtraction is exact for the comb (the direct term is linear and
    /// un-delayed) and exact to the soft clipper's linearity for the waveguide
    /// (`softClip` is the identity below `kSoftClipThreshold`, and FR-033 holds
    /// the junction far below it - see `kTargetPeak`). FR-032's `G-hat` stays a
    /// VALID bound: subtracting the direct term takes the comb's realised gain
    /// from `Sum 1/(1-fb_n)` to `Sum fb_n/(1-fb_n)` and the string's from
    /// `1/(1-g)` to `g/(1-g)`, i.e. strictly DOWN, so nothing that was bounded
    /// becomes unbounded and every level clause can only get more headroom.
    static constexpr float kCombDirectGain = static_cast<float>(kNumCombs);
    /// log2(kResonanceScaleAtZero). FR-035's scale law is evaluated as
    /// `exp2f((1 - r) * kLog2ResonanceScale)` - one exp2, never std::pow
    /// (plan section 7.4, implementation note).
    static constexpr float kLog2ResonanceScale = 5.321928f;
    /// ln(1000) = 6.907755, rounded as the plan/spec write it. T60 = kT60OverB1 / b1_eff.
    static constexpr float kT60OverB1 = 6.91f;

    // --- drive normalisation (FR-032, FR-033, FR-034) ------------------------
    /// @brief The steady-state output level the drive law aims an engine at, at
    ///        `userDrive = 1` with the AGC held at unity.
    ///
    /// **This is NOT 1.0, and the reason is a defect SC-001 caught.** FR-032's
    /// `G-hat` is an upper bound on an engine's steady-state gain, and FR-033
    /// divides by it, so an engine that ATTAINS its bound comes out at exactly
    /// `kTargetPeak * rmsGain * userDrive`. The waveguide attains it exactly - a
    /// sine at `f_body` sits on a comb tooth, where `1/(1-gTotal)` is not a bound
    /// but the realised gain - so with `kTargetPeak = 1.0f` (the value the spec
    /// pins, spec.md FR-033) the drive law aims Strings at precisely
    /// `kEngineClipThreshold`, leaving ZERO headroom before
    /// `WaveguideString::process`'s own `softClip` (`waveguide_string.h:181`).
    /// Measured at SC-001's settings (full-scale sine, `resonance 1.0`,
    /// `drive 4.0`, AGC on): steady-state peak **0.990**, i.e. the string's own
    /// clipper - not the drive law - was setting the level, which is exactly what
    /// SC-001's headroom clause exists to reject. The modal materials hide the
    /// same defect because their `G-hat` sums over all modes while a sine excites
    /// one, so they run 5-15 dB under their bound and never reach the clipper.
    ///
    /// The value is therefore DERIVED, not tuned:
    /// `kEngineHeadroomFrac * kEngineClipThreshold / kMaxUserDrive = 0.9/4`, i.e.
    /// **a full-scale resonant input, AGC at unity, at the MAXIMUM user drive,
    /// lands an attaining engine exactly on the headroom fraction and never
    /// inside its clipper.** The `static_assert` below pins that relation, so a
    /// later change to any of the three constants breaks the build instead of
    /// silently re-opening the defect. At the default `userDrive = 1` the nominal
    /// level is -13 dBFS, and the Drive control's full range is what reaches
    /// saturation - which is what a control named Drive should do.
    ///
    /// Everything measured relative to `kTargetPeak` is unaffected: SC-007's
    /// absolute-level clause is written against `CB::kTargetPeak` symbolically
    /// (`-20 dB ... +3 dB` of it), SC-007(i)/(iii) and SC-015 are ratios, and the
    /// compensation `kTargetPeak/G-hat` stays far inside `kMinDriveGain` for the
    /// largest bound in the table (Metal Plate, `G-hat = 7.9e4` -> `2.9e-6`).
    static constexpr float kTargetPeak = 0.225f;
    static constexpr float kMinDriveGain = 1.0e-7f;
    static constexpr float kMaxDriveGain = 4.0f;
    static constexpr float kGainBoundEps = 1.0e-6f;
    static constexpr float kTargetInputRms = 0.25f;
    static constexpr float kRmsFloor = 1.0e-5f;
    static constexpr float kMinRmsGain = 0.05f;
    static constexpr float kMaxRmsGain = 4.0f;
    static constexpr float kRmsAttackMs = 50.0f;
    static constexpr float kRmsReleaseMs = 200.0f;

    // --- excitation compensation (FR-033a, added 2026-07-31) -----------------
    /// @brief Upper clamp on `excitationComp_`, FR-033a's measured correction
    ///        between `Ĝ` and the gain an engine ACTUALLY realises.
    ///
    /// **Why FR-033's `kTargetPeak / Ĝ` alone is not a level normalisation, and
    /// the defect that proved it.** `Ĝ` (FR-032) is an all-contributors-in-phase,
    /// exactly-on-resonance UPPER BOUND. Only the waveguide attains it (a sine at
    /// `f_body` sits on a comb tooth); every other engine, under every excitation
    /// that is not a full-scale sine parked on a mode, realises a small fraction
    /// of it. The header already warned that modal materials "run 5-15 dB under
    /// their bound". MEASURED against the excitation ContinuousBody actually
    /// ships inside - SeraphisVoice's HarmonicCloud at the shipped voice defaults
    /// (richness 0.60, inharmonicity 0.030, mutation 0.25, gravity 0.20), note 60,
    /// body at resonance 0.7 / damping 0.25 / keyTracking 1.0 / drive 1.0 - the
    /// realised engine peak sat these many dB under `kTargetPeak`:
    ///
    ///     Glass -42.0   Strings -30.8   MetalPlate -55.8   Chamber -44.5   Ice -41.0
    ///
    /// i.e. the body delivered -55 dBFS where it documents -13 dBFS, and a
    /// Seraphis single note reached the user at -60 dBFS. That is the defect this
    /// constant exists to close (phase-owner ruling 2026-07-31; see
    /// specs/seraphis-phase4-continuous-body/spec.md's amendment).
    ///
    /// **Why the correction cannot be a constant.** The spread above is 25 dB and
    /// it is not a property of the engine alone - it is a property of the
    /// engine AND the excitation's spectrum. The same Glass bank driven by a
    /// full-scale sine on mode 0 runs only 8.2 dB under `kTargetPeak`
    /// (SC-007(ii)'s measured table). A static +42 dB would therefore drive the
    /// sine case 34 dB INTO `applyOutputStage`'s soft clipper. No fixed number,
    /// per-material or global, can serve both; the correction has to be measured
    /// from the signal that is actually there. Hence FR-033a.
    ///
    /// **The clamp value.** The largest measured requirement is Metal Plate's
    /// x618. 1024 (+60 dB) leaves 4.4 dB over it. The clamp is NOT the thing that
    /// bounds a transient: `applyOutputStage`'s soft clip at
    /// `kEngineClipThreshold` bounds the engine output at ~+13 dB over
    /// `kTargetPeak` whatever this constant is, and FR-037's `kOutputClamp` sits
    /// behind that. This clamp bounds the ESTIMATOR, so a pathological
    /// input/output pair cannot park the drive at an absurd value that then takes
    /// `kExcitationCompSmoothMs` to unwind.
    static constexpr float kMaxExcitationComp = 1024.0f;
    /// The floor is EXACTLY 1, and that is a statement, not a guard: `Ĝ` is an
    /// upper bound, so the realised gain can never exceed it and the correction
    /// can never legitimately be an attenuation. `excitationComp_ == 1` reproduces
    /// FR-033's original law bit-for-bit, which is what the AGC-off path uses.
    static constexpr float kMinExcitationComp = 1.0f;
    /// Floor on the time constant for an INCREASE of `log2(excitationComp_)`,
    /// advanced once per estimator window (`kEstimatorWindowMs`). Log domain for
    /// FR-033's own reason: the quantity spans three decades between engines and
    /// a linear one-pole would spend its whole trajectory near the larger
    /// endpoint. The effective constant is the larger of this and
    /// `kEstimatorPlantFactor * T60 / ln(1000)`.
    static constexpr float kExcitationCompSmoothMs = 250.0f;
    /// @brief Time constant for a DECREASE of the correction. Fast, fixed, and
    ///        deliberately asymmetric against `kExcitationCompSmoothMs`.
    ///
    /// **A sweeping excitation is the worst case and SC-001 carries one.** Its
    /// 20 Hz -> 8 kHz log sweep spends most of its time off every resonance, so
    /// the estimator settles on a low coupling - and then the sweep crosses a
    /// mode and the realised gain jumps by tens of dB. Measured with a symmetric
    /// (plant-slowed) loop, Chamber at `resonance = 1` / `userDrive = 4` /
    /// `cloudMix = 1`: body peak **16.3** and 98,994 `kOutputClamp` engagements,
    /// against SC-001's 1.5 bound and its `clampDelta == 0` clause.
    ///
    /// The asymmetry is the standard answer and it is sound here for the same
    /// reason it is in a limiter: an UNDER-driven body is a slow, benign error
    /// that must not be corrected faster than the resonator can charge (see
    /// `kEstimatorPlantFactor`), while an OVER-driven one is an immediate
    /// headroom problem. 50 ms matches `kDriveSmoothMs`, so the decrease is
    /// never faster than the smoother that has to carry it.
    static constexpr float kExcitationCompDecreaseMs = 50.0f;
    /// RELEASE time constant of the peak follower the estimator runs on the
    /// measured COUPLING (not on the raw engine peak - see updateExcitationComp).
    /// Long, because it is what makes the drive follow the WORST coupling seen
    /// recently rather than the average: a sweeping or mutating excitation
    /// crosses a resonance only occasionally, and forgetting the crossing faster
    /// than the body rings leaves the body over-driven at the next one.
    static constexpr float kEnginePeakReleaseMs = 2000.0f;
    /// Attack time constant of that same follower. See updateExcitationComp.
    static constexpr float kCouplingAttackMs = 50.0f;
    /// @brief Length of the estimator's measurement window, in ms.
    ///
    /// The peak and the RMS the coupling is formed from are accumulated over
    /// THIS span rather than over one control chunk, so the peak-over-RMS
    /// statistic covers the same number of signal cycles at every sample rate -
    /// see updateExcitationComp for the SC-018 measurement that forces it.
    /// 10 ms is 8 control chunks at 48 kHz and 15 at 96 kHz.
    static constexpr float kEstimatorWindowMs = 10.0f;
    /// Relative `f_body` movement across one estimator window above which the
    /// estimate is HELD. 0.1 % is 1.7 cents per 10 ms - far below any glide
    /// SC-004 renders and far above the residual jitter of a settled pitch
    /// smoother. See updateExcitationComp's clause 4.
    static constexpr float kEstimatorPitchEpsilon = 1.0e-3f;
    /// Estimator gate: a measured coupling at or below this is treated as
    /// "nothing to measure" and the estimate is HELD. The realised couplings in
    /// the table above run from ~2 to ~900, so this only ever fires before the
    /// first driven window or on a fully bypassed resonator.
    static constexpr float kEnginePeakFloor = 1.0e-7f;
    /// @brief How many plant time constants the estimator's own one-pole is
    ///        slowed to, whenever `T60` makes the plant the slower of the two.
    ///
    /// **The estimator regulates a resonator whose own charging time constant is
    /// `T60 / ln(1000)` - up to 2 s. A loop faster than its plant overshoots,
    /// and SC-001 catches that too.** Measured with a fixed 250 ms one-pole,
    /// Glass at `resonance = 1` (`T60 = 13.82 s`, plant tau = 2.0 s): the
    /// estimator raced the charging resonator, read the still-rising output as
    /// "far under target", and parked the drive ~20 dB high - steady-state peak
    /// 0.819 against SC-001(b)'s 0.730. At `kEstimatorPlantFactor = 1.5` the
    /// loop is over-damped for every material in the table and the same
    /// measurement lands where FR-033 says it should.
    ///
    /// The cost is convergence time on a COLD start (nothing else: the estimate
    /// persists across notes, so only the first note of a session sees it). At
    /// the shipped Seraphis body settings `T60 = 4.57 s`, so tau = 0.99 s.
    static constexpr float kEstimatorPlantFactor = 1.5f;
    /// The same plant floor applied to a DECREASE, at a quarter of the weight.
    /// A decrease has to stay fast enough to protect headroom against a sweeping
    /// excitation (see kExcitationCompDecreaseMs) but a 50 ms gain move on a body
    /// that is now running at its documented level is itself an audible event -
    /// SC-012's transition, glide and parameter-sweep clauses count it.
    static constexpr float kEstimatorDecreasePlantFactor = 0.25f;
    /// Upper clamp on the estimator's per-chunk one-pole coefficient. The
    /// coefficient is formed as `chunkSec / tau` - the first-order form of
    /// `1 - exp(-chunkSec/tau)`, exact to 0.3 % at the 250 ms floor and cheaper
    /// than an `exp` per control chunk - so it needs a ceiling to stay a
    /// contraction if a pathological sample rate ever makes the chunk long.
    static constexpr float kMaxEstimatorAlpha = 0.5f;
    /// Hard ceiling on the value handed to `rmsFollower_.processSample`.
    /// `EnvelopeFollower::processRMS` squares its argument IN FLOAT
    /// (`envelope_follower.h:313`), so any |x| > ~1.8e19 overflows to +Inf and
    /// LATCHES: the IIR at `:316-321` keeps a non-finite `squaredEnvelope_`
    /// forever, `detail::flushDenormal` at `:184-185` passes Inf through
    /// unchanged (`db_utils.h:168`), and only `reset()`/`prepare()` clears it.
    /// SC-013(b)'s own +/-1e38 probe exceeds that range, so without this clamp a
    /// single legal finite block permanently pins `stateFinite()` false and mutes
    /// the voice. 1e9 squares to 1e18, ~11 orders inside the float ceiling.
    static constexpr float kMaxFollowerInput = 1.0e9f;
    /// log2(10). FR-033's drive is smoothed in log10 and read back as
    /// `exp2f(x * kLog2Of10)`; `std::pow(10, x)` is forbidden on this path
    /// (evaluated once per control chunk per active slot, ~1500/s/voice).
    static constexpr float kLog2Of10 = 3.32192809f;

    // --- dirty gates (FR-042, FR-042a) ---------------------------------------
    static constexpr float kRetuneEpsilonCents = 0.5f;
    static constexpr float kDampingEpsilonRel = 0.005f;
    static constexpr float kB3Floor = 1.0e-12f;

    // --- output safety (FR-037) ----------------------------------------------
    /// LAST-RESORT guard on the post-crossfade engine sum. Four of the five
    /// materials are bounded to +/-1.0 UPSTREAM of this clamp and two
    /// equal-power gains sum to at most sqrt(2), so this counter is
    /// structurally incapable of moving for them.
    static constexpr float kOutputClamp = 2.0f;  // mirrors harmonic_cloud.h:174
    /// The modal bank's own output-stage soft-clip threshold
    /// (`setOutputSoftClipThreshold(1.0f)`, modal_resonator_bank.h:150).
    /// `applyOutputStage` (`:816-823`) computes `softClip(x / t) * t`, and
    /// `WaveguideString::process` returns `softClip(junction)`
    /// (`waveguide_string.h:181`) with the same effective threshold of 1.0.
    /// This is the real saturation point on 4 of 5 materials.
    static constexpr float kEngineClipThreshold = 1.0f;
    /// FR-031/SC-001 headroom target: the steady-state peak of an engine's own
    /// pre-clip sum must stay at or below this fraction of kEngineClipThreshold.
    static constexpr float kEngineHeadroomFrac = 0.9f;

    // --- decay cloud (FR-050 - FR-053a) --------------------------------------
    static constexpr float kCloudLoopMsL = 37.0f;
    static constexpr float kCloudLoopMsR = 41.0f;
    static constexpr float kCloudDensity = 100.0f;  // percent, all 8 stages
    static constexpr float kMaxCloudFeedback = 0.9995f;
    static constexpr float kCloudDampMinHz = 800.0f;
    static constexpr float kCloudDampMaxHz = 18000.0f;
    static constexpr float kCloudDcCutoffHz = 10.0f;
    static constexpr float kCloudBypassEpsilon = 1.0e-4f;
    static constexpr float kCloudSilenceFloor = 1.0e-6f;
    /// FR-052 calibration lever, and the ONLY sanctioned response to an SC-008
    /// miss ("calibrate `fb` against a measured tail at configure time, never
    /// widen SC-008"). 1.0 would be the nominal Sigma-of-stage-delays figure.
    ///
    /// =================== WHY THIS IS NOT 1.0 (OQ-A, MEASURED) ================
    /// FR-052 derives `fb` from the cascade's MEAN throughput delay. That mean
    /// is exactly right as a mean - feeding an impulse through
    /// `DiffusionNetwork` at size = 100 % and taking the energy centroid of the
    /// output measures 56.83 ms against the spec's 56.886 ms figure, and the
    /// centroid at size = 50 % is 28.35 ms against 28.44 ms. The constant below
    /// does NOT correct that number, and must never be read as claiming the
    /// cascade is longer than it is.
    ///
    /// What the mean does not capture is that a traversal of the cascade is a
    /// RANDOM time, not a fixed one: each Schroeder allpass emits its input at
    /// 0, D, 2D, ... with energies g^2, (1-g^2)^2, (1-g^2)^2 g^2, ...
    /// (`diffusion_network.h:65-67`, g = kAllpassCoeff), so eight stages in
    /// series spread one traversal over a measured standard deviation of
    /// 25.7 ms - 27 % of the 93.9 ms nominal loop. A loop whose per-traversal
    /// time is dispersed decays SLOWER than the mean predicts, because the tail
    /// at any instant is dominated by the paths that have made the FEWEST
    /// traversals and therefore lost the least gain (Jensen: E[e^(theta T)] >=
    /// e^(theta E[T])). The effect grows as `fb` falls, i.e. at short decays.
    ///
    /// MEASURED, this component, 48 kHz, SC-008's exact configuration
    /// (`setResonatorBypass(true)`, cloudMix 1, cloudDamping 0, EDC over
    /// -5..-35 dB), as (measured T60 - requested) / requested:
    ///
    ///   requested   cloudSize 0.0   cloudSize 1.0 @1.0   cloudSize 1.0 @1.32
    ///      0.5 s        -8.4 %          +31.4 %                +10.2 %
    ///      2   s       -12.0 %          +10.9 %                 -5.6 %
    ///     10   s       -11.2 %           +6.1 %                -10.9 %
    ///     30   s        -9.7 %           +6.0 %                -11.2 %
    ///
    /// The cloudSize = 0.0 column is UNTOUCHED by this constant (the cascade
    /// term is multiplied by cloudSize, which is 0 there), so the discrimination
    /// SC-008 exists for - a `fb` derived from the delay line alone fails the
    /// size = 1.0 end - is fully preserved. That column's residual -10 % is the
    /// in-loop one-pole at 18 kHz (cloudDamping = 0's cutoff, FR-053) taking the
    /// top octave out on every traversal; it is the damping control doing its
    /// job and it is inside SC-008's band.
    ///
    /// 1.32 is the centre of the interval that puts ALL EIGHT grid points inside
    /// +/-15 %: the size = 1.0 row needs an effective loop in
    /// [107.3, 117.1] ms, and 37 + 56.886 x 1.32 = 112.1 ms. A single scalar
    /// cannot flatten the row (the required effective loop runs from 123.4 ms at
    /// 0.5 s to 99.5 ms at 30 s), which is exactly the dispersion signature
    /// above; anything decay-dependent would be a curve fit with no physics
    /// behind it, so this stays one constant.
    static constexpr float kCascadeDelayFactor = 1.32f;
    /// Sum of `kDelayRatiosL` (`diffusion_network.h:51-53`) = 17.777 exactly.
    /// A Schroeder allpass of delay D has mean group delay exactly D, so this
    /// sum times `kBaseDelayMs * cloudSize` is the cascade's mean throughput
    /// delay - the `cascadeSec` term of FR-052's `loopSeconds`.
    /// Held as a literal because a `static constexpr` data member cannot be
    /// initialised from a member function defined later in the same class;
    /// `ContinuousBody_ControlSurfaceDefaults` recomputes it from
    /// `kDelayRatiosL` and pins the two together.
    static constexpr float kSumDelayRatios = 17.777f;

    // --- determinism (FR-070a) -----------------------------------------------
    static constexpr float kSeedDetuneCents = 3.0f;

    // =========================================================================
    // Derived compile-time facts, asserted so a later edit cannot silently
    // invalidate the plan.
    // =========================================================================

    static_assert(kControlChunkSamples == 64, "A-5: the Phase 7 shared control clock");
    static_assert(kMinB1 > 1.0f / 5.0f,
                  "FR-035: the component floor must sit ABOVE the bank's own b1 floor "
                  "(modal_resonator_bank.h:712) so the bank's guard is never the thing that binds");
    static_assert(kMaxFollowerInput * kMaxFollowerInput < 1.0e30f,
                  "FR-034: EnvelopeFollower::processRMS squares in float (envelope_follower.h:313); "
                  "the clamped input must not be able to overflow, or the follower latches forever");
    static_assert(kTargetPeak * kMaxUserDrive <= kEngineHeadroomFrac * kEngineClipThreshold,
                  "FR-033/SC-001: an engine that ATTAINS its G-hat (the waveguide does, exactly) "
                  "must still keep kEngineHeadroomFrac of headroom at the maximum user drive - "
                  "see the kTargetPeak comment for the measurement that forced this");

    // =========================================================================
    // Material profile record (FR-011)
    // =========================================================================

    /// @brief Everything that distinguishes one material from another.
    ///
    /// `ratios` is a raw pointer into one of the class-scoped ratio tables below
    /// (nullptr for the two non-modal materials, whose engine generates its own
    /// mode set). `constexpr` static data members are implicitly `inline` in
    /// C++17+, so the pointer targets have external linkage with no out-of-line
    /// definition and no ODR hazard.
    struct MaterialProfile {
        Engine engine;
        const float* ratios;  ///< nullptr for non-modal; points at a class-scoped table
        int defaultModeCount;  ///< <= kModeCountCeiling
        float amplitudeExponent;  ///< alpha in a_k = k^-alpha
        ModalResonatorBank::DampingLaw damping;  ///< {b1, b3}; b3 also = hfDampingParam (modal)
        float stretch;
        float scatter;
        float referenceHz;
        float t60AtMaxResonanceSec;
        float hfDampingParam;  ///< modal: == damping.b3; waveguide: S; comb: per-comb damping
    };

    // =========================================================================
    // The two modal ratio tables (FR-012)
    //
    // Both are STRICTLY INCREASING over all 32 entries, which is what makes
    // FR-043's Nyquist *prefix* truncation exact: if mode k is above the guard,
    // so is every mode after it.
    // =========================================================================

    /// Glass / Ice: free-edge axisymmetric shell law f_n ~ n(n^2-1)/sqrt(n^2+1),
    /// normalised at n = 2, continued to n = 33 (Rossing, wine glasses).
    /// Mean |r_k - (k+1)| over the first 8 entries = 8.1908 - the figure
    /// SC-003(c) is derived from.
    static constexpr std::array<float, kModeCountCeiling> kGlassRatios = {
        1.0000f,   2.8284f,   5.4233f,   8.7706f,   //
        12.8663f,  17.7088f,  23.2974f,  29.6319f,  //
        36.7120f,  44.5377f,  53.1089f,  62.4255f,  //
        72.4875f,  83.2950f,  94.8478f,  107.1460f, //
        120.1897f, 133.9786f, 148.5130f, 163.7927f, //
        179.8178f, 196.5883f, 214.1041f, 232.3653f, //
        251.3718f, 271.1237f, 291.6209f, 312.8636f, //
        334.8515f, 357.5849f, 381.0636f, 405.2876f,
    };

    /// Metal Plate: Rossing's published free circular plate first 8 verbatim,
    /// then a constant-modal-density linear continuation of slope 1.7309 (LSQ
    /// over the published k = 4..8) anchored at k = 8. A thin plate's modal
    /// density is asymptotically constant (Cremer & Heckl; Fletcher & Rossing),
    /// unlike the shell's n^2 law.
    ///
    /// This is deliberately NOT tools/gen-plate-chladni.js's table (P = 1.7,
    /// kappa = 0.11, used by Membrum's plate_modes.h): that models a different
    /// object (cymbal/gong vs Rossing thin circular plate), produces a different
    /// first 8, and contradicts FR-012 and SC-003(c)'s 0.99 figure.
    /// Mean |r_k - (k+1)| over the first 8 entries = 0.9880.
    static constexpr std::array<float, kModeCountCeiling> kPlateRatios = {
        1.0000f,  1.7300f,  2.3280f,  4.0610f,  // Rossing, published
        5.9800f,  6.7100f,  9.0110f,  11.2000f, // Rossing, published
        12.9309f, 14.6618f, 16.3927f, 18.1236f, // plate-density continuation
        19.8545f, 21.5854f, 23.3163f, 25.0472f, //
        26.7781f, 28.5090f, 30.2399f, 31.9708f, //
        33.7017f, 35.4326f, 37.1635f, 38.8944f, //
        40.6253f, 42.3562f, 44.0871f, 45.8180f, //
        47.5489f, 49.2798f, 51.0107f, 52.7416f,
    };

    // =========================================================================
    // The five profiles (FR-011a)
    //
    // Indexed by BodyMaterial's underlying value, so the array order MUST match
    // the enumerator order: Glass, Strings, MetalPlate, Chamber, Ice.
    //
    // Designated initialisers with explicit `f` suffixes throughout: Clang
    // errors on narrowing in brace init where MSVC does not.
    //
    // Strings' `stiffness = 0.15f` / `pickPosition = 0.22f` and Chamber's
    // `spread = 0.45f` / `numCombs = 6` are NOT profile fields - they are
    // engine-assignment literals (plan section 6.2), and `numCombs` is
    // kNumCombs above.
    // =========================================================================

    // Double-braced (the `body_resonance.h:74-75` idiom): std::array wraps a
    // C-array member, and the explicit inner brace keeps Clang's
    // -Wmissing-braces silent on an aggregate-of-aggregates.
    static constexpr std::array<MaterialProfile, kNumMaterials> kMaterialProfiles = {{
        // --- Glass: long, bright, ringing; HF dies first -----------------------
        MaterialProfile{
            .engine = Engine::Modal,
            .ratios = kGlassRatios.data(),
            .defaultModeCount = kModeCountCeiling,
            .amplitudeExponent = 1.0f,
            .damping = ModalResonatorBank::DampingLaw{.b1 = 0.50f, .b3 = 5.0e-8f},
            .stretch = 0.0f,
            .scatter = 0.0f,
            .referenceHz = 660.0f,
            .t60AtMaxResonanceSec = 13.8f,
            .hfDampingParam = 5.0e-8f,  // == damping.b3
        },
        // --- Strings: waveguide loop; damping law is the loss filter S --------
        MaterialProfile{
            .engine = Engine::Waveguide,
            .ratios = nullptr,  // the loop generates its own harmonic series
            .defaultModeCount = 0,
            .amplitudeExponent = 0.0f,  // unused: no modal amplitude profile
            .damping = ModalResonatorBank::DampingLaw{.b1 = 0.0f, .b3 = 0.0f},  // unused
            .stretch = 0.0f,
            .scatter = 0.0f,
            .referenceHz = 196.0f,
            .t60AtMaxResonanceSec = 8.0f,
            .hfDampingParam = 0.15f,  // loss-filter S (setBrightness(2*S); C-6: it DARKENS)
        },
        // --- Metal Plate: longest, near-flat HF damping -----------------------
        MaterialProfile{
            .engine = Engine::Modal,
            .ratios = kPlateRatios.data(),
            .defaultModeCount = kModeCountCeiling,
            .amplitudeExponent = 0.7f,
            .damping = ModalResonatorBank::DampingLaw{.b1 = 0.30f, .b3 = 1.0e-9f},
            .stretch = 0.15f,
            .scatter = 0.10f,
            .referenceHz = 330.0f,
            .t60AtMaxResonanceSec = 23.0f,
            .hfDampingParam = 1.0e-9f,  // == damping.b3
        },
        // --- Chamber: comb bank; damping law is the per-comb lowpass ----------
        MaterialProfile{
            .engine = Engine::Comb,
            .ratios = nullptr,  // f[n] = f_body * sqrt(1 + n*spread) (timevar_comb_bank.h:789-794)
            .defaultModeCount = 0,
            .amplitudeExponent = 0.0f,  // unused
            .damping = ModalResonatorBank::DampingLaw{.b1 = 0.0f, .b3 = 0.0f},  // unused
            .stretch = 0.0f,
            .scatter = 0.0f,
            .referenceHz = 110.0f,
            .t60AtMaxResonanceSec = 2.5f,
            .hfDampingParam = 0.35f,  // per-comb damping (setCombDamping)
        },
        // --- Ice: Glass's ratio table, but shallower, stretched and scattered -
        //
        // TWO FR-011a fields were re-valued against measurement (SC-003's own
        // instruction: "change the FR-011a profiles until the materials really
        // are distinct - NOT the thresholds"). Both moves take Ice FURTHER from
        // Glass, which is the entire point of the pair.
        //
        // `amplitudeExponent` 1.3 -> 0.9. The draft value made Ice DARKER than
        // Glass (a_k = k^-alpha, so a larger alpha starves the upper modes),
        // which contradicts FR-013's own description of the material - "bright
        // but scattered" - and inverted SC-003(d), whose whole claim is that Ice
        // is spectrally FLATTER than Glass. MEASURED at the SC-003 excitation
        // with alpha = 1.3: flatness Ice 0.177 against Glass 0.203 (i.e. 0.026
        // the WRONG way, against a criterion of +0.02), and centroid Ice 2510 Hz
        // against Glass 3110 Hz. Flatness is a property of the mode-AMPLITUDE
        // distribution - scatter moves peaks but cannot flatten a spectrum - so
        // alpha is the only field that can carry this claim.
        //
        // The value was SWEPT, not guessed, because flatness is not monotone in
        // alpha (too shallow and the upper peaks grow faster than the floor
        // between them). Ice flatness against Glass's 0.2034, at the SC-003
        // excitation: alpha 1.3 -> 0.1772, 0.5 -> 0.2197, 0.7 -> 0.2224,
        // 0.9 -> 0.2252. 0.9 is the shipped value: it clears SC-003(d)'s +0.02
        // (delta 0.0219), leaves Ice brighter than Glass (centroid 3300 vs 3110)
        // as FR-013 describes, and stays clear of Glass's own 1.0 so the two
        // materials still differ in all four of FR-011a's numbered fields.
        //
        // `scatter` 0.8 -> 1.0 (the bank's own ceiling, `modal_resonator_bank.h:702`).
        // SC-003(c3) requires >= 6 of the first 8 peaks to sit >= 2 % away from
        // Glass's, and FR-012 derived that from the SCATTER column alone. Ice
        // also carries `stretch = 0.5`, a strictly positive, monotonically
        // growing displacement Glass does not have, and at C = 0.08 it cancels
        // the negative scatter terms at k = 3 (-3.51 % + 1.98 % = -1.60 %) and
        // k = 6 (-6.35 % + 5.95 % = -0.78 %), leaving FIVE of eight. At C = 0.10
        // the product is 0.12, +9.87, -5.72, -2.50, +13.34, +1.49, -2.46,
        // +16.91 %, i.e. SIX clear it, and the two that carry the clause do so
        // by 23 %. (k = 0 can never clear it: sin(0) = 0 leaves only the stretch
        // warp, +0.12 %.)
        MaterialProfile{
            .engine = Engine::Modal,
            .ratios = kGlassRatios.data(),  // SHARED with Glass, by design
            .defaultModeCount = kModeCountCeiling,
            .amplitudeExponent = 0.9f,
            .damping = ModalResonatorBank::DampingLaw{.b1 = 0.60f, .b3 = 3.0e-8f},
            .stretch = 0.5f,
            .scatter = 1.0f,
            .referenceHz = 880.0f,
            .t60AtMaxResonanceSec = 11.5f,
            .hfDampingParam = 3.0e-8f,  // == damping.b3
        },
    }};

    /// @brief The compile-time profile for a material.
    /// @param m Any BodyMaterial enumerator.
    [[nodiscard]] static constexpr const MaterialProfile& profileFor(BodyMaterial m) noexcept
    {
        return kMaterialProfiles[static_cast<std::size_t>(m)];
    }

    // =========================================================================
    // Lifecycle (FR-002, FR-004)
    // =========================================================================

    /// Fills `seedDetuneCache_` for the default seed, so the memoised table is
    /// valid before any code path can read it (a `setMaterial` before
    /// `prepare()` already builds a mode set).
    ContinuousBody() noexcept { rebuildSeedDetuneCache(); }

    // Non-copyable: the engines and the decay cloud hold move-only delay lines.
    // No exception specification is written on the defaulted members - an
    // explicit `noexcept` that disagreed with the implicit one would make the
    // program ill-formed, and the implicit spec is already correct.
    ContinuousBody(const ContinuousBody&) = delete;
    ContinuousBody& operator=(const ContinuousBody&) = delete;

    /// @brief Re-derive every sample-rate-dependent quantity and reset.
    ///
    /// The ONLY method permitted to allocate; NOT real-time safe. Repeatable.
    /// Parameters are NOT restored - a configured body stays configured across a
    /// sample-rate change (FR-009). A prepare() during a crossfade abandons it:
    /// the incoming material becomes current at full gain and every engine is
    /// silenced.
    ///
    /// @param sampleRate Sample rate in Hz; values <= 1 are clamped to 1
    ///                   (the `harmonic_cloud.h:283` idiom).
    void prepare(double sampleRate) noexcept
    {
        sampleRate_ = (sampleRate > 1.0) ? sampleRate : 1.0;
        const auto sr = static_cast<float>(sampleRate_);

        // --- 2. engines -------------------------------------------------------
        modal_[0].prepare(sampleRate_);
        modal_[1].prepare(sampleRate_);
        waveguide_.prepare(sampleRate_);  // allocates 50 ms = the 20 Hz worst case
        comb_.prepare(sampleRate_, kCombMaxDelayMs);

        // --- 3. decay cloud ---------------------------------------------------
        // loopSamples FIRST, so the delay lines can be sized to hold exactly the
        // tap the loop will read. The plan lists the delay prepare before the
        // lround, but `DelayLine::prepare` truncates
        // (`maxDelaySamples_ = (size_t)(sr * seconds)`, delay_line.h:188) while
        // the loop index rounds, so at 44.1 kHz the naive order yields
        // lround(0.037*44100) = 1632 against maxDelaySamples() = 1631 and the
        // read would clamp silently. One sample of headroom removes the
        // rounding hazard without ever summing the cascade's throughput delay
        // into the delay-line size (C-3).
        // Floored at ONE sample: the batched read indexes
        // `loopSamples - 1 - s` in std::size_t arithmetic, so a degenerate
        // prepare() that rounded the loop to 0 samples would underflow to
        // SIZE_MAX and read a clamped, meaningless tap (delay_line.h:212-218)
        // instead of faulting.
        cloud_.loopSamplesL = std::max<std::size_t>(
            1u, static_cast<std::size_t>(std::lround(kCloudLoopMsL * 1.0e-3 * sampleRate_)));
        cloud_.loopSamplesR = std::max<std::size_t>(
            1u, static_cast<std::size_t>(std::lround(kCloudLoopMsR * 1.0e-3 * sampleRate_)));
        cloud_.delayL.prepare(sampleRate_,
                              static_cast<float>(cloud_.loopSamplesL + 1) / sr);
        cloud_.delayR.prepare(sampleRate_,
                              static_cast<float>(cloud_.loopSamplesR + 1) / sr);
        // `DelayLine::read` clamps silently (delay_line.h:212-218), so an
        // over-long index is a wrong loop time with no fault. Debug-only check.
        assert(cloud_.loopSamplesL <= cloud_.delayL.maxDelaySamples());
        assert(cloud_.loopSamplesR <= cloud_.delayR.maxDelaySamples());

        // The DiffusionNetwork allocates its own ~16.9 ms per-stage buffers
        // (diffusion_network.h:202-205); its ~57-64 ms of THROUGHPUT delay is
        // distributed across those stages. That is a different quantity from
        // the 37/41 ms delay-line loop above and the two are never summed.
        cloud_.diffusion.prepare(sr, kControlChunkSamples);
        cloud_.diffusion.setDensity(kCloudDensity);
        cloud_.diffusion.setModDepth(0.0f);
        // reset() AGAIN: `prepare` leaves size_ at 50 % and its internal 10 ms
        // smoothers (kDiffusionSmoothingMs, diffusion_network.h:48) would
        // otherwise glide for the first 480 samples of every render (plan 9.3).
        // Size and width are pushed by `applyCloudGeometry()` from reset(),
        // which prepare() calls at the end.
        cloud_.diffusion.reset();

        cloud_.dampL.prepare(sampleRate_);
        cloud_.dampR.prepare(sampleRate_);
        cloud_.dcL.prepare(sampleRate_, kCloudDcCutoffHz);
        cloud_.dcR.prepare(sampleRate_, kCloudDcCutoffHz);

        // --- 4. input RMS follower, at the CONTROL rate ------------------------
        // `EnvelopeFollower::processSample` advances exactly one step per call
        // and derives its coefficients from the prepared rate
        // (envelope_follower.h:164-188, :359-365). Preparing at the audio rate
        // and calling once per 64 samples would stretch 50 ms into 3.2 s.
        rmsFollower_.prepare(sampleRate_ / static_cast<double>(kControlChunkSamples), 1);
        rmsFollower_.setMode(DetectionMode::RMS);
        rmsFollower_.setAttackTime(kRmsAttackMs);
        rmsFollower_.setReleaseTime(kRmsReleaseMs);

        // --- 5. smoothers -----------------------------------------------------
        configureSmoothers(sr);
        crossfadeInc_ = crossfadeIncrement(kMaterialCrossfadeMs, sampleRate_);
        collapseInc_ = crossfadeIncrement(kSlotReleaseMs, sampleRate_);
        // The INTEGER lengths the fade and the collapse actually run for. Both
        // are compared against a sample count advanced in whole control chunks,
        // so each ends on the first chunk boundary at or past its nominal length
        // (`ceil(len/64)*64` samples) - see advanceCrossfade().
        crossfadeTotalSamples_ = durationSamples(kMaterialCrossfadeMs);
        collapseTotalSamples_ = durationSamples(kSlotReleaseMs);

        // --- 6. derived cloud quantities and the sub-chunk cap ----------------
        updateCloudDerived();
        updateCloudDampingTarget();
        cloudChunkCap_ = std::min({cloud_.loopSamplesL, cloud_.loopSamplesR,
                                   kControlChunkSamples});
        // A degenerate prepare(1000.0) or below can leave the cap at 0; the
        // walker must always make progress, so floor it at one sample.
        if (cloudChunkCap_ == 0) {
            cloudChunkCap_ = 1;
        }

        // --- 7. reset (also abandons any crossfade) ---------------------------
        reset();

        // --- 8 -----------------------------------------------------------------
        prepared_ = true;
    }

    /// @brief Clear all internal state; leave every parameter unchanged.
    ///
    /// Real-time safe. Abandons any crossfade the same way prepare() does, snaps
    /// every smoother to its target, and clears every applied-value shadow so the
    /// next control step's FR-042/FR-042a dirty gates fire unconditionally. That
    /// last clause is not tidiness: `WaveguideString::silence()` sets
    /// `bridgeDelayFloat_ = 0` (`waveguide_string.h:243`) and `process()`
    /// early-returns 0 below `kMinDelaySamples` (`:156`), so without the shadow
    /// clear a reset() would leave Strings permanently silent (plan 10.1 path 3).
    void reset() noexcept
    {
        modal_[0].silence();
        modal_[1].silence();
        waveguide_.silence();
        waveguideTuned_ = false;  // silence() zeroes bridgeDelayFloat_ (see advanceSlot)
        comb_.reset();

        // FR-038a: the SAME clearing set the state recovery runs, called
        // unconditionally here. Sharing the helper is what stops the two paths
        // drifting - a field cleared on one path and not the other is exactly
        // R-13's unrecoverable latch.
        clearCloudState();

        // FR-038a: the recovery ramp is STATE. A reset() abandons it the same way
        // it abandons a crossfade, at full output.
        recoveryGain_ = 1.0f;
        recoveryPhase_ = RecoveryPhase::Idle;
        recoverySamples_ = 0;

        // FR-063: the bypass ramp is STATE, the bypass flag is a PARAMETER.
        // reset() clears the former and leaves the latter alone (FR-009), so the
        // ramp snaps to whichever end its parameter already selects.
        bypassPos_ = resonatorBypass_ ? 1.0f : 0.0f;
        bypassBasePos_ = bypassPos_;
        bypassSamples_ = 0;

        rmsFollower_.reset();
        inputRms_ = 0.0f;
        rmsGain_ = 1.0f;
        clearExcitationComp();

        sampleCounter_ = 0;
        chunkSumSq_ = 0.0;
        chunkCount_ = 0;
        chunkPoisoned_ = false;
        clampCount_ = 0;
        engineSampleCount_.fill(0);

        // Abandon any crossfade: one sounding slot at full gain.
        crossfadePos_ = 0.0f;
        crossfadeSamples_ = 0;
        collapsePos_ = 0.0f;
        collapseSamples_ = 0;
        collapseBasePos_ = 0.0f;
        collapsing_ = false;
        pendingMaterial_ = material_;
        outgoingSlot_ = -1;
        soundingSlot_ = 0;
        for (std::size_t i = 0; i < kNumSlots; ++i) {
            Slot& slot = slots_[i];
            slot.active = (i == 0);
            slot.gain = (i == 0) ? 1.0f : 0.0f;
            slot.gainBound = 1.0f;
            slot.modeCount = 0;
            slot.modeWindowBaseHz = 0.0f;
            slot.inputMuted = false;
            slot.lastEngineSample = 0.0f;
            slot.material = material_;
            slot.engine = profileFor(material_).engine;
            slot.modalIndex = (i == 0 && slot.engine == Engine::Modal) ? 0 : -1;
            // The applied-value shadows (plan 10.1 path 3).
            slot.appliedBodyHz = 0.0f;
            slot.appliedB1 = 0.0f;
            slot.appliedB3 = 0.0f;
            slot.appliedT60 = 0.0f;
            slot.appliedS = 0.0f;
            slot.appliedCombFb.fill(0.0f);
            slot.appliedCombDamp = 0.0f;
        }

        // Snap every smoother to the target implied by the (unchanged) parameters.
        refreshSmootherTargets();
        keyTrackSmoother_.snapToTarget();
        noteLog2Smoother_.snapToTarget();
        mixSmoother_.snapToTarget();
        cloudMixSmoother_.snapToTarget();
        cloudSizeSmoother_.snapToTarget();
        cloudDampLog2Smoother_.snapToTarget();
        widthSmoother_.snapToTarget();
        fbLSmoother_.snapToTarget();
        fbRSmoother_.snapToTarget();
        // The network's geometry follows the two smoothers just snapped, so it
        // is pushed AFTER them and never from the raw parameter.
        applyCloudGeometry();
        applyCloudDampingCutoff();

        updateBodyPitch();
        updateEngineTargets();
        // The sounding slot's engine is CONFIGURED here, not left blank: FR-009
        // pins the freshly-prepare()d body as material Glass at f_body = 220 Hz,
        // i.e. audible without a single setter call. Configuring at reset() is
        // also what gives the FR-042/FR-042a shadows a defined starting point.
        assignSoundingSlotEngine();
        applyCloudDampingCutoff();
        snapDrive();
    }

    // =========================================================================
    // Control surface (FR-006, FR-009)
    //
    // EVERY float setter has the same three-line shape:
    //   1. substitute the FR-009 Default when the argument is non-finite,
    //      checked by BIT PATTERN - before the clamp, because
    //      `std::clamp(NaN, lo, hi)` returns NaN;
    //   2. clamp to the FR-009 range;
    //   3. hand the clamped value to its smoother, or (for the FR-009
    //      exceptions) to nothing - it is read at the control step.
    //
    // `OnePoleSmoother::setTarget` is NOT a substitute for step 1: it maps
    // NaN -> 0 and Inf -> +/-1e10 (`smoother.h:170-181`), i.e. to its OWN
    // fallbacks, not to FR-009's Default column.
    // =========================================================================

    /// @brief Select the body material. Range-checked by the enum; an
    ///        out-of-range cast is UB and is not defended against (documented).
    ///
    /// RT-safe and callable at any time, including in the middle of a fade.
    ///
    /// @par The no-op rule (FR-014)
    /// `material_` IS the incoming material: it is the fade target while a fade
    /// is in flight (including the pending target during a collapse) and the
    /// sounding slot's material otherwise. Selecting it again disturbs nothing -
    /// no crossfade, no engine reconfiguration, no state clear.
    ///
    /// @par The three paths, in FR-024a's own order
    /// 1. **No fade in flight** -> the standard equal-power fade over
    ///    `kMaterialCrossfadeMs` (`startFadeTo`).
    /// 2. **A fade in flight that has not advanced a single control step**
    ///    (`crossfadeSamples_ == 0`) -> the incoming slot is RE-TARGETED in
    ///    place. This is not a shortcut around FR-024a. Gains move only at a
    ///    control step, so the incoming slot's gain is still EXACTLY 0 and every
    ///    sample rendered since the fade began carried `0 * incomingEngine`;
    ///    reconfiguring that engine is therefore inaudible, by the identical
    ///    argument that makes FR-024's own assign-and-snap legal (spec Q2).
    ///    Collapsing instead would swap a material the listener never heard into
    ///    the sounding position for 10 ms and then ring it out for 500 ms.
    ///    **The retarget is REFUSED when the requested engine INSTANCE is the one
    ///    the outgoing slot is ringing on** (`engineInstanceFreeFor`): reassigning
    ///    it would clear a ringing state at crossfade gain 1.0, i.e. a click.
    ///    Only the two single-instance engines can collide - modal bank `i` is
    ///    bound to slot `i` (`configureSlotEngine`), so two modal materials never
    ///    can.
    /// 3. **Otherwise** -> FR-024a's collapse: `pendingMaterial_` is recorded and
    ///    the current `(fadeOut, fadeIn)` pair is ramped to `(0, 1)` over
    ///    `kSlotReleaseMs` while `crossfadePos_` stands still. A further
    ///    `setMaterial` during the collapse only re-points `pendingMaterial_`,
    ///    which is what caps simultaneously-advanced engines at two (FR-020) for
    ///    ANY sequence of calls.
    void setMaterial(BodyMaterial m) noexcept
    {
        if (m == material_) {
            return;
        }
        material_ = m;

        // Before prepare() there is no sample rate to build a crossfade on and
        // no prepared engine to ring out: adopt the material directly. prepare()
        // -> reset() reconfigures the sounding slot from `material_` anyway.
        if (!prepared_) {
            slots_[static_cast<std::size_t>(soundingSlot_)].material = m;
            updateBodyPitch();
            updateEngineTargets();
            assignSoundingSlotEngine();
            return;
        }

        if (outgoingSlot_ < 0) {
            startFadeTo(m);
            return;
        }

        if (!collapsing_ && crossfadeSamples_ == 0 && engineInstanceFreeFor(m)) {
            retargetIncomingSlot(m);
            return;
        }

        pendingMaterial_ = m;
        if (!collapsing_) {
            collapsing_ = true;
            collapseSamples_ = 0;
            collapsePos_ = 0.0f;
            collapseBasePos_ = crossfadePos_;
        }
    }

    /// @brief Resonance [0,1], default 0.7. FR-009 exception: no smoother -
    ///        read at the control step (FR-036's law is evaluated there).
    void setResonance(float v) noexcept
    {
        if (!isFiniteBits(v)) {
            v = kDefaultResonance;
        }
        resonance_ = std::clamp(v, kMinResonance, kMaxResonance);
    }

    /// @brief Damping [0,1], default 0.0. FR-009 exception: no smoother.
    void setDamping(float v) noexcept
    {
        if (!isFiniteBits(v)) {
            v = kDefaultDamping;
        }
        damping_ = std::clamp(v, kMinDamping, kMaxDamping);
    }

    /// @brief Key-tracking amount [0,1], default 1.0. Smoothed 20 ms.
    void setKeyTracking(float v) noexcept
    {
        if (!isFiniteBits(v)) {
            v = kDefaultKeyTracking;
        }
        keyTracking_ = std::clamp(v, kMinKeyTracking, kMaxKeyTracking);
        keyTrackSmoother_.setTarget(keyTracking_);
    }

    /// @brief Played note frequency [20, 8000] Hz, default 220.
    ///        Smoothed 20 ms in the LOG-frequency domain, so a glide is geometric.
    void setNoteFrequencyHz(float v) noexcept
    {
        if (!isFiniteBits(v)) {
            v = kDefaultNoteHz;
        }
        noteHz_ = std::clamp(v, kMinNoteHz, kMaxNoteHz);
        noteLog2Smoother_.setTarget(std::log2(noteHz_));
    }

    /// @brief User drive [0,4], default 1.0. Smoothed 50 ms in the log10 domain.
    void setDrive(float v) noexcept
    {
        if (!isFiniteBits(v)) {
            v = kDefaultUserDrive;
        }
        userDrive_ = std::clamp(v, kMinUserDrive, kMaxUserDrive);
    }

    /// @brief Dry/processed mix [0,1], default 1.0. Smoothed 20 ms, equal power.
    void setMix(float v) noexcept
    {
        if (!isFiniteBits(v)) {
            v = kDefaultMix;
        }
        mix_ = std::clamp(v, kMinMix, kMaxMix);
        mixSmoother_.setTarget(mix_);
    }

    /// @brief Decay-cloud blend [0,1], default 0.25. Smoothed 20 ms, equal power.
    void setCloudMix(float v) noexcept
    {
        if (!isFiniteBits(v)) {
            v = kDefaultCloudMix;
        }
        cloudMix_ = std::clamp(v, kMinCloudMix, kMaxCloudMix);
        cloudMixSmoother_.setTarget(cloudMix_);
    }

    /// @brief Decay-cloud RT60 [0.1, 30] s, default 4.0. The derived feedback
    ///        gain is smoothed 50 ms (FR-052).
    void setCloudDecaySec(float v) noexcept
    {
        if (!isFiniteBits(v)) {
            v = kDefaultCloudDecaySec;
        }
        cloudDecaySec_ = std::clamp(v, kMinCloudDecaySec, kMaxCloudDecaySec);
        updateCloudDerived();
    }

    /// @brief Decay-cloud size [0,1], default 1.0. Smoothed 50 ms.
    ///        Also re-derives `loopSeconds` and `fb` (FR-052).
    void setCloudSize(float v) noexcept
    {
        if (!isFiniteBits(v)) {
            v = kDefaultCloudSize;
        }
        cloudSize_ = std::clamp(v, kMinCloudSize, kMaxCloudSize);
        cloudSizeSmoother_.setTarget(cloudSize_);
        // NOT forwarded to the network here - see applyCloudGeometry().
        updateCloudDerived();
    }

    /// @brief Decay-cloud damping [0,1], default 0.3. Smoothed 50 ms in the
    ///        log-frequency domain (18 kHz -> 800 Hz, geometric).
    void setCloudDamping(float v) noexcept
    {
        if (!isFiniteBits(v)) {
            v = kDefaultCloudDamping;
        }
        cloudDamping_ = std::clamp(v, kMinCloudDamping, kMaxCloudDamping);
        updateCloudDampingTarget();
    }

    /// @brief Stereo width [0,1], default 1.0. Smoothed 20 ms.
    void setWidth(float v) noexcept
    {
        if (!isFiniteBits(v)) {
            v = kDefaultWidth;
        }
        width_ = std::clamp(v, kMinWidth, kMaxWidth);
        widthSmoother_.setTarget(width_);
        // NOT forwarded to the network here - see applyCloudGeometry().
    }

    /// @brief Enable the input AGC (FR-034), default true. Absorbed by the
    ///        drive smoother, so toggling is clickless.
    void setInputAgcEnabled(bool enabled) noexcept
    {
        agcEnabled_ = enabled;
    }

    /// @brief Bypass the resonator engines (FR-063), default false.
    ///        FR-009 exception: the 10 ms equal-power ramp is applied at the
    ///        control step, not by a parameter smoother.
    ///
    /// While the ramp is in flight BOTH paths run - the engines are still
    /// advanced (and still counted) at their fading gain, and the direct
    /// cloud-drive path fades in against them with the equal-power law, exactly
    /// as FR-024a's collapse does. Only when the ramp COMPLETES into bypass is
    /// every active engine `silence()`d and left un-advanced, which is the state
    /// SC-016's "no engine is advanced" clause measures.
    ///
    /// @par Un-bypass must re-tune the string BEFORE the ramp back (plan 10.1)
    /// `WaveguideString::silence()` sets `bridgeDelayFloat_ = 0`
    /// (`waveguide_string.h:243`) and `process()` then early-returns `0.0f` for
    /// every sample below `kMinDelaySamples` (`:156`). The field is written in
    /// exactly three places - `silence()`, `noteOn()` and `retune()` - and none
    /// of the bypass paths moves the pitch, so FR-042's `pitchDirty` gate would
    /// never fire and a bypassed-then-un-bypassed Strings body would emit
    /// digital silence forever while passing every clickless criterion.
    void setResonatorBypass(bool bypass) noexcept
    {
        if (bypass == resonatorBypass_) {
            return;
        }
        resonatorBypass_ = bypass;
        // Ramp from wherever the previous ramp got to, so a toggle mid-ramp
        // reverses rather than jumping.
        bypassBasePos_ = bypassPos_;
        bypassSamples_ = 0;

        if (!bypass && prepared_) {
            const auto& slot = slots_[static_cast<std::size_t>(soundingSlot_)];
            if (slot.engine == Engine::Waveguide) {
                // `retune` refuses below kMinFrequency and before its own
                // prepare() (`waveguide_string.h:501-503`), so the flag is only
                // set on a path where the loop length really was rewritten.
                waveguide_.retune(bodyHz_);
                waveguideTuned_ = true;
            }
        }
    }

    /// @brief Per-voice determinism seed (FR-070), default 1.
    ///
    /// **Configure-time only, and deliberately NOT retro-deterministic.** The seed
    /// is consumed where a mode set is built (`buildModalModeSet`), so it takes
    /// effect at the next event that rebuilds one - a material assignment, or the
    /// next control step whose FR-042 dirty gate fires. Calling `setSeed` on a
    /// settled, un-glided body therefore changes nothing until something else
    /// moves; Phase 7 sets it once per voice before the first note, which is the
    /// only usage FR-070 promises. Re-seeding mid-ring is not defined to
    /// re-detune the modes that are already ringing, and must not be relied on.
    ///
    /// Seed 0 is passed straight through to `Xorshift32`, which substitutes its
    /// own default (`random.h:45-46`), so 0 is a legal seed and not a disable.
    ///
    /// **What it drives (FR-070a): exactly one thing** - the per-voice modal
    /// micro-detune of `seedDetuneFactor`, on the three MODAL materials only.
    /// Strings and Chamber are documented seed-INDEPENDENT (FR-071): the
    /// waveguide's RNG feeds only the note-on burst, which FR-022c injects at
    /// velocity 0 (`velScale = 0`, `waveguide_string.h:393`, consumed at `:446`),
    /// and `TimeVaryingCombBank` hard-seeds its per-comb generators from
    /// `12345u + i*7919u` with no setter (`timevar_comb_bank.h:429`, `:450`).
    void setSeed(std::uint32_t seed) noexcept
    {
        seed_ = seed;
        rebuildSeedDetuneCache();
    }

    // =========================================================================
    // Processing (FR-005, FR-005a)
    // =========================================================================

    /// @brief Render one block, stereo in / stereo out.
    ///
    /// @param inLeft    Left input, `numSamples` readable.
    /// @param inRight   Right input, `numSamples` readable.
    /// @param outLeft   Left output, `numSamples` writable.
    /// @param outRight  Right output, `numSamples` writable.
    /// @param numSamples Block length; any value, including 0 and values far
    ///                   above `kControlChunkSamples`.
    ///
    /// @note IN-PLACE OPERATION IS NOT SUPPORTED: the output pointers must not
    ///       alias the input pointers.
    /// @note Latency is 0 samples at every block size - nothing is buffered.
    ///       The control grid is absolute: a control chunk split 36 + 28 by a
    ///       block boundary yields exactly the same control step as an unsplit
    ///       64 (FR-005a).
    void processStereoBlock(const float* inLeft, const float* inRight,
                            float* outLeft, float* outRight,
                            std::size_t numSamples) noexcept
    {
        // Guards, in order (FR-004). A null pointer means NOTHING is written.
        if (inLeft == nullptr || inRight == nullptr || outLeft == nullptr
            || outRight == nullptr) {
            return;
        }
        // A zero-length block is a no-op and consumes NO control step.
        if (numSamples == 0) {
            return;
        }
        // Processing before prepare() emits silence rather than reading
        // uninitialised coefficients (the harmonic_cloud.h:887-891 idiom).
        if (!prepared_) {
            std::fill_n(outLeft, numSamples, 0.0f);
            std::fill_n(outRight, numSamples, 0.0f);
            return;
        }

        std::size_t done = 0;
        while (done < numSamples) {
            const auto toGrid = kControlChunkSamples
                                - static_cast<std::size_t>(sampleCounter_
                                                           % kControlChunkSamples);
            const std::size_t subChunk =
                std::min({numSamples - done, toGrid, cloudChunkCap_});

            // 1. Mono-sum, finiteness scan and Sigma x^2 - all CARRIED across
            //    calls. The guard is applied AT THE POINT OF ACCUMULATION, never
            //    repaired afterwards: read as a subtraction it would leave
            //    NaN - NaN = NaN, and chunkRms flows straight into
            //    `rmsFollower_.processSample`, documented "Does NOT validate
            //    input" (envelope_follower.h:163-164), which latches permanently.
            for (std::size_t s = 0; s < subChunk; ++s) {
                float m = 0.5f * (inLeft[done + s] + inRight[done + s]);
                if (!isFiniteBits(m)) {
                    chunkPoisoned_ = true;
                    m = 0.0f;
                }
                if (chunkPoisoned_) {
                    // Sticky for the remainder of the control chunk.
                    m = 0.0f;
                }
                monoScratch_[s] = m;
                chunkSumSq_ += static_cast<double>(m) * static_cast<double>(m);
            }
            chunkCount_ += subChunk;
            if (chunkPoisoned_) {
                chunkSumSq_ = 0.0;  // ASSIGNMENT, never a subtraction
            }

            // 2. Advance the engines / crossfade mix / cloud over exactly
            //    subChunk samples, using the coefficients latched at the LAST
            //    control step.
            renderSub(inLeft + done, inRight + done, monoScratch_.data(),
                      outLeft + done, outRight + done, subChunk);

            sampleCounter_ += subChunk;
            done += subChunk;

            // 3. The control step fires ONLY on the absolute 64-grid, never on
            //    a sub-64 tail.
            if ((sampleCounter_ % kControlChunkSamples) == 0) {
                const auto chunkRms = static_cast<float>(
                    std::sqrt(chunkSumSq_ / static_cast<double>(chunkCount_)));
                controlStep(chunkRms);
                chunkSumSq_ = 0.0;
                chunkCount_ = 0;
                chunkPoisoned_ = false;
            }
        }
    }

    // =========================================================================
    // Introspection (FR-007) - this list is EXHAUSTIVE. No success criterion
    // may assert on a quantity absent from it, and nothing else is public.
    // =========================================================================

    /// @brief The material currently selected (the incoming one during a fade).
    [[nodiscard]] BodyMaterial getMaterial() const noexcept { return material_; }

    /// @brief Modes handed to the modal bank after FR-043's truncation.
    [[nodiscard]] int getActiveModeCount() const noexcept
    {
        return slots_[static_cast<std::size_t>(soundingSlot_)].modeCount;
    }

    /// @brief Configured frequency of mode `k`; 0 if `k` >= the active count.
    [[nodiscard]] float getModeFrequencyHz(std::size_t k) const noexcept
    {
        const Slot& slot = slots_[static_cast<std::size_t>(soundingSlot_)];
        if (slot.modalIndex < 0 || k >= static_cast<std::size_t>(slot.modeCount)) {
            return 0.0f;
        }
        return modal_[static_cast<std::size_t>(slot.modalIndex)]
            .getModeFrequency(static_cast<int>(k));
    }

    /// @brief `f_body` after FR-040's key-tracking law.
    [[nodiscard]] float getBodyFrequencyHz() const noexcept { return bodyHz_; }

    /// @brief The T60 FR-036 currently targets for the active engine.
    [[nodiscard]] float getEngineT60Sec() const noexcept
    {
        return slots_[static_cast<std::size_t>(soundingSlot_)].engineT60;
    }

    /// @brief FR-033's smoothed ENGINE drive for the sounding slot. Bottoms out
    ///        at `kMinDriveGain` (the log10 smoother's floor), never at 0.
    [[nodiscard]] float getDriveGain() const noexcept
    {
        return exp10Fast(
            slots_[static_cast<std::size_t>(soundingSlot_)].driveLog10.getCurrentValue());
    }

    /// @brief FR-034's tracked input RMS. Saturates at `kMaxFollowerInput`:
    ///        the follower squares its argument in float and would latch on
    ///        overflow (envelope_follower.h:313, :316-321).
    [[nodiscard]] float getInputRms() const noexcept { return inputRms_; }

    /// @brief FR-033a's measured excitation compensation - the factor between
    ///        FR-032's `Ĝ` bound and the gain the CURRENT excitation actually
    ///        realises. Exactly 1 whenever the AGC is off (FR-034a) or the
    ///        estimator has never had a driven chunk to measure.
    [[nodiscard]] float getExcitationComp() const noexcept { return excitationComp_; }

    /// @brief FR-032's steady-state gain bound for the sounding slot.
    [[nodiscard]] float getSteadyStateGainBound() const noexcept
    {
        return slots_[static_cast<std::size_t>(soundingSlot_)].gainBound;
    }

    /// @brief True while a material crossfade is in flight (FR-024).
    [[nodiscard]] bool isCrossfading() const noexcept { return outgoingSlot_ >= 0; }

    /// @brief Crossfade position in [0,1] (FR-024).
    [[nodiscard]] float getCrossfadePosition() const noexcept { return crossfadePos_; }

    /// @brief FR-052's decay-cloud feedback gain for the left loop.
    [[nodiscard]] float getCloudFeedbackGain() const noexcept { return cloud_.fbL; }

    /// @brief The left loop time FR-052 derived `fb` from: the delay line plus
    ///        the diffusion cascade's distributed throughput delay, the latter
    ///        scaled by `kCascadeDelayFactor`. This is NOT the delay-line size
    ///        (plan 9.1, C-3), and - because the cascade disperses each
    ///        traversal rather than delaying it by a fixed amount - it is the
    ///        DECAY-EFFECTIVE loop time rather than the mean acoustic one; see
    ///        `kCascadeDelayFactor` for the measurement that separates them.
    [[nodiscard]] float getCloudLoopSeconds() const noexcept
    {
        return cloud_.loopSecondsL;
    }

    /// @brief Individual SAMPLES on which FR-037's +/-2.0 clamp altered the
    ///        post-crossfade engine sum (mono, one count per sample).
    ///        Cleared by reset()/prepare() and by nothing else.
    [[nodiscard]] std::uint64_t getClampEngagementCount() const noexcept
    {
        return clampCount_;
    }

    /// @brief Cumulative samples for which engine `e` was actually advanced.
    ///        Cleared by reset()/prepare().
    [[nodiscard]] std::uint64_t getEngineSampleCount(Engine e) const noexcept
    {
        return engineSampleCount_[static_cast<std::size_t>(e)];
    }

    /// @brief Every engine, delay, smoother and follower state is finite.
    /// @note Checks the IEEE-754 exponent field by BIT PATTERN, never
    ///       `std::isnan`/`std::isinf` (the macOS leg builds -ffast-math).
    [[nodiscard]] bool stateFinite() const noexcept
    {
        for (std::size_t i = 0; i < kNumSlots; ++i) {
            if (!engineStateFinite(static_cast<int>(i))) {
                return false;
            }
        }
        return cloudStateFinite() && controlStateFinite();
    }

private:
    // =========================================================================
    // Bit-pattern finiteness (FR-006, FR-007)
    //
    // NEVER std::isnan / std::isinf / std::numeric_limits: the macOS leg builds
    // with -ffast-math, which licenses the compiler to fold them away.
    // =========================================================================

    [[nodiscard]] static bool isFiniteBits(float v) noexcept
    {
        std::uint32_t bits = 0;
        std::memcpy(&bits, &v, sizeof(bits));
        return (bits & 0x7F800000u) != 0x7F800000u;
    }

    [[nodiscard]] static bool isFiniteBits(double v) noexcept
    {
        std::uint64_t bits = 0;
        std::memcpy(&bits, &v, sizeof(bits));
        return (bits & 0x7FF0000000000000ULL) != 0x7FF0000000000000ULL;
    }

    /// FR-038's substitution, as a value: a non-finite sample becomes 0.
    /// Used on the FR-060 dry path, which bypasses the mono accumulator and its
    /// sticky per-chunk poison flag.
    [[nodiscard]] static float substituteNonFinite(float v) noexcept
    {
        return isFiniteBits(v) ? v : 0.0f;
    }

    [[nodiscard]] static bool smootherFinite(const OnePoleSmoother& s) noexcept
    {
        return isFiniteBits(s.getCurrentValue()) && isFiniteBits(s.getTarget());
    }

    /// 10^x without std::pow (FR-033). Deliberately NOT named `exp10f`:
    /// glibc declares a global `exp10f` as a GNU extension.
    [[nodiscard]] static float exp10Fast(float x) noexcept
    {
        return std::exp2(x * kLog2Of10);
    }

    /// FR-035's resonance -> damping-scale law, 40^(1-r).
    [[nodiscard]] static float resonanceScale(float r) noexcept
    {
        return std::exp2((1.0f - r) * kLog2ResonanceScale);
    }

    // =========================================================================
    // Slot state (plan 6.1)
    // =========================================================================

    struct Slot {
        BodyMaterial material = kDefaultMaterial;
        Engine engine = Engine::Modal;
        int modalIndex = -1;      ///< 0 or 1 when engine == Modal, else -1
        bool active = false;      ///< true while this slot is being advanced
        float gain = 0.0f;        ///< crossfade gain, [0,1]
        float gainBound = 1.0f;   ///< FR-032 G-hat for THIS slot
        float engineT60 = 0.0f;   ///< FR-007 getEngineT60Sec source
        int modeCount = 0;        ///< post-FR-043 truncation
        /// The `f_body` `modeCount` was computed at. FR-043's headroom window is
        /// `[modeWindowBaseHz, modeWindowBaseHz * 2^kNyquistHeadroomOct]`: inside
        /// it the count is FROZEN (the bank's own cull at
        /// `modal_resonator_bank.h:759-765` silences any mode that crosses the
        /// guard, reversibly); outside it the count may only ever INCREASE.
        float modeWindowBaseHz = 0.0f;
        bool inputMuted = false;  ///< FR-024 step 3
        /// The LAST sample this slot's engine emitted, captured in `advanceSlot`.
        /// FR-007's belt-and-braces observable for the waveguide and comb slots -
        /// the only public window on the comb bank's state, which exposes no
        /// energy accessor of its own. Cleared by `reset()` and by FR-038a's
        /// recovery, which is what keeps it out of R-13's latch class.
        float lastEngineSample = 0.0f;
        OnePoleSmoother driveLog10;  ///< FR-033, state = log10(engineDrive)
        // Applied-value shadows for the FR-042 / FR-042a dirty gates.
        float appliedBodyHz = 0.0f;
        float appliedB1 = 0.0f;
        float appliedB3 = 0.0f;
        float appliedT60 = 0.0f;
        float appliedS = 0.0f;
        std::array<float, kNumCombs> appliedCombFb{};
        float appliedCombDamp = 0.0f;
    };

    // =========================================================================
    // Decay cloud (plan 9.1) - single-use, private, no independent existence.
    // =========================================================================

    struct DecayCloud {
        DelayLine delayL;
        DelayLine delayR;
        DiffusionNetwork diffusion;
        OnePoleLP dampL;
        OnePoleLP dampR;
        DCBlocker dcL;
        DCBlocker dcR;
        std::size_t loopSamplesL = 0;
        std::size_t loopSamplesR = 0;
        float fbL = 0.0f;
        float fbR = 0.0f;
        float loopSecondsL = 0.0f;
        float loopSecondsR = 0.0f;
        float lastPeak = 0.0f;  ///< FR-053a bypass evaluation
        /// FR-007 / plan 7.8.1: the loop's own state, sampled once per sub-chunk.
        /// The delay lines and the diffusion cascade expose no state accessor, so
        /// the last tap read out of them and the last value written back into them
        /// ARE the observation - and they are the only subsystem a FINITE input
        /// can genuinely poison (plan 7.8.2: the FR-063 bypass path carries no
        /// `1/G-hat` and no FR-037 clamp). Cleared by `clearCloudState()`.
        float lastTapL = 0.0f;
        float lastTapR = 0.0f;
        float lastWriteL = 0.0f;
        float lastWriteR = 0.0f;
    };

    // =========================================================================
    // Internal helpers
    // =========================================================================

    void configureSmoothers(float sr) noexcept
    {
        keyTrackSmoother_.configure(kPitchSmoothMs, sr);
        noteLog2Smoother_.configure(kPitchSmoothMs, sr);
        mixSmoother_.configure(kMixSmoothMs, sr);
        cloudMixSmoother_.configure(kMixSmoothMs, sr);
        widthSmoother_.configure(kMixSmoothMs, sr);
        cloudSizeSmoother_.configure(kCloudSmoothMs, sr);
        cloudDampLog2Smoother_.configure(kCloudSmoothMs, sr);
        fbLSmoother_.configure(kCloudSmoothMs, sr);
        fbRSmoother_.configure(kCloudSmoothMs, sr);
        for (Slot& slot : slots_) {
            slot.driveLog10.configure(kDriveSmoothMs, sr);
        }
        configureExcitationComp(sr);
    }

    /// FR-033a's two per-control-chunk one-pole coefficients. Both are advanced
    /// exactly once per `kControlChunkSamples`, never per sample, so the chunk
    /// period - not the sample period - is what the time constants are built
    /// against. That is also what makes the estimator block-size invariant.
    void configureExcitationComp(float sr) noexcept
    {
        const float chunkSec = static_cast<float>(kControlChunkSamples) / std::max(sr, 1.0f);
        estWindowChunks_ = std::max<std::uint32_t>(
            1u, static_cast<std::uint32_t>(
                    std::ceil((kEstimatorWindowMs * 1.0e-3f) / std::max(chunkSec, 1.0e-9f))));
        estWindowSeconds_ = chunkSec * static_cast<float>(estWindowChunks_);
        estSumSq_ = 0.0f;
        estChunks_ = 0;
    }

    /// @brief `dsp_utils.h`'s `softClip` (`:105-113`), restated rather than
    ///        included.
    ///
    /// FR-003 pins this header's krate include set at thirteen and deliberately
    /// EXCLUDES `dsp_utils.h`; `modal_resonator_bank.h` supplies the real one
    /// transitively, and depending on a transitive include for a control-path
    /// expression is exactly the fragility FR-003 exists to prevent. Five flops,
    /// once per control chunk. If the shared helper's shape ever changes, this
    /// copy must follow it - it exists only to predict what
    /// `applyOutputStage` (`modal_resonator_bank.h:816-823`) and
    /// `WaveguideString::process` (`waveguide_string.h:181`) will do to the
    /// estimator's target.
    [[nodiscard]] static float engineSoftClip(float x) noexcept
    {
        const float x2 = x * x;
        return (x * (27.0f + x2)) / (27.0f + (9.0f * x2));
    }

    void refreshSmootherTargets() noexcept
    {
        keyTrackSmoother_.setTarget(keyTracking_);
        noteLog2Smoother_.setTarget(std::log2(noteHz_));
        mixSmoother_.setTarget(mix_);
        cloudMixSmoother_.setTarget(cloudMix_);
        cloudSizeSmoother_.setTarget(cloudSize_);
        widthSmoother_.setTarget(width_);
        updateCloudDerived();
        updateCloudDampingTarget();
    }

    /// FR-052: `loopSeconds` and the derived feedback gain. The cascade term is
    /// the diffusion network's mean THROUGHPUT delay - a different quantity from
    /// the delay-line size, and never summed into it (plan 9.1, C-3) - scaled by
    /// the measured `kCascadeDelayFactor` so the DECAY, not the mean delay, is
    /// what `fb` is solved against (OQ-A; see that constant for the grid).
    void updateCloudDerived() noexcept
    {
        const float cascadeL =
            kBaseDelayMs * 1.0e-3f * cloudSize_ * kSumDelayRatios * kCascadeDelayFactor;
        const float cascadeR = cascadeL * kStereoOffset;
        cloud_.loopSecondsL = (kCloudLoopMsL * 1.0e-3f) + cascadeL;
        cloud_.loopSecondsR = (kCloudLoopMsR * 1.0e-3f) + cascadeR;
        cloud_.fbL = std::min(exp10Fast(-3.0f * cloud_.loopSecondsL / cloudDecaySec_),
                              kMaxCloudFeedback);
        cloud_.fbR = std::min(exp10Fast(-3.0f * cloud_.loopSecondsR / cloudDecaySec_),
                              kMaxCloudFeedback);
        fbLSmoother_.setTarget(cloud_.fbL);
        fbRSmoother_.setTarget(cloud_.fbR);
    }

    /// Geometric 18 kHz -> 800 Hz map, smoothed in the LOG-frequency domain.
    void updateCloudDampingTarget() noexcept
    {
        const float log2Max = std::log2(kCloudDampMaxHz);
        const float log2Min = std::log2(kCloudDampMinHz);
        cloudDampLog2Smoother_.setTarget(log2Max + (cloudDamping_ * (log2Min - log2Max)));
    }

    void applyCloudDampingCutoff() noexcept
    {
        const float fc = std::exp2(cloudDampLog2Smoother_.getCurrentValue());
        cloud_.dampL.setCutoff(fc);
        cloud_.dampR.setCutoff(fc);
    }

    /// @brief Push the SMOOTHED cloud geometry into `DiffusionNetwork` and snap
    ///        the network's own smoothers (FR-009, FR-053, FR-062).
    ///
    /// ================== WHY THE SMOOTHED VALUE, NOT THE RAW ONE ==============
    /// FR-009 pins `setCloudSize` at `kCloudSmoothMs = 50` and `setWidth` at
    /// `kMixSmoothMs = 20`. Those two smoothers were being advanced every
    /// control step and READ BY NOTHING: the setters forwarded the RAW
    /// parameter straight to the network, so the smoothing the caller actually
    /// heard was `DiffusionNetwork`'s own 10 ms `kDiffusionSmoothingMs`, not
    /// FR-009's figure. Forwarding the smoothed value here makes the FR-009
    /// column true and deletes the second smoother in the series - the same
    /// "adding a second smoother in front of it would buy nothing" argument
    /// FR-042a makes for the modal bank's coefficient smoother.
    ///
    /// ============================ AND WHY IT IS SNAPPED ======================
    /// With the network's smoothers left running, NOTHING in the cascade is
    /// ever settled while any cloud parameter moves, and
    /// `DiffusionNetwork::process` cannot take its static path: every sample
    /// re-derives eleven smoother steps plus sixteen `std::floor`s and sixteen
    /// float divisions. Measured on this repo's MSVC Release build, one
    /// 512-sample block through the cascade costs 49,828 ns with the smoothers
    /// running against 15,197 ns with them settled - a 34.6 us swing on a
    /// 53,333 ns budget (SC-005), and the difference between the operating
    /// point fitting inside 0.5 % of a core and missing it by 30 %.
    ///
    /// Snapping does not remove continuity, it relocates it: the trajectory is
    /// now `cloudSizeSmoother_`'s own 50 ms exponential (and `widthSmoother_`'s
    /// 20 ms), sampled on the absolute 64-sample control grid - 1.33 ms at
    /// 48 kHz. This is the identical trade, with the identical argument, that
    /// `ModalResonatorBank::snapCoefficients()` takes on the retune path.
    /// =========================================================================
    void applyCloudGeometry() noexcept
    {
        cloud_.diffusion.setSize(
            std::clamp(cloudSizeSmoother_.getCurrentValue(), kMinCloudSize, kMaxCloudSize)
            * 100.0f);
        cloud_.diffusion.setWidth(
            std::clamp(widthSmoother_.getCurrentValue(), kMinWidth, kMaxWidth) * 100.0f);
        cloud_.diffusion.snapSmoothers();
    }

    /// FR-040, evaluated in the log-frequency domain so a glide is geometric.
    void updateBodyPitch() noexcept
    {
        const float refHz = profileFor(material_).referenceHz;
        const float kt =
            std::clamp(keyTrackSmoother_.getCurrentValue(), kMinKeyTracking, kMaxKeyTracking);
        const float refLog2 = std::log2(refHz);
        const float noteLog2 = noteLog2Smoother_.getCurrentValue();
        bodyHz_ = std::clamp(std::exp2(refLog2 + (kt * (noteLog2 - refLog2))),
                             kMinNoteHz, kMaxNoteHz);
    }

    /// FR-036: one law, three engines. Damping shapes `b3` (modal) / `S`
    /// (waveguide) / per-comb damping, none of which move the T60 reported here.
    void updateEngineTargets() noexcept
    {
        const float scale = resonanceScale(resonance_);
        for (Slot& slot : slots_) {
            const MaterialProfile& p = profileFor(slot.material);
            switch (p.engine) {
                case Engine::Modal: {
                    const float b1Eff = std::clamp(p.damping.b1 * scale, kMinB1, kMaxB1);
                    slot.engineT60 = kT60OverB1 / b1Eff;
                    break;
                }
                case Engine::Waveguide:
                    slot.engineT60 = std::clamp(p.t60AtMaxResonanceSec / scale,
                                                kWgT60Min, kWgT60Max);
                    break;
                case Engine::Comb:
                default:
                    slot.engineT60 = p.t60AtMaxResonanceSec / scale;
                    break;
            }
        }
    }

    // =========================================================================
    // Modal engine path (FR-022a, FR-040 - FR-043)
    // =========================================================================

    /// FR-036 / plan 7.4: `b1_eff = clamp(b1_material * scale(r), kMinB1, kMaxB1)`.
    [[nodiscard]] float modalB1Eff(const MaterialProfile& p) const noexcept
    {
        return std::clamp(p.damping.b1 * resonanceScale(resonance_), kMinB1, kMaxB1);
    }

    /// FR-036 / plan 7.4: `b3_eff = b3_material * (1 + kDampingB3Scale * d)`.
    [[nodiscard]] float modalB3Eff(const MaterialProfile& p) const noexcept
    {
        return p.damping.b3 * (1.0f + (kDampingB3Scale * damping_));
    }

    /// @brief FR-043's Nyquist PREFIX truncation.
    ///
    /// `N = min(defaultModeCount, #{leading k : f_w(k) < kBankNyquistGuard*fs})`
    /// with `f_w(k)` evaluated at `fBody * 2^kNyquistHeadroomOct` - one octave of
    /// glide headroom - and warped by exactly the bank's own stretch and scatter
    /// laws (`modal_resonator_bank.h:753`, `:756`, `:729-730`) so the prefix
    /// boundary is the one the bank will actually compute.
    ///
    /// Zero-amplitude modes above the guard are NOT free: `processBlock` hands
    /// `numModes_` to the SIMD kernel (`:389-391`) and `flushSilentModes` only
    /// decrements `numActiveModes_` (`:410-423`), so a mode above the guard still
    /// costs a lane for the life of the note. Truncating the COUNT is the fix.
    ///
    /// Both ratio tables are strictly increasing (FR-012), which is what makes a
    /// prefix truncation exact: if mode k is above the guard, so is every mode
    /// after it. `wanted` is therefore monotonically NON-INCREASING in `fBody`.
    [[nodiscard]] int computeModeCount(const MaterialProfile& p, float fBody) const noexcept
    {
        if (p.ratios == nullptr || p.defaultModeCount <= 0) {
            return 0;
        }
        const float stretchB = p.stretch * p.stretch * 0.01f;   // :729
        const float scatterC = p.scatter * 0.10f;               // :730
        const float guardHz = kBankNyquistGuard * static_cast<float>(sampleRate_);
        const float configHz = fBody * std::exp2(kNyquistHeadroomOct);

        const int ceiling = std::min(p.defaultModeCount, kModeCountCeiling);
        int count = 0;
        for (int k = 0; k < ceiling; ++k) {
            const float nk = static_cast<float>(k + 1);
            float fw = p.ratios[k] * configHz * std::sqrt(1.0f + (stretchB * nk * nk));
            fw *= 1.0f + (scatterC * std::sin(static_cast<float>(k) * kScatterD));
            if (fw >= guardHz) {
                break;
            }
            ++count;
        }
        return count;
    }

    /// Fill the mode scratch arrays for `count` modes of `p` at `fBody`.
    /// `a_k = (k+1)^(-alpha)` normalised so `sum(a_k) == 1` over exactly those
    /// `count` entries (plan 5.4), which is what makes
    /// `setOutputGain(1/getInputGainSum())` land at ~1 and the bank's output
    /// stage transparent.
    /// @note FR-070a's per-mode seeded micro-detune is applied on the frequency
    ///       line below - see `seedDetuneFactor`.
    /// @note The two transcendental loops this used to run per call are
    ///       memoised in `ampTableCache_` and `seedDetuneCache_`: neither the
    ///       amplitude law nor the micro-detune is a function of pitch or
    ///       damping, and this is called on every dirty control step
    ///       (FR-042/FR-042a) - the single largest term in SC-005's operating
    ///       point after the mode rewrite itself. The arithmetic is unchanged:
    ///       `ampTableCache_` holds exactly `(k+1)^(-alpha) * norm` over the
    ///       same `count` entries, formed in the same order.
    void buildModalModeSet(const MaterialProfile& p, float fBody, int count) noexcept
    {
        rebuildAmpTable(p.amplitudeExponent, count);
        for (int k = 0; k < count; ++k) {
            const auto i = static_cast<std::size_t>(k);
            modeFreqScratch_[i] = p.ratios[k] * fBody * seedDetuneCache_[i];
            modeAmpScratch_[i] = ampTableCache_[i];
        }
    }

    /// Rebuild `ampTableCache_` when, and only when, `(alpha, count)` moved.
    void rebuildAmpTable(float alpha, int count) noexcept
    {
        if (ampTableAlpha_ == alpha && ampTableCount_ == count) {
            return;
        }
        float ampSum = 0.0f;
        for (int k = 0; k < count; ++k) {
            const float a = std::pow(static_cast<float>(k + 1), -alpha);
            ampTableCache_[static_cast<std::size_t>(k)] = a;
            ampSum += a;
        }
        const float norm = (ampSum > 0.0f) ? (1.0f / ampSum) : 1.0f;
        for (int k = 0; k < count; ++k) {
            ampTableCache_[static_cast<std::size_t>(k)] *= norm;
        }
        ampTableAlpha_ = alpha;
        ampTableCount_ = count;
    }

    /// Rebuild `seedDetuneCache_` for the whole mode ceiling. Called by
    /// `prepare()` and `setSeed()` only - `seedDetuneFactor` is a pure function
    /// of `(seed_, k)`.
    void rebuildSeedDetuneCache() noexcept
    {
        for (int k = 0; k < kModeCountCeiling; ++k) {
            seedDetuneCache_[static_cast<std::size_t>(k)] = seedDetuneFactor(k);
        }
    }

    /// @brief FR-070a: mode `k`'s per-voice seeded micro-detune multiplier.
    ///
    /// `exp2(j_k * kSeedDetuneCents / 1200)` with `j_k in [-1, 1]` drawn from a
    /// FRESH `Xorshift32` (`core/random.h:41`, `nextFloat()` at `:59`) seeded with
    /// `deriveStreamSeed(seed_, k)` (`:102`). A fresh generator per mode - rather
    /// than one walked across the mode set - is what makes the detune a pure
    /// function of `(seed, k)`: `buildModalModeSet` is called with different
    /// `count`s over the life of a body (FR-043 may GROW the count on a downward
    /// glide), and a walked generator would re-detune every mode whenever the
    /// count moved, i.e. would retune the whole bank as a side effect of adding
    /// one mode.
    ///
    /// `deriveStreamSeed`'s non-zero substitution is load-bearing and not
    /// decoration: `Xorshift32::seed()` silently replaces 0 with its own default
    /// (`random.h:72-74`), so two modes whose hash landed on 0 would COLLAPSE onto
    /// one stream and share a detune.
    ///
    /// **This is the ONLY thing the seed drives in this component** (FR-070a), and
    /// therefore the only place the three modal materials differ from Strings and
    /// Chamber under a seed change. It costs one hash and one `exp2` per mode, at
    /// mode-set build time only - never per sample.
    [[nodiscard]] float seedDetuneFactor(int k) const noexcept
    {
        Xorshift32 rng(deriveStreamSeed(seed_, static_cast<std::size_t>(k)));
        const float j = rng.nextFloat();  // [-1, 1]
        return std::exp2(j * kSeedDetuneCents / 1200.0f);
    }

    /// FR-022a material assignment for a modal slot. Clears state via
    /// `setModes` (`modal_resonator_bank.h:238-239`) - legal here and ONLY here,
    /// because a material assignment starts a new body.
    void configureModalSlot(Slot& slot) noexcept
    {
        const MaterialProfile& p = profileFor(slot.material);
        auto& bank = modal_[static_cast<std::size_t>(slot.modalIndex)];

        const int count = computeModeCount(p, bodyHz_);
        const float b1Eff = modalB1Eff(p);
        const float b3Eff = modalB3Eff(p);
        buildModalModeSet(p, bodyHz_, count);

        bank.setModes(modeFreqScratch_.data(), modeAmpScratch_.data(), count,
                      ModalResonatorBank::DampingLaw{.b1 = b1Eff, .b3 = b3Eff},
                      p.stretch, p.scatter);
        // FR-022a: the historical 0.707 clipper (modal_resonator_bank.h:598)
        // would sit PINNED for the entire ring of a sustained input and mask
        // damping modulation - exactly what the header's own note at :120-131
        // warns about. Raise it to 1.0 and normalise the sum instead.
        bank.setOutputSoftClipThreshold(kEngineClipThreshold);
        bank.setOutputGain(1.0f / bank.getInputGainSum());

        // FR-032: G-hat is computed HERE, at material assignment, from the mode
        // set that was just staged - and recomputed at a control step only when
        // this slot's dirty gate fires (applyEngineRetune). The output gain is
        // read back from the bank rather than recomputed so the bound tracks
        // whatever `applyOutputStage` will actually apply.
        slot.gainBound = modalGainBound(bank, count, bank.getOutputGain());

        slot.modeCount = count;
        slot.modeWindowBaseHz = bodyHz_;
        slot.appliedBodyHz = bodyHz_;
        slot.appliedB1 = b1Eff;
        slot.appliedB3 = b3Eff;
    }

    /// FR-022c / FR-022b material assignment for a NON-modal slot: the engine's
    /// own configuration calls, the engine-native damping values FR-032/FR-033
    /// need, the `G-hat` derived from them, and the applied-value shadows the
    /// control-step dirty gate compares against.
    void configureNonModalSlot(Slot& slot) noexcept
    {
        const MaterialProfile& p = profileFor(slot.material);
        slot.modalIndex = -1;
        slot.modeCount = 0;
        slot.modeWindowBaseHz = 0.0f;
        // DELIBERATELY LEFT AT 0, not at bodyHz_: `applyNonModalRetune` treats a
        // non-positive `appliedBodyHz` as unconditionally dirty, so the next
        // control step is guaranteed to run the engine-native retune once. That
        // is what keeps risk R-14 closed - `WaveguideString::silence()` zeroes
        // `bridgeDelayFloat_` (`waveguide_string.h:243`) and `process()`
        // early-returns 0 below `kMinDelaySamples` (`:156`), so a string that is
        // silenced (reset(), FR-063 un-bypass, FR-038a recovery) and never
        // re-tuned is bricked, silently and permanently.
        slot.appliedBodyHz = 0.0f;
        slot.appliedB1 = 0.0f;
        slot.appliedB3 = 0.0f;
        slot.appliedT60 = slot.engineT60;

        if (p.engine == Engine::Waveguide) {
            const float sEff = waveguideSEff(p);

            // FR-022c. Stiffness, pick position, decay and brightness are ALL set
            // BEFORE noteOn: noteOn freezes stiffness and pick position
            // (`waveguide_string.h:283-284`) and SNAPS the frequency, decay and
            // brightness smoothers (`:288-290`), so anything set afterwards would
            // become a glide this assignment never asked for.
            //
            // `setBrightness(2 * S_eff)` - correction C-6: the argument DARKENS
            // (`process()` maps `S = brightness * 0.5` at `:168` into the loss
            // filter at `:197`).
            //
            // WHY noteOn AT ALL, when N-8 says Phase 4 is continuous resonance and
            // never struck: `bridgeDelayFloat_` - the loop length that sets the
            // pitch - is written in exactly three places, `silence()` (`:243`),
            // `noteOn()` (`:325`) and RA-1's `retune()` (`:519`), and retune()
            // cannot be the first of them to run because noteOn is also what
            // designs the dispersion cascade for this pitch (`:299`). At velocity
            // 0 the excitation is provably silent: `velScale = velocity *
            // excitationGain_ = 0` (`:393`) multiplies every sample written into
            // the loop (`:446`), so the delay is filled with exact zeros. This is
            // a tuning primitive, not a pluck.
            //
            // RISK R-7, recorded at the call site so a stack-depth investigation
            // lands on it immediately: `noteOn` builds TWO
            // `std::array<float, 4096>` on the stack (`:405`, `:425`) - 32 KB of
            // audio-thread frame. RT-legal (no allocation, no lock, no I/O) and
            // unchanged by this phase, but it is by far the largest single frame
            // anywhere on this component's path.
            waveguide_.setStiffness(kWgStiffness);
            waveguide_.setPickPosition(kWgPickPosition);
            waveguide_.setDecay(slot.engineT60);
            waveguide_.setBrightness(2.0f * sEff);
            waveguide_.noteOn(bodyHz_, 0.0f);
            waveguideTuned_ = true;

            slot.appliedS = sEff;
            slot.gainBound = waveguideGainBound(slot.engineT60, sEff, bodyHz_);
            return;
        }

        // FR-022b comb assignment, in full. `setStereoSpread` is inert on this
        // component's mono path and is applied regardless - see
        // `kCombStereoSpread` for why.
        const float f0 = combFundamentalHz();
        const float dampEff = combDampingEff(p);
        comb_.setTuningMode(Tuning::Inharmonic);
        comb_.setSpread(kCombSpread);
        comb_.setStereoSpread(kCombStereoSpread);
        comb_.setNumCombs(kNumCombs);
        comb_.setFundamental(f0);
        slot.appliedCombDamp = dampEff;
        for (std::size_t n = 0; n < kNumCombs; ++n) {
            const float fb = combFeedbackFor(n, slot.engineT60, f0);
            slot.appliedCombFb[n] = fb;
            comb_.setCombFeedback(n, fb);
            comb_.setCombDamping(n, dampEff);
        }
        // NOT snapped here, and the reason is measured. FR-024 puts the
        // incoming slot at zero crossfade gain at this instant, so a snap IS
        // inaudible on this slot's own output - but the comb bank is a single
        // SHARED instance, and `setFundamental` at assignment moves delays that
        // a previous Chamber episode may still be ringing on. Letting the bank's
        // own 20 ms delay smoothers carry that ONE change costs nothing (they
        // settle inside 20 ms and the retune path snaps them from then on, so no
        // steady-state or operating-point figure moves) and it is what SC-012
        // measures: with the snap here, transition Chamber -> Strings reported
        // 29 right-channel detections against a control's 28, i.e. it FAILED the
        // control-relative clause at factor 1.0. Without it, all 20 ordered
        // transitions pass.
        slot.gainBound = combGainBound(slot.engineT60, f0);
    }

    /// @brief Configure ONE slot's engine for that slot's own material
    ///        (FR-022a / FR-022b / FR-022c).
    ///
    /// **Modal bank `i` is bound to slot `i`.** Two slots, two banks, so the two
    /// simultaneously-ringing modal states a Glass -> Ice fade needs can never
    /// share an instance - which is what makes `setModes`'s state memset
    /// (`modal_resonator_bank.h:264-265`) safe here: it can only ever clear the
    /// state of the slot being assigned, and a slot is only ever assigned at
    /// crossfade gain 0.
    void configureSlotEngine(Slot& slot, std::size_t slotIndex) noexcept
    {
        const MaterialProfile& p = profileFor(slot.material);
        slot.engine = p.engine;
        if (p.engine == Engine::Modal) {
            slot.modalIndex = static_cast<int>(slotIndex);
            configureModalSlot(slot);
        } else {
            configureNonModalSlot(slot);
        }
        // FR-033: the drive is SNAPPED to the new engine's `1/G-hat`, never
        // glided into it - see snapSlotDrive().
        snapSlotDrive(slot);
    }

    /// Assign the SOUNDING slot's engine for its current material. Used by
    /// reset() and by the pre-prepare() adopt path, both of which run with no
    /// fade in flight and `soundingSlot_ == 0`.
    void assignSoundingSlotEngine() noexcept
    {
        configureSlotEngine(slots_[static_cast<std::size_t>(soundingSlot_)],
                            static_cast<std::size_t>(soundingSlot_));
    }

    /// @brief Is the engine INSTANCE material `m` needs free of the outgoing
    ///        slot? Callers must hold `outgoingSlot_ >= 0`.
    ///
    /// Only the two single-instance engines can collide: there is one
    /// `WaveguideString` and one `TimeVaryingCombBank`, but one modal bank PER
    /// SLOT. Without this test a Strings -> Glass fade retargeted back to Strings
    /// would re-`noteOn` the very string the outgoing slot is ringing on, wiping
    /// its delay lines (`waveguide_string.h:387-389`) at crossfade gain 1.0.
    [[nodiscard]] bool engineInstanceFreeFor(BodyMaterial m) const noexcept
    {
        const Engine wanted = profileFor(m).engine;
        if (wanted == Engine::Modal) {
            return true;
        }
        return wanted != slots_[static_cast<std::size_t>(outgoingSlot_)].engine;
    }

    /// @brief Silence one slot's engine instance.
    ///
    /// **Only ever called at crossfade gain 0** - anywhere in this component.
    /// Silencing a ringing engine is a step equal to its current crossfade gain,
    /// i.e. a click, which is precisely what FR-024a's collapse rule exists to
    /// avoid.
    void silenceSlotEngine(const Slot& slot) noexcept
    {
        switch (slot.engine) {
            case Engine::Modal:
                if (slot.modalIndex >= 0) {
                    modal_[static_cast<std::size_t>(slot.modalIndex)].silence();
                }
                break;
            case Engine::Waveguide:
                waveguide_.silence();
                // silence() zeroes bridgeDelayFloat_ (`waveguide_string.h:243`)
                // and process() then early-returns 0 until a retune - see the
                // waveguideTuned_ note in advanceSlot().
                waveguideTuned_ = false;
                break;
            case Engine::Comb:
            default:
                // TimeVaryingCombBank has no silence(); reset() is its equivalent
                // (`timevar_comb_bank.h:445`) and clears every comb delay line.
                comb_.reset();
                break;
        }
    }

    /// @brief FR-024 steps 1-3: assign `m` to the free slot at gain 0, mute the
    ///        outgoing slot's INPUT, and start the equal-power fade at position 0.
    void startFadeTo(BodyMaterial m) noexcept
    {
        const auto outgoingIndex = static_cast<std::size_t>(soundingSlot_);
        const std::size_t incomingIndex = (outgoingIndex == 0) ? 1u : 0u;
        Slot& incoming = slots_[incomingIndex];
        Slot& outgoing = slots_[outgoingIndex];

        incoming.material = m;
        incoming.gain = 0.0f;  // FR-024 step 1: silent at the instant of assignment
        incoming.inputMuted = false;
        incoming.active = true;

        // `f_body` follows the INCOMING material's referenceHz (FR-040) and each
        // slot's T60 target is a function of its OWN material (FR-036), so both
        // must be refreshed BEFORE the incoming engine is configured -
        // configureNonModalSlot reads `slot.engineT60` for setDecay / the comb
        // feedback solve.
        updateBodyPitch();
        updateEngineTargets();
        configureSlotEngine(incoming, incomingIndex);

        // FR-024 step 3: the outgoing engine's INPUT is muted so it decays
        // through its own damping law - the physically correct behaviour for a
        // resonant body. Its state is never cut.
        outgoing.inputMuted = true;
        outgoing.gain = 1.0f;

        outgoingSlot_ = static_cast<int>(outgoingIndex);
        soundingSlot_ = static_cast<int>(incomingIndex);
        crossfadeSamples_ = 0;
        crossfadePos_ = 0.0f;
        pendingMaterial_ = m;
    }

    /// @brief Re-point an incoming slot that is still at crossfade gain 0
    ///        (setMaterial path 2 - see there for why this is legal).
    void retargetIncomingSlot(BodyMaterial m) noexcept
    {
        const auto incomingIndex = static_cast<std::size_t>(soundingSlot_);
        Slot& incoming = slots_[incomingIndex];
        if (profileFor(m).engine != incoming.engine) {
            // The abandoned instance is at gain 0, so this silence() is legal -
            // and it stops a comb or string that saw at most one sub-chunk of
            // drive from carrying that energy into its next assignment. A modal
            // bank needs no silence(): `setModes` clears its state anyway.
            silenceSlotEngine(incoming);
        }
        incoming.material = m;
        updateBodyPitch();
        updateEngineTargets();
        configureSlotEngine(incoming, incomingIndex);
        pendingMaterial_ = m;
    }

    /// @brief FR-024 step 4: retire the outgoing slot at gain 0 - silence its
    ///        engine, free the slot, and leave the sounding slot at unity.
    void retireOutgoingSlot() noexcept
    {
        Slot& outgoing = slots_[static_cast<std::size_t>(outgoingSlot_)];
        outgoing.gain = 0.0f;
        silenceSlotEngine(outgoing);
        outgoing.active = false;
        outgoing.inputMuted = false;
        slots_[static_cast<std::size_t>(soundingSlot_)].gain = 1.0f;
        outgoingSlot_ = -1;
        crossfadeSamples_ = 0;
    }

    /// Latch both slot gains from one equal-power position (`crossfade_utils.h:50`).
    void applyCrossfadeGainsAt(float position) noexcept
    {
        float fadeOut = 1.0f;
        float fadeIn = 0.0f;
        equalPowerGains(position, fadeOut, fadeIn);
        slots_[static_cast<std::size_t>(outgoingSlot_)].gain = fadeOut;
        slots_[static_cast<std::size_t>(soundingSlot_)].gain = fadeIn;
    }

    /// @brief FR-024 / FR-024a, control step 6: advance the fade (or the
    ///        collapse) by exactly one control chunk and latch the slot gains.
    ///
    /// @par Why the position is `samples * increment` and never an accumulator
    /// The position must be a function of an INTEGER sample count, not of a
    /// running float sum. `crossfadeIncrement(500, 48000) = 1/24000` is not
    /// representable, so accumulating it 375 times lands a few ULP either side of
    /// 1.0 - and a sum that stops one ULP short costs a whole extra control chunk
    /// of double-engine advance, which SC-016's exact-arithmetic clause measures
    /// to the sample. Multiplying the elapsed sample count by the same
    /// `crossfadeIncrement` value gives the identical trajectory with an exact
    /// completion instant.
    ///
    /// The gains are latched ONCE per control chunk and held for all 64 samples:
    /// a step of at most `sin(pi/2 * 64/24000) ~= 0.0042` at 48 kHz, three orders
    /// below ClickDetector's sensitivity.
    void advanceCrossfade() noexcept
    {
        if (outgoingSlot_ < 0) {
            return;
        }

        if (collapsing_) {
            collapseSamples_ += static_cast<std::uint64_t>(kControlChunkSamples);
            if (collapseSamples_ >= collapseTotalSamples_) {
                // FR-024a step 2 complete: the in-flight INCOMING engine - the
                // one whose state IS what is being heard now - becomes the sole
                // sounding engine, the other slot is silenced at gain 0 and
                // freed, and step 3 starts the fade to `pendingMaterial_`.
                const BodyMaterial pending = pendingMaterial_;
                collapsing_ = false;
                collapsePos_ = 0.0f;
                collapseSamples_ = 0;
                collapseBasePos_ = 0.0f;
                crossfadePos_ = 1.0f;
                retireOutgoingSlot();
                startFadeTo(pending);
                return;
            }
            collapsePos_ =
                std::min(1.0f, collapseInc_ * static_cast<float>(collapseSamples_));
            // The collapse ramps the FROZEN (fadeOut, fadeIn) pair to (0, 1) with
            // the SAME equal-power law. That pair is (cos, sin) of
            // `position * pi/2`, so moving `position` from its frozen value to 1
            // IS that ramp - and it is the only reading that keeps
            // `fadeOut^2 + fadeIn^2 == 1` throughout. Multiplying the frozen pair
            // element-wise by a second (cos, sin) pair instead would either never
            // reach (0, 1) or overshoot the incoming gain by 2.3 dB mid-ramp.
            // `crossfadePos_` itself does NOT advance during a collapse (FR-024a).
            applyCrossfadeGainsAt(collapseBasePos_
                                  + ((1.0f - collapseBasePos_) * collapsePos_));
            return;
        }

        crossfadeSamples_ += static_cast<std::uint64_t>(kControlChunkSamples);
        if (crossfadeSamples_ >= crossfadeTotalSamples_) {
            crossfadePos_ = 1.0f;  // FR-024 step 4: clamp to 1, then retire.
            retireOutgoingSlot();
            return;
        }
        crossfadePos_ =
            std::min(1.0f, crossfadeInc_ * static_cast<float>(crossfadeSamples_));
        applyCrossfadeGainsAt(crossfadePos_);
    }

    /// @brief FR-063, control step 6b: advance the resonator-bypass ramp by
    ///        exactly one control chunk.
    ///
    /// Shares `kSlotReleaseMs` with FR-024a's collapse, and therefore its
    /// increment and its integer length - the two ramps are independent state
    /// machines over one duration, and deriving both from `collapseInc_` /
    /// `collapseTotalSamples_` is what keeps them from drifting apart.
    ///
    /// Position is `samples * increment` and never a float accumulator, for the
    /// reason spelled out on `advanceCrossfade`.
    void advanceBypass() noexcept
    {
        const float target = resonatorBypass_ ? 1.0f : 0.0f;
        if (bypassPos_ == target) {
            return;
        }

        bypassSamples_ += static_cast<std::uint64_t>(kControlChunkSamples);
        if (bypassSamples_ >= collapseTotalSamples_) {
            bypassPos_ = target;
            bypassBasePos_ = target;
            if (resonatorBypass_) {
                // FR-063: silence at ZERO gain, exactly as FR-024a's collapse
                // does - the ramp has just reached the end where the engine
                // contributes nothing, so this cannot be a step.
                for (const Slot& slot : slots_) {
                    if (slot.active) {
                        silenceSlotEngine(slot);
                    }
                }
            }
            return;
        }

        const float t = std::min(1.0f, collapseInc_ * static_cast<float>(bypassSamples_));
        bypassPos_ = bypassBasePos_ + (t * (target - bypassBasePos_));
    }

    /// Whole samples in `ms` at the prepared rate, floored at 1 so a degenerate
    /// `prepare(1.0)` still completes a fade. Mirrors the rounding the
    /// SC-016 exact-arithmetic clause recomputes from the same constants.
    [[nodiscard]] std::uint64_t durationSamples(float ms) const noexcept
    {
        const long long n = std::llround(static_cast<double>(ms) * 1.0e-3 * sampleRate_);
        return (n > 0) ? static_cast<std::uint64_t>(n) : std::uint64_t{1};
    }

    /// @brief FR-041 / FR-042 / FR-042a / FR-043 - the control-step apply.
    ///
    /// At most ONE `updateModes` call per active modal slot per control step,
    /// gated by an OR of a pitch flag and a RELATIVE damping flag. Relative on
    /// damping because `b1`/`b3` differ by eight orders of magnitude between
    /// materials. `computeModeCoefficients` runs a `sqrt`, two `sin` and an
    /// `exp` per mode (`modal_resonator_bank.h:753`, `:756`, `:770`, `:773`) -
    /// ~128 transcendentals for 32 modes - so this gate is the single largest
    /// CPU lever in the component.
    void applyEngineRetune() noexcept
    {
        for (Slot& slot : slots_) {
            if (!slot.active) {
                continue;
            }
            if (slot.engine != Engine::Modal) {
                applyNonModalRetune(slot);
                continue;
            }
            if (slot.modalIndex < 0) {
                continue;
            }
            const MaterialProfile& p = profileFor(slot.material);
            const float b1Eff = modalB1Eff(p);
            const float b3Eff = modalB3Eff(p);

            const bool pitchDirty =
                (slot.appliedBodyHz <= 0.0f)
                || (std::fabs(1200.0f * std::log2(bodyHz_ / slot.appliedBodyHz))
                    > kRetuneEpsilonCents);
            const bool dampingDirty =
                (std::fabs(b1Eff - slot.appliedB1)
                 > kDampingEpsilonRel * std::max(slot.appliedB1, kMinB1))
                || (std::fabs(b3Eff - slot.appliedB3)
                    > kDampingEpsilonRel * std::max(slot.appliedB3, kB3Floor));

            if (!pitchDirty && !dampingDirty) {
                continue;
            }

            // FR-043 mid-ring count rule. `wanted` is non-increasing in f_body,
            // so an UPWARD excursion out of the headroom window can only ever
            // ask for FEWER modes - and a decrease is forbidden mid-ring
            // (truncating numModes_ drops ringing state instantaneously, i.e. a
            // click), so it is deferred to the next material assignment where
            // setModes clears state anyway. Only a DOWNWARD excursion below the
            // window base can legitimately grow the count, so that is the only
            // case that pays for a recount.
            if (bodyHz_ < slot.modeWindowBaseHz) {
                const int wanted = computeModeCount(p, bodyHz_);
                slot.modeCount = std::max(slot.modeCount, wanted);
                slot.modeWindowBaseHz = bodyHz_;
            }

            buildModalModeSet(p, bodyHz_, slot.modeCount);
            // NEVER setModes on a retune (`:238-239` memsets sinState_/cosState_
            // and would silence the ring), and NEVER updateDampingLaw
            // (`:307-321` skips `!active_[k]` modes, so it would PERMANENTLY
            // miss any mode `flushSilentModes` (`:410-423`) culled during a
            // quiet passage - risk R-8).
            ModalResonatorBank& bank = modal_[static_cast<std::size_t>(slot.modalIndex)];
            bank.updateModes(
                modeFreqScratch_.data(), modeAmpScratch_.data(), slot.modeCount,
                ModalResonatorBank::DampingLaw{.b1 = b1Eff, .b3 = b3Eff},
                p.stretch, p.scatter);

            // R-12 / OQ-E resolution. `updateModes` only moves the TARGETS; the
            // bank closes the gap in `smoothCoefficients()`, which runs once per
            // `processBlock` CALL (`modal_resonator_bank.h:384`), not once per
            // sample. The FR-005a walker calls `processBlock` once per
            // sub-chunk, and the number of sub-chunks in 1024 samples is a
            // function of the HOST's partitioning (16 for 1x1024, 27 for
            // 100+...+24), so leaving the smoothing to the bank makes the
            // rendered audio depend on the block size - measured at 7.1e-2
            // against SC-011's 1e-4 bound, which is 1.4x the render's own peak
            // (the divergence is phase, not amplitude: epsilon differences
            // integrate into a phase drift over the 1024 samples).
            //
            // Snapping here removes the dependence at ZERO latency, and is the
            // only one of the three candidate fixes that does: OQ-E's option (a)
            // (accumulate into a 64-sample scratch, one `processBlock` per
            // COMPLETE control chunk) costs up to 63 samples of latency and
            // contradicts FR-005a; option (b) records a measured deviation and
            // guts SC-011 on this path.
            //
            // Continuity is NOT lost, it moves here. The coefficient trajectory
            // becomes a staircase whose step interval is the control chunk -
            // 1.33 ms at 48 kHz, i.e. the ~2 ms granularity FR-041/FR-042a
            // *state*, rather than the tau ~= kSmoothingTimeMs x subChunkSamples
            // ~= 128 ms the bank actually delivers (plan section 8.2) - and
            // whose step SIZE is bounded by the dirty gates above:
            // kRetuneEpsilonCents on pitch and kDampingEpsilonRel on b1/b3. A
            // step in `radius_` changes a decay slope, and a step in `epsilon_`
            // changes an instantaneous frequency; neither is a discontinuity in
            // the output, because `sinState_`/`cosState_` carry through
            // untouched. `inputGain_` is the only coefficient whose step WOULD
            // show as an amplitude step, and it cannot move here: the FR-011a
            // amplitude law is a function of the mode INDEX, not of pitch or
            // damping, so a retune leaves every `inputGainTarget_` where it was.
            // Amplitudes only change with the material, which goes through
            // `setModes` on the other slot behind the crossfade.
            bank.snapCoefficients();

            // FR-032: G-hat is recomputed on EXACTLY the gate that fired
            // updateModes, never unconditionally - it costs two `sqrt`, one
            // `exp` and one `sin` per mode, i.e. ~128 transcendentals for a
            // 32-mode bank. modeFreqScratch_/modeAmpScratch_ still hold the set
            // buildModalModeSet staged above; updateModes only read them.
            slot.gainBound =
                modalGainBound(bank, slot.modeCount, bank.getOutputGain());

            slot.appliedBodyHz = bodyHz_;
            slot.appliedB1 = b1Eff;
            slot.appliedB3 = b3Eff;
        }
    }

    /// @brief FR-036 / FR-042 for the waveguide and comb slots: recompute the
    ///        engine-native damping quantities and FR-032's `G-hat` behind the
    ///        same relative dirty gate the modal path uses.
    ///
    /// @note FR-041: every engine call below is STATE-PRESERVING. The waveguide
    ///       is retuned with RA-1's `retune()` and never with `noteOn()`, which
    ///       would reset the delay lines (`waveguide_string.h:387-389`) and re-run
    ///       the excitation fill; the comb bank is retuned with `setFundamental`,
    ///       whose delay change is carried by the bank's own 20 ms smoothers
    ///       (`timevar_comb_bank.h:109`).
    void applyNonModalRetune(Slot& slot) noexcept
    {
        const MaterialProfile& p = profileFor(slot.material);
        const bool pitchDirty =
            (slot.appliedBodyHz <= 0.0f)
            || (std::fabs(1200.0f * std::log2(bodyHz_ / slot.appliedBodyHz))
                > kRetuneEpsilonCents);

        // FR-035: T60_eff is already clamped into [kWgT60Min, kWgT60Max] by
        // updateEngineTargets for the waveguide, and G-hat must use the CLAMPED
        // value, never the requested one.
        const float t60Eff = slot.engineT60;
        const bool decayDirty = relativelyDirty(t60Eff, slot.appliedT60, kB3Floor);

        if (p.engine == Engine::Waveguide) {
            const float sEff = waveguideSEff(p);
            if (!pitchDirty && !decayDirty
                && !relativelyDirty(sEff, slot.appliedS, kB3Floor)) {
                return;
            }
            slot.gainBound = waveguideGainBound(t60Eff, sEff, bodyHz_);

            // FR-041. `setDecay`/`setBrightness` only re-target the string's own
            // smoothers (`waveguide_string.h:145`, `:151`), which `process()`
            // reads per sample (`:163-168`) - no state is cleared.
            //
            // ================= DO NOT COLLAPSE THESE THREE CALLS ==============
            // `retune()` is deliberately handed the material's FIXED reference
            // brightness, not `sEff`, and the real `sEff` is applied AFTER it.
            // Fusing them back into one `setBrightness(2*sEff)` before `retune`
            // makes FR-036's Damping control on Strings provably INERT. See
            // `waveguideRetuneReferenceS` for the derivation and the numbers.
            // ==================================================================
            waveguide_.setDecay(t60Eff);
            waveguide_.setBrightness(2.0f * waveguideRetuneReferenceS(p));
            waveguide_.retune(bodyHz_);
            waveguideTuned_ = true;
            waveguide_.setBrightness(2.0f * sEff);
            // The waveguide counterpart of `bank.snapCoefficients()` and
            // `comb_.snapSmoothers()`, for the same reason. `retune()` has
            // already set the loop LENGTH exactly (FR-080 writes
            // `bridgeDelayFloat_` directly); the three smoothers being snapped
            // feed only the loop-loss gain and the loss filter's S, both of
            // which this control step just recomputed. Leaving them running
            // costs a `std::exp2` and a `std::pow` per sample for the whole of
            // a glide - measured as the difference between Strings' steady
            // state and its operating point in SC-005.
            waveguide_.snapSmoothers();

            slot.appliedBodyHz = bodyHz_;
            slot.appliedT60 = t60Eff;
            slot.appliedS = sEff;
            return;
        }

        // Comb: gated on T60_eff and the per-comb damping, NOT on `fb_n`. `fb_n`
        // sits within a few 1e-3 of 1, so a relative gate on it would be far
        // coarser than the same gate on the T60 it is solved from: fb moving
        // 0.5 % (0.9627 -> 0.9675) moves the loop gain 1/(1-fb) by 15 %.
        const float dampEff = combDampingEff(p);
        if (!pitchDirty && !decayDirty
            && !relativelyDirty(dampEff, slot.appliedCombDamp, kB3Floor)) {
            return;
        }
        // `configureNonModalSlot` sets `appliedBodyHz = 0` so the FIRST control
        // step after a material assignment is unconditionally dirty (risk R-14).
        // That retune re-derives values the assignment has ALREADY pushed, so it
        // moves nothing - but snapping on it would cut short the one glide the
        // bank's own 20 ms delay smoothers are genuinely carrying, the
        // assignment's `setFundamental`. See the note at the assignment site for
        // the SC-012 measurement that makes this distinction, not an aesthetic
        // preference.
        const bool assignmentForced = (slot.appliedBodyHz <= 0.0f);
        const float f0 = combFundamentalHz();
        // FR-041: `setFundamental` recomputes the tuned delays (`:521`) and
        // re-targets the bank's own delay smoothers. Nothing is cleared.
        comb_.setFundamental(f0);
        for (std::size_t n = 0; n < kNumCombs; ++n) {
            const float fb = combFeedbackFor(n, t60Eff, f0);
            slot.appliedCombFb[n] = fb;
            comb_.setCombFeedback(n, fb);
            comb_.setCombDamping(n, dampEff);
        }
        // The comb counterpart of `bank.snapCoefficients()` on the modal path,
        // for the same reason and with the same bound on the step size.
        //
        // `bodyHz_` is ALREADY a control-grid staircase before it reaches any
        // engine: `noteLog2Smoother_` is a 20 ms smoother advanced once per
        // 64-sample chunk (`advanceSamples(kControlChunkSamples)`), so the comb
        // bank's own per-sample 20 ms delay smoother is a SECOND lag in series
        // with it - FR-042a's "adding a second smoother in front of it would buy
        // nothing", applied to the comb. What it does buy is cost: with the
        // bank unsettled `processBlock` cannot hoist, and one 512-sample block
        // through 6 combs measures 57,311 ns against 35,077 ns settled - 22 us
        // of a 53,333 ns budget (SC-005), which is the whole of Chamber's
        // operating-point overage.
        //
        // The step this leaves is bounded by the same gates as the modal one:
        // `kRetuneEpsilonCents` on pitch (0.5 cents = 0.03 % of a delay time)
        // and `kDampingEpsilonRel` on the damping, evaluated 1.33 ms apart at
        // 48 kHz. Clicklessness is measured, not assumed - SC-004's glide and
        // SC-012's full-range setter sweep both include Chamber.
        if (!assignmentForced) {
            comb_.snapSmoothers();
        }
        slot.gainBound = combGainBound(t60Eff, f0);
        slot.appliedCombDamp = dampEff;
        slot.appliedBodyHz = bodyHz_;
        slot.appliedT60 = t60Eff;
    }

    // =========================================================================
    // Continuous-excitation adapter (FR-032 - FR-037) - the new DSP work
    //
    // Why any of this exists: at kMinB1 = 0.23 and fs = 48 kHz a mode's
    // steady-state magnitude at its own resonance is ~1/(2(1-R)) ~= 1.0e5 times
    // the drive. Membrum never sees that because it excites with impulses.
    // Without compensation a continuously-driven bank sits pinned at its
    // clipper for the whole ring and every material sounds identical.
    // =========================================================================

    /// @brief FR-036, waveguide damping: `S_eff = S + d*(kWgDampingSMax - S)`.
    ///
    /// **Correction C-6, written down once so it is not rediscovered as a bug:
    /// `WaveguideString::setBrightness` DARKENS.** It stores
    /// `brightness_` (`waveguide_string.h:148-152`), `process()` maps
    /// `S = brightness * 0.5` (`:168`) into the loss filter
    /// `rho*[(1-S)x + S*x[n-1]]` (`:197`), whose magnitude is flat at `S = 0`
    /// and has a null at Nyquist at `S = 0.5`. A LARGER argument is DARKER, so
    /// the call site is `setBrightness(2 * S_eff)`.
    [[nodiscard]] float waveguideSEff(const MaterialProfile& p) const noexcept
    {
        return p.hfDampingParam + (damping_ * (kWgDampingSMax - p.hfDampingParam));
    }

    /// @brief The loss-filter brightness `retune()`'s loop-length budget is
    ///        solved at - deliberately NOT the brightness the string is running.
    ///
    /// **Why this exists: without it FR-036's Damping control on Strings is
    /// EXACTLY cancelled, and the string's brightness becomes a random function
    /// of its pitch instead.**
    ///
    /// The string's round-trip HF loss is the product of TWO one-zero filters,
    /// not one. The loss filter is `(1-S) + S*z^-1` (`waveguide_string.h:197`),
    /// and the fractional part of the loop length is realised by LINEAR
    /// interpolation (`:175`, `delay_line.h` `readLinear`), which is the
    /// one-zero `(1-frac) + frac*z^-1` - the same form. Their magnitudes
    /// multiply, so what the ear hears is set by `S + frac`, not by `S`.
    ///
    /// `retune()` solves the exact resonance condition
    /// `D = period - 1 - dLoss(S) - dDisp - dDC` (`waveguide_string.h:516`) and
    /// `dLoss(S) ~ S` samples, so `frac = frac(D_raw - S)` and
    ///     `S + frac = D_raw - floor(D_raw - S)`,
    /// which is INDEPENDENT of `S` for any `S` inside one integer step - and the
    /// FR-036 span (`S_material .. kWgDampingSMax`, i.e. 0.30 samples for
    /// Strings) is well inside one. The cancellation is algebraic, not
    /// approximate.
    ///
    /// MEASURED at the SC-003(b) settings (220 Hz, `resonance = 0.7`,
    /// band-limited noise, spectral centroid of the last 1 s):
    ///   - `retune` at the live brightness: `d = 0` 5575 Hz -> `d = 1` 5565 Hz
    ///     (**-0.2 %**, and the sign flips on `band[4]`) - a dead control;
    ///   - `retune` at this fixed reference: `d = 0.25` 5928 Hz -> `d = 1`
    ///     5208 Hz (**-12 %**), monotone at every step of the grid.
    /// The same coupling is why the string's timbre otherwise tracks
    /// `frac(D_raw)`, which jitters arbitrarily from pitch to pitch.
    ///
    /// @par The cost, stated explicitly
    /// The loop length is now solved for a brightness the string is not running,
    /// so its realised pitch is off by `(S_eff - reference)` samples of loop
    /// length. The reference is the MIDPOINT of the material's FR-036 damping
    /// span, which makes that error symmetric and at most half the span
    /// (0.15 samples for Strings): **1.2 cents at 220 Hz, 2.4 at 440, 4.8 at
    /// 880** - inside SC-009's 5-cent bound, which in any case names only the
    /// three modal materials (spec.md:1388). `WaveguideString::retune` itself is
    /// untouched: it remains the exact budget for the brightness it is handed,
    /// so its own SC-009c table and test still hold.
    [[nodiscard]] static float waveguideRetuneReferenceS(const MaterialProfile& p) noexcept
    {
        return 0.5f * (p.hfDampingParam + kWgDampingSMax);
    }

    /// @brief FR-036, comb damping: `damping_eff = damping + d*(1 - damping)`,
    ///        with the endpoint held off `kMaxCombDamping`.
    ///
    /// **The ceiling is load-bearing, and 1.0 is a DEGENERATE value, not a
    /// maximally-damped one.** The comb's damping filter is the one-pole
    /// `LP(x) = (1-d)*x + d*LP_prev` with `LP_prev` fed back from its own output
    /// (`comb_filter.h:346-347`). At `d = 1` the input term vanishes and the
    /// recursion becomes `state = state`: the pole sits exactly ON the unit
    /// circle, the filter FREEZES at whatever sample it last held, and the comb
    /// degenerates to `y = x + fb*constant` - a DC offset with no decay and no
    /// resonance at all.
    ///
    /// MEASURED before the ceiling was added, at `damping = 1` on Chamber with
    /// the SC-003 excitation: the whole output collapsed into the 20-100 Hz band
    /// (band energy fraction **1.000**, spectral centroid **24 Hz** against 5650
    /// Hz at `damping = 0`). FR-036's own comb FEEDBACK clamp,
    /// `kMaxCombFeedback = 0.995`, exists for exactly this reason one term over;
    /// this is its counterpart on the damping term.
    ///
    /// 0.95 leaves the pole a finite time constant (~20 samples, a ~380 Hz
    /// one-pole corner at 48 kHz) - dark enough that the control's top end is
    /// still an extreme, and far past the point where SC-003(b)'s centroid
    /// clause is satisfied (`d = 0.75`, i.e. `damping_eff = 0.8375`, already
    /// measures -27 %).
    [[nodiscard]] float combDampingEff(const MaterialProfile& p) const noexcept
    {
        return std::min(p.hfDampingParam + (damping_ * (1.0f - p.hfDampingParam)),
                        kMaxCombDamping);
    }

    /// The fundamental the comb bank will actually use, after its own
    /// `[20, 1000]` Hz clamp (`timevar_comb_bank.h:521`).
    [[nodiscard]] float combFundamentalHz() const noexcept
    {
        return std::clamp(bodyHz_, kCombMinFundamentalHz, kCombMaxFundamentalHz);
    }

    /// Comb `n`'s round-trip time in seconds, from the bank's own inharmonic
    /// law (`:789-794`) and its CLAMPED delay range (`:737-741`, `[1, 50]` ms).
    /// Using the unclamped delay would mis-solve `fb_n` for any comb the bank
    /// then clamps.
    [[nodiscard]] float combTauSec(std::size_t n, float f0) const noexcept
    {
        const float freq =
            f0 * std::sqrt(1.0f + (static_cast<float>(n) * kCombSpread));
        const float delayMs =
            std::clamp(1000.0f / std::max(freq, 1.0f), 1.0f, kCombMaxDelayMs);
        return delayMs * 1.0e-3f;
    }

    /// FR-035/FR-036: `fb_n = min(10^(-3*tau_n/T60_eff), kMaxCombFeedback)`.
    /// The ceiling sits well inside the bank's own +/-0.9999 (`comb_filter.h:32`,
    /// `:35`, applied at `timevar_comb_bank.h:488`).
    [[nodiscard]] float combFeedbackFor(std::size_t n, float t60Eff,
                                        float f0) const noexcept
    {
        const float tau = combTauSec(n, f0);
        return std::min(exp10Fast(-3.0f * tau / std::max(t60Eff, kGainBoundEps)),
                        kMaxCombFeedback);
    }

    /// @brief FR-032's `G-hat` for a modal slot, derived from the recursion the
    ///        bank ACTUALLY runs.
    ///
    /// `ModalResonatorBank` is the coupled (magic-circle) form
    /// (`modal_resonator_bank.h:880-886`):
    /// `s[n] = R(s + eps*c) + g*u; c[n] = R(c - eps*s[n]); y = s[n]`, whose
    /// transfer function is
    /// `H(z) = g(1 - R z^-1) / [1 - R(2 - R eps^2) z^-1 + R^2 z^-2]` - i.e. it
    /// has a **zero at `z = R`**. Ignoring that zero (the "flat numerator" form
    /// an earlier spec draft carried) over-estimates by a factor
    /// `~1/(2 sin(theta/2))` = **35x (31 dB)** at 220 Hz / 48 kHz, which is
    /// exactly what SC-015's tightness clause rejects.
    ///
    /// `|H|` is evaluated at the POLE ANGLE with no transcendentals beyond the
    /// `R`/`eps` the bank itself computes:
    /// ```
    /// cth  = 1 - R*eps^2/2          // NEVER assume theta = 2*pi*f/fs; the two
    /// c2th = 2*cth^2 - 1            // coincide only at R = 1
    /// Ghat_k = g_k * sqrt(1 - 2R*cth + R^2) / [(1-R) * sqrt(1 - 2R*c2th + R^2)]
    /// Ghat   = Sum_k Ghat_k         // all modes in phase -> a TRUE upper bound
    /// ```
    /// Two `sqrt` per mode. Gated by the SAME dirty flag as `updateModes`
    /// (FR-042/FR-042a) - it is never computed unconditionally.
    ///
    /// ================ WHY THIS READS THE BANK RATHER THAN RE-DERIVING =======
    /// It used to reproduce the bank's stretch warp, scatter warp, amplitude
    /// cull, Nyquist cull, `radius` and `epsilon` from `modeFreqScratch_` /
    /// `modeAmpScratch_` - a `sqrt`, two `sin` and an `exp` per mode, i.e. a
    /// SECOND full copy of `computeModeCoefficients`' transcendental work on a
    /// path that runs on every dirty control step (FR-042/FR-042a), and a
    /// second implementation of five cull/warp rules that had to be kept in
    /// step with the bank by hand. `getRadiusTargets()` / `getEpsilonTargets()`
    /// / `getInputGainTargets()` hand back exactly what `updateModes` just
    /// computed, with every cull already applied (a culled mode reads back as
    /// three zeros and contributes nothing to the sum with no branch), so the
    /// summed set is not merely intended to be the bank's set - it IS the
    /// bank's set. Two `sqrt` per mode remain, and nothing else.
    ///
    /// MUST be called after `setModes`/`updateModes` has staged the mode set
    /// and before anything else re-targets the bank.
    ///
    /// @param outputGain the bank's own `outputGain_`, applied by
    ///        `applyOutputStage` (`:816-823`) downstream of the mode sum.
    [[nodiscard]] float modalGainBound(const ModalResonatorBank& bank, int count,
                                       float outputGain) const noexcept
    {
        const float* epsTargets = bank.getEpsilonTargets();
        const float* radiusTargets = bank.getRadiusTargets();
        const float* ampTargets = bank.getInputGainTargets();

        float sum = 0.0f;
        for (int k = 0; k < count; ++k) {
            const auto i = static_cast<std::size_t>(k);
            const float amp = ampTargets[i];
            if (amp <= 0.0f) {
                continue;  // culled by amplitude or by Nyquist
            }
            const float radius = radiusTargets[i];
            const float eps = epsTargets[i];

            const float cth = 1.0f - (0.5f * radius * eps * eps);
            const float c2th = (2.0f * cth * cth) - 1.0f;
            const float rSq = radius * radius;
            const float num =
                std::sqrt(std::max(1.0f - (2.0f * radius * cth) + rSq, 0.0f));
            const float den =
                std::sqrt(std::max(1.0f - (2.0f * radius * c2th) + rSq, 0.0f));
            const float oneMinusR = std::max(1.0f - radius, kGainBoundEps);
            if (den > 0.0f) {
                sum += amp * num / (oneMinusR * den);
            }
        }
        return std::max(sum * outputGain, kGainBoundEps);
    }

    /// @brief FR-032's `G-hat` for the waveguide slot.
    ///
    /// `G-hat = 1 / max(1 - gTotal, kGainBoundEps)` with
    /// `gTotal = rho * sqrt((1-S)^2 + 2S(1-S)cos w0 + S^2)` - the identical
    /// round-trip loss magnitude the header computes for its own excitation
    /// normalisation (`waveguide_string.h:379-382`), with
    /// `rho = 10^(-3/(T60_eff*f0))` (`:556-561`).
    ///
    /// `t60Eff` must be the value AFTER the `[kWgT60Min, kWgT60Max]` clamp
    /// (FR-035): the requested value would over-state the loop gain wherever the
    /// clamp binds.
    [[nodiscard]] float waveguideGainBound(float t60Eff, float sEff,
                                           float f0) const noexcept
    {
        const auto sr = static_cast<float>(sampleRate_);
        const float rho =
            exp10Fast(-3.0f / (std::max(t60Eff, kWgT60Min) * std::max(f0, 1.0f)));
        const float w0 = 2.0f * std::numbers::pi_v<float> * f0 / sr;
        const float s = std::clamp(sEff, 0.0f, 0.5f);
        const float oneMinusS = 1.0f - s;
        const float magSq = (oneMinusS * oneMinusS)
                            + (2.0f * s * oneMinusS * std::cos(w0)) + (s * s);
        const float gTotal = rho * std::sqrt(std::max(magSq, 0.0f));
        return 1.0f / std::max(1.0f - gTotal, kGainBoundEps);
    }

    /// FR-032's `G-hat` for the comb slot: `Sum_n 1/max(1 - fb_n, eps)` over the
    /// six active combs - the all-combs-in-phase worst case, the same shape as
    /// the modal sum.
    [[nodiscard]] float combGainBound(float t60Eff, float f0) const noexcept
    {
        float sum = 0.0f;
        for (std::size_t n = 0; n < kNumCombs; ++n) {
            const float fb = combFeedbackFor(n, t60Eff, f0);
            sum += 1.0f / std::max(1.0f - fb, kGainBoundEps);
        }
        return std::max(sum, kGainBoundEps);
    }

    /// Relative dirty test shared by every engine's gate. Relative because the
    /// quantities compared differ by up to eight orders of magnitude between
    /// materials (`b3` spans 1e-9 .. 5e-8, `T60` spans 0.05 .. 10).
    /// @param magnitudeFloor keeps the threshold non-zero the first time round,
    ///        when `applied` is still 0 - which is what makes the gate fire
    ///        unconditionally after a reset().
    [[nodiscard]] static bool relativelyDirty(float value, float applied,
                                              float magnitudeFloor) noexcept
    {
        return std::fabs(value - applied)
               > (kDampingEpsilonRel * std::max(applied, magnitudeFloor));
    }

    /// @brief FR-033a. Reset the excitation estimator to the state in which it
    ///        contributes NOTHING, i.e. FR-033's original law exactly.
    void clearExcitationComp() noexcept
    {
        excitationCompLog2_ = 0.0f;
        excitationComp_ = kMinExcitationComp;
        enginePeakAccum_ = 0.0f;
        couplingEnv_ = 0.0f;
        engineInEnv_ = 0.0f;
        estAppliedBodyHz_ = 0.0f;
        estSumSq_ = 0.0f;
        estChunks_ = 0;
    }

    /// @brief FR-033a - measure how far under its `Ĝ` bound the CURRENT
    ///        excitation actually drives the sounding engine, and correct for it.
    ///
    /// @par What is estimated, and why it is not a level compressor
    /// The estimated quantity is
    ///
    ///     A = enginePeak / (engineDrive * inputRms)
    ///
    /// - the engine's realised peak output per unit of (drive x input RMS). Both
    /// divisors are known exactly, so `A` is INVARIANT under the drive this
    /// function then sets (no loop gain: the plant is linear in the drive below
    /// its clipper) and INVARIANT under the input level. It is a measurement of
    /// the excitation's SPECTRAL coupling to the engine - the one term FR-032's
    /// bound cannot know - and nothing else. The correction is then computed in
    /// ONE step from that measurement,
    ///
    ///     excitationComp = Ĝ / (A * kTargetInputRms)
    ///
    /// which puts the engine at `kTargetPeak * userDrive` at the AGC's own
    /// operating point. `userDrive` divides out of `A` and reappears in the
    /// drive, so the Drive control keeps its full documented authority - it is
    /// NOT regulated away, and driving it to `kMaxUserDrive` still reaches
    /// saturation exactly as FR-033 documents.
    ///
    /// @par Why it is gated on the AGC
    /// With `setInputAgcEnabled(false)` the component's contract is a FIXED
    /// gain (SC-007(iii) measures exactly that: a 20 dB input drop must give a
    /// 20 dB output drop). `A`'s level-invariance means the estimator would not
    /// break that clause, but the honest reading of FR-034a is "no automatic
    /// level behaviour at all", so the estimate is forced to unity there and the
    /// AGC-off path is bit-for-bit what it was before FR-033a existed.
    ///
    /// @par The three HOLD conditions, and the ring-out they exist for
    /// The estimate is held - not zeroed, not decayed - whenever it cannot be
    /// measured honestly:
    ///   1. `rmsGain_ >= kMaxRmsGain`: the AGC has run out of range, so the
    ///      excitation is below the operating point the correction is defined
    ///      at. THIS IS THE ONE THAT MATTERS MUSICALLY. On note-off the cloud
    ///      decays in ~0.5 s while a modal body rings for `T60` seconds; without
    ///      this gate the estimator would see the output falling with no input to
    ///      explain it, read a rising `A`, and pump the tail back up. Holding
    ///      lets the tail decay at exactly the rate the damping law sets, which
    ///      is what SC-008 and `ContinuousBody_DecaysToSilence` measure.
    ///   2. `couplingEnv_ <= kEnginePeakFloor`: nothing has come out yet.
    ///   3. `drive <= kMinDriveGain`: the divisor is degenerate.
///   4. `f_body` moved across the window - see the in-body comment.
    void updateExcitationComp(float chunkRms) noexcept
    {
        if (!agcEnabled_) {
            // FR-034a: no automatic level behaviour at all. Unity, and the
            // window is abandoned rather than carried across the mode change.
            excitationCompLog2_ = 0.0f;
            excitationComp_ = kMinExcitationComp;
            enginePeakAccum_ = 0.0f;
            estSumSq_ = 0.0f;
            estChunks_ = 0;
            return;
        }

        // --- the measurement window ------------------------------------------
        //
        // FIXED IN TIME (kEstimatorWindowMs), NOT in control chunks, and that is
        // what makes the estimator sample-rate invariant. The statistic being
        // formed is a PEAK over an RMS, and the ratio of those two over a window
        // of `n` samples depends on how many CYCLES of the signal the window
        // spans: 64 samples is 0.29 cycles of a 220 Hz tone at 48 kHz and 0.15 at
        // 96 kHz, a systematic difference no amount of downstream smoothing
        // removes. MEASURED with the window pinned at one control chunk,
        // SC-018's 96 kHz-against-48 kHz steady-state RMS: 1.14 dB, against that
        // clause's 1.0 dB bound.
        //
        // The peak accumulator is filled by `renderSub` and consumed HERE and
        // only here, so it spans exactly the same samples as the RMS.
        estSumSq_ += chunkRms * chunkRms;
        ++estChunks_;
        if (estChunks_ < estWindowChunks_) {
            return;
        }
        const float windowPeak = enginePeakAccum_;
        const float windowRms = std::sqrt(estSumSq_ / static_cast<float>(estChunks_));
        enginePeakAccum_ = 0.0f;
        estSumSq_ = 0.0f;
        estChunks_ = 0;

        const Slot& slot = slots_[static_cast<std::size_t>(soundingSlot_)];
        const float drive = exp10Fast(slot.driveLog10.getCurrentValue());

        // --- the coupling ----------------------------------------------------
        //
        // *** THE RATIO IS FORMED FIRST AND ONLY THE RATIO IS FOLLOWED. ***
        // Every arrangement that follows the numerator and the denominator
        // separately closes a positive-feedback loop through the drive, because
        // the drive scales one side of a HELD quantity. Both were built and both
        // are unstable, in opposite directions:
        //
        //   held peak / instantaneous (drive x rms)
        //       A decrease of `excitationComp` shrinks the denominator at once
        //       while the held numerator does not move, so the ratio rises and
        //       the estimator decreases again. MEASURED: Chamber came back
        //       23.7 dB low on SC-007(iv)'s 20 dB input step (bound 6 dB).
        //   held peak / held (drive x rms)
        //       The mirror image upward - between two resonance crossings the
        //       held denominator remembers the largest recent drive, the ratio
        //       under-reads and the estimator climbs. MEASURED: Chamber's SC-001
        //       sweep reached body peak 12.16 with 2,045 FR-037 clamp
        //       engagements.
        //
        // Formed per window, the drive divides out exactly and there is no loop.
        //
        // THE DENOMINATOR IS LAGGED BY THE PLANT'S OWN TIME CONSTANT. The
        // numerator is the output of a resonator whose amplitude envelope trails
        // its excitation by `T60 / ln(1000)`; the raw engine input does not
        // trail at all, so dividing one by the other makes every level TRANSIENT
        // read as a coupling change. On SC-007(iv)'s step the input collapses at
        // once while the body is still ringing, the ratio spikes, and the fast
        // decrease cuts the drive on an artefact (MEASURED: Glass 14.1 dB low
        // against the same 6 dB bound). Lagging the denominator by the same
        // constant makes both sides move together, and it re-introduces no
        // feedback: `drive` scales numerator and denominator identically and
        // with the same delay.
        const float plantTauSec = std::max(estWindowSeconds_, slot.engineT60 / kT60OverB1);
        const float inAlpha = std::min(estWindowSeconds_ / plantTauSec, 1.0f);
        engineInEnv_ += inAlpha * ((drive * std::max(windowRms, kRmsFloor)) - engineInEnv_);
        const float windowCoupling = windowPeak / std::max(engineInEnv_, kRmsFloor);

        // A PEAK follower on the coupling - fast attack, slow release. The slow
        // release is what makes the estimator conservative rather than merely
        // average: a sweeping or mutating excitation crosses a resonance
        // occasionally and realises tens of dB more gain than it does between
        // crossings, and a follower that forgot the crossing faster than the body
        // rings would set the drive from the quiet intervals and be over-driven
        // at every subsequent one. The attack is a time constant rather than an
        // instantaneous max-hold because the windowed statistic still carries
        // noise, and a max-hold takes the largest excursion of it: MEASURED at
        // max-hold, Metal Plate landed 8.03 dB under `kTargetPeak` on SC-007a.
        const float couplingTauMs =
            (windowCoupling > couplingEnv_) ? kCouplingAttackMs : kEnginePeakReleaseMs;
        const float couplingAlpha =
            std::min(estWindowSeconds_ / (couplingTauMs * 1.0e-3f), 1.0f);
        couplingEnv_ += couplingAlpha * (windowCoupling - couplingEnv_);

        // Clause 4: the body pitch moved across this window. A coupling measured
        // while `f_body` is gliding belongs to neither the old pitch nor the new
        // one - every mode moves, `Ĝ` is recomputed under it, and the resonators
        // are mid-retune. Holding is also the only thing that keeps SC-004's
        // waveguide count clause inside `kGlideRetuneFloorFactor`, whose own
        // banner forbids raising that number: the estimator's motion on top of
        // `WaveguideString`'s block-rate retune took Strings from 44 detections
        // to 51 against a 28 control (ratio 1.82 against the 1.6 the phase pinned
        // and told the next reader NOT to raise). `Ĝ` still tracks the glide, so
        // the drive still follows the pitch throughout - only the excitation
        // correction, which the glide does not change, stays put.
        const float pitchRef = std::max(estAppliedBodyHz_, 1.0f);
        const bool pitchMoved =
            std::fabs(bodyHz_ - estAppliedBodyHz_) > (kEstimatorPitchEpsilon * pitchRef);
        estAppliedBodyHz_ = bodyHz_;
        if (pitchMoved) {
            return;
        }
        if (rmsGain_ >= kMaxRmsGain) {
            return;  // hold - see the banner's clause 1
        }
        if (!(drive > kMinDriveGain) || !(userDrive_ > kMinDriveGain)) {
            return;  // hold - clause 3, and setDrive(0) leaves nothing to normalise
        }
        const float coupling = couplingEnv_;
        if (!(coupling > kEnginePeakFloor)) {
            return;  // hold - clause 2: nothing has come out of the engine yet
        }

        // The target is stated in the domain the measurement lives in - AFTER
        // the engine's own output soft clip. FR-033's `kTargetPeak` is a PRE-clip
        // level (it is derived from `kEngineHeadroomFrac * kEngineClipThreshold`),
        // and at `userDrive = 1` the two agree to 0.03 dB, but at `kMaxUserDrive`
        // the intended pre-clip 0.9 images to 0.730 and regulating the post-clip
        // peak to 0.9 instead would ask for 2.8 dB more pre-clip level than the
        // design point - which is exactly SC-001(b)'s bound. Applying the forward
        // clip to the TARGET is also what makes runaway structurally impossible:
        // `softClip` is bounded by `kEngineClipThreshold`, so the estimator can
        // never chase a level the engine cannot produce.
        const float targetPeak = engineSoftClip(kTargetPeak * userDrive_);
        const float compTarget = std::clamp(
            (std::max(slot.gainBound, kGainBoundEps) * targetPeak)
                / (coupling * kTargetPeak * kTargetInputRms * userDrive_),
            kMinExcitationComp, kMaxExcitationComp);

        // FR-033a's ASYMMETRIC one-pole. An increase is slowed to the PLANT
        // whenever the plant is slower (see kEstimatorPlantFactor);
        // `engineT60 / kT60OverB1` is the resonator's own amplitude time
        // constant. A decrease runs at the fixed, fast
        // kExcitationCompDecreaseMs - see that constant for the sweep
        // measurement that forces the asymmetry.
        const float errorLog2 = std::log2(compTarget) - excitationCompLog2_;
        const float tauSec =
            (errorLog2 < 0.0f)
                ? std::max(kExcitationCompDecreaseMs * 1.0e-3f,
                           kEstimatorDecreasePlantFactor * slot.engineT60 / kT60OverB1)
                : std::max(kExcitationCompSmoothMs * 1.0e-3f,
                           kEstimatorPlantFactor * slot.engineT60 / kT60OverB1);
        const float alpha = std::min(estWindowSeconds_ / std::max(tauSec, 1.0e-6f),
                                     kMaxEstimatorAlpha);
        excitationCompLog2_ += alpha * errorLog2;
        excitationComp_ = std::clamp(std::exp2(excitationCompLog2_), kMinExcitationComp,
                                     kMaxExcitationComp);
    }

    /// FR-033: `engineDrive = clamp(kTargetPeak/G-hat, ...) * rmsGain * userDrive`,
    /// smoothed in log10 so the trajectory is a constant dB/ms slope across the
    /// 3-5 decades `G-hat` spans between engines.
    ///
    /// FR-033a inserts `excitationComp_` into the numerator. It is EXACTLY 1
    /// whenever the AGC is off or the estimator has never had anything to
    /// measure, so this expression is unchanged on every path that predates it.
    /// It is a per-COMPONENT scalar, not per-slot, deliberately: `Ĝ_slot` already
    /// carries everything that distinguishes the two slots, so a crossfade keeps
    /// its equal-power relationship and `snapSlotDrive` still lands the incoming
    /// engine at the right level on the first sample.
    [[nodiscard]] float engineDriveFor(const Slot& slot) const noexcept
    {
        const float comp =
            std::clamp((kTargetPeak * excitationComp_) / std::max(slot.gainBound, kGainBoundEps),
                       kMinDriveGain, kMaxDriveGain);
        return comp * rmsGain_ * userDrive_;
    }

    /// FR-033's SECOND drive: the decay cloud carries **no** `1/G-hat` term.
    /// `G-hat` corrects a resonator's own steady-state gain and has no referent
    /// when there is no resonator in the path (FR-063's bypass), so applying it
    /// there would attenuate the cloud by 3-5 decades and make SC-008's 30 s
    /// tail unmeasurable. Consumed by the decay-cloud path (T013).
    [[nodiscard]] float cloudDriveGain() const noexcept
    {
        return rmsGain_ * userDrive_;
    }

    void updateDrive() noexcept
    {
        for (Slot& slot : slots_) {
            slot.driveLog10.setTarget(
                std::log10(std::max(engineDriveFor(slot), kMinDriveGain)));
            slot.driveLog10.advanceSamples(kControlChunkSamples);
        }
    }

    /// FR-033: snap ONE slot's drive, at material assignment. Legal and
    /// clickless because the incoming slot is at zero crossfade gain at that
    /// instant (FR-024 step 1); NECESSARY because `G-hat` spans 3-5 decades
    /// between engines and a smoother crossing that span would over-drive the
    /// incoming engine by tens of dB for tens of ms.
    void snapSlotDrive(Slot& slot) noexcept
    {
        slot.driveLog10.snapTo(std::log10(std::max(engineDriveFor(slot), kMinDriveGain)));
    }

    void snapDrive() noexcept
    {
        for (Slot& slot : slots_) {
            snapSlotDrive(slot);
        }
    }

    /// @brief FR-037's last-resort guard on the post-crossfade engine sum,
    ///        applied AFTER the mix and BEFORE the decay cloud, mono only (the
    ///        resonator core is mono, spec A-1). One count per altered sample.
    ///
    /// **Read this before treating `getClampEngagementCount()` as evidence of
    /// anything.** Four of the five materials are bounded to +/-1.0 UPSTREAM of
    /// this clamp - modal via `applyOutputStage` -> `softClip`
    /// (`modal_resonator_bank.h:393`, `:816-823`), waveguide via
    /// `softClip(junction)` (`waveguide_string.h:181`) - and two equal-power
    /// crossfade gains sum to at most sqrt(2) ~= 1.414. The counter is therefore
    /// **structurally incapable of moving** for Glass, Metal Plate, Ice and
    /// Strings, whatever the drive law does. Only Chamber's comb bank has no
    /// output stage (`timevar_comb_bank.h:593-651` has only a per-comb
    /// non-finite reset and a denormal flush), so the counter is meaningful
    /// there and nowhere else. The measurable substitute for the other four is
    /// SC-001's pre-clip headroom clause (`kEngineHeadroomFrac`).
    ///
    /// Written as two one-sided tests rather than `std::clamp` plus an equality
    /// probe: a NaN fails both comparisons, so it passes through unaltered and
    /// UNCOUNTED, and `stateFinite()` remains the mechanism that observes it.
    [[nodiscard]] float clampEngineSum(float v) noexcept
    {
        if (v > kOutputClamp) {
            ++clampCount_;
            return kOutputClamp;
        }
        if (v < -kOutputClamp) {
            ++clampCount_;
            return -kOutputClamp;
        }
        return v;
    }

    /// @brief Render exactly `n` samples using the coefficients latched at the
    ///        last control step.
    ///
    /// @par What is wired at this stage of the build
    /// All three engines (FR-023), FR-033's drive scaling of the engine input,
    /// the per-slot crossfade gain sum (FR-024, latched once per control chunk by
    /// advanceCrossfade) and FR-037's output clamp. The decay-cloud loop is added
    /// by the task that owns it. With one sounding slot at gain 1.0 the sum below
    /// is bit-exactly that engine's own output (`0.0f + 1.0f * x == x`).
    ///
    /// @par The drive is constant inside a sub-chunk, by construction
    /// `driveLog10` is advanced ONLY at a control step (updateDrive), so its
    /// current value is identical for every sample of a control chunk however
    /// the host partitioned the block. That is what keeps FR-005a's block-size
    /// invariance exact on this path, and it is why there is no per-sample
    /// smoother here - one multiply per sample and nothing else.
    ///
    /// @par R-5 - `applyTransientEmphasis` is on this path and cannot be disabled
    /// `ModalResonatorBank::processBlock` calls `applyTransientEmphasis`
    /// (`modal_resonator_bank.h:386`, body at `:906-922`) for every sample, and
    /// there is NO setter to switch it off. It is a TIME-VARYING INPUT GAIN,
    /// `1 + kTransientEmphasisGain * max(0, d/dt |x|)` with a 5 ms envelope
    /// (`kTransientEmphasisGain = 4`, `:591`). Membrum needs it; a continuously
    /// excited body does not. At steady state the derivative tends to 0 and the
    /// factor to 1, but a steady sine leaves a residual ripple of about
    /// +0.06 dB at unit amplitude, and a 20 dB level step transiently adds about
    /// +0.55 dB. **If a later drive-law or level test misses by a fraction of a
    /// dB, the diagnosis is here** - not in FR-032's `G-hat` derivation.
    void renderSub(const float* inLeft, const float* inRight, const float* mono,
                   float* outLeft, float* outRight, std::size_t n) noexcept
    {
        std::fill_n(mixScratch_.data(), n, 0.0f);

        // FR-063's equal-power ramp between the resonator path and the direct
        // cloud drive. `bypassPos_` advances ONLY at a control step, so both
        // gains are constant across a sub-chunk however the host partitioned the
        // block - the same argument that keeps the drive block-size invariant.
        //
        // The `>= 1.0f` branch is not defensive noise: `equalPowerGains` is
        // (cos, sin) of `pos * pi/2` (`crossfade_utils.h:50-53`) and
        // `std::cos(kHalfPi)` is about -4.4e-8 in float, not 0. SC-016 requires
        // the engine counters to be EXACTLY flat once the bypass is engaged, so
        // the fully-bypassed state is tested for rather than inferred from a
        // gain that never quite reaches zero.
        float engineGain = 1.0f;
        float bypassGain = 0.0f;
        equalPowerGains(bypassPos_, engineGain, bypassGain);
        const bool engineActive = (bypassPos_ < 1.0f);
        if (!engineActive) {
            engineGain = 0.0f;
            bypassGain = 1.0f;
        }

        // FR-063: with the resonator fully bypassed NO engine is advanced at all
        // - the counters stay flat for every engine (SC-016), which is precisely
        // what makes the cloud's cost attributable in SC-005.
        if (engineActive) {
            // FR-023: for each ACTIVE slot, in slot order. An inactive slot is not
            // advanced AT ALL - not called with zero input, not called and
            // discarded. During a crossfade the OUTGOING slot is active with
            // `inputMuted`, so it rings out through its own damping law.
            for (std::size_t i = 0; i < kNumSlots; ++i) {
                Slot& slot = slots_[i];
                if (!slot.active) {
                    continue;
                }

                if (slot.inputMuted) {
                    std::fill_n(driveScratch_.data(), n, 0.0f);
                } else {
                    // FR-033: one multiply per sample with the value latched at
                    // the last control step. exp10Fast is
                    // std::exp2(x * log2(10)) - std::pow(10, x) is forbidden on
                    // this path.
                    const float drive = exp10Fast(slot.driveLog10.getCurrentValue());
                    for (std::size_t s = 0; s < n; ++s) {
                        driveScratch_[s] = mono[s] * drive;
                    }
                }

                if (!advanceSlot(slot, n)) {
                    continue;
                }
                for (std::size_t s = 0; s < n; ++s) {
                    mixScratch_[s] += slot.gain * engineScratch_[s];
                }
            }
        }

        // FR-037: the clamp is applied to the post-crossfade engine sum, mono
        // (the resonator core is mono, A-1), one count per altered sample. The
        // FR-063 blend sits AFTER it: the clamp guards the engine sum, and the
        // direct cloud-drive path has no resonator in it to guard.
        //
        // FR-033 splits here (spec Q1): the cloud drive carries `rmsGain *
        // userDrive` and NOT `1/G-hat`, because `G-hat` bounds a resonator's
        // steady-state gain and the bypassed path has no resonator. That is what
        // makes SC-008's tail start near full scale instead of ~94 dB down.
        // Computed unconditionally: `bypassGain` is EXACTLY 0 (`std::sin(0)`)
        // while no bypass is engaged, and the direct path has to fade in DURING
        // the ramp or the 10 ms toggle would be a dip rather than a crossfade.
        const float cloudDrive = cloudDriveGain();
        float enginePeak = enginePeakAccum_;
        for (std::size_t s = 0; s < n; ++s) {
            const float engineOut = clampEngineSum(mixScratch_[s]);
            const float engineMixed = engineGain * engineOut;
            // FR-033a's measurement point: the post-crossfade, post-clamp engine
            // output, which is the quantity `kTargetPeak` is defined on. The
            // direct cloud-drive path deliberately does NOT enter it - it has no
            // resonator, so it has no `Ĝ` to correct (spec Q1).
            //
            // `std::max(a, NaN)` returns `a` (`a < NaN` is false), so a poisoned
            // sample cannot latch the accumulator; `stateFinite()` remains the
            // mechanism that observes one.
            enginePeak = std::max(enginePeak, std::fabs(engineMixed));
            bodyScratch_[s] = engineMixed + (bypassGain * cloudDrive * mono[s]);
        }
        enginePeakAccum_ = enginePeak;

        // --- the decay cloud (FR-050 - FR-053a) ------------------------------
        // FR-053a: the cloud is skipped only when it is BOTH inaudible and
        // silent. Gating on the mix alone would truncate a ringing tail the
        // moment the mix was turned down; gating on the loop's own peak as well
        // means the tail always rings out and the saving only ever lands on a
        // cloud that has nothing left to say.
        const float cloudMix = std::clamp(cloudMixSmoother_.getCurrentValue(),
                                          kMinCloudMix, kMaxCloudMix);
        const bool cloudActive = !((cloudMix < kCloudBypassEpsilon)
                                   && (cloud_.lastPeak < kCloudSilenceFloor));
        if (cloudActive) {
            runCloud(bodyScratch_.data(), n);
        }

        // FR-053: equal-power parallel blend of the cloud against the DRY
        // resonator output - not a series stage.
        float cloudDryGain = 1.0f;
        float cloudWetGain = 0.0f;
        equalPowerGains(cloudMix, cloudDryGain, cloudWetGain);

        // FR-060: equal-power dry/processed mix. At mix = 0 `equalPowerGains`
        // returns (1, 0) EXACTLY, so the input is passed through bit-unchanged -
        // which is the endpoint SC's identity clause, and the reason the raw
        // per-channel input is carried in here rather than the mono sum.
        float dryGain = 1.0f;
        float wetGain = 0.0f;
        equalPowerGains(std::clamp(mixSmoother_.getCurrentValue(), kMinMix, kMaxMix),
                        dryGain, wetGain);

        // FR-038a's recovery gain. Advanced ONLY at a control step
        // (advanceStateRecovery), so it is constant across a sub-chunk however the
        // host partitioned the block - the same argument that keeps the drive and
        // the bypass ramp block-size invariant. It is exactly 1.0f whenever no
        // recovery is in flight, and `x * 1.0f == x` for every float including
        // NaN, so FR-060's mix = 0 identity is untouched.
        const float recovery = recoveryGain_;

        for (std::size_t s = 0; s < n; ++s) {
            const float body = bodyScratch_[s];
            const float wetL = cloudActive ? cloudWetL_[s] : 0.0f;
            const float wetR = cloudActive ? cloudWetR_[s] : 0.0f;
            // FR-062: the mono resonator output goes to BOTH channels; the
            // cloud's own decorrelation (the near-coprime 37/41 ms loops) plus
            // DiffusionNetwork::setWidth supplies the width.
            const float processedL =
                recovery * ((cloudDryGain * body) + (cloudWetGain * wetL));
            const float processedR =
                recovery * ((cloudDryGain * body) + (cloudWetGain * wetR));
            // The dry path is the RAW input, zero-substituted per sample. The
            // mono path's sticky FR-038 poison does not apply here: this branch
            // never reaches an engine, a follower or a feedback loop, so a
            // single bad sample cannot latch anything - but it must still not be
            // multiplied into the output.
            //
            // ============ WHY THE FINAL WRITE IS ALSO ZERO-SUBSTITUTED =========
            // SC-013(b) requires the output to be finite at EVERY sample of a
            // state-poisoning event, and FR-038a can only ACT at a control step -
            // i.e. at the end of the very chunk in which the state first went
            // non-finite. Those samples have already been computed by the time
            // `stateFinite()` is evaluated, so no ramp, however fast, can make
            // that clause true on its own; the guard here is what does.
            //
            // It is a FINITENESS guard and nothing else - not a second level
            // control, and not a substitute for FR-037's +/-2.0 clamp, which is on
            // the engine sum and still counts every engagement. It is also NOT an
            // alternative to FR-038a: the poisoned STATE is still in the loop and
            // would emit substituted zeros forever, which is precisely the
            // "unrecoverable latch" R-13 names. This makes the symptom inaudible;
            // `advanceStateRecovery` is what removes the cause.
            outLeft[s] = substituteNonFinite((dryGain * substituteNonFinite(inLeft[s]))
                                             + (wetGain * processedL));
            outRight[s] = substituteNonFinite((dryGain * substituteNonFinite(inRight[s]))
                                              + (wetGain * processedR));
        }
    }

    /// @brief One sub-chunk of the FR-050 loop:
    ///        `in -> [+] -> DelayLine -> DiffusionNetwork -> OnePoleLP -> DCBlocker -> out`,
    ///        tapped back through `x fb`.
    ///
    /// Leaves the damped, DC-blocked loop output in `cloudWetL_`/`cloudWetR_`.
    ///
    /// @par Why this runs HERE and not in controlStep (plan 9.1 governs over 11)
    /// `controlStep` fires only on the ABSOLUTE 64-sample grid, so a loop placed
    /// there would emit no cloud output at all for any sub-64 tail - exactly the
    /// 1023+1, 100+...+24 and 7x146+2 partitions the block-size criterion exists
    /// to catch. `controlStep` owns the cloud's COEFFICIENTS (fb, damping cutoff,
    /// the smoother advances); this owns its audio.
    ///
    /// @par Batched read BEFORE any write
    /// `DiffusionNetwork::process` (`diffusion_network.h:327-329`) is a block
    /// entry point with no per-sample form, so the whole sub-chunk is read out of
    /// the delay lines first. That is causal by a wide margin: the shortest loop
    /// is 37 ms (1776 samples at 48 kHz, 296 even at 8 kHz) and `cloudChunkCap_`
    /// caps every sub-chunk at `min(loopSamplesL, loopSamplesR, 64)`, so
    /// `n <= loopSamples` holds by construction and `tap[s]` is always a sample
    /// written before this call.
    ///
    /// @par The explicit flushDenormal on the feedback write is MANDATORY (R-6)
    /// A 30 s tail decays toward 1e-30 and `DelayLine` has no flush of its own -
    /// unlike `OnePoleLP`, `OnePoleSmoother` and the comb bank, all of which call
    /// `detail::flushDenormal` internally. Note that it is a denormal guard ONLY:
    /// `db_utils.h:168` returns NaN and Inf unchanged (both comparisons are false
    /// for a NaN), so it is never a finiteness guard.
    void runCloud(const float* in, std::size_t n) noexcept
    {
        for (std::size_t s = 0; s < n; ++s) {
            // Oldest first: read(d) = buffer[(write - 1 - d) & mask], so
            // `loopSamples - 1 - s` walks forward in time as `s` advances and
            // every tap sits exactly `loopSamples` behind its own write.
            cloudTapL_[s] = cloud_.delayL.read(cloud_.loopSamplesL - 1 - s);
            cloudTapR_[s] = cloud_.delayR.read(cloud_.loopSamplesR - 1 - s);
        }

        cloud_.diffusion.process(cloudTapL_.data(), cloudTapR_.data(),
                                 cloudWetL_.data(), cloudWetR_.data(), n);

        // The SMOOTHED feedback gain, not the target: `setCloudDecaySec` and
        // `setCloudSize` both move `fb` by a step, and 50 ms of glide is what
        // keeps that step out of the audio (FR-052, FR-006).
        const float fbL = fbLSmoother_.getCurrentValue();
        const float fbR = fbRSmoother_.getCurrentValue();

        float peak = cloudPeakAccum_;
        float lastWriteL = cloud_.lastWriteL;
        float lastWriteR = cloud_.lastWriteR;
        for (std::size_t s = 0; s < n; ++s) {
            const float wetL = cloud_.dcL.process(cloud_.dampL.process(cloudWetL_[s]));
            const float wetR = cloud_.dcR.process(cloud_.dampR.process(cloudWetR_[s]));
            cloudWetL_[s] = wetL;
            cloudWetR_[s] = wetR;
            peak = std::fmax(peak, std::fmax(std::fabs(wetL), std::fabs(wetR)));
            const float writeL = detail::flushDenormal(in[s] + (fbL * wetL));
            const float writeR = detail::flushDenormal(in[s] + (fbR * wetR));
            cloud_.delayL.write(writeL);
            cloud_.delayR.write(writeR);
            lastWriteL = writeL;
            lastWriteR = writeR;
        }
        // FR-007 / plan 7.8.1: the cloud's state observation. The WRITE is
        // where the poison first appears - `detail::flushDenormal` is a
        // DENORMAL guard and returns NaN/Inf unchanged (`db_utils.h:168`) - and
        // the TAP is where it comes back a loop time later. Both are captured,
        // so a single overflowing write is seen on the chunk it happened rather
        // than 37 ms afterwards.
        //
        // Latched from a local AFTER the loop rather than stored through
        // `cloud_` on every sample: the loop overwrote the member each
        // iteration, so only the final value ever survived, and the store was
        // two per sample of pure loop-carried memory traffic.
        cloud_.lastWriteL = lastWriteL;
        cloud_.lastWriteR = lastWriteR;
        cloud_.lastTapL = cloudTapL_[n - 1];
        cloud_.lastTapR = cloudTapR_[n - 1];
        cloudPeakAccum_ = peak;
    }

    /// @brief Advance ONE active slot's engine over `n` samples, reading
    ///        `driveScratch_` and writing `engineScratch_` (FR-023).
    ///
    /// @return true when the engine was advanced (and counted); false when the
    ///         slot has nothing to run, in which case `engineScratch_` is left
    ///         untouched and the caller must not mix it in.
    ///
    /// `engineSampleCount_[engine] += n` happens exactly once per active slot per
    /// sub-chunk - the functional evidence SC-016 asserts on, and the only thing
    /// that can distinguish "not advanced" from "advanced with zero input".
    [[nodiscard]] bool advanceSlot(Slot& slot, std::size_t n) noexcept
    {
        switch (slot.engine) {
            case Engine::Modal: {
                if (slot.modalIndex < 0 || slot.modeCount <= 0) {
                    return false;
                }
                // processBlock is the ONLY SIMD path (`:382-396`, kernel at
                // `:389-391`) and smooths coefficients once per call (`:384`).
                // NEVER ModalResonatorBank::process()/processSample (`:372-376`,
                // `:519-528`): they smooth PER SAMPLE and run the scalar core at
                // `:841`, bypassing the kernel entirely.
                // getControlEnergy()/getPerceptualEnergy() are updated only inside
                // process() (`:521-527`) and are stale here - nothing on this path
                // reads them.
                modal_[static_cast<std::size_t>(slot.modalIndex)].processBlock(
                    driveScratch_.data(), engineScratch_.data(), static_cast<int>(n));
                break;
            }
            case Engine::Waveguide: {
                // `process(float)` (`waveguide_string.h:154`) is the string's ONLY
                // processing entry point - there is no block form.
                //
                // The `- driveScratch_[s]` is the FR-060 dry-leak subtraction; see
                // the banner on `kCombDirectGain` for the measurement and the
                // argument. The string's output tap is the summing junction,
                // `output = softClip(feedback + excitation)`
                // (`waveguide_string.h:178-181`), so its return value carries the
                // excitation at UNITY on top of the resonated signal.
                //
                // `waveguideTuned_` is not defensive noise: `reset()` calls
                // `silence()`, which sets `bridgeDelayFloat_ = 0` (`:243`), and
                // `process()` then early-returns 0 (`:156`) until the NEXT control
                // step retunes - and `renderSub` runs BEFORE `controlStep`
                // (`processStereoBlock` step 2 vs step 3). Subtracting through that
                // window would emit `-x*drive`, i.e. a phase-INVERTED copy of the
                // dry input from a body that is supposed to be silent, for up to
                // one control chunk after every reset().
                if (waveguideTuned_) {
                    for (std::size_t s = 0; s < n; ++s) {
                        engineScratch_[s] =
                            waveguide_.process(driveScratch_[s]) - driveScratch_[s];
                    }
                } else {
                    for (std::size_t s = 0; s < n; ++s) {
                        engineScratch_[s] = waveguide_.process(driveScratch_[s]);
                    }
                }
                break;
            }
            case Engine::Comb:
            default: {
                // The MONO `process(float)` (`timevar_comb_bank.h:328`, impl
                // `:593-651`), never `processStereo` (`:653`): a stereo comb output
                // would fork FR-037's mono clamp and its per-sample counter for one
                // material, and `processStereo`'s only extra benefit is the
                // `stereoSpread` pan (`:715-716`) this component declines (D-12).
                //
                // `- kCombDirectGain * driveScratch_[s]`: FR-060 dry-leak
                // subtraction, six times over - see `kCombDirectGain`.
                //
                // `processBlock` (`timevar_comb_bank.h`), not a per-sample
                // `process()` loop: it is the block form of the SAME mono path
                // (it delegates to `process()` sample by sample whenever
                // anything is moving) and it hoists 24 smoother steps and 18
                // setter calls per sample out of the inner loop once the bank
                // has settled. `driveScratch_` and `engineScratch_` are
                // distinct buffers, which is that entry point's one
                // precondition.
                comb_.processBlock(driveScratch_.data(), engineScratch_.data(), n);
                for (std::size_t s = 0; s < n; ++s) {
                    engineScratch_[s] -= kCombDirectGain * driveScratch_[s];
                }
                break;
            }
        }
        // FR-007 / plan 7.8.1: the belt-and-braces engine observable. Taken from
        // the scratch buffer rather than from an accessor because the comb bank
        // has none, and `n >= 1` always holds here (`processStereoBlock` returns
        // early on a zero-length block, and every sub-chunk is at least one
        // sample by construction - see `cloudChunkCap_`'s floor in prepare()).
        slot.lastEngineSample = engineScratch_[n - 1];
        engineSampleCount_[static_cast<std::size_t>(slot.engine)] += n;
        return true;
    }

    /// FR-005a clause 3: the control step runs in this FIXED order.
    void controlStep(float chunkRms) noexcept
    {
        // 1. RMS-follower advance (FR-034) -> rmsGain.
        //    The clamp is load-bearing: `processRMS` squares in float
        //    (envelope_follower.h:313) and a non-finite squaredEnvelope_ latches
        //    forever (:316-321).
        const float followerIn = std::min(chunkRms, kMaxFollowerInput);
        inputRms_ = rmsFollower_.processSample(followerIn);
        rmsGain_ = agcEnabled_
                       ? std::clamp(kTargetInputRms / std::max(inputRms_, kRmsFloor),
                                    kMinRmsGain, kMaxRmsGain)
                       : 1.0f;

        // 1b. FR-033a's excitation compensation. BEFORE step 3, so the drive
        //     recomputed below already carries this chunk's estimate.
        updateExcitationComp(followerIn);

        // 2. G-hat recompute (FR-032). It does NOT get its own pass: it is
        //    computed at material assignment (assignSoundingSlotEngine) and
        //    recomputed inside step 5's retune, on exactly the dirty gate that
        //    fires `updateModes` - computing it unconditionally would cost two
        //    `sqrt`, one `exp` and one `sin` per mode on every control step.
        //    Consequence, stated rather than discovered: a G-hat that moves in
        //    step 5 is consumed by step 3 of the NEXT control chunk, i.e. 1.33 ms
        //    later at 48 kHz. That is inside the 50 ms log10 drive smoother and
        //    invisible to SC-007/SC-015, both of which measure steady state; the
        //    case that actually matters - a material change - snaps the drive at
        //    assignment with the fresh bound and never waits.

        // 3. Drive recompute + the log10 smoother advance (FR-033).
        updateDrive();

        // 4. Key-track retune: the pitch smoothers advance, then f_body
        //    (FR-040 - FR-042). The dirty gates and the engine apply calls are
        //    owned by the per-engine tasks.
        keyTrackSmoother_.advanceSamples(kControlChunkSamples);
        noteLog2Smoother_.advanceSamples(kControlChunkSamples);
        updateBodyPitch();

        // 5. Resonance / damping apply (FR-036, FR-042a) and the FR-041/FR-042
        //    state-preserving retune, OR-ed into at most ONE updateModes call
        //    per active slot per control step.
        updateEngineTargets();
        applyEngineRetune();

        // 6. Crossfade / collapse position advance (FR-024, FR-024a). Runs AFTER
        //    the retune so a collapse that completes here configures its new
        //    incoming engine at the f_body step 4 just latched.
        advanceCrossfade();
        mixSmoother_.advanceSamples(kControlChunkSamples);

        // 6b. FR-063's resonator-bypass ramp, on the same 10 ms law. After the
        //     crossfade, so a bypass engaged mid-fade silences whatever the fade
        //     left active rather than an engine the fade is about to retire.
        advanceBypass();

        // 7. Decay-cloud COEFFICIENT UPDATE ONLY (FR-050). The read / diffuse /
        //    damp / DC-block / write pass runs inside renderSub over subChunk
        //    samples - the control step fires only on the absolute 64-grid, so
        //    running the loop here would emit no cloud output for any sub-64
        //    tail (plan 9.1).
        cloudMixSmoother_.advanceSamples(kControlChunkSamples);
        cloudSizeSmoother_.advanceSamples(kControlChunkSamples);
        cloudDampLog2Smoother_.advanceSamples(kControlChunkSamples);
        widthSmoother_.advanceSamples(kControlChunkSamples);
        fbLSmoother_.advanceSamples(kControlChunkSamples);
        fbRSmoother_.advanceSamples(kControlChunkSamples);
        applyCloudDampingCutoff();
        applyCloudGeometry();

        // 8. Cloud-bypass evaluation (FR-053a). The loop's peak is latched ONCE
        //    per control chunk from the peak `renderSub` accumulated over that
        //    chunk's sub-chunks, so the "previous control chunk" the bypass test
        //    reads is exact however the host partitioned the block.
        cloud_.lastPeak = cloudPeakAccum_;
        cloudPeakAccum_ = 0.0f;

        // 9. stateFinite() evaluation and the FR-038a recovery. LAST, and after
        //    the engines have been advanced (renderSub runs before controlStep),
        //    so the predicates see the state this chunk actually produced.
        advanceStateRecovery();
    }

    // --- the three private stateFinite() predicates (FR-007) -----------------

    /// @brief One slot's engine state (plan 7.8.1).
    ///
    /// **THE OBSERVABLE MUST NOT BE A SATURATED ONE.** This is the defect an
    /// earlier draft of this component shipped, and it is why `getPerceptualEnergy`
    /// is NOT read here for either engine that has one. Every output-side
    /// observable on this path runs through a saturator that maps overflow to a
    /// FINITE value: `softClip(+Inf)` returns `1.0f` outright
    /// (`dsp_utils.h:107`, `if (sample > 3.0f) return 1.0f;`),
    /// `ModalResonatorBank::processBlock` ends every sample with
    /// `applyOutputStage` = `softClip(x/t)*t`, and
    /// `WaveguideString::feedbackVelocity_` is assigned the POST-`softClip` value
    /// (`waveguide_string.h:181`). A bank whose `sinState_`/`cosState_` had gone
    /// to +/-Inf would read out as exactly 1.0 and this predicate would say
    /// "finite".
    ///
    /// `getModalEnergy()` (`modal_resonator_bank.h:442`) is therefore the modal
    /// observable: it is `Sum_k sin^2 + cos^2` computed DIRECTLY from the mode
    /// states (`:444-447`), never through `outputGain_` and never through the
    /// clipper - the header says so at `:434-437`. It is the only public window
    /// on the bank's raw state.
    ///
    /// The waveguide and comb rows are belt-and-braces only: the string's loop is
    /// self-bounding (`output = softClip(junction)` is what is written back into
    /// the delay, `waveguide_string.h:181`, `:205`), and the comb bank resets any
    /// comb whose own output goes non-finite and returns 0
    /// (`timevar_comb_bank.h:637-641`), i.e. it self-heals.
    [[nodiscard]] bool engineStateFinite(int slotIndex) const noexcept
    {
        const Slot& slot = slots_[static_cast<std::size_t>(slotIndex)];
        if (!isFiniteBits(slot.gain) || !isFiniteBits(slot.gainBound)
            || !isFiniteBits(slot.engineT60) || !smootherFinite(slot.driveLog10)
            || !isFiniteBits(slot.lastEngineSample)) {
            return false;
        }
        switch (slot.engine) {
            case Engine::Modal:
                if (slot.modalIndex >= 0
                    && !isFiniteBits(modal_[static_cast<std::size_t>(slot.modalIndex)]
                                         .getModalEnergy())) {
                    return false;
                }
                break;
            case Engine::Waveguide:
                if (!isFiniteBits(waveguide_.getFeedbackVelocity())) {
                    return false;
                }
                break;
            case Engine::Comb:
            default:
                // `lastEngineSample` above IS the comb observation - the bank has
                // no energy accessor of any kind.
                break;
        }
        return true;
    }

    /// @brief The decay cloud's state - the one subsystem a FINITE input can
    ///        genuinely poison (plan 7.8.2).
    ///
    /// On the FR-063 bypass path the mono-summed input is scaled by
    /// `cloudDrive = rmsGain * userDrive` and fed straight in: FR-037's +/-2.0
    /// clamp is on the ENGINE sum and does not cover it, and FR-033's `1/G-hat`
    /// is deliberately absent (there is no resonator to compensate). That
    /// asymmetry is a real property of the design, not an oversight - clamping
    /// the user's own excitation path would make FR-038a unreachable, i.e. would
    /// turn the whole state-recovery mechanism into dead code no test could
    /// exercise.
    [[nodiscard]] bool cloudStateFinite() const noexcept
    {
        return isFiniteBits(cloud_.fbL) && isFiniteBits(cloud_.fbR)
               && isFiniteBits(cloud_.loopSecondsL) && isFiniteBits(cloud_.loopSecondsR)
               && isFiniteBits(cloud_.lastPeak) && isFiniteBits(cloud_.lastTapL)
               && isFiniteBits(cloud_.lastTapR) && isFiniteBits(cloud_.lastWriteL)
               && isFiniteBits(cloud_.lastWriteR) && smootherFinite(fbLSmoother_)
               && smootherFinite(fbRSmoother_) && smootherFinite(cloudSizeSmoother_)
               && smootherFinite(cloudDampLog2Smoother_) && smootherFinite(cloudMixSmoother_);
    }

    /// @brief The control layer's state. `rmsFollower_` is the one that LATCHES
    ///        (R-13): `processRMS` squares in float (`envelope_follower.h:313`)
    ///        and only `reset()`/`prepare()` clears a non-finite
    ///        `squaredEnvelope_` - `detail::flushDenormal` at `:184-185` passes
    ///        Inf straight through (`db_utils.h:168`). It is observed here AND
    ///        cleared by the recovery set, which is what keeps it out of R-13's
    ///        "observed but never cleared" latch class.
    [[nodiscard]] bool controlStateFinite() const noexcept
    {
        return isFiniteBits(chunkSumSq_) && isFiniteBits(inputRms_) && isFiniteBits(rmsGain_)
               && isFiniteBits(excitationCompLog2_) && isFiniteBits(excitationComp_)
               && isFiniteBits(couplingEnv_) && isFiniteBits(enginePeakAccum_)
               && isFiniteBits(engineInEnv_) && isFiniteBits(estSumSq_)
               && isFiniteBits(estAppliedBodyHz_)
               && isFiniteBits(bodyHz_) && isFiniteBits(crossfadePos_)
               && isFiniteBits(collapsePos_) && isFiniteBits(collapseBasePos_)
               && isFiniteBits(bypassPos_) && isFiniteBits(bypassBasePos_)
               && isFiniteBits(rmsFollower_.getCurrentValue())
               && smootherFinite(keyTrackSmoother_)
               && smootherFinite(noteLog2Smoother_) && smootherFinite(mixSmoother_)
               && smootherFinite(widthSmoother_);
    }

    // =========================================================================
    // FR-038a - the state-recovery ramp (plan 7.8.3)
    //
    // THE RULE: a state `stateFinite()` observes and the recovery below does not
    // clear is an UNRECOVERABLE LATCH BY CONSTRUCTION (risk R-13). Every row of
    // the three predicates above therefore appears in exactly one of the three
    // clearing helpers, and `reset()` runs the same helpers unconditionally so
    // the two paths cannot drift.
    // =========================================================================

    /// Where the FR-038a ramp is. `Idle` is the only state in which the component
    /// is at full output.
    enum class RecoveryPhase : std::uint8_t { Idle = 0, RampDown, RampUp };

    /// Snap one smoother to a known-good value if EITHER its current value or its
    /// target reads non-finite. `snapTo` is used rather than `snapToTarget`
    /// because the target is one of the two things that may be poisoned.
    static void snapSmootherIfNonFinite(OnePoleSmoother& s, float fallback) noexcept
    {
        if (!smootherFinite(s)) {
            s.snapTo(fallback);
        }
    }

    /// @brief The cloud's clearing set (FR-038a clause 2). Shared verbatim with
    ///        `reset()` and `prepare()`.
    void clearCloudState() noexcept
    {
        cloud_.delayL.reset();
        cloud_.delayR.reset();
        cloud_.diffusion.reset();
        cloud_.dampL.reset();
        cloud_.dampR.reset();
        cloud_.dcL.reset();
        cloud_.dcR.reset();
        cloud_.lastPeak = 0.0f;
        cloud_.lastTapL = 0.0f;
        cloud_.lastTapR = 0.0f;
        cloud_.lastWriteL = 0.0f;
        cloud_.lastWriteR = 0.0f;
        cloudPeakAccum_ = 0.0f;

        // The derived coefficients are state too, and `cloudStateFinite()`
        // observes them. Rebuild them from the (always-clamped, always-finite)
        // parameters rather than trusting whatever is there.
        updateCloudDerived();
        snapSmootherIfNonFinite(fbLSmoother_, cloud_.fbL);
        snapSmootherIfNonFinite(fbRSmoother_, cloud_.fbR);
        snapSmootherIfNonFinite(cloudSizeSmoother_, cloudSize_);
        snapSmootherIfNonFinite(cloudMixSmoother_, cloudMix_);
        if (!smootherFinite(cloudDampLog2Smoother_)) {
            updateCloudDampingTarget();
            cloudDampLog2Smoother_.snapToTarget();
        }
        applyCloudDampingCutoff();
    }

    /// @brief The control layer's clearing set (FR-038a clause 3).
    ///
    /// Runs BEFORE the engine clause, which is a deviation from the plan's
    /// numbering and a deliberate one: the engine re-tune below reads `bodyHz_`,
    /// and `WaveguideString::retune` neither rejects nor repairs a non-finite
    /// argument (`f0 < kMinFrequency` is FALSE for NaN, and `std::clamp(NaN,..)`
    /// returns NaN, `waveguide_string.h:501-503`, `:508`). Re-tuning to a poisoned
    /// pitch would write NaN into `bridgeDelayFloat_` and brick the string on the
    /// path that exists to un-brick it. The plan's 1/2/3 are independent clauses,
    /// not an order.
    void recoverControlState() noexcept
    {
        rmsFollower_.reset();
        inputRms_ = 0.0f;
        chunkSumSq_ = 0.0;
        // FR-033a's estimator is patched, NOT cleared. `excitationComp_` is a
        // property of the excitation's coupling to the engine, not of the
        // poisoned state, and clearing it drops the body ~40 dB and then walks
        // it back up over seconds - an artefact far larger and far longer than
        // the poisoning event FR-038a exists to hide. MEASURED with the clearing
        // form, SC-012's poisoned-input ensemble on Strings: 51 detections
        // against a 28 control. Only the parts that are actually non-finite are
        // reset; the window is restarted either way, because its accumulator
        // spans samples the engine is about to have silenced.
        if (!isFiniteBits(excitationCompLog2_) || !isFiniteBits(excitationComp_)) {
            excitationCompLog2_ = 0.0f;
            excitationComp_ = kMinExcitationComp;
        }
        if (!isFiniteBits(couplingEnv_)) {
            couplingEnv_ = 0.0f;
        }
        if (!isFiniteBits(engineInEnv_)) {
            engineInEnv_ = 0.0f;
        }
        if (!isFiniteBits(estAppliedBodyHz_)) {
            estAppliedBodyHz_ = 0.0f;
        }
        enginePeakAccum_ = 0.0f;
        estSumSq_ = 0.0f;
        estChunks_ = 0;

        snapSmootherIfNonFinite(keyTrackSmoother_, keyTracking_);
        snapSmootherIfNonFinite(noteLog2Smoother_, std::log2(noteHz_));
        snapSmootherIfNonFinite(mixSmoother_, mix_);
        snapSmootherIfNonFinite(widthSmoother_, width_);

        if (!isFiniteBits(crossfadePos_)) {
            crossfadePos_ = 0.0f;
        }
        if (!isFiniteBits(collapsePos_)) {
            collapsePos_ = 0.0f;
        }
        if (!isFiniteBits(collapseBasePos_)) {
            collapseBasePos_ = 0.0f;
        }
        if (!isFiniteBits(bypassPos_)) {
            bypassPos_ = resonatorBypass_ ? 1.0f : 0.0f;
        }
        if (!isFiniteBits(bypassBasePos_)) {
            bypassBasePos_ = bypassPos_;
        }

        // `bodyHz_` is derived, so it is REBUILT rather than patched - the two
        // smoothers it reads are finite by the time we get here.
        updateBodyPitch();

        // The AGC gain and, through it, every slot's drive were derived from the
        // poisoned follower. Recompute the gain from the post-reset follower and
        // SNAP the drives to it: gliding to a value that is now 3-5 decades away
        // is the over-drive `snapSlotDrive` exists to prevent, and we are at zero
        // gain here so the snap is inaudible.
        rmsGain_ = agcEnabled_
                       ? std::clamp(kTargetInputRms / std::max(inputRms_, kRmsFloor),
                                    kMinRmsGain, kMaxRmsGain)
                       : 1.0f;
        snapDrive();
    }

    /// @brief One slot's clearing set (FR-038a clause 1), INCLUDING plan 10.1's
    ///        mandatory re-tune.
    ///
    /// `WaveguideString::silence()` sets `bridgeDelayFloat_ = 0.0f`
    /// (`waveguide_string.h:243`) and `process()` early-returns `0.0f` for every
    /// sample below `kMinDelaySamples` (`:156`). The field is written in exactly
    /// three places - `silence()`, `noteOn()` (`:325`) and RA-1's `retune()` -
    /// and FR-042's `pitchDirty` gate cannot rescue it because no silence path
    /// moves the pitch. A silenced-and-not-retuned string is bricked silently,
    /// permanently, and passes every clickless criterion in the suite: digital
    /// silence has no clicks. Hence the `retune` immediately below the `silence`.
    void recoverEngineSlot(Slot& slot, std::size_t slotIndex) noexcept
    {
        silenceSlotEngine(slot);
        if (slot.engine == Engine::Waveguide) {
            waveguide_.retune(bodyHz_);  // plan 10.1 path 1 - NOT optional
            waveguideTuned_ = true;
        }
        slot.lastEngineSample = 0.0f;

        if (!isFiniteBits(slot.gain)) {
            slot.gain = (static_cast<int>(slotIndex) == soundingSlot_) ? 1.0f : 0.0f;
        }
        if (!isFiniteBits(slot.gainBound)) {
            slot.gainBound = 1.0f;
        }
        if (!isFiniteBits(slot.engineT60)) {
            slot.engineT60 = kWgT60Min;
        }

        // Clear the applied-value shadows, exactly as `reset()` does: the next
        // control step's FR-042/FR-042a dirty gates then fire unconditionally and
        // rebuild the mode set, the damping law and `G-hat` from scratch. This is
        // plan 10.1 path 3's mechanism, reused - the two paths cannot drift
        // because they clear the same fields.
        slot.appliedBodyHz = 0.0f;
        slot.appliedB1 = 0.0f;
        slot.appliedB3 = 0.0f;
        slot.appliedT60 = 0.0f;
        slot.appliedS = 0.0f;
        slot.appliedCombFb.fill(0.0f);
        slot.appliedCombDamp = 0.0f;

        snapSlotDrive(slot);
    }

    /// @brief Run only the clauses whose own predicate failed (FR-038a clause 2's
    ///        "only if the cloud's own state is what went non-finite" - which is
    ///        exactly why `stateFinite()` is composed rather than aggregate).
    void runRecoveryActions() noexcept
    {
        if (!controlStateFinite()) {
            recoverControlState();
        }
        for (std::size_t i = 0; i < kNumSlots; ++i) {
            if (!engineStateFinite(static_cast<int>(i))) {
                recoverEngineSlot(slots_[i], i);
            }
        }
        if (!cloudStateFinite()) {
            clearCloudState();
        }
    }

    /// @brief FR-038a, control step 9: evaluate `stateFinite()` and drive the
    ///        equal-power ramp down / clear / ramp back up.
    ///
    /// Shares `collapseInc_` and `collapseTotalSamples_` with FR-024a's collapse
    /// and FR-063's bypass, so all three ramps run over exactly `kSlotReleaseMs`
    /// and cannot drift apart. Position is `samples * increment` and never a float
    /// accumulator, for the reason spelled out on `advanceCrossfade`.
    ///
    /// The `silence()` in `runRecoveryActions` therefore only ever happens at
    /// `recoveryGain_ == 0`, which is the invariant this component holds
    /// everywhere: a ringing engine is never cut at audible gain.
    void advanceStateRecovery() noexcept
    {
        const bool finite = stateFinite();

        switch (recoveryPhase_) {
            case RecoveryPhase::Idle:
                if (!finite) {
                    recoveryPhase_ = RecoveryPhase::RampDown;
                    recoverySamples_ = 0;
                }
                return;

            case RecoveryPhase::RampDown: {
                recoverySamples_ += static_cast<std::uint64_t>(kControlChunkSamples);
                if (recoverySamples_ >= collapseTotalSamples_) {
                    recoveryGain_ = 0.0f;
                    runRecoveryActions();
                    recoveryPhase_ = RecoveryPhase::RampUp;
                    recoverySamples_ = 0;
                    return;
                }
                const float t =
                    std::min(1.0f, collapseInc_ * static_cast<float>(recoverySamples_));
                float down = 1.0f;
                float up = 0.0f;
                equalPowerGains(t, down, up);
                recoveryGain_ = down;
                return;
            }

            case RecoveryPhase::RampUp:
            default: {
                if (!finite) {
                    // Still poisoned - the fault is ongoing rather than a single
                    // event. HOLD at zero rather than ramping up into it, and
                    // re-run the clearing set once per `kSlotReleaseMs` until it
                    // takes. Retrying on EVERY control step instead would call
                    // `DiffusionNetwork::reset()` ~750 times a second, which is a
                    // memset of eight stage buffers on the audio thread - RT-legal
                    // but pointlessly expensive, and no faster: the state cannot
                    // come clean while the input that poisons it is still
                    // arriving.
                    recoveryGain_ = 0.0f;
                    recoverySamples_ += static_cast<std::uint64_t>(kControlChunkSamples);
                    if (recoverySamples_ >= collapseTotalSamples_) {
                        runRecoveryActions();
                        recoverySamples_ = 0;
                    }
                    return;
                }
                recoverySamples_ += static_cast<std::uint64_t>(kControlChunkSamples);
                if (recoverySamples_ >= collapseTotalSamples_) {
                    recoveryGain_ = 1.0f;
                    recoveryPhase_ = RecoveryPhase::Idle;
                    recoverySamples_ = 0;
                    return;
                }
                const float t =
                    std::min(1.0f, collapseInc_ * static_cast<float>(recoverySamples_));
                float down = 1.0f;
                float up = 0.0f;
                equalPowerGains(t, down, up);
                recoveryGain_ = up;
                return;
            }
        }
    }

    // =========================================================================
    // State
    // =========================================================================

    /// The comb bank's maximum delay: 50 ms = the 20 Hz worst case.
    static constexpr float kCombMaxDelayMs = 50.0f;

    // --- engines --------------------------------------------------------------
    std::array<ModalResonatorBank, 2> modal_{};
    WaveguideString waveguide_{};
    TimeVaryingCombBank comb_{};
    DecayCloud cloud_{};
    EnvelopeFollower rmsFollower_{};

    // --- slots / crossfade ----------------------------------------------------
    std::array<Slot, kNumSlots> slots_{};
    int soundingSlot_ = 0;
    int outgoingSlot_ = -1;
    float crossfadePos_ = 0.0f;
    float crossfadeInc_ = 0.0f;
    float collapsePos_ = 0.0f;
    float collapseInc_ = 0.0f;
    bool collapsing_ = false;
    /// FR-024a: the material the collapse will fade into once it completes. Kept
    /// equal to `material_` whenever no collapse is pending, so it is always the
    /// answer to "what is this body heading for".
    BodyMaterial pendingMaterial_ = kDefaultMaterial;
    /// Fade / collapse progress in SAMPLES. Integer by design - see
    /// advanceCrossfade() for why a float accumulator cannot carry SC-016's
    /// exact-arithmetic clause.
    std::uint64_t crossfadeSamples_ = 0;
    std::uint64_t crossfadeTotalSamples_ = 1;
    std::uint64_t collapseSamples_ = 0;
    std::uint64_t collapseTotalSamples_ = 1;
    /// The crossfade position the collapse froze at, i.e. where its own ramp to
    /// `(0, 1)` starts.
    float collapseBasePos_ = 0.0f;

    // --- FR-063 resonator-bypass ramp -----------------------------------------
    /// 0 = the resonator path at unity, 1 = fully bypassed (cloud drive only).
    /// Equal-power throughout, advanced only at a control step.
    float bypassPos_ = 0.0f;
    /// Where the ramp in flight started, so a mid-ramp toggle reverses.
    float bypassBasePos_ = 0.0f;
    std::uint64_t bypassSamples_ = 0;

    // --- FR-038a state-recovery ramp ------------------------------------------
    /// 1 = full output (the only value while `recoveryPhase_ == Idle`), 0 = muted
    /// while the clearing set runs. Equal-power in both directions, advanced only
    /// at a control step.
    float recoveryGain_ = 1.0f;
    RecoveryPhase recoveryPhase_ = RecoveryPhase::Idle;
    std::uint64_t recoverySamples_ = 0;

    // --- parameters (already substituted and clamped by their setters) --------
    BodyMaterial material_ = kDefaultMaterial;
    float resonance_ = kDefaultResonance;
    float damping_ = kDefaultDamping;
    float keyTracking_ = kDefaultKeyTracking;
    float noteHz_ = kDefaultNoteHz;
    float userDrive_ = kDefaultUserDrive;
    float mix_ = kDefaultMix;
    float cloudMix_ = kDefaultCloudMix;
    float cloudDecaySec_ = kDefaultCloudDecaySec;
    float cloudSize_ = kDefaultCloudSize;
    float cloudDamping_ = kDefaultCloudDamping;
    float width_ = kDefaultWidth;
    bool agcEnabled_ = kDefaultAgcEnabled;
    bool resonatorBypass_ = kDefaultResonatorBypass;
    std::uint32_t seed_ = kDefaultSeed;

    // --- smoothers ------------------------------------------------------------
    OnePoleSmoother keyTrackSmoother_{kDefaultKeyTracking};
    OnePoleSmoother noteLog2Smoother_{};
    OnePoleSmoother mixSmoother_{kDefaultMix};
    OnePoleSmoother cloudMixSmoother_{kDefaultCloudMix};
    OnePoleSmoother cloudSizeSmoother_{kDefaultCloudSize};
    OnePoleSmoother cloudDampLog2Smoother_{};
    OnePoleSmoother widthSmoother_{kDefaultWidth};
    OnePoleSmoother fbLSmoother_{};
    OnePoleSmoother fbRSmoother_{};

    // --- derived control state ------------------------------------------------
    float bodyHz_ = kDefaultNoteHz;
    float inputRms_ = 0.0f;
    float rmsGain_ = 1.0f;

    // --- FR-033a excitation compensation (see kMaxExcitationComp) ------------
    /// Smoothed in log2, read back through `exp2f` into `excitationComp_`.
    float excitationCompLog2_ = 0.0f;
    float excitationComp_ = kMinExcitationComp;
    /// Per-control-chunk peak of the post-crossfade ENGINE output, accumulated
    /// in `renderSub` and consumed by `controlStep` - the same carry-across-
    /// blocks contract as `cloudPeakAccum_`, which is what keeps the estimator
    /// block-size invariant.
    float enginePeakAccum_ = 0.0f;
    /// Max-hold of the PER-CHUNK coupling (engine peak per unit engine input).
    /// See updateExcitationComp for why the hold is on the ratio.
    float couplingEnv_ = 0.0f;
    /// Plant-lagged engine input level - the coupling estimate's denominator.
    float engineInEnv_ = 0.0f;
    /// `bodyHz_` as of the previous estimator window - clause 4's reference.
    float estAppliedBodyHz_ = 0.0f;
    /// Rebuilt in `prepare()`. The estimator's own coefficient is derived per
    /// control chunk from this and the sounding slot's `T60`, so a material
    /// change re-times the loop without a second configure pass.
    float estWindowSeconds_ = 0.0f;
    std::uint32_t estWindowChunks_ = 1u;
    float estSumSq_ = 0.0f;
    std::uint32_t estChunks_ = 0u;

    // --- the control-grid walker (FR-005a) ------------------------------------
    /// Absolute sample position. Cleared ONLY by prepare()/reset().
    std::uint64_t sampleCounter_ = 0;
    double chunkSumSq_ = 0.0;
    std::size_t chunkCount_ = 0;
    bool chunkPoisoned_ = false;
    std::size_t cloudChunkCap_ = kControlChunkSamples;

    /// True once the waveguide's loop length has been solved for a real pitch
    /// (`noteOn` at assignment, `retune` at a control step) and not silenced
    /// since. Gates the FR-060 dry-leak subtraction only - see `advanceSlot`.
    bool waveguideTuned_ = false;

    // --- counters (FR-007) ----------------------------------------------------
    std::uint64_t clampCount_ = 0;
    std::array<std::uint64_t, kNumEngines> engineSampleCount_{};

    // --- scratch (fixed size; the sub-chunk is capped at 64 by construction) ---
    std::array<float, kControlChunkSamples> monoScratch_{};
    /// The mono-summed, zero-substituted input scaled by FR-033's engine drive.
    std::array<float, kControlChunkSamples> driveScratch_{};
    std::array<float, kControlChunkSamples> engineScratch_{};
    /// The gain-weighted sum over active slots (FR-023's mix), clamped by FR-037
    /// on its way out. One buffer rather than one per slot: the engines are
    /// advanced in slot order and each one's output is folded in immediately.
    std::array<float, kControlChunkSamples> mixScratch_{};
    /// The mono pre-cloud signal: the clamped engine sum and FR-063's direct
    /// cloud drive, already blended by the bypass ramp. This is both the cloud's
    /// input and the "dry resonator output" `setCloudMix` blends against.
    std::array<float, kControlChunkSamples> bodyScratch_{};
    /// The decay loop's delay-line taps and, after the diffusion / damping /
    /// DC-blocking pass, its wet output (FR-050).
    std::array<float, kControlChunkSamples> cloudTapL_{};
    std::array<float, kControlChunkSamples> cloudTapR_{};
    std::array<float, kControlChunkSamples> cloudWetL_{};
    std::array<float, kControlChunkSamples> cloudWetR_{};
    /// Peak |cloud| over the control chunk IN FLIGHT; latched into
    /// `cloud_.lastPeak` at the control step (FR-053a).
    float cloudPeakAccum_ = 0.0f;
    // Mode-set staging for setModes/updateModes. Members, not locals: the audio
    // thread may not grow a stack frame by 256 bytes per control step, and both
    // calls take raw pointers.
    /// FR-070a's per-mode seeded micro-detune, memoised. It is a pure function
    /// of `(seed_, k)` and therefore constant for the life of a voice, but
    /// `buildModalModeSet` runs on EVERY dirty control step (FR-042/FR-042a) -
    /// at 8 control steps per 512-sample block and up to 32 modes that is 256
    /// `Xorshift32` seedings and 256 `std::exp2` calls per block for a value
    /// that never changes. Rebuilt only by `prepare()` and `setSeed()`.
    std::array<float, static_cast<std::size_t>(kModeCountCeiling)> seedDetuneCache_{};

    /// The normalised FR-011a amplitude law `a_k = (k+1)^(-alpha) / sum`,
    /// memoised against the `(alpha, count)` pair it was built for. Same
    /// argument as `seedDetuneCache_`: `std::pow` per mode per dirty control
    /// step, for a table that depends on neither pitch nor damping and so
    /// changes only when the material or FR-043's mode count does.
    std::array<float, static_cast<std::size_t>(kModeCountCeiling)> ampTableCache_{};
    float ampTableAlpha_ = -1.0f;
    int ampTableCount_ = -1;

    std::array<float, static_cast<std::size_t>(kModeCountCeiling)> modeFreqScratch_{};
    std::array<float, static_cast<std::size_t>(kModeCountCeiling)> modeAmpScratch_{};

    // --- lifecycle ------------------------------------------------------------
    double sampleRate_ = 44100.0;
    bool prepared_ = false;
};

}  // namespace DSP
}  // namespace Krate
