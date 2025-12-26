// ==============================================================================
// Layer 4: User Feature - DuckingDelay
// ==============================================================================
// Delay effect with automatic gain reduction when input signal is present.
// Classic sidechain ducking for voiceover, podcast, and live performance.
//
// Composes:
// - FlexibleFeedbackNetwork (Layer 3): Delay engine with feedback and filter
// - DuckingProcessor (Layer 2): 2 instances for output/feedback ducking
// - OnePoleSmoother (Layer 1): Parameter smoothing
//
// Feature: 032-ducking-delay
// Layer: 4 (User Feature)
// Reference: specs/032-ducking-delay/spec.md
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocations in process)
// - Principle III: Modern C++ (C++20, RAII)
// - Principle IX: Layer 4 (composes only from Layer 0-3)
// - Principle X: DSP Constraints (parameter smoothing, click-free)
// - Principle XII: Test-First Development
// ==============================================================================

#pragma once

#include "dsp/core/block_context.h"
#include "dsp/core/db_utils.h"
#include "dsp/core/note_value.h"
#include "dsp/primitives/smoother.h"
#include "dsp/processors/ducking_processor.h"
#include "dsp/systems/delay_engine.h"            // For TimeMode enum
#include "dsp/systems/flexible_feedback_network.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace Iterum {
namespace DSP {

// =============================================================================
// DuckTarget Enumeration (FR-010)
// =============================================================================

/// @brief Specifies which signal path to apply ducking to
enum class DuckTarget : uint8_t {
    Output = 0,    ///< Duck the delay output before dry/wet mix (FR-011)
    Feedback = 1,  ///< Duck the feedback path only (FR-012)
    Both = 2       ///< Duck both output and feedback (FR-013)
};

// =============================================================================
// DuckingDelay Class
// =============================================================================

/// @brief Layer 4 User Feature - Ducking Delay
///
/// Delay effect that automatically reduces output when input signal is present.
/// The "ducking" effect keeps the delay from stepping on primary audio content,
/// ideal for voiceover, podcasts, and live performance.
///
/// @par Signal Flow (Output Only Mode - FR-011)
/// ```
/// Input ──┬────────────────────────────────────────────┬──> Mix ──> Output
///         │                                            │
///         v (sidechain)                                │
///    ┌─────────┐                                       │
///    │  Delay  │──> [DuckingProcessor] ────────────────┘
///    │   FFN   │          (output)
///    └─────────┘
/// ```
///
/// @par User Controls
/// - Ducking Enable: On/Off (FR-001)
/// - Threshold: -60 to 0 dB (FR-002)
/// - Duck Amount: 0-100% (maps to 0 to -48 dB depth) (FR-003, FR-004, FR-005)
/// - Attack Time: 0.1-100 ms (FR-006)
/// - Release Time: 10-2000 ms (FR-007)
/// - Hold Time: 0-500 ms (FR-008, FR-009)
/// - Duck Target: Output, Feedback, Both (FR-010 to FR-013)
/// - Sidechain Filter: On/Off, 20-500 Hz (FR-014 to FR-016)
/// - Dry/Wet Mix: 0-100% (FR-020)
/// - Output Gain: -inf to +6 dB (FR-021)
///
/// @par Constitution Compliance
/// - Principle II: Real-Time Safety (noexcept, no allocations in process)
/// - Principle IX: Layer 4 composes from Layer 0-3 only
/// - Principle XII: Test-First Development
///
/// @par Usage
/// @code
/// DuckingDelay delay;
/// delay.prepare(44100.0, 512);
/// delay.setDuckingEnabled(true);
/// delay.setThreshold(-30.0f);
/// delay.setDuckAmount(50.0f);
/// delay.setDuckTarget(DuckTarget::Output);
/// delay.snapParameters();
///
/// // In process callback
/// delay.process(left, right, numSamples, ctx);
/// @endcode
class DuckingDelay {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    // Delay time limits
    static constexpr float kMinDelayMs = 10.0f;
    static constexpr float kMaxDelayMs = 5000.0f;
    static constexpr float kDefaultDelayMs = 500.0f;

    // Threshold (FR-002)
    static constexpr float kMinThreshold = -60.0f;    // dB
    static constexpr float kMaxThreshold = 0.0f;      // dB
    static constexpr float kDefaultThreshold = -30.0f;

