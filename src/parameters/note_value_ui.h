// ==============================================================================
// Note Value UI Constants - Centralized Dropdown Strings
// ==============================================================================
// Provides centralized UI strings for note value dropdowns.
// All delay modes use this single source of truth for consistency.
//
// The dropdown mapping (NoteValue + NoteModifier) is defined in DSP layer:
//   dsp/core/note_value.h - kNoteValueDropdownMapping[]
//
// This file provides the UI strings that match that mapping.
//
// Constitution Compliance:
// - Principle III: Modern C++ (constexpr arrays)
// - Principle XII: Single Source of Truth
// ==============================================================================

#pragma once

#include "dsp/core/note_value.h"          // For kNoteValueDropdownCount, kNoteValueDefaultIndex
#include "pluginterfaces/base/ftypes.h"   // For Steinberg::Vst::TChar
#include "pluginterfaces/base/ustring.h"  // For STR16

namespace Iterum::Parameters {

// Re-export DSP constants for convenience
using DSP::kNoteValueDropdownCount;
using DSP::kNoteValueDefaultIndex;

// =============================================================================
// Note Value Dropdown UI Strings
// =============================================================================
// Grouped by note value (Triplet, Normal, Dotted for each note).
// Order MUST match kNoteValueDropdownMapping[] in dsp/core/note_value.h
//
// Total: 21 entries (indices 0-20)
// Default: index 10 (1/8 note)
// =============================================================================

/// @brief UI strings for note value dropdown, grouped by note value
/// Each group: Triplet, Normal, Dotted (e.g., 1/8T, 1/8, 1/8D)
inline const Steinberg::Vst::TChar* const kNoteValueDropdownStrings[] = {
    // 1/64 variants
    STR16("1/64T"),   // 0:  triplet 64th
    STR16("1/64"),    // 1:  64th note
    STR16("1/64D"),   // 2:  dotted 64th
    // 1/32 variants
    STR16("1/32T"),   // 3:  triplet 32nd
    STR16("1/32"),    // 4:  32nd note
    STR16("1/32D"),   // 5:  dotted 32nd
    // 1/16 variants
    STR16("1/16T"),   // 6:  triplet 16th
    STR16("1/16"),    // 7:  16th note
    STR16("1/16D"),   // 8:  dotted 16th
    // 1/8 variants
    STR16("1/8T"),    // 9:  triplet 8th
    STR16("1/8"),     // 10: 8th note (DEFAULT)
    STR16("1/8D"),    // 11: dotted 8th
    // 1/4 variants
    STR16("1/4T"),    // 12: triplet quarter
    STR16("1/4"),     // 13: quarter note
    STR16("1/4D"),    // 14: dotted quarter
    // 1/2 variants
    STR16("1/2T"),    // 15: triplet half
    STR16("1/2"),     // 16: half note
    STR16("1/2D"),    // 17: dotted half
    // 1/1 variants
    STR16("1/1T"),    // 18: triplet whole
    STR16("1/1"),     // 19: whole note
    STR16("1/1D"),    // 20: dotted whole
};

} // namespace Iterum::Parameters
