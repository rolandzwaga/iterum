// ==============================================================================
// Seraphis - Version-3 state tests (Phase 10)
// ==============================================================================
// Reference: specs/seraphis-phase10-effects/spec.md   (C-6, C-8, FR-018,
//            FR-019, FR-021, FR-031 - FR-034, SC-009)
//            specs/seraphis-phase10-effects/plan.md   (D-2, section 2.5.4)
//
// CRITERION OWNED BY THIS TU: SC-009 - state version 3 round-trips and migrates.
//   (a) all 16 effects IDs driven NON-DEFAULT, getState -> setState, every field
//       back EXACTLY (floats compared with ==; this is a serialization round trip
//       inside one process, not a render, so there is no toolchain FP spread to
//       tolerate and an approximate comparison would be strictly weaker);
//   (b) a version-2 blob loads with kResultOk, the 16 effects fields sit at their
//       C-6 defaults, and every Phase 9 field equals what the blob encodes;
//   (c) a version-1 blob still loads with kResultOk;
//   (d) the v3 blob's bytes FROM OFFSET 4 have the v2 blob's bytes from offset 4
//       as a STRICT PREFIX, and the two differ ONLY in the leading int32.
//
// CRITERION ADDED BY PHASE 11 (specs/seraphis-phase11-ui, T012): SC-015 /
// FR-034/FR-034a - an EDITED state round-trips at version 3, unchanged.
//   Seraphis_EditedState_RoundTripsAtV3 authors a slot payload edit, a pan
//   override and a mask through the REAL C-5 message path, then asserts the
//   272-byte [partials] block is appended LAST, the leading int32 is still 3,
//   getState -> setState -> getState is BYTE-IDENTICAL (FR-094), and a stream
//   truncated immediately before the block still loads with every override
//   absent - the EOF-safe strict-prefix chain, never a version branch.
//
// ------------------------------------------------------------------------------
// THREE CONSTRUCTION CHOICES, stated here rather than discovered in review:
//
// 1. THE EXPECTED BYTES ARE DERIVED, NOT TRANSCRIBED. Arm (a) compares the
//    [effects] block the PROCESSOR wrote against one produced by driving a local
//    EffectsParams through the pack's own handleEffectsParamChange + the pack's
//    own saveEffectsParams with the SAME normalized values. That is what makes
//    the assertion about T008's new code - the dispatch rung in
//    processParameterChanges() and the saveEffectsParams() call in getState() -
//    rather than a second hand-copy of effects_params.h's denormalization
//    arithmetic, which would drift the first time a range moved.
//    NON-VACUITY is asserted separately and per field: every one of the 16
//    reference values must differ from the C-6 default (spec C-6), so a build
//    that never stored anything cannot pass by writing defaults.
//
// 2. THE VERSION-2 BLOB IS THE v3 BLOB'S OWN 2532-BYTE PREFIX, RE-STAMPED. C-8
//    defines a v2 stream as exactly that (the [effects] block is APPENDED after
//    every v2 field), and a 2532-byte literal byte array is not maintainable.
//    The construction is deliberately NOT the load-bearing part of arms (b)/(d):
//    what is asserted is that RELOADING that prefix reproduces every Phase 9
//    field byte-for-byte while the effects fields fall back to their C-6
//    defaults. A build that wrote the effects block anywhere but LAST would cut
//    a Phase 9 field at the 2532-byte boundary and fail that comparison.
//
// 3. FR-034 IS ASSERTED ON THE COMPONENTS, NOT ON THE ATOMICS. The
//    `setState()`-after-prepare path is covered by TWO SECTIONs. The first
//    (T008) reads back the composed output saturation through
//    SeraphisEngine::getOutputSaturation() (seraphis_engine.h:695). The second
//    (T022) reads back the SpectralDelay and MidSideProcessor surfaces the send
//    stage now installs - `getBaseDelayMs()` (spectral_delay.h:474),
//    `getFeedback()` (:509), `getDiffusion()` (:538), `getStereoWidth()` (:561),
//    `getSpreadDirection()` (:487), `getTimeMode()` (:572), `getNoteValue()`
//    (:580) and `globalMs_.getWidth()` (midside_processor.h:236).
//
//    SC-009 ALONE CANNOT COVER FR-034. Every arm above compares values read back
//    out of the EffectsParams ATOMICS, so a build that loads a stream into the
//    atomics and never re-pushes them to the DSP round-trips perfectly while the
//    loaded patch renders on the prepare-time values. The T022 SECTION is the
//    only clause in this TU that asserts the PUSH happened.
// ==============================================================================

#include "processor/processor.h"

// Phase 11 T018. The CONTROLLER half of SC-016: slotMirror_ is controller-side
// session state, and its two re-seed sources (the 409-412 dropdowns and the
// state stream) can only be observed from the controller. This TU therefore
// names BOTH components - which is a test-only composition, not a production
// cross-include: neither shipping header sees the other.
#include "controller/controller.h"

#include "parameters/dropdown_mappings.h"
#include "parameters/effects_params.h"
#include "parameters/morph_params.h"
#include "plugin_ids.h"
#include "seraphis_test_fixture.h"
// Phase 11 C-5. The wire format the edit arm drives the processor through - the
// same POD Controller sends, so the round trip is asserted about the SHIPPING
// path rather than about a test-only back door.
#include "ui/edit_message.h"

#include "base/source/fstreamer.h"
#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"

#include <pluginterfaces/base/smartpointer.h>  // Steinberg::owned
#include <pluginterfaces/vst/ivstmessage.h>    // IMessage / IAttributeList

#include <krate/dsp/core/note_value.h>
#include <krate/dsp/effects/spectral_delay.h>
#include <krate/dsp/processors/midside_processor.h>
#include <krate/dsp/processors/spectral_state.h>
#include <krate/dsp/systems/seraphis_engine.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>

using namespace Steinberg;

