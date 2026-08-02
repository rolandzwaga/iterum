# Implementation Plan: Seraphis Phase 9 — Full Parameter Surface & State

**Spec:** `specs/seraphis-phase9-parameters/spec.md`
**Roadmap:** `specs/Seraphis-roadmap.md` → Part B, Phase 9 (lines 453–460, post-FR-058 amendment)
**Branch:** `feat/seraphis-phase1-life-modulators` (all Seraphis phases share it)
**Status:** PLAN — no implementation
**Date:** 2026-08-01

---

## 0. Ground rules for this plan

### 0.1 What the plan is required to decide

The spec defers exactly seven things to the plan stage, and each has a numbered section here.
Nothing below is left to the implementer:

| Deferred by | What the plan must fix | Section |
|---|---|---|
| FR-059 cl. 2 / Q1 | the class-(b) smoothing **time constant**, as one number or a per-ID column | §3.5.2 |
| FR-059 / SC-005 | the full **`kContinuityMechanism[]` classification** of all 85 in-scope IDs, with file:line evidence | §3.5.3 |
| FR-056 / Q3 | the **tempo sample point** and the **epsilon** that counts as "changed" | §3.6 |
| FR-070 #12 | explicit verification of `setInputAgcEnabled`'s interaction with the FR-033a estimator | §1.5.3 |
| SC-009 | the **exhaustively enumerated non-default parameter table**, one plain value per ID, all 91 rows | §7.9 |
| SC-023 cl. 2 | its **own** all-non-default value table (no pinned rows, no `n/a` rows) | §7.13 |
| SC-003 / SC-020 / SC-008 | the **measured** thresholds pinned at plan stage (`floor(min/1.05)`, `ceil(worst×1.05)`) | §7.4, §7.11, §7.8 |

All seven are discharged in §12's table. Where this plan's design contradicts normative spec text,
the resolution is an **edit to `spec.md`** — §12.1's amendments A1–A9, applied as **§11 step 0, before
any code** — never a logged deviation. A logged deviation would leave the compliance pass grading the
implementation against FR text the plan deliberately violates, and would make a test written from the
spec fail a plan-conformant implementation.

### 0.2 Every signature in this plan was read this session

Line numbers are as they stand on `feat/seraphis-phase1-life-modulators` at 2026-08-01. Files opened
and quoted below: `dsp/include/krate/dsp/systems/{seraphis_engine,seraphis_voice,seraphis_macro_matrix,
continuous_body,atmosphere_engine,spectral_morph_engine,harmonic_cloud}.h`,
`dsp/include/krate/dsp/processors/{spectral_state,growth_envelope,orbit_modulator,midside_processor}.h`,
`dsp/include/krate/dsp/primitives/smoother.h`, `dsp/include/krate/dsp/effects/aether_reverb.h`,
`plugins/seraphis/src/{plugin_ids.h,processor/processor.h,processor/processor.cpp,
controller/controller.cpp,engine/seraphis_engine_config.h,parameters/global_params.h,
parameters/macro_params.h}`, `plugins/shared/src/ui/parameter_helpers.h`,
`plugins/seraphis/tests/{CMakeLists.txt,seraphis_test_fixture.h,unit/param_denorm_test.cpp}`,
`plugins/seraphis/resources/editor.uidesc`, `tests/test_helpers/{render_fingerprint.h,
seraphis_chain.h,uidesc_reachability.h}`, `dsp/tests/CMakeLists.txt`,
`dsp/tests/unit/systems/seraphis_perf_test.cpp`.

### 0.3 Standing constraints this plan inherits

- **RT safety.** Every new push path is a sequence of `noexcept` scalar setters over pre-existing
  objects. No allocation, lock, exception or I/O on the audio thread (FR-048).
- **Layer discipline.** The four `dsp/` files touched stay Layer 3; no new include of any Layer 4
  header (FR-006). `plugins/seraphis/src/engine/seraphis_engine_config.h` is the only place that
  names both `SeraphisEngine` (L3) and `AetherReverb` (L4) — as it already does
  (`seraphis_engine_config.h:12-14`).
- **Portability.** No `std::isnan` / `std::isinf` / `std::numeric_limits<>::infinity()` anywhere new —
  bit-pattern finiteness only, reusing the helpers the classes already carry
  (`seraphis_macro_matrix.h:685-689`, `seraphis_voice.h:786-790`). No narrowing in brace init; every
  aggregate uses designated initializers. `node tools/check-portability.js` before every commit.
- **Surgical changes.** No existing DSP signature moves. No existing member moves. No Phase 8
  registered parameter changes type, ID, default or unit (FR-063, C-9).

---

## 1. DSP additions (Layer 3, `Krate::DSP`, additive only)

Four files are touched, and no others (FR-071): `seraphis_engine.h`, `seraphis_macro_matrix.h`,
`seraphis_voice.h`, and — under the single named carve-out — `continuous_body.h`.

### 1.1 `SeraphisVoiceParams` (FR-001)

**Header:** `dsp/include/krate/dsp/systems/seraphis_engine.h`, at namespace scope immediately after
`SeraphisEngineConfig` (`:92-97`).
**Layer:** 3 (systems/). Every enum it names already arrives through `seraphis_voice.h`, which
`seraphis_engine.h` includes. **One include is added**, and it is not optional: the field-count guard
below uses `std::is_trivially_copyable_v`, which requires `<type_traits>`, and neither
`seraphis_engine.h`'s stdlib block (`:79-86`: `<algorithm> <array> <cassert> <cmath> <cstddef>
<cstdint> <cstring> <span>`) nor `seraphis_voice.h`'s (`:48-74`) includes it. MSVC and libstdc++
commonly drag it in through `<array>`/`<algorithm>`; libc++ need not, and `node
tools/check-portability.js` compiles the changed TUs on whatever standard library is present — so it
would pass on a toolchain that happens to leak the header and fail on CI's. `#include <type_traits>`
is therefore added to `seraphis_engine.h`'s stdlib block, and it is recorded in the FR-071
touched-file note as the one include this phase adds to `dsp/`.

A POD of **37 fields**, one per `VP`-routed row of C-6, each with a default member initializer equal
to the shipped voice default. It is *not* `SeraphisVoiceConfig` (`seraphis_voice.h:105-120`), which is
prepare-time and is not extended.

```cpp
/// @par Layer: 3 (systems/). Dependencies: Layers 0-2 + Layer 3 peers. NO Layer 4.
/// @par Real-Time Safety: plain data; copying is trivial and allocation-free.
///
/// FR-001. One field per VP-routed parameter of the Phase 9 surface (spec C-6).
/// NO FIELD HERE MAY NAME A SeraphisMacroTarget (spec C-1/FR-055): those 27
/// values reach the voices through SeraphisMacroMatrix::setTargetBase, and a
/// second write path would double-apply them.
struct SeraphisVoiceParams {
    // -- HarmonicCloud (IDs 206, 209, 210) ---------------------------------
    float cloudDriftSmoothness   = 0.5f;    // harmonic_cloud.h:2131
    float cloudDecaySec          = 0.5f;    // seraphis_voice.h:298
    float cloudEnvOffsetSpread   = 0.0f;    // harmonic_cloud.h:2135

    // -- SpectralMorphEngine (IDs 401, 403, 404, 407) ----------------------
    float morphBloom             = 0.0f;    // seraphis_voice.h:302
    SpectralMorphEngine::TravelMode morphTravelMode =
        SpectralMorphEngine::TravelMode::External;              // :139
    float morphTravelRate        = SpectralMorphEngine::kMinTravelRate;  // :101, voice :303
    double morphWaypointSeconds  = 2.0;     // SplineTrajectory::kDefaultInterval

    // -- Spatial / life modulators (IDs 601, 602, 603) ---------------------
    float spatialRateHz          = 0.1f;    // seraphis_voice.h:331
    float spatialCoupling        = 0.0f;    // :332
    float spatialGrowth          = 0.0f;    // :333

    // -- Voice envelope (IDs 700, 701) -------------------------------------
    SeraphisVoice::EnvelopeMode envMode = SeraphisVoice::EnvelopeMode::Standard;  // :341
    float envGrowthDurationSec   = 10.0f;   // :364

    // -- ContinuousBody (IDs 800, 801, 803-812) ----------------------------
    ContinuousBody::BodyMaterial bodyMaterial =
        ContinuousBody::BodyMaterial::Glass;                    // :306
    float bodyResonance          = 0.7f;    // :307
    float bodyKeyTracking        = 1.0f;    // :309
    float bodyDrive              = 1.0f;    // :310
    float bodyMix                = 1.0f;    // :311
    float bodyCloudMix           = 0.25f;   // :313
    float bodyCloudDecaySec      = 4.0f;    // :314
    float bodyCloudSize          = 1.0f;    // :315
    float bodyCloudDamping       = 0.3f;    // :316
    float bodyWidth              = 1.0f;    // :317
    bool  bodyInputAgc           = ContinuousBody::kDefaultAgcEnabled;       // :163 (true)
    bool  bodyResonatorBypass    = ContinuousBody::kDefaultResonatorBypass;  // :164 (false)

    // -- AtmosphereEngine (IDs 1002, 1003, 1005-1007, 1009-1016) -----------
    float atmosDensity           = 4.0f;    // seraphis_voice.h:322
    float atmosGrainSeconds      = 4.0f;    // :323
    float atmosPanSpread         = 0.7f;    // :325
    float atmosDecorrelation     = 0.5f;    // :326
    float atmosFreezeMix         = 0.0f;    // :327
    float atmosDriftSmoothness   = 0.7f;    // atmosphere_engine.h:843 (documented default)
    float atmosDriftRangeSemis   = 2.0f;    // :850
    float atmosJitter            = 0.5f;    // :798
    float atmosPositionSeconds   = 1.0f;    // :805-806
    float atmosPositionSpread    = 0.3f;    // :813-814
    float atmosPitchSemitones    = 0.0f;    // :820
    float atmosPitchSpread       = 0.15f;   // :828-829
    GrainEnvelopeType atmosGrainEnvelope = GrainEnvelopeType::Hann;  // :952
};

static_assert(std::is_trivially_copyable_v<SeraphisVoiceParams>,
              "FR-001: the broadcast POD is copied on the audio thread");
```

**Field-count guard.** A `static_assert` cannot count named fields, so FR-001's "static_assert-able
field count" is met by a **compile-time constant plus a behavioural test**, not by a `sizeof`
assertion (which is padding-dependent and would break on a legal ABI difference):

```cpp
    /// FR-001. The spec's VP row count (C-6). Any field added or removed without
    /// updating the C-6 table fails SeraphisVoiceParams_CoversEveryVpRow.
    static constexpr std::size_t kFieldCount = 37;
```

The disjointness half of FR-001/FR-055 is checked at run time by
`SeraphisVoiceParams_IsDisjointFromMacroTargets` (§7.2): push every field to a non-default value,
call `applyVoiceParams`, and assert `getTargetBase(t)` is unchanged for all 27 targets.

**Two type notes.**
- `morphWaypointSeconds` is a `double`, because `SpectralMorphEngine::setWaypointInterval` takes
  `double` (`spectral_morph_engine.h:385`). It is *stored* as a `float` in the state stream (C-8) and
  in the parameter pack; the widening happens at the push, never the other way.
- `atmosGrainEnvelope`'s type is the namespace-scope `Krate::DSP::GrainEnvelopeType`
  (`dsp/include/krate/dsp/core/grain_envelope.h`), reached through `atmosphere_engine.h`.

### 1.2 `SeraphisEngine::applyVoiceParams` (FR-002)

**Header:** same file, public section, beside `setAtmosphereFreeze` (`:551`).

```cpp
    /// @brief FR-002 (spec Phase 9). Broadcast the run-time voice parameter set
    ///        to EVERY slot.
    ///
    /// THE BOUND IS kMaxVoices AND NOT getPolyphony(), and that is load-bearing.
    /// setPolyphony() force-idles an excess slot with voices_[i].noteOff() and
    /// records orphanTail_ |= voiceBit(i) when !isFinished() (:339-348), and
    /// processStereoBlock's loop bound is `v < kMaxVoices` unconditionally
    /// (:437, :464-486). A getPolyphony() bound would leave an audibly-summed
    /// orphan running on prepare-time defaults for its whole release - up to
    /// 8000 ms at the shipped default (seraphis_voice.h:359) - and would leave a
    /// slot the allocator hands out after a polyphony INCREASE unconfigured.
    /// Same bound as setSeed (:355) and setAtmosphereFreeze (:557).
    ///
    /// Does NOT call setSpectralState / setSpectralStateCount: those are
    /// configure-time gated and belong to applySpectralStates (FR-005).
    ///
    /// @par Real-Time Safety: allocation-free, lock-free, exception-free.
    ///      37 noexcept scalar setters x kMaxVoices; every one is idempotent.
    void applyVoiceParams(const SeraphisVoiceParams& p) noexcept {
        for (std::size_t v = 0; v < kMaxVoices; ++v) {
            SeraphisVoice& voice = voices_[v];
            // Cloud
            voice.setCloudDriftSmoothness(p.cloudDriftSmoothness);   // FR-070 #1
            voice.setDecayTimeSec(p.cloudDecaySec);                  // :650
            voice.setEnvelopeOffsetSpread(p.cloudEnvOffsetSpread);   // FR-070 #2
            // Morph
            voice.setBloom(p.morphBloom);                            // :654
            voice.setTravelMode(p.morphTravelMode);                  // :664
            voice.setTravelRate(p.morphTravelRate);                  // :657
            voice.setWaypointInterval(p.morphWaypointSeconds);       // FR-070 #5
            // Spatial
            voice.setSpatialRate(p.spatialRateHz);                   // :615
            voice.setSpatialCoupling(p.spatialCoupling);             // :616
            voice.setSpatialGrowth(p.spatialGrowth);                 // :617
            // Envelope
            voice.setEnvelopeMode(p.envMode);                        // :567
            voice.setGrowthDurationSeconds(p.envGrowthDurationSec);  // :580
            // Body
            voice.setMaterial(p.bodyMaterial);                       // :673
            voice.setResonance(p.bodyResonance);                     // :674
            voice.setKeyTracking(p.bodyKeyTracking);                 // :676
            voice.setDrive(p.bodyDrive);                             // :677
            voice.setMix(p.bodyMix);                                 // :678
            voice.setCloudMix(p.bodyCloudMix);                       // :679
            voice.setCloudDecaySec(p.bodyCloudDecaySec);             // :680
            voice.setCloudSize(p.bodyCloudSize);                     // :681
            voice.setCloudDamping(p.bodyCloudDamping);               // :682
            voice.setWidth(p.bodyWidth);                             // :683
            voice.setBodyInputAgcEnabled(p.bodyInputAgc);            // FR-070 #12
            voice.setBodyResonatorBypass(p.bodyResonatorBypass);     // FR-070 #13
            // Atmosphere
            voice.setDensity(p.atmosDensity);                        // :688
            voice.setGrainSeconds(p.atmosGrainSeconds);              // :689
            voice.setPanSpread(p.atmosPanSpread);                    // :691
            voice.setDecorrelation(p.atmosDecorrelation);            // :692
            voice.setFreezeMix(p.atmosFreezeMix);                    // :693
            voice.setAtmosDriftSmoothness(p.atmosDriftSmoothness);   // FR-070 #3
            voice.setAtmosDriftRangeSemitones(p.atmosDriftRangeSemis);// FR-070 #4
            voice.setAtmosJitter(p.atmosJitter);                     // FR-070 #6
            voice.setAtmosPositionSeconds(p.atmosPositionSeconds);   // FR-070 #7
            voice.setAtmosPositionSpread(p.atmosPositionSpread);     // FR-070 #8
            voice.setAtmosPitchSemitones(p.atmosPitchSemitones);     // FR-070 #9
            voice.setAtmosPitchSpread(p.atmosPitchSpread);           // FR-070 #10
            voice.setAtmosGrainEnvelope(p.atmosGrainEnvelope);       // FR-070 #11
        }
    }
```

**Two name collisions the mapping resolves, and they are easy to invert.**
`setDecayTimeSec` (cloud, `:650`) serves ID **209**; `setCloudDecaySec` (body, `:680`) serves ID
**807**. `setWidth` (body, `:683`) serves ID **810**; the *voice* width base is
`setVoiceWidthBasePercent` (`:625`) and is `MB`-routed, not a `SeraphisVoiceParams` field.
`SeraphisVoiceParams_MapsEveryFieldToItsOwnSetter` (§7.2) pins both pairs by driving one field at a
time and asserting only its own read-back moved.

### 1.3 `SeraphisEngine::applySpectralStates` (FR-005)

```cpp
    /// @brief FR-005. Configure-time fan-out of the four spectral slots.
    ///
    /// ALL FOUR SLOTS ARE WRITTEN, not `count` of them. SpectralMorphEngine::
    /// setState accepts any slot in [0, kMaxStates) irrespective of numStates_
    /// (spectral_morph_engine.h:292-295) and stores it, so writing all four is
    /// legal - and it is REQUIRED, because SC-003's rows for IDs 411/412 raise
    /// kMorphStateCountId to 4 and then expect slots 2 and 3 to already carry
    /// their content. `states` is always the Processor's 4-slot array (FR-041b).
    ///
    /// The per-voice gate (seraphis_voice.h:699-719) is the ONLY guard; this
    /// function adds none and swallows no rejection - the caller reads
    /// getRejectedConfigureTimeCallCount() (:720) across the pool to decide
    /// whether to retry (FR-046 clause 3).
    ///
    /// kMaxVoices, not getPolyphony(): a slot the allocator hands out later must
    /// already carry the states.
    ///
    /// `voiceMask` selects which slots are written; bit v selects voices_[v].
    /// The default 0xFFFF is the whole pool, so the FR-005 contract is unchanged
    /// for every caller that does not name a mask. It exists because the FR-046
    /// RETRY must not re-push to voices that already accepted: on an accepting
    /// voice SpectralMorphEngine::setState runs isValidSpectralState AND
    /// buildSanitized - a full 64-entry std::log2 pass
    /// (spectral_morph_engine.h:296-301, :537-543) - BEFORE the identity check at
    /// :302-304 that would make it a no-op. A whole-pool retry therefore costs
    /// 15 x 4 x 64 ~= 3840 std::log2 per block, every block, for as long as ONE
    /// voice keeps rejecting - which is the whole of a sustained note plus its
    /// release (up to 8000 ms, seraphis_voice.h:359). See plan section 3.3.
    ///
    /// THE SAME ARITHMETIC BOUNDS THE SUCCESS PATH, and the plan does not pretend
    /// otherwise: a mask of 0xFFFF over a quiescent pool is 16 x 4 = 64
    /// buildSanitized calls = 4096 std::log2 plus 64 isValidSpectralState scans
    /// plus 64 128-float array comparisons, in ONE process() call. That is why
    /// pushAllSurfaces() raises spectralStatesPending_ / spectralRetryMask_ only
    /// when a slot id actually moved or the engine was re-prepared (plan section
    /// 3.4), and why section 7.8's worst-case arm carries a one-directional
    /// remedy for the case where the measured figure breaches FR-057's 0.50 %.
    ///
    /// @par Real-Time Safety: allocation-free, lock-free, exception-free.
    void applySpectralStates(const SpectralState* states, int count,
                             std::uint16_t voiceMask = 0xFFFFu) noexcept {
        if (states == nullptr) {
            return;
        }
        for (std::size_t v = 0; v < kMaxVoices; ++v) {
            if ((voiceMask & (std::uint16_t{1} << v)) == 0u) {
                continue;
            }
            voices_[v].setSpectralStateCount(count);   // clamps [2,4] downstream (:319)
            for (int slot = 0; slot < SpectralMorphEngine::kMaxStates; ++slot) {
                voices_[v].setSpectralState(slot, states[static_cast<std::size_t>(slot)]);
            }
        }
    }
```

The mask is a **defaulted parameter on the same symbol**, not a second overload, so FR-006's symbol
count does not move and no existing call site changes shape.

**Rejection accounting.** On a sounding voice each call increments `rejectedConfigCalls_` by exactly
**5** (one count + four slots), because the gate runs before the morph call and fires even for an
unchanged value (`seraphis_voice.h:706-719`). FR-046 clause 3 compares the counter **per voice**
before and after (§3.3), not only in total: the per-voice delta is what identifies which bits of the
retry mask may be cleared. The constant multiplier is irrelevant either way — a voice whose delta is
0 accepted.

### 1.4 `SeraphisMacroMatrix` base overrides (FR-003, FR-004)

**Header:** `dsp/include/krate/dsp/systems/seraphis_macro_matrix.h`.

Two new private members and three new public methods. `apply()` (`:623`), `computeAetherTargets()`
(`:667`) and `contributionOf()` (`:702`) do not change shape at all; `evaluateAll()` (`:719-731`)
changes exactly one line.

```cpp
public:
    /// @brief FR-003 (Phase 9). Override the per-target `base` that evaluateAll()
    ///        seeds from, so a deep parameter IS the origin the macros move from.
    ///
    /// Well-defined by construction: everyRowSharesOneBasePerTarget(kRows)
    /// (:752) already guarantees exactly one base per target, so a PER-TARGET
    /// override cannot be ambiguous.
    ///
    /// A non-finite argument leaves the stored base UNCHANGED - checked with the
    /// class's own bit-pattern helper (:685-689), never std::isnan, which
    /// -ffast-math folds away on the macOS leg.
    void setTargetBase(SeraphisMacroTarget target, float base) noexcept {
        const auto i = static_cast<std::size_t>(target);
        if (i >= kNumTargets || !isFiniteBits(base)) {
            return;
        }
        baseOverride_[i] = base;
        hasOverride_[i] = true;
    }

    /// @brief FR-003. Restore every kRows literal verbatim.
    void resetTargetBases() noexcept {
        hasOverride_.fill(false);
        baseOverride_.fill(0.0f);
    }

    /// @brief FR-003. The override if one was set, else the kRows literal.
    [[nodiscard]] float getTargetBase(SeraphisMacroTarget target) const noexcept {
        const auto i = static_cast<std::size_t>(target);
        if (i >= kNumTargets) {
            return 0.0f;
        }
        return hasOverride_[i] ? baseOverride_[i] : literalBaseFor(target);
    }

private:
    /// The kRows base for `target`. everyTargetInFr061to065IsPresent (:750)
    /// guarantees the scan always finds one, so the 0 fallback is unreachable.
    [[nodiscard]] static constexpr float literalBaseFor(SeraphisMacroTarget target) noexcept {
        for (const SeraphisMacroRow& row : kRows) {
            if (row.target == target) {
                return row.base;
            }
        }
        return 0.0f;
    }

    std::array<float, kNumTargets> baseOverride_{};
    std::array<bool,  kNumTargets> hasOverride_{};
```

`evaluateAll()`'s single changed line (`:725`):

```cpp
            if (!seeded[i]) {
                value[i] = hasOverride_[i] ? baseOverride_[i] : row.base;   // FR-004
                seeded[i] = true;
            }
```

**SC-002 clause 4 falls out of the construction.** A default-constructed matrix has
`hasOverride_` all `false`, so `evaluateAll()` evaluates the identical expression it does today, and
`apply()` / `computeAetherTargets()` are bit-identical. `SeraphisMacroMatrix_DefaultBases_Unchanged`
(§7.1) pins all 27 targets against the `kRows` literals.

**Size.** `sizeof(SeraphisMacroMatrix)` grows from ~20 B to ~20 + 27·4 + 27 ≈ **155 B**, held by
value in `Processor` (`processor.h:78`). `static_assert(sizeof(Processor) < 64 KiB)` (`:104`) is
unaffected (§3.1).

