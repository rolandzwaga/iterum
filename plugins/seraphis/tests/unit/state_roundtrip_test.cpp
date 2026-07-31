// ==============================================================================
// Seraphis - State round-trip tests (T019)
// ==============================================================================
// FR-041, FR-045, FR-046, FR-047 -> SC-010.
//
// Includes BOTH sides on purpose: processor.h and controller.h do not include
// each other, so the VST3 separation still holds.
//
// HOW THE EIGHT PARAMETERS ARE SEEDED, and why it is not vacuous.
// The obvious seeding route -- an IParameterChanges queue through process() --
// runs through Processor::processParameterChanges(), which is owned by T020 and
// does not exist yet. This file therefore authors the seed DIRECTLY, as a
// byte stream in the FIXED layout of plan 3.4, and feeds it through setState().
// That is not circular: every section asserts that the stream getState() then
// produces decodes to the seeded NON-DEFAULT values. A setState() that ignored
// its stream (or a getState() that wrote constants) would leave the registered
// defaults in place -- 1.0f / 8 / on / 0,0,0,0.5,0 -- and every decode check
// below would fail. The byte-identity check alone would pass for such an
// implementation, which is exactly why it is never the only assertion here.
//
// The memcmp over the serialized state is the SANCTIONED form of pinning:
// tools/lint-float-bit-goldens.js explicitly exempts digests over a serialized
// byte stream (stored values walked as char data), and forbids only digests
// over rendered audio (FR-068). No render is fingerprinted in this file.
// ==============================================================================

#include "controller/controller.h"
#include "processor/processor.h"

#include "plugin_ids.h"

#include "base/source/fstreamer.h"
#include "public.sdk/source/common/memorystream.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstring>
#include <memory>

using namespace Steinberg;
using Catch::Approx;

