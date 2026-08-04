// ==============================================================================
// Seraphis - the C-5 edit channel (Phase 11, T010)
// ==============================================================================
// Reference: specs/seraphis-phase11-ui/spec.md   (C-5, C-6, FR-033a, FR-036)
//            specs/seraphis-phase11-ui/plan.md   (section 6.2; tasks.md T010)
//
// CRITERIA OWNED BY THIS TU:
//   SC-018  Processor::notify rejects garbage without corrupting a slot, the
//           staging ring or the render.
//   SC-025  the Blend gesture is ABSOLUTE, not compounding (Q2), and a kind-4
//           with no live kind-7 snapshot is DROPPED.
//   SC-028  a ratio edit reaches a CURRENTLY SOUNDING voice - the criterion
//           T003a's gate relaxation exists for - and does NOT increment
//           SeraphisVoice::getRejectedConfigureTimeCallCount().
//   SC-029  that live edit is CLICK-FREE, against Phase 3's OWN per-chunk
//           bounds, referenced by name from spectral_morph_engine.h and never
//           restated as literals.
//   SC-014  (T011) the per-partial override TABLE survives every clearing
//           event, INCLUDING a macro-ring sweep that writes CloudStereoSpread
//           through the matrix with ParamID 207 held still (arm 6).
//   SC-033  (T011) a mask toggled back OFF restores the voice, end to end -
//           the arm a re-push body that walks only the SET mask bits fails.
//
// -fno-fast-math IS REQUIRED FOR THIS TU (plugins/seraphis/tests/CMakeLists.txt).
// SC-018 injects bit-pattern NaN/Inf EditMessage payloads and asserts on the
// rejection, and SC-029's per-chunk step statistic must not be reshaped by
// fast-math contraction. Even so, NOTHING here calls std::isnan / std::isinf /
// std::numeric_limits<>::infinity(): every non-finite value is BUILT from an
// exponent bit pattern through a volatile sink, and every finiteness test reads
// the exponent field back. That keeps the TU honest if the flag is ever lost.
//
// NO CHECKED-IN FLOAT GOLDEN. Every number asserted here is an integer count, a
// byte comparison of two POD payloads produced in this same process, a measured
// cents offset against a tolerance the spec states, or a bound named from a DSP
// header.
// ==============================================================================

#include "processor/cloud_frame.h"
#include "processor/processor.h"
#include "seraphis_test_fixture.h"
#include "ui/edit_message.h"

// T018 / SC-024 ONLY. The Q6 criterion is a statement about the CloudView
// gesture's inverse map, so the arm has to drive the real view against a real
// controller and then push the resulting message through the shipping wire.
#include "controller/controller.h"
#include "ui/cloud_view.h"

#include "plugin_ids.h"

#include "public.sdk/source/vst/hosting/hostclasses.h"

#include <pluginterfaces/base/smartpointer.h>       // Steinberg::owned
#include <pluginterfaces/vst/ivstaudioprocessor.h>  // ProcessSetup (the re-prepare arm)
#include <pluginterfaces/vst/ivstmessage.h>         // IMessage / IAttributeList

#include <krate/dsp/primitives/fft.h>
#include <krate/dsp/processors/spectral_state.h>
#include <krate/dsp/systems/harmonic_cloud.h>
#include <krate/dsp/systems/seraphis_engine.h>
#include <krate/dsp/systems/spectral_morph_engine.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <random>
#include <vector>

