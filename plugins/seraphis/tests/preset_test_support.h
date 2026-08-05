#pragma once

// ==============================================================================
// Seraphis - factory-preset test support (Phase 12, T007)
// ==============================================================================
// Reference: specs/seraphis-phase12-presets-release/spec.md
//            (FR-025a, FR-026, FR-027b, C-6.1, C-7, C-10)
//            specs/seraphis-phase12-presets-release/tasks.md (T007)
//
// The machinery every Phase 12 preset case shares: locating the committed
// library, parsing a .vstpreset container, decoding a component chunk into TYPED
// values, deriving the C-6.1 render timeline from those values, and the handful
// of measurement primitives the sweep arms are written against.
//
// FOUR CONSTRUCTION RULES, stated here rather than discovered in review:
//
// 1. HEADER-ONLY, EVERY FREE FUNCTION `inline`, EVERY TABLE A FUNCTION-LOCAL
//    `static const`. Two Phase 12 TUs link into the SAME binary (seraphis_tests),
//    so a non-`inline` definition here is a duplicate-symbol LINK ERROR, not a
//    style preference.
//
// 2. THE DECODE IS THE SHIPPED LOADERS, IN getState()'s ORDER, WITH A TRIPWIRE
//    AFTER EVERY STEP. decodePresetState() calls the same `load*Params` free
//    functions Processor::setState() calls, in the order getState() wrote them
//    (processor.cpp:1868-1914), and asserts the cumulative byte offset after each
//    one. Re-deriving a denormalized value with arithmetic in a test would test
//    the test's arithmetic; reading blocks by hardcoded offset would silently
//    slide the moment a block's width moves. The tripwire NAMES the block that
//    drifted, which a bare `size() == 2868` cannot.
//
// 3. THE `[partials]` BLOCK IS THE ONE HAND-READ STEP, AND IT IS READ RAW.
//    loadPartialOverrides (processor.cpp:468-500) scrubs non-finites to 0.0f and
//    clamps into [-1, 1] before storing. The harness must see the bytes AS
//    STORED to be able to assert FR-009's range clause and FR-006a's all-zero
//    rule at all - through the shipped loader those two assertions would be
//    tautologies that pass on a corrupt preset. Hence plain, non-atomic,
//    unclamped fields here.
//
// 4. NEVER std::isnan / std::isinf. The macOS leg builds with -ffast-math, under
//    which both are optimised away. bufferIsFinite() uses
//    Krate::DSP::detail::isNaN / isInf - the same bit-pattern pair
//    processor.cpp:482 screens untrusted stream floats with.
// ==============================================================================

#include "parameters/aether_params.h"
#include "parameters/atmosphere_params.h"
#include "parameters/body_params.h"
#include "parameters/cloud_params.h"
#include "parameters/dropdown_mappings.h"  // toEnvelopeMode (:335)
#include "parameters/effects_params.h"
#include "parameters/global_params.h"
#include "parameters/life_mod_params.h"
#include "parameters/macro_params.h"
#include "parameters/morph_params.h"

#include "base/source/fstreamer.h"                  // IBStreamer, tell() (:215)
#include "public.sdk/source/common/memorystream.h"  // MemoryStream(void*, TSize)

#include <krate/dsp/core/db_utils.h>              // detail::isNaN (:54) / isInf (:175)
#include <krate/dsp/processors/spectral_state.h>  // SpectralState, kSpectralStateBytes
#include <krate/dsp/systems/harmonic_cloud.h>     // HarmonicCloud::kMaxPartials (:138)
#include <krate/dsp/systems/seraphis_voice.h>     // SeraphisVoice::EnvelopeMode (:136)

