// ==============================================================================
// Layer 2: Processor Tests - Vorago Phase 1 CSV trajectory harness (SC-015)
// ==============================================================================
// Spec:  specs/vorago-phase1-events-modulation/spec.md   (FR-081, FR-082, SC-015)
// Plan:  specs/vorago-phase1-events-modulation/plan.md   (section 4)
// Tasks: specs/vorago-phase1-events-modulation/tasks.md  (T013)
//
// This is the roadmap's "inspected for organic character" evaluation step, not
// an assertion of quality: it renders five reference trajectories to CSV so a
// human (or a plotting script) can look at them.
//
// OPT-IN ONLY (FR-082). The tag carries a LEADING DOT --
// "[.harness][processors][vorago]" -- so Catch2 hides the case from every
// default run and the suite performs no file I/O unless it is selected by name
// or by tag. Precedent: dsp/tests/unit/effects/aether_reverb_perf_test.cpp:976
// ("[.perf]"). FR-082 is therefore satisfied by the tag, not by a runtime flag.
//
//   build/windows-x64-release/bin/Release/dsp_processors_tests.exe "[.harness]"
//
// OUTPUT LOCATION (FR-081). VORAGO_P1_HARNESS_DIR is injected by
// dsp/tests/CMakeLists.txt:473-474 as "${CMAKE_BINARY_DIR}/vorago_p1/", so the
// CSVs land in the build tree regardless of the launch working directory (same
// rationale as KRATE_DSP_TESTS_DIR).
//
// NO ALLOCATION HARNESS HERE. This case allocates on purpose (streams, strings)
// and includes neither <allocation_detector.h> nor
// <allocation_operator_overrides.h>; brownian_drift_test.cpp:27-28 remains the
// single owner of the operator replacements in dsp_processors_tests.
//
// NO FLOAT GOLDENS. Nothing here is compared against a checked-in digest; the
// re-read pass asserts structure (header, parseability, finiteness, row count,
// covered duration) and event CONTENT, all with measured tolerances.
// ==============================================================================

#include <krate/dsp/processors/chaos_mod_source.h>
#include <krate/dsp/processors/perlin_noise_source.h>
#include <krate/dsp/processors/slow_event_scheduler.h>

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/primitives/chaos_waveshaper.h>  // ChaosModel

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <ios>
#include <string>

#ifndef VORAGO_P1_HARNESS_DIR
#  error "VORAGO_P1_HARNESS_DIR must be injected by dsp/tests/CMakeLists.txt"
#endif

using namespace Krate::DSP;

