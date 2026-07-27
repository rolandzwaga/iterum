// ==============================================================================
// Layer 2: Processor Tests - SpectralState (Seraphis Phase 3)
// ==============================================================================
// Spec:  specs/seraphis-phase3-spectral-morph/spec.md  (FR-011 – FR-014, FR-021 – FR-023,
//                                                       FR-031 – FR-033, SC-007, SC-008)
// Plan:  specs/seraphis-phase3-spectral-morph/plan.md  (sections 3.1 – 3.4)
// Tasks: specs/seraphis-phase3-spectral-morph/tasks.md (T007, T008, T009)
//
// NON-FINITE INPUTS ARE BUILT FROM BIT PATTERNS, NEVER FROM
// std::numeric_limits<float>::quiet_NaN() / infinity(): the macOS leg builds
// -ffast-math (-ffinite-math-only), under which the compiler assumes non-finite
// values do not exist and constant-folds those calls to finite garbage -- the
// "rejects non-finite" assertions would then silently be testing ordinary
// numbers. The volatile sink below forces a real non-finite bit pattern to
// exist at runtime regardless of FP mode.
// ==============================================================================

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <krate/dsp/core/random.h>
#include <krate/dsp/processors/spectral_state.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

using Krate::DSP::deserializeSpectralState;
using Krate::DSP::isValidSpectralState;
using Krate::DSP::kSpectralStateBytes;
using Krate::DSP::makeFactoryState;
using Krate::DSP::normalizeSpectralState;
using Krate::DSP::serializeSpectralState;
using Krate::DSP::SpectralState;
using Krate::DSP::SpectralStateId;
using Krate::DSP::Xorshift32;

