// ==============================================================================
// Layer 3: System Component - FM Voice System
// ==============================================================================
// Complete 4-operator FM synthesis voice with algorithm routing. Composes 4
// FMOperator instances (Layer 2) with selectable algorithm topologies for
// DX7-style FM/PM synthesis.
//
// Constitution Compliance:
// - Principle II:  Real-Time Safety (noexcept, no allocations in process())
// - Principle III: Modern C++ (C++20, std::array, constexpr, [[nodiscard]])
// - Principle IX:  Layer 3 (depends on Layer 0-2 only)
// - Principle XII: Test-First Development
//
// Reference: specs/022-fm-voice-system/spec.md
// ==============================================================================

#pragma once

// Layer 0 dependencies
#include <krate/dsp/core/db_utils.h>

// Layer 1 dependencies
#include <krate/dsp/primitives/dc_blocker.h>

// Layer 2 dependencies
#include <krate/dsp/processors/fm_operator.h>

// Standard library
#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>

namespace Krate::DSP {

// =============================================================================
// Algorithm Enum (FR-004, FR-007)
// =============================================================================

/// @brief FM synthesis algorithm routing topologies.
///
/// Each algorithm defines a specific routing configuration for the 4 operators,
/// specifying which operators are carriers (produce audible output), which are
/// modulators (modulate other operators' phases), and the modulation routing.
enum class Algorithm : uint8_t {
    Stacked2Op = 0,           ///< Simple 2->1 stack (bass, leads)
    Stacked4Op = 1,           ///< Full 4->3->2->1 chain (rich leads, brass)
    Parallel2Plus2 = 2,       ///< Two parallel 2-op stacks (organ, pads)
    Branched = 3,             ///< Multiple mods to single carrier (bells, metallic)
    Stacked3PlusCarrier = 4,  ///< 3-op stack + independent carrier (e-piano)
    Parallel4 = 5,            ///< All 4 as carriers (additive/organ)
    YBranch = 6,              ///< Mod feeding two parallel stacks (complex)
    DeepStack = 7,            ///< 4->3->2->1 chain, mid-chain feedback (aggressive, noise)
    kNumAlgorithms = 8        ///< Sentinel for validation
};

// =============================================================================
// Operator Mode Enum (FR-013)
// =============================================================================

/// @brief Distinguishes ratio-tracking from fixed-frequency behavior.
enum class OperatorMode : uint8_t {
    Ratio = 0,  ///< frequency = baseFrequency * ratio (default, FR-016)
    Fixed = 1   ///< frequency = fixedFrequency, ignores base (FR-017)
};

// =============================================================================
// Algorithm Topology Data Structures (FR-006, FR-007)
// =============================================================================

/// @brief Single modulation connection between operators.
struct ModulationEdge {
    uint8_t source;  ///< Modulator operator index (0-3)
    uint8_t target;  ///< Target operator index (0-3)
};

/// @brief Complete routing definition for one algorithm.
///
/// Static constexpr structure defining the topology of an FM algorithm:
/// - Which operators are carriers (produce output)
/// - Which operator has self-feedback capability
/// - The modulation routing edges (source -> target)
/// - Precomputed processing order (modulators before carriers)
struct AlgorithmTopology {
    uint8_t carrierMask;           ///< Bitmask: bit i set = operator i is carrier
    uint8_t feedbackOperator;      ///< Which operator has self-feedback (0-3)
    uint8_t numEdges;              ///< Number of valid modulation edges (0-6)
    ModulationEdge edges[6];       ///< Modulation connections (max 6 for 4 ops)
    uint8_t processOrder[4];       ///< Operator processing order (modulators first)
    uint8_t carrierCount;          ///< Precomputed count of carriers (popcount of mask)
};

// =============================================================================
// Algorithm Topology Tables (FR-004, FR-006)
// =============================================================================

/// @brief Static constexpr table of all 8 algorithm topologies.
///
/// Each algorithm defines:
/// - carrierMask: Bitmask indicating which operators are carriers
/// - feedbackOperator: Which operator (0-3) receives the voice's feedback setting
/// - numEdges: Number of modulation connections
/// - edges: Array of source->target modulation pairs
/// - processOrder: Order to process operators (modulators before carriers)
/// - carrierCount: Number of carrier operators (for output normalization)
static constexpr AlgorithmTopology kAlgorithmTopologies[8] = {
    // Algorithm 0: Stacked2Op - Simple 2->1 stack
    // Topology: [1] -> [0*]  (op 1 modulates op 0, op 0 is carrier)
    // Operators 2, 3 are unused (level=0 by default)
    {
        .carrierMask = 0b0001,           // Operator 0 is carrier
        .feedbackOperator = 1,           // Op 1 has feedback
        .numEdges = 1,
        .edges = {{1, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}},
        .processOrder = {1, 0, 2, 3},    // Process modulator (1) before carrier (0)
        .carrierCount = 1
    },
    // Algorithm 1: Stacked4Op - Full 4->3->2->1 chain
    // Topology: [3] -> [2] -> [1] -> [0*]
    {
        .carrierMask = 0b0001,           // Operator 0 is carrier
        .feedbackOperator = 3,           // Op 3 (top of chain) has feedback
        .numEdges = 3,
        .edges = {{3, 2}, {2, 1}, {1, 0}, {0, 0}, {0, 0}, {0, 0}},
        .processOrder = {3, 2, 1, 0},    // Top to bottom
        .carrierCount = 1
    },
    // Algorithm 2: Parallel2Plus2 - Two parallel 2-op stacks
    // Topology: [1] -> [0*], [3] -> [2*]
    {
        .carrierMask = 0b0101,           // Operators 0 and 2 are carriers
        .feedbackOperator = 1,           // Op 1 has feedback
        .numEdges = 2,
        .edges = {{1, 0}, {3, 2}, {0, 0}, {0, 0}, {0, 0}, {0, 0}},
        .processOrder = {1, 3, 0, 2},    // Modulators first, then carriers
        .carrierCount = 2
    },
    // Algorithm 3: Branched - Y into carrier (2,1->0)
    // Topology: [1] -> [0*], [2] -> [0*]
    {
        .carrierMask = 0b0001,           // Operator 0 is carrier
        .feedbackOperator = 2,           // Op 2 has feedback
        .numEdges = 2,
        .edges = {{1, 0}, {2, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}},
        .processOrder = {1, 2, 0, 3},    // Both modulators before carrier
        .carrierCount = 1
    },
    // Algorithm 4: Stacked3PlusCarrier - 3-stack + carrier
    // Topology: [3] -> [2] -> [1*], [0*] (independent)
    {
        .carrierMask = 0b0011,           // Operators 0 and 1 are carriers
        .feedbackOperator = 3,           // Op 3 (top of stack) has feedback
        .numEdges = 2,
        .edges = {{3, 2}, {2, 1}, {0, 0}, {0, 0}, {0, 0}, {0, 0}},
        .processOrder = {3, 2, 1, 0},    // Stack top to bottom, then independent
        .carrierCount = 2
    },
    // Algorithm 5: Parallel4 - All carriers (additive)
    // Topology: [0*], [1*], [2*], [3*] (no modulation)
    {
        .carrierMask = 0b1111,           // All operators are carriers
        .feedbackOperator = 0,           // Op 0 has feedback (arbitrary choice)
        .numEdges = 0,
        .edges = {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}},
        .processOrder = {0, 1, 2, 3},    // Any order works
        .carrierCount = 4
    },
    // Algorithm 6: YBranch - Mod feeding two paths
    // Topology: [3] -> [1] -> [0*], [3] -> [2] -> [0*]
    // Op 3 feeds both op 1 and op 2, both of which modulate carrier op 0
    {
        .carrierMask = 0b0001,           // Operator 0 is carrier
        .feedbackOperator = 3,           // Op 3 has feedback
        .numEdges = 4,
        .edges = {{3, 1}, {3, 2}, {1, 0}, {2, 0}, {0, 0}, {0, 0}},
        .processOrder = {3, 1, 2, 0},    // Top first, then both mid-level, then carrier
        .carrierCount = 1
    },
    // Algorithm 7: DeepStack - Deep modulation chain with mid-chain feedback
    // Topology: [3] -> [2] -> [1] -> [0*] (same as Stacked4Op but feedback on op 2)
    // Differs from Stacked4Op: feedback on op 2 (middle) instead of op 3 (top)
    {
        .carrierMask = 0b0001,           // Operator 0 is carrier
        .feedbackOperator = 2,           // Mid-chain feedback (differs from Stacked4Op)
        .numEdges = 3,
        .edges = {{3, 2}, {2, 1}, {1, 0}, {0, 0}, {0, 0}, {0, 0}},
        .processOrder = {3, 2, 1, 0},    // Top to bottom
        .carrierCount = 1
    }
};

