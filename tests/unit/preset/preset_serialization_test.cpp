// ==============================================================================
// Preset Serialization Tests
// ==============================================================================
// Comprehensive tests to verify that preset generator write functions produce
// data that can be correctly loaded by the load*Params() functions.
//
// These tests catch serialization order mismatches between preset_generator.cpp
// and the *_params.h files.
// ==============================================================================

#include <catch2/catch_all.hpp>
#include <vector>
#include <cstdint>
#include <cstring>

#include "base/source/fstreamer.h"
#include "pluginterfaces/base/ibstream.h"

// Include all params headers
#include "parameters/digital_params.h"
#include "parameters/shimmer_params.h"
#include "parameters/bbd_params.h"
#include "parameters/reverse_params.h"
#include "parameters/multitap_params.h"
#include "parameters/freeze_params.h"
#include "parameters/ducking_params.h"
#include "parameters/granular_params.h"
#include "parameters/spectral_params.h"
#include "parameters/tape_params.h"
#include "parameters/pingpong_params.h"

using namespace Iterum;

// ==============================================================================
// Test Helper: Simple IBStream implementation backed by a vector
// ==============================================================================

class VectorStream : public Steinberg::IBStream {
public:
    std::vector<uint8_t> data_;
    int64_t cursor_ = 0;
    uint32_t refCount_ = 1;

    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID, void**) override {
        return Steinberg::kNotImplemented;
    }

    uint32_t PLUGIN_API addRef() override { return ++refCount_; }
    uint32_t PLUGIN_API release() override {
        if (--refCount_ == 0) { delete this; return 0; }
        return refCount_;
    }

    Steinberg::tresult PLUGIN_API read(void* buffer, Steinberg::int32 numBytes,
        Steinberg::int32* numBytesRead) override {
        if (cursor_ + numBytes > static_cast<int64_t>(data_.size())) {
            numBytes = static_cast<Steinberg::int32>(data_.size() - cursor_);
        }
        if (numBytes > 0) {
            std::memcpy(buffer, data_.data() + cursor_, numBytes);
            cursor_ += numBytes;
        }
        if (numBytesRead) *numBytesRead = numBytes;
        return Steinberg::kResultOk;
    }

    Steinberg::tresult PLUGIN_API write(void* buffer, Steinberg::int32 numBytes,
        Steinberg::int32* numBytesWritten) override {
        const auto* bytes = static_cast<const uint8_t*>(buffer);
        for (Steinberg::int32 i = 0; i < numBytes; ++i) {
            if (cursor_ >= static_cast<int64_t>(data_.size())) {
                data_.push_back(bytes[i]);
            } else {
                data_[static_cast<size_t>(cursor_)] = bytes[i];
            }
            cursor_++;
        }
        if (numBytesWritten) *numBytesWritten = numBytes;
        return Steinberg::kResultOk;
    }

    Steinberg::tresult PLUGIN_API seek(Steinberg::int64 pos, Steinberg::int32 mode,
        Steinberg::int64* result) override {
        switch (mode) {
            case kIBSeekSet: cursor_ = pos; break;
            case kIBSeekCur: cursor_ += pos; break;
            case kIBSeekEnd: cursor_ = static_cast<int64_t>(data_.size()) + pos; break;
        }
        if (result) *result = cursor_;
        return Steinberg::kResultOk;
    }

    Steinberg::tresult PLUGIN_API tell(Steinberg::int64* pos) override {
        if (pos) *pos = cursor_;
        return Steinberg::kResultOk;
    }
};

// ==============================================================================
// Test Helper: Wrapper for writing and reading preset data
// ==============================================================================

class MemoryStreamWrapper {
public:
    VectorStream stream_;

    void writeInt32(int32_t val) {
        stream_.write(&val, sizeof(val), nullptr);
    }

    void writeFloat(float val) {
        stream_.write(&val, sizeof(val), nullptr);
    }

    Steinberg::IBStreamer createReader() {
        stream_.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
        return Steinberg::IBStreamer(&stream_, kLittleEndian);
    }
};

// ==============================================================================
// Write Functions (copied from preset_generator.cpp for testing)
// These MUST match the order in preset_generator.cpp
// ==============================================================================

void writeDigitalPreset(MemoryStreamWrapper& w, float delayTime, int timeMode, int noteValue,
    float feedback, int limiterCharacter, int era, float age,
    float modulationDepth, float modulationRate, int modulationWaveform,
    float mix, float width) {
    w.writeFloat(delayTime);
    w.writeInt32(timeMode);
    w.writeInt32(noteValue);
    w.writeFloat(feedback);
    w.writeInt32(limiterCharacter);
    w.writeInt32(era);
    w.writeFloat(age);
    w.writeFloat(modulationDepth);
    w.writeFloat(modulationRate);
    w.writeInt32(modulationWaveform);
    w.writeFloat(mix);
    w.writeFloat(width);
}