namespace {

using Catch::Approx;
using SeraphisTest::ProcessorFixture;
using Krate::DSP::SpectralState;

constexpr double kSampleRate = 48000.0;
constexpr Steinberg::int32 kBlock = 512;
/// SC-029 observes the morph engine once per ABSOLUTE 64-sample control chunk,
/// so the render is driven one chunk at a time and each process() call maps to
/// exactly one chunk (harmonic_cloud.h:144, seraphis_engine.h:213).
constexpr Steinberg::int32 kChunk = 64;

constexpr float kHostVelocity = 0.75f;
constexpr std::size_t kPartials = SpectralState::kStatePartials;

/// FR-043's mapping: clamp(int(v * 15 + 1.5), 1, 16), so voices n -> (n-1)/15.
constexpr double polyphonyNorm(int voices) {
    return static_cast<double>(voices - 1) / 15.0;
}

constexpr double linNorm(double plain, double mn, double mx) {
    return (mx > mn) ? ((plain - mn) / (mx - mn)) : 0.0;
}

// -----------------------------------------------------------------------------
// Non-finite values, and finiteness, BY BIT PATTERN ONLY
// -----------------------------------------------------------------------------

/// Build a float from its object representation, through a `volatile` sink so no
/// optimiser (fast-math included) can constant-fold a NaN/Inf pattern back into
/// a finite value at the point of use.
[[nodiscard]] float floatFromBits(std::uint32_t bits) noexcept {
    volatile std::uint32_t sink = bits;
    const std::uint32_t held = sink;
    float value = 0.0f;
    std::memcpy(&value, &held, sizeof(value));
    return value;
}

/// 0x7F800000 is the all-ones exponent field shared by +/-inf and every NaN.
[[nodiscard]] bool isFiniteBits(float x) noexcept {
    std::uint32_t bits = 0u;
    std::memcpy(&bits, &x, sizeof(bits));
    return (bits & 0x7F800000u) != 0x7F800000u;
}

[[nodiscard]] bool allFiniteBits(const std::vector<float>& v) noexcept {
    return std::all_of(v.begin(), v.end(), [](float s) { return isFiniteBits(s); });
}

constexpr std::uint32_t kQuietNaNBits = 0x7FC00000u;
constexpr std::uint32_t kPosInfBits = 0x7F800000u;
constexpr std::uint32_t kNegInfBits = 0xFF800000u;

// -----------------------------------------------------------------------------
// The wire: one HostMessage per send, exactly the shape the controller uses
// -----------------------------------------------------------------------------

[[nodiscard]] Seraphis::UI::EditMessage makeEdit(std::uint8_t kind, std::uint8_t slot,
                                                 std::uint16_t index, float a, float b) {
    Seraphis::UI::EditMessage m{};
    m.kind = kind;
    m.slot = slot;
    m.index = index;
    m.a = a;
    m.b = b;
    return m;
}

/// Hand `m` to Processor::notify as ONE binary attribute on the "SeraphisEdit"
/// message ID - byte for byte what Controller sends.
Steinberg::tresult sendEdit(Seraphis::Processor& processor,
                            const Seraphis::UI::EditMessage& m) {
    auto message = Steinberg::owned(new Steinberg::Vst::HostMessage());
    message->setMessageID(Seraphis::UI::kSeraphisEditMessageId);
    Steinberg::Vst::IAttributeList* attributes = message->getAttributes();
    REQUIRE(attributes != nullptr);
    REQUIRE(attributes->setBinary(Seraphis::UI::kSeraphisEditAttributeId, &m,
                                  static_cast<Steinberg::uint32>(sizeof(m)))
            == Steinberg::kResultOk);
    return processor.notify(message);
}

/// MEMBER-WISE, never memcmp over the object representation. SpectralState is
/// mostly `float`, and a float has no unique object representation (-0.0f vs
/// +0.0f compare equal but differ in bits; two NaN payloads compare unequal but
/// may be the same value's encoding), so a byte compare asks a different
/// question from the one every call site here means. The subject is always a
/// state that passed isValidSpectralState - which rejects non-finite outright
/// (spectral_state.h:82-...) - so the two coincide in practice; the point is
/// that this spelling is the one that stays correct if that ever stops holding.
/// std::array's operator== is element-wise, so the arrays need no loop.
[[nodiscard]] bool statesEqual(const SpectralState& a, const SpectralState& b) noexcept {
    return a.ratios == b.ratios && a.amplitudes == b.amplitudes && a.name == b.name
           && a.tiltDbPerOct == b.tiltDbPerOct && a.inharmonicity == b.inharmonicity
           && a.numPartials == b.numPartials;
}

// -----------------------------------------------------------------------------
// Spectral isolation - the conditioning SC-028's cents measurement needs
// -----------------------------------------------------------------------------
//
// Every push below removes a term that would move partial 0's RENDERED frequency
// away from `fundamentalHz * ratios[0]`, or would bury it under a wideband floor:
//
//   inharmonicity - HarmonicCloud stretches every partial by
//                   sqrt(1 + inharmonicity * n^2) (harmonic_cloud.h:1331). At the
//                   registered 0.03 that is +25.6 cents on n = 1 alone, five
//                   times SC-028's whole tolerance.
//   drift depth   - BrownianDrift multiplies the frequency per partial
//                   (getPartialDriftDetune, :991). Q6/SC-024 exclude it from the
//                   authoring map for the same reason.
//   morph entropy - EntropyProcessor scatters the morph OUTPUT ratios, i.e. the
//                   very array the edit writes.
//   mutation      - a second per-partial perturbation on the same grid.
//   gravity       - warps the ratio grid by exp2(g * range * log2 n). It is the
//                   identity at n = 1, so partial 0 is unaffected either way;
//                   zeroed anyway so the pre-edit and post-edit branches of
//                   harmonic_cloud.h:1312 agree about what "1.0" means.
//   body / atmos / aether - three parallel voices on the same output bus.
//   soft limit + master gain - TapeSaturator's intermodulation products are the
//                   detector's noise floor, not a trim (macro_wiring_test.cpp's
//                   kSoftLimitOff / kPreLimiterHeadroom record the measurement).
void conditionForSpectralIsolation(ProcessorFixture& fx) {
    using namespace Seraphis;
    fx.setParam(kSoftLimitId, 0.0);
    fx.setParam(kMasterGainId, 0.05);  // linear 0.1 - 20 dB of pre-limiter headroom
    fx.setParam(kBodyMixId, linNorm(0.0, kBodyMixMin, kBodyMixMax));
    fx.setParam(kBodyCloudMixId, linNorm(0.0, kBodyCloudMixMin, kBodyCloudMixMax));
    fx.setParam(kAtmosLevelId, linNorm(0.0, kAtmosLevelMin, kAtmosLevelMax));
    fx.setParam(kAetherMixId, 0.0);
    fx.setParam(kAetherModDepthId, 0.0);
    fx.setParam(kCloudInharmonicityId,
                linNorm(0.0, kCloudInharmonicityMin, kCloudInharmonicityMax));
    fx.setParam(kCloudDriftDepthId, linNorm(0.0, kCloudDriftDepthMin, kCloudDriftDepthMax));
    fx.setParam(kCloudGravityId, linNorm(0.0, kCloudGravityMin, kCloudGravityMax));
    fx.setParam(kCloudMutationId, 0.0);
    fx.setParam(kMorphEntropyId, 0.0);
    // The journey PARKED on slot 0: External travel, target position 0, so
    // currentSegment() is 0 and slot 0 is a contributing slot
    // (spectral_morph_engine.h:565). slotContributes() is private, which is why
    // the position is pinned rather than queried.
    fx.setParam(kMorphPositionId, 0.0);
}

// -----------------------------------------------------------------------------
// The detector: a 4096-point Blackman-Harris magnitude spectrum with parabolic
// peak interpolation. 4096 is pinned by tasks.md T010 case 3 (and by T005's arm
// (a), which SC-028 says to measure "exactly as" it does).
// -----------------------------------------------------------------------------

constexpr std::size_t kFftSize = 4096;

struct Spectrum {
    std::vector<float> magnitude;  // kFftSize/2 + 1 bins
    double binHz = 0.0;
    bool valid = false;
};

[[nodiscard]] Spectrum analyseTail(const std::vector<float>& mono) {
    Spectrum out;
    if (mono.size() < kFftSize) {
        return out;
    }
    const std::size_t start = mono.size() - kFftSize;

    std::vector<float> windowed(kFftSize, 0.0f);
    constexpr double kA0 = 0.35875;
    constexpr double kA1 = 0.48829;
    constexpr double kA2 = 0.14128;
    constexpr double kA3 = 0.01168;
    constexpr double kTwoPi = 6.283185307179586;
    for (std::size_t i = 0; i < kFftSize; ++i) {
        const double t = static_cast<double>(i) / static_cast<double>(kFftSize - 1);
        const double w = kA0 - kA1 * std::cos(kTwoPi * t) + kA2 * std::cos(2.0 * kTwoPi * t)
                         - kA3 * std::cos(3.0 * kTwoPi * t);
        windowed[i] = static_cast<float>(static_cast<double>(mono[start + i]) * w);
    }

    Krate::DSP::FFT fft;
    fft.prepare(kFftSize);
    REQUIRE(fft.isPrepared());

    std::vector<Krate::DSP::Complex> spectrum(fft.numBins());
    fft.forward(windowed.data(), spectrum.data());

    out.magnitude.resize(spectrum.size(), 0.0f);
    for (std::size_t b = 0; b < spectrum.size(); ++b) {
        out.magnitude[b] = spectrum[b].magnitude();
    }
    out.binHz = kSampleRate / static_cast<double>(kFftSize);
    out.valid = true;
    return out;
}

struct PeakEstimate {
    double hz = 0.0;
    double magnitude = 0.0;
    bool valid = false;
};

/// The interpolated peak within +/- `halfWidthHz` of `hz`.
///
/// The parabolic (log-magnitude, three-point) refinement is NOT optional here:
/// one bin is 11.72 Hz at 4096/48 kHz, while SC-028's 5-cent tolerance at
/// 392 Hz is 1.13 Hz. The nearest-bin answer could not distinguish a correct
/// build from one that missed by a semitone-tenth, so the criterion would be
/// structurally unable to fail.
[[nodiscard]] PeakEstimate interpolatedPeakNear(const Spectrum& s, double hz,
                                                double halfWidthHz) {
    PeakEstimate out;
    if (!s.valid || s.magnitude.size() < 3u || !(s.binHz > 0.0)) {
        return out;
    }
    const auto lastBin = static_cast<int>(s.magnitude.size()) - 1;
    const int lo = std::max(1, static_cast<int>((hz - halfWidthHz) / s.binHz));
    const int hi = std::min(lastBin - 1, static_cast<int>((hz + halfWidthHz) / s.binHz) + 1);
    if (lo > hi) {
        return out;
    }

    int best = lo;
    for (int b = lo; b <= hi; ++b) {
        if (s.magnitude[static_cast<std::size_t>(b)] > s.magnitude[static_cast<std::size_t>(best)]) {
            best = b;
        }
    }

    constexpr double kFloor = 1.0e-30;
    const auto logMag = [&s, kFloor](int b) {
        return std::log(std::max(static_cast<double>(s.magnitude[static_cast<std::size_t>(b)]),
                                 kFloor));
    };
    const double left = logMag(best - 1);
    const double centre = logMag(best);
    const double right = logMag(best + 1);
    const double denom = left - 2.0 * centre + right;
    // denom == 0 is a flat triple (all three on the log floor); fall back to the
    // bin centre rather than dividing by zero.
    const double delta = (denom != 0.0) ? (0.5 * (left - right) / denom) : 0.0;

    out.hz = (static_cast<double>(best) + std::clamp(delta, -0.5, 0.5)) * s.binHz;
    out.magnitude = static_cast<double>(s.magnitude[static_cast<std::size_t>(best)]);
    out.valid = true;
    return out;
}

[[nodiscard]] double centsBetween(double measuredHz, double expectedHz) {
    if (!(measuredHz > 0.0) || !(expectedHz > 0.0)) {
        return 1.0e9;  // finite, and far outside any tolerance
    }
    return 1200.0 * std::log2(measuredHz / expectedHz);
}

/// The engine slot the allocator handed the one held note.
[[nodiscard]] std::size_t soundingVoice(const Krate::DSP::SeraphisEngine& engine) {
    for (std::size_t v = 0; v < Krate::DSP::SeraphisEngine::kMaxVoices; ++v) {
        if (engine.getVoiceState(v) != Krate::DSP::VoiceState::Idle) {
            return v;
        }
    }
    return 0u;
}

// -----------------------------------------------------------------------------
// SC-029's measuring device.
//
// PORTED, not re-invented, from dsp/tests/unit/systems/
// spectral_morph_engine_test.cpp:990-1026 (`ChunkDeltaTracker`, used by
// SpectralMorph_TravelIsContinuous at :1145/:1182). The `life > 0` gate on the
// ratio arm is the one piece that is not obvious and is exactly why the original
// is ported rather than rewritten: a partial whose life factor is 0 is inaudible,
// and its ratio may legally step by up to 2 * kMaxScatterCents while silent.
//
// Named `MorphChunkDeltaTracker` here: `ChunkDeltaTracker` already exists as a
// TU-local struct in the dsp test above, and this file will not reuse a name that
// is spoken for anywhere in the tree, even TU-locally.
// -----------------------------------------------------------------------------
class MorphChunkDeltaTracker {
public:
    void observe(const Krate::DSP::SpectralMorphEngine& engine) noexcept {
        const float* ratios = engine.getOutputRatios();
        const float* amplitudes = engine.getOutputAmplitudes();
        for (std::size_t i = 0; i < kPartials; ++i) {
            const float life = engine.entropy().getLifeAmplitudeFactor(i);
            if (!isFiniteBits(ratios[i]) || !isFiniteBits(amplitudes[i])) {
                ++nonFinite_;
            }
            if (havePrevious_) {
                worstAmpDelta_ = std::max(worstAmpDelta_, std::abs(amplitudes[i] - prevAmp_[i]));
                if (life > 0.0f && prevLife_[i] > 0.0f && ratios[i] > 0.0f
                    && prevRatio_[i] > 0.0f) {
                    const float cents = std::abs(1200.0f * std::log2(ratios[i] / prevRatio_[i]));
                    worstCentsDelta_ = std::max(worstCentsDelta_, cents);
                } else {
                    ++gatedPartialChunks_;
                }
            }
            prevAmp_[i] = amplitudes[i];
            prevRatio_[i] = ratios[i];
            prevLife_[i] = life;
        }
        havePrevious_ = true;
    }

    [[nodiscard]] float worstAmpDelta() const noexcept { return worstAmpDelta_; }
    [[nodiscard]] float worstCentsDelta() const noexcept { return worstCentsDelta_; }
    [[nodiscard]] int nonFinite() const noexcept { return nonFinite_; }
    [[nodiscard]] long long gatedPartialChunks() const noexcept { return gatedPartialChunks_; }

private:
    std::array<float, kPartials> prevRatio_{};
    std::array<float, kPartials> prevAmp_{};
    std::array<float, kPartials> prevLife_{};
    float worstAmpDelta_ = 0.0f;
    float worstCentsDelta_ = 0.0f;
    int nonFinite_ = 0;
    long long gatedPartialChunks_ = 0;
    bool havePrevious_ = false;
};

}  // namespace