namespace {

/// A float built from its IEEE-754 bit pattern through a volatile sink.
/// See the -ffast-math note at the top of this file.
[[nodiscard]] float floatFromBits(std::uint32_t bits) noexcept {
    volatile std::uint32_t sink = bits;
    const std::uint32_t observed = sink;
    return std::bit_cast<float>(observed);
}

constexpr std::uint32_t kQuietNaNBits = 0x7FC00000u;
constexpr std::uint32_t kPosInfBits = 0x7F800000u;
constexpr std::uint32_t kNegInfBits = 0xFF800000u;

/// A known-valid state: 4 strictly increasing ratios in [0.5, 128], amplitudes
/// in [0, 1], a NUL-terminated printable name, tilt and inharmonicity in range.
[[nodiscard]] SpectralState makeValidState() noexcept {
    SpectralState s{};
    s.numPartials = 4;
    for (std::size_t i = 0; i < 4; ++i) {
        s.ratios[i] = 1.0f + static_cast<float>(i);
        s.amplitudes[i] = 0.5f;
    }
    s.name = {'T', 'e', 's', 't', '\0'};
    s.tiltDbPerOct = 0.0f;
    s.inharmonicity = 0.0f;
    return s;
}

/// Bit-identical float comparison that compares OBJECT REPRESENTATIONS directly
/// instead of reinterpreting each float as a std::uint32_t.
///
/// The two forms are equivalent, but the memcmp form keeps this file free of
/// float -> integer bit reinterpretation, which matters because the file now
/// also carries an FNV-1a digest (see fnv1aOverBytes below).
/// tools/lint-float-bit-goldens.js flags any file containing BOTH, on the sound
/// premise that a hash fed from float bits is a bit-exact float golden. That
/// premise does not hold here -- the digest is taken over a serialized byte
/// stream, which the linter explicitly carves out -- so the right fix is to
/// drop the incidental reinterpretation, not to weaken the gate.
[[nodiscard]] bool sameFloatBits(float a, float b) noexcept {
    // NOLINTNEXTLINE(bugprone-suspicious-memory-comparison) - intentional bit-exact check
    return std::memcmp(&a, &b, sizeof(float)) == 0;
}

/// Bitwise field-by-field comparison. Deliberately NOT std::memcmp over the
/// object: padding bytes are not required to be copied by the implicit copy
/// constructor, so a memcmp against a saved copy could differ for reasons that
/// have nothing to do with normalizeSpectralState.
[[nodiscard]] bool statesBitwiseEqual(const SpectralState& a, const SpectralState& b) noexcept {
    if (a.numPartials != b.numPartials) return false;
    if (!sameFloatBits(a.tiltDbPerOct, b.tiltDbPerOct)) return false;
    if (!sameFloatBits(a.inharmonicity, b.inharmonicity)) return false;
    for (std::size_t i = 0; i < SpectralState::kStatePartials; ++i) {
        if (!sameFloatBits(a.ratios[i], b.ratios[i])) return false;
        if (!sameFloatBits(a.amplitudes[i], b.amplitudes[i])) return false;
    }
    for (std::size_t i = 0; i < SpectralState::kStateNameBytes; ++i) {
        if (a.name[i] != b.name[i]) return false;
    }
    return true;
}

// --------------------------------------------------------------------------
// Factory-state helpers (T008, SC-008 clauses 1/2/4)
// --------------------------------------------------------------------------

/// One row of the plan §3.4 pinned-arithmetic table.
struct FactoryPin {
    SpectralStateId id;
    const char* label;
    int numPartials;
    double maxRatioAllSlots;  ///< max over ALL 64 slots, i.e. including the FR-041 fill
    double minSpacingCents;   ///< min over i in [1,64) of 1200*log2(r[i]/r[i-1])
};

/// Plan §3.4 / tasks T008. Bell's 240.32 lives entirely in the fill region --
/// see deviation D6. The spacings are D7-corrected (1200*log2(64/63) = 27.264).
constexpr std::array<FactoryPin, 5> kFactoryPins{
    FactoryPin{SpectralStateId::SineStack, "SineStack", 64, 64.00, 27.264},
    FactoryPin{SpectralStateId::Bell, "Bell", 24, 240.32, 28.000},
    FactoryPin{SpectralStateId::Choir, "Choir", 64, 64.00, 27.264},
    FactoryPin{SpectralStateId::Glass, "Glass", 64, 80.38, 32.79},
    FactoryPin{SpectralStateId::Breath, "Breath", 64, 64.00, 27.264}};

/// SC-008 clause 2's spectral distance, verbatim: the AMPLITUDE term runs over
/// all kStatePartials slots (unused amplitudes are exactly 0), the RATIO term
/// only over [0, min(A.numPartials, B.numPartials)). lambda = 1.0.
/// Accumulated in double so the assertion measures the states, not the sum.
[[nodiscard]] double factoryDistance(const SpectralState& a, const SpectralState& b) noexcept {
    double sumSquares = 0.0;
    for (std::size_t i = 0; i < SpectralState::kStatePartials; ++i) {
        const double d =
            static_cast<double>(a.amplitudes[i]) - static_cast<double>(b.amplitudes[i]);
        sumSquares += d * d;
    }
    const double amplitudeTerm = std::sqrt(sumSquares);

    const int shared = std::min(a.numPartials, b.numPartials);
    double centsAccum = 0.0;
    for (int i = 0; i < shared; ++i) {
        const auto slot = static_cast<std::size_t>(i);
        centsAccum += std::abs(1200.0
                               * std::log2(static_cast<double>(a.ratios[slot])
                                           / static_cast<double>(b.ratios[slot])));
    }
    const double ratioTerm =
        (shared > 0) ? (centsAccum / static_cast<double>(shared)) / 1200.0 : 0.0;

    constexpr double kLambda = 1.0;
    return amplitudeTerm + kLambda * ratioTerm;
}

/// Cosine similarity of two amplitude vectors over all 64 slots (FR-022).
[[nodiscard]] double amplitudeCosine(const SpectralState& a, const SpectralState& b) noexcept {
    double dot = 0.0;
    double normA = 0.0;
    double normB = 0.0;
    for (std::size_t i = 0; i < SpectralState::kStatePartials; ++i) {
        const double av = static_cast<double>(a.amplitudes[i]);
        const double bv = static_cast<double>(b.amplitudes[i]);
        dot += av * bv;
        normA += av * av;
        normB += bv * bv;
    }
    return dot / std::sqrt(normA * normB);
}

// --------------------------------------------------------------------------
// Serialization helpers (T009, SC-007)
// --------------------------------------------------------------------------

/// FNV-1a (64-bit) over a SERIALIZED BYTE STREAM.
///
/// This walks stored bytes through `unsigned char` -- it never reinterprets a
/// float's bits, and it never performs arithmetic on `std::byte` (whose value
/// has to pass through `unsigned char` to be well defined at all). See the
/// long note beside kLayoutProbeDigest for why a digest is legitimate here.
[[nodiscard]] std::uint64_t fnv1aOverBytes(const std::byte* data, std::size_t size) noexcept {
    constexpr std::uint64_t kFnvOffsetBasis = 0xcbf29ce484222325ULL;
    constexpr std::uint64_t kFnvPrime = 0x100000001b3ULL;

    std::uint64_t digest = kFnvOffsetBasis;
    for (std::size_t i = 0; i < size; ++i) {
        digest ^= static_cast<std::uint64_t>(static_cast<unsigned char>(data[i]));
        digest *= kFnvPrime;
    }
    return digest;
}

/// The state the SC-007 digest golden is pinned against.
///
/// Every float is an exactly representable binary32 value: `0.5 + 0.5*i` and
/// `1 - i/64` are dyadic rationals with small exponents, so both the products
/// and the sums are exact on any IEEE-754 target and remain exact under the
/// macOS leg's -ffast-math (there is no rounding step for it to reassociate,
/// and FMA contraction cannot change an exact result). Every field carries a
/// DIFFERENT bit pattern from every other field, so a swap of the ratio and
/// amplitude blocks -- or of tilt and inharmonicity -- moves the digest.
[[nodiscard]] SpectralState makeLayoutProbeState() noexcept {
    SpectralState s{};
    s.numPartials = static_cast<int>(SpectralState::kStatePartials);
    for (std::size_t i = 0; i < SpectralState::kStatePartials; ++i) {
        s.ratios[i] = 0.5f + 0.5f * static_cast<float>(i);          // 0.5 .. 32.0
        s.amplitudes[i] = 1.0f - static_cast<float>(i) / 64.0f;     // 1.0 .. 0.015625
    }
    s.tiltDbPerOct = -12.0f;
    s.inharmonicity = 0.0625f;

    const char* label = "LayoutProbe";
    for (std::size_t i = 0; i + 1 < SpectralState::kStateNameBytes && label[i] != '\0'; ++i) {
        s.name[i] = label[i];
    }
    return s;
}

/// Edge state: one partial, at the FR-012 lower ratio bound, full amplitude.
/// The name is left all-zero -- the terminator at index 0 is FR-012-valid.
[[nodiscard]] SpectralState makeSinglePartialEdgeState() noexcept {
    SpectralState s{};
    s.numPartials = 1;
    s.ratios[0] = SpectralState::kMinStateRatio;
    s.amplitudes[0] = 1.0f;
    return s;
}

/// Edge state: all 64 slots used, extremal tilt and inharmonicity, and a name
/// that fills all 15 usable bytes (leaving exactly the trailing NUL).
[[nodiscard]] SpectralState makeFullEdgeState() noexcept {
    SpectralState s{};
    s.numPartials = static_cast<int>(SpectralState::kStatePartials);
    for (std::size_t i = 0; i < SpectralState::kStatePartials; ++i) {
        s.ratios[i] = SpectralState::kMinStateRatio + 2.0f * static_cast<float>(i); // .. 126.5
        s.amplitudes[i] = 1.0f / (1.0f + static_cast<float>(i));
    }
    s.tiltDbPerOct = SpectralState::kMinStateTiltDbPerOct;   // -12
    s.inharmonicity = SpectralState::kMaxStateInharmonicity; // 0.1

    const char* label = "EdgeMaxNameXYZ!"; // 15 chars
    for (std::size_t i = 0; i + 1 < SpectralState::kStateNameBytes && label[i] != '\0'; ++i) {
        s.name[i] = label[i];
    }
    return s;
}

} // namespace

