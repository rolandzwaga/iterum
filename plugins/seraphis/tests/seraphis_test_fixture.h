#pragma once

// ==============================================================================
// Seraphis - shared processor test fixture (T015, plan 4.2)
// ==============================================================================
// One fixture used by every audio-touching Seraphis case. It owns the
// processor, the output buffers (with guard words either side), the parameter-
// change stand-in and the event stand-in, so no case has to re-derive the VST3
// host-side plumbing.
//
// FR-067: the processor is held through a unique_ptr. The
// static_assert(sizeof(Processor) < 64 KiB) in processor.h is what would licence
// a stack local; the 771 968 B engine it owns lives on the heap regardless.
//
// ALLOCATION BEHAVIOUR (matters for the SC-026 no-alloc case): every container
// here grows on demand and is then REUSED - clear()/reset() keep capacity. So a
// render inside a TestHelpers::AllocationScope is allocation-free only once the
// buffers are warm: call reserveCapture() (and one warm-up render, or a prepare
// at the block size you will use) BEFORE opening the scope.
// ==============================================================================

#include "processor/processor.h"

#include <vst_event_list.h>

#include <pluginterfaces/vst/ivstaudioprocessor.h>
#include <pluginterfaces/vst/ivstevents.h>
#include <pluginterfaces/vst/ivstparameterchanges.h>
#include <pluginterfaces/vst/ivstprocesscontext.h>

#include <cstddef>
#include <initializer_list>
#include <memory>
#include <vector>

namespace SeraphisTest {

// -----------------------------------------------------------------------------
// MultiPointParamValueQueue - a parameter queue carrying an ARBITRARY number of
// automation points.
//
// The shared mock (tests/test_helpers/vst_param_changes.h:36-71) is single-point
// by design and documents that as its scope. A single-point queue cannot test
// FR-042's "using the LAST value of each parameter queue": with one point,
// getPoint(0) and getPoint(getPointCount() - 1) are the same call, so a wrong
// implementation is indistinguishable from a right one. Hence this local
// multi-point variant.
// -----------------------------------------------------------------------------
class MultiPointParamValueQueue final : public Steinberg::Vst::IParamValueQueue {
public:
    explicit MultiPointParamValueQueue(Steinberg::Vst::ParamID id) : id_(id) {}

    // Reuse the object (and its point storage) for another parameter.
    void reset(Steinberg::Vst::ParamID id) {
        id_ = id;
        points_.clear();  // keeps capacity
    }

    void addTestPoint(Steinberg::int32 sampleOffset, Steinberg::Vst::ParamValue value) {
        points_.push_back(Point{sampleOffset, value});
    }

    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID /*iid*/,
                                                 void** /*obj*/) override {
        return Steinberg::kNoInterface;
    }
    Steinberg::uint32 PLUGIN_API addRef() override { return 1; }
    Steinberg::uint32 PLUGIN_API release() override { return 1; }

    Steinberg::Vst::ParamID PLUGIN_API getParameterId() override { return id_; }

    Steinberg::int32 PLUGIN_API getPointCount() override {
        return static_cast<Steinberg::int32>(points_.size());
    }

    Steinberg::tresult PLUGIN_API getPoint(Steinberg::int32 index,
                                           Steinberg::int32& sampleOffset,
                                           Steinberg::Vst::ParamValue& value) override {
        if (index < 0 || index >= static_cast<Steinberg::int32>(points_.size())) {
            return Steinberg::kResultFalse;
        }
        const Point& p = points_[static_cast<std::size_t>(index)];
        sampleOffset = p.sampleOffset;
        value = p.value;
        return Steinberg::kResultTrue;
    }

    Steinberg::tresult PLUGIN_API addPoint(Steinberg::int32 /*sampleOffset*/,
                                           Steinberg::Vst::ParamValue /*value*/,
                                           Steinberg::int32& /*index*/) override {
        return Steinberg::kResultFalse;
    }

private:
    struct Point {
        Steinberg::int32 sampleOffset;
        Steinberg::Vst::ParamValue value;
    };

    Steinberg::Vst::ParamID id_;
    std::vector<Point> points_;
};

// -----------------------------------------------------------------------------
// ParameterChanges - container of multi-point queues.
//
// clear() does NOT destroy the queues: it drops the active count so the next
// block reuses the same objects and their point storage. Queue pointers are
// handed out only through getParameterData() during process(); never hold one
// across an addQueue() call.
// -----------------------------------------------------------------------------
class ParameterChanges final : public Steinberg::Vst::IParameterChanges {
public:
    void clear() noexcept { activeCount_ = 0; }

