# Implementation Plan: Seraphis Phase 4 — Continuous Resonant Body

**Spec:** `specs/seraphis-phase4-continuous-body/spec.md`
**Roadmap:** `specs/Seraphis-roadmap.md` Part A → Phase 4 (lines 198–222)
**Deliverables:** one new Layer 3 header (`dsp/include/krate/dsp/systems/continuous_body.h`), two
strictly-additive amendments to shared Layer 2 headers (`processors/waveguide_string.h`,
`processors/diffusion_network.h`), and five new test TUs under `dsp/tests/unit/systems/`.
**Plugin work:** none.

Every signature, constant and line number below was read from the working tree in the session that
produced this document (branch `feat/seraphis-phase1-life-modulators`). Where the plan diverges from the
spec, or where a spec number was found to be unachievable as written, it is recorded in §17
(Recorded deviations) and §20 (Open items) — never silently. §21.1 lists the lines re-read during the
review-revision pass and §22 records how each review finding was disposed of.

---

## 0. Reused components — verified signatures (read this session)

| Component | Header | Signatures / facts this plan depends on |
|---|---|---|
| `ModalResonatorBank` | `processors/modal_resonator_bank.h` | `class … : public IResonator` (`:71`); `kMaxModes = 96` (`:73`); `struct DampingLaw { float b1; float b3; }` (`:81-89`); `void setModes(const float*, const float*, int, DampingLaw, float stretch, float scatter) noexcept` (`:228-244`, **memsets `sinState_`/`cosState_` at `:238-239`**); `void updateModes(…, DampingLaw, …) noexcept` (`:264-275`, state-preserving); `void processBlock(const float*, float*, int) noexcept` (`:355-369`); `void setOutputGain(float)` (`:140`); `void setOutputSoftClipThreshold(float)` (`:150-153`); `float getInputGainSum() const` (`:168-171`); `float getModeFrequency(int) const` (`:448-455`); `int getNumModes() const` (`:458`); `void silence()` (`:517-523`); `void prepare(double)` (`:180-189`) |
| … its maths | same | `b1 = max(b1In, 1/5)` (`:685`), `b3 = max(b3In, 0)` — **no upper clamp** (`:686`); `B = stretch²·0.01`, `C = scatter·0.10` (`:702-703`); stretch warp `f_w = f_k·√(1+B(k+1)²)` (`:726`); scatter warp `f_w ×= (1 + C·sin(k·kScatterD))`, `kScatterD = π(φ−1)` (`:577-578`, `:729`) — deterministic, no RNG; Nyquist cull `f_w ≥ 0.49·fs` (`:567`, `:732-738`); amplitude cull `< 1e-4` (`:568`, `:710`); `decayRate_k = b1 + b3·f_w²`, `R_k = exp(−decayRate_k/fs)` (`:742-743`); `eps_k = 2·sin(π f_w/fs)` (`:746`); `gain_k = amp` (`:754`); coupled recursion `s = R(s+εc)+g·u; c = R(c−ε·s); out += s` (`:853-859`); `applyOutputStage` = `×outputGain_` then `softClip(x/t)·t` when `t>0` (`:789-796`); coefficient smoother `kSmoothingTimeMs = 2.0` (`:569`, `:801-809`) |
| `IResonator` | `processors/iresonator.h` | `class IResonator` (`:32`); pure virtuals `prepare(double)` (`:38`), `setFrequency` (`:42`), `setDecay` (`:46`), `setBrightness` (`:50`), `process(float)` (`:55`), `getControlEnergy` (`:59`), `getPerceptualEnergy` (`:63`), `silence()` (`:67`); virtual `getFeedbackVelocity()` default 0 (`:72`) |
| `WaveguideString` | `processors/waveguide_string.h` | `class … : public IResonator` (`:38`); `kMinDelaySamples = 4` (`:44`); `kMinFrequency = 20` (`:49`); `prepare(double)` allocates `1/20 Hz = 50 ms` (`:105-122`); `prepareVoice(uint32_t)` (`:125-128`); `setFrequency` re-targets the log2 smoother only (`:134-140`); `setDecay(float t60)` **clamps to `[0.01, 10.0]`** (`:142-146`); `setBrightness` (`:148-152`); `process(float)` (`:154-218`); `silence()` (`:230-244`); `setStiffness` (`:256-259`); `setPickPosition` (`:262-265`); `noteOn(float, float)` (`:273-448`) |
| … its maths | same | `D = period − 1 − 0.55·dLoss − 0.96·dDisp` (`:321`), clamped into `bridgeDelayFloat_` (`:325`); `computeRho(f0,t60) = 10^(−3/(t60·f0))` (`:476-481`); `computeLossPhaseDelay(f0,sr,S)` (`:486-499`); `computeDispersionPhaseDelay(f0,sr)` (`:595-619`); `configureDispersionFilters(B,f0,sr)` called **only** from `noteOn` (`:299`, `:634-706`); loss filter `rho·[(1−S)x + S·x[n−1]]` (`:197`) with `S = brightness·0.5` (`:168`); `velScale = velocity · excitationGain_` (`:393`) consumed at `:446`; `XorShift32 rng_` (`:750`); `bridgeDelayFloat_` assigned **only** at `:243` (`silence`) and `:325` (`noteOn`) |
| `TimeVaryingCombBank` | `systems/timevar_comb_bank.h` | `class TimeVaryingCombBank` (`:81`) — **no base class**; `kMaxCombs = 8` (`:88`); `kMinFundamental = 20`, `kMaxFundamental = 1000` (`:91`, `:94`); `prepare(double, float maxDelayMs = 50)` (`:154`, impl `:399`); `reset()` (`:162`, impl `:445`); `setNumCombs(size_t)` (`:178`); `setCombFeedback(size_t,float)` (`:197`, clamp to `[-0.9999, 0.9999]` at `:488` via `comb_filter.h:32,:35`); `setCombDamping(size_t,float)` (`:206`); `setTuningMode(Tuning)` (`:226`); `setFundamental(float)` (`:238`, clamp at `:521`); `setSpread(float)` (`:249`); `setModPhaseSpread` (`:285`); `setStereoSpread` (`:311`); `float process(float)` (`:328`, impl `:593`); `void processStereo(float&,float&)` (`:338`, impl `:653`, mono-sums at `:661`); `enum class Tuning{Harmonic,Inharmonic,Custom}` (`:43`); inharmonic delay `1000/(f0·√(1+n·spread))` ms, clamped `[1, maxDelayMs]` (`:789-794`, `:737-741`); per-comb RNG hard-seeded `12345u + i·7919u` in **both** `prepare` (`:429`) and `reset` (`:450`) |
| `DiffusionNetwork` | `processors/diffusion_network.h` | `class DiffusionNetwork` (`:161`); `kNumDiffusionStages = 8` (`:36`); `kAllpassCoeff = 0.618…` (`:39`); `kBaseDelayMs = 3.2` (`:42`); `kMaxModDepthMs = 2.0` (`:45`); `kDelayRatiosL` (`:51-53`, **Σ = 17.777**); `kStereoOffset = 1.127` (`:56`); `prepare(float sampleRate, size_t maxBlockSize)` — **`float` first arg** (`:197-246`); `setSize/Density/Width/ModDepth/ModRate` take **percent** (`:273-305`); `process(const float*, const float*, float*, float*, size_t)` (`:327-403`) — **block entry point only**; bypass when smoothed `size < 0.001` (`:344`); stage skipped when `stageEnable < 0.001` (`:356`); per-stage delay `kBaseDelayMs·size·ratio[i]` on L, `×kStereoOffset` on R (`:366-374`); `std::sin(lfoPhase_ + i·π/4)` **evaluated per stage per sample, unconditionally** (`:362`); LFO phase advances once per sample outside the stage loop (`:398-401`); **no feedback path anywhere** |
| … its defaults | same | `prepare()` ends with `size_ = kDefaultSize` (**50**, `:169`, `:237`) then `reset()` snaps smoothers (`:245`, `:257-264`). `kDefaultDensity = 100` (`:173`), `kDefaultWidth = 100` (`:177`), **`kDefaultModDepth = 0`** (`:181`) |
| `AllpassStage` | same | `prepare(float, float)` (`:92-97`); `process(float input, float delaySamples)` (`:108-132`), single-delay-line Schroeder form, `readAllpass(clampedDelay − 1)` (`:120`) |
| `EnvelopeFollower` | `processors/envelope_follower.h` | `class EnvelopeFollower` (`:82`); `prepare(double, size_t)` (`:106-124`); `float processSample(float)` (`:164-188`) — **advances the one-pole exactly once per call**; `setMode(DetectionMode)` (`:202-216`); `setAttackTime(float)` `[0.1, 500]` (`:220`, `:88-89`); `setReleaseTime(float)` `[1, 5000]` (`:227`, `:90-91`); `getCurrentValue()` (`:192`); `calculateCoefficient(ms) = exp(−2π/(ms·0.001·sampleRate_))` (`:359-365`); `sidechainEnabled_ = false` by default (`:397`-ish member init) |
| `OnePoleSmoother` | `primitives/smoother.h` | `class OnePoleSmoother` (`:134`); `configure(float ms, float sr)` (`:160-164`); `setTarget(float)` — **NaN → 0, Inf → ±1e10** (`:170-181`); `getCurrentValue()` (`:191`); `process()` (`:197-211`); `advanceSamples(size_t)` — **O(1) closed form `target + diff·coeff^N`** (`:243-254`); `snapToTarget()` (`:257`); `snapTo(float)` (`:263`); `kCompletionThreshold = 1e-4` (`:55`) |
| `DelayLine` | `primitives/delay_line.h` | `class DelayLine` (`:57`); `prepare(double, float maxDelaySeconds)` — **allocates** (`:186-198`); `reset()` (`:200-204`); `write(float)` (`:206-209`); `read(size_t d)` = `buffer_[(writeIndex_ − 1 − d) & mask_]` (`:211-219`); `readLinear` (`:221-238`); `readAllpass` (`:266`); `maxDelaySamples()` (`:297`) |
| `OnePoleLP` | `primitives/one_pole.h` | `class OnePoleLP` (`:56`); `prepare(double)` (`:65-71`); `setCutoff(float)` (`:75-78`); `process(float)` — `y = (1−a)x + a·y`, **resets and returns 0 on NaN/Inf** (`:98-117`); `reset()` (`:128`); `a = exp(−2π·fc/fs)` (`:135`-ish `updateCoefficient`) |
| `DCBlocker` | `primitives/dc_blocker.h` | `class DCBlocker` (`:94`); `prepare(double, float cutoffHz = 10.0f)` (`:135`); `reset()` (`:155`); `process(float)` |
| `equalPowerGains` / `crossfadeIncrement` | `core/crossfade_utils.h` | `void equalPowerGains(float, float&, float&)` (`:50-53`); `std::pair<float,float> equalPowerGains(float)` (`:64-66`); `float crossfadeIncrement(float ms, double sr)` (`:89-92`) |
| `Xorshift32` / `deriveStreamSeed` | `core/random.h` | `class Xorshift32` (`:41`), ctor substitutes `kDefaultSeed` for 0 (`:45-46`), `nextFloat()` → `[−1,1]` (`:59-63`), `seed()` (`:73-75`); `constexpr std::uint32_t deriveStreamSeed(std::uint32_t, std::size_t)` (`:102-111`) |
| `softClip` | `core/dsp_utils.h` | `float softClip(float)` (`:105-113`), rational tanh approximation clamped to `[−1,1]` |
| `HarmonicCloud` (idioms only) | `systems/harmonic_cloud.h` | `kMaxPartials = 64` (`:138`); `kControlChunkSamples = 64` (`:144`); `kOutputClamp = 2.0f` (`:174`); `processStereoBlock` (`:878`); pre-prepare silence (`:887-891`); output clamp (`:935-936`). **Not composed** (N-1) |

### 0.1 Verified traps this plan must not fall into

1. **`ModalResonatorBank::applyTransientEmphasis` is on the `processBlock` hot path** (`:359`, `:879-895`)
   and is a *time-varying input gain*: `1 + 4·max(0, d/dt |x|)` with a 5 ms envelope. Membrum needs it;
   a sustained input does not. At steady state the derivative → 0 and the factor → 1, but a 220 Hz sine
   leaves a residual ripple: measured analytically at ≈ **+0.06 dB** at unit amplitude, growing ≈ linearly
   with input amplitude (≈ +0.55 dB for a 10× level step). This is why SC-015's tightness bound is `0.1×`
   and not `0.9×`, and it is a named risk (§18 R-5). It cannot be switched off — there is no setter.
2. **`ModalResonatorBank::process()` (the `IResonator` path, `:492-502`) must never be used.** It calls
   `processSample` → `smoothCoefficients()` *per sample* (`:345-349`) and the *scalar* `processSampleCore`
   (`:814`), bypassing the SIMD kernel entirely. Only `processBlock` reaches
   `processModalBankSampleSIMD` (`:362-364`) and smooths once (`:357`).
3. **`controlEnergy_`/`perceptualEnergy_` are updated only inside `process()`** (`:494-500`). On the
   `processBlock` path they are stale. Nothing in this plan reads them.
4. **`DiffusionNetwork::prepare()` leaves `size_` at 50 %, not 100 %** (`:169`, `:237`, `:245`).
   `ContinuousBody::prepare` must call `setSize()` explicitly *and* the internal 10 ms smoother
   (`kDiffusionSmoothingMs = 10.0f`, `:48`) will glide — so `prepare()` must also settle it, or accept a
   10 ms size ramp after every `prepare`. The plan settles it (§9.3).
5. **`DiffusionNetwork`'s per-stage delay uses the *smoothed* normalised size**, so FR-052's
   `cascadeSec` must be computed from the same normalised `[0,1]` quantity, not from the percent value
   handed to `setSize` (§9.2).
6. **`TimeVaryingCombBank::process`/`processStereo` call `reset()` on a non-finite input**
   (`:598-601`, `:664-669`) — i.e. it destroys its whole ring. FR-038's zero-substitution must happen
   **upstream** in `ContinuousBody` so the comb bank never sees a non-finite sample.
7. **`TimeVaryingCombBank` consumes one `rng_.nextFloat()` per comb per sample unconditionally**
   (`:620`-ish in `process`, `:686`-ish in `processStereo`), from a hard-coded seed. Deterministic, and
   the source of FR-071's documented seed-independence.
8. **`WaveguideString::noteOn` builds two `std::array<float, 4096>` on the stack** (`:405`, `:425`) —
   32 KB of stack frame, on the audio thread, at every Strings material assignment. RT-legal (no
   allocation) but a named risk (§18 R-7).
9. **`OnePoleLP::process` resets to 0 on a non-finite input** (`:104-107`). Same containment as (6).
10. **`WaveguideString::setDecay` clamps to `[0.01, 10.0]` s** (`:144`). Any T60 above 10 s is silently
    truncated; FR-035/FR-036 keep Strings at ≤ 8.0 s so the clamp never binds.
11. **`ModalResonatorBank::setModes` clears state** (`:238-239`); `updateModes` does not (`:273-274`).
    Every retune and every damping change goes through `updateModes`, never `setModes` — and never
    through `updateDampingLaw` (`:280-294`), which **skips `!active_[k]` modes** and would therefore
    permanently miss a mode that `flushSilentModes` (`:383-396`) culled during a quiet passage.

---

## 1. Blocking prerequisites (task T0 — before any header is written)

| Check | Command | Result this session |
|---|---|---|
| `ContinuousBody` free | `grep -rn "class ContinuousBody\b" dsp/ plugins/` | **0** |
| `BodyMaterial` free | same pattern | **0** |
| `MaterialProfile` free | same pattern | **0** |
| `DecayCloud` free | same pattern | **0** |
| `DriveNormalizer` free | same pattern | **0** |
| `EngineSlot` free | same pattern | **0** |
| header does not exist | `ls dsp/include/krate/dsp/systems/continuous_body.h` | **No such file** |
| `retune` not already a `WaveguideString` member | `grep -n "retune" …/waveguide_string.h` | **0** |

Names that are **taken** and must not be used: `Material` (`processors/modal_resonator.h:81`, namespace
scope), `MaterialCoefficients` (`:91`), `BodyMode` (`processors/body_resonance.h:60`),
`ResonatorBank`, `ModalResonator`, `GranularEngine`. Namespace-scope constants that must not be
redeclared (FR-008): `kBodyModeCount`, `kBodyPresetCount`, `kBodyFDNLines`, `kBodyFDNMaxDelay`
(`body_resonance.h:45-54`); `kNumDiffusionStages`, `kAllpassCoeff`, `kBaseDelayMs`, `kMaxModDepthMs`,
`kDiffusionSmoothingMs`, `kDelayRatiosL`, `kStereoOffset` (`diffusion_network.h:36-56`);
`kMinCombCoeff`, `kMaxCombCoeff` (`comb_filter.h:32`, `:35`); `kMaterialPresets`
(`modal_resonator.h:99`). **Every `ContinuousBody` constant is `static constexpr` inside the class.**

Re-run the sweep at implementation time; a name that was free when this plan was written is not
guaranteed free when it is executed.

---

## 2. Amendment A — `WaveguideString::retune(float)` (RA-1, FR-080…FR-084)

**File:** `dsp/include/krate/dsp/processors/waveguide_string.h`. **Layer 2. SHARED DSP** — consumers in
`dsp/`, Innexus **and Membrum** (§16.2). Strictly additive; no existing member changes.

Placement: immediately after `noteOn` (`:448`), inside the public "Note Lifecycle" section, so the
budget expression sits next to the one it mirrors.

```cpp
/// Retune the loop to a new fundamental WITHOUT clearing state or re-exciting.
///
/// Recomputes `bridgeDelayFloat_` with the SAME budget expression noteOn() uses
/// (`D = period - 1 - 0.55*dLoss - 0.96*dDisp`, :321) and the same clamp (:325).
/// Deliberately does NOT touch: nutSideDelay_, bridgeSideDelay_, dcBlocker_,
/// dispersionFilters_, lossState_, the energy followers, frozenStiffness_ or
/// frozenPickPosition_ — the loop keeps ringing through the retune.
///
/// The frozen dispersion cascade is NOT reconfigured (FR-081): reconfiguring
/// biquads mid-ring changes the meaning of their state and clicks. The cost is
/// a small pitch error at large intervals, bounded to +/-5 cents over
/// +/-12 semitones from the noteOn() pitch (SC-009c).
///
/// Inert unless called (FR-084): a build that never calls retune() is
/// functionally identical to the pre-amendment component (SC-014).
///
/// Added by specs/seraphis-phase4-continuous-body (RA-1).
void retune(float f0) noexcept
{
    // FR-082: same guard as noteOn (:275-276).
    if (!prepared_ || f0 < kMinFrequency)
        return;

    const float sr = static_cast<float>(sampleRate_);
    // FR-082: same clamp as setFrequency (:136-137).
    f0 = std::clamp(f0, kMinFrequency, sr * 0.45f);

    // Same in-loop filter state noteOn budgets against (:302-303).
    const float S = brightness_ * 0.5f;
    const float period = sr / f0;
    const float dLoss = computeLossPhaseDelay(f0, sr, S);
    const float dDisp = computeDispersionPhaseDelay(f0, sr);   // reads the FROZEN cascade

    const float D = period - 1.0f - 0.55f * dLoss - 0.96f * dDisp;   // :321
    const float maxD = static_cast<float>(bridgeSideDelay_.maxDelaySamples());
    bridgeDelayFloat_ = std::clamp(D, static_cast<float>(kMinDelaySamples), maxD);  // :325

    // Integer companion kept coherent for the debug accessors (:328-330).
    const auto bridgeN = static_cast<size_t>(std::round(bridgeDelayFloat_));
    bridgeDelaySamples_ = std::clamp(bridgeN, kMinDelaySamples,
                                     bridgeSideDelay_.maxDelaySamples());

    // FR-083: keep the loss-filter path converging on the same pitch.
    // setTarget, NOT snapTo — noteOn snaps (:288), which is right for a new note
    // and wrong for a glide.
    frequency_ = f0;
    frequencySmoother_.setTarget(std::log2(f0));
}
```

**Notes for the implementer.**
- `computeDispersionPhaseDelay` is a **non-static const member** (`:595`); `computeLossPhaseDelay` is
  **static** (`:486`). Both are private and reachable from a member.
- `retune` uses `brightness_` (the stored value, `:150`), not the smoothed one — this is exactly what
  `noteOn` does (`:303`), so the two paths agree.
- Do **not** call `configureDispersionFilters` (FR-081).
- Do **not** touch `totalLoopDelay_` or the `debug*` fields other than `bridgeDelaySamples_`; they are
  note-onset diagnostics.

**Containment test:** `WaveguideString_RetuneIsInert` (§15.2).

---

## 3. Amendment B — `DiffusionNetwork` zero-modulation fast path (RA-4)

**File:** `dsp/include/krate/dsp/processors/diffusion_network.h`. The edit **replaces line 362 only**
(the `std::sin` initialiser) and leaves `:361` untouched.

> **Corrected (review, blocker-adjacent).** An earlier draft of this section said "exactly one line, at
> `:362`" but reproduced `const float stagePhaseOffset = …` inside the snippet. That declaration
> **already exists at `:361`** (read this session:
> `const float stagePhaseOffset = static_cast<float>(i) * (kPi / 4.0f);`), so inserting the snippet
> verbatim is a redefinition in the same scope — a hard compile error in a shared Layer 2 header
> consumed by Iterum, `shimmer_delay.h` and `freeze_mode.h`. The snippet below is the replacement for
> `:362` alone.

Line `:361`, **unchanged**:

```cpp
                const float stagePhaseOffset = static_cast<float>(i) * (kPi / 4.0f);
```

Line `:362`, **replaced by**:

```cpp
                // RA-4 (specs/seraphis-phase4-continuous-body): the sin() below was
                // evaluated per stage per sample UNCONDITIONALLY — 8 transcendentals
                // per sample per instance (~384 k/s at 48 kHz) even at modDepth = 0,
                // which is the default (kDefaultModDepth, :181). The guard is
                // BIT-IDENTICAL, not a behaviour change: lfoValue feeds exactly one
                // expression, `modMs = modDepth * kMaxModDepthMs * lfoValue` (:363),
                // and with modDepth == 0 that product is 0 (or -0) for every finite
                // lfoValue, leaving delayMsL/R (:369, :373) unchanged bit-for-bit
                // (`baseDelayMs = kBaseDelayMs * size` with `size >= 0.001` after the
                // `:344` bypass is strictly positive, so delayMsL/R can never be -0.0f
                // and `+0.0f` vs `-0.0f` is indistinguishable in the sum).
                // The LFO phase accumulator (:398-401) is OUTSIDE this loop and still
                // advances, so a later modDepth > 0 resumes on the same phase.
                const float lfoValue = (modDepth > 0.0f)
                                     ? std::sin(lfoPhase_ + stagePhaseOffset)
                                     : 0.0f;
```

