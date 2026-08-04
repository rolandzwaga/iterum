# Implementation Plan: Seraphis Phase 11 — UI (Organism-First Editor)

**Spec:** `specs/seraphis-phase11-ui/spec.md` (revision of 2026-08-03 carrying the phase-owner rulings)
**Roadmap:** `specs/Seraphis-roadmap.md` → Part B, Phase 11
**Tasks:** `specs/seraphis-phase11-ui/tasks.md` (T001 – T029; §13 maps plan sections onto them)
**Status:** PLAN — no implementation yet
**Date:** 2026-08-03, **Revision 3** (applies all 22 findings of the second plan review; supersedes
Revision 2, which superseded the 11:45 draft). See *Review notes* for the finding-by-finding index.
**Branch:** `feat/seraphis-phase1-life-modulators` (the single Seraphis branch; do not rename)

---

## 0. What this plan is, what the rulings changed, and what is still a deviation

Every signature quoted below was opened and read **this session**; each carries the `file:line` it was
read at, and every one of the plan's pre-ruling citations was re-verified against the headers before this
revision (the verification pass is listed at the end of this section).

The previous draft of this plan raised seven deviations `D-1 … D-7` and one non-blocking note. The
phase owner has ruled (spec *Clarifications* → *Session 2026-08-03 (phase-owner rulings, plan §11)*).
**Three are now closed; five remain open plan-level corrections that still need a spec edit** — D-3 – D-6
plus **D-9**, the collection Revision 3 added (§11.2).

### 0.1 Closed by ruling — these change what gets built

- **D-1 → RELAXED, not disclosed.** `SeraphisVoice::setSpectralState` is configure-time gated:
  `if (!isConfigurable()) { ++rejectedConfigCalls_; return; }` (`seraphis_voice.h:770-776`, verified this
  session), where `isConfigurable()` is `!hasSounded_ || isFinished()` (`:908`, verified). The plan's
  recommended (A) — ship as-is with a "will apply on next note" indicator — is **overruled**. The gate is
  **relaxed for `setSpectralState` / `setSpectralStateCount` only** (§2.3), because the primitive they
  forward to was already continuity-safe: `SpectralMorphEngine::setState` (`:292`) arms the FR-047
  absorption fade whenever the slot contributes (`armStateFade()` at `:312`, `slotContributes` at `:558`
  — all verified this session). The disclosure indicator is **not built**. New work: §2.3, SC-028 – SC-030
  (§10.1/§10.2), R-16 (§12). This is the phase's **one non-additive `dsp/` edit**.
- **D-2 → accepted, replacement observable adopted.** FR-038's original mechanism sentence named
  `effectsPushes_` (`processor.cpp:1709 … :1834`, incremented **only** inside `pushEffectsParams()`,
  `:1665`) as the counter a macro move must move. It provably cannot: neither 1410 nor 1441 is in that
  function's ID set — 1410 rides `fxReturnGainSm_.setTarget(fxEffectiveReturnGain_)` (`:3051`, whose input
  is computed from `effectsParams_.delayMix` inside `updateEffectsBypassState()` at `:2351`) and 1441
  rides `fxWanderDepthSm_.setTarget(effectsParams_.wanderDepth…)` (`:3052`) — all four sites verified this
  session. The spec now states the substituted-reads mechanism and the replacement observable
  (`composedFxDelaySendForTest()` / `composedFxWanderDepthForTest()` /
  `composedEffectsRecomputeCountForTest()`). §4.2 is unchanged; §10.2's SC-021(c) row now names the
  accepted observable instead of deferring to an open question.
- **Composition cadence, one-block lag → accepted as designed.** `computeEffectsTargets()` is read once
  per `process()` call **before** `pushMacroSurfaces()` (`processor.cpp:1310`, inside the slice loop)
  refreshes the bases, so a macro or deep move arrives on the **next** call — 10.67 ms at 512/48 kHz,
  inside the 20 ms class-(b) smoothing both consumers already impose. SC-021(a) reads its sweep allowing
  exactly one block of settle per point. No spec weakening; §4.2 stands as written.
- **D-7 → absorbed into the spec.** SC-021(a) now names `preOutputTapLForTest()` (`processor.h:431`) and
  `preOutputTapRForTest()` (`:434`) and states the statistic. Both accessors verified this session; there
  is still no `preOutputTapForTest()`. Nothing left to do.

### 0.2 New finding this session — it closes the plan's one non-blocking note

- **D-8 (resolves `fundamentalHz`'s source, and it needs no new API).** The previous draft left
  `CloudFrame::fundamentalHz` as "derive from the focus voice's note; confirm at T-time whether
  `SeraphisEngine` exposes a per-voice note read-back, otherwise track it in `dispatchEvent`". It does
  **not** expose one — `allocator_.getVoiceNote(i)` is used only internally (`seraphis_engine.h:494`,
  `:1333`) and is not public — and `SeraphisEngine::noteOn` takes a `std::uint8_t note`
  (`seraphis_engine.h:476`) while the processor's `dispatchEvent` (`processor.cpp:95`, call at `:103`)
  never learns which slot the allocator chose, so `dispatchEvent` bookkeeping **cannot** attribute a note
  to a voice either. Both of the ruling's two named candidates therefore fail.
  **What exists and is exactly right:** `HarmonicCloud::getFundamentalHz()`
  (`dsp/include/krate/dsp/systems/harmonic_cloud.h:405`, `[[nodiscard]] float … const noexcept`, returning
  the `fundamentalHz_` shadow written **only** by `setFundamentalHz` at `:383`, whose **sole** caller is
  `SeraphisVoice::noteOn` at `seraphis_voice.h:529` — all four verified this session). It is the note
  frequency with **no drift multiplier applied**, reachable through the already-public
  `SeraphisVoice::cloud()` (`:827`). §5.3 now uses it. This satisfies the ruling's binding half —
  `frequencyHz[0]` is forbidden by name, and this is not it — with **zero** `dsp/` additions.

### 0.3 Still open — plan-level corrections that need a spec edit before compliance

Five now; none relaxes a threshold, and each names the exact spec sentence to edit (§11.2). D-4 – D-6 are
unchanged from the previous draft; D-3's *size* is unchanged but its *rationale* was wrong and is
replaced; **D-9 is new** and is a collection.

- **D-3.** The `[partials]` block is **272 bytes**, not "≈268" — 64 floats (256) + two 64-bit masks (16).
  The previous draft blamed the number on `IBStreamer` having no 64-bit accessor; **it has one**
  (`FStreamer::writeInt64u`/`readInt64u`, `extern/vst3sdk/base/source/fstreamer.h:97-106`, used already at
  `plugins/disrumpo/src/processor/processor_state.cpp:356`), and the split changed no arithmetic. Both
  the size claim and the corrected rationale go into the spec.
- **D-4.** `sub-controller="SeraphisEdit"` must sit on the **template root**, not on an intermediate
  container, or FR-023's absolute drawer rects cannot be what `getViewSize()` returns.
- **D-5.** The header preset button (FR-007) is tag-less and has no listener owner in C-7b's table;
  D-4's root-level sub-controller is what makes FR-045 satisfiable for it without a carve-out.
- **D-6.** `cloudFrameEnabled_` must be `std::atomic<bool>`, not the spec's "plain bool" — it is written
  on the message thread and read on the audio thread every `process()` call.
- **D-9 (NEW).** A collection of ten spec statements this revision proved wrong, unreachable, or
  incomplete — C-4's inverted mask polarity, C-5 clause 1's message-thread fan-out, C-9/FR-042's
  now-incomplete `process()` enumeration, C-6's `setPartial` no-op list, C-2 clause 7's attempt counter,
  SC-007/R-1's divisor, SC-023's unreachable `draw()`, SC-026 clause 2's "allocates nothing",
  SC-013's dsp-unimplementable precondition — **plus** the requirement that every criterion arm this plan
  adds is written into spec.md's own Success Criteria, since the compliance table is filled against that
  list and not against this document.

### 0.4 Citation verification pass (this session)

Re-read and confirmed at the exact lines this plan quotes: `seraphis_voice.h` (`:641`, `:770`, `:777`,
`:784`, `:827`, `:828`, `:908`, `:989`, `:1208`); `spectral_morph_engine.h` (`:199-206`, `:292`, `:296`,
`:304`, `:312`, `:318`, `:327`, `:423`, `:424`, `:434`, `:443`, `:449`, `:558`); `seraphis_engine.h`
(`:211`, `:213`, `:215`, `:476`, `:811`, `:927`, `:939`, `:949`, `:952`, `:955`, `:975`, `:997`);
`harmonic_cloud.h` (`:138`, `:144`, `:183-185`, `:331-332`, `:383`, `:405`, `:501`, `:535`, `:545`,
`:701`, `:703`, `:950`, `:955`, `:959`, `:973`, `:986`, `:991`, `:1069`, `:1084`, `:1101`);
`spectral_state.h` (`:13-15`, `:21-23`, `:26-35`, `:44-65`, `:82`, `:155`, `:185-186`, `:315`, `:317`,
`:342-346`, `:373`); `seraphis_macro_matrix.h` (`:52`, `:55-89`, `:110`, `:161`, `:163`, `:166`, `:168`,
`:180`, `:446-452`, `:457`, `:480`, `:495`, `:508`, `:530`, `:548`, `:623`, `:667`, `:814-825`);
`processor.cpp` (`:95`, `:103`, `:1040`, `:1082`, `:1084`, `:1310`, `:1311`, `:1385`, `:1416`, `:1665`,
`:1709`, `:1821`, `:1858-1859`, `:2344-2358`, `:2619`, `:2705`, `:2734`, `:2779`, `:2830`, `:3051`,
`:3052`); `processor.h` (`:303`, `:390`, `:398`, `:406`, `:431`, `:434`, `:444`);
`effects_params.h` (`:105`, `:117`, `:127`, `:139`); `plugins/seraphis/tests/CMakeLists.txt` (`:5-53`);
`dsp/tests/CMakeLists.txt` (`:156`, `:282`, `:299`).

Everything else in the spec is implementable as written.

---

## 1. Layer 2 — the three authoring mutators (`dsp/include/krate/dsp/processors/spectral_state.h`)

**Layer:** 2 (processors). **Adds no include** — the header already carries
`<krate/dsp/core/db_utils.h>` + `<algorithm> <array> <bit> <cmath> <cstddef> <cstdint> <cstring>
<type_traits>` (`spectral_state.h:26-35`), which is everything the three functions need
(`std::clamp`, `std::log2`, `std::pow`, `std::memcpy`). Layer discipline holds unchanged (`:17-19`).

**RT safety:** all three are `noexcept`, allocation-free, lock-free, exception-free, I/O-free. They are
**message-thread-only** by C-5 and are *not* in SC-011's audio-thread corpus. Finiteness is tested with
`detail::isNaN` / `detail::isInf` (defined in `db_utils.h`, used at `spectral_state.h:91`, `:103`),
**never** `std::isnan` — the header's own `-ffast-math` banner at `:21-23`.

**Placement — after `makeFactoryState`, not after `normalizeSpectralState`.** The obvious slot (right
after `normalizeSpectralState`, `:155-175`) **does not compile**: `kAuthorSpacing` below is initialised
from `detail::factory::kFillSpacingFactor`, which is declared at `:344` inside a `namespace
detail::factory` that does not open until `:317`, and §1.2 step 6 additionally names
`detail::factory::kFillMaxGrowth` (`:342`) and `kFillMaxRatio` (`:343`). Name lookup in a namespace-scope
`inline constexpr` initialiser and in a non-template inline function body is resolved **at the point of
definition**, so a use at `:177` is a hard compile error on every leg — tasks.md T002's "compiles" gate would fail
immediately.

The three mutators are therefore placed **immediately after `makeFactoryState`'s closing brace**
(`:483`) and before the namespace close (`:485`). At that point `isValidSpectralState` (`:82`),
`normalizeSpectralState` (`:155`), the four range constants and the whole of `detail::factory`
(`:317-346`) are all declared. (The alternative — hoisting the three FR-041 constants into their own
block above `:155` — is rejected: it splits `detail::factory` in two and moves lines the phase has no
reason to touch.)

**The four range constants are `SpectralState`-SCOPED, and every use below must be qualified.**
`kMinStateRatio` / `kMaxStateRatio` / `kMinStateTiltDbPerOct` / `kMaxStateTiltDbPerOct` are
`static constexpr` **members** of `struct SpectralState` (`spectral_state.h:51-54`, read verbatim this
session), not namespace-scope names — the header's own code always writes `SpectralState::kMinStateRatio`
(`:94`, `:132`). The three mutators below are namespace-scope **free functions**, so an unqualified use
does not compile on any leg and T002's "compiles" gate would hit it immediately. Every occurrence in
§1.1 step 5, §1.1 step 6's `lo`/`hi` fallbacks and §1.3 step 3 is therefore written
`SpectralState::kMinStateRatio` and so on.

### 1.1 `setPartial`

```cpp
/// Author one partial's ratio and amplitude in place (Phase 11 FR-031, C-6).
/// PRESERVATION, not repair: a state that already satisfies isValidSpectralState
/// still does afterwards; a state that does not is left BYTE-UNCHANGED.
inline void setPartial(SpectralState& s, std::size_t index, float ratio,
                       float amplitude) noexcept;
```

**Authoring spacing constant.** Declared beside the function, reusing the header's own value so no new
number enters the file:

```cpp
/// The strictly-monotone authoring guard band: 28 cents, i.e.
/// detail::factory::kFillSpacingFactor (:344), the same geometric spacing the
/// FR-041 continuation already uses.
inline constexpr float kAuthorSpacing = detail::factory::kFillSpacingFactor;  // 1.0163049f
```

**Body, in order (every clause is a no-op-with-no-write on failure):**

0. **`if (!isValidSpectralState(s)) { return; }`** — the whole-state gate, exactly as `tiltState`'s
   step 2 has it. **This step is load-bearing and was missing from the previous draft.** FR-032 cl. 2 /
   SC-012 cl. 2 require that an *invalid* input state is left **byte-unchanged**, and SC-012's own
   coverage list (spec `:1396-1400`) enumerates rows that are invalid **somewhere other than the edited
   index** — `amplitudes[5] = 1.5` (`spectral_state.h:106-108`), a descending pair at index 30
   (`:97-99`), a `name` field with no NUL (`:118-120`). Steps 1–6 are all *local* checks and every one of
   those rows passes them, so without step 0 step 7 stores and `memcmp != 0`. The old claim that "every
   rejection happens before the first store, so the property is structural" was true of the *checks that
   were there* and false of the *criterion*, because the criterion is keyed on `isValidSpectralState`,
   not on the local window.
1. `if (s.numPartials < 0 || s.numPartials > 64) return;` (subsumed by step 0 in practice — kept because
   it is the precondition step 2's cast relies on and costs one predicted branch).
2. `if (index >= static_cast<std::size_t>(s.numPartials)) return;`
3. `if (isNaN||isInf on ratio or amplitude) return;`
4. `const float amp = std::clamp(amplitude, 0.0f, 1.0f);`
5. `float r = std::clamp(ratio, SpectralState::kMinStateRatio, SpectralState::kMaxStateRatio);`
6. Monotone window:
   ```
   lo = (index > 0)            ? s.ratios[index-1] * kAuthorSpacing : SpectralState::kMinStateRatio
   hi = (index+1 < numPartials)? s.ratios[index+1] / kAuthorSpacing : SpectralState::kMaxStateRatio
   if (lo > hi) return;            // neighbours closer than kAuthorSpacing^2 -> NO-OP, no write
   r = std::clamp(r, lo, hi);
   ```
7. `s.ratios[index] = r; s.amplitudes[index] = amp;` — **and nothing else.** `name`,
   `tiltDbPerOct`, `inharmonicity`, `numPartials` are untouched.

**Why no-op-before-write matters:** FR-032 clause 2 / SC-012 clause 2 assert
`std::memcmp(&before, &after, sizeof(SpectralState)) == 0` on the invalid branch. With **step 0** in
place every rejection — whole-state *and* local — happens before the first store, so the property is
structural rather than tested-in. `SpectralState` is trivially copyable (`:65`), so `memcmp` is well
defined.

**Spec edit required (folded into D-9, §11.2).** Spec C-6's `setPartial` no-op list (spec `:698-699`)
enumerates only the local rejections; it must name **whole-state invalidity** as well, so the contract
and SC-012 clause 2 agree about what the invalid branch is.

**The tight-window consequence (SC-013(a) depends on it).** Every factory state's authored ratios are
integer or near-integer harmonics (`:398-413`), so for 0-based `k` the upper edge is
`(k + 2) / 1.0163049`. A perfect-fifth target `1.5·(k+1)` exceeds it for every `k ≥ 1`
(`k = 1`: 3.000 vs 2.952). Only `k = 0` (target 1.5, edge 1.968) and the topmost authored slot have a
fifth of room. This is correct clamp-not-swap behaviour; any criterion naming an interval must pin the
index at 0.

### 1.2 `blendStates`

```cpp
/// Convex blend of two endpoints. Returns UNCONDITIONALLY valid (FR-032 cl. 3).
[[nodiscard]] inline SpectralState blendStates(const SpectralState& a, const SpectralState& b,
                                               float t) noexcept;
```

Returns by value; `SpectralState` is trivially copyable (`:65`), so this is a **`sizeof(SpectralState)`
= 540-byte** stack copy, not an allocation. (540 = `ratios` 64×4 + `amplitudes` 64×4 + `name` 16 +
`tiltDbPerOct` 4 + `inharmonicity` 4 + `numPartials` 4, no padding — every member is 4-byte-aligned,
`spectral_state.h:57-62`. **Do not confuse this with 541**, `kSpectralStateBytes` (`:186`), which is the
*serialized* size and a different quantity; this plan uses 541 only for stream payloads.)

**Body:**

1. `const bool va = isValidSpectralState(a), vb = isValidSpectralState(b);`
   `if (!va && !vb) return SpectralState{};` — documented valid (`:42-43`).
   `if (!va) return b; if (!vb) return a;`
2. `if (isNaN(t) || isInf(t)) return a;` else `const float u = std::clamp(t, 0.0f, 1.0f);`
2a. **Exact endpoint short-circuit — `if (u == 0.0f) { return a; } if (u == 1.0f) { return b; }`.**
   This is what makes C-6's rule `blendStates(a, b, 0) == a` **literally** true, and SC-025's
   `memcmp`-form assertion satisfiable. Without it the interior body cannot reproduce `a` byte-for-byte,
   for three independent reasons, none of which is a rounding nicety: (i) step 7 unconditionally writes
   `out.name ← "Blend"` while `a` carries its own label, and `name` is **inside**
   `sizeof(SpectralState)` (`spectral_state.h:56`, `kStateNameBytes = 16`), so the `memcmp` fails on
   that alone; (ii) step 6 regenerates every slot at `i >= out.numPartials` from the FR-041
   continuation, whereas `a`'s tail is whatever `a` had — and `out.numPartials = min(a, b)` may be
   **shorter** than `a.numPartials`, so real authored partials would be replaced by continuation values;
   (iii) step 4's `exp2((1−u)·log2(x) + u·log2(y))` at `u = 0` is a binary32 `log2`/`exp2` round trip and
   neither function is required to be correctly rounded, so it is not the identity even on the ratios it
   does touch. The short-circuit costs one compare, does not touch the interior monotonicity proof
   (which is stated for `u ∈ (0,1)` and holds unchanged on the closed interval), and is the *only*
   resolution that does not weaken SC-025: the alternative — restating SC-025 as a field-wise comparison
   with tolerances — would drop the byte-identity claim §1.2's "no `normalizeSpectralState`" argument
   below also rests on.
3. `SpectralState out{}; out.numPartials = std::min(a.numPartials, b.numPartials);`
4. For `i < out.numPartials`:
   - **ratios interpolate in `log2`, then `exp2`:**
     `out.ratios[i] = std::exp2((1-u)*std::log2(a.ratios[i]) + u*std::log2(b.ratios[i]));`
     *Validity proof.* `log2` is strictly increasing, so `log2(a.ratios[·])` and `log2(b.ratios[·])` are
     strictly increasing sequences; a convex combination of two strictly increasing sequences is
     strictly increasing; `exp2` is strictly increasing; therefore `out.ratios` is strictly increasing
     over `[0, numPartials)`. And a convex combination of two values in `[log2 0.5, log2 128]` lies in
     that interval, so `out.ratios[i] ∈ [0.5, 128]`. **No clamp is applied, and none is needed** — a
     clamp would be the one operation that could flatten two neighbours into equality and break
     monotonicity. `log2` is chosen because it is the domain `SpectralMorphEngine` itself stores
     (`spectral_morph_engine.h:286`), so a blend and a morph agree about what "halfway" means.
   - `out.amplitudes[i] = (1-u)*a.amplitudes[i] + u*b.amplitudes[i];` — both inputs in `[0,1]` ⇒ result
     in `[0,1]`.
5. `out.tiltDbPerOct` and `out.inharmonicity` interpolate linearly; both inputs are in range ⇒ result
   is in range.
6. Slots at `i >= out.numPartials` get the **same FR-041 geometric continuation** `makeFactoryState`
   uses (`:416-433`), copied verbatim (the `count >= 2` recurrence, the `kFillMaxGrowth` clamp, the
   `kFillMaxRatio` ceiling, the `kFillSpacingFactor` floor). The validator does not examine them
   (`:78-79`), but leaving them at 0 would make a blended state byte-shaped differently from a factory
   state and would put `log2(0)` into any consumer that scans the whole array.
   Amplitudes there stay value-initialised `0.0f`.
7. `out.name` ← `"Blend"` (NUL-padded; `out.name` is already all-zero, copy stops one byte short of the
   field), which trivially satisfies `:111-127`.

**No `normalizeSpectralState` call.** Both inputs are already normalised and a convex combination of
two unit-norm vectors has norm ≤ 1, so amplitudes stay in range; normalising would additionally rescale
the interior, which the reversibility argument does not want. (The byte-identity half of SC-025 is
carried by step 2a's endpoint short-circuit, **not** by the absence of a normalise call — the interior
body could never have produced it, see 2a.)

### 1.3 `tiltState`

```cpp
/// Set the state's spectral tilt to an ABSOLUTE dB/octave, baking it into
/// `amplitudes` (writing the field alone is inaudible - spectral_morph_engine.h:285-289).
inline void tiltState(SpectralState& s, float dbPerOct) noexcept;
```

**Body:**

1. `if (isNaN(dbPerOct) || isInf(dbPerOct)) return;`
2. `if (!isValidSpectralState(s)) return;` — the invalid branch, byte-unchanged.
3. `const float target = std::clamp(dbPerOct, SpectralState::kMinStateTiltDbPerOct,
   SpectralState::kMaxStateTiltDbPerOct);`
   `const float current = s.tiltDbPerOct; const float delta = target - current;`
4. `if (s.numPartials <= 0) { s.tiltDbPerOct = target; return; }` — an empty state has no amplitudes to
   bake into; the field is still assigned so the control's readout is truthful.
5. For `i < numPartials`:
   ```cpp
   const float octaves = std::log2(s.ratios[i] / s.ratios[0]);   // ratios[0] >= 0.5 by validity
   s.amplitudes[i] *= std::pow(10.0f, (delta / 20.0f) * octaves);
   ```
6. `normalizeSpectralState(s);` (`:155`)
7. `s.tiltDbPerOct = target;`

**`std::pow(10.0f, x)`, never `exp10f`.** `exp10f` is a glibc GNU extension, absent on MSVC; the repo
already adopted a standing prohibition — *"Deliberately NOT named `exp10f`: glibc declares a global
`exp10f` as a GNU extension"* (`dsp/include/krate/dsp/systems/continuous_body.h:1643-1645`). This site
is configuration-time (message thread), where `makeFactoryState` already evaluates ~200 `std::pow`
calls (`spectral_state.h:371-372`), so the portable form is free. A Layer-2 copy of
`continuous_body.h`'s `exp10Fast` is explicitly **not** taken — that would be a DSP addition outside
the enumerated set.

**Absoluteness proof.** `delta = target − current` undoes the state's existing tilt before applying the
new one, on the *same* expression. Two consecutive `tiltState(s, −6)` calls give
`delta₁ = −6 − 0 = −6` then `delta₂ = −6 − (−6) = 0`, i.e. the second is the identity on amplitudes
(`10^0 = 1`) and re-normalises an already-unit-norm vector by exactly `1.0f`. So the render and
`s.tiltDbPerOct` after two calls equal those after one — SC-013(c)'s equivalence arm.

**Validity proof.** `normalizeSpectralState` divides by `‖a‖₂` when `sumSquares > 0` (`:169`); the L2
norm of a vector is never smaller than its largest element, so every post-normalisation amplitude
satisfies `|aᵢ| ≤ 1`, and all inputs are non-negative and `10^x > 0`, so all outputs are non-negative:
`[0,1]` with **no clamp**. The `sumSquares > 0` guard leaves an all-zero state alone (no NaN). Ratios
are untouched ⇒ monotonicity untouched. `target` is clamped ⇒ the field's range check passes.
Normalisation rescales every amplitude by **one common factor** and therefore changes no *relative*
tilt, which is what keeps the stored field and the bake in agreement.

**Numerical range.** `ratios[i]/ratios[0] ∈ [1, 256]` ⇒ `octaves ∈ [0, 8]`; `delta ∈ [−24, +24]` ⇒
exponent `∈ [−9.6, +9.6]` ⇒ multiplier `∈ [2.5e−10, 4.0e9]`. With `amplitudes[i] ≤ 1` the worst
pre-normalisation element is `4.0e9`, `sumSquares ≤ 64·1.6e19 ≈ 1.0e21` — three orders inside binary32's
`3.4e38`. No overflow, and the small end (`2.5e-10 × 1e-3 = 2.5e-13`) stays well above the `1.2e-38`
normal-float floor, so no denormal is produced.

---

## 2. Layer 3 — the two fan-out groups, and D-1's one gate relaxation

**Test TU (ruling, tasks.md T003):** the fan-out pass-throughs get their **own** dedicated TU,
`dsp/tests/unit/systems/seraphis_partial_fanout_test.cpp`, registered in `dsp_systems_tests`' enumerated
list — rather than folding the cases into an existing TU. Failing-test-first beats file-count minimalism
here, and the phase owner accepted the extra file explicitly. §10.1 states its cases; §13 states the CMake
edit. §2.3's gate relaxation is tested from `tests/integration/partial_edit_test.cpp` (plugin side,
SC-028/SC-029) plus a re-run of Phase 3's existing suite (SC-030) — it needs a *plugin* edit path, so it
does not belong in this TU.

### 2.1 `SeraphisVoice` (`dsp/include/krate/dsp/systems/seraphis_voice.h`)

Three one-line pass-throughs, placed in the existing `-- HarmonicCloud (Phase 2)` forwarder block
(`:641-650`), on the same "no added clamping" contract the block's banner states (`:638`):

```cpp
void setPartialPosition(std::size_t i, float p) noexcept { cloud_.setPartialPosition(i, p); } // :1069
void setPartialMask(std::size_t i, bool active) noexcept { cloud_.setPartialMask(i, active); } // :1084
void clearPartialMask() noexcept { cloud_.clearPartialMask(); }                                // :1101
```

The owners already reject an out-of-range index and a non-finite position (`harmonic_cloud.h:1070-1075`,
`:1085-1087`), so a second guard here would only let the two surfaces disagree about what was stored.

> **MASK POLARITY — the convention every section below obeys, stated once.**
> `HarmonicCloud::setPartialMask(std::size_t index, bool active)`'s body is
> **`masked_[index] = !active;`** (`dsp/include/krate/dsp/systems/harmonic_cloud.h:1082-1089`, read
> verbatim this session). So **`active == true` ⇒ AUDIBLE**, **`active == false` ⇒ SILENCED**, and
> `clearPartialMask()` is `masked_.fill(false)` (`:1101`) ⇒ **everything audible**.
> The plugin-side bit convention is the *opposite* sense by design: **`CloudFrame::maskBits` bit `i` set
> ⇔ partial `i` is masked ⇔ the fan-out must be called with `active = false`.**
> Every call written in this plan therefore reads
> `setPartialMaskAllVoices(i, /*active=*/((maskBits >> i) & 1) == 0)`.
> This paragraph exists because the polarity is inverted in **spec C-4's mask row** (spec `:609`, which
> says `setPartialMaskAllVoices(i, !currentMask)` — for an already-masked partial that is
> `active = false`, i.e. it stays masked, so the documented unmask gesture is a no-op). That spec
> sentence is scheduled for correction in **D-9** (§11.2).

### 2.2 `SeraphisEngine` (`dsp/include/krate/dsp/systems/seraphis_engine.h`)

Three fan-outs over the whole pool, placed beside `applySpectralStates` (`:811`):

```cpp
void setPartialPositionAllVoices(std::size_t index, float position) noexcept {
    for (std::size_t v = 0; v < kMaxVoices; ++v) { voices_[v].setPartialPosition(index, position); }
}
void setPartialMaskAllVoices(std::size_t index, bool active) noexcept { /* same shape */ }
void clearPartialMaskAllVoices() noexcept { /* same shape */ }
```

**`kMaxVoices` (16, `:211`), not `getPolyphony()`** — the same rule `applySpectralStates`' banner
states (`:785-787`): a slot the allocator hands out later must already carry the override. This is
FR-030's fourth clearing event (a polyphony increase) discharged by construction.

**These exist because `getVoice()` is `const`** (`:955`) and the non-const path is
`friend class SeraphisMacroMatrix` (`:997`), which the plugin cannot use.

**Thread ownership — audio thread only.** These three fan-outs write `HarmonicCloud`'s `panPosition_`,
`positionOverridden_`, `panLeft_`/`panRight_` (`harmonic_cloud.h:1069-1079`, `updatePanGains` at
`:1818-1834`) and `masked_` (`:1084-1089`) — **all of which `process()` reads and writes**. Calling them
from the message thread is a data race, and it contradicts the plugin's own ownership discipline:
`spectralSlots_` is annotated `// audio-thread-owned` (`plugins/seraphis/src/processor/processor.h:858`)
and the three-buffer staging ring exists precisely so a message-thread writer never touches engine-facing
state. (`Processor::setActive` is the one place message-thread engine mutation is legal, and it says why:
*"Both branches run on the host thread with the audio thread stopped"*, `processor.cpp:799-801`.) The
repo's own `notify` precedent does not touch the engine either — Membrum stores to an atomic and lets the
audio thread consume it (`plugins/membrum/src/processor/processor.cpp:1191`,
`pendingAudition_.store(word, release)`).

