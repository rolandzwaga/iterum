// ==============================================================================
// Seraphis - Factory preset static gates (Phase 12)
// ==============================================================================
// Reference: specs/seraphis-phase12-presets-release/spec.md (C-1, FR-001,
//            FR-002, SC-001)
//            specs/seraphis-phase12-presets-release/tasks.md (T002)
//
// This TU carries every STATIC gate of the factory preset library - the ones
// that read the declared config and the committed tree, never a render.
//
// CRITERION OWNED BY THIS CASE: FR-001 + SC-001 - the shipped category set and
// the on-disk preset tree are the SAME seven names, in the SAME order, and the
// tree holds nothing else.
//
// TWO CONSTRUCTION CHOICES, stated here rather than discovered in review:
//
// 1. THE EXPECTED LIST IS A LITERAL. Comparing makeSeraphisPresetConfig()
//    against any expression that itself reads makeSeraphisPresetConfig() is a
//    tautology that passes for a wrong or reordered list. The literal is the
//    second, independent copy that makes the assertion able to fail.
// 2. THE ORDER CLAUSE IS CARRIED BY AN ELEMENT-WISE VECTOR COMPARISON, NOT BY A
//    SET. C-1 fixes the ORDER (it is the browser's tab order,
//    controller.cpp:463-470), and a std::set comparison is order-blind. A set is
//    used ONLY for the filesystem bijection, where directory-iteration order is
//    unspecified by the standard and carries no meaning.
// ==============================================================================

#include "plugin_ids.h"
#include "preset/preset_manager.h"  // isValidPresetName (:120) - the SHIPPED
                                    // predicate, never a re-derived character set
#include "preset/seraphis_preset_config.h"
#include "preset_test_support.h"    // T007 - ${CMAKE_CURRENT_SOURCE_DIR} (= plugins/
                                    // seraphis/tests) is on this target's include
                                    // path (tests/CMakeLists.txt:102).
#include "seraphis_preset_defs.h"  // ${CMAKE_SOURCE_DIR}/tools is on this target's
                                   // include path (tests/CMakeLists.txt:104).
#include "seraphis_test_fixture.h"

#include "public.sdk/source/common/memorystream.h"

#include <krate/dsp/core/db_utils.h>              // T011 - detail::isNaN (:54) /
                                                  // isInf (:175), the BIT-PATTERN
                                                  // pair; never std::isnan
#include <krate/dsp/processors/spectral_state.h>  // T011 - SpectralState::kStatePartials

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <atomic>  // T011 - std::memory_order_relaxed on the decoded packs
#include <cmath>   // T020 - std::abs(double) on the relative-error metric
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>  // T020 - std::scientific / std::setprecision in the probe report
#include <iterator>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>  // std::error_code - every TempUserPresetDir filesystem
                         // call takes the non-throwing overload
#include <utility>       // T021 - std::move on the parsed-container vector
#include <vector>

namespace {

// FR-001 / C-1: the seven shipped subcategories, in order. `Textures` keeps its
// byte-exact spelling and its existing directory - renaming a shipped category
// leaves `subcategory` EMPTY for every user preset saved against the old name
// (PresetManager::parsePresetFile, plugins/shared/src/preset/preset_manager.cpp:95-103).
const std::vector<std::string> kExpectedCategories{"Textures", "Pads",   "Drones", "Bells",
                                                   "Choirs",   "Motion", "Cinematic"};

std::string joinNames(const std::set<std::string>& names) {
    std::ostringstream os;
    bool first = true;
    for (const auto& n : names) {
        if (!first) {
            os << ", ";
        }
        os << n;
        first = false;
    }
    return os.str();
}

/// Build one diagnostic line out of its parts.
///
/// Every failure message in this TU used to be an `a + ": " + b + ...` chain,
/// which allocates one throw-away std::string per `+` and is what clang-tidy's
/// performance-inefficient-string-concatenation reports. One `append` per part
/// into a single buffer replaces the chain; the call sites read the same.
template <typename... Parts>
[[nodiscard]] std::string joinText(const Parts&... parts) {
    std::string out;
    (out.append(parts), ...);
    return out;
}

std::string joinPaths(const std::vector<std::string>& paths) {
    std::ostringstream os;
    bool first = true;
    for (const auto& p : paths) {
        if (!first) {
            os << " | ";
        }
        os << p;
        first = false;
    }
    return os.str();
}

// T010 - a fresh, EMPTY user preset directory for the lifetime of one case.
//
// THE OVERRIDE IS LOAD-BEARING, NOT HYGIENE. PresetManager::scanPresets() scans
// the USER directory FIRST and only then the factory one
// (plugins/shared/src/preset/preset_manager.cpp:41-49), and both scans feed the
// SAME cachedPresets_ list. Leaving the 4th constructor parameter empty points
// that first scan at the real machine location
// (Platform::getUserPresetDirectory, preset_manager.cpp:470-478), so the entry
// count and the isFactory tally would depend on whatever the developer running
// the suite happened to save last - the case would pass or fail for reasons that
// have nothing to do with the shipped library.
//
// RAII RATHER THAN A remove_all() AT THE END OF THE CASE: Catch2's REQUIRE
// throws, so a trailing cleanup statement is skipped on exactly the runs that
// produce the most leftover directories. Every filesystem call here takes the
// std::error_code overload - a destructor must not throw.
class TempUserPresetDir {
public:
    TempUserPresetDir() {
        std::error_code ec;
        const std::filesystem::path base = std::filesystem::temp_directory_path(ec);
        if (ec) {
            return;
        }
        // create_directory returns FALSE (with no error) when the path already
        // exists, so the counter walks past a directory left behind by a
        // previously crashed run instead of adopting its contents.
        for (int attempt = 0; attempt < 4096; ++attempt) {
            const std::filesystem::path candidate =
                base / ("seraphis_preset_scan_" + std::to_string(attempt));
            std::error_code createEc;
            if (std::filesystem::create_directory(candidate, createEc)) {
                path_ = candidate;
                return;
            }
        }
    }

    ~TempUserPresetDir() {
        if (!path_.empty()) {
            std::error_code ec;
            std::filesystem::remove_all(path_, ec);
        }
    }

    TempUserPresetDir(const TempUserPresetDir&) = delete;
    TempUserPresetDir& operator=(const TempUserPresetDir&) = delete;
    TempUserPresetDir(TempUserPresetDir&&) = delete;
    TempUserPresetDir& operator=(TempUserPresetDir&&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

private:
    std::filesystem::path path_;
};

// T008 - the per-category tally, rendered for a failure message.
std::string joinTally(const std::map<std::string, std::size_t>& tally) {
    std::ostringstream os;
    bool first = true;
    for (const auto& [name, count] : tally) {
        if (!first) {
            os << ", ";
        }
        os << name << '=' << count;
        first = false;
    }
    return first ? std::string("(none)") : os.str();
}

// C-2 / FR-004: 42 presets, 6 per category, with a hard floor of 5 in any one.
// The floor is the GATE; the per-category target of 6 is REPORTED only - a
// library of 7/6/6/6/6/6/5 satisfies C-2 and must not be failed by an equality
// this phase never promised.
constexpr std::size_t kExpectedPresetCount = 42;
constexpr std::size_t kPerCategoryFloor = 5;
constexpr std::size_t kPerCategoryTarget = 6;

// T009 / FR-006 / SC-004: the shipped component-chunk length
// (processor.cpp:1836-1843). Stated here as a LITERAL rather than reused from
// SeraphisTest::kSeraphisStateBytes: this TU is the thing asserting the length,
// and asserting the support header's constant against itself would pass for any
// value that constant happened to hold.
constexpr std::size_t kComponentChunkBytes = 2868;

// T009 / FR-006: kCurrentStateVersion (plugin_ids.h:27), again as a literal. The
// Non-goals of this phase freeze it at 3; reading the shipped constant back would
// make this clause pass for a version this phase never sanctioned.
constexpr std::int32_t kExpectedStateVersion = 3;

// T009 / SC-003: the container magic, read straight off the file.
//
// parseVstPreset() already rejects a bad magic and says so through `parseError`,
// so this is a SECOND, INDEPENDENT copy of the clause - the one that makes the
// magic name itself in the failure list instead of arriving as one more line of
// generic parser text. Returns an empty string when the file cannot be opened or
// is shorter than four bytes; both are reported as a magic failure, which is what
// they are at this point in the case (the file already parsed).
[[nodiscard]] std::string readContainerMagic(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return {};
    }
    std::array<char, 4> buffer{};
    stream.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    if (stream.gcount() != static_cast<std::streamsize>(buffer.size())) {
        return {};
    }
    return {buffer.data(), buffer.size()};
}

// T009 / SC-004: the component chunk's leading int32.
//
// getState() writes through `IBStreamer streamer(state, kLittleEndian)`
// (processor.cpp:1861), so the version prefix is LITTLE-ENDIAN ON DISK
// regardless of the host's own byte order. Assembled byte by byte rather than
// memcpy'd into an int32 for exactly that reason: the file's endianness is fixed
// by the writer, and byte assembly states it without needing a host-endianness
// assumption that would be wrong on a big-endian leg.
[[nodiscard]] bool leadingStateVersion(const std::vector<std::uint8_t>& comp,
                                       std::int32_t& out) {
    if (comp.size() < 4u) {
        return false;
    }
    const std::uint32_t raw = static_cast<std::uint32_t>(comp[0])
                              | (static_cast<std::uint32_t>(comp[1]) << 8)
                              | (static_cast<std::uint32_t>(comp[2]) << 16)
                              | (static_cast<std::uint32_t>(comp[3]) << 24);
    out = static_cast<std::int32_t>(raw);
    return true;
}

// =============================================================================
// T011 - shared machinery for the four authoring ledgers
// =============================================================================

/// Every decoded-field read below is `relaxed`: the packs are only ever atomic
/// because the SHIPPED structs are, and a decoded DecodedPresetState is a
/// single-threaded local that no audio thread has ever seen.
constexpr std::memory_order kRlx = std::memory_order_relaxed;

/// Parse + decode every preset, handing each (file, decoded state) pair to `fn`.
///
/// A CALLBACK RATHER THAN A RETURNED CONTAINER, and that is forced, not stylistic:
/// DecodedPresetState holds nine packs of std::atomic members, so it is neither
/// copyable nor movable (preset_test_support.h:361-366). It cannot be returned,
/// collected into a vector, or captured by value - a stack-local per file is the
/// only shape that compiles, and it also keeps the 42-preset sweep at exactly one
/// live decode at a time.
///
/// STRUCTURAL FAILURES ARE COLLECTED, NEVER ASSERTED HERE. A helper that
/// REQUIREd would abort the whole case on the first unreadable file and report
/// one path where the library has six; each caller asserts `failures.empty()`
/// once, with every path named.
template <typename Fn>
void forEachDecodedPreset(const std::vector<std::filesystem::path>& files,
                          std::vector<std::string>& failures, const Fn& fn) {
    for (const auto& file : files) {
        const SeraphisTest::PresetFile pf = SeraphisTest::parseVstPreset(file);
        if (!pf.parseError.empty()) {
            failures.push_back(file.string() + ": " + pf.parseError);
            continue;
        }
        SeraphisTest::DecodedPresetState st;
        std::string why;
        if (!SeraphisTest::decodePresetState(pf.comp, st, why)) {
            failures.push_back(file.string() + ": decode failed: " + why);
            continue;
        }
        fn(pf, st);
    }
}

// FR-009's finiteness test, BY BIT PATTERN.
//
// NEVER std::isnan / std::isinf: the macOS leg builds with -ffast-math, under
// which the compiler is entitled to assume no NaN exists and both calls fold to
// `false`. Krate::DSP::detail::isNaN / isInf inspect the IEEE-754 exponent and
// mantissa fields as integers (db_utils.h:54-58, :175) and are unaffected - which
// is exactly why THIS TU is compiled with -fno-fast-math
// (plugins/seraphis/tests/CMakeLists.txt:177). It is the same pair
// loadPartialOverrides screens untrusted stream floats with (processor.cpp:482).
[[nodiscard]] bool isFiniteFloat(float value) {
    return !Krate::DSP::detail::isNaN(value) && !Krate::DSP::detail::isInf(value);
}

std::string joinInts(const std::set<int>& values) {
    std::ostringstream os;
    bool first = true;
    for (const int v : values) {
        if (!first) {
            os << ", ";
        }
        os << v;
        first = false;
    }
    return first ? std::string("(none)") : os.str();
}

// T011 / FR-008 / SC-014 (C-5): the budgeted operating point. The REGISTERED
// range stays 1-16 and this phase does not narrow it (roadmap lines 322-326);
// what is asserted is what a FACTORY preset stores. Roadmap lines 313-321:
// 24.21 % of one core at 8 voices against 47.36 % at 16.
constexpr int kMaxFactoryPolyphony = 8;

// T011 / FR-008a / SC-014a (Q8): the envelope-timing authoring ceiling. It bounds
// the sweep BY BOUNDING WHAT SHIPS - no arm truncates a timeline, so a breach is
// re-authored, never measured with a capped A.
constexpr double kMaxAttackSeconds = 12.0;
constexpr double kMaxReleaseSeconds = 10.0;

// T011 / C-2's grain-envelope row: >= 3 of the 6 shipped window types
// (dropdown_mappings.h:211-213), not all six.
constexpr std::size_t kGrainEnvelopeFloor = 3;

// --- C-2's enumerations, as ASCII, in the shipped declaration order ----------
// SECOND, INDEPENDENT COPIES, exactly as kExpectedCategories and kExpectedTabs
// are. The shipped tables are `Steinberg::Vst::TChar*` (UTF-16) arrays that this
// TU cannot print, and reading them back would make the coverage message read
// "index 3 missing" instead of "Chamber missing" - which is the difference
// between a message T014 can author against and one it cannot.
const std::vector<std::string> kBodyMaterialNames{"Glass", "Strings", "Metal Plate", "Chamber",
                                                  "Ice"};                     // :198-200
const std::vector<std::string> kSpectralStateNames{"Sine Stack", "Bell", "Choir", "Glass",
                                                   "Breath"};                 // :178-180
const std::vector<std::string> kTravelModeNames{"External", "Spline"};        // :118-119
const std::vector<std::string> kEnvelopeModeNames{"Standard", "Growth"};      // :190-191
const std::vector<std::string> kGrainEnvelopeNames{"Hann",     "Trapezoid", "Sine",
                                                   "Blackman", "Linear",    "Exponential"};  // :211-213

// --- the per-block 4-byte field census ---------------------------------------
//
// SHARED BY TWO CASES ON PURPOSE, and it lives here rather than inside either of
// them for exactly that reason (tasks.md T021: "Reuse the SAME counters T011
// case 4 defines - the two must not drift apart"):
//
//   * Seraphis_FactoryPresets_PartialsBlockIsInert (T011) reconciles its
//     finiteness enumeration against it;
//   * Seraphis_FactoryPresets_TreeMatchesGenerator (T021) reconciles the union of
//     its integer comparator (clause 4) and its float comparator (clause 5)
//     against it.
//
// Two independent tables would let one case grow a field the other never
// compares, which is precisely the silent-escape this tripwire exists to stop.
//
// The enumeration is FIELD-COUNTED per block so that a float added to any pack in
// a later phase fails the COUNT rather than silently escaping the checks above.
// Each count is that block's stream width / 4 (processor.cpp:1836-1843), counting
// every 4-byte field - the floats that are checked AND the int32/bool fields that
// are only counted:
//
//   [global] 12 B  -> 3  = masterGain(f) + polyphony + softLimit
//   [macro]  20 B  -> 5  = 5 floats
//   [seed]    4 B  -> 1  = seedIndex
//   [cloud]  44 B  -> 11 = 11 floats
//   [morph]  52 B  -> 13 = 5 floats + 4 scalars (travelMode/sync/syncNote/
//                          stateCount) + slot[4]. Tasks.md's "9 scalars" is
//                          the non-slot subset; the block is 13 fields wide.
//   [life]   40 B  -> 10 = 9 floats + envMode
//   [body]   52 B  -> 13 = 10 floats + material + inputAgc + resonatorBypass
//   [atmos]  68 B  -> 17 = 15 floats + freeze + grainEnvelope
//   [aether] 72 B  -> 18 = 17 floats + freeze
//   [effects]64 B  -> 16 = 12 floats + spreadDirection + delaySync +
//                          delaySyncNote + spectralFreeze
//
// EXPLICITLY EXCLUDED because they are not 4-byte pack fields: the leading
// state-version int32, the four SpectralState payloads, and the whole 272-byte
// [partials] block (64 pans + two uint64 masks). Each of those is covered by its
// own, separately counted clause in both cases.
struct BlockFieldCount {
    const char* block;
    std::size_t fields;
};
constexpr std::array<BlockFieldCount, 10> kExpectedFieldCounts{
    {{.block = "[global]", .fields = 3},
     {.block = "[macro]", .fields = 5},
     {.block = "[seed]", .fields = 1},
     {.block = "[cloud]", .fields = 11},
     {.block = "[morph]", .fields = 13},
     {.block = "[life]", .fields = 10},
     {.block = "[body]", .fields = 13},
     {.block = "[atmos]", .fields = 17},
     {.block = "[aether]", .fields = 18},
     {.block = "[effects]", .fields = 16}}};

}  // namespace

