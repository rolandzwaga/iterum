// ==============================================================================
// Layer 0: Core Utilities
// random.h - Fast Pseudo-Random Number Generation
// ==============================================================================
// Constitution Principle II: Real-Time Audio Thread Safety
// - No allocation, no locks, no exceptions, no I/O
//
// Constitution Principle III: Modern C++ Standards
// - constexpr where possible, const, value semantics
//
// Constitution Principle IX: Layered DSP Architecture
// - Layer 0: NO dependencies on higher layers
// ==============================================================================

#pragma once

#include <cstddef>
#include <cstdint>

namespace Krate {
namespace DSP {

// ==============================================================================
// Xorshift32 PRNG
// ==============================================================================

/// Fast 32-bit pseudo-random number generator using xorshift algorithm.
///
/// Xorshift32 provides a good balance of speed and quality for audio noise
/// generation. It has a period of 2^32-1 and passes most statistical tests.
///
/// Algorithm: Marsaglia's xorshift with shifts 13, 17, 5
///
/// @note Real-time safe: no allocation, no exceptions
/// @note NOT cryptographically secure - for audio/DSP use only
///
/// @example Basic usage:
///     Xorshift32 rng(12345);
///     float noise = rng.nextFloat();  // Returns [-1.0, 1.0]
///
class Xorshift32 {
public:
    /// Construct with seed value.
    /// @param seedValue Initial seed (0 is automatically replaced with default)
    explicit constexpr Xorshift32(uint32_t seedValue = 1) noexcept
        : state_(seedValue != 0 ? seedValue : kDefaultSeed) {}

    /// Generate next 32-bit unsigned integer.
    /// @return Random uint32_t in range [1, 2^32-1]
    [[nodiscard]] constexpr uint32_t next() noexcept {
        state_ ^= state_ << 13;
        state_ ^= state_ >> 17;
        state_ ^= state_ << 5;
        return state_;
    }

    /// Generate next float in bipolar range.
    /// @return Random float in range [-1.0, 1.0]
    [[nodiscard]] constexpr float nextFloat() noexcept {
        // Convert uint32_t to float in [0, 1] then scale to [-1, 1]
        // Divide by (2^32 - 1) to get [0, 1], then scale
        return static_cast<float>(next()) * kToFloat * 2.0f - 1.0f;
    }

    /// Generate next float in unipolar range.
    /// @return Random float in range [0.0, 1.0]
    [[nodiscard]] constexpr float nextUnipolar() noexcept {
        return static_cast<float>(next()) * kToFloat;
    }

    /// Reseed the generator.
    /// @param seedValue New seed (0 is automatically replaced with default)
    constexpr void seed(uint32_t seedValue) noexcept {
        state_ = (seedValue != 0) ? seedValue : kDefaultSeed;
    }

    /// Get current state (for debugging/serialization).
    /// @return Current internal state
    [[nodiscard]] constexpr uint32_t state() const noexcept {
        return state_;
    }

private:
    /// Default seed used when 0 is passed (0 would cause generator to output only zeros)
    static constexpr uint32_t kDefaultSeed = 2463534242u;

    /// Conversion factor from uint32_t to [0, 1] float
    /// 1.0 / (2^32 - 1) = 1.0 / 4294967295.0
    static constexpr float kToFloat = 2.3283064370807974e-10f;

    uint32_t state_;
};

// ==============================================================================
// Stream seed derivation
// ==============================================================================

/// @brief Derive a guaranteed-non-zero per-stream seed from a base seed and a salt.
/// lowbias32 finaliser. The non-zero substitution is load-bearing: Xorshift32::seed()
/// silently replaces 0 with its own default (random.h:72-74), so two lanes hashing to 0
/// would COLLAPSE ONTO ONE STREAM.
[[nodiscard]] constexpr std::uint32_t deriveStreamSeed(std::uint32_t base,
                                                       std::size_t salt) noexcept {
    std::uint32_t h = base ^ (static_cast<std::uint32_t>(salt + 1u) * 0x9E3779B9u);
    h ^= h >> 16;
    h *= 0x7FEB352Du;
    h ^= h >> 15;
    h *= 0x846CA68Bu;
    h ^= h >> 16;
    return (h != 0u) ? h : 0x2545F491u;
}

} // namespace DSP
} // namespace Krate
