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
#include "delay_mode.h"  // DelayMode enum (SDK-independent)

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

// Note: DelayMode enum is defined in delay_mode.h to allow use without VST3 SDK

enum ParameterIDs : Steinberg::Vst::ParamID {
    // ==========================================================================
    // Global Parameters (0-99)
    // ==========================================================================
    // Note: kBypassId removed - DAWs provide their own bypass functionality
    kGainId = 1,
    kModeId = 2,    // 0-10 (DelayMode enum) - selects active delay mode

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
    kGranularMixId = 110,            // 0-1 (renamed from kGranularDryWetId)
    kGranularEnvelopeTypeId = 112,   // 0-3 (Hann, Trapezoid, Sine, Blackman)
    kGranularTimeModeId = 113,       // 0-1 (Free, Synced) - spec 038
    kGranularNoteValueId = 114,      // 0-9 (note values) - spec 038
    kGranularJitterId = 115,         // 0-1 (timing randomness)
    kGranularPitchQuantId = 116,     // 0-4 (Off, Semitones, Octaves, Fifths, Scale)
    kGranularTextureId = 117,        // 0-1 (ordered to chaotic)
    kGranularStereoWidthId = 118,    // 0-1 (stereo decorrelation)
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
    kSpectralMixId = 208,            // 0-100% (renamed from kSpectralDryWetId)
    kSpectralSpreadCurveId = 209,    // 0-1 (Linear, Logarithmic)
    kSpectralStereoWidthId = 210,    // 0-1 (stereo decorrelation amount)
    kSpectralTimeModeId = 211,       // 0=Free, 1=Synced (spec 041)
    kSpectralNoteValueId = 212,      // 0-9 note value dropdown (spec 041)
    kSpectralEndId = 299,

    // ==========================================================================
    // Shimmer Delay Parameters (300-399) - spec 029
    // ==========================================================================
    kShimmerBaseId = 300,
    kShimmerDelayTimeId = 300,        // 10-5000ms
    kShimmerPitchSemitonesId = 301,   // -24 to +24 semitones
    kShimmerPitchCentsId = 302,       // -100 to +100 cents
    kShimmerPitchBlendId = 303,       // 0-100% (renamed from kShimmerShimmerMixId)
    kShimmerFeedbackId = 304,         // 0-120%
    kShimmerDiffusionAmountId = 305,  // 0-100%
    kShimmerDiffusionSizeId = 306,    // 0-100%
    kShimmerFilterEnabledId = 307,    // on/off
    kShimmerFilterCutoffId = 308,     // 20-20000Hz
    kShimmerMixId = 309,              // 0-100% (renamed from kShimmerDryWetId)
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
    kBBDDelayTimeId = 500,           // 20-1000ms
    kBBDFeedbackId = 501,            // 0-120%
    kBBDModDepthId = 502,            // 0-100% (renamed from kBBDModulationDepthId)
    kBBDModRateId = 503,             // 0.1-10Hz (renamed from kBBDModulationRateId)
    kBBDAgeId = 504,                 // 0-100%
    kBBDEraId = 505,                 // 0-3 (MN3005, MN3007, MN3205, SAD1024)
    kBBDMixId = 506,                 // 0-100%
    kBBDEndId = 599,

    // ==========================================================================
    // Digital Delay Parameters (600-699) - spec 026
    // ==========================================================================
    kDigitalBaseId = 600,
    kDigitalDelayTimeId = 600,       // 1-10000ms
    kDigitalTimeModeId = 601,        // 0-1 (Free, Synced)
    kDigitalNoteValueId = 602,       // 0-9 (note values)
    kDigitalFeedbackId = 603,        // 0-120%
    kDigitalLimiterCharacterId = 604, // 0-2 (Soft, Medium, Hard)
    kDigitalEraId = 605,             // 0-2 (Pristine, 80s, LoFi)
    kDigitalAgeId = 606,             // 0-100%
    kDigitalModDepthId = 607,        // 0-100%
    kDigitalModRateId = 608,         // 0.1-10Hz
    kDigitalModWaveformId = 609,     // 0-5 (waveforms)
    kDigitalMixId = 610,             // 0-100%
    kDigitalWidthId = 612,           // 0-200% (spec 036)
    kDigitalEndId = 699,

    // ==========================================================================
    // PingPong Delay Parameters (700-799) - spec 027
    // ==========================================================================
    kPingPongBaseId = 700,
    kPingPongDelayTimeId = 700,      // 1-10000ms
    kPingPongTimeModeId = 701,       // 0-1 (Free, Synced)
    kPingPongNoteValueId = 702,      // 0-9 (note values)
    kPingPongLRRatioId = 703,        // 0-6 (ratio presets)
    kPingPongFeedbackId = 704,       // 0-120%
    kPingPongCrossFeedbackId = 705,  // 0-100%
    kPingPongWidthId = 706,          // 0-200%
    kPingPongModDepthId = 707,       // 0-100%
    kPingPongModRateId = 708,        // 0.1-10Hz
    kPingPongMixId = 709,            // 0-100%
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
    kReverseMixId = 807,             // 0-100% (renamed from kReverseDryWetId)
    kReverseEndId = 899,

    // ==========================================================================
    // MultiTap Delay Parameters (900-999) - spec 028
    // ==========================================================================
    kMultiTapBaseId = 900,
    kMultiTapTimingPatternId = 900,  // 0-19 (pattern presets)
    kMultiTapSpatialPatternId = 901, // 0-6 (spatial presets)
    kMultiTapTapCountId = 902,       // 2-16 taps
    kMultiTapBaseTimeId = 903,       // 1-5000ms
    kMultiTapTempoId = 904,          // 20-300 BPM
    kMultiTapFeedbackId = 905,       // 0-110%
    kMultiTapFeedbackLPCutoffId = 906, // 20-20000Hz
    kMultiTapFeedbackHPCutoffId = 907, // 20-20000Hz
    kMultiTapMorphTimeId = 908,      // 50-2000ms
    kMultiTapMixId = 909,            // 0-100% (renamed from kMultiTapDryWetId)
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
    kFreezeMixId = 1012,              // 0-100% (renamed from kFreezeDryWetId)
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
    kDuckingMixId = 1111,               // 0-100% (renamed from kDuckingDryWetId)
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
