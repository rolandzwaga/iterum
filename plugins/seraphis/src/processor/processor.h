#pragma once

// ==============================================================================
// Seraphis - Audio Processor (audio thread)
// ==============================================================================
// Constitution Principle I: VST3 Architecture Separation.
// This header NEVER includes anything under controller/.
// ==============================================================================

#include "public.sdk/source/vst/vstaudioeffect.h"

#include "parameters/global_params.h"
#include "parameters/macro_params.h"

#include <krate/dsp/effects/aether_reverb.h>
#include <krate/dsp/primitives/smoother.h>
#include <krate/dsp/systems/seraphis_engine.h>
#include <krate/dsp/systems/seraphis_macro_matrix.h>

#include <array>
#include <cstddef>
#include <memory>
#include <vector>

namespace Seraphis {

class Processor : public Steinberg::Vst::AudioEffect {
public:
    Processor();
    ~Processor() override;

    static Steinberg::FUnknown* createInstance(void* /*context*/) {
        return static_cast<Steinberg::Vst::IAudioProcessor*>(new Processor());
    }

    Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown* context) override;
    Steinberg::tresult PLUGIN_API terminate() override;
    Steinberg::tresult PLUGIN_API setBusArrangements(
        Steinberg::Vst::SpeakerArrangement* inputs, Steinberg::int32 numIns,
        Steinberg::Vst::SpeakerArrangement* outputs, Steinberg::int32 numOuts) override;
    Steinberg::tresult PLUGIN_API setupProcessing(Steinberg::Vst::ProcessSetup& setup) override;
    Steinberg::tresult PLUGIN_API setActive(Steinberg::TBool state) override;
    Steinberg::tresult PLUGIN_API process(Steinberg::Vst::ProcessData& data) override;
    Steinberg::uint32 PLUGIN_API getLatencySamples() override;
    Steinberg::tresult PLUGIN_API setState(Steinberg::IBStream* state) override;
    Steinberg::tresult PLUGIN_API getState(Steinberg::IBStream* state) override;

    // -------------------------------------------------------------------------
    // Test-only read surfaces (NEVER called from process()).
    // -------------------------------------------------------------------------
    [[nodiscard]] Krate::DSP::SeraphisEngine* engineForTest() noexcept { return engine_.get(); }
    [[nodiscard]] Krate::DSP::AetherReverb* reverbForTest() noexcept { return reverb_.get(); }
    [[nodiscard]] std::size_t setPolyphonyCallCountForTest() const noexcept {
        return setPolyphonyCalls_;
    }

private:
    void processParameterChanges(Steinberg::Vst::IParameterChanges* changes) noexcept;
    void pushGlobalParams() noexcept;                                    // FR-024 step 0 / FR-024a
    void renderSlice(float* outL, float* outR, std::size_t n) noexcept;  // FR-024 steps 2-6

    // NO announceLatencyIfChanged(), NO lastReportedLatency_. Plan §1.3 C-1/C-2:
    // the reported latency is the CONSTANT 1024 in every reachable state
    // (AetherReverb::getLatencySamples() returns spectralEnabled_ ?
    // diffusionFftSize_ : 0, already 1024 before any prepare), so there is no
    // transition to announce; and a Steinberg::Vst::AudioEffect has NO route to
    // an IComponentHandler - the handler is delivered only to the edit
    // controller. The FUnknownPtr<IComponentHandler>(getHostContext())
    // substitute is FORBIDDEN: that FUnknown* is an IHostApplication and the
    // query is null in every real host. If a later phase makes the latency
    // variable, add it as processor -> IMessage -> controller ->
    // getComponentHandler()->restartComponent(kLatencyChanged).

    // FR-022: NEVER by value. sizeof(SeraphisEngine) is 771 968 B against MSVC's
    // 1 MiB default stack (seraphis_engine.h:119-122, :159-164).
    std::unique_ptr<Krate::DSP::SeraphisEngine> engine_;
    std::unique_ptr<Krate::DSP::AetherReverb> reverb_;
    Krate::DSP::SeraphisMacroMatrix macros_{};  // ~20 B; by value is fine

    GlobalParams globalParams_{};
    MacroParams macroParams_{};  // INERT in Phase 8 (FR-041)

    Krate::DSP::OnePoleSmoother masterGain_{1.0f};

    // NO [[maybe_unused]] on any member below. Every one is read on a live path,
    // so the attribute would only serve to hide a genuinely dead private field
    // from Clang's -Wunused-private-field on the macOS/Linux CI legs.
    bool anySamplesSincePrepare_ = false;  // FR-024a cl.3 snap seam
    double sampleRate_ = 44100.0;
    bool prepared_ = false;
    std::size_t lastPushedPolyphony_ = 0;  // FR-024a cl.1 on-change tracker
    bool lastPushedSoftLimit_ = true;      // FR-044 on-change tracker
    std::size_t setPolyphonyCalls_ = 0;    // test seam only (read by the accessor above)

    // FR-028: sized ONCE at setupProcessing(), to the CONSTANT 2048.
    std::vector<float> dryL_, dryR_, wetL_, wetR_;
    // Filled by renderSlice()'s bloom lifecycle (FR-024 step 6).
    std::array<float, Krate::DSP::SeraphisEngine::kBloomPartialCap> bloomPartials_{};
};

// FR-067: unique_ptr ownership keeps the object small enough for a stack local
// in tests. If this ever fails, the tests must heap-allocate the processor
// (seraphis_engine.h:119-122).
static_assert(sizeof(Processor) < 64u * 1024u,
              "FR-067: Processor must stay small; the 771 968 B engine lives on the heap");

}  // namespace Seraphis
