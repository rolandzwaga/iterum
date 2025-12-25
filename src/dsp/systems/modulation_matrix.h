// ==============================================================================
// Layer 3: System Component - Modulation Matrix
// ==============================================================================
// Routes modulation sources (LFO, EnvelopeFollower) to parameter destinations
// with per-route depth control, bipolar/unipolar modes, and smooth transitions.
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

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>

// Layer 0 dependencies
#include "dsp/core/db_utils.h"

// Layer 1 dependencies
#include "dsp/primitives/smoother.h"

namespace Iterum {
namespace DSP {

// =============================================================================
// Constants
// =============================================================================

/// Maximum number of modulation sources (LFOs, EnvelopeFollowers, etc.)
inline constexpr size_t kMaxModulationSources = 16;

/// Maximum number of modulation destinations (parameters)
inline constexpr size_t kMaxModulationDestinations = 16;

/// Maximum number of modulation routes
inline constexpr size_t kMaxModulationRoutes = 32;

/// Fixed smoothing time for depth changes (per spec FR-011)
inline constexpr float kModulationSmoothingTimeMs = 20.0f;

// =============================================================================
// ModulationMode
// =============================================================================

/// @brief Modulation mode - how source values are mapped before applying depth
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
    uint8_t id = 0;              ///< Unique identifier (0-15)
    float minValue = 0.0f;       ///< Minimum parameter value
    float maxValue = 1.0f;       ///< Maximum parameter value
    std::array<char, 32> label = {};  ///< Human-readable name
    bool registered = false;     ///< Whether this slot is in use
};

// =============================================================================
// ModulationRoute
// =============================================================================

/// @brief Connection between a source and destination
struct ModulationRoute {
    uint8_t sourceId = 0;                          ///< Source identifier
    uint8_t destinationId = 0;                     ///< Destination identifier
    float depth = 0.0f;                            ///< Modulation depth [0, 1]
    ModulationMode mode = ModulationMode::Bipolar; ///< Mapping mode
    bool enabled = true;                           ///< Active state
    bool inUse = false;                            ///< Whether this slot is in use

    // Internal state
    OnePoleSmoother depthSmoother;                 ///< Smooth depth transitions
    float currentModulation = 0.0f;                ///< Computed during process()
};

// =============================================================================
// ModulationMatrix
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
    // Lifecycle
    // =========================================================================

    /// @brief Default constructor
    ModulationMatrix() noexcept = default;

    /// @brief Prepare matrix for processing
    /// @param sampleRate Audio sample rate in Hz
    /// @param maxBlockSize Maximum samples per process() call
    /// @param maxRoutes Maximum number of routes (default: 32)
    void prepare(double sampleRate, size_t maxBlockSize,
                 size_t maxRoutes = kMaxModulationRoutes) noexcept {
        sampleRate_ = sampleRate;
        maxBlockSize_ = maxBlockSize;
        maxRoutes_ = std::min(maxRoutes, kMaxModulationRoutes);

        // Configure all route smoothers
        for (auto& route : routes_) {
            route.depthSmoother.configure(kModulationSmoothingTimeMs,
                                          static_cast<float>(sampleRate));
        }

        reset();
    }

    /// @brief Reset all modulation state without deallocating
    void reset() noexcept {
        // Clear modulation sums
        modulationSums_.fill(0.0f);

        // Reset route smoothers
        for (auto& route : routes_) {
            route.depthSmoother.snapTo(route.depth);
            route.currentModulation = 0.0f;
        }
    }

    // =========================================================================
    // Source/Destination Registration
    // =========================================================================

    /// @brief Register a modulation source
    /// @param id Source identifier (0 to kMaxModulationSources-1)
    /// @param source Pointer to source (must outlive matrix)
    /// @return true if registration succeeded
    bool registerSource(uint8_t id, ModulationSource* source) noexcept {
        if (id >= kMaxModulationSources || source == nullptr) {
            return false;
        }

        // Check if this is a new registration
        if (sources_[id] == nullptr) {
            ++numSources_;
        }

        sources_[id] = source;
        return true;
    }