**What C-1's clamp behaviour means here, and what the code must NOT do.** `kRows`' `amount` values
were sized against the default base (Bloom → `CloudRichness` is `base 0.60, amount +0.40`, `:246-251`,
against `setRichness`' `[0,1]` clamp). Overriding the base moves the reachable span, and an override
placed at the clamp the macro travels toward consumes it. **No headroom rescaling is added** — that
would make the two surfaces multiply instead of compose and would change shipped macro behaviour at
the defaults. SC-004 Arm 3 asserts the saturation case as legal.

### 1.5 Thirteen `SeraphisVoice` forwarders (FR-070)

**Header:** `dsp/include/krate/dsp/systems/seraphis_voice.h`, appended to the existing
"Engine parameter surface (FR-030) — one-to-one forwarders, no added clamping" block
(`:637-693`). The banner's "no added clamping" contract holds for all thirteen: each is a single
delegation with no guard of its own.

#### 1.5.1 The five criterion-(a)/(b) admissions

```cpp
    // -- HarmonicCloud, Phase 9 additions (FR-070 #1, #2) --------------------
    /// PREFIXED: the bare setDriftSmoothness would be ambiguous between
    /// HarmonicCloud (harmonic_cloud.h:513) and AtmosphereEngine
    /// (atmosphere_engine.h:844), both of which this facade reaches.
    void setCloudDriftSmoothness(float s) noexcept { cloud_.setDriftSmoothness(s); }
    void setEnvelopeOffsetSpread(float spread) noexcept {
        cloud_.setEnvelopeOffsetSpread(spread);      // harmonic_cloud.h:580
    }

    // -- AtmosphereEngine drift (FR-070 #3, #4) ------------------------------
    void setAtmosDriftSmoothness(float s) noexcept { atmos_.setDriftSmoothness(s); }   // :844
    void setAtmosDriftRangeSemitones(float st) noexcept {
        atmos_.setDriftRangeSemitones(st);           // :852
    }

    // -- SpectralMorphEngine spline shape (FR-070 #5) ------------------------
    /// double, matching the owner's signature (spectral_morph_engine.h:385),
    /// which rejects a non-finite argument itself (:386-388).
    void setWaypointInterval(double seconds) noexcept { morph_.setWaypointInterval(seconds); }
```

#### 1.5.2 The ATMOSPHERE SET (FR-070 #6–#11, criterion (c))

```cpp
    void setAtmosJitter(float amount) noexcept { atmos_.setJitter(amount); }              // :800
    void setAtmosPositionSeconds(float s) noexcept { atmos_.setPositionSeconds(s); }      // :807
    void setAtmosPositionSpread(float sp) noexcept { atmos_.setPositionSpread(sp); }      // :815
    void setAtmosPitchSemitones(float st) noexcept { atmos_.setPitchSemitones(st); }      // :822
    void setAtmosPitchSpread(float sp) noexcept { atmos_.setPitchSpread(sp); }            // :830
    void setAtmosGrainEnvelope(GrainEnvelopeType t) noexcept { atmos_.setGrainEnvelope(t); } // :959
```

All six already ship matching getters (`:803`, `:811`, `:819`, `:826`, `:833`, `:962`), so **FR-072
creates nothing for them** and SC-003's blanket `VP` read-back rule applies unmodified.
`setGrainEnvelope` is a plain store over windows `prepare()` already generated (`:954-961`), which is
what makes it registerable at block rate at all.

#### 1.5.3 The BODY SET (FR-070 #12–#13, criterion (c)) — and the FR-033a check the spec demands

```cpp
    /// FR-070 #12. Serves ID 811. See the AGC note below: turning this OFF is a
    /// documented level change, not a defect.
    void setBodyInputAgcEnabled(bool enabled) noexcept {
        body_.setInputAgcEnabled(enabled);           // continuous_body.h:1276
    }
    /// FR-070 #13. Serves ID 812. body_.setResonatorBypass is SELF-GUARDING
    /// (:1302-1304) and applies its own 10 ms equal-power ramp plus the
    /// mandatory waveguide re-tune on un-bypass (:1311-1320). This forwarder
    /// MUST NOT add a second guard.
    void setBodyResonatorBypass(bool bypass) noexcept {
        body_.setResonatorBypass(bypass);            // :1300
    }
```

**FR-070 #12's interaction with FR-033a, verified against the shipped code this session.**
`ContinuousBody::setInputAgcEnabled` is a bare store into `agcEnabled_` (`:1276-1279`, member at
`:4217`, default `kDefaultAgcEnabled = true` at `:163`). Turning it **off** changes three coupled
things at once, by contract:

1. `seedLog2For()` returns `0.0f` — *"the AGC-off branch is not a guard, it is the contract"* — so the
   per-material `kExcitationCompSeed` warm start shipped in `ee408854` is switched off;
2. `updateExcitationComp()` early-returns having forced `excitationCompLog2_ = 0`,
   `excitationComp_ = kMinExcitationComp`, and abandoned the measurement window;
3. `controlStep()` and the recovery path set `rmsGain_ = 1.0f` instead of the tracked
   `clamp(kTargetInputRms / max(inputRms_, kRmsFloor), …)`.

The component becomes a **fixed gain**. Three consequences bind, and each is discharged in this plan:

- **(i)** A level change on toggling is **correct behaviour**. No criterion asserts level continuity
  across ID 811 — SC-003's row for 811 is a boolean read-back only (§7.4), and SC-005's matched-regime
  bound (§7.6) measures a **per-sample** step statistic, which an AGC-driven level *trend* over tens
  of milliseconds does not produce.
- **(ii)** ID 811 stays in scope for SC-005 clauses 1–3, and is classified **class (a)** in
  `kContinuityMechanism[]` — but the header's own citation, *"Absorbed by the drive smoother, so
  toggling is clickless"* (`continuous_body.h:1274-1276`), **is scoped to the engine path only and is
  not sufficient evidence on its own.** Verified this session: `agcEnabled_` selects
  `rmsGain_` (`:3686`, `:4000`), and `rmsGain_` — together with `userDrive_`, which
  `setDrive` stores raw at `:1205` — feeds **two** consumers, not one:
  1. `engineDriveFor()` → `slot.driveLog10.setTarget(...)` (`:3233`, `:3249`), the 50 ms
     `kDriveSmoothMs` log-domain smoother the header's note is about; **and**
  2. `cloudDriveGain()`, which returns `rmsGain_ * userDrive_` **completely unsmoothed** (`:3241-3244`)
     and is applied per sample at `:3406`/`:3420` as `bypassGain * cloudDrive * mono[s]`.

  `bypassGain` is *"EXACTLY 0 (`std::sin(0)`) while no bypass is engaged"* (`:3403-3405`), so
  consumer 2 is silent at the registered default of ID 812 (`kDefaultResonatorBypass = false`,
  `:164`) — which is exactly why SC-005 as constructed (one ID automated at a time, all others at
  registered defaults) **cannot reach it**. It becomes live whenever ID 812 is on, and during its
  10 ms un-bypass ramp, where a step on ID 804 or a toggle of ID 811 is an unsmoothed per-sample
  level step.

  **Therefore §7.6's render set carries a third edge combination, `kBodyResonatorBypassId = on`**,
  under which IDs 804 and 811 are re-measured. The classification for both is class (a) **with both
  consumers cited** (§3.5.3), and the remedy rule is one-directional as everywhere else: if the
  measurement finds a step, 804 and 811 move to class (b) — never an exemption, never a looser bound.
- **(iii)** SC-002 is unaffected: the registered default is **on**, which is the shipped state
  (`kDefaultAgcEnabled = true`, `:163`).

#### 1.5.4 Names that are deliberately *not* changed

Phase 7's existing bare `setDriftDepthCents` (cloud, `:647`) and `setDriftDepth` (atmosphere, `:690`)
are **not renamed** — surgical-changes rule. The new prefixes exist only where the bare name would be
ambiguous or would sit misleadingly beside the existing bare body facade (`:671-683`).

### 1.6 Fourteen read-back accessors (FR-072)

SC-003's `VP` / `MB` rules assert a pushed value against "the matching getter", and thirteen routed
IDs have none; SC-023 clause 4 additionally names "the soft-limit state" as an `ENG` read-back that
does not exist either (below). All fourteen additions are `[[nodiscard]] … const noexcept` pure
member reads of values the setters already store clamped.

**One on `SeraphisEngine`**, beside `setOutputSaturation` (`:566`):

```cpp
    /// FR-072 (extended 2026-08-01). The ONLY read-back for kSoftLimitId on
    /// SeraphisEngine. A PURE CONST FORWARDER - it adds NO state.
    ///
    /// Verified this session: SeraphisEngine ships setOutputSaturation (:566-570),
    /// which pushes satL_/satR_ and is the ONLY writer of either (satL_/satR_ at
    /// :1222-1223; prepare() pushes kOutputSaturation at :230-231). TapeSaturator
    /// ALREADY ships the read-back: `[[nodiscard]] float getSaturation() const
    /// noexcept { return saturation_; }` (processors/tape_saturator.h:283-285),
    /// and `saturation_` is written ONLY by setSaturation, as
    /// `saturation_ = std::clamp(amount, 0.0f, 1.0f)` BEFORE the smoother target
    /// (:248-252) - i.e. exactly "the amount last pushed, not the saturator's ramp
    /// position", which is the semantics SC-023 clause 4 needs. TapeSaturator::
    /// reset() snaps saturationSmoother_ to saturation_ and does not clear it
    /// (:180-199), so SeraphisEngine::reset() (:938-943) cannot desynchronise it
    /// either. The read-back therefore exists BY CONSTRUCTION; the only thing
    /// missing was a route to it through the engine's access wall.
    [[nodiscard]] float getOutputSaturation() const noexcept {
        return satL_.getSaturation();          // tape_saturator.h:283
    }
```

**No new member, and no second source of truth.** An earlier revision added a mutable
`outputSaturation_` float and had `prepare()` seed it — which is two writes, two places that must
stay in step, and precisely the divergence-from-the-saturator failure mode the accessor exists to
rule out. It was justified by a verification claim that is false: the cited
`grep -n "getOutputSaturation\|getSaturation"` over `seraphis_engine.h`,
`processors/tape_saturator.h` and `processor.h` returns `tape_saturator.h:283`. With the forwarder,
**`seraphis_engine.h`'s Phase 9 addition outside §1.1–1.3 is `const`-only**, matching the
`continuous_body.h` carve-out's own const-only rule (below). Nothing downstream changes: ID 2's two
pushed values (`kOutputSaturation = 0.15f`, `seraphis_engine.h:142`, and `0.0f`, `processor.cpp:250`)
both lie inside `TapeSaturator`'s `[0,1]` clamp, so SC-023 clauses 4 and 7(c) still read back
exactly.

**One on `SeraphisVoice`**, appended to the accessor family at `:763-771`:

```cpp
    /// SIXTH sub-component accessor. GrowthEnvelope::getDuration()
    /// (growth_envelope.h:149) is the only read-back for ID 701 and is
    /// unreachable without this.
    [[nodiscard]] const GrowthEnvelope& growth() const noexcept { return growth_; }
```

**Twelve on `ContinuousBody`** (the FR-071 carve-out), appended to the FR-007 introspection block
whose banner at `:1445-1447` says *"this list is EXHAUSTIVE"* — the banner is updated in the same
edit to say so of the extended list:

| New accessor | Returns | Backing member | Stored clamped at | Serves ID |
|---|---|---|---|---|
| `getResonance()` | `float` | `resonance_` (`:4206`) | `:1166` | 801 `VP` |
| `getDamping()` | `float` | `damping_` (`:4207`) | `:1175` | 802 `MB` |
| `getKeyTracking()` | `float` | `keyTracking_` (`:4208`) | `:1184` | 803 `VP` |
| `getDrive()` | `float` | `userDrive_` (`:4210`) | `:1205` | 804 `VP` |
| `getMix()` | `float` | `mix_` (`:4211`) | `:1214` | 805 `VP` |
| `getCloudMix()` | `float` | `cloudMix_` (`:4212`) | `:1224` | 806 `VP` |
| `getCloudDecaySec()` | `float` | `cloudDecaySec_` (`:4213`) | `:1235` | 807 `VP` |
| `getCloudSize()` | `float` | `cloudSize_` (`:4214`) | `:1246` | 808 `VP` |
| `getCloudDamping()` | `float` | `cloudDamping_` (`:4215`) | `:1259` | 809 `VP` |
| `getWidth()` | `float` | `width_` (`:4216`) | `:1269` | 810 `VP` |
| `isInputAgcEnabled()` | `bool` | `agcEnabled_` (`:4217`) | `:1278` | 811 `VP` |
| `isResonatorBypass()` | `bool` | `resonatorBypass_` (`:4218`) | `:1305` | 812 `VP` |

Three naming facts, all checked against the class's complete getter surface (`:1449-1537`) this
session:

- **`getDrive()` is deliberately distinct from the existing `getDriveGain()`** (`:1480-1484`), which
  returns `exp10Fast(driveLog10.getCurrentValue())` — the *smoothed derived engine* gain floored at
  `kMinDriveGain`, not the pushed user drive.
- **`isInputAgcEnabled()` / `isResonatorBypass()`** take the `is` prefix of the class's existing
  `isCrossfading()` (`:1504`) because they return `bool`.
- **`isResonatorBypass()` returns the requested state, not the ramp position.**
  `setResonatorBypass` stores `resonatorBypass_` immediately (`:1305`) and then ramps `bypassPos_`
  over 10 ms. SC-003 asserts the stored request; the ramp is SC-005's subject. An accessor returning
  `bypassPos_` would fail SC-003 for a correct implementation on the first block after the push.

**Because the setters clamp before storing, each accessor returns the *clamped* value.** Every C-6
plain range for these thirteen IDs lies inside the component's own clamp range
(`continuous_body.h:115-165`), so the two coincide for every value the parameter can produce, and
SC-003's exact-equality thresholds are satisfiable.

**The `continuous_body.h` carve-out is `const`-only.** No non-`const` behaviour is added to
`continuous_body.h`, no existing member moves, includes are unchanged, and the file stays Layer 3.
**§1.6 as a whole is `const`-only** — all fourteen accessors are pure member reads or one-line
forwarders, and none adds a member to any file. The compliance pass records the full list of touched
`dsp/` files (FR-071).

### 1.7 Documentation banners (FR-006)

Each of the **six groups / thirty-three public symbols** — (1) the POD, (2) `applyVoiceParams`,
(3) the three base-override methods, (4) `applySpectralStates`, (5) the thirteen forwarders,
(6) the **fourteen** accessors (one on `SeraphisEngine`, one on `SeraphisVoice`, twelve on
`ContinuousBody`) — carries the `@par Layer: 3 (systems/)` and `@par Real-Time Safety:` banners its
siblings already use, and introduces no Layer 4 include. The count moved from 32 to 33 with
`getOutputSaturation()` (§1.6); `applySpectralStates`' `voiceMask` (§1.3) is a defaulted parameter on
an existing symbol and does not move it.

---

## 2. Plugin-local additions (`namespace Seraphis`)

### 2.1 `plugins/seraphis/src/plugin_ids.h` (FR-010 – FR-013)

Four edits, all additive to the existing file.

**(a) The reserved-range comment (`:46-55`)** is rewritten: bands 200–1399 marked **SHIPPED (Phase 9)**,
1400+ left as Phase 10, and the stale pre-Phase-9 roadmap citation corrected to the band list's real
span (the "start at 0 with 100-ID gaps" decision is one line, the eight-band reserve list three
more) — C-5. FR-058 clause 2's sweep re-verified these numbers *after* the roadmap amendment landed,
because every citation after roadmap line 313 shifts (§9.1); **as applied 2026-08-01 they are roadmap
line 396 and roadmap lines 399–401**.

**(b) `enum ParameterIDs`** gains the 83 new IDs in band order, exactly as C-6 lists them. The eight
Phase 8 IDs (`:59-68`) are untouched.

**(c) Range-dispatch bounds (FR-011)**, extending the two that exist (`:79-80`) so FR-040's dispatch
stays a range ladder and never becomes a 91-case switch:

```cpp
constexpr Steinberg::Vst::ParamID kGlobalParamRangeEnd  =  100;  // existing
constexpr Steinberg::Vst::ParamID kMacroParamRangeEnd   =  200;  // existing
constexpr Steinberg::Vst::ParamID kCloudParamRangeEnd   =  400;
constexpr Steinberg::Vst::ParamID kMorphParamRangeEnd   =  600;
constexpr Steinberg::Vst::ParamID kLifeModParamRangeEnd =  800;  // 600-799: life mods + envelope
constexpr Steinberg::Vst::ParamID kBodyParamRangeEnd    = 1000;
constexpr Steinberg::Vst::ParamID kAtmosParamRangeEnd   = 1200;
constexpr Steinberg::Vst::ParamID kAetherParamRangeEnd  = 1400;
```

The Life-Modulator band carries **two** sub-blocks (600–699 orbit/width, 700–799 voice envelope) but
**one** pack and **one** range-end constant — C-5's confirmed placement.

**(d) State version (FR-012)**, expressed against symbols so FR-093's migration is not written against
literals:

```cpp
constexpr Steinberg::int32 kStateVersion1        = 1;  // Phase 8's 36-byte layout
constexpr Steinberg::int32 kCurrentStateVersion  = 2;  // Phase 9 (spec C-8), 2532 bytes
```

**(e) The frozen-type note (`:71-76`)** is extended to enumerate the registered type of **all 91** IDs,
grouped by type per C-9 — `R` plain `Vst::Parameter`, `L` `StringListParameter`, `T` stepped toggle
(`stepCount = 1`). The two new `T` toggles (811, 812) and the new `L` dropdowns (3, 403, 406, 408–412,
800, 1016) are named explicitly, because a `RangeParameter ↔ StringListParameter` swap at a live ID
breaks editor load in DAWs that cache parameter metadata.

### 2.2 `plugins/seraphis/src/parameters/dropdown_mappings.h` (FR-015)

New header, no new type: `inline constexpr` label tables and `inline` index↔enum converters. **Eight**
tables, one per dropdown, each read by BOTH registration and formatting so a label list cannot exist in
two places and drift.

| Table | Entries | Consumer |
|---|---|---|
| `kSeedLabels` / `kSeedValues` | 16 | ID 3 |
| `kTravelModeLabels` | 2 — `External`, `Spline` | ID 403 |
| `kSyncNoteLabels` / `kSyncNoteBeats` | 8 — C-7's table | IDs 406 (labels) + FR-056 (beats) |
| `kStateCountLabels` | 3 — `2`, `3`, `4` | ID 408 |
| `kSpectralStateLabels` | 5 — `Sine Stack`, `Bell`, `Choir`, `Glass`, `Breath` | IDs 409–412 |
| `kEnvelopeModeLabels` | 2 — `Standard`, `Growth` | ID 700 |
| `kBodyMaterialLabels` | 5 — `Glass`, `Strings`, `Metal Plate`, `Chamber`, `Ice` | ID 800 |
| `kGrainEnvelopeLabels` | 6 — `Hann`, `Trapezoid`, `Sine`, `Blackman`, `Linear`, `Exponential` | ID 1016 |

Two tables carry extra obligations:

```cpp
// --- grain envelope: declaration order of GrainEnvelopeType, tied to the count
//     AtmosphereEngine::prepare() generated windows for.
inline constexpr std::array<const Steinberg::Vst::TChar*, 6> kGrainEnvelopeLabels = {
    STR16("Hann"), STR16("Trapezoid"), STR16("Sine"),
    STR16("Blackman"), STR16("Linear"), STR16("Exponential")};
static_assert(kGrainEnvelopeLabels.size() == Krate::DSP::AtmosphereEngine::kEnvelopeTypeCount,
              "FR-015: an enum extension must not silently desynchronise the label list from "
              "the windows prepare() generates (atmosphere_engine.h:197, :427)");

// --- seed: C-10's CURATED, CHECKED-IN table. NOT `index + 1`.
//     Index 0 is PINNED to 1u so kEngineSeed == kReverbSeed == 1u
//     (engine/seraphis_engine_config.h:28-29) survives as the registered default,
//     which is what keeps SC-002's negative control and SC-022 valid.
inline constexpr std::array<std::uint32_t, 16> kSeedValues = {
    1u,          0x2F1B4C97u, 0x7A3E11D5u, 0xC4905E2Bu,
    0x18D7A063u, 0x9B26F84Fu, 0x5E4C39A1u, 0xE7B152DCu,
    0x3A81C7F6u, 0xD05F2A8Bu, 0x6C93E410u, 0xB2470D9Eu,
    0x0F5A86C3u, 0x8E31BD74u, 0x47C6285Au, 0xF9A0736Eu};
static_assert(kSeedValues[0] == 1u, "C-10 / SC-020 cl.3: the Phase 8 default must not drift");
inline constexpr std::array<const Steinberg::Vst::TChar*, 16> kSeedLabels = {
    STR16("Seed 1"), /* … */ STR16("Seed 16")};   // ORDINAL, never the raw constants
static_assert(kSeedLabels.size() == kSeedValues.size());
```

> **The sixteen constants above are a starting table, not a result.** SC-020 clause 2's gate is a
> **property of this table**: §7.11 renders all sixteen at the pinned operating point, records the
> pairwise seed-to-seed total-variation spread, and — if any pair is too close — **re-picks the
> offending constant and re-measures**. Lowering the gate is not an available remedy. The table is
> re-checked-in with the measured spread beside it.

Converters are plain `inline` functions with a bounds clamp, one pair per enum-backed table, e.g.

```cpp
[[nodiscard]] inline Krate::DSP::ContinuousBody::BodyMaterial toBodyMaterial(int index) noexcept {
    const int i = std::clamp(index, 0, static_cast<int>(Krate::DSP::ContinuousBody::kNumMaterials) - 1);
    return static_cast<Krate::DSP::ContinuousBody::BodyMaterial>(i);
}
[[nodiscard]] inline int fromBodyMaterial(Krate::DSP::ContinuousBody::BodyMaterial m) noexcept {
    return static_cast<int>(m);
}
```

`kSyncNoteBeats` is `std::array<double, 8>` holding C-7's `beatsPerJourney` in **beats**, with the four
bar-denominated entries stored as their **bar multiple** and multiplied by `barBeats` at use
(§3.6). It is the single transcription; FR-056 may not re-derive it.

### 2.3 The six parameter packs (FR-014, FR-016 – FR-019)

Six new headers in `plugins/seraphis/src/parameters/`: `cloud_params.h`, `morph_params.h`,
`life_mod_params.h`, `body_params.h`, `atmosphere_params.h`, `aether_params.h`. Each declares a
`struct <Section>Params` of `std::atomic<>` fields with initializers equal to the C-6 defaults and
exposes the **six-function contract** the Phase 8 packs already implement
(`global_params.h:72, 102, 129, 160, 167, 189`):

```
void handle<Section>ParamChange(<Section>Params&, Vst::ParamID, Vst::ParamValue);
void register<Section>Params(Vst::ParameterContainer&);
Steinberg::tresult format<Section>Param(Vst::ParamID, Vst::ParamValue, Vst::String128);
void save<Section>Params(const <Section>Params&, Steinberg::IBStreamer&);
bool load<Section>Params(<Section>Params&, Steinberg::IBStreamer&);            // EOF-safe
template <typename SetParamFunc>
void load<Section>ParamsToController(Steinberg::IBStreamer&, SetParamFunc);
```

Pack ownership of the 83 new IDs:

| Pack | IDs | Atomic fields |
|---|---|---|
| `global_params.h` (extended) | **3** `kSeedId` | +1 `std::atomic<int> seedIndex{0}` |
| `cloud_params.h` | 200–210 | 11 float |
| `morph_params.h` | 400–412 | 5 float, 4 int, 4 int (factory slots) |
| `life_mod_params.h` | 600–604, 700–704 | 9 float, 1 int |
| `body_params.h` | 800–812 | 10 float, 1 int, 2 bool |
| `atmosphere_params.h` | 1000–1016 | 15 float, 1 int, 1 bool |
| `aether_params.h` | 1200–1217 | 17 float, 1 bool |

`kSeedId` stays in the **global** pack for registration / handling / formatting (it is a 0–99 band ID),
but its **state** functions are separate and explicitly positioned — see §5.2 and FR-091a.

#### 2.3.0 The morph pack's two loaders are a NAMED exception to the six-function contract

`morph_params.h` is the one pack whose state block carries a payload that is not an atomic field: the
four 541-byte `SpectralState` records of C-8. FR-041b forbids putting a `SpectralState` inside an
atomic pack (a 540-byte object is not lock-free), so the payload's destination is passed in. Both
loaders are stated here **normatively**, because a silent divergence from the contract every other
pack implements is how the controller half gets forgotten:

```cpp
/// PROCESSOR side. THIRD PARAMETER is the named exception to the FR-014 contract:
/// the four payloads land in a Processor-owned staging buffer (plan section 3.7),
/// never in MorphParams, because FR-041b forbids a SpectralState inside an
/// atomic pack. Everything else - field order, EOF-safety, return value - is the
/// ordinary load<Section>Params shape.
bool loadMorphParams(MorphParams&, Steinberg::IBStreamer&,
                     std::array<Krate::DSP::SpectralState, 4>& destination);

/// CONTROLLER side. The signature IS the contract's, and the body MUST STILL
/// CONSUME the 4 x 541 = 2164 payload bytes - reading them into the same 541-byte
/// scratch and DISCARDING them - because the controller has nowhere to put a
/// SpectralState and no parameter to set from one.
template <typename SetParamFunc>
void loadMorphParamsToController(Steinberg::IBStreamer&, SetParamFunc);
```

**The discard loop is load-bearing, not tidiness.** `setComponentState` reads the C-8 blocks
sequentially from one `IBStreamer` (§4). A controller loader that read the morph pack's 13 scalar
fields and stopped would leave the cursor **2164 bytes short**, so `[life]`, `[body]`, `[atmos]` and
`[aether]` — 55 parameters — would each be read from the wrong offset. SC-010's controller-parity
clause would fail on 55 rows at once, and it would fail with garbage values rather than an obvious
truncation. §7.10 asserts the cursor position directly (below), so the failure is localized rather
than inferred from 55 wrong numbers.

#### 2.3.1 The four denormalization forms (FR-017, FR-018)

Exactly four, and no pack may invent a fifth:

| Map | Denormalize (`handle…`) | Normalize (`load…ToController`, registration default) |
|---|---|---|
| `lin [a,b]` | `clamp(a + v*(b-a), a, b)` | `(plain - a) / (b - a)` |
| `log [mn,mx]` | `Krate::Plugins::logMapFromNormalized(v, mn, mx)` (`parameter_helpers.h:80`) | `logMapToNormalized(plain, mn, mx)` (`:85`) |
| `L` (n entries) | `clamp(int(v*(n-1) + 0.5), 0, n-1)` | `index / double(n-1)` |
| `T` | `v >= 0.5` | `on ? 1.0 : 0.0` |

**FR-018 is not decorative.** Every `handle…` clamps into the C-6 plain range **before** storing.
An unclamped store would make FR-042's change detector fire forever — the exact failure
`clampPolyphony` was introduced for (`global_params.h:52-59`).

**Two mandatory construction rules, both driven by C-4 / SC-022's exact-equality gate.**

1. **Every `mn` / `mx` bound is written as `static_cast<double>(<the DSP constant>)`, never a re-typed
   literal.** e.g. ID 404's `mn` is `static_cast<double>(SpectralMorphEngine::kMinTravelRate)`
   (`:101`), not `1.0/600.0`; ID 1003's bounds are `AtmosphereEngine::kMinGrainSeconds` /
   `kMaxGrainSeconds` (`:299-300`). A re-typed literal is how a default drifts by one ULP and SC-022
   goes red.
2. **Every registered `defaultNormalizedValue` is computed from the plain default through the same
   mapping**, never hand-typed:
   `registerCloudParams` passes `logMapToNormalized(0.5, kMinDecaySec, kMaxDecaySec)` for ID 209, not
   `0.3247`. This closes the round trip SC-022 asserts.

*Round-trip analysis (why exact `==` is achievable).* `logMapFromNormalized`/`ToNormalized` compute in
`double`; the round-trip error is ~1e-15 relative, and the store casts to `float`, whose half-ULP is
~6e-8 relative. Every C-6 default therefore lands on the same `float` as its literal. Spot-checked:
2000 ms on `[1,10000]` → 1999.999999999999x → `2000.0f`; 0.5 s on `[0.05,60]` → 0.4999999999999999 →
`0.5f`; 2.0 s on `[0.5,30]` → 2.0000000000000004 → `2.0f`; 0.030 on `[0,0.1]` → 0.030000000000000002 →
`0.03f`; 0.20 on `[-1,+1]` → 0.19999999999999996 → `0.2f`; 1.0 s on `[0,30]` → 0.99999999999999989 →
`1.0f`. SC-022 is the gate, per ID, with no tolerance (§7.12).

#### 2.3.2 The 1 ms floor on IDs 702–704 is load-bearing

`logMapFromNormalized` is `clamp(mn * pow(mx/mn, u), mn, mx)` (`parameter_helpers.h:80-83`). At
`mn == 0` the ratio is `+inf`, `pow(+inf, u)` is `+inf` for every `u > 0`, and `0 * inf` is **NaN**,
which `std::clamp` propagates because both comparisons are false. Every non-zero normalized value on
those three parameters would denormalize to NaN; and because all three are `MB`-routed, FR-003's
`isFiniteBits` rejection would silently keep the `kRows` literal, leaving three parameters permanently
inert. **`mn = 1.0` ms on IDs 702, 703, 704.** `MultiStageEnvelope::setStageTime` / `setReleaseTime`
clamp to `[0, kMaxStageTimeMs]`, so 1 ms is inaudible at the DSP end.

The identical hazard is why **ID 1012 `kAtmosPositionId` is `lin`, not `log`** (its plain range starts
at 0, and a non-zero floor would make position 0 — grains born at the write head — unreachable).

#### 2.3.3 Dropdown registration (FR-016)

Every `L` parameter is registered through `Krate::Plugins::createDropdownParameterWithDefault`
(`parameter_helpers.h:47`, or the pointer+count overload at `:118` when the labels come from a
`dropdown_mappings.h` table — which is all of them). Never a hand-rolled `StringListParameter`, never a
`RangeParameter` with a step count. Dropdown IDs format themselves and MUST NOT be claimed by any
`format…Param` (FR-061).

#### 2.3.4 No pack reads the sample rate (FR-019)

Every time-domain parameter is stored in seconds, milliseconds, Hz, semitones or grains-per-second and
is converted to samples **inside** the DSP component that owns the rate. Mechanically checked in the
compliance pass by pasting the verbatim output of

```bash
grep -n "sampleRate\|sampleRate_\|getSampleRate" plugins/seraphis/src/parameters/*.h
```

An empty result set is the pass condition; a claim without the pasted output is not evidence.
FR-019 deliberately has no success criterion, and the Traceability table says so.

### 2.4 `applyAetherParams` (FR-049)

Added to `plugins/seraphis/src/engine/seraphis_engine_config.h` as a **free function** beside the
existing `applyAetherTargets` (`:93-103`), for the same stated reason: the reverb has no getters for
these ten controls, so a free function is the only surface a test can drive directly with non-neutral
values.

```cpp
/// @brief FR-049. Push the ten NON-macro reverb controls (spec C-6, route `AE`).
///
/// The eight MACRO-owned controls are applyAetherTargets' (:93); the two sets are
/// disjoint by construction (spec FR-055), so the two functions never fight.
///
/// @par Real-Time Safety: noexcept and allocation-free. FOURTEEN of AetherReverb's
///      eighteen setters funnel through applyControl (a clamp + smoother store,
///      :2950-2958); of the four that do not, exactly TWO are reached here -
///      setFreeze, a self-guarding latch (:2230-2237), and setModSmoothness,
///      which loops drift_[j].setSmoothness over 8 channels (:2268-2273). The
///      other two, setSizeBreathDepth (:2320) and setDimensionalityTideDepth
///      (:2328), are MB-routed and belong to applyAetherTargets
///      (seraphis_engine_config.h:101-102) - the two sets are disjoint, so the
///      count here is two and not three. That is exactly why spec C-3 calls this
///      ON CHANGE ONLY and not every slice.
inline void applyAetherParams(Krate::DSP::AetherReverb& reverb, const AetherParams& p) noexcept {
    reverb.setDensity(...);            // aether_reverb.h:2211   ID 1202
    reverb.setDecaySeconds(...);       // :2214                  ID 1203
    reverb.setFreeze(...);             // :2230                  ID 1204
    reverb.setDimensionality(...);     // :2239                  ID 1205
    reverb.setDamping(...);            // :2244                  ID 1206
    reverb.setPreDelayMs(...);         // :2247                  ID 1207
    reverb.setModDepth(...);           // :2254                  ID 1208
    reverb.setModSmoothness(...);      // :2268                  ID 1209
    reverb.setBloomDecay(...);         // :2301                  ID 1213
    reverb.setSpectralDiffusion(...);  // :2310                  ID 1214
}
```

`AetherReverb::setSeed` (`:2361`) is **not** here: it is `ENG`-routed and pushed from
`pushGlobalParams()` alongside `SeraphisEngine::setSeed` (FR-045).

---

## 3. Processor wiring

### 3.1 New `Processor` members (FR-041, FR-041a, FR-041b, FR-042)

Added to `plugins/seraphis/src/processor/processor.h` beside the Phase 8 members (`:76-98`):

```cpp
    // --- FR-041: one instance of each new pack, by value ---------------------
    CloudParams      cloudParams_{};
    MorphParams      morphParams_{};
    LifeModParams    lifeParams_{};
    BodyParams       bodyParams_{};
    AtmosphereParams atmosParams_{};
    AetherParams     aetherParams_{};

    // --- FR-041b: the ONLY readable source of the four spectral states -------
    // The five factory states, built ONCE IN THE CONSTRUCTOR - not in
    // setupProcessing() - IMMUTABLE thereafter and therefore readable from both
    // threads without synchronisation. makeFactoryState is documented
    // "Deterministic and stateless" and "CONFIGURATION-TIME, not audio-thread:
    // ... it evaluates ~200 std::pow/std::exp calls" (spectral_state.h:349-351,
    // :371-372), so it may not be called from processParameterChanges (§3.2) -
    // and it needs NO sample rate, which is why deferring it to prepare buys
    // nothing and costs the before-prepare window (§3.7).
    std::array<Krate::DSP::SpectralState, 5> factoryStates_ = makeFactoryStateTable();

    std::array<Krate::DSP::SpectralState, 4> spectralSlots_{};   // audio-thread-owned, §3.7
    // THREE staging buffers, not one: see §3.7's writer interlock. Two would let
    // a second setState() write the buffer the audio thread is copying.
    std::array<std::array<Krate::DSP::SpectralState, 4>, 3> spectralSlotsStaging_{};
    std::atomic<int> spectralSlotsHandoff_{-1};    // published buffer index, or -1
    std::atomic<int> spectralSlotsConsuming_{-1};  // buffer being copied, or -1
    int stagingWriteCursor_ = 0;                   // message-thread-only
    bool spectralStatesPending_ = false;                               // FR-046
    // FR-046: which voices have NOT yet accepted. Bit v selects voices_[v].
    std::uint16_t spectralRetryMask_ = 0u;

    // --- FR-042: two independent on-change generation-counter pairs ----------
    std::size_t voiceParamGeneration_            = 0;
    std::size_t lastAppliedVoiceParamGeneration_ = kGenerationSentinel;
    std::size_t aetherParamGeneration_           = 0;
    std::size_t lastAppliedAetherParamGeneration_= kGenerationSentinel;

    // --- FR-043 / FR-045 on-change trackers ---------------------------------
    std::array<float, Krate::DSP::SeraphisMacroMatrix::kNumTargets> lastPushedBase_{};
    bool lastPushedBaseValid_   = false;   // one flag, not 27 - pushAllSurfaces clears it
    Krate::DSP::SeraphisMacroValues lastPushedMacros_{};
    bool lastPushedMacrosValid_ = false;
    int  lastPushedSeedIndex_   = -1;      // sentinel: never a legal index
    std::array<int, 4> lastPushedSlotStateId_{-1, -1, -1, -1};  // §3.2 CFG change guard
    bool lastPushedFreeze_      = false;
    bool lastPushedFreezeValid_ = false;
    // (lastPushedPolyphony_ / lastPushedSoftLimit_ already exist, :91-92)
    bool lastPushedSoftLimitValid_ = false;

    // --- FR-047: the one force-push request, raised off the audio thread -----
    std::atomic<bool> forcePushAllPending_{false};

    // --- FR-059 class-(b) processor-side smoothing (§3.5) --------------------
    Krate::DSP::OnePoleSmoother resonanceSm_{0.7f};      // ID 801  (VP)
    Krate::DSP::OnePoleSmoother bodyDampingSm_{0.25f};   // ID 802  (MB)
    Krate::DSP::OnePoleSmoother breathDepthSm_{0.20f};   // ID 1215 (MB)
    Krate::DSP::OnePoleSmoother tideDepthSm_{0.20f};     // ID 1216 (MB)
    Krate::DSP::OnePoleSmoother macroSm_[5]{             // IDs 100-104
        OnePoleSmoother{0.0f}, OnePoleSmoother{0.0f}, OnePoleSmoother{0.0f},
        OnePoleSmoother{0.5f}, OnePoleSmoother{0.0f}};   // gravity neutral is 0.5
    bool snapParamSmoothers_ = true;   // armed by prepare / preset load
    bool paramSmootherBypass_ = false; // FR-059a probe seam; false in every shipping path
    // §3.5.4: the ABSOLUTE control-chunk phase. Continuous across slices AND
    // across process() calls, exactly like ContinuousBody's sampleCounter_ %
    // kControlChunkSamples (continuous_body.h:1392-1394, :1433). This is what
    // makes the class-(b) ramp block-size independent.
    std::uint64_t controlPhase_ = 0;
    // §3.5.4: latches the settling->settled transition so the EXACT target value
    // is pushed once. Without it the block on which advanceSamples() snaps
    // current_ = target_ (smoother.h:251-253) also makes isComplete() true
    // (:232-234), the settling clause goes false, the generation compare is
    // equal, and the voice is left permanently ~1e-4 short of target.
    bool wasVoiceClassBSettling_ = false;

    // --- FR-056 synced travel ------------------------------------------------
    float lastSyncedTravelRate_ = -1.0f;   // sentinel: below kMinTravelRate

    // --- FR-041a test-only counters (plain size_t; audio-thread-written) -----
    std::size_t applyVoiceParamsCalls_ = 0;
    std::size_t applySpectralStatesCalls_ = 0;
    std::size_t applySpectralStatesAttempts_ = 0;   // ATTEMPTS, incl. failed retries
    std::size_t applyAetherParamsCalls_ = 0;
    std::size_t setTargetBasePushes_ = 0;
    std::size_t spectralHandoffConsumes_ = 0;       // SC-023 clause 5
    // FR-045's four ENG values, one counter each. Nothing else can see their
    // cadence - see the note below.
    std::size_t engSeedPushes_       = 0;           // engine_->setSeed + reverb_->setSeed
    std::size_t engPolyphonyPushes_  = 0;           // (setPolyphonyCalls_ already exists, :92)
    std::size_t engSoftLimitPushes_  = 0;           // engine_->setOutputSaturation
    std::size_t engFreezePushes_     = 0;           // engine_->setAtmosphereFreeze
```

**Size budget.** Six packs ≈ 330 B; the `SpectralState` arrays are now **nine** 4-slot arrays worth
(one live + three staging + the five-entry factory table) = (4 + 12 + 5) × 540 = **11 340 B**
(`sizeof(SpectralState)` is 256 + 256 + 16 + 4 + 4 + 4 = 540; the **541** figure elsewhere is the
*serialized* form, which adds the format-version byte — `spectral_state.h:185`); 27-float base tracker
108 B, nine `OnePoleSmoother`s ≈ 180 B, counters/flags ≈ 165 B (the four FR-045 `ENG` counters added
32 B). Total ≈ **12.1 KiB** against the
`static_assert(sizeof(Processor) < 64 KiB)` at `processor.h:104`, which continues to hold with ~5×
headroom. The 771 968 B engine stays on the heap (`:76`). *(The budget grew from ≈ 5.1 KiB when §3.7's
writer interlock went from one staging buffer to three and §3.2's factory table was added; both are
recorded here rather than discovered by the `static_assert`.)*

**FR-041a test-only read surfaces**, under the existing banner *"Test-only read surfaces (NEVER called
from `process()`)"* (`:48-55`), in the shape of `setPolyphonyCallCountForTest()` (`:53-55`) —
each `[[nodiscard]] … const noexcept`:

```cpp
    std::size_t applyVoiceParamsCallCountForTest() const;      // SC-007: successful applications
    std::size_t applySpectralStatesCallCountForTest() const;   // SC-007: applications that CLEARED
    std::size_t applyAetherParamsCallCountForTest() const;     // SC-007: INVOCATIONS
    std::size_t setTargetBasePushCountForTest() const;         // SC-007: INVOCATIONS, so a
                                                               //   per-slice re-push is visible
    const Krate::DSP::SeraphisMacroMatrix& macroMatrixForTest() const;  // only route to getTargetBase
    bool spectralStatesPendingForTest() const;                 // SC-013
    const Krate::DSP::SpectralState& spectralSlotForTest(int slot) const;  // SC-012
    // --- two additions to FR-041a's list, 2026-08-01 ------------------------
    std::size_t spectralHandoffConsumeCountForTest() const;    // SC-023 clause 5
    std::size_t applySpectralStatesAttemptCountForTest() const;// SC-007: retry ATTEMPTS
    // --- FR-045's four ENG cadence counters (see below) ---------------------
    std::size_t engSeedPushCountForTest() const;
    std::size_t engPolyphonyPushCountForTest() const;          // == setPolyphonyCallCountForTest()
    std::size_t engSoftLimitPushCountForTest() const;
    std::size_t engFreezePushCountForTest() const;
```

**Two accessors are added to FR-041a's list, and the reason each is added is recorded here so the FR
creating the seam and the criterion consuming it stay paired** (the spec's standing rule that no
criterion may depend on introspection no requirement creates):

- `spectralHandoffConsumeCountForTest()` — SC-023 clause 5 requires asserting that
  `spectralSlotsHandoff_` *"was consumed exactly once — the staging copy reached `spectralSlots_` on
  the audio thread and not on the message thread"*. `spectralSlotsHandoff_` is private, and
  `spectralStatesPendingForTest()` cannot distinguish *consumed once* from *consumed twice* or from
  *copied on the message thread*. The counter is incremented at the single consume site in §3.3, so
  the test asserts it is **0 immediately after `setState()` returns** (the "not on the message thread"
  half) and **1 after one block** (the "exactly once" half).
- `applySpectralStatesAttemptCountForTest()` — SC-007 can otherwise only see FR-046 **successes**
  (`applySpectralStatesCalls_` is incremented on the clearing path only), so a retry that re-ran the
  whole fan-out every block for the length of a sustained note would ship green. The attempt counter
  is what bounds the repeated work; §7.7 asserts it against the retry-mask design of §3.3.
- **The four `ENG` push counters** — FR-045 requires the four `ENG` values (polyphony, soft limit,
  seed, atmosphere freeze) to be pushed *"on change only"*, and **nothing verifies that cadence**
  without them. FR-041a's seam list and the counter block above create counters for `applyVoiceParams`,
  `applySpectralStates`, `applyAetherParams` and `setTargetBase` only; SC-007's table has no `ENG` row;
  FR-045 has no Traceability row at all. A regression to a per-block `ENG` re-push therefore fails no
  planned assertion — SC-008's steady-state arm times only *"the tracker comparisons, the settled-check
  and the synced-rate comparison"* (§7.8), and `SeraphisEngine::setSeed` is deterministic per call
  (`seraphis_engine.h:352-357` re-derives from `deriveStreamSeed`), so a redundant re-push is not
  visible in SC-002's render either. The four counters are in the shape of the shipped
  `setPolyphonyCallCountForTest()` (`processor.h:53-55`), and §7.7 carries the one row that consumes
  them. `engPolyphonyPushCountForTest()` is a **named alias** of the existing counter, not a second
  one, so no Phase 8 assertion moves. *This is also what makes §3.3's force-push ordering fix and
  §3.4's `SurfaceInvalidation::PresetLoad` arm testable: without the seed counter, a `setState()` that
  re-seeds the engine for an unchanged seed is unobservable.*

### 3.2 `processParameterChanges` (FR-040)

The existing ladder (`processor.cpp:549-554`) is extended, preserving both established behaviours:
the **last point** of each queue is taken (`getPoint(numPoints - 1)`, `:544`), and an ID outside every
band is **ignored** rather than misrouted.

```cpp
        const Vst::ParamID id = queue->getParameterId();
        if (id < kGlobalParamRangeEnd) {
            handleGlobalParamChange(globalParams_, id, value);
            if (id == kSeedId) { /* ENG tracker handles it; no generation bump */ }
        } else if (id < kMacroParamRangeEnd) {
            handleMacroParamChange(macroParams_, id, value);
        } else if (id < kCloudParamRangeEnd) {
            handleCloudParamChange(cloudParams_, id, value);
            markDirty(id);
        } else if (id < kMorphParamRangeEnd) {
            handleMorphParamChange(morphParams_, id, value);
            markDirty(id);
        } else if (id < kLifeModParamRangeEnd) {
            handleLifeModParamChange(lifeParams_, id, value);
            markDirty(id);
        } else if (id < kBodyParamRangeEnd) {
            handleBodyParamChange(bodyParams_, id, value);
            markDirty(id);
        } else if (id < kAtmosParamRangeEnd) {
            handleAtmosphereParamChange(atmosParams_, id, value);
            markDirty(id);
        } else if (id < kAetherParamRangeEnd) {
            handleAetherParamChange(aetherParams_, id, value);
            markDirty(id);
        }
        // else: an ID outside every shipped range - ignored.
