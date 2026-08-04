// ==============================================================================
// Seraphis - Phase 11 UI RT-safety and performance tests
// ==============================================================================
// Reference: specs/seraphis-phase11-ui/spec.md   (SC-011 and its arm 2, FR-040,
//                                                 FR-005; SC-009/SC-010 land here
//                                                 in T023)
//            specs/seraphis-phase11-ui/plan.md   (section 10.3's compile-flag
//                                                 ruling)
//            specs/seraphis-phase11-ui/tasks.md  (T022 creates this TU; T023
//                                                 appends the four [.perf] arms)
//
// CRITERIA OWNED BY THIS TU (T022's half):
//   SC-011 arm 1  "The snapshot allocates nothing and throws nothing" - a 60 s
//                 gate-open render inside TestHelpers::AllocationScope, plus the
//                 source scan for lock primitives and throw sites, plus the
//                 runtime lock-free assertion over every Phase 11 atomic that
//                 crosses the message/audio boundary.
//   SC-011 arm 2  "No platform API anywhere in Phase 11 source" - FR-005. This
//   / FR-005      arm exists because FR-005 is otherwise mapped only to SC-019
//                 (builds + portability + clang-tidy) and A PLATFORM-GUARDED
//                 NATIVE POPUP COMPILES CLEAN ON ALL THREE LEGS: nothing in the
//                 build detects it, so the honest instrument is the source.
//
// CRITERIA OWNED BY THIS TU (T023's half - the four "[.perf]" arms at the bottom).
// THREE OF THEM WERE RESTATED AS DIFFERENTIAL AND ONE RE-BASELINED BY THE
// PHASE-OWNER RULING OF 2026-08-04; see the "RULING" block above the arms:
//   SC-009 (a)    the PRODUCER'S MARGINAL whole-process() cost - gate OPEN minus
//                 gate CLOSED, interleaved on one fixture - and (b) the snapshot
//                 stage measured alone on its own scoped timer (unchanged).
//   SC-010 (a)(b) the gate-CLOSED arms: zero publish attempts over 60 s
//                 (unchanged, hard), and a whole-process() figure against a
//                 WHOLE-process() baseline, PROVISIONAL and reporting until a
//                 seven-run fresh-boot cold set pins it.
//   SC-014 arm 7  the RE-PUSH'S MARGINAL cost - the same macro sweep with 64
//                 overrides authored minus with none (2048 transcendentals per
//                 fan-out), measured rather than assumed to be bounded.
//   SC-031        the IN-FLIGHT GESTURE'S MARGINAL cost - the arm T003a's gate
//                 relaxation needs and that no pre-existing criterion supplies,
//                 because SC-009/SC-010/arm 7 all run with a STATIC slot set.
//
// COMPILE FLAGS: this TU MUST NOT be listed under "-fno-fast-math
//   -fno-finite-math-only" in plugins/seraphis/tests/CMakeLists.txt. It carries
//   the [.perf] arms T023 adds, and those flags move the figures their baselines
//   are pinned to - the same rule integration/param_perf_test.cpp:22-27 and
//   integration/effects_perf_test.cpp:22-29 already follow, and the reason the
//   TU is created here, BEFORE any baseline is pinned, rather than split out
//   later (tasks.md T022's file list states it).
//   It therefore injects NO non-finite value and names no std::isnan /
//   std::isinf / std::numeric_limits infinity: the macOS leg builds with
//   -ffast-math, which folds them.
//
// NO CHECKED-IN FLOAT GOLDEN: nothing here compares a float to a stored
// constant. Every number asserted below is an integer count.
//
// THE INSTRUMENT IS PHASE 10's, RE-POINTED - NOT A NEW ONE.
// integration/effects_perf_test.cpp:619-759 owns the original comment stripper,
// occurrence counter and scan struct; this file carries the same three, with the
// same two anti-vacuity guards (`filesMissing == 0`, `codeBytes > 0`) and the
// same witness-token idea (`:692-695`), pointed at two different corpora and two
// different token sets. They are re-declared here rather than hoisted into a
// shared header because each lives in its TU's anonymous namespace and neither
// scan is a general-purpose facility: the corpus IS the criterion in both cases.
// ==============================================================================

#include "processor/cloud_frame.h"
#include "processor/processor.h"
#include "seraphis_test_fixture.h"
#include "ui/edit_message.h"

#include "plugin_ids.h"

#include "public.sdk/source/vst/hosting/hostclasses.h"

#include <pluginterfaces/base/smartpointer.h>  // Steinberg::owned
#include <pluginterfaces/vst/ivstevents.h>     // Event::kNoteOnEvent
#include <pluginterfaces/vst/ivstmessage.h>    // IMessage / IAttributeList

#include <krate/dsp/systems/harmonic_cloud.h>
#include <krate/dsp/systems/seraphis_engine.h>

#include <allocation_detector.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

namespace {

using Catch::Approx;
using Fixture = SeraphisTest::ProcessorFixture;

// =============================================================================
// Measurement basis
// =============================================================================

constexpr double kSr48 = 48000.0;
constexpr Steinberg::int32 kBlock = 512;
constexpr std::size_t kBlockSize = 512;

/// SC-011's render length: 5 625 x 512 = 2 880 000 samples = EXACTLY 60 s at
/// 48 kHz. The same geometry Phase 10's SC-011 renders
/// (effects_perf_test.cpp:432-434), so the two RT-safety criteria measure the
/// same amount of audio.
constexpr std::size_t kRtBlocks = 5625;
static_assert(kRtBlocks * kBlockSize == 2880000u, "SC-011 renders exactly 60 s at 48 kHz");

/// Blocks rendered before the AllocationScope opens. The fixture's containers
/// grow on demand and are then REUSED (seraphis_test_fixture.h:15-19), so a
/// render inside the scope is allocation-free only once every one of them has
/// been through at least one block: the output storage, the channel-pointer
/// array, the event list and the parameter-queue container.
constexpr int kWarmupBlocks = 8;

/// TWO voices, and that is a MEASURED-SUBJECT decision rather than a shortcut -
/// the same one Phase 10's SC-011 records at effects_perf_test.cpp:472-480.
/// publishCloudFrame() snapshots exactly ONE voice, the focus voice
/// (processor.cpp:4025), so the voice count changes the HARNESS's wall cost and
/// never this criterion's subject. Two notes keep a real cloud sounding - so
/// `partialCount > 0` below is a genuine assertion and the 64-partial read loop
/// really executes - while keeping a 60 s render affordable in the DEFAULT
/// (non-[.perf]) suite this case is tagged into.
constexpr std::size_t kRtPolyphony = 2;
constexpr Steinberg::int16 kFirstNote = 57;  // 220 Hz; distinct notes -> distinct slots
constexpr float kVelocity = 100.0f / 127.0f;

static_assert(kRtPolyphony <= Krate::DSP::SeraphisEngine::kMaxVoices,
              "the scenario must fit the pool");

/// C-5 kinds used below (edit_message.h:31-52). Named locally; the header
/// deliberately carries the table as prose plus `kEditKindCount` only.
constexpr std::uint8_t kEditPartialPan = 2;
constexpr std::uint8_t kEditPartialMask = 3;

/// The three partials the staged overrides below name, and the pan they author.
constexpr std::uint16_t kPannedPartial = 9;
constexpr std::uint16_t kMaskedPartialA = 3;
constexpr std::uint16_t kMaskedPartialB = 17;
constexpr float kAuthoredPan = 0.75f;

/// THE OPTIMIZATION BARRIER (effects_perf_test.cpp:347-355). Every render drains
/// its accumulator into this, so no timed or measured render can be dead-coded
/// away. It only works as a MUTABLE object at namespace scope - const or
/// function-local storage lets the optimizer prove the stores dead - which is why
/// the check below is suppressed rather than obeyed.
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
volatile double gSink = 0.0;

// =============================================================================
// THE SOURCE-SCAN INSTRUMENT (Phase 10's, re-pointed)
// =============================================================================
// Two clauses have no portable runtime hook: "no locks" and "no platform API".
// A prose comment reading "it is structural" does not fail when somebody adds a
// std::mutex or a #include <windows.h> behind a platform guard - which is
// precisely what these scans close. The scan strips comments and string
// literals first, so the gate cannot be tripped by prose (this very comment
// names std::mutex and windows.h, and this TU is in neither corpus).
// =============================================================================

/// Source with `//` comments, `/* */` comments and string-literal CONTENTS
/// removed, newlines preserved. Character literals are deliberately NOT tracked,
/// for the reason effects_perf_test.cpp:619-622 records: none of the scanned
/// files holds a comment opener in one, and treating `'` as a state change would
/// misparse a digit separator such as `1'000`.
[[nodiscard]] std::string strippedSource(const std::string& src) {
    enum class State : std::uint8_t { Code, LineComment, BlockComment, StringLiteral };
    State state = State::Code;
    std::string out;
    out.reserve(src.size());

    for (std::size_t i = 0; i < src.size(); ++i) {
        const char c = src[i];
        const char next = (i + 1u < src.size()) ? src[i + 1u] : '\0';

        switch (state) {
        case State::Code:
            if ((c == '/') && (next == '/')) {
                state = State::LineComment;
                ++i;
            } else if ((c == '/') && (next == '*')) {
                state = State::BlockComment;
                ++i;
            } else if (c == '"') {
                state = State::StringLiteral;
            } else {
                out.push_back(c);
            }
            break;
        case State::LineComment:
            if (c == '\n') {
                state = State::Code;
                out.push_back(c);
            }
            break;
        case State::BlockComment:
            if ((c == '*') && (next == '/')) {
                state = State::Code;
                ++i;
            } else if (c == '\n') {
                out.push_back(c);
            }
            break;
        case State::StringLiteral:
            if (c == '\\') {
                ++i;  // skip the escaped character, whatever it is
            } else if (c == '"') {
                state = State::Code;
            }
            break;
        }
    }
    return out;
}

[[nodiscard]] std::size_t countOccurrences(const std::string& haystack, const char* needle) {
    std::size_t n = 0;
    const std::string pattern(needle);
    if (pattern.empty()) {
        return 0;
    }
    for (std::size_t at = haystack.find(pattern); at != std::string::npos;
         at = haystack.find(pattern, at + pattern.size())) {
        ++n;
    }
    return n;
}

struct SourceScan {
    std::size_t filesScanned = 0;
    std::size_t filesMissing = 0;
    /// Non-comment bytes actually examined. Asserted non-trivial so a build that
    /// resolved the paths but read nothing - or a stripper that ate every file -
    /// cannot report "no hits found" about an empty corpus.
    std::size_t codeBytes = 0;
    /// Occurrences of a token that MUST be present. It is the WITNESS that the
    /// corpus is the intended one: a scan pointed at readable but WRONG files
    /// would clear the forbidden-token count and this would be 0.
    std::size_t witnesses = 0;
    std::size_t hits = 0;
    std::string firstHit;  ///< "<file>: <token>" for the failure message
};

/// One scan: `files` stripped of comments and string literals, counted for
/// `witness` and for every token in `forbidden`.
[[nodiscard]] SourceScan scanSources(const std::vector<std::string>& files,
                                     const std::vector<const char*>& forbidden,
                                     const char* witness) {
    SourceScan scan{};
    for (const std::string& path : files) {
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            ++scan.filesMissing;
            if (scan.firstHit.empty()) {
                scan.firstHit = path + ": UNREADABLE";
            }
            continue;
        }
        const std::string raw((std::istreambuf_iterator<char>(in)),
                              std::istreambuf_iterator<char>());
        const std::string code = strippedSource(raw);
        ++scan.filesScanned;
        scan.codeBytes += code.size();
        scan.witnesses += countOccurrences(code, witness);

        for (const char* token : forbidden) {
            const std::size_t n = countOccurrences(code, token);
            scan.hits += n;
            if ((n != 0u) && scan.firstHit.empty()) {
                scan.firstHit = path + ": " + token;
            }
        }
    }
    return scan;
}

// -----------------------------------------------------------------------------
// The two corpora. SERAPHIS_SRC_DIR is handed to this TU by
// plugins/seraphis/tests/CMakeLists.txt (the same definition
// effects_perf_test.cpp uses).
// -----------------------------------------------------------------------------

/// SC-011's corpus, EXACTLY three files: the phase's audio-thread-reachable
/// surface. `spectral_state.h` is deliberately NOT scanned - the three authoring
/// mutators are message-thread-only by C-5, and adding it would scan ~600 lines
/// of unrelated Phase 3 code (spec.md:1385-1388).
[[nodiscard]] std::vector<std::string> cloudFramePathSources() {
    const std::string src(SERAPHIS_SRC_DIR);
    return {
        src + "/processor/processor.cpp",
        src + "/processor/processor.h",
        src + "/processor/cloud_frame.h",
    };
}