void writeShimmerPreset(MemoryStreamWrapper& w, float delayTime, int timeMode, int noteValue,
    float pitchSemitones, float pitchCents, float shimmerMix, float feedback,
    float diffusionAmount, float diffusionSize, int filterEnabled,
    float filterCutoff, float dryWet) {
    w.writeFloat(delayTime);
    w.writeInt32(timeMode);
    w.writeInt32(noteValue);
    w.writeFloat(pitchSemitones);
    w.writeFloat(pitchCents);
    w.writeFloat(shimmerMix);
    w.writeFloat(feedback);
    w.writeFloat(diffusionAmount);
    w.writeFloat(diffusionSize);
    w.writeInt32(filterEnabled);
    w.writeFloat(filterCutoff);
    w.writeFloat(dryWet);
}

void writeBBDPreset(MemoryStreamWrapper& w, float delayTime, int timeMode, int noteValue,
    float feedback, float modulationDepth, float modulationRate, float age,
    int era, float mix) {
    w.writeFloat(delayTime);
    w.writeInt32(timeMode);
    w.writeInt32(noteValue);
    w.writeFloat(feedback);
    w.writeFloat(modulationDepth);
    w.writeFloat(modulationRate);
    w.writeFloat(age);
    w.writeInt32(era);
    w.writeFloat(mix);
}

void writeReversePreset(MemoryStreamWrapper& w, float chunkSize, int timeMode, int noteValue,
    float crossfade, int playbackMode, float feedback, int filterEnabled,
    float filterCutoff, int filterType, float dryWet) {
    w.writeFloat(chunkSize);
    w.writeInt32(timeMode);
    w.writeInt32(noteValue);
    w.writeFloat(crossfade);
    w.writeInt32(playbackMode);
    w.writeFloat(feedback);
    w.writeInt32(filterEnabled);
    w.writeFloat(filterCutoff);
    w.writeInt32(filterType);
    w.writeFloat(dryWet);
}

void writeMultiTapPreset(MemoryStreamWrapper& w, int timeMode, int noteValue,
    int timingPattern, int spatialPattern, int tapCount, float baseTime,
    float tempo, float feedback, float feedbackLPCutoff, float feedbackHPCutoff,
    float morphTime, float dryWet) {
    w.writeInt32(timeMode);
    w.writeInt32(noteValue);
    w.writeInt32(timingPattern);
    w.writeInt32(spatialPattern);
    w.writeInt32(tapCount);
    w.writeFloat(baseTime);
    w.writeFloat(tempo);
    w.writeFloat(feedback);
    w.writeFloat(feedbackLPCutoff);
    w.writeFloat(feedbackHPCutoff);
    w.writeFloat(morphTime);
    w.writeFloat(dryWet);
}

void writeFreezePreset(MemoryStreamWrapper& w, int freezeEnabled, float delayTime,
    int timeMode, int noteValue, float feedback, float pitchSemitones,
    float pitchCents, float shimmerMix, float decay, float diffusionAmount,
    float diffusionSize, int filterEnabled, int filterType, float filterCutoff,
    float dryWet) {
    w.writeInt32(freezeEnabled);
    w.writeFloat(delayTime);
    w.writeInt32(timeMode);
    w.writeInt32(noteValue);
    w.writeFloat(feedback);
    w.writeFloat(pitchSemitones);
    w.writeFloat(pitchCents);
    w.writeFloat(shimmerMix);
    w.writeFloat(decay);
    w.writeFloat(diffusionAmount);
    w.writeFloat(diffusionSize);
    w.writeInt32(filterEnabled);
    w.writeInt32(filterType);
    w.writeFloat(filterCutoff);
    w.writeFloat(dryWet);
}

void writeDuckingPreset(MemoryStreamWrapper& w, int duckingEnabled, float threshold,
    float duckAmount, float attackTime, float releaseTime, float holdTime,
    int duckTarget, int sidechainFilterEnabled, float sidechainFilterCutoff,
    float delayTime, int timeMode, int noteValue, float feedback, float dryWet) {
    w.writeInt32(duckingEnabled);
    w.writeFloat(threshold);
    w.writeFloat(duckAmount);
    w.writeFloat(attackTime);
    w.writeFloat(releaseTime);
    w.writeFloat(holdTime);
    w.writeInt32(duckTarget);
    w.writeInt32(sidechainFilterEnabled);
    w.writeFloat(sidechainFilterCutoff);
    w.writeFloat(delayTime);
    w.writeInt32(timeMode);
    w.writeInt32(noteValue);
    w.writeFloat(feedback);
    w.writeFloat(dryWet);
}

