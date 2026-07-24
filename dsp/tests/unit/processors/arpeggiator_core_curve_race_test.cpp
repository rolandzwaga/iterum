// ==============================================================================
// Speed-curve table handoff must not tear (GMF-012)
// ==============================================================================
// setLaneSpeedCurveTable() runs on the message thread (a UI drag on the
// speed-curve editor fires ~30-60 IMessages/s, and a preset load fires one per
// lane) while consumePendingCurveTables() runs on the audio thread at the top of
// every process(). With a SINGLE staging buffer per lane the release/acquire on
// the dirty flag serialized exactly one write->read handoff, but nothing stopped
// a second message-thread write from landing in the middle of the audio thread's
// 1 KB copy of that same buffer. That is a data race -- undefined behaviour on
// the audio thread -- and a relaxed `dirty = false` landing after a racing
// `dirty = true` could persist the torn table across subsequent blocks.
//
// The fix is a real double buffer: the writer always targets the slot the
// published atomic index does NOT name, so reader and writer never touch the
// same memory.
//
// Detection: each published table is uniform (all 0.0 or all 1.0), so a torn
// active table contains BOTH values. Aligned 32-bit float stores mean the
// mixing assertion can pass by luck on the single-buffer version -- a clean
// ThreadSanitizer/valgrind report is the authoritative signal, and the race is
// confirmed from source regardless. This test is the portable proxy and also
// pins that the handoff still delivers updates at all.
// ==============================================================================

#include "arpeggiator_core_test_helpers.h"

#include <atomic>
#include <thread>

TEST_CASE("GMF-012: concurrent curve-table publish never yields a torn active table",
          "[ArpeggiatorCore][rtsafety][curve]")
{
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);

    std::array<float, 256> tableA{};
    std::array<float, 256> tableB{};
    tableA.fill(0.0f);
    tableB.fill(1.0f);

    constexpr size_t kLane = 0;
    constexpr int kIterations = 20000;

    std::atomic<bool> stop{false};
    std::atomic<int> tornTables{0};
    std::atomic<int> consumed{0};

    std::thread writer([&] {
        for (int i = 0; i < kIterations && !stop.load(std::memory_order_relaxed); ++i) {
            arp.setLaneSpeedCurveTable(kLane, (i % 2) == 0 ? tableA : tableB);
        }
        stop.store(true, std::memory_order_relaxed);
    });

    // Audio-thread role: consume, then inspect the active table. Every entry
    // must equal the first one -- a mix of 0.0 and 1.0 means the copy read a
    // buffer that was being rewritten underneath it.
    while (!stop.load(std::memory_order_relaxed)) {
        arp.consumePendingCurveTables();
        const auto& active = arp.laneSpeedCurveTable(kLane);
        const float first = active[0];
        for (float v : active) {
            if (v != first) {
                tornTables.fetch_add(1, std::memory_order_relaxed);
                break;
            }
        }
        consumed.fetch_add(1, std::memory_order_relaxed);
    }
    writer.join();

    // Drain whatever the writer published last so the handoff is proven to work.
    arp.consumePendingCurveTables();

    INFO("consume passes: " << consumed.load()
         << ", torn tables observed: " << tornTables.load());
    CHECK(tornTables.load() == 0);

    const auto& finalTable = arp.laneSpeedCurveTable(kLane);
    INFO("final active table[0] = " << finalTable[0]);
    CHECK((finalTable[0] == 0.0f || finalTable[0] == 1.0f));
    for (float v : finalTable) {
        CHECK(v == finalTable[0]);
    }
}

TEST_CASE("GMF-012: staged curve table reaches the active table",
          "[ArpeggiatorCore][curve]")
{
    ArpeggiatorCore arp;
    arp.prepare(44100.0, 512);

    std::array<float, 256> ramp{};
    for (size_t i = 0; i < ramp.size(); ++i)
        ramp[i] = static_cast<float>(i) / 255.0f;

    // Publish twice in a row without an intervening consume: the second publish
    // must be the one that lands (it targets the other slot, then republishes).
    std::array<float, 256> ones{};
    ones.fill(1.0f);
    arp.setLaneSpeedCurveTable(3, ones);
    arp.setLaneSpeedCurveTable(3, ramp);
    arp.consumePendingCurveTables();

    const auto& active = arp.laneSpeedCurveTable(3);
    for (size_t i = 0; i < ramp.size(); ++i) {
        INFO("entry " << i);
        REQUIRE(active[i] == ramp[i]);
    }
}
