# Tasks: Vorago Phase 2 — Noise Organism

**Spec:** `specs/vorago-phase2-noise-organism/spec.md` (1622 lines)
**Plan:** `specs/vorago-phase2-noise-organism/plan.md` (1691 lines)
**Deliverable:** one new Layer 3 header `dsp/include/krate/dsp/systems/noise_organism.h`, three
additive amendments to shipped components, four new test TUs, three new cases in existing TUs.
**Test targets:** `dsp_systems_tests` (new TUs), `dsp_processors_tests` / `dsp_primitives_tests`
(amendment cases), `dsp_effects_tests` + `membrum_tests` (regression gates).
**Plugin work:** none. Phases 1–10 of the Vorago roadmap are KrateDSP-only.

---

## How to read this file

* Tasks are grouped into **ordered groups**. Groups run in sequence; a group starts only when every
  task in the previous group is green.
* `[P]` marks tasks that are parallel-safe **within their group**: they touch files that are fully
  disjoint from every other task in the same group, and no task in that group edits a file another
  task in the group edits. Every task that edits `noise_organism.h` is unmarked and sits in its own
  group, because that header is the single shared file of this phase.
* Each task is self-contained. It names the exact files, the failing test to write **first** (file,
  `TEST_CASE` name, the numeric assertions), then the implementation intent, then the verification
  command. An executor needs no other context.
* Canonical order inside every task: **failing test → implement → zero warnings → tests pass.**
* No commit tasks. Commits happen outside this workflow.

**Deviation from the requested layout, called out deliberately:** CMake registration is **T001**, not
a final task. `dsp/tests/CMakeLists.txt`'s `dsp_systems_tests` source list is **enumerated, not
globbed** (verified this session — the list runs from `dsp/tests/CMakeLists.txt:306` to `:389` and
carries its own comment saying so at `:360-361`), so an unregistered TU compiles into nothing and its
cases silently never run. Registering the four TUs as one single task up front is the only ordering
under which every later task's "run the suite" step actually proves anything. The final group still
contains a **registration-completeness audit** (T022) that re-verifies the four entries and the
`-fno-fast-math` invariant, plus the full-suite run and the portability check.

### Build and run commands (Windows, full CMake path is mandatory)

```bash
CMAKE="C:/Program Files/CMake/bin/cmake.exe"
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_systems_tests
build/windows-x64-release/bin/Release/dsp_systems_tests.exe 2>&1 | tail -5
build/windows-x64-release/bin/Release/dsp_systems_tests.exe "NoiseOrganism_GuardLadder*" 2>&1 | tail -5
build/windows-x64-release/bin/Release/dsp_systems_tests.exe "[.perf]" 2>&1 | tail -30
```

Catch2 filters: test-case name as a **positional** argument (`"Name*"`), tags in `[brackets]`.
`[long]`-tagged cases run locally by default and are excluded from per-push CI.
**Never** re-run a slow suite just to grep it — redirect to a log on the first run and read the log.

---

## Group A — Test-target registration (blocking, single shared-file task)

### T001 — Register the four new test TUs (`dsp/tests/CMakeLists.txt`) and create them as stubs

**Files to create** (all four, minimal stubs that compile and link — no `TEST_CASE` yet except where
stated):

* `dsp/tests/unit/systems/noise_organism_test.cpp`
* `dsp/tests/unit/systems/noise_organism_spectral_test.cpp`
* `dsp/tests/unit/systems/noise_organism_perf_test.cpp`
* `dsp/tests/unit/systems/noise_organism_nonfinite_test.cpp`

Each stub is exactly:

```cpp
// Vorago Phase 2 (specs/vorago-phase2-noise-organism): <role>
#include <catch2/catch_test_macros.hpp>
```

**File to edit:** `dsp/tests/CMakeLists.txt`

1. In the `add_executable(dsp_systems_tests ...)` source list (starts `:306`, ends at the closing
   `)` after `unit/systems/spectral_state_authoring_test.cpp`), append, with this comment block:

```cmake
    # Vorago Phase 2 (specs/vorago-phase2-noise-organism): NoiseOrganism.
    # This list is ENUMERATED, not globbed - an unregistered TU silently drops
    # out of the build and its cases never run.
    #   noise_organism_test.cpp          SC-003, SC-005(a), SC-006, SC-007, SC-010,
    #                                    SC-013, SC-014, SC-016, SC-017, SC-018, SC-021
    #   noise_organism_spectral_test.cpp SC-001, SC-002, SC-005(b), SC-008, SC-009,
    #                                    SC-019, SC-020   (the [long] set)
    #   noise_organism_perf_test.cpp     SC-004(a)-(e) + the T002 stage probe   [.perf]
    #   noise_organism_nonfinite_test.cpp SC-015 only
    unit/systems/noise_organism_test.cpp
    unit/systems/noise_organism_spectral_test.cpp
    unit/systems/noise_organism_perf_test.cpp
    unit/systems/noise_organism_nonfinite_test.cpp
```

2. In the **single** `-fno-fast-math -fno-finite-math-only` `set_source_files_properties` block (the
   one whose last listed file is `unit/systems/seraphis_nonfinite_test.cpp`, immediately before
   `PROPERTIES COMPILE_FLAGS "-fno-fast-math -fno-finite-math-only"`), add **exactly one** of the
   four, with this comment (FR-097):

```cmake
        # Vorago Phase 2: SC-015 injects NaN/Inf via bit patterns in this TU and
        # needs IEEE semantics to assert on them. ONLY this one of the four Phase 2
        # TUs is listed; the other three must NOT be. noise_organism_test.cpp and
        # noise_organism_spectral_test.cpp stay out so the FR-008 guards are proved
        # in the /fp:fast + -ffast-math mode the header actually ships in. The perf
        # TU must stay out too: -fno-fast-math would change the figures its
        # baselines are pinned to.
        unit/systems/noise_organism_nonfinite_test.cpp
```

**Do NOT** add anything to `dsp/CMakeLists.txt` (the component is header-only) and do **not** touch
`dsp/lint_all_headers.cpp` yet — the header does not exist until T009.

**Verify:**
`"$CMAKE" --build build/windows-x64-release --config Release --target dsp_systems_tests` builds with
zero warnings; `build/windows-x64-release/bin/Release/dsp_systems_tests.exe 2>&1 | tail -5` still
reports the pre-existing suite green.

---

## Group B — CPU measurement gate (blocking; may stop the phase)

### T002 — Stage-cost probe: measure before building anything

Per plan S11 and FR-095/OQ-CPU-POLICY, the budget risk is front-loaded. The estimate spans
~94 000–170 000 ns against a **106 666 ns** ceiling (1 % of one core = one 512-sample block period of
10 666 667 ns ÷ 100), so the reference configuration's feasibility is genuinely uncertain and must be
measured before the component exists.

**File to edit:** `dsp/tests/unit/systems/noise_organism_perf_test.cpp`

**Write the case first** — `TEST_CASE("NoiseOrganism_StageCostProbe", "[.perf]")`. It builds the
sub-components directly (no `NoiseOrganism` yet) at 48 kHz, renders 512-sample blocks,
best-of-25 trials × 500 blocks after 400 warm-up blocks (the
`dsp/tests/unit/systems/atmosphere_engine_perf_test.cpp:22-70` idiom), and measures **each of six
stages standalone**, printing a per-stage ns/512-block table with `UNSCOPED_INFO` / `WARN` so the
figures appear in the run output:

| Stage | Exact shape to measure |
|---|---|
| `NoiseGenerator` | `prepare(48000.0f, 512)`, exactly one `NoiseType` enabled (`Brown`), `process(buf, 512)` |
| `ResonatorBank` | `prepare(48000.0)`, **3** resonators enabled at 70/140/260 Hz, decay 1.5 s, `processBlock(buf, 512)` |
| `TimeVaryingCombBank` **per-sample path** | `prepare(48000.0, 50.0f)`, `setNumCombs(2)`, `setCombDelay` written every 64 samples and **no** `snapSmoothers()` |
| `TimeVaryingCombBank` **hoisted path** | same, but `snapSmoothers()` called after each 64-sample parameter push |
| `StochasticFilter` | `prepare(48000.0, 512)`, `RandomMode::Walk`, `setChangeRate(0.03f)`, `setCutoffOctaveRange(1.0f)`, `processBlock(buf, 512)` |
| dust stand-in loop | per sample: iterate **all 24** `DustGrain`-shaped POD slots with an `if (active)` test, up to 24 `GrainEnvelope::lookup(table, 2048, phase)` calls, times a carrier sample. **Iterate the full 24-slot pool, not a mean-concurrency subset** — that is the shape plan S6.1 renders |

**Assertions:** the case `REQUIRE`s only that every measured figure is > 0 and finite (it is a probe,
not a gate). It then computes and prints the SC-004 (c) projection:
`4 × (NoiseGenerator + ResonatorBank + hoisted combs + StochasticFilter) + 1 × dust` and compares it
to **106 666 ns**.