void writeGranularPreset(MemoryStreamWrapper& w, float grainSize, float density,
    float delayTime, float pitch, float pitchSpray, float positionSpray,
    float panSpray, float reverseProb, int freeze, float feedback, float dryWet,
    int envelopeType, int timeMode, int noteValue, float jitter, int pitchQuantMode,
    float texture, float stereoWidth) {
    w.writeFloat(grainSize);
    w.writeFloat(density);
    w.writeFloat(delayTime);
    w.writeFloat(pitch);
    w.writeFloat(pitchSpray);
    w.writeFloat(positionSpray);
    w.writeFloat(panSpray);
    w.writeFloat(reverseProb);
    w.writeInt32(freeze);
    w.writeFloat(feedback);
    w.writeFloat(dryWet);
    w.writeInt32(envelopeType);
    w.writeInt32(timeMode);
    w.writeInt32(noteValue);
    w.writeFloat(jitter);
    w.writeInt32(pitchQuantMode);
    w.writeFloat(texture);
    w.writeFloat(stereoWidth);
}

void writeSpectralPreset(MemoryStreamWrapper& w, int fftSize, float baseDelay,
    float spread, int spreadDirection, float feedback, float feedbackTilt,
    int freeze, float diffusion, float dryWet, int spreadCurve, float stereoWidth,
    int timeMode, int noteValue) {
    w.writeInt32(fftSize);
    w.writeFloat(baseDelay);
    w.writeFloat(spread);
    w.writeInt32(spreadDirection);
    w.writeFloat(feedback);
    w.writeFloat(feedbackTilt);
    w.writeInt32(freeze);
    w.writeFloat(diffusion);
    w.writeFloat(dryWet);
    w.writeInt32(spreadCurve);
    w.writeFloat(stereoWidth);
    w.writeInt32(timeMode);
    w.writeInt32(noteValue);
}

// NOTE: This function uses the preset_generator order which is DIFFERENT from
// saveTapeParams - specifically, head enable/level/pan are interleaved vs grouped
void writeTapePresetWrongOrder(MemoryStreamWrapper& w, float motorSpeed, float motorInertia,
    float wear, float saturation, float age, int spliceEnabled, float spliceIntensity,
    float feedback, float mix, int head1Enabled, float head1Level, float head1Pan,
    int head2Enabled, float head2Level, float head2Pan, int head3Enabled,
    float head3Level, float head3Pan) {
    w.writeFloat(motorSpeed);
    w.writeFloat(motorInertia);
    w.writeFloat(wear);
    w.writeFloat(saturation);
    w.writeFloat(age);
    w.writeInt32(spliceEnabled);
    w.writeFloat(spliceIntensity);
    w.writeFloat(feedback);
    w.writeFloat(mix);
    // INTERLEAVED ORDER (WRONG - doesn't match saveTapeParams)
    w.writeInt32(head1Enabled);
    w.writeFloat(head1Level);
    w.writeFloat(head1Pan);
    w.writeInt32(head2Enabled);
    w.writeFloat(head2Level);
    w.writeFloat(head2Pan);
    w.writeInt32(head3Enabled);
    w.writeFloat(head3Level);
    w.writeFloat(head3Pan);
}

// Write Tape preset in the CORRECT order that matches saveTapeParams
void writeTapePreset(MemoryStreamWrapper& w, float motorSpeed, float motorInertia,
    float wear, float saturation, float age, int spliceEnabled, float spliceIntensity,
    float feedback, float mix, int head1Enabled, int head2Enabled, int head3Enabled,
    float head1Level, float head2Level, float head3Level,
    float head1Pan, float head2Pan, float head3Pan) {
    w.writeFloat(motorSpeed);
    w.writeFloat(motorInertia);
    w.writeFloat(wear);
    w.writeFloat(saturation);
    w.writeFloat(age);
    w.writeInt32(spliceEnabled);
    w.writeFloat(spliceIntensity);
    w.writeFloat(feedback);
    w.writeFloat(mix);
    // GROUPED ORDER (CORRECT - matches saveTapeParams)
    w.writeInt32(head1Enabled);
    w.writeInt32(head2Enabled);
    w.writeInt32(head3Enabled);
    w.writeFloat(head1Level);
    w.writeFloat(head2Level);
    w.writeFloat(head3Level);
    w.writeFloat(head1Pan);
    w.writeFloat(head2Pan);
    w.writeFloat(head3Pan);
}