TEST_CASE("Seraphis_FactoryPresets_CategoriesMatchConfig", "[seraphis][preset]") {
    const Krate::Plugins::PresetManagerConfig cfg = Seraphis::makeSeraphisPresetConfig();

    // --- FR-001 clauses a+b: exactly seven names, in C-1 order ----------------
    REQUIRE(cfg.subcategoryNames.size() == 7);
    REQUIRE(cfg.subcategoryNames == kExpectedCategories);
    // Asserted SEPARATELY so the byte-exact-spelling clause names itself on
    // failure instead of hiding inside the whole-vector comparison.
    REQUIRE(cfg.subcategoryNames[0] == "Textures");

    // --- FR-001 clause c: the three fields this phase must NOT touch ----------
    REQUIRE(cfg.pluginName == "Seraphis");
    REQUIRE(cfg.pluginCategoryDesc == "Synth");
    REQUIRE(cfg.processorUID == Seraphis::kProcessorUID);

    // --- T004: the GENERATOR's category source == the RUNTIME's ---------------
    // tools/seraphis_preset_defs.h::kCategories is what the generator uses to
    // create the output subdirectories; cfg.subcategoryNames is what the browser
    // matches a preset's parent directory against
    // (plugins/shared/src/preset/preset_manager.cpp:95-103). The header says the
    // two "MUST equal"; without this clause that sentence is a comment with no
    // check, and a divergence ships presets that exist on disk but never appear
    // in the browser. Element-wise and ordered - the order IS the tab order
    // (controller.cpp:463-470).
    REQUIRE(Seraphis::PresetDefs::kCategories.size() == cfg.subcategoryNames.size());
    REQUIRE(std::equal(Seraphis::PresetDefs::kCategories.begin(),
                       Seraphis::PresetDefs::kCategories.end(),
                       cfg.subcategoryNames.begin(), cfg.subcategoryNames.end(),
                       [](std::string_view a, const std::string& b) { return a == b; }));

    // --- SC-001: 1:1 correspondence with the committed tree, BOTH directions --
    // SERAPHIS_RESOURCES_DIR is handed to this target by
    // plugins/seraphis/tests/CMakeLists.txt:112 - there is NO walk-up loop here.
    const std::filesystem::path root =
        std::filesystem::path(SERAPHIS_RESOURCES_DIR) / "presets";
    INFO("presets root: " << root.string());
    REQUIRE(std::filesystem::is_directory(root));

    std::set<std::string> onDisk;
    for (const auto& entry : std::filesystem::directory_iterator(root)) {
        if (entry.is_directory()) {
            onDisk.insert(entry.path().filename().string());
        }
    }

    const std::set<std::string> expectedSet(kExpectedCategories.begin(),
                                            kExpectedCategories.end());

    std::set<std::string> missing;     // declared in the config, absent on disk
    std::set<std::string> unexpected;  // present on disk, undeclared
    std::set_difference(expectedSet.begin(), expectedSet.end(), onDisk.begin(), onDisk.end(),
                        std::inserter(missing, missing.end()));
    std::set_difference(onDisk.begin(), onDisk.end(), expectedSet.begin(), expectedSet.end(),
                        std::inserter(unexpected, unexpected.end()));

    INFO("missing directories (declared, absent on disk): " << joinNames(missing));
    INFO("unexpected directories (present on disk, undeclared): " << joinNames(unexpected));
    REQUIRE(missing.empty());
    REQUIRE(unexpected.empty());
    REQUIRE(onDisk == expectedSet);

    // FR-002: every .vstpreset sits DIRECTLY inside a category directory.
    // recursive_directory_iterator::depth() is 0 for entries in the root itself,
    // so a preset file must be at depth 1 - nothing at the root, nothing deeper.
    std::vector<std::string> misplaced;
    for (auto it = std::filesystem::recursive_directory_iterator(root);
         it != std::filesystem::recursive_directory_iterator(); ++it) {
        if (!it->is_regular_file()) {
            continue;
        }
        if (it->path().extension() != ".vstpreset") {
            continue;
        }
        if (it.depth() != 1) {
            misplaced.push_back(it->path().string());
        }
    }
    INFO("misplaced .vstpreset files: " << joinPaths(misplaced));
    REQUIRE(misplaced.empty());
}

// =============================================================================
// T007 - the decode tripwire (FR-025a)
// =============================================================================
// CRITERION OWNED BY THIS CASE: preset_test_support.h's decodePresetState()
// consumes EXACTLY the stream Processor::getState() produces - every block, in
// getState()'s order, ending at byte 2868 with nothing left over.
//
// WHY THIS CASE EXISTS AT ALL, given that eight later cases decode presets: it
// is the ONLY case whose subject is the decoder itself. Every other Phase 12
// case reads a decoded FIELD, and a decoder that drifted by one block would hand
// those cases plausible-looking values from the wrong offsets - a preset whose
// [aether] decay was really the [body] block's last float would sail through
// SC-014a and fail nothing. The tripwire is what turns that into a named
// failure, and this case is what proves the tripwire fires on the real stream.
//
// THE SUBJECT IS A FRESH, DEFAULT-STATE PROCESSOR, NOT A FILE. The decoder must
// be proven against the shipped serializer's output BEFORE any generated preset
// is trusted; running it against a .vstpreset first would leave a decoder bug
// and a generator bug able to cancel out. It also makes this case independent of
// T014's authoring - it is green from T007 onward, not red until the library
// lands.
TEST_CASE("Seraphis_PresetSupport_DecodeConsumesWholeStream", "[seraphis][preset]") {
    SeraphisTest::ProcessorFixture fx;
    REQUIRE(fx.prepare(44100.0, 512) == Steinberg::kResultOk);

    Steinberg::MemoryStream stream;
    REQUIRE(fx.proc->getState(&stream) == Steinberg::kResultOk);

    const char* raw = stream.getData();
    REQUIRE(raw != nullptr);
    const auto size = static_cast<std::size_t>(stream.getSize());
    const auto* first = reinterpret_cast<const std::uint8_t*>(raw);
    const std::vector<std::uint8_t> comp(first, first + size);

    // FR-006 / SC-004. The literal is the independent copy: comparing against
    // kSeraphisStateBytes alone would only prove the header agrees with itself.
    INFO("component chunk size: " << comp.size());
    REQUIRE(comp.size() == std::size_t{2868});
    REQUIRE(comp.size() == SeraphisTest::kSeraphisStateBytes);

    SeraphisTest::DecodedPresetState st;
    std::string why;
    const bool decoded = SeraphisTest::decodePresetState(comp, st, why);
    // Streamed AFTER `why` is populated: Catch2's INFO stringifies at the point
    // the macro executes, so placing it above the call would report an empty
    // reason on every failure.
    INFO("decodePresetState: " << why);
    REQUIRE(decoded);

    // kCurrentStateVersion (plugin_ids.h:27). Written as the LITERAL 3 on
    // purpose - reading the shipped constant back would pass for any value the
    // constant happened to hold, and this phase's Non-goals freeze it at 3.
    REQUIRE(st.version == 3);

    // FR-006a. A fresh Processor has never seen an EditMessage, so both
    // bitmasks are zero at the source (processor.cpp:1911-1914). Asserting it
    // here pins the RAW read: decodePresetState deliberately does NOT go through
    // loadPartialOverrides, whose clamping would mask a corrupt block.
    REQUIRE(st.panOverrideBits == 0u);
    REQUIRE(st.maskBits == 0u);
}

// =============================================================================
// T008 - the inventory gates (SC-002 / FR-004, and FR-005 via OI-6)
// =============================================================================
// BOTH CASES BELOW ARE EXPECTED TO FAIL UNTIL T014. The committed tree holds
// only T004's three pilot presets (Vellum / First Light / Deep Well), so the
// count assertion reports 3 and four categories report a tally of 0. That red
// state is the point: these two cases are what T014's authoring is written
// against, and authoring 42 presets against a gate that does not yet exist is
// how a library ships with a duplicate name or an empty category.
//
// FR-005 HAS NO SC OF ITS OWN in the spec (OI-6). It gets a real case anyway -
// a requirement with no executable check is a requirement that ships unbuilt,
// and every clause of FR-005 (validity, library-wide uniqueness, ASCII, and
// agreement with the embedded metadata) is mechanically checkable here.

TEST_CASE("Seraphis_FactoryPresets_CountAndDistribution", "[seraphis][preset]") {
    const std::vector<std::filesystem::path> files = SeraphisTest::allPresetFiles();

    // THE TALLY IS BUILT BEFORE THE FIRST ASSERTION. Catch2 abandons the case at
    // the first failed REQUIRE, so a tally assembled afterwards would never be
    // printed on the very failure it exists to explain. Every category is seeded
    // at 0 so an EMPTY category appears in the message as `Bells=0` rather than
    // vanishing from it.
    std::map<std::string, std::size_t> tally;
    for (const auto& category : kExpectedCategories) {
        tally[category] = 0;
    }
    std::map<std::string, std::size_t> undeclared;
    for (const auto& file : files) {
        const std::string category = file.parent_path().filename().string();
        const auto it = tally.find(category);
        if (it != tally.end()) {
            ++it->second;
        } else {
            ++undeclared[category];
        }
    }

    INFO("presets root: " << SeraphisTest::factoryPresetRoot().string());
    INFO("per-category tally: " << joinTally(tally));
    INFO("presets under UNDECLARED directories: " << joinTally(undeclared));
    INFO("total .vstpreset files found: " << files.size());

    // --- SC-002 / FR-004 clause 1: the library size ---------------------------
    REQUIRE(files.size() == kExpectedPresetCount);

    // --- SC-002 / FR-004 clause 2: the per-category floor ---------------------
    // Asserted per category rather than on `*std::min_element` so the failure
    // names WHICH category is short instead of only how short the worst one is.
    for (const auto& category : kExpectedCategories) {
        INFO("category `" << category << "` holds " << tally.at(category) << " preset(s), floor is "
                          << kPerCategoryFloor);
        REQUIRE(tally.at(category) >= kPerCategoryFloor);
    }

    // --- C-2's 6-per-category target: REPORTED, never gated -------------------
    // C-2 sizes the library at 7 x 6 but states the floor as 5. Turning the 6
    // into an equality would make a legal 7/5 redistribution a test failure, so
    // the deviation is surfaced and nothing more.
    for (const auto& category : kExpectedCategories) {
        if (tally.at(category) != kPerCategoryTarget) {
            WARN("category `" << category << "` holds " << tally.at(category)
                              << " presets, off the C-2 target of " << kPerCategoryTarget
                              << " (>= " << kPerCategoryFloor << " is the gate, so this is a "
                              << "report, not a failure)");
        }
    }
}

TEST_CASE("Seraphis_FactoryPresets_NamesAreValidAndUnique", "[seraphis][preset]") {
    const std::vector<std::filesystem::path> files = SeraphisTest::allPresetFiles();

    // FIRST, so a vacuous pass is impossible: every clause below is a loop over
    // `files`, and all four of them are trivially satisfied by an empty library.
    INFO("presets root: " << SeraphisTest::factoryPresetRoot().string());
    REQUIRE(!files.empty());

    std::vector<std::string> unreadable;      // container did not parse at all
    std::vector<std::string> invalidNames;    // (a) isValidPresetName == false
    std::vector<std::string> duplicateStems;  // (b) collides library-wide
    std::vector<std::string> nonAscii;        // (c) a code unit >= 0x80
    std::vector<std::string> nameMismatch;    // (d) stem != Info `Name`
    std::set<std::string> stems;

    for (const auto& file : files) {
        const SeraphisTest::PresetFile pf = SeraphisTest::parseVstPreset(file);
        if (!pf.parseError.empty()) {
            unreadable.push_back(file.string() + ": " + pf.parseError);
            continue;
        }

        // (a) THE SHIPPED PREDICATE, not a re-derived character set. Rejecting
        // `/\:*?"<>|` plus empty/oversize is preset_manager.cpp:491-501's job; a
        // second copy here would drift the moment that set changes and would
        // then pass names the browser refuses to save over.
        if (!Krate::Plugins::PresetManager::isValidPresetName(pf.stem)) {
            invalidNames.push_back(file.string());
        }

        // (b) UNIQUENESS IS LIBRARY-WIDE, not per category. The browser's list is
        // flat and its search matches across categories
        // (preset_manager.cpp:123-147), so `Bells/Halo` and `Motion/Halo` are two
        // indistinguishable rows even though the filesystem is happy.
        if (!stems.insert(pf.stem).second) {
            duplicateStems.push_back(pf.stem + " (second occurrence: " + file.string() + ")");
        }

        // (c) ASCII, checked on `unsigned char`. On a signed-char platform a byte
        // >= 0x80 is NEGATIVE, so the natural `ch < 0x80` test on a plain `char`
        // passes for exactly the bytes it is meant to reject.
        for (const char ch : pf.stem) {
            if (static_cast<unsigned char>(ch) >= 0x80u) {
                nonAscii.push_back(file.string());
                break;
            }
        }

        // (d) THE METADATA SIDE, cross-linked. The file stem and the `Info` XML
        // `Name` attribute are two independently written copies of the same
        // string (tools/seraphis_preset_defs.h:207-226 writes one, the generator
        // names the file with the other), and a rename that touches only one of
        // them leaves the host browser showing a name the filesystem does not
        // have. A MISSING attribute is reported as its own fault, not as an
        // empty-string mismatch - parseInfoAttributes leaves it ABSENT.
        const std::map<std::string, std::string> attributes =
            SeraphisTest::parseInfoAttributes(pf.info);
        const auto nameAttribute = attributes.find("Name");
        if (nameAttribute == attributes.end()) {
            nameMismatch.push_back(file.string() + ": Info chunk carries no `Name` attribute");
        } else if (nameAttribute->second != pf.stem) {
            nameMismatch.push_back(file.string() + ": Info `Name` = \"" + nameAttribute->second
                                   + "\", file stem = \"" + pf.stem + "\"");
        }
    }

    // Reported before any of the four clauses: a file that failed to parse was
    // SKIPPED above, so leaving this until last would let an unreadable preset
    // shrink `stems` and turn clause (b)'s count into a second, misleading
    // failure.
    INFO("unreadable preset files: " << joinPaths(unreadable));
    REQUIRE(unreadable.empty());

    INFO("names rejected by PresetManager::isValidPresetName: " << joinPaths(invalidNames));
    REQUIRE(invalidNames.empty());

    INFO("duplicate preset names (library-wide): " << joinPaths(duplicateStems));
    REQUIRE(duplicateStems.empty());

    INFO("non-ASCII preset names: " << joinPaths(nonAscii));
    REQUIRE(nonAscii.empty());

    INFO("stem / Info `Name` disagreements: " << joinPaths(nameMismatch));
    REQUIRE(nameMismatch.empty());

    // The count assertion is LAST and is stated on the distinct-name set: with
    // `duplicateStems` already proven empty this is `files.size() == 42` restated
    // in the units FR-005 cares about, and it is what keeps this case red until
    // T014 rather than passing over the three pilot presets.
    INFO("distinct preset names: " << stems.size() << " over " << files.size() << " files");
    REQUIRE(stems.size() == kExpectedPresetCount);
}

// =============================================================================
// T009 - the container / stream / round-trip gates (SC-003, SC-004, SC-005)
// =============================================================================
// UNLIKE T008's TWO CASES, ALL THREE BELOW ARE GREEN FROM T009 ONWARD. They
// assert a PROPERTY OF EVERY FILE FOUND, never a library SIZE, so they hold over
// T004's three pilot presets exactly as they will over T014's 42. That split is
// deliberate: the library-size gate belongs to SC-002 and lives in exactly one
// place (Seraphis_FactoryPresets_CountAndDistribution), and duplicating it here
// would make three more cases red for a reason none of them is about - which is
// how a real container regression gets lost inside an expected red.
//
// EACH CASE STILL OPENS WITH `REQUIRE(!files.empty())`. Every clause below is a
// loop body, and a loop over nothing satisfies all of them; without that guard a
// discovery bug (a wrong SERAPHIS_RESOURCES_DIR, a renamed directory) would
// present as three passing cases.

TEST_CASE("Seraphis_FactoryPresets_ContainerIsValid", "[seraphis][preset]") {
    const std::vector<std::filesystem::path> files = SeraphisTest::allPresetFiles();
    INFO("presets root: " << SeraphisTest::factoryPresetRoot().string());
    REQUIRE(!files.empty());

    // THE EXPECTED CLASS ID IS DERIVED AT RUN TIME, NEVER A LITERAL. Ruinae's
    // generator hardcodes its 32-char string (tools/ruinae_preset_generator.cpp:28),
    // which means a preset whose header disagrees with the shipped FUID is
    // unreachable by any test. Deriving it from Seraphis::kProcessorUID through
    // FUID::toString (funknown.h:295 - documented as the 32-char uppercase hex
    // form) is what ties the file header to the identity a host actually looks
    // the plugin up by; a preset carrying any other class id is silently ignored
    // by every DAW that reads the container.
    //
    // The buffer is 32 hex chars + a terminator: FUID::toString delegates to
    // toString8, which opens with `*string = 0` and strcat()s each byte
    // (funknown.cpp:466-475), so the result IS NUL-terminated. Constructing the
    // std::string from the pointer and THEN asserting size() == 32 is what turns
    // any deviation into a named failure rather than a silently short compare.
    std::array<Steinberg::char8, 33> classIdBuffer{};
    Seraphis::kProcessorUID.toString(classIdBuffer.data());
    const std::string expectedClassId(classIdBuffer.data());
    INFO("kProcessorUID as ASCII: " << expectedClassId);
    REQUIRE(expectedClassId.size() == std::size_t{32});

    std::vector<std::string> structural;   // magic / List / chunk bounds, via parseError
    std::vector<std::string> badMagic;     // the independent re-check
    std::vector<std::string> wrongClassId;
    std::vector<std::string> missingPayload;

    for (const auto& file : files) {
        const SeraphisTest::PresetFile pf = SeraphisTest::parseVstPreset(file);

        // parseVstPreset carries FOUR of SC-003's clauses and reports each one
        // with its own named reason: the "VST3" magic, the "List" tag at the
        // declared listOffset, the presence of BOTH a `Comp` and an `Info` entry,
        // and every entry's `offset + size` lying inside the file
        // (preset_test_support.h:218-301). A file that trips any of them is
        // recorded here and SKIPPED - running the remaining clauses on a
        // half-parsed container would add misleading second failures.
        if (!pf.parseError.empty()) {
            structural.push_back(file.string() + ": " + pf.parseError);
            continue;
        }

        if (readContainerMagic(file) != "VST3") {
            badMagic.push_back(file.string());
        }

        if (pf.classIdAscii != expectedClassId) {
            wrongClassId.push_back(file.string() + ": header class id \"" + pf.classIdAscii
                                   + "\", kProcessorUID \"" + expectedClassId + "\"");
        }

        // The two payloads were copied out only AFTER parseVstPreset bounds-checked
        // their spans, so a non-empty pair is the case's own restatement of "a
        // List chunk carrying both Comp and Info, both inside the file". An
        // EMPTY-but-declared chunk passes every bounds check and would otherwise
        // reach SC-004 and SC-005 as two unrelated failures.
        if (pf.comp.empty() || pf.info.empty()) {
            missingPayload.push_back(file.string() + ": Comp " + std::to_string(pf.comp.size())
                                     + " bytes, Info " + std::to_string(pf.info.size()) + " bytes");
        }
    }

    INFO("containers that failed structural parsing: " << joinPaths(structural));
    REQUIRE(structural.empty());

    INFO("containers without the \"VST3\" magic: " << joinPaths(badMagic));
    REQUIRE(badMagic.empty());

    INFO("containers whose class id is not kProcessorUID: " << joinPaths(wrongClassId));
    REQUIRE(wrongClassId.empty());

    INFO("containers with an empty Comp or Info payload: " << joinPaths(missingPayload));
    REQUIRE(missingPayload.empty());
}

