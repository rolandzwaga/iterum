// ==============================================================================
// Layer 3: System Tests - SeraphisEngine per-partial fan-outs (Phase 11, T003)
// ==============================================================================
// Constitution Principle XII: Test-First Development.
//
// Reference: specs/seraphis-phase11-ui/spec.md
//            specs/seraphis-phase11-ui/plan.md   (§2.1, §2.2)
//            specs/seraphis-phase11-ui/tasks.md  (T003 creates this TU)
//
// SCOPE OF THIS TU: exactly the three SeraphisEngine per-partial fan-outs added
//   by T003 - setPartialPositionAllVoices, setPartialMaskAllVoices and
//   clearPartialMaskAllVoices - plus the three SeraphisVoice pass-throughs they
//   ride on. The phase owner asked for a DEDICATED file rather than folding the
//   cases into seraphis_engine_test.cpp.
//
// MASK POLARITY - the single most important fact in this file:
//   HarmonicCloud::setPartialMask(index, active)'s body is `masked_[index] =
//   !active;` (dsp/include/krate/dsp/systems/harmonic_cloud.h:1084-1089), so
//     active == true  => AUDIBLE
//     active == false => SILENCED
//   and clearPartialMask() is `masked_.fill(false)` (:1101) => everything
//   audible. An implementation that silences on `true` passes the pan arm and
//   fails the mask arm's RECOVERY assertion, which is exactly why that
//   assertion is here.
//
// STACK RULE (seraphis-phase7 plan §6.3): a SeraphisEngine is ~750 KB
//   (16 x 47 616 B of voices) and must NEVER be a test local - MSVC's default
//   main-thread stack is 1 MiB and dsp/tests/CMakeLists.txt sets no /STACK.
//   Every engine here is heap-allocated through makeEngine().
//
// ALLOCATION DETECTION: deliberately NOT included. The single owner of the
//   global operator new/delete replacements in dsp_systems_tests is
//   unit/systems/selectable_oscillator_test.cpp:388.
//
// COMPILE FLAGS: this TU is NOT listed under "-fno-fast-math
//   -fno-finite-math-only" in dsp/tests/CMakeLists.txt and must not be. The
//   non-finite position in the rejection arm is therefore built from a BIT
//   PATTERN through a volatile sink, never from
//   std::numeric_limits<float>::quiet_NaN(), which folds to finite garbage
//   under the macOS leg's -ffast-math.
// ==============================================================================

#include <catch2/catch_all.hpp>

#include <krate/dsp/systems/harmonic_cloud.h>
#include <krate/dsp/systems/seraphis_engine.h>
#include <krate/dsp/systems/seraphis_voice.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <vector>

using Krate::DSP::HarmonicCloud;
using Krate::DSP::SeraphisEngine;
using Krate::DSP::SeraphisEngineConfig;
using Krate::DSP::SeraphisVoiceConfig;

using Catch::Approx;

namespace {

constexpr double kSr = 48000.0;
constexpr std::size_t kBlock = 512;

/// Heap-allocate, prepare, return. See the STACK RULE banner above.
[[nodiscard]] std::unique_ptr<SeraphisEngine> makeEngine(std::size_t polyphony) {
    auto engine = std::make_unique<SeraphisEngine>();
    // Designated initialisers throughout - no narrowing in brace init.
    engine->prepare(kSr, SeraphisEngineConfig{.voice = SeraphisVoiceConfig{},
                                              .polyphony = polyphony,
                                              .seed = 1u});
    return engine;
}

void renderSeconds(SeraphisEngine& engine, double seconds) {
    const auto total = static_cast<std::size_t>(seconds * kSr);
    std::vector<float> left(kBlock, 0.0f);
    std::vector<float> right(kBlock, 0.0f);
    std::size_t done = 0;
    while (done < total) {
        const std::size_t take = std::min(kBlock, total - done);
        engine.processStereoBlock(left.data(), right.data(), take);
        done += take;
    }
}

/// @brief Build a non-finite float from its bit pattern through a volatile sink.
///
/// The volatile READ is the sink: it is what stops the constant from being
/// folded back into the memcpy at compile time under -ffast-math.
[[nodiscard]] float makeNonFinite(std::uint32_t bits) noexcept {
    volatile std::uint32_t b = bits;
    const std::uint32_t materialized = b;
    float f = 0.0f;
    std::memcpy(&f, &materialized, sizeof(f));
    return f;
}

constexpr std::uint32_t kQuietNaNBits = 0x7FC00000u;
constexpr std::uint32_t kPositiveInfBits = 0x7F800000u;

/// The engine's whole slot pool, snapshotted as raw pan positions.
using PositionSnapshot =
    std::array<std::array<float, HarmonicCloud::kMaxPartials>, SeraphisEngine::kMaxVoices>;

[[nodiscard]] PositionSnapshot snapshotPositions(const SeraphisEngine& engine) {
    PositionSnapshot snap{};
    for (std::size_t v = 0; v < SeraphisEngine::kMaxVoices; ++v) {
        const HarmonicCloud& cloud = engine.getVoice(v).cloud();
        for (std::size_t i = 0; i < HarmonicCloud::kMaxPartials; ++i) {
            snap[v][i] = cloud.getPartialPosition(i);
        }
    }
    return snap;
}

}  // namespace

