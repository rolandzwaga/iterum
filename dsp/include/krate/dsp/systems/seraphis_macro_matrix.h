// ==============================================================================
// Layer 3: System - SeraphisMacroMatrix (the five performance macros)
// ==============================================================================
// Seraphis Phase 7. Spec slug: seraphis-phase7-voice-engine.
//   Spec:    specs/seraphis-phase7-voice-engine/spec.md
//   Plan:    specs/seraphis-phase7-voice-engine/plan.md   (§4)
//   Roadmap: specs/Seraphis-roadmap.md, Part A -> Phase 7 (lines 288-315)
//
// Dream / Bloom / Dissolve / Gravity / Entropy, implemented as a DATA table of
// {macro, owner, target, base, amount, curve} rows (FR-058) evaluated through
// the shared Layer 0 applyModCurve() (core/modulation_curves.h:38).
//
// TWO APPLICATION SURFACES, because the reverb is Layer 4 (FR-056):
//   - apply(SeraphisEngine&)  pushes the Voice- and Engine-owned rows through
//                             the engine's own setters.
//   - computeAetherTargets()  RETURNS the Aether-owned rows as plain floats in
//                             a POD. No Layer 4 type is named anywhere here;
//                             the caller pushes them into its reverb.
//
// LAYER DISCIPLINE (FR-056, FR-070). Layers 0-2 + Layer 3 peers only. NO
//   effects/ header, ever. node tools/lint-layers.js gates this.
//
// THE TABLE IS THE RECORD OF THE TUNING (Clarifications Q3). `amount` and
//   `curve` are implementation tuning; what is NORMATIVE is each row's
//   DIRECTION (FR-061..FR-065), the three permitted curves (FR-057) and
//   SC-009's per-macro minimum end-to-end effect size. Retuning means editing
//   an `amount` here, never lowering a criterion.
// ==============================================================================

#pragma once

// Layer 0: Core
#include <krate/dsp/core/modulation_curves.h>

// Layer 3: Systems (peers)
#include <krate/dsp/systems/seraphis_engine.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace Krate {
namespace DSP {

/// The five performance controls (roadmap lines 300-308).
enum class SeraphisMacro : std::uint8_t { Dream = 0, Bloom, Dissolve, Gravity, Entropy, Count };

/// Which surface owns a row's target.
enum class SeraphisMacroTargetOwner : std::uint8_t { Voice = 0, Engine, Aether };

/// Every parameter any macro row may write.
enum class SeraphisMacroTarget : std::uint8_t {
    // -- Voice-owned ---------------------------------------------------------
    CloudInharmonicity,
    CloudMutation,
    CloudSpectralGravity,
    CloudRichness,
    CloudSpectralTiltDb,
    CloudStereoSpread,
    CloudAttackTimeSec,
    CloudDriftDepthCents,
    MorphEntropy,
    MorphTargetPosition,
    BodyDamping,
    AtmosLevel,
    AtmosBlur,
    AtmosDriftDepth,
    SpatialDepth,
    /// Routes to SeraphisVoice::setVoiceWidthBasePercent - the M/S width CENTRE,
    /// not the width itself, which plan §2.6 recomputes from the orbit's y once
    /// per control chunk (plan §4.1.0).
    VoiceWidth,
    EnvStage0Ms,
    EnvStage1Ms,
    EnvReleaseMs,
    // -- Aether-owned (each MUST have a 1:1 field in SeraphisAetherTargets) ---
    AetherMix,
    AetherSize,
    AetherWidth,
    AetherShimmerOctaveSend,
    AetherShimmerFifthSend,
    AetherBloomSend,
    AetherSizeBreathDepth,
    AetherDimensionalityTideDepth,
    Count
};

/// FR-056. The Aether-owned rows as plain floats - no Layer 4 type is named.
///
/// The defaults ARE the eight duplicated AetherReverb literals (plan §4.1.1), so
/// a default-constructed SeraphisAetherTargets is already the FR-060 neutral:
///   mix                     0.35f  aether_reverb.h:2779 (kDefaultMix)
///   size                    0.50f  :2730 (kDefaultSize)
///   width                   1.0f   :2777 (kDefaultWidth)
///   shimmerOctaveSend       0.0f   :2760 (kDefaultSend, snapTo at :1911)
///   shimmerFifthSend        0.0f   :2760 (kDefaultSend, snapTo at :1913)
///   bloomSend               0.0f   :2760 (kDefaultSend, snapTo at :1915)
///   sizeBreathDepth         0.20f  :2749 (kDefaultSizeBreathDepth, init :4629)
///   dimensionalityTideDepth 0.20f  :2750 (kDefaultTideDepth, init :4630)
/// They are duplicated rather than referenced because AetherReverb's `private:`
/// opens at :2724 and every one of them sits below it - unreachable from any
/// consumer, and FR-056 forbids naming a Layer 4 type at all.
///
/// A DRIFTED LITERAL IS INVISIBLE TO A LITERAL-VS-LITERAL COMPARISON, which is
/// why SC-010 clause 3 also carries a RENDER differential in the Layer-4-aware
/// test TU (plan §4.1.1).
struct SeraphisAetherTargets {
    float mix = 0.35f;
    float size = 0.50f;
    float width = 1.0f;
    float shimmerOctaveSend = 0.0f;
    float shimmerFifthSend = 0.0f;
    float bloomSend = 0.0f;
    float sizeBreathDepth = 0.20f;
    float dimensionalityTideDepth = 0.20f;
};

/// FR-060 documented neutrals. Gravity is bipolar around 0.5; the rest are 0.
struct SeraphisMacroValues {
    float dream = 0.0f;
    float bloom = 0.0f;
    float dissolve = 0.0f;
    float gravity = 0.5f;
    float entropy = 0.0f;
};

/// FR-058. One row of the mapping; the mapping is DATA, not code.
struct SeraphisMacroRow {
    SeraphisMacro macro;
    SeraphisMacroTargetOwner owner;
    SeraphisMacroTarget target;
    /// The FR-019 shipped voice default, or - for the eight Aether rows - the
    /// duplicated component default enumerated on SeraphisAetherTargets. Every
    /// row that shares a target MUST carry the same base; asserted below the
    /// class by everyRowSharesOneBasePerTarget().
    float base;
    /// SIGNED; implementation tuning.
    float amount;
    /// Linear | Exponential | SCurve ONLY (FR-057); Stepped is excluded because
    /// std::floor(x*4)/3 (core/modulation_curves.h:53-54) breaks SC-009's
    /// continuity bound by construction.
    ModCurve curve;
};

/// @par Layer: 3 (systems/). Dependencies: Layers 0-2 + Layer 3 peers. NO Layer 4.
/// @par Real-Time Safety: every method is noexcept, allocation-free, lock-free.
///      FR-059: apply() and computeAetherTargets() are idempotent - calling them
///      every block with unchanged knobs steps nothing. Every writable target
///      either early-outs on an unchanged value or is a plain scalar store; the
///      matrix adds no smoother of its own (plan §4.4.1).
class SeraphisMacroMatrix {
public:
    // =========================================================================
    // Constants - ALL class-scoped (plan §0.2)
    // =========================================================================

