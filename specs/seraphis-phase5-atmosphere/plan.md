# Implementation Plan: Seraphis Phase 5 — Granular Atmosphere Engine

**Spec:** `specs/seraphis-phase5-atmosphere/spec.md`
**Roadmap:** `specs/Seraphis-roadmap.md` Part A → Phase 5 (lines 227–248)
**Deliverables:** one new Layer 3 header (`dsp/include/krate/dsp/systems/atmosphere_engine.h`), one
strictly-additive amendment to a shared Layer 1 header (`primitives/rolling_capture_buffer.h`), four new
test TUs under `dsp/tests/unit/systems/`, and an extension of the existing
`dsp/tests/unit/primitives/test_rolling_capture_buffer.cpp`.
**Plugin work:** none.

Every signature, constant and line number below was read from the working tree in the session that
produced this document (branch `feat/seraphis-phase1-life-modulators`). Where this plan refines,
tightens or diverges from the spec, it is recorded in **§17 (Recorded deviations)** — never silently;
risks and unresolved items are in **§18**, and **§19** is the executable task order.

---

## 0. Reused components — verified signatures (read this session)

| Component | Header (layer) | Signatures / facts this plan depends on |
|---|---|---|
| `RollingCaptureBuffer` | `primitives/rolling_capture_buffer.h` (L1) | `class RollingCaptureBuffer` (`:50`), non-copyable / movable (`:59-62`); `void prepare(double sampleRate, float maxDurationSeconds) noexcept` (`:75-93`) — `capacity_ = nextPowerOf2(sampleRate*seconds)` (`:83`), `bufferL_/bufferR_.resize(capacity_, 0)` (`:86-87`), `mask_ = capacity_ - 1` (`:90`), ends with `reset()` (`:92`); `void reset() noexcept` fills both buffers with 0 and zeroes `writeIndex_`/`samplesWritten_` (`:96-101`); `void writeStereo(float,float) noexcept` writes at `writeIndex_`, then `writeIndex_ = (writeIndex_+1) & mask_`, then `if (samplesWritten_ < capacity_) ++samplesWritten_` (`:113-122`); `void extractSlice(float*,float*,size_t lengthSamples,size_t offsetSamples) const noexcept` (`:141-170`) with `startOffset = offsetSamples + lengthSamples`, `startIndex = (writeIndex_ - startOffset + capacity_) & mask_` (`:161-162`), `out[i] = buffer[(startIndex+i) & mask_]` (`:165-169`); `size_t getCapacitySamples()` (`:187`); `size_t getSamplesWritten()` (`:197`) — **saturates at capacity**; `size_t getAvailableSamples()` = `min(samplesWritten_, capacity_)` (`:204-206`); members `capacity_`, `mask_`, `writeIndex_`, `samplesWritten_` all `size_t` and **0 before `prepare`** (`:227-230`) |
| `GrainScheduler` | `processors/grain_scheduler.h` (L2) | `class GrainScheduler` (`:29`); `void prepare(double) noexcept` → sets `sampleRate_`, calls `reset()` (`:33-36`); `void reset() noexcept` → `samplesUntilNextGrain_ = 0`, `calculateInteronset()` — **does not touch `rng_`** (`:39-42`); `void setDensity(float) noexcept` → `density_ = std::max(0.1f, grainsPerSecond)` (`:46-49`); `void setJitter(float) noexcept` clamps `[0,1]` (`:64-66`); `void setMode(SchedulingMode)` (`:56`); `[[nodiscard]] bool process() noexcept` (`:73-93`) — decrements, and on trigger draws **exactly one** `rng_.nextFloat()` when `mode_ == Asynchronous && jitter_ > 0` (`:82`), reload `interonsetSamples_ * (1 + r*jitter_*0.5)` (`:84`); `void seed(uint32_t) noexcept` → `rng_ = Xorshift32(seedValue)` (`:97`) — **the only writer of `rng_`**; `interonsetSamples_ = sampleRate_/density_` (`:100-103`); `Xorshift32 rng_{12345}` (`:110`); `enum class SchedulingMode : uint8_t { Asynchronous, Synchronous }` (`:22-25`) |
| `GrainEnvelope` | `core/grain_envelope.h` (L0) | `enum class GrainEnvelopeType : uint8_t { Hann, Trapezoid, Sine, Blackman, Linear, Exponential }` (`:14-21`); `inline void generate(float* out, size_t size, GrainEnvelopeType, float attackRatio=0.1f, float releaseRatio=0.1f) noexcept` (`:33-158`) — null/zero-size guard (`:35-37`), phase denominator `size-1` (`:41`); **`Exponential`'s release is `std::exp(-t*4)` with `t = (i-sustainEnd)/releaseSamples` (`:147-149`), so the last entry is `exp(-(releaseSamples-1)/releaseSamples * 4) ≈ 0.0183`, not 0**; `Hann` (`:47-50`), `Sine` (`:79-82`), `Blackman` (`:88-94`), `Trapezoid` (`:61-73`), `Linear` (`:106-118`) all end at exactly 0; `[[nodiscard]] inline float lookup(const float* table, size_t tableSize, float phase) noexcept` (`:165-182`) — clamps phase to `[0,1]` (`:172`), `indexFloat = phase*(tableSize-1)` (`:175`), linear interpolation between `index0` and `min(index0+1, tableSize-1)` (`:176-181`) |
| `BrownianDrift` | `processors/brownian_drift.h` (L2) | `class BrownianDrift : public ModulationSource` (`:94`); `kTauMin = 0.2f` (`:97`), `kTauMax = 30.0f` (`:99`), `kInternalStd = 0.5f` (`:101`), `kDriftOutputSmoothMs = 150.0f` (`:103`), `kControlRateInterval = 32` (`:105`), `kWalkLimit = 4.0f` (`:226`), `kDenormalFloor = 1e-20f` (`:228`); `void prepare(double) noexcept` (`:121-129`) — `controlDtSeconds_ = kControlRateInterval / sampleRate_`, `updateCoefficients()`, `outputSmoother_.configure(150, sr)`, `initState()`; `void reset() noexcept` → `initState()` (`:133-135`) which **re-seeds** `rng_.seed(configuredSeed_)` (`:243`); `void setSeed(uint32_t)` (`:145-148`); `void setSmoothness(float)` (`:152-155`); `void setDepth(float)` (`:159-161`); `void processBlock(size_t) noexcept` (`:194-206`) — carries `samplesUntilControl_` across calls, `outputSmoother_.advanceSamples(advance)` per sub-span; `float getCurrentValue() const noexcept override` = `clamp(outputSmoother_.getCurrentValue(), -1, +1)` (`:212-214`); `updateCoefficients()` (`:230-240`): `tau = lerp(kTauMin,kTauMax,smoothness)` in **double**, `a = exp(-controlDt/tau)`, `g = kInternalStd*sqrt(1-a²)`; `advanceControlStep()` (`:253-270`): three **sequenced** `rng_.nextFloat()` locals summed Irwin–Hall, `x = mean + a*(x-mean) + g*z`, `clamp(±kWalkLimit)`, denormal flush, `outputSmoother_.setTarget(clamp(depth*x,-1,+1))` |
| `HarmonicCloud` drift lanes (idiom, **not composed**) | `systems/harmonic_cloud.h` (L3) | `kMaxPartials = 64` (`:138`); `kControlChunkSamples = 64` (`:144`); `kDriftControlInterval = 32` (`:148`); `kDriftTauMin/Max` (`:151-152`), `kDriftInternalStd` (`:155`), `kDriftOutputSmoothMs` (`:158`), `kDriftWalkLimit`/`kDriftDenormalFloor` (`:161-162`); `struct LaneRng { Xorshift32 rng{1}; }` (`:1121-1123`) — exists because `Xorshift32`'s only ctor is `explicit` (`random.h:45`), making `std::array<Xorshift32,N>{}` ill-formed; `struct DriftLanes` (`:1125-1148`) — `alignas(32) std::array<float,N> walk/smoothCur/smoothTgt`, `std::array<LaneRng,N> rng`, `float a,g,depth`, **one shared** `int samplesUntilControl`, plus `cachedPowN`/`cachedPowValue` memo; `updateDriftCoefficients` (`:1863-1875`) — double-precision intermediates, transcribed from `brownian_drift.h:230-240`; `advanceControlStepAllLanes` (`:1888-1907`) — three sequenced draws per lane, **every** lane steps; `advanceSmootherAllLanes` (`:1964-1985`) — transcription of `OnePoleSmoother::advanceSamples` including the `isComplete()` early **continue** (`:1976-1977`), `detail::flushDenormal` (`:1980`) and the post-advance snap (`:1981-1983`), with `std::pow(coeff, (float)numSamples)` hoisted out of the lane loop and memoised; `advanceDriftLanes` (`:1999-2011`) — structural mirror of `BrownianDrift::processBlock`; `resetDriftLanes` (`:2014-2021`); `driftSmoothCoeff_ = calculateOnePolCoefficient(kDriftOutputSmoothMs, sr)` (`:296-297`); `[[nodiscard]] float getDriftLaneValue(std::size_t i) const noexcept` (`:1025`) |
| `OnePoleSmoother` | `primitives/smoother.h` (L1) | `class OnePoleSmoother` (`:134`); `void configure(float smoothTimeMs, float sampleRate) noexcept` (`:160-164`); `ITERUM_NOINLINE void setTarget(float) noexcept` — NaN → both 0, Inf → ±1e10 (`:170-181`); `[[nodiscard]] float process() noexcept` — early **snap** to target under `kCompletionThreshold`, then `current_ = target_ + coeff*(current_-target_)`, then `flushDenormal` (`:197-211`); `[[nodiscard]] bool isComplete() const` (`:232-234`); `void advanceSamples(size_t) noexcept` (`:243-254`) — `if (numSamples==0 \|\| isComplete()) return;` then `target + diff*std::pow(coefficient_,(float)N)`, `flushDenormal`, post-snap; `void snapTo(float)` (`:263-272`); `void snapToTarget()` (`:257-259`); `[[nodiscard]] float getCurrentValue() const` (`:191-193`) |
| `LinearRamp` | `primitives/smoother.h` (L1) | `class LinearRamp` (`:305`); `void configure(float rampTimeMs, float sampleRate) noexcept` (`:329-336`) — recomputes `increment_` only if a transition is live; `ITERUM_NOINLINE void setTarget(float)` (`:342-354`) — NaN → 0, Inf → ±1e10, `increment_ = calculateLinearIncrement(target_-current_, rampTimeMs_, sampleRate_)`; `[[nodiscard]] float process() noexcept` (`:370-389`) — early return at target, increment, overshoot clamp, `flushDenormal`; `void snapTo(float)` (`:421-431`); `bool isComplete() const` = `current_ == target_` (`:409-411`) |
| smoother free functions | `primitives/smoother.h` (L1) | `inline constexpr float kCompletionThreshold = 0.0001f` (`:55`); `[[nodiscard]] constexpr float calculateOnePolCoefficient(float ms, float sr) noexcept` = `constexprExp(-5000/(clamp(ms)*sr))` (`:77-93`); `calculateLinearIncrement(delta, ms, sr)` (`:100-108`) |
| `STFT` | `primitives/stft.h` (L1) | `class STFT` (`:35`), non-copyable / movable (`:41-44`); `void prepare(size_t fftSize, size_t hopSize, WindowType = Hann, float kaiserBeta = 9.0f) noexcept` (`:58-86`) — `inputBuffer_.resize(fftSize*8)` (`:78`), `windowedFrame_.resize(fftSize)` (`:81`); `void reset() noexcept` (`:90-94`); `void pushSamples(const float*, size_t) noexcept` (`:104-113`) — **`samplesAvailable_` is incremented per sample with no bound check**; `[[nodiscard]] bool canAnalyze() const noexcept` = `samplesAvailable_ >= fftSize_` (`:120-124`); `void analyze(SpectralBuffer&) noexcept` (`:130-149`) — reads the **oldest** `fftSize_` samples, windows, forward FFT, then `samplesAvailable_ -= hopSize_`; `[[nodiscard]] size_t latency() const noexcept { return fftSize_; }` (`:160`); `bool isPrepared()` (`:162`) |
| `OverlapAdd` | `primitives/stft.h` (L1) | `class OverlapAdd` (`:181`); `void prepare(size_t fftSize, size_t hopSize, WindowType, float kaiserBeta, bool applySynthesisWindow) noexcept` (`:206-250`) — COLA sum over hop positions of `w[idx]²` when `applySynthesisWindow` (`:226-239`), `outputBuffer_.resize(fftSize*2)` (`:243`); doc `:201-204`: synthesis windowing "Required for spectral modification processors … at ≥75 % overlap where Hann² satisfies COLA. Must NOT be used at 50 % overlap"; `void reset() noexcept` (`:254-257`); `void synthesize(const SpectralBuffer&) noexcept` (`:266-289`) — **always accumulates at `outputBuffer_[0..fftSize)` with no offset** (`:277-285`), then `samplesReady_ += hopSize_`; `[[nodiscard]] size_t samplesAvailable() const` (`:296`); `void pullSamples(float*, size_t) noexcept` (`:305-324`) — **returns silently if `numSamples > samplesReady_`**, copies, shifts the buffer left, zero-fills the tail |
| `SpectralBuffer` | `primitives/spectral_buffer.h` (L1) | `class SpectralBuffer` (`:45`); `void prepare(size_t fftSize) noexcept` → `numBins_ = fftSize/2+1` (`:61-67`); `void reset()` (`:71-77`); `[[nodiscard]] float getMagnitude(size_t) const noexcept` (`:84-88`); `[[nodiscard]] float getPhase(size_t) const noexcept` (`:91-95`); `void setMagnitude(size_t,float)` (`:98-103`); `void setPhase(size_t,float) noexcept` (`:106-111`) — invalidates `cartesianValid_`; `const Complex* data() const noexcept` **rebuilds Cartesian from polar** (`:156-159`); `[[nodiscard]] size_t numBins() const` (`:166`); `bool isPrepared()` (`:169`) |
| `Window::generate` | `core/window_functions.h` (L0) | `enum class WindowType : uint8_t` (`:53`); `[[nodiscard]] inline std::vector<float> generate(WindowType, size_t, float kaiserBeta) ` (`:282-308`); `generateHann` is the **periodic (DFT-even)** variant, `0.5 - 0.5*cos(2πn/N)` dividing by `N` not `N-1` (`:110-120`) — so Hann² at 75 % overlap is **exactly** COLA (hop-position squares `0 + 0.25 + 1 + 0.25 = 1.5`) |
| `SpectralFreezeOscillator` | `processors/spectral_freeze_oscillator.h` (L2) | `class SpectralFreezeOscillator` (`:80`), non-copyable / movable (`:93-96`); `kMinFFTSize = 256` (`:642`), `kMaxFFTSize = 8192` (`:643`); `void prepare(double sampleRate, size_t fftSize = 2048) noexcept` (`:105-…`) — clamps to `[256,8192]` then `std::bit_floor`s a non-power-of-two and re-clamps (`:107-113`), `hopSize_ = fftSize/4` (`:117`); **`void reset() noexcept` (`:173-196`) — PUBLIC (`public:` at `:81`, `private:` at `:435`), documented "Clear all internal buffers and state without deallocating (FR-002)" and "@note Real-time safe": nine `std::fill`s over `frozenMagnitudes_`/`initialPhases_`/`phaseAccumulators_`/`ifftBuffer_`/`workingMagnitudes_`/`captureBuffer_`/`outputBuffer_`/`originalEnvelope_`/`shiftedEnvelope_`, plus `workingSpectrum_.reset()` and `formantPreserver_.reset()`, and it clears `frozen_`/`unfreezing_`/`unfadeSamplesRemaining_`/`outputWriteIndex_`/`outputReadIndex_`/`samplesInBuffer_`. Early-outs on `!prepared_` (`:174`). This is the allocation-free rewind `reset()` needs (§6.2 step 7, §12.4)**; `void freeze(const float* inputBlock, size_t blockSize) noexcept` (`:217-288`) — **mono**, `copyLen = min(blockSize, fftSize_)` (`:222`), zero-pads (`:221`), pre-fills the overlap-add pipeline with `overlapFactor` frames (`:267-287`); `void unfreeze() noexcept` (`:295-300`) — sets `unfreezing_ = true`, `unfadeSamplesRemaining_ = hopSize_` (`:299`); **`processBlock` decrements that counter once per sample while it is `> 0` (`:346-351`) and only reaches the `else` that sets `frozen_ = false` (`:352-357`) on the sample AFTER it hits zero — so draining exactly `getHopSize()` samples leaves `frozen_ == true`**; `bool isFrozen() const` (`:303`); `void processBlock(float* output, size_t numSamples) noexcept` (`:317-369`) — **mono**, fills zeros when not prepared/not frozen (`:321-330`), clamps output to `[-2,+2]` (`:364`), `flushDenormal` (`:367`); `void setPitchShift(float)` (`:384`); `void setSpectralTilt(float)` (`:395`); `[[nodiscard]] size_t getLatencySamples() const` (`:421-423`); `[[nodiscard]] size_t getFftSize() const` (`:426-428`); `[[nodiscard]] size_t getHopSize() const` (`:431-433`) |
| `Xorshift32` / `deriveStreamSeed` | `core/random.h` (L0) | `class Xorshift32` (`:41`); `explicit constexpr Xorshift32(uint32_t = 1)` — substitutes `kDefaultSeed` for 0 (`:45-46`); `[[nodiscard]] constexpr uint32_t next()` (`:50-55`); `[[nodiscard]] constexpr float nextFloat()` → `[-1,1]` (`:59-63`); `[[nodiscard]] constexpr float nextUnipolar()` → `[0,1]` (`:67-69`); `constexpr void seed(uint32_t)` — substitutes `kDefaultSeed` for 0 (`:73-75`); `[[nodiscard]] constexpr uint32_t state() const` (`:79-81`); `[[nodiscard]] constexpr std::uint32_t deriveStreamSeed(std::uint32_t base, std::size_t salt) noexcept` (`:102-111`) — lowbias32 finaliser with explicit non-zero substitution `(h != 0u) ? h : 0x2545F491u` |
| `detail::isNaN` / `isInf` / `flushDenormal` | `core/db_utils.h` (L0) | `constexpr bool isNaN(float) noexcept` (`:54-57`) — `((bits & 0x7F800000) == 0x7F800000) && ((bits & 0x007FFFFF) != 0)`; `[[nodiscard]] constexpr bool isInf(float) noexcept` (`:175-178`) — `(bits & 0x7FFFFFFF) == 0x7F800000`; `[[nodiscard]] inline constexpr float flushDenormal(float) noexcept` (`:168-170`); `-ffast-math` rationale documented at `:44-52` |
| `semitonesToRatio` / `centsToPitchRatio` | `core/pitch_utils.h` (L0) | `[[nodiscard]] inline float semitonesToRatio(float semitones) noexcept` = `std::pow(2.0f, semitones/12.0f)` (`:23-26`); `[[nodiscard]] inline float centsToPitchRatio(float cents) noexcept` = `std::exp2(cents/1200.0f)`, accurate over the whole float range (`:33-36`) |
| `kPi` / `kTwoPi` / `kHalfPi` | `core/math_constants.h` (L0) | `:28`, `:32`, `:36` |
| stream contract | `systems/continuous_body.h` (L3) | `void processStereoBlock(const float* inLeft, const float* inRight, float* outLeft, float* outRight, std::size_t numSamples) noexcept` (`:1161-1163`); guards in order — null → write nothing (`:1166-1170`), `numSamples == 0` → no-op consuming **no** control step (`:1172-1174`), pre-`prepare` → zero-fill (`:1176-1180`); absolute control grid `toGrid = kControlChunkSamples - (sampleCounter_ % kControlChunkSamples)` (`:1184-1186`); `static constexpr std::size_t kControlChunkSamples = 64` (`:97`) |
| `GrainProcessor` pan law (**law only, class unused**) | `processors/grain_processor.h` (L2) | `panNorm = (pan + 1)*0.5`; `panL = std::cos(panNorm*kHalfPi)`; `panR = std::sin(panNorm*kHalfPi)` (`:101-103`) |

### 0.1 Verified traps this plan must not fall into

1. **`STFT::pushSamples` has no overflow guard** (`stft.h:104-113`). `samplesAvailable_` is incremented per
   pushed sample with no comparison against `inputBuffer_.size()` (= `fftSize*8`). Push more than
   `8·fftSize − fftSize` samples without draining and `writeIndex_` laps the read position while
   `samplesAvailable_` keeps climbing, so `analyze()` reads overwritten samples. FR-043's literal wording
   ("draining every available frame each block") does **not** bound this: at `blurFftSize = 256` and
   `maxBlockSamples = 8192` a single call pushes 8192 samples into a 2048-sample buffer *before* any
   drain. §8's pump is therefore **control-chunk bounded** (≤ 64 samples pushed between drains), which
   makes `samplesAvailable_ ≤ fftSize + 64 < 8·fftSize` for every legal `blurFftSize ≥ 256`. Deviation
   **D-2**.
2. **`OverlapAdd::synthesize` accumulates at offset 0 unconditionally** (`stft.h:277-285`); the hop offset
   comes **only** from `pullSamples` shifting the buffer left (`:309-323`). Two `synthesize()` calls
   without an intervening `pullSamples(hopSize)` stack both frames at the same offset and destroy COLA.
   The pull **must** be inside the drain loop (FR-043).
3. **`OverlapAdd::pullSamples` returns silently when `numSamples > samplesReady_`** (`:306`) — it does not
   zero the destination. A pull of the wrong size is therefore a *silent stale-buffer read*, not a
   detectable failure. §8's FIFO owns the block-rate re-timing; the pull is always exactly `hopSize`.
