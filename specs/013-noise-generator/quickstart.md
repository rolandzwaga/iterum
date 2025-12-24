# Quickstart: NoiseGenerator

**Feature**: 013-noise-generator | **Date**: 2025-12-23

## Basic Usage

### Adding White Noise

```cpp
#include "dsp/processors/noise_generator.h"

using namespace Iterum::DSP;

// Create and prepare
NoiseGenerator noise;
noise.prepare(44100.0f, 512);

// Enable white noise at -20dB
noise.setNoiseEnabled(NoiseType::White, true);
noise.setNoiseLevel(NoiseType::White, -20.0f);

// Generate noise
float buffer[512];
noise.process(buffer, 512);
```

### Adding Pink Noise

```cpp
NoiseGenerator noise;
noise.prepare(44100.0f, 512);

// Enable pink noise at -18dB
noise.setNoiseEnabled(NoiseType::Pink, true);
noise.setNoiseLevel(NoiseType::Pink, -18.0f);

// Generate
float output[512];
noise.process(output, 512);
```

## Signal-Dependent Noise

### Tape Hiss with Sidechain

```cpp
NoiseGenerator noise;
noise.prepare(44100.0f, 512);

// Configure tape hiss
noise.setNoiseEnabled(NoiseType::TapeHiss, true);
noise.setNoiseLevel(NoiseType::TapeHiss, -30.0f);
noise.setTapeHissParams(
    -60.0f,  // Floor: minimum hiss when signal is silent
    1.0f     // Sensitivity: normal modulation depth
);

// Process with sidechain input
float input[512];  // Audio signal
float output[512]; // Noise only
noise.process(input, output, 512);

// Or mix noise into signal (in-place OK)
noise.processMix(input, input, 512);
```

### Asperity Noise

```cpp
NoiseGenerator noise;
noise.prepare(44100.0f, 512);

// Configure asperity (tape head contact noise)
noise.setNoiseEnabled(NoiseType::Asperity, true);
noise.setNoiseLevel(NoiseType::Asperity, -36.0f);
noise.setAsperityParams(
    -72.0f,  // Floor: very quiet when no signal
    1.5f     // Sensitivity: more responsive to signal
);

// Must use sidechain version for signal-dependent modulation
float input[512];
float output[512];
noise.process(input, output, 512);
```

## Vinyl Effects

### Crackle with Surface Noise

```cpp
NoiseGenerator noise;
noise.prepare(44100.0f, 512);

// Configure vinyl crackle
noise.setNoiseEnabled(NoiseType::VinylCrackle, true);
noise.setNoiseLevel(NoiseType::VinylCrackle, -24.0f);
noise.setCrackleParams(
    3.0f,    // Density: 3 clicks per second
    -42.0f   // Surface noise: continuous low-level hiss
);

// Generate
float output[512];
noise.process(output, 512);
```

## Mixing Multiple Noise Types

### Full Tape Character

```cpp
NoiseGenerator noise;
noise.prepare(44100.0f, 512);

// Layer 1: Base tape hiss
noise.setNoiseEnabled(NoiseType::TapeHiss, true);
noise.setNoiseLevel(NoiseType::TapeHiss, -30.0f);
noise.setTapeHissParams(-54.0f, 1.0f);

// Layer 2: Asperity for realism
noise.setNoiseEnabled(NoiseType::Asperity, true);
noise.setNoiseLevel(NoiseType::Asperity, -40.0f);
noise.setAsperityParams(-66.0f, 1.2f);

// Master level
noise.setMasterLevel(-6.0f);

// Process
float input[512];
float output[512];
noise.process(input, output, 512);
```

### Lo-Fi Vinyl Character

```cpp
NoiseGenerator noise;
noise.prepare(44100.0f, 512);

// Vinyl crackle
noise.setNoiseEnabled(NoiseType::VinylCrackle, true);
noise.setNoiseLevel(NoiseType::VinylCrackle, -20.0f);
noise.setCrackleParams(5.0f, -36.0f);  // Medium crackle, audible surface

// Add subtle pink noise for warmth
noise.setNoiseEnabled(NoiseType::Pink, true);
noise.setNoiseLevel(NoiseType::Pink, -36.0f);

// Mix into audio
float audio[512];
noise.processMix(audio, audio, 512);  // In-place
```

## Integration with Other Processors

### In a Processing Chain

```cpp
// Typical usage in a delay/character processor
class CharacterProcessor {
    NoiseGenerator noise_;
    // ... other components

public:
    void prepare(float sampleRate, size_t maxBlockSize) {
        noise_.prepare(sampleRate, maxBlockSize);
        // ... prepare other components
    }

    void process(float* buffer, size_t numSamples) {
        // ... other processing (saturation, filtering)

        // Add noise character at the end
        if (noise_.isAnyEnabled()) {
            noise_.processMix(buffer, buffer, numSamples);
        }
    }
};
```

### Stereo Processing

```cpp
// NoiseGenerator is mono; for stereo, use two instances or duplicate
NoiseGenerator noiseL, noiseR;
noiseL.prepare(sampleRate, blockSize);
noiseR.prepare(sampleRate, blockSize);

// Different seeds for uncorrelated stereo noise
noiseR.reset();  // Reset with different internal seed

// Configure both the same
auto configureNoise = [](NoiseGenerator& n) {
    n.setNoiseEnabled(NoiseType::TapeHiss, true);
    n.setNoiseLevel(NoiseType::TapeHiss, -30.0f);
};
configureNoise(noiseL);
configureNoise(noiseR);

// Process stereo
float left[512], right[512];
noiseL.processMix(left, left, 512);
noiseR.processMix(right, right, 512);
```

## Real-Time Considerations

### Avoiding Clicks

Level changes are automatically smoothed:

```cpp
// Safe to call from UI thread - no clicks
noise.setNoiseLevel(NoiseType::White, -30.0f);  // Smoothed transition

// Enable/disable also smoothed
noise.setNoiseEnabled(NoiseType::Pink, true);   // Fade in
noise.setNoiseEnabled(NoiseType::Pink, false);  // Fade out
```

### Performance Check

```cpp
// Skip processing if nothing enabled
void process(float* buffer, size_t numSamples) {
    if (noise_.isAnyEnabled()) {
        noise_.processMix(buffer, buffer, numSamples);
    }
    // else: no CPU cost
}
```

## Common Configurations

| Character | Noise Types | Levels | Notes |
|-----------|-------------|--------|-------|
| Subtle Tape | TapeHiss | -36dB | Floor -66dB, sens 0.8 |
| Authentic Tape | TapeHiss + Asperity | -30/-40dB | Floor -54/-66dB |
| Lo-Fi Vinyl | VinylCrackle + Pink | -20/-36dB | Density 5, surface -36dB |
| Heavy Vinyl | VinylCrackle | -12dB | Density 10, surface -24dB |
| Test/Dither | White | -72dB | For dithering applications |
| Ambience | Pink | -40dB | Subtle room tone |