    static constexpr std::size_t kNumMacros = static_cast<std::size_t>(SeraphisMacro::Count);
    static constexpr std::size_t kNumTargets = static_cast<std::size_t>(SeraphisMacroTarget::Count);
    /// FR-058's table length. 8 Dream + 9 Bloom + 6 Dissolve + 4 Gravity + 3 Entropy.
    static constexpr std::size_t kNumRows = 30;
    /// First Aether-owned target; SeraphisAetherTargets' fields are declared in
    /// exactly this enum order, which is what everyAetherRowHasAPodField() checks.
    static constexpr std::size_t kFirstAetherTarget =
        static_cast<std::size_t>(SeraphisMacroTarget::AetherMix);
    static constexpr std::size_t kNumAetherTargets = 8;

    // =========================================================================
    // FR-058. THE TABLE.
    // =========================================================================
    //
    // Every `base` in the Voice block is the FR-019 shipped voice default, read
    // off SeraphisVoice::prepare step 6 (seraphis_voice.h:269-313) and step 7
    // (:334-338). Every `base` in the Aether block is the duplicated literal
    // enumerated on SeraphisAetherTargets above.
    //
    // Direction per FR-061..FR-065 (normative); amounts are the Q3 tuning.
    static constexpr std::array<SeraphisMacroRow, kNumRows> kRows = {{
        // ---------------------------------------------------------------------
        // FR-061 DREAM - harmonic purity ^, reverb send ^, life-mod depth ^,
        //                entropy v.
        // setDriftDepthCents is deliberately NOT here: it RAISES deviation from
        // the harmonic grid and would fight Dream's own primary metric (FR-061).
        // ---------------------------------------------------------------------
        {.macro = SeraphisMacro::Dream,
         .owner = SeraphisMacroTargetOwner::Voice,
         .target = SeraphisMacroTarget::CloudInharmonicity,
         .base = 0.030f,  // seraphis_voice.h:270
         .amount = -0.030f,
         .curve = ModCurve::Linear},  // -> 0.0 (the cloud's floor) at Dream = 1
        {.macro = SeraphisMacro::Dream,
         .owner = SeraphisMacroTargetOwner::Voice,
         .target = SeraphisMacroTarget::CloudSpectralGravity,
         .base = 0.20f,  // :273
         .amount = -0.20f,
         .curve = ModCurve::Linear},  // -> 0 exactly, which is FR-061's wording
        {.macro = SeraphisMacro::Dream,
         .owner = SeraphisMacroTargetOwner::Voice,
         .target = SeraphisMacroTarget::CloudMutation,
         .base = 0.25f,  // :272
         .amount = -0.25f,
         .curve = ModCurve::Linear},
        {.macro = SeraphisMacro::Dream,
         .owner = SeraphisMacroTargetOwner::Voice,
         .target = SeraphisMacroTarget::MorphEntropy,
         .base = 0.20f,  // :280
         .amount = -0.20f,
         .curve = ModCurve::Linear},
        {.macro = SeraphisMacro::Dream,
         .owner = SeraphisMacroTargetOwner::Voice,
         .target = SeraphisMacroTarget::SpatialDepth,
         .base = 0.35f,  // :309, FR-019's zero-travel fix
         .amount = 0.65f,
         .curve = ModCurve::Linear},  // -> 1.0, OrbitModulator::setDepth's max
        {.macro = SeraphisMacro::Dream,
         .owner = SeraphisMacroTargetOwner::Aether,
         .target = SeraphisMacroTarget::AetherMix,
         .base = 0.35f,
         .amount = 0.35f,
         .curve = ModCurve::Linear},  // -> 0.70
        {.macro = SeraphisMacro::Dream,
         .owner = SeraphisMacroTargetOwner::Aether,
         .target = SeraphisMacroTarget::AetherSizeBreathDepth,
         .base = 0.20f,
         .amount = 0.60f,
         .curve = ModCurve::SCurve},  // -> 0.80
        {.macro = SeraphisMacro::Dream,
         .owner = SeraphisMacroTargetOwner::Aether,
         .target = SeraphisMacroTarget::AetherDimensionalityTideDepth,
         .base = 0.20f,
         .amount = 0.60f,
         .curve = ModCurve::SCurve},  // -> 0.80

        // ---------------------------------------------------------------------
        // FR-062 BLOOM - upper partials ^, shimmer send ^, stereo width ^,
        //                morph toward the brighter state.
        // ---------------------------------------------------------------------
        {.macro = SeraphisMacro::Bloom,
         .owner = SeraphisMacroTargetOwner::Voice,
         .target = SeraphisMacroTarget::CloudSpectralTiltDb,
         .base = 0.0f,  // :271
         .amount = 9.0f,
         .curve = ModCurve::Linear},  // toward kMaxTiltDbPerOct = +12
        {.macro = SeraphisMacro::Bloom,
         .owner = SeraphisMacroTargetOwner::Voice,
         .target = SeraphisMacroTarget::CloudRichness,
         .base = 0.60f,  // :269, FR-019's headroom fix
         .amount = 0.40f,
         .curve = ModCurve::Linear},  // -> 1.0, setRichness' clamp max
        {.macro = SeraphisMacro::Bloom,
         .owner = SeraphisMacroTargetOwner::Voice,
         .target = SeraphisMacroTarget::CloudStereoSpread,
         .base = 0.35f,  // :275
         .amount = 0.60f,
         .curve = ModCurve::Linear},
        // VoiceWidth's amount is bounded by construction, not by taste: the
        // per-chunk width is clamp(widthBase + y * kVoiceWidthSpanPct, 50, 150)
        // and |y| <= SpatialDepth <= ... well, <= 1, but at Bloom's own sweep
        // the FR-019 depth of 0.35 caps |y * 50| at 17.5. 100 + 30 + 17.5 =
        // 147.5 < kMaxVoiceWidthPct, so getSpatialWidthPercent() never
        // saturates and SC-009's width secondary stays strictly monotone. A
        // larger amount would clip the top of the sweep into a tie run and
        // depress Spearman rho for no audible gain.
        {.macro = SeraphisMacro::Bloom,
         .owner = SeraphisMacroTargetOwner::Voice,
         .target = SeraphisMacroTarget::VoiceWidth,
         .base = 100.0f,  // :313, plan §4.1.0
         .amount = 30.0f,
         .curve = ModCurve::Linear},
        {.macro = SeraphisMacro::Bloom,
         .owner = SeraphisMacroTargetOwner::Voice,
         .target = SeraphisMacroTarget::MorphTargetPosition,
         .base = 0.0f,  // :261, slot 0 (SineStack)
         .amount = 1.0f,
         .curve = ModCurve::Linear},  // -> slot 1, FR-019a's Glass state
        {.macro = SeraphisMacro::Bloom,
         .owner = SeraphisMacroTargetOwner::Aether,
         .target = SeraphisMacroTarget::AetherShimmerOctaveSend,
         .base = 0.0f,
         .amount = 0.60f,
         .curve = ModCurve::SCurve},
        {.macro = SeraphisMacro::Bloom,
         .owner = SeraphisMacroTargetOwner::Aether,
         .target = SeraphisMacroTarget::AetherShimmerFifthSend,
         .base = 0.0f,
         .amount = 0.40f,
         .curve = ModCurve::SCurve},
        {.macro = SeraphisMacro::Bloom,
         .owner = SeraphisMacroTargetOwner::Aether,
         .target = SeraphisMacroTarget::AetherBloomSend,
         .base = 0.0f,
         .amount = 0.60f,
         .curve = ModCurve::SCurve},
        // FR-062 names `width ^` and FR-060 pins the base at the component
        // default, which for AetherReverb::setWidth IS the clamp maximum
        // (0..1, default 1.0, aether_reverb.h:2333). The row is therefore
        // present with the normative sign and SATURATES in the caller's setter -
        // the POD carries the raw sum, exactly as FR-057's "clamped to the
        // target setter's own documented range" prescribes. It is stated here
        // rather than silently dropped: dropping it would break
        // everyTargetInFr061to065IsPresent().
        {.macro = SeraphisMacro::Bloom,
         .owner = SeraphisMacroTargetOwner::Aether,
         .target = SeraphisMacroTarget::AetherWidth,
         .base = 1.0f,
         .amount = 0.25f,
         .curve = ModCurve::Linear},

        // ---------------------------------------------------------------------
        // FR-063 DISSOLVE - atmosphere mix ^, spectral blur ^, transient
        //                   definition v, envelope slew ^. No Aether rows.
        // ---------------------------------------------------------------------
        {.macro = SeraphisMacro::Dissolve,
         .owner = SeraphisMacroTargetOwner::Voice,
         .target = SeraphisMacroTarget::AtmosLevel,
         .base = 0.50f,  // :299
         .amount = 1.50f,
         .curve = ModCurve::Linear},  // -> kMaxLevel = 2, a 4x trim range
        // BLUR FIGHTS DISSOLVE'S OWN PRIMARY ABOVE ~0.45, and the mechanism is
        // in the source: AtmosphereEngine's blur stage randomises the STFT phase
        // and never touches magnitude (atmosphere_engine.h:2050-2071), so the
        // 75 %-overlap OLA that reassembles it sums INCOHERENTLY and the wet
        // bus loses energy as blur rises. That loss is subtracted from exactly
        // the energy the AtmosLevel row is adding. MEASURED with amount = 1.0:
        // the atmosphere fraction rose to 0.0924 at step 9 (blur 0.45) and then
        // FELL monotonically to 0.0741 by step 17, Spearman rho = 0.199 against
        // the +0.9 gate. Capping the row at the turning point keeps FR-063's
        // "spectral blur ^" direction and its blur-induced-spread secondary
        // while leaving AtmosLevel in charge of the axis.
        {.macro = SeraphisMacro::Dissolve,
         .owner = SeraphisMacroTargetOwner::Voice,
         .target = SeraphisMacroTarget::AtmosBlur,
         .base = 0.0f,  // :300
         .amount = 0.40f,
         .curve = ModCurve::Linear},
        {.macro = SeraphisMacro::Dissolve,
         .owner = SeraphisMacroTargetOwner::Voice,
         .target = SeraphisMacroTarget::CloudAttackTimeSec,
         .base = 0.05f,  // :276, the component floor
         .amount = 1.95f,
         .curve = ModCurve::Linear},  // -> 2.0 s
        {.macro = SeraphisMacro::Dissolve,
         .owner = SeraphisMacroTargetOwner::Voice,
         .target = SeraphisMacroTarget::EnvStage0Ms,
         .base = 2000.0f,  // :334
         .amount = 4000.0f,
         .curve = ModCurve::Linear},
        {.macro = SeraphisMacro::Dissolve,
         .owner = SeraphisMacroTargetOwner::Voice,
         .target = SeraphisMacroTarget::EnvStage1Ms,
         .base = 4000.0f,  // :335
         .amount = 5000.0f,
         .curve = ModCurve::Linear},
        {.macro = SeraphisMacro::Dissolve,
         .owner = SeraphisMacroTargetOwner::Voice,
         .target = SeraphisMacroTarget::EnvReleaseMs,
         .base = 8000.0f,  // :338
         .amount = 2000.0f,
         .curve = ModCurve::Linear},  // -> kMaxStageTimeMs = 10000

        // ---------------------------------------------------------------------
        // FR-064 GRAVITY - the one BIPOLAR macro. 0 = air, 0.5 = neutral,
        //   1 = stone. Every row is driven by g = (gravity - 0.5) * 2 as
        //   amount * curve(|g|) * sign(g), so at 0.5 it contributes exactly 0
        //   and each row travels in BOTH directions from its FR-019 base.
        // ---------------------------------------------------------------------
        {.macro = SeraphisMacro::Gravity,
         .owner = SeraphisMacroTargetOwner::Voice,
         .target = SeraphisMacroTarget::CloudRichness,
         .base = 0.60f,
         .amount = -0.35f,
         .curve = ModCurve::Linear},  // stone 0.25 <- 0.60 -> 0.95 air
        {.macro = SeraphisMacro::Gravity,
         .owner = SeraphisMacroTargetOwner::Voice,
         .target = SeraphisMacroTarget::BodyDamping,
         .base = 0.25f,  // :287, FR-019's zero-travel fix
         .amount = 0.25f,
         .curve = ModCurve::Linear},  // stone 0.50 <- 0.25 -> 0.0 air
        {.macro = SeraphisMacro::Gravity,
         .owner = SeraphisMacroTargetOwner::Voice,
         .target = SeraphisMacroTarget::CloudSpectralTiltDb,
         .base = 0.0f,
         .amount = -8.0f,
         .curve = ModCurve::Linear},  // stone -8 dB/oct <- 0 -> +8 air
        {.macro = SeraphisMacro::Gravity,
         .owner = SeraphisMacroTargetOwner::Aether,
         .target = SeraphisMacroTarget::AetherSize,
         .base = 0.50f,
         .amount = 0.45f,
         .curve = ModCurve::Linear},  // stone 0.95 <- 0.50 -> 0.05 air

        // ---------------------------------------------------------------------
        // FR-065 ENTROPY - direct wire to the Phase 3 EntropyProcessor plus the
        //                  two drift depths. No Aether rows.
        // ---------------------------------------------------------------------
        // THE ROW STOPS AT EntropyProcessor's STAGE-3 THRESHOLD, deliberately.
        // Its four stage ramps are amplitude jitter [0.00, 0.35], phase
        // decoherence [0.25, 0.60], ratio scatter [0.50, 0.85] and
        // death/rebirth [0.75, 1.00] (entropy_processor.h:66-69). Stages 1 and 2
        // ADD spectral content and raise flatness monotonically; stage 3
        // SATURATES it and stage 4 REMOVES partials outright, and both turn
        // Entropy's own primary metric around. MEASURED on SC-009's 21-step
        // sweep, cloud-only arm:
        //   amount 0.80 (entropy -> 1.00): peaked at step 13 and fell to the
        //       end,                             Spearman rho = 0.521
        //   amount 0.50 (entropy -> 0.70): monotone to step ~12, then a wiggling
        //       plateau,                         rho >= 0.9 at 21 points but
        //                                        0.836 at 11 and 0.700 at 5
        //   amount 0.30 (entropy -> 0.50): monotone throughout, rho >= 0.9 at
        //                                  5, 11 and 21 points
        // The row keeps FR-065's direction and a 2.5x travel on the parameter;
        // Entropy's audible width is carried by the two drift rows below, which
        // both run to their component maxima.
        {.macro = SeraphisMacro::Entropy,
         .owner = SeraphisMacroTargetOwner::Voice,
         .target = SeraphisMacroTarget::MorphEntropy,
         .base = 0.20f,
         .amount = 0.30f,
         .curve = ModCurve::Linear},  // -> 0.50
        {.macro = SeraphisMacro::Entropy,
         .owner = SeraphisMacroTargetOwner::Voice,
         .target = SeraphisMacroTarget::CloudDriftDepthCents,
         .base = 0.0f,  // :274
         .amount = 50.0f,
         .curve = ModCurve::Linear},  // -> kMaxDriftCents = 50
        {.macro = SeraphisMacro::Entropy,
         .owner = SeraphisMacroTargetOwner::Voice,
         .target = SeraphisMacroTarget::AtmosDriftDepth,
         .base = 0.30f,  // :303
         .amount = 0.70f,
         .curve = ModCurve::Linear},  // -> 1.0
    }};

