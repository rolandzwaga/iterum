// ==============================================================================
// Seraphis - Parameter denormalization tests (T020)
// ==============================================================================
// FR-042, FR-043, FR-048 -> SC-009.
//
// HOW THE ATOMICS ARE OBSERVED, and why it is not circular.
// Processor::processParameterChanges() is private and so are globalParams_ /
// macroParams_. The only public surfaces that touch them are process() (which
// calls processParameterChanges() FIRST, before every shape guard) and
// getState() (which serializes all eight values in the fixed layout of plan
// 3.4). This file therefore drives the parameters through process() and reads
// them back through getState().
//
// That pairing is not circular: T019's Seraphis_StateRoundTrip already pins
// getState()'s layout and its non-vacuity independently of this file (it seeds
// through setState() and asserts the decoded bytes), so a getState() that wrote
// constants is caught there, not assumed away here.
//
// Every process() call below renders ZERO samples (numSamples == 0). That is
// deliberate: FR-042's latching must happen even for a block that renders
// nothing, and it keeps the case free of the 771 968 B engine's prepare().
// ==============================================================================

#include "controller/controller.h"
#include "parameters/dropdown_mappings.h"
#include "parameters/effects_params.h"
#include "plugin_ids.h"
#include "processor/processor.h"
#include "seraphis_test_fixture.h"

#include "base/source/fstreamer.h"
#include "pluginterfaces/base/ustring.h"
#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/vstparameters.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

using namespace Steinberg;
using Catch::Approx;

namespace {

// -----------------------------------------------------------------------------
// The eight shipped IDs (plugin_ids.h:57-69).
// -----------------------------------------------------------------------------
constexpr Vst::ParamID kMacroIds[5] = {
    Seraphis::kMacroDreamId,   Seraphis::kMacroBloomId, Seraphis::kMacroDissolveId,
    Seraphis::kMacroGravityId, Seraphis::kMacroEntropyId,
};

/// The normalized sweep every ID is driven through.
constexpr double kSweep[5] = {0.0, 0.25, 0.5, 0.75, 1.0};

/// The EXACT polyphony each sweep point must denormalize to. Written out as
/// literals rather than recomputed from the expression under test - a table that
/// is only ever the formula re-evaluated asserts nothing about the formula.
constexpr int32 kSweepPolyphony[5] = {1, 5, 9, 12, 16};

/// The spec's denormalization (spec "Parameters shipped in Phase 8", FR-043),
/// kept alongside the literal table purely as a consistency self-check.
[[nodiscard]] int32 expectedPolyphony(double normalized) {
    return static_cast<int32>(
        std::clamp(static_cast<int>(normalized * 15.0 + 1.0 + 0.5), 1, 16));
}

// -----------------------------------------------------------------------------
// State readback. `readParams` reads only the 36-byte PREFIX of the Phase 9 v2
// stream (plan 5.1) -- the nine scalar fields Phase 8 shipped -- and asserts
// nothing about the stream's total length, so it is unaffected by the v2 fields
// that follow byte 36. Little-endian:
//   0 int32 version | 4 float masterGain | 8 int32 polyphony |
//  12 int32 softLimit | 16 dream | 20 bloom | 24 dissolve | 28 gravity |
//  32 entropy
// -----------------------------------------------------------------------------
struct ParamSnapshot {
    int32 version = 0;
    float masterGain = 0.0f;
    int32 polyphony = 0;
    int32 softLimit = 0;
    float macros[5] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
};

struct StreamReleaser {
    void operator()(MemoryStream* s) const noexcept {
        if (s != nullptr) {
            s->release();
        }
    }
};
using StreamPtr = std::unique_ptr<MemoryStream, StreamReleaser>;

[[nodiscard]] ParamSnapshot readParams(Seraphis::Processor& proc) {
    StreamPtr stream(new MemoryStream());
    REQUIRE(proc.getState(stream.get()) == kResultOk);
    stream->seek(0, IBStream::kIBSeekSet, nullptr);

    ParamSnapshot s{};
    IBStreamer r(stream.get(), kLittleEndian);
    REQUIRE(r.readInt32(s.version));
    REQUIRE(r.readFloat(s.masterGain));
    REQUIRE(r.readInt32(s.polyphony));
    REQUIRE(r.readInt32(s.softLimit));
    for (float& macro : s.macros) {
        REQUIRE(r.readFloat(macro));
    }
    return s;
}

/// Deliver the queued parameter changes and read the resulting atomics back.
/// numSamples == 0, so process() latches the automation and then early-outs
/// without touching the (unprepared) chain.
[[nodiscard]] ParamSnapshot latchParams(SeraphisTest::ProcessorFixture& fixture) {
    REQUIRE(fixture.processBlock(0) == kResultOk);
    return readParams(*fixture.proc);
}

/// The ASCII form of a String128, for the display assertions.
[[nodiscard]] std::string toStdString(Vst::String128 text) {
    char ascii[128] = {};
    UString(text, 128).toAscii(ascii, 128);
    return {static_cast<const char*>(ascii)};
}

}  // namespace

