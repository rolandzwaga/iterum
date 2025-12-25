// ==============================================================================
// Layer 3: System Component - Modulation Matrix (Contract)
// ==============================================================================
// This is the API CONTRACT for the ModulationMatrix component.
// Implementation details may vary, but this interface must be satisfied.
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocations in process)
// - Principle III: Modern C++ (C++20, RAII, value semantics)
// - Principle IX: Layer 3 (depends only on Layer 0-2)
// - Principle X: DSP Constraints (sample-accurate modulation)
// - Principle XII: Test-First Development
//
// Reference: specs/020-modulation-matrix/spec.md
// ==============================================================================

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>

// Forward declarations (actual includes in implementation)
// #include "dsp/primitives/smoother.h"

namespace Iterum {
namespace DSP {

// =============================================================================
// Constants
// =============================================================================

inline constexpr size_t kMaxModulationSources = 16;
inline constexpr size_t kMaxModulationDestinations = 16;
inline constexpr size_t kMaxModulationRoutes = 32;
inline constexpr float kModulationSmoothingTimeMs = 20.0f;

// =============================================================================
// Enumerations
// =============================================================================

/// @brief Modulation mode - how source values are mapped
enum class ModulationMode : uint8_t {
    Bipolar = 0,   ///< Source [-1,+1] maps directly to [-1,+1] × depth
    Unipolar = 1   ///< Source [-1,+1] maps to [0,1] × depth
};

// =============================================================================
// ModulationSource Interface
// =============================================================================

/// @brief Abstract interface for modulation sources
///
/// Any class that can provide modulation values should implement this interface.
/// Known implementations: LFO (Layer 1), EnvelopeFollower (Layer 2)
class ModulationSource {
public:
    virtual ~ModulationSource() = default;

    /// @brief Get the current modulation output value
    /// @return Current value (typically [-1,+1] for LFO, [0,1+] for EnvFollower)
    [[nodiscard]] virtual float getCurrentValue() const noexcept = 0;

    /// @brief Get the output range of this source
    /// @return Pair of (minValue, maxValue)
    [[nodiscard]] virtual std::pair<float, float> getSourceRange() const noexcept = 0;
};

// =============================================================================
// ModulationDestination
// =============================================================================

/// @brief Registration entry for a modulatable parameter
struct ModulationDestination {
    uint8_t id = 0;                      ///< Unique identifier (0-15)
    float minValue = 0.0f;               ///< Minimum parameter value
    float maxValue = 1.0f;               ///< Maximum parameter value
    std::array<char, 32> label = {};     ///< Human-readable name
};

// =============================================================================
// ModulationRoute
// =============================================================================

/// @brief Connection between a source and destination
struct ModulationRoute {
    uint8_t sourceId = 0;                ///< Source identifier
    uint8_t destinationId = 0;           ///< Destination identifier
    float depth = 0.0f;                  ///< Modulation depth [0, 1]
    ModulationMode mode = ModulationMode::Bipolar;  ///< Mapping mode
    bool enabled = true;                 ///< Active state

    // Internal state (managed by ModulationMatrix)
    // OnePoleSmoother depthSmoother;    // Smooth depth transitions
    // float currentModulation = 0.0f;   // Computed during process()
};

// =============================================================================
// ModulationMatrix API Contract
// =============================================================================

/// @brief Layer 3 System Component - Modulation routing and processing
///
/// Routes modulation sources (LFO, EnvelopeFollower) to parameter destinations
/// with per-route depth control and bipolar/unipolar modes.
///
/// @par Features
/// - Register up to 16 sources and 16 destinations (FR-001, FR-002)
/// - Create up to 32 routes with depth and mode (FR-003, FR-004, FR-005)
/// - Sum multiple routes to same destination (FR-006)
/// - Smooth depth changes to prevent zipper noise (FR-011)
/// - Real-time safe: noexcept, no allocations in process (FR-014)
///
/// @par Constitution Compliance
/// - Principle II: Real-Time Safety
/// - Principle III: Modern C++ (C++20)
/// - Principle IX: Layer 3 (depends only on Layer 0-2)
/// - Principle XII: Test-First Development
///
/// @par Usage
/// @code
/// ModulationMatrix matrix;
/// matrix.prepare(44100.0, 512, 32);
///
/// // Register sources and destinations
/// matrix.registerSource(0, &lfo);
/// matrix.registerDestination(0, 0.0f, 2000.0f, "Delay Time");
///
/// // Create route
/// matrix.createRoute(0, 0, 0.5f, ModulationMode::Bipolar);
///
/// // In process callback
/// matrix.process(numSamples);
/// float delayTime = matrix.getModulatedValue(0, baseDelayTime);
/// @endcode
class ModulationMatrix {
public:
    // =========================================================================
    // Lifecycle (FR-015, FR-016)
    // =========================================================================

