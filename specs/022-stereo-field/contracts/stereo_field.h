// ==============================================================================
// Layer 3: System Component - StereoField (API Contract)
// ==============================================================================
// Stereo processing modes for delay effects with width, pan, and timing control.
//
// Feature: 022-stereo-field
// Layer: 3 (System Component)
// Dependencies: Layer 0-2 (DelayEngine, MidSideProcessor, OnePoleSmoother)
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocations in process)
// - Principle III: Modern C++ (enum class, nodiscard, noexcept)
// - Principle IX: Layered Architecture (Layer 3 depends only on Layer 0-2)
// - Principle XII: Test-First Development
//
// Reference: specs/022-stereo-field/spec.md
// ==============================================================================

#pragma once

#include <cstddef>
#include <cstdint>

namespace Iterum {
namespace DSP {

// =============================================================================
// StereoMode Enumeration (FR-001)
// =============================================================================

/// @brief Stereo processing mode selection.
enum class StereoMode : uint8_t {
    Mono,      ///< Sum L+R, identical output on both channels (FR-007)
    Stereo,    ///< Independent L/R processing with optional ratio (FR-008)
    PingPong,  ///< Alternating L/R delays with cross-feedback (FR-009)
    DualMono,  ///< Same delay time, panned output (FR-010)
    MidSide    ///< M/S encoding with independent Mid/Side delays (FR-011)
};

// =============================================================================
// StereoField Class (API Contract)
// =============================================================================

/// @brief Layer 3 stereo processing system for delay effects.
///
/// Provides:
/// - Five stereo modes: Mono, Stereo, PingPong, DualMono, MidSide
/// - Width control (0-200%) via MidSideProcessor
/// - Constant-power panning (-100 to +100)
/// - L/R timing offset (±50ms) for Haas-style widening
/// - L/R ratio (0.1-10.0) for polyrhythmic delays
/// - Smooth 50ms mode transitions
///
/// @par Real-Time Safety
/// All processing methods are noexcept and allocation-free after prepare().
///
/// @par Layer Dependencies
/// - Layer 3: Composes DelayEngine instances
/// - Layer 2: Uses MidSideProcessor for width and M/S mode
/// - Layer 1: Uses OnePoleSmoother for all parameters
/// - Layer 0: Uses db_utils for NaN handling and gain conversions
class StereoField {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    static constexpr float kMinWidth = 0.0f;           ///< Minimum width (mono)
    static constexpr float kMaxWidth = 200.0f;         ///< Maximum width
    static constexpr float kDefaultWidth = 100.0f;     ///< Unity width

    static constexpr float kMinPan = -100.0f;          ///< Full left
    static constexpr float kMaxPan = 100.0f;           ///< Full right
    static constexpr float kDefaultPan = 0.0f;         ///< Center

    static constexpr float kMinLROffset = -50.0f;      ///< Max L delay (ms)
    static constexpr float kMaxLROffset = 50.0f;       ///< Max R delay (ms)
    static constexpr float kDefaultLROffset = 0.0f;    ///< No offset

    static constexpr float kMinLRRatio = 0.1f;         ///< Minimum ratio (FR-016)
    static constexpr float kMaxLRRatio = 10.0f;        ///< Maximum ratio (FR-016)
    static constexpr float kDefaultLRRatio = 1.0f;     ///< Equal L/R times

    static constexpr float kDefaultSmoothingMs = 20.0f;   ///< Parameter smoothing
    static constexpr float kModeCrossfadeMs = 50.0f;      ///< Mode transition time

    // =========================================================================
    // Lifecycle
    // =========================================================================

    StereoField() noexcept;
    ~StereoField() noexcept;

    // Non-copyable, movable
    StereoField(const StereoField&) = delete;
    StereoField& operator=(const StereoField&) = delete;
    StereoField(StereoField&&) noexcept;
    StereoField& operator=(StereoField&&) noexcept;