// =============================================================================
// Compile-Time Algorithm Topology Validation (FR-007)
// =============================================================================

namespace detail {

/// @brief Validates algorithm topology invariants at compile time.
/// @param topology The topology to validate
/// @return true if all invariants hold
constexpr bool validateTopology(const AlgorithmTopology& topology) noexcept {
    // Invariant 1: Edge count <= 6
    if (topology.numEdges > 6) {
        return false;
    }

    // Invariant 2: Carrier count >= 1 (every algorithm needs at least one carrier)
    if (topology.carrierCount < 1) {
        return false;
    }

    // Invariant 3: Feedback operator in range [0, 3]
    if (topology.feedbackOperator > 3) {
        return false;
    }

    // Invariant 4: No self-modulation in edges (feedback is handled separately)
    for (uint8_t i = 0; i < topology.numEdges; ++i) {
        if (topology.edges[i].source == topology.edges[i].target) {
            return false;
        }
        // Also check source and target are in range [0, 3]
        if (topology.edges[i].source > 3 || topology.edges[i].target > 3) {
            return false;
        }
    }

    // Invariant 5: Carrier mask matches carrier count
    uint8_t countFromMask = 0;
    for (int i = 0; i < 4; ++i) {
        if ((topology.carrierMask >> i) & 1) {
            ++countFromMask;
        }
    }
    if (countFromMask != topology.carrierCount) {
        return false;
    }

    return true;
}

/// @brief Validates all algorithm topologies at compile time.
/// @return true if all topologies are valid
constexpr bool validateAllTopologies() noexcept {
    for (size_t i = 0; i < 8; ++i) {
        if (!validateTopology(kAlgorithmTopologies[i])) {
            return false;
        }
    }
    return true;
}

} // namespace detail