TEST_CASE("Seraphis_ParamDenormRoundTrip", "[seraphis][params]") {

    // -------------------------------------------------------------------------
    // 1. Sweep (FR-043).
    // -------------------------------------------------------------------------
    SECTION("Master gain denormalizes to value * 2.0") {
        SeraphisTest::ProcessorFixture fixture;
        for (const double v : kSweep) {
            INFO("normalized " << v);
            fixture.setParam(Seraphis::kMasterGainId, v);
            const ParamSnapshot s = latchParams(fixture);
            CHECK(s.masterGain == Approx(v * 2.0).margin(1.0e-6));
        }
    }

    SECTION("Polyphony denormalizes to exactly {1, 5, 9, 12, 16}") {
        SeraphisTest::ProcessorFixture fixture;
        for (std::size_t i = 0; i < 5; ++i) {
            INFO("normalized " << kSweep[i]);
            // The literal table and the spec expression must agree; if they ever
            // diverge the table is what SC-009 pins.
            REQUIRE(expectedPolyphony(kSweep[i]) == kSweepPolyphony[i]);

            fixture.setParam(Seraphis::kPolyphonyId, kSweep[i]);
            const ParamSnapshot s = latchParams(fixture);
            CHECK(s.polyphony == kSweepPolyphony[i]);  // EXACT, not approximate
        }
    }

    SECTION("Soft limit denormalizes to value >= 0.5") {
        SeraphisTest::ProcessorFixture fixture;
        for (const double v : kSweep) {
            INFO("normalized " << v);
            fixture.setParam(Seraphis::kSoftLimitId, v);
            const ParamSnapshot s = latchParams(fixture);
            CHECK((s.softLimit != 0) == (v >= 0.5));
        }
    }

    SECTION("Every macro denormalizes to the identity") {
        SeraphisTest::ProcessorFixture fixture;
        for (std::size_t m = 0; m < 5; ++m) {
            for (const double v : kSweep) {
                INFO("macro index " << m << ", normalized " << v);
                fixture.setParam(kMacroIds[m], v);
                const ParamSnapshot s = latchParams(fixture);
                CHECK(s.macros[m] == Approx(v).margin(1.0e-6));
            }
        }
    }

    // -------------------------------------------------------------------------
    // 2. Last-point clause (FR-042) - MANDATORY.
    // -------------------------------------------------------------------------
    // With one-point queues an implementation reading getPoint(0, ...) is
    // byte-for-byte indistinguishable from one reading getPointCount() - 1, and
    // no other criterion covers it (T021's block-size case deliberately carries
    // no automation). The three points below are all distinct and the LAST one
    // is neither the first nor the maximum, so neither getPoint(0) nor a
    // max-scan can pass.
    SECTION("The LAST point of a multi-point queue is the one that lands") {
        SeraphisTest::ProcessorFixture fixture;

        fixture.setParamPoints(Seraphis::kPolyphonyId, {0.0, 1.0, 0.4});
        fixture.setParamPoints(Seraphis::kMacroDreamId, {0.0, 1.0, 0.25});

        const ParamSnapshot s = latchParams(fixture);

        // Discrimination, stated so the intent survives a later edit: the three
        // polyphony points denormalize to 1 (first), 16 (max) and 7 (last).
        REQUIRE(expectedPolyphony(0.0) == 1);
        REQUIRE(expectedPolyphony(1.0) == 16);
        REQUIRE(expectedPolyphony(0.4) == 7);

        CHECK(s.polyphony == 7);
        CHECK(s.macros[0] == Approx(0.25f).margin(1.0e-6));
    }

    // -------------------------------------------------------------------------
    // 3. Degenerate queues (FR-042's null guard and "else ignore").
    // -------------------------------------------------------------------------
    SECTION("A null IParameterChanges is ignored") {
        SeraphisTest::ProcessorFixture fixture;

        Vst::ProcessData& data = fixture.withOutputChannels(2);
        data.numSamples = 0;
        data.inputParameterChanges = nullptr;
        CHECK(fixture.proc->process(data) == kResultOk);

        const ParamSnapshot s = readParams(*fixture.proc);
        CHECK(s.masterGain == Approx(1.0f).margin(1.0e-6));  // registered default
        CHECK(s.polyphony == 8);
    }

    SECTION("An empty queue and an unknown ID leave every atomic untouched") {
        SeraphisTest::ProcessorFixture fixture;

        // Seed something non-default first, so "untouched" is a real claim.
        fixture.setParam(Seraphis::kMasterGainId, 0.25);
        const ParamSnapshot seeded = latchParams(fixture);
        REQUIRE(seeded.masterGain == Approx(0.5f).margin(1.0e-6));

        fixture.params.addQueue(Seraphis::kMasterGainId);  // ZERO points
        fixture.params.addQueue(4242u).addTestPoint(0, 1.0);  // outside both ranges

        const ParamSnapshot after = latchParams(fixture);
        CHECK(after.masterGain == Approx(0.5f).margin(1.0e-6));
        CHECK(after.polyphony == seeded.polyphony);
        CHECK(after.softLimit == seeded.softLimit);
        for (std::size_t m = 0; m < 5; ++m) {
            CHECK(after.macros[m] == Approx(seeded.macros[m]).margin(1.0e-6));
        }
    }

    // -------------------------------------------------------------------------
    // 4. Controller clause (FR-048).
    // -------------------------------------------------------------------------
    // getParamNormalized() after setParamNormalized() returns the value
    // EXACTLY, for every registered parameter INCLUDING kPolyphonyId:
    // Parameter::setNormalized only clamps to [0, 1]
    // (extern/vst3sdk/public.sdk/source/vst/vstparameters.cpp), and
    // StringListParameter overrides toString/fromString/toPlain/toNormalized but
    // NOT setNormalized. Demanding a "nearest step" here would fail a CORRECT
    // implementation, so the quantization is asserted below on the surfaces that
    // really do quantize.
    SECTION("Controller stores every registered parameter verbatim") {
        Seraphis::Controller controller;
        REQUIRE(controller.initialize(nullptr) == kResultOk);

        const Vst::ParamID allIds[8] = {
            Seraphis::kMasterGainId,    Seraphis::kPolyphonyId,
            Seraphis::kSoftLimitId,     Seraphis::kMacroDreamId,
            Seraphis::kMacroBloomId,    Seraphis::kMacroDissolveId,
            Seraphis::kMacroGravityId,  Seraphis::kMacroEntropyId,
        };
        // Deliberately NOT step-aligned for the 16-entry list parameter.
        const double values[6] = {0.0, 0.1, 0.4, 0.5, 0.7, 1.0};

        for (const Vst::ParamID id : allIds) {
            for (const double v : values) {
                INFO("id " << id << ", normalized " << v);
                REQUIRE(controller.setParamNormalized(id, v) == kResultTrue);
                CHECK(controller.getParamNormalized(id) == v);  // EXACT
            }
        }

        // Out-of-range input is clamped, not rejected.
        REQUIRE(controller.setParamNormalized(Seraphis::kMasterGainId, 1.5) == kResultTrue);
        CHECK(controller.getParamNormalized(Seraphis::kMasterGainId) == 1.0);
        REQUIRE(controller.setParamNormalized(Seraphis::kMasterGainId, -0.5) == kResultTrue);
        CHECK(controller.getParamNormalized(Seraphis::kMasterGainId) == 0.0);

        REQUIRE(controller.terminate() == kResultOk);
    }

    // Quantization, asserted where it actually happens.
    //
    // NOTE ON THE SDK'S QUANTIZER, so nobody "fixes" this into a false identity:
    // StringListParameter::toPlain(v) is FromNormalized(v, stepCount) ==
    // min(stepCount, int32(v * (stepCount + 1))) (vstparameters.cpp:318-323,
    // pluginterfaces/base/futils.h:94-97) - a TRUNCATION over 16 buckets, not a
    // round over 15 steps. It coincides with std::round(v * 15) exactly at the
    // 16 canonical step values v = k/15 that toNormalized() produces (and that a
    // host sends for a list parameter), and NOT in general: at v = 0.75 the SDK
    // yields 12 while std::round(0.75 * 15) is 11. The step values are therefore
    // where the identity is asserted, and the off-step SDK behaviour is pinned
    // separately so the divergence stays visible.
    SECTION("Polyphony quantizes on the surfaces that do quantize") {
        Seraphis::Controller controller;
        REQUIRE(controller.initialize(nullptr) == kResultOk);

        Vst::Parameter* param = controller.getParameterObject(Seraphis::kPolyphonyId);
        REQUIRE(param != nullptr);

        // FR-048 freezes the type: kPolyphonyId is a StringListParameter.
        auto* list = dynamic_cast<Vst::StringListParameter*>(param);
        REQUIRE(list != nullptr);
        REQUIRE(param->getInfo().stepCount == 15);  // 16 entries -> 15 steps

        SeraphisTest::ProcessorFixture fixture;

        for (int k = 0; k <= 15; ++k) {
            INFO("step index " << k);
            const double v = static_cast<double>(k) / 15.0;

            CHECK(static_cast<int>(list->toPlain(v)) == k);
            CHECK(static_cast<int>(list->toPlain(v)) ==
                  static_cast<int>(std::round(v * 15.0)));
            CHECK(list->toNormalized(static_cast<Vst::ParamValue>(k)) == Approx(v));

            // The displayed string is the voice count, not the index.
            Vst::String128 text = {};
            REQUIRE(controller.getParamStringByValue(Seraphis::kPolyphonyId, v, text) ==
                    kResultOk);
            CHECK(toStdString(text) == std::to_string(k + 1));

            // ...and the processor's own denormalization agrees with the display
            // at every step value, which is what makes the dropdown honest.
            fixture.setParam(Seraphis::kPolyphonyId, v);
            const ParamSnapshot s = latchParams(fixture);
            CHECK(s.polyphony == k + 1);
        }

        // The off-step SDK behaviour, pinned so the note above stays true.
        CHECK(static_cast<int>(list->toPlain(0.75)) == 12);
        CHECK(static_cast<int>(std::round(0.75 * 15.0)) == 11);

        REQUIRE(controller.terminate() == kResultOk);
    }
}

