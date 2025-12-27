// ==============================================================================
// Audio Processor Implementation
// ==============================================================================

#include "processor.h"
#include "plugin_ids.h"

#include "base/source/fstreamer.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"

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

    return AudioEffect::setupProcessing(setup);
}

Steinberg::tresult PLUGIN_API Processor::setActive(Steinberg::TBool state) {
    if (state) {
        // Activating: reset any processing state
        granularDelay_.reset();
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
    // Update GranularDelay parameters from atomics
    // ==========================================================================

    granularDelay_.setGrainSize(granularGrainSize_.load(std::memory_order_relaxed));
    granularDelay_.setDensity(granularDensity_.load(std::memory_order_relaxed));
    granularDelay_.setDelayTime(granularDelayTime_.load(std::memory_order_relaxed));
    granularDelay_.setPitch(granularPitch_.load(std::memory_order_relaxed));
    granularDelay_.setPitchSpray(granularPitchSpray_.load(std::memory_order_relaxed));
    granularDelay_.setPositionSpray(granularPositionSpray_.load(std::memory_order_relaxed));
    granularDelay_.setPanSpray(granularPanSpray_.load(std::memory_order_relaxed));
    granularDelay_.setReverseProbability(granularReverseProb_.load(std::memory_order_relaxed));
    granularDelay_.setFreeze(granularFreeze_.load(std::memory_order_relaxed));
    granularDelay_.setFeedback(granularFeedback_.load(std::memory_order_relaxed));
    granularDelay_.setDryWet(granularDryWet_.load(std::memory_order_relaxed));
    granularDelay_.setOutputGain(granularOutputGain_.load(std::memory_order_relaxed));
    granularDelay_.setEnvelopeType(static_cast<DSP::GrainEnvelopeType>(
        granularEnvelopeType_.load(std::memory_order_relaxed)));

    // ==========================================================================
    // Process audio through GranularDelay
    // ==========================================================================

    granularDelay_.process(inputL, inputR, outputL, outputR,
                           static_cast<size_t>(data.numSamples));

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

    // Save parameter values
    float gain = gain_.load(std::memory_order_relaxed);
    streamer.writeFloat(gain);

    Steinberg::int32 bypass = bypass_.load(std::memory_order_relaxed) ? 1 : 0;
    streamer.writeInt32(bypass);

    // Save granular delay parameters (spec 034)
    streamer.writeFloat(granularGrainSize_.load(std::memory_order_relaxed));
    streamer.writeFloat(granularDensity_.load(std::memory_order_relaxed));
    streamer.writeFloat(granularDelayTime_.load(std::memory_order_relaxed));
    streamer.writeFloat(granularPitch_.load(std::memory_order_relaxed));
    streamer.writeFloat(granularPitchSpray_.load(std::memory_order_relaxed));
    streamer.writeFloat(granularPositionSpray_.load(std::memory_order_relaxed));
    streamer.writeFloat(granularPanSpray_.load(std::memory_order_relaxed));
    streamer.writeFloat(granularReverseProb_.load(std::memory_order_relaxed));
    Steinberg::int32 freeze = granularFreeze_.load(std::memory_order_relaxed) ? 1 : 0;
    streamer.writeInt32(freeze);
    streamer.writeFloat(granularFeedback_.load(std::memory_order_relaxed));
    streamer.writeFloat(granularDryWet_.load(std::memory_order_relaxed));
    streamer.writeFloat(granularOutputGain_.load(std::memory_order_relaxed));
    streamer.writeInt32(granularEnvelopeType_.load(std::memory_order_relaxed));

    return Steinberg::kResultTrue;
}

Steinberg::tresult PLUGIN_API Processor::setState(Steinberg::IBStream* state) {
    // Restore processor state for project load

    Steinberg::IBStreamer streamer(state, kLittleEndian);

    float gain = 1.0f;
    if (streamer.readFloat(gain)) {
        gain_.store(gain, std::memory_order_relaxed);
    }

    Steinberg::int32 bypass = 0;
    if (streamer.readInt32(bypass)) {
        bypass_.store(bypass != 0, std::memory_order_relaxed);
    }

    // Restore granular delay parameters (spec 034)
    float floatVal = 0.0f;
    Steinberg::int32 intVal = 0;

    if (streamer.readFloat(floatVal)) {
        granularGrainSize_.store(floatVal, std::memory_order_relaxed);
    }
    if (streamer.readFloat(floatVal)) {
        granularDensity_.store(floatVal, std::memory_order_relaxed);
    }
    if (streamer.readFloat(floatVal)) {
        granularDelayTime_.store(floatVal, std::memory_order_relaxed);
    }
    if (streamer.readFloat(floatVal)) {
        granularPitch_.store(floatVal, std::memory_order_relaxed);
    }
    if (streamer.readFloat(floatVal)) {
        granularPitchSpray_.store(floatVal, std::memory_order_relaxed);
    }
    if (streamer.readFloat(floatVal)) {
        granularPositionSpray_.store(floatVal, std::memory_order_relaxed);
    }
    if (streamer.readFloat(floatVal)) {
        granularPanSpray_.store(floatVal, std::memory_order_relaxed);
    }
    if (streamer.readFloat(floatVal)) {
        granularReverseProb_.store(floatVal, std::memory_order_relaxed);
    }
    if (streamer.readInt32(intVal)) {
        granularFreeze_.store(intVal != 0, std::memory_order_relaxed);
    }
    if (streamer.readFloat(floatVal)) {
        granularFeedback_.store(floatVal, std::memory_order_relaxed);
    }
    if (streamer.readFloat(floatVal)) {
        granularDryWet_.store(floatVal, std::memory_order_relaxed);
    }
    if (streamer.readFloat(floatVal)) {
        granularOutputGain_.store(floatVal, std::memory_order_relaxed);
    }
    if (streamer.readInt32(intVal)) {
        granularEnvelopeType_.store(intVal, std::memory_order_relaxed);
    }

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

        // Apply parameter change
        // Constitution Principle V: Values are normalized 0.0 to 1.0
        switch (paramId) {
            case kGainId:
                // Convert normalized to linear gain (0.0 to 2.0 range)
                gain_.store(static_cast<float>(value * 2.0),
                           std::memory_order_relaxed);
                break;

            case kBypassId:
                bypass_.store(value >= 0.5, std::memory_order_relaxed);
                break;

            // =====================================================================
            // Granular Delay Parameters (spec 034)
            // =====================================================================

            case kGranularGrainSizeId:
                // 10-500ms range
                granularGrainSize_.store(
                    static_cast<float>(10.0 + value * 490.0),
                    std::memory_order_relaxed);
                break;

            case kGranularDensityId:
                // 1-100 grains/sec
                granularDensity_.store(
                    static_cast<float>(1.0 + value * 99.0),
                    std::memory_order_relaxed);
                break;

            case kGranularDelayTimeId:
                // 0-2000ms
                granularDelayTime_.store(
                    static_cast<float>(value * 2000.0),
                    std::memory_order_relaxed);
                break;

            case kGranularPitchId:
                // -24 to +24 semitones
                granularPitch_.store(
                    static_cast<float>(-24.0 + value * 48.0),
                    std::memory_order_relaxed);
                break;

            case kGranularPitchSprayId:
                // 0-1 (already normalized)
                granularPitchSpray_.store(
                    static_cast<float>(value),
                    std::memory_order_relaxed);
                break;

            case kGranularPositionSprayId:
                // 0-1 (already normalized)
                granularPositionSpray_.store(
                    static_cast<float>(value),
                    std::memory_order_relaxed);
                break;

            case kGranularPanSprayId:
                // 0-1 (already normalized)
                granularPanSpray_.store(
                    static_cast<float>(value),
                    std::memory_order_relaxed);
                break;

            case kGranularReverseProbId:
                // 0-1 (already normalized)
                granularReverseProb_.store(
                    static_cast<float>(value),
                    std::memory_order_relaxed);
                break;

            case kGranularFreezeId:
                // Boolean switch
                granularFreeze_.store(value >= 0.5, std::memory_order_relaxed);
                break;

            case kGranularFeedbackId:
                // 0-1.2 range (allows self-oscillation)
                granularFeedback_.store(
                    static_cast<float>(value * 1.2),
                    std::memory_order_relaxed);
                break;

            case kGranularDryWetId:
                // 0-1 (already normalized)
                granularDryWet_.store(
                    static_cast<float>(value),
                    std::memory_order_relaxed);
                break;

            case kGranularOutputGainId:
                // -96 to +6 dB
                granularOutputGain_.store(
                    static_cast<float>(-96.0 + value * 102.0),
                    std::memory_order_relaxed);
                break;

            case kGranularEnvelopeTypeId:
                // 0-3 (Hann, Trapezoid, Sine, Blackman)
                granularEnvelopeType_.store(
                    static_cast<int>(value * 3.0 + 0.5),
                    std::memory_order_relaxed);
                break;

            default:
                break;
        }
    }
}

} // namespace Iterum