    // =========================================================================
    // FR-058's compile-time guards (plan §4.2). Instantiated as static_asserts
    // BELOW the class, which is the first complete-class context in which these
    // member function bodies exist.
    // =========================================================================

    /// The POD field index for an Aether-owned target, or -1 if the target is
    /// not Aether-owned. SeraphisAetherTargets' fields are declared in enum
    /// order, so this is a pure offset - stated as a function so the guard and
    /// computeAetherTargets() can never disagree.
    [[nodiscard]] static constexpr int aetherFieldIndex(SeraphisMacroTarget t) noexcept {
        const auto i = static_cast<std::size_t>(t);
        if (i < kFirstAetherTarget || i >= (kFirstAetherTarget + kNumAetherTargets)) {
            return -1;
        }
        return static_cast<int>(i - kFirstAetherTarget);
    }

    /// FR-058: no row may be unreachable. An Aether-owned target MUST carry
    /// owner == Aether and vice versa, or apply() and computeAetherTargets()
    /// would disagree about who writes it.
    [[nodiscard]] static constexpr bool everyRowOwnerIsValid(
        const std::array<SeraphisMacroRow, kNumRows>& rows) noexcept {
        for (const SeraphisMacroRow& row : rows) {
            if (static_cast<std::size_t>(row.macro) >= kNumMacros) {
                return false;
            }
            if (static_cast<std::size_t>(row.target) >= kNumTargets) {
                return false;
            }
            const bool isAetherTarget = (aetherFieldIndex(row.target) >= 0);
            const bool isAetherOwner = (row.owner == SeraphisMacroTargetOwner::Aether);
            if (isAetherTarget != isAetherOwner) {
                return false;
            }
            if (!isAetherOwner && row.owner != SeraphisMacroTargetOwner::Voice
                && row.owner != SeraphisMacroTargetOwner::Engine) {
                return false;
            }
        }
        return true;
    }

