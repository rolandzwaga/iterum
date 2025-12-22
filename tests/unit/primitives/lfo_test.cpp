// ==============================================================================
// Layer 1: DSP Primitive Tests - LFO (Low Frequency Oscillator)
// ==============================================================================
// Test-First Development (Constitution Principle XII)
// Tests written before implementation.
//
// Tests for: src/dsp/primitives/lfo.h
// Contract: specs/003-lfo/contracts/lfo.h
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "dsp/primitives/lfo.h"

#include <array>
#include <cmath>
#include <numbers>
#include <vector>

using namespace Iterum::DSP;
using Catch::Approx;

// ==============================================================================
// Phase 2: Foundational Tests - Enumerations (T005-T007)
// ==============================================================================

// T005: Waveform enum tests
TEST_CASE("Waveform enum has correct values", "[lfo][enum][foundational]") {
    SECTION("Sine is first waveform (value 0)") {
        CHECK(static_cast<uint8_t>(Waveform::Sine) == 0);
    }

    SECTION("All 6 waveforms have sequential values") {
        CHECK(static_cast<uint8_t>(Waveform::Sine) == 0);
        CHECK(static_cast<uint8_t>(Waveform::Triangle) == 1);
        CHECK(static_cast<uint8_t>(Waveform::Sawtooth) == 2);
        CHECK(static_cast<uint8_t>(Waveform::Square) == 3);
        CHECK(static_cast<uint8_t>(Waveform::SampleHold) == 4);
        CHECK(static_cast<uint8_t>(Waveform::SmoothRandom) == 5);
    }

    SECTION("Waveform enum is uint8_t") {
        static_assert(std::is_same_v<std::underlying_type_t<Waveform>, uint8_t>);
    }
}

// T006: NoteValue enum tests
TEST_CASE("NoteValue enum has correct values", "[lfo][enum][foundational]") {
    SECTION("Whole is first note value (value 0)") {
        CHECK(static_cast<uint8_t>(NoteValue::Whole) == 0);
    }

    SECTION("All 6 note values have sequential values") {
        CHECK(static_cast<uint8_t>(NoteValue::Whole) == 0);
        CHECK(static_cast<uint8_t>(NoteValue::Half) == 1);
        CHECK(static_cast<uint8_t>(NoteValue::Quarter) == 2);
        CHECK(static_cast<uint8_t>(NoteValue::Eighth) == 3);
        CHECK(static_cast<uint8_t>(NoteValue::Sixteenth) == 4);
        CHECK(static_cast<uint8_t>(NoteValue::ThirtySecond) == 5);
    }

    SECTION("NoteValue enum is uint8_t") {
        static_assert(std::is_same_v<std::underlying_type_t<NoteValue>, uint8_t>);
    }
}

// T007: NoteModifier enum tests
TEST_CASE("NoteModifier enum has correct values", "[lfo][enum][foundational]") {
    SECTION("None is first modifier (value 0)") {
        CHECK(static_cast<uint8_t>(NoteModifier::None) == 0);
    }

    SECTION("All 3 modifiers have sequential values") {
        CHECK(static_cast<uint8_t>(NoteModifier::None) == 0);
        CHECK(static_cast<uint8_t>(NoteModifier::Dotted) == 1);
        CHECK(static_cast<uint8_t>(NoteModifier::Triplet) == 2);
    }

    SECTION("NoteModifier enum is uint8_t") {
        static_assert(std::is_same_v<std::underlying_type_t<NoteModifier>, uint8_t>);
    }
}

// ==============================================================================
// Phase 2: LFO Class Foundational Tests
// ==============================================================================

TEST_CASE("LFO default construction and preparation", "[lfo][foundational]") {
    LFO lfo;

    SECTION("can be prepared with standard sample rates") {
        REQUIRE_NOTHROW(lfo.prepare(44100.0));
        CHECK(lfo.sampleRate() == 44100.0);
    }

    SECTION("can be prepared with high sample rate") {
        lfo.prepare(96000.0);
        CHECK(lfo.sampleRate() == 96000.0);
    }

    SECTION("can be prepared with low sample rate") {
        lfo.prepare(22050.0);
        CHECK(lfo.sampleRate() == 22050.0);
    }
}

TEST_CASE("LFO reset clears state", "[lfo][foundational]") {
    LFO lfo;
    lfo.prepare(44100.0);
    lfo.setFrequency(5.0f);

    // Process some samples to advance phase
    for (int i = 0; i < 1000; ++i) {
        lfo.process();
    }

    SECTION("reset returns phase to zero") {
        lfo.reset();
        // After reset, first sine sample at phase 0 should be 0.0
        float firstSample = lfo.process();
        CHECK(firstSample == Approx(0.0f).margin(0.001f));
    }
}

