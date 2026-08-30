# Implementation Plan: Seraphis Phase 7 — Voice & Engine

**Spec:** `specs/seraphis-phase7-voice-engine/spec.md`
**Roadmap:** `specs/Seraphis-roadmap.md` → Part A → Phase 7 (lines 288–315)
**Deliverable:** three Layer 3 headers + one test-only helper header + five test TUs. No plugin code.

> **Evidence rule.** Every `file:line` in this plan was opened in the session that produced it. Where the
> plan contradicts the spec, the contradiction is called out explicitly under **§1 Forced design
> decisions** or **§10 Deviations** with the header evidence that forces it. Nothing is inferred from a
> component's name.

---

## §0 Deliverables, layers, ODR

### 0.1 Files created

| Path | Kind | Layer |
|---|---|---|
| `dsp/include/krate/dsp/systems/seraphis_voice.h` | shipped header | 3 |
| `dsp/include/krate/dsp/systems/seraphis_engine.h` | shipped header | 3 |
| `dsp/include/krate/dsp/systems/seraphis_macro_matrix.h` | shipped header | 3 |
| `tests/test_helpers/seraphis_chain.h` | test-only header | n/a (may include Layer 4) |
| `dsp/tests/unit/systems/seraphis_voice_test.cpp` | test TU | — |
| `dsp/tests/unit/systems/seraphis_engine_test.cpp` | test TU | — |
| `dsp/tests/unit/systems/seraphis_macro_test.cpp` | test TU (SC-009/SC-010, `[.slow]` grid) | — |
| `dsp/tests/unit/systems/seraphis_nonfinite_test.cpp` | test TU (SC-018, IEEE FP) | — |
| `dsp/tests/unit/systems/seraphis_perf_test.cpp` | test TU (SC-001/SC-002, `[.perf]`) | — |

Include direction: `seraphis_voice.h` includes Layers 0–2 plus the Layer 3 peers it owns
(`harmonic_cloud.h`, `spectral_morph_engine.h`, `continuous_body.h`, `atmosphere_engine.h`).
Its Layer 0/1/2 includes are listed **explicitly**, never left to a transitive path — in particular
`core/env_curve.h` (which declares `EnvCurve`, `:24`) and `primitives/envelope_utils.h` (which declares
`RetriggerMode`, `:64`) are two *different* headers and both are included by name; `EnvCurve` currently
reaches this TU only through `multi_stage_envelope.h`, which is exactly the kind of transitive dependency
that breaks on a sibling refactor.
`seraphis_engine.h` includes `seraphis_voice.h` + `voice_allocator.h`.
`seraphis_macro_matrix.h` includes `seraphis_engine.h` + `core/modulation_curves.h` and **no Layer 4
header** (FR-056, FR-070). `tools/lint-layers.js` scans only `dsp/include/krate/dsp`
(`tools/lint-layers.js`, `DSP_INC`), so the test helper's `effects/aether_reverb.h` include is
out of scope by construction — no exclusion needed.

### 0.2 ODR sweep (run this session)

`grep -rEn "(class|struct|enum class|using) <Name>\b" dsp/ plugins/ tests/` → **0 matches** for every one of:
`SeraphisVoice`, `SeraphisEngine`, `SeraphisMacroMatrix`, `SeraphisMacro`, `SeraphisVoiceConfig`,
`SeraphisEngineConfig`, `SeraphisMacroValues`, `SeraphisAetherTargets`, `SeraphisMacroTargetOwner`,
`SeraphisMacroTarget`, `SeraphisMacroRow`, `EnvelopeMode`, `BloomEvents`.

`SeraphisMacroTarget` and `SeraphisMacroRow` are **new type names not listed in the spec's New-components
table**; they are the concrete shape of FR-058's `{macro, owner, target, amount, curve}` row and are swept
above.

Everything this plan's review passes added — `resetForSteal`, `applyStage`, `advanceOneChunkLifeOnly`,
`orbit()`, `getVoiceAllocationSerial`, `isFiniteBits`, `kStageCurve`, `kVoiceSizeBound`,
`kResetsPerControlChunk`, `kFreezeRetriesPerChunk`, `kBaselineHeadroom`, and from the second round
`setVoiceWidthBasePercent`, `getVoiceWidthBasePercent`, `hasRenderedSinceNoteOn`,
`runPreRenderControlStep`, `runPostRenderControlStep`, `widthBase_`, `widthSpan_`, `carryIsLifeOnly_`,
`renderedSinceNoteOn_`, `orphanTail_`, `retriggerSlot_` — is a **class-scoped member or constant**, so the
sweep above is unchanged and no new namespace-scope name enters the codebase.

**`EnvelopeMode` and `BloomEvents` are class-scoped, not namespace-scoped** — `SeraphisVoice::EnvelopeMode`
and `SeraphisEngine::BloomEvents`. Both are generic enough that a future component could collide at
namespace scope, and `HarmonicCloud` already sets the precedent for class-scoping a collision-prone name:
*"CLASS-scoped on purpose: `Krate::DSP::kMaxPartials = 96` already exists at namespace scope in
`processors/harmonic_types.h:21`, and a namespace-scope redeclaration here is a hard redefinition error"*
(`harmonic_cloud.h:132-138`). Every new constant in all three headers is likewise class-scoped.

---

## §1 Forced design decisions

These are the places where the shipped headers dictate a structure the spec's prose does not describe.
Each is load-bearing; getting it wrong fails a named criterion.

### D1 — Chunk-aligned rendering with a carry FIFO (this is the phase's central design decision)

**The spec assumes all four generators share an absolute control grid. Two of them do not.**

`ContinuousBody`, `AtmosphereEngine` and `AetherReverb` anchor their control grid to an absolute sample
counter and are partition-invariant by construction:

- `continuous_body.h:1182-1188` — `const auto toGrid = kControlChunkSamples - (sampleCounter_ % kControlChunkSamples);`
- `atmosphere_engine.h:689-690` — *"FR-005's ABSOLUTE control grid. The grid is anchored to `sampleCounter_`, NOT to block starts: a 64-sample chunk split 36 + 28 …"*
- `aether_reverb.h:2181-2195` — `const auto phase = sampleCounter_ % kControlChunkSamples; if (phase == 0u) runControlStep();`

`HarmonicCloud` does **not**. Its render loop restarts the chunk walk at `done = 0` on **every call**:

```cpp
// harmonic_cloud.h:908-912
std::size_t done = 0;
while (done < numSamples) {
    const std::size_t chunk = std::min(kControlChunkSamples, numSamples - done);
    updateControl(chunk);
```

and `updateControl(n)` performs **exactly one** control application per call regardless of `n`: one
`++driftReadCount_` (`:1677`), one per-partial read of `detuneLanes_.smoothCur[i]` / the mutation lane
(`:1698-1714`) held constant for the whole sub-chunk, and one per-partial envelope advance sized by
`chunkSeconds = numSamples * invSampleRate_` (`:1690`). Only the OU walk itself is sample-accurate
(`advanceDriftLanes`, `:1999-2011`, accumulates through `samplesUntilControl`).
`SpectralMorphEngine::updateChunk` (`:405-412`) and `EntropyProcessor::processChunk` (`:269-280`) are the
same shape — one travel advance, one pipeline run, one entropy application per call.

**Consequence.** If `SeraphisVoice` slices on its own absolute grid and passes the sub-slices down —
which is the literal reading of FR-007 and FR-010 — a caller block boundary that splits a chunk 36 + 28
gives the cloud and the morph **two** control steps where an unsplit 64 gives one. The detune/mutation
weights, the entropy jitter and `driftReadCount_` all differ. At the FR-019 shipped defaults
(`setMutation` = 0.25, `setEntropy` = 0.20) that difference is orders of magnitude above SC-014's
**1e-5 max per-sample** bound. **SC-014 would fail by construction.**

**Design.** `SeraphisVoice::processStereoBlock` **never renders a partial chunk.** It renders whole
`kControlChunkSamples`-sized chunks into a 64-sample stereo carry FIFO and serves the caller out of it:

```cpp
void processStereoBlock(float* outL, float* outR, std::size_t n) noexcept {
    if (outL == nullptr || outR == nullptr) return;      // FR-006, continuous_body.h:1166-1169
    if (n == 0) return;                                  // FR-006, no control step consumed
    if (!prepared_) { std::fill_n(outL, n, 0.0f); std::fill_n(outR, n, 0.0f); return; }
    std::size_t done = 0;
    while (done < n) {
        if (carryAvail_ == 0) { renderOneChunk(); }      // always exactly kControlChunkSamples
        const std::size_t take = std::min(n - done, carryAvail_);
        std::copy_n(carryL_.data() + carryRead_, take, outL + done);
        std::copy_n(carryR_.data() + carryRead_, take, outR + done);
        // D3's anti-click tail is armed from the last sample the caller ACTUALLY RECEIVED,
        // captured here at serve time — NOT from carryL_[63] at render time. See D3.
        lastOutL_ = outL[done + take - 1];
        lastOutR_ = outR[done + take - 1];
        carryRead_ += take; carryAvail_ -= take; done += take;
    }
}
```

Properties, all of which the criteria depend on:

- **Zero added latency.** A chunk is rendered on demand at the moment its first sample is requested;
  nothing is rendered ahead of the caller's position beyond the chunk the caller is currently inside.
  FR-015's "voice latency is 0 samples" holds unchanged.
- **Exact partition invariance.** Every sub-component sees an identical call sequence
  (`…, 64, 64, 64, …`) for any caller partition — including `n = 1` and `n = 4096`. SC-014's 1e-5 bound
  becomes trivially satisfiable rather than marginal.
- **FR-007 is delivered, not weakened.** "A control chunk split 36 + 28 yields exactly the same control
  step as an unsplit 64" is true of `SeraphisVoice`'s observable behaviour; the FIFO is *how*.
- **Edge Case 4 dissolves.** A block larger than `maxBlockSamples` never reaches a sub-component as a
  large block, so the atmosphere's `maxBlockSamples`-sized blur FIFO (`atmosphere_engine.h:373`) can never
  be overrun. The explicit "split into `maxBlockSamples` slices" step Edge Case 4 describes is subsumed.
- **Scratch shrinks from 2048 to 64 samples per buffer.** See §10 Deviation V-1.

`SeraphisEngine::processStereoBlock` runs its **own** absolute control grid (the `aether_reverb.h:2181-2195`
idiom) for the things it owns — sum-gain read, freeze-pending retry, bloom collection, voice-retirement
check — and passes the resulting slices to the voices, which absorb any partition via their FIFOs.

### D2 — The level detector's release coefficient must NOT use `calculateOnePolCoefficient`

FR-032 derives retirement latency as `kLevelReleaseMs · ln(1/kTailSilenceThreshold) = 0.1 s × 11.51 ≈ 1.15 s`.
That derivation treats `kLevelReleaseMs` as the **exponential time constant τ**.
`primitives/smoother.h:77-93`'s `calculateOnePolCoefficient(smoothTimeMs, sampleRate)` returns
`exp(-5000/(smoothTimeMs · sampleRate))` — i.e. it treats its argument as **time-to-99 %, which is 5τ**
(`smoother.h:86-88`). Using it would give τ = 20 ms and a 0.23 s retirement, contradicting FR-032's
stated figure and SC-012's derived 45 s window.

**Pin the coefficient explicitly**, per control chunk, in `prepare`:

```cpp
levelReleaseCoeff_ = std::exp(-static_cast<float>(kControlChunkSamples)
                              / (0.001f * kLevelReleaseMs * static_cast<float>(sampleRate_)));
```

### D3 — `silence()` cannot render; the ramp is a carried decaying tail

FR-034 requires a `kSilenceRampMs = 1.0f` fade "over a fixed short ramp" and FR-047 requires
`silence() → reset() → noteOn()` **within one block** (the plan realises FR-047's middle step as
`resetForSteal()` — see the two-entry-point table below). But a steal is triggered from
`SeraphisEngine::noteOn`, which is called *between* `processStereoBlock` calls: at that instant the voice
has already rendered up to the block boundary, and the next sample it will produce belongs to the new
note. There are no samples for a fade to occupy. `silence()` therefore cannot fade anything by rendering.

**What the discontinuity actually is:** the stolen voice's last emitted sample pair `(lastOutL_, lastOutR_)`
followed immediately by the new note's first sample, which is ~0 (the FR-020 envelope starts at 0). That
is a step of magnitude `|lastOut|`.

**`lastOutL_`/`lastOutR_` are captured at SERVE time, not at render time.** They are assigned inside
`processStereoBlock`'s copy loop (§1 D1 above) from `outL[done + take - 1]` / `outR[done + take - 1]`, and
**not** from `carryL_[n-1]` at the end of `renderOneChunk`. The distinction is load-bearing: §5 rule 1
sub-divides caller blocks at event boundaries (and Phase 8's host loop does the same with the host's
`sampleOffset`), so a steal routinely arrives with `carryAvail_ > 0` — mid-chunk. The render-time value is
then up to 63 samples of program material away from the amplitude the output actually reached, and a decay
ramp started from the wrong amplitude makes the step **larger** than doing nothing, with SC-003's
clicklessness statistic paying for it. `advanceOneChunkLifeOnly` (§2.8) sets
`lastOutL_ = lastOutR_ = 0.0f`, because a non-rendering voice emits silence and its "last emitted sample"
is 0.