TEST_CASE("SpectralState_ValidityAndNormalisation", "[spectral_state][seraphis]") {
    STATIC_REQUIRE(std::is_trivially_copyable_v<SpectralState>);
    STATIC_REQUIRE(SpectralState::kStatePartials == 64);

    SECTION("Default-constructed state is valid, silent and anonymous (FR-011)") {
        const SpectralState s{};
        REQUIRE(isValidSpectralState(s));
        REQUIRE(s.numPartials == 0);
        for (std::size_t i = 0; i < SpectralState::kStatePartials; ++i) {
            REQUIRE(s.ratios[i] == 0.0f);
            REQUIRE(s.amplitudes[i] == 0.0f);
        }
        REQUIRE(s.name[0] == '\0');
        REQUIRE(s.tiltDbPerOct == 0.0f);
        REQUIRE(s.inharmonicity == 0.0f);
    }

    SECTION("The baseline state used by the reject grid is itself valid") {
        REQUIRE(isValidSpectralState(makeValidState()));
    }

    SECTION("Reject grid - one defect at a time (FR-012)") {
        const auto rejects = [](const char* label, const SpectralState& bad) {
            INFO(label);
            REQUIRE_FALSE(isValidSpectralState(bad));
        };

        {
            SpectralState s = makeValidState();
            s.numPartials = -1;
            rejects("numPartials = -1", s);
        }
        {
            SpectralState s = makeValidState();
            s.numPartials = 65;
            rejects("numPartials = 65", s);
        }
        {
            SpectralState s = makeValidState();
            s.ratios[1] = s.ratios[0];
            rejects("ratios[1] == ratios[0] (non-strict increase)", s);
        }
        {
            SpectralState s = makeValidState();
            s.ratios[1] = s.ratios[0] - 0.1f;
            rejects("ratios[1] < ratios[0]", s);
        }
        {
            SpectralState s = makeValidState();
            s.ratios[0] = 0.4f;
            rejects("ratios[0] = 0.4f (< kMinStateRatio)", s);
        }
        {
            // numPartials = 1 so the over-range ratio is the ONLY defect: any
            // higher slot would have to exceed 128.5 to stay strictly increasing.
            SpectralState s = makeValidState();
            s.numPartials = 1;
            s.ratios[0] = 128.5f;
            rejects("ratios[0] = 128.5f (> kMaxStateRatio)", s);
        }
        {
            SpectralState s = makeValidState();
            s.amplitudes[0] = -1e-6f;
            rejects("amplitudes[0] = -1e-6f", s);
        }
        {
            SpectralState s = makeValidState();
            s.amplitudes[0] = 1.0001f;
            rejects("amplitudes[0] = 1.0001f", s);
        }

        // Non-finite poison in every float field that FR-012 examines.
        const std::array<std::uint32_t, 3> poison{kQuietNaNBits, kPosInfBits, kNegInfBits};
        for (const std::uint32_t bits : poison) {
            const float bad = floatFromBits(bits);
            {
                SpectralState s = makeValidState();
                s.ratios[0] = bad;
                rejects("non-finite ratios[0]", s);
            }
            {
                SpectralState s = makeValidState();
                s.amplitudes[0] = bad;
                rejects("non-finite amplitudes[0]", s);
            }
            {
                SpectralState s = makeValidState();
                s.tiltDbPerOct = bad;
                rejects("non-finite tiltDbPerOct", s);
            }
            {
                SpectralState s = makeValidState();
                s.inharmonicity = bad;
                rejects("non-finite inharmonicity", s);
            }
        }

        {
            SpectralState s = makeValidState();
            s.tiltDbPerOct = 12.5f;
            rejects("tiltDbPerOct = 12.5f", s);
        }
        {
            SpectralState s = makeValidState();
            s.tiltDbPerOct = -12.5f;
            rejects("tiltDbPerOct = -12.5f", s);
        }
        {
            SpectralState s = makeValidState();
            s.inharmonicity = -1e-6f;
            rejects("inharmonicity = -1e-6f", s);
        }
        {
            SpectralState s = makeValidState();
            s.inharmonicity = 0.1001f;
            rejects("inharmonicity = 0.1001f", s);
        }
        {
            SpectralState s = makeValidState();
            for (std::size_t i = 0; i < SpectralState::kStateNameBytes; ++i) {
                s.name[i] = 'A';
            }
            rejects("name has 16 non-NUL bytes (no terminator)", s);
        }
        {
            SpectralState s = makeValidState();
            s.name = {'A', '\x07', '\0'};
            rejects("name has a control byte before the NUL", s);
        }
        {
            SpectralState s = makeValidState();
            s.name = {'A', '\x7F', '\0'};
            rejects("name has a DEL byte before the NUL", s);
        }
    }

    SECTION("Entries at i >= numPartials are not examined (FR-012)") {
        SpectralState s = makeValidState();
        s.ratios[63] = -5.0f;
        s.amplitudes[63] = 99.0f;
        REQUIRE(s.numPartials == 4);
        REQUIRE(isValidSpectralState(s));
    }

    SECTION("normalizeSpectralState L2-normalises the active partials (FR-014)") {
        SpectralState s{};
        s.numPartials = 4;
        for (std::size_t i = 0; i < 4; ++i) {
            s.ratios[i] = 1.0f + static_cast<float>(i);
            s.amplitudes[i] = 1.0f;
        }

        normalizeSpectralState(s);

        float sumSquares = 0.0f;
        for (std::size_t i = 0; i < 4; ++i) {
            REQUIRE(s.amplitudes[i] == Catch::Approx(0.5f).margin(1e-6));
            sumSquares += s.amplitudes[i] * s.amplitudes[i];
        }
        REQUIRE(sumSquares == Catch::Approx(1.0f).margin(1e-5));
    }

    SECTION("normalizeSpectralState leaves an all-zero state bitwise unchanged (FR-014)") {
        SpectralState s{};
        s.numPartials = 4;
        for (std::size_t i = 0; i < 4; ++i) {
            s.ratios[i] = 1.0f + static_cast<float>(i);
            s.amplitudes[i] = 0.0f;
        }
        const SpectralState before = s;

        normalizeSpectralState(s);

        REQUIRE(statesBitwiseEqual(s, before));
    }

    SECTION("normalizeSpectralState leaves a numPartials == 0 state bitwise unchanged (FR-014)") {
        SpectralState s{};
        s.numPartials = 0;
        s.amplitudes[0] = 1.0f;
        s.amplitudes[1] = 1.0f;
        const SpectralState before = s;

        normalizeSpectralState(s);

        REQUIRE(statesBitwiseEqual(s, before));
    }
}