TEST_CASE("Seraphis_FactoryPresets_StreamIsCurrentVersion", "[seraphis][preset]") {
    const std::vector<std::filesystem::path> files = SeraphisTest::allPresetFiles();
    INFO("presets root: " << SeraphisTest::factoryPresetRoot().string());
    REQUIRE(!files.empty());

    // WHY BOTH CLAUSES, AND WHY THE LENGTH IS AN EXACT EQUALITY: every stream
    // version is a STRICT BYTE PREFIX of the next (plugin_ids.h:21-27) and the
    // loader chain is EOF-safe (processor.cpp:1762-1790), so a truncated v1- or
    // v2-era stream LOADS WITHOUT COMPLAINT and leaves every later block at its
    // registered default. The version int32 alone cannot catch that - a v3
    // prefix followed by 500 bytes reads as "version 3". Only `== 2868` exactly
    // rejects it, and only in combination do the two clauses say "this is the
    // whole current stream".
    std::vector<std::string> unreadable;
    std::vector<std::string> wrongLength;
    std::vector<std::string> wrongVersion;

    for (const auto& file : files) {
        const SeraphisTest::PresetFile pf = SeraphisTest::parseVstPreset(file);
        if (!pf.parseError.empty()) {
            unreadable.push_back(file.string() + ": " + pf.parseError);
            continue;
        }

        if (pf.comp.size() != kComponentChunkBytes) {
            wrongLength.push_back(file.string() + ": Comp chunk is " + std::to_string(pf.comp.size())
                                  + " bytes, expected " + std::to_string(kComponentChunkBytes));
        }

        std::int32_t version = 0;
        if (!leadingStateVersion(pf.comp, version)) {
            wrongVersion.push_back(file.string()
                                   + ": Comp chunk is shorter than the 4-byte version prefix");
        } else if (version != kExpectedStateVersion) {
            wrongVersion.push_back(file.string() + ": leading int32 = " + std::to_string(version)
                                   + ", expected " + std::to_string(kExpectedStateVersion));
        }
    }

    INFO("unreadable preset files: " << joinPaths(unreadable));
    REQUIRE(unreadable.empty());

    INFO("component chunks of the wrong length: " << joinPaths(wrongLength));
    REQUIRE(wrongLength.empty());

    INFO("component chunks that are not version 3: " << joinPaths(wrongVersion));
    REQUIRE(wrongVersion.empty());
}

TEST_CASE("Seraphis_FactoryPresets_RoundTripByteIdentical", "[seraphis][preset]") {
    const std::vector<std::filesystem::path> files = SeraphisTest::allPresetFiles();
    INFO("presets root: " << SeraphisTest::factoryPresetRoot().string());
    REQUIRE(!files.empty());

    // THIS IS A SERIALIZED-STATE-STREAM COMPARISON - the explicitly sanctioned
    // carve-out from the no-bit-exact-float-golden rule (spec FR-027a; the lint
    // at ci.yml:162-166). It is NOT a float render digest: nothing here is
    // rendered, no DSP runs, and every byte compared was produced by the SAME
    // getState() on the SAME machine in the SAME process moments earlier. The
    // claim is "setState o getState is the identity on a shipped preset", which
    // is a property of the serializer pair alone and carries no float-math
    // portability assumption whatsoever.
    //
    // A FRESH FIXTURE PER PRESET, deliberately. Reusing one processor would let
    // preset N's state survive into preset N+1 through any field setState fails
    // to overwrite - and a field that setState never writes is EXACTLY the defect
    // this case exists to catch, so a shared fixture would hide it.
    std::vector<std::string> unreadable;
    std::vector<std::string> notPrepared;
    std::vector<std::string> rejected;   // setState != kResultOk
    std::vector<std::string> reSaveFailed;
    std::vector<std::string> mismatched;

    for (const auto& file : files) {
        const SeraphisTest::PresetFile pf = SeraphisTest::parseVstPreset(file);
        if (!pf.parseError.empty()) {
            unreadable.push_back(file.string() + ": " + pf.parseError);
            continue;
        }
        if (pf.comp.size() != kComponentChunkBytes) {
            // SC-004's fault, recorded here as its own line so the memcmp below
            // really is "over 2868 bytes" and never a shorter comparison quietly
            // reporting success.
            mismatched.push_back(file.string() + ": Comp chunk is " + std::to_string(pf.comp.size())
                                 + " bytes, not " + std::to_string(kComponentChunkBytes));
            continue;
        }

        SeraphisTest::ProcessorFixture fx;
        if (fx.prepare(44100.0, 512) != Steinberg::kResultOk) {
            notPrepared.push_back(file.string());
            continue;
        }

        // MemoryStream's two-argument constructor BORROWS the buffer and starts
        // at cursor 0 (ownMemory == false, memorystream.cpp:28-37), so the local
        // mutable copy is what keeps this free of a const_cast on `pf.comp`.
        std::vector<std::uint8_t> loaded = pf.comp;
        Steinberg::MemoryStream fromFile(loaded.data(),
                                         static_cast<Steinberg::TSize>(loaded.size()));
        if (fx.proc->setState(&fromFile) != Steinberg::kResultOk) {
            rejected.push_back(file.string());
            continue;
        }

        Steinberg::MemoryStream reSavedStream;
        if (fx.proc->getState(&reSavedStream) != Steinberg::kResultOk) {
            reSaveFailed.push_back(file.string());
            continue;
        }

        const char* raw = reSavedStream.getData();
        const auto reSavedSize = static_cast<std::size_t>(reSavedStream.getSize());
        if (raw == nullptr || reSavedSize != pf.comp.size()) {
            mismatched.push_back(file.string() + ": re-saved " + std::to_string(reSavedSize)
                                 + " bytes, the file carries " + std::to_string(pf.comp.size()));
            continue;
        }

        if (std::memcmp(raw, pf.comp.data(), reSavedSize) != 0) {
            // The FIRST differing offset, not just "they differ": the block
            // layout in preset_test_support.h's decode table turns an offset
            // straight into the block that failed to survive the round trip.
            const auto* reSaved = reinterpret_cast<const std::uint8_t*>(raw);
            std::size_t at = 0;
            while (at < reSavedSize && reSaved[at] == pf.comp[at]) {
                ++at;
            }
            mismatched.push_back(file.string() + ": first differing byte at offset "
                                 + std::to_string(at));
        }
    }

    INFO("unreadable preset files: " << joinPaths(unreadable));
    REQUIRE(unreadable.empty());

    INFO("presets whose fixture failed to prepare: " << joinPaths(notPrepared));
    REQUIRE(notPrepared.empty());

    // FR-006 / SC-005's "0 failed setState calls", stated as its own assertion:
    // a rejected stream would otherwise leave `mismatched` empty and let the
    // case pass while nothing was actually round-tripped.
    INFO("presets rejected by Processor::setState: " << joinPaths(rejected));
    REQUIRE(rejected.empty());

    INFO("presets that could not be re-saved by Processor::getState: "
         << joinPaths(reSaveFailed));
    REQUIRE(reSaveFailed.empty());

    INFO("presets whose re-saved stream differs from the file: " << joinPaths(mismatched));
    REQUIRE(mismatched.empty());
}

// =============================================================================
// T010 - metadata, browser scan, tabs (SC-006, SC-007, SC-008)
// =============================================================================
// THE THREE CASES BELOW READ THE LIBRARY THROUGH THREE DIFFERENT EYES, and that
// is the point of grouping them: the embedded `Info` XML is what an EXTERNAL
// host reads, PresetManager::scanPresets() is what OUR browser reads, and the
// tab vector is what the user CLICKS. All three derive a category, and a
// disagreement between any two of them ships a preset that exists on disk but is
// unreachable, mislabelled, or invisible - none of which any earlier Phase 12
// case can see, because T002's bijection only compares DIRECTORY names and T009
// never opens the `Info` chunk at all.
//
// RED-STATE EXPECTATION: cases 1 and 3 are GREEN from T010 onward (they assert a
// property of every file found, and a config that is already correct); case 2's
// entry-count clause stays RED until T014 lands the full 42, exactly as T008's
// two cases do.

TEST_CASE("Seraphis_FactoryPresets_InfoMetadataMatchesDirectory", "[seraphis][preset]") {
    const std::vector<std::filesystem::path> files = SeraphisTest::allPresetFiles();
    INFO("presets root: " << SeraphisTest::factoryPresetRoot().string());
    REQUIRE(!files.empty());

    const Krate::Plugins::PresetManagerConfig cfg = Seraphis::makeSeraphisPresetConfig();

    // FR-003's six ids, EXACTLY - not "at least". An extra attribute is as much a
    // defect as a missing one: the generator emits a fixed six-element template
    // (tools/seraphis_preset_defs.h:207-226), so a seventh id means something
    // wrote a chunk this phase does not own, and a host that keys off the set
    // would be reading metadata nothing here validates.
    const std::set<std::string> kRequiredAttributeIds{"MediaType",       "PlugInName",
                                                      "PlugInCategory",  "Name",
                                                      "MusicalCategory", "MusicalInstrument"};

    std::vector<std::string> unreadable;
    std::vector<std::string> wrongAttributeSet;
    std::vector<std::string> valueMismatch;
    std::vector<std::string> undeclaredCategory;

    for (const auto& file : files) {
        const SeraphisTest::PresetFile pf = SeraphisTest::parseVstPreset(file);
        if (!pf.parseError.empty()) {
            unreadable.push_back(file.string() + ": " + pf.parseError);
            continue;
        }

        const std::map<std::string, std::string> attributes =
            SeraphisTest::parseInfoAttributes(pf.info);

        std::set<std::string> presentIds;
        for (const auto& attribute : attributes) {
            presentIds.insert(attribute.first);
        }
        if (presentIds != kRequiredAttributeIds) {
            wrongAttributeSet.push_back(file.string() + ": Info carries {" + joinNames(presentIds)
                                        + "}, expected {" + joinNames(kRequiredAttributeIds) + "}");
            // SKIPPED rather than checked further: every clause below is an
            // attributes.at() on an id this file has just been shown not to
            // carry, and reporting six absent-value failures on top of the one
            // structural fault buries it.
            continue;
        }

        // THE EXPECTED STRINGS ARE LITERALS, not cfg.pluginName /
        // cfg.pluginCategoryDesc. Reading the config on BOTH sides would make
        // this pass for any pair of values the config happened to hold; the
        // config's own values are pinned separately, and once, by
        // Seraphis_FactoryPresets_CategoriesMatchConfig.
        const auto expect = [&](const char* id, const std::string& expected) {
            const std::string& actual = attributes.at(id);
            if (actual != expected) {
                valueMismatch.push_back(joinText(file.string(), ": ", id, " = \"", actual,
                                                 "\", expected \"", expected, "\""));
            }
        };

        expect("MediaType", "VstPreset");
        expect("PlugInName", "Seraphis");
        expect("PlugInCategory", "Synth");

        // `Name` against the FILE STEM and the two musical ids against the PARENT
        // DIRECTORY - never against each other and never against the definition
        // table. The directory is what the runtime derives `subcategory` from
        // (preset_manager.cpp:95-103), so the directory is the side the metadata
        // has to agree with; comparing the XML to the XML would pass for a preset
        // filed under the wrong category with self-consistent metadata.
        expect("Name", pf.stem);
        expect("MusicalCategory", pf.category);
        expect("MusicalInstrument", pf.category);

        if (std::find(cfg.subcategoryNames.begin(), cfg.subcategoryNames.end(), pf.category)
            == cfg.subcategoryNames.end()) {
            undeclaredCategory.push_back(file.string() + ": parent directory \"" + pf.category
                                         + "\" is not a declared subcategory");
        }
    }

    INFO("unreadable preset files: " << joinPaths(unreadable));
    REQUIRE(unreadable.empty());

    INFO("presets whose Info chunk does not carry exactly the six FR-003 ids: "
         << joinPaths(wrongAttributeSet));
    REQUIRE(wrongAttributeSet.empty());

    INFO("Info attribute value mismatches: " << joinPaths(valueMismatch));
    REQUIRE(valueMismatch.empty());

    INFO("presets under a directory that is not a declared subcategory: "
         << joinPaths(undeclaredCategory));
    REQUIRE(undeclaredCategory.empty());
}

TEST_CASE("Seraphis_FactoryPresets_BrowserScanFilesEveryPreset", "[seraphis][preset]") {
    const std::vector<std::filesystem::path> files = SeraphisTest::allPresetFiles();
    INFO("presets root: " << SeraphisTest::factoryPresetRoot().string());
    REQUIRE(!files.empty());

    // The on-disk truth this case measures the SCANNER against. Built from the
    // same allPresetFiles() walk every other Phase 12 case uses, so a
    // disagreement is the scanner's, not a second discovery rule's.
    std::map<std::string, std::size_t> onDisk;
    for (const auto& category : kExpectedCategories) {
        onDisk[category] = 0;
    }
    std::map<std::string, std::size_t> undeclared;
    for (const auto& file : files) {
        const std::string category = file.parent_path().filename().string();
        const auto it = onDisk.find(category);
        if (it != onDisk.end()) {
            ++it->second;
        } else {
            ++undeclared[category];
        }
    }

    const TempUserPresetDir userDir;
    INFO("temporary user preset directory: " << userDir.path().string());
    REQUIRE(!userDir.path().empty());
    REQUIRE(std::filesystem::is_directory(userDir.path()));

    // Both overrides are the 4th and 5th constructor parameters
    // (plugins/shared/src/preset/preset_manager.h:55-61). The processor and
    // controller pointers are null on purpose: nothing below loads, saves or
    // deletes a preset, and a real IComponent would drag the whole VST3 host
    // surface into a case whose subject is the SCAN.
    Krate::Plugins::PresetManager manager(Seraphis::makeSeraphisPresetConfig(), nullptr, nullptr,
                                          userDir.path(), SeraphisTest::factoryPresetRoot());

    const Krate::Plugins::PresetManager::PresetList scanned = manager.scanPresets();

    std::vector<std::string> emptySubcategory;
    std::vector<std::string> notFactory;
    for (const auto& preset : scanned) {
        if (preset.subcategory.empty()) {
            // THE MEMBRUM FAILURE MODE. parsePresetFile leaves `subcategory`
            // EMPTY when the parent directory matches no configured name
            // (preset_manager.cpp:95-103) - it does not warn and it does not drop
            // the entry, so such a preset is scanned, counted, and then invisible
            // under every browser tab but "All".
            emptySubcategory.push_back(preset.path.string());
        }
        if (!preset.isFactory) {
            // The committed tree was handed in as the FACTORY override, so every
            // entry must carry isFactory == true. A false one means the entry came
            // from the user scan instead - i.e. the temp override did not take,
            // and the numbers below describe the developer's machine.
            notFactory.push_back(preset.path.string());
        }
    }

    INFO("scanned entries: " << scanned.size() << ", .vstpreset files on disk: " << files.size());
    INFO("per-category tally on disk: " << joinTally(onDisk));
    INFO("presets under UNDECLARED directories: " << joinTally(undeclared));

    // CLAUSE 1, AND IT IS FIRST: every file on disk became exactly one scanned
    // entry. This is SC-007's actual subject and it is independent of the library
    // SIZE, so it is green over the pilot presets and names a real scanner defect
    // (a dropped file, a duplicated one) rather than being masked by the count
    // clause that follows.
    REQUIRE(scanned.size() == files.size());

    // CLAUSE 2 - the library size. RED until T014; the INFO above names the count.
    REQUIRE(scanned.size() == kExpectedPresetCount);

    INFO("scanned entries with an EMPTY subcategory: " << joinPaths(emptySubcategory));
    REQUIRE(emptySubcategory.empty());

    INFO("scanned entries with isFactory == false: " << joinPaths(notFactory));
    REQUIRE(notFactory.empty());

    // CLAUSE 4 - the tab filter the browser actually calls
    // (getPresetsForSubcategory, preset_manager.h:75). Asserted per category so
    // the failure names WHICH tab is short, and against the on-disk tally rather
    // than against a fixed 6: C-2's per-category target is a report, not a gate
    // (see Seraphis_FactoryPresets_CountAndDistribution).
    for (const auto& category : kExpectedCategories) {
        const Krate::Plugins::PresetManager::PresetList filtered =
            manager.getPresetsForSubcategory(category);
        INFO("category `" << category << "`: filter returned " << filtered.size()
                          << ", on disk " << onDisk.at(category));
        REQUIRE(filtered.size() == onDisk.at(category));
    }
}

TEST_CASE("Seraphis_PresetBrowser_TabsMatchConfig", "[seraphis][preset]") {
    // THE CONTROLLER'S OWN CONSTRUCTION, INVOKED - not a copy of it.
    // Controller::togglePresetBrowser() calls exactly this function and hands the
    // result straight to PresetBrowserView
    // (plugins/seraphis/src/controller/controller.cpp:462-467), so an edit to the
    // tab list on the controller side now fails HERE. The construction was moved
    // into seraphis_preset_config.h for precisely that reason: togglePresetBrowser
    // itself needs a live CFrame and an open editor and cannot be called from a
    // headless test, and the re-typed copy this case used to hold would have kept
    // passing while the shipped vector drifted.
    const std::vector<std::string> tabLabels = Seraphis::makeSeraphisPresetTabLabels();

    // THE COMPARISON IS AGAINST A LITERAL EIGHT-ELEMENT VECTOR, NEVER AGAINST
    // {"All"} + config.subcategoryNames. Both sides of THAT comparison read the
    // same config, which makes it a tautology that passes for a wrong, reordered
    // or truncated list - the exact defect SC-008 exists to catch. This literal is
    // the second, independent copy that makes the assertion able to fail.
    const std::vector<std::string> kExpectedTabs{"All",   "Textures", "Pads",   "Drones",
                                                 "Bells", "Choirs",   "Motion", "Cinematic"};

    INFO("tab labels: " << joinPaths(tabLabels));
    REQUIRE(tabLabels.size() == std::size_t{8});

    // Element-wise, so a reordering names the INDEX that moved instead of
    // reporting one opaque whole-vector inequality. The order is not cosmetic: it
    // is the vertical tab order the shared CategoryTabBar lays out
    // (category_tab_bar.cpp:25-36).
    for (std::size_t i = 0; i < kExpectedTabs.size(); ++i) {
        INFO("tab " << i << ": got \"" << tabLabels[i] << "\", expected \"" << kExpectedTabs[i]
                    << "\"");
        REQUIRE(tabLabels[i] == kExpectedTabs[i]);
    }

    // Asserted SEPARATELY so the "All"-comes-first clause names itself on failure:
    // an "All" tab that drifted to any other index still filters correctly (the
    // empty-string filter returns everything, preset_manager.cpp:108-112) and
    // would fail only as one more index mismatch above.
    REQUIRE(tabLabels[0] == "All");
}