/// FR-005's corpus: EVERY header and TU under src/ui/, plus the processor and
/// controller pairs.
///
/// The ui/ half is ENUMERATED FROM THE DIRECTORY, not hard-coded, and that is
/// load-bearing: FR-005 is a statement about "any Phase 11 source file", so a
/// hard-coded list would silently stop covering a file added by a later task.
/// The `>= 8` floor asserted at the call site is what keeps a wrong or empty
/// directory red rather than vacuously green.
[[nodiscard]] std::vector<std::string> phase11UiAndShellSources() {
    const std::string src(SERAPHIS_SRC_DIR);
    std::vector<std::string> files;

    std::error_code ec;
    const std::filesystem::path uiDir = std::filesystem::path(src) / "ui";
    for (const std::filesystem::directory_entry& entry :
         std::filesystem::directory_iterator(uiDir, ec)) {
        std::error_code fileEc;
        if (!entry.is_regular_file(fileEc)) {
            continue;
        }
        const std::string ext = entry.path().extension().string();
        if (ext == ".h" || ext == ".cpp") {
            files.push_back(entry.path().string());
        }
    }
    std::sort(files.begin(), files.end());  // stable failure messages

    files.push_back(src + "/processor/processor.h");
    files.push_back(src + "/processor/processor.cpp");
    files.push_back(src + "/controller/controller.h");
    files.push_back(src + "/controller/controller.cpp");
    return files;
}

/// Blocking primitives. `.lock()` and `->lock()` catch a hand-rolled or
/// third-party lockable that none of the named spellings would.
[[nodiscard]] std::vector<const char*> lockTokens() {
    return {"std::mutex",  "recursive_mutex", "shared_mutex", "lock_guard",
            "unique_lock", "scoped_lock",     "shared_lock",  "condition_variable",
            "pthread_mutex", ".lock()",       "->lock()"};
}

/// Exception machinery. Zero today; a hit means somebody put unwinding on - or
/// one call away from - the audio thread.
[[nodiscard]] std::vector<const char*> throwTokens() {
    return {"throw ", "throw;", "throw(", "catch (", "catch("};
}

/// FR-005's forbidden set: Win32, Cocoa/AppKit, GTK and raw X11. Root CLAUDE.md's
/// Cross-Platform Requirement names exactly these families.
///
/// KNOWN BLIND SPOT, stated rather than papered over: the stripper removes
/// string-literal CONTENTS, so a quoted `#include "windows.h"` would not be seen
/// (the angle-bracket form, which is how a platform header is actually included,
/// is). It is left as-is deliberately - un-stripping literals is what would let
/// prose about the forbidden tokens trip the gate, and that trade is the reason
/// the stripper exists. `#import` is caught either way: the directive itself is
/// never inside a literal.
[[nodiscard]] std::vector<const char*> platformTokens() {
    return {"windows.h", "HWND",    "CreateWindow", "MessageBox", "NSView",
            "NSWindow",  "NSAlert", "#import",      "gtk_",       "XCreateWindow"};
}

// =============================================================================
// The wire: one HostMessage per send, exactly the shape the controller uses
// (integration/partial_edit_test.cpp:145-157)
// =============================================================================

[[nodiscard]] Seraphis::UI::EditMessage makeEdit(std::uint8_t kind, std::uint16_t index,
                                                 float a) {
    Seraphis::UI::EditMessage m{};
    m.kind = kind;
    m.slot = 0;
    m.index = index;
    m.a = a;
    m.b = 0.0f;
    return m;
}

Steinberg::tresult sendEdit(Seraphis::Processor& processor,
                            const Seraphis::UI::EditMessage& m) {
    auto message = Steinberg::owned(new Steinberg::Vst::HostMessage());
    message->setMessageID(Seraphis::UI::kSeraphisEditMessageId);
    Steinberg::Vst::IAttributeList* attributes = message->getAttributes();
    REQUIRE(attributes != nullptr);
    REQUIRE(attributes->setBinary(Seraphis::UI::kSeraphisEditAttributeId, &m,
                                  static_cast<Steinberg::uint32>(sizeof(m)))
            == Steinberg::kResultOk);
    return processor.notify(message);
}

void pressChord(Fixture& fx, std::size_t voices) {
    for (std::size_t v = 0; v < voices; ++v) {
        fx.pushEvent(Steinberg::Vst::Event::kNoteOnEvent,
                     static_cast<Steinberg::int16>(kFirstNote + static_cast<int>(v)), kVelocity,
                     0);
    }
}

}  // namespace

// =============================================================================
// SC-011 arm 1 / FR-040 - THE SNAPSHOT ALLOCATES NOTHING AND THROWS NOTHING
// =============================================================================
TEST_CASE("Seraphis_CloudFrame_AllocatesNothing", "[ui][phase11]") {
    // ---- THE SCANNER'S OWN NEGATIVE CONTROL, FIRST --------------------------
    // A scanner that silently found nothing - a bad path, a stripper that ate the
    // whole file, a find() loop that never advanced - would let the lock clause
    // pass vacuously forever. These assertions prove the instrument detects what
    // it is looking for BEFORE it is believed about what it did not find.
    {
        const std::string positive =
            "void f() { std::mutex m; std::lock_guard<std::mutex> g(m); throw 1; }";
        REQUIRE(countOccurrences(strippedSource(positive), "std::mutex") == 2u);
        REQUIRE(countOccurrences(strippedSource(positive), "lock_guard") == 1u);
        REQUIRE(countOccurrences(strippedSource(positive), "throw ") == 1u);
        // ... and that comments and string literals really are removed, which is
        // what lets the scanned files carry prose about locks without tripping it.
        const std::string commented =
            "int a; // std::mutex\n/* lock_guard */ const char* s = \"throw \";\n";
        REQUIRE(countOccurrences(strippedSource(commented), "std::mutex") == 0u);
        REQUIRE(countOccurrences(strippedSource(commented), "lock_guard") == 0u);
        REQUIRE(countOccurrences(strippedSource(commented), "throw ") == 0u);
    }

    // ---- SC-011's LOCK and THROW clauses over the Phase 11 corpus -----------
    // Two passes over the same three files, one token family each, so a failure
    // names which clause broke rather than reporting a merged count.
    const std::vector<std::string> corpus = cloudFramePathSources();
    const SourceScan locks = scanSources(corpus, lockTokens(), "publishCloudFrame");
    const SourceScan throws = scanSources(corpus, throwTokens(), "publishCloudFrame");

    INFO("lock scan first hit : " << locks.firstHit);
    INFO("throw scan first hit: " << throws.firstHit);

    // The corpus is the right one, is non-empty, and was really read - asserted
    // BEFORE the two zero-counts, because a zero count over nothing is not a
    // finding (spec.md:1389-1394).
    REQUIRE(locks.filesMissing == 0u);
    REQUIRE(throws.filesMissing == 0u);
    REQUIRE(locks.filesScanned == 3u);
    REQUIRE(throws.filesScanned == 3u);
    REQUIRE(locks.codeBytes > 0u);
    REQUIRE(throws.codeBytes > 0u);
    // `publishCloudFrame` is this phase's own audio-thread entry point: the
    // declaration in processor.h plus the definition in processor.cpp, i.e. at
    // least two, and the call site inside process() makes it three today.
    REQUIRE(locks.witnesses >= 2u);
    REQUIRE(throws.witnesses >= 2u);
    // ... and only now, the clauses themselves.
    REQUIRE(locks.hits == 0u);
    REQUIRE(throws.hits == 0u);

    // ---- The render ---------------------------------------------------------
    auto fx = std::make_unique<Fixture>();
    REQUIRE(fx->prepare(kSr48, kBlock) == Steinberg::kResultOk);
    REQUIRE(fx->proc->engineForTest() != nullptr);

    // The gate is opened BEFORE the warm-up, so whatever the first publish
    // touches is already warm when the detector starts counting. The measured
    // count is a DELTA across the scope for the same reason.
    fx->proc->setCloudFrameGateForTest(true);

    bool warmupOk = true;
    for (int b = 0; b < kWarmupBlocks; ++b) {
        if (b == 0) {
            pressChord(*fx, kRtPolyphony);
        }
        // NOT `warmupOk = warmupOk && processBlock(...)`: && short-circuits, so a
        // single non-ok result would silently stop rendering the rest of the
        // warm-up. The call is always made and the flag is folded afterwards.
        const bool ok = (fx->processBlock(kBlock) == Steinberg::kResultOk);
        warmupOk = warmupOk && ok;
    }
    REQUIRE(warmupOk);
    REQUIRE(fx->proc->cloudFramePublishAttemptCountForTest() > 0u);

    // Stage three C-5 overrides so partialOverridesPending_ is TRUE on entry to
    // the scope: the FIRST measured block then executes T011's full fan-out
    // (repushPartialOverrides) with the detector live, rather than only its
    // one-atomic-exchange fast path. sendEdit() itself allocates a HostMessage
    // and therefore runs OUTSIDE the scope, which is correct - notify() is the
    // MESSAGE thread and is not FR-040's subject.
    REQUIRE(sendEdit(*fx->proc, makeEdit(kEditPartialPan, kPannedPartial, kAuthoredPan))
            == Steinberg::kResultOk);
    REQUIRE(sendEdit(*fx->proc, makeEdit(kEditPartialMask, kMaskedPartialA, 1.0f))
            == Steinberg::kResultOk);
    REQUIRE(sendEdit(*fx->proc, makeEdit(kEditPartialMask, kMaskedPartialB, 1.0f))
            == Steinberg::kResultOk);

    const std::size_t attemptsBefore = fx->proc->cloudFramePublishAttemptCountForTest();

    // READING FORM (normative, effects_perf_test.cpp:852-859): AllocationScope::
    // getAllocationCount() returns a member assigned only in the DESTRUCTOR
    // (allocation_detector.h:81-87), so reading THAT inside the scope yields a
    // value-initialized 0 and passes unconditionally. The count is taken from the
    // LIVE atomic while tracking is still on, stored in a local, and asserted
    // after the scope has closed - Catch2's REQUIRE is itself an allocating
    // expression and must never run inside.
    std::size_t allocations = 0;
    std::size_t exceptions = 0;
    std::size_t blocksOk = 0;
    double audioSink = 0.0;
    {
        TestHelpers::AllocationScope scope;
        for (std::size_t b = 0; b < kRtBlocks; ++b) {
            // SC-011's EXCEPTION clause, counted rather than reasoned about.
            // Processor::process() carries the SDK's signature and is NOT
            // noexcept, so anything thrown from it - or from any non-noexcept
            // frame beneath it - unwinds to here. Catching costs nothing on the
            // non-throwing path and allocates nothing, so it is legal inside the
            // AllocationScope.
            try {
                if (fx->processBlock(kBlock) == Steinberg::kResultOk) {
                    ++blocksOk;
                }
            } catch (...) {
                ++exceptions;
            }
            // A real consumer reads the buffer; this is what stops the optimizer
            // dead-coding the render away.
            audioSink += static_cast<double>(fx->audioL()[0])
                         + static_cast<double>(fx->audioR()[kBlockSize - 1u]);
        }
        allocations = TestHelpers::AllocationDetector::instance().getAllocationCount();
    }

    // ---- THE CRITERION ------------------------------------------------------
    CHECK(allocations == 0u);
    CHECK(exceptions == 0u);
    REQUIRE(blocksOk == kRtBlocks);
    REQUIRE(fx->checkCanaries());

    // ---- ...and it was not measured about an idle producer ------------------
    const std::size_t attempts =
        fx->proc->cloudFramePublishAttemptCountForTest() - attemptsBefore;
    INFO("publish attempts = " << attempts
                               << ", skipped = "
                               << fx->proc->cloudFrameSkippedBlockCountForTest());
    REQUIRE(attempts == kRtBlocks);
    // The snapshot loop really ran over a live cloud rather than an empty one...
    const Seraphis::CloudFrame& frame = fx->proc->lastPublishedFrameForTest();
    REQUIRE(frame.partialCount > 0);
    // ...the three staged edits were live across the whole measured render, so
    // the consumer had real work on its first block...
    CHECK((frame.maskBits & (std::uint64_t{1} << kMaskedPartialA)) != 0u);
    CHECK((frame.maskBits & (std::uint64_t{1} << kMaskedPartialB)) != 0u);
    CHECK((frame.overriddenBits & (std::uint64_t{1} << kPannedPartial)) != 0u);
    // ...and the fan-out itself ran INSIDE the scope: the authored pan reached
    // the engine, which only repushPartialOverrides() can do (the message thread
    // never touches HarmonicCloud - T003's thread-ownership note). Read on voice
    // 0 at SC-014's own 0.01 band (partial_edit_test.cpp:865-886); the
    // every-slot and clearing-event arms belong to that case, not this one.
    const Krate::DSP::SeraphisEngine& engine = *fx->proc->engineForTest();
    CHECK(static_cast<double>(engine.getVoice(0).cloud().getPartialPosition(kPannedPartial))
          == Approx(static_cast<double>(kAuthoredPan)).margin(0.01));
    gSink = gSink + audioSink;  // optimization barrier

    // ---- SC-011's LOCK-FREE ARM --------------------------------------------
    // The constitution's rule is that only std::atomic_flag is GUARANTEED
    // lock-free, so the "lock-free on x86-64/arm64" claim behind the Phase 11
    // message/audio handshake is asserted at runtime rather than assumed. A
    // locking atomic on the audio thread is an RT violation, and this is the arm
    // that finds it: cloudFrameEnabled_, partialOverridesPending_, both override
    // bitmasks and partialPanStaging_[0] (processor.h's
    // phase11AtomicsAreLockFreeForTest).
    CHECK(fx->proc->phase11AtomicsAreLockFreeForTest());

    // ---- LIVENESS PROBE - a SEPARATE, never nested scope ---------------------
    // Nesting is silently wrong in both directions: the inner ctor's
    // startTracking() RESETS the outer count (allocation_detector.h:31-34) and
    // the inner dtor's stopTracking() switches tracking off for the outer scope
    // too (:37-40). Without this probe the zero above would be vacuous on a
    // detector that counts nothing.
    std::size_t probe = 0;
    {
        TestHelpers::AllocationScope scope;
        // `volatile` is load-bearing: [expr.new]/10 lets a compiler elide an
        // otherwise-unobserved new/delete pair even when the global allocation
        // functions are replaced.
        int* volatile deliberate = new int(7);
        probe = TestHelpers::AllocationDetector::instance().getAllocationCount();
        delete deliberate;
    }
    REQUIRE(probe >= 1u);
}

