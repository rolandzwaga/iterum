#pragma once

// ==============================================================================
// Plugin Identifiers
// ==============================================================================
// These GUIDs uniquely identify the plugin components.
// Generate new GUIDs for your plugin at: https://www.guidgenerator.com/
//
// IMPORTANT: Once published, NEVER change these IDs or hosts will not
// recognize saved projects using your plugin.
// ==============================================================================

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/vst/vsttypes.h"

namespace Iterum {

// Processor Component ID
// The audio processing component (runs on audio thread)
static const Steinberg::FUID kProcessorUID(0x12345678, 0x12345678, 0x12345678, 0x12345678);

// Controller Component ID
// The edit controller component (runs on UI thread)
static const Steinberg::FUID kControllerUID(0x87654321, 0x87654321, 0x87654321, 0x87654321);

// ==============================================================================
// Parameter IDs
// ==============================================================================
// Define all automatable parameters here.
// Constitution Principle V: All parameter values MUST be normalized (0.0 to 1.0)
//
// ID Range Allocation (100-ID gaps for future expansion):
//   0-99:      Global parameters
//   100-199:   Granular Delay (spec 034)
//   200-299:   Spectral Delay (spec 033)
//   300-399:   Shimmer Delay (spec 029)
//   400-499:   Tape Delay (spec 024)
//   500-599:   BBD Delay (spec 025)
//   600-699:   Digital Delay (spec 026)
//   700-799:   PingPong Delay (spec 027)
//   800-899:   Reverse Delay (spec 030)
//   900-999:   MultiTap Delay (spec 028)
//   1000-1099: Freeze Mode (spec 031)
//   1100-1199: Ducking Delay (spec 032)
// ==============================================================================

enum ParameterIDs : Steinberg::Vst::ParamID {
    // ==========================================================================
    // Global Parameters (0-99)
    // ==========================================================================
    kBypassId = 0,
    kGainId = 1,

    // ==========================================================================
    // Granular Delay Parameters (100-199) - spec 034
    // ==========================================================================
    kGranularBaseId = 100,
    kGranularGrainSizeId = 100,      // 10-500ms
    kGranularDensityId = 101,        // 1-100 grains/sec
    kGranularDelayTimeId = 102,      // 0-2000ms
    kGranularPitchId = 103,          // -24 to +24 semitones
    kGranularPitchSprayId = 104,     // 0-1
    kGranularPositionSprayId = 105,  // 0-1
    kGranularPanSprayId = 106,       // 0-1
    kGranularReverseProbId = 107,    // 0-1
    kGranularFreezeId = 108,         // on/off
    kGranularFeedbackId = 109,       // 0-1.2
    kGranularDryWetId = 110,         // 0-1
    kGranularOutputGainId = 111,     // -96 to +6 dB
    kGranularEnvelopeTypeId = 112,   // 0-3 (Hann, Trapezoid, Sine, Blackman)
    kGranularEndId = 199,

    // ==========================================================================
    // Spectral Delay Parameters (200-299) - spec 033
    // ==========================================================================
    kSpectralBaseId = 200,
    kSpectralFFTSizeId = 200,        // 512, 1024, 2048, 4096
    kSpectralBaseDelayId = 201,      // 0-2000ms
    kSpectralSpreadId = 202,         // 0-2000ms
    kSpectralSpreadDirectionId = 203, // 0-2 (LowToHigh, HighToLow, CenterOut)
    kSpectralFeedbackId = 204,       // 0-1.2
    kSpectralFeedbackTiltId = 205,   // -1.0 to +1.0
    kSpectralFreezeId = 206,         // on/off
    kSpectralDiffusionId = 207,      // 0-1
    kSpectralDryWetId = 208,         // 0-100%
    kSpectralOutputGainId = 209,     // -96 to +6 dB
    kSpectralEndId = 299,

    // ==========================================================================
    // Shimmer Delay Parameters (300-399) - spec 029
    // ==========================================================================
    kShimmerBaseId = 300,
    kShimmerDelayTimeId = 300,        // 10-5000ms
    kShimmerPitchSemitonesId = 301,   // -24 to +24 semitones
    kShimmerPitchCentsId = 302,       // -100 to +100 cents
    kShimmerShimmerMixId = 303,       // 0-100%
    kShimmerFeedbackId = 304,         // 0-120%
    kShimmerDiffusionAmountId = 305,  // 0-100%
    kShimmerDiffusionSizeId = 306,    // 0-100%
    kShimmerFilterEnabledId = 307,    // on/off
    kShimmerFilterCutoffId = 308,     // 20-20000Hz
    kShimmerDryWetId = 309,           // 0-100%
    kShimmerOutputGainId = 310,       // -12 to +12 dB
    kShimmerEndId = 399,