**Design.** `silence()` captures that pair, arms a linear decay that is **added** to the first
`rampSamples = max(1, round(0.001 · kSilenceRampMs · sr))` samples the voice renders afterwards, **and
then hard-clears every sub-component** — which is FR-034 read literally ("fade the voice out over a fixed
short ramp *and then hard-clear every sub-component*"):

```cpp
void silence() noexcept {
    fadeTailL_ = lastOutL_; fadeTailR_ = lastOutR_;
    fadeRemaining_ = silenceRampSamples_;          // 48 @ 48 kHz, 44 @ 44.1 kHz — < 64 by FR-034
    // FR-034's hard-clear. Without these five calls a standalone voice.silence()
    // would leave the generators running at full level and only ADD a decaying
    // tail — the opposite of silence.
    cloud_.reset();                                // harmonic_cloud.h:313
    morph_.reset();                                // spectral_morph_engine.h:249
    body_.reset();                                 // continuous_body.h:766
    mse_.reset();                                  // multi_stage_envelope.h:79
    growth_.reset();                               // growth_envelope.h:129
    atmos_.reset();                                // atmosphere_engine.h:522 — a HARD clear.
                                                   // NOT atmos_.silence(): see the box below.
    carryAvail_ = 0; carryRead_ = 0;               // discard any un-served rendered audio
}
```

and inside `renderOneChunk`, after the spatial stage, **guarded** — `renderOneChunk` always produces
exactly `kControlChunkSamples = 64` samples while `silenceRampSamples_` is 48 @ 48 kHz / 44 @ 44.1 kHz
(§2.3 step 8), so an unguarded post-decrement would run negative for samples 48..63 and add an inverted,
magnitude-*growing* tail — the exact opposite of the anti-click behaviour — and would then keep running on
every subsequent chunk forever:

```cpp
if (fadeRemaining_ > 0) {
    const float w = static_cast<float>(fadeRemaining_) / static_cast<float>(silenceRampSamples_);
    outL[s] += fadeTailL_ * w;
    outR[s] += fadeTailR_ * w;
    --fadeRemaining_;
}
```

**Why `atmos_.reset()` and not `atmos_.silence()`.** `AtmosphereEngine::silence()` is **not** a hard clear
and does **not** latch on the call: it only sets `runState_ = RunState::Silencing`
(`atmosphere_engine.h:644-650`), after which the engine keeps rendering its grain bed and freeze drone,
multiplied per output sample by a linearly decaying `silenceGain_ -= silenceStep_` (`:2237-2242`), and
latches only when that gain reaches 0. The ramp is `kSilenceRampMs = 10.0f` (`:278`, step derived at
`:514`) — **480 samples @ 48 kHz, ten times `SeraphisVoice::kSilenceRampMs = 1.0f`** (48 samples). Its
grains read the capture ring, not the just-cleared body input, so over that window it emits the previously
audible texture scaled by the ramp, at the shipped `atmos_.setLevel(0.5f)` (§2.3 step 6). With
`atmos_.silence()` in this block, §6.1's FR-034 assertion — samples `[silenceRampSamples_, 512)` all
≤ `kTailSilenceThreshold` after a standalone `voice.silence()` — is **unsatisfiable by construction** on a
sounding voice. `reset()` is the class's only immediate clear and is already the documented single re-entry
out of the latch (R10, `atmosphere_engine.h:641-643`), so using it here is FR-034's "hard-clear every
sub-component" read literally, keeps the criterion at 1 ms, and makes the engine's `silence(); reset*()`
pair non-redundant instead of relying on the following `reset()` to do the clearing that `silence()`
claimed to have done. Cost is priced in R13 (the 2 MiB capture-ring `std::fill`), which every steal
already pays through `resetForSteal()`.

`rampSamples < kControlChunkSamples` at every supported rate is exactly why FR-034 pins 1 ms, and it is
what makes the whole steal complete inside one chunk. `orbit_` and `ms_` are **not** cleared: they are
life state, they produce no signal of their own, and FR-051/SC-016 require the orbit to keep advancing
on every slot at all times.

**Two reset entry points, because the two callers want opposite things about the armed tail.**

| Entry point | Armed fade tail | Caller |
|---|---|---|
| `void reset() noexcept` (public, FR-005) | **cleared** — `fadeTailL_ = fadeTailR_ = 0.0f; fadeRemaining_ = 0;` → exact post-`prepare` state, no exception to FR-005 | `SeraphisEngine::reset()`, `SeraphisEngine::silence()` (FR-055), the FR-072 non-finite recovery, and any direct caller |
| `void resetForSteal() noexcept` (public, FR-047) | **preserved** — otherwise identical to `reset()` | **only** `SeraphisEngine`'s steal / orphan-slot teardown in §3.6 |

This replaces the earlier design in which `reset()` unconditionally preserved the tail. That version was
self-contradictory: FR-055's `SeraphisEngine::silence()` is per-voice `silence()` then `reset()` with no
following `noteOn()`, so every voice would end holding a live armed tail; §6.1's FR-055 row ("the next
block is exactly 0 for every sample") would fail by construction, and the stale pre-silence sample value
would be summed into whatever note happened to arrive next — an arbitrarily-later click and a determinism
hazard for SC-005. With the split, `SeraphisEngine::silence()` yields exact zeros and the anti-click
carry survives exactly where FR-047 needs it. Recorded in §10 V-3.

### D4 — Bloom partials cannot be collected inside `noteOn`

`SeraphisVoice::noteOn` calls `cloud_.setFundamentalHz(f)`, which only raises the dirty flag
(`harmonic_cloud.h:399-401`, `markFreqDirty()`); `frequencyHz_[]` is recomputed at the head of the next
`updateControl` (`:1657-1661`). Reading `getPartialFrequencyHz(i)` inside `noteOn` therefore returns the
**previous note's** partials.

**Design.** `SeraphisEngine` sets a per-voice `bloomOnPending_` bit at note-on and performs the
`collectHeldPartials` snapshot at the **first absolute control-chunk boundary that falls after that
voice has completed at least one `renderOneChunk`** — recorded into `lastBloomPartials_[v]` /
`lastBloomCount_[v]`, with the voice's bit raised in the mask returned by `consumeBloomEvents()`.
SC-017 asserts against the snapshot, which is exactly the array handed to `AetherReverb::bloomNoteOn`.

**Three things make that invariant true rather than merely stated.** An earlier draft said only "at the
first boundary at or after the note-on", ran the snapshot in the *pre*-render half of `runControlStep`,
and left the idle-slot carry in place. All three are wrong, and each one on its own reproduces exactly the
staleness D4 exists to prevent:

1. **The snapshot runs in the POST-render half of the control step.** §3.4 splits `runControlStep` into
   `runPreRenderControlStep()` (sum gain, freeze retry, non-finite recovery) and
   `runPostRenderControlStep()` (bloom collection, deferred retirement), and runs the second **after** the
   slice that completes a chunk. With the whole step in front of the render, the snapshot at the boundary
   immediately following a note-on executes before any voice has called `updateControl` for that note,
   so it always reads the previous note's partials — one control chunk early, every time.
2. **The pending bit must survive one completed `renderOneChunk` on that voice.** `SeraphisVoice` carries
   `renderedSinceNoteOn_` (§2.2), cleared in `noteOn` and set at the top of `renderOneChunk`; §3.4 step 4
   snapshots only voices for which `bloomOnPending_` is set **and** `voices_[v].hasRenderedSinceNoteOn()`
   is true. This is what proves `updateControl` has consumed `freqDirty_` — `getPartialFrequencyHz`
   returns `frequencyHz_[i]` verbatim (`harmonic_cloud.h:955-957`), `setFundamentalHz` only calls
   `markFreqDirty()` (`:402`), and `frequencyHz_[]` is recomputed only at the head of `updateControl`
   (`:1656-1661`, `if (freqDirty_) { recalculateFrequencies(true); … }`).
3. **A note-on onto a non-rendering slot discards the life-only carry** (§2.9). While idle, §2.8 refills
   the carry FIFO with zero-filled chunks; at engine phase p the slot holds `64 - p` stale zero samples.
   Without the discard, `processStereoBlock` serves those straight out of the FIFO and never calls
   `renderOneChunk` for the rest of that chunk — so no `updateControl`, and the note's audible onset is
   delayed by up to 63 samples against FR-015's "0 samples". The discard is conditional on
   `carryIsLifeOnly_` (§2.2): a **live** retrigger's carry is real program material that must still be
   served, and dropping it would skip up to 63 rendered samples and create the click SC-004 measures.
   Where the carry is preserved, rule 2 still holds the snapshot back to the boundary after the next
   completed chunk — which is why rule 2 is not redundant with rule 3.

### D5 — `AetherReverb` is driven `bloomNoteOn`-late by exactly one control chunk

Direct consequence of D4 and stated so the FR-070 helper and Phase 8's processor reproduce it: the caller
polls `consumeBloomEvents()` **after** `processStereoBlock` returns, then issues `bloomNoteOn` /
`bloomNoteOff` before the *next* block. A ≤ 64-sample delay on a resonant-emphasis stage is inaudible and
is the only ordering under which the partial frequencies are correct.

### D6 — `RollingCaptureBuffer` power-of-two rounding is why `getCaptureCapacitySamples()` is asserted, not `captureSeconds`

FR-014 ships `captureSeconds = 4.0f`. `AtmosphereEngine` rounds capacity up to a power of two, so at
48 kHz 4 s → 262 144 samples (5.46 s) and the *seconds* figure is rate-dependent (RA-8, Edge Case 16).
The prepare-time assertion in tests reads
`AtmosphereEngine::getCaptureCapacitySamples()` (`atmosphere_engine.h:1080`), never the requested seconds.

---

## §2 `SeraphisVoice` — `systems/seraphis_voice.h`

### 2.1 Public API sketch

```cpp
namespace Krate { namespace DSP {

/// @par Layer: 3 (systems/). Dependencies: Layers 0-2 + Layer 3 peers. NO Layer 4.
/// @par Real-Time Safety: everything except prepare() is noexcept, allocation-free, lock-free.
struct SeraphisVoiceConfig {                                    // FR-004
    float       captureSeconds   = 4.0f;   ///< FR-014; clamped to AtmosphereEngine's [1, 30]
    bool        blurEnabled      = true;
    bool        freezeEnabled    = true;
    std::size_t blurFftSize      = 1024;
    std::size_t freezeFftSize    = 2048;
    std::size_t maxBlockSamples  = 2048;   ///< clamped to [1, kMaxBlockSamples]
};

class SeraphisVoice {
public:
    enum class EnvelopeMode : std::uint8_t { Standard = 0, Growth = 1 };   // FR-021, class-scoped

    // --- constants (all class-scoped, §0.2) ---------------------------------
    static constexpr std::size_t kControlChunkSamples = 64;    // FR-007; == harmonic_cloud.h:144,
                                                              // continuous_body.h:97, atmosphere_engine.h:269
    static constexpr std::size_t kMaxBlockSamples     = 2048;  // FR-004; == atmosphere_engine.h:373 default
    static constexpr float kTailSilenceThreshold      = 1.0e-5f;   // FR-032, -100 dBFS
    static constexpr float kLevelReleaseMs            = 100.0f;    // FR-033
    static constexpr float kSilenceRampMs             = 1.0f;      // FR-034
    static constexpr int   kQuiescentChunksToRetire   = 4;         // FR-032
    static constexpr float kMinVoiceWidthPct          = 50.0f;     // FR-025
    static constexpr float kMaxVoiceWidthPct          = 150.0f;    // FR-025
    static constexpr float kSpatialSmoothMs           = 20.0f;     // FR-025 ("smoothed")
    static constexpr float kSqrt2                     = 1.41421356f; // FR-025 unity-at-centre
    static constexpr EnvCurve kStageCurve = EnvCurve::Exponential;  // FR-020; the ONE curve every
                                                              // setStage call in §2.3/§2.10 uses.
                                                              // EnvCurve is core/env_curve.h:24 (Layer 0)
                                                              // — NOT primitives/envelope_utils.h, which
                                                              // declares RetriggerMode (:64) and no
                                                              // curve enum. Both are included by name.

    // --- seed salts (FR-016; pairwise distinct, non-overlapping ranges) ------
    static constexpr std::size_t kCloudSalt  = 0x0100;
    static constexpr std::size_t kMorphSalt  = 0x0200;
    static constexpr std::size_t kBodySalt   = 0x0300;
    static constexpr std::size_t kAtmosSalt  = 0x0400;
    static constexpr std::size_t kOrbitSalt  = 0x0500;
    static_assert(kCloudSalt != kMorphSalt && kMorphSalt != kBodySalt
                  && kBodySalt != kAtmosSalt && kAtmosSalt != kOrbitSalt, "FR-016");

    SeraphisVoice() noexcept = default;
    // NON-COPYABLE AND NON-MOVABLE, stated rather than silently produced.
    // `ContinuousBody` user-declares a deleted copy constructor and NO move members
    // (continuous_body.h:647-648), which suppresses its implicit move; overload resolution
    // then selects the deleted copy ctor, so `std::is_move_constructible_v<ContinuousBody>`
    // is false (verified) while HarmonicCloud (harmonic_cloud.h:272-273), SpectralMorphEngine
    // and AtmosphereEngine (atmosphere_engine.h:388-389) all DO declare defaulted noexcept
    // moves. `= default`ed move members here would therefore be DEFINED AS DELETED while
    // reading as if the type were movable. Adding move members to ContinuousBody is forbidden
    // by N-9. Nothing in Phase 7 moves a voice — `std::array<SeraphisVoice, kMaxVoices>`
    // does not require it.
    SeraphisVoice(const SeraphisVoice&) = delete;
    SeraphisVoice& operator=(const SeraphisVoice&) = delete;
    SeraphisVoice(SeraphisVoice&&) = delete;
    SeraphisVoice& operator=(SeraphisVoice&&) = delete;

    // --- lifecycle -----------------------------------------------------------
    void prepare(double sampleRate, const SeraphisVoiceConfig& cfg) noexcept;   // FR-003; only allocating path
    void reset() noexcept;                                                      // FR-005; CLEARS the D3 fade tail
    void resetForSteal() noexcept;                                              // FR-047; PRESERVES it (D3)
    void silence() noexcept;                                                    // FR-034; ramp + hard-clear
    void processStereoBlock(float* outL, float* outR, std::size_t n) noexcept;  // FR-006
    void advanceLifeOnly(std::size_t n) noexcept;                               // FR-027

    // --- notes ---------------------------------------------------------------
    void noteOn(float frequencyHz, float velocity) noexcept;                    // FR-023
    void noteOff() noexcept;                                                    // FR-024

    // --- seeding -------------------------------------------------------------
    void setSeed(std::uint32_t seed) noexcept;                                  // FR-016
    [[nodiscard]] std::uint32_t getSeed() const noexcept;

    // --- envelope ------------------------------------------------------------
    void setEnvelopeMode(EnvelopeMode) noexcept;                                // FR-021
    void setGrowthDurationSeconds(float) noexcept;                              // FR-022
    void setEnvelopeStageTimeMs(int stage, float ms) noexcept;                  // FR-030
    void setEnvelopeReleaseMs(float ms) noexcept;                               // FR-030
    [[nodiscard]] float getEnvelopeStageTimeMs(int stage) const noexcept;
    [[nodiscard]] float getEnvelopeReleaseMs() const noexcept;
    [[nodiscard]] EnvelopeMode getEnvelopeMode() const noexcept;
    [[nodiscard]] float getEnvelopeOutput() const noexcept;                     // composite gain, FR-085

    // --- spatial -------------------------------------------------------------
    void setSpatialDepth(float) noexcept;      // -> OrbitModulator::setDepth   (orbit_modulator.h:185)
    void setSpatialRate(float) noexcept;       // -> setRate                    (:167)
    void setSpatialCoupling(float) noexcept;   // -> setCoupling                (:173)
    void setSpatialGrowth(float) noexcept;     // -> setGrowth                  (:179)
    /// FR-062's `VoiceWidth` macro target. Sets the CENTRE the orbit's y modulates around;
    /// the per-chunk width is `clamp(widthBase_ + y * widthSpan_, 50, 150)` (§2.6). Without
    /// this the macro row would be overwritten within <= 64 samples by §2.6's recompute — see
    /// §4.1's `VoiceWidth` note. Base clamped to [kMinVoiceWidthPct, kMaxVoiceWidthPct];
    /// defaults to 100.0f, which is the FR-019 shipped value and keeps FR-025's
    /// "y = 0 -> exactly 100 %" exact at the FR-060 macro neutral.
    void setVoiceWidthBasePercent(float pct) noexcept;
    [[nodiscard]] float getVoiceWidthBasePercent() const noexcept;
    [[nodiscard]] float getSpatialAzimuth() const noexcept;        // orbit x, [-1,+1]
    [[nodiscard]] float getSpatialWidthPercent() const noexcept;   // [50,150]

    // --- engine parameter surface (FR-030; one-to-one, no clamping added) -----
    //  cloud:  setRichness setInharmonicity setSpectralTiltDb setMutation setSpectralGravity
    //          setDriftDepthCents setStereoSpread setAttackTimeSec setDecayTimeSec
    //  morph:  setEntropy setBloom setTargetPosition setTravelRate
    //  body:   setMaterial setResonance setDamping setKeyTracking setDrive setMix setCloudMix
    //          setCloudDecaySec setCloudSize setCloudDamping setWidth
    //  atmos:  setLevel setBlur setDensity setGrainSeconds setDriftDepth setPanSpread
    //          setDecorrelation setFreezeMix
    //
    // `setTravelMode` is called out separately because its parameter is a CLASS-SCOPED nested enum
    // (`spectral_morph_engine.h:139`, `enum class TravelMode : std::uint8_t { External = 0, Spline };`
    // declared inside `class SpectralMorphEngine`). Unqualified `TravelMode` names nothing from
    // SeraphisVoice's scope, so both the forwarder and every use site spell it out in full:
    void setTravelMode(SpectralMorphEngine::TravelMode) noexcept;               // FR-030
    [[nodiscard]] SpectralMorphEngine::TravelMode getTravelMode() const noexcept;

    // --- configure-time-gated forwarders (FR-031) ---------------------------
    void setSpectralState(int slot, const SpectralState& s) noexcept;  // rejected while !isFinished()
    void setSpectralStateCount(int n) noexcept;                        // rejected while !isFinished()
    [[nodiscard]] std::uint32_t getRejectedConfigureTimeCallCount() const noexcept;

    // --- freeze (FR-030a) ----------------------------------------------------
    void captureFreeze() noexcept;
    void releaseFreeze() noexcept;
    [[nodiscard]] bool isFreezeCaptured() const noexcept;

    // --- introspection (FR-085) ---------------------------------------------
    [[nodiscard]] float getCurrentLevel() const noexcept;   // FR-033
    [[nodiscard]] bool  isFinished()      const noexcept;   // FR-032
    /// True once at least one renderOneChunk has completed since the last noteOn().
    /// D4 rule 2 — the engine gates its bloom snapshot on this, because it is the only
    /// observable proof that cloud_.updateControl has consumed freqDirty_. §10 V-11.
    [[nodiscard]] bool  hasRenderedSinceNoteOn() const noexcept;
    [[nodiscard]] bool  stateFinite()     const noexcept;   // FR-035
    [[nodiscard]] const HarmonicCloud&        cloud() const noexcept;
    [[nodiscard]] const SpectralMorphEngine&  morph() const noexcept;
    [[nodiscard]] const ContinuousBody&       body()  const noexcept;
    [[nodiscard]] const AtmosphereEngine&     atmos() const noexcept;
    /// FIFTH sub-component accessor, added by this plan beyond FR-085's enumerated list.
    /// SC-010 clause 1 requires reading back `OrbitModulator::getDepth`/`getRate`/`getCoupling`/
    /// `getGrowth` (`orbit_modulator.h:189-192`) "reached through SeraphisVoice's forwarders", and
    /// the four FR-019 spatial rows — including the zero-travel fix `setSpatialDepth = 0.35` — have
    /// no other reachable read-back. Without it those rows fall back to an inert-differential that
    /// cannot detect them (see §6.2 SC-010 clause 4). Recorded in §10 V-8.
    [[nodiscard]] const OrbitModulator&       orbit() const noexcept;
};
}}  // namespace Krate::DSP
```

### 2.2 State layout

```cpp
// --- owned sub-components (FR-002; exactly one each, by value) ---------------
HarmonicCloud        cloud_;
SpectralMorphEngine  morph_;
ContinuousBody       body_;
AtmosphereEngine     atmos_;
MultiStageEnvelope   mse_;
GrowthEnvelope       growth_;
OrbitModulator       orbit_;
MidSideProcessor     ms_;

// --- fixed-size scratch, one control chunk each (D1, deviation V-1) ---------
std::array<float, kControlChunkSamples> cloudL_{}, cloudR_{};
std::array<float, kControlChunkSamples> bodyL_{},  bodyR_{};
std::array<float, kControlChunkSamples> atmosL_{}, atmosR_{};
std::array<float, kControlChunkSamples> carryL_{}, carryR_{};
std::size_t carryAvail_ = 0, carryRead_ = 0;   // ALSO the advanceLifeOnly clock — see §2.8
bool        carryIsLifeOnly_ = true;           // D4 rule 3: the carry currently held was produced by
                                               // advanceOneChunkLifeOnly (zeros), not renderOneChunk.
                                               // noteOn() discards the carry iff this is true.

// --- spatial -----------------------------------------------------------------
OnePoleSmoother gainLSm_, gainRSm_;      // azimuth balance, kSpatialSmoothMs
float           widthPct_ = 100.0f;      // last value pushed into ms_.setWidth
float           widthBase_ = 100.0f;     // FR-062 VoiceWidth macro target; §2.6's centre
float           widthSpan_ = 50.0f;      // FR-025's +-50 % orbit excursion; NOT macro-writable

// --- level detector (FR-033) --------------------------------------------------
float level_ = 0.0f;
float levelReleaseCoeff_ = 0.0f;         // D2
int   quiescentChunks_ = kQuiescentChunksToRetire;   // FR-032, 4 consecutive. SEEDED AT THE RETIRE
                                         // VALUE, not 0 — see §2.7. reset()/resetForSteal() restore
                                         // it to this value, so a never-rendered slot is isFinished()
                                         // from its first block and §3.4 skips it (FR-041, FR-051).

// --- envelope ----------------------------------------------------------------
EnvelopeMode envMode_ = EnvelopeMode::Standard;
float velocity_ = 1.0f;
std::array<float, MultiStageEnvelope::kMaxStages> stageTimeMs_{};   // FR-030 shadow (Growth mode)
std::array<float, MultiStageEnvelope::kMaxStages> stageLevel_{};
float releaseMs_ = 8000.0f;

// --- silence carry (D3) -------------------------------------------------------
float fadeTailL_ = 0.0f, fadeTailR_ = 0.0f, lastOutL_ = 0.0f, lastOutR_ = 0.0f;
int   fadeRemaining_ = 0, silenceRampSamples_ = 48;

// --- bookkeeping --------------------------------------------------------------
double sampleRate_ = 48000.0;
bool   prepared_ = false;
bool   hasSounded_ = false;              // §2.11 configure-time gate: set in noteOn(), cleared in
                                         // reset()/resetForSteal(). A never-noted voice is
                                         // CONFIGURABLE even though isFinished() is false.
bool   renderedSinceNoteOn_ = false;     // D4 rule 2: set at the top of renderOneChunk, cleared in
                                         // noteOn()/reset()/resetForSteal().
std::uint32_t seed_ = 1u, rejectedConfigCalls_ = 0u;
```

No `std::vector`, no `std::function`, no smart pointer, no `std::string` anywhere — SC-008's grep is a
flat zero. Total scratch: 8 × 64 × 4 B = **2 KB/voice**.

### 2.3 `prepare` contract (FR-003, FR-019, FR-019a)

Order, exactly:

1. `sampleRate_ = (sampleRate > 1.0) ? sampleRate : 1.0;` — the `atmosphere_engine.h:406-408` idiom.
2. Clamp `cfg` fields; `maxBlockSamples` → `[1, kMaxBlockSamples]`. Never reject (FR-004).
3. `cloud_.prepare(sampleRate_)` (`harmonic_cloud.h:282`); `morph_.prepare(sampleRate_)` (`:231`);
   `body_.prepare(sampleRate_)` (`:660`);
   `atmos_.prepare(sampleRate_, AtmosphereEngine::PrepareConfig{ .captureSeconds = cfg.captureSeconds,
   .blurEnabled = …, .freezeEnabled = …, .blurFftSize = …, .freezeFftSize = …,
   .maxBlockSamples = cfg.maxBlockSamples })` (`:405`, config at `:367-374`) — **designated initialisers
   only**, no narrowing in brace init (cross-cutting constraint).
   `mse_.prepare(static_cast<float>(sampleRate_))` (`:73` — takes `float`, not `double`);
   `growth_.prepare(sampleRate_)` (`:117`); `orbit_.prepare(sampleRate_)` (`:137`);
   `ms_.prepare(static_cast<float>(sampleRate_), kControlChunkSamples)` (`:96`).
4. Seeds: `applySeeds()` (§2.4) — **before** the first note, because `ContinuousBody::setSeed` is
   *"[c]onfigure-time only, and deliberately NOT retro-deterministic"* (`continuous_body.h:1117-1124`).
5. **FR-019a spectral-state authoring** — the only place `setState`/`setStateCount` are ever called:
   `morph_.setState(0, makeFactoryState(SpectralStateId::SineStack))`,
   `morph_.setState(1, makeFactoryState(SpectralStateId::Glass))` (`spectral_state.h:373`, ids at `:313`),
   `morph_.setStateCount(2)`, `morph_.setTravelMode(SpectralMorphEngine::TravelMode::External)`
   (**fully qualified — the enum is nested at `spectral_morph_engine.h:139`**),
   `morph_.setTargetPosition(0.0f)`.
6. **FR-019 shipped voice defaults** — apply the complete table verbatim. The six values that differ from
   the component defaults, each with the header line proving the component default:
   `cloud_.setRichness(0.60f)` (component 1.0, `harmonic_cloud.h:2125`, clamp max at `:416`);
   `cloud_.setInharmonicity(0.030f)` (0.0, `:2126`); `cloud_.setMutation(0.25f)` (0.0, `:2128`);
   `cloud_.setSpectralGravity(0.20f)` (0.0, `:2129`); `cloud_.setStereoSpread(0.35f)` (0.0, `:2132`);
   `morph_.setEntropy(0.20f)` (0.0, `:224`); `body_.setDamping(0.25f)`
   (`kDefaultDamping = kMinDamping = 0.0f`, `continuous_body.h:126-128`);
   `atmos_.setLevel(0.5f)` (1.0, `atmosphere_engine.h:2365`);
   `orbit_.setDepth(0.35f)` (`kDefaultDepth = 1.0f` **which is `setDepth`'s clamp maximum**,
   `orbit_modulator.h:123`, `:184-186`).
   Every *(unchanged)* row is still written explicitly, so the table is the code.
   `setVoiceWidthBasePercent(100.0f)` is written here too (§4.1.0): it is FR-062's `VoiceWidth` macro
   `base`, and at that value §2.6 reduces to FR-025's `100 + y·50` exactly.
7. Envelope defaults (FR-020). **Every stage write in this step goes through the private helper
   `applyStage(int st, float level, float ms)`, never through `mse_.setStage` directly:**

   ```cpp
   void applyStage(int st, float level, float ms) noexcept {
       // FR-059 idempotence guard. MultiStageEnvelope::setStage has NO equality early-out of
       // its own, so without this an unchanged-knob apply() every block would keep rewriting
       // the stage and FR-059's "must not step any parameter" would rest on nothing. See the
       // §4.4 early-out audit — this is one of the two rows that needed a guard added.
       if (stageLevel_[st] == level && stageTimeMs_[st] == ms) return;
       stageLevel_[st]  = level;          // the shadows are the SINGLE SOURCE OF TRUTH
       stageTimeMs_[st] = ms;
       if (envMode_ == EnvelopeMode::Standard || st >= mse_.getSustainPoint())
           mse_.setStage(st, level, ms, kStageCurve);      // multi_stage_envelope.h:166
       else
           mse_.setStage(st, level, 0.0f, kStageCurve);    // Growth mode zeroes pre-sustain times
   }
   ```

   The guard is on the **shadows**, which §2.10 keeps in lockstep with `mse_` on every mode switch, so
   it can never suppress a write that `mse_` still needs. `prepare` runs `applyStage` from the
   zero-initialised shadow state, so the FR-020 values are never suppressed at prepare time.

   `applyStage(0, 1.0f, 2000.0f)`, `applyStage(1, 0.7f, 4000.0f)`, `applyStage(2, 0.7f, 0.0f)`,
   `applyStage(3, 0.0f, 0.0f)` after `mse_.setNumStages(4)` and `setSustainPoint(2)` (`:178`);
   `setReleaseTime(8000.0f)` (`:206`); **`setRetriggerMode(RetriggerMode::Legato)`** (`:215`, enum at
   `primitives/envelope_utils.h:64-67`) — explicit, because the component default is `Hard` (`:463`).
   `growth_.setDuration(10.0f)` (`growth_envelope.h:144`).

   **Why the helper and not a direct `setStage`.** §2.10's Standard branch restores FR-020's stage
   times from `stageTimeMs_`/`stageLevel_`, and §2.2 declares those arrays zero-initialised. If
   `prepare` wrote FR-020's values straight into `mse_`, the shadows would still hold `{0, 0}` and a
   Standard → Growth → Standard round trip would restore **{level 0, 0 ms}** for stages 0 and 1,
   destroying FR-020's 2 s attack and 4 s decay. `setEnvelopeStageTimeMs` (FR-030) is likewise
   re-expressed as `applyStage(stage, stageLevel_[stage], ms)` so there is exactly one write path.
   `SeraphisVoice_EnvelopeModesBehave` asserts the round trip (§6.1).
8. Derived constants: `levelReleaseCoeff_` (D2); `silenceRampSamples_ = max(1, round(0.001f·kSilenceRampMs·sr))`;
   `gainLSm_.configure(kSpatialSmoothMs, static_cast<float>(sampleRate_))`, `gainRSm_.configure(...)`
   (`smoother.h:160` — `void configure(float smoothTimeMs, float sampleRate)`; the cast is required,
   the parameter is `float` and MSVC raises C4244 without it under SC-019's zero-warning gate).
9. `prepared_ = true; reset();`

**What `reset()`/`resetForSteal()` restore, precisely.** Run state only: the carry FIFO
(`carryAvail_ = carryRead_ = 0`, `carryIsLifeOnly_ = true`), `level_ = 0`,
**`quiescentChunks_ = kQuiescentChunksToRetire`** (§2.7 — *not* 0), `hasSounded_ = false`,
`renderedSinceNoteOn_ = false`, `lastOutL_ = lastOutR_ = 0`, and each sub-component's own `reset()`.
**Parameters are not restored** — `widthBase_`, the FR-019 table values and everything a macro or a
Phase 9 forwarder wrote survive. That is the owned sub-components' own contract for `reset()`
(*"Clear all internal state; leave every parameter unchanged"*, `continuous_body.h:757`), so the voice
inherits it rather than inventing a different one, and it is what keeps
`SeraphisMacroMatrix::apply()` from being silently undone by a host transport stop. The one difference
between the two entry points remains the D3 fade tail: `reset()` clears it, `resetForSteal()` keeps it.

### 2.4 `applySeeds` (FR-016, FR-017)

```cpp
cloud_.setSeed(deriveStreamSeed(seed_, kCloudSalt));   // core/random.h:102-111
morph_.setSeed(deriveStreamSeed(seed_, kMorphSalt));
body_ .setSeed(deriveStreamSeed(seed_, kBodySalt));
atmos_.setSeed(deriveStreamSeed(seed_, kAtmosSalt));
orbit_.setSeed(deriveStreamSeed(seed_, kOrbitSalt));
```

`GrowthEnvelope::setSeed` is a documented no-op (`growth_envelope.h:140`) and is not called.
`deriveStreamSeed` substitutes `0x2545F491u` when the hash lands on 0 (`random.h:110`), which is why seed 0
is legal (Edge Case 19).

### 2.5 `renderOneChunk()` — the FR-010 chain, one control chunk

Exactly `kControlChunkSamples` samples, always. Steps map 1:1 onto FR-010:

```
n = kControlChunkSamples

0. renderedSinceNoteOn_ = true                                      // D4 rule 2, before any work:
                                                                    // step 2's cloud_ call is what
                                                                    // consumes freqDirty_

1. morph_.updateChunk(n)                                            // spectral_morph_engine.h:405
   cloud_.setSpectralTarget(morph_.getOutputRatios(),                // harmonic_cloud.h:769
                            morph_.getOutputAmplitudes(),
                            morph_.getOutputCount())                 // :423-425
   -- unconditional every chunk (FR-012); the whole-array skip at :776-786 makes an
      unchanged target cheap and the voice does not duplicate it.

2. cloud_.processStereoBlock(cloudL_.data(), cloudR_.data(), n)      // :878

3. excitation gate, in place on cloudL_/cloudR_ (FR-010 step 3):
      Standard: g[s] = velocity_ * mse_.process()                    // :223, per sample
      Growth:   growth_.processBlock(n) once at chunk head;          // growth_envelope.h:185
                gGrowth = growth_.getCurrentValue();                 // :197, held across chunk
                g[s] = velocity_ * gGrowth * mse_.process()
   envOutput_ = g[n-1]                                               // getEnvelopeOutput(), FR-085

4. body_.processStereoBlock(cloudL_, cloudR_, bodyL_, bodyR_, n)     // continuous_body.h:1161
   -- NOT in place: "IN-PLACE OPERATION IS NOT SUPPORTED" (:1155-1156)

5. atmos_.processStereoBlock(bodyL_, bodyR_, atmosL_, atmosR_, n)    // atmosphere_engine.h:665
   -- shape identical by design (:656-658); output is WET TEXTURE ONLY (:660-661)

6. bus[s] = bodyL_[s] + atmosL_[s]   (and R) -- PLAIN SUM, no second gain.
   The atmosphere's own trim is already applied inside the component
   (setLevel documented "Output gain trim", :944-949, multiplied at :2233).

7. spatial stage (FR-025, §2.6) -> carryL_/carryR_
8. + silence fade tail (D3)
9. level detector update (FR-033, §2.7); retirement counter (FR-032)
10. carryAvail_ = n; carryRead_ = 0; carryIsLifeOnly_ = false;
    -- lastOutL_/lastOutR_ are NOT assigned here. They are captured at SERVE time in
       processStereoBlock (§1 D1 / D3): the tail must be armed from the last sample the
       CALLER received, which on a mid-chunk steal is not carry[n-1].
```

`orbit_.processBlock(n)` is advanced at the head of step 7 (see §2.6), so it advances on **every** chunk
whether or not audio is produced.

### 2.6 Spatial stage (FR-025, RA-3) — exact math

Once per chunk:

```cpp
orbit_.processBlock(n);                                   // orbit_modulator.h:216
const float x = orbit_.getCurrentValue();                 // :236, clamped +-1
const float y = orbit_.getY();                            // :242, clamped +-1

// (a) unity-at-centre equal-power balance
const float panNorm = (x + 1.0f) * 0.5f;                  // [0,1]
float gL, gR;
equalPowerGains(panNorm, gL, gR);                         // core/crossfade_utils.h:50-53
gL *= kSqrt2;  gR *= kSqrt2;                              // FR-025's load-bearing sqrt(2)
gainLSm_.setTarget(gL);  gainRSm_.setTarget(gR);

// (b) width: the orbit modulates AROUND a settable centre, not around a literal 100
widthPct_ = std::clamp(widthBase_ + y * widthSpan_,       // widthBase_ defaults to 100.0f
                       kMinVoiceWidthPct, kMaxVoiceWidthPct);
ms_.setWidth(widthPct_);                                  // midside_processor.h:133
```

`widthBase_` is FR-062's `VoiceWidth` macro target (§4.1); `widthSpan_` is fixed at 50 and is **not**
macro-writable. At the FR-060 macro neutral `widthBase_ == 100.0f` and the expression reduces to
`100 + y·50` exactly, so FR-025's "y = 0 → exactly 100 %" and the [50, 150] range are unchanged from
the shipped defaults. The `std::clamp` is what keeps a macro-raised centre inside FR-025's range.
**Without the settable centre the `VoiceWidth` row is inert by construction:** §2.6 runs once per control
chunk, so anything `SeraphisMacroMatrix::apply()` wrote into `ms_` directly would be overwritten within
≤ 64 samples.

Per sample (in place on `bodyL_+atmosL_` sum, into `carry*`):

```cpp
tmpL[s] = bus L * gainLSm_.process();                     // smoother.h:197
tmpR[s] = bus R * gainRSm_.process();
```

then `ms_.process(tmpL, tmpR, carryL_.data(), carryR_.data(), n)` (`midside_processor.h:183`).

Verification of the constants: at `panNorm = 0.5`, `cos(π/4) = sin(π/4) = 0.70710678`, × `kSqrt2` = **1.0**
exactly on both channels (FR-026, Edge Case 10). At the endpoints the gain is 1.41421 (+3 dB), and
`gL² + gR² = 2` at every position, so the law is constant-power. `MidSideProcessor` at width = 100 %
computes `mid=(L+R)·0.5; side=(L−R)·0.5; out = mid ± side` (`:196-207`) — the algebraic identity, **not
bit-exact**, hence FR-026's 1e-6 measurable bound rather than a transparency claim. It is also verified
in-place-safe: it reads `L`/`R` into locals before writing (`:195-196`, comment *"Read input samples
(before potentially overwriting in-place)"*).

**`getGrowth()` is never read.** Its documented neutral is 0 (`orbit_modulator.h:177-180`), and growth is
already baked into the radius that `getY()` returns (`:25`, `:240-244`). SC-016 clause 2 is the regression
guard.

### 2.7 Level detector (FR-033) and retirement (FR-032)

Once per chunk, on the **post-spatial** buffer (i.e. exactly what the voice contributes to the sum, before
FR-052's sum gain):

```cpp
float chunkPeak = 0.0f;
for (s) chunkPeak = std::max({chunkPeak, std::abs(carryL_[s]), std::abs(carryR_[s])});
level_ = (chunkPeak > level_) ? chunkPeak                                   // instant attack
                              : chunkPeak + (level_ - chunkPeak) * levelReleaseCoeff_;
quiescentChunks_ = (level_ < kTailSilenceThreshold) ? (quiescentChunks_ + 1) : 0;
```

`advanceOneChunkLifeOnly()` (FR-027, §2.8) runs the same release with `chunkPeak = 0` on the same carry
clock, so a skipped voice's level decays to 0 instead of freezing.

```cpp
[[nodiscard]] bool isFinished() const noexcept {
    return !mse_.isActive()                       // multi_stage_envelope.h:251
        && cloud_.isQuiescent()                   // harmonic_cloud.h:1040
        && quiescentChunks_ >= kQuiescentChunksToRetire;
}
```

**`quiescentChunks_` is seeded to `kQuiescentChunksToRetire`, not 0 — in the member initialiser (§2.2)
and in both `reset()` and `resetForSteal()`.** It is only ever advanced inside
`renderOneChunk`/`advanceOneChunkLifeOnly`, so with a 0 seed `isFinished()` is false for every
never-rendered slot immediately after `prepare()` and after every `SeraphisEngine::reset()`/`silence()`.
Both branches of §3.4's `isRendering(v)` would then return true and **all 16 slots would take the full
cloud/morph/body/atmosphere path for the first 4 control chunks (5.33 ms)** — roughly double the SC-001
worst-case cost at every transport start, unbudgeted against R7's 1.4 points of real margin, and a direct
contradiction of FR-041 ("only voices below the current polyphony are rendered") and FR-051 ("idle voices
are skipped from the audio path"). Nothing in the criteria detects it: SC-016 passes because a never-noted
voice renders exact zeros, and SC-001's best-of-8 timing warms past the 5.33 ms window. The seed is the
minimal fix — the counter's meaning ("consecutive chunks below threshold") is trivially satisfied by a
voice that has produced nothing at all. It does not weaken SC-012's "never `Idle` while level >
threshold" reasoning: `noteOn` does not touch `quiescentChunks_`, the first rendered chunk above
threshold resets it to 0, and `mse_.isActive()` is true from the gate onward regardless.

The alternative — folding `hasSounded_` into `isFinished()` — was rejected: `hasSounded_` is §2.11's
configure-time gate, and overloading it would couple FR-031's configurability window to FR-032's
retirement predicate, so a future change to either would silently move the other.

### 2.8 `advanceLifeOnly` (FR-027)

Ticks `orbit_.processBlock(kControlChunkSamples)` on the voice's own chunk grid, releases the level
detector with a zero chunk peak, writes no samples and touches no cloud/body/atmosphere state. It follows
`HarmonicCloud`'s own quiescent early-out idiom, which still calls `advanceDriftLanes(...)` and bumps
`driftReadCount_` so *"a silent render and a sounding render of the same length leave identical lane
state"* (`harmonic_cloud.h:893-903`).

**It runs on the same carry clock as `processStereoBlock`, so no separate residual counter exists.**
`advanceLifeOnly(n)` consumes `n` samples out of `carryAvail_` exactly the way `processStereoBlock(n)`
does — it just discards them instead of copying them out — and refills the carry with a *life-only*
chunk step whenever it is exhausted:

```cpp
void advanceLifeOnly(std::size_t n) noexcept {
    if (n == 0 || !prepared_) return;
    std::size_t done = 0;
    while (done < n) {
        if (carryAvail_ == 0) advanceOneChunkLifeOnly();   // orbit tick + level release @ peak 0;
                                                           // carryL_/carryR_ zero-filled,
                                                           // carryAvail_ = kControlChunkSamples,
                                                           // carryIsLifeOnly_ = true (D4 rule 3),
                                                           // lastOutL_ = lastOutR_ = 0 (D3: a
                                                           // non-rendering voice emits silence)
        const std::size_t take = std::min(n - done, carryAvail_);
        carryRead_ += take; carryAvail_ -= take; done += take;
    }
}
```

The engine (§3.4) calls exactly one of `processStereoBlock(slice)` / `advanceLifeOnly(slice)` on **every**
slot for **every** slice, so the two paths stay in lockstep and the voice's chunk boundaries coincide with
the engine's absolute 64-sample grid. `advanceLifeOnly` with `slice` as small as 1 sample is therefore
correct with no extra state: the sub-64 remainder lives in `carryAvail_`/`carryRead_`, which already exist.
Zero-filling the carry is what makes a mid-chunk non-rendering → rendering transition serve silence for
the remainder of that chunk, which is the correct contribution from a voice that was not rendering.

**Invariant to test (SC-016 / FR-027 support):** advancing `n` samples through `advanceLifeOnly` and
through `processStereoBlock` leaves `getSpatialAzimuth()`/`getSpatialWidthPercent()` identical **for every
`n`**, not only multiples of 64, because both consume the same clock and `OrbitModulator::processBlock` is
accumulator-based (`orbit_modulator.h:216-229`). The test asserts it at `n ∈ {1, 7, 64, 65, 512}`.
(This supersedes the earlier chunk-boundary-only caveat — see §10 V-7.)

### 2.9 `noteOn` / `noteOff` (FR-023, FR-024)

```cpp
void noteOn(float freqHz, float velocity) noexcept {
    hasSounded_ = true;                      // §2.11 configure-time gate closes here
    renderedSinceNoteOn_ = false;            // D4 rule 2
    if (carryIsLifeOnly_) {                  // D4 rule 3: drop the zero-filled idle carry so the
        carryAvail_ = 0; carryRead_ = 0;     // onset is sample-accurate (FR-015) and the next
    }                                        // sample forces a renderOneChunk -> updateControl.
                                             // A LIVE retrigger's carry is real audio and is KEPT:
                                             // dropping it would skip up to 63 rendered samples and
                                             // create exactly the click SC-004 measures.
    velocity_ = std::clamp(velocity, 0.0f, 1.0f);
    body_.setNoteFrequencyHz(freqHz);        // continuous_body.h:982, clamps [20, 8000] (:118-119)
    cloud_.setFundamentalHz(freqHz);         // harmonic_cloud.h:383, clamps [20, 4000] (:184-185)
    cloud_.noteOn();                         // :635 -- redraws phases ONLY when quiescent (:636-655)
    mse_.gate(true);                         // :99
    if (envMode_ == EnvelopeMode::Growth) growth_.trigger();   // :161, no-op while Rising (:158-160)
}

void noteOff() noexcept {
    cloud_.noteOff();                        // :663
    mse_.gate(false);
    // body_, atmos_, orbit_ keep running -- they ARE the tail (RA-2).
    // growth_ is NOT reset (FR-024).
}
```

The two clamps differ deliberately (Edge Case 12): MIDI 0 (8.18 Hz) and 127 (12 543 Hz) land at different
places in the two engines. Documented in the header, not repaired.

### 2.10 Envelope mode switch (FR-021)

```cpp
void setEnvelopeMode(EnvelopeMode m) noexcept {
    if (m == envMode_) return;
    envMode_ = m;
    if (m == EnvelopeMode::Growth) {
        // Force EVERY stage from 0 up to and including sustainPoint-1 to 0 ms, preserving
        // level and curve. Zeroing stage 0 alone is NOT enough: advanceToNextStage() only
        // enters Sustaining when currentStage_ == sustainPoint_ (multi_stage_envelope.h:386-389,
        // reached from :307-312), so FR-020's 4 s stage-1 ramp would still shape the composite.
        for (int st = 0; st < mse_.getSustainPoint(); ++st)
            mse_.setStage(st, stageLevel_[st], 0.0f, kStageCurve);
    } else {
        for (int st = 0; st < mse_.getSustainPoint(); ++st)
            mse_.setStage(st, stageLevel_[st], stageTimeMs_[st], kStageCurve);
    }
}
```

The Standard branch restores from the shadows, which is why **`prepare` must populate them through
`applyStage`** (§2.3 step 7). `setEnvelopeStageTimeMs(stage, ms)` is `applyStage(stage,
stageLevel_[stage], ms)`: the shadow always takes the caller's value (so `getEnvelopeStageTimeMs` reads
back what was set — FR-030's "stored but not applied") while the forward to `mse_` is gated on
`envMode_ == Standard || stage >= mse_.getSustainPoint()`.

### 2.11 Configure-time gate (FR-031)

`setSpectralState` / `setSpectralStateCount` are gated on **`!hasSounded_ || isFinished()`**; if that is
false, `++rejectedConfigCalls_` and return without touching `morph_`. The gate exists because the header
carries a boxed contract: *"CONFIGURATION-TIME CALLS: prepare(), reset(), setSeed(), setState() and
setStateCount() are NOT to be called while the consumer is sounding"* (`spectral_morph_engine.h:198-207`),
and `setState` arms an absorption fade when the slot contributes to the output (`:288-291`).
`setTargetPosition` (`:348`) is **not** gated — FR-062's Bloom row needs it live.

**Why the predicate is not `isFinished()` alone.** `isFinished()` requires
`quiescentChunks_ >= kQuiescentChunksToRetire` (§2.7), `quiescentChunks_` starts at 0 (§2.2), it is only
incremented inside `renderOneChunk`/`advanceOneChunkLifeOnly`, and `prepare` ends with `reset()` (§2.3
step 9). So a freshly prepared, never-rendered `SeraphisVoice` — precisely the object Phase 9 configures
and the one SC-006(b) constructs — has `isFinished() == false` and an `isFinished()`-only gate would
**reject every configure-time call it will ever receive**, making FR-031's stated purpose ("the public
forwarders … exist for Phase 9") unreachable. `hasSounded_` is set in `noteOn` (§2.9) and cleared in
`reset()`/`resetForSteal()`, so the configurable window is exactly "prepared but not currently sounding",
which is what the `SpectralMorphEngine` contract asks for. `SeraphisVoice_ForwardersAndConfigureTimeGate`
asserts the **accept** path as well as the reject path (§6.1) — a gate that rejects unconditionally must
fail a named test.

### 2.12 `stateFinite` (FR-035) and the non-finite guard (FR-072)

```cpp
[[nodiscard]] bool stateFinite() const noexcept {
    return body_.stateFinite()            // continuous_body.h:1328
        && morph_.stateFinite()           // spectral_morph_engine.h:456
        && isFiniteBits(level_) && isFiniteBits(lastOutL_) && isFiniteBits(lastOutR_)
        && isFiniteBits(gainLSm_.getCurrentValue()) && isFiniteBits(gainRSm_.getCurrentValue());
}
```

`isFiniteBits` is a **private, plain (inlinable) `static bool`** duplicated in each of `SeraphisVoice`
and `SeraphisEngine`, copying `ContinuousBody`'s shape verbatim (`continuous_body.h:1346-1351`):

```cpp
[[nodiscard]] static bool isFiniteBits(float v) noexcept {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &v, sizeof(bits));
    return (bits & 0x7F800000u) != 0x7F800000u;
}
```

**Not** the `ITERUM_NOINLINE` wrapper at `atmosphere_engine.h:1214-1216`. That one wraps `detail::isNaN` /
`detail::isInf` (`core/db_utils.h:54`, `:175`), which are *floating-point comparisons* and therefore need
a call boundary the caller's `-ffast-math` context cannot see through — and its own header states the
resulting contract: *"The call boundary is not free (~2 ns), so the hot paths call this O(1) times per
64-sample control chunk (accumulate-then-test), **never once per sample**"* (`atmosphere_engine.h:1203-1206`).
§3.4 calls the check twice per sample per voice; at polyphony 16 / 48 kHz that is 1.54 M non-inlinable
calls/s (~0.3 % of a core by that header's own figure) and, worse, a call boundary in the innermost loop
blocks vectorisation of the voice sum — against R7's 4.6-point headroom.

The `memcpy`-bit form has no such problem: it is integer arithmetic on the bit pattern, immune to
`-ffast-math` *without* a call boundary, self-contained (no `detail::` dependency), and free to inline.
It is also the idiom FR-072 actually cites. Both the per-sample path (§3.4) and the control-rate paths
(`stateFinite()`, setters) use it. **`std::isnan` / `std::isinf` / `std::isfinite` appear nowhere**
(SC-008's grep, `tools/lint-nonfinite-symbols.js`). Duplicating the helper privately in both classes —
rather than introducing a shared namespace-scope name — is the same treatment `continuous_body.h` and
`atmosphere_engine.h` already give each other, and it keeps `SeraphisEngine` from reaching into a
`SeraphisVoice` private (which §3.4's earlier draft did, and which does not compile).

---

## §3 `SeraphisEngine` — `systems/seraphis_engine.h`

### 3.1 Public API sketch

```cpp
struct SeraphisEngineConfig {                          // forwarded verbatim to every voice
    SeraphisVoiceConfig voice{};
    std::size_t         polyphony = 8;                 // FR-040 shipped default (RQ-1)
    std::uint32_t       seed      = 1u;
};

class SeraphisEngine {
public:
    static constexpr std::size_t kMaxVoices           = 16;    // FR-040 (roadmap line 290 upper bound)
    static constexpr std::size_t kControlChunkSamples = 64;    // FR-007
    static constexpr std::size_t kMaxBlockSamples     = 2048;  // FR-004
    static constexpr std::size_t kVoiceSaltBase       = 0x9000; // FR-050; disjoint from every §2.1 salt
    static constexpr float kSumGainSmoothMs           = 20.0f;  // FR-052
    static constexpr float kAmnestyLevelThreshold     = 0.0316f;// FR-046, -30 dBFS
    static constexpr float kOutputSaturation          = 0.15f;  // FR-053
    static constexpr float kOutputDriveDb             = 0.0f;   // FR-053, not user-exposed

    struct BloomEvents { std::uint32_t noteOnMask = 0u; std::uint32_t noteOffMask = 0u; };

    void prepare(double sampleRate, const SeraphisEngineConfig& cfg) noexcept;
    void reset() noexcept;                                          // FR-055; per-voice reset()
    void silence() noexcept;                                        // FR-055; per-voice silence() then
                                                                    // reset() — the tail-CLEARING one
                                                                    // (D3), so the next block is 0
    void setPolyphony(std::size_t n) noexcept;                      // FR-040
    void setSeed(std::uint32_t seed) noexcept;                      // FR-050

    void noteOn(std::uint8_t note, std::uint8_t velocity) noexcept; // FR-042
    void noteOff(std::uint8_t note) noexcept;                       // FR-042

    void processStereoBlock(float* l, float* r, std::size_t n) noexcept;  // FR-051, voice sum ONLY
    void processOutputStage(float* l, float* r, std::size_t n) noexcept;  // FR-053a, in place

    void setAtmosphereFreeze(bool on) noexcept;                     // FR-030a
    [[nodiscard]] bool getAtmosphereFreeze() const noexcept;
    void setOutputSaturation(float amount) noexcept;                // FR-053

    void collectHeldPartials(std::size_t voiceIndex, float* dest, std::size_t capacity,
                             std::size_t& outCount) const noexcept; // FR-071
    [[nodiscard]] BloomEvents consumeBloomEvents() noexcept;        // FR-070 lifecycle hook (D4/D5)

    // FR-085 introspection
    [[nodiscard]] std::size_t   getPolyphony()             const noexcept;
    [[nodiscard]] std::size_t   getActiveVoiceCount()      const noexcept;
    [[nodiscard]] std::size_t   getRenderingVoiceCount()   const noexcept;
    [[nodiscard]] float         getVoiceLevel(std::size_t) const noexcept;
    [[nodiscard]] VoiceState    getVoiceState(std::size_t) const noexcept;
    [[nodiscard]] const SeraphisVoice& getVoice(std::size_t) const noexcept;
    [[nodiscard]] int           getLastStolenVoiceIndex()  const noexcept;
    [[nodiscard]] std::uint32_t getNonFiniteRecoveryCount()const noexcept;
    /// FR-045 step 4's allocation order, tracked engine-side (§3.6.1). Strictly increasing across
    /// note events; the tie-break key. Added beyond FR-085's enumerated list — §10 V-9.
    [[nodiscard]] std::uint64_t getVoiceAllocationSerial(std::size_t) const noexcept;
    [[nodiscard]] std::uint32_t getSeed()                  const noexcept;
    [[nodiscard]] std::span<const float> getLastBloomPartials(std::size_t) const noexcept;
    [[nodiscard]] std::size_t   getLastBloomCount(std::size_t) const noexcept;
};
```

### 3.2 State layout

```cpp
std::array<SeraphisVoice, kMaxVoices> voices_;
VoiceAllocator allocator_;

std::array<float, kControlChunkSamples> busL_{}, busR_{};      // per-slice accumulation — 64, NOT
std::array<float, kControlChunkSamples> vL_{},   vR_{};        // kMaxBlockSamples. §10 V-1.

TapeSaturator satL_, satR_;                                    // mono, in place (tape_saturator.h:335)
TruePeakLimiter limiter_;                                      // stereo, in place (:104)

OnePoleSmoother sumGain_;                                      // FR-052, target 1/sqrt(polyphony)
float           sumGainHeld_ = 1.0f;                           // read once per control chunk (§3.4)
std::uint64_t   sampleCounter_ = 0;                            // FR-007 absolute grid
std::size_t     polyphony_ = 8;
std::uint32_t   seed_ = 1u;
bool            prepared_ = false;
bool            freezeLatched_ = false;
std::uint32_t   freezePending_ = 0u;                           // FR-030a per-voice retry bitmask
std::uint32_t   nonFinitePending_ = 0u;                        // FR-072 deferred-reset bitmask (§3.4)
std::uint32_t   bloomOnPending_ = 0u, bloomOnMask_ = 0u, bloomOffMask_ = 0u;
std::uint32_t   orphanTail_ = 0u;                              // §3.6.2: slots the polyphony shrink
                                                               // force-idled while !isFinished().
                                                               // The FR-047 teardown predicate — see
                                                               // §3.6's NoteOn row.
int             retriggerSlot_ = -1;                           // §3.6: slot the CURRENT noteOn() is a
                                                               // same-note retrigger of, or -1.
std::array<std::array<float, kBloomPartialCap>, kMaxVoices> lastBloomPartials_{};
std::array<std::size_t, kMaxVoices> lastBloomCount_{};
std::array<std::uint64_t, kMaxVoices> voiceSerial_{};          // FR-045 step 4 tie-break key (§3.6.1)
std::uint64_t   nextSerial_ = 1u;
int             lastStolenVoice_ = -1;
std::uint32_t   nonFiniteRecoveries_ = 0u;
```

**There is no `renderingHigh_`.** An earlier draft bounded the render loop by
`v < polyphony_ || v < renderingHigh_`, which (a) left slots at or above `max(polyphony_,
renderingHigh_)` receiving neither `processStereoBlock` nor `advanceLifeOnly` — so at the shipped
polyphony of 8, slots 8–15 never advanced their `OrbitModulator` and SC-016 clauses 1 and 2 ("**every**
voice's `getSpatialAzimuth()`/`getSpatialWidthPercent()` has non-zero total variation") failed as
written; (b) never stated where `renderingHigh_` was assigned or that `renderingHigh_ <= kMaxVoices`,
making a bookkeeping slip an out-of-bounds access on `voices_` on the audio thread rather than a compile
error; and (c) contradicted §3.8, which deliberately iterates `v < kMaxVoices`. §3.4 now iterates
`v < kMaxVoices` unconditionally — matching FR-041's "prepare all 16" and roadmap Key Design Decision 1
— and the bound is a compile-time constant, so the loop cannot leave the array.

**`SeraphisEngine` is ~750 KB of automatic storage and must never be a test local.** `voices_` is
`std::array<SeraphisVoice, kMaxVoices>` **by value**, and the plan's only size guard is on one voice
(`kVoiceSizeBound`, §6.1 FR-002 row). Measured this session (g++ 13, `-std=c++20`, repo headers):
`HarmonicCloud` 8 672 + `SpectralMorphEngine` 8 928 + `ContinuousBody` 16 960 + `AtmosphereEngine` 10 272
+ `MultiStageEnvelope` 180 + `GrowthEnvelope` 88 + `OrbitModulator` 112 + `MidSideProcessor` 120 + §2.2's
2 048 B of scratch = **47 380 B per voice**, so `voices_` alone is **758 080 B** and `SeraphisEngine` is
about **750 KB** (plus `TruePeakLimiter` 7 176, `VoiceAllocator` 1 344 and the 2 KB bloom array). SC-005
specifies "two `SeraphisEngine` + `AetherReverb` pairs" — ~1.5 MB against MSVC's **1 MiB default
main-thread stack**, and `dsp/tests/CMakeLists.txt` sets no `/STACK`. Sibling systems TUs declare their
subjects as plain locals (e.g. `dsp/tests/unit/systems/atmosphere_engine_nonfinite_test.cpp:368-369`), so
copying that pattern here stack-overflows. §6.3 therefore makes heap allocation a rule for every
`SeraphisEngine` in every TU, and §8 step 5 records the measured `sizeof(SeraphisEngine)` with a
`static_assert` beside it so the figure is tracked rather than discovered by a crash.

`kBloomPartialCap` is a **local `static constexpr std::size_t kBloomPartialCap = 32;`**
with a comment citing `aether_reverb.h:1442` (`kMaxBloomResonators = 32`) and `:2398-2399`
(`std::min(count, kMaxBloomResonators)`). The Layer 4 constant **cannot be named** from a Layer 3 header,
so it is duplicated with the citation — exactly as the three sibling systems duplicate
`kControlChunkSamples = 64` from each other (FR-007). Memory: 16 × 32 × 4 B = 2 KB.

### 3.3 `prepare` (FR-041)

Prepares **all `kMaxVoices` slots**, not `polyphony_`, so `setPolyphony` can never allocate.
`static_cast<void>(allocator_.setVoiceCount(polyphony_));` — the real signature is
`[[nodiscard]] std::span<const VoiceEvent> setVoiceCount(size_t count) noexcept`
(`voice_allocator.h:326`, documented *"@return Span of NoteOff events for released excess voices"*), **not**
the `void setVoiceCount(size_t)` the spec's Existing-components table records. Written as a bare statement
it discards a `[[nodiscard]]` result — MSVC C4834 / GCC `-Wunused-result` — against SC-019's zero-warning
gate. The explicit `static_cast<void>` is the same treatment §3.6.1 step 2 already gives
`allocator_.noteOff`; at prepare time there is nothing to release, so discarding is correct. §3.6.2 does
consume the return, so this is the only affected call site. **The spec's Existing-components table needs
the return type corrected** — carried in §11.
`allocator_.setAllocationMode(AllocationMode::Oldest)` (`:311`),
`allocator_.setStealMode(StealMode::Hard)` (`:317`) (FR-043).
`satL_.prepare(sr, kControlChunkSamples)`, `satR_.prepare(...)` (`tape_saturator.h:141`) with
`setDrive(kOutputDriveDb)`, `setSaturation(kOutputSaturation)`, `setMix(1.0f)` (`:239`, `:248`, `:266`).
`limiter_.prepare(sr, kMaxBlockSamples)` (`true_peak_limiter.h:59`) at
`kDefaultCeilingDb = -1.0f` (`:46`). `sumGain_.configure(kSumGainSmoothMs, (float)sr)` and
`sumGain_` is **snapped** to `1/√polyphony_` (no ramp from 0 on the first block).
Memory consequence, at `captureSeconds = 4.0f` and `kMaxVoices = 16`: **33.6 MB @ 48 kHz, 67.2 MB @ 96 kHz**
(RA-8, Edge Case 17).

### 3.4 `processStereoBlock` — absolute-grid slicing (FR-051)

```cpp
void processStereoBlock(float* outL, float* outR, std::size_t n) noexcept {
    if (outL == nullptr || outR == nullptr) return;
    if (n == 0) return;
    if (!prepared_) { std::fill_n(outL, n, 0.0f); std::fill_n(outR, n, 0.0f); return; }

    std::size_t done = 0;
    while (done < n) {
        const auto phase = static_cast<std::size_t>(sampleCounter_ % kControlChunkSamples);
        if (phase == 0u) runPreRenderControlStep();              // aether_reverb.h:2183-2186 idiom
        const std::size_t slice = std::min(n - done, kControlChunkSamples - phase);

        std::fill_n(busL_.data(), slice, 0.0f);
        std::fill_n(busR_.data(), slice, 0.0f);
        for (std::size_t v = 0; v < kMaxVoices; ++v) {           // ALL prepared slots — see §3.2
            if (!isRendering(v)) { voices_[v].advanceLifeOnly(slice); continue; }  // FR-027/FR-051
            voices_[v].processStereoBlock(vL_.data(), vR_.data(), slice);
            for (std::size_t s = 0; s < slice; ++s) {
                const float a = vL_[s], b = vR_[s];
                if (!isFiniteBits(a) || !isFiniteBits(b)) {      // FR-072, AT the accumulation point
                    nonFinitePending_ |= (1u << v);              // reset is DEFERRED — see below
                    break;                                       // this voice contributes 0 this slice
                }
                busL_[s] += a; busR_[s] += b;
            }
        }
        const float g = sumGainHeld_;                            // read once per control chunk
        for (std::size_t s = 0; s < slice; ++s) {
            outL[done + s] = busL_[s] * g;
            outR[done + s] = busR_[s] * g;
        }
        sampleCounter_ += slice;  done += slice;
        if (sampleCounter_ % kControlChunkSamples == 0u)         // the slice that COMPLETED a chunk
            runPostRenderControlStep();                          // D4: bloom + retirement, AFTER render
    }
}
```

`isFiniteBits` is the plain inlinable `memcpy`-bit helper, duplicated privately in `SeraphisEngine`
(§2.12) — **not** a `SeraphisVoice` private, which is not reachable from here, and **not** the
`ITERUM_NOINLINE` variant, whose own header forbids per-sample use.

**Why the non-finite reset is deferred.** `SeraphisVoice::reset()` is not cheap: it must call
`atmos_.reset()` (R10 — `reset()` is the atmosphere's only immediate clear, which is also why `silence()`
uses it, §1 D3 / V-14), which calls
`capture_.reset()` (`atmosphere_engine.h:527`), which is a `std::fill` over both channel buffers of the
whole capture ring (`rolling_capture_buffer.h:96-99`, sized to `capacity_` at `:86-87`). At the shipped
`captureSeconds = 4.0f` → 262 144 samples (D6) that is **262 144 × 2 × 4 B = 2 MiB of memset per voice
reset**, plus the blur-FIFO and freeze fills at `atmosphere_engine.h:571-572`, `:589-590`. Calling it
inline inside the per-sample accumulation loop would let a single poisoned block trigger up to 16 of
them, 32 MiB of bulk clearing inside one 1.33 ms control chunk. Instead the offending voice is masked,
contributes 0 for the rest of the slice, and `runPreRenderControlStep` services **at most `kResetsPerControlChunk
= 1`** pending voice — bounding the path at one 2 MiB clear per 1.33 ms. See R13.

**The control step is SPLIT into a pre-render and a post-render half, and the split is load-bearing
(D4).** `runPreRenderControlStep()` runs at `phase == 0`, before the voices render that chunk;
`runPostRenderControlStep()` runs after the slice that *completes* a chunk. Steps 1–3 are pre-render
(they set up the chunk); steps 4–5 are post-render (they observe what the chunk produced). With all five
in front of the render, the bloom snapshot at the boundary following a note-on executes before the voice
has called `updateControl` for that note and reads the **previous** note's partials — the exact staleness
D4 exists to prevent, and SC-017's 0.1-cent comparison (which recomputes the FR-071 selection from
`getVoice(i).cloud()` at assertion time, by which point the cloud *has* recomputed) fails. It also aligns
§10 V-2's "once per absolute control chunk **after** rendering" with what the loop actually does; with the
unsplit step, FR-044/SC-012's state trace was off by one chunk in the other direction.

`runPreRenderControlStep()` does, in order:

1. Sum gain: `sumGain_.advanceSamples(kControlChunkSamples - 1); sumGainHeld_ = sumGain_.process();` —
   read once, held for the whole chunk, so the value is identical under any caller partition (FR-052,
   SC-014). The `- 1` is load-bearing: `OnePoleSmoother::process()` **itself advances one sample**
   (`smoother.h:197-210`), so `advanceSamples(64)` followed by `process()` would advance the smoother
   65 samples per 64-sample chunk. (`advanceSamples(64)` + `getCurrentValue()` `:190-192` is the
   equivalent alternative; pick one and only one.)
2. **Freeze-pending retry** (FR-030a), **staggered**: service at most `kFreezeRetriesPerChunk = 1`
   pending voice — the lowest set bit of `freezePending_`, round-robined from the last serviced index.
   `if (!voices_[v].isFreezeCaptured()) voices_[v].captureFreeze(); else clear bit`.
   `AtmosphereEngine::captureFreeze()` early-outs until its ring holds a whole analysis window
   (`atmosphere_engine.h:914-917`), so the *failing* branch is nearly free — but the **succeeding**
   branch is a `capture_.extractSlice` plus two `SpectralFreezeOscillator::freeze` calls, i.e. an
   FFT of `freezeFftSize` (2048 by the `SeraphisVoiceConfig` default) per channel
   (`atmosphere_engine.h:909-921`). All 16 rings start filling at the same `reset()`, so without
   staggering the availability test flips for every voice on the *same* control chunk and up to
   16 × 2 = 32 FFT(2048) land inside one 1.33 ms chunk. Staggering spreads them over ≥ 16 chunks
   (≥ 21 ms) at a worst case of 2 FFT(2048) per chunk. FR-030a's observable — "after ≥ `captureSeconds`
   of render every voice's `isFreezeCaptured()` is true" — is unaffected, because `captureSeconds`
   is ≥ 4 s ≫ 21 ms. Worst-case per-chunk cost recorded in §9 R14.
3. **Non-finite recovery** (FR-072): service at most `kResetsPerControlChunk = 1` bit of
   `nonFinitePending_` → `voices_[v].reset(); ++nonFiniteRecoveries_;` clear the bit. `reset()` (not
   `resetForSteal()`) — a poisoned voice must not carry a poisoned fade tail forward.
`runPostRenderControlStep()` does, in order:

4. **Bloom collection** (D4):
   `for each v in bloomOnPending_: if (voices_[v].hasRenderedSinceNoteOn()) { snapshot(v); bloomOnMask_ |= bit; clear bit; }`.
   A voice whose flag is still false keeps its pending bit and is snapshotted at a later boundary — that
   is D4 rule 2, and it is what makes the snapshot provably taken *after* `cloud_.updateControl` consumed
   `freqDirty_`. Also clear `orphanTail_` for any slot that has become `isFinished()` (§3.6's `NoteOn`
   predicate).
5. **Deferred retirement** (FR-044): for every slot whose `VoiceState` is `Releasing`
   (`voice_allocator.h:424`) **and** whose `voices_[v].isFinished()` is true → `allocator_.voiceFinished(v)`
   (`:288`) and `bloomOffMask_ |= bit`. Running this on the **absolute control grid** rather than
   "once per block" (FR-044's wording) makes retirement timing partition-invariant; the audible effect is
   nil (the voice is below −100 dBFS) but the state trace SC-012 reads becomes deterministic. Running it
   **after** the render (rather than before, as an earlier draft's single control step did) is what makes
   §10 V-2's own wording true.

`isRendering(v)` is `v < polyphony_ ? (state != Idle || !voices_[v].isFinished())
                                    : !voices_[v].isFinished()` — the second clause is what keeps a
post-shrink orphan tail rendering (FR-040 step 2). **Both clauses depend on §2.7's seeded
`quiescentChunks_`:** with the counter starting at 0 a never-rendered slot is not `isFinished()`, so both
branches return true and all 16 slots take the full audio path for the first 4 control chunks after every
`prepare()`/`reset()`/`silence()` — roughly double SC-001's worst case, in contradiction of FR-041 and
FR-051, and detected by nothing (SC-016 passes on exact zeros; SC-001's best-of-8 warms past the 5.33 ms
window). The loop bound stays `v < kMaxVoices` for the reasons in §3.2; it is the *predicate* that keeps
the spare slots out of the audio path. `getRenderingVoiceCount()` counts exactly this
predicate; `getActiveVoiceCount()` counts `v < polyphony_ && state != Idle`. Slots that are neither
(the 8 spare slots at the shipped polyphony) take the `advanceLifeOnly` branch, which is what makes
SC-016's "**every** voice" clauses satisfiable. Cost: one `OrbitModulator::processBlock` per spare slot
per slice (`orbit_modulator.h:216-229` — an accumulator walk, no audio).

### 3.5 `processOutputStage` (FR-053a, FR-053, FR-054)

```cpp
void processOutputStage(float* l, float* r, std::size_t n) noexcept {
    if (l == nullptr || r == nullptr || n == 0 || !prepared_) return;   // FR-006 guard order
    for (std::size_t done = 0; done < n; done += kControlChunkSamples) {
        const std::size_t slice = std::min(kControlChunkSamples, n - done);
        satL_.process(l + done, slice);                 // tape_saturator.h:335 -- mono, in place
        satR_.process(r + done, slice);
    }
    limiter_.processBlock(l, r, static_cast<int>(n));   // true_peak_limiter.h:104 -- stereo, in place, ALWAYS LAST
}
```

**The saturator's slicing is a cadence choice, not a size constraint.**
`TapeSaturator::prepare(double sampleRate, [[maybe_unused]] size_t maxBlockSize)`
(`processors/tape_saturator.h:141` — Layer **2**, `processors/`, not `effects/` as the roadmap's reuse
table implies) **ignores** its block-size argument; nothing inside the class is sized from it, so there
is no maximum to respect. The loop is kept only so the output stage runs on the same 64-sample cadence
as everything else in the engine, which keeps the two stages easy to reason about together. It is not
load-bearing: `satL_.process(l, n)` / `satR_.process(r, n)` over the whole block is equally correct,
because `TapeSaturator::process` and `TruePeakLimiter::processBlock` are both per-sample stateful and
partition-invariant. **Phase 8 must not copy the loop as if it were a requirement.** The limiter was
prepared at `kMaxBlockSamples` and takes the whole block.

### 3.6 Note dispatch (FR-042) and the steal path (FR-045, FR-046, FR-047, RA-4)

```cpp
void noteOn(std::uint8_t note, std::uint8_t velocity) noexcept {
    if (velocity == 0u) { noteOff(note); return; }            // Edge Case 13; allocator maps it at :230-233
    // Detect the ordinary same-note retrigger UP FRONT, with the allocator's own predicate
    // (voice_allocator.h:830-841, findVoicePlayingNote): a slot that is not Idle and whose
    // tracked note already equals `note`. This is the ONLY way to tell the two provenances
    // of a Steal event apart — see the box below.
    retriggerSlot_ = -1;
    for (std::size_t i = 0; i < polyphony_; ++i)
        if (allocator_.getVoiceState(i) != VoiceState::Idle                 // :424
            && allocator_.getVoiceNote(i) == static_cast<int>(note))        // :406
            { retriggerSlot_ = static_cast<int>(i); break; }
    if (retriggerSlot_ < 0 && noIdleVoice()) freeChosenVictimSlot();        // §3.6.1
    dispatch(allocator_.noteOn(note, velocity));              // voice_allocator.h:228
    retriggerSlot_ = -1;
}
```

`dispatch(span)` follows `PolySynthEngine::dispatchPolyNoteOn` (`poly_synth_engine.h:597-620`):

| `VoiceEvent::Type` | action |
|---|---|
| `NoteOn` | if `(orphanTail_ & (1u << i))` → FR-047 teardown `silence(); resetForSteal(); noteOn(f, vel/127)` and clear the bit (the orphaned post-shrink slot, Q8/FR-042); else plain `voices_[i].noteOn(event.frequency, event.velocity/127.0f)`. Set the `bloomOnPending_` bit and `voiceSerial_[i] = nextSerial_++` **exactly once per dispatched span**, either way. |
| `NoteOff` | `voices_[i].noteOff()` |
| `Steal` (`i == retriggerSlot_`) | **Ignore the event entirely.** It is the allocator's bookkeeping for the outgoing note on an ordinary retrigger; the `NoteOn` event that follows it in the same span does the work. No `silence()`, no `resetForSteal()`, no extra `voiceSerial_` bump. |
| `Steal` (`i != retriggerSlot_`) | Engine-initiated steal. FR-047 teardown (`silence(); resetForSteal(); noteOn(...)`), then set `bloomOnPending_`, `bloomOffMask_` bits and `voiceSerial_[i] = nextSerial_++`. **Unreachable by construction under §3.6.1** (RA-4 frees the victim slot *before* `allocator_.noteOn`, so the allocator sees an idle slot and emits a plain `NoteOn`); retained as a defensive branch, not as the live path. `bloomOffMask_` for a real steal is set in `freeChosenVictimSlot()` — see §3.6.1. |

`resetForSteal()`, not `reset()` — this is the one path that must keep the D3 anti-click fade tail armed
across the teardown (§1 D3). Every other reset caller in the engine uses the tail-clearing `reset()`.

#### 3.6.0 Why the `Steal` row must be split by provenance

**The shipped allocator emits `Steal` on the ordinary same-note retrigger, with no pool saturation
involved.** `VoiceAllocator::noteOn` routes any note already sounding into `retriggerNote`
(`voice_allocator.h:239-242`), which pushes a `Steal` carrying the **OLD** note/velocity/frequency
(`:846-853`) and then a `NoteOn` carrying the new ones for the **same slot** (`:865-872`). Under an
unconditional `Steal → FR-047 teardown` mapping every retrigger would:

- run `silence()` (D3's hard-clear of cloud/morph/body/mse/growth **and** `atmos_.reset()`) plus
  `resetForSteal()` — i.e. the ~2 MiB capture-ring wipe priced in §3.6.1/R13 — on a **live, sounding**
  voice;
- call `voices_[i].noteOn()` **twice** in one dispatch: first with the OLD frequency carried by the
  `Steal` event, then with the new one from the `NoteOn` event;
- bump `voiceSerial_[i] = nextSerial_++` **twice**, corrupting the FR-045 step 4 tie-break key that
  SC-011 clause 5 asserts is strictly increasing in note-on order.

That directly contradicts Clarification Q8 as the spec records it — *"ordinary retriggers on live voices
use a plain `noteOn` with legato continuation"* — destroys FR-020's `RetriggerMode::Legato` continuation
and the body/atmosphere tail the whole RA-2 argument rests on, and puts SC-004 ("64 note-ons **incl.
retriggers on a sounding voice**") at direct risk, since a 1 ms silence ramp plus a full generator reset
would land mid-note. Provenance is therefore established **before** the allocator call, by the engine,
using the allocator's own public read surface — `getVoiceState` (`:424`) and `getVoiceNote` (`:406`) —
which is exactly the predicate `findVoicePlayingNote` (`:830-841`) uses internally.

#### 3.6.1 Freeing the victim slot — the only mechanism the shipped allocator permits (RA-4)

`VoiceAllocator` has no `Quietest` mode (`voice_allocator.h:55-60` offers only
`RoundRobin, Oldest, LowestVelocity, HighestNote`) and *"[d]oes NOT own or process any DSP"* (`:124-125`),
so it cannot see a level. Selection moves to the engine; the allocator stays the bookkeeper.

```
1. Pick victim v:
     candidates = { i : getVoiceState(i) == Releasing }                     // :424
     eligible   = { i in candidates : getVoiceLevel(i) < kAmnestyLevelThreshold }   // FR-046
     if eligible non-empty      -> v = argmin level over eligible
     else if candidates non-empty -> v = argmin level over candidates       // Edge Case 15
     else                       -> v = argmin level over { i : state == Active }
     ties -> lower voiceSerial_[i]   // FR-045 step 4: the OLDER allocation. See below.
2. if getVoiceState(v) == Active:
       static_cast<void>(allocator_.noteOff(
           static_cast<std::uint8_t>(allocator_.getVoiceNote(v))));         // :257, :406 -- events DISCARDED
3. allocator_.voiceFinished(v);   // :288 -- legal now that v is Releasing (:288-292 early-outs otherwise)
4. bloomOffMask_ |= (1u << v);    // FR-071's "when the voice is stolen or finished" -- THE stolen half.
5. lastStolenVoice_ = static_cast<int>(v);
6. allocator_.noteOn(...) is then called by noteOn(); the dispatch loop ASSERTS that the
   returned NoteOn event names v (SC-011). A mismatch is a defect, not a fallback.
```

**Step 4 is where FR-071's stolen half actually lives, and it must be here rather than in §3.6's `Steal`
row.** Because this function frees the victim *before* `allocator_.noteOn` — RA-4 states it outright
("the only idle-or-oldest slot the allocator can now pick for the new note is v") — the allocator sees an
idle slot and returns a plain `NoteOn`. The `Steal` row therefore never fires on a real steal, the
`NoteOn`-with-teardown branch sets `bloomOnPending_` and `voiceSerial_` but not `bloomOffMask_`, and
`runPostRenderControlStep` step 5 never runs for the slot because the allocator already idled it. Without
step 4, `bloomNoteOff` is **never** issued for a stolen voice and the reverb keeps a bloom voice bound to
a note that no longer exists; SC-017 as originally written asserts only the note-off/reclaim path and
detects nothing.

**The tie-break key is engine-owned, because the allocator's is not reachable.** FR-045 step 4 requires
"the older allocator timestamp, preserving `Oldest` semantics", but `VoiceAllocator` exposes no
timestamp: `timestamp` is a member of the **private** internal voice struct (`voice_allocator.h:483`,
`private:` at `:471`) and the public read surface is `getVoiceNote` (`:406`), `getVoiceState` (`:424`)
and `getVoiceFrequency` (`:446`) only. Substituting "lower voice index" would **not** be equivalent:
the allocator's own `Oldest` walk ranks by `voices_[i].timestamp < bestTimestamp` (`:575-576`) and only
falls back to first-index when timestamps are *equal*, so index order and timestamp order differ in
general. Rather than record a silent deviation, the engine tracks allocation order itself:
`voiceSerial_[i] = nextSerial_++` **exactly once per dispatched span** that lands a note on slot `i`
(§3.6) — the retrigger-path `Steal` is ignored and contributes no bump (§3.6.0, V-12), which is what keeps
SC-011 clause 5's "strictly increasing in note-on order" true — and the tie-break is
`argmin voiceSerial_[i]` over the tied set. That satisfies FR-045 step 4 **as written** —
`nextSerial_` increases monotonically with note events exactly as the allocator's `timestamp_` does
(`:236`, `:605`). Exposed for tests as `getVoiceAllocationSerial(i)` (§3.1); SC-011 clause 5 asserts it.

**Teardown cost, priced.** Each steal runs `silence(); resetForSteal(); noteOn(...)` on the audio thread
inside `SeraphisEngine::noteOn`. Both `silence()` (§1 D3's hard-clear) and `resetForSteal()` reach
`AtmosphereEngine::reset()` → `capture_.reset()` → a `std::fill` over the whole stereo capture ring
(`atmosphere_engine.h:527`; `rolling_capture_buffer.h:96-99`, capacity at `:86-87`), which at the shipped
`captureSeconds = 4.0f` is 262 144 samples → **2 MiB of memset per teardown**, plus the blur/freeze fills
(`atmosphere_engine.h:571-572`, `:589-590`). It is bounded and allocation-free, but it is not free, and
neither SC-001's timed region (8 sounding voices, **no steals**) nor SC-002 measures it. It is therefore
**priced explicitly**: `SeraphisEngine_VoiceStealIsClickless` (SC-003) records the measured per-teardown
cost in µs and the **worst single-block wall time** across its 32-steal render, and `REQUIRE`s that worst
block to stay inside the 512-sample budget (10.67 ms @ 48 kHz). A block carrying K note-ons that all
steal costs K teardowns; the figure and the K at which the budget is exceeded are recorded in
`compliance.md`. §9 R13 carries the risk and the escape hatch.

Precedent for the engine owning allocator lifecycle: `PolySynthEngine` already owns the deferred
`voiceFinished` call rather than the allocator (`poly_synth_engine.h:810-813`).

#### 3.6.2 `setPolyphony` shrink (FR-040, Edge Case 8)

`allocator_.setVoiceCount(n)` pushes **`VoiceEvent::Type::NoteOff`** (`voice_allocator.h:340-346`) and, in
the same loop, force-idles each excess slot — `state = Idle`, `note = -1`, `velocity = 0`, `frequency = 0`,
`activeVoiceCount_` decremented (`:347-352`). The engine therefore treats each returned event as a
**musical release** (`voices_[i].noteOff()`), keeps rendering that slot until its own `isFinished()`, does
**not** call `voiceFinished` on it, and guards its reuse via the FR-047 teardown in §3.6's `NoteOn` row.
`sumGain_.setTarget(1/√n)` is the **only** place the sum-gain target moves (FR-052).

**This handler is the sole writer of `orphanTail_`:** for every slot the shrink force-idled while
`!voices_[i].isFinished()`, set `orphanTail_ |= (1u << i)`. The bit is cleared when that slot becomes
`isFinished()` (`runPostRenderControlStep` step 4) or when the `NoteOn` row tears it down.

**Why the predicate is `orphanTail_` and not the earlier draft's
`!voices_[i].isFinished() && allocator_.getVoiceState(i) == Idle`.** That second conjunct can **never** be
true at dispatch time. `VoiceAllocator::allocateNote` stores `VoiceState::Active` into the slot the moment
`findIdleVoice()` returns it — `voice_allocator.h:933-935`, comment *"Mark as active temporarily so
findIdleVoice doesn't find it again"* — and pushes the `NoteOn` event only afterwards, in the final
assignment loop (`:1062`); `retriggerNote` does the same (`:855-860` before the push at `:865`). By the
time the engine iterates the returned span, `getVoiceState(i)` is **always** `Active`. The orphaned
post-shrink path that Clarification Q8 explicitly requires ("FR-047's teardown is required whenever a
`noteOn` lands on a slot the allocator force-idled during a polyphony shrink whose `isFinished()` is
still false") was therefore dead code, and §6.1's FR-042/FR-043 assertion about it failed.
`!voices_[i].isFinished()` **alone** is not a substitute either: it is true for any live retrigger target,
which would re-introduce exactly the full-teardown-on-retrigger defect §3.6.0 removes. Engine-owned state
is the only predicate that separates the two.

### 3.7 `collectHeldPartials` (FR-071) — the selection rule

`bloomNoteOn` truncates at `kMaxBloomResonators = 32` (`aether_reverb.h:1442`, `:2377`, `:2398-2399`)
while `HarmonicCloud::kMaxPartials = 64` (`harmonic_cloud.h:138`), so up to half a full-richness voice's
partials are dropped and the choice matters.

```cpp
outCount = min(cloud.getActivePartialCount(), kBloomPartialCap, capacity);
// 1. build idx[0..active) ; sort by (getPartialCurrentAmplitude desc, index asc)
//    -- harmonic_cloud.h:960 (current amplitude), :950 (active count)
// 2. take the first outCount
// 3. re-sort those by getPartialFrequencyHz ascending   -- :955
// 4. write dest[0..outCount)
```

Both sorts run over a stack `std::array<std::uint8_t, HarmonicCloud::kMaxPartials>` with `std::sort` and a
`noexcept` comparator — allocation-free and exception-free (introsort/heapsort, no heap). Called once per
note-on at a control-chunk boundary, never on the per-sample path.

Amplitude, not index, because the bloom stage is a resonant emphasis of the held chord; reinforcing 32
inaudible upper partials while dropping the fundamental's neighbours would invert the effect.
`getPartialFrequencyHz` returns the **undetuned** synthesized frequency (`:953-956`), which is also what
SC-017 compares against — same accessor on both sides, so the 0.1-cent bound is meaningful.

`voiceId` passed to `bloomNoteOn` is the voice index (`voiceId < 0` is rejected by that API, `:2394`).
`kMaxBloomVoices = 8` (`:1445`) means at polyphony > 8 the reverb retires its own oldest bloom voice
(`:2416-2424`) — accepted behaviour, asserted as such, not treated as an error.

### 3.8 `setAtmosphereFreeze` (FR-030a)

```cpp
void setAtmosphereFreeze(bool on) noexcept {
    freezeLatched_ = on;
    if (on) {
        freezePending_ = (1u << kMaxVoices) - 1u;   // ARM ONLY — no captureFreeze() here
    } else {
        for (std::size_t v = 0; v < kMaxVoices; ++v) voices_[v].releaseFreeze();
        freezePending_ = 0u;                        // releaseFreeze is a cheap state flip
    }
}
```

`setAtmosphereFreeze(true)` **arms and returns**; every actual capture happens in `runPreRenderControlStep` step 2,
one voice per control chunk. Calling `captureFreeze()` on all 16 slots inline would put up to 32
FFT(2048) into a single caller's `setAtmosphereFreeze` call — see §3.4 step 2 for the arithmetic and
§9 R14 for the priced worst case. The pending bit is cleared once `isFreezeCaptured()` becomes true.
`reset()`, `silence()` and a steal on voice `v` re-arm bit `v` while `freezeLatched_` is true.

---

## §4 `SeraphisMacroMatrix` — `systems/seraphis_macro_matrix.h`

### 4.1 Types

```cpp
enum class SeraphisMacro : std::uint8_t { Dream = 0, Bloom, Dissolve, Gravity, Entropy, Count };
enum class SeraphisMacroTargetOwner : std::uint8_t { Voice = 0, Engine, Aether };

enum class SeraphisMacroTarget : std::uint8_t {
    // Voice-owned
    CloudInharmonicity, CloudMutation, CloudSpectralGravity, CloudRichness, CloudSpectralTiltDb,
    CloudStereoSpread, CloudAttackTimeSec, CloudDriftDepthCents,
    MorphEntropy, MorphTargetPosition,
    BodyDamping,
    AtmosLevel, AtmosBlur, AtmosDriftDepth,
    SpatialDepth, VoiceWidth,               // VoiceWidth -> SeraphisVoice::setVoiceWidthBasePercent
    EnvStage0Ms, EnvStage1Ms, EnvReleaseMs,
    // Aether-owned  (must have a 1:1 field in SeraphisAetherTargets)
    AetherMix, AetherSize, AetherWidth, AetherShimmerOctaveSend, AetherShimmerFifthSend,
    AetherBloomSend, AetherSizeBreathDepth, AetherDimensionalityTideDepth,
    Count
};

struct SeraphisAetherTargets {                       // FR-056; plain floats, no Layer 4 type named
    // Defaults ARE the eight `base` values below, so a default-constructed
    // SeraphisAetherTargets is already the FR-060 neutral.
    float mix = 0.35f, size = 0.50f, width = 1.0f;
    float shimmerOctaveSend = 0.0f, shimmerFifthSend = 0.0f, bloomSend = 0.0f;
    float sizeBreathDepth = 0.20f, dimensionalityTideDepth = 0.20f;
};

struct SeraphisMacroValues {                         // FR-060 documented neutrals
    float dream = 0.0f, bloom = 0.0f, dissolve = 0.0f, gravity = 0.5f, entropy = 0.0f;
};

struct SeraphisMacroRow {                            // FR-058: the mapping is DATA
    SeraphisMacro            macro;
    SeraphisMacroTargetOwner owner;
    SeraphisMacroTarget      target;
    float                    base;      // the FR-019 shipped voice default, or — for the eight Aether
                                        // rows — the DUPLICATED component default enumerated below
    float                    amount;    // SIGNED; implementation tuning (Q3)
    ModCurve                 curve;     // Linear | Exponential | SCurve ONLY (FR-057)
};
```

#### 4.1.0 `VoiceWidth` routes to a macro-owned width CENTRE, `base = 100.0f`

FR-062's Bloom row calls for "`SeraphisVoice` width ↑", but §2.6 recomputes and pushes the M/S width once
per control chunk from the orbit's y, so a `VoiceWidth` row that wrote into `ms_` directly would be
overwritten within ≤ 64 samples — an inert row, and one that **no criterion detects**: SC-009's Bloom
stereo-width secondary ("L/R correlation ↓ and M/S side energy ↑") is also driven by the `CloudStereoSpread`
row in the same macro, so it passes with `VoiceWidth` completely broken. FR-019's body-`setWidth` row is
also explicit that "the voice's own width axis is the FR-025 M/S stage, driven by the orbit's y", so the
M/S width is not a directly-settable target, and §2.1's public surface exposed only
`getSpatialWidthPercent()` — no setter at all — so the row named a target that did not exist.

**Resolution:** the row targets `SeraphisVoice::setVoiceWidthBasePercent` (§2.1), and §2.6 becomes
`widthPct_ = clamp(widthBase_ + y · widthSpan_, kMinVoiceWidthPct, kMaxVoiceWidthPct)`. The orbit still
owns the *movement*; the macro owns the *centre*. `base = 100.0f` is FR-019's shipped value, so at the
FR-060 neutral the expression reduces to the previous `100 + y·50` exactly and FR-025/FR-026 are
untouched. The alternative — re-routing the row to `ContinuousBody::setWidth` (`continuous_body.h:1056`,
FR-019 base 1.0) — was rejected because it changes which axis FR-062 names and would require amending
FR-019's rationale for that row, whereas the centre/span split delivers FR-062 as written.

SC-009's Bloom gets an observable that **isolates** this row from `CloudStereoSpread`: sample
`getSpatialWidthPercent()` at a fixed orbit phase (`setSpatialRate(0)` with a pinned seed, so y is constant
across the sweep) and require it to rise monotonically with Bloom. The side-energy secondary is retained
but measured with `setStereoSpread` held at its FR-019 base, so the two rows cannot cover for each other.

#### 4.1.1 The eight Aether `base` values are duplicated literals, with citations

`AetherReverb`'s defaults are **unreachable from any consumer**: `private:` opens at
`aether_reverb.h:2724` and every one of them sits below it. Neither `SeraphisMacroMatrix` (Layer 3, and
FR-056 forbids naming a Layer 4 type at all) nor the FR-070 test helper can name them. They are therefore
duplicated as literals — the same treatment §3.2 gives `kBloomPartialCap`, and for the same reason:

| `SeraphisAetherTargets` field | `base` | Component constant | Line |
|---|---|---|---|
| `mix` | **0.35f** | `kDefaultMix = 0.35f` | `aether_reverb.h:2779` |
| `size` | **0.50f** | `kDefaultSize = 0.50f` | `:2730` |
| `width` | **1.0f** | `kDefaultWidth = 1.0f` | `:2777` |
| `shimmerOctaveSend` | **0.0f** | `kDefaultSend = 0.0f` (`shimmerOctSm_.snapTo(kDefaultSend)` `:1911`) | `:2760` |
| `shimmerFifthSend` | **0.0f** | `kDefaultSend` (`shimmerFifthSm_.snapTo` `:1913`) | `:2760` |
| `bloomSend` | **0.0f** | `kDefaultSend` (`bloomSendSm_.snapTo` `:1915`) | `:2760` |
| `sizeBreathDepth` | **0.20f** | `kDefaultSizeBreathDepth = 0.20f` (member init `:4629`) | `:2749` |
| `dimensionalityTideDepth` | **0.20f** | `kDefaultTideDepth = 0.20f` (member init `:4630`) | `:2750` |

**Duplication drift is a real failure mode, not a theoretical one.** §5's helper pushes all eight targets
into the reverb every block, so a drifted literal silently *overrides* the component's real default — a
`base` of 0 for `sizeBreathDepth` would flatten SC-016 clause 3's only life observable, and a `base` of 0
for `mix` would run the whole composed chain dry while every other criterion still passed. Comparing the
matrix's output against the same literals it carries (SC-010 clause 3) cannot detect that. The detector
is a **render** differential, added to the FR-070 helper's TU (which links Layer 4 and can therefore see
the real behaviour even though it cannot name the constants): with the macros at the FR-060 neutral,
a reverb driven by `computeAetherTargets()` every block and a reverb never touched after `prepare` must
produce fingerprints that match (`compareFingerprints(...).withinTolerance()`). Any drifted literal
breaks that equality. Listed in §6.2 under SC-010 clause 3.

### 4.2 The table and its compile-time guards

```cpp
static constexpr std::array<SeraphisMacroRow, kNumRows> kRows = {{ … }};

// FR-058: no row may be unreachable, and no Aether target may lack a POD field.
static_assert(everyRowOwnerIsValid(kRows));
static_assert(everyAetherRowHasAPodField(kRows));
static_assert(noRowUsesSteppedCurve(kRows));          // RA-7 / FR-057
static_assert(everyTargetInFr061to065IsPresent(kRows));
```

`ModCurve::Stepped` is excluded because it is `std::floor(x·4)/3` (`core/modulation_curves.h:53-54`) — over
SC-009's 21-step sweep that is 18 zero-change steps and 3 jumps of ~1/3, a jump/mean-step ratio of ~6.7×,
failing SC-009's 3× continuity bound by construction. `Exponential` (x², `:46`) peaks at ~1.95× the mean
step, which is where the 3× factor comes from. All three permitted curves are evaluated through the shared
Layer 0 `applyModCurve(curve, x)` (`core/modulation_curves.h:38`).

### 4.3 Evaluation (FR-057)

```
for each target t:
    acc = base(t)
    for each row r with r.target == t:
        m = knob(r.macro)
        if r.macro == SeraphisMacro::Gravity:
            g = (m - 0.5f) * 2.0f                                   // FR-064, bipolar
            acc += r.amount * applyModCurve(r.curve, std::abs(g)) * (g < 0 ? -1.0f : 1.0f)
        else:
            acc += r.amount * applyModCurve(r.curve, m)
    write acc through the owning setter (which does its own clamping)
```

Summation-then-clamp is `ModulationEngine`'s order (`modulation_engine.h:44-54`) and is kept so Phase 9
inherits familiar semantics. Three targets are hit by two macros each and that is specified, not
accidental (FR-057, Edge Case 9): `CloudRichness` (Bloom ↑ / Gravity −), `CloudSpectralTiltDb`
(Bloom ↑ / Gravity −), `MorphEntropy` (Dream − / Entropy ↑).

At the FR-060 neutral (`gravity = 0.5`, rest 0) every term is exactly 0 — `applyModCurve(c, 0) = 0` for all
three permitted curves and `g = 0` for Gravity — so `apply()` writes exactly `base` and is **inert**
(SC-010 clause 2). No `if (neutral) return;` shortcut: the inertness must be a property of the arithmetic,
so a mis-signed row cannot hide behind a fast path.

### 4.4 Application surfaces (FR-056)

```cpp
void apply(SeraphisEngine& engine) const noexcept;              // Voice + Engine rows
[[nodiscard]] SeraphisAetherTargets computeAetherTargets() const noexcept;   // Aether rows
```

`apply` iterates `v < engine.getPolyphony()` and pushes through `SeraphisVoice`'s forwarders. It requires a
**non-const** voice accessor, which FR-085's const `getVoice(i)` deliberately is not — so `SeraphisEngine`
declares `friend class SeraphisMacroMatrix;` and the matrix reaches `voices_[v]` directly. This keeps
`getVoice(i)` const for tests (the reason Q4 gave for making the freeze triggers engine methods) while
giving the matrix write access without widening the public surface. Alternative rejected: a public
non-const `voiceForMacro(i)`, which any caller could use to bypass the matrix.

#### 4.4.1 FR-059 idempotence is an AUDITED property, not an assumed one

FR-059 requires that `apply()` and `computeAetherTargets()` are real-time safe **and idempotent** —
"calling them every block with unchanged knobs must not step any parameter". The earlier draft supported
this with one sentence ("every target setter early-outs on an unchanged value", citing
`harmonic_cloud.h:417-420`) and **no assertion anywhere**. That is not established for every writable
target on the list, and two of them provably lack an early-out:

| Writable target | Early-out? | Evidence / action |
|---|---|---|
| `Cloud*` rows (richness, inharmonicity, mutation, gravity, tilt, spread, attack, drift) | yes | `harmonic_cloud.h:417-420`, `:432-434` — `if (v == richness_) return;` |
| `MorphEntropy` | yes | forwards to `EntropyProcessor::setEntropy`, which stores a scalar and marks dirty; no per-call state advance |
| `MorphTargetPosition` | **NO** | `spectral_morph_engine.h:348-354` stores `targetPosition_` unconditionally after a non-finite guard. Harmless — it is a plain scalar store with no smoother and no travel advance — but it is a *store*, not an early-out, so it is recorded here rather than claimed as one |
| `BodyDamping`, `AtmosLevel`, `AtmosBlur`, `AtmosDriftDepth` | yes (clamp-and-store into a smoother **target**, not a step) | the smoothers advance on the render clock, not on the setter |
| `SpatialDepth`, `VoiceWidth` | yes (scalar stores) | `orbit_modulator.h:184-186`; `widthBase_` is a plain member |
| `EnvStage0Ms`, `EnvStage1Ms`, `EnvReleaseMs` | **NO — guard ADDED** | `SeraphisVoice::setEnvelopeStageTimeMs` is `applyStage(...)` (§2.3 step 7), which wrote `stageLevel_`/`stageTimeMs_` and called `mse_.setStage(...)` unconditionally. `MultiStageEnvelope::setStage` has no equality guard of its own, so an unchanged-knob re-apply rewrote the stage every block. §2.3 step 7 now opens `applyStage` with `if (stageLevel_[st] == level && stageTimeMs_[st] == ms) return;` |

`computeAetherTargets()` is a pure function of the knobs and the table — it writes nothing — so its half of
FR-059 is structural. The matrix adds no smoother of its own.

**The property is now asserted, at a NON-neutral operating point.** SC-010 clause 2 and the retained
clause-4 render exercise repeated `apply()` only at the FR-060 neutral, which is the single operating
point where the §4.3 arithmetic writes `base` and therefore *cannot* step anything — they can never detect
a missing early-out. A new clause on `SeraphisMacroMatrix_NeutralIsInert` (§6.1's FR-056…FR-060 row) holds
the macros at Bloom = Dissolve = 0.7, Gravity = 0.8, renders 1 s with `apply()` called every block, and
compares against a render where `apply()` is called **once** before the loop; the two must satisfy
`compareFingerprints(...).withinTolerance()`.

---

## §5 Test helper `tests/test_helpers/seraphis_chain.h` (FR-070)

`tests/test_helpers/CMakeLists.txt` declares `add_library(test_helpers INTERFACE)` with
`target_include_directories(... INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})` and **enumerates no headers** — so
dropping the file in the directory is the whole registration. **FR-081's "registered in
`tests/test_helpers/CMakeLists.txt`" requires no edit**; recorded so the implementer does not invent one.

```cpp
struct SeraphisChainScript {                 // note events + macro/param edits, TIME-stamped in SECONDS
    struct Event { double seconds;           // NOT samples -- see "Event timing" below
                   enum class Kind { NoteOn, NoteOff, Freeze, Polyphony } kind;
                   std::uint8_t note, velocity; std::size_t value; };
    std::vector<Event> events;               // TEST-ONLY: heap is fine here, SC-008 does not scan this file

    /// Resolve to an absolute sample index at the render's rate. Events MUST already be sorted by
    /// `seconds`; renderSeraphisChain asserts that and asserts the resolved indices are non-decreasing.
    [[nodiscard]] static std::uint64_t toSamples(double seconds, double sampleRate) noexcept;
};

/// voice sum -> AetherReverb -> processOutputStage, plus SeraphisAetherTargets and bloom lifecycle.
void renderSeraphisChain(SeraphisEngine& engine, AetherReverb& reverb,
                         const SeraphisMacroMatrix& macros, const SeraphisChainScript& script,
                         double sampleRate, std::size_t blockSize, std::size_t totalSamples,
                         std::vector<float>& outL, std::vector<float>& outR);
```

**Event timing is partition-independent by construction — two rules, both load-bearing.**

1. **Sub-division at event boundaries.** `renderSeraphisChain` does **not** dispatch a whole block's
   events at the block head. It splits each caller block at every event's resolved sample index:
   dispatch the events due at that index, then render up to the next event index or the end of the
   block, whichever comes first. `SeraphisEngine::noteOn` / `noteOff` have no sample-offset parameter
   (§3.1), and sub-division is how a sample-accurate offset is delivered without one — it is also
   exactly what Phase 8's `processParameterChanges` / event-list loop will do with the host's
   `sampleOffset`, so the helper stays the faithful model.

   Without this rule **SC-014 cannot pass with any non-trivial script**. An event at sample S fires at
   `ceil(S/B)·B`, which agrees across partitions {1, 7, 64, 65, 512, 4096} only when S is a multiple of
   `lcm(7, 65, 512, 4096) = 1 863 680` samples (≈ 38.8 s @ 48 kHz) — longer than SC-014's own 10 s
   render. A note-on at sample 5000 fires at 5000 under the 1-sample partition and at 4096 under the
   4096-sample partition: a 904-sample shift in onset, orders of magnitude above SC-014's ≤ 1e-5
   per-sample bound and entirely unrelated to D1's carry FIFO (the risk R1 that case exists to cover).
   D1 makes the *control grid* partition-invariant; this rule makes *event time* partition-invariant.
   The resulting engine partitions differ between runs (a 4096 block may render as 3000 + 1096), which
   is precisely the invariance D1 guarantees, so the two rules compose.

2. **Seconds, not samples.** `Event::seconds` is resolved per render through `toSamples(seconds, rate)`.
   A sample-denominated script is a *different piece of music* at 44.1 / 48 / 96 kHz, so SC-013's "the
   same note script at three rates" would not be comparing the same script at all. Recorded in §10 V-10.

**Allocation contract (for SC-007).** `renderSeraphisChain` sizes `outL`/`outR` and its internal
`dryL`/`dryR`/`wetL`/`wetR` scratch **once, before the render loop**, and performs **zero** allocations
inside it. The scratch is sized to `blockSize` (the maximum sub-slice a split block can produce), and
`std::vector::resize` is never called from within the loop. This matters because SC-007 is a *runtime*
detector (`AllocationScope`), which a grep exemption does not help: an allocation inside the helper would
be reported as an engine defect. §6.2's SC-007 row states exactly what the scope wraps.

Per sub-slice, in this fixed order (Phase 8's processor reproduces it verbatim):

1. dispatch the script events due at this slice's start onto `engine`;
2. `macros.apply(engine)`; `const auto at = macros.computeAetherTargets();` then push
   `reverb.setMix(at.mix)`, `setSize`, `setWidth`, `setShimmerOctaveSend`, `setShimmerFifthSend`,
   `setBloomSend`, `setSizeBreathDepth`, `setDimensionalityTideDepth`
   (`aether_reverb.h:2336, 2208, 2333, 2280, 2285, 2295, 2320, 2328`);
3. `engine.processStereoBlock(dryL, dryR, n)`;
4. `reverb.processStereoBlock(dryL, dryR, wetL, wetR, n)` (`:2164`);
5. `engine.processOutputStage(wetL, wetR, n)`;
6. `const auto be = engine.consumeBloomEvents();` for each set bit: `reverb.bloomNoteOff(v)` (`:2473`)
   then, for on-bits, `engine.collectHeldPartials(v, buf, 32, count)` → `reverb.bloomNoteOn(v, buf, count)`
   (`:2392`). Step 6 is **after** step 3 by D4/D5.

`buf` is a `std::array<float, 32>` held by the helper across the whole render, not a per-slice local —
part of the allocation contract above.

---

## §6 Test plan

All five TUs live in `dsp/tests/unit/systems/` and run in `dsp_systems_tests`.
Tags: `[.perf]` hidden from the default run (the `atmosphere_engine_perf_test.cpp:1251, :1322` idiom);
`[.slow]` for the SC-020 full-grid halves.

### 6.1 FR coverage

| FR | TU | `TEST_CASE` | Assertion strategy |
|---|---|---|---|
| FR-001, FR-002 | voice | `SeraphisVoice_LayerAndOwnership` | The header compiles inside `dsp/lint_all_headers.cpp` and `tools/lint-layers.js` gates the layer direction. **Positive half:** the five sub-component const accessors (`cloud`, `morph`, `body`, `atmos`, `orbit`) exist and return distinct addresses. **Negative half** — FR-002's "owns no `AetherReverb`, no `StereoField`, no `VoiceModRouter`, no `ModulationEngine`" — is *not* covered by either of those: `lint-layers.js` only catches a Layer **4** include, so a stray `StereoField`/`VoiceModRouter`/`ModulationEngine` member (all Layer 3) passes every existing gate, leaving RA-3's whole argument for not instantiating `StereoField` (4 `DelayLine`s + a `MidSideProcessor` per instance, 64 delay lines across the pool) unguarded. Two additions: (a) a grep clause folded into SC-008's compliance sweep over the three headers — **zero hits** for `StereoField`, `VoiceModRouter`, `ModulationEngine`, `PolySynthEngine`, `SynthVoice`, `AetherReverb`; (b) `static_assert(sizeof(SeraphisVoice) <= kVoiceSizeBound)` as a coarse positive-count guard, with the eight declared members enumerated in the comment above it. `kVoiceSizeBound` is **measured** at §8 step 2 and recorded as `ceil(measured × 1.05)` — not guessed — and any of the six forbidden members would blow it. |
| FR-003, FR-005 | voice | `SeraphisVoice_PrepareAndResetAreIdempotent` | second `prepare` while sounding → silent output, `isFinished()`; `reset()` → fingerprint-identical to a freshly prepared voice over 1 s. |
| FR-004 | voice | `SeraphisVoice_ConfigIsClampedNeverRejected` | `captureSeconds = -5 / 1e9`, `maxBlockSamples = 0 / 1e6` → prepared, non-silent, `atmos().getCaptureCapacitySamples()` inside `[1,30] s` bounds (D6). |
| FR-006, Edge 1–3 | voice | `SeraphisVoice_ProcessGuards` | null L / null R / `n == 0` / before `prepare`; assert no write, no counter movement (`getSpatialAzimuth()` unchanged for `n == 0`). |
| FR-007, D1 | voice | `SeraphisVoice_ControlGridIsPartitionInvariant` | render 1 s in {1, 7, 64, 65, 512, 4096}-sample partitions; max abs per-sample diff vs the 512 reference **≤ 1e-6**. Tighter than SC-014 because the voice alone has no reverb in the chain. |
| FR-008 | all | — | `tools/lint-nonfinite-symbols.js` + SC-008's grep, recorded in `compliance.md`. |
| FR-010 | voice | `SeraphisVoice_ChainOrderIsCloudEnvelopeBodyAtmosphere` | **Mutations go through `SeraphisVoice`'s FR-030 forwarders, never through the introspection accessors** — `body()`/`atmos()` are `const ContinuousBody&` / `const AtmosphereEngine&` (§2.1, FR-085), so `body().setMix(0)` does not compile. With `voice.setMix(0.0f)` the output is the enveloped cloud + atmosphere; with `voice.setLevel(0.0f)` it is body-only; note-off then +5 s still non-silent at `setCloudDecaySec(30)` — proves the envelope is **pre**-body. (SC-010 clause 4 already routes through the forwarders; this row and SC-009's Dissolve metric were the two that did not.) |
| FR-011, FR-012 | voice | `SeraphisVoice_MorphHandoffRunsEveryChunk` | drive `setTargetPosition` mid-render; assert `cloud().getPartialTargetAmplitude(i)` tracks `morph().getOutputAmplitudes()[i]` within one chunk. |
| FR-013 | — | — | SC-008 grep; plus `static_assert(sizeof(SeraphisVoice) < 3 * 1024 * 1024)` guarding against an accidental heap-free giant. |
| FR-014 | voice | `SeraphisVoice_ShipsFourSecondCapture` | `atmos().getCaptureCapacitySamples() == 262144` @ 48 kHz (D6). |
| FR-015 | voice | `SeraphisVoice_HasNoLatencyAccessor` | impulse-ish gate at t = 0 produces non-zero output inside the first chunk. |
| FR-016, FR-017, FR-018 | voice | `SeraphisVoice_SeedingIsDeterministic` | same seed → `compareFingerprints(...).withinTolerance()`; salts asserted pairwise distinct at compile time. |
| FR-019, FR-019a | voice | `SeraphisVoice_ShipsDocumentedDefaults` | SC-010 clause 1 (§6.2). |
| FR-020, FR-021, FR-022 | voice | `SeraphisVoice_EnvelopeModesBehave` | Standard: `getEnvelopeOutput()` reaches ≥ 0.99 only after ~2 s. **Growth (rewritten — see the box below §6.1):** advance a standalone reference `GrowthEnvelope` alongside the voice (same `prepare(sampleRate)`, same `setDuration(10.0f)`, `trigger()` on the same sample, `processBlock(64)` on the same chunk grid) and require `getEnvelopeOutput() == Approx(velocity · stageLevel_[sustainPoint] · growthRef.getCurrentValue()).margin(1e-4)` for every control chunk of the duration, from the second chunk onward (the first covers the 0 ms pre-sustain stage walk). That is FR-021's "match the `GrowthEnvelope` shape alone" stated literally, and it fails loudly if any pre-sustain MSE ramp is left in series. Secondary, derived from the real curve: composite monotone non-decreasing and ≥ 0.99 of its final value only within the **last 10 %** of the duration. Legato retrigger: a gate on a `Releasing` envelope does **not** drop below its pre-gate level on the next sample. **Round-trip clause (FR-021's "leaving Standard mode restores the FR-020 stage times"):** after `setEnvelopeMode(Growth)` then `setEnvelopeMode(Standard)`, `getEnvelopeStageTimeMs(0) == Approx(2000)` and `(1) == Approx(4000)`, and a gated render again reaches ≥ 0.99 only after ~2 s. Without §2.3 step 7's `applyStage` helper the shadows are still `{0, 0}` here and the restore silently installs a 0 ms attack — this clause is the only thing that detects it. |
| FR-023, FR-024 | voice | `SeraphisVoice_NoteLifecycle` | body/atmos keep ringing after `noteOff`; `growth_` not reset (re-note continues the rise). |
| FR-025, FR-026 | voice | `SeraphisVoice_SpatialStageMath` | `setSpatialDepth(0)` → `getSpatialAzimuth() == 0`, `getSpatialWidthPercent() == Approx(100).margin(1e-4)`, and stage transparency **≤ 1e-6 per sample** vs a build with the stage bypassed. Also assert `gL == gR == Approx(1.0f).margin(1e-6f)` at centre. |
| FR-027 | voice | `SeraphisVoice_AdvanceLifeOnlyMatchesRender` | azimuth/width identical after `n ∈ {1, 7, 64, 65, 512}` through either path — **every `n`**, not just multiples of 64, because §2.8 puts both paths on the shared carry clock. |
| FR-030, FR-031 | voice | `SeraphisVoice_ForwardersAndConfigureTimeGate` | Every forwarder round-trips through the component getter where one exists (including `setTravelMode(SpectralMorphEngine::TravelMode::Spline)` → `getTravelMode()`). **Reject path:** `setSpectralState` on a *sounding* voice → rejected, `getRejectedConfigureTimeCallCount()` increments, `morph().getStateCount()` unchanged. **Accept path (mandatory — a gate that rejects unconditionally must fail):** on a freshly prepared, never-noted voice, `setSpectralState(1, makeFactoryState(SpectralStateId::Bell))` + `setSpectralStateCount(3)` are observable as `morph().getStateCount() == 3` (`spectral_morph_engine.h:443`) with `getRejectedConfigureTimeCallCount()` **unchanged**; likewise after `noteOn`/`noteOff` and enough render for `isFinished()`. Without both halves, §2.11's `!hasSounded_ || isFinished()` predicate reduces to the unreachable `isFinished()`-only form and nothing detects it. |
| FR-030a | engine | `SeraphisEngine_FreezeFansOutAndRetries` | `setAtmosphereFreeze(true)` on a cold pool → no voice captured immediately; after ≥ `captureSeconds` of render **every** voice's `isFreezeCaptured()` is true; a voice stolen afterwards re-arms and eventually captures. |
| FR-032, FR-033 | voice | `SeraphisVoice_LevelDetectorAndRetirement` | full-scale then silence → `getCurrentLevel()` crosses `kTailSilenceThreshold` at **1.15 s ± 5 %** (D2's coefficient is what this pins); `isFinished()` becomes true ≤ 1.16 s after audio silence. |
| FR-034, FR-047 | engine + voice | `SeraphisEngine_StealTeardownOrder`, `SeraphisVoice_SilenceHardClears` | **Engine case:** `silence()` → `resetForSteal()` → `noteOn()` completes inside one block; `atmos().isFreezeCaptured()`/grain counters prove the atmosphere was re-entered via the reset. **Voice case (new, and the only thing that tests FR-034's hard-clear):** on a *sounding* voice, `voice.silence()` then render 512 samples — samples `[silenceRampSamples_, 512)` are all ≤ `kTailSilenceThreshold`, and samples `[0, silenceRampSamples_)` are bounded by the pre-silence `|lastOut|` and monotonically non-increasing in magnitude. **This window is satisfiable only because §1 D3 calls `atmos_.reset()`, not `atmos_.silence()`.** `AtmosphereEngine::silence()` is a 10 ms ramp-then-latch (`kSilenceRampMs = 10.0f`, `atmosphere_engine.h:278`; per-sample decay at `:2237-2242`; state flip at `:644-650`) = 480 samples @ 48 kHz, ten times the voice's own 48-sample ramp, and its grains keep reading the capture ring at the shipped `setLevel(0.5f)` — so with `atmos_.silence()` the assertion fails by construction on any sounding voice. The engine case cannot substitute: its following `reset()` masks a `silence()` that forgot to clear the generators, so a `silence()` that only armed the fade and latched the atmosphere would render the voice at full level plus a decaying tail and still pass. |
| FR-035, FR-072 | nonfinite | `SeraphisEngine_NonFiniteIsContained` | SC-018 (§6.2). |
| FR-040, FR-041 | engine | `SeraphisEngine_PolyphonyAndPreparation` | `getPolyphony() == 8` after default prepare; `setPolyphony(16)` mid-render allocates nothing (allocation probe); shrink emits `NoteOff` (not `Steal`) and the slot keeps rendering. |
| FR-042, FR-043 | engine | `SeraphisEngine_NoteDispatch` | velocity 0 → note-off; same-note retrigger occupies **one** slot (`voice_allocator.h:237-244`). **Retrigger-provenance clause (§3.6.0, mandatory):** a same-note retrigger on a live sounding voice must produce **exactly one** `voices_[i].noteOn()` on that slot, must leave `atmos().isFreezeCaptured()` and the atmosphere's grain counters **untouched** (proving no `silence()`/`resetForSteal()` ran), and must increment `getVoiceAllocationSerial(i)` **exactly once**. Without it the allocator's retrigger-path `Steal` event (`voice_allocator.h:846-853`, followed by a `NoteOn` for the same slot at `:865-872`) maps onto the FR-047 teardown and every retrigger silently wipes a live voice. **Orphan clause:** an orphaned post-shrink slot gets the FR-047 teardown — assert the new note's first chunk contains no residue of the old tail, and assert it is `orphanTail_`-driven by checking that a *non*-orphan `NoteOn` onto a still-ringing slot (an ordinary retrigger) does **not** tear down. |
| FR-044 | engine | `SeraphisEngine_DeferredVoiceFinished` | SC-012 clause 1. |
| FR-045, FR-046 | engine | `SeraphisEngine_QuietestStealWithAmnesty` | SC-011 (§6.2). |
| FR-050 | engine | `SeraphisEngine_VoiceSeedsAreDistinct` | SC-006(a). |
| FR-051, FR-052 | engine | `SeraphisEngine_VoiceSumGain` | one note at polyphony 1 vs 8 → RMS ratio `Approx(1/√8).epsilon(0.01)` after the smoother settles; the ratio does **not** drift as tails retire. |
| FR-053, FR-053a, FR-054 | engine | `SeraphisEngine_OutputStageIsSeparate` | `processStereoBlock` output may exceed the ceiling; `processOutputStage` on the same buffer never does. **Plus the FR-053 constants themselves** — nothing else in the plan asserts drive 0 dB / saturation 0.15 / mix 1.0, and the ceiling clause passes just as readily at saturation 1.0 because the limiter bounds the result either way, which makes the Traceability row for roadmap line 78 ("No aggressive distortion") vacuous. Drive a −6 dBFS 1 kHz sine through `processOutputStage` at the shipped defaults and `REQUIRE` `calculateTHD(out, n, 1000.0f, sr)` (`tests/test_helpers/signal_metrics.h:111`, returns **percent**) below a bound recorded from the measurement at §8 step 5, with a **positive control**: `setOutputSaturation(1.0f)` on the same input must exceed it by a stated margin. FR-053's values are outside FR-019's table (voice targets only), so SC-010 does not reach them. |
| FR-055 | engine | `SeraphisEngine_ResetAndSilence` | after `silence()` the next block is **exactly 0** for every sample — which holds because `SeraphisEngine::silence()` is per-voice `silence()` then the tail-**clearing** `reset()`, never `resetForSteal()` (§1 D3). A subsequent note sounds normally (proves the atmosphere was `reset()`, not left latched). **Positive control:** a build in which `SeraphisEngine::silence()` uses `resetForSteal()` must fail this row — record the measured non-zero peak. |
| FR-056…FR-060 | macro | `SeraphisMacroMatrix_TableIsWellFormed`, `SeraphisMacroMatrix_NeutralIsInert` | Table `static_assert`s compile; each of FR-061…FR-065's named targets is present with the stated sign; no `Stepped` curve. **FR-059 idempotence clause (new — nothing previously asserted FR-059 at all):** with the macros held at a **non-neutral** point (Bloom = Dissolve = 0.7, Gravity = 0.8), render 1 s calling `apply()` every block and compare against a render calling `apply()` **once** before the loop; require `compareFingerprints(...).withinTolerance()`. The neutral-point renders (SC-010 clause 2, clause 4) cannot cover this — at the FR-060 neutral §4.3 writes `base` and cannot step anything by construction. Paired with §4.4.1's per-target early-out audit, which added the missing guard to `applyStage`. |
| FR-061…FR-065 | macro | `SeraphisEngine_MacroSweepsMoveTheirAxis` | SC-009 (§6.2). |
| FR-070 | engine | `SeraphisEngine_ComposedChainRuns` | `renderSeraphisChain` produces non-silent, finite output; the helper is the FR-070 realisation. |
| FR-071 | engine | `SeraphisEngine_BloomTracksHeldChord` | SC-017 (§6.2). |
| FR-080…FR-084 | — | — | build integration (§7) + `compliance.md`. |
| FR-085 | all | — | every accessor is exercised by the cases above; a case that needs `#ifdef` scaffolding is a defect. |

### 6.2 SC coverage

| SC | TU / `TEST_CASE` | Method, thresholds, controls |
|---|---|---|
| **SC-001** | perf / `SeraphisEngine_FullPolyCpuBudget` `[.perf]` | Composed chain, the RA-1 normative scenario **stated in code**: polyphony 8, all 8 sounding, macros at FR-060 neutral, 64 partials + drift, body at the worst measured material config, atmosphere **frozen** with `REQUIRE(voice.isFreezeCaptured())` for every voice **before** timing starts, `AetherReverb` config (c) (`numChannels=16`, shimmer+bloom+diffusion on, `diffusionFftSize=4096`, `setSize(1)`, `setDensity(1)`, 32 bloom resonators — `aether_reverb_perf_test.cpp:329-330`). Reference `kBlockBudgetNs = (512/48000)·1e9`; `kReferenceNs = kBlockBudgetNs · 0.25`; `kMaxAdmissibleNs = kReferenceNs / kRegressionFactor`; `static_assert(kBaselineNs · kRegressionFactor <= kReferenceNs)`. ≥ 8 trials, best-of-N, idle machine; the baseline-replacement procedure is transcribed into the TU header. **Two distinct constants, and Phase 7's run-time gate is NOT the Phase 6 file's** — see the box below. Prediction to beat: 20.36 %. |
| **SC-002** | perf / `SeraphisVoice_CompositionOverhead` `[.perf]` | Ratio of one `SeraphisVoice::processStereoBlock` to the arithmetic sum of **eight** standalone sub-components measured in the same TU under the pinned shared configuration (64 partials + drift, FR-019 defaults, worst body material, atmosphere default density **unfrozen**, Standard envelope gated on, spatial depth 0.5, 512-sample blocks @ 48 kHz). Bound **≤ 1.10**. ≥ 8 trials, best-of-N per subject, ratio from the aggregates. |
| **SC-003** | engine / `SeraphisEngine_VoiceStealIsClickless` | Matched-regime, single render: 60 s saturated pool, 32 steals at randomised block offsets. Statistic = `maxPerSampleDelta` over the ±10 ms window at each steal. Reference = **95th percentile** of 64 same-length (20 ms) windows drawn ≥ 50 ms clear of any steal, uniformly spaced. Bound `max(stat) ≤ 1.5 × reference`. Plus SC-015's ceiling clause. **Both positive controls are mandatory:** (a) an injected one-sample step of **2× that window's own `maxPerSampleDelta`** must exceed the bound (denominated in delta, not peak — Phase 2 measured that a step below the natural swing is undetectable, `specs/seraphis-phase2-harmonic-cloud/spec.md:748-753`); (b) a `kSilenceRampMs = 0` build must fail clause 3, recorded as a measured figure. `[.slow]` for the full 32-steal/60 s form; always-on runs 8 steals over 10 s. **Teardown-cost clause (§3.6.1, R13):** the same render records the per-teardown wall time and the **worst single-block wall time** over the whole run, and `REQUIRE`s that worst block to stay inside the 512-sample budget (10.67 ms @ 48 kHz). This is the only place the FR-047 teardown's ~2 MiB capture-ring memset is timed — SC-001's timed region contains no steals at all, and SC-002 measures a voice in steady state. Both figures go in `compliance.md`, together with the K (note-ons per block) at which the budget is exceeded. |
| **SC-004** | engine / `SeraphisEngine_NoteLifecycleIsClickless` | Same construction and same two controls over 64 note-ons (incl. retriggers on a sounding voice, exercising `harmonic_cloud.h:604-606`) and 64 note-offs. **Growth clause, rewritten.** The transcribed "composite monotone non-decreasing, ≥ 0.99 only in the last 5 % of the duration" is **unsatisfiable against the shipped component and inert against the defect it was written to catch**, so it is replaced with the spec's own primary wording rather than relaxed. `GrowthEnvelope` is a *normalised* logistic, `y(τ) = (L(τ)−L(0))/(L(1)−L(0))` with `L(t) = 1/(1+exp(−k(t−0.5)))` and `kSteepness = 10.0f` (`growth_envelope.h:18-26`, `:102`). Solving `y(τ) = 0.99` gives `L(τ) = 0.983441` → **τ = 0.9085**, i.e. the shape crosses 0.99 of its final value at 9.08 s of a 10 s duration — outside "the last 5 %" (τ ≥ 0.95), so the always-on FR-020/021/022 case and this one both failed by construction. Nor could the clause detect the failure it targets: if FR-021 zeroed only stage 0 and left FR-020's 4 s stage-1 ramp in series, the composite is `0.7·growth(t)` after 6 s, still crosses 0.99-of-final at τ = 0.9085 and is still monotone increasing (the MSE's 1.0 → 0.7 decay over 2–6 s is dominated by growth's rise: composite 0.041 at 2 s, 0.068 at 2.5 s, 0.514 at 6 s), so **both halves were inert**. Replacement: the §6.1 FR-020/021/022 reference-`GrowthEnvelope` comparison (`getEnvelopeOutput() ≈ velocity · sustainLevel · growthRef.getCurrentValue()`, margin 1e-4, whole duration) — which is satisfiable, is "match the `GrowthEnvelope` shape alone" read literally, and fails loudly on a leftover pre-sustain ramp. The threshold-style secondary is kept but derived from the real curve: **≥ 0.99 of final only within the last 10 %** (τ ≈ 0.909, plus the 20 ms `kOutputSmoothMs` lag, `:103`). |
| **SC-005** | engine / `SeraphisEngine_SeededRenderIsReproducible` | Two `SeraphisEngine` + `AetherReverb` pairs, same seed/config/script, 30 s (`[.slow]`) / 5 s (always-on); `compareFingerprints(...).withinTolerance()` — `worstMetricRelativeError ≤ 1e-5`, `worstSampleError ≤ 1e-4` (`render_fingerprint.h:49-52`). **Bit-exact comparison is forbidden** (FR-084, `tools/lint-float-bit-goldens.js`). |
| **SC-006** | engine / `SeraphisEngine_VoiceSeedsAreDistinct` (a); voice / `SeraphisVoice_VoicesDriftIndependently` (b) | (a) pairwise-distinct, non-zero derived seeds across all 16 slots, asserted against `deriveStreamSeed(engineSeed, kVoiceSaltBase + v)`. (b) **standalone** `SeraphisVoice` objects seeded with that exact expression, same note, pairwise Pearson |ρ| ≤ 0.5 over 30 s (`[.slow]`; 4 voices × 5 s always-on); same-seed control > 0.999. (b) is **not** at engine level because `VoiceAllocator::noteOn` retriggers the one existing slot for a repeated note (`voice_allocator.h:237-244`). |
| **SC-007** | engine / `SeraphisEngine_NoAllocInProcess` | `tests/test_helpers/allocation_detector.h` `AllocationScope`; **liveness probe first** (one deliberate allocation observed) then 0 allocations across a script with note-ons/offs, steals, polyphony 1↔16, every macro swept. **What the scope wraps, stated:** only `engine.processStereoBlock`, `engine.processOutputStage`, `engine.noteOn`/`noteOff`/`setPolyphony`/`setAtmosphereFreeze`, `engine.collectHeldPartials`/`consumeBloomEvents`, and `macros.apply`/`computeAetherTargets` — **not** `renderSeraphisChain`'s setup, and not `AetherReverb`. The helper's own contract (§5) is zero allocations inside its render loop, but the scope is drawn around the engine calls so that a helper-side slip is diagnosed as a helper slip rather than reported as an engine defect. |
| **SC-008** | — (compliance) | `grep -nE 'new \|delete \|malloc\|std::vector\|std::string\|std::function\|mutex\|lock\|throw\|try \{\|printf\|fopen\|std::cout\|shared_ptr\|unique_ptr\|resize\(\|push_back\|emplace\|std::isnan\|std::isinf\|isfinite'` over the three headers → **zero code hits**, enumerated in `compliance.md`. Satisfiable because §2.2/§3.2 are all `std::array`. **Second sweep, for FR-002's negative half:** `grep -nE 'StereoField\|VoiceModRouter\|ModulationEngine\|PolySynthEngine\|SynthVoice\|AetherReverb'` over the same three headers → **zero code hits** (prose citations in comments are permitted and enumerated). These are all Layer 3 (except `AetherReverb`), so `tools/lint-layers.js` cannot catch them and nothing else in the plan guards RA-3's argument for not instantiating `StereoField`. |
| **SC-009** | macro / `SeraphisEngine_MacroSweepsMoveTheirAxis` `[.slow]` | 5 macros × 21 steps × 4 s on the composed chain, fixed seed and note, each non-swept macro at **its own** FR-060 neutral. Primary metric + secondary observables exactly as the spec's two tables. Gate: Spearman \|ρ\| ≥ 0.9 (monotone **trend**, not strict); **no-discontinuity**: consecutive step change ≤ 3× the mean step change; **minimum end-to-end effect size** per the spec's second table (Dream ≤ 50 % of its start value, Bloom ≥ +20 %, Dissolve ≥ +0.15 abs, Gravity ≥ 6 dB, Entropy ≥ +0.10 abs). **Pinned detector:** 65 536-point FFT (`primitives/fft.h`), `Window::generateBlackmanHarris` (`core/window_functions.h`) — the exact pair `harmonic_cloud_spectral_test.cpp:51, :157` uses, with `REQUIRE(fft.isPrepared())` so a future `kMaxFFTSize` tightening fails loudly; analysis segment = the last 1 s of each 4 s step; peak picking at −60 dB-from-max with ≥ 20 dB peak-to-local-median SNR; parabolic interpolation on log magnitude; peaks matched to grid slots by nearest ratio, unmatched excluded, **< 24 detected partials fails the case**. Flatness via `calculateSpectralFlatness` (`tests/test_helpers/signal_metrics.h:326`). **Dream's primary metric is measured on `processStereoBlock`'s dry voice sum with the Aether `mix` target held at neutral**, so reverb smearing cannot corrupt the partial detector; its reverb-send sub-axis is the wet-tail secondary on the composed chain. **Dissolve's primary differential goes through `SeraphisVoice`'s FR-030 forwarder, not the accessor** — `atmos()` is `const AtmosphereEngine&` (§2.1, FR-085), so `AtmosphereEngine::setLevel(0)` on it does not compile; the zeroed arm applies `voice.setLevel(0.0f)` to **every** voice via the forwarder, and the matrix is **not** applied on that arm (applying it would rewrite `AtmosLevel` from the table's `base` and restore the very thing being zeroed). **Bloom's stereo-width secondary isolates the `VoiceWidth` row (§4.1.0):** `getSpatialWidthPercent()` sampled at a fixed orbit phase (`setSpatialRate(0)`, pinned seed, so y is constant across the sweep) must rise monotonically with Bloom, and the side-energy/correlation observable is measured with `setStereoSpread` held at its FR-019 base — otherwise `CloudStereoSpread` carries the whole secondary and a completely broken `VoiceWidth` row passes. Always-on probe: 1 macro × 5 steps × 1 s, every metric computed and every direction correct. |
| **SC-010** | macro / `SeraphisMacroMatrix_NeutralIsInert`; voice / `SeraphisVoice_ShipsDocumentedDefaults` | Clause 1: the **whole** FR-019 table read back through the components' own getters — cloud (`getRichness`…`getSpectralGravity` `harmonic_cloud.h:490-494`, `getDriftDepthCents` `:527`, `getStereoSpread` `:549`, `getAttackTimeSec`/`getDecayTimeSec` `:595-596`), morph (`entropy().getEntropy()` — `spectral_morph_engine.h:453` returns the processor, `entropy_processor.h:305` the value — `getBloom` `:440`, `getTravelRate` `:441`, `getTravelMode` `:442`, `getStateCount` `:443`), atmosphere (`getLevel` `:950`, `getBlur` `:878`, `getDensity` `:796`, `getGrainSeconds` `:783`, `getDriftDepth` `:840`, `getPanSpread` `:862`, `getDecorrelation` `:869`, `getFreezeMix` `:886`, `isFreezeCaptured() == false`), spatial (`orbit().getDepth()`/`getRate()`/`getCoupling()`/`getGrowth()` `orbit_modulator.h:189-192`, reached through the §2.1 `orbit()` accessor — **without it these four rows, including the zero-travel fix `setSpatialDepth = 0.35`, have no reachable read-back at all**), envelope (`getEnvelopeStageTimeMs`/`getEnvelopeReleaseMs`/`getEnvelopeMode`/`getTravelMode`). Clause 2: `apply()` at neutral is the identity on that surface. Clause 3: `computeAetherTargets()` equals the table's `base` for all eight fields, **plus** §4.1.1's render differential in the FR-070 helper's TU — the literal-vs-literal comparison alone cannot detect a drifted duplicate. Clause 4: see below. |
| | | **SC-010 clause 4, rewritten.** The four getter-less `ContinuousBody` rows — `setDamping` (the load-bearing zero-travel fix, 0.25), `setResonance`, `setMix`, `setCloudMix`; the twelve getters at `continuous_body.h:1242-1320` are material/mode/T60/drive/RMS/crossfade/cloud-loop/clamp-count only, and N-9 forbids adding more — get a **differential built from `SeraphisVoice`'s own FR-030 forwarders**, three renders of 4 s each from a fresh `prepare` at a fixed seed: **A** = `prepare` only; **B** = `prepare` + explicitly calling each of the four forwarders at the FR-019 table value (`setDamping(0.25f)` &c.); **C** = `prepare` + `setDamping(0.60f)`. Require `compareFingerprints(A, B).withinTolerance()` **and** `!compareFingerprints(A, C).withinTolerance()` — the second is the mandatory positive control that proves the render is actually sensitive to the parameter, so B ≡ A is evidence that `prepare` shipped 0.25 rather than evidence that nothing is wired. The earlier draft's "matrix applied every block vs never applied" comparison **cannot detect these rows failing**: both arms come from the same build and the same `prepare`, so whatever value `prepare` shipped appears identically in both and the comparison is invariant to it. That render is retained, but only as what it actually is — the FR-060 **inertness** check (clause 2's engine-level counterpart), not a defaults check. |
| **SC-011** | engine / `SeraphisEngine_QuietestStealWithAmnesty` | Saturated pool at known distinct levels established by rendering **≥ 8 control chunks (≥ 10.7 ms)** per voice at its intended level (sufficient because FR-033's attack is instant), then read back and `REQUIRE` distinct **before** forcing the steal. Assertions: (1) lowest-level `Releasing` voice is taken; (2) with none `Releasing`, the lowest-level `Active`; (3) a `Releasing` voice ≥ `kAmnestyLevelThreshold` is skipped while a candidate below exists; (4) **Edge Case 15 / FR-046's fallback branch** — with **every** voice `Releasing` and **every** level ≥ `kAmnestyLevelThreshold`, a forced steal still takes the lowest-level `Releasing` slot (there are no `Active` voices, so an implementation that falls through to the `Active` branch or refuses to steal fails here; clauses 1–3 all pass for such an implementation, which is why this clause is needed); (5) **FR-045 step 4's tie-break** — two voices given `noteOn` then immediately `noteOff` with no render in between are both `Releasing` at exactly `level_ == 0.0f` (the reset value, §2.2, untouched because the detector only updates inside a chunk step), an exact tie; a third note must steal the one with the **lower** `getVoiceAllocationSerial(i)`, and serials must be strictly increasing in note-on order; and `getLastStolenVoiceIndex()` equals both the FR-045 selection **and** the slot the allocator's returned `NoteOn` named (the RA-4 assertion). |
| **SC-012** | engine / `SeraphisEngine_VoiceReclaimIsCorrect` | 60 s script with 10 s+ tails (`setCloudDecaySec(30)`). Clause 1: never `Idle` while `getVoiceLevel(i) > kTailSilenceThreshold`; never dropped from `getRenderingVoiceCount()` while `isFinished()` is false. **Polyphony shrink is exempt from the allocator-state half** (`voice_allocator.h:347-352` force-idles), the rendering half still applies. Clause 2: after the last note-off + 45 s, `getActiveVoiceCount() == 0 && getRenderingVoiceCount() == 0`. The 45 s is derived: 30 s cloud decay + 1.15 s detector release + 4 chunks ≈ 31.2 s, ~14 s margin. |
| **SC-013** | engine / `SeraphisEngine_SampleRateIndependence` | 44.1 / 48 / 96 kHz, **the same seconds-denominated `SeraphisChainScript`, resolved per rate via `toSamples` (§5 rule 2)** — a sample-denominated script is a different piece of music at each rate, so "the same note script" would not be what gets rendered; composed chain, **`setDriftDepthCents` held at its FR-019 default of 0**. RMS and mean-abs within **5 %** computed per rate on that rate's own render (no resampling); peak bounded separately at **10 %**; measured fundamental within **1 cent** over `[2.0 s, 3.0 s)` after note-on (past the 20 ms pitch smoother, `continuous_body.h:168`, and the FR-020 attack) with the same 65 536-point Blackman-Harris estimator. Grain-dependent timing detail explicitly exempt (RA-8, D6). |
| **SC-014** | engine / `SeraphisEngine_BlockSizeInvariance` | Same 10 s composed-chain render in partitions {1, 7, 64, 65, 512, 4096} vs the 512 reference; **max abs per-sample difference ≤ 1e-5** (Phase 5's SC-011 figure, `specs/seraphis-phase5-atmosphere/spec.md:1433-1434`). `render_fingerprint.h` kept only as a **secondary** aggregate check — it samples 32 of 480 000 points (`:46`, `:83-86`) and its 1e-5 relative `totalVariation` bound is tighter than the sub-components guarantee for a re-partitioned computation. **Required in-case coverage:** the parameter set must put at least one partition boundary **inside** a 64-sample control chunk **and** at least one grain birth in that partial chunk, asserted via `AtmosphereEngine::getTotalGrainsBorn()` (`:1009`) at a non-multiple of 64 — otherwise D1's FIFO path is assumed rather than exercised. **Event timing is a separate, equally required constraint.** The script MUST contain at least one note event after sample 0 (a note-on, a note-off and a steal), and those events MUST land at the same absolute sample index in all six runs — which is what §5 rule 1's sub-division at event boundaries delivers. Without it the case cannot pass with any non-trivial script: an event at sample S fires at `ceil(S/B)·B`, agreeing across {1, 7, 64, 65, 512, 4096} only when S is a multiple of 1 863 680 samples (≈ 38.8 s @ 48 kHz), longer than the 10 s render — a note-on at sample 5000 shifts by 904 samples between the 1- and 4096-sample partitions, orders of magnitude above the 1e-5 bound and entirely unrelated to D1. The case asserts event-time equality directly: `getVoiceAllocationSerial(v)` transitions and `getLastStolenVoiceIndex()` must occur at identical sample counts in all six runs. |
| **SC-015** | engine / `SeraphisEngine_OutputNeverExceedsCeiling` | 60 s adversarial composed-chain render (16 voices, all macros at 1 — note that puts **Gravity at full stone**, +0.5 from neutral — maximum resonance, frozen atmosphere, infinite Aether decay). No sample of **`processOutputStage`'s output** exceeds `TruePeakLimiter::kDefaultCeilingDb = -1.0f` (`true_peak_limiter.h:46`) by more than 0.1 dB; no non-finite sample. The voice sum and the reverb return are intermediate and explicitly **not** bounded. |
| **SC-016** | engine / `SeraphisEngine_LifeModulatorsRunAtIdle` | **No notes ever played**, 60 s (`[.slow]`) / 24 s always-on (one full breath cycle at Phase 6's pinned 0.05 Hz), driving `processStereoBlock` every block so every idle voice takes `advanceLifeOnly` (FR-051). Clause 1: **every** voice's `getSpatialAzimuth()` has non-zero total variation — **all 16 slots, not just the 8 below the shipped polyphony**, which is why §3.4's render loop runs `v < kMaxVoices` and gives every non-rendering slot `advanceLifeOnly` (§3.2). Narrowing this clause to `v < polyphony_` would be exactly the quiet scope reduction the criterion exists to prevent. Clause 2: **every** voice's `getSpatialWidthPercent()` has non-zero total variation — this is the guard against a width axis multiplied by `getGrowth()`'s neutral 0. Clause 3: non-zero total variation of `AetherReverb::getEffectiveDelayLengthSamples(0)` (`:2506`) — the only life observable the class exposes; Phase 6 declined to add a breath/tide accessor (`specs/seraphis-phase6-aether-space/spec.md:2160`). Audio asserted **exactly 0**. |
| **SC-017** | engine / `SeraphisEngine_BloomTracksHeldChord` | Three `noteOn`s, render until each voice's snapshot has been taken (D4: the first absolute boundary after that voice completed a `renderOneChunk`), then for each sounding voice compare `getLastBloomPartials(i)` against the FR-071 selection recomputed from `getVoice(i).cloud()` — within **0.1 cent**, `getLastBloomCount(i) == min(getActivePartialCount(), 32)`, ordering ascending by frequency. **Staleness positive control (mandatory):** record the FR-071 selection **before** the note-on and `REQUIRE` the snapshot differs from it by more than 0.1 cent on at least one partial. Without this clause the whole case passes on a snapshot taken one control chunk early — the pre-split `runControlStep` ran the collection before the voices rendered, so it always read the previous note's (or the post-reset default) partials, and the assertion above compares against a cloud that has since recomputed. **Steal clause (mandatory):** force a steal on a voice with a live bloom and require `consumeBloomEvents().noteOffMask` to carry that slot in the same or an earlier poll than its new `noteOnMask` bit — this is the only test of FR-071's "when the voice is **stolen** or finished" half, which lives in `freeChosenVictimSlot()` step 4 (§3.6.1) because §3.6's `Steal` row is unreachable under RA-4. **Note-off clause:** after `noteOff` + reclaim, the voice's bit appears in `consumeBloomEvents().noteOffMask`. |
| **SC-018** | nonfinite / `SeraphisEngine_NonFiniteIsContained` | TU carries `-fno-fast-math -fno-finite-math-only`; non-finite inputs built **from bit patterns through a volatile sink** (never `std::numeric_limits::quiet_NaN()`, which folds to finite garbage under the macOS leg's `-ffast-math`). Inject into one voice: composed-chain output finite for every sample of the next 5 s, `getNonFiniteRecoveryCount()` increments **exactly once**, other voices fingerprint-identical to a control. |
| **SC-019** | — | Zero warnings under MSVC and `g++ -Wall -Wextra -std=c++20` (WSL, g++ 13 available); `node tools/check-portability.js` clean on the staged tree. |
| **SC-020** | — (compliance) | Always-on wall clock ≤ 60 s for `dsp_systems_tests`' Phase 7 share, split per the spec's always-on/`[.slow]` table. Measured figure recorded in `compliance.md`; if it exceeds 60 s the response is a further **recorded** demotion, never a silent one. |

#### 6.2.1 SC-001's two factors — stated, because an earlier draft conflated them

`aether_reverb_perf_test.cpp` uses **two different numbers** and the earlier draft of this plan borrowed
the wrong one while calling the result "the pattern verbatim". It is not verbatim, and the substitution
is load-bearing:

| Constant | Value in the Phase 6 file | Role |
|---|---|---|
| `kRegressionFactor` | **1.5** (`aether_reverb_perf_test.cpp:138`) | the **run-time gate**: every `REQUIRE(measured <= baseline · kRegressionFactor)` at `:1155-1170`, and the `static_assert(baseline · kRegressionFactor <= kReferenceNs)` at `:350`, `:356`, `:362`, `:368`, `:374`, `:380` |
| the `× 1.05` in `ceil(109138 × 1.05)` (`:325`, `:329`, `:333`, `:337`, `:342`) | **1.05** | the **baseline-recording headroom** applied once when a new baseline is transcribed from a measurement — a different quantity entirely |

Phase 7 therefore names them separately: **`kBaselineHeadroom = 1.05`** (recording convention, used only
in the comment arithmetic when a baseline is replaced) and **`kRegressionFactor = 1.15`** (the run-time
band and the `static_assert` multiplier).

**Why not 1.5, and why not 1.05.** The two are both wrong here, in opposite directions, and the
arithmetic must be on the table *before* implementation rather than discovered at §8 step 10:

- **1.5 cannot fit the ceiling.** At RA-1's predicted 20.36 % of the block budget, a baseline recorded as
  `ceil(20.36 % × 1.05) = 21.38 %` gives `21.38 % × 1.5 = 32.1 % > 25 %`, so
  `static_assert(kBaselineNs · kRegressionFactor <= kReferenceNs)` would **fail to compile**. The widest
  run-time band a 25 % ceiling admits at that baseline is `25 / 21.38 = 1.169`.
- **1.05 is not a gate.** It compiles (`21.38 % × 1.05 = 22.45 % ≤ 25 %`) but collapses the run-time
  regression band from 50 % to 5 % on a best-of-8 wall-clock measurement across machines — a flake
  generator, not a regression detector.
- **1.15 is the choice.** `21.38 % × 1.15 = 24.59 % ≤ 25 %`, so the `static_assert` holds with ~0.4
  points of margin, and a 15 % run-time band is defensible for a best-of-8 timing on an idle machine
  (Phase 6 could afford 50 % because its 5 % global ceiling left far more relative headroom).

**The margin is thin and the lever is named now, per R7.** The admissible baseline ceiling is
`25 % / 1.15 = 21.74 %` of the block budget. If the measured Phase 7 baseline lands above that, the
`static_assert` fails and the **only** admissible responses are R7's: re-derive the shipped voice count
(RQ-1; `kMaxVoices = 16` means no ABI change) or reduce Phase 7's own composition cost. **Not** a
Phase 2/4/5/6 gate (N-10), **not** the 25 % ceiling (RQ-1 kept it), and **not** a quiet widening of
`kRegressionFactor` — any change to it must be accompanied by the re-derived arithmetic above.

### 6.3 Test-construction rules and shared fixtures

**Every `SeraphisEngine` in every TU is heap-allocated: `auto engine = std::make_unique<SeraphisEngine>();`.
Never a plain local.** `sizeof(SeraphisEngine)` is ~750 KB (§3.2's measured arithmetic: 47 380 B/voice ×
16 = 758 080 B for `voices_` alone), and SC-005 constructs **two** of them plus two `AetherReverb`s — ~1.5 MB
against MSVC's 1 MiB default main-thread stack, with no `/STACK` set anywhere in `dsp/tests/CMakeLists.txt`.
The sibling systems TUs declare their subjects as plain locals (e.g.
`dsp/tests/unit/systems/atmosphere_engine_nonfinite_test.cpp:368-369`), so copying that pattern here
stack-overflows before a single assertion runs. A standalone `SeraphisVoice` (~47 KB) is safe as a local
but is heap-allocated too wherever a case holds more than four of them. The measured
`sizeof(SeraphisEngine)` is recorded at §8 step 5 with a `static_assert` beside it, so the figure is
tracked rather than rediscovered by a crash.

Shared fixtures (kept in each TU's anonymous namespace, not in a helper):

- `makeVoiceConfig()` → the FR-014 shipped `SeraphisVoiceConfig`.
- `makeWorstBodyConfig(SeraphisVoice&)` → the `ContinuousBody` configuration behind
  `specs/seraphis-phase4-continuous-body/compliance.md:19`, so SC-001 and SC-002 share one definition.
- `makeAetherConfigC(AetherReverb&)` → RA-1 row (c), matching `aether_reverb_perf_test.cpp:329-330`.
- `maxPerSampleDelta(span)` → the SC-003/SC-004 statistic.
- `spearman(a, b)` → the SC-009 rank correlation.

---

## §7 Build integration

| File | Change |
|---|---|
| `dsp/CMakeLists.txt` | Add the three headers to `KRATE_DSP_SYSTEMS_HEADERS` (`set(...)` at `:152`, e.g. next to `include/krate/dsp/systems/poly_synth_engine.h` at `:164`). FR-080. |
| `dsp/lint_all_headers.cpp` | Add `#include <krate/dsp/systems/seraphis_voice.h>` / `seraphis_engine.h` / `seraphis_macro_matrix.h` in the Layer 3 block (shape at `:167-168`). FR-080. Missing either site is a silent CI gap. |
| `dsp/tests/CMakeLists.txt` | Add the five TUs to the `dsp_systems_tests` source list (enumerated, not globbed — the Phase 5 block ends at `:349`). Add **only** `unit/systems/seraphis_nonfinite_test.cpp` to the `-fno-fast-math -fno-finite-math-only` `set_source_files_properties` block (`:690-728`); the other four must **not** be listed — the perf TU especially, because `-fno-fast-math` would move the figures its baselines are pinned to (the block's own comments at `:704-714` state this rule for Phases 5 and 6). |
| `tests/test_helpers/CMakeLists.txt` | **No change.** `test_helpers` is `add_library(... INTERFACE)` with a `target_include_directories` and enumerates no headers; dropping `seraphis_chain.h` in the directory is the whole registration. |
| `tools/check-portability.js` | **No change expected.** The exclusion at `:85-96` exists for TUs carrying an `#error` guard (the `KRATE_DSP_AETHER_TEST_HOOKS` case); no Phase 7 TU needs one. If one is added, the exclusion must be too. |

Targets to build and run:

```bash
"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target dsp_systems_tests
build/windows-x64-release/bin/Release/dsp_systems_tests.exe 2>&1 | tail -5
build/windows-x64-release/bin/Release/dsp_systems_tests.exe "[.perf]" 2>&1 | tail -20
build/windows-x64-release/bin/Release/dsp_systems_tests.exe "[.slow]" 2>&1 | tail -10
node tools/check-portability.js
node tools/lint-layers.js && node tools/lint-nonfinite-symbols.js && node tools/lint-float-bit-goldens.js
```

No plugin changed → **pluginval is skipped**. `dsp_systems_tests` already links `KrateDSP` and
`test_helpers` (`dsp/tests/CMakeLists.txt:352-358`), and `KrateDSP` carries the Layer 4 headers, so the
chain helper's `effects/aether_reverb.h` include needs no target change.

---

## §8 Implementation order

Each step ends green before the next begins.

1. **Skeletons + registration.** Three headers with the public API of §2.1/§3.1/§4.1 and empty bodies;
   `dsp/CMakeLists.txt` + `lint_all_headers.cpp`; `node tools/lint-layers.js` clean. Verify: builds, lints.
2. **`SeraphisVoice` prepare/reset/defaults.** §2.3–§2.4 plus FR-019/FR-019a, including the `applyStage`
   helper that maintains the envelope shadows. Verify: `SeraphisVoice_ShipsDocumentedDefaults`
   (SC-010 clause 1) passes — this is the base point everything else is measured from, so it goes first.
   **Also record `kVoiceSizeBound`** here: print `sizeof(SeraphisVoice)`, set the constant to
   `ceil(measured × 1.05)`, and land the `static_assert` (§6.1 FR-002 row).
3. **`renderOneChunk` + the carry FIFO (D1).** §2.5–§2.7. Verify:
   `SeraphisVoice_ControlGridIsPartitionInvariant` at ≤ 1e-6, `SeraphisVoice_ChainOrderIs…`.
4. **Envelope modes, spatial stage, notes, level detector, silence carry.** §2.6, §2.9–§2.10, D2, D3.
   Verify: FR-020…FR-027, FR-032/FR-033 cases.
5. **`SeraphisEngine` pool, dispatch, sum, output stage.** §3.3–§3.5. Verify: FR-040…FR-044, FR-051…FR-055,
   plus §6.1's retrigger-provenance clause (§3.6.0).
   **Record the FR-053 THD bound** here from the measured −6 dBFS sine at the shipped defaults, and land
   the `setOutputSaturation(1.0f)` positive control (§6.1 FR-053 row).
   **Also record `sizeof(SeraphisEngine)`** here — print it, land a `static_assert` on
   `ceil(measured × 1.05)` beside `kVoiceSizeBound`'s, and put the figure in `compliance.md`. §3.2's
   ~750 KB estimate is a projection from the sub-component sizes; the recorded number is the fact, and it
   is what makes §6.3's heap rule auditable rather than advisory.
6. **Steal path (RA-4) + amnesty + the FR-045 serial tie-break.** §3.6, §3.6.1. Verify: SC-011 (all five
   clauses), SC-003 (including the teardown-cost clause).
7. **Freeze fan-out, bloom collection, `consumeBloomEvents`.** §3.7–§3.8, D4/D5. Verify: FR-030a, SC-017.
8. **`seraphis_chain.h` + composed-chain cases.** §5. Verify: SC-005, SC-013, SC-014, SC-015, SC-016.
9. **`SeraphisMacroMatrix`.** §4, with the table's `amount`/`curve` **tuned against SC-009's minimum
   effect-size table** — expect iteration here; that is what Q3 left to implementation.
   Verify: SC-009, SC-010.
10. **Perf TU + baselines.** SC-001, SC-002, following `aether_reverb_perf_test.cpp`'s baseline-provenance
    and replacement procedure verbatim. Verify: `[.perf]` green with `static_assert`ed baselines.
11. **Non-finite TU, portability, warnings, SC-020 wall-clock measurement, `compliance.md`.**

---

## §9 Risks and mitigations

| # | Risk | Mitigation |
|---|---|---|
| R1 | **Block-size invariance (SC-014).** `HarmonicCloud`/`SpectralMorphEngine`/`EntropyProcessor` take one control step *per call*, not per absolute chunk (D1's evidence). A naive absolute-grid slicer fails SC-014 by orders of magnitude. | D1's carry FIFO makes every sub-component call exactly 64 samples. Step 3 of §8 gates on a **tighter** voice-level bound (1e-6) than SC-014 itself, so the defect surfaces before the chain is assembled. |
| R2 | **Level-detector time constant (D2).** Using `calculateOnePolCoefficient(kLevelReleaseMs, sr)` gives τ = 20 ms, not 100 ms, silently breaking FR-032's 1.15 s derivation and SC-012's 45 s window. | The explicit `std::exp(-chunk/(τ·sr))` form, plus `SeraphisVoice_LevelDetectorAndRetirement` asserting the 1.15 s crossing at ±5 %. |
| R3 | **Denormals in the tail.** Voices ring toward −100 dBFS for 30 s; the FDN and modal banks recirculate. | `kTailSilenceThreshold = 1e-5` is two decades above the `float` denormal region, so FTZ/DAZ cannot make retirement unreachable (spec's own derivation). Tests enable FTZ/DAZ via `tests/test_helpers/enable_ftz_daz.h` where the sibling phases do. `ContinuousBody` and `AetherReverb` already carry denormal floors. |
| R4 | **Non-finite under `-ffast-math` (macOS leg).** `std::isnan` folds away; `quiet_NaN()` folds to finite garbage. | `detail::isNaN`/`detail::isInf` bit-pattern helpers behind an `ITERUM_NOINLINE` wrapper (`atmosphere_engine.h:1214-1216` idiom); `tools/lint-nonfinite-symbols.js`; SC-018's TU builds with IEEE semantics and constructs its inputs from bit patterns through a volatile sink. |
| R5 | **MSVC-green ≠ CI-green.** Narrowing in brace init, missing `<cstdint>`/`<algorithm>`/`<cmath>` includes, `size_t` vs `std::size_t`. | Designated initialisers everywhere (`AtmosphereEngine::PrepareConfig`, `SeraphisVoiceConfig`); `node tools/check-portability.js` before every commit; a WSL `g++ -Wall -Wextra -std=c++20` pass as SC-019 requires. |
| R6 | **Steal assertion fragility (RA-4).** The mechanism relies on the allocator's `Oldest` search picking the just-freed slot. | The engine **asserts** the returned `NoteOn` names the chosen slot and records it in `getLastStolenVoiceIndex()`; SC-011 checks it. A future allocator change fails loudly instead of stealing the wrong voice. |
| R7 | **CPU budget (SC-001).** RA-1 predicts 20.36 % against a 25 % ceiling — 4.6 points for the voice sum, spatial stage, output stage and macro matrix, none of which carries a roadmap budget. The carry FIFO adds a `std::copy_n` of 2×64 floats per chunk. **§6.2.1 tightens this:** with `kRegressionFactor = 1.15` the admissible baseline is 21.74 % of the block budget, i.e. **1.4 points of real margin, not 4.6**. Also, §3.4 now advances all 16 slots (8 spare `OrbitModulator::processBlock` walks per slice at the shipped polyphony). | The FIFO copy is ~0.5 % of one voice's chunk cost and is inside SC-002's 10 % allowance; the spare-slot orbit walks are accumulator arithmetic with no audio path (`orbit_modulator.h:216-229`) and are inside the same allowance. If the `static_assert` at §6.2.1 fails, the **only** admissible levers are the shipped voice count (a re-derivation of RQ-1 — `kMaxVoices = 16` means no ABI change) and Phase 7's own composition cost. **Never** a Phase 2/4/5/6 gate (N-10), never the 25 % ceiling (RQ-1 kept it), and never a quiet widening of `kRegressionFactor` past §6.2.1's arithmetic. |
| R8 | **SC-009 amount tuning is open-ended.** Amounts/curves are implementation tuning (Q3) but must clear both a \|ρ\| ≥ 0.9 trend and a minimum end-to-end effect size, with a 3× continuity cap. | Step 9 of §8 budgets explicit iteration. The always-on 1-macro × 5-step probe gives a fast inner loop; the full `[.slow]` grid runs only at the end. The `static_assert`ed exclusion of `ModCurve::Stepped` removes the one curve that fails continuity by construction. |
| R9 | **Wall clock (SC-020).** The criteria mandate > 800 s of audio; the composed chain runs ~4.9× real time. | The always-on/`[.slow]` split is fixed **in the spec**, not discovered at implementation time. Measure and record the actual always-on figure in `compliance.md`; a demotion is recorded, never silent. |
| R10 | **`AtmosphereEngine::silence()` is a 10 ms ramp that then latches.** It is not an immediate clear: `runState_ = RunState::Silencing` (`atmosphere_engine.h:644-650`), then `kSilenceRampMs = 10.0f` (`:278`) of grain bed and freeze drone scaled by a per-sample decay (`:2237-2242`) — 480 samples @ 48 kHz — and once latched, `reset()` is the ONLY re-entry (`:641-643`, *"There is no resume()"*). Two hazards: a voice left latched is permanently mute, and a `silence()` believed to be instantaneous is audible for 10× the voice's own ramp. | **`SeraphisVoice::silence()` does not call it at all** — it calls `atmos_.reset()` (§1 D3, V-14), so there is no latch to escape and the hard clear is immediate. `SeraphisEngine::silence()` (FR-055) still does per-voice `silence()` then `reset()`, and FR-047's order is enforced in one place, the engine's steal path. `SeraphisVoice_SilenceHardClears` asserts the ≤ 1e-5 window from `silenceRampSamples_` onward — which only passes with the `reset()` form — and `SeraphisEngine_ResetAndSilence` proves a note after `silence()` still sounds. |
| R11 | **Memory at 96 kHz.** 67.2 MB of capture ring at `kMaxVoices = 16` (Edge Case 17), all committed in `prepare` because FR-041 prepares all slots. | Accepted and priced; asserted in `SeraphisVoice_ShipsFourSecondCapture` via `getCaptureCapacitySamples()`. The shared-ring alternative was rejected by RQ-2. |
| R12 | **`std::sort` in `collectHeldPartials` on an RT path.** | It runs once per note-on inside a control step, never per sample; introsort/heapsort allocate nothing and the comparator is `noexcept`. Bounded at 64 elements. If a reviewer objects, the fallback is an explicit O(64×32) selection with identical output. |
| R13 | **Voice teardown is allocation-free but *large*.** `SeraphisVoice::reset()`/`resetForSteal()`/`silence()` all reach `AtmosphereEngine::reset()` → `capture_.reset()`, a `std::fill` over the whole stereo capture ring (`atmosphere_engine.h:527`; `rolling_capture_buffer.h:96-99`, capacity `:86-87`) = **2 MiB per teardown** at the shipped `captureSeconds = 4.0f` (262 144 samples × 2 ch × 4 B), plus the blur/freeze fills (`:571-572`, `:589-590`). It runs on the audio thread on every steal (§3.6), and `SeraphisEngine::silence()`/`reset()` do it 16× = **32 MiB @ 48 kHz, 64 MiB @ 96 kHz**. "Allocation-free" was the only property the earlier draft asserted; the work was never priced. | Three mitigations, all stated rather than assumed. (1) The FR-072 non-finite path no longer resets inline in the per-sample loop — it masks the voice and `runPreRenderControlStep` services at most `kResetsPerControlChunk = 1` (§3.4), bounding that path at one 2 MiB clear per 1.33 ms. (2) SC-003 times it: per-teardown µs and worst single-block wall time, `REQUIRE`d inside the 512-sample budget, recorded in `compliance.md` with the K-note-ons-per-block at which it is exceeded (§6.2). (3) `SeraphisEngine::silence()`/`reset()` are **not** audio-thread operations in any Phase 7 or Phase 8 flow (host reset / transport stop), and that is stated at their declarations. **If SC-003's clause fails,** the escape hatch is a cheap atmosphere re-entry that resets cursors and grains without zeroing the capture ring — a Phase 5 change, out of Phase 7's scope, so it would be raised rather than improvised. |
| R14 | **Freeze fan-out FFT burst (FR-030a).** All 16 capture rings fill from the same `reset()`, so `capture_.getAvailableSamples() >= need` (`atmosphere_engine.h:915`) flips for every voice on the *same* control chunk. An un-staggered retry would land 16 × 2 = 32 FFT(2048) inside one 1.33 ms chunk; `setAtmosphereFreeze(true)` calling `captureFreeze()` on all 16 slots inline would do the same inside one caller call. | §3.4 step 2 services **one** pending voice per control chunk and §3.8 arms without capturing, so the worst case is 2 FFT(2048) per 1.33 ms chunk and the fan-out spreads over ≥ 16 chunks (≥ 21 ms). FR-030a's observable is "after ≥ `captureSeconds` (≥ 4 s) of render every voice is captured", which 21 ms cannot threaten. Worst-case per-chunk cost measured and recorded in `compliance.md` alongside SC-001. |

---

## §10 Deviations from the spec, recorded

| # | Deviation | Justification |
|---|---|---|
| **V-1** | **FR-013's voice scratch AND FR-051's engine stereo bus are `std::array<float, kControlChunkSamples>` (64), not `std::array<float, kMaxBlockSamples>` (2048).** | D1 makes every sub-component call exactly one control chunk, so a 2048-entry buffer would have 1984 permanently unused entries. FR-013's intent — "fixed-size `std::array`, never locals, never heap containers", which is what SC-008's zero-hit grep tests — is met strictly more cheaply: **2 KB/voice instead of 49 KB/voice, 32 KB instead of 786 KB across `kMaxVoices = 16`**. The **same deviation is taken in the engine** and is recorded here rather than left as an unexplained mismatch: spec FR-004 says "`SeraphisEngine` declares the identical constant and clamps the same way (FR-051's stereo bus is sized from it)" and SC-008 repeats "FR-013 sizes every scratch buffer — and FR-051's stereo bus — as `std::array` against it", but §3.2 declares `busL_`/`busR_`/`vL_`/`vR_` at 64. The justification is identical — §3.4 slices on the same absolute 64-sample grid and never accumulates more than one chunk before writing out — and the arithmetic is 4 × 64 × 4 B = **1 KB instead of 32 KB** for the engine. `kMaxBlockSamples = 2048` is retained as `SeraphisVoiceConfig::maxBlockSamples`'s clamp (FR-004) and as the `TruePeakLimiter::prepare` block size. **Flagged for the phase owner.** |
| **V-2** | **FR-044's "evaluated once per block after rendering" becomes "once per absolute control chunk after rendering".** | Per-block evaluation makes retirement timing depend on the caller's partition. Per-chunk is partition-invariant and makes SC-012's state trace deterministic. The audible difference is nil — the voice is below −100 dBFS at that point. **"After rendering" is now literally true**: §3.4 splits the control step into `runPreRenderControlStep()` (sum gain, freeze retry, non-finite recovery) and `runPostRenderControlStep()` (bloom collection, deferred retirement), and runs the second after the slice that completes a chunk. The earlier single-step form put all five steps in front of the render, which made this row's own wording false and shifted FR-044/SC-012's state trace by one chunk. |
| **V-3** | **FR-034's ramp is a carried decaying tail added to the voice's next `rampSamples` samples, not a fade rendered inside `silence()`.** FR-034's hard-clear **is** performed by `silence()` as written, for all six sub-components including the atmosphere (V-14 records why that one uses `reset()`). There are **two** reset entry points: `reset()` (public, FR-005-exact, clears the armed tail) and `resetForSteal()` (used only by §3.6's FR-047 teardown, preserves it). | `silence()` is called between blocks, so there are no samples for it to fade (D3). The carry removes exactly the discontinuity that exists — the step from the stolen voice's last sample to the new note's zero — and completes inside one control chunk, which is why FR-034 pins the ramp shorter than 64 samples. **The split into two reset entry points replaces an earlier design in which `reset()` unconditionally preserved the tail as "the single exception to FR-005".** That version was internally contradictory: FR-055's `SeraphisEngine::silence()` is per-voice `silence()` then `reset()` with **no** following `noteOn()`, so every voice would end holding a live armed tail; §6.1's FR-055 assertion ("the next block is exactly 0 for every sample") would fail by construction; and because the tail is emitted whenever the voice *next* renders, a stale pre-silence sample would be summed into an arbitrarily-later note's first samples — a click and a determinism hazard for SC-005. With the split there is **no** exception to FR-005: `reset()` is exactly post-`prepare`. **Flagged for the phase owner.** |
| **V-4** | **`SeraphisMacroMatrix` is a `friend` of `SeraphisEngine`.** | `apply()` needs non-const voice access; FR-085 deliberately keeps `getVoice(i)` const (Q4's reason for putting the freeze triggers on the engine). Friendship gives the matrix write access without widening the public surface. |
| **V-5** | **Two new type names beyond the spec's New-components table: `SeraphisMacroTarget`, `SeraphisMacroRow`; plus class-scoped `SeraphisVoice::EnvelopeMode` and `SeraphisEngine::BloomEvents`.** | They are the concrete shape of FR-058's row and FR-070's lifecycle hook. All four ODR-swept clean this session (§0.2). |
| **V-6** | **FR-081's "register `seraphis_chain.h` in `tests/test_helpers/CMakeLists.txt`" is a no-op.** | That file declares an INTERFACE library with an include directory and enumerates no headers. Recorded so no one invents an edit. |
| **V-7** | **~~FR-027's equivalence holds at multiples of 64 only.~~ WITHDRAWN — it holds at every `n`.** | The earlier draft gave `advanceLifeOnly` no clock at all, which forced either a dropped sub-64 remainder or state the plan never specified. §2.8 now runs `advanceLifeOnly` on the **same** `carryAvail_`/`carryRead_` clock as `processStereoBlock` (consume-and-discard, refilling with a life-only chunk step), and §3.4 gives every slot exactly one of the two calls per slice, so the two paths stay in lockstep and the equivalence is exact for any `n`. The test asserts at `n ∈ {1, 7, 64, 65, 512}`. No deviation remains; the row is kept so the change is visible to anyone who read the earlier draft. |
| **V-8** | **A fifth const sub-component accessor, `orbit()`, beyond FR-085's enumerated list** (which names `cloud`, `morph`, `body`, `atmos` only). | SC-010 clause 1 requires reading back `OrbitModulator::getDepth`/`getRate`/`getCoupling`/`getGrowth` (`orbit_modulator.h:189-192`) "reached through `SeraphisVoice`'s forwarders", and the voice's public surface otherwise exposes only `getSpatialAzimuth()`/`getSpatialWidthPercent()`. Without `orbit()` the four FR-019 spatial rows — including the zero-travel fix `setSpatialDepth = 0.35` — have **no reachable read-back**, i.e. the criterion is unsatisfiable as written. `orbit()` matches the four existing accessors exactly (const reference, `[[nodiscard]]`, `noexcept`). **FR-085's list in the spec should gain it.** |
| **V-9** | **`getVoiceAllocationSerial(std::size_t)` on `SeraphisEngine`, beyond FR-085's enumerated list.** | FR-045 step 4's "older allocator timestamp" is **unreachable**: `timestamp` is a member of `VoiceAllocator`'s private internal voice struct (`voice_allocator.h:483`, `private:` at `:471`) and the public surface is `getVoiceNote` `:406` / `getVoiceState` `:424` / `getVoiceFrequency` `:446` only. Rather than silently substituting "lower voice index" — which is **not** equivalent, since the allocator's `Oldest` walk ranks by timestamp (`:575-576`) and only falls back to index when timestamps tie — the engine tracks allocation order itself (`voiceSerial_`, §3.2/§3.6.1), which satisfies FR-045 step 4 as written. The accessor exists so SC-011 clause 5 can assert it. |
| **V-10** | **`SeraphisChainScript::Event` is timestamped in SECONDS, and `renderSeraphisChain` sub-divides each caller block at event boundaries** (§5 rules 1 and 2). | Neither is optional. (a) Block-granular dispatch quantises an event at sample S to `ceil(S/B)·B`, which agrees across SC-014's six partitions only for S a multiple of 1 863 680 samples (≈ 38.8 s @ 48 kHz) — longer than SC-014's 10 s render — so **SC-014 could not pass with any non-trivial script**. `SeraphisEngine::noteOn`/`noteOff` have no sample-offset parameter (§3.1); sub-division is how sample-accurate offsets are delivered without adding one, and it is what Phase 8's event loop does anyway. (b) A sample-denominated script is a different piece of music at 44.1 / 48 / 96 kHz, so SC-013 would not be rendering "the same note script". |
| **V-11** | **`SeraphisVoice::hasRenderedSinceNoteOn()` and `SeraphisVoice::setVoiceWidthBasePercent()`/`getVoiceWidthBasePercent()`, beyond FR-085's and FR-030's enumerated surfaces.** | `hasRenderedSinceNoteOn()` is D4 rule 2's gate: it is the only observable proof that `cloud_.updateControl` has consumed `freqDirty_` (`harmonic_cloud.h:402` sets it, `:1656-1661` clears it), and without it FR-071's snapshot is provably one control chunk stale and SC-017's 0.1-cent bound fails. `setVoiceWidthBasePercent` is FR-062's `VoiceWidth` target: §2.1 exposed **no** width setter, and §2.6 overwrites `ms_.setWidth` once per control chunk, so the macro row named a target that did not exist and would have been inert even if it had (§4.1.0). Both are `noexcept`, allocation-free, and match the shape of the surrounding accessors. |
| **V-12** | **The allocator's retrigger-path `Steal` event is IGNORED, not mapped to FR-047's teardown.** | `VoiceAllocator::noteOn` routes an already-sounding note into `retriggerNote` (`voice_allocator.h:239-242`), which emits `Steal` (old note, `:846-853`) then `NoteOn` (new note, same slot, `:865-872`) with no pool saturation involved. Mapping that `Steal` to FR-047 would `silence()` + `resetForSteal()` a live voice on every ordinary retrigger, call `voices_[i].noteOn()` twice in one dispatch (first at the OLD frequency), and bump `voiceSerial_[i]` twice — contradicting Clarification Q8 ("ordinary retriggers on live voices use a plain `noteOn` with legato continuation"), destroying FR-020's `RetriggerMode::Legato` continuation and the RA-2 tail, and corrupting the FR-045 step 4 key SC-011 clause 5 asserts. Provenance is established engine-side before the allocator call (§3.6), so the FR-047 teardown fires only on the engine-initiated steal and the orphaned post-shrink slot — exactly the two cases Q8 names. |
| **V-13** | **The `NoteOn` teardown predicate is the engine-owned `orphanTail_` bitmask, not `allocator_.getVoiceState(i) == Idle`.** | The allocator-state conjunct can never be true at dispatch time: `allocateNote` stores `VoiceState::Active` the moment `findIdleVoice()` returns the slot (`voice_allocator.h:933-935`, *"Mark as active temporarily…"*) and pushes the `NoteOn` only afterwards (`:1062`); `retriggerNote` does the same (`:855-860` before `:865`). The orphaned post-shrink path Q8 explicitly requires was therefore dead code and §6.1's FR-042/FR-043 assertion about it failed. `!isFinished()` alone is not a substitute — it is true for any live retrigger target, which re-introduces V-12's defect. `orphanTail_` is written only by §3.6.2's shrink handler and cleared on retirement or teardown. |
| **V-14** | **`SeraphisVoice::silence()` hard-clears the atmosphere with `atmos_.reset()`, not `atmos_.silence()`.** | FR-034 says "hard-clear every sub-component"; `AtmosphereEngine::silence()` is not one. It sets `runState_ = RunState::Silencing` (`atmosphere_engine.h:644-650`) and keeps rendering the grain bed and freeze drone under a linear per-sample decay (`:2237-2242`) for `kSilenceRampMs = 10.0f` (`:278`) = 480 samples @ 48 kHz — **ten times** `SeraphisVoice::kSilenceRampMs`, with the capture ring and in-flight grains untouched and the shipped `setLevel(0.5f)` still applied. §6.1's FR-034 window (`[silenceRampSamples_, 512)` ≤ 1e-5) is unsatisfiable with it. `reset()` is the class's only immediate clear and already the documented single re-entry out of the latch (R10, `:641-643`). It also makes the engine's `silence(); reset*()` pair non-redundant instead of relying on the following `reset()` to do the clearing `silence()` claimed. Cost is R13's 2 MiB capture-ring `std::fill`, which every steal already pays. |
| **V-15** | **`quiescentChunks_` is seeded to `kQuiescentChunksToRetire`, so a never-rendered voice is `isFinished()` immediately.** | The counter only advances inside `renderOneChunk`/`advanceOneChunkLifeOnly`, so a 0 seed makes `isFinished()` false for every slot after `prepare()`/`reset()`/`silence()`; both branches of §3.4's `isRendering(v)` then return true and all 16 slots take the full audio path for 4 control chunks (5.33 ms) at every transport start — ~2× SC-001's worst case, unbudgeted against R7's 1.4 points, contradicting FR-041 and FR-051, and detected by nothing (SC-016 passes on exact zeros, SC-001's best-of-8 warms past the window). The counter's meaning ("consecutive chunks below threshold") is trivially satisfied by a voice that has produced nothing. |

---

## §11 Review notes

### 11.1 Second review round (22 issues) — nothing rejected, nothing relaxed

All 22 issues from the second review were applied. Several were raised twice from different lenses; each
is resolved once, at the place named below.

| Issue (severity) | Resolution | Where |
|---|---|---|
| Bloom snapshot reads stale partials — wrong point in the chunk loop (**blocker**) | Control step split into `runPreRenderControlStep()` (steps 1–3) and `runPostRenderControlStep()` (steps 4–5); the latter runs after the slice that completes a chunk. Snapshot additionally gated on `hasRenderedSinceNoteOn()`. §10 V-2's "after rendering" wording is now literally true. SC-017 gains a **staleness positive control** that fails on the old ordering. | §1 D4, §3.4, §6.2 SC-017, V-2, V-11 |
| Bloom snapshot stale via the idle-slot carry FIFO (**major**) | `noteOn` discards the carry **iff `carryIsLifeOnly_`** — fixing the ≤ 63-sample onset delay against FR-015 without dropping a live retrigger's real audio (which would create the click SC-004 measures). D4 rule 2 still covers the retrigger case. | §1 D4 rule 3, §2.2, §2.9 |
| SC-004 Growth clause unsatisfiable **and** inert (**blocker**) | Replaced with the spec's own primary wording — a standalone reference `GrowthEnvelope` advanced alongside the voice, compared sample-by-sample at margin 1e-4. Threshold secondary **re-derived from the real curve** (τ = 0.9085 → "last 10 %"), not relaxed to fit. The arithmetic (`kSteepness = 10`, `growth_envelope.h:18-26`, `:102`) is on the record. | §6.1 FR-020/021/022, §6.2 SC-004 |
| `Steal` fires on ordinary same-note retriggers (**blocker**) | Dispatch table split by provenance; retrigger detected up front with the allocator's own predicate (`getVoiceState`/`getVoiceNote`). The retrigger `Steal` is ignored — one `noteOn`, one serial bump, no teardown. New mandatory `SeraphisEngine_NoteDispatch` clause. | §3.6, §3.6.0, §6.1 FR-042/043, V-12 |
| `NoteOn` teardown predicate is dead code (**blocker**) | Predicate keyed on the engine-owned `orphanTail_` bitmask, written only by §3.6.2's shrink handler. `!isFinished()` alone explicitly rejected as a substitute (it re-introduces the retrigger blocker). | §3.2, §3.6, §3.6.2, V-13 |
| `VoiceWidth` macro target has no design element (**major**) | Option (b) taken: macro-owned width **centre** (`setVoiceWidthBasePercent`, `widthBase_`/`widthSpan_`), §2.6 becomes `clamp(widthBase_ + y·widthSpan_, 50, 150)`. Option (a) (re-route to `ContinuousBody::setWidth`) rejected — it changes which axis FR-062 names. SC-009's Bloom gets an isolating observable. | §2.1, §2.2, §2.6, §4.1.0, §6.2 SC-009, V-11 |
| `bloomNoteOff` unreachable on a steal (**major**) | `bloomOffMask_ |= (1u << v)` moved into `freeChosenVictimSlot()` step 4; §3.6's `Steal` row annotated unreachable-by-construction with the RA-4 citation. New SC-017 steal clause. | §3.6, §3.6.1, §6.2 SC-017 |
| FR-059 idempotence asserted nowhere (**major**) | Per-target early-out **audit** added, with the two missing guards named; `applyStage` gains an equality guard. New non-neutral idempotence clause on `SeraphisMacroMatrix_NeutralIsInert`. | §2.3 step 7, §4.4.1, §6.1 FR-056…060 |
| `atmos_.silence()` is not a hard clear (**major**, raised twice) | `SeraphisVoice::silence()` calls `atmos_.reset()`. Option (a) taken; option (b) (widening the FR-034 test window to 480 samples) rejected as relaxing a criterion to fit an implementation. R10 rewritten with the correct 10 ms figure and citations. | §1 D3, §6.1 FR-034, R10, V-14 |
| Anti-click tail armed from the wrong sample (**major**) | `lastOutL_`/`lastOutR_` captured at **serve** time inside `processStereoBlock`; assignment deleted from §2.5 step 10; `advanceOneChunkLifeOnly` zeroes them. | §1 D1, §1 D3, §2.5, §2.8 |
| `sizeof(SeraphisEngine)` unmeasured, ~750 KB on a 1 MiB stack (**major**) | §6.3 makes heap allocation a rule for every `SeraphisEngine` in every TU; §8 step 5 records the measured size with a `static_assert`. Sub-component arithmetic transcribed. | §3.2, §6.3, §8 step 5 |
| Never-rendered voices take the full audio path (**minor**, raised twice) | `quiescentChunks_` seeded to `kQuiescentChunksToRetire` in the member initialiser and both reset entry points. The `hasSounded_` alternative rejected (it would couple FR-031's gate to FR-032's predicate). | §2.2, §2.7, §3.4, V-15 |
| Test descriptions mutate through const accessors (**minor**) | FR-010 row and SC-009's Dissolve metric restated through the FR-030 forwarders, with the note that the matrix must not be applied on the zeroed arm. | §6.1 FR-010, §6.2 SC-009 |
| V-1 doesn't record the engine bus deviation (**minor**) | V-1 extended to cover FR-051's stereo bus with the same justification and its own arithmetic. | V-1, §3.2 |
| Unguarded fade-tail decrement (**minor**) | Guarded form written out in D3, with the 48-vs-64 arithmetic that makes the guard necessary. | §1 D3 |
| Defaulted move members are defined as deleted (**minor**, raised twice) | Moves explicitly `= delete`d with the `continuous_body.h:647-648` citation and the contrast against the three sub-components that *do* declare moves. | §2.1 |
| `setVoiceCount` discards a `[[nodiscard]]` (**minor**) | `static_cast<void>(...)` at the prepare-time call site; the spec's Existing-components table return type flagged for correction. | §3.3, §11.2 |
| `EnvCurve` attributed to the wrong header (**minor**) | Corrected to `core/env_curve.h:24`; `core/env_curve.h` now listed explicitly in §0.1's include direction rather than relied on transitively. | §0.1, §2.1 |

### 11.2 First review round (27 issues)

**No issue from the review was rejected.** All 27 were applied. Six of them offered alternative
resolutions; this section records which branch was taken and why, so the choice is not re-litigated at
implementation time. The three duplicate pairs (FR-055/D3 raised three times; SC-014 event timing raised
twice) are resolved once each, in §1 D3 / §10 V-3 and in §5 / §10 V-10 respectively.

| Issue | Alternatives offered | Taken | Why |
|---|---|---|---|
| FR-055 vs D3 (`silence()` tail) | (a) engine `silence()` clears the tail and the "exactly 0" assertion stands; (b) relax the assertion to `≤ kTailSilenceThreshold` over the ramp | **(a)**, generalised into two reset entry points (§1 D3) | (b) weakens a criterion to fit an implementation. Splitting `reset()` / `resetForSteal()` keeps the assertion at "exactly 0", removes the FR-005 exception entirely, and kills the stale-tail-into-a-future-note determinism hazard that (b) would leave in place. |
| FR-034 hard-clear | (a) `silence()` hard-clears as FR-034 says; (b) amend FR-034 so the hard-clear is `reset()`'s job | **(a)** (§1 D3) | (b) is a spec relaxation with no forcing evidence — the sub-components all expose a public `reset()` (`harmonic_cloud.h:313`, `spectral_morph_engine.h:249`, `continuous_body.h:766`, `multi_stage_envelope.h:79`, `growth_envelope.h:129`), so FR-034 is implementable as written. Only the atmosphere is exempt, and its exemption is R10's documented latch, already recorded. |
| SC-016 loop bound | (a) render loop runs `v < kMaxVoices`; (b) amend SC-016 to "every voice below the current polyphony" | **(a)** (§3.2, §3.4) | (b) is the quiet scope reduction SC-016 exists to prevent. (a) also matches FR-041's "prepare all 16" and roadmap Key Design Decision 1, makes the loop bound a compile-time constant (removing the `renderingHigh_` out-of-bounds hazard), and aligns §3.4 with §3.8, which already iterated `kMaxVoices`. Cost is one accumulator walk per spare slot per slice. |
| FR-045 step 4 tie-break | (a) record a deviation and amend the FR to "lower voice index"; (b) track allocation order engine-side | **(b)** (§3.2, §3.6.1, §10 V-9) | (a) weakens a normative FR step on the strength of a missing accessor. (b) satisfies FR-045 as written for the cost of one `std::array<std::uint64_t, 16>` and one counter, and gives SC-011 clause 5 something to assert. |
| SC-014 event timing | (a) pin every script event to sample 0; (b) sub-divide blocks at event boundaries | **(b)** (§5 rule 1) | (a) narrows SC-014 to a case with no mid-render steals or note-offs — exactly the events most likely to break partition invariance. (b) needs no engine API change, is what Phase 8's host event loop does with `sampleOffset` anyway, and leaves SC-014 testing what it was written to test. |
| SC-001 regression factor | keep 1.5 (and name the lever) or choose a new number | **1.15, with the arithmetic on the table** (§6.2.1) | 1.5 cannot compile against a 25 % ceiling at RA-1's predicted baseline (32.1 % > 25 %), and 1.05 is not a gate. 1.15 is the widest band that fits, stated together with the 21.74 % admissible-baseline ceiling and R7's levers, so a failure at §8 step 10 is a known outcome rather than a discovery. |

### 11.3 Consequences for the spec

These the plan cannot edit; the phase owner should carry them across. None widens the shipped behaviour —
each makes an existing criterion satisfiable or corrects a transcription error.

1. **FR-085's enumerated introspection list** should gain `SeraphisVoice::orbit()` (V-8),
   `SeraphisEngine::getVoiceAllocationSerial()` (V-9) and `SeraphisVoice::hasRenderedSinceNoteOn()`
   (V-11). Without them SC-010 clause 1, SC-011 clause 5 and SC-017 respectively are unsatisfiable.
2. **FR-030's forwarder list** should gain `SeraphisVoice::setVoiceWidthBasePercent()` /
   `getVoiceWidthBasePercent()` (V-11) — FR-062's `VoiceWidth` target has no other landing point (§4.1.0).
3. **SC-004's Growth clause** ("≥ 0.99 only in the last 5 % of the duration") is unsatisfiable against
   `GrowthEnvelope` as shipped and inert against the defect it targets; it should be restated as the
   reference-envelope comparison, with the threshold secondary at the derived τ ≈ 0.909 ("last 10 %").
   The same wording appears in the FR-020/021/022 acceptance text.
4. **The Existing-components table** records `void setVoiceCount(size_t)`; the real signature is
   `[[nodiscard]] std::span<const VoiceEvent> setVoiceCount(size_t) noexcept` (`voice_allocator.h:326`).
5. **The Existing-components entry for `AtmosphereEngine::silence()`** should state that it is a 10 ms
   ramp-then-latch (`kSilenceRampMs = 10.0f`, `atmosphere_engine.h:278`), not an immediate clear —
   FR-034's "hard-clear every sub-component" is only implementable for that component via `reset()` (V-14).
6. **Clarification Q8** should note explicitly that the shipped `VoiceAllocator` emits a `Steal` event on
   the *ordinary retrigger* path (`voice_allocator.h:239-242`, `:846-853`), so "ordinary retriggers use a
   plain `noteOn`" requires the consumer to discriminate by provenance (V-12).

## §12 Phase-owner resolutions (recorded before build stage, 2026-07-30)

All fourteen open items from the plan/tasks round, resolved. None changes a grill decision;
item 14 is resolved AGAINST auto-remediation per the standing rule that CPU-budget walls are
phase-owner decisions.

1. **V-1 CONFIRMED** — voice scratch and engine bus ship as `std::array<float, kControlChunkSamples>`
   (64), not 2048. FR-013's intent (fixed-size `std::array`, no heap, SC-008 grep zero) is fully met;
   the 2048 figure was a means, not an end.
2. **V-3 CONFIRMED** — `silence()`'s 1 ms teardown ships as the armed decaying tail captured from
   `lastOut`, added to the voice's next 48 samples; `reset()` clears it (FR-005-exact),
   `resetForSteal()` preserves it (FR-047 only). The two-entry-point split is confirmed.
3. **V-2 CONFIRMED** — `voiceFinished` evaluation moves to the absolute 64-sample control grid
   (partition-invariant retirement; audible difference nil below −100 dBFS).
4. **D4/D5 CONFIRMED** — `SeraphisEngine::consumeBloomEvents()` ships as new surface, authorised by
   FR-070's per-voice note-lifecycle wording; bloom events issue one control chunk late by necessity
   (partial frequencies are stale inside `noteOn`).
5. **CONFIRMED (restates Clarification Q3)** — SC-009 per-row amounts/curves stay implementation
   tuning against the minimum-effect-size table.
6. **CMake-in-T001 CONFIRMED** — registration cannot be deferred to the final group because the
   enumerated source list would silently drop every phase TU; T016's verification stands.
7. **(= item 1)** — 64-sample form confirmed for both FR-013 and FR-051 arrays.
8. **(= item 2)** — `reset()` / `resetForSteal()` split confirmed.
9. **Spec citation fixes CONFIRMED** — FR-085 gains `orbit()`, `hasRenderedSinceNoteOn()`,
   `getVoiceAllocationSerial()`; FR-030 gains `setVoiceWidthBasePercent()`/`getVoiceWidthBasePercent()`;
   the Existing-components `VoiceAllocator::setVoiceCount` row corrected to
   `[[nodiscard]] std::span<const VoiceEvent>`. Mechanical; encode during build.
10. **SC-004 Growth clause CONFIRMED as restated** — the literal ">= 0.99 only in the last 5 %" is
    unsatisfiable against the shipped GrowthEnvelope (tau = 0.9085) and inert against its target
    defect; the reference-GrowthEnvelope comparison plus last-10 % secondary is the stronger test.
    Spec text to be restated accordingly during build.
11. **SC-010 c3 placement CONFIRMED** — `seraphis_macro_test.cpp`.
12. **FR-053 THD bound CONFIRMED as measured-then-pinned** — record the T005 measurement, pin it with
    margin, and verify the pinned bound still fails on an injected defect (house rule for measured
    gates).
13. **(= item 5).**
14. **SC-001 static_assert failure: DO NOT auto-pull the voice-count lever.** If
    `kBaselineNs * 1.15 <= kReferenceNs` fails, the build HALTS and the choice between re-deriving
    the shipped voice count (RQ-1) and reducing composition cost goes to the phase owner — CPU-budget
    walls are phase-owner decisions (Phase 5 and Phase 6 precedent). The tasks' encoded remediation
    list is a menu for that escalation, not an authorisation.
