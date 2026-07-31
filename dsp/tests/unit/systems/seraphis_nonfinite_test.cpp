// ==============================================================================
// Layer 3: System Tests - Seraphis non-finite hygiene (SC-018)
//                                    (specs/seraphis-phase7-voice-engine)
// ==============================================================================
// Constitution Principle XII: Test-First Development.
//
// Reference: specs/seraphis-phase7-voice-engine/spec.md   (SC-018, FR-072, FR-084)
//            specs/seraphis-phase7-voice-engine/plan.md   (§2.12, §3.4, §6.2)
//            specs/seraphis-phase7-voice-engine/tasks.md  (T001 creates this TU,
//                                                          T015 lands this case)
//
// SCOPE OF THIS TU: SC-018 ONLY - NaN/Inf injection into voice and engine state,
//   the FR-035 stateFinite() sweep, and the FR-072 deferred per-voice reset with
//   its getNonFiniteRecoveryCount() accounting.
//
// THIS IS A SEPARATE TU BECAUSE OF ITS COMPILE FLAGS. It is the ONLY one of the
//   five Phase 7 TUs listed under "-fno-fast-math -fno-finite-math-only" in
//   dsp/tests/CMakeLists.txt:740; those flags must NOT be applied to the other
//   four. Non-finite inputs are built from BIT PATTERNS through a volatile sink,
//   never from std::numeric_limits, because the rest of the suite ships under
//   /fp:fast + -ffast-math where those constants fold to finite garbage.
//   Do not merge these cases into the main TUs.
//
// TWO INJECTIONS, BECAUSE FR-072 HAS TWO HALVES AND ONLY ONE OF THEM IS
// REACHABLE THROUGH A PUBLIC SETTER:
//
//   (a) THE REAL NON-FINITE PARAMETER SURFACE. Every SeraphisVoice forwarder is
//       driven with a bit-pattern quiet NaN / +Inf / -Inf. This is the half that
//       actually needs the -fno-fast-math flags, and it is what proves the
//       sanitiser chain seraphis_engine.h's banner enumerates (:43-60) is real:
//       harmonic_cloud / continuous_body / atmosphere_engine setters all do
//       `isFinite(x) ? x : default`, spectral_morph_engine and entropy_processor
//       REJECT non-finite outright (spectral_morph_engine.h:332-338, :350-353,
//       :358-361; entropy_processor.h:230-234), and OrbitModulator's four
//       std::clamp-only setters (orbit_modulator.h:166-186 - std::clamp passes
//       NaN straight through) funnel into OnePoleSmoother::setTarget, which
//       substitutes 0 for NaN and +-1e10 for Inf (smoother.h:170-181).
//
//   (b) THE FR-072 GUARD ITSELF. Because (a) is sealed, NO legal call sequence
//       can make a voice emit a non-finite sample, so the per-sample guard at
//       the accumulation point (seraphis_engine.h:477-481) is unreachable from
//       the public API. That is a property of the composed components, not an
//       oversight - Phase 6 hit the identical wall and answered it with a
//       fault-injection hook (aether_reverb.h:2691-2722,
//       injectNonFiniteStateForTest). The Layer 3 equivalent is the friend
//       probe DECLARED at seraphis_engine.h:113 and defined below, which raises
//       exactly the bit the guard raises, i.e. it stands in for a detection that
//       has already happened.
//
//   Both land on the SAME slot in Part 1, so SC-018's "increments exactly once"
//   stays literally true: (b) contributes the one recovery and (a) contributes
//   none. A count ABOVE one is therefore a real finding - it would mean one of
//   the sanitisers above has a hole and a voice reached the bus non-finite.
//
// THE ENGINE-SIDE HALF OF FR-072 IS NOT REPEATED HERE. The servicing bound
//   (kResetsPerControlChunk = 1, four poisoned slots needing four control
//   chunks) is asserted by SeraphisEngine_NonFiniteContainmentIsBounded in
//   seraphis_engine_test.cpp:2093. This TU owns the COMPOSED-CHAIN half.
//
// ODR NOTE ON THE PROBE. Krate::DSP::detail::SeraphisEngineNonFiniteProbe is
//   defined in two TUs of dsp_systems_tests (here and
//   seraphis_engine_test.cpp:1826). That is well formed only because the two
//   definitions are the SAME sequence of tokens naming the same entities
//   ([basic.def.odr]); the library declares the type but never defines it, and
//   there is no shipped header either TU could share it through. Keep the two
//   copies byte-identical.
// ==============================================================================