```

`markDirty(id)` is a single private helper that owns the route classification in **one** place:

```cpp
// FR-042. The route table lives HERE and nowhere else, so C-6's routing cannot be
// restated (and desynchronised) at three call sites.
void Processor::markDirty(Vst::ParamID id) noexcept {
    switch (routeOf(id)) {                       // constexpr, from C-6
        case Route::VP:  ++voiceParamGeneration_;  break;
        case Route::MB:  break;                              // see below: NO bump
        case Route::AE:  ++aetherParamGeneration_; break;
        case Route::CFG: refreshSpectralSlotFromFactory(id);  break;
        case Route::ENG:                                     // pushGlobalParams' own trackers
        case Route::Local: break;
    }
}
```

`routeOf` is a `constexpr` function over the C-6 table (a switch on ID, grouped by band), and the same
table drives the SC-003 / SC-009 / SC-023 test fixtures.

**`MB` deliberately bumps NO generation counter, and that is a correction.** An earlier revision wrote
`case Route::MB: ++voiceParamGeneration_;` on the ground that "MB is pushed pre-slice too". It is
pushed pre-slice, but by `pushMacroSurfaces()` (§3.5.5), which change-detects on `lastPushedBase_[]`
and **never reads `voiceParamGeneration_`**. The bump was therefore purely spurious, and it made every
deep `MB` edit — 27 IDs, including the five macro-adjacent cloud controls users automate most — run
`applyVoiceParams`, i.e. **37 setters × 16 voices = 592 setter calls it does not need**. FR-042 states
the counter is incremented *"whenever a **VP**-routed parameter is written"* (spec `:1168-1176`) and
justifies the two separate counter pairs precisely *"so an AE change does not force a 37-setter ×
16-voice fan-out and vice versa"*; the bump contradicted both. SC-007's two separation clauses would
not have caught it — they forbid only AE→`applyVoiceParams` and VP→`applyAetherParams` — so §7.7
carries a **third** separation clause: *an `MB`-only change MUST NOT increment `applyVoiceParams`*.

**`CFG` no longer builds a factory state on the audio thread**, and it guards on the slot actually
moving:

```cpp
// FR-041b. `id` is one of 409-412. Reads the pack's already-clamped atomic for
// that slot and copies 540 B out of the prepare-time table - NO transcendentals.
void Processor::refreshSpectralSlotFromFactory(Vst::ParamID id) noexcept {
    const int slot = spectralSlotIndexOf(id);                       // 0..3, constexpr
    const int stateId = morphParams_.slot[slot].load(std::memory_order_relaxed);
    if (stateId == lastPushedSlotStateId_[slot]) {
        return;                        // unchanged automation point costs nothing
    }
    lastPushedSlotStateId_[slot] = stateId;
    spectralSlots_[slot] = factoryStates_[clampFactoryIndex(stateId)];  // plain POD copy
    spectralStatesPending_ = true;
    spectralRetryMask_ = 0xFFFFu;                                   // §3.3
}
```

An earlier revision called `makeFactoryState(id)` (`spectral_state.h:373`) here, justified only on
thread-ownership grounds. That is a contract violation, not a style point: the function's own banner
reads *"CONFIGURATION-TIME, not audio-thread: allocation-free, lock-free and exception-free, but it
evaluates **~200 `std::pow`/`std::exp` calls**"* (`spectral_state.h:371-372`), and each call also runs
`normalizeSpectralState` over up to 64 partials. Nothing debounced it — `handle…ParamChange` stores
and `markDirty` fires on **every delivered queue point** — so a host automating IDs 409–412 would
re-run ~200 transcendentals per changed slot per block, indefinitely, inside
`processParameterChanges`, a region **neither SC-008 arm measures** (§7.8 times only the pre-slice
push block). The five states are precomputed once in `setupProcessing()` into `factoryStates_`
instead; `makeFactoryState` is documented *"Deterministic and stateless"* (`spectral_state.h:349-351`),
so the table is exactly what the per-change call would have produced.

`spectralSlots_` remains audio-thread-**owned** and is now written only by this function and by §3.3's
handoff consume. The message thread never reads it — §3.7 states why and what `getState()` reads
instead.

### 3.3 The pre-slice push block in `process()` (FR-042 – FR-047)

Phase 8's `process()` order is preserved exactly: `processParameterChanges` at the top (`:307`), the
five shape guards (`:320-342`), the not-ready silence path (`:345-350`), `pushGlobalParams()` (`:358`),
the master-gain snap/target (`:367-377`), then the event-driven slice loop (`:387-434`). Phase 9
inserts **two** blocks, both run **once per `process()` call and never per slice**:

**(A) The force-push consume, placed IMMEDIATELY AFTER the not-ready silence path (`:345-350`) and
BEFORE `pushGlobalParams()` (`:358`).** The position is normative, not incidental. An earlier
revision put it after `pushGlobalParams()`, which produces a gratuitous re-seed one block later:
`pushAllSurfaces()` invalidates the four `ENG` trackers (`lastPushedSeedIndex_`,
`lastPushedPolyphony_`, `lastPushedSoftLimitValid_`, `lastPushedFreezeValid_`, §3.4), but on that
block `pushGlobalParams()` has *already* run and has already pushed any genuinely changed `ENG` value
through the ordinary on-change path. The sentinels then survive into the **next** block, where
FR-045's code below sees `seedIndex != -1` and re-runs `engine_->setSeed(seed)` and
`reverb_->setSeed(seed)` for a seed that did not change — `SeraphisEngine::setSeed` re-derives all
sixteen voice seeds (`seraphis_engine.h:352-357`) and `AetherReverb::setSeed` (`:2361-2364`) is
documented *"Mid-render this is therefore a discontinuity in the drift and tide"* (`:2351-2358`),
which is the exact property that puts these two on the on-change-only path and exempts `kSeedId` from
SC-005 clauses 1–3. `setPolyphony()` is likewise re-run for an unchanged value, walking the
allocator's excess-slot loop and re-targeting `sumGain_` (`seraphis_engine.h:321-350`) and moving
`setPolyphonyCallCountForTest()`, which Phase 8 tests assert. With the consume **above**
`pushGlobalParams()`, the invalidation and the single `ENG` re-push happen in the same block and
nothing is re-pushed on the next one.

```cpp
    // ---- FR-047: consume the off-audio-thread force-push request ------------
    // ABOVE pushGlobalParams() - see the note above. PresetLoad, not Reprepared:
    // setState() wrote the atomics before the release store, so the ordinary
    // atomic-vs-tracker compare already delivers a CHANGED seed, and forcing an
    // UNCHANGED seed through setSeed() is a discontinuity this plan rules out
    // everywhere else (§3.4).
    if (forcePushAllPending_.exchange(false, std::memory_order_acquire)) {
        pushAllSurfaces(SurfaceInvalidation::PresetLoad);   // §3.4
    }
```

**(B) The remaining pre-slice work, between `pushGlobalParams()` and the master-gain read:**

```cpp
    // §3.7 steps 3-4. `spectralSlotsConsuming_` is stored BEFORE the handoff is
    // cleared; that ORDER is the whole interlock (§3.7 proves it).
    const int handoff = spectralSlotsHandoff_.load(std::memory_order_acquire);
    if (handoff >= 0) {
        spectralSlotsConsuming_.store(handoff, std::memory_order_release);
        spectralSlotsHandoff_.store(-1, std::memory_order_release);
        spectralSlots_ = spectralSlotsStaging_[static_cast<std::size_t>(handoff)];  // 2.1 KiB POD
        spectralSlotsConsuming_.store(-1, std::memory_order_release);
        spectralStatesPending_ = true;
        spectralRetryMask_ = 0xFFFFu;
        ++spectralHandoffConsumes_;           // SC-023 clause 5
    }

    // ---- FR-056 / C-3 amendment 2: tempo is sampled ONCE per process() ------
    updateSyncedTravelRate(data.processContext);   // §3.6; may bump voiceParamGeneration_

    // ---- FR-044: the 10 AE values, on change only --------------------------
    pushAetherParamsIfDirty();

    // ---- FR-046: the 5 CFG values, while pending ---------------------------
    pushSpectralStatesIfPending();

    // ---- FR-059(b): the class-(b) smoother TARGETS, ONCE per process() ------
    // §3.5.4 proves why this may NOT live inside advanceParamSmoothers(): the
    // slice loop reads anyClassBSmootherUnsettled() BEFORE it advances, so a
    // target set inside the advance leaves the predicate looking at the previous
    // target on the first slice after every change, no subdivision happens, and
    // 93.0 % of the step is delivered in one push. Hoisting is valid for the same
    // stated reason the master-gain target hoist is (processor.cpp:360-367):
    // processParameterChanges() ran at the top of process() and took the LAST
    // point of every queue, so no atomic can change within a process() call.
    setParamSmootherTargets();
```

**The two class-(b)-carrying pushes moved into the slice loop, and nothing else did.**
`advanceParamSmoothers(...)`, `pushMacroSurfaces()` and `pushVoiceParams()` run **per sub-slice** on
§3.5.4's absolute 64-sample grid rather than once per `process()`, because a value pushed before a
512-sample render applies to all 512 samples and no once-per-block push can produce a ramp (§3.5.2).
`pushAetherParamsIfDirty()` and `pushSpectralStatesIfPending()` stay **once per `process()` call and
never per slice**: no `AE` ID is class (b) (§3.5.3), and the `CFG` push is configure-time and gated.

When every class-(b) smoother is settled — ordinary playback — the slice loop runs Phase 8's slice
structure unchanged, so the two moved pushes execute exactly once per `process()` and the cadence
SC-007 counts is identical to the once-per-block form.

`renderSlice()`'s **body** is unchanged: `macros_.apply(*engine_)` and
`applyAetherTargets(*reverb_, macros_.computeAetherTargets())` stay at `:623-624`, every slice — that
is how a base override reaches the voices, and FR-059 idempotence is documented at
`seraphis_macro_matrix.h:150-153`. Steps 3–6 of the chain (`:627-676`) do not move. Only the slice
*length* rule changes, and only while un-settled (§3.5.4).

The three on-change pushes, in full:

```cpp
void Processor::pushVoiceParams() noexcept {
    const bool settling = anyVoiceClassBSmootherUnsettled();     // §3.5.4
    // THE THIRD CLAUSE IS NOT REDUNDANT. advanceParamSmoothers() runs BEFORE
    // this (the §3.3 order), and OnePoleSmoother::advanceSamples snaps
    // current_ = target_ on the chunk it converges (smoother.h:251-253), at
    // which point isComplete() is already true (:232-234). Without the latch the
    // converging chunk is skipped - the generation compare is equal and
    // `settling` is false - and the engine keeps the PREVIOUS chunk's value.
    // Worked example at 48 kHz, tau = 4 ms, a 0.7 -> 1.0 step on ID 801: the
    // pushes are ... 0.99986, 0.99990, and then the converging chunk is dropped,
    // leaving the voice permanently ~1e-4 short of target until some unrelated
    // VP parameter moves - which breaks §7.4's exact-equality read-back for the
    // ID 801 row by construction. (pushMacroSurfaces has no such bug: it
    // compares per-target VALUES, not a generation counter, so the exact final
    // value differs from lastPushedBase_[t] and is pushed.)
    if (voiceParamGeneration_ == lastAppliedVoiceParamGeneration_
        && !settling && !wasVoiceClassBSettling_) {
        return;
    }
    engine_->applyVoiceParams(buildVoiceParams());               // FR-002
    lastAppliedVoiceParamGeneration_ = voiceParamGeneration_;
    wasVoiceClassBSettling_ = settling;
    ++applyVoiceParamsCalls_;
}

void Processor::pushAetherParamsIfDirty() noexcept {
    if (aetherParamGeneration_ == lastAppliedAetherParamGeneration_) {
        return;                                                  // NO settling clause: no AE
    }                                                            //   ID is class (b) (§3.5.3)
    applyAetherParams(*reverb_, aetherParams_);                   // FR-049
    lastAppliedAetherParamGeneration_ = aetherParamGeneration_;
    ++applyAetherParamsCalls_;
}

void Processor::pushSpectralStatesIfPending() noexcept {
    if (!spectralStatesPending_) {
        return;
    }
    // PER-VOICE acceptance, not whole-pool. See the note below.
    std::array<std::uint32_t, kMaxVoices> before{};
    for (std::size_t v = 0; v < kMaxVoices; ++v) {
        before[v] = engine_->getVoice(v).getRejectedConfigureTimeCallCount();  // :720
    }
    engine_->applySpectralStates(spectralSlots_.data(),
                                 morphParams_.stateCount.load(std::memory_order_relaxed),
                                 spectralRetryMask_);            // FR-005, §1.3
    ++applySpectralStatesAttempts_;                              // SC-007 bounds retries
    for (std::size_t v = 0; v < kMaxVoices; ++v) {
        const std::uint16_t bit = std::uint16_t{1} << v;
        if ((spectralRetryMask_ & bit) == 0u) {
            continue;
        }
        if (engine_->getVoice(v).getRejectedConfigureTimeCallCount() == before[v]) {
            spectralRetryMask_ &= static_cast<std::uint16_t>(~bit);   // this voice accepted
        }
    }
    if (spectralRetryMask_ == 0u) {                              // FR-046 clause 3
        spectralStatesPending_ = false;
        ++applySpectralStatesCalls_;                             // SC-007 counts SUCCESSES
    }
    // else: leave the flag set and retry NEXT BLOCK, to the remaining voices only.
    // The parameter atomics are NEVER touched in response to a rejection (FR-046
    // clause 4).
}
```

The per-voice counters are read through the const `getVoice()` (`seraphis_engine.h:696`) —
no new engine surface is needed.

**Why the retry is per-voice and not whole-pool.** An earlier revision compared the *total* rejection
count across `kMaxVoices` and, on any rise, re-ran the entire fan-out next block. That is a per-block
cost with no upper bound: `applySpectralStates` writes all four slots to all sixteen voices
unconditionally (§1.3), and on a **quiescent** voice the gate passes (`seraphis_voice.h:706-712`) and
`SpectralMorphEngine::setState` runs `isValidSpectralState` **plus** `buildSanitized` — a full
64-entry `std::log2` pass (`spectral_morph_engine.h:296-301`, `:537-543`) — **before** the identity
check at `:302-304` that would make it a no-op. So with one voice held and fifteen idle the retry cost
15 × 4 × 64 ≈ **3840 `std::log2` per block, every block**, and `isFinished()` is false for the whole of
a sustained note plus its release (up to 8000 ms at the shipped default, `seraphis_voice.h:359`) —
indefinitely under continuous playing. No criterion could see it: SC-007 counted only *successes*, and
SC-008's steady-state arm is defined with nothing pending.

With the mask, a voice that accepted is never written again, so the ongoing cost is proportional to
the voices still rejecting; when the whole pool is sounding the cost is the **gate only**, which is
the cheap path. **No retry interval is introduced**, so SC-013 clause 3 keeps its exact wording — the
retry succeeds on the *first* block after the last voice becomes quiescent, not within some interval.
*(The one thing that would change that wording is §7.8's conditional per-block fan-out bound, which is
why it carries a pre-drafted spec amendment, A9, rather than being adopted here.)*
`applySpectralStatesAttemptCountForTest()` is what lets §7.7 bound the repeated work.

**FR-045: the four `ENG` values** are pushed from `pushGlobalParams()` (`:585-603`), extending the
existing on-change tracker pattern with the two new ones:

```cpp
    const int seedIndex = globalParams_.seedIndex.load(std::memory_order_relaxed);
    if (seedIndex != lastPushedSeedIndex_) {                 // ON CHANGE ONLY
        const std::uint32_t seed = kSeedValues[clampSeedIndex(seedIndex)];   // C-10
        engine_->setSeed(seed);                              // seraphis_engine.h:353
        reverb_->setSeed(seed);                              // aether_reverb.h:2361
        lastPushedSeedIndex_ = seedIndex;
    }
    const bool freeze = atmosParams_.freeze.load(std::memory_order_relaxed);
    if (!lastPushedFreezeValid_ || freeze != lastPushedFreeze_) {
        engine_->setAtmosphereFreeze(freeze);                // seraphis_engine.h:551
        lastPushedFreeze_ = freeze;
        lastPushedFreezeValid_ = true;
    }
```

Both `setSeed` calls are documented **configure-time** (`spectral_morph_engine.h:198-207`,
`aether_reverb.h:2345-2358`), which is exactly why they are on-change only and why `kSeedId` is
exempt from SC-005 clauses 1–3.

### 3.4 `pushAllSurfaces()` (FR-047, Q2) — one sequence, two entry points

**The sequence is pure invalidation, and it runs on the audio thread (or with the audio thread
stopped). It never touches the DSP from the message thread.** FR-047's own parenthetical equates
"force-push" with "resetting the `lastPushed*` trackers to a sentinel", and that reading is the only
one that is thread-safe: `setState()` can legally run concurrently with `process()` (the *Edge cases*
→ *State* bullet says so, and pluginval's stress paths do it), so writing ~40 tracker scalars and
calling engine setters from `setState()` would be a data race on every one of them.

```cpp
/// FR-047. THE shared sequence. Two callers, ONE body - a divergence between the
/// prepare-time seeding and the preset-load seeding is the failure this exists to
/// prevent. Called (a) from setupProcessing(), with the audio thread stopped, and
/// (b) from process(), when it observes forcePushAllPending_.
///
/// The ONE parameter distinguishes the two SITUATIONS, not two bodies: after a
/// re-prepare the DSP objects really were re-initialised and every surface must
/// be re-pushed unconditionally; after a preset load the atomics carry the new
/// values and the ordinary compare-against-tracker path delivers whatever
/// changed. Two pushes are gratuitous DISCONTINUITIES when forced with an
/// unchanged value, and both are on the Reprepared-only list below.
enum class SurfaceInvalidation { Reprepared, PresetLoad };

void Processor::pushAllSurfaces(SurfaceInvalidation scope) noexcept {
    ++voiceParamGeneration_;
    ++aetherParamGeneration_;
    lastAppliedVoiceParamGeneration_  = kGenerationSentinel;   // cannot compare equal
    lastAppliedAetherParamGeneration_ = kGenerationSentinel;
    lastPushedBaseValid_        = false;      // forces all 27 setTargetBase pushes
    lastPushedMacrosValid_      = false;
    lastPushedSoftLimitValid_   = false;
    lastPushedFreezeValid_      = false;
    lastSyncedTravelRate_       = -1.0f;
    snapParamSmoothers_         = true;       // a preset load SNAPS; it does not ramp
    wasVoiceClassBSettling_     = false;      // §3.3 latch; the snap makes it moot
    lastPushedSlotStateId_.fill(-1);          // §3.2: force the next CFG compare to miss

    // --- Reprepared-ONLY invalidations ------------------------------------
    // engine_->setSeed re-derives all sixteen voice seeds (seraphis_engine.h:
    // 352-357) and AetherReverb::setSeed is documented "Mid-render this is
    // therefore a discontinuity in the drift and tide" (:2351-2358);
    // setPolyphony walks the allocator's excess-slot loop and re-targets
    // sumGain_ (:321-350) and moves setPolyphonyCallCountForTest(), which Phase
    // 8 tests assert. On the PresetLoad path setState() wrote both atomics
    // BEFORE the release store, so the ordinary compare in pushGlobalParams()
    // already delivers a changed value and forcing an unchanged one is a
    // discontinuity for nothing. On the Reprepared path the engine genuinely
    // lost both, so both must be re-pushed.
    if (scope == SurfaceInvalidation::Reprepared) {
        lastPushedSeedIndex_ = -1;
        lastPushedPolyphony_ = kPolyphonySentinel;   // 0: never a legal clamped value
    }

    // --- The spectral fan-out is raised ONLY when a slot actually differs ---
    // Raising it unconditionally costs 16 voices x 4 slots = 64 buildSanitized
    // calls = 4096 std::log2 in ONE process() call, on EVERY setState() and
    // EVERY setupProcessing(), even when nothing changed - see §1.3 and §7.8.
    // The guard is §3.2's, applied here: compare each slot's factory id against
    // lastPushedSlotStateId_ BEFORE it is cleared above.
    // (Implementation note: capture the four ids into a local before the
    // .fill(-1) above, or run this block first; both are stated so the ordering
    // is not rediscovered at implementation time.)
    const bool slotsMoved = anySpectralSlotIdChanged();   // vs the pre-fill ids
    if (scope == SurfaceInvalidation::Reprepared || slotsMoved) {
        spectralStatesPending_ = true;        // pushes NOTHING spectral itself
        spectralRetryMask_     = 0xFFFFu;     // §3.3: every voice must accept again
    }
    // else: every voice already holds the identical sanitized state, and
    // SpectralMorphEngine::setState would run isValidSpectralState +
    // buildSanitized on all 64 (voice, slot) pairs before reaching the identity
    // check at spectral_morph_engine.h:302-304 that makes it a no-op.
}

// setState(): the ONLY thing the message thread writes.
void Processor::requestPushAllSurfaces() noexcept {
    forcePushAllPending_.store(true, std::memory_order_release);
}
```

**The `setState()` path still delivers new spectral slots**, because a preset that changes them
publishes a staging buffer and §3.3's handoff consume raises `spectralStatesPending_` and the full
mask on its own — that path is unconditional and is unaffected by the guard above. The guard only
removes the case where `pushAllSurfaces()` would have re-sanitized four unchanged states into sixteen
voices for nothing.

**When the raise IS genuine the whole-pool fan-out still happens in one block, and that is what
§7.8's worst-case arm measures.** 16 voices × 4 slots = 64 `buildSanitized` calls = **4096
`std::log2`** plus 64 `isValidSpectralState` scans and 64 × 128-float array comparisons, inside the
0.50 %-of-block ceiling FR-057 clause 1 sets (53.3 µs at 512/48 kHz). That is not obviously
achievable, so §7.8's worst-case arm carries an explicit **one-directional remedy** — the same shape
§3.5.2 and §3.5.4 already carry, so a breach has a stated route and does not become an implementation
choice or a relaxed ceiling:

> **Remedy if the measured worst case breaches 0.50 %.** Bound the per-block fan-out:
> `pushSpectralStatesIfPending()` writes at most `kSpectralFanOutVoicesPerBlock` voices per block
> (4 is the first value to try), clearing their bits from `spectralRetryMask_` as they accept, so a
> whole-pool raise is amortised over `ceil(16 / k)` blocks. **Raising the 0.50 % ceiling is not an
> available remedy, and neither is dropping the identity guard's `Reprepared` arm.** The bound
> interacts with **SC-013 clause 3** — "on the **first** block after every voice has become
> quiescent" becomes "within `ceil(16 / k)` blocks" — so adopting it is a **spec amendment to
> SC-013**, written into `spec.md` under §12's amendment step and not logged as a deviation.

**`snapParamSmoothers_ = true` is required, not cosmetic.** SC-023 clause 4 asserts every route's
read-back after **one** block. With a 20 ms class-(b) ramp and a 512-sample block, an un-snapped
smoother reaches ~93 % of target after one block and the exact-equality read-back for IDs 801, 802,
1215, 1216 would fail for a correct implementation. A preset load is not automation; it snaps.

**`setupProcessing()`'s one documented exception.** Phase 8 deliberately seeds
`lastPushedPolyphony_ = engine_->getPolyphony()` after `prepare()` so the first `process()` does **not**
re-call `setPolyphony()` — which would re-arm `sumGain_` (`seraphis_engine.h:349`) and walk the
allocator's excess-slot loop on every host prepare, and would move
`setPolyphonyCallCountForTest()`, which Phase 8 tests assert. `prepare()` has *already* installed the
parameter's polyphony (it is passed through `makeSeraphisEngineConfig`, `processor.cpp:210-215`), so
the value is delivered. The order is therefore:

```cpp
    engine_->prepare(...);  reverb_->prepare(...);      // :215-216, unchanged
    pushAllSurfaces(SurfaceInvalidation::Reprepared);   // FR-047: the one sequence
    lastPushedPolyphony_ = engine_->getPolyphony();     // Phase 8's exception, ONE line, :234
    lastPushedSoftLimit_ = globalParams_.softLimit.load(std::memory_order_relaxed);
    lastPushedSoftLimitValid_ = true;
```

(The Phase 8 exception line overrides the `Reprepared` arm's polyphony sentinel for this caller only,
exactly as it did before; on the `PresetLoad` path the sentinel is not raised at all and polyphony
still reaches the engine, because `setState()` wrote `globalParams_.polyphony` before the release
store and `pushGlobalParams()`'s ordinary compare sees the new value. SC-023's `kPolyphonyId = 16`
row is what proves it.)

This is still "one helper, not two code paths": the helper body is one function with one `if` over a
two-value enum, and the exception is a single, commented line *after* it.

**This contradicts FR-091 / FR-047 as currently written, so `spec.md` is AMENDED — amendment A4 of
§12.1, applied in §11 step 0, before any code.** FR-091 says `setState` *"MUST then call FR-047's
`pushAllSurfaces()`"* (spec `:1450-1455`) and FR-047 names `setupProcessing()` and
`Processor::setState()` as *"its only two callers"* (`:1230-1232`). Under this design `setState()`
calls **`requestPushAllSurfaces()`** — a single release store — and the helper body runs from
`process()`. The thread-safety argument above is the reason, and it stands: writing ~40 tracker
scalars from the message thread while `process()` reads them is a data race on every one of them.
A4 rewrites both FRs to *"raises a single release-store request consumed at the top of `process()`,
before `pushGlobalParams()`"* and adds the `SurfaceInvalidation` scope; **it is not logged as a
deviation**, because a logged deviation would leave the compliance pass grading the implementation
against FR text the plan deliberately violates. §7.13 clause 6's negative control is worded against
the seam this design actually has (`requestPushAllSurfaces()` stubbed out of `setState()`), not
against a `pushAllSurfaces()` call in `setState()` that does not exist.

`setState()` calls `requestPushAllSurfaces()` after the loaders return (FR-091). It also fixes the
Phase 8 residual recorded at `processor.cpp:237-249`: because `pushAllSurfaces()` invalidates the
soft-limit tracker and `setupProcessing()` re-seeds it *after* the explicit
`engine_->setOutputSaturation(...)` at `:250`, the prepare-time saturation push keeps its Phase 8
behaviour and no second push is added.

### 3.5 FR-059: parameter-push continuity

#### 3.5.1 The two classes

- **(a) component-internal** — the target component already smooths, ramps, gates or snapshots the
  pushed value, and Phase 9 adds nothing.
- **(b) processor-side** — `Processor` smooths the **pushed plain value** with
  `Krate::DSP::OnePoleSmoother` before it reaches the setter, and **delivers it on the engine's own
  absolute 64-sample control-chunk grid**, not once per `process()` call. Plugin-side, so no `dsp/`
  change and no FR-071 carve-out.

  **The delivery grid is the load-bearing half, and an earlier revision got it wrong.** That revision
  claimed class-(b) smoothing was "in exactly the shape `masterGain_` already uses
  (`processor.cpp:260`, `:374-377`, `:642`)" while advancing the smoothers with
  `advanceSamples(blockSamples)` once per `process()` and reading `getCurrentValue()` once per block.
  Those are not the same shape. `masterGain_` is advanced **once per output sample**, inside
  `renderSlice`'s sample loop (`plugins/seraphis/src/processor/processor.cpp:641-645`,
  `const float g = masterGain_.process();`), and the shipped comment immediately above it
  (`:633-636`) states the rule verbatim: *"ONCE PER OUTPUT SAMPLE … Never `advanceSamples(n)` and
  never once per slice — a ramp advanced per slice is partition-dependent BY CONSTRUCTION."*

  A once-per-block read is not a ramp, it is a **block-rate staircase**, and the numbers say how bad:
  with `kParamSmoothMs = 20.0f` the one-pole tau is `20/5000 s = 4 ms = 192 samples` at 48 kHz
  (`smoother.h:77-93` — the argument is time-to-99 % = 5·tau), so at the pinned 512-sample block the
  **first block boundary delivers `1 − e^(−512/192) = 93.0 % of the whole step in a single jump**, and
  at the 2048-sample bound (`seraphis_engine_config.h:40`) it delivers **99.99 %**. Two consequences,
  both fatal to the criteria this mechanism exists to satisfy:
  1. it removes essentially none of the discontinuity SC-005 clause 3 measures for IDs 801/802/1215/
     1216 and 100–104; and
  2. SC-005's mandatory positive control (b) would compare a 1.000·D step (probe bypass) against a
     0.930·D step (smoothed) — a ratio of **1.075** against a **1.5 ×** bound — so the criterion-wiring
     control would be **structurally incapable of failing**, which is exactly the defect §7.6 exists to
     rule out.

  It was also host-block-size dependent, which `processor.cpp:634-636` names as a defect in so many
  words.

#### 3.5.2 The grid, the time constant, and the derived `N`

**The grid: `kControlChunkSamples = 64`, on an ABSOLUTE phase counter.** 64 is not a new number — it
is the shared control clock every component in this engine already runs on
(`SeraphisEngine::kControlChunkSamples`, `seraphis_engine.h:132`; `SeraphisVoice`'s, same value;
`ContinuousBody::kControlChunkSamples = 64`, `continuous_body.h:97`, with the
`static_assert(kControlChunkSamples == 64, "A-5: the Phase 7 shared control clock")` at `:630`).
Critically, it is also the grid on which every **class-(a)** smoother this criterion compares against
is advanced: `ContinuousBody::controlStep` runs `keyTrackSmoother_ / mixSmoother_ / cloudMixSmoother_
/ cloudSizeSmoother_ / cloudDampLog2Smoother_ / widthSmoother_ / fbLSmoother_ / fbRSmoother_
.advanceSamples(kControlChunkSamples)` (`continuous_body.h:3713-3744`) on the absolute grid
(`sampleCounter_ % kControlChunkSamples == 0`, `:1433`). Putting class (b) on the same grid with the
same time constant is what makes SC-005's matched-regime bound compare like against like.

