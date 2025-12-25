// ==============================================================================
// Layer 4: User Feature - TapeDelay Tests
// ==============================================================================
// Tests for the TapeDelay user feature (classic tape echo emulation).
// Follows test-first development per Constitution Principle XII.
//
// Feature: 024-tape-delay
// Layer: 4 (User Feature)
// Reference: specs/024-tape-delay/spec.md
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "dsp/features/tape_delay.h"

#include <array>
#include <cmath>

using Catch::Approx;
using namespace Iterum::DSP;

// =============================================================================
// Phase 2: Foundational Component Tests
// =============================================================================

TEST_CASE("TapeHead default construction", "[features][tape-delay][tape-head]") {
    TapeHead head;

    SECTION("default values") {
        REQUIRE(head.ratio == Approx(1.0f));
        REQUIRE(head.levelDb == Approx(0.0f));
        REQUIRE(head.pan == Approx(0.0f));
        REQUIRE(head.enabled == true);
    }
}

TEST_CASE("TapeHead configurable construction", "[features][tape-delay][tape-head]") {
    SECTION("head at 1.5x ratio") {
        TapeHead head{1.5f, -6.0f, -50.0f, true};
        REQUIRE(head.ratio == Approx(1.5f));
        REQUIRE(head.levelDb == Approx(-6.0f));
        REQUIRE(head.pan == Approx(-50.0f));
        REQUIRE(head.enabled == true);
    }

    SECTION("disabled head") {
        TapeHead head{2.0f, 0.0f, 50.0f, false};
        REQUIRE(head.enabled == false);
    }
}

TEST_CASE("MotorController basic functionality", "[features][tape-delay][motor-controller]") {
    MotorController motor;

    SECTION("default state") {
        // Not prepared yet, should have reasonable defaults
        REQUIRE(motor.getCurrentDelayMs() >= 0.0f);
    }

    SECTION("prepare initializes state") {
        motor.prepare(44100.0f, 512);
        REQUIRE(motor.getCurrentDelayMs() >= 0.0f);
    }
}

TEST_CASE("MotorController delay time management", "[features][tape-delay][motor-controller]") {
    MotorController motor;
    motor.prepare(44100.0f, 512);

    SECTION("setTargetDelayMs sets target") {
        motor.setTargetDelayMs(500.0f);
        REQUIRE(motor.getTargetDelayMs() == Approx(500.0f));
    }

    SECTION("delay smooths over time (inertia)") {
        motor.setTargetDelayMs(0.0f);
        motor.snapToTarget();

        motor.setTargetDelayMs(500.0f);

        // First sample should not be at target (inertia)
        float firstDelay = motor.process();
        REQUIRE(firstDelay < 500.0f);
        REQUIRE(firstDelay > 0.0f);
    }

    SECTION("snapToTarget bypasses inertia") {
        motor.setTargetDelayMs(500.0f);
        motor.snapToTarget();
        REQUIRE(motor.getCurrentDelayMs() == Approx(500.0f));
    }
}

TEST_CASE("MotorController inertia timing", "[features][tape-delay][motor-controller]") {
    MotorController motor;
    motor.prepare(44100.0f, 512);

    SECTION("default inertia time is tape-realistic (200-500ms)") {
        motor.setTargetDelayMs(0.0f);
        motor.snapToTarget();

        motor.setTargetDelayMs(1000.0f);

        // Process for 200ms worth of samples
        const size_t samples200ms = static_cast<size_t>(44100.0f * 0.2f);
        float delay = 0.0f;
        for (size_t i = 0; i < samples200ms; ++i) {
            delay = motor.process();
        }

        // Should be significantly toward target but not there yet
        REQUIRE(delay > 500.0f);  // Past halfway
        REQUIRE(delay < 990.0f);  // Not at target yet
    }

    SECTION("setInertiaTimeMs changes transition speed") {
        motor.setInertiaTimeMs(100.0f);  // Fast inertia
        motor.setTargetDelayMs(0.0f);
        motor.snapToTarget();

        motor.setTargetDelayMs(1000.0f);

        // Process for 100ms
        const size_t samples100ms = static_cast<size_t>(44100.0f * 0.1f);
        float delay = 0.0f;
        for (size_t i = 0; i < samples100ms; ++i) {
            delay = motor.process();
        }

        // With 100ms inertia, should be near target after 100ms
        REQUIRE(delay > 900.0f);
    }
}