namespace {

// ------------------------------------------------------------------------------
// Render geometry
// ------------------------------------------------------------------------------

constexpr double kSampleRate = 48000.0;

/// All three sources decimate their control work by the same interval.
constexpr std::size_t kControlStep = 32u;
static_assert(PerlinNoiseSource::kControlRateInterval == 32u,
              "harness row cadence assumes a 32-sample control interval");
static_assert(ChaosModSource::kControlRateInterval == 32u,
              "harness row cadence assumes a 32-sample control interval");
static_assert(SlowEventScheduler::kControlRateInterval == 32,
              "harness row cadence assumes a 32-sample control interval");

/// One control step in seconds; also the duration tolerance (SC-015).
constexpr double kStepSeconds = static_cast<double>(kControlStep) / kSampleRate;

constexpr double kShortDurationSeconds = 60.0;   ///< Perlin + Aizawa (roadmap line 172)
constexpr double kEventDurationSeconds = 900.0;  ///< scheduler (FR-081 rationale)

/// 60 s / (32 samples @ 48 kHz) = 90 000 rows.
constexpr std::size_t kShortRows = 90000u;
/// 900 s / (32 samples @ 48 kHz) = 1 350 000 rows (~40 MB file - expected).
constexpr std::size_t kEventRows = 1350000u;

static_assert(kShortRows * kControlStep == 60u * 48000u, "short row count");
static_assert(kEventRows * kControlStep == 900u * 48000u, "event row count");

const char* const kValueHeader = "timeSeconds,value";
const char* const kEventHeader = "timeSeconds,value,target,phase";

/// The five files SC-015 checks by name. Order matches FR-081's table.
const std::array<const char*, 5> kFileNames = {
    "vorago_p1_perlin_oct1.csv", "vorago_p1_perlin_oct2.csv", "vorago_p1_perlin_oct4.csv",
    "vorago_p1_aizawa.csv",      "vorago_p1_slow_events.csv",
};

/// Row timestamp = step * 32 / 48000 evaluated on the INTEGER step count, never
/// a running float sum: the integer product is exact well inside 2^53, so the
/// last row of a 900 s render reads back as exactly 900.000000.
[[nodiscard]] double timeSecondsAt(std::size_t step) noexcept {
    return static_cast<double>(step * kControlStep) / kSampleRate;
}

// ------------------------------------------------------------------------------
// Writers
// ------------------------------------------------------------------------------

/// std::fixed with 6 decimals (not bare setprecision, which is 6 SIGNIFICANT
/// digits and would round 899.999333 to "899.999" - a 1 ms error, larger than
/// the 0.667 ms control step SC-015 tolerates on the covered duration).
void applyNumericFormat(std::ofstream& out) {
    out << std::fixed << std::setprecision(6);
}

/// Render a two-column "timeSeconds,value" trajectory. Rows are written AFTER
/// each advance, so row `step` carries the value at t = step * 32 / 48000 and
/// the final row lands exactly on the stated duration.
template <typename SourceT>
void writeValueTrajectory(SourceT& source, const std::filesystem::path& file,
                          std::size_t numRows) {
    INFO("writing " << file.string());
    std::ofstream out(file, std::ios::out | std::ios::trunc);
    REQUIRE(out.is_open());
    out << kValueHeader << '\n';
    applyNumericFormat(out);
    for (std::size_t step = 1; step <= numRows; ++step) {
        source.processBlock(kControlStep);
        out << timeSecondsAt(step) << ',' << source.getCurrentValue() << '\n';
    }
    out.close();
    REQUIRE(std::filesystem::exists(file));
}

/// Render the four-column scheduler trajectory. `target` is kNoTarget (255)
/// while idle; `phase` is the Phase enumerator (0 = Idle, 1/2/3 = A/H/R).
void writeEventTrajectory(SlowEventScheduler& scheduler, const std::filesystem::path& file,
                          std::size_t numRows) {
    INFO("writing " << file.string());
    std::ofstream out(file, std::ios::out | std::ios::trunc);
    REQUIRE(out.is_open());
    out << kEventHeader << '\n';
    applyNumericFormat(out);
    for (std::size_t step = 1; step <= numRows; ++step) {
        scheduler.processBlock(kControlStep);
        const auto phase =
            static_cast<unsigned>(static_cast<std::uint8_t>(scheduler.getEventPhase()));
        out << timeSecondsAt(step) << ',' << scheduler.getCurrentValue() << ','
            << static_cast<unsigned>(scheduler.getActiveTarget()) << ',' << phase << '\n';
    }
    out.close();
    REQUIRE(std::filesystem::exists(file));
}

// ------------------------------------------------------------------------------
// Re-read / validation pass
// ------------------------------------------------------------------------------

struct ParsedCsv {
    std::string header;
    std::size_t rowCount = 0;        ///< data rows (header excluded)
    std::size_t badFields = 0;       ///< empty, non-numeric or non-finite
    std::size_t wrongWidthRows = 0;  ///< column count != expected
    double lastTimeSeconds = 0.0;
    std::size_t risingEdges = 0;   ///< phase 0 -> non-zero transitions
    std::size_t targetedRows = 0;  ///< target != kNoTarget
};

/// Re-open a written file and parse EVERY field. Failures are counted rather
/// than REQUIREd per field: a 1.35 M-row file holds 5.4 M fields, and one
/// Catch2 assertion each would dominate both the run time and the report.
[[nodiscard]] ParsedCsv parseCsv(const std::filesystem::path& file,
                                 std::size_t expectedColumns) {
    ParsedCsv result;
    std::ifstream in(file);
    REQUIRE(in.is_open());
    REQUIRE(static_cast<bool>(std::getline(in, result.header)));
    if (!result.header.empty() && result.header.back() == '\r') result.header.pop_back();

    std::array<double, 4> fields{};
    int previousPhase = 0;
    bool havePreviousPhase = false;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;

        std::size_t columns = 0;
        std::size_t pos = 0;
        bool rowOk = true;
        for (;;) {
            const std::size_t comma = line.find(',', pos);
            const std::size_t end = (comma == std::string::npos) ? line.size() : comma;
            const std::string field = line.substr(pos, end - pos);
            char* parseEnd = nullptr;
            const double value = std::strtod(field.c_str(), &parseEnd);
            const bool consumedWholeField =
                !field.empty() && parseEnd == field.c_str() + field.size();
            // detail::isFinite, never std::isnan/isinf: the macOS leg builds
            // with -ffast-math, which folds the <cmath> predicates away.
            if (!consumedWholeField || !detail::isFinite(value)) {
                ++result.badFields;
                rowOk = false;
            } else if (columns < fields.size()) {
                fields[columns] = value;
            }
            ++columns;
            if (comma == std::string::npos) break;
            pos = comma + 1;
        }

        ++result.rowCount;
        if (columns != expectedColumns) ++result.wrongWidthRows;
        if (rowOk && columns >= 1u) result.lastTimeSeconds = fields[0];
        if (rowOk && expectedColumns == 4u && columns >= 4u) {
            // Both columns are written from integers, so the round trip through
            // the text is exact; compare as ints to avoid float equality.
            // kNoTarget is std::uint8_t; widen it once into a named int so the
            // comparison below has no signed/unsigned cast inside it.
            constexpr int kNoTargetValue = static_cast<int>(SlowEventScheduler::kNoTarget);
            const int target = static_cast<int>(fields[2]);
            const int phase = static_cast<int>(fields[3]);
            if (target != kNoTargetValue) ++result.targetedRows;
            if (havePreviousPhase && previousPhase == 0 && phase != 0) ++result.risingEdges;
            previousPhase = phase;
            havePreviousPhase = true;
        }
    }
    return result;
}

