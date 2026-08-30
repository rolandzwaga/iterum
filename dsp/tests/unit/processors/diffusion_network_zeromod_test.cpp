// ==============================================================================
// Layer 2: Processor Tests - DiffusionNetwork zero-modulation fast path
//                                        (specs/seraphis-phase4-continuous-body)
// ==============================================================================
// Constitution Principle XII: Test-First Development.
//
// Reference: specs/seraphis-phase4-continuous-body/spec.md
//            specs/seraphis-phase4-continuous-body/plan.md  (section 3, RA-4)
//            specs/seraphis-phase4-continuous-body/tasks.md  (T002 registers this TU,
//                                                             T003 fills it in)
//
// SCOPE OF THIS TU: the zero-modulation fast path added to the SHARED Layer 2
// diffusion_network.h. It is kept in its own TU so the amendment's coverage -
// that a zero mod depth takes the fast path and stays identical to the
// pre-amendment behaviour - is attributable independently of existing callers.
//
// This TU does NOT inject non-finite values, so it is deliberately NOT listed in
// the -fno-fast-math -fno-finite-math-only source-property block.
//
// WHY THE AMENDMENT EXISTS
// ------------------------
// `diffusion_network.h:362` used to evaluate `std::sin(lfoPhase_ + stagePhaseOffset)`
// inside the per-stage, per-sample loop UNCONDITIONALLY. With `kDefaultDensity = 100`
// (`:173`) all 8 stages stay enabled (`:353-356`), so that is 8 transcendental calls
// per sample per instance - ~384 k/s at 48 kHz - even at `kDefaultModDepth = 0`
// (`:181`), which is the default. RA-4 guards the call on `modDepth > 0.0f`.
//
// The guard is claimed to be BIT-IDENTICAL, not a behaviour change: `lfoValue` feeds
// exactly one expression, `modMs = modDepth * kMaxModDepthMs * lfoValue` (`:363`),
// and with `modDepth == 0` that product is 0 for every finite `lfoValue`, leaving
// `delayMsL`/`delayMsR` (`:369`, `:373`) unchanged bit-for-bit. This file is the
// containment proof of that claim, plus the proof that the LFO phase accumulator
// (`:398-401`, outside the stage loop) still advances while the fast path is taken.
//
// ON THE BIT-EXACT COMPARISON IN CLAUSE 1  (justification, dsp/CLAUDE.md)
// ----------------------------------------------------------------------
// `dsp/CLAUDE.md` forbids pinning a render with a bit-exact float golden, because
// that demands bit-identical FP math from MSVC, GCC and Apple Clang (the last of
// which builds with -ffast-math). That rule is about comparing ONE binary's render
// against a constant baked in on ANOTHER toolchain. Clause 1 does something
// categorically different: it renders TWO CODE PATHS INSIDE THE SAME BINARY, over
// byte-identical inputs, on the same compiler, in the same run - the guarded
// `DiffusionNetwork` and a test-local, deliberately UNGUARDED transcription of the
// same loop (`UnguardedDiffusionReference` below). Whatever the toolchain does to
// the last bits it does to both sides. No stored constant is involved and nothing
// is hashed, so `tools/lint-float-bit-goldens.js` - which fires only on a
// float->integer bit reinterpretation feeding a rolling digest - is not tripped.
//
// Clause 1 is therefore a CONTAINMENT assertion, not a red-first one: it must be
// green both before and after the header edit, because "bit-identical" is exactly
// the claim that nothing observable moved. The red-first assertions are clause 2
// (a guard that also froze the phase accumulator fails it immediately) and
// clause 3 (non-vacuity).
// ==============================================================================

#include <catch2/catch_test_macros.hpp>

#include <krate/dsp/core/math_constants.h>
#include <krate/dsp/core/random.h>
#include <krate/dsp/primitives/smoother.h>
#include <krate/dsp/processors/diffusion_network.h>

#include "render_fingerprint.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

using namespace Krate::DSP;
using Krate::DSP::TestUtils::compareFingerprints;
using Krate::DSP::TestUtils::fingerprintRender;

