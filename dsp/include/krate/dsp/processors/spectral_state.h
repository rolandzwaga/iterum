#pragma once

// ==============================================================================
// Layer 2: Processors
// spectral_state.h - SpectralState, the Seraphis morph endpoint data model
// ==============================================================================
// Spec: specs/seraphis-phase3-spectral-morph/spec.md  (FR-011 - FR-014, FR-021 - FR-023,
//                                                      FR-031 - FR-033)
// Plan: specs/seraphis-phase3-spectral-morph/plan.md  (sections 3.1 - 3.4)
//
// A SpectralState is a plain, trivially copyable description of one spectral
// endpoint: partial ratios, partial amplitudes, a global tilt, an inharmonicity
// amount and a short ASCII label. It owns no resources and has NO MEMBER
// FUNCTIONS -- every operation is a free function, which is what keeps the
// deferred authoring mutators a pure addition later.
//
// Layer discipline: Layer 0 + stdlib only. <krate/dsp/core/db_utils.h> is the
// DEFINITION site of detail::isNaN (:54) / detail::isInf (:175); FR-007 forbids
// reaching for a Layer 3 use site for those checks.
//
// std::isnan / std::isinf are NOT usable here: the VST3 SDK and the macOS leg
// build with -ffast-math, under which they are optimised away. The bit-pattern
// checks in db_utils.h are the portable form.
// ==============================================================================

#include <krate/dsp/core/db_utils.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace Krate::DSP {

/// @brief One spectral morph endpoint: ratios, amplitudes, tilt, inharmonicity, label (FR-011).
///
/// Aggregate-initialisable in C++20 despite the default member initialisers, and
/// trivially copyable (NSDMIs affect trivial *default construction*, not trivial
/// copyability). The default value is valid, finite, silent and anonymous.
struct SpectralState {
    /// Partial slot count. Class-scoped on purpose: Krate::DSP::kMaxPartials (96)
    /// already exists at namespace scope in processors/harmonic_types.h:21, so no
    /// new namespace-scope partial-count constant is introduced.
    static constexpr std::size_t kStatePartials = 64; ///< == HarmonicCloud::kMaxPartials (:133)
    static constexpr std::size_t kStateNameBytes = 16;

    static constexpr float kMinStateRatio = 0.5f; ///< FR-012: finite log-ratio span
    static constexpr float kMaxStateRatio = 128.0f;
    static constexpr float kMinStateTiltDbPerOct = -12.0f; ///< spectral_tilt.h:98-101 convention
    static constexpr float kMaxStateTiltDbPerOct = 12.0f;
    static constexpr float kMaxStateInharmonicity = 0.1f; ///< HarmonicCloud::kMaxInharmonicity :186

    std::array<float, kStatePartials> ratios{};     ///< Strictly increasing over [0, numPartials)
    std::array<float, kStatePartials> amplitudes{}; ///< In [0, 1] over [0, numPartials)
    std::array<char, kStateNameBytes> name{};       ///< NUL-terminated printable ASCII
    float tiltDbPerOct = 0.0f;
    float inharmonicity = 0.0f;
    int numPartials = 0;
};

static_assert(std::is_trivially_copyable_v<SpectralState>);
static_assert(SpectralState::kStatePartials == 64);