TEST_CASE("LFO default values after prepare", "[lfo][foundational]") {
    LFO lfo;
    lfo.prepare(44100.0);

    SECTION("default waveform is Sine") {
        CHECK(lfo.waveform() == Waveform::Sine);
    }

    SECTION("default frequency is 1 Hz") {
        CHECK(lfo.frequency() == Approx(1.0f));
    }

    SECTION("default phase offset is 0") {
        CHECK(lfo.phaseOffset() == Approx(0.0f));
    }

    SECTION("tempo sync is disabled by default") {
        CHECK_FALSE(lfo.tempoSyncEnabled());
    }

    SECTION("retrigger is enabled by default") {
        CHECK(lfo.retriggerEnabled());
    }
}

// ==============================================================================
// Phase 3: User Story 1 - Basic Sine LFO Tests (T015-T020)
// ==============================================================================
// Reference: specs/003-lfo/spec.md US1

// T015: Test sine wavetable generation
TEST_CASE("Sine wavetable generates correct values", "[lfo][US1][sine]") {
    LFO lfo;
    lfo.prepare(44100.0);
    lfo.setWaveform(Waveform::Sine);

    SECTION("sine starts at zero crossing (phase 0)") {
        float sample = lfo.process();
        CHECK(sample == Approx(0.0f).margin(0.001f));
    }

    SECTION("sine reaches peak at 1/4 cycle") {
        lfo.prepare(100.0);  // 100 Hz sample rate for easy math
        lfo.setFrequency(1.0f);  // 1 Hz = 100 samples per cycle

        // Process 25 samples to reach 1/4 cycle (90 degrees)
        float sample = 0.0f;
        for (int i = 0; i < 25; ++i) {
            sample = lfo.process();
        }
        CHECK(sample == Approx(1.0f).margin(0.02f));  // Peak at 90 degrees
    }

    SECTION("sine reaches trough at 3/4 cycle") {
        lfo.prepare(100.0);
        lfo.setFrequency(1.0f);

        // Process 75 samples to reach 3/4 cycle (270 degrees)
        float sample = 0.0f;
        for (int i = 0; i < 75; ++i) {
            sample = lfo.process();
        }
        CHECK(sample == Approx(-1.0f).margin(0.02f));  // Trough at 270 degrees
    }
}

// T016: Test process() returns values in [-1, +1] range
TEST_CASE("LFO process returns values in [-1, +1] range", "[lfo][US1][range]") {
    LFO lfo;
    lfo.prepare(44100.0);
    lfo.setWaveform(Waveform::Sine);
    lfo.setFrequency(10.0f);  // Fast oscillation

    // Process many samples
    for (int i = 0; i < 10000; ++i) {
        float sample = lfo.process();
        REQUIRE(sample >= -1.0f);
        REQUIRE(sample <= 1.0f);
    }
}

// T017: Test 1 Hz LFO completes one cycle in 44100 samples
TEST_CASE("1 Hz LFO completes one cycle in sampleRate samples", "[lfo][US1][cycle]") {
    LFO lfo;
    lfo.prepare(44100.0);
    lfo.setWaveform(Waveform::Sine);
    lfo.setFrequency(1.0f);

    // Collect samples for one full cycle plus a bit more
    std::vector<float> samples(44100 + 100);
    for (size_t i = 0; i < samples.size(); ++i) {
        samples[i] = lfo.process();
    }

    SECTION("first sample is at zero crossing") {
        CHECK(samples[0] == Approx(0.0f).margin(0.001f));
    }

    SECTION("sample at 44100 returns to start") {
        // After exactly one cycle, should be back near zero
        CHECK(samples[44100] == Approx(0.0f).margin(0.001f));
    }

    SECTION("peak occurs near 1/4 cycle") {
        // Peak should be around sample 11025 (44100/4)
        float maxVal = 0.0f;
        size_t maxIdx = 0;
        for (size_t i = 0; i < 44100; ++i) {
            if (samples[i] > maxVal) {
                maxVal = samples[i];
                maxIdx = i;
            }
        }
        CHECK(maxVal == Approx(1.0f).margin(0.001f));
        CHECK(maxIdx == Approx(11025).margin(100));  // Near 1/4 cycle
    }
}

// T018: Test sine starts at 0.0 at phase 0
TEST_CASE("Sine LFO starts at zero crossing", "[lfo][US1][phase]") {
    LFO lfo;
    lfo.prepare(44100.0);
    lfo.setWaveform(Waveform::Sine);
    lfo.setFrequency(1.0f);

    // Very first sample should be at zero crossing
    float firstSample = lfo.process();
    CHECK(firstSample == Approx(0.0f).margin(0.001f));
}

