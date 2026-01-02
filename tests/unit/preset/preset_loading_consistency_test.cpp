// =============================================================================
// Preset Loading Consistency Test
// =============================================================================
// Verifies that preset serialization/deserialization is consistent:
// 1. save*Params() and sync*ParamsToController() use the same field order
// 2. All modes have matching field counts between save and load
//
// This test catches bugs where save and load get out of sync (e.g., missing
// field reads that corrupt all subsequent mode data).
// =============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "base/source/fstreamer.h"
#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/vsteditcontroller.h"

#include "parameters/granular_params.h"
#include "parameters/spectral_params.h"
#include "parameters/ducking_params.h"
#include "parameters/freeze_params.h"
#include "parameters/reverse_params.h"
#include "parameters/shimmer_params.h"
#include "parameters/tape_params.h"
#include "parameters/bbd_params.h"
#include "parameters/digital_params.h"
#include "parameters/pingpong_params.h"
#include "parameters/multitap_params.h"
#include "parameters/note_value_ui.h"  // For kNoteValueDropdownCount

using namespace Steinberg;
using Catch::Approx;

namespace {

// =============================================================================
// Test helper: Mock controller that just stores parameter values
// =============================================================================
class MockController : public Vst::EditControllerEx1 {
public:
    std::map<Vst::ParamID, double> paramValues;

    tresult PLUGIN_API setParamNormalized(Vst::ParamID id, Vst::ParamValue value) override {
        paramValues[id] = value;
        return kResultOk;
    }

    Vst::ParamValue PLUGIN_API getParamNormalized(Vst::ParamID id) override {
        auto it = paramValues.find(id);
        return (it != paramValues.end()) ? it->second : 0.0;
    }
};

} // anonymous namespace

// =============================================================================
// Verify stream position after each mode's read matches write size
// This catches missing or extra field reads
// =============================================================================