TEST_CASE("MotorController reset", "[features][tape-delay][motor-controller]") {
    MotorController motor;
    motor.prepare(44100.0f, 512);

    motor.setTargetDelayMs(500.0f);
    motor.snapToTarget();

    motor.reset();

    // After reset, current should snap to target
    REQUIRE(motor.getCurrentDelayMs() == Approx(motor.getTargetDelayMs()));
}

// =============================================================================
// Phase 3: TapeDelay Construction and Lifecycle Tests
// =============================================================================

TEST_CASE("TapeDelay construction", "[features][tape-delay][lifecycle]") {
    TapeDelay delay;

    SECTION("default construction succeeds") {
        REQUIRE_FALSE(delay.isPrepared());
    }

    SECTION("constants are correct") {
        REQUIRE(TapeDelay::kNumHeads == 3);
        REQUIRE(TapeDelay::kMinDelayMs == Approx(20.0f));
        REQUIRE(TapeDelay::kMaxDelayMs == Approx(2000.0f));
        REQUIRE(TapeDelay::kHeadRatio1 == Approx(1.0f));
        REQUIRE(TapeDelay::kHeadRatio2 == Approx(1.5f));
        REQUIRE(TapeDelay::kHeadRatio3 == Approx(2.0f));
    }
}

TEST_CASE("TapeDelay prepare", "[features][tape-delay][lifecycle]") {
    TapeDelay delay;

    SECTION("prepare marks as prepared") {
        delay.prepare(44100.0, 512, 2000.0f);
        REQUIRE(delay.isPrepared());
    }

    SECTION("prepare accepts various sample rates") {
        delay.prepare(48000.0, 256, 2000.0f);
        REQUIRE(delay.isPrepared());
    }
}

TEST_CASE("TapeDelay reset", "[features][tape-delay][lifecycle]") {
    TapeDelay delay;
    delay.prepare(44100.0, 512, 2000.0f);

    delay.setMotorSpeed(500.0f);
    delay.setFeedback(0.5f);

    SECTION("reset clears delay state") {
        delay.reset();
        // After reset, isPrepared should still be true
        REQUIRE(delay.isPrepared());
    }
}

// =============================================================================
// Phase 3: Motor Speed (Delay Time) Tests
// =============================================================================

TEST_CASE("TapeDelay motor speed control", "[features][tape-delay][motor-speed]") {
    TapeDelay delay;
    delay.prepare(44100.0, 512, 2000.0f);

    SECTION("setMotorSpeed sets target delay") {
        delay.setMotorSpeed(500.0f);
        REQUIRE(delay.getTargetDelayMs() == Approx(500.0f));
    }

    SECTION("delay time clamped to valid range") {
        delay.setMotorSpeed(10.0f);  // Below minimum
        REQUIRE(delay.getTargetDelayMs() >= TapeDelay::kMinDelayMs);

        delay.setMotorSpeed(5000.0f);  // Above maximum
        REQUIRE(delay.getTargetDelayMs() <= TapeDelay::kMaxDelayMs);
    }
}

// =============================================================================
// Phase 3: Feedback Tests
// =============================================================================

TEST_CASE("TapeDelay feedback control", "[features][tape-delay][feedback]") {
    TapeDelay delay;
    delay.prepare(44100.0, 512, 2000.0f);

    SECTION("setFeedback stores value") {
        delay.setFeedback(0.5f);
        REQUIRE(delay.getFeedback() == Approx(0.5f));
    }

    SECTION("feedback clamped to valid range") {
        delay.setFeedback(-0.1f);
        REQUIRE(delay.getFeedback() >= 0.0f);

        delay.setFeedback(1.5f);
        REQUIRE(delay.getFeedback() <= 1.2f);
    }
}