// Static assertions for compile-time validation (FR-007)
static_assert(detail::validateAllTopologies(),
    "Algorithm topology validation failed: check edge count, carrier count, "
    "feedback operator range, and no self-modulation in edges");

// =============================================================================
// FMVoice Class (FR-001 through FR-028)
// =============================================================================

/// @brief Complete 4-operator FM synthesis voice with algorithm routing.
///
/// A Layer 3 system component that composes 4 FMOperator instances with
/// selectable algorithm routing, providing a complete FM synthesis voice.
///
/// @par Features
/// - 8 selectable algorithm topologies (stacked, parallel, branched)
/// - Per-operator ratio or fixed frequency modes
/// - Single feedback-enabled operator per algorithm
/// - Carrier output normalization (sum / carrier count)
/// - DC blocking on output (20.0 Hz highpass)
///
/// @par Thread Safety
/// Single-threaded model. All methods must be called from the same thread.
///
/// @par Real-Time Safety
/// process() and all setters are fully real-time safe.
/// prepare() is NOT real-time safe (initializes wavetables).
///
/// @par Memory
/// Approximately 360 KB per instance (4 operators with wavetables).
/// For polyphony, consider voice sharing at a higher level.
class FMVoice {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    static constexpr size_t kNumOperators = 4;
    static constexpr size_t kNumAlgorithms = static_cast<size_t>(Algorithm::kNumAlgorithms);

    // =========================================================================
    // Lifecycle (FR-001, FR-002, FR-003)
    // =========================================================================

    /// @brief Default constructor (FR-001).
    ///
    /// Initializes to safe silence state:
    /// - All operators at zero frequency, zero level
    /// - Algorithm 0 (Stacked2Op) selected
    /// - Unprepared state
    ///
    /// process() returns 0.0 until prepare() is called (FR-026).
    FMVoice() noexcept = default;

    /// @brief Initialize the voice for the given sample rate (FR-002).
    ///
    /// Initializes all 4 operators and the DC blocker. All internal state
    /// is reset (phases, feedback history).
    ///
    /// @param sampleRate Sample rate in Hz (must be > 0)
    ///
    /// @note NOT real-time safe (generates wavetables via FFT)
    /// @note Calling prepare() multiple times is safe; state is fully reset
    void prepare(double sampleRate) noexcept {
        sampleRate_ = sampleRate;

        // Initialize all 4 operators
        for (auto& op : operators_) {
            op.prepare(sampleRate);
        }

        // Initialize DC blocker with 20.0 Hz cutoff (FR-027, FR-028)
        dcBlocker_.prepare(sampleRate, 20.0f);

        // Reset to default state
        currentAlgorithm_ = Algorithm::Stacked2Op;
        baseFrequency_ = 440.0f;
        feedbackAmount_ = 0.0f;

        // Reset operator configs to defaults
        for (auto& config : configs_) {
            config.mode = OperatorMode::Ratio;
            config.ratio = 1.0f;
            config.fixedFrequency = 440.0f;
        }

        // Set default levels (all zero for silence)
        for (auto& op : operators_) {
            op.setLevel(0.0f);
            op.setFeedback(0.0f);
        }

        prepared_ = true;
    }

