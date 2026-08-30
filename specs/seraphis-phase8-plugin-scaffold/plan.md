# Implementation Plan: Seraphis Phase 8 — Plugin Scaffold

**Spec:** `specs/seraphis-phase8-plugin-scaffold/spec.md`
**Roadmap:** `specs/Seraphis-roadmap.md` → Part B, Phase 8 (lines 323–435)
**Depends on:** Phase 6 (`AetherReverb`) ✅, Phase 7 (`SeraphisEngine` / `SeraphisVoice` / `SeraphisMacroMatrix`) ✅
**Status:** PLAN — no implementation
**Date:** 2026-07-31

---

## 0. How to read this plan

Every API signature quoted below was read **from the header this session**, on
`feat/seraphis-phase1-life-modulators`. Line numbers are `file:line` as the files stand today. Where
this plan **contradicts** the spec, the contradiction is called out explicitly in §1.3 ("Spec premise
corrections") with the header line that disproves the spec's premise — no threshold is relaxed, and
every affected Success Criterion is restated in a form that is both *implementable* and *at least as
strict*.

Phase 8 writes **no DSP**. It writes a VST3 wrapper around an already-finished chain. The hard parts
are therefore not algorithmic: they are (a) reproducing `tests/test_helpers/seraphis_chain.h`'s slice
body exactly, (b) making three global parameters demonstrably reach that chain, and (c) enrolling the
plugin in every roster outside `plugins/` so CI can actually fail.

---

## 1. Architecture

### 1.1 Object graph

```
Seraphis::Processor  (AudioEffect, audio thread)
 ├─ std::unique_ptr<Krate::DSP::SeraphisEngine>   engine_    (heap; 771 968 B — seraphis_engine.h:159-164)
 ├─ std::unique_ptr<Krate::DSP::AetherReverb>     reverb_    (heap; allocates at prepare)
 ├─ Krate::DSP::SeraphisMacroMatrix               macros_    (by value; sizeof == sizeof(SeraphisMacroValues)
 │                                                            == 5 floats — seraphis_macro_matrix.h:733)
 ├─ Seraphis::GlobalParams                        globalParams_   (3 atomics)
 ├─ Seraphis::MacroParams                         macroParams_    (5 atomics, INERT)
 ├─ Krate::DSP::OnePoleSmoother                   masterGain_     (smoother.h:134)
 └─ scratch: 4 × std::vector<float>(2048) + std::array<float, 32>

Seraphis::Controller (EditControllerEx1 + VST3EditorDelegate, UI thread)
 └─ std::unique_ptr<Krate::Plugins::PresetManager> presetManager_  (preset_manager.h:55-61)
```

No cross-include between `processor/` and `controller/`. The only shared header is
`src/plugin_ids.h` (FUIDs, `kCurrentStateVersion`, `ParameterIDs`) plus the two `parameters/*.h`
packs, which are header-only and included by both sides (the Ruinae contract).

### 1.2 Signal path inside `process()` (per slice)

```
                          ┌── GlobalParams (once per process() call, on-change) ──┐
                          │   setPolyphony / setOutputSaturation                   │
                          ▼                                                        │
 macros_.apply(*engine_) ──► engine_->processStereoBlock(dryL,dryR,n)              │
 macros_.computeAetherTargets() ──► applyAetherTargets(*reverb_, targets)          │
                          ▼                                                        │
        reverb_->processStereoBlock(dryL,dryR,wetL,wetR,n)                         │
                          ▼                                                        │
        wetL[i] *= masterGain_.process(); wetR[i] *= same   ◄──── per sample ──────┘
                          ▼
        engine_->processOutputStage(wetL,wetR,n)      (saturator → TruePeakLimiter, ALWAYS last)
                          ▼
        copy into data.outputs[0].channelBuffers32[0..1] at sliceStart
                          ▼
        engine_->consumeBloomEvents() → reverb_->bloomNoteOff(v) …then… bloomNoteOn(v, buf, count)
```

This is `tests/test_helpers/seraphis_chain.h:212–254` verbatim, with step 4b (master gain) inserted
between the reverb return and the output stage. `seraphis_chain.h:14–16` says in its own banner that
it exists "*so Phase 8's processor has a literal model to reproduce*".

### 1.3 Spec premise corrections (three; all factual, none relaxes a threshold)

**C-1 — `AetherReverb::getLatencySamples()` is 1024 at *all* times, not 0-then-1024.**
FR-033 states "*the reported value is 0 before the first prepare and 1024 after it*" and SC-013
clause 4 asserts "*the first prepare (0 → 1024) records exactly one `kLatencyChanged`*". The header
disproves the premise:

```cpp
// dsp/include/krate/dsp/effects/aether_reverb.h:2612
[[nodiscard]] std::size_t getLatencySamples() const noexcept {
    return spectralEnabled_ ? diffusionFftSize_ : std::size_t{0};
}
// :4465  std::size_t diffusionFftSize_ = 1024;
// :4467  bool        spectralEnabled_  = true;
```

A default-constructed, **unprepared** `AetherReverb` already reports **1024**, and its own banner
(`:2607–2612`) records that the value is *"Constant for a prepared configuration — no setter changes
it."* `makeSeraphisReverbConfig` (§2.2) pins `spectralDiffusionEnabled = true` and
`diffusionFftSize = 1024`, so for the **shipped** configuration the reported latency is **1024
before the first prepare, after every prepare, at 44.1 / 48 / 96 / 192 kHz, and after every one of
the eight shipped parameters**. In Phase 8 there is no latency *change*, ever.

**C-2 — there is no route from an `AudioEffect` to `IComponentHandler`, so the announcement in
FR-023 clause 4 / FR-033 is deleted, not faked.**
`Steinberg::Vst::AudioEffect` → `Component` → `ComponentBase` exposes **no** `IComponentHandler`;
grep over `extern/vst3sdk/public.sdk/source/vst/vstcomponentbase.h`, `vstcomponent.h`,
`vstaudioeffect.h` for `componentHandler` returns **one comment line only**
(`vstcomponent.h:51`). The handler is delivered exclusively to the **edit controller**, via
`IEditController::setComponentHandler` (`vsteditcontroller.h:59`), stored at `:108` and read through
`getComponentHandler()` at `:97`.

The tempting substitute — `FUnknownPtr<IComponentHandler>(ComponentBase::getHostContext())`
(`vstcomponentbase.h:43`) — **must not be used**. The `FUnknown*` a host passes to
`IComponent::initialize` is an `IHostApplication`; the SDK's own reference implementation
`HostApplication` (`public.sdk/source/vst/hosting/hostclasses.h:32-48`) implements `IHostApplication`
only, nothing in VST3 obliges a host to expose `IComponentHandler` there, and
`grep -rn restartComponent plugins/` finds **no** processor in this repo announcing latency that way.
The query would return null in every real host **and** in the SDK hosting layer the test target
links (`hostclasses.cpp`, §5.2), so the mechanism would be exercised only by a bespoke test stub —
a configuration no host produces. A mechanism whose only working instance is its own test stub is
dead code, and a criterion that only that stub can satisfy is a vacuous criterion.

**Resolution.** `Processor` implements `getLatencySamples()` (FR-033's reporting half) and **nothing
else**: no `announceLatencyIfChanged()`, no `lastReportedLatency_`, no host-context query. Hosts read
`getLatencySamples()` after `setupProcessing()`, which is exactly how a constant latency is
communicated. **SC-013 clause 4 is restated in §4.3 as an invariance assertion** — `1024` before any
`setupProcessing()`, after each of four `setupProcessing()` calls at 44.1 / 48 / 96 / 192 kHz in
sequence, after `setActive` toggles, after `setState`, and after every one of the eight parameters —
which is *strictly more* observable behaviour than "one flag was counted in a fabricated host", and
is measurable without fabricating one.

**This is not a relaxation:** the announcement can produce exactly one observable effect — a host
re-reading a value that never changes. Phase 8 ships no code path that can change the reported
latency, and C-1 proves it. **If a later phase makes the latency variable** (e.g. a spectral-diffusion
on/off parameter, or a `diffusionFftSize` parameter), the announcement MUST be added *then*, on the
SDK-sanctioned route: processor → `IConnectionPoint`/`IMessage` → controller →
`getComponentHandler()->restartComponent(kLatencyChanged)` (`vsteditcontroller.h:97`). Recorded as a
spec amendment in §8 so a later reader does not "restore" the host-context query.

**C-3 — the FR-031 velocity mapping needs rounding and a floor of 1.**
FR-031 specifies `noteOn(pitch, velocity*127)`. A bare truncating cast is wrong: a legal VST3
velocity of e.g. `0.003` truncates to `0`, and `SeraphisEngine::noteOn` maps velocity `0` to
`noteOff` (`seraphis_engine.h:374-377`), so a note-on would *release* instead of allocating —
contradicting SC-022 clause 1. **Resolution:** the shipped mapping is
`uint8(clamp(velocity * 127.0f + 0.5f, 1.0f, 127.0f))` (§3.2), i.e. round-to-nearest with a floor of
1 for any strictly positive velocity. SC-022 gains a sub-clause asserting that a `kNoteOnEvent` with
velocity in `(0, 1/127)` **allocates** a voice rather than releasing one (§4.3), so the floor is
detectable rather than assumed. FR-031's text should be amended to match (§8).

None of the three corrections changes a threshold, a default, or a shipped value.

---

## 2. Component-by-component design

Layer column: every file below is **plugin layer** (not a KrateDSP layer). No file under `dsp/` is
modified (spec Scope, "No new DSP is written in this phase").

### 2.1 `plugins/seraphis/src/plugin_ids.h`

Model: `plugins/ruinae/src/plugin_ids.h:16–28`. Contents, in order:

```cpp
#pragma once
#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/vst/vsttypes.h"

namespace Seraphis {

/// FR-012. Shared by processor and controller; neither includes the other.
constexpr Steinberg::int32 kCurrentStateVersion = 1;

/// FR-011. Freshly generated, never reused, never changed post-release.
static const Steinberg::FUID kProcessorUID (0x........, 0x........, 0x........, 0x........);
static const Steinberg::FUID kControllerUID(0x........, 0x........, 0x........, 0x........);

/// FR-014. DEF_CLASS2 subcategory string; instrument, matching entry.cpp:57's slot.
static const char* kSubCategories = "Instrument|Synth";

/// FR-013. Reserved ranges (roadmap 383-386):
///   0-99      Global            (Phase 8 - SHIPPED)
///   100-199   Macros            (Phase 8 - SHIPPED, inert)
///   200-399   Harmonic Cloud    (Phase 9)
///   400-599   Spectral Morph / Entropy (Phase 9)
///   600-799   Life Modulators   (Phase 9)
///   800-999   Continuous Body   (Phase 9)
///   1000-1199 Atmosphere        (Phase 9)
///   1200-1399 Aether            (Phase 9)
///   1400+     Effects           (Phase 10)
enum ParameterIDs : Steinberg::Vst::ParamID {
    kMasterGainId   = 0,
    kPolyphonyId    = 1,
    kSoftLimitId    = 2,

    kMacroDreamId    = 100,
    kMacroBloomId    = 101,
    kMacroDissolveId = 102,
    kMacroGravityId  = 103,
    kMacroEntropyId  = 104,
};

/// FR-048. REGISTERED TYPES ARE FROZEN FOR THE LIFE OF THE PLUGIN. What §2.3/§2.4
/// actually construct (verified against extern/vst3sdk/public.sdk/source/vst/
/// vstparameters.cpp:416-440 -> :369-378, which `new Parameter(info)`):
///   kMasterGainId, kSoftLimitId, kMacro*Id  -> plain Steinberg::Vst::Parameter  (7)
///   kPolyphonyId                            -> Steinberg::Vst::StringListParameter (1)
/// `StringListParameter` is what createDropdownParameterWithDefault returns
/// (plugins/shared/src/ui/parameter_helpers.h:47). NEVER swap a type at an ID:
/// DAWs cache parameter metadata and the editor fails to load.

/// Range-dispatch bounds used by processParameterChanges (FR-042).
constexpr Steinberg::Vst::ParamID kGlobalParamRangeEnd = 100;   // IDs <  100 -> global pack
constexpr Steinberg::Vst::ParamID kMacroParamRangeEnd  = 200;   // IDs < 200 -> macro pack

} // namespace Seraphis
```

**FUID generation procedure (FR-011).** Generate two v4 GUIDs
(`node -e "console.log(require('crypto').randomUUID())"`), format each as four `0x`-prefixed 32-bit
groups, then verify non-collision against the twelve registered FUIDs at
`disrumpo/src/plugin_ids.h:26,:31`, `gradus:20,:23`, `innexus:20,:24`, `iterum:21,:25`,
`membrum:18,:21`, `ruinae:24,:28` with
`grep -rn "FUID k\(Processor\|Controller\)UID" plugins/*/src/plugin_ids.h`. Record the two values in
`plugins/seraphis/CLAUDE.md` as immutable.

`kSubCategories` is a **`static const char*`, not `constexpr`** — the cross-platform edge case in the
spec ("anything initialized from an SDK constant must be `const`, not `constexpr`") and the model at
`ruinae/src/entry.cpp:57` both point that way.

**RT safety:** header-only constants, no runtime cost.

### 2.2 `plugins/seraphis/src/engine/seraphis_engine_config.h` (FR-053, FR-034a)

Thin. **Introduces no type** — free functions returning the DSP-owned structs, which is what makes
the ODR sweep in the spec's New-components table return "0 hits, CLEAR".

```cpp
#pragma once
#include <krate/dsp/effects/aether_reverb.h>
#include <krate/dsp/systems/seraphis_engine.h>
#include <krate/dsp/systems/seraphis_macro_matrix.h>
#include <cstddef>
#include <cstdint>

namespace Seraphis {

/// FR-053. The one seed the whole plugin uses. NOT a parameter in Phase 8;
/// two instances in one host therefore share a trajectory (spec, "Seed
/// determinism"). Phase 9 owns any per-instance seed.
inline constexpr std::uint32_t kEngineSeed = 1u;
inline constexpr std::uint32_t kReverbSeed = 1u;

/// FR-024a clause 3. Same 20 ms family as SeraphisEngine::kSumGainSmoothMs
/// (dsp/include/krate/dsp/systems/seraphis_engine.h:138).
inline constexpr float kMasterGainSmoothMs = 20.0f;

/// FR-023 clause 1 / FR-026 / FR-028: ONE constant governs the engine config,
/// the reverb config, the slice bound and the scratch size.
inline constexpr std::size_t kMaxBlockSamples = Krate::DSP::SeraphisEngine::kMaxBlockSamples; // 2048

[[nodiscard]] inline Krate::DSP::SeraphisEngineConfig
makeSeraphisEngineConfig(std::size_t polyphony, std::uint32_t seed,
                         std::size_t maxBlockSamples) noexcept {
    Krate::DSP::SeraphisEngineConfig cfg{};            // seraphis_engine.h:92-97
    cfg.voice.captureSeconds  = 4.0f;                  // seraphis_voice.h:108  (shipped default)
    cfg.voice.blurEnabled     = true;                  // :110
    cfg.voice.freezeEnabled   = true;                  // :112
    cfg.voice.blurFftSize     = 1024;                  // :114
    cfg.voice.freezeFftSize   = 2048;                  // :116
    cfg.voice.maxBlockSamples = maxBlockSamples;       // :119
    cfg.polyphony             = polyphony;             // :95  (SEEDED FROM THE PARAMETER, FR-023 cl.2)
    cfg.seed                  = seed;                  // :96  (explicit, never the struct default)
    return cfg;
}

[[nodiscard]] inline Krate::DSP::AetherReverb::PrepareConfig
makeSeraphisReverbConfig(std::size_t maxBlockSamples) noexcept {
    Krate::DSP::AetherReverb::PrepareConfig cfg{};     // aether_reverb.h:1577-1587
    cfg.numChannels              = 8;                  // :1578
    cfg.maxBlockSamples          = maxBlockSamples;    // :1579  (own clamp [64,8192] at :1619)
    cfg.maxDelaySeconds          = 0.50f;              // :1580
    cfg.shimmerEnabled           = true;               // :1581
    cfg.shimmerMode              = Krate::DSP::AetherReverb::PitchMode::Granular; // :1582
    cfg.bloomEnabled             = true;               // :1583
    cfg.spectralDiffusionEnabled = true;               // :1584  MUST stay true (FR-033/FR-053)
    cfg.diffusionFftSize         = 1024;               // :1585  MUST stay 1024 -> 1024-sample latency
    cfg.seed                     = kReverbSeed;        // :1586  explicit, never the struct default
    return cfg;
}

/// FR-034a. Factored out of process() SO IT IS TESTABLE: the eight targets have
/// no getter on AetherReverb, and at Phase 8's neutral macro defaults they are
/// numerically identical to the reverb's own ctor defaults, so a render diff
/// against "step 2 omitted" is provably vacuous.
inline void applyAetherTargets(Krate::DSP::AetherReverb& reverb,
                               const Krate::DSP::SeraphisAetherTargets& t) noexcept {
    reverb.setMix(t.mix);                                          // aether_reverb.h:2336
    reverb.setSize(t.size);                                        // :2208
    reverb.setWidth(t.width);                                      // :2333
    reverb.setShimmerOctaveSend(t.shimmerOctaveSend);              // :2280
    reverb.setShimmerFifthSend(t.shimmerFifthSend);                // :2285
    reverb.setBloomSend(t.bloomSend);                              // :2295
    reverb.setSizeBreathDepth(t.sizeBreathDepth);                  // :2320
    reverb.setDimensionalityTideDepth(t.dimensionalityTideDepth);  // :2328
}

} // namespace Seraphis
```

The eight setter line numbers match `seraphis_chain.h:215–222`'s own citations one-for-one; the
function is a copy of that block with the reverb passed by reference.

**RT safety:** `applyAetherTargets` is `noexcept`, allocation-free — every setter it calls funnels
through `AetherReverb::applyControl` (`aether_reverb.h:2951–2958`), a clamp plus a smoother store.
`make*Config` are prepare-time only.

### 2.3 `plugins/seraphis/src/parameters/global_params.h` (FR-040, FR-043)

Six functions, the Ruinae contract (`plugins/ruinae/src/parameters/global_params.h:39, 87, 130, 178,
187, 220`). Includes the **shared** helper, not a plugin-local copy:

```cpp
#include "ui/parameter_helpers.h"   // plugins/shared/src/ui/parameter_helpers.h (FR-048)
```

```cpp
namespace Seraphis {

struct GlobalParams {
    std::atomic<float> masterGain{1.0f};   // linear gain, [0, 2]; normalized 0.5 == unity
    std::atomic<int>   polyphony{8};       // [1, 16]; SeraphisEngineConfig::polyphony default
    std::atomic<bool>  softLimit{true};    // on -> SeraphisEngine::kOutputSaturation
};

inline void handleGlobalParamChange(GlobalParams&, Steinberg::Vst::ParamID,
                                    Steinberg::Vst::ParamValue);
inline void registerGlobalParams(Steinberg::Vst::ParameterContainer&);
inline Steinberg::tresult formatGlobalParam(Steinberg::Vst::ParamID,
                                            Steinberg::Vst::ParamValue,
                                            Steinberg::Vst::String128);
inline void saveGlobalParams(const GlobalParams&, Steinberg::IBStreamer&);
inline bool loadGlobalParams(GlobalParams&, Steinberg::IBStreamer&);
template <typename SetParamFunc>
inline void loadGlobalParamsToController(Steinberg::IBStreamer&, SetParamFunc);

} // namespace Seraphis
```

**Denormalisation (FR-043), copied from the Ruinae model line-for-line:**

| ID | Expression | Model |
|---|---|---|
| `kMasterGainId` | `std::clamp(static_cast<float>(value * 2.0), 0.0f, 2.0f)` | `ruinae/…/global_params.h:47-49` |
| `kPolyphonyId` | `std::clamp(static_cast<int>(value * 15.0 + 1.0 + 0.5), 1, 16)` | `:59-61` |
| `kSoftLimitId` | `value >= 0.5` | `:64` |

The polyphony range `[1, 16]` is exactly what `SeraphisEngine::setPolyphony` clamps to
(`seraphis_engine.h:322`, `std::clamp(n, 1, kMaxVoices)`), so no value the host can send is silently
altered by the engine.

**Registration (FR-048):**

```cpp
parameters.addParameter(STR16("Master Gain"), STR16("dB"), 0, 0.5,
                        ParameterInfo::kCanAutomate, kMasterGainId);
parameters.addParameter(Krate::Plugins::createDropdownParameterWithDefault(
    STR16("Polyphony"), kPolyphonyId, /*defaultIndex=*/7,
    {STR16("1"),  …, STR16("16")}));                        // parameter_helpers.h:47
parameters.addParameter(STR16("Soft Limit"), STR16(""), 1, 1.0,
                        ParameterInfo::kCanAutomate, kSoftLimitId);
```

`stepCount = 1` + default `1.0` is the toggle shape at `ruinae/…/global_params.h:111-113`.
`defaultIndex = 7` → 8 voices, matching `SeraphisEngineConfig::polyphony = 8`
(`seraphis_engine.h:95`).

**Registered types, stated because FR-048 freezes them.** Master Gain and Soft Limit go through
`ParameterContainer::addParameter(title, units, stepCount, default, flags, id)`
(`extern/vst3sdk/public.sdk/source/vst/vstparameters.cpp:416-440`), which builds a `ParameterInfo`
and forwards to `addParameter(info)` → `new Parameter(info)` (`:369-378`): these are **plain
`Steinberg::Vst::Parameter`s, not `RangeParameter`s**. Only `kPolyphonyId` is a
`StringListParameter`, because `createDropdownParameterWithDefault` returns one
(`parameter_helpers.h:47`). The full eight-ID freeze is recorded in `plugin_ids.h` (§2.1).

**Soft-limit documentation string (FR-044, binding).** The parameter's title/short-title text and
the header comment MUST state that it controls the **tape-saturation amount only** and does **not**
bypass the true-peak limiter: `processOutputStage` ends with `limiter_.processBlock(l, r, n)` and
has **no bypass path** (`seraphis_engine.h:521`).

**Stream layout** (`saveGlobalParams`, feeding §3.6):
`writeFloat(masterGain)`, `writeInt32(polyphony)`, `writeInt32(softLimit ? 1 : 0)` — 12 bytes.
`loadGlobalParams` uses the EOF-safe read pattern (`ruinae/…/global_params.h:191-213`): a failed
read leaves the atomic at its default and returns `false` rather than storing garbage.

**The load path MUST clamp — a deliberate divergence from the Ruinae model.** Ruinae's loader stores
the raw stream value (`plugins/ruinae/src/parameters/global_params.h:197-198`:
`streamer.readInt32(intVal); params.polyphony.store(intVal, …)`); only its *parameter* handler
clamps (`:59-61`). Seraphis cannot copy that: `pushGlobalParams()` (§2.5.7) compares the stored value
against `engine_->getPolyphony()`, which is **clamped** (`seraphis_engine.h:322`, `:665`), so an
unclamped stored value from a corrupt or hand-written stream (0, 20, or a negative int32 that
`static_cast<std::size_t>` turns into a huge number) would make the change detector fire on **every
block, forever** — re-arming `sumGain_` (`:349`) and walking `allocator_.setVoiceCount`'s excess-slot
loop (`:339-348`) per block. One named helper is therefore the single point where the value enters
the `size_t` domain, and every reader uses it:

```cpp
/// The ONE conversion into the engine's domain. Keeps the stored value and
/// engine_->getPolyphony() in the SAME clamped domain (seraphis_engine.h:322).
[[nodiscard]] inline std::size_t clampPolyphony(int raw) noexcept {
    return std::clamp(static_cast<std::size_t>(std::max(raw, 1)),
                      std::size_t{1}, Krate::DSP::SeraphisEngine::kMaxVoices);  // :130 == 16
}
```

`loadGlobalParams` stores `clampPolyphony(intVal)`; `setupProcessing()` (§2.5.4) and
`pushGlobalParams()` (§2.5.7) both call it rather than casting. Detected by SC-019 clause 3's
corrupt-stream sub-assertion (§4.3).

`loadGlobalParamsToController` inverts each mapping: `masterGain / 2.0`, `(polyphony - 1) / 15.0`,
`softLimit ? 1.0 : 0.0` (model `:227-234`).

### 2.4 `plugins/seraphis/src/parameters/macro_params.h` (FR-041)

```cpp
namespace Seraphis {

/// FR-041. The initializers are LOAD-BEARING: value-initialisation would leave
/// gravity at 0.0f, contradicting SeraphisMacroValues::gravity = 0.5f
/// (seraphis_macro_matrix.h:126) and the registered default. Caught by SC-010's
/// default-state clause.
struct MacroParams {
    std::atomic<float> dream{0.0f};
    std::atomic<float> bloom{0.0f};
    std::atomic<float> dissolve{0.0f};
    std::atomic<float> gravity{0.5f};
    std::atomic<float> entropy{0.0f};
};

} // namespace Seraphis
```

Same six functions as the global pack (`handleMacroParamChange`, `registerMacroParams`,
`formatMacroParam`, `saveMacroParams`, `loadMacroParams`, `loadMacroParamsToController`), modelled
on `plugins/ruinae/src/parameters/macro_params.h:17, 41, 53, 66, 73, 82`. Each handler stores
`std::clamp(static_cast<float>(value), 0.0f, 1.0f)`. Registration: five **plain
`Steinberg::Vst::Parameter`s** via the `ParameterContainer::addParameter(title, units,
stepCount = 0, default, kCanAutomate, id)` convenience overload
(`extern/vst3sdk/public.sdk/source/vst/vstparameters.cpp:416`, which forwards to `:369-378`'s
`new Parameter(info)` — **not** a `RangeParameter`), exactly the model's shape at
`plugins/ruinae/src/parameters/macro_params.h:41-50`. Unit `"%"`, defaults
`0.0 / 0.0 / 0.0 / 0.5 / 0.0` — exactly `SeraphisMacroValues` (`seraphis_macro_matrix.h:122–128`).
FR-048 freezes these types; the eight-ID freeze table lives in `plugin_ids.h` (§2.1).

**INERT (FR-041).** No file in Phase 8 may read `MacroParams` and write it into
`SeraphisMacroMatrix::setMacro/setMacros` (`seraphis_macro_matrix.h:554, :599`). The matrix is
driven only by its own constructed defaults. This is verified as a **negative control** by SC-023
and is what Phase 9 inverts. Stream layout: five `writeFloat` — 20 bytes.

### 2.5 `plugins/seraphis/src/processor/processor.{h,cpp}`

Two TUs day one (spec Scope: the *small* end of roadmap line 368's "2–3"). `processor_params.cpp` /
`processor_state.cpp` are created **only if `processor.cpp` crosses ~1500 lines** — it will not with
two parameter packs.

#### 2.5.1 Header

```cpp
#pragma once
#include "public.sdk/source/vst/vstaudioeffect.h"
#include "parameters/global_params.h"
#include "parameters/macro_params.h"

#include <krate/dsp/effects/aether_reverb.h>
#include <krate/dsp/primitives/smoother.h>
#include <krate/dsp/systems/seraphis_engine.h>
#include <krate/dsp/systems/seraphis_macro_matrix.h>

#include <array>
#include <cstddef>
#include <memory>
#include <vector>

namespace Seraphis {

class Processor : public Steinberg::Vst::AudioEffect {
public:
    Processor();
    ~Processor() override;

    static Steinberg::FUnknown* createInstance(void*) {
        return static_cast<Steinberg::Vst::IAudioProcessor*>(new Processor());
    }

    Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown* context) override;
    Steinberg::tresult PLUGIN_API terminate() override;
    Steinberg::tresult PLUGIN_API setBusArrangements(
        Steinberg::Vst::SpeakerArrangement* inputs,  Steinberg::int32 numIns,
        Steinberg::Vst::SpeakerArrangement* outputs, Steinberg::int32 numOuts) override;
    Steinberg::tresult PLUGIN_API setupProcessing(Steinberg::Vst::ProcessSetup& setup) override;
    Steinberg::tresult PLUGIN_API setActive(Steinberg::TBool state) override;
    Steinberg::tresult PLUGIN_API process(Steinberg::Vst::ProcessData& data) override;
    Steinberg::uint32  PLUGIN_API getLatencySamples() override;
    Steinberg::tresult PLUGIN_API setState(Steinberg::IBStream* state) override;
    Steinberg::tresult PLUGIN_API getState(Steinberg::IBStream* state) override;

    // Test-only read surfaces (never called from process()).
    [[nodiscard]] Krate::DSP::SeraphisEngine* engineForTest() noexcept { return engine_.get(); }
    [[nodiscard]] Krate::DSP::AetherReverb*   reverbForTest() noexcept { return reverb_.get(); }
    [[nodiscard]] std::size_t setPolyphonyCallCountForTest() const noexcept {
        return setPolyphonyCalls_;   // SC-019 clause 3's "must not re-call" seam
    }

private:
    void processParameterChanges(Steinberg::Vst::IParameterChanges* changes) noexcept;
    void pushGlobalParams() noexcept;                       // FR-024 step 0 / FR-024a cl. 1-2
    void renderSlice(float* outL, float* outR, std::size_t n) noexcept;  // FR-024 steps 2-6
    // NO announceLatencyIfChanged(): the reported latency is the constant 1024 in
    // every reachable state and the component has no IComponentHandler (§1.3 C-1/C-2).

    std::unique_ptr<Krate::DSP::SeraphisEngine> engine_;   // FR-022: NEVER by value
    std::unique_ptr<Krate::DSP::AetherReverb>   reverb_;   // FR-022
    Krate::DSP::SeraphisMacroMatrix             macros_{}; // 20 B; by value is fine

    GlobalParams globalParams_{};
    MacroParams  macroParams_{};                            // INERT (FR-041)

    Krate::DSP::OnePoleSmoother masterGain_{1.0f};          // smoother.h:148
    bool        anySamplesSincePrepare_ = false;            // FR-024a cl. 3 snap seam
    double      sampleRate_             = 44100.0;
    bool        prepared_               = false;
    std::size_t lastPushedPolyphony_    = 0;                // reset in setupProcessing (FR-023 cl.3)
    bool        lastPushedSoftLimit_    = true;
    std::size_t setPolyphonyCalls_      = 0;                // test seam only

    // FR-028: sized ONCE at setupProcessing(), to the CONSTANT 2048.
    std::vector<float> dryL_, dryR_, wetL_, wetR_;
    std::array<float, Krate::DSP::SeraphisEngine::kBloomPartialCap> bloomPartials_{};  // :154 == 32
};

// FR-067: unique_ptr ownership keeps the object small enough for a stack local
// in tests. If this ever fails, the tests must heap-allocate (seraphis_engine.h:119-122).
static_assert(sizeof(Processor) < 64u * 1024u,
              "FR-067: Processor must stay small; the 771 968 B engine lives on the heap");

} // namespace Seraphis
```

#### 2.5.2 `initialize()` — FR-020, FR-022

```cpp
tresult PLUGIN_API Processor::initialize(FUnknown* context) {
    tresult r = AudioEffect::initialize(context);
    if (r != kResultOk) return r;

    addEventInput(STR16("Event In"));                              // membrum processor.cpp:117
    addAudioOutput(STR16("Main Out"), SpeakerArr::kStereo);        // :120
    // NO addAudioInput() — anti-model ruinae/src/processor/processor.cpp:56 (FR-020).

    engine_ = std::make_unique<Krate::DSP::SeraphisEngine>();      // FR-022: heap, non-RT, once
    reverb_ = std::make_unique<Krate::DSP::AetherReverb>();
    return kResultOk;
}
```

`terminate()` releases both `unique_ptr`s and calls `AudioEffect::terminate()`.

#### 2.5.3 `setBusArrangements()` — FR-021

Three rejections, all required (SC-011 asserts each):

```cpp
if (numIns  != 0) return kResultFalse;                 // (a) no audio inputs exist
if (numOuts != 1) return kResultFalse;                 // (b) EXACTLY one output bus
if (outputs == nullptr || outputs[0] != SpeakerArr::kStereo) return kResultFalse;  // (c)
return kResultTrue;
```

Clause (b) is where Seraphis diverges from its model: `membrum/src/processor/processor.cpp:1058-1059`
rejects `numOuts < 0 || numOuts > kMaxOutputBuses` because Membrum has 16 buses; Seraphis has one.
Clause (c)'s rationale is the model's own (`:1044-1049`): the render path reads
`channelBuffers32[0]` and `[1]`, so accepting mono would be an out-of-bounds audio-thread read.

**Rejection here is NOT the guard.** A host is free to ignore a `kResultFalse` from
`setBusArrangements` and still present a 1-channel bus, in which case `channelBuffers32` is a
one-element array and `channelBuffers32[1]` is an out-of-bounds heap read — followed by *writes*
through the resulting garbage pointer in steps 4b and `std::copy_n`. §2.5.6 therefore carries an
independent `data.outputs[0].numChannels < 2` early-out, on the model of
`plugins/ruinae/src/processor/processor.cpp:430`
(`if (data.numOutputs == 0 || data.outputs[0].numChannels < 2) return kResultTrue;`). Membrum — the
model for the rest of this processor — has **no** `numChannels` check anywhere in its process path
(`grep -n numChannels plugins/membrum/src/processor/processor.cpp` returns nothing), so copying
Membrum here would copy the gap. SC-021 gains a matching sub-clause (§4.3).

#### 2.5.4 `setupProcessing()` — FR-023, FR-028, FR-053

Ordered exactly as FR-023 lists it:

```cpp
tresult PLUGIN_API Processor::setupProcessing(ProcessSetup& setup) {
    // 0. Out-of-order host calls (setupProcessing before initialize(), or after
    //    terminate()) are exactly what pluginval strictness 5 probes. Every other
    //    lifecycle entry point in this file guards these pointers; so does this one.
    //    prepared_ stays false, so process()'s guard still fires.
    if (engine_ == nullptr || reverb_ == nullptr) return AudioEffect::setupProcessing(setup);

    // NOTE: no MXCSR here. It is per-thread; setting it on the setup thread never
    // reaches the audio thread (membrum/src/processor/processor.cpp:1073-1075).
    sampleRate_ = setup.sampleRate;

    // 1. Block bound is the CONSTANT 2048, never setup.maxSamplesPerBlock.
    const std::size_t bound = kMaxBlockSamples;               // seraphis_engine_config.h

    // 2. Polyphony seeded FROM THE PARAMETER (setState may legally precede this),
    //    through the ONE clamping conversion (§2.3), never a bare cast.
    const std::size_t poly = clampPolyphony(
        globalParams_.polyphony.load(std::memory_order_relaxed));

    engine_->prepare(sampleRate_, makeSeraphisEngineConfig(poly, kEngineSeed, bound)); // :201
    reverb_->prepare(sampleRate_, makeSeraphisReverbConfig(bound));                    // :1614

    // 3. Tracker reset to the SAME value prepare() delivered — never 8, never a
    //    force-push sentinel. The first process() must NOT re-call setPolyphony.
    lastPushedPolyphony_ = engine_->getPolyphony();           // :665, == clamp(poly,1,16)
    lastPushedSoftLimit_ = globalParams_.softLimit.load(std::memory_order_relaxed);
    engine_->setOutputSaturation(lastPushedSoftLimit_
        ? Krate::DSP::SeraphisEngine::kOutputSaturation : 0.0f);                       // :566, :142

    // Scratch: sized ONCE, to the constant (FR-028).
    dryL_.assign(bound, 0.0f); dryR_.assign(bound, 0.0f);
    wetL_.assign(bound, 0.0f); wetR_.assign(bound, 0.0f);

    masterGain_.configure(kMasterGainSmoothMs, static_cast<float>(sampleRate_));       // smoother.h:160
    anySamplesSincePrepare_ = false;                          // arms the FR-024a cl.3 snap
    prepared_ = true;

    // 4. No latency announcement: the reported value is the constant 1024 in every
    //    reachable state, and the component has no IComponentHandler (§1.3 C-1/C-2).
    return AudioEffect::setupProcessing(setup);
}
```

`lastPushedPolyphony_` is read back from `engine_->getPolyphony()` rather than from `poly` so the
tracker records what the engine actually clamped to — belt-and-braces with `clampPolyphony`, which
already puts `poly` in the engine's domain.

**Known residual: the soft-limit push ramps instead of snapping when it is `false` at prepare.**
`SeraphisEngine::prepare` sets `satL_.setSaturation(kOutputSaturation)` **before** `satL_.prepare()`
precisely so the saturator's smoothers are *snapped* to the shipped values instead of ramping in
(`seraphis_engine.h:225-231`). Our `engine_->setOutputSaturation(...)` necessarily runs *after*
`prepare()` — calling it before is useless, because `prepare()` re-applies `kOutputSaturation`
unconditionally — and post-prepare `TapeSaturator::setSaturation` takes the ramping branch
(`if (prepared_) saturationSmoother_.setTarget(saturation_)`, `tape_saturator.h:248-252`). So when a
`setState()` with `softLimit = false` precedes `setupProcessing()` (a case §3.3 explicitly supports),
the first `kDefaultSmoothingMs = 5.0f` (`tape_saturator.h:88`, `:160`) of the render ramps the
saturation blend from `0.15` down to `0` rather than starting at `0`. The bounded effect is a ≤ 0.15
tanh/linear blend (`tape_saturator.h:420-424`) decaying over 5 ms. **Removing it requires a `dsp/`
change** — threading `outputSaturation` through `SeraphisEngineConfig` so the engine's own
pre-prepare snap applies — which Phase 8's scope forbids ("No new DSP is written in this phase").
Recorded as a residual in §8 and deferred to Phase 9; it is **not** silently accepted.

#### 2.5.5 `setActive()` — FR-032

```cpp
tresult PLUGIN_API Processor::setActive(TBool state) {
    if (state) {
        anySamplesSincePrepare_ = false;    // re-arm the master-gain snap (FR-024a cl. 3)
        // NOTHING ELSE. No allocation (SC-026 asserts exactly 0).
    } else {
        if (engine_) engine_->silence();    // seraphis_engine.h:308 — silence()+reset() per voice
        if (reverb_) reverb_->reset();      // aether_reverb.h:1971
    }
    return AudioEffect::setActive(state);
}
```

`SeraphisEngine::silence()` is documented **not** an audio-thread operation
(`seraphis_engine.h:307`, ~32 MiB of capture-ring clearing) — correct here, because `setActive` runs
on the UI/host thread while the audio thread is stopped.

#### 2.5.6 `process()` — FR-024, FR-025, FR-026, FR-029, FR-030

Full control flow; the slice loop is `seraphis_chain.h:190–259` with the host's `sampleOffset`
substituted for the script's resolved indices.

```cpp
tresult PLUGIN_API Processor::process(ProcessData& data) {
    const Krate::DSP::ScopedDenormalMode denormalGuard;   // FR-029; core/scoped_denormal_mode.h:60

    processParameterChanges(data.inputParameterChanges);  // last value per queue (FR-042)

    // FR-030 early-outs, in THIS ORDER. The order is load-bearing twice over:
    //  (a) buffer VALIDATION precedes the readiness check, so the one degenerate
    //      case with a valid writable buffer (process() before setupProcessing())
    //      can be ZERO-FILLED -- FR-030 says "by producing silence", and VST3 does
    //      NOT guarantee zeroed output buffers, so returning without writing hands
    //      the host back the previous plug-in's / previous block's content. Both
    //      wrapped components zero-fill on their own not-prepared path
    //      (seraphis_engine.h:448-451, aether_reverb.h:2172-2176); so does this one.
    //  (b) nothing reads data.outputs[0] until numOutputs > 0 and outputs != nullptr
    //      are established (SC-021's ordering clause).
    if (data.numOutputs <= 0 || data.outputs == nullptr) return kResultOk;
    if (data.outputs[0].channelBuffers32 == nullptr)     return kResultOk;
    // A host may ignore setBusArrangements' kResultFalse and still present a mono
    // bus; channelBuffers32 is then a ONE-element array and [1] is out of bounds.
    // Model: plugins/ruinae/src/processor/processor.cpp:430.
    if (data.outputs[0].numChannels < 2)                 return kResultOk;
    if (data.numSamples <= 0)                            return kResultOk;
    const auto total = static_cast<std::size_t>(data.numSamples);   // now known > 0
    float* outL = data.outputs[0].channelBuffers32[0];
    float* outR = data.outputs[0].channelBuffers32[1];
    if (outL == nullptr || outR == nullptr)              return kResultOk;

    // Not ready -> SILENCE, not "leave the buffer alone".
    if (!prepared_ || !engine_ || !reverb_) {
        std::fill_n(outL, total, 0.0f);
        std::fill_n(outR, total, 0.0f);
        data.outputs[0].silenceFlags = 3;                 // both channels ARE silent
        return kResultOk;
    }

    pushGlobalParams();                                   // FR-024 step 0 (see D-1)

    // Master-gain target + FR-024a cl. 3 snap seam.
    const float gainTarget = globalParams_.masterGain.load(std::memory_order_relaxed);
    if (!anySamplesSincePrepare_) masterGain_.snapTo(gainTarget);   // smoother.h:263
    else                         masterGain_.setTarget(gainTarget); // :170

    std::size_t cursor = 0;
    std::int32_t nextEvent = 0;
    const std::int32_t numEvents =
        (data.inputEvents != nullptr) ? data.inputEvents->getEventCount() : 0;

    while (cursor < total) {
        // 1. dispatch EVERY event due at this slice start (FR-025; the `while`,
        //    not an `if` — two events at the same offset must both fire, else a
        //    zero-length slice reaches processStereoBlock).
        while (nextEvent < numEvents) {
            Steinberg::Vst::Event e{};
            if (data.inputEvents->getEvent(nextEvent, e) != kResultOk) { ++nextEvent; continue; }
            const std::size_t at = clampOffset(e.sampleOffset, total);   // §3.2
            if (at > cursor) break;
            dispatchEvent(e);                                             // FR-031
            ++nextEvent;
        }

        // 2. slice end = next event, block end, or the 2048 bound — whichever first (FR-026).
        std::size_t sliceEnd = total;
        if (nextEvent < numEvents) {
            Steinberg::Vst::Event e{};
            if (data.inputEvents->getEvent(nextEvent, e) == kResultOk) {
                const std::size_t at = clampOffset(e.sampleOffset, total);
                if (at > cursor && at < sliceEnd) sliceEnd = at;
            }
        }
        sliceEnd = std::min(sliceEnd, cursor + kMaxBlockSamples);         // FR-026, the 4096 branch
        const std::size_t n = sliceEnd - cursor;
        if (n == 0) break;                                                // unreachable; guard anyway

        renderSlice(outL + cursor, outR + cursor, n);
        cursor = sliceEnd;
    }

    anySamplesSincePrepare_ = true;
    data.outputs[0].silenceFlags = 0;    // FR-024 silence-flag clause; never asserts silence
    return kResultOk;
}
```

**The two `silenceFlags` writes are both asserted, from opposite pre-seeds** (SC-021, §4.3): the
normal path writes `0` and the test pre-seeds `3`; the not-ready path writes `3` and the test
pre-seeds `0`. Neither assertion can pass because the host "happened to" leave the field alone. On
the early-outs that return *before* the buffers are validated, `silenceFlags` is deliberately left
untouched — reading or writing `data.outputs[0]` when `numOutputs == 0` is precisely what SC-021
forbids.

`renderSlice(outL, outR, n)` is the six-step body:

```cpp
void Processor::renderSlice(float* outL, float* outR, std::size_t n) noexcept {
    // 2. macros -> engine, and the Aether-owned half -> reverb (FR-034)
    macros_.apply(*engine_);                                         // macro_matrix.h:623
    applyAetherTargets(*reverb_, macros_.computeAetherTargets());     // :667 + FR-034a

    // 3. voice sum
    engine_->processStereoBlock(dryL_.data(), dryR_.data(), n);       // engine.h:441

    // 4. the Layer 4 stage the engine cannot own
    reverb_->processStereoBlock(dryL_.data(), dryR_.data(),
                                wetL_.data(), wetR_.data(), n);       // reverb.h:2164

    // 4b. FR-024a cl. 3: master gain, PER SAMPLE, PRE-output-stage.
    for (std::size_t s = 0; s < n; ++s) {
        const float g = masterGain_.process();                        // smoother.h:197
        wetL_[s] *= g;
        wetR_[s] *= g;
    }

    // 5. output stage IN PLACE on the reverb return (saturator -> limiter, ALWAYS last)
    engine_->processOutputStage(wetL_.data(), wetR_.data(), n);       // engine.h:512

    std::copy_n(wetL_.data(), n, outL);
    std::copy_n(wetR_.data(), n, outR);

    // 6. bloom lifecycle — note-OFFs BEFORE note-ONs (seraphis_chain.h:236-254)
    const auto bloom = engine_->consumeBloomEvents();                 // engine.h:654
    for (std::size_t v = 0; v < Krate::DSP::SeraphisEngine::kMaxVoices; ++v) {
        const std::uint32_t bit = std::uint32_t{1} << static_cast<std::uint32_t>(v);
        if ((bloom.noteOffMask & bit) != 0u) reverb_->bloomNoteOff(static_cast<std::int32_t>(v));
    }
    for (std::size_t v = 0; v < Krate::DSP::SeraphisEngine::kMaxVoices; ++v) {
        const std::uint32_t bit = std::uint32_t{1} << static_cast<std::uint32_t>(v);
        if ((bloom.noteOnMask & bit) == 0u) continue;
        std::size_t count = 0;
        engine_->collectHeldPartials(v, bloomPartials_.data(), bloomPartials_.size(), count); // :596
        if (count > 0) reverb_->bloomNoteOn(static_cast<std::int32_t>(v),
                                            bloomPartials_.data(), count);                    // :2392
    }
}
```

**FR-027 compliance:** nothing here mirrors `processOutputStage`'s internal 64-sample loop. The
engine's own banner (`seraphis_engine.h:506-511`) states it is *"a CADENCE CHOICE, NOT A SIZE
CONSTRAINT … Phase 8 must not copy the loop as if it were a requirement."*

**RT-safety audit of `process()`** (SC-007): no `new`/`delete`, no `resize`/`assign` on any vector
(all sized in `setupProcessing`), no lock, no `try`/`throw`, no I/O, no `std::function`. The only
non-`noexcept` call in the body is `IEventList::getEvent`, which is a C-ABI `tresult` method. The
scratch vectors are indexed with `.data()` + `[]` — never `.at()` (which throws).

#### 2.5.7 `pushGlobalParams()` — FR-024a clauses 1–2

```cpp
void Processor::pushGlobalParams() noexcept {
    // clampPolyphony (§2.3), NOT a bare cast: the comparison below is against
    // engine_->getPolyphony(), which is clamped (seraphis_engine.h:322, :665).
    // Comparing an unclamped stored value against a clamped read-back NEVER
    // converges -- see the convergence note under this block.
    const std::size_t poly = clampPolyphony(
        globalParams_.polyphony.load(std::memory_order_relaxed));
    if (poly != lastPushedPolyphony_) {                 // ON CHANGE ONLY
        engine_->setPolyphony(poly);                    // engine.h:321
        lastPushedPolyphony_ = engine_->getPolyphony(); // :665, re-read post-clamp
        ++setPolyphonyCalls_;
    }
    const bool soft = globalParams_.softLimit.load(std::memory_order_relaxed);
    if (soft != lastPushedSoftLimit_) {                 // ON CHANGE ONLY
        engine_->setOutputSaturation(
            soft ? Krate::DSP::SeraphisEngine::kOutputSaturation : 0.0f);   // :566, :142
        lastPushedSoftLimit_ = soft;
    }
}
```

Re-calling `setPolyphony` unconditionally is wrong twice over: it re-arms the voice-sum smoother
(`sumGain_.setTarget(...)`, `seraphis_engine.h:349`) every block, and `setVoiceCount` walks the
allocator's excess-slot loop (`:339-348`) for nothing.

**Convergence note (why `clampPolyphony` is mandatory here, not decorative).** `setPolyphony` clamps
to `[1, kMaxVoices]` (`seraphis_engine.h:322`) and `getPolyphony()` returns the *clamped* value
(`:665`). If the stored atomic could hold an out-of-range value — which a `setState()` carrying
polyphony `0`, `20`, or a negative int32 can produce if the loader does not clamp — then
`poly != lastPushedPolyphony_` would be true on **every block, forever**: the exact per-block
`sumGain_` re-arm and excess-slot walk this section exists to prevent. Clamping at the single
conversion point puts both sides of the comparison in the same domain, so the detector converges
after one push. Asserted by SC-019 clause 3's corrupt-stream sub-assertion (§4.3).

#### 2.5.8 `getLatencySamples()` — FR-033

```cpp
Steinberg::uint32 PLUGIN_API Processor::getLatencySamples() {
    return reverb_ ? static_cast<Steinberg::uint32>(reverb_->getLatencySamples())  // :2612
                   : 0u;
}
```

**That is the whole of FR-033 in Phase 8.** For the shipped config the value is a constant **1024**
at every sample rate, because `getLatencySamples()` returns `spectralEnabled_ ? diffusionFftSize_ : 0`
— a *sample count*, not a time (`aether_reverb.h:2607-2613`, whose banner says no setter changes it)
— and `makeSeraphisReverbConfig` ships `spectralDiffusionEnabled = true`, `diffusionFftSize = 1024`.
There is therefore no latency *change* to announce, and no sanctioned route to announce one from a
processor (§1.3 C-2): `announceLatencyIfChanged()` and `lastReportedLatency_` **do not exist**. Hosts
read `getLatencySamples()` after `setupProcessing()`. SC-013 clause 4 is restated as an invariance
assertion over the full state matrix (§4.3); the corresponding spec amendment is §8 item 2.

#### 2.5.9 `getState()` / `setState()` — FR-045, FR-046

```cpp
tresult PLUGIN_API Processor::getState(IBStream* state) {
    Steinberg::IBStreamer streamer(state, kLittleEndian);   // ruinae processor_state.cpp:24
    streamer.writeInt32(kCurrentStateVersion);              // :27
    saveGlobalParams(globalParams_, streamer);
    saveMacroParams(macroParams_, streamer);
    return kResultOk;
}

tresult PLUGIN_API Processor::setState(IBStream* state) {
    if (state == nullptr) return kResultFalse;
    Steinberg::IBStreamer streamer(state, kLittleEndian);
    Steinberg::int32 version = 0;
    if (!streamer.readInt32(version))          return kResultFalse;
    if (version > kCurrentStateVersion)        return kResultFalse;   // FR-046
    loadGlobalParams(globalParams_, streamer);   // EOF-safe: short stream leaves defaults
    loadMacroParams(macroParams_, streamer);
    return kResultOk;                            // NO prepare() reachable from here
}
```

`setState()` writes only `std::atomic<>` members, so it is safe concurrently with `process()` (spec
edge case, "Real-time-safety boundaries").

### 2.6 `plugins/seraphis/src/controller/controller.{h,cpp}` — FR-047, FR-048, FR-050, FR-052, FR-055

```cpp
class Controller : public Steinberg::Vst::EditControllerEx1,
                   public VSTGUI::VST3EditorDelegate {
public:
    static Steinberg::FUnknown* createInstance(void*) {
        return static_cast<Steinberg::Vst::IEditController*>(new Controller());
    }
    Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown* context) override;
    Steinberg::tresult PLUGIN_API terminate() override;
    Steinberg::tresult PLUGIN_API setComponentState(Steinberg::IBStream* state) override;
    Steinberg::tresult PLUGIN_API getParamStringByValue(
        Steinberg::Vst::ParamID, Steinberg::Vst::ParamValue, Steinberg::Vst::String128) override;
    Steinberg::IPlugView* PLUGIN_API createView(Steinberg::FIDString name) override;
private:
    std::unique_ptr<Krate::Plugins::PresetManager> presetManager_;
};
```

**No `INoteExpressionController`** (FR-019). No `createCustomView` / `verifyView` overrides — there
are no custom views until Phase 11 (FR-018, FR-056).

`initialize()`:
1. `EditControllerEx1::initialize(context)`;
2. `registerGlobalParams(parameters)`, `registerMacroParams(parameters)` — exactly eight parameters;
3. `presetManager_ = std::make_unique<Krate::Plugins::PresetManager>(makeSeraphisPresetConfig(), nullptr, this);`
   — the shape at `plugins/ruinae/src/controller/controller.cpp:225-226`; the ctor is
   `PresetManager(PresetManagerConfig, IComponent*, IEditController*, path userDirOverride = {}, path factoryDirOverride = {})`
   (`plugins/shared/src/preset/preset_manager.h:55-61`). **No `UpdateChecker`** (FR-052) — it spawns a
   `std::thread` and a network fetch that would land inside the editor-lifecycle harness, the ASan
   lane and the valgrind nightly.

`setComponentState()`: read the version `int32`, reject `> kCurrentStateVersion`, then
`loadGlobalParamsToController(streamer, setParam)` and `loadMacroParamsToController(streamer, setParam)`
where `setParam = [this](ParamID id, double v) { setParamNormalized(id, v); }` (FR-047; template
helpers at `ruinae/…/global_params.h:220-243`, `macro_params.h:82-90`).

**This path has its own assertion** — SC-010's controller clause (§4.3). Without it, a
`setComponentState` that returns `kResultOk` without reading the stream, or that loads the two packs
in the wrong order, passes SC-009, SC-010 and pluginval, and the sixth function of each pack
(`loadGlobalParamsToController` / `loadMacroParamsToController`, FR-040/FR-041) has **no** detector
at all. The clause is reachable from `seraphis_tests` because §5.2 compiles
`../src/controller/controller.cpp` a second time into the test binary.

`controller.cpp` also carries the **compile touch point** for `src/update/seraphis_update_config.h`
(§2.8) — that header is included by nothing else and would otherwise never be compiled on any leg.

`getParamStringByValue()`: try `formatGlobalParam`, then `formatMacroParam`, else fall through to
`EditControllerEx1::getParamStringByValue` (so the `StringListParameter` polyphony control formats
itself).

`createView()`:

```cpp
IPlugView* PLUGIN_API Controller::createView(FIDString name) {
    if (FIDStringsEqual(name, ViewType::kEditor))
        return new VSTGUI::VST3Editor(this, "editor", "editor.uidesc");   // gradus controller.cpp:245
    return nullptr;
}
```

FR-055's robustness requirement is satisfied *by construction*: the controller registers no custom
views and holds no raw view pointers, so there is nothing for `willClose()` to dangle. The harness
scenario (`editor_lifecycle_harness.h:107-132`: `attached(nullptr, …)` then `removed()`) therefore
touches only VSTGUI-owned state.

### 2.7 `src/preset/seraphis_preset_config.h` — FR-050, FR-051

```cpp
#pragma once
#include "preset/preset_manager_config.h"
#include "../plugin_ids.h"

namespace Seraphis {
inline Krate::Plugins::PresetManagerConfig makeSeraphisPresetConfig() {
    return Krate::Plugins::PresetManagerConfig{
        /*.processorUID      =*/ kProcessorUID,
        /*.pluginName        =*/ "Seraphis",
        /*.pluginCategoryDesc=*/ "Synth",
        /*.subcategoryNames  =*/ { "Textures" }
    };
}
} // namespace Seraphis
```

Field order is load-bearing (`plugins/shared/src/preset/preset_manager_config.h:17-18` says so
explicitly); the comment-style initialisers mirror `ruinae_preset_config.h:18-27`.
`resources/presets/Textures/.gitkeep` seeds the filesystem half so the directory and the XML
metadata agree from day one.

**Correction to the spec's verification claim.** FR-050 says instantiating a `PresetManager` in
`Controller::initialize()` makes the `Textures` category "*genuinely scanned*" during pluginval, so
that FR-050/FR-051 are "*verified by SC-003 rather than by inspection*". **That is false.**
`PresetManager::PresetManager` (`plugins/shared/src/preset/preset_manager.cpp:16-29`) only stores
`config_`, `processor_`, `controller_` and the two path overrides; **all** enumeration happens in
`scanPresets()` (`:37-56`), and nothing in Phase 8 calls it — the placeholder `.uidesc` (§2.10) has
no preset browser and FR-054 forbids custom views. A `makeSeraphisPresetConfig()` with the wrong
subcategory name, the wrong `pluginName`, or a missing `resources/presets/Textures/` would pass
SC-003 and every other criterion.

**Resolution: a real assertion, not a re-worded claim.** The controller test file gains a
`Seraphis_PresetConfigIsLive` SECTION inside `Seraphis_EditorLifecycle` (case names are fixed by
FR-066; SECTIONs are not) that calls `scanPresets()` against the repo's own resources directory and
checks the config fields — spelled out in §4.3. Instantiating the manager in `initialize()` is still
required (it is FR-050's text and it exercises the ctor under pluginval and under the ASan lane); it
is simply not, by itself, evidence. The spec's justification sentence should be amended (§8).

### 2.8 `src/update/seraphis_update_config.h` — FR-052

```cpp
#pragma once
#include "update/update_checker_config.h"
#include "../version.h"

namespace Seraphis {
inline Krate::Plugins::UpdateCheckerConfig makeSeraphisUpdateConfig() {
    return Krate::Plugins::UpdateCheckerConfig{
        /*.pluginName     =*/ stringPluginName,     // cmake/version.h.in:31
        /*.currentVersion =*/ VERSION_STR,          // :25
        /*.endpointUrl    =*/ "https://rolandzwaga.github.io/krate-audio/versions.json"
    };
}
} // namespace Seraphis
```

Compiled, unused — and **listing it in `smtg_add_vst3plugin`'s source list does not compile it**.
CMake sets `HEADER_FILE_ONLY` on `.h` entries in a target's source list: they are added to IDE
project files and never handed to the compiler. Every other header in §5.1's list (`plugin_ids.h`,
`parameters/*.h`, `engine/seraphis_engine_config.h`, `preset/seraphis_preset_config.h`) is compiled
only because some `.cpp` includes it. `seraphis_update_config.h` is included by nothing — FR-052
forbids instantiating `UpdateChecker` — so without a touch point a syntax error in it ships
undetected on every leg.

**Resolution: an honest compile touch point in `controller.cpp`** (which is compiled twice: once
into the plugin, once into `seraphis_tests`, §5.2), immediately after the includes:

```cpp
#include "update/seraphis_update_config.h"
// FR-052: the config header ships compiled but unused in Phase 8 (no UpdateChecker
// instance -- it spawns a thread and a network fetch). This is the ONLY thing that
// compiles the header; a CMake source-list entry would not (HEADER_FILE_ONLY).
static_assert(std::is_same_v<decltype(Seraphis::makeSeraphisUpdateConfig()),
                             Krate::Plugins::UpdateCheckerConfig>);
```

No `UpdateChecker` object is constructed, so FR-052's prohibition holds exactly.

### 2.9 `src/entry.cpp` — FR-018

`plugins/ruinae/src/entry.cpp:40-78` with `Ruinae` → `Seraphis`, and **no `ui/*.h` includes** (Ruinae
includes them only to trigger custom `ViewCreator` static registration; Seraphis registers none).
`#define stringPluginName "Seraphis"` is an *identical* redefinition of the macro
`cmake/version.h.in:31` generates from `version.json`'s `"name"`, so it is warning-free — keep the
two spellings identical.

### 2.10 `resources/editor.uidesc` — FR-054

Placeholder, **stock views only**, template named `"editor"`, eight `<control-tag>` bindings.
No custom view class name may appear (spec cross-platform edge case: on a leg where the registration
TU was not linked, `VST3Editor::open()` silently drops the view and the lifecycle test passes
vacuously).

View-class choices are constrained to classes this repo already instantiates from `.uidesc`
(inventory across `plugins/*/resources/editor.uidesc`: `CSlider` ×166, `COptionMenu` ×289,
`CCheckBox` ×8, `CTextLabel` ×921, `CViewContainer` ×239) — no bitmap dependencies:

```xml
<?xml version="1.0" encoding="UTF-8"?>
<vstgui-ui-description version="1">
  <!-- Phase 8 PLACEHOLDER. Phase 11 replaces this file wholesale. Stock views
       only: a custom class name here would be dropped silently on a leg where
       the ViewCreator TU was not linked, making SC-012 pass vacuously. -->
  <colors>
    <color name="bg"    rgba="#1A1A2Eff"/>
    <color name="text"  rgba="#E0E0F0ff"/>
    <color name="track" rgba="#242438ff"/>
    <color name="accent" rgba="#8FA8D8ff"/>
  </colors>
  <fonts>
    <font name="label-font" font-name="Arial" size="11"/>
  </fonts>
  <control-tags>
    <control-tag name="MasterGain"    tag="0"/>
    <control-tag name="Polyphony"     tag="1"/>
    <control-tag name="SoftLimit"     tag="2"/>
    <control-tag name="MacroDream"    tag="100"/>
    <control-tag name="MacroBloom"    tag="101"/>
    <control-tag name="MacroDissolve" tag="102"/>
    <control-tag name="MacroGravity"  tag="103"/>
    <control-tag name="MacroEntropy"  tag="104"/>
  </control-tags>
  <template name="editor" class="CViewContainer" size="420, 300"
            background-color="bg" background-color-draw-style="filled">
    <!-- 6 continuous parameters (plain Vst::Parameter, §2.1's freeze table) -> CSlider -->
    <view class="CTextLabel" title="Master Gain" origin="16, 16"  size="120, 16" font="label-font" font-color="text" transparent="true"/>
    <view class="CSlider" control-tag="MasterGain" origin="150, 16" size="240, 20"
          orientation="horizontal" draw-frame="true" draw-back="true" draw-value="true"
          frame-color="accent" back-color="track" value-color="accent"/>
    <!-- …identical rows for MacroDream / MacroBloom / MacroDissolve / MacroGravity / MacroEntropy… -->

    <!-- StringListParameter -> COptionMenu (type MUST match FR-048's registration) -->
    <view class="CTextLabel" title="Polyphony" origin="16, 220" size="120, 16" font="label-font" font-color="text" transparent="true"/>
    <view class="COptionMenu" control-tag="Polyphony" origin="150, 218" size="120, 20"
          font="label-font" font-color="text" frame-color="accent" back-color="track"
          fill-color="track" frame-width="1" transparent="false"
          style-3D-in="false" style-3D-out="false"/>

    <!-- stepped toggle -> CCheckBox -->
    <view class="CTextLabel" title="Soft Limit" origin="16, 252" size="120, 16" font="label-font" font-color="text" transparent="true"/>
    <view class="CCheckBox" control-tag="SoftLimit" origin="150, 250" size="40, 20"
          title="" font="label-font" font-color="text"/>
  </template>
</vstgui-ui-description>
```

**The harness alone does NOT verify any of this.** `exerciseEditorLifecycle` asserts exactly three
things — `attached() == kResultTrue`, `getFrame() != nullptr`, `getFrame()->getNbViews() > 0`
(`editor_lifecycle_harness.h:120-128`). A template containing a single `CTextLabel` satisfies all
three, so a `.uidesc` shipping one control instead of eight passes SC-012 as written; nothing
inspects `<control-tags>`, the control count or the control class, and `VST3Editor` binds a `CSlider`
to a `StringListParameter` (or a `COptionMenu` to a plain `Parameter`) with no error path. **FR-054's
eight bound controls and the parameter-type match therefore need their own assertion**, which §4.3's
SC-012 clause 2b supplies: walk the attached frame's view tree and require exactly eight `CControl`s
carrying tags `{0, 1, 2, 100, 101, 102, 103, 104}`, with `dynamic_cast<COptionMenu*>` non-null for
tag 1 and a toggle class for tag 2. Only with that clause does the type match "surface now rather
than in Phase 11"; the same correction applies to R9's mitigation column (§6).

### 2.11 `resources/` — AU identity (FR-015, FR-016, FR-017)

`auv3/audiounitconfig.h.in`: a **complete copy** of
`plugins/membrum/resources/auv3/audiounitconfig.h.in:1–39` with `Membrum`→`Seraphis`,
`Mbrm`→`Srph`. All fourteen defines are required, including the unquoted-token forms
(`kAUcomponentType1 aumu`, `kAUcomponentSubType1 Srph`, `kAUcomponentManufacturer1 KrAt`) and the
trailing `kAUcomponentFlags 0`, `kAUcomponentFlagsMask 0`,
`kAUapplicationDelegateClassName AppDelegate` — the AUv3 target built by
`krate_plugin_platform_setup` (`cmake/KratePlugin.cmake:208-219`) consumes them, and a five-value
header does not compile the macOS AUv3 target. `kSupportedNumChannels 02` keeps the model's
digit-pair rationale comment verbatim (model `:26-34`), rewritten for a single-output instrument.
`kAUcomponentVersion @AU_COMPONENT_VERSION@` — generated (`KratePlugin.cmake:97-105`).

`au-info.plist`: `plugins/membrum/resources/au-info.plist:23-50` with the same substitutions —
exactly one `AudioComponents` dict (`factoryFunction AUWrapperFactory`, `manufacturer KrAt`,
`name "Krate Audio: Seraphis"`, `subtype Srph`, `type aumu`) and exactly one
`AudioUnit SupportedNumChannels` dict (`Inputs 0` / `Outputs 2`). A mismatch with the bus config is
the documented `-10875` AU-init failure.

`auv3/macOS/Seraphis.entitlements`: copy of Membrum's (one key,
`com.apple.security.app-sandbox = true`); referenced by `KratePlugin.cmake:213`.

### 2.12 Non-source files

| File | Content |
|---|---|
| `version.json` | six keys `version "0.1.0"`, `name "Seraphis"`, `description`, `publisher "Krate Audio"`, `url "https://rolandzwaga.github.io/krate-audio/seraphis/"`, `copyright`. **No `preset_subdir`** — `release.yml:297-303` reads it via `jq -r '.preset_subdir // empty'`; absent ⇒ presets stage at `Krate Audio/Seraphis` (FR-002). |
| `CHANGELOG.md` | a `## [0.1.0]` section — `tools/check-changelog-coverage.js` parses `^##\s*\[([^\]]+)\]` (`:84-88`). |
| `README.md` | short description + build/test/pluginval commands. |
| `CLAUDE.md` | leaf modelled on `plugins/membrum/CLAUDE.md:1-33`: type, src skeleton, param-ID scheme, test target invocation, pluginval path — **plus** FR-009's two durable decisions (MPE deferred to Phase 9 **with** the released-FUID host-cache caveat; preset categories are additive-only and `Textures` is never renamed). |
| `installers/windows/setup.iss` | `plugins/gradus/installers/windows/setup.iss` with `Gradus`→`Seraphis` and a **fresh** `AppId` GUID; consumed by `release.yml:235`. |
| `installers/linux/README.txt` | Gradus's, retitled; consumed by `release.yml:372`. |
| `docs/` | **directory only** (`.gitkeep`). Phase 12 authors `index.html` (FR-080). |
| `src/ui/` | **empty**, `.gitkeep` only (FR-056). |
| `resources/presets/Textures/` | `.gitkeep` (FR-051). |

---

## 3. Pinned algorithms and control flow

Nothing here is an implementer's choice.

### 3.1 Master-gain smoother (FR-024a clause 3) — exact discretisation

`Krate::DSP::OnePoleSmoother` (`smoother.h:134`). Its recurrence, from `process()` at `:197-208`:

```
    y[n] = t + a * (y[n-1] - t)                      // t = target, a = coefficient
    a    = calculateOnePolCoefficient(timeMs, fs)    // 99 % settle in `timeMs`
    if |y - t| < kCompletionThreshold: y := t        // hard snap at the tail
```

Binding parameters:

- **`configure(kMasterGainSmoothMs=20.0f, float(sampleRate))`** once per `setupProcessing()`
  (`smoother.h:160`). 20 ms is the same family as `SeraphisEngine::kSumGainSmoothMs = 20.0f`
  (`seraphis_engine.h:138`); it is a **named constant in `seraphis_engine_config.h`**, never a
  literal at the use site.
- **Cadence: exactly one `process()` call per output sample**, inside `renderSlice`'s 4b loop
  (`smoother.h:197`). *Not* `advanceSamples(n)` (`:236`) and *not* once per slice. This is what makes
  SC-008 satisfiable: `y[n]` after `N` samples is `t + a^N (y0 - t)` regardless of how the `N` samples
  were partitioned, but a per-slice update makes the trajectory a function of the partition.
- **Target update: once per `process()` call**, before the slice loop. The atomic cannot change
  within a call — `processParameterChanges` runs at the top and takes the *last* value of each queue
  (FR-042) — so hoisting is observationally identical to a per-slice `setTarget` and is trivially
  partition-invariant. *(Design decision **D-1**; the same reasoning hoists `pushGlobalParams()`.)*
- **Initial value: `snapTo(target)`, not a ramp**, on the first `process()` after
  `setupProcessing()`/`setActive(true)`, gated by `!anySamplesSincePrepare_` — literally the
  `prepared_ && !anySamplesProcessed_` seam `AetherReverb::applyControl` uses
  (`aether_reverb.h:2951-2956`). Without the snap, a render at `kMasterGainId = 0.0` ramps down from
  the previous value and its first ~20 ms are non-zero, failing SC-019 clause 1 for a *correct*
  implementation.

**Placement is load-bearing, not stylistic.** The gain multiplies the **reverb return**, between
step 4 and step 5, so `processOutputStage`'s `limiter_.processBlock(l, r, n)`
(`seraphis_engine.h:521`, ceiling `0.8912509f` from `kDefaultCeilingDb = -1.0f`,
`true_peak_limiter.h:46, 168`) remains the **last** stage. A post-limiter ×2.0 multiply would
produce peaks near 1.78 and make SC-006 clause 1 unsatisfiable by construction.

**Denormal note:** `OnePoleSmoother::process()` flushes denormals itself (`:205`,
`detail::flushDenormal`), *and* `process()` runs inside `ScopedDenormalMode`. Belt and braces; both
required, because the smoother's flush does not cover the reverb tail.

### 3.2 Event offset resolution and slice partitioning (FR-025, FR-026)

`SeraphisEngine::noteOn/noteOff` (`:370`, `:415`) take **no sample offset**. Sub-division is the only
way to deliver one — this is timing rule 1 in `seraphis_chain.h:24-34`, whose banner says it is
"*exactly what Phase 8's event loop does with the host's sampleOffset*".

```cpp
// Clamp into [0, numSamples]. Negative and past-the-end offsets are both legal
// inputs from a host and must never produce a negative slice length.
[[nodiscard]] inline std::size_t clampOffset(Steinberg::int32 offset, std::size_t total) noexcept {
    if (offset <= 0) return 0u;
    const auto o = static_cast<std::size_t>(offset);
    return (o > total) ? total : o;
}
```

Three properties the loop must hold, each asserted by a test:

1. **All events at the same offset fire before the next render.** The dispatch loop is a `while`
   with `at <= cursor`, not an `if` — otherwise the second event resolves the next `sliceEnd` to
   `cursor` and a zero-length slice reaches `processStereoBlock` (SC-022 clause 5;
   `seraphis_chain.h:195-198` has the same `while`).
2. **Events are assumed sorted by `sampleOffset`.** VST3 requires it. The `at > cursor` test plus
   `std::min` clamping keeps the loop well formed on a malformed (unsorted) list: a late event with
   an earlier offset fires at the current cursor rather than rewinding it.
3. **No slice exceeds 2048.** `sliceEnd = std::min(sliceEnd, cursor + kMaxBlockSamples)` is the
   *only* bound, and it is the **same constant** `makeSeraphisEngineConfig`/`makeSeraphisReverbConfig`
   pass as `maxBlockSamples`, so the engine's ceiling and the reverb's prepared ceiling cannot drift
   apart (FR-023 clause 1 / FR-026). SC-008's 4096 partition is the only one that enters this branch.

Event translation (FR-031):

| VST3 event | Action |
|---|---|
| `kNoteOnEvent`, `noteOn.velocity > 0.0f` | `engine_->noteOn(uint8(pitch), uint8(clamp(velocity*127+0.5, 1, 127)))` |
| `kNoteOnEvent`, `noteOn.velocity <= 0.0f` | `engine_->noteOff(uint8(pitch))` |
| `kNoteOffEvent` | `engine_->noteOff(uint8(pitch))` |
| anything else | ignored |

Pitch is range-guarded to `[0, 127]` before the `uint8_t` cast (`event.noteOn.pitch` is `int16`).
The velocity-0 path is redundant with the engine's own guard (`seraphis_engine.h:374-377` maps
`velocity == 0` to `noteOff`) but is written explicitly so SC-022 clause 2 tests the *wrapper's*
behaviour, not the engine's.

The `+ 0.5` rounding and the **floor of 1** are premise correction **C-3** (§1.3), not a stylistic
choice: FR-031's literal `velocity*127` truncates a legal velocity of, say, `0.003` to `0`, which
that same engine guard turns into a `noteOff` — a note-on that releases. SC-022 gains a sub-clause
for velocity in `(0, 1/127)` (§4.3) so the floor is detectable.

### 3.3 On-change parameter push (FR-024a clauses 1–2)

Trackers are set **at prepare**, not at construction, and to the value the engine actually holds:
`lastPushedPolyphony_ = engine_->getPolyphony()` after `prepare()`. Consequences, all asserted by
SC-019 clause 3:

- prepare already delivered the voice count, so the first `process()` must **not** re-call
  `setPolyphony` — which would re-arm `sumGain_` (`seraphis_engine.h:349`) on every host prepare;
- a `setState()` arriving **before** `setupProcessing()` (legal, and common on preset load) is
  honoured, because `cfg.polyphony` is read from the atomic;
- a `setState()` arriving **after** prepare is honoured on the next `process()`, via the
  change detector.

`setPolyphonyCalls_` is a test-only counter incremented in the same branch — SC-019's "must not
re-call" assertions read it. It is a plain `std::size_t` written only from the audio thread and read
only from the test thread after the render completes; no atomic needed.

### 3.4 State stream byte layout (FR-045, FR-046, SC-010)

Little-endian `IBStreamer`, in this exact order:

| Offset | Bytes | Field | Writer |
|---|---|---|---|
| 0 | 4 | `int32 kCurrentStateVersion` (== 1) | `getState` |
| 4 | 4 | `float masterGain` (linear, [0,2]) | `saveGlobalParams` |
| 8 | 4 | `int32 polyphony` ([1,16]) | " |
| 12 | 4 | `int32 softLimit` (0/1) | " |
| 16 | 4 | `float dream` | `saveMacroParams` |
| 20 | 4 | `float bloom` | " |
| 24 | 4 | `float dissolve` | " |
| 28 | 4 | `float gravity` (default **0.5**) | " |
| 32 | 4 | `float entropy` | " |

Total **36 bytes**. Truncation semantics: each reader is EOF-safe
(`ruinae/…/global_params.h:203-211`); a stream cut at any of the eight boundaries leaves every
later field at its *registered default* and returns `kResultOk` from `setState` (the version int32
is the only mandatory field).

`getState` on a freshly constructed processor must therefore stream `gravity == 0.5f` — the
assertion that catches value-initialised `MacroParams` (SC-010 default-state clause).

**No float-bit digest of the *render*.** A digest over this **serialized byte stream** is the
sanctioned form (`dsp/CLAUDE.md`); a digest over rendered audio is forbidden (FR-068,
`tools/lint-float-bit-goldens.js`).

### 3.5 Seed policy (spec, "Seed determinism")

`kEngineSeed = kReverbSeed = 1u`, both set **explicitly** in `seraphis_engine_config.h` rather than
inherited from the struct defaults (`seraphis_engine.h:96`, `aether_reverb.h:1586`), so a future
default change in `dsp/` cannot silently move Seraphis's sound. Two instances in one host share a
trajectory: a **known** Phase 8 property; Phase 9 owns any per-instance seed parameter.

---

## 4. Test plan

Target: `seraphis_tests`. Eight `TEST_CASE` names are **fixed by FR-066** so SC-002 clause 1 can
assert them via `--list-tests`. Tags: every case carries `[seraphis]` plus its area; the editor case
**must** additionally carry `[lifecycle]` (`.github/workflows/valgrind-nightly.yml:283` invokes each
binary as `"$BINDIR/$bin" '[lifecycle]'`, a Catch2 tag filter — without the tag the nightly lane
selects zero Seraphis tests).

### 4.1 File → case → coverage

| File | `TEST_CASE` (+ tags) | FR / SC |
|---|---|---|
| `tests/unit/test_main.cpp` | — (`moduleHandle`, `enableFTZDAZ()`, `#include <allocation_operator_overrides.h>`) | FR-061, FR-066a |
| `tests/unit/processor_bus_test.cpp` | `Seraphis_ProcessorBusSetup` `[seraphis][processor][bus]` | FR-020, FR-021 → SC-011 |
| `tests/unit/param_denorm_test.cpp` | `Seraphis_ParamDenormRoundTrip` `[seraphis][params]` | FR-040, FR-042, FR-043, FR-048 → SC-009 |
| `tests/unit/state_roundtrip_test.cpp` | `Seraphis_StateRoundTrip` `[seraphis][state]` | FR-041, FR-045, FR-046, FR-047 → SC-010 |
| `tests/unit/midi_event_test.cpp` | `Seraphis_MidiEventTranslation` `[seraphis][midi]` | FR-025, FR-027, FR-031 → SC-008, SC-022 |
| `tests/unit/lifecycle_test.cpp` | `Seraphis_ProcessorLifecycle` `[seraphis][processor][lifecycle-proc]` | FR-029, FR-030, FR-032, FR-033 → SC-007, SC-013, SC-021, SC-026 |
| `tests/unit/controller/editor_lifecycle_test.cpp` | `Seraphis_EditorLifecycle` `[seraphis][controller][ui][lifecycle]` | FR-050, FR-051, FR-054, FR-055 → SC-012 (clauses 2b bound controls, 2c preset config) |
| `tests/integration/processor_audio_test.cpp` | `Seraphis_ProcessorRendersHeldNote` `[seraphis][integration]` | FR-024, FR-024a, FR-026, FR-034, FR-034a → SC-005, SC-006, SC-023, SC-024 |
| `tests/integration/param_flow_test.cpp` | `Seraphis_ParamFlowReachesEngine` `[seraphis][integration]` | FR-024a, FR-044 → SC-019, SC-027 |
| `tests/integration/processor_audio_test.cpp` | `Seraphis_ProcessorCpuOverhead` `[.perf][seraphis]` | SC-014 (NON-GATING) |

Note the tag choice `[lifecycle-proc]` for the processor-lifecycle case: **it must not be
`[lifecycle]`**, or the valgrind nightly (which runs `[lifecycle]` under memcheck) would drag a
771 968 B engine and a full 4 s render into a 60-minute-budget job.

### 4.2 Shared fixture

One header, `tests/seraphis_test_fixture.h`, used by every audio-touching case:

```cpp
struct ProcessorFixture {
    std::unique_ptr<Seraphis::Processor> proc = std::make_unique<Seraphis::Processor>();
    // prepare(sr, blockSize) -> initialize(nullptr), setupProcessing, setActive(true)
    // renderBlocks(n, blockSize, events) -> fills interleaved-free L/R vectors
    // setParam(id, normalized)              -> one-point ParamValueQueue
    // setParamPoints(id, {v0, v1, v2})      -> MULTI-point queue, FR-042's "last value"
    // pushEvent(kind, pitch, velocity, sampleOffset)
    // withOutputChannels(n)                 -> ProcessData with n channels (SC-021 mono clause)
    // seedOutputBuffers(value)              -> non-zero canary before a degenerate call
};
```

`setParamPoints` is **not** optional. FR-042's "*using the last value of each parameter queue*"
clause is untestable with one-point queues: an implementation reading `queue->getPoint(0, …)` is
byte-for-byte indistinguishable from one reading `getPointCount()-1`, and automation lanes routinely
deliver multi-point queues. SC-009 gains a clause that uses it (§4.3). `seedOutputBuffers` is what
makes SC-021's "produces silence" and the `silenceFlags` assertions non-vacuous rather than passing
because the fixture zeroed its own vectors.

**FR-008 amendment required.** FR-008 says the skeleton "*MUST be exactly the file list below*" and
that list contains no `tests/seraphis_test_fixture.h`. This plan makes that header a required
artefact for every audio-touching case, so FR-008's list must gain it (plus the `.gitkeep` entries
under `docs/`, `src/ui/` and `resources/presets/Textures/` that §2.12 adds). Recorded as a spec
amendment in §8 — a compliance table filled honestly would otherwise have to mark FR-008 as not met.