    // Duck amount (FR-003, FR-004, FR-005)
    static constexpr float kMinDuckAmount = 0.0f;     // % (no ducking)
    static constexpr float kMaxDuckAmount = 100.0f;   // % (full attenuation -48dB)
    static constexpr float kDefaultDuckAmount = 50.0f;

    // Attack time (FR-006)
    static constexpr float kMinAttackMs = 0.1f;
    static constexpr float kMaxAttackMs = 100.0f;
    static constexpr float kDefaultAttackMs = 10.0f;

    // Release time (FR-007)
    static constexpr float kMinReleaseMs = 10.0f;
    static constexpr float kMaxReleaseMs = 2000.0f;
    static constexpr float kDefaultReleaseMs = 200.0f;

    // Hold time (FR-008)
    static constexpr float kMinHoldMs = 0.0f;
    static constexpr float kMaxHoldMs = 500.0f;
    static constexpr float kDefaultHoldMs = 50.0f;

    // Sidechain filter (FR-014, FR-015)
    static constexpr float kMinSidechainHz = 20.0f;
    static constexpr float kMaxSidechainHz = 500.0f;
    static constexpr float kDefaultSidechainHz = 80.0f;

    // Output (FR-020, FR-021)
    static constexpr float kMinDryWetMix = 0.0f;
    static constexpr float kMaxDryWetMix = 100.0f;
    static constexpr float kDefaultDryWetMix = 50.0f;

    static constexpr float kMinOutputGainDb = -96.0f;  // -inf floor per FR-021
    static constexpr float kMaxOutputGainDb = 6.0f;
    static constexpr float kDefaultOutputGainDb = 0.0f;

    // Filter
    static constexpr float kMinFilterCutoff = 20.0f;
    static constexpr float kMaxFilterCutoff = 20000.0f;
    static constexpr float kDefaultFilterCutoff = 4000.0f;

    // Internal
    static constexpr float kSmoothingTimeMs = 20.0f;

    // =========================================================================
    // Construction / Destruction
    // =========================================================================

    DuckingDelay() noexcept = default;
    ~DuckingDelay() = default;

    // Non-copyable, movable
    DuckingDelay(const DuckingDelay&) = delete;
    DuckingDelay& operator=(const DuckingDelay&) = delete;
    DuckingDelay(DuckingDelay&&) noexcept = default;
    DuckingDelay& operator=(DuckingDelay&&) noexcept = default;

    // =========================================================================
    // Lifecycle Methods (FR-023)
    // =========================================================================

    /// @brief Prepare for processing (allocates memory)
    /// @param sampleRate Audio sample rate in Hz
    /// @param maxBlockSize Maximum samples per process() call
    /// @post Ready for process() calls
    void prepare(double sampleRate, std::size_t maxBlockSize) noexcept;

    /// @brief Reset all internal state
    /// @post Delay lines cleared, smoothers snapped to current values
    void reset() noexcept;

    /// @brief Snap all smoothers to current targets (for initialization)
    /// @note Call after setting multiple parameters for tests or preset loads
    void snapParameters() noexcept;

    /// @brief Check if prepared for processing
    [[nodiscard]] bool isPrepared() const noexcept { return prepared_; }

    // =========================================================================
    // Ducking Control (FR-001 to FR-009)
    // =========================================================================

    /// @brief Enable or disable ducking (FR-001)
    /// @param enabled true to enable ducking
    void setDuckingEnabled(bool enabled) noexcept;

    /// @brief Get ducking enabled state
    [[nodiscard]] bool isDuckingEnabled() const noexcept { return duckingEnabled_; }

    /// @brief Set threshold level (FR-002)
    /// @param dB Threshold in dB [-60, 0]
    void setThreshold(float dB) noexcept;

    /// @brief Get threshold
    [[nodiscard]] float getThreshold() const noexcept { return thresholdDb_; }

    /// @brief Set duck amount as percentage (FR-003)
    /// @param percent Duck amount [0, 100] (0 = no ducking, 100 = -48dB)
    void setDuckAmount(float percent) noexcept;

    /// @brief Get duck amount
    [[nodiscard]] float getDuckAmount() const noexcept { return duckAmountPercent_; }

    /// @brief Set attack time (FR-006)
    /// @param ms Attack time [0.1, 100] milliseconds
    void setAttackTime(float ms) noexcept;

    /// @brief Get attack time
    [[nodiscard]] float getAttackTime() const noexcept { return attackTimeMs_; }