    /// @brief Reset all operator phases while preserving configuration (FR-003).
    ///
    /// After reset():
    /// - All operator phases start from 0
    /// - All feedback history cleared
    /// - Algorithm, frequency, ratios, levels preserved
    ///
    /// Use on note-on for clean attack in polyphonic context.
    ///
    /// @note Real-time safe
    void reset() noexcept {
        for (auto& op : operators_) {
            op.reset();
        }
        dcBlocker_.reset();
    }

    // =========================================================================
    // Algorithm Selection (FR-004, FR-005, FR-005a, FR-006, FR-007, FR-008)
    // =========================================================================

    /// @brief Select the active algorithm (FR-005).
    ///
    /// Changes the routing topology. Phase preservation is guaranteed (FR-005a):
    /// operators continue oscillating with only routing changed.
    ///
    /// @param algorithm The algorithm to select (0-7)
    ///
    /// @note Invalid values (>= kNumAlgorithms) are ignored (preserve previous)
    /// @note Change takes effect on next process() call
    /// @note Real-time safe
    void setAlgorithm(Algorithm algorithm) noexcept {
        // FR-005: Invalid values ignored, preserve previous
        if (static_cast<uint8_t>(algorithm) >= static_cast<uint8_t>(Algorithm::kNumAlgorithms)) {
            return;
        }
        currentAlgorithm_ = algorithm;

        // Update feedback operator based on new algorithm
        updateFeedbackOperator();
    }

    /// @brief Get the current algorithm.
    [[nodiscard]] Algorithm getAlgorithm() const noexcept {
        return currentAlgorithm_;
    }

    // =========================================================================
    // Voice Control (FR-015, FR-016, FR-017)
    // =========================================================================

    /// @brief Set the voice base frequency in Hz (FR-015).
    ///
    /// For operators in Ratio mode (FR-016): effective freq = base * ratio
    /// For operators in Fixed mode (FR-017): effective freq = fixed (ignores base)
    ///
    /// @param hz Frequency in Hz
    ///
    /// @note NaN/Inf inputs sanitized to 0 Hz (FR-025)
    /// @note Real-time safe
    void setFrequency(float hz) noexcept {
        // FR-025: Sanitize NaN/Inf
        if (detail::isNaN(hz) || detail::isInf(hz)) {
            hz = 0.0f;
        }
        baseFrequency_ = hz;
    }

    /// @brief Get the current base frequency.
    [[nodiscard]] float getFrequency() const noexcept {
        return baseFrequency_;
    }

    // =========================================================================
    // Operator Configuration (FR-009, FR-010, FR-011, FR-012, FR-013, FR-014)
    // =========================================================================

    /// @brief Set operator frequency ratio (FR-010).
    ///
    /// Only effective when operator is in Ratio mode.
    ///
    /// @param opIndex Operator index (0-3)
    /// @param ratio Frequency multiplier, clamped to [0.0, 16.0]
    ///
    /// @note Invalid opIndex silently ignored
    /// @note NaN/Inf inputs ignored (preserve previous) (FR-025)
    /// @note Real-time safe
    void setOperatorRatio(size_t opIndex, float ratio) noexcept {
        if (opIndex >= kNumOperators) {
            return;
        }
        // FR-025: NaN/Inf ignored
        if (detail::isNaN(ratio) || detail::isInf(ratio)) {
            return;
        }
        // FR-010: Clamp to [0.0, 16.0]
        configs_[opIndex].ratio = std::clamp(ratio, 0.0f, 16.0f);
    }

    /// @brief Get operator frequency ratio.
    [[nodiscard]] float getOperatorRatio(size_t opIndex) const noexcept {
        if (opIndex >= kNumOperators) {
            return 1.0f;  // Default
        }
        return configs_[opIndex].ratio;
    }

    /// @brief Set operator output level (FR-011).
    ///
    /// @param opIndex Operator index (0-3)
    /// @param level Output amplitude, clamped to [0.0, 1.0]
    ///
    /// @note Invalid opIndex silently ignored
    /// @note NaN/Inf inputs ignored (preserve previous) (FR-025)
    /// @note Real-time safe
    void setOperatorLevel(size_t opIndex, float level) noexcept {
        if (opIndex >= kNumOperators) {
            return;
        }
        // FR-025: NaN/Inf ignored
        if (detail::isNaN(level) || detail::isInf(level)) {
            return;
        }
        // FR-011: Clamp to [0.0, 1.0]
        operators_[opIndex].setLevel(std::clamp(level, 0.0f, 1.0f));
    }