    /// @brief Register a modulation destination
    /// @param id Destination identifier (0 to kMaxModulationDestinations-1)
    /// @param minValue Minimum parameter value
    /// @param maxValue Maximum parameter value
    /// @param label Human-readable name (optional)
    /// @return true if registration succeeded
    bool registerDestination(uint8_t id, float minValue, float maxValue,
                             const char* label = nullptr) noexcept {
        if (id >= kMaxModulationDestinations) {
            return false;
        }

        // Check if this is a new registration
        if (!destinations_[id].registered) {
            ++numDestinations_;
        }

        destinations_[id].id = id;
        destinations_[id].minValue = minValue;
        destinations_[id].maxValue = maxValue;
        destinations_[id].registered = true;

        // Copy label if provided
        if (label != nullptr) {
            size_t len = std::min(std::strlen(label), size_t{31});
            std::copy_n(label, len, destinations_[id].label.begin());
            destinations_[id].label[len] = '\0';
        } else {
            destinations_[id].label[0] = '\0';
        }

        return true;
    }

    // =========================================================================
    // Route Management
    // =========================================================================

    /// @brief Create a modulation route
    /// @param sourceId Registered source identifier
    /// @param destinationId Registered destination identifier
    /// @param depth Initial depth [0, 1]
    /// @param mode Bipolar or Unipolar mapping
    /// @return Route index, or -1 if failed
    int createRoute(uint8_t sourceId, uint8_t destinationId,
                    float depth = 1.0f,
                    ModulationMode mode = ModulationMode::Bipolar) noexcept {
        // Validate source and destination
        if (sourceId >= kMaxModulationSources ||
            sources_[sourceId] == nullptr) {
            return -1;
        }

        if (destinationId >= kMaxModulationDestinations ||
            !destinations_[destinationId].registered) {
            return -1;
        }

        // Check if we have room for another route
        if (numRoutes_ >= maxRoutes_) {
            return -1;
        }

        // Find an empty slot
        for (size_t i = 0; i < maxRoutes_; ++i) {
            if (!routes_[i].inUse) {
                // Clamp depth to valid range
                depth = std::clamp(depth, 0.0f, 1.0f);

                routes_[i].sourceId = sourceId;
                routes_[i].destinationId = destinationId;
                routes_[i].depth = depth;
                routes_[i].mode = mode;
                routes_[i].enabled = true;
                routes_[i].inUse = true;
                routes_[i].currentModulation = 0.0f;

                // Initialize smoother to current depth
                routes_[i].depthSmoother.configure(
                    kModulationSmoothingTimeMs,
                    static_cast<float>(sampleRate_));
                routes_[i].depthSmoother.snapTo(depth);

                ++numRoutes_;
                return static_cast<int>(i);
            }
        }

        return -1;
    }

    /// @brief Set route depth with smoothing
    /// @param routeIndex Index returned by createRoute()
    /// @param depth New depth [0, 1]
    void setRouteDepth(int routeIndex, float depth) noexcept {
        if (routeIndex < 0 || static_cast<size_t>(routeIndex) >= maxRoutes_) {
            return;
        }

        if (!routes_[routeIndex].inUse) {
            return;
        }

        // Clamp depth to valid range
        depth = std::clamp(depth, 0.0f, 1.0f);

        routes_[routeIndex].depth = depth;
        routes_[routeIndex].depthSmoother.setTarget(depth);
    }

    /// @brief Set route enabled state
    /// @param routeIndex Index returned by createRoute()
    /// @param enabled true to enable, false to disable
    void setRouteEnabled(int routeIndex, bool enabled) noexcept {
        if (routeIndex < 0 || static_cast<size_t>(routeIndex) >= maxRoutes_) {
            return;
        }

        if (!routes_[routeIndex].inUse) {
            return;
        }

        routes_[routeIndex].enabled = enabled;
    }

    /// @brief Get current route depth
    /// @param routeIndex Index returned by createRoute()
    /// @return Current smoothed depth
    [[nodiscard]] float getRouteDepth(int routeIndex) const noexcept {
        if (routeIndex < 0 || static_cast<size_t>(routeIndex) >= maxRoutes_) {
            return 0.0f;
        }

        if (!routes_[routeIndex].inUse) {
            return 0.0f;
        }

        return routes_[routeIndex].depthSmoother.getCurrentValue();
    }

    /// @brief Check if route is enabled
    /// @param routeIndex Index returned by createRoute()
    /// @return true if enabled
    [[nodiscard]] bool isRouteEnabled(int routeIndex) const noexcept {
        if (routeIndex < 0 || static_cast<size_t>(routeIndex) >= maxRoutes_) {
            return false;
        }

        if (!routes_[routeIndex].inUse) {
            return false;
        }

        return routes_[routeIndex].enabled;
    }

    // =========================================================================
    // Processing
    // =========================================================================