#include <catch2/catch_all.hpp>

// Layer 4, reached ONLY from test TUs and the FR-070 helper: AetherReverb is
// what SeraphisEngine deliberately does not own (spec FR-070).
#include <krate/dsp/effects/aether_reverb.h>
#include <krate/dsp/systems/seraphis_engine.h>
#include <krate/dsp/systems/seraphis_macro_matrix.h>
#include <krate/dsp/systems/seraphis_voice.h>
#include <krate/dsp/systems/voice_allocator.h>

#include <render_fingerprint.h>
#include <seraphis_chain.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <vector>

using Krate::DSP::AetherReverb;
using Krate::DSP::SeraphisEngine;
using Krate::DSP::SeraphisEngineConfig;
using Krate::DSP::SeraphisMacroMatrix;
using Krate::DSP::SeraphisVoice;
using Krate::DSP::SeraphisVoiceConfig;
using Krate::DSP::VoiceState;
using Krate::DSP::TestUtils::compareFingerprints;
using Krate::DSP::TestUtils::fingerprintRender;
using Krate::DSP::TestUtils::renderSeraphisChain;
using Krate::DSP::TestUtils::SeraphisChainScript;

// -----------------------------------------------------------------------------
// FR-072 fault-injection probe. See the ODR NOTE in this file's banner: this
// definition is a byte-identical twin of seraphis_engine_test.cpp:1826.
// -----------------------------------------------------------------------------
namespace Krate::DSP::detail {
struct SeraphisEngineNonFiniteProbe {
    static void markVoiceContributionNonFinite(SeraphisEngine& engine, std::size_t v) noexcept {
        engine.nonFinitePending_ |= SeraphisEngine::voiceBit(v);
    }
};
}  // namespace Krate::DSP::detail