// =============================================================================
// Phase 10 - the sixteen effects IDs (spec C-6 -> SC-001)
// =============================================================================
// This case is the surface gate for the 1400+ band: the registered COUNT, the
// frozen per-row TYPE and step count, the registered DEFAULTS, the
// normalized<->plain round trip, and the two dropdown label tables asserted
// element by element.
//
// The label clause is not decoration. `kSyncNoteLabels` (dropdown_mappings.h:135)
// is an eight-entry table in a DIFFERENT order serving ID 406, and
// `spectral_delay.h:529-531`'s own doc comment names a mapping the component does
// not produce (plan D-1), so a permuted or aspirational ID-1419 table is the most
// likely error in this band and nothing else in the suite would catch it.
// =============================================================================
namespace {

enum class FxKind : std::uint8_t { R, L, T };  ///< plugin_ids.h's frozen-type legend (:184-240)

/// One checked-in row per registered effects parameter. `minPlain`/`maxPlain` are
/// the C-6 plain range the normalized default is derived from; they are pinned
/// against the pack's own range constants in the self-check below, so a range
/// constant that moves fails here rather than silently re-scaling a user's patch.
struct FxRow {
    Vst::ParamID id;
    FxKind kind;
    double minPlain;
    double maxPlain;
    double defaultNormalized;
    int stepCount;
};

constexpr FxRow kEffectsSurface[16] = {
    {.id = Seraphis::kFxSaturationId,           .kind = FxKind::R,  .minPlain = 0.0,    .maxPlain = 1.0,    .defaultNormalized = 0.15,          .stepCount = 0},
    {.id = Seraphis::kFxDelayMixId,             .kind = FxKind::R,  .minPlain = 0.0,    .maxPlain = 1.0,    .defaultNormalized = 0.0,           .stepCount = 0},
    {.id = Seraphis::kFxDelayTimeId,            .kind = FxKind::R,  .minPlain = 0.0, .maxPlain = 2000.0,    .defaultNormalized = 0.125,         .stepCount = 0},
    {.id = Seraphis::kFxDelaySpreadId,          .kind = FxKind::R,  .minPlain = 0.0, .maxPlain = 2000.0,    .defaultNormalized = 0.0,           .stepCount = 0},
    {.id = Seraphis::kFxDelaySpreadDirectionId, .kind = FxKind::L,  .minPlain = 0.0,    .maxPlain = 2.0,    .defaultNormalized = 0.0,           .stepCount = 2},
    {.id = Seraphis::kFxDelayFeedbackId,        .kind = FxKind::R,  .minPlain = 0.0,    .maxPlain = 0.95,   .defaultNormalized = 0.35 / 0.95,   .stepCount = 0},
    {.id = Seraphis::kFxDelayTiltId,            .kind = FxKind::R, .minPlain = -1.0,    .maxPlain = 1.0,    .defaultNormalized = 0.5,           .stepCount = 0},
    {.id = Seraphis::kFxDelayDiffusionId,       .kind = FxKind::R,  .minPlain = 0.0,    .maxPlain = 1.0,    .defaultNormalized = 0.30,          .stepCount = 0},
    {.id = Seraphis::kFxDelayWidthId,           .kind = FxKind::R,  .minPlain = 0.0,    .maxPlain = 1.0,    .defaultNormalized = 0.50,          .stepCount = 0},
    {.id = Seraphis::kFxDelaySyncId,            .kind = FxKind::T,  .minPlain = 0.0,    .maxPlain = 1.0,    .defaultNormalized = 0.0,           .stepCount = 1},
    {.id = Seraphis::kFxDelaySyncNoteId,        .kind = FxKind::L,  .minPlain = 0.0,    .maxPlain = 9.0,    .defaultNormalized = 7.0 / 9.0,     .stepCount = 9},
    {.id = Seraphis::kFxSpectralFreezeId,       .kind = FxKind::T,  .minPlain = 0.0,    .maxPlain = 1.0,    .defaultNormalized = 0.0,           .stepCount = 1},
    {.id = Seraphis::kFxWidthId,                .kind = FxKind::R,  .minPlain = 0.0,  .maxPlain = 200.0,    .defaultNormalized = 0.5,           .stepCount = 0},
    {.id = Seraphis::kFxWanderDepthId,          .kind = FxKind::R,  .minPlain = 0.0,    .maxPlain = 1.0,    .defaultNormalized = 0.0,           .stepCount = 0},
    {.id = Seraphis::kFxWanderRateId,           .kind = FxKind::R,  .minPlain = 0.0,    .maxPlain = 1.0,    .defaultNormalized = 0.50,          .stepCount = 0},
    {.id = Seraphis::kFxAzimuthDepthId,         .kind = FxKind::R,  .minPlain = 0.0,    .maxPlain = 1.0,    .defaultNormalized = 0.0,           .stepCount = 0},
};

constexpr std::size_t kEffectsRowCount = sizeof(kEffectsSurface) / sizeof(kEffectsSurface[0]);
static_assert(kEffectsRowCount == 16, "spec C-6 registers exactly 16 effects parameters");

/// The label tables, transcribed HERE as literals rather than read back out of
/// the header under test. The arrow is a universal-character-name for the same
/// reason dropdown_mappings.h:238-243 gives: one char16_t code unit on every
/// compiler, independent of the source file's assumed encoding.
const char16_t* const kExpectedSpreadDirLabels[3] = {
    u"Low \u2192 High", u"High \u2192 Low", u"Center \u2192 Out"};

const char16_t* const kExpectedSyncNoteLabels[10] = {
    u"1/64T", u"1/64", u"1/64D", u"1/32T", u"1/32",
    u"1/32D", u"1/16T", u"1/16", u"1/16D", u"1/8T"};

/// Widen a VST3 TChar run to std::u16string. A static_cast per code unit rather
/// than a reinterpret_cast of the pointer, so the helper is correct whether
/// Steinberg::Vst::TChar resolves to char16_t or to a 16-bit integer type.
[[nodiscard]] std::u16string toU16(const Vst::TChar* text) {
    std::u16string out;
    for (std::size_t i = 0; text[i] != 0; ++i) {
        out.push_back(static_cast<char16_t>(text[i]));
    }
    return out;
}

/// The normalized value the pack's own constants say a row's default is. Used
/// ONLY as a consistency self-check against the literal table above - a table
/// that is merely the formula re-evaluated would assert nothing about either.
[[nodiscard]] double fxNormalized(double plain, double lo, double hi) {
    return (plain - lo) / (hi - lo);
}

}  // namespace