    /// @brief Prepare matrix for processing
    /// @param sampleRate Audio sample rate in Hz
    /// @param maxBlockSize Maximum samples per process() call
    /// @param maxRoutes Maximum number of routes (default: 32)
    /// @pre Call before any processing or source/destination registration
    void prepare(double sampleRate, size_t maxBlockSize,
                 size_t maxRoutes = kMaxModulationRoutes) noexcept;

    /// @brief Reset all modulation state without deallocating
    /// @note Clears accumulated modulation values and smoother states
    void reset() noexcept;

    // =========================================================================
    // Source/Destination Registration (FR-001, FR-002, FR-013)
    // =========================================================================

    /// @brief Register a modulation source
    /// @param id Source identifier (0 to kMaxModulationSources-1)
    /// @param source Pointer to source (must outlive matrix)
    /// @return true if registration succeeded
    /// @pre Must be called during prepare() phase, not during process()
    bool registerSource(uint8_t id, ModulationSource* source) noexcept;

    /// @brief Register a modulation destination
    /// @param id Destination identifier (0 to kMaxModulationDestinations-1)
    /// @param minValue Minimum parameter value
    /// @param maxValue Maximum parameter value
    /// @param label Human-readable name (optional)
    /// @return true if registration succeeded
    /// @pre Must be called during prepare() phase, not during process()
    bool registerDestination(uint8_t id, float minValue, float maxValue,
                             const char* label = nullptr) noexcept;

    // =========================================================================
    // Route Management (FR-003, FR-004, FR-005, FR-010)
    // =========================================================================

    /// @brief Create a modulation route
    /// @param sourceId Registered source identifier
    /// @param destinationId Registered destination identifier
    /// @param depth Initial depth [0, 1]
    /// @param mode Bipolar or Unipolar mapping
    /// @return Route index, or -1 if failed
    /// @pre Source and destination must be registered
    int createRoute(uint8_t sourceId, uint8_t destinationId,
                    float depth = 1.0f,
                    ModulationMode mode = ModulationMode::Bipolar) noexcept;

    /// @brief Set route depth with smoothing (FR-004, FR-011)
    /// @param routeIndex Index returned by createRoute()
    /// @param depth New depth [0, 1]
    void setRouteDepth(int routeIndex, float depth) noexcept;

    /// @brief Set route enabled state (FR-010)
    /// @param routeIndex Index returned by createRoute()
    /// @param enabled true to enable, false to disable
    void setRouteEnabled(int routeIndex, bool enabled) noexcept;

    /// @brief Get current route depth
    /// @param routeIndex Index returned by createRoute()
    /// @return Current smoothed depth
    [[nodiscard]] float getRouteDepth(int routeIndex) const noexcept;

    /// @brief Check if route is enabled
    /// @param routeIndex Index returned by createRoute()
    /// @return true if enabled
    [[nodiscard]] bool isRouteEnabled(int routeIndex) const noexcept;

    // =========================================================================
    // Processing (FR-008, FR-014)
    // =========================================================================

    /// @brief Process all routes for a block
    /// @param numSamples Number of samples in current block
    /// @pre prepare() has been called
    /// @note Reads source values, applies depth smoothing, accumulates to destinations
    void process(size_t numSamples) noexcept;

    // =========================================================================
    // Value Retrieval (FR-009, FR-012)
    // =========================================================================

    /// @brief Get modulated parameter value
    /// @param destinationId Destination identifier
    /// @param baseValue Base parameter value (before modulation)
    /// @return Base value + modulation offset, clamped to destination range (FR-007)
    [[nodiscard]] float getModulatedValue(uint8_t destinationId,
                                           float baseValue) const noexcept;

    /// @brief Get current raw modulation offset for a destination (FR-012)
    /// @param destinationId Destination identifier
    /// @return Sum of all route contributions (for UI feedback)
    [[nodiscard]] float getCurrentModulation(uint8_t destinationId) const noexcept;

    // =========================================================================
    // Query Methods
    // =========================================================================

    /// @brief Get number of registered sources
    [[nodiscard]] size_t getSourceCount() const noexcept;

    /// @brief Get number of registered destinations
    [[nodiscard]] size_t getDestinationCount() const noexcept;

    /// @brief Get number of active routes
    [[nodiscard]] size_t getRouteCount() const noexcept;

    /// @brief Get sample rate
    [[nodiscard]] double getSampleRate() const noexcept;
};

}  // namespace DSP
}  // namespace Iterum
