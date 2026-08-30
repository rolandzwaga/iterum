// ==============================================================================
// Layer 2: SIMD-Accelerated Grain Span Accumulation (Phase 11.5)
// ==============================================================================
// The gather half of AtmosphereEngine's grain sweep. The engine's scalar pass
// walks one grain's span of a control chunk and precomputes, per output
// sample, the ring indices + interpolation weight for the L read, the
// (possibly decorrelated) R read, and the envelope table lookup - with
// arithmetic bit-identical to RollingCaptureBuffer::LinearReader and
// GrainEnvelope::lookup. This kernel then does the memory half: six gathers,
// three lerps and the pan-weighted accumulate into the bus, vectorized.
//
// EVERY OPERATION IS PER-LANE - no cross-lane reduction anywhere - so the
// result of sample i is a function of sample i's inputs alone and the
// vector/tail grouping (and therefore the caller's block partition) cannot
// change any sample's value beyond the FMA-vs-mul+add rounding that is
// uniform per sample.
//
// Reference pattern: harmonic_oscillator_bank_simd.{h,cpp}.
// ==============================================================================

#pragma once

#include <cstddef>
#include <cstdint>

namespace Krate {
namespace DSP {

/// @brief Accumulate one grain's span into the stereo bus.
///
/// For each sample k in [0, count):
///   envK  = envTable[envI0[k]] + envFrac[k] * (envTable[envI1[k]] - envTable[envI0[k]])
///   lK    = ringL[idxL0[k]] + fracL[k] * (ringL[idxL1[k]] - ringL[idxL0[k]])
///   rK    = ringR[idxR0[k]] + fracR[k] * (ringR[idxR1[k]] - ringR[idxR0[k]])
///   busL[k] += envK * panL * lK
///   busR[k] += envK * panR * rK
///
/// A non-decorrelated grain passes idxR0 == idxL0, idxR1 == idxL1 and
/// fracR == fracL (the same arrays, not copies).
///
/// All index values must be in-bounds for their tables; the caller's scalar
/// pass guarantees it (ring indices are masked, envelope indices clamped).
void accumulateGrainSpanSIMD(const float* ringL, const float* ringR, const std::int32_t* idxL0,
                             const std::int32_t* idxL1, const float* fracL,
                             const std::int32_t* idxR0, const std::int32_t* idxR1,
                             const float* fracR, const float* envTable,
                             const std::int32_t* envI0, const std::int32_t* envI1,
                             const float* envFrac, float panL, float panR, std::size_t count,
                             float* busL, float* busR) noexcept;

}  // namespace DSP
}  // namespace Krate
