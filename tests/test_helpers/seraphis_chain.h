// ==============================================================================
// seraphis_chain.h -- the FR-070 composed Seraphis chain, as a test-only driver
// ==============================================================================
// specs/seraphis-phase7-voice-engine/spec.md   FR-070
// specs/seraphis-phase7-voice-engine/plan.md   S5 (and D4/D5 for the ordering)
//
// AetherReverb is Layer 4 and SeraphisEngine is Layer 3, so the engine does NOT
// own the reverb: the CALLER composes
//
//     SeraphisEngine::processStereoBlock  ->  AetherReverb::processStereoBlock
//                                         ->  SeraphisEngine::processOutputStage
//
// and pushes the SeraphisAetherTargets POD plus the bloom lifecycle across the
// layer boundary itself. This header is that caller, written once so every
// "the composed chain" criterion drives the same code, and so Phase 8's
// processor has a literal model to reproduce.
//
// LAYER LINT: tools/lint-layers.js scans only dsp/include/krate/dsp, so the
//   effects/ include below is out of scope by construction and needs no
//   exclusion (plan S0.1).
//
// TWO TIMING RULES, BOTH LOAD-BEARING (plan S5):
//
//  1. SUB-DIVIDE EVERY CALLER BLOCK AT EVENT BOUNDARIES. Events are not
//     dispatched at the head of the block that contains them. Each block is
//     split at every event's resolved sample index: dispatch what is due at
//     that index, then render to the next event index or the block end.
//     SeraphisEngine::noteOn/noteOff have no sample-offset parameter, and
//     sub-division is how a sample-accurate offset is delivered without one --
//     exactly what Phase 8's event loop does with the host's sampleOffset.
//     Without it SC-014 cannot pass with any non-trivial script: an event at
//     sample S would fire at ceil(S/B)*B, which agrees across the partition set
//     {1, 7, 64, 65, 512, 4096} only when S is a multiple of 1 863 680 samples
//     (~38.8 s @ 48 kHz).
//
//  2. SECONDS, NOT SAMPLES. Event::seconds is resolved per render through
//     toSamples(seconds, rate). A sample-denominated script is a DIFFERENT
//     piece of music at 44.1 / 48 / 96 kHz, so SC-013 would not be comparing
//     the same script at all.
//
// ALLOCATION CONTRACT (SC-007 depends on it): outL/outR, the dry/wet scratch
//   and the resolved event indices are all sized ONCE, before the render loop.
//   std::vector::resize is never called from inside it, and the bloom partial
//   buffer is a std::array held across the whole render rather than a
//   per-slice local. SC-007 is a RUNTIME detector (AllocationScope), which a
//   grep exemption would not help: an allocation in here would be reported as
//   an engine defect.
// ==============================================================================
#pragma once

#include <krate/dsp/effects/aether_reverb.h>
#include <krate/dsp/systems/seraphis_engine.h>
#include <krate/dsp/systems/seraphis_macro_matrix.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace Krate {
namespace DSP {
namespace TestUtils {

/// @brief A time-stamped note/parameter script for renderSeraphisChain.
///
/// TEST-ONLY: the std::vector here is fine. SC-008's heap sweep scans the three
/// shipped Layer 3 headers, not this file.
struct SeraphisChainScript {
    struct Event {
        /// SECONDS, never samples -- see timing rule 2 in this file's banner.
        double seconds = 0.0;

        enum class Kind : std::uint8_t {
            NoteOn = 0,   ///< engine.noteOn(note, velocity)
            NoteOff,      ///< engine.noteOff(note)
            Freeze,       ///< engine.setAtmosphereFreeze(value != 0)
            Polyphony     ///< engine.setPolyphony(value)
        };

        Kind kind = Kind::NoteOn;
        std::uint8_t note = 60;
        std::uint8_t velocity = 100;
        std::size_t value = 0;
    };

    std::vector<Event> events;