// =============================================================================
// T011 - the authoring ledgers (SC-013, SC-014, SC-014a, FR-006a + FR-009)
// =============================================================================
// THESE FOUR CASES ARE WHAT MAKE T014's AUTHORING VERIFIED AS IT GOES rather than
// a search. Every one of them reads the preset through decodePresetState (T007) -
// never from tools/seraphis_preset_defs.h, and never by re-deriving a
// denormalized value with arithmetic:
//
//   * reading the DEFINITION TABLE would assert that the author wrote what the
//     author wrote. The whole chain under test is {ParamID, normalized} -> the
//     shipped handle*ParamChange denormalizers -> getState() -> the file, and
//     only the decoded FILE is downstream of all three.
//   * re-deriving with arithmetic (e.g. `polyphony == round(norm*15+1)`) would
//     put a second copy of global_params.h:99-101 in a test and then check the
//     copy against itself.
//
// RED-STATE EXPECTATION: cases 2, 3 and 4 are GREEN from T011 onward - they
// assert a PROPERTY OF EVERY PRESET FOUND, so they hold over T004's three pilot
// presets exactly as they will over T014's 42. Case 1 is the exception and is
// RED until T014: coverage is a property of the LIBRARY AS A WHOLE, and three
// pilot presets cannot reach 5 body materials or both envelope modes. Its
// failure message names each missing value and its ParamID, which is precisely
// the worklist T014 authors against.

TEST_CASE("Seraphis_FactoryPresets_CoversShippedSurface", "[seraphis][preset]") {
    const std::vector<std::filesystem::path> files = SeraphisTest::allPresetFiles();
    INFO("presets root: " << SeraphisTest::factoryPresetRoot().string());
    REQUIRE(!files.empty());

    // The coverage ledger. Sets rather than counters: C-2 asks whether a VALUE
    // appears at all, never how often, and a set makes the missing-value
    // subtraction below a one-liner that can name what it is missing.
    std::set<int> bodyMaterial;       // ID 800
    std::set<int> spectralSlot;       // IDs 409-412
    std::set<int> morphStateCount;    // ID 408 - stores the COUNT, not the index
    std::set<int> travelMode;         // ID 403
    std::set<int> envMode;            // ID 700
    std::set<int> grainEnvelope;      // ID 1016
    std::set<bool> atmosFreeze;       // ID 1008
    std::set<bool> aetherFreeze;      // ID 1204
    std::set<bool> fxSpectralFreeze;  // ID 1430
    std::set<bool> delaySync;         // ID 1418
    std::set<bool> resonatorBypass;   // ID 812
    std::set<bool> inputAgc;          // ID 811

    std::vector<std::string> failures;
    forEachDecodedPreset(
        files, failures,
        [&](const SeraphisTest::PresetFile&, const SeraphisTest::DecodedPresetState& st) {
            bodyMaterial.insert(st.body.material.load(kRlx));

            // ALL FOUR SLOTS, not just the `stateCount` active ones. C-2 states
            // this row on the IDs (409-412) and every one of the four is a
            // registered, automatable, host-visible parameter whose stored value
            // is what a defect in kSpectralStateLabels / toSpectralStateId would
            // corrupt - reachable from the preset regardless of how many slots
            // the morph engine currently interpolates between.
            for (const auto& slot : st.morph.slot) {
                spectralSlot.insert(slot.load(kRlx));
            }

            morphStateCount.insert(st.morph.stateCount.load(kRlx));
            travelMode.insert(st.morph.travelMode.load(kRlx));
            envMode.insert(st.life.envMode.load(kRlx));
            grainEnvelope.insert(st.atmos.grainEnvelope.load(kRlx));
            atmosFreeze.insert(st.atmos.freeze.load(kRlx));
            aetherFreeze.insert(st.aether.freeze.load(kRlx));
            fxSpectralFreeze.insert(st.effects.spectralFreeze.load(kRlx));
            delaySync.insert(st.effects.delaySync.load(kRlx));
            resonatorBypass.insert(st.body.resonatorBypass.load(kRlx));
            inputAgc.insert(st.body.inputAgc.load(kRlx));
        });

    INFO("presets that could not be parsed or decoded: " << joinPaths(failures));
    REQUIRE(failures.empty());

    // ONE `missing` LIST, ASSERTED ONCE AT THE END. Splitting this into nine
    // REQUIREs would abort at the first uncovered surface and report a single
    // missing value per run - turning T014's authoring into nine build-run
    // rounds instead of one worklist.
    std::vector<std::string> missing;

    const auto idText = [](Steinberg::Vst::ParamID id) {
        return std::to_string(static_cast<unsigned long>(id));
    };

    // The index-enumerated surfaces: every index of the shipped label table must
    // appear in at least one preset, and the failure NAMES THE LABEL, not just
    // the index.
    const auto requireEveryIndex = [&missing, &idText](const char* surface,
                                                       Steinberg::Vst::ParamID paramId,
                                                       const std::vector<std::string>& labels,
                                                       const std::set<int>& seen) {
        for (std::size_t i = 0; i < labels.size(); ++i) {
            if (!seen.contains(static_cast<int>(i))) {
                missing.push_back(std::string(surface) + " (ID " + idText(paramId) + "): \""
                                  + labels[i] + "\" (index " + std::to_string(i)
                                  + ") appears in no preset");
            }
        }
    };

    requireEveryIndex("Body material", Seraphis::kBodyMaterialId, kBodyMaterialNames, bodyMaterial);
    // Reported against ID 409 as the head of the 409-412 run: the ledger is the
    // union over all four slots, so no single one of the four owns the gap.
    requireEveryIndex("Factory spectral state (IDs 409-412)", Seraphis::kMorphState0Id,
                      kSpectralStateNames, spectralSlot);
    requireEveryIndex("Travel mode", Seraphis::kMorphTravelModeId, kTravelModeNames, travelMode);
    requireEveryIndex("Envelope mode", Seraphis::kEnvModeId, kEnvelopeModeNames, envMode);

    // ID 408 STORES THE COUNT, NOT THE LIST INDEX (morph_params.h:96-101,
    // :120 - `stateCount{kMorphStateCountMin + kMorphStateCountDefaultIndex}`),
    // so the expected values are 2, 3, 4 and NOT 0, 1, 2. Asserting indices here
    // would demand a stored `0`, which the shipped denormalizer cannot produce -
    // an un-satisfiable gate that would have blocked T014 forever.
    for (const int count : {2, 3, 4}) {
        if (!morphStateCount.contains(count)) {
            missing.push_back("Morph state count (ID " + idText(Seraphis::kMorphStateCountId)
                              + "): count "
                              + std::to_string(count) + " appears in no preset");
        }
    }

    // The toggles: each ON in >= 1 preset AND OFF in >= 1 preset. An all-OFF
    // library never exercises the freeze paths at all; an all-ON one never
    // exercises the ordinary ones.
    const auto requireBothPolarities = [&missing, &idText](const char* surface,
                                                           Steinberg::Vst::ParamID paramId,
                                                           const std::set<bool>& seen) {
        if (!seen.contains(true)) {
            missing.push_back(std::string(surface) + " (ID " + idText(paramId)
                              + "): never ON in any preset");
        }
        if (!seen.contains(false)) {
            missing.push_back(std::string(surface) + " (ID " + idText(paramId)
                              + "): never OFF in any preset");
        }
    };

    requireBothPolarities("Atmosphere freeze", Seraphis::kAtmosFreezeId, atmosFreeze);
    requireBothPolarities("Aether freeze", Seraphis::kAetherFreezeId, aetherFreeze);
    requireBothPolarities("FX spectral freeze", Seraphis::kFxSpectralFreezeId, fxSpectralFreeze);
    requireBothPolarities("Delay sync", Seraphis::kFxDelaySyncId, delaySync);
    requireBothPolarities("Body resonator bypass", Seraphis::kBodyResonatorBypassId,
                          resonatorBypass);
    requireBothPolarities("Body input AGC", Seraphis::kBodyInputAgcId, inputAgc);

    // The one PARTIAL-coverage row in C-2: >= 3 of the 6 grain windows, not all
    // six. Folded into `missing` rather than asserted separately so a shortfall
    // here does not abort the case before the rows above are reported.
    if (grainEnvelope.size() < kGrainEnvelopeFloor) {
        missing.push_back("Grain envelope (ID " + idText(Seraphis::kAtmosGrainEnvelopeId)
                          + "): only "
                          + std::to_string(grainEnvelope.size()) + " of "
                          + std::to_string(kGrainEnvelopeNames.size())
                          + " window types used, floor is " + std::to_string(kGrainEnvelopeFloor)
                          + " (indices seen: " + joinInts(grainEnvelope) + ")");
    }

    INFO("presets examined: " << files.size());
    INFO("body material indices covered: " << joinInts(bodyMaterial));
    INFO("factory spectral-state indices covered: " << joinInts(spectralSlot));
    INFO("morph state counts covered: " << joinInts(morphStateCount));
    INFO("travel modes covered: " << joinInts(travelMode));
    INFO("envelope modes covered: " << joinInts(envMode));
    INFO("grain envelope indices covered: " << joinInts(grainEnvelope));
    INFO("UNCOVERED surface values (" << missing.size() << "):\n  " << joinPaths(missing));
    REQUIRE(missing.empty());
}

TEST_CASE("Seraphis_FactoryPresets_RespectVoiceBudget", "[seraphis][preset]") {
    const std::vector<std::filesystem::path> files = SeraphisTest::allPresetFiles();
    INFO("presets root: " << SeraphisTest::factoryPresetRoot().string());
    REQUIRE(!files.empty());

    // C-5. The value read here is the one loadGlobalParams stored through
    // clampPolyphony (global_params.h:226-228), i.e. the value the plugin will
    // actually run at - not the raw stream int32 and not a re-derivation of
    // handleGlobalParamChange's `clamp(value*15 + 1 + 0.5, 1, 16)`.
    std::vector<std::string> overBudget;
    std::vector<std::string> softLimitOff;

    std::vector<std::string> failures;
    forEachDecodedPreset(
        files, failures,
        [&](const SeraphisTest::PresetFile& pf, const SeraphisTest::DecodedPresetState& st) {
            const int polyphony = st.global.polyphony.load(kRlx);
            if (polyphony > kMaxFactoryPolyphony) {
                overBudget.push_back(pf.path.string() + ": polyphony " + std::to_string(polyphony)
                                     + " > " + std::to_string(kMaxFactoryPolyphony));
            }
            if (!st.global.softLimit.load(kRlx)) {
                softLimitOff.push_back(pf.path.string());
            }
        });

    INFO("presets that could not be parsed or decoded: " << joinPaths(failures));
    REQUIRE(failures.empty());

    // The two clauses are asserted SEPARATELY: they are two different authoring
    // mistakes with two different fixes (re-author the voice count vs. re-author
    // the output stage), and one combined list would not say which.
    INFO("presets storing more than " << kMaxFactoryPolyphony
                                      << " voices: " << joinPaths(overBudget));
    REQUIRE(overBudget.empty());

    INFO("presets storing soft limit OFF: " << joinPaths(softLimitOff));
    REQUIRE(softLimitOff.empty());
}

TEST_CASE("Seraphis_FactoryPresets_RespectTimingCeiling", "[seraphis][preset]") {
    const std::vector<std::filesystem::path> files = SeraphisTest::allPresetFiles();
    INFO("presets root: " << SeraphisTest::factoryPresetRoot().string());
    REQUIRE(!files.empty());

    // FR-008a / SC-014a. THE CEILING BOUNDS WHAT SHIPS, NOT WHAT IS MEASURED:
    // every sweep arm still renders the real, unmodified preset on the real
    // C-6.1 timeline, so a breach here is re-authored - never "measured with a
    // capped A". Capping A in the harness would silently shorten the very
    // envelope the preset ships and make the sustain window land inside the
    // attack.
    //
    // A and Rel come from makeTimeline (preset_test_support.h:530-567), the ONE
    // implementation of C-6.1's arithmetic, so this case and the sweep can never
    // disagree about what A is.
    std::vector<std::string> attackBreaches;
    std::vector<std::string> releaseBreaches;

    std::vector<std::string> failures;
    forEachDecodedPreset(
        files, failures,
        [&](const SeraphisTest::PresetFile& pf, const SeraphisTest::DecodedPresetState& st) {
            const SeraphisTest::SweepTimeline t = SeraphisTest::makeTimeline(st);

            // The four decoded inputs, printed on EVERY breach: `A` alone does
            // not say whether the author must shorten the growth duration or
            // stage 1, and which of the two even contributes depends on envMode
            // (dropdown_mappings.h:190-191 - Standard = 0, Growth = 1).
            const std::string decoded =
                " [envMode=" + std::to_string(st.life.envMode.load(kRlx))
                + " growthDurationSec=" + std::to_string(st.life.growthDurationSec.load(kRlx))
                + " stage0Ms=" + std::to_string(st.life.stage0Ms.load(kRlx))
                + " stage1Ms=" + std::to_string(st.life.stage1Ms.load(kRlx))
                + " releaseMs=" + std::to_string(st.life.releaseMs.load(kRlx)) + "]";

            if (t.A > kMaxAttackSeconds) {
                attackBreaches.push_back(pf.path.string() + ": A = " + std::to_string(t.A)
                                         + " s > " + std::to_string(kMaxAttackSeconds) + " s"
                                         + decoded);
            }
            if (t.Rel > kMaxReleaseSeconds) {
                releaseBreaches.push_back(pf.path.string() + ": Rel = " + std::to_string(t.Rel)
                                          + " s > " + std::to_string(kMaxReleaseSeconds) + " s"
                                          + decoded);
            }
        });

    INFO("presets that could not be parsed or decoded: " << joinPaths(failures));
    REQUIRE(failures.empty());

    INFO("presets breaching the attack ceiling: " << joinPaths(attackBreaches));
    REQUIRE(attackBreaches.empty());

    INFO("presets breaching the release ceiling: " << joinPaths(releaseBreaches));
    REQUIRE(releaseBreaches.empty());
}