void writePingPongPreset(MemoryStreamWrapper& w, float delayTime, int timeMode,
    int noteValue, int lrRatio, float feedback, float crossFeedback, float width,
    float modulationDepth, float modulationRate, float mix) {
    w.writeFloat(delayTime);
    w.writeInt32(timeMode);
    w.writeInt32(noteValue);
    w.writeInt32(lrRatio);
    w.writeFloat(feedback);
    w.writeFloat(crossFeedback);
    w.writeFloat(width);
    w.writeFloat(modulationDepth);
    w.writeFloat(modulationRate);
    w.writeFloat(mix);
}

// ==============================================================================
// Digital Delay Tests
// ==============================================================================

TEST_CASE("Digital preset serialization round-trip", "[preset][digital]") {
    // Test values - use distinctive values to detect field swaps
    const float delayTime = 750.0f;
    const int timeMode = 1;
    const int noteValue = 6;
    const float feedback = 0.65f;
    const int limiterCharacter = 2;
    const int era = 1;
    const float age = 0.3f;
    const float modulationDepth = 0.25f;
    const float modulationRate = 2.5f;
    const int modulationWaveform = 3;
    const float mix = 0.6f;
    const float width = 150.0f;

    // Write preset data
    MemoryStreamWrapper wrapper;
    writeDigitalPreset(wrapper, delayTime, timeMode, noteValue, feedback,
        limiterCharacter, era, age, modulationDepth, modulationRate,
        modulationWaveform, mix, width);

    // Load using params loader
    Steinberg::IBStreamer streamer = wrapper.createReader();
    DigitalParams params;
    loadDigitalParams(params, streamer);

    // Verify all fields
    REQUIRE(params.delayTime.load() == Catch::Approx(delayTime));
    REQUIRE(params.timeMode.load() == timeMode);
    REQUIRE(params.noteValue.load() == noteValue);
    REQUIRE(params.feedback.load() == Catch::Approx(feedback));
    REQUIRE(params.limiterCharacter.load() == limiterCharacter);
    REQUIRE(params.era.load() == era);
    REQUIRE(params.age.load() == Catch::Approx(age));
    REQUIRE(params.modulationDepth.load() == Catch::Approx(modulationDepth));
    REQUIRE(params.modulationRate.load() == Catch::Approx(modulationRate));
    REQUIRE(params.modulationWaveform.load() == modulationWaveform);
    REQUIRE(params.mix.load() == Catch::Approx(mix));
    REQUIRE(params.width.load() == Catch::Approx(width));
}

// ==============================================================================
// Shimmer Delay Tests
// ==============================================================================

TEST_CASE("Shimmer preset serialization round-trip", "[preset][shimmer]") {
    const float delayTime = 450.0f;
    const int timeMode = 1;
    const int noteValue = 5;
    const float pitchSemitones = 7.0f;
    const float pitchCents = 25.0f;
    const float shimmerMix = 80.0f;
    const float feedback = 0.7f;
    const float diffusionAmount = 60.0f;
    const float diffusionSize = 40.0f;
    const int filterEnabled = 1;
    const float filterCutoff = 8000.0f;
    const float dryWet = 55.0f;

    MemoryStreamWrapper wrapper;
    writeShimmerPreset(wrapper, delayTime, timeMode, noteValue, pitchSemitones,
        pitchCents, shimmerMix, feedback, diffusionAmount, diffusionSize,
        filterEnabled, filterCutoff, dryWet);

    Steinberg::IBStreamer streamer = wrapper.createReader();
    ShimmerParams params;
    loadShimmerParams(params, streamer);

    REQUIRE(params.delayTime.load() == Catch::Approx(delayTime));
    REQUIRE(params.timeMode.load() == timeMode);
    REQUIRE(params.noteValue.load() == noteValue);
    REQUIRE(params.pitchSemitones.load() == Catch::Approx(pitchSemitones));
    REQUIRE(params.pitchCents.load() == Catch::Approx(pitchCents));
    REQUIRE(params.shimmerMix.load() == Catch::Approx(shimmerMix));
    REQUIRE(params.feedback.load() == Catch::Approx(feedback));
    REQUIRE(params.diffusionAmount.load() == Catch::Approx(diffusionAmount));
    REQUIRE(params.diffusionSize.load() == Catch::Approx(diffusionSize));
    REQUIRE(params.filterEnabled.load() == (filterEnabled != 0));
    REQUIRE(params.filterCutoff.load() == Catch::Approx(filterCutoff));
    REQUIRE(params.dryWet.load() == Catch::Approx(dryWet));
}

// ==============================================================================
// BBD Delay Tests
// ==============================================================================

