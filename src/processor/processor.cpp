// ==============================================================================
// Audio Processor Implementation
// ==============================================================================

#include "processor.h"
#include "plugin_ids.h"
#include "dsp/core/block_context.h"

#include "base/source/fstreamer.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"

#include <algorithm>
#include <cmath>

namespace Iterum {

// ==============================================================================
// Constructor
// ==============================================================================

Processor::Processor() {
    // Set the controller class ID for host to create the correct controller
    // Constitution Principle I: Processor/Controller separation
    setControllerClass(kControllerUID);
}

// ==============================================================================
// IPluginBase
// ==============================================================================

Steinberg::tresult PLUGIN_API Processor::initialize(FUnknown* context) {
    // Always call parent first
    Steinberg::tresult result = AudioEffect::initialize(context);
    if (result != Steinberg::kResultTrue) {
        return result;
    }

    // Add audio I/O buses
    // Stereo input
    addAudioInput(STR16("Audio Input"), Steinberg::Vst::SpeakerArr::kStereo);
    // Stereo output
    addAudioOutput(STR16("Audio Output"), Steinberg::Vst::SpeakerArr::kStereo);

    return Steinberg::kResultTrue;
}

Steinberg::tresult PLUGIN_API Processor::terminate() {
    // Cleanup any resources allocated in initialize()
    return AudioEffect::terminate();
}

// ==============================================================================
// IAudioProcessor
// ==============================================================================

Steinberg::tresult PLUGIN_API Processor::setupProcessing(
    Steinberg::Vst::ProcessSetup& setup) {

    // Store processing parameters
    sampleRate_ = setup.sampleRate;
    maxBlockSize_ = setup.maxSamplesPerBlock;

    // ==========================================================================
    // Constitution Principle II & VI: Pre-allocate ALL buffers HERE
    // ==========================================================================

    // Prepare GranularDelay (spec 034)
    granularDelay_.prepare(sampleRate_);

    // Prepare SpectralDelay (spec 033)
    spectralDelay_.prepare(sampleRate_, static_cast<size_t>(maxBlockSize_));

    // Prepare DuckingDelay (spec 032)
    duckingDelay_.prepare(sampleRate_, static_cast<size_t>(maxBlockSize_));

    // Prepare ShimmerDelay (spec 029)
    shimmerDelay_.prepare(sampleRate_, static_cast<size_t>(maxBlockSize_), 5000.0f);

    // Prepare FreezeMode (spec 031)
    freezeMode_.prepare(sampleRate_, static_cast<size_t>(maxBlockSize_), 5000.0f);

    // Prepare ReverseDelay (spec 030)
    reverseDelay_.prepare(sampleRate_, static_cast<size_t>(maxBlockSize_), 2000.0f);

    // Prepare TapeDelay (spec 024)
    tapeDelay_.prepare(sampleRate_, static_cast<size_t>(maxBlockSize_), 2000.0f);

    // Prepare BBDDelay (spec 025)
    bbdDelay_.prepare(sampleRate_, static_cast<size_t>(maxBlockSize_), 1000.0f);

    // Prepare DigitalDelay (spec 026)
    digitalDelay_.prepare(sampleRate_, static_cast<size_t>(maxBlockSize_), 10000.0f);

    // Prepare PingPongDelay (spec 027)
    pingPongDelay_.prepare(sampleRate_, static_cast<size_t>(maxBlockSize_), 10000.0f);

    // Prepare MultiTapDelay (spec 028)
    multiTapDelay_.prepare(sampleRate_, static_cast<size_t>(maxBlockSize_), 5000.0f);

    return AudioEffect::setupProcessing(setup);
}

Steinberg::tresult PLUGIN_API Processor::setActive(Steinberg::TBool state) {
    if (state) {
        // Activating: reset any processing state
        granularDelay_.reset();
        spectralDelay_.reset();
        duckingDelay_.reset();
        freezeMode_.reset();
        reverseDelay_.reset();
        shimmerDelay_.reset();
        tapeDelay_.reset();
        bbdDelay_.reset();
        digitalDelay_.reset();
        pingPongDelay_.reset();
        multiTapDelay_.reset();
    }

    return AudioEffect::setActive(state);
}

Steinberg::tresult PLUGIN_API Processor::process(Steinberg::Vst::ProcessData& data) {
    // ==========================================================================
    // Constitution Principle II: REAL-TIME SAFETY CRITICAL
    // - NO memory allocation (new, malloc, vector resize, etc.)
    // - NO locks or mutexes
    // - NO file I/O or system calls
    // - NO exceptions (throw/catch)
    // - This function MUST complete within the buffer duration
    // ==========================================================================

    // Process parameter changes first
    if (data.inputParameterChanges) {
        processParameterChanges(data.inputParameterChanges);
    }

    // Check if we have audio to process
    if (data.numSamples == 0) {
        return Steinberg::kResultTrue;
    }

    // Get current parameter values (atomic reads are lock-free)
    const float currentGain = gain_.load(std::memory_order_relaxed);
    const bool currentBypass = bypass_.load(std::memory_order_relaxed);

    // Handle bypass mode
    if (currentBypass) {
        // Pass through: copy input to output
        if (data.numInputs > 0 && data.numOutputs > 0) {
            for (Steinberg::int32 channel = 0;
                 channel < data.inputs[0].numChannels; ++channel) {

                if (data.inputs[0].channelBuffers32[channel] &&
                    data.outputs[0].channelBuffers32[channel]) {

                    std::copy_n(
                        data.inputs[0].channelBuffers32[channel],
                        static_cast<size_t>(data.numSamples),
                        data.outputs[0].channelBuffers32[channel]
                    );
                }
            }
        }
        return Steinberg::kResultTrue;
    }

    // ==========================================================================
    // Main Audio Processing
    // ==========================================================================

    // Verify we have valid stereo I/O
    if (data.numInputs == 0 || data.numOutputs == 0) {
        return Steinberg::kResultTrue;
    }

    if (data.inputs[0].numChannels < 2 || data.outputs[0].numChannels < 2) {
        return Steinberg::kResultTrue;
    }

    float* inputL = data.inputs[0].channelBuffers32[0];
    float* inputR = data.inputs[0].channelBuffers32[1];
    float* outputL = data.outputs[0].channelBuffers32[0];
    float* outputR = data.outputs[0].channelBuffers32[1];

    if (!inputL || !inputR || !outputL || !outputR) {
        return Steinberg::kResultTrue;
    }

    // ==========================================================================
    // Read Host Transport (tempo, playback state)
    // ==========================================================================

    double tempoBPM = 120.0;  // fallback
    bool isPlaying = false;

    if (data.processContext) {
        if (data.processContext->state & Steinberg::Vst::ProcessContext::kTempoValid) {
            tempoBPM = data.processContext->tempo;
        }
        isPlaying = (data.processContext->state & Steinberg::Vst::ProcessContext::kPlaying) != 0;
    }

    DSP::BlockContext ctx{
        .sampleRate = sampleRate_,
        .blockSize = static_cast<size_t>(data.numSamples),
        .tempoBPM = tempoBPM,
        .isPlaying = isPlaying
    };

    // ==========================================================================
    // Process ONLY the active delay mode
    // ==========================================================================

    const int currentMode = mode_.load(std::memory_order_relaxed);
    const size_t numSamples = static_cast<size_t>(data.numSamples);

    switch (static_cast<DelayMode>(currentMode)) {
        case DelayMode::Granular:
            // Update Granular parameters
            granularDelay_.setGrainSize(granularParams_.grainSize.load(std::memory_order_relaxed));
            granularDelay_.setDensity(granularParams_.density.load(std::memory_order_relaxed));
            granularDelay_.setDelayTime(granularParams_.delayTime.load(std::memory_order_relaxed));
            granularDelay_.setPitch(granularParams_.pitch.load(std::memory_order_relaxed));
            granularDelay_.setPitchSpray(granularParams_.pitchSpray.load(std::memory_order_relaxed));
            granularDelay_.setPositionSpray(granularParams_.positionSpray.load(std::memory_order_relaxed));
            granularDelay_.setPanSpray(granularParams_.panSpray.load(std::memory_order_relaxed));
            granularDelay_.setReverseProbability(granularParams_.reverseProb.load(std::memory_order_relaxed));
            granularDelay_.setFreeze(granularParams_.freeze.load(std::memory_order_relaxed));
            granularDelay_.setFeedback(granularParams_.feedback.load(std::memory_order_relaxed));
            granularDelay_.setDryWet(granularParams_.dryWet.load(std::memory_order_relaxed));
            granularDelay_.setOutputGain(granularParams_.outputGain.load(std::memory_order_relaxed));
            granularDelay_.setEnvelopeType(static_cast<DSP::GrainEnvelopeType>(
                granularParams_.envelopeType.load(std::memory_order_relaxed)));
            // Tempo sync parameters (spec 038)
            granularDelay_.setTimeMode(granularParams_.timeMode.load(std::memory_order_relaxed));
            granularDelay_.setNoteValue(granularParams_.noteValue.load(std::memory_order_relaxed));
            granularDelay_.process(inputL, inputR, outputL, outputR, numSamples, ctx);
            break;

        case DelayMode::Spectral:
            // Update Spectral parameters
            spectralDelay_.setFFTSize(static_cast<size_t>(
                spectralParams_.fftSize.load(std::memory_order_relaxed)));
            spectralDelay_.setBaseDelayMs(spectralParams_.baseDelay.load(std::memory_order_relaxed));
            spectralDelay_.setSpreadMs(spectralParams_.spread.load(std::memory_order_relaxed));
            spectralDelay_.setSpreadDirection(static_cast<DSP::SpreadDirection>(
                spectralParams_.spreadDirection.load(std::memory_order_relaxed)));
            spectralDelay_.setFeedback(spectralParams_.feedback.load(std::memory_order_relaxed));
            spectralDelay_.setFeedbackTilt(spectralParams_.feedbackTilt.load(std::memory_order_relaxed));
            spectralDelay_.setFreezeEnabled(spectralParams_.freeze.load(std::memory_order_relaxed));
            spectralDelay_.setDiffusion(spectralParams_.diffusion.load(std::memory_order_relaxed));
            spectralDelay_.setDryWetMix(spectralParams_.dryWet.load(std::memory_order_relaxed));
            spectralDelay_.setOutputGainDb(spectralParams_.outputGain.load(std::memory_order_relaxed));
            // Copy input to output first (SpectralDelay processes in-place)
            std::copy_n(inputL, numSamples, outputL);
            std::copy_n(inputR, numSamples, outputR);
            spectralDelay_.process(outputL, outputR, numSamples, ctx);
            break;

        case DelayMode::Shimmer:
            // Update Shimmer parameters
            shimmerDelay_.setDelayTimeMs(shimmerParams_.delayTime.load(std::memory_order_relaxed));
            shimmerDelay_.setPitchSemitones(shimmerParams_.pitchSemitones.load(std::memory_order_relaxed));
            shimmerDelay_.setPitchCents(shimmerParams_.pitchCents.load(std::memory_order_relaxed));
            shimmerDelay_.setShimmerMix(shimmerParams_.shimmerMix.load(std::memory_order_relaxed));
            shimmerDelay_.setFeedbackAmount(shimmerParams_.feedback.load(std::memory_order_relaxed));
            shimmerDelay_.setDiffusionAmount(shimmerParams_.diffusionAmount.load(std::memory_order_relaxed));
            shimmerDelay_.setDiffusionSize(shimmerParams_.diffusionSize.load(std::memory_order_relaxed));
            shimmerDelay_.setFilterEnabled(shimmerParams_.filterEnabled.load(std::memory_order_relaxed));
            shimmerDelay_.setFilterCutoff(shimmerParams_.filterCutoff.load(std::memory_order_relaxed));
            shimmerDelay_.setDryWetMix(shimmerParams_.dryWet.load(std::memory_order_relaxed));
            shimmerDelay_.setOutputGainDb(shimmerParams_.outputGain.load(std::memory_order_relaxed));
            // Copy input to output first (ShimmerDelay processes in-place)
            std::copy_n(inputL, numSamples, outputL);
            std::copy_n(inputR, numSamples, outputR);
            shimmerDelay_.process(outputL, outputR, numSamples, ctx);
            break;

        case DelayMode::Tape:
            // Update Tape parameters
            tapeDelay_.setMotorSpeed(tapeParams_.motorSpeed.load(std::memory_order_relaxed));
            tapeDelay_.setMotorInertia(tapeParams_.motorInertia.load(std::memory_order_relaxed));
            tapeDelay_.setWear(tapeParams_.wear.load(std::memory_order_relaxed));
            tapeDelay_.setSaturation(tapeParams_.saturation.load(std::memory_order_relaxed));
            tapeDelay_.setAge(tapeParams_.age.load(std::memory_order_relaxed));
            tapeDelay_.setSpliceEnabled(tapeParams_.spliceEnabled.load(std::memory_order_relaxed));
            tapeDelay_.setSpliceIntensity(tapeParams_.spliceIntensity.load(std::memory_order_relaxed));
            tapeDelay_.setFeedback(tapeParams_.feedback.load(std::memory_order_relaxed));
            tapeDelay_.setMix(tapeParams_.mix.load(std::memory_order_relaxed));
            // Output level is already in dB (no conversion needed)
            tapeDelay_.setOutputLevel(tapeParams_.outputLevel.load(std::memory_order_relaxed));
            tapeDelay_.setHeadEnabled(0, tapeParams_.head1Enabled.load(std::memory_order_relaxed));
            tapeDelay_.setHeadEnabled(1, tapeParams_.head2Enabled.load(std::memory_order_relaxed));
            tapeDelay_.setHeadEnabled(2, tapeParams_.head3Enabled.load(std::memory_order_relaxed));
            {
                float linearGain = tapeParams_.head1Level.load(std::memory_order_relaxed);
                float dB = (linearGain <= 0.0f) ? -96.0f : 20.0f * std::log10(linearGain);
                tapeDelay_.setHeadLevel(0, dB);
            }
            {
                float linearGain = tapeParams_.head2Level.load(std::memory_order_relaxed);
                float dB = (linearGain <= 0.0f) ? -96.0f : 20.0f * std::log10(linearGain);
                tapeDelay_.setHeadLevel(1, dB);
            }
            {
                float linearGain = tapeParams_.head3Level.load(std::memory_order_relaxed);
                float dB = (linearGain <= 0.0f) ? -96.0f : 20.0f * std::log10(linearGain);
                tapeDelay_.setHeadLevel(2, dB);
            }
            tapeDelay_.setHeadPan(0, tapeParams_.head1Pan.load(std::memory_order_relaxed) * 100.0f);
            tapeDelay_.setHeadPan(1, tapeParams_.head2Pan.load(std::memory_order_relaxed) * 100.0f);
            tapeDelay_.setHeadPan(2, tapeParams_.head3Pan.load(std::memory_order_relaxed) * 100.0f);
            // Copy input to output first (TapeDelay processes in-place)
            std::copy_n(inputL, numSamples, outputL);
            std::copy_n(inputR, numSamples, outputR);
            tapeDelay_.process(outputL, outputR, numSamples);
            break;

        case DelayMode::BBD:
            // Update BBD parameters
            bbdDelay_.setTime(bbdParams_.delayTime.load(std::memory_order_relaxed));
            bbdDelay_.setFeedback(bbdParams_.feedback.load(std::memory_order_relaxed));
            bbdDelay_.setModulation(bbdParams_.modulationDepth.load(std::memory_order_relaxed));
            bbdDelay_.setModulationRate(bbdParams_.modulationRate.load(std::memory_order_relaxed));
            bbdDelay_.setAge(bbdParams_.age.load(std::memory_order_relaxed));
            bbdDelay_.setEra(Parameters::getBBDEraFromDropdown(
                bbdParams_.era.load(std::memory_order_relaxed)));
            bbdDelay_.setMix(bbdParams_.mix.load(std::memory_order_relaxed));
            // Output level is already in dB (no conversion needed)
            bbdDelay_.setOutputLevel(bbdParams_.outputLevel.load(std::memory_order_relaxed));
            // Copy input to output first (BBDDelay processes in-place)
            std::copy_n(inputL, numSamples, outputL);
            std::copy_n(inputR, numSamples, outputR);
            bbdDelay_.process(outputL, outputR, numSamples);
            break;

        case DelayMode::Digital:
            // Update Digital parameters
            digitalDelay_.setTime(digitalParams_.delayTime.load(std::memory_order_relaxed));
            digitalDelay_.setTimeMode(static_cast<DSP::TimeMode>(
                digitalParams_.timeMode.load(std::memory_order_relaxed)));
            {
                // Map dropdown index (0-9) to proper NoteValue + NoteModifier
                const int noteIdx = digitalParams_.noteValue.load(std::memory_order_relaxed);
                const auto noteMapping = DSP::getNoteValueFromDropdown(noteIdx);
                digitalDelay_.setNoteValue(noteMapping.note, noteMapping.modifier);
            }
            digitalDelay_.setFeedback(digitalParams_.feedback.load(std::memory_order_relaxed));
            digitalDelay_.setLimiterCharacter(static_cast<DSP::LimiterCharacter>(
                digitalParams_.limiterCharacter.load(std::memory_order_relaxed)));
            digitalDelay_.setEra(static_cast<DSP::DigitalEra>(
                digitalParams_.era.load(std::memory_order_relaxed)));
            digitalDelay_.setAge(digitalParams_.age.load(std::memory_order_relaxed));
            digitalDelay_.setModulationDepth(digitalParams_.modulationDepth.load(std::memory_order_relaxed));
            digitalDelay_.setModulationRate(digitalParams_.modulationRate.load(std::memory_order_relaxed));
            digitalDelay_.setModulationWaveform(static_cast<DSP::Waveform>(
                digitalParams_.modulationWaveform.load(std::memory_order_relaxed)));
            digitalDelay_.setMix(digitalParams_.mix.load(std::memory_order_relaxed));
            digitalDelay_.setWidth(digitalParams_.width.load(std::memory_order_relaxed));
            // Output level is already in dB (no conversion needed)
            digitalDelay_.setOutputLevel(digitalParams_.outputLevel.load(std::memory_order_relaxed));
            // Copy input to output first (DigitalDelay processes in-place)
            std::copy_n(inputL, numSamples, outputL);
            std::copy_n(inputR, numSamples, outputR);
            digitalDelay_.process(outputL, outputR, numSamples, ctx);
            break;

        case DelayMode::PingPong:
            // Update PingPong parameters
            pingPongDelay_.setDelayTimeMs(pingPongParams_.delayTime.load(std::memory_order_relaxed));
            pingPongDelay_.setTimeMode(static_cast<DSP::TimeMode>(
                pingPongParams_.timeMode.load(std::memory_order_relaxed)));
            {
                // Map dropdown index (0-9) to proper NoteValue + NoteModifier
                const int noteIdx = pingPongParams_.noteValue.load(std::memory_order_relaxed);
                const auto noteMapping = DSP::getNoteValueFromDropdown(noteIdx);
                pingPongDelay_.setNoteValue(noteMapping.note, noteMapping.modifier);
            }
            pingPongDelay_.setLRRatio(Parameters::getLRRatioFromDropdown(
                pingPongParams_.lrRatio.load(std::memory_order_relaxed)));
            pingPongDelay_.setFeedback(pingPongParams_.feedback.load(std::memory_order_relaxed));
            pingPongDelay_.setCrossFeedback(pingPongParams_.crossFeedback.load(std::memory_order_relaxed));
            pingPongDelay_.setWidth(pingPongParams_.width.load(std::memory_order_relaxed));
            pingPongDelay_.setModulationDepth(pingPongParams_.modulationDepth.load(std::memory_order_relaxed));
            pingPongDelay_.setModulationRate(pingPongParams_.modulationRate.load(std::memory_order_relaxed));
            pingPongDelay_.setMix(pingPongParams_.mix.load(std::memory_order_relaxed));
            // Output level is already in dB (no conversion needed)
            pingPongDelay_.setOutputLevel(pingPongParams_.outputLevel.load(std::memory_order_relaxed));
            // Copy input to output first (PingPongDelay processes in-place)
            std::copy_n(inputL, numSamples, outputL);
            std::copy_n(inputR, numSamples, outputR);
            pingPongDelay_.process(outputL, outputR, numSamples, ctx);
            break;

        case DelayMode::Reverse:
            // Update Reverse parameters
            reverseDelay_.setChunkSizeMs(reverseParams_.chunkSize.load(std::memory_order_relaxed));
            reverseDelay_.setCrossfadePercent(reverseParams_.crossfade.load(std::memory_order_relaxed));
            reverseDelay_.setPlaybackMode(static_cast<DSP::PlaybackMode>(
                reverseParams_.playbackMode.load(std::memory_order_relaxed)));
            reverseDelay_.setFeedbackAmount(reverseParams_.feedback.load(std::memory_order_relaxed));
            reverseDelay_.setFilterEnabled(reverseParams_.filterEnabled.load(std::memory_order_relaxed));
            reverseDelay_.setFilterCutoff(reverseParams_.filterCutoff.load(std::memory_order_relaxed));
            reverseDelay_.setFilterType(static_cast<DSP::FilterType>(
                reverseParams_.filterType.load(std::memory_order_relaxed)));
            reverseDelay_.setDryWetMix(reverseParams_.dryWet.load(std::memory_order_relaxed) * 100.0f);
            {
                float linearGain = reverseParams_.outputGain.load(std::memory_order_relaxed);
                float dB = (linearGain <= 0.0f) ? -96.0f : 20.0f * std::log10(linearGain);
                reverseDelay_.setOutputGainDb(dB);
            }
            // Copy input to output first (ReverseDelay processes in-place)
            std::copy_n(inputL, numSamples, outputL);
            std::copy_n(inputR, numSamples, outputR);
            reverseDelay_.process(outputL, outputR, numSamples, ctx);
            break;

        case DelayMode::MultiTap:
            // Update MultiTap parameters
            multiTapDelay_.loadTimingPattern(Parameters::getTimingPatternFromDropdown(
                multiTapParams_.timingPattern.load(std::memory_order_relaxed)),
                static_cast<size_t>(multiTapParams_.tapCount.load(std::memory_order_relaxed)));
            multiTapDelay_.applySpatialPattern(Parameters::getSpatialPatternFromDropdown(
                multiTapParams_.spatialPattern.load(std::memory_order_relaxed)));
            multiTapDelay_.setBaseTimeMs(multiTapParams_.baseTime.load(std::memory_order_relaxed));
            multiTapDelay_.setTempo(multiTapParams_.tempo.load(std::memory_order_relaxed));
            multiTapDelay_.setFeedbackAmount(multiTapParams_.feedback.load(std::memory_order_relaxed));
            multiTapDelay_.setFeedbackLPCutoff(multiTapParams_.feedbackLPCutoff.load(std::memory_order_relaxed));
            multiTapDelay_.setFeedbackHPCutoff(multiTapParams_.feedbackHPCutoff.load(std::memory_order_relaxed));
            multiTapDelay_.setMorphTime(multiTapParams_.morphTime.load(std::memory_order_relaxed));
            multiTapDelay_.setDryWetMix(multiTapParams_.dryWet.load(std::memory_order_relaxed));
            // Output level is already in dB (no conversion needed)
            multiTapDelay_.setOutputLevel(multiTapParams_.outputLevel.load(std::memory_order_relaxed));
            // Copy input to output first (MultiTapDelay processes in-place)
            std::copy_n(inputL, numSamples, outputL);
            std::copy_n(inputR, numSamples, outputR);
            multiTapDelay_.process(outputL, outputR, numSamples, ctx);
            break;

        case DelayMode::Freeze:
            // Update Freeze parameters
            freezeMode_.setFreezeEnabled(freezeParams_.freezeEnabled.load(std::memory_order_relaxed));
            freezeMode_.setDelayTimeMs(freezeParams_.delayTime.load(std::memory_order_relaxed));
            freezeMode_.setFeedbackAmount(freezeParams_.feedback.load(std::memory_order_relaxed));
            freezeMode_.setPitchSemitones(freezeParams_.pitchSemitones.load(std::memory_order_relaxed));
            freezeMode_.setPitchCents(freezeParams_.pitchCents.load(std::memory_order_relaxed));
            freezeMode_.setShimmerMix(freezeParams_.shimmerMix.load(std::memory_order_relaxed) * 100.0f);
            freezeMode_.setDecay(freezeParams_.decay.load(std::memory_order_relaxed) * 100.0f);
            freezeMode_.setDiffusionAmount(freezeParams_.diffusionAmount.load(std::memory_order_relaxed) * 100.0f);
            freezeMode_.setDiffusionSize(freezeParams_.diffusionSize.load(std::memory_order_relaxed) * 100.0f);
            freezeMode_.setFilterEnabled(freezeParams_.filterEnabled.load(std::memory_order_relaxed));
            freezeMode_.setFilterType(static_cast<DSP::FilterType>(
                freezeParams_.filterType.load(std::memory_order_relaxed)));
            freezeMode_.setFilterCutoff(freezeParams_.filterCutoff.load(std::memory_order_relaxed));
            freezeMode_.setDryWetMix(freezeParams_.dryWet.load(std::memory_order_relaxed) * 100.0f);
            {
                float linearGain = freezeParams_.outputGain.load(std::memory_order_relaxed);
                float dB = (linearGain <= 0.0f) ? -96.0f : 20.0f * std::log10(linearGain);
                freezeMode_.setOutputGainDb(dB);
            }
            // Copy input to output first (FreezeMode processes in-place)
            std::copy_n(inputL, numSamples, outputL);
            std::copy_n(inputR, numSamples, outputR);
            freezeMode_.process(outputL, outputR, numSamples, ctx);
            break;

        case DelayMode::Ducking:
            // Update Ducking parameters
            duckingDelay_.setDuckingEnabled(duckingParams_.duckingEnabled.load(std::memory_order_relaxed));
            duckingDelay_.setThreshold(duckingParams_.threshold.load(std::memory_order_relaxed));
            duckingDelay_.setDuckAmount(duckingParams_.duckAmount.load(std::memory_order_relaxed));
            duckingDelay_.setAttackTime(duckingParams_.attackTime.load(std::memory_order_relaxed));
            duckingDelay_.setReleaseTime(duckingParams_.releaseTime.load(std::memory_order_relaxed));
            duckingDelay_.setHoldTime(duckingParams_.holdTime.load(std::memory_order_relaxed));
            duckingDelay_.setDuckTarget(static_cast<DSP::DuckTarget>(
                duckingParams_.duckTarget.load(std::memory_order_relaxed)));
            duckingDelay_.setSidechainFilterEnabled(duckingParams_.sidechainFilterEnabled.load(std::memory_order_relaxed));
            duckingDelay_.setSidechainFilterCutoff(duckingParams_.sidechainFilterCutoff.load(std::memory_order_relaxed));
            duckingDelay_.setDelayTimeMs(duckingParams_.delayTime.load(std::memory_order_relaxed));
            duckingDelay_.setFeedbackAmount(duckingParams_.feedback.load(std::memory_order_relaxed));
            duckingDelay_.setDryWetMix(duckingParams_.dryWet.load(std::memory_order_relaxed));
            duckingDelay_.setOutputGainDb(duckingParams_.outputGain.load(std::memory_order_relaxed));
            // Copy input to output first (DuckingDelay processes in-place)
            std::copy_n(inputL, numSamples, outputL);
            std::copy_n(inputR, numSamples, outputR);
            duckingDelay_.process(outputL, outputR, numSamples, ctx);
            break;

        default:
            // Unknown mode - pass through
            std::copy_n(inputL, numSamples, outputL);
            std::copy_n(inputR, numSamples, outputR);
            break;
    }

    // Apply output gain
    for (Steinberg::int32 i = 0; i < data.numSamples; ++i) {
        outputL[i] *= currentGain;
        outputR[i] *= currentGain;
    }

    return Steinberg::kResultTrue;
}

Steinberg::tresult PLUGIN_API Processor::setBusArrangements(
    Steinberg::Vst::SpeakerArrangement* inputs, Steinberg::int32 numIns,
    Steinberg::Vst::SpeakerArrangement* outputs, Steinberg::int32 numOuts) {

    // Accept only stereo in/out for this example
    if (numIns == 1 && numOuts == 1 &&
        inputs[0] == Steinberg::Vst::SpeakerArr::kStereo &&
        outputs[0] == Steinberg::Vst::SpeakerArr::kStereo) {
        return AudioEffect::setBusArrangements(inputs, numIns, outputs, numOuts);
    }

    return Steinberg::kResultFalse;
}

// ==============================================================================
// IComponent - State Management
// ==============================================================================

Steinberg::tresult PLUGIN_API Processor::getState(Steinberg::IBStream* state) {
    // Save processor state for project save
    // Constitution Principle I: This state will be sent to Controller

    Steinberg::IBStreamer streamer(state, kLittleEndian);

    // Save global parameters
    float gain = gain_.load(std::memory_order_relaxed);
    streamer.writeFloat(gain);

    Steinberg::int32 bypass = bypass_.load(std::memory_order_relaxed) ? 1 : 0;
    streamer.writeInt32(bypass);

    Steinberg::int32 mode = mode_.load(std::memory_order_relaxed);
    streamer.writeInt32(mode);

    // Save mode-specific parameter packs
    saveGranularParams(granularParams_, streamer);
    saveSpectralParams(spectralParams_, streamer);
    saveDuckingParams(duckingParams_, streamer);
    saveFreezeParams(freezeParams_, streamer);
    saveReverseParams(reverseParams_, streamer);
    saveShimmerParams(shimmerParams_, streamer);
    saveTapeParams(tapeParams_, streamer);
    saveBBDParams(bbdParams_, streamer);
    saveDigitalParams(digitalParams_, streamer);
    savePingPongParams(pingPongParams_, streamer);
    saveMultiTapParams(multiTapParams_, streamer);

    return Steinberg::kResultTrue;
}

Steinberg::tresult PLUGIN_API Processor::setState(Steinberg::IBStream* state) {
    // Restore processor state for project load

    Steinberg::IBStreamer streamer(state, kLittleEndian);

    // Restore global parameters
    float gain = 1.0f;
    if (streamer.readFloat(gain)) {
        gain_.store(gain, std::memory_order_relaxed);
    }

    Steinberg::int32 bypass = 0;
    if (streamer.readInt32(bypass)) {
        bypass_.store(bypass != 0, std::memory_order_relaxed);
    }

    Steinberg::int32 mode = 0;
    if (streamer.readInt32(mode)) {
        mode_.store(mode, std::memory_order_relaxed);
    }

    // Restore mode-specific parameter packs
    loadGranularParams(granularParams_, streamer);
    loadSpectralParams(spectralParams_, streamer);
    loadDuckingParams(duckingParams_, streamer);
    loadFreezeParams(freezeParams_, streamer);
    loadReverseParams(reverseParams_, streamer);
    loadShimmerParams(shimmerParams_, streamer);
    loadTapeParams(tapeParams_, streamer);
    loadBBDParams(bbdParams_, streamer);
    loadDigitalParams(digitalParams_, streamer);
    loadPingPongParams(pingPongParams_, streamer);
    loadMultiTapParams(multiTapParams_, streamer);

    return Steinberg::kResultTrue;
}

// ==============================================================================
// Parameter Handling
// ==============================================================================

void Processor::processParameterChanges(Steinberg::Vst::IParameterChanges* changes) {
    if (!changes) {
        return;
    }

    const Steinberg::int32 numParamsChanged = changes->getParameterCount();

    for (Steinberg::int32 i = 0; i < numParamsChanged; ++i) {
        Steinberg::Vst::IParamValueQueue* paramQueue = changes->getParameterData(i);
        if (!paramQueue) {
            continue;
        }

        const Steinberg::Vst::ParamID paramId = paramQueue->getParameterId();
        const Steinberg::int32 numPoints = paramQueue->getPointCount();

        // Get the last value (most recent)
        Steinberg::int32 sampleOffset = 0;
        Steinberg::Vst::ParamValue value = 0.0;

        if (paramQueue->getPoint(numPoints - 1, sampleOffset, value)
            != Steinberg::kResultTrue) {
            continue;
        }

        // =======================================================================
        // Route parameter changes by ID range
        // Constitution Principle V: Values are normalized 0.0 to 1.0
        // =======================================================================

        if (paramId < kGranularBaseId) {
            // Global parameters (0-99)
            switch (paramId) {
                case kGainId:
                    // Convert normalized to linear gain (0.0 to 2.0 range)
                    gain_.store(static_cast<float>(value * 2.0),
                               std::memory_order_relaxed);
                    break;

                case kBypassId:
                    bypass_.store(value >= 0.5, std::memory_order_relaxed);
                    break;

                case kModeId:
                    // Convert normalized (0-1) to mode index (0-10)
                    mode_.store(static_cast<int>(value * 10.0 + 0.5),
                               std::memory_order_relaxed);
                    break;

                default:
                    break;
            }
        }
        else if (paramId >= kGranularBaseId && paramId <= kGranularEndId) {
            // Granular Delay parameters (100-199) - spec 034
            handleGranularParamChange(granularParams_, paramId, value);
        }
        else if (paramId >= kSpectralBaseId && paramId <= kSpectralEndId) {
            // Spectral Delay parameters (200-299) - spec 033
            handleSpectralParamChange(spectralParams_, paramId, value);
        }
        else if (paramId >= kShimmerBaseId && paramId <= kShimmerEndId) {
            // Shimmer Delay parameters (300-399) - spec 029
            handleShimmerParamChange(shimmerParams_, paramId, value);
        }
        else if (paramId >= kTapeBaseId && paramId <= kTapeEndId) {
            // Tape Delay parameters (400-499) - spec 024
            handleTapeParamChange(tapeParams_, paramId, value);
        }
        else if (paramId >= kBBDBaseId && paramId <= kBBDEndId) {
            // BBD Delay parameters (500-599) - spec 025
            handleBBDParamChange(bbdParams_, paramId, value);
        }
        else if (paramId >= kDigitalBaseId && paramId <= kDigitalEndId) {
            // Digital Delay parameters (600-699) - spec 026
            handleDigitalParamChange(digitalParams_, paramId, value);
        }
        else if (paramId >= kPingPongBaseId && paramId <= kPingPongEndId) {
            // PingPong Delay parameters (700-799) - spec 027
            handlePingPongParamChange(pingPongParams_, paramId, value);
        }
        else if (paramId >= kReverseBaseId && paramId <= kReverseEndId) {
            // Reverse Delay parameters (800-899) - spec 030
            handleReverseParamChange(reverseParams_, paramId, value);
        }
        else if (paramId >= kMultiTapBaseId && paramId <= kMultiTapEndId) {
            // MultiTap Delay parameters (900-999) - spec 028
            handleMultiTapParamChange(multiTapParams_, paramId, value);
        }
        else if (paramId >= kFreezeBaseId && paramId <= kFreezeEndId) {
            // Freeze Mode parameters (1000-1099) - spec 031
            handleFreezeParamChange(freezeParams_, paramId, value);
        }
        else if (paramId >= kDuckingBaseId && paramId <= kDuckingEndId) {
            // Ducking Delay parameters (1100-1199) - spec 032
            handleDuckingParamChange(duckingParams_, paramId, value);
        }
    }
}

} // namespace Iterum