TEST_CASE("All modes consume exact bytes written", "[preset][consistency]") {
    // This test verifies that save*Params writes exactly as many bytes
    // as sync*ParamsToController reads - catching missing/extra fields

    SECTION("Digital") {
        Iterum::DigitalParams params;
        MemoryStream stream;
        IBStreamer writer(&stream, kLittleEndian);
        Iterum::saveDigitalParams(params, writer);
        int64 written = 0;
        stream.tell(&written);

        stream.seek(0, IBStream::kIBSeekSet, nullptr);
        IBStreamer reader(&stream, kLittleEndian);
        MockController controller;
        Iterum::syncDigitalParamsToController(reader, controller);
        int64 read = 0;
        stream.tell(&read);

        REQUIRE(read == written);
    }

    SECTION("BBD") {
        Iterum::BBDParams params;
        MemoryStream stream;
        IBStreamer writer(&stream, kLittleEndian);
        Iterum::saveBBDParams(params, writer);
        int64 written = 0;
        stream.tell(&written);

        stream.seek(0, IBStream::kIBSeekSet, nullptr);
        IBStreamer reader(&stream, kLittleEndian);
        MockController controller;
        Iterum::syncBBDParamsToController(reader, controller);
        int64 read = 0;
        stream.tell(&read);

        REQUIRE(read == written);
    }

    SECTION("Shimmer") {
        Iterum::ShimmerParams params;
        MemoryStream stream;
        IBStreamer writer(&stream, kLittleEndian);
        Iterum::saveShimmerParams(params, writer);
        int64 written = 0;
        stream.tell(&written);

        stream.seek(0, IBStream::kIBSeekSet, nullptr);
        IBStreamer reader(&stream, kLittleEndian);
        MockController controller;
        Iterum::syncShimmerParamsToController(reader, controller);
        int64 read = 0;
        stream.tell(&read);

        REQUIRE(read == written);
    }

    SECTION("Reverse") {
        Iterum::ReverseParams params;
        MemoryStream stream;
        IBStreamer writer(&stream, kLittleEndian);
        Iterum::saveReverseParams(params, writer);
        int64 written = 0;
        stream.tell(&written);

        stream.seek(0, IBStream::kIBSeekSet, nullptr);
        IBStreamer reader(&stream, kLittleEndian);
        MockController controller;
        Iterum::syncReverseParamsToController(reader, controller);
        int64 read = 0;
        stream.tell(&read);

        REQUIRE(read == written);
    }

    SECTION("Freeze") {
        Iterum::FreezeParams params;
        MemoryStream stream;
        IBStreamer writer(&stream, kLittleEndian);
        Iterum::saveFreezeParams(params, writer);
        int64 written = 0;
        stream.tell(&written);

        stream.seek(0, IBStream::kIBSeekSet, nullptr);
        IBStreamer reader(&stream, kLittleEndian);
        MockController controller;
        Iterum::syncFreezeParamsToController(reader, controller);
        int64 read = 0;
        stream.tell(&read);

        REQUIRE(read == written);
    }

    SECTION("Ducking") {
        Iterum::DuckingParams params;
        MemoryStream stream;
        IBStreamer writer(&stream, kLittleEndian);
        Iterum::saveDuckingParams(params, writer);
        int64 written = 0;
        stream.tell(&written);

        stream.seek(0, IBStream::kIBSeekSet, nullptr);
        IBStreamer reader(&stream, kLittleEndian);
        MockController controller;
        Iterum::syncDuckingParamsToController(reader, controller);
        int64 read = 0;
        stream.tell(&read);

        REQUIRE(read == written);
    }

    SECTION("MultiTap") {
        Iterum::MultiTapParams params;
        MemoryStream stream;
        IBStreamer writer(&stream, kLittleEndian);
        Iterum::saveMultiTapParams(params, writer);
        int64 written = 0;
        stream.tell(&written);

        stream.seek(0, IBStream::kIBSeekSet, nullptr);
        IBStreamer reader(&stream, kLittleEndian);
        MockController controller;
        Iterum::syncMultiTapParamsToController(reader, controller);
        int64 read = 0;
        stream.tell(&read);

        REQUIRE(read == written);
    }

    SECTION("Granular") {
        Iterum::GranularParams params;
        MemoryStream stream;
        IBStreamer writer(&stream, kLittleEndian);
        Iterum::saveGranularParams(params, writer);
        int64 written = 0;
        stream.tell(&written);

        stream.seek(0, IBStream::kIBSeekSet, nullptr);
        IBStreamer reader(&stream, kLittleEndian);
        MockController controller;
        Iterum::syncGranularParamsToController(reader, controller);
        int64 read = 0;
        stream.tell(&read);

        REQUIRE(read == written);
    }

    SECTION("Spectral") {
        Iterum::SpectralParams params;
        MemoryStream stream;
        IBStreamer writer(&stream, kLittleEndian);
        Iterum::saveSpectralParams(params, writer);
        int64 written = 0;
        stream.tell(&written);

        stream.seek(0, IBStream::kIBSeekSet, nullptr);
        IBStreamer reader(&stream, kLittleEndian);
        MockController controller;
        Iterum::syncSpectralParamsToController(reader, controller);
        int64 read = 0;
        stream.tell(&read);

        REQUIRE(read == written);
    }

    SECTION("Tape") {
        Iterum::TapeParams params;
        MemoryStream stream;
        IBStreamer writer(&stream, kLittleEndian);
        Iterum::saveTapeParams(params, writer);
        int64 written = 0;
        stream.tell(&written);

        stream.seek(0, IBStream::kIBSeekSet, nullptr);
        IBStreamer reader(&stream, kLittleEndian);
        MockController controller;
        Iterum::syncTapeParamsToController(reader, controller);
        int64 read = 0;
        stream.tell(&read);

        REQUIRE(read == written);
    }

    SECTION("PingPong") {
        Iterum::PingPongParams params;
        MemoryStream stream;
        IBStreamer writer(&stream, kLittleEndian);
        Iterum::savePingPongParams(params, writer);
        int64 written = 0;
        stream.tell(&written);

        stream.seek(0, IBStream::kIBSeekSet, nullptr);
        IBStreamer reader(&stream, kLittleEndian);
        MockController controller;
        Iterum::syncPingPongParamsToController(reader, controller);
        int64 read = 0;
        stream.tell(&read);

        REQUIRE(read == written);
    }
}

// =============================================================================
// Roundtrip value tests - verify specific values survive save/load
// =============================================================================

TEST_CASE("Digital params roundtrip preserves values", "[preset][digital][roundtrip]") {
    Iterum::DigitalParams params;
    params.delayTime.store(750.0f);
    params.timeMode.store(1);
    params.noteValue.store(5);
    params.feedback.store(0.6f);
    params.limiterCharacter.store(2);
    params.era.store(1);
    params.age.store(0.3f);
    params.modulationDepth.store(0.25f);
    params.modulationRate.store(2.5f);
    params.modulationWaveform.store(3);
    params.mix.store(0.7f);
    params.width.store(150.0f);

    // Save to stream
    MemoryStream stream;
    IBStreamer writer(&stream, kLittleEndian);
    Iterum::saveDigitalParams(params, writer);

    // Read back via sync function
    stream.seek(0, IBStream::kIBSeekSet, nullptr);
    IBStreamer reader(&stream, kLittleEndian);
    MockController controller;
    Iterum::syncDigitalParamsToController(reader, controller);

    // Verify key values
    // Note value normalization: index / (kNoteValueDropdownCount - 1) = index / 20
    constexpr double noteValDivisor = Iterum::Parameters::kNoteValueDropdownCount - 1;
    REQUIRE(controller.paramValues[Iterum::kDigitalTimeModeId] == Approx(1.0).margin(0.001));
    REQUIRE(controller.paramValues[Iterum::kDigitalNoteValueId] == Approx(5.0 / noteValDivisor).margin(0.001));
    REQUIRE(controller.paramValues[Iterum::kDigitalFeedbackId] == Approx(0.6 / 1.2).margin(0.001));
    REQUIRE(controller.paramValues[Iterum::kDigitalMixId] == Approx(0.7).margin(0.001));
}