#include <render_fingerprint.h>  // tests/test_helpers/ - RenderFingerprint (:54-60)

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace SeraphisTest {

// =============================================================================
// Constants
// =============================================================================

/// The shipped component-chunk length (processor.cpp:1836-1843). This is a HARD
/// equality everywhere in Phase 12: a v1/v2-prefix stream would load without
/// complaint (processor.cpp:1762-1790), so only the exact length rejects one.
inline constexpr std::size_t kSeraphisStateBytes = 2868;

/// The `[partials]` block's pan count (processor.cpp:420-432).
inline constexpr std::size_t kPartialCount = 64;

/// Where the four `SpectralState` payloads start inside the component chunk, and
/// how many bytes they occupy: `[version] 4 + [global] 12 + [macro] 20 +
/// [seed] 4 + [cloud] 44 + [morph scalars] 52 = 136`, then `4 x 541 = 2164`.
///
/// These two are what makes FR-025a's HAND-SKIP possible: the typed decode
/// advances the streamer by kSpectralPayloadBlockBytes without looking at the
/// payloads, and FR-029 clause 5's separate pass reads them straight out of the
/// chunk at kSpectralPayloadOffset. Both numbers are re-derived below by
/// static_assert from the shipped kSpectralStateBytes, so a format change breaks
/// the build rather than shifting a silent skip.
inline constexpr std::size_t kSpectralPayloadOffset = 136;
inline constexpr std::size_t kSpectralPayloadCount = 4;
inline constexpr std::size_t kSpectralPayloadBlockBytes =
    kSpectralPayloadCount * Krate::DSP::kSpectralStateBytes;

static_assert(Krate::DSP::kSpectralStateBytes == 541,
              "FR-025a: the hand-skip advances by the SHIPPED payload width");
static_assert(kSpectralPayloadOffset + kSpectralPayloadBlockBytes == 2300,
              "FR-025a: [morph] ends at cumulative offset 2300 (preset_test_support tripwire)");

static_assert(kPartialCount == Krate::DSP::HarmonicCloud::kMaxPartials,
              "FR-034a: the [partials] block is 64 pan floats + two 64-bit masks; a wider "
              "cloud changes the block and this header with it");

/// Header of a .vstpreset container: "VST3" + uint32 version + 32 ASCII class-id
/// chars + int64 listOffset (tools/membrum_preset_generator.cpp:375-383).
inline constexpr std::size_t kVstPresetHeaderBytes = 48;

/// The chunk-list entry width: 4-byte id + int64 offset + int64 size.
inline constexpr std::size_t kChunkEntryBytes = 20;

// =============================================================================
// Discovery
// =============================================================================
// SERAPHIS_RESOURCES_DIR is handed to this target by
// plugins/seraphis/tests/CMakeLists.txt:119. There is deliberately NO walk-up
// loop: a walk-up finds whatever `presets` directory happens to sit above the
// working directory, which on a developer machine can be the INSTALLED library
// under %PROGRAMDATA% rather than the committed tree under test.

[[nodiscard]] inline std::filesystem::path factoryPresetRoot() {
    return std::filesystem::path(SERAPHIS_RESOURCES_DIR) / "presets";
}

/// Every committed `.vstpreset`, at any depth, SORTED.
///
/// The sort is not cosmetic: directory-iteration order is unspecified by the
/// standard and differs between NTFS, ext4 and APFS, so an unsorted list makes
/// "the closest pair was #17 and #31" unreproducible across legs and makes any
/// index-keyed failure message meaningless.
[[nodiscard]] inline std::vector<std::filesystem::path> allPresetFiles() {
    std::vector<std::filesystem::path> out;
    const std::filesystem::path root = factoryPresetRoot();
    if (!std::filesystem::is_directory(root)) {
        return out;
    }
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
        if (entry.is_regular_file() && entry.path().extension() == ".vstpreset") {
            out.push_back(entry.path());
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

// =============================================================================
// Container parsing
// =============================================================================

struct PresetFile {
    std::filesystem::path path;
    std::string stem;          ///< file name without the extension
    std::string category;      ///< the PARENT DIRECTORY name - the browser's own source
                               ///< (preset_manager.cpp:95-103), never the XML
    std::string classIdAscii;  ///< the 32 header chars, verbatim
    std::vector<std::uint8_t> comp;  ///< the `Comp` chunk payload
    std::string info;                ///< the `Info` chunk payload (XML text)
    std::string parseError;          ///< EMPTY on success; the first failure otherwise
};

namespace detail {

/// Little-endian byte assembly rather than a memcpy of the object
/// representation: the FILE is little-endian by definition, and assembling from
/// individual bytes says so without needing an endianness assertion.
[[nodiscard]] inline bool readLE32(const std::vector<std::uint8_t>& bytes, std::size_t offset,
                                   std::uint32_t& out) {
    if (offset + 4u > bytes.size()) {
        return false;
    }
    out = static_cast<std::uint32_t>(bytes[offset])
          | (static_cast<std::uint32_t>(bytes[offset + 1u]) << 8)
          | (static_cast<std::uint32_t>(bytes[offset + 2u]) << 16)
          | (static_cast<std::uint32_t>(bytes[offset + 3u]) << 24);
    return true;
}

[[nodiscard]] inline bool readLE64(const std::vector<std::uint8_t>& bytes, std::size_t offset,
                                   std::int64_t& out) {
    if (offset + 8u > bytes.size()) {
        return false;
    }
    std::uint64_t v = 0;
    for (std::size_t i = 0; i < 8u; ++i) {
        v |= static_cast<std::uint64_t>(bytes[offset + i]) << (8u * i);
    }
    out = static_cast<std::int64_t>(v);
    return true;
}

[[nodiscard]] inline bool tagAt(const std::vector<std::uint8_t>& bytes, std::size_t offset,
                                const char* tag) {
    if (offset + 4u > bytes.size()) {
        return false;
    }
    return std::memcmp(bytes.data() + offset, tag, 4u) == 0;
}

}  // namespace detail

/// Read and structurally validate one `.vstpreset`.
///
/// Every structural failure is reported through `parseError` rather than through
/// an assertion, so a case can print WHICH file failed and WHY over a 42-file
/// sweep instead of aborting on the first one.
[[nodiscard]] inline PresetFile parseVstPreset(const std::filesystem::path& path) {
    PresetFile pf;
    pf.path = path;
    pf.stem = path.stem().string();
    if (path.has_parent_path()) {
        pf.category = path.parent_path().filename().string();
    }

    std::ifstream in(path, std::ios::binary);
    if (!in) {
        pf.parseError = "cannot open " + path.string();
        return pf;
    }
    const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)),
                                          std::istreambuf_iterator<char>());

    if (bytes.size() < kVstPresetHeaderBytes) {
        pf.parseError = "file is " + std::to_string(bytes.size()) + " bytes, shorter than the "
                        + std::to_string(kVstPresetHeaderBytes) + "-byte header";
        return pf;
    }
    if (!detail::tagAt(bytes, 0, "VST3")) {
        pf.parseError = "missing \"VST3\" magic at offset 0";
        return pf;
    }

    std::uint32_t formatVersion = 0;
    if (!detail::readLE32(bytes, 4, formatVersion) || formatVersion != 1u) {
        pf.parseError = "container format version is " + std::to_string(formatVersion)
                        + ", expected 1";
        return pf;
    }

    pf.classIdAscii.assign(reinterpret_cast<const char*>(bytes.data()) + 8, 32u);

    std::int64_t listOffset = 0;
    if (!detail::readLE64(bytes, 40, listOffset) || listOffset < 0
        || static_cast<std::uint64_t>(listOffset) + 8u > bytes.size()) {
        pf.parseError = "listOffset " + std::to_string(listOffset) + " is outside the file ("
                        + std::to_string(bytes.size()) + " bytes)";
        return pf;
    }

    const auto listAt = static_cast<std::size_t>(listOffset);
    if (!detail::tagAt(bytes, listAt, "List")) {
        pf.parseError = "no \"List\" tag at listOffset " + std::to_string(listOffset);
        return pf;
    }

    std::uint32_t entryCount = 0;
    if (!detail::readLE32(bytes, listAt + 4u, entryCount)) {
        pf.parseError = "truncated chunk-list entry count";
        return pf;
    }
    if (listAt + 8u + (static_cast<std::size_t>(entryCount) * kChunkEntryBytes) > bytes.size()) {
        pf.parseError = "chunk list declares " + std::to_string(entryCount)
                        + " entries, which runs past the end of the file";
        return pf;
    }

    // Both chunks are located BEFORE either is copied out, so a file missing one
    // of them reports that fact rather than a bounds failure on the other.
    std::optional<std::pair<std::size_t, std::size_t>> compSpan;
    std::optional<std::pair<std::size_t, std::size_t>> infoSpan;

    for (std::uint32_t i = 0; i < entryCount; ++i) {
        const std::size_t entryAt = listAt + 8u + (static_cast<std::size_t>(i) * kChunkEntryBytes);
        std::int64_t chunkOffset = 0;
        std::int64_t chunkSize = 0;
        if (!detail::readLE64(bytes, entryAt + 4u, chunkOffset)
            || !detail::readLE64(bytes, entryAt + 12u, chunkSize)) {
            pf.parseError = "truncated chunk-list entry " + std::to_string(i);
            return pf;
        }
        if (chunkOffset < 0 || chunkSize < 0
            || static_cast<std::uint64_t>(chunkOffset) + static_cast<std::uint64_t>(chunkSize)
                   > bytes.size()) {
            pf.parseError = "chunk-list entry " + std::to_string(i) + " spans ["
                            + std::to_string(chunkOffset) + ", "
                            + std::to_string(chunkOffset + chunkSize) + ") which is outside the "
                            + std::to_string(bytes.size()) + "-byte file";
            return pf;
        }
        const auto span = std::make_pair(static_cast<std::size_t>(chunkOffset),
                                         static_cast<std::size_t>(chunkSize));
        if (detail::tagAt(bytes, entryAt, "Comp")) {
            compSpan = span;
        } else if (detail::tagAt(bytes, entryAt, "Info")) {
            infoSpan = span;
        }
    }

    if (!compSpan.has_value()) {
        pf.parseError = "chunk list carries no \"Comp\" entry";
        return pf;
    }
    if (!infoSpan.has_value()) {
        pf.parseError = "chunk list carries no \"Info\" entry";
        return pf;
    }

    pf.comp.assign(bytes.begin() + static_cast<std::ptrdiff_t>(compSpan->first),
                   bytes.begin()
                       + static_cast<std::ptrdiff_t>(compSpan->first + compSpan->second));
    pf.info.assign(reinterpret_cast<const char*>(bytes.data()) + infoSpan->first,
                   infoSpan->second);
    return pf;
}

