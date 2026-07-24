#pragma once

// ==============================================================================
// Gradus Processor — Audio Thread Component
// ==============================================================================

#include "../parameters/arpeggiator_params.h"
#include "../dsp/audition_voice.h"

#include <krate/dsp/processors/arpeggiator_core.h>
#include <krate/dsp/processors/midi_note_delay.h>
#include <krate/dsp/systems/voice_allocator.h>

#include "public.sdk/source/vst/vstaudioeffect.h"

#include <array>
#include <atomic>
#include <span>

namespace Gradus {

// Shared constants
static constexpr int kMaxLaneSteps = 32;
static constexpr float kMaxLaneStepsF = 32.0f;
static constexpr float kAuditionDecayMinMs = 10.0f;
static constexpr float kAuditionDecayMaxMs = 2000.0f;
static constexpr float kAuditionDecayRangeMs = kAuditionDecayMaxMs - kAuditionDecayMinMs;
static constexpr int kAuditionWaveformCount = 3;
/// Voices in the audition pool. The arp can sound a full held chord plus its
/// MIDI-delay echoes at once, so a monophonic monitor misrepresented anything
/// but a single-note pattern.
static constexpr size_t kAuditionVoiceCount = 8;

class Processor : public Steinberg::Vst::AudioEffect
{
public:
    Processor();
    ~Processor() override = default;

    // --- IAudioProcessor ---
    Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown* context) override;
    Steinberg::tresult PLUGIN_API terminate() override;
    Steinberg::tresult PLUGIN_API setActive(Steinberg::TBool state) override;
    Steinberg::tresult PLUGIN_API setupProcessing(
        Steinberg::Vst::ProcessSetup& setup) override;
    Steinberg::tresult PLUGIN_API setBusArrangements(
        Steinberg::Vst::SpeakerArrangement* inputs, Steinberg::int32 numIns,
        Steinberg::Vst::SpeakerArrangement* outputs, Steinberg::int32 numOuts) override;
    Steinberg::tresult PLUGIN_API process(
        Steinberg::Vst::ProcessData& data) override;
    Steinberg::tresult PLUGIN_API getState(Steinberg::IBStream* state) override;
    Steinberg::tresult PLUGIN_API setState(Steinberg::IBStream* state) override;
    Steinberg::tresult PLUGIN_API notify(Steinberg::Vst::IMessage* message) override;

    static Steinberg::FUnknown* createInstance(void*)
    {
        return static_cast<Steinberg::Vst::IAudioProcessor*>(new Processor());
    }

private:
    // Parameter handling
    void processParameterChanges(Steinberg::Vst::IParameterChanges* changes);
    void applyParamsToEngine();

    // Arpeggiator
    Krate::DSP::ArpeggiatorCore arpCore_;
    std::array<Krate::DSP::ArpEvent, 128> arpEvents_{};
    std::array<Krate::DSP::ArpEvent, 512> combinedEvents_{};  // arp + delay echoes
    ArpeggiatorParams arpParams_;

    // MIDI delay post-processor (echo scheduling; lane tracking is inside arpCore_)
    Krate::DSP::MidiNoteDelay midiDelay_;

    // Previous arp param values — only call setters that reset internal state
    // when the value actually changes.
    Krate::DSP::ArpMode prevArpMode_{Krate::DSP::ArpMode::Up};
    int prevArpOctaveRange_{1};
    Krate::DSP::OctaveMode prevArpOctaveMode_{Krate::DSP::OctaveMode::Sequential};
    int prevArpNoteValue_{10};
    Krate::DSP::LatchMode prevArpLatchMode_{Krate::DSP::LatchMode::Off};
    Krate::DSP::ArpRetriggerMode prevArpRetrigger_{Krate::DSP::ArpRetriggerMode::Off};
    // Spec 142: track previous source mode so we can detect toggle edges and
    // emit a panic note-off for any sounding programmed note.
    int prevSourceMode_{0};  // 0 = Live (matches ArpeggiatorParams::sourceMode default)

    // Set on setActive(false) when note-off obligations were recorded for the
    // first process() after reactivation to discharge; see setActive().
    bool pendingDeactivateFlush_{false};

    // Transport state
    bool wasTransportPlaying_{false};
    double prevProjectTimeMusic_{-1.0};

    // Audition voice pool. VoiceAllocator owns the note-to-voice routing and
    // the stealing policy; the voices themselves hold no note identity.
    std::array<AuditionVoice, kAuditionVoiceCount> auditionVoices_{};
    Krate::DSP::VoiceAllocator auditionAllocator_;

    /// Act on a VoiceAllocator event span (note-on / note-off / hard steal).
    void dispatchAuditionEvents(
        std::span<const Krate::DSP::VoiceEvent> events) noexcept;
    /// Silence every audition voice and return the allocator to all-idle.
    void resetAuditionVoices() noexcept;
    [[nodiscard]] bool anyAuditionVoiceActive() const noexcept;

    // Audition sound params
    std::atomic<bool>  auditionEnabled_{false};
    std::atomic<float> auditionVolume_{0.7f};
    std::atomic<int>   auditionWaveform_{0};
    std::atomic<float> auditionDecay_{200.0f};

    double sampleRate_ = 44100.0;
    Steinberg::int32 maxBlockSize_ = 512;
};

} // namespace Gradus