namespace {

// =============================================================================
// Stream sizes - spec C-8
// =============================================================================

/// Phase 9's layout: the version int32 + 73 floats + 18 int32 + 4 x 541.
constexpr int32 kV2StateBytes = 2532;
/// Phase 8's layout, a strict prefix of both.
constexpr int32 kV1StateBytes = 36;
/// C-8's [effects] block: 12 floats + 4 int32, appended LAST in Phase 10.
constexpr int32 kEffectsBlockBytes = 64;
/// Everything through [effects] - the whole stream as PHASE 10 wrote it, and
/// therefore the OFFSET of the block Phase 11 appends after it.
constexpr int32 kEffectsEndBytes = 2596;
/// Phase 11 FR-034a's [partials] block: 64 x float pan + two uint64 masks,
/// appended LAST with kCurrentStateVersion UNCHANGED at 3.
constexpr int32 kPartialsBlockBytes = 272;
/// The WHOLE v3 stream as Phase 11 writes it: 2596 + 272.
constexpr int32 kV3StateBytes = kEffectsEndBytes + kPartialsBlockBytes;

static_assert(12 * 4 + 4 * 4 == kEffectsBlockBytes,
              "SC-009: C-8's [effects] block is 12 floats + 4 int32 = 64 bytes");
static_assert(kV2StateBytes + kEffectsBlockBytes == kEffectsEndBytes,
              "SC-009: the Phase 10 stream is the v2 stream plus the 64-byte [effects] block");
static_assert(85 * 4 + 22 * 4 + 4 + 4 * 541 == kEffectsEndBytes,
              "SC-009: the Phase 10 arithmetic must reproduce 2596");
static_assert(64 * 4 + 8 + 8 == kPartialsBlockBytes,
              "SC-015 / FR-034a: 256 pan bytes + two 8-byte masks = 272");
static_assert(kV3StateBytes == 2868,
              "SC-015 / FR-034a: the Phase 11 stream is 2596 + 272 - and the VERSION int32 "
              "at offset 0 still reads 3, because the block is an APPEND");

// =============================================================================
// Stream plumbing (modelled on unit/state_v2_test.cpp)
// =============================================================================

struct StreamReleaser {
    void operator()(MemoryStream* s) const noexcept {
        if (s != nullptr) {
            s->release();
        }
    }
};
using StreamPtr = std::unique_ptr<MemoryStream, StreamReleaser>;

[[nodiscard]] StreamPtr makeStream() { return StreamPtr(new MemoryStream()); }

void rewindStream(MemoryStream& s) { s.seek(0, IBStream::kIBSeekSet, nullptr); }

[[nodiscard]] StreamPtr captureState(Seraphis::Processor& proc) {
    StreamPtr s = makeStream();
    REQUIRE(proc.getState(s.get()) == kResultOk);
    rewindStream(*s);
    return s;
}

/// A hand-built 36-byte VERSION-1 stream: version | float gain | int32 poly |
/// int32 soft | five macro floats. Nothing else - that is the whole point.
/// Same shape as state_v2_test.cpp's helper.
[[nodiscard]] StreamPtr makeV1Stream(int32 version, float gain, int32 poly, int32 soft,
                                     float dream, float bloom, float dissolve,
                                     float gravity, float entropy) {
    StreamPtr s = makeStream();
    {
        IBStreamer w(s.get(), kLittleEndian);
        w.writeInt32(version);
        w.writeFloat(gain);
        w.writeInt32(poly);
        w.writeInt32(soft);
        w.writeFloat(dream);
        w.writeFloat(bloom);
        w.writeFloat(dissolve);
        w.writeFloat(gravity);
        w.writeFloat(entropy);
    }
    rewindStream(*s);
    return s;
}

/// Construction choice 2: a version-2 blob is the v3 blob's own 2532-byte
/// prefix with the leading version int32 re-stamped to kStateVersion2.
[[nodiscard]] StreamPtr makeV2BlobFrom(MemoryStream& v3) {
    REQUIRE(v3.getSize() == kV3StateBytes);
    StreamPtr s = makeStream();
    int32 written = 0;
    s->write(v3.getData(), kV2StateBytes, &written);
    REQUIRE(written == kV2StateBytes);

    const int32 version = Seraphis::kStateVersion2;
    std::memcpy(s->getData(), &version, sizeof(int32));
    rewindStream(*s);
    return s;
}

// =============================================================================
// The [effects] block, read straight out of a stream by offset
// =============================================================================
// Field order is saveEffectsParams()' order (effects_params.h:417-437), which is
// C-6's table order with the four discrete rows collected at the end:
//   floats  1400 1410 1411 1412 1414 1415 1416 1417 1440 1441 1442 1443
//   int32s  1413 1419 1418 1430

struct EffectsBlock {
    std::array<float, 12> f{};
    std::array<int32, 4> i{};
};

[[nodiscard]] EffectsBlock decodeEffectsBlock(const char* base) {
    EffectsBlock b{};
    std::memcpy(b.f.data(), base, b.f.size() * sizeof(float));
    std::memcpy(b.i.data(), base + b.f.size() * sizeof(float), b.i.size() * sizeof(int32));
    return b;
}

/// The [effects] block of a full v3 stream.
[[nodiscard]] EffectsBlock effectsBlockOf(MemoryStream& s) {
    REQUIRE(s.getSize() == kV3StateBytes);
    return decodeEffectsBlock(s.getData() + kV2StateBytes);
}

// =============================================================================
// Spec C-6's 16 effects rows, and the non-default value each is driven to
// =============================================================================

struct EffectsRow {
    Vst::ParamID id;
    /// The NORMALIZED value the row is driven to. Every one is chosen so the
    /// denormalized result differs from that row's C-6 default; the difference is
    /// asserted per field below, never assumed.
    double normalized;
};

constexpr EffectsRow kEffects[] = {
    {.id = Seraphis::kFxSaturationId, .normalized = 0.80},            // default 0.15
    {.id = Seraphis::kFxDelayMixId, .normalized = 0.42},              // default 0
    {.id = Seraphis::kFxDelayTimeId, .normalized = 0.50},             // -> 1000 ms, default 250
    {.id = Seraphis::kFxDelaySpreadId, .normalized = 0.25},           // -> 500 ms,  default 0
    {.id = Seraphis::kFxDelaySpreadDirectionId, .normalized = 1.00},  // -> index 2, default 0
    {.id = Seraphis::kFxDelayFeedbackId, .normalized = 0.50},         // -> 0.475,   default 0.35
    {.id = Seraphis::kFxDelayTiltId, .normalized = 0.75},             // -> +0.5,    default 0
    {.id = Seraphis::kFxDelayDiffusionId, .normalized = 0.65},        // default 0.30
    {.id = Seraphis::kFxDelayWidthId, .normalized = 0.20},            // default 0.50
    {.id = Seraphis::kFxDelaySyncId, .normalized = 1.00},             // -> on,      default off
    {.id = Seraphis::kFxDelaySyncNoteId, .normalized = 0.00},         // -> index 0, default 7
    {.id = Seraphis::kFxSpectralFreezeId, .normalized = 1.00},        // -> on,      default off
    {.id = Seraphis::kFxWidthId, .normalized = 0.75},                 // -> 150 %,   default 100 %
    {.id = Seraphis::kFxWanderDepthId, .normalized = 0.60},           // default 0
    {.id = Seraphis::kFxWanderRateId, .normalized = 0.85},            // default 0.50
    {.id = Seraphis::kFxAzimuthDepthId, .normalized = 0.35},          // default 0
};

constexpr std::size_t kEffectsRowCount = sizeof(kEffects) / sizeof(kEffects[0]);
static_assert(kEffectsRowCount == 16, "spec C-6 registers exactly 16 effects IDs");

/// The rows `driveEffects` writes, replayed into a LOCAL pack instance through
/// the pack's own handler. ONE body, so an expectation and the processor that
/// produced the value it is compared against cannot drift apart.
///
/// `quietWander` holds IDs 1441 and 1443 at their C-6 default of 0 - see the
/// banner on the T022 SECTION for why that arm needs them there.
void applyDrivenRows(Seraphis::EffectsParams& params, bool quietWander) {
    for (const EffectsRow& row : kEffects) {
        const bool quiet = quietWander
                           && (row.id == Seraphis::kFxWanderDepthId
                               || row.id == Seraphis::kFxAzimuthDepthId);
        Seraphis::handleEffectsParamChange(params, row.id, quiet ? 0.0 : row.normalized);
    }
}

/// Construction choice 1: the expected block, produced by the PACK's own
/// handler + writer from the same normalized values the processor was given.
[[nodiscard]] EffectsBlock referenceEffectsBlock() {
    Seraphis::EffectsParams params{};
    applyDrivenRows(params, /*quietWander=*/false);
    StreamPtr s = makeStream();
    {
        IBStreamer w(s.get(), kLittleEndian);
        Seraphis::saveEffectsParams(params, w);
    }
    REQUIRE(s->getSize() == kEffectsBlockBytes);
    return decodeEffectsBlock(s->getData());
}

/// The block a processor that never saw an effects parameter must write.
[[nodiscard]] EffectsBlock defaultEffectsBlock() {
    const Seraphis::EffectsParams params{};  // C-6 defaults, by construction
    StreamPtr s = makeStream();
    {
        IBStreamer w(s.get(), kLittleEndian);
        Seraphis::saveEffectsParams(params, w);
    }
    REQUIRE(s->getSize() == kEffectsBlockBytes);
    return decodeEffectsBlock(s->getData());
}

/// EXACT equality, field by field, with the field index in the failure message.
/// Floats are compared with `==` on purpose: this is a serialization round trip
/// inside one process, so both sides are the same stored bits.
void requireBlocksEqual(const EffectsBlock& got, const EffectsBlock& want) {
    for (std::size_t k = 0; k < got.f.size(); ++k) {
        INFO("[effects] float field " << k);
        CHECK(got.f[k] == want.f[k]);
    }
    for (std::size_t k = 0; k < got.i.size(); ++k) {
        INFO("[effects] int32 field " << k);
        CHECK(got.i[k] == want.i[k]);
    }
}

/// The C-6 default column, named constant by named constant (effects_params.h:
/// 104-119) rather than re-derived, so a default that silently moved is caught.
void requireBlockIsC6Defaults(const EffectsBlock& got) {
    CHECK(got.f[0] == Seraphis::kFxSaturationDefault);
    CHECK(got.f[1] == Seraphis::kFxDelayMixDefault);
    CHECK(got.f[2] == Seraphis::kFxDelayTimeDefault);
    CHECK(got.f[3] == Seraphis::kFxDelaySpreadDefault);
    CHECK(got.f[4] == Seraphis::kFxDelayFeedbackDefault);
    CHECK(got.f[5] == Seraphis::kFxDelayTiltDefault);
    CHECK(got.f[6] == Seraphis::kFxDelayDiffusionDefault);
    CHECK(got.f[7] == Seraphis::kFxDelayWidthDefault);
    CHECK(got.f[8] == Seraphis::kFxWidthDefault);
    CHECK(got.f[9] == Seraphis::kFxWanderDepthDefault);
    CHECK(got.f[10] == Seraphis::kFxWanderRateDefault);
    CHECK(got.f[11] == Seraphis::kFxAzimuthDepthDefault);

    CHECK(got.i[0] == static_cast<int32>(Seraphis::kFxDelaySpreadDirectionDefault));
    CHECK(got.i[1] == static_cast<int32>(Seraphis::kFxDelaySyncNoteDefault));
    CHECK(got.i[2] == (Seraphis::kFxDelaySyncDefault ? 1 : 0));
    CHECK(got.i[3] == (Seraphis::kFxSpectralFreezeDefault ? 1 : 0));
}

/// Prepare, drive all 16 effects IDs, render one block so every push path ran.
/// `quietWander` mirrors applyDrivenRows()' flag on the PROCESSOR side.
void driveEffects(SeraphisTest::ProcessorFixture& fx, double sampleRate, int32 blockSize,
                  bool quietWander = false) {
    REQUIRE(fx.prepare(sampleRate, blockSize) == kResultOk);
    for (const EffectsRow& row : kEffects) {
        const bool quiet = quietWander
                           && (row.id == Seraphis::kFxWanderDepthId
                               || row.id == Seraphis::kFxAzimuthDepthId);
        fx.setParam(row.id, quiet ? 0.0 : row.normalized);
    }
    REQUIRE(fx.processBlock(blockSize) == kResultOk);
}

// =============================================================================
// T022's ACCESS SHIM - two PRIVATE members of Seraphis::Processor
// =============================================================================
// FR-034's subjects are `spectralDelay_` (processor.h:669) and `globalMs_`
// (:803). Neither is reachable from this translation unit: FR-041 closes the
// public test surface at its named `*ForTest()` accessors and neither member is
// among them, and the one friendship the phase grants
// (Seraphis::detail::SeraphisEffectsStageBypassProbe, processor.h:276) is
// DEFINED by integration/effects_chain_test.cpp with in-class - hence inline -
// members, so re-declaring it here would be an ODR violation rather than a
// forward declaration.
//
// So the two reads use the standard explicit-instantiation access idiom, in the
// same shape unit/lifecycle_test.cpp:238-266 uses it for globalMs_ and
// fxReturnGainSm_: [temp.spec]/6 states that the usual access checking rules do
// NOT apply to names used to specify an explicit instantiation, which is the one
// place a pointer-to-private-member may legally be formed from outside the
// class. Everything here has internal linkage (the enclosing unnamed namespace),
// so nothing collides with that file's copy.
//
// The tags carry the members' exact declared types, so a member whose name or
// type moved is a COMPILE error here rather than a silently-skipped clause. Both
// helpers hand back a const reference and nothing writes through them.

struct SpectralDelayAccess {
    using MemberPtr = Krate::DSP::SpectralDelay Seraphis::Processor::*;
    friend MemberPtr seraphisStateV3PrivateMember(SpectralDelayAccess);
};

struct GlobalMsAccess {
    using MemberPtr = Krate::DSP::MidSideProcessor Seraphis::Processor::*;
    friend MemberPtr seraphisStateV3PrivateMember(GlobalMsAccess);
};

template <typename Access, typename Access::MemberPtr Member>
struct PrivateMemberBinder {
    friend typename Access::MemberPtr seraphisStateV3PrivateMember(Access) { return Member; }
};

// THIS is the exempt context - the member pointers must not be named elsewhere.
template struct PrivateMemberBinder<SpectralDelayAccess, &Seraphis::Processor::spectralDelay_>;
template struct PrivateMemberBinder<GlobalMsAccess, &Seraphis::Processor::globalMs_>;

[[nodiscard]] const Krate::DSP::SpectralDelay& spectralDelayOf(
    const Seraphis::Processor& processor) noexcept {
    return processor.*seraphisStateV3PrivateMember(SpectralDelayAccess{});
}

[[nodiscard]] const Krate::DSP::MidSideProcessor& globalMsOf(
    const Seraphis::Processor& processor) noexcept {
    return processor.*seraphisStateV3PrivateMember(GlobalMsAccess{});
}

// =============================================================================
// Phase 11 FR-034a - the appended [partials] block, read out of a stream
// =============================================================================
// Layout, 272 bytes, appended LAST (after [effects]) with kCurrentStateVersion
// UNCHANGED at 3 - Processor::savePartialOverrides() is the authority:
//
//   | Offset | Size | Field                                       |
//   |      0 |  256 | 64 x float pan, index order                 |
//   |    256 |    8 | uint64 panOverrideBits (writeInt64u)        |
//   |    264 |    8 | uint64 maskBits        (writeInt64u)        |
//
// Decoded by OFFSET rather than by re-running the writer, so a block written at
// the wrong place in the chain - or split into four int32s - fails here.

struct PartialsBlock {
    std::array<float, 64> pan{};
    std::uint64_t panOverrideBits = 0;
    std::uint64_t maskBits = 0;
};

[[nodiscard]] PartialsBlock decodePartialsBlock(const char* base) {
    PartialsBlock b{};
    constexpr std::size_t kPanBytes = 64 * sizeof(float);
    std::memcpy(b.pan.data(), base, kPanBytes);
    std::memcpy(&b.panOverrideBits, base + kPanBytes, sizeof(std::uint64_t));
    std::memcpy(&b.maskBits, base + kPanBytes + sizeof(std::uint64_t), sizeof(std::uint64_t));
    return b;
}

/// The [partials] block of a full Phase 11 stream.
[[nodiscard]] PartialsBlock partialsBlockOf(MemoryStream& s) {
    REQUIRE(s.getSize() == kV3StateBytes);
    return decodePartialsBlock(s.getData() + kEffectsEndBytes);
}

/// Every field of the block absent - what a stream that carries no [partials]
/// block at all must produce on the next save.
void requirePartialsBlockAbsent(const PartialsBlock& got) {
    CHECK(got.panOverrideBits == 0u);
    CHECK(got.maskBits == 0u);
    for (std::size_t i = 0; i < got.pan.size(); ++i) {
        INFO("[partials] pan " << i);
        CHECK(got.pan[i] == 0.0f);
    }
}

// =============================================================================
// The four 541-byte SpectralState payloads, read out of a stream by offset
// =============================================================================
// Processor::getState()'s write order gives the offset:
//   4 [version] + 12 [global] + 20 [macro] + 4 [seed] + 44 [cloud]
//   + 52 [morph scalars] = 136.
constexpr int32 kSpectralPayloadOffset = 136;
constexpr int32 kSpectralPayloadBytes =
    4 * static_cast<int32>(Krate::DSP::kSpectralStateBytes);

static_assert(kSpectralPayloadOffset + kSpectralPayloadBytes + 40 + 52 + 68 + 72
                  == kV2StateBytes,
              "the payload offset must reproduce the v2 total: [life] 40 + [body] 52 "
              "+ [atmos] 68 + [aether] 72 follow the payloads");

[[nodiscard]] Krate::DSP::SpectralState slotPayloadOf(MemoryStream& s, int slot) {
    REQUIRE(s.getSize() == kV3StateBytes);
    REQUIRE(slot >= 0);
    REQUIRE(slot < 4);
    std::array<std::byte, Krate::DSP::kSpectralStateBytes> buf{};
    std::memcpy(buf.data(),
                s.getData() + kSpectralPayloadOffset
                    + (static_cast<std::ptrdiff_t>(slot)
                       * static_cast<std::ptrdiff_t>(Krate::DSP::kSpectralStateBytes)),
                buf.size());
    Krate::DSP::SpectralState out{};
    REQUIRE(Krate::DSP::deserializeSpectralState(buf.data(), buf.size(), out));
    return out;
}

// =============================================================================
// The C-5 edit wire - one HostMessage per send, byte for byte what Controller
// sends (the same helper shape integration/partial_edit_test.cpp uses)
// =============================================================================

[[nodiscard]] Seraphis::UI::EditMessage makeEdit(std::uint8_t kind, std::uint8_t slot,
                                                 std::uint16_t index, float a, float b) {
    Seraphis::UI::EditMessage m{};
    m.kind = kind;
    m.slot = slot;
    m.index = index;
    m.a = a;
    m.b = b;
    return m;
}

tresult sendEdit(Seraphis::Processor& processor, const Seraphis::UI::EditMessage& m) {
    auto message = Steinberg::owned(new Steinberg::Vst::HostMessage());
    message->setMessageID(Seraphis::UI::kSeraphisEditMessageId);
    Vst::IAttributeList* attributes = message->getAttributes();
    REQUIRE(attributes != nullptr);
    REQUIRE(attributes->setBinary(Seraphis::UI::kSeraphisEditAttributeId, &m,
                                  static_cast<uint32>(sizeof(m)))
            == kResultOk);
    return processor.notify(message);
}

/// MEMBER-WISE, never memcmp over the object representation - see the identical
/// helper in tests/integration/partial_edit_test.cpp for the reasoning: a float
/// has no unique object representation, so a byte compare asks a different
/// question from the one every call site here means. std::array's operator== is
/// element-wise, so the arrays need no loop.
[[nodiscard]] bool statesEqual(const Krate::DSP::SpectralState& a,
                               const Krate::DSP::SpectralState& b) noexcept {
    return a.ratios == b.ratios && a.amplitudes == b.amplitudes && a.name == b.name
           && a.tiltDbPerOct == b.tiltDbPerOct && a.inharmonicity == b.inharmonicity
           && a.numPartials == b.numPartials;
}

// =============================================================================
// Phase 11 T018 - the CONTROLLER-side slot mirror (SC-016, FR-035, FR-046)
// =============================================================================

/// The normalized value of factory-state index `i` on a 409-412 dropdown. The
/// registration form is addDropdownParam over kSpectralStateLabels, so the
/// inverse of detail::morphDropdownIndex is index / (count - 1).
[[nodiscard]] double slotDropdownNorm(int index) {
    return static_cast<double>(index)
           / static_cast<double>(Seraphis::kSpectralStateLabels.size() - 1);
}

/// A byte-for-byte copy of `source`, so a corruption arm can edit one payload
/// without disturbing the pristine stream the other arm reads.
[[nodiscard]] StreamPtr cloneStream(MemoryStream& source) {
    StreamPtr copy = makeStream();
    int32 written = 0;
    const auto size = static_cast<int32>(source.getSize());
    copy->write(source.getData(), size, &written);
    REQUIRE(written == size);
    rewindStream(*copy);
    return copy;
}

/// An edit that is guaranteed to STORE on any valid state: the ratio is written
/// back unchanged (so setPartial's monotone window cannot clamp it into a
/// different value) and only the amplitude moves.
[[nodiscard]] Seraphis::UI::EditMessage makeAmplitudeEdit(int slot,
                                                          const Krate::DSP::SpectralState& current,
                                                          float amplitude) {
    return makeEdit(/*kind=*/1, static_cast<std::uint8_t>(slot), /*index=*/0,
                    current.ratios[0], amplitude);
}

}  // namespace