// =============================================================================
// SC-018, FR-036 - notify() is hardened against a garbage sender.
//
// The controller is the only shipping sender, but a message is UNTRUSTED INPUT:
// a host may replay, reorder or corrupt one, and the mutators' own rejection
// (C-6) is the SECOND line of defence, not the first.
// =============================================================================
TEST_CASE("Seraphis_EditMessage_RejectsGarbage", "[partial_edit][phase11]") {
    ProcessorFixture fx;
    REQUIRE(fx.prepare(kSampleRate, kBlock) == Steinberg::kResultOk);
    REQUIRE(fx.proc->engineForTest() != nullptr);

    fx.setParam(Seraphis::kPolyphonyId, polyphonyNorm(4));
    fx.pushEvent(Steinberg::Vst::Event::kNoteOnEvent, 60, kHostVelocity, 0);
    REQUIRE(fx.processBlock(kBlock) == Steinberg::kResultOk);

    // Non-vacuity: the four slots start VALID, so "still valid afterwards" is a
    // property the fuzz could actually destroy.
    for (int slot = 0; slot < 4; ++slot) {
        REQUIRE(Krate::DSP::isValidSpectralState(fx.proc->spectralSlotForTest(slot)));
        REQUIRE(Krate::DSP::isValidSpectralState(fx.proc->spectralAuthoringSlotForTest(slot)));
    }

    // A fixed seed: the fuzz must be REPRODUCIBLE, or a failure cannot be chased.
    std::mt19937 rng(0x5E4A9415u);
    constexpr int kMessages = 10000;

    const auto fuzzFloat = [&rng]() -> float {
        switch (rng() % 4u) {
            case 0:
                return floatFromBits(kQuietNaNBits);
            case 1:
                return floatFromBits((rng() % 2u == 0u) ? kPosInfBits : kNegInfBits);
            case 2:
                // A wholly random 32-bit pattern: subnormals, huge finites, and
                // roughly 0.4 % genuine NaNs, none of them written as a literal.
                return floatFromBits(static_cast<std::uint32_t>(rng()));
            default: {
                // A plausible in-range value, so the fuzz also drives the ACCEPT
                // path and the run is not merely 10 000 rejections.
                const auto unit = static_cast<float>(rng() % 2001u) / 1000.0f;  // [0, 2]
                return unit - 0.5f;
            }
        }
    };

    for (int i = 0; i < kMessages; ++i) {
        // Drawn into NAMED LOCALS, in order: argument evaluation order is
        // unspecified in C++, so building the message inline would make the
        // sequence compiler-dependent and the "fixed seed" claim above false.
        const auto kind = static_cast<std::uint8_t>(rng() & 0xFFu);      // 0..255
        const auto slot = static_cast<std::uint8_t>(rng() & 0x0Fu);      // 0..15
        const auto index = static_cast<std::uint16_t>(rng() & 0x01FFu);  // 0..511
        const float a = fuzzFloat();
        const float b = fuzzFloat();
        const Seraphis::UI::EditMessage m = makeEdit(kind, slot, index, a, b);
        // A dropped message is still a HANDLED message: notify() answers ok and
        // never propagates an error for input it chose to ignore.
        REQUIRE(sendEdit(*fx.proc, m) == Steinberg::kResultOk);

        // Interleave real blocks so the staging ring is genuinely being consumed
        // by the audio thread's side of the interlock while the fuzz writes it.
        if (i % 250 == 0) {
            REQUIRE(fx.processBlock(kBlock) == Steinberg::kResultOk);
        }
    }

    for (int extra = 0; extra < 8; ++extra) {
        REQUIRE(fx.processBlock(kBlock) == Steinberg::kResultOk);
    }

    // NO Catch2 SECTIONs below this point. A SECTION re-runs the whole TEST_CASE
    // body, and this one costs 10 000 messages plus 48 blocks of render; four
    // sections would be four fuzz runs for four assertions that share one state.
    {
        // Every morph slot - live AND authoring - is still valid.
        for (int slot = 0; slot < 4; ++slot) {
            INFO("slot " << slot);
            CHECK(Krate::DSP::isValidSpectralState(fx.proc->spectralSlotForTest(slot)));
            CHECK(Krate::DSP::isValidSpectralState(fx.proc->spectralAuthoringSlotForTest(slot)));
        }
    }
    {
        // The staging ring index is -1 or inside the ring.
        const int handoff = fx.proc->spectralSlotsHandoffForTest();
        INFO("handoff = " << handoff);
        CHECK((handoff == -1 || (handoff >= 0 && handoff < 3)));
    }
    {
        // The session state the fuzz can reach stays in range.
        INFO("selected edit slot = " << fx.proc->selectedEditSlotForTest());
        CHECK(fx.proc->selectedEditSlotForTest() >= 0);
        CHECK(fx.proc->selectedEditSlotForTest() <= 3);
    }
    {
        // A subsequent render is finite everywhere - BIT PATTERN, never
        // std::isnan.
        fx.capturedL.clear();
        fx.capturedR.clear();
        fx.renderBlocks(32u, static_cast<std::size_t>(kBlock));
        REQUIRE(fx.capturedL.size() == 32u * static_cast<std::size_t>(kBlock));
        CHECK(allFiniteBits(fx.capturedL));
        CHECK(allFiniteBits(fx.capturedR));
        CHECK(fx.checkCanaries());
    }
}

// =============================================================================
// SC-025, Q2 - Blend A->B is ABSOLUTE, re-blended from a latched pristine A.
//
// A compounding implementation (one that re-read the destination slot on every
// kind 4) passes any single-step assertion and only shows up on a sweep that
// RETURNS: t = 0 -> 0.5 -> 1 -> 0.5 -> 0 must land byte-identically back on A.
// =============================================================================
TEST_CASE("Seraphis_BlendGesture_IsAbsoluteNotCompounding", "[partial_edit][phase11]") {
    ProcessorFixture fx;
    REQUIRE(fx.prepare(kSampleRate, kBlock) == Steinberg::kResultOk);
    REQUIRE(fx.processBlock(kBlock) == Steinberg::kResultOk);

    // Slot 0 defaults to SineStack and slot 1 to Glass
    // (morph_params.h:86, kMorphSlotDefaultIndices = {0, 3, 0, 0}), so A and B
    // are genuinely different payloads and "landed on B" is falsifiable.
    const SpectralState pristineA = fx.proc->spectralSlotForTest(0);
    const SpectralState slotB = fx.proc->spectralSlotForTest(1);
    REQUIRE_FALSE(statesEqual(pristineA, slotB));
    REQUIRE(Krate::DSP::isValidSpectralState(pristineA));
    REQUIRE(Krate::DSP::isValidSpectralState(slotB));

    constexpr std::uint8_t kBlendBegin = 7;
    constexpr std::uint8_t kBlendStates = 4;
    constexpr std::uint8_t kSlotSelect = 6;
    constexpr float kSlotBAsFloat = 1.0f;

    SECTION("a returning sweep lands byte-identically back on A") {
        REQUIRE(sendEdit(*fx.proc, makeEdit(kBlendBegin, 0, 0, 0.0f, kSlotBAsFloat))
                == Steinberg::kResultOk);
        CHECK(fx.proc->blendSnapshotValidForTest());

        bool leftA = false;
        for (const float t : {0.0f, 0.5f, 1.0f, 0.5f, 0.0f}) {
            REQUIRE(sendEdit(*fx.proc, makeEdit(kBlendStates, 0, 0, t, kSlotBAsFloat))
                    == Steinberg::kResultOk);
            REQUIRE(fx.processBlock(kBlock) == Steinberg::kResultOk);
            if (t == 1.0f) {
                leftA = !statesEqual(fx.proc->spectralSlotForTest(0), pristineA);
            }
        }

        // NON-VACUITY: the sweep really did leave A on the way, so "back on A"
        // is a return and not a slot that was never written.
        REQUIRE(leftA);
        INFO("editStageWrites = " << fx.proc->editStageWriteCountForTest());
        CHECK(fx.proc->editStageWriteCountForTest() == 5u);
        CHECK(statesEqual(fx.proc->spectralSlotForTest(0), pristineA));
    }

    SECTION("t = 1 lands on B, and a SECOND gesture lands on B again - not doubly") {
        // Gesture 1: run the slot all the way to B.
        REQUIRE(sendEdit(*fx.proc, makeEdit(kBlendBegin, 0, 0, 0.0f, kSlotBAsFloat))
                == Steinberg::kResultOk);
        REQUIRE(sendEdit(*fx.proc, makeEdit(kBlendStates, 0, 0, 1.0f, kSlotBAsFloat))
                == Steinberg::kResultOk);
        REQUIRE(fx.processBlock(kBlock) == Steinberg::kResultOk);
        CHECK(statesEqual(fx.proc->spectralSlotForTest(0), slotB));

        // Gesture 2 snapshots the NOW-CURRENT state (which is B) and runs to
        // t = 1 again. Absolute blending re-lands on B; a compounding one would
        // blend B with B for one gesture and then drift on the next.
        REQUIRE(sendEdit(*fx.proc, makeEdit(kBlendBegin, 0, 0, 0.0f, kSlotBAsFloat))
                == Steinberg::kResultOk);
        REQUIRE(sendEdit(*fx.proc, makeEdit(kBlendStates, 0, 0, 1.0f, kSlotBAsFloat))
                == Steinberg::kResultOk);
        REQUIRE(fx.processBlock(kBlock) == Steinberg::kResultOk);
        CHECK(statesEqual(fx.proc->spectralSlotForTest(0), slotB));

        // ...and slot B itself was never written.
        CHECK(statesEqual(fx.proc->spectralSlotForTest(1), slotB));
    }

    SECTION("a kind 4 with no live kind-7 snapshot is DROPPED") {
        // A kind 6 ends any gesture (C-5's table), which is the cheapest way to
        // reach the no-snapshot state through the real message path.
        REQUIRE(sendEdit(*fx.proc, makeEdit(kBlendBegin, 0, 0, 0.0f, kSlotBAsFloat))
                == Steinberg::kResultOk);
        REQUIRE(sendEdit(*fx.proc, makeEdit(kSlotSelect, 2, 0, 0.0f, 0.0f))
                == Steinberg::kResultOk);
        CHECK_FALSE(fx.proc->blendSnapshotValidForTest());
        CHECK(fx.proc->selectedEditSlotForTest() == 2);

        const std::size_t writesBefore = fx.proc->editStageWriteCountForTest();
        REQUIRE(sendEdit(*fx.proc, makeEdit(kBlendStates, 0, 0, 0.5f, kSlotBAsFloat))
                == Steinberg::kResultOk);
        REQUIRE(fx.processBlock(kBlock) == Steinberg::kResultOk);

        // DROPPED, not "applied and happened to be a no-op": the staging counter
        // is what tells those two apart.
        CHECK(fx.proc->editStageWriteCountForTest() == writesBefore);
        CHECK(statesEqual(fx.proc->spectralSlotForTest(0), pristineA));
    }
}