    /// FR-058: an Aether row absent from SeraphisAetherTargets is a compile error.
    [[nodiscard]] static constexpr bool everyAetherRowHasAPodField(
        const std::array<SeraphisMacroRow, kNumRows>& rows) noexcept {
        for (const SeraphisMacroRow& row : rows) {
            if (row.owner == SeraphisMacroTargetOwner::Aether
                && aetherFieldIndex(row.target) < 0) {
                return false;
            }
        }
        return true;
    }

    /// FR-057 / RA-7. ModCurve::Stepped is std::floor(x*4)/3
    /// (core/modulation_curves.h:53-54) - 18 zero-change steps and 3 jumps of
    /// ~1/3 over SC-009's 21-step sweep, a jump/mean-step ratio of ~6.7x, which
    /// fails SC-009's 3x continuity bound BY CONSTRUCTION.
    [[nodiscard]] static constexpr bool noRowUsesSteppedCurve(
        const std::array<SeraphisMacroRow, kNumRows>& rows) noexcept {
        for (const SeraphisMacroRow& row : rows) {
            if (row.curve == ModCurve::Stepped) {
                return false;
            }
        }
        return true;
    }

    /// FR-061..FR-065: every enumerated target is claimed by at least one row.
    /// The enum IS the union of the five mappings, so "no target is orphaned"
    /// and "no mapping lost a row" are the same statement.
    [[nodiscard]] static constexpr bool everyTargetInFr061to065IsPresent(
        const std::array<SeraphisMacroRow, kNumRows>& rows) noexcept {
        for (std::size_t t = 0; t < kNumTargets; ++t) {
            bool found = false;
            for (const SeraphisMacroRow& row : rows) {
                if (static_cast<std::size_t>(row.target) == t) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                return false;
            }
        }
        return true;
    }