namespace {

// =============================================================================
// Shared constants
// =============================================================================

constexpr double kSr = 48000.0;
/// Caller block size for the composed chain. 512 = 8 x 64, a whole number of
/// control chunks: partial-chunk behaviour is SC-014's subject, not this one's.
constexpr std::size_t kBlock = 512;
constexpr std::uint8_t kVel = 100;

/// Four notes onto an empty pool, so slots 0..3 sound and 4..15 stay idle under
/// the allocator's Oldest mode (seraphis_engine.h:216).
constexpr std::size_t kNotes = 4;
/// A SOUNDING slot - Part 1's target.
constexpr std::size_t kPoisonSlot = 2;
/// A never-noted slot - Part 2's target. Idle and isFinished(), so it renders
/// nothing in EITHER arm and the two fingerprints can be compared directly.
constexpr std::size_t kIdleSlot = 7;

/// The three bit patterns, named once rather than spelled at the injection
/// sites.
constexpr std::uint32_t kQuietNaNBits = 0x7FC00000u;
constexpr std::uint32_t kPosInfBits = 0x7F800000u;
constexpr std::uint32_t kNegInfBits = 0xFF800000u;

/// IEEE-754 binary32 exponent field, all ones == Inf or NaN.
constexpr std::uint32_t kExponentMask = 0x7F800000u;

// =============================================================================
// Non-finite construction and classification
// =============================================================================

/// @brief Build a non-finite float from its bit pattern through a volatile sink.
///
/// std::numeric_limits<float>::quiet_NaN() / infinity() fold to FINITE GARBAGE
/// under -ffast-math, which the macOS CI leg uses, so they are never used - even
/// though this TU itself carries -fno-fast-math -fno-finite-math-only. The
/// volatile READ is the sink: it is what stops the constant from being folded
/// back into the memcpy at compile time.
[[nodiscard]] float makeNonFinite(std::uint32_t bits) noexcept {
    volatile std::uint32_t b = bits;
    const std::uint32_t materialized = b;
    float f = 0.0f;
    std::memcpy(&f, &materialized, sizeof(f));
    return f;
}

/// @brief FR-008's finiteness test, as the plan §2.12 memcpy-bit form.
///
/// Never std::isnan / std::isinf / std::isfinite: FR-008 forbids them
/// phase-wide, and this is integer arithmetic on the bit pattern, so it reads
/// correctly under any fast-math setting rather than only under this TU's.
[[nodiscard]] bool sampleIsFinite(float value) noexcept {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return (bits & kExponentMask) != kExponentMask;
}

// =============================================================================
// Fixtures
// =============================================================================

/// `note` as a MIDI byte, explicitly, so no int -> uint8_t narrowing is left to
/// the compiler's discretion.
[[nodiscard]] constexpr std::uint8_t midi(int note) noexcept {
    return static_cast<std::uint8_t>(note);
}

/// Heap-allocate, prepare, return. A SeraphisEngine is ~750 KB (16 voices by
/// value) against MSVC's 1 MiB default main-thread stack and dsp/tests/
/// CMakeLists.txt sets no /STACK, so it must NEVER be a test local (plan §6.3).
[[nodiscard]] std::unique_ptr<SeraphisEngine> makeEngine(std::size_t polyphony,
                                                         std::uint32_t seed) {
    auto engine = std::make_unique<SeraphisEngine>();
    engine->prepare(kSr, SeraphisEngineConfig{.voice = SeraphisVoiceConfig{},
                                              .polyphony = polyphony,
                                              .seed = seed});
    return engine;
}

/// Heap-allocated for the same reason, and because AetherReverb is non-copyable
/// (aether_reverb.h:1598-1599).
[[nodiscard]] std::unique_ptr<AetherReverb> makeReverb() {
    auto reverb = std::make_unique<AetherReverb>();
    reverb->prepare(kSr, AetherReverb::PrepareConfig{.numChannels = std::size_t{8},
                                                     .maxBlockSamples = std::size_t{2048},
                                                     .bloomEnabled = true});
    return reverb;
}

/// The engine hands out `const SeraphisVoice&` (FR-085 introspection), but the
/// FR-030 parameter surface this case has to poison lives on the voice itself.
/// Same treatment seraphis_engine_test.cpp:199 gives it.
[[nodiscard]] SeraphisVoice& mutableVoice(SeraphisEngine& engine, std::size_t v) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
    return const_cast<SeraphisVoice&>(engine.getVoice(v));
}

/// Render `seconds` of the FR-070 composed chain
/// (voice sum -> AetherReverb -> processOutputStage) with no script events; the
/// notes are dispatched directly by the caller before the render.
void renderChain(SeraphisEngine& engine, AetherReverb& reverb, const SeraphisMacroMatrix& macros,
                 double seconds, std::vector<float>& outL, std::vector<float>& outR) {
    const SeraphisChainScript emptyScript{};
    const auto total = static_cast<std::size_t>(seconds * kSr);
    renderSeraphisChain(engine, reverb, macros, emptyScript, kSr, kBlock, total, outL, outR);
}

/// Accumulated rather than REQUIREd per sample: 240 000 assertions is a
/// different test's worth of runtime. `firstBad` is the diagnostic.
[[nodiscard]] bool buffersAllFinite(const std::vector<float>& l, const std::vector<float>& r,
                                    std::size_t& firstBad) {
    firstBad = l.size();
    for (std::size_t i = 0; i < l.size(); ++i) {
        if (!sampleIsFinite(l[i]) || !sampleIsFinite(r[i])) {
            firstBad = i;
            return false;
        }
    }
    return true;
}

/// RMS of the mono sum, accumulated in double: the voice bus sits near 3e-3 and
/// a float accumulator over 240 000 samples loses too much of it.
[[nodiscard]] double rmsOf(const std::vector<float>& l, const std::vector<float>& r) {
    if (l.empty()) {
        return 0.0;
    }
    double sumSq = 0.0;
    for (std::size_t i = 0; i < l.size(); ++i) {
        const double m = 0.5 * (static_cast<double>(l[i]) + static_cast<double>(r[i]));
        sumSq += m * m;
    }
    return std::sqrt(sumSq / static_cast<double>(l.size()));
}