    MultiPointParamValueQueue& addQueue(Steinberg::Vst::ParamID id) {
        if (activeCount_ == queues_.size()) {
            queues_.emplace_back(id);  // grows only until warm
        }
        MultiPointParamValueQueue& q = queues_[activeCount_];
        ++activeCount_;
        q.reset(id);
        return q;
    }

    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID /*iid*/,
                                                 void** /*obj*/) override {
        return Steinberg::kNoInterface;
    }
    Steinberg::uint32 PLUGIN_API addRef() override { return 1; }
    Steinberg::uint32 PLUGIN_API release() override { return 1; }

    Steinberg::int32 PLUGIN_API getParameterCount() override {
        return static_cast<Steinberg::int32>(activeCount_);
    }

    Steinberg::Vst::IParamValueQueue* PLUGIN_API getParameterData(
        Steinberg::int32 index) override {
        if (index < 0 || index >= static_cast<Steinberg::int32>(activeCount_)) {
            return nullptr;
        }
        return &queues_[static_cast<std::size_t>(index)];
    }

    Steinberg::Vst::IParamValueQueue* PLUGIN_API addParameterData(
        const Steinberg::Vst::ParamID& /*id*/, Steinberg::int32& /*index*/) override {
        return nullptr;
    }

private:
    std::vector<MultiPointParamValueQueue> queues_;
    std::size_t activeCount_ = 0;
};

// -----------------------------------------------------------------------------
// ProcessorFixture
// -----------------------------------------------------------------------------
struct ProcessorFixture {
    // Guard words either side of every output channel buffer. A processor that
    // writes outside [0, numSamples) trips checkCanaries().
    static constexpr std::size_t kGuardWords = 8;
    static constexpr float kGuardValue = -8.5e17f;  // finite, never a legal sample

    std::unique_ptr<Seraphis::Processor> proc = std::make_unique<Seraphis::Processor>();

    // Filled by the caller (directly, or through pushEvent/setParam) before each
    // process() call; renderBlocks() clears them per block.
    Krate::Test::EventList events;
    ParameterChanges params;

    // renderBlocks() appends the rendered output here, per channel.
    std::vector<float> capturedL, capturedR;

    // -------------------------------------------------------------------------
    // Lifecycle
    // -------------------------------------------------------------------------
    // initialize(nullptr) -> setupProcessing -> setActive(true). Returns the
    // first non-ok result so a caller can REQUIRE(prepare(...) == kResultOk).
    Steinberg::tresult prepare(double sampleRate, Steinberg::int32 blockSize) {
        Steinberg::tresult r = proc->initialize(nullptr);
        if (r != Steinberg::kResultOk) {
            return r;
        }

        ensureCapacity(static_cast<std::size_t>(blockSize < 0 ? 0 : blockSize));

        Steinberg::Vst::ProcessSetup setup{};
        setup.processMode = Steinberg::Vst::kRealtime;
        setup.symbolicSampleSize = Steinberg::Vst::kSample32;
        setup.maxSamplesPerBlock = blockSize;
        setup.sampleRate = sampleRate;

        r = proc->setupProcessing(setup);
        if (r != Steinberg::kResultOk) {
            return r;
        }

        return proc->setActive(true);
    }

    // -------------------------------------------------------------------------
    // Parameter automation
    // -------------------------------------------------------------------------
    // One-point queue at sampleOffset 0 - the common case.
    void setParam(Steinberg::Vst::ParamID id, double normalized) {
        params.addQueue(id).addTestPoint(0, normalized);
    }

    // Multi-point queue. NOT optional: FR-042 says the processor must take the
    // LAST point of each queue, which a one-point queue cannot distinguish.
    void setParamPoints(Steinberg::Vst::ParamID id, std::initializer_list<double> values) {
        MultiPointParamValueQueue& q = params.addQueue(id);
        Steinberg::int32 offset = 0;
        for (double v : values) {
            q.addTestPoint(offset, v);
            ++offset;
        }
    }