    /// Plan §4.3 evaluates `acc = base(t)` ONCE per target, so two rows on the
    /// same target that disagree about `base` would silently make the result
    /// depend on table order. Three targets are hit by two macros each
    /// (CloudRichness, CloudSpectralTiltDb, MorphEntropy) and that is specified,
    /// not accidental (FR-057) - this guard is what keeps it safe.
    [[nodiscard]] static constexpr bool everyRowSharesOneBasePerTarget(
        const std::array<SeraphisMacroRow, kNumRows>& rows) noexcept {
        for (std::size_t a = 0; a < rows.size(); ++a) {
            for (std::size_t b = a + 1; b < rows.size(); ++b) {
                if (rows[a].target == rows[b].target && rows[a].base != rows[b].base) {
                    return false;
                }
            }
        }
        return true;
    }

    // =========================================================================
    // Knobs (FR-056, FR-060)
    // =========================================================================

    /// FR-060's documented neutral for one macro: 0.5 for the bipolar Gravity,
    /// 0 for the other four.
    [[nodiscard]] static constexpr float neutralFor(SeraphisMacro macro) noexcept {
        return (macro == SeraphisMacro::Gravity) ? 0.5f : 0.0f;
    }

    /// Clamped to [0, 1]; a non-finite argument restores the macro's FR-060
    /// neutral rather than poisoning the whole matrix.
    void setMacro(SeraphisMacro macro, float value) noexcept {
        const float sanitised = isFiniteBits(value) ? value : neutralFor(macro);
        const float clamped = std::clamp(sanitised, 0.0f, 1.0f);
        switch (macro) {
            case SeraphisMacro::Dream:
                values_.dream = clamped;
                break;
            case SeraphisMacro::Bloom:
                values_.bloom = clamped;
                break;
            case SeraphisMacro::Dissolve:
                values_.dissolve = clamped;
                break;
            case SeraphisMacro::Gravity:
                values_.gravity = clamped;
                break;
            case SeraphisMacro::Entropy:
                values_.entropy = clamped;
                break;
            case SeraphisMacro::Count:
            default:
                break;
        }
    }