// =============================================================================
// SC-028 - a ratio edit reaches a CURRENTLY SOUNDING voice.
//
// This is the criterion T003a's gate relaxation exists for. Arm (ii) is the one
// that fails on an un-relaxed build even if the FR-046 retry machinery
// eventually lands the edit on the next note: SeraphisVoice::setSpectralState
// used to reject the push while the voice hasSounded_ and count it in
// rejectedConfigCalls_ (seraphis_voice.h's getRejectedConfigureTimeCallCount).
// =============================================================================
TEST_CASE("Seraphis_EditMode_RatioEditReachesSoundingVoice", "[partial_edit][phase11]") {
    ProcessorFixture fx;
    REQUIRE(fx.prepare(kSampleRate, kBlock) == Steinberg::kResultOk);
    REQUIRE(fx.proc->engineForTest() != nullptr);

    conditionForSpectralIsolation(fx);
    fx.setParam(Seraphis::kPolyphonyId, polyphonyNorm(1));
    REQUIRE(fx.processBlock(kBlock) == Steinberg::kResultOk);

    // MIDI 60 = C4. Held for the whole case; the edit lands MID-NOTE.
    //
    // The note-on is placed by renderBlocks' PER-BLOCK SCRIPT, not by a
    // pushEvent before the loop: renderBlocks clears `events` at the top of
    // every block (seraphis_test_fixture.h), so an event queued beforehand would
    // be discarded and the case would measure silence.
    constexpr std::size_t kPreEditBlocks = 94;  // ~1 s at 512 / 48 kHz
    fx.capturedL.clear();
    fx.capturedR.clear();
    fx.renderBlocks(kPreEditBlocks, static_cast<std::size_t>(kBlock),
                    [&fx](std::size_t block, auto& /*events*/, auto& /*params*/) {
                        if (block == 0) {
                            fx.pushEvent(Steinberg::Vst::Event::kNoteOnEvent, 60, kHostVelocity,
                                         0);
                        }
                    });
    REQUIRE(allFiniteBits(fx.capturedL));

    const Krate::DSP::SeraphisEngine& engine = *fx.proc->engineForTest();
    const std::size_t voice = soundingVoice(engine);
    REQUIRE(engine.getVoiceState(voice) != Krate::DSP::VoiceState::Idle);

    const double fundamentalHz = static_cast<double>(engine.getVoice(voice).cloud().getFundamentalHz());
    INFO("sounding voice " << voice << ", f0 = " << fundamentalHz);
    REQUIRE(fundamentalHz > 200.0);
    REQUIRE(fundamentalHz < 350.0);

    // The state the edit will author from, and the amplitude it must NOT change.
    const SpectralState before = fx.proc->spectralSlotForTest(0);
    REQUIRE(before.numPartials >= 2);
    REQUIRE(static_cast<double>(before.ratios[0]) == Approx(1.0).margin(1.0e-6));
    const float amplitude0 = before.amplitudes[0];

    // The pre-edit peak really is at f0 * 1.0 - otherwise "it moved" proves
    // nothing about where it moved FROM.
    const Spectrum spectrumBefore = analyseTail(fx.capturedL);
    REQUIRE(spectrumBefore.valid);
    const PeakEstimate peakBefore = interpolatedPeakNear(spectrumBefore, fundamentalHz, 40.0);
    REQUIRE(peakBefore.valid);
    const double centsBefore = centsBetween(peakBefore.hz, fundamentalHz);
    INFO("pre-edit peak = " << peakBefore.hz << " Hz (" << centsBefore << " cents)");
    // Non-vacuity at 30 cents (the detector really is locked onto the
    // fundamental, not onto a noise shoulder), then the SAME 5-cent tolerance the
    // post-edit arm is judged by - so a detector that cannot resolve 5 cents
    // fails visibly HERE rather than being mistaken for a broken edit path.
    REQUIRE(std::abs(centsBefore) <= 30.0);
    CHECK(std::abs(centsBefore) <= 5.0);

    // --- the edit, MID-NOTE ---------------------------------------------------
    //
    // INDEX 0 IS PINNED and must stay there (spec C-6): the authoring window caps
    // the upper edge at ratios[k+1] / kAuthorSpacing = (k + 2) / 1.0163049, and a
    // perfect fifth 1.5 * (k + 1) exceeds that for every k >= 1. At k = 0 the
    // edge is 1.968 and 1.5 fits with room to spare.
    constexpr float kFifthRatio = 1.5f;
    constexpr std::uint8_t kPartialRatioAmp = 1;

    const std::uint32_t rejectedBefore =
        engine.getVoice(voice).getRejectedConfigureTimeCallCount();

    REQUIRE(sendEdit(*fx.proc, makeEdit(kPartialRatioAmp, 0, 0, kFifthRatio, amplitude0))
            == Steinberg::kResultOk);
    CHECK(fx.proc->editStageWriteCountForTest() == 1u);

    // Past SpectralMorphEngine's OWN FR-047 absorption window
    // (kStateChangeFadeSec = 2 s, spectral_morph_engine.h:103) plus the cloud's
    // ratio slew, then a clean 4096-sample tail to analyse. NO NEW TIME CONSTANT
    // IS INTRODUCED: the wait is Phase 3's, read from Phase 3's header.
    constexpr std::size_t kPostEditBlocks = 376;  // ~4 s
    fx.capturedL.clear();
    fx.capturedR.clear();
    fx.renderBlocks(kPostEditBlocks, static_cast<std::size_t>(kBlock));
    REQUIRE(allFiniteBits(fx.capturedL));

    const std::uint32_t rejectedAfter =
        engine.getVoice(voice).getRejectedConfigureTimeCallCount();

    // NO Catch2 SECTIONs below: a SECTION re-runs the whole body, and this one
    // renders five seconds of audio before it asserts anything.
    {
        // (ii) the push was NOT rejected as a configure-time call.
        INFO("rejected before = " << rejectedBefore << ", after = " << rejectedAfter);
        // The voice is still sounding, so on the un-relaxed Phase 9 build this
        // counter would have moved once per fan-out attempt.
        CHECK(engine.getVoiceState(voice) != Krate::DSP::VoiceState::Idle);
        CHECK(rejectedAfter == rejectedBefore);
    }
    {
        // The authored ratio actually reached the live slot.
        const SpectralState after = fx.proc->spectralSlotForTest(0);
        CHECK(static_cast<double>(after.ratios[0]) == Approx(kFifthRatio).margin(1.0e-6));
        CHECK(static_cast<double>(after.amplitudes[0])
              == Approx(static_cast<double>(amplitude0)).margin(1.0e-6));
        // setPartial writes exactly two entries and nothing else.
        CHECK(after.numPartials == before.numPartials);
        CHECK(static_cast<double>(after.ratios[1])
              == Approx(static_cast<double>(before.ratios[1])).margin(1.0e-6));
    }
    {
        // (i) the RENDERED peak moved to the new authored ratio, within 5 cents.
        const double expectedHz = fundamentalHz * static_cast<double>(kFifthRatio);
        const Spectrum spectrumAfter = analyseTail(fx.capturedL);
        REQUIRE(spectrumAfter.valid);
        // +/- 40 Hz is ~3.4 bins and ~170 cents at this frequency: wide enough to
        // catch a peak that landed slightly off, far too narrow to accidentally
        // lock onto partial 1 at 2 * f0.
        const PeakEstimate peakAfter = interpolatedPeakNear(spectrumAfter, expectedHz, 40.0);
        REQUIRE(peakAfter.valid);
        const double cents = centsBetween(peakAfter.hz, expectedHz);
        INFO("expected " << expectedHz << " Hz, measured " << peakAfter.hz << " Hz (" << cents
                         << " cents), magnitude " << peakAfter.magnitude);
        CHECK(std::abs(cents) <= 5.0);
        // ...and it is a real partial, not the interpolated shoulder of noise.
        CHECK(peakAfter.magnitude > 0.0);
    }
}