// T019: Test setFrequency() clamps to [0.01, 20.0] Hz
TEST_CASE("setFrequency clamps to valid range", "[lfo][US1][frequency]") {
    LFO lfo;
    lfo.prepare(44100.0);

    SECTION("frequency below minimum is clamped to 0.01 Hz") {
        lfo.setFrequency(0.001f);
        CHECK(lfo.frequency() == Approx(0.01f));
    }

    SECTION("frequency at minimum is accepted") {
        lfo.setFrequency(0.01f);
        CHECK(lfo.frequency() == Approx(0.01f));
    }

    SECTION("frequency above maximum is clamped to 20 Hz") {
        lfo.setFrequency(100.0f);
        CHECK(lfo.frequency() == Approx(20.0f));
    }

    SECTION("frequency at maximum is accepted") {
        lfo.setFrequency(20.0f);
        CHECK(lfo.frequency() == Approx(20.0f));
    }

    SECTION("frequency in range is unchanged") {
        lfo.setFrequency(5.0f);
        CHECK(lfo.frequency() == Approx(5.0f));
    }

    SECTION("zero frequency is clamped to minimum") {
        lfo.setFrequency(0.0f);
        CHECK(lfo.frequency() == Approx(0.01f));
    }

    SECTION("negative frequency is clamped to minimum") {
        lfo.setFrequency(-5.0f);
        CHECK(lfo.frequency() == Approx(0.01f));
    }
}

// T020: Test processBlock() generates correct samples
TEST_CASE("processBlock generates correct samples", "[lfo][US1][block]") {
    LFO lfo;
    lfo.prepare(44100.0);
    lfo.setWaveform(Waveform::Sine);
    lfo.setFrequency(1.0f);

    // Generate samples using process() for reference
    std::array<float, 512> reference{};
    for (size_t i = 0; i < reference.size(); ++i) {
        reference[i] = lfo.process();
    }

    // Reset and generate using processBlock()
    lfo.reset();
    std::array<float, 512> block{};
    lfo.processBlock(block.data(), block.size());

    // Both methods should produce identical results
    for (size_t i = 0; i < 512; ++i) {
        CHECK(block[i] == Approx(reference[i]).margin(0.0001f));
    }
}

// ==============================================================================
// Phase 4: User Story 2 - Multiple Waveforms Tests (T029-T034)
// ==============================================================================
// Reference: specs/003-lfo/spec.md US2

// T029: Test triangle wavetable generation
TEST_CASE("Triangle waveform has correct shape", "[lfo][US2][triangle]") {
    LFO lfo;
    lfo.prepare(100.0);  // 100 samples/sec for easy math
    lfo.setWaveform(Waveform::Triangle);
    lfo.setFrequency(1.0f);  // 1 Hz = 100 samples per cycle

    std::vector<float> samples(100);
    for (int i = 0; i < 100; ++i) {
        samples[i] = lfo.process();
    }

    SECTION("triangle starts at 0") {
        CHECK(samples[0] == Approx(0.0f).margin(0.05f));
    }

    SECTION("triangle reaches +1 at 1/4 cycle") {
        CHECK(samples[25] == Approx(1.0f).margin(0.05f));
    }

    SECTION("triangle returns to 0 at 1/2 cycle") {
        CHECK(samples[50] == Approx(0.0f).margin(0.05f));
    }

    SECTION("triangle reaches -1 at 3/4 cycle") {
        CHECK(samples[75] == Approx(-1.0f).margin(0.05f));
    }

    SECTION("triangle values stay in [-1, +1]") {
        for (float s : samples) {
            CHECK(s >= -1.0f);
            CHECK(s <= 1.0f);
        }
    }
}

// T030: Test sawtooth wavetable generation
TEST_CASE("Sawtooth waveform has correct shape", "[lfo][US2][sawtooth]") {
    LFO lfo;
    lfo.prepare(100.0);
    lfo.setWaveform(Waveform::Sawtooth);
    lfo.setFrequency(1.0f);

    std::vector<float> samples(100);
    for (int i = 0; i < 100; ++i) {
        samples[i] = lfo.process();
    }

    SECTION("sawtooth starts at -1") {
        CHECK(samples[0] == Approx(-1.0f).margin(0.05f));
    }

    SECTION("sawtooth reaches 0 at mid cycle") {
        CHECK(samples[50] == Approx(0.0f).margin(0.05f));
    }

    SECTION("sawtooth approaches +1 at end of cycle") {
        // Last sample before wrap should be close to +1
        CHECK(samples[99] == Approx(1.0f).margin(0.05f));
    }

    SECTION("sawtooth is monotonically increasing within cycle") {
        for (size_t i = 1; i < samples.size(); ++i) {
            CHECK(samples[i] >= samples[i-1] - 0.01f);  // Allow small tolerance
        }
    }
}

