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
#include <climits>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <utility>
#include <vector>

using Krate::DSP::blendStates;
using Krate::DSP::deserializeSpectralState;
using Krate::DSP::isValidSpectralState;
using Krate::DSP::kSpectralStateBytes;
using Krate::DSP::makeFactoryState;
using Krate::DSP::normalizeSpectralState;
using Krate::DSP::serializeSpectralState;
using Krate::DSP::setPartial;
using Krate::DSP::SpectralState;
using Krate::DSP::SpectralStateId;
using Krate::DSP::tiltState;
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
/// The five palette-widening rows (2026-08-30) are MEASURED-then-pinned per the
/// design doc's section 1.7 loop: Hollow/Organ/Shimmer's maxima live in their
/// FR-041 fill regions (like Bell's), Metal's 125.86 is the authored
/// n*sqrt(1 + 0.0007 n^2) top slot, and the 28.000 spacings are the fill
/// floor's 28-cent step.
constexpr std::array<FactoryPin, 10> kFactoryPins{
    FactoryPin{SpectralStateId::SineStack, "SineStack", 64, 64.00, 27.264},
    FactoryPin{SpectralStateId::Bell, "Bell", 24, 240.32, 28.000},
    FactoryPin{SpectralStateId::Choir, "Choir", 64, 64.00, 27.264},
    FactoryPin{SpectralStateId::Glass, "Glass", 64, 80.38, 32.79},
    FactoryPin{SpectralStateId::Breath, "Breath", 64, 64.00, 27.264},
    FactoryPin{SpectralStateId::Hollow, "Hollow", 32, 150.47, 28.000},
    FactoryPin{SpectralStateId::Metal, "Metal", 64, 125.86, 47.395},
    FactoryPin{SpectralStateId::Organ, "Organ", 9, 265.03, 28.000},
    FactoryPin{SpectralStateId::Vowel, "Vowel", 64, 64.00, 27.264},
    FactoryPin{SpectralStateId::Shimmer, "Shimmer", 16, 244.44, 28.000}};

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

// --------------------------------------------------------------------------
// Authoring-mutator helpers (Phase 11 T002, FR-031/FR-032, SC-012)
// --------------------------------------------------------------------------

/// A valid 4-partial state whose slots 0 and 2 are closer together than
/// kAuthorSpacing^2, so setPartial's monotone window at index 1 COLLAPSES
/// (lo > hi) and the call must be a no-op rather than a clamped store.
[[nodiscard]] SpectralState makeCollapsedWindowState() noexcept {
    SpectralState s = makeValidState();
    s.ratios[0] = 1.0f;
    s.ratios[1] = 1.005f;
    s.ratios[2] = 1.01f;
    s.ratios[3] = 4.0f;
    return s;
}

/// The three states below are invalid SOMEWHERE OTHER THAN slot 0. They are the
/// rows that prove setPartial's whole-state gate (plan §1.1 step 0) exists:
/// every LOCAL check at index 0 passes on all three, so without the gate the
/// store happens and the byte-unchanged assertion fails.
[[nodiscard]] SpectralState makeAmplitudeInvalidAtSlot5() noexcept {
    SpectralState s = makeFactoryState(SpectralStateId::SineStack);
    s.amplitudes[5] = 1.5f; // > 1 -> isValidSpectralState false (:106-108)
    return s;
}

[[nodiscard]] SpectralState makeRatioNonMonotoneAtSlot30() noexcept {
    SpectralState s = makeFactoryState(SpectralStateId::SineStack);
    s.ratios[30] = s.ratios[29]; // no longer STRICTLY increasing (:97-99)
    return s;
}

[[nodiscard]] SpectralState makeNameWithoutTerminator() noexcept {
    SpectralState s = makeFactoryState(SpectralStateId::SineStack);
    s.name.fill('A'); // no NUL anywhere in the field (:118-120)
    return s;
}

[[nodiscard]] SpectralState makeStateWithNumPartials(int n) noexcept {
    SpectralState s = makeValidState();
    s.numPartials = n;
    return s;
}

[[nodiscard]] SpectralState makeRatioBelowMinState() noexcept {
    SpectralState s = makeValidState();
    s.ratios[0] = 0.4f; // < kMinStateRatio
    return s;
}

[[nodiscard]] SpectralState makeRatioAboveMaxState() noexcept {
    SpectralState s = makeValidState();
    s.numPartials = 1;
    s.ratios[0] = 200.0f; // > kMaxStateRatio
    return s;
}

[[nodiscard]] SpectralState makeEqualNeighbourState() noexcept {
    SpectralState s = makeValidState();
    s.ratios[1] = s.ratios[0];
    return s;
}

[[nodiscard]] SpectralState makeDescendingNeighbourState() noexcept {
    SpectralState s = makeValidState();
    s.ratios[1] = s.ratios[0] - 0.1f;
    return s;
}