/// @brief Drive the whole FR-030 parameter surface with bit-pattern non-finites.
///
/// SCOPE, STATED. This covers exactly the forwarders seraphis_engine.h's banner
/// (:43-60) enumerates as sanitising - cloud, morph, body, atmosphere and the
/// four OrbitModulator axes plus the M/S width centre. The envelope TIME
/// setters (setEnvelopeStageTimeMs / setEnvelopeReleaseMs /
/// setGrowthDurationSeconds) are deliberately NOT poisoned here: they are
/// outside that enumerated set, they gate no signal path that FR-072's
/// accumulation-point guard does not already cover, and a criterion about
/// CONTAINMENT must not quietly turn into an un-specced sanitiser audit of a
/// different surface.
///
/// The three patterns are rotated across the surface rather than applied one at
/// a time, because the two sanitiser SHAPES answer them differently. The
/// `isFinite(x) ? x : default` setters take the default branch for all three,
/// but the clamp-only spatial setters do not: std::clamp passes NaN straight
/// through (neither `NaN < lo` nor `hi < NaN` is true) while it folds +-Inf onto
/// the bound. A NaN-only sweep would therefore never reach the Inf-folding
/// branch, and an Inf-only sweep would never reach the smoother's
/// NaN-substitution branch, which is the one that actually matters.
void poisonVoiceParameterSurface(SeraphisVoice& voice) {
    const float qnan = makeNonFinite(kQuietNaNBits);
    const float pinf = makeNonFinite(kPosInfBits);
    const float ninf = makeNonFinite(kNegInfBits);

    // Cloud (harmonic_cloud.h:412/426/439/452/478/501/535/556/568).
    voice.setRichness(qnan);
    voice.setInharmonicity(pinf);
    voice.setSpectralTiltDb(ninf);
    voice.setMutation(qnan);
    voice.setSpectralGravity(pinf);
    voice.setDriftDepthCents(ninf);
    voice.setStereoSpread(qnan);
    voice.setAttackTimeSec(pinf);
    voice.setDecayTimeSec(ninf);

    // Morph + entropy (spectral_morph_engine.h:332/350/358, entropy_processor.h:230).
    voice.setEntropy(qnan);
    voice.setBloom(pinf);
    voice.setTargetPosition(ninf);
    voice.setTravelRate(qnan);

    // Body (continuous_body.h:953/962/971/992/1001/1011/1022/1033/1046/1056).
    voice.setResonance(pinf);
    voice.setDamping(ninf);
    voice.setKeyTracking(qnan);
    voice.setDrive(pinf);
    voice.setMix(ninf);
    voice.setCloudMix(qnan);
    voice.setCloudDecaySec(pinf);
    voice.setCloudSize(ninf);
    voice.setCloudDamping(qnan);
    voice.setWidth(pinf);

    // Atmosphere (atmosphere_engine.h:779/792/836/859/866/874/882/946).
    voice.setLevel(ninf);
    voice.setBlur(qnan);
    voice.setDensity(pinf);
    voice.setGrainSeconds(ninf);
    voice.setDriftDepth(qnan);
    voice.setPanSpread(pinf);
    voice.setDecorrelation(ninf);
    voice.setFreezeMix(qnan);

    // Spatial. THE FOUR ORBIT AXES ARE THE ONLY UNSANITISED SETTERS IN THE WHOLE
    // SURFACE: std::clamp returns NaN unchanged (orbit_modulator.h:166-186), so
    // the NaN reaches the orbit's own state and is scrubbed one level down, at
    // OnePoleSmoother::setTarget (smoother.h:170-181). +-Inf is folded to the
    // clamp bound and never gets that far.
    voice.setSpatialDepth(pinf);
    voice.setSpatialRate(ninf);
    voice.setSpatialCoupling(qnan);
    voice.setSpatialGrowth(pinf);
    // The M/S width centre IS sanitised, in SeraphisVoice itself
    // (seraphis_voice.h:604-609, `if (!isFiniteBits(pct)) return;`).
    voice.setVoiceWidthBasePercent(ninf);
}

}  // namespace