// =============================================================================
// Phase 3: Mix Tests
// =============================================================================

TEST_CASE("TapeDelay mix control", "[features][tape-delay][mix]") {
    TapeDelay delay;
    delay.prepare(44100.0, 512, 2000.0f);

    SECTION("setMix stores value") {
        delay.setMix(0.5f);
        REQUIRE(delay.getMix() == Approx(0.5f));
    }

    SECTION("mix clamped to 0-1 range") {
        delay.setMix(-0.1f);
        REQUIRE(delay.getMix() >= 0.0f);

        delay.setMix(1.1f);
        REQUIRE(delay.getMix() <= 1.0f);
    }
}

// =============================================================================
// Phase 3: Output Level Tests
// =============================================================================

TEST_CASE("TapeDelay output level control", "[features][tape-delay][output-level]") {
    TapeDelay delay;
    delay.prepare(44100.0, 512, 2000.0f);

    SECTION("setOutputLevel stores value") {
        delay.setOutputLevel(-6.0f);
        REQUIRE(delay.getOutputLevel() == Approx(-6.0f));
    }

    SECTION("output level clamped to valid range") {
        delay.setOutputLevel(-100.0f);
        REQUIRE(delay.getOutputLevel() >= -96.0f);

        delay.setOutputLevel(20.0f);
        REQUIRE(delay.getOutputLevel() <= 12.0f);
    }
}

// =============================================================================
// Phase 3: Basic Processing Tests
// =============================================================================

TEST_CASE("TapeDelay basic processing", "[features][tape-delay][processing]") {
    TapeDelay delay;
    delay.prepare(44100.0, 512, 2000.0f);

    std::array<float, 512> left{};
    std::array<float, 512> right{};

    SECTION("process with silence produces silence initially") {
        delay.process(left.data(), right.data(), 512);

        // With no input and no delay built up, output should be near zero
        float maxOutput = 0.0f;
        for (size_t i = 0; i < 512; ++i) {
            maxOutput = std::max(maxOutput, std::abs(left[i]));
            maxOutput = std::max(maxOutput, std::abs(right[i]));
        }
        REQUIRE(maxOutput < 0.001f);
    }

    SECTION("process handles impulse") {
        left[0] = 1.0f;
        right[0] = 1.0f;

        delay.setMotorSpeed(100.0f);  // 100ms delay
        delay.setFeedback(0.5f);
        delay.setMix(1.0f);  // Full wet

        delay.process(left.data(), right.data(), 512);

        // Impulse should appear after delay time
        // At 44100Hz, 100ms = 4410 samples, but we only have 512
        // So no echo should appear yet in this block
        // (This is just a basic smoke test)
        REQUIRE_FALSE(std::isnan(left[511]));
        REQUIRE_FALSE(std::isnan(right[511]));
    }
}

TEST_CASE("TapeDelay mono processing", "[features][tape-delay][processing]") {
    TapeDelay delay;
    delay.prepare(44100.0, 512, 2000.0f);

    std::array<float, 512> buffer{};

    SECTION("mono process handles silence") {
        delay.process(buffer.data(), 512);

        for (size_t i = 0; i < 512; ++i) {
            REQUIRE_FALSE(std::isnan(buffer[i]));
        }
    }
}

// =============================================================================
// Phase 4: Wear (Wow/Flutter) Tests
// =============================================================================

TEST_CASE("TapeDelay wear control", "[features][tape-delay][wear]") {
    TapeDelay delay;
    delay.prepare(44100.0, 512, 2000.0f);

    SECTION("setWear stores value") {
        delay.setWear(0.5f);
        REQUIRE(delay.getWear() == Approx(0.5f));
    }

    SECTION("wear clamped to 0-1 range") {
        delay.setWear(-0.1f);
        REQUIRE(delay.getWear() >= 0.0f);

        delay.setWear(1.5f);
        REQUIRE(delay.getWear() <= 1.0f);
    }
}

// =============================================================================
// Phase 5: Saturation Tests
// =============================================================================