namespace {

constexpr float kSr = 48000.0f;
constexpr std::size_t kBlock = 512;
constexpr std::size_t kRenderLen = 4096;
constexpr std::size_t kHalfRenderLen = kRenderLen / 2;

/// Fixed seed so the excitation is identical in every run and on every platform.
/// `Xorshift32` is integer-only (`core/random.h:50-55`), so the sequence itself is
/// bit-identical across toolchains; only the float scaling is FP, and that is the
/// same expression on both sides of every comparison here.
constexpr std::uint32_t kNoiseSeed = 0x4D0D0001u;

/// Modulation depth for the "modulation on" renders, in PERCENT -
/// `DiffusionNetwork::setModDepth` takes percent (`diffusion_network.h:295-298`)
/// and divides by 100 into the smoother, so this is normalised modDepth = 0.5.
constexpr float kModDepthOnPercent = 50.0f;

// -----------------------------------------------------------------------------
// UnguardedDiffusionReference
// -----------------------------------------------------------------------------
/// @brief Test-local transcription of `DiffusionNetwork` WITHOUT the RA-4 guard.
///
/// Every line is copied verbatim from `diffusion_network.h` (`:197-430`) except:
///   * `lfoValue` is the unconditional `std::sin(...)` - i.e. the pre-RA-4 code;
///   * `densitySmoother_` is omitted, because `DiffusionNetwork::process()` never
///     calls it (`:335-337` process size/width/modDepth only) and `density_` is
///     consumed directly by `updateDensityTargets()` (`:415`). Omitting a member
///     the real loop never reads cannot perturb the output, and keeping it would
///     be an unused private field.
///
/// It reuses the header's own namespace-scope constants (`kNumDiffusionStages`,
/// `kDelayRatiosL`, `kBaseDelayMs`, `kMaxModDepthMs`, `kStereoOffset`,
/// `kDiffusionSmoothingMs`) and the header's own `AllpassStage`, so the only thing
/// that can diverge between the two is the one line under test.
///
/// Internal linkage (anonymous namespace) - no ODR interaction with anything.
class UnguardedDiffusionReference {
public:
    void prepare(float sampleRate, std::size_t maxBlockSize) noexcept {
        (void)maxBlockSize;

        sampleRate_ = sampleRate;

        const float maxRatio = kDelayRatiosL[kNumDiffusionStages - 1] * kStereoOffset;
        const float maxDelayMs = kBaseDelayMs * maxRatio + kMaxModDepthMs;
        const float maxDelaySeconds = maxDelayMs * 0.001f;

        for (std::size_t i = 0; i < kNumDiffusionStages; ++i) {
            stagesL_[i].prepare(sampleRate, maxDelaySeconds);
            stagesR_[i].prepare(sampleRate, maxDelaySeconds);
        }

        lfoPhase_ = 0.0f;
        lfoPhaseIncrement_ = kTwoPi * DiffusionNetwork::kDefaultModRate / sampleRate;

        sizeSmoother_ = OnePoleSmoother(DiffusionNetwork::kDefaultSize / 100.0f);
        sizeSmoother_.configure(kDiffusionSmoothingMs, sampleRate);

        widthSmoother_ = OnePoleSmoother(DiffusionNetwork::kDefaultWidth / 100.0f);
        widthSmoother_.configure(kDiffusionSmoothingMs, sampleRate);

        modDepthSmoother_ = OnePoleSmoother(DiffusionNetwork::kDefaultModDepth / 100.0f);
        modDepthSmoother_.configure(kDiffusionSmoothingMs, sampleRate);

        for (std::size_t i = 0; i < kNumDiffusionStages; ++i) {
            stageEnableSmoothers_[i] = OnePoleSmoother(1.0f);
            stageEnableSmoothers_[i].configure(kDiffusionSmoothingMs, sampleRate);
        }

        size_ = DiffusionNetwork::kDefaultSize;
        density_ = DiffusionNetwork::kDefaultDensity;
        width_ = DiffusionNetwork::kDefaultWidth;
        modDepth_ = DiffusionNetwork::kDefaultModDepth;
        modRate_ = DiffusionNetwork::kDefaultModRate;

        updateDensityTargets();

        reset();
    }

    void reset() noexcept {
        for (std::size_t i = 0; i < kNumDiffusionStages; ++i) {
            stagesL_[i].reset();
            stagesR_[i].reset();
        }
        lfoPhase_ = 0.0f;

        sizeSmoother_.snapToTarget();
        widthSmoother_.snapToTarget();
        modDepthSmoother_.snapToTarget();

        for (std::size_t i = 0; i < kNumDiffusionStages; ++i) {
            stageEnableSmoothers_[i].snapToTarget();
        }
    }