`controlPhase_` (§3.1) is an absolute sample counter that is continuous across slices **and** across
`process()` calls, so the ramp is **block-size independent by construction** — the property
`processor.cpp:634-636` demands and a per-block advance cannot have.

**The cadence: `process()` caps its slice length at `kControlChunkSamples` while — and only while —
any class-(b) smoother is un-settled.** The pushes that deliver these values are pre-slice
(`applyVoiceParams`) and per-slice (`macros_.apply` / `applyAetherTargets` at `processor.cpp:623-624`),
so 64-sample delivery is reachable only by subdividing the slice. This is **not** the per-slice ramp
C-3 forbids: a slice boundary is event-driven and varies with MIDI placement, whereas this grid is a
fixed absolute 64-sample rule that a host's block size and a performer's timing cannot move. FR-044 is
satisfied unchanged — `macros_.apply(*engine_)` and `applyAetherTargets(...)` still run **every slice**
at their existing positions, and steps 3–6 of the chain (`:627-676`) do not move.

When every class-(b) smoother is settled — the steady state, i.e. all of ordinary playback — the slice
structure is **exactly Phase 8's** and there is no subdivision and no extra cost. SC-008's
steady-state arm therefore measures the same thing it did before.

**The time constant — ONE number:**

```cpp
/// FR-059(b) clause 2, PER-ID COLUMN FORM. OnePoleSmoother's argument is
/// TIME-TO-99 % (primitives/smoother.h:77-93), so tau = ms/5000 s.
inline constexpr float kParamSmoothMs = 20.0f;        // 801, 802
inline constexpr float kAetherDepthSmoothMs = 300.0f; // 100-104, 1215, 1216
```

> **AMENDED 2026-08-01, from the step-13 measurement this section itself demanded.** This block
> originally fixed **one** number, `kParamSmoothMs = 20.0f`, under the note that the one-directional
> remedy for a surviving step is to lengthen it. SC-005 was then run and **ID 1215 breached the bound
> at 1.817 ×**, so the remedy was exercised — and the sweep showed the *single-number* form cannot
> work, which is why FR-059(b) clause 2's **second** form (a per-ID column of
> `kContinuityMechanism[]`) is what ships. The two families differ by an order of magnitude because
> their consumers do:
>
> - **Body coefficients (801, 802).** `resonance_`/`damping_` reach `b1_eff`/`b3_eff`/T60.
>   `continuous_body.h:2545-2558` states that a step there "changes a decay slope … neither is a
>   discontinuity in the output, because `sinState_`/`cosState_` carry through untouched", and the one
>   gain path (`engineDriveFor` → `slot.driveLog10`) rides the 50 ms `kDriveSmoothMs`. **Measured:**
>   801 = 1.044 × smoothed and **1.045 × with FR-059a's probe SNAPPING it**; 802 = 1.167 × both ways.
>   20 ms is retained (the values genuinely are stored raw, which is the class-(b) test) and nothing
>   is bought by lengthening it.
> - **Aether depths (1215, 1216) and the five macros that reach them.** `sizeBreathDepth_` scales a
>   live [−1,+1] modulator into the **smoothed** Size *before* the **exponential** S(v) mapping
>   (`aether_reverb.h:3036-3055`), and the product **is** `effectiveDelay_[i]`, the delay-line read
>   length, consumed raw at `:4256`. A stair in the depth is a read-pointer jump of **many samples**,
>   so the discontinuity **saturates**: it stops shrinking with the stair once the jump exceeds ~1
>   sample. **Measured ratio for ID 1215 against its own reference:** 20 ms → 1.817, 60 ms → 2.297,
>   100 ms → 1.847, 200 ms → 1.172, **300 ms → 1.126**, 500 ms → 1.093, with the raw statistic
>   *identical* at 300 and 500 ms (0.003258 vs 0.003257). 300 ms is the knee, and it is not a fitted
>   number: it is exactly `AetherReverb::kSizeSmoothingMs` (`aether_reverb.h:2731`), the component's
>   own smoothing time for the quantity these depths modulate. **The rule that generalises it:** a
>   class-(b) time constant must be at least the component's own smoothing time for the quantity it
>   modulates — which is what makes 1215 provably equivalent to **ID 1201 (Size)**, the class-(a) ID
>   that rides that very smoother and passes SC-005.
>
> `kContinuityMechanism[]` gains a `float smoothMs` column carrying these numbers per row, gated by
> `Seraphis_ContinuityMechanism_CoversEveryInScopeId`: populated on exactly the nine class-(b) rows,
> one of exactly the two shipped constants, 0 elsewhere, and the split itself asserted so a silent
> collapse back to one number fails there rather than at the next SC-005 run.

**Per-chunk delivery fraction.** `1 − e^(−64/192) = 0.2835` at 48 kHz. That is the largest single jump
the mechanism can produce, against **1.000** with FR-059a's probe bypassing it: a ratio of
**3.53 ×**, comfortably outside SC-005 clause 3's `1.5 ×` bound, so §7.6's positive control (b) **can**
fail — which is the property the old design lacked. *If the measurement in step 13 shows the bypassed
render does not in fact breach the bound, the one-directional remedy is to **lengthen
`kParamSmoothMs`** (50 ms, `ContinuousBody::kDriveSmoothMs`, is the next value in the same family, and
takes the per-chunk fraction to 0.125 and the ratio to 8 ×) and re-measure. Loosening the bound is not
an available remedy, and neither is deleting the control.*

**The derived `N` SC-007 counts.** `OnePoleSmoother::isComplete()` is
`|current − target| < kCompletionThreshold`, with `kCompletionThreshold = 1.0e-4f`
(`smoother.h:55`, `:232-234`). For a step of plain span `D`:

```
N_chunk = ceil( tau * ln(D / kCompletionThreshold) / chunkSeconds ),  tau = <the row's own constant> / 5000 s
```

Every class-(b) ID has `D = 1.0` (§3.5.3: all four deep IDs and all five macros are `[0,1]` spans), and
`chunkSeconds = 64/48000 = 1.3333 ms`. **Since the time constant is now a per-ID column, so is `N`** —
one pair per family, not one pair overall:

```
BODY family (801, 802), kParamSmoothMs = 20 ms, tau = 4 ms:
  stair   = 1 - e^(-64/192)                = 0.2835
  t       = 4 ms * ln(1 / 1e-4)            = 36.84 ms
  N_chunk = ceil(36.84 / 1.3333)           = 28    <- PUSH-COUNT bound SC-007 asserts
  N_block = ceil(36.84 / 10.6667)          = 4     <- WALL-CLOCK bound in 512-sample blocks

AETHER-DEPTH family (100-104, 1215, 1216), kAetherDepthSmoothMs = 300 ms, tau = 60 ms:
  stair   = 1 - e^(-64/2880)               = 0.0220
  t       = 60 ms * ln(1 / 1e-4)           = 552.62 ms
  N_chunk = ceil(552.62 / 1.3333)          = 415
  N_block = ceil(552.62 / 10.6667)         = 52
```

**The body family is `N_chunk = 28` / `N_block = 4` — unchanged, and it is the pair §7.4's class-(b)
`VP` read-back rows (801, 802) render against.** The aether-depth family is `N_chunk = 415` /
`N_block = 52`; its rows are all `MB`, so the counter they move is `setTargetBase` (one target,
one scalar store) and never the 37 × 16 `applyVoiceParams` fan-out — which is the reason the split
is worth having rather than raising the single number to 300 ms for everything. §7.4's 1215/1216
rows already render 40 s and 60 s, far past 52 blocks, so no read-back row moves. SC-007's class-(b)
rows assert `1 ≤ Δ ≤ N_chunk` **of that row's own family** on the **invocation** counters **and**
that the counter stops rising. SC-008's "settled" steady state is unaffected: it is measured with
every smoother settled, the state in which this clause does no work at all.

**This contradicts FR-042 amendment 1**, which says the settling push *"runs **once per block** … it
is **per block, never per slice**"* (spec `:1179-1183`, `:1388-1395`), **so `spec.md` is AMENDED —
amendments A1, A2 and A3 of §12.1, applied in §11 step 0, before any code is written.** Amendment 1
and FR-059(b) are in direct conflict as written: FR-059(b) mandates *"exactly the shape `masterGain_`
already uses"*, and `masterGain_`'s shape is per-output-sample (`processor.cpp:641-645`), which a
once-per-block push cannot approximate at any time constant that also settles in a musically usable
time. A1 rewrites the cadence to the absolute 64-sample control-chunk grid (keeping the per-**slice**
prohibition, which is about event-driven boundaries and is honoured here); A2 removes FR-059(b)'s
`masterGain_` wording, which no push-based surface can satisfy; A3 splits SC-007's single `N` into
`N_chunk = 28` (push counts) and `N_block = 4` (render lengths), which the criterion currently
conflates. **None of this is logged as a deviation** — a test written from the unamended spec would
fail a plan-conformant implementation, which is exactly what step 0 exists to prevent.

#### 3.5.3 `kContinuityMechanism[]` — the full classification

The checked-in array lives in the SC-005 test TU and has **one row per in-scope ID** (85 rows: 91
registered, less `kSeedId` and the five `CFG` IDs). The table below groups IDs that share an identical
evidence citation; the array does not.

```cpp
struct ContinuityRow {
    Steinberg::Vst::ParamID id;
    enum class Class : std::uint8_t { ComponentInternal, ProcessorSmoothed } cls;
    enum class Evidence : std::uint8_t { Smoother, Ramp, SnapshotAtBirth,
                                         CoefficientOnly, PhaseContinuous, Structural } why;
    const char* citation;     // file:line, MANDATORY
};

/// The row count is an ASSERTION, not a claim in prose. An earlier revision said
/// "85 rows" over a table that enumerated 83 - IDs 1 and 2 had no row, no class
/// and no evidence, and ID 1 is the most plausible discontinuity in the set.
static_assert(std::size(kContinuityMechanism) == 85,
              "SC-005: 91 registered, less kSeedId and the five CFG IDs");
```

The TU additionally runs a **set** check, because a count alone cannot catch a duplicated row paired
with a missing one: the ID set of `kContinuityMechanism[]` must equal
(registered IDs) − {`kSeedId`} − {408, 409, 410, 411, 412}, asserted in both directions against the
same `getParameterInfo` enumeration §7.3 uses, with no ID appearing twice.