    /// @brief Process all routes for a block
    /// @param numSamples Number of samples in current block
    void process(size_t numSamples) noexcept {
        // Clear modulation sums
        modulationSums_.fill(0.0f);

        // Process each active route
        for (size_t i = 0; i < maxRoutes_; ++i) {
            auto& route = routes_[i];

            if (!route.inUse || !route.enabled) {
                route.currentModulation = 0.0f;
                continue;
            }

            // Advance depth smoother
            // Process numSamples steps but just use the final value
            float smoothedDepth = 0.0f;
            for (size_t s = 0; s < numSamples; ++s) {
                smoothedDepth = route.depthSmoother.process();
            }

            // Get source value
            ModulationSource* source = sources_[route.sourceId];
            if (source == nullptr) {
                route.currentModulation = 0.0f;
                continue;
            }

            float sourceValue = source->getCurrentValue();

            // Handle NaN (FR-018)
            if (detail::isNaN(sourceValue)) {
                sourceValue = 0.0f;
            }

            // Apply mode conversion
            float mappedValue = sourceValue;
            if (route.mode == ModulationMode::Unipolar) {
                // Convert [-1, +1] to [0, 1]
                mappedValue = (sourceValue + 1.0f) * 0.5f;
            }

            // Get destination range
            const auto& dest = destinations_[route.destinationId];
            float range = dest.maxValue - dest.minValue;
            float halfRange = range * 0.5f;

            // Calculate modulation amount
            // For bipolar: sourceValue * depth * halfRange
            // For unipolar: mappedValue * depth * halfRange (but shifted)
            float modulation;
            if (route.mode == ModulationMode::Bipolar) {
                modulation = mappedValue * smoothedDepth * halfRange;
            } else {
                // Unipolar: full range modulation but only positive
                modulation = mappedValue * smoothedDepth * halfRange;
            }

            route.currentModulation = modulation;

            // Accumulate to destination
            modulationSums_[route.destinationId] += modulation;
        }
    }

    // =========================================================================
    // Value Retrieval
    // =========================================================================

    /// @brief Get modulated parameter value
    /// @param destinationId Destination identifier
    /// @param baseValue Base parameter value (before modulation)
    /// @return Base value + modulation offset, clamped to destination range
    [[nodiscard]] float getModulatedValue(uint8_t destinationId,
                                           float baseValue) const noexcept {
        if (destinationId >= kMaxModulationDestinations ||
            !destinations_[destinationId].registered) {
            return baseValue;
        }

        const auto& dest = destinations_[destinationId];
        float result = baseValue + modulationSums_[destinationId];

        // Clamp to destination range (FR-007)
        return std::clamp(result, dest.minValue, dest.maxValue);
    }

    /// @brief Get current raw modulation offset for a destination
    /// @param destinationId Destination identifier
    /// @return Sum of all route contributions
    [[nodiscard]] float getCurrentModulation(uint8_t destinationId) const noexcept {
        if (destinationId >= kMaxModulationDestinations) {
            return 0.0f;
        }

        return modulationSums_[destinationId];
    }

    // =========================================================================
    // Query Methods
    // =========================================================================

    /// @brief Get number of registered sources
    [[nodiscard]] size_t getSourceCount() const noexcept {
        return numSources_;
    }

    /// @brief Get number of registered destinations
    [[nodiscard]] size_t getDestinationCount() const noexcept {
        return numDestinations_;
    }

    /// @brief Get number of active routes
    [[nodiscard]] size_t getRouteCount() const noexcept {
        return numRoutes_;
    }

    /// @brief Get sample rate
    [[nodiscard]] double getSampleRate() const noexcept {
        return sampleRate_;
    }

private:
    // Sample rate for smoothing calculations
    double sampleRate_ = 44100.0;
    size_t maxBlockSize_ = 512;
    size_t maxRoutes_ = kMaxModulationRoutes;

    // Source pointers (not owned)
    std::array<ModulationSource*, kMaxModulationSources> sources_ = {};
    size_t numSources_ = 0;

    // Destination registrations
    std::array<ModulationDestination, kMaxModulationDestinations> destinations_ = {};
    size_t numDestinations_ = 0;

    // Routes
    mutable std::array<ModulationRoute, kMaxModulationRoutes> routes_ = {};
    size_t numRoutes_ = 0;

    // Per-destination modulation sums (computed during process)
    std::array<float, kMaxModulationDestinations> modulationSums_ = {};
};

}  // namespace DSP
}  // namespace Iterum