    [[nodiscard]] float getMacro(SeraphisMacro macro) const noexcept {
        switch (macro) {
            case SeraphisMacro::Dream:
                return values_.dream;
            case SeraphisMacro::Bloom:
                return values_.bloom;
            case SeraphisMacro::Dissolve:
                return values_.dissolve;
            case SeraphisMacro::Gravity:
                return values_.gravity;
            case SeraphisMacro::Entropy:
                return values_.entropy;
            case SeraphisMacro::Count:
            default:
                return 0.0f;
        }
    }

    /// Bulk set, one call per knob so every value goes through setMacro()'s
    /// sanitising clamp.
    void setMacros(const SeraphisMacroValues& v) noexcept {
        setMacro(SeraphisMacro::Dream, v.dream);
        setMacro(SeraphisMacro::Bloom, v.bloom);
        setMacro(SeraphisMacro::Dissolve, v.dissolve);
        setMacro(SeraphisMacro::Gravity, v.gravity);
        setMacro(SeraphisMacro::Entropy, v.entropy);
    }

    [[nodiscard]] SeraphisMacroValues getMacros() const noexcept { return values_; }

    // =========================================================================
    // FR-056's two application surfaces
    // =========================================================================

    /// @brief FR-056. Pushes the Voice- and Engine-owned rows through the engine.
    ///
    /// Reaches `SeraphisEngine::voices_` directly via `friend class
    /// SeraphisMacroMatrix;` (seraphis_engine.h:738), so FR-085's getVoice() can
    /// stay const for tests (plan §4.4). Iterates `v < getPolyphony()`: slots
    /// above the current polyphony are neither summed nor allocatable.
    ///
    /// Each value is written through the owning setter, which does its OWN
    /// clamping - FR-057's summation-then-clamp, which is ModulationEngine's
    /// order (modulation_engine.h:44-54).
    void apply(SeraphisEngine& engine) const noexcept {
        const std::array<float, kNumTargets> v = evaluateAll();
        const std::size_t voiceCount = engine.getPolyphony();
        for (std::size_t i = 0; i < voiceCount; ++i) {
            SeraphisVoice& voice = engine.voices_[i];

            // Harmonic cloud
            voice.setInharmonicity(at(v, SeraphisMacroTarget::CloudInharmonicity));
            voice.setMutation(at(v, SeraphisMacroTarget::CloudMutation));
            voice.setSpectralGravity(at(v, SeraphisMacroTarget::CloudSpectralGravity));
            voice.setRichness(at(v, SeraphisMacroTarget::CloudRichness));
            voice.setSpectralTiltDb(at(v, SeraphisMacroTarget::CloudSpectralTiltDb));
            voice.setStereoSpread(at(v, SeraphisMacroTarget::CloudStereoSpread));
            voice.setAttackTimeSec(at(v, SeraphisMacroTarget::CloudAttackTimeSec));
            voice.setDriftDepthCents(at(v, SeraphisMacroTarget::CloudDriftDepthCents));

            // Spectral morph
            voice.setEntropy(at(v, SeraphisMacroTarget::MorphEntropy));
            voice.setTargetPosition(at(v, SeraphisMacroTarget::MorphTargetPosition));

            // Continuous body
            voice.setDamping(at(v, SeraphisMacroTarget::BodyDamping));

            // Atmosphere
            voice.setLevel(at(v, SeraphisMacroTarget::AtmosLevel));
            voice.setBlur(at(v, SeraphisMacroTarget::AtmosBlur));
            voice.setDriftDepth(at(v, SeraphisMacroTarget::AtmosDriftDepth));

            // Spatial
            voice.setSpatialDepth(at(v, SeraphisMacroTarget::SpatialDepth));
            voice.setVoiceWidthBasePercent(at(v, SeraphisMacroTarget::VoiceWidth));

            // Envelope (FR-030's named forwarders; FR-063's slew axis)
            voice.setEnvelopeStageTimeMs(0, at(v, SeraphisMacroTarget::EnvStage0Ms));
            voice.setEnvelopeStageTimeMs(1, at(v, SeraphisMacroTarget::EnvStage1Ms));
            voice.setEnvelopeReleaseMs(at(v, SeraphisMacroTarget::EnvReleaseMs));
        }
    }