TEST_CASE("BBD params roundtrip preserves values", "[preset][bbd][roundtrip]") {
    Iterum::BBDParams params;
    params.delayTime.store(200.0f);
    params.timeMode.store(1);
    params.noteValue.store(4);
    params.feedback.store(0.5f);
    params.modulationDepth.store(0.4f);
    params.modulationRate.store(1.5f);
    params.age.store(0.2f);
    params.era.store(1);
    params.mix.store(0.6f);

    MemoryStream stream;
    IBStreamer writer(&stream, kLittleEndian);
    Iterum::saveBBDParams(params, writer);

    stream.seek(0, IBStream::kIBSeekSet, nullptr);
    IBStreamer reader(&stream, kLittleEndian);
    MockController controller;
    Iterum::syncBBDParamsToController(reader, controller);

    constexpr double noteValDivisor = Iterum::Parameters::kNoteValueDropdownCount - 1;
    REQUIRE(controller.paramValues[Iterum::kBBDTimeModeId] == Approx(1.0).margin(0.001));
    REQUIRE(controller.paramValues[Iterum::kBBDNoteValueId] == Approx(4.0 / noteValDivisor).margin(0.001));
    REQUIRE(controller.paramValues[Iterum::kBBDMixId] == Approx(0.6).margin(0.001));
}

TEST_CASE("Shimmer params roundtrip preserves values", "[preset][shimmer][roundtrip]") {
    Iterum::ShimmerParams params;
    params.delayTime.store(300.0f);
    params.timeMode.store(1);
    params.noteValue.store(6);
    params.pitchSemitones.store(12.0f);
    params.pitchCents.store(5.0f);
    params.shimmerMix.store(0.6f);
    params.feedback.store(0.4f);
    params.diffusionAmount.store(0.5f);
    params.diffusionSize.store(50.0f);
    params.filterEnabled.store(true);
    params.filterCutoff.store(5000.0f);
    params.dryWet.store(0.55f);

    MemoryStream stream;
    IBStreamer writer(&stream, kLittleEndian);
    Iterum::saveShimmerParams(params, writer);

    stream.seek(0, IBStream::kIBSeekSet, nullptr);
    IBStreamer reader(&stream, kLittleEndian);
    MockController controller;
    Iterum::syncShimmerParamsToController(reader, controller);

    constexpr double noteValDivisor = Iterum::Parameters::kNoteValueDropdownCount - 1;
    REQUIRE(controller.paramValues[Iterum::kShimmerTimeModeId] == Approx(1.0).margin(0.001));
    REQUIRE(controller.paramValues[Iterum::kShimmerNoteValueId] == Approx(6.0 / noteValDivisor).margin(0.001));
    REQUIRE(controller.paramValues[Iterum::kShimmerMixId] == Approx(0.55).margin(0.001));
}

TEST_CASE("MultiTap params roundtrip preserves values", "[preset][multitap][roundtrip]") {
    Iterum::MultiTapParams params;
    params.timeMode.store(1);
    params.noteValue.store(4);
    params.timingPattern.store(3);
    params.spatialPattern.store(2);
    params.tapCount.store(6);
    params.baseTime.store(400.0f);
    params.tempo.store(140.0f);
    params.feedback.store(0.5f);
    params.feedbackLPCutoff.store(10000.0f);
    params.feedbackHPCutoff.store(100.0f);
    params.morphTime.store(300.0f);
    params.dryWet.store(0.6f);

    MemoryStream stream;
    IBStreamer writer(&stream, kLittleEndian);
    Iterum::saveMultiTapParams(params, writer);

    stream.seek(0, IBStream::kIBSeekSet, nullptr);
    IBStreamer reader(&stream, kLittleEndian);
    MockController controller;
    Iterum::syncMultiTapParamsToController(reader, controller);

    constexpr double noteValDivisor = Iterum::Parameters::kNoteValueDropdownCount - 1;
    REQUIRE(controller.paramValues[Iterum::kMultiTapTimeModeId] == Approx(1.0).margin(0.001));
    REQUIRE(controller.paramValues[Iterum::kMultiTapNoteValueId] == Approx(4.0 / noteValDivisor).margin(0.001));
    REQUIRE(controller.paramValues[Iterum::kMultiTapMixId] == Approx(0.6).margin(0.001));
}

