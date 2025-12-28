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
#include "dsp/features/bbd_delay.h"
#include "dsp/features/digital_delay.h"
#include "dsp/features/ducking_delay.h"
#include "dsp/features/freeze_mode.h"
#include "dsp/features/granular_delay.h"
#include "dsp/features/multi_tap_delay.h"
#include "dsp/features/ping_pong_delay.h"
#include "dsp/features/reverse_delay.h"
#include "dsp/features/shimmer_delay.h"
#include "dsp/features/spectral_delay.h"
#include "dsp/features/tape_delay.h"
#include "parameters/bbd_params.h"
#include "parameters/digital_params.h"
#include "parameters/ducking_params.h"
#include "parameters/freeze_params.h"
#include "parameters/granular_params.h"
#include "parameters/multitap_params.h"
#include "parameters/pingpong_params.h"
#include "parameters/reverse_params.h"
#include "parameters/shimmer_params.h"
#include "parameters/spectral_params.h"
#include "parameters/tape_params.h"
#include "parameters/dropdown_mappings.h"

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
    std::atomic<int> mode_{0};  // DelayMode enum value (0 = Granular)

    // ==========================================================================
    // Mode-Specific Parameter Packs
    // ==========================================================================

    GranularParams granularParams_;   // Granular Delay (spec 034)
    SpectralParams spectralParams_;   // Spectral Delay (spec 033)
    DuckingParams duckingParams_;     // Ducking Delay (spec 032)
    FreezeParams freezeParams_;       // Freeze Mode (spec 031)
    ReverseParams reverseParams_;     // Reverse Delay (spec 030)
    ShimmerParams shimmerParams_;     // Shimmer Delay (spec 029)
    TapeParams tapeParams_;           // Tape Delay (spec 024)
    BBDParams bbdParams_;             // BBD Delay (spec 025)
    DigitalParams digitalParams_;     // Digital Delay (spec 026)
    PingPongParams pingPongParams_;   // PingPong Delay (spec 027)
    MultiTapParams multiTapParams_;   // MultiTap Delay (spec 028)

    // ==========================================================================
    // DSP Components
    // ==========================================================================

    DSP::GranularDelay granularDelay_;
    DSP::SpectralDelay spectralDelay_;
    DSP::DuckingDelay duckingDelay_;
    DSP::FreezeMode freezeMode_;
    DSP::ReverseDelay reverseDelay_;
    DSP::ShimmerDelay shimmerDelay_;
    DSP::TapeDelay tapeDelay_;
    DSP::BBDDelay bbdDelay_;
    DSP::DigitalDelay digitalDelay_;
    DSP::PingPongDelay pingPongDelay_;
    DSP::MultiTapDelay multiTapDelay_;
};

} // namespace Iterum