// =============================================================================
// SC-029 - the live ratio edit is CLICK-FREE.
//
// The bounds are PHASE 3'S OWN, referenced by name from
// spectral_morph_engine.h - never restated as literals. The FR-044 contributor
// static_asserts at :156-186 sum the enumerated per-chunk contributors against
// exactly these two constants, so this case cannot be made to pass by loosening
// a number: loosening one breaks that header's own build.
//
// A MEASURED DISCONTINUITY BOUND, never a bit-exact comparison.
// =============================================================================
TEST_CASE("Seraphis_EditMode_LiveRatioEditIsClickFree", "[partial_edit][phase11]") {
    using Morph = Krate::DSP::SpectralMorphEngine;

    // --- the plugin-side arm --------------------------------------------------
    ProcessorFixture fx;
    REQUIRE(fx.prepare(kSampleRate, kBlock) == Steinberg::kResultOk);
    REQUIRE(fx.proc->engineForTest() != nullptr);

    conditionForSpectralIsolation(fx);
    fx.setParam(Seraphis::kPolyphonyId, polyphonyNorm(1));
    REQUIRE(fx.processBlock(kChunk) == Steinberg::kResultOk);

    fx.pushEvent(Steinberg::Vst::Event::kNoteOnEvent, 60, kHostVelocity, 0);

    // One process() call per ABSOLUTE 64-sample control chunk, so an observation
    // between calls is exactly one chunk apart - the granularity Phase 3's bounds
    // are stated at.
    constexpr int kChunksBeforeEdit = 380;  // ~0.5 s
    constexpr int kChunksAfterEdit = 1900;  // ~2.5 s, past kStateChangeFadeSec = 2 s

    for (int c = 0; c < kChunksBeforeEdit; ++c) {
        REQUIRE(fx.processBlock(kChunk) == Steinberg::kResultOk);
    }

    const Krate::DSP::SeraphisEngine& engine = *fx.proc->engineForTest();
    const std::size_t voice = soundingVoice(engine);
    REQUIRE(engine.getVoiceState(voice) != Krate::DSP::VoiceState::Idle);

    const SpectralState before = fx.proc->spectralSlotForTest(0);
    REQUIRE(before.numPartials >= 2);

    MorphChunkDeltaTracker plugin;
    plugin.observe(engine.getVoice(voice).morph());  // the pre-edit reference chunk

    REQUIRE(sendEdit(*fx.proc, makeEdit(/*kind=*/1, /*slot=*/0, /*index=*/0, 1.5f,
                                        before.amplitudes[0]))
            == Steinberg::kResultOk);

    for (int c = 0; c < kChunksAfterEdit; ++c) {
        REQUIRE(fx.processBlock(kChunk) == Steinberg::kResultOk);
        plugin.observe(engine.getVoice(voice).morph());
    }

    // Non-vacuity: the edit really did reach the live slot, so the window that
    // was measured is the absorption window and not a quiescent stretch.
    REQUIRE(static_cast<double>(fx.proc->spectralSlotForTest(0).ratios[0])
            == Approx(1.5).margin(1.0e-6));

    INFO("plugin worst amp delta  = " << plugin.worstAmpDelta() << " (bound "
                                      << Morph::kMaxAmpDeltaPerChunk << ")");
    INFO("plugin worst cents delta = " << plugin.worstCentsDelta() << " (bound "
                                       << Morph::kMaxRatioDeltaCentsPerChunk << ")");
    INFO("gated partial-chunks = " << plugin.gatedPartialChunks());
    CHECK(plugin.nonFinite() == 0);
    CHECK(plugin.worstAmpDelta() <= Morph::kMaxAmpDeltaPerChunk);
    CHECK(plugin.worstCentsDelta() <= Morph::kMaxRatioDeltaCentsPerChunk);

    // --- the SANITY arm: the same edit, in-DSP -------------------------------
    //
    // Proving the RELAXATION created no second, worse path. If the in-DSP
    // setState on the same slot at the same instant breaches the same bounds,
    // the defect is Phase 3's and not this phase's, and this case says so
    // instead of blaming the plugin wiring.
    {
        Morph morph;
        morph.prepare(kSampleRate);
        morph.setStateCount(2);
        morph.setState(0, before);
        morph.setState(1, fx.proc->spectralSlotForTest(1));
        morph.setEntropy(0.0f);
        morph.setTravelMode(Morph::TravelMode::External);
        morph.setTargetPosition(0.0f);

        MorphChunkDeltaTracker direct;
        for (int c = 0; c < kChunksBeforeEdit; ++c) {
            morph.updateChunk(static_cast<std::size_t>(kChunk));
        }
        direct.observe(morph);

        SpectralState edited = before;
        Krate::DSP::setPartial(edited, 0u, 1.5f, before.amplitudes[0]);
        REQUIRE(Krate::DSP::isValidSpectralState(edited));
        morph.setState(0, edited);

        for (int c = 0; c < kChunksAfterEdit; ++c) {
            morph.updateChunk(static_cast<std::size_t>(kChunk));
            direct.observe(morph);
        }

        INFO("in-DSP worst amp delta   = " << direct.worstAmpDelta());
        INFO("in-DSP worst cents delta = " << direct.worstCentsDelta());
        CHECK(direct.nonFinite() == 0);
        CHECK(direct.worstAmpDelta() <= Morph::kMaxAmpDeltaPerChunk);
        CHECK(direct.worstCentsDelta() <= Morph::kMaxRatioDeltaCentsPerChunk);
    }
}

// =============================================================================
// T011 - the override TABLE and its re-push.
//
// Everything below observes the table through TWO independent surfaces and
// requires them to agree:
//
//   the ENGINE  - HarmonicCloud::getPartialPosition (harmonic_cloud.h:986) and
//                 getPartialCurrentAmplitude (:959) / getPartialTargetAmplitude
//                 (:963), read through Processor::engineForTest() on EVERY one
//                 of the sixteen voice slots; and
//   the FRAME   - Processor::lastPublishedFrameForTest(), i.e. what the UI would
//                 actually draw.
//
// A build that keeps the processor-side bitmasks but never fans them back out to
// the voices passes a frame-only assertion and fails the engine one, which is
// exactly the defect FR-030 exists to prevent.
// =============================================================================

namespace {

/// C-5 kinds 2 and 3.
constexpr std::uint8_t kEditPartialPan = 2;
constexpr std::uint8_t kEditPartialMask = 3;

/// Both indices sit well inside the active count. Richness defaults to 0.60 and
/// N(r) = clamp(round(64^r), 1, 64) (harmonic_cloud.h:1459-1463), so 64^0.6 ~=
/// 12.1 -> TWELVE active partials at the shipped default. 3 and 6 (and 6's
/// neighbour 7) are therefore all real, sounding partials rather than slots that
/// are zero for an unrelated reason.
constexpr std::size_t kPanPartial = 3;
constexpr std::size_t kMaskPartial = 6;
constexpr std::size_t kNeighbourPartial = kMaskPartial + 1;

constexpr float kAuthoredPan = 0.8f;
/// SC-014's stated band.
constexpr double kPanTolerance = 0.01;

constexpr std::size_t kVoiceSlots = Krate::DSP::SeraphisEngine::kMaxVoices;

/// ~1 s at 512 / 48 kHz.
constexpr std::size_t kSettleBlocks = 94;

/// The authored pan must be present on EVERY slot in [0, kMaxVoices) - not only
/// on the allocated ones. SeraphisEngine's fan-out banner (seraphis_engine.h:
/// 835-841) states the rule: a slot the allocator hands out LATER must already
/// carry the override, which is what discharges the polyphony-increase clearing
/// event by construction.
void checkAuthoredPanOnEveryVoice(const Krate::DSP::SeraphisEngine& engine,
                                  const char* what) {
    for (std::size_t v = 0; v < kVoiceSlots; ++v) {
        INFO(what << " - voice " << v);
        CHECK(static_cast<double>(engine.getVoice(v).cloud().getPartialPosition(kPanPartial))
              == Approx(static_cast<double>(kAuthoredPan)).margin(kPanTolerance));
    }
}

/// The mask half, on a SOUNDING voice. targetAmplitude_[i] is set to EXACTLY
/// 0.0f when masked_[i] is true and to the ordinary chain product otherwise
/// (harmonic_cloud.h:1750), so this reads the mask directly rather than
/// inferring it from a decay. The neighbour arm is the non-vacuity: it proves
/// the voice is producing amplitudes at all, so "0" means masked and not "idle".
void checkMaskHeldOnSoundingVoice(const Krate::DSP::SeraphisEngine& engine,
                                  std::size_t voice, const char* what) {
    const Krate::DSP::HarmonicCloud& cloud = engine.getVoice(voice).cloud();
    INFO(what << " - sounding voice " << voice);
    CHECK(cloud.getPartialTargetAmplitude(kMaskPartial) == 0.0f);
    CHECK(cloud.getPartialTargetAmplitude(kNeighbourPartial) > 0.0f);
}

}  // namespace

