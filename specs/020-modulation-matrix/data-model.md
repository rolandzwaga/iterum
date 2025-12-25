# Data Model: Modulation Matrix

**Feature**: 020-modulation-matrix
**Date**: 2025-12-25

## Entities

### ModulationMode (Enumeration)

Defines how source values are mapped before applying depth.

| Value | Meaning | Source Range | Output Range |
|-------|---------|--------------|--------------|
| Bipolar | Direct mapping | [-1, +1] | [-1, +1] × depth |
| Unipolar | Positive only | [-1, +1] → [0, 1] | [0, 1] × depth |

```cpp
enum class ModulationMode : uint8_t {
    Bipolar = 0,   // Source [-1,+1] maps directly
    Unipolar = 1   // Source [-1,+1] maps to [0,1]
};
```

### ModulationSource (Interface)

Abstract interface for any modulation source.

| Method | Return | Description |
|--------|--------|-------------|
| getCurrentValue() | float | Returns current modulation output |
| getSourceRange() | std::pair<float, float> | Returns min/max output range |

```cpp
class ModulationSource {
public:
    virtual ~ModulationSource() = default;
    [[nodiscard]] virtual float getCurrentValue() const noexcept = 0;
    [[nodiscard]] virtual std::pair<float, float> getSourceRange() const noexcept = 0;
};
```

**Known Implementations**:
- LFO: Range [-1, +1], all waveforms output bipolar
- EnvelopeFollower: Range [0, 1+], tracks amplitude

### ModulationDestination (Struct)

Registration entry for a modulatable parameter.

| Field | Type | Description |
|-------|------|-------------|
| id | uint8_t | Unique identifier (0-15) |
| minValue | float | Minimum parameter value |
| maxValue | float | Maximum parameter value |
| label | std::array<char, 32> | Human-readable name |

```cpp
struct ModulationDestination {
    uint8_t id;
    float minValue;
    float maxValue;
    std::array<char, 32> label;
};
```

**Example Destinations**:
- DelayTime: id=0, min=0.0, max=2000.0 (ms)
- FilterCutoff: id=1, min=20.0, max=20000.0 (Hz)
- FeedbackAmount: id=2, min=0.0, max=1.0
- DryWetMix: id=3, min=0.0, max=1.0

### ModulationRoute (Struct)

Connection between a source and destination.

| Field | Type | Description | Real-Time Safe |
|-------|------|-------------|----------------|
| sourceId | uint8_t | Source identifier | Read-only in process |
| destinationId | uint8_t | Destination identifier | Read-only in process |
| depth | float | Modulation depth [0, 1] | Atomic write allowed |
| mode | ModulationMode | Bipolar or Unipolar | Read-only in process |
| enabled | bool | Active state | Atomic write allowed |
| depthSmoother | OnePoleSmoother | Smooth depth transitions | Internal state |

```cpp
struct ModulationRoute {
    uint8_t sourceId = 0;
    uint8_t destinationId = 0;
    float depth = 0.0f;
    ModulationMode mode = ModulationMode::Bipolar;
    bool enabled = true;
    OnePoleSmoother depthSmoother;

    // Computed during process()
    float currentModulation = 0.0f;
};
```

### ModulationMatrix (Class)

Main class managing sources, destinations, and routes.

| Member | Type | Description |
|--------|------|-------------|
| sources_ | std::array<ModulationSource*, 16> | Registered source pointers |
| numSources_ | uint8_t | Count of registered sources |
| destinations_ | std::array<ModulationDestination, 16> | Registered destinations |
| numDestinations_ | uint8_t | Count of registered destinations |
| routes_ | std::array<ModulationRoute, 32> | All routes (active and inactive) |
| numRoutes_ | uint8_t | Count of active routes |
| modulationSums_ | std::array<float, 16> | Accumulated per-destination |
| sampleRate_ | double | Sample rate for smoothers |

## Relationships

```
┌─────────────────┐     1      ┌─────────────────────┐
│ ModulationSource│◄───────────│   ModulationRoute   │
│   (Interface)   │  sourceId  │                     │
└─────────────────┘            │  - sourceId         │
                               │  - destinationId    │
┌─────────────────┐     1      │  - depth            │
│ Modulation      │◄───────────│  - mode             │
│ Destination     │ destId     │  - enabled          │
└─────────────────┘            │  - depthSmoother    │
                               └─────────────────────┘
                                         │
                                         │ *
                                         ▼
                               ┌─────────────────────┐
                               │  ModulationMatrix   │
                               │                     │
                               │  - sources_[16]     │
                               │  - destinations_[16]│
                               │  - routes_[32]      │
                               │  - modulationSums_  │
                               └─────────────────────┘
```

## State Transitions

### Route Lifecycle

```
┌───────┐  createRoute()  ┌─────────┐
│ (none)│ ───────────────▶│ Active  │
└───────┘                 └────┬────┘
                               │
                    setEnabled(false)
                               │
                               ▼
                          ┌─────────┐
                          │Disabled │
                          └────┬────┘
                               │
                     setEnabled(true)
                               │
                               ▼
                          ┌─────────┐
                          │ Active  │
                          └─────────┘
```

### Depth Smoothing

```
┌──────────────────┐
│ Target Depth Set │  setRouteDepth(id, newDepth)
└────────┬─────────┘
         │
         ▼
┌──────────────────┐
│ Smoother Running │  depthSmoother.process() each block
└────────┬─────────┘
         │ isComplete() == true
         ▼
┌──────────────────┐
│ Depth Settled    │  currentDepth == targetDepth
└──────────────────┘
```

## Validation Rules

| Entity | Field | Rule |
|--------|-------|------|
| ModulationRoute | depth | Must be in [0.0, 1.0], clamp if out of range |
| ModulationRoute | sourceId | Must reference a registered source |
| ModulationRoute | destinationId | Must reference a registered destination |
| ModulationMatrix | numSources_ | Cannot exceed kMaxSources (16) |
| ModulationMatrix | numDestinations_ | Cannot exceed kMaxDestinations (16) |
| ModulationMatrix | numRoutes_ | Cannot exceed kMaxRoutes (32) |
| ModulationSource | getCurrentValue() | NaN treated as 0.0 |

## Constants

```cpp
inline constexpr size_t kMaxSources = 16;
inline constexpr size_t kMaxDestinations = 16;
inline constexpr size_t kMaxRoutes = 32;
inline constexpr float kDefaultSmoothingTimeMs = 20.0f;  // Depth smoothing
```
