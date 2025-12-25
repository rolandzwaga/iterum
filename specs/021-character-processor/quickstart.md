# Quickstart: Character Processor

**Feature**: 021-character-processor
**Date**: 2025-12-25
**Purpose**: Usage examples and integration guide

## Basic Usage

### Setup

```cpp
#include "dsp/systems/character_processor.h"

using namespace Iterum::DSP;

// Create and prepare processor
CharacterProcessor character;
character.prepare(44100.0, 512);  // 44.1kHz, 512 sample blocks
```

### Tape Mode Example

Apply vintage tape warmth with wow/flutter:

```cpp
// Select tape mode
character.setMode(CharacterMode::Tape);

// Configure tape character
character.setTapeSaturation(0.5f);      // 50% saturation
character.setTapeWowRate(1.0f);         // 1Hz wow rate
character.setTapeWowDepth(0.3f);        // 30% wow depth
character.setTapeFlutterRate(5.0f);     // 5Hz flutter rate
character.setTapeFlutterDepth(0.1f);    // 10% flutter depth
character.setTapeHissLevel(-55.0f);     // -55dB hiss
character.setTapeRolloffFreq(8000.0f);  // 8kHz high-frequency rolloff

// Process audio
character.process(audioBuffer, numSamples);
```

### BBD Mode Example

Apply bucket-brigade device coloration:

```cpp
// Select BBD mode
character.setMode(CharacterMode::BBD);

// Configure BBD character
character.setBBDBandwidth(6000.0f);         // 6kHz bandwidth limit
character.setBBDClockNoiseLevel(-65.0f);    // -65dB clock noise
character.setBBDSaturation(0.3f);           // 30% soft saturation

// Process stereo audio
character.processStereo(leftBuffer, rightBuffer, numSamples);
```

### Digital Vintage Mode Example

Apply lo-fi digital character:

```cpp
// Select digital vintage mode
character.setMode(CharacterMode::DigitalVintage);

// Configure lo-fi character
character.setDigitalBitDepth(8.0f);              // 8-bit quantization
character.setDigitalSampleRateReduction(4.0f);   // 4x sample rate reduction
character.setDigitalDitherAmount(0.7f);          // 70% dither

// Process audio
character.process(audioBuffer, numSamples);
```

### Clean Mode Example

Bypass all processing:

```cpp
// Select clean mode (unity gain passthrough)
character.setMode(CharacterMode::Clean);

// Audio passes through unchanged
character.process(audioBuffer, numSamples);
```

---

## Mode Transitions

### Smooth Crossfade

Mode changes automatically crossfade to prevent clicks:

```cpp
// Configure crossfade time (optional, default 50ms)
character.setCrossfadeTime(30.0f);  // 30ms crossfade

// Switch mode - crossfade happens automatically
character.setMode(CharacterMode::BBD);

// Check if still crossfading
if (character.isCrossfading()) {
    // Transition in progress
}
```

### Rapid Mode Switching

Safe for rapid automation:

```cpp
for (int block = 0; block < numBlocks; ++block) {
    // Mode can change every block without clicks
    if (shouldChangeMode) {
        character.setMode(newMode);
    }

    character.process(buffer, blockSize);
}
```

---

## Integration with Delay Engine

### In Feedback Loop

```cpp
#include "dsp/systems/delay_engine.h"
#include "dsp/systems/feedback_network.h"
#include "dsp/systems/character_processor.h"

// Typical delay plugin setup
DelayEngine delay;
FeedbackNetwork feedback;
CharacterProcessor character;

// In process callback
void processBlock(float* buffer, size_t numSamples) {
    // Get delay output
    delay.process(buffer, numSamples);

    // Apply character to delayed signal
    character.process(buffer, numSamples);

    // Feed back into delay with feedback amount
    feedback.process(buffer, numSamples);
}
```

### Per-Mode Preset Example

```cpp
void setDelayPreset(DelayPreset preset) {
    switch (preset) {
        case DelayPreset::VintageTape:
            character.setMode(CharacterMode::Tape);
            character.setTapeSaturation(0.6f);
            character.setTapeWowDepth(0.4f);
            character.setTapeHissLevel(-50.0f);
            break;

        case DelayPreset::AnalogChorus:
            character.setMode(CharacterMode::BBD);
            character.setBBDBandwidth(5000.0f);
            character.setBBDSaturation(0.4f);
            break;

        case DelayPreset::EarlyDigital:
            character.setMode(CharacterMode::DigitalVintage);
            character.setDigitalBitDepth(12.0f);
            character.setDigitalSampleRateReduction(2.0f);
            break;

        case DelayPreset::Clean:
            character.setMode(CharacterMode::Clean);
            break;
    }
}
```

---

## Real-Time Safety

### Thread-Safe Parameter Updates

All setters are real-time safe:

```cpp
// Safe to call from audio thread
character.setTapeSaturation(automatedValue);  // Uses smoothing
character.setMode(newMode);                   // Uses crossfade
```

### State Reset

Reset without reallocation:

```cpp
// On transport stop/start
character.reset();  // Clears internal state, no allocation
```

---

## Performance Considerations

### CPU Usage

- Tape mode: ~0.4% CPU (includes saturation oversampling)
- BBD mode: ~0.3% CPU
- Digital Vintage mode: ~0.1% CPU
- Clean mode: ~0.01% CPU

### Latency

```cpp
// Get processing latency for host compensation
size_t latency = character.getLatency();  // Samples
```

### Optimize for Clean Mode

```cpp
// Skip processing entirely when in clean mode
if (character.getMode() == CharacterMode::Clean &&
    !character.isCrossfading()) {
    // No processing needed, audio passes through unchanged
    return;
}

character.process(buffer, numSamples);
```

---

## Test Verification Example

```cpp
TEST_CASE("CharacterProcessor applies tape saturation", "[character][tape]") {
    CharacterProcessor character;
    character.prepare(44100.0, 512);
    character.setMode(CharacterMode::Tape);
    character.setTapeSaturation(1.0f);  // Maximum saturation

    // Generate 1kHz sine wave
    std::array<float, 512> buffer;
    generateSine(buffer.data(), 512, 1000.0f, 44100.0f);

    character.process(buffer.data(), 512);

    // Verify harmonic distortion was added
    float thd = measureTHD(buffer.data(), 512, 1000.0f, 44100.0f);
    REQUIRE(thd > 0.001f);  // THD > 0.1%
}
```