// =============================================================================
// SC-009
// =============================================================================

TEST_CASE("Seraphis_StateVersion3_RoundTripsAndMigrates", "[seraphis][state][v3]") {
    constexpr double kSampleRate = 48000.0;
    constexpr int32 kBlock = 512;

    const EffectsBlock reference = referenceEffectsBlock();
    const EffectsBlock defaults = defaultEffectsBlock();

    // -------------------------------------------------------------------------
    // Non-vacuity for the whole case: every one of the 16 driven values really
    // does differ from its C-6 default, so nothing below can pass by writing
    // defaults (spec C-6; construction choice 1).
    // -------------------------------------------------------------------------
    SECTION("Every one of the 16 driven values differs from its C-6 default") {
        for (std::size_t k = 0; k < reference.f.size(); ++k) {
            INFO("[effects] float field " << k);
            CHECK(reference.f[k] != defaults.f[k]);
        }
        for (std::size_t k = 0; k < reference.i.size(); ++k) {
            INFO("[effects] int32 field " << k);
            CHECK(reference.i[k] != defaults.i[k]);
        }
        requireBlockIsC6Defaults(defaults);
    }

    // -------------------------------------------------------------------------
    // (a) The round trip. FR-018's dispatch rung, FR-032's save/load pair.
    // -------------------------------------------------------------------------
    SECTION("(a) All 16 effects fields survive getState -> setState exactly") {
        SeraphisTest::ProcessorFixture driven;
        driveEffects(driven, kSampleRate, kBlock);

        StreamPtr a = captureState(*driven.proc);
        REQUIRE(a->getSize() == kV3StateBytes);

        // The processor's own block must be the pack's block. This is what fails
        // when the dispatch rung is missing (the block would be `defaults`) or
        // when getState writes the effects fields in the wrong order.
        requireBlocksEqual(effectsBlockOf(*a), reference);

        Seraphis::Processor second;
        rewindStream(*a);
        REQUIRE(second.setState(a.get()) == kResultOk);

        StreamPtr b = captureState(second);
        REQUIRE(b->getSize() == kV3StateBytes);
        requireBlocksEqual(effectsBlockOf(*b), reference);

        // ...and the WHOLE stream is a fixed point, not merely the new block.
        CHECK(std::memcmp(a->getData(), b->getData(),
                          static_cast<std::size_t>(kV3StateBytes)) == 0);
    }

    // -------------------------------------------------------------------------
    // (b) + (d). One driven processor supplies both the v3 blob and, through its
    // own 2532-byte prefix, the v2 blob (construction choice 2).
    // -------------------------------------------------------------------------
    SECTION("(b) A version-2 blob loads, effects at C-6 defaults, Phase 9 fields intact") {
        SeraphisTest::ProcessorFixture driven;
        driveEffects(driven, kSampleRate, kBlock);

        StreamPtr v3 = captureState(*driven.proc);
        REQUIRE(v3->getSize() == kV3StateBytes);
        StreamPtr v2 = makeV2BlobFrom(*v3);
        REQUIRE(v2->getSize() == kV2StateBytes);

        int32 encodedVersion = 0;
        std::memcpy(&encodedVersion, v2->getData(), sizeof(int32));
        REQUIRE(encodedVersion == Seraphis::kStateVersion2);

        Seraphis::Processor migrated;
        rewindStream(*v2);
        REQUIRE(migrated.setState(v2.get()) == kResultOk);  // FR-033: MUST NOT fail

        StreamPtr out = captureState(migrated);
        REQUIRE(out->getSize() == kV3StateBytes);  // migration WIDENS

        // FR-033: all 16 effects fields at their C-6 defaults - the behaviour the
        // v2 stream already had (C-7), reached with NO version-aware branch.
        requireBlockIsC6Defaults(effectsBlockOf(*out));

        // ...and every Phase 9 field equals what the blob encodes. From offset 4:
        // the version int32 legitimately differs (2 -> 3).
        CHECK(std::memcmp(out->getData() + 4, v2->getData() + 4,
                          static_cast<std::size_t>(kV2StateBytes - 4)) == 0);
    }

    SECTION("(d) The v2 blob is a strict prefix of the v3 blob from offset 4 on") {
        SeraphisTest::ProcessorFixture driven;
        driveEffects(driven, kSampleRate, kBlock);

        StreamPtr v3 = captureState(*driven.proc);
        StreamPtr v2 = makeV2BlobFrom(*v3);

        // STRICT: the v3 stream is longer, by exactly the two APPENDED blocks -
        // Phase 10's [effects] and Phase 11's [partials].
        REQUIRE(v3->getSize() > v2->getSize());
        CHECK(v3->getSize() - v2->getSize() == kEffectsBlockBytes + kPartialsBlockBytes);

        // The payloads agree over the whole of the shorter one...
        CHECK(std::memcmp(v3->getData() + 4, v2->getData() + 4,
                          static_cast<std::size_t>(kV2StateBytes - 4)) == 0);
        // ...and the ONLY difference inside those 2532 bytes is the leading int32.
        CHECK(std::memcmp(v3->getData(), v2->getData(), 4) != 0);

        // The load-bearing half, which the construction above does NOT give for
        // free: reloading that prefix as a v2 stream reproduces every Phase 9
        // field. A build that wrote the [effects] block anywhere but LAST would
        // have a Phase 9 field cut at the 2532-byte boundary and fail here.
        Seraphis::Processor reloaded;
        rewindStream(*v2);
        REQUIRE(reloaded.setState(v2.get()) == kResultOk);
        StreamPtr out = captureState(reloaded);
        REQUIRE(out->getSize() == kV3StateBytes);
        CHECK(std::memcmp(out->getData() + 4, v3->getData() + 4,
                          static_cast<std::size_t>(kV2StateBytes - 4)) == 0);
    }

    // -------------------------------------------------------------------------
    // (c) The oldest stream this binary has ever written still loads.
    // -------------------------------------------------------------------------
    SECTION("(c) A 36-byte version-1 blob still loads") {
        StreamPtr v1 = makeV1Stream(Seraphis::kStateVersion1, 1.5f, 12, 0, 0.125f, 0.25f,
                                    0.375f, 0.8f, 0.625f);
        REQUIRE(v1->getSize() == kV1StateBytes);

        Seraphis::Processor proc;
        REQUIRE(proc.setState(v1.get()) == kResultOk);

        StreamPtr out = captureState(proc);
        REQUIRE(out->getSize() == kV3StateBytes);
        requireBlockIsC6Defaults(effectsBlockOf(*out));
    }

    // -------------------------------------------------------------------------
    // (e) Palette-widening migration (v3 -> v4). The v4 layout is BYTE-IDENTICAL
    // to v3 and the four slot selections are stored as RAW int32 indices, so the
    // v<=3 "migration" is the IDENTITY on stored data: a v3 stream must load to
    // the SAME archetypes it meant, and re-saving must reproduce the whole
    // stream byte-for-byte with only the leading int32 restamped to 4. Any
    // round(v*4)-style rescale sneaking into a loader corrupts a raw index
    // (stored 3 -> 12 -> clamp 9 = wrong archetype) and fails here.
    // The two index sets cover every pre-widening index 0..4 across the four
    // slots.
    // -------------------------------------------------------------------------
    SECTION("(e) A v3-stamped stream loads to the same archetypes, identity-migrated") {
        constexpr std::array<std::array<int, 4>, 2> kOldIndexSets = {
            {{0, 1, 2, 3},    // SineStack, Bell, Choir, Glass
             {4, 3, 2, 1}}};  // Breath, Glass, Choir, Bell

        for (const auto& oldIndices : kOldIndexSets) {
            INFO("index set { " << oldIndices[0] << ", " << oldIndices[1] << ", "
                                << oldIndices[2] << ", " << oldIndices[3] << " }");

            // Drive the four slot dropdowns, then capture a current stream.
            SeraphisTest::ProcessorFixture fx;
            REQUIRE(fx.prepare(kSampleRate, kBlock) == kResultOk);
            for (int s = 0; s < 4; ++s) {
                fx.setParam(static_cast<Vst::ParamID>(Seraphis::kMorphState0Id + s),
                            slotDropdownNorm(oldIndices[static_cast<std::size_t>(s)]));
            }
            REQUIRE(fx.processBlock(kBlock) == kResultOk);
            StreamPtr current = captureState(*fx.proc);
            REQUIRE(current->getSize() == kV3StateBytes);  // v4 layout == v3 layout

            // Restamp the leading int32 to 3. Byte-wise this IS a pre-widening
            // stream - every committed factory .vstpreset carries version 3 and
            // raw slot indices in [0, 4], exactly as built here.
            StreamPtr v3 = cloneStream(*current);
            const int32 three = Seraphis::kStateVersion3;
            std::memcpy(v3->getData(), &three, sizeof(int32));

            Seraphis::Processor migrated;
            rewindStream(*v3);
            REQUIRE(migrated.setState(v3.get()) == kResultOk);

            // The four slots decode to the SAME archetypes the v3 stream meant.
            for (int s = 0; s < 4; ++s) {
                INFO("slot " << s);
                CHECK(statesEqual(migrated.spectralAuthoringSlotForTest(s),
                                  Krate::DSP::makeFactoryState(
                                      static_cast<Krate::DSP::SpectralStateId>(
                                          oldIndices[static_cast<std::size_t>(s)]))));
            }

            // Re-saving reproduces the original v4 stream byte-for-byte: the
            // migration is the identity on every stored byte, and the version
            // restamp (3 -> 4) is the ONLY difference from the input.
            StreamPtr out = captureState(migrated);
            REQUIRE(out->getSize() == kV3StateBytes);
            CHECK(std::memcmp(out->getData(), current->getData(),
                              static_cast<std::size_t>(kV3StateBytes)) == 0);
            CHECK(std::memcmp(out->getData(), v3->getData(), 4) != 0);
            CHECK(std::memcmp(out->getData() + 4, v3->getData() + 4,
                              static_cast<std::size_t>(kV3StateBytes - 4)) == 0);

            // Controller half: the same v3 stream replays each 409-412 dropdown
            // at index / 9 - the identity re-encode over the widened list.
            Seraphis::Controller controller;
            REQUIRE(controller.initialize(nullptr) == kResultOk);
            rewindStream(*v3);
            REQUIRE(controller.setComponentState(v3.get()) == kResultOk);
            for (int s = 0; s < 4; ++s) {
                INFO("slot " << s);
                CHECK(controller.getParamNormalized(
                          static_cast<Vst::ParamID>(Seraphis::kMorphState0Id + s))
                      == slotDropdownNorm(oldIndices[static_cast<std::size_t>(s)]));
            }
            REQUIRE(controller.terminate() == kResultOk);
        }
    }

    // -------------------------------------------------------------------------
    // FR-034, as far as T008 ships (construction choice 3). A setState() that
    // arrives AFTER setupProcessing() must reach the DSP, not merely the atomics:
    // pushAllSurfaces() invalidates the composed-saturation tracker, so the next
    // process() re-pushes it.
    //
    // This is also the D-2 single-writer assertion: 0.80 is what ID 1400 says,
    // and the value pushGlobalParams() used to install unconditionally was
    // SeraphisEngine::kOutputSaturation = 0.15f.
    // -------------------------------------------------------------------------
    SECTION("FR-034: a preset loaded after prepare re-pushes the composed saturation") {
        SeraphisTest::ProcessorFixture driven;
        driveEffects(driven, kSampleRate, kBlock);
        StreamPtr preset = captureState(*driven.proc);

        SeraphisTest::ProcessorFixture target;
        REQUIRE(target.prepare(kSampleRate, kBlock) == kResultOk);
        REQUIRE(target.processBlock(kBlock) == kResultOk);

        Krate::DSP::SeraphisEngine* engine = target.proc->engineForTest();
        REQUIRE(engine != nullptr);
        // Precondition: the prepared processor is at the C-6 default amount,
        // which by C-7 is the engine's own kOutputSaturation.
        REQUIRE(engine->getOutputSaturation() == Seraphis::kFxSaturationDefault);
        REQUIRE(Seraphis::kFxSaturationDefault
                == Krate::DSP::SeraphisEngine::kOutputSaturation);

        rewindStream(*preset);
        REQUIRE(target.proc->setState(preset.get()) == kResultOk);
        REQUIRE(target.processBlock(kBlock) == kResultOk);

        CHECK(engine->getOutputSaturation() == 0.80f);
    }

    // -------------------------------------------------------------------------
    // T022. FR-034 on the COMPONENTS. Everything above this line - including the
    // saturation SECTION - reads a value back out of the EffectsParams atomics or
    // out of a re-serialized stream, so a build whose setState() fills the atomics
    // and never re-pushes them passes all of it while the loaded patch renders on
    // the prepare-time values. These two arms are the ones that fail for such a
    // build: they read the DSP COMPONENTS the send stage installs into.
    //
    // The push path under test is pushAllSurfaces() (processor.cpp:2796), whose
    // FR-034 block clears lastPushedSaturationValid_, lastPushedFxValid_ and
    // lastPushedFreezeValid_ (:2813-2815); with lastPushedFxValid_ false,
    // pushEffectsParams()' `first` (processor.cpp:1643) is true and the WHOLE
    // registered set is delivered on the next process().
    //
    // WHY TWO ARMS, AND WHY THE SECOND ONE ZEROES IDs 1441/1443. The seven
    // SpectralDelay getters are pushed straight from the atomics, so the
    // all-16-non-default blob asserts them exactly. globalMs_'s width is NOT: the
    // wander stage pushes fxWidthBase_ + depthW * driftW * kWanderWidthSpanPercent
    // (processor.cpp:2155), so with a non-zero wander depth the installed width is
    // the loaded width plus an unobservable drift term and no exact comparison is
    // available. Holding IDs 1441 and 1443 at their C-6 default of 0 makes that
    // term identically zero - the loaded 1440 IS the pushed target - while ID 1440
    // itself stays non-default, so the wander predicate (processor.cpp:1053-1055)
    // still evaluates true and the stage still runs. The two rows dropped are
    // exactly the two the push path under test does not touch: they are class-(b)
    // smoothers targeted in setParamSmootherTargets(), not pushEffectsParams()
    // (see that function's "IDs 1410, 1440, 1441 and 1443 are DELIBERATELY
    // ABSENT", processor.cpp:1582-1586).
    // -------------------------------------------------------------------------
    SECTION("FR-034: a preset loaded after prepare re-pushes the effects surface to the DSP") {
        constexpr auto kRelaxed = std::memory_order_relaxed;

        // --- arm 1: the seven SpectralDelay getters, all 16 rows non-default ---
        Seraphis::EffectsParams expected{};
        applyDrivenRows(expected, /*quietWander=*/false);

        SeraphisTest::ProcessorFixture driven;
        driveEffects(driven, kSampleRate, kBlock);
        StreamPtr preset = captureState(*driven.proc);
        REQUIRE(preset->getSize() == kV3StateBytes);

        SeraphisTest::ProcessorFixture target;
        REQUIRE(target.prepare(kSampleRate, kBlock) == kResultOk);
        REQUIRE(target.processBlock(kBlock) == kResultOk);

        const Krate::DSP::SpectralDelay& delay = spectralDelayOf(*target.proc);

        // The expectations, and the PRECONDITION each one is measured against.
        // The precondition is the C-6 default the prepare-time push installed -
        // which is also the value a build that never re-pushes would still be
        // holding after the load, so asserting `want != got-before` per row is
        // what makes each CHECK below non-vacuous.
        const float wantDelayMs = expected.delayTimeMs.load(kRelaxed);
        const float wantFeedback = Seraphis::tiltCompensatedFeedback(
            expected.delayFeedback.load(kRelaxed), expected.delayTilt.load(kRelaxed));
        const float wantDiffusion = expected.delayDiffusion.load(kRelaxed);
        const float wantStereoWidth = expected.delayWidth.load(kRelaxed);
        const int wantSpreadDirection = expected.spreadDirection.load(kRelaxed);
        const bool wantSync = expected.delaySync.load(kRelaxed);
        const int wantNoteValue = expected.delaySyncNote.load(kRelaxed);

        REQUIRE(delay.getBaseDelayMs() == Seraphis::kFxDelayTimeDefault);
        REQUIRE(delay.getFeedback()
                == Seraphis::tiltCompensatedFeedback(Seraphis::kFxDelayFeedbackDefault,
                                                     Seraphis::kFxDelayTiltDefault));
        REQUIRE(delay.getDiffusion() == Seraphis::kFxDelayDiffusionDefault);
        REQUIRE(delay.getStereoWidth() == Seraphis::kFxDelayWidthDefault);
        REQUIRE(static_cast<int>(delay.getSpreadDirection())
                == Seraphis::kFxDelaySpreadDirectionDefault);
        REQUIRE(static_cast<int>(delay.getTimeMode())
                == static_cast<int>(Krate::DSP::TimeMode::Free));
        REQUIRE(delay.getNoteValue() == Seraphis::kFxDelaySyncNoteDefault);

        // Non-vacuity, per row: every loaded value differs from the prepare-time
        // one asserted immediately above.
        REQUIRE(wantDelayMs != Seraphis::kFxDelayTimeDefault);
        REQUIRE(wantFeedback != delay.getFeedback());
        REQUIRE(wantDiffusion != Seraphis::kFxDelayDiffusionDefault);
        REQUIRE(wantStereoWidth != Seraphis::kFxDelayWidthDefault);
        REQUIRE(wantSpreadDirection != Seraphis::kFxDelaySpreadDirectionDefault);
        REQUIRE(wantSync != Seraphis::kFxDelaySyncDefault);
        REQUIRE(wantNoteValue != Seraphis::kFxDelaySyncNoteDefault);

        rewindStream(*preset);
        REQUIRE(target.proc->setState(preset.get()) == kResultOk);
        REQUIRE(target.processBlock(kBlock) == kResultOk);

        CHECK(delay.getBaseDelayMs() == wantDelayMs);
        // FR-016a: what reaches the component is the COMPENSATED feedback, so the
        // expectation is the pack's own helper over the two loaded fields - never
        // the raw 1414 value, which the component must never be given.
        CHECK(delay.getFeedback() == wantFeedback);
        CHECK(delay.getDiffusion() == wantDiffusion);
        CHECK(delay.getStereoWidth() == wantStereoWidth);
        CHECK(static_cast<int>(delay.getSpreadDirection())
              == static_cast<int>(Seraphis::toSpreadDirection(wantSpreadDirection)));
        CHECK(static_cast<int>(delay.getTimeMode())
              == static_cast<int>(wantSync ? Krate::DSP::TimeMode::Synced
                                           : Krate::DSP::TimeMode::Free));
        CHECK(delay.getNoteValue() == wantNoteValue);

        // --- arm 2: globalMs_'s width, with the two wander depths held at 0 ----
        Seraphis::EffectsParams expectedQuiet{};
        applyDrivenRows(expectedQuiet, /*quietWander=*/true);

        SeraphisTest::ProcessorFixture drivenQuiet;
        driveEffects(drivenQuiet, kSampleRate, kBlock, /*quietWander=*/true);
        StreamPtr quietPreset = captureState(*drivenQuiet.proc);
        REQUIRE(quietPreset->getSize() == kV3StateBytes);

        SeraphisTest::ProcessorFixture widthTarget;
        REQUIRE(widthTarget.prepare(kSampleRate, kBlock) == kResultOk);
        REQUIRE(widthTarget.processBlock(kBlock) == kResultOk);

        // Precondition: at the C-6 defaults the wander stage is skipped entirely
        // (processor.cpp:1053-1055, :2090), so globalMs_ still holds the width
        // setupProcessing() gave it - kFxWidthDefault, which IS kDefaultWidth.
        const float wantWidth = expectedQuiet.width.load(kRelaxed);
        REQUIRE(globalMsOf(*widthTarget.proc).getWidth()
                == Krate::DSP::MidSideProcessor::kDefaultWidth);
        REQUIRE(wantWidth != Krate::DSP::MidSideProcessor::kDefaultWidth);

        rewindStream(*quietPreset);
        REQUIRE(widthTarget.proc->setState(quietPreset.get()) == kResultOk);
        REQUIRE(widthTarget.processBlock(kBlock) == kResultOk);

        CHECK(globalMsOf(*widthTarget.proc).getWidth() == wantWidth);
    }

    // -------------------------------------------------------------------------
    // The other half of D-2: the gate. Toggling kSoftLimitId must not revert the
    // amount ID 1400 installed - the defect two independent writers on one setter
    // produced (spec FR-021, plan D-2).
    // -------------------------------------------------------------------------
    SECTION("D-2: kSoftLimitId gates the amount ID 1400 supplies, and never reverts it") {
        SeraphisTest::ProcessorFixture fx;
        REQUIRE(fx.prepare(kSampleRate, kBlock) == kResultOk);

        Krate::DSP::SeraphisEngine* engine = fx.proc->engineForTest();
        REQUIRE(engine != nullptr);

        fx.setParam(Seraphis::kFxSaturationId, 0.80);
        REQUIRE(fx.processBlock(kBlock) == kResultOk);
        CHECK(engine->getOutputSaturation() == 0.80f);

        // Gate OFF -> no output saturation at all.
        fx.setParam(Seraphis::kSoftLimitId, 0.0);
        REQUIRE(fx.processBlock(kBlock) == kResultOk);
        CHECK(engine->getOutputSaturation() == 0.0f);

        // Gate back ON -> the amount ID 1400 supplies, NOT kOutputSaturation.
        fx.setParam(Seraphis::kSoftLimitId, 1.0);
        REQUIRE(fx.processBlock(kBlock) == kResultOk);
        CHECK(engine->getOutputSaturation() == 0.80f);
    }
}