| IDs | Class | Evidence | Citation |
|---|---|---|---|
| 0 | (a) | Smoother | `masterGain_` 20 ms, `processor.cpp:260`, `:642` |
| **1** | (a) | Smoother | `setPolyphony`'s only level move is `sumGain_.setTarget(sumGainForPolyphony(n))` (`seraphis_engine.h:349`) into the `kSumGainSmoothMs` smoother (`:218-244`). **That constant is 100 ms, raised from 20 ms on 2026-08-01 by SC-005's own measurement** — `sumGain_` is read once and HELD for a whole control chunk (`:1079-1080`) for SC-014's partition invariance, so it reaches the bus as a STAIRCASE, and at 20 ms the first stair of a polyphony 1 → 2 change (sumGain 1.0 → 0.7071) was 8.3 % of the bus level in one sample: **this row measured 2.651 × against the 1.5 × bound**. Sweep: 20 ms → 2.651, 100 ms → 1.143, 200 ms → 1.143, 300 ms → 1.144, 500 ms → 1.147, i.e. 100 ms is the knee at which the staircase disappears under the render's own floor. Nothing about the delivery shape changed, so SC-014 is untouched, and `prepare()` still snaps (`:353`). The force-idle at `:344` is `voices_[i].noteOff()` **only** — a *musical release*, not a retirement, so the orphan keeps rendering its release envelope (`:334-348`, and §1.2's bound depends on the same reading); it is a note-off, not a cut. Both halves are in scope for clauses 1–3 and neither is exempt |
| **2** | (a) | Smoother | `setOutputSaturation` → `TapeSaturator::setSaturation`, whose post-prepare branch targets `saturationSmoother_` rather than snapping (`processors/tape_saturator.h:248-252`); the prepare-time `kDefaultSmoothingMs = 5.0f` residual is recorded at `processor.cpp:237-249` and is a prepare artefact, not an automation step |
| **100–104** | **(b)** | — | the macro values reach `BodyDamping` / `AetherSizeBreathDepth` / `AetherDimensionalityTideDepth`, which are themselves class (b); smoothing the five knobs covers every macro row uniformly (`seraphis_macro_matrix.h:180`) |
| 200, 202, 203 | (a) | Smoother | kernel-amplitude smoother, `kAmpSmoothTimeSec = 0.002f`, `harmonic_cloud.h:164` |
| 201, 204, 205 | (a) | PhaseContinuous | partial **frequency** controls; the oscillator bank is phase-continuous, so a ratio step is not a sample discontinuity (`harmonic_cloud.h:191`, `:214`, `:482`) |
| 206 | (a) | CoefficientOnly | rewrites the detune AR(1) coefficients only, leaves the walk value untouched, `harmonic_cloud.h:513-525` |
| 207 | (a) | Smoother | per-partial pan gains ride the same kernel-amplitude smoother, `harmonic_cloud.h:164` |
| 208, 209, 210 | (a) | Structural | envelope **times**; the envelope value continues from where it stands, `harmonic_cloud.h:1611-1616` |
| 400, 401 | (a) | Smoother | entropy amp/cents smoothers `kEntropyAmpSmoothMs` / `kEntropyCentsSmoothMs`, and the per-chunk bound `kMaxAmpDeltaPerChunk = 0.025f` (`spectral_morph_engine.h:133`, `:174-187`) |
| 402 | (a) | Ramp | slew-limited by `advanceTravel`'s `travelRate_ * (numStates_ - 1) * dt` cap, `spectral_morph_engine.h:716-725` |
| 403, 404, 407 | (a) | Structural | travel **mode / rate / waypoint interval** — rate-domain, no value step (`:345`, `:358`, `:385`) |
| 405, 406 | (a) | Structural | processor-local; they only re-derive ID 404's pushed rate (§3.6) |
| 600, 601, 602, 603 | (a) | Smoother | orbit output reaches the audio path only through `gainLSm_` / `gainRSm_`, configured at `kSpatialSmoothMs`, `seraphis_voice.h:378-379`, `:1047-1057` |
| 604 | (a) | Smoother | `ms_.setWidth(widthPct_)` targets `MidSideProcessor::widthSmoother_`, `midside_processor.h:133-136`; `seraphis_voice.h:1051-1053` |
| 700 | (a) | Structural | `setEnvelopeMode` rewrites stage **times** and preserves the shadow, `seraphis_voice.h:567-578` |
| 701–704 | (a) | Structural | envelope durations; `MultiStageEnvelope` recomputes the increment and never re-writes the current value |
| 800 | (a) | Ramp | `setMaterial` self-guards and **crossfades**, `continuous_body.h:1122-1157` |
| **801** | **(b)** | — | `resonance_` stored **raw** (`:1166`, member `:4206`) and read directly at the control step; absent from the ten-smoother list at `:4222-4230` |
| **802** | **(b)** | — | `damping_` stored **raw** (`:1175`, member `:4207`); likewise absent from `:4222-4230`. `MB`-routed → the settling push is `setTargetBase` |
| 803, 805, 806, 808, 810 | (a) | Smoother | `keyTrackSmoother_`, `mixSmoother_`, `cloudMixSmoother_`, `cloudSizeSmoother_`, `widthSmoother_`, `continuous_body.h:4222-4228`; targets set at `:1185`, `:1215`, `:1225`, `:1247`, `:1270` |
| 804 | (a) | Smoother | **Two consumers, both cited (§1.5.3).** (i) `engineDriveFor()` → `slot.driveLog10.setTarget(...)` (`:3233`, `:3249`), the 50 ms `kDriveSmoothMs` log-domain smoother (`:169`), read back by `getDriveGain()` at `:1480-1484`; (ii) `cloudDriveGain()` = `rmsGain_ * userDrive_` (`:3241-3244`), **unsmoothed**, applied per sample at `:3406`/`:3420` as `bypassGain * cloudDrive * mono[s]`. Consumer (ii) is silent while `bypassGain` is exactly 0 (`:3403-3405`), i.e. at ID 812's registered default; §7.6's third edge combination (`kBodyResonatorBypassId = on`) is what measures it |
| 807, 809 | (a) | Smoother | derived feedback gain (`fbLSmoother_`/`fbRSmoother_`, `:4229-4230`) and `cloudDampLog2Smoother_` (`:4227`); set at `:1236`, `:1260` |
| 811 | (a) | Smoother | *"Absorbed by the drive smoother, so toggling is clickless"*, `continuous_body.h:1274-1276` — **that citation covers the ENGINE path only**. `agcEnabled_` selects `rmsGain_` (`:3686`, `:4000`), which feeds the same two consumers as ID 804: the `driveLog10` smoother **and** the unsmoothed `cloudDriveGain()` (`:3241-3244`). Measured under §7.6's third edge combination for the same reason (§1.5.3(ii)) |
| 812 | (a) | Ramp | its own **10 ms equal-power ramp** at the control step, plus the un-bypass waveguide re-tune, `continuous_body.h:1281-1321` |
| 1000, 1001 | (a) | Smoother | `levelSmoother_` / `blurSmoother_`, `atmosphere_engine.h:2322`, `:2339-2340` |
| 1002, 1003, 1005, 1006, 1010, 1011, 1012, 1013, 1014, 1015, 1016 | (a) | SnapshotAtBirth | read (or snapshotted) at grain birth, so no live grain moves: `atmosphere_engine.h:798-834` (*"SNAPSHOTTED at birth"*, *"Read at birth"*), `:850-856`, `:952-961` |
| 1004 | (a) | PhaseContinuous | `driftLanes_.depth` is live over the whole bank (`:836-839`) but scales a **pitch**; the grain oscillators are phase-continuous |
| 1007 | (a) | Ramp | `freezeMixRamp_.setTarget(...)`, a `LinearRamp`, `atmosphere_engine.h:882-885` |
| 1008 | (a) | Ramp | engine-level latch; the release path is a one-hop fade arm, `seraphis_engine.h:549-560` |
| 1009 | (a) | CoefficientOnly | `updateDriftCoefficients()` only, `atmosphere_engine.h:844-847` |
| 1200–1203, 1205–1208, 1210–1214, 1217 | (a) | Smoother | **fourteen** IDs, matching the fourteen cited setters, all funnelling through `AetherReverb::applyControl` — a clamp plus a smoother target (`aether_reverb.h:2950-2958`) — at `:2208`, `:2211`, `:2214`, `:2239`, `:2244`, `:2247`, `:2254`, `:2280`, `:2285`, `:2295`, `:2301`, `:2310`, `:2333`, `:2336`. **1204 is excluded from this range on purpose**: an earlier revision wrote `1200–1208` here, which asserts something false about a shipped setter — `setFreeze` (`:2230-2237`) does **not** call `applyControl`, and 1204's own Ramp row below is its only classification |
| 1204 | (a) | Ramp | `setFreeze` is a self-guarding latch onto the `kFreezeLatchMs = 50 ms` `freezeRamp_`, `aether_reverb.h:2218-2236` |
| 1209 | (a) | CoefficientOnly | forwards to `BrownianDrift::setSmoothness` on 8 channels — rewrites tau, leaves the walk value untouched, `aether_reverb.h:2258-2273` |
| **1215** | **(b)** | — | `sizeBreathDepth_` is a **direct unsmoothed member store** (`:2320-2322`) that scales a live `[-1,+1]` modulator added to Size *before* the S(v) mapping — a depth step is a delay-length step. `MB`-routed |
| **1216** | **(b)** | — | `tideDepth_` is a **direct unsmoothed member store** (`:2328-2330`) scaling a live `[-1,+1]` modulator added to Dimensionality before the `[0,1]` clamp. `MB`-routed |

**Class (b) is exactly nine IDs: 100, 101, 102, 103, 104, 801, 802, 1215, 1216.** All nine have plain
span `D = 1.0` (the macros and 801/802/1215/1216 are all `[0,1]`), which is what makes §3.5.2's
`N_chunk = 28` / `N_block = 4` a single pair of numbers rather than a per-ID column.

**The remedy rule is one-directional.** If SC-005 finds a step on some class-(a) ID, the in-scope
remedy is to move that ID **into class (b)** — a plugin-side `OnePoleSmoother` on the pushed value —
never to exempt the ID and never to loosen SC-005 clause 3's `1.5 ×` bound. An ID may not be moved
*into* class (a) without a file:line citation of the smoother that covers it.

#### 3.5.4 The settling push (C-3 amendment 1, FR-042 amendment 1)

**The slice loop gains one rule, and `process()`'s structure is otherwise Phase 8's.** The existing
event-driven slice loop (`processor.cpp:387-434`) already computes each slice's length from the next
event offset; while any class-(b) smoother is un-settled it additionally caps that length at the
**distance to the next absolute control-chunk boundary**, which is at most 64 samples:

```cpp
// process(), inside the slice loop. The clamp shape is ContinuousBody's own
// (continuous_body.h:1392-1394): distance to the next ABSOLUTE 64-sample
// boundary, so the grid does not restart at a block or a MIDI event.
std::size_t n = samplesToNextEventOrEndOfBlock;
if (anyClassBSmootherUnsettled()) {
    const auto toGrid = kControlChunkSamples
                      - static_cast<std::size_t>(controlPhase_ % kControlChunkSamples);
    n = std::min(n, toGrid == 0 ? kControlChunkSamples : toGrid);
}
advanceParamSmoothers(n);      // BEFORE the pushes, so this sub-slice carries the new value
pushVoiceParams();             // §3.3, on-change / while-settling
pushMacroSurfaces();           // §3.5.5, per-target
renderSlice(n);                // UNCHANGED body: macros_.apply + applyAetherTargets + steps 3-6
controlPhase_ += n;
```

Two properties this shape has and the per-block form did not: the grid is **absolute** (so two hosts
using 512 and 2048 get the same ramp), and the subdivision is **conditional** (so settled playback —
all of ordinary use — has Phase 8's exact slice structure and Phase 8's exact cost).

**`setParamSmootherTargets()` is NOT called from here, and that placement is the whole mechanism.**
An earlier revision made it `advanceParamSmoothers()`'s first statement. That is a defect that
silently reinstates the block-rate staircase §3.5.1/R7a exist to eliminate, and it does so on
**exactly the blocks that matter** — the first block after any target change. The chain, all four
links read this session:

1. the slice loop evaluates `anyClassBSmootherUnsettled()` **before** it calls
   `advanceParamSmoothers(n)` (the code above);
2. on the first slice after `processParameterChanges` latched a new value, no smoother has been
   given the new target yet, so every class-(b) smoother still has `current_ == target_` (the *old*
   value) — nothing else in §3.3's pre-slice block sets a smoother target;
3. `OnePoleSmoother::isComplete()` is `std::abs(current_ - target_) < kCompletionThreshold`
   (`smoother.h:232-235`), which is therefore **true**, `anyClassBSmootherUnsettled()` returns
   `false`, and `n` is left at the full slice length;
4. `advanceParamSmoothers(512)` then sets the target and immediately runs
   `advanceSamples(512)` (`smoother.h:243-256`) in one step. At `kParamSmoothMs = 20.0f` the
   coefficient is `exp(-5000/(20·48000)) = 0.994805` (`smoother.h:105-114`), so
   `coeff^512 = 0.0697` — **93.0 % of the step in a single push**, and 99.998 % at the 2048-sample
   bound. That is the identical figure §3.5.2 cites as fatal, and it collapses §7.6's positive
   control (b) from 3.53 × back to ~1.075 × against a 1.5 × bound, i.e. structurally incapable of
   failing. Lengthening `kParamSmoothMs` does **not** repair it (`coeff^512` at 50 ms is 0.344,
   still a 65.6 % jump); only the ordering does.

**Therefore `setParamSmootherTargets()` runs ONCE per `process()`, in §3.3's pre-slice block, before
the slice loop.** That is valid for exactly the reason the master-gain target hoist at
`processor.cpp:360-367` is valid and states verbatim: `processParameterChanges()` ran at the top of
`process()` and took the **last** point of every queue, so no atomic can change within a `process()`
call and a per-slice re-target would push the identical value. With the targets set before the loop,
the first slice's `anyClassBSmootherUnsettled()` already sees the new target and caps `n` to the
64-sample grid — which is what §3.5.2's 28.35 %-per-chunk figure assumes.

```cpp
// Advanced by the SUB-SLICE's own sample count, so the ramp is wall-clock correct
// whatever the host does. advanceSamples() is the O(1) closed form
// (smoother.h:243-256), so advancing by n once equals advancing by n/2 twice up to
// float rounding.
//
// IT DOES NOT SET TARGETS. setParamSmootherTargets() runs once per process(), in
// the section 3.3 pre-slice block, BEFORE the slice loop reads
// anyClassBSmootherUnsettled(). Calling it here would make the predicate see a
// stale target on the first slice after every change - see above.
void Processor::advanceParamSmoothers(std::size_t sliceSamples) noexcept {
    if (snapParamSmoothers_ || paramSmootherBypass_) {
        for (OnePoleSmoother* s : classBSmoothers()) { s->snapToTarget(); }   // :257
        snapParamSmoothers_ = false;
        return;
    }
    for (OnePoleSmoother* s : classBSmoothers()) { s->advanceSamples(sliceSamples); }
}

/// @par Real-Time Safety: allocation-free, lock-free, exception-free. Returns a
///      FIXED-SIZE array BY VALUE - nine raw pointers to members of `*this`, one
///      per class-(b) ID (section 3.5.3). The return type is pinned here because
///      this is the hottest new audio-thread path in the phase (twice per
///      sub-slice, up to 32 times per 2048-sample block while settling) and a
///      range-returning helper with an unstated type is where an implementer
///      would reach for std::vector and allocate per sub-slice. SC-006 would
///      catch that, but only after the fact and only if the settling window
///      overlaps the measured render.
[[nodiscard]] std::array<Krate::DSP::OnePoleSmoother*, 9> classBSmoothers() noexcept;

/// @par Real-Time Safety: allocation-free, lock-free, exception-free. Nine
///      setTarget() calls (smoother.h:170) from the already-clamped atomics.
///      Called ONCE per process(), from the section 3.3 pre-slice block.
void setParamSmootherTargets() noexcept;

[[nodiscard]] bool Processor::anyVoiceClassBSmootherUnsettled() const noexcept {
    return !resonanceSm_.isComplete();               // the ONLY class-(b) VP row (ID 801)
}
// PER-TARGET, not one flag for all 27 - see §3.5.5.
[[nodiscard]] bool Processor::targetClassBUnsettled(SeraphisMacroTarget t) const noexcept {
    switch (t) {
        case SeraphisMacroTarget::BodyDamping:  return !bodyDampingSm_.isComplete();
        case SeraphisMacroTarget::AetherSizeBreathDepth:        return !breathDepthSm_.isComplete();
        case SeraphisMacroTarget::AetherDimensionalityTideDepth:return !tideDepthSm_.isComplete();
        default: return false;                       // the other 24 targets are class (a)
    }
}
[[nodiscard]] bool Processor::anyMacroSmootherUnsettled() const noexcept { /* macroSm_[0..4] */ }
[[nodiscard]] bool Processor::anyClassBSmootherUnsettled() const noexcept {
    return !resonanceSm_.isComplete() || !bodyDampingSm_.isComplete()
        || !breathDepthSm_.isComplete() || !tideDepthSm_.isComplete()
        || anyMacroSmootherUnsettled();              // drives the SUBDIVISION only
}
```

`buildVoiceParams()` reads `resonanceSm_.getCurrentValue()` (`smoother.h:191`) for `bodyResonance`, and
the raw clamped atomic for the other 36 fields. `pushMacroSurfaces()` reads
`bodyDampingSm_ / breathDepthSm_ / tideDepthSm_.getCurrentValue()` for targets `BodyDamping`,
`AetherSizeBreathDepth`, `AetherDimensionalityTideDepth`, and `macroSm_[i].getCurrentValue()` for the
five macro knobs.

`configure(kParamSmoothMs, sampleRate_)` for all nine runs in `setupProcessing()` beside
`masterGain_.configure(...)` (`processor.cpp:260`).

**The cost of the subdivision is measured, not assumed.** Under continuous automation of a class-(b)
ID the smoother is essentially never settled, so 512-sample blocks are rendered as eight 64-sample
sub-slices for the duration. §7.8 therefore carries a **third** SC-008 arm — the *class-(b) settling
arm* — with its own `ceil(worst × 1.05)` checked-in baseline. If it breaches, the one-directional
remedy is to **coarsen the grid to 128 samples** (still absolute, still block-size independent, at the
cost of doubling the per-chunk delivery fraction to 0.487 — which must then be re-checked against
§7.6's positive control) — never to revert to per-block delivery and never to loosen SC-005.

#### 3.5.5 `pushMacroSurfaces()` (FR-043)

```cpp
/// Krate::DSP::SeraphisMacroValues is a plain aggregate of five floats with no
/// operator== declared and none defaulted (seraphis_macro_matrix.h:122-128), and
/// C++20 does NOT synthesise == for such an aggregate. `m != lastPushedMacros_`
/// therefore does not compile, and defaulting == in the DSP struct would touch a
/// FIFTH dsp/ file, which FR-071 restricts to four. Compared field-by-field, in
/// the processor TU, instead.
[[nodiscard]] inline bool macrosEqual(const Krate::DSP::SeraphisMacroValues& a,
                                      const Krate::DSP::SeraphisMacroValues& b) noexcept {
    return a.dream == b.dream && a.bloom == b.bloom && a.dissolve == b.dissolve
        && a.gravity == b.gravity && a.entropy == b.entropy;
}

void Processor::pushMacroSurfaces() noexcept {
    // --- the five macro knobs -------------------------------------------------
    const SeraphisMacroValues m = readSmoothedMacros();
    if (!lastPushedMacrosValid_ || !macrosEqual(m, lastPushedMacros_)
        || anyMacroSmootherUnsettled()) {
        macros_.setMacros(m);                       // seraphis_macro_matrix.h:599
        lastPushedMacros_ = m;
        lastPushedMacrosValid_ = true;
    }
    // --- the 27 MB bases ------------------------------------------------------
    for (std::size_t t = 0; t < SeraphisMacroMatrix::kNumTargets; ++t) {
        const auto target = static_cast<SeraphisMacroTarget>(t);
        const float value = baseValueForTarget(target);
        // PER-TARGET settling, and the macro smoothers are NOT in this predicate.
        if (lastPushedBaseValid_ && value == lastPushedBase_[t]
            && !targetClassBUnsettled(target)) {     // §3.5.4
            continue;                                // ON CHANGE ONLY
        }
        macros_.setTargetBase(target, value);        // FR-003
        lastPushedBase_[t] = value;
        ++setTargetBasePushes_;                      // INVOCATIONS, for SC-007
    }
    lastPushedBaseValid_ = true;
}
```

**The settling predicate is per-target, and an earlier revision's single global flag was a defect that
falsified §7.7's own table.** That revision computed one `settling` from
`anyBaseClassBSmootherUnsettled()` — `bodyDamping || breathDepth || tideDepth ||
anyMacroSmootherUnsettled()` — and used it to skip the on-change `continue` for **all 27 targets**. So
one class-(b) `MB` change (ID 802), *or any macro knob move at all* (the macro smoothers were in the
same predicate), pushed 27 × `setTargetBase` per chunk for the whole settling window — up to 27 × 28 =
756 increments of `setTargetBasePushes_` — while §7.7's table asserts a class-(b) `MB` change yields
`Δ = +1 … +N` and the paragraph below asserts that a macro settling push does **not** move the counter
at all. A faithful implementation of that code failed SC-007 as written.

With the per-target predicate: exactly **one** target re-pushes per chunk per un-settled class-(b)
`MB` row, so Δ is `1 … 28`; the other 26 targets are untouched; and the macro smoothers are excluded
from the base loop entirely, because the macro push below owns them.

The macro push is its **own** owning push (the macros are route `processor`, not `MB`), so a macro
settling push deliberately does **not** increment `setTargetBasePushCountForTest()` — which is what
keeps SC-007's `setTargetBase` row exact at "27 at prepare, +1 for a class-(a) `MB` change".

`baseValueForTarget()` is a single switch from `SeraphisMacroTarget` to the owning atomic (or the
class-(b) smoother), and it is the **only** place C-6's 27 `MB` rows are mapped. FR-055's
"never also through `SeraphisVoiceParams`" is checkable by construction because `SeraphisVoiceParams`
has no field for any of the 27 (§1.1).

### 3.6 Host-synced morph travel (FR-056, C-7, Q3)

**Tempo sample point: once per `process()` call**, from `data.processContext`, before the pre-slice
push block — never per slice. `ProcessContext` carries one tempo per block by construction, so a
per-slice read would re-derive the identical value at up to 2048-sample granularity for nothing. This
matches the repo's existing shape (`plugins/gradus/src/processor/processor.cpp:264-272` reads
`data.processContext->tempo` once per block).

**Epsilon:**

```cpp
/// FR-056 / C-3 amendment 2. The change threshold on the DERIVED travel rate.
/// 0.1 % of kMinTravelRate (spectral_morph_engine.h:101, = 1.6667e-3 journeys/s),
/// i.e. 1.6667e-6. Far below any audible change in a 600-second journey, and ~10x
/// above the float noise of the division: tempo/(60*beats) produces values <= 1.0
/// whose relative float error is ~1e-7, i.e. ~1e-7 absolute.
inline constexpr float kSyncedRateEpsilon =
    Krate::DSP::SpectralMorphEngine::kMinTravelRate * 1.0e-3f;
```

```cpp
void Processor::updateSyncedTravelRate(const Vst::ProcessContext* ctx) noexcept {
    if (!morphParams_.sync.load(std::memory_order_relaxed)) {
        lastSyncedTravelRate_ = -1.0f;            // free-run: ID 404 is used unchanged
        return;
    }
    // FALLBACK, stated: no context or no valid tempo -> the free-running
    // kMorphTravelRateId value, unchanged. NEVER silence, never zero, and never a
    // retained stale synced rate.
    if (ctx == nullptr || (ctx->state & Vst::ProcessContext::kTempoValid) == 0
        || !(ctx->tempo > 0.0)) {
        lastSyncedTravelRate_ = -1.0f;
        return;
    }
    // C-7's bar rule, and the ONLY reading of "bar" permitted.
    double barBeats = 4.0;                        // common time
    if ((ctx->state & Vst::ProcessContext::kTimeSigValid) != 0
        && ctx->timeSigNumerator > 0 && ctx->timeSigDenominator > 0) {
        barBeats = static_cast<double>(ctx->timeSigNumerator)
                 * (4.0 / static_cast<double>(ctx->timeSigDenominator));
    }
    const int idx = clampSyncNoteIndex(morphParams_.syncNote.load(std::memory_order_relaxed));
    const double beats = kSyncNoteBeats[idx] * (kSyncNoteIsBarDenominated[idx] ? barBeats : 1.0);
    const auto rate = static_cast<float>(
        std::clamp(ctx->tempo / (60.0 * beats),
                   static_cast<double>(SpectralMorphEngine::kMinTravelRate),
                   static_cast<double>(SpectralMorphEngine::kMaxTravelRate)));
    if (lastSyncedTravelRate_ < 0.0f || std::fabs(rate - lastSyncedTravelRate_) > kSyncedRateEpsilon) {
        lastSyncedTravelRate_ = rate;
        ++voiceParamGeneration_;                  // FR-042 amendment 2
    }
}
```

`buildVoiceParams()` then takes `morphTravelRate = (lastSyncedTravelRate_ >= 0.0f)
? lastSyncedTravelRate_ : <ID 404's stored value>`.

`beatsPerJourney` is never zero by construction (the smallest entry is 0.25, bar-independent), so the
division cannot produce a non-finite rate before the clamp.

Because a moving tempo increments `applyVoiceParams` every block, SC-007's quiescent arm holds
`processContext` at a **fixed** tempo (or supplies none), and SC-007's separate moving-tempo clause
asserts the increment behaviour rather than treating it as a violation.

### 3.7 The spectral-slot concurrency contract (FR-041b, Q8)

**Ownership, stated as a rule before the steps, because two earlier revisions broke it in opposite
directions:**

| Array | Written by | Read by |
|---|---|---|
| `factoryStates_[5]` | **written once at construction** (`makeFactoryStateTable()`, §3.1); immutable thereafter | both threads (immutable ⇒ no synchronisation needed) |
| `spectralSlots_[4]` | **audio** thread only — §3.2's `CFG` refresh and step 4 below | **audio** thread only |
| `spectralSlotsStaging_[3][4]` | **message** thread only (`setState`) | message thread (`getState`) and, for one published index at a time, the audio thread's step-4 copy |

**`getState()` never reads `spectralSlots_`.** An earlier revision had it *"serialize from
`spectralSlotsStaging_` when a handoff is outstanding, from **`spectralSlots_`** otherwise"* — an
unsynchronised message-thread read of a 2160-byte non-atomic array that the audio thread writes in
§3.2's `CFG` refresh and in step 4. A host that automates a `CFG` dropdown (IDs 409–412) while saving
a preset — pluginval stress-tests exactly this, and §3.4 states that host calls can legally run
concurrently with `process()` — would get a data race whose visible symptom is a **torn
`SpectralState` written into the saved preset**. R5's "the staging copy removes tearing" covered only
the `setState` direction and did not reach this path at all.

The fix needs no second publication, because in Phase 9's factory-selection-only design the slot
content is a **pure function of the four `morphParams_.slot[i]` atomics**: both threads would compute
`makeFactoryState(toSpectralStateId(...))`, and `factoryStates_` now *is* that table. So `getState()`
reads the message-thread-safe sources only:

```cpp
// getState(), message thread. Reads: one atomic int per slot, plus an immutable
// table, plus - only while a handoff is outstanding - the staging buffer this
// thread itself wrote. It touches spectralSlots_ NOWHERE.
const int published = spectralSlotsHandoff_.load(std::memory_order_acquire);
for (int slot = 0; slot < 4; ++slot) {
    const Krate::DSP::SpectralState& s =
        (published >= 0)
            ? spectralSlotsStaging_[static_cast<std::size_t>(published)][slot]
            : factoryStates_[clampFactoryIndex(
                  morphParams_.slot[slot].load(std::memory_order_relaxed))];
    // ... serializeSpectralState(s, ...) per §5.4
}
```

FR-094's byte-identity holds: `makeFactoryState` is documented *"Deterministic and stateless"*
(`spectral_state.h:349-351`), so the table entry is bitwise what the audio thread holds in
`spectralSlots_[slot]` in every reachable Phase 9 state, and a save issued between a `setState()` and
the next `process()` still writes back what was loaded.

**The publication protocol, and why there are THREE staging buffers.** One staging buffer with one
flag has no writer-side interlock: the message thread writes staging and publishes; the audio thread
observes and begins the 2.1 KiB copy; a **second** `setState()` then writes staging *while that copy
is in flight* — an unsynchronised concurrent write/read of a non-atomic array, which pluginval's
state-stress paths and any host doing rapid preset changes hit. Spinning the message thread on the
flag is not available either: `setState()` may legally arrive with the audio thread stopped
(FR-047), and the flag would then never clear.

Two buffers do not suffice — one may be published-and-unconsumed while the other is being copied, and
both are then excluded. **Three do**, and the write-index search is a bounded loop over three fixed
indices with no wait on another thread's progress:

| Step | Thread | What |
|---|---|---|
| 1 | message | `setState()` picks `w` = the first of `{cursor, cursor+1, cursor+2} mod 3` that equals **neither** `spectralSlotsHandoff_.load(acquire)` **nor** `spectralSlotsConsuming_.load(acquire)` — at most two are excluded, so this always terminates |
| 2 | message | deserializes the four 541-byte payloads into `spectralSlotsStaging_[w]`, never `spectralSlots_` |
| 3 | message | `spectralSlotsHandoff_.store(w, release)`; `stagingWriteCursor_ = (w + 1) % 3` |
| 4 | audio | top of `process()`: load `handoff` with **acquire**; if `>= 0`, `spectralSlotsConsuming_.store(idx, release)` **first**, then `spectralSlotsHandoff_.store(-1, release)`, then copy `spectralSlotsStaging_[idx]` → `spectralSlots_`, then `spectralSlotsConsuming_.store(-1, release)`; set `spectralStatesPending_`, reset `spectralRetryMask_`, `++spectralHandoffConsumes_` |
| 5 | audio | `spectralSlots_` is audio-thread-**owned** thereafter: written only by §3.2's `CFG` refresh and by step 4; read only by FR-046's push |

**Why step 4's store order is the whole interlock.** `spectralSlotsConsuming_` is stored *before*
`spectralSlotsHandoff_` is cleared, and the message thread loads `handoff` before `consuming`. The
dangerous state — message thread sees `handoff == -1` **and** `consuming == -1` while buffer `idx` is
mid-copy — is therefore unreachable: if the acquire-load of `handoff` sees `-1`, it happens-after the
release-store of `-1`, which happens-after the release-store of `consuming = idx`, so the subsequent
acquire-load of `consuming` sees either `idx` (copy in flight, buffer excluded) or `-1` (copy already
finished, buffer safe). Both atomics are `int` and lock-free on every target;
`spectralSlotsConsuming_` is also what makes step 4's "on the audio thread and not on the message
thread" observable, alongside `spectralHandoffConsumeCountForTest()` (§3.1) which SC-023 clause 5
asserts directly.

**`factoryStates_` is filled in the CONSTRUCTOR, and that placement closes a window
`setupProcessing()`-time construction leaves open.** `makeFactoryState` is documented *"Deterministic
and stateless"* (`spectral_state.h:349-351`) and takes **no sample rate**, so there is nothing to
defer. Deferring it makes `factoryStates_` an all-zero `std::array<SpectralState,5>{}` for the whole
window between `initialize()` and the first `setupProcessing()` — a window a host may legally call
`getState()` in (pluginval does; and §3.7 and §7.13 clause 6a rely on the same legality argument for
`setState()`). In that window `getState()`'s no-handoff branch below would serialize
`factoryStates_[…]` as four **zeroed** states, and a zeroed `SpectralState` is **not** rejected: with
`numPartials == 0` the ratio/amplitude loop of `isValidSpectralState` does not execute, the all-zero
`name` terminates at index 0, and `tiltDbPerOct = 0` / `inharmonicity = 0` are inside their bounds
(`spectral_state.h:82-145`), so it returns `true`. `serializeSpectralState` therefore does **not**
return 0 and §5.4's zero-fallback never fires — the preset is saved with four *valid, empty*
payloads, which reload cleanly and install four silent slots. That is strictly worse than the
rejected-and-defaulted failure it would be mistaken for, and no criterion looks for it. Filling the
table at construction makes the window unreachable in every order.

**`setupProcessing()` seeds `spectralSlots_` from the CURRENT ATOMICS, not from the registered
defaults**: for each slot, `spectralSlots_[slot] =
factoryStates_[clampFactoryIndex(morphParams_.slot[slot].load())]` and `lastPushedSlotStateId_[slot] =`
that id. It does **not** rebuild `factoryStates_`. This matters because FR-047 and the *Edge cases* →
*State* bullet both make **`setState()` before `setupProcessing()` legal** (spec `:1228`,
`:2361-2362`): if `setupProcessing()` seeded from the registered defaults it would silently overwrite
a preset that had already been loaded, and nothing would ever re-derive it —
`refreshSpectralSlotFromFactory()` runs only on a `CFG` parameter change (§3.2), so the DSP and every
later `getState()` would carry the wrong states for the life of the instance. Reading the atomics
makes the two orders converge on the same result. §7.13 carries the criterion (a section of
`Seraphis_PresetLoadAfterPrepare_ReachesDsp` that calls `setState()` **before**
`setupProcessing()`); no existing criterion covered it, because SC-023 is explicitly the
after-prepare case. The staging buffers stay zero-initialised at prepare — they are written only by
`setState()`, and `getState()`'s `published >= 0` guard is what keeps an unwritten buffer unreadable.

The two `std::atomic<int>`s are not a new synchronisation primitive in the sense FR-048 forbids — they
are neither a lock nor an allocation, and the design already carried a `bool` flag of the same role.
With them, FR-041b's "no new synchronisation primitive is introduced" is **unconditionally** true
rather than conditional on host behaviour.

### 3.8 The FR-059a probe seam

```cpp
// processor.h, above the class:
namespace detail {
/// SC-005 positive control (b). DECLARED HERE, DEFINED ONLY BY THE SC-005 TEST TU.
/// Shape: detail::SeraphisVoiceSilenceRampProbe (seraphis_voice.h:97, :775) - but
/// declared PLUGIN-SIDE on purpose, because adding a bypass seam to dsp/ is what
/// FR-071 forbids. Its SOLE capability is to set paramSmootherBypass_, which makes
/// advanceParamSmoothers() snap instead of ramp - a deliberate un-smoothed write.
/// The library never defines it, so a shipping build has no way to call it.
/// ODR swept this session: grep -rn "SeraphisParamSmootherBypassProbe" dsp/ plugins/ -> 0 hits.
struct SeraphisParamSmootherBypassProbe;
}  // namespace detail

// inside class Processor, private:
    friend struct detail::SeraphisParamSmootherBypassProbe;
```

Nothing in `plugins/seraphis/src/` other than that declaration references the probe; no `process()`
path calls it; the one branch it drives (`paramSmootherBypass_`) is `false` on every shipping path.

---

## 4. Controller wiring (FR-060 – FR-064)

`plugins/seraphis/src/controller/controller.cpp` grows three parallel band-ordered lists and nothing
else. No new interface is implemented; `Controller`'s class declaration (`controller.h:24`) is
unchanged.

```cpp
tresult PLUGIN_API Controller::initialize(FUnknown* context) {
    …
    registerGlobalParams(parameters);       // 4 IDs now: 0, 1, 2, 3   (:35)
    registerMacroParams(parameters);        // 100-104                 (:36)
    registerCloudParams(parameters);        // 200-210
    registerMorphParams(parameters);        // 400-412
    registerLifeModParams(parameters);      // 600-604, 700-704
    registerBodyParams(parameters);         // 800-812
    registerAtmosphereParams(parameters);   // 1000-1016
    registerAetherParams(parameters);       // 1200-1217
    …                                       // => getParameterCount() == 91  (FR-060, SC-001)
}
```

`getParamStringByValue` (`:76-87`) delegates to each `format<Section>Param` in band order and falls
through to `EditControllerEx1::getParamStringByValue` so every `StringListParameter` formats itself
(FR-061). No formatter claims a dropdown ID.

`setComponentState` (`:50-74`) keeps the version gate, then calls every
`load<Section>ParamsToController` **in exactly `getState`'s order** (§5.1), each using
`setParamNormalized`. Every denormalization's inverse is exercised, not approximated — the four forms
of §2.3.1 invert exactly, and SC-010's controller-parity clause (within `1e-6` normalized) is the gate.

**FR-063 / FR-064.** No Phase 8 parameter changes type, ID, default or unit (verified by SC-014
against a checked-in table of the eight `getParameterInfo` records). `INoteExpressionController` is
**not** added — unconditionally, per RQ-2: per-note expression ships in a new named phase that owns
both halves of it, because the engine's note API is `noteOn(std::uint8_t, std::uint8_t)` /
`noteOff(std::uint8_t)` (`seraphis_engine.h:370`, `:415`) with no per-note expression input for an
interface shipped here to drive.

---

## 5. State format version 2 (C-8, FR-090 – FR-094)

### 5.1 The layout, and the exact write order

`getState` writes, and `Processor::setState` / `Controller::setComponentState` read, in this order.
Little-endian, `IBStreamer`, extending `processor.cpp:500-509`:

```
        int32   version = kCurrentStateVersion (2)                              4 B
[global]  saveGlobalParams()      float masterGain | int32 poly | int32 soft    12 B
[macro]   saveMacroParams()       5 x float                                     20 B
        ------ end of a version-1 stream: 36 B, a STRICT PREFIX of v2 ------
[seed]    saveGlobalSeed()        int32 seedIndex                                4 B
[cloud]   saveCloudParams()       11 x float   (200-210)                        44 B
[morph]   saveMorphParams()        5 x float   (400,401,402,404,407)            20 B
                                   4 x int32   (403,405,406,408)                16 B
                                   4 x int32   factory state id (409-412)       16 B
                                   4 x 541 B   serializeSpectralState        2164 B
[life]    saveLifeModParams()      9 x float   (600-604, 701-704)               36 B
                                   1 x int32   (700 envMode)                     4 B
[body]    saveBodyParams()        10 x float   (801-810)                        40 B
                                   3 x int32   (800 material, 811 agc, 812 byp) 12 B
[atmos]   saveAtmosphereParams()  15 x float   (1000-1007, 1009-1015)           60 B
                                   2 x int32   (1008 freeze, 1016 grainEnv)      8 B
[aether]  saveAetherParams()      17 x float   (1200-1203, 1205-1217)           68 B
                                   1 x int32   (1204 freeze)                     4 B
                                                                   total =    2532 B
```

Arithmetic, which SC-010 asserts exactly so a field added without a spec change fails the test rather
than silently widening the format: **73 floats + 18 int32** plus the version int32 and the four
payloads → `73·4 + 18·4 + 4 + 4·541 = 292 + 72 + 4 + 2164 = 2532`.

### 5.2 `kSeedId` is written AFTER `[macro]`, and that placement is normative (FR-091a)

Phase 8's `saveGlobalParams` writes exactly `float | int32 | int32` and `loadGlobalParams` is a
**fixed three-field sequential reader with no version parameter** (`global_params.h:160-183`). A v2
reader that consumed a fourth field there would eat `dream`'s four bytes as `seed` on every v1 stream,
shift the whole macro block by one field, and hit EOF before `entropy` — a **shape divergence
mid-stream**, which FR-093's EOF-safety mechanism addresses only for *truncation* and cannot detect at
all.

Therefore `saveGlobalParams` / `loadGlobalParams` / `loadGlobalParamsToController` keep their Phase 8
three-field shape **unchanged**, and the seed is carried by a separate, explicitly-positioned trio in
the same header:

```cpp
inline void saveGlobalSeed(const GlobalParams&, Steinberg::IBStreamer&);
inline bool loadGlobalSeed(GlobalParams&, Steinberg::IBStreamer&);            // EOF-safe
template <typename SetParamFunc>
inline void loadGlobalSeedToController(Steinberg::IBStreamer&, SetParamFunc);
```

Neither `loadGlobalParams` nor its controller twin gains a version parameter, because with this
placement neither needs one — and a version-aware fixed-sequence reader is exactly the failure mode
C-8 records.

### 5.3 The three binding rules

- **Every loader stays EOF-safe** in the Phase 8 sense (`global_params.h:166-183`): a short stream
  leaves unread fields at their registered defaults and returns `false`, which is not an error. The
  four `SpectralState` payloads use the same discipline: `IBStreamer::readRaw` into a 541-byte scratch,
  and a short read leaves that slot at its factory default.
- **A version-1 stream (36 bytes) loads correctly** — global + macro are read, everything else stays
  at its registered default, and the result is exactly the Phase 8 sound (FR-093 / SC-011).
- **The four payloads are serialized from the message-thread-safe sources of §3.7** — the published
  staging buffer, else `factoryStates_[morphParams_.slot[i]]` — never from the audio-thread-owned
  `spectralSlots_` (§3.7) and never read back out of the DSP: `SpectralMorphEngine` exposes `setState` (`:292`), `setStateCount`
  (`:318`) and `getStateCount` (`:443`) and **no per-slot getter**, and stores each slot *sanitized* —
  log2-transformed ratios with `tiltDbPerOct`, `inharmonicity` and `name` discarded (`:285-313`).
  Without the Processor-side copy, `getState` has no source for 2164 of the 2532 bytes.

### 5.4 Spectral payload encode / decode (FR-092)

```cpp
// write
std::array<std::byte, Krate::DSP::kSpectralStateBytes> buf{};      // 541, spectral_state.h:185
for (int slot = 0; slot < 4; ++slot) {
    const std::size_t n = Krate::DSP::serializeSpectralState(source[slot], buf.data(), buf.size());
    if (n == 0) { buf.fill(std::byte{0}); }        // invalid state -> 541 ZERO bytes
    streamer.writeRaw(buf.data(), static_cast<Steinberg::int32>(buf.size()));
}
// read
for (int slot = 0; slot < 4; ++slot) {
    if (streamer.readRaw(buf.data(), static_cast<Steinberg::int32>(buf.size())) != buf.size()) {
        break;                                     // EOF-safe: this and later slots stay factory
    }
    // deserializeSpectralState leaves `out` BITWISE UNTOUCHED on rejection (:263-268),
    // so a bad slot does not make a bad preset.
    (void) Krate::DSP::deserializeSpectralState(buf.data(), buf.size(), destination[slot]);
}
```

**In Phase 9's factory-selection-only design the zero path is unreachable, and that is what makes
FR-094 hold — but only because §3.7 fills `factoryStates_` in the CONSTRUCTOR.** Every slot
`getState()` can reach is a `makeFactoryState(...)` result (FR-041b) or a payload the message thread
itself deserialized, so it always satisfies `isValidSpectralState` (`spectral_state.h:82-145`,
enforced at the serializer's guard `:240`) and `serializeSpectralState` never returns 0. *With
`factoryStates_` built at prepare instead, the claim would be false in the before-prepare window —
and it would fail in the worse direction: a zeroed `SpectralState` is **valid** (numPartials 0 skips
the ratio loop; the all-zero `name` terminates at index 0; tilt and inharmonicity are in range), so
the zero path would not even fire and four valid-but-empty payloads would be written instead. §3.7
records the fix.* The clause below therefore exists for robustness against a corrupt in-memory slot
only. It matters because the zero path would otherwise **break FR-094**: a rejected 541-zero
payload leaves the runtime slot at its previous valid contents, so the *second* `getState` would write
541 non-zero bytes for that slot and the two streams would not be byte-identical. When Phase 11 makes
states user-editable (RQ-1) it must either preserve the validity invariant — the criterion RQ-1 hands
it — or add an explicit FR-094 carve-out.

A stream written by a build with a different `kSpectralStateFormatVersion` is refused **per slot** by
the version-byte check (`:284-286`), leaving that slot at its factory default while the rest of the
preset loads. One bad slot is not a bad preset.

### 5.5 `setState` / `getState` control flow

```cpp
tresult PLUGIN_API Processor::setState(IBStream* state) {
    …                                              // null check, version read, version gate (:479-485)
    if (version > kCurrentStateVersion) { return kResultFalse; }   // v3 refused, nothing mutated
    loadGlobalParams(globalParams_, streamer);     // unchanged Phase 8 shape
    loadMacroParams(macroParams_, streamer);
    loadGlobalSeed(globalParams_, streamer);       // FR-091a
    loadCloudParams(cloudParams_, streamer);
    const std::size_t w = pickStagingBuffer();     // §3.7 step 1; excludes handoff & consuming
    loadMorphParams(morphParams_, streamer, spectralSlotsStaging_[w]);  // §2.3.0's 3-arg loader
    loadLifeModParams(lifeParams_, streamer);
    loadBodyParams(bodyParams_, streamer);
    loadAtmosphereParams(atmosParams_, streamer);
    loadAetherParams(aetherParams_, streamer);
    spectralSlotsHandoff_.store(static_cast<int>(w), std::memory_order_release);  // publish
    stagingWriteCursor_ = static_cast<int>((w + 1) % 3);
    requestPushAllSurfaces();                                        // FR-047 / SC-023
    return kResultOk;
}
```

`getState` mirrors the scalar order exactly and takes its four spectral payloads from the two
message-thread-safe sources of §3.7 — the published staging buffer while a handoff is outstanding,
`factoryStates_[morphParams_.slot[i]]` otherwise. **It does not read `spectralSlots_`** (FR-041b
clause 5 is **amended** to say so — §12.1's A5, applied in §11 step 0; §3.7's ownership table carries
the reason).

**Amendment (2026-08-01): SC-012's "bitwise unchanged" claim and the staging ring do not conflict.**
T001 flagged that SC-012's clause *"a garbage spectral payload leaves `spectralSlots_[slot]` bitwise
unchanged"* reads as if `deserializeSpectralState` wrote into `spectralSlots_` directly, which the
three-buffer ring of §3.7 forbids. **Ruling: the clause stands as written, and no spec edit is
required.** `loadMorphParams` deserializes into `spectralSlotsStaging_[w]` — the staging buffer — and
validation happens *there*, before anything is published: `deserializeSpectralState` leaves its `out`
argument bitwise untouched on rejection (`spectral_state.h:274-286`), so a garbage payload's staging
write is discarded in place. The audio thread only ever copies a staging buffer into `spectralSlots_`
via the §3.3 handoff consume, and that consume runs only for a buffer `setState()` published with the
release store. A rejected payload therefore produces **no handoff and no publish**, `spectralSlots_`
is never written at all, and "bitwise unchanged" is satisfied *a fortiori* — the ring makes the
guarantee stronger, not weaker. `spectralSlotForTest(slot)` (§3.1) reads `spectralSlots_`, which is
the surface SC-012 asserts against, so the criterion is testable exactly as spec'd. `spec.md` is not
changed.

---

## 6. `editor.uidesc` control-tags (FR-100)

`plugins/seraphis/resources/editor.uidesc` gains a `<control-tag>` entry for **every** one of the 91
IDs, named after the enum without the `k` / `Id` affixes (`MasterGain`, `CloudRichness`,
`AetherTideDepth`, …). The existing eight tags (`:15-24`) keep their names verbatim.

**No new `<view>` is added.** The 420 × 300 placeholder template (`:25-69`) with its six `CSlider`,
one `COptionMenu` and one `CCheckBox` stays exactly as it is; layout is Phase 11's (roadmap lines
471–479), and the banner at `:3-5` already says Phase 11 replaces the file wholesale.

**SC-015's helper choice matters.** `Krate::Test::unreachableParams`
(`tests/test_helpers/uidesc_reachability.h`) checks reachability *from views*, and Phase 9 deliberately
adds 83 tags with no view — it would report all 83 as unreachable. SC-015 therefore uses
`Krate::Test::extractControlTagMap(xml)` (`:43`) directly and compares the **tag-value set** with the
registered ID set for exact equality (no missing tag, no orphan tag), then separately asserts the
eight existing `<view>` bindings still resolve to the C-6 type for their ID.

---

## 7. Test plan

### 7.0 Test-file map

| File | Target | Criteria |
|---|---|---|
| `dsp/tests/unit/systems/seraphis_param_broadcast_test.cpp` **(new)** | `dsp_systems_tests` | FR-001–FR-005, FR-070, FR-072; SC-002 cl. 4 |
| `plugins/seraphis/tests/unit/parameter_surface_test.cpp` **(new)** | `seraphis_tests` | SC-001, SC-014, SC-015, SC-022 |
| `plugins/seraphis/tests/unit/state_v2_test.cpp` **(new)** | `seraphis_tests` | SC-010, SC-011, SC-012, SC-023 |
| `plugins/seraphis/tests/unit/morph_sync_test.cpp` **(new)** | `seraphis_tests` | SC-018 |
| `plugins/seraphis/tests/integration/param_reach_test.cpp` **(new)** | `seraphis_tests` | SC-003 |
| `plugins/seraphis/tests/integration/param_cadence_test.cpp` **(new)** | `seraphis_tests` | SC-006, SC-007, SC-013 |
| `plugins/seraphis/tests/integration/param_continuity_test.cpp` **(new)** | `seraphis_tests` | SC-005 (+ `kContinuityMechanism[]`, + the FR-059a probe definition) |
| `plugins/seraphis/tests/integration/macro_wiring_test.cpp` **(new)** | `seraphis_tests` | SC-002, SC-004 |
| `plugins/seraphis/tests/integration/param_character_test.cpp` **(new)** | `seraphis_tests` | SC-019, SC-020 |
| `plugins/seraphis/tests/integration/param_perf_test.cpp` **(new)** | `seraphis_tests`, `[.perf]` | SC-008, SC-009 |
| `plugins/seraphis/tests/unit/controller/editor_lifecycle_test.cpp` *(existing, unchanged)* | `seraphis_tests` | SC-016 |
| — | pluginval / lints | SC-017, SC-021 |

**Four EXISTING test files are edited by this phase, and one of them fails outright without the
edit.** A phase that changes the state layout and the state version cannot leave the Phase 8 state
tests untouched; the plan states each edit so none is discovered as a red build.

| Existing file | Why Phase 9 touches it | Edit |
|---|---|---|
| `plugins/seraphis/tests/unit/state_roundtrip_test.cpp` | **BREAKS.** It hard-codes the v1 layout and version: `constexpr int32 kStateBytes = 36;` (`:54`), `REQUIRE(s.getSize() == kStateBytes)` / `REQUIRE(s->getSize() == kStateBytes)` (`:146`, `:215`, `:237`), `CHECK(int32AtOffset(*s, 0) == 1)` (`:219`), the truncation ladder's `if (n >= kStateBytes)` (`:316`), and `future.version = Seraphis::kCurrentStateVersion + 1; REQUIRE(future.version == 2);` (`:330-331`). FR-012 sets `kCurrentStateVersion = 2` and C-8 makes `getState` write 2532 bytes, so **six** assertions fail. | §7.10, step 8a |
| `plugins/seraphis/tests/integration/param_flow_test.cpp` | Its hand-authored stream (`:205-222`) writes `kCurrentStateVersion` followed by only the 36-byte v1 body. Under v2 that is a **v2-labelled stream carrying v1 content**, which survives only by FR-093's EOF-safety and no longer tests what its comment (`:186-192`) claims. | §7.10, step 8a |
| `plugins/seraphis/tests/unit/lifecycle_test.cpp` | Same shape: `writeNonDefaultState` (`:85-99`) writes `kCurrentStateVersion` + the 36-byte v1 body. | §7.10, step 8a |
| `plugins/seraphis/tests/integration/processor_audio_test.cpp` | Carries FR-051's deletion target: `SECTION("Seraphis_MacroParametersAreInert")` at **`:856`**, whose Phase 8 banner (`:843-855`) says in so many words *"PHASE 9 MUST INVERT THIS TEST"*. | §11 step 12 |

`plugins/seraphis/tests/unit/param_denorm_test.cpp` needs **no** edit: its `readParams` (`:94-109`)
reads the first nine fields off the front of the stream and asserts nothing about total length, so a
2532-byte v2 stream satisfies it unchanged. Its layout comment (`:72-76`) is corrected to say it reads
the v2 stream's **prefix**, and that is the whole diff.

**Shared fixture change.** `plugins/seraphis/tests/seraphis_test_fixture.h` sets
`data_.processContext = nullptr` (`:285`). Phase 9 adds an owned
`Steinberg::Vst::ProcessContext context_{}` plus `bool useContext_ = false` and a
`setTempo(double bpm, int sigNum, int sigDen, bool tempoValid, bool sigValid)` helper, so
`withOutputChannels()` can attach it. Everything else in the fixture is reused unchanged — including
`ParameterChanges` / `MultiPointParamValueQueue` (`:48-152`), the guard-word canaries (`:160-161`,
`:319-321`) and `renderBlocks`' per-block script hook (`:336-358`).

### 7.1 SC-002 — negative control: defaults are unchanged

*File:* `integration/macro_wiring_test.cpp` and `dsp/tests/unit/systems/seraphis_param_broadcast_test.cpp`.
*Cases:* `Seraphis_Phase9Defaults_MatchPhase8Render`, `SeraphisMacroMatrix_DefaultBases_Unchanged`.

**Arm A** — 4 s render of note 60 through `Processor` at **registered defaults**, 48 kHz, block 512,
`kSeedId` at index 0.
**Arm B** — the same 4 s render produced **in the same translation unit and the same binary** by
configuring a `SeraphisEngine` + `AetherReverb` pair directly with the Phase 8 shipped defaults
(`makeSeraphisEngineConfig` / `makeSeraphisReverbConfig`, `seraphis_engine_config.h:43`, `:64`) and
driving the Phase 8 chain through `Krate::DSP::TestUtils::renderSeraphisChain`
(`tests/test_helpers/seraphis_chain.h:147`) — **no Phase 9 push path engaged at all**: no
`applyVoiceParams`, no `setTargetBase`, no `applyAetherParams`, no `applySpectralStates`.

*Gate:* per-sample `maxAbsDiff` over **all** samples of **both** channels **≤ 1.0e-5**.
`compareFingerprints` (`render_fingerprint.h:101`) runs as a **secondary, warn-only** aggregate and
**must not gate**; **a checked-in fingerprint reference is forbidden** (it would be the cross-toolchain
float golden this repo has already broken three times, and `render_fingerprint.h:20-30` records that
its tolerances were measured on phaser/flanger cases, not on a 4 s stochastic granular + reverb chain).

*Clause 4* (`dsp_systems_tests`): a default-constructed `SeraphisMacroMatrix` evaluates all 27 targets
to the `kRows` literals — asserted through `getTargetBase(t)` for every `SeraphisMacroTarget`, and
through `apply()` on a prepared engine at the FR-060 neutral, before and after `resetTargetBases()`.

### 7.2 The `dsp_systems_tests` additions (FR-001 – FR-005, FR-070, FR-072)

*File:* `dsp/tests/unit/systems/seraphis_param_broadcast_test.cpp`.

| Case | Asserts |
|---|---|
| `SeraphisVoiceParams_DefaultsMatchPreparedVoice` | a default-constructed POD's 37 fields equal what `SeraphisVoice::prepare()` step 6/7 installs (`seraphis_voice.h:284-364`) / the component member defaults for the eight 2026-08-01 additions — read back through the FR-072 accessors and `AtmosphereEngine`'s six existing getters |
| `SeraphisVoiceParams_MapsEveryFieldToItsOwnSetter` | 37 sub-sections; each pushes ONE field off its default, calls `applyVoiceParams`, and asserts **only that field's** read-back moved. Pins the `setDecayTimeSec` (209) vs `setCloudDecaySec` (807) and `setWidth` (810) vs `setVoiceWidthBasePercent` (604) pairs |
| `SeraphisVoiceParams_IsDisjointFromMacroTargets` | after pushing all 37 fields to non-defaults, `getTargetBase(t)` is unchanged for all 27 targets (FR-055 by construction) |
| `SeraphisEngine_ApplyVoiceParams_ReachesAllSixteenSlots` | polyphony **8**, push, then assert the read-back on every `i < kMaxVoices` — the orphan-tail/new-slot hazard FR-002's bound exists for. Second section: shrink polyphony to 4 with a note still ringing, push, assert slots 4–15 also took it |
| `SeraphisEngine_ApplySpectralStates_WritesAllFourSlotsToAllSixteenVoices` | quiescent engine, count 4, four distinct factory states; `getVoice(i).morph().getStateCount() == 4` for all 16. Second section: with a voice sounding, `getRejectedConfigureTimeCallCount()` rises by exactly **5** on that voice and the others still accept. **Third section (the `voiceMask`, §1.3):** the default argument writes all sixteen (so no existing behaviour moved); a mask of `0x0001` moves voice 0's state count and leaves voices 1–15 at their previous count **and** leaves their rejection counters unmoved — which is what makes §3.3's per-voice retry the bound on repeated `buildSanitized` work |
| `SeraphisMacroMatrix_TargetBaseOverride_Composes` | `setTargetBase(t, v)` → `getTargetBase(t) == v`; at the FR-060 neutral `apply()` writes exactly `v` into every voice; `resetTargetBases()` restores the `kRows` literal; a non-finite argument (built from a **bit pattern**, never `std::numeric_limits`) leaves the stored base unchanged |
| `SeraphisVoice_Phase9Forwarders_AreOneToOne` | each of the 13 forwarders moves its component getter and nothing else; `setBodyResonatorBypass` read back through `isResonatorBypass()` returns the **requested** state on the first control step, not the ramp position |
| `ContinuousBody_Phase9Accessors_ReturnClampedStoredValues` | each of the 12 accessors returns the setter's clamped store, including out-of-range and non-finite inputs; `getDrive()` and `getDriveGain()` are shown to differ under an un-settled drive smoother |

The TU carries **no** `-fno-fast-math` requirement of its own beyond the non-finite section; that
section uses the repo's bit-pattern construction, so the whole file can stay off the
`set_source_files_properties` list.

### 7.3 SC-001 / SC-014 / SC-015 / SC-022 — the surface tests

*File:* `unit/parameter_surface_test.cpp`. All four are pure table tests: no render, no engine.

- **`Seraphis_ParameterSurface_IsComplete`** (SC-001). `Controller::initialize(nullptr)` then
  `getParameterCount() == 91`; iterate `getParameterInfo(i)` and assert the ID set equals C-6's exactly
  (no duplicate, none outside the reserved bands), and each `stepCount` matches the *Type* column
  (`0` for `R`, `1` for `T`, `n−1` for an `n`-entry `L`).
  **Plus a `getParamStringByValue` section (FR-061), because no other planned case exercises display
  formatting.** FR-061 requires `getParamStringByValue` to delegate to every pack's
  `format<Section>Param` in band order and forbids any formatter from claiming a dropdown ID
  (`spec.md:1422-1425`); §2.3.3 states the rule; and FR-061 has **no Traceability row**. Without this
  section, a formatter that claimed a dropdown ID — the exact failure the FR names — would render
  `"0.400"` instead of `"Metal Plate"` in every host and pass every other planned assertion, since
  §7.3's other three cases check `getParameterCount`, `getParameterInfo`, control-tags and defaults
  only. Three assertions, all on an initialized `Controller`:
  1. for each of the **nine `L` IDs** (3, 403, 406, 408–412, 800, 1016), at **every** index `i`,
     `getParamStringByValue(id, i / double(n-1), buf)` returns `kResultOk` and `buf` equals the
     `dropdown_mappings.h` label at `i` — the single table both registration and formatting read
     (§2.2), so a label list that existed in two places would fail here;
  2. for a sample of `R` IDs spanning all six new packs (one per pack: 200, 400, 601, 804, 1003,
     1207), at normalized 0.0, 0.5 and 1.0, the returned string is **non-empty**;
  3. for **every** dropdown ID, each of the six `format<Section>Param` functions returns something
     other than `kResultOk` — asserted by calling all six directly, which is the only way to see that
     none of them claimed the ID before `EditControllerEx1::getParamStringByValue` could format it.
- **`Seraphis_Phase8Parameters_AreFrozen`** (SC-014). For each of the eight Phase 8 IDs, compare
  `getParameterInfo` field-by-field (`id`, `stepCount`, `defaultNormalizedValue`, `units`, `flags`)
  against a **checked-in table of the eight infos**.
- **`Seraphis_UidescControlTags_MatchRegisteredIds`** (SC-015). Parse
  `SERAPHIS_RESOURCES_DIR "/editor.uidesc"` (`tests/CMakeLists.txt:57-60`) with
  `extractControlTagMap`; assert set equality with the 91 registered IDs in **both** directions; assert
  the eight existing `<view>` elements still bind and that each bound view's class still matches C-6's
  type (`CSlider` / `COptionMenu` / `CCheckBox`).
- **`Seraphis_RegisteredDefaults_AreExact`** (SC-022). For **each** of the 91 IDs, feed
  `getParameterInfo(i).defaultNormalizedValue` through that ID's own
  `handle<Section>ParamChange(params, id, value)` and compare the stored plain value with **exact float
  `==`, no tolerance**, against a checked-in table of C-6's *Default* column. The rows for 811, 812 and
  1011–1016 carry the **component member initializer** values (`continuous_body.h:163-164`;
  `atmosphere_engine.h:798`, `:805-806`, `:813-814`, `:820`, `:828-829`, `:952`), **not**
  `SeraphisVoice::prepare()`'s — `prepare()` touches none of the eight (`seraphis_voice.h:319-327` sets
  eight atmosphere values, none of them these).

