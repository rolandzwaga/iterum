// ==============================================================================
// Seraphis - Factory Preset Generator (Phase 12, FR-010 / FR-011 / FR-013 /
//            FR-014 / FR-015)
// ==============================================================================
// Spec:  specs/seraphis-phase12-presets-release/spec.md (C-3, C-4, FR-010..FR-016)
// Plan:  specs/seraphis-phase12-presets-release/plan.md section 1.3
// Tasks: specs/seraphis-phase12-presets-release/tasks.md (T005 - this file;
//        T006 adds the CMake target that builds it)
//
// Usage: seraphis_preset_generator [output_dir]
//   Default output_dir: plugins/seraphis/resources/presets
//   Matches .github/workflows/release.yml:170-174, which runs
//   `./build/bin/seraphis_preset_generator generated-presets`.
//
// HOW A PRESET IS PRODUCED (C-3 / C-4 / FR-013)
//   There is NO hand-written state layout here and there never will be. Each
//   preset's `Comp` chunk is whatever the SHIPPED
//   `Seraphis::Processor::getState()` writes (processor.cpp:1856) after the
//   authored normalized values have been driven through the SHIPPED parameter
//   fan-out. `Processor::processParameterChanges` is private, so the only
//   supported route is a `process()` call carrying an `IParameterChanges` - which
//   is exactly what `SeraphisTest::ProcessorFixture` builds
//   (plugins/seraphis/tests/seraphis_test_fixture.h:179-352). Reusing that
//   fixture rather than re-deriving the host plumbing is deliberate: FR-029's
//   in-process regeneration drives the processor through the SAME code path, so
//   the two sides of that byte comparison cannot silently diverge (plan R-2).
//
// DETERMINISM (FR-014)
//   Iteration is the DEFINITION ORDER of `PresetDefs::allPresets()` - never a
//   directory iteration, never an unordered container. No timestamp, no path
//   string and no RNG enters any byte: the `Comp` chunk is `getState()` output
//   over stored values and the `Info` chunk is derived from the definition table.
//   Re-running over an existing tree rewrites identical bytes (idempotence,
//   checked by tools/check-preset-generator-determinism.js).
//
// NO VSTGUI (FR-015)
//   Nothing below includes a VSTGUI header, and the target links
//   `KrateDSP KratePluginsShared sdk` only. `processor.cpp`'s single `ui/`
//   include is `ui/edit_message.h`, a POD plus three `constexpr` strings that
//   names no VSTGUI type (processor.cpp:18, :21).
//
// NO EDIT CHANNEL (FR-006a / FR-016 / OQ-4 = NO)
//   This tool has no `IMessage` / `notify()` surface at all. The `[partials]`
//   block is written from members that are zero on a fresh `Processor`
//   (processor.cpp:1911-1914), so all 64 pans and both bitmasks are zero BY
//   CONSTRUCTION - the harness asserts that rather than assuming it.
// ==============================================================================

#include "plugin_ids.h"              // ${CMAKE_SOURCE_DIR}/plugins/seraphis/src
#include "seraphis_preset_defs.h"    // ${CMAKE_SOURCE_DIR}/tools
#include "seraphis_test_fixture.h"   // ${CMAKE_SOURCE_DIR}/plugins/seraphis/tests

#include "public.sdk/source/common/memorystream.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iostream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

// Satisfies moduleinit.cpp's `extern void* moduleHandle;`
// (extern/vst3sdk/public.sdk/source/main/moduleinit.cpp:21, dereferenced at :85).
// MUST be a MUTABLE global with EXTERNAL linkage - `static`, `const`, or an
// anonymous namespace all turn it into an unresolved external at LINK time, on
// MSVC and on GCC alike. This tool is not a loaded plugin module, so nullptr is
// the correct value. Precedent: tools/gen_v2_fixtures/main.cpp:19.
// (plugins/seraphis/tests/vstgui_test_stubs.cpp, also in this target's source
// list, is 13 lines and defines ONLY GetPluginFactory - not this symbol.)
// NOLINTNEXTLINE(misc-use-internal-linkage,cppcoreguidelines-avoid-non-const-global-variables)
void* moduleHandle = nullptr;

// At file scope, not inside the unnamed namespace below: `main()` names this
// type too, and relying on the unnamed namespace's implicit using-directive to
// surface it there is needlessly subtle.
using Seraphis::PresetDefs::SeraphisPresetDef;