    /// @brief FR-056. Pure function of the knobs and the table; writes nothing.
    ///
    /// The POD carries the RAW sum. Range clamping for these eight belongs to
    /// the Layer-4 setter the caller pushes into (FR-057), which is also the
    /// only place that knows the reverb's documented ranges.
    [[nodiscard]] SeraphisAetherTargets computeAetherTargets() const noexcept {
        const std::array<float, kNumTargets> v = evaluateAll();
        SeraphisAetherTargets out{};
        out.mix = at(v, SeraphisMacroTarget::AetherMix);
        out.size = at(v, SeraphisMacroTarget::AetherSize);
        out.width = at(v, SeraphisMacroTarget::AetherWidth);
        out.shimmerOctaveSend = at(v, SeraphisMacroTarget::AetherShimmerOctaveSend);
        out.shimmerFifthSend = at(v, SeraphisMacroTarget::AetherShimmerFifthSend);
        out.bloomSend = at(v, SeraphisMacroTarget::AetherBloomSend);
        out.sizeBreathDepth = at(v, SeraphisMacroTarget::AetherSizeBreathDepth);
        out.dimensionalityTideDepth = at(v, SeraphisMacroTarget::AetherDimensionalityTideDepth);
        return out;
    }

    // =========================================================================
    // FR-003 / FR-004 (Phase 9, spec slug seraphis-phase9-parameters, plan §1.4)
    //
    // Per-target BASE OVERRIDES, so a deep parameter IS the origin the macros
    // move from rather than a second, competing write path (spec C-1/FR-055).
    //
    // @par Layer: 3 (systems/). Dependencies: Layers 0-2 + Layer 3 peers. NO Layer 4.
    // @par Real-Time Safety: allocation-free, lock-free, exception-free - each
    //      of the three is a bounded scalar read/write over member storage.
    // =========================================================================

    /// @brief FR-003. Override the per-target `base` that evaluateAll() seeds
    ///        from.
    ///
    /// Well-defined by construction: everyRowSharesOneBasePerTarget(kRows)
    /// (below the class) already guarantees exactly one base per target, so a
    /// PER-TARGET override cannot be ambiguous.
    ///
    /// A non-finite argument leaves the stored base UNCHANGED - checked with
    /// this class's own bit-pattern helper isFiniteBits (:748), never
    /// std::isnan, which -ffast-math folds away on the macOS leg.
    ///
    /// NO HEADROOM RESCALING is applied (spec C-1): the macro `amount` values
    /// were sized against the shipped bases, so an override placed at the clamp
    /// a macro travels toward consumes that macro's travel. That saturation is
    /// accepted - rescaling would make the two surfaces MULTIPLY instead of
    /// compose and would change shipped macro behaviour at the defaults.
    void setTargetBase(SeraphisMacroTarget target, float base) noexcept {
        const auto i = static_cast<std::size_t>(target);
        if (i >= kNumTargets || !isFiniteBits(base)) {
            return;
        }
        baseOverride_[i] = base;
        hasOverride_[i] = true;
    }

    /// @brief FR-003. Restore every kRows literal verbatim.
    void resetTargetBases() noexcept {
        hasOverride_.fill(false);
        baseOverride_.fill(0.0f);
    }

