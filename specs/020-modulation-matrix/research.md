# Research: Modulation Matrix

**Feature**: 020-modulation-matrix
**Date**: 2025-12-25

## Decision Summary

| Topic | Decision | Rationale |
|-------|----------|-----------|
| Source Interface | Abstract base class with virtual getCurrentValue() | Allows any modulation source type without templates |
| Destination Storage | Pre-allocated std::array with ID lookup | Real-time safe, O(n) lookup acceptable for 16 destinations |
| Route Storage | Pre-allocated std::array<ModulationRoute, 32> | Fixed capacity, no runtime allocation |
| Depth Smoothing | OnePoleSmoother per route | Reuses existing Layer 1 component, proven implementation |
| Block Processing | Process all routes per block, cache modulation sums | Efficient for multiple routes to same destination |

## Research Topics

### 1. Source Interface Design

**Question**: How should LFO and EnvelopeFollower expose their values to ModulationMatrix?

**Options Evaluated**:

| Option | Pros | Cons |
|--------|------|------|
| Template-based source | Zero overhead | Compile-time coupling, complex |
| Virtual interface | Runtime polymorphism, clean API | Single virtual call overhead |
| Function pointer callback | No inheritance needed | Less type-safe |

**Decision**: Virtual interface with abstract `ModulationSource` base class

**Rationale**:
- Virtual call overhead is negligible (1 indirection per source per block, not per sample)
- Clean API allows easy extension for new source types
- LFO and EnvelopeFollower can inherit or use adapter pattern
- Matches patterns used elsewhere in the codebase (e.g., filter types)

### 2. Route Storage Strategy

**Question**: How to store routes while maintaining real-time safety?

**Options Evaluated**:

| Option | Pros | Cons |
|--------|------|------|
| std::vector with reserve | Flexible size | Still possible to accidentally grow |
| std::array fixed capacity | Guaranteed no allocation | Wastes memory if few routes used |
| Custom pool allocator | Best of both | Complex, overkill for 32 routes |

**Decision**: `std::array<ModulationRoute, kMaxRoutes>` with active count

**Rationale**:
- 32 routes × ~64 bytes = ~2KB - negligible memory impact
- Guaranteed no allocation in process path
- Simple iteration over active routes
- Matches FeedbackNetwork pattern

### 3. Multi-Route Summation

**Question**: How to efficiently sum multiple routes targeting the same destination?

**Options Evaluated**:

| Option | Pros | Cons |
|--------|------|------|
| Sum per-sample in route order | Simple | Redundant destination lookups |
| Accumulate to destination array | O(n) destinations, cache-friendly | Extra array needed |
| Lazy evaluation on query | No wasted work | Query becomes expensive |

**Decision**: Accumulate modulation sums to per-destination array during process()

**Rationale**:
- Pre-allocate `std::array<float, kMaxDestinations>` for modulation sums
- Clear at start of process(), accumulate from each active route
- Query methods read from accumulated values - O(1)
- Avoids recalculating when same destination queried multiple times

### 4. Bipolar/Unipolar Conversion

**Question**: When to apply bipolar-to-unipolar conversion?

**Options Evaluated**:

| Option | Where | Pros | Cons |
|--------|-------|------|------|
| In source | Source outputs [0,1] for unipolar | Simple for matrix | Breaks source reuse |
| In route processing | Matrix converts based on mode | Source stays pure | Extra operation per route |
| In destination | Destination knows its needs | Flexible | Couples destination to mode |

**Decision**: Convert in route processing

**Rationale**:
- Sources always output their natural range (LFO: [-1,+1], EnvFollower: [0,1+])
- ModulationRoute applies conversion based on mode before applying depth
- Formula for unipolar: `(bipolarValue + 1.0f) * 0.5f` → [0, 1]
- Keeps sources reusable, keeps destinations simple

### 5. Sample-Accurate vs Block-Level Modulation

**Question**: Should modulation be calculated per-sample or per-block?

**Options Evaluated**:

| Option | CPU Cost | Accuracy |
|--------|----------|----------|
| Per-sample modulation | High (N×M×S operations) | Perfect |
| Per-block modulation | Low (N×M operations) | Slightly stepped |
| Interpolated (start/end of block) | Medium | Good compromise |

**Decision**: Per-block with depth smoothing

**Rationale**:
- Source values (LFO, EnvFollower) are already per-sample internally
- Matrix reads source's current value once per block
- Depth smoothing provides sample-accurate depth changes
- Most modulation destinations (filter cutoff, delay time) are themselves smoothed
- Block-level (512 samples at 44.1kHz = 11.6ms) is fast enough for modulation

### 6. Thread Safety for Route Changes

**Question**: Can routes be modified during audio processing?

**Decision**: Route structural changes (add/remove) are NOT thread-safe

**Rationale**:
- Following pattern of DelayEngine and FeedbackNetwork
- Route configuration happens in prepare() or between process() calls
- Only depth and enabled state can change during processing (atomic writes)
- This is documented in spec assumptions

## Alternatives Considered But Rejected

### Template-Based Matrix

```cpp
template<typename... Sources>
class ModulationMatrix { ... };
```

**Rejected because**: Compile-time coupling prevents runtime route configuration. Users couldn't add routes based on UI actions.

### Per-Sample Source Polling

```cpp
for (size_t i = 0; i < numSamples; ++i) {
    float sourceValue = source->process();  // Per-sample!
    ...
}
```

**Rejected because**: Would require MatrixMatrix to drive source processing. Sources are owned externally and processed independently. Matrix only reads current values.

### Normalized Destination Ranges

**Rejected because**: Destinations should represent whatever range makes sense for the parameter (e.g., 20-20000Hz for filter). Normalization to [0,1] would lose semantic meaning and require caller to denormalize anyway.

## Implementation Notes

### Source Adapter Pattern

If LFO/EnvelopeFollower don't directly implement ModulationSource, use adapters:

```cpp
class LFOAdapter : public ModulationSource {
    LFO& lfo_;
public:
    float getCurrentValue() const noexcept override { return lfo_.process(); }
};
```

However, for efficiency, we'll poll the actual source value once per block rather than per-sample.

### EnvelopeFollower Output Range

EnvelopeFollower outputs [0, 1+] (can exceed 1.0 for hot signals). When used with unipolar mode:
- Already [0, 1+] - no conversion needed
- For bipolar mode: convert with `(value * 2.0f) - 1.0f` → [-1, +1+]

This will be handled in the route processing logic.