namespace {

// -----------------------------------------------------------------------------
// The stream layout (plan 3.4). Total 36 bytes, little-endian IBStreamer.
//
//   0 int32 version | 4 float masterGain | 8 int32 polyphony |
//  12 int32 softLimit | 16 dream | 20 bloom | 24 dissolve | 28 gravity |
//  32 entropy
// -----------------------------------------------------------------------------
constexpr int32 kStateBytes = 36;

constexpr int32 kOffMasterGain = 4;
constexpr int32 kOffPolyphony = 8;
constexpr int32 kOffSoftLimit = 12;
constexpr int32 kOffDream = 16;
constexpr int32 kOffBloom = 20;
constexpr int32 kOffDissolve = 24;
constexpr int32 kOffGravity = 28;
constexpr int32 kOffEntropy = 32;

// The DEFAULT member initializers are the registered defaults: GlobalParams
// (masterGain 1.0f, polyphony 8, softLimit true) and MacroParams
// (0, 0, 0, gravity 0.5f, 0). Gravity is the FR-041 tripwire: a
// value-initialized MacroParams would stream 0.0f here.
struct StatePayload {
    int32 version = Seraphis::kCurrentStateVersion;
    float masterGain = 1.0f;
    int32 polyphony = 8;
    int32 softLimit = 1;
    float dream = 0.0f;
    float bloom = 0.0f;
    float dissolve = 0.0f;
    float gravity = 0.5f;
    float entropy = 0.0f;
};

// Eight DISTINCT non-default values. Every one differs from the registered
// default above, and no two share a value, so a loader that mixed two fields up
// (or loaded the packs in the wrong order) cannot pass by coincidence.
// The chosen floats are all exactly representable, so the round trip through
// the stream is exact and `==` is legitimate here (these are stored values, not
// computed ones).
[[nodiscard]] StatePayload nonDefaultPayload() {
    StatePayload p;
    p.masterGain = 1.5f;  // normalized 0.75   (default 1.0f -> 0.5)
    p.polyphony = 12;     // normalized 11/15  (default 8)
    p.softLimit = 0;      // normalized 0.0    (default on)
    p.dream = 0.125f;
    p.bloom = 0.25f;
    p.dissolve = 0.375f;
    p.gravity = 0.8f;  // deliberately AWAY from the 0.5 default (FR-047)
    p.entropy = 0.625f;
    return p;
}

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

// A hand-authored state stream in the fixed layout, positioned at 0.
[[nodiscard]] StreamPtr makeStateStream(const StatePayload& p) {
    StreamPtr s = makeStream();
    {
        IBStreamer w(s.get(), kLittleEndian);
        w.writeInt32(p.version);
        w.writeFloat(p.masterGain);
        w.writeInt32(p.polyphony);
        w.writeInt32(p.softLimit);
        w.writeFloat(p.dream);
        w.writeFloat(p.bloom);
        w.writeFloat(p.dissolve);
        w.writeFloat(p.gravity);
        w.writeFloat(p.entropy);
    }
    rewindStream(*s);
    return s;
}

// The first `n` bytes of `full`, as an independent stream positioned at 0.
[[nodiscard]] StreamPtr makeTruncatedStream(MemoryStream& full, int32 n) {
    StreamPtr s = makeStream();
    if (n > 0) {
        int32 written = 0;
        s->write(full.getData(), n, &written);
        REQUIRE(written == n);
    }
    rewindStream(*s);
    return s;
}

// Decode a complete 36-byte state stream.
[[nodiscard]] StatePayload decodeState(MemoryStream& s) {
    REQUIRE(s.getSize() == kStateBytes);
    rewindStream(s);

    StatePayload p{};
    IBStreamer r(&s, kLittleEndian);
    REQUIRE(r.readInt32(p.version));
    REQUIRE(r.readFloat(p.masterGain));
    REQUIRE(r.readInt32(p.polyphony));
    REQUIRE(r.readInt32(p.softLimit));
    REQUIRE(r.readFloat(p.dream));
    REQUIRE(r.readFloat(p.bloom));
    REQUIRE(r.readFloat(p.dissolve));
    REQUIRE(r.readFloat(p.gravity));
    REQUIRE(r.readFloat(p.entropy));
    return p;
}

[[nodiscard]] StreamPtr captureState(Seraphis::Processor& proc) {
    StreamPtr s = makeStream();
    REQUIRE(proc.getState(s.get()) == kResultOk);
    rewindStream(*s);
    return s;
}

void checkPayload(const StatePayload& actual, const StatePayload& expected) {
    CHECK(actual.version == expected.version);
    CHECK(actual.masterGain == expected.masterGain);
    CHECK(actual.polyphony == expected.polyphony);
    CHECK(actual.softLimit == expected.softLimit);
    CHECK(actual.dream == expected.dream);
    CHECK(actual.bloom == expected.bloom);
    CHECK(actual.dissolve == expected.dissolve);
    CHECK(actual.gravity == expected.gravity);
    CHECK(actual.entropy == expected.entropy);
}

// Read one little-endian float straight out of the raw bytes, so the
// "gravity lives at OFFSET 28" half of the default-state clause is asserted
// against the byte layout and not merely against read order.
[[nodiscard]] float floatAtOffset(MemoryStream& s, int32 offset) {
    REQUIRE(s.getSize() >= offset + 4);
    float value = 0.0f;
    std::memcpy(&value, s.getData() + offset, sizeof(float));
    return value;
}

[[nodiscard]] int32 int32AtOffset(MemoryStream& s, int32 offset) {
    REQUIRE(s.getSize() >= offset + 4);
    int32 value = 0;
    std::memcpy(&value, s.getData() + offset, sizeof(int32));
    return value;
}

}  // namespace