Nothing else in the file changes. `modDepth` is already the smoothed value read at `:337`, so a
`setModDepth(0)` glides through the guard rather than snapping into it: the guard engages only once the
smoother has actually reached 0 (within `kCompletionThreshold = 1e-4`, `smoother.h:55`, `:199-202`).

**Containment tests:** `DiffusionNetwork_ZeroModIsBitIdentical` (§15.2), plus the whole existing
consumer set (§16.2).

---

## 4. `ContinuousBody` — file, layer, includes, constants

### 4.1 File header and includes (FR-001, FR-003)

`dsp/include/krate/dsp/systems/continuous_body.h`, header-only, `namespace Krate { namespace DSP {`.

```cpp
// Layer 3 (systems). Dependencies: Layers 0-3 only; no Layer 4, no plugin header,
// no arch-guarded krate include.
#include <krate/dsp/core/crossfade_utils.h>     // equalPowerGains, crossfadeIncrement
#include <krate/dsp/core/db_utils.h>            // detail::flushDenormal (R-6 cloud feedback write)
#include <krate/dsp/core/math_constants.h>      // kPi, kTwoPi, kHalfPi
#include <krate/dsp/core/random.h>              // Xorshift32, deriveStreamSeed
#include <krate/dsp/primitives/dc_blocker.h>
#include <krate/dsp/primitives/delay_line.h>
#include <krate/dsp/primitives/one_pole.h>
#include <krate/dsp/primitives/smoother.h>
#include <krate/dsp/processors/diffusion_network.h>
#include <krate/dsp/processors/envelope_follower.h>
#include <krate/dsp/processors/modal_resonator_bank.h>
#include <krate/dsp/processors/waveguide_string.h>
#include <krate/dsp/systems/timevar_comb_bank.h>   // SAME LAYER — legal, see below

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>     // std::memcpy for the bit-pattern finiteness check
```

Two include-list corrections against FR-003's "permitted includes are exactly" enumeration, both recorded
in §17 (D-7):

- **`core/dsp_utils.h` is dropped** and is deliberately **absent from the block above** (an earlier draft
  carried the line with a "kept out — see note" comment, which contradicted this paragraph and would have
  shipped the include the prose says to omit). Nothing in this plan calls `softClip`: FR-037's guard is a
  `std::clamp`, and every soft clip on the path lives inside `ModalResonatorBank`/`WaveguideString`.
  A header-only Layer 3 file that pulls `dsp_utils.h` rebuilds a lot of the tree.
- **`core/db_utils.h` is added.** R-6's mitigation mandates an explicit `flushDenormal` on the cloud
  feedback write, and `detail::flushDenormal` is declared in `core/db_utils.h:168` (its `namespace detail`
  spans `:39-180`). It would otherwise resolve only *transitively* through `primitives/smoother.h:28`,
  which is exactly the fragility FR-003's exhaustive enumeration exists to prevent — it breaks the moment
  `smoother.h`'s include set is trimmed. Layer 0, so no layer consequence.
  Note that `db_utils.h:168`'s `flushDenormal` returns NaN/Inf **unchanged**
  (`(x > -k && x < k) ? 0.0f : x` — both comparisons are false for a NaN), so it is a denormal guard only
  and is never a finiteness guard. §7.8 relies on that distinction.

**Same-layer include is legal.** `tools/lint-layers.js:74` flags only
`layerIndex(toLayer) > layerIndex(fromLayer)`, i.e. strictly upward. In-tree precedent:
`systems/poly_synth_engine.h:40-41` includes `systems/voice_allocator.h` and `systems/synth_voice.h`.
`dsp/CLAUDE.md` states the stricter "layers below" wording in prose; **the lint governs** and this file is
written against it. `timevar_comb_bank.h` itself includes only Layers 0–1 (`:22-26`), so no cycle exists.

`node tools/lint-layers.js` and `node tools/lint-arch-guarded-includes.js` must stay clean.

### 4.2 Class-scoped constants (FR-008) — the complete set

```cpp
class ContinuousBody {
public:
    // --- structure -------------------------------------------------------
    static constexpr std::size_t kControlChunkSamples = 64;   // A-5, matches HarmonicCloud:144
    static constexpr int         kModeCountCeiling    = 32;   // A-3 / OQ-2, fixed
    static constexpr std::size_t kNumCombs            = 6;    // FR-013a Chamber
    static constexpr float       kNyquistHeadroomOct  = 1.0f; // FR-043 (configure at 2*f_body)
    static constexpr float       kBankNyquistGuard    = 0.49f;// mirrors modal_resonator_bank.h:567

    // --- ranges and DEFAULTS (FR-009, complete) --------------------------
    // FR-006 requires every float setter to substitute the FR-009 Default when
    // its argument is non-finite, so every Default column value is a named
    // constant here. §6.4 states the uniform setter shape that consumes them.
    static constexpr float kMinNoteHz    = 20.0f;
    static constexpr float kMaxNoteHz    = 8000.0f;
    static constexpr float kDefaultNoteHz = 220.0f;

    static constexpr float kMinResonance = 0.0f;
    static constexpr float kMaxResonance = 1.0f;
    static constexpr float kDefaultResonance = 0.7f;

    static constexpr float kMinDamping = 0.0f;
    static constexpr float kMaxDamping = 1.0f;
    static constexpr float kDefaultDamping = 0.0f;

    static constexpr float kMinKeyTracking = 0.0f;
    static constexpr float kMaxKeyTracking = 1.0f;
    static constexpr float kDefaultKeyTracking = 1.0f;

    static constexpr float kMinUserDrive = 0.0f;
    static constexpr float kMaxUserDrive = 4.0f;
    static constexpr float kDefaultUserDrive = 1.0f;

    static constexpr float kMinMix = 0.0f;
    static constexpr float kMaxMix = 1.0f;
    static constexpr float kDefaultMix = 1.0f;

    static constexpr float kMinCloudMix = 0.0f;
    static constexpr float kMaxCloudMix = 1.0f;
    static constexpr float kDefaultCloudMix = 0.25f;

    static constexpr float kMinCloudDecaySec = 0.1f;
    static constexpr float kMaxCloudDecaySec = 30.0f;
    static constexpr float kDefaultCloudDecaySec = 4.0f;

    static constexpr float kMinCloudSize = 0.0f;
    static constexpr float kMaxCloudSize = 1.0f;
    static constexpr float kDefaultCloudSize = 1.0f;

    static constexpr float kMinCloudDamping = 0.0f;
    static constexpr float kMaxCloudDamping = 1.0f;
    static constexpr float kDefaultCloudDamping = 0.3f;

    static constexpr float kMinWidth = 0.0f;
    static constexpr float kMaxWidth = 1.0f;
    static constexpr float kDefaultWidth = 1.0f;

    static constexpr BodyMaterial kDefaultMaterial = BodyMaterial::Glass;
    static constexpr bool          kDefaultAgcEnabled = true;
    static constexpr bool          kDefaultResonatorBypass = false;
    static constexpr std::uint32_t kDefaultSeed = 1u;

    // --- smoothing times (FR-009) ---------------------------------------
    static constexpr float kPitchSmoothMs      = 20.0f;
    static constexpr float kDriveSmoothMs      = 50.0f;   // log10 domain
    static constexpr float kMixSmoothMs        = 20.0f;
    static constexpr float kCloudSmoothMs      = 50.0f;
    static constexpr float kMaterialCrossfadeMs = 500.0f;
    static constexpr float kSlotReleaseMs      = 10.0f;

    // --- damping / resonance law (FR-035, FR-036) ------------------------
    static constexpr float kResonanceScaleAtZero = 40.0f;
    static constexpr float kMinB1              = 0.23f;   // T60 = 6.91/0.23 = 30.0 s
    static constexpr float kMaxB1              = 30.0f;   // T60 = 0.23 s
    static constexpr float kDampingB3Scale     = 32.0f;
    static constexpr float kMaxCombFeedback    = 0.995f;
    static constexpr float kWgT60Min           = 0.05f;
    static constexpr float kWgT60Max           = 10.0f;   // waveguide_string.h:144 hard ceiling
    static constexpr float kWgDampingSMax      = 0.45f;

    // --- drive normalisation (FR-032, FR-033, FR-034) --------------------
    static constexpr float kTargetPeak     = 1.0f;
    static constexpr float kMinDriveGain   = 1.0e-7f;
    static constexpr float kMaxDriveGain   = 4.0f;
    static constexpr float kGainBoundEps   = 1.0e-6f;
    static constexpr float kTargetInputRms = 0.25f;
    static constexpr float kRmsFloor       = 1.0e-5f;
    static constexpr float kMinRmsGain     = 0.05f;
    static constexpr float kMaxRmsGain     = 4.0f;
    static constexpr float kRmsAttackMs    = 50.0f;
    static constexpr float kRmsReleaseMs   = 200.0f;
    /// Hard ceiling on the value handed to `rmsFollower_.processSample`.
    /// `EnvelopeFollower::processRMS` squares its argument IN FLOAT
    /// (`envelope_follower.h:313`), so any |x| > ~1.8e19 overflows to +Inf and
    /// LATCHES: the IIR at `:316-321` keeps a non-finite `squaredEnvelope_`
    /// forever, `detail::flushDenormal` at `:184-185` passes Inf through
    /// unchanged (`db_utils.h:168`), and only `reset()`/`prepare()` clears it.
    /// SC-013(b)'s own +/-1e38 probe exceeds that range, so without this clamp a
    /// single legal finite block permanently pins `stateFinite()` false and mutes
    /// the voice. 1e9 squares to 1e18, ~11 orders inside the float ceiling.
    static constexpr float kMaxFollowerInput = 1.0e9f;

    // --- dirty gates (FR-042, FR-042a) ----------------------------------
    static constexpr float kRetuneEpsilonCents = 0.5f;
    static constexpr float kDampingEpsilonRel  = 0.005f;
    static constexpr float kB3Floor            = 1.0e-12f;

    // --- output safety (FR-037) -----------------------------------------
    /// LAST-RESORT guard on the post-crossfade engine sum. Read §7.9 before
    /// treating `getClampEngagementCount()` as a drive-compensation detector:
    /// four of the five materials are bounded to +/-1.0 UPSTREAM of this clamp
    /// and two equal-power gains sum to at most sqrt(2), so this counter is
    /// structurally incapable of moving for them.
    static constexpr float kOutputClamp = 2.0f;           // mirrors harmonic_cloud.h:174
    /// The modal bank's own output-stage soft-clip threshold, set by §6.2's
    /// `setOutputSoftClipThreshold(1.0f)`. `applyOutputStage`
    /// (`modal_resonator_bank.h:789-796`) computes `softClip(x / t) * t`, and
    /// `Krate::DSP::softClip` is strictly bounded to [-1, 1]
    /// (`dsp_utils.h:105-113`). `WaveguideString::process` returns
    /// `softClip(junction)` (`waveguide_string.h:181`) with the same effective
    /// threshold of 1.0. This is the real saturation point on 4 of 5 materials.
    static constexpr float kEngineClipThreshold = 1.0f;
    /// FR-031/SC-001 headroom target: the steady-state peak of an engine's own
    /// pre-clip sum must stay at or below this fraction of
    /// `kEngineClipThreshold`. See §7.9 for the inversion that makes it
    /// measurable from the component's output with no new accessor.
    static constexpr float kEngineHeadroomFrac = 0.9f;

    // --- decay cloud (FR-050 - FR-053a) ---------------------------------
    static constexpr float kCloudLoopMsL       = 37.0f;
    static constexpr float kCloudLoopMsR       = 41.0f;
    static constexpr float kCloudDensity       = 100.0f;  // percent, all 8 stages
    static constexpr float kMaxCloudFeedback   = 0.9995f;
    static constexpr float kCloudDampMinHz     = 800.0f;
    static constexpr float kCloudDampMaxHz     = 18000.0f;
    static constexpr float kCloudDcCutoffHz    = 10.0f;
    static constexpr float kCloudBypassEpsilon = 1.0e-4f;
    static constexpr float kCloudSilenceFloor  = 1.0e-6f;
    /// FR-052 calibration lever (§9.2). 1.0 = the nominal Sigma-of-stage-delays
    /// figure. Documented, measured, and the ONLY sanctioned response to an
    /// SC-008 miss — never widen SC-008's +/-15 %.
    static constexpr float kCascadeDelayFactor = 1.0f;

    // --- determinism (FR-070a) ------------------------------------------
    static constexpr float kSeedDetuneCents = 3.0f;
```

Declaration-order note: `kDefaultMaterial` names `BodyMaterial`, so §5.1's `enum class BodyMaterial` must
be declared **above** this constant block inside the class. The section numbering here is expository, not
source order. `kMinWidth`/`kMaxWidth`/`kDefaultWidth` collide by name with `DiffusionNetwork`'s
namespace-scope constants — harmlessly, because every `ContinuousBody` constant is class-scoped (FR-008);
this is exactly what FR-008's rule buys.

Three derived compile-time facts, asserted so a later edit cannot silently invalidate the plan:

```cpp
    static_assert(kControlChunkSamples == 64, "A-5: the Phase 7 shared control clock");
    static_assert(kMinB1 > 1.0f / 5.0f,
                  "FR-035: the component floor must sit ABOVE the bank's own b1 floor "
                  "(modal_resonator_bank.h:685) so the bank's guard is never the thing "
                  "that binds");
    static_assert(kMaxFollowerInput * kMaxFollowerInput < 1.0e30f,
                  "FR-034: EnvelopeFollower::processRMS squares in float "
                  "(envelope_follower.h:313); the clamped input must not be able to "
                  "overflow, or the follower latches non-finite forever");
```

---

## 5. The material model (FR-010 – FR-013a)

### 5.1 Types

```cpp
    enum class BodyMaterial : std::uint8_t { Glass = 0, Strings, MetalPlate, Chamber, Ice };
    enum class Engine       : std::uint8_t { Modal = 0, Waveguide, Comb };
    static constexpr std::size_t kNumMaterials = 5;
    static constexpr std::size_t kNumEngines   = 3;

    struct MaterialProfile {
        Engine engine;
        const float* ratios;          // nullptr for non-modal; points at a file-scope table
        int   defaultModeCount;       // <= kModeCountCeiling
        float amplitudeExponent;      // alpha in a_k = k^-alpha
        ModalResonatorBank::DampingLaw damping;   // {b1, b3}; b3 also = hfDampingParam (modal)
        float stretch;
        float scatter;
        float referenceHz;
        float t60AtMaxResonanceSec;
        float hfDampingParam;         // modal: == damping.b3; waveguide: S; comb: per-comb damping
    };
```

`ratios` is a raw pointer into a `static constexpr` array declared **inside the class** as
`static constexpr std::array<float, kModeCountCeiling> kGlassRatios` / `kPlateRatios`. Class-scoped
per FR-008; `constexpr` static data members are implicitly `inline` in C++17+, so there is no ODR
hazard and no out-of-line definition needed.

### 5.2 The five profiles, valued (FR-011a)

| Field | Glass | Strings | Metal Plate | Chamber | Ice |
|---|---|---|---|---|---|
| `engine` | Modal | Waveguide | Modal | Comb | Modal |
| `ratios` | `kGlassRatios` | `nullptr` | `kPlateRatios` | `nullptr` | `kGlassRatios` |
| `defaultModeCount` | 32 | — | 32 | — | 32 |
| `amplitudeExponent` α | 1.0 | — | 0.7 | — | 1.3 |
| `damping` `{b1, b3}` | `{0.50f, 5.0e-8f}` | `{0,0}` (unused) | `{0.30f, 1.0e-9f}` | `{0,0}` (unused) | `{0.60f, 3.0e-8f}` |
| `stretch` | 0.0 | — | 0.15 | — | 0.5 |
| `scatter` | 0.0 | — | 0.10 | — | 0.8 |
| `referenceHz` | 660.0 | 196.0 | 330.0 | 110.0 | 880.0 |
| `t60AtMaxResonanceSec` | 13.8 | 8.0 | 23.0 | 2.5 | 11.5 |
| `hfDampingParam` | 5.0e-8 (`b3`) | 0.15 (`S`) | 1.0e-9 (`b3`) | 0.35 (comb damping) | 3.0e-8 (`b3`) |

Strings additionally: `stiffness = 0.15`, `pickPosition = 0.22` (frozen at assignment, FR-022c).
Chamber additionally: `spread = 0.45`, `numCombs = 6`.

**FR-011a's `stereoSpread = 0.6` field is dropped (D-12).** It is provably inert on the path this plan
chose. Verified this session: `setStereoSpread` writes `stereoSpread_` (`timevar_comb_bank.h:585`), which
is read at exactly one site — `recalculatePanPositions`, `:763` — which writes
`channels_[i].panLeftGain`/`panRightGain` (`:769-770`); those two members are read at exactly one site,
`:715-716`, **inside `processStereo`**. `TimeVaryingCombBank::process(float)` (`:593-651`) accumulates
`output += combOutput * smoothedGain` (`:644`) and never touches the pan gains. §6.3 drives the comb
through the mono `process`, so the setting would configure a parameter that is then discarded.