// =============================================================================
// SC-011 arm 2 / FR-005 - NO PLATFORM API IN ANY PHASE 11 SOURCE FILE
// =============================================================================
// WHY A SOURCE SCAN AND NOT THE BUILD. FR-005 is otherwise mapped only to SC-019
// (three green builds + node tools/check-portability.js + clang-tidy), and a
// platform-guarded native popup - `#ifdef _WIN32 #include <windows.h> ... #endif`
// - compiles clean on ALL THREE legs and passes every one of those gates. The
// only instrument that fails on it is a scan of the source itself.
//
// OBSERVE IT FAIL BEFORE TRUSTING IT (tasks.md T022): temporarily insert
// `#include <windows.h>` behind a platform guard in one src/ui/ file, confirm
// this case goes red naming that file and that token, then revert.
// =============================================================================
TEST_CASE("Seraphis_Phase11_UsesNoPlatformApi", "[ui][phase11]") {
    // The scanner's negative control, in this case's own token family: a guarded
    // include is exactly the shape the criterion exists to catch, and a comment
    // mentioning one is exactly the shape it must NOT catch.
    {
        const std::string guarded =
            "#ifdef _WIN32\n#include <windows.h>\nHWND h = nullptr;\n#endif\n";
        REQUIRE(countOccurrences(strippedSource(guarded), "windows.h") == 1u);
        REQUIRE(countOccurrences(strippedSource(guarded), "HWND") == 1u);
        const std::string prose = "// windows.h and HWND are forbidden\n";
        REQUIRE(countOccurrences(strippedSource(prose), "windows.h") == 0u);
        REQUIRE(countOccurrences(strippedSource(prose), "HWND") == 0u);
    }

    const std::vector<std::string> corpus = phase11UiAndShellSources();
    // The ui/ half is enumerated from the directory, so an empty or wrong path
    // would hand the scan four files and clear the token count vacuously. The
    // floor is the Phase 11 view surface as shipped: cloud_view.{h,cpp},
    // drawer_container.{h,cpp}, edit_sub_controller.{h,cpp}, macro_ring_knob.h
    // and edit_message.h - eight - plus the four shell files.
    INFO("corpus size = " << corpus.size());
    REQUIRE(corpus.size() >= 12u);

    const SourceScan scan = scanSources(corpus, platformTokens(), "VSTGUI::");
    INFO("first hit: " << scan.firstHit);

    // Both anti-vacuity guards, plus the witness - asserted BEFORE the clause.
    REQUIRE(scan.filesMissing == 0u);
    REQUIRE(scan.filesScanned == corpus.size());
    REQUIRE(scan.codeBytes > 0u);
    // `VSTGUI::` must be present: the UI corpus is written against VSTGUI, so a
    // scan that found readable but WRONG files would report zero here.
    REQUIRE(scan.witnesses > 0u);

    // THE CRITERION. Zero hits, on every token, across every Phase 11 source
    // file. If this fails, the fix is a VSTGUI abstraction (COptionMenu,
    // CFileSelector, CButtonState, ...) - never a platform #if.
    CHECK(scan.hits == 0u);
}

// =============================================================================
// T023 - THE FOUR "[.perf]" ARMS
// =============================================================================
// MEASUREMENT PROTOCOL (param_perf_test.cpp:144-207, restated because it is a
// property of the RUN and not of the code): fresh boot, IDLE machine, SEVEN
// consecutive runs of `seraphis_tests.exe "[.perf]"`, each figure below a
// best-of-16, the WORST of the seven reported. Every case here is tagged
// "[.perf]" and is therefore outside the default run.
//
// THE ANCHORS ARE COPIED BY VALUE, NOT INCLUDED. kFullPolyCeilingNs,
// kRegressionFactor and kBaselineFullPolyNs live in param_perf_test.cpp's own
// anonymous namespace (`:392`, `:395`, `:472`) and T023 forbids touching that
// TU's baseline constants. Two copies can drift, so each one below is pinned by
// a static_assert against the arithmetic that produced it - a build break is the
// only cross-TU enforcement available.
//
// WHY THE SUBJECT HERE IS Processor AND NOT param_perf_test.cpp's HAND-BUILT
// PAIR, stated so no reader assumes the two figures are the same quantity.
// `Seraphis_FullPoly_CpuBudget_WithFullSurface` times exactly three calls on a
// hand-built engine/reverb pair (that file's FINDING 1): there is no
// SpectralDelay, no MidSideProcessor and NO PROCESSOR in its timed region, so
// the C-2 clause 6 gate this phase adds structurally cannot exist in it and
// "re-run it with the gate open" is not an operation that subject admits. The
// gate-open figure therefore has to be a whole-`process()` figure, which is what
// every arm below measures. Two consequences, both deliberate:
//   * this subject uses the SHIPPED reverb (numChannels 8, diffusionFftSize
//     1024, seraphis_engine_config.h:71,:82), not RA-1 row (c)'s deliberate
//     worst case (16 / 4096), so it is cheaper in that dimension;
//   * it contains C-1 steps 4 and 5 - the whole Phase 10 effects stage - which
//     that subject does not, so it is more expensive in that one.
// Neither figure is a regression bound on the other, and no arm below compares
// them. What they share is the CEILING, which is a property of the 512/48 kHz
// block period and of nothing else.
//
// =============================================================================
// ESCALATION (2026-08-04): ALL FOUR ARMS BREACH THE 25 % CEILING, AND PHASE 11
// IS NOT THE MECHANISM. MEASURED, ON A MACHINE THE PROTOCOL'S OWN CONTROL
// CERTIFIES COLD. NOTHING BELOW WAS RELAXED.
// =============================================================================
// This is recorded here rather than in a report because the next reader of these
// four failures needs the decomposition, not a re-derivation of it.
//
// THE MACHINE STATE WAS VALIDATED, NOT ASSUMED - the same two controls
// param_perf_test.cpp's own cold-dataset banner uses:
//   * SC-008 arm 1 calibrator      78.55 / 73.55 ns  vs the 2026-08-02
//                                                    fresh-boot cold reference
//                                                    WORST of 82.40 ns (:160)
//                                                    -> 0.95x / 0.89x, i.e. at
//                                                    or BELOW the cold
//                                                    reference, not above it;
//   * Phase 7's own SC-001 case (dsp_systems_tests.exe
//     "SeraphisEngine_FullPolyCpuBudget", which contains NO Phase 9/10/11 code)
//                                  19.283 %          inside Phase 7's recorded
//                                                    18.34 %-20.07 % band.
// An EARLIER pass read 3.7e6-4.6e6 on these same arms; those runs were host
// contention (Phase 10's compliance already reversed exactly that shape once).
// The figures below are the idle ones and they still breach.
//
// THE COLD DATASET - TWO consecutive idle passes, every figure a best-of-16:
//                                             run 1      run 2      WORST
//   chain only, 107-row surface, UNDIVIDED   20.87 %    22.04 %    22.04 % PASS
//     (Seraphis_FullPoly_CpuBudget_WithFullSurface - engine + reverb +
//      processOutputStage, no Processor, NO effects stage)
//   effects stage alone at maxima             0.4475 %   0.4484 %   0.4484 % PASS
//     (effects_perf_test.cpp SC-013)
//   whole-process(), defaults, undivided     11.89 %    12.13 %
//   whole-process(), defaults, 8 x 64        13.88 %    12.56 %
//     (param_perf_test.cpp SC-008 arm 3; its subdivision ratio is itself noisy,
//      1.168x then 1.036x, so it is reported and never used as a bound)
//   -------------------------------------------------------------------------
//   SC-009(a)  whole-process(), gate OPEN    30.69 %    31.74 %    31.74 % BREACH
//   SC-010(b)  whole-process(), gate CLOSED  31.19 %    31.30 %    31.30 % BREACH
//   SC-014 a7  whole-process() + 64 re-push  31.68 %    33.32 %    33.32 % BREACH
//   SC-031     whole-process() + gesture     30.99 %    30.93 %    30.99 % BREACH
//
// WHAT THAT ARITHMETIC SAYS, and it is the whole finding:
//   1. THE GATE-CLOSED FIGURE IS NOT BELOW THE GATE-OPEN ONE: 31.19 vs 30.69 on
//      run 1, 31.30 vs 31.74 on run 2 - i.e. it lands on either side of it
//      across two passes. The producer's own cost is therefore below the
//      run-to-run noise floor, measured twice. SC-010's ACTUAL CLAIM ("the
//      producer costs nothing when the editor is closed") is TRUE. What fails
//      is the absolute comparison, on a path with no Phase 11 code in it.
//   2. Phase 11's whole measurable audio-thread cost is the two deltas against
//      SC-009(a) in the SAME run: the 64-override re-push (arm 7) is +0.99 then
//      +1.58 points; the 30 Hz in-flight gesture (SC-031, i.e. D1/FR-033a) is
//      +0.30 then -0.81, i.e. INSIDE the noise. Worst case 1.88 points, which
//      is inside the 2.68 points spec premise 6 budgeted for it.
//   3. The breach is 6.7-8.3 points and it sits in whole-process() with the
//      gate closed and no gesture: chain (22.04 %) + effects (0.4484 %) =
//      22.5 %, against 31.74 % measured. The ~9.2-point remainder is Processor
//      plumbing - the 8 x 64 control-chunk subdivision and the per-slice
//      parameter fan-out over a maxed 107-row surface - i.e. Phase 8-10 code.
//
// WHY THE SPEC'S PREMISE IS THE THING THAT IS WRONG, not this measurement:
// spec premise 6 states "the audio-thread headroom is 2.68 percentage points"
// and derives it from Phase 10's SC-014 worst of 2 380 980 ns = 22.32 %. THAT
// FIGURE IS THE CHAIN-ONLY SUBJECT (this banner's own paragraph above says so).
// SC-009(a), SC-010(b), SC-014 arm 7 and SC-031 then measure WHOLE-process(),
// which strictly CONTAINS the chain plus the effects stage plus the slice loop
// plus the plumbing. The 2.68 points of headroom do not exist at the subject
// these four criteria measure, and SC-010(b) in particular - a whole-process()
// figure against a ceiling derived from a chain-only baseline - is UNSATISFIABLE
// BY CONSTRUCTION on any machine.
//
// WHY THE PRE-DECLARED REMEDIES CANNOT CLOSE IT. Each arm's banner names its own
// lever, and all of them are measured above:
//   * "make the producer cheaper" (SC-009)  - the producer is already free by
//                                             fact 1. Bounded by 0 points.
//   * "make the fan-out cheaper" (SC-014)   - bounded by 1.58 points.
//   * "narrow spectralRetryMask_" (SC-031)  - bounded by 0.30 points.
// Their SUM is 1.88 points against a 6.7-point breach. NO COMBINATION OF THE
// SANCTIONED LEVERS PASSES THESE ARMS, which is precisely why this is escalated
// rather than patched - the same call Phase 9's T028 made
// (param_perf_test.cpp:113-142) when its remedy list could not reach its gate.
//
// WHAT WAS **NOT** DONE, explicitly, because it is the tempting move here:
// kFullPolyCeilingNs, kClosedGateCeilingNs, kBaselineFullPolyNs and every
// REQUIRE below are UNTOUCHED. No arm was retagged, skipped or deleted. The
// roadmap's budget is "8 voices, EVERYTHING ON, <= 25 % of one core" (roadmap
// line 313) and whole-process() at that operating point is the honest subject
// for it; the finding is that the shipped plugin is at 31.7 %, which is a real
// breach that no criterion before Phase 11 ever measured. Owner's call, not a
// constant's.
//
// PROTOCOL SHORTFALL, STATED RATHER THAN HIDDEN: the discipline is worst-of-
// SEVEN on a fresh-boot idle machine. This is TWO cold passes. It is nowhere
// near the boundary - the breach is 6.7-8.3 points against a run-to-run band
// the 2026-08-02 cold dataset measured at ~1.0 point - so a seven-run set would
// not change the verdict. But any RESTATED baseline (see the owner's ruling 2
// in the spec's OE-1) MUST be pinned from a full seven-run set, not from these.
//
// =============================================================================
// RULING (2026-08-04, PHASE OWNER): "HYBRID". THE BANNER ABOVE IS HISTORY FROM
// HERE DOWN - IT IS KEPT VERBATIM BECAUSE IT IS THE EVIDENCE BASE THE RULING
// CITES, NOT BECAUSE THE FOUR ARMS STILL READ THAT WAY.
// =============================================================================
// (a) SC-009(a), SC-014 arm 7 and SC-031 are RESTATED AS DIFFERENTIAL criteria.
//     Each now measures its own Phase 11 feature's MARGINAL whole-process()
//     cost, as a delta against a whole-process() baseline arm measured IN THE
//     SAME TEST CASE, on the same warm fixture:
//        SC-009(a)     gate OPEN        minus  gate CLOSED    (the producer)
//        SC-014 arm 7  64-override sweep minus same sweep, no overrides
//        SC-031        gesture in flight minus no gesture
//     A same-run delta needs no pinned absolute baseline and is robust to the
//     machine state that made the two-pass dataset above ambiguous - which is
//     exactly what fact 2 of that dataset already demonstrated by reading the
//     Phase 11 cost off same-run deltas rather than off the absolutes.
// (b) SC-010(b) is RESTATED against a whole-process() ABSOLUTE baseline (the
//     same subject it measures - the chain-only baseline it compared against was
//     unsatisfiable by construction). The new constant is kBaselineWholeProcessNs
//     and it is PROVISIONAL: the seven-run fresh-boot cold set the protocol
//     demands DOES NOT EXIST YET, so kSc010BaselinePinned is FALSE and the
//     absolute comparison REPORTS instead of gating - the exact mechanism
//     param_perf_test.cpp:2156-2175 defines and T028 used. kRegressionFactor
//     stays 1.15. SC-010 ARM (a) IS UNCHANGED and still gates hard.
// (c) THE ABSOLUTE 25 % PROMISE IS NOT DROPPED. It moves to a NEW roadmap phase,
//     "Phase 11.5: Processor whole-process() optimization"
//     (specs/Seraphis-roadmap.md), which owns the ~9.2-point inherited remainder
//     (the 8 x 64 slice loop + per-slice parameter fan-out, i.e. Phase 8-10
//     plumbing) and which PHASE 12 MUST NOT SHIP BEFORE. kFullPolyCeilingNs is
//     still computed and still REPORTED by every arm below; it is simply no
//     longer a Phase 11 gate, because Phase 11 is not the mechanism.
//
// WHAT DID NOT CHANGE: kFullPolyCeilingNs, kSnapshotStageBudgetNs,
// kRegressionFactor, kBaselineFullPolyNs, kClosedGateCeilingNs, every tag, and
// every other REQUIRE in this file - including SC-009(b)'s 0.10 % stage budget
// and SC-010(a)'s hard zero.
// =============================================================================