/// `<Attr id="X" value="Y" type="string"/>` -> { "X" -> "Y" }.
///
/// A MISSING ATTRIBUTE IS ABSENT FROM THE MAP - never a default-constructed
/// empty string. SC-006 asserts on the presence of exactly six ids, and a
/// defaulting lookup would turn "the generator forgot MusicalInstrument" into
/// "MusicalInstrument is empty", which reads as a value mismatch rather than as
/// the structural fault it is.
[[nodiscard]] inline std::map<std::string, std::string> parseInfoAttributes(std::string_view xml) {
    std::map<std::string, std::string> out;

    // Each `<Attr .../>` element is isolated FIRST and the two keys are searched
    // only inside it, so a malformed document cannot pair one element's id with
    // the next element's value.
    std::size_t pos = 0;
    while ((pos = xml.find("<Attr", pos)) != std::string_view::npos) {
        const std::size_t end = xml.find('>', pos);
        if (end == std::string_view::npos) {
            break;
        }
        const std::string_view element = xml.substr(pos, end - pos);

        const auto attribute = [element](std::string_view key) -> std::optional<std::string> {
            const std::string needle = std::string(key) + "=\"";
            const std::size_t at = element.find(needle);
            if (at == std::string_view::npos) {
                return std::nullopt;
            }
            const std::size_t valueBegin = at + needle.size();
            const std::size_t valueEnd = element.find('"', valueBegin);
            if (valueEnd == std::string_view::npos) {
                return std::nullopt;
            }
            return std::string(element.substr(valueBegin, valueEnd - valueBegin));
        };

        const std::optional<std::string> id = attribute("id");
        const std::optional<std::string> value = attribute("value");
        if (id.has_value() && value.has_value()) {
            out.emplace(*id, *value);
        }
        pos = end + 1u;
    }
    return out;
}