TEST_CASE("Seraphis_StateRoundTrip", "[seraphis][state]") {

    // -------------------------------------------------------------------------
    // 2. Default-state clause (FR-041).
    // -------------------------------------------------------------------------
    // A getState() on a FRESHLY CONSTRUCTED processor, before any parameter
    // change, must stream the registered defaults -- in particular
    // gravity == 0.5f at offset 28. This is the assertion that catches a
    // value-initialized MacroParams (which would stream 0.0f there and silently
    // disagree with the controller's registered 0.5 default).
    SECTION("A fresh processor streams the registered defaults") {
        Seraphis::Processor proc;

        StreamPtr s = captureState(proc);
        REQUIRE(s->getSize() == kStateBytes);

        // Byte-level: version prefix and the gravity slot, by offset.
        CHECK(int32AtOffset(*s, 0) == Seraphis::kCurrentStateVersion);
        CHECK(int32AtOffset(*s, 0) == 1);
        CHECK(floatAtOffset(*s, kOffGravity) == 0.5f);

        // ...and the whole payload, field by field.
        checkPayload(decodeState(*s), StatePayload{});
    }

    // -------------------------------------------------------------------------
    // 1. Byte stability (FR-045, FR-046).
    // -------------------------------------------------------------------------
    SECTION("Round trip through a fresh processor is byte-stable") {
        const StatePayload seeded = nonDefaultPayload();

        Seraphis::Processor first;
        StreamPtr seed = makeStateStream(seeded);
        REQUIRE(first.setState(seed.get()) == kResultOk);

        StreamPtr a = captureState(first);
        REQUIRE(a->getSize() == kStateBytes);

        // NON-VACUITY: A must carry the SEEDED values, not the defaults. This
        // is what stops a no-op setState()/constant getState() from passing the
        // byte-identity check below.
        CHECK(int32AtOffset(*a, 0) == 1);
        CHECK(floatAtOffset(*a, kOffMasterGain) == seeded.masterGain);
        CHECK(int32AtOffset(*a, kOffPolyphony) == seeded.polyphony);
        CHECK(int32AtOffset(*a, kOffSoftLimit) == seeded.softLimit);
        CHECK(floatAtOffset(*a, kOffDream) == seeded.dream);
        CHECK(floatAtOffset(*a, kOffBloom) == seeded.bloom);
        CHECK(floatAtOffset(*a, kOffDissolve) == seeded.dissolve);
        CHECK(floatAtOffset(*a, kOffGravity) == seeded.gravity);
        CHECK(floatAtOffset(*a, kOffEntropy) == seeded.entropy);
        checkPayload(decodeState(*a), seeded);

        // A -> fresh processor -> B.
        Seraphis::Processor second;
        rewindStream(*a);
        REQUIRE(second.setState(a.get()) == kResultOk);
        StreamPtr b = captureState(second);

        REQUIRE(b->getSize() == a->getSize());
        REQUIRE(std::memcmp(a->getData(), b->getData(),
                            static_cast<std::size_t>(a->getSize())) == 0);
        checkPayload(decodeState(*b), seeded);
    }

    // -------------------------------------------------------------------------
    // 3. Truncation (FR-046).
    // -------------------------------------------------------------------------
    // Every reader is EOF-safe, so a stream cut at any field boundary must load
    // what it has and leave EVERY LATER FIELD at its registered default -- never
    // at garbage, and never crashing.
    SECTION("Truncated streams leave every field beyond the cut at its default") {
        const StatePayload seeded = nonDefaultPayload();
        StreamPtr full = makeStateStream(seeded);

        const int32 cuts[] = {0, 4, 8, 12, 16, 20, 24, 28, 32};
        for (const int32 n : cuts) {
            INFO("truncated to " << n << " bytes");

            StreamPtr cut = makeTruncatedStream(*full, n);

            Seraphis::Processor proc;
            const tresult r = proc.setState(cut.get());
            // The version int32 is the only mandatory field: below 4 bytes the
            // stream is unreadable and MUST be rejected; at or above 4 bytes
            // every short read is absorbed by the EOF-safe loaders.
            if (n < 4) {
                CHECK(r == kResultFalse);
            } else {
                CHECK(r == kResultOk);
            }

            // Fields present in the cut carry the seeded value; the rest keep
            // their registered defaults.
            StatePayload expected{};
            if (n >= kOffPolyphony) {
                expected.masterGain = seeded.masterGain;
            }
            if (n >= kOffSoftLimit) {
                expected.polyphony = seeded.polyphony;
            }
            if (n >= kOffDream) {
                expected.softLimit = seeded.softLimit;
            }
            if (n >= kOffBloom) {
                expected.dream = seeded.dream;
            }
            if (n >= kOffDissolve) {
                expected.bloom = seeded.bloom;
            }
            if (n >= kOffGravity) {
                expected.dissolve = seeded.dissolve;
            }
            if (n >= kOffEntropy) {
                expected.gravity = seeded.gravity;
            }
            if (n >= kStateBytes) {
                expected.entropy = seeded.entropy;
            }

            StreamPtr after = captureState(proc);
            checkPayload(decodeState(*after), expected);
        }
    }

    // -------------------------------------------------------------------------
    // 4. Version rejection (FR-046).
    // -------------------------------------------------------------------------
    SECTION("A future state version is rejected and applies nothing") {
        StatePayload future = nonDefaultPayload();
        future.version = Seraphis::kCurrentStateVersion + 1;  // == 2
        REQUIRE(future.version == 2);

        Seraphis::Processor proc;
        StreamPtr s = makeStateStream(future);
        REQUIRE(proc.setState(s.get()) == kResultFalse);

        // Rejected means rejected: not one field of the payload was applied.
        StreamPtr after = captureState(proc);
        checkPayload(decodeState(*after), StatePayload{});
    }

    SECTION("A null stream is rejected") {
        Seraphis::Processor proc;
        CHECK(proc.setState(nullptr) == kResultFalse);
    }

    // -------------------------------------------------------------------------
    // 5. Controller clause (FR-047) - MANDATORY.
    // -------------------------------------------------------------------------
    // Without this, FR-047 has NO detector: a setComponentState() that returns
    // kResultOk without reading the stream, or that loads the two packs in the
    // wrong order, passes every other criterion in this file, and
    // loadGlobalParamsToController / loadMacroParamsToController are never
    // called by any test.
    SECTION("Controller::setComponentState refreshes all eight parameters") {
        const StatePayload seeded = nonDefaultPayload();

        Seraphis::Processor proc;
        StreamPtr seed = makeStateStream(seeded);
        REQUIRE(proc.setState(seed.get()) == kResultOk);

        StreamPtr state = captureState(proc);
        checkPayload(decodeState(*state), seeded);  // the stream really is non-default
        rewindStream(*state);

        Seraphis::Controller controller;
        REQUIRE(controller.initialize(nullptr) == kResultOk);

        // Baseline: the registered defaults, all of which differ from the
        // values in the stream. Any of these surviving the load is a no-op
        // loader.
        REQUIRE(controller.getParamNormalized(Seraphis::kMasterGainId) == Approx(0.5));
        REQUIRE(controller.getParamNormalized(Seraphis::kPolyphonyId) == Approx(7.0 / 15.0));
        REQUIRE(controller.getParamNormalized(Seraphis::kSoftLimitId) == Approx(1.0));
        REQUIRE(controller.getParamNormalized(Seraphis::kMacroGravityId) == Approx(0.5));

        REQUIRE(controller.setComponentState(state.get()) == kResultOk);

        // The inverse mappings of the two packs: masterGain / 2.0,
        // (polyphony - 1) / 15.0, softLimit ? 1.0 : 0.0, macros as-is.
        CHECK(controller.getParamNormalized(Seraphis::kMasterGainId) ==
              Approx(static_cast<double>(seeded.masterGain) / 2.0));
        CHECK(controller.getParamNormalized(Seraphis::kPolyphonyId) ==
              Approx((static_cast<double>(seeded.polyphony) - 1.0) / 15.0));
        CHECK(controller.getParamNormalized(Seraphis::kSoftLimitId) ==
              Approx(seeded.softLimit != 0 ? 1.0 : 0.0));
        CHECK(controller.getParamNormalized(Seraphis::kMacroDreamId) ==
              Approx(static_cast<double>(seeded.dream)));
        CHECK(controller.getParamNormalized(Seraphis::kMacroBloomId) ==
              Approx(static_cast<double>(seeded.bloom)));
        CHECK(controller.getParamNormalized(Seraphis::kMacroDissolveId) ==
              Approx(static_cast<double>(seeded.dissolve)));
        CHECK(controller.getParamNormalized(Seraphis::kMacroGravityId) ==
              Approx(static_cast<double>(seeded.gravity)));
        CHECK(controller.getParamNormalized(Seraphis::kMacroEntropyId) ==
              Approx(static_cast<double>(seeded.entropy)));

        REQUIRE(controller.terminate() == kResultOk);
    }

    SECTION("Controller::setComponentState rejects a future state version") {
        StatePayload future = nonDefaultPayload();
        future.version = Seraphis::kCurrentStateVersion + 1;

        Seraphis::Controller controller;
        REQUIRE(controller.initialize(nullptr) == kResultOk);

        StreamPtr s = makeStateStream(future);
        CHECK(controller.setComponentState(s.get()) == kResultFalse);

        // Nothing was applied.
        CHECK(controller.getParamNormalized(Seraphis::kMasterGainId) == Approx(0.5));
        CHECK(controller.getParamNormalized(Seraphis::kMacroGravityId) == Approx(0.5));

        REQUIRE(controller.terminate() == kResultOk);
    }
}