/// Structural assertions shared by all five files (SC-015).
[[nodiscard]] ParsedCsv validateFile(const std::filesystem::path& file,
                                     const char* expectedHeader, std::size_t expectedColumns,
                                     std::size_t expectedRows, double durationSeconds) {
    INFO("validating " << file.string());
    REQUIRE(std::filesystem::exists(file));

    const ParsedCsv parsed = parseCsv(file, expectedColumns);
    REQUIRE(parsed.header == std::string(expectedHeader));
    REQUIRE(parsed.badFields == 0u);
    REQUIRE(parsed.wrongWidthRows == 0u);
    REQUIRE(parsed.rowCount == expectedRows);
    // Covers the stated window to within one control step.
    REQUIRE(std::fabs(parsed.lastTimeSeconds - durationSeconds) <= kStepSeconds);
    return parsed;
}

}  // namespace

// ==============================================================================
// SC-015 / FR-081 / FR-082
// ==============================================================================

TEST_CASE("VoragoPhase1_TrajectoryHarness", "[.harness][processors][vorago]") {
    const std::filesystem::path dir{VORAGO_P1_HARNESS_DIR};
    std::filesystem::create_directories(dir);
    REQUIRE(std::filesystem::is_directory(dir));

    // Drop any CSVs left by a previous harness run so the "exactly five files"
    // count below is a statement about THIS run. Only our own vorago_p1_*.csv
    // names are removed - nothing else in the directory is touched, and the
    // directory itself is never removed.
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        if (!entry.is_regular_file()) continue;
        const std::string name = entry.path().filename().string();
        if (name.starts_with("vorago_p1_") && entry.path().extension().string() == ".csv") {
            std::filesystem::remove(entry.path());
        }
    }

    // --------------------------------------------------------------------------
    // Render (FR-081 table)
    // --------------------------------------------------------------------------

    // Three Perlin renders: kDefaultRate, depth 1, kDefaultPerlinSeed, octaves
    // 1 / 2 / 4 - the fBm layering is what the visual inspection compares.
    const std::array<int, 3> perlinOctaves = {1, 2, 4};
    for (std::size_t i = 0; i < perlinOctaves.size(); ++i) {
        PerlinNoiseSource perlin;
        perlin.prepare(kSampleRate);
        perlin.setSeed(PerlinNoiseSource::kDefaultPerlinSeed);
        perlin.setRate(PerlinNoiseSource::kDefaultRate);
        perlin.setOctaves(perlinOctaves[i]);
        perlin.setDepth(1.0f);
        perlin.reset();  // canonical start state after configuration
        REQUIRE(perlin.getOctaves() == perlinOctaves[i]);
        writeValueTrajectory(perlin, dir / kFileNames[i], kShortRows);
    }

    // Aizawa: speed 1.0, coupling 0 (free-running, no audio-input coupling).
    {
        ChaosModSource aizawa;
        aizawa.prepare(kSampleRate);
        aizawa.setModel(ChaosModel::Aizawa);
        aizawa.setSpeed(ChaosModSource::kDefaultSpeed);
        aizawa.setCoupling(0.0f);
        aizawa.reset();
        writeValueTrajectory(aizawa, dir / kFileNames[3], kShortRows);
    }

    // Scheduler: all defaults (20-90 s period, 5/3/8 s envelope), kDefaultEventSeed.
    // 900 s, not 60 s: per FR-067 the first period is idle pre-roll, so a 60 s
    // window would be a flat line of zeros across most seeds.
    {
        SlowEventScheduler scheduler;
        scheduler.prepare(kSampleRate);
        scheduler.setSeed(SlowEventScheduler::kDefaultEventSeed);
        scheduler.reset();  // re-draws the pre-roll period from the fresh stream
        writeEventTrajectory(scheduler, dir / kFileNames[4], kEventRows);
    }

    // --------------------------------------------------------------------------
    // Exactly five files, checked by name (SC-015)
    // --------------------------------------------------------------------------

    std::size_t regularFileCount = 0;
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        if (entry.is_regular_file()) ++regularFileCount;
    }
    REQUIRE(regularFileCount == kFileNames.size());
    for (const char* name : kFileNames) {
        INFO("expected output " << name);
        REQUIRE(std::filesystem::is_regular_file(dir / name));
    }

    // --------------------------------------------------------------------------
    // Re-open each file and validate structure + content (SC-015)
    // --------------------------------------------------------------------------

    for (std::size_t i = 0; i < 4u; ++i) {  // three Perlin files + Aizawa
        (void)validateFile(dir / kFileNames[i], kValueHeader, 2u, kShortRows,
                           kShortDurationSeconds);
    }

    const ParsedCsv events =
        validateFile(dir / kFileNames[4], kEventHeader, 4u, kEventRows, kEventDurationSeconds);

    // Content assertion, not just row count: the 900 s window must actually
    // contain swells. FR-081 predicts >= 9 onsets at the slowest possible draw
    // and ~16 at the mean; SC-015's floor is 3.
    INFO("rising edges = " << events.risingEdges << ", targeted rows = " << events.targetedRows);
    REQUIRE(events.risingEdges >= 3u);
    REQUIRE(events.targetedRows >= 1u);
}