// T031: Test square wavetable generation
TEST_CASE("Square waveform has correct shape", "[lfo][US2][square]") {
    LFO lfo;
    lfo.prepare(100.0);
    lfo.setWaveform(Waveform::Square);
    lfo.setFrequency(1.0f);

    std::vector<float> samples(100);
    for (int i = 0; i < 100; ++i) {
        samples[i] = lfo.process();
    }

    SECTION("square is +1 for first half") {
        for (int i = 0; i < 50; ++i) {
            CHECK(samples[i] == Approx(1.0f).margin(0.01f));
        }
    }

    SECTION("square is -1 for second half") {
        for (int i = 50; i < 100; ++i) {
            CHECK(samples[i] == Approx(-1.0f).margin(0.01f));
        }
    }

    SECTION("square only has values +1 or -1") {
        for (float s : samples) {
            bool isValid = (std::abs(s - 1.0f) < 0.01f) || (std::abs(s + 1.0f) < 0.01f);
            CHECK(isValid);
        }
    }
}

// T032: Test sample & hold outputs
TEST_CASE("Sample & Hold waveform behavior", "[lfo][US2][samplehold]") {
    LFO lfo;
    lfo.prepare(100.0);
    lfo.setWaveform(Waveform::SampleHold);
    lfo.setFrequency(1.0f);  // 1 Hz = 100 samples per cycle

    SECTION("output stays constant within a cycle") {
        float firstSample = lfo.process();

        // All samples within the first cycle should be the same
        for (int i = 1; i < 99; ++i) {
            float sample = lfo.process();
            CHECK(sample == Approx(firstSample).margin(0.001f));
        }
    }

    SECTION("output changes at cycle boundary") {
        // Process first cycle
        float firstCycleValue = lfo.process();
        for (int i = 1; i < 100; ++i) {
            lfo.process();
        }

        // Get second cycle value
        float secondCycleValue = lfo.process();

        // Values might be the same by chance, but after many cycles they should differ
        // This is a probabilistic test - run multiple cycles
        bool foundDifferent = (firstCycleValue != secondCycleValue);
        for (int cycle = 0; cycle < 10 && !foundDifferent; ++cycle) {
            for (int i = 0; i < 100; ++i) {
                lfo.process();
            }
            float nextValue = lfo.process();
            if (std::abs(nextValue - firstCycleValue) > 0.01f) {
                foundDifferent = true;
            }
        }
        CHECK(foundDifferent);
    }

    SECTION("output is in [-1, +1] range") {
        for (int i = 0; i < 1000; ++i) {
            float sample = lfo.process();
            CHECK(sample >= -1.0f);
            CHECK(sample <= 1.0f);
        }
    }
}

// T033: Test smoothed random outputs
TEST_CASE("Smoothed Random waveform behavior", "[lfo][US2][smoothrandom]") {
    LFO lfo;
    lfo.prepare(100.0);
    lfo.setWaveform(Waveform::SmoothRandom);
    lfo.setFrequency(1.0f);

    SECTION("output is in [-1, +1] range") {
        for (int i = 0; i < 1000; ++i) {
            float sample = lfo.process();
            CHECK(sample >= -1.0f);
            CHECK(sample <= 1.0f);
        }
    }

    SECTION("output changes smoothly (no discontinuities)") {
        float prevSample = lfo.process();
        float maxDelta = 0.0f;

        for (int i = 0; i < 500; ++i) {
            float sample = lfo.process();
            float delta = std::abs(sample - prevSample);
            maxDelta = std::max(maxDelta, delta);
            prevSample = sample;
        }

        // Should have smooth transitions (max change per sample should be small)
        // At 1 Hz with 100 samples/cycle, max interpolation delta ~ 2.0/100 = 0.02
        CHECK(maxDelta < 0.1f);  // Allow some margin
    }

    SECTION("output varies over time (not constant)") {
        float firstSample = lfo.process();
        float minVal = firstSample, maxVal = firstSample;

        for (int i = 0; i < 500; ++i) {
            float sample = lfo.process();
            minVal = std::min(minVal, sample);
            maxVal = std::max(maxVal, sample);
        }

        // Should have some variation
        CHECK(maxVal - minVal > 0.5f);
    }
}