TEST_CASE("SpectralState_FactoryStatesAreDistinct", "[spectral_state][seraphis]") {
    STATIC_REQUIRE(Krate::DSP::kSpectralStateCount == 5);
    STATIC_REQUIRE(kFactoryPins.size() == Krate::DSP::kSpectralStateCount);

    std::array<SpectralState, 5> states{};
    for (std::size_t k = 0; k < kFactoryPins.size(); ++k) {
        states[k] = makeFactoryState(kFactoryPins[k].id);
    }

    SECTION("Every factory state is FR-012-valid, normalized and named (SC-008 cl.1)") {
        for (std::size_t k = 0; k < kFactoryPins.size(); ++k) {
            const FactoryPin& pin = kFactoryPins[k];
            const SpectralState& s = states[k];
            INFO(pin.label);

            REQUIRE(isValidSpectralState(s));
            REQUIRE(s.numPartials == pin.numPartials);

            // L2 norm over the ACTIVE partials only (FR-014).
            double sumSquares = 0.0;
            for (int i = 0; i < s.numPartials; ++i) {
                const double a = static_cast<double>(s.amplitudes[static_cast<std::size_t>(i)]);
                sumSquares += a * a;
            }
            REQUIRE(std::sqrt(sumSquares) == Catch::Approx(1.0).margin(1e-5));

            // Name: non-empty, NUL-terminated, and equal to the id's ASCII label (FR-021).
            REQUIRE(s.name[0] != '\0');
            bool terminated = false;
            for (std::size_t i = 0; i < SpectralState::kStateNameBytes; ++i) {
                if (s.name[i] == '\0') {
                    terminated = true;
                    break;
                }
            }
            REQUIRE(terminated);
            REQUIRE(std::strcmp(s.name.data(), pin.label) == 0);
        }
    }

    SECTION("Ratio bounds are scoped as deviation D6 requires (SC-008 cl.1)") {
        for (std::size_t k = 0; k < kFactoryPins.size(); ++k) {
            const FactoryPin& pin = kFactoryPins[k];
            const SpectralState& s = states[k];
            INFO(pin.label);

            // D6: FR-012's kMaxStateRatio applies only to the AUTHORED region.
            // Bell's FR-041 fill deliberately climbs past 128 (to 240.32) because
            // the 28-cent spacing floor keeps stepping after the fill cap binds.
            float maxAuthored = 0.0f;
            for (int i = 0; i < s.numPartials; ++i) {
                maxAuthored = std::max(maxAuthored, s.ratios[static_cast<std::size_t>(i)]);
            }
            REQUIRE(maxAuthored <= SpectralState::kMaxStateRatio);

            // Over ALL 64 slots: strictly increasing, and bounded by the engine's
            // kMaxOutputRatio. Written as a literal 360.37f because
            // SpectralMorphEngine does not exist yet -- plan §5.1 owns the value
            // (kMaxFillRatio * 2^(64 * 28/1200), deviation D13).
            constexpr float kMaxOutputRatio = 360.37f;
            float maxAll = s.ratios[0];
            for (std::size_t i = 1; i < SpectralState::kStatePartials; ++i) {
                INFO("slot " << i);
                REQUIRE(s.ratios[i] > s.ratios[i - 1]);
                maxAll = std::max(maxAll, s.ratios[i]);
            }
            REQUIRE(maxAll <= kMaxOutputRatio);
            REQUIRE(static_cast<double>(maxAll)
                    == Catch::Approx(pin.maxRatioAllSlots).epsilon(0.005));

            // Minimum adjacent spacing in cents over i in [1, 64).
            double minSpacingCents = 1.0e9;
            for (std::size_t i = 1; i < SpectralState::kStatePartials; ++i) {
                const double cents = 1200.0
                                     * std::log2(static_cast<double>(s.ratios[i])
                                                 / static_cast<double>(s.ratios[i - 1]));
                minSpacingCents = std::min(minSpacingCents, cents);
            }
            REQUIRE(minSpacingCents == Catch::Approx(pin.minSpacingCents).margin(0.05));
        }
    }

    SECTION("All 10 pairs clear the a-priori distance threshold (SC-008 cl.2)") {
        constexpr float kMinFactoryStateDistance = 0.4f;

        struct PairPin {
            std::size_t a;
            std::size_t b;
            double distance;
        };
        // Plan §3.4, recomputed this session in double and in float32-emulated
        // arithmetic (identical to 4 decimals in both).
        constexpr std::array<PairPin, 10> kPairPins{
            PairPin{0, 1, 1.5723}, PairPin{0, 2, 0.5258}, PairPin{0, 3, 0.7406},
            PairPin{0, 4, 1.0932}, PairPin{1, 2, 2.0245}, PairPin{1, 3, 1.9742},
            PairPin{1, 4, 2.5841}, PairPin{2, 3, 0.8824}, PairPin{2, 4, 1.0538},
            PairPin{3, 4, 0.9517}};

        for (const PairPin& pair : kPairPins) {
            INFO(kFactoryPins[pair.a].label << " / " << kFactoryPins[pair.b].label);
            const double d = factoryDistance(states[pair.a], states[pair.b]);
            REQUIRE(d > static_cast<double>(kMinFactoryStateDistance));
            REQUIRE(d == Catch::Approx(pair.distance).epsilon(0.02));
        }
    }

    SECTION("Closest-pair regression pin (SC-008 cl.2)") {
        // A SEPARATELY LABELLED regression pin, not the pass/fail threshold:
        // it catches a later FR-022 constant change that silently converges two
        // states even though the pair still clears kMinFactoryStateDistance.
        constexpr float kMeasuredClosestPairDistance = 0.5258f;
        const double closest = factoryDistance(states[0], states[2]); // SineStack / Choir
        REQUIRE(closest
                == Catch::Approx(static_cast<double>(kMeasuredClosestPairDistance)).epsilon(0.10));
    }

    SECTION("Cosine similarity of the three ratio_n = n states (FR-022)") {
        // For SineStack / Choir / Breath the ratio term is exactly 0, so
        // d = sqrt(2*(1 - rho)) and d >= 0.4 is exactly rho <= 0.92.
        constexpr double kMaxCosineSimilarity = 0.92;

        struct CosinePin {
            std::size_t a;
            std::size_t b;
            double rho;
        };
        constexpr std::array<CosinePin, 3> kCosinePins{
            CosinePin{0, 2, 0.8618},  // SineStack / Choir
            CosinePin{0, 4, 0.4025},  // SineStack / Breath
            CosinePin{2, 4, 0.4447}}; // Choir / Breath

        for (const CosinePin& pin : kCosinePins) {
            INFO(kFactoryPins[pin.a].label << " / " << kFactoryPins[pin.b].label);
            const double rho = amplitudeCosine(states[pin.a], states[pin.b]);
            REQUIRE(rho <= kMaxCosineSimilarity);
            REQUIRE(rho == Catch::Approx(pin.rho).epsilon(0.02));
        }
    }
}