// =============================================================================
// SC-015 / FR-034 / FR-034a - Phase 11 (T012)
// =============================================================================
// The three things a Phase 11 edit puts in the stream, and the ONE thing that
// must not move:
//
//   * a slot payload edit  -> already carried, by Phase 9's full 541-byte
//                             payload serialization. Nothing new is needed for
//                             it, which is exactly why the format version does
//                             not move (spec Overview fact 5).
//   * a pan override       -> the new [partials] block.
//   * a mask              -> the new [partials] block.
//   * kCurrentStateVersion -> stayed 3 through Phase 11: the block is an
//                             APPEND, read back by an EOF-safe loader with NO
//                             version-aware branch, so an older stream runs out
//                             before it and an older binary ignores the tail.
//                             (The palette widening later moved it to 4 for
//                             forward-protection only - the LAYOUT is still
//                             byte-identical to v3, which is what this case
//                             continues to assert.)
//
// Everything is driven through the REAL C-5 message path (Processor::notify),
// never by writing the processor's tables directly: a round trip that only
// asserts about a back door says nothing about what a user's project saves.
//
// FR-094 (getState -> setState -> getState is byte-identical) survives by
// construction, because every field of the block is a STORED VALUE and never an
// arithmetic result - the same argument Phase 9's [morph] payload uses. It is
// asserted here rather than assumed, and NO carve-out is taken for the block.
// =============================================================================