    // -------------------------------------------------------------------------
    // Host transport (Phase 9, plan 7.0's "shared fixture change")
    // -------------------------------------------------------------------------
    // Phase 8 shipped `data_.processContext = nullptr` unconditionally, which
    // makes FR-056's host-synced morph travel untestable: Processor::
    // updateSyncedTravelRate only derives a rate when a ProcessContext carrying
    // kTempoValid arrives, so with no context every SC-018 clause would measure
    // the FALLBACK and clauses 1-4 could not be written at all.
    //
    // The fixture OWNS the context and attaches it from withOutputChannels().
    // Until setTempo() is called the behaviour is byte-for-byte the Phase 8 one:
    // useContext_ starts false, so processContext stays null - which is also
    // exactly SC-018 clause 5's first arm.
    void setTempo(double bpm, int sigNum, int sigDen, bool tempoValid, bool sigValid) noexcept {
        context_ = Steinberg::Vst::ProcessContext{};
        context_.tempo = bpm;
        context_.timeSigNumerator = static_cast<Steinberg::int32>(sigNum);
        context_.timeSigDenominator = static_cast<Steinberg::int32>(sigDen);

        Steinberg::uint32 flags = 0;
        if (tempoValid) {
            flags |= static_cast<Steinberg::uint32>(Steinberg::Vst::ProcessContext::kTempoValid);
        }
        if (sigValid) {
            flags |= static_cast<Steinberg::uint32>(Steinberg::Vst::ProcessContext::kTimeSigValid);
        }
        context_.state = flags;

        useContext_ = true;
    }

    // Detach the context again: the next block sees processContext == nullptr,
    // FR-056's stated fallback. Needed to test that a synced rate is not RETAINED
    // when the host stops supplying a transport (SC-018 clause 5).
    void clearProcessContext() noexcept { useContext_ = false; }

    // -------------------------------------------------------------------------
    // Events
    // -------------------------------------------------------------------------
    // `type` is a Steinberg::Vst::Event type constant (kNoteOnEvent /
    // kNoteOffEvent) so a case can push a velocity-0 note-on verbatim.
    void pushEvent(Steinberg::uint16 type, Steinberg::int16 pitch, float velocity,
                   Steinberg::int32 sampleOffset = 0) {
        Steinberg::Vst::Event e{};
        e.type = type;
        e.sampleOffset = sampleOffset;
        e.busIndex = 0;
        e.ppqPosition = 0.0;
        e.flags = 0;
        if (type == Steinberg::Vst::Event::kNoteOffEvent) {
            e.noteOff.channel = 0;
            e.noteOff.pitch = pitch;
            e.noteOff.velocity = velocity;
            e.noteOff.noteId = -1;
            e.noteOff.tuning = 0.0f;
        } else {
            e.noteOn.channel = 0;
            e.noteOn.pitch = pitch;
            e.noteOn.velocity = velocity;
            e.noteOn.noteId = -1;
            e.noteOn.tuning = 0.0f;
            e.noteOn.length = 0;
        }
        events.addEvent(e);
    }

    // -------------------------------------------------------------------------
    // ProcessData
    // -------------------------------------------------------------------------
    // A ProcessData whose channelBuffers32 array has EXACTLY `numChannels`
    // elements, so an out-of-bounds channel read is a real heap overflow under
    // ASan (SC-021's mono clause depends on that).
    Steinberg::Vst::ProcessData& withOutputChannels(int numChannels) {
        // Guarantees storageL_/storageR_ are non-empty, so audioL()/audioR()
        // (data() + kGuardWords) are real pointers even when a case builds a
        // degenerate ProcessData without going through prepare()/processBlock().
        ensureCapacity(blockSamples_);

        channelPtrs_.assign(static_cast<std::size_t>(numChannels < 0 ? 0 : numChannels), nullptr);
        if (numChannels > 0) {
            channelPtrs_[0] = audioL();
        }
        if (numChannels > 1) {
            channelPtrs_[1] = audioR();
        }
        // Any further channel stays null - the plugin declares stereo only.

        outBus_.numChannels = numChannels;
        outBus_.silenceFlags = 0;
        outBus_.channelBuffers32 = channelPtrs_.empty() ? nullptr : channelPtrs_.data();

        data_.processMode = Steinberg::Vst::kRealtime;
        data_.symbolicSampleSize = Steinberg::Vst::kSample32;
        data_.numSamples = static_cast<Steinberg::int32>(blockSamples_);
        data_.numInputs = 0;
        data_.inputs = nullptr;
        data_.numOutputs = 1;
        data_.outputs = &outBus_;
        data_.inputParameterChanges = &params;
        data_.outputParameterChanges = nullptr;
        data_.inputEvents = &events;
        data_.outputEvents = nullptr;
        // Phase 9: null until setTempo() is called, which is the Phase 8 shape.
        data_.processContext = useContext_ ? &context_ : nullptr;
        return data_;
    }