namespace {

// -----------------------------------------------------------------------------
// The anchors
// -----------------------------------------------------------------------------

/// 10 666 666.7 ns - one 512-sample block at 48 kHz. The constant every Seraphis
/// perf TU derives rather than re-types (param_perf_test.cpp:384).
constexpr double kBlockBudgetNs = (static_cast<double>(kBlockSize) / kSr48) * 1.0e9;
/// SC-009(a) / SC-014 arm 7 / SC-031: 25 % of one core (param_perf_test.cpp:392).
constexpr double kFullPolyCeilingNs = kBlockBudgetNs * 0.25;
/// SC-009(b): 0.10 % of one core = 10 666.67 ns/block - the same shape and figure
/// Phase 10 budgets its default effects stage at (effects_perf_test.cpp:262).
constexpr double kSnapshotStageBudgetNs = kBlockBudgetNs * 0.001;
/// param_perf_test.cpp:395 - the run-time gate multiplier.
constexpr double kRegressionFactor = 1.15;
/// param_perf_test.cpp:472 - PINNED 2026-08-02, CEILING-DERIVED, and already the
/// maximum both of that file's static_asserts admit.
constexpr double kBaselineFullPolyNs = 2318840.0;
/// SC-010(b)'s ORIGINAL ceiling, spelled exactly as the criterion used to state
/// it: 1.15 x 2 318 840 ns.
///
/// SUPERSEDED by the 2026-08-04 ruling (b) and kept for the audit trail plus its
/// static_assert. It is a CHAIN-ONLY baseline (param_perf_test.cpp's hand-built
/// engine/reverb pair) used as the ceiling for a WHOLE-process() measurement,
/// which is the "unsatisfiable by construction" finding OE-1 records. Nothing
/// asserts against it any more; kBaselineWholeProcessNs below is what SC-010(b)
/// reports against now.
constexpr double kClosedGateCeilingNs = kBaselineFullPolyNs * kRegressionFactor;

// -----------------------------------------------------------------------------
// RULING (b) - SC-010(b)'s RESTATED, WHOLE-process() ABSOLUTE BASELINE
// -----------------------------------------------------------------------------

/// PROVISIONAL - NOT PINNED. 31.74 % of one core = 3 385 600 ns/block, the WORST
/// whole-process() figure in the two-pass cold table in this file's banner (the
/// SC-009(a) gate-OPEN run-2 reading; the gate-CLOSED worst is 31.30 %, and by
/// fact 1 of that dataset the two are the same quantity inside the noise, so the
/// WORSE of the pair is the conservative pick for a baseline that gates a closed
/// gate).
///
/// WHY IT IS PROVISIONAL, AND WHAT WOULD MAKE IT REAL: the repo's measurement
/// protocol pins a baseline from a SEVEN-run fresh-boot idle dataset
/// (param_perf_test.cpp:144-207). That dataset does not exist for this subject -
/// the banner's table is TWO passes, and the machine has been under load since.
/// Until it does, kSc010BaselinePinned below is false and the absolute arm
/// REPORTS rather than gates.
constexpr double kBaselineWholeProcessNs = 3385600.0;

/// FALSE, deliberately: a placeholder must not be able to pass OR fail anything
/// (param_perf_test.cpp:2156-2175's invariant, and the mechanism kSc009BaselinePinned
/// used at param_perf_test.cpp:418 while SC-009's own baseline was unpinned).
/// FLIP TO TRUE ONLY after a seven-run fresh-boot cold set has been recorded in
/// this banner and kBaselineWholeProcessNs re-derived from its WORST.
constexpr bool kSc010BaselinePinned = false;

/// SC-010(b)'s restated gate: 1.15 x the whole-process() baseline. kRegressionFactor
/// is untouched by the ruling.
constexpr double kWholeProcessGateNs = kBaselineWholeProcessNs * kRegressionFactor;

// NOLINTBEGIN(modernize-use-std-numbers) -- 0.3174 is 31.74 % of one core (the cold-table
// figure this constant transcribes), NOT an approximation of 1/pi; "fixing" it to
// std::numbers::inv_pi would silently corrupt the baseline.
static_assert(kBaselineWholeProcessNs > (kBlockBudgetNs * 0.3174) - 1.0
                  && kBaselineWholeProcessNs < (kBlockBudgetNs * 0.3174) + 1.0,
              "kBaselineWholeProcessNs IS 31.74 % of one core, recomputed from the block period "
              "rather than re-typed. If this fails the literal has drifted from the cold "
              "table it was transcribed from");
// NOLINTEND(modernize-use-std-numbers)
static_assert(!kSc010BaselinePinned,
              "kBaselineWholeProcessNs is PROVISIONAL. Flipping kSc010BaselinePinned to true "
              "means a seven-run fresh-boot cold dataset exists and is recorded in this file's "
              "banner - delete this static_assert in the same change that records it");

// -----------------------------------------------------------------------------
// RULING (a) - THE THREE DIFFERENTIAL BOUNDS
// -----------------------------------------------------------------------------
// Every bound below is a delta between two whole-process() figures measured in
// the SAME test case, so none of them needs a pinned absolute baseline.
//
// A UNIT DISCREPANCY IN THE RULING TEXT, RECORDED RATHER THAN SILENTLY RESOLVED.
// The ruling states the last two bounds twice over: once as "2.68 points worth"
// and "1.0 point", and once as parenthetical figures "71467 ns/block" and
// "26667 ns/block". Those parentheticals are 2.68 % and 1.00 % of the 25 %
// CEILING (2 666 666.7 ns), not of one core (10 666 666.7 ns), i.e. 0.67 and
// 0.25 points. Against the ruling's own governing clause - "choose per-arm
// differential ceilings FROM THE SPEC'S OWN MEASURED DELTAS with honest
// headroom" - the parentheticals fail on the very data they are derived from:
// the banner's measured re-push delta is +0.99 / +1.58 points (105 600 /
// 168 533 ns) and the measured gesture delta is +0.30 / -0.81 points (32 000 /
// -86 400 ns). The bounds below therefore take the ruling's stated UNIT -
// points of one core, which is how every other figure in this phase, this file
// and the spec is expressed, and which is the unit the ruling's own first bound
// (10 666 ns = 0.10 point of one core) uses. FLAGGED TO THE PHASE OWNER: if the
// parentheticals were meant literally, these two constants are the only edit.
// -----------------------------------------------------------------------------

/// SC-009(a) restated: the PRODUCER's marginal whole-process() cost, gate open
/// minus gate closed. The bound is premise 6's stage budget - 0.10 % of one core,
/// 10 666.7 ns/block - i.e. the producer may cost no more inside whole-process()
/// than SC-009(b) already requires the snapshot stage to cost on its own timer.
/// This is the strictest of the three and it is the ruling's own wording:
/// "producer <= noise floor is the SC-009 claim".
constexpr double kProducerDeltaBoundNs = kSnapshotStageBudgetNs;

/// SC-014 arm 7 restated: the RE-PUSH's marginal whole-process() cost, the
/// 64-override macro sweep minus the same sweep with no overrides authored.
/// 2.68 POINTS OF ONE CORE = 285 866.7 ns/block - premise 6's whole headroom
/// figure, against a measured worst delta of 1.58 points, i.e. 1.70x headroom.
constexpr double kRepushDeltaBoundNs = kBlockBudgetNs * 0.0268;

/// SC-031 restated: the in-flight GESTURE's marginal whole-process() cost, the
/// 30 Hz kind-1 drag minus no gesture at all. 1.00 POINT OF ONE CORE =
/// 106 666.7 ns/block, against a measured delta of +0.30 / -0.81 points, i.e.
/// the bound is 3.3x the worst positive reading and the gesture's own cost has
/// already been measured on BOTH sides of zero.
constexpr double kGestureDeltaBoundNs = kBlockBudgetNs * 0.01;

static_assert(kProducerDeltaBoundNs > 10666.0 && kProducerDeltaBoundNs < 10667.0,
              "SC-009(a) differential bound is 0.10 % of one core - the same figure SC-009(b) "
              "budgets the stage at, and the same number premise 6 states");
static_assert(kRepushDeltaBoundNs > 285866.0 && kRepushDeltaBoundNs < 285867.0,
              "SC-014 arm 7 differential bound is 2.68 POINTS OF ONE CORE");
static_assert(kGestureDeltaBoundNs > 106666.0 && kGestureDeltaBoundNs < 106667.0,
              "SC-031 differential bound is 1.00 POINT OF ONE CORE");

static_assert(kBlockBudgetNs > 10666666.0 && kBlockBudgetNs < 10666667.0,
              "the 512/48 kHz block period is 10 666 666.7 ns");
static_assert(kFullPolyCeilingNs > 2666666.0 && kFullPolyCeilingNs < 2666667.0,
              "SC-009(a): 25 % of one core is 2 666 666.7 ns/block - the same number "
              "param_perf_test.cpp:392 derives. NOT A LEVER at any point in this phase");
static_assert(kSnapshotStageBudgetNs > 10666.0 && kSnapshotStageBudgetNs < 10667.0,
              "SC-009(b): 0.10 % of one core is 10 667 ns/block");
static_assert(kClosedGateCeilingNs <= kFullPolyCeilingNs,
              "SC-010(b)'s ceiling is Phase 10's pinned baseline x 1.15, which param_perf_test.cpp"
              ":462-465 records is the largest value inside the 25 % ceiling. If this fails, the "
              "copied baseline has drifted from the pinned one - fix the copy, never the ceiling");

// -----------------------------------------------------------------------------
// The operating point
// -----------------------------------------------------------------------------

/// FR-040's shipped default polyphony, and the gate every Seraphis CPU criterion
/// is stated at. The shipped default is index 7 -> 8 voices (global_params.h:143),
/// so no case below writes kPolyphonyId: it presses eight distinct notes and
/// asserts getActiveVoiceCount() instead.
constexpr std::size_t kPerfPolyphony = 8;
static_assert(kPerfPolyphony <= Krate::DSP::SeraphisEngine::kMaxVoices,
              "the scenario must fit the pool");

constexpr int kPerfTrials = 16;             ///< best-of-16, the protocol's number
constexpr int kPerfBlocksPerTrial = 100;    ///< ~1.07 s of audio per trial
/// ~6.4 s: past the atmosphere's capture ring, the body's crossfade, the cloud's
/// attack, the reverb build-up, every smoother in the chain and the send's
/// one-chunk accumulator pipeline (effects_perf_test.cpp:317-320).
constexpr int kPerfWarmupBlocks = 600;
/// Upper bound on the blocks spent ARMING the atmosphere freeze. One slot is
/// retried per control chunk and a capture is a no-op until that voice's ring
/// holds a whole analysis window, so this is far more than the fan-out needs
/// (param_perf_test.cpp:2542).
constexpr int kFreezeArmBlocks = 200;

// -----------------------------------------------------------------------------
// Instruments
// -----------------------------------------------------------------------------

/// Finite check WITHOUT std::isnan: the macOS leg builds with -ffast-math, which
/// folds it. Inspect the IEEE-754 exponent field instead
/// (param_perf_test.cpp:598-602).
[[nodiscard]] bool isFinitePerfValue(double v) noexcept {
    const auto f = static_cast<float>(v);
    std::uint32_t bits = 0;
    std::memcpy(&bits, &f, sizeof(bits));
    return (bits & 0x7F800000u) != 0x7F800000u;
}

/// Best-of-N over WHOLE-process() block time, with an UNTIMED per-block prelude.
///
/// The prelude exists because two arms below have to drive something between
/// blocks that is not part of the subject: SC-014 arm 7 writes a macro automation
/// point (a `setParam` into the fixture's queue container) and SC-031 sends an
/// EditMessage through `notify()`, which allocates a HostMessage on the MESSAGE
/// thread. Neither is FR-040's subject and neither may be charged to a
/// whole-`process()` figure, so the clock brackets `render()` alone.
///
/// COST OF THE PER-BLOCK BRACKET, disclosed rather than assumed away: one
/// steady_clock::now() pair per block against a ~2.3 ms block, i.e. ~2e-5 of the
/// measured quantity. It also rejects LESS noise than one window per trial would,
/// because a preemption inside any single block lands in the trial's sum - which
/// is the conservative direction (a higher figure against a fixed ceiling).
template <typename Prelude, typename Render>
[[nodiscard]] double bestNsPerBlock(int trials, int blocksPerTrial, const Prelude& prelude,
                                    const Render& render) {
    double best = std::numeric_limits<double>::max();
    for (int trial = 0; trial < trials; ++trial) {
        double elapsedNs = 0.0;
        for (int b = 0; b < blocksPerTrial; ++b) {
            prelude();
            const auto start = std::chrono::steady_clock::now();
            render();
            const auto end = std::chrono::steady_clock::now();
            elapsedNs += std::chrono::duration<double, std::nano>(end - start).count();
        }
        best = std::min(best, elapsedNs / static_cast<double>(blocksPerTrial));
    }
    return best;
}

/// Two whole-process() configurations measured INSIDE ONE TRIAL LOOP, A then B in
/// every trial, each keeping its own best-of-N.
///
/// WHY INTERLEAVED RATHER THAN TWO SEQUENTIAL CALLS. The SC-009 differential's
/// bound is 0.10 point of one core - ~0.3 % of the measured quantity - which is
/// well inside the drift two back-to-back best-of-16 windows can accumulate on a
/// warm machine (the banner's own run-to-run band is ~1.0 point). Alternating the
/// two configurations trial by trial makes any monotone drift common-mode: it
/// lands in BOTH bests, and the delta subtracts it out.
///
/// THE OTHER TWO DIFFERENTIALS ARE MEASURED SEQUENTIALLY, AND WHAT THAT COSTS IS
/// STATED RATHER THAN HIDDEN. SC-014 arm 7's two configurations are not free to
/// toggle - panBits cannot be un-authored, there being no clearing EditMessage -
/// so its baseline has to be the window before the overrides exist; SC-031's
/// baseline is defined as the STATIC slot set, i.e. before any edit has been
/// sent, for the same "measure the thing the criterion names" reason. Both
/// deltas therefore carry the drift term this pairing removes, empirically
/// +-1.5 points on this machine. That is why their bounds are 27x and 10x looser
/// than SC-009's, and it is a real weakness of those two arms: a drift-favoured
/// window can mask up to ~1.5 points of genuine marginal cost. A negative delta
/// on either is evidence of the drift, not of a free feature.
///
/// THE WITHIN-TRIAL ORDER IS COUNTERBALANCED (A,B then B,A on alternating
/// trials), and that is load-bearing rather than fussy. A FIXED order does not
/// cancel drift, it CONVERTS it into a bias with a sign: the two sequential arms
/// below both measured their SECOND window ~0.5 and ~1.5 points FASTER than their
/// first on this machine, and a fixed A-then-B loop hands that whole trend to B.
/// Measured: with a fixed order this differential read +0.95 points on a subject
/// whose own two-pass cold data puts it inside the noise floor. Alternating gives
/// each arm an equal number of first and second positions, so a monotone drift
/// lands in both bests and subtracts out.
///
/// `enterA` / `enterB` run OUTSIDE the clock, once per window, and are the only
/// thing that distinguishes the two arms.
struct PairedNs {
    double a = 0.0;
    double b = 0.0;
};

template <typename Enter, typename Render>
[[nodiscard]] double timedWindowNs(int blocksPerTrial, const Enter& enter, const Render& render) {
    enter();
    double ns = 0.0;
    for (int b = 0; b < blocksPerTrial; ++b) {
        const auto start = std::chrono::steady_clock::now();
        render();
        const auto end = std::chrono::steady_clock::now();
        ns += std::chrono::duration<double, std::nano>(end - start).count();
    }
    return ns / static_cast<double>(blocksPerTrial);
}

template <typename EnterA, typename EnterB, typename Render>
[[nodiscard]] PairedNs bestNsPerBlockPaired(int trials, int blocksPerTrial, const EnterA& enterA,
                                            const EnterB& enterB, const Render& render) {
    PairedNs out{.a = std::numeric_limits<double>::max(), .b = std::numeric_limits<double>::max()};
    for (int trial = 0; trial < trials; ++trial) {
        if ((trial % 2) == 0) {
            out.a = std::min(out.a, timedWindowNs(blocksPerTrial, enterA, render));
            out.b = std::min(out.b, timedWindowNs(blocksPerTrial, enterB, render));
        } else {
            out.b = std::min(out.b, timedWindowNs(blocksPerTrial, enterB, render));
            out.a = std::min(out.a, timedWindowNs(blocksPerTrial, enterA, render));
        }
    }
    return out;
}

/// The report row every DIFFERENTIAL arm prints (ruling (a), 2026-08-04). It
/// carries the subject, the same-run baseline, the delta in both ns and points,
/// and the bound - so a reader sees the marginal cost that IS this phase's, and
/// the absolute figure that is Phase 11.5's, side by side and never conflated.
[[nodiscard]] std::string deltaRow(const char* label, double subjectNs, double baselineNs,
                                   double boundNs) {
    const double deltaNs = subjectNs - baselineNs;
    std::ostringstream os;
    os << label << "\n"
       << "  block budget : " << kBlockBudgetNs << " ns  (512 samples @ 48 kHz)\n"
       << "  subject      : " << subjectNs << " ns/block  ("
       << ((subjectNs / kBlockBudgetNs) * 100.0) << " % of one core)\n"
       << "  same-run base: " << baselineNs << " ns/block  ("
       << ((baselineNs / kBlockBudgetNs) * 100.0) << " % of one core)\n"
       << "  DELTA        : " << deltaNs << " ns/block  ("
       << ((deltaNs / kBlockBudgetNs) * 100.0) << " points)   <- THE CRITERION\n"
       << "  bound        : " << boundNs << " ns/block  ("
       << ((boundNs / kBlockBudgetNs) * 100.0) << " points)\n"
       << "  headroom     : " << (boundNs - deltaNs) << " ns  ("
       << (((boundNs - deltaNs) / kBlockBudgetNs) * 100.0) << " points)\n"
       << "  ABSOLUTE (reported, NOT gated by this phase - roadmap Phase 11.5 owns it): "
       << ((subjectNs / kBlockBudgetNs) * 100.0) << " % vs the 25 % ceiling "
       << kFullPolyCeilingNs << " ns";
    return os.str();
}

/// The baseline clause, in ONE place, and it is param_perf_test.cpp:2163-2175's
/// verbatim shape: it GATES where the number is pinned and REPORTS where it is
/// not, so an unpinned placeholder can neither pass nor fail anything. `pinned`
/// is a parameter rather than a constant expression precisely so both branches
/// are live code.
void checkAgainstWholeProcessBaseline(const char* name, double measuredNs, double baselineNs,
                                      bool pinned) {
    if (pinned) {
        INFO(name << ": measured " << measuredNs << " ns vs gate "
                  << (baselineNs * kRegressionFactor) << " ns");
        REQUIRE(measuredNs <= baselineNs * kRegressionFactor);
    } else {
        std::ostringstream os;
        os << name
           << ": *** PROVISIONAL - PIN FROM A 7-RUN FRESH-BOOT COLD SET BEFORE RELEASE ***\n"
           << "  measured        : " << measuredNs << " ns/block  ("
           << ((measuredNs / kBlockBudgetNs) * 100.0) << " % of one core)\n"
           << "  provisional base: " << baselineNs << " ns/block  ("
           << ((baselineNs / kBlockBudgetNs) * 100.0)
           << " % of one core)  <- from the TWO-pass cold table in this file's banner\n"
           << "  gate WOULD be   : " << (baselineNs * kRegressionFactor) << " ns/block  (x"
           << kRegressionFactor << ")\n"
           << "  this run vs it  : "
           << ((measuredNs <= baselineNs * kRegressionFactor) ? "under" : "OVER")
           << " the provisional gate\n"
           << "  STATUS          : REPORTED, NOT GATED. A placeholder must not be able to pass "
              "or fail anything (param_perf_test.cpp:2156-2175). SC-010 ARM (a) still gates "
              "hard on this run, and so does every other REQUIRE in this case.";
        WARN(os.str());
        SUCCEED("SC-010(b) absolute arm reported against a PROVISIONAL baseline - see the WARN");
    }
}

/// The report row every arm prints, in Phase 10's shape
/// (effects_perf_test.cpp's reportRow) so a run that transcribes figures into the
/// phase notes copies them rather than re-deriving them.
[[nodiscard]] std::string perfRow(const char* label, double ns, double budgetNs) {
    std::ostringstream os;
    os << label << "\n"
       << "  block budget : " << kBlockBudgetNs << " ns  (512 samples @ 48 kHz)\n"
       << "  measured     : " << ns << " ns/block  (" << ((ns / kBlockBudgetNs) * 100.0)
       << " % of one core)\n"
       << "  ceiling      : " << budgetNs << " ns/block  ("
       << ((budgetNs / kBlockBudgetNs) * 100.0) << " % of one core)\n"
       << "  headroom     : " << (budgetNs - ns) << " ns  ("
       << (((budgetNs - ns) / kBlockBudgetNs) * 100.0) << " points)";
    return os.str();
}

/// THE OPERATING POINT, and what it is NOT.
///
/// This is deliberately NOT param_perf_test.cpp's 107-row `kNonDefaultTable`.
/// That table is a file-local structure of that TU with a per-row justification
/// and a static_assert tying it to the registered ID set; forking it here would
/// produce two tables that drift, and T023's file list forbids editing that TU's
/// constants. What is reproduced instead is its MEASURED DOMINANT COST plus Phase
/// 10's, each row cited to the place that established it:
///
///   * the FROZEN atmosphere at Freeze Mix 1.0 - the rows that file's banner
///     names as the reason SC-009 reads ~20.9 % rather than Phase 7's ~19 %,
///     priced there at 1.048 % -> 1.440 % per voice, i.e. +3.1 points at eight
///     (param_perf_test.cpp:136-142, :2524-2538). `renderFreezeLayer`'s
///     `settledDry` bypass cannot engage at mix 1.0.
///   * Cloud Richness 1.0 - N(1) = 64 active partials, which is also what makes
///     the snapshot's read loop run its full 64 iterations rather than a short
///     one (param_perf_test.cpp:2573-2574).
///   * Morph State Count 4 - SpectralMorphEngine::kMaxStates, so the spectral
///     fan-out writes four slots rather than the default two
///     (morph_params.h:90, :100-101).
///   * the sixteen effects rows at their maxima - Phase 10's SC-013 subject,
///     verbatim from effects_perf_test.cpp:1232-1252.
///   * the Aether rows whose cost is structural (density, spectral diffusion,
///     both shimmer voices, the bloom send) at maximum.
///
/// Applied ONCE, before the warm-up, and never again inside a timed region -
/// which is how the hand-built subject applies its surface too (`buildChainSubject`
/// configures, then renders). A per-block re-write would charge 30+ parameter
/// queues to every measured block and measure the host boundary instead.
void writeDominantSurface(Fixture& fx) {
    // --- Harmonic Cloud: all 64 partials sounding --------------------------
    fx.setParam(Seraphis::kCloudRichnessId, 1.0);

    // --- Spectral Morph: four slots, not two -------------------------------
    fx.setParam(Seraphis::kMorphStateCountId, 1.0);

    // --- Granular Atmosphere: ARMED and fully wet --------------------------
    fx.setParam(Seraphis::kAtmosLevelId, 1.0);
    fx.setParam(Seraphis::kAtmosDensityId, 1.0);
    fx.setParam(Seraphis::kAtmosFreezeMixId, 1.0);
    fx.setParam(Seraphis::kAtmosFreezeId, 1.0);

    // --- Aether: the structurally expensive controls -----------------------
    fx.setParam(Seraphis::kAetherMixId, 1.0);
    fx.setParam(Seraphis::kAetherDensityId, 1.0);
    fx.setParam(Seraphis::kAetherSpectralDiffusionId, 1.0);
    fx.setParam(Seraphis::kAetherShimmerOctaveId, 1.0);
    fx.setParam(Seraphis::kAetherShimmerFifthId, 1.0);
    fx.setParam(Seraphis::kAetherBloomSendId, 1.0);

    // --- Effects at maxima (effects_perf_test.cpp:1232-1252, verbatim) -----
    fx.setParam(Seraphis::kFxSaturationId, 1.0);
    fx.setParam(Seraphis::kFxDelayMixId, 1.0);
    fx.setParam(Seraphis::kFxDelayTimeId, 1.0);
    fx.setParam(Seraphis::kFxDelaySpreadId, 1.0);
    fx.setParam(Seraphis::kFxDelaySpreadDirectionId, 1.0);
    fx.setParam(Seraphis::kFxDelayFeedbackId, 1.0);
    fx.setParam(Seraphis::kFxDelayTiltId, 1.0);
    fx.setParam(Seraphis::kFxDelayDiffusionId, 1.0);
    fx.setParam(Seraphis::kFxDelayWidthId, 1.0);
    fx.setParam(Seraphis::kFxDelaySyncId, 1.0);
    fx.setParam(Seraphis::kFxDelaySyncNoteId, 1.0);
    fx.setParam(Seraphis::kFxSpectralFreezeId, 1.0);
    fx.setParam(Seraphis::kFxWidthId, 1.0);
    fx.setParam(Seraphis::kFxWanderDepthId, 1.0);
    fx.setParam(Seraphis::kFxWanderRateId, 1.0);
    fx.setParam(Seraphis::kFxAzimuthDepthId, 1.0);
}

/// Render until every voice reports a captured freeze, or `kFreezeArmBlocks`
/// blocks have passed. Returns how many of `voices` captured, so the caller
/// ASSERTS the precondition rather than assuming it: an un-asserted freeze
/// silently measures the cheaper UNFROZEN path (param_perf_test.cpp:2526-2532).
[[nodiscard]] std::size_t armAtmosphereFreeze(Fixture& fx, std::size_t voices) {
    std::size_t captured = 0;
    const Krate::DSP::SeraphisEngine* engine = fx.proc->engineForTest();
    if (engine == nullptr) {
        return 0;
    }
    for (int i = 0; (i < kFreezeArmBlocks) && (captured < voices); ++i) {
        (void)fx.processBlock(kBlock);
        captured = 0;
        for (std::size_t v = 0; v < voices; ++v) {
            if (engine->getVoice(v).isFreezeCaptured()) {
                ++captured;
            }
        }
    }
    return captured;
}

/// Bring a fixture to the measured operating point: prepared, surface applied,
/// eight voices held, atmosphere frozen, everything warm. The gate is NOT touched
/// here - each case owns its own gate state, which is the only difference between
/// SC-009 and SC-010.
///
/// THE ORDER IS param_perf_test.cpp:2520-2550's, and it is load-bearing rather
/// than stylistic: the warm-up runs FIRST and the freeze is armed AFTER it.
/// setAtmosphereFreeze(true) only ARMS - a capture is a documented no-op until
/// that voice's ring holds a whole analysis window, and the ring is ~4 s deep, so
/// an arming loop placed before the ~6.4 s warm-up would run out of blocks and
/// the REQUIRE below would fail on a CORRECT build.
void bringToOperatingPoint(Fixture& fx) {
    writeDominantSurface(fx);
    pressChord(fx, kPerfPolyphony);
    REQUIRE(fx.processBlock(kBlock) == Steinberg::kResultOk);

    bool warmupOk = true;
    for (int b = 0; b < kPerfWarmupBlocks; ++b) {
        // NOT `warmupOk = warmupOk && processBlock(...)`: && short-circuits, so a
        // single non-ok result would silently stop rendering the rest of the
        // warm-up. The call is always made and the flag is folded afterwards.
        const bool ok = (fx.processBlock(kBlock) == Steinberg::kResultOk);
        warmupOk = warmupOk && ok;
    }
    REQUIRE(warmupOk);

    REQUIRE(armAtmosphereFreeze(fx, kPerfPolyphony) == kPerfPolyphony);
}

/// The preconditions every arm shares, CHECKED rather than assumed: a figure for
/// a configuration that is not the one named would be the wrong number wearing
/// the right label (param_perf_test.cpp:2557-2560).
void requireOperatingPoint(Fixture& fx) {
    const Krate::DSP::SeraphisEngine* engine = fx.proc->engineForTest();
    REQUIRE(engine != nullptr);
    REQUIRE(engine->getActiveVoiceCount() == kPerfPolyphony);
    for (std::size_t v = 0; v < kPerfPolyphony; ++v) {
        INFO("operating-point precondition, voice " << v);
        // The atmosphere is frozen AND fully wet - both halves, because
        // "captured" alone would still pass with a bypassed layer.
        REQUIRE(engine->getVoice(v).isFreezeCaptured());
        REQUIRE(static_cast<double>(engine->getVoice(v).atmos().getFreezeMix())
                == Approx(1.0).margin(1e-4));
        // Richness 1.0 is N(1) = 64 active partials, so the snapshot's read loop
        // runs its full 64 iterations.
        REQUIRE(engine->getVoice(v).cloud().getActivePartialCount()
                == Krate::DSP::HarmonicCloud::kMaxPartials);
    }
    // The effects send is genuinely running - otherwise the whole-process()
    // figure would be budgeting an idle stage (effects_perf_test.cpp:1292-1294).
    REQUIRE(fx.proc->sendChunkCountForTest() > 0u);
}

/// SC-014 arm 7's authored pan for partial `i`: a ramp across the whole legal
/// range so no two partials share a value and nothing can be constant-folded.
/// A FUNCTION rather than a literal because the arm both AUTHORS it and READS IT
/// BACK OFF THE ENGINE, and those two must be the same number by construction.
[[nodiscard]] constexpr float authoredPanFor(std::size_t i) noexcept {
    return -1.0f + (2.0f * static_cast<float>(i) / 63.0f);
}

}  // namespace