    /// @brief Get operator output level.
    [[nodiscard]] float getOperatorLevel(size_t opIndex) const noexcept {
        if (opIndex >= kNumOperators) {
            return 0.0f;  // Default
        }
        return operators_[opIndex].getLevel();
    }

    /// @brief Set operator frequency mode (FR-013).
    ///
    /// @param opIndex Operator index (0-3)
    /// @param mode Ratio (frequency tracking) or Fixed (absolute frequency)
    ///
    /// @note Invalid opIndex silently ignored
    /// @note Mode change is glitch-free
    /// @note Real-time safe
    void setOperatorMode(size_t opIndex, OperatorMode mode) noexcept {
        if (opIndex >= kNumOperators) {
            return;
        }
        configs_[opIndex].mode = mode;
    }

    /// @brief Get operator frequency mode.
    [[nodiscard]] OperatorMode getOperatorMode(size_t opIndex) const noexcept {
        if (opIndex >= kNumOperators) {
            return OperatorMode::Ratio;  // Default
        }
        return configs_[opIndex].mode;
    }

    /// @brief Set operator fixed frequency (FR-014).
    ///
    /// Only effective when operator is in Fixed mode.
    ///
    /// @param opIndex Operator index (0-3)
    /// @param hz Absolute frequency in Hz
    ///
    /// @note Invalid opIndex silently ignored
    /// @note NaN/Inf inputs ignored (preserve previous) (FR-025)
    /// @note Clamped to [0.0, Nyquist]
    /// @note Real-time safe
    void setOperatorFixedFrequency(size_t opIndex, float hz) noexcept {
        if (opIndex >= kNumOperators) {
            return;
        }
        // FR-025: NaN/Inf ignored
        if (detail::isNaN(hz) || detail::isInf(hz)) {
            return;
        }
        // Clamp to [0.0, Nyquist]
        const float nyquist = static_cast<float>(sampleRate_) * 0.5f;
        configs_[opIndex].fixedFrequency = std::clamp(hz, 0.0f, nyquist);
    }

    /// @brief Get operator fixed frequency.
    [[nodiscard]] float getOperatorFixedFrequency(size_t opIndex) const noexcept {
        if (opIndex >= kNumOperators) {
            return 440.0f;  // Default
        }
        return configs_[opIndex].fixedFrequency;
    }

    /// @brief Set feedback amount for the designated operator (FR-012).
    ///
    /// The feedback-enabled operator is determined by the current algorithm.
    /// Only the designated operator uses feedback; others ignore this setting.
    ///
    /// @param amount Feedback intensity, clamped to [0.0, 1.0]
    ///
    /// @note NaN/Inf inputs ignored (preserve previous) (FR-025)
    /// @note Soft-limited via tanh to prevent instability (FR-023)
    /// @note Real-time safe
    void setFeedback(float amount) noexcept {
        // FR-025: NaN/Inf ignored
        if (detail::isNaN(amount) || detail::isInf(amount)) {
            return;
        }
        // FR-012: Clamp to [0.0, 1.0]
        feedbackAmount_ = std::clamp(amount, 0.0f, 1.0f);

        // Update the feedback operator
        updateFeedbackOperator();
    }

    /// @brief Get the current feedback amount.
    [[nodiscard]] float getFeedback() const noexcept {
        return feedbackAmount_;
    }

    // =========================================================================
    // Processing (FR-018, FR-019, FR-020, FR-021, FR-022, FR-026)
    // =========================================================================