The alternative — routing Chamber through `processStereo` (which mono-sums its own input at `:660-661`,
so A-1's "one object, mono core" input premise would survive) — is **rejected**, because it makes the
Chamber engine's *output* stereo while FR-037's clamp and its `getClampEngagementCount()` counter are
pinned by FR-007 as "the post-crossfade engine sum only, one count per sample, not per channel — the
resonator core is mono, A-1". A stereo comb engine would fork the mix/clamp/`Ĝ` path for one material.
Chamber's width therefore comes **solely from the decay cloud** (FR-051, FR-062), and spec assumption
A-1's second named width source ("plus per-engine stereo spread where the engine offers it for free")
does not apply to Chamber. Recorded as D-12; a correction is filed against FR-011a and A-1.

**Verified against the spec's own derived numbers.** `scale(r) = 40^(1−r)`; at `r = 0.8`,
`scale = 2.0913`. Modal `T60 = 6.91 / (b1·scale)`:
MetalPlate 11.01 s, Glass 6.608 s, Ice 5.506 s; Strings `8.0/2.0913 = 3.826` s; Chamber
`2.5/2.0913 = 1.195` s — reproducing FR-036's table (11.0 / 6.61 / 5.51 / 3.83 / 1.20) and its strict
ordering. At `r = 0`: 0.576 / 0.345 / 0.288 / 0.200 / 0.0625 s, matching FR-036's `r = 0` row.
`b1_eff` spans 0.30 … 24.0, strictly inside `[kMinB1, kMaxB1] = [0.23, 30.0]`, so no clamp binds
anywhere in the table and the ordering is strict at every `r`. **Computed this session, not copied.**

### 5.3 The two ratio tables, pinned (FR-012)

**Glass / Ice** — free-edge axisymmetric shell law `f_n ∝ n(n²−1)/√(n²+1)`, normalised at `n = 2`,
continued to `n = 33` (32 entries). Generated this session from the closed form:

```cpp
    static constexpr std::array<float, kModeCountCeiling> kGlassRatios = {
           1.0000f,    2.8284f,    5.4233f,    8.7706f,
          12.8663f,   17.7088f,   23.2974f,   29.6319f,
          36.7120f,   44.5377f,   53.1089f,   62.4255f,
          72.4875f,   83.2950f,   94.8478f,  107.1460f,
         120.1897f,  133.9786f,  148.5130f,  163.7927f,
         179.8178f,  196.5883f,  214.1041f,  232.3653f,
         251.3718f,  271.1237f,  291.6209f,  312.8636f,
         334.8515f,  357.5849f,  381.0636f,  405.2876f,
    };
```

FR-012's illustrative list ("1.000, 2.83, 5.42, 8.77, 12.85, 17.65") is a rounding of the same law; the
law is normative and entries 5–6 above are the exact values (12.8663, 17.7088). Confirmed by the
spec's own SC-003(c) figure: mean `|r_k − k|` over the first 8 entries = **8.1908 ≈ 8.19**, which is the
number SC-003(c) states — so the spec's inharmonicity target was derived from these exact values.

**Metal Plate** — Rossing's free circular plate ratios for the first 8 (verbatim from FR-012), continued
to 32 by the **constant-modal-density asymptote** for a thin plate (`f_k ≈ a·k + b`; Cremer & Heckl,
Fletcher & Rossing §3 — a plate's modal density is asymptotically constant, unlike a string's harmonic
series or the shell's `n²` law). `a = 1.7309` from a least-squares fit over the published `k = 4…8` and
anchored at `k = 8` so the table is continuous:

```cpp
    static constexpr std::array<float, kModeCountCeiling> kPlateRatios = {
           1.0000f,    1.7300f,    2.3280f,    4.0610f,   // Rossing, published
           5.9800f,    6.7100f,    9.0110f,   11.2000f,   // Rossing, published
          12.9309f,   14.6618f,   16.3927f,   18.1236f,   // plate-density continuation
          19.8545f,   21.5854f,   23.3163f,   25.0472f,
          26.7781f,   28.5090f,   30.2399f,   31.9708f,
          33.7017f,   35.4326f,   37.1635f,   38.8944f,
          40.6253f,   42.3562f,   44.0871f,   45.8180f,
          47.5489f,   49.2798f,   51.0107f,   52.7416f,
    };
```

Mean `|r_k − k|` over the first 8 = **0.9880 ≈ 0.99**, again exactly SC-003(c)'s stated figure.
Both tables are **strictly increasing** (checked programmatically over all 32 entries), which is what
makes FR-043's *prefix* truncation exact.

> Rejected: `tools/gen-plate-chladni.js` (P = 1.7, κ = 0.11), which Membrum's `plate_modes.h` uses.
> It produces `1.0000, 1.1100, 1.9923, 2.2115, 3.2490, …` — a different table that contradicts FR-012's
> published first 8 and would move SC-003(c)'s inharmonicity figure. Recorded so a reviewer does not
> "unify" the two: they model different objects (a cymbal/gong vs a Rossing thin circular plate).

### 5.4 Amplitude profile (FR-011)

`a_k = (k+1)^(−α)` for `k = 0…N−1` where `N` is the **post-truncation** count (FR-043), normalised so
`Σ a_k = 1` over exactly those `N` entries. Consequences, all wanted:

- `getInputGainSum()` (`modal_resonator_bank.h:168-171`) then returns ≈ 1, so
  `setOutputGain(1.0f / getInputGainSum())` (FR-022a) is ≈ 1 and the bank's output stage is transparent.
- No entry falls under the bank's amplitude cull (`kAmplitudeThresholdLinear = 1e-4`, `:568`, `:710`):
  the worst case is the STEEPEST profile at `N = 32`. With Ice re-valued to α = 0.9 during
  implementation (spec FR-011a) that is **Glass**: `a_32 = 32^(−1.0)/4.0585 = 7.70e-3`, 77× above the
  cull (Ice 9.34e-3, Metal Plate 1.32e-2).
- `Ĝ = Σ_k Ĝ_k` (FR-032) is therefore a weighted average of the per-mode bounds with weights summing
  to 1, which is what makes the drive law scale-free in `N`.

Normalisation sums (for the untruncated 32): Glass α=1.0 → 4.058495; MetalPlate α=0.7 → 6.693735;
Ice α=**0.9** → **4.734015**. (Ice was α=1.3 → 2.758925 in this plan as written; it was re-valued
against measurement during implementation because α > Glass made Ice the *darker* of the pair and
inverted SC-003(d) — see spec.md FR-011a. Its `scatter` moved 0.8 → 1.0 in the same pass, for
SC-003(c3); spec.md FR-012 carries the arithmetic.)

### 5.5 Non-modal damping laws (FR-013a)

- **Strings (waveguide).** Mode set = the loop's harmonic series, dispersion-warped by
  `B = stiffness·0.002` (`waveguide_string.h:296`). Damping = round-trip loss
  `|H(ω)| = rho·√((1−S)² + 2S(1−S)cos ω + S²)` (`:379-382`, applied at `:197`),
  `rho = 10^(−3/(T60·f0))` (`:476-481`), `S = brightness·0.5` (`:168`). **C-6: the setter argument
  darkens** — `setBrightness(2·S_eff)`. Flat at `S = 0`, a null at Nyquist at `S = 0.5`, so decay rate
  rises monotonically with frequency for any `S > 0`.
- **Chamber (comb).** Mode set `f[n] = f_body·√(1 + n·spread)` (`timevar_comb_bank.h:789-794`).
  Damping = the per-comb one-pole lowpass inside the feedback path (`setCombDamping`, `:206`), so the
  per-round-trip loss is `fb_n·|H_lp(ω)|` and higher partials of each comb decay faster.

---

## 6. Engine pool, slots, crossfade and the collapse rule (FR-020 – FR-024a)

### 6.1 Pool and slot state

```cpp
    static constexpr std::size_t kNumSlots = 2;

    struct Slot {
        BodyMaterial material   = BodyMaterial::Glass;
        Engine       engine     = Engine::Modal;
        int          modalIndex = -1;      // 0 or 1 when engine == Modal, else -1
        bool         active     = false;   // true while this slot is being advanced
        float        gain       = 0.0f;    // crossfade gain, [0,1]
        float        gainBound  = 1.0f;    // FR-032 G-hat for THIS slot
        float        engineT60  = 0.0f;    // FR-007 getEngineT60Sec source
        int          modeCount  = 0;       // post-FR-043 truncation
        bool         inputMuted = false;   // FR-024 step 3
        OnePoleSmoother driveLog10;        // FR-033, state = log10(engineDrive)
        // applied-value shadows for the FR-042 / FR-042a dirty gates
        float appliedBodyHz = 0.0f;
        float appliedB1 = 0.0f, appliedB3 = 0.0f;
        float appliedT60 = 0.0f, appliedS = 0.0f;
        float appliedCombFb[kNumCombs]{}, appliedCombDamp = 0.0f;
    };

    ModalResonatorBank modal_[2];
    WaveguideString    waveguide_;
    TimeVaryingCombBank comb_;
    Slot  slots_[kNumSlots];
    int   soundingSlot_ = 0;      // index of the slot whose gain is (or is heading to) 1
    int   outgoingSlot_ = -1;     // -1 when no fade in flight
```

Two modal banks are required and sufficient: three materials share the modal engine (A-2), `setModes`
memsets the state (`:238-239`), and FR-024a's collapse rule caps simultaneously-advanced engines at
**two** for any sequence of `setMaterial` calls.

### 6.2 Crossfade state machine

State: `crossfadePos_ ∈ [0,1]`, `crossfadeInc_` (from `crossfadeIncrement(kMaterialCrossfadeMs, sr)`,
`crossfade_utils.h:89`), plus a separate `collapse` sub-state with its own `collapsePos_` /
`collapseInc_` (`crossfadeIncrement(kSlotReleaseMs, sr)`).

`setMaterial(m)`:
1. `m == slots_[incoming].material` → **no-op** (FR-014). "Incoming" is `soundingSlot_` when idle and
   the fade target otherwise.
2. No fade in flight → *assign* (below), `crossfadePos_ = 0`, `outgoingSlot_ = soundingSlot_`,
   `soundingSlot_ = free slot`, and mute the outgoing slot's input (FR-024 step 3).
3. Fade in flight → set `pendingMaterial_ = m` and enter **collapse**. The collapse ramps the current
   `(fadeOut, fadeIn)` pair to `(0, 1)` over `kSlotReleaseMs` using the same equal-power law;
   `crossfadePos_` **does not advance** during the collapse. On completion: the in-flight *incoming*
   engine is the sole sounding engine, the other slot is `silence()`d and freed, then step 2 runs for
   `pendingMaterial_`.

**Gains.** Per control step, `equalPowerGains(crossfadePos_, fadeOut, fadeIn)`
(`crossfade_utils.h:50`). During a collapse, `equalPowerGains(collapsePos_, gOld, gNew)` multiplies the
*frozen* pair. Both are held constant across the 64 samples of one control chunk — a step of at most
`sin(π/2 · 64/24000) ≈ 0.0042` per chunk at 48 kHz, three orders below `ClickDetector`'s sensitivity.

**Assignment** (the incoming slot is at gain 0 at this instant — this is what makes the snaps legal):
- **Modal:** pick the modal bank not in use; compute `f_body` (§8), the truncated mode count (§8.3), the
  seeded micro-detune (§12), the amplitudes (§5.4), `b1_eff`/`b3_eff` (§7.4); call
  `setModes(freqs, amps, count, DampingLaw{b1_eff, b3_eff}, stretch, scatter)`; then
  `setOutputSoftClipThreshold(1.0f)` and `setOutputGain(1.0f / getInputGainSum())` (FR-022a).
- **Waveguide:** `setStiffness(0.15f)`, `setPickPosition(0.22f)`, `setDecay(T60_eff)`,
  `setBrightness(2·S_eff)` — **all before** `noteOn(f_body, 0.0f)`, because `noteOn` snaps all three
  smoothers (`:288-290`) and freezes stiffness/pick (`:283-284`). At velocity 0,
  `velScale = 0` (`:393`, consumed `:446`) so the excitation buffer written is entirely zero (FR-022c,
  N-8). This is the *only* pre-RA-1 way to set `bridgeDelayFloat_` (`:325`).
- **Comb:** `setTuningMode(Tuning::Inharmonic)`, `setSpread(0.45f)`, `setNumCombs(6)`,
  `setFundamental(f_body)`, then per comb `setCombFeedback(n, fb_n)` and `setCombDamping(n, damping_eff)`.
  **No `setStereoSpread`** — it is inert on the mono `process()` path (§5.2, D-12).
- Compute `Ĝ_slot` (§7.3) and **snap** `driveLog10.snapTo(log10(max(engineDrive, kMinDriveGain)))`
  (FR-033).

**Fade completion:** when `crossfadePos_ ≥ 1`, clamp to 1, `silence()` the outgoing engine, mark the
slot free, `outgoingSlot_ = -1`.

### 6.3 Advancing engines (FR-023, SC-016)

Per control chunk, for each **active** slot, in slot order:

| Engine | Call | Notes |
|---|---|---|
| Modal | `modal_[i].processBlock(driveBuf, engBuf, int(chunk))` | the **only** SIMD path (`:355-369`); never `process()` (§0.1 trap 2) |
| Waveguide | `for (s) engBuf[s] = waveguide_.process(driveBuf[s]);` | its only entry point (`:154`) |
| Comb | `for (s) engBuf[s] = comb_.process(driveBuf[s]);` | mono `process` (`:328`, impl `:593-651`). `processStereo` (`:653`) mono-sums its own input at `:660-661`, so its *cost* is one extra accumulate per comb per sample — but its **benefit is real**: it is the only path that applies `stereoSpread` (`:715-716`). That benefit is declined here, deliberately, because a stereo comb output forks FR-037's mono clamp and its per-sample counter for one material (§5.2, D-12). |

`driveBuf` is the mono-summed, zero-substituted, drive-scaled input; when `inputMuted` (the outgoing
slot during a fade) it is a zeroed scratch buffer so the engine rings out through its own damping law.

Accumulate `engineSampleCount_[engine] += chunk` **once per active slot per chunk** — the functional
evidence SC-016 asserts on.

Mix: `mono[s] = Σ_slots gain_slot · engBuf_slot[s]`, then
`mono[s] = std::clamp(mono[s], -kOutputClamp, kOutputClamp)` with a counter increment when the clamp
altered the value (FR-037, one count per sample, mono only). **Read §7.9 before using that counter as
evidence of anything** — on four of the five materials it cannot move.

Two `std::array<float, kControlChunkSamples>` scratch buffers (one per slot) plus one drive buffer and
one mute buffer — 4 × 64 floats = 1 KB, fixed-size members, no allocation, no block-size assumption
(the chunk is capped at 64 by construction).

### 6.4 The uniform setter shape (FR-006, FR-009)

FR-006 requires every setter to clamp to its FR-009 range **and to substitute the FR-009 Default when the
argument is non-finite**, checked by bit pattern. That second clause had no home in an earlier draft of
this plan; it is pinned here so no setter can be written without it. Every float setter in the class is
written in exactly this shape, with no exceptions:

```cpp
    void setResonance(float v) noexcept
    {
        if (!isFiniteBits(v)) v = kDefaultResonance;          // FR-006, §7.8's helper
        resonance_ = std::clamp(v, kMinResonance, kMaxResonance);
        // then either: smoother.setTarget(resonance_)                (smoothed setters)
        //        or  : nothing — read at the control step             (FR-009's three exceptions)
    }
```

Three points the implementer must not vary:

1. **Substitute before clamping, never after.** `std::clamp(NaN, lo, hi)` returns NaN
   (`v < lo` and `hi < v` are both false for a NaN), so a clamp-first ordering silently admits the poison.
2. **`OnePoleSmoother::setTarget` is not a substitute for the check.** It maps NaN → 0 and Inf → ±1e10
   (`smoother.h:170-181`), i.e. to *its own* fallbacks, not to FR-009's Default column — a NaN handed to
   `setResonance` would land the component on resonance 0, not 0.7.
3. The **three FR-009 exceptions** (`setResonance`, `setDamping`, and the ramped/crossfaded
   `setMaterial`/`setResonatorBypass`) skip only the *smoother* step. They still substitute and clamp.
   `setSeed(std::uint32_t)` takes no float and needs neither.

`setInputAgcEnabled(bool)` and `setResonatorBypass(bool)` take no float. `setMaterial(BodyMaterial)` is
range-checked by the enum; an out-of-range cast is UB and is not defended against (documented).

Verified by `ContinuousBody_ControlSurfaceDefaults` (§15.2), which feeds every float setter a bit-pattern
NaN and a bit-pattern ±Inf and asserts each observable lands on its Default — the test that would have
caught the omission.

---

## 7. Continuous-excitation adapter (FR-031 – FR-038a) — the new DSP work

### 7.1 Why (FR-031, restated with the verified arithmetic)

At `kMinB1 = 0.23` and `fs = 48 kHz`, `1 − R = 1 − exp(−0.23/48000) = 4.79e-6`, so a mode's
steady-state magnitude at its own resonance is ≈ `1/(2(1−R)) ≈ 1.04e5` times the drive. Membrum never
sees this because it excites with impulses. Without compensation the bank sits pinned at its clipper for
the whole ring of a sustained input and every material sounds identical.

### 7.2 The modal transfer function — derived and **numerically verified**

The bank's per-mode update is the coupled (magic-circle) form, `modal_resonator_bank.h:853-859`:

```
s[n] = R·(s[n−1] + ε·c[n−1]) + g·u[n]
c[n] = R·(c[n−1] − ε·s[n])           // uses the UPDATED s
y[n] = s[n]
```

Eliminating `c`:

```
H(z) = g·(1 − R z⁻¹) / [ 1 − R(2 − Rε²) z⁻¹ + R² z⁻² ]
```

— a **zero at `z = R`**, poles of radius `R` at angle `θ` with `cos θ = 1 − Rε²/2`, and
`ε = 2 sin(π f/fs)` (`:746`). The pole angle is taken from `ε` and `R`, never assumed to be `2πf/fs`
(they coincide only at `R = 1`).

Evaluating `|H(e^{jθ})|` exactly, with no transcendentals:

```
cθ  = 1 − R·ε²/2
c2θ = 2·cθ² − 1
Ĝ_k = g_k · √(1 − 2R·cθ + R²) / [ (1 − R) · √(1 − 2R·c2θ + R²) ]
Ĝ   = Σ_k Ĝ_k            // all-modes-in-phase worst case → a true upper bound
```

**Verified this session** by driving the exact recursion above with a unit sine at the pole angle and
measuring the steady-state peak:

| f (Hz) | b1 | closed form | simulated | ratio |
|---|---|---|---|---|
| 220 | 1.512 | 15 849.55 | 15 849.55 | 1.0000 |
| 220 | 0.500 | 47 774.00 | 47 773.99 | 1.0000 |
| 440 | 1.512 | 15 778.84 | 15 778.81 | 1.0000 |
| 2000 | 1.512 | 14 139.91 | 14 128.20 | 0.9992 |
| 110 | 0.230 | 104 350.78 | 104 350.66 | 1.0000 |
| 3900 | 0.600 | 23 481.89 | 23 480.23 | 0.9999 |

The formula is exact to the fourth decimal. The flat-numerator form an earlier spec draft carried
over-estimates by `≈ 1/(2 sin(θ/2)) ≈ 35×` (31 dB) at 220 Hz / 48 kHz — this is precisely what SC-015's
tightness clause (`measuredGain ≥ 0.1 × Ĝ`) rejects.

Cost: 2 `sqrt` and ~10 flops per mode, at most once per control step, gated by the same dirty flag as
`updateModes` (§7.5). For a 32-mode bank that is ≈ 64 `sqrt` on a *gated* path.

### 7.3 Per-engine `Ĝ`

- **Modal:** the sum above, over the configured (post-truncation) modes.
- **Waveguide:** `Ĝ = 1 / max(1 − gTotal, kGainBoundEps)` where
  `gTotal = rho·√((1−S)² + 2S(1−S)cos ω₀ + S²)`, `rho = 10^(−3/(T60_eff·f0))`
  (`waveguide_string.h:476-481`), `ω₀ = 2π f0/fs` — the identical expression the header computes for its
  own excitation normalisation (`:379-383`). **`T60_eff` is the value after the `[0.05, 10.0]` clamp**,
  not the requested one (FR-035).
- **Comb:** `Ĝ = Σ_n 1 / max(1 − fb_n, kGainBoundEps)` over the `kNumCombs` active combs, with `fb_n`
  as solved in §7.4 and `τ_n` from the bank's **clamped** delay (`:737-741` clamps to `[1, 50]` ms).

`Ĝ` is per slot (FR-032): computed at material assignment, recomputed at a control step only when the
slot's dirty gate fires, frozen for the outgoing slot for the remainder of a fade.

### 7.4 Resonance and Damping (FR-036)

One law, three engines:

```
scale(r) = kResonanceScaleAtZero ^ (1 − r)     // 40^(1-r); scale(1)=1, scale(0)=40
```

| Engine | Resonance | Damping |
|---|---|---|
| Modal | `b1_eff = clamp(b1_material · scale(r), kMinB1, kMaxB1)` | `b3_eff = b3_material · (1 + kDampingB3Scale·d)` |
| Waveguide | `T60_eff = clamp(t60AtMaxResonance / scale(r), kWgT60Min, kWgT60Max)` → `setDecay` | `S_eff = S_material + d·(kWgDampingSMax − S_material)` → `setBrightness(2·S_eff)` |
| Comb | `T60_eff = t60AtMaxResonance / scale(r)`; per comb `fb_n = min(10^(−3·τ_n/T60_eff), kMaxCombFeedback)`, `τ_n` = the comb's clamped delay in seconds | `damping_eff = damping_material + d·(1 − damping_material)` → `setCombDamping(n, ·)` |

`scale(r)` is strictly decreasing in `r` for every material, so **T60 monotonicity in Resonance is a
property of the law, not a hope** (SC-003b). `getEngineT60Sec()` returns `6.91/b1_eff` (modal),
`T60_eff` (waveguide), `T60_eff` (comb).

Implementation note: `scale(r)` costs one `std::exp2` — write it as
`exp2f((1.0f − r) · log2f(40.0f))` with `log2(40)` a `constexpr` (`5.321928f`), not `std::pow`.

### 7.5 Drive compensation (FR-033) — two drives, per slot, log10-smoothed

```
engineDrive_slot = clamp(kTargetPeak / Ĝ_slot, kMinDriveGain, kMaxDriveGain) · rmsGain · userDrive
cloudDrive       = rmsGain · userDrive          // FR-063 path: no 1/Ĝ, no referent
```

- At **material assignment**: `driveLog10.snapTo(log10f(max(engineDrive, kMinDriveGain)))`. Legal and
  clickless because the incoming slot is at zero crossfade gain (FR-024 step 1); *necessary* because
  `Ĝ` spans 3–5 decades between engines and a shared linear smoother crossing that span would over-drive
  the incoming engine by tens of dB for tens of ms — landing directly on SC-001's
  `getClampEngagementCount() == 0` clause.
- For **every continuous move** (glide, resonance/damping, AGC, `setDrive`):
  `driveLog10.setTarget(log10f(max(engineDrive, kMinDriveGain)))`, then per control step
  `driveLog10.advanceSamples(chunk)` (O(1), `smoother.h:243-254`) and read
  `exp10f(driveLog10.getCurrentValue())` — a constant dB/ms slope, scale-invariant across the `Ĝ` span.
  A one-pole on the *linear* value spends most of its trajectory near the larger endpoint; a one-pole on
  `log10` does not.
- `exp10f` is `std::exp2(x · 3.32192809f)`; **do not use `std::pow(10, x)`** on this path (it is
  evaluated once per control chunk per active slot, ~1500/s/voice).
- The gain applied inside a chunk is **constant** (the value read at the chunk's control step) — one
  multiply per sample, no per-sample smoother.

### 7.6 Input RMS tracking / AGC (FR-034, FR-034a)

```cpp
    EnvelopeFollower rmsFollower_;   // DetectionMode::RMS
```

- `prepare`: `rmsFollower_.prepare(sampleRate / double(kControlChunkSamples), 1)` —
  **the control rate, not the audio rate.** `calculateCoefficient` derives from `sampleRate_`
  (`envelope_follower.h:359-365`) and `processSample` advances exactly one step per call (`:164-188`),
  so preparing at 48 kHz and calling once per 64 samples would stretch 50 ms into 3.2 s and 200 ms into
  12.8 s, and SC-007's 1 s recovery clause would be unreachable. Then `setMode(DetectionMode::RMS)`,
  `setAttackTime(kRmsAttackMs)`, `setReleaseTime(kRmsReleaseMs)`. The sidechain highpass is off by
  default and is never enabled; it is configured at the control rate by `prepare` and unused.
- Feed it `chunkRms = min(√(chunkSumSq_ / 64), kMaxFollowerInput)`, **not** one sample of the chunk.
  `DetectionMode::RMS` squares its input and square-roots the smoothed result, so equal-length chunk RMS
  values give the true windowed RMS exactly.
- **The `kMaxFollowerInput` clamp is load-bearing, not defensive garnish.** `processRMS` squares its
  argument **in float** (`envelope_follower.h:313`, `const float squared = sample * sample;`), so a legal
  finite input block of ±1e38 — which FR-038's `isFiniteBits` guard correctly does **not** intercept, and
  which is exactly SC-013(b)'s probe — produces `chunkRms ≈ 1e38`, squares to `+Inf`, and latches:
  the branch at `:316-321` keeps `squaredEnvelope_` non-finite for every subsequent update,
  `envelope_ = std::sqrt(Inf)` at `:325` is Inf, and the denormal flush at `:184-185` passes Inf through
  untouched (`db_utils.h:168`). `processSample` is documented "Does NOT validate input" (`:163-164`).
  Only `reset()`/`prepare()` clears it. Without the clamp, **any** finite input with `|m| > ~1.8e19`
  permanently pins `stateFinite()` false — which under §7.8's FR-038a ramp mutes the voice forever, and
  makes SC-013(b)'s "`stateFinite()` true again within 100 ms" unreachable by construction.
  Clamping at 1e9 keeps the square 11 orders inside the float ceiling; the `static_assert` in §4.2 pins it.
- `getInputRms()` (FR-007) therefore reports a value saturating at `kMaxFollowerInput`. Documented at the
  accessor. No success criterion drives the follower anywhere near it — SC-007(iv)'s ±10 % tracking clause
  runs at full-scale-and-below levels.
- `sumSq_` (float accumulator, but **accumulate in `double`** to keep 64 squares of a full-scale signal
  exact) and `sampleCounter_` **carry across `processStereoBlock` calls** (FR-005a): `N` is always
  exactly 64 at the instant the follower advances.
- `rmsGain = clamp(kTargetInputRms / max(rms, kRmsFloor), kMinRmsGain, kMaxRmsGain)`; held at exactly
  `1.0f` when `setInputAgcEnabled(false)` (the follower still tracks, so `getInputRms()` stays
  meaningful).

### 7.7 Damping floors (FR-035)

| Engine | Floor | Rationale |
|---|---|---|
| Modal | `b1_eff ≥ kMinB1 = 0.23` (T60 = 30.0 s) | above the bank's own `1/5` floor (`:685`), so that guard is never the binding one — asserted at compile time (§4.2) |
| Waveguide | `T60 ∈ [0.05, 10.0]` s | the component's own clamp is `[0.01, 10.0]` (`:144`); FR-036 tops out at 8.0 s so neither binds. `rho < 1` strictly at every setting |
| Comb | `fb_n ≤ kMaxCombFeedback = 0.995` | well inside the bank's `±0.9999` (`comb_filter.h:32,:35`, applied at `timevar_comb_bank.h:488`) |

### 7.8 Non-finite handling (FR-038, FR-038a)

**Bit-pattern finiteness only** — the macOS leg builds with `-ffast-math`, which folds `std::isnan` /
`std::isinf` / `numeric_limits::infinity()`:

```cpp
    [[nodiscard]] static bool isFiniteBits(float v) noexcept {
        std::uint32_t bits = 0;
        std::memcpy(&bits, &v, sizeof(bits));
        return (bits & 0x7F800000u) != 0x7F800000u;
    }
```

- **FR-038 (input hygiene).** While accumulating the control chunk, if any mono-summed sample fails
  `isFiniteBits`, the poisoned **sub-chunk and the remainder of the control chunk** are replaced by zeros
  before the drive stage, `chunkSumSq_` is *assigned* 0, and processing continues.
  **No `silence()`, ring preserved.** See §11 step 1 for the exact guarded accumulation and D-10 for why
  the unit is the sub-chunk rather than the whole chunk. This must happen upstream of the engines because
  `TimeVaryingCombBank` (`:598-601`, `:664-669`) and `OnePoleLP` (`:104-107`) both `reset()` themselves on
  a non-finite input — i.e. they would destroy the ring on our behalf.
- **FR-038a (state recovery).** Once per control step, *after* the engines advance, evaluate
  `stateFinite()`. On `false`: ramp the affected subsystem(s) to zero over `kSlotReleaseMs` with the
  equal-power law (the same mechanism as FR-024a's collapse and FR-063's bypass), run the recovery action
  set below at zero gain, then ramp back to unity once `stateFinite()` reports true.
  A `silence()` therefore only ever happens at zero gain — everywhere in this component.

#### 7.8.1 `stateFinite()` is an AND of per-subsystem predicates

FR-038a clause 2 requires clearing the cloud "only if the cloud's own state is what went non-finite".
A single aggregate boolean cannot express that, so `stateFinite()` is composed:

```cpp
    [[nodiscard]] bool engineStateFinite(int slot) const noexcept;   // private
    [[nodiscard]] bool cloudStateFinite()          const noexcept;   // private
    [[nodiscard]] bool controlStateFinite()        const noexcept;   // private
    [[nodiscard]] bool stateFinite() const noexcept                  // FR-007, public
    { return engineStateFinite(0) && engineStateFinite(1)
          && cloudStateFinite() && controlStateFinite(); }
```

FR-007's accessor list stays exactly as specified — the three predicates are **private**, and the public
surface is unchanged.

**The engine predicate must not observe saturated state.** This was the defect an earlier draft shipped:
it checked "both modal banks' **output** over the last chunk" and `waveguide_.getFeedbackVelocity()`, and
every one of those observables passes through a saturator that maps overflow to a *finite* value.
`softClip(+Inf)` returns `1.0f` outright (`dsp_utils.h:107`, `if (sample > 3.0f) return 1.0f;`),
`ModalResonatorBank::processBlock` ends every sample with `output[i] = applyOutputStage(modeSum)`
(`:366`) which is `softClip(modeSum / t) * t` (`:789-796`), and
`WaveguideString::feedbackVelocity_ = output` is assigned the **post-`softClip`** value
(`waveguide_string.h:181`, `:215`). A bank whose `sinState_`/`cosState_` had gone to ±Inf would read out
as exactly 1.0. So:

| Subsystem | Predicate observes | Why this observable |
|---|---|---|
| Modal slot | `modal_[i].getModalEnergy()` (`modal_resonator_bank.h:415`, public) | computed directly from `sinState_`/`cosState_` (`:418-419`), **never** through `outputGain_` or the soft clipper — the header says so at `:408-410`. It is the only public window on the bank's raw state. |
| Waveguide slot | `waveguide_.getFeedbackVelocity()` + the last engine sample | belt-and-braces only — see the self-bounding note below |
| Comb slot | the comb's last output | belt-and-braces only — `TimeVaryingCombBank::process` resets a comb whose own output goes non-finite (`:637-641`) and returns 0, so it self-heals |
| Cloud | last tap + both feedback-write values, captured during the chunk | the one subsystem a finite input can actually poison |
| Control | every smoother's `getCurrentValue()` and `rmsFollower_.getCurrentValue()` | the follower is the one that latches (§7.6) |

It must be cheap — a fixed set of scalars captured during the chunk, not a buffer scan of internal arrays
we cannot reach.

#### 7.8.2 What can actually go non-finite (traced, not assumed)

- **Waveguide: cannot, from finite input.** `process` (`:154-218`) computes
  `junction = feedback + excitation`, then `output = softClip(junction)` ∈ [−1, 1] (`:181`); the DC
  blocker, dispersion cascade and loss filter (`rho·((1−S)x + S·lossState_)`, `:197`, `rho < 1`) all run
  on that bounded value before it is written back into the delay (`:205`). The loop is **self-bounding**.
  The only poisoning path is a non-finite *excitation*, which FR-038 intercepts upstream.
- **Modal: bounded in practice.** `s = R(s + εc) + g·u` with `R < 1`; the drive that reaches it is
  `(1/Ĝ) · rmsGain · userDrive`, and `Ĝ` is 3–5 decades, so a ±1e38 input block (which drives `rmsGain`
  to `kMinRmsGain = 0.05`) arrives attenuated. Reachable only at absurd gain combinations; the predicate
  exists so the recovery branch is correct if it ever is reached.
- **Comb: self-heals** (`:637-641`), and additionally cannot be poisoned because FR-038 never lets a
  non-finite sample reach it.
- **Cloud: reachable, and this is the case SC-013(b) must use.** On the FR-063 bypass path the
  mono-summed input is scaled by `cloudDrive = rmsGain · userDrive` and fed **directly** to the cloud —
  FR-037's ±2.0 clamp sits on the *engine sum*, so it does not cover this path. At `cloudDecaySec = 30`
  the loop's steady-state gain is `1/(1 − fb) ≈ 1/(1 − 0.97862) ≈ 47`, and with `kMaxCloudFeedback`
  actually reached it is up to 2000. A ±1e38 input block gives `rmsGain = 0.05`, `userDrive = 4`, so
  ≈ 2e37 enters the delay line and the accumulation overflows to ±Inf within a few traversals.
  With the resonator **not** bypassed the cloud's input is the clamped engine sum (≤ 2.0), so
  `2.0 × 2000 = 4000` — finite at every setting. That asymmetry is a real property and is recorded here
  rather than discovered as a bug.

The bypass-path input is deliberately **not** clamped: it is the user's own excitation path, and clamping
it would make FR-038a unreachable — i.e. would turn the entire state-recovery mechanism into dead code
that no test could ever exercise. Containment is FR-038a's job and FR-038a does it.

#### 7.8.3 The recovery action set — every checked state must appear in it

**The rule: a state that `stateFinite()` observes and the recovery does not clear is an unrecoverable
latch by construction.** An earlier draft checked `rmsFollower_.getCurrentValue()` but reset only the
engines, so a single latched follower held the FR-038a ramp at zero forever. On `stateFinite() == false`,
at zero gain:

1. **If `engineStateFinite(slot)` is false** — `silence()` the slot's engine, then **immediately re-tune
   it** (§10.1) before the ramp back to unity. For `Engine::Waveguide` that re-tune is mandatory, not
   hygiene: `WaveguideString::silence()` sets `bridgeDelayFloat_ = 0.0f` (`waveguide_string.h:243`) and
   `process()` early-returns 0 whenever `bridgeDelayFloat_ < kMinDelaySamples` (`:156`), so a silenced
   string is bricked until something rewrites that field — and it is written in exactly three places:
   `silence()` (`:243`), `noteOn()` (`:325`) and RA-1's `retune()` (§2).
2. **If `cloudStateFinite()` is false** — clear `delayL`/`delayR`, `diffusion.reset()`, `dampL/R.reset()`,
   `dcL/R.reset()`, and zero `cloud_.lastPeak`. This is FR-038a clause 2, and the per-subsystem predicate
   is what makes "only if the cloud's own state is what went non-finite" implementable.
3. **If `controlStateFinite()` is false** — `rmsFollower_.reset()` **and** re-snap `driveLog10` for every
   slot to `log10f(max(engineDrive, kMinDriveGain))` recomputed from the post-reset `rmsGain`, plus
   `snapToTarget()` on any smoother reading non-finite. The follower reset is the clause whose absence
   made the latch permanent; the drive re-snap is required because the drive was derived from the
   poisoned follower value.
4. Ramp back to unity over `kSlotReleaseMs` once `stateFinite()` reports true — never as a step.

`reset()` and `prepare()` run the same clearing set unconditionally (§13), so the two paths cannot drift.

### 7.9 What `getClampEngagementCount()` can and cannot detect (FR-037, SC-001, R-4)

FR-037's ±2.0 clamp sits on the **post-crossfade engine sum**. Every engine that feeds it, except one, is
already bounded to ±1.0 upstream:

| Material | Engine | Upstream bound | Verified at |
|---|---|---|---|
| Glass, Metal Plate, Ice | Modal | `applyOutputStage` → `softClip(x/1.0)·1.0`, strictly `[−1, 1]` | `modal_resonator_bank.h:366`, `:789-796`; `dsp_utils.h:102-113`; threshold set by §6.2 |
| Strings | Waveguide | `process` returns `softClip(junction)` | `waveguide_string.h:181` |
| Chamber | Comb | **none** — `process` has no output stage, only a per-comb non-finite reset and a denormal flush | `timevar_comb_bank.h:593-651` |

Two equal-power crossfade gains sum to at most `√2 ≈ 1.414`, so an all-modal render, a modal↔Strings
crossfade, or any Strings render **can never reach 2.0**. `getClampEngagementCount()` is therefore
structurally incapable of moving for four of the five materials, and a criterion resting on it alone
cannot distinguish working drive compensation from an engine pinned permanently at its own clipper —
which is exactly the failure FR-031 exists to prevent and SC-001 exists to detect.

**The measurable substitute, with no new accessor.** `softClip` is strictly monotone on `[−3, 3]`
(`dsp_utils.h:105-113`), so a bound on the *post*-clip output is an exact bound on the *pre*-clip engine
sum. With `cloudMix = 0`, `setMix(1.0)` and no crossfade in flight, the component's output **is** the
post-`applyOutputStage` engine sum. Requiring pre-clip headroom
`|modeSum| ≤ kEngineHeadroomFrac · kEngineClipThreshold = 0.9` is therefore exactly:

```
softClip(0.9) = 0.9 · (27 + 0.81) / (27 + 9·0.81) = 0.9 · 27.81 / 34.29 = 0.72996…
```

> **SC-001 headroom clause (modal + waveguide materials):** the steady-state peak of a `cloudMix = 0`
> probe render must not exceed **0.730**. Equivalent to "the engine's own soft clipper compresses by no
> more than 1.9 dB at the steady-state peak", and it fails long before the ±2.0 clamp would.

For **Chamber** the engine sum is genuinely unbounded, so `getClampEngagementCount()` is meaningful and is
kept as-is; the same headroom clause is applied against `kOutputClamp` (peak ≤ `0.9 × 2.0 = 1.8`) rather
than against a clipper Chamber does not have.

Consequences recorded elsewhere: §15.2's SC-001 row carries **both** clauses and names which material each
binds on; §18's R-4 detector is re-pointed at the headroom clause; §17 records the change as D-9.
Option (i) from the review — adding a per-engine saturation counter to the introspection surface — was
**rejected**: FR-007 declares its accessor list exhaustive ("a success criterion may not assert on a
quantity absent from it"), so it would require a spec amendment, and the monotone inversion above yields
the same discrimination from accessors that already exist.

---

## 8. Key tracking and retune (FR-040 – FR-043)

### 8.1 Body pitch law (FR-040)

```
f_body = referenceHz · (clamp(noteHz, 20, 8000) / referenceHz) ^ keyTracking
```

Implemented in the log domain: `f_body = exp2f(log2f(referenceHz) + keyTracking · (log2f(noteHz) − log2f(referenceHz)))`.
Both `keyTracking` and `log2(noteHz)` are smoothed at `kPitchSmoothMs = 20` **in the log-frequency
domain** (FR-009), so a glide is geometric and `advanceSamples(chunk)` gives the per-chunk value in O(1).
`f_body` is clamped to `[kMinNoteHz, kMaxNoteHz]`.

### 8.2 State-preserving retune (FR-041)

| Engine | Call | Continuity mechanism |
|---|---|---|
| Modal | `updateModes(freqs, amps, count, DampingLaw{b1_eff,b3_eff}, stretch, scatter)` (`:264-275`) | the bank's coefficient smoother — but see the correction below: it runs at **block rate**, not sample rate |
| Comb | `setFundamental(f_body)` (`:238`) | the bank's 20 ms delay smoothers (`:109`) |
| Waveguide | **`retune(f_body)`** (§2) — never `noteOn` | `bridgeDelayFloat_` is read by `readLinear` (`:175`), which interpolates; the loss path converges via `frequencySmoother_` |

Never `updateDampingLaw` (§0.1 trap 11).

**Correction: the modal bank's "2 ms coefficient smoother" is not 2 ms on this path.** Verified this
session: `smoothCoeff_ = std::exp(-1.0f / (kSmoothingTimeMs * 0.001f * sampleRate_))` is computed in
`prepare` (`modal_resonator_bank.h:182`) as a **per-sample** constant with `kSmoothingTimeMs = 2.0f`
(`:569`), but `smoothCoefficients()` is invoked exactly **once per `processBlock` call** (`:357`), not once
per sample. The §11 walker calls `processBlock` once per sub-chunk, so the effective time constant is
`kSmoothingTimeMs × subChunkSamples` — **≈ 128 ms at 48 kHz with a full 64-sample chunk**, and each call
moves only `1 − exp(−1/96) ≈ 1.04 %` of the remaining delta. Two consequences:

1. **Continuity is still delivered, but slowly.** A key-tracked glide's `ε`/`R` lag the control step by
   ~128 ms and move in per-chunk stairs rather than a per-sample glide. That is well inside SC-004's
   click bounds (a 1 % step on a coefficient is not a waveform discontinuity) and inside SC-003(b)'s 5 %
   per-step T60 tolerance, but the earlier wording ("the bank's own 2 ms coefficient smoother") was simply
   wrong and would have sent a click investigation to the wrong place.
2. **The smoothing cadence is host-partition dependent** — and that is a real SC-011 exposure, tracked as
   R-12 and OQ-E. A 1024-sample render is 16 `processBlock` calls when the host delivers 1×1024 from
   counter 0, 17 when it delivers 1023+1 (15×64 + 63, then 1), and more still under 100+…+24. Whenever
   coefficients are in flight (any `updateModes` from FR-042/FR-042a) the partitions therefore see
   different smoothing trajectories. SC-011's block-size case is amended in §15.2 to render **with an
   in-flight retune and damping change** so the dependence is measured against `kSampleTolerance = 1e-4`
   instead of hiding behind a settled render, where `epsilon_ == epsilonTarget_` (the `reset()` memcpy at
   `:199-201`) makes `smoothCoefficients` a no-op and the criterion vacuous for this failure mode.

### 8.3 Cadence, dirty gates and the mode-count rule (FR-042, FR-042a, FR-043)

One `updateModes` call per control step **at most**, gated by an OR of two flags:

```
pitchDirty   = |1200·log2(f_body / appliedBodyHz)| > kRetuneEpsilonCents          // 0.5 cents
dampingDirty = |b1_new − appliedB1| > kDampingEpsilonRel · max(appliedB1, kMinB1)
            || |b3_new − appliedB3| > kDampingEpsilonRel · max(appliedB3, kB3Floor)
```

A **relative** damping threshold because `b1` and `b3` differ by eight orders of magnitude between
materials. `computeModeCoefficients` runs a `sqrt`, two `sin` and an `exp` per mode
(`:726`, `:729`, `:743`, `:746`) — ≈ 128 transcendentals for 32 modes — so this gate is the single
largest lever on SC-005's *operating point* configuration. The waveguide and comb paths use the same
gate on `T60_eff`, `S_eff`, `fb_n`, `damping_eff`.

Monotonicity (SC-003b) is unaffected: the gate defers an apply until the change exceeds 0.5 %, far below
SC-003(b)'s 5 % per-step T60 tolerance.

**FR-043 mode count.** At **material assignment**:

```
N = min(defaultModeCount,
        #{leading k : ratio[k] · (2·f_body) · √(1 + B(k+1)²) · (1 + C·sin(k·kScatterD)) < kBankNyquistGuard·fs})
```

i.e. the count is chosen for **one octave of glide headroom** (`kNyquistHeadroomOct = 1.0`), applying the
bank's own stretch and scatter warps (`:726`, `:729`) so the prefix boundary matches what the bank will
actually compute. Both tables are strictly increasing, so a prefix truncation is exact. Zero-amplitude
modes are **not free**: `processBlock` passes `numModes_` to the kernel (`:362-364`) and
`flushSilentModes` only decrements `numActiveModes_` (`:383-396`).

Counts computed this session (48 kHz, configured at `2·f_body`):

| `f_body` | Glass | Metal Plate | Ice |
|---|---|---|---|
| 55 Hz | 22 | 32 | 19 |
| 110 Hz | 15 | 32 | 14 |
| 220 Hz | **11** | **29** | **10** |
| 440 Hz | 7 | 16 | 7 |
| 880 Hz | 5 | 9 | 4 |

At 44.1 kHz / 220 Hz: 10 / 27 / 10. At 96 kHz / 220 Hz: 15 / 32 / 14.

**Two consequences the plan pins now.** (i) FR-043's worked example ("Glass at 220 Hz gets ~13 modes")
is 11 with the exact tables — no threshold moves, the estimate was rough. (ii) **Metal Plate is the most
expensive material at every pitch** (its ratios grow linearly, Glass's grow as `n²`), so SC-005's four
worst-case baselines are pinned to Metal Plate (§14, Q8).

On **retune inside** the headroom window the count is unchanged and the bank's own cull (`:732-738`)
silences modes that cross the guard — correct, reversible, at the cost of a few idle lanes. On retune
**outside** the window the count may only ever *increase* mid-ring; a decrease is deferred to the next
material assignment, where `setModes` clears state anyway.

---

## 9. The decay cloud (FR-050 – FR-055)

### 9.1 Topology and cadence

```
in → [+] → DelayLine(L_ch) → DiffusionNetwork → OnePoleLP(damp) → DCBlocker → out
      ↑                                                              │
      └──────────────────────── × fb_ch ─────────────────────────────┘
```

Nested private struct `DecayCloud` (single-use, private, no independent existence — project rule):

```cpp
    struct DecayCloud {
        DelayLine      delayL, delayR;
        DiffusionNetwork diffusion;
        OnePoleLP      dampL, dampR;
        DCBlocker      dcL, dcR;
        std::size_t    loopSamplesL = 0, loopSamplesR = 0;
        float          fbL = 0.0f, fbR = 0.0f;
        float          loopSecondsL = 0.0f, loopSecondsR = 0.0f;
        float          lastPeak = 0.0f;      // FR-053a bypass evaluation
    };
```

**The loop is evaluated in the audio-rendering path (`renderSub`), batched over the sub-chunk — not in
`controlStep`.** `DiffusionNetwork::process` (`:327-329`) is a block entry point with no per-sample API;
calling it with `numSamples = 1` pays call overhead 48 000 times a second. But the batch unit is
`subChunk`, **not** the full control chunk: `controlStep` fires only on the absolute 64-grid (§11), so
placing the cloud there would produce **no cloud output at all** for any sub-64 tail — precisely the
1023+1, 100+…+24 and 7×146+2 partitions SC-011 exists to catch. `controlStep`'s cloud item is therefore
**coefficient update only** (`fb`, damping cutoff, size smoother advance); the read / diffuse / damp /
DC-block / write pass runs inside `renderSub` over exactly `subChunk` samples. §11 item 7 is worded to
match; the two sections are consistent, and where an earlier draft let them disagree, **§9.1 governs**.

The chunk read is causal because the loop delay far exceeds any sub-chunk: 37 ms = 1776 samples at 48 kHz,
296 even at 8 kHz.

The batched read uses `DelayLine::read(size_t)` (`:211-219`, `read(d) = buffer[(writeIndex−1−d) & mask]`)
**before any write in the sub-chunk**:

```cpp
    for (std::size_t s = 0; s < subChunk; ++s)
        tap[s] = cloud_.delayL.read(cloud_.loopSamplesL - 1 - s);   // oldest first
    // ... diffusion / damp / dcblock over tap[0..subChunk)
    for (std::size_t s = 0; s < subChunk; ++s)
        cloud_.delayL.write(detail::flushDenormal(in[s] + cloud_.fbL * wet[s]));   // R-6
```

This requires **`loopSamples ≥ subChunk`** (not `≥ chunk` — the walker's unit is the sub-chunk).
`prepare()` therefore computes `cloudChunkCap_ = min(loopSamplesL, loopSamplesR, kControlChunkSamples)`
and the block walker (§11) caps every sub-chunk by it, so the guard holds by construction. At every sane
rate `cloudChunkCap_ == 64`; the cap exists so a degenerate `prepare(1000.0)` does not read the future.

**`loopSamplesL/R` are derived from the delay line alone, never from `loopSecondsL/R`.** The two names sit
adjacent in §13 step 6 and it would be natural — and wrong — to derive one from the other:
`loopSecondsL` (93.9 ms at `cloudSize = 1.0`) is the *acoustic* loop time including the diffusion
cascade's distributed throughput delay, while the `DelayLine` is sized for its own 37/41 ms only (§13
step 3, C-3). Pinned:

```cpp
    loopSamplesL = static_cast<std::size_t>(std::lround(kCloudLoopMsL * 1e-3 * sampleRate_));
    loopSamplesR = static_cast<std::size_t>(std::lround(kCloudLoopMsR * 1e-3 * sampleRate_));
    // prepare-time sanity — DelayLine::read CLAMPS silently (delay_line.h:212-218,
    // `std::min(delaySamples, maxDelaySamples_)`), so an over-long index does not fault:
    // it just reads the wrong tap and the loop time is silently wrong, with no symptom
    // other than a T60 miss that SC-008's +/-15 % may or may not catch.
    assert(loopSamplesL <= cloud_.delayL.maxDelaySamples());   // delay_line.h:297
    assert(loopSamplesR <= cloud_.delayR.maxDelaySamples());
```

(`assert` is debug-only and therefore RT-legal here; `prepare()` is the one method allowed to be
non-`noexcept`. `getCloudLoopSeconds()` (FR-007) continues to report `loopSecondsL`, the acoustic
quantity FR-052 derives `fb` from.)

`setModDepth(0.0f)` is the default and RA-4 makes it free. `setDensity(kCloudDensity = 100)` keeps all
8 stages (`:356`).

### 9.2 RT60 mapping (FR-052) — and the two things measurement forced

```
cascadeSec_ch = kBaseDelayMs·1e-3 · cloudSize · Σ kDelayRatiosL · kCascadeDelayFactor
                                              · (ch == R ? kStereoOffset : 1)
loopSeconds_ch = kCloudLoopMs_ch·1e-3 + cascadeSec_ch
fb_ch          = min(exp10f(-3.0f · loopSeconds_ch / seconds), kMaxCloudFeedback)
```

`Σ kDelayRatiosL = 17.777` (`diffusion_network.h:51-53`); a Schroeder allpass of delay `D` has mean group
delay exactly `D`, so the cascade's mean delay is the sum of its stage delays. **`cloudSize` is the
normalised `[0,1]` value**, matching the smoothed quantity the network itself multiplies by (`:335`,
`:366`) — not the percent handed to `setSize`. At `cloudSize = 1.0`: `cascadeSec_L = 56.9` ms,
`cascadeSec_R = 64.1` ms, `loopSeconds_L = 93.9` ms, `loopSeconds_R = 105.1` ms. At `seconds = 30`,
`fb_L ≈ 0.97862` — far inside `kMaxCloudFeedback`, so the loop is provably contracting at every setting
(FR-054). Recomputed in `prepare()` and on every `setCloudSize()`; below `size < 0.001` the network
bypasses (`:344`) and `cascadeSec` degrades correctly to 0.

**This formula was simulated end-to-end this session** (delay + 8 single-delay-line Schroeder allpasses
at `kAllpassCoeff = 0.618` and the exact `kBaseDelayMs·size·ratio` delays + one-pole LP + DC blocker +
`fb`), and two things came out that the spec does not state and the implementer must not rediscover as a
bug:

**(a) The RT60 must be measured by Schroeder backward energy integration (EDC), not by a peak- or
RMS-envelope regression.** Measured error of the *correct* implementation, `cloudDamping = 0`, broadband
impulse:

| config | requested | peak-env | rms-env | **EDC** |
|---|---|---|---|---|
| size 1.0 | 30 s | 22.7 s (−24 %) | 26.2 s (−13 %) | **31.9 s (+6.2 %)** |
| size 1.0 | 10 s | 8.2 s (−18 %) | 9.2 s (−8 %) | **10.6 s (+6.4 %)** |
| size 1.0 | 2 s | 2.1 s (+3 %) | 2.2 s (+7 %) | **2.2 s (+11.5 %)** |
| size 0.0 | 30 s | **14.0 s (−53 %)** | 42.9 s (+43 %) | **27.1 s (−9.7 %)** |
| size 0.0 | 10 s | 5.6 s (−44 %) | 14.1 s (+41 %) | **8.9 s (−11.2 %)** |
| size 0.0 | 2 s | 1.4 s (−29 %) | 2.8 s (+37 %) | **1.8 s (−11.9 %)** |
| size 0.5 | 30 s | — | — | **29.2 s (−2.7 %)** |

A peak-envelope regression reports **−53 %** on a perfectly correct implementation, because the peak of a
repeatedly-lowpassed pulse decays faster than its energy (the N-fold convolution of the damping
filter's impulse response spreads the pulse). SC-008 says "linear regression on the log-envelope"; the
plan pins that to **EDC** and records why. Every EDC figure above is inside SC-008's ±15 %.

**(b) SC-008 must be measured at `cloudDamping = 0.0`.** At `cloudDamping = 1` (an 800 Hz in-loop LP)
the measured EDC T60 at a requested 30 s is **1.6 s (−95 %)**: the damping filter, not `fb`, sets the
tail. That is the damping control doing its job, not a defect — but the RT60 *accuracy* claim cannot bind
there. FR-052's accuracy statement is about the loop-**time** derivation.

**(c) The one grid point that does not clear ±15 %** is `(0.5 s, cloudSize = 1.0)`: measured **+30.9 %**.
At `loopSeconds = 93.9` ms that is 5.3 loop traversals — right on FR-052's own "binds at ≥ 4×
loopSeconds" boundary, where a −5…−35 dB regression has ~5 decay steps to fit.

**This is a blocking open item (OQ-A), not a plan decision.** FR-052 names the sanctioned response to a
miss in one sentence — "the prescribed response is to **calibrate `fb` against a measured tail at
configure time**, never to widen SC-008" — and the Success Criteria preamble allows the plan to rename but
"may not weaken a threshold". Deleting the failing grid point is neither a calibration nor a rename; it
is a **reduction in coverage at the shortest decay setting**, which is also the setting where a
mis-derived `cascadeSec` is most visible. The plan therefore **does not adopt it as a default**.
**T8 (§19) is gated**: before the decay cloud ships, either

- **(a)** implement the decay-dependent `fb` calibration FR-052 prescribes — the bias is a
  few-traversal artefact rather than a constant loop-time error, so this is more than the single
  `kCascadeDelayFactor` constant; or
- **(b)** obtain explicit user sign-off that SC-008's `cloudSize = 1.0` grid starts at 2 s, recorded in
  §17 (D-5) with the date and the decision.

`kCascadeDelayFactor` remains the sanctioned constant lever — never a wider tolerance. Measured reference
for either path: the 0.5 s point at `cloudSize = 0.0` is 13.5 traversals and measures **−0.1 %** with a
band-limited probe, **−7.1 %** broadband, so the shortest-decay coverage is retained there regardless.

### 9.3 Cloud controls and bypass (FR-053, FR-053a)

- `setCloudMix(m)` — equal-power parallel blend of cloud against the dry resonator output
  (`equalPowerGains`, `crossfade_utils.h:64`), smoothed at `kMixSmoothMs`.
- `setCloudDamping(d)` — `fc = kCloudDampMaxHz · (kCloudDampMinHz/kCloudDampMaxHz)^d`
  (geometric, 18 kHz → 800 Hz), applied to both `OnePoleLP`s, smoothed at `kCloudSmoothMs` **in the
  log-frequency domain**.
- `setCloudSize(s)` — `diffusion.setSize(s · 100.0f)`, and recompute `loopSeconds`/`fb`.
- `setWidth(w)` — `diffusion.setWidth(w · 100.0f)` (FR-062).
- **Bypass:** when `cloudMix < kCloudBypassEpsilon` **and** `cloud_.lastPeak < kCloudSilenceFloor`, the
  entire cloud path is skipped for that chunk and the dry resonator output passes through. Re-entry is
  ramped by `setCloudMix`'s own smoother, so it cannot click; and the tail is never truncated to save
  CPU, because the energy test gates the bypass independently of the mix.

**`prepare()` sequencing hazard (§0.1 trap 4).** `DiffusionNetwork::prepare` leaves `size_` at 50 % and
`reset()` snaps the smoothers to *that*, and its internal smoothers run at
`kDiffusionSmoothingMs = 10.0f` (`diffusion_network.h:48`). `ContinuousBody::prepare` must therefore, in
order:
`diffusion.prepare(float(sr), kControlChunkSamples)` → `setSize`/`setDensity`/`setWidth`/`setModDepth(0)`
→ `diffusion.reset()` **again**, so the 10 ms internal smoothers snap to the configured targets rather
than gliding for the first 480 samples of every render. `reset()` also clears every stage, which is what
`prepare` wants.

### 9.4 Boundary against Phase 6 (FR-055)

No pitch shifting, no spectral stage, no freeze / unity-feedback mode, no cross-channel matrix. Those are
`AetherReverb` (roadmap 256–272); adding any of them here violates N-3.

---

## 10. Output stage (FR-060 – FR-063)

- **FR-060 `setMix(m)`** — equal-power: `out = gDry·in + gWet·(body + cloud)`, `gDry/gWet` from
  `equalPowerGains(m)`. 0 = input passed through unchanged, 1 = body+cloud only. Default 1.0.
- **FR-061 — deleted.** There is no level control in this component. SC-007 measures raw output.
- **FR-062 stereo re-expansion** — the mono resonator output is written to both channels before the
  cloud; the cloud's own decorrelation (`kCloudLoopMsL/R`, mutually near-coprime) plus
  `DiffusionNetwork::setWidth` (`:288`) supplies the width.
- **FR-063 `setResonatorBypass(b)`** — the mono-summed input is scaled by `cloudDrive = rmsGain·userDrive`
  (**no `1/Ĝ`**) and feeds the cloud directly; **no** resonator engine is advanced, so
  `getEngineSampleCount` stays flat for every engine (SC-016). Toggling ramps over `kSlotReleaseMs` with
  the equal-power law and `silence()`s the engine at zero gain — the same mechanism as FR-024a's collapse.
  On un-bypass, **§10.1's re-tune runs before the ramp back to unity.**

### 10.1 `silence()` on the waveguide MUST be followed by `retune()` (blocker)

`WaveguideString::silence()` sets `bridgeDelayFloat_ = 0.0f` (`waveguide_string.h:243`), and
`process()` early-returns `0.0f` whenever `bridgeDelayFloat_ < static_cast<float>(kMinDelaySamples)`
(`:156`). `bridgeDelayFloat_` is written in exactly three places in the amended header — `silence()`
(`:243`), `noteOn()` (`:325`) and RA-1's `retune()` (§2) — a fact §0 row 25 already records but which an
earlier draft of this plan did not act on. **A silenced string therefore outputs digital silence forever**
unless something rewrites that field.

§8.3's control-step retune does **not** rescue it: `retune` fires only when `pitchDirty` (|Δ| > 0.5 cents
against `appliedBodyHz`), and none of the three silence paths moves the pitch. The only path that
recovers today is a material reassignment, because §6.2's Waveguide assignment calls `noteOn`.

This is on tested ground — SC-012(iii) toggles `setResonatorBypass` both ways and SC-016 asserts counts
after bypass, both with Strings among the five materials — so the defect would have shipped as a silent
Strings engine that every existing criterion passed.

**Every path that calls `silence()` on the waveguide slot must call `waveguide_.retune(f_body)` before the
equal-power ramp back to unity.** The three paths, exhaustively:

| # | Path | Required addition |
|---|---|---|
| 1 | §7.8.3 step 1 — FR-038a state recovery | `retune(f_body)` as the **first** step of the ramped re-entry |
| 2 | §10's FR-063 un-bypass, when the sounding slot's `engine == Engine::Waveguide` | `retune(f_body)` before the ramp to unity |
| 3 | §13 `reset()` | clear the dirty shadows — `appliedBodyHz = 0.0f`, `appliedT60 = 0.0f`, `appliedS = 0.0f` — so the **next** control step's dirty gate fires unconditionally and re-tunes. (A bare `retune()` in `reset()` would also work, but clearing the shadows is the mechanism already present and it fixes the comb/modal apply paths in the same stroke.) |

The §6.2 fade-completion `silence()` and FR-024a's collapse `silence()` need no addition: they free the
slot, and the next thing to touch it is a material assignment, which calls `noteOn` (`:325`).

Assertion added to §15.2: SC-012 and SC-016 both require Strings output RMS after a bypass round-trip to
be **non-zero** — without it the defect stays invisible.

---

## 11. `processStereoBlock` — the control-grid contract (FR-005, FR-005a)

```cpp
void processStereoBlock(const float* inLeft, const float* inRight,
                        float* outLeft, float* outRight,
                        std::size_t numSamples) noexcept;
```

Guards, in order: null `inLeft`/`inRight`/`outLeft`/`outRight` → **immediate return, no write**;
`numSamples == 0` → no-op and **no control step is consumed**; `!prepared_` → write silence and return
(the `harmonic_cloud.h:887-891` idiom). In-place operation is **not supported** and is documented as such.

State: `std::uint64_t sampleCounter_` (cleared only by `prepare()`/`reset()`),
`double chunkSumSq_`, `std::size_t chunkCount_`, `bool chunkPoisoned_`.

```
done = 0
while done < numSamples:
    toGrid   = kControlChunkSamples - (sampleCounter_ % kControlChunkSamples)
    subChunk = min(numSamples - done, toGrid, cloudChunkCap_)

    // 1. accumulate: mono-sum, finiteness scan, Sigma x^2 — CARRIED across calls.
    //    The guard is APPLIED IN THE LOOP, never repaired afterwards.
    for s in [0, subChunk):
        m = 0.5f * (inLeft[done+s] + inRight[done+s])
        if !isFiniteBits(m):
            chunkPoisoned_ = true
            m = 0.0f
        if chunkPoisoned_: m = 0.0f          // sticky for the rest of the control chunk
        mono[s] = m
        chunkSumSq_ += double(m) * double(m)
    chunkCount_ += subChunk
    if chunkPoisoned_: chunkSumSq_ = 0.0     // ASSIGNMENT, never a subtraction

    // 2. advance the engines / crossfade mix / cloud over exactly subChunk samples,
    //    using the coefficients latched at the LAST control step
    renderSub(mono, outLeft+done, outRight+done, subChunk)

    sampleCounter_ += subChunk
    done           += subChunk

    // 3. control step fires ONLY on the absolute 64-grid, never on a sub-64 tail
    if (sampleCounter_ % kControlChunkSamples) == 0:
        chunkRms = sqrt(chunkSumSq_ / double(chunkCount_))    // chunkCount_ == 64 here
        controlStep(chunkRms)
        chunkSumSq_ = 0.0; chunkCount_ = 0; chunkPoisoned_ = false
```

**Two things about step 1 that an implementer must not reinterpret.**

- **The poison is substituted at the point of accumulation, not repaired retroactively.** An earlier draft
  accumulated `chunkSumSq_ += m·m` for *every* sample including the poisoned one and then said "zero the
  carried `chunkSumSq_` contribution". That is unimplementable as stated: read as a subtraction of this
  sub-chunk's contribution it leaves `NaN − NaN = NaN` or `Inf − Inf = NaN`, and `chunkRms` then flows
  straight into `rmsFollower_.processSample`, which is documented "Does NOT validate input"
  (`envelope_follower.h:163-164`) and latches permanently (§7.6). The clause is now literal:
  substitute `m = 0.0f` **before** the accumulate, and on poison **assign** `chunkSumSq_ = 0.0`.
  `chunkCount_` is left intact, so the control step sees `chunkRms == 0` — the AGC sees a gap, which is
  exactly what FR-038 asks for, and nothing non-finite ever reaches the follower.
- **`chunkPoisoned_` is sticky for the remainder of the control chunk** (it is cleared only at the control
  step), so the poisoned sub-chunk **and every later sub-chunk of the same control chunk** are zeroed.
  Sub-chunks of that control chunk that were **already rendered** in an earlier `processStereoBlock` call
  are *not* retroactively zeroed — they cannot be; they have already left the component. This is a
  deviation from FR-038's normative "the **whole chunk** is replaced by zeros before it reaches the drive
  stage", recorded as **D-10**. The alternative — buffering the whole control chunk before rendering —
  costs up to 63 samples of latency and directly contradicts FR-005a's 0-sample guarantee, so it is not
  viable. SC-013(a) is amended in §15.2 with a partition × poison cross-case that pins the resulting
  behaviour instead of leaving it undetected between SC-011 (varies the partition, injects no poison) and
  SC-013(a) (injects poison at a single partition).

`controlStep` runs **in this fixed order** (FR-005a clause 3):

1. RMS-follower advance (FR-034) → `rmsGain`
2. `Ĝ` recompute for the sounding slot, gated (FR-032)
3. drive recompute + `driveLog10.advanceSamples(64)` (FR-033)
4. key-track retune: pitch smoothers advance, `f_body`, dirty gates, `updateModes`/`setFundamental`/`retune` (FR-040–FR-042)
5. resonance/damping apply, same dirty gate (FR-036, FR-042a)
6. crossfade / collapse position advance (FR-024, FR-024a)
7. decay-cloud **coefficient update only** — `fb`, the damping cutoff, and the size-smoother advance
   (FR-050). **The cloud's read / diffuse / damp / DC-block / write pass does NOT run here**; it runs
   inside `renderSub` over `subChunk ≤ min(64, cloudChunkCap_)` samples (§9.1). `controlStep` fires only
   on the absolute 64-grid, so running the loop here would emit no cloud output for any sub-64 tail —
   the 1023+1, 100+…+24 and 7×146+2 partitions SC-011 exists to catch.
8. cloud-bypass evaluation (FR-053a)
9. `stateFinite()` evaluation and, on false, the FR-038a recovery (§7.8.3)

**No input or output is buffered: the component's latency is 0 samples at every block size.** A control
chunk split 36 + 28 by a block boundary yields exactly the same `chunkRms` as an unsplit 64, and a
1023 + 1 partition steps the controls at the same absolute sample indices as a single 1024 — this is the
mechanism SC-011 tests, at the *same* tolerance as the 64-multiple partitions.

Because `subChunk ≤ 64` by construction, every scratch buffer is a fixed
`std::array<float, kControlChunkSamples>` member. **No fixed-size stack buffer may assume a maximum block
size** — a 32 768-sample host block is 512 sub-chunks, not one frozen control frame.

---

## 12. Determinism and seeding (FR-070 – FR-072)

`void setSeed(std::uint32_t seed) noexcept` — configure-time only; takes effect at the next control
step; not required to be retro-deterministic (documented). Seed 0 is passed through to `Xorshift32`,
which substitutes its own `kDefaultSeed` (`random.h:45-46`).

**The seed drives exactly one thing (FR-070a): per-voice modal micro-detune.** At `setModes`/`updateModes`
time, mode `k`'s frequency is multiplied by `exp2f(j_k · kSeedDetuneCents / 1200.0f)` where
`j_k ∈ [−1,1]` comes from `Xorshift32(deriveStreamSeed(seed_, k)).nextFloat()` (`random.h:102-111`,
`:59-63`). Recomputed only when the seed or the mode set changes — configure-time cost only.
It applies to the **three modal materials only**.

Everything else this phase configures is deterministic: the bank's scatter is a fixed golden-ratio
displacement (`:577-578`, `:729`), `DiffusionNetwork`'s LFO is a deterministic sine and is off by
default, `WaveguideString`'s RNG feeds only the note-on burst injected at velocity 0
(`velScale = 0`, `:393`/`:446`), and the comb bank's RNG is hard-seeded (`:429`, `:450`). **Strings and
Chamber are therefore documented seed-independent** (FR-071) and SC-010 asserts that asymmetry in both
directions rather than papering over it.

`deriveStreamSeed`'s non-zero substitution is load-bearing: two lanes hashing to 0 would collapse onto
one stream (`random.h:98-101`).

---

## 13. prepare / reset / RT-safety contract (FR-002, FR-004)

**`prepare(double sampleRate)`** — the only method permitted to allocate; not `noexcept`-required but
written `noexcept`. Order:

1. `sampleRate_ = (sampleRate > 1.0) ? sampleRate : 1.0` (the `harmonic_cloud.h:283` idiom).
2. `modal_[0].prepare(sr)`, `modal_[1].prepare(sr)`, `waveguide_.prepare(sr)` (allocates 50 ms = the
   20 Hz worst case, `:105-122`), `comb_.prepare(sr, 50.0f)`.
3. Cloud: `delayL.prepare(sr, kCloudLoopMsL·1e-3f)`, `delayR.prepare(sr, kCloudLoopMsR·1e-3f)` — sized
   for **their own loop only**; the `DiffusionNetwork` allocates its own per-stage buffers (≈16.9 ms
   each, `:202-205`) and its ~57–64 ms of *throughput* delay is distributed across those stages.
   **The two figures are different quantities and must not be added into one buffer size** (C-3).
   Then `diffusion.prepare(float(sr), kControlChunkSamples)` → configure → `diffusion.reset()` (§9.3);
   `dampL/R.prepare(sr)`, `dcL/R.prepare(sr, kCloudDcCutoffHz)`.
4. `rmsFollower_.prepare(sr / double(kControlChunkSamples), 1)` + mode/attack/release (§7.6).
5. `configure()` every smoother at `sr` with its FR-009 time.
6. `loopSamplesL/R` — **`lround(kCloudLoopMs_ch · 1e-3 · sampleRate_)`, the delay-line portion only,
   never derived from `loopSeconds_ch`** (§9.1; `DelayLine::read` clamps silently at
   `delay_line.h:212-218`, so an over-long index is a wrong loop time with no fault) — then the
   `maxDelaySamples()` assertions, then `loopSecondsL/R` (= `kCloudLoopMs_ch·1e-3 + cascadeSec_ch`,
   the *acoustic* quantity, §9.2), `fbL/R`, `cloudChunkCap_`.
7. `reset()`.
8. `prepared_ = true`.

Repeatable. **Parameters are not restored** — a configured body stays configured across a sample-rate
change (FR-009). A `prepare()` during a crossfade abandons it: the incoming material becomes current at
full gain, all engines are silenced.

**`reset() noexcept`** — clears every engine (`silence()`), delay line, `DiffusionNetwork`, damping
filter, DC blocker, `rmsFollower_.reset()`, `sampleCounter_`, `chunkSumSq_`, `chunkCount_`,
`chunkPoisoned_`, `engineSampleCount_[]`, `clampCount_`; **snaps every smoother to its target**; leaves
parameters unchanged; abandons any crossfade the same way `prepare` does. RT-safe.

**And it clears every applied-value shadow** — `appliedBodyHz = 0.0f`, `appliedB1 = appliedB3 = 0.0f`,
`appliedT60 = appliedS = 0.0f`, `appliedCombFb[] = {}`, `appliedCombDamp = 0.0f` — so the next control
step's FR-042/FR-042a dirty gates fire **unconditionally**. This is not tidiness: it is §10.1 path 3.
`silence()` on the waveguide sets `bridgeDelayFloat_ = 0.0f` (`waveguide_string.h:243`) and `process()`
early-returns 0 below `kMinDelaySamples` (`:156`), so without the shadow clear a `reset()` leaves Strings
permanently silent — `pitchDirty` would be false (the pitch did not move) and `retune` would never fire.
The same clear re-applies `setFundamental`/`setDecay`/`setCombFeedback` on the comb and waveguide paths,
which is correct after a `silence()` anyway.

The clearing set here and §7.8.3's FR-038a recovery set are deliberately **the same set**, so a state that
one clears and the other does not cannot exist.

**Freshly-prepared state (FR-009), assertable as one line:** material Glass, resonance 0.7, damping 0.0,
keyTracking 1.0, noteHz 220, drive 1.0, mix 1.0, cloudMix 0.25, cloudDecaySec 4.0, cloudSize 1.0,
cloudDamping 0.3, width 1.0, AGC **on**, resonator bypass **off**, seed 1. At that state Glass has
`b1_eff = 0.50·40^0.3 = 1.5122` → **T60 = 4.57 s**, so the component is audible without configuration.

**RT safety.** Every method except `prepare()` is `noexcept`, allocation-free, lock-free, IO-free. No
`std::function`, no virtual dispatch on the hot path (engines are held as concrete members; `IResonator`
is used only as documentation of the shared lifecycle contract — the slot machinery dispatches on the
`Engine` enum, not through a base pointer, because `TimeVaryingCombBank` does not implement `IResonator`
anyway, `:81`). Setters called from a non-audio thread concurrently with `processStereoBlock` are
**not** supported — single-threaded like every other KrateDSP component, documented, not defended
against with locks.

**Introspection surface (FR-007)** — public, non-`#ifdef`, exactly the 13 accessors the spec's table
lists, no more (a success criterion may not assert on a quantity absent from it):
`getMaterial`, `getActiveModeCount`, `getModeFrequencyHz`, `getBodyFrequencyHz`, `getEngineT60Sec`,
`getDriveGain`, `getInputRms`, `getSteadyStateGainBound`, `isCrossfading`, `getCrossfadePosition`,
`getCloudFeedbackGain`, `getCloudLoopSeconds`, `getClampEngagementCount`, `getEngineSampleCount`,
`stateFinite`.

---

## 14. CPU budget (SC-005) — cost model, gate shape, ordered levers

### 14.1 Gate shape (reproduced from `harmonic_cloud_perf_test.cpp:80-151`, `:412`)

```cpp
constexpr double kBlockBudgetNs      = (512.0 / 48000.0) * 1e9;          // 10,666,667 ns
constexpr double kRegressionFactor   = 1.5;
constexpr double kReference1PctNs    = kBlockBudgetNs * 0.01;            // 106,667 ns
constexpr double kReferenceHalfPctNs = kBlockBudgetNs * 0.005;           //  53,333 ns
```

Each of the four baselines carries **both** compile-time clauses plus the relative runtime gate:

```cpp
static_assert(kBaselineNsPerBlock * kRegressionFactor <= kReferenceNsPerBlock, "…");
static_assert(kBaselineNsPerBlock <= kReferenceNsPerBlock / kRegressionFactor, "…");
REQUIRE(measured <= kBaselineNsPerBlock * kRegressionFactor);
```

The two compose: a baseline that would let `measured` exceed the reference does not **compile**, so the
runtime `REQUIRE` transitively binds the absolute figure on every machine. `[.perf]` keeps the *timing*
out of CI; the `static_assert`s are evaluated by every CI leg regardless of tags — which is why the gate
is placed there. The percent-of-core figure is **reported via `WARN`**, never asserted.

| Configuration | Reference |
|---|---|
| steady state — one material, cloud active, no crossfade, static parameters | 53,333 ns (0.5 %) |
| operating point — every setter stepped once per 64-sample chunk, note gliding | 53,333 ns (0.5 %) |
| crossfade window — two engines (FR-024/FR-024a cap) | 106,667 ns (1 %) |
| cloud only — `setResonatorBypass(true)` | 53,333 ns (0.5 %) |

**Each baseline is pinned to the most expensive material's measured number and every material is
`REQUIRE`d against it** (Q8). §8.3 establishes that this is **Metal Plate** (29 modes at 220 Hz vs
Glass's 11). Additionally the cheapest material must measure `≤ 0.7 ×` the most expensive.

### 14.2 Cost model (per 512-sample block, 48 kHz, 8 control chunks)

| Item | Per block | Note |
|---|---|---|
| Modal SIMD kernel | 512 samples × 29 modes (MetalPlate @ 220 Hz) | ≈ 5 ops/mode/sample; Phase 2 measured 64 SIMD partials + drift at ~21–25 k ns/block, so ~29 modes should land well under 15 k ns |
| Modal coefficient smoothing | 8 × 3 × 29 mul-add | `smoothCoefficients` once per `processBlock` (`:357`) |
| `applyTransientEmphasis` | 512 × (1 exp-free one-pole + 2 flops) | unavoidable (§0.1 trap 1) |
| `applyOutputStage` | 512 × (1 mul + `softClip`) | `softClip` is the rational form (`dsp_utils.h:105-113`), ~5 flops |
| `updateModes` (gated) | 0 in steady state; ≤ 8 × 29 × (1 sqrt + 2 sin + 1 exp) at the operating point | **the single largest lever** — FR-042a's dirty gate |
| `Ĝ` recompute (gated) | ≤ 8 × 29 × (2 sqrt + ~10 flops) | same gate |
| Diffusion cascade | 512 × 8 stages × 2 ch × (1 `readAllpass` + 4 flops) | RA-4 removes 512×8×1 `std::sin` per block per instance |
| Cloud delay + damp + DC | 512 × 2 ch × ~8 flops | |
| Drive / smoothers | 8 × (~6 `advanceSamples` + 1 `exp2`) | O(1) per chunk, not per sample |

RA-4 alone removes **4096 `std::sin` calls per 512-sample block** from the cloud — on the order of
20–40 k ns/block on a typical x64 part, i.e. most of the "cloud only" budget by itself.

### 14.3 Ordered levers if a baseline does not fit

**Reduce cost, never raise the baseline** (`harmonic_cloud_perf_test.cpp:82-85`). In order:

1. **FR-042a's relative dirty gate** — verify it is actually firing; a bug that dirties every step costs
   ~128 transcendentals/chunk.
2. **RA-4's fast path** — verify `modDepth` reaches exactly 0 through the smoother.
3. **FR-053a's cloud bypass** — verify it engages at `cloudMix = 0` with a settled loop.
4. **Hoist `Ĝ` behind the same dirty flag as `updateModes`** (they share inputs — do not compute `Ĝ`
   unconditionally).
5. **Lower `kNyquistHeadroomOct` from 1.0 to 0.5** — the count is then chosen at `√2·f_body` instead of
   `2·f_body`, which admits more modes at every pitch and is therefore *not* a CPU lever; the lever is the
   opposite direction, **raising** it (e.g. to 1.585 = a minor tenth) to truncate harder. Either way it
   trades specified glide headroom for CPU and must be justified in the header — see §20 OQ-D.
6. Only then escalate to the user. `kModeCountCeiling` is **fixed at 32** (OQ-2) and is not a lever.

---

## 15. Test plan

### 15.1 Files (all new)

| File | Target | Contents |
|---|---|---|
| `dsp/tests/unit/systems/continuous_body_test.cpp` | `dsp_systems_tests` | SC-001, SC-002, SC-004, SC-006, SC-007, SC-009, SC-010, SC-011, SC-012, SC-013, SC-016, plus the three **FR-level** cases the criteria do not cover: `ContinuousBody_ModeCountTruncation` (FR-043), `ContinuousBody_ControlSurfaceDefaults` (FR-006/FR-009), `ContinuousBody_OutputStageEndpoints` (FR-060/FR-062) |
| `dsp/tests/unit/systems/continuous_body_spectral_test.cpp` | `dsp_systems_tests` | SC-003 (a–d), SC-008, SC-015 — everything that needs an FFT / EDC |
| `dsp/tests/unit/systems/continuous_body_perf_test.cpp` | `dsp_systems_tests` | SC-005, all `[.perf]`-tagged |
| `dsp/tests/unit/processors/waveguide_string_retune_test.cpp` | `dsp_processors_tests` | SC-009(c), SC-014 RA-1 clause |
| `dsp/tests/unit/processors/diffusion_network_zeromod_test.cpp` | `dsp_processors_tests` | SC-014 RA-4 clause |

Splitting the spectral/EDC cases out keeps the FFT and the 65 s renders in a TU that can be run
independently, and keeps `continuous_body_test.cpp` from crossing 3 000 lines.

### 15.2 Per-criterion assertion strategy

| SC | TEST_CASE | Strategy, tolerances, seeds |
|---|---|---|
| **SC-001** | `ContinuousBody_SustainedDriveBounded` | Parameterised over 5 materials × 3 signals (white noise seeded `0x5E4A0001`, sine at `getBodyFrequencyHz()`, 20 Hz→8 kHz log sweep). `resonance 1.0, drive 4.0, cloudDecay 30, cloudMix 1.0`, 48 kHz, 60 s. Assert: peak ≤ 1.5; RMS(final 1 s) ≤ 1.10 × RMS(s 9–10); LSQ slope of `log10(RMS)` over four 15 s windows ≤ +0.025/window; `stateFinite()` true every block; `getClampEngagementCount()` delta **== 0** for all three signals (bracketed by subtracting the value read at render start). **PLUS the pre-clip headroom clause, which is the clause that actually discriminates** (§7.9): a companion probe render per material × signal at the same settings but `cloudMix = 0`, no crossfade in flight, so the output *is* the post-`applyOutputStage` engine sum — steady-state peak ≤ **0.730** for Glass / Metal Plate / Ice / Strings (⟺ pre-clip `|modeSum| ≤ 0.9`, by monotone inversion of `softClip`) and ≤ **1.8** for Chamber (⟺ `0.9 × kOutputClamp`; Chamber's comb has no output clipper, `timevar_comb_bank.h:593-651`). The counter clause is retained but is documented in the test as binding **only on Chamber** — for the other four materials it is structurally incapable of moving, since `softClip` bounds them to ±1 and two equal-power gains sum to at most √2. Edge-Case clause, restated the same way: `userDrive = 4` + `rmsGain` at max — on Chamber, `getClampEngagementCount()` may engage and must fall to 0 within 500 ms of `setDrive(1)`; on the other four, the headroom metric may exceed 0.730 and must fall back below it within 500 ms. |
| **SC-002** | `ContinuousBody_DecaysToSilence`, `ContinuousBody_MaterialSwitchNoInfiniteRing` | Continue each SC-001 render with exactly zero input. Final-block peak `< 1.0e-4` (the Membrum threshold, `plugins/membrum/tests/unit/processor/test_kit_switch_infinite_ring.cpp:59`) within **65 s** (`30 + 6.91/kMinB1 + 5`), uniform across materials. Second case: hit all five materials in rapid succession under sustained input, **including Glass → Ice → MetalPlate inside one 500 ms window** (the FR-024a collapse case), stop the input, same bound. |
| **SC-003 (a)** | `ContinuousBody_MaterialsDistinct` | 2 s band-limited noise, `f_body = 220`, `keyTracking 1`, `cloudMix 0`. Analysis pipeline stated once: last 1.0 s → one 8192-point Hann FFT → discard DC → 4096 bins → dB rel. own peak bin → clamp at −80 dB → map `(dB+80)/80`. Distance = **mean absolute difference per bin**. (a1) every one of the 10 cross-material distances ≥ 4 × the largest within-material distance (same material, two noise seeds); (a2) every cross distance ≥ 0.02. If the matrix misses, **change the FR-011a profiles**, never the thresholds. |
| **SC-003 (b)** | same | For every material, measured T60 non-decreasing in `setResonance` over `{0,0.25,0.5,0.75,1}` within 5 %/step; `extractAudioFeatures(mono, sr).centroidHz` (`tests/test_helpers/audio_features.h:37`, `:88`) non-increasing in `setDamping` over the same grid within 2 %, **and** strictly lower at `d=1` than `d=0` by ≥ 5 %. |
| **SC-003 (c)** | `ContinuousBody_MaterialCharacterOrdering` | T60 at `r = 0.8` within ±15 % of the derived table (MetalPlate 11.0 > Glass 6.61 > Ice 5.51 > Strings 3.83 > Chamber 1.20) and strictly ordered. Inharmonicity = **mean `|ratio_k − k|` over the first 8 detected peaks, unnormalised and uncapped**; required `Glass ≥ 5× MetalPlate`, `Ice ≥ 5× MetalPlate`, `MetalPlate ≥ 3× Strings`, `Strings ≤ 0.15`. Glass↔Ice is **not** ordered by this metric (§5.3: 8.19 vs 8.29); it is separated by requiring ≥ 6 of the first 8 peaks to differ by ≥ 2 % (measured 8.19 vs **8.95** with the shipped profiles; the ≥ 6 needed `scatter = 1.0`, see spec.md FR-012). |
| **SC-003 (d)** | same | `calculateSpectralFlatness` (`tests/test_helpers/signal_metrics.h:326`): Ice exceeds Glass by ≥ 0.02 at identical excitation and identical `resonance`/`damping`. |
| **peak detection** | shared helper in the spectral TU | `f0` = highest-magnitude peak below `1.5·f_body` in the 8192-point spectrum, refined by 3-point parabolic interpolation on **log** magnitudes. Autocorrelation / cepstrum / YIN are **excluded by name** (Glass/Plate/Ice have no harmonic series). Peaks = local maxima ≥ 8 bins apart within 40 dB of the render peak, frequency order, first 8. |
| **SC-004** | `ContinuousBody_GlideIsClickless` | Per material: sustained noise, `keyTracking 1`, glide 110→440 Hz linearly in log-f over 1.0 s. **Control-relative** (the `dsp/tests/unit/systems/harmonic_cloud_test.cpp:4817-4836` precedent): control = same material/excitation/seed/duration with the note **fixed at 110 Hz**. `ClickDetector` (`tests/test_helpers/artifact_detection.h:99`, `:130`) configured with **`sampleRate = 48000.0f`** (not the `44100.0f` default at `:39`, which would mis-scope every `timeSeconds` by 8.8 %), otherwise default 512/256/5.0. Assert `detections(glide) ≤ detections(control)`; `max|x[n]−x[n−1]|(glide) ≤ 1.5 × control`; **non-vacuity** (the two renders must differ); 20 ms-window RMS within ±3 dB of pre-glide. Absolute bounds are unusable here — a unit 440 Hz sinusoid already has `max|Δ| = 0.0576`. |
| **SC-005** | `ContinuousBody_CpuBudget` | `[.perf]`. Basis: **ns per 512-sample block**, best-of-25 × 500 blocks (the `harmonic_cloud_perf_test.cpp:191-193` shape, chosen for hybrid-core migration rejection). Four baselines (§14.1), each pinned to Metal Plate, all five materials `REQUIRE`d against each. Plus cheapest ≤ 0.7 × most expensive. Baseline provenance block (machine, build, trial shape, date, 8 consecutive runs) recorded in the TU exactly as `harmonic_cloud_perf_test.cpp:104-122` does. |
| **SC-006** | `ContinuousBody_NoAllocInProcess` | The in-repo bracketing idiom (`harmonic_cloud_test.cpp:4840-4900`): **liveness clause first and mandatory** — a deliberate `new int[16]` read through a `volatile int*` (defeats N3664 elision) must be counted, or the zero clause proves nothing because the operator overrides live in a different TU. Then `AllocationDetector::instance().startTracking()` → 200 × 512-sample blocks with every setter stepped per block, two material changes and a glide → `stopTracking()` → `REQUIRE(count == 0)`. **No Catch2 macro, no stream formatting, no vector growth inside the tracked window.** This TU must NOT include `allocation_operator_overrides.h`. |
| **SC-007** | `ContinuousBody_DriveNormalization` | Sine at `getBodyFrequencyHz()`, `cloudMix 0`, per material. **"Steady-state peak" defined once**: render `max(5.0 s, 3 × getEngineT60Sec())`, take the mean per-block peak over the **final 1.0 s**. Clauses: (i) across `resonance ∈ {0.2,0.5,0.8,1.0}` the steady-state peak varies ≤ ±3 dB about its own mean; (ii) that mean lies within **−20 dB … +3 dB** of `kTargetPeak = 1.0`; (iii) with `setInputAgcEnabled(false)` a 20 dB input **decrease** (amplitude 1.0 → 0.1) gives **−20 ± 1 dB** output change. **The direction is pinned and an upward step is not usable**: with the AGC off the drive is a fixed gain, so +20 dB lands straight in the engine, and §15's SC-015 note puts the nominal single-mode steady-state peak near `a₁` ≈ 0.246 (Glass) / 0.149 (Plate) / 0.362 (Ice). +20 dB puts Glass at ≈ 2.46 into `applyOutputStage` at threshold 1.0, where `softClip(2.46) ≈ 0.998` (`dsp_utils.h:105-113`) — a measured change of ≈ **+12 dB**, failing a ±1 dB clause on a fully correct implementation (FR-022a's own clipper, not the drive law). If an upward leg is wanted it must run from a **−20 dB reference** (0.1 → 1.0) so both endpoints stay below the clipper; the plan runs the downward leg only. (iv) with AGC on, a 20 dB input drop recovers to within 6 dB within 1 s and `getInputRms()` tracks the true windowed RMS within ±10 %. |
| **SC-008** | `ContinuousBody_CloudDecayAccuracy` | `setResonatorBypass(true)`, `cloudMix = 1`, **`cloudDamping = 0.0`** (§9.2b), broadband impulse. **T60 by Schroeder backward energy integration**: `E[n] = Σ_{m≥n} y[m]²`, block the `√E` at 512, convert to dB rel. its own max, LSQ over the −5…−35 dB span, `T60 = −60/slope` (§9.2a — a peak-envelope regression reports −53 % on a correct implementation and must not be used). Grid `{0.5, 2, 10, 30}` s at `cloudSize = 0.0`. At `cloudSize = 1.0` the grid is **`{0.5, 2, 10, 30}` s unless OQ-A is resolved in favour of `{2, 10, 30}` by recorded user sign-off, or the decay-dependent `fb` calibration lands** — the plan does **not** adopt the reduced grid as a default (§9.2c, §20 OQ-A, D-5). ±15 %. The 30 s case must still be above the noise floor at 20 s. |
| **SC-009** | `ContinuousBody_KeyTrackingLaw`, `WaveguideString_RetunePitchAccuracy` | (a) `getBodyFrequencyHz()` matches `referenceHz·(noteHz/referenceHz)^keyTracking` within **0.1 cent** over `keyTracking ∈ {0,0.25,0.5,0.75,1}` × `noteHz ∈ {55,110,220,440,880,1760}` — modal materials only (Chamber saturates at the comb bank's 1000 Hz clamp, `:521`, documented). (b) detected fundamental within **5 cents** of `f_body` for Glass/MetalPlate/Ice using the named estimator. (c) waveguide within **5 cents** over ±12 semitones from the `noteOn` pitch — the bound FR-081's frozen dispersion cascade permits. |
| **SC-010** | `ContinuousBody_SeedDeterminism` | Two instances, same seed/rate/parameter script → `compareFingerprints(fingerprintRender(a), fingerprintRender(b)).withinTolerance()` (`tests/test_helpers/render_fingerprint.h:64`, `:101`, `kSampleTolerance = 1e-4` at `:49`). **No bit-exact float golden anywhere.** Anti-vacuity, per material: Glass/MetalPlate/Ice — seeds 1 and 2 must **fail** `compareFingerprints`, and mode 8's detected frequency must differ by ≥ 0.5 cents; Strings/Chamber — seeds 1 and 2 must produce an **identical** render (the documented FR-071 limitation, asserted rather than ignored). |
| **SC-011** | `ContinuousBody_SampleRateInvariance`, `ContinuousBody_BlockSizeInvariance` | Rate: 44 100 / 48 000 / 96 000 from the FR-009 prepared state — T60 within ±10 %, detected fundamental within 5 cents, steady-state RMS within ±1 dB. Block: one 1024-sample render as **1×1024, 2×512, 16×64, 1023+1, 100+…+24, 7×146+2** — all six agree to `kSampleTolerance = 1e-4`, at the **same** tolerance. The first three are exact multiples of the control grid and cannot fail on a grid-alignment bug; the last three are what FR-005a's persistent counter and carried `Σx²` exist for. **Two sub-cases, not one.** (α) the settled render described above. (β) **the same six partitions with coefficients in flight** — a key-track glide plus a `setDamping` step running throughout, so every control step calls `updateModes`. (β) is mandatory: in (α) the targets never move, so `epsilon_/radius_/inputGain_` equal their targets from the start (`reset()` memcpies them at `modal_resonator_bank.h:199-201`) and `smoothCoefficients` is a no-op for the whole render — (α) alone is therefore **vacuous** for the block-rate-smoothing dependence §8.2 documents (`smoothCoefficients` runs once per `processBlock` call, `:357`, so the number of smoothing steps over 1024 samples is 16 for 1×1024 and 17 for 1023+1). If (β) exceeds 1e-4, do **not** widen the tolerance: escalate via OQ-E — either drive the modal bank on a partition-independent cadence (accumulate into a 64-sample scratch and call `processBlock` exactly once per *complete* control chunk, accepting and documenting the resulting latency) or record a **measured** deviation in §17. |
| **SC-012** | `ContinuousBody_CrossfadeClickless`, `ContinuousBody_RetargetClickless`, `ContinuousBody_ParameterSweepClickless` | (i) all 20 ordered material transitions; (ii) Glass→Ice→MetalPlate inside one 500 ms window and modal→waveguide→comb at 100 ms spacing; (iii) `setResonatorBypass` toggled both ways; (iv) every setter swept full range once per 64-sample block for 10 s. Measured **exactly as SC-004** — control-relative, `ClickDetector` at 48 kHz, `detections(test) ≤ detections(control)`, `max|Δ|(test) ≤ 1.5 × control`, non-vacuity. **Plus a liveness clause on (iii), material = Strings:** RMS over the 500 ms **after** the bypass round-trip must be ≥ 0.5 × RMS over the 500 ms **before** it, and strictly non-zero. Without it, §10.1's bricked-string defect (`silence()` zeroes `bridgeDelayFloat_`, `waveguide_string.h:243`; `process()` then early-returns 0, `:156`) passes every clickless criterion trivially — digital silence has no clicks. |
| **SC-013** | `ContinuousBody_NonFiniteInputRecovery`, `ContinuousBody_NonFiniteStateRecovery` + lint gates | Non-finite test inputs built **from bit patterns through a `volatile` sink** — never `std::numeric_limits::infinity()` / `quiet_NaN()`, which the macOS `-ffast-math` leg folds to finite garbage. (a) inject NaN/Inf for one block under sustained excitation → finite output, **no `silence()`**, unbroken tail: RMS over the following 100 ms within **±1 dB** of an un-poisoned control render, and the injection point passes SC-012's control-relative clauses. **(a2) partition × poison cross-case (D-10):** inject the *same absolute-sample-index* NaN under **1×1024** and under a **36+28-split** partition and require the two outputs to agree to `kSampleTolerance = 1e-4`, pinning the sticky-`chunkPoisoned_` semantics §11 defines. Without it SC-011 (varies the partition, injects no poison) and SC-013(a) (poisons one partition) never intersect and the sub-chunk substitution unit is unmeasured. (b) **restated so the premise is reachable** — the earlier draft drove ±1e38 for one block "until `stateFinite()` reports false" on the *engine* path, which cannot happen: `softClip(+Inf) = 1.0f` (`dsp_utils.h:107`), the modal predicate now reads `getModalEnergy()` and the waveguide loop is self-bounding (§7.8.2), so the observables never go non-finite there. Poison the path that can actually be poisoned: `setResonatorBypass(true)`, `setCloudDecaySec(30)`, `setCloudMix(1)`, `setDrive(4)`, then ±1e38 (bit-pattern-built) for one block — the FR-063 path scales by `cloudDrive` with **no** `1/Ĝ` and **no** FR-037 clamp, so ≈ 2e37 enters the cloud delay line and the feedback accumulation overflows within a few traversals. Assert: `stateFinite()` goes false; `cloudStateFinite()` is the predicate that failed (via the observable behaviour — the cloud is cleared, the engines are **not** silenced, FR-038a clause 2's discriminator); output finite at every sample; `stateFinite()` true again within **100 ms**; and — the clause whose absence made the latch permanent — with `setResonatorBypass(false)` restored, sustained excitation produces non-zero output, proving `rmsFollower_.reset()` ran (§7.6, §7.8.3 step 3). **(b2) follower-overflow regression:** feed ±1e38 with the resonator *active* (bypass off), for one block, and assert `stateFinite()` is **never** false and output RMS recovers to within ±1 dB of a control render within 100 ms — this is the case `kMaxFollowerInput` exists for, and without the clamp it fails permanently. Plus `node tools/check-portability.js`, `lint-layers.js`, `lint-arch-guarded-includes.js`, `lint-float-bit-goldens.js`, `lint-simd-aligned-loadstore.js`, `lint-odr.js` clean, and zero compiler warnings. |
| **SC-014** | `WaveguideString_RetuneIsInert`, `DiffusionNetwork_ZeroModIsBitIdentical` + the consumer suites | RA-1: a render that never calls `retune()` matches a pre-amendment reference under `compareFingerprints` at default tolerances (capture the reference by rendering with the pre-amendment header in the same session, or by asserting the amendment's containment structurally — see §20 OQ-B). RA-4: a `modDepth = 0` render is **bit-identical** before and after the guard — the one place a bit-exact comparison is legal, because it compares **the same binary's two code paths over the same inputs**, not two toolchains; a `modDepth > 0` render matches under `compareFingerprints`, **including across a `modDepth` 0 → 0.5 transition**, which is what proves the LFO phase kept advancing while the fast path was taken. |
| **SC-015** | `ContinuousBody_GainBoundValidAndTight` | Per modal material × `resonance ∈ {0.2,0.5,0.8,1.0}`, drive a unit sine at each of the first 8 mode frequencies (`getModeFrequencyHz(k)`); `measuredGain = steadyStatePeak / (getDriveGain() × inputAmplitude)` with `steadyStatePeak` the SC-007 definition. **Validity:** `measuredGain ≤ getSteadyStateGainBound()` at all 8. **Tightness:** at mode 1, `measuredGain ≥ 0.1 × getSteadyStateGainBound()`. The rejected flat-numerator form gives `measuredGain/Ĝ ≈ 0.029` and fails. §7.2's closed-form verification predicts the ratio at mode 1 will be near `g_1 / Σ g_k = a_1` (0.246 Glass, 0.149 Plate, 0.362 Ice) times an O(1) factor — all comfortably above 0.1. |
| **SC-016** | `ContinuousBody_OnlyActiveEnginesAdvance` | **Functional, not timing-based, NOT `[.perf]`-tagged**, so every CI leg evaluates it. 200-block render, no material change: `getEngineSampleCount(active) == numSamples`, `== 0` for every other engine. With one material change: the sum across engines equals `numSamples + (samples spent crossfading)`. **The FR-024a bound is asserted as exact arithmetic, not as "no third engine".** `getEngineSampleCount` is keyed by the `Engine` enum (3 values), while the collapse rule concerns **slots**, and the Glass → Ice → MetalPlate sequence is entirely `Engine::Modal` — so all three counts collapse onto one bucket, and since only two `ModalResonatorBank` instances exist (§6.1) a third modal engine cannot run *by construction*. A qualitative "no third engine" clause is therefore satisfied whether or not the collapse rule works. Instead: place every `setMaterial` call on an exact 64-sample control-chunk boundary and `REQUIRE` the exact total — `getEngineSampleCount(Modal) == numSamples + (t₂ − t₁) + round64(kSlotReleaseMs) + round64(kMaterialCrossfadeMs)` for the Glass(t₀) → Ice(t₁) → MetalPlate(t₂ < t₁+500 ms) sequence, where each term is the count of samples during which **two** slots were simultaneously advanced: `(t₂ − t₁)` for the interrupted fade, `kSlotReleaseMs` for the collapse (§6.2 step 3), `kMaterialCrossfadeMs` for the fade that follows. Every duration is a whole number of control chunks by construction, so **no tolerance is admitted**. A broken collapse rule — one that let the interrupted fade run to completion instead of collapsing — over-counts by ≈ `kMaterialCrossfadeMs − kSlotReleaseMs` = 490 ms of samples and fails. (Adding a per-slot `getSlotSampleCount(int)` accessor was rejected: FR-007 declares its list exhaustive, so it would need a spec amendment, and the arithmetic above is strictly more informative.) With `setResonatorBypass(true)` every count stays flat — **and, material = Strings, output RMS after the round-trip is non-zero** (§10.1). |
| **FR-043** | `ContinuousBody_ModeCountTruncation` | **New — FR-043 had no test anywhere, and `getActiveModeCount()` was asserted by no criterion.** FR-043 carries three distinct behavioural rules and a regression in any of them (truncating to a fixed count; *decreasing* the count mid-ring, the click the third rule exists to prevent) passes every other test in this plan; SC-005's per-material timing spread is corroboration, not proof (spec's own words). Assert, per Glass / Metal Plate / Ice: (i) `getActiveModeCount()` equals §8.3's computed table for `f_body ∈ {55, 110, 220, 440, 880}` Hz × `{44100, 48000, 96000}` Hz — the 48 kHz / 220 Hz row is 11 / 29 / 10, the 44.1 kHz row 10 / 27 / 10, the 96 kHz row 15 / 32 / 14, and the 55 Hz row 22 / 32 / 19; (ii) the count is **unchanged** across a retune that stays inside the one-octave headroom window; (iii) the count **never decreases** during an upward glide that leaves the window (sampled every control chunk over a 110 → 880 Hz glide), and may increase; (iv) `getModeFrequencyHz(k) == 0.0f` for every `k ≥ getActiveModeCount()` (FR-007's stated contract for the accessor). |
| **FR-006 / FR-009** | `ContinuousBody_ControlSurfaceDefaults` | **New.** (i) The freshly-`prepare()`d state asserted as the one line §13 says is assertable: material Glass, resonance 0.7, damping 0.0, keyTracking 1.0, noteHz 220, drive 1.0, mix 1.0, cloudMix 0.25, cloudDecaySec 4.0, cloudSize 1.0, cloudDamping 0.3, width 1.0, AGC on, bypass off, seed 1 — read back through the FR-007 surface where an accessor exists (`getMaterial`, `getBodyFrequencyHz` = 220 at keyTracking 1, `getEngineT60Sec` ≈ 4.57 s ± 5 %, `getCloudLoopSeconds`, `getCloudFeedbackGain`) and through rendered behaviour otherwise. (ii) **Every float setter is fed a bit-pattern NaN and a bit-pattern +Inf and −Inf** (built through a `volatile` sink — never `numeric_limits`, §15.3) and each observable must land on its FR-009 **Default**, not on 0 and not on `OnePoleSmoother`'s own NaN→0 / Inf→±1e10 fallbacks (`smoother.h:170-181`). (iii) each setter's range clamp is checked at both ends with an out-of-range finite argument. This is §6.4's uniform setter shape, and it is the case that would have caught its omission from an earlier draft. |
| **FR-060 / FR-062** | `ContinuousBody_OutputStageEndpoints` | **New.** Both output-stage setters had a design element and no functional test: SC-012(iv) sweeps them but measures clicklessness only, so a `setMix` that ignored its argument, or a `setWidth` wired to nothing, passed every test in the plan. (i) **FR-060 endpoint:** with `setMix(0)` settled (≥ 5 × `kMixSmoothMs`) under a sustained stereo signal, `outLeft[n] == inLeft[n]` and `outRight[n] == inRight[n]` within `kSampleTolerance = 1e-4` — exact and non-vacuous given FR-005's no-in-place rule; and `setMix(1)` under zero input with a ringing body gives non-zero output. (ii) **FR-062 endpoint:** `setResonatorBypass(true)`, `cloudMix = 1`, broadband excitation — the L/R Pearson correlation of the final 1 s must be ≥ **0.999** at `setWidth(0)` (`DiffusionNetwork` collapses to mid at `:385-391`, `sampleL = sampleR = mid`) and ≤ **0.95** at `setWidth(1)`, i.e. a correlation delta ≥ 0.049. Report the measured delta via `WARN` so a future tightening has a number to start from. |

### 15.3 Assertion hygiene applying to every test

- No `std::isnan` / `std::isinf` / `std::numeric_limits<float>::infinity()` / `quiet_NaN()` anywhere in
  component or test code — bit-pattern checks only.
- Any TU that injects non-finite values gets `-fno-fast-math -fno-finite-math-only` source properties in
  `dsp/tests/CMakeLists.txt`.
- No bit-exact float golden except the single sanctioned same-binary comparison in
  `DiffusionNetwork_ZeroModIsBitIdentical`; `node tools/lint-float-bit-goldens.js` must stay clean (add
  the justification comment the lint recognises).
- Catch2 filter is the **positional** case name (`dsp_systems_tests.exe "ContinuousBody_*"`), not `-c`.
- Long renders (65 s × 5 materials × 3 signals for SC-001/002) dominate wall time. Tag the 65 s
  infinite-ring sweeps `[.slow]` **only if** the user approves (§20 OQ-C) — the default is that they run
  in CI, matching Membrum's kit-switch pattern.

---

## 16. Build integration

### 16.1 Files that change

| File | Change |
|---|---|
| `dsp/tests/CMakeLists.txt` | add `unit/systems/continuous_body_test.cpp`, `unit/systems/continuous_body_spectral_test.cpp`, `unit/systems/continuous_body_perf_test.cpp` to `add_executable(dsp_systems_tests …)` (the systems list is at `:295-339`; **sources are listed explicitly, never globbed — a file not listed silently drops**) |
| same | add `unit/processors/waveguide_string_retune_test.cpp`, `unit/processors/diffusion_network_zeromod_test.cpp` to `dsp_processors_tests` |
| same | `set_source_files_properties(… PROPERTIES COMPILE_OPTIONS "-fno-fast-math;-fno-finite-math-only")` (non-MSVC) on `continuous_body_test.cpp` (SC-013 injects non-finite values) |

No CMake change is needed for the header itself — KrateDSP is header-only and `dsp/include` is already on
the interface include path.

### 16.2 Suites that must be built and run

```bash
CMAKE="/c/Program Files/CMake/bin/cmake.exe"

# Phase 4's own suites
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_systems_tests
build/windows-x64-release/bin/Release/dsp_systems_tests.exe "ContinuousBody_*" 2>&1 | tail -5
# perf cases are hidden behind [.perf]; run explicitly:
build/windows-x64-release/bin/Release/dsp_systems_tests.exe "ContinuousBody_CpuBudget*" 2>&1 | tail -20

# SC-014 — the amendment regression set. RA-1 touches SHARED DSP.
"$CMAKE" --build build/windows-x64-release --config Release \
    --target dsp_processors_tests dsp_effects_tests innexus_tests membrum_tests plugin_tests approval_tests
for t in dsp_processors_tests dsp_effects_tests innexus_tests membrum_tests plugin_tests approval_tests; do
  build/windows-x64-release/bin/Release/$t.exe 2>&1 | tail -3
done
# belt and braces, zero cost:
"$CMAKE" --build build/windows-x64-release --config Release --target disrumpo_tests
```

**`membrum_tests` is not optional.** `waveguide_string.h` is consumed by
`plugins/membrum/src/dsp/drum_voice.h:41` and `plugins/membrum/src/dsp/bodies/string_body.h:22` — Membrum
is the heaviest `WaveguideString` consumer in the repo, and a build stage that skips it has not verified
RA-1. `tools/membrum_preset_generator.cpp` links the same header and builds as part of the tools target.

RA-4's consumers: `dsp/tests/unit/processors/diffusion_network_test.cpp:14`,
`dsp/include/krate/dsp/effects/shimmer_delay.h:32`, `effects/freeze_mode.h:31`,
`plugins/iterum/src/processor/processor.h:27` → `dsp_processors_tests`, `dsp_effects_tests`,
`plugin_tests` **and `approval_tests`** (Iterum's golden-output suite; running only `plugin_tests`
silently skips golden-output regression coverage).

### 16.3 Gates before any commit

```bash
node tools/check-portability.js          # MSVC-green proves nothing about GCC/Clang
node tools/lint-layers.js
node tools/lint-arch-guarded-includes.js
node tools/lint-float-bit-goldens.js
node tools/lint-simd-aligned-loadstore.js
node tools/lint-odr.js
node tools/lint-allocation-operator-overrides.js
./tools/run-clang-tidy.ps1 -Target dsp -BuildDir build/windows-ninja
```

No pluginval run — this phase touches no plugin source. (Iterum's `approval_tests` is the RA-4 guard, not
a plugin-behaviour change.)

---

## 17. Recorded deviations from the spec (nothing silent)

| # | Spec text | Plan | Why |
|---|---|---|---|
| D-1 | FR-012: Glass "1.000, 2.83, 5.42, 8.77, 12.85, 17.65" | 1.0000, 2.8284, 5.4233, 8.7706, **12.8663, 17.7088** | The **law** is normative; the listed values are rounded. Confirmed by SC-003(c)'s own inharmonicity figure 8.19, which reproduces exactly from the law's values (computed: 8.1908). |
| D-2 | FR-012: Metal Plate "extended to 32" (method unstated) | Rossing's published 8 verbatim + a constant-modal-density linear continuation, slope 1.7309 LSQ-fitted over `k = 4…8`, anchored at `k = 8` | A plate's modal density is asymptotically constant (Cremer & Heckl; Fletcher & Rossing §3). The alternative in-repo generator (`tools/gen-plate-chladni.js`, P=1.7 κ=0.11) produces a **different first 8** and would move SC-003(c)'s 0.99 figure. |
| D-3 | FR-043 worked example: "Glass at `f_body = 220` Hz gets ~13 modes" | 11 | Computed with the exact tables and the bank's own stretch/scatter warps. No threshold moves; the spec figure was an estimate. |
| D-4 | SC-008: "linear regression on the log-envelope" | **Schroeder backward energy integration (EDC)**, pinned | Measured this session: a peak-envelope regression reports **−53 %** at (30 s, size 0.0) on a *correct* implementation. An RMS-envelope regression reports **+43 %** at the same point. Only EDC lands inside ±15 %. |
| D-5 | SC-008: grid `{0.5, 2, 10, 30}` at **both** size ends | **NOT ADOPTED as a default.** The reduced `cloudSize = 1.0` grid `{2, 10, 30}` is a *proposal* gated on OQ-A; the plan ships the full grid at both size ends unless OQ-A resolves otherwise. | (0.5 s, size 1.0) is 5.3 loop traversals and measures **+30.9 %**. FR-052 names calibration as the only sanctioned response to a miss ("never widen SC-008") and the SC preamble allows renaming but not weakening; **deleting the failing grid point is neither**, and it removes coverage precisely at the shortest decay, where a mis-derived `cascadeSec` is most visible. §19 gates T8 on either the decay-dependent `fb` calibration or recorded user sign-off. If sign-off is given, record the date and decision **in this row**. |
| D-6 | SC-008 does not state `cloudDamping` | Pinned to **0.0** | At `cloudDamping = 1` (800 Hz LP) the measured T60 at a requested 30 s is **1.6 s (−95 %)** — the damping filter sets the tail, which is its job. FR-052's claim is about the loop-**time** derivation. |
| D-7 | FR-003 include list ("permitted includes are exactly") | `core/dsp_utils.h` **dropped** (nothing calls `softClip`); `core/math_constants.h`, `primitives/dc_blocker.h` and **`core/db_utils.h`** added | FR-050 requires a DC blocker but the spec's include list omits `dc_blocker.h`. `math_constants.h` supplies `kHalfPi` used by `crossfade_utils.h`. `db_utils.h` supplies `detail::flushDenormal` (`:168`), which R-6's mitigation mandates on the cloud feedback write and which would otherwise resolve only transitively through `smoother.h:28`. All three are Layer 0/1 — no layer consequence. |
| D-8 | FR-050 delay-line class | `DelayLine::read(size_t)`, batched read-before-write, with a `cloudChunkCap_` guard | `DelayLine` has no batch API; the guard exists so a degenerate `prepare(1000.0)` cannot make the loop shorter than a control chunk (§9.1). |
| **D-9** | SC-001: `getClampEngagementCount()` is the sole discriminator between working drive compensation and permanent hard clipping | The counter clause is **kept** but is joined by a **pre-clip headroom clause** (steady-state peak ≤ 0.730 on a `cloudMix = 0` probe for the four soft-clipped materials, ≤ 1.8 for Chamber), and R-4's detector is re-pointed at it (§7.9) | FR-037's ±2.0 clamp is fed by engines already bounded to ±1.0 upstream — modal via `applyOutputStage`/`softClip` (`modal_resonator_bank.h:366`, `:789-796`; `dsp_utils.h:102-113`), waveguide via `softClip(junction)` (`waveguide_string.h:181`) — and two equal-power gains sum to at most √2. The counter is **structurally incapable of moving** for 4 of 5 materials, so the clause it anchored was unfalsifiable. The headroom clause is strictly **stronger** (it fails at 1.9 dB of engine compression, long before ±2.0) and needs no new accessor: `softClip` is monotone, so `softClip(0.9) = 0.72996` inverts the bound exactly. **This tightens, it does not relax.** |
| **D-10** | FR-038: "the **whole chunk** is replaced by zeros before it reaches the drive stage" | "the poisoned **sub-chunk** and the remainder of the control chunk are zeroed"; sub-chunks of the same control chunk already rendered in an earlier `processStereoBlock` call are not retroactively zeroed | The §11 walker renders per sub-chunk with **0 samples of latency** (FR-005a). When a control chunk is split by a block boundary (36 + 28) and the poison lands in the second sub-chunk, the first 36 samples have already left the component. The only way to honour the literal wording is to buffer the whole control chunk before rendering — up to 63 samples of latency, directly contradicting FR-005a's 0-sample guarantee. SC-013(a2) pins the resulting behaviour with a partition × poison cross-case so it is measured rather than assumed. |
| **D-11** | SC-007(iii): "a 20 dB input change gives 20 ± 1 dB output change" (direction unstated) | pinned to a 20 dB **decrease** (1.0 → 0.1, required −20 ± 1 dB); an upward leg, if wanted, must run from a −20 dB reference | With the AGC off the drive is a fixed gain, so +20 dB from unity lands at ≈ 2.46 (Glass) into `applyOutputStage` at threshold 1.0, where `softClip(2.46) ≈ 0.998` — a measured **+12 dB**, failing ±1 dB on a *correct* implementation. The clause is about the drive law being a gain; the bank's own clipper (FR-022a) is a different mechanism and must not be in the measurement path. No threshold moves. |
| **D-12** | FR-011a / A-1: Chamber profile field `stereoSpread = 0.6`, and A-1's "plus per-engine stereo spread where the engine offers it for free" | Field **dropped**; `setStereoSpread` is not called; Chamber's width comes solely from the decay cloud (FR-051, FR-062) | `stereoSpread_` is read only by `recalculatePanPositions` (`timevar_comb_bank.h:763`) → `panLeftGain`/`panRightGain` (`:769-770`), read only at `:715-716` inside `processStereo`. The mono `process(float)` (`:593-651`) never touches them, so the setting was **provably inert**. Routing Chamber through `processStereo` instead was rejected because it makes one material's engine output stereo while FR-037's clamp and its counter are pinned by FR-007 as mono ("the resonator core is mono, A-1"). **A correction is filed against FR-011a and A-1.** |

| **D-13** | FR-041 ("`updateModes()`, whose coefficients are then smoothed by the bank's own 2 ms one-pole") and FR-042a ("clicklessness comes from the modal bank's own 2 ms coefficient smoother") | `ContinuousBody::applyEngineRetune` calls the new, strictly-additive `ModalResonatorBank::snapCoefficients()` (`modal_resonator_bank.h:297-303`) immediately after `updateModes`, so the bank's per-block smoothing is an exact no-op and continuity is carried by the **64-sample control grid** instead. Retune stays state-preserving — `snapCoefficients` touches only `epsilon_`/`radius_`/`inputGain_`, never `sinState_`/`cosState_` — so FR-041's *normative* claim is untouched; only its statement of *which mechanism* supplies continuity changes. **A correction is filed against FR-041 and FR-042a.** | **This is the OQ-E resolution, and it is neither of OQ-E's two options.** Sub-case (β) missed by 710× as R-12 predicted (worst \|dL\| 7.096e-02 against 1e-4 — 1.4× the render's own peak, because ε differences integrate into phase drift). OQ-E's option (a) costs up to 63 samples of latency and contradicts FR-005a; option (b) records a deviation that guts SC-011 on the modal path. The snap costs **zero** latency, keeps the tolerance at 1e-4, and additionally removes the τ ≈ 128 ms coefficient lag §8.2 records as a correction to the spec's "2 ms". Continuity is not lost: the trajectory becomes a staircase whose **interval** is 1.33 ms at 48 kHz (i.e. finer than the 2 ms the spec believes it has) and whose **step size** is bounded by the FR-042/FR-042a dirty gates. A step in `radius_` changes a decay slope and a step in `epsilon_` an instantaneous frequency — neither is an output discontinuity, since the resonator states carry through untouched — and `inputGain_` provably cannot step here, because the FR-011a amplitude law is a function of the mode **index**, not of pitch or damping. **Measured after the fix: all six SC-011 partitions are bit-identical (0.000e+00) on both channels with coefficients in flight.** Side effect: the insertion shifts every `modal_resonator_bank.h` line ≥ 277 by **+27**; citations in `continuous_body.h`, `continuous_body_test.cpp` and `test_modal_resonator_bank.cpp` were updated, **the ones in this plan and in spec.md were not**. |

None of these relaxes a threshold. D-4/D-6 make SC-008 **measurable**; D-1/D-2/D-3 replace estimates with
computed values; **D-9 and D-11 make two SC clauses falsifiable that previously were not, and D-9 is
strictly tighter than the clause it supplements**; D-10 and D-12 record where the spec's literal wording
is unimplementable against its own FR-005a / A-1 / FR-037 constraints, with the corrections filed.
**D-5 is explicitly not adopted** — it is a gated proposal, not a plan decision.

---

## 18. Risks and mitigations

| # | Risk | Mitigation |
|---|---|---|
| **R-1** | **RA-1 touches shared DSP consumed by Innexus and Membrum.** A behavioural regression there is invisible to `dsp_systems_tests`. | The addition is inert unless called (FR-084) and touches no existing member. §16.2's suite list is **mandatory and includes `membrum_tests`** (the suite an earlier SC-014 draft omitted). `WaveguideString_RetuneIsInert` asserts the containment directly. |
| **R-2** | **RA-4's "bit-identical" claim.** If `modDepth` never reaches exactly 0 (smoother asymptote), the guard never engages and the CPU saving evaporates; if the compiler reorders, the claim breaks. | `OnePoleSmoother::process` snaps to target within `kCompletionThreshold = 1e-4` (`smoother.h:199-202`), so `modDepth` **does** reach exactly 0. `DiffusionNetwork_ZeroModIsBitIdentical` asserts the claim, including across a `0 → 0.5` transition (proves the LFO phase kept advancing). |
| **R-3** | **The decay-cloud RT60 formula misses.** Simulated errors span −12 % … +31 % depending on configuration. | §9.2's measured table pins the configuration in which it holds; D-4/D-5/D-6 scope SC-008 to it; `kCascadeDelayFactor` is the documented, checked-in calibration lever, with the rule "**calibrate `fb`, never widen SC-008**" recorded at the constant. |
| **R-4** | **`Ĝ` spans 3–5 decades between engines**; a shared linear drive smoother crossing that span over-drives the incoming engine by tens of dB. | Per-slot `driveGain`, **snapped** at assignment (legal — the incoming slot is at zero gain) and **log10-smoothed** for continuous moves (FR-033). **Detector: SC-001's pre-clip headroom clause** (steady-state peak ≤ 0.730 on the `cloudMix = 0` probe; ≤ 1.8 for Chamber) — **not** `getClampEngagementCount()`, which §7.9 proves cannot move for four of the five materials and which an earlier draft named here as the only detector. The counter is retained as a secondary detector, binding on Chamber. |
| **R-5** | **`applyTransientEmphasis` is an un-disableable time-varying input gain on the modal path** (`:359`, `:879-895`). It perturbs SC-007's "gain, not a compressor" clause and SC-015's tightness. | Analysed: ≈ **+0.06 dB** at unit amplitude for a steady sine, growing ≈ linearly with level (≈ +0.55 dB across a 20 dB step) — inside SC-007's ±1 dB. Documented in the header. If SC-007(iii) fails, the diagnosis is here, not in the drive law. |
| **R-6** | **Denormals in a 30 s tail.** Modal states, cloud delay contents and smoother states all decay toward 1e-30. | FTZ/DAZ is enabled process-wide by `dsp/tests/dsp_test_main.cpp:13` and by the plugin host path. The bank has `kSilenceThreshold = 1e-12` + `flushSilentModes` (`:383-396`); `OnePoleLP`, `OnePoleSmoother`, `LeakyIntegrator` and `TimeVaryingCombBank` all call `detail::flushDenormal`. The **cloud's own delay line has no flush** — add an explicit `flushDenormal` on the feedback write, and assert (SC-005) that the "cloud only" baseline does not degrade during a 30 s tail. |
| **R-7** | **`WaveguideString::noteOn` builds 32 KB of stack arrays** (`:405`, `:425`) on the audio thread at every Strings material assignment. | Existing behaviour, RT-legal (no allocation). Documented in the header comment at the assignment site so a stack-depth investigation lands on it immediately. Not amended — outside this phase's scope. |
| **R-8** | **`ModalResonatorBank::flushSilentModes` can cull a mode during a quiet passage**, after which `updateDampingLaw` would skip it forever (`:285-286`). | FR-041 routes **every** damping change through `updateModes` (`:264-275`), which rewrites every mode unconditionally (`:705-770`). `updateDampingLaw` is never called. Recorded as a code comment at the call site. |
| **R-9** | **Portability.** MSVC accepts what GCC/Clang reject; narrowing in brace init; `constexpr` from an SDK constant. | `node tools/check-portability.js` before every commit; designated initializers with explicit `f` suffixes for every `MaterialProfile`; no `std::isnan`; no SIMD added by this phase (so `lint-simd-aligned-loadstore.js` is a formality, but run it). |
| **R-10** | **SC-005's four baselines are machine-specific** and the first honest measurement may be over budget. | §14.3's ordered lever list, with RA-4 and FR-042a's dirty gate as the two big ones. The rule is stated at the constant: **reduce cost, never raise the baseline**. |
| **R-11** | **65 s × 5 materials × 3 signals** (SC-001 + SC-002) is ~16 minutes of audio per run at 48 kHz. | The renders are far faster than real time, but this is the dominant test cost. §20 OQ-C asks whether to tag the sweep `[.slow]`. |
| **R-12** | **`ModalResonatorBank`'s coefficient smoothing is block-rate, so its cadence depends on the host's block partitioning.** `smoothCoefficients()` runs once per `processBlock` call (`:357`) with a per-sample coefficient (`:182`, `kSmoothingTimeMs = 2.0f`, `:569`), and the §11 walker calls `processBlock` once per **sub-chunk** — 16 calls for 1×1024 from counter 0, 17 for 1023+1. Whenever coefficients are in flight the partitions produce different audio, which is what SC-011's `kSampleTolerance = 1e-4` block-size clause forbids. | §8.2 states the real figure (effective τ ≈ `kSmoothingTimeMs × subChunkSamples` ≈ 128 ms at 48 kHz, 1.04 % of the remaining delta per call) instead of the wrong "2 ms". SC-011's block-size case gains a **sub-case (β) with an in-flight retune and damping change** so the dependence is measured rather than hidden behind a settled render (where the `reset()` memcpy at `:199-201` makes the smoother a no-op and the criterion vacuous). Escalation if (β) misses: **OQ-E** — either a partition-independent modal cadence (one `processBlock` per complete control chunk, at the cost of latency) or a measured deviation in §17. **Never a wider tolerance.** |
| **R-13** | **A state observed by `stateFinite()` but not cleared by FR-038a's recovery set is an unrecoverable latch.** `EnvelopeFollower` is the concrete instance: `processRMS` squares in float (`:313`), overflows to Inf on a legal finite input above ~1.8e19, and only `reset()`/`prepare()` clears it (`:184-185`'s flush passes Inf through, `db_utils.h:168`). | Two independent guards. **(1)** `kMaxFollowerInput = 1e9` clamps the value handed to the follower so the overflow is unreachable (§7.6, with a `static_assert` in §4.2). **(2)** §7.8.3 states the rule explicitly and puts `rmsFollower_.reset()` + the `driveLog10` re-snap in the recovery set, and §13 makes `reset()` use the **same** clearing set so the two cannot drift. SC-013(b2) is the regression: ±1e38 with the resonator active must never trip `stateFinite()` and must recover within 100 ms. |
| **R-14** | **`WaveguideString::silence()` bricks the string.** It zeroes `bridgeDelayFloat_` (`:243`) and `process()` early-returns 0 below `kMinDelaySamples` (`:156`); the field is written in only three places. Three plan paths call `silence()` without re-tuning, and none of them moves the pitch, so §8.3's `pitchDirty` gate never fires. | §10.1 enumerates all three paths (FR-038a recovery, FR-063 un-bypass, `reset()`) and mandates `retune(f_body)` — or, for `reset()`, clearing the applied-value shadows so the next control step re-tunes unconditionally. SC-012(iii) and SC-016 gain a Strings **non-zero output after a bypass round-trip** clause; without it, digital silence passes every clickless criterion. |

---

## 19. Implementation order

1. **T0** — ODR sweep + lints (§1). Blocking.
2. **T1** — RA-4 (§3, one line) + `DiffusionNetwork_ZeroModIsBitIdentical`. Run `dsp_processors_tests`,
   `dsp_effects_tests`, `plugin_tests`, `approval_tests`. Smallest change, largest CPU return, and it
   de-risks the cloud budget before the cloud exists.
3. **T2** — RA-1 (§2) + `WaveguideString_RetuneIsInert`, `WaveguideString_RetunePitchAccuracy`. Run
   `dsp_processors_tests`, `innexus_tests`, **`membrum_tests`**.
4. **T3** — `continuous_body.h` skeleton: constants (**including the full FR-009 range/default set,
   §4.2**), `BodyMaterial`/`Engine`/`MaterialProfile`, the two ratio tables, §6.4's uniform setter shape,
   `prepare`/`reset`/`processStereoBlock` walker (§11) with a pass-through body, the full FR-007
   introspection surface. First tests: SC-011 block-size invariance (sub-case α), SC-006 no-alloc,
   **`ContinuousBody_ControlSurfaceDefaults`** — they exercise the walker and the control surface and
   nothing else, and they are the ones that catch the control-grid carry and the FR-006 substitution.
5. **T4** — modal engine path: profiles, mode-count truncation (§8.3), `setModes`/`updateModes`, the
   dirty gates. Tests: SC-009(a)(b), SC-003(a)(c)(d), **`ContinuousBody_ModeCountTruncation`**, and
   **SC-011 sub-case β** (the in-flight-retune block-size case — it cannot be written before
   `updateModes` exists, and it is the R-12 gate).
6. **T5** — the drive normaliser (§7): `Ĝ`, two drives, log10 smoothing, AGC, damping floors, resonance/
   damping laws. Tests: **SC-015 first** (it validates the central maths in isolation), then SC-007,
   then SC-001/SC-002 for the modal materials.
7. **T6** — waveguide and comb engines + `getEngineSampleCount`. Tests: SC-016, SC-003 for all five,
   SC-001/SC-002 for all five.
8. **T7** — crossfade + collapse rule (§6.2). Tests: SC-012, SC-002's material-switch case.
9. **T8** — decay cloud (§9) + `setResonatorBypass`. **GATED: OQ-A must be resolved before this task
   ships** — either the decay-dependent `fb` calibration FR-052 prescribes, or recorded user sign-off on
   the reduced `cloudSize = 1.0` grid written into D-5. The plan may not pick a red criterion and may not
   silently drop a grid point. Tests: SC-008, SC-004, **`ContinuousBody_OutputStageEndpoints`** (FR-060's
   `setMix(0)` identity and FR-062's width-correlation delta both need the cloud in place).
10. **T9** — seeding (§12), non-finite handling (§7.8, §7.8.1–§7.8.3), the `kMaxFollowerInput` clamp
    (§7.6) and §10.1's re-tune-after-`silence()` on all three paths. Tests: SC-010, SC-013 (a, a2, b, b2),
    and the Strings-non-zero-after-bypass clauses in SC-012(iii)/SC-016.
11. **T10** — `continuous_body_perf_test.cpp`, baselines measured and pinned with a provenance block.
    Test: SC-005.
12. **T11** — full gate sweep (§16.3), the complete SC-014 suite matrix, compliance table.

---

## 20. Open items for the user

**OQ-A — SC-008's `(0.5 s, cloudSize = 1.0)` grid point. HARD GATE ON T8 (§19), not a default.**
Measured **+30.9 %** against a ±15 % criterion, on a correct implementation, at 5.3 loop traversals.
Options: **(a)** narrow the `cloudSize = 1.0` grid to `{2, 10, 30}` s and keep `{0.5, 2, 10, 30}` at
`cloudSize = 0.0` — consistent with FR-052's own "binds at ≥ 4× loopSeconds" scoping, but it is a
**reduction in coverage at the shortest decay setting**, which is where a mis-derived `cascadeSec` is most
visible, and FR-052 sanctions exactly one response to a miss ("calibrate `fb` against a measured tail at
configure time, **never** widen SC-008") of which deleting a grid point is not one;
**(b)** implement the decay-dependent `fb` calibration — the bias is a few-traversal artefact rather than
a constant loop-time error, so it is more than the single `kCascadeDelayFactor` constant;
**(c)** keep the grid and accept a red criterion. The plan cannot pick (c), and — changed from an earlier
draft — **does not pick (a) by default either**. (a) requires explicit user sign-off recorded in §17's D-5
row with the date; otherwise T8 ships (b).

**OQ-B — how `WaveguideString_RetuneIsInert` obtains its pre-amendment reference.** SC-014 asks that a
build which never calls `retune()` produce renders "identical to pre-amendment `WaveguideString`
renders". Since the amendment ships in the same commit, there is no pre-amendment binary to compare
against at test time. Options: **(a)** pin a `RenderFingerprint` measured on the pre-amendment header
during implementation and check the four aggregate metrics + 32 checkpoints into the test as a reference
struct (this is *not* a bit-exact golden — it is exactly what `render_fingerprint.h` exists for);
**(b)** assert containment structurally (the amendment adds a method and touches no member) and rely on
the unedited existing `waveguide_string_test.cpp` / `innexus_tests` / `membrum_tests` as the regression
evidence. The plan recommends **(a) plus (b)** — (a) is cheap and (b) is what actually covers Innexus and
Membrum.

**OQ-C — test wall time.** SC-001 + SC-002 render 5 materials × 3 signals × (60 s drive + up to 65 s
ring-out) = ~31 minutes of audio at 48 kHz per full run, plus SC-007's self-sizing steady-state renders
(up to 69 s for Metal Plate at `r = 1.0`, × 4 resonance points × 5 materials). Should the 65 s
infinite-ring sweeps carry a `[.slow]` tag (excluded from the default CI run, run explicitly and in the
nightly lane), or run on every leg like Membrum's kit-switch test does? The plan's default is **run them
on every leg** — they are the criterion the roadmap names first (line 219) — but the cost is real and the
choice is the user's.

**OQ-D — `kNyquistHeadroomOct = 1.0` vs Metal Plate's 29 modes at 220 Hz.** Metal Plate is ~2.6× more
expensive than Glass at the same pitch purely because its ratio table grows linearly rather than as `n²`.
If SC-005's baselines do not fit, lever 5 in §14.3 (halving the glide headroom) is the first thing that
trades a *specified* behaviour (one octave of clickless upward glide before the bank's own cull engages)
for CPU. Flagging it now so the trade is not made silently at measurement time.

**OQ-E — modal coefficient smoothing is block-rate, and the walker's block cadence is host-dependent.**
`ModalResonatorBank::smoothCoefficients()` runs once per `processBlock` call (`:357`), not once per
sample, so §11's per-sub-chunk cadence makes the smoothing trajectory a function of how the host
partitions the buffer: 16 calls for 1×1024 from counter 0, 17 for 1023+1, more for 100+…+24 (§8.2, R-12).
With coefficients in flight, SC-011's block-size clause (`kSampleTolerance = 1e-4` across six partitions)
is therefore measuring something real for the first time — sub-case (β) exists to find out whether it
passes. If it does not, the options are **(a)** drive the modal bank on a partition-independent cadence
(accumulate into a 64-sample scratch and call `processBlock` exactly once per *complete* control chunk),
which costs up to 63 samples of latency and contradicts FR-005a's 0-sample guarantee for the modal path
only; or **(b)** record a **measured** deviation in §17 with the actual maximum divergence. Widening
SC-011's tolerance is not an option. Raised now because (a) is a structural change to §11 and would be
expensive to retrofit at T10.

> **RESOLVED at T007 — by a third option, recorded as D-13.** (β) missed by 710× (worst |dL| 7.096e-02
> against 1e-4). Neither (a) nor (b) was taken: a strictly-additive `ModalResonatorBank::snapCoefficients()`
> lets `applyEngineRetune` apply the targets on the component's own 64-sample control grid, which makes the
> bank's per-block smoothing an exact no-op. Partition-independent at **zero** latency, tolerance unchanged
> at 1e-4, and all six partitions now agree **bit-identically**. FR-041 and FR-042a need the corresponding
> one-sentence correction (filed in D-13).

---

## 21. Verification notes

Everything asserted about existing code above was read this session from the working tree at
`f:/projects/iterum`, branch `feat/seraphis-phase1-life-modulators`. Headers opened and quoted:
`processors/modal_resonator_bank.h` (full), `processors/waveguide_string.h` (full),
`processors/diffusion_network.h` (full), `systems/timevar_comb_bank.h` (`:1-60`, `:75-400`, `:399-543`,
`:590-797`), `processors/envelope_follower.h` (`:80-250`, `:350-404`), `processors/iresonator.h`,
`primitives/smoother.h` (`:50-270`), `primitives/delay_line.h` (`:40-300`), `primitives/one_pole.h`,
`core/random.h`, `core/crossfade_utils.h`, `core/dsp_utils.h` (`:95-125`), `systems/harmonic_cloud.h`
(`:125-160`, `:275-300`, `:870-960`), `systems/poly_synth_engine.h` (`:30-45`), plus
`dsp/tests/CMakeLists.txt` (`:290-345`), `dsp/tests/unit/systems/harmonic_cloud_perf_test.cpp`
(`:1-290`, `:380-420`), `dsp/tests/unit/systems/harmonic_cloud_test.cpp` (`:11-54`, `:4835-4905`),
`tests/test_helpers/{render_fingerprint,artifact_detection,audio_features,signal_metrics}.h`,
`plugins/membrum/tests/unit/processor/test_kit_switch_infinite_ring.cpp` (`:50-65`),
`tools/lint-layers.js`, `tools/gen-plate-chladni.js`,
`plugins/membrum/src/dsp/bodies/plate_modes.h` (`:1-60`).

**Numerical work performed this session** (not copied from the spec):
- The FR-032 closed form was **validated against the bank's exact recursion** by direct simulation at six
  (frequency, damping) points — agreement to 4 decimal places (§7.2).
- FR-036's T60 tables at `r ∈ {0, 0.8, 1.0}` were **recomputed** from the FR-011a profiles and reproduce
  the spec's numbers.
- The Glass ratio table was **generated from the shell law** and its SC-003(c) inharmonicity figure
  (8.1908) reproduces the spec's 8.19; the Metal Plate table's figure (0.9880) reproduces the spec's 0.99.
- FR-043's mode counts were **computed** for 5 pitches × 3 sample rates with the bank's own stretch,
  scatter and Nyquist-guard arithmetic — establishing Metal Plate as the worst-case material.
- The FR-052 decay-cloud loop was **simulated end to end** (delay + 8 Schroeder allpasses at the exact
  `kBaseDelayMs·size·ratio` delays + one-pole LP + DC blocker + `fb`) across `cloudSize ∈ {0, 0.5, 1.0}` ×
  `decay ∈ {0.5, 2, 10, 30}` s × `cloudDamping ∈ {0, 1}` × three envelope-extraction methods. This is
  what produced D-4, D-5, D-6, OQ-A and R-3 — none of which the spec anticipates.

Two spec figures were found to be estimates rather than derivations and are corrected in §17 (D-1, D-3).
No spec threshold is relaxed anywhere in this plan.

### 21.1 Additional lines read during the review-revision pass

Re-opened and quoted while resolving the review (same working tree, same branch):
`core/dsp_utils.h` (`:95-124` — `softClip`'s `if (sample > 3.0f) return 1.0f;` at `:107` and the closing
`std::clamp(y, -1, 1)` at `:111`); `core/db_utils.h` (`:155-178` — `flushDenormal` at `:168` returns
NaN/Inf unchanged, `isInf` at `:175`); `processors/modal_resonator_bank.h` (`:178-205` — `smoothCoeff_`
computed in `prepare` at `:182`, `reset()`'s target memcpies at `:199-201`; `:340-420` — `processBlock`
at `:355-369` with `smoothCoefficients()` at `:357` and `applyOutputStage` at `:366`, `getModalEnergy()`
public at `:415`; `:565-575` — `kSmoothingTimeMs = 2.0f` at `:569`; `:780-820` — `applyOutputStage` at
`:789-796`, `smoothCoefficients` at `:801-809`; access specifiers: single `public:` at `:72`, `private:`
at `:561`); `processors/waveguide_string.h` (`:150-259` — the `bridgeDelayFloat_` early-return at `:156`,
`softClip(junction)` at `:181`, `feedbackVelocity_ = output` at `:215`, `silence()` zeroing
`bridgeDelayFloat_` at `:243`); `processors/envelope_follower.h` (`:155-204` — "Does NOT validate input"
at `:163-164`, the denormal flush at `:184-185`; `:300-329` — `processRMS`'s **float** square at `:313`,
the latching branch at `:316-321`, `std::sqrt` at `:325`); `processors/diffusion_network.h` (`:283-300` —
`setWidth`; `:330-379` — the `size < 0.001` bypass at `:344`, `stagePhaseOffset` **already declared** at
`:361`, the `std::sin` at `:362`; `:385-391` — the mid/side width application);
`systems/timevar_comb_bank.h` (`:593-651` — mono `process`, per-comb non-finite reset at `:637-641`,
accumulate at `:644`, **no output clipper**; `stereoSpread_` written at `:585` and read only at `:763` →
`panLeftGain`/`panRightGain` at `:769-770` → read only at `:715-716` inside `processStereo`);
`primitives/delay_line.h` (`:206-224` — `read()`'s silent `std::min` clamp at `:213`).

**Arithmetic performed during this pass:** `softClip(0.9) = 0.9·(27 + 0.81)/(27 + 7.29) = 0.72996` and
`softClip(2.46) = 2.46·(27 + 6.05)/(27 + 54.46) = 0.998`, which are respectively §7.9's headroom
threshold and D-11's demonstration that SC-007(iii)'s upward leg measures ≈ +12 dB rather than +20 dB on a
correct implementation. Float overflow bound for `EnvelopeFollower::processRMS`: `√(3.4e38) ≈ 1.8e19`,
which sets `kMaxFollowerInput = 1e9` with ~10 orders of margin.

---

## 22. Review notes — disposition of the review pass

Every blocker and major was applied. Two suggested resolutions were implemented in a **different form**
than the reviewer proposed, and the reasons are recorded here rather than left as a silent divergence.
Nothing was resolved by relaxing a threshold; two clauses (D-9, D-11) were made *stricter* in the process.

| Review item | Disposition |
|---|---|
| **SC-001 / R-4 clamp counter** (blocker) — suggested (i) a new engine-level saturation counter on the introspection surface, or (ii) a pre-clip headroom assertion | **(ii) adopted, (i) rejected.** FR-007 declares its accessor list exhaustive ("a success criterion may not assert on a quantity absent from it"), so (i) requires amending the spec's own introspection contract. Because `softClip` is strictly monotone, inverting it gives the identical discrimination from accessors that already exist — §7.9, D-9. The ±2.0 clamp's lack of diagnostic value on 4 of 5 materials is recorded explicitly at `kOutputClamp`, in §6.3, in §7.9 and in R-4, so no reader can re-acquire the wrong mental model. |
| **`stateFinite()` split** (blocker) — suggested public per-subsystem predicates | **Adopted, but the predicates are `private`** and `stateFinite()` remains the single public accessor, for the same FR-007 reason. FR-038a clause 2's discriminator is satisfied internally; SC-013(b) observes it through behaviour (cloud cleared, engines not silenced) rather than through a new accessor. |
| **FR-038 whole-chunk substitution** (major) — suggested (a) restate the unit as the sub-chunk, or (b) buffer the control chunk | **(a) adopted**, recorded as D-10, with the partition × poison cross-case added to SC-013(a2). (b) costs up to 63 samples of latency and contradicts FR-005a's 0-sample guarantee, which the spec states normatively. |
| **SC-016 third-engine clause** (minor) — suggested a per-slot accessor **or** exact arithmetic | **Exact arithmetic adopted**, per-slot accessor rejected (FR-007 again). The arithmetic is strictly more informative: a broken collapse rule over-counts by ≈ 490 ms of samples and fails, where the qualitative clause could not fail at all. |
| **Chamber `stereoSpread`** (major/minor, reported twice) — suggested (a) route through `processStereo` or (b) drop the field | **(b) adopted**, recorded as D-12 with corrections filed against FR-011a and A-1. (a) makes one material's engine output stereo while FR-037's clamp and its per-sample counter are pinned by FR-007 as mono ("the resonator core is mono, A-1"), forking the mix/clamp/`Ĝ` path for a single material. §6.3's note, which previously asserted the opposite conclusion, is corrected in place. |
| **D-5 / OQ-A** (minor) | **Adopted in full: D-5 is no longer the default.** T8 is gated on either FR-052's prescribed calibration or recorded user sign-off. |
| **Amendment B line number** (minor, reported twice) | Adopted — §3 now says "replaces line 362 only" and the snippet no longer redeclares `stagePhaseOffset`. The reviewer's parenthetical confirmation that the bit-identity argument itself holds (`baseDelayMs > 0` after the `:344` bypass, so `delayMsL/R` can never be `-0.0f`) is folded into the amendment's comment. |