    // ==========================================================================
    // Tape Delay Parameters (400-499) - spec 024
    // ==========================================================================
    kTapeBaseId = 400,
    kTapeMotorSpeedId = 400,       // 20-2000ms (delay time)
    kTapeMotorInertiaId = 401,     // 100-1000ms
    kTapeWearId = 402,             // 0-100% (wow/flutter/hiss)
    kTapeSaturationId = 403,       // 0-100%
    kTapeAgeId = 404,              // 0-100%
    kTapeSpliceEnabledId = 405,    // on/off
    kTapeSpliceIntensityId = 406,  // 0-100%
    kTapeFeedbackId = 407,         // 0-120%
    kTapeMixId = 408,              // 0-100%
    kTapeOutputLevelId = 409,      // -96 to +12 dB
    kTapeHead1EnabledId = 410,     // on/off
    kTapeHead2EnabledId = 411,     // on/off
    kTapeHead3EnabledId = 412,     // on/off
    kTapeHead1LevelId = 413,       // -96 to +6 dB
    kTapeHead2LevelId = 414,       // -96 to +6 dB
    kTapeHead3LevelId = 415,       // -96 to +6 dB
    kTapeHead1PanId = 416,         // L100-R100
    kTapeHead2PanId = 417,         // L100-R100
    kTapeHead3PanId = 418,         // L100-R100
    kTapeEndId = 499,

    // ==========================================================================
    // BBD Delay Parameters (500-599) - spec 025
    // ==========================================================================
    kBBDBaseId = 500,
    // Parameters to be added during integration
    kBBDEndId = 599,

    // ==========================================================================
    // Digital Delay Parameters (600-699) - spec 026
    // ==========================================================================
    kDigitalBaseId = 600,
    // Parameters to be added during integration
    kDigitalEndId = 699,

    // ==========================================================================
    // PingPong Delay Parameters (700-799) - spec 027
    // ==========================================================================
    kPingPongBaseId = 700,
    // Parameters to be added during integration
    kPingPongEndId = 799,

    // ==========================================================================
    // Reverse Delay Parameters (800-899) - spec 030
    // ==========================================================================
    kReverseBaseId = 800,
    kReverseChunkSizeId = 800,       // 10-2000ms
    kReverseCrossfadeId = 801,       // 0-100%
    kReversePlaybackModeId = 802,    // 0-2 (FullReverse, Alternating, Random)
    kReverseFeedbackId = 803,        // 0-120%
    kReverseFilterEnabledId = 804,   // on/off
    kReverseFilterCutoffId = 805,    // 20-20000Hz
    kReverseFilterTypeId = 806,      // 0-2 (LowPass, HighPass, BandPass)
    kReverseDryWetId = 807,          // 0-100%
    kReverseOutputGainId = 808,      // -96 to +6 dB
    kReverseEndId = 899,

    // ==========================================================================
    // MultiTap Delay Parameters (900-999) - spec 028
    // ==========================================================================
    kMultiTapBaseId = 900,
    // Parameters to be added during integration
    kMultiTapEndId = 999,

    // ==========================================================================
    // Freeze Mode Parameters (1000-1099) - spec 031
    // ==========================================================================
    kFreezeBaseId = 1000,
    kFreezeEnabledId = 1000,          // on/off
    kFreezeDelayTimeId = 1001,        // 10-5000ms
    kFreezeFeedbackId = 1002,         // 0-120%
    kFreezePitchSemitonesId = 1003,   // -24 to +24
    kFreezePitchCentsId = 1004,       // -100 to +100
    kFreezeShimmerMixId = 1005,       // 0-100%
    kFreezeDecayId = 1006,            // 0-100%
    kFreezeDiffusionAmountId = 1007,  // 0-100%
    kFreezeDiffusionSizeId = 1008,    // 0-100%
    kFreezeFilterEnabledId = 1009,    // on/off
    kFreezeFilterTypeId = 1010,       // 0-2 (LowPass, HighPass, BandPass)
    kFreezeFilterCutoffId = 1011,     // 20-20000Hz
    kFreezeDryWetId = 1012,           // 0-100%
    kFreezeOutputGainId = 1013,       // -96 to +6 dB
    kFreezeEndId = 1099,

    // ==========================================================================
    // Ducking Delay Parameters (1100-1199) - spec 032
    // ==========================================================================
    kDuckingBaseId = 1100,
    kDuckingEnabledId = 1100,           // on/off
    kDuckingThresholdId = 1101,         // -60 to 0 dB
    kDuckingDuckAmountId = 1102,        // 0-100%
    kDuckingAttackTimeId = 1103,        // 0.1-100ms
    kDuckingReleaseTimeId = 1104,       // 10-2000ms
    kDuckingHoldTimeId = 1105,          // 0-500ms
    kDuckingDuckTargetId = 1106,        // 0-2 (Output, Feedback, Both)
    kDuckingSidechainFilterEnabledId = 1107,  // on/off
    kDuckingSidechainFilterCutoffId = 1108,   // 20-500Hz
    kDuckingDelayTimeId = 1109,         // 10-5000ms
    kDuckingFeedbackId = 1110,          // 0-120%
    kDuckingDryWetId = 1111,            // 0-100%
    kDuckingOutputGainId = 1112,        // -96 to +6 dB
    kDuckingEndId = 1199,

    // ==========================================================================
    kNumParameters = 1200
};

// ==============================================================================
// Plugin Metadata
// ==============================================================================
// Note: Vendor info (company name, URL, email, copyright) is defined in
// version.h.in which CMake uses to generate version.h
// ==============================================================================

// VST3 Sub-categories (see VST3 SDK documentation for full list)
// Examples: "Fx", "Instrument", "Analyzer", "Delay", "Reverb", etc.
constexpr const char* kSubCategories = "Delay";

} // namespace Iterum