    // Stereo ProcessData for `numSamples`, then process() once. Events and
    // parameter queues queued so far are delivered and then cleared.
    Steinberg::tresult processBlock(Steinberg::int32 numSamples) {
        ensureCapacity(static_cast<std::size_t>(numSamples < 0 ? 0 : numSamples));
        blockSamples_ = static_cast<std::size_t>(numSamples < 0 ? 0 : numSamples);
        Steinberg::Vst::ProcessData& data = withOutputChannels(2);
        data.numSamples = numSamples;
        const Steinberg::tresult r = proc->process(data);
        events.clear();
        params.clear();
        return r;
    }

    // -------------------------------------------------------------------------
    // Buffers
    // -------------------------------------------------------------------------
    [[nodiscard]] float* audioL() noexcept { return storageL_.data() + kGuardWords; }
    [[nodiscard]] float* audioR() noexcept { return storageR_.data() + kGuardWords; }

    // Write a non-zero canary across the audio region. MANDATORY before any
    // "produces silence" assertion: without it the assertion passes because the
    // fixture zeroed its own vectors, not because the processor wrote silence.
    void seedOutputBuffers(float value) {
        for (std::size_t i = 0; i < capacity_; ++i) {
            audioL()[i] = value;
            audioR()[i] = value;
        }
    }

    // True while every guard word either side of both buffers is untouched.
    [[nodiscard]] bool checkCanaries() const noexcept {
        return guardsIntact(storageL_) && guardsIntact(storageR_);
    }

    // Pre-grow the capture vectors so a later renderBlocks() inside an
    // AllocationScope allocates nothing.
    void reserveCapture(std::size_t totalSamples) {
        capturedL.reserve(totalSamples);
        capturedR.reserve(totalSamples);
    }

    // -------------------------------------------------------------------------
    // Rendering
    // -------------------------------------------------------------------------
    // `script` is invoked before every block as
    // script(blockIndex, events, params) so a case can place note traffic and
    // automation per block. Output is APPENDED to capturedL / capturedR.
    template <typename Script>
    void renderBlocks(std::size_t numBlocks, std::size_t blockSize, Script&& script) {
        ensureCapacity(blockSize);
        reserveCapture(capturedL.size() + numBlocks * blockSize);

        for (std::size_t b = 0; b < numBlocks; ++b) {
            events.clear();
            params.clear();
            script(b, events, params);

            blockSamples_ = blockSize;
            Steinberg::Vst::ProcessData& data = withOutputChannels(2);
            data.numSamples = static_cast<Steinberg::int32>(blockSize);
            proc->process(data);

            for (std::size_t i = 0; i < blockSize; ++i) {
                capturedL.push_back(audioL()[i]);
                capturedR.push_back(audioR()[i]);
            }
        }
        events.clear();
        params.clear();
    }

    // Same, with no per-block script.
    void renderBlocks(std::size_t numBlocks, std::size_t blockSize) {
        renderBlocks(numBlocks, blockSize,
                     [](std::size_t, Krate::Test::EventList&, ParameterChanges&) {});
    }

private:
    void ensureCapacity(std::size_t samples) {
        if (samples <= capacity_ && !storageL_.empty()) {
            return;
        }
        capacity_ = samples;
        const std::size_t total = capacity_ + 2u * kGuardWords;
        storageL_.assign(total, 0.0f);
        storageR_.assign(total, 0.0f);
        writeGuards(storageL_);
        writeGuards(storageR_);
    }

    static void writeGuards(std::vector<float>& v) noexcept {
        for (std::size_t i = 0; i < kGuardWords; ++i) {
            v[i] = kGuardValue;
            v[v.size() - 1u - i] = kGuardValue;
        }
    }

    static bool guardsIntact(const std::vector<float>& v) noexcept {
        if (v.size() < 2u * kGuardWords) {
            return false;
        }
        for (std::size_t i = 0; i < kGuardWords; ++i) {
            if (v[i] != kGuardValue || v[v.size() - 1u - i] != kGuardValue) {
                return false;
            }
        }
        return true;
    }

    std::vector<float> storageL_, storageR_;  // guard | audio | guard
    std::vector<float*> channelPtrs_;         // EXACTLY n elements (see above)
    std::size_t capacity_ = 0;
    std::size_t blockSamples_ = 0;

    Steinberg::Vst::AudioBusBuffers outBus_{};
    Steinberg::Vst::ProcessData data_{};

    // Phase 9 transport hook - see setTempo() above.
    Steinberg::Vst::ProcessContext context_{};
    bool useContext_ = false;
};

}  // namespace SeraphisTest