namespace {

// -----------------------------------------------------------------------------
// Render configuration
// -----------------------------------------------------------------------------
// One 64-sample block at 44 100 Hz is enough to latch the entire fan-out:
// `processParameterChanges` takes the LAST point of each queue
// (processor.cpp:1923-1929) and `setParam` writes one point at sample offset 0
// (seraphis_test_fixture.h:219-221). The sample rate does not reach the stored
// state; it only has to be a rate the processor accepts.
constexpr double kSampleRate = 44100.0;
constexpr Steinberg::int32 kBlockSize = 64;

// -----------------------------------------------------------------------------
// Little-endian scalar writers (tools/membrum_preset_generator.cpp:342-347)
// -----------------------------------------------------------------------------
void writeLE32(std::ofstream& f, std::uint32_t v) {
    f.write(reinterpret_cast<const char*>(&v), 4);
}

void writeLE64(std::ofstream& f, std::int64_t v) {
    f.write(reinterpret_cast<const char*>(&v), 8);
}

// -----------------------------------------------------------------------------
// The class id, derived AT RUN TIME - never a hardcoded literal (C-3)
// -----------------------------------------------------------------------------
// `FUID::toString` writes 32 uppercase hex characters plus a terminator into a
// char8[33] (extern/vst3sdk/pluginterfaces/base/funknown.h:295).
[[nodiscard]] std::string seraphisClassIdAscii() {
    Steinberg::char8 buf[33] = {};
    Seraphis::kProcessorUID.toString(buf);
    return std::string(buf, 32);
}

// -----------------------------------------------------------------------------
// Drive one preset through the SHIPPED processor and capture its state stream
// -----------------------------------------------------------------------------
[[nodiscard]] bool captureComponentState(const SeraphisPresetDef& def,
                                         std::vector<std::uint8_t>& comp,
                                         std::string& why) {
    SeraphisTest::ProcessorFixture fx;

    if (fx.prepare(kSampleRate, kBlockSize) != Steinberg::kResultOk) {
        why = "prepare() (initialize/setupProcessing/setActive) failed";
        return false;
    }

    // OI-5 / R-14. `prepare()` turns the effects-stage instrumentation ON
    // (seraphis_test_fixture.h:210) because every Seraphis TEST wants it.
    // processor.h:1466-1470 documents it as FALSE on every SHIPPING path, and
    // this is a RELEASE-pipeline tool (release.yml:168-174): it must drive the
    // shipped configuration, not the test one. FR-029's in-process regeneration
    // performs the identical disable, so both sides of that comparison run the
    // same configuration.
    fx.proc->setEffectsStageInstrumentedForTest(false);

    // Untouched IDs keep their registered defaults, which `getState()` writes
    // unchanged (C-4). Nothing is denormalized here - the shipped
    // `handle*ParamChange` functions own that arithmetic.
    for (const Seraphis::PresetDefs::ParamSetting& s : def.params) {
        fx.setParam(s.id, s.normalized);
    }

    if (fx.processBlock(kBlockSize) != Steinberg::kResultOk) {
        why = "process() failed";
        return false;
    }

    // The four SpectralState payloads need no extra drive: getState() calls
    // syncAuthoringMirrorFromDropdowns() first (processor.cpp:1866) and then
    // writes spectralSlotsAuthoring_[s] (:1893-1897), so setting IDs 409-412
    // through the fan-out above places the right factory payloads in the stream.
    Steinberg::MemoryStream stream;
    if (fx.proc->getState(&stream) != Steinberg::kResultOk) {
        why = "getState() failed";
        return false;
    }

    const Steinberg::TSize size = stream.getSize();
    const char* const data = stream.getData();
    if (size <= 0 || data == nullptr) {
        why = "getState() produced an empty stream";
        return false;
    }

    const auto* const bytes = reinterpret_cast<const std::uint8_t*>(data);
    comp.assign(bytes, bytes + static_cast<std::size_t>(size));
    return true;
}

// -----------------------------------------------------------------------------
// VST3 .vstpreset container writer
// -----------------------------------------------------------------------------
// Modelled byte-for-byte on tools/membrum_preset_generator.cpp:363-405:
//
//   offset 0    "VST3"                     4 B
//   offset 4    uint32 version = 1         4 B
//   offset 8    class id, 32 ASCII chars  32 B   (run-time derived, see above)
//   offset 40   int64 listOffset           8 B
//   offset 48   Comp payload         |comp| B
//               Info payload         |info| B
//   listOffset  "List", uint32 2,
//               {"Comp", int64 off, int64 size}, {"Info", int64 off, int64 size}
//
// The `Info` payload comes from PresetDefs::buildSeraphisInfoXml - there is
// deliberately NO second XML template in this file, so FR-029 clause 2's byte
// comparison has exactly one source (OI-1).
[[nodiscard]] bool writeVstPreset(const std::filesystem::path& path,
                                  const std::vector<std::uint8_t>& comp,
                                  const std::string& classIdAscii,
                                  const std::string& info) {
    std::ofstream f(path, std::ios::binary);
    if (!f) {
        std::cerr << "ERROR: failed to create " << path.string() << "\n";
        return false;
    }

    constexpr std::int64_t kHeaderSize = 48;
    const std::int64_t compOffset = kHeaderSize;
    const auto compSize = static_cast<std::int64_t>(comp.size());
    const std::int64_t infoOffset = compOffset + compSize;
    const auto infoSize = static_cast<std::int64_t>(info.size());
    const std::int64_t listOffset = infoOffset + infoSize;

    // Header
    f.write("VST3", 4);
    writeLE32(f, 1u);
    f.write(classIdAscii.data(), 32);
    writeLE64(f, listOffset);

    // Comp payload - the component state stream, verbatim.
    f.write(reinterpret_cast<const char*>(comp.data()),
            static_cast<std::streamsize>(comp.size()));

    // Info payload - the metadata XML, no terminator.
    f.write(info.data(), static_cast<std::streamsize>(info.size()));

    // Chunk list: 2 entries.
    f.write("List", 4);
    writeLE32(f, 2u);
    f.write("Comp", 4);
    writeLE64(f, compOffset);
    writeLE64(f, compSize);
    f.write("Info", 4);
    writeLE64(f, infoOffset);
    writeLE64(f, infoSize);

    return f.good();
}

// A category is only writable if it is one of the seven the config declares -
// otherwise the preset would land outside the directory set the browser scans.
[[nodiscard]] bool isKnownCategory(std::string_view category) {
    for (const std::string_view c : Seraphis::PresetDefs::kCategories) {
        if (c == category) {
            return true;
        }
    }
    return false;
}

}  // namespace

