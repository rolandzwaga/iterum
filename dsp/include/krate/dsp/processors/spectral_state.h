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
[[nodiscard]] inline SpectralState makeFactoryState(SpectralStateId id) noexcept {
    SpectralState s{};

    int count = 64;
    const char* label = "SineStack";
    switch (id) {
    case SpectralStateId::SineStack:
        break;
    case SpectralStateId::Bell:
        count = 24;
        label = "Bell";
        break;
    case SpectralStateId::Choir:
        label = "Choir";
        break;
    case SpectralStateId::Glass:
        label = "Glass";
        break;
    case SpectralStateId::Breath:
        label = "Breath";
        break;
    }
    s.numPartials = count;

    // (1) Authored ratios.
    for (int i = 0; i < count; ++i) {
        const float n = static_cast<float>(i + 1);
        float ratio = n;
        switch (id) {
        case SpectralStateId::Bell:
            ratio = n * std::sqrt(1.0f + detail::factory::kBellB * n * n);
            break;
        case SpectralStateId::Glass:
            ratio = n * (1.0f + detail::factory::kGlassStretch * n);
            break;
        case SpectralStateId::SineStack:
        case SpectralStateId::Choir:
        case SpectralStateId::Breath:
            break;
        }
        s.ratios[static_cast<std::size_t>(i)] = ratio;
    }

    // (2) FR-041 geometric continuation over the unauthored slots.
    // The `count < 2` arm is deviation D9's `j + 1` rule and applies for EVERY
    // j, not only j < 2; no factory state reaches it (the sparsest is Bell at
    // 24), but the recurrence is kept identical to the engine's on purpose.
    for (std::size_t j = static_cast<std::size_t>(count); j < SpectralState::kStatePartials; ++j) {
        float grown = 0.0f;
        if (count >= 2) {
            // j >= 2 always holds here: the loop starts at j = count >= 2.
            const float lastSpacing = s.ratios[j - 1] / s.ratios[j - 2];
            const float g = std::clamp(lastSpacing, 1.0f, detail::factory::kFillMaxGrowth);
            grown = std::min(s.ratios[j - 1] * g, detail::factory::kFillMaxRatio);
        } else {
            grown = static_cast<float>(j + 1);
        }
        const float floorValue = (j >= 1) ? s.ratios[j - 1] * detail::factory::kFillSpacingFactor
                                          : static_cast<float>(j + 1);
        s.ratios[j] = std::max(grown, floorValue);
    }

    // (3) Authored amplitudes; slots at or above `count` stay at the
    // value-initialised 0.0f.
    for (int i = 0; i < count; ++i) {
        const float n = static_cast<float>(i + 1);
        float amplitude = 1.0f;
        switch (id) {
        case SpectralStateId::SineStack:
            amplitude = 1.0f / n;
            break;
        case SpectralStateId::Bell:
            amplitude = std::pow(n, -1.4f);
            break;
        case SpectralStateId::Choir: {
            float shape = detail::factory::kChoirFloor;
            for (std::size_t k = 0; k < detail::factory::kChoirCentres.size(); ++k) {
                const float offset = n - detail::factory::kChoirCentres[k];
                const float sigma = detail::factory::kChoirSigmas[k];
                shape += detail::factory::kChoirGains[k]
                         * std::exp(-(offset * offset) / (2.0f * sigma * sigma));
            }
            amplitude = std::pow(n, -0.8f) * shape;
            break;
        }
        case SpectralStateId::Glass:
            amplitude = std::pow(n, -0.5f)
                        * (((i + 1) % 2 == 0) ? detail::factory::kGlassEvenAtten : 1.0f);
            break;
        case SpectralStateId::Breath:
            amplitude =
                std::pow(n, -0.25f)
                * (1.0f
                   - detail::factory::kBreathLowDepth
                         * std::exp(-(n - 1.0f) / detail::factory::kBreathLowScale));
            break;
        }
        s.amplitudes[static_cast<std::size_t>(i)] = amplitude;
    }

    // (4) FR-014.
    normalizeSpectralState(s);

    // (5) NUL-padded ASCII label; `name` is already all-zero, so the copy stops
    // one byte short of the field and the terminator is guaranteed.
    for (std::size_t i = 0; i + 1 < SpectralState::kStateNameBytes && label[i] != '\0'; ++i) {
        s.name[i] = label[i];
    }

    return s;
}

} // namespace Krate::DSP