    /// @brief Resolve a script time to an absolute sample index at this render's rate.
    ///
    /// Events MUST already be sorted by `seconds`; renderSeraphisChain asserts
    /// that, and asserts the resolved indices are non-decreasing.
    [[nodiscard]] static std::uint64_t toSamples(double seconds, double sampleRate) noexcept {
        // Written as `!(x > 0)` rather than a std::isnan test: the macOS leg
        // builds with -ffast-math, under which std::isnan may be folded away.
        if (!(seconds > 0.0) || !(sampleRate > 0.0)) {
            return 0u;
        }
        const double index = std::floor((seconds * sampleRate) + 0.5);
        if (!(index > 0.0)) {
            return 0u;
        }
        return static_cast<std::uint64_t>(index);
    }
};

namespace detail {

/// One script event onto the engine. Kept out of renderSeraphisChain's body so
/// the render loop reads as the six-step order the plan pins.
inline void dispatchSeraphisChainEvent(SeraphisEngine& engine,
                                       const SeraphisChainScript::Event& event) noexcept {
    switch (event.kind) {
        case SeraphisChainScript::Event::Kind::NoteOn:
            engine.noteOn(event.note, event.velocity);
            break;
        case SeraphisChainScript::Event::Kind::NoteOff:
            engine.noteOff(event.note);
            break;
        case SeraphisChainScript::Event::Kind::Freeze:
            engine.setAtmosphereFreeze(event.value != std::size_t{0});
            break;
        case SeraphisChainScript::Event::Kind::Polyphony:
            engine.setPolyphony(event.value);
            break;
    }
}

}  // namespace detail

/// @brief Render the FR-070 chain: voice sum -> AetherReverb -> output stage.
///
/// @param engine       Prepared engine. Its note/polyphony/freeze surface is
///                     driven by `script`.
/// @param reverb       Prepared reverb. Its eight macro-owned controls are
///                     pushed per sub-slice from macros.computeAetherTargets(),
///                     and its bloom lifecycle is driven from
///                     engine.consumeBloomEvents().
/// @param macros       Applied per sub-slice; FR-059 makes that idempotent.
/// @param script       Sorted by `seconds`. See the two timing rules above.
/// @param sampleRate   Used only to resolve `script`'s event times.
/// @param blockSize    The CALLER block size. Sub-slices are never larger.
/// @param totalSamples Render length; outL/outR are resized to it.
/// @param outL,outR    Output, resized once before the loop.
inline void renderSeraphisChain(SeraphisEngine& engine, AetherReverb& reverb,
                                const SeraphisMacroMatrix& macros,
                                const SeraphisChainScript& script, double sampleRate,
                                std::size_t blockSize, std::size_t totalSamples,
                                std::vector<float>& outL, std::vector<float>& outR) {
    outL.assign(totalSamples, 0.0f);
    outR.assign(totalSamples, 0.0f);
    if ((totalSamples == std::size_t{0}) || (blockSize == std::size_t{0})) {
        return;
    }

    // --- everything the render loop touches is sized HERE, once ---------------
    std::vector<std::size_t> eventAt(script.events.size(), std::size_t{0});
    std::size_t previous = 0;
    for (std::size_t e = 0; e < script.events.size(); ++e) {
        if (e > 0) {
            // The sortedness precondition. Debug-only, so the Release render
            // stays well formed on a malformed script: the std::max below
            // clamps the resolved indices to be non-decreasing regardless.
            assert(script.events[e].seconds >= script.events[e - 1].seconds
                   && "SeraphisChainScript events must be sorted by seconds");
        }
        const std::uint64_t raw =
            SeraphisChainScript::toSamples(script.events[e].seconds, sampleRate);
        // Past the end of the render is representable: an index of totalSamples
        // is never <= any sliceStart, so such an event simply never fires.
        const std::size_t capped =
            static_cast<std::size_t>(std::min(raw, static_cast<std::uint64_t>(totalSamples)));
        assert(capped >= previous && "resolved event indices must be non-decreasing");
        const std::size_t resolved = std::max(previous, capped);
        eventAt[e] = resolved;
        previous = resolved;
    }

    std::vector<float> dryL(blockSize, 0.0f);
    std::vector<float> dryR(blockSize, 0.0f);
    std::vector<float> wetL(blockSize, 0.0f);
    std::vector<float> wetR(blockSize, 0.0f);
    // Held across the whole render, not a per-slice local (allocation contract).
    std::array<float, SeraphisEngine::kBloomPartialCap> buf{};

    std::size_t nextEvent = 0;
    std::size_t blockStart = 0;
    while (blockStart < totalSamples) {
        const std::size_t blockEnd = std::min(blockStart + blockSize, totalSamples);
        std::size_t sliceStart = blockStart;
        while (sliceStart < blockEnd) {
            // 1. dispatch the events due at this slice's start ----------------
            while ((nextEvent < eventAt.size()) && (eventAt[nextEvent] <= sliceStart)) {
                detail::dispatchSeraphisChainEvent(engine, script.events[nextEvent]);
                ++nextEvent;
            }

            // Timing rule 1: split at the next event, never past it.
            std::size_t sliceEnd = blockEnd;
            if ((nextEvent < eventAt.size()) && (eventAt[nextEvent] < blockEnd)) {
                sliceEnd = eventAt[nextEvent];
            }
            const std::size_t n = sliceEnd - sliceStart;
            if (n == std::size_t{0}) {
                // Only reachable if an event resolved to sliceStart and was not
                // consumed above, which the dispatch loop makes impossible.
                break;
            }

            // 2. macros -> engine, and the Aether-owned half -> reverb --------
            macros.apply(engine);
            const SeraphisAetherTargets at = macros.computeAetherTargets();
            reverb.setMix(at.mix);                                          // :2336
            reverb.setSize(at.size);                                        // :2208
            reverb.setWidth(at.width);                                      // :2333
            reverb.setShimmerOctaveSend(at.shimmerOctaveSend);              // :2280
            reverb.setShimmerFifthSend(at.shimmerFifthSend);                // :2285
            reverb.setBloomSend(at.bloomSend);                              // :2295
            reverb.setSizeBreathDepth(at.sizeBreathDepth);                  // :2320
            reverb.setDimensionalityTideDepth(at.dimensionalityTideDepth);  // :2328

            // 3. voice sum (pre-reverb) --------------------------------------
            engine.processStereoBlock(dryL.data(), dryR.data(), n);

            // 4. the Layer 4 stage the engine cannot own ----------------------
            reverb.processStereoBlock(dryL.data(), dryR.data(), wetL.data(), wetR.data(), n);

            // 5. output stage, IN PLACE on the reverb return -------------------
            engine.processOutputStage(wetL.data(), wetR.data(), n);

            std::copy_n(wetL.data(), n, outL.data() + sliceStart);
            std::copy_n(wetR.data(), n, outR.data() + sliceStart);

            // 6. bloom lifecycle -- AFTER the render, by plan D4/D5 ------------
            const SeraphisEngine::BloomEvents bloom = engine.consumeBloomEvents();
            for (std::size_t v = 0; v < SeraphisEngine::kMaxVoices; ++v) {
                const std::uint32_t bit = std::uint32_t{1} << static_cast<std::uint32_t>(v);
                if ((bloom.noteOffMask & bit) != 0u) {
                    reverb.bloomNoteOff(static_cast<std::int32_t>(v));  // :2473
                }
            }
            for (std::size_t v = 0; v < SeraphisEngine::kMaxVoices; ++v) {
                const std::uint32_t bit = std::uint32_t{1} << static_cast<std::uint32_t>(v);
                if ((bloom.noteOnMask & bit) == 0u) {
                    continue;
                }
                std::size_t count = 0;
                engine.collectHeldPartials(v, buf.data(), buf.size(), count);
                if (count > std::size_t{0}) {
                    reverb.bloomNoteOn(static_cast<std::int32_t>(v), buf.data(), count);  // :2392
                }
            }

            sliceStart = sliceEnd;
        }
        blockStart = blockEnd;
    }
}

}  // namespace TestUtils
}  // namespace DSP
}  // namespace Krate