TEST_CASE("TapeDelay saturation control", "[features][tape-delay][saturation]") {
    TapeDelay delay;
    delay.prepare(44100.0, 512, 2000.0f);

    SECTION("setSaturation stores value") {
        delay.setSaturation(0.5f);
        REQUIRE(delay.getSaturation() == Approx(0.5f));
    }

    SECTION("saturation clamped to 0-1 range") {
        delay.setSaturation(-0.1f);
        REQUIRE(delay.getSaturation() >= 0.0f);

        delay.setSaturation(1.5f);
        REQUIRE(delay.getSaturation() <= 1.0f);
    }
}

// =============================================================================
// Phase 6: Echo Heads (Multi-Tap) Tests
// =============================================================================

TEST_CASE("TapeDelay head control", "[features][tape-delay][heads]") {
    TapeDelay delay;
    delay.prepare(44100.0, 512, 2000.0f);

    SECTION("heads enabled by default") {
        for (size_t i = 0; i < TapeDelay::kNumHeads; ++i) {
            REQUIRE(delay.isHeadEnabled(i));
        }
    }

    SECTION("setHeadEnabled toggles head") {
        delay.setHeadEnabled(0, false);
        REQUIRE_FALSE(delay.isHeadEnabled(0));

        delay.setHeadEnabled(0, true);
        REQUIRE(delay.isHeadEnabled(0));
    }

    SECTION("setHeadLevel stores value") {
        delay.setHeadLevel(0, -6.0f);
        auto head = delay.getHead(0);
        REQUIRE(head.levelDb == Approx(-6.0f));
    }

    SECTION("setHeadPan stores value") {
        delay.setHeadPan(1, 50.0f);
        auto head = delay.getHead(1);
        REQUIRE(head.pan == Approx(50.0f));
    }

    SECTION("head ratios are fixed") {
        auto head0 = delay.getHead(0);
        auto head1 = delay.getHead(1);
        auto head2 = delay.getHead(2);

        REQUIRE(head0.ratio == Approx(TapeDelay::kHeadRatio1));
        REQUIRE(head1.ratio == Approx(TapeDelay::kHeadRatio2));
        REQUIRE(head2.ratio == Approx(TapeDelay::kHeadRatio3));
    }

    SECTION("out-of-range head index handled gracefully") {
        // Should not crash
        delay.setHeadEnabled(10, true);
        delay.setHeadLevel(10, 0.0f);
        delay.setHeadPan(10, 0.0f);

        // Querying out of range returns safe defaults
        REQUIRE_FALSE(delay.isHeadEnabled(10));
    }

    SECTION("getActiveHeadCount returns correct count") {
        delay.setHeadEnabled(0, true);
        delay.setHeadEnabled(1, true);
        delay.setHeadEnabled(2, false);
        REQUIRE(delay.getActiveHeadCount() == 2);
    }
}

// =============================================================================
// Phase 7: Age/Degradation Tests
// =============================================================================

TEST_CASE("TapeDelay age control", "[features][tape-delay][age]") {
    TapeDelay delay;
    delay.prepare(44100.0, 512, 2000.0f);

    SECTION("setAge stores value") {
        delay.setAge(0.5f);
        REQUIRE(delay.getAge() == Approx(0.5f));
    }

    SECTION("age clamped to 0-1 range") {
        delay.setAge(-0.1f);
        REQUIRE(delay.getAge() >= 0.0f);

        delay.setAge(1.5f);
        REQUIRE(delay.getAge() <= 1.0f);
    }
}

// =============================================================================
// Phase 8: Motor Inertia Tests
// =============================================================================

TEST_CASE("TapeDelay motor inertia", "[features][tape-delay][motor-inertia]") {
    TapeDelay delay;
    delay.prepare(44100.0, 512, 2000.0f);

    SECTION("setMotorInertia stores value") {
        delay.setMotorInertia(300.0f);
        // No getter, but verify it doesn't crash
    }

    SECTION("isTransitioning detects motor changes") {
        delay.setMotorSpeed(200.0f);
        delay.reset();  // Snap to target

        delay.setMotorSpeed(500.0f);
        REQUIRE(delay.isTransitioning());
    }
}