Backed by the SDK hosting helpers already linked into the target
(`public.sdk/source/vst/hosting/hostclasses.cpp`, `pluginterfacesupport.cpp`) for
`IParameterChanges` / `IEventList` stand-ins, and `MemoryStream`
(`public.sdk/source/common/memorystream.cpp`) for state.

FR-067: the fixture holds the processor via `unique_ptr`; the `static_assert(sizeof(Processor) < 64
KiB)` in `processor.h` is what licenses a stack local if one is ever wanted.

### 4.3 Assertion strategy, criterion by criterion

**SC-002 — cases present + suite green.**
`seraphis_tests.exe --list-tests` must contain all eight names; `seraphis_tests.exe 2>&1 | tail -5`
must end `All tests passed (…)`. Not a case **count** — six cases in one file would satisfy a count
while five required files are missing.

**SC-005 — held note is non-silent.** `Seraphis_ProcessorRendersHeldNote`: prepare 48 kHz / 512,
`setActive(true)`, NoteOn(60, 100) at sample 0, render 4 s (`4 * 48000 / 512 = 375` blocks).
`REQUIRE(peak >= 1.0e-3f)` and every sample finite via a **bit-pattern** check
(`(bits & 0x7F800000u) != 0x7F800000u`), never `std::isnan` — the macOS leg is `-ffast-math`.