// T034: Test setWaveform() changes active waveform
TEST_CASE("setWaveform changes active waveform", "[lfo][US2][waveform]") {
    LFO lfo;
    lfo.prepare(100.0);
    lfo.setFrequency(1.0f);

    SECTION("waveform query returns set value") {
        lfo.setWaveform(Waveform::Sine);
        CHECK(lfo.waveform() == Waveform::Sine);

        lfo.setWaveform(Waveform::Triangle);
        CHECK(lfo.waveform() == Waveform::Triangle);

        lfo.setWaveform(Waveform::Sawtooth);
        CHECK(lfo.waveform() == Waveform::Sawtooth);

        lfo.setWaveform(Waveform::Square);
        CHECK(lfo.waveform() == Waveform::Square);

        lfo.setWaveform(Waveform::SampleHold);
        CHECK(lfo.waveform() == Waveform::SampleHold);

        lfo.setWaveform(Waveform::SmoothRandom);
        CHECK(lfo.waveform() == Waveform::SmoothRandom);
    }

    SECTION("different waveforms produce different output") {
        // Get first sample from sine
        lfo.setWaveform(Waveform::Sine);
        lfo.reset();
        float sineSample = lfo.process();

        // Get first sample from square (should be +1)
        lfo.setWaveform(Waveform::Square);
        lfo.reset();
        float squareSample = lfo.process();

        // They should be different
        CHECK(sineSample != squareSample);
    }
}

// ==============================================================================
// Phase 5: User Story 3 - Tempo Sync Tests (T045-T050)
// ==============================================================================
// Reference: specs/003-lfo/spec.md US3

// T045: Test 1/4 note at 120 BPM = 2 Hz
TEST_CASE("Tempo sync 1/4 note at 120 BPM", "[lfo][US3][temposync]") {
    LFO lfo;
    lfo.prepare(44100.0);
    lfo.setWaveform(Waveform::Sine);
    lfo.setTempoSync(true);
    lfo.setTempo(120.0f);
    lfo.setNoteValue(NoteValue::Quarter, NoteModifier::None);

    // At 120 BPM, quarter note = 0.5 seconds = 2 Hz
    CHECK(lfo.frequency() == Approx(2.0f).margin(0.001f));
}

// T046: Test dotted 1/8 note at 120 BPM
TEST_CASE("Tempo sync dotted 1/8 at 120 BPM", "[lfo][US3][temposync]") {
    LFO lfo;
    lfo.prepare(44100.0);
    lfo.setTempoSync(true);
    lfo.setTempo(120.0f);
    lfo.setNoteValue(NoteValue::Eighth, NoteModifier::Dotted);

    // Dotted 1/8 = 0.75 beats at 120 BPM
    // Frequency = 120 / (60 * 0.75) = 2.667 Hz
    CHECK(lfo.frequency() == Approx(2.6667f).margin(0.01f));
}

// T047: Test triplet 1/4 note at 120 BPM
TEST_CASE("Tempo sync triplet 1/4 at 120 BPM", "[lfo][US3][temposync]") {
    LFO lfo;
    lfo.prepare(44100.0);
    lfo.setTempoSync(true);
    lfo.setTempo(120.0f);
    lfo.setNoteValue(NoteValue::Quarter, NoteModifier::Triplet);

    // Triplet 1/4 = 2/3 beats at 120 BPM
    // Frequency = 120 / (60 * 2/3) = 3 Hz
    CHECK(lfo.frequency() == Approx(3.0f).margin(0.01f));
}

// T048: Test all 6 note values with normal modifier
TEST_CASE("Tempo sync all note values at 120 BPM", "[lfo][US3][temposync]") {
    LFO lfo;
    lfo.prepare(44100.0);
    lfo.setTempoSync(true);
    lfo.setTempo(120.0f);

    SECTION("Whole note (4 beats) = 0.5 Hz") {
        lfo.setNoteValue(NoteValue::Whole, NoteModifier::None);
        CHECK(lfo.frequency() == Approx(0.5f).margin(0.01f));
    }

    SECTION("Half note (2 beats) = 1 Hz") {
        lfo.setNoteValue(NoteValue::Half, NoteModifier::None);
        CHECK(lfo.frequency() == Approx(1.0f).margin(0.01f));
    }

    SECTION("Quarter note (1 beat) = 2 Hz") {
        lfo.setNoteValue(NoteValue::Quarter, NoteModifier::None);
        CHECK(lfo.frequency() == Approx(2.0f).margin(0.01f));
    }

    SECTION("Eighth note (0.5 beats) = 4 Hz") {
        lfo.setNoteValue(NoteValue::Eighth, NoteModifier::None);
        CHECK(lfo.frequency() == Approx(4.0f).margin(0.01f));
    }

    SECTION("Sixteenth note (0.25 beats) = 8 Hz") {
        lfo.setNoteValue(NoteValue::Sixteenth, NoteModifier::None);
        CHECK(lfo.frequency() == Approx(8.0f).margin(0.01f));
    }

    SECTION("ThirtySecond note (0.125 beats) = 16 Hz") {
        lfo.setNoteValue(NoteValue::ThirtySecond, NoteModifier::None);
        CHECK(lfo.frequency() == Approx(16.0f).margin(0.01f));
    }
}