**Stop-and-surface rule (FR-095, OQ-CPU-POLICY — non-negotiable):** if the projection exceeds
106 666 ns, the executor **halts the phase** and surfaces to the user: the measured per-stage table,
the projection, and the three options from plan S11 (A: a `StochasticFilter` hoisted-path amendment,
est. 15 000–35 000 ns saving; B: a `NoiseGenerator` enabled-only smoother path, currently excluded by
the spec's Non-Goals; C: a cap or budget change). **No implementing agent may lower `kMaxSources`,
`kMaxResonatorsPerSource`, `kMaxCombsPerSource` or `kMaxDustGrains`, raise the 1 % budget, or relax
any threshold.** Under no circumstance may a threshold be relaxed to make a test pass.

**Verify:**
`build/windows-x64-release/bin/Release/dsp_systems_tests.exe "[.perf]" 2>&1 | tee probe.log | tail -30`
— read `probe.log`; do not re-run to grep it.

---

## Group C — Failing tests for the three shared-component amendments

All three tasks edit **different existing** test TUs and are mutually disjoint; no other task in this
group touches those files. All three files are already registered
(`dsp/tests/CMakeLists.txt:167`, `:140`, `:192`) and already sit in the `-fno-fast-math` block
(`:575`, `:568`, `:663`) — **no CMake change is needed for any of them.**

### T003 [P] — Failing test: `NoiseGenerator::setSeed` (FR-080–FR-083, SC-011 (i)–(iv))

**File to edit:** `dsp/tests/unit/processors/noise_generator_test.cpp`
**Test target:** `dsp_processors_tests`

Add `TEST_CASE("NoiseGenerator_SetSeedIsOptInAndReproducible", "[noise_generator][vorago-phase2]")`
with four sections, all rendering 4096 samples at 48 kHz via `prepare(48000.0f, 512)` +
`setNoiseEnabled(NoiseType::White, true)` + `setNoiseLevel(NoiseType::White, -20.0f)` +
`process(buf, 512)`:

* **(i) un-seeded instances are identical.** Two default-constructed, identically prepared
  instances, **no** `setSeed` call ⇒ `max |a[n] − b[n]| == 0.0f` exactly. (This pins the
  `Xorshift32 rng_{12345}` construction seed at `noise_generator.h:593` that FR-082 is built on.)
* **(ii) the historical `reset()` scramble is intact on an un-seeded instance.** Render, `reset()`,
  render, `reset()`, render ⇒ the two post-reset renders differ: `max |a − b| > 1e-3f`.
* **(iii) `setSeed` makes `reset()` reproducible.** `setSeed(0xC0FFEEu)`, render A, `reset()`,
  render B ⇒ `max |A − B| == 0.0f`.
* **(iv) different seeds decorrelate.** `setSeed(1)` vs `setSeed(2)` ⇒ Pearson `|r| ≤ 0.05`.

Also add `REQUIRE(gen.getSeed() == 0xC0FFEEu)` after a `setSeed` call.

Sections (i) and (ii) must **pass before** the amendment (they pin unchanged behaviour); (iii) and
(iv) and the `getSeed` line must **fail to compile / fail** before it. Confirm the compile failure is
"no member named `setSeed`" — the right reason.

**Verify:** `"$CMAKE" --build ... --target dsp_processors_tests` fails to compile on the missing
`setSeed`; record that as the red state.

### T004 [P] — Failing test: `NoiseOscillator` Velvet / RadioStatic (FR-098, SC-011 (v))

**File to edit:** `dsp/tests/unit/primitives/noise_oscillator_test.cpp`
**Test target:** `dsp_primitives_tests`

Add `TEST_CASE("NoiseOscillator_VelvetRadioStaticFixed", "[noise_oscillator][vorago-phase2]")`.
For each of `NoiseColor::Velvet` and `NoiseColor::RadioStatic`, separately: build two oscillators,
`prepare(48000.0)`, `setSeed(4242u)` on both, set one to the colour under test and one to
`NoiseColor::White`, render 8192 samples each with `processBlock`.

Assertions:
* `max |colour[n] − white[n]| > 1e-3f` — the colour is no longer silently identical to White (this
  is the exact defect: `noise_oscillator.h:241-267` has no `case` for either and falls through
  `default:`).
* Velvet-specific: at least **60 %** of samples are exactly `0.0f`, and every non-zero sample has
  `|x| == 1.0f` (± 1e-6f) — the sparse ±1 impulse train property (mirrors
  `noise_generator.h:521-534`).
* RadioStatic-specific: the render is non-silent (RMS > −60 dBFS) and its high-frequency content is
  below White's — assert the mean absolute first difference `mean|x[n] − x[n−1]|` is at most **0.7×**
  White's (a low-passed colour has less sample-to-sample motion).

Both must fail before the fix (the two renders are currently bit-identical to White).

### T005 [P] — Failing test: `ResonatorBank::setFrequency` re-derives Q (FR-099, SC-011 (vi))

**File to edit:** `dsp/tests/unit/processors/resonator_bank_test.cpp`
**Test target:** `dsp_processors_tests`

Add `TEST_CASE("ResonatorBank_SetFrequencyRederivesQ", "[resonator_bank][vorago-phase2]")`:

1. `prepare(48000.0)`; `setEnabled(0, true)`; `setFrequency(0, 200.0f)`; `setDecay(0, 1.0f)`.
2. Excite with a single unit impulse, render 3 s, fit RT60 from the envelope decay: take the 20 ms
   RMS envelope in dB, least-squares fit dB-vs-time over the region from −6 dB to −40 dB below the
   envelope peak, extrapolate to −60 dB. `REQUIRE` the fitted RT60 is within **±25 %** of 1.0 s
   (baseline sanity — this passes before and after the fix).
3. Now `setFrequency(0, 800.0f)` **without** touching `setDecay`, reset state, re-excite, re-fit.
   `REQUIRE` the fitted RT60 is still within **±25 %** of 1.0 s.

Step 3 fails before the fix: `setFrequency` (`resonator_bank.h:328-332`) does not re-derive Q from
`decays_[index]` the way `setDecay` does (`:345-352`), so at 4× the frequency the held Q yields
roughly ¼ the RT60. Confirm the pre-fix failure is a **short** measured RT60, not a NaN — that is
"failing for the right reason".

---

## Group D — Implement the three shared-component amendments

All three tasks edit **different existing** headers and are mutually disjoint.

### T006 [P] — `NoiseGenerator::setSeed` (FR-080, FR-081, FR-082, FR-083)

**File to edit:** `dsp/include/krate/dsp/processors/noise_generator.h`

Add, with the signature copied verbatim from `NoiseOscillator::setSeed`
(`primitives/noise_oscillator.h:109`):

```cpp
/// @brief Seed the PRNG for deterministic, decorrelated instances (Vorago Phase 2, FR-080).
/// Opt-in: an instance that never calls this keeps the historical reset() scramble, so every
/// existing consumer is byte-identical (FR-081).
void setSeed(std::uint32_t seed) noexcept {
    seedLatched_    = true;
    configuredSeed_ = seed;
    rng_.seed(seed);
}
[[nodiscard]] std::uint32_t getSeed() const noexcept { return configuredSeed_; }
```

and two new members `bool seedLatched_ = false; std::uint32_t configuredSeed_ = 12345;` beside
`Xorshift32 rng_{12345}` (`:593`). In `reset()` (`:186-189`) replace the single reseed line with:

```cpp
if (seedLatched_) { rng_.seed(configuredSeed_); }        // reproducible for opt-in callers
else              { rng_.seed(rng_.next() ^ 0xDEADBEEF); } // UNCHANGED historical scramble
```

**Nothing else changes.** `kNumNoiseTypes` stays 13; the `NoiseType` enum, every other signature and
every default are untouched (FR-083).

**Verify:** T003's case green (all four sections); then
`"$CMAKE" --build ... --target dsp_processors_tests dsp_systems_tests dsp_effects_tests membrum_tests`
and run all four exes — every one reports "All tests passed", **with no edits to any existing case**.
The five existing consumers (`systems/character_processor.h`, `systems/tape_machine.h`,
`effects/pattern_freeze_mode.h`, `primitives/noise_oscillator.h`,
`plugins/membrum/src/dsp/exciters/noise_burst_exciter.h`) call no `setSeed`, so all five must be
byte-identical.

### T007 [P] — `NoiseOscillator` renders Velvet and RadioStatic (FR-098)

**File to edit:** `dsp/include/krate/dsp/primitives/noise_oscillator.h`

In `process()`'s colour switch (`:241-267`), add the two missing cases before `default:`:

```cpp
case NoiseColor::Velvet:      return processVelvet(white);
case NoiseColor::RadioStatic: return processRadioStatic(white);
```

Add the two private helpers beside the existing `processPink/Brown/Blue/Violet/Grey`
(`:145-198`):

* `processVelvet(float white)` — reuse the draw already made: `p = (white + 1.0f) * 0.5f`; if
  `p < kVelvetDensityHz / static_cast<float>(sampleRate_)` emit
  `rng_.nextUnipolar() < 0.5f ? +1.0f : -1.0f`, else `0.0f`. Add
  `static constexpr float kVelvetDensityHz = 2000.0f;` with a comment saying the class has no
  density setter and adding one is out of scope. The law mirrors `noise_generator.h:521-534`.
* `processRadioStatic(float white)` — one `Biquad radioLowPass_` member, configured in `prepare()` as
  `configure(FilterType::Lowpass, 5000.0f, 0.707f, 0.0f, sampleRate_)` (the same configuration
  `noise_generator.h:180` uses) and cleared in `resetFilterState()`.

Update the class doc from "six noise colors" to eight (`:29-40`).

**Verify:** T004's case green; then `dsp_primitives_tests`, `dsp_processors_tests`,
`dsp_systems_tests`, `dsp_effects_tests`, `membrum_tests` all green with **no edits to existing
cases**. Every shipped consumer pins a colour other than Velvet/RadioStatic
(`ring_modulator.h:295,298`; Membrum's `noise_body.h`, `click_layer.h`, `clap_exciter.h`,
`feedback_exciter.h`, `noise_burst_exciter.h`), and the one runtime-selectable path,
`noise_layer.h:81`'s `denormColor`, is hard-capped to Brown/Pink/White/Violet
(`noise_layer.h:306-309`) — so the fix must change no shipped output.

### T008 [P] — `ResonatorBank::setFrequency` re-derives Q from the configured decay (FR-099)

**File to edit:** `dsp/include/krate/dsp/processors/resonator_bank.h`

```cpp
void setFrequency(std::size_t index, float hz) noexcept {
    if (index >= kMaxResonators) { return; }
    frequencies_[index] = clampFrequency(hz);
    // Vorago Phase 2 (FR-099): match setDecay (:349) so a drifting frequency no
    // longer silently changes the effective RT60.
    qValues_[index] = rt60ToQ(frequencies_[index], decays_[index]);
    updateFilterCoefficients(index);
}
```

**Verify:** T005's case green; then `dsp_processors_tests`, `dsp_systems_tests`,
`dsp_effects_tests`, `dsp_primitives_tests`, `membrum_tests` all green with no edits to existing
cases. A word-bounded sweep for the exact class `ResonatorBank` (excluding the unrelated
`ModalResonatorBank` declared in the same header) finds no consumer outside
`dsp/tests/unit/processors/resonator_bank_test.cpp` and the compile-only `dsp/lint_all_headers.cpp`
— re-run `grep -rn "\bResonatorBank\b" dsp/ plugins/` to confirm before claiming zero risk.

---

## Group E — Component skeleton

### T009 — `noise_organism.h` skeleton, salt table, non-finite contract, lint registration

**File to create:** `dsp/include/krate/dsp/systems/noise_organism.h`
**Files to edit:** `dsp/lint_all_headers.cpp`; the three organism test TUs (add the include).

**ODR sweep first** (mandatory, run and record):
`grep -rn "\(class\|struct\|enum class\|using\) NoiseOrganism\b" dsp/ plugins/ tools/` and the same
for `NoiseOrganismModel`, `DustGrain`. All must be **0 hits**. `LinearRamp` must be **1 hit** — the
shipped `dsp/include/krate/dsp/primitives/smoother.h:305` primitive, which this component
**reuses**. Declaring a nested type of that name would shadow the library type; do not.

**Header contents (skeleton — every method present, bodies returning documented neutrals):**

1. Banner `// Layer: 3 (Systems)` in the form of `systems/timevar_comb_bank.h:1-10`.
2. Includes, downward only, exactly as plan S1.1:
   `core/db_utils.h`, `core/grain_envelope.h`, `core/pattern_freeze_types.h`, `core/random.h`,
   `primitives/noise_oscillator.h`, `primitives/smoother.h`, `primitives/svf.h`,
   `processors/breathing_modulator.h`, `processors/brownian_drift.h`, `processors/noise_generator.h`,
   `processors/perlin_noise_source.h`, `processors/resonator_bank.h`,
   `processors/stochastic_filter.h`, and the single **same-layer** include
   `systems/timevar_comb_bank.h` carrying the justification comment
   *"L3 - same layer, permitted: tools/lint-layers.js fails only on an UPWARD reach. Precedent:
   systems/continuous_body.h:42 includes this exact header the same way."*
3. `enum class NoiseOrganismModel : std::uint8_t { Direct = 0, FilteredWind = 1, GranularDust = 2,
   MetallicHiss = 3 };` with an **APPEND ONLY, never reorder** banner (FR-011).
4. `class NoiseOrganism` with the constants: `kMaxSources = 4`, `kMaxResonatorsPerSource = 4`,
   `kMaxCombsPerSource = 4`, `kMaxDustGrains = 24`, `kControlChunkSamples = 64`,
   `kDustEnvelopeTableSize = 2048`, `kDefaultWanderRateHz = 0.03f`, `kCombFeedbackCap = 0.9f`,
   `kBreathGainSpan = 0.45f`, `kQWanderSpan = 0.9f`, `kGainRampMs = 50.0f`, `kOutputClamp = 4.0f`.
   Add `static_assert(kControlChunkSamples == 64)` with a comment citing `harmonic_cloud.h:144`.
5. `struct PrepareConfig { std::size_t maxBlockSamples = 2048; float maxCombDelayMs = 50.0f;
   std::size_t numSources = 2; };` (nested, `atmosphere_engine.h:369-376` shape). Callers must use
   **designated initialisers** — no narrowing in brace init (FR-094).
6. Copy deleted, move defaulted (deep buffers; never copy on the RT thread).
7. The **full public API exactly as plan S1.2 lists it** — 3 lifecycle + `processBlock`, 4 slot
   setters, 8 chain setters, 4 model setters, 7 wander setters, 3 breathing/event setters, and the
   24-entry `[[nodiscard]] ... const noexcept` read surface. Bodies return neutrals for now.
8. `struct DustGrain { float phase; float phaseIncrement; float gain; bool active; };` and
   `struct Slot { ... }` exactly as plan S1.3, including `appliedCombDelayMs[]` (which is both the
   S6.3 slew limiter's previous state and the `getCombCurrentDelayMs` read surface).
9. **The `sanitise` helper and its normative neutral table**, reproduced verbatim in the header as a
   comment block (plan S1.2a). `std::clamp` does **not** reject NaN, so every float-taking public
   setter calls, as its *first* statement before any clamp:
   `[[nodiscard]] static constexpr float sanitise(float v, float neutral) noexcept { return detail::isFinite(v) ? v : neutral; }`
   using `detail::isFinite` (`core/db_utils.h:118`) — **never** `std::isnan`/`std::isinf`
   (they fold away under the macOS `-ffast-math` leg). Neutrals: `setSourceLevel` → `-12.0f`;
   `setResonatorAnchor` → the FR-016 anchor for that index (70/140/260/500); `setResonatorDecay` →
   `1.5f`; `setCombTuning` → `60.0f`, `0.35f` **guarded independently**; `setCombFeedback` →
   `0.55f`; `setFilterBaseCutoff` → `800.0f`; `setFilterBaseResonance` → `0.7f`; `setDustGrainMs` →
   `40.0f`; `setDustDensity` → `100.0f`; `setResonatorWander` → `2.0f`, `1/kDefaultWanderRateHz`;
   `setResonatorQWander` → `0.25f`; `setFilterWander` → `1.5f`, `1/kDefaultWanderRateHz`;
   `setFilterResonanceWander` → `0.2f`, `1/kDefaultWanderRateHz`; `setCombWander` → `12.0f`,
   `kDefaultWanderRateHz`; `setWanderRate` → `kDefaultWanderRateHz`; `setSourceBreathing` →
   `kDefaultWanderRateHz`, `0.25f`, `0.3f`; `setSourceWake` → `1.0f`;
   `prepare`'s `sampleRate` → `48000.0` (double overload, `db_utils.h:125`);
   `PrepareConfig::maxCombDelayMs` → `50.0f`.
10. **The compile-time salt table** (plan S2.4), with an APPEND-ONLY banner and the range asserts:

```cpp
static constexpr std::size_t kSaltNoiseGen      = 0;    // + slot
static constexpr std::size_t kSaltDustCarrier   = 16;   // + slot
static constexpr std::size_t kSaltChainFilter   = 32;   // + slot
static constexpr std::size_t kSaltResonatorLane = 48;   // + slot*kMaxResonatorsPerSource + index
static constexpr std::size_t kSaltFilterCutoff  = 64;   // + slot
static constexpr std::size_t kSaltFilterReso    = 80;   // + slot
static constexpr std::size_t kSaltCombLane      = 96;   // + slot*kMaxCombsPerSource + index
static constexpr std::size_t kSaltBreathing     = 112;  // + slot
static constexpr std::size_t kSaltNextFree      = 128;
static_assert(kSaltResonatorLane + kMaxSources * kMaxResonatorsPerSource <= kSaltFilterCutoff, "...");
static_assert(kSaltCombLane + kMaxSources * kMaxCombsPerSource <= kSaltBreathing, "...");
static_assert(kSaltBreathing + kMaxSources <= kSaltNextFree, "salt table overflow");
```

   `TimeVaryingCombBank` takes **no salt** and gets a comment saying why: it exposes no `setSeed` and
   hard-seeds `ch.rng.seed(12345u + i * 7919u)` (`timevar_comb_bank.h:466`, repeated at `:487`), so
   FR-042 pins `setModDepth`/`setRandomModulation` at their `0.0f` library defaults (`:414`, `:416`)
   and all comb motion comes from the salted Perlin lanes.
11. Out-of-range `slot`/`index`: setters are silent no-ops, getters return the documented neutral
    (`0.0f`, `Direct`, `Brown`, `false`) — the `resonator_bank.h:329` idiom.

**`dsp/lint_all_headers.cpp`:** add, after the Seraphis Phase 7 block that ends at `:175`
(`#include <krate/dsp/systems/seraphis_macro_matrix.h>`):

```cpp
// Vorago Phase 2 (specs/vorago-phase2-noise-organism), FR-001
#include <krate/dsp/systems/noise_organism.h>
```

This list is **enumerated** (166 explicit includes; the Layer 3 block runs `:149-175`) and picks up
nothing automatically — without this line `./tools/run-clang-tidy.ps1 -Target dsp` gives the new
header zero coverage and SC-012's clang-tidy gate passes vacuously.

**Test (write first):** in `dsp/tests/unit/systems/noise_organism_test.cpp`, add
`TEST_CASE("NoiseOrganism_ControlSurfaceClamps", "[noise_organism]")` — its group (iv) is
implementable now against the skeleton, the rest after later groups:
* (iv) Every setter called with `slot == kMaxSources` and with `index == kMaxResonatorsPerSource` is
  a silent no-op — no getter's value changes anywhere — and every getter called out of range returns
  the documented neutral: `0.0f` for float getters, `NoiseOrganismModel::Direct`,
  `NoiseColor::Brown`, `false`, `0` for the size getters.

Groups (i)–(iii) are added in T015/T014 and are listed there.

**Verify:** builds with zero warnings; `node tools/lint-layers.js` exits 0; `node tools/lint-odr.js`
exits 0; `node tools/check-portability.js` exits 0; `dsp_systems_tests` green.

---

## Group F — Lifecycle

### T010 — `prepare` / `reset` / `setSeed` / `applyConfiguration`, guard ladder, absolute control grid

**Files to edit:** `dsp/include/krate/dsp/systems/noise_organism.h`,
`dsp/tests/unit/systems/noise_organism_test.cpp`
**Test target:** `dsp_systems_tests`

**Write these cases first (they must fail):**

`TEST_CASE("NoiseOrganism_GuardLadder", "[noise_organism]")` — SC-017, each clause measured against
an **uninterrupted reference render of the same instance**, so "no state advanced" is asserted and
not merely the output:
* (a) `processBlock(nullptr, 512)` writes nothing **and advances nothing**: the render that follows
  is `max |diff| == 0.0f` against the reference at the same absolute sample position.
* (b) `processBlock(out, 0)` leaves `out` untouched and consumes no control step — proved the same
  way.
* (c) `processBlock` before `prepare()` fills **exactly** `numSamples` zeros (assert the sample past
  the end still holds its sentinel) and advances nothing.
* (d) 100 000 samples in **one** call equal the same 100 000 rendered as **195 × 512 + one 160-sample
  block** (`195 × 512 = 99 840`, `+160 = 100 000`), `max |diff| == 0.0f`. Note: the spec's
  "196 × 512" is 100 352 — 352 samples longer — so use the 195+160 partition.
* (e) **Overwrite arm** (FR-003): pre-poison the output buffer with a non-zero sentinel
  (`0x7F7F7F7F` bit pattern via `std::memset`), render with slot 0 dormant and, separately, with all
  `kMaxSources` slots dormant; **every** returned sample must be exactly `0.0f`, never the sentinel.

`TEST_CASE("NoiseOrganism_BlockSizeInvariance", "[noise_organism]")` — SC-016: render 240 000 samples
(5 s @ 48 kHz) three ways from three **freshly prepared and identically seeded** instances: (i) one
call; (ii) 469 calls of 512; (iii) the repeating irregular cycle 36, 28, 1000, 1, 511, 2048.
`REQUIRE` all three buffers identical, `max |diff| == 0.0f`. Same binary, same process — not a
stored golden.

`TEST_CASE("NoiseOrganism_PrepareFootprint", "[noise_organism]")` — SC-014:
`getAllocatedBytes() ≤ 640 KiB` after `prepare(48000.0, PrepareConfig{})`; cross-check against the
formula recomputed in the test:
`kMaxSources × kMaxCombs(8) × nextPowerOf2(trunc(48000 × 0.050) + 1) × 4 B + 2048 × 4 B`
= `4 × 8 × 4096 × 4 + 8192` = **532 480 B**; the header's documented figure must match within 5 %.
Separately, `AllocationScope` (`tests/test_helpers/allocation_detector.h:111`) over `prepare` counts
**≤ 64** allocations.

`TEST_CASE("NoiseOrganism_NoAllocationAfterPrepare", "[noise_organism]")` — SC-003, first pass
(re-verified again at T017 with the complete setter set): `AllocationScope` around 20 000 blocks of
512 that also call `reset()`; `REQUIRE` the count is **exactly 0**.

**Implement (plan S2):**
* `prepare(double, const PrepareConfig&)` — the **only** allocator. Order matters:
  `sampleRate_ = std::max(1.0, sanitise(sampleRate, 48000.0))`; clamp `maxBlockSamples` to
  `[64, 8192]`, `maxCombDelayMs` to `[5, 200]`, `numSources` to `[1, kMaxSources]`;
  `rampSamples_ = max<size_t>(1, lround(kGainRampMs * 0.001 * sampleRate_))`;
  `GrainEnvelope::generate(dustEnvelope_.data(), 2048, GrainEnvelopeType::Hann)`
  (`core/grain_envelope.h:33`) **once**; prepare **all** `kMaxSources` slots (not only the active
  ones — a later `setNumSources` must not need a re-prepare), with the **single narrowing cast** here
  and nowhere else: `generator.prepare(static_cast<float>(sampleRate_), config_.maxBlockSamples)`
  (`noise_generator.h:135` takes a `float`); reset every configuration member to the FR-016 defaults;
  `applyConfiguration()`; `clampEngagements_ = 0`, `controlPhase_ = 0`; configure and snap both
  `LinearRamp`s; accumulate `allocatedBytes_`; `prepared_ = true`; then **re-apply
  `setSeed(seed_)` LAST** — `NoiseGenerator::prepare` ends with `reset()` (`:182`) which scrambles
  the RNG (`:189`), so seeding earlier is discarded.
* `reset()` — **configuration-preserving** (FR-004). Reset every sub-component and lane, clear
  grains/`grainCursor`/`sourceRmsSmoothed`/duck FSM, `controlPhase_ = 0`, `clampEngagements_ = 0`,
  then **mandatorily** `applyConfiguration()` and `setSeed(seed_)`, then snap both ramps.
  The re-apply is load-bearing: `ResonatorBank::reset()` is a **configuration wipe** — 440 Hz,
  default decay, unity gain, default Q and `enabled_[i] = false` (`resonator_bank.h:226-232`, doc at
  `:212`) — so a forwarded reset without re-application renders **silence** on every slot.
* `applyConfiguration()` (private) — the one routine `prepare`, `reset` and every
  configuration-changing setter call. Pushes per slot exactly plan S2.3: the single enabled
  `NoiseType` at `kSourceReferenceDb + kSourceDriveDb[activeType]` (**never** `levelDb` — the level
  is carried by the mix-stage `levelRamp` alone), `setMasterLevel(0.0f)`, carrier colour,
  `resonators.setEnabled(i, i < numResonators)` for all 16 plus `setFrequency`+`setDecay` **once**
  per enabled resonator, comb count/delays/feedback/gain/damping + `snapSmoothers()`, the filter's
  seven pinned settings, `setDepth(1.0f)` on every lane, `setMean(0.0f)` on the **`BrownianDrift`
  lanes only** (`PerlinNoiseSource` and `BreathingModulator` declare no such setter — it would not
  compile).
* `processBlock` with the plan S5.1 loop and the **residual `controlPhase_` counter** — an
  **absolute** 64-sample grid. This deliberately differs from `HarmonicCloud`, whose loop is
  block-relative and would run *two* control steps for a 36+28 split where an unsplit 64 runs one;
  copying it verbatim fails SC-016.
* `getAllocatedBytes()` accumulated in `prepare` from the lengths requested.
  `AllocationDetector` has no byte accounting (`allocation_detector.h:83-89`; the operator-new
  replacements discard `size`), which is why the component reports its own sizing.
* At this stage `renderChunk` may write silence — SC-016 is asserted on silence here and re-asserted
  on real audio at T011.

**Verify:** the four cases green; zero compiler warnings; `dsp_systems_tests` green.

---

## Group G — `Direct` slot end to end

### T011 — Source → resonators → combs (+`snapSmoothers`) → filter → gain

**Files to edit:** `noise_organism.h`, `dsp/tests/unit/systems/noise_organism_test.cpp`

**Write first:**

`TEST_CASE("NoiseOrganism_BoundedShort", "[noise_organism]")` — SC-005 (a), **untagged** (per-push
lane; CLAUDE.md forbids tagging a NaN/Inf sentinel `[long]`). Fixture: SC-004 configuration (d) — 4
slots × 4 resonators × 4 combs — every wander depth at maximum and the fastest rate, comb feedback at
`kCombFeedbackCap = 0.9`, `setResonatorDecay(30.0f)` (`kMaxDecayTime`, which saturates `rt60ToQ` at
`kMaxResonatorQ` for every anchor), Q-wander depth `1.0`, wake/dormant toggled by a seeded
pseudo-schedule. Render **60 s**. Assertions: every sample finite via an IEEE-754 **exponent-field**
test (`detail::isFinite`, `core/db_utils.h:118`) — never `std::isnan`; peak `< 4.0f`;
`getClampEngagementCount() == 0`; no 1 s window below **−60 dBFS**.

Extend `NoiseOrganism_BlockSizeInvariance` (T010) to run on the now-audible `Direct` configuration —
`max |diff| == 0.0f` must still hold.

**Implement (plan S5.3, S6.1–S6.4):**
* `renderChunk(out, n)`, `n ≤ 64`. **Step 0, before the slot loop and unconditionally:**
  `std::fill_n(out, n, 0.0f)`; every slot then accumulates with `+=`. There is no "first slot writes
  with `=`" case — under that rule a dormant or dropped slot 0 leaves the caller's buffer untouched
  and the FR-074 tail would scale-and-clamp host garbage. The shipped idiom is identical
  (`timevar_comb_bank.h:761-763`).
* Per slot, using the two shared organism-level 64-sample scratch buffers (slots render
  sequentially, so no per-slot buffers): (1) source → `scratchA`; (1b) accumulate the slot's own
  sum-of-squares for `getSourceRms` **here, on the slot's own data** — not in `updateControl`, where
  `scratchA_` still holds the previous chunk's *last* slot; (2) `if (numResonators > 0)
  resonators.processBlock(scratchA, n)` **else skip the call entirely** — with nothing enabled,
  `process()` returns `input×mix + wetSum×(1−mix)` with `wetSum == 0` and `exciterMix_ == 0`
  (`resonator_bank.h:511`, `:589`), i.e. **silence, not bypass**; (3)
  `if (numCombs > 0) combs.processBlock(scratchA, scratchB, n)` else copy — `setNumCombs` floors at 1
  (`timevar_comb_bank.h:502`) so a forwarded 0 leaves one comb running; (4)
  `filter.processBlock(scratchB, n)` in place; (5)
  `out[s] += scratchB[s] * levelRamp.process() * breathGain * gate.process() * modelTrimGain`.
* Tail: scale by `1/sqrt(kMaxSources)`, clamp to `±kOutputClamp`, increment `clampEngagements_`
  (saturating) on any engagement, flush denormals with `detail::flushDenormal`
  (`core/db_utils.h:245`).
* Resonator control step (plan S6.2) — **`setQ` is the sole per-step writer of `qValues_`**;
  `setDecay` is called once at configuration time and never again, because `setDecay` and `setQ`
  write the same variable (`resonator_bank.h:349` vs `:383`) and alternating them destroys one with
  the other. Per enabled resonator: `driftedHz = clampFreq(sanitise(anchorHz[i] * exp2(semis*d/12), anchorHz[i]))`,
  `targetQ = clamp(sanitise(rt60ToQ(driftedHz, decaySeconds) * qFactor, rt60ToQ(driftedHz, decaySeconds)), kMinResonatorQ, kMaxResonatorQ)`,
  then `setFrequency`, `setQ`, `setGain(i, resonatorMakeupDb(targetQ))` in that order.
  `setSpectralTilt` stays at exactly `0.0f` and is not exposed (at non-zero tilt `calculateTiltGain`
  costs a `std::log2` + a `dbToGain` per resonator **per sample**, `:120-126`, called at `:504`; at
  zero it early-returns `1.0f`). `setDamping` and `setExciterMix` stay at their defaults.
* Comb control step (plan S6.3) — the organism computes the inharmonic law itself,
  `f[n] = combFundamental * sqrt(1 + n * combSpread)`,
  `combBaseDelayMs[n] = clamp(1000/f[n], 1.0f, config_.maxCombDelayMs)`; per step apply the wander,
  the S6.3 slew bound, `setCombDelay(n, targetMs)`, store into `appliedCombDelayMs[n]`, then
  **`combs.snapSmoothers()` once per slot per control step**.
  **`snapSmoothers()` is load-bearing, not an optimisation:** `processBlock` takes the hoisted path
  only when `modDepth_ == 0` **and** every smoother reports `isComplete()` (`:728-741`); writing
  `setCombDelay` every control step keeps the 20 ms delay smoother (`kDelaySmoothingMs`, `:109`)
  permanently unsettled, pinning the bank to the per-sample path — ~99 000 ns/512-block for 8 combs
  against a 106 666 ns budget, versus ~30 700 ns hoisted. The bank's own doc endorses this usage
  (`:352-357`) and `ContinuousBody` is the shipped precedent.
  Use the conservative fallback `kMaxCombDelayStepSamples = 0.25f` for now; **T016 replaces it with
  the measured value.**
* Chain filter control step (plan S6.4): `hz = clamp(sanitise(base * exp2(oct * c), base), 20.0f, 0.45f*sr)`,
  `q = clamp(sanitise(baseQ * (1 + resWander * r), baseQ), SVF::kMinQ, SVF::kMaxQ)`, then
  `setBaseCutoff(hz)`, `setBaseResonance(q)`.

**Verify:** `NoiseOrganism_BoundedShort` and `NoiseOrganism_BlockSizeInvariance` green; zero
warnings; `dsp_systems_tests` green.

---

## Group H — Wander lanes

### T012 — Lanes, the FR-069 rate mapping, `setWanderEnabled`

**Files to edit:** `noise_organism.h`, `dsp/tests/unit/systems/noise_organism_test.cpp`

**Write first:**

`TEST_CASE("NoiseOrganism_DormantLanesFreewheel", "[noise_organism]")` — SC-010, four arms:
* (a) **Source and lane freewheeling.** Render slot 0 dormant for 60 s then wake it; independently
  render the same slot awake for 60 s with its gate applied *after* the chain. Compare **read-surface
  trajectories, not samples**: `getResonatorCurrentFrequency`, `getResonatorCurrentQ` and
  `getFilterCurrentCutoff` sampled every control step agree to within **1e-5 relative** across the
  whole 60 s; and `getSourceRms(0)` over the first **250 ms after wake** agrees within **0.5 dB**.
* (b) **Post-wake chain settle, on statistics not samples.** After
  `tSettle = max(decay, 8 × maxCombDelayMs / (1 − combFeedback))` = **1.5 s** at the FR-016 defaults
  (decay 1.5 s, 16.7 ms base delay, feedback 0.55), the dormant-then-woken render agrees with the
  always-awake render on RMS within **±1.0 dB** and on each of the five band-energy fractions within
  **±0.05** absolute, over the following 10 s. **No sample-identity clause** — the awake arm's
  biquads carry up to 30 s of ringing and its delay lines hold `maxCombDelayMs` of past audio, so a
  `kSampleTolerance` clause would fail on a correct implementation.
* (c) **Per-slot RMS aliasing arm.** `numSources = 2`, slot 0 = White at −6 dB, slot 1 = Brown at
  −24 dB; `REQUIRE` `getSourceRms(0)` and `getSourceRms(1)` differ by the expected offset within
  **±1.5 dB**. This fails if the RMS is computed in `updateControl` from the shared `scratchA_`;
  arm (a) alone cannot see that, because both of its arms alias identically and the aliasing vanishes
  entirely at `numSources == 1`.
* (d) **FR-066 freeze-then-restore arm.** Render 30 s with `setFilterWander(slot, 0.0f, s)`, then
  restore the depth; `REQUIRE` the `getFilterCurrentCutoff` trajectory after restore matches an
  always-on reference within **1e-5 relative**, and — negatively — that the first post-restore sample
  is **not** the base cutoff. Without it, an implementation that skips `lane.processBlock(64)`
  whenever a span is 0 (a plausible CPU saving) passes every other case, because (a) proves
  freewheeling only under *dormancy*, a different branch.

`TEST_CASE("NoiseOrganism_QWanderAudible", "[noise_organism]")` — SC-021. The lane extreme is
**unreachable through the public API** (`b` is a free-running OU output; `setWanderEnabled(false)`
also yields `qFactor = 1`), so measure off the applied state: one resonator, all other depths 0,
`setResonatorQWander(1.0f)` at the default (slowest) rate, **fixture anchored at 70 Hz with
`setResonatorDecay(1.0f)`** giving `rt60ToQ(70, 1.0) = π·70/ln1000 ≈ 31.8` — comfortably below
`kMaxResonatorQ = 100`, so the downward factor is fully observable (at the FR-016 default decay of
1.5 s the top anchors already saturate). Render **600 s** (20 τ at τ = 30 s); sample
`getResonatorCurrentQ` per control step; take the two 10 s segments with the highest and lowest mean
Q; measure each segment's −3 dB bandwidth around the peak. `REQUIRE` (i) the observed Q ratio between
the segments is **≥ 3**; (ii) the measured bandwidth ratio matches the observed Q ratio within
**±25 %**; (iii) both segments render non-silent.

**Implement (plan S7):**
* Lanes advance in `updateControl` by exactly `kControlChunkSamples` — `lane.processBlock(64)` —
  **even when their depth is 0 and even when the slot is dormant** (FR-066, FR-071).
  Fixed-size advances are mandatory: `BreathingModulator::processBlock(n)` advances phase **once per
  call** (`breathing_modulator.h:208-215`) and is therefore *not* partition-invariant;
  `BrownianDrift`/`PerlinNoiseSource` are bit-identical under any partitioning
  (`brownian_drift.h:191`, `perlin_noise_source.h:286`) but use the same 64 for uniformity.
* Read each lane once with `getCurrentValue()`, guard with `detail::isFinite`, substitute `0.0f`.
* `setWanderRate(float hz)` — the FR-069 single scalar, clamped `[0.01, 100]`
  (`StochasticFilter::kMinChangeRate/kMaxChangeRate`):
  **the three `BrownianDrift` lane kinds need a seconds→normalised conversion, because
  `BrownianDrift::setSmoothness(float)` takes a NORMALISED `[0,1]` argument, not a tau in seconds**
  (`brownian_drift.h:149-153`; the internal mapping is `tau = kTauMin + s·(kTauMax − kTauMin)`).
  The spec's literal "forward the tau" wording is wrong at every rate but the default. Use:
  `tau = clamp(1.0f/hz, kTauMin, kTauMax)` then
  `s = (tau − kTauMin)/(kTauMax − kTauMin)`, clamped `[0,1]`, forwarded to `setSmoothness`.
  Perlin: `combLane[n].setRate(hz)` (clamps `[0.005, 5]`). Filter: `filter.setChangeRate(hz)`.
  Breathing: `breathing.setRate(hz)` (clamps `[0.01, 0.5]`). At `kDefaultWanderRateHz = 0.03` the
  Brownian lanes sit at the **clamp ceiling** (30 s tau, since `1/0.03 = 33.3 > kTauMax`) — a
  deliberate, documented consequence. Per-lane `smoothnessSeconds` arguments use the same seconds
  domain and the same conversion; precedence is **last-writer-wins**, with `prepare`/`reset`
  re-applying `setWanderRate(wanderRateHz_)` as the baseline.
* Every lane runs at `setDepth(1.0f)` permanently; the organism's own span constants
  (`resWanderSemis`, `cutoffWanderOct`, `resonanceWander`, `combWanderPct`, `resQWander`) scale the
  `[-1,+1]` output, so a span of 0 freezes the parameter while the lane keeps advancing.
* `setWanderEnabled(bool)` (default `true`) multiplies every external span by 0 **and** calls
  `setCutoffRandomEnabled(false)` on every slot's `StochasticFilter`. Both halves are required:
  the filter's internal randomiser defaults to **on** (`stochastic_filter.h:555`) at
  `kDefaultChangeRate = 1.0f` over a 2-octave range (`:103`, `:112`), so zeroing the external depths
  alone leaves a fast spectral wander running with no control arm reachable.
  It deliberately does **not** touch breathing (FR-068 enumerates FR-061…FR-067 only) — the SC-002
  (b) fixture zeroes breathing itself; do not change shipped behaviour to suit a test.
* `getResonatorCurrentFrequency` / `getResonatorCurrentQ` / `getFilterCurrentCutoff` /
  `getCombCurrentDelayMs` return the applied-state echo fields written by the control step.

**Verify:** both cases green; zero warnings; `dsp_systems_tests` green.

---

## Group I — Gain chain, breathing, duck FSM

### T013 — `levelRamp`, breathing affine map, gate, and the change-detected coalescing duck

**Files to edit:** `noise_organism.h`, `dsp/tests/unit/systems/noise_organism_test.cpp`

**Write first:**

`TEST_CASE("NoiseOrganism_ModelChangeContinuity", "[noise_organism]")` — SC-018, five arms:
* **Duck present and shaped.** 100 `setSourceModel` changes and 100 `setSourceNoiseType` changes at
  random block offsets; `getSourceGain(slot)` sampled on the control grid is monotone in each
  direction and the **total 0–100 % duration is 50 ms ± 5 ms**.
* **Naive-path arm.** With the duck removed (a locally built variant or a temporary flag in the
  test), the 25 ms-frame envelope `maxΔ` across a `setSourceNoiseType` change **exceeds** the SC-009
  (b) bound. Without this arm the criterion could pass with no duck at all. The reason it clicks:
  each type is gated on `if (noiseEnabled_[idx])` (`noise_generator.h:388` … `:568`), so disabling a
  type removes a full-amplitude broadband contribution on the very next sample while
  `updateLevelTarget`'s ramp to zero (`:578-584`) never gets to run.
* **Coalescing arm, no-op writes.** Write the *same* effective `setSourceNoiseType` value **1000
  times** in succession; `REQUIRE` `getSourceGain(slot)` never leaves `1.0` (± 1e-6). A
  parameter-echoing host must not arm the duck.
* **Coalescing arm, pre-swap.** A second genuine change written while the **Down** leg is still
  running ⇒ the trajectory shows exactly **one** 50 ms duck, not two back-to-back (100 ms of
  near-silence).
* **Lost-write arm.** Write a second genuine change at a **randomised offset anywhere inside the
  whole 50 ms duck — Up leg included** — and `REQUIRE`, once the trajectory settles, that
  `getSourceModel`/`getSourceNoiseType` equal the **last** value written and the render is
  non-silent. A change arriving during the Up leg legitimately costs a second duck, so this arm
  asserts the *final state*, not the duck count.
* **FR-012 remembered-type arm.** `setSourceNoiseType(0, NoiseType::Pink)` →
  `setSourceModel(0, MetallicHiss)` → `setSourceModel(0, Direct)` ⇒ `REQUIRE`
  `getSourceNoiseType(0) == NoiseType::Pink` and the render is non-silent.

`TEST_CASE("NoiseOrganism_NoZipperUnderDrift_GainDomain", "[noise_organism]")` — SC-009 (a) (the
`[long]` envelope arm lands in T021). `getSourceGain(slot)` sampled every control step across 100
randomised: full-range `setSourceLevel` steps, `setSourceDormant` toggles, `setSourceWake(0 → 1)`
transitions, type+model changes, **and `setNumSources` reductions and increases**. Assertions:
monotone through each ramp (no overshoot, no reversal); **0–100 % duration 50 ms ± 5 ms**; and the
slot dropped by a `setNumSources` reduction ramps **monotonically to exactly 0.0f** over 50 ms ± 5 ms
— FR-010/FR-072's only gate, since SC-003 exercises `setNumSources` for allocation counting only.
Note the convention: a linear-in-gain ramp of 0–100 % duration D has a 10–90 % duration of `0.8 D`
(40 ms here) — **every ramp assertion in this phase is stated in 0–100 % terms**;
`getSourceLevel` (the configured target) and `getSourceRms` (a smoothed output level) are **not**
substitutes for `getSourceGain` (the applied smoothed gain).

Add SC-001 (d)'s breathing-bound arm here as a section (its 10-minute sibling lands at T021):
with level and wake held fixed, `getSourceGain(slot)` sampled every control step is **strictly
positive, never zero, never sign-changing**, and stays inside `[1 − 0.45·depth, 1 + 0.45·depth]`.

**Implement (plan S8):**
* **Reuse `Krate::DSP::LinearRamp`** (`primitives/smoother.h:305`) — two per slot, `levelRamp` and
  `gate`. Do **not** declare a nested type of that name (it would shadow the library type). Its law
  is constant-**duration** despite a class doc that says "constant rate":
  `increment = delta / (rampTimeMs · 0.001 · sampleRate)`, recomputed on every `setTarget`
  (`smoother.h:100-108`, `:342-354`) — exactly the fixed-50 ms-regardless-of-step-size law the
  criteria need. It also brings two guards a hand-rolled POD would drop: `setTarget` neutralises
  NaN → 0 and saturates Inf → ±1e10 under `ITERUM_NOINLINE` so the check survives `/fp:fast`, and
  `process()` flushes denormals and clamps overshoot (`:379-386`).
  Surface: `configure(rampTimeMs, sr)` `:329`, `setTarget` `:342`, `getCurrentValue` `:364`,
  `process()` `:370`, `isComplete()` `:409`, `snapTo(v)` `:421`.
* `appliedGain(slot) = levelRamp.getCurrentValue() × breathGain × gate.getCurrentValue()`, and
  `getSourceGain(slot)` returns **exactly that**. `modelTrimGain` is deliberately excluded from it
  (it is a calibration property, not a gain the caller set, and `kModelTrimDb[Direct] == 0` keeps the
  no-op arm exact). Both ramps advance **per sample**, never held across the control chunk — a
  1.33 ms staircase on the one signal whose monotonicity is a criterion is not acceptable.
* Breathing (FR-070): `b = clamp(breathing.getCurrentValue(), -1, 1)` — the modulator is **bipolar**
  `[-1, +1]` (`breathing_modulator.h:103`, `getSourceRange()` at `:227-229`), so a bare multiply
  would invert the slot on every exhale. Use the affine map
  `breathGain = 1.0f + kBreathGainSpan * breathDepth * b` (`kBreathGainSpan = 0.45f`): exactly `1.0`
  at `b == 0`, inside `[0.55, 1.45]` for every legal depth, ±0.92 dB at the default `depth = 0.25`.
  `BreathingModulator::setDepth` stays at its library `1.0f` (`:112`) — the organism owns depth and
  forwarding it too would square it.
* Gate: `gateSteady(slot) = (dormant || slot >= numSources) ? 0.0f : wakeAmount`. Any change to
  `dormant`, `wakeAmount` or `numSources` does `gate.configure(kGainRampMs, sr)` then
  `gate.setTarget(gateSteady)`.
* **Duck FSM** (per slot, three states `Idle/Down/Up`), plan S8.3:
  on `setSourceModel`/`setSourceNoiseType`, apply FR-012's `ModulationNoise → TapeHiss` substitution
  **first**, then **change-detect**: a write equal to the current *effective* value is a full no-op
  (does not arm the duck, does not touch `NoiseGenerator`). A genuine change sets the pending target;
  if `duckState` is `Idle` **or `Up`** it re-arms (`Down`, `gate.configure(kGainRampMs * 0.5f, sr)`,
  `gate.setTarget(0.0f)`); if it is `Down` the pending target is simply updated and the ramp is
  **not** restarted (coalescing).
  **Re-arming from `Up` is mandatory**: without it a change arriving during the Up leg sets
  `duckPending` but the swap condition never fires again and the write is silently *lost*, while the
  getters report the old value — and "exactly one duck" would still pass.
  The swap happens on the exact sample where `gate.getCurrentValue() == 0.0f`, not at the next
  control-step boundary, so the total duration is exactly `rampSamples_`: disable the old type,
  set `activeType`/`model`, push
  `setNoiseLevel(activeType, kSourceReferenceDb + kSourceDriveDb[activeType])` — **the identical
  constant expression as `applyConfiguration`, with no `levelDb` term** — enable the new type, apply
  the model configuration, then `duckState = Up`, `gate.configure(kGainRampMs * 0.5f, sr)`,
  `gate.setTarget(gateSteady)`.
* `setSourceLevel` does **not** arm the duck (it changes no generator state) and never touches
  `generator.setNoiseLevel`; it does `levelRamp.configure(kGainRampMs, sr)` then
  `levelRamp.setTarget(dbToGain(clamp(sanitise(dB, -12.0f), -96.0f, 12.0f)))`.
* **Dormancy chain-skip is gated on the gate ramp reaching zero, not on the flag.** While the gate is
  above 0 or still moving the chain runs, so `setSourceDormant(slot, true)` fades the chain output
  over 50 ms; only once the gate lands on exactly 0 does the slot take the cheap path (source and
  lanes keep running — FR-071's "source runs, chain skipped").

**Verify:** both cases green; zero warnings; `dsp_systems_tests` green.

---

## Group J — Composed models: filtered wind and metallic hiss

### T014 — `FilteredWind` and `MetallicHiss`

**Files to edit:** `noise_organism.h`, `dsp/tests/unit/systems/noise_organism_spectral_test.cpp`,
`dsp/tests/unit/systems/noise_organism_test.cpp`

**Write first:**

`TEST_CASE("NoiseOrganism_MetallicHissInharmonicity", "[noise_organism]")` (spectral TU) — SC-020:
20 s isolated `MetallicHiss` slot, **comb-delay wander depth set to 0**; magnitude spectrum
(`tests/test_helpers/spectral_analysis.h`); locate the comb peaks and take their ratios to the lowest
peak. `REQUIRE` (a) at least **3** peaks are found; (b) **every** ratio deviates from the nearest
integer by at least **4 %**; (c) the measured peak frequencies match the organism's own
`f[n] = fundamental × sqrt(1 + n × spread)` within **3 %**.
`getTuningMode()` is deliberately **not** asserted — `setCombDelay` sets `tuningMode_ = Tuning::Custom`
(`timevar_comb_bank.h:515`) on the first control step, so an `Inharmonic` assertion would fail on a
correct implementation.

Add to `NoiseOrganism_ControlSurfaceClamps` (main TU) group (iii):
`setCombFeedback(slot, 0.99f)` ⇒ `getCombFeedback(slot) == kCombFeedbackCap == 0.9f` exactly (FR-090
— the organism's own cap, below `TimeVaryingCombBank`'s higher limit).

**Implement:**
* `FilteredWind`: base type pinned to `NoiseType::Brown`; chain filter set to
  `setBaseFilterType(SVFMode::Bandpass)`, `setCutoffRandomEnabled(true)`,
  `setCutoffOctaveRange(2.0f)`, `setSmoothingTime(400.0f)`, `setChangeRate(wanderRateHz_)`.
  Everything else is the default configuration; **no new DSP**.
* `MetallicHiss`: base type pinned to `NoiseType::Blue`, or `NoiseType::Violet` when
  `setHissBright(slot, true)`; comb feedback default 0.75 (still under `kCombFeedbackCap`);
  the organism computes the inharmonic ratios itself (already in T011) — it does **not** rely on
  `Tuning::Inharmonic`, because `setCombDelay` unconditionally forces `Tuning::Custom`
  (`timevar_comb_bank.h:189` doc, assignment `:515`) and FR-063 writes `setCombDelay` every control
  step. `setModDepth` and `setRandomModulation` are **never called** — they stay at their `0.0f`
  library defaults (`:414`, `:416`); the bank has no `setSeed` and hard-seeds every per-comb PRNG
  `12345u + i*7919u` (`:466`, `:487`), so its internal motion would be bit-identical across slots.
  Leaving them at 0 is also what keeps the hoisted path reachable (`modDepth_ <= 0` is the first
  hoist condition, `:728`). **No `FrequencyShifter`** (out of scope, FR-042).
* `setCombTuning(slot, fundamentalHz, spread)` — `fundamentalHz` clamped
  `[kMinResonatorFrequency, sampleRate × kMaxResonatorFrequencyRatio]`, `spread` clamped `[0, 1]`,
  each `sanitise`d **independently**; `setCombFeedback(slot, f)` clamped `[0, kCombFeedbackCap]`.
  Matching getters. **No `setNoteFrequency` / key-follow** — Phase 3 owns that idiom.
* Model changes go through the T013 duck.

**Verify:** SC-020 green; the clamp arm green; zero warnings; `dsp_systems_tests` green.

---

## Group K — Composed model: granular dust

### T015 — `GranularDust` pool, concurrency clamp, FR-036 gain

**Files to edit:** `noise_organism.h`, `dsp/tests/unit/systems/noise_organism_spectral_test.cpp`,
`dsp/tests/unit/systems/noise_organism_test.cpp`

**Write first:**

`TEST_CASE("NoiseOrganism_ModelRosterAndDustLevel", "[noise_organism][long]")` (spectral TU) —
SC-019. **(b) is implementable now; (a)'s ±3 dB window becomes meaningful only after T016's
calibration** — write both arms now and expect (a)'s window to fail until T016.
* **(a) 15 cells, not 48.** FR-012 makes type selection effective only for `Direct` slots (the
  composed models pin their own base type), so 36 of the 48 type×model cells are duplicates of 3
  distinct renders. Walk the **12 selectable `NoiseType`s on a `Direct` slot** plus the **3 composed
  models**. Each cell: 5 s isolated slot; RMS **> −60 dBFS** *and* within **±3 dB** of the White
  reference. Plus: `setSourceNoiseType(slot, NoiseType::ModulationNoise)` snaps to
  `NoiseType::TapeHiss` through `getSourceNoiseType`; a **bare** `NoiseGenerator` with only
  `ModulationNoise` enabled and a zero sidechain renders **exactly** `0.0f` (the verified fact the
  exclusion rests on — `noise_generator.h:553-558`); and a slot with **0 resonators and 0 combs**
  renders > −60 dBFS (this is the arm that catches an implementation that forwards the zero counts).
  **Level-ownership arm:** render one slot at `setSourceLevel(-24)` and at `setSourceLevel(-12)`,
  everything else identical; `REQUIRE` the absolute RMS difference is exactly the requested **12 dB,
  ±0.5 dB**. This is the only arm that catches a double-applied slot level (generator *and* mix
  stage), which would move the render 24 dB — the ±3 dB-vs-White comparison cannot, because that
  error is uniform across all types.
* **(b) Dust level across density.** Sweep `setDustDensity` over **100 / 400 / 1600 / 6400 / 20 000
  imp/s**, each with an **explicitly requested `setDustGrainMs(40.0f)`** (the FR-016 default —
  requesting the 200 ms maximum would let the FR-035 ceiling pin concurrency at `kMaxDustGrains` for
  every point and the criterion would pass without exercising the gain law at all; at 40 ms the
  ceiling only binds above ~600 imp/s). `REQUIRE` slot RMS varies by at most **6 dB** peak-to-peak
  with **no adjacent step above 3 dB**. **Plus, at the 20 000 imp/s ceiling** — where mean grain
  concurrency equals `kMaxDustGrains` and instantaneous concurrency exceeds it about half the time —
  the 25 ms-frame envelope `maxΔ` must stay inside the SC-009 (b) bound, proving the largest-phase
  steal policy rather than asserting it.

Add to `NoiseOrganism_ControlSurfaceClamps` (main TU) groups (i) and (ii):
* (i) `setDustCarrierColor(slot, NoiseColor::Velvet)` ⇒ `getDustCarrierColor(slot) ==
  NoiseColor::Brown` — rejected on **musical-design** grounds (Velvet is the impulsive colour used as
  the grain *trigger*, unsuitable as a continuous carrier), **not** because of the `NoiseOscillator`
  fallthrough, which T007 fixed. `NoiseColor::RadioStatic` is now **accepted** — assert
  `getDustCarrierColor` reports it.
* (ii) `setDustDensity(slot, 20000.0f)` then `setDustGrainMs(slot, 200.0f)` ⇒
  `getDustDensity(slot) == 20000.0f` **and** `getDustGrainMs(slot) == 1.2f` (the FR-035 ceiling
  `1000 × kMaxDustGrains / density`), i.e. the clamp is bidirectional (density first, grain length
  second) and both effective values are readable rather than silent. Also assert that at the
  100 imp/s floor the ceiling is 240 ms so the whole requested `[5, 200]` ms range is honoured.

**Implement (plan S6.1):**
* Trigger: the slot's `NoiseGenerator` with **only** `NoiseType::Velvet` enabled. Velvet contributes
  `±1 × velvetGain` at impulse samples and **exactly `0.0f`** between them
  (`noise_generator.h:521-534`), so `trigger[s] != 0.0f` is an exact impulse detector consuming no
  extra RNG stream.
* Carrier: one `NoiseOscillator` per slot, seeded per the salt table, colour `dustColor`. Reuse
  `scratchB` for the carrier buffer (the comb stage has not run yet) — no third buffer.
* Grain pool: `std::array<DustGrain, kMaxDustGrains>` per slot. `acquireGrain()` — **prefer a free
  slot** (scan forward from `grainCursor`); only when all 24 are live, **steal the largest-phase
  grain** (nearest its own Hann zero, so the truncation step is the smallest available). An
  unconditional ring overwrite would truncate a live Hann envelope at an arbitrary non-zero value.
* Grain gain (FR-036): at birth,
  `gain = (trigger[s] > 0 ? +1 : -1) * dustGrainGain` where
  `dustGrainGain = 1 / sqrt(max(1, expectedConcurrency))` and
  `expectedConcurrency = dustDensityEffective * dustGrainMsEffective / 1000`, recomputed **only** on
  a density or grain-length change. **In-flight grains keep their birth gain and birth
  `phaseIncrement`.** The velvet trigger's random polarity supplies the sign — free decorrelation of
  overlapping grains at no extra state or RNG draw. Continuity at birth/death is structural: the Hann
  table starts and ends at exactly 0.
* Per sample: `scratchA[s] = carrier[s] * Σ(GrainEnvelope::lookup(table, 2048, g.phase) * g.gain)`;
  advance `g.phase += g.phaseIncrement`; retire at `phase >= 1`.
* Effective-value clamp, on every density/length write, **density first**:
  `dustDensityEffective = clamp(requested, 100, 20000)`;
  `grainCeilingMs = 1000 * kMaxDustGrains / dustDensityEffective`;
  `dustGrainMsEffective = min(clamp(requestedGrainMs, 5, 200), grainCeilingMs)`;
  then `generator.setVelvetDensity(dustDensityEffective)` (`noise_generator.h:315`, which itself
  floors at 100 — which is why the clamp must land on the grain length, not the density).
* `setDustGrainMs` changed while grains are in flight: in-flight grains keep their original
  `phaseIncrement` and finish normally; no grain is truncated by a parameter change.

**Verify:** SC-019 (b) green; SC-019 (a)'s non-silence and snap arms green (its ±3 dB window may
still fail — T016 closes it); the two clamp groups green; zero warnings.

---

## Group L — Calibration pass

### T016 — Measure `kSourceDriveDb`, `kModelTrimDb`, the FR-018 Q make-up fit, and the comb slew bound

**Files to edit:** `dsp/tests/unit/systems/noise_organism_perf_test.cpp` (the hidden
`[.calibration]` case), `noise_organism.h` (transcribe the measured constants)

These four tables are **measured, not authored**. A placeholder fails SC-019 (a)'s ±3 dB arm and
SC-001 (c)'s window. Add
`TEST_CASE("NoiseOrganism_MeasureSourceDrive", "[.calibration]")` — hidden, run on demand — that
produces all four and prints them in copy-pasteable form.

1. **`kSourceDriveDb[kNumNoiseTypes]` (FR-017).** For each of the 13 `NoiseType`s: a bare
   `NoiseGenerator`, `prepare(48000.0f, 512)`, `setSeed(1)`, only that type enabled at its own
   `kDefaultLevelDb` (`noise_generator.h:106` = −20 dB), render 5 s; take
   `extractAudioFeatures(render, 48000.0).rmsDbfs` (`tests/test_helpers/audio_features.h:37`);
   `kSourceDriveDb[type] = rmsDbfs(White) − rmsDbfs(type)` (White is the 0 dB reference — flat
   spectrum, no shaping).
   **The case must `REQUIRE`, for every selectable type, that
   `kSourceReferenceDb + kSourceDriveDb[t]` lands inside
   `[NoiseGenerator::kMinLevelDb, kMaxLevelDb] = [−96, +12]`** and fail loudly with the measured
   value otherwise. Silently clamping would push that type outside SC-019 (a)'s window with no
   explanation. Headroom check: `kSourceReferenceDb = NoiseGenerator::kDefaultLevelDb = −20.0f`, so
   the allowance is 32 dB; Velvet is the worst case at roughly **+22 dB**
   (`RMS ≈ peak·sqrt(density/fs) = sqrt(100/48000)` ⇒ ~26.8 dB below peak, versus white's ~4.8 dB).
2. **`kModelTrimDb[4]` — the composed models need their own constant, which `kSourceDriveDb`
   structurally cannot reach.** `kSourceDriveDb` is a per-`NoiseType` offset applied at
   `NoiseGenerator::setNoiseLevel`, and three of the four models do not take their level from there:
   `GranularDust` uses only the velvet impulse *sign* and derives level from carrier RMS × Hann
   window energy × `dustGrainGain`; `FilteredWind` adds a 2-octave bandpass and `MetallicHiss` a
   0.75-feedback comb, whose chain gains neither FR-017 nor FR-018 compensates. Measure
   `kModelTrimDb[model] = rmsDbfs(Direct/White reference slot) − rmsDbfs(model)` at the FR-016
   defaults (for `GranularDust`, at the default 100 imp/s), apply it at the **mix stage** alongside
   `levelRamp` — *not* at the generator, which the dust path bypasses. `kModelTrimDb[Direct] == 0`
   exactly.
3. **`resonatorMakeupDb(float q)` (FR-018).** A bandpass with constant 0 dB peak gain admits a
   broadband source's power in proportion to `ENBW ∝ f0/Q`, so slot RMS falls as `1/sqrt(Q)`. Fit
   `makeupDb(Q) = kMakeupSlope · log10(Q / kQRef)` — analytic starting point `kMakeupSlope = 10.0`,
   `kQRef = rt60ToQ(anchor[0], 1.5 s)` — by least squares on dB-vs-`log10(Q)` over
   `Q ∈ {1, 3, 10, 30, 100}` at each FR-016 anchor. Clamp the result at `kMakeupCeilingDb`, chosen so
   a `kMaxResonatorQ = 100` resonator (which FR-064's downward-only factor guarantees is the *quiet*
   end) cannot push a slot above SC-001 (c)'s −3 dBFS ceiling. Apply via
   `ResonatorBank::setGain(i, makeupDb)` (`:364`) on the same control step **immediately after
   `setQ`** — `setGain` writes only the gain and does **not** recompute filter coefficients
   (`:364-369`), so it costs ~1 `dbToGain` per resonator per control step.
4. **`kMaxCombDelayStepSamples` (the S6.3 slew bound) is produced, not chosen.** Sweep
   per-control-step delay steps at the worst case (feedback at `kCombFeedbackCap = 0.9`,
   `numCombs = 4`, base delay at the FR-016 default) and record the **largest** step whose
   25 ms-frame envelope `maxΔ` stays inside the SC-009 (b) bound. `0.25 samples` is the conservative
   *starting point* of the sweep and the fallback if the measurement cannot be made — it is **~32×
   tighter** than the unlimited trajectory at the fastest legal setting (50 % depth, 5 cells/s
   Perlin, ~8 samples/step), so shipping it unmeasured would turn the comb lane into a near-static
   parameter. Store it as a **distance in samples** and convert:
   `kMaxCombDelayStepMs = kMaxCombDelayStepSamples / sampleRate_ * 1000`, so the bound is
   sample-rate independent (SC-008 (c) depends on that).

**Transcribe** all four into `noise_organism.h` as `static constexpr` values, each with the
**measurement method recorded verbatim beside it** and the measurement date. The `[.calibration]`
case stays checked in — it is what makes a guessed table impossible to ship.

**Verify:** SC-019 (a)'s ±3 dB window and its level-ownership arm now green; SC-001 (c)'s
(−60, −3] dBFS window green (its 10-minute sibling runs at T021); re-run the SC-002 comb-excursion
arm at T021 with the measured bound.

---

## Group M — Determinism, decorrelation, and the full setter sweep

### T017 — SC-006, SC-007, SC-003 (complete), SC-005 (a) re-verify

**Files to edit:** `noise_organism.h` (fixes only), `dsp/tests/unit/systems/noise_organism_test.cpp`

**Write first:**

`TEST_CASE("NoiseOrganism_SeedDeterminism", "[noise_organism]")` — SC-006:
* Two instances, same seed, same configuration, same sample rate ⇒ **identical** 10 s renders,
  `max |diff| == 0.0f`.
* A third instance seeded differently ⇒ `|Pearson r|` against the first **≤ 0.05**.
* (a) With **no setter called since `prepare(sr, cfg)`**, `reset()` reproduces the exact
  post-`prepare` stream (`max |diff| == 0.0f`).
* (b) **After configuration changes** (non-default resonator count, anchors, comb tuning), `reset()`
  re-applies that configuration: the render immediately after `reset()` is **non-silent** and matches
  a fresh `prepare()` followed by the same configuration calls, **sample-exact** — *not* the FR-016
  defaults. This is the arm that catches an implementation that forwards `reset()` to
  `ResonatorBank`/`TimeVaryingCombBank`/`StochasticFilter` without re-applying configuration, which
  renders silence (`resonator_bank.h:226-232`'s `enabled_[i] = false`).
* `setSeed(0)` is legal — assert it renders non-silent (`deriveStreamSeed` substitutes
  `0x2545F491u` for a zero hash, `random.h:112`; `Xorshift32::seed` substitutes its own default for
  0, `random.h:44-45`).
* Two organisms with the same seed but different `numSources`: slots `0..n−1` produce identical
  streams — salts are indexed by slot, not by active count.

`TEST_CASE("NoiseOrganism_SourceDecorrelation", "[noise_organism]")` — SC-007: four **identically
configured** slots (same model, same noise type, same chain), differing only in salt; isolate each
with `setSourceDormant(other, true)` — which contributes **exactly** zero.
**Do not use `setSourceLevel(-96)`**: it leaves a residual that floors the measurable correlation.
`REQUIRE` all six pairwise `|r| ≤ 0.05` over 10 s at 48 kHz.
**Anti-vacuity control arm, built in-process:** instantiate two bare `NoiseGenerator` objects,
prepare identically, call **no** `setSeed`, render both, `REQUIRE |r| > 0.99` (they share
`Xorshift32 rng_{12345}`, `noise_generator.h:593` — this is exactly FR-082's +12 dB coherent-sum
hazard); then call `setSeed(deriveStreamSeed(seed, salt))` on each with distinct salts and
`REQUIRE |r| ≤ 0.05` for the same pair.

**Complete `NoiseOrganism_NoAllocationAfterPrepare`** (SC-003): the setter enumeration is **not**
hand-written in the test. Write one helper
`void touchEverySetter(NoiseOrganism&, std::size_t block)` that calls **every setter declared in the
header's public API, in declaration order**, once per block, so a setter added by a later phase is
covered by construction. 20 000 blocks of 512 plus `reset()`, all inside an `AllocationScope`;
`REQUIRE` the count is **exactly 0**. (A hand-written list previously omitted `setSourceBreathing`,
`setResonatorAnchor`, `setResonatorDecay`, `setFilterBaseCutoff`, `setFilterBaseResonance`,
`setDustCarrierColor` and `setHissBright` — several of which trigger `applyConfiguration()`, the
routine most likely to reach a sub-component allocation path.)

**Verify:** all three cases green; `dsp_systems_tests` green; zero warnings.

---

## Group N — Render pin

### T018 — SC-013 render fingerprint with a measured tolerance and an injected-defect proof

**Files to edit:** `dsp/tests/unit/systems/noise_organism_test.cpp` (+ header fixes only if needed)

`TEST_CASE("NoiseOrganism_RenderFingerprint", "[noise_organism]")`:
* `fingerprintRender` (`tests/test_helpers/render_fingerprint.h`) over a **30 s** render of the
  SC-004 (c) reference configuration at 48 kHz with the seed pinned.
* Aggregate metrics compared within the shared `kMetricTolerance` (`render_fingerprint.h:61`,
  `2.5e-4`).
* Checkpoint samples compared within a **measured per-comparison sample tolerance**, passed
  explicitly as `compareFingerprints`' `sampleTolerance` argument (`:124-126`), derived from a
  three-toolchain probe (MSVC, `g++ -O3 -ffast-math`, `clang++ -O2`) of this exact render and
  recorded in `compliance.md`. **The shared `kSampleTolerance = 5.0e-4f` (`:58`) is NOT loosened for
  this caller** — the header's own banner mandates exactly this treatment for a "STORED golden of a
  trajectory-accumulating render (drift, mutation, chaotic modulators)" (`:116-121`), and its
  measured trajectory-bearing spread is already `sample 3.73e-4` / `metric 9.37e-5` (`:31-36`),
  within a factor of 1.3 of the shared bound *before* this component's OU walks, Perlin lattice,
  velvet Poisson triggers and 0.75-feedback combs are added.
* **Acceptance requires showing the test can fail.** Temporarily inject the defect "the comb wander
  lane salt collides with the resonator lane salt" (`kSaltCombLane = kSaltResonatorLane`) and
  demonstrate the case goes red at the loosened bound; then revert. A fingerprint that cannot fail is
  not a pin, and a loosened bound that cannot fail is worse than none. Record the injected-defect
  demonstration in `compliance.md`.
* **No bit-exact float golden anywhere.** `node tools/lint-float-bit-goldens.js` must stay green.

**Verify:** case green; the injected-defect demonstration recorded; lint green.

---

## Group O — Non-finite input handling

### T019 — SC-015 in the `-fno-fast-math` TU

**File to edit:** `dsp/tests/unit/systems/noise_organism_nonfinite_test.cpp` (already registered by
T001, and the **only** new TU in the `-fno-fast-math -fno-finite-math-only` block)

`TEST_CASE("NoiseOrganism_NonFiniteSetterInputs", "[noise_organism]")` — untagged (per-push lane):
* Call **every** public float-taking setter with NaN, +Inf and −Inf, each **built from bit patterns
  through a `volatile` sink** — never `std::numeric_limits<float>::quiet_NaN()`/`infinity()`, which
  fold to finite garbage on the macOS `-ffast-math` leg. Then render a 1 s block.
* (a) Each non-finite argument is replaced by **the neutral named in the header's normative table**
  (T009 item 9) and read back through the read surface — e.g.
  `setSourceWake(slot, NaN)` ⇒ `getSourceWakeAmount(slot) == 1.0f`;
  `setSourceLevel(slot, NaN)` ⇒ `getSourceLevel(slot) == -12.0f`;
  `setCombTuning(slot, NaN, 0.6f)` ⇒ `getCombFundamental(slot) == 60.0f` **and**
  `getCombSpread(slot) == 0.6f` (the two arguments are guarded independently).
* (b) Every rendered sample is finite, tested on the IEEE-754 **exponent field** — never
  `std::isnan`. This is a real trace, not a formality: `std::clamp` returns NaN **unchanged** (with
  `v = NaN`, both `v < lo` and `hi < v` are false), so a clamp-only setter would admit NaN into
  configuration state → `setResonatorAnchor(NaN)` → NaN drifted frequency → `ResonatorBank::setFrequency`
  clamps with a bare `std::clamp` (`:539`) → `BiquadCoefficients::calculate` clamps with another
  (`biquad.h:155`) → NaN coefficients → `Biquad::process` resets only on a non-finite *input sample*
  (`biquad.h:354`), never on non-finite coefficients, so the resonator emits NaN forever, and
  FR-074's output clamp is itself a `std::clamp` and propagates it.
* (c) A rejected value must not perturb state: post-injection RMS within **±0.5 dB** of an uninjected
  reference, and every *other* getter unchanged from its pre-injection value.

**Verify:** case green on MSVC; then run `node tools/check-portability.js` (exit 0) and confirm the
TU still appears exactly once in the `-fno-fast-math` block and its three siblings do not.

---

## Group P — CPU baselines

### T020 — SC-004 (a)–(e) with both compile-time clauses

**File to edit:** `dsp/tests/unit/systems/noise_organism_perf_test.cpp`

`TEST_CASE("NoiseOrganism_CpuBudget", "[.perf]")` — ns per 512-sample block at 48 kHz, best-of-25
trials × 500 blocks after 400 warm-up blocks (the `atmosphere_engine_perf_test.cpp:22-70` idiom).
One 512-block period is 10 666 667 ns, so 1 % is `kReferenceNsPerBlock = 106666`.

Configurations, each with its own checked-in baseline:
* **(a)** default — 2 slots, `Direct`, 2 resonators + 2 combs each.
* **(b)** 4 slots, `Direct`, 3 resonators + 2 combs each.
* **(c)** **reference** — 4 slots, one each of `Direct`/`FilteredWind`/`GranularDust`/`MetallicHiss`,
  3 resonators + 2 combs each, everything else at the FR-016 defaults, dust at **100 imp/s × 40 ms**
  (mean concurrency 4 of 24, ~17 %, so steal-oldest is a genuine backstop and not the normal path).
* **(d)** **out-of-region** — every cap maxed (4 slots × 4 resonators × 4 combs, all `GranularDust`
  at the FR-035 concurrency ceiling). Regression-tracked against **its own baseline only**, not
  gated against the 1 % reference — the `atmosphere_engine_perf_test.cpp:44-50` convention. It sits
  deliberately outside the in-region envelope FR-095 names (≤ 4 slots × 3 resonators × 2 combs, ≤ 1
  dust slot, dust concurrency ≤ 50 % of the FR-035 ceiling), which (a)–(c) sit inside.
* **(e)** **all-dormant** — 4 slots configured as (c) but every slot `setSourceDormant(true)`:
  measures the residual cost of "source runs, chain skipped". Tracked against its own baseline, and
  additionally asserts a **measured saving vs (c)** so FR-071's dormancy claim is a number.

(a), (b) and (c) are gated at `baseline × 1.5` and each carries **two different** compile-time
clauses (`atmosphere_engine_perf_test.cpp:34-42`):

```cpp
static_assert(kBaselineX * kRegressionFactor <= kReferenceNsPerBlock, "baseline exceeds the 1% budget");
static_assert(kBaselineX >= kReferenceNsPerBlock / 50.0,             "baseline looks like a no-op run");
```

The floor is **not** a restatement of the ceiling — its documented purpose is catching "a baseline
recorded from a no-op or misconfigured run", and without it a baseline taken from an un-`prepare`d
organism (which fills silence and advances nothing) compiles and passes forever. Do not write the
same inequality twice.

**If (c) misses 106 666 ns:** FR-095 / OQ-CPU-POLICY's **stop-and-surface** applies — halt, and
surface the measured ns/512-block figure plus the per-stage breakdown (source, resonators, combs,
`StochasticFilter`, dust grains) from T002's probe. **No implementing agent may lower any cap, raise
the budget, or relax the threshold.** Precedent (context, not permission): Seraphis's
`AtmosphereEngine` went 1 % → 1.5 % by an explicit user call.

**Verify:** `dsp_systems_tests.exe "[.perf]" 2>&1 | tee perf.log | tail -30`; read the log.

---

## Group Q — The `[long]` spectral set

### T021 — SC-001, SC-002, SC-005 (b), SC-008, SC-009 (b)

**File to edit:** `dsp/tests/unit/systems/noise_organism_spectral_test.cpp`

All four cases below are tagged `[long]` (multi-minute renders whose assertions are
toolchain-independent). SC-005 (a)'s finiteness sentinel already lives untagged in the main TU
(T011) — never tag a NaN/Inf guard `[long]`.

`TEST_CASE("NoiseOrganism_LongRenderStationarity", "[noise_organism][long]")` — SC-001. **10 minute**
mono render at 48 kHz in the SC-004 (c) reference configuration, **every setting exactly as the
FR-016 defaults table states it** — nothing inherited implicitly from a library default. 60 windows
of 10 s, `extractAudioFeatures(window, 48000.0).rmsDbfs`:
(a) every window within **±3.0 dB** of the median (`statistical_utils.h`'s median);
(b) the least-squares slope of window RMS (dB) vs time within **±0.5 dB per 10 minutes**;
(c) no window below **−60 dBFS** and none above **−3 dBFS**;
(d) the FR-070 breathing factor sampled from `getSourceGain(slot)` with level and wake fixed is
strictly positive, never zero, never sign-changing, and inside `[1 − 0.45·depth, 1 + 0.45·depth]`.

`TEST_CASE("NoiseOrganism_SpectralMotion", "[noise_organism][long]")` — SC-002. `T = 1/r` where `r`
is the FR-069 wander-rate scalar; at the default `r = 0.03` Hz, `T = 33.3 s`. Render **≥ 10·T =
350 s**. Every 100 ms extract the five band-energy fractions (`audio_features.h:28-29`); per band,
mean-remove, compute the normalised autocorrelation, take the lag `L` of its first zero crossing,
**capped at 0.25 × the record length** (an unmeasurable lag is a **failure**, not a coin flip).
* (a) **wander on, FR-016 defaults** — at least **three of five** bands have `L ∈ [0.4·T, 3.0·T]`.
* (b) **control arm** — `setWanderEnabled(false)` **plus `setSourceBreathing(slot, rate, 0.0f, irr)`
  on every slot.** Both are required: `setWanderEnabled` deliberately does not touch the FR-070
  breathing lane, and at the default `depth = 0.25` breathing is ±0.92 dB per slot — by design the
  dominant contributor to broadband level variation — so the arm would not be static without it.
  (Zeroing breathing inside `setWanderEnabled` is the rejected alternative: it changes shipped
  behaviour to suit a test.) Assert **every** band has `L < 0.4·T` and the broadband RMS CV is at
  least **3× below** the wander-on arm's CV. Note the direction: with no wander the band-fraction
  estimates are stationary plus estimator noise, so `L ≈ 0.1 s`, far *below* `T`.
* (c) broadband RMS coefficient of variation across the wander-on windows **≤ 0.06** while at least
  one band's energy-fraction CV is **≥ 0.10** — the motion is spectral, not level.
* (d) **comb-lane excursion arm.** 120 s at maximum `setCombWander` depth and rate; sample
  `getCombCurrentDelayMs(slot, n)` per control step; `REQUIRE` the realised peak-to-peak excursion is
  **≥ 25 %** of the configured span (`2 × 0.01 × combWanderPct × combBaseDelayMs[n]`, doubled for the
  bipolar lane). 25 % is the floor below which the control misrepresents itself to the user. This is
  the arm that makes T016's measured slew bound's cost **visible** instead of letting it silently
  freeze the lane. **Record the realised percentage in `compliance.md` whether it passes or not.**
* **Record both SC-002 (b) CV figures in `compliance.md`** so the 3× clause is shown reachable rather
  than assumed.

`TEST_CASE("NoiseOrganism_BoundedSoak", "[noise_organism][long]")` — SC-005 (b). **30 minute** render
on the T011 fixture (configuration (d), all depths max, fastest rate, feedback 0.9, decay 30 s,
Q-wander 1.0, seeded wake/dormant schedule). All of SC-005 (a)'s thresholds, plus: RMS of the final
minute within **±6 dB** of the first minute after the initial 30 s settle.

`TEST_CASE("NoiseOrganism_SampleRateInvariance", "[noise_organism][long]")` — SC-008. 60 s renders at
**44 100 / 48 000 / 96 000 / 192 000 Hz**, same seed, FR-016 configuration:
(a) overall RMS within **±1.0 dB** across rates;
(b) every sample finite (exponent-field test) and no 1 s window below −60 dBFS at any rate;
(c) the SC-002 first-zero-crossing lag `L` of the strongest-moving band agrees across the four rates
within **±15 %**, and the measured **0–100 %** duration of a `setSourceWake(0 → 1)` ramp read from
`getSourceGain` is **50 ms ± 5 ms** at every rate. (The spec's "10–90 %" wording at this site is
wrong on a correct implementation: a linear-in-gain ramp of 0–100 % duration 50 ms has a 10–90 %
duration of 40 ms. All three ramp criteria in this phase use the single 0–100 % wording.)
(d) a mid-render `prepare()` at a new rate produces silence-free, finite output.
**Spectral shape is deliberately not asserted** — brown's leaky integrator has a hard-coded
coefficient (`kBrownLeak = 0.98f`, `noise_generator.h:467-468`) whose corner moves ~155 Hz → ~620 Hz
from 48 to 192 kHz; blue and violet are one-sample differentiators (`:483`, `:498`); pink's Kellet
coefficients are tuned at 44.1 kHz (`pink_noise_filter.h:65-70`); and the fifth `AudioFeatures` band
is literally `[8k, Nyquist]`. Asserting centroid or band fractions would be a criterion no correct
implementation can pass.

`TEST_CASE("NoiseOrganism_NoZipperUnderDrift", "[noise_organism][long]")` — SC-009 (b), the envelope
arm (the gain-domain arm is already green from T013). 25 ms-frame RMS envelope in dB over a **5
minute** render with every wander lane at maximum rate and depth;
`maxΔ = max |env[k] − env[k−1]|`. The test **first measures and records** the estimator noise floor
at this frame length on a fixed-gain render of the same configuration and asserts the acceptance
threshold sits at least **3 σ** above it; only then does it require `maxΔ ≤ 1.5 ×` the same statistic
on a **`setWanderEnabled(false)`** render (zeroed depths alone are not a static configuration).
`artifact_detection.h`'s `ClickDetector` is **not** used, and the test says why in a comment: it
thresholds the signal's first derivative at 5 σ (`artifact_detection.h:38-99`), which flags every
sample of a broadband noise render.

**Verify:**
`build/windows-x64-release/bin/Release/dsp_systems_tests.exe "NoiseOrganism_*" 2>&1 | tee long.log | tail -20`
— read the log; do not re-run to grep it.

---

## Group R — Integration

### T022 — Registration audit, full-suite run, lints, portability, compliance

**Registration audit (re-verify T001's single CMake task landed intact):**
* `dsp/tests/CMakeLists.txt` lists **all four** new TUs in the `dsp_systems_tests` source list,
  each **exactly once**. The list is enumerated, not globbed — an unregistered TU compiles into
  nothing and its cases silently never run.
* **Exactly one** of them — `unit/systems/noise_organism_nonfinite_test.cpp` — appears in the
  `-fno-fast-math -fno-finite-math-only` block; the other three do **not**. Confirm the perf TU
  especially is absent (`-fno-fast-math` would move the figures its baselines pin) and the two
  ordinary TUs are absent (they must prove the FR-008 guards in the FP mode the header ships in).
* `dsp/lint_all_headers.cpp` contains
  `#include <krate/dsp/systems/noise_organism.h>` in the Layer 3 block — without it clang-tidy gives
  the new header zero coverage and SC-012's gate passes vacuously. Record the line number.
* No change to `dsp/CMakeLists.txt` (header-only), no plugin/CI/preset changes.

**Full-suite run (SC-011):**
```bash
CMAKE="C:/Program Files/CMake/bin/cmake.exe"
"$CMAKE" --build build/windows-x64-release --config Release --target \
  dsp_core_tests dsp_primitives_tests dsp_processors_tests dsp_systems_tests dsp_effects_tests membrum_tests
for t in dsp_core_tests dsp_primitives_tests dsp_processors_tests dsp_systems_tests dsp_effects_tests membrum_tests; do
  build/windows-x64-release/bin/Release/$t.exe 2>&1 | tail -3
done
```
Every one must report "All tests passed", with **no edits to any existing case** —
`noise_generator_test.cpp`, `resonator_bank_test.cpp` and `noise_oscillator_test.cpp` green with
their pre-existing cases untouched.

**Lints and portability (SC-012):** each must exit 0 —
```bash
node tools/check-portability.js
node tools/lint-layers.js
node tools/lint-odr.js
node tools/lint-float-bit-goldens.js
node tools/lint-simd-aligned-loadstore.js
./tools/run-clang-tidy.ps1 -Target dsp -BuildDir build/windows-ninja
```
Confirm `noise_organism.h`'s only same-layer include is `systems/timevar_comb_bank.h` and that it
carries the justification comment. Fix **all** clang-tidy warnings the new header produces — not
only the ones you consider new.

**Zero compiler warnings** across the whole build. No `std::isnan`/`std::isinf` anywhere in the new
code; no narrowing in brace initialisation; no new SIMD.

**Write `specs/vorago-phase2-noise-organism/compliance.md`** — one row per FR and per SC, each with
**concrete evidence**: a `file:line` for every FR, and for every SC the test-case name plus the
**actual measured number** compared against the spec threshold. Generic "implemented" / "test passes"
rows are not acceptable. Specifically record:
* the T002 stage-cost table and the T020 SC-004 (a)–(e) measured ns/512-block figures against
  106 666 ns, plus (e)'s measured saving vs (c);
* the T016 measured `kSourceDriveDb`, `kModelTrimDb`, FR-018 fit coefficients and
  `kMaxCombDelayStepSamples`, each with its measurement date;
* the T018 three-toolchain sample tolerance and the injected-defect (salt-collision) demonstration;
* the T021 SC-002 (b) CV pair and the SC-002 (d) realised comb excursion percentage;
* `getAllocatedBytes()` vs the 640 KiB threshold and the header's documented figure.

If any requirement is not met, say so honestly with the gap named. A table of ✅ that was not
individually verified is worse than an honest ❌.