// =============================================================================
// Typed decode (FR-025a)
// =============================================================================

/// One instance of every shipped per-block params pack, plus the two blocks that
/// have no pack.
///
/// The packs hold `std::atomic` members, so this struct is neither copyable nor
/// movable. That is deliberate: it is always filled in place through
/// decodePresetState(&out) and a silent copy would not compile rather than
/// silently costing a per-preset duplicate in a 42x42 sweep.
struct DecodedPresetState {
    Steinberg::int32 version = 0;

    Seraphis::GlobalParams global;
    Seraphis::MacroParams macro;
    Seraphis::CloudParams cloud;
    Seraphis::MorphParams morph;
    Seraphis::LifeModParams life;
    Seraphis::BodyParams body;
    Seraphis::AtmosphereParams atmos;
    Seraphis::AetherParams aether;
    Seraphis::EffectsParams effects;

    /// The four 541-byte payloads.
    ///
    /// FR-025a's decode mechanism does NOT produce these: it HAND-SKIPS the 2164
    /// payload bytes (see decodePresetState below) because a `SpectralState` is
    /// not a typed pack field and FR-029 clause 5 owns its comparison, at a
    /// separately measured tolerance. They are filled by a second, explicitly
    /// separate pass - decodeSpectralPayloads() - which calls the shipped
    /// `Krate::DSP::deserializeSpectralState` directly on the raw chunk bytes,
    /// exactly as FR-029 clause 5 words it.
    std::array<Krate::DSP::SpectralState, 4> payloads{};

