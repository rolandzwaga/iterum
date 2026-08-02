// ==============================================================================
// Seraphis - Host-synced morph travel tests (Phase 9)
// ==============================================================================
// Reference: specs/seraphis-phase9-parameters/spec.md
//            specs/seraphis-phase9-parameters/plan.md   (§7.0, §7.14, §3.6)
//
// CRITERIA OWNED BY THIS TU (plan §7.0's test-file map):
//   SC-018  host-synced travel - kMorphTravelSyncId picks the beat division of
//           C-7's table, the travel rate follows the ProcessContext tempo at
//           the sample point §3.6 fixes, and an absent / invalid tempo leaves
//           the free-running rate in charge
//
// The fixture's Phase 9 ProcessContext hook (plan §7.0's "shared fixture
// change") is what makes this TU possible: seraphis_test_fixture.h shipped
// data_.processContext = nullptr in Phase 8.
//
// THE OBSERVABLE FOR CLAUSES 1-4 IS getVoice(0).morph().getTravelRate()
// (spectral_morph_engine.h:441), which returns the pushed travelRate_ EXACTLY.
// It is NOT an inferred rate read off the travel position: at clause 3's
// 1.0417e-2 journeys/s the position moves ~1.1e-4 per 512-sample block, so an
// inference is dominated by the position quantum and by advanceTravel's own slew
// cap - it could not meet the criterion's 1e-5 equality gate for a CORRECT
// implementation. applyVoiceParamsCallCountForTest() is the SECONDARY on every
// clause: the derived rate must have been PUSHED, not merely computed.
//
// THE EXPECTED VALUES ARE COMPUTED FROM C-7's TABLE AS THE PRODUCTION CODE READS
// IT (dropdown_mappings.h's kSyncNoteBeats / kSyncNoteIsBarDenominated), never
// re-derived here - C-7 says that table is the single transcription. What this
// TU hard-codes is the ARITHMETIC RESULT the criterion states in plain numbers
// (0.5, 1.0, 1.0417e-2, 0.667), so a table edit that silently changed a division
// would fail here.
//
// NO std::isnan / std::isinf / std::numeric_limits<>::infinity() ANYWHERE: the
// macOS leg builds with -ffast-math, under which the compiler may assume finite
// values and fold such a test away.
//
// COMPILE FLAGS: this TU is NOT listed under "-fno-fast-math
//   -fno-finite-math-only" in plugins/seraphis/tests/CMakeLists.txt.
// ==============================================================================

#include "processor/processor.h"
#include "seraphis_test_fixture.h"

#include "parameters/dropdown_mappings.h"
#include "parameters/morph_params.h"
#include "plugin_ids.h"

#include "ui/parameter_helpers.h"

#include <krate/dsp/systems/seraphis_engine.h>
#include <krate/dsp/systems/spectral_morph_engine.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <initializer_list>
#include <memory>
#include <vector>