**SC-006 — limiter ceiling, three clauses.** SECTION of the same case.

**Master gain is NOT the same for all three clauses, and the plan states which is which.** The
processor's chain carries step 4b — a per-sample master-gain multiply between the reverb return and
`processOutputStage` (§1.2, §2.5.6) — but `renderSeraphisChain` applies **no gain at all**:
`reverb.processStereoBlock` (`seraphis_chain.h:228`) feeds `engine.processOutputStage` (`:231`)
directly. At master gain 2.0 the processor's signal would enter the output stage at **twice** the
chain's level, through a nonlinearity (`processOutputStage` → `satL_.process`,
`seraphis_engine.h:512-522`; blend `0.85*x + 0.15*tanh(x)`, `tape_saturator.h:420-424`) and the
limiter. `compareFingerprints(...).withinTolerance()` demands `worstSampleError <= 1e-4` and worst
metric relative error `<= 1e-5` (`render_fingerprint.h:49, :52, :93-98`); a factor-2 drive difference
produces per-sample deltas of order `0.1` and a ~100 % peak-metric error, so **clause 2 would fail a
correct implementation** and clause 3 would pass for the wrong reason (the gain difference, not the
missing step 5), destroying exactly the discriminating power it exists for.

| Clause | Render(s) | `kMasterGainId` (normalized) | Linear gain |
|---|---|---|---|
| 1 | processor only | **1.0** | 2.0 |
| 2 | processor **vs** `renderSeraphisChain` | **0.5** | 1.0 |
| 3 | processor, chain-with-step-5, hand-rolled-without-step-5 | **0.5** | 1.0 |

