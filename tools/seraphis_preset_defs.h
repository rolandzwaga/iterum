#pragma once

// ==============================================================================
// Seraphis - Factory Preset Definitions (Phase 12, FR-016 / FR-016a)
// ==============================================================================
// Spec:  specs/seraphis-phase12-presets-release/spec.md (C-3, C-4, FR-016,
//        FR-016a, FR-029 clause 2, OI-1)
// Tasks: specs/seraphis-phase12-presets-release/tasks.md (T004 created this
//        file with a 3-preset pilot; T005 consumes it in the generator; T014 -
//        DONE - replaced that pilot with the full 42-entry table below)
//
// THIS HEADER IS DATA ONLY.
//
//   - NO state layout. NO component-stream serialization. C-3 is unchanged: the
//     generator produces every preset's `Comp` chunk by calling the SHIPPED
//     `Seraphis::Processor::getState()`, so there is nothing here to drift from
//     (this is why there is no `tools/seraphis_preset_format.h` and no
//     `preset_format_compat_test`, unlike Ruinae).
//   - PARTIALS AUTHORING IS DATA, NOT MESSAGES (FR-016 / OQ-4, re-ratified YES
//     2026-08-30, palette-widening amendment). A definition MAY carry an
//     optional `[partials]` authoring block - a sparse pan list plus a 64-bit
//     mask - and the GENERATOR translates it into the shipped
//     `Processor::notify()` EditMessage surface (kind 3 per set mask bit,
//     kind 2 per authored pan). No `EditMessage` structs live here; the wire
//     shape stays owned by src/ui/edit_message.h and the Comp chunk stays
//     whatever `Processor::getState()` writes.
//   - Values are NORMALIZED 0..1, exactly as a host delivers them (C-4). No
//     denormalization arithmetic is duplicated outside the shipped
//     `handle*ParamChange` functions - every stored value is, by construction,
//     one the plugin can itself reach.
//
// EVERY FUNCTION HERE IS `inline` AND EVERY TABLE IS A FUNCTION-LOCAL
// `static const`. That is a LINK requirement, not a style preference: this
// header is included by three TUs, two of which link into ONE binary
// (`seraphis_tests`), so the anonymous-namespace trick the five existing
// `tools/*_preset_generator.cpp` `PresetDef` structs rely on is unavailable.
// The distinct name `SeraphisPresetDef` is mandatory for the same reason.
// ==============================================================================