### 7.4 SC-003 — every parameter reaches the DSP

*File:* `integration/param_reach_test.cpp`. *Case:* `Seraphis_EveryParameter_ReachesDsp`,
data-driven over an **83-row table** whose columns are *precondition*, *render length*, *observable*,
*threshold*. The route rows sum to the 83 new IDs: 37 `VP` + 19 `MB-voice` + 8 `MB-aether` + 5 `CFG`
+ 10 `AE` + 2 new `ENG` (3, 1008) + 2 new processor-local (405, 406) = **83**. IDs 1 and 2 are
additionally exercised (they are named by the criterion) but are not among the 83.

**Blanket rules.**

- `VP` (37): no precondition, **1 block**, read back the matching getter through
  `engineForTest()->getVoice(i)` for **every** `i < kMaxVoices`, exact equality with the pushed plain
  value after the component's own clamp.
- `MB-voice` (19): **polyphony pinned to 16 before these rows** (Q6), so `getPolyphony() ==
  kMaxVoices` and `apply()`'s own `getPolyphony()` bound (`seraphis_macro_matrix.h:625-626`) coincides
  with the shared `for every i < kMaxVoices` wording. Primary = the voice-side read-back after one
  `renderSlice`; secondary = `macroMatrixForTest().getTargetBase(...)`. **The `MB` rows may never be
  gated on `getTargetBase` alone** — it is FR-003's own storage and would pass even if
  `macros_.apply()` were never invoked.
- `ENG` (3, 1008; plus 1): `getSeed()` (`seraphis_engine.h:719`), `getAtmosphereFreeze()` (`:562`),
  `getPolyphony()` (`:665`), exact, 1 block.
- `AE` (10) and `MB-aether` (8): the per-row tables of the spec, **driven through the fixture's
  `IParameterChanges` on the `Processor`** like every other row, on the stated render windows and with
  the spec's per-row observables unchanged. Where a rendered observable is too weak to localize the
  failure, the row adds a **secondary**: `applyAetherParamsCallCountForTest()` incrementing plus a
  re-push comparison — the same shape §7.13 clause 4 uses.

  **These eighteen rows may NOT be driven by calling `applyAetherParams` / `setTargetBase` directly**,
  which an earlier revision specified. SC-003's opening obligation is that each ID is driven *"through
  `IParameterChanges`"* (spec `:1547-1548`), and a direct DSP-side call exercises **none** of the
  plugin path the criterion exists to prove: `handleAetherParamChange`'s denormalization
  (FR-017/FR-018), `processParameterChanges`' range dispatch (FR-040), the `aetherParamGeneration_`
  pair (FR-042) and the on-change `AE` push (FR-044). All eighteen rows would have passed with the
  entire 1200-band branch of the dispatch ladder deleted. Every observable in those rows — echo
  density, T60, onset shift, morph position, L/R correlation — is measurable from a `Processor`
  render, so nothing forced the direct-drive form.

  Direct `applyAetherParams` calls are reserved for the **SC-008 / SC-009 perf TU** (§7.8, §7.9),
  where the hand-built `SeraphisEngine` + `AetherReverb` pair genuinely has no `Processor` behind it.
- `CFG` (5): the quiescent-window ordering — push while quiescent, assert acceptance
  (`getRejectedConfigureTimeCallCount()` did not rise on any voice,
  `spectralStatesPendingForTest()` cleared, `morph().getStateCount()` equals the pushed count), *then*
  note-on and render ≥ 1 s and assert the spectral differential ≥ 1 % relative RMS against the same
  note-on render at the default slot assignment.

**Two rows deviate from the blanket render length, and this plan states why.** IDs **801** and **802**
are class (b) (§3.5.3), so their pushed value ramps over `kParamSmoothMs` on §3.5.2's control-chunk
grid, reaching only ~93 % of target after one 512-sample block. Their rows therefore render
**`N_block = 4` blocks** (§3.5.2 — the wall-clock settling time, unchanged by the grid) before the
read-back, which is the criterion's own per-row *render length* column doing its job. The read-back
is **exact** at that point and not merely within 1e-4, because §3.3's `wasVoiceClassBSettling_` latch
pushes the converged target once; without it these two rows would fail for a correct implementation
by ~1e-4. 1215 and 1216 are also class (b) but already carry render windows of ≥ 40 s and ≥ 60 s.

**The spec's four carve-out rows and the three inert-by-design rows are reproduced verbatim** as table
rows: 402 (`kMorphTravelRateId = kMaxTravelRate`, External, ≥ 1.5 s, `getTravelPosition()` within
1e-3), 411/412 (state count 4, rate 1.0, External, position 2.0 / 3.0, ≥ 1.7 s / ≥ 2 s), 2
(stages pinned to 1 ms, 8 notes held, 2 s, third+fifth-harmonic energy over the settled last second),
and 404/405/406's sync preconditions.

**Two thresholds are pinned by measurement at implementation time, in the `floor(min observed / 1.05)`
shape:** ID 1202's echo-density factor (spec says ≥ 1.5 × as the floor) and ID 2's harmonic-energy
floor (measured over 8 repeats, floored below the run-to-run spread). Both measured values are written
into the test TU as named constants beside the measurement note.

### 7.5 SC-004 — macros are audibly effective, and compose

*File:* `integration/macro_wiring_test.cpp`. All three cases `[.slow]`.