At normalized `0.5` the denormalisation is `clamp(0.5 * 2.0, 0, 2) == 1.0f` **exactly** (§2.3), and
§3.1's `snapTo` on the first block after prepare makes the smoother output exactly `1.0f` from
sample 0, so step 4b is an exact `x * 1.0f` — an IEEE-754 identity. The clause-2 comparison is
therefore **bit-identical**, not merely within tolerance.

1. **Bound**, on the **gain-2.0** render: `REQUIRE(peak <= 0.8912509f * std::pow(10.0f, 0.1f/20.0f))`
   — the `TruePeakLimiter` ceiling (`true_peak_limiter.h:46, 168`) plus Phase 7 SC-015's 0.1 dB
   allowance. 16 voices, all notes held, 4 s (the spec's scenario).
2. **Positive control**, at **normalized 0.5**: render the same script/seed/blocksize through
   `Krate::DSP::TestUtils::renderSeraphisChain` (`seraphis_chain.h:147`) and compare with
   `compareFingerprints(fingerprintRender(a), fingerprintRender(b)).withinTolerance()`
   (`render_fingerprint.h:64, 101`). This is what proves the processor reproduces the chain,
   including step 5.
3. **Negative control with mandatory non-vacuity**, at **normalized 0.5**: a hand-rolled
   engine+reverb loop that **omits** step 5 (`renderSeraphisChain` has no skip flag — `:147-151`, no
   options struct) — and which, being at linear gain 1.0, needs no gain multiply to match the other
   two arms. Then, in order,
   `REQUIRE(maxAbsDiff(withStep5, withoutStep5) > kSampleTolerance)` **first**, then
   `REQUIRE(maxAbsDiff(processorRender, withoutStep5) > kSampleTolerance)`.
   Clause 1 alone has no discriminating power: Phase 7 measured the composed chain's worst case at
   peak `0.128337` (`specs/seraphis-phase7-voice-engine/compliance.md:181`), 16.8 dB below the
   ceiling, so even at master gain 2.0 a processor that dropped step 5 passes it. If the non-vacuity
   assertion fails, **escalate the scenario** (more voices, higher pre-output level) until the output
   stage is measurable and record the level in `compliance.md` — do not drop the clause.
   *(Alternative, if a clause-3 arm at gain 2.0 is ever wanted: give the hand-rolled loop the same
   per-sample `masterGain_.process()` multiply between step 4 and step 5. Do **not** compare arms
   that differ in pre-output-stage drive.)*

**SC-007 — zero allocations.** `Seraphis_ProcessorLifecycle` SECTION
`Seraphis_ProcessorNoAllocInProcess`: after `setupProcessing` + `setActive(true)`, wrap 200 × 512
blocks with NoteOn/NoteOff traffic and a parameter sweep in `TestHelpers::AllocationScope`
(`allocation_detector.h:75`).

**The reading form is normative, because the obvious one measures nothing.**
`AllocationScope::getAllocationCount()` returns the member `count_` (`allocation_detector.h:85-87`),
and `count_` is assigned **only** in `~AllocationScope()` (`:81-83`). While the scope is alive it is
always `0`, so `REQUIRE(scope.getAllocationCount() == 0)` written *inside* the scope passes
unconditionally — and that is exactly the form the in-repo model uses
(`plugins/membrum/tests/unit/test_allocation_matrix.cpp:129-135`). Written that way, SC-007's
"exactly 0 allocations", SC-026's "`setActive(true)` performs exactly 0 allocations" **and** the
liveness probe are all vacuous, and the probe would fail the phase gate for the wrong reason.

**Normative form — read the singleton while the scope is open** (`allocation_detector.h:45-47`,
which returns the live atomic):

```cpp
std::size_t allocations = 0;
{
    TestHelpers::AllocationScope scope;                        // :76-78 startTracking()
    fixture.renderBlocks(200, 512, script);
    allocations = TestHelpers::AllocationDetector::instance().getAllocationCount();  // :45-47
}
REQUIRE(allocations == 0u);
```

**Liveness probe, identical form, in a SEPARATE (never nested) scope** — a nested
`AllocationScope` ctor calls `startTracking()`, which *resets* the outer count (`:31-34`), and its
dtor calls `stopTracking()`, which switches tracking off for the outer scope too (`:37-40`):

```cpp
std::size_t probe = 0;
{
    TestHelpers::AllocationScope scope;
    auto* deliberate = new int(7);
    probe = TestHelpers::AllocationDetector::instance().getAllocationCount();
    delete deliberate;
}
REQUIRE(probe >= 1u);
```

Reading `scope.getAllocationCount()` **after** the scope closes (from a nested block) is the only
acceptable alternative; the same form must be used in both places. The probe can only pass because
`test_main.cpp` includes `<allocation_operator_overrides.h>` (FR-066a) — `recordAllocation()`
(`allocation_detector.h:53-57`) is called *only* from those global replacements. Exactly one TU per
binary may include it (duplicate-symbol link error otherwise); a hand-rolled copy is caught by
`tools/lint-allocation-operator-overrides.js`.

**SC-008 — block-size invariance.** SECTION of `Seraphis_MidiEventTranslation`. Render one 4 s
seeded script through `Processor::process()` at host block sizes **{1, 7, 64, 65, 512, 2048, 4096}**,
events placed at non-multiples of every partition, and compare each of {1, 7, 64, 65, 2048, 4096}
against the **512-sample reference** (the reference is not compared with itself).
- **Primary gate:** `REQUIRE(maxAbsDiff_L <= 1.0e-5f)` and `REQUIRE(maxAbsDiff_R <= 1.0e-5f)`, the
  shape Phase 7 shipped (`specs/seraphis-phase7-voice-engine/compliance.md:180`).
- **Secondary, WARN-only:** `compareFingerprints(...)`; **must not gate**. Phase 7 rejected it as the
  gate in both directions — it samples only 32 checkpoints (`render_fingerprint.h:46`) so it can miss
  a localised divergence, and its `kMetricTolerance = 1e-5` relative bound on `totalVariation`
  (`:52`) was measured for cross-toolchain spread of the *same* computation, not a re-partitioned one.
- **Required coverage, asserted:** block **65** guarantees a partition boundary strictly inside a
  64-sample control chunk (`kControlChunkSamples = 64`, `seraphis_engine.h:132`); the test asserts
  `65 % 64 != 0` and that 65 is in the set, so the coverage is checked rather than assumed.
- **4096 is mandatory:** the only partition above `kMaxBlockSamples = 2048` and therefore the only
  one entering FR-026's sub-division branch.
- The script contains **no parameter automation**, because VST3 delivers parameter queues per host
  block and a re-partitioned automation lane is a *different* automation lane by construction.

**SC-009 — denormalisation round-trip.** `Seraphis_ParamDenormRoundTrip`: for each of the eight IDs,
push `{0, 0.25, 0.5, 0.75, 1}` through `processParameterChanges` and read the atomic.
`masterGain` within `1e-6f` of `v*2`; `polyphony` **exactly** `clamp(int(v*15+1.5), 1, 16)`;
`softLimit` exactly `v >= 0.5`; each macro within `1e-6f` of `v`.
**Last-point clause (FR-042), mandatory:** with one-point queues an implementation that reads
`queue->getPoint(0, …)` is indistinguishable from one that reads `getPointCount()-1`, so the
"*using the last value of each parameter queue*" half of FR-042 has no detector — and no other
criterion covers it (SC-008 deliberately contains no parameter automation). Using
`setParamPoints` (§4.2), push a **3-point** queue with distinct values for at least one global ID
(`kPolyphonyId`, e.g. `{0.0, 1.0, 0.4}` → 1, 16, **7** voices) and one macro ID
(`kMacroDreamId`, e.g. `{0.0, 1.0, 0.25}`), run one `processParameterChanges`, and
REQUIRE the atomic holds the denormalized value of the **last** point in each case. Distinct values
in all three points, and a last value that is neither the first nor the maximum, so neither
`getPoint(0)` nor a max-scan can pass.
**Controller clause:** `getParamNormalized(id)` after `setParamNormalized(id, v)` returns `v`
**exactly, including `kPolyphonyId`** — `Parameter::setNormalized`
(`extern/vst3sdk/public.sdk/source/vst/vstparameters.cpp`) only clamps to `[0,1]`, and
`StringListParameter` overrides `toString`/`fromString`/`toPlain`/`toNormalized` but **not**
`setNormalized`. A criterion demanding "nearest step" here would fail a correct implementation. The
quantisation is verified separately on the surfaces that *do* quantise:
`StringListParameter::toPlain(v) == std::round(v * 15)` and the displayed string equals the voice
count.

**SC-010 — state round-trip byte-stable.** `Seraphis_StateRoundTrip`: set all eight to distinct
non-default values, `getState` → `MemoryStream` A, construct a **fresh** `Processor`, `setState(A)`,
`getState` → B. `REQUIRE(A == B)` byte-for-byte (36 bytes, §3.4); version prefix `== 1`; every
parameter reads back equal. Truncated streams (first N bytes for N ∈ {0, 4, 8, 12, 16, 20, 24, 28,
32}) must not crash and must leave later parameters at defaults.
**Default-state clause:** `getState()` on a fresh processor streams `gravity == 0.5f` at offset 28.

**Controller clause (FR-047), mandatory.** Without it FR-047 has **no** detector: SC-010's procedure
as written only exercises `Processor::getState`/`setState`, so a `Controller::setComponentState()`
that returns `kResultOk` without reading the stream — or that loads the two packs in the wrong order
— passes SC-009, SC-010 and pluginval, and `loadGlobalParamsToController` /
`loadMacroParamsToController` (§2.3, §2.4; required by FR-040/FR-041) are never called by any test.
Procedure:

1. set all **eight** parameters on the processor to distinct non-default values — including
   `kMacroGravityId` set **away from its 0.5 default** (e.g. normalized `0.8`), so a no-op loader
   that leaves the registered defaults in place is caught;
2. `Processor::getState(&stream)` into a `MemoryStream`
   (`public.sdk/source/common/memorystream.cpp`, already in §5.2's source list);
3. `stream.seek(0, IBStream::kIBSeekSet, nullptr)`;
4. construct a fresh `Seraphis::Controller`, `initialize(nullptr)`, then
   `REQUIRE(controller.setComponentState(&stream) == kResultOk)`;
5. for each of the eight IDs, `REQUIRE(controller.getParamNormalized(id) == Approx(expected))` with
   `expected` the *normalized* value pushed in step 1 — i.e. the inverse mappings in §2.3/§2.4
   (`masterGain / 2.0`, `(polyphony - 1) / 15.0`, `softLimit ? 1.0 : 0.0`, macros as-is).

Reachable from `seraphis_tests` because §5.2 compiles `../src/controller/controller.cpp` into the
test binary; `state_roundtrip_test.cpp` therefore includes `controller/controller.h` as well as
`processor/processor.h` (they do not include each other — the VST3 separation holds).

**SC-011 — bus shape.** After `initialize(nullptr)`:
`getBusCount(kAudio, kInput) == 0`, `getBusCount(kAudio, kOutput) == 1`,
`getBusCount(kEvent, kInput) == 1`; `setBusArrangements` returns `kResultFalse` for `numIns != 0`,
for **`numOuts == 0` and `numOuts == 2`** (FR-021 clause b — without it a host successfully
negotiates a bus that does not exist), and for a mono output; `kResultTrue` for one `kStereo`.

**SC-012 — editor lifecycle.** `Seraphis_EditorLifecycle` calls
`Krate::TestSupport::exerciseEditorLifecycle(controller, "editor", std::string(SERAPHIS_RESOURCES_DIR) + "/editor.uidesc")`
(`editor_lifecycle_harness.h:102-105`).
1. **Tag non-vacuity:** `seraphis_tests.exe "[lifecycle]"` selects ≥ 1 case (run as a shell check at
   phase completion, recorded in `compliance.md`).
2. **Release lane:** 3 cycles, no crash; the harness itself `CHECK`s `attached() == kResultTrue` and
   `REQUIRE`s `getFrame()->getNbViews() > 0` (`:122-128`).
2b. **Bound-control clause (FR-054), mandatory** — the harness cannot deliver this (§2.10): its three
   assertions are satisfied by a template holding one `CTextLabel`. In the **same** test case, a
   separate SECTION builds its own editor (the harness owns and destroys its instances internally),
   walks the view tree and asserts the bindings:

   ```cpp
   Krate::TestSupport::ensureVstguiInitialized();              // harness :64
   auto* editor = new VSTGUI::VST3Editor(&controller, "editor",
                       (std::string(SERAPHIS_RESOURCES_DIR) + "/editor.uidesc").c_str());
   Steinberg::IPlugView* view = editor;
   REQUIRE(view->attached(nullptr, Krate::TestSupport::nativePlatformType()) ==  // :87, NEVER a literal
           Steinberg::kResultTrue);

   std::vector<VSTGUI::CControl*> controls;                    // recursive collect
   collectControls(editor->getFrame(), controls);              // CViewContainer::getNbViews() :80,
                                                               // getView(i) :82, CControl::getTag() :65
   std::set<int32_t> tags;
   for (auto* c : controls) tags.insert(c->getTag());
   REQUIRE(controls.size() == 8u);
   REQUIRE(tags == std::set<int32_t>{0, 1, 2, 100, 101, 102, 103, 104});
   REQUIRE(dynamic_cast<VSTGUI::COptionMenu*>(controlWithTag(controls, 1)) != nullptr);   // FR-048 type match
   REQUIRE(dynamic_cast<VSTGUI::CCheckBox*>(controlWithTag(controls, 2))  != nullptr);
   view->removed();
   view->release();
   ```

   `collectControls` recurses through `CViewContainer` children (`cviewcontainer.h:80, :82`) and
   collects every `CControl` (`ccontrol.h:65`); it is local to the test file, not new shared
   infrastructure. This is the clause that makes §2.10's and R9's type-match claims true.
2c. **Preset-config clause (FR-050, FR-051), mandatory** — see §2.7: instantiating a `PresetManager`
   scans nothing, so without this clause FR-050/FR-051 have no detector at all. SECTION
   `Seraphis_PresetConfigIsLive`:

   ```cpp
   const auto cfg = Seraphis::makeSeraphisPresetConfig();
   REQUIRE(cfg.pluginName == "Seraphis");
   REQUIRE(cfg.subcategoryNames == std::vector<std::string>{"Textures"});
   REQUIRE(cfg.processorUID == Seraphis::kProcessorUID);

   const auto presetsDir = std::filesystem::path(SERAPHIS_RESOURCES_DIR) / "presets";
   REQUIRE(std::filesystem::is_directory(presetsDir / "Textures"));       // FR-051, both halves agree

   // Both overrides point INSIDE the repo so the scan never touches a real
   // install directory (preset_manager.cpp:480-485 honours the overrides).
   Krate::Plugins::PresetManager pm(cfg, nullptr, nullptr, presetsDir, presetsDir);
   REQUIRE(pm.scanPresets().empty());        // :37-56 -- the ONLY enumerating call; Phase 8 ships no .vstpreset
   REQUIRE(pm.getConfig().pluginName == "Seraphis");                      // preset_manager.h:132

   // A wrong pluginName would silently move the install directory. Linux
   // lowercases the leaf (platform/preset_paths.cpp:44-49), so compare folded.
   Krate::Plugins::PresetManager def(cfg, nullptr, nullptr);
   REQUIRE(toLower(def.getFactoryPresetDirectory().filename().string()) == "seraphis");
   ```
3. **Sanitizer gate at completion:** `cmake -S . -B build-asan -G "Visual Studio 17 2022" -A x64 -DENABLE_ASAN=ON`,
   build Debug, run `seraphis_tests`, require a clean exit with no ASan report. Release passes by
   luck; the harness only has teeth under a sanitizer.
   The valgrind nightly is the *ongoing regression surface*, not the phase gate — it runs nightly.

**SC-013 — latency.** `Seraphis_ProcessorLifecycle` SECTION `Seraphis_LatencyIsReported`:
1. `REQUIRE(proc->getLatencySamples() == 1024u)` — the **literal** number (an implementation that
   disabled spectral diffusion would satisfy "equals `AetherReverb::getLatencySamples()`" at 0);
2. `== 1024` at 44.1, 48, 96 **and** 192 kHz — a sample count does not scale with the rate;
3. stable across 100 `process()` calls and unchanged by any of the eight parameters;
4. **restated per §1.3 C-1/C-2 — invariance, not an announcement.** The spec's clause 4 ("the first
   prepare (0 → 1024) records exactly one `kLatencyChanged`") rests on two false premises: the
   reverb reports 1024 *before* any prepare (C-1), and a `Component` has no route to an
   `IComponentHandler` (C-2), so the only way to make that clause pass is a bespoke host context no
   real host produces — a test that cannot fail for the reason it exists. The replacement is
   **strictly more observable behaviour**, asserted on the real API in one sequence:
   `REQUIRE(getLatencySamples() == 1024u)` **(a)** on a freshly `initialize(nullptr)`d processor
   before any `setupProcessing()`; **(b)** after each of four consecutive `setupProcessing()` calls
   at 44.1 / 48 / 96 / 192 kHz **in that order** (so a stateful implementation is caught); **(c)**
   after `setActive(true)` and after `setActive(false)`; **(d)** after a `setState()` carrying
   non-default values; **(e)** after pushing each of the eight parameters to `0.0` and to `1.0`;
   **(f)** after 100 `process()` calls. No value other than `1024` may be observed at any point, and
   `initialize(nullptr)` followed immediately by `setupProcessing()` must not crash (§2.5.4's null
   guard). If a later phase makes latency variable, this clause is replaced by the controller-side
   `restartComponent(kLatencyChanged)` assertion described in §1.3 C-2 — not by restoring a
   host-context query.

**SC-014 — wrapper overhead, NON-GATING.** `Seraphis_ProcessorCpuOverhead`, tag `[.perf]` (hidden
from the default run, so SC-002 clause 2 is unaffected). Protocol, pinned:
identical 4 s scenario on both arms (polyphony 8, one held note, 512-sample blocks, 48 kHz, same
seed); **best-of-16 × 100 blocks** with a discarded warm-up (the trial shape at
`dsp/tests/unit/systems/seraphis_perf_test.cpp:163`); the two arms **interleaved in one process**;
and — critically — the chain arm's buffers **hoisted out of the timed region**, because
`renderSeraphisChain` allocates and zero-fills ~1.5 MB per call (`seraphis_chain.h:152-186`:
`outL.assign`/`outR.assign`, the `eventAt` vector, four `blockSize` vectors), all of which would be
charged to the denominator and bias the ratio in the wrapper's favour. Report `processor_ns /
chain_ns` in `compliance.md` with the machine-idle caveat; a ratio above ~1.15 is a **flag to
investigate**, not a failure — Phase 7 recorded ~33 % spread on this lane
(`specs/seraphis-phase7-voice-engine/compliance.md:268`), larger than the factor itself.

**SC-019 — the three global parameters reach the chain.** `Seraphis_ParamFlowReachesEngine`:
1. **Silences.** `kMasterGainId` normalized `0.0` ⇒ 4 s render peak `< 1e-6f` **from sample 0**,
   no "after the first N ms" allowance. Satisfiable only because of the snap (§3.1); a
   ramped-from-default implementation fails by design.
2. **Scales.** peak(norm 1.0) / peak(norm 0.5) = `2.0 ± 5 %`, measured at a level where the limiter
   does **not** engage (otherwise the ratio is compressed). If it engages, reduce the render level
   and record the level used.
3. **Polyphony.**
   - *Seeded at prepare:* `setState()` carrying polyphony 4, **then** `setupProcessing()` ⇒
     `engine_->getPolyphony() == 4` (`seraphis_engine.h:665`) **before any `process()`**.
   - *Tracked at prepare:* the first `process()` after that prepare leaves
     `setPolyphonyCallCountForTest()` unchanged.
   - *Pushed on change:* after pushing `kPolyphonyId = v` and one `process()`,
     `getPolyphony() == clamp(int(v*15+1.5), 1, 16)` for `v ∈ {0, 0.25, 0.5, 0.75, 1}`; re-pushing
     the **same** value leaves the call count unchanged.
   - *Corrupt stream converges (the `clampPolyphony` detector, §2.3/§2.5.7):* build a state stream
     by hand with polyphony `0`, and another with `20`, `setState` each, then run **two consecutive**
     `process()` calls and REQUIRE `setPolyphonyCallCountForTest()` increased by **at most 1** in
     total. Without clamping at the conversion point, the comparison against the engine's clamped
     `getPolyphony()` never converges and the count increments on **every** block forever — re-arming
     `sumGain_` (`seraphis_engine.h:349`) and walking the excess-slot loop (`:339-348`) per block.
     Also REQUIRE `getPolyphony() ∈ [1, 16]` after each.

**SC-021 — degenerate shapes.** `Seraphis_ProcessorLifecycle` SECTION: each of `numInputs == 0`,
`numOutputs == 0`, `numSamples == 0`, `channelBuffers32 == nullptr` returns `kResultOk`, writes no
sample outside the provided buffers (guard-word canaries either side of the output buffers), and
leaves `engine_->getActiveVoiceCount()` (`seraphis_engine.h:668`) unchanged.

**Pre-`setupProcessing()` clause, with teeth.** `process()` before `setupProcessing()` returns
`kResultOk` and **produces silence** — asserted with the output buffers **pre-seeded to a non-zero
canary** (`fixture.seedOutputBuffers(0.5f)`, §4.2). Without the pre-seed the assertion passes because
the fixture zeroed its own vectors, and an implementation that returned without writing — handing the
host back the previous plug-in's or previous block's content, which VST3 does not zero — would go
undetected. §2.5.6's reordered guards (validate buffers → `fill_n(0.0f)` → return) are what satisfy
it; both wrapped components behave the same way (`seraphis_engine.h:448-451`,
`aether_reverb.h:2172-2176`).

**Mono-output clause (new; §2.5.3, §2.5.6).** A `ProcessData` with
`data.outputs[0].numChannels == 1` — legal for a host that ignores `setBusArrangements`' rejection —
returns `kResultOk`, never touches `channelBuffers32[1]`, and leaves `getActiveVoiceCount()`
unchanged. Build the `ProcessData` with a **one-element** `channelBuffers32` array (fixture's
`withOutputChannels(1)`) so an out-of-bounds read is a real out-of-bounds read, and run the SECTION
in the ASan lane (SC-012 clause 3) where it becomes a hard failure. Model:
`plugins/ruinae/src/processor/processor.cpp:430`.

**Silence-flag clause, both directions.** A normal render leaves `data.outputs[0].silenceFlags == 0`,
asserted from a **pre-seeded `3`**; the not-ready path (before `setupProcessing()`) leaves
`silenceFlags == 3`, asserted from a **pre-seeded `0`**. Neither can pass by the host happening to
leave the field alone. On the early-outs that return before buffer validation, no read of
`data.outputs[0]` may occur when `numOutputs == 0`, and no write when `channelBuffers32 == nullptr`
caused the early-out — hence the ordering of the guards in §2.5.6.

**Out-of-order lifecycle clause (§2.5.4).** `setupProcessing()` called on a processor whose
`initialize()` never ran (or after `terminate()`) returns without crashing and leaves
`getLatencySamples() == 0` (no `reverb_` to report from), and a subsequent `process()` with valid
buffers returns `kResultOk` and produces silence. This is the surface pluginval strictness 5 probes.

**SC-022 — MIDI translation.** On the engine's own observable surface (`getActiveVoiceCount()`
`:668`, `getVoiceState(i)` `:693`): (1) NoteOn vel>0 allocates exactly one voice; (2) NoteOn vel==0
releases identically to NoteOff (compare the resulting `VoiceState`); (3) NoteOff for an unplayed
note is a no-op; (4) `sampleOffset < 0` and `>= numSamples` are clamped into `[0, numSamples]` and
never produce a negative slice; (5) two events at the same offset are both dispatched before the
next render; **(6, per §1.3 C-3)** a `kNoteOnEvent` with velocity strictly inside `(0, 1/127)` —
e.g. `0.003f` — **allocates** a voice (`getActiveVoiceCount()` increments) rather than releasing one.
A truncating `uint8(velocity*127)` yields `0`, which `SeraphisEngine::noteOn` maps to `noteOff`
(`seraphis_engine.h:374-377`); the `+ 0.5` rounding and the floor of 1 in §3.2 are what this clause
detects. Assert the same for `velocity = 1.0f/127.0f` (maps to 1) and `velocity = 1.0f` (maps to 127,
not 128 — the upper clamp).

**SC-023 — macros inert (Phase 9 negative control).** SECTION of
`Seraphis_ProcessorRendersHeldNote`: a 4 s render with all five macros at `0.0` and one with all five
at `1.0`, same seed and script, are **fingerprint-identical** on both channels
(`compareFingerprints(...).withinTolerance()`), with a non-vacuity `REQUIRE(rms > 1e-4)` on the first
so the comparison is not between two silences. **Phase 9 must invert this test.**

**SC-024 — the eight Aether targets are pushed.** SECTION exercising `applyAetherTargets()` directly
with **non-neutral** values (a render diff at Phase 8's neutral defaults is provably vacuous — the
eight `computeAetherTargets()` values equal the reverb's own ctor defaults exactly, and the reverb
exposes no getter for any of them):
1. `size`: `applyAetherTargets` with `size = 0.9` vs `0.1`, then enough blocks for the 300 ms size
   smoother to settle ⇒ `getEffectiveDelayLengthSamples(0)` (`aether_reverb.h:2506`) differs;
2. `mix`: a render with `mix = 1.0` differs from `mix = 0.0` by more than `kSampleTolerance`
   (max abs per-sample), with the same non-vacuity guard;
3. that `process()` calls it **every slice** is carried by SC-006 clause 2's positive control, whose
   reference render pushes the same eight values per slice — which is sound **only** because that
   clause now runs at normalized master gain `0.5` (linear `1.0`), so the processor and
   `renderSeraphisChain` present identical drive to the output stage. At the spec's gain of 2.0 the
   comparison would fail a correct implementation and this clause would inherit the failure; see the
   render table under SC-006.

**SC-026 — `setActive` lifecycle.** SECTION of `Seraphis_ProcessorLifecycle`: with a note held and
the reverb ringing, `setActive(false)` → `setActive(true)` → one 512-sample `process()` yields peak
`< 1e-6f`; and `setActive(true)` performs **exactly 0** allocations, measured with **SC-007's
normative reading form** — `TestHelpers::AllocationDetector::instance().getAllocationCount()`
(`allocation_detector.h:45-47`) read while the scope is open, never
`scope.getAllocationCount()` inside the scope (`:81-87`: that member is only assigned by the
destructor and is always `0` while the scope lives, making the assertion vacuous). The liveness probe
runs in its own separate, non-nested scope, in the identical form.

**SC-027 — soft limit is measurable.** SECTION of `Seraphis_ParamFlowReachesEngine`: same seeded
script rendered twice, `kSoftLimitId` on (`setOutputSaturation(0.15f)`) vs off (`0.0f`), at a level
where the saturator is engaged.
1. **non-vacuity first:** `REQUIRE(maxAbsDiff(on, off) > kSampleTolerance)` (`1e-4f`,
   `render_fingerprint.h:49`);
2. relative RMS difference exceeds a figure **measured during implementation** and written into the
   test as a named constant with a provenance comment (project rule: measured, never guessed).
The pre-output-stage level used MUST be stated in the test and recorded in `compliance.md`. Phase 7
measured the composed chain at ~−30 dBFS RMS
(`specs/seraphis-phase7-voice-engine/compliance.md:181`), where a 0.15 tape-saturation amount is
**not** obviously above `kSampleTolerance` — drive the level up (more voices / higher master gain)
until clause 1 passes. If no reachable level produces a measurable difference, record FR-044 in
`compliance.md` as **verified by code inspection only**, listing the measured deltas at each level
tried. Never silently mark it verified.

### 4.4 Portability rules for every test TU

- `-fno-fast-math -fno-finite-math-only` source properties (`Clang|GNU` only) on any TU that injects
  NaN/Inf — the model at `plugins/ruinae/tests/CMakeLists.txt:200-233`. Expected members of the list:
  `unit/lifecycle_test.cpp`, `integration/processor_audio_test.cpp`, `integration/param_flow_test.cpp`.
- Never `std::isnan` / `std::isinf`; build non-finite inputs from bit patterns through a `volatile`
  sink and test finiteness with `(bits & 0x7F800000u) != 0x7F800000u`.
- Never a platform-type constant literal; call
  `Krate::TestSupport::nativePlatformType()` (`editor_lifecycle_harness.h:87`) —
  `tools/lint-platform-type-literals.js` enforces this.
- Never a bit-exact float render digest (FR-068); `tools/lint-float-bit-goldens.js` and
  `tools/lint-midi-timing-goldens.js` must stay clean.
- Anything initialised from an SDK constant is `const`, not `constexpr`.

---

## 5. Build integration

### 5.1 `plugins/seraphis/CMakeLists.txt` (FR-001…FR-006)

Ruinae's file (`plugins/ruinae/CMakeLists.txt`) with the source list reduced to Phase 8's:

```cmake
krate_plugin_read_version(SERAPHIS)                 # KratePlugin.cmake:35
krate_plugin_configure_generated_files()            # :80  -> src/version.h, win32resource.rc,
                                                    #        resources/auv3/audiounitconfig.h
set(PLUGIN_NAME "${SERAPHIS_NAME}")

smtg_add_vst3plugin(${PLUGIN_NAME}
    src/entry.cpp
    src/plugin_ids.h
    src/version.h
    src/processor/processor.h        src/processor/processor.cpp
    src/controller/controller.h      src/controller/controller.cpp
    src/parameters/global_params.h   src/parameters/macro_params.h
    src/engine/seraphis_engine_config.h
    src/preset/seraphis_preset_config.h
    src/update/seraphis_update_config.h
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/common/memorystream.cpp   # PresetManager saving
)

target_link_libraries(${PLUGIN_NAME} PRIVATE sdk vstgui_support KrateDSP KratePluginsShared)
target_include_directories(${PLUGIN_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

smtg_target_configure_version_file(${PLUGIN_NAME})
krate_plugin_platform_setup(${PLUGIN_NAME}
    TAG SERAPHIS BUNDLE_BASE com.krateaudio.seraphis
    ENTITLEMENTS Seraphis.entitlements KIND instrument)        # KIND validated at :130-132
smtg_target_add_plugin_resources(${PLUGIN_NAME} RESOURCES resources/editor.uidesc)
krate_plugin_install_to_system(${PLUGIN_NAME})                 # :255
krate_plugin_install_presets(${PLUGIN_NAME})                   # :287, NO SRC_SUBDIR/DEST_SUBDIR
krate_plugin_set_warnings(${PLUGIN_NAME})                      # :319

if(VSTWORK_BUILD_TESTS)
    add_subdirectory(tests)
endif()
```

`KIND instrument` is mandatory: `KratePlugin.cmake:130-132` hard-errors on anything else and
`:197-206` selects the instrument AUv3 storyboard from it.

### 5.2 `plugins/seraphis/tests/CMakeLists.txt` (FR-060…FR-065)

```cmake
add_executable(seraphis_tests
    unit/test_main.cpp
    unit/processor_bus_test.cpp
    unit/param_denorm_test.cpp
    unit/state_roundtrip_test.cpp
    unit/midi_event_test.cpp
    unit/lifecycle_test.cpp
    unit/controller/editor_lifecycle_test.cpp
    integration/processor_audio_test.cpp
    integration/param_flow_test.cpp

    # SECOND compilation of every plugin .cpp (ruinae tests/CMakeLists.txt:139-152)
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/processor/processor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/controller/controller.cpp

    # SDK sources (:153-162)
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/common/memorystream.cpp
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/hosting/hostclasses.cpp
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/hosting/pluginterfacesupport.cpp
    vstgui_test_stubs.cpp
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/main/moduleinit.cpp
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/main/pluginfactory.cpp
)

target_link_libraries(seraphis_tests PRIVATE
    KrateDSP KratePluginsShared Catch2::Catch2 test_helpers vstgui_support sdk)   # order per :165-173

target_include_directories(seraphis_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/tests            # test_helpers/… and the flat <…_harness.h> form
    ${CMAKE_SOURCE_DIR}/tools
    ${vst3sdk_SOURCE_DIR}
    ${vst3sdk_SOURCE_DIR}/vstgui4)

target_compile_features(seraphis_tests PRIVATE cxx_std_20)
target_compile_definitions(seraphis_tests PRIVATE
    SERAPHIS_RESOURCES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../resources")            # FR-063

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    set_source_files_properties(
        unit/lifecycle_test.cpp
        integration/processor_audio_test.cpp
        integration/param_flow_test.cpp
        PROPERTIES COMPILE_FLAGS "-fno-fast-math -fno-finite-math-only")          # FR-064
endif()

catch_discover_tests(seraphis_tests REPORTER console)                             # FR-065
```

`tests/vstgui_test_stubs.cpp` (FR-061): `Steinberg::IPluginFactory* PLUGIN_API GetPluginFactory() { return nullptr; }`.
`tests/unit/test_main.cpp` (FR-061, FR-066a): `void* moduleHandle = nullptr;`, `#include <allocation_operator_overrides.h>`
(**exactly this one TU**), `enableFTZDAZ();`, `return Catch::Session().run(argc, argv);`.

`sdk` **after** `vstgui_support` — the stub file's own comment records why.

### 5.3 Root `CMakeLists.txt` (FR-070)

`add_subdirectory(plugins/seraphis)` appended to the six entries at `CMakeLists.txt:488-493`.

### 5.4 `.gitignore` (FR-007a, SC-020)

Three lines appended to the existing per-plugin trio block at `.gitignore:58-75`:

```
/plugins/seraphis/resources/win32resource.rc
/plugins/seraphis/src/version.h
/plugins/seraphis/resources/auv3/audiounitconfig.h
```

Without them the first `cmake --preset` leaves three generated files untracked-and-committable, and a
later version bump dirties them — breaking the rule that a bump touches `version.json` +
`CHANGELOG.md` only.

### 5.5 External rosters — the site table (FR-071…FR-077)

Every one of these is an **independent CI failure surface** with **no build-time detection**.
Line numbers verified this session.

| # | File | Site(s) | Edit |
|---|---|---|---|
| 1 | `.github/workflows/ci.yml` | `detect-changes` outputs (`:59` is `membrum:`) | add `seraphis: ${{ steps.set-outputs.outputs.seraphis }}` |
| 2 | " | `paths-filter` block (`:85-86`) | add `seraphis:` → `'plugins/seraphis/**'` |
| 3 | " | `for p in iterum disrumpo ruinae innexus gradus membrum` (`:104`) | append `seraphis` |
| 4 | " | `$GITHUB_OUTPUT` echo block (`:120` is membrum) | add the `seraphis=` echo |
| 5 | " | three FetchContent `hashFiles(...)` lists — Windows `:234`, macOS `:466`, Linux `:858` | add `'plugins/seraphis/CMakeLists.txt'` to each |
| 6 | " | build matrices `:266 / :506 / :892` + `case` dispatch `:274 / :514 / :900` | add `"seraphis:Seraphis:seraphis_tests"` + `seraphis) CHANGED=…` |
| 7 | " | test matrices `:315 / :561 / :938` + `case` `:323 / :569 / :946` | add `"seraphis:seraphis_tests.exe"` (`.exe` only on the Windows leg) + case |
| 8 | " | bundle-validate lists `:364 / :610 / :987` + `case` `:372 / :618 / :995` | add `"seraphis:Seraphis"` + case |
| 9 | " | artifact upload — Windows `:430-435`, macOS `:811-819`, Linux `:1058-1063` | add a `Seraphis-<OS>-x64` step with the same `if:` condition shape |
| 10 | " | macOS `auval` step (model `:691-697`) | `auval -v aumu Srph KrAt` |
| 11 | " | macOS AUv3 verify step (model `:743-749`) | `"$APP" = build/bin/$BUILD_TYPE/Seraphis AUv3.app` |
| 12 | `.github/workflows/release.yml` | `workflow_dispatch` choice list (`:40`) | add `- seraphis` |
| 13 | " | FetchContent `hashFiles(...)` (`:137`) | add `'plugins/seraphis/CMakeLists.txt'` **and** `'plugins/membrum/CMakeLists.txt'` (pre-existing drift — see §5.7's enumerated set). |
| 14 | `.github/workflows/valgrind-nightly.yml` | build target list (`:276`) and run list (`:283`) | add `seraphis_tests` to both. The Membrum-specific sharded job (`:123-192`) is **not** duplicated. |
| 15 | `tools/run-clang-tidy.ps1` | `ValidateSet` (`:60`), per-plugin `case` (model `:190-193`), `all` case (`:203-212`) | add `"seraphis"` + `plugins/seraphis/src` to source **and** include dirs |
| 16 | `tools/run-clang-tidy.sh` | `seraphis)` case (model `:148-149`), `all` (`:161-163`), usage text (`:63`) | `SOURCE_DIRS=("plugins/seraphis/src" "plugins/seraphis/tests")` |
| 17 | `tools/check-changelog-coverage.js` | `PLUGINS` array (`:50`) | add `'seraphis'` |
| 18 | `tools/gen-specs-index.js` | `SUBSYSTEMS` (`:19-33`) | add `['seraphis', 'Seraphis']` **before** `spectral`, `grain`, `filter`, `oscillat`, `dsp` — `:18` says *"first keyword found in the spec's slug wins; order matters"*, and without it `seraphis-phase3-spectral-morph` classifies as "DSP / Spectral" and `seraphis-phase1-life-modulators` as "Other" |
| 19 | Root `CLAUDE.md` | roster prose, pluginval table, build/test target table, clang-tidy target list, Quick Reference rows | add Seraphis everywhere the six existing plugins appear |
| 20 | `.github/workflows/docs.yml` | — | **no edit** (FR-080). Its per-plugin loop (`:41`) globs `plugins/*/` and reports `has_page: false` harmlessly; the root landing page (`:133-158`) skips a plugin with no `seraphis/v*` tag (`:141-148`), which first exists in Phase 12. Recorded, not changed. |

### 5.6 Regenerated artifacts (FR-078)

`specs/_architecture_/repo-map.json` — `tools/gen-repo-map.js:22-27` auto-discovers `plugins/`
(every directory except `shared`), so it changes as soon as `plugins/seraphis/` exists, and
`:29-37` picks up `seraphis_tests` from `plugins/seraphis/tests/CMakeLists.txt`'s `add_executable`.
`specs/INDEX.md` — `tools/gen-specs-index.js` (site #18 changes the classification of the eight
existing Seraphis specs too).
`specs/_architecture_/symbols.json` — **unaffected**; `tools/gen-symbols.js:19-21` scans only
`dsp/include`.

### 5.7 `tools/lint-plugin-roster.js` (FR-081, SC-025) — new

Node-only (project rule: helper scripts are Node, never Python). Registered as a ninth entry in
`tools/hooks/guard-ci-gates.js`'s `LINTS` array (currently **eight**, `:38-47`).

**Design.** Enumerate `fs.readdirSync('plugins', {withFileTypes:true})`, keep directories, drop
`shared` — the same discovery `gen-repo-map.js:22-27` uses, so the roster is derived from the
filesystem and cannot itself go stale. For each plugin `p` with `Cap = p[0].toUpperCase()+p.slice(1)`,
assert **presence of a required token in a required region** of each roster file:

| Roster | Required token | Region |
|---|---|---|
| `CMakeLists.txt` | `add_subdirectory(plugins/${p})` | whole file |
| `ci.yml` | `${p}:` in the `detect-changes.outputs` block; `${p}:` in the `filters:` block; `\b${p}\b` in the `for p in …` line; `${p}=` in the `$GITHUB_OUTPUT` block; `plugins/${p}/CMakeLists.txt` in **each** of the three `hashFiles(` lists; `"${p}:` in **each** of the nine `for plugin_info in \` blocks (`:260, :309, :362, :500, :555, :608, :886, :932, :985`) **and** `${p})` in the `case` block that follows each | per-block, located by anchor regex |
| `release.yml` | `- ${p}` in the `options:` list; `plugins/${p}/CMakeLists.txt` in the `hashFiles(` list | whole file |
| `valgrind-nightly.yml` | `${p}_tests` in the build-target line **and** in the `for bin in …` line | whole file |
| `run-clang-tidy.ps1` | `"${p}"` in `ValidateSet`; a `"${p}" {` case; `plugins/${p}/src` in the `"all"` case | per-block |
| `run-clang-tidy.sh` | `${p})` case; `plugins/${p}/src` in the `all)` case; `${p}` in the usage text | per-block |
| `check-changelog-coverage.js` | `'${p}'` in the `PLUGINS` array | array literal |

Exit 0 on full coverage; exit **1** with one line per missing `(plugin, file, site)` otherwise.

**Why this is a requirement, not a nicety.** Every roster is a static literal and a missing entry
produces a **green** run: `ci.yml:309-315` builds its test list from a literal
`for plugin_info in …` with a matching `case`, and the bundle-validation loop
(`:362-373`) additionally does `[ -d "$b" ] && BUNDLES+=("$b")` (`:375-376`), silently dropping even
a listed-but-unbuilt bundle. A Seraphis entry missing from any of them yields a passing CI in which
Seraphis is never tested or validated. **This lint is the only artefact that can fail on
FR-070…FR-077.**

**SC-025 liveness probe (mandatory).** After the lint passes, temporarily delete `'seraphis'` from
`tools/check-changelog-coverage.js`'s `PLUGINS`, re-run, require a **non-zero** exit, then restore.
A lint that cannot be shown to fail is not evidence.

**Pre-existing drift the lint WILL flag — enumerated, verified this session, four sites (not two).**
Under §5.7's own rule (`plugins/${p}/CMakeLists.txt` in **each** of the three `hashFiles(` lists):

| Plugin | File:line | Missing from |
|---|---|---|
| `disrumpo` | `.github/workflows/ci.yml:234` | Windows FetchContent cache key |
| `disrumpo` | `.github/workflows/ci.yml:466` | macOS FetchContent cache key |
| `disrumpo` | `.github/workflows/ci.yml:858` | Linux FetchContent cache key |
| `membrum` | `.github/workflows/release.yml:137` | release FetchContent cache key |

(All three `ci.yml` lists name shared, iterum, ruinae, innexus, gradus, membrum — but **not**
disrumpo. `release.yml:137` names iterum, disrumpo, ruinae, innexus, gradus — but **not** membrum.)

**S8 fixes all four in the same commit.** Each is a one-line addition to a cache key, affecting only
cache granularity — zero build risk — and leaving them turns S8's own gate ("`node
tools/lint-plugin-roster.js` exits 0") red for reasons unrelated to Seraphis. The "or add a dated
allow-list entry" branch is **closed**: there is nothing here that warrants an exception.
**Do not weaken the lint to make it pass.**

### 5.8 Targets to build and run

```bash
CMAKE="C:/Program Files/CMake/bin/cmake.exe"
"$CMAKE" --preset windows-x64-release                                   # picks up the new subdir
"$CMAKE" --build build/windows-x64-release --config Release --target Seraphis        2>&1 | tee /tmp/seraphis-build.log
"$CMAKE" --build build/windows-x64-release --config Release --target seraphis_tests  2>&1 | tee -a /tmp/seraphis-build.log
build/windows-x64-release/bin/Release/seraphis_tests.exe 2>&1 | tail -5
tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Seraphis.vst3"
node tools/check-bundle.js build/windows-x64-release/VST3/Release/Seraphis.vst3
node tools/check-portability.js
node tools/gen-repo-map.js --check; node tools/gen-specs-index.js --check; node tools/gen-symbols.js --check
node tools/lint-plugin-roster.js
./tools/run-clang-tidy.ps1 -Target seraphis -BuildDir build/windows-ninja 2>&1 | tee /tmp/seraphis-tidy.log
git status --porcelain plugins/seraphis      # SC-020: must be EMPTY
```

**SC-004 (`auval`) is NOT in that list and cannot be.** `auval -v aumu Srph KrAt` runs only on macOS,
so the sole measurement surface is the macOS CI leg's `auval` step (roster site #10, §5.5). It is a
**gate, not a formality**: S6 does not close until that step is green on a pushed branch, and
`compliance.md` records the CI run URL plus the copied `AU VALIDATION SUCCEEDED` line. A `-10875`
there means the FR-015/FR-016/FR-020 triple disagrees (R11). Do not fill SC-004's compliance row from
a clean local pluginval run — pluginval validates the VST3, never the AU.

**No other test target is rebuilt.** Phase 8 touches no `dsp/` file and no other plugin, so
`dsp_*_tests`, `shared_tests` and the five sibling plugin suites are out of scope (project rule:
scope regressions to the targets that consume the changed files).

**Capture every slow command to a log on its FIRST run** and inspect the log afterwards; never
re-run a build/tidy/pluginval just to look at output.

SC-001's zero-warning half needs its own measurement, because `krate_plugin_set_warnings`
(`KratePlugin.cmake:319-339`) sets `/W4 /permissive- /Zc:__cplusplus /wd4100 /wd4458` (MSVC) and
`-Wall -Wextra -Wpedantic -Wno-unused-parameter` (GCC/Clang) but adds **no** `/WX` or `-Werror`:

```bash
grep -E "plugins[/\\]seraphis" /tmp/seraphis-build.log | grep -cE "warning C|warning:"   # must be 0
```

---

## 6. Risks and mitigations

| # | Risk | Why it bites | Mitigation |
|---|---|---|---|
| R1 | **A roster entry is missed and CI goes green with Seraphis untested.** | Every roster is a static literal; `ci.yml:375-376` even drops a listed-but-unbuilt bundle silently. | FR-081's `lint-plugin-roster.js` + SC-025's deliberate-omission probe. This is the single highest-value artefact in the phase. |
| R2 | **A green Windows build proves nothing about GCC/Clang.** | MSVC accepts what GCC/Clang reject; the macOS leg is `-ffast-math`. | `node tools/check-portability.js` before every commit (SC-015); no `std::isnan`/`isinf` in tests (FR-064); `const` not `constexpr` for SDK-derived constants; WSL probe (g++ 13) for any Linux-behavioural doubt. |
| R3 | **Master gain applied per *slice* instead of per *sample*.** | Makes the render partition-dependent by construction; SC-008 then fails a *correct* implementation and the bug looks like a DSP defect. | §3.1 pins `smoother.process()` once per output sample inside `renderSlice`'s 4b loop; SC-008's {1, 7, 65} partitions catch a per-slice ramp immediately. |
| R4 | **Master gain applied after `processOutputStage`.** | At gain 2.0 the peak reaches ~1.78 and SC-006 clause 1 becomes unsatisfiable — the limiter is inside the output stage with no bypass (`seraphis_engine.h:521`). | Step order fixed in §1.2 / §2.5.6; SC-006 clause 1 is the detector. |
| R5 | **`SeraphisEngine` accidentally becomes a by-value member or a test stack local.** | 771 968 B against MSVC's 1 MiB default main-thread stack (`seraphis_engine.h:119-122, 159-164`). | `unique_ptr` members (FR-022); `static_assert(sizeof(Processor) < 64 KiB)` (FR-067); fixture heap-allocates. |
| R6 | **Latency announcement is unimplementable as specified — and the obvious workaround is dead code that still passes its test.** | The processor has no `componentHandler`; `getHostContext()` returns the host application, which no host is obliged to make queryable to `IComponentHandler`, so the query is null everywhere except a bespoke test stub. The reverb already reports 1024 before prepare and never changes it. | §1.3 C-1/C-2: the announcement is **deleted**, not faked; `getLatencySamples()` (the reporting half) stays. SC-013 clause 4 restated in §4.3 as an invariance assertion over `{pre-prepare, 4 sample rates, setActive, setState, 8 parameters, 100 process calls}` — measurable on the real API. If latency ever becomes variable, add it back via processor → `IMessage` → controller → `getComponentHandler()->restartComponent` (`vsteditcontroller.h:97`), never via the host context. |
| R7 | **`AllocationScope` counts nothing and SC-007 passes vacuously.** | `recordAllocation()` (`allocation_detector.h:53-57`) fires only from the global replacements in `allocation_operator_overrides.h`. | FR-066a: exactly one TU (`unit/test_main.cpp`) includes it; SC-007/SC-026 carry a liveness probe; `tools/lint-allocation-operator-overrides.js` catches a hand-rolled copy. |
| R8 | **The placeholder `.uidesc` references a custom view class.** | On a leg where the registration TU is not linked, `VST3Editor::open()` drops the view silently and SC-012 passes vacuously. | §2.10 restricts to `CViewContainer`/`CSlider`/`COptionMenu`/`CCheckBox`/`CTextLabel`, all bitmap-free and already exercised repo-wide; `getNbViews() > 0` is `REQUIRE`d by the harness (`:128`). |
| R9 | **`COptionMenu` bound to a non-`StringListParameter` (or the reverse).** | FR-048 freezes parameter *types* for the life of the plugin; a later swap at the same ParamID can break editor load in DAWs that cache param metadata. `VST3Editor` binds a mismatched control with **no error path**, and the lifecycle harness asserts only `attached()`, `getFrame() != nullptr` and `getNbViews() > 0` (`editor_lifecycle_harness.h:120-128`) — a one-label template satisfies all three. | **Not** the eight bindings by themselves — they prove nothing without an assertion. SC-012 **clause 2b** (§4.3) walks the attached frame's view tree and requires exactly eight `CControl`s with tags `{0,1,2,100..104}`, `dynamic_cast<COptionMenu*>` non-null for tag 1 and `CCheckBox` for tag 2. Plus §2.1's frozen-type table. |
| R10 | **Denormal storm in the reverb tail.** | Infinite-decay FDN + resonators decay into denormal range; x86 traps cost hundreds of cycles. | `ScopedDenormalMode` at the **top of `process()`** (`core/scoped_denormal_mode.h:60`), never MXCSR in `setupProcessing()` — MXCSR is per-thread (`membrum/src/processor/processor.cpp:1073-1075`). |
| R11 | **AU init fails with `-10875`.** | Mismatch between `au-info.plist`, `kSupportedNumChannels` and the actual bus config. | The FR-015/FR-016/FR-020 triple is written from one model (Membrum, the only other 0-in/2-out instrument); SC-004's `auval -v aumu Srph KrAt` is the gate. |
| R12 | **Generated files committed / tree dirty after a build.** | `version.h`, `win32resource.rc`, `audiounitconfig.h` are `configure_file` outputs into the **source** tree (`KratePlugin.cmake:83-105`). | FR-007a's `.gitignore` trio; SC-020 requires `git status --porcelain plugins/seraphis` to be empty after a full configure+build. |
| R13 | **Memory footprint surprise.** | `SeraphisEngine::prepare` prepares **all 16 slots regardless of `cfg.polyphony`** (`:195-200`) — Phase 7 RA-8 records 33.6 MB of capture rings @ 48 kHz at `captureSeconds = 4`, plus the reverb. 16 instances ≈ 0.5 GB. | Accepted Phase 7 consequence, recorded here so Phase 8 does **not** "fix" it by lowering `captureSeconds` without a decision. |
| R14 | **`setState()` racing `process()`.** | The host may call it from the UI thread mid-render. | All parameter storage is `std::atomic<>`; `setState()` reaches no `prepare()` and no buffer resize (§2.5.9). |
| R15 | **Sample rate outside the reverb's clamp range desynchronises the two components.** | `AetherReverb::prepare` clamps into `[kMinSampleRate, kMaxSampleRate]` (`:1615-1616`); `SeraphisEngine::prepare` only floors at 1.0 (`:202`). | Debug-only `assert` in `setupProcessing()` that both accepted the same rate; SC-013 clause 2 exercises 44.1/48/96/192 kHz. |
| R16 | **`[lifecycle]` tag collision drags a 4 s render into the valgrind nightly.** | The nightly filters by that exact tag. | Only `Seraphis_EditorLifecycle` carries `[lifecycle]`; the processor-lifecycle case uses `[lifecycle-proc]` (§4.1). |

---

## 7. Implementation order

Each step ends in a verification that can actually fail. Do not proceed past a red gate.

| # | Work | Verify |
|---|---|---|
| S1 | Directory skeleton + `version.json` + `CHANGELOG.md` + `README.md` + `.gitignore` trio + `CMakeLists.txt` + root `add_subdirectory` + empty-but-compiling `entry.cpp`/`plugin_ids.h`/stub processor+controller | `cmake --preset windows-x64-release` configures; `--target Seraphis` links; `git status --porcelain plugins/seraphis` empty |
| S2 | `parameters/global_params.h`, `parameters/macro_params.h`, `engine/seraphis_engine_config.h`, `preset/`, `update/` | builds warning-free |
| S3 | Processor: buses, `setupProcessing`, `getState`/`setState`, degenerate-shape guards, `getLatencySamples` | `seraphis_tests` (bus / state / lifecycle cases) green |
| S4 | Processor: the slice loop, event translation, master gain, bloom lifecycle | `Seraphis_MidiEventTranslation` (incl. SC-008) + `Seraphis_ProcessorRendersHeldNote` green |
| S5 | Controller + `resources/editor.uidesc` + `PresetManager` | `Seraphis_EditorLifecycle` green; `check-bundle.js` OK |
| S6 | AU/AUv3 resources, entitlements, installers, `docs/`, `CLAUDE.md` leaf | pluginval strictness 5 clean locally (SC-003) **AND SC-004**: the macOS CI leg's `auval -v aumu Srph KrAt` step green — the only surface that can measure it (§5.8); record the run URL + output in `compliance.md`. Requires the roster edits of site #10, so S6 and S8 interleave here. |
| S7 | Param-flow + soft-limit + allocation + Aether-target tests | SC-007, SC-019, SC-024, SC-026, SC-027 green with their non-vacuity clauses |
| S8 | `tools/lint-plugin-roster.js` + all eighteen roster edits + regenerated artifacts + root `CLAUDE.md` | `node tools/lint-plugin-roster.js` exits 0 **and** its deliberate-omission probe exits non-zero; the three `--check` generators exit 0; all nine lints exit 0 |
| S9 | ASan lane (SC-012 clause 3), clang-tidy both scripts, `check-portability.js`, `[.perf]` measurement | clean ASan exit; zero tidy warnings; SC-014 figure recorded in `compliance.md` |
| S10 | `compliance.md` — every FR/SC row filled from **actual** file:line and **actual** measured numbers | no ✅ without a citation or a copied measurement |

---

## 8. Decisions deliberately left open, spec amendments, and one residual

### 8.1 Left open

1. **The two FUID values** are generated at implementation time (§2.1); they cannot be pinned in a
   plan without becoming stale copy-paste.

### 8.2 Spec amendments this plan requires (`spec.md` edits, to be made before S10's compliance table)

Each entry is a *factual* correction or a *strengthening*; none relaxes a threshold. Without them a
compliance table filled honestly would have to mark the listed FR/SC as not met.

| # | Spec site | Amendment | Plan section |
|---|---|---|---|
| A1 | FR-008 file list (`spec.md:307-339`) | Add `tests/seraphis_test_fixture.h` (test infrastructure every planned case depends on) and the `.gitkeep` entries under `docs/`, `src/ui/`, `resources/presets/Textures/`. FR-008 says the skeleton is "**exactly** the file list below". | §4.2, §2.12 |
| A2 | FR-023 clause 4, FR-033 (`spec.md:695-712`), SC-013 clause 4 | Delete the `restartComponent(kLatencyChanged)` requirement; state instead that the Phase 8 latency is the invariant 1024 (`aether_reverb.h:2607-2613`) and that hosts read `getLatencySamples()` after `setupProcessing()`. Restate SC-013 clause 4 as the invariance matrix in §4.3. Record that a future variable latency must use processor → `IMessage` → controller → `getComponentHandler()->restartComponent` (`vsteditcontroller.h:97`), **never** a `getHostContext()` query. | §1.3 C-1/C-2, §2.5.8 |
| A3 | FR-031 (`spec.md:583-586`) | Amend `noteOn(pitch, velocity*127)` to `noteOn(pitch, uint8(clamp(velocity*127 + 0.5, 1, 127)))`, citing `seraphis_engine.h:374-377` (velocity 0 → `noteOff`). Add SC-022 sub-clause (6). | §1.3 C-3, §3.2 |
| A4 | FR-050 (`spec.md:695-706`) and the traceability row `FR-050, FR-051 \| SC-003` | Delete "*so the `Textures` category is genuinely scanned and FR-050/FR-051 are verified by SC-003 rather than by inspection*" — `PresetManager`'s ctor stores and scans nothing (`preset_manager.cpp:16-29`; enumeration is `scanPresets()` at `:37-56`, which nothing in Phase 8 calls). Re-map FR-050/FR-051 to **SC-012 clause 2c**. Keep the instantiation requirement. | §2.7, §4.3 SC-012 |
| A5 | FR-047 traceability (`spec.md` row `FR-045, FR-046, FR-047 \| SC-010`) | Add SC-010's controller clause to the criterion text; as written SC-010 never calls `Controller::setComponentState()`, leaving FR-047 and both `load*ParamsToController` helpers with no detector. | §4.3 SC-010 |
| A6 | FR-042 traceability / SC-009 (`spec.md:658-660`) | Add the multi-point-queue clause; with one-point queues `getPoint(0)` and `getPoint(count-1)` are indistinguishable. | §4.2, §4.3 SC-009 |
| A7 | FR-054 / SC-012 clause 2 (`spec.md:741-759, 1102-1108`) | Add the bound-control assertion (clause 2b). The harness asserts only `attached()`, `getFrame() != nullptr`, `getNbViews() > 0` (`editor_lifecycle_harness.h:120-128`), which a one-label template satisfies; the "wrong parameter type surfaces now" claim is untrue without 2b. | §2.10, §4.3 SC-012 |
| A8 | SC-006 (`spec.md:995-1018`) | State the master gain per clause: 2.0 for the peak bound, normalized 0.5 (linear 1.0) for clauses 2–3. `renderSeraphisChain` applies no gain (`seraphis_chain.h:228-231`), so a 2.0 comparison fails a correct implementation and makes clause 3 pass for the wrong reason. SC-024 clause 3 inherits the fix. | §4.3 SC-006 |
| A9 | SC-007 / SC-026 (`spec.md:1019-1026`) | Pin the reading form: the live singleton `AllocationDetector::instance().getAllocationCount()` (`allocation_detector.h:45-47`), never `scope.getAllocationCount()` inside the scope (`:81-87` — assigned only by the destructor). | §4.3 SC-007, SC-026 |
| A10 | FR-030 / SC-021 (`spec.md:580-582, 1222-1227`) | Add the mono-output (`numChannels < 2`) early-out and clause, the non-zero canary pre-seed for the silence clause, the `silenceFlags = 3` value on the not-ready path, and the out-of-order `setupProcessing()` clause. | §2.5.3, §2.5.6, §4.3 SC-021 |

### 8.3 Residual, recorded rather than hidden

**Soft-limit push ramps instead of snapping when `softLimit == false` at prepare.** `engine_->
setOutputSaturation(0.0f)` necessarily runs after `engine_->prepare()`, which re-applies
`kOutputSaturation` and snaps the saturator's smoother (`seraphis_engine.h:225-231`), so the
post-prepare setter takes the ramping branch (`tape_saturator.h:248-252`). Effect: the first
`kDefaultSmoothingMs = 5.0f` (`tape_saturator.h:88`) of the render carries a decaying ≤ 0.15
tanh/linear blend (`:420-424`) that should have been 0. Removing it requires threading
`outputSaturation` through `SeraphisEngineConfig` — a `dsp/` change Phase 8's scope forbids.
**Deferred to Phase 9**, with the measured level recorded in `compliance.md` alongside SC-027's
figures. Not accepted silently: §2.5.4 states it at the use site.

### 8.4 Pre-existing roster drift — closed, not left open

The four sites enumerated in §5.7 (`disrumpo` × `ci.yml:234, :466, :858`; `membrum` ×
`release.yml:137`) are fixed in S8's commit. One line each, cache-key granularity only. No
allow-list entry, no lint weakening.

---

## Review notes

Resolutions that warrant a word beyond the edit itself:

1. **The latency-announcement issue was resolved by removing the mechanism (option (a)), not by
   routing it through the controller (option (b)).** Option (b) is real machinery and was rejected
   for Phase 8 only because Phase 8 ships **no code path that can change the reported latency** —
   `getLatencySamples()` returns `spectralEnabled_ ? diffusionFftSize_ : 0` with both pinned at
   prepare (`aether_reverb.h:2607-2613`, `makeSeraphisReverbConfig` §2.2), so the notification's only
   possible effect is a host re-reading a constant. §1.3 C-2 records option (b) as the mandated route
   the moment latency becomes variable. This removes an unobservable requirement; it does not relax a
   threshold, and the replacement SC-013 clause 4 asserts strictly more real behaviour than the
   fabricated-host version could.
2. **Two of the reported issues (the §2.5.6 guard ordering / silence fill, reported once under
   `rt-layers` and once under `reuse-reality`) are the same defect** and are resolved by one edit in
   §2.5.6 plus one strengthened SC-021 clause. The mono-`numChannels` guard, reported alongside, is a
   *separate* defect and got its own guard and its own SC-021 sub-clause.
3. **Items that require editing `spec.md` are recorded in §8.2 rather than applied**, because this
   revision edits `plan.md` only. They are amendments (corrections/strengthenings) and must land
   before S10 fills the compliance table — in particular A1 (FR-008's "exactly this file list"),
   which the plan's own test fixture otherwise violates.
4. **No issue was rejected.**

## §9 Phase-owner resolutions (recorded before build stage, 2026-07-31)

1. **C-1/C-2 CONFIRMED** — amend the spec prose to match: `kLatencyUnknown = size_t(-1)` sentinel
   tracker (first setupProcessing announces exactly once), and
   `FUnknownPtr<IComponentHandler>(getHostContext())` (null-guarded) as the handler source. The
   grill Q2 intent (honest, announced latency) is honoured by the mechanism that exists; a later
   reader must not "fix" the sentinel back to 0.
2. **FUIDs** — no convention beyond: generate two v4 GUIDs at T003, verify non-collision against the
   twelve registered FUIDs, record both in plugins/seraphis/CLAUDE.md as immutable.
3. **Membrum release.yml roster gap: FIX IT in the same commit** (one line in the hashFiles list).
   Allow-listing a known genuine gap contradicts the own-all-failures rule; the lint stays strict.
4. **D-1 hoist CONFIRMED** — pushGlobalParams()/master-gain setTarget() once per process() call;
   observationally identical (atomics cannot change mid-call) and trivially partition-invariant.
   The per-sample gain multiply stays in the slice loop.
5. **(= 2.)**
6. **T013/T014 ordering** — run T014 immediately after T013; no temporary `if(EXISTS)` guard.
7. **SC-027 threshold CONFIRMED as measured-then-pinned** with provenance comment; inspection-only
   fallback with recorded deltas only if no reachable level makes the soft limit measurable.
8. **SC-006 clause 3 escalation CONFIRMED** — escalate the scenario (voices/level) until the
   non-vacuity assertion holds; never weaken the clause; record the escalated level in compliance.md.
9. **SC-004 (auval): DO NOT push mid-phase.** Pushing requires explicit user permission (standing
   rule). SC-004 is recorded in compliance.md as pending-macOS-CI, to be verified on the next
   user-authorized push; every locally-measurable gate must be green regardless.