TEST_CASE("Ducking params roundtrip preserves values", "[preset][ducking][roundtrip]") {
    Iterum::DuckingParams params;
    params.duckingEnabled.store(true);
    params.threshold.store(-20.0f);
    params.duckAmount.store(0.7f);
    params.attackTime.store(15.0f);
    params.releaseTime.store(300.0f);
    params.holdTime.store(100.0f);
    params.duckTarget.store(1);
    params.sidechainFilterEnabled.store(true);
    params.sidechainFilterCutoff.store(150.0f);
    params.delayTime.store(350.0f);
    params.timeMode.store(1);
    params.noteValue.store(4);
    params.feedback.store(50.0f);
    params.dryWet.store(0.75f);

    MemoryStream stream;
    IBStreamer writer(&stream, kLittleEndian);
    Iterum::saveDuckingParams(params, writer);

    stream.seek(0, IBStream::kIBSeekSet, nullptr);
    IBStreamer reader(&stream, kLittleEndian);
    MockController controller;
    Iterum::syncDuckingParamsToController(reader, controller);

    constexpr double noteValDivisor = Iterum::Parameters::kNoteValueDropdownCount - 1;
    REQUIRE(controller.paramValues[Iterum::kDuckingTimeModeId] == Approx(1.0).margin(0.001));
    REQUIRE(controller.paramValues[Iterum::kDuckingNoteValueId] == Approx(4.0 / noteValDivisor).margin(0.001));
    REQUIRE(controller.paramValues[Iterum::kDuckingMixId] == Approx(0.75).margin(0.001));
}

TEST_CASE("Reverse params roundtrip preserves values", "[preset][reverse][roundtrip]") {
    Iterum::ReverseParams params;
    params.chunkSize.store(400.0f);
    params.timeMode.store(1);
    params.noteValue.store(3);
    params.crossfade.store(0.15f);
    params.playbackMode.store(1);
    params.feedback.store(0.3f);
    params.filterEnabled.store(true);
    params.filterCutoff.store(3000.0f);
    params.filterType.store(1);
    params.dryWet.store(0.65f);

    MemoryStream stream;
    IBStreamer writer(&stream, kLittleEndian);
    Iterum::saveReverseParams(params, writer);

    stream.seek(0, IBStream::kIBSeekSet, nullptr);
    IBStreamer reader(&stream, kLittleEndian);
    MockController controller;
    Iterum::syncReverseParamsToController(reader, controller);

    constexpr double noteValDivisor = Iterum::Parameters::kNoteValueDropdownCount - 1;
    REQUIRE(controller.paramValues[Iterum::kReverseTimeModeId] == Approx(1.0).margin(0.001));
    REQUIRE(controller.paramValues[Iterum::kReverseNoteValueId] == Approx(3.0 / noteValDivisor).margin(0.001));
    REQUIRE(controller.paramValues[Iterum::kReverseMixId] == Approx(0.65).margin(0.001));
}

TEST_CASE("Freeze params roundtrip preserves values", "[preset][freeze][roundtrip]") {
    Iterum::FreezeParams params;
    params.freezeEnabled.store(false);
    params.delayTime.store(500.0f);
    params.timeMode.store(1);
    params.noteValue.store(5);
    params.feedback.store(0.95f);
    params.pitchSemitones.store(0.0f);
    params.pitchCents.store(0.0f);
    params.shimmerMix.store(0.0f);
    params.decay.store(0.5f);
    params.diffusionAmount.store(0.3f);
    params.diffusionSize.store(0.5f);
    params.filterEnabled.store(true);
    params.filterType.store(0);
    params.filterCutoff.store(8000.0f);
    params.dryWet.store(0.8f);

    MemoryStream stream;
    IBStreamer writer(&stream, kLittleEndian);
    Iterum::saveFreezeParams(params, writer);

    stream.seek(0, IBStream::kIBSeekSet, nullptr);
    IBStreamer reader(&stream, kLittleEndian);
    MockController controller;
    Iterum::syncFreezeParamsToController(reader, controller);

    constexpr double noteValDivisor = Iterum::Parameters::kNoteValueDropdownCount - 1;
    REQUIRE(controller.paramValues[Iterum::kFreezeTimeModeId] == Approx(1.0).margin(0.001));
    REQUIRE(controller.paramValues[Iterum::kFreezeNoteValueId] == Approx(5.0 / noteValDivisor).margin(0.001));
    REQUIRE(controller.paramValues[Iterum::kFreezeMixId] == Approx(0.8).margin(0.001));
}
