// ==============================================================================
// Seraphis - Audio Processor implementation
// ==============================================================================
// Phase 8 scaffold. initialize()/terminate() own the two heap-allocated DSP
// components; every other override is filled in by a later, test-first task
// (each is marked with the task that owns it).
// ==============================================================================

#include "processor/processor.h"

#include "engine/seraphis_engine_config.h"
#include "plugin_ids.h"

#include "base/source/fstreamer.h"  // IBStreamer (getState/setState)

#include "pluginterfaces/vst/ivstevents.h"            // IEventList / Vst::Event (FR-025, FR-031)
#include "pluginterfaces/vst/ivstparameterchanges.h"  // IParameterChanges/IParamValueQueue

#include <krate/dsp/core/scoped_denormal_mode.h>

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>

namespace Seraphis {

using namespace Steinberg;

// ==============================================================================
// FR-031 velocity mapping (plan 1.3 C-3, 3.2)
// ==============================================================================
// Declared and defined at NAMESPACE scope, deliberately NOT in an anonymous
// namespace: SC-022 clause 6 asserts the upper clamp ("127, not 128") and that
// has no behavioural proxy anywhere in the chain - SeraphisEngine::dispatch
// computes `static_cast<float>(e.velocity) / 127.0f` (seraphis_engine.h:1137)
// and SeraphisVoice::noteOn clamps that to [0, 1] (seraphis_voice.h:527), so a
// mapped 128 and a mapped 127 render bit-identical audio. The test TU declares
// this prototype itself and calls it directly.
//
// The `+ 0.5f` rounding and the FLOOR OF 1 are load-bearing, not stylistic: a
// truncating uint8(velocity * 127.0f) turns a legal velocity of 0.003 into 0,
// which SeraphisEngine::noteOn maps to noteOff (seraphis_engine.h:374-377) - a
// note-on that releases.
//
// Only ever reached with `velocity > 0.0f`, so a NaN velocity (for which that
// test is false) never gets here; +inf clamps to 127.
//
// NOLINTBEGIN(misc-use-internal-linkage) -- external linkage is REQUIRED, not an
// oversight: tests/unit/midi_event_test.cpp:46 re-declares this prototype and
// calls Seraphis::mapNoteOnVelocity directly (:361-368) to assert the clamp.
// Internal linkage (static / anonymous namespace) would make that an unresolved
// external at link time.
[[nodiscard]] std::uint8_t mapNoteOnVelocity(float velocity) noexcept;

[[nodiscard]] std::uint8_t mapNoteOnVelocity(float velocity) noexcept {
    return static_cast<std::uint8_t>(std::clamp(velocity * 127.0f + 0.5f, 1.0f, 127.0f));
}
// NOLINTEND(misc-use-internal-linkage)

namespace {

/// Plan 3.2. Clamp a host event offset into [0, total].
///
/// Negative and past-the-end offsets are both legal inputs from a malformed
/// host, and neither may ever produce a negative slice length - which, in
/// std::size_t arithmetic, would wrap and hand processStereoBlock a length of
/// several exabytes.
[[nodiscard]] std::size_t clampOffset(Steinberg::int32 offset, std::size_t total) noexcept {
    if (offset <= 0) {
        return 0u;
    }
    const auto o = static_cast<std::size_t>(offset);
    return (o > total) ? total : o;
}

/// FR-031's translation table, in whole:
///
///   kNoteOnEvent  + velocity >  0  -> noteOn(pitch, mapNoteOnVelocity(v))
///   kNoteOnEvent  + velocity <= 0  -> noteOff(pitch)
///   kNoteOffEvent                  -> noteOff(pitch)
///   anything else                  -> ignored
///
/// The velocity-0 branch is redundant with the engine's own guard
/// (seraphis_engine.h:374-377) and is written explicitly anyway, so SC-022
/// clause 2 tests THIS WRAPPER's behaviour rather than the engine's.
///
/// Pitch is range-guarded before the uint8_t cast: Event::noteOn.pitch is an
/// int16 and a host is free to put anything in it.
void dispatchEvent(Krate::DSP::SeraphisEngine& engine, const Vst::Event& event) noexcept {
    if (event.type == Vst::Event::kNoteOnEvent) {
        const int16 pitch = event.noteOn.pitch;
        if (pitch < 0 || pitch > 127) {
            return;
        }
        const auto note = static_cast<std::uint8_t>(pitch);
        if (event.noteOn.velocity > 0.0f) {
            engine.noteOn(note, mapNoteOnVelocity(event.noteOn.velocity));  // :370
        } else {
            engine.noteOff(note);  // :415
        }
        return;
    }
    if (event.type == Vst::Event::kNoteOffEvent) {
        const int16 pitch = event.noteOff.pitch;
        if (pitch < 0 || pitch > 127) {
            return;
        }
        engine.noteOff(static_cast<std::uint8_t>(pitch));
    }
    // Every other event type (controller, poly-pressure, data, ...) is ignored:
    // Phase 8's whole note surface is the event-input bus (FR-019).
}

}  // namespace

Processor::Processor() {
    setControllerClass(kControllerUID);
}

Processor::~Processor() = default;

tresult PLUGIN_API Processor::initialize(FUnknown* context) {
    const tresult result = AudioEffect::initialize(context);
    if (result != kResultOk) {
        return result;
    }

    // FR-020: instrument shape - one event input, one stereo audio output.
    // There is deliberately NO addAudioInput(): Seraphis takes no audio in, and
    // an auxiliary input bus would also make the AU wrapper create an IO element
    // that au-info.plist's "Inputs 0 / Outputs 2" does not declare (-10875).
    // Model: plugins/membrum/src/processor/processor.cpp:117, 120.
    // Anti-model: plugins/ruinae/src/processor/processor.cpp:56.
    addEventInput(STR16("Event In"));
    addAudioOutput(STR16("Main Out"), Vst::SpeakerArr::kStereo);

    // FR-022: heap, non-RT, exactly once.
    engine_ = std::make_unique<Krate::DSP::SeraphisEngine>();
    reverb_ = std::make_unique<Krate::DSP::AetherReverb>();

    return kResultOk;
}

tresult PLUGIN_API Processor::terminate() {
    // `= nullptr`, not `.reset()`: both pointees expose their own reset() member,
    // so `engine_.reset()` reads as "reset the engine's DSP state" when it in
    // fact DESTROYS the engine (readability-ambiguous-smartptr-reset-call).
    // Assignment says the intended thing unambiguously and is identical in effect.
    engine_ = nullptr;
    reverb_ = nullptr;
    return AudioEffect::terminate();
}

// FR-021. The base AudioEffect::setBusArrangements accepts whatever the host
// proposes, so it is overridden here with three rejections, each guarding a
// distinct failure:
//   (a) no audio input bus exists to arrange;
//   (b) EXACTLY one output bus exists - accepting numOuts == 2 would let a host
//       successfully negotiate a bus that does not exist. (The model rejects
//       numOuts > kMaxOutputBuses at plugins/membrum/src/processor/processor.cpp:
//       1058-1059 because Membrum has 16 buses; Seraphis has one.)
//   (c) the render path reads channelBuffers32[0] and [1] unconditionally, so
//       accepting a mono arrangement is an out-of-bounds read on the audio
//       thread (the model's own rationale, membrum processor.cpp:1044-1049).
// The rejection is NOT the guard: a host may ignore kResultFalse and still
// present a mono bus, so process() carries an independent
// data.outputs[0].numChannels < 2 early-out (added by T018).
tresult PLUGIN_API Processor::setBusArrangements(
    Vst::SpeakerArrangement* /*inputs*/, int32 numIns,
    Vst::SpeakerArrangement* outputs, int32 numOuts) {
    if (numIns != 0) {
        return kResultFalse;
    }
    if (numOuts != 1) {
        return kResultFalse;
    }
    if (outputs == nullptr || outputs[0] != Vst::SpeakerArr::kStereo) {
        return kResultFalse;
    }
    return kResultTrue;
}

// FR-023 / FR-028 / FR-053, in exactly the order FR-023 lists.
tresult PLUGIN_API Processor::setupProcessing(Vst::ProcessSetup& setup) {
    // 0. Out-of-order host calls - setupProcessing() before initialize(), or
    //    after terminate() - are exactly what pluginval strictness 5 probes.
    //    Returning here leaves prepared_ false, so process()'s readiness guard
    //    still fires and getLatencySamples() still reports 0 (no reverb_).
    //    There is deliberately NO MXCSR/denormal call in this function: the
    //    flush-to-zero mode is PER-THREAD, and setupProcessing() runs on the
    //    host's setup thread, never the audio thread. Setting it here would
    //    never reach the render (plugins/membrum/src/processor/processor.cpp:
    //    1073-1075); ScopedDenormalMode inside process() is the real guard.
    if (engine_ == nullptr || reverb_ == nullptr) {
        return AudioEffect::setupProcessing(setup);
    }

    sampleRate_ = setup.sampleRate;

    // 1. The block bound is the CONSTANT 2048 (seraphis_engine_config.h:40),
    //    never setup.maxSamplesPerBlock: FR-026's slice loop and FR-028's
    //    scratch must not change shape when a host renegotiates its block size.
    const std::size_t bound = kMaxBlockSamples;

    // 2. Polyphony is seeded FROM THE PARAMETER - setState() may legally
    //    precede setupProcessing() - through the ONE clamping conversion
    //    (parameters/global_params.h:62), never a bare cast.
    const std::size_t poly =
        clampPolyphony(globalParams_.polyphony.load(std::memory_order_relaxed));

    // 3. The two prepares. Both are non-RT and allocate; this is the only place
    //    either is called.
    engine_->prepare(sampleRate_, makeSeraphisEngineConfig(poly, kEngineSeed, bound));
    reverb_->prepare(sampleRate_, makeSeraphisReverbConfig(bound));

    // DEBUG-ONLY: the two components must have adopted the SAME rate. Neither
    // exposes a sample-rate getter, so the agreement is asserted on the one
    // input that decides it: AetherReverb::prepare clamps into
    // [kMinSampleRate, kMaxSampleRate] (aether_reverb.h:1615-1616) while
    // SeraphisEngine::prepare only floors at 1.0 (seraphis_engine.h:202). Inside
    // that window - and only inside it - both adopt sampleRate_ verbatim.
    assert(sampleRate_ >= static_cast<double>(Krate::DSP::AetherReverb::kMinSampleRate) &&
           sampleRate_ <= static_cast<double>(Krate::DSP::AetherReverb::kMaxSampleRate) &&
           "engine and reverb adopt different sample rates outside the reverb's clamp window");

    // 4. Seed the push trackers with what prepare() ACTUALLY delivered - read
    //    back from the engine, not from `poly`, so the tracker records the
    //    engine's own clamp (seraphis_engine.h:665). NEVER 8, and never a
    //    force-push sentinel: prepare() already installed this voice count, so
    //    the first process() must NOT re-call setPolyphony(), which would
    //    re-arm sumGain_ (seraphis_engine.h:349) on every host prepare.
    lastPushedPolyphony_ = engine_->getPolyphony();
    lastPushedSoftLimit_ = globalParams_.softLimit.load(std::memory_order_relaxed);

    // KNOWN RESIDUAL (plan §8.3), recorded rather than hidden: when softLimit is
    // false at prepare, this push RAMPS instead of snapping. SeraphisEngine::
    // prepare() sets satL_/satR_.setSaturation(kOutputSaturation) BEFORE
    // satL_.prepare() precisely so the saturator's smoothers are snapped
    // (seraphis_engine.h:225-231); this setter necessarily runs AFTER prepare()
    // - calling it before is useless, because prepare() re-applies
    // kOutputSaturation unconditionally - and post-prepare
    // TapeSaturator::setSaturation takes the ramping branch
    // (tape_saturator.h:248-252). Effect: the first kDefaultSmoothingMs = 5.0f
    // of the render carries a decaying <= 0.15 tanh/linear blend that should
    // have been 0. Removing it requires threading outputSaturation through
    // SeraphisEngineConfig - a dsp/ change Phase 8's scope forbids.
    // DEFERRED TO PHASE 9; not silently accepted.
    engine_->setOutputSaturation(
        lastPushedSoftLimit_ ? Krate::DSP::SeraphisEngine::kOutputSaturation : 0.0f);

    // 5. FR-028: scratch sized ONCE, to the constant - never to the host block.
    dryL_.assign(bound, 0.0f);
    dryR_.assign(bound, 0.0f);
    wetL_.assign(bound, 0.0f);
    wetR_.assign(bound, 0.0f);

    // 6. Master-gain smoother, then arm the FR-024a clause 3 first-block snap.
    masterGain_.configure(kMasterGainSmoothMs, static_cast<float>(sampleRate_));
    anySamplesSincePrepare_ = false;
    prepared_ = true;

    // 7. No latency announcement: the reported value is the constant 1024 in
    //    every reachable state, and this component has no IComponentHandler to
    //    announce on (plan §1.3 C-1/C-2).
    return AudioEffect::setupProcessing(setup);
}

// FR-032. Both branches run on the host thread with the audio thread stopped,
// which is what makes the deactivate branch legal at all.
tresult PLUGIN_API Processor::setActive(TBool state) {
    if (state != 0) {
        // Activation does exactly ONE thing, and it allocates nothing (SC-026
        // clause 2): re-arm the FR-024a clause 3 seam, so the first process()
        // after re-activation SNAPS the master-gain smoother to the current
        // parameter instead of ramping from the pre-deactivation value.
        anySamplesSincePrepare_ = false;
    } else {
        // Deactivation must leave no ringing tail (SC-026 clause 1).
        // SeraphisEngine::silence() is documented NOT an audio-thread operation
        // (seraphis_engine.h:306-307; ~32 MiB of capture-ring clearing) - which
        // is correct HERE and only here.
        if (engine_ != nullptr) {
            engine_->silence();
        }
        if (reverb_ != nullptr) {
            // Dereferenced form, not `reverb_->reset()`: the arrow spelling is
            // ambiguous with unique_ptr's OWN reset() (which would destroy the
            // reverb here), and clang-tidy flags it for exactly that reason
            // (readability-ambiguous-smartptr-reset-call). This clears the
            // reverb's tail - aether_reverb.h:1971 - and keeps the object.
            (*reverb_).reset();
        }
    }
    return AudioEffect::setActive(state);
}

tresult PLUGIN_API Processor::process(Vst::ProcessData& data) {
    // FR-029. FTZ/DAZ is PER-THREAD, so it must be armed here, on the audio
    // thread, and not in setupProcessing() (plugins/membrum/src/processor/
    // processor.cpp:1073-1075). core/scoped_denormal_mode.h:60.
    const Krate::DSP::ScopedDenormalMode denormalGuard;

    // Automation is latched BEFORE the shape guards: a block that renders
    // nothing must still not lose the host's parameter changes.
    processParameterChanges(data.inputParameterChanges);

    // FR-030 early-outs, in THIS ORDER. The order is load-bearing twice over:
    //  (a) buffer VALIDATION precedes the readiness check, so the one degenerate
    //      case with a valid writable buffer - process() before
    //      setupProcessing() - can be ZERO-FILLED. FR-030 says "by producing
    //      silence", and VST3 does NOT guarantee zeroed output buffers, so
    //      returning without writing hands the host back the previous
    //      plug-in's content. Both wrapped components zero-fill on their own
    //      not-prepared path (seraphis_engine.h:448-451,
    //      aether_reverb.h:2172-2176); so does this one.
    //  (b) nothing reads data.outputs[0] until numOutputs > 0 and
    //      outputs != nullptr are established (SC-021's ordering clause).
    if (data.numOutputs <= 0 || data.outputs == nullptr) {
        return kResultOk;
    }
    if (data.outputs[0].channelBuffers32 == nullptr) {
        return kResultOk;
    }
    // A host may ignore setBusArrangements' kResultFalse and still present a
    // mono bus; channelBuffers32 is then a ONE-element array and [1] is out of
    // bounds. Model: plugins/ruinae/src/processor/processor.cpp:430. Membrum -
    // the model for the rest of this processor - has no numChannels check
    // anywhere, so copying Membrum here would copy the gap.
    if (data.outputs[0].numChannels < 2) {
        return kResultOk;
    }
    if (data.numSamples <= 0) {
        return kResultOk;
    }
    const auto total = static_cast<std::size_t>(data.numSamples);  // now known > 0
    float* outL = data.outputs[0].channelBuffers32[0];
    float* outR = data.outputs[0].channelBuffers32[1];
    if (outL == nullptr || outR == nullptr) {
        return kResultOk;
    }

    // Not ready -> SILENCE, not "leave the buffer alone".
    if (!prepared_ || engine_ == nullptr || reverb_ == nullptr) {
        std::fill_n(outL, total, 0.0f);
        std::fill_n(outR, total, 0.0f);
        data.outputs[0].silenceFlags = 3;  // both channels ARE silent
        return kResultOk;
    }

    // FR-024 STEP 0. Once per process() call, BEFORE the slice loop and before
    // the engine renders a sample. Hoisting is valid for the same reason the
    // master-gain target below is hoisted (plan D-1): processParameterChanges()
    // ran at the top of this function and took the LAST point of every queue
    // (FR-042), so neither atomic can change within a process() call and a
    // per-slice push would push the identical value.
    pushGlobalParams();

    // FR-024a clause 3, first half. The master-gain TARGET is read ONCE per
    // process() call, BEFORE the slice loop. Hoisting is valid, not a shortcut:
    // processParameterChanges() ran at the top of this function and took the
    // LAST point of every queue (FR-042), so the atomic cannot change within a
    // process() call - a per-slice setTarget() would push the identical value.
    // Hoisting it additionally makes the target trivially partition-invariant
    // (plan D-1, 3.1).
    const float gainTarget = globalParams_.masterGain.load(std::memory_order_relaxed);
    if (!anySamplesSincePrepare_) {
        // SNAPPED, never ramped, on the first block after setupProcessing() /
        // setActive(true) - the seam AetherReverb::applyControl uses
        // (aether_reverb.h:2951-2956). Without it a render at kMasterGainId = 0
        // would ramp DOWN from the previous value and its first ~20 ms would be
        // non-zero, failing SC-019 clause 1 for a correct implementation.
        masterGain_.snapTo(gainTarget);  // smoother.h:263
    } else {
        masterGain_.setTarget(gainTarget);  // smoother.h:170
    }

    // FR-025 / FR-026: the event-driven slice loop. SeraphisEngine::noteOn /
    // noteOff take NO sample offset (seraphis_engine.h:370, 415), so
    // sub-division is the only way to deliver one.
    std::size_t cursor = 0;
    int32 nextEvent = 0;
    const int32 numEvents =
        (data.inputEvents != nullptr) ? data.inputEvents->getEventCount() : 0;

    while (cursor < total) {
        // 1. Dispatch EVERY event due at this slice start. A `while`, NOT an
        //    `if`: with an `if` the second event at the same offset would
        //    resolve the next sliceEnd back to `cursor` and a zero-length slice
        //    would reach processStereoBlock (SC-022 clause 5;
        //    tests/test_helpers/seraphis_chain.h:195-198 has the same `while`).
        while (nextEvent < numEvents) {
            Vst::Event event{};
            if (data.inputEvents->getEvent(nextEvent, event) != kResultOk) {
                ++nextEvent;
                continue;
            }
            if (clampOffset(event.sampleOffset, total) > cursor) {
                break;  // due later in this block
            }
            dispatchEvent(*engine_, event);
            ++nextEvent;
        }

        // 2. Slice end = the next event's offset, the block end, or the 2048
        //    bound - whichever comes first. Events are assumed sorted by
        //    sampleOffset (VST3 requires it); the `at > cursor` test keeps the
        //    loop well formed on a malformed list by firing a late-but-earlier
        //    event at the current cursor instead of rewinding it.
        std::size_t sliceEnd = total;
        if (nextEvent < numEvents) {
            Vst::Event event{};
            if (data.inputEvents->getEvent(nextEvent, event) == kResultOk) {
                const std::size_t at = clampOffset(event.sampleOffset, total);
                if (at > cursor && at < sliceEnd) {
                    sliceEnd = at;
                }
            }
        }
        // FR-026, and the ONLY slice bound. kMaxBlockSamples is the SAME
        // constant makeSeraphisEngineConfig()/makeSeraphisReverbConfig() were
        // prepared with (seraphis_engine_config.h:40), so the engine ceiling and
        // the reverb ceiling cannot drift apart. This is the branch a host block
        // larger than 2048 enters.
        sliceEnd = std::min(sliceEnd, cursor + kMaxBlockSamples);
        const std::size_t n = sliceEnd - cursor;
        if (n == 0) {
            break;  // unreachable given the `while` above; guarded anyway
        }

        renderSlice(outL + cursor, outR + cursor, n);
        cursor = sliceEnd;
    }

    // FR-024a clause 3: the snap seam is consumed only once samples were
    // actually produced. setActive(true) and setupProcessing() re-arm it.
    anySamplesSincePrepare_ = true;

    // FR-024's silence-flag clause. Seraphis writes only the CLEARING half: it
    // never asserts silence, because deciding when the instance is genuinely
    // quiet needs a "reverb has decayed" predicate Phase 8 has no criterion for.
    // getTailSamples() therefore stays at the SDK default; tail/idle reporting
    // is Phase 10. Leaving the flags alone would let a host that reads a stale
    // "silent" flag cut the (by-design near-infinite) reverb tail.
    data.outputs[0].silenceFlags = 0;
    return kResultOk;
}

// FR-033, in whole. AetherReverb::getLatencySamples() returns
// `spectralEnabled_ ? diffusionFftSize_ : 0` (aether_reverb.h:2607-2613) and
// makeSeraphisReverbConfig pins both, so this reports the CONSTANT 1024 from
// construction onwards - before the first prepare, at every sample rate, and
// after every parameter. There is no transition, hence no announcement (and no
// IComponentHandler to make one on; plan §1.3 C-1/C-2). The 0 branch is only
// reachable between construction and initialize(), or after terminate().
uint32 PLUGIN_API Processor::getLatencySamples() {
    if (reverb_ == nullptr) {
        return 0;
    }
    return static_cast<uint32>(reverb_->getLatencySamples());
}

// FR-046. Reading state is safe CONCURRENTLY WITH process(): no prepare() is
// reachable from here, and the only things written are std::atomic<> members of
// the two parameter packs. (polyphony in particular enters the engine's domain
// only through pushGlobalParams(), on the audio thread, via clampPolyphony.)
tresult PLUGIN_API Processor::setState(IBStream* state) {
    if (state == nullptr) {
        return kResultFalse;
    }

    IBStreamer streamer(state, kLittleEndian);

    // The version int32 is the ONE mandatory field. A stream too short to carry
    // it is not a Seraphis state at all; a stream from a FUTURE version cannot
    // be interpreted, and applying its bytes to the Phase 8 layout would install
    // garbage. Both are refused outright, before anything is stored.
    int32 version = 0;
    if (!streamer.readInt32(version)) {
        return kResultFalse;
    }
    if (version > kCurrentStateVersion) {
        return kResultFalse;
    }

    // Order MUST match getState (and Controller::setComponentState). Both
    // loaders are EOF-safe: a short stream leaves every unread field at its
    // registered default and returns false, which is not an error here - a
    // truncated preset loads as far as it goes.
    loadGlobalParams(globalParams_, streamer);
    loadMacroParams(macroParams_, streamer);

    return kResultOk;
}

// FR-045. Stream layout is FIXED (plan 3.4): little-endian, 36 bytes -
// int32 version | float masterGain | int32 polyphony | int32 softLimit |
// five floats dream, bloom, dissolve, gravity, entropy.
tresult PLUGIN_API Processor::getState(IBStream* state) {
    IBStreamer streamer(state, kLittleEndian);

    streamer.writeInt32(kCurrentStateVersion);

    saveGlobalParams(globalParams_, streamer);
    saveMacroParams(macroParams_, streamer);

    return kResultOk;
}

// ==============================================================================
// Private helpers
// ==============================================================================

// FR-042 / FR-043. Automation is latched by taking THE LAST POINT of each
// queue, never getPoint(0): VST3 delivers a whole block's automation lane at
// once, so the value that must be in force when the block is rendered is the
// final one in the queue. Dispatch is by ID RANGE (plugin_ids.h:79-80), not by
// an enumeration of the eight IDs, so a Phase 9 parameter added inside 0-99 or
// 100-199 reaches its pack without touching this function; anything outside
// both ranges is ignored rather than misrouted.
//
// RT-safe: no allocation, no locking, no I/O. Every store beneath this is a
// relaxed std::atomic write in the two parameter packs.
void Processor::processParameterChanges(Vst::IParameterChanges* changes) noexcept {
    if (changes == nullptr) {
        return;
    }

    const int32 numQueues = changes->getParameterCount();
    for (int32 q = 0; q < numQueues; ++q) {
        Vst::IParamValueQueue* queue = changes->getParameterData(q);
        if (queue == nullptr) {
            continue;
        }

        const int32 numPoints = queue->getPointCount();
        if (numPoints <= 0) {
            continue;
        }

        int32 sampleOffset = 0;
        Vst::ParamValue value = 0.0;
        if (queue->getPoint(numPoints - 1, sampleOffset, value) != kResultTrue) {
            continue;
        }

        const Vst::ParamID id = queue->getParameterId();
        if (id < kGlobalParamRangeEnd) {
            handleGlobalParamChange(globalParams_, id, value);
        } else if (id < kMacroParamRangeEnd) {
            handleMacroParamChange(macroParams_, id, value);
        }
        // else: an ID outside every shipped range - ignored.
    }
}

// FR-024a clauses 1-2. Called ONLY from process(), which has already
// established that engine_ is non-null and that the processor is prepared.
//
// Both pushes are ON CHANGE ONLY. Re-calling setPolyphony() unconditionally is
// wrong twice over: it re-arms the voice-sum smoother (sumGain_.setTarget(...),
// seraphis_engine.h:349) every block, and setVoiceCount walks the allocator's
// excess-slot loop (:339-348) for nothing.
//
// clampPolyphony() (parameters/global_params.h:62) is MANDATORY here, not
// decorative. setPolyphony clamps to [1, kMaxVoices] (seraphis_engine.h:322)
// and getPolyphony() returns the CLAMPED value (:665), so if the stored atomic
// could hold an out-of-range number - which a hand-written or corrupt state
// stream carrying 0, 20 or a negative int32 can produce - the comparison below
// would be true on EVERY BLOCK, FOREVER: exactly the per-block sumGain_ re-arm
// and excess-slot walk this function exists to prevent. Clamping at the single
// conversion point puts both sides of the comparison in the same domain, so the
// detector converges after one push.
//
// setPolyphonyCalls_ is a test-only counter. It is a plain std::size_t written
// only from the audio thread and read only from the test thread once the render
// has completed; no atomic is needed.
//
// @par Real-Time Safety: allocation-free, lock-free, exception-free. Both
//      atomics are read relaxed; both engine setters are noexcept and
//      allocation-free (setPolyphony is documented allocation-free at
//      seraphis_engine.h:319-321 because prepare() prepares all kMaxVoices
//      slots).
void Processor::pushGlobalParams() noexcept {
    const std::size_t poly =
        clampPolyphony(globalParams_.polyphony.load(std::memory_order_relaxed));
    if (poly != lastPushedPolyphony_) {  // ON CHANGE ONLY
        engine_->setPolyphony(poly);     // seraphis_engine.h:321
        // Re-read POST-CLAMP rather than storing `poly`, so the tracker records
        // what the engine actually holds - the same value the comparison above
        // will be made against next block.
        lastPushedPolyphony_ = engine_->getPolyphony();  // :665
        ++setPolyphonyCalls_;
    }

    const bool soft = globalParams_.softLimit.load(std::memory_order_relaxed);
    if (soft != lastPushedSoftLimit_) {  // ON CHANGE ONLY
        engine_->setOutputSaturation(soft ? Krate::DSP::SeraphisEngine::kOutputSaturation
                                          : 0.0f);  // :566, :142
        lastPushedSoftLimit_ = soft;
    }
}

// FR-024 steps 2-6, in the order tests/test_helpers/seraphis_chain.h:190-259
// models them. Called ONLY from process(), which has already established that
// engine_, reverb_, outL and outR are non-null and that 0 < n <= 2048.
//
// FR-027: nothing here mirrors processOutputStage's internal 64-sample loop.
// The engine's own banner (seraphis_engine.h:506-511) states it is "a CADENCE
// CHOICE, NOT A SIZE CONSTRAINT ... Phase 8 must not copy the loop as if it
// were a requirement."
//
// @par Real-Time Safety: allocation-free, lock-free, exception-free. The
//      scratch vectors were sized once in setupProcessing() and are indexed
//      through .data() / operator[], never .at() (which throws).
void Processor::renderSlice(float* outL, float* outR, std::size_t n) noexcept {
    // 2. Macros -> engine, and the Aether-owned half -> reverb.
    //
    //    Applied EVERY SLICE even at Phase 8's neutral macro defaults (FR-034):
    //    computeAetherTargets() is what pushes the reverb's eight controls, and
    //    "inert" describes the macro VALUES, not the push.
    macros_.apply(*engine_);                                      // macro_matrix.h:623
    applyAetherTargets(*reverb_, macros_.computeAetherTargets());  // :667 + FR-034a

    // 3. Voice sum only - no reverb, no output stage.
    engine_->processStereoBlock(dryL_.data(), dryR_.data(), n);  // engine.h:441

    // 4. The Layer 4 stage the engine cannot own.
    reverb_->processStereoBlock(dryL_.data(), dryR_.data(), wetL_.data(), wetR_.data(),
                                n);  // reverb.h:2164

    // 4b. FR-024a clause 3: master gain, ONCE PER OUTPUT SAMPLE, on the reverb
    //     return. Never advanceSamples(n) and never once per slice - a ramp
    //     advanced per slice is partition-dependent BY CONSTRUCTION and would
    //     fail SC-008's block-size gate for a correct implementation.
    //
    //     The placement is load-bearing: a post-limiter multiply is FORBIDDEN,
    //     because at master gain 2.0 it produces peaks up to ~1.78 and makes
    //     SC-006's ceiling bound unsatisfiable by construction.
    for (std::size_t s = 0; s < n; ++s) {
        const float g = masterGain_.process();  // smoother.h:197
        wetL_[s] *= g;
        wetR_[s] *= g;
    }

    // 5. Output stage IN PLACE on the reverb return: tape saturator -> true-peak
    //    limiter. ALWAYS LAST.
    engine_->processOutputStage(wetL_.data(), wetR_.data(), n);  // engine.h:512

    std::copy_n(wetL_.data(), n, outL);
    std::copy_n(wetR_.data(), n, outR);

    // 6. Bloom lifecycle. Note-OFFs BEFORE note-ONs (seraphis_chain.h:236-254):
    //    a steal issues both in the same batch, and running the note-on first
    //    would release the resonator bank the note-on had just claimed.
    const auto bloom = engine_->consumeBloomEvents();  // engine.h:654
    for (std::size_t v = 0; v < Krate::DSP::SeraphisEngine::kMaxVoices; ++v) {
        const std::uint32_t bit = std::uint32_t{1} << static_cast<std::uint32_t>(v);
        if ((bloom.noteOffMask & bit) != 0u) {
            reverb_->bloomNoteOff(static_cast<std::int32_t>(v));  // reverb.h:2473
        }
    }
    for (std::size_t v = 0; v < Krate::DSP::SeraphisEngine::kMaxVoices; ++v) {
        const std::uint32_t bit = std::uint32_t{1} << static_cast<std::uint32_t>(v);
        if ((bloom.noteOnMask & bit) == 0u) {
            continue;
        }
        std::size_t count = 0;
        engine_->collectHeldPartials(v, bloomPartials_.data(), bloomPartials_.size(),
                                     count);  // engine.h:596
        if (count > 0) {
            reverb_->bloomNoteOn(static_cast<std::int32_t>(v), bloomPartials_.data(),
                                 count);  // reverb.h:2392
        }
    }
}

}  // namespace Seraphis