    /// @brief Set release time (FR-007)
    /// @param ms Release time [10, 2000] milliseconds
    void setReleaseTime(float ms) noexcept;

    /// @brief Get release time
    [[nodiscard]] float getReleaseTime() const noexcept { return releaseTimeMs_; }

    /// @brief Set hold time (FR-008)
    /// @param ms Hold time [0, 500] milliseconds
    void setHoldTime(float ms) noexcept;

    /// @brief Get hold time
    [[nodiscard]] float getHoldTime() const noexcept { return holdTimeMs_; }

    // =========================================================================
    // Target Selection (FR-010 to FR-013)
    // =========================================================================

    /// @brief Set ducking target (FR-010)
    /// @param target Where to apply ducking (Output, Feedback, or Both)
    void setDuckTarget(DuckTarget target) noexcept;

    /// @brief Get ducking target
    [[nodiscard]] DuckTarget getDuckTarget() const noexcept { return duckTarget_; }

    // =========================================================================
    // Sidechain Filter (FR-014 to FR-016)
    // =========================================================================

    /// @brief Enable or disable sidechain highpass filter (FR-016)
    /// @param enabled true to enable HP filter on sidechain
    void setSidechainFilterEnabled(bool enabled) noexcept;

    /// @brief Get sidechain filter enabled state
    [[nodiscard]] bool isSidechainFilterEnabled() const noexcept {
        return sidechainFilterEnabled_;
    }

    /// @brief Set sidechain filter cutoff (FR-015)
    /// @param hz Cutoff frequency [20, 500] Hz
    void setSidechainFilterCutoff(float hz) noexcept;

    /// @brief Get sidechain filter cutoff
    [[nodiscard]] float getSidechainFilterCutoff() const noexcept {
        return sidechainFilterCutoffHz_;
    }

    // =========================================================================
    // Delay Configuration (FR-017, FR-018, FR-019)
    // =========================================================================

    /// @brief Set delay time in milliseconds
    /// @param ms Delay time [10, 5000] milliseconds
    void setDelayTimeMs(float ms) noexcept;

    /// @brief Get delay time
    [[nodiscard]] float getDelayTimeMs() const noexcept { return delayTimeMs_; }

    /// @brief Set time mode (free or synced)
    /// @param mode TimeMode::Free or TimeMode::Synced
    void setTimeMode(TimeMode mode) noexcept { timeMode_ = mode; }

    /// @brief Get current time mode
    [[nodiscard]] TimeMode getTimeMode() const noexcept { return timeMode_; }

    /// @brief Set note value for tempo sync
    /// @param note Note value (quarter, eighth, etc.)
    /// @param modifier Note modifier (none, dotted, triplet)
    void setNoteValue(NoteValue note,
                      NoteModifier modifier = NoteModifier::None) noexcept;

    /// @brief Set feedback amount
    /// @param percent Feedback [0, 120] percent
    void setFeedbackAmount(float percent) noexcept;

    /// @brief Get feedback amount
    [[nodiscard]] float getFeedbackAmount() const noexcept {
        return feedbackAmount_ * 100.0f;
    }

    // =========================================================================
    // Filter (in feedback path)
    // =========================================================================

    /// @brief Enable or disable feedback filter
    /// @param enabled true to enable filter
    void setFilterEnabled(bool enabled) noexcept;

    /// @brief Get filter enabled state
    [[nodiscard]] bool isFilterEnabled() const noexcept { return filterEnabled_; }

    /// @brief Set filter type
    /// @param type Filter type (lowpass, highpass, bandpass)
    void setFilterType(FilterType type) noexcept;

    /// @brief Set filter cutoff
    /// @param hz Cutoff frequency [20, 20000] Hz
    void setFilterCutoff(float hz) noexcept;

    /// @brief Get filter cutoff
    [[nodiscard]] float getFilterCutoff() const noexcept { return filterCutoffHz_; }

    // =========================================================================
    // Output (FR-020, FR-021)
    // =========================================================================

    /// @brief Set dry/wet mix (FR-020)
    /// @param percent Mix [0, 100] (0 = dry, 100 = wet)
    void setDryWetMix(float percent) noexcept;

    /// @brief Get dry/wet mix
    [[nodiscard]] float getDryWetMix() const noexcept { return dryWetMix_; }