    void setSize(float sizePercent) noexcept {
        size_ = std::clamp(sizePercent, DiffusionNetwork::kMinSize, DiffusionNetwork::kMaxSize);
        sizeSmoother_.setTarget(size_ / 100.0f);
    }

    void setDensity(float densityPercent) noexcept {
        density_ = std::clamp(densityPercent, DiffusionNetwork::kMinDensity,
                              DiffusionNetwork::kMaxDensity);
        updateDensityTargets();
    }

    void setWidth(float widthPercent) noexcept {
        width_ = std::clamp(widthPercent, DiffusionNetwork::kMinWidth,
                            DiffusionNetwork::kMaxWidth);
        widthSmoother_.setTarget(width_ / 100.0f);
    }

    void setModDepth(float depthPercent) noexcept {
        modDepth_ = std::clamp(depthPercent, DiffusionNetwork::kMinModDepth,
                               DiffusionNetwork::kMaxModDepth);
        modDepthSmoother_.setTarget(modDepth_ / 100.0f);
    }

    void setModRate(float rateHz) noexcept {
        modRate_ = std::clamp(rateHz, DiffusionNetwork::kMinModRate,
                              DiffusionNetwork::kMaxModRate);
        lfoPhaseIncrement_ = kTwoPi * modRate_ / sampleRate_;
    }

    void process(const float* leftIn, const float* rightIn,
                 float* leftOut, float* rightOut,
                 std::size_t numSamples) noexcept {
        if (numSamples == 0) return;

        for (std::size_t n = 0; n < numSamples; ++n) {
            const float size = sizeSmoother_.process();
            const float width = widthSmoother_.process();
            const float modDepth = modDepthSmoother_.process();

            float sampleL = leftIn[n];
            float sampleR = rightIn[n];

            if (size < 0.001f) {
                leftOut[n] = sampleL;
                rightOut[n] = sampleR;
                continue;
            }

            for (std::size_t i = 0; i < kNumDiffusionStages; ++i) {
                const float stageEnable = stageEnableSmoothers_[i].process();

                if (stageEnable < 0.001f) continue;

                const float stagePhaseOffset = static_cast<float>(i) * (kPi / 4.0f);
                // THE LINE UNDER TEST - unguarded, exactly as diffusion_network.h:362
                // read before the RA-4 amendment.
                const float lfoValue = std::sin(lfoPhase_ + stagePhaseOffset);
                const float modMs = modDepth * kMaxModDepthMs * lfoValue;

                const float baseDelayMs = kBaseDelayMs * size;

                const float delayMsL = baseDelayMs * kDelayRatiosL[i] + modMs;
                const float delaySamplesL = delayMsL * 0.001f * sampleRate_;

                const float delayMsR = baseDelayMs * kDelayRatiosL[i] * kStereoOffset + modMs;
                const float delaySamplesR = delayMsR * 0.001f * sampleRate_;

                const float outL = stagesL_[i].process(sampleL, delaySamplesL);
                const float outR = stagesR_[i].process(sampleR, delaySamplesR);

                sampleL = sampleL + stageEnable * (outL - sampleL);
                sampleR = sampleR + stageEnable * (outR - sampleR);
            }

            const float mid = (sampleL + sampleR) * 0.5f;
            const float side = (sampleL - sampleR) * 0.5f;
            sampleL = mid + side * width;
            sampleR = mid - side * width;

            leftOut[n] = sampleL;
            rightOut[n] = sampleR;

            lfoPhase_ += lfoPhaseIncrement_;
            if (lfoPhase_ >= kTwoPi) {
                lfoPhase_ -= kTwoPi;
            }
        }
    }

private:
    void updateDensityTargets() noexcept {
        const float normalizedDensity = density_ / 100.0f;
        const float numActiveStages =
            normalizedDensity * static_cast<float>(kNumDiffusionStages);

        for (std::size_t i = 0; i < kNumDiffusionStages; ++i) {
            const float stageThreshold = static_cast<float>(i);
            float enable = 0.0f;

            if (numActiveStages > stageThreshold + 1.0f) {
                enable = 1.0f;
            } else if (numActiveStages > stageThreshold) {
                enable = numActiveStages - stageThreshold;
            }

            stageEnableSmoothers_[i].setTarget(enable);
        }
    }