    /// false for any slot whose 541 bytes `deserializeSpectralState` REJECTED.
    /// The slot then still holds its default-constructed value (the deserializer
    /// leaves `out` bitwise untouched on rejection, spectral_state.h:274-286), so
    /// without this flag a rejected payload is indistinguishable from a decoded
    /// one and every FR-029 clause-5 comparison against it would silently pass.
    std::array<bool, 4> payloadDecoded{};

    /// The `[partials]` block, RAW: no NaN scrub and no clamp - see rule 3 in the
    /// file banner.
    std::array<float, kPartialCount> partialPan{};
    std::uint64_t panOverrideBits = 0;
    std::uint64_t maskBits = 0;
};

/// FR-029 CLAUSE 5's payload pass, and the ONLY place a `SpectralState` is
/// decoded in this header.
///
/// It is deliberately NOT part of FR-025a's typed-field mechanism: that
/// mechanism reads scalar packs through their shipped `load*Params` functions
/// and hand-skips these 2164 bytes. Here the bytes are read straight out of the
/// chunk at their fixed offset and handed to the shipped
/// `Krate::DSP::deserializeSpectralState`.
///
/// A REJECTED payload is recorded as `payloadDecoded[i] == false` rather than
/// swallowed: `deserializeSpectralState` leaves its output bitwise untouched on
/// rejection (spectral_state.h:274-286), so a caller that ignored the flag would
/// compare two default-constructed states and call them equal.
inline void decodeSpectralPayloads(const std::vector<std::uint8_t>& comp,
                                   std::array<Krate::DSP::SpectralState, 4>& payloads,
                                   std::array<bool, 4>& decoded) {
    for (std::size_t i = 0; i < payloads.size(); ++i) {
        const std::size_t at = kSpectralPayloadOffset + (i * Krate::DSP::kSpectralStateBytes);
        // std::byte-aliased read of the caller's std::uint8_t buffer: both are
        // narrow character-adjacent types, so this is a legal reinterpretation
        // and not a copy of 541 bytes per slot per preset.
        const auto* src = reinterpret_cast<const std::byte*>(comp.data() + at);
        decoded[i] = Krate::DSP::deserializeSpectralState(src, Krate::DSP::kSpectralStateBytes,
                                                          payloads[i]);
    }
}