[[nodiscard]] SpectralState makeStateWithPoisonedFloat(std::uint32_t bits, int field) noexcept {
    SpectralState s = makeValidState();
    const float bad = floatFromBits(bits);
    switch (field) {
    case 0:
        s.ratios[0] = bad;
        break;
    case 1:
        s.amplitudes[0] = bad;
        break;
    default:
        s.tiltDbPerOct = bad;
        break;
    }
    return s;
}

[[nodiscard]] SpectralState makeInharmonicityOutOfRangeState() noexcept {
    SpectralState s = makeValidState();
    s.inharmonicity = 0.5f; // > kMaxStateInharmonicity (0.1)
    return s;
}

/// Which mutator a table row exercises.
enum class Mutator : std::uint8_t { SetPartial, BlendStates, TiltState };

/// One row of the SC-012 table. `before` is setPartial/tiltState's subject and
/// blendStates' `a`; `other` is blendStates' `b`.
struct AuthoringRow {
    const char* label;
    Mutator mutator;
    SpectralState before;
    SpectralState other;
    std::size_t index; ///< setPartial only
    float arg0;        ///< setPartial ratio | blendStates t | tiltState dbPerOct
    float arg1;        ///< setPartial amplitude
};

[[nodiscard]] AuthoringRow setPartialRow(const char* label, const SpectralState& before,
                                         std::size_t index, float ratio, float amplitude) {
    return AuthoringRow{label, Mutator::SetPartial, before, SpectralState{}, index, ratio,
                        amplitude};
}

[[nodiscard]] AuthoringRow tiltRow(const char* label, const SpectralState& before,
                                   float dbPerOct) {
    return AuthoringRow{label, Mutator::TiltState, before, SpectralState{}, 0, dbPerOct, 0.0f};
}

[[nodiscard]] AuthoringRow blendRow(const char* label, const SpectralState& a,
                                    const SpectralState& b, float t) {
    return AuthoringRow{label, Mutator::BlendStates, a, b, 0, t, 0.0f};
}