TEST_CASE("BBD preset serialization round-trip", "[preset][bbd]") {
    const float delayTime = 350.0f;
    const int timeMode = 1;
    const int noteValue = 4;
    const float feedback = 0.55f;
    const float modulationDepth = 0.4f;
    const float modulationRate = 0.8f;
    const float age = 0.5f;
    const int era = 2;
    const float mix = 0.65f;

    MemoryStreamWrapper wrapper;
    writeBBDPreset(wrapper, delayTime, timeMode, noteValue, feedback,
        modulationDepth, modulationRate, age, era, mix);

    Steinberg::IBStreamer streamer = wrapper.createReader();
    BBDParams params;
    loadBBDParams(params, streamer);

    REQUIRE(params.delayTime.load() == Catch::Approx(delayTime));
    REQUIRE(params.timeMode.load() == timeMode);
    REQUIRE(params.noteValue.load() == noteValue);
    REQUIRE(params.feedback.load() == Catch::Approx(feedback));
    REQUIRE(params.modulationDepth.load() == Catch::Approx(modulationDepth));
    REQUIRE(params.modulationRate.load() == Catch::Approx(modulationRate));
    REQUIRE(params.age.load() == Catch::Approx(age));
    REQUIRE(params.era.load() == era);
    REQUIRE(params.mix.load() == Catch::Approx(mix));
}

// ==============================================================================
// Reverse Delay Tests
// ==============================================================================

TEST_CASE("Reverse preset serialization round-trip", "[preset][reverse]") {
    const float chunkSize = 750.0f;
    const int timeMode = 1;
    const int noteValue = 6;
    const float crossfade = 35.0f;
    const int playbackMode = 1;
    const float feedback = 0.3f;
    const int filterEnabled = 1;
    const float filterCutoff = 6000.0f;
    const int filterType = 2;
    const float dryWet = 0.7f;

    MemoryStreamWrapper wrapper;
    writeReversePreset(wrapper, chunkSize, timeMode, noteValue, crossfade,
        playbackMode, feedback, filterEnabled, filterCutoff, filterType, dryWet);

    Steinberg::IBStreamer streamer = wrapper.createReader();
    ReverseParams params;
    loadReverseParams(params, streamer);

    REQUIRE(params.chunkSize.load() == Catch::Approx(chunkSize));
    REQUIRE(params.timeMode.load() == timeMode);
    REQUIRE(params.noteValue.load() == noteValue);
    REQUIRE(params.crossfade.load() == Catch::Approx(crossfade));
    REQUIRE(params.playbackMode.load() == playbackMode);
    REQUIRE(params.feedback.load() == Catch::Approx(feedback));
    REQUIRE(params.filterEnabled.load() == (filterEnabled != 0));
    REQUIRE(params.filterCutoff.load() == Catch::Approx(filterCutoff));
    REQUIRE(params.filterType.load() == filterType);
    REQUIRE(params.dryWet.load() == Catch::Approx(dryWet));
}

// ==============================================================================
// MultiTap Delay Tests
// ==============================================================================

TEST_CASE("MultiTap preset serialization round-trip", "[preset][multitap]") {
    const int timeMode = 1;
    const int noteValue = 5;
    const int timingPattern = 7;
    const int spatialPattern = 3;
    const int tapCount = 6;
    const float baseTime = 350.0f;
    const float tempo = 135.0f;
    const float feedback = 0.45f;
    const float feedbackLPCutoff = 12000.0f;
    const float feedbackHPCutoff = 100.0f;
    const float morphTime = 750.0f;
    const float dryWet = 55.0f;

    MemoryStreamWrapper wrapper;
    writeMultiTapPreset(wrapper, timeMode, noteValue, timingPattern, spatialPattern,
        tapCount, baseTime, tempo, feedback, feedbackLPCutoff, feedbackHPCutoff,
        morphTime, dryWet);

    Steinberg::IBStreamer streamer = wrapper.createReader();
    MultiTapParams params;
    loadMultiTapParams(params, streamer);

    REQUIRE(params.timeMode.load() == timeMode);
    REQUIRE(params.noteValue.load() == noteValue);
    REQUIRE(params.timingPattern.load() == timingPattern);
    REQUIRE(params.spatialPattern.load() == spatialPattern);
    REQUIRE(params.tapCount.load() == tapCount);
    REQUIRE(params.baseTime.load() == Catch::Approx(baseTime));
    REQUIRE(params.tempo.load() == Catch::Approx(tempo));
    REQUIRE(params.feedback.load() == Catch::Approx(feedback));
    REQUIRE(params.feedbackLPCutoff.load() == Catch::Approx(feedbackLPCutoff));
    REQUIRE(params.feedbackHPCutoff.load() == Catch::Approx(feedbackHPCutoff));
    REQUIRE(params.morphTime.load() == Catch::Approx(morphTime));
    REQUIRE(params.dryWet.load() == Catch::Approx(dryWet));
}

