#pragma once

// ==============================================================================
// Delay Mode Enumeration
// ==============================================================================
// Used by processor, controller, and preset system to identify the active delay mode.
// This header has NO VST3 SDK dependencies to allow use in pure C++ tests.
// ==============================================================================

namespace Iterum {

enum class DelayMode : int {
    Granular = 0,   // spec 034 - Granular processing with pitch/time spray
    Spectral = 1,   // spec 033 - FFT-based per-band delays
    Shimmer = 2,    // spec 029 - Pitch-shifted feedback with diffusion
    Tape = 3,       // spec 024 - Classic tape echo with wow/flutter
    BBD = 4,        // spec 025 - Bucket-brigade analog character
    Digital = 5,    // spec 026 - Clean or vintage digital
    PingPong = 6,   // spec 027 - Stereo alternating delays
    Reverse = 7,    // spec 030 - Grain-based reverse processing
    MultiTap = 8,   // spec 028 - Up to 16 taps with patterns
    Freeze = 9,     // spec 031 - Infinite sustain
    Ducking = 10,   // spec 032 - Envelope-based signal reduction
    NumModes = 11
};

} // namespace Iterum