TEST_CASE("Seraphis effects parameters denormalize", "[seraphis][params][effects]") {

    // -------------------------------------------------------------------------
    // 0. Self-check: the checked-in table agrees with the shipped constants.
    // -------------------------------------------------------------------------
    SECTION("The checked-in table agrees with effects_params.h's own constants") {
        // Ranges (FR-015: each constant is transcribed once, in the pack).
        CHECK(kEffectsSurface[2].minPlain == Approx(Seraphis::kFxDelayTimeMinMs));
        CHECK(kEffectsSurface[2].maxPlain == Approx(Seraphis::kFxDelayTimeMaxMs));
        CHECK(kEffectsSurface[3].minPlain == Approx(Seraphis::kFxDelaySpreadMinMs));
        CHECK(kEffectsSurface[3].maxPlain == Approx(Seraphis::kFxDelaySpreadMaxMs));
        CHECK(kEffectsSurface[4].maxPlain ==
              Approx(static_cast<double>(Seraphis::kSpreadDirectionCount) - 1.0));
        CHECK(kEffectsSurface[5].maxPlain == Approx(Seraphis::kFxDelayFeedbackMax));
        CHECK(kEffectsSurface[6].minPlain == Approx(Seraphis::kFxDelayTiltMin));
        CHECK(kEffectsSurface[6].maxPlain == Approx(Seraphis::kFxDelayTiltMax));
        CHECK(kEffectsSurface[10].maxPlain ==
              Approx(static_cast<double>(Seraphis::kFxDelaySyncNoteLabels.size()) - 1.0));
        CHECK(kEffectsSurface[12].minPlain == Approx(Seraphis::kFxWidthMinPercent));
        CHECK(kEffectsSurface[12].maxPlain == Approx(Seraphis::kFxWidthMaxPercent));

        // Defaults, recomputed from the pack's constants through each row's range.
        const double expected[kEffectsRowCount] = {
            static_cast<double>(Seraphis::kFxSaturationDefault),
            static_cast<double>(Seraphis::kFxDelayMixDefault),
            fxNormalized(Seraphis::kFxDelayTimeDefault,
                         Seraphis::kFxDelayTimeMinMs, Seraphis::kFxDelayTimeMaxMs),
            fxNormalized(Seraphis::kFxDelaySpreadDefault,
                         Seraphis::kFxDelaySpreadMinMs, Seraphis::kFxDelaySpreadMaxMs),
            static_cast<double>(Seraphis::kFxDelaySpreadDirectionDefault) /
                (static_cast<double>(Seraphis::kSpreadDirectionCount) - 1.0),
            static_cast<double>(Seraphis::kFxDelayFeedbackDefault) /
                static_cast<double>(Seraphis::kFxDelayFeedbackMax),
            fxNormalized(Seraphis::kFxDelayTiltDefault,
                         Seraphis::kFxDelayTiltMin, Seraphis::kFxDelayTiltMax),
            static_cast<double>(Seraphis::kFxDelayDiffusionDefault),
            static_cast<double>(Seraphis::kFxDelayWidthDefault),
            Seraphis::kFxDelaySyncDefault ? 1.0 : 0.0,
            static_cast<double>(Seraphis::kFxDelaySyncNoteDefault) /
                (static_cast<double>(Seraphis::kFxDelaySyncNoteLabels.size()) - 1.0),
            Seraphis::kFxSpectralFreezeDefault ? 1.0 : 0.0,
            fxNormalized(Seraphis::kFxWidthDefault,
                         Seraphis::kFxWidthMinPercent, Seraphis::kFxWidthMaxPercent),
            static_cast<double>(Seraphis::kFxWanderDepthDefault),
            static_cast<double>(Seraphis::kFxWanderRateDefault),
            static_cast<double>(Seraphis::kFxAzimuthDepthDefault),
        };
        for (std::size_t i = 0; i < kEffectsRowCount; ++i) {
            INFO("row " << i << ", id " << kEffectsSurface[i].id);
            CHECK(kEffectsSurface[i].defaultNormalized ==
                  Approx(expected[i]).margin(1.0e-6));
        }
    }

    // -------------------------------------------------------------------------
    // 1-2. The surface count, and every row's frozen type / step count / default.
    // -------------------------------------------------------------------------
    SECTION("The registered surface is 107 rows and every effects row matches C-6") {
        Seraphis::Controller controller;
        REQUIRE(controller.initialize(nullptr) == kResultOk);

        REQUIRE(controller.getParameterCount() == 107);

        for (const FxRow& row : kEffectsSurface) {
            INFO("id " << row.id);
            Vst::Parameter* param = controller.getParameterObject(row.id);
            REQUIRE(param != nullptr);

            const Vst::ParameterInfo& info = param->getInfo();
            CHECK(info.id == row.id);
            CHECK(info.stepCount == row.stepCount);
            CHECK(info.defaultNormalizedValue ==
                  Approx(row.defaultNormalized).margin(1.0e-6));
            CHECK((info.flags & Vst::ParameterInfo::kCanAutomate) != 0);

            // FR-048 / plugin_ids.h:184-240 freeze the TYPE per row: only the two
            // `L` rows are StringListParameters. Swapping a registered VST3
            // parameter's type at the same ID breaks editor load in hosts that
            // cache parameter metadata, so it is pinned rather than inferred.
            auto* list = dynamic_cast<Vst::StringListParameter*>(param);
            if (row.kind == FxKind::L) {
                CHECK(list != nullptr);
            } else {
                CHECK(list == nullptr);
            }
        }

        REQUIRE(controller.terminate() == kResultOk);
    }

    // -------------------------------------------------------------------------
    // 3. normalized -> plain -> normalized is the identity.
    // -------------------------------------------------------------------------
    // The `R` and `T` rows are plain Vst::Parameters, whose toPlain/toNormalized
    // ARE the identity (vstparameters.cpp:112-121), so the five-point sweep must
    // return each point exactly.
    //
    // The two `L` rows are StringListParameters and DO quantize: toPlain is
    // FromNormalized(v, stepCount) == min(stepCount, int32(v * (stepCount + 1)))
    // (vstparameters.cpp:318-323), a TRUNCATION over stepCount + 1 buckets. The
    // round trip is therefore an identity exactly at the canonical step values
    // v = k / stepCount that toNormalized() produces and that a host sends for a
    // list parameter - and NOT at an arbitrary 0.25, where it would fail a
    // CORRECT implementation. This is the same distinction the polyphony section
    // above documents at length; asserting the sweep on a dropdown would pin a
    // false claim.
    SECTION("Every effects row round-trips normalized -> plain -> normalized") {
        Seraphis::Controller controller;
        REQUIRE(controller.initialize(nullptr) == kResultOk);

        for (const FxRow& row : kEffectsSurface) {
            Vst::Parameter* param = controller.getParameterObject(row.id);
            REQUIRE(param != nullptr);

            if (row.kind == FxKind::L) {
                for (int k = 0; k <= row.stepCount; ++k) {
                    INFO("id " << row.id << ", step index " << k);
                    const double n =
                        static_cast<double>(k) / static_cast<double>(row.stepCount);
                    CHECK(static_cast<int>(param->toPlain(n)) == k);
                    CHECK(param->toNormalized(param->toPlain(n)) ==
                          Approx(n).margin(1.0e-6));
                }
            } else {
                for (const double n : kSweep) {
                    INFO("id " << row.id << ", normalized " << n);
                    CHECK(param->toNormalized(param->toPlain(n)) ==
                          Approx(n).margin(1.0e-6));
                }
            }
        }

        REQUIRE(controller.terminate() == kResultOk);
    }

    // -------------------------------------------------------------------------
    // 4. Every dropdown index displays a distinct, non-empty string.
    // -------------------------------------------------------------------------
    SECTION("Every dropdown index of 1413 and 1419 displays a distinct string") {
        Seraphis::Controller controller;
        REQUIRE(controller.initialize(nullptr) == kResultOk);

        const Vst::ParamID dropdownIds[2] = {Seraphis::kFxDelaySpreadDirectionId,
                                             Seraphis::kFxDelaySyncNoteId};
        const int entryCounts[2] = {3, 10};

        for (std::size_t d = 0; d < 2u; ++d) {
            const int steps = entryCounts[d] - 1;
            std::vector<std::u16string> seen;
            for (int k = 0; k < entryCounts[d]; ++k) {
                INFO("id " << dropdownIds[d] << ", index " << k);
                const double n = static_cast<double>(k) / static_cast<double>(steps);

                Vst::String128 text = {};
                REQUIRE(controller.getParamStringByValue(dropdownIds[d], n, text) ==
                        kResultOk);

                const std::u16string shown = toU16(text);
                CHECK(!shown.empty());
                CHECK(std::find(seen.begin(), seen.end(), shown) == seen.end());

                // ...and it is the label the registered table holds at that index,
                // so the displayed string cannot drift from the registered list.
                const char16_t* const expected =
                    (d == 0) ? kExpectedSpreadDirLabels[k] : kExpectedSyncNoteLabels[k];
                CHECK(shown == std::u16string(expected));

                seen.push_back(shown);
            }
        }

        REQUIRE(controller.terminate() == kResultOk);
    }

    // -------------------------------------------------------------------------
    // 5. The two label tables, element by element, IN ORDER.
    // -------------------------------------------------------------------------
    SECTION("The effects label tables are the shipped strings, in order") {
        REQUIRE(Seraphis::kFxSpreadDirectionLabels.size() == 3u);
        for (std::size_t i = 0; i < 3u; ++i) {
            INFO("kFxSpreadDirectionLabels index " << i);
            CHECK(toU16(Seraphis::kFxSpreadDirectionLabels[i]) ==
                  std::u16string(kExpectedSpreadDirLabels[i]));
        }

        REQUIRE(Seraphis::kFxDelaySyncNoteLabels.size() == 10u);
        for (std::size_t i = 0; i < 10u; ++i) {
            INFO("kFxDelaySyncNoteLabels index " << i);
            CHECK(toU16(Seraphis::kFxDelaySyncNoteLabels[i]) ==
                  std::u16string(kExpectedSyncNoteLabels[i]));
        }

        // ID 1419's registered default index names "1/16" - the plan D-1 ruling,
        // pinned here as well as in dropdown_mappings.h's compile-time gate, so a
        // permuted table cannot quietly move which period the default selects.
        REQUIRE(Seraphis::kFxDelaySyncNoteDefaultIndex == 7);
        CHECK(toU16(Seraphis::kFxDelaySyncNoteLabels[static_cast<std::size_t>(
                  Seraphis::kFxDelaySyncNoteDefaultIndex)]) == std::u16string(u"1/16"));
    }
}