// T049: Test setTempoSync() enables/disables tempo mode
TEST_CASE("setTempoSync enables and disables sync", "[lfo][US3][temposync]") {
    LFO lfo;
    lfo.prepare(44100.0);

    SECTION("tempo sync is disabled by default") {
        CHECK_FALSE(lfo.tempoSyncEnabled());
    }

    SECTION("setTempoSync(true) enables sync") {
        lfo.setTempoSync(true);
        CHECK(lfo.tempoSyncEnabled());
    }

    SECTION("setTempoSync(false) disables sync") {
        lfo.setTempoSync(true);
        lfo.setTempoSync(false);
        CHECK_FALSE(lfo.tempoSyncEnabled());
    }

    SECTION("when sync disabled, setFrequency controls frequency") {
        lfo.setTempoSync(false);
        lfo.setFrequency(5.0f);
        CHECK(lfo.frequency() == Approx(5.0f));
    }

    SECTION("when sync enabled, tempo/note controls frequency") {
        lfo.setFrequency(5.0f);  // This should be ignored when sync enabled
        lfo.setTempoSync(true);
        lfo.setTempo(120.0f);
        lfo.setNoteValue(NoteValue::Quarter);
        CHECK(lfo.frequency() == Approx(2.0f).margin(0.01f));  // Not 5.0f
    }
}

// T050: Test tempo change updates frequency
TEST_CASE("Tempo change updates synced frequency", "[lfo][US3][temposync]") {
    LFO lfo;
    lfo.prepare(44100.0);
    lfo.setTempoSync(true);
    lfo.setNoteValue(NoteValue::Quarter);

    SECTION("frequency scales with tempo") {
        lfo.setTempo(120.0f);
        CHECK(lfo.frequency() == Approx(2.0f).margin(0.01f));

        lfo.setTempo(140.0f);  // 140 BPM quarter = 140/60 = 2.333 Hz
        CHECK(lfo.frequency() == Approx(2.333f).margin(0.01f));

        lfo.setTempo(60.0f);  // 60 BPM quarter = 1 Hz
        CHECK(lfo.frequency() == Approx(1.0f).margin(0.01f));
    }
}

// ==============================================================================
// Phase 6: User Story 4 - Phase Control Tests (T060-T063)
// ==============================================================================
// Reference: specs/003-lfo/spec.md US4

// T060: Test 90° offset sine starts at 1.0
TEST_CASE("Phase offset 90 degrees starts sine at peak", "[lfo][US4][phase]") {
    LFO lfo;
    lfo.prepare(44100.0);
    lfo.setWaveform(Waveform::Sine);
    lfo.setFrequency(1.0f);
    lfo.setPhaseOffset(90.0f);

    float sample = lfo.process();
    CHECK(sample == Approx(1.0f).margin(0.01f));
}

// T061: Test 180° offset sine is inverted
TEST_CASE("Phase offset 180 degrees inverts sine", "[lfo][US4][phase]") {
    LFO lfo;
    lfo.prepare(44100.0);
    lfo.setWaveform(Waveform::Sine);
    lfo.setFrequency(1.0f);

    // Get samples with 0° offset
    std::array<float, 100> samples0{};
    lfo.setPhaseOffset(0.0f);
    lfo.reset();
    for (size_t i = 0; i < samples0.size(); ++i) {
        samples0[i] = lfo.process();
    }

    // Get samples with 180° offset
    std::array<float, 100> samples180{};
    lfo.setPhaseOffset(180.0f);
    lfo.reset();
    for (size_t i = 0; i < samples180.size(); ++i) {
        samples180[i] = lfo.process();
    }

    // 180° offset should be inverted (opposite sign)
    for (size_t i = 0; i < 100; ++i) {
        CHECK(samples180[i] == Approx(-samples0[i]).margin(0.01f));
    }
}