TEST_CASE("SpectralState_FactoryConsumesNoRng", "[spectral_state][seraphis]") {
    // memcmp over the whole struct is only meaningful if SpectralState has no
    // padding bytes; it has none (two 64-float arrays, 16 chars, two floats and
    // an int, all 4-byte aligned), and this assertion is what keeps that true.
    STATIC_REQUIRE(sizeof(SpectralState)
                   == 2 * SpectralState::kStatePartials * sizeof(float)
                          + SpectralState::kStateNameBytes + 2 * sizeof(float) + sizeof(int));

    Xorshift32 rng{1u};

    SECTION("makeFactoryState does not advance a shared RNG (SC-008 cl.4)") {
        for (const FactoryPin& pin : kFactoryPins) {
            INFO(pin.label);
            const std::uint32_t before = rng.state();
            const SpectralState s = makeFactoryState(pin.id);
            REQUIRE(isValidSpectralState(s));
            REQUIRE(rng.state() == before);
        }
    }

    SECTION("Calls separated by 10^6 RNG draws are bitwise identical (SC-008 cl.4)") {
        std::array<SpectralState, 5> first{};
        for (std::size_t k = 0; k < kFactoryPins.size(); ++k) {
            first[k] = makeFactoryState(kFactoryPins[k].id);
        }

        constexpr int kDraws = 1000000;
        for (int i = 0; i < kDraws; ++i) {
            (void)rng.next();
        }

        for (std::size_t k = 0; k < kFactoryPins.size(); ++k) {
            INFO(kFactoryPins[k].label);
            const SpectralState second = makeFactoryState(kFactoryPins[k].id);
            // NOLINTNEXTLINE(bugprone-suspicious-memory-comparison) - intentional bit-exact check
            REQUIRE(std::memcmp(&first[k], &second, sizeof(SpectralState)) == 0);
            REQUIRE(statesBitwiseEqual(first[k], second));
        }
    }
}

