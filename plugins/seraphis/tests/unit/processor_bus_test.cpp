// ==============================================================================
// Seraphis - Processor bus setup tests (T016, FR-020 / FR-021 -> SC-011)
// ==============================================================================
// Seraphis is an instrument: 1 event input, 1 stereo audio output, and NO audio
// input bus at all. The three rejections in setBusArrangements are each asserted
// here because each one guards a different failure:
//   (a) numIns  != 0 -- no audio input bus exists to arrange;
//   (b) numOuts != 1 -- exactly one output bus exists; accepting numOuts == 2
//       lets a host successfully negotiate a bus that does not exist;
//   (c) non-stereo   -- the render path reads channelBuffers32[0] and [1]
//       unconditionally, so a mono arrangement is an out-of-bounds audio-thread
//       read (model: plugins/membrum/src/processor/processor.cpp:1044-1069).
// ==============================================================================

#include "processor/processor.h"

#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/vstspeaker.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <memory>

using namespace Steinberg;
using namespace Steinberg::Vst;

TEST_CASE("Seraphis_ProcessorBusSetup", "[seraphis][processor][bus]") {
    auto proc = std::make_unique<Seraphis::Processor>();
    REQUIRE(proc->initialize(nullptr) == kResultOk);

    SECTION("bus counts are exactly instrument-shaped") {
        REQUIRE(proc->getBusCount(kAudio, kInput) == 0);
        REQUIRE(proc->getBusCount(kAudio, kOutput) == 1);
        REQUIRE(proc->getBusCount(kEvent, kInput) == 1);
    }

    SECTION("an audio input arrangement is rejected") {
        SpeakerArrangement in = SpeakerArr::kStereo;
        SpeakerArrangement out = SpeakerArr::kStereo;
        REQUIRE(proc->setBusArrangements(&in, 1, &out, 1) == kResultFalse);

        // Any input arrangement, not just stereo.
        SpeakerArrangement monoIn = SpeakerArr::kMono;
        REQUIRE(proc->setBusArrangements(&monoIn, 1, &out, 1) == kResultFalse);
    }

    SECTION("zero output buses is rejected") {
        REQUIRE(proc->setBusArrangements(nullptr, 0, nullptr, 0) == kResultFalse);
    }

    SECTION("two output buses is rejected (only one exists)") {
        std::array<SpeakerArrangement, 2> outs{SpeakerArr::kStereo, SpeakerArr::kStereo};
        REQUIRE(proc->setBusArrangements(nullptr, 0, outs.data(),
                                         static_cast<int32>(outs.size())) == kResultFalse);
    }

    SECTION("a mono output arrangement is rejected") {
        SpeakerArrangement mono = SpeakerArr::kMono;
        REQUIRE(proc->setBusArrangements(nullptr, 0, &mono, 1) == kResultFalse);
    }

    SECTION("one stereo output bus with no inputs is accepted") {
        SpeakerArrangement stereo = SpeakerArr::kStereo;
        REQUIRE(proc->setBusArrangements(nullptr, 0, &stereo, 1) == kResultTrue);
    }

    REQUIRE(proc->terminate() == kResultOk);
}