// =============================================================================
// SC-009 - THE PRODUCER FITS THE MEASURED HEADROOM
// =============================================================================
// (a) RESTATED 2026-08-04 (ruling (a)): the producer's MARGINAL whole-process()
//     cost at the 8-voice operating point - gate OPEN minus gate CLOSED, both
//     measured in this case's own interleaved trial loop - against
//     kProducerDeltaBoundNs (0.10 point of one core). The absolute whole-process()
//     figure is still MEASURED and still REPORTED against the 25 % ceiling; it is
//     roadmap Phase 11.5's gate, not this phase's, because OE-1 established that
//     ~9.2 of its points are inherited Phase 8-10 plumbing.
// (b) the snapshot stage ALONE, on its own scoped timer, against 0.10 % of one
//     core. UNCHANGED by the ruling and still a hard REQUIRE.
//
// THE ORDER OF THE TWO ARMS IS LOAD-BEARING. (a) runs with instrumentation OFF,
// because the stage timer is two clock reads per process() call and charging them
// to a whole-process() figure would measure the instrument. (b) turns it on
// afterwards, on the same warm fixture.
//
// IF (a) FAILS, THE PRODUCER IS MADE CHEAPER - never the bound (plan R-12): drop
// `overriddenBits`' recompute, or publish on every other call. Neither the 0.10 %
// differential bound nor the 25 % figure is a lever at any point in this phase.
// =============================================================================
TEST_CASE("Seraphis_CloudFrame_CpuBudget", "[.perf][phase11]") {
    auto fx = std::make_unique<Fixture>();
    REQUIRE(fx->prepare(kSr48, kBlock) == Steinberg::kResultOk);

    // THE GATE IS OPEN BEFORE THE WARM-UP, so every measured block publishes and
    // nothing the producer touches is cold when the clock starts.
    fx->proc->setCloudFrameGateForTest(true);
    bringToOperatingPoint(*fx);
    requireOperatingPoint(*fx);

    // The producer really ran over a live cloud, not an empty one.
    REQUIRE(fx->proc->cloudFramePublishAttemptCountForTest() > 0u);
    REQUIRE(static_cast<std::size_t>(fx->proc->lastPublishedFrameForTest().partialCount)
            == Krate::DSP::HarmonicCloud::kMaxPartials);

    // -------------------------------------------------------------------------
    // (a) THE PRODUCER'S MARGINAL COST: whole-process() GATE OPEN minus the SAME
    //     whole-process() GATE CLOSED, interleaved trial by trial.
    // -------------------------------------------------------------------------
    // The gate-closed arm here is the criterion's BASELINE, not SC-010's subject:
    // SC-010 owns its own case, on its own fixture, and nothing below compares
    // against it. What makes this delta the producer's cost and not a second
    // machine-state reading is that both figures come off ONE fixture inside ONE
    // trial loop - the two arms differ by exactly one atomic bool.
    double audioSink = 0.0;
    const auto renderBlock = [&]() {
        (void)fx->processBlock(kBlock);
        audioSink += static_cast<double>(fx->audioL()[0])
                     + static_cast<double>(fx->audioR()[kBlockSize - 1u]);
    };
    const auto openGate = [&]() { fx->proc->setCloudFrameGateForTest(true); };
    const auto closeGate = [&]() { fx->proc->setCloudFrameGateForTest(false); };

    const std::size_t attemptsBefore = fx->proc->cloudFramePublishAttemptCountForTest();
    const PairedNs paired =
        bestNsPerBlockPaired(kPerfTrials, kPerfBlocksPerTrial, openGate, closeGate, renderBlock);
    const double openGateNs = paired.a;
    const double closedGateNs = paired.b;
    const std::size_t attempts =
        fx->proc->cloudFramePublishAttemptCountForTest() - attemptsBefore;
    // Leave the gate as this case found it: arm (b) below measures the stage.
    fx->proc->setCloudFrameGateForTest(true);

    WARN(deltaRow("SC-009(a) THE PRODUCER'S MARGINAL whole-process() COST: gate OPEN minus gate "
                  "CLOSED, interleaved, same fixture (8 voices held, atmosphere FROZEN at mix "
                  "1.0, richness 1.0, morph state count 4, effects at maxima, 48 kHz / 512)",
                  openGateNs, closedGateNs, kProducerDeltaBoundNs));
    INFO("publish attempts inside the timed region = " << attempts << ", skipped = "
                                                       << fx->proc
                                                              ->cloudFrameSkippedBlockCountForTest());
    // Every GATE-OPEN timed block published - half the paired loop's blocks, and
    // EXACTLY half: a figure taken with a producer that silently stopped running
    // would make the delta below trivially zero.
    REQUIRE(attempts == static_cast<std::size_t>(kPerfTrials * kPerfBlocksPerTrial));
    // A 0 ns arm FAILS: it means the clock never ran, not that the block is free.
    REQUIRE(openGateNs > 0.0);
    REQUIRE(closedGateNs > 0.0);
    // THE CRITERION, RESTATED (ruling (a)). If this fails, the producer gets
    // cheaper - never the bound.
    REQUIRE((openGateNs - closedGateNs) <= kProducerDeltaBoundNs);

    // -------------------------------------------------------------------------
    // (b) THE SNAPSHOT STAGE ALONE
    // -------------------------------------------------------------------------
    // The scoped timer opens OUTSIDE the cloudFrameEnabled_ predicate and its
    // divisor counts EVERY process() call, not every publish (processor.cpp:3962-
    // 3973, :4093-4097). Both halves are asserted, not trusted: the divisor
    // identity below is what stops a counter that silently became per-publish -
    // which would divide by the same number here and by 8x fewer elsewhere, and
    // make the budget unfailable.
    fx->proc->setCloudFrameInstrumentedForTest(true);

    double bestStageNs = std::numeric_limits<double>::max();
    for (int trial = 0; trial < kPerfTrials; ++trial) {
        const double nsBefore = fx->proc->cloudFrameStageNsForTest();
        const std::size_t callsBefore = fx->proc->cloudFrameStageProcessCallsForTest();

        for (int b = 0; b < kPerfBlocksPerTrial; ++b) {
            renderBlock();
        }

        const double nsAfter = fx->proc->cloudFrameStageNsForTest();
        const std::size_t calls =
            fx->proc->cloudFrameStageProcessCallsForTest() - callsBefore;

        INFO("SC-009(b) trial " << trial);
        REQUIRE(calls == static_cast<std::size_t>(kPerfBlocksPerTrial));
        bestStageNs = std::min(bestStageNs, (nsAfter - nsBefore) / static_cast<double>(calls));
    }
    fx->proc->setCloudFrameInstrumentedForTest(false);

    gSink = gSink + audioSink;  // optimization barrier

    WARN(perfRow("SC-009(b) the SNAPSHOT STAGE ALONE (same operating point, gate open)",
                 bestStageNs, kSnapshotStageBudgetNs));

    // A 0 ns stage FAILS: the producer always fills 808 bytes and reads two
    // clocks, so a genuinely zero figure is a broken seam, not a free stage.
    REQUIRE(bestStageNs > 0.0);
    REQUIRE(bestStageNs <= kSnapshotStageBudgetNs);

    REQUIRE(fx->checkCanaries());
    REQUIRE(isFinitePerfValue(audioSink));
}