namespace {

using Steinberg::Vst::ParamID;
using Fixture = SeraphisTest::ProcessorFixture;
using Morph = Krate::DSP::SpectralMorphEngine;

constexpr double kSampleRate = 48000.0;
constexpr Steinberg::int32 kBlock = 512;
constexpr std::size_t kBlockSamples = 512;
constexpr std::size_t kMaxVoices = Krate::DSP::SeraphisEngine::kMaxVoices;

/// SC-018's equality gate on the pushed rate.
constexpr double kRateMargin = 1.0e-5;

/// Blocks covering at least `seconds` at 48 kHz / 512.
[[nodiscard]] constexpr std::size_t blocksFor(double seconds) {
    return static_cast<std::size_t>(seconds * kSampleRate / static_cast<double>(kBlockSamples))
           + 1u;
}

/// Clause 5's "does not go silent" floor.
///
/// PROVENANCE, not a guess. processor_audio_test.cpp:120-134 records the MEASURED
/// peak of the shipped chain at the registered defaults (48 kHz / 512, master
/// gain unity, polyphony 8, note 60, 4 s): 0.000959373 on the louder channel, and
/// pins its own SC-005 floor at 5.0e-4 (`:134`, `:643`). Clause 5's arms perturb
/// the morph travel rate away from its default, so this floor takes a further
/// ~5x of margin below that measured figure. It still sits ~40 dB above the two
/// ways a render can genuinely be silent - the zero-fill path and a denormal
/// floor - which is the whole discriminating power the clause asks for.
constexpr float kNonSilencePeakFloor = 1.0e-4f;

// -----------------------------------------------------------------------------
// Rig helpers
// -----------------------------------------------------------------------------

[[nodiscard]] std::unique_ptr<Fixture> makeRig() {
    auto fx = std::make_unique<Fixture>();
    REQUIRE(fx->prepare(kSampleRate, kBlock) == Steinberg::kResultOk);
    return fx;
}

struct ParamPoint {
    ParamID id;
    double normalized;
};

/// The `L` form: a StringListParameter's normalized value for entry `index` of
/// `count`. The exact inverse of Seraphis::detail::morphDropdownIndex
/// (morph_params.h:128-133).
[[nodiscard]] constexpr double dropdownNorm(int index, int count) {
    return (count <= 1) ? 0.0 : static_cast<double>(index) / static_cast<double>(count - 1);
}

/// The plain value ID 404 stores for a given normalized position - the SAME
/// mapping handleMorphParamChange applies (morph_params.h:170-175), so the
/// fallback clauses compare against what the pack actually stored and not
/// against a second transcription of the log map.
[[nodiscard]] float freeTravelRate(double normalized) {
    return static_cast<float>(Krate::Plugins::logMapFromNormalized(
        normalized, Seraphis::kMorphTravelRateMin, Seraphis::kMorphTravelRateMax));
}

/// One block through process(), delivering `points` at sample offset 0. The
/// fixture's ProcessContext (setTempo / clearProcessContext) rides along, because
/// withOutputChannels() attaches it.
void renderOneBlock(Fixture& fx, std::initializer_list<ParamPoint> points) {
    for (const ParamPoint& p : points) {
        fx.params.addQueue(p.id).addTestPoint(0, p.normalized);
    }
    REQUIRE(fx.processBlock(kBlock) == Steinberg::kResultOk);
}

/// The pushed rate on voice 0 - SC-018's stated observable.
[[nodiscard]] float travelRateOfVoice0(Fixture& fx) {
    Krate::DSP::SeraphisEngine* engine = fx.proc->engineForTest();
    REQUIRE(engine != nullptr);
    return engine->getVoice(0).morph().getTravelRate();
}

/// FR-002's broadcast bound is kMaxVoices, so every slot must carry the rate -
/// not only the sounding ones.
void requireEveryVoiceAt(Fixture& fx, double expected) {
    Krate::DSP::SeraphisEngine* engine = fx.proc->engineForTest();
    REQUIRE(engine != nullptr);
    for (std::size_t v = 0; v < kMaxVoices; ++v) {
        INFO("voice " << v);
        REQUIRE(engine->getVoice(v).morph().getTravelRate()
                == Catch::Approx(expected).margin(kRateMargin));
    }
}

[[nodiscard]] float maxAbs(const std::vector<float>& v) {
    float peak = 0.0f;
    for (const float s : v) {
        peak = std::max(peak, std::abs(s));
    }
    return peak;
}

// C-7's eight rows, as the production code reads them. Pinned here so a clause
// that names "index 4" is checked against the table it means.
static_assert(Seraphis::kSyncNoteBeats.size() == 8, "C-7: eight sync-note rows");
static_assert(Seraphis::kSyncNoteBeats[0] == 0.25 && !Seraphis::kSyncNoteIsBarDenominated[0],
              "SC-018 clause 2 names index 0 = 1/16 = 0.25 beats, bar-independent");
static_assert(Seraphis::kSyncNoteBeats[4] == 1.0 && Seraphis::kSyncNoteIsBarDenominated[4],
              "SC-018 clauses 1 and 4 name index 4 = 1 Bar");
static_assert(Seraphis::kSyncNoteBeats[7] == 8.0 && Seraphis::kSyncNoteIsBarDenominated[7],
              "SC-018 clause 3 names index 7 = 8 Bars");

/// The dropdown normalized values the clauses use.
constexpr double kNote1_16 = dropdownNorm(0, 8);
constexpr double kNote1Bar = dropdownNorm(4, 8);
constexpr double kNote8Bars = dropdownNorm(7, 8);

/// ID 404 pinned to its MINIMUM (kMinTravelRate = 1.667e-3). Every clause that
/// asserts a DERIVED rate pins the free-running value here, so a passing
/// assertion cannot be the free rate wearing the derived rate's clothes: 1.667e-3
/// is more than 1e-5 away from 0.5, 1.0, 1.0417e-2 and 0.667 alike.
constexpr double kFreeRateNormMin = 0.0;

/// ID 404 pinned to its MAXIMUM (kMaxTravelRate = 1.0), used by the fallback
/// clause - where the free value is what must be OBSERVED.
constexpr double kFreeRateNormMax = 1.0;

}  // namespace