/// The SC-012 table. Coverage is enumerated in tasks.md T002: out-of-range and
/// non-monotone ratios, amplitude and t edges, every interesting numPartials and
/// index, both tilt clamp edges and beyond, non-finite arguments built from bit
/// patterns, and three states that are invalid AWAY FROM the edited index.
[[nodiscard]] std::vector<AuthoringRow> makeAuthoringRows() {
    const SpectralState valid4 = makeValidState();
    const SpectralState sine64 = makeFactoryState(SpectralStateId::SineStack);
    const SpectralState bell24 = makeFactoryState(SpectralStateId::Bell);
    const SpectralState single = makeSinglePartialEdgeState();
    const SpectralState empty{};

    // Deliberately NOT named `nan` / `inf`: <cmath> declares ::nan and ::inf-like
    // names at global scope, and a local shadowing them trips -Wshadow.
    const float nanValue = floatFromBits(kQuietNaNBits);
    const float posInfValue = floatFromBits(kPosInfBits);
    const float negInfValue = floatFromBits(kNegInfBits);

    std::vector<AuthoringRow> rows;

    // ---- setPartial, VALID inputs (assertion 1: still valid afterwards) -----
    rows.push_back(setPartialRow("setPartial: index 0, in-window ratio", valid4, 0, 1.2f, 0.5f));
    rows.push_back(
        setPartialRow("setPartial: index numPartials-1 (3)", valid4, 3, 10.0f, 0.25f));
    rows.push_back(
        setPartialRow("setPartial: ratio < kMinStateRatio (0.1)", valid4, 1, 0.1f, 0.5f));
    rows.push_back(
        setPartialRow("setPartial: ratio > kMaxStateRatio (500)", valid4, 1, 500.0f, 0.5f));
    rows.push_back(setPartialRow("setPartial: amplitude -0.5f", valid4, 0, 2.0f, -0.5f));
    rows.push_back(setPartialRow("setPartial: amplitude 1.5f", valid4, 0, 2.0f, 1.5f));
    rows.push_back(setPartialRow("setPartial: amplitude exactly 0.0f", valid4, 2, 3.0f, 0.0f));
    rows.push_back(setPartialRow("setPartial: amplitude exactly 1.0f", valid4, 2, 3.0f, 1.0f));
    rows.push_back(setPartialRow("setPartial: index == numPartials (4)", valid4, 4, 2.5f, 0.5f));
    rows.push_back(setPartialRow("setPartial: index 63 on a 4-partial state", valid4, 63, 2.5f,
                                 0.5f));
    rows.push_back(setPartialRow("setPartial: index 64 on a 64-partial state", sine64, 64, 2.5f,
                                 0.5f));
    rows.push_back(setPartialRow("setPartial: index SIZE_MAX", valid4, SIZE_MAX, 2.5f, 0.5f));
    rows.push_back(setPartialRow("setPartial: index 63 on a 64-partial state", sine64, 63, 200.0f,
                                 0.5f));
    rows.push_back(setPartialRow("setPartial: index 0 at exactly kMinStateRatio", sine64, 0, 0.5f,
                                 0.5f));
    rows.push_back(
        setPartialRow("setPartial: single-partial state at kMaxStateRatio", single, 0, 128.0f,
                      1.0f));
    rows.push_back(setPartialRow("setPartial: numPartials == 0", empty, 0, 2.0f, 0.5f));
    rows.push_back(setPartialRow("setPartial: collapsed window (neighbours < spacing^2)",
                                 makeCollapsedWindowState(), 1, 1.5f, 0.5f));
    rows.push_back(
        setPartialRow("setPartial: NaN ratio (bit pattern)", valid4, 0, nanValue, 0.5f));
    rows.push_back(
        setPartialRow("setPartial: +Inf ratio (bit pattern)", valid4, 0, posInfValue, 0.5f));
    rows.push_back(
        setPartialRow("setPartial: NaN amplitude (bit pattern)", valid4, 0, 2.0f, nanValue));
    rows.push_back(
        setPartialRow("setPartial: -Inf amplitude (bit pattern)", valid4, 0, 2.0f, negInfValue));

    // ---- setPartial, INVALID inputs (assertion 2: byte-unchanged) -----------
    rows.push_back(setPartialRow("setPartial on invalid: ratios[0] < kMinStateRatio",
                                 makeRatioBelowMinState(), 0, 1.0f, 0.5f));
    rows.push_back(setPartialRow("setPartial on invalid: ratios[0] > kMaxStateRatio",
                                 makeRatioAboveMaxState(), 0, 1.0f, 0.5f));
    rows.push_back(setPartialRow("setPartial on invalid: equal neighbours",
                                 makeEqualNeighbourState(), 1, 2.5f, 0.5f));
    rows.push_back(setPartialRow("setPartial on invalid: descending neighbours",
                                 makeDescendingNeighbourState(), 1, 2.5f, 0.5f));
    rows.push_back(setPartialRow("setPartial on invalid: numPartials = -1",
                                 makeStateWithNumPartials(-1), 0, 2.0f, 0.5f));
    rows.push_back(setPartialRow("setPartial on invalid: numPartials = 65",
                                 makeStateWithNumPartials(65), 0, 2.0f, 0.5f));
    rows.push_back(setPartialRow("setPartial on invalid: numPartials = INT_MAX",
                                 makeStateWithNumPartials(INT_MAX), 0, 2.0f, 0.5f));
    rows.push_back(setPartialRow("setPartial on invalid: NaN in ratios[0]",
                                 makeStateWithPoisonedFloat(kQuietNaNBits, 0), 1, 2.5f, 0.5f));
    rows.push_back(setPartialRow("setPartial on invalid: +Inf in amplitudes[0]",
                                 makeStateWithPoisonedFloat(kPosInfBits, 1), 1, 2.5f, 0.5f));
    rows.push_back(setPartialRow("setPartial on invalid: -Inf in tiltDbPerOct",
                                 makeStateWithPoisonedFloat(kNegInfBits, 2), 0, 2.0f, 0.5f));
    rows.push_back(setPartialRow("setPartial on invalid: inharmonicity 0.5",
                                 makeInharmonicityOutOfRangeState(), 0, 2.0f, 0.5f));
    // The three whole-state-gate rows: invalid AWAY FROM the edited index 0.
    rows.push_back(setPartialRow("setPartial at 0 on state invalid at amplitudes[5]",
                                 makeAmplitudeInvalidAtSlot5(), 0, 1.1f, 0.5f));
    rows.push_back(setPartialRow("setPartial at 0 on state invalid at ratios[30]",
                                 makeRatioNonMonotoneAtSlot30(), 0, 1.1f, 0.5f));
    rows.push_back(setPartialRow("setPartial at 0 on state with unterminated name",
                                 makeNameWithoutTerminator(), 0, 1.1f, 0.5f));

    // ---- tiltState ---------------------------------------------------------
    rows.push_back(tiltRow("tiltState: kMinStateTiltDbPerOct (-12)", valid4,
                           SpectralState::kMinStateTiltDbPerOct));
    rows.push_back(tiltRow("tiltState: kMaxStateTiltDbPerOct (+12)", valid4,
                           SpectralState::kMaxStateTiltDbPerOct));
    rows.push_back(tiltRow("tiltState: -30 (beyond the low clamp edge)", valid4, -30.0f));
    rows.push_back(tiltRow("tiltState: +30 (beyond the high clamp edge)", valid4, 30.0f));
    rows.push_back(tiltRow("tiltState: -6 on a 64-partial state", sine64, -6.0f));
    rows.push_back(tiltRow("tiltState: +6 on a 64-partial state", sine64, 6.0f));
    rows.push_back(tiltRow("tiltState: numPartials == 0", empty, 3.0f));
    rows.push_back(tiltRow("tiltState: single-partial state", single, -12.0f));
    rows.push_back(tiltRow("tiltState: NaN dbPerOct (bit pattern)", valid4, nanValue));
    rows.push_back(tiltRow("tiltState: +Inf dbPerOct (bit pattern)", valid4, posInfValue));
    rows.push_back(
        tiltRow("tiltState on invalid: numPartials = -1", makeStateWithNumPartials(-1), 3.0f));
    rows.push_back(
        tiltRow("tiltState on invalid: equal neighbours", makeEqualNeighbourState(), -6.0f));
    rows.push_back(
        tiltRow("tiltState on invalid: unterminated name", makeNameWithoutTerminator(), 6.0f));
    rows.push_back(tiltRow("tiltState on invalid: amplitudes[5] = 1.5",
                           makeAmplitudeInvalidAtSlot5(), 0.0f));

    // ---- blendStates (assertion 3: the RETURN is valid on every row) --------
    rows.push_back(blendRow("blendStates: t = 0.0", valid4, sine64, 0.0f));
    rows.push_back(blendRow("blendStates: t = 0.5", valid4, sine64, 0.5f));
    rows.push_back(blendRow("blendStates: t = 1.0", valid4, sine64, 1.0f));
    rows.push_back(blendRow("blendStates: t = -1.0", valid4, sine64, -1.0f));
    rows.push_back(blendRow("blendStates: t = 2.0", valid4, sine64, 2.0f));
    rows.push_back(blendRow("blendStates: t = NaN (bit pattern)", valid4, sine64, nanValue));
    rows.push_back(blendRow("blendStates: t = +Inf (bit pattern)", valid4, sine64, posInfValue));
    rows.push_back(blendRow("blendStates: a invalid (numPartials = -1)",
                            makeStateWithNumPartials(-1), sine64, 0.5f));
    rows.push_back(
        blendRow("blendStates: b invalid (unterminated name)", valid4,
                 makeNameWithoutTerminator(), 0.5f));
    rows.push_back(blendRow("blendStates: both invalid", makeStateWithNumPartials(65),
                            makeDescendingNeighbourState(), 0.5f));
    rows.push_back(blendRow("blendStates: two empty states", empty, empty, 0.5f));
    rows.push_back(blendRow("blendStates: 1-partial vs 4-partial", single, valid4, 0.25f));
    rows.push_back(blendRow("blendStates: SineStack vs Bell", sine64, bell24, 0.75f));
    rows.push_back(blendRow("blendStates: a blended with itself", valid4, valid4, 0.3f));

    return rows;
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
    STATIC_REQUIRE(Krate::DSP::kSpectralStateCount == 10);
    STATIC_REQUIRE(kFactoryPins.size() == Krate::DSP::kSpectralStateCount);

    std::array<SpectralState, kFactoryPins.size()> states{};
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

    SECTION("All 45 pairs clear the a-priori distance threshold (SC-008 cl.2)") {
        constexpr float kMinFactoryStateDistance = 0.4f;

        struct PairPin {
            std::size_t a;
            std::size_t b;
            double distance;
        };
        // Rows 0-9 are plan §3.4 (recomputed in double and float32-emulated
        // arithmetic, identical to 4 decimals). The 35 palette-widening rows
        // are MEASURED values from the design-doc §1.7 measure-then-pin loop
        // (g++ 13 -O2 -fno-fast-math, double accumulation), 2026-08-30.
        constexpr std::array<PairPin, 45> kPairPins{
            PairPin{0, 1, 1.5723}, PairPin{0, 2, 0.5258}, PairPin{0, 3, 0.7406},
            PairPin{0, 4, 1.0932}, PairPin{1, 2, 2.0245}, PairPin{1, 3, 1.9742},
            PairPin{1, 4, 2.5841}, PairPin{2, 3, 0.8824}, PairPin{2, 4, 1.0538},
            PairPin{3, 4, 0.9517},
            // -- palette widening (Hollow 5, Metal 6, Organ 7, Vowel 8, Shimmer 9)
            PairPin{0, 5, 1.3973}, PairPin{0, 6, 1.2351}, PairPin{0, 7, 1.3312},
            PairPin{0, 8, 0.4838}, PairPin{0, 9, 2.7194}, PairPin{1, 5, 1.3273},
            PairPin{1, 6, 2.2155}, PairPin{1, 7, 1.9793}, PairPin{1, 8, 1.8936},
            PairPin{1, 9, 1.9771}, PairPin{2, 5, 1.4251}, PairPin{2, 6, 1.1490},
            PairPin{2, 7, 1.1184}, PairPin{2, 8, 0.4864}, PairPin{2, 9, 2.4653},
            PairPin{3, 5, 1.3405}, PairPin{3, 6, 0.8770}, PairPin{3, 7, 1.6173},
            PairPin{3, 8, 1.0151}, PairPin{3, 9, 2.7108}, PairPin{4, 5, 1.7519},
            PairPin{4, 6, 1.0848}, PairPin{4, 7, 1.8713}, PairPin{4, 8, 1.2061},
            PairPin{4, 9, 2.9399}, PairPin{5, 6, 1.3912}, PairPin{5, 7, 2.1316},
            PairPin{5, 8, 1.5837}, PairPin{5, 9, 1.7141}, PairPin{6, 7, 1.5958},
            PairPin{6, 8, 1.3670}, PairPin{6, 9, 2.6778}, PairPin{7, 8, 1.1968},
            PairPin{7, 9, 3.1642}, PairPin{8, 9, 2.7007}};
        STATIC_REQUIRE(kPairPins.size()
                       == kFactoryPins.size() * (kFactoryPins.size() - 1) / 2);

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
        // Since the palette widening the closest pair is SineStack / Vowel
        // (measured 0.4838; the old closest, SineStack / Choir at 0.5258, is
        // now second).
        constexpr float kMeasuredClosestPairDistance = 0.4838f;
        const double closest = factoryDistance(states[0], states[8]); // SineStack / Vowel
        REQUIRE(closest
                == Catch::Approx(static_cast<double>(kMeasuredClosestPairDistance)).epsilon(0.10));
    }

    SECTION("Cosine similarity of the ratio_n = n states (FR-022)") {
        // For the four harmonic-ratio states (SineStack, Choir, Breath, Vowel)
        // the ratio term is exactly 0, so d = sqrt(2*(1 - rho)) and d >= 0.4 is
        // exactly rho <= 0.92. (Pairs with DIFFERENT ratio laws are covered by
        // the 45-pair distance clause above -- their nonzero ratio term is what
        // carries the distinctness, e.g. SineStack / Bell at rho 0.9654 yet
        // d = 1.5723.)
        constexpr double kMaxCosineSimilarity = 0.92;

        struct CosinePin {
            std::size_t a;
            std::size_t b;
            double rho;
        };
        // Vowel rows measured 2026-08-30 with the final section-1.7 constants
        // (kVowelFloor 0.05, centres {2, 5, 14}, sigmas {0.7, 1.0, 2.5}).
        constexpr std::array<CosinePin, 6> kCosinePins{
            CosinePin{0, 2, 0.8618},  // SineStack / Choir
            CosinePin{0, 4, 0.4025},  // SineStack / Breath
            CosinePin{2, 4, 0.4447},  // Choir / Breath
            CosinePin{0, 8, 0.8830},  // SineStack / Vowel
            CosinePin{2, 8, 0.8817},  // Choir / Vowel
            CosinePin{4, 8, 0.2727}}; // Breath / Vowel

        for (const CosinePin& pin : kCosinePins) {
            INFO(kFactoryPins[pin.a].label << " / " << kFactoryPins[pin.b].label);
            const double rho = amplitudeCosine(states[pin.a], states[pin.b]);
            REQUIRE(rho <= kMaxCosineSimilarity);
            REQUIRE(rho == Catch::Approx(pin.rho).epsilon(0.02));
        }
    }

    SECTION("New-archetype ratio and amplitude laws, spot-checked (FR-021, FR-022)") {
        const SpectralState& hollow = states[5];
        const SpectralState& metal = states[6];
        const SpectralState& organ = states[7];
        const SpectralState& vowel = states[8];
        const SpectralState& shimmer = states[9];

        SECTION("Hollow: odd harmonics 2k-1, monotonically decaying amplitudes") {
            // 2k-1 over small integers is EXACT in binary32 -- no tolerance
            // needed on the ratio law itself.
            for (int k = 1; k <= hollow.numPartials; ++k) {
                const auto i = static_cast<std::size_t>(k - 1);
                INFO("authored slot " << i);
                REQUIRE(hollow.ratios[i] == static_cast<float>(2 * k - 1));
            }
            for (std::size_t i = 1; std::cmp_less(i, hollow.numPartials); ++i) {
                INFO("amplitude slot " << i);
                REQUIRE(hollow.amplitudes[i] < hollow.amplitudes[i - 1]);
            }
        }

        SECTION("Metal: stretched ladder with every THIRD partial dominant") {
            // ratio_n = n * sqrt(1 + 0.0007 n^2): top authored slot ~125.86,
            // still under kMaxStateRatio, and always sharp of harmonic.
            REQUIRE(static_cast<double>(metal.ratios[63])
                    == Catch::Approx(125.857).epsilon(0.001));
            for (int n = 1; n <= metal.numPartials; ++n) {
                const auto i = static_cast<std::size_t>(n - 1);
                INFO("authored slot " << i);
                REQUIRE(metal.ratios[i] >= static_cast<float>(n));
            }
            // Inverted comb: n = 3 is the GLOBAL amplitude peak (the design's
            // "fundamental attenuated to 0.35, peak at n = 3"), and every
            // multiple of 3 dominates its immediate neighbours.
            for (std::size_t i = 0; std::cmp_less(i, metal.numPartials); ++i) {
                if (i == 2) {
                    continue;
                }
                INFO("slot " << i << " vs the n = 3 peak");
                REQUIRE(metal.amplitudes[2] > metal.amplitudes[i]);
            }
            for (int n = 3; n <= metal.numPartials - 1; n += 3) {
                const auto i = static_cast<std::size_t>(n - 1);
                INFO("comb tooth at n = " << n);
                REQUIRE(metal.amplitudes[i] > metal.amplitudes[i - 1]);
                REQUIRE(metal.amplitudes[i] > metal.amplitudes[i + 1]);
            }
        }

        SECTION("Organ: the authored drawbar tables, verbatim ratios") {
            namespace factory = Krate::DSP::detail::factory;
            REQUIRE(std::cmp_equal(organ.numPartials, factory::kOrganRatios.size()));
            for (std::size_t i = 0; i < factory::kOrganRatios.size(); ++i) {
                INFO("drawbar " << i);
                // Stored VERBATIM from the constant table -- no arithmetic, so
                // exact equality is portable.
                REQUIRE(organ.ratios[i] == factory::kOrganRatios[i]);
            }
            // 16' drawbar sits at the inclusive FR-012 lower ratio bound.
            REQUIRE(organ.ratios[0] == SpectralState::kMinStateRatio);
            // Amplitude PROPORTIONS survive the FR-014 normalisation: 8' is
            // the loudest rank and every ratio-to-8' matches the table.
            for (std::size_t i = 0; i < factory::kOrganAmps.size(); ++i) {
                if (i == 1) {
                    continue;
                }
                INFO("rank " << i << " vs the 8' rank");
                REQUIRE(organ.amplitudes[i] < organ.amplitudes[1]);
                REQUIRE(static_cast<double>(organ.amplitudes[i] / organ.amplitudes[1])
                        == Catch::Approx(static_cast<double>(factory::kOrganAmps[i]
                                                             / factory::kOrganAmps[1]))
                               .margin(1e-5));
            }
        }

        SECTION("Vowel: harmonic ratios, formant humps at the authored centres") {
            for (int n = 1; n <= vowel.numPartials; ++n) {
                INFO("authored slot " << (n - 1));
                REQUIRE(vowel.ratios[static_cast<std::size_t>(n - 1)]
                        == static_cast<float>(n));
            }
            // F1 (centre 2) carries the global peak; the inter-formant valley
            // at n = 4 sits below BOTH neighbouring formants (F2 centre 5).
            for (std::size_t i = 0; std::cmp_less(i, vowel.numPartials); ++i) {
                if (i == 1) {
                    continue;
                }
                INFO("slot " << i << " vs the F1 peak at n = 2");
                REQUIRE(vowel.amplitudes[1] > vowel.amplitudes[i]);
            }
            REQUIRE(vowel.amplitudes[4] > vowel.amplitudes[3]); // n = 5 > n = 4
        }

        SECTION("Shimmer: faint anchor plus a stretched sparse-high cluster") {
            REQUIRE(shimmer.ratios[0] == 1.0f);
            // Cluster ratios m*(1 + 0.002 m), m = 4k: measured ends 8.128 and
            // 72.192 (both exact expressions of the law, pinned with margin).
            REQUIRE(static_cast<double>(shimmer.ratios[1])
                    == Catch::Approx(8.128).epsilon(0.001));
            REQUIRE(static_cast<double>(shimmer.ratios[15])
                    == Catch::Approx(72.192).epsilon(0.001));
            // The anchor is FAINTER than the first cluster partial, and the
            // cluster decays monotonically ((4k)^-0.35).
            REQUIRE(shimmer.amplitudes[0] < shimmer.amplitudes[1]);
            for (std::size_t i = 2; std::cmp_less(i, shimmer.numPartials); ++i) {
                INFO("cluster slot " << i);
                REQUIRE(shimmer.amplitudes[i] < shimmer.amplitudes[i - 1]);
            }
        }

        SECTION("FR-041 geometric fill continues every sparse new state") {
            // The unauthored slots must follow the same recurrence the engine
            // uses: grown = min(prev * clamp(prev/prevprev, 1, 2), 128), then
            // floored at prev * kFillSpacingFactor. Recomputed here in float,
            // compared with a relative tolerance (the state's own value went
            // through identical float ops, but from a different TU).
            namespace factory = Krate::DSP::detail::factory;
            for (const SpectralState* s : {&hollow, &organ, &shimmer}) {
                const auto count = static_cast<std::size_t>(s->numPartials);
                for (std::size_t j = count; j < SpectralState::kStatePartials; ++j) {
                    const float lastSpacing = s->ratios[j - 1] / s->ratios[j - 2];
                    const float g = std::clamp(lastSpacing, 1.0f, factory::kFillMaxGrowth);
                    const float grown = std::min(s->ratios[j - 1] * g, factory::kFillMaxRatio);
                    const float floorValue = s->ratios[j - 1] * factory::kFillSpacingFactor;
                    const float expected = std::max(grown, floorValue);
                    INFO(s->name.data() << " fill slot " << j);
                    REQUIRE(static_cast<double>(s->ratios[j])
                            == Catch::Approx(static_cast<double>(expected)).epsilon(1e-5));
                }
            }
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
        std::array<SpectralState, kFactoryPins.size()> first{};
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

    // SC-007's corpus: the 10 factory states plus 3 edge states.
    const std::array<CorpusEntry, 13> corpus{
        CorpusEntry{"factory: SineStack", makeFactoryState(SpectralStateId::SineStack)},
        CorpusEntry{"factory: Bell", makeFactoryState(SpectralStateId::Bell)},
        CorpusEntry{"factory: Choir", makeFactoryState(SpectralStateId::Choir)},
        CorpusEntry{"factory: Glass", makeFactoryState(SpectralStateId::Glass)},
        CorpusEntry{"factory: Breath", makeFactoryState(SpectralStateId::Breath)},
        CorpusEntry{"factory: Hollow", makeFactoryState(SpectralStateId::Hollow)},
        CorpusEntry{"factory: Metal", makeFactoryState(SpectralStateId::Metal)},
        CorpusEntry{"factory: Organ", makeFactoryState(SpectralStateId::Organ)},
        CorpusEntry{"factory: Vowel", makeFactoryState(SpectralStateId::Vowel)},
        CorpusEntry{"factory: Shimmer", makeFactoryState(SpectralStateId::Shimmer)},
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
        // The five palette-widening rows were computed independently the same
        // way (Node.js: [version 1][int32 count][2 zero floats][NUL-padded
        // name] -> 64-bit FNV-1a), 2026-08-30, and cross-checked against the
        // implementation's serialized bytes.
        const std::array<HeaderPin, 10> headerPins{
            HeaderPin{SpectralStateId::SineStack, "SineStack", 0xA7631DF3A83E0401ULL},
            HeaderPin{SpectralStateId::Bell, "Bell", 0x52DB0DA2DC293495ULL},
            HeaderPin{SpectralStateId::Choir, "Choir", 0x517EB7970084F4E9ULL},
            HeaderPin{SpectralStateId::Glass, "Glass", 0x4E10D4B9D466A9C0ULL},
            HeaderPin{SpectralStateId::Breath, "Breath", 0xACB307A3367314EEULL},
            HeaderPin{SpectralStateId::Hollow, "Hollow", 0xEB20453503F01F47ULL},
            HeaderPin{SpectralStateId::Metal, "Metal", 0xA1862DCD67F63D85ULL},
            HeaderPin{SpectralStateId::Organ, "Organ", 0xD8CB78D5DD3D1096ULL},
            HeaderPin{SpectralStateId::Vowel, "Vowel", 0xD50356A068262BF7ULL},
            HeaderPin{SpectralStateId::Shimmer, "Shimmer", 0xD0BFDCA57439EB75ULL}};
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

        // Non-vacuity: ten identical digests would satisfy ten equalities
        // against ten identical constants without pinning anything.
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
        // every time, all ten streams differ from one another, and each one
        // round-trips bitwise (asserted in the first section). Their VALUES are
        // pinned separately and portably by SC-008 -- forty-five pairwise
        // spectral distances within 2%, plus per-state max-ratio and
        // min-spacing pins (kFactoryPins above).
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

// ==============================================================================
// Phase 11 (specs/seraphis-phase11-ui) T002 - the three authoring mutators
// ==============================================================================
// FR-031, FR-032, SC-012. The contract under test is PRESERVATION, not repair:
//   1. a valid input state is still valid after the call;
//   2. an INVALID input state is left BYTE-UNCHANGED by setPartial and tiltState
//      -- there is no half-write, because every rejection (whole-state AND
//      local) happens before the first store;
//   3. blendStates' RETURN satisfies isValidSpectralState unconditionally.
// ==============================================================================

TEST_CASE("SpectralState_AuthoringMutators_PreserveValidity", "[spectral_state][phase11]") {
    // std::memcmp over the whole struct is a valid bitwise comparison only if
    // SpectralState has no padding bytes. It has none, and this assertion is
    // what keeps clause 2 below meaningful.
    STATIC_REQUIRE(std::is_trivially_copyable_v<SpectralState>);
    STATIC_REQUIRE(sizeof(SpectralState)
                   == 2 * SpectralState::kStatePartials * sizeof(float)
                          + SpectralState::kStateNameBytes + 2 * sizeof(float) + sizeof(int));

    SECTION("The SC-012 table: every row preserves the branch its input selects") {
        const std::vector<AuthoringRow> rows = makeAuthoringRows();
        REQUIRE(rows.size() >= 40u); // tasks.md T002: the table is at least 40 rows

        for (const AuthoringRow& row : rows) {
            INFO(row.label);
            const bool beforeValid = isValidSpectralState(row.before);

            if (row.mutator == Mutator::BlendStates) {
                // Clause 3: unconditionally valid, valid inputs or not.
                const SpectralState result = blendStates(row.before, row.other, row.arg0);
                CHECK(isValidSpectralState(result));
                continue;
            }

            SpectralState after = row.before;
            if (row.mutator == Mutator::SetPartial) {
                setPartial(after, row.index, row.arg0, row.arg1);
            } else {
                tiltState(after, row.arg0);
            }

            if (beforeValid) {
                CHECK(isValidSpectralState(after)); // clause 1
            } else {
                // clause 2 -- byte-unchanged, no half-write.
                // NOLINTNEXTLINE(bugprone-suspicious-memory-comparison) - intentional bit-exact
                CHECK(std::memcmp(&row.before, &after, sizeof(SpectralState)) == 0);
                CHECK(statesBitwiseEqual(row.before, after));
            }
        }
    }

    SECTION("blendStates' endpoints are BYTE-IDENTICAL to the endpoints (SC-025)") {
        // Two valid states with DIFFERENT name fields and DIFFERENT numPartials:
        // the interior body cannot reproduce either byte-for-byte (it rewrites
        // `name` to "Blend", regenerates the tail from the FR-041 continuation,
        // and exp2(log2(x)) is not the identity), so this is the case that is
        // red without the exact-endpoint short-circuit.
        const SpectralState a = makeValidState();                            // "Test", 4
        const SpectralState b = makeFactoryState(SpectralStateId::SineStack); // "SineStack", 64
        REQUIRE(isValidSpectralState(a));
        REQUIRE(isValidSpectralState(b));
        REQUIRE(a.numPartials != b.numPartials);
        REQUIRE(std::strcmp(a.name.data(), b.name.data()) != 0);

        const SpectralState atZero = blendStates(a, b, 0.0f);
        const SpectralState atOne = blendStates(a, b, 1.0f);

        // NOLINTNEXTLINE(bugprone-suspicious-memory-comparison) - intentional bit-exact
        CHECK(std::memcmp(&atZero, &a, sizeof(SpectralState)) == 0);
        CHECK(statesBitwiseEqual(atZero, a));
        // NOLINTNEXTLINE(bugprone-suspicious-memory-comparison) - intentional bit-exact
        CHECK(std::memcmp(&atOne, &b, sizeof(SpectralState)) == 0);
        CHECK(statesBitwiseEqual(atOne, b));

        // Non-vacuity: the interior really is a different state, so the two
        // equalities above are not passing because everything is equal anyway.
        const SpectralState atHalf = blendStates(a, b, 0.5f);
        CHECK(isValidSpectralState(atHalf));
        CHECK_FALSE(statesBitwiseEqual(atHalf, a));
        CHECK_FALSE(statesBitwiseEqual(atHalf, b));
    }

    SECTION("tiltState is ABSOLUTE: applying it twice equals applying it once") {
        SpectralState once = makeValidState();
        tiltState(once, -6.0f);
        REQUIRE(isValidSpectralState(once));
        CHECK(once.tiltDbPerOct == -6.0f);

        SpectralState twice = once;
        tiltState(twice, -6.0f);

        // NOLINTNEXTLINE(bugprone-suspicious-memory-comparison) - intentional bit-exact
        CHECK(std::memcmp(&once, &twice, sizeof(SpectralState)) == 0);
        CHECK(statesBitwiseEqual(once, twice));

        // Non-vacuity: the FIRST application really did move the amplitudes, so
        // the equality above is about absoluteness and not about a dead mutator.
        const SpectralState untouched = makeValidState();
        CHECK_FALSE(statesBitwiseEqual(once, untouched));
    }

    SECTION("setPartial CLAMPS into the monotone window, it never swaps neighbours") {
        const SpectralState before = makeValidState(); // ratios 1, 2, 3, 4
        SpectralState after = before;
        setPartial(after, 1, 100.0f, 0.5f); // far beyond the window's upper edge

        // The neighbours are untouched and strict monotonicity survives.
        CHECK(after.ratios[0] == before.ratios[0]);
        CHECK(after.ratios[2] == before.ratios[2]);
        CHECK(after.ratios[3] == before.ratios[3]);
        CHECK(after.ratios[1] > after.ratios[0]);
        CHECK(after.ratios[1] < after.ratios[2]);
        CHECK(isValidSpectralState(after));

        // Clamped to exactly the window edge ratios[2] / kAuthorSpacing.
        CHECK(static_cast<double>(after.ratios[1])
              == Catch::Approx(static_cast<double>(before.ratios[2])
                               / static_cast<double>(Krate::DSP::kAuthorSpacing))
                     .margin(1e-5));

        // Nothing outside ratios[index] / amplitudes[index] moved.
        CHECK(after.numPartials == before.numPartials);
        CHECK(after.tiltDbPerOct == before.tiltDbPerOct);
        CHECK(after.inharmonicity == before.inharmonicity);
        for (std::size_t i = 0; i < SpectralState::kStateNameBytes; ++i) {
            CHECK(after.name[i] == before.name[i]);
        }
    }

    SECTION("setPartial is a NO-OP when the monotone window collapses") {
        const SpectralState before = makeCollapsedWindowState();
        REQUIRE(isValidSpectralState(before));

        SpectralState after = before;
        setPartial(after, 1, 1.5f, 0.25f);

        // lo = ratios[0] * kAuthorSpacing > hi = ratios[2] / kAuthorSpacing, so
        // the call must write NOTHING rather than store a clamped value.
        // NOLINTNEXTLINE(bugprone-suspicious-memory-comparison) - intentional bit-exact
        CHECK(std::memcmp(&before, &after, sizeof(SpectralState)) == 0);
        CHECK(statesBitwiseEqual(before, after));
    }
}