/// @brief Validate a SpectralState (FR-012).
///
/// Checks, in this order and with early exit:
///   - numPartials in [0, kStatePartials];
///   - for i < numPartials: ratios[i] finite, in [kMinStateRatio, kMaxStateRatio]
///     and STRICTLY greater than ratios[i-1]; amplitudes[i] finite in [0, 1];
///   - name contains at least one '\0' and no byte before it is < 0x20 or == 0x7F;
///   - tiltDbPerOct finite in [kMinStateTiltDbPerOct, kMaxStateTiltDbPerOct];
///   - inharmonicity finite in [0, kMaxStateInharmonicity].
///
/// Entries at i >= numPartials are NOT examined -- they are scratch space that
/// the morph engine's geometric continuation owns.
///
/// Real-time safe: no allocation, no locks, no exceptions.
[[nodiscard]] inline bool isValidSpectralState(const SpectralState& s) noexcept {
    if (s.numPartials < 0 || s.numPartials > static_cast<int>(SpectralState::kStatePartials)) {
        return false;
    }

    const auto count = static_cast<std::size_t>(s.numPartials);
    float previousRatio = 0.0f;
    for (std::size_t i = 0; i < count; ++i) {
        const float ratio = s.ratios[i];
        if (detail::isNaN(ratio) || detail::isInf(ratio)) {
            return false;
        }
        if (ratio < SpectralState::kMinStateRatio || ratio > SpectralState::kMaxStateRatio) {
            return false;
        }
        if (i > 0 && ratio <= previousRatio) {
            return false;
        }
        previousRatio = ratio;

        const float amplitude = s.amplitudes[i];
        if (detail::isNaN(amplitude) || detail::isInf(amplitude)) {
            return false;
        }
        if (amplitude < 0.0f || amplitude > 1.0f) {
            return false;
        }
    }

    std::size_t terminator = SpectralState::kStateNameBytes;
    for (std::size_t i = 0; i < SpectralState::kStateNameBytes; ++i) {
        if (s.name[i] == '\0') {
            terminator = i;
            break;
        }
    }
    if (terminator == SpectralState::kStateNameBytes) {
        return false; // No NUL anywhere in the field.
    }
    for (std::size_t i = 0; i < terminator; ++i) {
        // char's signedness is implementation-defined; compare through unsigned char.
        const auto byte = static_cast<unsigned char>(s.name[i]);
        if (byte < 0x20u || byte == 0x7Fu) {
            return false;
        }
    }

    if (detail::isNaN(s.tiltDbPerOct) || detail::isInf(s.tiltDbPerOct)) {
        return false;
    }
    if (s.tiltDbPerOct < SpectralState::kMinStateTiltDbPerOct
        || s.tiltDbPerOct > SpectralState::kMaxStateTiltDbPerOct) {
        return false;
    }

    if (detail::isNaN(s.inharmonicity) || detail::isInf(s.inharmonicity)) {
        return false;
    }
    if (s.inharmonicity < 0.0f || s.inharmonicity > SpectralState::kMaxStateInharmonicity) {
        return false;
    }

    return true;
}

/// @brief L2-normalise the active amplitudes in place (FR-014).
///
/// The harmonic_snapshot.h:99-107 idiom over [0, numPartials): accumulate
/// sumSquares and scale by 1/sqrt(sumSquares) ONLY if sumSquares > 0. An
/// all-zero state and a numPartials == 0 state are therefore left untouched --
/// the guard is what keeps NaN out.
///
/// Real-time safe: no allocation, no locks, no exceptions.
inline void normalizeSpectralState(SpectralState& s) noexcept {
    std::size_t count = 0;
    if (s.numPartials > 0) {
        count = static_cast<std::size_t>(s.numPartials);
        if (count > SpectralState::kStatePartials) {
            count = SpectralState::kStatePartials;
        }
    }

    float sumSquares = 0.0f;
    for (std::size_t i = 0; i < count; ++i) {
        sumSquares += s.amplitudes[i] * s.amplitudes[i];
    }

    if (sumSquares > 0.0f) {
        const float invNorm = 1.0f / std::sqrt(sumSquares);
        for (std::size_t i = 0; i < count; ++i) {
            s.amplitudes[i] *= invNorm;
        }
    }
}

// ==============================================================================
// Serialization (FR-031 - FR-033) -- plan section 3.3
// ==============================================================================

/// @brief Stream format version written at offset 0 (FR-032).
inline constexpr std::uint8_t kSpectralStateFormatVersion = 1;

/// @brief Exact size of one serialized SpectralState: 1 + 4 + 4 + 4 + 256 + 256 + 16.
inline constexpr std::size_t kSpectralStateBytes = 1 + 4 + 4 + 4 + 256 + 256 + 16;
static_assert(kSpectralStateBytes == 541);