TEST_CASE("Seraphis_FactoryPresets_PartialsBlockIsInert", "[seraphis][preset]") {
    const std::vector<std::filesystem::path> files = SeraphisTest::allPresetFiles();
    INFO("presets root: " << SeraphisTest::factoryPresetRoot().string());
    REQUIRE(!files.empty());

    // -------------------------------------------------------------------------
    // WHY FR-009's FINITENESS CLAUSE IS A TYPED ENUMERATION AND NOT A 4-BYTE WALK
    // -------------------------------------------------------------------------
    // A walk over the 2868-byte chunk reinterpreting every aligned quadruple as a
    // float is WRONG for 2164 of those bytes. One SpectralState payload is a
    // uint8 version at offset 0, an int32 numPartials at 1, two scalars at 5 and
    // 9, two 64-float arrays at 13 and 269, and a SIXTEEN-BYTE `name` char array
    // at 525-540 (spectral_state.h:57-62, layout at :192-205). A walker would
    // read four letters of a state name as a float, and the leading state-version
    // int32 and the two [partials] uint64 masks the same way - producing failures
    // on correct presets and, worse, silently "passing" a genuinely corrupt
    // payload whose garbage happened to land on a finite bit pattern.
    //
    // The enumeration below is reconciled against the SHARED per-block field
    // census `kExpectedFieldCounts` (declared in this TU's anonymous namespace,
    // with the full byte-width derivation), so that a float added to any pack in
    // a later phase fails the COUNT here rather than silently escaping the
    // finiteness check. That table is shared with
    // Seraphis_FactoryPresets_TreeMatchesGenerator (T021) deliberately: two
    // private copies would let one case grow a field the other never sees.
    //
    // EXPLICITLY EXCLUDED because they are not floats: numPartials, the payload
    // version byte, the 16-byte name, the leading state-version int32, and the
    // two uint64 bitmasks.
    //
    // THIS DELIBERATELY DUPLICATES PART OF isValidSpectralState's COVERAGE
    // (spectral_state.h:82-145). That predicate is COMPOSITE - finiteness plus
    // ranges plus strict ratio monotonicity plus the name's printability - so a
    // `false` from it does not name the finiteness clause. Do not collapse the
    // two into one call.

    std::vector<std::string> nonFinite;
    std::vector<std::string> nonZeroPan;
    std::vector<std::string> panOutOfRange;
    std::vector<std::string> liveBitmask;
    std::vector<std::string> badNumPartials;
    std::vector<std::string> fieldCountMismatch;

    // The field COUNTS are a property of the shipped structs, identical for every
    // preset, so they are reconciled ONCE. Checking them per preset would emit 42
    // copies of the same line and bury the per-preset finiteness failures the
    // list is really for.
    bool fieldCountsReconciled = false;

    std::vector<std::string> failures;
    forEachDecodedPreset(
        files, failures,
        [&](const SeraphisTest::PresetFile& pf, const SeraphisTest::DecodedPresetState& st) {
            const std::string where = pf.path.string();

            std::map<std::string, std::size_t> counts;
            for (const auto& entry : kExpectedFieldCounts) {
                counts[entry.block] = 0;  // seeded, so a block that enumerated
                                          // NOTHING reports 0 rather than vanishing
            }

            const auto checkFloat = [&](const char* block, const char* member, float value) {
                ++counts[block];
                if (!isFiniteFloat(value)) {
                    nonFinite.push_back(where + ": " + block + "." + member + " is non-finite");
                }
            };
            // A 4-byte field that is NOT a float: counted so the block width
            // reconciles, never bit-pattern-checked (an int32 is finite by
            // construction and every bit pattern is a legal one).
            const auto countIntField = [&counts](const char* block) { ++counts[block]; };

            // --- [global] 12 B ------------------------------------------------
            checkFloat("[global]", "masterGain", st.global.masterGain.load(kRlx));
            countIntField("[global]");  // polyphony (int32)
            countIntField("[global]");  // softLimit (int32)

            // --- [macro] 20 B -------------------------------------------------
            checkFloat("[macro]", "dream", st.macro.dream.load(kRlx));
            checkFloat("[macro]", "bloom", st.macro.bloom.load(kRlx));
            checkFloat("[macro]", "dissolve", st.macro.dissolve.load(kRlx));
            checkFloat("[macro]", "gravity", st.macro.gravity.load(kRlx));
            checkFloat("[macro]", "entropy", st.macro.entropy.load(kRlx));

            // --- [seed] 4 B ---------------------------------------------------
            countIntField("[seed]");  // seedIndex (int32)

            // --- [cloud] 44 B -------------------------------------------------
            checkFloat("[cloud]", "richness", st.cloud.richness.load(kRlx));
            checkFloat("[cloud]", "inharmonicity", st.cloud.inharmonicity.load(kRlx));
            checkFloat("[cloud]", "tiltDbPerOct", st.cloud.tiltDbPerOct.load(kRlx));
            checkFloat("[cloud]", "mutation", st.cloud.mutation.load(kRlx));
            checkFloat("[cloud]", "gravity", st.cloud.gravity.load(kRlx));
            checkFloat("[cloud]", "driftDepthCents", st.cloud.driftDepthCents.load(kRlx));
            checkFloat("[cloud]", "driftSmoothness", st.cloud.driftSmoothness.load(kRlx));
            checkFloat("[cloud]", "stereoSpread", st.cloud.stereoSpread.load(kRlx));
            checkFloat("[cloud]", "attackSec", st.cloud.attackSec.load(kRlx));
            checkFloat("[cloud]", "decaySec", st.cloud.decaySec.load(kRlx));
            checkFloat("[cloud]", "envOffsetSpread", st.cloud.envOffsetSpread.load(kRlx));

            // --- [morph] 52 B of scalars (the 2164 B of payloads follow) -------
            checkFloat("[morph]", "entropy", st.morph.entropy.load(kRlx));
            checkFloat("[morph]", "bloom", st.morph.bloom.load(kRlx));
            checkFloat("[morph]", "position", st.morph.position.load(kRlx));
            checkFloat("[morph]", "travelRate", st.morph.travelRate.load(kRlx));
            checkFloat("[morph]", "waypointSeconds", st.morph.waypointSeconds.load(kRlx));
            countIntField("[morph]");  // travelMode (int32)
            countIntField("[morph]");  // sync (int32)
            countIntField("[morph]");  // syncNote (int32)
            countIntField("[morph]");  // stateCount (int32)
            for (std::size_t i = 0; i < st.morph.slot.size(); ++i) {
                countIntField("[morph]");  // slot[i] (int32)
            }

            // --- [life] 40 B --------------------------------------------------
            checkFloat("[life]", "spatialDepth", st.life.spatialDepth.load(kRlx));
            checkFloat("[life]", "spatialRateHz", st.life.spatialRateHz.load(kRlx));
            checkFloat("[life]", "spatialCoupling", st.life.spatialCoupling.load(kRlx));
            checkFloat("[life]", "spatialGrowth", st.life.spatialGrowth.load(kRlx));
            checkFloat("[life]", "voiceWidthPercent", st.life.voiceWidthPercent.load(kRlx));
            checkFloat("[life]", "growthDurationSec", st.life.growthDurationSec.load(kRlx));
            checkFloat("[life]", "stage0Ms", st.life.stage0Ms.load(kRlx));
            checkFloat("[life]", "stage1Ms", st.life.stage1Ms.load(kRlx));
            checkFloat("[life]", "releaseMs", st.life.releaseMs.load(kRlx));
            countIntField("[life]");  // envMode (int32)

            // --- [body] 52 B --------------------------------------------------
            checkFloat("[body]", "resonance", st.body.resonance.load(kRlx));
            checkFloat("[body]", "damping", st.body.damping.load(kRlx));
            checkFloat("[body]", "keyTracking", st.body.keyTracking.load(kRlx));
            checkFloat("[body]", "drive", st.body.drive.load(kRlx));
            checkFloat("[body]", "mix", st.body.mix.load(kRlx));
            checkFloat("[body]", "cloudMix", st.body.cloudMix.load(kRlx));
            checkFloat("[body]", "cloudDecaySec", st.body.cloudDecaySec.load(kRlx));
            checkFloat("[body]", "cloudSize", st.body.cloudSize.load(kRlx));
            checkFloat("[body]", "cloudDamping", st.body.cloudDamping.load(kRlx));
            checkFloat("[body]", "width", st.body.width.load(kRlx));
            countIntField("[body]");  // material (int32)
            countIntField("[body]");  // inputAgc (int32)
            countIntField("[body]");  // resonatorBypass (int32)

            // --- [atmos] 68 B -------------------------------------------------
            checkFloat("[atmos]", "level", st.atmos.level.load(kRlx));
            checkFloat("[atmos]", "blur", st.atmos.blur.load(kRlx));
            checkFloat("[atmos]", "density", st.atmos.density.load(kRlx));
            checkFloat("[atmos]", "grainSeconds", st.atmos.grainSeconds.load(kRlx));
            checkFloat("[atmos]", "driftDepth", st.atmos.driftDepth.load(kRlx));
            checkFloat("[atmos]", "panSpread", st.atmos.panSpread.load(kRlx));
            checkFloat("[atmos]", "decorrelation", st.atmos.decorrelation.load(kRlx));
            checkFloat("[atmos]", "freezeMix", st.atmos.freezeMix.load(kRlx));
            checkFloat("[atmos]", "driftSmoothness", st.atmos.driftSmoothness.load(kRlx));
            checkFloat("[atmos]", "driftRangeSemitones", st.atmos.driftRangeSemitones.load(kRlx));
            checkFloat("[atmos]", "jitter", st.atmos.jitter.load(kRlx));
            checkFloat("[atmos]", "positionSeconds", st.atmos.positionSeconds.load(kRlx));
            checkFloat("[atmos]", "positionSpread", st.atmos.positionSpread.load(kRlx));
            checkFloat("[atmos]", "pitchSemitones", st.atmos.pitchSemitones.load(kRlx));
            checkFloat("[atmos]", "pitchSpread", st.atmos.pitchSpread.load(kRlx));
            countIntField("[atmos]");  // freeze (int32)
            countIntField("[atmos]");  // grainEnvelope (int32)

            // --- [aether] 72 B ------------------------------------------------
            checkFloat("[aether]", "mix", st.aether.mix.load(kRlx));
            checkFloat("[aether]", "size", st.aether.size.load(kRlx));
            checkFloat("[aether]", "density", st.aether.density.load(kRlx));
            checkFloat("[aether]", "decaySeconds", st.aether.decaySeconds.load(kRlx));
            checkFloat("[aether]", "dimensionality", st.aether.dimensionality.load(kRlx));
            checkFloat("[aether]", "damping", st.aether.damping.load(kRlx));
            checkFloat("[aether]", "preDelayMs", st.aether.preDelayMs.load(kRlx));
            checkFloat("[aether]", "modDepth", st.aether.modDepth.load(kRlx));
            checkFloat("[aether]", "modSmoothness", st.aether.modSmoothness.load(kRlx));
            checkFloat("[aether]", "shimmerOctave", st.aether.shimmerOctave.load(kRlx));
            checkFloat("[aether]", "shimmerFifth", st.aether.shimmerFifth.load(kRlx));
            checkFloat("[aether]", "bloomSend", st.aether.bloomSend.load(kRlx));
            checkFloat("[aether]", "bloomDecay", st.aether.bloomDecay.load(kRlx));
            checkFloat("[aether]", "spectralDiffusion", st.aether.spectralDiffusion.load(kRlx));
            checkFloat("[aether]", "sizeBreathDepth", st.aether.sizeBreathDepth.load(kRlx));
            checkFloat("[aether]", "tideDepth", st.aether.tideDepth.load(kRlx));
            checkFloat("[aether]", "width", st.aether.width.load(kRlx));
            countIntField("[aether]");  // freeze (int32)

            // --- [effects] 64 B -----------------------------------------------
            checkFloat("[effects]", "saturation", st.effects.saturation.load(kRlx));
            checkFloat("[effects]", "delayMix", st.effects.delayMix.load(kRlx));
            checkFloat("[effects]", "delayTimeMs", st.effects.delayTimeMs.load(kRlx));
            checkFloat("[effects]", "delaySpreadMs", st.effects.delaySpreadMs.load(kRlx));
            checkFloat("[effects]", "delayFeedback", st.effects.delayFeedback.load(kRlx));
            checkFloat("[effects]", "delayTilt", st.effects.delayTilt.load(kRlx));
            checkFloat("[effects]", "delayDiffusion", st.effects.delayDiffusion.load(kRlx));
            checkFloat("[effects]", "delayWidth", st.effects.delayWidth.load(kRlx));
            checkFloat("[effects]", "width", st.effects.width.load(kRlx));
            checkFloat("[effects]", "wanderDepth", st.effects.wanderDepth.load(kRlx));
            checkFloat("[effects]", "wanderRate", st.effects.wanderRate.load(kRlx));
            checkFloat("[effects]", "azimuthDepth", st.effects.azimuthDepth.load(kRlx));
            countIntField("[effects]");  // spreadDirection (int32)
            countIntField("[effects]");  // delaySync (int32)
            countIntField("[effects]");  // delaySyncNote (int32)
            countIntField("[effects]");  // spectralFreeze (int32)

            if (!fieldCountsReconciled) {
                fieldCountsReconciled = true;
                for (const auto& entry : kExpectedFieldCounts) {
                    const std::size_t enumerated = counts.at(entry.block);
                    if (enumerated != entry.fields) {
                        fieldCountMismatch.push_back(
                            std::string(entry.block) + ": enumerated " + std::to_string(enumerated)
                            + " fields, the shipped block is " + std::to_string(entry.fields)
                            + " (a field added to this pack must be added to the enumeration in "
                              "Seraphis_FactoryPresets_PartialsBlockIsInert, not just here)");
                    }
                }
            }

            // --- the four SpectralState payloads ------------------------------
            // ratios[i] / amplitudes[i] for i < numPartials ONLY: entries at
            // i >= numPartials are scratch space the morph engine's geometric
            // continuation owns and never initialises (spectral_state.h:78-79),
            // so checking them would fail correct presets.
            const auto maxPartials =
                static_cast<int>(Krate::DSP::SpectralState::kStatePartials);
            for (std::size_t s = 0; s < st.payloads.size(); ++s) {
                const Krate::DSP::SpectralState& payload = st.payloads[s];
                const std::string slot = "payload[" + std::to_string(s) + "]";

                const int declared = payload.numPartials;
                if (declared < 0 || declared > maxPartials) {
                    // Recorded rather than clamped-and-forgotten: an out-of-range
                    // count is what makes the loop bound below meaningful, and it
                    // is a defect in its own right.
                    badNumPartials.push_back(joinText(where, ": ", slot, ".numPartials = ",
                                                      std::to_string(declared), ", outside [0, ",
                                                      std::to_string(maxPartials), "]"));
                }
                const int active = std::clamp(declared, 0, maxPartials);

                for (int i = 0; i < active; ++i) {
                    const auto index = static_cast<std::size_t>(i);
                    if (!isFiniteFloat(payload.ratios[index])) {
                        nonFinite.push_back(joinText(where, ": ", slot, ".ratios[",
                                                     std::to_string(i), "] is non-finite"));
                    }
                    if (!isFiniteFloat(payload.amplitudes[index])) {
                        nonFinite.push_back(joinText(where, ": ", slot, ".amplitudes[",
                                                     std::to_string(i), "] is non-finite"));
                    }
                }
                if (!isFiniteFloat(payload.tiltDbPerOct)) {
                    nonFinite.push_back(joinText(where, ": ", slot, ".tiltDbPerOct is non-finite"));
                }
                if (!isFiniteFloat(payload.inharmonicity)) {
                    nonFinite.push_back(joinText(where, ": ", slot, ".inharmonicity is non-finite"));
                }
            }

            // --- [partials] 272 B: FR-006a + FR-009's range clause -------------
            // Read RAW by decodePresetState - NOT through loadPartialOverrides,
            // whose NaN scrub and [-1, 1] clamp (processor.cpp:482-485) would
            // turn both assertions below into tautologies that pass on a corrupt
            // preset (preset_test_support.h banner rule 3).
            for (std::size_t i = 0; i < st.partialPan.size(); ++i) {
                const float pan = st.partialPan[i];
                if (!isFiniteFloat(pan)) {
                    nonFinite.push_back(where + ": [partials].pan[" + std::to_string(i)
                                        + "] is non-finite");
                    continue;  // a non-finite value compares FALSE against every
                               // bound, so the two clauses below would add two
                               // more misleading lines for one fault
                }
                if (pan != 0.0f) {
                    nonZeroPan.push_back(where + ": [partials].pan[" + std::to_string(i) + "] = "
                                         + std::to_string(pan) + ", expected 0.0");
                }
                if (pan < -1.0f || pan > 1.0f) {
                    panOutOfRange.push_back(where + ": [partials].pan[" + std::to_string(i)
                                            + "] = " + std::to_string(pan) + ", outside [-1, 1]");
                }
            }

            if (st.panOverrideBits != 0u) {
                liveBitmask.push_back(where + ": panOverrideBits = "
                                      + std::to_string(st.panOverrideBits) + ", expected 0");
            }
            if (st.maskBits != 0u) {
                liveBitmask.push_back(where + ": maskBits = " + std::to_string(st.maskBits)
                                      + ", expected 0");
            }
        });

    INFO("presets that could not be parsed or decoded: " << joinPaths(failures));
    REQUIRE(failures.empty());

    // FIRST: a count drift means the enumeration above no longer covers the
    // shipped structs, so every finiteness verdict below is over an INCOMPLETE
    // field set and would be reported as a pass.
    INFO("per-block field-count reconciliation: " << joinPaths(fieldCountMismatch));
    REQUIRE(fieldCountMismatch.empty());

    INFO("payloads with an out-of-range numPartials: " << joinPaths(badNumPartials));
    REQUIRE(badNumPartials.empty());

    // FR-006a, both clauses, asserted separately from the pans: a live bitmask
    // with all-zero pans and all-zero pans with a live bitmask are two different
    // generator faults.
    INFO("presets with a non-zero [partials] bitmask: " << joinPaths(liveBitmask));
    REQUIRE(liveBitmask.empty());

    INFO("presets with a non-zero [partials] pan: " << joinPaths(nonZeroPan));
    REQUIRE(nonZeroPan.empty());

    INFO("presets with a [partials] pan outside [-1, 1]: " << joinPaths(panOutOfRange));
    REQUIRE(panOutOfRange.empty());

    INFO("non-finite stored floats: " << joinPaths(nonFinite));
    REQUIRE(nonFinite.empty());
}

// =============================================================================
// T020 - the FR-029a tolerance probe (REPORT MODE, gates nothing)
// =============================================================================
// Reference: specs/seraphis-phase12-presets-release/spec.md (FR-029, FR-029a,
//            SC-017; Clarifications Q3 / CQ-3 Option B + C)
//            specs/seraphis-phase12-presets-release/tasks.md (T020)
//
// WHAT THIS CASE IS FOR. FR-029's committed-tree comparison (T021) needs TWO
// float tolerances, and FR-029a forbids writing either one down before it has
// been MEASURED: "No tolerance may be written down before this task produces a
// number." This case produces those numbers. It asserts only STRUCTURAL
// preconditions (every preset parses, has a definition, decodes, regenerates and
// enumerates the full field set); every measured quantity leaves through WARN.
//
// TAGGED `[.measure]` - the leading dot excludes it from the default run. It
// regenerates all 42 presets in-process (42 x prepare + one 64-sample block), so
// it is a deliberate, explicitly invoked measurement, not a per-commit cost:
//
//     seraphis_tests.exe "Seraphis_FactoryPresets_TreeToleranceProbe"
//
// THE REGENERATION IS THE GENERATOR'S DRIVE SEQUENCE, NOT A SECOND ONE (R-2 /
// OI-5). regenerateComponentState() below is line-for-line
// tools/seraphis_preset_generator.cpp:120-171's captureComponentState, including
// `setEffectsStageInstrumentedForTest(false)` immediately after prepare()
// (:130-137 - prepare() turns it ON for every Seraphis test,
// seraphis_test_fixture.h:210, and the shipping/release configuration is OFF,
// processor.h:1466-1470). If the two sides of the comparison ran different
// configurations, FR-029 would be comparing two different things and would pass
// while proving nothing. T021 REUSES this helper for exactly that reason - it
// must never grow a second copy.
//
// THE METRIC. Per field, the relative error against the LARGER magnitude:
//
//     err(c, r) = 0                            when c == r exactly (covers 0/0)
//               = |c - r| / max(|c|, |r|)      otherwise
//
// No epsilon is added to the denominator. A denominator guard would quietly turn
// a pathological pair (one side 0, the other a denormal) into a small number and
// inflate the pinned tolerance with a fault nobody looked at; instead the worst
// pair's field name AND both values are printed, so anything outside FR-029a's
// sanity band (scalar near 1e-6, payload near 1e-4) is INVESTIGATED, never
// widened.
//
// THE TWO CLASSES ARE MEASURED SEPARATELY AND MAY NOT SHARE ONE NUMBER (Q3):
//   * SCALAR  - every `float` member of the nine decoded packs. These are single
//               denormalizer outputs; the canonical case is Aether decay, one
//               std::pow through logMapFromNormalized = clamp(mn * pow(mx/mn, u),
//               mn, mx) (aether_params.h:111-116, parameter_helpers.h:80-83).
//   * PAYLOAD - the four SpectralStates' ratios[i] / amplitudes[i] (i <
//               numPartials), tiltDbPerOct and inharmonicity. These are
//               makeFactoryState outputs - ~200 std::pow/std::exp calls plus a
//               1.0f/std::sqrt(sumSquares) normalisation (spectral_state.h:164-170)
//               - memcpy'd as raw bit patterns (:238-260), so their libm
//               differences COMPOUND and cannot be covered by the scalar number.
//
// NO TOLERANCE CONSTANT IS DECLARED IN THIS FILE BY T020, deliberately. The
// probe reports; compliance.md § "FR-029a measured tolerances" records the three
// legs (Windows/MSVC, WSL/GCC 13, and one CI dry run on the real macOS and Linux
// legs) and pins each class at 10x ITS OWN worst observed error across all three.
// T021 then declares kScalarFieldTolerance / kPayloadFieldTolerance from those
// recorded numbers.