4. **`OnePoleSmoother::configure` computes a per-`process()`-call coefficient** (`:160-164`) and
   `process()` advances exactly one sample (`:197`). A smoother read once per blur frame must be advanced
   with `advanceSamples(hopSize)` (`:243`), never one `process()` (FR-009's smoother-cadence rule).
5. **`OnePoleSmoother::process()` snaps to target when within `kCompletionThreshold = 1e-4`** (`:199-202`).
   The `1/√n` gain and the level trim therefore quantise to 1e-4 at rest — irrelevant audibly, but it is
   why SC-008's bound is a statistical argument and not an exact algebraic one.
6. **`GrainScheduler::reset()` and `prepare()` never touch `rng_`** (`:33-42`); only `seed()` does
   (`:97`). `AtmosphereEngine::reset()` **must** call `scheduler_.seed(deriveStreamSeed(seed_, kSchedulerSalt))`
   explicitly or the post-`reset` render does not reproduce the original (FR-006).
7. **`GrainScheduler::setDensity` clamps to `≥ 0.1`** (`:47`). The engine clamps to the same bound so the
   control table and the component agree (FR-009).
8. **`GrainEnvelope::generate` leaves `Exponential` ending at ≈0.0183** (`:147-149`), **and forcing the
   last table entry to 0 does not by itself fix it.** `lookup` maps `phase → indexFloat = phase·(size−1)`
   (`:175`), so an envelope phase of `ageSamples/L'` — whose maximum over ages `0 … L'−1` is
   `(L'−1)/L' < 1` — never reads the last entry at all. The engine therefore does **two** things (§9.6):
   the phase denominator is `L' − 1`, so the final sample lands exactly on the last entry (D-4b), and the
   forced zero region is a **tail run of `kEnvelopeTailZeroEntries = 2`** plus `table[0]`, so the last two
   emitted samples of every grain are 0 for every type (D-13). FR-027.
9. **`Xorshift32`'s only constructor is `explicit`** (`random.h:45`), so `std::array<Xorshift32, N>{}` is
   ill-formed. The `DriftLaneRng` wrapper exists for exactly this reason (`harmonic_cloud.h:1116-1123`).
10. **`BrownianDrift::reset()` re-seeds** (`:133-135` → `initState()` → `:243`). A grain birth must
    **not** be modelled as a lane `reset()`: FR-030 zeroes the walk state and leaves the stream position
    alone. Modelling it as a reset would hand every grain on a slot one identical walk.
11. **`RollingCaptureBuffer` is entirely zero before `prepare`** — `capacity_ = mask_ = 0`, `bufferL_`/
    `bufferR_` empty vectors (`:227-230`). `readStereoLinear` must guard on `capacity_ == 0` *before* any
    `& mask_` indexing (FR-081) or it is an out-of-bounds read on a shared Layer 1 primitive.
12. **`getAvailableSamples()` returns `size_t` and starts at 0** (`:204-206`, `:100`). A bare
    `available - 2` wraps to ~2⁶⁴ on a fresh `prepare`/`reset` — the clamp becomes a no-op exactly where
    it is needed (FR-081).
13. **`extractSlice` anchors from the *end* of the slice** (`:161-162`): `out[i]` has age
    `offsetSamples + lengthSamples − 1 − i`. Only `out[lengthSamples-1]` has age `offsetSamples`. SC-012's
    correspondence must be written from this form, not from "same offset".
14. **`ContinuousBody` carries a private `isFiniteBits`** (`continuous_body.h:1346-1358`) — a *fourth*
    reimplementation of the same bit test. FR-008 forbids Phase 5 adding a fifth. §13.2's `isFinite`
    helper is a one-line **composition** of `detail::isNaN` / `detail::isInf`, not a new bit test —
    and it is declared **`ITERUM_NOINLINE`** (`primitives/smoother.h:37-45`), because `db_utils.h:44-52`
    requires `-fno-fast-math` of every source file that uses those helpers and a header cannot impose
    that on its consumers. `constexpr` is deliberately absent. See D-16.
15. **`SpectralFreezeOscillator::freeze` truncates its input at `fftSize_`** (`:222-223`) and zero-pads a
    short one. FR-051's capture length must come from `getFftSize()` (`:426-428`), never from the
    requested `config.freezeFftSize`.
16. **`SpectralFreezeOscillator::processBlock` fills zeros when not frozen** (`:327-330`) — so a
    never-captured freeze leg is silent, not stale. That is what makes FR-052's `m = 0` hard bypass and
    the non-bypassed path agree.
17. **`std::array<Xorshift32,…>`-style value-init is not the only `explicit`-ctor trap**: `SpectralBuffer`
    declares a move ctor/assignment but **no copy** (`spectral_buffer.h:51-52`), and `STFT`/`OverlapAdd`
    delete copy (`stft.h:41-44`, `:187-190`). `AtmosphereEngine` therefore deletes copy and defaults move.
18. **`ITERUM_NOINLINE` is not free, and the naive placements are unaffordable.** ~2 ns per call means
    per-sample finiteness checks cost ~4 % of the SC-004 reference. §9.1 and §13.3 are therefore
    accumulate-then-test (one call per 64-sample chunk), and `RollingCaptureBuffer::readStereoLinear` —
    up to 2 calls per grain per sample, ~65 k per block at saturation — uses **ordered comparisons**
    instead of any classification test at all (§2). Neither construction can miss a value: non-finites
    propagate through `+`, and the ordered comparisons are false for NaN.

---

## 1. Blocking prerequisites (task T0 — before any header is written)

| Check | Command | Result this session |
|---|---|---|
| `AtmosphereEngine` free | `grep -rn "class AtmosphereEngine\|struct AtmosphereEngine" dsp/ plugins/ tools/` | **0** |
| `AtmosphereGrain` free | `grep -rn "AtmosphereGrain" dsp/ plugins/ tools/` | **0** |
| `GrainDriftLanes` free | `grep -rn "GrainDriftLanes" dsp/ plugins/ tools/` | **0** |
| `DriftLaneRng` free | `grep -rn "DriftLaneRng" dsp/ plugins/ tools/` | **0** |
| `PrepareConfig` free | `grep -rn "struct PrepareConfig\|class PrepareConfig" dsp/ plugins/ tools/` | **0** |
| header does not exist | `ls dsp/include/krate/dsp/systems/atmosphere_engine.h` | **No such file** |
| `readStereoLinear` not already a member | `grep -n "readStereoLinear" dsp/include/krate/dsp/primitives/rolling_capture_buffer.h` | **0** |

Names **taken** and forbidden at namespace scope: `Grain` (`primitives/grain_pool.h:23`), `GrainPool`
(`:39`), `GrainScheduler` (`processors/grain_scheduler.h:29`), `GrainProcessor`
(`processors/grain_processor.h:37`), `GranularEngine` (`systems/granular_engine.h:30`), `SlicePool`
(`primitives/slice_pool.h:137`), `kMaxGrains` (**namespace-scope-free but a `GrainPool` class constant at
`grain_pool.h:39`-region** — Phase 5's is class-scoped, which is why it does not collide),
`DriftLanes`/`LaneRng` (`systems/harmonic_cloud.h:1125`, `:1121` — class-nested, so not an ODR hazard, but
renamed anyway so a reader is never in doubt), `LaneRng` again at
`processors/entropy_processor.h:423`.

**Every `AtmosphereEngine` constant is `static constexpr` inside the class.** Nothing is added at
namespace scope. Re-run the sweep at implementation time and again with `node tools/lint-odr.js`.

---

## 2. Amendment RA-1 — `RollingCaptureBuffer::readStereoLinear` (FR-080 … FR-084)

**File:** `dsp/include/krate/dsp/primitives/rolling_capture_buffer.h`. **Layer 1. SHARED DSP.**
Strictly additive; no existing member, default or behaviour changes.

**Placement:** immediately after `extractSlice` (`:170`), inside the existing "Slice Extraction
(Real-Time Safe)" section, so the two anchoring conventions sit next to each other and the doc comment
that distinguishes them cannot drift apart from either.

```cpp
    /// @brief Read one linearly-interpolated stereo sample at a fractional age.
    ///
    /// Age 0.0 addresses the MOST RECENT written sample (index `writeIndex_ - 1`);
    /// increasing age moves back in time. The value is interpolated between the
    /// samples at ages `floor(age)` and `floor(age) + 1`.
    ///
    /// @par Relationship to extractSlice(). extractSlice anchors from the END of
    ///      the slice (`startOffset = offsetSamples + lengthSamples`, :161-162), so
    ///      `out[i]` has age `offsetSamples + lengthSamples - 1 - i`. The two agree
    ///      on "same offset" ONLY at `lengthSamples == 1`. The general identity is
    ///          extractSlice(outL, outR, L, O)[i] == readStereoLinear(O + L - 1 - i)
    ///
    /// @param ageSamples  Age behind the write head; clamped to
    ///                    [0, getAvailableSamples() - 2]. A NON-FINITE argument
    ///                    lands on age 0 (the most recent sample) via the same
    ///                    two ordered comparisons that perform the clamp -- see
    ///                    the fast-math note below.
    /// @param outLeft     Receives the interpolated left sample.
    /// @param outRight    Receives the interpolated right sample.
    ///
    /// @note Unprepared buffer, or fewer than 2 samples written, yields (0, 0) and
    ///       touches nothing. Both halves of that guard are load-bearing: before
    ///       prepare() `capacity_ = mask_ = 0` and both vectors are EMPTY (:227-230),
    ///       so `bufferL_[idx & mask_]` would be an out-of-bounds read; and
    ///       getAvailableSamples() is size_t starting at 0 (:204, :100), so a bare
    ///       `available - 2` wraps to ~2^64 and the clamp becomes a no-op in exactly
    ///       the case it exists for.
    ///
    /// @note NON-FINITE ARGUMENTS AND -ffast-math. This function is a header that
    ///       lands in translation units built with /fp:fast (MSVC) and -ffast-math
    ///       (the macOS leg, via the VST3 SDK's global flags). `core/db_utils.h:44-52`
    ///       states the resulting contract verbatim: "Source files using this
    ///       function MUST be compiled with -fno-fast-math", and the repo's existing
    ///       remedy for headers that cannot guarantee that is to hide the check
    ///       behind a call boundary (`ITERUM_NOINLINE`, `primitives/smoother.h:37-45`
    ///       — "Required to prevent branch elimination with NaN checks under
    ///       /fp:fast", applied at `:170`, `:342`, `:519`). NEITHER is used here,
    ///       because this function sits on AtmosphereEngine's innermost loop (up to
    ///       2 calls per grain per sample) where a non-inlinable call per invocation
    ///       is unaffordable. Instead the guard is expressed as TWO ORDINARY ORDERED
    ///       COMPARISONS which double as the range clamp: `!(age >= 0)` is taken by
    ///       negatives, by -Inf and by NaN (an unordered compare yields false), and
    ///       `age > maxAge` is then taken by +Inf. Neither is an FP classification
    ///       predicate, so -ffinite-math-only has nothing to fold away — it cannot
    ///       prove `age >= 0.0f`. The result is always a finite value in
    ///       [0, maxAge] with no bit test, no call boundary and no added cost.
    ///
    /// @note O(1), const, noexcept, allocation-free. Steady-state indexing uses the
    ///       existing `& mask_` wraparound (:117, :166) — no new indexing scheme and
    ///       no per-wrap branch.
    void readStereoLinear(float ageSamples, float& outLeft,
                          float& outRight) const noexcept {
        const size_t available = getAvailableSamples();
        if (capacity_ == 0 || available < 2) {          // FR-081 guard, FIRST
            outLeft = 0.0f;
            outRight = 0.0f;
            return;
        }
        const float maxAge = static_cast<float>(available - 2);

        // Ordered-comparison clamp; see the fast-math note above. Written as two
        // separate `if`s, in this order, so NaN falls into the FIRST one.
        float age = ageSamples;
        if (!(age >= 0.0f)) { age = 0.0f; }     // negatives, -Inf, NaN
        if (age > maxAge)   { age = maxAge; }   // +Inf, over-range finite

        const float floorAge = std::floor(age);
        const auto  ageInt   = static_cast<size_t>(floorAge);
        const float frac     = age - floorAge;

        // Age 0 == writeIndex_ - 1. Adding capacity_ keeps the unsigned arithmetic
        // positive before the mask, exactly as extractSlice does (:162).
        const size_t i0 = (writeIndex_ + capacity_ - 1 - ageInt) & mask_;
        const size_t i1 = (i0 + capacity_ - 1) & mask_;   // one sample OLDER

        outLeft  = bufferL_[i0] + frac * (bufferL_[i1] - bufferL_[i0]);
        outRight = bufferR_[i0] + frac * (bufferR_[i1] - bufferR_[i0]);
    }
```

**New includes required in `rolling_capture_buffer.h`:** `<cmath>` (for `std::floor`) and nothing else.
The ordered-comparison guard above needs no `detail::isNaN`/`isInf`, so the earlier draft's
`<krate/dsp/core/db_utils.h>` include is **not** taken — one fewer edge added to a shared Layer 1 header
whose only live consumer is `PatternFreezeMode`. `<algorithm>`, `<cstddef>`, `<cstdint>` and `<vector>`
are already present (`:21-24`), and `<cmath>` is stdlib, so the file stays a legal Layer 1 header
(`node tools/lint-layers.js` gates this).

**Why `i1` is `i0 + capacity_ - 1` and not `i0 - 1`:** ages increase backwards, so the *older* neighbour
sits one index lower modulo capacity. Written additively so the expression never underflows `size_t`
before the mask, matching `extractSlice`'s idiom.

**Why the clamp is `available - 2` and not `available - 1`:** the interpolation needs the sample at
`floor(age) + 1`. At `age = C - 1` that neighbour is age `C`, and `(writeIndex_ - 1 - C) & mask_ ==
(writeIndex_ - 1) & mask_` — the **newest** sample. The reader would blend the oldest sample with the
newest: exactly the wraparound discontinuity FR-025 exists to prevent. FR-025's grain bound is `C − 2`
for the same reason, so the two agree instead of one silently masking the other.

**Cross-consumer impact** (verified by `grep -rn "rolling_capture_buffer.h" dsp/ plugins/ tools/` this
session — no `plugins/` consumer exists):

| Consumer | Site | Suite that must be run **unedited** |
|---|---|---|
| `PatternFreezeMode` | `dsp/include/krate/dsp/effects/pattern_freeze_mode.h:40` | `dsp_effects_tests` |
| header-compile lint | `dsp/lint_all_headers.cpp:70` | (build) |
| existing unit tests | `dsp/tests/unit/primitives/test_rolling_capture_buffer.cpp:20` | `dsp_primitives_tests` |

---

## 3. `AtmosphereEngine` — file, layer, include set

**File:** `dsp/include/krate/dsp/systems/atmosphere_engine.h` (new). **Layer 3.**
`namespace Krate::DSP`. Header-only, like every other Seraphis component.

Header banner states, in this order: layer; spec slug `seraphis-phase5-atmosphere`; the roadmap lines
implemented (227–248); the RT-safety contract; the **memory formula** and RA-2's byte table (FR-073); the
`density × grainSeconds ≤ kMaxGrains` operating rule (FR-022/FR-073); the blur latency (RA-3); and
`kMaxGrains = 64` flagged as **provisional, measurement-backed** (FR-022) together with whichever SC-004
lever was actually spent, if any.

```cpp
#include <krate/dsp/core/db_utils.h>          // L0  detail::isNaN / isInf / flushDenormal
#include <krate/dsp/core/grain_envelope.h>    // L0  GrainEnvelopeType, generate, lookup
#include <krate/dsp/core/math_constants.h>    // L0  kHalfPi, kPi
#include <krate/dsp/core/pitch_utils.h>       // L0  semitonesToRatio
#include <krate/dsp/core/random.h>            // L0  Xorshift32, deriveStreamSeed
#include <krate/dsp/primitives/rolling_capture_buffer.h>  // L1  capture ring + RA-1 reader
#include <krate/dsp/primitives/smoother.h>    // L1  OnePoleSmoother, LinearRamp,
                                              //     calculateOnePolCoefficient, kCompletionThreshold
#include <krate/dsp/primitives/spectral_buffer.h>          // L1
#include <krate/dsp/primitives/stft.h>        // L1  STFT, OverlapAdd, WindowType (via window_functions.h)
#include <krate/dsp/processors/grain_scheduler.h>          // L2
#include <krate/dsp/processors/spectral_freeze_oscillator.h>  // L2

#include <algorithm>
#include <array>
#include <bit>        // std::bit_floor, std::has_single_bit
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>     // envelopeTable_, blurFifo_, fifoScratch_, freezeCapture_, freezeDelay_
```

`<vector>` is listed explicitly even though `primitives/rolling_capture_buffer.h:24` and
`primitives/stft.h:25` would both drag it in transitively: five members are `std::vector` or
`std::array<std::vector<…>, 2>` (§5.3), and relying on a transitive include is exactly the fragility R-8
names the explicit include list as the mitigation for.

`primitives/smoother.h` additionally supplies the `ITERUM_NOINLINE` macro (`:37-45`) that §13.2's
finiteness helper is declared with — that is a second, load-bearing reason the include is present, beyond
`OnePoleSmoother`/`LinearRamp`.

**Deliberately absent, and why** (FR-002):

- `core/stereo_utils.h` — nothing in it is used. Its only function is `stereoCrossBlend` (`:41-49`), a
  width/ping-pong blend that cannot decorrelate. N-9 / C-7.
- `processors/brownian_drift.h` — FR-030 reproduces the recurrence as SoA lanes and instantiates no
  `BrownianDrift`. This mirrors `harmonic_cloud.h`, whose include list (`:17-23`) also omits it. The
  **test** includes it, for SC-002's lane-equivalence gate.
- any `systems/` or `effects/` header — Layer 3 may not include Layer 3 or 4. No `HarmonicCloud`, no
  `ContinuousBody` (N-2).

Layer discipline is gated by `node tools/lint-layers.js`, not by inspection (SC-013).

---

## 4. Constants and `PrepareConfig`

All class-scoped. Values that mirror an existing component cite it, because a divergence would silently
break the equivalence the citation promises.

```cpp
class AtmosphereEngine {
public:
    // --- capacities -------------------------------------------------------
    /// Grain pool capacity. PROVISIONAL and measurement-backed (FR-022):
    /// SC-004 lever (5) may reduce it, which is a specified capability trade
    /// (the documented `density * grainSeconds <= kMaxGrains` region shrinks with it).
    static constexpr std::size_t kMaxGrains = 64;
    /// Envelope lookup table size (FR-027). Regenerated IN PLACE, never resized.
    static constexpr std::size_t kEnvelopeTableSize = 4096;
    /// Entries forced to 0 at the TAIL of the envelope table after every generate()
    /// (FR-027, §9.6). Two, not one: with the FR-026 phase denominator L'-1 the
    /// per-sample table-index step is 4095/(L'-1), which is < 1 for every legal L'
    /// at an audio sample rate, so a two-entry run makes the LAST TWO samples of
    /// every grain exactly 0 for every GrainEnvelopeType including Exponential.
    static constexpr std::size_t kEnvelopeTailZeroEntries = 2;
    /// Youngest age any grain may read (FR-025's young-side guard `g`). This is NOT
    /// the FR-014 admission margin: see §9.4(c)/(e) and deviation D-11.
    static constexpr std::size_t kMinAgeSamples = 64;
    /// Old-side margin the interpolator itself needs: readStereoLinear reads the
    /// samples at ages floor(age) and floor(age)+1, and clamps at
    /// getAvailableSamples() - 2. This, not `g`, is what FR-014's admission test
    /// must require above the birth read age (deviation D-11).
    static constexpr std::size_t kInterpMarginSamples = 2;

    // --- control cadence --------------------------------------------------
    /// Absolute control grid (FR-005). The VALUE is copied from
    /// `ContinuousBody::kControlChunkSamples` (`systems/continuous_body.h:97`) and
    /// `HarmonicCloud::kControlChunkSamples` (`systems/harmonic_cloud.h:144`);
    /// there is no header dependency in either direction.
    static constexpr std::size_t kControlChunkSamples = 64;
    /// Drift-lane OU step interval (`brownian_drift.h:105`, `harmonic_cloud.h:148`).
    static constexpr int kDriftControlInterval = 32;

    // --- smoothing times (FR-009) ----------------------------------------
    static constexpr float kSilenceRampMs   = 10.0f;   // FR-007
    static constexpr float kGainSmoothMs    = 50.0f;   // FR-028, per-sample process()
    static constexpr float kBlurSmoothMs    = 50.0f;   // FR-009, advanceSamples(hopSize)
    static constexpr float kFreezeMixRampMs = 100.0f;  // FR-052, LinearRamp
    static constexpr float kLevelSmoothMs   = 20.0f;   // FR-061, per-sample process()

    // --- drift lane coefficients, transcribed from brownian_drift.h -------
    static constexpr float kDriftTauMin        = 0.2f;    // :97
    static constexpr float kDriftTauMax        = 30.0f;   // :99
    static constexpr float kDriftInternalStd   = 0.5f;    // :101
    static constexpr float kDriftOutputSmoothMs= 150.0f;  // :103
    static constexpr float kDriftWalkLimit     = 4.0f;    // :226
    static constexpr float kDriftDenormalFloor = 1e-20f;  // :228

    // --- ranges -----------------------------------------------------------
    static constexpr float kMinGrainSeconds = 0.05f;
    static constexpr float kMaxGrainSeconds = 30.0f;
    static constexpr float kMinDensity      = 0.1f;   // == grain_scheduler.h:47
    static constexpr float kMaxDensity      = 20.0f;
    static constexpr float kMaxPositionSeconds = 30.0f;
    static constexpr float kMaxPitchSemitones  = 24.0f;
    static constexpr float kPitchSpreadCents   = 1200.0f;
    static constexpr float kMaxDriftRangeSemitones = 12.0f;
    /// Hard bound on a grain's total static pitch AND on its drift envelope
    /// endpoints, so r stays in [2^-3, 2^3] = [0.125, 8] for the grain's whole
    /// life and FR-025's rMin/rMax are always well defined (FR-031).
    static constexpr float kMaxAbsGrainSemitones = 36.0f;
    static constexpr float kMaxDecorrelationMs   = 30.0f;
    static constexpr float kMaxLevel             = 2.0f;
    static constexpr float kMinCaptureSeconds    = 1.0f;
    static constexpr float kMaxCaptureSeconds    = 30.0f;
    static constexpr std::size_t kMinBlurFftSize = 256;
    static constexpr std::size_t kMaxBlurFftSize = 4096;
    static constexpr std::size_t kMinFreezeFftSize = 256;   // == spectral_freeze_oscillator.h:642
    static constexpr std::size_t kMaxFreezeFftSize = 8192;  // == :643
    static constexpr std::size_t kMinMaxBlockSamples = 64;
    static constexpr std::size_t kMaxMaxBlockSamples = 8192;

    // --- seed salts (FR-070). Disjoint by construction: kDriftSaltBase spans
    //     [0x4000, 0x4000 + kMaxGrains) and no other salt lands there.
    static constexpr std::size_t kGrainSalt     = 0x1000;
    static constexpr std::size_t kBlurSalt      = 0x2000;
    static constexpr std::size_t kSchedulerSalt = 0x3000;
    static constexpr std::size_t kDriftSaltBase = 0x4000;

    static constexpr std::uint32_t kDefaultSeed = 1u;
```

`static_assert`s carried in the header (structural clauses — if any of these moves the design's
arithmetic no longer holds):

```cpp
    static_assert(kControlChunkSamples % static_cast<std::size_t>(kDriftControlInterval) == 0,
                  "a control chunk must be a whole number of OU steps");
    static_assert(kMinAgeSamples >= kControlChunkSamples,
                  "FR-025's guard band must cover a whole control chunk: r is held constant "
                  "within a chunk, so the youngest age is only re-checked at chunk boundaries");
    static_assert(kMaxGrains <= 255,
                  "the active-index list stores slot indices in std::uint8_t");
    static_assert(kMaxGrains <= 64,
                  "getActiveSlotMask() returns one bit per slot in a std::uint64_t "
                  "(FR-072 / D-3); SC-004 lever (5) only ever REDUCES kMaxGrains, so "
                  "this constrains nothing the plan permits");
    static_assert(kEnvelopeTailZeroEntries >= 2 && kEnvelopeTailZeroEntries < kEnvelopeTableSize,
                  "FR-027's forced tail run must cover at least the last two entries");
    static_assert(kDriftSaltBase > kSchedulerSalt + kMaxGrains, "salt ranges must not overlap");
```

```cpp
    /// Prepare-time configuration (FR-009). Every field is validated in prepare();
    /// nothing here is read again afterwards except through the SNAPPED members.
    struct PrepareConfig {
        float       captureSeconds  = 8.0f;   // [1, 30]      RA-2's byte table applies
        bool        blurEnabled     = true;
        bool        freezeEnabled   = true;
        std::size_t blurFftSize     = 1024;   // [256, 4096], snapped DOWN to a power of two
        std::size_t freezeFftSize   = 2048;   // [256, 8192], snapped DOWN to a power of two
        std::size_t maxBlockSamples = 2048;   // [64, 8192]   sizes the blur output FIFO
    };
```

**FFT-size validation order (FR-009, binding):** clamp to the bounds **first**, then
`if (!std::has_single_bit(n)) n = std::bit_floor(n);`, then re-clamp to the lower bound. This is the
order `SpectralFreezeOscillator::prepare` already uses internally (`:107-113`); doing it in
`AtmosphereEngine::prepare` as well is what stops the engine from *keeping the unsnapped request* — FR-051
takes the freeze capture length from the oscillator's own `getFftSize()` (`:426-428`), and
`getLatencySamples()` (FR-046) reports the snapped `blurFftSize`. A request of 3000 must not leave a
capture length of 3000 disagreeing with an analysis length of 2048.

---

## 5. State layout

### 5.1 `AtmosphereGrain` (private nested, ~64 B)

```cpp
    /// One live grain. Deliberately NOT named `Grain`: `struct Grain` already
    /// exists at namespace scope (`primitives/grain_pool.h:23`).
    ///
    /// EVERYTHING PITCH-RELATED IS SNAPSHOTTED AT BIRTH (FR-009, Clarification Q2).
    /// No setter and no host automation can widen a live grain's ratio envelope
    /// past the one its lifetime was truncated for, which is what keeps FR-025 a
    /// closed-form guarantee with no runtime age clamp anywhere in the read path.
    struct AtmosphereGrain {
        std::uint64_t readIndexInt = 0;   ///< absolute source index, integer part (FR-013/024)
        float readFrac   = 0.0f;          ///< absolute source index, fraction in [0,1)
        float ratio      = 1.0f;          ///< r, recomputed at each control step, held within a chunk
        float staticSemis= 0.0f;          ///< s  (FR-031) snapshot
        float driftSemis = 0.0f;          ///< d  (FR-009 driftRangeSemitones) snapshot
        float semisLo    = 0.0f;          ///< clamp(s-d, +/-36) snapshot — the clamp bound for r
        float semisHi    = 0.0f;          ///< clamp(s+d, +/-36) snapshot
        float ratioMin   = 1.0f;          ///< semitonesToRatio(semisLo) snapshot
        float ratioMax   = 1.0f;          ///< semitonesToRatio(semisHi) snapshot
        float decorrAge  = 0.0f;          ///< dR, right-channel extra age in samples (FR-033)
        float panL       = 1.0f;          ///< equal-power gains, computed once at birth (FR-032)
        float panR       = 1.0f;
        float envPhaseInc= 0.0f;          ///< 1 / (L'-1)  (FR-026, D-4) — MULTIPLIED, never accumulated.
                                          ///< The denominator is L'-1, NOT L', so the last emitted
                                          ///< sample has phase EXACTLY 1.0 and lands on the forced
                                          ///< table tail. See S9.6 / D-13.
        std::uint32_t lifetime = 0;       ///< L' in samples (FR-025 truncation), always >= 2
        std::uint32_t ageSamples = 0;     ///< samples since birth; retirement is an INTEGER compare
        bool active = false;
    };
```

`readIndexInt` is `std::uint64_t` because FR-013 stores read positions in the **absolute** written-sample
domain, which never wraps (2⁶⁴ samples ≈ 12 million years at 48 kHz). `ageSamples`/`lifetime` are
`std::uint32_t`: the largest legal lifetime is 30 s × 96 kHz = 2 880 000 ≪ 2³².

### 5.2 `DriftLaneRng` and `GrainDriftLanes` (private nested)

```cpp
    /// Wrapper so `std::array<..., kMaxGrains>{}` is value-initialisable:
    /// `Xorshift32`'s only ctor is explicit (`core/random.h:45`). Named distinctly
    /// from `HarmonicCloud::LaneRng` (`:1121`) and `EntropyProcessor::LaneRng`
    /// (`processors/entropy_processor.h:423`) so a reader is never in doubt which
    /// one is in scope. This holds the REAL Layer 0 RNG — never a hand-rolled
    /// xorshift, which would silently desynchronise from BrownianDrift's streams.
    struct DriftLaneRng { Xorshift32 rng{1}; };

    /// kMaxGrains independent Ornstein-Uhlenbeck walks, SoA. Laid out exactly like
    /// `HarmonicCloud::DriftLanes` (`systems/harmonic_cloud.h:1125-1148`).
    /// NEVER kMaxGrains BrownianDrift objects (C-5).
    struct GrainDriftLanes {
        alignas(32) std::array<float, kMaxGrains> walk{};       ///< x_i
        alignas(32) std::array<float, kMaxGrains> smoothCur{};  ///< 150 ms one-pole current
        alignas(32) std::array<float, kMaxGrains> smoothTgt{};  ///< 150 ms one-pole target
        std::array<DriftLaneRng, kMaxGrains> rng{};
        float a = 0.0f;                  ///< AR(1) retention coefficient
        float g = 0.0f;                  ///< AR(1) innovation gain
        float depth = 1.0f;              ///< BrownianDrift::setDepth semantics
        int samplesUntilControl = 0;     ///< SHARED across the whole bank
        int   cachedPowN = 0;            ///< memo of std::pow(coeff, (float)N) — see harmonic_cloud.h:1940
        float cachedPowValue = 0.0f;
    };
```

`alignas(32)` is for **locality only**. Nothing here is loaded with an aligned SIMD op; there is no
Highway code in this component at all, so `tools/lint-simd-aligned-loadstore.js` has nothing to flag.

### 5.3 Engine members

```cpp
    // --- capture ----------------------------------------------------------
    RollingCaptureBuffer capture_;
    std::uint64_t writeCounter_ = 0;   ///< FR-013: total samples written, monotonic, never saturates
    std::size_t   captureCapacity_ = 0;///< cached capture_.getCapacitySamples()

    // --- grains -----------------------------------------------------------
    std::array<AtmosphereGrain, kMaxGrains> grains_{};
    std::array<std::uint8_t,    kMaxGrains> activeIdx_{};  ///< persistent, never rebuilt by scanning
    std::size_t activeCount_ = 0;
    std::size_t nextSlot_    = 0;                          ///< FR-020 round-robin cursor
    GrainScheduler scheduler_;
    GrainDriftLanes driftLanes_;
    float driftSmoothCoeff_ = 0.0f;                        ///< calculateOnePolCoefficient(150 ms, sr)

    // --- envelope ---------------------------------------------------------
    std::vector<float> envelopeTable_;                     ///< kEnvelopeTableSize, allocated in prepare
    GrainEnvelopeType envelopeType_ = GrainEnvelopeType::Hann;

    // --- blur -------------------------------------------------------------
    std::array<STFT, 2>           blurStft_;
    std::array<OverlapAdd, 2>     blurOla_;
    std::array<SpectralBuffer, 2> blurSpectrum_;
    std::array<std::vector<float>, 2> blurFifo_;           ///< power-of-two ring, see 11.3
    std::array<std::vector<float>, 2> fifoScratch_;        ///< blurHopSize_ pull target, see 11.3
    std::size_t blurFifoMask_ = 0, blurFifoWrite_ = 0, blurFifoRead_ = 0, blurFifoCount_ = 0;
    Xorshift32 blurRng_{1};                                ///< FR-044: SEPARATE from grainRng_
    OnePoleSmoother blurSmoother_;                         ///< advanced by advanceSamples(hopSize)
    std::size_t blurFftSize_ = 0, blurHopSize_ = 0;
    bool blurEnabled_ = false;

    // --- freeze -----------------------------------------------------------
    std::array<SpectralFreezeOscillator, 2> freezeOsc_;
    std::array<std::vector<float>, 2> freezeCapture_;      ///< getFftSize() scratch (FR-051)
    std::array<std::vector<float>, 2> freezeDelay_;        ///< blurFftSize-sample ring (FR-052)
    std::size_t freezeDelayMask_ = 0, freezeDelayIdx_ = 0;
    LinearRamp freezeMixRamp_;
    bool freezeEnabled_ = false;

    // --- output -----------------------------------------------------------
    OnePoleSmoother gainSmoother_;   ///< 1/sqrt(n) population compensation (FR-028)
    OnePoleSmoother levelSmoother_;  ///< FR-061
    float silenceGain_ = 1.0f;       ///< FR-007 ramp
    float silenceStep_ = 0.0f;
    enum class RunState : std::uint8_t { Running, Silencing, Latched };
    RunState runState_ = RunState::Running;
    float busPoisonAccum_ = 0.0f;    ///< FR-063: per-chunk sum of the pre-level bus; ONE finiteness
                                     ///< test per chunk instead of two per sample (§13.3)
    bool chunkPoisoned_ = false;     ///< FR-063 internal-non-finite flag, evaluated at chunk boundaries

    // --- control values (FR-009), all plain scalars ------------------------
    float grainSeconds_ = 4.0f, density_ = 4.0f, jitter_ = 0.5f;
    float positionSeconds_ = 1.0f, positionSpread_ = 0.3f;
    float pitchSemitones_ = 0.0f, pitchSpread_ = 0.15f;
    float driftDepth_ = 0.3f, driftSmoothness_ = 0.7f, driftRangeSemitones_ = 2.0f;
    float panSpread_ = 0.7f, decorrelation_ = 0.5f;
    float blur_ = 0.0f, freezeMix_ = 0.0f, level_ = 1.0f;   ///< the smoothers' TARGETS, kept so
                                                            ///< reset() can snap without re-deriving
    Xorshift32 grainRng_{1};
    std::uint32_t seed_ = kDefaultSeed;

    // --- clock, scratch, introspection ------------------------------------
    double sampleRate_ = 44100.0;
    std::uint64_t sampleCounter_ = 0;   ///< FR-005's ABSOLUTE control grid anchor
    bool prepared_ = false;
    std::array<float, kControlChunkSamples> busL_{}, busR_{};        // grain sum
    std::array<float, kControlChunkSamples> wetL_{}, wetR_{};        // post-blur
    std::array<float, kControlChunkSamples> freezeL_{}, freezeR_{};  // oscillator output
    // FR-072 counters
    std::uint64_t skipPoolFull_ = 0, skipRingCold_ = 0, totalBorn_ = 0;
    std::uint64_t totalRetired_ = 0;   ///< incremented at EVERY deactivation site (D-3)
    float minObservedAge_ = 0.0f, maxObservedAge_ = 0.0f;
    float lastBirthAge_ = 0.0f, lastBirthRatio_ = 1.0f;
    std::uint64_t lastBirthLifetime_ = 0;
    std::size_t lastBirthSlot_ = 0;                        ///< FR-020's round-robin, observable (D-3)
    float lastBirthPanL_ = 1.0f, lastBirthPanR_ = 1.0f;    ///< FR-032's law, observable (D-3)
```

**All per-slice scratch is `kControlChunkSamples` long, not `maxBlockSamples` long.** §8's processing is
control-chunk bounded, so 64-sample fixed arrays are sufficient, are members rather than allocations, and
remove `maxBlockSamples` from every hot-path bound. `maxBlockSamples` survives only as FR-043's FIFO
sizing input (§11.3).

---

## 6. Lifecycle

### 6.1 `prepare(double sampleRate, const PrepareConfig& config) noexcept`

The **only** non-RT-safe method. Calling it twice is legal and fully reconfigures. Order:

1. `sampleRate_ = sampleRate > 1.0 ? sampleRate : 1.0` (`BrownianDrift::prepare`'s floor, `:122` — a zero
   or negative rate would make `controlDtSeconds` non-finite).
2. Validate and store the config: `captureSeconds` clamped to `[1, 30]`; `maxBlockSamples` clamped to
   `[64, 8192]`; `blurFftSize` clamped to `[256, 4096]` then `bit_floor`-snapped then re-clamped;
   `freezeFftSize` clamped to `[256, 8192]` then snapped then re-clamped. `blurHopSize_ = blurFftSize_/4`.
3. `capture_.prepare(sampleRate_, captureSeconds)`; `captureCapacity_ = capture_.getCapacitySamples()`.
4. `envelopeTable_.assign(kEnvelopeTableSize, 0.0f)` then `regenerateEnvelope()` (§9.6).
5. `scheduler_.prepare(sampleRate_)`.
6. `driftSmoothCoeff_ = calculateOnePolCoefficient(kDriftOutputSmoothMs, (float)sampleRate_)`
   (`smoother.h:77`, the same call `harmonic_cloud.h:296-297` makes);
   `updateDriftCoefficients(driftLanes_, driftSmoothness_)` (§10.1).
7. If `config.blurEnabled`: for `ch ∈ {0,1}` —
   `blurStft_[ch].prepare(blurFftSize_, blurHopSize_, WindowType::Hann)`;
   `blurOla_[ch].prepare(blurFftSize_, blurHopSize_, WindowType::Hann, 9.0f, /*applySynthesisWindow=*/true)`;
   `blurSpectrum_[ch].prepare(blurFftSize_)`;
   `blurFifo_[ch].assign(fifoCapacity, 0.0f)` with `fifoCapacity` from §11.3.
   Else leave every blur object unprepared and every vector empty (FR-045).
8. If `config.freezeEnabled`: for `ch ∈ {0,1}` — `freezeOsc_[ch].prepare(sampleRate_, freezeFftSize_)`;
   `freezeCapture_[ch].assign(freezeOsc_[ch].getFftSize(), 0.0f)` — **the oscillator's own snapped size**
   (FR-051, trap 15). If **both** blur and freeze are enabled, `freezeDelay_[ch].assign(blurFftSize_, 0.0f)`
   and `freezeDelayMask_ = blurFftSize_ - 1` (a power of two after snapping); otherwise the delay is not
   allocated and not traversed (FR-045, FR-052).
9. Configure smoothers against `sampleRate_`: `gainSmoother_.configure(kGainSmoothMs, sr)`,
   `levelSmoother_.configure(kLevelSmoothMs, sr)`, `blurSmoother_.configure(kBlurSmoothMs, sr)`,
   `freezeMixRamp_.configure(kFreezeMixRampMs, sr)`.
10. `silenceStep_ = 1.0f / max(1.0f, kSilenceRampMs * 0.001f * (float)sampleRate_)`.
11. `prepared_ = true;` then `reset()`.

A freshly prepared engine is **silent** (empty ring) with every control value at its FR-009 default.

### 6.2 `reset() noexcept` — allocation-free, returns the exact post-`prepare` state

1. `capture_.reset()` (`:96-101`); `writeCounter_ = 0`; `sampleCounter_ = 0`.
2. `grains_.fill(AtmosphereGrain{})`; `activeCount_ = 0`; `nextSlot_ = 0`.
3. `scheduler_.reset()` **and then** `scheduler_.seed(deriveStreamSeed(seed_, kSchedulerSalt))`.
   The second call is not optional: `GrainScheduler::reset()`/`prepare()` never touch `rng_`
   (`:33-42`), only `seed()` does (`:97`), so without it the jitter stream resumes mid-sequence and the
   post-`reset` render does not match the original (FR-006, trap 6).
4. `grainRng_.seed(deriveStreamSeed(seed_, kGrainSalt))`; `blurRng_.seed(deriveStreamSeed(seed_, kBlurSalt))`.
5. `resetDriftLanes()` — zero `walk`/`smoothCur`/`smoothTgt`, `samplesUntilControl = 0`, clear the pow
   memo (`harmonic_cloud.h:2014-2021`), **and** re-seed every lane:
   `driftLanes_.rng[i].rng.seed(deriveStreamSeed(seed_, kDriftSaltBase + i))`. Re-seeding on `reset` is
   `BrownianDrift::reset()`'s documented behaviour (`:133-135` → `:243`); a **grain birth** is the case
   that must not re-seed (§10.4).
6. Blur — **guarded on `blurEnabled_`**, because when blur is disabled no FIFO was allocated and a
   non-zero occupancy would describe a buffer that does not exist:
   ```
   if (blurEnabled_) {
       blurStft_[ch].reset(); blurOla_[ch].reset(); blurSpectrum_[ch].reset();
       std::fill(blurFifo_[ch].begin(), blurFifo_[ch].end(), 0.0f);
       blurFifoRead_  = 0;
       blurFifoWrite_ = blurFftSize_ & blurFifoMask_;      // == blurFftSize_ (capacity > fftSize)
       blurFifoCount_ = blurFftSize_;
   } else {
       blurFifoRead_ = blurFifoWrite_ = blurFifoCount_ = 0;
   }
   ```
   The pre-fill of `blurFftSize_` **zeros** is what makes the layer's latency exactly `blurFftSize_` and
   keeps the first block's pop from underflowing (§11.4's occupancy trace). `blurFifoWrite_` must lead
   `blurFifoRead_` by the occupancy, not equal it: the ring's invariant is
   `blurFifoWrite_ == (blurFifoRead_ + blurFifoCount_) & blurFifoMask_` (§11.3). Setting both cursors to
   0 with a non-zero count leaves the reader permanently `blurFftSize_` indices AHEAD of the writer — the
   first 16 chunks at the default geometry pop the 1024 pre-fill zeros and advance `read` to 1024 while
   `write` is still 0, after which every pop returns data written a lap earlier. The real latency becomes
   the FIFO capacity (4096), not the `blurFftSize_` that `getLatencySamples()` reports (FR-046), and both
   SC-006 (delay-compensated by `getLatencySamples()`) and SC-007 (crossfade alignment, §12.2) fail —
   presenting as a COLA/windowing bug rather than a FIFO-init bug. Pinned here for that reason.
7. Freeze: `for (ch ∈ {0,1}) freezeOsc_[ch].reset();` — the oscillator's own public, documented
   real-time-safe, non-allocating rewind (`spectral_freeze_oscillator.h:173-196`; nine `std::fill`s plus
   `workingSpectrum_.reset()` / `formantPreserver_.reset()`, and it clears `frozen_`, `unfreezing_`,
   `unfadeSamplesRemaining_`, `outputWriteIndex_`, `outputReadIndex_` and `samplesInBuffer_`). It
   early-outs on `!prepared_` (`:174`), so it is safe to call unconditionally. The freeze-leg delay ring
   is then zero-filled and `freezeDelayIdx_ = 0`. §12.4 records why the earlier `unfreeze()`+drain
   substitute was wrong.
8. Smoothers: `gainSmoother_.snapTo(1.0f)` (n = 0 ⇒ target 1), `levelSmoother_.snapTo(level_)`,
   `blurSmoother_.snapTo(blur_)`, `freezeMixRamp_.snapTo(freezeMix_)` — "snapped to their current
   targets" (FR-006).
9. `silenceGain_ = 1.0f`; `runState_ = RunState::Running`; `chunkPoisoned_ = false`;
   `busPoisonAccum_ = 0.0f`.
10. Counters: `skipPoolFull_ = skipRingCold_ = totalBorn_ = totalRetired_ = 0`;
    `minObservedAge_ = (float)captureCapacity_`; `maxObservedAge_ = 0.0f`;
    `lastBirthAge_ = 0.0f`; `lastBirthRatio_ = 1.0f`; `lastBirthLifetime_ = 0`;
    `lastBirthSlot_ = 0`; `lastBirthPanL_ = lastBirthPanR_ = 1.0f`.

`minObservedAge_` is seeded at the ring capacity rather than at an "infinite" sentinel: no
`std::numeric_limits<float>::infinity()` may appear anywhere (SC-013's scripted grep gate), and any real
observation is `≤ C − 2 < C`, so the first observation always lowers it. Both accessors are documented
meaningless until `getTotalGrainsBorn() > 0`.

### 6.3 `silence() noexcept` — the latch (FR-007)

```
if (runState_ == RunState::Latched) return;          // idempotent
runState_ = RunState::Silencing;
// silenceGain_ keeps its current value; the ramp continues from where it is.
```

While `Silencing`, the engine processes **normally** and multiplies the final output by `silenceGain_`,
which decrements by `silenceStep_` per sample. When `silenceGain_ ≤ 0`:
`silenceGain_ = 0.0f`; every grain is deactivated (`activeCount_ = 0`, all `active = false`, and
`totalRetired_ += activeCount_` **before** the count is zeroed, so FR-072's `retired + active == born`
identity holds through the latch as well as through ordinary retirement — §9.7);
`runState_ = RunState::Latched`.

While `Latched`, `processStereoBlock` writes exactly `0.0f` to both outputs and **returns immediately**:
no capture, no scheduler tick, no grain ageing, no drift-lane advance, no control-grid advance, no
counter movement. `getActiveGrainCount()` reads 0 and neither skip counter moves, so a latched engine
costs the zero-fill and nothing else.

**`reset()` — or a fresh `prepare()` — is the one documented re-entry.** There is no `resume()` and no
auto-resume, including after FR-063's internal-non-finite trip (§13.3).

---

## 7. Control table — setters (FR-009)

Every setter is `noexcept`, sanitises a non-finite argument to the field's **default**, then clamps.
The sanitiser is the shared helper composition from §13.2; no setter writes a new bit test.

| Setter | Clamp | Default | Stored into | Smoothing / cadence |
|---|---|---|---|---|
| `setGrainSeconds(float)` | `[0.05, 30]` | 4.0 | `grainSeconds_` | none; read at birth only |
| `setDensity(float)` | `[0.1, 20]` | 4.0 | `density_` | pushed to `scheduler_.setDensity` at the next control step |
| `setJitter(float)` | `[0, 1]` | 0.5 | `jitter_` | pushed to `scheduler_.setJitter` at the next control step |
| `setPositionSeconds(float)` | `[0, 30]` | 1.0 | `positionSeconds_` | none; read at birth only |
| `setPositionSpread(float)` | `[0, 1]` | 0.3 | `positionSpread_` | none; read at birth only |
| `setPitchSemitones(float)` | `[-24, +24]` | 0.0 | `pitchSemitones_` | none; **snapshotted at birth** |
| `setPitchSpread(float)` | `[0, 1]` | 0.15 | `pitchSpread_` | none; **snapshotted at birth** |
| `setDriftDepth(float)` | `[0, 1]` | 0.3 | `driftLanes_.depth` | live, whole bank |
| `setDriftSmoothness(float)` | `[0, 1]` | 0.7 | `driftSmoothness_` + `updateDriftCoefficients` | live, whole bank |
| `setDriftRangeSemitones(float)` | `[0, 12]` | 2.0 | `driftRangeSemitones_` | none; **snapshotted at birth** |
| `setPanSpread(float)` | `[0, 1]` | 0.7 | `panSpread_` | none; read at birth only |
| `setDecorrelation(float)` | `[0, 1]` | 0.5 | `decorrelation_` | none; read at birth only (→ 0…30 ms) |
| `setBlur(float)` | `[0, 1]` | 0.0 | `blurSmoother_.setTarget` | 50 ms, advanced by `advanceSamples(blurHopSize_)` **immediately before** each frame's value is read |
| `setFreezeMix(float)` | `[0, 1]` | 0.0 | `freezeMixRamp_.setTarget` | 100 ms `LinearRamp`, `process()` per output sample |
| `setLevel(float)` | `[0, 2]` | 1.0 | `levelSmoother_.setTarget` | 20 ms, `process()` per output sample |
| `setGrainEnvelope(GrainEnvelopeType)` | enum | `Hann` | `envelopeType_` + `regenerateEnvelope()` | none; **early-out on an unchanged value**, then **in place**, never resizes |
| `setSeed(std::uint32_t)` | any | 1 | `seed_` + re-seeds all streams (§14.1) | none |

**Snapshot rule (binding).** `pitchSemitones_`, `pitchSpread_` and `driftRangeSemitones_` are read **only**
inside `birthGrain()`. Everything derived from them — `staticSemis`, `driftSemis`, `semisLo`/`semisHi`,
`ratioMin`/`ratioMax` and the truncated lifetime `lifetime` — is frozen into the grain at birth. Changing
any of the three affects **only grains born afterwards**. The cost is deliberate: at
`grainSeconds = 30` the pitch controls take up to a grain lifetime to fully take effect. The drift **lane
value** stays live; only the range that scales it is frozen. This is what keeps FR-025 closed-form — no
setter and no host automation can widen an in-flight grain's ratio envelope past the one its lifetime was
truncated for, so there is **no runtime age clamp and no per-sample saturation anywhere in the grain read
path**.

**`setGrainEnvelope` on the audio thread** regenerates `kEnvelopeTableSize` entries in place. That is
O(4096) transcendentals: `std::cos` per entry for `Hann` (`grain_envelope.h:47-50`), `Sine` (`:79-82`)
and `Blackman` (`:88-94`), and for `Exponential` roughly **two** `std::exp` per attack entry, because
`endValue = 1 - std::exp(-kTimeConstant)` is recomputed inside the loop (`:141-142`). Bounded and
allocation-free, but not cheap — so two things are binding rather than advisory:

```cpp
    void setGrainEnvelope(GrainEnvelopeType type) noexcept {
        if (type == envelopeType_) { return; }   // idempotence guard, NOT optional
        envelopeType_ = type;
        regenerateEnvelope();
    }
```

1. **The idempotence guard.** A host that re-sends an unchanged parameter every block — the common case
   for an automation lane parked on a value — would otherwise repay the full 4096-entry cost every block
   for no observable change.
2. **It is measured, not asserted.** SC-004 configuration **(e)** (§15.4) calls `setGrainEnvelope` with an
   *alternating* type once per 512-sample block, so the worst case the guard cannot elide is gated against
   the same 1 % reference as the other four configurations. Without (e) a cost the roadmap treats as a
   functional requirement (106,667 ns per block) would be entirely ungated.

The header still states that the setter is intended to be called at most once per block.

---

## 8. `processStereoBlock` — the exact loop

```cpp
void processStereoBlock(const float* inLeft, const float* inRight,
                        float* outLeft, float* outRight,
                        std::size_t numSamples) noexcept;
```

Shape identical to `ContinuousBody::processStereoBlock` (`systems/continuous_body.h:1161-1163`) so Phase 7
chains them without an adapter. **In-place is not supported** (documented precondition): the engine reads
all of the input for capture before writing output.

Guards, in this order (mirroring `continuous_body.h:1166-1180`):

1. any of the four pointers null → **write nothing**, return (FR-004);
2. `numSamples == 0` → no-op, and the control grid does **not** advance (FR-004);
3. `!prepared_` → zero-fill both outputs, return;
4. `runState_ == Latched` → zero-fill both outputs, return, advancing nothing (FR-007).

Then:

```
std::size_t done = 0;
while (done < numSamples) {
    const std::size_t toGrid = kControlChunkSamples
                             - static_cast<std::size_t>(sampleCounter_ % kControlChunkSamples);
    const std::size_t n = std::min(numSamples - done, toGrid);

    // --- (A) control step, only on an exact grid boundary -----------------
    if ((sampleCounter_ % kControlChunkSamples) == 0) {
        runControlStep();                 // section 8.1
    }

    // --- (B) per-sample: capture, schedule, accumulate --------------------
    renderGrainChunk(inLeft + done, inRight + done, n);   // section 9.7 -> busL_/busR_

    // --- (C) blur (block-rate, <= 64 samples pushed per pump) ------------
    if (blurEnabled_) pumpBlur(n);        // section 11.4 -> wetL_/wetR_
    else              { copy busL_/busR_ -> wetL_/wetR_ }

    // --- (D) freeze leg + crossfade + level + silence ramp ---------------
    finishChunk(outLeft + done, outRight + done, n);      // section 12.3 / 13.1

    sampleCounter_ += n;
    done += n;
}
```

**The grid is anchored to `sampleCounter_`, not to block starts.** A chunk split 36 + 28 by a block
boundary yields exactly the same control step as an unsplit 64, which is what SC-011 measures. The `n`
computed above is `≤ 64` always, so:

- every scratch array is a fixed 64-sample member (§5.3);
- the blur pump never pushes more than 64 samples between drains, which is what keeps
  `STFT::samplesAvailable_` below `8·fftSize` at every legal geometry (trap 1, deviation **D-2**);
- `maxBlockSamples` never bounds anything on the hot path — FR-009's "processed in `maxBlockSamples`-sized
  internal slices" is satisfied a fortiori by 64-sample slices.

### 8.1 `runControlStep()` — what happens on the 64-sample grid (FR-005)

In this fixed order (the order is part of the determinism contract, FR-071):

1. `scheduler_.setDensity(density_)`; `scheduler_.setJitter(jitter_)` — cheap, idempotent, and keeps the
   scheduler's `interonsetSamples_` in step with the control table without a dirty flag.
2. `advanceDriftLanes(kControlChunkSamples)` (§10.3) — **two** OU steps per chunk, because
   `kDriftControlInterval = 32`.
3. For every **live** grain: recompute `ratio` from the lane (§9.5).
4. `gainSmoother_.setTarget(1.0f / std::sqrt((float)std::max<std::size_t>(1, activeCount_)))` (FR-028).
5. Age bookkeeping: fold each live grain's current age into `minObservedAge_`/`maxObservedAge_` (§9.8).
6. Evaluate the previous chunk's poison accumulator — `chunkPoisoned_ = !isFinite(busPoisonAccum_)`,
   then `busPoisonAccum_ = 0.0f` — and if set, fire `silence()`, retire under the FR-007 ramp and clear
   the flag (§13.3). This is **one** `isFinite` call per 64 samples, not two per sample; §13.2 explains
   why the call count matters.

Step 2 runs on the grid **before** step 3, so a grain born mid-chunk sees the lane value as of the most
recent **completed** control step and birth timing inside a chunk cannot change the value it sees
(FR-030).

---

## 9. Grain engine

### 9.1 Capture (FR-010 … FR-014)

Inside `renderGrainChunk`, **before** anything reads the ring. `isFinite` is non-inlinable (§13.2), so
the sanitiser is written as a **two-pass, chunk-granular** form: one call per ≤64-sample chunk on the
common path, with a per-sample fallback taken only when the chunk actually contains a non-finite value.

```
// Pass 1: n adds, no calls. NaN and +/-Inf both propagate through `+`, and
// (+Inf) + (-Inf) is NaN -- still non-finite, so nothing can cancel out.
float probe = 0.0f;
for (i in [0, n)) probe += inLeft[i] + inRight[i];

if (isFinite(probe)) {                       // ONE call per chunk, common path
    for (i in [0, n)) {
        capture_.writeStereo(inLeft[i], inRight[i]);   // rolling_capture_buffer.h:113
        ++writeCounter_;                               // FR-013 monotonic uint64
    }
} else {                                     // rare path: per-sample substitution
    for (i in [0, n)) {
        float l = inLeft[i], r = inRight[i];
        if (!isFinite(l)) l = 0.0f;          // FR-063 substitution; ring is PRESERVED
        if (!isFinite(r)) r = 0.0f;
        capture_.writeStereo(l, r);
        ++writeCounter_;
    }
}
```

The observable behaviour is exactly FR-063's: each non-finite input sample is substituted with `0.0f`
for both capture and output, per channel, and the ring is preserved. Only the *cost* of proving a chunk
clean changes. A `probe` that overflows to `±Inf` from finite inputs (impossible for host audio over 64
samples, but not excluded by the type) merely takes the slow path, which is correct.

`writeCounter_` exists because `RollingCaptureBuffer::getSamplesWritten()` saturates at capacity
(`:119-121`) and cannot serve as an absolute clock. After the write, the newest sample has absolute index
`writeCounter_ - 1` and age 0 — this is the identity every age computation below uses.

Because the write precedes every read for that sample, a grain may legitimately read audio produced in
the same block. That is the self-granulation the roadmap asks for (line 235).

### 9.2 Scheduling and the two skip causes (FR-021, FR-023, FR-072)

Per sample, after the capture write:

```
if (scheduler_.process()) {          // grain_scheduler.h:73, one rng draw on trigger
    tryBirthGrain();
}
```

`tryBirthGrain()` returns without touching the grain RNG in exactly two cases, each with its **own**
counter — a single counter could not distinguish them, and SC-001/SC-003's preconditions need the
pool-full one specifically:

- **ring cold** — `capture_.getAvailableSamples()` short of the birth requirement, or the FR-025 headroom
  `H ≤ 0`, or a truncated lifetime below 1 sample. `++skipRingCold_`.
- **pool full** — a full round-robin sweep found no inactive slot. `++skipPoolFull_`. **No in-flight grain
  is ever reset, truncated or reused** (FR-023): stealing a 30 s grain mid-envelope is a guaranteed click
  (contrast `GrainPool::acquireGrain`, `grain_pool.h:71-91`).

Ordering note: the slot sweep runs **first** (64 integer tests, no RNG), then the birth draws are taken.
This keeps the grain RNG stream a function of *successful* births only, which is what makes
`getGrainRngState()` a usable determinism probe (SC-010's FR-044 clause). The **ring-cold** rejection is
evaluated *after* the draws, because its threshold depends on `a₀` and `dR`, which are drawn — so a
ring-cold skip **does** consume four draws. That asymmetry is pinned here rather than discovered later.

### 9.3 Slot allocation — round-robin (FR-020)

```
std::size_t slot = kMaxGrains;
for (std::size_t k = 0; k < kMaxGrains; ++k) {
    const std::size_t s = (nextSlot_ + k) % kMaxGrains;
    if (!grains_[s].active) { slot = s; break; }
}
if (slot == kMaxGrains) { ++skipPoolFull_; return; }
nextSlot_ = (slot + 1) % kMaxGrains;
```

First-free allocation would concentrate every grain on the low `density × grainSeconds` slots — ~16 of 64
at the defaults — so the same drift lanes (lane `i` is bound to slot `i`) would be reused over and over
while the upper lanes idle, and successive grains would be serially correlated in pitch motion.
Round-robin spreads births across all 64 lanes, which is what makes the cloud decorrelate. `nextSlot_` is
part of the deterministic state and `reset()` returns it to 0.

The active list is maintained, never rebuilt by scanning (contrast `GrainPool::activeGrains()`,
`grain_pool.h:107-116`, which `GranularEngine` calls once per **sample**, `granular_engine.h:213`):
activation appends `slot` at `activeIdx_[activeCount_++]`; retirement swaps the retiring entry with
`activeIdx_[--activeCount_]`.

### 9.4 Birth — exact draw order and the liveness arithmetic (FR-025, FR-029, FR-031 … FR-033)

**Draw order is fixed and documented**, because `reset()` and SC-010 must reproduce every draw. All four
come off `grainRng_` in this sequence; inserting, removing or reordering one re-shuffles every subsequent
grain's parameters:

```
const float uPos   = grainRng_.nextFloat();     // [-1,1]  position spread
const float uPitch = grainRng_.nextFloat();     // [-1,1]  static detune
const float uPan   = grainRng_.nextFloat();     // [-1,1]  pan
const float uDec   = grainRng_.nextUnipolar();  // [0,1]   decorrelation offset
```

Then, in order:

**(a) Snapshot the pitch envelope (FR-031).**

```
s       = clamp(pitchSemitones_ + uPitch * pitchSpread_ * 12.0f, ±36)   // 1200 cents == 12 semitones
d       = driftRangeSemitones_
semisLo = clamp(s - d, ±36)              // ±36 == kMaxAbsGrainSemitones
semisHi = clamp(s + d, ±36)
rMin    = semitonesToRatio(semisLo)      // core/pitch_utils.h:23, via ratioAtPitch()
rMax    = semitonesToRatio(semisHi)
```

The ±36 clamp is applied to the **envelope endpoints**, not only to `s`, so `r ∈ [0.125, 8]` at every
instant of the grain's life and `rMin`/`rMax` are well defined and fixed for that life.

**(b) Decorrelation offset (FR-033).**

```
dR = decorrelation_ * kMaxDecorrelationMs * 0.001f * (float)sampleRate_ * uDec    // samples, >= 0
```

The right channel reads at `ageL + dR`, i.e. L and R read **different points of the ring**. This is a
genuine decorrelator, unlike `stereoCrossBlend`, which cannot decorrelate two correlated inputs (C-7).

**(c) Liveness arithmetic (FR-025).** With `C = captureCapacity_`, `g = kMinAgeSamples = 64`,
`L = round(grainSeconds_ · sampleRate_)`:

```
wUp   = max(rMax - 1, 0)          // age SHRINKS at this rate (reads forward faster than the write head)
wDown = max(1 - rMin, 0)          // age GROWS at this rate
w     = wUp + wDown               // Clarification Q1: the SUM, never the maximum
H     = C - 2 - g - dR            // headroom the two excursions must share
if (H <= 2)                 -> ring too short for this offset: ++skipRingCold_, return
Hs    = H - 2                     // CEILING SLACK, reserved -- see the convergence proof below
L'    = (w * L > Hs) ? floor(Hs / w) : L
if (L' < 2)                 -> ++skipRingCold_, return      // L' >= 2: FR-026's 1/(L'-1) phase
aLo   = ceil(wUp   * L') + g
aHi   = C - 2 - ceil(wDown * L') - dR
```

**Convergence proof, with the ceilings counted.** Subtracting,

```
aHi - aLo = (C - 2 - dR - g) - ceil(wUp * L') - ceil(wDown * L')
          = H - ceil(wUp * L') - ceil(wDown * L')
```

Each `ceil` adds strictly less than 1 to its argument, so `ceil(wUp·L') + ceil(wDown·L') < w·L' + 2`, and
therefore `aHi − aLo > H − w·L' − 2`. **Without the reserved slack** (`L' = floor(H/w)`, so only
`w·L' ≤ H`) that bound is `> −2`: the window can invert by one sample. It is reachable inside the spec's
own ranges — `wUp = 0.3`, `wDown = 0.4` (`s ≈ −2.15`, `d ≈ 6.7` semitones, both legal), `H = 524222`,
`L' = floor(H/0.7) = 748888` gives `ceil(wUp·L') = 224667` and `ceil(wDown·L') = 299556`, summing to
524223 > H, i.e. `aLo = aHi + 1`. Step (d)'s `clamp(a0, aLo, aHi)` with `lo > hi` is a **precondition
violation of `std::clamp`** — undefined behaviour — and it lands squarely in the truncation-binding,
straddling case SC-002 clauses 2/3 sweep.

Reserving the two samples in the truncation removes it: `w·L' ≤ Hs = H − 2`, hence
`aHi − aLo > H − (H − 2) − 2 = 0` … and since both sides are integers, `aHi ≥ aLo`. The same threshold
`Hs` gates the *non*-truncating branch (`w·L > Hs`, not `w·L > H`), because a lifetime in the gap
`(Hs, H]` would carry the same one-sample inversion. When `w = 0` nothing truncates (`0 > Hs` is false),
`aLo = g` and `aHi = C − 2 − dR`, so `aHi − aLo = H ≥ 0`. The rule therefore converges in **one** step
for every envelope, straddling or not, and never produces an empty window. Deviation **D-12**.

`L' ≥ 2` rather than `L' ≥ 1`: FR-026's envelope phase denominator is `L' − 1` (step (g), §9.6), so a
one-sample grain has no defined phase. A grain that truncates to a single sample is inaudible by
construction, so rejecting it as ring-cold costs nothing.

*Why the sum and not the maximum.* When the envelope straddles `r = 1` both terms are non-zero. At
`s = 0, d = 2`: `rMin = 0.8909`, `rMax = 1.1225`, sum `= 0.2316` against a maximum of `0.1225`. A
maximum-based `w` under-truncates by ~2×, leaves the birth window empty after substitution, and makes the
rule non-convergent in exactly the configuration SC-002 clause 3 sweeps. Whenever the envelope does not
straddle `r = 1` at most one term is non-zero, so the sum reduces to `|1 − r|` when `d = 0` and the
drift-free closed form SC-002 clause 2 asserts is unchanged.

*Why `dR` appears in `H` and in `aHi`.* FR-033 states that the offset "participates in FR-025's clamp (the
larger of the two ages is the one bounded)", but FR-025's stated closed form omits it. Including it is the
only way the invariant can hold for the right channel. With `dR = 0` the expression reduces to
`⌊(C − 2 − g − 2)/w⌋` — FR-025's `⌊(C − 2 − g)/w⌋` less the two samples D-12 reserves for the ceilings —
so SC-002's closed-form clauses are exact provided they pin `decorrelation = 0` (which §15.2 requires) and
are written against **that** expression. Deviations **D-1** (the `dR` term) and **D-12** (the `− 2`).

**(d) Birth age, clamped into the window (FR-029).**

```
a0 = positionSeconds_ * (float)sampleRate_ * (1.0f + uPos * positionSpread_)
a0 = clamp(a0, (float)aLo, (float)aHi)
```

**(e) FR-014 admission.**

```
if (capture_.getAvailableSamples() < (size_t)ceil(a0 + dR) + kInterpMarginSamples)   // + 2
    -> ++skipRingCold_, return
```

Checked **after** the clamp so it tests the age actually used, and against `a0 + dR` because the right
channel reads the older point.

**The margin is `kInterpMarginSamples = 2`, not `g = 64`, and that is a correction, not a relaxation.**
FR-014 as written requires `available ≥ birth read age + kMinAgeSamples` (spec.md:471-473), but `g` is
**already** the *young-side* guard, baked into `aLo = ceil(wUp·L') + g` by step (c). Requiring it a second
time on the *old* side makes the two rules jointly unsatisfiable wherever `ceil(wDown·L')` is small.
Worked case, entirely inside §15.2's own sweep: `captureSeconds = 1` at 48 kHz ⇒ `C = 65536`;
`grainSeconds = 30`, `pitchSemitones = +24`, no drift, `dR = 0` ⇒ `r = rMin = rMax = 4`, `wUp = 3`,
`wDown = 0`, `w = 3`; `H = 65470`, `Hs = 65468`, `L' = 21822`; `aLo = ceil(3·21822) + 64 = 65530`,
`aHi = 65534`, so `a0 ∈ [65530, 65534]`. With a `+ g` margin the admission demands
`available ≥ 65530 + 64 = 65594 > C` — unreachable at **any** point in **any** render, so no grain is
ever born and `skipRingCold_` climbs forever. That contradicts the spec's own edge case "`positionSeconds`
> `captureSeconds` → clamped by FR-025's window, never an out-of-range read" (spec.md:1311), which expects
grains, not permanent silence. With `+ 2` the same cell admits once the ring is full
(`available = 65536 ≥ 65534 + 2`), which is ~1.37 s after `prepare`.

Two is exactly what the read needs and no less: `readStereoLinear` interpolates between ages
`floor(age)` and `floor(age) + 1` and clamps at `getAvailableSamples() − 2` (§2), so `available ≥ age + 2`
is the precise condition under which the clamp does not engage. Nothing about the *young*-side guarantee
changes — `g` still sits in `aLo`, so no grain ever reads within 64 samples of the write head, and §9.10's
proof is restated against the `+ 2` form. Deviation **D-11**.

**(f) Pan (FR-032).** The same equal-power law `GrainProcessor` uses (`grain_processor.h:101-103`),
computed **once**:

```
pan     = panSpread_ * uPan;                  // [-spread, +spread]
panNorm = (pan + 1.0f) * 0.5f;
panL    = std::cos(panNorm * kHalfPi);
panR    = std::sin(panNorm * kHalfPi);
```

Hoisting these two transcendentals to birth is SC-004 lever (4): a regression that moved them per-sample
is the single largest cost lever in the component.

**(g) Commit — the read position.**

```
readIndexInt = writeCounter_ - 1 - (uint64)ceil(a0);      // absolute index; age 0 == writeCounter_-1
readFrac     = (float)(ceil(a0) - a0);                    // in [0,1) => ageL == a0 exactly at sample 0
ratio        = ratioAtPitch(clamp(s + smoothCur[slot]*d, semisLo, semisHi));
lifetime     = (uint32)L';
envPhaseInc  = 1.0f / (float)(L' - 1);      // FR-026 phase denominator; L' >= 2 by step (c)
ageSamples   = 0;
active       = true;
```

The `ceil` form (not `floor`) is required so `readFrac` stays non-negative: `ageL = (writeCounter_ − 1) −
(readIndexInt + readFrac)`, so `readIndexInt` must sit at or **before** the target position and `readFrac`
takes up the remainder.

**(h) Drift-lane birth semantics (FR-030, Clarification Q8).** Zero lane `slot`'s walk state — `walk`,
`smoothCur` and `smoothTgt` all set to 0 — and **do not re-seed** its `Xorshift32`. Consequences, all
intended: every grain starts at exactly its snapshotted static pitch `s` and drifts away over its life (the
plainest reading of the roadmap's "pitch drift per grain", line 243); there is no birth-time pitch step of
up to `±d` from whatever value a free-running lane happened to hold; and successive grains on the same slot
are not handed the same walk sequence, because the stream position is preserved. Combined with
round-robin allocation, successive grains are decorrelated both across lanes and along each lane. This is
deliberately **not** a `BrownianDrift::reset()`-equivalent: that call re-seeds
(`brownian_drift.h:133-135` → `:243`), which would make every grain on a slot replay one identical walk.
Only `prepare`, `reset` and `setSeed` re-seed a lane.

**(i) Introspection.** `lastBirthAge_ = a0`; `lastBirthRatio_ = ratio`; `lastBirthLifetime_ = L'`;
`lastBirthSlot_ = slot`; `lastBirthPanL_ = panL`; `lastBirthPanR_ = panR`; `++totalBorn_`; fold `a0` and
`a0 + dR` into the observed-age extremes. The last three are the additions D-3 records: without
`lastBirthSlot_` nothing observes FR-020's round-robin, and without the two pan gains nothing observes
FR-032's equal-power law — both are plain scalar stores at birth, on a path that already writes three.

### 9.5 Ratio recomputation at each control step (FR-024, FR-030)

For every live grain, once per 64-sample control chunk:

```
const float p = std::clamp(g.staticSemis + driftLanes_.smoothCur[slot] * g.driftSemis,
                           g.semisLo, g.semisHi);
g.ratio = ratioAtPitch(p);
```

The clamp to the **snapshotted** `[semisLo, semisHi]` is load-bearing, not belt-and-braces. The lane value
is clamped to `[-1,+1]` (as `BrownianDrift::getCurrentValue()` is, `:212-214`), so `lane·d` is
mathematically within `±d` — but the float add `s + lane·d` can round to 1 ULP above `s + d`. Clamping the
*pitch* to the two floats the envelope was built from makes `r ∈ [rMin, rMax]` **exactly**, because
`ratioAtPitch` is monotone and deterministic. Without it, a 1-ULP overshoot accumulated over 1.44 M
samples is ≈0.14 samples of extra age, enough to fail SC-002's `max ≤ C − 2` assertion whenever the floor
division leaves zero slack.

```cpp
    /// The single point at which a semitone offset becomes a playback ratio.
    /// FR-024 names `semitonesToRatio` (`core/pitch_utils.h:23`), and BOTH the
    /// birth-time envelope (rMin/rMax) and the per-control-step ratio go through
    /// HERE, so monotone consistency between them is structural rather than a
    /// convention a later edit can break.
    [[nodiscard]] static float ratioAtPitch(float semitones) noexcept {
        return semitonesToRatio(semitones);
    }
```

**Cost, and the recorded lever.** At saturation this is 64 grains × 8 chunks = **512 `std::pow` calls per
512-sample block** — the same call count that took `HarmonicCloud` over budget, and the first of the three
levers it spent to get back inside. The measured figures for all three levers **combined**, from the file
itself (`harmonic_cloud_perf_test.cpp:99-100`), are **31,281–32,027 → 20,641–23,154 ns/block static** and
**33,257–34,184 → 21,917–25,262 ns/block automated**. (An earlier draft of this plan quoted
"35,052 → 26,000": 35,052 is the *first honest measurement* of the static configuration recorded in prose
at `:137`, and 26,000 is the checked-in baseline constant `kAutomatedBaselineNsPerBlock` at `:140` — a
threshold, not a measurement. Neither is a before/after pair, and neither appears in `:82-100`.)

**The attribution matters, because HarmonicCloud's lever is not available here.** What HarmonicCloud
replaced `std::pow` *with* was `detail::centsToDriftRatio`, which `core/pitch_utils.h:41-43` records is
now a one-line forward to `centsToPitchRatioFast` — a degree-4 Horner polynomial (`:63-68`) documented
accurate only on **±50 cents**, with the error growing as `u⁵` outside it and reaching 1.3e-3 relative at
1200 cents (`:59-61`). Phase 5's ratio domain is `±kMaxAbsGrainSemitones = ±36` semitones = **±3600
cents**, so that variant is unusable.

Phase 5's lever (3b) is therefore the *full-range* form `centsToPitchRatio(semitones * 100.0f)`
(`core/pitch_utils.h:33-36` — one `std::exp2`, accurate over the whole float range), applied **inside
`ratioAtPitch`** so both call sites move together and the monotone guarantee survives. It replaces one
transcendental with a cheaper one rather than with a polynomial, so its expected saving is **smaller than
HarmonicCloud's and is UNMEASURED** — it is recorded here as a sanctioned first move, not as a quantified
one. It changes nothing observable beyond last-bit ratio values.

### 9.6 Envelope table (FR-027)

```
GrainEnvelope::generate(envelopeTable_.data(), kEnvelopeTableSize, envelopeType_);  // :33
envelopeTable_.front() = 0.0f;                                  // FORCED, not assumed
for (k in [kEnvelopeTableSize - kEnvelopeTailZeroEntries, kEnvelopeTableSize))
    envelopeTable_[k] = 0.0f;                                   // FORCED tail RUN, not one entry
```

Five of the six shipped types already end at exactly 0 — `Hann` (`grain_envelope.h:47-50`), `Sine`
(`:79-82`), `Blackman` (`:88-94`), `Trapezoid` (`:61-73`), `Linear` (`:106-118`) — but **`Exponential`
does not**: its release branch is `std::exp(-t·4)` with `t = (i − sustainEnd)/releaseSamples`
(`:144-150`), so the last entry is `exp(-(releaseSamples−1)/releaseSamples · 4) ≈ 0.0183` and the
second-to-last ≈0.0187. `setGrainEnvelope` exposes the whole enum (`:14-21`), so without intervention a
grain could terminate on a step of ~1.9 % of its amplitude while SC-003 requires **0** detections at every
lifetime.

**Forcing table entries is necessary but NOT sufficient on its own, and an earlier draft of this plan
over-claimed that it was.** `GrainEnvelope::lookup` maps `phase` to `indexFloat = phase · (tableSize − 1)`
and clamps `phase` to `[0,1]` (`:172-181`). With the envelope phase written as `ageSamples · (1/L')` over
ages `0 … L'−1` the **maximum phase is `(L'−1)/L' < 1`**, so `table[kEnvelopeTableSize − 1]` is never read
at all and forcing it changes nothing. Worse, at `grainSeconds = 0.05` (`L' = 2400` at 48 kHz) the final
lookup lands at `index0 = 4093`, `frac ≈ 0.29` — envelope ≈ **0.0188** on the last emitted sample, a 1.9 %
terminal step, in exactly the `grainSeconds = 0.05 × Exponential` cell SC-003 sweeps.

**Two changes fix it, together:**

1. **The phase denominator is `L' − 1`, not `L'`** (§9.4g, §9.7): `phase = ageSamples · (1/(L'−1))`, so
   the last emitted sample (`ageSamples = L' − 1`) has phase **exactly 1.0**, `indexFloat = 4095`,
   `index0 = index1 = 4095`, `frac = 0`, and the lookup returns the forced `table[4095] = 0`. This is the
   same denominator `GrainEnvelope::generate` itself uses to lay the table out (`size − 1`, `:41`), so
   table and lookup now agree instead of being off by one grid step. `L' ≥ 2` is guaranteed by §9.4(c).
   Recorded as part of deviation **D-4**.
2. **A tail RUN of `kEnvelopeTailZeroEntries = 2` forced entries, not one.** The per-sample table-index
   step is `Δ = 4095/(L' − 1)`. Forcing a run of `Z` entries makes the last `⌈Z/Δ⌉` emitted samples exactly
   0 and bounds the terminal step at `table[4095 − Z − ⌈Δ⌉]`. `Z = 2` suffices for every legal
   configuration at an audio sample rate: the smallest `L'` the truncation rule can produce is
   `floor(Hs/w)` with `Hs = C − 2 − g − dR − 2`, and over the documented operating region
   (`sampleRate ≥ 44 100`, `captureSeconds ≥ 1` ⇒ `C ≥ 65536`; `w ≤ 7.875` from `r ∈ [0.125, 8]`;
   `dR ≤ 30 ms × 96 kHz = 2880`) that is `L' ≥ ⌊62588/7.875⌋ = 7947`, giving `Δ ≤ 0.52 < 1`. Hence the
   **last two** emitted samples of every grain are exactly 0 for every `GrainEnvelopeType`, and the largest
   remaining `Exponential` terminal step is `table[4093] · Δ ≈ 0.0187 × 0.52 ≈ 0.010` of one grain's
   amplitude, three samples from the end — an order of magnitude below the ~0.19 within-frame `|Δy|`
   detection threshold SC-003's pinned input produces (§15.3), and further attenuated by the `1/√n` bus
   gain.

The `sampleRate ≥ 44 100` conditioning of the `Δ < 1` bound is stated in the header banner alongside the
memory formula and the `density × grainSeconds ≤ kMaxGrains` rule (FR-073). `prepare`'s `sampleRate > 1.0`
floor (§6.1 step 1) is a defensive guard against a non-finite `controlDt`, not an operating point; below
44.1 kHz the first and last samples are still exactly 0 (change 1 guarantees that for any `L' ≥ 2`), only
the terminal-step bound weakens.

Forcing three of 4096 entries changes no shipped component — the fix lives in the engine, not in
`grain_envelope.h` — and SC-003 sweeps `Exponential` explicitly.

`regenerateEnvelope()` writes **in place** into the already-allocated vector; it must never `resize`
(contrast `GrainProcessor::prepare`, which resizes at `grain_processor.h:49`).

### 9.7 Per-sample grain accumulation (FR-024, FR-026, FR-028, FR-034)

```
busL_[i] = 0.0f; busR_[i] = 0.0f;
const std::uint64_t newest = writeCounter_ - 1;

for (std::size_t j = 0; j < activeCount_; ) {
    AtmosphereGrain& gr = grains_[activeIdx_[j]];

    const float ageL = static_cast<float>(newest - gr.readIndexInt) - gr.readFrac;
    const float env  = GrainEnvelope::lookup(envelopeTable_.data(), kEnvelopeTableSize,
                                             static_cast<float>(gr.ageSamples) * gr.envPhaseInc);
    float l0, r0;
    capture_.readStereoLinear(ageL, l0, r0);
    float rr = r0;
    if (gr.decorrAge > 0.0f) {
        float lUnused;
        capture_.readStereoLinear(ageL + gr.decorrAge, lUnused, rr);   // R reads a DIFFERENT point
    }
    busL_[i] += env * gr.panL * l0;
    busR_[i] += env * gr.panR * rr;

    // advance: integer + fraction, exact for the whole 30 s lifetime at any rate
    gr.readFrac += gr.ratio;                     // ratio held constant within the chunk
    const float carry = std::floor(gr.readFrac);
    gr.readIndexInt += static_cast<std::uint64_t>(carry);
    gr.readFrac     -= carry;

    if (++gr.ageSamples >= gr.lifetime) {        // FR-026, INTEGER compare
        gr.active = false;
        ++totalRetired_;                              // FR-072 / D-3: counted HERE, independently
        activeIdx_[j] = activeIdx_[--activeCount_];   // swap-remove; do NOT ++j
    } else {
        ++j;
    }
}
```

`totalRetired_` is incremented at the swap-remove site and at the FR-007 latch's bulk deactivation
(§6.3) — the only two places a grain leaves the active set. It is counted **independently** of
`activeCount_` so that `getTotalGrainsRetired() + getActiveGrainCount() == getTotalGrainsBorn()` is a real
assertion rather than the tautology it would be if the test computed retirements as `born − active`
(which holds for a *stealing* implementation too, and so cannot see FR-023 fail). Deviation **D-3**.

**Why the absolute domain.** `newest − gr.readIndexInt` is a `std::uint64_t` difference, non-negative by
construction (`aLo ≥ g = 64 > 0`, so the grain never reads ahead of the write head) and bounded by
`C − 2 < 2²²`, so the conversion to `float` is exact at every sample rate for the whole 30 s lifetime. A
`float` read position is not: at 30 s and 48 kHz the position reaches 1 440 000 samples where the `float`
ULP is 2²¹·2⁻²⁴ = **0.125 samples**, degrading sub-sample interpolation to eighth-sample quantisation
(C-1). Equivalently the age moves by `1 − r` per sample — the arithmetic correction C-6 records, and the
reason `GrainProcessor`'s `readPosition += |r|` (`grain_processor.h:129-143`) is wrong for every ratio but
0.5.

**Envelope phase is multiplied, never accumulated** (deviation **D-4**). `ageSamples · envPhaseInc` with
`ageSamples` exact to 2²⁴ costs one rounding (relative error ≤ 6e-8). A `phase += 1/L'` accumulator over
1.44 M additions drifts by up to ~4 % of full scale, which would retire a grain at envelope value ≈0.02
instead of 0 — a click, and precisely the failure SC-003 exists to catch. Retirement is an integer
compare, so it is exact at every lifetime.

**The R-channel double read.** When `decorrAge > 0` the grain costs **two** `readStereoLinear` calls and
discards half of each — four interpolated ring reads where two would do. That is the price of RA-1
shipping exactly one reader (FR-080/FR-084), and it is skipped entirely at `decorrelation = 0`. If SC-004
binds, a strictly-additive `readLeftLinear`/`readRightLinear` pair would halve the ring work at
`decorrelation > 0` — but that is a second RA-1-class amendment with its own cross-consumer table, and it
is **not** taken in this phase. Risk **R-2**. (It is also why §2 spends no non-inlinable call inside
`readStereoLinear`: at saturation this loop makes ~65 k reader calls per 512-sample block, so even one
`ITERUM_NOINLINE` invocation apiece would exceed the whole SC-004 budget on its own — hence the
ordered-comparison guard, D-16.)

**No per-grain amplitude term** (FR-034): it would be identically 1, so it exists neither as a field nor as
a multiply. There is no per-grain random gain — that would be an amplitude-spread control, which the
roadmap's Phase 5 list does not contain (N-8). No per-grain filtering and no per-grain STFT (C-4).

### 9.8 Observed-age bookkeeping (FR-072, SC-002)

`minObservedAge_`/`maxObservedAge_` are folded **once per control chunk per live grain**, from the ages at
the chunk's first and last sample (both channels: `ageL` and `ageL + dR`), plus once at birth and once at
retirement.

This is **exact, not a sample**: `ratio` is held constant within a chunk (FR-024), so `age(t)` is affine in
`t` over the chunk and its extremes are the two endpoints. Chunk-rate folding therefore loses nothing and
costs 4 compares per grain per 64 samples instead of per sample — which matters because SC-004 measures
the engine with this bookkeeping enabled.

### 9.9 `1/√n` population gain (FR-028)

Target refreshed at control steps (§8.1 step 4), smoothed by `gainSmoother_` (50 ms), advanced by **one
`process()` call per output sample**, and applied as **a single multiply on the summed stereo bus** after
every live grain has been accumulated:

```
const float gN = gainSmoother_.process();
busL_[i] *= gN;  busR_[i] *= gN;
```

The 50 ms constant suits long grains, where `n` changes rarely and by ±1 out of ~16. The cadence is stated
because it is FR-009's smoother-cadence rule: `configure`d against the audio rate but advanced once per
control chunk it would give a 3.2 s effective time constant, and the resulting level lag is not visibly
wrong in any single test.

It is **never** captured per grain. A birth-time snapshot would leave a grain born into a crowd quiet for
its whole 30 s life as the crowd thins — a slow, non-restoring level error — and it would invalidate
SC-008's incoherent-sum argument, which depends on every grain contributing with unit weight so the sum's
variance is ≈1 regardless of `n`.

### 9.10 Proof that the invariant holds for the grain's whole life

Let `A(t) = min(available at time t, C)` and let `age(t)` be the **right-channel** (larger) age.

- At birth (§9.4e) `A(0) ≥ ⌈a₀ + dR⌉ + 2 ≥ age(0) + 2`.
- Per sample the write head advances by 1, so `A` increases by 1 until it saturates at `C`.
- Per sample `age` moves by `1 − r(t)` with `r(t) > 0`, so `age` increases by **at most** 1.
- Therefore while `A < C`, `A(t) − age(t)` is non-decreasing and stays `≥ 2`.
- Once `A = C`, §9.4c's clamp gives `age(t) ≤ aHi + (1−rMin)⁺·L′ + dR ≤ C − 2` directly, so
  `A(t) − age(t) ≥ 2`.
- On the young side, `age(t) ≥ aLo − (rMax−1)⁺·L′ ≥ g = 64 > 0`, so no grain ever reads ahead of the write
  head.

The two guards are **different bounds doing different jobs**, which is why the old side needs only 2 and
the young side needs `g = 64`: the `+ 2` at birth is what the interpolator's `floor(age) + 1` neighbour
requires (and it is exactly `readStereoLinear`'s own clamp point), while `g` inside `aLo` is what keeps
the read point clear of the write head as `age` *shrinks* at up to `wUp` per sample. Conflating them —
requiring `+ g` on both sides — is what made FR-014 and FR-025 jointly unsatisfiable (D-11).

Hence `age(t) ∈ [g, C − 2]` unconditionally, `readStereoLinear`'s clamp never engages on a live grain,
and **no grain ever reads a sample the write head has overwritten**. Because the age never becomes
non-finite either (it is a `std::uint64_t` difference minus a fraction in `[0,1)`), §2's ordered-comparison
guard is likewise never taken from this call site — the engine relies on the proof, not on the guard. The guarantee is a bound, not an estimate: it is
closed-form, has no runtime clamp, and nothing a caller does mid-flight can invalidate it, because every
term is snapshotted at birth.

Consequences the header must state: a 30 s grain is reachable at 0 semitones with zero drift and any ring
≥ its start age; at +12 semitones (`r = 2`, `w = 1`) it needs `C ≥ 30 s`; at −12 (`r = 0.5`, `w = 0.5`) it
needs `C ≥ 15 s`; and a non-zero `driftRangeSemitones` **widens `w`**, so drift makes long grains shorter,
never less safe.

---

## 10. Drift lanes — the OU bank (FR-030)

### 10.1 Coefficients

Transcribed from `BrownianDrift::updateCoefficients` (`brownian_drift.h:230-240`), **including its
double-precision intermediates**. Computing `tau`/`a`/`g` in float instead moves the coefficients in the
last bits and puts SC-002's equivalence gate near its tolerance for no reason: the walk is an AR(1)
recursion, so a coefficient difference is re-applied at every control step.

```
controlDt = kDriftControlInterval / sampleRate_                          (double)
tau       = kDriftTauMin + smoothness * (kDriftTauMax - kDriftTauMin)    (double)
a         = exp(-controlDt / tau)                                        (double) -> lanes.a (float)
g         = kDriftInternalStd * sqrt(max(0, 1 - a*a))                    (double) -> lanes.g (float)
```

Exact discretisation of the Ornstein–Uhlenbeck SDE `dX = (1/τ)(0 − X)dt + σ dW` over the fixed step `dt`,
not forward Euler. The AR(1) coefficient alone fixes the autocorrelation, `corr(lag k) = a^k`, so the 1/e
decorrelation time is exactly `τ` seconds for any zero-mean increment — which is what makes
`setDriftSmoothness` measurable. `τ = lerp(0.2 s, 30 s, smoothness)`.

### 10.2 One control step, all lanes

```
for (i = 0 .. kMaxGrains-1) {
    const float z0 = lanes.rng[i].rng.nextFloat();     // SEQUENCED into named locals: the operands
    const float z1 = lanes.rng[i].rng.nextFloat();     // of `+` are unsequenced in C++, and a
    const float z2 = lanes.rng[i].rng.nextFloat();     // different draw order is a DIFFERENT STREAM,
    const float z  = z0 + z1 + z2;                     // not a rounding difference
    float x = lanes.a * lanes.walk[i] + lanes.g * z;   // mean is 0
    x = std::clamp(x, -kDriftWalkLimit, kDriftWalkLimit);
    if (x < kDriftDenormalFloor && x > -kDriftDenormalFloor) x = 0.0f;
    lanes.walk[i] = x;
    lanes.smoothTgt[i] = std::clamp(lanes.depth * x, -1.0f, 1.0f);   // brownian_drift.h:249-251, :269
}
```

`z` is the Irwin–Hall sum of three `nextFloat()` draws (each uniform on `[-1,1]`, variance 1/3), giving a
zero-mean, unit-variance, roughly Gaussian increment. `kDriftWalkLimit = 4` is ≥ 6σ from the stationary
distribution (σ = 0.5 around mean 0) and exists purely so the recurrence is provably bounded.

**Every lane steps, not just the live ones.** Three binding reasons: it makes lane state after `N`
advanced samples a function of `N` alone (SC-011); it makes SC-002's equivalence gate expressible against
a plain `BrownianDrift` driven with the same chunk schedule; and a lane whose stream position depended on
its slot's occupancy would make one grain's pitch a function of unrelated grains' lifetimes. Measured
precedent: `HarmonicCloud` runs **two** 64-lane banks on this exact code shape, and its whole quiescent
lane-bank path measures 12,421 ns per 512-sample block (`harmonic_cloud.h:1954-1958`) — ~6.2 µs for one
64-lane bank, i.e. ~6 % of Phase 5's 106,667 ns reference.

### 10.3 Advancing the bank

Structural mirror of `BrownianDrift::processBlock` (`:194-206`) and `HarmonicCloud::advanceDriftLanes`
(`:1999-2011`):

```
void advanceDriftLanes(std::size_t numSamples) noexcept {
    int remaining = static_cast<int>(numSamples);
    while (remaining > 0) {
        if (lanes.samplesUntilControl <= 0) {
            lanes.samplesUntilControl = kDriftControlInterval;
            advanceControlStepAllLanes();
        }
        const int advance = std::min(remaining, lanes.samplesUntilControl);
        lanes.samplesUntilControl -= advance;
        remaining -= advance;
        advanceSmootherAllLanes(advance);
    }
}
```

`samplesUntilControl` is **shared** across the bank: every lane advances by the same sample counts, so one
counter is both sufficient and correct, and it is what makes the bank's state a function of the total
advanced samples rather than of how they were partitioned. A 64-sample control chunk performs **two**
internal OU steps, because `kDriftControlInterval = 32`.

The bank is advanced **at each 64-sample control chunk on the absolute grid, by exactly that chunk's
length** — never "once per block" by `numSamples`. `BrownianDrift::processBlock` advances the output
smoother for the whole span before returning, so a value read after a 4096-sample advance is 4096 samples
further along the walk than the same value read under 64-sample partitions. Since that value scales a
grain's pitch, the two renders would diverge in pitch by orders of magnitude above SC-011's 1e-5 bound and
SC-011 would be unsatisfiable by construction.

### 10.4 The output smoother — a transcription, not the exponential identity

`advanceSmootherAllLanes(int n)` transcribes `OnePoleSmoother::advanceSamples` (`smoother.h:243-254`),
copied from `HarmonicCloud::advanceSmootherAllLanes` (`:1964-1985`):

```
if (n <= 0) return;                                                   // smoother.h:244
if (lanes.cachedPowN != n) {
    lanes.cachedPowValue = std::pow(driftSmoothCoeff_, static_cast<float>(n));   // :248
    lanes.cachedPowN = n;
}
const float coeffN = lanes.cachedPowValue;
for (i = 0 .. kMaxGrains-1) {
    const float diff0 = lanes.smoothCur[i] - lanes.smoothTgt[i];
    if (std::abs(diff0) < kCompletionThreshold) continue;             // :244 — SKIP, do NOT snap
    lanes.smoothCur[i] = lanes.smoothTgt[i] + diff0 * coeffN;         // :247-249
    lanes.smoothCur[i] = detail::flushDenormal(lanes.smoothCur[i]);   // :250
    if (std::abs(lanes.smoothCur[i] - lanes.smoothTgt[i]) < kCompletionThreshold) {
        lanes.smoothCur[i] = lanes.smoothTgt[i];                      // :251-253
    }
}
```

The naive `cur = tgt + (cur − tgt)·coeff^k` omits three operations that the real smoother performs, all
three observable: the `isComplete()` early **return** (which leaves `current_` unchanged — it does not
snap), `detail::flushDenormal`, and the post-advance hard snap below `kCompletionThreshold`. On
`HarmonicCloud`'s equivalent gate the naive form measured up to 1.64e-4 of divergence and this one
measured 0.000e+00 (`harmonic_cloud.h:1919-1924`).

`coeff^N` is formed by the **same expression** `advanceSamples` uses —
`std::pow(coefficient_, static_cast<float>(numSamples))` — never a precomputed `coeff^k` table. A
`for (k) table[k] = std::pow(coeff, (float)k)` loop is unrolled by `/O2`, which makes every exponent a
compile-time constant, and under `/fp:fast` (this repo's MSVC setting, and `-ffast-math` on the macOS leg)
the compiler strength-reduces the constant-exponent `pow` into repeated multiplication. Measured on
`HarmonicCloud`: 4 ULP at N = 32, which the 150 ms pole's 1440-fold accumulation and the snap turn into
1.02e-4 of divergence (`harmonic_cloud.h:1929-1938`). The memo on `numSamples` is **not** that table — it
is filled by this same call site with the same runtime operand, so no exponent ever becomes a
compile-time constant, and the float served is bit-for-bit the float the uncached form computed. It is
cleared by `resetDriftLanes()` because `driftSmoothCoeff_` only moves in `prepare()`, which calls
`reset()`. The call is hoisted **out** of the lane loop, so it costs 1 `powf` per bank per chunk-step.

The caller only ever advances to the next control boundary, so `n` is 32 on every call and the memo serves
one value.

### 10.5 Per-grain semantics and seeding

Lane `i` belongs to grain slot `i` for the slot's whole life and is seeded from
`deriveStreamSeed(seed_, kDriftSaltBase + i)` (`core/random.h:102-111`) at `prepare`, `reset` and
`setSeed` — and at **no other time**. A grain's instantaneous pitch is `sᵢ + laneᵢ · dᵢ` with `sᵢ` and
`dᵢ` snapshotted (§9.4) and `laneᵢ = smoothCur[i]` live; it is re-evaluated at each control step and held
constant within the chunk. A grain born mid-chunk uses the lane value as of the **most recent completed**
control step (§8.1 orders the lane advance before the ratio refresh), so birth timing inside a chunk
cannot change the value it sees.

`[[nodiscard]] float getDriftLaneValue(std::size_t slot) const noexcept` returns `smoothCur[slot]`, or 0
for an out-of-range slot — the same shape as `HarmonicCloud::getDriftLaneValue` (`:1025`) and
`EntropyProcessor::getDecoherenceLaneValue` (`processors/entropy_processor.h:386`). It exists for SC-002's
lane-equivalence gate; deviation **D-3**.

---

## 11. Spectral blur (FR-040 … FR-046)

### 11.1 Geometry

**One** stereo stage on the **summed grain output** — two `STFT`, two `OverlapAdd`, two `SpectralBuffer`
(C-4). A per-grain STFT is impossible inside the budget: one comparable pipeline is documented at
"< 0.5 % CPU single core @ 44.1 kHz, 512 samples, 2048 FFT" (`spectral_freeze_oscillator.h:20`), so 64 of
them would be ~32 % of one core per voice against a 1 % budget, at ~5.8 MB per voice. Only the placement
moves; the audible effect the roadmap asks for (phase decoherence → fog) is unchanged.

`fftSize = blurFftSize_` (default 1024, snapped per §4); `hopSize = fftSize/4` (**75 % overlap**);
`WindowType::Hann`; `applySynthesisWindow = true` — mandatory at this overlap per `stft.h:201-204` and
forbidden at 50 %. `Window::generate`'s Hann is the **periodic (DFT-even)** variant
(`core/window_functions.h:110-120`), so the hop-position squares are exactly `0 + 0.25 + 1 + 0.25 = 1.5`
and `colaNormalization_ = 1/1.5` (`stft.h:226-239`) reconstructs to unity with no COLA ripple. That is
what makes SC-006's −60 dBFS transparency gate achievable rather than marginal — a symmetric
(`÷(N−1)`) Hann would leave an O(1/N) ripple right at the threshold.

When blur is enabled the grain sum is routed through the stage **unconditionally**, so the path is
transparent at `blur = 0` and the latency never changes with the knob (RA-3).

### 11.2 Phase randomisation (FR-042)

Per **frame** — meaning per hop of audio time, once for the L/R pair, **not** once per channel-frame:

```
// ONCE per frame, OUTSIDE the per-channel block:
blurSmoother_.advanceSamples(blurHopSize_);          // FR-009 cadence, BEFORE the value is read
const float blur = blurSmoother_.getCurrentValue();  // ONE value, shared by L and R

// then, per channel, ch = 0 (left) THEN ch = 1 (right), both consuming from the
// ONE blurRng_ stream and both using the `blur` read above:
for (std::size_t k = 1; k + 1 < numBins; ++k) {      // bin 0 (DC) and Nyquist untouched
    buf.setPhase(k, buf.getPhase(k) + blur * kPi * blurRng_.nextFloat());
}
```

**The advance is per hop of audio, not per channel-frame — binding.** Both `STFT`s are pushed the same
sample counts, so `canAnalyze()` fires for L and R in lockstep; running `advanceSamples(blurHopSize_)`
inside the per-channel block would advance the smoother `2 × blurHopSize_` per hop of audio, **halving the
50 ms time constant to ~25 ms**, and would hand L and R blur values one hop apart within the same frame.
FR-009 declares the smoother-cadence rule binding, not commentary (spec.md:424-430), and no success
criterion sweeping *settled* blur values (SC-005) could see the error. `AtmosphereEngine_ControlTableClamps`
therefore carries an explicit settling-time clause for it (§15.8).

Magnitude is preserved (`SpectralBuffer::getMagnitude`, `:84`, is never written). `nextFloat()` is bipolar
(`random.h:59-63`), so the perturbation is uniform on `±blur·π`: `blur = 0` is the identity and
`blur = 1` is full decoherence. DC and Nyquist are skipped because their phase is not free in a real
spectrum.

**Pinned:** the loop runs `k ∈ [1, numBins−1)` and **draws only for those bins** — no draw is consumed for
DC or Nyquist. That is a determinism decision, not a detail: FR-071/SC-010 pin the whole stream.

**The draw is per bin per channel.** Left is processed first and right second, so the two channels receive
**independent** perturbations. Blur therefore produces fog **and** progressive stereo decorrelation as it
rises. That is intended, specified behaviour (SC-005's decorrelation clause) and is not a second width
control: it is per-source decorrelation inside the layer, the same axis FR-033 occupies per grain, while
global width remains Phase 7's. The L-then-R consumption order is part of the determinism contract;
swapping it changes the render.

The smoother must be advanced by `advanceSamples(blurHopSize_)`, **not** one `process()` call:
`configure(50 ms, sampleRate)` computes a per-`process()`-call coefficient (`smoother.h:160-164`) and
`process()` advances exactly one sample (`:197`), so one call per frame would stretch a 50 ms constant to
~12.8 s at the default geometry and the blur knob would appear frozen.

`SpectralBuffer::setPhase` invalidates the Cartesian cache (`:110`); `OverlapAdd::synthesize` calls the
const `data()` (`:156-159`), which rebuilds Cartesian from polar. The polar round trip at `blur = 0`
costs one magnitude/phase and one `cos`/`sin` per bin with ~1e-7 relative error — 60 dB below SC-006's
threshold.

### 11.3 The output FIFO (FR-043)

`OverlapAdd`'s own buffer is `2·fftSize` and is consumed by the shift-left in `pullSamples` (`:309-323`);
it cannot serve as the re-timing FIFO. FR-043 therefore requires a `prepare`-allocated FIFO of capacity
≥ `fftSize + maxBlockSize`, which is what `PrepareConfig::maxBlockSamples` exists for.

```
fifoCapacity  = std::bit_ceil(blurFftSize_
                              + std::max(maxBlockSamples_, kControlChunkSamples)
                              + blurHopSize_);
blurFifoMask_ = fifoCapacity - 1;
```

A power-of-two ring with `& mask_` indexing, one per channel, plus `blurFifoCount_` occupancy.

**Ring invariant, asserted post-`reset()` and maintained by every push and pop:**

```
blurFifoWrite_ == (blurFifoRead_ + blurFifoCount_) & blurFifoMask_
```

A push writes at `blurFifoWrite_`, advances it by 1 under the mask and increments the count; a pop reads
at `blurFifoRead_`, advances it by 1 under the mask and decrements the count — so both operations
preserve it by construction. The **only** way to break it is to initialise the three fields
inconsistently, which is exactly what §6.2 step 6 now pins (`blurFifoWrite_ = blurFftSize_ &
blurFifoMask_`, not 0). It is stated here, next to the declarations, so §11.4's occupancy trace is
checkable against it rather than merely plausible.

The
`max(…, kControlChunkSamples)` term keeps the capacity legal if a caller passes `maxBlockSamples = 64`;
the `+ hopSize` term covers the largest single-drain burst. At the default geometry this is
`bit_ceil(1024 + 2048 + 256) = 4096` floats per channel = 32 KB.

Because §8's pump is control-chunk bounded, real occupancy never exceeds
`blurFftSize_ + kControlChunkSamples + blurHopSize_` (1344 at the defaults): the FIFO honours FR-043's
stated rule but is never filled past a third of it. Deviation **D-2**.

`fifoScratch_[ch]` is a separate `prepare`-allocated `blurHopSize_`-sample vector — a fixed member array
is impossible because `blurHopSize_` reaches 1024.

### 11.4 The pump — loop order is load-bearing (FR-043)

Per ≤64-sample chunk, **frame-major and channel-minor** — the channel loop is *inside* the frame loop,
so the smoother is advanced once per frame (§11.2) and the two channels share one `blur` value:

```
blurStft_[0].pushSamples(busL_, n);
blurStft_[1].pushSamples(busR_, n);

// Both STFTs receive identical sample counts, so canAnalyze() fires in lockstep;
// gating on channel 0 gates the pair.
while (blurStft_[0].canAnalyze()) {
    blurSmoother_.advanceSamples(blurHopSize_);          // ONCE per frame-pair
    const float blur = blurSmoother_.getCurrentValue();  // ONE value for L and R

    for (ch in {0, 1}) {                                 // L first: fixes blurRng_ order (FR-044)
        blurStft_[ch].analyze(blurSpectrum_[ch]);
        /* FR-042 phase perturbation on blurSpectrum_[ch] using `blur` */
        blurOla_[ch].synthesize(blurSpectrum_[ch]);
        blurOla_[ch].pullSamples(fifoScratch_[ch].data(), blurHopSize_);  // MUST be inside the loop
        /* push blurHopSize_ samples into blurFifo_[ch] */
    }
}

/* pop exactly n samples from each blurFifo_[ch] into wetL_/wetR_ */
```

`OverlapAdd::synthesize` always accumulates at `outputBuffer_[0 .. fftSize)` with **no offset**
(`stft.h:277-285`); the per-frame hop offset comes *only* from `pullSamples` shifting the buffer left
(`:309-323`). Two `synthesize()` calls without an intervening pull of `hopSize` stack both frames at the
same offset and destroy COLA. A literal reading of "drain all frames, then pull `numSamples` once" fails
SC-006's −60 dBFS transparency check, and the failure presents as a windowing bug rather than a loop-order
bug — which is why it is pinned here.

Both channels are pumped in the same frame iteration, L first, so the `blurRng_` consumption order is
fixed (FR-044).

**Occupancy trace** (`fftSize = 1024`, `hop = 256`, `fifoCapacity = 4096`, chunk `n = 64`). Post-`reset()`
the ring holds 1024 zeros with `read = 0`, `write = 1024`, `count = 1024` (§6.2 step 6), satisfying
`write == (read + count) & mask`. The first analysis needs 1024 pushed samples = 16 chunks. Over those 16
chunks the FIFO pops 16·64 = 1024 (indices 0…1023, all pre-fill zeros; `read → 1024`, `count → 0`) and
gains nothing, so occupancy reaches exactly 0 at the moment the first frame lands — and that frame is
pushed at `write = 1024`, i.e. **exactly where the reader now is**, adding 256 before the 17th pop. In
steady state each 256 pushed samples (4 chunks) yields one frame (+256) against 4 pops (−256), so
occupancy oscillates in `[0, 256]`, never underflows, and the invariant holds at every step. At `n = 1`
the same trace gives a minimum occupancy of 1. A pre-fill of exactly `fftSize` is therefore both necessary
and sufficient, and it is what makes `getLatencySamples() == blurFftSize_` the honest number.

Had `write` been left at 0 alongside `count = 1024`, the same trace puts the reader at index 1024 and the
writer at index 0 after the pre-fill is consumed; both then advance at 64 samples per chunk on average, so
the reader stays permanently 1024 indices ahead and every pop returns data written a lap earlier. Real
latency becomes the FIFO capacity (4096), `getLatencySamples()` under-reports by 3072, and SC-006's
delay-compensated difference and SC-007's crossfade alignment both fail — as a windowing bug, not as a
FIFO bug. That is the failure §6.2 step 6 and §11.3's invariant exist to make impossible.

If the FIFO were ever short (it cannot be, by the trace above), the pop zero-fills the shortfall rather
than reading stale samples — a defensive branch, not a designed path.

### 11.5 Blur disabled (FR-045, FR-046)

No `STFT`, `OverlapAdd`, `SpectralBuffer`, FIFO, `fifoScratch_` or freeze-leg delay is allocated;
`setBlur` still clamps and stores but nothing reads it; `wet* = bus*` by copy; latency is 0.

`[[nodiscard]] std::size_t getLatencySamples() const noexcept` returns
`blurEnabled_ ? blurFftSize_ : 0`. It is constant between `prepare` calls and describes the **whole
layer — both crossfade legs**, because §12.2 delay-matches the freeze leg to the same figure; there is
never a second, different latency for a caller to discover. The freeze oscillator's own
`getLatencySamples()` (`spectral_freeze_oscillator.h:421-423`) is deliberately **not** added: the drone is
synthesised, not a delayed copy of the input, so it has no dry counterpart to align against.

---

## 12. Pure-freeze mode (FR-050 … FR-054)

### 12.1 Capture and release

`void captureFreeze() noexcept`, allocation-free:

```
const std::size_t need = freezeEnabled_ ? freezeOsc_[0].getFftSize() : 0;   // :426-428
if (need == 0 || capture_.getAvailableSamples() < need) return;             // no-op, FR-051
capture_.extractSlice(freezeCapture_[0].data(), freezeCapture_[1].data(), need, 0);   // :141
freezeOsc_[0].freeze(freezeCapture_[0].data(), need);                       // :217 — MONO
freezeOsc_[1].freeze(freezeCapture_[1].data(), need);
```

`extractSlice(…, need, 0)` returns the most recent `need` samples in time order, oldest first
(`:161-169`), which is exactly what `freeze()` wants. The extraction length is **the oscillator's own
`getFftSize()`**, never the requested `config.freezeFftSize`: `freeze()` truncates at `fftSize_`
(`:222-223`), so a larger capture would silently discard the newest audio and a smaller one would
zero-pad. Insufficient history is a **no-op, not a partial capture**.

Two instances because `freeze()` and `processBlock()` are mono (`:217`, `:317`).

`void releaseFreeze() noexcept` calls `unfreeze()` (`:295`) on both, crossfading each to silence over one
hop.

### 12.2 The delay-matched leg (FR-052)

Per ≤64-sample chunk:

```
const bool settledDry = freezeMixRamp_.isComplete() && freezeMixRamp_.getTarget() == 0.0f;
if (freezeEnabled_ && !settledDry) {
    freezeOsc_[0].processBlock(freezeL_.data(), n);        // :317
    freezeOsc_[1].processBlock(freezeR_.data(), n);
} else {
    std::fill_n(freezeL_.data(), n, 0.0f);                 // hard bypass — roadmap line 207
    std::fill_n(freezeR_.data(), n, 0.0f);
}
if (blurEnabled_ && freezeEnabled_) {                      // exactly blurFftSize_ samples of delay
    for (i in [0, n)) {
        const float dl = freezeDelay_[0][freezeDelayIdx_];
        const float dr = freezeDelay_[1][freezeDelayIdx_];
        freezeDelay_[0][freezeDelayIdx_] = freezeL_[i];
        freezeDelay_[1][freezeDelayIdx_] = freezeR_[i];
        freezeDelayIdx_ = (freezeDelayIdx_ + 1) & freezeDelayMask_;
        freezeL_[i] = dl;  freezeR_[i] = dr;
    }
}
```

**The freeze leg bypasses blur spectrally but is delay-matched to it.** The drone is never routed through
the STFT ↔ OverlapAdd stage — FR-050's pure spectral hold stays pure, and SC-005/SC-007 keep measuring the
two paths for what they are — but when blur is enabled it passes through a `prepare`-allocated stereo
delay of exactly `blurFftSize_` samples **before** the crossfade. Both legs then share one layer latency,
FR-046 reports a single honest number, and a `freezeMix` sweep crossfades two time-aligned signals.
Without the delay the legs would be offset by 21.3 ms at the default geometry and the crossfade would
smear in time. When blur is disabled the delay is neither allocated nor traversed.

**The delay is advanced with zeros while in hard bypass** (the `else` branch zeroes `freezeL_/freezeR_`
and the delay loop still runs). Cost is one load + one store per sample per channel — ~1 % of the
oscillator cost the bypass saves — and it is what makes leaving bypass click-free without an O(fftSize)
memset spike on the audio thread. Pinned decision.

### 12.3 Crossfade, and why there is no symmetric bypass at `m = 1`

Per output sample:

```
const float m = freezeMixRamp_.process();          // 100 ms LinearRamp, smoother.h:370
const float yL = wetL_[i] * (1.0f - m) + freezeL_[i] * m;
const float yR = wetR_[i] * (1.0f - m) + freezeR_[i] * m;
```

Linear, not equal-power: the two legs are independent signals, not two views of one, and SC-007's
0-detection clause is what gates the transition.

At a settled `freezeMix = 1.0` the **grain layer keeps running in full** — scheduler, grain ageing, ring
reads, `1/√n` compensation and the blur stage — even though its contribution is multiplied by zero. Two
binding reasons: releasing the freeze is seamless because the grain population never lapsed (a bypass
would restart from an empty pool and the level would swell back over `density × grainSeconds` seconds
under FR-028's smoother), and SC-004 configuration (d) therefore measures the honest grain+freeze worst
case — one of the five measured numbers RA-4 hands to Phase 7, which would otherwise understate a frozen
voice. The `m = 0` bypass is deliberately **not** symmetric with this: the freeze oscillators hold nothing
that has to stay warm, whereas the grain population does.

### 12.4 `reset()` and the freeze oscillators (allocation-free rewind)

```
for (ch in {0, 1}) freezeOsc_[ch].reset();      // spectral_freeze_oscillator.h:173-196
std::fill(freezeDelay_[ch].begin(), freezeDelay_[ch].end(), 0.0f);
freezeDelayIdx_ = 0;
```

`SpectralFreezeOscillator::reset()` is public (`public:` at `:81`, `private:` at `:435`), documented
"Clear all internal buffers and state without deallocating (FR-002)" and "@note Real-time safe", and does
exactly the rewind this engine needs: nine `std::fill`s over `frozenMagnitudes_`, `initialPhases_`,
`phaseAccumulators_`, `ifftBuffer_`, `workingMagnitudes_`, `captureBuffer_`, `outputBuffer_`,
`originalEnvelope_`, `shiftedEnvelope_`, plus `workingSpectrum_.reset()` / `formantPreserver_.reset()`,
and it clears `frozen_`, `unfreezing_`, `unfadeSamplesRemaining_`, `outputWriteIndex_`,
`outputReadIndex_` and `samplesInBuffer_`. It early-outs on `!prepared_` (`:174`), so calling it on a
freeze-disabled engine is safe. O(fftSize) `std::fill`, no allocation, no transcendentals.

**An earlier draft of this plan prescribed `unfreeze()` + a one-hop `processBlock` drain instead, on the
false premise that the component has no `reset()`. That substitute is also wrong on its own terms**, and
it is recorded here so it is not reintroduced: `unfreeze()` sets `unfadeSamplesRemaining_ = hopSize_`
(`:299`), and `processBlock` decrements that counter once per sample **while it is `> 0`** (`:346-351`),
reaching the `else` that sets `frozen_ = false` (`:352-357`) only on the sample *after* it hits zero.
Draining exactly `getHopSize()` samples therefore leaves `frozen_ == true`, with `samplesInBuffer_`,
`outputReadIndex_` and `phaseAccumulators_` all uncleared — so the leg is neither silent nor in its
post-`prepare` state, and FR-006's rewind guarantee and SC-010's `reset()`-determinism clause are unmet.
Deviation **D-7** is deleted from §17 accordingly.

`freezeCapture_` is consequently sized from `getFftSize()` for FR-051's capture **only** (§6.1 step 8);
it is no longer used as drain scratch.

### 12.5 Freeze disabled (FR-054)

No oscillator, no capture scratch and no freeze-leg delay is allocated; `setFreezeMix`, `captureFreeze`
and `releaseFreeze` are inert; the freeze path costs nothing.

---

## 13. Output stage, non-finite hygiene, denormals

### 13.1 Output (FR-060 … FR-062, FR-064)

Per output sample, after the crossfade:

```
const float lvl = levelSmoother_.process();        // 20 ms, FR-061
float oL = yL * lvl;
float oR = yR * lvl;
if (runState_ == RunState::Silencing) {
    oL *= silenceGain_;  oR *= silenceGain_;
    silenceGain_ -= silenceStep_;
    if (silenceGain_ <= 0.0f) { silenceGain_ = 0.0f; latchNow(); }
}
outLeft[i]  = detail::flushDenormal(oL);           // FR-064
outRight[i] = detail::flushDenormal(oR);
```

**There is no width control** (FR-060, N-9). `AtmosphereEngine` has no `setWidth`, does not call
`stereoCrossBlend`, and does not include `stereo_utils.h`. Its stereo image comes solely from FR-032's
per-grain equal-power pan and FR-033's per-grain L/R read-age decorrelation — which is what the roadmap's
"spatial diffusion" line asks for. Global width is Phase 7's (`StereoField`, roadmap lines 287–288). This
is stated as a requirement rather than an omission so a later phase does not add one here "for symmetry"
and end up with two controls on one axis. Blur's per-channel phase draws do widen the image as `blur`
rises, and that is not a width control either: it is per-source decorrelation, inseparable from the fog,
with no parameter whose job is stereo width.

`setLevel` is the one output-stage control the engine keeps, because the `1/√n` grain sum is produced
inside the engine and the caller cannot trim it without a second pass over the buffer. It is a gain trim,
not a dry/wet mix (N-3) and not a width control (N-9).

**Output is the wet texture only** (FR-062): the input stream appears in the output only via grains and
freeze, never as a dry pass-through.

The denormal guard is `detail::flushDenormal` (`core/db_utils.h:168-170`), matching `BrownianDrift`'s
`kDenormalFloor` practice (`:228`), and is additionally applied inside the drift-lane walk (§10.2) and by
`OnePoleSmoother`/`LinearRamp` themselves (`smoother.h:208`, `:386`). At `level = 0` the output is exact
silence with no denormals.

### 13.2 The finiteness helper (FR-008)

```cpp
    /// FR-008: the ONE finiteness test in this component. A COMPOSITION of the
    /// existing Layer 0 helpers, never a new bit test — `detail::isNaN`
    /// (`core/db_utils.h:54-57`) and `detail::isInf` (`:175-178`) are already what
    /// `OnePoleSmoother::setTarget` (`primitives/smoother.h:170-181`) and
    /// `BrownianDrift` (`processors/brownian_drift.h:64-68`) rely on, and both are
    /// constexpr exponent-field tests with the -ffast-math rationale documented at
    /// db_utils.h:44-52. `ContinuousBody` carries a private reimplementation
    /// (`systems/continuous_body.h:1346-1358`); FR-008 forbids a fifth.
    ///
    /// ITERUM_NOINLINE IS LOAD-BEARING, NOT STYLE. This is a header, and it lands in
    /// translation units built with /fp:fast (MSVC, this repo's setting) and
    /// -ffast-math (the macOS leg, via the VST3 SDK's global flags).
    /// `core/db_utils.h:44-52` states the contract verbatim: "Source files using this
    /// function MUST be compiled with -fno-fast-math." A header cannot impose that on
    /// its consumers, so the repo's established remedy is to put the check behind a
    /// call boundary the caller's fast-math context cannot see through:
    /// `primitives/smoother.h:37-45` defines ITERUM_NOINLINE with the comment
    /// "Required to prevent branch elimination with NaN checks under /fp:fast", and
    /// applies it to OnePoleSmoother::setTarget (`:170`), LinearRamp::setTarget
    /// (`:342`) and SlewLimiter::setTarget (`:519`). Phase 5 does the same. The macro
    /// arrives with <krate/dsp/primitives/smoother.h>, already in the include set
    /// (S3). `constexpr` is DROPPED: it is an inlining invitation, i.e. exactly the
    /// opposite of what is wanted here.
    ITERUM_NOINLINE [[nodiscard]] static bool isFinite(float v) noexcept {
        return !(detail::isNaN(v) || detail::isInf(v));
    }
```

No `std::isnan`, `std::isinf` or `std::numeric_limits<float>::infinity()` appears anywhere in the header.
SC-013's scripted `rg` gate enforces this, not a review step — and SC-014 additionally runs the injection
through a TU compiled **with** fast-math (§15.7), because a symbol grep can only see which helpers were
named, never whether the branch survived optimisation.

**The call boundary is not free, so the two hot paths call it O(1) times per control chunk rather than
O(n) times per sample.** A non-inlinable `bool(float)` costs ~2 ns; the naive placements — two calls per
sample in the input sanitiser and two per sample in the bus poison check — would be ~2048 calls per
512-sample block, ≈4 µs, ≈4 % of the 106,667 ns SC-004 reference, for a test that fires essentially never.
Both are therefore written as **accumulate-then-test**: §9.1 sums the chunk's input and tests once (falling
back to per-sample substitution only when that sum is non-finite), and §13.3 accumulates the bus and tests
once at the chunk boundary. Non-finite values propagate through `+` without cancelling — `(+Inf) + (−Inf)`
is NaN, still non-finite — so neither form can miss a value. Control-rate callers (every setter in §7)
call `isFinite` directly; one call per setter per block is immaterial.

The **third** finiteness site, `RollingCaptureBuffer::readStereoLinear`, uses neither this helper nor a
call boundary: it sits on the innermost loop (up to two calls per grain per sample, ~65 k per block at
saturation), where even one non-inlinable call per invocation is unaffordable. §2 replaces the guard there
with two **ordered comparisons that double as the range clamp** — not an FP-classification predicate, so
`-ffinite-math-only` has nothing to fold, and the cost is zero rather than merely small.

### 13.3 Non-finite policy (FR-063)

- **Input.** A non-finite input sample is substituted with `0.0f` for **both** capture and output, and the
  ring is **preserved** — no silence, no ring clear. This is the policy Phase 4 settled on
  (`seraphis-phase4-continuous-body/spec.md`, Clarification Q3), and it is the deliberate difference from
  a silence-on-NaN policy: grains born before the injection keep reproducing the pre-injection audio,
  which SC-014(b) measures.
- **Internal.** `busPoisonAccum_ += busL_[i] + busR_[i]` on the pre-level bus, per sample — **two adds,
  no calls**. It is tested once at the control-chunk boundary (§8.1 step 6) as
  `chunkPoisoned_ = !isFinite(busPoisonAccum_)`, after which `busPoisonAccum_` is zeroed; if the flag is
  set, `silence()` fires, grains retire under the FR-007 ramp, and the flag is cleared. Per-sample
  *accumulation* with a chunk-rate *test* cannot miss a transient — NaN and ±Inf both propagate through
  `+`, and `(+Inf) + (−Inf)` is NaN, so nothing cancels — while costing one `isFinite` call per 64 samples
  instead of 128. §13.2 explains why the call count is a design constraint rather than a micro-optimisation.
  A `busPoisonAccum_` that overflows to `±Inf` from finite values inside one 64-sample chunk would be a
  false positive; the bus is bounded by SC-008's analysis at well under 4, so 64 samples cannot reach 3.4e38.
- **Recovery.** Because `silence()` **latches** (FR-007), that path leaves the engine muted and not
  scheduling until `reset()` — the same, and only, documented recovery. The engine never resumes on its
  own after an internal non-finite event; SC-014's sub-case asserts both the latch and the `reset()`
  re-entry.

The engine has no feedback path, so an internal non-finite value can only originate from a non-finite ring
sample (impossible — input is sanitised before the write) or from a non-finite coefficient/smoother state.
Both are sticky, which is why the chunk-boundary evaluation is sufficient.

---

## 14. Determinism and introspection

### 14.1 `setSeed` (FR-070)

```
seed_ = s;
grainRng_.seed(deriveStreamSeed(s, kGrainSalt));
blurRng_.seed(deriveStreamSeed(s, kBlurSalt));
scheduler_.seed(deriveStreamSeed(s, kSchedulerSalt));                  // grain_scheduler.h:97
for (i = 0 .. kMaxGrains-1)
    driftLanes_.rng[i].rng.seed(deriveStreamSeed(s, kDriftSaltBase + i));
```

Every derived value goes through `deriveStreamSeed` (`core/random.h:102-111`) with a distinct salt, so no
two streams are correlated **and** none can be handed 0 — `Xorshift32::seed()` silently substitutes its
own default for 0 (`:73-75`), so two streams hashing to 0 would collapse onto one. The header states that
`setSeed(0)` is nonetheless a **valid, distinct** engine seed, because the derivation and not the raw
value is what reaches `Xorshift32::seed`. Default seed is 1.

`setSeed` mid-render re-seeds every lane's **stream** but does **not** zero any lane's walk state (only a
grain birth does that, §9.4h), and it cannot touch a live grain's snapshotted `s`, `d`, `rMin`, `rMax` or
`L′`. A live grain's future walk therefore changes while its ratio stays inside the envelope its lifetime
was truncated for, so FR-025's invariant holds across a `setSeed` at any point in a render — which is what
lets SC-001 exercise `setSeed` mid-render without a caveat.

### 14.2 Introspection accessors (FR-072)

All `[[nodiscard]] … const noexcept`, all plain member reads (the one exception, `getActiveSlotMask()`, is
a const 64-iteration scan that adds **no** state at all). Nothing here adds state to the audio path beyond
the six `lastBornGrain*` scalars, the retirement counter and the two age extremes.

| Accessor | Returns | Exists for |
|---|---|---|
| `getActiveGrainCount()` | `activeCount_` | SC-004(c), SC-009 |
| `getSkippedTriggerCountPoolFull()` | `skipPoolFull_` | SC-001 / SC-003 preconditions — proves FR-023's path was reached |
| `getSkippedTriggerCountRingCold()` | `skipRingCold_` | FR-014's path, kept **separate** so the two causes are never conflated |
| `getTotalGrainsBorn()` | `totalBorn_` | SC-010, SC-002 bookkeeping |
| `getTotalGrainsRetired()` | `totalRetired_`, counted **independently** at the swap-remove site (§9.7) and at the FR-007 latch (§6.3) | FR-023's skip-never-steal: `retired + active == born` is only an assertion if `retired` is measured, not derived — derived, it is a tautology a stealing implementation also satisfies (**D-3**) |
| `getActiveSlotMask()` | `std::uint64_t`, bit `i` set iff `grains_[i].active` (const scan, `kMaxGrains ≤ 64` is `static_assert`ed) | FR-020's round-robin coverage: which slots are live is otherwise unobservable (**D-3**) |
| `getLastBornGrainSlot()` | `lastBirthSlot_` | FR-020: the birth *sequence* of slot indices is what proves round-robin rather than first-free (**D-3**) |
| `getLastBornGrainPanGains(float& l, float& r)` | `lastBirthPanL_` / `lastBirthPanR_` (void, two out-params) | FR-032's equal-power law `gL² + gR² ≈ 1`, otherwise unobservable (**D-3**) |
| `getMinObservedGrainAgeSamples()` | `minObservedAge_` | SC-002 clause 1 |
| `getMaxObservedGrainAgeSamples()` | `maxObservedAge_` | SC-002 clause 1 |
| `getLastBornGrainBirthAgeSamples()` | `lastBirthAge_` | SC-002's shadow model needs `a₀` (drawn internally) |
| `getLastBornGrainRatioAtBirth()` | `lastBirthRatio_` | SC-002's shadow model needs `r` |
| `getLastBornGrainLifetimeSamples()` | `lastBirthLifetime_` | SC-002 clause 2/3 closed form, SC-009 lifetime |
| `getGrainRngState()` | `grainRng_.state()` (`random.h:79`) | SC-010's FR-044 clause — the only way to prove the blur stage did not consume from the grain stream |
| `getLatencySamples()` | §11.5 | SC-006, RA-3 |
| `getCaptureCapacitySamples()` | `captureCapacity_` | SC-009's rate-aware truncation expectation (RA-2: `C` in *seconds* is rate-dependent) |
| `getDriftLaneValue(std::size_t)` | `smoothCur[slot]` | SC-002's lane-equivalence gate (**D-3**) |

---

## 15. Test plan

Four new TUs plus an extension of one existing TU. **None of the four may include
`tests/test_helpers/allocation_operator_overrides.h`** — the single owner in `dsp_systems_tests` is
`dsp/tests/unit/systems/selectable_oscillator_test.cpp:388`, and a second include is a duplicate-symbol
link error. They include `allocation_detector.h` only. (A global `operator new`/`delete` override leaking
into a general-purpose TU has already produced flaky, ASan-invisible crashes in this repo.)

| TU | Carries |
|---|---|
| `dsp/tests/unit/systems/atmosphere_engine_test.cpp` | SC-001, SC-002, SC-008, SC-009, SC-010, SC-011 + the FR-level cases in §15.8, **plus the two clauses that must run under fast-math**: SC-014's fourth clause `AtmosphereEngine_NonFiniteGuardSurvivesFastMath` and a copy of SC-012's sub-case 6. This TU is deliberately **not** in the `-fno-fast-math` list (§16.1(2)) — that is what gives those two clauses their teeth |
| `dsp/tests/unit/systems/atmosphere_engine_spectral_test.cpp` | SC-003, SC-005, SC-006, SC-007 |
| `dsp/tests/unit/systems/atmosphere_engine_perf_test.cpp` | SC-004 + FR-022's micro-benchmark, both `[.perf]` |
| `dsp/tests/unit/systems/atmosphere_engine_nonfinite_test.cpp` | SC-014 only — a separate TU **because** it carries `-fno-fast-math -fno-finite-math-only`, which must not be applied to the others |
| `dsp/tests/unit/primitives/test_rolling_capture_buffer.cpp` (**extended, not replaced**) | SC-012's positive cases |

### 15.1 SC-001 — Zero allocation after `prepare`

*Test:* `AtmosphereEngine_NoAllocationAfterPrepare` (main TU).
*Metric:* allocation count inside `TestHelpers::AllocationScope` (`tests/test_helpers/allocation_detector.h:75`).
*Configuration:* `captureSeconds = 30`, `grainSeconds = 30`, `density = 20`, blur on, freeze on — i.e.
sustained pool exhaustion **and** sustained trigger skipping — over 10 s of `processStereoBlock` at 48 kHz
in 512-sample blocks, with `captureFreeze()`, `releaseFreeze()`, `setSeed()`, `silence()`, `reset()` and
**every** setter exercised mid-render.
*Call schedule (pinned, not incidental — §15.3 pins SC-003's for the same reason):* the setter sweep,
`setSeed()`, `captureFreeze()` and `releaseFreeze()` run across t ∈ [0, 4) s; `reset()` at **t = 4 s**;
`silence()` at **t = 9 s**.
*Threshold:* `REQUIRE(scope.getAllocationCount() == 0)`.
*Precondition assertion, not optional:* `REQUIRE(poolFullBeforeReset > 0)`, where `poolFullBeforeReset` is
`getSkippedTriggerCountPoolFull()` **captured immediately before the `reset()` call**. With a conflated
counter the case could pass having only exercised FR-014's cold-ring path and never FR-023's, which is the
path this worst case exists to stress — and asserting the *live* counter at the end of the render would
instead be a timing lottery, because `reset()` zeroes `skipPoolFull_` (§6.2 step 10) and re-saturating the
pool afterwards costs ~1 s of ring refill plus 64/20 ≈ 3.2 s of births. Capturing before `reset()` makes
the precondition independent of where the reset lands; pinning the schedule makes the whole case
reproducible.
*Construction note:* the engine is `prepare`d **outside** the `AllocationScope`, and the input/output
buffers are allocated outside it too.

### 15.2 SC-002 — Grain liveness

*Test:* `AtmosphereEngine_GrainLiveness` (main TU), organised as `SECTION`s.
*Metric:* `getMinObservedGrainAgeSamples()` / `getMaxObservedGrainAgeSamples()` sampled every block, plus
a shadow model built in the test from `getLastBornGrainBirthAgeSamples()`,
`getLastBornGrainRatioAtBirth()` and `getLastBornGrainLifetimeSamples()`.

**Births precondition, every cell of every clause below:** `REQUIRE(engine.getTotalGrainsBorn() > 0)`.
`reset()` seeds `minObservedAge_ = captureCapacity_` and `maxObservedAge_ = 0` (§6.2 step 10), so a cell in
which no grain is ever admitted satisfies `min ≥ 64` and `max ≤ C − 2` **vacuously**. That is the same
silent-pass failure mode SC-005 and SC-010 already carry explicit preconditions against
(spec.md:1093-1095, :1192-1193), and cells that were structurally birth-free existed in this very sweep
before D-11 corrected FR-014's admission margin — which is precisely why the precondition, not the
correction alone, is the mitigation.

1. **Invariant, always.** `min ≥ 64` and `max ≤ getCaptureCapacitySamples() − 2` across
   `grainSeconds ∈ {0.05, 1, 5, 15, 30}` × `pitchSemitones ∈ {−24, −12, 0, +12, +24}` ×
   `captureSeconds ∈ {1, 8, 30}` × `driftDepth ∈ {0.0, 1.0}`. `decorrelation` is swept freely here
   (`{0.0, 1.0}`) because the invariant must hold with the offset in play. Each cell renders long enough
   for the ring to reach `C` and for at least one birth (the tightest cell, `captureSeconds = 1` ×
   `grainSeconds = 30` × `pitchSemitones = +24`, admits only once the ring is completely full — ~1.37 s at
   48 kHz — because `a₀` clamps into `[65530, 65534]` against `C = 65536`; see the D-11 worked case).
2. **Closed form, drift-free sub-sweep.** `driftDepth = 0`, `pitchSpread = 0`, `positionSpread = 0`
   **and `decorrelation = 0`** (deviation **D-1**: `dR` enters the headroom, so the closed form is exact
   only at `dR = 0`). Then `a₀` and `r` follow deterministically from the setters, the shadow model
   `a(t) = a₀ + (1−r)·t` is exact, and `getLastBornGrainLifetimeSamples()` is asserted **equal** to
   `⌊(C − 2 − 64 − 2)/|1 − r|⌋` wherever truncation binds, and equal to the requested lifetime where it
   does not. The trailing `− 2` is D-12's reserved ceiling slack; writing the assertion from FR-025's
   unreserved `⌊(C − 2 − g)/w⌋` would fail a correct implementation by one sample.
3. **Envelope, drift-on sub-sweep.** `driftDepth = 1`, `driftRangeSemitones ∈ {2, 12}`,
   `decorrelation = 0`. The shadow model becomes FR-025's **bound**
   `a(t) ∈ [a₀ + min(0,1−rMax)·t, a₀ + max(0,1−rMin)·t]`; the observed min/max must lie inside it, and
   `getLastBornGrainLifetimeSamples()` is asserted equal to `⌊(C − 2 − 64 − 2)/w⌋` with
   **`w = (rMax − 1)⁺ + (1 − rMin)⁺`**.
   *Window-non-emptiness clause (D-12), which no other clause would see:* the test recomputes
   `aLo = ⌈wUp·L'⌉ + 64` and `aHi = C − 2 − ⌈wDown·L'⌉` from the observed `L'` and asserts both
   `REQUIRE(aLo <= aHi)` and `REQUIRE(a₀ ∈ [aLo, aHi])` using `getLastBornGrainBirthAgeSamples()`. Without
   the reserved slack the two `ceil`s can sum to `H + 1`, inverting the window by one sample and making
   step (d)'s `std::clamp(a0, aLo, aHi)` a **precondition violation** (UB) — which, being UB, may produce
   a plausible `a₀` and pass every bound-style assertion on the machine it is run on.
   The sub-sweep **must** include a *straddling* envelope —
   `pitchSemitones = 0`, `driftRangeSemitones = 2`, where `rMin = 0.8909 < 1 < 1.1225 = rMax` and the sum
   (0.2316) is nearly double the maximum (0.1225) — because that is the only case in which the two
   candidate definitions of `w` differ. Sweeping only non-straddling envelopes would pass on a
   maximum-based implementation, which under-truncates and leaves the birth window empty.
4. **Snapshot clause.** `grainSeconds = 30`, `captureSeconds = 30`: a long grain is born, then
   `setPitchSemitones(+24)` and `setDriftRangeSemitones(12)` are called **while it is in flight**. Assert
   that grain's recorded `L′` is unchanged, that the observed age bounds stay inside the envelope computed
   from the values in force **at its birth**, and that the widened settings appear only in the **next**
   grain born (via `getLastBornGrainRatioAtBirth()`). Clauses 1–3 sweep static configurations only, so
   without this clause a live-reading implementation — which can widen an in-flight grain's envelope past
   the one its lifetime was truncated for — passes every other clause.
5. **Drift-lane equivalence gate (FR-030).** In the same TEST_CASE, a `SECTION` that includes
   `<krate/dsp/processors/brownian_drift.h>` (the engine header does not), constructs a reference
   `BrownianDrift`, `prepare`s it at the same sample rate, `setSeed(deriveStreamSeed(seed, kDriftSaltBase
   + slot))`, `setSmoothness(driftSmoothness)`, `setDepth(driftDepth)`, `reset()`, and then drives it with
   `processBlock(64)` once per engine control chunk. `getDriftLaneValue(slot)` must track
   `getCurrentValue()` to within **1e-6**.
   *Window (stated because the birth semantics changed it):* the chosen slot must have **no birth at any
   point in the render**, not merely none inside the comparison window — a birth zeroes the lane's walk
   state without re-seeding its stream (§9.4h), whereas `BrownianDrift::reset()` re-seeds
   (`brownian_drift.h:133-135`), so any birth desynchronises the two permanently. Achieved by setting
   `density = 1`, `grainSeconds = 0.2` (so at most ~1 concurrent grain and the round-robin cursor advances
   slowly) and choosing `slot = kMaxGrains − 1`, asserted with `REQUIRE(engine.getTotalGrainsBorn() <
   kMaxGrains - 1)` at the end of the window.
   Without this gate a hand-rolled xorshift can silently desynchronise from the shared Layer 0 RNG — the
   failure Phase 2 wrote the same gate to prevent.

### 15.3 SC-003 — No clicks at grain boundaries at any lifetime

*Test:* `AtmosphereEngine_NoGrainBoundaryClicks` (spectral TU).
*Input (pinned; the metric is relative, so an unpinned input makes the criterion unreproducible):* a fixed
harmonic stack — fundamental 220 Hz with partials at 2×…9× at `1/n` amplitude, all sine, zero phase,
band-limited below 2 kHz, scaled to peak 0.5. Built by a file-local `makeHarmonicStack()` helper (no such
generator exists in `tests/test_helpers/test_signals.h`, which offers only sine/noise/sweep/square/saw),
and **shared with SC-005**.
*Metric:* `Krate::DSP::TestUtils::ClickDetector::detect()` (`tests/test_helpers/artifact_detection.h:99-160`)
with the config stated verbatim, as `shimmer_delay_test.cpp:1224-1231` does:
`ClickDetectorConfig{.sampleRate = 48000.0f, .frameSize = 512, .hopSize = 256, .detectionThreshold = 5.0f,
.energyThresholdDb = -60.0f, .mergeGap = 5}`.
*Threshold:* **0 detections** at every `grainSeconds ∈ {0.05, 0.2, 1, 5, 30}` × every `GrainEnvelopeType`
including **`Exponential`**, with `density = 20` (FR-009's maximum) and with `silence()` invoked
mid-render, over a 60 s render.

*Precondition — SCOPED, because pool saturation is unreachable in three of the five cells.* The mean
concurrent grain count is `density × grainSeconds`, and FR-009 caps `density` at 20 (spec.md:394), so at
`grainSeconds ∈ {0.05, 0.2, 1}` the maxima are **1 / 4 / 20** concurrent grains against
`kMaxGrains = 64`. `skipPoolFull_` is structurally 0 there, and a blanket
`REQUIRE(getSkippedTriggerCountPoolFull() > 0)` would fail a **correct** implementation in three of five
cells. The precondition is therefore split:

| Cells | Precondition | What it proves was reached |
|---|---|---|
| `grainSeconds ∈ {5, 30}` (`density × grainSeconds` = 100 / 600 > 64) | `REQUIRE(getSkippedTriggerCountPoolFull() > 0)` | FR-023's skip-never-steal path |
| `grainSeconds ∈ {0.05, 0.2, 1}` | `REQUIRE(getTotalGrainsBorn() > 0)` **and** `getActiveGrainCount() > 0` observed at least once during the render | the envelope path was exercised at all, i.e. the 0-detection result is not a silent-engine pass |

**FR-023's skip path is only exercisable in the long-grain cells**, and that is stated in the header next
to `kMaxGrains` and in the TU, so a later reader does not "restore" the stricter-looking precondition. It
is not a weakening: SC-001 (`grainSeconds = 30`, `density = 20`) still carries the unconditional pool-full
precondition, so FR-023 remains gated.
*Latch clause (FR-007), which fixes when `silence()` is invoked:* `silence()` at **40 s**, i.e. after the
pool-saturation precondition has been observed. From the end of its 10 ms ramp every output sample must be
exactly `0.0f` and `getActiveGrainCount()` must read 0. `reset()` at **50 s**; the final 10 s must be
non-silent again (window RMS > −60 dBFS once the ring has refilled). The detector runs over the whole
60 s, so both transitions are inside the 0-detection threshold.
*Secondary bound — a ratio, not an absolute:* `max |Δy|` of the engine output must not exceed **1.5 ×**
`max |Δy|` of a reference render of the same input and seed with pool saturation and `silence()` **not**
exercised. An absolute `max |Δy| ≤ 0.05` would constrain input bandwidth and level rather than the engine
(a 5 kHz sine at amplitude 0.5 already has per-sample deltas of 0.33 at 48 kHz).
*Note on the detector's statistics (why the input and config are pinned):* `ClickDetector` flags any
`|Δy|` above `mean + 5·stddev` of `|Δy|` **within each 512-sample frame**
(`artifact_detection.h:186-193`, `:209-218`). On the near-Gaussian output of a 16–64-grain wash the
half-normal threshold sits at ≈3.81σ, giving P(exceed) ≈ 1.4e-4 — hundreds of expected detections over a
60 s render with no click present. The pinned harmonic input is deliberately far from Gaussian so the
statistic is usable. **If the measured false-positive rate on the reference render is non-zero**, raise
`detectionThreshold` to the smallest value giving 0 detections on the *reference* render and record that
value and its measured false-positive floor in the header — never relax the 0-detection requirement on the
engine render.

### 15.4 SC-004 — CPU ≤ 1 % of one core per voice

*Test:* `AtmosphereEngine_CpuBudget`, tagged `[.perf]` (perf TU).
*Metric:* nanoseconds per 512-sample block at 48 kHz, the reproducible basis established by
`harmonic_cloud_perf_test.cpp:69-101` and reused by `continuous_body_perf_test.cpp:108-137`:

```
constexpr double kSr48 = 48000.0;
constexpr std::size_t kBlockSize = 512;
constexpr double kBlockBudgetNs = (512.0 / 48000.0) * 1e9;     // 10,666,666.67
constexpr double kRegressionFactor = 1.5;
constexpr double kReference1PctNs = kBlockBudgetNs * 0.01;     // 106,666.67
constexpr double kMaxAdmissibleNs = kReference1PctNs / kRegressionFactor;   // 71,111.11
```

*Trial shape:* best-of-**25** × **500** blocks after **400** warm-up blocks, copied from
`continuous_body_perf_test.cpp:311-317` / `harmonic_cloud_perf_test.cpp:191-193` — many short trials,
because the dev machine is a hybrid part and the dominant noise source is a whole trial migrating onto an
E-core. Affinity pinning was tried and rejected in both of those files.

*Five configurations, each with its own baseline, every one gated against the **same** 1 % reference:*
(a) defaults, blur off, freeze off — the roadmap's "default density": 4 grains/s × 4 s = 16 concurrent
(OQ-1); (b) defaults, blur on; (c) pool saturated (64 concurrent), blur on, `blurFftSize = 256` (the most
expensive blur geometry — 8 frames per 512-block); (d) `freezeMix = 1.0` **with the grain layer still
running** (FR-052 has no symmetric bypass), i.e. the honest grain+freeze worst case rather than a
freeze-only figure; **(e) configuration (b) plus `setGrainEnvelope` called once per 512-sample block with
an *alternating* `GrainEnvelopeType`** — the case the §7 idempotence guard cannot elide, so the full
4096-entry `regenerateEnvelope()` runs every block (`std::cos` per entry for `Hann`/`Sine`/`Blackman`;
~two `std::exp` per attack entry for `Exponential`, `grain_envelope.h:141-142`). Without (e) a cost the
roadmap treats as a functional requirement is entirely ungated: §7 previously accepted it as "slow but
bounded" with no measurement behind either word.

*Two distinct compile-time clauses per baseline, plus the runtime bound:*

```cpp
static_assert(kBaselineX * kRegressionFactor <= kReference1PctNs,
              "SC-004 (x): baseline must be no weaker than the 1 % reference");
static_assert(kBaselineX >= kReference1PctNs / 50.0,
              "SC-004 (x): a baseline below reference/50 was recorded from a no-op or "
              "misconfigured run - the measurement, not the threshold, is wrong");
REQUIRE(measured <= kBaselineX * kRegressionFactor);
```

The **floor** clause is not decorative: `baseline·1.5 ≤ reference` and `baseline ≤ reference/1.5` are the
same inequality, so asserting both adds no coverage, whereas a baseline accidentally recorded from a
no-op/misconfigured run would satisfy the headroom clause trivially and then make the runtime `REQUIRE`
fail spuriously on slower CI hardware. The floor catches that at compile time. (This is the review-agreed
improvement over `harmonic_cloud_perf_test.cpp:87-95`'s algebraically-identical pair.)

*Structural clauses* (so the TU stops compiling rather than silently measuring a different configuration):

```cpp
static_assert(kBlockSize % AtmosphereEngine::kControlChunkSamples == 0);
static_assert(kBlockSize / AtmosphereEngine::kControlChunkSamples == 8);
static_assert(AtmosphereEngine::kMaxGrains == 64,
              "SC-004(c) measures the saturated pool; if kMaxGrains moved under SC-004 lever (5), "
              "this number and FR-073's documented operating region move with it");
```

*Baselines are measurements, not allowances:* each is the worst (largest) of eight consecutive
best-of-25 runs on the machine named in a **BASELINE PROVENANCE** block in the TU header, with **no
padding**, and all five measured ns/block figures are copied verbatim into this phase's compliance
document. RA-4 explains why: Phase 7's 25 % full-poly tally needs real numbers from this phase, not a
ceiling nobody approached.

*If over budget, reduce cost — never raise a baseline and never relax the reference.* Levers, in order:
(1) verify FR-052's freeze hard-bypass engages; (2) verify the control-step decimation fires (a bug that
refreshes per sample pays the scheduler/drift-lane cost 64× over); (3) drop `blurFftSize` to 512 — a
specified capability trade, flagged in the header; **(3b) swap `ratioAtPitch` to
`centsToPitchRatio(st*100)`** (§9.5) — 512 `powf`/block is the single largest arithmetic term and this is
the exact lever `HarmonicCloud` spent; (4) confirm the equal-power pan `cos`/`sin` are still birth-time
only; (5) **reduce `kMaxGrains`** below 64 and shrink FR-073's documented `density × grainSeconds`
operating region to match — FR-022 declares 64 provisional precisely so this lever exists; (6) only then
escalate.

*FR-022's micro-benchmark:* `AtmosphereEngine_GrainSampleCost`, also `[.perf]`, same trial shape, reporting
**ns per grain-sample** = (ns/block) / (activeCount × 512) at the worst case — `captureSeconds = 30`,
`decorrelation = 1.0`, `positionSpread = 1.0`, pool saturated, so the read points are maximally scattered
— and at the 8 s default for contrast. It exists because the arithmetic ceiling (3.25 ns per grain-sample)
is not a cost model: with 64 grains × two decorrelated read points × two channels there are up to ~128
independent, non-sequential read streams into a multi-megabyte ring, and a single L3/DRAM miss exceeds the
whole allowance. The case **reports** (via `WARN`/`INFO`) rather than gating, and its number is what
decides whether lever (5) is spent.

### 15.5 SC-005 / SC-006 / SC-007 — blur and freeze

**SC-005 — `AtmosphereEngine_BlurMonotonicity`** (spectral TU).
*Input:* SC-003's pinned harmonic stack, peak 0.5.
*Metric:* `Krate::DSP::TestUtils::SignalMetrics::calculateSpectralFlatness(signal, n, sampleRate)`
(`tests/test_helpers/signal_metrics.h:326`) — **fully qualified**, because a same-named 2-argument overload
exists at `dsp/include/krate/dsp/primitives/spectral_utils.h:335` and an unqualified call can bind the
wrong one.

*Secondary metric, with its input pinned exactly as the harmonic stack is:*
`…::SignalMetrics::calculateCrestFactorDb` (`:222`) on an **impulsive input**, defined here because the
metric is relative and an unpinned input makes the criterion unreproducible — the same reason SC-003's
stack is pinned. The generator is a second file-local helper `makeImpulseTrain()`, next to
`makeHarmonicStack()` and shared with nothing else:

> **Impulsive input (pinned).** An otherwise-silent stereo stream at 48 kHz in which `x[n] = 1.0f` on
> both channels at every `n` that is a multiple of `24 000` (one unit impulse every **0.5 s**), and
> `0.0f` everywhere else. Rendered for **20 s** (40 impulses). The metric is computed over the settled
> region only — samples `[settleSamples, settleSamples + 480 000)`, i.e. 10 s, with `settleSamples` the
> same `positionSeconds · sampleRate + 2 · blurFftSize` used for the flatness windows — so the
> pre-first-birth silence cannot enter the crest calculation.

***Measurement method (binding — the naïve call cannot see the signal).*** `calculateSpectralFlatness`
picks `fftSize` as the largest power of two ≤ `n`, capped at 4096 (`:336-339`), and fills its window from
`signal[0 .. fftSize)` **only** (`:350-352`). Under this spec's own defaults those samples are guaranteed
**exactly zero**: `positionSeconds = 1.0` and FR-014 forbid any birth until 48 000 samples have been
written at 48 kHz, and RA-3 adds 1024 more. On an all-zero window the helper returns `0.0f`
(`arithMean < 1e-10` → early return, `:376-378`), so every flatness is 0, "non-decreasing" holds trivially
and `flatness(1.0) ≥ 1.25 · flatness(0.0)` reduces to `0 ≥ 0` — **the criterion would pass on a silent
engine.** The test therefore:
1. renders past `settleSamples ≥ positionSeconds · sampleRate + 2 · blurFftSize`;
2. passes `render.data() + settleSamples` with a length of **exactly 8192**, so the helper selects
   `fftSize = 4096` — deliberately longer than the 1024 blur FFT, so inter-frame phase decoherence widens
   spectral lines into skirts and is visible;
3. averages over **four disjoint 8192-sample windows** from the settled region, to reduce variance from
   the stochastic grain population;
4. asserts a **non-silence precondition before any threshold**: `REQUIRE(rmsDb(window) > -40.0f)` on every
   window **and** `REQUIRE(flatness(0.0) > 0.0f)`.
*Threshold:* over `blur ∈ {0.0, 0.25, 0.5, 0.75, 1.0}` the averaged flatness is **non-decreasing** at
every step (2 % epsilon) and `flatness(1.0) ≥ 1.25 · flatness(0.0)`; crest factor on the pinned impulse
train at `blur = 1.0` is at least **3 dB** below `blur = 0.0`. Both the 1.25× flatness floor and the 3 dB
crest delta are **minimums**: replace each with the measured value less a stated margin, and only ever
move them **up**. The measured crest delta and its margin are recorded under **O-2** alongside the
flatness and ρ floors.
*Stereo-decorrelation clause (FR-042):* over the same windows the normalised inter-channel correlation
`ρ(L,R)` must be **non-increasing** across the sweep (2 % epsilon) and `ρ(1.0)` at least **0.2** below
`ρ(0.0)`. Also a floor, also replaced by the measured value less a margin. An implementation that applies
one draw to both channels is a defect no other criterion in this spec would see.

**SC-006 — `AtmosphereEngine_BlurTransparentAtZero`** (spectral TU).
*Metric:* per-sample difference between `blurEnabled = true, blur = 0` (delay-compensated by
`getLatencySamples()`) and `blurEnabled = false`, same seed and identical setter history.
*Threshold:* RMS difference ≤ −60 dBFS relative to the signal RMS, after discarding the first
`2 · fftSize` samples of OverlapAdd warm-up. This is the COLA-reconstruction check; a wrong
`applySynthesisWindow`, a wrong hop, or FR-043's pull moved outside the loop fails it loudly.
*Note:* the two engines must be seeded identically **and** must not differ in RNG consumption — they do
not, because `blurRng_` is a separate stream (FR-044) and the blur stage is the only consumer.

**SC-007 — `AtmosphereEngine_FreezeStability`** (spectral TU).
*Configuration:* `blurEnabled = true`, so FR-052's delay-matched leg is in the path and an uncompensated
1024-sample offset presents as a step at the crossfade — caught by the 0-detection clause, the only place
in this spec that would see it.
*Metric:* after `captureFreeze()` and a settled `freezeMix = 1.0`, cut the render into successive
non-overlapping 1 s windows; `peak(k) = max |y[n]|` over window `k`.
*Threshold:* windows **2 … 60** each within **±1.0 dB** of `peak(2)`, and `peak(2) ≥ −60 dBFS`. The
reference is the **second** window: the first necessarily contains the 100 ms `LinearRamp` crossfade
**and** the freeze oscillator's own overlap-add pre-fill (`spectral_freeze_oscillator.h:261-287`), so a
first-window reference is measured on a partially-ramped signal and spends the whole ±1.0 dB budget on a
transient the criterion is not about.
*Plus:* crossfading `freezeMix` 0 → 1 → 0 produces **0** `ClickDetector` detections using SC-003's pinned
config.

### 15.6 SC-008 / SC-009 / SC-010 / SC-011

**SC-008 — `AtmosphereEngine_BoundedUnderStress`** (main TU).
*Configuration (fully pinned):* full-scale white noise input, `captureSeconds = 30`, `density = 20`,
`grainSeconds = 30`, `blur = 1.0`, freeze crossfading, `driftDepth = 1.0`, and **`level = 1.0`** — pinned
because it multiplies the threshold directly and the FR-009 maximum of 2.0 would double the analytic bound
with no defect present. (There is no `width` to pin — FR-060/N-9.)
*Metric:* peak absolute output over a 10-minute render.
*Threshold:* `< 4.0`, and **every sample finite** via `detail::isNaN`/`detail::isInf`, never `std::isnan`.
*Justification (the analytic and statistical bounds are different numbers; the criterion uses the
statistical one):* `1/√n` normalisation of `n` **coherent** grains bounds the sum at `√n · level` = 8, so
4.0 is **not** implied by the normalisation law. The real argument is that grains are born at independent
ring positions with independent pitches and independent decorrelation offsets, so they sum
**incoherently**: with `1/√n` applied once on the summed bus every grain contributes with unit weight, the
sum has ≈unit variance regardless of `n`, and its peak over `N` samples grows as ≈`σ·√(2 ln N)` — about
5.1σ over a 10-minute render at 48 kHz. 4.0 is a genuine statistical bound with margin against a runaway,
while `√64 · 1 = 8` is the coherent worst case no realistic configuration reaches. If a measured run
approaches 4.0 the response is to investigate coherence (all grains reading the same position at `r = 1`),
not to raise the threshold. This is the Membrum infinite-ring harness pattern applied to a layer with no
feedback path but a self-capture loop.

**SC-009 — `AtmosphereEngine_SampleRateIndependence`** (main TU).
*Metric:* grain lifetime in **seconds** (from `getLastBornGrainLifetimeSamples()` ÷ that rate, never
inferred from block-granular active-count transitions), mean concurrent grain count, and output RMS, at
44 100 / 48 000 / 96 000 Hz with identical settings and seed.
*Clause 1 — non-truncating sweep:* only configurations satisfying `w·L ≤ C − 2 − g` are swept. Lifetime
within **0.5 %** of the requested seconds at every rate; mean concurrent count within **5 %**; output RMS
within **1.0 dB**.
*Clause 2 — truncating, rate-aware:* for configurations where truncation binds (e.g. `grainSeconds = 30`,
`pitchSemitones = ±12`), measured `L′` within **0.5 %** of `⌊(C − 2 − g)/w⌋ / sampleRate` computed from
**that rate's own** `getCaptureCapacitySamples()`. A single rate-invariant expectation is unachievable and
would be a false failure: `RollingCaptureBuffer::prepare` rounds capacity **up to the next power of two**
(`:83`, `:210-220`), so `captureSeconds = 8` yields 11.89 s of ring at 44.1 kHz but 10.92 s at 48 and
96 kHz — an 8.8 % spread in `C/sampleRate` (RA-2) that propagates straight into `L′`.
*Allocation clause (a metric and a threshold, not a wish) — stated as the property it protects, not as an
equality that cannot hold.* An earlier draft required the allocation count of a second `prepare()` at a
new rate on an already-prepared engine to **equal** the count of a fresh engine prepared directly at that
rate. That fails deterministically on a **correct** implementation, because every re-prepare path reuses
existing capacity: `RollingCaptureBuffer::prepare` uses `bufferL_.resize(capacity_, 0.0f)` /
`bufferR_.resize(...)` (`rolling_capture_buffer.h:86-87`), `SpectralBuffer::prepare` uses
`data_/mags_/phases_.resize(numBins_)` (`spectral_buffer.h:63-65`),
`SpectralFreezeOscillator::prepare` `resize`s eleven vectors (`:127-163`), and
`envelopeTable_.assign(kEnvelopeTableSize, …)` is a fixed size. Where the FFT geometry and
`kEnvelopeTableSize` are unchanged across the two rates — which is exactly what SC-009 holds constant —
those `resize`s allocate **zero** times on the second prepare and N times on a fresh engine. Requiring
equality would fail green code, and the natural response under time pressure is to relax it. The clause is
therefore:

1. `REQUIRE(secondPrepareCount <= freshPrepareCount)` — the ordering check. Re-prepare can only ever
   allocate a **subset** of what a fresh prepare allocates; a count *above* the fresh one means a buffer
   is being reallocated rather than reused, or a `reserve`/`assign` is throwing capacity away.
2. `REQUIRE(renderScope.getAllocationCount() == 0)` around a **full render at the new rate**, immediately
   after that second `prepare()`. This is the property the clause exists for: re-prepare must not leave
   any buffer undersized for the new rate, and an undersized buffer shows up as an audio-thread allocation
   on the very first block. It is the same instrument SC-001 uses, pointed at the re-prepare path.

The engine must then be silent-but-usable (FR-014's cold ring), asserted by a non-silent render after the
ring refills.

**SC-010 — `AtmosphereEngine_SeedDeterminism`** (main TU).
*Metric:* `fingerprintRender` / `compareFingerprints` (`tests/test_helpers/render_fingerprint.h:64`,
`:101`) over a 20 s render — never a bit-exact digest (roadmap line 492; `node
tools/lint-float-bit-goldens.js` gates it).
*Threshold:* same seed ⇒ `withinTolerance()` (`kSampleTolerance = 1e-4f`, `kMetricTolerance = 1e-5`,
`:49`, `:52`); **different** seeds ⇒ **not** within tolerance (the negative half — without it the test
passes on a silent engine).
*FR-044 clause, asserted on grain-birth **parameters**, not on the birth count:* render twice with
identical seed and setter history, once at `blur = 0` and once at `blur = 1`, both with
`blurEnabled = true`, and require `getGrainRngState()` to be **identical** after both renders, plus
`getTotalGrainsBorn()` equal. Comparing only `getTotalGrainsBorn()` cannot detect the defect FR-044 guards
against: birth *timing* comes from `GrainScheduler`'s own private `Xorshift32 rng_{12345}`
(`grain_scheduler.h:110`, drawn at `:82`), not from the engine's grain-birth RNG, so if the blur stage
shared the grain-birth stream the number of births would be unchanged — only their parameters would shift
— and the count comparison would pass on a genuinely broken implementation.
*Plus the `reset()` clause (FR-006 edge case):* a render, then `reset()`, then the same render again ⇒
fingerprints within tolerance. This is what fails if `reset()` omits the explicit
`scheduler_.seed(...)` call (trap 6).

**SC-011 — `AtmosphereEngine_BlockPartitionInvariance`** (main TU).
*Metric:* per-sample difference between one 4096-sample call and the same render split into
`{1, 7, 64, 65, 511, 512, 1000}`-sample calls, same seed.
*Threshold:* **bit-identical is not required and not asserted**; RMS difference ≤ −100 dBFS and max
per-sample difference ≤ 1e-5. An implementation that anchors control steps to block starts instead of the
absolute grid fails this by orders of magnitude — and so does one that advances the drift bank "once per
block" by `numSamples`.
*Required coverage inside the case:* the parameter set must guarantee that **at least one grain is born
inside a partial control chunk** (the `{1, 7, 65, 511, 1000}` partitions all produce them; assert it via
`getTotalGrainsBorn()` transitions at non-multiples of 64), so the carry-over path is exercised rather
than assumed.

### 15.7 SC-012 / SC-013 / SC-014

**SC-012 — `RollingCaptureBuffer_ReadStereoLinear`**, appended to
`dsp/tests/unit/primitives/test_rolling_capture_buffer.cpp` (extended, **not** replaced).
*Regression half:* `dsp_primitives_tests` and `dsp_effects_tests` (which owns `PatternFreezeMode`,
`effects/pattern_freeze_mode.h:40`) both green with **no existing test file edited**.
*Positive half, with both anchoring conventions pinned rather than assumed identical:*
1. **Length 1** (the only case where "same offset" is true): `readStereoLinear(A, l, r)` equals the single
   sample returned by `extractSlice(&l2, &r2, /*lengthSamples=*/1, /*offsetSamples=*/A)` exactly.
2. **Length > 1:** for a slice of length `L` at offset `O`,
   `extractSlice(outL, outR, L, O)[i] == readStereoLinear(O + L − 1 − i)`, asserted at `i = 0` and
   `i = L − 1`. `extractSlice` anchors from the **end** (`:161-162`), so the first extracted sample sits
   at `writeIndex_ − O − L`, not `writeIndex_ − O − 1`; a test written from the naive wording with any
   `L > 1` compares different samples and fails a **correct** implementation.
3. **Fractional:** at half-integer ages the result is the midpoint of the two neighbours, within 1e-6.
4. **Degenerate (FR-081's guard):** immediately after `prepare` — and again after exactly one
   `writeStereo` — `readStereoLinear` at ages 0 and 1 returns `(0.0f, 0.0f)` both times. This is the
   `getAvailableSamples() < 2` case where a bare `available − 2` underflows `size_t`. Also: on a
   **default-constructed, never-prepared** buffer, the same call returns `(0,0)` and does not read out of
   bounds (visible under ASan/valgrind).
5. **Wraparound:** write `2·capacity` samples of a known ramp, then assert `readStereoLinear(C − 2)` is the
   oldest live sample and that no age in `[0, C−2]` returns a sample from the wrong side of the write
   head.
6. **Non-finite argument** (contract restated with §2's ordered-comparison guard): a NaN age and a `−Inf`
   age both land on **age 0** — the value equals `readStereoLinear(0.0f)`, i.e. the most recent sample —
   and a `+Inf` age lands on **age `getAvailableSamples() − 2`**, the same result as the largest legal
   finite age. Not `(0,0)`: that was the earlier `detail::isNaN`/`isInf` formulation, which is replaced
   because it cannot be afforded on `AtmosphereEngine`'s innermost loop (§13.2). The values are built by
   `makeNonFinite` (§15.7's `volatile` + `memcpy` form) so the construction works regardless of the TU's
   fast-math setting. **This sub-case is asserted in BOTH TUs**: here (where
   `test_rolling_capture_buffer.cpp` is already in the `-fno-fast-math -fno-finite-math-only` list,
   `dsp/tests/CMakeLists.txt:~505` — verify before relying on it) *and* in
   `atmosphere_engine_test.cpp`, which is not. The second copy is the one with teeth: a guard that only
   works under `-fno-fast-math` is a guard that never works in a shipped build.

**SC-013 — portability and lint gates.** Not a Catch2 case.
```
node tools/check-portability.js
node tools/lint-float-bit-goldens.js
node tools/lint-arch-guarded-includes.js
node tools/lint-simd-aligned-loadstore.js
node tools/lint-layers.js
node tools/lint-odr.js
node tools/lint-allocation-operator-overrides.js
rg -n "std::isnan|std::isinf|numeric_limits<float>::infinity|numeric_limits<double>::infinity" \
   dsp/include/krate/dsp/systems/atmosphere_engine.h && exit 1 || true
```
plus a **zero-warning** build of all five DSP layer test targets on MSVC and a WSL g++ syntax check. The
`rg` gate is scripted and run alongside the lints — a manual check is not a gate. `lint-layers.js` and
`lint-odr.js` are the only *automated* checks for FR-002's layer discipline and for the ODR sweep.

**SC-014 — `AtmosphereEngine_NonFiniteHygiene`** (nonfinite TU, `-fno-fast-math -fno-finite-math-only`).
*Construction:* the non-finite values are built **from bit patterns via a `volatile` sink** — the repo's
`-ffast-math`-safe construction — because `std::numeric_limits<float>::quiet_NaN()` / `infinity()` fold to
finite garbage on the macOS leg:
```cpp
[[nodiscard]] float makeNonFinite(std::uint32_t bits) noexcept {
    volatile std::uint32_t b = bits;      // defeats constant folding
    float f = 0.0f;
    std::memcpy(&f, const_cast<const std::uint32_t*>(&b), sizeof(f));
    return f;
}
// 0x7FC00000 = quiet NaN, 0x7F800000 = +Inf, 0xFF800000 = -Inf
```
*Three clauses, one per behaviour FR-063 specifies:*
(a) **every** output sample finite, by `detail::isNaN`/`detail::isInf`;
(b) **the ring is preserved** — after the injection, feed silence and confirm grains still reproduce the
*pre-injection* audio (correlation ≥ 0.99 against the same render without injection, over a window whose
birth read age predates the injection), and that `getCaptureCapacitySamples()` is unchanged;
(c) **0** `ClickDetector` detections across the injection window, using SC-003's pinned config.
*Separate sub-case:* force the engine's **own** state non-finite and assert FR-063's second half —
`silence()` fires, grains retire under the FR-007 ramp, output returns to exact zero, and then **stays**
exactly zero with `getActiveGrainCount() == 0` for the remainder of the render (the latch: no auto-resume)
until `reset()`, after which the engine renders non-silent audio again once the ring has refilled.
*How the internal path is reached:* the input sanitiser (§9.1) makes the ring unpoisonable, so an
injected input **cannot** reach it. The sub-case therefore drives the internal path directly, by
`setLevel`/`setBlur` etc. with a non-finite argument (which the setters sanitise — so that also fails) —
**the honest construction is a friend-free test hook**: the sub-case constructs the condition by
`prepare`ing with a `captureSeconds` that makes `C` small, then… *this is an open item, see §18 O-1.* If
no non-invasive construction exists, the sub-case is implemented as a **`#if defined(KRATE_TESTING)`
injection point** on the bus accumulator, declared in the header next to `chunkPoisoned_`, and that fact
is recorded in the compliance document rather than the clause being quietly dropped.
*Fourth clause — `AtmosphereEngine_NonFiniteGuardSurvivesFastMath`, and it lives in the **main** TU, not
this one.* The three clauses above prove the guards work **in a TU compiled with `-fno-fast-math`** —
which is the one configuration the shipped header will never be compiled in. The guards that matter are
§13.2's `ITERUM_NOINLINE isFinite` and §2's ordered-comparison clamp, and both exist specifically to
survive `/fp:fast` / `-ffast-math`; SC-013's scripted grep (`:1993-1994`) only checks which *symbols* were
named, so nothing else in this spec would notice the branch being folded away. The clause therefore
repeats clause (a)'s input injection and clause (b)'s ring-preservation check verbatim from
`atmosphere_engine_test.cpp`, which is deliberately **absent** from the `-fno-fast-math` list
(§16.1(2)) and is built with the repo's normal MSVC `/fp:fast` and the macOS leg's `-ffast-math`.
`makeNonFinite`'s `volatile` + `memcpy` construction works identically there — defeating constant folding
is exactly its purpose — so the injection reaches the engine on both legs. Assertions: every output sample
finite, and `getCaptureCapacitySamples()` unchanged. If the guard is folded away, this case is where it
surfaces; the `-fno-fast-math` TU would keep passing.

*Why SC-014 is needed at all:* SC-008 asserts finiteness only under **full-scale white noise**, which is
finite input — it can never reach the substitution path or the ring-preservation clause.

### 15.8 FR-level cases not covered by a success criterion

All in the main TU unless noted.

| TEST_CASE | Covers |
|---|---|
| `AtmosphereEngine_LifecycleAndGuards` | FR-003 (double `prepare` reconfigures), FR-004 (null pointer writes nothing; `numSamples == 0` advances no control step; pre-`prepare` renders silence) |
| `AtmosphereEngine_ControlTableClamps` | FR-009 — every setter's range, default, and non-finite-argument substitution; `setDensity(0.01)` lands at 0.1 and matches `GrainScheduler::getDensity()`; `PrepareConfig` FFT-size snapping (3000 → 2048, 100 → 256) reflected in `getLatencySamples()`. **Plus FR-009's smoother-cadence clause for blur** (§11.2): with `blurEnabled = true`, step `setBlur` 0 → 1 and measure the number of samples until the applied blur reaches `1 − 1/e`; it must match `kBlurSmoothMs = 50 ms` within 10 %, and must **not** land near 25 ms — which is what an `advanceSamples(blurHopSize_)` left inside the per-channel loop produces, and which SC-005 (settled values only) cannot see. **Plus FR-064** — after `setLevel(0)` and 100 ms of settling with grains live, every output sample is **exactly** `0.0f` and no sample is denormal (bit test: exponent field 0 with a non-zero mantissa), covering the spec's `level = 0` edge case (spec.md:1336) |
| `AtmosphereEngine_CaptureAndColdRing` | FR-010 … FR-014 — no births before the ring holds `a₀ + dR + 2` (D-11); `getSkippedTriggerCountRingCold()` climbs; a grain born in block *k* can read audio written in block *k*. **Plus FR-062's dry-pass-through clause** (spec.md:823-824), which nothing else covers: feed **full-scale** input with `positionSeconds = 30`, `density = 0.1` so the first birth is far away, and assert every output sample is **exactly** `0.0f` for the whole window before `getTotalGrainsBorn()` first becomes non-zero — at least 1 s of full-scale input producing bit-exact silence. Any dry leak, however small, is a non-zero sample. Second half: with `level = 0` and grains live, the output stays exactly `0.0f`, so no path bypasses the level trim |
| `AtmosphereEngine_SkipNeverSteal` | FR-020, FR-022, FR-023 — `getActiveGrainCount()` never exceeds 64; **`getTotalGrainsRetired() + getActiveGrainCount() == getTotalGrainsBorn()` at every block boundary**, with `retired` counted independently at the swap-remove site (D-3) rather than derived as `born − active` (derived, the identity is a tautology that a *stealing* implementation also satisfies, so it could not detect FR-023 failing); **round-robin verified from the birth sequence** — record `getLastBornGrainSlot()` after each `getTotalGrainsBorn()` increment and assert every slot index in `[0, kMaxGrains)` appears within `2·kMaxGrains` births (a first-free allocator concentrates births on the low `density × grainSeconds` slots and fails this), cross-checked against `getActiveSlotMask()` for which slots are live |
| `AtmosphereEngine_EnvelopeEndpointsForced` | FR-027 — for every `GrainEnvelopeType` **including `Exponential`**, and at `grainSeconds ∈ {0.05, 30}` (the extremes of the phase-resolution range), a single grain's **first two and last two** output samples are 0 to within 1e-6. Two, not one, at each end: §9.6's fix is the phase denominator `L' − 1` **plus** a forced tail run of `kEnvelopeTailZeroEntries = 2`, and asserting only the endpoints would still pass an implementation that kept the `1/L'` denominator (under which `table[4095]` is never read at all and `Exponential`'s last sample is ≈0.0188). *Plus the terminal-step bound:* for `Exponential`, `max abs(y[n] − y[n−1])` over the last 8 samples of the grain must be below `0.02 × maxAbs(y)` — the ≈0.010 bound §9.6 derives, with margin |
| `AtmosphereEngine_PopulationGain` | FR-028, FR-034 — output RMS is within 1 dB across `density ∈ {1, 4, 16}` at fixed `grainSeconds`, i.e. the `1/√n` law tracks the live population; and a grain born into a crowd does not stay quiet as the crowd thins (the birth-snapshot defect) |
| `AtmosphereEngine_PanAndDecorrelation` | FR-032, FR-033 — at `panSpread = 0` the two channels are identical when `decorrelation = 0`; at `decorrelation = 1` inter-channel correlation drops measurably; **per-grain pan gains satisfy `gL² + gR² ≈ 1` to within 1e-6**, read through `getLastBornGrainPanGains(l, r)` (D-3) after each birth across `panSpread ∈ {0, 0.5, 1}` and at least 200 births, so the equal-power law is asserted on real drawn values rather than assumed from the formula |
| `AtmosphereEngine_BlurDisabledIsFree` | FR-045, FR-046 — `getLatencySamples() == 0`, `setBlur(1.0)` changes nothing, and a second `prepare` with `blurEnabled = false` allocates strictly less than with it true |
| `AtmosphereEngine_FreezeCaptureAndRelease` | FR-050 … FR-054 — `captureFreeze()` before sufficient history is a no-op (output unchanged); after capture, `freezeMix = 1` is non-silent; `releaseFreeze()` fades within one hop; with `freezeEnabled = false` all three are inert |
| `AtmosphereEngine_SilenceLatchAndReset` | FR-007 — ramp length ≈10 ms; exact zero afterwards; counters frozen; a second `silence()` is a no-op; `reset()` is the only re-entry |
| `AtmosphereEngine_SeedZeroIsValid` | FR-070 edge — `setSeed(0)` and `setSeed(1)` produce **different** renders (proving `deriveStreamSeed`'s non-zero substitution did not collapse streams) |

---

## 16. Build integration

Exactly two files change outside the new header and the new TUs.

### 16.1 `dsp/tests/CMakeLists.txt` — two edits

**(1) `dsp_systems_tests` source list.** Sources are listed explicitly, never globbed, so an unregistered
file silently drops. Append after the Phase 4 block (currently `:344-346`):

```cmake
    # Seraphis Phase 5 (specs/seraphis-phase5-atmosphere): AtmosphereEngine
    unit/systems/atmosphere_engine_test.cpp
    unit/systems/atmosphere_engine_spectral_test.cpp
    unit/systems/atmosphere_engine_perf_test.cpp
    unit/systems/atmosphere_engine_nonfinite_test.cpp
```

**(2) the `-fno-fast-math` block.** Inside `if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")`, append to the
existing `set_source_files_properties(... PROPERTIES COMPILE_FLAGS "-fno-fast-math
-fno-finite-math-only")` list (which currently ends at `unit/systems/continuous_body_test.cpp`, `:664`):

```cmake
        # Seraphis Phase 5: SC-014's clauses (a)-(c) inject NaN/Inf via bit patterns
        # in this TU and need the FP semantics preserved to assert on them.
        # ONLY this one of the four Phase 5 TUs is listed. The other three must NOT
        # be, and for atmosphere_engine_test.cpp that is DELIBERATE AND LOAD-BEARING,
        # not an omission: SC-014's fourth clause
        # (AtmosphereEngine_NonFiniteGuardSurvivesFastMath) and SC-012's sub-case 6
        # live there precisely so the ITERUM_NOINLINE guard (S13.2) and the
        # ordered-comparison clamp (S2) are proved under the /fp:fast + -ffast-math
        # settings the header actually ships in. Adding it here would silently
        # disable the only check that has teeth. The perf TU must stay out too:
        # -fno-fast-math would change the figures the baselines are pinned to.
        unit/systems/atmosphere_engine_nonfinite_test.cpp
```

`dsp/tests/unit/primitives/test_rolling_capture_buffer.cpp` is **already** registered in
`dsp_primitives_tests` and **already** in the `-fno-fast-math` list, so SC-012 needs no CMake change.
Confirm both at implementation time before relying on the second.

### 16.2 `dsp/lint_all_headers.cpp` — deliberately unchanged

The systems section (`:147-168`) does **not** list `harmonic_cloud.h`, `spectral_morph_engine.h` or
`continuous_body.h`. Phase 5 follows that precedent and does not add `atmosphere_engine.h`. The header is
therefore compiled only through the four test TUs — which is sufficient coverage, and consistent with
Phases 2–4. (If a later phase decides to enrol all Seraphis headers there, it should do all four at once,
not one.)

### 16.3 Targets to build and run

```bash
CMAKE="/c/Program Files/CMake/bin/cmake.exe"

# The layer whose header changed (RA-1) plus the layer that consumes it:
"$CMAKE" --build build/windows-x64-release --config Release \
         --target dsp_primitives_tests dsp_effects_tests dsp_systems_tests

build/windows-x64-release/bin/Release/dsp_primitives_tests.exe 2>&1 | tail -5   # SC-012 regression
build/windows-x64-release/bin/Release/dsp_effects_tests.exe     2>&1 | tail -5   # SC-012 regression
build/windows-x64-release/bin/Release/dsp_systems_tests.exe     2>&1 | tail -5   # SC-001..003, 005..014

# Perf cases are excluded by tag everywhere else; run them explicitly:
build/windows-x64-release/bin/Release/dsp_systems_tests.exe "AtmosphereEngine_CpuBudget*"
build/windows-x64-release/bin/Release/dsp_systems_tests.exe "AtmosphereEngine_GrainSampleCost*"
```

`dsp_core_tests` and `dsp_processors_tests` are unaffected (no Layer 0 or Layer 2 file changes), but the
zero-warning clause of SC-013 covers all five, so build all five before the compliance pass.

**Do not use `ctest -R dsp_systems_tests`** — `catch_discover_tests` registers individual Catch2 case
names, not executable names, so that pattern matches nothing and reports success.

---

## 17. Recorded deviations from the spec

Each is a place where the plan tightens, refines, corrects or extends the spec. **No audible threshold and
no success-criterion bound is relaxed anywhere below.** Three rows (D-11, D-17, D-18) change a *stated
number or precondition* rather than a threshold, and each does so because the spec's version is
demonstrably unsatisfiable by a correct implementation — the demonstration is in the row, and in every case
the guarantee it was protecting is preserved by other means. Rows **D-10 … D-18** were added by the plan
review; **D-7 was deleted** (its premise, that `SpectralFreezeOscillator` has no `reset()`, is false —
see D-10).

| # | Spec text | What the plan does | Why |
|---|---|---|---|
| **D-1** | FR-025's closed form is `L′ = ⌊(C − 2 − g)/w⌋`; FR-033 separately says the decorrelation offset "participates in FR-025's clamp (the larger of the two ages is the one bounded)" | The implemented headroom is `H = C − 2 − g − dR` and `aHi = C − 2 − ⌈wDown·L′⌉ − dR` (§9.4c). SC-002 clauses 2 and 3 **pin `decorrelation = 0`**, where the two forms differ only by D-12's `− 2` | FR-025's closed form and FR-033's clamp requirement are jointly unsatisfiable unless `dR` enters the headroom: the right channel reads `dR` samples further back, so a lifetime computed without it can put `ageR` above `C − 2`. Reduces exactly to the spec's form at `dR = 0`, so no stated number moves |
| **D-2** | FR-043 sizes the blur FIFO at `≥ fftSize + maxBlockSize` and drains "every available frame each block"; `PrepareConfig::maxBlockSamples` exists for that | The pump is **control-chunk bounded**: at most `kControlChunkSamples = 64` samples are pushed between drains (§8, §11.4). The FIFO is still sized to FR-043's rule but is never filled past a third of it | `STFT::pushSamples` has **no overflow guard** (`stft.h:104-113`) and its input buffer is `8·fftSize`. FR-043 as written permits `blurFftSize = 256` with `maxBlockSamples = 8192` — 8192 samples pushed into a 2048-sample buffer before any drain, i.e. `analyze()` reading overwritten samples. Chunk-bounding makes `samplesAvailable_ ≤ fftSize + 64` at every legal geometry. Also removes `maxBlockSamples` from every hot-path bound and lets all per-slice scratch be fixed 64-sample members |
| **D-3** | FR-072's accessor list (spec.md:856-875) has no lane accessor, no retirement counter, no slot observation and no pan observation | Adds **five** accessors: `[[nodiscard]] float getDriftLaneValue(std::size_t) const noexcept` (§10.5), `getTotalGrainsRetired()`, `getActiveSlotMask()`, `getLastBornGrainSlot()` and `getLastBornGrainPanGains(float&, float&)` (§14.2). All are plain const member reads except `getActiveSlotMask()`, a const 64-iteration scan; the audio path gains one counter and three scalars | Four requirements are otherwise **unobservable, i.e. untested**: SC-002's lane-equivalence gate (as before); FR-023's skip-never-steal, whose "births = retirements + live" identity is a tautology unless `retired` is counted independently at the swap-remove site — derived as `born − active` it holds for a *stealing* implementation too; FR-020's round-robin, since nothing exposed which slot a birth took or which slots are live; and FR-032's `gL² + gR² ≈ 1`, since no per-grain pan value was readable. Same name and shape as `HarmonicCloud::getDriftLaneValue` (`systems/harmonic_cloud.h:1025`) and `EntropyProcessor::getDecoherenceLaneValue` (`processors/entropy_processor.h:386`), so it is the established Seraphis idiom, not a new one |
| **D-4** | FR-026: "Envelope phase advances by `1/L'` per sample" | Phase is **computed** as `ageSamples · (1/(L′ − 1))` from an integer sample counter, not accumulated (§9.7) — note the denominator is **`L′ − 1`**, not `L′`, and `L′ ≥ 2` is enforced at birth (§9.4c). Retirement is still the integer compare `ageSamples ≥ L′` | Two independent changes. **(a) Multiplied, not accumulated:** observationally the same function, numerically safe — a float accumulator over 1.44 M additions drifts by up to ~4 % of full scale, retiring a grain at envelope value ≈0.02 instead of 0 — a click, and precisely the failure SC-003 exists to catch. **(b) The `L′ − 1` denominator:** with `1/L′` the maximum phase over ages `0 … L′−1` is `(L′−1)/L′ < 1`, so `GrainEnvelope::lookup` (`grain_envelope.h:165-182`) **never reads `table[kEnvelopeTableSize − 1]`** and FR-027's forced last entry does nothing. At `grainSeconds = 0.05` the final lookup lands at `index0 = 4093`, `frac ≈ 0.29`, giving ≈**0.0188** for `Exponential` — a 1.9 % terminal step in the exact cell SC-003 sweeps. `L′ − 1` is the same denominator `generate` uses to lay the table out (`:41`), so table and lookup finally agree. See D-13 for the companion tail-run change |
| **D-5** | FR-024: "recomputed at each control step from `semitonesToRatio(sᵢ + laneᵢ · dᵢ)`" | The pitch is additionally **clamped to the snapshotted `[semisLo, semisHi]`** before the conversion, and both the birth envelope and the per-step ratio go through one `ratioAtPitch()` helper (§9.5) | `lane·d` is within `±d` mathematically, but `s + lane·d` can round 1 ULP above `s + d`; over 1.44 M samples that is ≈0.14 samples of extra age, enough to fail SC-002's `max ≤ C − 2` when the floor division leaves zero slack. The single helper makes the birth/step monotone consistency structural rather than conventional |
| **D-6** | FR-042: "for every bin `k ∈ [0, numBins)` … bin-0 (DC) and Nyquist … left untouched" | The loop runs `k ∈ [1, numBins−1)` and **consumes no RNG draw** for DC or Nyquist (§11.2) | The spec does not say whether a skipped bin still consumes a draw, and FR-071/SC-010 pin the whole stream. Pinned rather than left to the implementer |
| **D-8** | FR-052: hard bypass at settled `m = 0` | The freeze-leg **delay** is still advanced with zeros while bypassed; only the two `SpectralFreezeOscillator::processBlock` calls are skipped (§12.2) | One load + one store per sample per channel — ~1 % of what the bypass saves — and it makes leaving bypass click-free without an O(fftSize) memset spike on the audio thread |
| **D-9** | FR-072: `getMinObservedGrainAgeSamples()` / `getMaxObservedGrainAgeSamples()` "sampled every block" | Folded once per **control chunk** per live grain, from the ages at the chunk's first and last sample, plus at birth and retirement (§9.8) | Exact, not a sample: `ratio` is constant within a chunk, so `age(t)` is affine and its extremes are the endpoints. 4 compares per grain per 64 samples instead of per sample, which matters because SC-004 measures the engine with this bookkeeping enabled |
| **D-10** | (unstated) `SpectralFreezeOscillator` was assumed to have no `reset()`, and `reset()` was specified as `unfreeze()` + a one-hop `processBlock` drain | `reset()` calls **`freezeOsc_[ch].reset()`** (§6.2 step 7, §12.4). The old **D-7** row is deleted; `freezeCapture_` is no longer used as drain scratch | The premise was false. `spectral_freeze_oscillator.h:173-196` declares a public `void reset() noexcept` — `public:` at `:81`, `private:` at `:435` — documented "Clear all internal buffers and state without deallocating (FR-002)" and "@note Real-time safe", doing nine `std::fill`s plus `workingSpectrum_`/`formantPreserver_` resets. The substitute was **also wrong**: `unfreeze()` sets `unfadeSamplesRemaining_ = hopSize_` (`:299`) and `processBlock` decrements it once per sample while `> 0` (`:346-351`), reaching `frozen_ = false` (`:352-357`) only on the sample AFTER zero — so draining exactly `getHopSize()` samples leaves `frozen_ == true` with `samplesInBuffer_`, `outputReadIndex_` and `phaseAccumulators_` uncleared, breaking FR-006 and SC-010 |
| **D-11** | FR-014 (spec.md:471-473): births require `getAvailableSamples() ≥ birth read age + kMinAgeSamples` (= 64) | Admission requires `available ≥ ⌈a₀ + dR⌉ + kInterpMarginSamples` (= **2**). `g = 64` is unchanged and still sits inside `aLo` (§9.4c) | FR-014 and FR-025 are **jointly unsatisfiable** with `+ g` on both sides: `aHi` reaches `C − 2 − ⌈wDown·L′⌉ − dR`, so substituting `a₀ = aHi` demands `available ≥ C − 2 + g`, which exceeds `C` whenever `⌈wDown·L′⌉ < g`. Worked case from §15.2's own sweep — `captureSeconds = 1` (C = 65536 @48k), `grainSeconds = 30`, `pitchSemitones = +24` — gives `a₀ ∈ [65530, 65534]` needing 65594 > 65536: **no grain is ever born** and `skipRingCold_` climbs forever, contradicting the spec's edge case "`positionSeconds` > `captureSeconds` → clamped by FR-025's window, never an out-of-range read" (spec.md:1311). The two guards protect different sides (§9.10): `g` is the young-side clearance from the write head, `2` is what `readStereoLinear`'s `floor(age)+1` neighbour needs, and it is exactly that reader's own clamp point. **Not a relaxation** — no safety margin is removed, one is moved off the side it did not belong on |
| **D-12** | FR-025's truncation is `L′ = ⌊(C − 2 − g)/w⌋` | `L′ = ⌊(H − 2)/w⌋` with `H = C − 2 − g − dR`, i.e. **two samples of ceiling slack reserved**, and the non-truncating branch gated on `w·L > H − 2` for the same reason (§9.4c). SC-002 clauses 2/3 assert the `− 2` form | With only `w·L′ ≤ H`, the window width is `aHi − aLo = H − ⌈wUp·L′⌉ − ⌈wDown·L′⌉ > H − w·L′ − 2 ≥ −2`: **it can invert by one sample**. Reachable inside the spec's ranges (`wUp = 0.3`, `wDown = 0.4`, `H = 524222`, `L′ = 748888` ⇒ the two ceils sum to 524223 > H), and step (d)'s `std::clamp(a0, aLo, aHi)` with `lo > hi` is a **precondition violation — UB** — landing in exactly the straddling, truncation-binding case SC-002 clauses 2/3 sweep. Reserving 2 makes `aHi ≥ aLo` provable for every envelope. Costs at most 2 samples of a lifetime; SC-002's window-non-emptiness clause is the standing check |
| **D-13** | FR-027 (spec.md:593-602): forcing `table[0]` and `table[kEnvelopeTableSize − 1]` to 0 fixes `Exponential` | Forces `table[0]` **and a tail RUN of `kEnvelopeTailZeroEntries = 2`**, together with D-4(b)'s `L′ − 1` phase denominator (§9.6) | The spec's claim only holds when `L′ ≫ kEnvelopeTableSize`, and not at all under the `1/L′` denominator, which never reaches the last entry. With `L′ − 1` the last sample lands exactly on the forced entry; the 2-entry run then makes the **last two** samples exactly 0 for every type, because the per-sample table-index step is `4095/(L′ − 1) ≤ 0.52` over the documented operating region (`sampleRate ≥ 44 100`, `captureSeconds ≥ 1` ⇒ `C ≥ 65536`; `w ≤ 7.875`; `dR ≤ 2880` ⇒ `L′ ≥ 7947`). Residual `Exponential` terminal step ≈0.010 of one grain's amplitude, three samples from the end — stated, bounded and asserted, not claimed away |
| **D-14** | (unstated) `reset()`'s blur-FIFO state | `blurFifoRead_ = 0`, `blurFifoWrite_ = blurFftSize_ & blurFifoMask_`, `blurFifoCount_ = blurFftSize_`, all guarded on `blurEnabled_` (§6.2 step 6), with the ring invariant `blurFifoWrite_ == (blurFifoRead_ + blurFifoCount_) & blurFifoMask_` stated at the declarations (§11.3) | An earlier draft set `blurFifoWrite_ = blurFifoRead_ = 0` **with** `blurFifoCount_ = blurFftSize_`, violating the invariant by `blurFftSize_`. Traced at the default geometry the reader ends 1024 indices ahead of the writer permanently, both advancing 64 samples/chunk, so every pop returns data written a lap earlier: real latency becomes the FIFO capacity (4096), `getLatencySamples()` under-reports by 3072, and SC-006 and SC-007 fail **as a COLA/windowing bug** rather than as a FIFO-init bug |
| **D-15** | FR-009's smoother-cadence rule, declared binding rather than commentary (spec.md:424-430) | `blurSmoother_.advanceSamples(blurHopSize_)` runs **once per frame-pair, outside the per-channel loop**, and the single value it yields is used by both L and R (§11.2, §11.4). `AtmosphereEngine_ControlTableClamps` measures the resulting settling time against `kBlurSmoothMs = 50 ms` ±10 % | Inside the per-channel loop the smoother advances `2 × hopSize` per hop of audio — halving the 50 ms constant to ~25 ms — and hands L and R blur values one hop apart within the same frame. SC-005 sweeps **settled** blur values only, so no existing criterion could see it |
| **D-16** | FR-008 names `detail::isNaN`/`detail::isInf` as the finiteness test; the spec says nothing about how they survive `-ffast-math` | §13.2's `isFinite` is declared **`ITERUM_NOINLINE`** (and not `constexpr`), and both hot paths are restructured to call it **once per 64-sample chunk** rather than per sample (§9.1, §13.3). RA-1's `readStereoLinear` uses **two ordered comparisons** instead, doubling as its range clamp (§2). SC-014 adds a clause that runs the injection through a **fast-math** TU | `core/db_utils.h:44-52` states the contract: "Source files using this function MUST be compiled with -fno-fast-math" — which a header cannot impose. The repo's existing remedy is `ITERUM_NOINLINE` (`primitives/smoother.h:37-45`, "Required to prevent branch elimination with NaN checks under /fp:fast", applied at `:170`, `:342`, `:519`). An earlier draft put plain inline `isFinite` calls per sample in header code destined for `/fp:fast` and `-ffast-math` TUs and added `-fno-fast-math` to the SC-014 TU **only**, so the guards could be folded away in precisely the builds that ship while SC-014 passed. SC-013's grep only sees symbol names |
| **D-17** | SC-003 (spec.md:1000-1001) applies `REQUIRE(getSkippedTriggerCountPoolFull() > 0)` to the whole sweep `grainSeconds ∈ {0.05, 0.2, 1, 5, 30}` | The precondition is **scoped to `grainSeconds ∈ {5, 30}`**; the three short-lifetime cells assert `getTotalGrainsBorn() > 0` and an observed `getActiveGrainCount() > 0` instead (§15.3). The 0-detection threshold is unchanged in every cell | Mean concurrent grains is `density × grainSeconds` and FR-009 caps density at 20 (spec.md:394), so the three short cells top out at **1 / 4 / 20** against `kMaxGrains = 64`: `skipPoolFull_` is structurally 0 and the precondition **fails a correct implementation** in three of five cells. FR-023 remains gated — SC-001 (`grainSeconds = 30`, `density = 20`) keeps the unconditional pool-full precondition, and the two long cells here keep it too. The header records that FR-023's skip path is only exercisable in the long-grain cells |
| **D-18** | SC-009's allocation clause required a re-`prepare` at a new rate to allocate **exactly as many times** as a fresh engine prepared at that rate | Replaced by `REQUIRE(secondPrepareCount <= freshPrepareCount)` **plus** `REQUIRE(allocationCount == 0)` for a full render at the new rate immediately afterwards (§15.6) | Equality cannot hold on correct code: every re-prepare path reuses capacity — `bufferL_/bufferR_.resize` (`rolling_capture_buffer.h:86-87`), `data_/mags_/phases_.resize` (`spectral_buffer.h:63-65`), eleven `resize`s in `SpectralFreezeOscillator::prepare` (`:127-163`), and a fixed-size `envelopeTable_.assign` — all of which allocate **zero** times when the geometry is unchanged, which is exactly what SC-009 holds constant. The replacement is **stronger** on the property that matters (re-prepare must leave nothing undersized) and is not a threshold move: the render clause is a hard 0 |

---

## 18. Risks, mitigations and open items

### 18.1 Risks

| # | Risk | Evidence | Mitigation |
|---|---|---|---|
| **R-1** | **SC-004 fails at 64 grains because the ring is a cache-miss workload, not an arithmetic one.** 106,667 ns / (64 × 512) = 3.25 ns per grain-sample; with up to ~128 independent non-sequential streams into a ring of up to 16.8 MB, a single DRAM miss exceeds the whole allowance | C-8 / FR-022. RA-2's byte table | `AtmosphereEngine_GrainSampleCost` (§15.4) measures it **before** `kMaxGrains` is treated as settled. Levers in order (§15.4); lever (5) reduces `kMaxGrains` and shrinks FR-073's documented operating region — a *specified capability trade*, flagged in the header and the compliance document, never a raised baseline |
| **R-2** | **The R-channel double ring read doubles the dominant cost at `decorrelation > 0`** — four interpolated reads where two would do | §9.7; RA-1 ships exactly one reader (FR-080/FR-084) | Skipped entirely at `decorrelation = 0`. If it binds, a strictly-additive `readLeftLinear`/`readRightLinear` pair halves it — but that is a second RA-1-class amendment needing its own cross-consumer table, and is **not** taken in this phase |
| **R-3** | **512 `std::pow` calls per block** for the per-control-step ratio | §9.5. `HarmonicCloud` carried this exact call count and spent a pitch-ratio lever on it as one of three; the file's own measured figures for all three combined are **33,257–34,184 → 21,917–25,262 ns/block automated** and **31,281–32,027 → 20,641–23,154 static** (`harmonic_cloud_perf_test.cpp:99-100`) | SC-004 lever (3b): swap `ratioAtPitch` to `centsToPitchRatio(st·100)` (`core/pitch_utils.h:33-36`, `std::exp2`). One call site, both consumers, monotone guarantee preserved. **Expected saving is UNMEASURED and smaller than HarmonicCloud's**: what HarmonicCloud actually switched to was `centsToPitchRatioFast`, a degree-4 Horner polynomial accurate only on ±50 cents with error growing as `u⁵` outside it (`core/pitch_utils.h:41-43`, `:59-68`), which Phase 5 **cannot use** at `±kMaxAbsGrainSemitones` = ±3600 cents. Lever (3b) trades one transcendental for a cheaper one, not for a polynomial |
| **R-4** | **SC-003's `ClickDetector` false-positive floor.** The detector's threshold is a within-frame `mean + 5σ` of `\|Δy\|`; on near-Gaussian output that is ≈3.81σ half-normal, P ≈ 1.4e-4 — hundreds of detections over 60 s with no click | `artifact_detection.h:186-193`, `:209-218`. Phase 4 hit the same wall and made every clause control-relative (`continuous_body_test.cpp:42-45`) | The pinned harmonic input is deliberately far from Gaussian. If the reference render still shows detections, raise `detectionThreshold` to the smallest value giving 0 on the **reference** and record it plus the measured false-positive floor in the header — never relax the engine-render requirement |
| **R-5** | **SC-006's −60 dBFS transparency is a COLA claim.** A symmetric Hann, a 50 % hop, `applySynthesisWindow = false`, or FR-043's pull moved outside the loop each break it | `stft.h:201-204`, `:226-239`; `window_functions.h:110-120` | The window is verified **periodic** (§11.1), so Hann² at 75 % overlap sums to exactly 1.5 with no ripple. The geometry is asserted in `prepare` (`hopSize == fftSize/4`) and SC-006 is the standing check |
| **R-6** | **RA-2's memory does not scale.** 30 s at 96 kHz is 33.6 MB per voice; 16 voices at 30 s @ 48 kHz is 268 MB | `rolling_capture_buffer.h:83`, `:210-220` | `captureSeconds` is a `prepare()` argument defaulting to 8 s (4.19 MB/voice, 67 MB at 16). The shipped value and any move to a shared ring are **Phase 7** decisions; the header carries the byte table (FR-073) so Phase 7 is not surprised |
| **R-7** | **RA-4: the roadmap's per-phase budgets do not sum to its Phase 7 ceiling** (45 % against 25 %; even halving every phase gives 29 %) | RA-4's table | Phase 5 keeps 1 % as the gate for **all five** configurations (a)-(e), pads no baseline, and copies five measured ns/block figures into the compliance document. The aggregate is flagged as a **blocking Phase 7 item**; no Phase 5 threshold is relaxed on the grounds that the aggregate is already over |
| **R-8** | **MSVC-green proves nothing.** `std::bit_ceil`/`std::bit_floor`/`std::has_single_bit` need `<bit>`; `std::uint8_t`/`std::uint64_t` need `<cstdint>`; narrowing in brace-init is an error on Clang, a warning on MSVC | Repo history: `check-portability.js` exists because of exactly this | Explicit include list (§3); designated initialisers with explicit types for every aggregate; `node tools/check-portability.js` before every commit (a `guard-portability.js` PreToolUse hook runs the `--staged` form). WSL g++ syntax check for anything behavioural |
| **R-9** | **Denormals in the drift walk and the smoothers** at long time constants | `brownian_drift.h:228`; `smoother.h:208`, `:250`, `:386` | `kDriftDenormalFloor` in the walk (§10.2), `detail::flushDenormal` on the output (§13.1), and the smoothers flush internally. `dsp_test_main.cpp:13` calls `enableFTZDAZ()` before any case runs, so every measurement is taken with denormals flushed by the process — the same environment the audio thread runs in |
| **R-10** | **`readStereoLinear` is added to a shared Layer 1 primitive with a live consumer** (`PatternFreezeMode`) | RA-1's cross-consumer table | Strictly additive and inert unless called (FR-084). SC-012 runs `dsp_primitives_tests` and `dsp_effects_tests` **unedited** as the regression gate. The single new include is `<cmath>` (stdlib), so `lint-layers.js` still passes and no new inter-header edge is created at all — D-16's ordered-comparison guard removed the `<krate/dsp/core/db_utils.h>` an earlier draft required |

### 18.2 Open items

- **O-1 — how SC-014's internal-non-finite sub-case constructs its condition.** §9.1's input sanitiser
  makes the ring unpoisonable and every setter sanitises its argument, so there is no *external* route to a
  non-finite bus value — which is the correct design and also means the sub-case has no obvious
  non-invasive construction. Resolve at implementation time in this order: (i) look for a legitimate
  arithmetic route (e.g. an extreme `captureSeconds`/`sampleRate` combination that makes a coefficient
  non-finite) — preferred, because it tests a real hazard; (ii) if none exists, add a
  `#if defined(KRATE_TESTING)` injection point on the bus accumulator, declared in the header next to
  `chunkPoisoned_`, and record it in the compliance document. **Do not** drop the clause: FR-063's second
  half and FR-007's latch are otherwise unmeasured, and an implementation that silently resumes after an
  internal non-finite event would look identical under every other criterion.
- **O-2 — the SC-005 and SC-007 floors are placeholders.** `flatness(1.0) ≥ 1.25 · flatness(0.0)`,
  `ρ(0.0) − ρ(1.0) ≥ 0.2` and the **3 dB crest-factor delta on the pinned impulse train** (§15.5) are
  **minimums**. Replace each with the measured value less a stated margin and record all three measured
  values and their margins in the TU header. They may only move **up**.
- **O-3 — `kMaxGrains = 64` is provisional** until `AtmosphereEngine_GrainSampleCost` has run on the
  BASELINE PROVENANCE machine. If it is reduced, FR-073's documented `density × grainSeconds` operating
  region, SC-004(c)'s "saturated" configuration, the `static_assert(kMaxGrains == 64)` in the perf TU and
  the header banner all move together.
- **O-4 — `blurFftSize = 1024` is provisional** in the same sense: the sanctioned fallback if SC-004(b)
  fails on measured hardware is a default of **512** (SC-004 lever 3), a specified capability trade
  flagged in the header banner and the compliance document. Never a raised baseline.

---

## 19. Implementation order

Each step ends in a state that builds and whose tests pass; nothing is left half-wired between steps.

| # | Work | Verify |
|---|---|---|
| **T0** | Re-run the §1 ODR sweep and `node tools/lint-odr.js`. Confirm `test_rolling_capture_buffer.cpp` is in the `-fno-fast-math` list | sweep output is 0 hits; grep confirms the CMake entry |
| **T1** | RA-1: `readStereoLinear` + the two new includes in `rolling_capture_buffer.h` | `dsp_primitives_tests` and `dsp_effects_tests` green **unedited**; `node tools/lint-layers.js` clean |
| **T2** | SC-012's positive cases appended to `test_rolling_capture_buffer.cpp` (all six sub-cases), plus the fast-math copy of sub-case 6 in `atmosphere_engine_test.cpp` | `dsp_primitives_tests` green; sub-case 4 fails if the `available − 2` guard is removed and sub-case 6 fails if either ordered comparison is dropped (verify by temporarily doing both) |
| **T3** | Header skeleton: banner, includes, constants, `PrepareConfig`, `AtmosphereGrain`, `DriftLaneRng`, `GrainDriftLanes`, all members, all `static_assert`s, `prepare`/`reset`/`silence`, every setter and every accessor — bodies for the control table and lifecycle only, `processStereoBlock` writing silence. Register all four TUs in CMake with a single smoke case | `dsp_systems_tests` builds with zero warnings; `AtmosphereEngine_ControlTableClamps` and `AtmosphereEngine_LifecycleAndGuards` pass |
| **T4** | Drift lanes (§10) complete, advanced from `runControlStep` | SC-002's lane-equivalence `SECTION` passes to 1e-6 against a reference `BrownianDrift` |
| **T5** | Capture, scheduler, birth (§9.1–9.6) including the full FR-025 arithmetic **with D-11's `+2` admission and D-12's reserved ceiling slack**, the `L′ − 1` envelope phase and the forced tail run (D-4b/D-13), the retirement counter and the four new introspection accessors (D-3); grains accumulate to the bus with no blur and no freeze | SC-002 clauses 1–4 **incl. the births precondition and the window-non-emptiness clause**, `AtmosphereEngine_CaptureAndColdRing` (incl. FR-062), `AtmosphereEngine_SkipNeverSteal`, `AtmosphereEngine_EnvelopeEndpointsForced` |
| **T6** | Per-sample accumulation, `1/√n`, level, silence ramp and latch, non-finite hygiene (§9.7, §9.9, §13) | SC-001, SC-003, SC-008, SC-010, SC-011, `AtmosphereEngine_PopulationGain`, `AtmosphereEngine_PanAndDecorrelation`, `AtmosphereEngine_SilenceLatchAndReset`, `AtmosphereEngine_SeedZeroIsValid` |
| **T7** | Blur stage (§11) — geometry, FIFO, pump, phase randomisation | SC-006 first (transparency proves the plumbing), then SC-005 |
| **T8** | Freeze leg (§12) — capture, delay match, crossfade, bypass, and `reset()` via `SpectralFreezeOscillator::reset()` (D-10) | SC-007, `AtmosphereEngine_FreezeCaptureAndRelease`, `AtmosphereEngine_BlurDisabledIsFree`, and SC-010's `reset()` clause (which is what the deleted D-7 rewind would have failed) |
| **T9** | SC-014's three clauses in the nonfinite TU with the `-fno-fast-math` CMake entry, **plus the fourth clause `AtmosphereEngine_NonFiniteGuardSurvivesFastMath` in the main TU** (D-16); resolve **O-1** | `dsp_systems_tests` green; the sub-case fails if the latch is replaced by an auto-resume (verify by temporarily doing so) |
| **T10** | SC-009 (three sample rates, both clauses, the allocation-equality clause) | `AtmosphereEngine_SampleRateIndependence` |
| **T11** | Perf TU: `AtmosphereEngine_GrainSampleCost` **first** (it decides **O-3**), then `AtmosphereEngine_CpuBudget` with five measured baselines (a)-(e) and the BASELINE PROVENANCE block | both `[.perf]` cases pass; the five ns/block figures recorded verbatim |
| **T12** | Spend any SC-004 lever that T11 shows is needed, in the documented order; update the header banner and `static_assert(kMaxGrains == …)` together if lever (5) is taken | re-run T11; every other suite still green |
| **T13** | Gates: all five DSP layer targets zero-warning on MSVC; `node tools/check-portability.js`, `lint-layers.js`, `lint-odr.js`, `lint-float-bit-goldens.js`, `lint-arch-guarded-includes.js`, `lint-simd-aligned-loadstore.js`, `lint-allocation-operator-overrides.js`, and the scripted `rg` non-finite-symbol gate; WSL g++ syntax check | all clean (SC-013) |
| **T14** | Compliance document: every FR cited to a file:line in the delivered header, every SC to a test name and its **actual** measured output — the five SC-004 ns/block figures verbatim, SC-005's and SC-007's measured floors (**O-2**), and whichever levers were spent | — |

**Commit granularity:** T1+T2 as one commit (the RA-1 amendment plus its own tests, so the shared
primitive is never in the tree untested); T3–T12 as one commit per T; T13 folded into the last; T14 its
own. No commit is pushed without explicit instruction.

---

## 20. Summary of what ships

- **1 new header** — `dsp/include/krate/dsp/systems/atmosphere_engine.h`, Layer 3, one class
  (`AtmosphereEngine`) with four private nested aggregates.
- **1 amended header** — `dsp/include/krate/dsp/primitives/rolling_capture_buffer.h`, one new `const`
  method, **one** new stdlib include (`<cmath>`), nothing else touched.
- **4 new test TUs** + **1 extended test TU**, 26 `TEST_CASE`s, covering all 8 lettered FR groups
  (FR-001 … FR-084) and all 14 success criteria. The two FR groups that had no assigned assertion before
  the plan review — **FR-062** (no dry pass-through) and **FR-064** (denormal guard / exact silence at
  `level = 0`) — now have explicit clauses in `AtmosphereEngine_CaptureAndColdRing` and
  `AtmosphereEngine_ControlTableClamps` respectively (§15.8), so the coverage claim is checkable rather
  than asserted.
- **2 edits** to `dsp/tests/CMakeLists.txt`.
- **0 plugin changes**, **0 changes** to `GranularEngine`, `GrainProcessor`, `GrainPool`, `GranularDelay`,
  `SlicePool`, `GrainScheduler`, `GrainEnvelope`, `STFT`, `OverlapAdd`, `SpectralBuffer`,
  `SpectralFreezeOscillator`, `BrownianDrift` or `stereo_utils.h`.

---

## 21. Review notes

**No review issue was rejected.** All 20 issues from the plan review were applied — 4 blockers, 9 majors
and 7 minors — as §17 rows **D-10 … D-18** plus the in-place corrections they reference. Two of the
majors (the blur-FIFO pre-fill, and `SpectralFreezeOscillator::reset()`) were raised twice under different
lenses and are recorded once each, as **D-14** and **D-10**.

This section exists for the one thing the review could not be resolved entirely inside: **five of the
corrections contradict text that lives in `spec.md`, which this revision does not edit.** They are
recorded here so the compliance pass reconciles them explicitly rather than discovering a plan/spec
mismatch and "fixing" it in the wrong direction.

| Plan deviation | `spec.md` text it contradicts | What the spec should say |
|---|---|---|
| **D-11** | FR-014 (`spec.md:471-473`): "`getAvailableSamples()` ≥ the birth read age plus `kMinAgeSamples = 64`" | "…plus `kInterpMarginSamples = 2`." With `+64` on both the young side (inside `aLo`) and the old side (admission), FR-014 and FR-025 are jointly unsatisfiable and the engine is permanently silent in reachable configurations. The `+64` young-side guarantee is unchanged. |
| **D-12** | FR-025's closed form `L′ = ⌊(C − 2 − g)/w⌋`, and the worked edge-case numbers derived from it (`spec.md:1305-1340`, e.g. `⌊(524 288 − 2 − 64)/1.5⌋ = 349 481`) | `L′ = ⌊(C − 2 − g − dR − 2)/w⌋`. The worked numbers shift by ≤ 1 sample (349 481 → 349 480 at `dR = 0`). Without the reserved 2 the birth window can invert and `std::clamp` is called with `lo > hi` — undefined behaviour. |
| **D-13** | FR-027 (`spec.md:593-602`): "Forcing two of 4096 entries changes no shipped component … and is measured" | The forced region is `table[0]` plus a **tail run of 2**, and it only works together with FR-026's phase denominator being `L′ − 1`. Under the spec's `1/L′` the last table entry is never read, so forcing it is a no-op and `Exponential` still terminates on ≈1.9 % of amplitude at short lifetimes. |
| **D-17** | SC-003's precondition (`spec.md:1000-1001`) applied to the whole `grainSeconds` sweep | Scope it to `grainSeconds ∈ {5, 30}`. At `density ≤ 20` (FR-009, `spec.md:394`) the other three cells cannot reach 64 concurrent grains, so the precondition fails a correct implementation there. |
| **D-18** | SC-009's allocation clause (equality of allocation counts between a re-`prepare` and a fresh `prepare`) | `secondPrepareCount <= freshPrepareCount`, plus **0** allocations across a full render at the new rate. Equality cannot hold while `prepare` uses `resize`/`assign` on already-sized buffers. |

Two further items are plan-internal and need no spec change, but are called out because they are easy to
undo by accident:

- **§16.1(2) — `atmosphere_engine_test.cpp` must stay OUT of the `-fno-fast-math` list.** It looks like an
  oversight next to the nonfinite TU; it is the opposite. SC-014's fourth clause and SC-012's sub-case 6
  are the only checks that the finiteness guards survive the flags the header actually ships under
  (**D-16**). Adding the TU to that list disables them silently and every suite stays green.
- **§11.2 — `blurSmoother_.advanceSamples()` must stay OUT of the per-channel loop.** Moving it inside is
  a one-line change that halves the blur smoothing time constant and desynchronises L from R, and no
  criterion that sweeps settled blur values can see it (**D-15**). The settling-time clause in
  `AtmosphereEngine_ControlTableClamps` is the only guard.

Finally, three numbers quoted in the earlier draft were **not** in the files they cited and have been
replaced with what those files actually contain: the `HarmonicCloud` perf figures (§9.5, R-3 — the file's
measured ranges are `33,257–34,184 → 21,917–25,262` automated and `31,281–32,027 → 20,641–23,154` static
at `harmonic_cloud_perf_test.cpp:99-100`; `26,000` is the checked-in baseline constant at `:140`, not a
measurement), the attribution of that lever (`centsToPitchRatioFast`, unusable at Phase 5's ±3600 cents),
and `SpectralFreezeOscillator`'s API surface (§0, §12.4 — it has a public `reset()` at `:173-196`).