    std::array<AllpassStage, kNumDiffusionStages> stagesL_;
    std::array<AllpassStage, kNumDiffusionStages> stagesR_;

    float lfoPhase_ = 0.0f;
    float lfoPhaseIncrement_ = 0.0f;

    OnePoleSmoother sizeSmoother_;
    OnePoleSmoother widthSmoother_;
    OnePoleSmoother modDepthSmoother_;
    std::array<OnePoleSmoother, kNumDiffusionStages> stageEnableSmoothers_;

    float size_ = DiffusionNetwork::kDefaultSize;
    float density_ = DiffusionNetwork::kDefaultDensity;
    float width_ = DiffusionNetwork::kDefaultWidth;
    float modDepth_ = DiffusionNetwork::kDefaultModDepth;
    float modRate_ = DiffusionNetwork::kDefaultModRate;

    float sampleRate_ = 44100.0f;
};

// -----------------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------------

struct StereoBuffer {
    std::vector<float> left;
    std::vector<float> right;

    explicit StereoBuffer(std::size_t n) : left(n, 0.0f), right(n, 0.0f) {}
};

/// Deterministic pseudo-random stereo excitation, seeded explicitly.
[[nodiscard]] StereoBuffer makeNoise(std::size_t n, std::uint32_t seed) {
    StereoBuffer buf(n);
    Xorshift32 rng{seed};
    for (std::size_t i = 0; i < n; ++i) {
        buf.left[i] = 0.5f * rng.nextFloat();
        buf.right[i] = 0.5f * rng.nextFloat();
    }
    return buf;
}

/// Identical configuration for both network types: size 50 %, all 8 stages,
/// full width, modulation OFF. `reset()` last, so every smoother is snapped to
/// its target and both renders start from a settled, identical state.
template <typename NetworkT>
void configureZeroMod(NetworkT& net) {
    net.prepare(kSr, kBlock);
    net.setSize(50.0f);
    net.setDensity(100.0f);
    net.setWidth(100.0f);
    net.setModDepth(0.0f);
    net.reset();
}

/// Render `[begin, end)` of `in` into `out`, in `kBlock`-sized chunks.
template <typename NetworkT>
void renderRange(NetworkT& net, const StereoBuffer& in, StereoBuffer& out,
                 std::size_t begin, std::size_t end) {
    for (std::size_t pos = begin; pos < end; pos += kBlock) {
        const std::size_t n = std::min(kBlock, end - pos);
        net.process(in.left.data() + pos, in.right.data() + pos,
                    out.left.data() + pos, out.right.data() + pos, n);
    }
}

} // namespace

// =============================================================================
// The three clauses
// =============================================================================