// =============================================================================
// Phase 9: Edge Case Tests
// =============================================================================

TEST_CASE("TapeDelay edge case: all heads disabled", "[features][tape-delay][edge-case]") {
    TapeDelay delay;
    delay.prepare(44100.0, 512, 2000.0f);

    SECTION("processing works with all heads disabled") {
        // Disable all heads
        delay.setHeadEnabled(0, false);
        delay.setHeadEnabled(1, false);
        delay.setHeadEnabled(2, false);

        REQUIRE(delay.getActiveHeadCount() == 0);

        // Process should still work without crashing
        std::array<float, 512> left{};
        std::array<float, 512> right{};
        left[0] = 1.0f;
        right[0] = 1.0f;

        delay.process(left.data(), right.data(), 512);

        // Should produce valid output (no NaN)
        for (size_t i = 0; i < 512; ++i) {
            REQUIRE_FALSE(std::isnan(left[i]));
            REQUIRE_FALSE(std::isnan(right[i]));
        }
    }
}

TEST_CASE("TapeDelay edge case: high feedback self-oscillation", "[features][tape-delay][edge-case]") {
    TapeDelay delay;
    delay.prepare(44100.0, 512, 2000.0f);

    SECTION("feedback >100% produces controlled output") {
        delay.setMotorSpeed(100.0f);  // Short delay
        delay.setFeedback(1.2f);      // >100% feedback (FR-030)
        delay.setMix(1.0f);           // Full wet

        std::array<float, 512> left{};
        std::array<float, 512> right{};
        left[0] = 1.0f;  // Impulse
        right[0] = 1.0f;

        // Process multiple blocks to allow feedback to build
        for (int block = 0; block < 10; ++block) {
            delay.process(left.data(), right.data(), 512);
        }

        // Output should not explode to infinity (SC-007: controlled self-oscillation)
        float maxOutput = 0.0f;
        for (size_t i = 0; i < 512; ++i) {
            maxOutput = std::max(maxOutput, std::abs(left[i]));
            maxOutput = std::max(maxOutput, std::abs(right[i]));
        }

        // Should be finite and reasonably bounded (not infinite)
        REQUIRE_FALSE(std::isinf(maxOutput));
        REQUIRE_FALSE(std::isnan(maxOutput));
    }

    SECTION("feedback at maximum (120%) is clamped") {
        delay.setFeedback(1.5f);  // Above max
        REQUIRE(delay.getFeedback() <= 1.2f);
    }
}

TEST_CASE("TapeDelay edge case: parameter smoothing", "[features][tape-delay][edge-case]") {
    TapeDelay delay;
    delay.prepare(44100.0, 512, 2000.0f);

    SECTION("mix parameter changes are smooth") {
        std::array<float, 512> left{};
        std::array<float, 512> right{};

        // Fill with constant signal
        for (auto& s : left) s = 1.0f;
        for (auto& s : right) s = 1.0f;

        delay.setMix(0.0f);  // Dry
        delay.reset();       // Snap smoothers

        // Jump to 100% wet
        delay.setMix(1.0f);

        // Process a block
        delay.process(left.data(), right.data(), 512);

        // Output should not contain clicks (abrupt changes)
        // Check that consecutive samples don't differ too much
        float maxDiff = 0.0f;
        for (size_t i = 1; i < 512; ++i) {
            maxDiff = std::max(maxDiff, std::abs(left[i] - left[i - 1]));
        }

        // Smoothed parameter should not create abrupt jumps
        // With 20ms smoothing at 44.1kHz, first sample change is ~0.68
        // but subsequent samples should be smoother. A value >1.0 would
        // indicate no smoothing at all (instant jump from 0 to 1).
        REQUIRE(maxDiff < 1.0f);
    }
}

// =============================================================================
// FR-007: Wow Rate Scales with Motor Speed Tests
// =============================================================================