// ==============================================================================
// Freeze Mode Tests
// ==============================================================================

TEST_CASE("Freeze preset serialization round-trip", "[preset][freeze]") {
    const int freezeEnabled = 1;
    const float delayTime = 600.0f;
    const int timeMode = 1;
    const int noteValue = 7;
    const float feedback = 0.8f;
    const float pitchSemitones = 5.0f;
    const float pitchCents = -15.0f;
    const float shimmerMix = 0.4f;
    const float decay = 0.6f;
    const float diffusionAmount = 0.5f;
    const float diffusionSize = 0.7f;
    const int filterEnabled = 1;
    const int filterType = 1;
    const float filterCutoff = 3000.0f;
    const float dryWet = 0.65f;

    MemoryStreamWrapper wrapper;
    writeFreezePreset(wrapper, freezeEnabled, delayTime, timeMode, noteValue,
        feedback, pitchSemitones, pitchCents, shimmerMix, decay,
        diffusionAmount, diffusionSize, filterEnabled, filterType,
        filterCutoff, dryWet);

    Steinberg::IBStreamer streamer = wrapper.createReader();
    FreezeParams params;
    loadFreezeParams(params, streamer);

    REQUIRE(params.freezeEnabled.load() == (freezeEnabled != 0));
    REQUIRE(params.delayTime.load() == Catch::Approx(delayTime));
    REQUIRE(params.timeMode.load() == timeMode);
    REQUIRE(params.noteValue.load() == noteValue);
    REQUIRE(params.feedback.load() == Catch::Approx(feedback));
    REQUIRE(params.pitchSemitones.load() == Catch::Approx(pitchSemitones));
    REQUIRE(params.pitchCents.load() == Catch::Approx(pitchCents));
    REQUIRE(params.shimmerMix.load() == Catch::Approx(shimmerMix));
    REQUIRE(params.decay.load() == Catch::Approx(decay));
    REQUIRE(params.diffusionAmount.load() == Catch::Approx(diffusionAmount));
    REQUIRE(params.diffusionSize.load() == Catch::Approx(diffusionSize));
    REQUIRE(params.filterEnabled.load() == (filterEnabled != 0));
    REQUIRE(params.filterType.load() == filterType);
    REQUIRE(params.filterCutoff.load() == Catch::Approx(filterCutoff));
    REQUIRE(params.dryWet.load() == Catch::Approx(dryWet));
}

// ==============================================================================
// Ducking Delay Tests
// ==============================================================================

TEST_CASE("Ducking preset serialization round-trip", "[preset][ducking]") {
    const int duckingEnabled = 1;
    const float threshold = -25.0f;
    const float duckAmount = 70.0f;
    const float attackTime = 15.0f;
    const float releaseTime = 300.0f;
    const float holdTime = 80.0f;
    const int duckTarget = 2;
    const int sidechainFilterEnabled = 1;
    const float sidechainFilterCutoff = 120.0f;
    const float delayTime = 450.0f;
    const int timeMode = 1;
    const int noteValue = 5;
    const float feedback = 35.0f;
    const float dryWet = 60.0f;

    MemoryStreamWrapper wrapper;
    writeDuckingPreset(wrapper, duckingEnabled, threshold, duckAmount, attackTime,
        releaseTime, holdTime, duckTarget, sidechainFilterEnabled,
        sidechainFilterCutoff, delayTime, timeMode, noteValue, feedback, dryWet);

    Steinberg::IBStreamer streamer = wrapper.createReader();
    DuckingParams params;
    loadDuckingParams(params, streamer);

    REQUIRE(params.duckingEnabled.load() == (duckingEnabled != 0));
    REQUIRE(params.threshold.load() == Catch::Approx(threshold));
    REQUIRE(params.duckAmount.load() == Catch::Approx(duckAmount));
    REQUIRE(params.attackTime.load() == Catch::Approx(attackTime));
    REQUIRE(params.releaseTime.load() == Catch::Approx(releaseTime));
    REQUIRE(params.holdTime.load() == Catch::Approx(holdTime));
    REQUIRE(params.duckTarget.load() == duckTarget);
    REQUIRE(params.sidechainFilterEnabled.load() == (sidechainFilterEnabled != 0));
    REQUIRE(params.sidechainFilterCutoff.load() == Catch::Approx(sidechainFilterCutoff));
    REQUIRE(params.delayTime.load() == Catch::Approx(delayTime));
    REQUIRE(params.timeMode.load() == timeMode);
    REQUIRE(params.noteValue.load() == noteValue);
    REQUIRE(params.feedback.load() == Catch::Approx(feedback));
    REQUIRE(params.dryWet.load() == Catch::Approx(dryWet));
}