namespace detail::state_io {

// The FR-031 layout. Every offset is a constant here rather than a running
// cursor so that a field reorder is a visible edit, not a silent one.
inline constexpr std::size_t kVersionOffset = 0;
inline constexpr std::size_t kNumPartialsOffset = 1;
inline constexpr std::size_t kTiltOffset = 5;
inline constexpr std::size_t kInharmonicityOffset = 9;
inline constexpr std::size_t kRatiosOffset = 13;
inline constexpr std::size_t kAmplitudesOffset = 269;
inline constexpr std::size_t kNameOffset = 525;

inline constexpr std::size_t kPartialArrayBytes = sizeof(float) * SpectralState::kStatePartials;

static_assert(kPartialArrayBytes == 256);
static_assert(kRatiosOffset + kPartialArrayBytes == kAmplitudesOffset);
static_assert(kAmplitudesOffset + kPartialArrayBytes == kNameOffset);
static_assert(kNameOffset + SpectralState::kStateNameBytes == kSpectralStateBytes);

// The layout is defined as LITTLE-ENDIAN and every scalar moves as a raw
// std::memcpy of its object representation, which is host-endian. Every target
// this repo builds for (x86-64 and arm64 on Windows, macOS and Linux) is
// little-endian; this assertion is what turns that from an assumption into a
// build-time contract, so a big-endian port fails loudly instead of silently
// emitting byte-swapped presets.
static_assert(std::endian::native == std::endian::little,
              "kSpectralStateBytes layout is little-endian; add byte swapping for this target");

} // namespace detail::state_io

/// @brief Write @p s into @p dest as exactly kSpectralStateBytes bytes (FR-031).
///
/// Layout, little-endian, fixed order:
///   | Offset | Size | Field                       |
///   |      0 |    1 | kSpectralStateFormatVersion |
///   |      1 |    4 | numPartials (std::int32_t)  |
///   |      5 |    4 | tiltDbPerOct (float bits)   |
///   |      9 |    4 | inharmonicity (float bits)  |
///   |     13 |  256 | ratios[0..63]               |
///   |    269 |  256 | amplitudes[0..63]           |
///   |    525 |   16 | name[0..15] verbatim        |
///
/// Every scalar moves as a std::memcpy of its bit pattern -- no
/// reinterpret_cast, no text formatting, and no arithmetic on std::byte (whose
/// value would first have to go through unsigned char to be well defined).
///
/// @return kSpectralStateBytes on success; 0 -- having written NOTHING -- if
///         @p dest is null, @p capacity is short, or @p s fails FR-012.
///
/// Real-time safe: no allocation, no locks, no exceptions, no I/O.
[[nodiscard]] inline std::size_t serializeSpectralState(const SpectralState& s, std::byte* dest,
                                                        std::size_t capacity) noexcept {
    if (dest == nullptr || capacity < kSpectralStateBytes || !isValidSpectralState(s)) {
        return 0;
    }

    namespace io = detail::state_io;

    const auto version = static_cast<unsigned char>(kSpectralStateFormatVersion);
    std::memcpy(dest + io::kVersionOffset, &version, sizeof(version));

    const auto count = static_cast<std::int32_t>(s.numPartials);
    std::memcpy(dest + io::kNumPartialsOffset, &count, sizeof(count));

    std::memcpy(dest + io::kTiltOffset, &s.tiltDbPerOct, sizeof(float));
    std::memcpy(dest + io::kInharmonicityOffset, &s.inharmonicity, sizeof(float));
    std::memcpy(dest + io::kRatiosOffset, s.ratios.data(), io::kPartialArrayBytes);
    std::memcpy(dest + io::kAmplitudesOffset, s.amplitudes.data(), io::kPartialArrayBytes);
    std::memcpy(dest + io::kNameOffset, s.name.data(), SpectralState::kStateNameBytes);

    return kSpectralStateBytes;
}

