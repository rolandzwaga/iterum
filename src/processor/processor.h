#pragma once

// ==============================================================================
// Audio Processor
// ==============================================================================
// Constitution Principle I: VST3 Architecture Separation
// - This is the Processor component (IAudioProcessor + IComponent)
// - MUST be completely separate from Controller
// - MUST function without Controller instantiation
//
// Constitution Principle II: Real-Time Audio Thread Safety
// - NEVER allocate memory in process()
// - NEVER use locks/mutexes
// - Pre-allocate ALL buffers in setupProcessing()
// ==============================================================================

#include "public.sdk/source/vst/vstaudioeffect.h"
#include "dsp/dsp_utils.h"
#include "dsp/features/granular_delay.h"

#include <array>
#include <atomic>

namespace Iterum {

// ==============================================================================
// Processor Class
// ==============================================================================

class Processor : public Steinberg::Vst::AudioEffect {
public:
    Processor();
    ~Processor() override = default;

    // ===========================================================================
    // IPluginBase
    // ===========================================================================

    /// Called when the plugin is first loaded
    Steinberg::tresult PLUGIN_API initialize(FUnknown* context) override;

    /// Called when the plugin is unloaded
    Steinberg::tresult PLUGIN_API terminate() override;

    // ===========================================================================
    // IAudioProcessor
    // ===========================================================================

    /// Called before processing starts - allocate ALL buffers here
    /// Constitution Principle II: Pre-allocate everything in this method
    Steinberg::tresult PLUGIN_API setupProcessing(
        Steinberg::Vst::ProcessSetup& setup) override;

    /// Called when audio processing starts/stops
    Steinberg::tresult PLUGIN_API setActive(Steinberg::TBool state) override;

    /// Main audio processing callback
    /// Constitution Principle II: NO allocations, NO locks, NO exceptions
    Steinberg::tresult PLUGIN_API process(
        Steinberg::Vst::ProcessData& data) override;

    /// Report audio I/O configuration support
    Steinberg::tresult PLUGIN_API setBusArrangements(
        Steinberg::Vst::SpeakerArrangement* inputs, Steinberg::int32 numIns,
        Steinberg::Vst::SpeakerArrangement* outputs, Steinberg::int32 numOuts) override;

    // ===========================================================================
    // IComponent
    // ===========================================================================

    /// Save processor state (called by host for project save)
    /// Constitution Principle I: State sync via setComponentState()
    Steinberg::tresult PLUGIN_API getState(Steinberg::IBStream* state) override;

    /// Restore processor state (called by host for project load)
    Steinberg::tresult PLUGIN_API setState(Steinberg::IBStream* state) override;

    // ===========================================================================
    // Factory
    // ===========================================================================

    static FUnknown* createInstance(void*) {
        return static_cast<Steinberg::Vst::IAudioProcessor*>(new Processor());
    }

protected:
    // ==========================================================================
    // Parameter Handling
    // ==========================================================================

    /// Process parameter changes from the input queue
    /// Called at the start of each process() call
    void processParameterChanges(Steinberg::Vst::IParameterChanges* changes);

private:
    // ==========================================================================
    // Processing State
    // ==========================================================================

    // Sample rate for DSP calculations
    double sampleRate_ = 44100.0;

    // Maximum expected block size (for buffer pre-allocation)
    Steinberg::int32 maxBlockSize_ = 0;

    // ==========================================================================
    // Parameters (atomic for thread-safe access)
    // Constitution Principle VI: Use std::atomic for simple shared state
    // ==========================================================================

    std::atomic<float> gain_{1.0f};
    std::atomic<bool> bypass_{false};

    // ==========================================================================
    // Granular Delay Parameters (spec 034)
    // ==========================================================================

    std::atomic<float> granularGrainSize_{100.0f};     // ms
    std::atomic<float> granularDensity_{10.0f};        // grains/sec
    std::atomic<float> granularDelayTime_{500.0f};     // ms
    std::atomic<float> granularPitch_{0.0f};           // semitones
    std::atomic<float> granularPitchSpray_{0.0f};      // 0-1
    std::atomic<float> granularPositionSpray_{0.0f};   // 0-1
    std::atomic<float> granularPanSpray_{0.0f};        // 0-1
    std::atomic<float> granularReverseProb_{0.0f};     // 0-1
    std::atomic<bool> granularFreeze_{false};
    std::atomic<float> granularFeedback_{0.0f};        // 0-1.2
    std::atomic<float> granularDryWet_{0.5f};          // 0-1
    std::atomic<float> granularOutputGain_{0.0f};      // dB
    std::atomic<int> granularEnvelopeType_{0};         // 0-3

    // ==========================================================================
    // DSP Components
    // ==========================================================================

    DSP::GranularDelay granularDelay_;
};

} // namespace Iterum
