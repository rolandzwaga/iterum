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
    // This is the ONLY place where memory allocation is permitted.
    // Allocate all buffers, delay lines, lookup tables, etc. here.
    //
    // Example:
    // delayBuffer_.resize(static_cast<size_t>(sampleRate_ * maxDelaySeconds_));
    // std::fill(delayBuffer_.begin(), delayBuffer_.end(), 0.0f);
    // ==========================================================================

    return AudioEffect::setupProcessing(setup);
}

Steinberg::tresult PLUGIN_API Processor::setActive(Steinberg::TBool state) {
    if (state) {
        // Activating: reset any processing state
        // Clear delay lines, reset filters, etc.
    } else {
        // Deactivating: optional cleanup
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

    // Verify we have valid I/O
    if (data.numInputs == 0 || data.numOutputs == 0) {
        return Steinberg::kResultTrue;
    }

    const Steinberg::int32 numChannels =
        std::min(data.inputs[0].numChannels, data.outputs[0].numChannels);
    const Steinberg::int32 numSamples = data.numSamples;

    // Process each channel
    for (Steinberg::int32 channel = 0; channel < numChannels; ++channel) {
        float* inputBuffer = data.inputs[0].channelBuffers32[channel];
        float* outputBuffer = data.outputs[0].channelBuffers32[channel];

        if (!inputBuffer || !outputBuffer) {
            continue;
        }

        // ==========================================================================
        // Constitution Principle IV: SIMD & DSP Optimization
        // - Process samples in contiguous, sequential order
        // - Minimize branching in inner loop
        // - Consider SIMD intrinsics for production code
        // ==========================================================================

        // Simple gain processing example
        // In production, use DSP utilities from src/dsp/
        for (Steinberg::int32 sample = 0; sample < numSamples; ++sample) {
            outputBuffer[sample] = inputBuffer[sample] * currentGain;
        }
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

            default:
                break;
        }
    }
}

} // namespace Iterum