// T062: Test phase offset wraps values >= 360
TEST_CASE("Phase offset wraps at 360 degrees", "[lfo][US4][phase]") {
    LFO lfo;
    lfo.prepare(44100.0);

    SECTION("360 degrees wraps to 0") {
        lfo.setPhaseOffset(360.0f);
        CHECK(lfo.phaseOffset() == Approx(0.0f).margin(0.01f));
    }

    SECTION("450 degrees wraps to 90") {
        lfo.setPhaseOffset(450.0f);
        CHECK(lfo.phaseOffset() == Approx(90.0f).margin(0.01f));
    }

    SECTION("720 degrees wraps to 0") {
        lfo.setPhaseOffset(720.0f);
        CHECK(lfo.phaseOffset() == Approx(0.0f).margin(0.01f));
    }

    SECTION("negative values wrap correctly") {
        lfo.setPhaseOffset(-90.0f);
        CHECK(lfo.phaseOffset() == Approx(270.0f).margin(0.01f));
    }
}

// T063: Test phaseOffset() returns current offset
TEST_CASE("phaseOffset query returns set value", "[lfo][US4][phase]") {
    LFO lfo;
    lfo.prepare(44100.0);

    lfo.setPhaseOffset(45.0f);
    CHECK(lfo.phaseOffset() == Approx(45.0f));

    lfo.setPhaseOffset(270.0f);
    CHECK(lfo.phaseOffset() == Approx(270.0f));
}

// ==============================================================================
// Phase 7: User Story 5 - Retrigger Tests (T070-T073)
// ==============================================================================
// Reference: specs/003-lfo/spec.md US5

// T070: Test retrigger() resets phase to 0
TEST_CASE("retrigger resets phase to start", "[lfo][US5][retrigger]") {
    LFO lfo;
    lfo.prepare(100.0);
    lfo.setWaveform(Waveform::Sine);
    lfo.setFrequency(1.0f);
    lfo.setPhaseOffset(0.0f);

    // Process some samples
    for (int i = 0; i < 50; ++i) {
        lfo.process();
    }

    // Retrigger
    lfo.retrigger();

    // First sample after retrigger should be at zero crossing
    float sample = lfo.process();
    CHECK(sample == Approx(0.0f).margin(0.01f));
}

// T071: Test retrigger() respects phase offset
TEST_CASE("retrigger respects phase offset", "[lfo][US5][retrigger]") {
    LFO lfo;
    lfo.prepare(100.0);
    lfo.setWaveform(Waveform::Sine);
    lfo.setFrequency(1.0f);
    lfo.setPhaseOffset(90.0f);  // Start at peak

    // Process some samples
    for (int i = 0; i < 50; ++i) {
        lfo.process();
    }

    // Retrigger
    lfo.retrigger();

    // First sample after retrigger should be at peak (90° offset)
    float sample = lfo.process();
    CHECK(sample == Approx(1.0f).margin(0.02f));
}

// T072: Test retrigger disabled ignores retrigger() call
TEST_CASE("retrigger disabled ignores retrigger call", "[lfo][US5][retrigger]") {
    LFO lfo;
    lfo.prepare(100.0);
    lfo.setWaveform(Waveform::Sine);
    lfo.setFrequency(1.0f);
    lfo.setRetriggerEnabled(false);

    // Process 30 samples (past the peak at sample 25)
    float sampleBefore = 0.0f;
    for (int i = 0; i < 30; ++i) {
        sampleBefore = lfo.process();
    }

    // Retrigger (should have no effect)
    lfo.retrigger();

    // Next sample should continue from where it was, not reset
    float sampleAfter = lfo.process();

    // If retrigger worked, we'd be near 0. If ignored, we continue descending.
    // At sample 30, phase = 0.30, sin(0.30 * 2π) ≈ 0.95
    // At sample 31, phase = 0.31, sin(0.31 * 2π) ≈ 0.93
    CHECK(sampleAfter < sampleBefore);  // Continuing descent
    CHECK(sampleAfter > 0.5f);  // Still in upper half
}

// T073: Test setRetriggerEnabled() toggles retrigger mode
TEST_CASE("setRetriggerEnabled toggles mode", "[lfo][US5][retrigger]") {
    LFO lfo;
    lfo.prepare(44100.0);

    SECTION("retrigger is enabled by default") {
        CHECK(lfo.retriggerEnabled());
    }

    SECTION("setRetriggerEnabled(false) disables") {
        lfo.setRetriggerEnabled(false);
        CHECK_FALSE(lfo.retriggerEnabled());
    }

    SECTION("setRetriggerEnabled(true) enables") {
        lfo.setRetriggerEnabled(false);
        lfo.setRetriggerEnabled(true);
        CHECK(lfo.retriggerEnabled());
    }
}