// =============================================================================
// SC-010 - THE PRODUCER COSTS NOTHING WHEN THE EDITOR IS CLOSED
// =============================================================================
// (a) a 60-second render with the gate false publishes NOTHING at all.
//     UNCHANGED by the 2026-08-04 ruling, and still a hard REQUIRE.
// (b) RESTATED 2026-08-04 (ruling (b)): the whole-process() figure against a
//     WHOLE-process() baseline - kBaselineWholeProcessNs x 1.15 - instead of
//     against Phase 10's CHAIN-ONLY 2 318 840 ns, which OE-1 established is
//     unsatisfiable by construction for this subject on any machine. The new
//     baseline is PROVISIONAL (no seven-run fresh-boot cold set exists yet), so
//     this arm REPORTS rather than gates until kSc010BaselinePinned flips.
//
// ARM (b) IS DELIBERATELY THE WHOLE-process() NUMBER, and the spec says why
// (spec.md:1376-1379): the snapshot-stage timer sits inside the gate, so with the
// gate closed it reads ~zero BY CONSTRUCTION regardless of what the producer
// costs. Asserting on it would measure the instrumentation's placement.
// =============================================================================
TEST_CASE("Seraphis_CloudFrame_CostsNothingWhenClosed", "[.perf][phase11]") {
    auto fx = std::make_unique<Fixture>();
    REQUIRE(fx->prepare(kSr48, kBlock) == Steinberg::kResultOk);

    // EXPLICIT, not inherited from the member's initializer: the negative control
    // must be a state this case set, so it still holds if a later phase changes
    // the default.
    fx->proc->setCloudFrameGateForTest(false);
    bringToOperatingPoint(*fx);
    requireOperatingPoint(*fx);

    // -------------------------------------------------------------------------
    // (a) 60 s, gate closed, ZERO attempts
    // -------------------------------------------------------------------------
    // kRtBlocks x 512 = 2 880 000 samples = exactly 60 s at 48 kHz, the same
    // geometry SC-011 renders above.
    double audioSink = 0.0;
    std::size_t blocksOk = 0;
    for (std::size_t b = 0; b < kRtBlocks; ++b) {
        if (fx->processBlock(kBlock) == Steinberg::kResultOk) {
            ++blocksOk;
        }
        audioSink += static_cast<double>(fx->audioL()[0])
                     + static_cast<double>(fx->audioR()[kBlockSize - 1u]);
    }
    REQUIRE(blocksOk == kRtBlocks);
    // THE CRITERION'S ARM (a). Not "small" - ZERO. The gate is the only
    // short-circuit ahead of the attempt counter (processor.cpp:3986-3988).
    REQUIRE(fx->proc->cloudFramePublishAttemptCountForTest() == 0u);
    // ...and nothing was skipped either, because nothing was attempted. This is
    // what distinguishes "the gate closed the producer" from "the producer ran
    // and found no transport", which in this headless harness look identical on
    // the attempt counter alone if the gate is ever moved below it.
    REQUIRE(fx->proc->cloudFrameSkippedBlockCountForTest() == 0u);

    // -------------------------------------------------------------------------
    // (b) whole-process(), gate closed
    // -------------------------------------------------------------------------
    const auto noPrelude = []() noexcept {};
    const auto renderBlock = [&]() {
        (void)fx->processBlock(kBlock);
        audioSink += static_cast<double>(fx->audioL()[0])
                     + static_cast<double>(fx->audioR()[kBlockSize - 1u]);
    };
    const double closedGateNs =
        bestNsPerBlock(kPerfTrials, kPerfBlocksPerTrial, noPrelude, renderBlock);
    gSink = gSink + audioSink;  // optimization barrier

    WARN(perfRow("SC-010(b) whole-process(), GATE CLOSED (same operating point). The ceiling "
                 "column is the 25 % figure, REPORTED - roadmap Phase 11.5 owns it, not this "
                 "phase; the gate for this arm is the provisional baseline below",
                 closedGateNs, kFullPolyCeilingNs));

    REQUIRE(closedGateNs > 0.0);
    // Still zero attempts after the timed region - the gate did not reopen
    // underneath the measurement.
    REQUIRE(fx->proc->cloudFramePublishAttemptCountForTest() == 0u);
    // THE CRITERION'S ARM (b), RESTATED (ruling (b)): a WHOLE-process() baseline
    // for a WHOLE-process() measurement. PROVISIONAL until a seven-run
    // fresh-boot cold set pins it, so this reports rather than gates.
    checkAgainstWholeProcessBaseline("SC-010(b)", closedGateNs, kBaselineWholeProcessNs,
                                     kSc010BaselinePinned);

    REQUIRE(fx->checkCanaries());
    REQUIRE(isFinitePerfValue(audioSink));
}