/// @brief Decode a stream written by serializeSpectralState (FR-031, FR-033).
///
/// Decodes into a LOCAL SpectralState and copies into @p out only on success,
/// so a rejected stream leaves @p out bitwise untouched.
///
/// @return false -- @p out unmodified -- if @p src is null, @p size is short,
///         the version byte is not kSpectralStateFormatVersion, or the decoded
///         payload fails isValidSpectralState (FR-012).
///
/// The round trip is EXACT because the bytes are stored values, never the
/// result of arithmetic (FR-033).
///
/// Real-time safe: no allocation, no locks, no exceptions, no I/O.
[[nodiscard]] inline bool deserializeSpectralState(const std::byte* src, std::size_t size,
                                                   SpectralState& out) noexcept {
    if (src == nullptr || size < kSpectralStateBytes) {
        return false;
    }

    namespace io = detail::state_io;

    unsigned char version = 0;
    std::memcpy(&version, src + io::kVersionOffset, sizeof(version));
    if (version != static_cast<unsigned char>(kSpectralStateFormatVersion)) {
        return false;
    }

    SpectralState decoded{};

    std::int32_t count = 0;
    std::memcpy(&count, src + io::kNumPartialsOffset, sizeof(count));
    decoded.numPartials = static_cast<int>(count);

    std::memcpy(&decoded.tiltDbPerOct, src + io::kTiltOffset, sizeof(float));
    std::memcpy(&decoded.inharmonicity, src + io::kInharmonicityOffset, sizeof(float));
    std::memcpy(decoded.ratios.data(), src + io::kRatiosOffset, io::kPartialArrayBytes);
    std::memcpy(decoded.amplitudes.data(), src + io::kAmplitudesOffset, io::kPartialArrayBytes);
    std::memcpy(decoded.name.data(), src + io::kNameOffset, SpectralState::kStateNameBytes);

    if (!isValidSpectralState(decoded)) {
        return false;
    }

    out = decoded;
    return true;
}

// ==============================================================================
// Factory states (FR-021 - FR-023) -- plan section 3.4
// ==============================================================================

/// @brief The five factory morph endpoints (FR-021).
enum class SpectralStateId : std::uint8_t { SineStack = 0, Bell, Choir, Glass, Breath };

inline constexpr std::size_t kSpectralStateCount = 5;

namespace detail::factory {

// Every constant below is PINNED by plan section 3.4 against SC-008's a-priori
// distance threshold and FR-022's rho <= 0.92 constraint. They are not taste:
// changing one moves the ten pairwise distances, which spectral_state_test.cpp
// asserts within 2 %. Bell's B in particular is pinned by FR-012 arithmetic --
// at B = 0.06 the law breaches kMaxStateRatio two slots early.
inline constexpr float kBellB = 0.04f;

inline constexpr float kChoirFloor = 0.12f;
inline constexpr std::array<float, 3> kChoirCentres{3.0f, 8.0f, 17.0f};
inline constexpr std::array<float, 3> kChoirSigmas{1.2f, 2.0f, 3.0f};
inline constexpr std::array<float, 3> kChoirGains{1.0f, 0.8f, 0.5f};

inline constexpr float kGlassStretch = 0.004f;
inline constexpr float kGlassEvenAtten = 0.35f;

inline constexpr float kBreathLowDepth = 0.9f;
inline constexpr float kBreathLowScale = 3.0f;

// The FR-041 geometric continuation, IDENTICAL to SpectralMorphEngine's rule
// (plan section 5.4). Duplicated as literals here rather than included, because
// the engine is Layer 3 and this header is Layer 2 -- the engine's constants
// alias these values, not the other way round. kFillSpacingFactor is
// exp2(kFillSpacingCents / 1200) with kFillSpacingCents = 28.
inline constexpr float kFillMaxGrowth = 2.0f;
inline constexpr float kFillMaxRatio = SpectralState::kMaxStateRatio; // 128
inline constexpr float kFillSpacingFactor = 1.0163049f;

} // namespace detail::factory