So: **`applyEditMessage` never calls these.** It stages, publishes a flag, and `process()` performs the
fan-out (§6.2 kinds 2/3, §6.3). RT cost per call: 16 `noexcept` scalar stores plus `updatePanGains(index)`
per voice. `updatePanGains` is **two transcendentals, not two `sqrt`** — it calls `equalPowerGains`
(`dsp/include/krate/dsp/core/crossfade_utils.h:50-53`), which is `std::cos(position * kHalfPi)` /
`std::sin(position * kHalfPi)`. The worst case §6.3 must budget for is therefore 64 set pan bits × 16
voices × 2 trig calls = **2048 transcendental evaluations inside a single block**, on top of Phase 10's
measured 22.32 % of the 25 % ceiling — which is why SC-014 gains a measured worst-case arm rather than an
assumed bound (§10.2, SC-014 arm 6).

### 2.3 The D-1 gate relaxation — the phase's ONE non-additive `dsp/` edit

**Ruling:** spec *Clarifications* → D1, encoded in *Non-goals* (SCOPE AMENDMENT), C-4, FR-029, FR-033a,
SC-028 – SC-030.

**What it is today** (`dsp/include/krate/dsp/systems/seraphis_voice.h:770-783`, read verbatim this
session — the doc comment above it at `:762-769` cites `spectral_morph_engine.h:198-207` as its
justification):

```cpp
void setSpectralState(int slot, const SpectralState& s) noexcept {
    if (!isConfigurable()) { ++rejectedConfigCalls_; return; }   // :771-774
    morph_.setState(slot, s);                                    // :775
}
void setSpectralStateCount(int n) noexcept {
    if (!isConfigurable()) { ++rejectedConfigCalls_; return; }   // :778-781
    morph_.setStateCount(n);                                     // :782
}
[[nodiscard]] bool isConfigurable() const noexcept { return !hasSounded_ || isFinished(); }  // :908
```

**What it becomes.** The two early returns are deleted; both bodies become the bare forward:

```cpp
/// Phase 11 FR-033a (D1). NOT configure-time gated: SpectralMorphEngine::setState
/// absorbs a live state swap through the FR-047 fade (spectral_morph_engine.h:312,
/// slotContributes() at :558), which Phase 3's FR-042/FR-044 already prove
/// continuity-safe. The Phase 9 gate was SeraphisVoice's own extra restriction and
/// made a Phase 11 partial edit inaudible until the next note-on.
void setSpectralState(int slot, const SpectralState& s) noexcept { morph_.setState(slot, s); }
void setSpectralStateCount(int n) noexcept { morph_.setStateCount(n); }
```

**Scope — exactly two call sites, and nothing else.** `isConfigurable()` (`:908`) itself is **kept**, and
every other caller it guards (construction-time seeding, the freeze/steal paths) is **unchanged**;
`rejectedConfigCalls_` (`:1208`) and its reader `getRejectedConfigureTimeCallCount()` (`:784-786`) are
**kept**, because SC-028 asserts that counter does **not** move across the edit push. Deleting the
counter would delete the criterion's observable.