// =============================================================================
// T003 - the fan-outs reach EVERY slot, in the API's real mask polarity
// =============================================================================

TEST_CASE("SeraphisEngine_PartialFanOut_ReachesEveryVoice", "[seraphis_engine][phase11]") {
    SECTION("Pan arm - every one of the sixteen slots carries the override") {
        auto engine = makeEngine(SeraphisEngine::kMaxVoices);

        engine->setPartialPositionAllVoices(7, 0.8f);

        // kMaxVoices, not getPolyphony(): a slot the allocator hands out LATER
        // must already carry the override, which is the whole reason the fan-out
        // loops the pool rather than the active count.
        for (std::size_t v = 0; v < SeraphisEngine::kMaxVoices; ++v) {
            INFO("voice slot " << v);
            const HarmonicCloud& cloud = engine->getVoice(v).cloud();
            CHECK(cloud.getPartialPosition(7) == Approx(0.8f).margin(1e-6f));
        }

        // The same holds on a pool whose polyphony is 1: the fan-out is over
        // kMaxVoices regardless of how many slots the allocator may hand out.
        auto mono = makeEngine(1);
        mono->setPartialPositionAllVoices(7, -0.4f);
        for (std::size_t v = 0; v < SeraphisEngine::kMaxVoices; ++v) {
            INFO("mono voice slot " << v);
            CHECK(mono->getVoice(v).cloud().getPartialPosition(7)
                  == Approx(-0.4f).margin(1e-6f));
        }
    }

    SECTION("Mask arm - active == true means AUDIBLE, and a masked partial recovers") {
        auto engine = makeEngine(SeraphisEngine::kMaxVoices);

        // Sixteen held notes so all sixteen slots are actually rendering: the
        // mask observable (targetAmplitude_) is only written by the cloud's
        // control step, which idle slots never reach.
        for (std::uint8_t n = 0; n < 16; ++n) {
            engine->noteOn(static_cast<std::uint8_t>(48 + n), static_cast<std::uint8_t>(100));
        }
        // 200 ms puts every per-partial envelope past its attack and into HOLD
        // (harmonic_cloud.h:1632-1634), where env == 1.0 and the amplitude
        // targets are stable for as long as the gate stays on. Decay IS the
        // release (:1594-1596), so nothing fades while the notes are held.
        renderSeconds(*engine, 0.2);
        REQUIRE(engine->getActiveVoiceCount() == SeraphisEngine::kMaxVoices);

        std::array<float, SeraphisEngine::kMaxVoices> before3{};
        for (std::size_t v = 0; v < SeraphisEngine::kMaxVoices; ++v) {
            INFO("voice slot " << v);
            const HarmonicCloud& cloud = engine->getVoice(v).cloud();
            // Richness defaults to 0.60 (seraphis_voice.h:290) => N = round(64^0.6)
            // = 12 active partials, so indices 3, 5 and 9 are all live.
            REQUIRE(cloud.getActivePartialCount() >= std::size_t{10});
            REQUIRE(cloud.getPartialTargetAmplitude(3) > 0.0f);
            REQUIRE(cloud.getPartialCurrentAmplitude(3) > 0.0f);
            before3[v] = cloud.getPartialCurrentAmplitude(3);
        }

        // active = false => SILENCED.
        engine->setPartialMaskAllVoices(3, false);
        renderSeconds(*engine, 0.1);
        for (std::size_t v = 0; v < SeraphisEngine::kMaxVoices; ++v) {
            INFO("voice slot " << v);
            const HarmonicCloud& cloud = engine->getVoice(v).cloud();
            // harmonic_cloud.h:1750 - masking is the LAST factor of the chain and
            // forces the target to exactly zero.
            CHECK(cloud.getPartialTargetAmplitude(3) == 0.0f);
            // 100 ms against the 2 ms FR-014 smoother (kAmpSmoothTimeSec,
            // harmonic_cloud.h:165) is 50 time constants: e^-50 ~= 2e-22, and the
            // denormal guard at :1786 snaps it to zero outright.
            CHECK(cloud.getPartialCurrentAmplitude(3) < before3[v] * 0.01f);
            // The neighbour is untouched - the fan-out edits ONE index.
            CHECK(cloud.getPartialTargetAmplitude(4) > 0.0f);
        }

        // active = true => AUDIBLE again. THIS is the assertion an implementation
        // with inverted polarity fails; it would pass every arm above.
        engine->setPartialMaskAllVoices(3, true);
        renderSeconds(*engine, 0.1);
        for (std::size_t v = 0; v < SeraphisEngine::kMaxVoices; ++v) {
            INFO("voice slot " << v);
            const HarmonicCloud& cloud = engine->getVoice(v).cloud();
            CHECK(cloud.getPartialTargetAmplitude(3) > 0.0f);
            CHECK(cloud.getPartialCurrentAmplitude(3) > 0.0f);
        }

        // clearPartialMaskAllVoices() => everything audible on every slot.
        engine->setPartialMaskAllVoices(3, false);
        engine->setPartialMaskAllVoices(5, false);
        engine->setPartialMaskAllVoices(9, false);
        renderSeconds(*engine, 0.05);
        for (std::size_t v = 0; v < SeraphisEngine::kMaxVoices; ++v) {
            INFO("voice slot " << v);
            const HarmonicCloud& cloud = engine->getVoice(v).cloud();
            CHECK(cloud.getPartialTargetAmplitude(3) == 0.0f);
            CHECK(cloud.getPartialTargetAmplitude(5) == 0.0f);
            CHECK(cloud.getPartialTargetAmplitude(9) == 0.0f);
        }

        engine->clearPartialMaskAllVoices();
        renderSeconds(*engine, 0.05);
        for (std::size_t v = 0; v < SeraphisEngine::kMaxVoices; ++v) {
            INFO("voice slot " << v);
            const HarmonicCloud& cloud = engine->getVoice(v).cloud();
            CHECK(cloud.getPartialTargetAmplitude(3) > 0.0f);
            CHECK(cloud.getPartialTargetAmplitude(5) > 0.0f);
            CHECK(cloud.getPartialTargetAmplitude(9) > 0.0f);
        }
    }

    SECTION("Rejection arm - the fan-outs add NO second guard") {
        auto engine = makeEngine(SeraphisEngine::kMaxVoices);

        // A negative position must survive intact. HarmonicCloud clamps to
        // [-1, +1] (harmonic_cloud.h:1076); a pass-through that grew its own
        // [0, 1] clamp would fail here.
        engine->setPartialPositionAllVoices(11, -0.6f);
        for (std::size_t v = 0; v < SeraphisEngine::kMaxVoices; ++v) {
            INFO("voice slot " << v);
            CHECK(engine->getVoice(v).cloud().getPartialPosition(11)
                  == Approx(-0.6f).margin(1e-6f));
        }

        const PositionSnapshot before = snapshotPositions(*engine);

        // Out-of-range indices: rejected by the OWNER (harmonic_cloud.h:1070-1072,
        // :1085-1087), so the fan-outs must simply pass them through and change
        // nothing anywhere.
        engine->setPartialPositionAllVoices(HarmonicCloud::kMaxPartials, 0.9f);
        engine->setPartialPositionAllVoices(std::numeric_limits<std::size_t>::max(), 0.9f);
        engine->setPartialMaskAllVoices(HarmonicCloud::kMaxPartials, false);
        engine->setPartialMaskAllVoices(std::numeric_limits<std::size_t>::max(), false);

        // Non-finite position at a VALID index: rejected by the owner
        // (harmonic_cloud.h:1073-1075). A fan-out that substituted 0.0f instead
        // of forwarding would move the stored value and fail here.
        engine->setPartialPositionAllVoices(11, makeNonFinite(kQuietNaNBits));
        engine->setPartialPositionAllVoices(11, makeNonFinite(kPositiveInfBits));

        const PositionSnapshot after = snapshotPositions(*engine);
        for (std::size_t v = 0; v < SeraphisEngine::kMaxVoices; ++v) {
            for (std::size_t i = 0; i < HarmonicCloud::kMaxPartials; ++i) {
                INFO("voice slot " << v << ", partial " << i);
                CHECK(after[v][i] == before[v][i]);
            }
        }
        // Belt and braces on the index the rejected writes targeted.
        for (std::size_t v = 0; v < SeraphisEngine::kMaxVoices; ++v) {
            INFO("voice slot " << v);
            CHECK(engine->getVoice(v).cloud().getPartialPosition(11)
                  == Approx(-0.6f).margin(1e-6f));
        }
    }
}