namespace {

// The generator's render configuration, verbatim
// (tools/seraphis_preset_generator.cpp:92-93). One 64-sample block at 44 100 Hz
// latches the whole fan-out: processParameterChanges takes the LAST point of each
// queue (processor.cpp:1923-1929) and setParam writes one point at offset 0
// (seraphis_test_fixture.h:219-221). The sample rate never reaches the stored
// state; it only has to be one the processor accepts.
constexpr double kRegenSampleRate = 44100.0;
constexpr Steinberg::int32 kRegenBlockSize = 64;

/// The number of `float` members across the nine decoded packs - the SAME set
/// T011's Seraphis_FactoryPresets_PartialsBlockIsInert bit-pattern-checks, minus
/// its int32/bool fields (which that case only COUNTS):
///
///   [global] 1 | [macro] 5 | [cloud] 11 | [morph] 5 | [life] 9 | [body] 10 |
///   [atmos] 15 | [aether] 17 | [effects] 12   ->   85
///
/// Asserted per preset below. A probe that silently enumerated 40 of the 85
/// fields would pin a tolerance measured over half the surface, and nothing else
/// in this case could tell.
constexpr std::size_t kScalarFloatFieldCount = 85;

/// Decade edges for the reported error histogram. Not a threshold of any kind -
/// it exists so the report shows whether the worst error is a lone outlier or the
/// tail of a broad distribution, which is what decides "pin" vs "investigate".
constexpr std::array<double, 8> kErrorDecades{1e-9, 1e-8, 1e-7, 1e-6,
                                              1e-5, 1e-4, 1e-3, 1e-2};

/// Which toolchain produced a report, printed INTO the report so a pasted block
/// carries its own provenance. `__clang__` is tested FIRST: Apple Clang also
/// defines `__GNUC__`, and clang-cl also defines `_MSC_VER`, so either of the
/// other two orders would mislabel a leg.
[[nodiscard]] std::string toolchainIdentity() {
#ifdef __clang__
    return std::string("clang ") + __clang_version__;
#elif defined(_MSC_VER)
    return "MSVC _MSC_VER=" + std::to_string(_MSC_VER);
#elif defined(__GNUC__)
    return "GCC " + std::to_string(__GNUC__) + "." + std::to_string(__GNUC_MINOR__) + "."
           + std::to_string(__GNUC_PATCHLEVEL__);
#else
    return "unknown toolchain";
#endif
}

/// Regenerate one preset's `Comp` chunk in-process.
///
/// THIS IS THE GENERATOR'S captureComponentState (tools/seraphis_preset_generator.cpp:120-171),
/// step for step. Every line of it is load-bearing; see the section banner.
[[nodiscard]] bool regenerateComponentState(const Seraphis::PresetDefs::SeraphisPresetDef& def,
                                            std::vector<std::uint8_t>& comp, std::string& why) {
    SeraphisTest::ProcessorFixture fx;

    if (fx.prepare(kRegenSampleRate, kRegenBlockSize) != Steinberg::kResultOk) {
        why = "prepare() (initialize/setupProcessing/setActive) failed";
        return false;
    }

    // OI-5 / R-14 - the one line whose absence would make FR-029 compare two
    // different configurations and pass.
    fx.proc->setEffectsStageInstrumentedForTest(false);

    // Untouched IDs keep their registered defaults, which getState() writes
    // unchanged (C-4). Nothing is denormalized here - the shipped
    // handle*ParamChange functions own that arithmetic.
    for (const Seraphis::PresetDefs::ParamSetting& s : def.params) {
        fx.setParam(s.id, s.normalized);
    }

    if (fx.processBlock(kRegenBlockSize) != Steinberg::kResultOk) {
        why = "process() failed";
        return false;
    }

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

/// Relative error against the larger magnitude. See the section banner for why
/// there is no epsilon in the denominator.
[[nodiscard]] double relativeFieldError(float committed, float regenerated) {
    const auto c = static_cast<double>(committed);
    const auto r = static_cast<double>(regenerated);
    if (c == r) {
        return 0.0;  // also the exact-zero-on-both-sides case
    }
    const double scale = std::max(std::abs(c), std::abs(r));
    if (scale <= 0.0) {
        return 0.0;  // unreachable while c != r; stated rather than assumed
    }
    return std::abs(c - r) / scale;
}

/// Visit every `float` member of the nine packs on BOTH decoded states at once,
/// as (block, member, committedValue, regeneratedValue). Returns the number of
/// fields visited, which the caller reconciles against kScalarFloatFieldCount.
///
/// A PAIRWISE walker rather than two independent enumerations: the two sides must
/// be traversed in lockstep for the comparison to be field-for-field, and a
/// single walker cannot drift against itself. T021's clause 5 comparator reuses
/// this - it must not grow a second copy of the field list.
template <typename Observe>
std::size_t forEachScalarFloatPair(const SeraphisTest::DecodedPresetState& a,
                                   const SeraphisTest::DecodedPresetState& b, const Observe& observe) {
    std::size_t visited = 0;
    const auto field = [&](const char* block, const char* member, const std::atomic<float>& x,
                           const std::atomic<float>& y) {
        ++visited;
        observe(block, member, x.load(kRlx), y.load(kRlx));
    };

    // --- [global] -------------------------------------------------------------
    field("[global]", "masterGain", a.global.masterGain, b.global.masterGain);

    // --- [macro] --------------------------------------------------------------
    field("[macro]", "dream", a.macro.dream, b.macro.dream);
    field("[macro]", "bloom", a.macro.bloom, b.macro.bloom);
    field("[macro]", "dissolve", a.macro.dissolve, b.macro.dissolve);
    field("[macro]", "gravity", a.macro.gravity, b.macro.gravity);
    field("[macro]", "entropy", a.macro.entropy, b.macro.entropy);

    // --- [cloud] --------------------------------------------------------------
    field("[cloud]", "richness", a.cloud.richness, b.cloud.richness);
    field("[cloud]", "inharmonicity", a.cloud.inharmonicity, b.cloud.inharmonicity);
    field("[cloud]", "tiltDbPerOct", a.cloud.tiltDbPerOct, b.cloud.tiltDbPerOct);
    field("[cloud]", "mutation", a.cloud.mutation, b.cloud.mutation);
    field("[cloud]", "gravity", a.cloud.gravity, b.cloud.gravity);
    field("[cloud]", "driftDepthCents", a.cloud.driftDepthCents, b.cloud.driftDepthCents);
    field("[cloud]", "driftSmoothness", a.cloud.driftSmoothness, b.cloud.driftSmoothness);
    field("[cloud]", "stereoSpread", a.cloud.stereoSpread, b.cloud.stereoSpread);
    field("[cloud]", "attackSec", a.cloud.attackSec, b.cloud.attackSec);
    field("[cloud]", "decaySec", a.cloud.decaySec, b.cloud.decaySec);
    field("[cloud]", "envOffsetSpread", a.cloud.envOffsetSpread, b.cloud.envOffsetSpread);

    // --- [morph] scalars (the four payloads are the OTHER class) --------------
    field("[morph]", "entropy", a.morph.entropy, b.morph.entropy);
    field("[morph]", "bloom", a.morph.bloom, b.morph.bloom);
    field("[morph]", "position", a.morph.position, b.morph.position);
    field("[morph]", "travelRate", a.morph.travelRate, b.morph.travelRate);
    field("[morph]", "waypointSeconds", a.morph.waypointSeconds, b.morph.waypointSeconds);

    // --- [life] ---------------------------------------------------------------
    field("[life]", "spatialDepth", a.life.spatialDepth, b.life.spatialDepth);
    field("[life]", "spatialRateHz", a.life.spatialRateHz, b.life.spatialRateHz);
    field("[life]", "spatialCoupling", a.life.spatialCoupling, b.life.spatialCoupling);
    field("[life]", "spatialGrowth", a.life.spatialGrowth, b.life.spatialGrowth);
    field("[life]", "voiceWidthPercent", a.life.voiceWidthPercent, b.life.voiceWidthPercent);
    field("[life]", "growthDurationSec", a.life.growthDurationSec, b.life.growthDurationSec);
    field("[life]", "stage0Ms", a.life.stage0Ms, b.life.stage0Ms);
    field("[life]", "stage1Ms", a.life.stage1Ms, b.life.stage1Ms);
    field("[life]", "releaseMs", a.life.releaseMs, b.life.releaseMs);

    // --- [body] ---------------------------------------------------------------
    field("[body]", "resonance", a.body.resonance, b.body.resonance);
    field("[body]", "damping", a.body.damping, b.body.damping);
    field("[body]", "keyTracking", a.body.keyTracking, b.body.keyTracking);
    field("[body]", "drive", a.body.drive, b.body.drive);
    field("[body]", "mix", a.body.mix, b.body.mix);
    field("[body]", "cloudMix", a.body.cloudMix, b.body.cloudMix);
    field("[body]", "cloudDecaySec", a.body.cloudDecaySec, b.body.cloudDecaySec);
    field("[body]", "cloudSize", a.body.cloudSize, b.body.cloudSize);
    field("[body]", "cloudDamping", a.body.cloudDamping, b.body.cloudDamping);
    field("[body]", "width", a.body.width, b.body.width);

    // --- [atmos] --------------------------------------------------------------
    field("[atmos]", "level", a.atmos.level, b.atmos.level);
    field("[atmos]", "blur", a.atmos.blur, b.atmos.blur);
    field("[atmos]", "density", a.atmos.density, b.atmos.density);
    field("[atmos]", "grainSeconds", a.atmos.grainSeconds, b.atmos.grainSeconds);
    field("[atmos]", "driftDepth", a.atmos.driftDepth, b.atmos.driftDepth);
    field("[atmos]", "panSpread", a.atmos.panSpread, b.atmos.panSpread);
    field("[atmos]", "decorrelation", a.atmos.decorrelation, b.atmos.decorrelation);
    field("[atmos]", "freezeMix", a.atmos.freezeMix, b.atmos.freezeMix);
    field("[atmos]", "driftSmoothness", a.atmos.driftSmoothness, b.atmos.driftSmoothness);
    field("[atmos]", "driftRangeSemitones", a.atmos.driftRangeSemitones,
          b.atmos.driftRangeSemitones);
    field("[atmos]", "jitter", a.atmos.jitter, b.atmos.jitter);
    field("[atmos]", "positionSeconds", a.atmos.positionSeconds, b.atmos.positionSeconds);
    field("[atmos]", "positionSpread", a.atmos.positionSpread, b.atmos.positionSpread);
    field("[atmos]", "pitchSemitones", a.atmos.pitchSemitones, b.atmos.pitchSemitones);
    field("[atmos]", "pitchSpread", a.atmos.pitchSpread, b.atmos.pitchSpread);

    // --- [aether] -------------------------------------------------------------
    field("[aether]", "mix", a.aether.mix, b.aether.mix);
    field("[aether]", "size", a.aether.size, b.aether.size);
    field("[aether]", "density", a.aether.density, b.aether.density);
    // The canonical single-std::pow scalar (aether_params.h:111-116).
    field("[aether]", "decaySeconds", a.aether.decaySeconds, b.aether.decaySeconds);
    field("[aether]", "dimensionality", a.aether.dimensionality, b.aether.dimensionality);
    field("[aether]", "damping", a.aether.damping, b.aether.damping);
    field("[aether]", "preDelayMs", a.aether.preDelayMs, b.aether.preDelayMs);
    field("[aether]", "modDepth", a.aether.modDepth, b.aether.modDepth);
    field("[aether]", "modSmoothness", a.aether.modSmoothness, b.aether.modSmoothness);
    field("[aether]", "shimmerOctave", a.aether.shimmerOctave, b.aether.shimmerOctave);
    field("[aether]", "shimmerFifth", a.aether.shimmerFifth, b.aether.shimmerFifth);
    field("[aether]", "bloomSend", a.aether.bloomSend, b.aether.bloomSend);
    field("[aether]", "bloomDecay", a.aether.bloomDecay, b.aether.bloomDecay);
    field("[aether]", "spectralDiffusion", a.aether.spectralDiffusion, b.aether.spectralDiffusion);
    field("[aether]", "sizeBreathDepth", a.aether.sizeBreathDepth, b.aether.sizeBreathDepth);
    field("[aether]", "tideDepth", a.aether.tideDepth, b.aether.tideDepth);
    field("[aether]", "width", a.aether.width, b.aether.width);

    // --- [effects] ------------------------------------------------------------
    field("[effects]", "saturation", a.effects.saturation, b.effects.saturation);
    field("[effects]", "delayMix", a.effects.delayMix, b.effects.delayMix);
    field("[effects]", "delayTimeMs", a.effects.delayTimeMs, b.effects.delayTimeMs);
    field("[effects]", "delaySpreadMs", a.effects.delaySpreadMs, b.effects.delaySpreadMs);
    field("[effects]", "delayFeedback", a.effects.delayFeedback, b.effects.delayFeedback);
    field("[effects]", "delayTilt", a.effects.delayTilt, b.effects.delayTilt);
    field("[effects]", "delayDiffusion", a.effects.delayDiffusion, b.effects.delayDiffusion);
    field("[effects]", "delayWidth", a.effects.delayWidth, b.effects.delayWidth);
    field("[effects]", "width", a.effects.width, b.effects.width);
    field("[effects]", "wanderDepth", a.effects.wanderDepth, b.effects.wanderDepth);
    field("[effects]", "wanderRate", a.effects.wanderRate, b.effects.wanderRate);
    field("[effects]", "azimuthDepth", a.effects.azimuthDepth, b.effects.azimuthDepth);

    return visited;
}

/// Visit every payload-class float on both states at once, as
/// (slot, member, index, committedValue, regeneratedValue). `index` is -1 for the
/// two per-state scalars. Returns the number of fields visited.
///
/// The ratio/amplitude loop stops at min(numPartials) OF THE TWO SIDES and never
/// at kStatePartials: entries at i >= numPartials are scratch space the morph
/// engine's geometric continuation owns and never initialises
/// (spectral_state.h:78-79), so measuring them would fold uninitialised bytes
/// into the number a tolerance is pinned from. A numPartials DISAGREEMENT is not
/// a tolerance question at all - the caller reports it separately, and T021
/// clause 5 gates numPartials exactly.
template <typename Observe>
std::size_t forEachPayloadFloatPair(const SeraphisTest::DecodedPresetState& a,
                                    const SeraphisTest::DecodedPresetState& b, const Observe& observe) {
    std::size_t visited = 0;
    const auto maxPartials = static_cast<int>(Krate::DSP::SpectralState::kStatePartials);

    for (std::size_t s = 0; s < a.payloads.size(); ++s) {
        const Krate::DSP::SpectralState& pa = a.payloads[s];
        const Krate::DSP::SpectralState& pb = b.payloads[s];

        const int active = std::clamp(std::min(pa.numPartials, pb.numPartials), 0, maxPartials);
        for (int i = 0; i < active; ++i) {
            const auto index = static_cast<std::size_t>(i);
            observe(s, "ratios", i, pa.ratios[index], pb.ratios[index]);
            observe(s, "amplitudes", i, pa.amplitudes[index], pb.amplitudes[index]);
            visited += 2;
        }

        observe(s, "tiltDbPerOct", -1, pa.tiltDbPerOct, pb.tiltDbPerOct);
        observe(s, "inharmonicity", -1, pa.inharmonicity, pb.inharmonicity);
        visited += 2;
    }

    return visited;
}

/// The worst single field of one class, with everything needed to judge it: which
/// preset, which field, and BOTH values. The values are what turn "worst error
/// 3e-3" from a number to widen a tolerance around into a fault to investigate.
struct WorstFieldError {
    double error = -1.0;  ///< -1 => nothing observed yet
    std::string preset;
    std::string field;
    double committed = 0.0;
    double regenerated = 0.0;

    void observe(double err, const std::string& where, const std::string& name, float c, float r) {
        if (err > error) {
            error = err;
            preset = where;
            field = name;
            committed = static_cast<double>(c);
            regenerated = static_cast<double>(r);
        }
    }
};

/// Decade histogram over one class's per-field errors. Bucket 0 is EXACTLY zero -
/// kept separate from "<= 1e-9" because "38 of 3570 fields differ at all" and
/// "every field differs a little" are different findings with different responses.
struct ErrorHistogram {
    std::array<std::size_t, kErrorDecades.size() + 2> counts{};

    void add(double error) {
        if (error == 0.0) {
            ++counts[0];
            return;
        }
        for (std::size_t k = 0; k < kErrorDecades.size(); ++k) {
            if (error <= kErrorDecades[k]) {
                ++counts[k + 1];
                return;
            }
        }
        ++counts[counts.size() - 1];
    }

    [[nodiscard]] std::size_t total() const {
        std::size_t sum = 0;
        for (const std::size_t c : counts) {
            sum += c;
        }
        return sum;
    }
};

[[nodiscard]] std::string renderHistogram(const ErrorHistogram& h) {
    std::ostringstream os;
    os << std::scientific << std::setprecision(0);
    os << "      exactly 0    : " << h.counts[0] << "\n";
    for (std::size_t k = 0; k < kErrorDecades.size(); ++k) {
        os << "      <= " << kErrorDecades[k] << "  : " << h.counts[k + 1] << "\n";
    }
    os << "      >  " << kErrorDecades[kErrorDecades.size() - 1] << "  : "
       << h.counts[h.counts.size() - 1] << "\n";
    return os.str();
}

[[nodiscard]] std::string renderWorst(const char* label, const WorstFieldError& w) {
    std::ostringstream os;
    os << std::scientific << std::setprecision(9);
    os << "  " << label << " worst relative error = ";
    if (w.error < 0.0) {
        os << "(no field observed)\n";
        return os.str();
    }
    os << w.error << "\n";
    os << "      preset      : " << w.preset << "\n";
    os << "      field       : " << w.field << "\n";
    os << "      committed   : " << w.committed << "\n";
    os << "      regenerated : " << w.regenerated << "\n";
    os << "      10x worst   : " << (w.error * 10.0) << "\n";
    return os.str();
}

}  // namespace

TEST_CASE("Seraphis_FactoryPresets_TreeToleranceProbe", "[.measure][seraphis][preset]") {
    const std::vector<std::filesystem::path> files = SeraphisTest::allPresetFiles();
    INFO("presets root: " << SeraphisTest::factoryPresetRoot().string());
    REQUIRE(!files.empty());

    WorstFieldError worstScalar;
    WorstFieldError worstPayload;
    ErrorHistogram scalarHistogram;
    ErrorHistogram payloadHistogram;

    std::vector<std::string> failures;         // structural - asserted
    std::vector<std::string> nonFiniteFields;  // reported AND asserted: a non-finite
                                               // side makes every comparison against
                                               // it silently vanish inside a `>`
    std::vector<std::string> numPartialsDisagreements;  // reported only - T021 gates it
    std::size_t presetsCompared = 0;

    for (const auto& file : files) {
        const SeraphisTest::PresetFile pf = SeraphisTest::parseVstPreset(file);
        if (!pf.parseError.empty()) {
            failures.push_back(file.string() + ": " + pf.parseError);
            continue;
        }

        // findDef MISS POLICY (seraphis_preset_defs.h:1070-1084): null is a
        // FAILURE, never "fall back to a default". A committed preset with no
        // definition cannot be regenerated at all, and silently skipping it would
        // pin a tolerance measured over a smaller library than the one that ships.
        const Seraphis::PresetDefs::SeraphisPresetDef* def =
            Seraphis::PresetDefs::findDef(pf.category, pf.stem);
        if (def == nullptr) {
            failures.push_back(file.string() + ": no definition in PresetDefs::allPresets() for "
                               + pf.category + "/" + pf.stem);
            continue;
        }

        SeraphisTest::DecodedPresetState committed;
        std::string why;
        if (!SeraphisTest::decodePresetState(pf.comp, committed, why)) {
            failures.push_back(file.string() + ": committed decode failed: " + why);
            continue;
        }

        std::vector<std::uint8_t> regenComp;
        if (!regenerateComponentState(*def, regenComp, why)) {
            failures.push_back(file.string() + ": regeneration failed: " + why);
            continue;
        }

        SeraphisTest::DecodedPresetState regenerated;
        if (!SeraphisTest::decodePresetState(regenComp, regenerated, why)) {
            failures.push_back(file.string() + ": regenerated decode failed: " + why);
            continue;
        }

        const std::string where = pf.category + "/" + pf.stem;

        const std::size_t scalarFields = forEachScalarFloatPair(
            committed, regenerated,
            [&](const char* block, const char* member, float c, float r) {
                const std::string name = std::string(block) + "." + member;
                if (!isFiniteFloat(c) || !isFiniteFloat(r)) {
                    nonFiniteFields.push_back(joinText(where, ": ", name,
                                                       " is non-finite on one side"));
                    return;
                }
                const double error = relativeFieldError(c, r);
                scalarHistogram.add(error);
                worstScalar.observe(error, where, name, c, r);
            });
        // The enumeration must cover the whole shipped surface or the number this
        // case exists to produce is measured over a subset.
        INFO("scalar float fields enumerated for " << where << ": " << scalarFields);
        REQUIRE(scalarFields == kScalarFloatFieldCount);

        for (std::size_t s = 0; s < committed.payloads.size(); ++s) {
            if (committed.payloads[s].numPartials != regenerated.payloads[s].numPartials) {
                numPartialsDisagreements.push_back(
                    where + ": payload[" + std::to_string(s) + "].numPartials committed="
                    + std::to_string(committed.payloads[s].numPartials)
                    + " regenerated=" + std::to_string(regenerated.payloads[s].numPartials));
            }
        }

        const std::size_t payloadFields = forEachPayloadFloatPair(
            committed, regenerated,
            [&](std::size_t slot, const char* member, int index, float c, float r) {
                std::string name = "payload[" + std::to_string(slot) + "]." + member;
                if (index >= 0) {
                    name += "[" + std::to_string(index) + "]";
                }
                if (!isFiniteFloat(c) || !isFiniteFloat(r)) {
                    nonFiniteFields.push_back(joinText(where, ": ", name,
                                                       " is non-finite on one side"));
                    return;
                }
                const double error = relativeFieldError(c, r);
                payloadHistogram.add(error);
                worstPayload.observe(error, where, name, c, r);
            });
        INFO("payload float fields enumerated for " << where << ": " << payloadFields);
        REQUIRE(payloadFields > 0u);

        ++presetsCompared;
    }

    INFO("structural failures: " << joinPaths(failures));
    REQUIRE(failures.empty());

    INFO("non-finite field pairs: " << joinPaths(nonFiniteFields));
    REQUIRE(nonFiniteFields.empty());

    INFO("presets compared: " << presetsCompared << " of " << files.size());
    REQUIRE(presetsCompared == files.size());

    // -------------------------------------------------------------------------
    // The report. Everything measured leaves through WARN - this case gates no
    // tolerance and never fails on a number.
    // -------------------------------------------------------------------------
    std::ostringstream report;
    report << "\n=== FR-029a tolerance probe (T020) =====================================\n";
    report << "  toolchain          : " << toolchainIdentity() << "\n";
    report << "  presets compared   : " << presetsCompared << "\n";
    report << "  scalar fields      : " << scalarHistogram.total() << " ("
           << kScalarFloatFieldCount << " per preset)\n";
    report << "  payload fields     : " << payloadHistogram.total() << "\n";
    if (!numPartialsDisagreements.empty()) {
        report << "  !! numPartials disagreements (T021 clause 5 gates these exactly):\n      "
               << joinPaths(numPartialsDisagreements) << "\n";
    }
    report << renderWorst("SCALAR ", worstScalar);
    report << "    scalar error distribution:\n" << renderHistogram(scalarHistogram);
    report << renderWorst("PAYLOAD", worstPayload);
    report << "    payload error distribution:\n" << renderHistogram(payloadHistogram);
    report << "  PIN RULE (FR-029a / Q3): each class is pinned at 10x ITS OWN worst observed\n"
              "  error across ALL THREE legs (Windows/MSVC, WSL/GCC 13, one CI dry run on the\n"
              "  real macOS and Linux legs) - the two numbers may not coincide by accident, and\n"
              "  a worst outside the sanity band (scalar ~1e-6, payload ~1e-4) is INVESTIGATED,\n"
              "  never widened. Record this block verbatim in compliance.md\n"
              "  section 'FR-029a measured tolerances' before T021 declares its constants.\n";
    report << "========================================================================\n";
    WARN(report.str());
}

// =============================================================================
// T021 - FR-029 / SC-017: the committed tree IS this toolchain's generator output
// =============================================================================
// Reference: specs/seraphis-phase12-presets-release/spec.md (C-8, FR-029,
//            FR-029a, SC-017)
//            specs/seraphis-phase12-presets-release/tasks.md (T021)
//
// WHY THERE IS NO memcmp OF THE `Comp` CHUNKS HERE, AND WHY ADDING ONE WOULD BE A
// DEFECT RATHER THAN A SIMPLIFICATION (spec C-8; plan R-1). The tree under
// plugins/seraphis/resources/presets/ is generated on the developer's
// Windows/MSVC build, while the artifact the installer actually ships is
// generated on ubuntu-latest with GCC (release.yml:100-102, :158-174, :201-224).
// Those two are never produced by the same toolchain, and three properties of the
// stream make a byte comparison across toolchains a BIT-EXACT FLOAT GOLDEN - the
// thing ci.yml:162-166 lints for and roadmap line 664 forbids:
//
//   1. ~14 stored fields are denormalized through logMapFromNormalized, whose
//      body is clamp(mn * std::pow(mx / mn, u), mn, mx)
//      (plugins/shared/src/ui/parameter_helpers.h:80-83). std::pow is not
//      correctly-rounded and differs between UCRT, glibc and Apple libm.
//   2. Each preset embeds four 541-byte SpectralState payloads that are memcpy'd
//      BIT PATTERNS (spectral_state.h:238-260) of makeFactoryState results - a
//      function whose own banner records ~200 std::pow/std::exp calls plus a
//      1.0f/std::sqrt(sumSquares) normalisation (:164-170).
//   3. The macOS leg additionally builds with -ffast-math.
//
// So the invariant this case asserts is the SEMANTIC one C-8 states: the
// committed tree and the generator's output come from the same generator source
// and the same definition table, and agree in file set, `Info` bytes, stream
// version and length, every integer/enum/bool field exactly, and every float
// field within a MEASURED tolerance. Per-toolchain ULP differences in the
// denormalized floats and the payloads are an ACCEPTED decision of this phase.
//
// BECAUSE THE COMPARISON IS SEMANTIC, THIS CASE RUNS ON ALL THREE LEGS - no
// `#if defined(_WIN32)` guard anywhere below. Each leg proves that ITS OWN
// toolchain's generator output is the committed library, which is a strictly
// stronger statement about the Linux-generated installer than a Windows-only
// byte check ever was (C-8's closing paragraph).
//
// THE REGENERATED SIDE IS PRODUCED IN-PROCESS, by T020's
// regenerateComponentState() - the generator's own drive sequence including
// setEffectsStageInstrumentedForTest(false) (OI-5 / R-2). It is NOT re-derived
// here and no second copy of it exists in this TU: two drive sequences would make
// FR-029 compare two different configurations and pass while proving nothing.
//
// FIELD-COVERAGE TRIPWIRE. The integer comparator (clause 4) and the float
// comparator (clause 5) increment ONE set of per-block counters, reconciled
// against the shared kExpectedFieldCounts census this TU also hands to
// Seraphis_FactoryPresets_PartialsBlockIsInert (T011). Combined with
// decodePresetState's cumulative offset tripwires (preset_test_support.h:424-437),
// a field added to a pack in a later phase fails BOTH checks rather than silently
// escaping comparison.

namespace {

// ---------------------------------------------------------------------------
// FR-029a's two tolerance classes.
// ---------------------------------------------------------------------------
// MEASURED AND PINNED, 2026-08-05. Both numbers below come from
// Seraphis_FactoryPresets_TreeToleranceProbe ([.measure]), run over all 42
// committed presets on two toolchains against the SAME Windows/MSVC-generated
// tree. Full reports are transcribed verbatim in
// specs/seraphis-phase12-presets-release/compliance.md, section
// "FR-029a measured tolerances".
//
//   leg                worst SCALAR (3570 fields)   worst PAYLOAD (21680 fields)
//   Windows / MSVC 1944   0.000000000e+00              0.000000000e+00
//   WSL / GCC 13.3.0      0.000000000e+00              1.175471576e-07
//                                                      Bells/Bell Garden,
//                                                      payload[1].amplitudes[20],
//                                                      1.267675012e-01 committed vs
//                                                      1.267675161e-01 regenerated
//                                                      (exactly 1 float ULP)
//
// PAYLOAD: the pin rule applies unchanged - 10 x 1.175471576e-07 = 1.175e-06,
// rounded up to 1.2e-06. GCC's payload distribution was 21 079 fields exactly
// equal, 445 at <= 1e-07 and 156 at <= 1e-06, i.e. the whole spread is
// single-ULP disagreement compounded through makeFactoryState's ~200
// pow/exp calls and normalizeSpectralState. 1.2e-06 leaves ~10 ULP of headroom.
//
// SCALAR: RECORDED DEVIATION, and it is deliberate rather than an oversight.
// The measured worst is EXACTLY ZERO on both legs, so "10x its own worst" gives
// 0.0 - and a tolerance of 0.0 is a bit-exact float equality assertion, which
// this repo forbids outright (roadmap line 664; the ci.yml:162-166 lint; the
// dsp/CLAUDE.md rule that a float golden must never demand bit-identical math).
// It would also collapse the two classes into one number, which Q3 forbids
// separately. The pin is therefore the TIGHTEST ADMISSIBLE NON-ZERO bound: one
// float ULP. A single-ULP disagreement has relative error in
// (2^-24, 2^-23] = (5.96e-08, 1.1920929e-07], so 1.2e-07 admits any one-ULP
// libm difference on a single std::pow and nothing wider - it is 10x TIGHTER
// than the payload pin, so the two classes stay distinct as Q3 requires.
//
// OUTSTANDING: the macOS / Apple-Clang leg has NOT been measured. Both numbers
// above are cross-toolchain (MSVC-generated tree vs GCC regeneration), so they
// are not a same-toolchain tautology, but a third libm could disagree by more.
// A macOS failure here means INVESTIGATE and re-record the probe - never widen
// the constant to make the leg pass.
constexpr double kScalarFieldTolerance = 1.2e-7;
constexpr double kPayloadFieldTolerance = 1.2e-6;

// Q3 is explicit that the two classes are measured separately and "may not share
// one number". If a measurement genuinely makes them coincide, that has to be
// stated in compliance.md as a finding - not arrived at by editing one of these
// two lines to match the other.
static_assert(kScalarFieldTolerance != kPayloadFieldTolerance,
              "FR-029a / Q3: the scalar and payload tolerance classes are measured separately "
              "and must not collapse into one number");
static_assert(kScalarFieldTolerance > 0.0 && kPayloadFieldTolerance > 0.0,
              "FR-029a: a zero tolerance is a bit-exact float golden, which this repo forbids "
              "(roadmap line 664, ci.yml:162-166)");
static_assert(kScalarFieldTolerance < kPayloadFieldTolerance,
              "FR-029a: the scalar class is a single std::pow and the payload class compounds "
              "~200 of them - the scalar pin must be the tighter of the two");

/// The 4-byte pack fields that are NOT floats: 107 fields in the census, 85 of
/// them floats (kScalarFloatFieldCount), so 22 integers/enums/bools.
///
///   [global] polyphony, softLimit                                    2
///   [seed]   seedIndex                                               1
///   [morph]  travelMode, sync, syncNote, stateCount, slot[0..3]      8
///   [life]   envMode                                                 1
///   [body]   material, inputAgc, resonatorBypass                     3
///   [atmos]  freeze, grainEnvelope                                   2
///   [aether] freeze                                                  1
///   [effects] spreadDirection, delaySync, delaySyncNote,
///             spectralFreeze                                         4
constexpr std::size_t kIntegerFieldCount = 22;

/// The [partials] block, counted separately from the pack census: 64 pans
/// (compared as floats) and two uint64 bitmasks (compared exactly).
constexpr std::size_t kPartialPanCount = 64;
constexpr std::size_t kPartialMaskCount = 2;

// The block width these two reconstruct is the shipped 272 bytes
// (processor.cpp:420-457) - the last block of the 2868-byte stream and the only
// one outside the ten-pack census, so nothing else would notice if it moved.
static_assert(kPartialPanCount * 4u + kPartialMaskCount * 8u == 272u,
              "[partials] is 64 float pans + two uint64 bitmasks = 272 bytes");

/// Index of the first differing byte, or npos when the two strings are equal.
/// Reported instead of two full XML dumps - the `Info` payload is ~380 bytes and
/// a side-by-side pair of them buries the one byte that moved.
[[nodiscard]] std::size_t firstDifference(const std::string& a, const std::string& b) {
    const std::size_t common = std::min(a.size(), b.size());
    for (std::size_t i = 0; i < common; ++i) {
        if (a[i] != b[i]) {
            return i;
        }
    }
    return a.size() == b.size() ? std::string::npos : common;
}

/// Visit every non-float 4-byte pack field on BOTH decoded states at once, as
/// (block, member, committedValue, regeneratedValue). Returns the number of
/// fields visited, which the caller reconciles against kIntegerFieldCount and
/// against the shared per-block census.
///
/// PAIRWISE, for the same reason forEachScalarFloatPair is: the two sides must be
/// traversed in lockstep for the comparison to be field-for-field, and a single
/// walker cannot drift against itself.
///
/// `bool` fields are widened to 0/1 rather than compared as bools so that one
/// message shape covers every field - an enum index that moved and a toggle that
/// flipped read identically in the failure list.
template <typename Observe>
std::size_t forEachIntegerFieldPair(const SeraphisTest::DecodedPresetState& a,
                                    const SeraphisTest::DecodedPresetState& b, const Observe& observe) {
    std::size_t visited = 0;
    const auto intField = [&](const char* block, const char* member, const std::atomic<int>& x,
                              const std::atomic<int>& y) {
        ++visited;
        observe(block, member, static_cast<long long>(x.load(kRlx)),
                static_cast<long long>(y.load(kRlx)));
    };
    const auto boolField = [&](const char* block, const char* member, const std::atomic<bool>& x,
                               const std::atomic<bool>& y) {
        ++visited;
        observe(block, member, x.load(kRlx) ? 1LL : 0LL, y.load(kRlx) ? 1LL : 0LL);
    };

    // --- [global] / [seed] ----------------------------------------------------
    // seedIndex lives in GlobalParams but is its OWN 4-byte stream block, written
    // after [macro] (processor.cpp:1868-1914) and decoded by loadGlobalSeed
    // (preset_test_support.h:451). It is counted against "[seed]" so the census
    // reconciles block-for-block against the stream, not struct-for-struct.
    intField("[global]", "polyphony", a.global.polyphony, b.global.polyphony);
    boolField("[global]", "softLimit", a.global.softLimit, b.global.softLimit);
    intField("[seed]", "seedIndex", a.global.seedIndex, b.global.seedIndex);

    // --- [morph] --------------------------------------------------------------
    intField("[morph]", "travelMode", a.morph.travelMode, b.morph.travelMode);
    boolField("[morph]", "sync", a.morph.sync, b.morph.sync);
    intField("[morph]", "syncNote", a.morph.syncNote, b.morph.syncNote);
    intField("[morph]", "stateCount", a.morph.stateCount, b.morph.stateCount);
    // The four factory-slot indices (IDs 409-412). Named individually so the
    // failure list says which slot moved.
    constexpr std::array<const char*, 4> kSlotNames{"slot[0]", "slot[1]", "slot[2]", "slot[3]"};
    for (std::size_t i = 0; i < a.morph.slot.size(); ++i) {
        intField("[morph]", kSlotNames[i], a.morph.slot[i], b.morph.slot[i]);
    }

    // --- [life] ---------------------------------------------------------------
    intField("[life]", "envMode", a.life.envMode, b.life.envMode);

    // --- [body] ---------------------------------------------------------------
    intField("[body]", "material", a.body.material, b.body.material);
    boolField("[body]", "inputAgc", a.body.inputAgc, b.body.inputAgc);
    boolField("[body]", "resonatorBypass", a.body.resonatorBypass, b.body.resonatorBypass);

    // --- [atmos] --------------------------------------------------------------
    boolField("[atmos]", "freeze", a.atmos.freeze, b.atmos.freeze);
    intField("[atmos]", "grainEnvelope", a.atmos.grainEnvelope, b.atmos.grainEnvelope);

    // --- [aether] -------------------------------------------------------------
    boolField("[aether]", "freeze", a.aether.freeze, b.aether.freeze);

    // --- [effects] ------------------------------------------------------------
    intField("[effects]", "spreadDirection", a.effects.spreadDirection, b.effects.spreadDirection);
    boolField("[effects]", "delaySync", a.effects.delaySync, b.effects.delaySync);
    intField("[effects]", "delaySyncNote", a.effects.delaySyncNote, b.effects.delaySyncNote);
    boolField("[effects]", "spectralFreeze", a.effects.spectralFreeze, b.effects.spectralFreeze);

    return visited;
}

/// One float mismatch, rendered with BOTH values and the tolerance it broke.
/// A bare "field X differs" cannot be triaged: whether the committed tree is
/// stale, the definition table moved, or a libm difference genuinely exceeded a
/// pinned tolerance is decided by looking at the two numbers.
[[nodiscard]] std::string formatFloatMismatch(const std::string& where, const std::string& field,
                                              float committed, float regenerated, double error,
                                              double tolerance) {
    std::ostringstream os;
    os << std::scientific << std::setprecision(9);
    os << where << ": " << field << " committed=" << static_cast<double>(committed)
       << " regenerated=" << static_cast<double>(regenerated) << " relError=" << error
       << " tolerance=" << tolerance;
    return os.str();
}

}  // namespace

TEST_CASE("Seraphis_FactoryPresets_TreeMatchesGenerator", "[seraphis][preset]") {
    // The PROVISIONAL-tolerance WARN that used to open this case is gone: both
    // constants are measured and pinned as of 2026-08-05 (see the banner above
    // and compliance.md's "FR-029a measured tolerances"). Do not reinstate it
    // without un-pinning them in the same change.
    const std::vector<std::filesystem::path> files = SeraphisTest::allPresetFiles();
    INFO("presets root: " << SeraphisTest::factoryPresetRoot().string());
    REQUIRE(!files.empty());

    // Parsed ONCE, up front: clause 1 needs the whole (category, stem) set before
    // any per-file comparison is meaningful, and re-reading 42 containers to get
    // it twice would be the only thing a second pass bought.
    std::vector<std::string> parseFailures;
    std::vector<SeraphisTest::PresetFile> parsed;
    parsed.reserve(files.size());
    for (const auto& file : files) {
        SeraphisTest::PresetFile pf = SeraphisTest::parseVstPreset(file);
        if (!pf.parseError.empty()) {
            parseFailures.push_back(file.string() + ": " + pf.parseError);
            continue;
        }
        parsed.push_back(std::move(pf));
    }
    INFO("files that did not parse: " << joinPaths(parseFailures));
    REQUIRE(parseFailures.empty());

    // -------------------------------------------------------------------------
    // CLAUSE 1 - file set, IN BOTH DIRECTIONS
    // -------------------------------------------------------------------------
    // A one-directional check ("every definition has a file") passes a tree that
    // also carries six stale files from a renamed earlier authoring pass, and the
    // opposite check passes a table whose newest entries were never generated.
    // Both differences are computed and both are asserted.
    std::set<std::string> onDisk;
    std::vector<std::string> duplicateOnDisk;
    for (const auto& pf : parsed) {
        const std::string key = pf.category + "/" + pf.stem;
        if (!onDisk.insert(key).second) {
            duplicateOnDisk.push_back(key);
        }
    }
    std::set<std::string> defined;
    std::vector<std::string> duplicateInTable;
    for (const auto& def : Seraphis::PresetDefs::allPresets()) {
        const std::string key = std::string(def.category) + "/" + std::string(def.name);
        if (!defined.insert(key).second) {
            duplicateInTable.push_back(key);
        }
    }

    std::vector<std::string> onlyOnDisk;
    std::vector<std::string> onlyInTable;
    std::set_difference(onDisk.begin(), onDisk.end(), defined.begin(), defined.end(),
                        std::back_inserter(onlyOnDisk));
    std::set_difference(defined.begin(), defined.end(), onDisk.begin(), onDisk.end(),
                        std::back_inserter(onlyInTable));

    INFO("committed files with no definition in PresetDefs::allPresets(): "
         << joinPaths(onlyOnDisk));
    REQUIRE(onlyOnDisk.empty());
    INFO("definitions with no committed file: " << joinPaths(onlyInTable));
    REQUIRE(onlyInTable.empty());
    // The two set_differences above are blind to a duplicate on either side, so
    // the collision lists are asserted separately rather than inferred from a
    // size comparison.
    INFO("duplicate (category, stem) on disk: " << joinPaths(duplicateOnDisk));
    REQUIRE(duplicateOnDisk.empty());
    INFO("duplicate (category, name) in the definition table: " << joinPaths(duplicateInTable));
    REQUIRE(duplicateInTable.empty());

    // -------------------------------------------------------------------------
    // CLAUSES 2-5, per preset
    // -------------------------------------------------------------------------
    std::vector<std::string> regenFailures;
    std::vector<std::string> infoMismatch;
    std::vector<std::string> versionOrLength;
    std::vector<std::string> integerMismatch;
    std::vector<std::string> scalarMismatch;
    std::vector<std::string> payloadMismatch;
    std::vector<std::string> numPartialsMismatch;
    std::vector<std::string> invalidPayload;
    std::vector<std::string> nonFiniteFields;
    std::vector<std::string> partialsMismatch;
    std::vector<std::string> fieldCountMismatch;

    // Reported on success too: a pass whose worst error sits at 0.9x the pinned
    // tolerance is one libm update away from red, and only the number says so.
    WorstFieldError worstScalar;
    WorstFieldError worstPayload;

    // The per-block census is a property of the shipped structs, identical for
    // every preset, so it is reconciled ONCE - 42 copies of the same line would
    // bury the per-preset mismatches the lists are really for.
    bool fieldCountsReconciled = false;
    std::size_t presetsCompared = 0;

    for (const auto& pf : parsed) {
        const std::string where = pf.category + "/" + pf.stem;

        // findDef MISS POLICY (tools/seraphis_preset_defs.h:1070-1084): null is a
        // FAILURE, never a fallback. Clause 1 has already asserted the sets match,
        // so a null here means the two lookups disagree - which is itself a
        // defect, not a reason to skip the file.
        const Seraphis::PresetDefs::SeraphisPresetDef* def =
            Seraphis::PresetDefs::findDef(pf.category, pf.stem);
        if (def == nullptr) {
            regenFailures.push_back(where + ": no definition in PresetDefs::allPresets()");
            continue;
        }

        // --- CLAUSE 2: `Info` XML, BYTE EQUALITY -----------------------------
        // Legitimate as a byte comparison, and only because the payload is pure
        // ASCII written with explicit "\n" line endings from ONE source
        // (buildSeraphisInfoXml, tools/seraphis_preset_defs.h:1100-1119). It
        // carries no float and is therefore toolchain-invariant - the exact
        // premise the `Comp` chunk lacks.
        const std::string expectedInfo =
            Seraphis::PresetDefs::buildSeraphisInfoXml(pf.stem, pf.category);
        if (pf.info != expectedInfo) {
            const std::size_t at = firstDifference(pf.info, expectedInfo);
            infoMismatch.push_back(where + ": Info XML differs from buildSeraphisInfoXml at byte "
                                   + std::to_string(at) + " (committed "
                                   + std::to_string(pf.info.size()) + " bytes, expected "
                                   + std::to_string(expectedInfo.size()) + ")");
        }

        // --- CLAUSE 3 (committed side): length -------------------------------
        if (pf.comp.size() != kComponentChunkBytes) {
            versionOrLength.push_back(where + ": committed Comp is "
                                      + std::to_string(pf.comp.size()) + " bytes, expected "
                                      + std::to_string(kComponentChunkBytes));
            continue;  // decodePresetState rejects any other length anyway; the
                       // message above is the one that names the fault
        }

        SeraphisTest::DecodedPresetState committed;
        std::string why;
        if (!SeraphisTest::decodePresetState(pf.comp, committed, why)) {
            regenFailures.push_back(joinText(where, ": committed decode failed: ", why));
            continue;
        }

        std::vector<std::uint8_t> regenComp;
        if (!regenerateComponentState(*def, regenComp, why)) {
            regenFailures.push_back(joinText(where, ": regeneration failed: ", why));
            continue;
        }

        // --- CLAUSE 3 (regenerated side): length + both versions --------------
        if (regenComp.size() != kComponentChunkBytes) {
            versionOrLength.push_back(where + ": regenerated Comp is "
                                      + std::to_string(regenComp.size()) + " bytes, expected "
                                      + std::to_string(kComponentChunkBytes));
            continue;
        }

        SeraphisTest::DecodedPresetState regenerated;
        if (!SeraphisTest::decodePresetState(regenComp, regenerated, why)) {
            regenFailures.push_back(joinText(where, ": regenerated decode failed: ", why));
            continue;
        }

        if (committed.version != kExpectedStateVersion) {
            versionOrLength.push_back(where + ": committed state version is "
                                      + std::to_string(committed.version) + ", expected "
                                      + std::to_string(kExpectedStateVersion));
        }
        if (regenerated.version != kExpectedStateVersion) {
            versionOrLength.push_back(where + ": regenerated state version is "
                                      + std::to_string(regenerated.version) + ", expected "
                                      + std::to_string(kExpectedStateVersion));
        }

        // --- the shared per-block counters ------------------------------------
        std::map<std::string, std::size_t> counts;
        for (const auto& entry : kExpectedFieldCounts) {
            counts[entry.block] = 0;  // seeded, so a block that enumerated NOTHING
                                      // reports 0 rather than vanishing
        }

        // --- CLAUSE 4: integer / enum / bool, EXACTLY equal -------------------
        const std::size_t integerFields = forEachIntegerFieldPair(
            committed, regenerated,
            [&](const char* block, const char* member, long long c, long long r) {
                ++counts[block];
                if (c != r) {
                    integerMismatch.push_back(where + ": " + block + "." + member
                                              + " committed=" + std::to_string(c)
                                              + " regenerated=" + std::to_string(r));
                }
            });
        INFO("integer fields enumerated for " << where << ": " << integerFields);
        REQUIRE(integerFields == kIntegerFieldCount);

        // --- CLAUSE 5a: scalar floats, within kScalarFieldTolerance -----------
        // The SAME walker T020's probe measured with (forEachScalarFloatPair) -
        // a second field list here could pin a tolerance over one set of fields
        // and gate over another.
        const std::size_t scalarFields = forEachScalarFloatPair(
            committed, regenerated,
            [&](const char* block, const char* member, float c, float r) {
                ++counts[block];
                const std::string name = std::string(block) + "." + member;
                if (!isFiniteFloat(c) || !isFiniteFloat(r)) {
                    // A non-finite side compares FALSE against every bound, so it
                    // would slip silently through the `>` below.
                    nonFiniteFields.push_back(joinText(where, ": ", name,
                                                       " is non-finite on one side"));
                    return;
                }
                const double error = relativeFieldError(c, r);
                worstScalar.observe(error, where, name, c, r);
                if (error > kScalarFieldTolerance) {
                    scalarMismatch.push_back(
                        formatFloatMismatch(where, name, c, r, error, kScalarFieldTolerance));
                }
            });
        INFO("scalar float fields enumerated for " << where << ": " << scalarFields);
        REQUIRE(scalarFields == kScalarFloatFieldCount);

        if (!fieldCountsReconciled) {
            fieldCountsReconciled = true;
            for (const auto& entry : kExpectedFieldCounts) {
                const std::size_t enumerated = counts.at(entry.block);
                if (enumerated != entry.fields) {
                    fieldCountMismatch.push_back(
                        std::string(entry.block) + ": compared " + std::to_string(enumerated)
                        + " fields, the shipped block is " + std::to_string(entry.fields)
                        + " wide (a field added to this pack must be added to "
                          "forEachIntegerFieldPair or forEachScalarFloatPair, not just to the "
                          "shipped struct)");
                }
            }
        }

        // --- CLAUSE 5b: the four SpectralState payloads -----------------------
        // numPartials is EXACT: it is an integer count, and a disagreement makes
        // every ratio/amplitude comparison below a comparison between different
        // partials rather than a tolerance question.
        for (std::size_t s = 0; s < committed.payloads.size(); ++s) {
            const Krate::DSP::SpectralState& pc = committed.payloads[s];
            const Krate::DSP::SpectralState& pr = regenerated.payloads[s];
            const std::string slot = "payload[" + std::to_string(s) + "]";

            if (pc.numPartials != pr.numPartials) {
                numPartialsMismatch.push_back(joinText(where, ": ", slot,
                                                       ".numPartials committed=",
                                                       std::to_string(pc.numPartials),
                                                       " regenerated=",
                                                       std::to_string(pr.numPartials)));
            }
            // FR-025a's hand-skip means these payloads come from
            // decodeSpectralPayloads' separate deserializeSpectralState pass, and
            // a REJECTED record leaves the slot default-constructed rather than
            // signalling. Without this check two rejected slots compare equal
            // field-for-field and clause 5 passes on a pair of blanks.
            if (!committed.payloadDecoded[s]) {
                invalidPayload.push_back(joinText(where, ": committed ", slot,
                                                  " was REJECTED by deserializeSpectralState"));
            }
            if (!regenerated.payloadDecoded[s]) {
                invalidPayload.push_back(joinText(where, ": regenerated ", slot,
                                                  " was REJECTED by deserializeSpectralState"));
            }
            // Asserted on BOTH sides: a committed payload that no longer
            // validates is a stale-tree defect, and a regenerated one that does
            // not is a generator defect. One check could not tell them apart.
            if (!Krate::DSP::isValidSpectralState(pc)) {
                invalidPayload.push_back(joinText(where, ": committed ", slot,
                                                  " fails isValidSpectralState"));
            }
            if (!Krate::DSP::isValidSpectralState(pr)) {
                invalidPayload.push_back(joinText(where, ": regenerated ", slot,
                                                  " fails isValidSpectralState"));
            }
        }

        const std::size_t payloadFields = forEachPayloadFloatPair(
            committed, regenerated,
            [&](std::size_t slot, const char* member, int index, float c, float r) {
                std::string name = "payload[" + std::to_string(slot) + "]." + member;
                if (index >= 0) {
                    name += "[" + std::to_string(index) + "]";
                }
                if (!isFiniteFloat(c) || !isFiniteFloat(r)) {
                    nonFiniteFields.push_back(joinText(where, ": ", name,
                                                       " is non-finite on one side"));
                    return;
                }
                const double error = relativeFieldError(c, r);
                worstPayload.observe(error, where, name, c, r);
                if (error > kPayloadFieldTolerance) {
                    payloadMismatch.push_back(
                        formatFloatMismatch(where, name, c, r, error, kPayloadFieldTolerance));
                }
            });
        INFO("payload float fields enumerated for " << where << ": " << payloadFields);
        REQUIRE(payloadFields > 0u);

        // --- the [partials] block, 272 B, counted on its own ------------------
        // Outside the pack census (which covers the ten scalar blocks only), and
        // compared here rather than left to FR-006a's all-zero gate: that gate
        // reads the committed side alone, so without this the last 272 bytes of
        // every preset would never be compared against the generator at all.
        std::size_t partialFieldsCompared = 0;
        for (std::size_t i = 0; i < committed.partialPan.size(); ++i) {
            ++partialFieldsCompared;
            const float c = committed.partialPan[i];
            const float r = regenerated.partialPan[i];
            const std::string name = "[partials].pan[" + std::to_string(i) + "]";
            if (!isFiniteFloat(c) || !isFiniteFloat(r)) {
                nonFiniteFields.push_back(joinText(where, ": ", name,
                                                   " is non-finite on one side"));
                continue;
            }
            const double error = relativeFieldError(c, r);
            worstScalar.observe(error, where, name, c, r);
            if (error > kScalarFieldTolerance) {
                partialsMismatch.push_back(
                    formatFloatMismatch(where, name, c, r, error, kScalarFieldTolerance));
            }
        }
        // The two masks are integer fields (clause 4's rule), compared exactly.
        if (committed.panOverrideBits != regenerated.panOverrideBits) {
            partialsMismatch.push_back(where + ": [partials].panOverrideBits committed="
                                       + std::to_string(committed.panOverrideBits)
                                       + " regenerated="
                                       + std::to_string(regenerated.panOverrideBits));
        }
        if (committed.maskBits != regenerated.maskBits) {
            partialsMismatch.push_back(where + ": [partials].maskBits committed="
                                       + std::to_string(committed.maskBits) + " regenerated="
                                       + std::to_string(regenerated.maskBits));
        }
        // The pan count is the one thing here that could silently shrink: it comes
        // from the harness's own array width (preset_test_support.h:94, :386), and
        // a narrower array would compare fewer than the 64 stored pans while every
        // list below still read empty.
        INFO("[partials] pans compared for " << where << ": " << partialFieldsCompared);
        REQUIRE(partialFieldsCompared == kPartialPanCount);

        ++presetsCompared;
    }

    // -------------------------------------------------------------------------
    // Verdicts, ordered so the FIRST failure reported is the one that invalidates
    // the ones after it.
    // -------------------------------------------------------------------------
    INFO("presets that could not be regenerated or decoded: " << joinPaths(regenFailures));
    REQUIRE(regenFailures.empty());

    // Before any per-field verdict: a census drift means the comparison ran over
    // an INCOMPLETE field set, so every "no mismatch" below is a pass over a
    // subset of the stream.
    INFO("per-block field-count reconciliation: " << joinPaths(fieldCountMismatch));
    REQUIRE(fieldCountMismatch.empty());

    INFO("presets compared: " << presetsCompared << " of " << parsed.size());
    REQUIRE(presetsCompared == parsed.size());

    INFO("non-finite field pairs: " << joinPaths(nonFiniteFields));
    REQUIRE(nonFiniteFields.empty());

    INFO("clause 3 - version / length: " << joinPaths(versionOrLength));
    REQUIRE(versionOrLength.empty());

    INFO("clause 2 - Info XML: " << joinPaths(infoMismatch));
    REQUIRE(infoMismatch.empty());

    INFO("clause 4 - integer / enum / bool fields: " << joinPaths(integerMismatch));
    REQUIRE(integerMismatch.empty());

    INFO("clause 5 - payload numPartials: " << joinPaths(numPartialsMismatch));
    REQUIRE(numPartialsMismatch.empty());

    INFO("clause 5 - isValidSpectralState: " << joinPaths(invalidPayload));
    REQUIRE(invalidPayload.empty());

    // The worst observed errors are reported unconditionally: on a green run they
    // are the margin against the pinned tolerances, and on a red one they name the
    // field to investigate before anyone reaches for a wider number.
    std::ostringstream margins;
    margins << "\n=== FR-029 tree-vs-generator margins (T021) ============================\n";
    margins << "  toolchain          : " << toolchainIdentity() << "\n";
    margins << "  presets compared   : " << presetsCompared << "\n";
    margins << std::scientific << std::setprecision(6);
    margins << "  scalar tolerance   : " << kScalarFieldTolerance
            << " (PINNED 2026-08-05 at 1 float ULP - the measured worst was exactly 0 on"
               " MSVC and on GCC 13.3.0, and 10x0 would be a forbidden bit-exact golden)\n";
    margins << "  payload tolerance  : " << kPayloadFieldTolerance
            << " (PINNED 2026-08-05 at 10x GCC 13.3.0's worst, 1.175472e-07)\n";
    margins << "  macOS leg          : NOT YET MEASURED - a disagreement there is"
               " INVESTIGATED, never widened\n";
    margins << renderWorst("SCALAR ", worstScalar);
    margins << renderWorst("PAYLOAD", worstPayload);
    margins << "========================================================================\n";
    WARN(margins.str());

    INFO("clause 5 - scalar float fields: " << joinPaths(scalarMismatch));
    REQUIRE(scalarMismatch.empty());

    INFO("clause 5 - payload float fields: " << joinPaths(payloadMismatch));
    REQUIRE(payloadMismatch.empty());

    INFO("[partials] block: " << joinPaths(partialsMismatch));
    REQUIRE(partialsMismatch.empty());
}