/// Decode a component chunk into typed values, in getState()'s exact order.
///
/// @return true only when every block loaded AND every cumulative offset matched.
///         On false, @p why names the block that drifted or failed.
///
/// The cumulative offsets below are getState()'s block widths
/// (processor.cpp:1868-1914), summed:
///
///   [version] 4 | [global] 12 -> 16 | [macro] 20 -> 36 | [seed] 4 -> 40 |
///   [cloud] 44 -> 84 | [morph] 52 scalars -> 136, + 2164 skipped payload -> 2300 |
///   [life] 40 -> 2340 | [body] 52 -> 2392 | [atmos] 68 -> 2460 |
///   [aether] 72 -> 2532 | [effects] 64 -> 2596 | [partials] 272 -> 2868
[[nodiscard]] inline bool decodePresetState(const std::vector<std::uint8_t>& comp,
                                            DecodedPresetState& out, std::string& why) {
    why.clear();

    if (comp.size() != kSeraphisStateBytes) {
        why = "component chunk is " + std::to_string(comp.size()) + " bytes, expected "
              + std::to_string(kSeraphisStateBytes);
        return false;
    }

    // MemoryStream's two-argument constructor BORROWS the buffer (ownMemory ==
    // false, memorystream.cpp:60-67), so a local mutable copy is what keeps this
    // free of a const_cast on the caller's data. 2868 bytes is nothing in a test.
    std::vector<std::uint8_t> borrowed = comp;
    Steinberg::MemoryStream stream(borrowed.data(),
                                   static_cast<Steinberg::TSize>(borrowed.size()));
    Steinberg::IBStreamer streamer(&stream, kLittleEndian);

    // THE TRIPWIRE. Asserting only the 2868-byte total would pass a decode in
    // which two adjacent blocks were mis-sized in opposite directions; asserting
    // after EVERY step names the one that moved.
    const auto step = [&streamer, &why](const char* block, std::int64_t expectedCumulative,
                                        bool loaded) -> bool {
        if (!loaded) {
            why = std::string("loader for ") + block + " returned false (short stream)";
            return false;
        }
        const std::int64_t at = streamer.tell();
        if (at != expectedCumulative) {
            why = std::string("stream drifted after ") + block + ": tell() = "
                  + std::to_string(at) + ", expected " + std::to_string(expectedCumulative);
            return false;
        }
        return true;
    };

    Steinberg::int32 version = 0;
    if (!step("[version]", 4, streamer.readInt32(version))) {
        return false;
    }
    out.version = version;

    if (!step("[global]", 16, Seraphis::loadGlobalParams(out.global, streamer))) {
        return false;
    }
    if (!step("[macro]", 36, Seraphis::loadMacroParams(out.macro, streamer))) {
        return false;
    }
    if (!step("[seed]", 40, Seraphis::loadGlobalSeed(out.global, streamer))) {
        return false;
    }
    if (!step("[cloud]", 84, Seraphis::loadCloudParams(out.cloud, streamer))) {
        return false;
    }
    // FR-025a, LITERALLY: the SCALAR overload reads the thirteen [morph] scalars
    // (52 bytes) through the shipped loader, and the four 541-byte SpectralState
    // payloads are then HAND-SKIPPED by their known byte count. They are not
    // decoded here; decodeSpectralPayloads() below does that separately, through
    // deserializeSpectralState, which is what FR-029 clause 5 asks for.
    if (!step("[morph scalars]", 136, Seraphis::loadMorphParams(out.morph, streamer))) {
        return false;
    }
    const std::int64_t afterSkip =
        streamer.seek(static_cast<Steinberg::int64>(kSpectralPayloadBlockBytes),
                      Steinberg::kSeekCurrent);
    if (!step("[morph payloads] (hand-skipped)", 2300, afterSkip >= 0)) {
        return false;
    }
    if (!step("[life]", 2340, Seraphis::loadLifeModParams(out.life, streamer))) {
        return false;
    }
    if (!step("[body]", 2392, Seraphis::loadBodyParams(out.body, streamer))) {
        return false;
    }
    if (!step("[atmos]", 2460, Seraphis::loadAtmosphereParams(out.atmos, streamer))) {
        return false;
    }
    if (!step("[aether]", 2532, Seraphis::loadAetherParams(out.aether, streamer))) {
        return false;
    }
    if (!step("[effects]", 2596, Seraphis::loadEffectsParams(out.effects, streamer))) {
        return false;
    }

    // --- [partials]: the ONE hand-read block, and it is read RAW ---------------
    // Same readFloat / readInt64u calls loadPartialOverrides uses
    // (processor.cpp:472-499), into plain fields with NO clamping and NO NaN
    // scrub - see rule 3 in the file banner.
    for (std::size_t i = 0; i < out.partialPan.size(); ++i) {
        float value = 0.0f;
        if (!streamer.readFloat(value)) {
            why = "[partials] pan " + std::to_string(i) + ": short read";
            return false;
        }
        out.partialPan[i] = value;
    }

    Steinberg::uint64 storedPanBits = 0;
    if (!streamer.readInt64u(storedPanBits)) {
        why = "[partials] panOverrideBits: short read";
        return false;
    }
    Steinberg::uint64 storedMaskBits = 0;
    if (!streamer.readInt64u(storedMaskBits)) {
        why = "[partials] maskBits: short read";
        return false;
    }
    out.panOverrideBits = static_cast<std::uint64_t>(storedPanBits);
    out.maskBits = static_cast<std::uint64_t>(storedMaskBits);

    if (!step("[partials]", static_cast<std::int64_t>(kSeraphisStateBytes), true)) {
        return false;
    }

    // FR-029 clause 5's pass, run AFTER the tripwire has confirmed the skip
    // landed where it should. It touches the streamer not at all - it reads the
    // chunk directly - so it cannot perturb the offsets asserted above.
    decodeSpectralPayloads(comp, out.payloads, out.payloadDecoded);
    return true;
}