#include "plugin_ids.h"  // ${CMAKE_SOURCE_DIR}/plugins/seraphis/src is on the
                         // include path of both consumers.

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Seraphis::PresetDefs {

// ==============================================================================
// Definition types
// ==============================================================================

/// One authored parameter point: a registered `ParamID` and its NORMALIZED
/// value, in the 0..1 units the VST boundary uses. Untouched IDs keep their
/// registered defaults, which `getState()` writes unchanged (C-4).
struct ParamSetting {
    Steinberg::Vst::ParamID id;
    double normalized;
};

/// FR-024a per-preset audition-stimulus override (Q2 / CQ-2 Option B). The
/// sweep's DEFAULT stimulus is pitch 60 / velocity 0.8f; this is present only on
/// the outlier presets that genuinely need something else.
struct AuditionStimulus {
    Steinberg::int16 pitch;     ///< MIDI note number.
    float            velocity;  ///< Normalized 0..1.
};

/// One authored per-partial pan override (FR-016, OQ-4 re-ratified YES
/// 2026-08-30). The generator sends one kind-2 EditMessage per entry; the
/// stored `[partials]` pan float is this value byte-exact and the matching
/// `panOverrideBits` bit is set.
struct PartialPanOverride {
    std::uint16_t index;  // 0..63
    float pan;            // [-1, 1]
};

/// One factory preset, as authored. `category` MUST be one of `kCategories`;
/// `name` is the file stem, the `Info` XML `Name` attribute and the browser row
/// label, so it is ASCII, unique library-wide, and valid per
/// `PresetManager::isValidPresetName` (plugins/shared/src/preset/preset_manager.h:120).
struct SeraphisPresetDef {
    std::string_view          name;
    std::string_view          category;
    std::string_view          description;
    std::vector<ParamSetting> params;
    std::optional<AuditionStimulus> stimulus;
    // Optional `[partials]` authoring block (FR-016 / FR-006a, OQ-4 re-ratified
    // YES 2026-08-30). Defaults mean "no partials authoring": the preset ships
    // the all-zero 272-byte block.
    std::vector<PartialPanOverride> partialPans{};  // empty = no pan authoring
    std::uint64_t partialMaskBits = 0;  // bit i set = partial i MASKED (plugin sense)
};

// ==============================================================================
// Categories (C-1)
// ==============================================================================
// MUST equal `makeSeraphisPresetConfig().subcategoryNames`, element-wise and in
// order (plugins/seraphis/src/preset/seraphis_preset_config.h:24-31). That "MUST"
// is CHECKED, not merely asserted in prose:
// `Seraphis_FactoryPresets_CategoriesMatchConfig` compares the two
// (plugins/seraphis/tests/unit/preset/factory_preset_test.cpp).
//
// This list is the GENERATOR's source for the output subdirectories; the config
// list is what the BROWSER matches a preset's parent directory name against
// (`PresetManager::parsePresetFile`, plugins/shared/src/preset/preset_manager.cpp:95-103,
// which leaves `subcategory` EMPTY on a miss). A divergence between them ships
// presets that exist on disk and never appear in the browser.
inline constexpr std::array<std::string_view, 7> kCategories{
    "Textures", "Pads", "Drones", "Bells", "Choirs", "Motion", "Cinematic"};

// ==============================================================================
// The table - 42 presets, 7 categories x 6 (C-2 / FR-004)
// ==============================================================================
// AUTHORING RULES. Each has a GATE; a breach is re-authored, never re-thresholded.
//
//  1. FR-008 / C-5 - polyphony <= 8 and soft limit ON. NO ENTRY BELOW TOUCHES
//     ID 1 OR ID 2, which is how the rule is met: the registered defaults are
//     already polyphony 8 (index 7, global_params.h:141-148) and soft limit 1.0
//     (global_params.h:152-154), and `getState()` writes untouched IDs unchanged
//     (C-4). Gate: `Seraphis_FactoryPresets_RespectVoiceBudget`.
//
//  2. FR-008a - the authoring timing ceiling, `A <= 12.0 s` and `Rel <= 10.0 s`,
//     where (T007's `makeTimeline`)
//        A   = (envMode == Growth ? growthDurationSec : stage0Ms * 1e-3)
//              + stage1Ms * 1e-3
//        Rel = releaseMs * 1e-3
//     Standard-mode defaults are 2 + 4 = 6 s with Rel 8 s, both inside the
//     ceiling. GROWTH MODE IS A LIVE TRAP: its defaults are 10.0 s + 4000 ms =
//     14 s, OVER the 12 s ceiling (life_mod_params.h:65-67). All three Growth
//     entries below (First Light, Approach Vector, Rising Dread) therefore set
//     701 AND 703 explicitly. Gate:
//     `Seraphis_FactoryPresets_RespectTimingCeiling`.
//
//  3. Normalized values for the log-mapped IDs are COMPUTED, never guessed:
//        normalized = ln(v / mn) / ln(mx / mn)
//     the exact inverse of `logMapFromNormalized = clamp(mn * pow(mx/mn, u), mn,
//     mx)` (plugins/shared/src/ui/parameter_helpers.h:80-83). The DENORMALIZED
//     target is written beside every such value. Ranges used below:
//        208  cloud attack       [0.05, 30] s   harmonic_cloud.h:218-219
//        209  cloud decay        [0.05, 60] s   harmonic_cloud.h:220-221
//        404  travel rate        [1/600, 1] j/s spectral_morph_engine.h:101-102
//        407  waypoint interval  [0.5, 30] s    spline_trajectory.h:117, :119
//        601  spatial rate       [0.01, 0.5] Hz orbit_modulator.h:108, :110
//        701  growth duration    [1, 60] s      growth_envelope.h:96, :98
//        702-704 stage / release [1, 10000] ms  life_mod_params.h:56-58
//        1002 grain density      [0.1, 20] /s   atmosphere_engine.h:303-304
//        1003 grain seconds      [0.05, 30] s   atmosphere_engine.h:301-302
//        1203 aether decay       [0.5, 60] s    aether_params.h:45-46
//     Authoring is nevertheless VERIFIED AGAINST THE DECODER (T007's
//     `decodePresetState` over the generated stream), never against this formula.
//
//  4. Dropdown IDs use `index / (N - 1)`, the exact inverse of the shipped
//     denormalizer `clamp(int(u * (N - 1) + 0.5), 0, N - 1)` (e.g.
//     life_mod_params.h:132-138, body_params.h's ID 800, morph_params.h's
//     `detail::morphDropdownIndex` at :138-142). Toggles are 1.0 ON / 0.0 OFF
//     against the shipped `value >= 0.5` test. IDs 409-412 have 10 labels since
//     the 2026-08-30 palette widening, so their encoding is `index / 9.0`.
//
//  5. Seed spread (ID 3). `kSeedId` is an INDEX into `kSeedValues`, whose index 0
//     is pinned to 1u (dropdown_mappings.h:93-99, global_params.h:53-59). Presets
//     that intend audibly different motion do not all sit on index 0, so the
//     index is spread deliberately across the library.
//
//  6. FR-016 / FR-006a - OQ-4 RE-RATIFIED YES (2026-08-30, palette-widening
//     amendment). An entry MAY author the Phase 11 `[partials]` block through
//     the optional `partialPans` / `partialMaskBits` fields; the generator
//     drives them through the SHIPPED `Processor::notify()` EditMessage surface
//     (kind 3 per set mask bit first, then kind 2 per authored pan). An entry
//     that authors neither ships the all-zero block. Every preset's stored
//     block must EQUAL its definition - bit-for-bit masks, byte-exact pans,
//     0.0f everywhere unauthored.
//     Gate: `Seraphis_FactoryPresets_PartialsBlockMatchesDefs`.
//
//  7. Tail-arm classification is CHOSEN, not discovered: it follows only from the
//     three freeze toggles, so the C-2 coverage assignment below fixes which arm
//     each preset is measured against. 1204 is ON in Event Horizon and Vast,
//     1008 in Ghost Choir, 1430 in Signal Lost, and OFF in every other entry.
//
//  8. `stimulus` is left ABSENT unless a preset genuinely needs an outlier
//     pitch/velocity; the sweep's default is MIDI pitch 60 / velocity 0.8f. No
//     entry in this library needs an override, so all 42 are `std::nullopt`.
//
//  9. FR-027b / C-10 - EVERY ENTRY AUTHORS THE BODY STAGE (IDs 800 and 805)
//     AND, since the 2026-08-30 palette-widening re-authoring, THE MORPH PAIR
//     (IDs 409 and 410; 411/412 too when 408 authors a count > 2). Preset
//     distinctness is carried jointly by the archetype pair and the body pair;
//     the 805 spread spans 0.15-0.85 (0.15x1, 0.25x2, 0.35x5, 0.45x6, 0.55x12,
//     0.70x11, 0.85x5 - six entries in the 0.15-0.45 cloud-forward band and no
//     entry left on the 1.0 default).
//
//     The body half of this rule was added by the GROUP 18 build pass, and it
//     exists because the first 42-entry draft did NOT author it and SC-028
//     failed with 14 of the 861 pairs under the 0.02 floor (closest pair
//     d = 0.0032).
//
//     THE CAUSE, MEASURED not guessed: `kBodyMixDefault` is 1.0
//     (body_params.h:86) and `ContinuousBody::setMix(1)` is defined as
//     "body+cloud only", the dry voice being what `setMix(0)` passes through
//     (continuous_body.h:219-221). So a preset that leaves 805 alone is heard
//     ENTIRELY through the default Glass resonator (800's default index is
//     `ContinuousBody::kDefaultMaterial`, body_params.h:93-95). The 34 entries
//     that did that produced the SAME ring in the FR-024 sustain window
//     [A+1, A+4]: their unit-RMS `totalVariation / meanAbs` all landed in
//     10 016 +/- 0.6 %, while the eight entries that DID author the body spanned
//     14 000 - 58 000. A grid render over
//     {800 index 0..4} x {805 in 0.35, 0.55, 0.70, 0.85} for those 34 (586 renders,
//     44 100 Hz, one per cell) measured the response; the body pairs below were
//     chosen from that measurement to maximise the minimum pairwise distance.
//     Levers that did NOT move the metric and are therefore NOT the fix: cloud
//     tilt (202) +/-8 dB/oct moved `d` by < 0.5 %, cloud richness (200) by < 0.4 %,
//     seed (3) not at all - all three are downstream of a body stage that replaces
//     the voice.
//
//     A breach of the floor is re-authored HERE, never accommodated in the test
//     (tasks.md T019). Gate: `Seraphis_PresetSweep_PresetsAreDistinct`.
//
// C-2 COVERAGE ASSIGNMENT, pinned by name so authoring is not a search (the
// harness still computes its ledger from the DECODED states, never from here):
//   body material 800   Glass/Strings/Metal Plate/Chamber/Ice
//                       -> Sea Glass / Continuum / Bronze Halo / Cathedral Moss
//                          / Struck Ice
//   spectral state 409-412  Sine Stack/Bell/Choir/Glass/Breath/Hollow/Metal/
//                       Organ/Vowel/Shimmer
//                       -> First Light (409=0.0) / Frost Bell / Vowel Field /
//                          Glass Carillon (410) / Breath Chorus / Quiet Machine
//                          / Iron Lung / Cathedral Moss / Aeolian Voices /
//                          Sea Glass
//   [partials] authored mask -> Bell Garden;  authored pan -> Sea Glass
//   state count 408     2/3/4  -> Vellum / Tide Pool / Slow Weather
//   travel mode 403     External/Spline -> Orbit Study / Wander Lamp
//   env mode 700        Standard (most) / Growth -> First Light, Approach
//                       Vector, Rising Dread
//   grain env 1016      Hann (default, many) / Trapezoid -> Quiet Machine /
//                       Blackman -> Slow Snow / Exponential -> Aftermath
//   1204 ON             Event Horizon, Vast        1008 ON  Ghost Choir
//   1430 ON             Signal Lost                1418 ON  Restless
//   812 ON              Warm Static                811 OFF  Iron Lung
[[nodiscard]] inline const std::vector<SeraphisPresetDef>& allPresets() {
    static const std::vector<SeraphisPresetDef> kPresets = {
        // ============================================================== //
        // Textures                                                        //
        // ============================================================== //
        SeraphisPresetDef{
            /*.name =*/"Vellum",
            /*.category =*/"Textures",
            /*.description =*/"Thin, papery two-state morph with a soft upper tilt.",
            /*.params =*/
            {
                // ID 408, 3 labels ("2"/"3"/"4", dropdown_mappings.h:165-166):
                // index 0 -> state count 2. C-2 coverage holder for count 2.
                {kMorphStateCountId, 0.0},
                {kMorphState0Id, 0.0},        // 409 index 0 -> Sine Stack
                {kMorphState1Id, 4.0 / 9.0},  // 410 index 4 -> Breath
                {kBodyMaterialId, 0.0},       // 800 index 0 -> Glass
                {kBodyMixId, 0.25},           // 805 lin [0, 1] -> 0.25
                {kCloudRichnessId, 0.35},
                {kCloudTiltId, 0.55},  // 202 lin [-12, +12] -> +1.2 dB/oct
                {kCloudMutationId, 0.15},
                {kMacroDreamId, 0.30},
                {kAetherMixId, 0.45},
                {kAtmosLevelId, 0.175},  // 1000 lin [0, 2] -> 0.35
                {kSeedId, 1.0 / 15.0},
            },
            /*.stimulus =*/std::nullopt,
            // 3.5 [partials]: bit i set = partial i MASKED. 0xAAAA... masks the
            // odd 0-based indices (= the even harmonics), leaving the
            // odd-harmonic hollow variant.
            /*.partialPans =*/{},
            /*.partialMaskBits =*/0xAAAAAAAAAAAAAAAAull},
        // ---------------------------------------------------------------- //
        SeraphisPresetDef{
            /*.name =*/"Sea Glass",
            /*.category =*/"Textures",
            /*.description =*/"Bright glass body, barely damped, over a sparse cloud.",
            /*.params =*/
            {
                // ID 800, 5 labels (dropdown_mappings.h:198-200): index 0 ->
                // Glass. C-2 coverage holder for the Glass material.
                {kBodyMaterialId, 0.0},
                {kBodyResonanceId, 0.85},
                {kBodyDampingId, 0.15},
                {kBodyMixId, 0.45},           // 805 lin [0, 1] -> 0.45
                // C-2 coverage holder for Shimmer.
                {kMorphState0Id, 1.0},        // 409 index 9 -> Shimmer
                {kMorphState1Id, 3.0 / 9.0},  // 410 index 3 -> Glass
                {kCloudRichnessId, 0.55},
                {kCloudInharmonicityId, 0.45},  // 201 lin [0, 0.1] -> 0.045
                {kAetherMixId, 0.50},
                {kSeedId, 2.0 / 15.0},
            },
            /*.stimulus =*/std::nullopt,
            // 3.5 [partials] pan scatter (C-2 authored-pan owner):
            // pan_i = (i even ? +1 : -1) * (0.8 - 0.04*i) for i = 0..15.
            /*.partialPans =*/
            {{0, 0.80f},   {1, -0.76f},  {2, 0.72f},   {3, -0.68f},
             {4, 0.64f},   {5, -0.60f},  {6, 0.56f},   {7, -0.52f},
             {8, 0.48f},   {9, -0.44f},  {10, 0.40f},  {11, -0.36f},
             {12, 0.32f},  {13, -0.28f}, {14, 0.24f},  {15, -0.20f}},
            /*.partialMaskBits =*/0},
        // ---------------------------------------------------------------- //
        SeraphisPresetDef{
            /*.name =*/"Slow Snow",
            /*.category =*/"Textures",
            /*.description =*/"Sparse Blackman grains falling through a quiet cloud.",
            /*.params =*/
            {
                // Rule 9 (FR-027b): body stage + morph pair are this preset's
                // distinctness axes (2026-08-30 palette-widening re-authoring).
                {kBodyMaterialId, 1.0 / 4.0},  // 800 index 1 -> Strings
                {kBodyMixId, 0.35},            // 805 lin [0, 1] -> 0.35
                {kMorphState0Id, 4.0 / 9.0},   // 409 index 4 -> Breath
                {kMorphState1Id, 8.0 / 9.0},   // 410 index 8 -> Vowel
                // ID 1016, 6 labels (dropdown_mappings.h:211-213): index 3 ->
                // Blackman. C-2 coverage holder for the Blackman window.
                {kAtmosGrainEnvelopeId, 3.0 / 5.0},
                {kAtmosLevelId, 0.45},          // 1000 lin [0, 2] -> 0.90
                {kAtmosDensityId, 0.46899921},  // 1002 log [0.1, 20] -> 1.2 /s
                {kAtmosGrainSecondsId, 0.74840463},  // 1003 log [0.05, 30] -> 6 s
                {kAtmosBlurId, 0.70},
                {kCloudRichnessId, 0.30},
                {kSeedId, 3.0 / 15.0},
            },
            /*.stimulus =*/std::nullopt},
        // ---------------------------------------------------------------- //
        SeraphisPresetDef{
            /*.name =*/"Paper Sky",
            /*.category =*/"Textures",
            /*.description =*/"Wide, downward-tilted wash with a slow partial drift.",
            /*.params =*/
            {
                // Rule 9 (FR-027b): body stage + morph pair are this preset's
                // distinctness axes (2026-08-30 palette-widening re-authoring).
                {kBodyMaterialId, 3.0 / 4.0},  // 800 index 3 -> Chamber
                {kBodyMixId, 0.55},            // 805 lin [0, 1] -> 0.55
                {kMorphState0Id, 7.0 / 9.0},   // 409 index 7 -> Organ
                {kMorphState1Id, 2.0 / 9.0},   // 410 index 2 -> Choir
                {kCloudTiltId, 0.25},        // 202 lin [-12, +12] -> -6 dB/oct
                {kCloudRichnessId, 0.45},
                {kCloudDriftDepthId, 0.24},  // 205 lin [0, 50] -> 12 cents
                {kCloudStereoSpreadId, 0.75},
                {kLifeVoiceWidthId, 0.80},   // 604 lin [50, 150] -> 130 %
                {kAtmosLevelId, 0.30},       // 1000 lin [0, 2] -> 0.60
                {kAetherMixId, 0.40},
                {kSeedId, 5.0 / 15.0},
            },
            /*.stimulus =*/std::nullopt},
        // ---------------------------------------------------------------- //
        SeraphisPresetDef{
            /*.name =*/"Rust Bloom",
            /*.category =*/"Textures",
            /*.description =*/"Saturated, inharmonic and slowly mutating.",
            /*.params =*/
            {
                // Rule 9 (FR-027b): body stage + morph pair are this preset's
                // distinctness axes (2026-08-30 palette-widening re-authoring).
                {kBodyMaterialId, 1.0},       // 800 index 4 -> Ice
                {kBodyMixId, 0.70},           // 805 lin [0, 1] -> 0.70
                {kMorphState0Id, 6.0 / 9.0},  // 409 index 6 -> Metal
                {kMorphState1Id, 3.0 / 9.0},  // 410 index 3 -> Glass
                {kFxSaturationId, 0.65},
                {kCloudInharmonicityId, 0.80},  // 201 lin [0, 0.1] -> 0.08
                {kCloudMutationId, 0.60},
                {kCloudRichnessId, 0.50},
                {kMacroEntropyId, 0.50},
                {kAetherMixId, 0.30},
                {kSeedId, 6.0 / 15.0},
            },
            /*.stimulus =*/std::nullopt},
        // ---------------------------------------------------------------- //
        SeraphisPresetDef{
            /*.name =*/"Quiet Machine",
            /*.category =*/"Textures",
            /*.description =*/"Dense short trapezoid grains, jittered into a hum.",
            /*.params =*/
            {
                // Rule 9 (FR-027b): body stage + morph pair are this preset's
                // distinctness axes (2026-08-30 palette-widening re-authoring).
                {kBodyMaterialId, 3.0 / 4.0},  // 800 index 3 -> Chamber
                {kBodyMixId, 0.15},            // 805 lin [0, 1] -> 0.15
                // C-2 coverage holder for Hollow.
                {kMorphState0Id, 5.0 / 9.0},   // 409 index 5 -> Hollow
                {kMorphState1Id, 6.0 / 9.0},   // 410 index 6 -> Metal
                // ID 1016 index 1 -> Trapezoid. C-2 coverage holder.
                {kAtmosGrainEnvelopeId, 1.0 / 5.0},
                {kAtmosDensityId, 0.82706005},       // 1002 log [0.1, 20] -> 8 /s
                {kAtmosGrainSecondsId, 0.30419440},  // 1003 log [0.05, 30] -> 0.35 s
                {kAtmosJitterId, 0.80},
                {kAtmosLevelId, 0.35},               // 1000 lin [0, 2] -> 0.70
                {kCloudRichnessId, 0.25},
                {kSeedId, 7.0 / 15.0},
            },
            /*.stimulus =*/std::nullopt,
            // 3.5 [partials] hard L/R split: pan_i = (i even ? -0.9 : +0.9)
            // for i = 0..11.
            /*.partialPans =*/
            {{0, -0.9f}, {1, 0.9f}, {2, -0.9f}, {3, 0.9f},
             {4, -0.9f}, {5, 0.9f}, {6, -0.9f}, {7, 0.9f},
             {8, -0.9f}, {9, 0.9f}, {10, -0.9f}, {11, 0.9f}},
            /*.partialMaskBits =*/0},

        // ============================================================== //
        // Pads                                                            //
        // ============================================================== //
        SeraphisPresetDef{
            /*.name =*/"First Light",
            /*.category =*/"Pads",
            /*.description =*/"Sine-stack pad that grows in over eight seconds.",
            /*.params =*/
            {
                // Rule 9 (FR-027b): body stage + morph pair are this preset's
                // distinctness axes (2026-08-30 palette-widening re-authoring).
                {kBodyMaterialId, 1.0},  // 800 index 4 -> Ice
                {kBodyMixId, 0.45},      // 805 lin [0, 1] -> 0.45
                // ID 409, 10 labels (dropdown_mappings.h:180-183): index 0 ->
                // Sine Stack. C-2 coverage holder for Sine Stack.
                {kMorphState0Id, 0.0},   // 409 index 0 -> Sine Stack
                {kMorphState1Id, 1.0},   // 410 index 9 -> Shimmer
                // ID 700, 2 labels (dropdown_mappings.h:193-194): index 1 ->
                // Growth. FR-008a IS A LIVE TRAP HERE: in Growth mode
                // A = growthDurationSec + stage1Ms/1000, and the DEFAULTS are
                // 10.0 s + 4000 ms = 14 s, over the 12 s ceiling
                // (life_mod_params.h:65-67). Both 701 and 703 are therefore set
                // explicitly below: 8 + 3 = 11 s.
                {kEnvModeId, 1.0},
                {kEnvGrowthDurationId, 0.50788142},  // 701 log [1, 60] s -> 8 s
                {kEnvStage1MsId, 0.86928031},        // 703 log [1, 1e4] ms -> 3000
                {kMacroBloomId, 0.45},
                {kAetherMixId, 0.50},
            },
            /*.stimulus =*/std::nullopt},
        // ---------------------------------------------------------------- //
        SeraphisPresetDef{
            /*.name =*/"Long Exhale",
            /*.category =*/"Pads",
            /*.description =*/"Standard envelope stretched to the ceiling: A 8 s, release 9 s.",
            /*.params =*/
            {
                // Rule 9 (FR-027b): body stage + morph pair are this preset's
                // distinctness axes (2026-08-30 palette-widening re-authoring).
                {kBodyMaterialId, 1.0},       // 800 index 4 -> Ice
                {kBodyMixId, 0.55},           // 805 lin [0, 1] -> 0.55
                {kMorphState0Id, 4.0 / 9.0},  // 409 index 4 -> Breath
                {kMorphState1Id, 5.0 / 9.0},  // 410 index 5 -> Hollow
                // Standard mode: A = stage0 + stage1 = 3 + 5 = 8 s <= 12,
                // Rel = 9 s <= 10 (FR-008a).
                {kEnvStage0MsId, 0.86928031},   // 702 log [1, 1e4] ms -> 3000
                {kEnvStage1MsId, 0.92474250},   // 703 log [1, 1e4] ms -> 5000
                {kEnvReleaseMsId, 0.98856063},  // 704 log [1, 1e4] ms -> 9000
                {kCloudDecayId, 0.77300134},    // 209 log [0.05, 60] s -> 12 s
                {kAetherMixId, 0.55},
                {kSeedId, 8.0 / 15.0},
            },
            /*.stimulus =*/std::nullopt},
        // ---------------------------------------------------------------- //
        SeraphisPresetDef{
            /*.name =*/"Cathedral Moss",
            /*.category =*/"Pads",
            /*.description =*/"Chamber body inside a large, soft Aether space.",
            /*.params =*/
            {
                // ID 800 index 3 -> Chamber. C-2 coverage holder.
                {kBodyMaterialId, 3.0 / 4.0},
                {kBodyResonanceId, 0.60},
                {kBodyMixId, 0.70},           // 805 lin [0, 1] -> 0.70
                // C-2 coverage holder for Organ.
                {kMorphState0Id, 7.0 / 9.0},  // 409 index 7 -> Organ
                {kMorphState1Id, 3.0 / 9.0},  // 410 index 3 -> Glass
                {kAetherMixId, 0.65},
                {kAetherSizeId, 0.80},
                {kAetherDecayId, 0.57913180},  // 1203 log [0.5, 60] s -> 8 s
                {kSeedId, 9.0 / 15.0},
            },
            /*.stimulus =*/std::nullopt},
        // ---------------------------------------------------------------- //
        SeraphisPresetDef{
            /*.name =*/"Warm Static",
            /*.category =*/"Pads",
            /*.description =*/"Resonator bypassed - saturation and decorrelated grain only.",
            /*.params =*/
            {
                // ID 812 ON. C-2 coverage holder for resonator bypass ON; every
                // other entry leaves it at its OFF default
                // (continuous_body.h:164).
                {kBodyResonatorBypassId, 1.0},
                // 800/805 are DEFENSIVE here - the bypass takes the resonator
                // out of the path - but rule 9 still authors them.
                {kBodyMaterialId, 0.0},       // 800 index 0 -> Glass
                {kBodyMixId, 0.55},           // 805 lin [0, 1] -> 0.55
                {kMorphState0Id, 8.0 / 9.0},  // 409 index 8 -> Vowel
                {kMorphState1Id, 6.0 / 9.0},  // 410 index 6 -> Metal
                {kFxSaturationId, 0.50},
                {kAtmosLevelId, 0.40},  // 1000 lin [0, 2] -> 0.80
                {kAtmosDecorrelationId, 0.90},
                {kCloudRichnessId, 0.70},
                {kSeedId, 10.0 / 15.0},
            },
            /*.stimulus =*/std::nullopt},
        // ---------------------------------------------------------------- //
        SeraphisPresetDef{
            /*.name =*/"Distant Choir",
            /*.category =*/"Pads",
            /*.description =*/"Choral state, pre-delayed and set well back.",
            /*.params =*/
            {
                // Rule 9 (FR-027b): body stage + morph pair are this preset's
                // distinctness axes (2026-08-30 palette-widening re-authoring).
                {kBodyMaterialId, 1.0},       // 800 index 4 -> Ice
                {kBodyMixId, 0.70},           // 805 lin [0, 1] -> 0.70
                {kMorphState0Id, 2.0 / 9.0},  // 409 index 2 -> Choir
                {kMorphState1Id, 1.0},        // 410 index 9 -> Shimmer
                {kAetherMixId, 0.75},
                {kAetherPreDelayId, 0.30},  // 1207 lin [0, 200] ms -> 60 ms
                {kAetherDecayId, 0.51904145},  // 1203 log [0.5, 60] s -> 6 s
                {kCloudTiltId, 1.0 / 3.0},  // 202 lin [-12, +12] -> -4 dB/oct
                {kMacroDreamId, 0.60},
                {kSeedId, 11.0 / 15.0},
            },
            /*.stimulus =*/std::nullopt},
        // ---------------------------------------------------------------- //
        SeraphisPresetDef{
            /*.name =*/"Blue Hour",
            /*.category =*/"Pads",
            /*.description =*/"Dim, slowly orbiting pad with a wide grain bed.",
            /*.params =*/
            {
                // Rule 9 (FR-027b): body stage + morph pair are this preset's
                // distinctness axes (2026-08-30 palette-widening re-authoring).
                {kBodyMaterialId, 3.0 / 4.0},  // 800 index 3 -> Chamber
                {kBodyMixId, 0.35},            // 805 lin [0, 1] -> 0.35
                {kMorphState0Id, 3.0 / 9.0},   // 409 index 3 -> Glass
                {kMorphState1Id, 5.0 / 9.0},   // 410 index 5 -> Hollow
                {kCloudTiltId, 0.41666667},  // 202 lin [-12, +12] -> -2 dB/oct
                {kCloudRichnessId, 0.50},
                {kLifeSpatialDepthId, 0.60},
                {kLifeSpatialRateId, 0.53155146},  // 601 log [0.01, 0.5] -> 0.08 Hz
                {kAtmosLevelId, 0.375},            // 1000 lin [0, 2] -> 0.75
                {kAetherMixId, 0.60},
                {kSeedId, 12.0 / 15.0},
            },
            /*.stimulus =*/std::nullopt},

        // ============================================================== //
        // Drones                                                          //
        // ============================================================== //
        SeraphisPresetDef{
            /*.name =*/"Deep Well",
            /*.category =*/"Drones",
            /*.description =*/"Low, unlit drone with a twenty-second Aether tail.",
            /*.params =*/
            {
                // Rule 9 (FR-027b): body stage + morph pair are this preset's
                // distinctness axes (2026-08-30 palette-widening re-authoring).
                {kBodyMaterialId, 2.0 / 4.0},  // 800 index 2 -> Metal Plate
                {kBodyMixId, 0.70},            // 805 lin [0, 1] -> 0.70
                {kMorphState0Id, 6.0 / 9.0},   // 409 index 6 -> Metal
                {kMorphState1Id, 7.0 / 9.0},   // 410 index 7 -> Organ
                // 1203, log map [0.5, 60] s -> ln(20/0.5)/ln(60/0.5). Freeze
                // (1204) is deliberately left OFF, so this preset's tail arm is
                // C-6.3 case 3 - decay consistent with its own stored RT60.
                {kAetherDecayId, 0.77052445},
                {kAetherMixId, 0.55},
                {kCloudTiltId, 0.25},  // 202 lin [-12, +12] -> -6 dB/oct
                {kCloudRichnessId, 0.60},
                // ID 3, 16 seed labels (dropdown_mappings.h:93-99): index 4 ->
                // 4/15. Seed spread, not decoration - index 0 is pinned to 1u
                // and presets that intend audibly different motion must not all
                // sit on it (authoring rule 5).
                {kSeedId, 4.0 / 15.0},
            },
            /*.stimulus =*/std::nullopt},
        // ---------------------------------------------------------------- //
        SeraphisPresetDef{
            /*.name =*/"Stone Circle",
            /*.category =*/"Drones",
            /*.description =*/"Very slow onset into a thirty-second room.",
            /*.params =*/
            {
                // Rule 9 (FR-027b): body stage + morph pair are this preset's
                // distinctness axes (2026-08-30 palette-widening re-authoring).
                {kBodyMaterialId, 1.0 / 4.0},  // 800 index 1 -> Strings
                {kBodyMixId, 0.85},            // 805 lin [0, 1] -> 0.85
                {kMorphState0Id, 5.0 / 9.0},   // 409 index 5 -> Hollow
                {kMorphState1Id, 1.0 / 9.0},   // 410 index 1 -> Bell
                {kAetherDecayId, 0.85521705},  // 1203 log [0.5, 60] s -> 30 s
                {kAetherSizeId, 0.90},
                {kAetherMixId, 0.60},
                {kCloudRichnessId, 0.75},
                {kCloudAttackId, 0.68502029},  // 208 log [0.05, 30] s -> 4 s
                {kMacroGravityId, 0.70},
                {kSeedId, 13.0 / 15.0},
            },
            /*.stimulus =*/std::nullopt},
        // ---------------------------------------------------------------- //
        SeraphisPresetDef{
            /*.name =*/"Tectonic",
            /*.category =*/"Drones",
            /*.description =*/"Heavy low tilt, inharmonic and driven hard.",
            /*.params =*/
            {
                // Rule 9 (FR-027b): body stage + morph pair are this preset's
                // distinctness axes (2026-08-30 palette-widening re-authoring).
                {kBodyMaterialId, 1.0},       // 800 index 4 -> Ice
                {kBodyMixId, 0.55},           // 805 lin [0, 1] -> 0.55
                {kMorphState0Id, 6.0 / 9.0},  // 409 index 6 -> Metal
                {kMorphState1Id, 4.0 / 9.0},  // 410 index 4 -> Breath
                {kCloudTiltId, 0.08333333},     // 202 lin [-12, +12] -> -10 dB/oct
                {kCloudRichnessId, 0.85},
                {kCloudInharmonicityId, 0.60},  // 201 lin [0, 0.1] -> 0.06
                {kFxSaturationId, 0.70},
                {kAetherDecayId, 0.71043410},   // 1203 log [0.5, 60] s -> 15 s
                {kSeedId, 14.0 / 15.0},
            },
            /*.stimulus =*/std::nullopt},
        // ---------------------------------------------------------------- //
        SeraphisPresetDef{
            /*.name =*/"Iron Lung",
            /*.category =*/"Drones",
            /*.description =*/"Body input AGC off - the resonator takes the full drive.",
            /*.params =*/
            {
                // ID 811 OFF. C-2 coverage holder for input AGC OFF; every other
                // entry leaves it at its ON default (continuous_body.h:163).
                {kBodyInputAgcId, 0.0},
                {kBodyDriveId, 0.625},  // 804 lin [0, 4] -> 2.5
                {kBodyMaterialId, 2.0 / 4.0},  // 800 index 2 -> Metal Plate
                {kBodyMixId, 0.85},            // 805 lin [0, 1] -> 0.85
                {kBodyResonanceId, 0.80},
                // C-2 coverage holder for Metal.
                {kMorphState0Id, 6.0 / 9.0},   // 409 index 6 -> Metal
                {kMorphState1Id, 5.0 / 9.0},   // 410 index 5 -> Hollow
                {kCloudRichnessId, 0.50},
                {kSeedId, 15.0 / 15.0},
            },
            /*.stimulus =*/std::nullopt},
        // ---------------------------------------------------------------- //
        SeraphisPresetDef{
            /*.name =*/"Continuum",
            /*.category =*/"Drones",
            /*.description =*/"Strings body, almost undamped, held indefinitely.",
            /*.params =*/
            {
                // ID 800 index 1 -> Strings. C-2 coverage holder.
                {kBodyMaterialId, 1.0 / 4.0},
                {kBodyResonanceId, 0.90},
                {kBodyDampingId, 0.10},
                {kBodyMixId, 0.70},           // 805 lin [0, 1] -> 0.70
                {kMorphState0Id, 7.0 / 9.0},  // 409 index 7 -> Organ
                {kMorphState1Id, 0.0},        // 410 index 0 -> Sine Stack
                {kCloudRichnessId, 0.65},
                {kAetherMixId, 0.40},
                {kSeedId, 2.0 / 15.0},
            },
            /*.stimulus =*/std::nullopt},
        // ---------------------------------------------------------------- //
        SeraphisPresetDef{
            /*.name =*/"Undertow",
            /*.category =*/"Drones",
            /*.description =*/"Tidal Aether motion under a detuning cloud.",
            /*.params =*/
            {
                // Rule 9 (FR-027b): body stage + morph pair are this preset's
                // distinctness axes (2026-08-30 palette-widening re-authoring).
                {kBodyMaterialId, 3.0 / 4.0},  // 800 index 3 -> Chamber
                {kBodyMixId, 0.45},            // 805 lin [0, 1] -> 0.45
                {kMorphState0Id, 8.0 / 9.0},   // 409 index 8 -> Vowel
                {kMorphState1Id, 4.0 / 9.0},   // 410 index 4 -> Breath
                {kAetherDecayId, 0.81713415},  // 1203 log [0.5, 60] s -> 25 s
                {kAetherTideDepthId, 0.80},
                {kAetherModDepthId, 0.50},
                {kAetherMixId, 0.55},
                {kCloudDriftDepthId, 0.40},    // 205 lin [0, 50] -> 20 cents
                {kCloudRichnessId, 0.55},
                {kSeedId, 5.0 / 15.0},
            },
            /*.stimulus =*/std::nullopt},

        // ============================================================== //
        // Bells                                                           //
        // ============================================================== //
        SeraphisPresetDef{
            /*.name =*/"Frost Bell",
            /*.category =*/"Bells",
            /*.description =*/"Bell state, thin and cold, with a six-second cloud decay.",
            /*.params =*/
            {
                // Rule 9 (FR-027b): body stage + morph pair are this preset's
                // distinctness axes (2026-08-30 palette-widening re-authoring).
                {kBodyMaterialId, 1.0},  // 800 index 4 -> Ice
                {kBodyMixId, 0.85},      // 805 lin [0, 1] -> 0.85
                // ID 409 index 1 -> Bell. C-2 coverage holder.
                {kMorphState0Id, 1.0 / 9.0},  // 409 index 1 -> Bell
                {kMorphState1Id, 3.0 / 9.0},  // 410 index 3 -> Glass
                {kCloudInharmonicityId, 0.50},  // 201 lin [0, 0.1] -> 0.05
                {kCloudDecayId, 0.67523834},    // 209 log [0.05, 60] s -> 6 s
                {kCloudStereoSpreadId, 0.70},
                {kAetherMixId, 0.50},
                {kSeedId, 6.0 / 15.0},
            },
            /*.stimulus =*/std::nullopt},
        // ---------------------------------------------------------------- //
        SeraphisPresetDef{
            /*.name =*/"Temple Rim",
            /*.category =*/"Bells",
            /*.description =*/"Long struck rim, nearly harmonic, in a twelve-second room.",
            /*.params =*/
            {
                // Rule 9 (FR-027b): body stage + morph pair are this preset's
                // distinctness axes (2026-08-30 palette-widening re-authoring).
                {kBodyMaterialId, 1.0 / 4.0},  // 800 index 1 -> Strings
                {kBodyMixId, 0.55},            // 805 lin [0, 1] -> 0.55
                {kMorphState0Id, 6.0 / 9.0},   // 409 index 6 -> Metal
                {kMorphState1Id, 1.0 / 9.0},   // 410 index 1 -> Bell
                {kCloudDecayId, 0.84504931},    // 209 log [0.05, 60] s -> 20 s
                {kCloudInharmonicityId, 0.20},  // 201 lin [0, 0.1] -> 0.02
                {kCloudTiltId, 0.375},          // 202 lin [-12, +12] -> -3 dB/oct
                {kAetherMixId, 0.60},
                {kAetherDecayId, 0.66382440},   // 1203 log [0.5, 60] s -> 12 s
                {kSeedId, 7.0 / 15.0},
            },
            /*.stimulus =*/std::nullopt},
        // ---------------------------------------------------------------- //
        SeraphisPresetDef{
            /*.name =*/"Glass Carillon",
            /*.category =*/"Bells",
            /*.description =*/"Parked on the glass state, struck and left to ring.",
            /*.params =*/
            {
                // Rule 9 (FR-027b): body stage + morph pair are this preset's
                // distinctness axes (2026-08-30 palette-widening re-authoring).
                {kBodyMaterialId, 3.0 / 4.0},  // 800 index 3 -> Chamber
                {kBodyMixId, 0.70},            // 805 lin [0, 1] -> 0.70
                {kMorphState0Id, 1.0},         // 409 index 9 -> Shimmer
                // ID 410 index 3 -> Glass. C-2 coverage holder (it is also the
                // registered default of slot 1, morph_params.h's
                // kMorphSlotDefaultIndices = {0, 3, 0, 0}; it is set explicitly
                // here so the coverage row has a NAMED owner).
                {kMorphState1Id, 3.0 / 9.0},
                {kMorphPositionId, 1.0 / 3.0},  // 402 lin [0, 3] -> 1.0
                {kCloudDecayId, 0.71581366},    // 209 log [0.05, 60] s -> 8 s
                {kAetherMixId, 0.45},
                {kSeedId, 8.0 / 15.0},
            },
            /*.stimulus =*/std::nullopt},
        // ---------------------------------------------------------------- //
        SeraphisPresetDef{
            /*.name =*/"Struck Ice",
            /*.category =*/"Bells",
            /*.description =*/"Ice body, high resonance, almost no damping.",
            /*.params =*/
            {
                // ID 800 index 4 -> Ice. C-2 coverage holder.
                {kBodyMaterialId, 4.0 / 4.0},
                {kBodyResonanceId, 0.95},
                {kBodyDampingId, 0.05},
                {kBodyMixId, 0.85},           // 805 lin [0, 1] -> 0.85
                {kMorphState0Id, 1.0 / 9.0},  // 409 index 1 -> Bell
                {kMorphState1Id, 1.0},        // 410 index 9 -> Shimmer
                {kCloudDecayId, 0.61805066},  // 209 log [0.05, 60] s -> 4 s
                {kSeedId, 9.0 / 15.0},
            },
            /*.stimulus =*/std::nullopt},
        // ---------------------------------------------------------------- //
        SeraphisPresetDef{
            /*.name =*/"Bronze Halo",
            /*.category =*/"Bells",
            /*.description =*/"Metal plate, driven, ringing well past the note.",
            /*.params =*/
            {
                // ID 800 index 2 -> Metal Plate. C-2 coverage holder.
                {kBodyMaterialId, 2.0 / 4.0},
                {kBodyResonanceId, 0.85},
                {kBodyDriveId, 0.45},  // 804 lin [0, 4] -> 1.8
                {kBodyMixId, 0.70},           // 805 lin [0, 1] -> 0.70
                {kMorphState0Id, 1.0 / 9.0},  // 409 index 1 -> Bell
                {kMorphState1Id, 6.0 / 9.0},  // 410 index 6 -> Metal
                {kCloudInharmonicityId, 0.90},  // 201 lin [0, 0.1] -> 0.09
                {kSeedId, 10.0 / 15.0},
            },
            /*.stimulus =*/std::nullopt},
        // ---------------------------------------------------------------- //
        SeraphisPresetDef{
            /*.name =*/"Bell Garden",
            /*.category =*/"Bells",
            /*.description =*/"Partials entering at staggered times, spread hard across the field.",
            /*.params =*/
            {
                // Rule 9 (FR-027b): body stage + morph pair are this preset's
                // distinctness axes (2026-08-30 palette-widening re-authoring).
                {kBodyMaterialId, 2.0 / 4.0},  // 800 index 2 -> Metal Plate
                {kBodyMixId, 0.55},            // 805 lin [0, 1] -> 0.55
                {kMorphState0Id, 1.0 / 9.0},   // 409 index 1 -> Bell
                {kMorphState1Id, 7.0 / 9.0},   // 410 index 7 -> Organ
                {kCloudEnvOffsetSpreadId, 0.80},
                {kCloudStereoSpreadId, 0.90},
                {kCloudDecayId, 0.74728631},  // 209 log [0.05, 60] s -> 10 s
                {kCloudRichnessId, 0.40},
                {kAetherMixId, 0.50},
                {kSeedId, 11.0 / 15.0},
            },
            /*.stimulus =*/std::nullopt,
            // 3.5 [partials] (C-2 authored-mask owner): bit i set = partial i
            // MASKED. 0x6DB6... keeps every partial with i % 3 == 0.
            /*.partialPans =*/{},
            /*.partialMaskBits =*/0x6DB6DB6DB6DB6DB6ull},

        // ============================================================== //
        // Choirs                                                          //
        // ============================================================== //
        SeraphisPresetDef{
            /*.name =*/"Vowel Field",
            /*.category =*/"Choirs",
            /*.description =*/"The choir state held open, tilted down and blurred.",
            /*.params =*/
            {
                // Rule 9 (FR-027b): body stage + morph pair are this preset's
                // distinctness axes (2026-08-30 palette-widening re-authoring).
                {kBodyMaterialId, 3.0 / 4.0},  // 800 index 3 -> Chamber
                {kBodyMixId, 0.35},            // 805 lin [0, 1] -> 0.35
                // ID 409 index 2 -> Choir. C-2 coverage holder.
                {kMorphState0Id, 2.0 / 9.0},   // 409 index 2 -> Choir
                {kMorphState1Id, 8.0 / 9.0},   // 410 index 8 -> Vowel
                {kCloudRichnessId, 0.55},
                {kCloudTiltId, 0.29166667},  // 202 lin [-12, +12] -> -5 dB/oct
                {kAtmosLevelId, 0.20},       // 1000 lin [0, 2] -> 0.40
                {kAetherMixId, 0.50},
                {kSeedId, 12.0 / 15.0},
            },
            /*.stimulus =*/std::nullopt},
        // ---------------------------------------------------------------- //
        SeraphisPresetDef{
            /*.name =*/"Breath Chorus",
            /*.category =*/"Choirs",
            /*.description =*/"Breath into choir - the airiest pair in the library.",
            /*.params =*/
            {
                // Rule 9 (FR-027b): body stage + morph pair are this preset's
                // distinctness axes (2026-08-30 palette-widening re-authoring).
                {kBodyMaterialId, 0.0},  // 800 index 0 -> Glass
                {kBodyMixId, 0.45},      // 805 lin [0, 1] -> 0.45
                // ID 409 index 4 -> Breath. C-2 coverage holder.
                {kMorphState0Id, 4.0 / 9.0},  // 409 index 4 -> Breath
                {kMorphState1Id, 2.0 / 9.0},  // 410 index 2 -> Choir
                {kCloudMutationId, 0.50},
                {kAtmosLevelId, 0.30},        // 1000 lin [0, 2] -> 0.60
                {kAetherMixId, 0.55},
                {kSeedId, 13.0 / 15.0},
            },
            /*.stimulus =*/std::nullopt},
        // ---------------------------------------------------------------- //
        SeraphisPresetDef{
            /*.name =*/"Ghost Choir",
            /*.category =*/"Choirs",
            /*.description =*/"Atmosphere frozen: the grain bed stops moving and hangs.",
            /*.params =*/
            {
                // ID 1008 ON. C-2 coverage holder for the Atmosphere freeze; OFF
                // in every other entry (aether 1204 and fx 1430 stay OFF here, so
                // this preset's tail arm is unambiguously the Atmos-freeze one -
                // authoring rule 7).
                {kAtmosFreezeId, 1.0},
                {kAtmosFreezeMixId, 0.70},
                {kAtmosLevelId, 0.40},         // 1000 lin [0, 2] -> 0.80
                {kBodyMaterialId, 3.0 / 4.0},  // 800 index 3 -> Chamber
                {kBodyMixId, 0.55},            // 805 lin [0, 1] -> 0.55
                {kMorphState0Id, 2.0 / 9.0},   // 409 index 2 -> Choir
                {kMorphState1Id, 5.0 / 9.0},   // 410 index 5 -> Hollow
                {kAetherMixId, 0.50},
                {kSeedId, 14.0 / 15.0},
            },
            /*.stimulus =*/std::nullopt},
        // ---------------------------------------------------------------- //
        SeraphisPresetDef{
            /*.name =*/"Aeolian Voices",
            /*.category =*/"Choirs",
            /*.description =*/"Wind-detuned voices, wide and never quite still.",
            /*.params =*/
            {
                // Rule 9 (FR-027b): body stage + morph pair are this preset's
                // distinctness axes (2026-08-30 palette-widening re-authoring).
                {kBodyMaterialId, 1.0 / 4.0},  // 800 index 1 -> Strings
                {kBodyMixId, 0.55},            // 805 lin [0, 1] -> 0.55
                // C-2 coverage holder for Vowel. The Vo->Br pair is distinct
                // from Slow Snow's Br->Vo by slot order - pair direction is
                // audible via morph position.
                {kMorphState0Id, 8.0 / 9.0},   // 409 index 8 -> Vowel
                {kMorphState1Id, 4.0 / 9.0},   // 410 index 4 -> Breath
                {kCloudDriftDepthId, 0.60},  // 205 lin [0, 50] -> 30 cents
                {kCloudDriftSmoothnessId, 0.80},
                {kLifeSpatialDepthId, 0.70},
                {kLifeVoiceWidthId, 0.90},   // 604 lin [50, 150] -> 140 %
                {kAetherMixId, 0.60},
                {kSeedId, 15.0 / 15.0},
            },
            /*.stimulus =*/std::nullopt},
        // ---------------------------------------------------------------- //
        SeraphisPresetDef{
            /*.name =*/"Whispered Mass",
            /*.category =*/"Choirs",
            /*.description =*/"Almost all grain: decorrelated, pitch-spread, barely pitched.",
            /*.params =*/
            {
                // Rule 9 (FR-027b): body stage + morph pair are this preset's
                // distinctness axes (2026-08-30 palette-widening re-authoring).
                {kBodyMaterialId, 1.0 / 4.0},  // 800 index 1 -> Strings
                {kBodyMixId, 0.70},            // 805 lin [0, 1] -> 0.70
                {kMorphState0Id, 5.0 / 9.0},   // 409 index 5 -> Hollow
                {kMorphState1Id, 2.0 / 9.0},   // 410 index 2 -> Choir
                {kAtmosLevelId, 0.50},  // 1000 lin [0, 2] -> 1.00
                {kAtmosPitchSpreadId, 0.50},
                {kAtmosDecorrelationId, 0.80},
                {kCloudRichnessId, 0.30},
                {kAetherMixId, 0.55},
                {kSeedId, 1.0 / 15.0},
            },
            /*.stimulus =*/std::nullopt},
        // ---------------------------------------------------------------- //
        SeraphisPresetDef{
            /*.name =*/"Angelic Drift",
            /*.category =*/"Choirs",
            /*.description =*/"Shimmer octave and fifth folded back into the tail.",
            /*.params =*/
            {
                // Rule 9 (FR-027b): body stage + morph pair are this preset's
                // distinctness axes (2026-08-30 palette-widening re-authoring).
                {kBodyMaterialId, 1.0 / 4.0},  // 800 index 1 -> Strings
                {kBodyMixId, 0.45},            // 805 lin [0, 1] -> 0.45
                {kMorphState0Id, 1.0},         // 409 index 9 -> Shimmer
                {kMorphState1Id, 2.0 / 9.0},   // 410 index 2 -> Choir
                {kAetherShimmerOctaveId, 0.60},
                {kAetherShimmerFifthId, 0.30},
                {kAetherMixId, 0.70},
                {kAetherDecayId, 0.62574150},  // 1203 log [0.5, 60] s -> 10 s
                {kCloudRichnessId, 0.50},
                {kSeedId, 3.0 / 15.0},
            },
            /*.stimulus =*/std::nullopt},

        // ============================================================== //
        // Motion                                                          //
        // ============================================================== //
        SeraphisPresetDef{
            /*.name =*/"Orbit Study",
            /*.category =*/"Motion",
            /*.description =*/"External travel driving a fast spatial orbit.",
            /*.params =*/
            {
                // Rule 9 (FR-027b): body stage + morph pair are this preset's
                // distinctness axes (2026-08-30 palette-widening re-authoring).
                {kBodyMaterialId, 1.0 / 4.0},  // 800 index 1 -> Strings
                {kBodyMixId, 0.35},            // 805 lin [0, 1] -> 0.35
                {kMorphState0Id, 0.0},         // 409 index 0 -> Sine Stack
                {kMorphState1Id, 6.0 / 9.0},   // 410 index 6 -> Metal
                // ID 403, 2 labels (dropdown_mappings.h:118): index 0 ->
                // External. C-2 coverage holder (it is also the registered
                // default; set explicitly so the row has a named owner).
                {kMorphTravelModeId, 0.0},
                {kMorphTravelRateId, 0.74840463},  // 404 log [1/600, 1] -> 0.2 /s
                {kMorphEntropyId, 0.40},
                {kLifeSpatialDepthId, 0.80},
                {kLifeSpatialRateId, 0.82281618},  // 601 log [0.01, 0.5] -> 0.25 Hz
                {kSeedId, 4.0 / 15.0},
            },
            /*.stimulus =*/std::nullopt},
        // ---------------------------------------------------------------- //
        SeraphisPresetDef{
            /*.name =*/"Tide Pool",
            /*.category =*/"Motion",
            /*.description =*/"Three states, crawling between them while the space breathes.",
            /*.params =*/
            {
                // Rule 9 (FR-027b): body stage + morph pair are this preset's
                // distinctness axes (2026-08-30 palette-widening re-authoring).
                {kBodyMaterialId, 1.0},  // 800 index 4 -> Ice
                {kBodyMixId, 0.55},      // 805 lin [0, 1] -> 0.55
                // ID 408 index 1 -> state count 3. C-2 coverage holder.
                {kMorphStateCountId, 0.50},
                {kMorphState0Id, 3.0 / 9.0},  // 409 index 3 -> Glass
                {kMorphState1Id, 1.0},        // 410 index 9 -> Shimmer
                {kMorphState2Id, 4.0 / 9.0},  // 411 index 4 -> Breath
                {kMorphPositionId, 0.50},          // 402 lin [0, 3] -> 1.5
                {kMorphTravelRateId, 0.53169217},  // 404 log [1/600, 1] -> 0.05 /s
                {kAetherTideDepthId, 0.60},
                {kAetherMixId, 0.50},
                {kSeedId, 5.0 / 15.0},
            },
            /*.stimulus =*/std::nullopt},
        // ---------------------------------------------------------------- //
        SeraphisPresetDef{
            /*.name =*/"Wander Lamp",
            /*.category =*/"Motion",
            /*.description =*/"Spline travel with four-second waypoints and a wandering image.",
            /*.params =*/
            {
                // Rule 9 (FR-027b): body stage + morph pair are this preset's
                // distinctness axes (2026-08-30 palette-widening re-authoring).
                {kBodyMaterialId, 3.0 / 4.0},  // 800 index 3 -> Chamber
                {kBodyMixId, 0.45},            // 805 lin [0, 1] -> 0.45
                {kMorphState0Id, 7.0 / 9.0},   // 409 index 7 -> Organ
                {kMorphState1Id, 8.0 / 9.0},   // 410 index 8 -> Vowel
                // ID 403 index 1 -> Spline. C-2 coverage holder.
                {kMorphTravelModeId, 1.0},
                {kMorphWaypointIntervalId, 0.50788142},  // 407 log [0.5, 30] -> 4 s
                {kMorphTravelRateId, 0.64004840},        // 404 log [1/600, 1] -> 0.1 /s
                {kFxWanderDepthId, 0.60},
                {kFxWanderRateId, 0.30},
                {kSeedId, 6.0 / 15.0},
            },
            /*.stimulus =*/std::nullopt},
        // ---------------------------------------------------------------- //
        SeraphisPresetDef{
            /*.name =*/"Restless",
            /*.category =*/"Motion",
            /*.description =*/"Tempo-synced spectral delay feeding a mutating cloud.",
            /*.params =*/
            {
                // ID 1418 ON. C-2 coverage holder for delay sync ON; OFF in every
                // other entry.
                {kFxDelaySyncId, 1.0},
                {kFxDelayMixId, 0.45},
                {kFxDelayFeedbackId, 0.63157895},  // 1414 -> value * 0.95 = 0.60
                {kFxDelayDiffusionId, 0.50},
                {kBodyMaterialId, 2.0 / 4.0},  // 800 index 2 -> Metal Plate
                {kBodyMixId, 0.55},            // 805 lin [0, 1] -> 0.55
                {kMorphState0Id, 5.0 / 9.0},   // 409 index 5 -> Hollow
                {kMorphState1Id, 1.0},         // 410 index 9 -> Shimmer
                {kCloudMutationId, 0.70},
                {kSeedId, 7.0 / 15.0},
            },
            /*.stimulus =*/std::nullopt},
        // ---------------------------------------------------------------- //
        SeraphisPresetDef{
            /*.name =*/"Spiral Arms",
            /*.category =*/"Motion",
            /*.description =*/"Coupled orbits sweeping the azimuth at the edge of the field.",
            /*.params =*/
            {
                // Rule 9 (FR-027b): body stage + morph pair are this preset's
                // distinctness axes (2026-08-30 palette-widening re-authoring).
                {kBodyMaterialId, 3.0 / 4.0},  // 800 index 3 -> Chamber
                {kBodyMixId, 0.25},            // 805 lin [0, 1] -> 0.25
                {kMorphState0Id, 0.0},         // 409 index 0 -> Sine Stack
                {kMorphState1Id, 7.0 / 9.0},   // 410 index 7 -> Organ
                {kLifeSpatialDepthId, 0.90},
                {kLifeSpatialRateId, 0.90882596},  // 601 log [0.01, 0.5] -> 0.35 Hz
                {kLifeSpatialCouplingId, 0.70},
                {kFxAzimuthDepthId, 0.60},
                {kCloudStereoSpreadId, 0.80},
                {kSeedId, 8.0 / 15.0},
            },
            /*.stimulus =*/std::nullopt,
            // 3.5 [partials] golden-angle spiral, i = 0..15 (authored
            // literals; index 0 carries pan 0.0 with its override bit SET).
            /*.partialPans =*/
            {{0, 0.0f},   {1, 0.57f},   {2, -0.85f},  {3, 0.68f},
             {4, -0.15f}, {5, -0.46f},  {6, 0.80f},   {7, -0.76f},
             {8, 0.29f},  {9, 0.33f},   {10, -0.77f}, {11, 0.81f},
             {12, -0.43f}, {13, -0.18f}, {14, 0.69f}, {15, -0.84f}},
            /*.partialMaskBits =*/0},
        // ---------------------------------------------------------------- //
        SeraphisPresetDef{
            /*.name =*/"Slow Weather",
            /*.category =*/"Motion",
            /*.description =*/"All four states in play, crossed at a geological rate.",
            /*.params =*/
            {
                // Rule 9 (FR-027b): body stage + morph pair are this preset's
                // distinctness axes (2026-08-30 palette-widening re-authoring).
                {kBodyMaterialId, 0.0},  // 800 index 0 -> Glass
                {kBodyMixId, 0.55},      // 805 lin [0, 1] -> 0.55
                // ID 408 index 2 -> state count 4. C-2 coverage holder.
                {kMorphStateCountId, 1.0},
                {kMorphState0Id, 2.0 / 9.0},       // 409 index 2 -> Choir
                {kMorphState1Id, 6.0 / 9.0},       // 410 index 6 -> Metal
                {kMorphState2Id, 4.0 / 9.0},       // 411 index 4 -> Breath
                {kMorphState3Id, 1.0 / 9.0},       // 412 index 1 -> Bell
                {kMorphTravelRateId, 0.38845302},  // 404 log [1/600, 1] -> 0.02 /s
                {kCloudDriftDepthId, 0.30},        // 205 lin [0, 50] -> 15 cents
                {kSeedId, 9.0 / 15.0},
            },
            /*.stimulus =*/std::nullopt},

        // ============================================================== //
        // Cinematic                                                       //
        // ============================================================== //
        SeraphisPresetDef{
            /*.name =*/"Approach Vector",
            /*.category =*/"Cinematic",
            /*.description =*/"Growth envelope closing in over nine seconds.",
            /*.params =*/
            {
                // Rule 9 (FR-027b): body stage + morph pair are this preset's
                // distinctness axes (2026-08-30 palette-widening re-authoring).
                {kBodyMaterialId, 2.0 / 4.0},  // 800 index 2 -> Metal Plate
                {kBodyMixId, 0.85},            // 805 lin [0, 1] -> 0.85
                {kMorphState0Id, 6.0 / 9.0},   // 409 index 6 -> Metal
                {kMorphState1Id, 1.0},         // 410 index 9 -> Shimmer
                // Growth mode: A = 9 + 2.5 = 11.5 s <= 12 (FR-008a). Both 701 and
                // 703 are set for exactly that reason - the Growth DEFAULTS are
                // 10 + 4 = 14 s and would breach the ceiling
                // (life_mod_params.h:65-67).
                {kEnvModeId, 1.0},
                {kEnvGrowthDurationId, 0.53664867},  // 701 log [1, 60] s -> 9 s
                {kEnvStage1MsId, 0.84948500},        // 703 log [1, 1e4] ms -> 2500
                {kAetherMixId, 0.60},
                {kAetherDecayId, 0.62574150},        // 1203 log [0.5, 60] s -> 10 s
                {kCloudRichnessId, 0.70},
                {kSeedId, 10.0 / 15.0},
            },
            /*.stimulus =*/std::nullopt},
        // ---------------------------------------------------------------- //
        SeraphisPresetDef{
            /*.name =*/"Event Horizon",
            /*.category =*/"Cinematic",
            /*.description =*/"Aether frozen at maximum size - the note never lands.",
            /*.params =*/
            {
                // Rule 9 (FR-027b): body stage + morph pair are this preset's
                // distinctness axes (2026-08-30 palette-widening re-authoring).
                {kBodyMaterialId, 0.0},       // 800 index 0 -> Glass
                {kBodyMixId, 0.70},           // 805 lin [0, 1] -> 0.70
                {kMorphState0Id, 6.0 / 9.0},  // 409 index 6 -> Metal
                {kMorphState1Id, 8.0 / 9.0},  // 410 index 8 -> Vowel
                // ID 1204 ON. C-2 coverage holder for the Aether freeze (with
                // Vast); OFF in every other entry. A frozen Aether is the 60 s
                // tail band, so the envelope is kept short on purpose:
                // A = 1 + 2 = 3 s, Rel = 4 s (both well inside FR-008a).
                {kAetherFreezeId, 1.0},
                {kAetherMixId, 0.80},
                {kAetherSizeId, 0.95},
                {kEnvStage0MsId, 0.75000000},   // 702 log [1, 1e4] ms -> 1000
                {kEnvStage1MsId, 0.82525750},   // 703 log [1, 1e4] ms -> 2000
                {kEnvReleaseMsId, 0.90051500},  // 704 log [1, 1e4] ms -> 4000
                {kSeedId, 11.0 / 15.0},
            },
            /*.stimulus =*/std::nullopt,
            // 3.5 [partials]: bit i set = partial i MASKED. Masks partials
            // 32-63 - the dark truncation.
            /*.partialPans =*/{},
            /*.partialMaskBits =*/0xFFFFFFFF00000000ull},
        // ---------------------------------------------------------------- //
        SeraphisPresetDef{
            /*.name =*/"Signal Lost",
            /*.category =*/"Cinematic",
            /*.description =*/"Spectral freeze over a saturated, inharmonic bed.",
            /*.params =*/
            {
                // Rule 9 (FR-027b): body stage + morph pair are this preset's
                // distinctness axes (2026-08-30 palette-widening re-authoring).
                {kBodyMaterialId, 0.0},       // 800 index 0 -> Glass
                {kBodyMixId, 0.70},           // 805 lin [0, 1] -> 0.70
                {kMorphState0Id, 1.0},        // 409 index 9 -> Shimmer
                {kMorphState1Id, 5.0 / 9.0},  // 410 index 5 -> Hollow
                // ID 1430 ON. C-2 coverage holder for the FX spectral freeze; OFF
                // in every other entry.
                {kFxSpectralFreezeId, 1.0},
                {kFxSaturationId, 0.60},
                {kFxDelayMixId, 0.40},
                {kCloudInharmonicityId, 0.70},  // 201 lin [0, 0.1] -> 0.07
                {kCloudRichnessId, 0.45},
                {kSeedId, 12.0 / 15.0},
            },
            /*.stimulus =*/std::nullopt},
        // ---------------------------------------------------------------- //
        SeraphisPresetDef{
            /*.name =*/"Rising Dread",
            /*.category =*/"Cinematic",
            /*.description =*/"Ten-second growth under a heavy downward tilt.",
            /*.params =*/
            {
                // Rule 9 (FR-027b): body stage + morph pair are this preset's
                // distinctness axes (2026-08-30 palette-widening re-authoring).
                {kBodyMaterialId, 3.0 / 4.0},  // 800 index 3 -> Chamber
                {kBodyMixId, 0.35},            // 805 lin [0, 1] -> 0.35
                {kMorphState0Id, 5.0 / 9.0},   // 409 index 5 -> Hollow
                {kMorphState1Id, 1.0 / 9.0},   // 410 index 1 -> Bell
                // Growth mode: A = 10 + 1.5 = 11.5 s <= 12 (FR-008a).
                {kEnvModeId, 1.0},
                {kEnvGrowthDurationId, 0.56238186},  // 701 log [1, 60] s -> 10 s
                {kEnvStage1MsId, 0.79402281},        // 703 log [1, 1e4] ms -> 1500
                {kCloudTiltId, 0.16666667},          // 202 lin [-12,+12] -> -8 dB/oct
                {kAetherDecayId, 0.74851700},        // 1203 log [0.5, 60] s -> 18 s
                {kMacroGravityId, 0.80},
                {kSeedId, 13.0 / 15.0},
            },
            /*.stimulus =*/std::nullopt},
        // ---------------------------------------------------------------- //
        SeraphisPresetDef{
            /*.name =*/"Vast",
            /*.category =*/"Cinematic",
            /*.description =*/"Frozen Aether at full width - scale without motion.",
            /*.params =*/
            {
                // Rule 9 (FR-027b): body stage + morph pair are this preset's
                // distinctness axes (2026-08-30 palette-widening re-authoring).
                {kBodyMaterialId, 3.0 / 4.0},  // 800 index 3 -> Chamber
                {kBodyMixId, 0.55},            // 805 lin [0, 1] -> 0.55
                {kMorphState0Id, 7.0 / 9.0},   // 409 index 7 -> Organ
                {kMorphState1Id, 4.0 / 9.0},   // 410 index 4 -> Breath
                // ID 1204 ON (with Event Horizon). Short envelope for the same
                // reason: A = 1.5 + 2.5 = 4 s, Rel = 5 s.
                {kAetherFreezeId, 1.0},
                {kAetherMixId, 0.90},
                {kAetherDimensionalityId, 0.80},
                {kEnvStage0MsId, 0.79402281},   // 702 log [1, 1e4] ms -> 1500
                {kEnvStage1MsId, 0.84948500},   // 703 log [1, 1e4] ms -> 2500
                {kEnvReleaseMsId, 0.92474250},  // 704 log [1, 1e4] ms -> 5000
                {kCloudRichnessId, 0.80},
                {kSeedId, 14.0 / 15.0},
            },
            /*.stimulus =*/std::nullopt},
        // ---------------------------------------------------------------- //
        SeraphisPresetDef{
            /*.name =*/"Aftermath",
            /*.category =*/"Cinematic",
            /*.description =*/"Exponential grains scattered late across a wide buffer.",
            /*.params =*/
            {
                // Rule 9 (FR-027b): body stage + morph pair are this preset's
                // distinctness axes (2026-08-30 palette-widening re-authoring).
                {kBodyMaterialId, 1.0 / 4.0},  // 800 index 1 -> Strings
                {kBodyMixId, 0.70},            // 805 lin [0, 1] -> 0.70
                {kMorphState0Id, 8.0 / 9.0},   // 409 index 8 -> Vowel
                {kMorphState1Id, 3.0 / 9.0},   // 410 index 3 -> Glass
                // ID 1016 index 5 -> Exponential. C-2 coverage holder.
                {kAtmosGrainEnvelopeId, 5.0 / 5.0},
                {kAtmosLevelId, 0.45},               // 1000 lin [0, 2] -> 0.90
                {kAtmosDensityId, 0.33817519},       // 1002 log [0.1, 20] -> 0.6 /s
                {kAtmosGrainSecondsId, 0.85676086},  // 1003 log [0.05, 30] -> 12 s
                {kAtmosPositionId, 0.26666667},      // 1012 lin [0, 30] s -> 8 s
                {kAetherMixId, 0.65},
                {kSeedId, 15.0 / 15.0},
            },
            /*.stimulus =*/std::nullopt},
    };
    return kPresets;
}

/// Look a definition up by its category and file stem.
///
/// MISS POLICY - BINDING ON EVERY CALLER: returns `nullptr`, and a caller MUST
/// treat null as a FAILURE. It is NEVER "use the default stimulus". A silently
/// unmatched preset would render at the default pitch 60 / velocity 0.8f and
/// pass every arm while its authored outlier stimulus went untested.
[[nodiscard]] inline const SeraphisPresetDef* findDef(std::string_view category,
                                                      std::string_view stem) {
    for (const auto& def : allPresets()) {
        if (def.category == category && def.name == stem) {
            return &def;
        }
    }
    return nullptr;
}

// ==============================================================================
// `Info` chunk (FR-003 / FR-021)
// ==============================================================================
// Membrum's exact six-attribute form (tools/membrum_preset_generator.cpp:349-361)
// with Seraphis values. It lives HERE rather than file-local in the generator so
// that FR-029 clause 2's byte comparison has ONE source (OI-1).
//
// It is neither state layout nor component-stream serialization, so FR-016a's
// "data only" rule is satisfied.
//
// Line endings are written as explicit `\n`, and the payload is pure ASCII, so
// the bytes are identical on all three CI legs - which is what makes FR-029's
// `Info` comparison a legitimate byte-level check rather than a cross-toolchain
// float golden.
[[nodiscard]] inline std::string buildSeraphisInfoXml(std::string_view presetName,
                                                      std::string_view subcategory) {
    std::string xml;
    xml += "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    xml += "<MetaInfo>\n";
    xml += "  <Attr id=\"MediaType\" value=\"VstPreset\" type=\"string\"/>\n";
    xml += "  <Attr id=\"PlugInName\" value=\"Seraphis\" type=\"string\"/>\n";
    xml += "  <Attr id=\"PlugInCategory\" value=\"Synth\" type=\"string\"/>\n";
    xml += "  <Attr id=\"Name\" value=\"";
    xml += presetName;
    xml += "\" type=\"string\"/>\n";
    xml += "  <Attr id=\"MusicalCategory\" value=\"";
    xml += subcategory;
    xml += "\" type=\"string\"/>\n";
    xml += "  <Attr id=\"MusicalInstrument\" value=\"";
    xml += subcategory;
    xml += "\" type=\"string\"/>\n";
    xml += "</MetaInfo>\n";
    return xml;
}

}  // namespace Seraphis::PresetDefs
