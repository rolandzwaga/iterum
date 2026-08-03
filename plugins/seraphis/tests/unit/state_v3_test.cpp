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

#include "parameters/dropdown_mappings.h"
#include "parameters/effects_params.h"
#include "plugin_ids.h"
#include "seraphis_test_fixture.h"

#include "base/source/fstreamer.h"
#include "public.sdk/source/common/memorystream.h"

#include <krate/dsp/core/note_value.h>
#include <krate/dsp/effects/spectral_delay.h>
#include <krate/dsp/processors/midside_processor.h>
#include <krate/dsp/systems/seraphis_engine.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <atomic>
#include <cstddef>
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
/// C-8's [effects] block: 12 floats + 4 int32, appended LAST.
constexpr int32 kEffectsBlockBytes = 64;
/// Phase 10: 2532 + 64.
constexpr int32 kV3StateBytes = 2596;

static_assert(12 * 4 + 4 * 4 == kEffectsBlockBytes,
              "SC-009: C-8's [effects] block is 12 floats + 4 int32 = 64 bytes");
static_assert(kV2StateBytes + kEffectsBlockBytes == kV3StateBytes,
              "SC-009: the v3 stream is the v2 stream plus the 64-byte [effects] block");
static_assert(85 * 4 + 22 * 4 + 4 + 4 * 541 == kV3StateBytes,
              "SC-009: the v3 arithmetic must reproduce 2596");

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

        // STRICT: the v3 stream is longer, by exactly the [effects] block.
        REQUIRE(v3->getSize() > v2->getSize());
        CHECK(v3->getSize() - v2->getSize() == kEffectsBlockBytes);

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