// =============================================================================
// SC-018 - a non-finite injection is CONTAINED to the voice it was injected into
// =============================================================================

TEST_CASE("SeraphisEngine_NonFiniteIsContained") {
    using Krate::DSP::detail::SeraphisEngineNonFiniteProbe;

    // -------------------------------------------------------------------------
    // PART 1 - the poisoned slot is SOUNDING.
    //
    // Clauses: composed-chain output finite for EVERY sample of the next 5 s;
    // getNonFiniteRecoveryCount() increments exactly once; stateFinite() on the
    // recovered voice is true afterwards.
    // -------------------------------------------------------------------------
    {
        auto engine = makeEngine(std::size_t{8}, 7u);
        auto reverb = makeReverb();
        const SeraphisMacroMatrix macros{};

        for (std::size_t i = 0; i < kNotes; ++i) {
            engine->noteOn(midi(60 + 4 * static_cast<int>(i)), kVel);
            CAPTURE(i);
            REQUIRE(engine->getVoiceState(i) == VoiceState::Active);
        }

        // Warm-up, so the poisoned slot is genuinely sounding when it is hit
        // rather than sitting at the head of its attack. FR-033's detector has
        // an instant attack, so half a second is far more than enough.
        std::vector<float> warmL;
        std::vector<float> warmR;
        renderChain(*engine, *reverb, macros, 0.5, warmL, warmR);
        REQUIRE(engine->getNonFiniteRecoveryCount() == std::uint32_t{0});
        CAPTURE(engine->getVoiceLevel(kPoisonSlot));
        REQUIRE(engine->getVoiceLevel(kPoisonSlot) > 0.0f);

        // Injection (a): the real bit-pattern non-finite parameter surface.
        // NOTE ON LIFETIME: renderSeraphisChain calls macros.apply(engine) on
        // every sub-slice, so the FR-058 macro-owned targets are rewritten with
        // their neutral base on the first slice after this call. That is fine -
        // for those rows the substituted value only has to survive to the first
        // slice - and every target OUTSIDE the FR-058 table (the majority of the
        // surface above) keeps its substitution for the whole 5 s render.
        poisonVoiceParameterSurface(mutableVoice(*engine, kPoisonSlot));

        // Injection (b): the FR-072 guard bit, which no legal call sequence can
        // raise (banner, half (b)).
        SeraphisEngineNonFiniteProbe::markVoiceContributionNonFinite(*engine, kPoisonSlot);

        std::vector<float> outL;
        std::vector<float> outR;
        renderChain(*engine, *reverb, macros, 5.0, outL, outR);
        REQUIRE(outL.size() == static_cast<std::size_t>(5.0 * kSr));
        REQUIRE(outR.size() == outL.size());

        std::size_t firstBad = 0;
        const bool finite = buffersAllFinite(outL, outR, firstBad);
        CAPTURE(firstBad);
        REQUIRE(finite);

        // Non-vacuity: a chain that had gone silent would satisfy the clause
        // above for free. The bus itself sits near 3e-3.
        const double rms = rmsOf(outL, outR);
        CAPTURE(rms);
        REQUIRE(rms > 1.0e-4);

        // EXACTLY once. The pending bit is cleared when it is serviced and a
        // reset voice cannot re-trip anything, so a count of 0 means the
        // deferred recovery never ran and a count ABOVE 1 means one of the
        // sanitisers in the banner has a hole (see "Both land on the SAME slot"
        // there).
        CAPTURE(engine->getNonFiniteRecoveryCount());
        REQUIRE(engine->getNonFiniteRecoveryCount() == std::uint32_t{1});

        // FR-035: the recovered slot is a healthy voice again, not a latched
        // corpse - and it stays healthy with the poisoned parameter values still
        // installed.
        REQUIRE(engine->getVoice(kPoisonSlot).stateFinite());
    }

    // -------------------------------------------------------------------------
    // PART 2 - containment: the OTHER voices are bit-for-bit unaffected.
    //
    // The poisoned slot here is one that is NEVER NOTED, so it is Idle and
    // isFinished() and therefore takes the advanceLifeOnly branch in BOTH arms
    // (seraphis_engine.h's isRendering predicate). Its contribution to the bus
    // is zero either way, which is exactly what makes the two renders
    // comparable: any difference between them is the recovery leaking into the
    // rest of the pool, the FR-052 sum gain or the shared FR-053a output stage.
    //
    // Isolating "the other voices" from a SOUNDING poisoned slot is not
    // possible - the engine exposes a summed bus, not per-voice taps - so this
    // is the strongest form the clause can take.
    // -------------------------------------------------------------------------
    {
        auto control = makeEngine(std::size_t{8}, 7u);
        auto controlReverb = makeReverb();
        auto injected = makeEngine(std::size_t{8}, 7u);
        auto injectedReverb = makeReverb();
        const SeraphisMacroMatrix macros{};

        for (std::size_t i = 0; i < kNotes; ++i) {
            const std::uint8_t note = midi(60 + 4 * static_cast<int>(i));
            control->noteOn(note, kVel);
            injected->noteOn(note, kVel);
        }

        std::vector<float> warmL;
        std::vector<float> warmR;
        renderChain(*control, *controlReverb, macros, 0.5, warmL, warmR);
        renderChain(*injected, *injectedReverb, macros, 0.5, warmL, warmR);

        // The premise of the comparison, asserted rather than assumed.
        REQUIRE(injected->getVoiceState(kIdleSlot) == VoiceState::Idle);
        REQUIRE(injected->getVoice(kIdleSlot).isFinished());
        REQUIRE(control->getVoiceState(kIdleSlot) == VoiceState::Idle);

        SeraphisEngineNonFiniteProbe::markVoiceContributionNonFinite(*injected, kIdleSlot);

        std::vector<float> refL;
        std::vector<float> refR;
        std::vector<float> actL;
        std::vector<float> actR;
        renderChain(*control, *controlReverb, macros, 2.0, refL, refR);
        renderChain(*injected, *injectedReverb, macros, 2.0, actL, actR);

        REQUIRE(control->getNonFiniteRecoveryCount() == std::uint32_t{0});
        REQUIRE(injected->getNonFiniteRecoveryCount() == std::uint32_t{1});

        const auto referenceL = fingerprintRender(std::span<const float>(refL));
        const auto referenceR = fingerprintRender(std::span<const float>(refR));

        // Non-vacuity again: two silent renders are trivially identical.
        CAPTURE(referenceL.rms, referenceR.rms);
        REQUIRE(referenceL.rms > 1.0e-4);
        REQUIRE(referenceR.rms > 1.0e-4);

        // FR-084: fingerprint tolerances, never a bit-exact float digest.
        const auto comparisonL =
            compareFingerprints(fingerprintRender(std::span<const float>(actL)), referenceL);
        CAPTURE(comparisonL.detail, comparisonL.worstMetricRelativeError,
                comparisonL.worstSampleError);
        REQUIRE(comparisonL.withinTolerance());

        const auto comparisonR =
            compareFingerprints(fingerprintRender(std::span<const float>(actR)), referenceR);
        CAPTURE(comparisonR.detail, comparisonR.worstMetricRelativeError,
                comparisonR.worstSampleError);
        REQUIRE(comparisonR.withinTolerance());

        // ...and the injected arm is finite throughout, which the fingerprint
        // comparison alone would NOT catch: a NaN checkpoint compares unequal to
        // itself, but the aggregate metrics would already have gone NaN and
        // std::abs(NaN - NaN) > tolerance is false, so the comparison can pass
        // vacuously on a poisoned buffer.
        std::size_t firstBad = 0;
        const bool finite = buffersAllFinite(actL, actR, firstBad);
        CAPTURE(firstBad);
        REQUIRE(finite);

        // FR-035 on the recovered slot, as in Part 1.
        REQUIRE(injected->getVoice(kIdleSlot).stateFinite());
    }
}