namespace detail {

/// Copy a NUL-terminated ASCII @p label into @p s.name, truncating to
/// `kStateNameBytes - 1` so the already-zeroed field keeps its terminator.
///
/// WHY A HELPER AND NOT THE OBVIOUS `label[i] != '\0'` LOOP: GCC 13 unrolls
/// `for (i; i + 1 < kStateNameBytes && label[i] != '\0'; ++i)` against a
/// call site whose argument is a shorter string literal, and reports
/// `-Warray-bounds` for the unrolled iterations it cannot prove the NUL test
/// kills (subscripts 10-14 of a `const char[10]`). Measuring the length first
/// makes the bound provable from the literal itself, so the diagnostic goes
/// away without suppressing anything. Behaviour is identical.
inline void copyStateName(SpectralState& s, const char* label) noexcept {
    const std::size_t copyCount =
        std::min(std::strlen(label), SpectralState::kStateNameBytes - std::size_t{1});
    std::memcpy(s.name.data(), label, copyCount);
}

} // namespace detail

/// @brief Build one of the five factory states (FR-021, FR-022).
///
/// Deterministic and stateless, and it CONSUMES NO RNG (FR-023): every value is
/// a closed-form function of the partial index, so two calls with the same id
/// are bitwise identical no matter what else has drawn from any generator.
///
/// Build order, per plan section 3.4:
///   1. `ratios[i]` for `i < numPartials` from the id's ratio law;
///   2. `ratios[i]` for `i >= numPartials` from the FR-041 geometric
///      continuation -- filled, not left at 0, because SC-008's ratio term
///      would otherwise evaluate `log2(0 / n)` for every Bell pair;
///   3. `amplitudes[i]` from the id's amplitude law, 0 above `numPartials`;
///   4. `normalizeSpectralState` (FR-014);
///   5. the NUL-padded ASCII label.
///
/// Laws, with 1-based `n = i + 1`:
///   | Id        | numPartials | ratio_n                     | amp_n (pre-norm)                    |
///   | SineStack | 64          | n                           | n^-1                                |
///   | Bell      | 24          | n*sqrt(1 + kBellB*n^2)      | n^-1.4                              |
///   | Choir     | 64          | n                           | n^-0.8 * (floor + 3 Gaussian bumps) |
///   | Glass     | 64          | n*(1 + kGlassStretch*n)     | n^-0.5 * (even ? atten : 1)         |
///   | Breath    | 64          | n                           | n^-0.25 * (1 - depth*exp(-(n-1)/s)) |
///
/// CONFIGURATION-TIME, not audio-thread: allocation-free, lock-free and
/// exception-free, but it evaluates ~200 `std::pow`/`std::exp` calls.
///
/// OUT-OF-LINE ON PURPOSE (spectral_state.cpp, 2026-08-05): an inline
/// definition compiles once per consuming TU under that TU's FP flags, and on
/// Apple Clang (Xcode 26.6) a -ffast-math TU and a -fno-fast-math TU lower
/// the pow/exp calls differently — the "same" factory state came out with
/// different last-ULP bits in the processor, the controller and the tests,
/// breaking the byte-identity the state format depends on. One compiled
/// definition (in a -fno-fast-math TU, like spectral_simd.cpp) restores the
/// FR-023 bitwise-identical guarantee process-wide. Do not move it back into
/// the header.
[[nodiscard]] SpectralState makeFactoryState(SpectralStateId id) noexcept;

// ==============================================================================
// Authoring mutators (Seraphis Phase 11 FR-031, FR-032, C-6) -- plan section 1
// ==============================================================================
// PLACEMENT IS LOAD-BEARING. These three sit AFTER makeFactoryState and not
// beside normalizeSpectralState (:155): kAuthorSpacing below is initialised from
// detail::factory::kFillSpacingFactor (:344) and blendStates' tail fill names
// detail::factory::kFillMaxGrowth (:342) and kFillMaxRatio (:343), all inside a
// namespace that does not open until :317. Name lookup in a namespace-scope
// inline constexpr initialiser and in a non-template inline function body is
// resolved at the point of definition, so an earlier placement is a hard compile
// error on every leg.
//
// The four range constants are SpectralState-SCOPED members (:51-54), never
// namespace-scope names, so every use below is written SpectralState::kMin... .
//
// All three are noexcept, allocation-free, lock-free, exception-free and I/O
// free, and they add no include -- <algorithm>, <cmath> and db_utils.h are
// already carried at :26-35. Finiteness is tested with detail::isNaN /
// detail::isInf (used at :91, :103), NEVER std::isnan: see the -ffast-math
// banner at :21-23.
// ==============================================================================