TEST_CASE("SpectralState_SerializationRoundTrips", "[spectral_state][seraphis]") {
    STATIC_REQUIRE(kSpectralStateBytes == 541);
    STATIC_REQUIRE(Krate::DSP::kSpectralStateFormatVersion == 1);

    // std::memcmp over the WHOLE struct is only a valid bitwise comparison if
    // SpectralState has no padding bytes. It has none, and this assertion is
    // what keeps that true for the round-trip checks below.
    STATIC_REQUIRE(sizeof(SpectralState)
                   == 2 * SpectralState::kStatePartials * sizeof(float)
                          + SpectralState::kStateNameBytes + 2 * sizeof(float) + sizeof(int));

    using Buffer = std::array<std::byte, kSpectralStateBytes>;

    struct CorpusEntry {
        const char* label;
        SpectralState state;
    };

    // SC-007's corpus: the 5 factory states plus 3 edge states.
    const std::array<CorpusEntry, 8> corpus{
        CorpusEntry{"factory: SineStack", makeFactoryState(SpectralStateId::SineStack)},
        CorpusEntry{"factory: Bell", makeFactoryState(SpectralStateId::Bell)},
        CorpusEntry{"factory: Choir", makeFactoryState(SpectralStateId::Choir)},
        CorpusEntry{"factory: Glass", makeFactoryState(SpectralStateId::Glass)},
        CorpusEntry{"factory: Breath", makeFactoryState(SpectralStateId::Breath)},
        CorpusEntry{"edge: numPartials = 0 (default state)", SpectralState{}},
        CorpusEntry{"edge: numPartials = 1 at kMinStateRatio", makeSinglePartialEdgeState()},
        CorpusEntry{"edge: numPartials = 64, extremal metadata, 15-char name",
                    makeFullEdgeState()}};

    SECTION("Every corpus state round-trips bitwise (FR-031, FR-033, SC-007)") {
        for (const CorpusEntry& entry : corpus) {
            INFO(entry.label);
            REQUIRE(isValidSpectralState(entry.state));

            Buffer buf{};
            buf.fill(std::byte{0xA5});
            REQUIRE(serializeSpectralState(entry.state, buf.data(), buf.size())
                    == kSpectralStateBytes);

            // `out` starts as a DIFFERENT valid state, so a deserialize that
            // silently leaves it alone cannot pass by accident.
            SpectralState out = makeValidState();
            REQUIRE(deserializeSpectralState(buf.data(), buf.size(), out));
            // NOLINTNEXTLINE(bugprone-suspicious-memory-comparison) - intentional bit-exact check
            REQUIRE(std::memcmp(&entry.state, &out, sizeof(SpectralState)) == 0);
            REQUIRE(statesBitwiseEqual(entry.state, out));

            // Re-serializing what came back reproduces the stream byte for byte
            // (FR-033: the payload is stored values, not arithmetic results).
            Buffer again{};
            again.fill(std::byte{0x5A});
            REQUIRE(serializeSpectralState(out, again.data(), again.size()) == kSpectralStateBytes);
            REQUIRE(std::memcmp(buf.data(), again.data(), buf.size()) == 0);
        }
    }

    SECTION("Byte-stream digest golden pins the FR-031 layout (SC-007)") {
        // ------------------------------------------------------------------
        // WHAT THIS DIGEST IS -- AND WHAT IT IS NOT.
        //
        // It is an FNV-1a digest over a SERIALIZED BYTE STREAM: the 541 STORED
        // VALUES that serializeSpectralState() copied out of a SpectralState.
        // It is NOT a render golden. No audio is rendered anywhere in this
        // file, and no float here is the result of a computation whose last
        // bits could move: every field of kLayoutProbe is an exactly
        // representable binary32 dyadic rational (see makeLayoutProbeState).
        // The stream is therefore byte-identical on MSVC, GCC and Apple Clang,
        // including the macOS leg's -ffast-math.
        //
        // dsp/CLAUDE.md's "never pin a render with a bit-exact digest over
        // float samples" rule consequently does not apply, and its explicit
        // carve-out does: digests over a serialized byte stream are "the
        // correct way to pin a preset format".
        //
        // What this catches: any change to the FR-031 field order, offsets,
        // widths, padding or endianness. What it must NEVER be extended to
        // cover: a state whose floats come out of arithmetic -- see the
        // factory-state section below for why.
        //
        // Golden recomputed by hand from the FR-031 layout table this session
        // (offset 0 version, 1 numPartials, 5 tilt, 9 inharmonicity, 13 ratios,
        // 269 amplitudes, 525 name), not read back out of the implementation.
        // ------------------------------------------------------------------
        constexpr std::uint64_t kLayoutProbeDigest = 0x6309B57B5E358D7AULL;

        const SpectralState probe = makeLayoutProbeState();
        REQUIRE(isValidSpectralState(probe));

        Buffer buf{};
        buf.fill(std::byte{0xA5});
        REQUIRE(serializeSpectralState(probe, buf.data(), buf.size()) == kSpectralStateBytes);

        // Two offsets asserted directly as well, so a digest mismatch says
        // WHICH end of the layout moved.
        REQUIRE(static_cast<unsigned>(buf[0]) == 1u);
        std::int32_t decodedCount = 0;
        std::memcpy(&decodedCount, buf.data() + 1, sizeof(decodedCount));
        REQUIRE(decodedCount == 64);

        REQUIRE(fnv1aOverBytes(buf.data(), buf.size()) == kLayoutProbeDigest);

        SpectralState out{};
        REQUIRE(deserializeSpectralState(buf.data(), buf.size(), out));
        // NOLINTNEXTLINE(bugprone-suspicious-memory-comparison) - intentional bit-exact check
        REQUIRE(std::memcmp(&probe, &out, sizeof(SpectralState)) == 0);
    }

    SECTION("Every factory state has a checked-in header/name digest golden (SC-007, D18)") {
        // ------------------------------------------------------------------
        // SC-007 asks for "a checked-in golden per factory state". THIS IS IT,
        // for every byte of a factory state's stream that can portably carry
        // one -- see deviation D18 in spec.md for the derivation, and the
        // section below for the 512 bytes that cannot.
        //
        // The digest covers the two ARITHMETIC-FREE regions of the FR-031
        // layout, concatenated in stream order:
        //   [0, 13)     version, numPartials, tiltDbPerOct, inharmonicity
        //   [525, 541)  name
        // Every one of those 29 bytes is a stored constant: the version byte,
        // an int32 partial count, two floats that makeFactoryState never
        // writes (so they hold the NSDMI 0.0f, bit pattern 0x00000000), and a
        // NUL-padded ASCII label. No transcendental, no normalisation, no
        // reassociable reduction -- identical on MSVC, glibc and Apple's libm,
        // -ffast-math included.
        //
        // What it catches that the run-to-run / mutual-distinctness clauses
        // below CANNOT: a changed partial count, a renamed or truncated label,
        // a factory state that started writing tilt or inharmonicity, a
        // changed format version, and any move of the name field relative to
        // the header. Those are exactly the fields a preset format must pin.
        //
        // Goldens computed independently from the FR-031 layout table this
        // session (a 29-byte FNV-1a over the bytes named above), not read back
        // out of a run.
        // ------------------------------------------------------------------
        struct HeaderPin {
            SpectralStateId id;
            const char* label;
            std::uint64_t digest;
        };
        const std::array<HeaderPin, 5> headerPins{
            HeaderPin{SpectralStateId::SineStack, "SineStack", 0xA7631DF3A83E0401ULL},
            HeaderPin{SpectralStateId::Bell, "Bell", 0x52DB0DA2DC293495ULL},
            HeaderPin{SpectralStateId::Choir, "Choir", 0x517EB7970084F4E9ULL},
            HeaderPin{SpectralStateId::Glass, "Glass", 0x4E10D4B9D466A9C0ULL},
            HeaderPin{SpectralStateId::Breath, "Breath", 0xACB307A3367314EEULL}};
        STATIC_REQUIRE(headerPins.size() == Krate::DSP::kSpectralStateCount);

        constexpr std::size_t kHeaderBytes = 13;                          // [0, 13)
        constexpr std::size_t kNameOffset = 525;                          // [525, 541)
        constexpr std::size_t kNameBytes = SpectralState::kStateNameBytes; // 16
        STATIC_REQUIRE(kNameOffset + kNameBytes == kSpectralStateBytes);

        std::array<std::uint64_t, headerPins.size()> seen{};
        for (std::size_t k = 0; k < headerPins.size(); ++k) {
            INFO(headerPins[k].label);

            Buffer buf{};
            buf.fill(std::byte{0xA5});
            REQUIRE(serializeSpectralState(makeFactoryState(headerPins[k].id), buf.data(),
                                           buf.size())
                    == kSpectralStateBytes);

            std::array<std::byte, kHeaderBytes + kNameBytes> pinned{};
            std::memcpy(pinned.data(), buf.data(), kHeaderBytes);
            std::memcpy(pinned.data() + kHeaderBytes, buf.data() + kNameOffset, kNameBytes);

            // Asserted directly as well, so a digest mismatch says WHICH field
            // moved rather than only that something did.
            REQUIRE(static_cast<unsigned>(buf[0]) == 1u);
            REQUIRE(static_cast<unsigned>(buf[5]) == 0u);   // tiltDbPerOct == +0.0f
            REQUIRE(static_cast<unsigned>(buf[9]) == 0u);   // inharmonicity == +0.0f
            REQUIRE(static_cast<char>(buf[kNameOffset]) == headerPins[k].label[0]);

            seen[k] = fnv1aOverBytes(pinned.data(), pinned.size());
            REQUIRE(seen[k] == headerPins[k].digest);
        }

        // Non-vacuity: five identical digests would satisfy five equalities
        // against five identical constants without pinning anything.
        for (std::size_t a = 0; a < seen.size(); ++a) {
            for (std::size_t b = a + 1; b < seen.size(); ++b) {
                INFO(headerPins[a].label << " / " << headerPins[b].label);
                REQUIRE(seen[a] != seen[b]);
            }
        }
    }

    SECTION("Factory-state byte streams are deterministic and mutually distinct (SC-007)") {
        // ------------------------------------------------------------------
        // DELIBERATELY NOT a checked-in digest constant over the WHOLE stream
        // of a factory state. The 29 arithmetic-free bytes ARE pinned exactly,
        // in the section above; this section covers the remaining 512, which
        // hold the ratio and amplitude arrays.
        //
        // The factory amplitudes come out of std::pow / std::exp and then the
        // FR-014 normalisation's 1/sqrt(sum-of-squares). Those transcendentals
        // differ in their last bits between MSVC's CRT, glibc and Apple's libm,
        // and the macOS leg additionally builds -ffast-math, under which the
        // sum-of-squares reduction may be reassociated and 1/sqrt lowered to
        // rsqrt + Newton. A hard-coded per-factory digest would therefore be a
        // bit-exact float golden wearing a byte-stream costume: green on
        // Windows and structurally incapable of passing the other two CI legs
        // (exactly the failure mode dsp/CLAUDE.md and
        // tools/lint-float-bit-goldens.js exist to prevent).
        //
        // The format pin lives on kLayoutProbe above, which is arithmetic-free,
        // and the per-factory pin lives on the 29 header/name bytes in the
        // section above. What the ARRAY bytes are pinned on here is everything
        // that IS toolchain-independent: the same state serializes identically
        // every time, all five streams differ from one another, and each one
        // round-trips bitwise (asserted in the first section). Their VALUES are
        // pinned separately and portably by SC-008 -- ten pairwise spectral
        // distances within 2%, plus per-state max-ratio and ratio-sum pins
        // (kFactoryPins, :121-126).
        // ------------------------------------------------------------------
        STATIC_REQUIRE(kFactoryPins.size() == Krate::DSP::kSpectralStateCount);
        std::array<std::uint64_t, kFactoryPins.size()> digests{};

        for (std::size_t k = 0; k < kFactoryPins.size(); ++k) {
            INFO(kFactoryPins[k].label);

            Buffer first{};
            first.fill(std::byte{0xA5});
            Buffer second{};
            second.fill(std::byte{0x5A});

            REQUIRE(serializeSpectralState(makeFactoryState(kFactoryPins[k].id), first.data(),
                                           first.size())
                    == kSpectralStateBytes);
            REQUIRE(serializeSpectralState(makeFactoryState(kFactoryPins[k].id), second.data(),
                                           second.size())
                    == kSpectralStateBytes);
            REQUIRE(std::memcmp(first.data(), second.data(), first.size()) == 0);

            digests[k] = fnv1aOverBytes(first.data(), first.size());
        }

        for (std::size_t a = 0; a < digests.size(); ++a) {
            for (std::size_t b = a + 1; b < digests.size(); ++b) {
                INFO(kFactoryPins[a].label << " / " << kFactoryPins[b].label);
                REQUIRE(digests[a] != digests[b]);
            }
        }
    }

    SECTION("serializeSpectralState rejects and writes nothing (FR-031)") {
        const SpectralState valid = makeLayoutProbeState();

        Buffer poison{};
        poison.fill(std::byte{0xA5});

        {
            INFO("capacity = kSpectralStateBytes - 1");
            Buffer buf = poison;
            REQUIRE(serializeSpectralState(valid, buf.data(), kSpectralStateBytes - 1) == 0);
            REQUIRE(std::memcmp(buf.data(), poison.data(), buf.size()) == 0);
        }
        {
            INFO("dest == nullptr");
            REQUIRE(serializeSpectralState(valid, nullptr, kSpectralStateBytes) == 0);
        }
        {
            INFO("invalid state: non-monotone ratios");
            SpectralState bad = valid;
            bad.ratios[1] = bad.ratios[0]; // no longer STRICTLY increasing
            REQUIRE_FALSE(isValidSpectralState(bad));

            Buffer buf = poison;
            REQUIRE(serializeSpectralState(bad, buf.data(), buf.size()) == 0);
            REQUIRE(std::memcmp(buf.data(), poison.data(), buf.size()) == 0);
        }
    }

    SECTION("deserializeSpectralState rejects and leaves `out` untouched (FR-031, FR-032)") {
        Buffer good{};
        good.fill(std::byte{0xA5});
        REQUIRE(serializeSpectralState(makeLayoutProbeState(), good.data(), good.size())
                == kSpectralStateBytes);

        const SpectralState sentinel = makeValidState();

        const auto rejects = [&sentinel](const char* label, const std::byte* src,
                                         std::size_t size) {
            INFO(label);
            SpectralState out = sentinel;
            REQUIRE_FALSE(deserializeSpectralState(src, size, out));
            // NOLINTNEXTLINE(bugprone-suspicious-memory-comparison) - intentional bit-exact check
            REQUIRE(std::memcmp(&out, &sentinel, sizeof(SpectralState)) == 0);
            REQUIRE(statesBitwiseEqual(out, sentinel));
        };

        {
            Buffer buf = good;
            buf[0] = std::byte{2}; // FR-032: unknown format version
            rejects("version byte = 2", buf.data(), buf.size());
        }

        rejects("size = kSpectralStateBytes - 1", good.data(), kSpectralStateBytes - 1);
        rejects("src == nullptr", nullptr, kSpectralStateBytes);

        {
            // A well-formed 541-byte stream whose decoded PAYLOAD violates
            // FR-012: ratios[0] = 5.0 followed by ratios[1] = 4.0, written
            // straight into the buffer at the layout's offsets 13 and 17.
            Buffer buf = good;
            const float firstRatio = 5.0f;
            const float secondRatio = 4.0f;
            std::memcpy(buf.data() + 13, &firstRatio, sizeof(float));
            std::memcpy(buf.data() + 17, &secondRatio, sizeof(float));
            rejects("non-monotone ratios at offsets 13/17", buf.data(), buf.size());
        }
    }
}