// =============================================================================
// The render timeline (C-6.1)
// =============================================================================

/// Every offset is derived from the preset's OWN decoded state - nothing here is
/// hardcoded per preset, which is what makes FR-008a's authoring ceiling the only
/// thing bounding the sweep's wall clock.
struct SweepTimeline {
    double A = 0.0;         ///< attack span, s
    double Rel = 0.0;       ///< release, s
    double RT60 = 0.0;      ///< stored Aether decay - a TRUE RT60 (aether_reverb.h:3139)
    double susBegin = 0.0;  ///< sustain-measurement window start, s
    double susEnd = 0.0;    ///< sustain-measurement window end, s
    double H = 0.0;         ///< NoteOff instant, s; [0, H] is the NoteOn-only hold
    double settle = 0.0;    ///< tail-measurement window start, s
    double W = 0.0;         ///< tail-measurement span, s
    double total = 0.0;     ///< render length, s

    bool aetherFreeze = false;      ///< kAetherFreezeId (1204)
    bool atmosOrFxFreeze = false;   ///< kAtmosFreezeId (1008) or kFxSpectralFreezeId (1430)
};

[[nodiscard]] inline SweepTimeline makeTimeline(const DecodedPresetState& st) {
    SweepTimeline t;

    // toEnvelopeMode() rather than a literal `== 1`: it is the shipped mapping
    // (dropdown_mappings.h:335-338) and it CLAMPS, so an out-of-range stored index
    // classifies here exactly as the processor would classify it.
    const bool growth =
        Seraphis::toEnvelopeMode(st.life.envMode.load(std::memory_order_relaxed))
        == Krate::DSP::SeraphisVoice::EnvelopeMode::Growth;

    const double stageOne =
        growth ? static_cast<double>(st.life.growthDurationSec.load(std::memory_order_relaxed))
               : static_cast<double>(st.life.stage0Ms.load(std::memory_order_relaxed)) * 1.0e-3;

    t.A = stageOne
          + (static_cast<double>(st.life.stage1Ms.load(std::memory_order_relaxed)) * 1.0e-3);
    t.Rel = static_cast<double>(st.life.releaseMs.load(std::memory_order_relaxed)) * 1.0e-3;
    t.RT60 = static_cast<double>(st.aether.decaySeconds.load(std::memory_order_relaxed));

    t.susBegin = t.A + 1.0;
    t.susEnd = t.A + 4.0;
    t.H = t.A + 5.0;

    t.aetherFreeze = st.aether.freeze.load(std::memory_order_relaxed);
    t.atmosOrFxFreeze = st.atmos.freeze.load(std::memory_order_relaxed)
                        || st.effects.spectralFreeze.load(std::memory_order_relaxed);

    const bool frozen = t.aetherFreeze || t.atmosOrFxFreeze;
    const double settlingAllowance = frozen ? 2.0 : 0.5;
    t.settle = t.H + t.Rel + settlingAllowance;

    // C-6.3: only Aether-freeze is PROMISED energy-conserving, and only a 60 s
    // window can tell a conserving tail from a 60 s-RT60 one.
    t.W = t.aetherFreeze ? 60.0 : (frozen ? 20.0 : 10.0);
    t.total = t.settle + t.W;

    return t;
}