/// @brief The strictly-monotone authoring guard band: 28 cents.
///
/// Reuses detail::factory::kFillSpacingFactor (:344) so no new number enters the
/// file -- an authored partial keeps the same geometric spacing from its
/// neighbours that the FR-041 continuation already uses.
inline constexpr float kAuthorSpacing = detail::factory::kFillSpacingFactor; // 1.0163049f

/// @brief Author one partial's ratio and amplitude in place (FR-031, C-6).
///
/// PRESERVATION, not repair: a state that already satisfies isValidSpectralState
/// still does afterwards; a state that does NOT is left BYTE-UNCHANGED. Every
/// rejection -- whole-state and local -- happens before the first store, so the
/// no-half-write property is structural rather than tested-in.
///
/// No-ops, in evaluation order: @p s is not FR-012-valid; numPartials outside
/// [0, 64]; @p index at or beyond numPartials; a non-finite @p ratio or
/// @p amplitude; or a monotone window that has collapsed because the two
/// neighbours are closer together than kAuthorSpacing^2.
///
/// The stored ratio is CLAMPED into (ratios[index-1] * kAuthorSpacing,
/// ratios[index+1] / kAuthorSpacing) -- neighbours are never swapped -- and the
/// stored amplitude is clamped into [0, 1]. Nothing but ratios[index] and
/// amplitudes[index] is written: `name`, `tiltDbPerOct`, `inharmonicity` and
/// `numPartials` are untouched.
///
/// Real-time safe: no allocation, no locks, no exceptions.
inline void setPartial(SpectralState& s, std::size_t index, float ratio,
                       float amplitude) noexcept {
    // (0) The whole-state gate. Load-bearing: steps 1-6 are all LOCAL checks, so
    // a state that is invalid somewhere OTHER than `index` (an out-of-range
    // amplitude at slot 5, a non-monotone ratio pair at slot 30, a `name` with
    // no NUL) passes every one of them and would otherwise be stored into.
    if (!isValidSpectralState(s)) {
        return;
    }

    // (1) The precondition step 2's cast relies on. Subsumed by step 0 in
    // practice; kept because it costs one predicted branch.
    if (s.numPartials < 0 || s.numPartials > static_cast<int>(SpectralState::kStatePartials)) {
        return;
    }
    const auto count = static_cast<std::size_t>(s.numPartials);

    // (2)
    if (index >= count) {
        return;
    }

    // (3)
    if (detail::isNaN(ratio) || detail::isInf(ratio) || detail::isNaN(amplitude)
        || detail::isInf(amplitude)) {
        return;
    }

    // (4) / (5)
    const float amp = std::clamp(amplitude, 0.0f, 1.0f);
    float r = std::clamp(ratio, SpectralState::kMinStateRatio, SpectralState::kMaxStateRatio);

    // (6) The monotone window.
    const float lo =
        (index > 0) ? s.ratios[index - 1] * kAuthorSpacing : SpectralState::kMinStateRatio;
    const float hi =
        (index + 1 < count) ? s.ratios[index + 1] / kAuthorSpacing : SpectralState::kMaxStateRatio;
    if (lo > hi) {
        return; // Neighbours closer than kAuthorSpacing^2 -- NO-OP, no write.
    }
    r = std::clamp(r, lo, hi);

    // (7) The only two stores in this function.
    s.ratios[index] = r;
    s.amplitudes[index] = amp;
}

