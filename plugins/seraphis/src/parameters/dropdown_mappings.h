#pragma once

// ==============================================================================
// Seraphis - Dropdown Mappings (FR-015)
// ==============================================================================
// ONE table per dropdown, read by BOTH registration (register<Section>Params)
// AND formatting, so a label list cannot exist in two places and drift. That
// duplication is exactly what the shared pointer+count overloads
// (plugins/shared/src/ui/parameter_helpers.h:97, :118) exist to prevent, so
// every `L` parameter registers from the array below via
// Krate::Plugins::createDropdownParameterWithDefault.
//
// No new TYPE is introduced here: `inline constexpr` tables plus `inline`
// index<->enum converters. Three plugins already carry a file of this name
// (gradus, iterum, ruinae), each in its own namespace; a fourth in
// `namespace Seraphis` collides with none.
//
// Eight tables:
//   kSeedLabels / kSeedValues                                       ID 3
//   kTravelModeLabels                                               ID 403
//   kSyncNoteLabels / kSyncNoteBeats / kSyncNoteIsBarDenominated    ID 406 + FR-056
//   kStateCountLabels                                               ID 408
//   kSpectralStateLabels                                            IDs 409-412
//   kEnvelopeModeLabels                                             ID 700
//   kBodyMaterialLabels                                             ID 800
//   kGrainEnvelopeLabels                                            ID 1016
// ==============================================================================

#include "ui/parameter_helpers.h"  // createDropdownParameterWithDefault (FR-015)

#include "pluginterfaces/base/fstrdefs.h"  // STR16
#include "pluginterfaces/vst/vsttypes.h"   // Steinberg::Vst::TChar
#include "public.sdk/source/vst/vsteditcontroller.h"  // Vst::ParameterContainer