    /// @brief Generate one mono output sample (FR-018).
    ///
    /// @return Output sample, normalized by carrier count and DC-blocked
    ///
    /// @note Returns 0.0 if prepare() has not been called (FR-026)
    /// @note Operators processed in dependency order (FR-021)
    /// @note Modulator outputs passed as phase modulation (FR-022)
    /// @note Output normalized by carrier count (FR-020)
    /// @note Real-time safe
    [[nodiscard]] float process() noexcept {
        // FR-026: Return 0.0 if not prepared
        if (!prepared_) {
            return 0.0f;
        }

        const auto& topology = kAlgorithmTopologies[static_cast<size_t>(currentAlgorithm_)];

        // Phase 1: Update operator frequencies (FR-016, FR-017)
        updateOperatorFrequencies();

        // Phase 2: Initialize modulation accumulator for each operator
        std::array<float, kNumOperators> modulation{};

        // Phase 3: Process operators in dependency order (FR-021)
        for (size_t i = 0; i < kNumOperators; ++i) {
            const uint8_t opIdx = topology.processOrder[i];

            // Process this operator with accumulated modulation (FR-022)
            (void)operators_[opIdx].process(modulation[opIdx]);

            // Distribute this operator's output to its targets
            const float rawOutput = operators_[opIdx].lastRawOutput();
            const float level = operators_[opIdx].getLevel();
            const float scaledOutput = rawOutput * level;

            for (uint8_t e = 0; e < topology.numEdges; ++e) {
                if (topology.edges[e].source == opIdx) {
                    modulation[topology.edges[e].target] += scaledOutput;
                }
            }
        }

        // Phase 4: Sum carriers with normalization (FR-020)
        float carrierSum = 0.0f;
        for (size_t i = 0; i < kNumOperators; ++i) {
            if ((topology.carrierMask >> i) & 1) {
                carrierSum += operators_[i].lastRawOutput() * operators_[i].getLevel();
            }
        }

        // Normalize by carrier count
        float output = carrierSum / static_cast<float>(topology.carrierCount);

        // Phase 5: DC blocking (FR-027, FR-028)
        output = dcBlocker_.process(output);

        // Phase 6: Sanitize output (FR-024)
        output = sanitize(output);

        return output;
    }

    /// @brief Generate a block of samples (FR-019).
    ///
    /// Equivalent to calling process() for each sample.
    ///
    /// @param output Output buffer (must have numSamples capacity)
    /// @param numSamples Number of samples to generate
    ///
    /// @note Real-time safe
    void processBlock(float* output, size_t numSamples) noexcept {
        for (size_t i = 0; i < numSamples; ++i) {
            output[i] = process();
        }
    }

private:
    // =========================================================================
    // Internal per-operator configuration
    // =========================================================================

    struct OperatorConfig {
        OperatorMode mode = OperatorMode::Ratio;
        float ratio = 1.0f;
        float fixedFrequency = 440.0f;
    };

    // =========================================================================
    // Internal helpers
    // =========================================================================

    /// @brief Branchless output sanitization (FR-024).
    /// NaN detection via bit manipulation, clamp to [-2.0, 2.0].
    [[nodiscard]] static float sanitize(float x) noexcept {
        const auto bits = detail::opaqueFloatBits(x);  // fast-math-immune read
        const bool isNan = ((bits & 0x7F800000u) == 0x7F800000u) &&
                           ((bits & 0x007FFFFFu) != 0);
        x = isNan ? 0.0f : x;
        x = (x < -2.0f) ? -2.0f : x;
        x = (x > 2.0f) ? 2.0f : x;
        return x;
    }

    /// @brief Update operator frequencies based on mode and base frequency.
    void updateOperatorFrequencies() noexcept {
        for (size_t i = 0; i < kNumOperators; ++i) {
            float freq = 0.0f;
            if (configs_[i].mode == OperatorMode::Ratio) {
                // FR-016: Ratio mode
                freq = baseFrequency_ * configs_[i].ratio;
            } else {
                // FR-017: Fixed mode
                freq = configs_[i].fixedFrequency;
            }
            operators_[i].setFrequency(freq);
        }
    }

    /// @brief Update feedback on the designated operator for current algorithm.
    void updateFeedbackOperator() noexcept {
        const auto& topology = kAlgorithmTopologies[static_cast<size_t>(currentAlgorithm_)];

        // Clear feedback on all operators
        for (auto& op : operators_) {
            op.setFeedback(0.0f);
        }

        // Set feedback on the designated operator (FR-008, FR-012)
        operators_[topology.feedbackOperator].setFeedback(feedbackAmount_);
    }

    // =========================================================================
    // Member variables
    // =========================================================================

    // Sub-components
    std::array<FMOperator, kNumOperators> operators_{};
    std::array<OperatorConfig, kNumOperators> configs_{};
    DCBlocker dcBlocker_{};

    // Parameters
    Algorithm currentAlgorithm_ = Algorithm::Stacked2Op;
    float baseFrequency_ = 440.0f;
    float feedbackAmount_ = 0.0f;

    // State
    double sampleRate_ = 0.0;
    bool prepared_ = false;
};

} // namespace Krate::DSP