**The same change corrects one comment, and only a comment.** `spectral_morph_engine.h:199-206` currently
reads *"CONFIGURATION-TIME CALLS: prepare(), reset(), setSeed(), setState() and setStateCount() are NOT to
be called while the consumer is sounding"* (verbatim, read this session). `setState` and `setStateCount`
are struck from that list — the surrounding prose already only justifies `reset()` and `setSeed()`
(*"reset() rewinds the travel position and every RNG stream; setSeed() redraws all 64 scatter offsets …
They are named exemptions in FR-044's continuity list"*), and Phase 3's FR-044 names only those two.
**`setState`'s body is not touched** (`:292-315`), which is what makes SC-030 a pure re-run.

**Why this is safe, in the primitive's own terms.** `setState` (`:292`) rejects an invalid state wholesale
(`:296-297`), no-ops on an identical one (`:302-305`, *"Identical -- no fade armed, isStateFadeActive()
untouched"*), and otherwise arms `armStateFade()` **only** `if (slotContributes(slot))` (`:311-313`). All
three branches are exactly the branches an in-DSP morph already takes while sounding. Phase 11 introduces
**no new fade, no new time constant and no new state**.

**RT safety — allocation-free and lock-free, but NOT cost-free, and the cost is what the gate was
accidentally paying for.** Both methods stay `noexcept`, allocation-free, lock-free and exception-free,
and the call still arrives from `Processor::pushSpectralStatesIfPending()` (`processor.cpp:2779`) on the
**audio** thread via the existing three-buffer handoff — Phase 11 adds a message-thread *writer* to the
ring (§6.2), never a message-thread caller of this method. The sentence "RT safety. Unchanged." that
stood here in the previous draft was **wrong**, and this is the correction:

`SpectralMorphEngine::setState` runs `isValidSpectralState` **and** `buildSanitized` — a full 64-entry
`std::log2` pass (`spectral_morph_engine.h:292-301`, `buildSanitized` at `:513`, the `std::log2` loop at
`:537-543`, all read this session) — **before** the identity early-out at `:302-305`. And every handoff
re-arms the whole pool: `Processor::consumeSpectralSlotHandoff()` sets `spectralRetryMask_ = 0xFFFFu`
(`processor.cpp:2834`), so `pushSpectralStatesIfPending()` → `applySpectralStates` walks
16 voices × 4 slots = 64 `buildSanitized` ≈ **4096 `std::log2` in one `process()` call**. Both the
processor (`processor.cpp:2764-2772`) and the engine (`seraphis_engine.h:791-806`) already flag exactly
this arithmetic in writing as a CPU hazard.

**What §2.3 changes about it, stated honestly.** Today a *sounding* voice rejects at
`if (!isConfigurable())` (`seraphis_voice.h:770-776`) — one predicted branch — so a held note makes up to
16 of those 64 slot pushes cheap. After the relaxation **every** voice executes the full pass. And Phase
11 turns a rare event into a **stream**: §7.4 throttles a drag to one message per 33 ms and §6.2's
`stageSlotEdit` publishes a handoff per accepted kind-1/4/5 message, i.e. roughly one 4096-`std::log2`
pass every ~3 blocks at 512 / 48 kHz, measured against Phase 10's pinned 22.32 % of a 25 % ceiling —
**2.68 points of headroom** (R-12).

**This is measured, not argued.** No pre-existing criterion covers it: SC-009, SC-010 and SC-014 arm 7
all run with a **static** slot set, and SC-029 measures continuity rather than time. **SC-031** (§10.2,
new) is the arm — a `[.perf]` case in `tests/integration/ui_perf_test.cpp` that drives a 30 Hz partial
drag (the §7.4 throttle rate) at the 8-voice operating point **with a note held**, and requires
worst-of-seven whole-`process()` ns/block to still satisfy `kFullPolyCeilingNs`
(`param_perf_test.cpp:392`).

**If SC-031 fails, the push gets cheaper — the ceiling does not move.** Two remedies in order, both
inside the plugin and neither a `dsp/` change: (1) narrow `spectralRetryMask_` at `:2834` to the voices
that can still reject rather than the blanket `0xFFFFu` — with the gate relaxed the only remaining
rejection source is the slot set itself, so a full re-arm is now pure waste; (2) add a pre-
`applySpectralStates` identity check against the processor's own last-pushed `spectralSlots_` copy so an
unchanged slot never reaches `buildSanitized` at all. Raising the ceiling, or dropping the throttle rate
below C-8's 30 Hz, are both forbidden.

**Consequence for the retry machinery.** `spectralRetryMask_` and `pushSpectralStatesIfPending()`'s
per-voice retry (`processor.cpp:2779-2830`) stay exactly as they are unless SC-031 forces remedy (1).
With the gate relaxed, every voice accepts on the **first** push, so the retry mask clears in one block
instead of surviving until the held note finishes — the *number of blocks* paying the cost goes down even
as the per-block cost of the first one goes up. Nothing about the handoff protocol changes;
`spectralSlotsHandoff_.store(-1, …)` at `:2830` still runs on the same condition.

**Criteria:** SC-028 (audible within the existing FR-047 window, and
`getRejectedConfigureTimeCallCount()` unchanged across the push), SC-029 (click-free, measured against
Phase 3's own `kMaxAmpDeltaPerChunk` / `kMaxRatioDeltaCentsPerChunk` bounds), SC-030 (Phase 3's
`spectral_morph_*` suites pass **unmodified**), **SC-031** (whole-`process()` block time with an edit
gesture in flight on a held note). §10 rows; R-16 and **R-18** (§12) are the risk entries.

---

## 3. Layer 3 — `SeraphisMacroMatrix`, the fourth target owner (C-10)

File: `dsp/include/krate/dsp/systems/seraphis_macro_matrix.h`. Additive by construction; **no existing
line of behaviour moves**.

### 3.1 Enum and POD

```cpp
enum class SeraphisMacroTargetOwner : std::uint8_t { Voice = 0, Engine, Aether, Effects };  // :52
```

Two target values appended **after the Aether block, immediately before `Count`** (`:87-88`):

```cpp
    AetherDimensionalityTideDepth,
    // -- Effects-owned (each MUST have a 1:1 field in SeraphisEffectsTargets) --
    FxDelaySend,
    FxWanderDepth,
    Count
```

Appending before `Count` keeps every existing target's index unchanged, so `aetherFieldIndex`'s window
`[kFirstAetherTarget, kFirstAetherTarget + kNumAetherTargets)` (`:446-452`) is untouched and
`SeraphisAetherTargets`' field offsets are unchanged.

```cpp
/// The Effects-owned rows as plain floats - no Layer 4 type is named (:105's rule).
/// Fields are declared IN ENUM ORDER; effectsFieldIndex() is a pure offset.
/// Both defaults are the SHIPPED parameter defaults (kFxDelayMixDefault = 0.0f,
/// kFxWanderDepthDefault = 0.0f; plugins/seraphis/src/parameters/effects_params.h:105, :117),
/// which is what makes the composition an identity at the FR-060 neutrals.
struct SeraphisEffectsTargets {
    float delaySend   = 0.0f;
    float wanderDepth = 0.0f;
};
```

New class constants beside `kFirstAetherTarget` (`:166-168`):

```cpp
static constexpr std::size_t kFirstEffectsTarget =
    static_cast<std::size_t>(SeraphisMacroTarget::FxDelaySend);
static constexpr std::size_t kNumEffectsTargets = 2;
static constexpr std::size_t kNumRows = 32;   // was 30 (:163)
```

### 3.2 The two rows

Appended to `kRows` (the table ends at `:434`):

| Macro | Owner | Target | `base` | `amount` | `curve` |
|---|---|---|---|---|---|
| `Dissolve` | `Effects` | `FxDelaySend` | `0.0f` | **pilot, §8.6** (start 0.35f) | `Linear` |
| `Entropy` | `Effects` | `FxWanderDepth` | `0.0f` | **pilot, §8.6** (start 0.50f) | `Linear` |

`.base = 0.0f` is the shipped default of the parameter each composes into, which is FR-060's rule and
what makes the composition an exact identity at neutral. `ModCurve::Linear` — `Stepped` is forbidden by
`noRowUsesSteppedCurve` (`:495-503`, asserted `:820`).

### 3.3 The guards — extended, never relaxed

```cpp
[[nodiscard]] static constexpr int effectsFieldIndex(SeraphisMacroTarget t) noexcept {
    const auto i = static_cast<std::size_t>(t);
    if (i < kFirstEffectsTarget || i >= (kFirstEffectsTarget + kNumEffectsTargets)) { return -1; }
    return static_cast<int>(i - kFirstEffectsTarget);
}

[[nodiscard]] static constexpr bool everyEffectsRowHasAPodField(
    const std::array<SeraphisMacroRow, kNumRows>& rows) noexcept {   // mirrors :480
    for (const SeraphisMacroRow& row : rows) {
        if (row.owner == SeraphisMacroTargetOwner::Effects && effectsFieldIndex(row.target) < 0) {
            return false;
        }
    }
    return true;
}
```

`everyRowOwnerIsValid`'s ladder (`:466-474`) gains the matching **biconditional**, so a row that names
an effects target with a Voice owner is a compile error:

```cpp
const bool isAetherTarget  = (aetherFieldIndex(row.target)  >= 0);
const bool isAetherOwner   = (row.owner == SeraphisMacroTargetOwner::Aether);
const bool isEffectsTarget = (effectsFieldIndex(row.target) >= 0);
const bool isEffectsOwner  = (row.owner == SeraphisMacroTargetOwner::Effects);
if (isAetherTarget  != isAetherOwner)  { return false; }
if (isEffectsTarget != isEffectsOwner) { return false; }
if (!isAetherOwner && !isEffectsOwner
    && row.owner != SeraphisMacroTargetOwner::Voice
    && row.owner != SeraphisMacroTargetOwner::Engine) { return false; }
```

`everyTargetInFr061to065IsPresent` (`:508`, asserted `:822`) and `everyRowSharesOneBasePerTarget`
(`:530`, asserted `:824`) continue to hold for the two new targets by the rows above. A new namespace-
scope assertion joins the six at `:814-825`:

```cpp
static_assert(SeraphisMacroMatrix::everyEffectsRowHasAPodField(SeraphisMacroMatrix::kRows),
              "C-10: every Effects row must have a 1:1 SeraphisEffectsTargets field");
static_assert(static_cast<std::size_t>(SeraphisMacroTarget::Count) == 29,
              "C-10 / SC-021(d): 27 pre-Phase-11 targets + EXACTLY 2; a third needs a spec amendment");
```

**27 counted from the enum this session** (`seraphis_macro_matrix.h:55-89`): 19 Voice-owned
(`CloudInharmonicity` … `EnvReleaseMs`) + 8 Aether-owned (`AetherMix` … `AetherDimensionalityTideDepth`).
That is the same 27 `pushMacroSurfaces()`' banner calls *"the 27 MB bases"* (`processor.cpp:2731`) and
that `lastPushedBase_` is sized by (`processor.h:~875`, `std::array<float, kNumTargets>`). Re-verify the
literal against the compiler on the first build; the assertion's job is to make a **third** addition a
build break.

### 3.4 The pure reader

```cpp
/// Pure function of the knobs and the table; writes nothing. A copy of
/// computeAetherTargets (:667-679) for the Effects half.
[[nodiscard]] SeraphisEffectsTargets computeEffectsTargets() const noexcept {
    const std::array<float, kNumTargets> v = evaluateAll();
    SeraphisEffectsTargets out{};
    out.delaySend   = at(v, SeraphisMacroTarget::FxDelaySend);
    out.wanderDepth = at(v, SeraphisMacroTarget::FxWanderDepth);
    return out;
}
```

**`apply(SeraphisEngine&)` (`:623`) gains no line** — an Effects-owned target is read by the plugin,
exactly as Aether's are. `evaluateAll()` (`:782`) is unchanged; it is generic over `kNumTargets` and
already handles the two new rows.

**Identity at neutral is arithmetic, not a fast path.** The class's own banner states *"at neutral
every term is exactly 0 — `applyModCurve(c, 0) == 0` for all three permitted curves"* (`:777-781`),
and `neutralFor()` returns 0.5 for Gravity and 0 for the rest (`:548-550`). With `base = 0` and both
consumed parameters defaulting to 0, `computeEffectsTargets()` returns `{0.0f, 0.0f}` bit-for-bit at the
shipped neutrals — which is what lets SC-001 keep its exact-equality form (FR-039).

---

## 4. The processor — macro reach into the effects surface (D-2's corrected seam)

### 4.1 The base half (unchanged shape)

`pushMacroSurfaces()` (`processor.cpp:2705-2748`) already iterates `t < kNumTargets` and calls
`macros_.setTargetBase(target, baseValueForTarget(target))` under a per-target on-change +
class-(b)-settling guard (`:2731-2746`). **Two `case`s are added to `baseValueForTarget`** (whose Aether
arm is at `:2648-2649`):

```cpp
case Target::FxDelaySend:   return effectsParams_.delayMix.load(kRelaxed);     // ID 1410
case Target::FxWanderDepth: return effectsParams_.wanderDepth.load(kRelaxed);  // ID 1441
```

This is the seam `setTargetBase`'s banner calls *"a deep parameter IS the origin the macros move from
rather than a second, competing write path"* (`seraphis_macro_matrix.h:684-685`). There remains
**exactly one** base writer per target. `setTargetBasePushes_` grows from 27 to 29 at prepare —
`param_cadence_test.cpp`'s SC-007 table must be updated in the same change (§9).

**Note on class-(b) settling.** `targetClassBUnsettled(target)` must return the right answer for the two
new targets. 1410 and 1441 *are* class-(b) IDs (they ride `fxReturnGainSm_` / `fxWanderDepthSm_`), so
their entries in `kContinuityMechanism[]` and the `targetClassBUnsettled` switch must name those two
smoothers. Read the existing table before editing; do not invent a row shape.

### 4.2 The composed half — **substituted reads, not re-pointed guards**

`Processor::process()` runs, before the slice loop (`processor.cpp:1082-1084`, `:1192-1207`):

```
updateEffectsBypassState(total);      // :1082  -> reads delayMix RAW at :2351
pushEffectsParams();                  // :1084
{ fxWanderRuns_ = ... }               // :1119-1134 -> reads wanderDepth RAW at :1126
setParamSmootherTargets();            // CALLED AT :1207 -> :3051 fxReturnGainSm_, :3052 fxWanderDepthSm_
...slice loop... { advanceParamSmoothers(n); pushVoiceParams(); pushMacroSurfaces(); renderSlice(...); }
```

**The function is `setParamSmootherTargets()`, not `updateParamSmootherTargets()`** — no symbol of the
latter name exists. It is called **once per `process()` call at `processor.cpp:1207`** (verified this
session) and contains the two cited lines `:3051` / `:3052`; recording the call site is what makes the
ordering claim below ("after the `fxWanderRuns_` block at `:1119-1134`") checkable by grep rather than
by assertion.

Add **one** member and **one** pre-slice line, immediately before `updateEffectsBypassState(total)`:

```cpp
// processor.h
Krate::DSP::SeraphisEffectsTargets composedEffects_{};   // this call's composed FX targets
```

```cpp
// processor.cpp, before :1082
composedEffects_ = macros_.computeEffectsTargets();   // seraphis_macro_matrix.h, C-10
updateEffectsBypassState(total);
```

Then **three substitutions**, and nothing else:

| # | Site | Today | Phase 11 |
|---|---|---|---|
| 1 | `updateEffectsBypassState`, `:2351` | `const float mix = effectsParams_.delayMix.load(kRelaxed);` | `const float mix = std::clamp(composedEffects_.delaySend, 0.0f, 1.0f);` |
| 2 | `setParamSmootherTargets`, `:3052` | `fxWanderDepthSm_.setTarget(effectsParams_.wanderDepth.load(kRelaxed));` | `fxWanderDepthSm_.setTarget(std::clamp(composedEffects_.wanderDepth, 0.0f, 1.0f));` |
| 3 | **the `fxWanderRuns_` block, `:1126`** | `\|\| effectsParams_.wanderDepth.load(kRelaxed) != 0.0f` | `\|\| std::clamp(composedEffects_.wanderDepth, 0.0f, 1.0f) != 0.0f` |

**Substitution 3 was missing from the previous draft, and it is the one that decides whether the wander
stage runs at all.** `processor.cpp:1119-1134` computes
`fxWanderRuns_ = width != kDefaultWidth || wanderDepth != 0.0f || azimuthDepth != 0.0f`, reading the
**raw** atomic at `:1126` (verified verbatim this session). With the shipped deep default
`kFxWanderDepthDefault = 0.0f` (`effects_params.h:117`), an **Entropy-macro-only** move would never set
that documented ENGAGE predicate. The stage would still engage — but only through the FR-010a
*disengage latch*, `fxWanderRunsEffective_ = fxWanderRuns_ || !wanderAtIdentity()` (`:1133`), because
`wanderAtIdentity()` returns false once `fxWanderDepthSm_` is incomplete or non-zero (`:2313-2315`). And
since `setParamSmootherTargets()` runs at `:1207`, **after** `:1133`, the smoother is still at 0 on the
first block of a macro move, so engagement would land **one further block late** — a second, undesigned
engage latency on top of the one-block composition lag §4.2 already accepts, and SC-021(a)'s "strictly
monotonically in RMS from exactly 0.0 at neutral" five-point sweep would be measuring that undesigned
path. Substituting at `:1126` makes the macro and deep paths engage on the **same** block, with the
**same** predicate, and `composedEffects_` is already available there: it is assigned before `:1082`,
which is before `:1119`.

`:3051` (`fxReturnGainSm_.setTarget(fxEffectiveReturnGain_)`) needs **no edit** — `fxEffectiveReturnGain_`
is derived from `mix` inside `updateEffectsBypassState` (`:2354`), so substitution 1 carries
through to the send's engage/bypass ramp and to FR-023a's freeze-forced gain, unchanged.

**The clamp is required and is not "extra behaviour".** `computeEffectsTargets()` returns the RAW sum
(`computeAetherTargets`' documented contract, `:662-666`: range clamping belongs to the consuming
setter). Both consumers here are unit-range: `MidSideProcessor` width scaling and the send's return
gain / `mix != 0.0f` bypass predicate. Clamping at the consumer is the same rule the Aether half
follows in `applyAetherTargets`.

**Cadence and the one-block lag, stated rather than discovered.** `pushMacroSurfaces()` runs *inside*
the slice loop, so `macros_`' knob values and the two new bases are refreshed after `composedEffects_`
was read. A macro or deep-knob move therefore reaches the composed value on the **next** `process()`
call — one block, 10.67 ms at 512/48 kHz. That is inside the 20 ms class-(b) smoothing time both
consumers already impose (`kParamSmoothMs`, `processor.h:~200`), so the lag is a *target-arrival* delay,
never a discontinuity, and it is symmetric between the macro and deep paths (both go through
`pushMacroSurfaces`). Moving the composition into the slice loop is **rejected**: FR-012 fixes
`updateEffectsBypassState` at once per `process()` call, and the send's chunk machine
(`kFxSendChunkSamples = 512`, `processor.h`) is not slice-partitionable.

> **Ruled on 2026-08-03: accepted as designed, no spec weakening.** The phase owner accepted the one-block
> lag rather than relaxing anything, and encoded the consequence in SC-021(a): the five-point sweep is read
> **allowing exactly one block of settle per point**. `pushMacroSurfaces()` is at `processor.cpp:1310`,
> inside the slice loop, and `composedEffects_` is read before `updateEffectsBypassState(total)` at
> `:1082` — both verified this session, so the lag is a fact of the call order, not an implementation
> choice this plan is free to make differently.

**Cost:** one `evaluateAll()` (32 rows × one `applyModCurve`) per `process()` call. At 512/48 kHz that
is ~94 Hz × 32 rows — under 1 µs/s, three orders below SC-009's 10 666 ns/block snapshot budget.

**FR-039 (identity at neutral) falls out:** at every macro neutral, `computeEffectsTargets()` returns
`{base, base}` = `{deep 1410, deep 1441}` bit-for-bit (§3.4), and `std::clamp(x, 0, 1)` on a value the
parameter surface already produced in `[0,1]` is the identity. So the substituted reads are `==` to the
raw atomics they replaced, the FR-010 send-stage skip is taken on exactly the same blocks, and SC-001's
exact-equality form survives.

**Test seams** (never called from `process()`), beside the Phase 9/10 `*ForTest()` block
(`processor.h:162-240`):

```cpp
[[nodiscard]] float composedFxDelaySendForTest()   const noexcept { return composedEffects_.delaySend; }
[[nodiscard]] float composedFxWanderDepthForTest() const noexcept { return composedEffects_.wanderDepth; }
[[nodiscard]] std::size_t composedEffectsRecomputeCountForTest() const noexcept { … }
```

The third is a `std::size_t` incremented once per `process()` call beside the composition — SC-021(c)'s
replacement observable (D-2, now ruled — §11.0).

---

## 5. The cloud-frame data path (C-2)

### 5.1 The payload — `plugins/seraphis/src/processor/cloud_frame.h` (new)

Sits beside the processor exactly as Membrum's `meters_block.h` sits beside its own
(`plugins/membrum/src/processor/meters_block.h:8-11` is the banner shape to copy).

```cpp
#pragma once
// Producer: Processor::publishCloudFrame(), audio thread, ONCE per process() call.
// Consumer: Controller::onDataExchangeBlocksReceived() (UI thread).
// One-way. Nothing about editing travels on this queue (C-2 clause 5).
#include <cstdint>

namespace Seraphis {

struct CloudFrame {                        // POD, little-endian, memcpy'd
    std::uint32_t sequence            = 0;    // monotonic; wrap is benign
    std::uint16_t activeVoices        = 0;    // SeraphisEngine::getActiveVoiceCount()  (:927)
    std::uint8_t  focusVoice          = 0;    // C-2 clause 4
    std::uint8_t  partialCount        = 0;    // 0 .. HarmonicCloud::kMaxPartials (64)
    float         fundamentalHz       = 0.0f; // UNDETUNED f0 (Q6 depends on this)
    float         voiceLevel          = 0.0f; // SeraphisEngine::getVoiceLevel(focus)   (:949)
    float         morphTravelPosition = 0.0f; // SpectralMorphEngine::getTravelPosition() (:434)
    float         frequencyHz[64]     = {};   // DRIFT-INCLUSIVE
    float         amplitude  [64]     = {};   // display amplitude
    float         position   [64]     = {};   // [-1, +1]
    std::uint64_t maskBits            = 0;    // bit i set <=> partial i masked
    std::uint64_t overriddenBits      = 0;    // bit i set <=> pan and/or mask override
};

// 8 (header) + 12 (three floats) + 768 (three float[64]) = 788, + 4 bytes of
// alignment padding before the first std::uint64_t (which forces alignof == 8)
// = 792, + 16 = 808.
static_assert(sizeof(CloudFrame) == 808, "C-2's pinned layout");

inline constexpr std::uint32_t kCloudFrameUserContextId = 0x53434C44u;  // 'SCLD'

}  // namespace Seraphis
```

**Padding discipline.** The 4 interior padding bytes are indeterminate in an aggregate the producer
fills field by field, and they cross a process boundary via `memcpy`. The producer therefore builds the
frame in a **member** `CloudFrame pendingFrame_{};` that is `std::memset`-to-zero **once in
`setupProcessing()`** and only ever field-assigned thereafter — so the padding is deterministically zero
in every published block, and a test may `memcmp` two frames. (A stack local zero-initialised per call
would also work but adds an 808-byte `memset` to every `process()` call for no benefit.)

### 5.2 Handler lifecycle — three new `Processor` overrides

Verbatim from Membrum (`plugins/membrum/src/processor/processor.cpp:1136-1163`, `:1111-1126`):

```cpp
// processor.h
namespace Steinberg::Vst { class DataExchangeHandler; }      // forward declaration
…
Steinberg::tresult PLUGIN_API connect(Steinberg::Vst::IConnectionPoint* other) override;
Steinberg::tresult PLUGIN_API disconnect(Steinberg::Vst::IConnectionPoint* other) override;
Steinberg::tresult PLUGIN_API notify(Steinberg::Vst::IMessage* message) override;   // §6
…
std::unique_ptr<Steinberg::Vst::DataExchangeHandler> dataExchangeHandler_;
```

- `connect()` — call `AudioEffect::connect(other)`; on `kResultTrue`, build the config callback with
  `blockSize = sizeof(CloudFrame)`, `numBlocks = 4`, `alignment = 32`,
  `userContextID = kCloudFrameUserContextId`; `make_unique<DataExchangeHandler>(this, cb)`;
  `onConnect(other, getHostContext())`.
- `disconnect()` — `onDisconnect(other)`, `.reset()`, then `AudioEffect::disconnect(other)`.
- `setActive(TBool)` (**already overridden**, `processor.cpp:801`) — on `true`, build a
  `Vst::ProcessSetup` from the stored sample rate / max block and call
  `dataExchangeHandler_->onActivate(setup)`; on `false`, `onDeactivate()`.

**`onActivate()` ALLOCATES, on the host thread, with the audio thread stopped — and that contradicts a
standing comment which must be amended in the same change.** In the SDK's fallback path (a host with no
`IDataExchangeHandler`), `onActivate` → `Impl::openQueue` does
`std::make_unique<MessageHandler>` + `Timer::create` + `aligned_alloc` × `numBlocks` +
`allocateMessage` (`extern/vst3sdk/public.sdk/source/vst/utility/dataexchange.cpp:76-105`, read this
session). `processor.cpp:794-797` currently states *"Activation does exactly ONE thing, and it allocates
nothing (SC-026 clause 2)"* — that sentence becomes false and is rewritten to say activation allocates
**only** in the DataExchange queue-open path, on the host thread, inside the window
`processor.cpp:799-801` already relies on (*"Both branches run on the host thread with the audio thread
stopped"*). No audio-thread-reachable path gains an allocation.

The affected criterion is `Seraphis_SetActiveDoesNotAllocate`
(`plugins/seraphis/tests/unit/lifecycle_test.cpp:768-791`), which measures a **re**-activation inside
`TestHelpers::AllocationScope` and does `REQUIRE(allocations == 0u)`. It passes today only because the
fixture never calls `connect()` (see §5.3) — an accident, not a design. **Resolution:** the criterion is
*narrowed*, not weakened — SC-026 clause 2 becomes "no allocation on any **audio-thread-reachable**
path", and the test keeps its exact `== 0u` form because the disconnected fixture is exactly the
audio-thread-reachable configuration it measures; a comment at the test states that a *connected*
instance allocates in `onActivate` by SDK design and is out of the criterion's scope.
`tests/unit/lifecycle_test.cpp` is added to §13's **Modified** list for that comment and for the
`processor.cpp:794-797` amendment's sibling edit. **Spec edit required (D-9, §11.2):** SC-026 clause 2's
"allocates nothing" wording.

### 5.3 `publishCloudFrame()` — the producer

Called **once per `process()` call, after the slice loop**, immediately before the
`data.outputs[0].silenceFlags = 0` line (`processor.cpp:~1325`). **Never from `renderSlice`**: the slice
loop subdivides on every MIDI event, on the 2048 cap and (while any class-(b) smoother is unsettled) on
the absolute 64-sample grid (`:1298-1305`), so a per-slice publish would issue up to 8× the frames for
one block and exhaust the 4-block queue inside one call — the same divisor correction Phase 10 made for
its stage counter.

```cpp
void Processor::publishCloudFrame() noexcept {
    // ORDER IS NORMATIVE - see "The handler is NOT a precondition" below.
    // Only the GATE short-circuits; a null handler does not.
    // cloudFrameEnabled_ is std::atomic<bool>, relaxed - see D-6.
    if (!cloudFrameEnabled_.load(std::memory_order_relaxed)) { return; }
    ++cloudFramePublishAttempts_;                       // C-2 clause 7, ATTEMPT counter

    // --- focus voice, C-2 clause 4 ---------------------------------------
    // (a) among non-idle slots, the greatest allocation serial (strictly
    //     increasing across note events, seraphis_engine.h:966-975 -> ties impossible)
    // (b) else retain the previous focus while getVoiceLevel(prev) > kCloudFrameSilenceLevel
    // (c) else slot 0
    …
    const auto& cloud = engine_->getVoice(focus).cloud();      // seraphis_voice.h:827
    const std::size_t n = std::min(cloud.getActivePartialCount(),               // :950
                                   Krate::DSP::HarmonicCloud::kMaxPartials);    // :138

    pendingFrame_.sequence            = ++cloudFrameSequence_;
    pendingFrame_.activeVoices        = static_cast<std::uint16_t>(engine_->getActiveVoiceCount());
    pendingFrame_.focusVoice          = static_cast<std::uint8_t>(focus);
    pendingFrame_.partialCount        = static_cast<std::uint8_t>(n);
    pendingFrame_.fundamentalHz       = (activeVoices > 0) ? cloud.getFundamentalHz()   // :405, D-8
                                                           : 0.0f;                      // Q6/SC-024(B)
    pendingFrame_.voiceLevel          = engine_->getVoiceLevel(focus);          // :949
    pendingFrame_.morphTravelPosition = engine_->getVoice(focus).morph().getTravelPosition(); // :434
    for (std::size_t i = 0; i < n; ++i) {
        pendingFrame_.frequencyHz[i] = cloud.getPartialFrequencyHz(i)      // :955 (UNDETUNED)
                                     * cloud.getPartialDriftDetune(i);     // :991 (MULTIPLIER)
        pendingFrame_.amplitude[i]   = cloud.getPartialCurrentAmplitude(i) // :959
                                     * cloud.getPartialAntiAliasGain(i);   // :973
        pendingFrame_.position[i]    = cloud.getPartialPosition(i);        // :986, already [-1,+1]
    }
    for (std::size_t i = n; i < 64; ++i) {                                 // NEVER stale
        pendingFrame_.frequencyHz[i] = 0.0f;
        pendingFrame_.amplitude[i]   = 0.0f;
        pendingFrame_.position[i]    = 0.0f;
    }
    pendingFrame_.maskBits       = partialMaskBits_;                       // §6.3
    pendingFrame_.overriddenBits = partialPanOverrideBits_ | partialMaskBits_;

    // --- transport ONLY from here down --------------------------------------
    if (dataExchangeHandler_ == nullptr) {
        ++cloudFrameSkippedBlocks_;      // no queue at all: same accounting as no block
        return;
    }
    auto block = dataExchangeHandler_->getCurrentOrNewBlock();
    if (block.blockID == Steinberg::Vst::InvalidDataExchangeBlockID
        || block.data == nullptr || block.size < sizeof(CloudFrame)) {
        ++cloudFrameSkippedBlocks_;      // RECORDED, never gating (C-2 clause 7)
        return;                          // skipped, never retried, never blocked on
    }
    std::memcpy(block.data, &pendingFrame_, sizeof(CloudFrame));
    dataExchangeHandler_->sendCurrentBlock();
}
```

**The handler is NOT a precondition of the attempt — resolution taken, so T008 does not discover it.**
The previous draft returned early on `dataExchangeHandler_ == nullptr` **before** `++cloudFramePublishAttempts_`
and before filling `pendingFrame_`. That made **every** frame-content and cadence criterion unrunnable in
the harness this phase actually uses: `plugins/seraphis/tests/seraphis_test_fixture.h`'s `ProcessorFixture`
does `initialize(nullptr) → setupProcessing → setActive(true)` (`:177-213`) and **never calls
`connect()`** — grepping that file for `connect` / `IConnectionPoint` returns **nothing** (verified this
session) — while the handler is only ever built in `connect()` (§5.2). So in every plugin-side test the
handler is null, and with the old order `cloudFramePublishAttemptCountForTest()` would stay 0
(failing SC-001 arm A's `> 0`, SC-007's equality and SC-026's "keeps incrementing" on a *correct*
implementation) and `lastPublishedFrameForTest()` — the accessor introduced below precisely so
SC-006 / SC-008 / SC-014 / SC-017 need not depend on the queue — would never be written.

**Option (a) is taken:** the gate is the only short-circuit; the counter increments and `pendingFrame_`
is filled unconditionally once the gate is open; **only the transport** (`getCurrentOrNewBlock` /
`memcpy` / `sendCurrentBlock`) is conditioned on a live handler, and a null handler is accounted as a
**skipped block**, which C-2 clause 7 already says is recorded and never gating. Option (b) — extending
`ProcessorFixture` with a peer `IConnectionPoint` — is rejected: it would put a second SDK object into
the boot path of **every** Seraphis test for the benefit of the ones that read frames, and the DataExchange
fallback allocates on `onActivate` (§5.2), which would land inside `lifecycle_test.cpp`'s
`Seraphis_SetActiveDoesNotAllocate` measurement window.

**Spec edit required (D-9, §11.2):** C-2 clause 7's wording must say the attempt counter is incremented
whenever the **gate** is open, independently of whether a queue exists.

**`fundamentalHz` — the undetuned f0. RESOLVED (D-8): it is `cloud.getFundamentalHz()`, and it needs no
new API.**

```cpp
pendingFrame_.fundamentalHz = (activeVoices > 0) ? cloud.getFundamentalHz()   // harmonic_cloud.h:405
                                                 : 0.0f;                     // Q6 / SC-024 arm B
```

The previous draft said *"`HarmonicCloud` exposes no `getFundamentalHz()`"* and fell back to note-derived
bookkeeping. **That was wrong on both halves**, and the correction is what makes this field free:

- `[[nodiscard]] float getFundamentalHz() const noexcept` **does** exist, at
  `dsp/include/krate/dsp/systems/harmonic_cloud.h:405`, returning the `fundamentalHz_` shadow (`:2115`,
  default `220.0f`). Its **only** writer is `setFundamentalHz` (`:383`), which clamps to
  `[kMinFundamentalHz, kMaxFundamentalHz] = [20, 4000]` (`:184-185`, `:387`); that setter's **only** caller
  in the whole tree is `SeraphisVoice::noteOn` (`seraphis_voice.h:529`). So the value is the note
  frequency and nothing else — **no drift multiplier is ever applied to it**, which is precisely the
  drift-exclusion guarantee Q6/SC-024 rests on.
- The note-derived fallback was **not implementable anyway**: `SeraphisEngine::noteOn(std::uint8_t note,
  std::uint8_t velocity)` (`seraphis_engine.h:476`) performs the allocation internally and returns nothing,
  and `dispatchEvent` (`processor.cpp:95`, calling at `:103`) therefore never learns which slot took the
  note; `allocator_.getVoiceNote(i)` is used only inside the engine (`:494`, `:1333`) and is not public.
  Both of the ruling's named candidates fail, and the accessor above is the third that works.

Reachability is already public and `const`: `engine_->getVoice(focus)` (`seraphis_engine.h:955`) →
`.cloud()` (`seraphis_voice.h:827`) → `.getFundamentalHz()`. **Zero `dsp/` additions**, so the closed
enumerated set in *Non-goals* is untouched.

`0.0f` when `activeVoices == 0` is deliberate and is what SC-024 arm B asserts: the shadow retains the
last note's value forever, so publishing it with no voice sounding would make the C4 fallback unreachable
and the two SC-024 arms untestable. **Never `frequencyHz[0]`** — forbidden by name in the ruling and
drift-inclusive by C-2 clause 3.

**The gate is `std::atomic<bool>`, not a plain `bool` (D-6).** It is written from `Processor::notify` on
the **message thread** (§6.2 kind 0) and from `setCloudFrameGateForTest`, and read here on the **audio
thread** every `process()` call. An unsynchronised cross-thread `bool` is a data race under the C++
memory model however benign the codegen looks; spec C-2 clause 6's "plain bool" wording is corrected in
D-6. `relaxed` ordering is sufficient — the flag publishes no other state — and costs one relaxed load per
`process()` call, three orders below SC-010's closed-gate budget. The repo's precedent for a
message→audio flag is the same shape (`plugins/membrum/src/processor/processor.cpp:1191`).

**RT safety (FR-015, SC-011):** a bounded `≤ 64`-iteration read loop over `const` accessors that are
plain array indexes with a bounds test (`harmonic_cloud.h:955-993`), plus one `memcpy` of 808 bytes. No
allocation, no lock, no exception, no I/O, no `std::sort`, **no transcendental** — every field is a load
or a multiply, and D-8 removed the last transcendental candidate: `fundamentalHz` is now a plain member
read (`getFundamentalHz()`, `harmonic_cloud.h:405`) rather than a `440 * exp2((note − 69)/12)`
computation.

**The seam set** (C-2 clause 7 plus the frame observable):

```cpp
[[nodiscard]] std::size_t cloudFramePublishAttemptCountForTest() const noexcept;  // SC-007, SC-010
[[nodiscard]] std::size_t cloudFrameSkippedBlockCountForTest()   const noexcept;  // recorded only
[[nodiscard]] std::size_t renderSliceCountForTest()              const noexcept;  // SC-007 strict >
void setCloudFrameGateForTest(bool open) noexcept;                                // SC-001 both arms
[[nodiscard]] bool dataExchangeHandlerLiveForTest() const noexcept;               // SC-006 arm (i), FR-011

// --- the frame observable (SC-006, SC-008, SC-014, SC-017) -------------------
[[nodiscard]] const CloudFrame& lastPublishedFrameForTest() const noexcept { return pendingFrame_; }
[[nodiscard]] std::uint32_t     cloudFrameSequenceForTest() const noexcept { return cloudFrameSequence_; }

// --- SC-011's lock-free arm: ANDs is_lock_free() over cloudFrameEnabled_,
//     partialOverridesPending_, both bitmasks and partialPanStaging_[0] -------
[[nodiscard]] bool phase11AtomicsAreLockFreeForTest() const noexcept;

// --- SC-009(b) stage instrumentation, modelled on processor.h:390 / :474 -----
[[nodiscard]] double      cloudFrameStageNsForTest()          const noexcept;
[[nodiscard]] std::size_t cloudFrameStageProcessCallsForTest() const noexcept;
void setCloudFrameInstrumentedForTest(bool on) noexcept;   // per-instance, default OFF
```

`setCloudFrameGateForTest` writes the same `cloudFrameEnabled_` flag the C-5 kind-0 message writes; it
is what lets SC-001 run both arms in **one build, one process, one `Processor` instance** — Phase 10's
SC-002 shape.

**Why `lastPublishedFrameForTest()` is required, and why the DataExchange path cannot be the
observable.** Four criteria assert on the *contents* of a published frame — SC-006 (every
`frequencyHz[i]` at the publish instant), SC-008 (aggregates over the whole sequence), SC-014
(`position[k]` on the next published frame) and SC-017 (*"measured entirely on the producer, headless, on
published `CloudFrame`s"*). But the spec itself states that in a headless run `getCurrentOrNewBlock()`
may never hand out a block, and the four counters above return *counts*, not frames. Without this
accessor all four criteria are unimplementable. `pendingFrame_` is filled **on every attempt, before**
`getCurrentOrNewBlock()` is consulted (§5.3's body order), so it is the frame the producer *would have*
published on every attempt — including the skipped ones — and is exactly the "publish instant" the
criteria name. It is `const&` to a member the audio thread writes, so **every test that reads it must
call it between `process()` calls, never concurrently**; the headless suites are single-threaded and
already work that way.

**Stage instrumentation (SC-009(b), SC-010(b)).** The scoped timer wraps the body of
`publishCloudFrame()` but sits **outside the `cloudFrameEnabled_` predicate** — i.e. the timer scope
opens before the gate test and the divisor `cloudFrameStageProcessCallsForTest()` counts every
`process()` call, not every publish. That is what keeps SC-010(b)'s reasoning honest: with the gate
closed the measured stage time is the cost of *testing the gate*, and SC-010(b) still measures
whole-`process()` rather than trusting it. The pair follows Phase 10's precedent exactly —
`effectsStageNsForTest()` (`processor.h:390`), `effectsStageProcessCallsForTest()` (`:398`) and the
per-instance `setEffectsStageInstrumentedForTest(bool)` toggle over a `bool
effectsStageInstrumented_ = false` (`:474`, `:1061-1063`). Instrumentation is **off by default**, so the
`[.perf]` arm that is not measuring the stage pays nothing.

### 5.4 The consumer — `Controller` as `IDataExchangeReceiver`

Membrum's wiring, verbatim (`plugins/membrum/src/controller/controller.h:44`, `:146`, `:365`;
`controller.cpp:1696-1740`):

```cpp
class Controller : public Steinberg::Vst::EditControllerEx1,
                   public VSTGUI::VST3EditorDelegate,          // ALREADY PRESENT (controller.h:23-24)
                   public Steinberg::Vst::IDataExchangeReceiver {
    …
    OBJ_METHODS(Controller, EditControllerEx1)
    DEFINE_INTERFACES
        DEF_INTERFACE(Steinberg::Vst::IDataExchangeReceiver)
    END_DEFINE_INTERFACES(EditControllerEx1)
    DELEGATE_REFCOUNT(EditControllerEx1)
    …
    Steinberg::Vst::DataExchangeReceiverHandler dataExchangeReceiver_{this};  // WITHOUT THIS, NEVER CALLED
    CloudFrame cachedCloudFrame_{};
};
```

- `queueOpened(...)` sets `dispatchOnBackgroundThread = false` (UI thread, no mutex needed).
- `queueClosed(...)` is a no-op (`cachedCloudFrame_` is POD).
- `onDataExchangeBlocksReceived(...)` loops `numBlocks` and `memcpy`s each valid block into
  `cachedCloudFrame_`, so the **last** (most recent) wins — Membrum's documented rule
  (`controller.cpp:1719-1726`). Older blocks are discarded, not queued.
- `notify(IMessage*)` gains the SDK's IMessage fallback: `if (dataExchangeReceiver_.onMessage(message))
  return kResultOk;` before delegating to `EditControllerEx1::notify` — the path a host with no native
  DataExchange takes (`plugins/membrum/src/controller/controller.cpp:1743-1755`).

**`cloud_frame.h` is included by the controller.** It lives under `src/processor/`, which reads like a
cross-include violation but is not: it is a **payload header with no processor dependency** (`<cstdint>`
only), exactly as Membrum's `meters_block.h` is included by both sides. The Constitution's rule is
"never include the processor's or controller's own headers across the boundary"; a shared POD contract
is the sanctioned exception and Membrum is the precedent.

---

## 6. The edit channel (C-5) and the override table

### 6.1 `plugins/seraphis/src/ui/edit_message.h` (new)

```cpp
namespace Seraphis::UI {

inline constexpr const char* kSeraphisEditMessageId   = "SeraphisEdit";
inline constexpr const char* kSeraphisEditAttributeId = "payload";

struct EditMessage {          // POD; moved as ONE binary attribute
    std::uint8_t  kind  = 0;  // 0 EditorGate, 1 PartialRatioAmp, 2 PartialPan, 3 PartialMask,
                              // 4 BlendStates, 5 TiltState, 6 SlotSelect, 7 BlendBegin
    std::uint8_t  slot  = 0;  // 0..3 morph slot (ignored by kinds 0, 2, 3)
    std::uint16_t index = 0;  // partial index 0..63 (kinds 1, 2, 3)
    float         a     = 0.0f;
    float         b     = 0.0f;
};
static_assert(sizeof(EditMessage) == 12);
inline constexpr std::uint8_t kEditKindCount = 8;

}  // namespace Seraphis::UI
```

Field semantics per kind are exactly C-5's table. `kind 0`: `a = 1` open, `a = 0` close.
`kind 1`: `a = ratio`, `b = amplitude`. `kind 2`: `a = position ∈ [-1,+1]`.
`kind 3`: `a = 0/1` (the **toggled** value the controller computed from `CloudFrame::maskBits`).
`kind 4`: `a = t`, `b = slot B as float`. `kind 5`: `a = ABSOLUTE dB/oct`.
`kind 6`: `slot` = newly selected slot. `kind 7`: `b = slot B as float`, `slot` = destination.

**The `ui/` header is included by `processor.cpp`.** That is deliberate and is not a UI dependency: the
file declares a POD and three `constexpr` strings, includes only `<cstdint>`, and names no VSTGUI type.
It is the *wire format*, shared by both sides, and is the one header under `src/ui/` the processor sees.

### 6.2 `Processor::notify` (new override)

```cpp
tresult PLUGIN_API Processor::notify(Vst::IMessage* message) {
    if (message == nullptr) { return AudioEffect::notify(message); }
    if (!FIDStringsEqual(message->getMessageID(), UI::kSeraphisEditMessageId)) {
        return AudioEffect::notify(message);
    }
    const void* data = nullptr; uint32 size = 0;
    if (message->getAttributes()->getBinary(UI::kSeraphisEditAttributeId, data, size) != kResultOk
        || data == nullptr || size != sizeof(UI::EditMessage)) {
        return kResultOk;                        // malformed -> DROPPED SILENTLY (C-5 cl. 5)
    }
    UI::EditMessage m{};
    std::memcpy(&m, data, sizeof(m));
    applyEditMessage(m);
    return kResultOk;
}
```

**`applyEditMessage(const EditMessage&)`** runs on the **message thread**, which is the same thread
`Processor::setState` already writes the staging ring from — so the *"THREE staging buffers, not one …
writer interlock"* argument (`processor.h:859-861`) holds unchanged, with **no second interlock and no
lock**. Validation first, per C-5 clause 5:

```
if (m.kind >= kEditKindCount)                                  return;   // unknown kind
if (kind in {1,4,5,6,7} && m.slot > 3)                         return;
if (kind in {1,2,3}     && m.index >= 64)                      return;
if (!isFiniteBits(m.a) || !isFiniteBits(m.b))                  return;   // bit pattern, never std::isnan
```

Then the dispatch:

| kind | Action |
|---|---|
| 0 `EditorGate` | `cloudFrameEnabled_.store(m.a != 0.0f, std::memory_order_relaxed);` — the C-2 clause 6 gate, an **`std::atomic<bool>`** (D-6). |
| 1 `PartialRatioAmp` | `stageSlotEdit(m.slot, [&](SpectralState& s){ setPartial(s, m.index, m.a, m.b); });` |
| 4 `BlendStates` | if `!blendSnapshotValid_` → **drop** (C-5 cl. 5, SC-025 arm 3). Else `stageSlotEdit(m.slot, [&](SpectralState& s){ s = blendStates(blendSnapshotA_, spectralSlots_[bIdx], m.a); });` |
| 5 `TiltState` | `stageSlotEdit(m.slot, [&](SpectralState& s){ tiltState(s, m.a); });` |
| 7 `BlendBegin` | `blendSnapshotA_ = currentSlotForEdit(m.slot); blendSnapshotValid_ = true;` — **writes the ring not at all.** |
| 2 `PartialPan` | `partialPanStaging_[m.index].store(clamp(m.a, -1, 1), relaxed); partialPanOverrideBits_ \|= bit; partialOverridesPending_.store(true, release);` — **no engine call.** |
| 3 `PartialMask` | `const bool masked = (m.a != 0.0f); set/clear bit in partialMaskBits_; partialOverridesPending_.store(true, release);` — **no engine call.** |
| 6 `SlotSelect` | `selectedEditSlot_ = m.slot; blendSnapshotValid_ = false;` (a slot change ends any gesture). |

**Kinds 2 and 3 do not touch the engine — that is a correction, not a shortcut, and it OVERRULES a
normative spec sentence.** Spec **C-5 clause 1** (spec `:656-658`) states *"Kinds 1, 4, 5 write the ring;
kinds 2, 3 write `partialOverrides_` and call the C-4 fan-outs directly"* — i.e. on the message thread.
That is wrong: the fan-outs write `HarmonicCloud` state `process()` concurrently reads and writes (§2.2's
ownership paragraph, with the `harmonic_cloud.h` line cites). "Allocates nothing, blocks nothing" answers
*allocation*, not *concurrency*. The deferral below is the same mechanism `stageSlotEdit` /
`spectralSlotsHandoff_` already implements for kinds 1/4/5, and the same one Membrum's `notify` uses.

**Two further spec sentences fall out of the reversal and are scheduled with it (D-9, §11.2).** Spec C-9
(spec `:857-859`, repeated verbatim in FR-042, spec `:1222-1224`) enumerates *"the only additions inside
`process()` are the C-2 predicate, the read-only snapshot loop … and C-10's composition"*. With the
deferral, that enumeration is **incomplete**: `partialOverridesPending_.exchange()` is a fourth addition
and `repushPartialOverrides()` — up to 2048 transcendentals per firing slice (§6.3) — is a fifth. Leaving
C-9/FR-042 as they stand would make the spec's own list of audio-thread additions a false statement
about the shipped build, and SC-001's negative control would be measured against an enumeration that does
not describe what runs.

**The deferral, in full:**

- The message thread writes only members it owns: `partialPanOverrideBits_`, `partialMaskBits_` (both
  `std::atomic<std::uint64_t>`, §6.3) and `partialPanStaging_` (an
  `std::array<std::atomic<float>, 64>`, §6.3), then publishes
  `partialOverridesPending_.store(true, std::memory_order_release)`.
- `process()` consumes it **once per call**, before the slice loop, beside the existing
  `pushSpectralStatesIfPending()` (`processor.cpp:2790-2810`):
  ```cpp
  if (partialOverridesPending_.exchange(false, std::memory_order_acquire)) {
      repushPartialOverrides();          // §6.3 - the fan-out, on the AUDIO thread
  }
  ```
  `exchange(false, acquire)` is the consume-and-clear: a message arriving *during* the fan-out sets the
  flag again and is picked up next call, so no edit is lost and no edit is applied twice in one block.
- Because the fan-out is now audio-thread-only, `repushPartialOverrides()` is the **single** code path
  that reaches `setPartialPositionAllVoices` / `setPartialMaskAllVoices` — the five FR-030 clearing
  events (§6.3) and the edit channel share it.

**`stageSlotEdit(slot, mutate)`** is the one function that touches the staging ring, and it reuses
`setState`'s published sequence verbatim (`processor.cpp:1373-1420`):

1. `const std::size_t w = pickStagingBuffer();` — the existing three-buffer chooser that skips both
   `spectralSlotsHandoff_` and `spectralSlotsConsuming_`.
2. Seed the whole buffer from the current **message-thread-visible** slot set (§6.2a below), so the
   three unedited slots are not lost.
3. `mutate(spectralSlotsStaging_[w][slot]);`
4. **Only if the result is valid**, publish: `spectralSlotsHandoff_.store(w, memory_order_release);
   stagingWriteCursor_ = (w + 1) % 3;`
   `SpectralMorphEngine::setState` rejects an invalid state wholesale (`spectral_morph_engine.h:296-298`),
   so publishing one would be a *silently inert* edit; checking here turns it into a dropped one, which
   is at least consistent with the ring's contents.
5. `++editStageWrites_;` (test seam).

**§6.2a — the message-thread mirror of the slot set.** `spectralSlots_` is **audio-thread-owned**
(`processor.h:858`, and `getState`'s own banner at `processor.cpp:~1440` says *"IT NEVER READS
`spectralSlots_`"*). So `stageSlotEdit` must not read it either. The processor therefore keeps a
**message-thread-only** `std::array<SpectralState, 4> spectralSlotsAuthoring_` that is:
- seeded from `factoryStates_[clampFactoryIndex(id)]` whenever a 409–412 dropdown change is observed
  (mirroring `processor.cpp:1383-1385`; see §6.4);
- overwritten wholesale by `setState()` at the same point it seeds `spectralSlotsStaging_[w]`
  (`:1382-1386`);
- mutated in place by every accepted `stageSlotEdit`.
`stageSlotEdit` seeds `spectralSlotsStaging_[w]` from it, mutates, publishes, and writes the mutated
slot back into it. This is the same "no message-thread read of an audio-thread array" rule Phase 9
established, applied to the second writer. `spectralSlotsAuthoring_` is also what `getState()` writes
(it already writes from the published staging buffer / the factory table, never `spectralSlots_` — read
that code before wiring, and extend it rather than adding a third source).

**RT safety:** `notify()` allocates nothing, blocks nothing, is never reached from `process()`, and —
after the kind-2/3 correction above — **touches no engine-facing state**. The heaviest operation is one
4 × `sizeof(SpectralState)` = 4 × 540 = **2160-byte** POD copy plus one mutator: well under any
message-thread concern, and off the audio thread entirely.

### 6.3 `partialOverrides_` — the table and the re-push (FR-030)

Four members. The message thread **writes** them; the audio thread **reads** them (in
`repushPartialOverrides()` and in `publishCloudFrame()`). Every one of them is therefore atomic:

```cpp
std::array<std::atomic<float>, 64> partialPanStaging_{};   // last authored position per partial
std::atomic<std::uint64_t> partialPanOverrideBits_{0};     // bit i: partial i has an authored pan
std::atomic<std::uint64_t> partialMaskBits_       {0};     // bit i: partial i is masked
std::atomic<bool>          partialOverridesPending_{false}; // release/acquire handshake (§6.2)
```

`std::atomic<std::uint64_t>` and `std::atomic<float>` are lock-free on every target this repo builds
(x86-64 / arm64); assert it once with `is_lock_free()` in the SC-011 TU rather than assuming it. The two
bitmasks and the pan array use **`relaxed`** ordering — they carry no dependency of their own; the
ordering that matters is `partialOverridesPending_`'s **release** store on the writer paired with the
**acquire** `exchange` on the reader (§6.2), which is what makes the pan/mask writes visible before the
fan-out reads them.

An earlier draft kept `partialPan_` a plain `std::array<float, 64>` on the argument that "the audio
thread never reads it". That was false in the same section: `repushPartialOverrides()` walks
`partialPan_[i]`, and four of its five call sites are inside `process()`. The array is atomic.

**`repushPartialOverrides()`** — **audio thread, or the host thread with the audio thread stopped** (the
two host-thread sites are marked ‡ in the table and are legal for the reason
`processor.cpp:799-801` already states: *"Both branches run on the host thread with the audio thread
stopped"*). The flat "audio thread only" label the previous draft carried contradicted two of its own
call sites and §6.4's rule, and as written it invited an implementer to add a message-thread call while
the audio thread is running — exactly the race §6.2's deferral exists to prevent. The exception is
**per-site**, never general: `setState` and any other message-thread path must publish
`partialOverridesPending_` and let `process()` do the fan-out.

**The body walks ALL 64 indices and pushes BOTH polarities for the mask.** This is the correction that
makes the unmask half of FR-028's click-toggle reachable at all:

```cpp
const std::uint64_t panBits  = partialPanOverrideBits_.load(std::memory_order_relaxed);
const std::uint64_t maskBits = partialMaskBits_.load(std::memory_order_relaxed);
for (std::size_t i = 0; i < 64; ++i) {
    const bool masked = ((maskBits >> i) & 1u) != 0u;
    // BOTH polarities, every index: active == !masked (harmonic_cloud.h:1082-1089).
    engine_->setPartialMaskAllVoices(i, /*active=*/!masked);
    if (((panBits >> i) & 1u) != 0u) {
        engine_->setPartialPositionAllVoices(i, partialPanStaging_[i].load(std::memory_order_relaxed));
    }
}
```

**Why walking only the SET mask bits was a defect, not an optimisation.** Kinds 2/3 deliberately make no
engine call (§6.2), so the *only* audio-thread path to `setPartialMask` is this function. The previous
draft re-issued `setPartialMaskAllVoices(i, /*active=*/false)` **for each set mask bit** — so **clearing**
a bit in `partialMaskBits_` produced no engine call whatsoever and `HarmonicCloud::masked_[i]` stayed
`true` forever (`harmonic_cloud.h:1084-1089`, `masked_[index] = !active`). `clearPartialMaskAllVoices()`
is added by FR-033 but was called from no site in the plan. **No criterion detected it**: SC-006(e)
compares `maskBits` against the processor's own table rather than against the engine; SC-014's mask arms
only assert that a mask *survives* a clearing event; and FR-028's click-toggle had no criterion at all
(now **SC-032**, §10.2). Q5's entire clarification exists to make unmasking possible, and the planned
mechanism could not do it. **Full-sweep is also cheaper than the alternative** (a `maskDirtyBits_` set
that must be published, consumed and cleared under the same handshake) and costs 64 predicted branches
plus, in the no-override steady state, 64 stores of `masked_[i] = false` per voice — see the cost note
below, which is re-derived for the full sweep.

`clearPartialMaskAllVoices()` (`harmonic_cloud.h:1101`, `masked_.fill(false)`) is therefore **not** on
this path; it exists for the FR-033 fan-out surface and is exercised by SC-033's fan-out case (§10.1).

It is called from **six** sites: the edit-channel handshake (§6.2) plus one per FR-030/FR-043 clearing
event:

| Event | Thread | Why it clears | Call site |
|---|---|---|---|
| `partialOverridesPending_` (an edit arrived) | audio | the fan-out is deferred off the message thread (§6.2) | before the slice loop, beside `pushSpectralStatesIfPending()` |
| **composed** `CloudStereoSpread` change | audio | `setStereoSpread` → `positionOverridden_.fill(false)` on any **value** change (`harmonic_cloud.h:535-547`) | in `renderSlice`, immediately after `macros_.apply(*engine_)` (`processor.cpp:1858`) |
| `kSeedId` (3) change | audio | `setSeed` → `positionOverridden_.fill(false)` (`:703`) | after the seed burst (`processor.cpp:~1690`) |
| engine `reset()` / `silence()` | **host ‡** | `reset()` → `positionOverridden_.fill(false); masked_.fill(false);` (`:331-332`) | after every engine reset/silence in `setActive` / `setupProcessing` — audio thread stopped (`processor.cpp:799-801`) |
| polyphony increase | audio | a newly-usable slot never received the write | after `setPolyphony` in `pushGlobalParams()` |
| `setupProcessing` re-entry (rate change) | **host ‡** | prepare reaches `cloud_.reset()` | end of `setupProcessing` — audio thread stopped |

**The stereo-spread trigger keys on the COMPOSED value, not on `ParamID` 207.** A draft tracked a
`lastPushedStereoSpread_` against the deep parameter `kCloudStereoSpreadId` (207). That is wrong, and the
defect it lets through is silent and user-visible. `macros_.apply(*engine_)` runs **every slice**
(`processor.cpp:1858`) and calls `voice.setStereoSpread(at(v, SeraphisMacroTarget::CloudStereoSpread))`
(`seraphis_macro_matrix.h:635`); the **Bloom** macro writes that target with
`.base = 0.35f, .amount = 0.60f` (`:252-257`). So moving the Bloom ring — one of the five rings this
phase ships, and the exact knob SC-017 sweeps — changes the pushed spread, `setStereoSpread` fires its
`positionOverridden_.fill(false)`, and **every user pan override is wiped**, with a 207-keyed tracker
seeing nothing.

The detection is therefore: cache `at(v, SeraphisMacroTarget::CloudStereoSpread)` — the value actually
pushed — in a `float lastPushedComposedSpread_` in `renderSlice`, and set the pending flag whenever it
differs from the previous slice's. Note this catches the deep-parameter path too, because 207 is
`CloudStereoSpread`'s `setTargetBase` origin (§4.1) and a deep move changes the composed value as well.

**Cost, re-derived for the full 64-index sweep.** The "runs only on the block the clearing event is
detected" bound **no longer holds** for the spread trigger: a macro smoother ramp changes the composed
spread on many consecutive slices, so the re-push can fire on every slice of a ring sweep. The honest
bound is therefore per-slice, and it now has **two** terms:

- **Mask term, unconditional:** 64 indices × 16 voices = **1024 `masked_[i] = !active` byte stores** per
  firing slice, with **no transcendental** — `setPartialMask`'s body is one bounds test and one store
  (`harmonic_cloud.h:1084-1089`). This is the price of making unmasking reachable, and it is paid even
  with no override authored.
- **Pan term, per set bit:** `popcount(panBits) × 16 voices × 2 trig`, up to **2048 transcendental
  evaluations** with all 64 partials overridden — §2.2's derivation, `updatePanGains` →
  `equalPowerGains` is `std::cos`/`std::sin` (`crossfade_utils.h:50-53`), **not** two `sqrt`.

Three things keep that acceptable and one of them is measured, not asserted:

1. With no overrides authored the **pan** term vanishes entirely and only the 1024 predicted byte stores
   remain — a bounded, branch-free, transcendental-free pass, and the state every SC-009/SC-010 baseline
   runs in. (The previous draft claimed "the whole function is one predicated `if`" in this state; that
   was true only of the walk-set-bits body that could not unmask, and is corrected here rather than kept
   by keeping the defect.)
2. Typical authoring is 0–3 pan overrides, i.e. ≤ 96 trig calls per firing slice on top of the mask term.
3. The 64-partial worst case is **measured**, not assumed: SC-014 arm 7 (§10.2) authors 64 pan overrides
   and sweeps Bloom, and asserts the `[.perf]` block time still fits `kFullPolyCeilingNs`. If it does
   not, the fix is a cheaper fan-out — the first candidate is now a `maskDirtyBits_` / `panDirtyBits_`
   pair published under the same release/acquire handshake so a *steady* override set costs nothing per
   slice, and the second is coalescing the re-push to once per `process()` call rather than once per
   slice — **never** a raised ceiling, and never reverting to a body that cannot unmask.

### 6.4 State persistence — the `[partials]` block (FR-034a, D-3)

**Version stays 3** (`plugin_ids.h:27`). The block is appended **last**, after `[effects]`
(`processor.cpp:1408`), and the loader chain stays EOF-safe with **no version-aware branch** — exactly
the mechanism `processor.cpp:1395-1400` documents.

Layout, 272 bytes:

| Offset | Size | Field |
|---|---|---|
| 0 | 256 | 64 × `float` pan, index order (`writeFloat`/`readFloat`) |
| 256 | 8 | `uint64` panOverrideBits (`writeInt64u`/`readInt64u`) |
| 264 | 8 | `uint64` maskBits (`writeInt64u`/`readInt64u`) |

**`IBStreamer` DOES have 64-bit integer accessors — the previous draft's justification was factually
false.** `IBStreamer` publicly inherits `FStreamer`
(`extern/vst3sdk/base/source/fstreamer.h:202`), which declares public
`bool writeInt64u(uint64)` / `bool readInt64u(uint64&)` (and the signed `writeInt64`/`readInt64`) at
`fstreamer.h:97-106` — all directly callable on an `IBStreamer&`. This is not hypothetical: this
codebase already uses it, at `plugins/disrumpo/src/processor/processor_state.cpp:356`
(`streamer.writeInt64(...)`) and `:908` (`streamer.readInt64(...)`), both read this session. The
four-`int32` split-and-reassemble the previous draft prescribed was therefore **unmotivated work** and is
dropped; each mask moves as one 8-byte field.

**And the split never explained the size anyway (D-3, corrected).** Two 64-bit masks are 16 bytes
whichever accessor writes them, so the block is `256 + 8 + 8 = 272` either way. Spec FR-034a's "≈268 B"
is simply an **arithmetic slip**, not a consequence of any accessor choice. D-3 (§11.2) is restated to
say exactly that, because T026 copies D-3's reasoning into the spec.

- `getState()` appends `savePartialOverrides(streamer)` after `saveEffectsParams`.
- `setState()` appends `loadPartialOverrides(streamer)` after `loadEffectsParams`. It is EOF-safe: each
  `readFloat`/`readInt64u` that fails leaves everything already-read in place and returns, so **a stream
  truncated immediately before the block loads successfully with every override absent** — SC-015's
  backward-compatibility arm. Because the pan array is read before the masks, a *partially* truncated
  block leaves both masks 0 and every pan value therefore unreferenced: absent, not garbage.
- After a successful load, `setState` calls `requestPushAllSurfaces()` (already there, `:1418`) and
  publishes `partialOverridesPending_.store(true, std::memory_order_release)` — the same
  `std::atomic<bool>` handshake §6.2 defines, consumed by the next `process()` call's
  `exchange(false, acquire)`. `setState` runs on the message thread, so this is the *only* legal way for
  a loaded override to reach the voices: it must not call the fan-outs itself (§2.2).
- `Controller::setComponentState` gains the mirror read so the controller's own view of the table is
  correct after a project reload (the block is read and discarded there today; §7.3).

**FR-094 (byte-identical `getState` round trip) survives:** the block is stored values, never
arithmetic results, so a save → load → save is byte-identical by construction — the same argument
Phase 9's `[morph]` payload uses.

---

## 7. The controller

### 7.1 New members and overrides (`controller.{h,cpp}`)

```cpp
// bases
public VSTGUI::VST3EditorDelegate      // ALREADY PRESENT (controller.h:23-24) - do NOT re-add
public Steinberg::Vst::IDataExchangeReceiver          // NEW

// VST3EditorDelegate / IController overrides (NEW)
VSTGUI::CView* createCustomView(VSTGUI::UTF8StringPtr name, const VSTGUI::UIAttributes&,
                                const VSTGUI::IUIDescription*, VSTGUI::VST3Editor*) override;
VSTGUI::IController* createSubController(VSTGUI::UTF8StringPtr name,
                                         const VSTGUI::IUIDescription*,
                                         VSTGUI::VST3Editor*) override;
void didOpen(VSTGUI::VST3Editor* editor) override;
void willClose(VSTGUI::VST3Editor* editor) override;

// IDataExchangeReceiver overrides (NEW) - all THREE are pure virtual in
// extern/vst3sdk/pluginterfaces/vst/ivstdataexchange.h:184, :192, :207. Omit any
// one of them and Controller stays abstract, so createInstance's `new Controller()`
// does not compile. Signatures copied from Membrum (controller.h:130-137).
void PLUGIN_API queueOpened(Steinberg::Vst::DataExchangeUserContextID userContextID,
                            Steinberg::uint32 blockSize,
                            Steinberg::TBool& dispatchOnBackgroundThread) override;
void PLUGIN_API queueClosed(Steinberg::Vst::DataExchangeUserContextID userContextID) override;
void PLUGIN_API onDataExchangeBlocksReceived(Steinberg::Vst::DataExchangeUserContextID userContextID,
                                             Steinberg::uint32 numBlocks,
                                             Steinberg::Vst::DataExchangeBlock* blocks,
                                             Steinberg::TBool onBackgroundThread) override;

// EditControllerEx1 override (NEW) - the IMessage fallback path, §5.4
Steinberg::tresult PLUGIN_API notify(Steinberg::Vst::IMessage* message) override;

// Frame accessor the CloudView timer reads (§8.1)
[[nodiscard]] const CloudFrame& cachedCloudFrame() const noexcept { return cachedCloudFrame_; }

// members (NEW)
Steinberg::Vst::DataExchangeReceiverHandler dataExchangeReceiver_{this};
CloudFrame                                   cachedCloudFrame_{};
std::array<Krate::DSP::SpectralState, 4>     slotMirror_{};        // C-11, DISPLAY ONLY
int                                          selectedSlot_       = 0;
int                                          subControllerInstances_ = 0;   // reset in willClose()
int                                          editorOpenCount_    = 0;       // reset in terminate()
UI::CloudView*                               cloudView_          = nullptr; // zeroed in willClose()
UI::DrawerContainer*                         drawer_             = nullptr; // zeroed in willClose()
std::array<UI::MacroRingKnob*, 5>            macroRings_{};                 // zeroed in willClose()
```

The header's banner at `controller.h:10-11` (*"NO createCustomView / verifyView overrides (FR-018,
FR-056 — there are no custom views until Phase 11)"*) **is rewritten in the same change** (FR-052's
reason: a file that contradicts itself misleads the next reader).

### 7.2 The editor-open refcount (Q7, FR-047, SC-026)

- `didOpen(editor)` → `if (editorOpenCount_++ == 0) sendEditMessage({.kind = 0, .a = 1.0f});`
- `willClose(editor)` → zero every cached raw view pointer, `subControllerInstances_ = 0`, then
  `if (--editorOpenCount_ == 0) sendEditMessage({.kind = 0, .a = 0.0f});` with a `< 0` floor guard.
- `terminate()` → `editorOpenCount_ = 0;` (**not** `willClose()`: `willClose` fires once per closing
  view, `terminate` once per plugin instance, and the refcount must survive the former to do its job —
  FR-041's explicit split).

The processor's gate stays C-2 clause 6's single flag (an `std::atomic<bool>`, D-6); it never learns the
view count.

### 7.3 The `SpectralState` mirror (C-11, Q1, FR-046)

`slotMirror_[4]` is **display-only**, never serialized, never in `process()`, never read back from the
processor. Two re-seed sources and one mutation site:

1. **Dropdown 409–412.** `Controller::setParamNormalized` (or the sub-controller's `valueChanged` for
   the slot dropdowns) maps the normalized value to a `SpectralStateId` via
   `dropdown_mappings.h`'s `kSpectralStateLabels` and assigns
   `slotMirror_[slot] = Krate::DSP::makeFactoryState(id);` (`spectral_state.h:373`) — the *same* source
   the processor's own factory-derivation path uses (`processor.cpp:1383-1385`), so the two do not
   diverge on that path (FR-035, SC-016).
2. **State stream.** `loadMorphParamsToController` (`morph_params.h:521-532`) stops discarding the four
   541-byte payloads. Its signature gains a fourth parameter, matching the processor-side loader's
   shape (`loadMorphParams(MorphParams&, IBStreamer&, std::array<SpectralState,4>&)`):

   ```cpp
   inline void loadMorphParamsToController(Steinberg::IBStreamer& streamer, SetParamFunc setParam,
                                           std::array<Krate::DSP::SpectralState, 4>& mirror);
   ```

   The discard loop becomes a `deserializeSpectralState(scratch.data(), scratch.size(), mirror[i])`
   (`spectral_state.h:274`) whose **return value is ignored deliberately**: a rejected payload leaves
   `mirror[i]` bitwise untouched (`:264-265`, `:300-305`), which is the correct display fallback, and
   the cursor still advanced by the full 541 bytes so the following 55 parameters read from the right
   offset — the original constraint the banner states. **This is the only change this phase makes to
   `morph_params.h`.**
   Add a default argument or an overload so no other caller changes shape.
3. **Mutation.** Every authoring gesture applies the *same Layer 2 function* to `slotMirror_[slot]`
   locally **in addition to** sending the `EditMessage` (FR-029: the local write must never substitute
   for the send). The two are never reconciled; divergence is cosmetic by construction (C-11 clause 4).

### 7.4 Gesture throttling and the terminal flush (Q8, FR-048, SC-027)

One small helper owned by the controller, per gesture:

```cpp
struct EditThrottle {
    std::chrono::steady_clock::time_point lastSend{};
    UI::EditMessage                       pending{};
    bool                                  hasPending = false;
    bool                                  active     = false;
};
```

- `beginGesture()` — `active = true; hasPending = false; lastSend = {}` (so the first move sends).
- `onGestureValue(m)` — `pending = m; hasPending = true;` then
  `if (now - lastSend >= 33ms) { send(pending); lastSend = now; hasPending = false; }`
- `endGesture()` — **unconditionally** `send(pending)` if `hasPending || active`, then `active = false`.
  This is the mandatory flush: at most one redundant identical message, never a dropped final value.

33 ms is C-8's 30 Hz redraw rate, so the message rate can never exceed the rate the view can show.

`sendEditMessage(const UI::EditMessage&)` is one `allocateMessage()` →
`setMessageID(kSeraphisEditMessageId)` → `getAttributes()->setBinary(kSeraphisEditAttributeId, &m,
sizeof(m))` → `sendMessage(msg)` → `msg->release()`. It runs on the UI thread and is a no-op when the
component connection is absent (headless tests), so nothing depends on a live processor.

**It records what it was asked to send, before the connection test:**

```cpp
UI::EditMessage lastSentEditMessage_{};   // UI thread only; SC-027 and SC-032 read it
std::size_t     editMessagesSent_ = 0;    // counts CALLS, not deliveries
[[nodiscard]] const UI::EditMessage& lastSentEditMessageForTest() const noexcept;
[[nodiscard]] std::size_t            editMessageSendCountForTest() const noexcept;
```

Recording **before** the `allocateMessage()`/connection path is what makes SC-032 (the FR-028 gesture
mapping) and SC-027 (the throttle's terminal flush) observable in a headless controller with no
processor attached — otherwise both would be asserting about a call that provably does nothing.

---

## 8. The three custom views + one sub-controller

Namespace `Seraphis::UI`, directory `plugins/seraphis/src/ui/`. All three derive from `VSTGUI::CView`
(directly or transitively) and **nothing else does** — FR-026 / SC-022(a).

### 8.1 `CloudView` (`cloud_view.{h,cpp}`)

```cpp
class CloudView : public VSTGUI::CView {
public:
    CloudView(const VSTGUI::CRect& size, Controller* controller);
    void draw(VSTGUI::CDrawContext* context) override;
    bool attached(VSTGUI::CView* parent) override;   // starts the 30 Hz timer
    bool removed(VSTGUI::CView* parent) override;    // CANCELS the timer (C-7c)
    VSTGUI::CMouseEventResult onMouseDown (VSTGUI::CPoint&, const VSTGUI::CButtonState&) override;
    VSTGUI::CMouseEventResult onMouseMoved(VSTGUI::CPoint&, const VSTGUI::CButtonState&) override;
    VSTGUI::CMouseEventResult onMouseUp   (VSTGUI::CPoint&, const VSTGUI::CButtonState&) override;

    enum class Mode { Observe, Edit };
    void setMode(Mode m) noexcept;                   // driven by the sub-controller (FR-027)
    [[nodiscard]] Mode mode() const noexcept;        // FR-027's default, asserted by SC-004 arm 3
    void setSelectedSlot(int slot) noexcept;

    // Test seams (SC-020, SC-023, and the FR-017 axis/mask arms)
    [[nodiscard]] std::size_t invalidCountForTest()  const noexcept;
    [[nodiscard]] std::size_t drawCountForTest()     const noexcept;
    [[nodiscard]] std::size_t pointsDrawnForTest()   const noexcept;
    void onTimerForTest() noexcept;                  // invokes the timer body directly

    /// FR-017 arm 1: the pure axis map, exposed so monotonicity + clamping are
    /// testable without a draw context. Same function draw() calls.
    [[nodiscard]] VSTGUI::CCoord yFromHzForTest(float hz) const noexcept;
    [[nodiscard]] VSTGUI::CCoord xFromPositionForTest(float p) const noexcept;
    /// FR-017 arm 2: the drawn point set of the last draw(), in draw order -
    /// {x, y, radius, hollow} per point. Lets SC-020's masked arm assert a masked
    /// partial with amplitude 0 is PRESENT at kMaskedRingRadius, not culled.
    struct DrawnPoint { VSTGUI::CCoord x, y, radius; bool hollow; };
    [[nodiscard]] const std::vector<DrawnPoint>& drawnPointsForTest() const noexcept;
    /// FR-017 arm 3: the hit test, so "a masked partial stays a click target" is a
    /// criterion rather than a comment. Returns -1 for a miss.
    [[nodiscard]] int hitTestForTest(const VSTGUI::CPoint& p) const noexcept;

    /// SC-023 / SC-020(g): the ONLY headless way to make draw() run.
    /// `exerciseEditorLifecycle` structurally cannot do it - see the box below.
    void renderForTest() noexcept;
    CLASS_METHODS(CloudView, VSTGUI::CView)
};
```

**`renderForTest()` exists because NOTHING in the harness ever paints.** SC-023 (spec `:1605-1610`)
requires *"the cloud view's `draw()` is entered at least once per cycle"* during
`exerciseEditorLifecycle`, and SC-020 arm (g) says *"call `onTimerForTest()` + `draw()`"* and then
asserts on `drawnPointsForTest()`. Neither is reachable:
`tests/test_helpers/editor_lifecycle_harness.h:98-133` calls only `IPlugView::attached(nullptr, …)` and
`removed()`, and the harness's own banner records why — *"The platform attach itself is a no-op here
(`CFrame::open(nullptr)` returns false harmlessly)"* (`:12-13`, read this session). No platform window
means no paint cycle and no `CDrawContext` ever exists; `draw(nullptr)` is not a defined call.

**Resolution:** `renderForTest()` builds a `VSTGUI::COffscreenContext` sized to the view's own rect
(`COffscreenContext::create(getViewSize().getSize())`), calls `draw(context)` through it, and returns.
It is the *same* `draw()` the platform would call — not a re-implementation — so `drawCountForTest()`,
`pointsDrawnForTest()` and `drawnPointsForTest()` all reflect the real body. If a leg's offscreen
context cannot be created headlessly, the **fallback is a `computeDrawnPointsForTest()` that runs
`draw()`'s point-building loop with the context-emitting calls factored out** — i.e. `draw()` becomes
`buildPoints(); emit(context);` and the seam calls `buildPoints()` only. What is **not** acceptable is
leaving SC-023 and SC-020(g) pointed at a code path the harness cannot enter; both criteria are
re-pointed at this seam in §10.2.

`drawnPointsForTest`' backing vector is **reserved to 64 once in the constructor** and only ever
`clear()`ed + `push_back`ed during `draw()`, which runs on the UI thread — no audio-thread allocation
is implied and SC-011's corpus does not include `cloud_view.cpp`.

**Timer (C-8, FR-018).** A `VSTGUI::SharedPointer<VSTGUI::CVSTGUITimer>` at 33 ms (Membrum's rate,
`pad_grid_view.h:32-34`), created in `attached()` and **cancelled in `removed()`**
(`pad_grid_view.h:37`). Its body reads `controller_->cachedCloudFrame()`, compares `sequence` against
the last seen value, and calls `invalid()` **only when it changed** — so an idle transport costs one
timer callback and no redraw. `onTimerForTest()` exposes exactly that body so SC-020 can drive it
deterministically.

**Axis mapping (FR-017).**
- `x = left + (position[i] + 1) * 0.5 * width` — `position[i]` is already `[-1,+1]`
  (`harmonic_cloud.h:986`). Clamped, never wrapped.
- `y`: `u = (log2(clamp(frequencyHz[i], 20, 20000)) - log2(20)) / (log2(20000) - log2(20))`, drawn
  **inverted** (`y = bottom - u * height`) so high partials are high. Fixed span constants
  `kViewMinHz = 20.0f`, `kViewMaxHz = 20000.0f` — a *fixed* span, never an autoscale, so
  `kCloudStereoSpreadId = 0` (64 coincident points) cannot divide by zero.
  A frame with `frequencyHz[i] == 0` (the zero-filled tail, `i >= partialCount`) is never reached:
  the loop runs `i < partialCount`.
- `radius = kMinRadius + amplitude[i] * (kMaxRadius - kMinRadius)`; `amplitude[i] == 0` draws at
  `kMinRadius` (a fading partial dissolves; it is not culled with a discontinuity). **`kMinRadius`
  is 0 for unmasked partials** so "zero radius, not culled" holds literally.
- **Masked exception (Q5, FR-017):** if `maskBits & (1ull << i)`, draw a **hollow ring** at a fixed
  `kMaskedRingRadius > 0` regardless of `amplitude[i]`, so a masked partial (whose amplitude has
  smoothed to 0) stays a click target for the reverse gesture. This is the one case where radius is not
  a monotone function of amplitude.
- `overriddenBits` tints the point (user-authored marker). Display only.

**Null-frame safety (FR-019, SC-023).** `cachedCloudFrame_` is a **value member**, never a pointer, and
is value-initialised — so a `draw()` with no frame ever received sees `partialCount == 0` and renders an
empty field. There is no null to dereference. `pointsDrawnForTest()` returns 0 in that state.

**Edit mode (FR-028) — the four gestures.**

| Gesture | Detection | Message |
|---|---|---|
| Vertical drag | `|dy| > |dx|`, no alt | kind 1: `a = newRatio`, `b = mirror.amplitudes[i]` (unchanged) |
| Alt + vertical drag | `buttons.getModifierState() & kAlt` (plain VSTGUI `CButtonState` — **never** a platform key API, FR-005) | kind 1: `a = mirror.ratios[i]` (unchanged), `b = newAmp` |
| Horizontal drag | `|dx| >= |dy|` | kind 2: `a = clamp(x → [-1,+1])` |
| Click (no drag) | mouse-up within `kClickSlopPx` of mouse-down | kind 3: `a = (maskBits bit i) ? 0.0f : 1.0f` — a **toggle** |

Hit test: nearest point within `kHitRadiusPx`; masked partials use `kMaskedRingRadius` so they remain
hittable.

**Three of these four gestures had no criterion at all — SC-032 (§10.2, new) is the arm.** Every test
that exercises editing injects `EditMessage`s at `Processor::notify` instead of driving the view: SC-014
uses "a kind-2 pan edit (message → notify)", SC-025 sends kinds 7/4 directly, SC-018 fuzzes `notify`, and
SC-013(d) tests the Layer-2 mutator. Only the plain vertical ratio drag is covered at view level, by
SC-024. A `CloudView` that emitted **kind 2 for an alt-drag**, that **never set `b`**, or that sent an
**unconditional mask instead of the toggle** would pass every criterion in §10 — and FR-028 exists
precisely to keep `EditMessage::b` from being a dead field and to make unmasking reachable from the UI
(the view half of the same defect §6.3 fixes on the processor side). SC-032 drives
`onMouseDown`/`onMouseMoved`/`onMouseUp` with synthetic `CPoint` / `CButtonState` sequences and asserts
the emitted `EditMessage`'s `kind`, `index`, `a` and `b` for all four rows, including alt-modifier
detection and the click toggle computed from `maskBits`. It needs one more seam — a
**send sink**: the controller's `sendEditMessage` is already a no-op with no component connection
(§7.4), so `CloudView` routes through `Controller::sendEditMessage` and the test reads
`Controller::lastSentEditMessageForTest()` (a plain `EditMessage` member the send path records
unconditionally, UI thread only, no processor required).

**The vertical drag's inverse map (Q6, C-4, SC-024) — the load-bearing arithmetic:**

```
referenceHz = (frame.activeVoices > 0 && frame.fundamentalHz > 0) ? frame.fundamentalHz   // UNDETUNED
                                                                  : 261.63f;              // C4
// the axis the user drags against, drawn from the MIRROR, not the frame:
yOf(i)      = yFromHz(slotMirror_[selectedSlot_].ratios[i] * referenceHz)
// the inverse:
newRatio    = hzFromY(pointerY) / referenceHz
```

`frequencyHz[i]` is **never** used in this map. It is drift-inclusive by definition (C-2 clause 3), so
using it would bake momentary Brownian detune into the stored ratio. `fundamentalHz` is the undetuned
f0 precisely so this map is drift-free, and the C4 fallback makes authoring work identically with no
note held (SC-024's two arms must produce the same stored ratio within float epsilon).

**In Edit mode the constellation still animates** (C-8): only the *dragged* partial is drawn at the
pointer; every other point keeps following the frames.

**Draw order (FR-006).** The cloud view is the **first** child added to the editor template's content
area, so the five rings, the Obs/Edit toggle and the drawer draw over it (VSTGUI z-order is child
order).

### 8.2 `MacroRingKnob` (`macro_ring_knob.h`)

```cpp
class MacroRingKnob : public Krate::Plugins::ArcKnob {           // arc_knob.h:49
public:
    MacroRingKnob(const VSTGUI::CRect& size, VSTGUI::IControlListener* l, int32_t tag)
        : ArcKnob(size, l, tag) {}
    MacroRingKnob(const MacroRingKnob& other) : ArcKnob(other) {}
    void draw(VSTGUI::CDrawContext* context) override;   // ring styling over ArcKnob's arc
    CLASS_METHODS(MacroRingKnob, Krate::Plugins::ArcKnob)
};

struct MacroRingKnobCreator : VSTGUI::ViewCreatorAdapter {       // arc_knob.h:555-564 shape
    MacroRingKnobCreator() { VSTGUI::UIViewFactory::registerViewCreator(*this); }
    VSTGUI::IdStringPtr   getViewName()     const override { return "MacroRingKnob"; }
    VSTGUI::IdStringPtr   getBaseViewName() const override { return VSTGUI::UIViewCreator::kCControl; }
    VSTGUI::UTF8StringPtr getDisplayName()  const override { return "Macro Ring Knob"; }
    VSTGUI::CView* create(const VSTGUI::UIAttributes&, const VSTGUI::IUIDescription*) const override {
        return new MacroRingKnob(VSTGUI::CRect(0, 0, 96, 96), nullptr, -1);
    }
};
inline MacroRingKnobCreator gMacroRingKnobCreator;   // arc_knob.h:714-716's rule
```

**Why a `ViewCreatorAdapter` and not `createCustomView` (C-7a):** it must accept `control-tag` and every
`CControl` attribute from the uidesc, which is exactly what `getBaseViewName() -> kCControl` buys
(`arc_knob.h:562-564`). `createCustomView` views are `CView`s the factory does not decorate with
`CControl` attributes.

**FR-021 — the perturbation is the real DSP.** `MacroRingKnob` performs the standard
`beginEdit/performEdit/endEdit` on its `ParamID` and does **nothing** to the cloud view. The visible
motion in the constellation is whatever the next `CloudFrame` reports, i.e. the real
`SeraphisMacroMatrix` response read back out of the engine. No view-local animation, no synthetic
displacement, no interpolation toward a target the DSP is not producing.

**And that sentence is now a test, not a design note.** SC-017 measures `P` on the *producer* — a
`CloudView` that faked the constellation's reaction to a ring (exactly the defect FR-021 forbids) would
leave `P` untouched and SC-017 would still pass. The view side is covered by **SC-022(c)** (§10.2): with
a fixed cached frame (constant `sequence`), driving `MacroRingKnob::valueChanged`/`performEdit` must
leave `invalidCountForTest()` and `drawnPointsForTest()` **unchanged** — i.e. there is no path from a
macro value to a point position inside the view.

**`entry.cpp` (FR-052)** gains `#include <ui/arc_knob.h>` (registers `gArcKnobCreator`) **and**
`#include "ui/macro_ring_knob.h"`, because an inline creator object only registers in a TU that is
actually linked. `toggle_button.h` is **not** included: the freeze cluster is `CCheckBox` (§9), which is
a stock VSTGUI class needing no creator registration, and FR-025 enumerates the drawer's permitted
classes as `ArcKnob` / `CSlider` / `COptionMenu` / `CCheckBox` — `ToggleButton` appears nowhere in the
shipped uidesc, so registering its creator would be dead weight and would leave the intent ambiguous for
the next reader. The standing prohibition at `plugins/seraphis/src/entry.cpp:12-14` is rewritten in the
same change.

### 8.3 `DrawerContainer` (`drawer_container.{h,cpp}`)

```cpp
class DrawerContainer : public VSTGUI::CViewContainer {
public:
    static constexpr int kTabCount = 7;   // Cloud, Morph, Body, Atmos, Aether, FX, Life/Env
    DrawerContainer(const VSTGUI::CRect& collapsedRect);
    void setOpen(bool open) noexcept;     // toggles between the two EXACT rects
    [[nodiscard]] bool isOpen() const noexcept;
    void setActiveTab(int index) noexcept;
    [[nodiscard]] int  activeTab() const noexcept;
    bool removed(VSTGUI::CView* parent) override;   // cancels the slide timer
    CLASS_METHODS(DrawerContainer, VSTGUI::CViewContainer)
};
```

- Two rects and no others (FR-023, C-1): collapsed `(0, 670, 1000, 700)`, open `(0, 420, 1000, 700)`.
  Both are `constexpr` in the header so SC-020(c) can byte-compare against the same constants.
- **`getViewSize()` is in PARENT coordinates, so the drawer MUST be a direct child of the 1000 × 700
  template root.** This is a hard constraint, not a layout preference. An earlier draft nested the drawer
  two levels deep (root → a `sub-controller="SeraphisEdit"` container at `origin="0, 32" size="1000, 668"`
  → the drawer). Relative to that parent, the declared `(0, 670, 1000, 700)` is *absolute*
  `(0, 702, 1000, 732)` — 32 px below the window and entirely outside the parent's 668 px height, so the
  collapsed strip would be clipped invisible; and the two `constexpr` rects above could never equal the
  drawer's parent-relative rect while it is nested, so SC-020(c)'s byte-comparison could not pass.
  **Resolution (D-4):** the sub-controller attribute moves onto the **template root** and the drawer
  becomes a direct child of it (§9). C-1's table, FR-023 and these two `constexpr` rects then all speak
  the same absolute coordinate space, and no number in the spec changes.
- The seven tab pages are **child containers of this container**, exactly one visible at a time
  (`setVisible`), never separate `.uidesc` files and never a `UIViewSwitchContainer` (which realises only
  the active template and would make `unreachableParams` report six tabs' worth of IDs as unreachable —
  C-3 requires an **empty** allowlist, so every page must be present in the XML).
- **Opening it never removes, hides, unmounts or resizes the cloud view** (FR-024): the drawer simply
  grows upward over it. The cloud view's `getViewSize()` stays `(0, 32, 1000, 670)` in both states —
  SC-020(c) asserts this byte-equal.
- The slide animation, if any, is a short `CVSTGUITimer` **owned by this view and cancelled in
  `removed()`**. A non-animated instant toggle is acceptable and is the fallback if the animation
  complicates SC-020.
- Drawer contents are plain uidesc controls — `ArcKnob` / `CSlider` / `COptionMenu` / `CCheckBox`
  (FR-025's exact four, no more). No additional custom class, and **no `ToggleButton`**: FR-025 does not
  list it and §9's freeze cluster and every drawer toggle are `CCheckBox`.
- **The seven tab titles are `Cloud, Morph, Body, Atmos, Aether, FX, Life/Env`, in that order** (FR-022).
  They are the tab buttons' `title` attributes in the uidesc, in document order, and SC-004 arm 2 reads
  them back off the built tree and compares the ordered string list — a wrong label or a swapped pair is
  a red test, not a build that ships.
- Created through `createCustomView` (C-7a): `<view class="CViewContainer"
  custom-view-name="DrawerContainer" .../>`.

### 8.4 `SeraphisEditSubController` (`edit_sub_controller.{h,cpp}`) — **not a `CView`**

```cpp
class SeraphisEditSubController : public VSTGUI::DelegationController {
public:
    SeraphisEditSubController(Controller* owner, VSTGUI::IController* parent);
    void      valueChanged(VSTGUI::CControl* control) override;
    VSTGUI::CView* verifyView(VSTGUI::CView*, const VSTGUI::UIAttributes&,
                              const VSTGUI::IUIDescription*) override;
};
```

Bound from the uidesc by `sub-controller="SeraphisEdit"` on the **template root** (D-4, §9), so every
control in the document — including the header's preset button — is inside its sub-tree.
`Controller::createSubController` returns one and `++subControllerInstances_`
(`UI-COMPONENTS.md:433-449` is the pattern, `:370` the once-per-instantiation-in-document-order rule and
the reset-in-`willClose()` trap). VSTGUI honours `sub-controller` on the template node itself: the
template is created through the same `UIDescription::createViewFromNode` that reads the attribute
(`extern/vst3sdk/vstgui4/vstgui/uidescription/uidescription.cpp:672-677`, reached from
`UIDescription::createView` at `:778`).

**It owns `valueChanged` for every tag-less control** (C-7b, FR-045) — no tag-less control's
`valueChanged` may live on `Controller`, on a `CView` subclass, or nowhere:

| Control | Tag | Drives |
|---|---|---|
| Preset button (header, FR-007) | `kPresetButtonTag` | opens/closes a `Krate::Plugins::PresetBrowserView` over `controller_->presetManager_` (`controller.h:43`, `controller.cpp:57`) |
| 7 drawer tab buttons | `kTabBaseTag + i` (view tags, **not** `ParamID`s) | `drawer_->setActiveTab(i)` (session) |
| Drawer handle | `kDrawerHandleTag` | `drawer_->setOpen(!isOpen())` (session) |
| Obs/Edit toggle | `kModeToggleTag` | `cloudView_->setMode(...)` (session) |
| Blend A→B slider | `kBlendTag` | mouse-down → kind 7 (`BlendBegin`); moves → kind 4; mouse-up → flush |
| Tilt dB control | `kTiltTag` | kind 5, **absolute** dB/oct |
| 4 morph slot selector buttons | `kSlotBaseTag + i` | kind 6, and `controller_->setSelectedSlot(i)` |

**The preset button is in this table on purpose (D-5).** FR-007 requires it and FR-045 requires *every*
tag-less control to have `SeraphisEditSubController` as its `IControlListener`; spec C-7b's table omits
it, which left it with no listener owner under either document and no criterion anywhere. Putting the
sub-controller on the template root (D-4) is what makes the requirement satisfiable rather than
carving the button out of FR-045. SC-022(b) asserts its `getListener()` and SC-022(d) exercises the
browser.

**How a tag-less control acquires a tag and a listener — `verifyView`, not the uidesc.** VSTGUI's control
creator sets a listener **only** when a `control-tag` attribute is present
(`vstgui/uidescription/viewcreator/controlcreator.cpp:75-100`); a control with no `control-tag` keeps
tag `-1` and listener `nullptr`. So the wiring is:

- each tag-less control carries a **custom attribute** `session-tag="<name>"` in the uidesc (a
  non-standard attribute is preserved in `UIAttributes` and ignored by the view factory — Disrumpo's
  `menu-items` uses the same trick, `plugins/disrumpo/src/controller/sub_controllers.h:194-203`);
- `SeraphisEditSubController::verifyView` reads it, and for a recognised name does
  `control->setTag(kSessionTagFor(name)); control->setListener(this);` before delegating to
  `DelegationController::verifyView`;
- an **unrecognised** `session-tag` value is a hard failure, not a silent pass: `verifyView` asserts in
  Debug and SC-022(b) asserts every control in the table above reports the sub-controller from
  `getListener()`, so a typo'd attribute is a red test.

**`getTagForName` is NOT overridden.** Seraphis has no repeated template needing per-instance
`ParamID`s (unlike Disrumpo's per-band case, `UI-COMPONENTS.md:315`, `:376-377`), so the
`DelegationController` forwarding default is correct. Only `valueChanged` and `verifyView` are
overridden. Session tags are chosen **outside the registered ID space** (`9000+`) and are never written
as `control-tag`, so they can never collide with a `ParamID` and can never be picked up by
`extractControlTagMap` — which is what keeps SC-002's binding count at exactly 110.

**Teardown (C-7c, FR-041):** the controller zeroes `cloudView_`, `drawer_`, `macroRings_` and resets
`subControllerInstances_ = 0` in `willClose()`; each view cancels its own timer in `removed()`.

---

## 9. `resources/editor.uidesc` — replaced wholesale (FR-001, C-1, C-3)

**Fixed 1000 × 700** (RQ-3). No `setAllowedZoomFactors`, no `onSize` relayout.

The `<control-tags>` block (`editor.uidesc:21-139`) is carried over **verbatim, all 107 entries** —
no tag added, removed, renamed or re-numbered (FR-002). The Phase 8 placeholder template
(`:140-184`) and its banner (`:3-5`) are deleted.

**One sub-controller, on the template root (D-4).** Every view below is a **direct child of the 1000 × 700
root**, so `getViewSize()` on the cloud view and on the drawer is in the window's own coordinate space
and FR-023 / FR-024 / C-1's absolute rects are literally what the XML declares. Putting
`sub-controller="SeraphisEdit"` on the root rather than on an intermediate container is what buys that,
and it simultaneously puts the header's preset button inside the sub-controller's sub-tree, which FR-045
requires (D-5). Tagged controls are unaffected: `DelegationController` forwards
`getControlListener`/`valueChanged` to the parent controller for anything it does not claim
(`vstgui/uidescription/delegationcontroller.h:26`).

```
<template name="editor" class="CViewContainer" size="1000, 700" sub-controller="SeraphisEdit">

  <!-- 1. CLOUD VIEW FIRST (FR-006) so everything below draws over it.
          Direct child of root => getViewSize() == (0, 32, 1000, 670), FR-024. -->
  <view class="CView" custom-view-name="CloudView" origin="0, 32" size="1000, 638"/>

  <!-- 2. HEADER (0,0,1000,32) - 7 bound views (FR-007) -->
  <view class="CTextLabel"  title="SERAPHIS" .../>              <!-- no tag, not counted -->
  <view class="COnOffButton" session-tag="preset" ... />
        <!-- Preset button: NO control-tag (not counted in the 110); listener +
             tag assigned by SeraphisEditSubController::verifyView, §8.4 / D-5 -->
  <!-- freeze cluster, RQ-2: 3 SECOND bindings. CCheckBox, per FR-025's four classes -->
  <view class="CCheckBox" control-tag="AtmosFreeze"    .../>    <!-- 1008 -->
  <view class="CCheckBox" control-tag="AetherFreeze"   .../>    <!-- 1204 -->
  <view class="CCheckBox" control-tag="FxSpectralFreeze" .../>  <!-- 1430 -->
  <!-- 4 globals, header-exclusive -->
  <view class="CSlider"     control-tag="MasterGain" .../>      <!-- 0    R -->
  <view class="COptionMenu" control-tag="Polyphony"  .../>      <!-- 1    L -->
  <view class="CCheckBox"   control-tag="SoftLimit"  .../>      <!-- 2    T -->
  <view class="COptionMenu" control-tag="Seed"       .../>      <!-- 3    L -->

  <!-- 3. FIVE MACRO RINGS over the cloud view (C-1 anchors) -->
  <view class="MacroRingKnob" control-tag="MacroDream"    origin="24, 56"   size="96, 96"/>
  <view class="MacroRingKnob" control-tag="MacroBloom"    origin="880, 56"  size="96, 96"/>
  <view class="MacroRingKnob" control-tag="MacroGravity"  origin="24, 518"  size="96, 96"/>
  <view class="MacroRingKnob" control-tag="MacroDissolve" origin="880, 518" size="96, 96"/>
  <view class="MacroRingKnob" control-tag="MacroEntropy"  origin="452, 556" size="96, 96"/>

  <!-- 4. Edit mini-toolbar: tag-less, session-tagged, sub-controller listens -->
  <view class="COnOffButton" session-tag="mode"  ... />  <!-- Obs|Edit -->
  <view class="CSlider"      session-tag="blend" ... />  <!-- Blend A->B -->
  <view class="CSlider"      session-tag="tilt"  ... />  <!-- Tilt dB -->

  <!-- 5. DRAWER - a DIRECT CHILD OF ROOT (D-4). origin/size are the COLLAPSED
          rect exactly as FR-023 states it: (0, 670) .. (1000, 700). -->
  <view class="CViewContainer" custom-view-name="DrawerContainer" origin="0, 670" size="1000, 30">
      <view class="COnOffButton" session-tag="drawerHandle" ... />
      <!-- 7 tab buttons, session-tag="tab0".."tab6", titles in FR-022's exact order:
           Cloud, Morph, Body, Atmos, Aether, FX, Life/Env -->
      <!-- 7 page containers, one visible; every deep parameter's PRIMARY binding
           lives here. All present in the XML - never a UIViewSwitchContainer (R-10). -->
  </view>
</template>
```

**The binding budget, and it is exact (C-3, FR-003, SC-002):**

| Surface | IDs | Bindings |
|---|---|---|
| Header globals | 0, 1, 2, 3 | 4 |
| Header freeze cluster | 1008, 1204, 1430 (second bindings) | 3 |
| Macro rings | 100–104 | 5 |
| Cloud tab | 200–210 | 11 |
| Morph tab | 400–412 (incl. the four slot selectors, RQ-1) | 13 |
| Body tab | 800–812 | 13 |
| Atmos tab | 1000–1016 | 17 |
| Aether tab | 1200–1217 | 18 |
| FX tab | 1400–1443 | 16 |
| Life/Env tab | 600–604, 700–704 | 10 |
| **Total** | | **110** |

107 primary + 3 duplicates. `{1008, 1204, 1430}` is the **complete, enumerated** duplicate allowlist.
`unreachableParams(xml, ids, {})` — **empty allowlist** — must return empty.

**Per-view class rule (FR-004, SC-003).** `parameter_surface_test.cpp:731` asserts each bound view's
class matches its parameter's registered kind; `expectedViewClass` (`:488-495`) currently maps
`R → "CSlider"`, `L → "COptionMenu"`, `T → "CCheckBox"`. Phase 11 widens it to a **set** per kind:

```
R -> { "CSlider", "ArcKnob" }                 + the enumerated exception { "MacroRingKnob" } for 100..104
L -> { "COptionMenu" }
T -> { "CCheckBox" }                          # NOT widened - FR-025's four classes, decided in §8.3
```

`T` stays a **singleton set**: the freeze cluster and every drawer toggle are `CCheckBox`, `ToggleButton`
appears nowhere in the shipped uidesc, and widening `T` to `{ "CCheckBox", "ToggleButton" }` would have
made SC-003 unable to detect either choice.

The `MacroRingKnob` exception is **enumerated by ID in the test**, not waived by loosening the rule.
No registered parameter's type, range, default or `stepCount` changes (FR-004); `plugin_ids.h:184-240`'s
frozen legend is untouched.

---

## 10. Test plan

Each row gives the file, the `TEST_CASE` name and the assertion strategy. New DSP tests are added to
`dsp/tests/CMakeLists.txt`'s **enumerated** lists (`unit/processors/spectral_state_test.cpp` at `:282`
for `dsp_processors_tests`, `unit/systems/` at `:340` for `dsp_systems_tests`); new plugin tests to
`plugins/seraphis/tests/CMakeLists.txt`'s enumerated list (`:16-32`) — neither is globbed.

### 10.1 DSP-layer criteria

| SC / FR | File | `TEST_CASE` | Strategy |
|---|---|---|---|
| SC-012 cl. 1–3, FR-032 | `dsp/tests/unit/processors/spectral_state_test.cpp` (extend) | `SpectralState_AuthoringMutators_PreserveValidity` | A **≥ 40-row** table. Per row: copy `before`; call the mutator; if `isValidSpectralState(before)` → `CHECK(isValidSpectralState(after))`; else → `CHECK(std::memcmp(&before, &after, sizeof(SpectralState)) == 0)` for `setPartial`/`tiltState`. `blendStates`' return is checked valid **unconditionally**. Coverage: ratios `< kMinStateRatio` / `> kMaxStateRatio`; equal, descending, and closer-than-`kAuthorSpacing²` neighbours; amplitudes `<0`, `>1`, exactly 0, exactly 1; `numPartials ∈ {-1, 0, 1, 64, 65, INT_MAX}`; indices `{0, numPartials-1, numPartials, 63, 64, SIZE_MAX}`; `t ∈ {-1, 0, 0.5, 1, 2}`; `dB` at both clamp edges and beyond. **Non-finite arguments built from bit patterns** (`std::memcpy` of `0x7FC00000` / `0x7F800000` through a `volatile` sink) — never `std::numeric_limits<float>::quiet_NaN()`, per `spectral_state.h:21-23`. |
| SC-012 acceptance arm | `dsp/tests/unit/systems/spectral_state_authoring_test.cpp` (**new**, `dsp_systems_tests`) — the **same** TU that hosts every SC-013 arm | `SpectralState_AuthoredStates_AreAcceptedByMorphEngine` | For every row whose post-call state is **valid**: park the journey on slot 0 (`setStateCount(2)`, `setTargetPosition(0)`, `updateChunk` until `getTravelPosition() == 0`), snapshot `getOutputRatios()`/`getOutputAmplitudes()` (`spectral_morph_engine.h:423-425`), call `setState(0, s)`, `updateChunk`, compare. Acceptance ⇔ the output arrays moved to the new state. **`getStateCount()` MUST NOT be used** — it returns `numStates_` (`:443`), written only by `setStateCount` (`:318-328`) and never by `setState` (`:292-314`). **`isStateFadeActive()` MUST NOT be used alone** — `setState` returns early without arming on an identical state (`:302-305`) and arms only `if (slotContributes(slot))` (`:311`). Invalid-output rows are excluded; rejection is correct for them. |
| SC-013(a) | `dsp/tests/unit/systems/spectral_state_authoring_test.cpp` (**the same new TU**, `dsp_systems_tests`) | `SpectralState_AuthoringMutators_AreAudible` | **One named host TU, registered in §13's CMake edit.** Not "the SC-012 file or a render TU beside `spectral_morph_render_test.cpp`": `unit/processors/spectral_state_test.cpp` is registered to `dsp_processors_tests` (`dsp/tests/CMakeLists.txt:282`) while every SC-013 arm needs a **Layer-3 render**, and per R-9 a TU absent from the enumerated list drops silently. Precondition for **every** FFT arm, stated in reachable terms: `HarmonicCloud::setDriftDepthCents(0.0f)` (`harmonic_cloud.h:501`) on the cloud under test, so `BrownianDrift` contributes no detune (`frequencyHz` is drift-inclusive by C-2 cl. 3). **Plugin `ParamID`s — `kCloudDriftDepthId` (205), `kMacroEntropyId` (104) — do not exist in a `dsp/` TU** and must not appear in this file. Load `makeFactoryState(SineStack)` into a slot, park the journey, `setPartial(s, 0, 1.5f, s.amplitudes[0])`, render steady state, 4096-point FFT: partial 0's peak moves **701.955 ± 5 cents**, no other partial's peak moves > **2 cents**. **Index pinned at 0** — C-6's window caps at `(k+2)/1.0163049`, so every `k ≥ 1` clamps and cannot move a fifth. |
| SC-013(b) | same | same | `blendStates(a,b,0)` / `(a,b,1)` render within `render_fingerprint.h` tolerance (`kSampleTolerance = 1e-4f`, `kMetricTolerance = 1e-5`, `:47-51`) of `a` / `b` — the **audio** comparison the helper is calibrated for. `t = 0.5` lands strictly between them on spectral centroid. |
| SC-013(c) | same | same | For `dB ∈ {-12,-6,0,+6,+12}` applied **to a fresh copy of the same source state each time**, rendered spectral centroid is strictly monotonically increasing in `dB`. Plus the absoluteness arm: `tiltState(s,-6)` **twice** leaves the render and `s.tiltDbPerOct` **equal** to a single call. |
| SC-013(d) | same | same | `setPartial(s,0,ratios[0],0.25f)` then `(…,1.0f)`: partial 0's rendered peak magnitude moves monotonically and by **≥ 6 dB**; its rendered frequency moves **< 2 cents**. This is what makes `EditMessage::b` and FR-028's modifier-drag live. |
| **FR-033** (the fan-outs) | `dsp/tests/unit/systems/seraphis_partial_fanout_test.cpp` (**new**, `dsp_systems_tests`) | `SeraphisEngine_PartialFanOut_ReachesEveryVoice` | A **dedicated TU** by phase-owner ruling (§2, tasks.md T003) rather than folded into an existing file. Prepare at 48 kHz; `setPartialPositionAllVoices(7, 0.8f)`; assert `getVoice(v).cloud().getPartialPosition(7) == 0.8f ± 1e-6` for **every** `v` in `[0, kMaxVoices)` (16, `seraphis_engine.h:211`) — all sixteen slots, not just allocated ones, because a slot handed out later must already carry the override. **Mask arm, in the API's real polarity** (§2.1's convention box; `setPartialMask`'s body is `masked_[index] = !active`, `harmonic_cloud.h:1082-1089`): `setPartialMaskAllVoices(3, /*active=*/false)` + a held-note block ⇒ partial 3's amplitude decays toward 0 on every voice while partial 4's does not; then `setPartialMaskAllVoices(3, /*active=*/true)` ⇒ partial 3 **recovers** on every voice (this is the polarity the FR-030 re-push depends on); then `clearPartialMaskAllVoices()` (`masked_.fill(false)`, `:1101`) ⇒ everything audible on every slot. **The previous draft's `setPartialMaskAllVoices(3, true)` → "decays toward 0" asserted the opposite of what the code does** and contradicted §6.3 in the same document; an implementer would have written a red test or "fixed" it by flipping the fan-out, which would make `repushPartialOverrides()` un-mask every user-masked partial on every clearing event. Out-of-range index (`64`, `SIZE_MAX`) and **bit-pattern** non-finite position are silently ignored and leave every voice byte-unchanged — the owners already reject them (`harmonic_cloud.h:1070-1075`, `:1085-1087`), so the fan-outs add **no second guard**. |
| **SC-030**, FR-033a (regression) | existing Phase 3 suites in `dsp_systems_tests` (`spectral_morph_*`) | — (re-run, **no new test file**) | The §2.3 relaxation touches `SeraphisVoice`'s two call sites and one comment; `SpectralMorphEngine::setState`'s body (`:292-315`) is not edited. Every existing Phase 3 case exercising `setState`, `setStateCount`, the FR-047 absorption fade and FR-044's continuity bound must pass **unmodified**. Any edit to a Phase 3 test to make it pass is a failure of SC-030, not a fix. Run **before and after** §2.3 and diff the Catch2 summary lines. |
| SC-021(d), FR-037 | `dsp/tests/unit/systems/seraphis_macro_test.cpp` (extend) | `SeraphisMacroMatrix_EffectsOwner_IsAdditive` | `static_assert`s survive; `computeAetherTargets()` at every macro setting is **bit-identical** to a pre-change reference table (proves the enum append moved no offset); `computeEffectsTargets()` returns `{0,0}` at the neutrals; `static_cast<std::size_t>(SeraphisMacroTarget::Count)` equals the pinned literal. |

### 10.2 Plugin-layer criteria

| SC / FR | File | `TEST_CASE` | Strategy |
|---|---|---|---|
| SC-001, FR-042 | `tests/integration/ui_negative_control_test.cpp` (**new**) | `Seraphis_Phase11_OpenGate_ChangesNoSample` | **One build, one process, one `Processor` instance.** Arm A: `setCloudFrameGateForTest(true)`; arm B: `false`. Identical 10 s MIDI script at the 8-voice operating point, all defaults. Assert `cloudFramePublishAttemptCountForTest() > 0` in A and `== 0` in B **before** comparing, then `max|a[i]-b[i]| == 0.0f` over both channels. Exact equality is legitimate because both arms are the *same compiled code path on the same instance*; a cross-build bit-exact comparison is **forbidden** (roadmap line 598). |
| SC-002, FR-003 | `tests/unit/parameter_surface_test.cpp` (extend) | `Seraphis_ParameterSurface_IsComplete` | `getParameterCount() == 107` (`:508`); tag/ID equality both ways (`:700-709`); `unreachableParams(xml, ids, {})` **empty with an empty allowlist**; `extractBoundViews(xml).size() == 110u` (`:714`, was `== 8u`); the multiset of bound IDs contains `{1008,1204,1430}` **exactly twice each** and every other registered ID **exactly once**, asserted against an enumerated allowlist. |
| SC-003, FR-004 | same | same | Per-view class check at `:731` widened to the §9 sets, with `MacroRingKnob` enumerated **by ID** for 100–104 and no other exception. |
| SC-004, FR-020 | `tests/unit/controller/custom_view_test.cpp` (**new**) | `Seraphis_Phase11_CustomViews_AreInstantiated` | **Arm 1.** After `exerciseEditorLifecycle`, walk the frame and count by **`dynamic_cast`**, not by view count: exactly one `CloudView`, exactly one `DrawerContainer`, exactly **five** `MacroRingKnob`. This is the criterion that catches the Phase 8 banner's hazard (`editor.uidesc:3-5`) — a creator TU that failed to link silently yields stock views. |
| SC-004 arm 2, **FR-022** | same | same | **The seven tab titles, named and ordered.** Walk the `DrawerContainer`'s tab buttons in child order and assert the title list is **exactly** `{"Cloud", "Morph", "Body", "Atmos", "Aether", "FX", "Life/Env"}` — string-equal, same order, size 7. FR-022 fixes both the names and the order (spec `:934-935`) and nothing else asserts either; without this arm a build shipping seven tabs in the wrong order, or with a mislabelled tab, passes every other criterion. `DrawerContainer::kTabCount == 7` is a `static_assert`, not a substitute for this. |
| SC-004 arm 3, **FR-006 / FR-025 / FR-027** | same | same | **Three one-line assertions on the already-built tree, for three FRs that had plan design and no criterion at all.** (i) **FR-006** — *the cloud view is the FIRST child so the rings draw over it* — is today only a uidesc comment (§9, §8.1); SC-004 arm 1 counts instances by `dynamic_cast` and SC-020 checks rects, and **neither sees child order**, so a build that put the cloud view last (hiding every ring behind it) passes everything. Assert `dynamic_cast<UI::CloudView*>(root->getView(0)) != nullptr` — child **index 0** of the template root. (ii) **FR-025**'s *"exactly one tab's page is visible at a time"* is stated in §8.3 with no assertion: walk the `DrawerContainer`'s seven page containers and assert `std::count_if(..., isVisible) == 1`, then `setActiveTab(i)` for each `i` and re-assert it is still exactly one **and** that it is page `i`. (iii) **FR-027**'s *"Observe is the default on every editor open"* has no assertion: immediately after `didOpen`, `cloudView_->mode() == CloudView::Mode::Observe` — asserted on **every** cycle of the lifecycle loop, not only the first, since the failure mode is a mode that survives a close. |
| SC-005, FR-041 | `tests/unit/controller/editor_lifecycle_test.cpp` (extend `:235-262`) | existing case | `exerciseEditorLifecycle(controller, "editor", …, /*cycles=*/10)`; `getParameterCount() == 107` before and after; **zero reports** under `-DENABLE_ASAN=ON` Debug and the valgrind-nightly `[lifecycle]` lane. |
| SC-006, FR-013 | `tests/integration/cloud_frame_test.cpp` (**new**) | `Seraphis_CloudFrame_MirrorsCloudAccessors` | **The frame is read at `lastPublishedFrameForTest()` (§5.3), between `process()` calls — never through the DataExchange queue**, which a headless run may never fill. Render a note; compare each `frequencyHz[i]` to `getPartialFrequencyHz(i)*getPartialDriftDetune(i)` and each `amplitude[i]` to `getPartialCurrentAmplitude(i)*getPartialAntiAliasGain(i)` within **1e-6 relative** (DSP side via `engineForTest()`, `processor.h:303`); `partialCount == getActivePartialCount()`; entries at `i >= partialCount` exactly `0.0f`. **(e)** after masking `{3,17}` and pan-overriding `9`: `maskBits` has exactly bits 3, 17; `overriddenBits` exactly 3, 9, 17. **(f)** sweep `kMorphPositionId` over `{0,.25,.5,.75,1}` and check `morphTravelPosition` tracks `getTravelPosition()` (`:434`) within 1e-6 relative. |
| SC-006 arm (g), **FR-014** | same file | `Seraphis_CloudFrame_FocusVoiceFollowsAllocationSerial` | **The three-step focus rule, all three clauses.** Driven from `engineForTest()` and read at `lastPublishedFrameForTest().focusVoice`. **(1)** Play three overlapping notes; after each note-on, `focusVoice` equals the slot with the greatest `getVoiceAllocationSerial` among non-idle slots (`seraphis_engine.h:975`; ties impossible, `:966-974`). **(2)** Release the newest note only: while `getVoiceLevel(prev) > kCloudFrameSilenceLevel` the focus slot is **retained** — clause (b), the release-retention arm; assert it holds for at least one published frame after note-off and that it is the *released* slot, not the next-highest serial. **(3)** Let all voices fall silent: `focusVoice == 0` — clause (c). **(4)** `kPolyphonyId = 1` (the *Edge cases* degeneration): every arm above still terminates and `focusVoice` is always 0. No SC named FR-014 before; a focus rule that always returned slot 0 passed everything. |
| SC-006 arm (h), **FR-016** | `tests/unit/controller/custom_view_test.cpp` | `Seraphis_Controller_CachesOnlyTheMostRecentBlock` | **The controller-side receiver, which had no arm at all** (SC-006's other arms are all producer-side; SC-020 bypasses the receiver by writing the frame cache directly). Call `Controller::onDataExchangeBlocksReceived` **directly** with one delivery of **three** blocks whose `sequence` values increase, and assert `cachedCloudFrame().sequence` equals the **last** one — the "most recent wins" rule (`plugins/membrum/src/controller/controller.cpp:1719-1726`). Then call `queueOpened` with a `TBool` seeded to `true` and assert it comes back **`false`** (`dispatchOnBackgroundThread = false`, FR-016). Both are pure function calls; no processor, no host. |
| SC-006 arm (i), **FR-011** | `tests/integration/cloud_frame_test.cpp` | `Seraphis_DataExchangeHandler_FollowsTheConnectionAndActivation` | **FR-011 had no criterion anywhere.** It requires the handler to be created in `connect()`, destroyed in `disconnect()`, and `onActivate`/`onDeactivate` driven from `setActive` (§5.2) — and the spec's Traceability table maps the whole DataExchange line to FR-010–FR-016 collectively with only SC-006/SC-007, both of which are **producer-content** criteria that (after §5.3's reordering) pass with the handler permanently null. A build that never called `onDeactivate()` on `setActive(false)`, or that leaked the handler across `disconnect()`, passed every criterion in this phase. **Arm:** this is the one case that builds a peer `IConnectionPoint` (a minimal test double, local to this TU — **not** in `ProcessorFixture`, §5.3). `connect(peer)` ⇒ a new seam `dataExchangeHandlerLiveForTest()` returns true; `setActive(true)` then `setActive(false)` then `setActive(true)` leaves it true and does not double-open (the SDK's `openQueue` is idempotent per activation, `dataexchange.cpp:76-105`); `disconnect(peer)` ⇒ the seam returns **false** and `cloudFrameSkippedBlockCountForTest()` resumes rising on every gated publish, proving the transport was released and not merely idled. |
| SC-007, FR-012 | same file | `Seraphis_CloudFrame_PublishesOncePerProcessCall` | 60 s render with MIDI on non-block boundaries and automation forcing the 64-sample subdivision, gate open: `cloudFramePublishAttemptCountForTest()` equals **the number of `process()` calls that reached the slice loop**, exactly; `renderSliceCountForTest() > cloudFramePublishAttemptCountForTest()` **strictly**. **The divisor is NOT the host call count, and the difference is not pedantry:** `Processor::process()` has six pre-slice-loop early returns (`processor.cpp:978`, `:981`, `:988`, `:992`, `:997`, `:1002-1008` — no outputs, null `channelBuffers32`, `numChannels < 2`, `numSamples <= 0`, null L/R, not prepared; all read this session) and `publishCloudFrame()` sits **after** the loop, so on any of those calls no attempt is recorded. A controlled render never hits them; **pluginval at strictness 5 and real hosts do**, so an equality stated against the host call count is simply false as an invariant. Use `effectsStageProcessCalls_` (incremented at `:1189`, which already has exactly the "reached the slice loop" meaning) as the divisor rather than adding a seventh counter. `cloudFrameSkippedBlockCountForTest()` is **reported via `INFO`** and asserted about not at all — it counts both an exhausted queue (`numBlocks = 4` drained at 30 Hz against ≈94 Hz fill exhausts by design) and the headless no-handler case (§5.3), so in the fixture it equals the attempt count. **Spec edit required (D-9, §11.2):** SC-007's and R-1's wording. |
| SC-008 | same file | `Seraphis_CloudFrame_IsDeterministic` | Two runs of the same seeded script **in the same process on the same build**. "The whole sequence" is accumulated by reading `lastPublishedFrameForTest()` **once after every `process()` call** (§5.3) — the producer's own frame, not the queue's, so a headless run with zero landed blocks still has a full sequence to aggregate; `cloudFrameSequenceForTest()` supplies the strictly-increasing check. Four aggregates over that sequence, each compared **relatively** at **1e-5**: amplitude-weighted mean pitch `Σaᵢlog2(fᵢ)/Σaᵢ`; its total variation; mean amplitude `Σaᵢ/partialCount`; mean position. Plus `sequence` strictly increasing and `partialCount` equal frame-for-frame. **`render_fingerprint.h`'s constants are deliberately not used** — `kSampleTolerance = 1e-4f` is an *absolute* per-sample bound on **audio** calibrated against a peak of 2.17 (`:25-29`, `:47`); on absolute Hz it would be 2.5e-8 relative on a 4 kHz partial, below float epsilon. If a pilot run measures a spread above 1e-5 on any metric, **the measured number is recorded in the spec and the criterion re-stated with it** — never relaxed to fit a failing run. Same-build determinism only; no cross-toolchain claim. |
| SC-009 | `tests/integration/param_perf_test.cpp` (extend) + `cloud_frame_test.cpp` | `Seraphis_CloudFrame_CpuBudget`, `Seraphis_FullPoly_CpuBudget_WithFullSurface` (re-run, gate open) | Protocol: fresh boot, idle machine, **seven** consecutive runs, best-of-16 per estimate, **worst reported** (`param_perf_test.cpp:133-156`). Both `[.perf]`. (a) gate open at 8 voices: worst-of-seven `≤ kFullPolyCeilingNs = 2 666 666.7` ns (`:392`) against Phase 10's pinned 2 380 980 ns (22.32 %) — the producer must fit in **2.68 points**. (b) snapshot stage alone: `cloudFrameStageNsForTest() / cloudFrameStageProcessCallsForTest()` (§5.3) with `setCloudFrameInstrumentedForTest(true)`, `≤ 10 666 ns` per 512-sample block (0.10 % of one core). **The timer scope must open OUTSIDE the `cloudFrameEnabled_` predicate and the divisor must count every `process()` call**, not every publish — that is what makes SC-010(b)'s "a timer inside the gate reads zero by construction" reasoning hold, and it is why SC-010(b) measures whole-`process()` instead. Modelled on `effectsStageNsForTest()` / `effectsStageProcessCallsForTest()` / `setEffectsStageInstrumentedForTest()` (`processor.h:390`, `:398`, `:474`). **The ceiling is not a lever**: if (a) fails, the producer is made cheaper. |
| SC-010 | same | `Seraphis_CloudFrame_CostsNothingWhenClosed` | Gate false, 60 s render: (a) `cloudFramePublishAttemptCountForTest() == 0`; (b) **whole-`process()`** best-of-16 ns/block at 8 voices `≤ 1.15 × 2 318 840` ns (`:395`, `:472`). Arm (b) is deliberately the whole-`process()` number: a snapshot-stage timer *inside* the gate reads zero by construction and would measure the instrumentation, not the plugin. `[.perf]`. |
| SC-011, FR-040 | `tests/integration/effects_perf_test.cpp` (extend) or a new `ui_perf_test.cpp` | `Seraphis_CloudFrame_AllocatesNothing` | Re-point Phase 10's instrument (`effects_perf_test.cpp:683-759`, `:872-879`) with **both anti-vacuity guards carried over**. Corpus exactly `{ src/processor/processor.cpp, src/processor/processor.h, src/processor/cloud_frame.h }`. Assert `scan.filesMissing == 0`, `scan.codeBytes > 0`, and a **witness** count `> 0` for the token `publishCloudFrame` (the role `runSendStage` plays for Phase 10, `:692-695`). Then 60 s / 5 625 blocks with the gate open inside `TestHelpers::AllocationScope`: `allocations == 0`, `exceptions == 0` through a real `try/catch(...)`, zero lock primitives, zero throw sites. `spectral_state.h` is **not** scanned — the mutators are message-thread-only. **Plus the lock-free arm:** `CHECK(processor.phase11AtomicsAreLockFreeForTest())` (§5.3 — it ANDs `is_lock_free()` over `cloudFrameEnabled_`, `partialOverridesPending_`, both bitmasks and `partialPanStaging_[0]`) — the constitution's rule is that only `std::atomic_flag` is *guaranteed* lock-free, so §6.3's "lock-free on x86-64/arm64" claim is asserted at runtime rather than assumed. A locking atomic on the audio thread is an RT violation and this is the arm that would find it. |
| SC-011 arm 2, **FR-005** | same TU as SC-011 | `Seraphis_Phase11_UsesNoPlatformApi` | FR-005 (*no Win32 / Cocoa / native popup in any Phase 11 source file*) is mapped only to SC-019 — builds + portability + clang-tidy — and **a platform-guarded native popup compiles clean on all three legs**, so nothing detects it. Reuse SC-011's own source-scan instrument (`effects_perf_test.cpp:683-759`) with corpus `src/ui/*.{h,cpp}` **plus** `src/processor/processor.{h,cpp}` and `src/controller/controller.{h,cpp}`, and a **forbidden-token** list: `windows.h`, `HWND`, `CreateWindow`, `MessageBox`, `NSView`, `NSWindow`, `NSAlert`, `#import`, `gtk_`, `XCreateWindow`. Carry **both anti-vacuity guards**: `scan.filesMissing == 0`, `scan.codeBytes > 0`, and a **witness** count `> 0` for a token that must be present (`VSTGUI::`) — a scan that found no files must be red, not green. Zero hits required. |
| SC-014, FR-030, FR-043 | `tests/integration/partial_edit_test.cpp` (**new**) | `Seraphis_PartialOverrides_SurviveClearingEvents` | After a kind-2 pan edit on partial `k` (message → `notify` → deferred fan-out, §6.2), each event below leaves `lastPublishedFrameForTest().position[k]` at `0.8 ± 0.01` on the next published frame — read at the **producer seam** (§5.3), not through the queue: (1) a `kCloudStereoSpreadId` (207) change; (2) a `kSeedId` (3) change; (3) an engine `reset()`; (4) `kPolyphonyId` 1 → 8, asserting a **newly allocated** voice reports 0.8 (not the default pan); (5) `setupProcessing` at a different sample rate — the only criterion for FR-043. The mask edit is asserted across events **3, 4, 5** only (mask survives spread and seed by construction: `setStereoSpread` and `setSeed` clear only `positionOverridden_`, `harmonic_cloud.h:535-547`, `:703`; `reset()` clears both, `:331-332`). |
| SC-014 arm 6, **the macro clearing path** | same file | `Seraphis_PartialOverrides_SurviveAMacroRingSweep` | **Sweep `kMacroBloomId` over `{0, .25, .5, .75, 1}`** with the deep 207 knob **held still**, and assert `position[k]` is still `0.8 ± 0.01` at every point. This is the defect §6.3 exists to catch: Bloom writes `CloudStereoSpread` with `.base = 0.35f, .amount = 0.60f` (`seraphis_macro_matrix.h:252-257`) through `macros_.apply()` every slice (`processor.cpp:1858` → `:635`), `setStereoSpread` wipes `positionOverridden_` on any **value** change (`harmonic_cloud.h:535-547`), and a tracker keyed on `ParamID` 207 cannot see it. A build that keyed on 207 passes arms 1–5 and fails only here. |
| SC-014 arm 7, **the worst-case re-push cost** | `tests/integration/ui_perf_test.cpp`, `[.perf]` | `Seraphis_PartialOverrides_RepushWorstCase` | Author **64** pan overrides (all bits set), then sweep `kMacroBloomId` across its full range so the composed spread changes on consecutive slices and `repushPartialOverrides()` fires repeatedly. Worst-of-seven whole-`process()` ns/block at 8 voices must still satisfy `kFullPolyCeilingNs = 2 666 666.7` (`param_perf_test.cpp:392`). This measures the 64 × 16 × 2 = **2048 transcendental** worst case §2.2 derives (`updatePanGains` → `equalPowerGains` is `cos`/`sin`, `crossfade_utils.h:50-53` — **two trig calls, not two `sqrt`**) instead of assuming it is bounded. If it fails, the fan-out gets cheaper (dirty-index set, or coalescing the re-push to once per `process()` call); the ceiling does not move. |
| SC-015, FR-034/034a | `tests/unit/state_v3_test.cpp` (extend) | `Seraphis_EditedState_RoundTripsAtV3` | Edit slot 1's ratios, pan-override partial 5 to 0.8, mask partial 9. `getState` → `setState` into a fresh processor → `getState`: the two streams are **byte-identical** (Phase 9 FR-094); first four bytes decode to **3**; slot 1's 541-byte payload deserializes to the edited state; the appended 272-byte `[partials]` block deserializes to the same pan/mask/override values. Then a stream **truncated immediately before** the block still loads, with every override absent — proving the EOF-safe strict-prefix chain, not a version branch. **No FR-094 carve-out is taken.** |
| SC-016, FR-035 | same | `Seraphis_SlotDropdown_DiscardsOnlyThatSlot` | After editing slots 0 and 1, moving ID 409 restores slot 0 to `makeFactoryState()`'s result **byte-compared** and leaves slot 1's payload byte-identical. The controller's `slotMirror_[0]` is byte-identical to `makeFactoryState()` after the same move. |
| SC-016 arm 2, **FR-046 (the state-stream re-seed)** | same | `Seraphis_SlotMirror_ReSeedsFromTheStateStream` | FR-046 names **two** re-seed sources — the 409–412 dropdown *and* "the four 541-byte payloads a loaded state stream carries" (spec `:1062-1068`). Only the first had a criterion, and the second is the sole justification for the signature widening to `loadMorphParamsToController` (§7.3 item 2, the phase's only change to `morph_params.h`). **Arm:** build a stream carrying four **edited** slot payloads, call `Controller::setComponentState` on it, and assert each `slotMirror_[i]` is **byte-identical** (`std::memcmp` over `sizeof(SpectralState)`) to the corresponding `deserializeSpectralState` result. **Arm 2b:** corrupt one payload's version byte; that mirror entry must be **byte-unchanged** from its pre-load value (the deliberately ignored return value, §7.3, `spectral_state.h:264-265`, `:300-305`) while the other three still load and the following 55 parameters still read from the right offset. Failure mode without this arm is silent and user-visible: after a project reload the Edit-mode y-axis is drawn from default-constructed states while the processor holds the loaded ones. |
| SC-017, FR-021 | `tests/integration/cloud_frame_test.cpp` | `Seraphis_MacroRing_PerturbsConstellation` | Headless, on the producer, reading `lastPublishedFrameForTest()` after each `process()` call (§5.3) — "published `CloudFrame`s" means the producer's frame, not a queue delivery. Metric **`P = Σᵢaᵢ·log2(fᵢ/f₀)/Σᵢaᵢ`** over `i < partialCount`, `f₀ = frame.fundamentalHz` — dimensionless, octaves above the fundamental. **(a)** `kMacroBloomId` over `{0,.25,.5,.75,1}`: `P` strictly monotonically increasing, `P(1)−P(0) ≥ 0.35 oct` (**placeholder — §10.4 pilot replaces it with the measured value rounded DOWN to two decimals**). **(b)** negative control: same script, Bloom held at neutral at all five points, every other macro at neutral, and `kMasterGainId` swept over the same five points instead; the control arm's `|ΔP| ≤ 0.1 × ΔP(swept)` **and** below the drift-only spread over the same script. **No suppression seam is invented** — `SeraphisMacroMatrix`'s entire mutator surface is `setMacro` (`:554`), `setMacros` (`:599`), `setTargetBase` (`:708`); building one would be a `dsp/` addition outside the enumerated set. "No movement" is **not** asserted and could not be: per-partial drift runs unconditionally and `frequencyHz` is drift-inclusive. |
| SC-018, FR-036 | `tests/integration/partial_edit_test.cpp` | `Seraphis_EditMessage_RejectsGarbage` | Fuzz `Processor::notify` with 10 000 messages of random `kind`/`slot`/`index` and **bit-pattern** non-finite `a`/`b`: every slot still satisfies `isValidSpectralState`; `spectralSlotsHandoff_` is `-1` or in `[0,3)`; a subsequent render is finite everywhere (bit-pattern check, `-fno-fast-math` on this TU). |
| SC-019, FR-044 | CI + local | — | `Seraphis.vst3` builds on all three legs; `seraphis_tests` green; `tools/pluginval.exe --strictness-level 5` clean; `node tools/check-portability.js` clean; `node tools/lint-layers.js` clean; `./tools/run-clang-tidy.ps1 -Target seraphis` clean; `auval -v aumu Srph KrAt` on macOS. |
| SC-020, FR-018/024 | `tests/unit/controller/custom_view_test.cpp` | `Seraphis_Drawer_DoesNotStopCloudView` | Headless. With the drawer **open**, feed `N = 60` synthetic `CloudFrame`s with **strictly increasing `sequence`** through the controller's frame cache, calling `cloudView_->onTimerForTest()` once per frame. (a) `invalidCountForTest() == N` exactly; (b) 30 further timer calls with no `sequence` change add **zero**; (c) `cloudView_->getViewSize()` **byte-equal** to `(0, 32, 1000, 670)`; (d) the same three with the drawer collapsed; (e) `drawer_->getViewSize()` **byte-equal** to `DrawerContainer::kCollapsedRect` `(0, 670, 1000, 700)` collapsed and `kOpenRect` `(0, 420, 1000, 700)` open — which is only meaningful because the drawer is a direct child of the 1000 × 700 root (D-4). Measures the **frame → redraw** path, not the bare timer — a headless controller with no processor receives no frames, so any observed *rate* would be the rate the test chose. |
| SC-020 arm (f), **FR-017 axis map** | same | `Seraphis_CloudView_AxisMapIsMonotoneAndClamped` | FR-017's *"monotone in each axis and MUST clamp rather than wrap at the span edges"* (spec `:911-913`) had no criterion — SC-020 counted `invalid()` calls, SC-023 asserted zero points. **Arm:** call `yFromHzForTest` over a swept `hz` list that includes `{1, 19.99, 20, 100, 1000, 20000, 20000.01, 44100}` and `xFromPositionForTest` over `{-2, -1, -0.5, 0, 0.5, 1, 2}` and assert (i) strict monotonicity across the in-span interior, (ii) the sub-20 Hz values all map to the **same** `y` as 20 Hz and the super-20 kHz values all map to the same `y` as 20 kHz — clamped, and demonstrably **not wrapped** (a wrap would put `44100` near the 20 Hz end), (iii) `y` is inverted (higher Hz ⇒ smaller `y`), (iv) the same for `x` at `±1`. |
| SC-020 arm (g), **FR-017 masked exception (Q5)** | same | `Seraphis_CloudView_MaskedPartialStaysAClickTarget` | The Q5 clause — *"a partial whose `maskBits` bit is set MUST be drawn as a hollow ring at a fixed minimum radius … so a masked partial … stays a valid click target"* (spec `:913-916`) — is the entire answer to how a user **unmasks**, and had no criterion. A build that culled masked partials at zero radius passed every other test and made unmasking impossible. **Arm:** feed a synthetic frame with `maskBits` bit *i* set **and `amplitude[i] == 0`**, call `onTimerForTest()` then **`renderForTest()`** (§8.1 — **not** a bare `draw()`: no `CDrawContext` exists in the harness, `editor_lifecycle_harness.h:12-13`, so `draw(nullptr)` is not a defined call), then assert (i) `pointsDrawnForTest()` counts partial *i* — it is **not** culled; (ii) its `DrawnPoint` has `hollow == true` and `radius == kMaskedRingRadius` (> 0), not `kMinRadius`; (iii) `hitTestForTest()` at that point returns *i*. Then assert the complementary case: an **unmasked** partial with `amplitude == 0` draws at `kMinRadius`. |
| SC-021(a)(b)(c), FR-037–039 | `tests/integration/effects_chain_test.cpp` (extend) | `Seraphis_MacroDissolve_ReachesEffects` | **(a)** `kFxDelayMixId` at its shipped 0; sweep `kMacroDissolveId` over `{0,.25,.5,.75,1}` (allowing **one block of settle** per point, §4.2): the isolated send return — Phase 10 SC-003's definition, read as the **mean of the per-channel RMS over `preOutputTapLForTest()` and `preOutputTapRForTest()`** (`processor.h:431`, `:434`) with `preOutputTapTruncatedForTest() == false` (`:444`) — grows **strictly monotonically** in RMS from exactly `0.0` at neutral. Same for `kMacroEntropyId` against the wander stage, measured as M/S side-channel RMS. **(b)** at every macro neutral, `composedFxDelaySendForTest()` and `composedFxWanderDepthForTest()` are **bit-equal** (`==`) to the raw deep atomic for each of `{0,.25,.5,1.0}` on the deep knob. **(c)** *(the ruled form, D-2 — the spec now states this; it is no longer an open question)* with the deep knob **held still**, moving `kMacroDissolveId` changes `composedFxDelaySendForTest()` and moving `kMacroEntropyId` changes `composedFxWanderDepthForTest()` on the **next** `process()` call, and `composedEffectsRecomputeCountForTest()` equals the `process()`-call count. A build that read the raw deep atomic at `:2351` / `:3052` leaves both unchanged — that is exactly the RQ-4 defect. **`effectsPushes_` (`processor.cpp:1709 … :1834`) is NOT asserted about**: it is incremented only inside `pushEffectsParams()` (`:1665`), whose ID set excludes 1410 and 1441, so a *correct* implementation never moves it here and an assertion on it would fail on correct code. |
| SC-022(a), FR-026 | `tests/unit/controller/custom_view_test.cpp` | `Seraphis_Phase11_ViewSurface_IsExactlyThreePlusSubController` | **Two arms, and the first is a compile-time check, not a text scan.** A pure source scan **cannot resolve transitive bases** — `MacroRingKnob` derives from `Krate::Plugins::ArcKnob` (`arc_knob.h:49`), which is `class ArcKnob : public VSTGUI::CKnobBase` → `CControl` → `CView`, a chain that lives entirely outside `src/ui/`; and a fourth class written `: public VSTGUI::CTextLabel` or `: public Krate::Plugins::XYMorphPad` is transitively a `CView` and invisible to a token scan. That is precisely the failure mode FR-026 exists to catch (SC-004 counts *instances* and cannot see a class the uidesc does not reference). **Arm 1 (compile-time):** the TU includes every header under `src/ui/` and asserts `static_assert(std::is_base_of_v<VSTGUI::CView, T>)` holds for exactly the three named types `{CloudView, MacroRingKnob, DrawerContainer}` and `static_assert(!std::is_base_of_v<VSTGUI::CView, SeraphisEditSubController>)`. **Arm 2 (scan as a tripwire, not as the proof):** scan `src/ui/*.h` for every `class X : public B` and fail on any `B` **not on an enumerated allowlist** `{VSTGUI::CView, VSTGUI::CViewContainer, Krate::Plugins::ArcKnob, VSTGUI::DelegationController, VSTGUI::ViewCreatorAdapter}` — so an **unknown base name is a red test**, never a silent pass. Carries SC-011's guards (`filesMissing == 0`, `codeBytes > 0`, witness count `> 0` for the token `CloudView`). Adding a fourth view class therefore forces either a new allowlist entry *and* a new `static_assert` (a visible spec amendment) or a failing build. |
| SC-022(b), FR-045 | same | same | After `exerciseEditorLifecycle`, `createSubController` was called ≥ once and returned a non-null `UI::SeraphisEditSubController`; **each control in §8.4's table — including the header preset button (D-5)** — reports that object from `getListener()` and carries its assigned session tag (`≥ 9000`, so it can never be a `ParamID`); after `willClose()` `subControllerInstances_` is back to 0. |
| SC-022(c), **FR-021 (the view half)** | same | `Seraphis_MacroRing_DoesNotAnimateTheCloudViewLocally` | FR-021 is a requirement about the **view** — *"No view-local animation, no synthetic displacement, no interpolation toward a target the DSP is not producing"* — and its only criterion, SC-017, is measured **entirely on the producer**. A `CloudView` that faked the constellation's reaction to a ring would leave `P` unchanged and SC-017 would still pass. **Arm:** with a **fixed** cached `CloudFrame` (constant `sequence`, never updated), drive `MacroRingKnob::valueChanged` / `performEdit` across its full range and assert `cloudView_->invalidCountForTest()` and `cloudView_->drawnPointsForTest()` are **unchanged** — no redraw, no moved point. The view has no path from a macro value to a point position; the only input is the frame. |
| SC-022(d), **FR-007 (the preset browser)** | same | `Seraphis_PresetButton_OpensTheBrowser` | Nothing anywhere exercised FR-007's preset button. **Arm:** after `exerciseEditorLifecycle`, drive the button's `valueChanged` through the sub-controller and assert a `Krate::Plugins::PresetBrowserView` is present in the frame and bound to `presetManager_` (`controller.h:43`, `controller.cpp:57`); drive it again and assert the browser is gone. Run inside the ASan lifecycle lane so an open browser at `willClose()` is a report, not luck. |
| SC-023, FR-019 | `tests/unit/controller/editor_lifecycle_test.cpp` (extend) | `Seraphis_Editor_WorksWithNoFrameEverReceived` | Gate never opened, `cycles = 10`: the lifecycle completes; `getParameterCount() == 107` before and after; and **per cycle, between `attached()` and `removed()`, the test calls `cloudView_->renderForTest()` once** and then asserts `drawCountForTest()` ≥ 1 and `pointsDrawnForTest() == 0`. **`exerciseEditorLifecycle` alone cannot satisfy SC-023 as the spec words it** — it calls only `IPlugView::attached(nullptr, …)` and `removed()` (`tests/test_helpers/editor_lifecycle_harness.h:98-133`) and its banner records that the platform attach is a no-op (`CFrame::open(nullptr)` fails harmlessly, `:12-13`), so no paint cycle and no `CDrawContext` ever exists and `draw()` is **never** entered. The criterion is re-pointed at the §8.1 seam rather than left asserting about an unreachable path; the null-frame property it actually tests (a `draw()` with no frame ever received renders an empty field and dereferences nothing) is unchanged and is now genuinely exercised. Because `exerciseEditorLifecycle` owns the cycle loop, this arm drives the open/close pair directly (same three calls) rather than through the helper. Run under `-DENABLE_ASAN=ON` Debug so a null-frame dereference is a report, not luck. **Spec edit required (D-9, §11.2):** SC-023's "`draw()` is entered at least once per cycle **during `exerciseEditorLifecycle`**" clause. |
| SC-024, Q6 | `tests/integration/partial_edit_test.cpp` | `Seraphis_EditMode_AuthoringWorksWithoutANote` | Two identical ratio-drag pointer-delta sequences on the same slot's partial: (A) a voice sounding at exactly C4 (`fundamentalHz == 261.63`) with `BrownianDrift` **active**; (B) no voice (`activeVoices == 0`, `fundamentalHz == 0`). Both produce the same stored `ratios[index]` within float epsilon, and (A) contains no drift-baked error — proving the inverse map excludes drift, not merely that (B) avoids a divide by zero. |
| SC-025, Q2 | `tests/integration/partial_edit_test.cpp` | `Seraphis_BlendGesture_IsAbsoluteNotCompounding` | `BlendBegin` (kind 7) then `t = 0 → 0.5 → 1 → 0.5 → 0`: the selected slot is **byte-identical** to the pristine A snapshot (`blendStates(A,B,0) == A` is C-6's rule). A second gesture (`BlendBegin` at the now-current state, then `t = 1`) lands on B, not on a doubly-blended state. A kind 4 with no preceding kind 7 in the same gesture is dropped and leaves the slot unchanged. |
| SC-026, Q7 | `tests/unit/controller/custom_view_test.cpp` | `Seraphis_MultiEditor_RefcountGatesCorrectly` | Two `didOpen` then one `willClose` leaves the refcount at 1 and sends **no** close message; the gate stays open and `cloudFramePublishAttemptCountForTest()` keeps incrementing. Closing the second brings it to 0 and sends exactly one close. `terminate()` resets it regardless of prior value. |
| SC-027, Q8 | `tests/unit/controller/custom_view_test.cpp` | `Seraphis_EditThrottle_FlushesFinalValue` | A synthetic drag emitting 200 pointer-moves inside one 33 ms window, then mouse-up: **at most one** throttled message for the window, **plus exactly one** terminal message whose payload equals the last pointer-move's value, sent unconditionally. |
| **SC-028**, FR-033a (D1) | `tests/integration/partial_edit_test.cpp` | `Seraphis_EditMode_RatioEditReachesSoundingVoice` | **The criterion §2.3 exists for.** Note-on; render until the focus voice has `hasSounded_` set and is not finished (drive it past the attack, then read `getRejectedConfigureTimeCallCount()` — `seraphis_voice.h:784`, verified — as the *pre* value). Send a kind-1 `setPartial` ratio edit to the **contributing** slot (park the journey there first, `slotContributes` at `spectral_morph_engine.h:558`) mid-note. Assert: (i) within `SpectralMorphEngine`'s existing FR-047 absorption window — **Phase 3's time constant, no new one is introduced** — the rendered peak for that partial moves to within **5 cents** of the new authored ratio, measured exactly as SC-013(a) measures a peak move (4096-point FFT of steady state, under SC-013's drift precondition); (ii) `getRejectedConfigureTimeCallCount()` is **unchanged** across the push — the push was *accepted*, not rejected and retried onto a future note. Arm (ii) is the one that fails on an un-relaxed build even if the retry machinery eventually lands the edit. |
| **SC-029**, FR-033a (D1) | same file | `Seraphis_EditMode_LiveRatioEditIsClickFree` | Reuse **Phase 3's own continuity machinery**, not a second one. The bounds are `SpectralMorphEngine::kMaxAmpDeltaPerChunk = 0.025f` (`spectral_morph_engine.h:133`) and `kMaxRatioDeltaCentsPerChunk = 125.0f` (`:134`), **referenced by name from the header, never restated as literals** — the FR-044 contributor `static_assert`s at `:156-186` sum the enumerated per-chunk contributors against them, so the case cannot be made to pass by loosening a constant. The measuring device already exists: `class ChunkDeltaTracker` (`dsp/tests/unit/systems/spectral_morph_engine_test.cpp:990-1026`, with `worstAmpDelta()` at `:1016`), used by `SpectralMorph_TravelIsContinuous` (`:1145`, asserted `:1182`). Port that tracker (or lift it into a shared test header) and drive it from the plugin-side render while the mid-note ratio edit lands; assert **no chunk-to-chunk step exceeds either bound** across the full absorption window. **A measured discontinuity bound, never a bit-exact comparison** (root `CLAUDE.md`, *Cross-Platform Compatibility*). Sanity arm: an in-DSP `SpectralMorphEngine::setState` on the same slot at the same instant satisfies the **same** bounds — proving the relaxation created no second, worse path. |
| **SC-031** (new), FR-033a / R-18 | `tests/integration/ui_perf_test.cpp`, `[.perf]` | `Seraphis_EditGestureInFlight_FitsTheBudget` | **The arm §2.3's relaxation needs and no existing criterion supplies.** SC-009, SC-010 and SC-014 arm 7 all run with a **static** slot set; SC-029 measures continuity, not time. Hold a note at the 8-voice operating point and drive a **30 Hz** kind-1 partial-ratio drag (the §7.4 throttle rate — one accepted `EditMessage`, hence one `stageSlotEdit` handoff, every 33 ms), so `consumeSpectralSlotHandoff()` re-arms `spectralRetryMask_ = 0xFFFFu` (`processor.cpp:2834`) and `applySpectralStates` runs 16 voices × 4 slots of `buildSanitized` ≈ **4096 `std::log2`** roughly every third block at 512 / 48 kHz. Protocol: fresh boot, **seven** runs, best-of-16 per estimate, **worst reported**. Assert worst-of-seven **whole-`process()`** ns/block ≤ `kFullPolyCeilingNs = 2 666 666.7` (`param_perf_test.cpp:392`) against Phase 10's pinned 2 380 980 ns — the same 2.68 points of headroom R-12 tracks. **If it fails the push gets cheaper, not the ceiling higher**: narrow `spectralRetryMask_` to the voices that can still reject, or add a pre-`applySpectralStates` identity check against the last-pushed `spectralSlots_` so an unchanged slot never reaches `buildSanitized` (§2.3). Reducing the throttle rate below C-8's 30 Hz is **not** an available fix. |
| **SC-032** (new), FR-028 | `tests/unit/controller/custom_view_test.cpp` | `Seraphis_CloudView_GesturesEmitTheRightEditMessage` | **Three of FR-028's four gestures had no criterion** (§8.1): every editing test injects `EditMessage`s at `Processor::notify` instead of driving the view, and only the plain vertical ratio drag is covered at view level (SC-024). Drive `CloudView::onMouseDown` / `onMouseMoved` / `onMouseUp` with synthetic `CPoint` / `CButtonState` sequences against a fixed synthetic frame, and after each gesture read `controller.lastSentEditMessageForTest()` (§7.4). Assert **all four rows** of §8.1's table: (1) plain vertical drag ⇒ `kind == 1`, `a == newRatio` from the inverse map, **`b == slotMirror_[slot].amplitudes[i]` unchanged**; (2) **alt** + vertical drag (`CButtonState` carrying `kAlt`, a plain VSTGUI modifier — never a platform key API, FR-005) ⇒ `kind == 1`, **`a` unchanged** and `b == newAmp`; (3) horizontal drag (`\|dx\| >= \|dy\|`) ⇒ `kind == 2`, `a ∈ [-1, +1]` and equal to the clamped x-map; (4) click within `kClickSlopPx` ⇒ `kind == 3` with `a` the **toggled** value computed from `maskBits` — assert **both** directions by running it twice against frames whose `maskBits` bit *i* is clear then set, giving `a == 1.0f` then `a == 0.0f`. `index` equals the hit-tested partial in all four. A view that emitted kind 2 for an alt-drag, never set `b`, or sent an unconditional mask passes every other criterion in §10; this is what makes `EditMessage::b` a live field. |
| **SC-033** (new), FR-028 / FR-030 / Q5 | `tests/integration/partial_edit_test.cpp` | `Seraphis_PartialMask_ToggleOffRestoresTheVoice` | **The unmask half of the mask gesture, end to end, which no criterion reached.** SC-006(e) compares `maskBits` against the processor's own table (not the engine); SC-014's mask arms only assert a mask *survives* a clearing event; and with the previous draft's re-push body — walk the **set** bits and push `active = false` — clearing a bit produced **no engine call at all** and `HarmonicCloud::masked_[i]` stayed `true` forever (`harmonic_cloud.h:1084-1089`). **Arm:** hold a note; send kind 3 with `a = 1` for partial *k*; render past the amplitude smoother and assert, **through `engineForTest()` on every voice in `[0, kMaxVoices)`**, that partial *k*'s current amplitude has decayed to ≈ 0 while partial *k+1*'s has not. Then send kind 3 with `a = 0` for the same *k*, render the same span, and assert partial *k*'s amplitude **recovers to within 1 % of its pre-mask value on every voice** — sixteen slots, not just the allocated one, for the same reason FR-033's fan-out covers `kMaxVoices`. Finally assert `lastPublishedFrameForTest().maskBits` bit *k* is clear, so the frame and the engine agree. |

### 10.3 `-fno-fast-math` source properties

`plugins/seraphis/tests/CMakeLists.txt`'s `set_source_files_properties` block gains **only** the TUs that
inject non-finite payloads or measure per-sample statistics:

```
integration/partial_edit_test.cpp        # SC-018 bit-pattern non-finite fuzz
integration/ui_negative_control_test.cpp # SC-001's exact per-sample comparison
```

`integration/cloud_frame_test.cpp` stays **out** if it carries `[.perf]` arms (SC-009/SC-010 baselines
would move under `-fno-fast-math`, the same rule `param_perf_test.cpp` and `effects_perf_test.cpp`
already follow) — **split** the `[.perf]` arms into `tests/integration/ui_perf_test.cpp` so the
functional TU can take the flag and the perf TU cannot. Do the split up front; retro-fitting it after
a baseline is pinned invalidates the baseline.

### 10.4 The two pilot measurements (OQ-4) — methodology and acceptance band are RULED

The phase owner ruled on 2026-08-03: **methodology and acceptance band decided now; the two numbers stay
pending this measurement — measured then fixed, never chosen and never relaxed afterwards.** Both numbers
MUST be written back into `spec.md` (SC-017(a) and C-10 clause 1) **before compliance** — a placeholder a
compliance pass quietly keeps is exactly the failure this rule exists to prevent.

1. **`.amount` for the two new macro rows.**
   - **Starting values (fixed by the ruling, not free):** `0.35f` Dissolve → `FxDelaySend`, `0.50f`
     Entropy → `FxWanderDepth`.
   - **Acceptance band (fixed by the ruling):** the isolated send-return RMS at **Dissolve = 1** MUST land
     between **−20 dB and −6 dB** relative to the dry sum, **and** the five-point sweep MUST be strictly
     monotone. A value outside the band is rejected and the `.amount` moves; the band does not.
   - **Procedure:** build with the starting values; run SC-021(a)'s sweep (one block of settle per point,
     §4.2); record the isolated send-return RMS — mean of the per-channel RMS over
     `preOutputTapLForTest()` / `preOutputTapRForTest()` (`processor.h:431`, `:434`) with
     `preOutputTapTruncatedForTest() == false` (`:444`) — at all five Dissolve points, and the M/S side
     RMS at all five Entropy points. Record the **full five-point table**, not just the accepted number,
     so the monotonicity claim is inspectable.
   - **Write-back:** the accepted `.amount` literals go into `kRows` (§3.2) **and** into spec C-10
     clause 1; the measured table is appended to this section.
2. **SC-017(a)'s octave threshold.** Run the Bloom sweep once under SC-013's drift precondition, compute
   `P(1.0) − P(0.0)`, round **down** to two decimals, and replace the `0.35` placeholder in spec
   SC-017(a). If the measured value is **below** 0.35 the *criterion* moves, not the implementation —
   the roadmap's "Bloom pulls partials upward" is what is being measured, and a smaller true value is a
   smaller true value. Raising the implementation's response to hit a pre-guessed number would be tuning
   the instrument to a test.

**Both write-backs are a task, not a note:** tasks.md T025 owns them and T026 owns the resulting spec
edits. Neither the `.amount` nor the octave figure may still read "pilot" or "placeholder" when the
compliance table is filled.

#### 10.4.1 Measured tables (recorded 2026-08-04, T026's write-back)

**Row 1a — `Dissolve → FxDelaySend`. MEASURED, accepted, written back into spec C-10 clause 1 and into
`kRows` (`seraphis_macro_matrix.h`).** The ruled pilot start `0.35f` did **not** hold: it put SC-005's
ID-102 arm (`tests/integration/param_continuity_test.cpp`, "no zipper, no click") over its 1.5× bound.
Measured ratio `maxTest/maxRef` over the Dissolve automation render against `.amount`, everything else
held, alongside the isolated send-return RMS at Dissolve = 1:

| `.amount` | SC-005 ratio (bound 1.5) | isolated return at Dissolve = 1 | verdict |
|---|---|---|---|
| `0.00` | 1.3532 (pre-Phase-11) | — (send never runs) | baseline |
| `0.12` | 1.4232 | ≈ −23.7 dB | **outside** the ruled `[−20, −6] dB` band |
| **`0.20`** | **1.4665** | **−19.3 dB** | **ACCEPTED** — lowest in-band value, most SC-005 headroom |
| `0.35` (pilot start) | 1.5138 | −14.4 dB | **SC-005 FAILS** |

Both margins are thin (2.2 % on SC-005, 0.7 dB inside the band). The cause is **not** this row: ID 102's
SC-005 ratio was already 1.3532 before Phase 11, against ≈ 1.0 for every other in-scope ID (Dream 0.98,
Bloom 1.03, Gravity 0.91, Entropy 1.01, 1410 1.02, 1441 1.12), so Dissolve's Phase 9 rows leave almost no
room for a new audible reach. The five-point SC-021(a) sweep at `0.20f` is strictly monotone from exactly
`0.0` at neutral, which is the band's second half.

**Row 1b — `Entropy → FxWanderDepth`. NOT MEASURED. OUTSTANDING, blocking.** `0.50f` is still the ruled
starting value and no five-point M/S **side**-channel RMS table exists beside it. Required before
compliance, in this section and in spec C-10 clause 1: the sweep over `kMacroEntropyId ∈ {0, .25, .5, .75,
1}`, one block of settle per point, side RMS taken over `preOutputTapLForTest()` / `preOutputTapRForTest()`
with `preOutputTapTruncatedForTest() == false`, asserted **strictly monotone**. If it is not monotone the
`.amount` moves; the band does not.

**Row 2 — SC-017(a)'s octave threshold `T`. NOT MEASURED. OUTSTANDING, blocking.** The value standing in
both `spec.md` SC-017(a) and the test's threshold constant is the ruled pilot start **`0.35`**. The
measurement device is already in place and prints on a **passing** run — `Seraphis_MacroRing_
PerturbsConstellation` (`tests/integration/cloud_frame_test.cpp`) emits the full five-point `P` table and
`P(1) − P(0)` via `WARN` (not `INFO`, so a measurement whose purpose is to be read is not hidden exactly
when it succeeds) — so the remaining work is to run it, read the delta, round **down** to two decimals,
and write that number into **both** places in one change. If the measured value comes in below `0.35`,
**the criterion moves, not the implementation.**

---

## 11. Deviations: what the rulings closed, and what is still open

### 11.0 Closed — no action beyond building what the ruling decided

| # | Ruling (spec *Clarifications*, 2026-08-03) | Where it lands in this plan |
|---|---|---|
| **D-1 / OQ-A** | **RELAXED, not disclosed.** The plan's recommended (A) was **overruled**. `SeraphisVoice::setSpectralState`/`setSpectralStateCount` stop routing through `isConfigurable()`; `spectral_morph_engine.h:199-206`'s comment is corrected in the same change; the "applies on next note" indicator is **not** built. | **§2.3** (the edit, its scope and its safety argument), §10.1 SC-030 row, §10.2 SC-028/SC-029 rows, **R-16** (§12), tasks.md T003a/T010 sequencing in §13 |
| **D-2 / OQ-B** | **Accepted; replacement observable adopted.** `effectsPushes_` provably cannot move for 1410/1441, so SC-021(c) now asserts `composedFxDelaySendForTest()` / `composedFxWanderDepthForTest()` / `composedEffectsRecomputeCountForTest()` instead. | §4.2 (unchanged mechanism), §10.2's SC-021 row (rewritten to the ruled form) |
| **Composition cadence** | **Accepted as designed, no spec weakening.** The one-block lag stands; SC-021(a) allows exactly one block of settle per sweep point. | §4.2's ruling box |
| **`fundamentalHz`'s source** | **`frequencyHz[0]` forbidden by name.** | **D-8** below and §5.3 — resolved to `HarmonicCloud::getFundamentalHz()` (`:405`), which is neither of the ruling's two named candidates and is better than both |
| **OQ-4** | **Methodology + acceptance band fixed; the two numbers stay pending measurement.** | §10.4 (band: send-return RMS at Dissolve = 1 in **[−20 dB, −6 dB]**, sweep strictly monotone; write-back before compliance) |
| **SC-008's tolerance** | **Confirmed as worded, no change.** `1e-5` relative stands unless a pilot measures a larger spread, in which case the *measured* number is recorded. | §10.2's SC-008 row, already stated that way |
| **The extra `dsp/` test TU** | **Accepted.** `dsp/tests/unit/systems/seraphis_partial_fanout_test.cpp` is its own TU. | §2 preamble, §10.1's FR-033 row, §13 |
| **CMake registration pattern** | **Accepted as authored.** Each task appends its own new file to the relevant enumerated list so it runs red-then-green from its own task; tasks.md T027 is the single authoritative audit pass. | §13 |
| **D-7** | Absorbed into the spec — SC-021(a) now names `preOutputTapLForTest()` (`processor.h:431`) and `preOutputTapRForTest()` (`:434`). | §10.2's SC-021 row |

### 11.1 D-8 — `fundamentalHz` resolves to an accessor that already exists (NEW this session)

The previous draft's non-blocking note said `HarmonicCloud` exposes no `getFundamentalHz()` and proposed
note-derived bookkeeping. Both halves were wrong, and the ruling's two named candidates both fail:

- `[[nodiscard]] float getFundamentalHz() const noexcept` **exists** at `harmonic_cloud.h:405`; its shadow
  `fundamentalHz_` (`:2115`) is written **only** by `setFundamentalHz` (`:383`), whose **only** caller in
  the tree is `SeraphisVoice::noteOn` (`seraphis_voice.h:529`). No drift multiplier ever touches it.
- The `dispatchEvent`-bookkeeping fallback is **not implementable**: `SeraphisEngine::noteOn(std::uint8_t,
  std::uint8_t)` (`seraphis_engine.h:476`) allocates internally and returns nothing, so
  `processor.cpp:103` never learns the slot; `allocator_.getVoiceNote` is internal (`:494`, `:1333`).

**Resolution:** §5.3 publishes `cloud.getFundamentalHz()` when `activeVoices > 0` and `0.0f` otherwise.
**No spec edit required** — the ruling's binding clause is that the source must not be `frequencyHz[0]`,
and this is not. **No `dsp/` addition**, so the closed enumerated set is untouched. Bonus: it removes the
last transcendental from the audio-thread producer.

### 11.2 Still open — five plan-level corrections, each needing a spec edit

### D-3 — `[partials]` is 272 bytes, and the previous rationale for it was false

**The size.** Spec FR-034a's "≈268 B" is an **arithmetic error**, nothing more: 64 floats (256) + two
64-bit masks (16) = **272**. The plan pins 272.

**The accessor claim is struck.** The previous draft justified 272 with *"`IBStreamer` exposes no 64-bit
integer accessor, so the two bitmasks move as four `int32`s"*. That is factually wrong on both halves,
and it mattered because T026 copies D-3's reasoning into the spec. `IBStreamer` publicly inherits
`FStreamer` (`extern/vst3sdk/base/source/fstreamer.h:202`), which declares public
`bool writeInt64u(uint64)` / `bool readInt64u(uint64&)` (and signed `writeInt64`/`readInt64`) at
`fstreamer.h:97-106` — directly callable on an `IBStreamer&`, and already an established pattern in this
codebase at `plugins/disrumpo/src/processor/processor_state.cpp:356` and `:908`. And the split changed
no arithmetic anyway: two 64-bit masks are 16 bytes either way. §6.4 therefore writes each mask as **one
8-byte field** and drops the unmotivated four-`int32` split-and-reassemble.

### D-4 — the drawer's rects are in the ROOT's coordinate space, so the drawer must be a root child

`CView::getViewSize()` is in **parent** coordinates. FR-023 requires the drawer's `getViewSize()` to be
*exactly* `(0, 670, 1000, 700)` collapsed and `(0, 420, 1000, 700)` open (spec `:936-940`; C-1's table
`:358-366`). A draft of §9 nested the drawer two levels deep (root → a `sub-controller="SeraphisEdit"`
container at `origin="0, 32" size="1000, 668"` → the drawer): relative to that parent the declared rect is
absolute `(0, 702, 1000, 732)` — 32 px below the window, entirely outside the parent's 668 px height, so
the collapsed strip is clipped invisible — and SC-020(c)/(e)'s byte-comparison against the two `constexpr`
rects could never pass.

**Resolution taken: `sub-controller="SeraphisEdit"` moves onto the template root, and `DrawerContainer`
and `CloudView` are direct children of it** (§9). VSTGUI honours the attribute on the template node —
`UIDescription::createViewFromNode` reads it for any node
(`extern/vst3sdk/vstgui4/vstgui/uidescription/uidescription.cpp:672-677`) and the template goes through
that same function (`:778`). **No spec number changes**: C-1's table, FR-023, FR-024 and §8.3's two
`constexpr` rects all now speak the same absolute space. The rejected alternative — restating FR-023 in
parent-relative form as `(0, 638, 1000, 668)` / `(0, 388, 1000, 668)` — would have made the spec's
window-relative geometry unreadable and would have left FR-024's cloud-view rect in a third coordinate
space. **Spec edit required:** none to the numbers; C-7's prose "the drawer/cloud sub-tree's root
container carries `sub-controller`" should read "the template root carries it".

### D-5 — the preset button had no listener owner and no criterion

FR-007 requires a header preset button opening a `PresetBrowserView` over `presetManager_`
(spec `:878-884`), and §9 declares it tag-less. FR-045 requires that **every** tag-less control have
`SeraphisEditSubController` as its `IControlListener` and that *"no tag-less control's `valueChanged` may
live on `Controller`, on a `CView` subclass, or nowhere"* (spec `:1102-1107`) — but C-7b's table
(spec `:705-713`) omits the button, and a draft of §9 placed it in the header, **outside** the
sub-controller's container. Under both documents it therefore had no owner, and no criterion anywhere
exercised the browser.

**Resolution:** D-4's root-level sub-controller puts the button inside the sub-tree by construction; §8.4's
table gains a `kPresetButtonTag` row; SC-022(b) asserts its `getListener()`; **SC-022(d)** (new) asserts
clicking it opens and closes the `PresetBrowserView`. **Spec edit required:** add the preset button row to
C-7b's table. FR-045 is **not** carved out — carving it out would have been the weakening resolution.

### D-6 — `cloudFrameEnabled_` is `std::atomic<bool>`, not a plain `bool`

Spec C-2 clause 6 (and `:565`, `:1072`) specify the gate as *a plain bool*. It is written on the message
thread (`Processor::notify` kind 0, plus `setCloudFrameGateForTest`) and read on the audio thread in
`publishCloudFrame()` every `process()` call — an unsynchronised cross-thread `bool`, i.e. a data race
under the C++ memory model however benign the generated code is. The repo's own precedent for a
message→audio flag is an atomic (`plugins/membrum/src/processor/processor.cpp:1191`).

**Resolution:** `std::atomic<bool>` with `relaxed` ordering on both sides (§5.3, §6.2). The same applies
to `partialOverridesPending_` (§6.2/§6.4), which uses **release/acquire** because it does publish other
state, and to the two override bitmasks and the pan array (§6.3). **Spec edit required:** C-2 clause 6's
"plain bool" wording. Cost is one relaxed load per `process()` call; SC-010's closed-gate arm is
unaffected.

### D-7 — SC-021(a) names an accessor that does not exist

Spec SC-021(a) reads *"read at `preOutputTapForTest()` with `preOutputTapTruncatedForTest() == false`"*
(spec `:1431-1433`). There is no `preOutputTapForTest()`. The real accessors are
`preOutputTapLForTest()` (`processor.h:431`) and `preOutputTapRForTest()` (`:434`); `:430` is a comment
line. `preOutputTapTruncatedForTest()` at `:444` is correct.

**Resolution:** §10.2's SC-021 row now names both channel accessors and states the statistic —
the **mean of the per-channel RMS over L and R**. **Spec edit required:** SC-021(a)'s accessor name and
the channel statement.

### D-9 — the spec sentences this revision proved wrong, and the criteria it added (NEW)

D-3 – D-7 were the previous draft's open set. This revision adds a ninth, and it is a **collection**: a
list of spec statements that the plan (correctly) contradicts or extends and that T026 must edit, so no
reader inherits a wrong instruction and no new criterion is invisible at compliance time. Each row names
the sentence and the plan section that supersedes it. **Nothing here relaxes a threshold**; where a
criterion is restated it is because the criterion as written was unsatisfiable by a *correct*
implementation.

| # | Spec sentence to edit | Why | Plan §|
|---|---|---|---|
| 9a | **C-4's mask row** (spec `:609`): `setPartialMaskAllVoices(i, !currentMask)` | Polarity-inverted. `setPartialMask`'s body is `masked_[index] = !active` (`harmonic_cloud.h:1082-1089`), so for an already-masked partial `!currentMask == false ⇒ active = false ⇒ masked_ = true` — it stays masked and the documented unmask gesture is a no-op. Restate as `setPartialMaskAllVoices(i, /*active=*/!desiredMasked)`. | §2.1 box, §6.3 |
| 9b | **C-5 clause 1** (spec `:656-658`): *"kinds 2, 3 write `partialOverrides_` and call the C-4 fan-outs directly"* | The fan-outs write `HarmonicCloud` state `process()` reads and writes; calling them on the message thread is a data race. Restate: kinds 2/3 stage + release-store `partialOverridesPending_`; the audio thread performs the fan-out. | §6.2 |
| 9c | **C-9** (spec `:857-859`) and **FR-042** (spec `:1222-1224`): *"the only additions inside `process()` are …"* | Incomplete after 9b. Add `partialOverridesPending_.exchange()` and the deferred `repushPartialOverrides()` to the enumeration. | §6.2, §6.3 |
| 9d | **C-6's `setPartial` no-op list** (spec `:698-699`) | Names only local rejections; must name **whole-state invalidity**, or SC-012 clause 2's byte-unchanged assertion and the contract disagree. | §1.1 step 0 |
| 9e | **C-2 clause 7** (the attempt counter) | Must state the counter increments whenever the **gate** is open, independently of whether a DataExchange queue exists — otherwise every frame-content and cadence criterion is unrunnable in the harness (`ProcessorFixture` never calls `connect()`). | §5.3 |
| 9f | **SC-007** and **R-1**: *"equals the number of `process()` calls"* | False as an invariant: six pre-slice-loop early returns (`processor.cpp:978`…`:1008`) publish nothing. Restate as *"`process()` calls that reached the slice loop"*. | §10.2 SC-007 |
| 9g | **SC-023**: *"`draw()` is entered at least once per cycle **during `exerciseEditorLifecycle`**"* | Structurally unreachable — the harness never attaches a platform window and no `CDrawContext` exists (`editor_lifecycle_harness.h:12-13`, `:98-133`). Re-point at the `renderForTest()` seam. | §8.1, §10.2 SC-023 |
| 9h | **SC-026 clause 2**: *"activation allocates nothing"* | `DataExchangeHandler::onActivate` allocates in the SDK fallback path (`dataexchange.cpp:76-105`). Narrow to *"no allocation on any audio-thread-reachable path"*; the host-thread queue open is out of scope. | §5.2 |
| 9i | **SC-013's drift precondition** (spec `:1439-1440`), stated only in plugin `ParamID`s | Unimplementable in the `dsp/` TU that hosts every SC-013 arm (§10.1) — `kCloudDriftDepthId` (205) and `kMacroEntropyId` (104) do not exist there — while **SC-028** (spec `:1642`) cites it *"exactly as SC-013(a) measures"* from a plugin TU where the ParamID form **is** correct. Restate in **both** forms: *"`setDriftDepthCents(0)` on the cloud under test (dsp TU), or `kCloudDriftDepthId` = 0 with every macro at its FR-060 neutral (plugin TU)"*, so both criteria can cite it verbatim. | §10.1 SC-013(a) |
| 9j | **The Success Criteria list itself** | This plan adds ~15 criterion arms that exist in no version of spec.md — SC-004 arms 2 and 3; SC-006 arms (g), (h), (i); SC-011 arms for lock-freedom and FR-005; SC-014 arms 6 and 7; SC-016 arm 2; SC-020 arms (f) and (g); SC-022 arms (c) and (d); and the three new criteria **SC-031**, **SC-032**, **SC-033**. Several are the **only** criterion for an FR (FR-005, FR-006, FR-011, FR-014, FR-016, FR-017, FR-021's view half, FR-022, FR-025, FR-027, FR-028, FR-046's second re-seed source). Since the compliance table is filled against **spec.md's** SC list (root `CLAUDE.md`, *Completion Honesty*), an arm that lives only in the plan is invisible at compliance time and is the first thing dropped under schedule pressure. **Every arm above must be written into spec.md's Success Criteria with its FR back-reference, and the matching Traceability rows added**, in the same T026 pass. | §10 throughout |

### Non-blocking notes

- **`fundamentalHz`'s source is settled** — see D-8 (§11.1). The earlier "derive it from the note /
  confirm at T-time" note is **struck**: the accessor exists (`harmonic_cloud.h:405`) and the fallback it
  proposed was not implementable.
- **`CloudFrame` has 4 interior padding bytes.** Handled by `memset`-once + field assignment (§5.1);
  do not rely on aggregate `{}` to zero padding.
- **`setTargetBasePushes_` goes 27 → 29 at prepare** (§4.1). `param_cadence_test.cpp`'s SC-007 table
  carries that literal and must move with it. Grep before editing.
- **`kNumRows` goes 30 → 32** and the enum's `Count` moves by exactly 2. Any test that pins those
  numbers (`seraphis_macro_test.cpp`) moves in the same change.

---

## 12. Risks and mitigations

| # | Risk | Why it is real here | Mitigation |
|---|---|---|---|
| R-1 | **Publishing per slice, not per call** | `renderSlice` runs once per MIDI slice, per 2048-sample cap, and per 64-sample grid step while any class-(b) smoother is unsettled (`processor.cpp:1298-1311`) — up to 8× per block. A per-slice publish burns the 4-block queue in one call. | Publish is a single call **after** the slice loop, and SC-007 asserts `attempts ==` the number of `process()` calls **that reached the slice loop** (divisor `effectsStageProcessCalls_`, `processor.cpp:1189`) exactly, **and** `renderSliceCount > attempts` strictly. The "reached the slice loop" qualifier is not a softening: `process()` has six pre-loop early returns (`:978`…`:1008`) that a real host and pluginval-5 do hit, so an equality against the raw host call count is false about a **correct** build. D-9 row 9f edits this sentence in the spec too. |
| R-2 | **A success-counting cadence seam makes SC-007 unsatisfiable** | `numBlocks = 4` filled at ≈94 Hz and drained at 30 Hz exhausts in ~64 ms by design; headless runs may land zero blocks. | Two counters: `cloudFramePublishAttemptCountForTest()` (incremented before `getCurrentOrNewBlock()`) gates; `cloudFrameSkippedBlockCountForTest()` is reported and asserted about not at all. |
| R-3 | **Cross-thread races on the override table AND on the engine itself** | The producer reads both masks and the pan array on the audio thread; the editor writes them on the message thread. Worse, a draft had `applyEditMessage` call `setPartialPositionAllVoices` / `setPartialMaskAllVoices` **directly on the message thread**, writing `HarmonicCloud::panPosition_`/`positionOverridden_`/`panLeft_`/`panRight_`/`masked_` (`harmonic_cloud.h:1069-1089`, `updatePanGains` `:1818-1834`) that `process()` reads and writes concurrently. | (1) `partialPanStaging_` is `std::array<std::atomic<float>,64>`, both masks are `std::atomic<std::uint64_t>`, all `relaxed`; lock-free on every target this repo builds, asserted once with `is_lock_free()`. (2) The **fan-outs are audio-thread-only** (§2.2): kinds 2/3 stage + release-store `partialOverridesPending_`, and `process()` acquire-`exchange`s it and calls `repushPartialOverrides()` — the same deferral `stageSlotEdit`/`spectralSlotsHandoff_` already implements for kinds 1/4/5, and the same shape Membrum's `notify` uses (`plugins/membrum/src/processor/processor.cpp:1191`). |
| R-3a | **`cloudFrameEnabled_` as a plain `bool`** | Written from `Processor::notify` (message thread) and `setCloudFrameGateForTest`, read from `publishCloudFrame()` (audio thread) every call. A plain cross-thread `bool` is a data race under the C++ memory model regardless of codegen. | `std::atomic<bool>`, `relaxed` both ways (D-6); one relaxed load per `process()` call, which does not disturb SC-010's closed-gate arm. Same for `partialOverridesPending_`, which uses release/acquire because it *does* publish other state. |
| R-3b | **The clearing-event tracker keyed on the wrong value** | `setStereoSpread` clears `positionOverridden_` on any **value** change (`harmonic_cloud.h:535-547`), and the value pushed is the **composed** macro output — Bloom writes `CloudStereoSpread` with `.base = 0.35f, .amount = 0.60f` (`seraphis_macro_matrix.h:252-257`) via `macros_.apply()` every slice (`processor.cpp:1858`, `:635`). A tracker on `ParamID` 207 is blind to a Bloom sweep, so moving a shipped ring silently wipes every user pan override. | §6.3 keys the detection on the cached composed `CloudStereoSpread` value, not on 207. SC-014 arm 6 sweeps `kMacroBloomId` with 207 held still and fails on exactly that defect; SC-014 arm 7 measures the resulting per-slice re-push cost rather than assuming it stays on one block. |
| R-4 | **`tiltState` overflow / denormal** | Exponent spans `[−9.6, +9.6]` in log10 ⇒ multiplier `[2.5e−10, 4.0e9]`. | Bounded by §1.3's range analysis: worst `sumSquares ≈ 1.0e21`, three orders inside binary32; smallest product `2.5e−13`, well above the `1.2e−38` normal floor. Normalisation brings everything back to `[0,1]`. `sumSquares > 0` guard (`spectral_state.h:169`) keeps NaN out of the all-zero case. |
| R-5 | **`blendStates` breaks strict monotonicity** | `isValidSpectralState` requires `ratio > previousRatio` strictly (`:97-99`); `SpectralMorphEngine::setState` rejects wholesale on failure (`:296-298`), so a broken blend is *silently inert*. | `log2`-domain interpolation is monotonicity-preserving by construction (§1.2 proof), and **no clamp is applied** — a clamp is the one operation that could flatten neighbours. SC-012 clause 3 asserts unconditional validity; SC-012's acceptance arm asserts `setState` does not take the rejection branch. |
| R-6 | **`exp10f` sneaks in** | glibc declares a global `exp10f`; MSVC does not. The repo hit this before (`continuous_body.h:1643-1645`). | C-6 bans it by name; `std::pow(10.0f, x)` at a configuration-time call site; `node tools/check-portability.js` as the backstop, not the finder. |
| R-7 | **`std::isnan` under `-ffast-math`** | The macOS leg builds `-ffast-math`; the SDK does too. | Every finiteness test uses `detail::isNaN`/`detail::isInf` (`spectral_state.h:26`, `:91`) or the matrix's own `isFiniteBits` (`seraphis_macro_matrix.h:748`). Non-finite **test inputs** are built from bit patterns through a `volatile` sink, never `std::numeric_limits`. |
| R-8 | **A `ViewCreator` TU that is not linked** | The Phase 8 uidesc banner names this exactly (`editor.uidesc:3-5`): a custom class name is dropped **silently** and the view falls back to a stock class. | `entry.cpp` includes every creator header (FR-052, Ruinae's shape at `entry.cpp:18-32`), and **SC-004 identifies views by `dynamic_cast`**, not by count — a stock fallback fails the test. |
| R-9 | **Enumerated CMake source lists** | `plugins/seraphis/tests/CMakeLists.txt:16-32` and `plugins/seraphis/CMakeLists.txt:17-46` are **not globbed**; `dsp/tests/CMakeLists.txt` likewise. An omitted TU drops silently and Catch2 reports "No tests ran" rather than a failure. | §13's task list adds every new `.cpp` to both lists explicitly; the first build after each new TU is checked for the case count moving, not just for exit 0. |
| R-10 | **`UIViewSwitchContainer` for the drawer tabs** | It realises only the active template, so `unreachableParams` would report six tabs' worth of IDs as unreachable — and C-3 mandates an **empty** allowlist. | Seven plain child containers with `setVisible`, all present in the XML. The reachability helper is text-level (`uidesc_reachability.h:73-85`) and sees every template. |
| R-11 | **Padding bytes crossing the process boundary** | `CloudFrame` has 4 interior padding bytes; a stack aggregate's padding is indeterminate. | One `memset`-to-zero of the member `pendingFrame_` in `setupProcessing`, field assignment thereafter. |
| R-12 | **The 2.68-point CPU headroom** | Phase 10's worst-of-seven at the 8-voice gate is 22.32 % against a 25 % ceiling that is **not a lever**. | The producer is a ≤ 64-iteration read loop + one 808-byte `memcpy`, gated on a bool. SC-009(b) budgets it at 0.10 % of one core independently. If (a) fails, the producer is made cheaper (drop `overriddenBits`' recompute, publish every other call) — never the ceiling. |
| R-13 | **MSVC-green proves nothing** | The other two legs reject what MSVC accepts; `constexpr` from SDK constants is the known trap. | `node tools/check-portability.js` before every commit (a `guard-portability.js` PreToolUse hook runs the `--staged` form); `node tools/lint-layers.js`; `node tools/lint-arch-guarded-includes.js`; clang-tidy `-Target seraphis`. |
| R-14 | **A bit-exact float golden creeps in** | SC-001 is an exact `== 0.0f` sample comparison and *looks* like one. | It is same-instance, same-build, same-code-path — the only difference is a bool. The spec states this inline and forbids the cross-build form; `node tools/lint-float-bit-goldens.js` is the gate. |
| R-15 | **Editor-lifecycle UAF** | Raw view pointers cached on the controller; three timers. | Every cached pointer zeroed in `willClose()`; every timer cancelled in `removed()`; `subControllerInstances_` reset in `willClose()`; the open refcount reset in `terminate()`. SC-005 under ASan + the valgrind-nightly `[lifecycle]` lane are the only places this has teeth (Release passes by luck). |
| R-16 | **D-1's gate relaxation regresses a Phase 3 invariant** | §2.3 deletes an early return that a Phase 9 doc comment justified by citing `spectral_morph_engine.h:198-207`. If that comment were load-bearing rather than over-grouped, a live `setState` would click or corrupt the journey — and `SeraphisVoice` is the *only* caller of `morph_.setState` in the voice, so nothing else would catch it. | Three layered gates, none of which is a re-run of the other. (1) **SC-030**: Phase 3's `spectral_morph_*` suites pass **unmodified** — run before and after §2.3 and diff the Catch2 summary; editing a Phase 3 test to make it pass *is* the failure. (2) **SC-029**: the mid-note edit is bounded by Phase 3's **own** `kMaxAmpDeltaPerChunk` / `kMaxRatioDeltaCentsPerChunk`, plus a sanity arm proving an in-DSP `setState` at the same instant satisfies the same bound — so the relaxation cannot have created a second, worse path. (3) The edit is **scoped to two call sites**: `isConfigurable()` (`:908`), `rejectedConfigCalls_` (`:1208`) and `getRejectedConfigureTimeCallCount()` (`:784`) are all **kept**, so every other gated caller is untouched and SC-028(ii) still has its observable. `setState`'s body (`:292-315`) is not edited at all. |
| R-18 | **D-1's relaxation makes every voice pay `buildSanitized`, and Phase 11 turns that into a stream** | `SpectralMorphEngine::setState` runs `isValidSpectralState` **and** `buildSanitized` — a 64-entry `std::log2` pass (`spectral_morph_engine.h:292-301`, `:513`, `:537-543`) — **before** the identity early-out at `:302-305`; `consumeSpectralSlotHandoff()` re-arms `spectralRetryMask_ = 0xFFFFu` (`processor.cpp:2834`), so one handoff costs 16 voices × 4 slots ≈ **4096 `std::log2`**. Today a held note makes 16 of those pushes reject at one branch; after §2.3 they all run the full pass. §7.4's 30 Hz throttle then makes it ≈ one such pass every third block at 512 / 48 kHz, against 2.68 points of headroom (R-12). Both the processor (`processor.cpp:2764-2772`) and the engine (`seraphis_engine.h:791-806`) already flag this arithmetic in writing. | **SC-031** (§10.2) measures whole-`process()` block time with a 30 Hz drag in flight on a held note, worst-of-seven, against `kFullPolyCeilingNs`. If it fails: narrow `spectralRetryMask_` to voices that can still reject, or add a pre-`applySpectralStates` identity check against the last-pushed `spectralSlots_` — **never** raise the ceiling, never drop below C-8's 30 Hz throttle. |
| R-19 | **A re-push body that cannot unmask** | Kinds 2/3 make no engine call by design (§6.2), so `repushPartialOverrides()` is the *only* audio-thread path to `setPartialMask`. A body that walks the **set** mask bits only (the previous draft's) leaves `masked_[i] == true` forever when a bit is cleared, because `masked_[index] = !active` (`harmonic_cloud.h:1084-1089`) is never re-issued. Q5's whole clarification exists to make unmasking possible; no existing criterion looked at the **engine** rather than the processor's own table. | §6.3 walks **all 64 indices** and pushes **both** polarities. **SC-033** (§10.2) masks partial *k*, renders, toggles off, and asserts recovery **on every voice through `engineForTest()`** — a build with the walk-set-bits body fails it and passes everything else. |
| R-17 | **The relaxation is implemented by deleting the counter** | The obvious "clean-up" after removing the two early returns is to drop `rejectedConfigCalls_` as now-dead. It is **not** dead: SC-028 arm (ii) asserts it is *unchanged* across the push, and other `isConfigurable()`-gated callers still increment it. | §2.3 states the keep-list explicitly. A build where `getRejectedConfigureTimeCallCount()` no longer compiles has removed SC-028's observable, not tidied up. |

---

## 13. Build integration and task order

### Files touched

**New — `dsp/`:** none (three functions land inside `spectral_state.h`; two method groups inside
`seraphis_voice.h` / `seraphis_engine.h`; one owner inside `seraphis_macro_matrix.h`).

**New — `plugins/seraphis/src/`:**
`processor/cloud_frame.h`, `ui/edit_message.h`, `ui/cloud_view.{h,cpp}`, `ui/macro_ring_knob.h`,
`ui/drawer_container.{h,cpp}`, `ui/edit_sub_controller.{h,cpp}`.

**New — tests:**
`dsp/tests/unit/systems/spectral_state_authoring_test.cpp` (hosts **both** SC-012's acceptance arm **and**
all four SC-013 audibility arms — §10.1; one file, one CMake entry, no "or a render TU beside …");
`dsp/tests/unit/systems/seraphis_partial_fanout_test.cpp` (**FR-033's fan-outs — a dedicated TU by
phase-owner ruling**, §2/§10.1);
`plugins/seraphis/tests/unit/controller/custom_view_test.cpp`;
`plugins/seraphis/tests/integration/cloud_frame_test.cpp`;
`plugins/seraphis/tests/integration/partial_edit_test.cpp`;
`plugins/seraphis/tests/integration/ui_negative_control_test.cpp`;
`plugins/seraphis/tests/integration/ui_perf_test.cpp`.

**Modified:** `dsp/include/krate/dsp/processors/spectral_state.h`,
`dsp/include/krate/dsp/systems/{seraphis_voice,seraphis_engine,seraphis_macro_matrix}.h`;
`plugins/seraphis/src/{entry.cpp, processor/processor.{h,cpp}, controller/controller.{h,cpp},
parameters/morph_params.h}`; `plugins/seraphis/resources/editor.uidesc`;
`plugins/seraphis/{CMakeLists.txt, CLAUDE.md, CHANGELOG.md}`;
`plugins/seraphis/tests/CMakeLists.txt`; `dsp/tests/CMakeLists.txt`;
`plugins/seraphis/tests/unit/parameter_surface_test.cpp`,
`tests/unit/controller/editor_lifecycle_test.cpp`, `tests/unit/state_v3_test.cpp`,
`tests/unit/lifecycle_test.cpp` (**new to this list** — the `Seraphis_SetActiveDoesNotAllocate` scope
comment that records the connected-instance carve-out, §5.2),
`tests/integration/{effects_chain_test.cpp, param_perf_test.cpp, param_cadence_test.cpp,
effects_perf_test.cpp}`;
`dsp/tests/unit/{processors/spectral_state_test.cpp, systems/seraphis_macro_test.cpp}`.

### CMake edits (both lists are ENUMERATED — FR-050, FR-051)

**Registration pattern (phase-owner ruling, accepted as authored):** *each task that creates a new TU
appends its own file to the relevant enumerated list*, so that TU runs red-then-green from within its own
task rather than waiting for a batch pass at the end. **tasks.md T027 is the single authoritative
audit-and-complete pass** over both `CMakeLists.txt` files — it verifies every file below is present
exactly once and adds anything a task missed. The two rules are complements, not alternatives: without
the per-task append a new test never runs during its own task; without T027 an omission is invisible,
because an unregistered TU makes Catch2 report *"No tests ran"* rather than a failure (R-9).

- `plugins/seraphis/CMakeLists.txt` `smtg_add_vst3plugin` source list (`:17-46`): add
  `src/processor/cloud_frame.h`, `src/ui/edit_message.h`, `src/ui/cloud_view.{h,cpp}`,
  `src/ui/macro_ring_knob.h`, `src/ui/drawer_container.{h,cpp}`,
  `src/ui/edit_sub_controller.{h,cpp}`.
- `plugins/seraphis/tests/CMakeLists.txt`: add the six new test TUs **and** the three new plugin `.cpp`s
  beside the second compilation of `processor.cpp`/`controller.cpp` (`:36-38`) — the test exe compiles
  plugin sources a second time, so a new `.cpp` must appear in **both** lists.
- `plugins/seraphis/tests/CMakeLists.txt` `set_source_files_properties`: add
  `integration/partial_edit_test.cpp` and `integration/ui_negative_control_test.cpp` only (§10.3).
- `dsp/tests/CMakeLists.txt`: add **two** files to the `dsp_systems_tests` list (`add_executable` at
  `:299`) — `unit/systems/spectral_state_authoring_test.cpp` (**the only registration SC-012's acceptance
  arm and all of SC-013 have**) and `unit/systems/seraphis_partial_fanout_test.cpp` (FR-033, the ruled
  dedicated TU). `unit/processors/spectral_state_test.cpp` is already registered to `dsp_processors_tests`
  (`:282`, verified this session) and stays there for SC-012 clauses 1–3; it cannot host SC-013, which
  needs a Layer-3 render, nor the fan-out cases, which need `SeraphisEngine`.
  **No new `dsp/` TU is needed for SC-030** — it is a re-run of the existing `spectral_morph_*` cases
  already in that executable.

### Targets to build and run

```bash
CMAKE="/c/Program Files/CMake/bin/cmake.exe"
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_processors_tests dsp_systems_tests
build/windows-x64-release/bin/Release/dsp_processors_tests.exe 2>&1 | tail -5
build/windows-x64-release/bin/Release/dsp_systems_tests.exe    2>&1 | tail -5

"$CMAKE" --build build/windows-x64-release --config Release --target seraphis_tests Seraphis
build/windows-x64-release/bin/Release/seraphis_tests.exe 2>&1 | tail -5
build/windows-x64-release/bin/Release/seraphis_tests.exe "[.perf]" 2>&1 | tail -20

tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Seraphis.vst3"
node tools/check-portability.js && node tools/lint-layers.js
node tools/lint-float-bit-goldens.js && node tools/lint-arch-guarded-includes.js
./tools/run-clang-tidy.ps1 -Target seraphis -BuildDir build/windows-ninja
```

`dsp_core_tests`, `dsp_primitives_tests` and `dsp_effects_tests` are **not** touched by this phase and
need not be rebuilt. `Seraphis` (the plugin target) must be rebuilt because `entry.cpp`, the uidesc and
six new TUs enter it.

### Task order — this plan's sections mapped onto `tasks.md` T001 – T029

`tasks.md` is the executable ordering; this table is the plan-side index into it, so a reader of either
document can find the other. Each task ends green before the next begins, and each task that creates a TU
registers it in CMake itself (see the pattern box above).

| tasks.md | Plan section | Verify |
|---|---|---|
| T001 | §0 — ODR sweep, green baseline, perf anchors, branch check | 0 matches for every claimed name; three suites green; `kFullPolyCeilingNs`/`kRegressionFactor`/`kBaselineFullPolyNs` and Phase 10's 2 380 980 ns recorded. **The D-1 ruling is now in the spec** — record it as *applied*, not as pending input. |
| T002 | §1 — the three mutators, placed **after `makeFactoryState`'s closing brace**, not after `normalizeSpectralState` (`kAuthorSpacing` aliases `detail::factory::kFillSpacingFactor` at `:344`, and `detail::factory` does not open until `:317`); the four range constants written **`SpectralState::`-qualified**; `setPartial`'s **step 0** `isValidSpectralState` gate; `blendStates`' **step 2a** exact endpoint short-circuit | `dsp_processors_tests` compiles. A build error naming `detail::factory` means the placement rule was skipped; one naming `kMinStateRatio` means the qualification rule was. SC-012 clause 2 must be **red without step 0** (add a row invalid at index 30 and watch it fail) before it is trusted; SC-025's `memcmp` form must be red without step 2a. |
| T003 | §2.1–2.2 — the `SeraphisVoice`/`SeraphisEngine` fan-outs **+ the new dedicated TU** `dsp/tests/unit/systems/seraphis_partial_fanout_test.cpp`, appended to `dsp_systems_tests` in this task | test red first, then `dsp_systems_tests` green |
| **T003a** (new, from the D-1 ruling — fold into T003 or run immediately after) | **§2.3** — the gate relaxation + the `spectral_morph_engine.h:199-206` comment correction | **SC-030 first**: capture Phase 3's `spectral_morph_*` summary **before** the edit, re-run **after**, and require it unchanged. `isConfigurable()`, `rejectedConfigCalls_` and `getRejectedConfigureTimeCallCount()` still compile (R-17). SC-028/SC-029 land later, at T010, once the plugin edit path exists. |
| T004 | §3 — the macro-matrix `Effects` owner (enum, POD, rows, guards, reader) + the additivity case | `dsp_systems_tests` green; `computeAetherTargets()` **bit-identical** to a pre-change reference table; `Count == 29`, `kNumRows == 32` |
| T005 | §10.1 — SC-012's acceptance arm + all four SC-013 audibility arms in `spectral_state_authoring_test.cpp` | red for the right reason, then green |
| T006 – T007 | §5.1, §6.1 — `cloud_frame.h`, `edit_message.h` | compile; `static_assert(sizeof(CloudFrame) == 808)`, `sizeof(EditMessage) == 12` |
| T008 | §5.2–5.3 — handler lifecycle, `publishCloudFrame()` with the **gate as the only short-circuit** (a null handler must NOT suppress the attempt counter or the `pendingFrame_` fill), the **full seam set**, and the `processor.cpp:794-797` activation-allocates comment amendment | SC-006 (incl. arms (g) focus rule and **(i)** FR-011 handler lifecycle, which builds the one peer `IConnectionPoint` double) and SC-007 green **against the reached-the-slice-loop divisor**. If `cloudFramePublishAttemptCountForTest()` reads 0 in the fixture, the ordering rule was skipped — `ProcessorFixture` never calls `connect()`. |
| T009 | §5.4 + §7.1 — controller as `IDataExchangeReceiver`, **all three pure virtuals** plus `notify` | SC-006 arm (h). Controller must be non-abstract or `createInstance` will not compile. |
| T010 | §6.2 — `Processor::notify`, `applyEditMessage` (kinds 2/3 **stage only**), `stageSlotEdit`, `spectralSlotsAuthoring_`, the release/acquire handshake | SC-018, SC-025 green — **and SC-028/SC-029**, which need this path to reach §2.3's relaxed voice |
| T011 | §6.3 — the atomic override table + `repushPartialOverrides()` at all **six** sites (two marked ‡ host-thread), body walking **all 64 indices** and pushing **both** mask polarities, keyed on the **composed** `CloudStereoSpread` | SC-014 arms 1–6; **SC-033** (mask → unmask reaches every voice) — write it **red first** against a walk-set-bits body so the unmask gap is observed, not assumed; arm 7 deferred to T023's `[.perf]` pass |
| T012 | §6.4 — the 272-byte `[partials]` block (D-3) | SC-015 green, incl. the truncated-stream arm |
| T013 | §4 — the composed effects seam (D-2's substituted reads + `baseValueForTarget` cases + `param_cadence_test`'s 27 → 29) | SC-021(a)(b)(c) green in the **ruled** form |
| T014 | §8.2 — `MacroRingKnob`, its creator, `entry.cpp` includes + banner rewrite | plugin links; creator registers |
| T015 | §8.1 — `CloudView`, **including the `renderForTest()` draw seam and the four gesture handlers** | SC-020 arms (f) axis map and (g) masked ring (both through `renderForTest()`, not a bare `draw()`), SC-023, **SC-032** (all four FR-028 gestures, read at `Controller::lastSentEditMessageForTest()`) |
| T016 | §8.3 — `DrawerContainer` as a **direct child of the template root** (D-4) | SC-020(c)(d)(e); `getViewSize()` byte-equal to the two `constexpr` rects |
| T017 | §8.4 — `SeraphisEditSubController`, `createSubController` on the **template root**, `verifyView` session-tag wiring incl. the preset button (D-5), `willClose` reset | SC-022(b), SC-022(d) |
| T018 | §7.2–7.4 — refcount, `slotMirror_`, `morph_params.h` (**both** re-seed sources), throttle + terminal flush | SC-016 both arms, SC-024, SC-026, SC-027 |
| T019 | §9 — `editor.uidesc` replaced wholesale | SC-002, SC-003 (`T` = `{CCheckBox}`), SC-004 arms 1, 2 **and 3** (FR-006 child index 0, FR-025 exactly-one-visible-page, FR-027 Observe-on-open); `unreachableParams(xml, ids, {})` empty |
| T020 – T024 | SC-001, SC-005, SC-008 – SC-011 (**incl. arm 2, the FR-005 platform-API source scan**), SC-014 arm 7, **SC-031**, SC-022(a)(c) | `[.perf]` protocol: fresh boot, **seven** runs, best-of-16, worst reported. SC-022(a) arm 1 is a `static_assert` set and must be *observed to fail* by temporarily adding a fourth `src/ui/` class before it is trusted; SC-011 arm 2's forbidden-token scan must likewise be *observed to fail* by temporarily inserting `#include <windows.h>` behind a guard. **SC-031 is the gate on §2.3** — if it is red, §2.3's remedies (narrow `spectralRetryMask_`, or a pre-push identity check) ship in the same task; the ceiling does not move. |
| T025 | §10.4 — the two pilot measurements | the `.amount` values land inside the **[−20 dB, −6 dB]** band with a strictly monotone sweep; SC-017(a)'s octave figure rounded **down** to two decimals |
| T026 | §11.2 — **five** spec-edit groups, not four: **D-3** (272 B *and* the corrected `writeInt64u` rationale), **D-4**, **D-5**, **D-6**, and **D-9's ten rows 9a–9j** — including 9j, which writes **every** criterion arm §10 adds into spec.md's own Success Criteria with its FR back-reference and the matching Traceability rows. Plus T025's two write-backs. | no "placeholder"/"pilot" text survives into compliance; **every SC named in §10 is findable in spec.md** — an arm that lives only in the plan cannot appear in the compliance table (root `CLAUDE.md`, *Completion Honesty*) and is the first casualty of schedule pressure |
| T027 | §13's CMake audit — the single authoritative pass over both `CMakeLists.txt` files | every new TU present exactly once; suite case counts moved, not just exit 0 |
| T028 | FR-053/FR-054 — `plugins/seraphis/CLAUDE.md` (`ui/` no longer empty; custom-view roster; cloud-frame data path) and `CHANGELOG.md` | `node tools/check-changelog-coverage.js` clean |
| T029 | SC-019 — full suites, pluginval, portability, layer lint, clang-tidy, ASan lifecycle | all clean before commit |

**Registration outside `plugins/`:** none. Seraphis is already in every CI/tooling roster from Phase 8
(roadmap §8.5); Phase 11 adds no new plugin, no new target, and no new workflow entry.

---

## Review notes

### Revision 3 — 2026-08-03, against the second plan review (22 findings)

**All 22 findings were applied; none was rejected.** No threshold was relaxed to resolve one, and where a
finding proved a criterion unsatisfiable by a *correct* implementation the criterion was **re-pointed at
a reachable seam**, never loosened. Two findings (D-3's rationale, and the FR-033 test row's polarity)
proved a *plan* statement factually wrong against a header read this session; nine proved a **spec**
statement wrong or unreachable and are collected as **D-9** in §11.2 with the exact sentence to edit,
rather than silently diverging.

| Finding (severity) | Where resolved |
|---|---|
| **blocker** — the unmask half of the mask gesture could never reach the DSP (`repushPartialOverrides` walked set bits only) | §6.3 body rewritten: all 64 indices, both polarities; cost re-derived; **SC-033** (§10.2); **R-19**; T011 |
| **blocker** — every frame-content/cadence criterion unrunnable (`publishCloudFrame` returned on a null handler before the counter and the frame fill; `ProcessorFixture` never calls `connect()`) | §5.3 — option (a) taken and stated so T008 does not discover it; D-9 row 9e; T008 |
| **major** — `setPartial` failed SC-012 cl. 2 for whole-state-invalid rows | §1.1 **step 0**; D-9 row 9d; T002's red-first gate |
| **major** — kinds 2/3 deferral reversed C-5 cl. 1 and invalidated C-9/FR-042 with no spec edit scheduled | §6.2; D-9 rows 9b, 9c; T026 |
| **major** — ~15 new criterion arms existed in no version of spec.md | D-9 row **9j**; T026's widened scope |
| **major** — SC-023 and SC-020(g) assert on `draw()` but the harness never paints | §8.1 `renderForTest()` seam; §10.2 both rows re-pointed; D-9 row 9g; T015 |
| **major** — three of FR-028's four gestures had no criterion | §8.1 gesture note; §7.4 `lastSentEditMessageForTest()`; **SC-032**; T015 |
| **major** — D-1's relaxation makes every voice run `buildSanitized`; nothing measured it | §2.3's "RT safety. Unchanged." **deleted** and replaced with the derivation + remedies; **SC-031**; **R-18**; T020–T024 |
| **major** — a third raw read of `wanderDepth` at `processor.cpp:1126` decides whether the wander stage engages | §4.2 — **three** substitutions, with the two-block latency this avoids stated |
| **major** — the FR-033 test row asserted the inverse of `setPartialMask`'s real polarity | §10.1 row rewritten; §2.1's polarity box states the convention once |
| **major** — `blendStates(a,b,0) == a` was unsatisfiable, so SC-025 and §1.2's normalisation argument both rested on it | §1.2 **step 2a** exact endpoint short-circuit, with all three reasons named |
| **major** — `IBStreamer` *does* have `writeInt64u`/`readInt64u`; D-3's rationale was a false API fact | §6.4 layout (two 8-byte fields); D-3 restated; §0.3 |
| minor — spec C-4's mask row is polarity-inverted | D-9 row 9a |
| minor — FR-011 had no criterion | **SC-006 arm (i)**; T008 |
| minor — FR-005 / FR-006 / FR-025 / FR-027 had no criterion | **SC-011 arm 2** (forbidden-token scan) and **SC-004 arm 3**; T019, T020–T024 |
| minor — D-3's "no 64-bit accessor" reason was wrong *and* irrelevant to the 272 | D-3 restated as an arithmetic slip + a separate, corrected accessor note |
| minor — SC-013's precondition is unimplementable in the dsp TU it is cited from | D-9 row 9i |
| minor — `setActive` → `onActivate` allocates, contradicting `processor.cpp:794-797` and `lifecycle_test.cpp:768-791` | §5.2; D-9 row 9h; `tests/unit/lifecycle_test.cpp` added to §13's Modified list; T008 |
| minor — "`repushPartialOverrides()` — audio thread only" contradicted two of its own call sites | §6.3 relabelled; the two host-thread sites marked ‡ per-site |
| minor — the four range constants are `SpectralState`-scoped and were used unqualified | §1 placement paragraph; §1.1 steps 5–6; §1.3 step 3; T002 |
| minor — `updateParamSmootherTargets()` does not exist | §4.2 renamed to `setParamSmootherTargets()`, call site recorded as `processor.cpp:1207` |
| minor — SC-007's "once per `process()` call" is false against six early returns | §10.2 SC-007 restated to *reached the slice loop*, divisor `effectsStageProcessCalls_` (`:1189`); D-9 row 9f |

**Still open after this revision, and each still needs a spec edit (T026):** D-3 (272 B + corrected
rationale), D-4 (sub-controller on the template root), D-5 (the preset button's listener owner), D-6
(`cloudFrameEnabled_` is `std::atomic<bool>`), **D-9** (rows 9a–9j).

### Revision 2 — 2026-08-03, after the phase-owner rulings

This revision reconciles the plan with `spec.md` as it stands after the rulings recorded in its
*Clarifications* → *Session 2026-08-03 (phase-owner rulings, plan §11)*. **No threshold moved**, and the
one ruling that changed the build (D-1) *added* work rather than removing it.

| Change | Where |
|---|---|
| D-1 **RELAXED**, not disclosed — the plan's recommended (A) was overruled | new **§2.3**; §10.1 SC-030 row; §10.2 SC-028/SC-029 rows; **R-16**, **R-17**; §13 T003a; §0.1 |
| D-2 accepted with the replacement observable now in the spec | §10.2's SC-021 row rewritten; §11.0 |
| Composition cadence one-block lag accepted as designed | §4.2 ruling box |
| OQ-4 methodology + acceptance band fixed (−20 dB … −6 dB, strictly monotone, write-back before compliance) | §10.4 rewritten |
| The extra `dsp/` fan-out TU accepted | §2 preamble; §10.1 FR-033 row; §13 |
| Per-task CMake registration + T027 as the single audit pass accepted | §13's pattern box |
| D-7 absorbed into the spec (`preOutputTapL/RForTest`) | §11.0 |
| **D-8 (new):** `fundamentalHz` resolves to `HarmonicCloud::getFundamentalHz()` (`:405`) — the previous draft's claim that no accessor exists was wrong, and both candidates the ruling named are unimplementable | **§5.3** rewritten; **§11.1**; §0.2 |
| Every pre-ruling citation re-verified against the headers | §0.4 |
| Task numbering reconciled with `tasks.md` T001 – T029 | §13 |

**Still open as of Revision 2:** D-3, D-4, D-5, D-6. (Revision 3 adds **D-9** and corrects D-3's
rationale; see above. tasks.md T026 owns all five.)

### Revision 1 — 2026-08-03, against the plan review

Revision of 2026-08-03 against the plan review. **All 23 findings were applied; none was rejected**, and
no threshold was relaxed to resolve one. Where a finding proved a *spec* statement wrong rather than a
plan statement, the plan records a `D-n` deviation and names the exact spec edit required, rather than
silently diverging.

| Finding | Where resolved |
|---|---|
| No seam to read a published `CloudFrame` (SC-006/008/014/017 unimplementable) | §5.3 seam set — `lastPublishedFrameForTest()`, `cloudFrameSequenceForTest()`; §10.2 rows re-pointed |
| Kinds 2/3 mutated engine state from the message thread | §2.2 ownership paragraph; §6.2 dispatch + deferral; R-3 |
| Clearing detection keyed on `ParamID` 207, blind to a Bloom sweep | §6.3; SC-014 arms 6 and 7; R-3b |
| `partialPan_` read on the audio thread while plain | §6.3 — `std::array<std::atomic<float>, 64>` |
| `cloudFrameEnabled_` a plain cross-thread `bool` | §5.3, §6.2, D-6, R-3a |
| Drawer nested two levels deep vs FR-023's absolute rects | §8.3, §9, D-4; SC-020(e) |
| Preset button had no listener owner, no criterion | §8.4 table, §9, D-5; SC-022(b)(d) |
| FR-021 had no view-level criterion | §8.2; SC-022(c) |
| FR-014 (focus voice) had no criterion | SC-006 arm (g) |
| FR-016 (receiver) had no criterion | SC-006 arm (h) |
| FR-017 (axis map, masked ring) had no criterion | §8.1 seams; SC-020 arms (f)(g) |
| FR-022 (tab names and order) had no criterion | §8.3; SC-004 arm 2 |
| FR-046's state-stream re-seed had no criterion | SC-016 arm 2 |
| SC-022(a)'s text scan cannot see transitive bases | SC-022(a) — `std::is_base_of_v` + base allowlist tripwire |
| SC-013's host TU unresolved and unregistered; drift precondition in plugin `ParamID`s | §10.1 SC-013(a); §13 CMake edits |
| SC-009(b)/SC-010(b) instrumentation never added | §5.3; §10.2 SC-009 row |
| `preOutputTapForTest()` does not exist | §10.2 SC-021 row; D-7 |
| `entry.cpp`'s conditional `ToggleButton` include | §8.2, §8.3, §9 — `CCheckBox` decided once; `T` stays a singleton set |
| `kAuthorSpacing` referenced `detail::factory` before its declaration | §1 placement paragraph; tasks.md T002 |
| `updatePanGains` costed as two `sqrt` | §2.2 (it is `cos`/`sin`, `crossfade_utils.h:50-53`); SC-014 arm 7 |
| `SpectralState` sized at 1084 bytes (twice) | §1.2 and §6.2 — `sizeof(SpectralState) == 540`; 541 kept only for stream payloads |
| §7.1 omitted the three `IDataExchangeReceiver` pure virtuals and `notify` | §7.1; tasks.md T009 |