// =============================================================================
// SC-014 arm 7 - THE 64-OVERRIDE WORST-CASE RE-PUSH
// =============================================================================
// 64 authored pan overrides (every bit set), then a macro Bloom sweep so the
// COMPOSED CloudStereoSpread moves on consecutive slices. HarmonicCloud::
// setStereoSpread wipes positionOverridden_ on any value change
// (harmonic_cloud.h:535-547), so renderSlice's composed-spread tracker fires
// repushPartialOverrides() every slice (processor.cpp:2316-2326).
//
// WHAT THAT COSTS, priced rather than assumed bounded: repushPartialOverrides()
// loops all 64 partials (processor.cpp:3411-3419) and each iteration fans out
// over kMaxVoices = 16 (the ...AllVoices contract), so a single re-push is
// 64 x 16 x 2 = 2048 pan/mask writes, and the pan half reaches updatePanGains ->
// equalPowerGains, which is COS AND SIN - two trig calls, not two sqrt
// (crossfade_utils.h:50-53).
//
// Bloom is the right driver rather than ParamID 207: Bloom writes
// CloudStereoSpread through the macro matrix with amount 0.60
// (seraphis_macro_matrix.h:284-289), which is exactly the path a macro-ring
// sweep takes and the one a ParamID-keyed tracker would be blind to.
//
// RESTATED 2026-08-04 (ruling (a)): the criterion is now the re-push's MARGINAL
// whole-process() cost - this same Bloom sweep measured TWICE on one fixture,
// once with all 64 pan overrides authored and once with NONE - against
// kRepushDeltaBoundNs (2.68 points of one core). The baseline arm keeps the sweep,
// so the macro-matrix apply(), the smoother travel and the tracker comparison are
// in BOTH figures and cancel; what the delta isolates is the pan half of
// repushPartialOverrides(), i.e. the 64 x 16 equalPowerGains cos+sin calls, which
// is the only part gated on panBits (processor.cpp:3415-3418). The absolute
// figure is still reported against the 25 % ceiling, which roadmap Phase 11.5
// owns.
//
// IF THIS FAILS, THE FAN-OUT GETS CHEAPER: a maskDirtyBits_/panDirtyBits_ pair
// published under the same release/acquire handshake, or coalescing the re-push
// to once per process() call. NEVER a raised bound, never a raised ceiling, and
// never a body that cannot unmask.
// =============================================================================
TEST_CASE("Seraphis_PartialOverrides_RepushWorstCase", "[.perf][phase11]") {
    auto fx = std::make_unique<Fixture>();
    REQUIRE(fx->prepare(kSr48, kBlock) == Steinberg::kResultOk);

    fx->proc->setCloudFrameGateForTest(true);
    bringToOperatingPoint(*fx);
    requireOperatingPoint(*fx);

    // ---- The Bloom sweep ----------------------------------------------------
    // A DIFFERENT value every block: re-writing the identical value moves no
    // smoother, leaves the composed spread where it was and produces no re-push
    // at all (the mechanism effects_perf_test.cpp:1224-1229 records for the
    // class-(b) smoothers).
    std::size_t sweepStep = 0;
    double audioSink = 0.0;
    const auto sweepBloom = [&]() {
        const double v = static_cast<double>(sweepStep % 16u) / 15.0;
        fx->setParam(Seraphis::kMacroBloomId, v);
        ++sweepStep;
    };
    const auto renderBlock = [&]() {
        (void)fx->processBlock(kBlock);
        audioSink += static_cast<double>(fx->audioL()[0])
                     + static_cast<double>(fx->audioR()[kBlockSize - 1u]);
    };

    // Warm the sweep itself: the first few blocks carry the macro smoothers'
    // travel from their defaults, which is not the steady state being budgeted.
    for (int b = 0; b < 64; ++b) {
        sweepBloom();
        renderBlock();
    }

    // ---- THE SAME-RUN BASELINE: the identical sweep, NO overrides authored ---
    // Measured FIRST, before a single pan is authored, because panBits cannot be
    // un-authored: there is no clearing EditMessage, and inventing one to run the
    // arms in the other order would put a seam in the shipped processor that
    // exists only for this measurement. The tracker still fires
    // repushPartialOverrides() every slice here - it is the PAN half that panBits
    // gates (processor.cpp:3415), and that is exactly the half this delta prices.
    REQUIRE(fx->proc->lastPublishedFrameForTest().overriddenBits
            == static_cast<std::uint64_t>(0));
    const double noRepushNs =
        bestNsPerBlock(kPerfTrials, kPerfBlocksPerTrial, sweepBloom, renderBlock);

    // ---- 64 authored pans, i.e. EVERY bit of panBits set --------------------
    // Distinct values per partial so nothing can be constant-folded, inside
    // [-1, +1] where kind 2 clamps anyway (processor.cpp:3719).
    for (std::size_t i = 0; i < Krate::DSP::HarmonicCloud::kMaxPartials; ++i) {
        REQUIRE(sendEdit(*fx->proc, makeEdit(kEditPartialPan, static_cast<std::uint16_t>(i),
                                             authoredPanFor(i)))
                == Steinberg::kResultOk);
    }
    // The staging is consumed on the audio thread, so one block has to run before
    // the fan-out has happened at all.
    REQUIRE(fx->processBlock(kBlock) == Steinberg::kResultOk);
    // All 64 bits, asserted rather than assumed - a partial surface here would
    // measure a fraction of the worst case and still pass. These are the
    // PROCESSOR's own staging bitmasks read back off the frame
    // (processor.cpp:4068-4072), i.e. what repushPartialOverrides() will iterate.
    REQUIRE(fx->proc->lastPublishedFrameForTest().overriddenBits
            == ~static_cast<std::uint64_t>(0));

    // Re-warm with the overrides in place, so the two figures differ by the
    // fan-out and not by a transient.
    for (int b = 0; b < 64; ++b) {
        sweepBloom();
        renderBlock();
    }

    const double repushNs =
        bestNsPerBlock(kPerfTrials, kPerfBlocksPerTrial, sweepBloom, renderBlock);
    gSink = gSink + audioSink;  // optimization barrier

    WARN(deltaRow("SC-014 arm 7 THE RE-PUSH'S MARGINAL whole-process() COST: the same macro "
                  "Bloom sweep with 64 pan overrides authored minus with none (2048 pan/mask "
                  "writes per re-push, cos+sin per pan)",
                  repushNs, noRepushNs, kRepushDeltaBoundNs));

    // THE FAN-OUT REALLY RAN, and this is the assertion that shows it rather than
    // the bitmask above: the bitmask is the processor's own message-thread record
    // and would read all-ones even if repushPartialOverrides() never executed. The
    // ENGINE's stored pan can only have got there through the fan-out - the
    // message thread never touches HarmonicCloud (T003's thread-ownership note) -
    // and the Bloom sweep wiped positionOverridden_ on every slice in between, so
    // a value still standing here is one the re-push put back. Read at SC-014's
    // own 0.01 band, on a partial whose authored pan is neither an endpoint nor
    // the default.
    const Krate::DSP::SeraphisEngine& engine = *fx->proc->engineForTest();
    CHECK(static_cast<double>(engine.getVoice(0).cloud().getPartialPosition(kPannedPartial))
          == Approx(static_cast<double>(authoredPanFor(kPannedPartial))).margin(0.01));
    REQUIRE(repushNs > 0.0);
    REQUIRE(noRepushNs > 0.0);
    // THE CRITERION, RESTATED (ruling (a)). If this fails, the fan-out gets
    // cheaper (banner) - never the bound and never the ceiling.
    REQUIRE((repushNs - noRepushNs) <= kRepushDeltaBoundNs);

    REQUIRE(fx->checkCanaries());
    REQUIRE(isFinitePerfValue(audioSink));
}

