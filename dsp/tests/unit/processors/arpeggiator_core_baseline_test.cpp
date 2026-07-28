// arpeggiator_core_baseline_test.cpp
// SC-002 baseline fixture generation (spec 073)
// Split from the former 17k-line arpeggiator_core_test.cpp (D1). Shared helpers in
// arpeggiator_core_test_helpers.h.
#include "arpeggiator_core_test_helpers.h"



TEST_CASE("ArpeggiatorCore: generate SC-002 baseline fixtures",
          "[processors][arpeggiator_core][fixture_gen]") {
    // Generate baseline fixtures at 3 BPMs
    const std::string basePath = std::string(KRATE_DSP_TESTS_DIR) + "/fixtures/";

    SECTION("120 BPM baseline") {
        size_t count = generateAndWriteBaseline(
            120.0, basePath + "arp_baseline_120bpm.dat");
        REQUIRE(count >= 1000);
        INFO("Generated " << count << " NoteOn events at 120 BPM");
    }

    SECTION("140 BPM baseline") {
        size_t count = generateAndWriteBaseline(
            140.0, basePath + "arp_baseline_140bpm.dat");
        REQUIRE(count >= 1000);
        INFO("Generated " << count << " NoteOn events at 140 BPM");
    }

    SECTION("180 BPM baseline") {
        size_t count = generateAndWriteBaseline(
            180.0, basePath + "arp_baseline_180bpm.dat");
        REQUIRE(count >= 1000);
        INFO("Generated " << count << " NoteOn events at 180 BPM");
    }
}


TEST_CASE("ArpeggiatorCore: verify SC-002 baseline fixtures are readable",
          "[processors][arpeggiator_core][fixture_verify]") {
    const std::string basePath = std::string(KRATE_DSP_TESTS_DIR) + "/fixtures/";

    SECTION("120 BPM fixture readable") {
        auto events = readBaselineFixture(basePath + "arp_baseline_120bpm.dat");
        REQUIRE(events.size() >= 1000);
        // Verify first event is C4 (note 60) with velocity 100
        REQUIRE(events[0].note == 60);
        REQUIRE(events[0].velocity == 100);
        // First NoteOn fires immediately at sample 0 (1/8 note at 120 BPM)
        REQUIRE(events[0].sampleOffset == 0);
    }

    SECTION("140 BPM fixture readable") {
        auto events = readBaselineFixture(basePath + "arp_baseline_140bpm.dat");
        REQUIRE(events.size() >= 1000);
        REQUIRE(events[0].note == 60);
    }

    SECTION("180 BPM fixture readable") {
        auto events = readBaselineFixture(basePath + "arp_baseline_180bpm.dat");
        REQUIRE(events.size() >= 1000);
        REQUIRE(events[0].note == 60);
    }
}