// =============================================================================
// SC-018 - host-synced travel is correct and degrades safely
// =============================================================================

TEST_CASE("Seraphis_MorphSync_DerivesAndFallsBack", "[seraphis][morph][sync]") {

    // -------------------------------------------------------------------------
    // Clause 1 - derivation.
    // 120 BPM, sync on, "1 Bar" (index 4), 4/4 -> 120 / (60 * 4) = 0.5 j/s.
    // -------------------------------------------------------------------------
    SECTION("clause 1 - 120 BPM, 1 Bar, 4/4 derives 0.5 journeys per second") {
        auto fx = makeRig();
        fx->setTempo(120.0, 4, 4, /*tempoValid*/ true, /*sigValid*/ true);

        const std::size_t before = fx->proc->applyVoiceParamsCallCountForTest();
        renderOneBlock(*fx, {{Seraphis::kMorphTravelRateId, kFreeRateNormMin},
                             {Seraphis::kMorphSyncId, 1.0},
                             {Seraphis::kMorphSyncNoteId, kNote1Bar}});

        REQUIRE(travelRateOfVoice0(*fx) == Catch::Approx(0.5).margin(kRateMargin));
        requireEveryVoiceAt(*fx, 0.5);

        // SECONDARY: the derived rate reached the voices through a real push.
        REQUIRE(fx->proc->applyVoiceParamsCallCountForTest() > before);

        // NEGATIVE CONTROL: the free-running value is NOT what was observed.
        REQUIRE(freeTravelRate(kFreeRateNormMin)
                == Catch::Approx(Morph::kMinTravelRate).margin(kRateMargin));
        REQUIRE(std::abs(travelRateOfVoice0(*fx) - Morph::kMinTravelRate) > kRateMargin);
    }

    // -------------------------------------------------------------------------
    // Clause 2 - the UPPER clamp.
    // 200 BPM, "1/16" (index 0, 0.25 beats) -> 200 / (60 * 0.25) = 13.333 j/s,
    // which kMaxTravelRate = 1.0 clamps.
    // -------------------------------------------------------------------------
    SECTION("clause 2 - 200 BPM at 1/16 clamps to kMaxTravelRate") {
        auto fx = makeRig();
        fx->setTempo(200.0, 4, 4, true, true);

        const std::size_t before = fx->proc->applyVoiceParamsCallCountForTest();
        renderOneBlock(*fx, {{Seraphis::kMorphTravelRateId, kFreeRateNormMin},
                             {Seraphis::kMorphSyncId, 1.0},
                             {Seraphis::kMorphSyncNoteId, kNote1_16}});

        // The unclamped derivation is far above the ceiling, so this asserts the
        // clamp and not an accidental coincidence.
        REQUIRE(200.0 / (60.0 * 0.25) > static_cast<double>(Morph::kMaxTravelRate));
        REQUIRE(travelRateOfVoice0(*fx)
                == Catch::Approx(Morph::kMaxTravelRate).margin(kRateMargin));
        requireEveryVoiceAt(*fx, Morph::kMaxTravelRate);
        REQUIRE(fx->proc->applyVoiceParamsCallCountForTest() > before);
    }

    // -------------------------------------------------------------------------
    // Clause 3 - NO clamp at the slow end, asserted as an exact value.
    // 20 BPM, "8 Bars" (index 7 -> 8 x barBeats = 32 beats at 4/4) ->
    // 20 / (60 * 32) = 1.0416667e-2 j/s, which is ABOVE kMinTravelRate.
    // -------------------------------------------------------------------------
    SECTION("clause 3 - 20 BPM at 8 Bars derives 1.0417e-2 with no clamp") {
        auto fx = makeRig();
        fx->setTempo(20.0, 4, 4, true, true);

        const double expected = 20.0 / (60.0 * 32.0);  // 1.0416667e-2

        // The lower clamp must NOT engage - that is what "no clamp" means here.
        REQUIRE(expected > static_cast<double>(Morph::kMinTravelRate));
        REQUIRE(expected < static_cast<double>(Morph::kMaxTravelRate));

        const std::size_t before = fx->proc->applyVoiceParamsCallCountForTest();
        renderOneBlock(*fx, {{Seraphis::kMorphTravelRateId, kFreeRateNormMin},
                             {Seraphis::kMorphSyncId, 1.0},
                             {Seraphis::kMorphSyncNoteId, kNote8Bars}});

        REQUIRE(travelRateOfVoice0(*fx) == Catch::Approx(expected).margin(kRateMargin));
        REQUIRE(travelRateOfVoice0(*fx) == Catch::Approx(1.0416667e-2).margin(kRateMargin));
        requireEveryVoiceAt(*fx, expected);
        REQUIRE(fx->proc->applyVoiceParamsCallCountForTest() > before);

        // And it is NOT the floor: a clamped implementation would report
        // kMinTravelRate here, which is 8.75e-3 away - well outside the gate.
        REQUIRE(std::abs(travelRateOfVoice0(*fx) - Morph::kMinTravelRate) > kRateMargin);
    }

    // -------------------------------------------------------------------------
    // Clause 4 - the time signature, BOTH halves of C-7's bar rule.
    //   kTimeSigValid set, 6/8 -> barBeats = 6 * (4/8) = 3 -> 120/(60*3) = 0.667
    //   flag CLEAR                -> barBeats = 4          -> 120/(60*4) = 0.5
    // The second half is driven WITHOUT touching a parameter: only the transport
    // changes, which additionally shows the tempo is sampled every process()
    // call and not latched at the last parameter edit.
    // -------------------------------------------------------------------------
    SECTION("clause 4 - 6/8 gives barBeats 3; an invalid time signature falls back to 4") {
        auto fx = makeRig();
        fx->setTempo(120.0, 6, 8, /*tempoValid*/ true, /*sigValid*/ true);

        const std::size_t before = fx->proc->applyVoiceParamsCallCountForTest();
        renderOneBlock(*fx, {{Seraphis::kMorphTravelRateId, kFreeRateNormMin},
                             {Seraphis::kMorphSyncId, 1.0},
                             {Seraphis::kMorphSyncNoteId, kNote1Bar}});

        const double sixEight = 120.0 / (60.0 * 3.0);  // 0.6666667
        REQUIRE(travelRateOfVoice0(*fx) == Catch::Approx(sixEight).margin(kRateMargin));
        requireEveryVoiceAt(*fx, sixEight);
        REQUIRE(fx->proc->applyVoiceParamsCallCountForTest() > before);

        // Same 6/8 numbers, but the host no longer marks the signature valid:
        // barBeats falls back to common time and the rate becomes 0.5.
        const std::size_t mid = fx->proc->applyVoiceParamsCallCountForTest();
        fx->setTempo(120.0, 6, 8, /*tempoValid*/ true, /*sigValid*/ false);
        renderOneBlock(*fx, {});

        REQUIRE(travelRateOfVoice0(*fx) == Catch::Approx(0.5).margin(kRateMargin));
        requireEveryVoiceAt(*fx, 0.5);
        REQUIRE(fx->proc->applyVoiceParamsCallCountForTest() > mid);
    }

    // -------------------------------------------------------------------------
    // Clause 5 - the FALLBACK, in its two stated shapes. In both, sync is ON and
    // the free-running ID 404 value must be used UNCHANGED: never silence, never
    // zero, and never a retained stale synced rate.
    // -------------------------------------------------------------------------
    SECTION("clause 5a - a context that disappears restores the free-running rate") {
        auto fx = makeRig();
        fx->setTempo(120.0, 4, 4, true, true);

        // First establish a DERIVED rate, so the assertion below can tell
        // "fell back" from "never synced in the first place".
        renderOneBlock(*fx, {{Seraphis::kMorphTravelRateId, kFreeRateNormMax},
                             {Seraphis::kMorphSyncId, 1.0},
                             {Seraphis::kMorphSyncNoteId, kNote1Bar}});
        REQUIRE(travelRateOfVoice0(*fx) == Catch::Approx(0.5).margin(kRateMargin));

        // Host stops supplying a transport. Sync stays ON.
        const std::size_t before = fx->proc->applyVoiceParamsCallCountForTest();
        fx->clearProcessContext();
        renderOneBlock(*fx, {});

        const float freeRate = freeTravelRate(kFreeRateNormMax);
        REQUIRE(freeRate == Catch::Approx(Morph::kMaxTravelRate).margin(kRateMargin));
        REQUIRE(travelRateOfVoice0(*fx) == Catch::Approx(freeRate).margin(kRateMargin));
        requireEveryVoiceAt(*fx, freeRate);

        // NOT the stale 0.5, and NOT zero.
        REQUIRE(std::abs(travelRateOfVoice0(*fx) - 0.5f) > kRateMargin);
        REQUIRE(travelRateOfVoice0(*fx) > 0.0f);

        // The fallback is PUSHED, not merely computed.
        REQUIRE(fx->proc->applyVoiceParamsCallCountForTest() > before);
    }

    SECTION("clause 5b - an invalid tempo flag leaves ID 404 in charge and the render sounds") {
        auto fx = makeRig();
        // A context IS attached, but the host does not mark the tempo valid.
        fx->setTempo(120.0, 4, 4, /*tempoValid*/ false, /*sigValid*/ true);

        const std::size_t before = fx->proc->applyVoiceParamsCallCountForTest();
        const std::size_t blocks = blocksFor(4.0);
        fx->renderBlocks(
            blocks, kBlockSamples,
            [&](std::size_t b, Krate::Test::EventList&, SeraphisTest::ParameterChanges& pc) {
                if (b != 0) {
                    return;
                }
                pc.addQueue(Seraphis::kMorphTravelRateId).addTestPoint(0, kFreeRateNormMax);
                pc.addQueue(Seraphis::kMorphSyncId).addTestPoint(0, 1.0);
                pc.addQueue(Seraphis::kMorphSyncNoteId).addTestPoint(0, kNote1Bar);
                fx->pushEvent(Steinberg::Vst::Event::kNoteOnEvent,
                              static_cast<Steinberg::int16>(60), 100.0f / 127.0f, 0);
            });

        const float freeRate = freeTravelRate(kFreeRateNormMax);
        REQUIRE(travelRateOfVoice0(*fx) == Catch::Approx(freeRate).margin(kRateMargin));
        requireEveryVoiceAt(*fx, freeRate);
        REQUIRE(fx->proc->applyVoiceParamsCallCountForTest() > before);

        // "does not go silent" - the criterion's own words. See
        // kNonSilencePeakFloor's provenance block.
        const float peak = std::max(maxAbs(fx->capturedL), maxAbs(fx->capturedR));
        WARN("SC-018 clause 5b (sync on, kTempoValid clear, note 60, 4 s): peak L="
             << maxAbs(fx->capturedL) << " R=" << maxAbs(fx->capturedR)
             << " | floor = " << kNonSilencePeakFloor);
        REQUIRE(peak >= kNonSilencePeakFloor);

        // The fixture's guard words are still intact - no out-of-bounds write.
        REQUIRE(fx->checkCanaries());
    }
}