// =============================================================================
// SC-014, FR-030, FR-043 - the table survives every clearing event.
//
// Five of the six events below CLEAR state the plugin authored. HarmonicCloud
// documents each one:
//   setStereoSpread -> positionOverridden_.fill(false)   (harmonic_cloud.h:535-547)
//   setSeed         -> positionOverridden_.fill(false)   (:701-706)
//   reset()         -> positionOverridden_ AND masked_   (:331-332)
// The MASK is therefore asserted across events 3, 4 and 5 only: it survives a
// spread change and a re-seed BY CONSTRUCTION, because neither of those two
// touches masked_, and asserting it there would assert nothing about the
// re-push.
// =============================================================================
TEST_CASE("Seraphis_PartialOverrides_SurviveClearingEvents", "[partial_edit][phase11]") {
    ProcessorFixture fx;
    REQUIRE(fx.prepare(kSampleRate, kBlock) == Steinberg::kResultOk);
    REQUIRE(fx.proc->engineForTest() != nullptr);
    const Krate::DSP::SeraphisEngine& engine = *fx.proc->engineForTest();

    // The frame is what the UI draws, and C-2 clause 6's gate is closed until an
    // editor opens. Nothing in publishCloudFrame() runs without it.
    fx.proc->setCloudFrameGateForTest(true);

    // Zero mutation / drift / inharmonicity so the amplitude chain is stationary
    // and the only thing that can move a position is a clearing event. It also
    // silences body, atmosphere and aether, which makes the ~10 s of render this
    // case needs cheap.
    conditionForSpectralIsolation(fx);
    fx.setParam(Seraphis::kPolyphonyId, polyphonyNorm(1));

    const auto holdOneNote = [&fx](Steinberg::int16 pitch, std::size_t blocks) {
        fx.pushEvent(Steinberg::Vst::Event::kNoteOnEvent, pitch, kHostVelocity, 0);
        for (std::size_t b = 0; b < blocks; ++b) {
            REQUIRE(fx.processBlock(kBlock) == Steinberg::kResultOk);
        }
    };

    holdOneNote(60, kSettleBlocks);

    const Seraphis::CloudFrame& frame = fx.proc->lastPublishedFrameForTest();
    REQUIRE(static_cast<std::size_t>(frame.partialCount) > kNeighbourPartial);

    // NON-VACUITY. The FR-021 seeded scatter must not already sit at the value
    // the edit authors, or "the override survived" would be indistinguishable
    // from "the override was never needed".
    REQUIRE(std::abs(static_cast<double>(
                         engine.getVoice(0).cloud().getPartialPosition(kPanPartial))
                     - static_cast<double>(kAuthoredPan))
            > 4.0 * kPanTolerance);

    // --- author one pan and one mask, through the REAL message path ----------
    REQUIRE(sendEdit(*fx.proc, makeEdit(kEditPartialPan, 0,
                                        static_cast<std::uint16_t>(kPanPartial), kAuthoredPan,
                                        0.0f))
            == Steinberg::kResultOk);
    REQUIRE(sendEdit(*fx.proc, makeEdit(kEditPartialMask, 0,
                                        static_cast<std::uint16_t>(kMaskPartial), 1.0f, 0.0f))
            == Steinberg::kResultOk);
    // Two blocks: the first CONSUMES partialOverridesPending_ and fans out, the
    // second publishes a frame that already carries the result.
    REQUIRE(fx.processBlock(kBlock) == Steinberg::kResultOk);
    REQUIRE(fx.processBlock(kBlock) == Steinberg::kResultOk);

    checkAuthoredPanOnEveryVoice(engine, "after the edit");
    checkMaskHeldOnSoundingVoice(engine, soundingVoice(engine), "after the edit");
    CHECK(static_cast<double>(frame.position[kPanPartial])
          == Approx(static_cast<double>(kAuthoredPan)).margin(kPanTolerance));
    CHECK(((frame.maskBits >> kMaskPartial) & 1u) == 1u);

    // --- (1) a deep kCloudStereoSpreadId (207) change ------------------------
    // 207 is CloudStereoSpread's setTargetBase origin, so this reaches
    // setStereoSpread through macros_.apply() exactly as the macro half does.
    fx.setParam(Seraphis::kCloudStereoSpreadId, 0.9);
    REQUIRE(fx.processBlock(kBlock) == Steinberg::kResultOk);
    REQUIRE(fx.processBlock(kBlock) == Steinberg::kResultOk);
    checkAuthoredPanOnEveryVoice(engine, "after a stereo-spread change");
    CHECK(static_cast<double>(frame.position[kPanPartial])
          == Approx(static_cast<double>(kAuthoredPan)).margin(kPanTolerance));

    // --- (2) a kSeedId (3) change --------------------------------------------
    // Index 7 of the sixteen curated seeds; the default is index 0, so the
    // on-change seed burst really fires.
    fx.setParam(Seraphis::kSeedId, 7.0 / 15.0);
    REQUIRE(fx.processBlock(kBlock) == Steinberg::kResultOk);
    REQUIRE(fx.processBlock(kBlock) == Steinberg::kResultOk);
    checkAuthoredPanOnEveryVoice(engine, "after a seed change");
    CHECK(static_cast<double>(frame.position[kPanPartial])
          == Approx(static_cast<double>(kAuthoredPan)).margin(kPanTolerance));

    // --- (3) an engine reset() ------------------------------------------------
    // setActive(false) runs SeraphisEngine::silence(), which is per-voice
    // silence() THEN reset() (seraphis_engine.h:414-420) - the one event that
    // clears BOTH halves of the table.
    REQUIRE(fx.proc->setActive(false) == Steinberg::kResultOk);
    REQUIRE(fx.proc->setActive(true) == Steinberg::kResultOk);
    holdOneNote(60, kSettleBlocks);
    checkAuthoredPanOnEveryVoice(engine, "after an engine reset");
    checkMaskHeldOnSoundingVoice(engine, soundingVoice(engine), "after an engine reset");
    CHECK(static_cast<double>(frame.position[kPanPartial])
          == Approx(static_cast<double>(kAuthoredPan)).margin(kPanTolerance));

    // --- (4) polyphony 1 -> 8, and a NEWLY ALLOCATED voice --------------------
    fx.setParam(Seraphis::kPolyphonyId, polyphonyNorm(8));
    REQUIRE(fx.processBlock(kBlock) == Steinberg::kResultOk);
    // A second, different pitch, so the allocator has to hand out a slot that
    // was idle when the edit was made.
    holdOneNote(67, kSettleBlocks);
    {
        std::size_t allocated = 0;
        for (std::size_t v = 0; v < kVoiceSlots; ++v) {
            if (engine.getVoiceState(v) != Krate::DSP::VoiceState::Idle) {
                ++allocated;
            }
        }
        INFO("allocated voices after the polyphony increase = " << allocated);
        // NON-VACUITY for this arm: a second slot really was handed out, so
        // "a newly allocated voice reports the authored pan" is a claim about a
        // voice that exists.
        REQUIRE(allocated >= 2u);
    }
    checkAuthoredPanOnEveryVoice(engine, "after a polyphony increase");
    checkMaskHeldOnSoundingVoice(engine, soundingVoice(engine),
                                 "after a polyphony increase");
    CHECK(static_cast<double>(frame.position[kPanPartial])
          == Approx(static_cast<double>(kAuthoredPan)).margin(kPanTolerance));

    // --- (5) setupProcessing re-entry at a DIFFERENT sample rate --------------
    // FR-043's only criterion. setupProcessing reaches SeraphisEngine::prepare
    // and therefore HarmonicCloud::reset(), which clears both halves again.
    {
        Steinberg::Vst::ProcessSetup setup{};
        setup.processMode = Steinberg::Vst::kRealtime;
        setup.symbolicSampleSize = Steinberg::Vst::kSample32;
        setup.maxSamplesPerBlock = kBlock;
        setup.sampleRate = 44100.0;
        REQUIRE(fx.proc->setupProcessing(setup) == Steinberg::kResultOk);
    }
    holdOneNote(60, kSettleBlocks);
    checkAuthoredPanOnEveryVoice(engine, "after a sample-rate change");
    checkMaskHeldOnSoundingVoice(engine, soundingVoice(engine), "after a sample-rate change");
    CHECK(static_cast<double>(frame.position[kPanPartial])
          == Approx(static_cast<double>(kAuthoredPan)).margin(kPanTolerance));
    CHECK(fx.checkCanaries());
}