/// Seconds -> sample index. Windows built from this are HALF-OPEN: [n(t0), n(t1)).
[[nodiscard]] inline std::size_t sampleIndex(double seconds, double sampleRate) {
    if (seconds <= 0.0 || sampleRate <= 0.0) {
        return 0;
    }
    return static_cast<std::size_t>(std::llround(seconds * sampleRate));
}

// =============================================================================
// Measurement
// =============================================================================

/// BIT PATTERN, never std::isnan - the macOS leg is -ffast-math.
[[nodiscard]] inline bool bufferIsFinite(std::span<const float> samples) {
    for (const float sample : samples) {
        if (Krate::DSP::detail::isNaN(sample) || Krate::DSP::detail::isInf(sample)) {
            return false;
        }
    }
    return true;
}

/// Stereo RMS over the HALF-OPEN window [first, lastExclusive):
/// sqrt((sum L^2 + sum R^2) / (2N)). Accumulated in double so the statistic is
/// not itself a source of cross-toolchain spread.
///
/// The window is CLAMPED to what both channels actually hold, so a short buffer
/// measures less rather than reading out of bounds.
[[nodiscard]] inline double rmsOver(std::span<const float> l, std::span<const float> r,
                                    std::size_t first, std::size_t lastExclusive) {
    const std::size_t last = std::min({l.size(), r.size(), lastExclusive});
    if (first >= last) {
        return 0.0;
    }

    double sumSquares = 0.0;
    for (std::size_t i = first; i < last; ++i) {
        const double a = static_cast<double>(l[i]);
        const double b = static_cast<double>(r[i]);
        sumSquares += (a * a) + (b * b);
    }
    const double count = static_cast<double>(last - first);
    return std::sqrt(sumSquares / (2.0 * count));
}

/// One stereo RMS per WHOLE second over [first, lastExclusive).
///
/// A trailing partial second is DROPPED rather than measured short: SC-012's
/// arms compare entries of this series against one another, and a final entry
/// covering a fraction of a second would be a systematically different statistic
/// masquerading as the same one.
[[nodiscard]] inline std::vector<double> perSecondRms(std::span<const float> l,
                                                      std::span<const float> r, std::size_t first,
                                                      std::size_t lastExclusive,
                                                      double sampleRate) {
    std::vector<double> out;
    if (sampleRate <= 0.0) {
        return out;
    }
    const std::size_t last = std::min({l.size(), r.size(), lastExclusive});
    const auto stride = static_cast<std::size_t>(std::llround(sampleRate));
    if (stride == 0 || first >= last) {
        return out;
    }

    for (std::size_t begin = first; begin + stride <= last; begin += stride) {
        out.push_back(rmsOver(l, r, begin, begin + stride));
    }
    return out;
}

/// FR-027b / C-10 (OI-4): the pairwise distinctness metric.
///
/// `rms` and the 32 checkpoints are EXCLUDED, and both exclusions are load-bearing:
///
///   * the caller normalises each buffer to unit RMS BEFORE fingerprinting (so
///     that two timbrally identical presets differing only in master gain score
///     ZERO rather than large), after which `rms` is identically 1 and carries no
///     information at all;
///   * the checkpoints are 32 evenly spaced RAW samples (render_fingerprint.h:83-86),
///     dominated at a 3 s window by instantaneous phase - two near-identical
///     presets differing by a few cents of drift would score near-maximum there
///     and, inside a max(), make the arm pass unconditionally.
///
/// What survives are three SHAPE statistics of a unit-level signal: `peak` is the
/// crest factor, `meanAbs` the inverse form factor, `totalVariation` a brightness
/// proxy.
[[nodiscard]] inline double fingerprintDistance(
    const Krate::DSP::TestUtils::RenderFingerprint& a,
    const Krate::DSP::TestUtils::RenderFingerprint& b) {
    const auto relative = [](double x, double y) {
        return std::abs(x - y) / std::max({std::abs(x), std::abs(y), 1.0e-12});
    };
    return std::max({relative(a.peak, b.peak), relative(a.meanAbs, b.meanAbs),
                     relative(a.totalVariation, b.totalVariation)});
}

}  // namespace SeraphisTest