    /// @brief FR-003. The override if one was set, else the kRows literal.
    [[nodiscard]] float getTargetBase(SeraphisMacroTarget target) const noexcept {
        const auto i = static_cast<std::size_t>(target);
        if (i >= kNumTargets) {
            return 0.0f;
        }
        return hasOverride_[i] ? baseOverride_[i] : literalBaseFor(target);
    }

private:
    /// The kRows base for `target`. everyTargetInFr061to065IsPresent (asserted
    /// below the class) guarantees the scan always finds one, so the 0 fallback
    /// is unreachable.
    [[nodiscard]] static constexpr float literalBaseFor(SeraphisMacroTarget target) noexcept {
        for (const SeraphisMacroRow& row : kRows) {
            if (row.target == target) {
                return row.base;
            }
        }
        return 0.0f;
    }

    /// -ffast-math folds std::isnan away on the macOS leg, so finiteness is a
    /// BIT-PATTERN question here - the same helper SeraphisVoice and
    /// SeraphisEngine carry.
    [[nodiscard]] static bool isFiniteBits(float value) noexcept {
        std::uint32_t bits = 0;
        std::memcpy(&bits, &value, sizeof(bits));
        return (bits & 0x7F800000u) != 0x7F800000u;
    }

    [[nodiscard]] static float at(const std::array<float, kNumTargets>& v,
                                  SeraphisMacroTarget t) noexcept {
        return v[static_cast<std::size_t>(t)];
    }

    /// FR-058's signed, curved contribution of one row.
    ///
    /// Gravity is the one BIPOLAR macro (FR-064): its knob deviation
    /// `g = (m - 0.5) * 2` spans [-1, +1] and the row contributes
    /// `amount * curve(|g|) * sign(g)`, so one row expresses both the air and
    /// the stone half and both halves have travel from the FR-019 base.
    [[nodiscard]] float contributionOf(const SeraphisMacroRow& row) const noexcept {
        const float m = getMacro(row.macro);
        if (row.macro == SeraphisMacro::Gravity) {
            const float g = (m - 0.5f) * 2.0f;
            const float sign = (g < 0.0f) ? -1.0f : 1.0f;
            return row.amount * applyModCurve(row.curve, std::fabs(g)) * sign;
        }
        return row.amount * applyModCurve(row.curve, m);
    }

    /// Plan §4.3. `acc = base(t)`, then one contribution per row on `t`.
    ///
    /// THERE IS DELIBERATELY NO `if (neutral) return;` FAST PATH. At the FR-060
    /// neutral every term is exactly 0 - applyModCurve(c, 0) == 0 for all three
    /// permitted curves and g == 0 for Gravity - so SC-010's inertness is a
    /// PROPERTY OF THE ARITHMETIC. A shortcut would let a mis-signed row hide
    /// behind it.
    [[nodiscard]] std::array<float, kNumTargets> evaluateAll() const noexcept {
        std::array<float, kNumTargets> value{};
        std::array<bool, kNumTargets> seeded{};
        for (const SeraphisMacroRow& row : kRows) {
            const auto i = static_cast<std::size_t>(row.target);
            if (!seeded[i]) {
                value[i] = hasOverride_[i] ? baseOverride_[i] : row.base;   // FR-004
                seeded[i] = true;
            }
            value[i] += contributionOf(row);
        }
        return value;
    }

    /// FR-060: the five knobs, already at their documented neutrals.
    SeraphisMacroValues values_{};

    /// FR-003. The per-target base override, and whether one was ever set.
    ///
    /// SC-002 CLAUSE 4 FALLS OUT OF THE CONSTRUCTION: a default-constructed
    /// matrix has hasOverride_ all false, so evaluateAll() evaluates the
    /// identical expression it did in Phase 7 and apply() /
    /// computeAetherTargets() are bit-identical at the shipped defaults.
    std::array<float, kNumTargets> baseOverride_{};
    std::array<bool, kNumTargets> hasOverride_{};
};

// =============================================================================
// FR-058's compile-time guards. Namespace scope, because a member-specification
// static_assert cannot call a member function whose body has not been parsed yet.
// =============================================================================

static_assert(SeraphisMacroMatrix::kRows.size() == SeraphisMacroMatrix::kNumRows,
              "FR-058: kNumRows must match the table");
static_assert(SeraphisMacroMatrix::everyRowOwnerIsValid(SeraphisMacroMatrix::kRows),
              "FR-058: every row must carry a valid owner matching its target's half");
static_assert(SeraphisMacroMatrix::everyAetherRowHasAPodField(SeraphisMacroMatrix::kRows),
              "FR-058: every Aether row must have a 1:1 SeraphisAetherTargets field");
static_assert(SeraphisMacroMatrix::noRowUsesSteppedCurve(SeraphisMacroMatrix::kRows),
              "FR-057 / RA-7: ModCurve::Stepped fails SC-009's continuity bound by construction");
static_assert(SeraphisMacroMatrix::everyTargetInFr061to065IsPresent(SeraphisMacroMatrix::kRows),
              "FR-061..FR-065: every enumerated target must be claimed by at least one row");
static_assert(SeraphisMacroMatrix::everyRowSharesOneBasePerTarget(SeraphisMacroMatrix::kRows),
              "plan §4.3: rows sharing a target must agree on `base`");

}  // namespace DSP
}  // namespace Krate