// =============================================================================
// SC-014 ARM 6 - the macro-ring sweep. THE DEFECT THIS TASK EXISTS TO CATCH.
//
// Bloom writes CloudStereoSpread through the matrix with .base = 0.35f and
// .amount = 0.60f (seraphis_macro_matrix.h:284-289), and macros_.apply() pushes
// the COMPOSED value into every voice on EVERY slice. HarmonicCloud::setStereoSpread
// wipes positionOverridden_ on any VALUE change - so a re-push tracker keyed on
// ParamID 207 is BLIND here: 207 never moves in this case, and such a tracker
// passes arms 1-5 of the case above while failing this one.
//
// The assertion is taken after EVERY block of the sweep, not only at the five
// end points: the macro knobs are class-(b) smoothed (~300 ms), so the composed
// spread moves continuously and a re-push that fired only on settled values
// would leave a window in which the pans are gone.
// =============================================================================
TEST_CASE("Seraphis_PartialOverrides_SurviveAMacroRingSweep", "[partial_edit][phase11]") {
    ProcessorFixture fx;
    REQUIRE(fx.prepare(kSampleRate, kBlock) == Steinberg::kResultOk);
    REQUIRE(fx.proc->engineForTest() != nullptr);
    const Krate::DSP::SeraphisEngine& engine = *fx.proc->engineForTest();

    fx.proc->setCloudFrameGateForTest(true);
    conditionForSpectralIsolation(fx);
    fx.setParam(Seraphis::kPolyphonyId, polyphonyNorm(1));

    fx.pushEvent(Steinberg::Vst::Event::kNoteOnEvent, 60, kHostVelocity, 0);
    for (std::size_t b = 0; b < kSettleBlocks; ++b) {
        REQUIRE(fx.processBlock(kBlock) == Steinberg::kResultOk);
    }

    const Seraphis::CloudFrame& frame = fx.proc->lastPublishedFrameForTest();
    REQUIRE(static_cast<std::size_t>(frame.partialCount) > kPanPartial);

    // The deep knob is recorded and then DELIBERATELY NEVER WRITTEN AGAIN, so
    // any spread movement below is the macro path's alone.
    const float spreadBeforeSweep = engine.getVoice(0).cloud().getStereoSpread();

    REQUIRE(sendEdit(*fx.proc, makeEdit(kEditPartialPan, 0,
                                        static_cast<std::uint16_t>(kPanPartial), kAuthoredPan,
                                        0.0f))
            == Steinberg::kResultOk);
    REQUIRE(fx.processBlock(kBlock) == Steinberg::kResultOk);
    checkAuthoredPanOnEveryVoice(engine, "before the sweep");

    constexpr std::size_t kBlocksPerPoint = 40;  // ~0.43 s, several smoother TCs
    float widestSpread = spreadBeforeSweep;
    for (const double bloom : {0.0, 0.25, 0.5, 0.75, 1.0}) {
        fx.setParam(Seraphis::kMacroBloomId, bloom);
        for (std::size_t b = 0; b < kBlocksPerPoint; ++b) {
            REQUIRE(fx.processBlock(kBlock) == Steinberg::kResultOk);

            const float spread = engine.getVoice(0).cloud().getStereoSpread();
            widestSpread = std::max(widestSpread, spread);

            // ONE assertion per block over the WORST of the sixteen slots, not
            // sixteen: the sweep is 200 blocks long and a per-voice CHECK would
            // add 3 400 assertions for no extra diagnostic power - the worst
            // deviation names the failure just as precisely.
            double worstDeviation = 0.0;
            std::size_t worstVoice = 0;
            for (std::size_t v = 0; v < kVoiceSlots; ++v) {
                const double deviation = std::abs(
                    static_cast<double>(engine.getVoice(v).cloud().getPartialPosition(kPanPartial))
                    - static_cast<double>(kAuthoredPan));
                if (deviation > worstDeviation) {
                    worstDeviation = deviation;
                    worstVoice = v;
                }
            }
            INFO("bloom = " << bloom << ", block " << b << ", composed spread = " << spread
                            << ", worst voice = " << worstVoice);
            CHECK(worstDeviation <= kPanTolerance);
            CHECK(static_cast<double>(frame.position[kPanPartial])
                  == Approx(static_cast<double>(kAuthoredPan)).margin(kPanTolerance));
        }
    }

    // NON-VACUITY, and the whole point of arm 6: the sweep really did move the
    // COMPOSED CloudStereoSpread, i.e. setStereoSpread really was called with a
    // changed value and really did clear positionOverridden_ - with ParamID 207
    // untouched throughout.
    INFO("spread before the sweep = " << spreadBeforeSweep << ", widest during = "
                                      << widestSpread);
    REQUIRE(widestSpread > spreadBeforeSweep + 0.05f);
    CHECK(fx.checkCanaries());
}

// =============================================================================
// SC-033 - masking OFF restores the voice, end to end.
//
// THIS IS THE CASE THAT REJECTS A "WALK ONLY THE SET MASK BITS" RE-PUSH BODY.
// Such a body issues setPartialMaskAllVoices(i, false) for every bit that IS
// set and NOTHING AT ALL for a bit that was just CLEARED, so masked_[k] stays
// true for the life of the instance: the mask arm below passes, the un-mask arm
// fails, and CloudFrame::maskBits disagrees with what is actually audible.
//
// ALL SIXTEEN SLOTS ARE ASSERTED, and sixteen notes are therefore held: an IDLE
// slot has every currentAmplitude_ at zero, so it could not tell partial k apart
// from partial k+1 and would make the assertion vacuous rather than strong.
// =============================================================================
TEST_CASE("Seraphis_PartialMask_ToggleOffRestoresTheVoice", "[partial_edit][phase11]") {
    ProcessorFixture fx;
    REQUIRE(fx.prepare(kSampleRate, kBlock) == Steinberg::kResultOk);
    REQUIRE(fx.proc->engineForTest() != nullptr);
    const Krate::DSP::SeraphisEngine& engine = *fx.proc->engineForTest();

    fx.proc->setCloudFrameGateForTest(true);
    // Mutation OFF matters here and is not decoration: the mutation weight w_i
    // is a live BrownianDrift read (harmonic_cloud.h:1721-1732), so at the
    // shipped 0.25 the amplitude a partial recovers to would differ from its
    // pre-mask value by several percent for reasons that have nothing to do with
    // the mask. conditionForSpectralIsolation zeroes it (and the drift).
    conditionForSpectralIsolation(fx);
    fx.setParam(Seraphis::kPolyphonyId, polyphonyNorm(16));

    // Sixteen distinct pitches in one block, so every slot in [0, kMaxVoices) is
    // sounding. process()'s slice loop dispatches every event due at the same
    // offset in one inner pass, which is why a single offset is correct here.
    for (std::size_t i = 0; i < kVoiceSlots; ++i) {
        fx.pushEvent(Steinberg::Vst::Event::kNoteOnEvent, static_cast<Steinberg::int16>(48u + i),
                     kHostVelocity, 0);
    }
    for (std::size_t b = 0; b < kSettleBlocks; ++b) {
        REQUIRE(fx.processBlock(kBlock) == Steinberg::kResultOk);
    }

    const Seraphis::CloudFrame& frame = fx.proc->lastPublishedFrameForTest();
    REQUIRE(static_cast<std::size_t>(frame.partialCount) > kNeighbourPartial);

    // The reference level, PER VOICE - each slot carries a different fundamental
    // and therefore its own anti-alias gain, so a single shared expectation would
    // be wrong.
    std::array<float, kVoiceSlots> beforeMask{};
    std::array<float, kVoiceSlots> beforeNeighbour{};
    for (std::size_t v = 0; v < kVoiceSlots; ++v) {
        const Krate::DSP::HarmonicCloud& cloud = engine.getVoice(v).cloud();
        beforeMask[v] = cloud.getPartialCurrentAmplitude(kMaskPartial);
        beforeNeighbour[v] = cloud.getPartialCurrentAmplitude(kNeighbourPartial);
        INFO("voice " << v);
        // NON-VACUITY: all sixteen really are sounding, so "decayed to ~0" is a
        // change and not the state the slot was already in.
        REQUIRE(engine.getVoiceState(v) != Krate::DSP::VoiceState::Idle);
        REQUIRE(beforeMask[v] > 0.0f);
        REQUIRE(beforeNeighbour[v] > 0.0f);
    }

    // --- MASK ON (a = 1) ------------------------------------------------------
    REQUIRE(sendEdit(*fx.proc, makeEdit(kEditPartialMask, 0,
                                        static_cast<std::uint16_t>(kMaskPartial), 1.0f, 0.0f))
            == Steinberg::kResultOk);
    for (std::size_t b = 0; b < kSettleBlocks; ++b) {
        REQUIRE(fx.processBlock(kBlock) == Steinberg::kResultOk);
    }

    for (std::size_t v = 0; v < kVoiceSlots; ++v) {
        const Krate::DSP::HarmonicCloud& cloud = engine.getVoice(v).cloud();
        INFO("masked - voice " << v << ", before = " << beforeMask[v]
                               << ", now = " << cloud.getPartialCurrentAmplitude(kMaskPartial));
        CHECK(cloud.getPartialCurrentAmplitude(kMaskPartial)
              <= 0.01f * beforeMask[v]);
        // ...and ONLY that partial. A fan-out that masked everything would pass
        // the line above and fail this one.
        CHECK(cloud.getPartialCurrentAmplitude(kNeighbourPartial)
              >= 0.5f * beforeNeighbour[v]);
    }
    CHECK(((frame.maskBits >> kMaskPartial) & 1u) == 1u);

    // --- MASK OFF (a = 0) - the arm the walk-set-bits body cannot pass --------
    REQUIRE(sendEdit(*fx.proc, makeEdit(kEditPartialMask, 0,
                                        static_cast<std::uint16_t>(kMaskPartial), 0.0f, 0.0f))
            == Steinberg::kResultOk);
    for (std::size_t b = 0; b < kSettleBlocks; ++b) {
        REQUIRE(fx.processBlock(kBlock) == Steinberg::kResultOk);
    }

    for (std::size_t v = 0; v < kVoiceSlots; ++v) {
        const Krate::DSP::HarmonicCloud& cloud = engine.getVoice(v).cloud();
        INFO("unmasked - voice " << v << ", before = " << beforeMask[v]
                                 << ", now = "
                                 << cloud.getPartialCurrentAmplitude(kMaskPartial));
        CHECK(static_cast<double>(cloud.getPartialCurrentAmplitude(kMaskPartial))
              == Approx(static_cast<double>(beforeMask[v])).epsilon(0.01));
    }

    // The frame and the engine AGREE - the UI is not showing a mask the voices
    // no longer carry.
    CHECK(((frame.maskBits >> kMaskPartial) & 1u) == 0u);
    CHECK(fx.checkCanaries());
}