#include <krate/dsp/core/grain_envelope.h>          // GrainEnvelopeType
#include <krate/dsp/processors/spectral_state.h>    // SpectralStateId, kSpectralStateCount
#include <krate/dsp/systems/atmosphere_engine.h>    // AtmosphereEngine::kEnvelopeTypeCount
#include <krate/dsp/systems/continuous_body.h>      // ContinuousBody::BodyMaterial
#include <krate/dsp/systems/seraphis_voice.h>       // SeraphisVoice::EnvelopeMode
#include <krate/dsp/systems/spectral_morph_engine.h>  // SpectralMorphEngine::TravelMode

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace Seraphis {

// ==============================================================================
// ID 3 - kSeedId (C-10)
// ==============================================================================
// C-10's CURATED, CHECKED-IN table. NOT `index + 1`: whether consecutive small
// integers decorrelate depends on the engine's per-slot seed derivation, and a
// table removes that dependency instead of testing it.
//
// Index 0 is PINNED to 1u so kEngineSeed == kReverbSeed == 1u
// (engine/seraphis_engine_config.h:28-29) survives as the registered default,
// which is what keeps SC-002's negative control and SC-022 valid.
//
// THESE SIXTEEN CONSTANTS ARE A RESULT, NOT A STARTING TABLE. SC-020 clause 2's
// spread gate is a PROPERTY OF THIS TABLE: the measurement renders all sixteen
// at the pinned operating point and, if any pair is too close, RE-PICKS the
// offending constant and re-measures. Lowering the gate is not an available
// remedy.
//
// CURATED 2026-08-01 (SC-020 cl. 2's measurement, spec.md:2360-2364 / C-10).
// The first table shipped here was an ad-hoc set of sixteen hex constants; its
// measured pairwise total-variation spread bottomed out at 0.202 TV units
// (indices 7 and 9), which is `floor(0.202 / 1.05) = 0` - a toothless gate, and
// therefore, in C-10's own words, a DEFECT OF THE TABLE. That is expected of an
// arbitrary set: sixteen values scattered over an ~80-unit TV range have a
// minimum gap of order 80/16^2 = ~0.3 by construction, so the criterion can only
// be met by a table whose members were CHOSEN to be separated.
//
// These sixteen were selected from a 224-candidate splitmix64 pool, each
// candidate rendered through the shipped `Processor` at SC-020's pinned
// operating point (note 60 vel 100, held 3 s, 4 s total, 48 kHz, block 512,
// polyphony 8, drift depth 25 cents, material Glass) and scored by
// `TV(L) + TV(R)`. Index 0 is the fixed anchor 1u; the remaining fifteen
// maximise the minimum pairwise TV gap subject to that anchor. Measured gap:
// **4.899 TV units**, i.e. SC-020's gate `floor(4.899 / 1.05) = 4`. The full
// measured table is checked in beside the gate in
// `plugins/seraphis/tests/integration/param_character_test.cpp`.
//
// The margin is ~80 000x the cross-toolchain spread of an aggregate render metric
// (`tests/test_helpers/render_fingerprint.h:22-29` measured 1.9e-7 relative
// under g++ -O3, g++ -O3 -ffast-math and clang++ -O2; 1.9e-7 x 310 = 6e-5 TV
// units), so the gate is portable.
inline constexpr std::array<std::uint32_t, 16> kSeedValues = {
    0x00000001u, 0x51A8749Bu, 0x2ACBD1F1u, 0x2E89A193u,
    0x72403C09u, 0x3B343439u, 0x3B0E7D2Fu, 0x46980CADu,
    0x6BA7EEE9u, 0xB43343A1u, 0xD4367D77u, 0x3B1E1B79u,
    0x6C6AD50Fu, 0xA6F2B569u, 0x724C81EDu, 0x743AAE49u};

static_assert(kSeedValues[0] == 1u,
              "C-10 / SC-020 cl.3: the Phase 8 default must not drift");

/// ORDINAL labels, never the raw constants - the display must not imply the
/// underlying value is meaningful or editable.
inline constexpr std::array<const Steinberg::Vst::TChar*, 16> kSeedLabels = {
    STR16("Seed 1"),  STR16("Seed 2"),  STR16("Seed 3"),  STR16("Seed 4"),
    STR16("Seed 5"),  STR16("Seed 6"),  STR16("Seed 7"),  STR16("Seed 8"),
    STR16("Seed 9"),  STR16("Seed 10"), STR16("Seed 11"), STR16("Seed 12"),
    STR16("Seed 13"), STR16("Seed 14"), STR16("Seed 15"), STR16("Seed 16")};

static_assert(kSeedLabels.size() == kSeedValues.size(),
              "FR-015: one label per curated seed constant");

// ==============================================================================
// ID 403 - kMorphTravelModeId
// ==============================================================================
// Declaration order of SpectralMorphEngine::TravelMode
// (spectral_morph_engine.h:139): External = 0, Spline.
inline constexpr std::array<const Steinberg::Vst::TChar*, 2> kTravelModeLabels = {
    STR16("External"), STR16("Spline")};

// ==============================================================================
// ID 406 - kMorphSyncNoteId, and FR-056's beat values (C-7)
// ==============================================================================
// THE SINGLE TRANSCRIPTION of C-7's eight-row table. FR-056 reads it and may NOT
// re-derive it.
//
// kSyncNoteBeats holds `beatsPerJourney` in BEATS for the four note-denominated
// entries (0-3), and the BAR MULTIPLE for the four bar-denominated entries
// (4-7), which kSyncNoteIsBarDenominated marks. The consumer multiplies a marked
// entry by barBeats, where
// barBeats = timeSigNumerator * (4 / timeSigDenominator) when the host reports a
// valid, strictly-positive time signature, and 4.0 (common time) otherwise.
//
//   idx  label    beats            idx  label    bar multiple
//    0   1/16     0.25              4   1 Bar    1 x barBeats  (default)
//    1   1/8      0.5               5   2 Bars   2 x barBeats
//    2   1/4      1.0               6   4 Bars   4 x barBeats
//    3   1/2      2.0               7   8 Bars   8 x barBeats
inline constexpr std::array<const Steinberg::Vst::TChar*, 8> kSyncNoteLabels = {
    STR16("1/16"),  STR16("1/8"),   STR16("1/4"),   STR16("1/2"),
    STR16("1 Bar"), STR16("2 Bars"), STR16("4 Bars"), STR16("8 Bars")};

/// Beats for indices 0-3; BAR MULTIPLE for indices 4-7. Never zero, by
/// construction (the smallest entry is 0.25 at the bar-independent index 0), so
/// FR-056's division cannot produce a non-finite rate before its clamp.
inline constexpr std::array<double, 8> kSyncNoteBeats = {
    0.25, 0.5, 1.0, 2.0,
    1.0,  2.0, 4.0, 8.0};

/// True where kSyncNoteBeats carries a bar multiple rather than beats.
inline constexpr std::array<bool, 8> kSyncNoteIsBarDenominated = {
    false, false, false, false,
    true,  true,  true,  true};

static_assert(kSyncNoteLabels.size() == kSyncNoteBeats.size(),
              "C-7: one beat value per sync-note label");
static_assert(kSyncNoteLabels.size() == kSyncNoteIsBarDenominated.size(),
              "C-7: one bar-denomination flag per sync-note label");

// ==============================================================================
// ID 408 - kMorphStateCountId
// ==============================================================================
// SpectralMorphEngine accepts [kMinStates, kMaxStates] == [2, 4]
// (spectral_morph_engine.h:96-97), so index i selects a count of i + kMinStates.
inline constexpr std::array<const Steinberg::Vst::TChar*, 3> kStateCountLabels = {
    STR16("2"), STR16("3"), STR16("4")};

static_assert(kStateCountLabels.size()
                  == static_cast<std::size_t>(Krate::DSP::SpectralMorphEngine::kMaxStates
                                              - Krate::DSP::SpectralMorphEngine::kMinStates + 1),
              "FR-015: the state-count list must span [kMinStates, kMaxStates]");

// ==============================================================================
// IDs 409-412 - kMorphState0Id .. kMorphState3Id
// ==============================================================================
// Declaration order of SpectralStateId (spectral_state.h:313):
// SineStack = 0, Bell, Choir, Glass, Breath.
inline constexpr std::array<const Steinberg::Vst::TChar*, 5> kSpectralStateLabels = {
    STR16("Sine Stack"), STR16("Bell"), STR16("Choir"),
    STR16("Glass"), STR16("Breath")};

static_assert(kSpectralStateLabels.size() == Krate::DSP::kSpectralStateCount,
              "FR-015: one label per factory spectral state (spectral_state.h:315)");

// ==============================================================================
// ID 700 - kEnvModeId
// ==============================================================================
// Declaration order of SeraphisVoice::EnvelopeMode (seraphis_voice.h:135):
// Standard = 0, Growth = 1.
inline constexpr std::array<const Steinberg::Vst::TChar*, 2> kEnvelopeModeLabels = {
    STR16("Standard"), STR16("Growth")};

// ==============================================================================
// ID 800 - kBodyMaterialId
// ==============================================================================
// Declaration order of ContinuousBody::BodyMaterial (continuous_body.h:81):
// Glass = 0, Strings, MetalPlate, Chamber, Ice.
inline constexpr std::array<const Steinberg::Vst::TChar*, 5> kBodyMaterialLabels = {
    STR16("Glass"), STR16("Strings"), STR16("Metal Plate"),
    STR16("Chamber"), STR16("Ice")};

static_assert(kBodyMaterialLabels.size() == Krate::DSP::ContinuousBody::kNumMaterials,
              "FR-015: an enum extension must not silently desynchronise the label "
              "list from the materials the body ships (continuous_body.h:81, :84)");

// ==============================================================================
// ID 1016 - kAtmosGrainEnvelopeId
// ==============================================================================
// Declaration order of GrainEnvelopeType (core/grain_envelope.h:14-22), tied to
// the count AtmosphereEngine::prepare() generated windows for.
inline constexpr std::array<const Steinberg::Vst::TChar*, 6> kGrainEnvelopeLabels = {
    STR16("Hann"), STR16("Trapezoid"), STR16("Sine"),
    STR16("Blackman"), STR16("Linear"), STR16("Exponential")};

static_assert(kGrainEnvelopeLabels.size() == Krate::DSP::AtmosphereEngine::kEnvelopeTypeCount,
              "FR-015: an enum extension must not silently desynchronise the label list from "
              "the windows prepare() generates (atmosphere_engine.h:197, :427)");

// ==============================================================================
// Index <-> enum converters
// ==============================================================================
// Plain `inline` functions with a bounds clamp, one pair per enum-backed table.
// The clamp is what keeps a hostile or corrupt index out of a static_cast.

[[nodiscard]] inline Krate::DSP::ContinuousBody::BodyMaterial toBodyMaterial(int index) noexcept {
    const int i = std::clamp(index, 0, static_cast<int>(Krate::DSP::ContinuousBody::kNumMaterials) - 1);
    return static_cast<Krate::DSP::ContinuousBody::BodyMaterial>(i);
}

[[nodiscard]] inline int fromBodyMaterial(Krate::DSP::ContinuousBody::BodyMaterial m) noexcept {
    return static_cast<int>(m);
}

[[nodiscard]] inline Krate::DSP::SpectralMorphEngine::TravelMode toTravelMode(int index) noexcept {
    const int i = std::clamp(index, 0, static_cast<int>(kTravelModeLabels.size()) - 1);
    return static_cast<Krate::DSP::SpectralMorphEngine::TravelMode>(i);
}

[[nodiscard]] inline int fromTravelMode(Krate::DSP::SpectralMorphEngine::TravelMode m) noexcept {
    return static_cast<int>(m);
}

[[nodiscard]] inline Krate::DSP::GrainEnvelopeType toGrainEnvelopeType(int index) noexcept {
    const int i = std::clamp(
        index, 0, static_cast<int>(Krate::DSP::AtmosphereEngine::kEnvelopeTypeCount) - 1);
    return static_cast<Krate::DSP::GrainEnvelopeType>(i);
}

[[nodiscard]] inline int fromGrainEnvelopeType(Krate::DSP::GrainEnvelopeType t) noexcept {
    return static_cast<int>(t);
}

[[nodiscard]] inline Krate::DSP::SpectralStateId toSpectralStateId(int index) noexcept {
    const int i = std::clamp(index, 0, static_cast<int>(Krate::DSP::kSpectralStateCount) - 1);
    return static_cast<Krate::DSP::SpectralStateId>(i);
}

[[nodiscard]] inline int fromSpectralStateId(Krate::DSP::SpectralStateId id) noexcept {
    return static_cast<int>(id);
}

[[nodiscard]] inline Krate::DSP::SeraphisVoice::EnvelopeMode toEnvelopeMode(int index) noexcept {
    const int i = std::clamp(index, 0, static_cast<int>(kEnvelopeModeLabels.size()) - 1);
    return static_cast<Krate::DSP::SeraphisVoice::EnvelopeMode>(i);
}

[[nodiscard]] inline int fromEnvelopeMode(Krate::DSP::SeraphisVoice::EnvelopeMode m) noexcept {
    return static_cast<int>(m);
}

// ==============================================================================
// Registration - the ONE path by which a Seraphis `L` parameter is added
// ==============================================================================
// Krate::Plugins::createDropdownParameterWithDefault does NOT set the registered
// default. Both of its overloads (parameter_helpers.h:65-67, :126-128) end in
// Parameter::setNormalized, which writes `valueNormalized` and never touches
// `info.defaultNormalizedValue`; StringListParameter's constructor leaves that
// field at 0 (vstparameters.cpp:235). A host "reset to default" therefore snaps
// every dropdown with a non-zero default index back to index 0 - C-6's
// Polyphony 8 (ID 1), "1 Bar" (406) and "Glass" (410) - and
// `info.defaultNormalizedValue` is exactly what SC-014 and SC-022 read.
//
// Pinning the field changes no ID, type, unit or INTENDED default, so FR-063 is
// untouched. EVERY Seraphis dropdown registers through this function, including
// the ones whose default index happens to be 0 (700, 800, 1016, and the seed at
// 3), so that changing one of those defaults later cannot silently re-open the
// hole. The fix is deliberately Seraphis-local: correcting the shared helper
// would move the registered default of every dropdown in Iterum, Ruinae and
// Gradus too, which is a shipped-behaviour change no phase-9 criterion asks for
// (and which gradus_vst_tests.cpp:308 pins as-is).
inline void addDropdownParam(Steinberg::Vst::ParameterContainer& parameters,
                             Steinberg::Vst::StringListParameter* param,
                             int defaultIndex) {
    if (param == nullptr) {
        return;
    }
    const int steps = param->getInfo().stepCount;  // entries - 1
    if (steps > 0) {
        const int index = std::clamp(defaultIndex, 0, steps);
        param->getInfo().defaultNormalizedValue =
            static_cast<Steinberg::Vst::ParamValue>(index) /
            static_cast<Steinberg::Vst::ParamValue>(steps);
    }
    parameters.addParameter(param);
}

/// Table-driven convenience form: builds the parameter from one of the label
/// tables above and pins its default in one step.
inline void addDropdownParam(Steinberg::Vst::ParameterContainer& parameters,
                             const Steinberg::Vst::TChar* title,
                             Steinberg::Vst::ParamID id,
                             int defaultIndex,
                             const Steinberg::Vst::TChar* const* labels,
                             int count) {
    addDropdownParam(parameters,
                     Krate::Plugins::createDropdownParameterWithDefault(
                         title, id, defaultIndex, labels, count),
                     defaultIndex);
}

}  // namespace Seraphis