    /// @brief Set output gain (FR-021)
    /// @param dB Output gain [-96, +6] dB
    void setOutputGainDb(float dB) noexcept;

    /// @brief Get output gain
    [[nodiscard]] float getOutputGainDb() const noexcept { return outputGainDb_; }

    // =========================================================================
    // Metering (FR-022)
    // =========================================================================

    /// @brief Get current gain reduction in dB
    /// @return Gain reduction (negative when ducking, 0 when idle)
    [[nodiscard]] float getGainReduction() const noexcept;

    // =========================================================================
    // Query (FR-024)
    // =========================================================================

    /// @brief Get processing latency in samples
    [[nodiscard]] std::size_t getLatencySamples() const noexcept;

    // =========================================================================
    // Processing
    // =========================================================================

    /// @brief Process stereo audio in-place
    /// @param left Left channel buffer (modified in-place)
    /// @param right Right channel buffer (modified in-place)
    /// @param numSamples Number of samples per channel
    /// @param ctx Block context with tempo/transport info
    /// @pre prepare() has been called
    /// @note noexcept, allocation-free
    void process(float* left, float* right, std::size_t numSamples,
                 const BlockContext& ctx) noexcept;

private:
    // =========================================================================
    // Internal Helpers
    // =========================================================================

    /// @brief Convert percentage (0-100) to depth in dB (0 to -48)
    /// @param percent Duck amount percentage
    /// @return Depth in dB (0 to -48)
    [[nodiscard]] static float percentToDepth(float percent) noexcept {
        return -48.0f * (percent / 100.0f);
    }

    /// @brief Update both DuckingProcessor instances with current parameters
    void updateDuckingProcessors() noexcept;

    /// @brief Calculate tempo-synced delay time
    [[nodiscard]] float calculateTempoSyncedDelay(const BlockContext& ctx) const noexcept;

    // =========================================================================
    // Member Variables
    // =========================================================================

    // Configuration
    double sampleRate_ = 44100.0;
    std::size_t maxBlockSize_ = 512;
    bool prepared_ = false;

    // Core components (Layer 3)
    FlexibleFeedbackNetwork feedbackNetwork_;

    // Ducking processors (Layer 2)
    DuckingProcessor outputDucker_;   // For Output/Both modes
    DuckingProcessor feedbackDucker_; // For Feedback/Both modes

    // Parameter smoothers (Layer 1)
    OnePoleSmoother dryWetSmoother_;
    OnePoleSmoother outputGainSmoother_;
    OnePoleSmoother delaySmoother_;

    // Ducking parameters
    bool duckingEnabled_ = true;
    DuckTarget duckTarget_ = DuckTarget::Output;
    float thresholdDb_ = kDefaultThreshold;
    float duckAmountPercent_ = kDefaultDuckAmount;
    float attackTimeMs_ = kDefaultAttackMs;
    float releaseTimeMs_ = kDefaultReleaseMs;
    float holdTimeMs_ = kDefaultHoldMs;
    bool sidechainFilterEnabled_ = false;
    float sidechainFilterCutoffHz_ = kDefaultSidechainHz;

    // Delay parameters
    float delayTimeMs_ = kDefaultDelayMs;
    TimeMode timeMode_ = TimeMode::Free;
    NoteValue noteValue_ = NoteValue::Quarter;
    NoteModifier noteModifier_ = NoteModifier::None;
    float feedbackAmount_ = 0.5f;  // 0-1.2

    // Filter parameters
    bool filterEnabled_ = false;
    FilterType filterType_ = FilterType::Lowpass;
    float filterCutoffHz_ = kDefaultFilterCutoff;

    // Output parameters
    float dryWetMix_ = kDefaultDryWetMix;
    float outputGainDb_ = kDefaultOutputGainDb;