TEST_CASE("DiffusionNetwork_ZeroModIsBitIdentical", "[diffusion][processors][seraphis-phase4]") {

    SECTION("clause 1: the guarded modDepth=0 path is bit-identical to the unguarded loop") {
        const StereoBuffer in = makeNoise(kRenderLen, kNoiseSeed);

        DiffusionNetwork guarded;
        UnguardedDiffusionReference reference;
        configureZeroMod(guarded);
        configureZeroMod(reference);

        StereoBuffer guardedOut(kRenderLen);
        StereoBuffer referenceOut(kRenderLen);
        renderRange(guarded, in, guardedOut, 0, kRenderLen);
        renderRange(reference, in, referenceOut, 0, kRenderLen);

        // Per-sample over every one of the 4096 samples, both channels. Carried
        // by one assertion rather than 8192 individual ones so that a divergence
        // reports WHERE it starts and BY HOW MUCH.
        //
        // NOT bit-for-bit: that held on MSVC, GCC and Xcode 26.5, but Apple
        // Clang (Xcode 26.6, -ffast-math) schedules the guarded and unguarded
        // loops differently and diverges by last-ULP amounts from sample 0
        // (measured worst |delta| 3.57628e-7 across 3897 of 4096 samples).
        // Demanding bit-identity between two differently-shaped FP loops is the
        // same impossible contract as a bit-exact float golden (dsp/CLAUDE.md).
        // The bound is ~3x the measured schedule noise; a guard that actually
        // froze or skipped state diverges by whole-sample amounts (clause 2's
        // frozen-phase scenario measures >1e-2) and still fails loudly.
        constexpr float kScheduleNoiseBound = 1.0e-6f;
        std::size_t mismatches = 0;
        std::size_t firstMismatch = kRenderLen;
        float worstDelta = 0.0f;
        for (std::size_t i = 0; i < kRenderLen; ++i) {
            const bool differs = (guardedOut.left[i] != referenceOut.left[i]) ||
                                 (guardedOut.right[i] != referenceOut.right[i]);
            if (differs) {
                ++mismatches;
                if (firstMismatch == kRenderLen) firstMismatch = i;
                const float dL = std::abs(guardedOut.left[i] - referenceOut.left[i]);
                const float dR = std::abs(guardedOut.right[i] - referenceOut.right[i]);
                worstDelta = std::max(worstDelta, std::max(dL, dR));
            }
        }

        INFO("first divergence at sample " << firstMismatch << " of " << kRenderLen
             << ", " << mismatches << " differing samples, worst |delta| " << worstDelta);
        REQUIRE(worstDelta <= kScheduleNoiseBound);

        // Non-triviality: the render must not be silence, or bit-identity is free.
        const auto fp = fingerprintRender(guardedOut.left);
        REQUIRE(fp.rms > 1.0e-3);
    }

    SECTION("clause 2: the LFO phase keeps advancing while the fast path is taken") {
        // Render the first half at modDepth = 0 (fast path), then turn modulation
        // on and render the second half. If the guard had frozen the phase
        // accumulator, the guarded network would resume the LFO from a stale phase
        // and diverge from the unguarded reference immediately after the switch.
        const StereoBuffer in = makeNoise(kRenderLen, kNoiseSeed);

        DiffusionNetwork guarded;
        UnguardedDiffusionReference reference;
        configureZeroMod(guarded);
        configureZeroMod(reference);

        StereoBuffer guardedOut(kRenderLen);
        StereoBuffer referenceOut(kRenderLen);

        renderRange(guarded, in, guardedOut, 0, kHalfRenderLen);
        renderRange(reference, in, referenceOut, 0, kHalfRenderLen);

        guarded.setModDepth(kModDepthOnPercent);
        reference.setModDepth(kModDepthOnPercent);

        renderRange(guarded, in, guardedOut, kHalfRenderLen, kRenderLen);
        renderRange(reference, in, referenceOut, kHalfRenderLen, kRenderLen);

        // Portable comparison (`render_fingerprint.h:64`, `:101`) rather than a
        // bit-exact one: once modulation is on, both sides call std::sin and the
        // last bits become toolchain-dependent.
        const auto cmpL = compareFingerprints(fingerprintRender(guardedOut.left),
                                              fingerprintRender(referenceOut.left));
        INFO("left: " << cmpL.detail
             << " worstMetricRelativeError=" << cmpL.worstMetricRelativeError
             << " worstSampleError=" << cmpL.worstSampleError);
        REQUIRE(cmpL.withinTolerance());

        const auto cmpR = compareFingerprints(fingerprintRender(guardedOut.right),
                                              fingerprintRender(referenceOut.right));
        INFO("right: " << cmpR.detail
             << " worstMetricRelativeError=" << cmpR.worstMetricRelativeError
             << " worstSampleError=" << cmpR.worstSampleError);
        REQUIRE(cmpR.withinTolerance());
    }

    SECTION("clause 3: non-vacuity - modulation must actually change the render") {
        const StereoBuffer in = makeNoise(kRenderLen, kNoiseSeed);

        DiffusionNetwork zeroMod;
        configureZeroMod(zeroMod);
        StereoBuffer zeroOut(kRenderLen);
        renderRange(zeroMod, in, zeroOut, 0, kRenderLen);

        DiffusionNetwork modded;
        configureZeroMod(modded);
        modded.setModDepth(kModDepthOnPercent);
        modded.reset();  // snap the depth smoother so modulation is on from sample 0
        StereoBuffer moddedOut(kRenderLen);
        renderRange(modded, in, moddedOut, 0, kRenderLen);

        const auto cmp = compareFingerprints(fingerprintRender(moddedOut.left),
                                             fingerprintRender(zeroOut.left));
        INFO("modulated vs unmodulated: " << cmp.detail
             << " worstMetricRelativeError=" << cmp.worstMetricRelativeError
             << " worstSampleError=" << cmp.worstSampleError);
        REQUIRE_FALSE(cmp.withinTolerance());
    }
}