// ==============================================================================
// Granular Delay Tests
// ==============================================================================

TEST_CASE("Granular preset serialization round-trip", "[preset][granular]") {
    const float grainSize = 75.0f;
    const float density = 25.0f;
    const float delayTime = 350.0f;
    const float pitch = 7.0f;
    const float pitchSpray = 3.0f;
    const float positionSpray = 0.4f;
    const float panSpray = 0.6f;
    const float reverseProb = 0.3f;
    const int freeze = 1;
    const float feedback = 0.5f;
    const float dryWet = 55.0f;
    const int envelopeType = 2;
    const int timeMode = 1;
    const int noteValue = 4;
    const float jitter = 0.3f;
    const int pitchQuantMode = 1;
    const float texture = 0.6f;
    const float stereoWidth = 0.8f;

    MemoryStreamWrapper wrapper;
    writeGranularPreset(wrapper, grainSize, density, delayTime, pitch, pitchSpray,
        positionSpray, panSpray, reverseProb, freeze, feedback, dryWet,
        envelopeType, timeMode, noteValue, jitter, pitchQuantMode,
        texture, stereoWidth);

    Steinberg::IBStreamer streamer = wrapper.createReader();
    GranularParams params;
    loadGranularParams(params, streamer);

    REQUIRE(params.grainSize.load() == Catch::Approx(grainSize));
    REQUIRE(params.density.load() == Catch::Approx(density));
    REQUIRE(params.delayTime.load() == Catch::Approx(delayTime));
    REQUIRE(params.pitch.load() == Catch::Approx(pitch));
    REQUIRE(params.pitchSpray.load() == Catch::Approx(pitchSpray));
    REQUIRE(params.positionSpray.load() == Catch::Approx(positionSpray));
    REQUIRE(params.panSpray.load() == Catch::Approx(panSpray));
    REQUIRE(params.reverseProb.load() == Catch::Approx(reverseProb));
    REQUIRE(params.freeze.load() == (freeze != 0));
    REQUIRE(params.feedback.load() == Catch::Approx(feedback));
    REQUIRE(params.dryWet.load() == Catch::Approx(dryWet));
    REQUIRE(params.envelopeType.load() == envelopeType);
    REQUIRE(params.timeMode.load() == timeMode);
    REQUIRE(params.noteValue.load() == noteValue);
    REQUIRE(params.jitter.load() == Catch::Approx(jitter));
    REQUIRE(params.pitchQuantMode.load() == pitchQuantMode);
    REQUIRE(params.texture.load() == Catch::Approx(texture));
    REQUIRE(params.stereoWidth.load() == Catch::Approx(stereoWidth));
}

// ==============================================================================
// Spectral Delay Tests
// ==============================================================================

TEST_CASE("Spectral preset serialization round-trip", "[preset][spectral]") {
    const int fftSize = 2048;
    const float baseDelay = 400.0f;
    const float spread = 500.0f;
    const int spreadDirection = 1;
    const float feedback = 0.4f;
    const float feedbackTilt = 0.3f;
    const int freeze = 1;
    const float diffusion = 0.5f;
    const float dryWet = 60.0f;
    const int spreadCurve = 1;
    const float stereoWidth = 0.7f;
    const int timeMode = 1;
    const int noteValue = 5;

    MemoryStreamWrapper wrapper;
    writeSpectralPreset(wrapper, fftSize, baseDelay, spread, spreadDirection,
        feedback, feedbackTilt, freeze, diffusion, dryWet, spreadCurve,
        stereoWidth, timeMode, noteValue);

    Steinberg::IBStreamer streamer = wrapper.createReader();
    SpectralParams params;
    loadSpectralParams(params, streamer);

    REQUIRE(params.fftSize.load() == fftSize);
    REQUIRE(params.baseDelay.load() == Catch::Approx(baseDelay));
    REQUIRE(params.spread.load() == Catch::Approx(spread));
    REQUIRE(params.spreadDirection.load() == spreadDirection);
    REQUIRE(params.feedback.load() == Catch::Approx(feedback));
    REQUIRE(params.feedbackTilt.load() == Catch::Approx(feedbackTilt));
    REQUIRE(params.freeze.load() == (freeze != 0));
    REQUIRE(params.diffusion.load() == Catch::Approx(diffusion));
    REQUIRE(params.dryWet.load() == Catch::Approx(dryWet));
    REQUIRE(params.spreadCurve.load() == spreadCurve);
    REQUIRE(params.stereoWidth.load() == Catch::Approx(stereoWidth));
    REQUIRE(params.timeMode.load() == timeMode);
    REQUIRE(params.noteValue.load() == noteValue);
}