TEST_CASE("Seraphis_EditedState_RoundTripsAtV3", "[seraphis][state][phase11]") {
    // C-5's kind numbers (ui/edit_message.h's table).
    constexpr std::uint8_t kPartialRatioAmp = 1;
    constexpr std::uint8_t kPartialPan = 2;
    constexpr std::uint8_t kPartialMask = 3;

    constexpr std::uint8_t kEditedSlot = 1;
    constexpr std::uint16_t kPanPartial = 5;
    constexpr std::uint16_t kMaskPartial = 9;
    constexpr float kAuthoredPan = 0.8f;
    // A perfect fifth on partial 0. The index is 0 deliberately: setPartial()'s
    // monotone window caps the upper edge at ratios[1] / kAuthorSpacing, and
    // index 0 is the one index at which 1.5 is inside it for every factory
    // state, so the edit STORES rather than clamping to a value the "the edit
    // survived" comparison could not distinguish from the original.
    constexpr float kEditedRatio = 1.5f;

    Seraphis::Processor source;

    // The pre-edit slot, so "the edit survived the round trip" is distinguishable
    // from "the slot was already like that".
    const Krate::DSP::SpectralState before = source.spectralAuthoringSlotForTest(kEditedSlot);

    REQUIRE(sendEdit(source, makeEdit(kPartialRatioAmp, kEditedSlot, /*index=*/0, kEditedRatio,
                                      before.amplitudes[0]))
            == kResultOk);
    REQUIRE(sendEdit(source, makeEdit(kPartialPan, /*slot=*/0, kPanPartial, kAuthoredPan, 0.0f))
            == kResultOk);
    REQUIRE(sendEdit(source, makeEdit(kPartialMask, /*slot=*/0, kMaskPartial, 1.0f, 0.0f))
            == kResultOk);

    const Krate::DSP::SpectralState edited = source.spectralAuthoringSlotForTest(kEditedSlot);

    // NON-VACUITY. The mutator really did store; otherwise every assertion below
    // would pass on a build whose edit channel dropped the message.
    REQUIRE(!statesEqual(before, edited));
    REQUIRE(edited.ratios[0] == kEditedRatio);
    REQUIRE(source.editStageWriteCountForTest() == 1u);

    StreamPtr a = captureState(source);
    REQUIRE(a->getSize() == kV3StateBytes);

    SECTION("The format version is the current one, and the block is appended LAST") {
        int32 encodedVersion = 0;
        std::memcpy(&encodedVersion, a->getData(), sizeof(int32));
        CHECK(encodedVersion == Seraphis::kCurrentStateVersion);
        // The independent literal copy: 4 since the palette widening
        // (plugin_ids.h - layout unchanged, 2868 B, forward-protection only).
        CHECK(encodedVersion == 4);

        // The [effects] block is still where Phase 10 put it - i.e. [partials]
        // went AFTER it, not into the middle of the chain. Nothing drove an
        // effects ID here, so it must decode as the C-6 default column.
        requireBlockIsC6Defaults(effectsBlockOf(*a));

        const PartialsBlock block = partialsBlockOf(*a);
        CHECK(block.panOverrideBits == (std::uint64_t{1} << kPanPartial));
        CHECK(block.maskBits == (std::uint64_t{1} << kMaskPartial));
        CHECK(block.pan[kPanPartial] == kAuthoredPan);
        for (std::size_t i = 0; i < block.pan.size(); ++i) {
            if (i == static_cast<std::size_t>(kPanPartial)) {
                continue;
            }
            INFO("[partials] un-authored pan " << i);
            CHECK(block.pan[i] == 0.0f);
        }

        // The slot payload, decoded from the SAME stream at Phase 9's offset.
        CHECK(statesEqual(slotPayloadOf(*a, kEditedSlot), edited));
    }

    SECTION("getState -> setState -> getState is byte-identical (FR-094)") {
        Seraphis::Processor second;
        rewindStream(*a);
        REQUIRE(second.setState(a.get()) == kResultOk);

        StreamPtr b = captureState(second);
        REQUIRE(b->getSize() == kV3StateBytes);

        // The WHOLE stream is a fixed point - the block included, with no
        // FR-094 carve-out.
        CHECK(std::memcmp(a->getData(), b->getData(),
                          static_cast<std::size_t>(kV3StateBytes))
              == 0);

        // ...and the three edited quantities really came back, read out of the
        // reloaded processor rather than only out of its re-serialized bytes.
        const PartialsBlock reloaded = partialsBlockOf(*b);
        CHECK(reloaded.panOverrideBits == (std::uint64_t{1} << kPanPartial));
        CHECK(reloaded.maskBits == (std::uint64_t{1} << kMaskPartial));
        CHECK(reloaded.pan[kPanPartial] == kAuthoredPan);
        CHECK(statesEqual(second.spectralAuthoringSlotForTest(kEditedSlot), edited));
    }

    SECTION("A stream truncated immediately before the block still loads, overrides absent") {
        // The strict prefix through [effects] - byte-wise what a Phase 10
        // binary wrote, except the leading int32 now reads 4 (palette
        // widening). The loader has no version-aware branch, so the overrides
        // are absent by running out of stream - the whole point of FR-034a.
        StreamPtr truncated = makeStream();
        int32 written = 0;
        truncated->write(a->getData(), kEffectsEndBytes, &written);
        REQUIRE(written == kEffectsEndBytes);
        rewindStream(*truncated);

        int32 encodedVersion = 0;
        std::memcpy(&encodedVersion, truncated->getData(), sizeof(int32));
        REQUIRE(encodedVersion == Seraphis::kCurrentStateVersion);

        Seraphis::Processor third;
        REQUIRE(third.setState(truncated.get()) == kResultOk);  // MUST NOT fail

        StreamPtr out = captureState(third);
        REQUIRE(out->getSize() == kV3StateBytes);  // the save WIDENS

        // Every override absent - reached by running out of stream, not by a
        // version branch.
        requirePartialsBlockAbsent(partialsBlockOf(*out));

        // ...and everything the truncated stream DID carry is intact, including
        // the slot payload edit, which needs no block of its own.
        CHECK(std::memcmp(out->getData() + 4, truncated->getData() + 4,
                          static_cast<std::size_t>(kEffectsEndBytes - 4))
              == 0);
        CHECK(statesEqual(slotPayloadOf(*out, kEditedSlot), edited));
    }
}