    // Scratch buffers (pre-allocated in prepare)
    std::vector<float> dryBufferL_;
    std::vector<float> dryBufferR_;
    std::vector<float> inputCopyL_;   // For sidechain (input copy)
    std::vector<float> inputCopyR_;
    std::vector<float> unduckedL_;    // For Feedback-Only mode
    std::vector<float> unduckedR_;
};

// =============================================================================
// Inline Implementations - Lifecycle
// =============================================================================

inline void DuckingDelay::prepare(double sampleRate, std::size_t maxBlockSize) noexcept {
    sampleRate_ = sampleRate;
    maxBlockSize_ = maxBlockSize;

    // Prepare flexible feedback network (Layer 3)
    feedbackNetwork_.prepare(sampleRate, maxBlockSize);
    feedbackNetwork_.setDelayTimeMs(delayTimeMs_);
    feedbackNetwork_.setFeedbackAmount(feedbackAmount_);
    feedbackNetwork_.setFilterEnabled(filterEnabled_);
    feedbackNetwork_.setFilterCutoff(filterCutoffHz_);
    feedbackNetwork_.setFilterType(filterType_);

    // Prepare ducking processors (Layer 2)
    outputDucker_.prepare(sampleRate, maxBlockSize);
    feedbackDucker_.prepare(sampleRate, maxBlockSize);
    updateDuckingProcessors();

    // Allocate scratch buffers
    dryBufferL_.resize(maxBlockSize, 0.0f);
    dryBufferR_.resize(maxBlockSize, 0.0f);
    inputCopyL_.resize(maxBlockSize, 0.0f);
    inputCopyR_.resize(maxBlockSize, 0.0f);
    unduckedL_.resize(maxBlockSize, 0.0f);
    unduckedR_.resize(maxBlockSize, 0.0f);

    // Configure smoothers (Layer 1)
    const float sr = static_cast<float>(sampleRate);
    dryWetSmoother_.configure(kSmoothingTimeMs, sr);
    outputGainSmoother_.configure(kSmoothingTimeMs, sr);
    delaySmoother_.configure(kSmoothingTimeMs, sr);

    // Initialize smoothers to current values
    dryWetSmoother_.snapTo(dryWetMix_ / 100.0f);
    outputGainSmoother_.snapTo(dbToGain(outputGainDb_));
    delaySmoother_.snapTo(delayTimeMs_);

    // Snap feedback network parameters
    feedbackNetwork_.snapParameters();

    prepared_ = true;
}

inline void DuckingDelay::reset() noexcept {
    // Reset feedback network
    feedbackNetwork_.reset();

    // Reset ducking processors
    outputDucker_.reset();
    feedbackDucker_.reset();

    // Snap smoothers to current targets
    dryWetSmoother_.snapTo(dryWetMix_ / 100.0f);
    outputGainSmoother_.snapTo(dbToGain(outputGainDb_));
    delaySmoother_.snapTo(delayTimeMs_);

    // Snap feedback network parameters
    feedbackNetwork_.snapParameters();
}

inline void DuckingDelay::snapParameters() noexcept {
    // Snap local smoothers
    dryWetSmoother_.snapTo(dryWetMix_ / 100.0f);
    outputGainSmoother_.snapTo(dbToGain(outputGainDb_));
    delaySmoother_.snapTo(delayTimeMs_);

    // Update ducking processors
    updateDuckingProcessors();

    // Update feedback network parameters
    feedbackNetwork_.setDelayTimeMs(delayTimeMs_);
    feedbackNetwork_.setFeedbackAmount(feedbackAmount_);
    feedbackNetwork_.setFilterEnabled(filterEnabled_);
    feedbackNetwork_.setFilterCutoff(filterCutoffHz_);
    feedbackNetwork_.setFilterType(filterType_);
    feedbackNetwork_.snapParameters();
}

// =============================================================================
// Inline Implementations - Ducking Control
// =============================================================================

inline void DuckingDelay::setDuckingEnabled(bool enabled) noexcept {
    duckingEnabled_ = enabled;
}

inline void DuckingDelay::setThreshold(float dB) noexcept {
    thresholdDb_ = std::clamp(dB, kMinThreshold, kMaxThreshold);
    updateDuckingProcessors();
}

inline void DuckingDelay::setDuckAmount(float percent) noexcept {
    duckAmountPercent_ = std::clamp(percent, kMinDuckAmount, kMaxDuckAmount);
    updateDuckingProcessors();
}

inline void DuckingDelay::setAttackTime(float ms) noexcept {
    attackTimeMs_ = std::clamp(ms, kMinAttackMs, kMaxAttackMs);
    updateDuckingProcessors();
}

inline void DuckingDelay::setReleaseTime(float ms) noexcept {
    releaseTimeMs_ = std::clamp(ms, kMinReleaseMs, kMaxReleaseMs);
    updateDuckingProcessors();
}

inline void DuckingDelay::setHoldTime(float ms) noexcept {
    holdTimeMs_ = std::clamp(ms, kMinHoldMs, kMaxHoldMs);
    updateDuckingProcessors();
}

inline void DuckingDelay::setDuckTarget(DuckTarget target) noexcept {
    duckTarget_ = target;
}

inline void DuckingDelay::setSidechainFilterEnabled(bool enabled) noexcept {
    sidechainFilterEnabled_ = enabled;
    outputDucker_.setSidechainFilterEnabled(enabled);
    feedbackDucker_.setSidechainFilterEnabled(enabled);
}

inline void DuckingDelay::setSidechainFilterCutoff(float hz) noexcept {
    sidechainFilterCutoffHz_ = std::clamp(hz, kMinSidechainHz, kMaxSidechainHz);
    outputDucker_.setSidechainFilterCutoff(sidechainFilterCutoffHz_);
    feedbackDucker_.setSidechainFilterCutoff(sidechainFilterCutoffHz_);
}

// =============================================================================
// Inline Implementations - Delay Configuration
// =============================================================================

inline void DuckingDelay::setDelayTimeMs(float ms) noexcept {
    delayTimeMs_ = std::clamp(ms, kMinDelayMs, kMaxDelayMs);
    delaySmoother_.setTarget(delayTimeMs_);
    feedbackNetwork_.setDelayTimeMs(delayTimeMs_);
}

inline void DuckingDelay::setNoteValue(NoteValue note, NoteModifier modifier) noexcept {
    noteValue_ = note;
    noteModifier_ = modifier;
}

inline void DuckingDelay::setFeedbackAmount(float percent) noexcept {
    feedbackAmount_ = std::clamp(percent / 100.0f, 0.0f, 1.2f);
    feedbackNetwork_.setFeedbackAmount(feedbackAmount_);
}

inline void DuckingDelay::setFilterEnabled(bool enabled) noexcept {
    filterEnabled_ = enabled;
    feedbackNetwork_.setFilterEnabled(enabled);
}

inline void DuckingDelay::setFilterType(FilterType type) noexcept {
    filterType_ = type;
    feedbackNetwork_.setFilterType(type);
}

inline void DuckingDelay::setFilterCutoff(float hz) noexcept {
    filterCutoffHz_ = std::clamp(hz, kMinFilterCutoff, kMaxFilterCutoff);
    feedbackNetwork_.setFilterCutoff(filterCutoffHz_);
}

// =============================================================================
// Inline Implementations - Output
// =============================================================================

inline void DuckingDelay::setDryWetMix(float percent) noexcept {
    dryWetMix_ = std::clamp(percent, kMinDryWetMix, kMaxDryWetMix);
    dryWetSmoother_.setTarget(dryWetMix_ / 100.0f);
}

inline void DuckingDelay::setOutputGainDb(float dB) noexcept {
    outputGainDb_ = std::clamp(dB, kMinOutputGainDb, kMaxOutputGainDb);
    outputGainSmoother_.setTarget(dbToGain(outputGainDb_));
}

inline float DuckingDelay::getGainReduction() const noexcept {
    // Return gain reduction from the output ducker (primary metering)
    return outputDucker_.getCurrentGainReduction();
}

inline std::size_t DuckingDelay::getLatencySamples() const noexcept {
    // Latency comes from feedback network
    return feedbackNetwork_.getLatencySamples();
}

// =============================================================================
// Inline Implementations - Internal Helpers
// =============================================================================

inline void DuckingDelay::updateDuckingProcessors() noexcept {
    // Convert duck amount percentage to depth in dB
    const float depthDb = percentToDepth(duckAmountPercent_);

    // Update output ducker
    outputDucker_.setThreshold(thresholdDb_);
    outputDucker_.setDepth(depthDb);
    outputDucker_.setAttackTime(attackTimeMs_);
    outputDucker_.setReleaseTime(releaseTimeMs_);
    outputDucker_.setHoldTime(holdTimeMs_);

    // Update feedback ducker
    feedbackDucker_.setThreshold(thresholdDb_);
    feedbackDucker_.setDepth(depthDb);
    feedbackDucker_.setAttackTime(attackTimeMs_);
    feedbackDucker_.setReleaseTime(releaseTimeMs_);
    feedbackDucker_.setHoldTime(holdTimeMs_);
}

inline float DuckingDelay::calculateTempoSyncedDelay(const BlockContext& ctx) const noexcept {
    const std::size_t delaySamples = ctx.tempoToSamples(noteValue_, noteModifier_);
    float delayMs = static_cast<float>(delaySamples * 1000.0 / ctx.sampleRate);
    return std::clamp(delayMs, kMinDelayMs, kMaxDelayMs);
}

inline void DuckingDelay::process(float* left, float* right, std::size_t numSamples,
                                   const BlockContext& ctx) noexcept {
    if (!prepared_ || numSamples == 0) return;

    // Calculate base delay time (tempo sync or free)
    float baseDelayMs = delayTimeMs_;
    if (timeMode_ == TimeMode::Synced) {
        baseDelayMs = calculateTempoSyncedDelay(ctx);
        feedbackNetwork_.setDelayTimeMs(baseDelayMs);
    }
    delaySmoother_.setTarget(baseDelayMs);

    // Process in chunks of maxBlockSize_ to handle large buffers
    std::size_t samplesProcessed = 0;
    while (samplesProcessed < numSamples) {
        const std::size_t chunkSize = std::min(maxBlockSize_, numSamples - samplesProcessed);
        float* chunkLeft = left + samplesProcessed;
        float* chunkRight = right + samplesProcessed;

        // Store dry signal for mixing
        for (std::size_t i = 0; i < chunkSize; ++i) {
            dryBufferL_[i] = chunkLeft[i];
            dryBufferR_[i] = chunkRight[i];
        }

        // Store input copy for sidechain
        for (std::size_t i = 0; i < chunkSize; ++i) {
            inputCopyL_[i] = chunkLeft[i];
            inputCopyR_[i] = chunkRight[i];
        }

        // Process through feedback network (delay + feedback + filter)
        feedbackNetwork_.process(chunkLeft, chunkRight, chunkSize, ctx);

        // Apply ducking based on target mode
        if (duckingEnabled_) {
            switch (duckTarget_) {
                case DuckTarget::Output: {
                    // Duck the delay output before dry/wet mix
                    for (std::size_t i = 0; i < chunkSize; ++i) {
                        const float sidechain = (inputCopyL_[i] + inputCopyR_[i]) * 0.5f;
                        chunkLeft[i] = outputDucker_.processSample(chunkLeft[i], sidechain);
                        chunkRight[i] = feedbackDucker_.processSample(chunkRight[i], sidechain);
                    }
                    break;
                }
                case DuckTarget::Feedback: {
                    // Store unducked output for user
                    for (std::size_t i = 0; i < chunkSize; ++i) {
                        unduckedL_[i] = chunkLeft[i];
                        unduckedR_[i] = chunkRight[i];
                    }
                    // Duck the output (which becomes feedback next block)
                    for (std::size_t i = 0; i < chunkSize; ++i) {
                        const float sidechain = (inputCopyL_[i] + inputCopyR_[i]) * 0.5f;
                        chunkLeft[i] = outputDucker_.processSample(chunkLeft[i], sidechain);
                        chunkRight[i] = feedbackDucker_.processSample(chunkRight[i], sidechain);
                    }
                    // Restore unducked output for user (ducked version feeds back)
                    for (std::size_t i = 0; i < chunkSize; ++i) {
                        chunkLeft[i] = unduckedL_[i];
                        chunkRight[i] = unduckedR_[i];
                    }
                    break;
                }
                case DuckTarget::Both: {
                    // Duck both output and feedback paths
                    for (std::size_t i = 0; i < chunkSize; ++i) {
                        const float sidechain = (inputCopyL_[i] + inputCopyR_[i]) * 0.5f;
                        chunkLeft[i] = outputDucker_.processSample(chunkLeft[i], sidechain);
                        chunkRight[i] = feedbackDucker_.processSample(chunkRight[i], sidechain);
                    }
                    break;
                }
            }
        }

        // Apply dry/wet mix and output gain with smoothed parameters
        for (std::size_t i = 0; i < chunkSize; ++i) {
            const float currentDryWet = dryWetSmoother_.process();
            const float currentOutputGain = outputGainSmoother_.process();

            chunkLeft[i] = (dryBufferL_[i] * (1.0f - currentDryWet) +
                           chunkLeft[i] * currentDryWet) * currentOutputGain;
            chunkRight[i] = (dryBufferR_[i] * (1.0f - currentDryWet) +
                            chunkRight[i] * currentDryWet) * currentOutputGain;
        }

        samplesProcessed += chunkSize;
    }
}

} // namespace DSP
} // namespace Iterum