/// @brief Convex blend of two morph endpoints (FR-031, FR-032 clause 3).
///
/// Returns an UNCONDITIONALLY valid state: if both inputs are invalid the result
/// is a default-constructed SpectralState (documented valid, :42-43); if exactly
/// one is invalid the other is returned verbatim; a non-finite @p t returns
/// @p a; otherwise @p t is clamped into [0, 1].
///
/// The exact endpoints are BYTE-IDENTICAL to the inputs: `blendStates(a, b, 0)`
/// is `a` and `blendStates(a, b, 1)` is `b`, bit for bit. The interior body
/// cannot reproduce that, for three independent reasons -- it rewrites `name` to
/// "Blend", it regenerates every slot at or above min(numPartials) from the
/// FR-041 continuation, and exp2(log2(x)) is a binary32 round trip that is not
/// the identity -- so the short-circuit below is what makes the rule literally
/// true rather than approximately true.
///
/// Ratios interpolate in log2 and come back through exp2, which is the domain
/// SpectralMorphEngine itself stores, so a blend and a morph agree about what
/// "halfway" means. NO CLAMP is applied to them: a convex combination of two
/// strictly increasing log-ratio sequences is strictly increasing and stays
/// inside [log2 0.5, log2 128], and a clamp is the one operation that could
/// flatten two neighbours into equality and break monotonicity.
///
/// Returns by value; SpectralState is trivially copyable (:65), so this is a
/// sizeof(SpectralState) stack copy and not an allocation. (That is 540 bytes --
/// NOT kSpectralStateBytes (:186), which is the 541-byte SERIALIZED size and a
/// different quantity.)
///
/// Real-time safe: no allocation, no locks, no exceptions.
[[nodiscard]] inline SpectralState blendStates(const SpectralState& a, const SpectralState& b,
                                               float t) noexcept {
    // (1) Select-or-default on validity.
    const bool validA = isValidSpectralState(a);
    const bool validB = isValidSpectralState(b);
    if (!validA && !validB) {
        return SpectralState{};
    }
    if (!validA) {
        return b;
    }
    if (!validB) {
        return a;
    }

    // (2)
    if (detail::isNaN(t) || detail::isInf(t)) {
        return a;
    }
    const float u = std::clamp(t, 0.0f, 1.0f);

    // (2a) The exact-endpoint short-circuit.
    if (u == 0.0f) {
        return a;
    }
    if (u == 1.0f) {
        return b;
    }

    // (3)
    SpectralState out{};
    out.numPartials = std::min(a.numPartials, b.numPartials);
    const auto count = static_cast<std::size_t>(out.numPartials); // >= 0 by validity

    // (4)
    for (std::size_t i = 0; i < count; ++i) {
        out.ratios[i] =
            std::exp2((1.0f - u) * std::log2(a.ratios[i]) + u * std::log2(b.ratios[i]));
        out.amplitudes[i] = (1.0f - u) * a.amplitudes[i] + u * b.amplitudes[i];
    }

    // (5) Metadata interpolates linearly. The clamps are NOT a range repair -- a
    // convex combination of two in-range values is in range in real arithmetic.
    // They pin the binary32 rounding at the extremes, where (1-u)*x + u*x with
    // both endpoints sitting exactly on a bound can land an ulp outside it and
    // cost this function its unconditional-validity guarantee.
    out.tiltDbPerOct = std::clamp((1.0f - u) * a.tiltDbPerOct + u * b.tiltDbPerOct,
                                  SpectralState::kMinStateTiltDbPerOct,
                                  SpectralState::kMaxStateTiltDbPerOct);
    out.inharmonicity = std::clamp((1.0f - u) * a.inharmonicity + u * b.inharmonicity, 0.0f,
                                   SpectralState::kMaxStateInharmonicity);

    // (6) The FR-041 geometric continuation over the unauthored slots, identical
    // to makeFactoryState's (:416-433). The validator does not examine these
    // (:78-79), but leaving them at 0 would put log2(0) into any consumer that
    // scans the whole array.
    for (std::size_t j = count; j < SpectralState::kStatePartials; ++j) {
        float grown = 0.0f;
        if (out.numPartials >= 2) {
            const float lastSpacing = out.ratios[j - 1] / out.ratios[j - 2];
            const float g = std::clamp(lastSpacing, 1.0f, detail::factory::kFillMaxGrowth);
            grown = std::min(out.ratios[j - 1] * g, detail::factory::kFillMaxRatio);
        } else {
            grown = static_cast<float>(j + 1);
        }
        const float floorValue = (j >= 1) ? out.ratios[j - 1] * detail::factory::kFillSpacingFactor
                                          : static_cast<float>(j + 1);
        out.ratios[j] = std::max(grown, floorValue);
    }

    // (7) NUL-padded label; `out.name` is already all-zero, so the copy stops one
    // byte short of the field and the terminator is guaranteed.
    detail::copyStateName(out, "Blend");

    // No normalizeSpectralState call: both inputs are already normalised and a
    // convex combination of two unit-norm vectors has norm <= 1, so amplitudes
    // stay in range; normalising would additionally rescale the interior, which
    // the reversibility argument does not want.
    return out;
}

