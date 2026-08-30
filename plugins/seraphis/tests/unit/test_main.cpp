// ==============================================================================
// Seraphis Test Main
// ==============================================================================

#include <catch2/catch_session.hpp>
#include <enable_ftz_daz.h>

// FR-066a. Global allocation-operator replacements live in ONE shared header so
// the matched set and its visibility cannot drift per-TU. This MUST be included
// from EXACTLY ONE translation unit of this binary: a second include is a
// duplicate-symbol link error, and a hand-rolled copy is caught by
// tools/lint-allocation-operator-overrides.js. Without it,
// TestHelpers::AllocationScope counts NOTHING and the allocation criteria pass
// vacuously.
#include <allocation_operator_overrides.h>

// Provide moduleHandle symbol required by VST3 SDK's moduleinit.cpp.
// In a real plugin this comes from dllmain.cpp; tests have no DLL entry point.
// This definition satisfies moduleinit.cpp's `extern void* moduleHandle;` (vst3sdk
// public.sdk/source/main/moduleinit.cpp:21), so it must be a MUTABLE global with EXTERNAL
// linkage - `static`, `const`, or an anonymous namespace all turn it into an unresolved
// external at link time. Hence the suppressions below rather than the suggested fixes.
// NOLINTNEXTLINE(misc-use-internal-linkage,cppcoreguidelines-avoid-non-const-global-variables)
void* moduleHandle = nullptr;

int main(int argc, char* argv[]) {
    enableFTZDAZ();
    return Catch::Session().run(argc, argv);
}
