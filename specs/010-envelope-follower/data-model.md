# Data Model: Envelope Follower

**Feature**: 010-envelope-follower
**Date**: 2025-12-23

## Entities

### DetectionMode (Enumeration)

Detection algorithm type selection.

| Value | Numeric | Description |
|-------|---------|-------------|
| Amplitude | 0 | Full-wave rectification + smoothing |
| RMS | 1 | Squared signal + smoothing + sqrt |
| Peak | 2 | Instant attack, configurable release |

### EnvelopeFollower (Class)

Layer 2 DSP Processor that tracks the amplitude envelope of an audio signal.

#### Attributes

| Attribute | Type | Range | Default | Description |
|-----------|------|-------|---------|-------------|
| mode_ | DetectionMode | enum | Amplitude | Current detection algorithm |
| attackTimeMs_ | float | [0.1, 500] | 10.0 | Attack time in milliseconds |
| releaseTimeMs_ | float | [1, 5000] | 100.0 | Release time in milliseconds |
| sidechainEnabled_ | bool | true/false | false | Sidechain filter enabled |
| sidechainCutoffHz_ | float | [20, 500] | 80.0 | Sidechain HP cutoff frequency |

#### State Variables

| Variable | Type | Description |
|----------|------|-------------|
| envelope_ | float | Current envelope value [0.0, 1.0+] |
| attackCoeff_ | float | Smoothing coefficient for attack |
| releaseCoeff_ | float | Smoothing coefficient for release |
| sampleRate_ | double | Current sample rate |

#### Composed Components

| Component | Type | Layer | Purpose |
|-----------|------|-------|---------|
| sidechainFilter_ | Biquad | 1 | Optional highpass pre-filter |

#### Methods

**Lifecycle:**

| Method | Signature | Description |
|--------|-----------|-------------|
| prepare | `void prepare(double sampleRate, size_t maxBlockSize) noexcept` | Initialize for processing |
| reset | `void reset() noexcept` | Clear internal state |

**Processing:**

| Method | Signature | Description |
|--------|-----------|-------------|
| process | `void process(const float* input, float* output, size_t numSamples) noexcept` | Block processing (separate buffers) |
| process | `void process(float* buffer, size_t numSamples) noexcept` | Block processing (in-place, writes envelope) |
| processSample | `float processSample(float input) noexcept` | Per-sample processing |
| getCurrentValue | `float getCurrentValue() const noexcept` | Get current envelope without advancing |

**Parameter Setters:**

| Method | Signature | Description |
|--------|-----------|-------------|
| setMode | `void setMode(DetectionMode mode) noexcept` | Set detection algorithm |
| setAttackTime | `void setAttackTime(float ms) noexcept` | Set attack time [0.1-500ms] |
| setReleaseTime | `void setReleaseTime(float ms) noexcept` | Set release time [1-5000ms] |
| setSidechainEnabled | `void setSidechainEnabled(bool enabled) noexcept` | Enable/disable sidechain filter |
| setSidechainCutoff | `void setSidechainCutoff(float hz) noexcept` | Set sidechain HP cutoff [20-500Hz] |

**Parameter Getters:**

| Method | Signature | Description |
|--------|-----------|-------------|
| getMode | `DetectionMode getMode() const noexcept` | Get current detection mode |
| getAttackTime | `float getAttackTime() const noexcept` | Get attack time in ms |
| getReleaseTime | `float getReleaseTime() const noexcept` | Get release time in ms |
| isSidechainEnabled | `bool isSidechainEnabled() const noexcept` | Get sidechain enabled state |
| getSidechainCutoff | `float getSidechainCutoff() const noexcept` | Get sidechain cutoff in Hz |

**Info:**

| Method | Signature | Description |
|--------|-----------|-------------|
| getLatency | `size_t getLatency() const noexcept` | Get processing latency (0 or filter delay) |

## Relationships

```
EnvelopeFollower ──────────────────────────────────────────────────────
│
├── contains: DetectionMode mode_
│
├── composes: Biquad sidechainFilter_          (Layer 1)
│   └── configured as FilterType::Highpass
│
└── uses: detail::flushDenormal()              (Layer 0)
    └── from dsp/core/db_utils.h
```

## State Transitions

### Detection Mode Change

```
[Any Mode] ──setMode(newMode)──> [New Mode]
                                    │
                                    └── No state reset (envelope preserved)
                                        Smooth transition via existing envelope value
```

### Sidechain Filter Toggle

```
[Disabled] ──setSidechainEnabled(true)──> [Enabled]
                                              │
                                              └── Filter applies to input before detection
                                                  Filter state preserved between calls

[Enabled] ──setSidechainEnabled(false)──> [Disabled]
                                              │
                                              └── Input bypasses filter
                                                  Filter state NOT reset (preserves warmth)
```

## Validation Rules

| Parameter | Validation | Behavior if Invalid |
|-----------|------------|---------------------|
| attackTimeMs | [0.1, 500] | Clamp to range |
| releaseTimeMs | [1, 5000] | Clamp to range |
| sidechainCutoffHz | [20, 500] | Clamp to range |
| input sample | NaN check | Treat as 0.0 |
| input sample | Inf check | Clamp to ±1e10 |

## Constants

| Constant | Value | Description |
|----------|-------|-------------|
| kMinAttackMs | 0.1f | Minimum attack time |
| kMaxAttackMs | 500.0f | Maximum attack time |
| kMinReleaseMs | 1.0f | Minimum release time |
| kMaxReleaseMs | 5000.0f | Maximum release time |
| kDefaultAttackMs | 10.0f | Default attack time |
| kDefaultReleaseMs | 100.0f | Default release time |
| kMinSidechainHz | 20.0f | Minimum sidechain cutoff |
| kMaxSidechainHz | 500.0f | Maximum sidechain cutoff |
| kDefaultSidechainHz | 80.0f | Default sidechain cutoff |
