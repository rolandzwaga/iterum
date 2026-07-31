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
#include <memory>
#include <string>

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
// State readback (plan 3.4 layout: 36 bytes, little-endian)
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
    return std::string(ascii);
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