// =============================================================================
// SC-016 arm 1 (T018) - FR-035: a slot dropdown discards THAT slot's edits only
// =============================================================================
// Two halves, because the criterion names two owners:
//
//   PROCESSOR - spectralSlotsAuthoring_ is reconciled with the four dropdowns
//               lazily (Processor::syncAuthoringMirrorFromDropdowns, called from
//               getState() and from every edit message), so the observable is
//               the saved payload plus the authoring seam.
//   CONTROLLER - slotMirror_ is re-seeded from makeFactoryState() on the SAME
//               dropdown move (FR-046 source 1), through the SAME factory
//               function, which is what keeps the display and the audio from
//               disagreeing about what "Breath" means.
//
// The edit used here moves ONLY the amplitude: setPartial writes the ratio back
// unchanged, so the monotone window cannot clamp the stored value into something
// the "the edit survived" comparison could not distinguish from the original.
// =============================================================================

TEST_CASE("Seraphis_SlotDropdown_DiscardsOnlyThatSlot", "[seraphis][state][phase11]") {
    constexpr double kSampleRate = 48000.0;
    constexpr int32 kBlock = 512;

    // Slot 0's registered default is SineStack (index 0) and slot 1's is Glass
    // (index 3) - kMorphSlotDefaultIndices, morph_params.h. The move below picks
    // Breath (index 4), which is neither, so "it reloaded" is distinguishable
    // from "it never moved".
    constexpr int kNewSlot0Index = 4;
    static_assert(Seraphis::kMorphSlotDefaultIndices[0] != kNewSlot0Index,
                  "SC-016: the dropdown must MOVE, or the discard is untested");

    const Krate::DSP::SpectralState breath =
        Krate::DSP::makeFactoryState(Krate::DSP::SpectralStateId::Breath);
    const Krate::DSP::SpectralState glass =
        Krate::DSP::makeFactoryState(Krate::DSP::SpectralStateId::Glass);

    // -------------------------------------------------------------------------
    // Processor half
    // -------------------------------------------------------------------------
    SeraphisTest::ProcessorFixture fx;
    REQUIRE(fx.prepare(kSampleRate, kBlock) == kResultOk);

    const Krate::DSP::SpectralState before0 = fx.proc->spectralAuthoringSlotForTest(0);
    const Krate::DSP::SpectralState before1 = fx.proc->spectralAuthoringSlotForTest(1);

    REQUIRE(sendEdit(*fx.proc, makeAmplitudeEdit(0, before0, 0.125f)) == kResultOk);
    REQUIRE(sendEdit(*fx.proc, makeAmplitudeEdit(1, before1, 0.375f)) == kResultOk);

    const Krate::DSP::SpectralState edited0 = fx.proc->spectralAuthoringSlotForTest(0);
    const Krate::DSP::SpectralState edited1 = fx.proc->spectralAuthoringSlotForTest(1);

    // NON-VACUITY: both slots really were edited before the discard.
    REQUIRE(!statesEqual(before0, edited0));
    REQUIRE(!statesEqual(before1, edited1));
    REQUIRE(edited0.amplitudes[0] == 0.125f);
    REQUIRE(edited1.amplitudes[0] == 0.375f);

    // Move ID 409 only.
    fx.setParam(Seraphis::kMorphState0Id, slotDropdownNorm(kNewSlot0Index));
    REQUIRE(fx.processBlock(kBlock) == kResultOk);

    StreamPtr saved = captureState(*fx.proc);  // getState() reconciles first
    REQUIRE(saved->getSize() == kV3StateBytes);

    CHECK(statesEqual(fx.proc->spectralAuthoringSlotForTest(0), breath));
    CHECK(statesEqual(slotPayloadOf(*saved, 0), breath));
    // ...and slot 1 is BYTE-IDENTICAL to what it was edited to. A discard that
    // walked all four slots would have reset this one to Glass.
    CHECK(statesEqual(fx.proc->spectralAuthoringSlotForTest(1), edited1));
    CHECK(statesEqual(slotPayloadOf(*saved, 1), edited1));
    CHECK(!statesEqual(slotPayloadOf(*saved, 1), glass));

    // -------------------------------------------------------------------------
    // Controller half (FR-046 source 1)
    // -------------------------------------------------------------------------
    Seraphis::Controller controller;
    REQUIRE(controller.initialize(nullptr) == kResultOk);

    // The mirror starts on the REGISTERED defaults, from the same factory table.
    REQUIRE(statesEqual(controller.slotMirror(1), glass));

    // Author into the mirror so "the dropdown discarded it" is observable.
    Krate::DSP::SpectralState authored = controller.slotMirror(0);
    Krate::DSP::setPartial(authored, 0, authored.ratios[0], 0.125f);
    controller.setSlotMirror(0, authored);
    REQUIRE(!statesEqual(controller.slotMirror(0), breath));
    REQUIRE(!statesEqual(controller.slotMirror(0),
                         Krate::DSP::makeFactoryState(Krate::DSP::SpectralStateId::SineStack)));

    controller.setParamNormalized(Seraphis::kMorphState0Id, slotDropdownNorm(kNewSlot0Index));

    CHECK(statesEqual(controller.slotMirror(0), breath));
    // Only that slot: the other three are untouched by the move.
    CHECK(statesEqual(controller.slotMirror(1), glass));

    SECTION("a dropdown moved to a NEW index (>= 5) re-seeds from the new archetype") {
        // Palette widening: indices 5-9 are the appended factory states. The
        // same makeFactoryState path must serve them on BOTH halves - a clamp
        // left at the old bound of 4 would silently re-seed Breath instead.
        constexpr int kNewArchetypeIndex = 7;  // Organ
        static_assert(kNewArchetypeIndex >= 5,
                      "this arm exists to cover the widened index range");
        const Krate::DSP::SpectralState organ =
            Krate::DSP::makeFactoryState(Krate::DSP::SpectralStateId::Organ);

        // Processor half: move ID 410 (slot 1, currently carrying an edit).
        fx.setParam(Seraphis::kMorphState1Id, slotDropdownNorm(kNewArchetypeIndex));
        REQUIRE(fx.processBlock(kBlock) == kResultOk);
        StreamPtr saved2 = captureState(*fx.proc);
        CHECK(statesEqual(fx.proc->spectralAuthoringSlotForTest(1), organ));
        CHECK(statesEqual(slotPayloadOf(*saved2, 1), organ));
        // ...and slot 0 (re-seeded to Breath by the main body's move) is not
        // walked by this one.
        CHECK(statesEqual(fx.proc->spectralAuthoringSlotForTest(0), breath));

        // Controller half: the mirror re-seeds from the SAME factory function.
        controller.setParamNormalized(Seraphis::kMorphState1Id,
                                      slotDropdownNorm(kNewArchetypeIndex));
        CHECK(statesEqual(controller.slotMirror(1), organ));
        CHECK(statesEqual(controller.slotMirror(0), breath));
    }

    SECTION("re-sending the SAME dropdown value does not discard an edit") {
        // FR-035 is about MOVING the dropdown. A host that re-sends the value it
        // already sent (every automation write of a parked control does) must not
        // silently wipe the user's authoring.
        Krate::DSP::SpectralState again = controller.slotMirror(0);
        Krate::DSP::setPartial(again, 0, again.ratios[0], 0.625f);
        controller.setSlotMirror(0, again);

        controller.setParamNormalized(Seraphis::kMorphState0Id,
                                      slotDropdownNorm(kNewSlot0Index));
        CHECK(statesEqual(controller.slotMirror(0), again));
    }

    controller.terminate();
}