- **`Seraphis_MacroSweep_MovesItsAxis`** (Arm 1). Per macro: sweep **0 → 1 in 21 steps**, 4 s per step,
  fixed seed and note, each non-swept macro held at its **own** FR-060 neutral (Gravity 0.5, the rest
  0). The gate is Phase 7 SC-009 **in full** — primary metric at **|ρ| ≥ 0.9** on the arms Phase 7
  pins (Dream on the **dry voice sum** with Aether mix at neutral; Dissolve's atmosphere-band
  contribution over the settled last second; Gravity's body-decay observable on a dry,
  isolated-damping, 1–8 kHz arm; Entropy's flatness on the cloud-only arm), every secondary at
  |ρ| ≥ 0.9 in its stated direction, the per-macro minimum end-to-end effect sizes (Dream ≤ 50 % of its
  Dream = 0 value, Bloom centroid +≥ 20 % relative, Dissolve +≥ 0.15 absolute, Gravity ≥ 6 dB, Entropy
  +≥ 25 % relative), the no-discontinuity clause (no step > **3 ×** the mean step change), and the
  pinned partial detector (65 536-point FFT, Blackman-Harris, last 1 s of each step, −60 dB peak
  threshold, 20 dB peak-to-local-median SNR, parabolic interpolation, **ordinal** grid matching with an
  exact-count gate). **Spearman ρ trend, not monotonicity** — Phase 7 withdrew the monotonicity wording
  explicitly.
- **`Seraphis_MacroAndDeepParameter_Compose`** (Arm 2). Repeat Arm 1 with one deep parameter pushed to
  the spec's stated headroom-preserving value: Dream ↔ 201 at **0.060**, Bloom ↔ 200 at **0.45**,
  Dissolve ↔ 1000 at **0.30**, Gravity ↔ 802 at **0.40**, Entropy ↔ 400 at **0.10**. Gate: |ρ| ≥ 0.9
  with the **same sign** as Arm 1, end-to-end effect size **≥ 50 % of Arm 1's**, and
  `macroMatrixForTest().getTargetBase(target)` equal to the pushed deep value exactly.
- **`Seraphis_MacroSaturatesAgainstDeepExtreme`** (Arm 3). `kCloudRichnessId` pushed to **1.0**; the
  Bloom sweep's primary metric is asserted **monotone non-decreasing** (largest downward step ≤ the
  detector's own noise floor, measured on a no-op sweep in the same render) and
  `getTargetBase(CloudRichness) == 1.0`. **No effect size is required on this arm** — Bloom's +0.40
  span is entirely consumed by `setRichness`' `[0,1]` clamp (`harmonic_cloud.h:416`), which C-1
  documents and accepts.

### 7.6 SC-005 — no zipper, no click

*File:* `integration/param_continuity_test.cpp`. *Case:*
`Seraphis_ParameterAutomation_IsClickFree`, with the two positive-control sections named explicitly.
This TU owns the checked-in `kContinuityMechanism[]` array (§3.5.3) and **defines**
`Seraphis::detail::SeraphisParamSmootherBypassProbe`.

*Construction (Phase 7 SC-003's matched-regime form, applied to automation).* Per in-scope ID, a 2 s
render at 48 kHz / block 512 in which the parameter is automated extreme-to-extreme in **64 equal
steps**:

1. **Test statistic** — per automation step, `maxPerSampleDelta` over the **±10 ms window** centred on
   `step sample + AetherReverb::getLatencySamples()` (`aether_reverb.h:2612`; 1024 samples = 21.3 ms
   at 48 kHz, **more than the whole window** — without the shift the clause measures the wrong 20 ms).
2. **Reference** — one window per measured step, the **same 20 ms length**, from the **same render**,
   at offsets ≥ **50 ms** clear of any step, uniformly spaced.
3. **Bound** — `max(test) ≤ 1.5 × max(reference)`, with the **same number of draws on both sides**.
4. **Non-finite clause (all 91 IDs, no exemptions)** — no sample is non-finite, tested by bit pattern
   (`isFiniteBits`), never `std::isnan`.

*Positive controls, both mandatory.*
(a) **Detector wiring** — the same statistic over a non-step window with a deliberately injected
one-sample step of **2 ×** that window's own `maxPerSampleDelta` must **exceed** the bound.
(b) **Criterion wiring** — with the probe setting `paramSmootherBypass_ = true` so a **class-(b)**
smoother snaps instead of ramping (a deliberate un-smoothed write), the same render must **fail**
clause 3. ID **801** is the subject, because it is the class-(b) row with the most direct
signal-path consequence (`resonance_` feeds the modal gain recompute at the control step).

> **This control is only constructible because §3.5.2 put the delivery on the 64-sample grid.** The
> bypassed render delivers **100 %** of each automation step in one write; the non-bypassed render
> delivers at most `1 − e^(−64/192) = 28.35 %` per control chunk — a **3.53 ×** separation against a
> `1.5 ×` bound. Under the old per-block design the separation was 100 % against 93.0 %, i.e.
> **1.075 ×**, and the control could not fail for *any* implementation. The measured
> bypassed-vs-smoothed ratio is recorded in `compliance.md` beside the pass/fail; if it does not
> breach the bound, the remedy is §3.5.2's — lengthen `kParamSmoothMs` and re-measure — never to
> loosen clause 3 and never to drop the control.

*Exemptions from clauses 1–3, clause 4 still applies:* `kSeedId` (3) — `setSeed` is documented
configure-time on both consumers and *"redraws all 64 scatter offsets (a step of up to 2 ×
kMaxScatterCents = 14 cents per partial in one chunk)"* (`spectral_morph_engine.h:198-207`) and
*"Mid-render this is therefore a discontinuity in the drift and tide"* (`aether_reverb.h:2354-2358`);
and the five `CFG` IDs (408–412), whose gate exists precisely because mid-note application is
undefined. **85 IDs remain in scope** — the count `kContinuityMechanism[]`'s `static_assert` pins
(§3.5.3), and the set its runtime check pins, so IDs 1 and 2 cannot go missing again.

*The render set carries **three** edge combinations, not two:* `kAtmosGrainSecondsId = 30 s`;
`kAetherDecayId = 60 s` + `kAetherFreezeId = on`; and **`kBodyResonatorBypassId = on`**, under which
IDs **804** and **811** are re-measured. The third is required because `cloudDriveGain()` —
`rmsGain_ * userDrive_`, unsmoothed, applied per sample at `continuous_body.h:3406`/`:3420` — is
multiplied by `bypassGain`, which is *"EXACTLY 0 … while no bypass is engaged"* (`:3403-3405`). At
ID 812's registered default (`kDefaultResonatorBypass = false`, `:164`) the one-ID-at-a-time
construction of this criterion **structurally cannot** reach that consumer, so the class-(a)
classification of 804 and 811 would have been asserted only over the path it does reach (§1.5.3,
§3.5.3). If a step is found, 804 and 811 move to class (b) — subject, like every class-(b) ID, to
§3.5.2's grid.

*The remedy rule is one-directional* (§3.5.3): a step found on a class-(a) ID moves that ID into class
(b); it never earns an exemption and never loosens the 1.5 × bound.

### 7.7 SC-006 / SC-007 / SC-013 — allocation and cadence

*File:* `integration/param_cadence_test.cpp`.

- **`Seraphis_ParameterPush_IsAllocationFree`** (SC-006). With `TestHelpers::AllocationScope` active
  and readings from `AllocationDetector::instance().getAllocationCount()`, a 4 s render during which
  **all 91 parameters** are automated every block records **exactly 0** allocations after
  `setupProcessing()` returns. The render **also calls `setState()` on the prepared processor** at
  least once inside the measured window, so FR-041b's 2.1 KiB staging handoff and FR-047's
  `pushAllSurfaces()` are both covered. The fixture is warmed first (`reserveCapture` + one warm-up
  render) per its own banner (`seraphis_test_fixture.h:15-19`).
- **`Seraphis_ParameterPush_IsOnChangeOnly`** (SC-007). Quiescent engine, **constant tempo**, every
  class-(b) smoother settled; render **200 blocks** with no parameter change:

  | Counter | After 200 unchanged blocks | After one change of… | Δ |
  |---|---|---|---|
  | `applyVoiceParams` | **exactly 1** | class-(a) `VP` ID | **+1** |
  | | | class-(b) `VP` ID (801) | **+1 … +28** (`N_chunk`, §3.5.2), and it must **stop rising**, and the settled read-back must equal the target **exactly** (the §3.3 latch) |
  | | | **any `MB` ID** | **+0** — the third separation clause |
  | `applySpectralStates` (successes) | **exactly 1** (the first in-`process()` push; `setupProcessing()` pushes nothing itself) | one `CFG` ID | **+1** |
  | `applySpectralStates` (**attempts**) | **exactly 1** | one `CFG` ID pushed while quiescent | **+1** |
  | `applyAetherParams` | **exactly 1** | one `AE` ID | **+1** |
  | `setTargetBase` | **exactly 27** | class-(a) `MB` ID | **+1** |
  | | | class-(b) `MB` ID (802) | **+1 … +28**, and it must **stop rising**; **the other 26 targets are untouched** |
  | | | any macro knob (100–104) | **+0** — the macro push owns those smoothers (§3.5.5) |
  | **each of the four `ENG` counters** (§3.1) | **exactly 1** | one change of ID 3 (seed) or ID 1008 (freeze) | **+1 on that counter, +0 on the other three** |

  Plus the **three separation clauses** — an `AE` change must not increment `applyVoiceParams`; a `VP`
  change must not increment `applyAetherParams` (a single shared counter passes every row above and
  fails both); and an **`MB`-only change must not increment `applyVoiceParams`**. The third is new,
  and it is the assertion that catches §3.2's corrected `markDirty`: while `case Route::MB` bumped
  `voiceParamGeneration_`, every deep `MB` edit ran a 37-setter × 16-voice fan-out it did not need,
  and neither of the first two clauses could see it.

  Plus the **`ENG` cadence clause (FR-045)**, which the four counters of §3.1 exist for. After the
  200 unchanged blocks each of `engSeedPushCountForTest()`, `engPolyphonyPushCountForTest()`,
  `engSoftLimitPushCountForTest()` and `engFreezePushCountForTest()` is **exactly 1**; after one
  change of ID **3** the seed counter is +1 and the other three +0; after one change of ID **1008**
  the freeze counter is +1 and the other three +0. Plus a **`setState()` sub-clause** that pins
  §3.4's `SurfaceInvalidation::PresetLoad` arm: `setState()` with a stream whose seed index and
  polyphony **equal the current values**, followed by one block, leaves `engSeedPushCountForTest()`
  and `engPolyphonyPushCountForTest()` **unmoved** — a forced re-`setSeed()` is the drift/tide
  discontinuity `aether_reverb.h:2351-2358` documents, and §7.6 exempts `kSeedId` from SC-005 clauses
  1–3 precisely because the seed is supposed to move only on change. The same stream with a
  **different** seed index moves the seed counter by exactly 1.

  Plus the **moving-tempo clause**: with `kMorphSyncId` on and a ramped `processContext` tempo,
  `applyVoiceParams` increments on every block in which the derived rate moved by more than
  `kSyncedRateEpsilon` and on **no other block**; a *constant* tempo with sync on must not increment
  it at all; `applyAetherParams` and `applySpectralStates` must not increment on any of them.

  Plus the **retry-bound clause**, which is what `applySpectralStatesAttemptCountForTest()` exists
  for: with a `CFG` change pushed while **one** voice is sounding and fifteen are idle, the attempt
  counter rises by 1 per block (the retry is per block by design) but the **per-voice** work does not
  — asserted by checking that the fifteen idle voices' `getRejectedConfigureTimeCallCount()` is
  unchanged after the first attempt **and** that their bits left `spectralRetryMask_` on that first
  attempt, so §3.3's mask, not the flag, is what bounds the repeated `buildSanitized` work.
- **`Seraphis_SpectralStateAssignment_HonoursGate`** (SC-013). (1) Assign a new state while a voice is
  **sounding**: that voice's audible spectrum is unchanged for the note and
  `getRejectedConfigureTimeCallCount()` rises. (2) `spectralStatesPendingForTest()` **stays set** while
  any targeted voice keeps rejecting, and the parameter atomics are never cleared or reset in response.
  (3) On the **first block after every voice has become quiescent** the retry succeeds: the flag
  clears, the rejection counter stops rising on every voice, `morph().getStateCount()` equals the
  pushed count, the next note-on renders the new spectrum, and **all sixteen voices hold the same
  state** — asserted across `getVoice(i).morph().getStateCount()` for `i < kMaxVoices`.

### 7.8 SC-008 — parameter-push CPU budget

*File:* `integration/param_perf_test.cpp`. *Case:* `Seraphis_ParameterPush_CpuBudget`, `[.perf]`.

**Measured directly, never by subtracting two whole-chain renders** — 0.05 % of one core at 512/48 kHz
is 5.3 µs/block against a block budget of 10 666 666.7 ns, and Phase 7 recorded ten consecutive
best-of-16 runs of that chain spanning 18.34 %–20.07 %, a ≈180 µs/block spread, **~34 × larger than
the quantity**.

- **Steady-state arm** — one `process()`-entry pass with every generation counter unchanged, **every
  class-(b) smoother settled and the tempo constant**: the tracker comparisons, the settled-check and
  the synced-rate comparison, and nothing else. **Two bounds, both binding:** the FR-057 absolute
  ceiling of **0.05 %** (5.3 µs/block) **and** a checked-in regression baseline at
  `ceil(measured worst × 1.05)`. *The ceiling alone is near-vacuous — the subject is two or three
  `std::size_t` comparisons and a bool — so the baseline is what actually gates; the ceiling survives
  only as the FR-057 statement it implements.*
- **Worst-case arm** — the full sequence: build a `SeraphisVoiceParams` from the atomics +
  `applyVoiceParams` at polyphony **8 and 16** (both reported; the gate is the worse), 27 ×
  `setTargetBase`, `applyAetherParams`, one `applySpectralStates` **with the full `0xFFFF` mask over a
  quiescent pool** — i.e. the genuine whole-pool fan-out, **4096 `std::log2`** plus 64
  `isValidSpectralState` scans plus 64 × 128-float array comparisons (§1.3). **≤ 0.50 %** of the block
  budget (53.3 µs at 512/48 kHz) **and** ≤ the checked-in `ceil(worst × 1.05)` baseline.

  **One-directional remedy, stated because 4096 transcendentals in 53.3 µs is not obviously
  achievable and FR-057 clause 1 must not be left without an escape route.** If the measured figure
  breaches 0.50 %, bound the per-block fan-out per §3.4 — `pushSpectralStatesIfPending()` writes at
  most `kSpectralFanOutVoicesPerBlock` voices per block (start at 4), clearing their mask bits as they
  accept, so a whole-pool raise amortises over `ceil(16 / k)` blocks. Adopting it **amends SC-013
  clause 3** ("on the first block after every voice has become quiescent" → "within `ceil(16 / k)`
  blocks"), and that amendment is written into `spec.md` through §12's amendment step. **Raising the
  0.50 % ceiling is not an available remedy**, and neither is dropping §3.4's identity guard.
- **Class-(b) settling arm (new, and it exists because §3.5.2 changed the delivery grid)** — a
  512-sample block rendered while a class-(b) ID is under **continuous** automation, so the smoother
  is never settled and `process()` runs the block as **eight 64-sample sub-slices** (§3.5.4). This
  measures whole-block wall time, not just the push block, because the subject is the sub-slice
  overhead of the chain (`processStereoBlock` → `AetherReverb::processStereoBlock` →
  `processOutputStage`) and not the pushes alone. Reported against the **same block rendered
  undivided** (settled) in the same trial set, and gated on a checked-in `ceil(worst × 1.05)`
  baseline. *No absolute ceiling is asserted here* — the phase has no budget for whole-chain render
  cost, that is Phase 7 SC-001's, which §7.9 re-measures. If the ratio is large enough to threaten
  §7.9's 25 %, the remedy is §3.5.4's stated one — coarsen the grid to 128 samples and re-check §7.6's
  positive control — never per-block delivery, never a looser SC-005.

**Both arms must defeat dead-code elimination and must prove they did**: every arm consumes its result
through an optimization barrier (a `volatile` sink) **and asserts a strictly non-zero elapsed time**
for the timed region. An arm reporting 0 ns **fails**. Measurement discipline is Phase 7's:
best-of-16 per subject, ≥ 8 trials, idle machine, checked-in baseline with the
`static_assert(baseline × kRegressionFactor ≤ kReference)` tie. **No compiled-out arm.**

### 7.9 SC-009 — full-poly budget, and the 91-row non-default table

*File:* `integration/param_perf_test.cpp`. *Case:*
`Seraphis_FullPoly_CpuBudget_WithFullSurface`, `[.perf]`.

**The measurement subject is a hand-built `SeraphisEngine` + `AetherReverb` pair in the perf TU, NOT
`Processor`.** RA-1 row (c) needs `numChannels = 16` and `diffusionFftSize = 4096`, which `Processor`
**structurally cannot** produce: `makeSeraphisReverbConfig` fixes `numChannels = 8` and
`diffusionFftSize = 1024` with the comment *"MUST stay 1024 -> 1024-sample latency"*
(`seraphis_engine_config.h:68`, `:79`), and that latency constancy is already load-bearing for Phase 8
(`processor.h:62-72`). No Phase 9 parameter can change either field. The case therefore lives in the
**plugin** perf TU rather than `dsp_systems_tests` — it needs both the hand-built pair *and*
`applyAetherParams`, which is plugin-side (FR-049). Phase 7's precedent for a hand-built pair is
`dsp/tests/unit/systems/seraphis_perf_test.cpp:1184`.

*Scenario, pinned verbatim from Phase 7 SC-001:* polyphony **8**, **all 8 voices sounding**, none
idle; atmosphere **frozen** via `setAtmosphereFreeze(true)` and **asserted** by `isFreezeCaptured()`
on every voice before the measurement starts; `AetherReverb` at RA-1 row **(c)**
(`PrepareConfig{numChannels = 16, shimmerEnabled/bloomEnabled/spectralDiffusionEnabled all true,
diffusionFftSize = 4096}`, `setSize(1)`, `setDensity(1)`, 32 bloom resonators) as a **deliberate worst
case above the shipped config** — the fact that the shipped plugin prepares a cheaper reverb is a
margin, recorded alongside the figure; 512-sample blocks at 48 kHz on the composed chain
(`processStereoBlock → AetherReverb::processStereoBlock → processOutputStage`).

*Gate:* **≤ 25 % of one core** (2 666 666.7 ns/block), Phase 7's baseline discipline (best-of-16,
≥ 8 trials, `ceil(worst × 1.05)` checked in). **The 16-voice figure is measured and recorded as a
non-gating number** and is written into the roadmap amendment (§9.1). If the gate fails, the lever is
the shipped voice count or Phase 9's own push cost — never the 25 % ceiling, never a Phase 2/4/5/6 gate.

**The exhaustively enumerated non-default parameter table (one plain value per ID, all 91 rows).**
Checked into the perf TU as a constant array; applied through the **four DSP routes**
(`setTargetBase`, `applyVoiceParams`, `applyAetherParams`, `applySpectralStates`) plus the direct
`ENG` setters. It is a spec artefact, not an implementer's choice. Rule: each row takes the
**most expensive** end of its range, except the two exception classes stated per row.

| ID | Value | ID | Value | ID | Value |
|---|---|---|---|---|---|
| 0 | *n/a — processor-local* | 400 | 1.0 | 1000 | 2.0 |
| 1 | **8 — pinned by scenario** | 401 | 0.6 | 1001 | 1.0 |
| 2 | on | 402 | 3.0 | 1002 | 20.0 |
| 3 | index 3 | 403 | Spline | 1003 | 30.0 |
| 100–104 | *n/a — set on the matrix directly, at the FR-060 neutral* | 404 | 1.0 | 1004 | 1.0 |
| 200 | 1.0 | 405 | *n/a — processor-local* | 1005 | 1.0 |
| 201 | 0.1 | 406 | *n/a — processor-local* | 1006 | 1.0 |
| 202 | +12.0 | 407 | 0.5 | 1007 | 1.0 |
| 203 | 1.0 | 408 | 4 | 1008 | **on — pinned by scenario** |
| 204 | +1.0 | 409 | Bell | 1009 | 1.0 |
| 205 | 50.0 | 410 | Choir | 1010 | 12.0 |
| 206 | 0.0 | 411 | Glass | 1011 | 1.0 |
| 207 | 1.0 | 412 | Breath | 1012 | 30.0 |
| 208 | 0.05 | 600 | 1.0 | 1013 | 1.0 |
| 209 | 60.0 | 601 | 0.5 | 1014 | +24.0 |
| 210 | 1.0 | 602 | 1.0 | 1015 | 1.0 |
| 800 | Metal Plate | 603 | +1.0 | 1016 | Blackman |
| 801 | 1.0 | 604 | 150.0 | 1200 | 1.0 |
| 802 | 0.0 | 700 | Standard | 1201 | **1.0 — pinned (RA-1 (c) setSize(1))** |
| 803 | 1.0 | 701 | 60.0 | 1202 | **1.0 — pinned (RA-1 (c) setDensity(1))** |
| 804 | 4.0 | 702 | 1.0 | 1203 | 60.0 |
| 805 | 1.0 | 703 | 1.0 | 1204 | off |
| 806 | 1.0 | 704 | 10000.0 | 1205 | 1.0 |
| 807 | 30.0 | | | 1206 | 0.0 |
| 808 | 1.0 | | | 1207 | 200.0 |
| 809 | 0.0 | | | 1208 | 1.0 |
| 810 | 1.0 | | | 1209 | 1.0 |
| 811 | on | | | 1210 | 1.0 |
| 812 | **off — pinned by scenario** | | | 1211 | 1.0 |
| | | | | 1212 | 1.0 |
| | | | | 1213 | 1.0 |
| | | | | 1214 | 1.0 |
| | | | | 1215 | 1.0 |
| | | | | 1216 | 1.0 |
| | | | | 1217 | 1.0 |

**The THREE exception classes, stated per row above.** SC-009's wording is *"an exhaustively
enumerated **non-default** parameter table … all 91 rows"*, so every row whose value equals the C-6
registered default is a declared deviation or a defect. Two classes were already recorded; the third
is recorded here rather than being rediscovered downstream (§7.13 found five of its nine members by
inspection and reported them as a bug).

*Class 1 — not applicable, processor-local (8 rows):* IDs **0** (master gain), **100–104** (the
macros, whose matrix values are set directly here, at the FR-060 neutral so the base overrides reach
the voices unmodified — the deep surface is this criterion's subject) and **405/406** (the sync pair,
a `Processor` computation feeding ID 404).

*Class 2 — pinned by the scenario (5 rows):* **1** = 8; **1008** = on; **812** = **off**, because
bypassing the resonators would remove the body engines from the very chain this criterion budgets;
**1201** = 1.0 and **1202** = 1.0, which are RA-1 row (c)'s `setSize(1)` / `setDensity(1)`.

*Class 3 — the most-expensive end **coincides with** the registered default (10 rows):* **2**
(on, `spec.md:553`), **208** (0.05 s, `:574`), **700** (Standard, `:605`), **803** (1.0, `:631`),
**805** (1.0, `:633`), **808** (1.0, `:636`), **810** (1.0, `:638`), **811** (on, `:639`), **1204**
(off, `:687`) and **1217** (1.0, `:700`). Each is defensible under this table's own "most expensive
end" rule — 208's shortest attack is the busiest envelope, 2's `on` keeps the saturator live, 1204's
`off` keeps the shimmer and bloom returns live, 811's `on` keeps the AGC estimator running, and the
five unity rows keep their signal paths at full contribution — but each **is** the C-6 default, so
none of them is a non-default row and the class must be declared. It costs the criterion nothing:
SC-009 measures CPU, and a row at the default is still at its most expensive end. *Two class-2 rows
(**1** = 8 and **812** = off) also land on their defaults; three do not (**1008** = on vs off,
**1201** = 1.0 vs 0.50, **1202** = 1.0 vs 0.70).*

**SC-023's override table is DERIVED by a stated rule, not enumerated by inspection.** SC-023
requires every one of the 91 rows to differ from its registered default (no `n/a` rows, no pinned
rows), so its value table is **SC-009's table with an override for exactly:**

> **(i)** every **class-1** row — all 8 processor-local IDs, which have no DSP route but *are*
> persisted C-8 fields; **plus (ii)** every row, in any class, whose SC-009 value **equals** its C-6
> registered default — the 10 class-3 rows plus class-2's **1** and **812**.

8 + 12 = **20 overridden IDs**, which is exactly §7.13's table. The rule is what makes that table
re-checkable rather than a list someone has to re-audit: §7.13's per-row `!=` assertion against the
same checked-in C-6 *Default* column is the mechanical gate, and any future edit to §7.9's values
re-derives (ii) automatically. *An earlier review pass enumerated class 3 as nine rows; applying the
rule adds **ID 2**, which §7.13 had already overridden without recording why.*

Three row choices carry a note rather than being self-evident:
- **800 = Metal Plate** as the modal material with the largest mode set. The table-authoring step
  **records `getActiveModeCount()` (`continuous_body.h:1453`) for all five materials** and takes the
  maximum; if another material wins, the table row changes and the measurement is redone.
- **1204 = off**, not on: freeze makes the shimmer and bloom returns inert (`aether_reverb.h` FR-033
  step 5), i.e. **cheaper**. The criterion wants the worst case.
- **1206 = 0.0** and **809 = 0.0**: minimum damping is the longest ring, i.e. the most sustained work.

### 7.10 SC-010 / SC-011 / SC-012 — state

*File:* `unit/state_v2_test.cpp`.

- **`Seraphis_StateRoundTrip_IsExact`** (SC-010). For a randomized-but-valid setting of all 91
  parameters (fixed seed), `getState` → `setState` → `getState` produces **byte-identical** streams of
  length **2532**, and every controller-side value after `setComponentState` equals the processor's,
  within `1e-6` normalized. Legitimate under the no-float-goldens rule because the bytes are **stored
  parameter values**, not arithmetic results (`dsp/CLAUDE.md`), and because `SpectralState`'s round
  trip is documented exact (`spectral_state.h:270-272`).

  **One extra assertion, localized rather than inferred:** the controller-side stream position
  immediately after the `[morph]` block equals the processor-side position at the same point. That is
  the direct check on §2.3.0's discard loop — `loadMorphParamsToController` must consume the
  4 × 541 = 2164 payload bytes it has nowhere to put — and without it the failure mode is 55
  parameters reading garbage from a 2164-byte-misaligned cursor, which is a diagnosis nobody wants to
  do from the parity assertion alone.
- **`Seraphis_StateVersion_MigratesAndRefuses`** (SC-011). A hand-built **36-byte version-1** stream
  loads without error; the eight Phase 8 parameters take their stream values; **all 83 Phase 9
  parameters read back at their registered defaults**; and the subsequent 4 s render satisfies
  **SC-002's pass condition using SC-002's construction verbatim** (same-build, same-TU Arm B from the
  Phase 8 shipped defaults, per-sample `maxAbsDiff ≤ 1.0e-5` over both channels, `compareFingerprints`
  warn-only). *The word "fingerprint" does not belong in this clause: SC-002 produces no fingerprint
  reference and forbids a checked-in one.* A **version-3** stream is refused (`kResultFalse`) with no
  state mutated. Version-2 streams truncated at each of **12 chosen byte offsets** — deliberately
  including offsets inside a 541-byte payload, at a block boundary, and one byte short of the end —
  load without crash and leave the remainder at defaults.
- **The Phase 8 state-test migration (step 8a).** Three existing files are edited **in the same step
  that lands the v2 format**, before `seraphis_tests` is run, so the suite is never knowingly red:

  1. **`unit/state_roundtrip_test.cpp` is kept and migrated, not deleted.** It covers Phase 8's
     FR-045/FR-046 clauses — the fresh-processor default stream, byte stability, the truncation ladder
     and the future-version refusal — over the *scalar prefix*, and `unit/state_v2_test.cpp` does not
     reproduce them. The edits, exhaustively:
     - `kStateBytes` 36 → **2532** (C-8), and a new `constexpr int32 kV1StateBytes = 36;` beside it
       named for what it now is: the **strict v1 prefix** of the v2 stream (§5.1);
     - `decodeState` (`:145-…`) keeps reading the nine v1 fields but its `REQUIRE(s.getSize() ==
       kStateBytes)` (`:146`) becomes `REQUIRE(s.getSize() == kStateBytes)` against the new 2532 and
       the helper is renamed `decodeV1Prefix` — it decodes a prefix, not "a complete state stream",
       and its banner (`:144`) says so;
     - the two other size assertions (`:215`, `:237`) take 2532;
     - `CHECK(int32AtOffset(*s, 0) == 1)` (`:219`) becomes `== 2`, beside the existing symbolic
       `CHECK(int32AtOffset(*s, 0) == Seraphis::kCurrentStateVersion)` (`:218`) — the literal is what
       makes the symbolic assertion non-vacuous and it is kept, not dropped;
     - the truncation ladder's final rung `if (n >= kStateBytes)` (`:316`) becomes
       `if (n >= kV1StateBytes)`, because `entropy` completes at byte 36, not 2532;
     - the future-version section (`:329-331`) becomes `future.version = Seraphis::kCurrentStateVersion
       + 1; REQUIRE(future.version == 3);`, and the same at `:403` for the controller half.
  2. **`integration/param_flow_test.cpp:211` and `unit/lifecycle_test.cpp:88`** write
     **`Seraphis::kStateVersion1`** (FR-012's new symbol, §2.1(d)) instead of `kCurrentStateVersion`.
     Both streams are 36-byte v1 bodies and stay that way: relabelling them makes each an honest v1
     stream that additionally exercises FR-093's migration, where writing `kCurrentStateVersion` over a
     v1 body would have them pass only by EOF-safety. Their layout comments (`:186-192`, `:80-84`) gain
     the words "version 1".
  3. **No CMake change** — all three files are already in the `seraphis_tests` list
     (`plugins/seraphis/tests/CMakeLists.txt:9`, `:13`, `:11`).

  *Verification for the step:* `seraphis_tests` green **in full**, not just the new cases. A migration
  that leaves `state_roundtrip_test.cpp` red is invisible to every new case in `state_v2_test.cpp`,
  because they are different files.
- **`Seraphis_SpectralStateSlots_RoundTripExactly`** (SC-012). Each of the five factory states
  (`makeFactoryState`, `spectral_state.h:373`) assigned to a slot, saved and reloaded, compares
  **equal field-by-field** (ratios, amplitudes, name, tilt, inharmonicity, numPartials). **The
  comparison is against `Processor`'s FR-041b copy through `spectralSlotForTest(slot)` — never against
  the engine slot, which cannot express it**: `SpectralMorphEngine::setState` stores sanitized
  (`:285-313`) and the class exposes no per-slot getter (full const surface `:392-456`), so three of the
  six named fields are unreadable there and the ratios are log2-transformed. A slot fed 541 bytes of
  garbage deserializes to `false` and leaves `spectralSlots_[slot]` **bitwise unchanged**.

### 7.11 SC-019 / SC-020 — rate independence and seed

*File:* `integration/param_character_test.cpp`.

- **`Seraphis_ParameterSurface_IsSampleRateIndependent`** (SC-019). The **settled last second** of a
  4 s render of note 60 at fixed seed and identical parameter settings, **65 536-point FFT,
  Blackman-Harris**, metrics over the **20 Hz – 16 kHz** band only (a band all three rates resolve —
  a centroid computed to Nyquist is not comparable between 44.1 and 96 kHz by construction).
  Thresholds: output **RMS within 1.0 dB** across 44.1 / 48 / 96 kHz; band-limited **spectral centroid
  within 5 %**; **spectral flatness is recorded, not gated** — it is dominated by the stochastic
  atmosphere and reverb tails, whose realisation is exactly what
  `RollingCaptureBuffer::prepare`'s power-of-two capacity rounding changes between rates (an 8.8 %
  rate-dependent spread in ring seconds, per Phase 5). The "no denormalization reads `sampleRate`"
  clause lives in **FR-019** (§2.3.4), not here.
- **`Seraphis_Seed_IsDeterministicAndDistinct`** (SC-020). *Operating point, pinned:* note **60**,
  velocity **100**, held 3 s then released, **4 s** total, **48 kHz**, block **512**, polyphony 8,
  registered defaults **except** `kCloudDriftDepthId` (205) at **25 cents** and `kBodyMaterialId` (800)
  at **Glass**. Both deviations are required: at the registered defaults cloud drift depth is **0.0
  cents** (`seraphis_voice.h:295`), and `ContinuousBody::setSeed` drives *"exactly one thing — the
  per-voice modal micro-detune … on the three MODAL materials only"*, with Strings and Chamber
  documented **seed-independent** (`continuous_body.h:1323-1348`).
  *Clause 1:* two `Processor` instances with identical parameters including `kSeedId` render within
  `render_fingerprint.h` tolerance.
  *Clause 2:* two instances differing **only** in `kSeedId` differ in total variation by more than a
  gate **derived from measurement**: the implementation step renders **all 16 entries of `kSeedValues`**
  at this operating point, records the pairwise spread, and sets the gate at
  `floor(min observed spread / 1.05)`. **A small spread is a defect of the checked-in table (C-10) and
  is fixed by re-picking the offending constant and re-measuring** — lowering the gate is not an
  available remedy, and neither is re-examining the engine's seed derivation. The measured table is
  checked in beside the constant.
  *Clause 3:* `kSeedValues[0] == 1u` asserted directly, so `kSeedId` at its registered default seeds
  engine and reverb exactly as `kEngineSeed` / `kReverbSeed` do (`seraphis_engine_config.h:28-29`) —
  SC-002's negative control depends on it.

### 7.12 SC-016 / SC-017 / SC-021 — harness, pluginval, lints

- **SC-016.** `unit/controller/editor_lifecycle_test.cpp` is unchanged in shape:
  `exerciseEditorLifecycle(controller, "editor", uidescPath, 3)`
  (`tests/test_helpers/editor_lifecycle_harness.h:102-105`). It must complete with **zero reports**
  under (a) the **valgrind-nightly editor-lifecycle job**, which already builds and runs
  `seraphis_tests` (`.github/workflows/valgrind-nightly.yml:273-283`), and (b) a **local
  `-DENABLE_ASAN=ON` Debug run**. *There is no ASan CI lane, and adding one is out of this phase's
  scope.*
- **SC-017.** `tools/pluginval.exe --strictness-level 5 --validate
  "build/windows-x64-release/VST3/Release/Seraphis.vst3"` exits 0, including its automated sweep over
  all 91 parameters. Captured to a log on the **first** run and recorded.
- **SC-021.** `node tools/check-portability.js` clean; `node tools/lint-odr.js`, `lint-layers.js`,
  `lint-float-bit-goldens.js`, `lint-arch-guarded-includes.js`, `lint-simd-aligned-loadstore.js` clean;
  `tools/run-clang-tidy.ps1 -Target seraphis` **and** `-Target dsp` clean; zero compiler warnings on
  all three OS legs.

### 7.13 SC-023 — a preset loaded into a prepared processor reaches the DSP

*File:* `unit/state_v2_test.cpp`. *Cases:* `Seraphis_PresetLoadAfterPrepare_ReachesDsp`
(clauses 1–6) and `Seraphis_SampleRateChange_RePushesEverySurface` (clause 7).

1. Prepare a `Processor`, then render **one** block so every prepare-time push has happened and its
   trackers are equalised.
2. Build a **non-default** stream in which **every one of the 91 parameters** differs from its
   registered default, written by the same `getState` path from a processor driven to **this
   criterion's own table** (below), and feed it to `setState()`.
3. Render **one** block.
4. Assert, for **every route**, that the DSP holds the preset value, using the same read-back surfaces
   SC-003 names and no others: the 37 `VP` rows through `getVoice(i)` for every `i < kMaxVoices`; the
   27 `MB` rows through `macroMatrixForTest().getTargetBase(...)` **and** the post-slice voice-side
   read-back (polyphony pinned to 16); the 10 `AE` rows through
   `applyAetherParamsCallCountForTest()` incrementing plus a re-push comparison, **with at least two
   of the ten additionally asserted through a rendered observable** (below); the 4 `ENG` rows through
   `getPolyphony()` / `getAtmosphereFreeze()` / `getSeed()` /
   **`getOutputSaturation()`** (§1.6); the 5 `CFG` rows through `spectralSlotForTest(slot)`
   field-by-field **and** `getVoice(i).morph().getStateCount()` once `spectralStatesPendingForTest()`
   has cleared. *One-block exactness is what `snapParamSmoothers_` (§3.4) exists for.*
   *An earlier revision named "the soft-limit state" here with no route to it: `SeraphisEngine` ships
   `setOutputSaturation` (`seraphis_engine.h:566-570`) and no engine-level getter, so the row was
   unreachable through the engine's access wall and clause 7(c) inherited the hole. §1.6 adds
   `SeraphisEngine::getOutputSaturation()` as a **pure const forwarder to the read-back
   `TapeSaturator` already ships** — `getSaturation()` at `tape_saturator.h:283-285`, returning the
   `std::clamp`ed `saturation_` that `setSaturation` stores before targeting the smoother (`:248-252`)
   — and records it in FR-072's list and FR-006's symbol count (32 → 33), which is the paired-FR route
   the spec requires. **A second, engine-owned copy of the value is explicitly rejected**: it would be
   a source of truth that can silently diverge from what the saturator holds, which is the failure
   mode this row exists to exclude.*

   **The two rendered `AE` rows are mandatory, and they are ID 1207 and ID 1203.** `AetherReverb` has
   no getter for any of the ten (SC-003 establishes this, `spec.md:1647-1651`), so
   "`applyAetherParamsCallCountForTest()` incremented plus a re-push comparison" degenerates to *"a
   push happened"* — which passes for **any** loaded value. Nothing else in the phase closes that
   hole: a self-inverse save/load field-order error (a swap of two same-typed aether fields between
   `saveAetherParams` and `loadAetherParams`) survives **SC-010**, whose round trip compares the
   second and third streams — both produced *after* the same load/save cycle, so a self-inverse
   permutation is invisible — and survives **SC-003**, which drives each ID through
   `IParameterChanges` and never through the state path. The two cheapest rendered observables are
   used: **ID 1207 (`kAetherPreDelayId`) → onset shift** (this table's value is 200 ms against a 0 ms
   default, so the first non-trivial output sample moves by 200 ms ± one control chunk against the
   same render at the default) and **ID 1203 (`kAetherDecayId`) → T60** (60 s against a 4.0 s default;
   the tail's −60 dB point on the same window SC-003's 1203 row uses). §7.4 already establishes both
   are reachable: *"Every observable in those rows — echo density, T60, onset shift, morph position,
   L/R correlation — is measurable from a `Processor` render"*. The remaining eight rows keep the
   counter-plus-re-push form, **and clause 7(c) repeats all ten in the same split**. *(The
   alternative — an `aetherParamsForTest()` const read surface on `Processor` — was rejected because
   it reads the **pack**, not the reverb: it would prove the loader wrote the atomic and still not
   prove the value reached `AetherReverb`.)*
5. Assert `spectralHandoffConsumeCountForTest()` (§3.1) is **0 immediately after `setState()`
   returns** — the staging copy did **not** happen on the message thread — and **exactly 1 after the
   clause-3 block** — it happened once, on the audio thread. *An earlier revision reproduced this
   clause with no seam behind it: `spectralSlotsHandoff_` is private and
   `spectralStatesPendingForTest()` cannot distinguish "consumed once" from "consumed twice" or from
   "copied on the message thread".*
6. **Negative control.** With `requestPushAllSurfaces()` stubbed out of `setState()` (a compile-time
   test-TU switch), the same assertions **must fail**. Without this the criterion cannot distinguish
   "the preset reached the DSP" from "the DSP already held those values". *The seam is named against
   the design §3.4 actually has: `setState()` performs one release store and the helper body runs
   from `process()`, so there is no `pushAllSurfaces()` call in `setState()` to remove.*
6a. **The before-prepare section** — `Seraphis_PresetLoadBeforePrepare_ReachesDsp`, in the same case
   file. FR-047 and the *Edge cases* → *State* bullet both make `setState()` **before**
   `setupProcessing()` legal (spec `:1228`, `:2361-2362`), and no criterion covered it because SC-023
   is explicitly the after-prepare case. Construction: `setState()` with this criterion's table on a
   **fresh, unprepared** `Processor`, then `setupProcessing()`, then one block. Assert the four
   `spectralSlotForTest(slot)` payloads and `getVoice(i).morph().getStateCount()` hold the **preset's**
   states, not the registered defaults — which is what §3.7's "seed `factoryStates_` and
   `spectralSlots_` from the **current atomics**, not the defaults" rule buys, and what a
   defaults-seeding `setupProcessing()` would silently destroy with nothing ever re-deriving it
   (`refreshSpectralSlotFromFactory()` runs only on a `CFG` parameter change, §3.2).
7. **The re-prepare arm.** Continuing from the processor of clauses 1–5 — prepared at **44 100 Hz**,
   holding this table on every route, having rendered ≥ 1 block — call `setupProcessing()` again at
   **96 000 Hz** and render **one** block. Assert:
   **(a)** `getVoice(i).body().getEngineSampleCount(e)` reads **0** for every `i < kMaxVoices` and every
   `ContinuousBody::Engine` immediately after the re-prepare and before the new block — the counter is
   documented *"Cleared by reset()/prepare()"* (`continuous_body.h:1532-1537`) and was non-zero at the
   end of clause 3;
   **(b)** `AetherReverb::isPrepared()` (`aether_reverb.h:2486`) is true and
   `getEffectiveDelayLengthSamples(ch)` (`:2506`) changed in proportion to the rate ratio for every
   channel — the tank lengths are stored at `kReferenceSampleRate` and rate-scaled inside `prepare()`;
   **(c)** clause 4 repeated **verbatim** over the same 37/27/10/4/5 rows against the same table — a
   freshly prepared voice carries `SeraphisVoice::prepare()`'s defaults, so any row that was not
   re-pushed reads its **default** and the assertion fails loudly;
   **(d)** with `pushAllSurfaces()` stubbed out of `setupProcessing()`, (c) **must fail** while (a) and
   (b) still pass.
   *This clause creates no new introspection beyond the two additions clause 4 and clause 5 record*
   (`SeraphisEngine::getOutputSaturation()` in FR-072's list, §1.6; and
   `spectralHandoffConsumeCountForTest()` in FR-041a's list, §3.1): otherwise only FR-072 accessors,
   getters that already ship, and the FR-041a/FR-041b/FR-049 seams clause 4 already names. Both
   additions are recorded in the FR that creates the seam, not invented at the criterion, which is
   the spec's standing rule.

**SC-023's own value table — no pinned rows, no `n/a` rows.** SC-009's table is a CPU artefact and
reusing it would make this criterion vacuous at exactly the rows most likely to break (a `setState`
that silently dropped polyphony would still pass, because 8 is the registered default). SC-023's table
is **SC-009's table with the overrides §7.9's derivation rule produces** — *(i)* all 8 class-1
processor-local rows, *(ii)* every row whose SC-009 value equals its C-6 default (class 3's ten, plus
class 2's **1** and **812**) — **20 IDs**, listed below, so that **every** one of the 91 rows differs
from its registered default. The list is derived, not audited: re-applying §7.9's rule must reproduce
it exactly.

| ID | SC-023 value | C-6 default | Why it differs from SC-009 |
|---|---|---|---|
| 0 | 1.5 | 1.0 | processor-local; participates (it is persisted state) |
| 1 | **16** | 8 | SC-009 pins 8, which is the default |
| 2 | **off** | on | SC-009's `on` is the default |
| 100–104 | 0.7, 0.3, 0.6, 0.2, 0.8 | 0, 0, 0, 0.5, 0 | processor-local; participate |
| 208 | **30.0** | 0.05 | SC-009's 0.05 is the default |
| 405 | **on** | Free | processor-local; participates |
| 406 | index 6 (`4 Bars`) | index 4 | processor-local; participates |
| 700 | **Growth** | Standard | SC-009's `Standard` is the default |
| **803** | **0.0** | 1.0 | **SC-009's 1.0 is the default** |
| **805** | **0.5** | 1.0 | **SC-009's 1.0 is the default** |
| **808** | **0.4** | 1.0 | **SC-009's 1.0 is the default** |
| **810** | **0.6** | 1.0 | **SC-009's 1.0 is the default** |
| 811 | **off** | on | SC-009's `on` is the default |
| 812 | **on** | off | SC-009 pins `off`, which is the default |
| 1204 | **on** | off | SC-009's `off` is the default |
| **1217** | **0.5** | 1.0 | **SC-009's 1.0 is the default** |

The five bold-ID rows (803 `kBodyKeyTracking`, 805 `kBodyMix`, 808 `kBodyCloudSize`, 810
`kBodyWidth`, 1217 `kAetherWidth`) were left at SC-009's value in an earlier revision, and SC-009's
value for each **is** the registered default (C-6 rows at `spec.md:631`, `:633`, `:636`, `:638`,
`:700`) — they are class-3 members of §7.9's third exception class, and the derivation rule now
produces them rather than an inspection pass rediscovering them. That break was clause 2 outright,
and it broke clauses 4 and 7(c) in the way the criterion's own
prose warns about for `kPolyphonyId`: those five rows would have asserted *"the DSP holds the
default"*, which passes even if `setState` dropped them entirely, and clause 7(c)'s whole force — *"a
freshly prepared voice carries `prepare()`'s defaults, so every row that fails to be re-pushed reads
its default and the assertion fails loudly"* — cannot hold for a row whose expected value **is** the
default.

**The construction rule is checked, not trusted.** The table is a `constexpr` array of
`{id, plainValue}` and the test TU asserts, **per row and over all 91 rows**, that `plainValue`
differs from that ID's registered default — the same checked-in C-6 *Default* column
`Seraphis_RegisteredDefaults_AreExact` (§7.3) uses, compared with exact `!=` for scalars and index
inequality for `L`/`T` rows. A future edit that reintroduces a default-valued row fails that check
before it ever reaches clause 4, and the failure names the ID.

Every other row is SC-009's value, each already non-default. **The eight processor-local IDs
participate**: they have no DSP route (clause 4's assertions do not reach them) but they *are*
persisted C-8 fields, and leaving them at their defaults would leave the byte range that most
exercises FR-093's reader untested. *"All except the pinned ones" is not an acceptable weakening —
there are no pinned ones.*

### 7.14 SC-018 — host-synced travel

*File:* `unit/morph_sync_test.cpp`. *Case:* `Seraphis_MorphSync_DerivesAndFallsBack`. Uses the fixture's
new `ProcessContext` hook (§7.0).

**The observable for clauses 1–4 is `engineForTest()->getVoice(0).morph().getTravelRate()`**
(`spectral_morph_engine.h:441`), which returns the pushed `travelRate_` exactly. An earlier revision
named *"`getVoice(0).morph()`'s travel behaviour"* — i.e. inferring the rate from how far the travel
position moved — which cannot meet the criterion's `1e-5` equality gate: at clause 3's 1.0417e-2
journeys/s with 2 states the position moves ~1.1e-4 per 512-sample block, so the inferred rate is
dominated by the position quantum and by `advanceTravel`'s own slew cap
(`spectral_morph_engine.h:716-725`). `getTravelRate()` ships and reads the pushed value directly, so
there is no reason to infer it. `applyVoiceParamsCallCountForTest()` remains the secondary on every
clause (the rate must have been *pushed*, not merely computed), and the render/travel-behaviour check
survives only in clause 5, whose assertion is "does not go silent".

1. **Derivation.** 120 BPM, sync on, `1 Bar` (index 4), 4/4 → `120/(60·4) = 0.5` journeys/s within `1e-5`.
2. **Upper clamp.** 200 BPM, `1/16` (index 0, `beatsPerJourney = 0.25`) → 13.33 clamps to
   `kMaxTravelRate = 1.0` (`spectral_morph_engine.h:102`).
3. **No clamp at the slow end, asserted as an exact value.** 20 BPM, `8 Bars` (index 7, 32 beats at
   4/4) → `20/(60·32) = 1.0417e-2`, which is **above** `kMinTravelRate = 1/600 = 1.667e-3` (`:101`);
   the pushed rate equals that within `1e-5` and **no clamp engages**. *(The lower clamp is unreachable
   through the sync path — it needs `beatsPerJourney > 10 × BPM`, i.e. 200 beats at 20 BPM, which C-7's
   longest 32-beat entry does not reach. It survives as a defensive bound and is exercised by clause 5.)*
4. **Time signature.** `kTimeSigValid` set, 6/8 → `barBeats = 6 · (4/8) = 3`, so `1 Bar` at 120 BPM
   gives `120/(60·3) = 0.667`; with the flag clear the same setting falls back to `barBeats = 4` and
   0.5. Both halves of C-7's rule are exercised.
5. **Fallback.** `processContext == nullptr`, and separately an invalid tempo flag: the free-running
   `kMorphTravelRateId` value is used unchanged and the render does not go silent.

These five clauses check the **derivation**, not the **cadence** — the every-block recompute is
asserted by SC-007's moving-tempo clause (§7.7). Neither covers the other; both are required.

---

## 8. Build integration

### 8.1 CMake test lists (FR-101)

**Neither source list is globbed.** A test file not listed silently drops.

**Existing test files this phase EDITS** (no CMake entry moves for any of them — all four are already
in the `seraphis_tests` list): `unit/state_roundtrip_test.cpp`, `integration/param_flow_test.cpp`,
`unit/lifecycle_test.cpp` and `integration/processor_audio_test.cpp` (§7.0's table gives the reason
and the edit for each; `unit/param_denorm_test.cpp` takes a comment-only correction). They are listed
here because §8.1's "the existing entries stay untouched" is about the **CMake list**, and an
implementer reading only §8.1 would otherwise conclude the existing *files* are untouched — which is
false, and one of them fails to compile-and-pass without its edit.

- **`plugins/seraphis/tests/CMakeLists.txt`** — add the **nine** new files to the
  `add_executable(seraphis_tests …)` list (`:5-31`), keeping the existing entries and the second
  compilation of `../src/processor/processor.cpp` and `../src/controller/controller.cpp` (`:16-18`)
  untouched. Nothing else in the target changes: `SERAPHIS_RESOURCES_DIR` (`:57-60`) already points
  SC-015 at `editor.uidesc`, and `catch_discover_tests(seraphis_tests REPORTER console)` (`:73`) picks
  the new cases up.
  Extend the `-fno-fast-math -fno-finite-math-only` source-properties list (`:63-70`) with
  **`integration/param_continuity_test.cpp`** (SC-005 clause 4 tests finiteness by bit pattern but the
  positive control injects a step, and the TU also asserts non-finite rejection paths) and
  **`unit/state_v2_test.cpp`** (SC-012 feeds a slot 541 bytes of garbage). No other new file injects
  NaN/Inf, and **`integration/param_perf_test.cpp` must NOT be listed** — those flags move the figures
  its baselines are pinned to (the same rule `dsp/tests/CMakeLists.txt:735-740` records for the Phase 7
  perf TU).
- **`dsp/tests/CMakeLists.txt`** — add `unit/systems/seraphis_param_broadcast_test.cpp` to the
  `add_executable(dsp_systems_tests …)` list beside the four existing Seraphis Phase 7 entries
  (`:355-359`). It must **not** be added to the `-fno-fast-math` list at `:735-740` (only
  `seraphis_nonfinite_test.cpp` is there); its non-finite section uses bit-pattern construction.

### 8.2 Targets to build and run

```bash
CMAKE="C:/Program Files/CMake/bin/cmake.exe"

# DSP additions (four headers => only the systems layer relinks)
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_systems_tests
build/windows-x64-release/bin/Release/dsp_systems_tests.exe 2>&1 | tail -5

# Plugin
"$CMAKE" --build build/windows-x64-release --config Release --target seraphis_tests
build/windows-x64-release/bin/Release/seraphis_tests.exe 2>&1 | tail -5

# The perf and slow cases are excluded by default; run them explicitly
build/windows-x64-release/bin/Release/seraphis_tests.exe "[.perf]" 2>&1 | tail -20
build/windows-x64-release/bin/Release/seraphis_tests.exe "[.slow]" 2>&1 | tail -20

# The bundle, for pluginval
"$CMAKE" --build build/windows-x64-release --config Release --target Seraphis
tools/pluginval.exe --strictness-level 5 --validate \
  "build/windows-x64-release/VST3/Release/Seraphis.vst3"
```

**Blast radius of the `dsp/` edits.** `seraphis_engine.h`, `seraphis_voice.h`,
`seraphis_macro_matrix.h` and `continuous_body.h` are Layer 3 headers. `continuous_body.h` is also
consumed by Membrum-adjacent tests through `dsp_systems_tests`, so **`dsp_systems_tests` must be run in
full**, not just the new case. The other four per-layer exes do not include any of the four files and
need not be rebuilt for this phase; if the local build tree says otherwise, run them.

### 8.3 Registration outside `plugins/`

**Nothing.** Phase 8 already registered Seraphis in every CI/tooling roster (root `CMakeLists.txt`,
`ci.yml`'s ~17 sites, `release.yml`, `valgrind-nightly.yml`, both `run-clang-tidy` scripts,
`check-changelog-coverage.js`, `gen-specs-index.js`). Phase 9 adds **no new build target, no new
source directory and no new tool**, so no roster changes. The only non-`plugins/` files this phase
touches are `dsp/` (§1), `dsp/tests/CMakeLists.txt` (§8.1) and `specs/Seraphis-roadmap.md` (§9.1).

### 8.4 Pre-commit gates (FR-104)

`node tools/check-portability.js` on every changed C++ file, and
`tools/run-clang-tidy.ps1 -Target seraphis` plus `-Target dsp` (the `.sh` equivalents on
Linux/macOS). Both must be clean before commit — a green Windows build is not evidence about the other
two legs.

---

## 9. Documentation and roadmap edits

### 9.1 `specs/Seraphis-roadmap.md` (FR-058) — six clauses, all in the shipping change

1. **Amend line 313 in place**, in the dated-parenthetical shape the Phase 5 budget amendment already
   used on this document (`:250-254`). The amendment records: that the gate is **8 voices**, not 16;
   the owner ruling that set it (2026-07-30, Phase 7's RQ-1); the **measured** 8-voice figure; and the
   **measured** 16-voice figure SC-009 records as non-gating (§7.9).
2. **Re-verify every roadmap-line citation** in this spec, in `specs/seraphis-phase9-parameters/*` and
   in `plugins/seraphis/src/plugin_ids.h:46` **after all** of clauses 1, 3, 4 and 5 have been applied.
   Citations **before** line 313 (113, 148, 186, 245) are unaffected; citations **after** it (383,
   386–388, 500–508, 511, 514, 516, 517) shift by the number of lines added and MUST be corrected in
   the same change. A citation left stale by this edit is a defect of **this** phase. **Applied
   2026-08-01: those eight became 396, 399–401, 550–558, 561, 564, 571 and 572**; line 313 itself does
   not move, because the amendment starts on it.
3. **Strike Open Question 2** (line 514 pre-amendment, 564 after), marked resolved by Phase 3 —
   mandatory and unconditional.
4. **Write the Phase 11 inheritance into the Phase 11 entry**: Phase 11 owns the three `SpectralState`
   authoring mutators (`setPartial`, `blendStates`, `tiltState`) **and** Phase 3's
   validity-preservation criterion over them, alongside the per-partial editing surface that is their
   only consumer, and `HarmonicCloud::setSpectralTarget` / `setPartialPosition` / `setPartialMask`.
5. **Move Open Question 5** (line 517 pre-amendment, 572 after) **to a new named phase** rather than striking it: per-note
   expression ships, and that phase owns **both** the `SeraphisVoice` per-voice expression inputs and
   `INoteExpressionController`. The entry records that the controller-FUID host-cache hazard is
   **accepted**, so it is not re-litigated later as a discovery.
6. **Record** that polyphony values 9…16 are user-reachable and outside the budgeted scenario.
   Reducing the registered maximum below 16 is out of Phase 9's scope (it would be a parameter-range
   change at a shipped ID, C-9), and raising the gate to 16 by relaxing the 25 % ceiling is forbidden.

### 9.2 Plugin docs (FR-103)

- `plugins/seraphis/CLAUDE.md` — the param-ID table's Phase column: bands 200–1399 marked **9 —
  shipped**; 1400+ stays Phase 10. The "MPE / note expression is a known **Phase 9** decision" entry
  under *Decisions that outlive Phase 8* is rewritten to record RQ-2's actual ruling (a new named
  phase owns both halves; the FUID hazard is accepted).
- `plugins/seraphis/CHANGELOG.md` — a section for the version this phase ships, so
  `tools/check-changelog-coverage.js` finds it. `version.json` is the only other file a version bump
  touches; generated files are never hand-edited.
- `specs/seraphis-phase9-parameters/compliance.md` — written by the compliance pass, and it must carry
  (a) the FR-019 `grep` command **and its verbatim output**, (b) the full list of touched `dsp/` files
  (FR-071), (c) every measured number this plan defers to measurement (§7.4, §7.8, §7.9, §7.11),
  (d) the pluginval log, and (e) the **diff of §12.1's amendments A1–A9 as applied to `spec.md`** in
  step 0 (and step 13 for A9). **It carries no "logged deviation" section**: every conflict between
  this plan and the spec is resolved by amending the spec before implementation, so the compliance
  pass grades against text nothing in the shipped code violates.

---

## 10. Risks and mitigations

| # | Risk | Mitigation |
|---|---|---|
| R1 | **A `MB` parameter is also written through `SeraphisVoiceParams`, double-applying it.** | Structural: `SeraphisVoiceParams` has no field for any of the 27 targets (§1.1), `baseValueForTarget()` is the only `MB` mapping (§3.5.5), and `SeraphisVoiceParams_IsDisjointFromMacroTargets` (§7.2) is the runtime check. |
| R2 | **A log-mapped default fails SC-022's exact `==`.** | Two construction rules (§2.3.1): bounds are `static_cast<double>(<DSP constant>)`, and every registered `defaultNormalizedValue` is computed through the same mapping. The float round-trip was analysed for the hard cases; SC-022 is the per-ID gate. |
| R3 | **`mn == 0` on a log map produces NaN for every non-zero normalized value**, silently making the parameter inert (FR-003's `isFiniteBits` would keep the `kRows` literal). | The 1 ms floor on IDs 702–704 and the `lin` choice for ID 1012, both stated normatively (§2.3.2). SC-003 catches any recurrence, and SC-005 clause 4 catches the non-finite half. |
| R4 | **`setState()` writes 90 values that never reach the DSP** — the classic silent-preset-load bug. | `pushAllSurfaces()` from both `setupProcessing()` and `setState()` (§3.4), with **SC-023** and its negative control (clause 6) as the gate. |
| R5 | **`setState()` racing `process()` tears a 540-byte `SpectralState`.** | §3.7's **three-buffer** staging ring with the `handoff` / `consuming` index pair, which covers all three directions the single-flag design did not: a second `setState()` arriving mid-copy (the writer interlock), a `getState()` racing the audio thread's `CFG` refresh (`getState` no longer reads `spectralSlots_` at all), and the `setState → process` publication itself. Plus the reduction of `pushAllSurfaces()`'s message-thread footprint to a single release store (§3.4). SC-006 shows the 2.1 KiB copy allocates nothing; SC-023 clause 5 shows it is consumed exactly once, on the audio thread, through `spectralHandoffConsumeCountForTest()`. |
| R5a | **A `CFG` automation point runs `makeFactoryState`'s ~200 transcendentals inside `processParameterChanges`, in a region no SC-008 arm measures.** | §3.2: the five states are precomputed once at prepare into `factoryStates_` and the `CFG` path is a 540 B POD copy behind a slot-index change guard. The header's own *"CONFIGURATION-TIME, not audio-thread"* banner (`spectral_state.h:371-372`) is the citation, and the precompute is legal because the function is documented deterministic and stateless (`:349-351`). |
| R5b | **The FR-046 retry re-sanitizes four states on every already-accepting voice, every block, for the length of a held note.** | §3.3's per-voice retry mask + §1.3's `voiceMask` parameter: a voice that accepted is never written again. `applySpectralStatesAttemptCountForTest()` (§3.1) is the seam SC-007's retry-bound clause (§7.7) asserts against; no retry interval is introduced, so SC-013 clause 3 keeps its exact "first block after quiescence" wording. |
| R5c | **The SUCCESS path costs more than the retry path it was optimised away from.** A whole-pool `applySpectralStates` is 16 × 4 = 64 `buildSanitized` calls = **4096 `std::log2`** — more than R5b's 3840 — in ONE `process()` call, and `pushAllSurfaces()` raised it unconditionally on every `setState()` and every `setupProcessing()`, even with nothing changed. | §3.4's identity guard: the raise happens only when a slot id actually moved or the engine was re-prepared (§3.2's `lastPushedSlotStateId_` compare, applied at the invalidation site). §7.8's worst-case arm measures the genuine whole-pool case explicitly and carries a **stated one-directional remedy** — the per-block fan-out bound with §12.1's pre-drafted A9 amendment to SC-013 clause 3 — so a breach of FR-057's 0.50 % has a route that is neither a relaxed ceiling nor an implementation choice. |
| R5d | **The `setState()` force-push re-seeds the engine for an unchanged seed.** `pushAllSurfaces()` invalidated `lastPushedSeedIndex_` unconditionally, and the consume sat *after* `pushGlobalParams()`, so the sentinel survived into the next block and forced `setSeed` on both engine and reverb — a documented drift/tide discontinuity (`aether_reverb.h:2351-2358`) on every preset load, plus a redundant `setPolyphony()` that moves `setPolyphonyCallCountForTest()`. | §3.3(A) moves the consume **above** `pushGlobalParams()`; §3.4's `SurfaceInvalidation` scope raises the seed and polyphony sentinels on the **re-prepare** path only. §7.7's `ENG` cadence clause and its `setState()` sub-clause are the gate, via §3.1's four new counters. |
| R17 | **An EXISTING Phase 8 test is left red by the v2 state format.** `unit/state_roundtrip_test.cpp` hard-codes 36 bytes and version 1 in six assertions; none of the nine new files would report it, because they are different files. | §7.0's edited-existing-files table, §7.10's migration sub-section (edit by edit), §8.1's explicit "existing files this phase EDITS" list, and §11 **step 8a**, whose verify column is "`seraphis_tests` green **in full**". FR-051's deletion target is likewise named by path and line (`integration/processor_audio_test.cpp:856`) rather than as "Phase 8's SC-023 negative control". |
| R18 | **A `getState()` before the first `setupProcessing()` writes four empty spectral payloads.** A zeroed `SpectralState` **passes** `isValidSpectralState` (`spectral_state.h:82-145`), so the §5.4 zero-fallback never fires and the preset saves valid-but-empty slots that reload cleanly into silence. | §3.7 fills `factoryStates_` **at construction**, not at prepare — `makeFactoryState` is documented deterministic, stateless and rate-independent (`spectral_state.h:349-351`), so there was never a reason to defer it. §5.4's FR-094 argument now names this as its precondition. |
| R6 | **A re-prepare at a new sample rate leaves voices on `SeraphisVoice::prepare()`'s defaults** — the *Edge cases* bullet calls this "the single most likely implementation slip in the phase", and SC-002 cannot catch it (defaults match defaults). | `setupProcessing()` calls `pushAllSurfaces()` (§3.4); **SC-023 clause 7** is the 44.1 k → 96 k arm with its own negative control. |
| R7 | **The class-(b) settling push never stops**, burning CPU forever. | `isComplete()` is `\|current − target\| < 1.0e-4` (`smoother.h:232-234`) and the smoothers are advanced by each sub-slice's own sample count, so settling is guaranteed in `N_chunk` pushes / `N_block` blocks of that row's own family — 28/4 for the body constant, 415/52 for the aether-depth constant (§3.5.2). SC-007's class-(b) rows assert both `1 ≤ Δ ≤ N_chunk` **and** that the counter stops rising. §3.3's `wasVoiceClassBSettling_` latch is what makes the last push carry the **exact** target rather than leaving the voice ~1e-4 short. |
| R7a | **The class-(b) smoother delivers a block-rate staircase rather than a ramp**, so the mechanism removes nothing SC-005 measures and its own positive control cannot fail. | §3.5.2: delivery is on an **absolute 64-sample control-chunk grid** (the grid every class-(a) smoother in this engine already uses, `continuous_body.h:1433`, `:3713-3744`), with `process()` subdividing its slices only while un-settled. Per-chunk delivery is 28.35 % vs the probe's 100 %, a 3.53 × separation against SC-005's 1.5 × bound. §7.8's third arm measures the subdivision's cost; §7.6 records the measured bypassed-vs-smoothed ratio. |
| R7b | **The grid is correct but the ORDER is not**: `setParamSmootherTargets()` inside `advanceParamSmoothers()` leaves `anyClassBSmootherUnsettled()` reading a stale target on the first slice after every change, so that block is never subdivided and delivers 93.0 % of the step in one push — the R7a failure, reinstated, on exactly the blocks that matter. | §3.5.4 proves the four-link chain and hoists the call into §3.3's pre-slice block, where the master-gain target hoist already lives (`processor.cpp:360-367`). §11 step 9's verify column makes the **measured** bypassed-vs-smoothed ratio the gate: a ratio near 1.075 × instead of 3.53 × is the signature of the un-hoisted form, and §7.6's positive control (b) then fails as it should. |
| R7c | **`classBSmoothers()` allocates.** Its return type was left unstated in an earlier revision, and it runs twice per sub-slice, up to 32 times per 2048-sample block while settling. | §3.5.4 pins the signature: `std::array<Krate::DSP::OnePoleSmoother*, 9>` by value, with the RT banner every other new audio-thread symbol carries. SC-006 is the backstop, but only if the settling window overlaps the measured render — which is why the type is pinned rather than left to the implementer. |
| R8 | **Denormals in an idle class-(b) smoother.** | `OnePoleSmoother::process`/`advanceSamples` call `detail::flushDenormal` (`smoother.h:208`, `:250`) and snap to target at the completion threshold; `ScopedDenormalMode` is armed for the whole of `process()` (`processor.cpp:303`). |
| R9 | **`-ffast-math` folds a finiteness test on the macOS leg.** | No new `std::isnan` / `std::isinf` / `std::numeric_limits<>::infinity()` anywhere. FR-003 uses the matrix's own `isFiniteBits` (`seraphis_macro_matrix.h:685-689`); SC-005 clause 4 uses a bit-pattern check; the two TUs that inject non-finite inputs are on the `-fno-fast-math` list (§8.1) and build their inputs from bit patterns regardless. |
| R10 | **MSVC-only green.** `constexpr` over an SDK constant, narrowing in brace init, an aggregate initialised positionally. | `node tools/check-portability.js` before every commit; designated initializers everywhere; anything initialised from an SDK constant is `const`, never `constexpr`. |
| R11 | **A bit-exact float golden creeps into SC-002 or SC-011.** | Both are same-binary, same-TU `maxAbsDiff` comparisons with `compareFingerprints` warn-only, and a checked-in fingerprint reference is **forbidden** (§7.1, §7.10). `node tools/lint-float-bit-goldens.js` is in SC-021. |
| R12 | **A new test file is added but not to the CMake list**, so it silently never runs. | §8.1 names both lists explicitly; the compliance pass counts `TEST_CASE`s discovered by `ctest` against the list. |
| R13 | **`sizeof(Processor)` breaches the 64 KiB `static_assert`.** | Measured budget ≈ 12.1 KiB (§3.1), ~5× headroom; the nine 4-slot-equivalent `SpectralState` arrays (one live + three staging + the five-entry factory table) are 11 340 B of it. The `static_assert` at `processor.h:104` is the automatic gate. |
| R14 | **`SeraphisEngine`'s access wall is widened** to let the plugin at a mutable voice. | It is not: `getVoice()` stays `const` (`:696`), `voices_` stays private (`:734`), the `friend` list is unchanged (`:738-740`), and the three new engine methods are the only route. FR-071's diff review records every touched `dsp/` file. |
| R15 | **The `apply()` polyphony residue is mistaken for a Phase 9 defect.** | C-2 records it: the 27 `MB` values still stop at `apply()`'s own `getPolyphony()` bound (`seraphis_macro_matrix.h:625-626`), which FR-004/FR-071 forbid changing. It is pre-existing Phase 7 behaviour; SC-003's `MB-voice` rows pin polyphony to 16 test-side and leave the residue untouched. |
| R16 | **The measured thresholds (§7.4, §7.9, §7.11) are chosen rather than measured.** | Each is written as `floor(min observed / 1.05)` or `ceil(worst × 1.05)` with the raw measurement pasted into `compliance.md`. SC-020's seed spread in particular is a **property of the checked-in table**: a small spread means re-picking a constant, never lowering the gate. |

---

## 11. Implementation order

Each step ends green — build clean, its own tests pass — before the next starts. Steps 1–3 are `dsp/`
only and can be verified with `dsp_systems_tests` alone.

| # | Step | Verify |
|---|---|---|
| **0** | **Apply §12.1's spec amendments A1–A8 to `specs/seraphis-phase9-parameters/spec.md`** (A9 is conditional on §7.8's measurement and lands in step 13 if at all). **No code in this step.** It is first because every later step's tests are written from the spec, and A1/A3 in particular change what SC-007's class-(b) rows assert | `spec.md` carries A1–A8; no FR or SC in the document contradicts §3.5.2, §3.4, §3.7, §1.6 or §7.3; the compliance pass has no "logged deviation" rows left to write |
| 1 | `seraphis_voice.h`: the 13 forwarders (§1.5) + `growth()` (§1.6); `continuous_body.h`: the 12 accessors (§1.6) | `dsp_systems_tests` green; `SeraphisVoice_Phase9Forwarders_AreOneToOne`, `ContinuousBody_Phase9Accessors_ReturnClampedStoredValues` pass |
| 2 | `seraphis_engine.h`: `#include <type_traits>` (§1.1), `SeraphisVoiceParams`, `applyVoiceParams`, `applySpectralStates` **with its `voiceMask`** (§1.1–1.3), and `getOutputSaturation()` as a **one-line const forwarder to `satL_.getSaturation()`** — no new member (§1.6) | the four broadcast cases of §7.2 pass, including the mask section; `seraphis_engine.h`'s diff outside §1.1–1.3 is `const`-only |
| 3 | `seraphis_macro_matrix.h`: `setTargetBase` / `resetTargetBases` / `getTargetBase` + the one `evaluateAll` line (§1.4) | `SeraphisMacroMatrix_TargetBaseOverride_Composes` **and** `SeraphisMacroMatrix_DefaultBases_Unchanged` pass |
| 4 | `plugin_ids.h` (§2.1) + `dropdown_mappings.h` (§2.2) | compiles; `static_assert`s on the seed and grain-envelope tables hold |
| 5 | The six packs + the `global_params.h` seed extension (§2.3) | `Seraphis_ParameterSurface_IsComplete`, `Seraphis_RegisteredDefaults_AreExact`, `Seraphis_Phase8Parameters_AreFrozen` pass |
| 6 | Controller registration / formatting / `setComponentState` (§4) | as above, plus SC-016 still green |
| 7 | `applyAetherParams` (§2.4); processor members, dispatch, the pre-slice block, `pushGlobalParams` extension (§3.1–3.3) | `Seraphis_EveryParameter_ReachesDsp` (SC-003) passes |
| 8 | `pushAllSurfaces(SurfaceInvalidation)` + the staging handoff + state v2 (§3.4, §3.7, §5) | SC-010, SC-011, SC-012, SC-023 pass |
| **8a** | **Migrate the three existing Phase 8 state tests** (§7.10): `unit/state_roundtrip_test.cpp` to the v2 size/version/prefix (six assertions), and `integration/param_flow_test.cpp:211` + `unit/lifecycle_test.cpp:88` to `kStateVersion1`. In the **same step**, not a later one — step 8 makes `state_roundtrip_test.cpp` red | **`seraphis_tests` green IN FULL**, not only the new cases. No new CMake entries |
| 9 | Class-(b) smoothing **on the absolute 64-sample grid**: `setParamSmootherTargets()` hoisted to §3.3's pre-slice block (**not** inside `advanceParamSmoothers`, §3.5.4), `classBSmoothers()` returning `std::array<OnePoleSmoother*, 9>` by value, the slice subdivision while un-settled, the `wasVoiceClassBSettling_` latch, per-target `setTargetBase` settling, `kContinuityMechanism[]` (85 rows, `static_assert` + set check), the FR-059a probe (§3.5) | SC-005 passes **with both positive controls**, and the measured bypassed-vs-smoothed ratio is recorded and is **> 1.5 ×** (a ratio near 1.075 × means the target hoist was not applied); SC-007's class-(b) rows pass at `Δ ≤ 28`; §7.8's third arm has a checked-in baseline |
| 10 | Morph sync (§3.6) | SC-018 passes; SC-007's moving-tempo clause passes |
| 11 | `editor.uidesc` control-tags (§6) | SC-015 passes |
| 12 | Macro wiring: `MacroParams` → `setMacros`, and **delete `SECTION("Seraphis_MacroParametersAreInert")` at `plugins/seraphis/tests/integration/processor_audio_test.cpp:856`, together with its `:843-855` banner** (FR-050, FR-051) — it now asserts the opposite of shipped behaviour | SC-002 and SC-004 pass; `grep -n "MacroParametersAreInert" plugins/seraphis/tests/` returns nothing |
| 13 | Measurement pass: SC-008 (**all three arms**, including the worst-case whole-pool `applySpectralStates`), SC-009, SC-020 cl. 2, SC-003's two measured floors; check in every baseline. **If SC-008's worst-case arm breaches 0.50 %**, adopt §7.8's per-block fan-out bound **and apply §12.1's amendment A9 to `spec.md` in the same commit** | `[.perf]` and `[.slow]` runs green; figures recorded; A9 applied or explicitly recorded as not needed, with the measured µs/block beside it |
| 14 | Roadmap + docs edits (§9), then FR-058 clause 2's **citation re-verification sweep** | `check-changelog-coverage.js` finds the entry; every roadmap citation re-checked |
| 15 | pluginval, portability, clang-tidy, the ASan/valgrind editor-lifecycle run (§7.12) | SC-016, SC-017, SC-021 green |

**Two deletions this phase owes, and neither is optional.** FR-050: `macro_params.h`'s inertness
banner (`:5-9`) is rewritten to describe the Phase 9 wiring. FR-051: Phase 8's SC-023 negative
control is **`SECTION("Seraphis_MacroParametersAreInert")` at
`plugins/seraphis/tests/integration/processor_audio_test.cpp:856`** — named here because FR-051 does
not name it and "Phase 8's SC-023 negative control" is not a file path. It is **superseded by SC-004
and must be deleted** (banner `:843-855` with it), not left asserting the opposite of the shipped
behaviour. *Its own Phase 8 banner says "Do not delete it … rewrite it to assert that the two renders
DIFFER"; FR-051 supersedes that instruction, because SC-004 §7.5 already asserts the differ-case with
a Spearman-ρ gate over a 21-step sweep — an inverted `fingerprintsMatch` would be the weaker
duplicate. The compliance pass records this as the resolution of the conflicting banner.*

---

## 12. Traceability check — every deferral is discharged

| Spec deferral | Discharged in |
|---|---|
| FR-059 cl. 2 — class-(b) time constant | §3.5.2, the clause's **per-ID column** form: `kParamSmoothMs = 20.0f` (801, 802) and `kAetherDepthSmoothMs = 300.0f` (100–104, 1215, 1216), both on the absolute 64-sample grid; `N_chunk`/`N_block` = 28/4 and 415/52 at 512/48 kHz |
| FR-059 — the classification with evidence | §3.5.3, all 85 in-scope IDs (`static_assert`ed count + a runtime set check); class (b) is exactly 9 |
| FR-056 / Q3 — tempo sample point + epsilon | §3.6, once per `process()`; `kSyncedRateEpsilon = kMinTravelRate × 1e-3` |
| FR-070 #12 — the FR-033a interaction | §1.5.3, all three consequences (i)–(iii) discharged, **and** the second (`cloudDriveGain()`) consumer the header note does not cover |
| FR-047 / Q2 — `pushAllSurfaces()` | §3.4, one sequence, two entry points, one release store off the audio thread |
| FR-041b / Q8 — `spectralSlots_` concurrency | §3.7, three-buffer staging ring + `handoff`/`consuming` index pair; `getState()` reads neither `spectralSlots_` nor an unpublished buffer |
| SC-009 — 91-row non-default table | §7.9, with its **three** declared exception classes |
| SC-023 cl. 2 — its own table | §7.13, **derived** from §7.9's classes by a stated rule |
| SC-003 — the two measured floors | §7.4, pinned by measurement in step 13 |
| SC-020 cl. 2 — the seed spread gate | §7.11, a property of C-10's checked-in table |
| SC-008 / SC-009 — perf baselines | §7.8, §7.9, `ceil(worst × 1.05)` |
| FR-058 — the roadmap amendment | §9.1, all six clauses |
| FR-019 — the recorded `grep` | §2.3.4, verbatim output into `compliance.md` |

### 12.1 The spec amendments — written into `spec.md` BEFORE implementation (step 0)

**These are not deviations to be logged. They are edits to `spec.md`, and they land as step 0 of §11.**
An earlier revision recorded them as *"either written back into the spec text or logged in
`compliance.md`"*. The second half of that disjunction is not acceptable: a logged deviation means the
compliance pass grades the implementation against FR text the plan **deliberately violates**, and a
test written from the unamended spec fails a plan-conformant implementation. The sharpest case is
FR-042 amendment 1, whose *"MUST NOT run the push per **slice**"* (`spec.md:1179-1183`) and *"per
block, never per slice"* (`:1389-1395`) directly contradict §3.5.2/§3.5.4, and whose SC-007 rows fix
`N = ceil(settling time / block) = 4` against this plan's `N_chunk = 28`.

| # | `spec.md` location | Amendment |
|---|---|---|
| A1 | **FR-042 amendment 1** (`:1179-1183`) and its restatement (`:1389-1395`) | Replace "once per block … per block, never per slice" with: *"the push that owns a class-(b) row MUST run on the engine's **absolute 64-sample control-chunk grid** (`SeraphisEngine::kControlChunkSamples`), for which `process()` MUST cap its slice length at the distance to the next grid boundary while — and only while — any class-(b) smoother is un-settled, and MUST stop as soon as every class-(b) smoother reports settled. The grid is absolute across slices and across `process()` calls, so the ramp is host-block-size independent; a per-**slice** ramp, whose boundaries move with MIDI placement, remains forbidden."* Rationale recorded in the amendment: a once-per-block push delivers 93.0 % of the step at 512 samples and 99.99 % at 2048, which is a staircase, not a ramp, and makes SC-005's positive control (b) structurally incapable of failing. |
| A2 | **FR-059(b) clause 1** | Replace *"exactly the shape `masterGain_` already uses"* with *"advanced by each sub-slice's own sample count and delivered on the absolute 64-sample control-chunk grid of A1"*. `masterGain_`'s shape is per-**output-sample** (`processor.cpp:641-645`), which a push-based surface cannot have; the unamended clause and A1's predecessor were in direct conflict, and this is the edit that removes the conflict rather than resolving it silently at implementation time. |
| A3 | **SC-007** class-(b) rows (`:1886-1898`) | Restate `N` as **two** numbers: `N_chunk = ceil(tau·ln(D/kCompletionThreshold) / chunkSeconds) = 28`, which is what the **push-count** rows assert, and `N_block = ceil(settling time / block) = 4`, which is what the **render-length** columns of SC-003/§7.4 use. The current single `N` conflates them. |
| A4 | **FR-047** (`:1230-1232`) and **FR-091** (`:1450-1455`) | Replace *"MUST then call FR-047's `pushAllSurfaces()`"* / *"its only two callers"* with *"`setState()` MUST raise a **single release-store request** that `process()` consumes at the top of the next block, before `pushGlobalParams()`; `setupProcessing()` calls the helper directly, with the audio thread stopped. The helper body is shared and is the only place the trackers are invalidated."* Reason: writing ~40 tracker scalars and calling engine setters from the message thread races `process()`, which the spec's own *Edge cases* → *State* bullet permits to run concurrently. Add the `SurfaceInvalidation` scope (§3.4): the seed and polyphony sentinels are raised on the **re-prepare** path only, because forcing an unchanged seed through `setSeed()` is the drift/tide discontinuity `aether_reverb.h:2351-2358` documents. |
| A5 | **FR-041b clause 5** (`:1154-1156`) | Replace *"`getState()` serializes from `spectralSlots_`"* with *"from the published staging buffer while a handoff is outstanding, and from `factoryStates_[morphParams_.slot[i]]` otherwise; it MUST NOT read `spectralSlots_`, which is audio-thread-owned."* The original is an unsynchronised message-thread read of a 2160-byte non-atomic array the audio thread writes; the substitute is bitwise identical by construction (`makeFactoryState` is *"Deterministic and stateless"*, `spectral_state.h:349-351`), so FR-094 is unaffected. Add: *"`factoryStates_` is built at construction, not at prepare"* (§3.7). |
| A6 | **FR-072** (`:969-999`) and **FR-006** (`:891-898`) | FR-072's table gains a **fourteenth** row — `SeraphisEngine::getOutputSaturation()`, a const forwarder to `TapeSaturator::getSaturation()` (`tape_saturator.h:283-285`), serving ID 2 (`ENG`) — and FR-006's enumeration becomes **six groups, thirty-three public symbols** (1 + 1 + 3 + 1 + 13 + **14**). Reason: SC-023 clause 4 names *"the soft-limit state"* as an `ENG` read-back with no engine-level route to it. |
| A7 | **FR-045** (`:1200-1203`) | Add the verification seam: *"the four `ENG` push counts MUST be observable through FR-041a test-only accessors, one per value"*, so "on change only" has a criterion (§3.1, §7.7). Add the matching SC-007 row and a Traceability row for FR-045, which currently has none. |
| A8 | **FR-061** (`:1422-1425`) | Add a Traceability row pointing at `Seraphis_ParameterSurface_IsComplete`'s new formatting section (§7.3), so the "no formatter claims a dropdown ID" clause has a criterion. |
| A9 | **SC-013 clause 3** — *conditional* | Amended **only if** §7.8's worst-case arm breaches 0.50 % and the per-block fan-out bound is adopted: *"on the first block after every voice has become quiescent"* → *"within `ceil(16 / kSpectralFanOutVoicesPerBlock)` blocks"*. Recorded here so the remedy has a stated route and is not taken silently. |

**Two surfaces added beyond the spec's lists, each recorded in the FR that creates it (A6, A7 above,
plus):** `Processor::spectralHandoffConsumeCountForTest()` and
`applySpectralStatesAttemptCountForTest()` → FR-041a (§3.1), because SC-023 clause 5 and SC-007's
retry bound had no seam. `applySpectralStates`' `voiceMask` (§1.3) is a defaulted parameter on an
existing symbol and adds none; `pushAllSurfaces`' `SurfaceInvalidation` argument (§3.4) is likewise a
parameter on a symbol A4 already introduces.

**§12 is the change log of that edit.** After step 0 lands, no row above is a deviation — each is the
spec text, and the compliance pass grades against it.

---

## 13. Review notes (2026-08-01, second revision)

**No review issue was rejected.** Every blocker, major and minor from both 2026-08-01 review passes is
applied above. This section records the places where an issue offered alternative remedies and this
plan had to choose, plus the two places where an issue's *mechanism* was wrong even though its
conclusion was right — so neither the choice nor the correction is re-litigated later as a discovery.

1. **Class-(b) smoothing (§3.5) — option (i), not option (ii).** The issue offered either a finer
   delivery grid or *"drop the processor-side smoother entirely for these IDs and add the target's own
   smoother where the value is consumed"*. Option (ii) is unavailable for two of the nine class-(b)
   IDs: **1215** and **1216** land on `AetherReverb::setSizeBreathDepth` / `setDimensionalityTideDepth`
   (`aether_reverb.h:2320`, `:2328`), a **Layer 4** file that FR-071's four-file list does not include.
   It would also require adding non-`const` behaviour to `continuous_body.h`, whose carve-out §1.6
   keeps `const`-only precisely to bound the Phase 4 regression surface. Option (i) covers all nine
   with one mechanism, so it is what §3.5.2 specifies — with the cost of the subdivision measured by
   §7.8's new third arm rather than assumed.
2. **IDs 804 / 811 (§1.5.3, §3.5.3) — corrected evidence plus a third SC-005 edge combination, not a
   move to class (b).** The issue offered either. `cloudDriveGain()`'s unsmoothed path is live only
   while `bypassGain` is non-zero, i.e. only with ID 812 on or ramping (`continuous_body.h:3403-3405`),
   which is off at the registered default — so the honest fix is to *measure* it rather than to buy
   §3.5.2's machinery for a path that may not need it. The remedy rule stays one-directional: if §7.6's
   third edge combination finds a step, both IDs move to class (b).
3. **SC-023 clause 4's soft-limit row (§7.13) — option (a), the accessor.** The issue offered either
   adding `SeraphisEngine::getOutputSaturation()` or substituting SC-003's rendered third+fifth-harmonic
   detector. The accessor was chosen because clause 7(c) repeats clause 4 **verbatim** after a
   re-prepare, at which point a 2 s / 8-notes-held harmonic measurement would have to be repeated too —
   turning a one-block read-back into a multi-second render inside the re-prepare arm. The accessor is
   `const`-only surface on a file the phase already extends, and it is recorded in FR-072's list and
   FR-006's count above. *(2026-08-01, second pass: the accessor is a **forwarder** to
   `TapeSaturator::getSaturation()`, `tape_saturator.h:283-285`, not a new engine member — the earlier
   revision's claim that no getter existed anywhere was wrong, and the duplicate state it added was a
   second source of truth. §1.6 carries the correction.)*
4. **The spectral fan-out (§1.3, §3.4, §7.8) — option (a) is mandatory, option (b) is the stated
   remedy.** The issue offered an identity guard on `pushAllSurfaces()`'s spectral raise, a per-block
   fan-out bound, or both. (a) is applied unconditionally, because the redundant case — every voice
   already holding the identical sanitized state — should cost nothing and currently costs 4096
   `std::log2`. (b) is **not** applied unconditionally, because it changes what SC-013 clause 3
   asserts and this plan does not spend a spec amendment before a measurement says it is needed; it is
   written into §7.8's worst-case arm as the one-directional remedy, with A9 pre-drafted in §12.1 so
   adopting it is an amendment and not a quiet relaxation.
5. **§7.9's third exception class contains TEN rows, not the nine the review listed.** Applying the
   stated rule — "SC-009 value equals the C-6 registered default" — over all 91 rows adds **ID 2**
   (`kSoftLimitId`, SC-009 `on`, default `on`, `spec.md:553`), which §7.13's override table already
   carried without recording why. The derivation now produces it. This is the whole point of stating
   the rule instead of enumerating the list.
6. **§3.7's before-prepare hole is real; the review's mechanism for it is not, and the real one is
   worse.** The issue argued that an all-zero `factoryStates_` makes `serializeSpectralState` return 0
   and write 541 zero bytes per slot, which reload rejects. Read this session: a zeroed
   `SpectralState` **passes** `isValidSpectralState` — `numPartials == 0` skips the ratio/amplitude
   loop, the all-zero `name` terminates at index 0, and `tiltDbPerOct = 0` / `inharmonicity = 0` are
   inside `[-12, 12]` / `[0, 0.1]` (`spectral_state.h:82-145`, bounds at `:51-55`). So the zero path
   never fires and the saved preset carries four **valid, empty** payloads that reload cleanly and
   install four silent slots — a silent-corruption failure rather than a rejected-and-defaulted one,
   and one no criterion looks for. The fix is the one the issue proposed (fill `factoryStates_` at
   construction; `makeFactoryState` is stateless and needs no sample rate), and §3.7 and §5.4 now
   state the correct mechanism.