    // =========================================================================
    // Lifecycle Methods (FR-004, FR-006)
    // =========================================================================

    /// @brief Prepare for processing. Allocates internal buffers.
    /// @param sampleRate Sample rate in Hz
    /// @param maxBlockSize Maximum samples per process block
    /// @param maxDelayMs Maximum delay time in milliseconds
    void prepare(double sampleRate, size_t maxBlockSize, float maxDelayMs) noexcept;

    /// @brief Clear all internal state. (FR-006)
    void reset() noexcept;

    // =========================================================================
    // Mode Selection (FR-001, FR-002, FR-003)
    // =========================================================================

    /// @brief Set stereo processing mode. (FR-002)
    /// @param mode The stereo mode to use
    /// @note Mode transitions use 50ms crossfade (FR-003)
    void setMode(StereoMode mode) noexcept;

    /// @brief Get current stereo mode.
    [[nodiscard]] StereoMode getMode() const noexcept;

    // =========================================================================
    // Width Control (FR-012)
    // =========================================================================

    /// @brief Set stereo width. (FR-012)
    /// @param widthPercent Width in percent [0%, 200%]
    ///        - 0% = mono (Side removed)
    ///        - 100% = unity (original stereo image)
    ///        - 200% = maximum width (Side doubled)
    void setWidth(float widthPercent) noexcept;

    /// @brief Get current width setting.
    [[nodiscard]] float getWidth() const noexcept;

    // =========================================================================
    // Pan Control (FR-013, FR-020)
    // =========================================================================

    /// @brief Set output pan position. (FR-013)
    /// @param pan Pan position [-100, +100]
    ///        - -100 = full left
    ///        - 0 = center
    ///        - +100 = full right
    /// @note Uses constant-power pan law (FR-020)
    void setPan(float pan) noexcept;

    /// @brief Get current pan setting.
    [[nodiscard]] float getPan() const noexcept;

    // =========================================================================
    // L/R Offset Control (FR-014)
    // =========================================================================

    /// @brief Set L/R timing offset. (FR-014)
    /// @param offsetMs Offset in milliseconds [-50, +50]
    ///        - Positive: R delayed relative to L
    ///        - Negative: L delayed relative to R
    void setLROffset(float offsetMs) noexcept;

    /// @brief Get current L/R offset setting.
    [[nodiscard]] float getLROffset() const noexcept;

    // =========================================================================
    // L/R Ratio Control (FR-015, FR-016)
    // =========================================================================

    /// @brief Set L/R delay time ratio. (FR-015)
    /// @param ratio L:R ratio [0.1, 10.0] (FR-016)
    ///        - 1.0 = equal times
    ///        - 0.75 = 3:4 (L = 75% of R)
    ///        - 0.667 = 2:3 (L = 67% of R)
    void setLRRatio(float ratio) noexcept;

    /// @brief Get current L/R ratio setting.
    [[nodiscard]] float getLRRatio() const noexcept;

    // =========================================================================
    // Delay Time Control
    // =========================================================================

    /// @brief Set base delay time in milliseconds.
    /// @param ms Delay time [0, maxDelayMs]
    void setDelayTimeMs(float ms) noexcept;

    /// @brief Get current base delay time.
    [[nodiscard]] float getDelayTimeMs() const noexcept;

    // =========================================================================
    // Processing (FR-005, FR-018, FR-019)
    // =========================================================================

    /// @brief Process stereo audio. (FR-005)
    /// @param leftIn Input left channel
    /// @param rightIn Input right channel
    /// @param leftOut Output left channel
    /// @param rightOut Output right channel
    /// @param numSamples Number of samples to process
    /// @pre prepare() has been called
    /// @note NaN inputs are treated as 0.0 (FR-019)
    /// @note No memory allocation occurs (FR-018)
    void process(const float* leftIn, const float* rightIn,
                 float* leftOut, float* rightOut,
                 size_t numSamples) noexcept;
};

} // namespace DSP
} // namespace Iterum