/// @brief Set the state's spectral tilt to an ABSOLUTE dB/octave (FR-031, C-6).
///
/// The tilt is BAKED into `amplitudes` -- writing the field alone is inaudible,
/// because the morph engine reads amplitudes, not the label. Absolute, not
/// relative: `delta = target - s.tiltDbPerOct` undoes whatever tilt the state
/// already carries before applying the new one, so two consecutive calls with
/// the same argument are the same as one.
///
/// No-ops on a non-finite @p dbPerOct and on a state that is not FR-012-valid,
/// both leaving @p s BYTE-UNCHANGED. @p dbPerOct is clamped into
/// [kMinStateTiltDbPerOct, kMaxStateTiltDbPerOct].
///
/// std::pow(10.0f, x), NEVER exp10f -- that is a glibc GNU extension, absent on
/// MSVC (the standing prohibition is recorded at
/// dsp/include/krate/dsp/systems/continuous_body.h:1643-1645). This site is
/// configuration-time, where makeFactoryState already evaluates ~200 std::pow
/// calls, so the portable form is free.
///
/// Real-time safe: no allocation, no locks, no exceptions.
inline void tiltState(SpectralState& s, float dbPerOct) noexcept {
    // (1) / (2)
    if (detail::isNaN(dbPerOct) || detail::isInf(dbPerOct)) {
        return;
    }
    if (!isValidSpectralState(s)) {
        return;
    }

    // (3)
    const float target = std::clamp(dbPerOct, SpectralState::kMinStateTiltDbPerOct,
                                    SpectralState::kMaxStateTiltDbPerOct);
    const float delta = target - s.tiltDbPerOct;

    // (3a) Already at the target: the bake below is the identity on amplitudes
    // (10^0 == 1), but normalizeSpectralState is NOT bit-idempotent -- its
    // sum-of-squares over an already-unit-norm vector lands within an ulp of 1
    // rather than exactly on it, and the resulting 1/sqrt rescales every
    // amplitude by one ulp. Short-circuiting is what makes absoluteness a
    // BYTE-identity instead of an approximate one, exactly as blendStates' step
    // 2a does for its endpoints.
    if (delta == 0.0f) {
        s.tiltDbPerOct = target;
        return;
    }

    // (4) An empty state has no amplitudes to bake into; the field is still
    // assigned so the control's readout is truthful.
    if (s.numPartials <= 0) {
        s.tiltDbPerOct = target;
        return;
    }

    // (5) ratios[0] >= kMinStateRatio by validity, so the division is safe.
    const auto count = static_cast<std::size_t>(s.numPartials);
    for (std::size_t i = 0; i < count; ++i) {
        const float octaves = std::log2(s.ratios[i] / s.ratios[0]);
        s.amplitudes[i] *= std::pow(10.0f, (delta / 20.0f) * octaves);
    }

    // (6) / (7)
    normalizeSpectralState(s);
    s.tiltDbPerOct = target;
}

} // namespace Krate::DSP