int main(int argc, char* argv[]) {
    std::filesystem::path outputBase = "plugins/seraphis/resources/presets";
    if (argc > 1) {
        outputBase = argv[1];
    }

    const std::string classIdAscii = seraphisClassIdAscii();
    if (classIdAscii.size() != 32) {
        std::cerr << "ERROR: kProcessorUID.toString() yielded " << classIdAscii.size()
                  << " characters, expected 32\n";
        return 1;
    }

    // FR-011: all seven category subdirectories exist under the output root,
    // whether or not a preset currently lands in each one.
    for (const std::string_view category : Seraphis::PresetDefs::kCategories) {
        const std::filesystem::path dir = outputBase / std::string(category);
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        if (ec) {
            std::cerr << "ERROR: failed to create " << dir.string() << ": " << ec.message()
                      << "\n";
            return 1;
        }
    }

    const std::vector<SeraphisPresetDef>& presets = Seraphis::PresetDefs::allPresets();

    std::size_t written = 0;
    std::size_t failures = 0;

    // FR-014: definition order, always.
    for (const SeraphisPresetDef& def : presets) {
        const std::string stem(def.name);
        const std::string category(def.category);

        if (!isKnownCategory(def.category)) {
            std::cerr << "ERROR: preset \"" << stem << "\" declares category \"" << category
                      << "\", which is not one of the seven in PresetDefs::kCategories\n";
            ++failures;
            continue;
        }

        std::vector<std::uint8_t> comp;
        std::string why;
        if (!captureComponentState(def, comp, why)) {
            std::cerr << "ERROR: " << category << "/" << stem << ": " << why << "\n";
            ++failures;
            continue;
        }

        const std::string info =
            Seraphis::PresetDefs::buildSeraphisInfoXml(def.name, def.category);
        const std::filesystem::path path = outputBase / category / (stem + ".vstpreset");

        if (!writeVstPreset(path, comp, classIdAscii, info)) {
            std::cerr << "ERROR: " << category << "/" << stem << ": write failed\n";
            ++failures;
            continue;
        }

        std::cout << "  Wrote " << comp.size() << " state bytes to " << path.string() << "\n";
        ++written;
    }

    std::cout << "\nGenerated " << written << " of " << presets.size()
              << " Seraphis factory presets." << std::endl;

    return failures == 0 ? 0 : 1;
}