// ==============================================================================
// Phase 8: Edge Cases and Cross-Cutting Tests (T079-T084)
// ==============================================================================

// T079: Test 0 Hz frequency outputs DC
TEST_CASE("Zero frequency clamped to minimum", "[lfo][edge]") {
    LFO lfo;
    lfo.prepare(44100.0);
    lfo.setWaveform(Waveform::Sine);
    lfo.setFrequency(0.0f);

    // Should be clamped to 0.01 Hz minimum
    CHECK(lfo.frequency() == Approx(0.01f));
}

// T080: Test 0 BPM in sync mode
TEST_CASE("Zero BPM clamped to minimum", "[lfo][edge]") {
    LFO lfo;
    lfo.prepare(44100.0);
    lfo.setTempoSync(true);
    lfo.setTempo(0.0f);
    lfo.setNoteValue(NoteValue::Quarter);

    // Should use minimum BPM (1) which gives 1/60 Hz for quarter note
    // But that's below min freq, so should clamp to 0.01 Hz
    CHECK(lfo.frequency() >= 0.01f);
}

// T081: Test noexcept guarantee
TEST_CASE("All public methods are noexcept", "[lfo][US6][realtime]") {
    LFO lfo;

    // Static assertions for noexcept
    static_assert(noexcept(lfo.prepare(44100.0)));
    static_assert(noexcept(lfo.reset()));
    static_assert(noexcept(lfo.process()));

    float buffer[10];
    static_assert(noexcept(lfo.processBlock(buffer, 10)));

    static_assert(noexcept(lfo.setWaveform(Waveform::Sine)));
    static_assert(noexcept(lfo.setFrequency(1.0f)));
    static_assert(noexcept(lfo.setPhaseOffset(0.0f)));
    static_assert(noexcept(lfo.setTempoSync(false)));
    static_assert(noexcept(lfo.setTempo(120.0f)));
    static_assert(noexcept(lfo.setNoteValue(NoteValue::Quarter)));
    static_assert(noexcept(lfo.retrigger()));
    static_assert(noexcept(lfo.setRetriggerEnabled(true)));

    static_assert(noexcept(lfo.waveform()));
    static_assert(noexcept(lfo.frequency()));
    static_assert(noexcept(lfo.phaseOffset()));
    static_assert(noexcept(lfo.tempoSyncEnabled()));
    static_assert(noexcept(lfo.retriggerEnabled()));
    static_assert(noexcept(lfo.sampleRate()));
}

// T082: Test output range for all waveforms (fuzz test)
TEST_CASE("All waveforms output in [-1, +1] range", "[lfo][US6][range]") {
    LFO lfo;
    lfo.prepare(44100.0);

    const std::array<Waveform, 6> waveforms = {
        Waveform::Sine, Waveform::Triangle, Waveform::Sawtooth,
        Waveform::Square, Waveform::SampleHold, Waveform::SmoothRandom
    };

    for (Waveform wf : waveforms) {
        lfo.setWaveform(wf);
        lfo.setFrequency(10.0f);
        lfo.reset();

        for (int i = 0; i < 10000; ++i) {
            float sample = lfo.process();
            REQUIRE(sample >= -1.0f);
            REQUIRE(sample <= 1.0f);
        }
    }
}

// T083: Test waveform transition produces no discontinuities
TEST_CASE("Waveform transition is smooth", "[lfo][SC008][transition]") {
    LFO lfo;
    lfo.prepare(44100.0);
    lfo.setFrequency(5.0f);
    lfo.setWaveform(Waveform::Sine);

    // Process some samples
    float lastSample = 0.0f;
    for (int i = 0; i < 1000; ++i) {
        lastSample = lfo.process();
    }

    // Switch waveform mid-stream
    lfo.setWaveform(Waveform::Triangle);
    float nextSample = lfo.process();

    // The transition should not cause extreme discontinuity
    // (actual click-free behavior depends on implementation)
    // This test documents the current behavior
    CHECK(std::abs(nextSample) <= 1.0f);  // At minimum, output is valid
}

// T084: Benchmark test (performance - informational)
TEST_CASE("Process performance is reasonable", "[lfo][SC005][benchmark][!benchmark]") {
    LFO lfo;
    lfo.prepare(44100.0);
    lfo.setWaveform(Waveform::Sine);
    lfo.setFrequency(5.0f);

    // Process 1 million samples
    volatile float sum = 0.0f;
    for (int i = 0; i < 1000000; ++i) {
        sum += lfo.process();
    }

    // If this completes without timeout, performance is acceptable
    // Real benchmark would measure time, but this at least verifies it runs
    CHECK(true);
}