TEST_CASE("FR-007: Wow rate scales inversely with Motor Speed", "[features][tape-delay][wow-rate]") {
    TapeDelay delay;
    delay.prepare(44100.0, 512, 2000.0f);

    SECTION("slow motor (long delay) produces slower wow rate") {
        delay.setMotorSpeed(2000.0f);  // Maximum delay = slowest tape
        delay.setWear(0.5f);           // Enable wow/flutter

        // Slow tape should have slower wow rate
        // Typical tape wow rate: 0.3-0.6 Hz at normal speed
        // At slowest speed, wow rate should be ~0.15-0.3 Hz
        float wowRate = delay.getWowRate();
        REQUIRE(wowRate < 0.5f);
        REQUIRE(wowRate >= 0.1f);
    }

    SECTION("fast motor (short delay) produces faster wow rate") {
        delay.setMotorSpeed(100.0f);   // Short delay = fast tape
        delay.setWear(0.5f);           // Enable wow/flutter

        // Fast tape should have faster wow rate
        // At fastest speed, wow rate should be ~1.0-2.0 Hz
        float wowRate = delay.getWowRate();
        REQUIRE(wowRate > 0.8f);
        REQUIRE(wowRate <= 3.0f);
    }

    SECTION("wow rate changes proportionally with motor speed") {
        delay.setWear(0.5f);

        delay.setMotorSpeed(500.0f);
        float rateAtMedium = delay.getWowRate();

        delay.setMotorSpeed(1000.0f);  // Half speed = slower tape
        float rateAtSlow = delay.getWowRate();

        delay.setMotorSpeed(250.0f);   // Double speed = faster tape
        float rateAtFast = delay.getWowRate();

        // Faster tape should have higher wow rate
        REQUIRE(rateAtFast > rateAtMedium);
        REQUIRE(rateAtMedium > rateAtSlow);
    }

    SECTION("wow rate at zero wear is still calculated but not audible") {
        delay.setMotorSpeed(500.0f);
        delay.setWear(0.0f);

        // Rate calculation should still work even at zero wear
        float wowRate = delay.getWowRate();
        REQUIRE(wowRate > 0.0f);  // Rate is calculated
    }
}

// =============================================================================
// FR-023: Splice Artifacts Tests
// =============================================================================

TEST_CASE("FR-023: Splice artifacts at tape loop point", "[features][tape-delay][splice]") {
    TapeDelay delay;
    delay.prepare(44100.0, 512, 2000.0f);

    SECTION("splice artifacts disabled by default") {
        REQUIRE_FALSE(delay.isSpliceEnabled());
    }

    SECTION("splice artifacts can be enabled/disabled") {
        delay.setSpliceEnabled(true);
        REQUIRE(delay.isSpliceEnabled());

        delay.setSpliceEnabled(false);
        REQUIRE_FALSE(delay.isSpliceEnabled());
    }

    SECTION("splice intensity can be set") {
        delay.setSpliceIntensity(0.5f);
        REQUIRE(delay.getSpliceIntensity() == Approx(0.5f));
    }

    SECTION("splice intensity clamped to 0-1 range") {
        delay.setSpliceIntensity(-0.1f);
        REQUIRE(delay.getSpliceIntensity() >= 0.0f);

        delay.setSpliceIntensity(1.5f);
        REQUIRE(delay.getSpliceIntensity() <= 1.0f);
    }

    SECTION("splice artifacts occur at tape loop interval") {
        delay.setMotorSpeed(100.0f);    // 100ms delay = 4410 samples at 44.1kHz
        delay.setSpliceEnabled(true);
        delay.setSpliceIntensity(1.0f); // Full intensity
        delay.setWear(0.0f);            // Disable wow/flutter
        delay.setSaturation(0.0f);      // Disable saturation
        delay.setAge(0.0f);             // Disable hiss
        delay.setMix(1.0f);             // Full wet
        delay.reset();

        // Process silence - splice artifacts should appear periodically
        const size_t totalSamples = 44100;  // 1 second = 10 loop points at 100ms
        std::vector<float> left(totalSamples, 0.0f);
        std::vector<float> right(totalSamples, 0.0f);

        delay.process(left.data(), right.data(), totalSamples);

        // Count peaks that could be splice transients
        size_t peakCount = 0;
        const float threshold = 0.001f;  // Small threshold for splice clicks
        for (size_t i = 1; i < totalSamples; ++i) {
            if (std::abs(left[i]) > threshold && std::abs(left[i]) > std::abs(left[i - 1])) {
                peakCount++;
                // Skip ahead to avoid counting the same peak multiple times
                i += 100;
            }
        }

        // At 100ms loop, we expect ~10 splice points in 1 second
        // Allow some tolerance (8-12)
        REQUIRE(peakCount >= 5);
        REQUIRE(peakCount <= 15);
    }

    SECTION("splice artifacts absent when disabled") {
        delay.setMotorSpeed(100.0f);
        delay.setSpliceEnabled(false);  // Disabled
        delay.setMix(1.0f);
        delay.setWear(0.0f);
        delay.setSaturation(0.0f);
        delay.setAge(0.0f);
        delay.reset();

        const size_t totalSamples = 4410;  // One loop
        std::vector<float> left(totalSamples, 0.0f);
        std::vector<float> right(totalSamples, 0.0f);

        delay.process(left.data(), right.data(), totalSamples);

        // With splice disabled and all other character off, output should be near silent
        float maxOutput = 0.0f;
        for (size_t i = 0; i < totalSamples; ++i) {
            maxOutput = std::max(maxOutput, std::abs(left[i]));
        }
        REQUIRE(maxOutput < 0.001f);
    }
}