// =============================================================================
// SC-024 (T018) - Edit-mode authoring works identically with NO voice held (Q6)
// =============================================================================
// Two arms of the SAME pointer path, on the same slot's same partial:
//
//   (A) a voice sounding at exactly C4, with a drift detune multiplying every
//       CloudFrame::frequencyHz entry - i.e. the frame a live, drifting cloud
//       publishes;
//   (B) no voice at all: activeVoices == 0 and fundamentalHz == 0, which is
//       what publishCloudFrame() writes when nothing is allocated.
//
// The pointer coordinates are computed from the UNDETUNED grid and are byte-for-
// byte the same in both arms, so "identical pointer-delta sequence" is literal
// rather than approximate.
//
// WHY THE TWO MUST AGREE. CloudView::referenceHz() returns the frame's
// fundamentalHz when a voice is sounding and kFallbackReferenceHz (C4) when none
// is - and both are the SAME number here. An implementation that reached for
// frequencyHz[0] instead would divide by a drift-detuned value in arm (A) only,
// so the two arms would disagree by exactly the drift. That is the failure this
// case exists to catch; asserting only that arm (B) does not divide by zero
// would not catch it.
// =============================================================================

namespace {

/// Push one CloudFrame through the SDK entry point the host calls, so the
/// controller's cache is filled the same way a real delivery fills it.
void deliverCloudFrame(Seraphis::Controller& controller, Seraphis::CloudFrame& frame) {
    Steinberg::Vst::DataExchangeBlock block{};
    block.data = &frame;
    block.size = static_cast<Steinberg::uint32>(sizeof(Seraphis::CloudFrame));
    controller.onDataExchangeBlocksReceived(
        static_cast<Steinberg::Vst::DataExchangeUserContextID>(
            Seraphis::kCloudFrameUserContextId),
        static_cast<Steinberg::uint32>(1), &block, static_cast<Steinberg::TBool>(false));
}

}  // namespace

TEST_CASE("Seraphis_EditMode_AuthoringWorksWithoutANote", "[partial_edit][phase11]") {
    constexpr VSTGUI::CCoord kViewW = 400.0;
    constexpr VSTGUI::CCoord kViewH = 300.0;
    constexpr std::uint8_t kSlot = 0;
    constexpr std::uint16_t kIndex = 0;
    constexpr std::uint8_t kFramePartials = 8;

    /// C4 - the pitch arm (A) sounds at, and the value CloudView falls back to
    /// with no voice (cloud_view.h:75). The two being the SAME number is the
    /// mechanism under test.
    constexpr float kC4Hz = Seraphis::UI::kFallbackReferenceHz;

    /// The target of the drag: a perfect fifth. Index 0's authoring window is
    /// [kMinStateRatio, ratios[1] / kAuthorSpacing] = [0.5, 1.9679] for slot 0's
    /// factory SineStack, so 1.5 STORES rather than clamping to a value the
    /// "the edit moved something" comparison could not distinguish.
    constexpr float kTargetRatio = 1.5f;

    /// ~34 cents of detune: four orders of magnitude outside float epsilon if it
    /// were baked into the stored ratio, yet under one pixel on this view, so the
    /// hit test still finds the same partial under the same pointer.
    constexpr float kDriftFactor = 1.02f;

    // A geometry-only view (no controller): the axis map depends on nothing but
    // getViewSize(), so this is the honest source of the pointer coordinates
    // BOTH arms use.
    auto probe = VSTGUI::owned(new Seraphis::UI::CloudView(
        VSTGUI::CRect(0.0, 0.0, kViewW, kViewH), nullptr));
    const VSTGUI::CCoord xAt = probe->xFromPositionForTest(0.0f);
    const VSTGUI::CCoord yDown = probe->yFromHzForTest(kC4Hz);
    const VSTGUI::CCoord yUp = probe->yFromHzForTest(kC4Hz * kTargetRatio);

    REQUIRE(std::abs(yUp - yDown) > Seraphis::UI::kClickSlopPx);  // a DRAG, not a click

    // What a drift-excluding map must produce, and what a drift-baked one would.
    const float expectedRatio = probe->hzFromYForTest(yUp) / kC4Hz;
    const float driftBakedRatio = probe->hzFromYForTest(yUp) / (kC4Hz * kDriftFactor);
    REQUIRE(std::abs(centsBetween(static_cast<double>(expectedRatio),
                                  static_cast<double>(driftBakedRatio)))
            > 30.0);

    struct ArmResult {
        Seraphis::UI::EditMessage message{};
        float ratioBefore = 0.0f;
        float storedRatio = 0.0f;
    };

    const auto runArm = [&](bool voiceSounding) {
        Seraphis::Controller controller;
        REQUIRE(controller.initialize(nullptr) == Steinberg::kResultOk);

        Seraphis::CloudFrame frame{};
        frame.sequence = 1u;
        frame.partialCount = kFramePartials;
        frame.activeVoices = voiceSounding ? static_cast<std::uint16_t>(1)
                                           : static_cast<std::uint16_t>(0);
        // THE UNDETUNED f0 - or 0 when nothing is allocated, which is what
        // publishCloudFrame() writes and what drives the C4 fallback.
        frame.fundamentalHz = voiceSounding ? kC4Hz : 0.0f;
        for (std::size_t i = 0; i < static_cast<std::size_t>(kFramePartials); ++i) {
            const float undetuned = kC4Hz * static_cast<float>(i + 1u);
            // frequencyHz is DRIFT-INCLUSIVE by definition (cloud_frame.h).
            frame.frequencyHz[i] = voiceSounding ? undetuned * kDriftFactor : undetuned;
            frame.amplitude[i] = 0.5f;
            frame.position[i] = 0.0f;
        }
        deliverCloudFrame(controller, frame);

        auto view = VSTGUI::owned(new Seraphis::UI::CloudView(
            VSTGUI::CRect(0.0, 0.0, kViewW, kViewH), &controller));
        view->setMode(Seraphis::UI::CloudView::Mode::Edit);
        view->setSelectedSlot(static_cast<int>(kSlot));
        view->onTimerForTest();
        view->renderForTest();
        REQUIRE(view->pointsDrawnForTest() == static_cast<std::size_t>(kFramePartials));

        // The SAME absolute pointer in both arms still lands on the same partial:
        // the drift moves the drawn point by well under the hit radius.
        REQUIRE(view->hitTestForTest(VSTGUI::CPoint(xAt, yDown)) == static_cast<int>(kIndex));

        const VSTGUI::CButtonState left(VSTGUI::kLButton);
        VSTGUI::CPoint down(xAt, yDown);
        REQUIRE(view->onMouseDown(down, left) == VSTGUI::kMouseEventHandled);
        VSTGUI::CPoint moved(xAt, yUp);
        view->onMouseMoved(moved, left);
        VSTGUI::CPoint up(xAt, yUp);
        view->onMouseUp(up, left);

        ArmResult result;
        result.message = controller.lastSentEditMessageForTest();
        REQUIRE(controller.editMessageSendCountForTest() == 1u);

        // Through the SHIPPING wire, into a processor, and read back what was
        // actually STORED - the quantity SC-024 is written about.
        Seraphis::Processor processor;
        result.ratioBefore =
            processor.spectralAuthoringSlotForTest(static_cast<int>(kSlot)).ratios[kIndex];
        REQUIRE(sendEdit(processor, result.message) == Steinberg::kResultOk);
        result.storedRatio =
            processor.spectralAuthoringSlotForTest(static_cast<int>(kSlot)).ratios[kIndex];

        controller.terminate();
        return result;
    };

    const ArmResult withVoice = runArm(/*voiceSounding=*/true);
    const ArmResult withoutVoice = runArm(/*voiceSounding=*/false);

    SECTION("both arms author the same partial of the same slot, as a ratio edit") {
        for (const ArmResult* arm : {&withVoice, &withoutVoice}) {
            CHECK(static_cast<unsigned>(arm->message.kind) == 1u);
            CHECK(static_cast<unsigned>(arm->message.slot) == static_cast<unsigned>(kSlot));
            CHECK(static_cast<unsigned>(arm->message.index) == static_cast<unsigned>(kIndex));
        }
    }

    SECTION("the two arms agree EXACTLY - on the wire and on what was stored") {
        // Bit-for-bit: identical pointer, identical reference, identical code
        // path. There is no toolchain FP spread inside one process to tolerate.
        CHECK(withVoice.message.a == withoutVoice.message.a);
        CHECK(withVoice.storedRatio == withoutVoice.storedRatio);
    }

    SECTION("arm (A) carries NO drift-baked error despite active drift") {
        CHECK(withVoice.message.a == Approx(expectedRatio).epsilon(1.0e-6));
        // ...and it is demonstrably NOT the value a frequencyHz[i]-based
        // reference would have produced.
        CHECK(std::abs(centsBetween(static_cast<double>(withVoice.message.a),
                                    static_cast<double>(driftBakedRatio)))
              > 30.0);
    }

    SECTION("NON-VACUITY: the edit really moved the stored ratio") {
        CHECK(withVoice.ratioBefore == withoutVoice.ratioBefore);
        CHECK(withVoice.storedRatio != withVoice.ratioBefore);
        CHECK(withVoice.storedRatio == Approx(expectedRatio).epsilon(1.0e-6));
    }
}