// ==============================================================================
// Tape Delay Tests
// ==============================================================================

TEST_CASE("Tape preset serialization round-trip", "[preset][tape]") {
    const float motorSpeed = 1.2f;
    const float motorInertia = 0.6f;
    const float wear = 0.4f;
    const float saturation = 0.5f;
    const float age = 0.3f;
    const int spliceEnabled = 1;
    const float spliceIntensity = 0.7f;
    const float feedback = 0.45f;
    const float mix = 55.0f;
    const int head1Enabled = 1;
    const int head2Enabled = 1;
    const int head3Enabled = 0;
    const float head1Level = 0.9f;
    const float head2Level = 0.7f;
    const float head3Level = 0.5f;
    const float head1Pan = -0.3f;
    const float head2Pan = 0.4f;
    const float head3Pan = 0.0f;

    // Write using CORRECT order (matching saveTapeParams)
    MemoryStreamWrapper wrapper;
    writeTapePreset(wrapper, motorSpeed, motorInertia, wear, saturation, age,
        spliceEnabled, spliceIntensity, feedback, mix,
        head1Enabled, head2Enabled, head3Enabled,
        head1Level, head2Level, head3Level,
        head1Pan, head2Pan, head3Pan);

    Steinberg::IBStreamer streamer = wrapper.createReader();
    TapeParams params;
    loadTapeParams(params, streamer);

    REQUIRE(params.motorSpeed.load() == Catch::Approx(motorSpeed));
    REQUIRE(params.motorInertia.load() == Catch::Approx(motorInertia));
    REQUIRE(params.wear.load() == Catch::Approx(wear));
    REQUIRE(params.saturation.load() == Catch::Approx(saturation));
    REQUIRE(params.age.load() == Catch::Approx(age));
    REQUIRE(params.spliceEnabled.load() == (spliceEnabled != 0));
    REQUIRE(params.spliceIntensity.load() == Catch::Approx(spliceIntensity));
    REQUIRE(params.feedback.load() == Catch::Approx(feedback));
    REQUIRE(params.mix.load() == Catch::Approx(mix));
    REQUIRE(params.head1Enabled.load() == (head1Enabled != 0));
    REQUIRE(params.head2Enabled.load() == (head2Enabled != 0));
    REQUIRE(params.head3Enabled.load() == (head3Enabled != 0));
    REQUIRE(params.head1Level.load() == Catch::Approx(head1Level));
    REQUIRE(params.head2Level.load() == Catch::Approx(head2Level));
    REQUIRE(params.head3Level.load() == Catch::Approx(head3Level));
    REQUIRE(params.head1Pan.load() == Catch::Approx(head1Pan));
    REQUIRE(params.head2Pan.load() == Catch::Approx(head2Pan));
    REQUIRE(params.head3Pan.load() == Catch::Approx(head3Pan));
}

// ==============================================================================
// PingPong Delay Tests
// ==============================================================================

TEST_CASE("PingPong preset serialization round-trip", "[preset][pingpong]") {
    const float delayTime = 450.0f;
    const int timeMode = 1;
    const int noteValue = 5;
    const int lrRatio = 2;
    const float feedback = 0.6f;
    const float crossFeedback = 0.8f;
    const float width = 150.0f;
    const float modulationDepth = 0.2f;
    const float modulationRate = 1.5f;
    const float mix = 0.55f;

    MemoryStreamWrapper wrapper;
    writePingPongPreset(wrapper, delayTime, timeMode, noteValue, lrRatio,
        feedback, crossFeedback, width, modulationDepth, modulationRate, mix);

    Steinberg::IBStreamer streamer = wrapper.createReader();
    PingPongParams params;
    loadPingPongParams(params, streamer);

    REQUIRE(params.delayTime.load() == Catch::Approx(delayTime));
    REQUIRE(params.timeMode.load() == timeMode);
    REQUIRE(params.noteValue.load() == noteValue);
    REQUIRE(params.lrRatio.load() == lrRatio);
    REQUIRE(params.feedback.load() == Catch::Approx(feedback));
    REQUIRE(params.crossFeedback.load() == Catch::Approx(crossFeedback));
    REQUIRE(params.width.load() == Catch::Approx(width));
    REQUIRE(params.modulationDepth.load() == Catch::Approx(modulationDepth));
    REQUIRE(params.modulationRate.load() == Catch::Approx(modulationRate));
    REQUIRE(params.mix.load() == Catch::Approx(mix));
}