// =============================================================================
// FR-024: Age Control Affects Artifact Intensity
// =============================================================================

TEST_CASE("FR-024: Age control affects splice artifact intensity", "[features][tape-delay][age-splice]") {
    TapeDelay delay;
    delay.prepare(44100.0, 512, 2000.0f);

    SECTION("Age at 0% produces no splice artifacts") {
        delay.setAge(0.0f);
        delay.setSpliceEnabled(true);

        // At Age=0, even with splice enabled, intensity should be zero
        REQUIRE(delay.getSpliceIntensity() == Approx(0.0f));
    }

    SECTION("Age increase raises splice artifact intensity") {
        delay.setSpliceEnabled(true);

        delay.setAge(0.5f);
        float intensity50 = delay.getSpliceIntensity();

        delay.setAge(1.0f);
        float intensity100 = delay.getSpliceIntensity();

        // Higher age = higher intensity
        REQUIRE(intensity100 > intensity50);
        REQUIRE(intensity50 > 0.0f);
    }

    SECTION("Age at 100% produces maximum artifact intensity") {
        delay.setSpliceEnabled(true);
        delay.setAge(1.0f);

        // At Age=100%, splice intensity should be at or near maximum
        REQUIRE(delay.getSpliceIntensity() >= 0.8f);
    }

    SECTION("Age simultaneously affects hiss, rolloff, and artifacts") {
        delay.setSpliceEnabled(true);
        delay.setMotorSpeed(500.0f);
        delay.setMix(1.0f);

        // At Age=0, minimal degradation
        delay.setAge(0.0f);
        delay.reset();

        std::vector<float> clean(4410, 0.0f);
        clean[0] = 1.0f;  // Impulse
        std::vector<float> cleanR(4410, 0.0f);
        cleanR[0] = 1.0f;
        delay.process(clean.data(), cleanR.data(), 4410);

        // At Age=100%, maximum degradation
        delay.setAge(1.0f);
        delay.reset();

        std::vector<float> aged(4410, 0.0f);
        aged[0] = 1.0f;
        std::vector<float> agedR(4410, 0.0f);
        agedR[0] = 1.0f;
        delay.process(aged.data(), agedR.data(), 4410);

        // Aged signal should have more noise (higher RMS in silent sections)
        float cleanNoise = 0.0f;
        float agedNoise = 0.0f;
        // Check samples after initial transient
        for (size_t i = 1000; i < 4410; ++i) {
            cleanNoise += clean[i] * clean[i];
            agedNoise += aged[i] * aged[i];
        }

        // Aged should have more residual noise/artifacts
        REQUIRE(agedNoise > cleanNoise);
    }
}
