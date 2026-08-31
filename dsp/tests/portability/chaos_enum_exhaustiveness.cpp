// ==============================================================================
// Vorago Phase 1 - SC-007 warning gate (NOT part of any CMake target)
// ==============================================================================
// Appending ChaosModel::Aizawa (chaos_waveshaper.h:60) makes every `default:`-less
// switch over ChaosModel non-exhaustive. GCC/Clang report that as -Wswitch; MSVC
// does not, so a green Windows build proves nothing here. This TU exists solely so
// the two headers carrying those switches can be pushed through GCC and Clang with
// -Wswitch -Werror, out of the Catch2 suite (a test summary reports results, not
// diagnostics), and out of tools/check-portability.js (its g++ invocation passes
// neither warning flags nor -Werror, :230, and isCheckable() only inspects
// .cpp/.cc files, :204-209 - the T006/T007 change lives entirely in headers).
//
// Run from the repo root; BOTH must exit 0
// (see specs/vorago-phase1-events-modulation/tasks.md T008). One line each,
// no backslash continuations - a trailing backslash inside a // comment is
// -Wcomment, which -Werror turns into a failure of this very gate:
//
//   wsl -e bash -c 'cd /mnt/f/projects/iterum && g++ -std=c++20 -Wall -Wextra -Wswitch -Werror -fsyntax-only -I . -I dsp/include -I dsp/tests -I tests -I tests/test_helpers dsp/tests/portability/chaos_enum_exhaustiveness.cpp'
//   wsl -e bash -c 'cd /mnt/f/projects/iterum && clang++ -std=c++20 -Wall -Wextra -Wswitch -Werror -fsyntax-only -I . -I dsp/include -I dsp/tests -I tests -I tests/test_helpers dsp/tests/portability/chaos_enum_exhaustiveness.cpp'
//
// A non-zero exit means a grouped `case ChaosModel::Aizawa:` arm is missing or
// misplaced - updateAttractor() (chaos_waveshaper.h:649) and resetModelState()
// (:699) in Layer 1, and the three switches in chaos_mod_source.h (:201, :237,
// :274) in Layer 2. Deliberately defines nothing: instantiating anything would
// only add failure modes that the Catch2 suite already covers.
// ==============================================================================

#include <krate/dsp/primitives/chaos_waveshaper.h>
#include <krate/dsp/processors/chaos_mod_source.h>