// =============================================================================
// SC-016 arm 2 (T018) - FR-046 source 2: the mirror re-seeds from the STREAM
// =============================================================================
// This is the sole justification for the loadMorphParamsToController signature
// change: before it, the four 541-byte payloads went into a scratch buffer and
// were dropped, so a reloaded project showed the FACTORY states in the cloud
// view while the processor rendered the user's edited ones.
//
// Arm 2b is what pins the failure mode of the discard loop's replacement: the
// deserialize's return value is IGNORED ON PURPOSE. A rejected payload must
// leave its mirror entry BITWISE UNTOUCHED (spectral_state.h:264-265, :300-305)
// and the cursor must still advance the full 541 bytes, or the 55 parameters
// that follow are read from the wrong offset.
// =============================================================================

TEST_CASE("Seraphis_SlotMirror_ReSeedsFromTheStateStream", "[seraphis][state][phase11]") {
    constexpr double kSampleRate = 48000.0;
    constexpr int32 kBlock = 512;

    // The slot whose payload arm 2b corrupts. Its dropdown keeps its REGISTERED
    // default in the stream, so the FR-046 source-1 path is a no-op for it and
    // "byte-unchanged from its pre-load value" is a statement about the payload
    // decode alone.
    constexpr int kCorruptSlot = 2;
    static_assert(kCorruptSlot >= 0 && kCorruptSlot < 4);

    // Four parameters from the blocks that FOLLOW the payloads - one per block.
    // If the payload loop stops advancing the cursor by 4 x 541, every one of
    // these is read from the wrong offset.
    constexpr std::array<Vst::ParamID, 4> kAfterPayloads = {
        Seraphis::kLifeVoiceWidthId, Seraphis::kBodyMixId, Seraphis::kAtmosLevelId,
        Seraphis::kAetherSizeId};
    constexpr std::array<double, 4> kAfterPayloadValues = {0.3125, 0.4375, 0.5625, 0.6875};

    // -------------------------------------------------------------------------
    // Build ONE stream carrying four EDITED payloads and four driven parameters
    // -------------------------------------------------------------------------
    SeraphisTest::ProcessorFixture fx;
    REQUIRE(fx.prepare(kSampleRate, kBlock) == kResultOk);

    for (std::size_t k = 0; k < kAfterPayloads.size(); ++k) {
        fx.setParam(kAfterPayloads[k], kAfterPayloadValues[k]);
    }
    REQUIRE(fx.processBlock(kBlock) == kResultOk);

    std::array<Krate::DSP::SpectralState, 4> expected{};
    for (int slot = 0; slot < 4; ++slot) {
        const Krate::DSP::SpectralState before = fx.proc->spectralAuthoringSlotForTest(slot);
        const float amplitude = 0.1f + 0.2f * static_cast<float>(slot);
        REQUIRE(sendEdit(*fx.proc, makeAmplitudeEdit(slot, before, amplitude)) == kResultOk);
        expected[static_cast<std::size_t>(slot)] =
            fx.proc->spectralAuthoringSlotForTest(slot);
        REQUIRE(!statesEqual(before, expected[static_cast<std::size_t>(slot)]));
        REQUIRE(expected[static_cast<std::size_t>(slot)].amplitudes[0] == amplitude);
    }

    StreamPtr stream = captureState(*fx.proc);
    REQUIRE(stream->getSize() == kV3StateBytes);
    for (int slot = 0; slot < 4; ++slot) {
        INFO("stream payload " << slot);
        REQUIRE(statesEqual(slotPayloadOf(*stream, slot),
                            expected[static_cast<std::size_t>(slot)]));
    }

    SECTION("every mirror entry comes back byte-identical to the stream's payload") {
        Seraphis::Controller controller;
        REQUIRE(controller.initialize(nullptr) == kResultOk);

        rewindStream(*stream);
        REQUIRE(controller.setComponentState(stream.get()) == kResultOk);

        for (int slot = 0; slot < 4; ++slot) {
            INFO("slot " << slot);
            const Krate::DSP::SpectralState& mirror = controller.slotMirror(slot);
            CHECK(statesEqual(mirror, expected[static_cast<std::size_t>(slot)]));

            // ...and that IS deserializeSpectralState's own result, not a
            // coincidence of two factory states.
            std::array<std::byte, Krate::DSP::kSpectralStateBytes> raw{};
            std::memcpy(raw.data(),
                        stream->getData() + kSpectralPayloadOffset
                            + (static_cast<std::ptrdiff_t>(slot)
                               * static_cast<std::ptrdiff_t>(Krate::DSP::kSpectralStateBytes)),
                        raw.size());
            Krate::DSP::SpectralState decoded{};
            REQUIRE(Krate::DSP::deserializeSpectralState(raw.data(), raw.size(), decoded));
            CHECK(statesEqual(mirror, decoded));
            // NON-VACUITY: it is NOT the factory state the dropdown names.
            CHECK(!statesEqual(mirror, Krate::DSP::makeFactoryState(
                                           static_cast<Krate::DSP::SpectralStateId>(
                                               Seraphis::kMorphSlotDefaultIndices
                                                   [static_cast<std::size_t>(slot)]))));
        }

        controller.terminate();
    }

    SECTION("a corrupt payload leaves ONLY its own mirror entry, byte-unchanged") {
        StreamPtr corrupt = cloneStream(*stream);

        char* payload = corrupt->getData() + kSpectralPayloadOffset
                        + (static_cast<std::ptrdiff_t>(kCorruptSlot)
                           * static_cast<std::ptrdiff_t>(Krate::DSP::kSpectralStateBytes));
        // Offset 0 of a record is the format version (spectral_state.h:193).
        REQUIRE(static_cast<unsigned char>(payload[0])
                == static_cast<unsigned char>(Krate::DSP::kSpectralStateFormatVersion));
        payload[0] = static_cast<char>(0x7F);

        Seraphis::Controller controller;
        REQUIRE(controller.initialize(nullptr) == kResultOk);

        // The pre-load value, made distinctive so "unchanged" is an assertion
        // and not a comparison of two default-constructed states.
        Krate::DSP::SpectralState marked = controller.slotMirror(kCorruptSlot);
        Krate::DSP::setPartial(marked, 0, marked.ratios[0], 0.9375f);
        controller.setSlotMirror(kCorruptSlot, marked);
        REQUIRE(!statesEqual(marked, expected[static_cast<std::size_t>(kCorruptSlot)]));

        rewindStream(*corrupt);
        REQUIRE(controller.setComponentState(corrupt.get()) == kResultOk);

        // (i) the rejected slot: BITWISE untouched.
        CHECK(statesEqual(controller.slotMirror(kCorruptSlot), marked));

        // (ii) the other three still loaded.
        for (int slot = 0; slot < 4; ++slot) {
            if (slot == kCorruptSlot) {
                continue;
            }
            INFO("slot " << slot);
            CHECK(statesEqual(controller.slotMirror(slot),
                              expected[static_cast<std::size_t>(slot)]));
        }

        // (iii) the cursor still advanced 541 bytes for the rejected record, so
        // the parameters that follow the payloads read from the right offset.
        //
        // The reference is a SECOND controller loading the CLEAN stream, not a
        // transcription of the normalized values driven above: several of these
        // IDs are log-mapped, so a hand-written expectation would be asserting
        // about the pack's mapping arithmetic instead of about the cursor. Two
        // loads of the same bytes are compared with ==, which is exact.
        Seraphis::Controller reference;
        REQUIRE(reference.initialize(nullptr) == kResultOk);
        std::array<double, 4> registeredDefaults{};
        for (std::size_t k = 0; k < kAfterPayloads.size(); ++k) {
            registeredDefaults[k] = reference.getParamNormalized(kAfterPayloads[k]);
        }
        rewindStream(*stream);
        REQUIRE(reference.setComponentState(stream.get()) == kResultOk);

        for (std::size_t k = 0; k < kAfterPayloads.size(); ++k) {
            INFO("parameter " << kAfterPayloads[k]);
            const double loaded = reference.getParamNormalized(kAfterPayloads[k]);
            // NON-VACUITY: the stream really does carry a non-default here, so a
            // build that read garbage - or nothing - cannot match by accident.
            REQUIRE(loaded != registeredDefaults[k]);
            CHECK(controller.getParamNormalized(kAfterPayloads[k]) == loaded);
        }
        reference.terminate();

        controller.terminate();
    }
}