// =============================================================================
// SC-031 - AN EDIT GESTURE IN FLIGHT STILL FITS THE BUDGET
// =============================================================================
// THE ARM T003a's RELAXATION NEEDS, and the one no pre-existing criterion
// supplies: SC-009, SC-010 and SC-014 arm 7 all run with a STATIC slot set, and
// SC-029 measures continuity rather than time.
//
// WHAT IT COSTS. With the configure-time gate relaxed, every voice now runs
// isValidSpectralState + buildSanitized (a 64-entry std::log2 pass,
// spectral_morph_engine.h:513, :537-543) BEFORE the identity early-out at :302-
// 305, and consumeSpectralSlotHandoff() re-arms spectralRetryMask_ = 0xFFFFu
// (processor.cpp:3444), so one handoff costs 16 voices x 4 slots ~= 4096
// std::log2.
//
// THE RATE IS C-8's 30 Hz THROTTLE, not a rate this test chose: one accepted
// EditMessage every 33 ms is one stageSlotEdit handoff every ~3.1 blocks at
// 512 / 48 kHz, which is what kEditBlocksPerMessage below spells.
//
// RESTATED 2026-08-04 (ruling (a)): the criterion is now the gesture's MARGINAL
// whole-process() cost - the same warm fixture measured TWICE, once with no
// gesture at all and once with the 30 Hz drag in flight - against
// kGestureDeltaBoundNs (1.00 point of one core). The no-gesture arm is measured
// FIRST, before any EditMessage is sent, so the baseline is a genuinely static
// slot set and the delta is the whole cost of D1/FR-033a: the relaxed gate's
// buildSanitized pass plus the 0xFFFFu retry re-arm. The absolute figure is still
// reported against the 25 % ceiling, which roadmap Phase 11.5 owns.
//
// IF THIS FAILS, THE PUSH GETS CHEAPER, IN THIS ORDER: (1) narrow
// spectralRetryMask_ at processor.cpp:3444 to the voices that can still reject -
// with the gate relaxed a blanket 0xFFFFu re-arm is pure waste; (2) add a
// pre-applySpectralStates identity check against the processor's last-pushed
// spectralSlots_ copy so an unchanged slot never reaches buildSanitized. RAISING
// THE BOUND, RAISING THE CEILING AND DROPPING BELOW THE 30 Hz THROTTLE ARE ALL
// FORBIDDEN. Both remedies are inside the plugin; neither is a dsp/ change.
// =============================================================================
namespace {

/// C-5 kind 1 (edit_message.h): `a` = ratio, `b` = amplitude.
constexpr std::uint8_t kEditPartialRatioAmp = 1;

/// One accepted message every 33 ms. 33 ms / (512 / 48 000 s) = 3.09 blocks, so
/// every third block is the closest whole-block cadence AT OR ABOVE the throttle
/// rate - i.e. very slightly MORE traffic than the throttle admits, which is the
/// conservative direction against a fixed ceiling.
constexpr std::size_t kEditBlocksPerMessage = 3;
static_assert(kEditBlocksPerMessage >= 1u, "a zero period would send one edit per block");

/// The partial the drag moves. Index 0 is pinned: setPartial's monotone window
/// caps the upper edge at ratios[index + 1] / kAuthorSpacing, so index 0 is the
/// one slot whose window is wide enough for the sweep below to land inside rather
/// than clamp to a constant - and a clamped-to-constant drag would stop changing
/// the state and stop producing handoffs.
constexpr std::uint16_t kDraggedPartial = 0;

}  // namespace

TEST_CASE("Seraphis_EditGestureInFlight_FitsTheBudget", "[.perf][phase11]") {
    auto fx = std::make_unique<Fixture>();
    REQUIRE(fx->prepare(kSr48, kBlock) == Steinberg::kResultOk);

    fx->proc->setCloudFrameGateForTest(true);
    bringToOperatingPoint(*fx);
    requireOperatingPoint(*fx);

    // ---- The drag -----------------------------------------------------------
    // A DIFFERENT ratio every message: setState returns early without arming on
    // an identical state (spectral_morph_engine.h:302-305), so a drag that
    // re-sent one value would stop being a gesture after the first message and
    // this criterion would measure a static slot set - exactly what it exists to
    // NOT measure.
    std::size_t blockIndex = 0;
    std::size_t editsSent = 0;
    double audioSink = 0.0;

    const auto driveGesture = [&]() {
        if ((blockIndex % kEditBlocksPerMessage) == 0u) {
            // 1.00 .. 1.20 in eight steps, well inside index 0's window
            // (ratios[1] / kAuthorSpacing ~= 1.968 on a factory state).
            const float ratio =
                1.0f + (0.20f * static_cast<float>(editsSent % 8u) / 7.0f);
            Seraphis::UI::EditMessage m{};
            m.kind = kEditPartialRatioAmp;
            m.slot = 0;
            m.index = kDraggedPartial;
            m.a = ratio;
            m.b = 0.8f;  // amplitude - live by C-6, not a dead field
            // notify() is the MESSAGE thread and allocates a HostMessage, which
            // is why this is the PRELUDE and never inside the timed bracket.
            (void)sendEdit(*fx->proc, m);
            ++editsSent;
        }
        ++blockIndex;
    };
    const auto renderBlock = [&]() {
        (void)fx->processBlock(kBlock);
        audioSink += static_cast<double>(fx->audioL()[0])
                     + static_cast<double>(fx->audioR()[kBlockSize - 1u]);
    };

    // ---- THE SAME-RUN BASELINE: no gesture at all ---------------------------
    // Measured FIRST, before a single EditMessage is sent, so it is the static
    // slot set SC-009 covers - measured HERE, on THIS fixture, in THIS run, which
    // is what makes the delta below the gesture's own cost rather than a second
    // reading of the machine.
    const auto noPrelude = []() noexcept {};
    const std::size_t editsAtBaseline = fx->proc->editStageWriteCountForTest();
    const double noGestureNs =
        bestNsPerBlock(kPerfTrials, kPerfBlocksPerTrial, noPrelude, renderBlock);
    // Nothing was in flight across the baseline window, asserted rather than
    // assumed: a baseline that carried edits would understate the delta.
    REQUIRE(fx->proc->editStageWriteCountForTest() == editsAtBaseline);

    // Warm the gesture: the first handoffs also pay the authoring mirror's
    // dropdown re-seed, which is not the steady state being budgeted.
    const std::size_t handoffsAtStart = fx->proc->spectralHandoffConsumeCountForTest();
    for (int b = 0; b < 64; ++b) {
        driveGesture();
        renderBlock();
    }
    // The gesture is REALLY IN FLIGHT before anything is timed: message-thread
    // stages accepted, and the audio thread consumed them. Without both, this
    // case measures the same static slot set SC-009 already covers.
    REQUIRE(fx->proc->editStageWriteCountForTest() >= editsSent);
    REQUIRE(fx->proc->spectralHandoffConsumeCountForTest() > handoffsAtStart);

    const std::size_t handoffsBefore = fx->proc->spectralHandoffConsumeCountForTest();
    const std::size_t editsBefore = editsSent;
    const double gestureNs =
        bestNsPerBlock(kPerfTrials, kPerfBlocksPerTrial, driveGesture, renderBlock);
    const std::size_t handoffs =
        fx->proc->spectralHandoffConsumeCountForTest() - handoffsBefore;
    const std::size_t edits = editsSent - editsBefore;
    gSink = gSink + audioSink;  // optimization barrier

    WARN(deltaRow("SC-031 THE IN-FLIGHT GESTURE'S MARGINAL whole-process() COST: a 30 Hz kind-1 "
                  "partial drag (one stageSlotEdit handoff every ~3 blocks; ~4096 std::log2 per "
                  "handoff) minus the same fixture with no gesture at all",
                  gestureNs, noGestureNs, kGestureDeltaBoundNs));
    INFO("edits sent = " << edits << ", handoffs consumed = " << handoffs << ", over "
                         << (kPerfTrials * kPerfBlocksPerTrial) << " timed blocks");
    // THE MEASURED REGION REALLY CARRIED THE GESTURE, and this is the exact
    // invariant rather than a floor: each accepted kind-1 publishes exactly one
    // handoff (processor.cpp:3665-3669) and the audio thread consumes at most one
    // per process() call (:3432-3446), so at one message per three blocks NONE is
    // overwritten and the two counts must agree. An inequality here would mean
    // either the drag died (fewer edits) or handoffs were lost (fewer consumes) -
    // both of which turn this case back into the static slot set SC-009 covers.
    REQUIRE(edits >= (static_cast<std::size_t>(kPerfTrials * kPerfBlocksPerTrial)
                      / kEditBlocksPerMessage)
                         - 1u);
    REQUIRE(handoffs == edits);
    REQUIRE(gestureNs > 0.0);
    REQUIRE(noGestureNs > 0.0);
    // THE CRITERION, RESTATED (ruling (a)). If this fails, the PUSH gets cheaper,
    // in the order the banner lists. The bound, the ceiling and the 30 Hz
    // throttle are all off the table.
    REQUIRE((gestureNs - noGestureNs) <= kGestureDeltaBoundNs);

    REQUIRE(fx->checkCanaries());
    REQUIRE(isFinitePerfValue(audioSink));
}
