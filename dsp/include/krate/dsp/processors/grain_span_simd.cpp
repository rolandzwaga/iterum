// ==============================================================================
// Layer 2: SIMD-Accelerated Grain Span Accumulation (Phase 11.5)
// ==============================================================================
// See grain_span_simd.h. Uses Highway's self-inclusion pattern: foreach_target
// re-includes this file once per ISA target; HWY_EXPORT/HWY_DYNAMIC_DISPATCH
// select the best at runtime.
//
// Reference: harmonic_oscillator_bank_simd.cpp (pattern).
// ==============================================================================

#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "krate/dsp/processors/grain_span_simd.cpp"
#include "hwy/foreach_target.h"  // NOLINT(misc-header-include-cycle) Highway self-inclusion by design
#include "hwy/highway.h"

#include <cmath>  // std::fma - the tail must round exactly like the FMA lanes
#include <cstddef>
#include <cstdint>

HWY_BEFORE_NAMESPACE();

// NOLINTNEXTLINE(modernize-concat-nested-namespaces) HWY_NAMESPACE is a macro
namespace Krate {
namespace DSP {
namespace HWY_NAMESPACE {

namespace hn = hwy::HWY_NAMESPACE;

// NOLINTNEXTLINE(misc-use-internal-linkage) exported via HWY_EXPORT
void AccumulateGrainSpanSIMDImpl(
    const float* HWY_RESTRICT ringL, const float* HWY_RESTRICT ringR,
    const std::int32_t* HWY_RESTRICT idxL0, const std::int32_t* HWY_RESTRICT idxL1,
    const float* HWY_RESTRICT fracL, const std::int32_t* HWY_RESTRICT idxR0,
    const std::int32_t* HWY_RESTRICT idxR1, const float* HWY_RESTRICT fracR,
    const float* HWY_RESTRICT envTable, const std::int32_t* HWY_RESTRICT envI0,
    const std::int32_t* HWY_RESTRICT envI1, const float* HWY_RESTRICT envFrac, float panL,
    float panR, std::size_t count, float* HWY_RESTRICT busL, float* HWY_RESTRICT busR) {
    const hn::ScalableTag<float> d;
    const hn::RebindToSigned<decltype(d)> di;
    const std::size_t N = hn::Lanes(d);

    const auto vPanL = hn::Set(d, panL);
    const auto vPanR = hn::Set(d, panR);

    std::size_t k = 0;
    for (; k + N <= count; k += N) {
        // Envelope: gather both taps, lerp.
        const auto vEnvA = hn::GatherIndex(d, envTable, hn::LoadU(di, envI0 + k));
        const auto vEnvB = hn::GatherIndex(d, envTable, hn::LoadU(di, envI1 + k));
        const auto vEnvF = hn::LoadU(d, envFrac + k);
        const auto vEnv = hn::MulAdd(vEnvF, hn::Sub(vEnvB, vEnvA), vEnvA);

        // Left channel: gather both taps, lerp.
        const auto vLA = hn::GatherIndex(d, ringL, hn::LoadU(di, idxL0 + k));
        const auto vLB = hn::GatherIndex(d, ringL, hn::LoadU(di, idxL1 + k));
        const auto vLF = hn::LoadU(d, fracL + k);
        const auto vL = hn::MulAdd(vLF, hn::Sub(vLB, vLA), vLA);

        // Right channel (decorrelated grains carry their own index arrays).
        const auto vRA = hn::GatherIndex(d, ringR, hn::LoadU(di, idxR0 + k));
        const auto vRB = hn::GatherIndex(d, ringR, hn::LoadU(di, idxR1 + k));
        const auto vRF = hn::LoadU(d, fracR + k);
        const auto vR = hn::MulAdd(vRF, hn::Sub(vRB, vRA), vRA);

        // busL += (env * panL) * l ; busR += (env * panR) * r
        const auto vBusL = hn::MulAdd(hn::Mul(vEnv, vPanL), vL, hn::LoadU(d, busL + k));
        const auto vBusR = hn::MulAdd(hn::Mul(vEnv, vPanR), vR, hn::LoadU(d, busR + k));
        hn::StoreU(vBusL, d, busL + k);
        hn::StoreU(vBusR, d, busR + k);
    }

    // Scalar tail, ROUNDING-MATCHED to the vector lanes: every MulAdd above is
    // a fused multiply-add, and a block-partition shift can move a given
    // sample between a vector lane and this tail - so the tail must fuse too
    // (std::fma is correctly-rounded, i.e. identical to the hardware FMA the
    // lanes use) or the render would be partition-variant at the ULP level.
    for (; k < count; ++k) {
        const float envA = envTable[envI0[k]];
        const float env = std::fma(envFrac[k], envTable[envI1[k]] - envA, envA);
        const float lA = ringL[idxL0[k]];
        const float l = std::fma(fracL[k], ringL[idxL1[k]] - lA, lA);
        const float rA = ringR[idxR0[k]];
        const float r = std::fma(fracR[k], ringR[idxR1[k]] - rA, rA);
        busL[k] = std::fma(env * panL, l, busL[k]);
        busR[k] = std::fma(env * panR, r, busR[k]);
    }
}

}  // namespace HWY_NAMESPACE
}  // namespace DSP
}  // namespace Krate

HWY_AFTER_NAMESPACE();

#if HWY_ONCE

#include "krate/dsp/processors/grain_span_simd.h"

// NOLINTNEXTLINE(modernize-concat-nested-namespaces) HWY_NAMESPACE dispatch section
namespace Krate {
namespace DSP {

HWY_EXPORT(AccumulateGrainSpanSIMDImpl);

void accumulateGrainSpanSIMD(const float* ringL, const float* ringR, const std::int32_t* idxL0,
                             const std::int32_t* idxL1, const float* fracL,
                             const std::int32_t* idxR0, const std::int32_t* idxR1,
                             const float* fracR, const float* envTable,
                             const std::int32_t* envI0, const std::int32_t* envI1,
                             const float* envFrac, float panL, float panR, std::size_t count,
                             float* busL, float* busR) noexcept {
    HWY_DYNAMIC_DISPATCH(AccumulateGrainSpanSIMDImpl)(ringL, ringR, idxL0, idxL1, fracL, idxR0,
                                                      idxR1, fracR, envTable, envI0, envI1,
                                                      envFrac, panL, panR, count, busL, busR);
}

}  // namespace DSP
}  // namespace Krate

#endif  // HWY_ONCE
