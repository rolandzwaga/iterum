// ==============================================================================
// Audio Processor Implementation
// ==============================================================================
// Split into multiple .cpp files for maintainability:
//   processor.cpp          - Lifecycle, setup, process(), small helpers
//   processor_params.cpp   - processParameterChanges()
//   processor_state.cpp    - getState() / setState()
//   processor_midi.cpp     - MIDI event handling, note on/off, pitch bend
//   processor_messages.cpp - IMessage notify(), sendDisplayData()
// ==============================================================================

#include "processor.h"
#include "dsp/dual_stft_config.h"
#include "dsp/physical_model_mixer.h"

#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstevents.h"

#include <krate/dsp/core/db_utils.h>  // detail::isFinite (fast-math-immune)
#include <krate/dsp/core/math_constants.h>
#include <krate/dsp/core/midi_utils.h>
#include <krate/dsp/core/note_value.h>
#include <krate/dsp/core/pitch_utils.h>
#include <krate/dsp/core/scoped_denormal_mode.h> // WI-17: FTZ/DAZ guard for process()
#include <krate/dsp/core/sigmoid.h>

#include "display/shared_display_bridge.h"
#include "display/display_bridge_log.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

#include <random>
#include <utility>

namespace Innexus {

// ==============================================================================
// Constructor / Destructor
// ==============================================================================
Processor::Processor() // NOLINT(cppcoreguidelines-pro-type-member-init) -- sampleAnalyzer_ is default-constructed; clang-tidy can't resolve its include
{
    setControllerClass(kControllerUID);

    // Generate unique instance ID for SharedDisplayBridge
    std::random_device rd;
    std::mt19937_64 gen(rd());
    instanceId_ = gen();
}

Processor::~Processor()
{
    // Clean up analysis pointers (SC-010: all deletion happens here, never on audio thread)
    auto* current = currentAnalysis_.load(std::memory_order_acquire);
    if (current) {
        delete current;
        currentAnalysis_.store(nullptr, std::memory_order_release);
    }
    delete pendingDeletion_;
    pendingDeletion_ = nullptr;
}

// ==============================================================================
// Initialize
// ==============================================================================
Steinberg::tresult PLUGIN_API Processor::initialize(Steinberg::FUnknown* context)
{
    auto result = AudioEffect::initialize(context);
    if (result != Steinberg::kResultOk)
        return result;

    // MIDI event input
    addEventInput(STR16("Event In"), 1);

    // Stereo audio output
    addAudioOutput(STR16("Stereo Out"), Steinberg::Vst::SpeakerArr::kStereo);

    // Spec 124 FR-012: Explicitly set hard retrigger mode for ADSR envelope
    voice_.adsr.setRetriggerMode(Krate::DSP::RetriggerMode::Hard);

    // Sidechain audio input (FR-001: auxiliary stereo bus, NOT default-active)
    // Must be inactive by default so AU wrapper can initialize without inputs.
    // Host activates this bus when user routes audio to the sidechain.
    addAudioInput(
        STR16("Sidechain"),
        Steinberg::Vst::SpeakerArr::kStereo,
        Steinberg::Vst::BusTypes::kAux,
        0 /* not default active */);

    // Register in SharedDisplayBridge (Tier 3 fallback)
    Krate::Plugins::SharedDisplayBridge::instance().registerInstance(
        instanceId_, &sharedDisplay_);

    KRATE_BRIDGE_LOG("Innexus::Processor::initialize() — id=0x%llx",
        static_cast<unsigned long long>(instanceId_));

    return Steinberg::kResultOk;
}

// ==============================================================================
// Terminate
// ==============================================================================
Steinberg::tresult PLUGIN_API Processor::terminate()
{
    KRATE_BRIDGE_LOG("Innexus::Processor::terminate() — id=0x%llx",
        static_cast<unsigned long long>(instanceId_));
    Krate::Plugins::SharedDisplayBridge::instance().unregisterInstance(instanceId_);

    sampleAnalyzer_.cancel();

    // SC-010: Clean up any pending deletion when deactivating
    cleanupPendingDeletion();

    return AudioEffect::terminate();
}

// ==============================================================================
// Cleanup Pending Deletion (SC-010: safe non-audio-thread deletion)
// ==============================================================================
void Processor::cleanupPendingDeletion()
{
    delete pendingDeletion_;
    pendingDeletion_ = nullptr;
}

// ==============================================================================
// Set Active
// ==============================================================================
Steinberg::tresult PLUGIN_API Processor::setActive(Steinberg::TBool state)
{
    KRATE_BRIDGE_LOG("Innexus::Processor::setActive(%s)",
        state ? "true" : "false");
    if (state)
    {
        // SC-010: Clean up any pending deletions from previous activation cycle
        cleanupPendingDeletion();

        // Prepare all voices (oscillator banks + residual synths)
        {
            const SampleAnalysis* analysis =
                currentAnalysis_.load(std::memory_order_acquire);
            const size_t fftSize =
                (analysis && !analysis->residualFrames.empty() &&
                 analysis->analysisFFTSize > 0)
                    ? analysis->analysisFFTSize
                    : kShortWindowConfig.fftSize;
            const size_t hopSize =
                (analysis && !analysis->residualFrames.empty() &&
                 analysis->analysisHopSize > 0)
                    ? analysis->analysisHopSize
                    : kShortWindowConfig.hopSize;

            for (size_t vi = 0; vi < voices_.size(); ++vi)
            {
                auto& voice = voices_[vi];
                voice.oscillatorBank.prepare(sampleRate_);
                voice.residualSynth.prepare(
                    fftSize, hopSize, static_cast<float>(sampleRate_));
                // Spec 128: Prepare impact exciter per voice
                voice.impactExciter.prepare(sampleRate_, static_cast<uint32_t>(vi));
                // Spec 130: Prepare bow exciter per voice
                voice.bowExciter.prepare(sampleRate_);
                // Spec 129: Prepare waveguide string per voice
                voice.waveguideString.prepare(sampleRate_);
                voice.waveguideString.prepareVoice(static_cast<uint32_t>(vi));
                // Spec 131: Prepare body resonance per voice
                voice.bodyResonance.prepare(sampleRate_);
            }
        }

        // Configure parameter smoothers (FR-025: 5ms time constant)
        harmonicLevelSmoother_.configure(5.0f, static_cast<float>(sampleRate_));
        harmonicLevelSmoother_.snapTo(1.0f); // default plain value
        residualLevelSmoother_.configure(5.0f, static_cast<float>(sampleRate_));
        residualLevelSmoother_.snapTo(0.3f); // default plain value
        brightnessSmoother_.configure(5.0f, static_cast<float>(sampleRate_));
        brightnessSmoother_.snapTo(0.0f); // default plain value (neutral)
        transientEmphasisSmoother_.configure(5.0f, static_cast<float>(sampleRate_));
        transientEmphasisSmoother_.snapTo(0.0f); // default plain value (no boost)

        // Compute per-voice timing constants and reset all voices
        {
            const int freezeRecovLen = std::max(1, static_cast<int>(
                kFreezeRecoveryTimeSec * static_cast<float>(sampleRate_)));
            const int antiClickLen = std::max(1, static_cast<int>(
                kAntiClickTimeSec * static_cast<float>(sampleRate_)));

            for (auto& voice : voices_)
            {
                voice.freezeRecoveryLengthSamples = freezeRecovLen;
                voice.spectralDecay.deactivate();
                voice.antiClickLengthSamples = antiClickLen;
                voice.reset();
            }
        }

        // Reset manual freeze state (M4)
        manualFreezeActive_ = false;
        manualFreezeRecoverySamplesRemaining_ = 0;
        manualFreezeRecoveryOldLevel_ = 0.0f;
        previousFreezeState_ = freeze_.load(std::memory_order_relaxed) > 0.5f;

        // Spec 124: Prepare ADSR envelope and amount smoother for all voices
        for (auto& voice : voices_)
        {
            voice.adsr.prepare(static_cast<float>(sampleRate_));
            voice.adsr.reset();
            voice.adsr.setRetriggerMode(Krate::DSP::RetriggerMode::Hard);
            voice.adsrAmountSmoother.configure(15.0f, static_cast<float>(sampleRate_));
            voice.adsrAmountSmoother.snapTo(adsrAmount_.load(std::memory_order_relaxed));
        }

        // Spec A: Snap harmonic physics smoothers and reset physics state
        warmthSmoother_.snapTo(warmth_.load(std::memory_order_relaxed));
        couplingSmoother_.snapTo(coupling_.load(std::memory_order_relaxed));
        stabilitySmoother_.snapTo(stability_.load(std::memory_order_relaxed));
        entropySmoother_.snapTo(entropy_.load(std::memory_order_relaxed));
        harmonicPhysics_.reset();

        // Reset sidechain crossfade state
        sourceCrossfadeSamplesRemaining_ = 0;
        sourceCrossfadeOldLevel_ = 0.0f;
        previousInputSource_ =
            inputSource_.load(std::memory_order_relaxed) > 0.5f ? 1 : 0;

        // Spec B FR-005, FR-018: Reset feedback buffer on activation
        feedbackBuffer_.fill(0.0f);
        previousFreezeForFeedback_ = freeze_.load(std::memory_order_relaxed) > 0.5f;

        // DataExchange: open queue for display data transfer
        if (dataExchange_)
            dataExchange_->onActivate(processSetup);
    }
    else
    {
        // DataExchange: close queue before deactivation
        if (dataExchange_)
            dataExchange_->onDeactivate();

        for (auto& voice : voices_)
        {
            voice.oscillatorBank.reset();
            voice.residualSynth.reset();
        }

        // SC-010: Clean up any pending deletions when deactivating
        cleanupPendingDeletion();
    }

    return AudioEffect::setActive(state);
}

// ==============================================================================
// Setup Processing
// ==============================================================================
Steinberg::tresult PLUGIN_API Processor::setupProcessing(
    Steinberg::Vst::ProcessSetup& newSetup)
{
    sampleRate_ = newSetup.sampleRate;
    displaySendIntervalSamples_ = 0; // recompute on next send

    // NOTE: denormal (FTZ/DAZ) control used to live here. MXCSR is per-thread,
    // so setting it on the setup thread never reached the audio thread; it is
    // now a ScopedDenormalMode at the top of process(). Do not re-add it here.

    // FR-011: source crossfade length = 20ms in samples
    sourceCrossfadeLengthSamples_ = static_cast<int>(
        0.020 * sampleRate_);
    sourceCrossfadeLengthSamples_ = std::max(sourceCrossfadeLengthSamples_, 1);

    // M4 FR-006: Manual freeze recovery crossfade length = 10ms in samples
    manualFreezeRecoveryLengthSamples_ = static_cast<int>(
        std::round(kManualFreezeRecoveryTimeSec * static_cast<float>(sampleRate_)));
    manualFreezeRecoveryLengthSamples_ = std::max(manualFreezeRecoveryLengthSamples_, 1);

    // M4 FR-017: Configure morph position smoother (7ms time constant)
    morphPositionSmoother_.configure(7.0f, static_cast<float>(sampleRate_));
    morphPositionSmoother_.snapTo(
        morphPosition_.load(std::memory_order_relaxed));

    // M4: Initialize filter mask to all-pass (1.0)
    filterMask_.fill(1.0f);
    currentFilterType_ = 0;

    // M6: Configure stereo/detune smoothers
    const float sr = static_cast<float>(sampleRate_);
    stereoSpreadSmoother_.configure(10.0f, sr);
    stereoSpreadSmoother_.snapTo(stereoSpread_.load(std::memory_order_relaxed));
    evolutionSpeedSmoother_.configure(5.0f, sr);
    evolutionSpeedSmoother_.snapTo(evolutionSpeed_.load(std::memory_order_relaxed));
    evolutionDepthSmoother_.configure(5.0f, sr);
    evolutionDepthSmoother_.snapTo(evolutionDepth_.load(std::memory_order_relaxed));
    mod1RateSmoother_.configure(5.0f, sr);
    mod1RateSmoother_.snapTo(mod1Rate_.load(std::memory_order_relaxed));
    mod1DepthSmoother_.configure(5.0f, sr);
    mod1DepthSmoother_.snapTo(mod1Depth_.load(std::memory_order_relaxed));
    mod2RateSmoother_.configure(5.0f, sr);
    mod2RateSmoother_.snapTo(mod2Rate_.load(std::memory_order_relaxed));
    mod2DepthSmoother_.configure(5.0f, sr);
    mod2DepthSmoother_.snapTo(mod2Depth_.load(std::memory_order_relaxed));
    detuneSpreadSmoother_.configure(5.0f, sr);
    detuneSpreadSmoother_.snapTo(detuneSpread_.load(std::memory_order_relaxed));
    for (auto& smoother : blendWeightSmootherArray_) {
        smoother.configure(5.0f, sr);
        smoother.snapTo(0.0f);
    }

    // Spec A: Configure harmonic physics smoothers and prepare physics processor
    warmthSmoother_.configure(5.0f, sr);
    warmthSmoother_.snapTo(warmth_.load(std::memory_order_relaxed));
    couplingSmoother_.configure(5.0f, sr);
    couplingSmoother_.snapTo(coupling_.load(std::memory_order_relaxed));
    stabilitySmoother_.configure(5.0f, sr);
    stabilitySmoother_.snapTo(stability_.load(std::memory_order_relaxed));
    entropySmoother_.configure(5.0f, sr);
    entropySmoother_.snapTo(entropy_.load(std::memory_order_relaxed));
    harmonicPhysics_.prepare(sampleRate_, 512); // default hop size

    // M6 FR-014: Prepare evolution engine
    evolutionEngine_.prepare(sampleRate_);
    evolutionEngine_.updateWaypoints(memorySlots_);

    // M6 FR-024, FR-051: Prepare harmonic modulators (phase init to 0.0)
    mod1_.prepare(sampleRate_);
    mod2_.prepare(sampleRate_);

    // FR-003/FR-005: Prepare live analysis pipeline
    auto currentMode = latencyMode_.load(std::memory_order_relaxed) > 0.5f
        ? LatencyMode::HighPrecision
        : LatencyMode::LowLatency;
    liveAnalysis_.prepare(sampleRate_, currentMode);

    // Prepare spectral decay envelope for frozen frame fade-out
    voice_.spectralDecay.prepare(sampleRate_, static_cast<size_t>(
        newSetup.maxSamplesPerBlock > 0 ? newSetup.maxSamplesPerBlock : 512));

    // Spec 132: Prepare sympathetic resonance (global, non-per-voice)
    sympatheticResonance_.prepare(sampleRate_);

    return AudioEffect::setupProcessing(newSetup);
}

// ==============================================================================
// Process (T082: Main audio processing -- FR-047 to FR-058)
// ==============================================================================
Steinberg::tresult PLUGIN_API Processor::process(Steinberg::Vst::ProcessData& data)
{
    // WI-17: flush denormals to zero for this block. Long exponential decay
    // tails (ADSR release, residual overlap-add, sympathetic resonance) drift
    // into denormal range where x86 traps cost hundreds of cycles per operation.
    //
    // Must be set HERE and not in setupProcessing(): MXCSR is per-thread state,
    // and hosts call setupProcessing() on the main thread while process() runs
    // on a dedicated audio thread (some rotate plugins across a render pool).
    // The guard restores the host's FP environment on scope exit.
    const Krate::DSP::ScopedDenormalMode denormalGuard;

    // --- Handle parameter changes ---
    processParameterChanges(data.inputParameterChanges);

    // Cache host tempo for modulator sync
    if (data.processContext &&
        (data.processContext->state & Steinberg::Vst::ProcessContext::kTempoValid)) {
        tempoBPM_ = data.processContext->tempo;
    }

    // --- Check for new analysis from background thread (FR-058) ---
    checkForNewAnalysis();


    // --- Forward responsiveness to live analysis pipeline (FR-030, FR-031) ---
    {
        float resp = responsiveness_.load(std::memory_order_relaxed);
        liveAnalysis_.setResponsiveness(resp);
    }

    // --- Detect input source switch and trigger crossfade (FR-011) ---
    {
        int currentSource = inputSource_.load(std::memory_order_relaxed) > 0.5f ? 1 : 0;
        if (currentSource != previousInputSource_)
        {
            // Source changed -- initiate crossfade
            if (voice_.active) {
                float captL = 0.0f;
                float captR = 0.0f;
                voice_.oscillatorBank.processStereo(captL, captR);
                sourceCrossfadeOldLevel_ = (captL + captR) * 0.5f;
            } else {
                sourceCrossfadeOldLevel_ = 0.0f;
            }
            sourceCrossfadeSamplesRemaining_ = sourceCrossfadeLengthSamples_;
            previousInputSource_ = currentSource;
            // Note: residualSynth_ is always prepared in setActive() for sidechain mode.
            // No prepare() call here to avoid heap allocation on audio thread (FR-008).
        }
    }

    // --- Sidechain downmix to mono (FR-001, R-002) ---
    const float* sidechainMono = nullptr;
    bool newLiveFrameThisBlock = false;
    {
        int currentSource = inputSource_.load(std::memory_order_relaxed) > 0.5f ? 1 : 0;
        if (currentSource == 1 && data.numInputs > 0 && data.inputs)
        {
            const auto& scBus = data.inputs[0];
            if (scBus.numChannels >= 2 &&
                scBus.channelBuffers32 &&
                scBus.channelBuffers32[0] && scBus.channelBuffers32[1])
            {
                // Stereo downmix
                auto count = std::min(data.numSamples,
                    static_cast<Steinberg::int32>(sidechainBuffer_.size()));
                for (Steinberg::int32 s = 0; s < count; ++s)
                {
                    sidechainBuffer_[static_cast<size_t>(s)] =
                        (scBus.channelBuffers32[0][s] +
                         scBus.channelBuffers32[1][s]) * 0.5f;
                }
                sidechainMono = sidechainBuffer_.data();
            }
            else if (scBus.numChannels == 1 &&
                     scBus.channelBuffers32 && scBus.channelBuffers32[0])
            {
                sidechainMono = scBus.channelBuffers32[0];
            }
        }
    }
    // Feed sidechain audio to live analysis pipeline (FR-003, FR-009)
    {
        int currentSource = inputSource_.load(std::memory_order_relaxed) > 0.5f ? 1 : 0;
        if (currentSource == 1 && sidechainMono != nullptr)
        {
            // Spec B FR-001, FR-003, FR-009, FR-014, FR-015: Mix feedback into
            // sidechain input before pushing to analysis pipeline.
            const float fbAmount = feedbackAmount_.load(std::memory_order_relaxed);
            const bool manualFrozen = freeze_.load(std::memory_order_relaxed) > 0.5f;

            if (fbAmount > 0.0f && !manualFrozen)
            {
                // If sidechainMono points to raw bus data (mono case), copy to
                // sidechainBuffer_ first so we can modify in-place.
                if (sidechainMono != sidechainBuffer_.data())
                {
                    auto count = std::min(data.numSamples,
                        static_cast<Steinberg::int32>(sidechainBuffer_.size()));
                    std::memcpy(sidechainBuffer_.data(), sidechainMono,
                        static_cast<size_t>(count) * sizeof(float));
                    sidechainMono = sidechainBuffer_.data();
                }

                // FR-001, FR-009: Per-sample soft-limited feedback mixing
                auto count = std::min(data.numSamples,
                    static_cast<Steinberg::int32>(sidechainBuffer_.size()));
                for (Steinberg::int32 s = 0; s < count; ++s)
                {
                    const float fbSample = std::tanh(
                        feedbackBuffer_[static_cast<size_t>(s)]
                        * fbAmount * 2.0f) * 0.5f;
                    sidechainBuffer_[static_cast<size_t>(s)] =
                        sidechainBuffer_[static_cast<size_t>(s)]
                        * (1.0f - fbAmount) + fbSample;
                }
            }

            // T091: Skip spectral coring when residual level is zero (~10% CPU reduction)
            const bool residualActive =
                residualLevel_.load(std::memory_order_relaxed) > 0.0f;
            liveAnalysis_.setResidualEnabled(residualActive);

            liveAnalysis_.pushSamples(
                sidechainMono,
                static_cast<size_t>(data.numSamples));

            // Consume new frames if available
            newLiveFrameThisBlock = liveAnalysis_.hasNewFrame();
            if (newLiveFrameThisBlock)
            {
                currentLiveFrame_ = liveAnalysis_.consumeFrame();
                currentLiveResidualFrame_ = liveAnalysis_.consumeResidualFrame();
                currentLivePolyFrame_ = liveAnalysis_.consumePolyphonicFrame();
            }
        }
    }

    // --- Read current analysis with acquire semantics (FR-058) ---
    const SampleAnalysis* analysis =
        currentAnalysis_.load(std::memory_order_acquire);

    // WI-9: ADSR playback state is published as copied scalars on the per-block
    // DisplayData bridge (see sendDisplayData). It used to be sent here as raw
    // pointers to processor-owned atomics via IMessage, which the controller
    // dereferenced on the UI thread -- a latent use-after-free on teardown, and
    // invalid whenever the host runs the controller in a separate process.

    // --- Spec 124: ADSR envelope setup (MUST happen before MIDI events) ---
    // Curve setters enable table processing mode. If called AFTER gate(true),
    // the phase increment is never initialized for the table path, producing
    // a stuck envelope. By updating ADSR parameters before processEvents,
    // enterAttack() sees the correct useTableProcessing_ state.
    const float adsrAmountTarget = adsrAmount_.load(std::memory_order_relaxed);
    const bool adsrActive = (adsrAmountTarget > 0.0f) ||
        (voice_.adsrAmountSmoother.getCurrentValue() > 1e-7f);

    if (adsrActive)
    {
        // Update envelope parameters from atomics
        const float timeScale = adsrTimeScale_.load(std::memory_order_relaxed);

        // Effective times = param * scale, clamped to [1, 5000]ms per segment
        float effAttack = std::clamp(
            adsrAttackMs_.load(std::memory_order_relaxed) * timeScale, 1.0f, 5000.0f);
        float effDecay = std::clamp(
            adsrDecayMs_.load(std::memory_order_relaxed) * timeScale, 1.0f, 5000.0f);
        float effRelease = std::clamp(
            adsrReleaseMs_.load(std::memory_order_relaxed) * timeScale, 1.0f, 5000.0f);

        const float sustainLevel = adsrSustainLevel_.load(std::memory_order_relaxed);
        const float attackCurve = adsrAttackCurve_.load(std::memory_order_relaxed);
        const float decayCurve = adsrDecayCurve_.load(std::memory_order_relaxed);
        const float releaseCurve = adsrReleaseCurve_.load(std::memory_order_relaxed);

        // Update ADSR parameters for all voices
        for (auto& voice : voices_)
        {
            voice.adsr.setAttack(effAttack);
            voice.adsr.setDecay(effDecay);
            voice.adsr.setSustain(sustainLevel);
            voice.adsr.setRelease(effRelease);
            voice.adsr.setAttackCurve(attackCurve);
            voice.adsr.setDecayCurve(decayCurve);
            voice.adsr.setReleaseCurve(releaseCurve);
            voice.adsrAmountSmoother.setTarget(adsrAmountTarget);
        }
    }

    // --- Process MIDI events ---
    processEvents(data.inputEvents);

    // --- Output ---
    if (data.numOutputs < 1 || !data.outputs)
        return Steinberg::kResultOk;
    const int numOutputChannels = data.outputs[0].numChannels;
    if (numOutputChannels < 1)
        return Steinberg::kResultOk;

    auto numSamples = data.numSamples;
    if (numSamples <= 0)
        return Steinberg::kResultOk;

    auto** out = data.outputs[0].channelBuffers32;
    if (!out || !out[0] || (numOutputChannels >= 2 && !out[1]))
        return Steinberg::kResultOk;


    // Determine current input source
    const int currentInputSource =
        inputSource_.load(std::memory_order_relaxed) > 0.5f ? 1 : 0;
    const bool isSidechainMode = (currentInputSource == 1);

    // FR-055: If no analysis loaded (sample mode) or no note active, output silence
    // In sidechain mode, we can synthesize even without a loaded sample analysis
    // as long as a note is active and the live pipeline has produced a frame.
    const bool hasSampleAnalysis = (analysis != nullptr) && !analysis->frames.empty();
    // A live frame is "available" if we have a valid pitch, OR we received
    // a noise-gated frame (signal was present but now silent — need to enter
    // confidence gate to trigger freeze + spectral decay), OR we're in an
    // active spectral decay.
    const bool hasLiveFrame = isSidechainMode &&
        (currentLiveFrame_.f0 > 0.0f ||
         (newLiveFrameThisBlock && currentLiveFrame_.f0Confidence == 0.0f &&
          voice_.lastGoodFrame.f0 > 0.0f) ||
         (voice_.isFrozen && voice_.spectralDecay.isActive()));


    // =========================================================================
    // M5: Recall Trigger Detection (FR-011, FR-012, FR-013, FR-015)
    // Fires at block start, BEFORE capture detection (T057).
    // Must run before the early-return so recall works even with no note/analysis.
    // =========================================================================
    {
        const float currentRecallTrigger =
            memoryRecall_.load(std::memory_order_relaxed);

        if (currentRecallTrigger > 0.5f && previousRecallTrigger_ <= 0.5f)
        {
            // Read selected slot index
            const int slot = std::clamp(
                static_cast<int>(std::round(
                    memorySlot_.load(std::memory_order_relaxed) * 7.0f)),
                0, 7);

            // FR-013: Silently ignore recall on empty slot
            if (memorySlots_[static_cast<size_t>(slot)].occupied)
            {
                // FR-012: Reconstruct frame from snapshot
                Krate::DSP::HarmonicFrame tempHarmonicFrame{};
                Krate::DSP::ResidualFrame tempResidualFrame{};
                Krate::DSP::recallSnapshotToFrame(
                    memorySlots_[static_cast<size_t>(slot)].snapshot,
                    tempHarmonicFrame, tempResidualFrame);

                // FR-015: If already frozen (slot-to-slot recall), initiate crossfade
                if (manualFreezeActive_)
                {
                    if (voice_.active) {
                        float captL = 0.0f;
                        float captR = 0.0f;
                        voice_.oscillatorBank.processStereo(captL, captR);
                        manualFreezeRecoveryOldLevel_ = (captL + captR) * 0.5f;
                    } else {
                        manualFreezeRecoveryOldLevel_ = 0.0f;
                    }
                    manualFreezeRecoverySamplesRemaining_ =
                        manualFreezeRecoveryLengthSamples_;
                }

                // FR-012: Load into freeze frame and engage freeze
                manualFrozenFrame_ = tempHarmonicFrame;
                manualFrozenResidualFrame_ = tempResidualFrame;
                manualFreezeActive_ = true;

                // Sync freeze state so the M4 freeze detection block
                // correctly handles disengage when the user toggles
                // the Freeze parameter off (FR-018).
                freeze_.store(1.0f, std::memory_order_relaxed);
                previousFreezeState_ = true;

                // Spec 124 FR-015: Restore all 9 ADSR values from slot
                {
                    const auto& ms = memorySlots_[static_cast<size_t>(slot)];
                    adsrAttackMs_.store(ms.adsrAttackMs, std::memory_order_relaxed);
                    adsrDecayMs_.store(ms.adsrDecayMs, std::memory_order_relaxed);
                    adsrSustainLevel_.store(ms.adsrSustainLevel, std::memory_order_relaxed);
                    adsrReleaseMs_.store(ms.adsrReleaseMs, std::memory_order_relaxed);
                    adsrAmount_.store(ms.adsrAmount, std::memory_order_relaxed);
                    adsrTimeScale_.store(ms.adsrTimeScale, std::memory_order_relaxed);
                    adsrAttackCurve_.store(ms.adsrAttackCurve, std::memory_order_relaxed);
                    adsrDecayCurve_.store(ms.adsrDecayCurve, std::memory_order_relaxed);
                    adsrReleaseCurve_.store(ms.adsrReleaseCurve, std::memory_order_relaxed);

                    // Send IMessage to Controller to update ADSR knob positions
                    auto* msg = allocateMessage();
                    if (msg)
                    {
                        msg->setMessageID("RecalledADSR");
                        auto* attrs = msg->getAttributes();
                        if (attrs)
                        {
                            attrs->setFloat("attackMs",
                                static_cast<double>(ms.adsrAttackMs));
                            attrs->setFloat("decayMs",
                                static_cast<double>(ms.adsrDecayMs));
                            attrs->setFloat("sustainLevel",
                                static_cast<double>(ms.adsrSustainLevel));
                            attrs->setFloat("releaseMs",
                                static_cast<double>(ms.adsrReleaseMs));
                            attrs->setFloat("amount",
                                static_cast<double>(ms.adsrAmount));
                            attrs->setFloat("timeScale",
                                static_cast<double>(ms.adsrTimeScale));
                            attrs->setFloat("attackCurve",
                                static_cast<double>(ms.adsrAttackCurve));
                            attrs->setFloat("decayCurve",
                                static_cast<double>(ms.adsrDecayCurve));
                            attrs->setFloat("releaseCurve",
                                static_cast<double>(ms.adsrReleaseCurve));
                        }
                        sendMessage(msg);
                        msg->release();
                    }
                }
            }

            // FR-011: Auto-reset trigger
            memoryRecall_.store(0.0f, std::memory_order_relaxed);

            // Notify host UI of the auto-reset (T059b)
            if (data.outputParameterChanges)
            {
                Steinberg::int32 index = 0;
                auto* queue = data.outputParameterChanges->addParameterData(
                    kMemoryRecallId, index);
                if (queue)
                    queue->addPoint(0, 0.0, index);
            }
        }

        previousRecallTrigger_ = currentRecallTrigger;
    }


    // =========================================================================
    // M5: Capture Trigger Detection (FR-006, FR-007, FR-008, FR-009)
    // Fires at block start, BEFORE any filter application (pre-filter capture).
    // Must run before the early-return so capture works even with no note/analysis.
    // =========================================================================
    {
        const float currentCaptureTrigger =
            memoryCapture_.load(std::memory_order_relaxed);

        if (currentCaptureTrigger > 0.5f && previousCaptureTrigger_ <= 0.5f)
        {
            // Determine capture source (FR-007)
            Krate::DSP::HarmonicFrame captureFrame{};
            Krate::DSP::ResidualFrame captureResidual{};

            const float smoothedMorph =
                morphPositionSmoother_.getCurrentValue();

            if (manualFreezeActive_ && smoothedMorph > 1e-6f)
            { // NOLINT(bugprone-branch-clone) branches assign from different sources
                // (a) Freeze active + morph > 0: capture post-morph blended state (FR-008)
                // voice_.morphedFrame contains the last-computed pre-filter morph result
                captureFrame = voice_.morphedFrame;
                captureResidual = voice_.morphedResidualFrame;
            }
            else if (manualFreezeActive_)
            {
                // (b) Freeze active + morph == 0: capture frozen frame
                captureFrame = manualFrozenFrame_;
                captureResidual = manualFrozenResidualFrame_;
            }
            else if (isSidechainMode)
            {
                // (c) Sidechain mode, no freeze: capture live analysis frame
                captureFrame = currentLiveFrame_;
                captureResidual = currentLiveResidualFrame_;
            }
            else if (hasSampleAnalysis)
            {
                // (d) Sample mode, no freeze: capture current sample frame
                captureFrame = analysis->getFrame(voice_.currentFrameIndex);
                if (!analysis->residualFrames.empty())
                    captureResidual =
                        analysis->getResidualFrame(voice_.currentFrameIndex);
            }
            // else: (e) No analysis -- captureFrame/captureResidual are default (empty)

            // Store snapshot in selected slot (FR-010)
            const int slot = std::clamp(
                static_cast<int>(std::round(
                    memorySlot_.load(std::memory_order_relaxed) * 7.0f)),
                0, 7);
            memorySlots_[static_cast<size_t>(slot)].snapshot =
                Krate::DSP::captureSnapshot(captureFrame, captureResidual);
            memorySlots_[static_cast<size_t>(slot)].occupied = true;

            // Spec 124 FR-014: Store all 9 ADSR parameter values into the slot
            {
                auto& ms = memorySlots_[static_cast<size_t>(slot)];
                ms.adsrAttackMs = adsrAttackMs_.load(std::memory_order_relaxed);
                ms.adsrDecayMs = adsrDecayMs_.load(std::memory_order_relaxed);
                ms.adsrSustainLevel = adsrSustainLevel_.load(std::memory_order_relaxed);
                ms.adsrReleaseMs = adsrReleaseMs_.load(std::memory_order_relaxed);
                ms.adsrAmount = adsrAmount_.load(std::memory_order_relaxed);
                ms.adsrTimeScale = adsrTimeScale_.load(std::memory_order_relaxed);
                ms.adsrAttackCurve = adsrAttackCurve_.load(std::memory_order_relaxed);
                ms.adsrDecayCurve = adsrDecayCurve_.load(std::memory_order_relaxed);
                ms.adsrReleaseCurve = adsrReleaseCurve_.load(std::memory_order_relaxed);
            }

            // M6: Update evolution waypoints after capture (FR-018)
            evolutionEngine_.updateWaypoints(memorySlots_);

            // Auto-reset trigger (FR-006)
            memoryCapture_.store(0.0f, std::memory_order_relaxed);

            // Notify host UI of the auto-reset so the Capture button
            // reflects the reset state (T042b)
            if (data.outputParameterChanges)
            {
                Steinberg::int32 index = 0;
                auto* queue = data.outputParameterChanges->addParameterData(
                    kMemoryCaptureId, index);
                if (queue)
                    queue->addPoint(0, 0.0, index);
            }
        }

        previousCaptureTrigger_ = currentCaptureTrigger;
    }

    // M4/T108: Freeze transition detection must happen BEFORE the early return
    // so that engaging freeze with no analysis still captures an empty frame.
    // This block runs only when the early return would fire; the main freeze
    // detection block handles the normal (has-analysis) case.
    {
        bool anyActive = false;
        for (const auto& v : voices_) { if (v.active) { anyActive = true; break; } }
        if (!anyActive || (!hasSampleAnalysis && !hasLiveFrame))
        {
        const bool currentFreezeState = freeze_.load(std::memory_order_relaxed) > 0.5f;

        if (currentFreezeState && !previousFreezeState_)
        {
            // Capture default-constructed empty frames
            manualFrozenFrame_ = {};
            manualFrozenResidualFrame_ = {};
            manualFreezeActive_ = true;
        }

        if (!currentFreezeState && previousFreezeState_)
        {
            manualFreezeRecoverySamplesRemaining_ = manualFreezeRecoveryLengthSamples_;
            manualFreezeActive_ = false;

            // Spec B FR-016: Clear feedback buffer on freeze disengage
            feedbackBuffer_.fill(0.0f);
        }

        previousFreezeState_ = currentFreezeState;
        }
    }

    {
        // Check if ANY voice is active (poly mode: voice 0 may be idle while others play)
        bool anyVoiceActive = false;
        for (const auto& v : voices_)
        {
            if (v.active) { anyVoiceActive = true; break; }
        }

        if (!anyVoiceActive || (!hasSampleAnalysis && !hasLiveFrame))
        {
            for (Steinberg::int32 s = 0; s < numSamples; ++s)
            {
                out[0][s] = 0.0f;
                if (numOutputChannels >= 2) out[1][s] = 0.0f;
            }
            data.outputs[0].silenceFlags = (numOutputChannels >= 2) ? 0x3 : 0x1;
            return Steinberg::kResultOk;
        }
    }

    // Hoist voice mode early (needed for all-voice operations below)
    const float voiceModeNormEarly = voiceMode_.load(std::memory_order_relaxed);
    const int voiceModeIdxEarly = std::clamp(
        static_cast<int>(std::round(voiceModeNormEarly * 2.0f)), 0, 2);
    constexpr int kVoiceCountsEarly[] = {1, 4, 8};
    const int maxVoicesEarly = kVoiceCountsEarly[voiceModeIdxEarly];

    // --- Update inharmonicity from parameter (all voices) ---
    {
        float inharm = inharmonicityAmount_.load(std::memory_order_relaxed);
        for (int vi = 0; vi < maxVoicesEarly; ++vi)
            voices_[static_cast<size_t>(vi)].oscillatorBank.setInharmonicityAmount(inharm);
    }

    // --- Active partial count from user parameter ---
    const int activePartialCount = getActivePartialCount();

    // --- Compute hop size in samples for frame advancement ---
    // Guard: analysis may be nullptr in sidechain mode (no sample loaded)
    const size_t hopSizeInSamples = analysis
        ? static_cast<size_t>(
            analysis->hopTimeSec * static_cast<float>(sampleRate_) + 0.5f)
        : 0;
    const size_t totalFrames = analysis ? analysis->totalFrames : 0;

    // --- Get master gain ---
    const float gain = masterGain_.load(std::memory_order_relaxed);

    // --- M2: Get residual parameters and set smoother targets (FR-025) ---
    const float harmLevelNorm = harmonicLevel_.load(std::memory_order_relaxed);
    const float resLevelNorm = residualLevel_.load(std::memory_order_relaxed);
    const float harmLevelPlain = harmLevelNorm * 2.0f;  // plain range 0-2
    const float resLevelPlain = resLevelNorm * 2.0f;    // plain range 0-2
    harmonicLevelSmoother_.setTarget(harmLevelPlain);
    residualLevelSmoother_.setTarget(resLevelPlain);

    // Brightness and transient emphasis: set smoother targets from atomics (FR-025)
    const float brightnessNorm = residualBrightness_.load(std::memory_order_relaxed);
    const float brightnessPlainTarget = brightnessNorm * 2.0f - 1.0f; // plain range -1 to +1
    const float transientEmpNorm = transientEmphasis_.load(std::memory_order_relaxed);
    const float transientEmpPlainTarget = transientEmpNorm * 2.0f;    // plain range 0 to 2
    brightnessSmoother_.setTarget(brightnessPlainTarget);
    transientEmphasisSmoother_.setTarget(transientEmpPlainTarget);

    // --- Spec 127: Physical model mix parameter (read once per block) ---
    // Material params (decay, brightness, stretch, scatter) are read in
    // broadcastFrameToVoices() and handleNoteOn() where they're applied.
    const float physModelMix = physModelMix_.load(std::memory_order_relaxed);

    // Spec 128: Load exciter type once per block
    const float exciterTypeNorm = exciterType_.load(std::memory_order_relaxed);
    const auto exciterType = static_cast<ExciterType>(
        std::clamp(static_cast<int>(std::round(exciterTypeNorm * 2.0f)), 0, 2));

    // Spec 129 FR-029: Read resonance type once per block for crossfade detection
    const float resTypeNorm = resonanceType_.load(std::memory_order_relaxed);
    const int targetResonanceType = std::clamp(
        static_cast<int>(std::round(resTypeNorm)), 0, 1);

    // Spec 131: Read body resonance params once per block
    const float bodySize = bodySize_.load(std::memory_order_relaxed);
    const float bodyMaterial = bodyMaterial_.load(std::memory_order_relaxed);
    const float bodyMix = bodyMix_.load(std::memory_order_relaxed);

    // Spec 132: Apply sympathetic resonance params once per block
    sympatheticResonance_.setAmount(
        sympatheticAmount_.load(std::memory_order_relaxed));
    sympatheticResonance_.setDecay(
        sympatheticDecay_.load(std::memory_order_relaxed));

    // Check if residual synthesis is available
    const bool hasSampleResidual = hasSampleAnalysis &&
                                    !analysis->residualFrames.empty() &&
                                    voice_.residualSynth.isPrepared();

    // In sidechain mode, live residual is always available if the pipeline produces it
    const bool hasLiveResidual = isSidechainMode && voice_.residualSynth.isPrepared();


    // =========================================================================
    // M4: Manual Freeze Detection (FR-001, FR-002, FR-003, FR-007, FR-008)
    // =========================================================================
    {
        const bool currentFreezeState = freeze_.load(std::memory_order_relaxed) > 0.5f;

        // Detect freeze engagement transition (off -> on)
        if (currentFreezeState && !previousFreezeState_)
        {
            // FR-002, FR-003: Capture current live frame as frozen state
            // In sidechain mode, use currentLiveFrame_/currentLiveResidualFrame_
            // In sample mode, use the current frame from analysis
            if (isSidechainMode)
            { // NOLINT(bugprone-branch-clone) branches assign from different sources
                manualFrozenFrame_ = currentLiveFrame_;
                manualFrozenResidualFrame_ = currentLiveResidualFrame_;
            }
            else if (hasSampleAnalysis)
            {
                manualFrozenFrame_ = analysis->getFrame(voice_.currentFrameIndex);
                if (!analysis->residualFrames.empty())
                    manualFrozenResidualFrame_ =
                        analysis->getResidualFrame(voice_.currentFrameIndex);
                else
                    manualFrozenResidualFrame_ = {};
            }
            else
            {
                // No analysis: capture empty/silent frames
                manualFrozenFrame_ = {};
                manualFrozenResidualFrame_ = {};
            }
            manualFreezeActive_ = true;
        }

        // Detect freeze disengage transition (on -> off)
        if (!currentFreezeState && previousFreezeState_)
        {
            // FR-006: Initiate 10ms crossfade from frozen to live
            // Capture current oscillator output level for smooth crossfade
            if (voice_.active) {
                float captL = 0.0f;
                float captR = 0.0f;
                voice_.oscillatorBank.processStereo(captL, captR);
                manualFreezeRecoveryOldLevel_ = (captL + captR) * 0.5f;
            } else {
                manualFreezeRecoveryOldLevel_ = 0.0f;
            }
            manualFreezeRecoverySamplesRemaining_ = manualFreezeRecoveryLengthSamples_;
            manualFreezeActive_ = false;

            // Spec B FR-016: Clear feedback buffer on freeze disengage
            feedbackBuffer_.fill(0.0f);
        }

        previousFreezeState_ = currentFreezeState;
    }

    // =========================================================================
    // M4: Harmonic Filter mask recomputation (FR-019 to FR-025, T081)
    // Recompute when filter type changes; mask is applied per-frame below.
    // =========================================================================
    {
        const float filterNorm = harmonicFilterType_.load(std::memory_order_relaxed);
        const int newFilterType = std::clamp(
            static_cast<int>(std::round(filterNorm * 4.0f)), 0, 4);

        if (newFilterType != currentFilterType_)
        {
            // Compute mask using a standard partials array with harmonicIndex = i+1.
            // Since analysis partials always follow this convention, the mask is
            // effectively a function of filterType alone and can be precomputed
            // independently of the current frame.
            std::array<Krate::DSP::Partial, Krate::DSP::kMaxPartials> stdPartials{};
            for (size_t i = 0; i < Krate::DSP::kMaxPartials; ++i)
                stdPartials[i].harmonicIndex = static_cast<int>(i) + 1;

            Krate::DSP::computeHarmonicMask(
                newFilterType, stdPartials,
                static_cast<int>(Krate::DSP::kMaxPartials), filterMask_);
            currentFilterType_ = newFilterType;
        }
    }


    // --- Spec A: Update harmonic physics smoothers ---
    warmthSmoother_.setTarget(warmth_.load(std::memory_order_relaxed));
    warmthSmoother_.advanceSamples(
        static_cast<size_t>(numSamples > 0 ? numSamples : 1));
    couplingSmoother_.setTarget(coupling_.load(std::memory_order_relaxed));
    couplingSmoother_.advanceSamples(
        static_cast<size_t>(numSamples > 0 ? numSamples : 1));
    stabilitySmoother_.setTarget(stability_.load(std::memory_order_relaxed));
    stabilitySmoother_.advanceSamples(
        static_cast<size_t>(numSamples > 0 ? numSamples : 1));
    entropySmoother_.setTarget(entropy_.load(std::memory_order_relaxed));
    entropySmoother_.advanceSamples(
        static_cast<size_t>(numSamples > 0 ? numSamples : 1));

    // M6: Read modulator enable flags (used in frame loading and per-sample loop)
    const bool mod1Enabled = mod1Enable_.load(std::memory_order_relaxed) > 0.5f;
    const bool mod2Enabled = mod2Enable_.load(std::memory_order_relaxed) > 0.5f;

    // =========================================================================
    // M4: Morph interpolation + load frames into osc bank
    // When freeze active: morph between frozen (A) and live (B) frames
    // When freeze inactive: pass-through live frame (FR-016)
    // (FR-004, FR-005, FR-007, FR-010 to FR-018)
    // =========================================================================
    if (manualFreezeActive_ && voice_.active)
    {
        // Update morph position smoother (FR-017)
        morphPositionSmoother_.setTarget(
            morphPosition_.load(std::memory_order_relaxed));

        // Determine which live frame to use as State B
        Krate::DSP::HarmonicFrame liveFrame{};
        Krate::DSP::ResidualFrame liveResidualFrame{};
        if (isSidechainMode)
        {
            liveFrame = currentLiveFrame_;
            liveResidualFrame = currentLiveResidualFrame_;
        }
        else if (hasSampleAnalysis)
        {
            liveFrame = analysis->getFrame(voice_.currentFrameIndex);
            if (!analysis->residualFrames.empty())
                liveResidualFrame = analysis->getResidualFrame(voice_.currentFrameIndex);
        }

        // Advance smoother by block size to get smoothed value (FR-017)
        // Use advanceSamples for correct convergence at block-rate processing
        morphPositionSmoother_.advanceSamples(
            static_cast<size_t>(numSamples > 0 ? numSamples : 1));
        const float smoothedMorph = morphPositionSmoother_.getCurrentValue();

        // Morph with early-out optimization (T061)
        if (smoothedMorph < 1e-6f)
        { // NOLINT(bugprone-branch-clone) branches assign from different sources
            // Fully frozen (morph = 0.0)
            voice_.morphedFrame = manualFrozenFrame_;
            voice_.morphedResidualFrame = manualFrozenResidualFrame_;
        }
        else if (smoothedMorph > 1.0f - 1e-6f)
        {
            // Fully live (morph = 1.0)
            voice_.morphedFrame = liveFrame;
            voice_.morphedResidualFrame = liveResidualFrame;
        }
        else
        {
            // Interpolate between frozen (A) and live (B)
            voice_.morphedFrame = Krate::DSP::lerpHarmonicFrame(
                manualFrozenFrame_, liveFrame, smoothedMorph);
            voice_.morphedResidualFrame = Krate::DSP::lerpResidualFrame(
                manualFrozenResidualFrame_, liveResidualFrame, smoothedMorph);
        }

        // M4: Apply harmonic filter mask (FR-020, FR-026, T082)
        // Filter is applied AFTER morph+blend and BEFORE oscillator bank.
        // Residual passes through unmodified (FR-027).
        if (currentFilterType_ != 0) // Skip for All-Pass (early-out)
            Krate::DSP::applyHarmonicMask(voice_.morphedFrame, filterMask_);

        // Load morphed+filtered frame into oscillator bank
        // M6 FR-025: Apply modulator amplitude modulation before loading
        applyModulatorAmplitude(mod1Enabled, mod2Enabled);
        applyHarmonicPhysics();
        broadcastFrameToVoices(activePartialCount, true);
    }

    // For sidechain mode: if a new live frame arrived this block, load it into
    // the oscillator bank and residual synth. This happens once per process() call
    // (not per-sample) since the live pipeline produces frames at STFT hop rate.
    // FR-007: Skip when manual freeze is active (manual takes priority)
    // A gated frame (f0=0, confidence=0) from the noise gate still enters this
    // block so the confidence gate can trigger freeze + spectral decay.
    const bool hasLiveF0 = currentLiveFrame_.f0 > 0.0f;
    const bool gotGatedFrame = newLiveFrameThisBlock && !hasLiveF0;
    if (!manualFreezeActive_ && isSidechainMode && (hasLiveF0 || gotGatedFrame))
    {
        // Apply confidence-gated freeze (FR-010, FR-052) to live frames
        float recoveryThreshold = voice_.isFrozen
            ? (kConfidenceThreshold + kConfidenceHysteresis)
            : kConfidenceThreshold;

        // During active spectral decay, also require sufficient input amplitude
        // to recover. This prevents background noise from interrupting the decay
        // with "spectral mush" frames.
        const bool amplitudeOk = !voice_.spectralDecay.isActive() ||
            currentLiveFrame_.globalAmplitude >=
                voice_.spectralDecay.initialAmplitude() * kDecayRecoveryAmplitudeRatio;

        if (currentLiveFrame_.f0Confidence >= recoveryThreshold && amplitudeOk)
        {
            if (voice_.isFrozen)
            {
                voice_.isFrozen = false;
                voice_.spectralDecay.deactivate();
                float captL = 0.0f;
                float captR = 0.0f;
                voice_.oscillatorBank.processStereo(captL, captR);
                voice_.freezeRecoveryOldLevel = (captL + captR) * 0.5f;
                voice_.freezeRecoverySamplesRemaining = voice_.freezeRecoveryLengthSamples;
            }
            voice_.lastGoodFrame = currentLiveFrame_;

            // M4: Store live frame as morphed (no freeze = pass-through)
            voice_.morphedFrame = currentLiveFrame_;
            voice_.morphedResidualFrame = currentLiveResidualFrame_;

            // Apply harmonic filter (FR-026, FR-027)
            if (currentFilterType_ != 0)
                Krate::DSP::applyHarmonicMask(voice_.morphedFrame, filterMask_);

            // M6 FR-025: Apply modulator amplitude modulation
            applyModulatorAmplitude(mod1Enabled, mod2Enabled);
            applyHarmonicPhysics();
            broadcastFrameToVoices(activePartialCount, true);
        }
        else
        {
            // FR-052: Hold last known-good frame
            bool wasFrozen = voice_.isFrozen;
            voice_.isFrozen = true;

            // Activate spectral decay on freeze transition.
            // Use lastGoodFrame — morphedFrame may already be zeroed by
            // the noise gate in the same block that triggers the freeze.
            if (!wasFrozen)
            {
                voice_.morphedFrame = voice_.lastGoodFrame;
                voice_.spectralDecay.activate(voice_.morphedFrame);
            }

            // Apply per-partial spectral decay to the held frame each block.
            // This produces a natural fade-out where higher partials die first.
            if (voice_.spectralDecay.isActive() && !voice_.spectralDecay.isFullyDecayed())
            {
                voice_.spectralDecay.processBlock(voice_.morphedFrame);

                // Re-load decayed frame into all active voices
                broadcastFrameToVoices(activePartialCount, true);
            }
        }
    }


    // =========================================================================
    // M6: Evolution Engine (FR-014 to FR-023)
    // When evolution is enabled AND blend is NOT enabled, the evolution engine
    // overrides the normal morph/freeze frame selection path (FR-022).
    // =========================================================================
    const bool evolutionEnabled =
        evolutionEnable_.load(std::memory_order_relaxed) > 0.5f;
    const bool blendEnabled =
        blendEnable_.load(std::memory_order_relaxed) > 0.5f;

    // Update evolution engine parameters from smoothers (FR-023)
    if (evolutionEnabled)
    {
        // Speed: denormalize from [0,1] to [0.01, 10.0] Hz
        const float speedNorm = evolutionSpeed_.load(std::memory_order_relaxed);
        const float speedPlain = 0.01f + speedNorm * (10.0f - 0.01f);
        evolutionSpeedSmoother_.setTarget(speedPlain);

        const float depthVal = evolutionDepth_.load(std::memory_order_relaxed);
        evolutionDepthSmoother_.setTarget(depthVal);

        const float modeNorm = evolutionMode_.load(std::memory_order_relaxed);
        const int modeInt = std::clamp(static_cast<int>(std::round(modeNorm * 2.0f)), 0, 2);
        evolutionEngine_.setMode(static_cast<EvolutionMode>(modeInt));

        // Manual offset: use morphPosition as the offset (FR-021)
        const float morphPos = morphPosition_.load(std::memory_order_relaxed);
        evolutionEngine_.setManualOffset(morphPos);
    }

    // --- M6: Update stereo spread and detune spread from smoothers ---
    stereoSpreadSmoother_.setTarget(stereoSpread_.load(std::memory_order_relaxed));
    detuneSpreadSmoother_.setTarget(detuneSpread_.load(std::memory_order_relaxed));

    // --- M6: Update modulator parameters (FR-024, FR-033) ---
    if (mod1Enabled)
    {
        const float waveNorm = mod1Waveform_.load(std::memory_order_relaxed);
        mod1_.setWaveform(static_cast<ModulatorWaveform>(
            std::clamp(static_cast<int>(std::round(waveNorm * 4.0f)), 0, 4)));

        float mod1RatePlain;
        if (mod1RateSync_.load(std::memory_order_relaxed) > 0.5f)
        {
            const float noteNorm = mod1NoteValue_.load(std::memory_order_relaxed);
            const int noteIdx = std::clamp(static_cast<int>(std::round(noteNorm * 20.0f)), 0, 20);
            const auto mapping = Krate::DSP::getNoteValueFromDropdown(noteIdx);
            const float beats = Krate::DSP::getBeatsForNote(mapping.note, mapping.modifier);
            mod1RatePlain = static_cast<float>(tempoBPM_ / (60.0 * static_cast<double>(beats)));
            mod1RatePlain = std::clamp(mod1RatePlain, 0.01f, 100.0f);
        }
        else
        {
            const float rateNorm = mod1Rate_.load(std::memory_order_relaxed);
            mod1RatePlain = 0.01f + rateNorm * (20.0f - 0.01f);
        }
        mod1RateSmoother_.setTarget(mod1RatePlain);

        const float depthVal = mod1Depth_.load(std::memory_order_relaxed);
        mod1DepthSmoother_.setTarget(depthVal);

        const float rangeStartNorm = mod1RangeStart_.load(std::memory_order_relaxed);
        const float rangeEndNorm = mod1RangeEnd_.load(std::memory_order_relaxed);
        const int rangeStart = 1 + static_cast<int>(std::round(rangeStartNorm * 95.0f));
        const int rangeEnd = 1 + static_cast<int>(std::round(rangeEndNorm * 95.0f));
        mod1_.setRange(rangeStart, rangeEnd);

        const float targetNorm = mod1Target_.load(std::memory_order_relaxed);
        mod1_.setTarget(static_cast<ModulatorTarget>(
            std::clamp(static_cast<int>(std::round(targetNorm * 2.0f)), 0, 2)));
    }

    if (mod2Enabled)
    {
        const float waveNorm = mod2Waveform_.load(std::memory_order_relaxed);
        mod2_.setWaveform(static_cast<ModulatorWaveform>(
            std::clamp(static_cast<int>(std::round(waveNorm * 4.0f)), 0, 4)));

        float mod2RatePlain;
        if (mod2RateSync_.load(std::memory_order_relaxed) > 0.5f)
        {
            const float noteNorm = mod2NoteValue_.load(std::memory_order_relaxed);
            const int noteIdx = std::clamp(static_cast<int>(std::round(noteNorm * 20.0f)), 0, 20);
            const auto mapping = Krate::DSP::getNoteValueFromDropdown(noteIdx);
            const float beats = Krate::DSP::getBeatsForNote(mapping.note, mapping.modifier);
            mod2RatePlain = static_cast<float>(tempoBPM_ / (60.0 * static_cast<double>(beats)));
            mod2RatePlain = std::clamp(mod2RatePlain, 0.01f, 100.0f);
        }
        else
        {
            const float rateNorm = mod2Rate_.load(std::memory_order_relaxed);
            mod2RatePlain = 0.01f + rateNorm * (20.0f - 0.01f);
        }
        mod2RateSmoother_.setTarget(mod2RatePlain);

        const float depthVal = mod2Depth_.load(std::memory_order_relaxed);
        mod2DepthSmoother_.setTarget(depthVal);

        const float rangeStartNorm = mod2RangeStart_.load(std::memory_order_relaxed);
        const float rangeEndNorm = mod2RangeEnd_.load(std::memory_order_relaxed);
        const int rangeStart = 1 + static_cast<int>(std::round(rangeStartNorm * 95.0f));
        const int rangeEnd = 1 + static_cast<int>(std::round(rangeEndNorm * 95.0f));
        mod2_.setRange(rangeStart, rangeEnd);

        const float targetNorm = mod2Target_.load(std::memory_order_relaxed);
        mod2_.setTarget(static_cast<ModulatorTarget>(
            std::clamp(static_cast<int>(std::round(targetNorm * 2.0f)), 0, 2)));
    }

    // --- Process each sample ---
    bool hasSoundOutput = false;
    const bool hasResidual = isSidechainMode ? hasLiveResidual : hasSampleResidual;

    // Hoist voice mode outside per-sample loop (avoid atomic load per sample)
    const float voiceModeNorm = voiceMode_.load(std::memory_order_relaxed);
    const int voiceModeIdx = std::clamp(
        static_cast<int>(std::round(voiceModeNorm * 2.0f)), 0, 2);
    constexpr int kVoiceCounts[] = {1, 4, 8};
    const int maxVoicesThisBlock = kVoiceCounts[voiceModeIdx];

    // Spec 129 FR-029: Detect resonance type change and initiate crossfade
    // for all active voices whose active type differs from the target.
    for (int vi = 0; vi < maxVoicesThisBlock; ++vi)
    {
        auto& v = voices_[static_cast<size_t>(vi)];
        if (!v.active) continue;
        if (!v.crossfadeActive && v.activeResonanceType_ != targetResonanceType)
        {
            v.crossfadeActive = true;
            v.crossfadeFromType = v.activeResonanceType_;
            v.crossfadeToType = targetResonanceType;
            // ~23ms at current sample rate
            v.crossfadeTotalSamples = std::max(
                static_cast<int>(0.023 * sampleRate_), 1);
            v.crossfadeSamplesRemaining = v.crossfadeTotalSamples;
        }
    }

    // Spec 130 FR-020: Enable/disable bowed-mode coupling on modal resonator
    {
        const bool isBowExciter = (exciterType == ExciterType::Bow);
        const float bPos = bowPosition_.load(std::memory_order_relaxed);
        for (int vi = 0; vi < maxVoicesThisBlock; ++vi)
        {
            auto& v = voices_[static_cast<size_t>(vi)];
            if (!v.active) continue;
            // Bowed-mode coupling active when exciter is Bow and resonator is modal (type 0)
            bool bowModalActive = isBowExciter && (v.activeResonanceType_ == 0);
            v.modalResonator.setBowModeActive(bowModalActive);
            if (bowModalActive)
                v.modalResonator.setBowPosition(bPos);
        }
    }

    // Spec 131: Set body resonance params per voice, once per block
    for (int vi = 0; vi < maxVoicesThisBlock; ++vi)
    {
        auto& v = voices_[static_cast<size_t>(vi)];
        if (!v.active) continue;
        v.bodyResonance.setParams(bodySize, bodyMaterial, bodyMix);
    }

    for (Steinberg::int32 s = 0; s < numSamples; ++s)
    {
        // --- Frame advancement (FR-047) -- only in sample mode ---
        // FR-007: Skip frame advancement when manual freeze is active
        if (!manualFreezeActive_ && !isSidechainMode && hopSizeInSamples > 0 && totalFrames > 0)
        {
            voice_.frameSampleCounter++;
            if (voice_.frameSampleCounter >= hopSizeInSamples)
            {
                if (voice_.currentFrameIndex < totalFrames - 1)
                {
                    voice_.frameSampleCounter = 0;
                    voice_.currentFrameIndex++;

                    // Spec 124: Sustain loop — when ADSR is in Sustain stage and
                    // a valid loop region exists, wrap frame index back to loop start.
                    // This keeps the note alive indefinitely while the key is held.
                    if (adsrActive && voice_.sustainLoopStart < voice_.sustainLoopEnd &&
                        voice_.adsr.getStage() == Krate::DSP::ADSRStage::Sustain &&
                        std::cmp_greater_equal(voice_.currentFrameIndex, voice_.sustainLoopEnd))
                    {
                        voice_.currentFrameIndex = static_cast<size_t>(voice_.sustainLoopStart);
                    }

                    // Get the new frame
                    const auto& frame = analysis->getFrame(voice_.currentFrameIndex);

                    // Confidence-gated freeze (FR-052) with hysteresis (FR-053)
                    float recoveryThreshold = voice_.isFrozen
                        ? (kConfidenceThreshold + kConfidenceHysteresis)
                        : kConfidenceThreshold;

                    if (frame.f0Confidence >= recoveryThreshold)
                    {
                        if (voice_.isFrozen)
                        {
                            voice_.isFrozen = false;
                            float captL = 0.0f;
                            float captR = 0.0f;
                            voice_.oscillatorBank.processStereo(captL, captR);
                            voice_.freezeRecoveryOldLevel = (captL + captR) * 0.5f;
                            voice_.freezeRecoverySamplesRemaining = voice_.freezeRecoveryLengthSamples;
                        }
                        voice_.lastGoodFrame = frame;

                        // M4: Store frame as morphed (no freeze = pass-through)
                        voice_.morphedFrame = frame;

                        // Apply harmonic filter (FR-026)
                        if (currentFilterType_ != 0)
                            Krate::DSP::applyHarmonicMask(voice_.morphedFrame, filterMask_);

                        // M6 FR-025: Apply modulator amplitude modulation
                        applyModulatorAmplitude(mod1Enabled, mod2Enabled);
                        applyHarmonicPhysics();

                        if (hasSampleResidual)
                        {
                            voice_.morphedResidualFrame =
                                analysis->getResidualFrame(voice_.currentFrameIndex);
                        }
                        broadcastFrameToVoices(activePartialCount, hasSampleResidual);
                    }
                    else
                    {
                        voice_.isFrozen = true;
                    }
                }
                else if (hasSampleResidual)
                {
                    // At the last frame: re-load residual to keep overlap-add
                    // synthesis alive. The MCF oscillators sustain without reloading,
                    // but the residual synth needs periodic loadFrame() calls.
                    voice_.frameSampleCounter = 0;
                    broadcastFrameToVoices(activePartialCount, true);
                }
            }
        }

        // WI-6: Gate evolution/blend frame RECONSTRUCTION (snapshot recall, lerp,
        // physics, and the residual STFT inside broadcastFrameToVoices) to the
        // analysis hop rate rather than per sample -- running it at sample rate is
        // a ~500x inflation of hop-rate work that overruns CPU and drops out. The
        // per-sample smoother/engine advance below is unchanged; only the frame
        // rebuild is gated. Between rebuilds the MCF bank sustains itself and the
        // amplitude one-pole smoothers cover sub-frame smoothing.
        bool rebuildEvoBlend = false;
        if (evolutionEnabled || blendEnabled)
        {
            if (evoBlendRebuildCounter_ <= 0)
            {
                rebuildEvoBlend = true;
                evoBlendRebuildCounter_ = (hopSizeInSamples > 0)
                    ? static_cast<int>(hopSizeInSamples)
                    : 512;
            }
            --evoBlendRebuildCounter_;
        }
        else
        {
            evoBlendRebuildCounter_ = 0;
        }

        // M6: Evolution Engine per-sample advance (FR-014, FR-022)
        if (evolutionEnabled && !blendEnabled)
        {
            // Advance smoothers per sample
            float evoSpeed = evolutionSpeedSmoother_.process();
            float evoDepth = evolutionDepthSmoother_.process();
            evolutionEngine_.setSpeed(evoSpeed);
            evolutionEngine_.setDepth(evoDepth);

            evolutionEngine_.advance();

            // Load evolution-interpolated frame at each frame boundary
            // (or on first sample of block if evolution just enabled)
            // For simplicity, we load every sample (loadFrame is cheap compared to processing)
            Krate::DSP::HarmonicFrame evoFrame{};
            Krate::DSP::ResidualFrame evoResidual{};
            Krate::DSP::MemorySlot evoAdsrInterp{};
            if (rebuildEvoBlend &&
                evolutionEngine_.getInterpolatedFrame(memorySlots_, evoFrame, evoResidual, &evoAdsrInterp))
            {
                voice_.morphedFrame = evoFrame;
                voice_.morphedResidualFrame = evoResidual;

                // Spec 124 FR-017: Wire interpolated ADSR from evolution to processor atomics
                adsrAttackMs_.store(evoAdsrInterp.adsrAttackMs, std::memory_order_relaxed);
                adsrDecayMs_.store(evoAdsrInterp.adsrDecayMs, std::memory_order_relaxed);
                adsrSustainLevel_.store(evoAdsrInterp.adsrSustainLevel, std::memory_order_relaxed);
                adsrReleaseMs_.store(evoAdsrInterp.adsrReleaseMs, std::memory_order_relaxed);
                adsrAmount_.store(evoAdsrInterp.adsrAmount, std::memory_order_relaxed);
                adsrTimeScale_.store(evoAdsrInterp.adsrTimeScale, std::memory_order_relaxed);
                adsrAttackCurve_.store(evoAdsrInterp.adsrAttackCurve, std::memory_order_relaxed);
                adsrDecayCurve_.store(evoAdsrInterp.adsrDecayCurve, std::memory_order_relaxed);
                adsrReleaseCurve_.store(evoAdsrInterp.adsrReleaseCurve, std::memory_order_relaxed);

                // Apply harmonic filter
                if (currentFilterType_ != 0)
                    Krate::DSP::applyHarmonicMask(voice_.morphedFrame, filterMask_);

                // M6 FR-025: Apply modulator amplitude modulation
                applyModulatorAmplitude(mod1Enabled, mod2Enabled);
                applyHarmonicPhysics();
                broadcastFrameToVoices(activePartialCount, true);
            }
        }
        else
        {
            // Advance smoothers regardless to keep them tracking
            (void)evolutionSpeedSmoother_.process();
            (void)evolutionDepthSmoother_.process();
            if (evolutionEnabled)
            {
                // Evolution is enabled but blend overrides it (FR-052)
                // Still advance the engine to keep phase continuous
                evolutionEngine_.advance();
            }
        }

        // M6: Multi-Source Blending (FR-034 to FR-042, FR-052)
        // When blend is enabled, blender produces currentFrame, overriding
        // both the normal recall/freeze path and the evolution path.
        if (blendEnabled)
        {
            // Update blend weights from smoothers (FR-041)
            for (int i = 0; i < 8; ++i)
            {
                blendWeightSmootherArray_[static_cast<size_t>(i)].setTarget(
                    blendSlotWeights_[static_cast<size_t>(i)].load(std::memory_order_relaxed));
                harmonicBlender_.setSlotWeight(
                    i, blendWeightSmootherArray_[static_cast<size_t>(i)].process());
            }
            blendWeightSmootherArray_[8].setTarget(
                blendLiveWeight_.load(std::memory_order_relaxed));
            harmonicBlender_.setLiveWeight(blendWeightSmootherArray_[8].process());

            // Determine if live source is available
            bool hasLiveSource = isSidechainMode &&
                currentLiveFrame_.f0Confidence > 0.0f;

            Krate::DSP::HarmonicFrame blendFrame{};
            Krate::DSP::ResidualFrame blendResidual{};
            if (rebuildEvoBlend &&
                harmonicBlender_.blend(memorySlots_,
                    currentLiveFrame_, currentLiveResidualFrame_,
                    hasLiveSource, blendFrame, blendResidual))
            {
                voice_.morphedFrame = blendFrame;
                voice_.morphedResidualFrame = blendResidual;

                // Spec 124 FR-016: Wire blended ADSR from morph engine to processor atomics
                Krate::DSP::MemorySlot blendAdsrInterp{};
                if (harmonicBlender_.blendADSR(memorySlots_, blendAdsrInterp))
                {
                    adsrAttackMs_.store(blendAdsrInterp.adsrAttackMs, std::memory_order_relaxed);
                    adsrDecayMs_.store(blendAdsrInterp.adsrDecayMs, std::memory_order_relaxed);
                    adsrSustainLevel_.store(blendAdsrInterp.adsrSustainLevel, std::memory_order_relaxed);
                    adsrReleaseMs_.store(blendAdsrInterp.adsrReleaseMs, std::memory_order_relaxed);
                    adsrAmount_.store(blendAdsrInterp.adsrAmount, std::memory_order_relaxed);
                    adsrTimeScale_.store(blendAdsrInterp.adsrTimeScale, std::memory_order_relaxed);
                    adsrAttackCurve_.store(blendAdsrInterp.adsrAttackCurve, std::memory_order_relaxed);
                    adsrDecayCurve_.store(blendAdsrInterp.adsrDecayCurve, std::memory_order_relaxed);
                    adsrReleaseCurve_.store(blendAdsrInterp.adsrReleaseCurve, std::memory_order_relaxed);
                }

                // Apply harmonic filter
                if (currentFilterType_ != 0)
                    Krate::DSP::applyHarmonicMask(voice_.morphedFrame, filterMask_);

                // Apply modulator amplitude modulation
                applyModulatorAmplitude(mod1Enabled, mod2Enabled);
                applyHarmonicPhysics();
                broadcastFrameToVoices(activePartialCount, true);
            }
        }
        else
        {
            // Blend disabled: advance smoothers to keep tracking
            for (int i = 0; i < 8; ++i)
            {
                blendWeightSmootherArray_[static_cast<size_t>(i)].setTarget(
                    blendSlotWeights_[static_cast<size_t>(i)].load(std::memory_order_relaxed));
                (void)blendWeightSmootherArray_[static_cast<size_t>(i)].process();
            }
            blendWeightSmootherArray_[8].setTarget(
                blendLiveWeight_.load(std::memory_order_relaxed));
            (void)blendWeightSmootherArray_[8].process();
        }

        // M6: Update stereo spread and detune per sample (all active voices)
        {
            float spread = stereoSpreadSmoother_.process();
            float detune = detuneSpreadSmoother_.process();
            for (int vi = 0; vi < maxVoicesThisBlock; ++vi)
            {
                auto& v = voices_[static_cast<size_t>(vi)];
                if (!v.active) continue;
                v.oscillatorBank.setStereoSpread(spread);
                v.oscillatorBank.setDetuneSpread(detune);
            }
        }

        // M6: Advance harmonic modulators per sample (FR-029: free-running)
        // Apply smoothed rate and depth each sample (FR-033)
        // Modulator state is global (shared), but effects apply to all voices.
        if (mod1Enabled)
        {
            mod1_.setRate(mod1RateSmoother_.process());
            mod1_.setDepth(mod1DepthSmoother_.process());
            mod1_.advance();

            std::array<float, Krate::DSP::kMaxPartials> mult1{};
            mod1_.getFrequencyMultipliers(mult1);
            std::array<float, Krate::DSP::kMaxPartials> panOff1{};
            mod1_.getPanOffsets(panOff1);

            for (int vi = 0; vi < maxVoicesThisBlock; ++vi)
            {
                auto& v = voices_[static_cast<size_t>(vi)];
                if (!v.active) continue;
                v.oscillatorBank.applyExternalFrequencyMultipliers(mult1);
                v.oscillatorBank.applyPanOffsets(panOff1);
            }
        }
        else
        {
            (void)mod1RateSmoother_.process();
            (void)mod1DepthSmoother_.process();
        }

        if (mod2Enabled)
        {
            mod2_.setRate(mod2RateSmoother_.process());
            mod2_.setDepth(mod2DepthSmoother_.process());
            mod2_.advance();

            std::array<float, Krate::DSP::kMaxPartials> mult2{};
            mod2_.getFrequencyMultipliers(mult2);
            std::array<float, Krate::DSP::kMaxPartials> panOff2{};
            mod2_.getPanOffsets(panOff2);

            for (int vi = 0; vi < maxVoicesThisBlock; ++vi)
            {
                auto& v = voices_[static_cast<size_t>(vi)];
                if (!v.active) continue;
                v.oscillatorBank.applyExternalFrequencyMultipliers(mult2);
                v.oscillatorBank.applyPanOffsets(panOff2);
            }
        }
        else
        {
            (void)mod2RateSmoother_.process();
            (void)mod2DepthSmoother_.process();
        }

        // --- Generate polyphonic stereo output ---
        // Advance global smoothers per-sample (FR-025)
        float harmLevel = harmonicLevelSmoother_.process();
        float resLevel = residualLevelSmoother_.process();
        (void)brightnessSmoother_.process();
        (void)transientEmphasisSmoother_.process();

        float sampleL = 0.0f;
        float sampleR = 0.0f;
        int activeCount = 0;

        for (int vi = 0; vi < maxVoicesThisBlock; ++vi)
        {
            auto& v = voices_[static_cast<size_t>(vi)];
            if (!v.active)
                continue;

            float vL = 0.0f;
            float vR = 0.0f;
            v.oscillatorBank.processStereo(vL, vR);
            // Spec 128/130 FR-030: Select excitation source based on exciter type
            // FR-015/FR-016/FR-017: Unified exciter interface — pass resonator feedback velocity
            float feedbackVelocity = (v.activeResonanceType_ == 1)
                ? v.waveguideString.getFeedbackVelocity()
                : v.modalResonator.getFeedbackVelocity();
            float excitation = 0.0f;
            switch (exciterType)
            {
            case ExciterType::Impact:
                excitation = v.impactExciter.process(feedbackVelocity);
                break;
            case ExciterType::Bow:
            {
                // Bow-specific pre-processing: feed ADSR and resonator energy
                float adsrValue = v.adsr.process();
                v.bowExciter.setEnvelopeValue(adsrValue);
                if (v.activeResonanceType_ == 1)
                    v.bowExciter.setResonatorEnergy(
                        v.waveguideString.getControlEnergy());
                else
                    v.bowExciter.setResonatorEnergy(
                        v.modalResonator.getControlEnergy());
                excitation = v.bowExciter.process(feedbackVelocity);
                break;
            }
            default: // ExciterType::Residual
                excitation = hasResidual ? v.residualSynth.process(feedbackVelocity) : 0.0f;
                break;
            }
            float residualSample = excitation;

            // M2: Mix harmonic and residual per voice
            // Phase 3: expressionBrightness modulates the harmonic/residual balance.
            // At 0.5 (default), global levels are used unmodified.
            // At 0.0, harmonics are boosted by 2x and residuals are silenced.
            // At 1.0, residuals are boosted by 2x and harmonics are silenced.
            float brightScale = v.expressionBrightness * 2.0f; // 0..2
            float perVoiceHarmLevel = harmLevel * (2.0f - brightScale);
            float perVoiceResLevel = resLevel * brightScale;

            // Spec 127/128: Modal resonator excitation
            // FR-031: Impact exciter feeds resonator identically to residual
            // FR-035: Choke envelope updates per sample for smooth decay transition
            if (exciterType == ExciterType::Impact)
            {
                // Advance choke envelope toward 1.0 (no choke)
                v.chokeEnvelope_ = v.chokeEnvelope_ * v.chokeEnvelopeCoeff_
                                   + (1.0f - v.chokeEnvelopeCoeff_);
                // lerp(maxChoke, 1.0, envelope): when envelope=0, full choke; when 1, normal
                v.chokeDecayScale_ = v.chokeMaxScale_
                    + (1.0f - v.chokeMaxScale_) * v.chokeEnvelope_;
            }
            // Spec 129: Route excitation to active resonator type
            // FR-029/FR-030/FR-031: Crossfade between resonator types
            float physicalSample = 0.0f;
            if (v.crossfadeActive)
            {
                // During crossfade: run both resonators with same excitation
                float modalOut = v.modalResonator.processSample(
                    excitation, v.chokeDecayScale_);
                float wgOut = v.waveguideString.process(excitation);

                // Determine which is old/new
                float oldOut = (v.crossfadeFromType == 1) ? wgOut : modalOut;
                float newOut = (v.crossfadeToType == 1) ? wgOut : modalOut;

                // FR-031: Energy-aware gain matching using perceptual energy
                float eOld = (v.crossfadeFromType == 1)
                    ? v.waveguideString.getPerceptualEnergy()
                    : v.modalResonator.getPerceptualEnergy();
                float eNew = (v.crossfadeToType == 1)
                    ? v.waveguideString.getPerceptualEnergy()
                    : v.modalResonator.getPerceptualEnergy();
                float gainMatch = (eNew > 1e-20f)
                    ? std::sqrt(eOld / eNew) : 1.0f;
                gainMatch = std::clamp(gainMatch, 0.25f, 4.0f);

                // FR-030: Equal-power cosine crossfade
                float t = 1.0f - static_cast<float>(v.crossfadeSamplesRemaining)
                    / static_cast<float>(v.crossfadeTotalSamples);
                constexpr float kHalfPi = 1.5707963267948966f;
                physicalSample = oldOut * std::cos(t * kHalfPi)
                               + newOut * gainMatch * std::sin(t * kHalfPi);

                v.crossfadeSamplesRemaining--;
                if (v.crossfadeSamplesRemaining <= 0)
                {
                    // Crossfade complete: switch to new type
                    v.activeResonanceType_ = v.crossfadeToType;
                    v.crossfadeActive = false;
                    // Silence the outgoing resonator
                    if (v.crossfadeFromType == 1)
                        v.waveguideString.silence();
                    else
                        v.modalResonator.reset();
                }
            }
            else if (v.activeResonanceType_ == 1) {
                // Waveguide mode
                physicalSample = v.waveguideString.process(excitation);
            } else {
                // Modal mode (default)
                physicalSample = v.modalResonator.processSample(
                    excitation, v.chokeDecayScale_);
            }

            // Spec 131: Apply body resonance post-processing to all resonator paths
            physicalSample = v.bodyResonance.process(physicalSample);

            float resContrib = residualSample * perVoiceResLevel;

            // Spec 127 FR-023: PhysicalModelMixer blends residual and physical paths
            // At mix=0: monoMix = resContrib (bit-exact with pre-feature behavior)
            // At mix=1: monoMix = physicalSample (full modal replacement)
            float monoMix = Innexus::PhysicalModelMixer::process(
                0.0f, resContrib, physicalSample, physModelMix);

            vL = vL * perVoiceHarmLevel + monoMix;
            vR = vR * perVoiceHarmLevel + monoMix;

            // --- Per-voice freeze recovery crossfade (FR-053) ---
            if (v.freezeRecoverySamplesRemaining > 0)
            {
                float fadeProgress = static_cast<float>(v.freezeRecoverySamplesRemaining) /
                                     static_cast<float>(v.freezeRecoveryLengthSamples);
                vL = v.freezeRecoveryOldLevel * fadeProgress + vL * (1.0f - fadeProgress);
                vR = v.freezeRecoveryOldLevel * fadeProgress + vR * (1.0f - fadeProgress);
                v.freezeRecoverySamplesRemaining--;
            }

            // --- Per-voice anti-click crossfade (FR-054) ---
            if (v.antiClickSamplesRemaining > 0)
            {
                float fadeProgress = static_cast<float>(v.antiClickSamplesRemaining) /
                                     static_cast<float>(v.antiClickLengthSamples);
                vL = v.antiClickOldLevel * fadeProgress + vL * (1.0f - fadeProgress);
                vR = v.antiClickOldLevel * fadeProgress + vR * (1.0f - fadeProgress);
                v.antiClickSamplesRemaining--;
            }

            // --- Per-voice velocity scaling (FR-050) ---
            vL *= v.velocityGain;
            vR *= v.velocityGain;

            // --- Per-voice expression volume (Phase 3: MPE) ---
            vL *= v.expressionVolume;
            vR *= v.expressionVolume;

            // --- Per-voice expression pan (Phase 3: MPE) ---
            if (v.expressionPan != 0.5f)
            {
                // Constant-power pan law
                constexpr float kPi4 = 0.7853981633974483f;
                float angle = v.expressionPan * kPi4 * 2.0f; // 0..pi/2
                float panL = std::cos(angle);
                float panR = std::sin(angle);
                float mono = (vL + vR) * 0.5f;
                vL = mono * panL;
                vR = mono * panR;
            }

            // --- Per-voice ADSR envelope gain ---
            if (adsrActive)
            {
                float envVal = v.adsr.process();
                float smoothedAmount = v.adsrAmountSmoother.process();
                float adsrGain = 1.0f - smoothedAmount + smoothedAmount * envVal;
                vL *= adsrGain;
                vR *= adsrGain;
            }
            else if (v.inAdsrRelease)
            {
                // QS-14: adsrAmount was automated to 0 while this voice was still
                // in its ADSR release, so the global gate above went false. Keep
                // advancing the envelope so the release can finish.
                (void)v.adsr.process();
                (void)v.adsrAmountSmoother.process();
            }

            // QS-14: free a finished ADSR release regardless of the global gate.
            // Previously this lived inside `if (adsrActive)`, so automating
            // adsrAmount to 0 mid-release stranded the voice active forever.
            if (v.inAdsrRelease &&
                v.adsr.getStage() == Krate::DSP::ADSRStage::Idle)
            {
                v.active = false;
                v.inAdsrRelease = false;
                v.oscillatorBank.reset();
                v.residualSynth.reset();
                // Signal finished to allocator (poly mode)
                if (maxVoicesThisBlock > 1)
                    voiceAllocator_.voiceFinished(static_cast<size_t>(vi));
                vL = 0.0f;
                vR = 0.0f;
            }

            // --- Per-voice release envelope (FR-049) ---
            if (v.inRelease)
            {
                v.releaseGain *= v.releaseDecayCoeff;
                vL *= v.releaseGain;
                vR *= v.releaseGain;

                if (v.releaseGain < 1e-6f)
                {
                    v.active = false;
                    v.inRelease = false;
                    v.releaseGain = 1.0f;
                    v.oscillatorBank.reset();
                    v.residualSynth.reset();
                    if (maxVoicesThisBlock > 1)
                        voiceAllocator_.voiceFinished(static_cast<size_t>(vi));
                    vL = 0.0f;
                    vR = 0.0f;
                }
            }

            sampleL += vL;
            sampleR += vR;
            ++activeCount;
        }

        // --- Gain compensation for polyphony: 1/sqrt(activeVoiceCount) ---
        if (activeCount > 1)
        {
            float comp = 1.0f / std::sqrt(static_cast<float>(activeCount));
            sampleL *= comp;
            sampleR *= comp;
        }

        // --- Source switch crossfade (FR-011) ---
        if (sourceCrossfadeSamplesRemaining_ > 0)
        {
            float fadeProgress = static_cast<float>(sourceCrossfadeSamplesRemaining_) /
                                 static_cast<float>(sourceCrossfadeLengthSamples_);
            sampleL = sourceCrossfadeOldLevel_ * fadeProgress +
                      sampleL * (1.0f - fadeProgress);
            sampleR = sourceCrossfadeOldLevel_ * fadeProgress +
                      sampleR * (1.0f - fadeProgress);
            sourceCrossfadeSamplesRemaining_--;
        }

        // --- M4: Manual freeze recovery crossfade (FR-006) ---
        if (manualFreezeRecoverySamplesRemaining_ > 0)
        {
            float fadeProgress = static_cast<float>(manualFreezeRecoverySamplesRemaining_) /
                                 static_cast<float>(manualFreezeRecoveryLengthSamples_);
            sampleL = manualFreezeRecoveryOldLevel_ * fadeProgress +
                      sampleL * (1.0f - fadeProgress);
            sampleR = manualFreezeRecoveryOldLevel_ * fadeProgress +
                      sampleR * (1.0f - fadeProgress);
            manualFreezeRecoverySamplesRemaining_--;
        }

        // --- Spec 132: Sympathetic resonance (post-voice-sum, pre-master-gain) ---
        {
            float monoSum = (sampleL + sampleR) * 0.5f;
            float sympatheticOut = sympatheticResonance_.process(monoSum);
            sampleL += sympatheticOut;
            sampleR += sympatheticOut;
        }

        // --- Master gain ---
        sampleL *= gain;
        sampleR *= gain;

        // --- Safety soft limiter (always on, no UI control) ---
        // Prevents output from exceeding [-1, 1] to protect speakers.
        // Only engages above ±kKnee to stay transparent at normal levels.
        // tanh at signal levels below the knee adds measurable intermodulation
        // noise between harmonics (~5 dB SNR degradation at RMS 0.5).
        {
            // WI-17: final non-finite guard (defense in depth). std::clamp and the
            // soft limiter both pass NaN straight through, so a single pathological
            // sample (corrupt WAV, degenerate analysis) would otherwise reach the
            // host. Uses a bit test because -ffast-math makes std::isfinite()
            // unreliable on some CI toolchains.
            auto sanitize = [](float x) noexcept -> float {
                // Krate::DSP::detail::isFinite is the barrier-hardened bit test;
                // a plain local memcpy/bit-mask is foldable under newer
                // fast-math compilers (Apple Clang / Xcode 26.6).
                return Krate::DSP::detail::isFinite(x) ? x : 0.0f; // Inf/NaN -> 0
            };
            auto softLimit = [&sanitize](float x) noexcept -> float {
                x = sanitize(x);
                constexpr float k = 0.85f;
                float ax = std::abs(x);
                if (ax <= k) return x;
                // Cubic soft-clip: maps [knee, inf) to [knee, 1) smoothly
                float excess = ax - k;
                float limited = k + (1.0f - k) * Krate::DSP::Sigmoid::tanh(
                    excess / (1.0f - k));
                return (x >= 0.0f) ? limited : -limited;
            };
            // Write stereo output (M6: FR-007), or sum to mono (FR-013).
            // QS-16: the mono bus must limit the SUM once. Limiting L and R
            // independently and then adding lets two sub-1.0 samples reach ~2.0,
            // defeating the safety limiter on the mono output.
            if (numOutputChannels >= 2) {
                sampleL = softLimit(sampleL);
                sampleR = softLimit(sampleR);
                out[0][s] = sampleL;
                out[1][s] = sampleR;
            } else {
                const float mono = softLimit(sampleL + sampleR);
                sampleL = mono;
                sampleR = mono;
                out[0][s] = mono;
            }
        }

        if (sampleL != 0.0f || sampleR != 0.0f)
            hasSoundOutput = true;
    }

    // Spec B FR-002, FR-006, FR-014: Capture mono output into feedback buffer
    // Only active in sidechain mode (InputSource::Sidechain)
    {
        const int currentSource =
            inputSource_.load(std::memory_order_relaxed) > 0.5f ? 1 : 0;
        if (currentSource == 1)
        {
            const auto count = std::min(numSamples,
                static_cast<Steinberg::int32>(feedbackBuffer_.size()));

            if (numOutputChannels >= 2)
            {
                for (Steinberg::int32 s = 0; s < count; ++s)
                {
                    feedbackBuffer_[static_cast<size_t>(s)] =
                        (out[0][s] + out[1][s]) * 0.5f;
                }
            }
            else
            {
                for (Steinberg::int32 s = 0; s < count; ++s)
                    feedbackBuffer_[static_cast<size_t>(s)] = out[0][s];
            }

            // FR-013: Apply per-block exponential decay
            const float decayAmount =
                feedbackDecay_.load(std::memory_order_relaxed);
            if (decayAmount > 0.0f)
            {
                const float decayCoeff = std::exp(
                    -decayAmount * static_cast<float>(count)
                    / static_cast<float>(sampleRate_));
                for (Steinberg::int32 s = 0; s < count; ++s)
                    feedbackBuffer_[static_cast<size_t>(s)] *= decayCoeff;
            }
        }
    }

    if (hasSoundOutput) {
        data.outputs[0].silenceFlags = 0;
    } else {
        data.outputs[0].silenceFlags = (numOutputChannels >= 2) ? 0x3 : 0x1;
    }

    // Spec 124 T048: Update ADSR playback state atomics for ADSRDisplay visualization
    adsrEnvelopeOutput_.store(voice_.adsr.getOutput(), std::memory_order_relaxed);
    adsrStage_.store(static_cast<int>(voice_.adsr.getStage()), std::memory_order_relaxed);
    adsrActive_.store(voice_.adsr.isActive(), std::memory_order_relaxed);

    // M7: Send display data to controller (FR-048)
    // Only when producing output and processing samples (SC-008)
    if (data.numOutputs > 0 && numSamples > 0)
        sendDisplayData(data);

    return Steinberg::kResultOk;
}

// ==============================================================================
// Set Bus Arrangements (instrument: no audio inputs, stereo output only)
// ==============================================================================
Steinberg::tresult PLUGIN_API Processor::setBusArrangements(
    Steinberg::Vst::SpeakerArrangement* inputs, Steinberg::int32 numIns,
    Steinberg::Vst::SpeakerArrangement* outputs, Steinberg::int32 numOuts)
{
    // Accept: 0 or 1 input (sidechain optional), 1 stereo output
    if (numOuts != 1 || outputs[0] != Steinberg::Vst::SpeakerArr::kStereo)
        return Steinberg::kResultFalse;

    if (numIns == 0)
        return AudioEffect::setBusArrangements(inputs, numIns, outputs, numOuts);

    if (numIns == 1 &&
        (inputs[0] == Steinberg::Vst::SpeakerArr::kStereo ||
         inputs[0] == Steinberg::Vst::SpeakerArr::kMono))
        return AudioEffect::setBusArrangements(inputs, numIns, outputs, numOuts);

    return Steinberg::kResultFalse;
}

// ==============================================================================
// Can Process Sample Size
// ==============================================================================
Steinberg::tresult PLUGIN_API Processor::canProcessSampleSize(
    Steinberg::int32 symbolicSampleSize)
{
    if (symbolicSampleSize == Steinberg::Vst::kSample32)
        return Steinberg::kResultTrue;
    return Steinberg::kResultFalse;
}

// ==============================================================================
// Check For New Analysis (FR-058, SC-010)
// ==============================================================================
// NOLINTNEXTLINE(readability-convert-member-functions-to-static) -- accesses members via includes clang-tidy can't resolve
void Processor::checkForNewAnalysis()
{
    if (!sampleAnalyzer_.isComplete())
        return;

    auto result = sampleAnalyzer_.takeResult();
    if (!result)
        return;

    // M2: Prepare residual synth for all voices if new analysis has residual data
    if (result->analysisFFTSize > 0 && result->analysisHopSize > 0 &&
        !result->residualFrames.empty())
    {
        for (auto& voice : voices_)
        {
            // WI-3: prepare() allocates (FFT setup + buffer resizes). Skip it on
            // the audio thread when the voice is already prepared for these exact
            // sizes — setActive() prepares all voices to kShortWindowConfig, which
            // is what the analyzer always emits, so this is normally a no-op.
            if (!voice.residualSynth.isPreparedFor(
                    result->analysisFFTSize, result->analysisHopSize))
            {
                voice.residualSynth.prepare(
                    result->analysisFFTSize, result->analysisHopSize,
                    static_cast<float>(sampleRate_));
            }
        }
    }

    // Spec 124 FR-003: Auto-populate ADSR parameter values upon new sample load
    {
        const auto& adsr = result->detectedADSR;
        adsrAttackMs_.store(adsr.attackMs, std::memory_order_relaxed);
        adsrDecayMs_.store(adsr.decayMs, std::memory_order_relaxed);
        adsrSustainLevel_.store(adsr.sustainLevel, std::memory_order_relaxed);
        adsrReleaseMs_.store(adsr.releaseMs, std::memory_order_relaxed);

        // Store sustain loop region frame indices for all voices
        for (auto& voice : voices_)
        {
            voice.sustainLoopStart = adsr.sustainStartFrame;
            voice.sustainLoopEnd = adsr.sustainEndFrame;
        }

        // Auto-enable ADSR when detection produces results — without this,
        // Amount stays at 0.0 and the detected envelope is never applied.
        adsrAmount_.store(1.0f, std::memory_order_relaxed);

        // T018: Send IMessage to Controller to update ADSR knob positions.
        // The controller converts plain values back to normalized for display.
        auto* msg = allocateMessage();
        if (msg)
        {
            msg->setMessageID("DetectedADSR");
            auto* attrs = msg->getAttributes();
            if (attrs)
            {
                // Send plain values; controller will normalize
                attrs->setFloat("attackMs",
                    static_cast<double>(adsr.attackMs));
                attrs->setFloat("decayMs",
                    static_cast<double>(adsr.decayMs));
                attrs->setFloat("sustainLevel",
                    static_cast<double>(adsr.sustainLevel));
                attrs->setFloat("releaseMs",
                    static_cast<double>(adsr.releaseMs));
                attrs->setFloat("amount", 1.0);
            }
            sendMessage(msg);
            msg->release();
        }
    }

    // Publish new analysis with release semantics
    auto* newAnalysis = result.release();
    auto* oldAnalysis = currentAnalysis_.exchange(newAnalysis,
                                                   std::memory_order_acq_rel);

    // SC-010: Do NOT delete on audio thread. Flag for deferred cleanup.
    // The old pendingDeletion_ (if any) must be cleaned up first.
    // Since this runs once per analysis completion (rare), we accept the
    // theoretical leak of one extra SampleAnalysis if two analyses complete
    // back-to-back before cleanup. In practice, analysis takes seconds
    // and setActive/terminate runs between them.
    // The destructor and setActive(true)/setActive(false) handle cleanup.
    pendingDeletion_ = oldAnalysis;
}

// ==============================================================================
// Load Sample (public API for triggering analysis)
// ==============================================================================
// NOLINTNEXTLINE(readability-convert-member-functions-to-static) -- accesses members via includes clang-tidy can't resolve
void Processor::loadSample(const std::string& filePath)
{
    loadedFilePath_ = filePath;
    if (!filePath.empty())
    {
        sampleAnalyzer_.startAnalysis(filePath);
    }
}

// ==============================================================================
// Apply Harmonic Physics (Spec A: FR-020, FR-021)
// ==============================================================================
void Processor::applyHarmonicPhysics() noexcept
{
    harmonicPhysics_.setWarmth(warmthSmoother_.getCurrentValue());
    harmonicPhysics_.setCoupling(couplingSmoother_.getCurrentValue());
    harmonicPhysics_.setStability(stabilitySmoother_.getCurrentValue());
    harmonicPhysics_.setEntropy(entropySmoother_.getCurrentValue());
    harmonicPhysics_.processFrame(voice_.morphedFrame);
}

// ==============================================================================
// Apply Modulator Amplitude Modulation (FR-025, FR-028)
// ==============================================================================
void Processor::applyModulatorAmplitude(bool mod1Enabled, bool mod2Enabled)
{
    // FR-028: Two modulators on overlapping amplitude ranges multiply effects
    // (sequential application = multiplicative)
    if (mod1Enabled)
        mod1_.applyAmplitudeModulation(voice_.morphedFrame);
    if (mod2Enabled)
        mod2_.applyAmplitudeModulation(voice_.morphedFrame);
}

// ==============================================================================
// Broadcast Morphed Frame to All Active Voices
// ==============================================================================
void Processor::broadcastFrameToVoices(int activePartialCount, bool loadResidual)
{
    const float brightness = brightnessSmoother_.getCurrentValue();
    const float transientEmp = transientEmphasisSmoother_.getCurrentValue();

    // Spec 127: Material parameters for modal resonator update
    const float resDecay = resonanceDecay_.load(std::memory_order_relaxed);
    const float resBrightness = resonanceBrightness_.load(std::memory_order_relaxed);
    const float resStretch = resonanceStretch_.load(std::memory_order_relaxed);
    const float resScatter = resonanceScatter_.load(std::memory_order_relaxed);

    const float modeNorm = voiceMode_.load(std::memory_order_relaxed);
    const int modeIdx = std::clamp(
        static_cast<int>(std::round(modeNorm * 2.0f)), 0, 2);
    constexpr int kCounts[] = {1, 4, 8};
    const int maxVoices = kCounts[modeIdx];

    for (int i = 0; i < maxVoices; ++i)
    {
        auto& v = voices_[static_cast<size_t>(i)];
        if (!v.active)
            continue;

        // Copy the global morphed frame to this voice
        v.morphedFrame = voice_.morphedFrame;
        v.morphedResidualFrame = voice_.morphedResidualFrame;

        // Clamp partials
        v.morphedFrame.numPartials = std::min(
            v.morphedFrame.numPartials, activePartialCount);

        // Compute this voice's target pitch
        float basePitch = Krate::DSP::midiNoteToFrequency(v.midiNote);
        float bendRatio = Krate::DSP::semitonesToRatio(
            v.pitchBendSemitones + v.expressionTuning);
        float targetPitch = basePitch * bendRatio;

        // Skip normalization during spectral decay — the decay envelope is
        // intentionally reducing amplitudes toward zero.
        const bool decaying = voice_.spectralDecay.isActive() &&
                              !voice_.spectralDecay.isFullyDecayed();
        v.oscillatorBank.loadFrame(v.morphedFrame, targetPitch, decaying);

        // Spec 127 FR-019: Update modal resonator modes on frame advance
        // (does NOT clear filter states -- modes ring through transitions)
        {
            // Extract frequencies from morphed frame partials
            // Note: oscillatorBank uses the frame with targetPitch applied,
            // but modal resonator uses the raw partial frequencies from analysis
            // scaled by the pitch ratio (targetPitch / f0)
            const auto& frame = v.morphedFrame;
            float pitchRatio = (frame.f0 > 0.0f) ? targetPitch / frame.f0 : 1.0f;
            std::array<float, Krate::DSP::kMaxPartials> modeFreqs{};
            std::array<float, Krate::DSP::kMaxPartials> modeAmps{};
            for (int k = 0; k < frame.numPartials; ++k)
            {
                modeFreqs[static_cast<size_t>(k)] =
                    frame.partials[static_cast<size_t>(k)].frequency * pitchRatio;
                modeAmps[static_cast<size_t>(k)] =
                    frame.partials[static_cast<size_t>(k)].amplitude;
            }
            v.modalResonator.updateModes(
                modeFreqs.data(), modeAmps.data(), frame.numPartials,
                resDecay, resBrightness, resStretch, resScatter);
        }

        if (loadResidual && v.residualSynth.isPrepared())
        {
            v.residualSynth.loadFrame(
                v.morphedResidualFrame, brightness, transientEmp);
        }
    }
}

} // namespace Innexus
