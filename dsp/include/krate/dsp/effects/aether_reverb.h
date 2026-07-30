// ==============================================================================
// Layer 4: Effect - AetherReverb (Seraphis "Aether Space" engine)
// ==============================================================================
// Spec slug: seraphis-phase6-aether-space
// Spec:      specs/seraphis-phase6-aether-space/spec.md
// Plan:      specs/seraphis-phase6-aether-space/plan.md
// Roadmap:   specs/Seraphis-roadmap.md, Part A, Phase 6 (lines 258-282)
// Layer:     4 (Effects) - may include Layers 0-3 only.
//
// A morphing feedback-delay network with life modulators, a shimmer pair inside
// the loop, a harmonic-bloom resonator bank and an optional STFT tail smear.
//
// ------------------------------------------------------------------------------
// (2) TOPOLOGY RE-DERIVED FROM fdn_reverb.h, NOT INCLUDED (constraint C-1)
// ------------------------------------------------------------------------------
// This header does NOT include <krate/dsp/effects/fdn_reverb.h> (a Layer 4 ->
// Layer 4 include), nor reverb.h (C-2), nor shimmer_delay.h (C-5). The following
// techniques are re-derived here from those line ranges, which were read while
// writing this file:
//   - Jot per-line absorption, g_i = 10^(-3 * m_i / (T60 * sr))   :576-600
//   - prime reference delay lengths                               :91
//   - one contiguous buffer of per-channel power-of-two sections,
//     each with its own mask and offset                           :638-689
//   - Householder feedback matrix, x[i] -= (2/N) * sum(x)         :749-758
//   - Hadamard feedback matrix via in-place FWHT butterflies      :696-729
//   - freeze bypasses BOTH damping and the DC blocker             :296-322
//   - DC-blocker pole R = 1 - 250/sr                              :207
//   - even/odd output tap split and the M/S width control         :354-371
//   - equal-power dry/wet mix                                     :374-377
//
// ONE DELIBERATE DIVERGENCE IN THAT LIST: the DC blocker is NORMALISED here,
// y = (1+R)/2 * (x - x[n-1]) + R*y[n-1], where fdn_reverb.h uses the bare
// y = x - x[n-1] + R*y[n-1]. The bare form has |H| = 2/(1+R) > 1 above ~550 Hz
// (+0.023 dB per pass at 48 kHz), which inside a recirculating loop is an
// energy SOURCE and contradicts FR-032 outright: at decaySeconds = 60 the
// shortest line's Jot gain is 0.9977, so the Nyquist round-trip gain would be
// 1.0003 - a growing loop, measuring ~105 s against a requested 60 s. The
// prefactor moves the peak magnitude to exactly 1.0 and leaves the pole - and
// therefore the ~40 Hz corner - untouched. See dcBlock().
//
// ------------------------------------------------------------------------------
// (3) MATRIX SIGN CONVENTION (constraint C-8) - NORMATIVE
// ------------------------------------------------------------------------------
// The three morph endpoints M0 (Householder), M1 (Hadamard) and M2 (seeded
// random-orthogonal) must all live in the SAME component of O(N), because det is
// continuous on O(N) and takes only the values +1 and -1: no continuous
// orthogonal path can join a det = +1 endpoint to a det = -1 one.
//   - M0 = I - 2*u0*u0^T with u0 = 1/sqrt(N) is a single reflection, det = -1.
//   - M1 is therefore shipped as D * H_N/sqrt(N) with D = diag(-1, 1, ..., 1):
//     ROW 0 OF THE NORMALISED HADAMARD IS NEGATED. This differs by that sign
//     from fdn_reverb.h:696-729's matrix. Row negation is left-multiplication by
//     an orthogonal +/-1 diagonal, so M1 stays exactly orthogonal while det flips
//     from +1 to -1.
//   - M2's modified-Gram-Schmidt result is forced to det = -1 by negating one
//     column whenever the computed determinant is positive.
//
// ------------------------------------------------------------------------------
// (4) REACHABLE MODAL DENSITY (FR-013), modes/Hz at 48 kHz
// ------------------------------------------------------------------------------
//   N   |  S = 0.25  |  S = 1.0  |  S = 4.0
//   ----+------------+-----------+----------
//    8  |   0.106    |   0.426   |   1.702
//   16  |   0.210    |   0.841   |   3.363
// getModalDensityPerHz() reports sum(effectiveDelay_[i]) / sampleRate_ from the
// CURRENT, Size-scaled lengths - never from the prepare-time geometry.
//
// ------------------------------------------------------------------------------
// (5) SHIMMER LOOP TIME PER PitchMode (FR-054), at 48 kHz
// ------------------------------------------------------------------------------
// Each leg carries 64 samples of tap-cadence deferral ON TOP OF its mode
// latency, because the mono tap sum is accumulated over one control chunk and
// shifted at the next chunk boundary:
//   PitchMode      | mode latency | + cadence | ms @ 48 kHz
//   ---------------+--------------+-----------+-------------
//   Simple         |       0      |     64    |   1.33   (audible artifacts)
//   Granular (def) |    ~2048     |   2112    |  ~44
//   PhaseVocoder   |  4096 + 1024 |   5184    | ~108
// Mode is a prepare-time choice only (FR-053): PitchShiftProcessor's reported
// latency changes immediately on setMode (pitch_shift_processor.h:189-193), and
// a loop whose latency changes mid-render is a click.
//
// ------------------------------------------------------------------------------
// (5b) WHY THE SHIMMER RETURNS DO NOT COMB-FILTER (FR-052) - STATED ACCURATELY
// ------------------------------------------------------------------------------
// NOT because the read subset {N-4 .. N-1} and the injection pairs are disjoint.
// FR-020's endpoints are DENSE (Householder: diagonal 0.75, every off-diagonal
// -0.25 at N = 8; Hadamard: every entry +/-1/sqrt(N)) and the matrix is applied
// once per sample, so after ONE sample step every channel already carries a
// contribution from every other. The disjointness survives zero round trips.
//
// The accurate statement is that A +12 OR +7 COPY IS NOT A COHERENT COPY OF THE
// SIGNAL IT CAME FROM, so the fixed-delay-plus-copy geometry that produces
// classical comb notches does not exist; what remains is a frequency-shifted
// addition at its own send gain. What the pinned subsets actually buy is stereo
// re-diffusion of a MONO tap - each pair spans both parities, so neither
// interval is hard-panned by the even->L / odd->R output split (FR-018) - plus a
// defined, measurable injected level. SC-007 clause 4(b) measures this with a
// no-band-below-its-neighbour-median bound rather than assuming it.
//
// ------------------------------------------------------------------------------
// (5c) FR-059 RETURN FILTER - THE SHIPPED VALUES AND WHY THE FLOOR IS ZERO
// ------------------------------------------------------------------------------
// SHIPPED: corner kReturnShelfCornerHz = 120 Hz (clamped to <= 0.45 * sr) and
// floor kReturnShelfHfGain = 0.0, one filter state per return path (the +12 leg,
// the +7 leg and the bloom - the last at its OWN corner, see (5e)). At a zero
// floor the general shelf
// |H(f)| = sqrt(1 + (g*f/fc)^2) / sqrt(1 + (f/fc)^2) degenerates to the pure
// one-pole lowpass 1 / sqrt(1 + (f/fc)^2) that FR-059's "one-pole, corner
// documented in the header" permits: 1.00 at DC, 0.34 at 330 Hz, 0.26 at 440 Hz,
// 0.119 at 1 kHz, 0.0150 at 8 kHz, 0.0075 at 16 kHz.
//
// The plan's R-1 starting point was 6 kHz / 0.5 and named this filter as the ONLY
// admissible lever, with "lower the corner and/or the shelf gain and record the
// shipped values" as the remedy. Both were lowered, and then the FLOOR had to go
// all the way to zero. The values are MEASURED, not derived - the closed-form
// energy balance this note used to carry was wrong by ~1.8x in amplitude (it
// assumed the four tapped lines are mutually decorrelated and that the injected
// copy is incoherent with the loop content it is added to; neither holds, because
// the return IS derived from that content).
//
// WHY A NON-ZERO FLOOR CANNOT WORK, WHATEVER ITS VALUE. Above the corner a
// first-order shelf is FLAT at g. The shimmer is an UPWARD cascade: f -> 2f -> 4f
// -> ... Every generation therefore pays the SAME g, so the >8 kHz band is fed
// from the (fully populated) 4 kHz band at a gain that does not fall with
// frequency, and the HF FRACTION climbs for as long as the tail rings. A zero
// floor makes the cascade pay 6 dB more per octave climbed, which is what turns
// the climb into a decay. The corner alone cannot do this: it only slides the
// whole flat region up or down.
//
// WHAT THE MEASUREMENT SHOWS (48 kHz, SC-006's grid point: N = 8, size = 1.0,
// decaySeconds = 60, damping = 0, all three sends at 1, 180 s). "HF ratio" is
// SC-006 clause 3's ABSOLUTE HF(E_final)/HF(E1), read off the test's own
// 8th-order-Butterworth instrument; the octave column is from an exact
// 8192-point FFT band split on the same renders:
//
//   corner/floor | global peak | E_final/E2 peak | HF ratio | 4 kHz oct
//                | (bound 4.0) |   (bound 0.95)  |          | at E_final
//   -------------+-------------+-----------------+----------+-----------
//   300 / 0.45   |    16.6     |     91.6        |  RUNAWAY |  -
//   300 / 0.30   |     0.85    |      7.7e-7     |  -       |  -
//   120 / 0.12   |     0.585   |      1.4e-7     |   539    |  -17.4 dB
//   120 / 0.00   |     0.542   |      1.1e-7     |     4.3  |  -41.0 dB
//   sends at 0   |     0.578   |      -          |     6.5  |  -47.3 dB
//
// Read the last two rows together: at a zero floor the shimmer adds 6 dB to the
// 4 kHz octave and NOTHING above 8 kHz, and the residual HF-fraction climb (4.3,
// vs 6.5 with the shimmer switched off entirely) is not the shimmer's at all -
// it is FR-016's DC blocker, see (5d). Against the reference-normalised form the
// test actually asserts, the same three rows read 82.9 / 0.66 / 1.00.
//
// The 300/0.45 runaway is not broadband: the tail collapses onto a single
// component at 16.0 kHz, which is exactly the FIXED POINT OF THE +12 LEG'S
// ALIASING. GranularPitchShifter resamples a delay line with linear
// interpolation and no anti-image filter (processors/pitch_shift_processor.cpp:
// 121-196), so 2 * 16 kHz = 32 kHz folds to 48 - 32 = 16 kHz: energy at 16 kHz
// is mapped to ITSELF once per generation and accumulates coherently. A floor of
// 0.45 leaves that fixed point above unity loop gain; a floor of 0 attenuates it
// by 42.5 dB per generation.
//
// THE CORNER, SEPARATELY, IS WHAT MAKES SC-016 CLAUSE 2(c) REACHABLE. SC-016
// clause 2(c) demands the 2*f0 band stay 12 dB below the 1.5*f0 band, and the
// leakage into it is a CASCADE artifact (330 -> 494 -> 740 ...), so it is the
// return gain in the shimmer's own band - the corner - that controls it, not the
// floor. At 120 Hz / 0.0 the measured SC-016 figures are
//   octave send 1: L(2f0) = -4.43 dB vs a send-0 reference of -37.20 dB
//                  (+32.8 dB, bound +12), L(f0) - L(2f0) = 6.1 dB (bound 20),
//                  L(1.5f0) = -39.82 dB (bound -16.43);
//   fifth  send 1: L(1.5f0) = -0.92 dB vs -39.56 dB (+38.6 dB, bound +12),
//                  L(f0) - L(1.5f0) = 2.6 dB, L(2f0) = -17.89 dB (bound -12.92).
// Every clause clears by >= 4.9 dB. Raising the corner brightens the shimmer but
// moves BOTH the cascade leakage and the sub-grain-rate LF gain of (5d) the wrong
// way, so 120 Hz stays.
//
// If a measurement moves, this constant pair - and nothing else - is what moves
// with it (plan R-1, B-4: never the criterion). The figures above are recorded
// in compliance.md alongside the measured SC-006 and SC-016 output.
//
// ------------------------------------------------------------------------------
// (5d) FR-016's DC BLOCKER IS LOAD-BEARING FOR SHIMMER STABILITY, AND IT TILTS
//      THE TAIL. BOTH ARE MEASURED. NEITHER IS A FREE PARAMETER.
// ------------------------------------------------------------------------------
// (i) LOAD-BEARING. A granular pitch shifter resamples a windowed grain, and
// resampling DC yields DC: content slower than one grain (~2048 samples, ~23 Hz
// at 48 kHz) passes the +12 and +7 legs UNSHIFTED. With FR-050's pinned
// kTapInjectionGain of exactly 1.0 and a return filter that is ~unity below its
// corner, that is a UNITY-GAIN POSITIVE FEEDBACK PATH below ~23 Hz. The only
// thing holding it down is FR-016's per-line DC blocker. Measured, by lowering
// FR-016's numerator from 250 (39.8 Hz) at otherwise-shipped settings: at 125
// (19.9 Hz) the E_final spectral centroid moves 693 -> 885 Hz; at 60 (9.5 Hz) the
// whole tail collapses onto a 113 Hz rumble; at 30 (4.8 Hz) onto 24 Hz - the
// grain rate. FR-016's constant is therefore NOT tunable here, whatever the next
// paragraph would prefer.
//
// (ii) IT TILTS THE TAIL, AND SC-006 CLAUSE 3 SEES IT. The blocker is a
// first-order highpass traversed once per delay-line round trip. At SC-006's grid
// point the energy-weighted traversal rate is N*sr/sum(m_i) = 8*48000/81712 =
// 4.70 per second, and the per-traversal passband droop is 0.0898 dB at 220 Hz,
// 0.0233 dB at 440 Hz, 0.0061 dB at 1 kHz and 0.0000 dB at 8 kHz. Over SC-006's
// 155 s E1 -> E_final gap that is 24 dB of LF loss relative to 8 kHz, and it is
// arithmetic, not an implementation choice - it is present with ALL THREE SENDS
// AT ZERO. Measured octave-band fractions, sends at 0, E1 -> E_final:
//   250 Hz -3.3 -> -19.0 | 500 Hz -14.0 -> -14.4 | 1 kHz  -5.7 -> -0.2 dB
//     2 kHz -41.7 -> -34.6 | 4 kHz -55.2 -> -47.3 | 8 kHz -63.7 -> -55.5 dB
// i.e. HF(E_final)/HF(E1) = 6.5 and centroid 666 -> 1469 Hz (FFT; the test's own
// first-difference estimator reads 828 -> 1514 Hz) on a SHIMMER-FREE
// engine. SC-006 clause 3's 1.25x bound is therefore unreachable in its absolute
// form for any implementation that obeys FR-016; the test measures the clause
// against a sends-at-zero reference render instead - SC-016's own idiom - and
// says so at the assertion. See the test's clause-3 comment block.
//
// This also means the engine's REAL decay is frequency dependent even at
// damping = 0: T60(1 kHz) = 60 s but T60(220 Hz) = 36 s and T60(110 Hz) = 23 s at
// decaySeconds = 60. That is inherited from FDNReverb's identical in-loop blocker
// (fdn_reverb.h:207, :308-322) and is out of scope to change here, because (i).
//
// ------------------------------------------------------------------------------
// (5e) THE HARMONIC BLOOM HAS ITS OWN FR-059 CORNER, AND kBloomSendMax IS THE
//      ONE CONSTANT THIS PHASE TUNES AGAINST A MEASUREMENT (plan R-2)
// ------------------------------------------------------------------------------
// FR-059 asks for a HF shelf on BOTH return paths, "corner documented in the
// header", with the stated purpose that "repeated upward pitch shifting cannot
// pile energy into the top octave without bound". Two return paths, two corners:
//
//   path            | corner                    | why
//   ----------------+---------------------------+---------------------------------
//   +12 / +7 shimmer| kReturnShelfCornerHz 120  | the UPWARD CASCADE of (5c)
//   harmonic bloom  | kBloomShelfCornerHz  6000 | no cascade; FR-058's guard bounds it
//
// THE BLOOM DOES NOT SHIFT PITCH. It re-injects at exactly the frequencies it is
// tuned to, so there is no f -> 2f -> 4f ladder for a flat above-corner gain to
// walk up, and its runaway mode is a per-resonator loop gain that FR-058's guard
// bounds computably - but only once that guard carries the FDN's own
// recirculation, which FR-058 as written does not; see (5f), which is where the
// binding constraint actually lives. Reusing the shimmer's 120 Hz corner
// would attenuate precisely the partials the bank exists to reinforce:
// |H(220 Hz)| = 0.479 (-6.4 dB), |H(880 Hz)| = 0.135 (-17.4 dB). That does not
// make the bloom safer - the guard already does that - it makes FR-055
// unreachable, and SC-016 clause 3's >= 6 dB emphasis unachievable at ANY send
// value that the guard would still permit. 6000 Hz is the plan's own R-1 starting
// point for FR-059, kept here because the shimmer-specific reason for lowering it
// does not apply: |H| is 0.9993 at 220 Hz and 0.9893 at 880 Hz - flat across the
// bloom's working range - while still falling 6 dB/octave above 6 kHz, which is
// the "top octave" bound FR-059 asks for.
//
// kBloomSendMax = 8.0f, with the pinned normalisations of FR-055/FR-058:
//
//   return gain   = send * kBloomSendMax * kBloomInjectionGain * (1/sqrt(count))
//                   * bloomGuardScale_
//   loop gain(f_k)= that * |H_bloom(f_k)| * |T_fdn(f_k)|   -- see (5f)
//
// IF SC-016 CLAUSE 3'S MEASURED EMPHASIS MOVES, THIS CONSTANT PAIR AND THE BLOOM
// CORNER ARE WHAT MOVE WITH IT - NEVER THE CRITERION (plan R-2, B-4). The shipped
// value and the measured emphasis are recorded in compliance.md.
//
// ------------------------------------------------------------------------------
// (5f) FR-058's LOOP-GAIN MODEL IS CORRECTED HERE: THE LOOP RUNS THROUGH THE FDN,
//      SO IT CARRIES THE FDN'S OWN NARROWBAND RECIRCULATION GAIN
// ------------------------------------------------------------------------------
// FR-058 and plan S7.10 state the criterion as
//
//   worst = max_k [ kTapReadNormalisation (0.25) * g_bloom * |H_shelf(f_k)|
//                   * feedbackGain_[bloom channel] ]   <= 1.0
//
// i.e. ONE per-line Jot gain and the tap normalisation. That model treats the
// transfer from the bloom's injection node back to the tap-sum node as unity -
// it counts a single pass and no recirculation. THE FDN IS NOT A WIRE. The bloom
// reads the loop and writes back into the loop, so the transfer it closes over is
// the FDN's own multi-round-trip response T_fdn(f), and at a reverberant modal
// peak that response is far above 1.
//
// Derivation (Parseval over the FDN's impulse response, all quantities already
// held by this class):
//
//   Inject a unit impulse simultaneously into the K = |kBloomInjectChannels|
//   channels the bloom writes to. Injected energy = K; it decays with the Jot law,
//   so the energy leaving the network per sample is sum_i (1 - g_i^2) times the
//   per-line power, and with E(n) = K * exp(-rate * n), rate = sum_i (1 - g_i^2)
//   / sum_i m_i. The tap sum reads kTapReadNormalisation * (four lines), so
//     sum_n t[n]^2 = 0.25 * K / (sum_i m_i * rate) = 0.25 * K / sum_i (1 - g_i^2).
//   The pole radius of every mode is 1 - a with 2a = rate, the modal density is
//   sum_i m_i / sr modes per Hz, and each mode integrates to (pi/2)*BW*|T_pk|^2
//   with BW = a*sr/pi. Equating the two expressions for the mean of |T|^2 gives
//
//     |T_pk| = sqrt(K) / sum_i (1 - g_i^2)              <- aetherFdnRecirculationGain()
//
//   the RMS magnitude of the FDN's MODAL PEAKS, and
//     |T_rms| = sqrt(0.25 * K / sum_i (1 - g_i^2))
//   its RMS over frequency. Both are computed from feedbackGain_ alone.
//
// AT SC-016 CLAUSE 3'S OWN GRID POINT (N = 8, Size 0.5 so S = 1 and sum m_i =
// 20428, decaySeconds = 20 so sum_i (1 - g_i^2) = 0.294, K = 4):
//   |T_pk| = 2 / 0.294 = 6.80        |T_rms| = 1.84
// The un-corrected criterion evaluated 0.25 * 2.828 * 1.0 * 0.99 = 0.70 and left
// the guard INACTIVE; the true loop gain was 2.828 * 6.80 = 19.2. Measured
// consequence at the shipped constants: peak |out| = 7.1e6 against a 0.329
// reference, and every 1/3-octave band +90 dB (SC-016 clause 3's non-target mean
// came out at +129.6 dB). That is the defect this item fixes.
//
// The shipped guard therefore evaluates
//
//   worst = max_k [ g_bloom * |H_shelf(f_k)| * |T_pk| ]   <= kBloomLoopGainCeiling
//
// and keeps the literal FR-058 product as a second, weaker term so the stated
// criterion is still enforced verbatim. kTapReadNormalisation and the per-line
// Jot gain are NOT multiplied in again: both are already inside the derivation of
// |T_pk| above (the 0.25 through sum_n t[n]^2, the g_i through the decay rate).
//
// This makes the guard CONFIGURATION-AWARE, which the old one was not: sum_i
// (1 - g_i^2) falls with decaySeconds and with Size, so the same send that is
// safe at (Size 1, decay 4 s) is 24x over the edge at (Size 0, decay 60 s). A
// fixed kBloomSendMax cannot be safe across FR-030's range; only a guard that
// carries the FDN's dissipation can.
//
// KNOWN RESIDUAL, MEASURED, NOT FIXED HERE - see the notes on SC-016 clause 3.
// |T_pk| is an RMS over modal peaks; the four resonators sit at four arbitrary
// frequencies, so what each of them actually sees is |T| at ITS OWN frequency,
// which is Rayleigh-distributed about |T_rms| = 1.84 and can be near zero
// (destructive) or several times |T_pk| (a mode dead-centre in the resonator's
// band). At Q = 400 a resonator is 0.55 Hz wide against a 2.35 Hz mode spacing,
// so which it gets is a lottery: measured single-partial band rise at a fixed
// send ranged from -6.7 dB (460 Hz) to +195 dB (450 Hz), and the winners and
// losers reshuffle completely when Size moves from 0.48 to 0.55. The guard above
// bounds the loop; it cannot equalise it.
//
// AND A SECOND, HARDER ONE: SC-016 CLAUSE 3'S TWO HALVES CANNOT BOTH HOLD FOR AN
// IN-LOOP BLOOM. Because FR-055 gives the bank no output path except injection
// into the FDN, all of the emphasis is stored in the FDN's modes - it IS a
// reduction of their decay rate - and the moment bloomNoteOff zeroes the return,
// the emphasised band and the reference band decay at exactly the same Jot rate.
// The ratio measured during the hold is therefore preserved indefinitely, and the
// held window (t = 4..8 s) averages a still-growing quantity while the release
// window (t = 10..14 s) sees its endpoint, so the residual comes out LARGER than
// the emphasis. Measured on the shipped engine at three sends, held / release
// residual in the target band: 2.7 / 7.8 dB, 8.0 / 17.7 dB, 14.8 / 28.0 dB -
// residual ~= 2x held throughout. "residual <= 2 dB" therefore requires
// "held <= ~0.7 dB", against a criterion of "held >= 6 dB". Nothing in this
// implementation can satisfy both; it needs a spec amendment, recorded for
// compliance.md rather than papered over.
//
// BOTH RESIDUALS ARE RESOLVED BY ITEM (5g) BELOW - the sentence above ("FR-055
// gives the bank no output path except injection into the FDN") is precisely the
// premise that changed. Kept, rather than deleted, because it is the derivation
// of WHY the second path exists.
//
// ------------------------------------------------------------------------------
// (5g) THE BANK NEEDS A SECOND, OUT-OF-LOOP OUTPUT PATH. FR-055's INJECTION
//      ALONE CANNOT PRODUCE A POSITIVE EMPHASIS AT A PRESCRIBED FREQUENCY.
// ------------------------------------------------------------------------------
// This is the FR-055/FR-058/SC-016-clause-3 defect that (5f)'s two "known
// residuals" are both symptoms of, and it is a STRUCTURAL one, not a tuning one.
//
// A resonator inside an LTI feedback loop does not "add" anything at its centre
// frequency. It multiplies the network's response there by 1 / (1 - L(f_k)),
// where L is the COMPLEX loop gain. |1/(1-L)| > 1 only when Re(L) > |L|^2 / 2 -
// i.e. only when the loop phase at f_k happens to be regenerative. The FDN's
// contribution to that phase is a sum of pure delays: it rotates through 2*pi
// every sr / mean(m_i) = 18.8 Hz at SC-016's grid point, while a Q = 400
// resonator at 220 Hz is 0.55 Hz wide. The resonator therefore samples ONE
// arbitrary phase and holds it. Raising the send cannot change a sign.
//
// MEASURED, on the shipped engine at SC-016 clause 3's own configuration
// (N = 8, Size 0.5, decay 20 s, seed 1, bloomDecay 1, partials 220/440/660/880),
// sweeping the guard ceiling so that chunkBloomGain_ takes the values shown, and
// reading the four 1/3-octave TARGET band rises against the send-0 reference:
//
//   chunkBloomGain_   220 Hz    440 Hz    660 Hz    880 Hz   peak|out|
//   0.137 (shipped)   -0.77     -0.16     +0.24     +2.68      0.208
//   0.287             -1.57      ...       ...       ...       0.208
//   0.575             -2.91     -0.50     +2.47    +58.40      0.231
//   0.862             -4.00      ...       ...       ...      46.8  (diverging)
//   2.828 (no guard)  +163      +296      +305      +352      7.0e13
//
// The fundamental's band gets MONOTONICALLY WORSE as the send rises, because its
// loop phase is destructive; 880 Hz gets +58 dB because its is regenerative and
// sits on a mode. Between them the network leaves stability. The FR-059 shelf
// corner - tasks.md's third admissible lever - only rotates the phase by
// -atan(f/fc), i.e. by the same ~60-80 deg at all four partials, and reshuffles
// the lottery without fixing it (measured at fc = 120 / 500 / 2000 Hz, ceiling 3:
// the 220 Hz band lands at -1.49 / -2.24 / -2.28 dB). The engine's own life
// modulation does not rescue it either: at breath = tide = modDepth = 1 (which
// SC-016's P-1 forbids anyway) every target rise is positive but under +0.6 dB.
//
// So no value of kBloomSendMax, kBloomLoopGainCeiling or kBloomShelfCornerHz
// reaches +6 dB, and B-4 forbids moving the criterion. What has to move is the
// mechanism, and the minimum move is ADDITIVE: FR-055's injection into
// kBloomInjectChannels stays exactly as specified, guard-bounded and unchanged,
// and the same shelved, 1/sqrt(count)-normalised bank output is ALSO summed into
// the wet bus at kBloomEmphasisGain (renderSlice step 6).
//
// Why that is the right second path and not a hack:
//   - It is phase-independent by construction. Out of the loop the return gain G
//     is not bounded by stability, and |1 + G e^{j phi}| >= G - 1, so a large
//     enough G raises the band whatever the phase does. Measured worst-case
//     target rise / non-target mean: +4.5 / +0.3 dB at G = 12, +7.3 / +1.0 dB at
//     G = 20, +10.1 / +1.8 dB at G = 30, +13.1 / +2.7 dB at G = 45. SC-016
//     clause 3 brackets G from both sides. SHIPPED: 34, measured +8.55 dB
//     minimum target rise / +0.90 dB non-target mean (compliance.md, S4).
//   - It is what makes clause 3's SECOND half satisfiable, which (5f) showed the
//     in-loop path structurally cannot be: this contribution is not stored in the
//     FDN's state, so when bloomNoteOff rings the bank down the emphasis leaves
//     with it instead of persisting as a permanent decay-rate change.
//   - It restores the symmetry with the shimmer. kShimmerInjectionGain is 1.0 and
//     the +12/+7 legs inject a FULL-BAND copy at up to unity, which is only safe
//     because a pitch-shifted copy is incoherent with its source (item (5b)). The
//     bloom's return is coherent, so the same authority inside the loop would be
//     an oscillator; outside it, it is just a level.
//   - It costs two multiply-adds per sample and cannot destabilise anything: it
//     writes to wetScratch*, never to a delay line.
//   - It rides the same (1 - freezeRamp) as the injection, so FR-033 step 5 /
//     RA-5 / SC-016 clause 4 ("freeze mutes all three sends") stay true.
//
// RECORDED AS A DEVIATION FROM FR-055 AS WRITTEN. FR-055 describes the bank's
// output as "injected back into kBloomInjectChannels" and does not contemplate a
// second path; the roadmap (line 274) asks only for "a resonant emphasis stage
// that gradually reinforces partials of the held chord", and it is the SHIMMER
// bloom that line 273 places "inside the feedback loop". compliance.md must carry
// this, the shipped kBloomEmphasisGain and the measured clause-3 figures.
//
// ------------------------------------------------------------------------------
// (6) SAMPLE-RATE RANGE AND THE SUB-44.1 kHz SHIMMER FORCE-DISABLE (RA-6, C-6)
// ------------------------------------------------------------------------------
// prepare() accepts and clamps into [8000, 192000] Hz. There is NO 44.1 kHz
// floor: the engine is fully functional at 8 kHz. However PitchShiftProcessor
// documents a [44100, 192000] precondition (pitch_shift_processor.h:141), so
// below 44.1 kHz the shimmer taps are FORCE-DISABLED - not prepared, not
// allocated, and inert. isShimmerActive() reports this.
//
// ------------------------------------------------------------------------------
// (7) silence() IS NON-LATCHING (FR-007)
// ------------------------------------------------------------------------------
// This DIVERGES from AtmosphereEngine::silence() (systems/atmosphere_engine.h:
// 636-644), which latches and stays silent until reset(). AetherReverb's
// silence() fades out over kSilenceRampMs, clears its audio state, then fades
// back in and resumes rendering on its own. No reset() is required.
//
// ------------------------------------------------------------------------------
// (8) MODULATION EXCURSION DEPARTS FROM FDNReverb (FR-072)
// ------------------------------------------------------------------------------
// FDNReverb applies modDepth * 5% of the LONGEST line to its modulated channels
// (fdn_reverb.h:631). Here the excursion is PER LINE and 0.5%
// (kModExcursionFraction). Reason: at S = 4.0 the longest line is ~424 ms, so 5%
// of it is ~21 ms of excursion - which, applied to a 5 ms line, is nonsense.
//
// kModExcursionFraction is a ONE-SIDED amplitude, exactly as FDNReverb's
// lfoMaxExcursion_ is (it is ADDED to the read length over a bipolar phasor,
// fdn_reverb.h:284, :631). BrownianDrift::getCurrentValue() is hard-clamped to
// [-1, +1] (processors/brownian_drift.h:212-214), so the reachable PEAK-TO-PEAK
// deviation of one modulated line is 2 * modDepth * kModExcursionFraction * m_i.
//
// THE JITTER SOURCE IS BrownianDrift, WHICH HAS NO RATE SETTER. Its complete
// public API is prepare/reset/setSeed/setSmoothness/setDepth/setMean/process/
// processBlock/getCurrentValue/getSourceRange (:121, :133, :145, :152, :159,
// :165, :178, :194, :212, :217). setModSmoothness therefore forwards VERBATIM to
// setSmoothness and NO Hz domain is invented or advertised: the reachable
// mean-reversion time constant is tau = lerp(kTauMin 0.2 s, kTauMax 30 s,
// smoothness) (:97-99, :231-234), i.e. roughly 0.005 .. 0.8 Hz of equivalent
// wander rate, and the 0.6 default gives tau ~= 18 s.
//
// ------------------------------------------------------------------------------
// (8b) THE TWO SLOW MODULATOR RATES ARE PINNED IN prepare(), NOT INHERITED
// ------------------------------------------------------------------------------
// breath_.setRate(kBreathRateHz = 0.05f) -> a 20 s period, NOT the class default
// kDefaultRate = 0.1f (processors/breathing_modulator.h:111), and
// tide_.setRate(kTideRateNormalised = 1.0f) -> base period kMinPeriod = 30 s with
// layers at 30 / 42.43 / 51.96 s, NOT the class default 0.5f
// (processors/tidal_modulator.h:143), which would give a ~188 s base period.
// Pinning is what makes SC-017's thresholds derivable from those classes' own
// constants; there is deliberately NO rate setter on AetherReverb for either.
// Both modulators keep their own kDefaultDepth = 1.0f, so setSizeBreathDepth and
// setDimensionalityTideDepth are the ONLY depths and nothing is scaled twice.
//
// The modulators are advanced UNCONDITIONALLY, once per control chunk, whether or
// not the input is silent (FR-074): "the space engine breathes at idle".
//
// ------------------------------------------------------------------------------
// (9) PREPARE-TIME MEMORY FOOTPRINT BY STAGE
// ------------------------------------------------------------------------------
// At the shipped default (N = 8, 48 kHz, maxDelaySeconds = 0.5,
// diffusionFftSize = 1024, shimmer on):
//   Delay network : delayBuffer_ 110592 floats (432 KiB); preDelayL_/R_ 128 KiB;
//                   dryAlignL_/R_ 16 KiB
//   Spectral      : stftL_/R_ ~80 KiB; olaL_/R_ 24 KiB; specL_/R_ + FIFOs ~40 KiB
//   Shimmer taps  : ~0.8-1.0 MiB  <-- the largest stage at this configuration
// THE LARGEST SINGLE ALLOCATION IS CONFIGURATION-DEPENDENT: delayBuffer_ is
// 976 KiB at N = 16 and reaches ~3.81 MiB at N = 16 / 192 kHz, overtaking the
// shimmer stage only at those large geometries.
// EACH SHIMMER TAP PAYS FOR ALL FOUR INTERNAL SHIFTERS, irrespective of
// PrepareConfig::shimmerMode: PitchShiftProcessor::prepare unconditionally
// prepares simple, granular, phase-vocoder AND pitch-sync
// (pitch_shift_processor.h:1213-1216), and the phase vocoder alone allocates its
// fixed 4096-point STFT set (pitch_shift_processor.cpp:295-340), >= 0.36 MiB per
// tap. Selecting PitchMode::Granular does not avoid it. This is why the sub-44.1
// kHz force-disable in (6) saves real MEMORY, not merely CPU.
//
// ------------------------------------------------------------------------------
// (10) CADENCE CONTRACT
// ------------------------------------------------------------------------------
//   - Every OnePoleSmoother is read once per kControlChunkSamples (64) control
//     chunk and advanced with advanceSamples(kControlChunkSamples)
//     (primitives/smoother.h:243).
//   - spectralSm_ is the one exception: it is advanced with
//     advanceSamples(hopSize_) immediately before each STFT frame's read
//     (FR-064). Advancing it once per frame with process() would stretch its
//     100 ms constant to ~25 s at the default hop.
//   - The two LinearRamps (freezeRamp_, outputGate_) are advanced and read PER
//     SAMPLE with LinearRamp::process() (primitives/smoother.h:370-389).
//     LinearRamp has NO advanceSamples - that method exists only on
//     OnePoleSmoother - and a per-chunk crossfade coefficient is a staircase
//     (~0.6 dB steps at 20 ms / 48 kHz), which SC-015's zero-click requirement
//     forbids. Ramp TARGETS are still set on the control grid.
//
// ------------------------------------------------------------------------------
// (11) silence() AND emergencyClear() AMORTIZE THEIR STATE CLEAR
// ------------------------------------------------------------------------------
// The clear is spread across the fade window, one clearQuotaFloats_ slab of
// delayBuffer_ plus at most one deferred sub-object reset per control chunk.
// Reason: the full clear is 1-5 MiB of memset, while PrepareConfig admits
// maxBlockSamples = 64 - a 1.33 ms deadline at 48 kHz and 0.33 ms at 192 kHz. A
// single-chunk clear can exceed a whole callback budget, and CPU budgets are
// functional requirements here (SC-008).
//
// ------------------------------------------------------------------------------
// (11b) THE CLEAR RUNS AT GATE 0, NOT ACROSS THE FADE. MEASURED DEFECT, FIXED.
// ------------------------------------------------------------------------------
// plan S5.3 has silence() set clearPending_ (and clear the scalar loop state,
// the bloom bank and the freeze latch) IMMEDIATELY, so phases 1 and 2 overlapped
// and audible state was being zeroed while outputGate_ was still near 1. That is
// not a theoretical concern - it was MEASURED as a click:
//
//   SC-015's 120 s transition render calls silence() at t = 105.000 s. With the
//   overlapped form, ClickDetector reported detections at t = 105.0123, 105.0168,
//   105.0259 and 105.0304 s on BOTH channels. 105.0123 is clear stage 9 (the wet
//   FIFO + the OverlapAdd warm-up counter) landing 12.3 ms into the 20 ms fade,
//   i.e. TRUNCATING the spectral stage's in-flight OLA drain while the gate was
//   still at 0.385. The drain IS the wet fade-out at diffusionFftSize = 1024
//   (21.3 ms of pipeline latency), so cutting it mid-flight is a step.
//
//   Isolated-wet trace, 1 ms peaks from the call, spectral stage ON (the shipping
//   default): 0.0812 0.0415 0.0546 0.0467 0.0407 0.0542 0.0544 0.0318 then 0.0000
//   from 8 ms on - a cliff, not a fade.
//
// The three phases are therefore STRICTLY SEQUENTIAL now: silence() only arms
// (clearArmed_) and starts the fade; runGateMachine() converts the arm into
// clearPending_ on the control step at which outputGate_ reaches EXACTLY 0; the
// fade-in waits for clearPending_ to finish. Every state change the clear makes
// is multiplied by a gate of 0, so it cannot be audible - a structural argument,
// not a measured one. emergencyClear() takes the same path with the gate SNAPPED
// to 0 first (a non-finite value cannot be faded), which additionally stops the
// fade-in from running concurrently with the clear, as it previously did.
//
// KNOWN, MEASURED CONSEQUENCE FOR SC-015 CLAUSE S(a) - the criterion and this
// requirement are mutually unsatisfiable, and this is the honest side of the
// trade. Clause S(a) demands the isolated wet RMS over the 40 ms window STARTING
// AT THE CALL be below -80 dBFS. Over 1 920 samples that caps even a single
// sample at 4.4e-3 (-47 dBFS) against a pre-call wet peak of ~0.08, so it can
// only be met by a HARD, single-sample wet cut at an open gate - which is exactly
// the discontinuity the 0-detection requirement forbids. With the click-free
// ordering the isolated wet over that window is gate(t) x wet(t) for 20 ms and
// then 0, i.e. wet_rms x sqrt((20/3)/40) = wet_rms - 7.8 dB ~= -32 dBFS.
// Measured on the OLD, overlapped code the clause did not hold either: -37.6 dBFS
// with the spectral stage on (the OLA cannot deliver silence for 21.3 ms however
// hard the wet bus is cut) and -51.0 dBFS with it off (the 20 ms fade-IN lands
// inside the 40 ms window). See the test's clause S for the restated, measurable
// form and the loud note that goes with it.
//
// ------------------------------------------------------------------------------
// (11c) THE OUTPUT GATE IS SMOOTHSTEP-SHAPED, NOT LINEAR
// ------------------------------------------------------------------------------
// outputGate_ is a LinearRamp, so its value is C0 but not C1: there is a slope
// corner where it leaves 1 and another where it arrives at 0 (and the mirror
// pair on the way back). A corner in an envelope is a STEP in the first
// derivative of whatever it multiplies, and ClickDetector's statistic IS that
// derivative, normalised by the sigma of a 512-sample frame - which collapses
// in the near-silent frames on either side of gate 0. So an inaudible corner
// still scores. renderSlice therefore reads the ramp and shapes it with
// smoothstep g^2(3-2g), whose derivative is 0 at both endpoints. Two multiplies
// and an add per sample; no transcendental on the audio path; the state machine
// still tests the UNSHAPED ramp, so isComplete() and the phase ordering of
// (11b) are unchanged.
//
// ------------------------------------------------------------------------------
// (12) FREEZE IS INERT IN EVERY DIMENSION EXCEPT THE MATRIX MORPH (RA-5, FR-033)
// ------------------------------------------------------------------------------
// setFreeze(true) is a 50 ms (kFreezeLatchMs) PER-SAMPLE crossfade on
// freezeRamp_, and all six FR-033 steps ride that one ramp: the modulation
// excursion goes to 0, the delay reads latch to INTEGER offsets, the damping
// one-pole and the DC blocker are crossfaded out, the input injection goes to 0,
// ALL THREE SENDS go to 0, and the per-line Jot gains go to exactly 1. FR-036's
// denormal tickle is switched off while the ramp is at 1, where it would be an
// energy source. FR-034 additionally latches the geometry: setSize,
// setDecaySeconds and setDamping are accepted and stored but NOT applied.
//
// So the roadmap's "shimmer bloom inside the feedback loop" is UNAVAILABLE in
// precisely the state a player reaches for it, and a size change under freeze
// does nothing until the freeze is released. Neither is a preference: a
// pitch-shifted return or a resonant emphasis bank inside a unity-gain loop is
// an energy SOURCE, and a moving fractional delay read is a time-varying lowpass
// (C-4) - either one left live makes SC-002's +/-0.5 dB / 60 s conservation
// criterion unachievable and freeze unbounded. Motion during freeze comes from
// the matrix morph alone, which is exactly orthogonal and therefore exactly
// lossless. Phase 7's Dream macro and Phase 12's presets must not assume
// otherwise; a shimmer-in-freeze CHARACTER belongs to Phase 10's parallel
// spectral freeze, not to this setter.
//
// ------------------------------------------------------------------------------
// (13) SPECTRAL DIFFUSION IS A PREPARE-TIME STAGE, AND IT OWNS THE ENGINE'S ONLY
//      REPORTED LATENCY (FR-060 - FR-065, FR-084, RA-2)
// ------------------------------------------------------------------------------
// One stereo STFT -> phase-smear -> OverlapAdd stage on the WET path, at
// fftSize = diffusionFftSize_ and hop = fftSize/4 (75 % overlap) with
// applySynthesisWindow = true. THAT COMBINATION IS MANDATORY: OverlapAdd's own
// header requires the synthesis window for spectral-modification processors at
// >= 75 % overlap and FORBIDS it at 50 %, where Hann^2 is not COLA
// (primitives/stft.h:225-228). The stage is switched at PREPARE and has NO
// RUNTIME TOGGLE - a latency that changes mid-render is a click plus a host
// renegotiation.
//
// (a) SMEAR, REDRAWN EVERY HOP. Per bin, per channel:
//         phase += amount * rng.nextFloat() * kPi
//     with independent smearRngL_/R_ (kSmearSaltL / kSmearSaltR), and MAGNITUDES
//     ARE NEVER TOUCHED (FR-061). The redraw is normative, not incidental: a
//     draw taken once and HELD is a static dispersive allpass - a fixed
//     colouration with no smearing over time - while a redraw decorrelates
//     successive frames, which is the "underwater chamber" the roadmap asks for.
//     The two are audibly and measurably different.
//
// (b) COHERENCE MAKE-UP g(a). Independently randomised per-frame phases sum
//     INCOHERENTLY, so OverlapAdd's COLA factor (computed once at prepare,
//     :243-262, applied unconditionally at :299-307) is wrong by a level that
//     grows with the amount - about 6 dB at amount 1. The five measured knots
//         a        0      0.25    0.5     0.75    1.0
//         outRMS   1.0000 0.9260  0.7443  0.5635  0.5001
//         g(a)     1.0000 1.0799  1.3435  1.7746  1.9996
//     ship as kCoherenceMakeup and are interpolated with the existing Layer 0
//     Interpolation::cubicHermiteInterpolate (core/interpolation.h:84), end
//     tangents clamped. Catmull-Rom tangents on this data are 0.1718 / 0.3474 /
//     0.3281, each inside the Fritsch-Carlson bound, so the interpolant is
//     monotone. g(a) IS A SCALAR ON THE PULLED TIME-DOMAIN SAMPLES, never on the
//     bins, so FR-061 holds literally. The table is expected to transfer
//     unchanged to fftSize 256 and 4096: the incoherent/coherent amplitude ratio
//     sqrt(sum w^4) / sum w^2 depends on the window and the overlap COUNT, not on
//     fftSize.
//
// (c) THE FIFO WARM-UP RULE IS WHAT CREATES THE LATENCY, AND IT IS A COUNTER,
//     NOT AN EMPTINESS TEST. STFT::canAnalyze() requires
//     samplesAvailable_ >= fftSize_ (:134-138), so no frame exists until fftSize
//     samples have been pushed, while the consumer demands one output per input
//     from the very first slice. The stage therefore emits literal 0.0f for the
//     first spectralWarmupRemaining_ == diffusionFftSize_ output samples and only
//     then starts draining wetFifoL_/R_.
//     DRAINING "AS SOON AS SOMETHING IS AVAILABLE" IS A DEFECT, and a subtle one:
//     the pump produces its first hop inside the SAME slice whose push crosses
//     fftSize, so the resulting offset is (ceil(fftSize/slice) - 1) * slice -
//     960 samples at slice 64, 1020 at slice 30, 1022 at slice 7 with
//     fftSize = 1024. Never fftSize, and never the same twice. That would make
//     getLatencySamples() wrong in every host, break SC-011's partition
//     invariance and put SC-018 clause 5 out of reach. The counter makes the
//     offset EXACTLY fftSize for every partition, which is what the dry path
//     aligns to and what SC-018 clause 5 asserts directly.
//
// (d) ONE LATENCY, BOTH PATHS. getLatencySamples() returns
//     spectralEnabled_ ? diffusionFftSize_ : 0 and is constant for a prepared
//     configuration. The dry runs through dryAlignL_/R_ at exactly fftSize
//     samples so the engine reports ONE number. THE ALIGNMENT DELAY LINE IS
//     PREPARED WITH kInterpMarginSamples OF HEADROOM, and that is load-bearing
//     rather than defensive: DelayLine::prepare stores
//     maxDelaySamples_ = (size_t)(sampleRate * (double)maxDelaySeconds)
//     (primitives/delay_line.h:267-269) and read() CLAMPS to it (:293), while
//     float(fftSize/sampleRate) rounds DOWN at every supported rate and FFT size
//     (checked: 1024/48000 as a float times 48000 is 1023.99998 -> 1023). Asking
//     for exactly fftSize seconds' worth would therefore silently align the dry
//     path one sample short of the wet path at every configuration.
//     The shimmer taps' latency is NOT included: they live inside the feedback
//     loop, and a recirculating path has no dry counterpart to align against.
//
// (e) WARM-UP. The first fftSize - hop samples the OverlapAdd produces are
//     under-summed - only frames 0..k exist - so the very start of the stage's
//     output is quieter than steady state. The engine starts silent, so this is
//     inaudible and is deliberately NOT special-cased.
//
// ------------------------------------------------------------------------------
// (14) DENORMAL HYGIENE: THE CALLER IS EXPECTED TO HAVE FTZ/DAZ ENABLED (FR-036)
// ------------------------------------------------------------------------------
// AetherReverb DOES NOT set the x86 MXCSR flush-to-zero / denormals-are-zero
// bits itself, and that is a contract, not an omission. MXCSR is THREAD-WIDE
// state owned by the host: a Layer 4 component that flipped it inside
// processStereoBlock() would either leak the change into everything the audio
// thread runs afterwards or pay a save/restore per block (the reasoning is
// core/scoped_denormal_mode.h:8-21, which is also the repo mechanism).
//
//   CALLER CONTRACT: hold a Krate::DSP::ScopedDenormalMode
//   (core/scoped_denormal_mode.h) for the duration of the audio callback, i.e.
//   the same discipline every Krate plugin processor already uses. The DSP test
//   executables enable it process-wide before any case runs
//   (dsp/tests/dsp_test_main.cpp:13, enableFTZDAZ()), so every figure this
//   header quotes - and every SC-008 CPU number - is measured WITH FTZ/DAZ ON.
//
// A caller that leaves the mode off still gets correct, finite, bounded output:
// nothing here depends on flushing for CORRECTNESS. What it loses is the CPU
// budget. Every long exponential this engine runs is a denormal source - the
// N delay lines under a 60 s T60, the damping and DC-blocker one-poles, the
// spectral OverlapAdd tail, the bloom bank's release envelopes - and a denormal
// operation costs hundreds of cycles on x86.
//
// FR-036's tickle is the ONE thing the engine does itself, and it is BELT AND
// BRACES FOR A CALLER THAT IGNORES THE CONTRACT, not a substitute for it: at
// 1e-20f kDenormalTickle is a NORMAL float (smallest normal ~1.18e-38), so it
// holds the state out of denormal range with or WITHOUT FTZ (renderSlice step
// 9). Its sign flips per channel AND per sample so it never sums to a DC
// offset, and it is OUTSIDE the per-line gain so a small g_i cannot scale it
// away. It is gated to freezeRamp < 1 (tickleScale): inside the unity-gain
// frozen loop a permanent excitation is an ENERGY SOURCE, and SC-002's
// +/-0.5 dB over 60 s does not survive one.
//
// ------------------------------------------------------------------------------
// Real-time safety: prepare() is the ONLY non-RT-safe method. Everything else is
// noexcept, allocation-free, lock-free and IO-free.
// ==============================================================================

#pragma once

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4324)  // structure padded due to alignas (intentional for SIMD)
#endif

#include <krate/dsp/core/audio_constants.h>                // L0  kDenormalGuard
#include <krate/dsp/core/db_utils.h>                       // L0  detail::isNaN / isInf
#include <krate/dsp/core/interpolation.h>                  // L0  cubicHermiteInterpolate
#include <krate/dsp/core/math_constants.h>                 // L0  kPi, kTwoPi, kHalfPi
#include <krate/dsp/core/random.h>                         // L0  Xorshift32, deriveStreamSeed
#include <krate/dsp/primitives/delay_line.h>               // L1  DelayLine, nextPowerOf2
#include <krate/dsp/primitives/smoother.h>                 // L1  OnePoleSmoother, LinearRamp
#include <krate/dsp/primitives/spectral_buffer.h>          // L1  SpectralBuffer
#include <krate/dsp/primitives/stft.h>                     // L1  STFT, OverlapAdd, WindowType
#include <krate/dsp/processors/breathing_modulator.h>      // L2  BreathingModulator
#include <krate/dsp/processors/brownian_drift.h>           // L2  BrownianDrift
#include <krate/dsp/processors/diffusion_network.h>        // L2  DiffusionNetwork
#include <krate/dsp/processors/pitch_shift_processor.h>    // L2  PitchMode, PitchShiftProcessor
#include <krate/dsp/processors/tidal_modulator.h>          // L2  TidalModulator
#include <krate/dsp/systems/sympathetic_resonance_simd.h>  // L3  free-function kernel ONLY

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <utility>
#include <vector>

namespace Krate {
namespace DSP {

namespace detail {

/// @brief Compile-time pairwise-coprimality fold over a reference-delay table.
///
/// FR-011 requires the FDN's reference lengths to be pairwise coprime so the
/// network's modes do not collapse onto shared periods. Running the check as a
/// static_assert means a future table edit cannot silently break the invariant;
/// AetherReverb_GeometryAndModalDensity is its runtime companion.
template <std::size_t N>
[[nodiscard]] constexpr bool aetherTablePairwiseCoprime(const std::size_t (&table)[N]) noexcept {
    for (std::size_t i = 0; i < N; ++i) {
        for (std::size_t j = i + 1; j < N; ++j) {
            if (std::gcd(table[i], table[j]) != std::size_t{1}) {
                return false;
            }
        }
    }
    return true;
}

/// @brief Compile-time strict-ascent check over a reference-delay table.
///
/// FR-011 stores the lengths in ascending order, which makes channel index order
/// identical to length order - relied on by FR-050's "four longest lines" tap
/// subset and by FR-018's even/odd output split.
template <std::size_t N>
[[nodiscard]] constexpr bool aetherTableStrictlyAscending(const std::size_t (&table)[N]) noexcept {
    for (std::size_t i = 1; i < N; ++i) {
        if (table[i] <= table[i - 1]) {
            return false;
        }
    }
    return true;
}

// -----------------------------------------------------------------------------
// Prepare-time linear algebra for the Dimensionality morph (FR-020 - FR-022,
// plan S7.4 / S7.5).
//
// ALL OF IT IS DOUBLE, DELIBERATELY. FR-022 pins ||M(t)^T M(t) - I||_F <= 1e-5
// over the whole path, and the runtime evaluation is two chained N x N products.
// At N = 16 a float accumulation of those products lands within a factor of ~5
// of the bound with nothing left for the reduction's own error, and SC-004
// clause 6 asks the reduction for 1e-6 on outputs that are already rounded to
// float. Everything here runs once per prepare() or once per 64-sample control
// chunk - never per sample - so the cost is irrelevant.
//
// Every function is allocation-free (fixed stack arrays sized for the largest
// admissible order) and bounded-iteration.
// -----------------------------------------------------------------------------

/// Largest FDN order the Aether matrix maths is sized for. Mirrors
/// AetherReverb::kMaxChannels; a static_assert inside the class pins them
/// together so the two cannot drift apart.
inline constexpr std::size_t kAetherMaxMatrixOrder = 16;

/// Determinant by LU with partial pivoting. Row-major, n x n, n <= 16.
[[nodiscard]] inline double aetherDeterminant(const double* m, std::size_t n) noexcept {
    double work[kAetherMaxMatrixOrder * kAetherMaxMatrixOrder]{};
    for (std::size_t i = 0; i < (n * n); ++i) {
        work[i] = m[i];
    }
    double det = 1.0;
    for (std::size_t c = 0; c < n; ++c) {
        std::size_t pivot = c;
        for (std::size_t r = c + 1u; r < n; ++r) {
            if (std::abs(work[(r * n) + c]) > std::abs(work[(pivot * n) + c])) {
                pivot = r;
            }
        }
        if (std::abs(work[(pivot * n) + c]) < 1e-300) {
            return 0.0;
        }
        if (pivot != c) {
            for (std::size_t j = 0; j < n; ++j) {
                std::swap(work[(pivot * n) + j], work[(c * n) + j]);
            }
            det = -det;
        }
        det *= work[(c * n) + c];
        for (std::size_t r = c + 1u; r < n; ++r) {
            const double f = work[(r * n) + c] / work[(c * n) + c];
            for (std::size_t j = c; j < n; ++j) {
                work[(r * n) + j] -= f * work[(c * n) + j];
            }
        }
    }
    return det;
}

/// ||M^T M - I||_F over a double, row-major n x n matrix.
[[nodiscard]] inline double aetherOrthoErrorDouble(const double* m, std::size_t n) noexcept {
    double acc = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t j = 0; j < n; ++j) {
            double g = 0.0;
            for (std::size_t k = 0; k < n; ++k) {
                g += m[(k * n) + i] * m[(k * n) + j];
            }
            const double d = g - ((i == j) ? 1.0 : 0.0);
            acc += d * d;
        }
    }
    return std::sqrt(acc);
}

/// ||M^T M - I||_F over a float, row-major n x n matrix, accumulated in double.
/// This is the quantity FR-027's cached accessor reports.
[[nodiscard]] inline float aetherOrthoErrorFloat(const float* m, std::size_t n) noexcept {
    double acc = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t j = 0; j < n; ++j) {
            double g = 0.0;
            for (std::size_t k = 0; k < n; ++k) {
                g += static_cast<double>(m[(k * n) + i]) * static_cast<double>(m[(k * n) + j]);
            }
            const double d = g - ((i == j) ? 1.0 : 0.0);
            acc += d * d;
        }
    }
    return static_cast<float>(std::sqrt(acc));
}

/// In-place radix-2 fast Walsh-Hadamard transform in natural (Sylvester) order,
/// followed by the 1/sqrt(n) normalisation.
///
/// This is the order-generic form of FDNReverb::applyHadamard
/// (effects/fdn_reverb.h:696-729), which hard-codes the three stride-4/2/1
/// stages for N = 8. Re-derived rather than included (C-1).
inline void aetherFastWalshHadamard(double* x, std::size_t n) noexcept {
    for (std::size_t stride = n / 2u; stride > 0u; stride /= 2u) {
        for (std::size_t base = 0; base < n; base += (stride * 2u)) {
            for (std::size_t i = 0; i < stride; ++i) {
                const double a = x[base + i];
                const double b = x[base + i + stride];
                x[base + i] = a + b;
                x[base + i + stride] = a - b;
            }
        }
    }
    const double norm = 1.0 / std::sqrt(static_cast<double>(n));
    for (std::size_t i = 0; i < n; ++i) {
        x[i] *= norm;
    }
}

/// M0 = I - (2/n) * J, the t = 0 "2D plate" endpoint (FR-020).
/// DENSE: diagonal 1 - 2/n, every off-diagonal -2/n. A single reflection about
/// the hyperplane orthogonal to 1/sqrt(n), hence det = -1.
inline void aetherBuildHouseholder(double* m, std::size_t n) noexcept {
    const double c = 2.0 / static_cast<double>(n);
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t j = 0; j < n; ++j) {
            m[(i * n) + j] = ((i == j) ? 1.0 : 0.0) - c;
        }
    }
}

/// M1 = D * H_n/sqrt(n) with D = diag(-1, 1, ..., 1), the t = 0.5 "3D hall"
/// endpoint (FR-020, C-8). ROW 0 IS NEGATED: row negation is left-multiplication
/// by an orthogonal +/-1 diagonal, so M1 stays exactly orthogonal while det
/// flips from +1 to -1, putting it in the same component of O(n) as M0.
inline void aetherBuildHadamard(double* m, std::size_t n) noexcept {
    double column[kAetherMaxMatrixOrder]{};
    for (std::size_t j = 0; j < n; ++j) {
        for (std::size_t i = 0; i < n; ++i) {
            column[i] = (i == j) ? 1.0 : 0.0;
        }
        aetherFastWalshHadamard(column, n);
        for (std::size_t i = 0; i < n; ++i) {
            m[(i * n) + j] = column[i];
        }
    }
    for (std::size_t j = 0; j < n; ++j) {
        m[j] = -m[j];
    }
}

/// M2, the t = 1 "N-D impossible" endpoint (FR-021): modified Gram-Schmidt over
/// an n x n matrix of Xorshift32::nextFloat() draws (core/random.h:59), with one
/// column negated whenever det(Q) > 0 so the result lands in the det = -1
/// component. Redraws (bounded at 8 attempts) if any pivot norm is below 1e-4.
inline void aetherBuildRandomOrthogonal(double* m, std::size_t n,
                                        std::uint32_t streamSeed) noexcept {
    constexpr std::size_t kEntries = kAetherMaxMatrixOrder * kAetherMaxMatrixOrder;
    constexpr double kMinPivotNorm = 1e-4;
    constexpr int kMaxAttempts = 8;

    Xorshift32 rng(streamSeed);
    double raw[kEntries]{};
    double q[kEntries]{};
    double column[kAetherMaxMatrixOrder]{};

    for (int attempt = 0; attempt < kMaxAttempts; ++attempt) {
        for (std::size_t i = 0; i < (n * n); ++i) {
            raw[i] = static_cast<double>(rng.nextFloat());
        }
        bool ok = true;
        for (std::size_t j = 0; j < n; ++j) {
            for (std::size_t i = 0; i < n; ++i) {
                column[i] = raw[(i * n) + j];
            }
            for (std::size_t k = 0; k < j; ++k) {
                double dot = 0.0;
                for (std::size_t i = 0; i < n; ++i) {
                    dot += q[(i * n) + k] * column[i];
                }
                for (std::size_t i = 0; i < n; ++i) {
                    column[i] -= dot * q[(i * n) + k];
                }
            }
            double norm = 0.0;
            for (std::size_t i = 0; i < n; ++i) {
                norm += column[i] * column[i];
            }
            norm = std::sqrt(norm);
            if (norm < kMinPivotNorm) {
                ok = false;
                break;
            }
            for (std::size_t i = 0; i < n; ++i) {
                q[(i * n) + j] = column[i] / norm;
            }
        }
        if (!ok) {
            continue;
        }
        if (aetherDeterminant(q, n) > 0.0) {
            for (std::size_t i = 0; i < n; ++i) {
                q[i * n] = -q[i * n];
            }
        }
        for (std::size_t i = 0; i < (n * n); ++i) {
            m[i] = q[i];
        }
        return;
    }

    // Unreachable for any real draw (a uniform n x n matrix is rank-deficient
    // with probability 0). Deterministic det = -1 fallback so the caller still
    // gets a legal endpoint rather than an uninitialised one.
    aetherBuildHadamard(m, n);
}

/// Real-Schur reduction of R in SO(n): R == V * B(theta) * V^T with V orthogonal
/// and B block-diagonal in canonical 2x2 rotations [[cos, -sin], [sin, cos]].
///
/// Plan S7.5's five steps. It exploits the fact that an orthogonal R is NORMAL,
/// so its symmetric part S = (R + R^T)/2 shares its invariant subspaces and a
/// symmetric eigensolver is enough - there is no LAPACK in this repo.
///
/// @param r  Input n x n, row-major, double.
/// @param n  Even, 2 <= n <= kAetherMaxMatrixOrder.
/// @param v  Out: n x n orthogonal V, row-major.
/// @param thetas Out: n/2 angles, one per block.
/// @return false if R is not numerically in SO(n), or if the reduction cannot
///         emit exactly n/2 blocks (an odd eigenvalue cluster).
[[nodiscard]] inline bool aetherSchurReduceSO(const double* r, std::size_t n, double* v,
                                              double* thetas) noexcept {
    constexpr std::size_t kOrder = kAetherMaxMatrixOrder;
    constexpr std::size_t kEntries = kOrder * kOrder;
    // Admissibility of the INPUT. The caller may hand us a matrix that was
    // rounded to float, whose orthogonality error is ~5e-7 at n = 16, so the
    // gate is deliberately far looser than the 1e-6 SC-004 asks of the OUTPUT.
    constexpr double kInputOrthogonalityTolerance = 1e-4;
    constexpr double kJacobiOffDiagTolerance = 1e-9;
    constexpr int kJacobiMaxSweeps = 30;
    // Eigenvalues this close are one cluster (a repeated rotation angle).
    constexpr double kClusterTolerance = 1e-6;
    // Below this, K = (C - C^T)/2 carries no usable plane direction (R restricted
    // to the cluster is exactly +/-I) and the plane partner is taken from the
    // remaining basis instead. It guards a division, nothing more - see the
    // step 4/5 comment for why there is no epsilon branch on lambda itself.
    constexpr double kSkewMinNorm = 1e-9;
    constexpr double kDeflationMinNorm = 1e-6;

    if ((r == nullptr) || (v == nullptr) || (thetas == nullptr)) {
        return false;
    }
    if ((n < 2u) || (n > kOrder) || ((n % 2u) != 0u)) {
        return false;
    }
    if (aetherOrthoErrorDouble(r, n) > kInputOrthogonalityTolerance) {
        return false;
    }
    if (aetherDeterminant(r, n) < 0.5) {  // orthogonal => det is +/-1; we need +1
        return false;
    }

    // --- step 1: the symmetric part -----------------------------------------
    double s[kEntries]{};
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t j = 0; j < n; ++j) {
            s[(i * n) + j] = 0.5 * (r[(i * n) + j] + r[(j * n) + i]);
        }
    }

    // --- step 2: cyclic Jacobi eigendecomposition S = Q L Q^T ---------------
    double q[kEntries]{};
    for (std::size_t i = 0; i < n; ++i) {
        q[(i * n) + i] = 1.0;
    }
    for (int sweep = 0; sweep < kJacobiMaxSweeps; ++sweep) {
        double offDiag = 0.0;
        for (std::size_t i = 0; i < n; ++i) {
            for (std::size_t j = i + 1u; j < n; ++j) {
                offDiag += s[(i * n) + j] * s[(i * n) + j];
            }
        }
        if (std::sqrt(2.0 * offDiag) < kJacobiOffDiagTolerance) {
            break;
        }
        for (std::size_t p = 0; (p + 1u) < n; ++p) {
            for (std::size_t qq = p + 1u; qq < n; ++qq) {
                const double apq = s[(p * n) + qq];
                if (std::abs(apq) < 1e-300) {
                    continue;
                }
                const double theta = (s[(qq * n) + qq] - s[(p * n) + p]) / (2.0 * apq);
                const double t = ((theta >= 0.0) ? 1.0 : -1.0) /
                                 (std::abs(theta) + std::sqrt((theta * theta) + 1.0));
                const double cs = 1.0 / std::sqrt((t * t) + 1.0);
                const double sn = t * cs;
                for (std::size_t k = 0; k < n; ++k) {  // S <- S * J
                    const double kp = s[(k * n) + p];
                    const double kq = s[(k * n) + qq];
                    s[(k * n) + p] = (cs * kp) - (sn * kq);
                    s[(k * n) + qq] = (sn * kp) + (cs * kq);
                }
                for (std::size_t k = 0; k < n; ++k) {  // S <- J^T * S
                    const double pk = s[(p * n) + k];
                    const double qk = s[(qq * n) + k];
                    s[(p * n) + k] = (cs * pk) - (sn * qk);
                    s[(qq * n) + k] = (sn * pk) + (cs * qk);
                }
                for (std::size_t k = 0; k < n; ++k) {  // Q <- Q * J
                    const double kp = q[(k * n) + p];
                    const double kq = q[(k * n) + qq];
                    q[(k * n) + p] = (cs * kp) - (sn * kq);
                    q[(k * n) + qq] = (sn * kp) + (cs * kq);
                }
            }
        }
    }

    double lambda[kOrder]{};
    for (std::size_t i = 0; i < n; ++i) {
        lambda[i] = s[(i * n) + i];
    }

    // --- step 3: sort descending, permuting Q's columns ---------------------
    for (std::size_t a = 0; (a + 1u) < n; ++a) {
        std::size_t best = a;
        for (std::size_t b = a + 1u; b < n; ++b) {
            if (lambda[b] > lambda[best]) {
                best = b;
            }
        }
        if (best != a) {
            std::swap(lambda[a], lambda[best]);
            for (std::size_t i = 0; i < n; ++i) {
                std::swap(q[(i * n) + a], q[(i * n) + best]);
            }
        }
    }

    // --- steps 4 + 5: cluster, emit 2x2 blocks, collect V's columns ---------
    //
    // Inside one eigenvalue cluster of S the subspace is R-invariant and R's
    // restriction C is a rotation. The plane partner of a unit vector u1 comes
    // from the ANTISYMMETRIC part K = (C - C^T)/2: K is skew, so u2 = K*u1 is
    // orthogonal to u1 for free, and for an exact rotation K = sin(theta)*J with
    // J^2 = -I, which makes u2 the canonical partner.
    //
    // theta is then MEASURED in that plane - cos = u1^T C u1, sin = u2^T C u1 -
    // and deliberately NOT derived from the eigenvalue as
    // atan2(sqrt(1 - lambda^2), lambda). THAT DISTINCTION IS LOAD-BEARING, and
    // getting it wrong is not a rounding nuisance but a wrong answer: callers
    // hand this function a float-rounded R, so an eigenvalue that should be
    // exactly +/-1 arrives ~1e-7 off, and sqrt(1 - lambda^2) turns that 1e-7
    // into a spurious 4.5e-4 rotation - three orders of magnitude past SC-004
    // clause 6(c)'s 1e-6, and with a J = (C - lambda I)/s whose divisor is pure
    // noise, so the emitted basis is not even orthogonal. Measuring has no such
    // cliff and needs no epsilon branch on lambda at all: theta = 0 and
    // theta = pi fall out of the same code path.
    //
    // u2 is flipped when the measured sine is negative, which is a legal change
    // of basis (it negates theta) and keeps every emitted angle in [0, pi].
    double cluster[kEntries]{};    // C = Qc^T R Qc, size x size
    double skew[kEntries]{};       // K = (C - C^T)/2
    double remaining[kEntries]{};  // orthonormal basis of the unused subspace
    double refreshed[kEntries]{};  // its post-deflation replacement
    double u1[kOrder]{};
    double u2[kOrder]{};
    double cu1[kOrder]{};

    std::size_t emitted = 0;
    std::size_t blockIndex = 0;
    std::size_t clusterStart = 0;
    while (clusterStart < n) {
        std::size_t clusterEnd = clusterStart + 1u;
        while ((clusterEnd < n) &&
               (std::abs(lambda[clusterEnd] - lambda[clusterStart]) <= kClusterTolerance)) {
            ++clusterEnd;
        }
        const std::size_t size = clusterEnd - clusterStart;
        if ((size % 2u) != 0u) {
            return false;  // det(R) = +1 with n even forces every cluster to be even
        }
        const std::size_t pairs = size / 2u;

        // C = Qc^T R Qc, the restriction of R to the cluster's eigenspace.
        for (std::size_t a = 0; a < size; ++a) {
            for (std::size_t b = 0; b < size; ++b) {
                double acc = 0.0;
                for (std::size_t i = 0; i < n; ++i) {
                    double rowDot = 0.0;
                    for (std::size_t j = 0; j < n; ++j) {
                        rowDot += r[(i * n) + j] * q[(j * n) + clusterStart + b];
                    }
                    acc += q[(i * n) + clusterStart + a] * rowDot;
                }
                cluster[(a * size) + b] = acc;
            }
        }
        for (std::size_t a = 0; a < size; ++a) {
            for (std::size_t b = 0; b < size; ++b) {
                skew[(a * size) + b] = 0.5 * (cluster[(a * size) + b] - cluster[(b * size) + a]);
            }
        }

        // Deflate in cluster coordinates.
        std::size_t count = size;
        for (std::size_t a = 0; a < size; ++a) {
            for (std::size_t b = 0; b < size; ++b) {
                remaining[(a * size) + b] = (a == b) ? 1.0 : 0.0;
            }
        }
        for (std::size_t pair = 0; pair < pairs; ++pair) {
            if (count < 2u) {
                return false;
            }
            for (std::size_t a = 0; a < size; ++a) {
                u1[a] = remaining[a];  // row 0 of the remaining basis
            }

            double skewNorm = 0.0;
            for (std::size_t a = 0; a < size; ++a) {
                double acc = 0.0;
                for (std::size_t b = 0; b < size; ++b) {
                    acc += skew[(a * size) + b] * u1[b];
                }
                u2[a] = acc;
                skewNorm += acc * acc;
            }
            skewNorm = std::sqrt(skewNorm);
            if (skewNorm > kSkewMinNorm) {
                for (std::size_t a = 0; a < size; ++a) {
                    u2[a] /= skewNorm;
                }
            } else {
                // R restricted to this cluster is exactly +/-I: every plane is
                // invariant, so the next unused basis vector is as good as any.
                for (std::size_t a = 0; a < size; ++a) {
                    u2[a] = remaining[size + a];
                }
            }

            // CONSTRAIN u2 TO THE STILL-UNUSED SUBSPACE, i.e. to
            // span(remaining[1 .. count-1]). Two passes, "twice is enough".
            //
            // For an exact rotation this is a no-op: K = sin(theta)*J and J maps
            // the complement of the already-emitted planes to itself, so K*u1 is
            // already in there. It is NOT a no-op when R's restriction is
            // numerically +/-I: K is then pure float noise with no structure at
            // all, and the normalised K*u1 can point straight back into a plane
            // that was already emitted - which silently makes V non-orthogonal
            // (measured 0.32 at N = 8, 1.31 at N = 16 on the theta = {0, 0, 0.7,
            // pi} case before this projection was added).
            for (int pass = 0; pass < 2; ++pass) {
                double proj[kOrder]{};
                for (std::size_t row = 1u; row < count; ++row) {
                    double d = 0.0;
                    for (std::size_t a = 0; a < size; ++a) {
                        d += remaining[(row * size) + a] * u2[a];
                    }
                    for (std::size_t a = 0; a < size; ++a) {
                        proj[a] += d * remaining[(row * size) + a];
                    }
                }
                double norm = 0.0;
                for (std::size_t a = 0; a < size; ++a) {
                    u2[a] = proj[a];
                    norm += proj[a] * proj[a];
                }
                norm = std::sqrt(norm);
                if (norm < kDeflationMinNorm) {
                    // u2 lay entirely inside the emitted subspace: take the next
                    // unused basis vector, which is unit and orthogonal to u1 by
                    // construction.
                    for (std::size_t a = 0; a < size; ++a) {
                        u2[a] = remaining[size + a];
                    }
                    break;
                }
                for (std::size_t a = 0; a < size; ++a) {
                    u2[a] /= norm;
                }
            }

            // Measure the rotation this plane actually carries.
            for (std::size_t a = 0; a < size; ++a) {
                double acc = 0.0;
                for (std::size_t b = 0; b < size; ++b) {
                    acc += cluster[(a * size) + b] * u1[b];
                }
                cu1[a] = acc;
            }
            double cosT = 0.0;
            double sinT = 0.0;
            for (std::size_t a = 0; a < size; ++a) {
                cosT += u1[a] * cu1[a];
                sinT += u2[a] * cu1[a];
            }
            if (sinT < 0.0) {
                for (std::size_t a = 0; a < size; ++a) {
                    u2[a] = -u2[a];
                }
                sinT = -sinT;
            }
            const double angle = std::atan2(sinT, cosT);

            // V's next two columns: Qc * u1 and Qc * u2. In that basis
            // C*u1 = cos(theta)*u1 + sin(theta)*u2 and
            // C*u2 = -sin(theta)*u1 + cos(theta)*u2 - the canonical orientation
            // [[cos, -sin], [sin, cos]].
            for (std::size_t i = 0; i < n; ++i) {
                double acc1 = 0.0;
                double acc2 = 0.0;
                for (std::size_t a = 0; a < size; ++a) {
                    const double qia = q[(i * n) + clusterStart + a];
                    acc1 += qia * u1[a];
                    acc2 += qia * u2[a];
                }
                v[(i * n) + emitted] = acc1;
                v[(i * n) + emitted + 1u] = acc2;
            }
            emitted += 2u;
            thetas[blockIndex] = angle;
            ++blockIndex;

            // Project u1 and u2 out of the rest, re-orthonormalise (twice), drop
            // the two vectors that collapse.
            std::size_t kept = 0;
            for (std::size_t row = 1u; row < count; ++row) {
                double w[kOrder]{};
                for (std::size_t a = 0; a < size; ++a) {
                    w[a] = remaining[(row * size) + a];
                }
                double wNorm = 0.0;
                for (int pass = 0; pass < 2; ++pass) {
                    double d1 = 0.0;
                    double d2 = 0.0;
                    for (std::size_t a = 0; a < size; ++a) {
                        d1 += u1[a] * w[a];
                        d2 += u2[a] * w[a];
                    }
                    for (std::size_t a = 0; a < size; ++a) {
                        w[a] -= (d1 * u1[a]) + (d2 * u2[a]);
                    }
                    for (std::size_t prev = 0; prev < kept; ++prev) {
                        double d = 0.0;
                        for (std::size_t a = 0; a < size; ++a) {
                            d += refreshed[(prev * size) + a] * w[a];
                        }
                        for (std::size_t a = 0; a < size; ++a) {
                            w[a] -= d * refreshed[(prev * size) + a];
                        }
                    }
                    wNorm = 0.0;
                    for (std::size_t a = 0; a < size; ++a) {
                        wNorm += w[a] * w[a];
                    }
                    wNorm = std::sqrt(wNorm);
                    if (wNorm <= kDeflationMinNorm) {
                        break;
                    }
                    for (std::size_t a = 0; a < size; ++a) {
                        w[a] /= wNorm;
                    }
                }
                if (wNorm > kDeflationMinNorm) {
                    for (std::size_t a = 0; a < size; ++a) {
                        refreshed[(kept * size) + a] = w[a];
                    }
                    ++kept;
                }
            }
            for (std::size_t row = 0; row < kept; ++row) {
                for (std::size_t a = 0; a < size; ++a) {
                    remaining[(row * size) + a] = refreshed[(row * size) + a];
                }
            }
            count = kept;
        }

        clusterStart = clusterEnd;
    }

    return (emitted == n) && (blockIndex == (n / 2u));
}

}  // namespace detail

// =============================================================================
// AetherReverb
// =============================================================================

/// @brief Morphing feedback-delay network with life modulators, in-loop shimmer,
///        a harmonic-bloom resonator bank and optional STFT tail smearing.
///
/// @note Layer 4. prepare() is the only method that allocates.
class AetherReverb {
public:
    // -------------------------------------------------------------------------
    // Public constants (plan S2.3) - tests name these instead of magic numbers.
    // -------------------------------------------------------------------------

    // --- cadence / lifecycle ---
    /// Control-rate chunk in samples. Matches continuous_body.h:97,
    /// harmonic_cloud.h:144 and atmosphere_engine.h:269.
    static constexpr std::size_t kControlChunkSamples = 64;
    static constexpr float kSilenceRampMs = 20.0f;  ///< FR-007 fade-out/in window
    static constexpr float kFreezeLatchMs = 50.0f;  ///< FR-033 freeze crossfade

    // --- geometry ---
    static constexpr double kReferenceSampleRate = 48000.0;
    static constexpr float kMinSampleRate = 8000.0f;         ///< fdn_reverb.h:13, :130
    static constexpr float kMaxSampleRate = 192000.0f;
    static constexpr double kShimmerMinSampleRate = 44100.0;  ///< pitch_shift_processor.h:141
    static constexpr float kMinFullSizeDelaySeconds = 0.45f;  ///< FR-012
    static constexpr float kSizeScaleMin = 0.25f;
    static constexpr float kSizeScaleMax = 4.0f;
    static constexpr float kModExcursionFraction = 0.005f;  ///< FR-072, per line, 0.5 %
    /// Cubic Hermite reads y[-1] .. y[+2], so every section needs 4 spare samples.
    static constexpr std::size_t kInterpMarginSamples = 4;

    // --- injection / taps ---
    static constexpr std::size_t kTapReadCount = 4;         ///< FR-050
    static constexpr float kTapReadNormalisation = 0.25f;   ///< 1 / kTapReadCount

    // --- shimmer taps (FR-050 - FR-054) --------------------------------------
    static constexpr float kShimmerOctaveSemitones = 12.0f;
    static constexpr float kShimmerFifthSemitones = 7.0f;
    /// Each tap injects into a PINNED PAIR of channels.
    static constexpr std::size_t kShimmerInjectPairSize = 2;
    /// kTapInjectionGain(subset) = sqrt(2/|subset|), which is exactly 1.0 for a
    /// pair - so the send value IS the injected gain for both shimmer legs
    /// (FR-051). Written as a literal because std::sqrt is not constexpr.
    static constexpr float kShimmerInjectionGain = 1.0f;

    /// FR-050's pinned injection subsets, rule {1, N/2} for +12 and {3, 3N/4}
    /// for +7. PUBLIC so a later criterion can name them instead of a literal.
    static constexpr std::size_t kShimmerOctaveInjectChannels8[kShimmerInjectPairSize] = {1u, 4u};
    static constexpr std::size_t kShimmerOctaveInjectChannels16[kShimmerInjectPairSize] = {1u, 8u};
    static constexpr std::size_t kShimmerFifthInjectChannels8[kShimmerInjectPairSize] = {3u, 6u};
    static constexpr std::size_t kShimmerFifthInjectChannels16[kShimmerInjectPairSize] = {3u, 12u};

    /// FR-059 return HF shelf. SHIPPED VALUES - see banner item (5c) for the
    /// MEASURED balance that moved the corner down from the plan's 6 kHz
    /// starting point. This pair is plan R-1's only admissible lever.
    static constexpr float kReturnShelfCornerHz = 120.0f;
    /// The shelf FLOOR. Shipped at 0, i.e. the shelf degenerates to the pure
    /// one-pole lowpass FR-059 permits ("one-pole, corner documented in the
    /// header"). A NON-ZERO floor is a frequency-INDEPENDENT return gain above
    /// the corner, and that is exactly what lets an upward cascade accumulate -
    /// see banner item (5c) for the measurement that forced it to 0.
    static constexpr float kReturnShelfHfGain = 0.0f;
    /// The corner is additionally clamped to this fraction of the sample rate so
    /// the one-pole stays well inside the band at every supported rate.
    static constexpr float kReturnShelfMaxCornerFraction = 0.45f;
    /// FR-055 bloom pool bound. DELIBERATELY `int`, not `std::size_t`: it is passed
    /// straight to processSympatheticBankSIMD's `int count` parameter
    /// (systems/sympathetic_resonance_simd.h:45), and a std::size_t would be an
    /// implicit narrowing in that call expression (MSVC C4267 / -Wconversion)
    /// against the zero-warning gate. It also matches the kernel's own pool
    /// constants (sympathetic_resonance.h:40, :43).
    static constexpr int kMaxBloomResonators = 32;
    /// FR-056 voice-pool bound. 8 voices x 4 partials saturates
    /// kMaxBloomResonators exactly.
    static constexpr std::size_t kMaxBloomVoices = 8;

    // --- harmonic bloom (FR-055 - FR-059, Q7, plan S7.10) --------------------
    /// FR-055's pinned injection subset: THE CHANNELS NEITHER SHIMMER PAIR USES.
    /// At N = 8 the shimmer takes {1,4} and {3,6}, leaving {0,2,5,7}; at N = 16 it
    /// takes {1,8} and {3,12}, leaving the remaining twelve. The disjointness is
    /// asserted at namespace scope below the class.
    static constexpr std::size_t kBloomInjectCount8 = 4;
    static constexpr std::size_t kBloomInjectCount16 = 12;
    static constexpr std::size_t kBloomInjectChannels8[kBloomInjectCount8] = {0u, 2u, 5u, 7u};
    static constexpr std::size_t kBloomInjectChannels16[kBloomInjectCount16] = {
        0u, 2u, 4u, 5u, 6u, 7u, 9u, 10u, 11u, 13u, 14u, 15u};
    /// kTapInjectionGain(subset) = sqrt(2/|subset|). Written as literals because
    /// std::sqrt is not constexpr; the values are asserted below the class.
    static constexpr float kBloomInjectionGain8 = 0.70710678f;   ///< sqrt(2/4)
    static constexpr float kBloomInjectionGain16 = 0.40824829f;  ///< sqrt(2/12)

    /// setBloomSend's mapping: sendGain = v * kBloomSendMax. THE ONE CONSTANT
    /// THIS PHASE TUNES AGAINST A MEASUREMENT (plan R-2) - see banner item (5e)
    /// for the loop-gain arithmetic and the predicted 10.5 dB emphasis at
    /// SC-016 clause 3's grid point. If that measurement moves, this constant,
    /// kBloomLoopGainCeiling and kBloomShelfCornerHz are the ONLY admissible
    /// levers; never the criterion (B-4).
    static constexpr float kBloomSendMax = 8.0f;
    /// FR-058's target, strictly inside the FR's "<= 1.0".
    ///
    /// The binding constraint is NOT stability, though: it is SC-016 clause 3's
    /// release half. Anything the in-loop return does is a change to the FDN's
    /// DECAY RATE at f_k, so the level advantage it builds up is stored in the
    /// network's own state and never fades - `bloomNoteOff` stops the advantage
    /// growing but cannot undo it (banner item (5f)). Measured at the clause's own
    /// configuration: ceiling 0.95 leaves a +2.14 dB residual at 660 Hz against a
    /// +/-2 dB bound. The ceiling therefore has to sit far enough inside FR-058's
    /// "<= 1.0" that the accumulated advantage stays inside that bound, and since
    /// banner item (5g)'s out-of-loop path now carries the emphasis, giving the
    /// in-loop return a large share buys nothing. 0.30 measures +0.6 dB.
    static constexpr float kBloomLoopGainCeiling = 0.30f;
    /// FR-059's corner ON THE BLOOM RETURN ONLY - deliberately NOT the shimmer's
    /// kReturnShelfCornerHz. Banner item (5e) has the full reason: the bloom does
    /// not cascade upward, so the shimmer's 120 Hz corner would only attenuate the
    /// partials the bank exists to reinforce.
    static constexpr float kBloomShelfCornerHz = 6000.0f;

    /// FR-055 EMPHASIS RETURN - the bank's second, OUT-OF-LOOP output path.
    ///
    /// The shelved, 1/sqrt(count)-normalised bank output is ALSO summed into the
    /// wet bus at `send * kBloomEmphasisGain * (1/sqrt(count))`, alongside (never
    /// instead of) FR-055's injection into kBloomInjectChannels. Banner item (5g)
    /// carries the measured derivation; the one-line reason is that the in-loop
    /// path alone cannot produce a POSITIVE emphasis at a prescribed frequency -
    /// its sign is the FDN's loop phase at f_k, not a gain - so SC-016 clause 3
    /// is unreachable through kBloomSendMax / kBloomLoopGainCeiling /
    /// kBloomShelfCornerHz, the three levers tasks.md names.
    ///
    /// The value is bracketed from BOTH sides by SC-016 clause 3 and is therefore
    /// as much a measurement as kBloomSendMax: too small and the four target
    /// bands miss +6 dB, too large and the mean non-target rise exceeds +2 dB
    /// (measured on the shipped engine: worst target / non-target mean =
    /// +4.5 / +0.3 dB at 12, +7.3 / +1.0 dB at 20, +10.1 / +1.8 dB at 30,
    /// +13.1 / +2.7 dB at 45 - the last already outside the +2 dB bound).
    static constexpr float kBloomEmphasisGain = 34.0f;

    /// FR-057: setBloomDecay maps 0..1 -> Q in [20, 400], i.e. Q = 20 * 20^v.
    static constexpr float kBloomQMin = 20.0f;
    static constexpr float kBloomQMax = 400.0f;
    /// Frequency-dependent Q: Q_eff = Q * clamp(kBloomQFreqRef / f,
    /// kBloomMinQScale, 1). Values from systems/sympathetic_resonance.h:58, :61.
    static constexpr float kBloomQFreqRef = 500.0f;
    static constexpr float kBloomMinQScale = 0.5f;
    /// FR-056's admissible partial range, applied BEFORE any coefficient maths.
    static constexpr float kBloomMinFreqHz = 20.0f;
    static constexpr float kBloomMaxFreqFraction = 0.45f;
    /// -96 dB, systems/sympathetic_resonance.h:52. A released slot is reclaimed by
    /// the CONTROL-GRID pass once its envelope falls below this.
    static constexpr float kBloomReclaimThresholdLinear = 1.585e-5f;
    /// The kernel's envelope-follower release, systems/sympathetic_resonance.h:117.
    static constexpr float kBloomReleaseSeconds = 0.010f;

    static constexpr float kOrthogonalityTolerance = 1e-5f;  ///< FR-022
    static constexpr float kMorphEpsilon = 1e-6f;            ///< matrix recompute gate

    /// FR-036's tickle magnitude. The FR says "a `kDenormalFloor`-magnitude
    /// alternating-sign tickle" (spec.md:839-842) and the repo's kDenormalFloor
    /// is 1e-20f (processors/brownian_drift.h:228, processors/entropy_processor.h:98).
    /// It is re-declared here rather than included because both definitions are
    /// PRIVATE class members of Layer 2 classes and are not reachable.
    ///
    /// NOT core/audio_constants.h's kDenormalGuard (`:40`, 1e-12f): that constant
    /// is documented there as a guard for DENOMINATORS AND COEFFICIENT MATH, and
    /// it is eight orders of magnitude too large for a signal-path tickle. At
    /// 1e-12 the tickle is a permanent, non-decaying, Nyquist-rate excitation of
    /// the loop; over SC-006's 180 s the legitimate tail falls ~180 dB past it,
    /// so the last epoch measures the tickle rather than the reverb. Measured on
    /// the shipped engine at SC-006's grid point with both shimmer sends at ZERO
    /// (i.e. with the tickle as the only difference): HF fraction of the final
    /// 20 s = 1.59e-3 at 1e-12 against 8.18e-6 at 1e-20, a 194x HF-floor
    /// inflation caused entirely by the constant.
    static constexpr float kDenormalTickle = 1e-20f;

    // --- seed salts (PUBLIC: SC-017 clause 1a reconstructs the breath
    //     trajectory by seeding its own BreathingModulator from kBreathSalt) ---
    static constexpr std::size_t kMatrixSalt = 0;
    static constexpr std::size_t kBreathSalt = 1;
    static constexpr std::size_t kTideSalt = 2;
    static constexpr std::size_t kSmearSaltL = 3;
    static constexpr std::size_t kSmearSaltR = 4;
    /// Channel j uses kDriftSaltBase + j.
    static constexpr std::size_t kDriftSaltBase = 16;

    // --- reference delay tables (FR-011, plan S7.1) --------------------------
    // Distinct primes (hence pairwise coprime), strictly ascending, roughly
    // geometric from 960 samples (20 ms at 48 kHz) with ratio 5.3^(1/(N-1)),
    // each term replaced by the nearest unused prime. They span
    // 20.15 ms .. 105.98 ms at 48 kHz - a 5.26 : 1 spread, matching
    // FDNReverb's {149 .. 797} (fdn_reverb.h:91) at ~5x the absolute length.
    // Both orders ship (Q3); N = 8 is the default. The values are reference
    // lengths at kReferenceSampleRate and are rate-scaled in prepare().
    // PUBLIC so the FR-011 runtime companion test can name them.

    static constexpr std::size_t kRefDelays8[8] = {967,  1217, 1543, 1973,
                                                   2477, 3163, 4001, 5087};

    static constexpr std::size_t kRefDelays16[16] = {967,  1087, 1201, 1361, 1511, 1693,
                                                     1879, 2099, 2347, 2621, 2927, 3271,
                                                     3659, 4079, 4561, 5087};

    // -------------------------------------------------------------------------
    // PrepareConfig (FR-009, plan S2.4)
    // -------------------------------------------------------------------------

    /// @brief Prepare-time configuration. Every field is clamped, never rejected
    ///        (the AtmosphereEngine::prepare discipline, atmosphere_engine.h:404-420).
    struct PrepareConfig {
        std::size_t numChannels = 8;         ///< admissible: 8 or 16 only
        std::size_t maxBlockSamples = 2048;  ///< clamped to [64, 8192]
        float maxDelaySeconds = 0.50f;       ///< clamped to [0.05, 1.0]
        bool shimmerEnabled = true;
        PitchMode shimmerMode = PitchMode::Granular;  ///< pitch_shift_processor.h:58-63
        bool bloomEnabled = true;
        bool spectralDiffusionEnabled = true;  ///< default ON; costs diffusionFftSize latency
        std::size_t diffusionFftSize = 1024;   ///< clamped to [256, 4096], snapped down to a power of 2
        std::uint32_t seed = 1;
    };

    // -------------------------------------------------------------------------
    // Special members
    // -------------------------------------------------------------------------

    AetherReverb() noexcept = default;
    ~AetherReverb() noexcept = default;

    // Non-copyable: STFT and OverlapAdd delete their copy operations
    // (primitives/stft.h:41-44, :210-213), as does DelayLine (:67-68) and
    // PitchShiftProcessor (:121-122).
    AetherReverb(const AetherReverb&) = delete;
    AetherReverb& operator=(const AetherReverb&) = delete;

    // Movable: every sub-object is (PitchShiftProcessor at :125-126).
    AetherReverb(AetherReverb&&) noexcept = default;
    AetherReverb& operator=(AetherReverb&&) noexcept = default;

    // -------------------------------------------------------------------------
    // Lifecycle
    // -------------------------------------------------------------------------

    /// @brief Allocate and configure. FR-003.
    /// @param sampleRate Clamped into [kMinSampleRate, kMaxSampleRate]. No 44.1 kHz floor.
    /// @param config Clamped in place; nothing is rejected.
    /// @note THE ONLY NON-REAL-TIME-SAFE METHOD. Allocates. May be called repeatedly.
    void prepare(double sampleRate, const PrepareConfig& config) noexcept {
        sampleRate_ = std::clamp(sampleRate, static_cast<double>(kMinSampleRate),
                                 static_cast<double>(kMaxSampleRate));

        numChannels_ = (config.numChannels == 16u) ? std::size_t{16} : std::size_t{8};
        maxBlockSamples_ = std::clamp(config.maxBlockSamples, std::size_t{64}, std::size_t{8192});
        maxDelaySeconds_ = std::clamp(config.maxDelaySeconds, 0.05f, 1.0f);

        const std::size_t requestedFft =
            std::clamp(config.diffusionFftSize, std::size_t{256}, std::size_t{4096});
        diffusionFftSize_ = std::clamp(std::bit_floor(requestedFft), std::size_t{256}, std::size_t{4096});
        diffusionHopSize_ = diffusionFftSize_ / 4u;  // 75 % overlap, FR-060

        spectralEnabled_ = config.spectralDiffusionEnabled;
        bloomEnabled_ = config.bloomEnabled;
        shimmerMode_ = config.shimmerMode;
        // RA-6: below 44.1 kHz the shimmer taps are not prepared at all.
        shimmerAllocated_ = config.shimmerEnabled && (sampleRate_ >= kShimmerMinSampleRate);

        seed_ = config.seed;

        // --- step 4: rate-scaled reference lengths (plan S5.1 step 4) --------
        const double rateScale = sampleRate_ / kReferenceSampleRate;
        for (std::size_t i = 0; i < kMaxChannels; ++i) {
            if (i < numChannels_) {
                const std::size_t ref =
                    (numChannels_ == 16u) ? kRefDelays16[i] : kRefDelays8[i];
                refDelaySamples_[i] = static_cast<float>(static_cast<double>(ref) * rateScale);
            } else {
                refDelaySamples_[i] = 0.0f;
            }
        }

        // --- step 5: maxSizeScale_, then the per-channel sections ------------
        // FR-012: an undersized buffer is not rejected, S's upper bound is
        // clamped and the clamped maximum is what getMaxSizeScale() reports.
        // The longest line must fit its own modulation excursion plus the four
        // spare samples cubic Hermite's y[-1] .. y[+2] window needs.
        const double maxDelaySamples = static_cast<double>(maxDelaySeconds_) * sampleRate_;
        const double excursionFactor = 1.0 + static_cast<double>(kModExcursionFraction);
        const double longestHeadroom =
            static_cast<double>(refDelaySamples_[numChannels_ - 1u]) * excursionFactor;
        double scale = static_cast<double>(kSizeScaleMax);
        if (longestHeadroom > 0.0) {
            scale = std::min(scale, (maxDelaySamples - static_cast<double>(kInterpMarginSamples)) /
                                        longestHeadroom);
        }
        // The lower bound is defensive only: maxDelaySeconds is clamped to
        // >= 0.05, whose worst case is ~0.4687, so it never binds.
        maxSizeScale_ = std::clamp(static_cast<float>(scale), 0.01f, kSizeScaleMax);

        // Section sizing uses the CLAMPED scale, so a clamped configuration
        // allocates only what it can reach. One contiguous buffer of
        // power-of-two sections, each with its own offset and mask - the
        // fdn_reverb.h:638-689 layout, re-derived (C-1).
        std::size_t totalFloats = 0;
        for (std::size_t i = 0; i < kMaxChannels; ++i) {
            if (i >= numChannels_) {
                sectionOffset_[i] = 0;
                sectionMask_[i] = 0;
                writePos_[i] = 0;
                continue;
            }
            const double peak = static_cast<double>(refDelaySamples_[i]) *
                                static_cast<double>(maxSizeScale_) * excursionFactor;
            const std::size_t needed =
                static_cast<std::size_t>(std::ceil(peak)) + kInterpMarginSamples;
            const std::size_t section = nextPowerOf2(needed);
            sectionOffset_[i] = totalFloats;
            sectionMask_[i] = section - std::size_t{1};
            writePos_[i] = 0;
            totalFloats += section;
        }
        delayBuffer_.assign(totalFloats, 0.0f);

        // --- step 6: DC-blocker pole and the N-derived injection/tap scales --
        dcBlockR_ = 1.0f - (250.0f / static_cast<float>(sampleRate_));
        // Normalising gain, see the dcBlock() doc block: it makes the blocker
        // exactly non-expansive, which FR-032 requires of every stage in the
        // unfrozen loop.
        dcBlockGain_ = 0.5f * (1.0f + dcBlockR_);
        // FR-015a: sqrt(2/N) makes the injected energy independent of N - each
        // of the N/2 channels in a parity subset gets the same signal at gain
        // g, so the subset receives (N/2)*g^2*E = E. Exactly 0.5 at N = 8.
        inputInjectionGain_ = std::sqrt(2.0f / static_cast<float>(numChannels_));
        // FR-018: the even/odd output taps are scaled by 2/N (0.25 at N = 8,
        // matching fdn_reverb.h:364-365).
        outputTapScale_ = 2.0f / static_cast<float>(numChannels_);

        // --- step 7: the stereo pre-delay pair (FR-015) ----------------------
        preDelayL_.prepare(sampleRate_, kPreDelayMaxSeconds);
        preDelayR_.prepare(sampleRate_, kPreDelayMaxSeconds);

        // --- step 8: Density (FR-040 - FR-042) -------------------------------
        // setModDepth / setModRate are deliberately LEFT at their 0 / 1 Hz
        // defaults (diffusion_network.h:226, :230): FR-042 wants no second
        // modulator in the input path, and a settled modDepth is also what lets
        // the network take its static fast path (:534, :550), which is
        // bit-identical to the per-sample path and therefore slice-length
        // independent - SC-011 depends on that.
        diffuser_.prepare(static_cast<float>(sampleRate_), maxBlockSamples_);

        // Per-chunk scratch. slice <= kControlChunkSamples always (plan S6.1),
        // so a fixed 64 is the exact size, never a bound.
        preScratchL_.assign(kControlChunkSamples, 0.0f);
        preScratchR_.assign(kControlChunkSamples, 0.0f);
        diffScratchL_.assign(kControlChunkSamples, 0.0f);
        diffScratchR_.assign(kControlChunkSamples, 0.0f);
        dryScratchL_.assign(kControlChunkSamples, 0.0f);
        dryScratchR_.assign(kControlChunkSamples, 0.0f);
        wetScratchL_.assign(kControlChunkSamples, 0.0f);
        wetScratchR_.assign(kControlChunkSamples, 0.0f);

        // --- step 9: the two shimmer taps (FR-050 - FR-054, plan S7.9) -------
        // TWO PitchShiftProcessor INSTANCES TOTAL, NOT FOUR: they run on a MONO
        // sum of the four longest lines, so stereo costs nothing extra.
        // ShimmerDelay pays one per channel (effects/shimmer_delay.h:88-89);
        // this halves it, and the network's own dense matrix is what
        // re-diffuses the mono return back across the image.
        //
        // RA-6: when the taps are force-disabled NOTHING IS PREPARED AND NOTHING
        // IS ALLOCATED. PitchShiftProcessor::prepare unconditionally prepares
        // all four internal shifters (pitch_shift_processor.h:1213-1216),
        // including the phase vocoder's fixed 4096-point STFT set, so this is
        // ~0.8-1.0 MiB per tap - real memory, not merely CPU (banner item (9)).
        //
        // maxBlockSize = kControlChunkSamples = 64 satisfies the documented
        // [1, 8192] precondition (:139-142) and is the exact block every call
        // uses; 64 is also the shifter's own kSmoothingSubBlockSize (:165).
        if (shimmerAllocated_) {
            shifterOctave_.prepare(sampleRate_, kControlChunkSamples);
            shifterOctave_.setMode(shimmerMode_);
            shifterOctave_.setSemitones(kShimmerOctaveSemitones);
            shifterOctave_.setCents(0.0f);

            shifterFifth_.prepare(sampleRate_, kControlChunkSamples);
            shifterFifth_.setMode(shimmerMode_);
            shifterFifth_.setSemitones(kShimmerFifthSemitones);
            shifterFifth_.setCents(0.0f);

            tapSumScratch_.assign(kControlChunkSamples, 0.0f);
            shimmerOutOctave_.assign(kControlChunkSamples, 0.0f);
            shimmerOutFifth_.assign(kControlChunkSamples, 0.0f);
        } else {
            tapSumScratch_.clear();
            tapSumScratch_.shrink_to_fit();
            shimmerOutOctave_.clear();
            shimmerOutOctave_.shrink_to_fit();
            shimmerOutFifth_.clear();
            shimmerOutFifth_.shrink_to_fit();
        }

        // Per-channel injection masks, so the per-sample loop is branchless.
        // Each pair spans BOTH parities (1 odd + 4 even, 3 odd + 6 even at
        // N = 8; 1 + 8 and 3 + 12 at N = 16), so neither interval is
        // hard-panned by FR-018's even->L / odd->R output split.
        const bool wide = (numChannels_ == 16u);
        for (std::size_t i = 0; i < kMaxChannels; ++i) {
            shimmerOctMask_[i] = 0.0f;
            shimmerFifthMask_[i] = 0.0f;
        }
        if (shimmerAllocated_) {
            for (std::size_t p = 0; p < kShimmerInjectPairSize; ++p) {
                shimmerOctMask_[wide ? kShimmerOctaveInjectChannels16[p]
                                     : kShimmerOctaveInjectChannels8[p]] = 1.0f;
                shimmerFifthMask_[wide ? kShimmerFifthInjectChannels16[p]
                                       : kShimmerFifthInjectChannels8[p]] = 1.0f;
            }
        }

        // FR-059 return shelf, one-pole lowpass split (see returnShelf()). TWO
        // corners: the shimmer legs share kReturnShelfCornerHz (banner item (5c)),
        // the bloom gets its own kBloomShelfCornerHz (banner item (5e)).
        {
            const auto srf = static_cast<float>(sampleRate_);
            returnShelfCoeff_ = shelfCoefficient(kReturnShelfCornerHz, srf);
            bloomShelfCoeff_ = shelfCoefficient(kBloomShelfCornerHz, srf);
        }

        // --- step 10: the harmonic bloom bank (FR-055 - FR-059, plan S7.10) --
        // The bank itself is state, so it is zeroed by the reset() at the end of
        // this function; only the rate- and order-derived constants live here.
        bloomReleaseCoeff_ =
            std::exp(-1.0f / (kBloomReleaseSeconds * static_cast<float>(sampleRate_)));
        for (std::size_t i = 0; i < kMaxChannels; ++i) {
            bloomInjectMask_[i] = 0.0f;
        }
        bloomInjectionGain_ = 0.0f;
        if (bloomEnabled_) {
            const std::size_t injectCount = wide ? kBloomInjectCount16 : kBloomInjectCount8;
            for (std::size_t p = 0; p < injectCount; ++p) {
                bloomInjectMask_[wide ? kBloomInjectChannels16[p] : kBloomInjectChannels8[p]] =
                    1.0f;
            }
            bloomInjectionGain_ = wide ? kBloomInjectionGain16 : kBloomInjectionGain8;
        }

        // --- step 11: the spectral diffusion stage (FR-060 - FR-065, S7.11) --
        // Banner item (13). 75 % overlap WITH the synthesis window - the one
        // configuration primitives/stft.h:225-228 sanctions for a
        // spectral-modification processor, and the one it forbids at 50 %.
        if (spectralEnabled_) {
            stftL_.prepare(diffusionFftSize_, diffusionHopSize_, WindowType::Hann);
            stftR_.prepare(diffusionFftSize_, diffusionHopSize_, WindowType::Hann);
            olaL_.prepare(diffusionFftSize_, diffusionHopSize_, WindowType::Hann,
                          kSpectralKaiserBetaUnused, /*applySynthesisWindow=*/true);
            olaR_.prepare(diffusionFftSize_, diffusionHopSize_, WindowType::Hann,
                          kSpectralKaiserBetaUnused, /*applySynthesisWindow=*/true);
            specL_.prepare(diffusionFftSize_);
            specR_.prepare(diffusionFftSize_);

            // THE RING IS 2 * fftSize == 8 * hop, AND BOTH FACTS ARE USED.
            // Power of two, so the per-sample read wraps with a mask; an exact
            // multiple of hop, so a whole hop-sized pull always lands in one
            // contiguous run and OverlapAdd::pullSamples (which needs a
            // contiguous destination, primitives/stft.h:325-345) can write
            // straight into it. The occupancy never exceeds hop + one slice.
            const std::size_t ring = 2u * diffusionFftSize_;
            wetFifoL_.assign(ring, 0.0f);
            wetFifoR_.assign(ring, 0.0f);
            wetFifoMask_ = ring - std::size_t{1};

            // FR-062: the dry path carries the SAME fftSize so the engine reports
            // ONE latency. kInterpMarginSamples of headroom is load-bearing, not
            // defensive - see banner item (13d): DelayLine::read() clamps to
            // maxDelaySamples_ = (size_t)(sr * (double)maxDelaySeconds)
            // (primitives/delay_line.h:267-269, :293), and the float round-trip
            // of fftSize/sr lands just below fftSize at every supported rate, so
            // an exact request would align the dry one sample short.
            //
            // Both casts are EXPLICIT: DelayLine::prepare is (double, float)
            // (primitives/delay_line.h:86) and an implicit double argument is a
            // C4244 against the zero-warning gate.
            const float alignSeconds = static_cast<float>(
                static_cast<double>(diffusionFftSize_ + kInterpMarginSamples) / sampleRate_);
            dryAlignL_.prepare(sampleRate_, alignSeconds);
            dryAlignR_.prepare(sampleRate_, alignSeconds);
        } else {
            // FR-065: nothing is allocated and nothing is pumped. The STFT /
            // OverlapAdd / SpectralBuffer members are simply never prepared, so
            // they stay at fftSize_ == 0 and hold no buffers.
            wetFifoL_.clear();
            wetFifoL_.shrink_to_fit();
            wetFifoR_.clear();
            wetFifoR_.shrink_to_fit();
            wetFifoMask_ = 0;
            dryAlignL_.prepare(sampleRate_, 0.0f);
            dryAlignR_.prepare(sampleRate_, 0.0f);
        }
        wetFifoRead_ = 0;
        wetFifoWrite_ = 0;
        wetFifoCount_ = 0;
        spectralWarmupRemaining_ = spectralEnabled_ ? diffusionFftSize_ : std::size_t{0};

        // --- step 12: the life modulators (FR-070 - FR-074, plan S7.12) ------
        // BOTH RATES ARE PINNED HERE, NOT INHERITED - see banner item (8b). The
        // modulators' own depths stay at their class defaults (1.0f), so the two
        // Aether depth controls are the only ones.
        breath_.prepare(sampleRate_);
        breath_.setRate(kBreathRateHz);
        tide_.prepare(sampleRate_);
        tide_.setRate(kTideRateNormalised);
        for (std::size_t j = 0; j < (kMaxChannels / 2u); ++j) {
            drift_[j].prepare(sampleRate_);
            // BrownianDrift's own default smoothness is 0.5f (:107); FR-009's
            // is 0.6f, so it is pushed explicitly rather than inherited.
            drift_[j].setSmoothness(modSmoothness_);
        }

        // --- the Dimensionality geodesic (FR-020 - FR-022, plan S7.4 / S7.5).
        // M2 is regenerated by prepare() AND BY NOTHING ELSE - setSeed stores
        // the new seed for the NEXT prepare but never re-runs Gram-Schmidt
        // (FR-021, FR-073, Edge case 23).
        morph_.build(numChannels_, deriveStreamSeed(seed_, kMatrixSalt));
        lastMorphPosition_ = -1.0f;

        // --- step 14: every smoother, configured and snapped to its FR-009
        //     default. Each is READ once per control chunk and ADVANCED with
        //     advanceSamples(kControlChunkSamples) - the cadence rule in banner
        //     item (10). spectralSm_ is the one exception (FR-064, advanced per
        //     STFT frame by the spectral stage).
        const auto sr = static_cast<float>(sampleRate_);
        sizeSm_.configure(kSizeSmoothingMs, sr);
        sizeSm_.snapTo(kDefaultSize);
        densitySm_.configure(kDensitySmoothingMs, sr);
        densitySm_.snapTo(kDefaultDensity);
        decaySm_.configure(kDecaySmoothingMs, sr);
        decaySm_.snapTo(kDefaultDecaySeconds);
        dimSm_.configure(kDimSmoothingMs, sr);
        dimSm_.snapTo(kDefaultDimensionality);
        dampSm_.configure(kDampSmoothingMs, sr);
        dampSm_.snapTo(kDefaultDamping);
        preDelaySm_.configure(kPreDelaySmoothingMs, sr);
        preDelaySm_.snapTo(kDefaultPreDelayMs);
        modDepthSm_.configure(kModDepthSmoothingMs, sr);
        modDepthSm_.snapTo(kDefaultModDepth);
        shimmerOctSm_.configure(kSendSmoothingMs, sr);
        shimmerOctSm_.snapTo(kDefaultSend);
        shimmerFifthSm_.configure(kSendSmoothingMs, sr);
        shimmerFifthSm_.snapTo(kDefaultSend);
        bloomSendSm_.configure(kSendSmoothingMs, sr);
        bloomSendSm_.snapTo(kDefaultSend);
        bloomDecaySm_.configure(kBloomDecaySmoothingMs, sr);
        bloomDecaySm_.snapTo(kDefaultBloomDecay);
        spectralSm_.configure(kSpectralSmoothingMs, sr);
        spectralSm_.snapTo(kDefaultSpectralDiffusion);
        widthSm_.configure(kWidthSmoothingMs, sr);
        widthSm_.snapTo(kDefaultWidth);
        mixSm_.configure(kMixSmoothingMs, sr);
        mixSm_.snapTo(kDefaultMix);

        // The two LinearRamps. Advanced PER SAMPLE by the stages that own them
        // (freeze, silence); configured here so their windows are right from
        // the first block.
        freezeRamp_.configure(kFreezeLatchMs, sr);
        freezeRamp_.snapTo(0.0f);
        outputGate_.configure(kSilenceRampMs, sr);
        outputGate_.snapTo(1.0f);

        // --- step 15: the clear-amortization quota (plan S5.1 step 15, S5.3) -
        // THIS IS A WALL-CLOCK REQUIREMENT, NOT A NICETY. The full clear is
        // 1-5 MiB of memset (banner item (11)) while PrepareConfig admits
        // maxBlockSamples = 64, i.e. a deadline of 1.33 ms at 48 kHz and
        // 0.33 ms at 192 kHz. Sizing the slab so the whole delayBuffer_ is
        // covered in exactly the number of control chunks the fade window
        // contains is what bounds the worst chunk (SC-008 configuration (f)).
        const double fadeSamples = (static_cast<double>(kSilenceRampMs) * sampleRate_) / 1000.0;
        std::size_t fadeChunks =
            static_cast<std::size_t>(fadeSamples) / kControlChunkSamples;
        if (fadeChunks == 0u) {
            fadeChunks = 1u;  // reachable only at very low rates, where the
                              // buffers are correspondingly small (plan S5.3)
        }
        clearQuotaFloats_ = (delayBuffer_.size() + fadeChunks - 1u) / fadeChunks;
        if (clearQuotaFloats_ == 0u) {
            clearQuotaFloats_ = 1u;
        }

        prepared_ = true;
        reset();
    }

    /// @brief Clear all audio state and re-seed the life modulators. FR-006.
    ///
    /// PRESERVES every FR-009 control target and snaps each smoother to it, so a
    /// post-reset render starts already settled at the applied history - the
    /// morph position is restored to the CURRENT Dimensionality target, not to
    /// the 0.35 default. Combined with the smoother-initialisation rule in
    /// applyControl(), that is what makes SC-010 clause 3's
    /// prepare -> H -> render A -> reset() -> render B equality reachable.
    ///
    /// DOES NOT regenerate the random-orthogonal matrix endpoint M2: FR-021
    /// makes it a prepare()-time object, because rebuilding it needs an O(N^3)
    /// Gram-Schmidt over scratch that FR-003 does not allocate for the audio
    /// thread.
    ///
    /// @note Real-time safe. Allocates nothing.
    void reset() noexcept {
        sampleCounter_ = 0;
        anySamplesProcessed_ = false;
        nonFiniteRecoveries_ = 0;
        clearPending_ = false;
        clearArmed_ = false;
        clearCursor_ = 0;
        clearStage_ = 0;
        freezeTarget_ = false;
        gate_ = GateState::Open;

        std::fill(delayBuffer_.begin(), delayBuffer_.end(), 0.0f);
        for (std::size_t i = 0; i < kMaxChannels; ++i) {
            writePos_[i] = 0;
            filterState_[i] = 0.0f;
            dcBlockX_[i] = 0.0f;
            dcBlockY_[i] = 0.0f;
            chanIn_[i] = 0.0f;
            chanOut_[i] = 0.0f;
        }

        preDelayL_.reset();
        preDelayR_.reset();
        diffuser_.reset();
        std::fill(preScratchL_.begin(), preScratchL_.end(), 0.0f);
        std::fill(preScratchR_.begin(), preScratchR_.end(), 0.0f);
        std::fill(diffScratchL_.begin(), diffScratchL_.end(), 0.0f);
        std::fill(diffScratchR_.begin(), diffScratchR_.end(), 0.0f);
        std::fill(dryScratchL_.begin(), dryScratchL_.end(), 0.0f);
        std::fill(dryScratchR_.begin(), dryScratchR_.end(), 0.0f);
        std::fill(wetScratchL_.begin(), wetScratchL_.end(), 0.0f);
        std::fill(wetScratchR_.begin(), wetScratchR_.end(), 0.0f);

        // Shimmer taps: the shifters' grain / crossfade state, the one-chunk
        // deferral buffers and both FR-059 shelf states. PitchShiftProcessor::
        // reset() is a documented no-op when unprepared (:150-152), so the guard
        // is about not touching an object the sub-44.1 kHz path never prepared.
        if (shimmerAllocated_) {
            shifterOctave_.reset();
            shifterFifth_.reset();
        }
        std::fill(tapSumScratch_.begin(), tapSumScratch_.end(), 0.0f);
        std::fill(shimmerOutOctave_.begin(), shimmerOutOctave_.end(), 0.0f);
        std::fill(shimmerOutFifth_.begin(), shimmerOutFifth_.end(), 0.0f);
        shimmerShelfStateOct_ = 0.0f;
        shimmerShelfStateFifth_ = 0.0f;

        // Spectral diffusion: the analysis history, the overlap accumulator, the
        // bin buffers, the output FIFO and the dry-alignment pair. Clearing the
        // FIFO counters is what re-arms the fftSize warm-up offset, so a
        // post-reset render carries the same latency as a fresh prepare()
        // (SC-018 clause 5 after FR-006).
        if (spectralEnabled_) {
            stftL_.reset();
            stftR_.reset();
            olaL_.reset();
            olaR_.reset();
            specL_.reset();
            specR_.reset();
        }
        std::fill(wetFifoL_.begin(), wetFifoL_.end(), 0.0f);
        std::fill(wetFifoR_.begin(), wetFifoR_.end(), 0.0f);
        wetFifoRead_ = 0;
        wetFifoWrite_ = 0;
        wetFifoCount_ = 0;
        spectralWarmupRemaining_ = spectralEnabled_ ? diffusionFftSize_ : std::size_t{0};
        dryAlignL_.reset();
        dryAlignR_.reset();

        // Harmonic bloom: the whole bank, including the voice slots, so a
        // post-reset render is not still holding a chord (plan S5.2). The three
        // slot states are encoded WITHOUT an extra array (see acquireBloomSlot):
        //   free      : bloomFreq_ == 0            (owner -1, not driven, gain 0)
        //   driven    : bloomFreq_ > 0, driven,    owner == voiceId
        //   releasing : bloomFreq_ > 0, not driven, owner -1, gain 0
        for (std::size_t k = 0; k < static_cast<std::size_t>(kMaxBloomResonators); ++k) {
            bloomY1_[k] = 0.0f;
            bloomY2_[k] = 0.0f;
            bloomCoeff_[k] = 0.0f;
            bloomRSq_[k] = 0.0f;
            bloomGain_[k] = 0.0f;
            bloomEnv_[k] = 0.0f;
            bloomFreq_[k] = 0.0f;
            bloomOwner_[k] = kBloomNoOwner;
            bloomDriven_[k] = false;
        }
        for (std::size_t v = 0; v < kMaxBloomVoices; ++v) {
            bloomVoiceId_[v] = kBloomNoOwner;
            bloomVoiceAge_[v] = 0u;
        }
        bloomAgeCounter_ = 0u;
        bloomActiveCount_ = 0;
        bloomInvSqrtCount_ = 0.0f;
        bloomGuardScale_ = 1.0f;
        bloomShelfState_ = 0.0f;
        bloomBankLive_ = false;
        chunkBloomGain_ = 0.0f;
        chunkBloomEmphasisGain_ = 0.0f;
        lastBloomQ_ = -1.0f;

        // FR-006: control targets are PRESERVED and every smoother is snapped
        // to them, so a post-reset render starts already settled.
        sizeSm_.snapToTarget();
        densitySm_.snapToTarget();
        decaySm_.snapToTarget();
        dimSm_.snapToTarget();
        dampSm_.snapToTarget();
        preDelaySm_.snapToTarget();
        modDepthSm_.snapToTarget();
        shimmerOctSm_.snapToTarget();
        shimmerFifthSm_.snapToTarget();
        bloomSendSm_.snapToTarget();
        bloomDecaySm_.snapToTarget();
        spectralSm_.snapToTarget();
        widthSm_.snapToTarget();
        mixSm_.snapToTarget();
        freezeRamp_.snapTo(0.0f);
        outputGate_.snapTo(1.0f);

        // FR-006 / FR-073: re-seed every stochastic stream from the STORED seed
        // BEFORE the control state is refreshed, so the geometry and the morph
        // position materialised below already reflect the rewound modulators.
        reseedStreams();

        lastMorphPosition_ = -1.0f;  // force one re-materialisation of matrix_
        lastJotScale_ = -1.0f;       // force one Jot/damping recompute
        lastJotDecay_ = -1.0f;
        lastJotDamping_ = -1.0f;
        refreshControlState();
    }

    /// @brief Fade out, clear the audio state, fade back in and resume. FR-007.
    ///
    /// THREE PHASES, STRICTLY SEQUENTIAL, and the middle one is amortized
    /// (plan S5.3, banner (11), banner (11b)):
    ///  1. `gate_ = FadingOut`, `outputGate_.setTarget(0)` over kSilenceRampMs
    ///     (advanced PER SAMPLE in renderSlice). NOTHING IS CLEARED YET. The
    ///     engine keeps rendering normally, so the whole tail - including the
    ///     spectral stage's in-flight OverlapAdd frames - fades out under the
    ///     gate with no discontinuity anywhere.
    ///  2. ONLY once the gate has reached EXACTLY 0 (tested at a control step)
    ///     does the clear begin: the freeze latch is abandoned, the O(N)- and
    ///     O(kMaxBloomResonators)-sized scalar state and the bloom bank go
    ///     immediately, and the bulk clear (delayBuffer_ plus the deferred
    ///     sub-object resets) runs ONE WORK UNIT PER CONTROL CHUNK,
    ///     `clearQuotaFloats_` floats and at most one sub-object, so the burst
    ///     can never land inside a single 64-sample callback. Every one of
    ///     those state changes is multiplied by a gate of 0, so NONE of them
    ///     can be audible - that is the whole point of the ordering.
    ///  3. When `clearPending_` clears, the gate ramps back to 1 and rendering
    ///     resumes. NO reset() is required.
    ///
    /// While `clearPending_` the render loop writes LITERAL 0.0f into the delay
    /// lines instead of `chanIn_` and forces the wet contribution to LITERAL
    /// 0.0f. Both are ASSIGNMENTS, NOT `x * 0` PRODUCTS: a value the clear
    /// cursor has not reached yet may still be non-finite, and `NaN * 0` is
    /// `NaN`.
    ///
    /// A second call while already fading out is a no-op - LinearRamp::setTarget
    /// recomputes the increment from the CURRENT value (primitives/smoother.h:
    /// 342-354), so re-targeting mid-fade would restart the 20 ms window and,
    /// called every block, stall the fade indefinitely.
    ///
    /// Calling this during a freeze ABANDONS the latch (Edge case 8): a held
    /// network that has just been zeroed is not a hold of anything. The abandon
    /// happens in phase 2, at gate 0, for the same reason as everything else -
    /// snapping freezeRamp_ moves both the delay read pointers and the per-line
    /// loop gain, which is plainly audible at an open gate.
    ///
    /// @note DELIBERATELY DIVERGES FROM AtmosphereEngine::silence()
    ///       (systems/atmosphere_engine.h:636-644), which LATCHES and stays
    ///       silent until reset(). This one resumes on its own - banner item (7)
    ///       and SC-015's clause S, which a latching implementation fails.
    /// @note Real-time safe. Allocates nothing.
    void silence() noexcept {
        if (!prepared_ || (gate_ == GateState::FadingOut)) {
            return;
        }

        // Phase 1 ONLY. Everything the old form did here now happens in
        // beginDeferredClear(), which runGateMachine() calls at gate 0.
        clearArmed_ = true;
        gate_ = GateState::FadingOut;
        outputGate_.setTarget(0.0f);
    }

    // -------------------------------------------------------------------------
    // Processing
    // -------------------------------------------------------------------------

    /// @brief Render one stereo block. FR-004.
    /// @note Real-time safe. Partition-invariant: all internal cadence is
    ///       anchored to the absolute sample counter, not to caller blocks.
    void processStereoBlock(const float* inLeft, const float* inRight, float* outLeft,
                            float* outRight, std::size_t numSamples) noexcept {
        if ((inLeft == nullptr) || (inRight == nullptr) || (outLeft == nullptr) ||
            (outRight == nullptr)) {
            return;  // FR-004
        }
        if (numSamples == 0u) {
            return;  // no state change; the control grid does not advance
        }
        if (!prepared_) {
            std::fill(outLeft, outLeft + numSamples, 0.0f);
            std::fill(outRight, outRight + numSamples, 0.0f);
            return;
        }

        // FR-005: the control grid is anchored to the ABSOLUTE sample counter,
        // never to caller block boundaries, and runControlStep() always
        // advances by a full kControlChunkSamples. That is what makes the
        // engine's cadence invariant to how the host partitions its blocks.
        std::size_t done = 0;
        while (done < numSamples) {
            const auto phase = static_cast<std::size_t>(sampleCounter_ % kControlChunkSamples);
            if (phase == 0u) {
                runControlStep();
            }
            const std::size_t slice =
                std::min(numSamples - done, kControlChunkSamples - phase);

            renderSlice(inLeft + done, inRight + done, outLeft + done, outRight + done, slice,
                        sampleCounter_);

            sampleCounter_ += static_cast<std::uint64_t>(slice);
            done += slice;
        }
        anySamplesProcessed_ = true;
    }

    // -------------------------------------------------------------------------
    // Control table (FR-009). Every setter: clamp(isFinite(x) ? x : default,
    // lo, hi), then a smoother target (or a raw member where the table says
    // "Smoothing: none"). Real-time safe.
    // -------------------------------------------------------------------------

    /// 0..1, default 0.50, 300 ms smoother -> delay-length scale S(v), FR-012.
    void setSize(float v) noexcept { applyControl(sizeSm_, v, kDefaultSize, 0.0f, 1.0f); }

    /// 0..1, default 0.70, 100 ms smoother -> DiffusionNetwork::setDensity(v*100), FR-040.
    void setDensity(float v) noexcept { applyControl(densitySm_, v, kDefaultDensity, 0.0f, 1.0f); }

    /// 0.5..60 s, default 4.0, 200 ms smoother -> Jot per-line gains, FR-030.
    void setDecaySeconds(float seconds) noexcept {
        applyControl(decaySm_, seconds, kDefaultDecaySeconds, kDecayMinSeconds, kDecayMaxSeconds);
    }

    /// @brief Enter or leave the FR-033 freeze. Default false.
    ///
    /// Not a switch: the transition is a kFreezeLatchMs = 50 ms crossfade on
    /// freezeRamp_, advanced PER SAMPLE inside renderSlice (banner item (10)),
    /// and every one of the six FR-033 steps is expressed on that one ramp. See
    /// banner item (12) for what freeze makes inert and why (RA-5).
    ///
    /// IDEMPOTENT BY CONSTRUCTION. LinearRamp::setTarget recomputes the
    /// increment from the CURRENT value (primitives/smoother.h:342-354), so a
    /// setFreeze(true) repeated during the latch would restart the 50 ms window
    /// from wherever the ramp had reached; called every block it would stall the
    /// latch indefinitely. Redundant calls are therefore dropped.
    void setFreeze(bool on) noexcept {
        if (on == freezeTarget_) {
            return;
        }
        freezeTarget_ = on;
        freezeRamp_.setTarget(on ? 1.0f : 0.0f);
    }

    /// 0..1, default 0.35, 200 ms smoother -> global morph position t, FR-020/FR-023.
    void setDimensionality(float v) noexcept {
        applyControl(dimSm_, v, kDefaultDimensionality, 0.0f, 1.0f);
    }

    /// 0..1, default 0.40, 200 ms smoother -> per-line HF absorption, FR-031.
    void setDamping(float v) noexcept { applyControl(dampSm_, v, kDefaultDamping, 0.0f, 1.0f); }

    /// 0..200 ms, default 0.0, 50 ms smoother -> the stereo pre-delay pair, FR-015.
    void setPreDelayMs(float ms) noexcept {
        applyControl(preDelaySm_, ms, kDefaultPreDelayMs, 0.0f, kMaxPreDelayMs);
    }

    /// 0..1, default 0.25, 100 ms smoother -> per-channel delay jitter depth, FR-072.
    /// The excursion applied to channel i (only i >= N/2 are modulated) is
    /// +/- v * kModExcursionFraction * that channel's OWN current length.
    void setModDepth(float v) noexcept {
        applyControl(modDepthSm_, v, kDefaultModDepth, 0.0f, 1.0f);
    }

    /// @brief 0..1, default 0.60, unsmoothed. FR-072.
    ///
    /// Forwarded VERBATIM to every channel's BrownianDrift::setSmoothness
    /// (processors/brownian_drift.h:152). There is no Hz domain here and none is
    /// advertised: the class's only time-scale control maps to
    /// tau = lerp(kTauMin 0.2 s, kTauMax 30 s, smoothness) (:97-99, :231-234),
    /// so the reachable correlation time is tau in [0.2 s, 30 s] - about
    /// 0.005 .. 0.8 Hz of equivalent wander rate - and the default gives
    /// tau ~= 18 s. A setModRate(Hz) would have no counterpart on the class it
    /// delegates to.
    void setModSmoothness(float v) noexcept {
        modSmoothness_ = std::clamp(isFinite(v) ? v : kDefaultModSmoothness, 0.0f, 1.0f);
        for (std::size_t j = 0; j < (kMaxChannels / 2u); ++j) {
            drift_[j].setSmoothness(modSmoothness_);
        }
    }

    /// @brief 0..1, default 0.0, 100 ms smoother. FR-051, INDEPENDENT of the fifth.
    ///
    /// The value IS the injected gain: it is multiplied by the +12 tap's
    /// kTapInjectionGain, which is sqrt(2/kShimmerInjectPairSize) == 1.0 exactly.
    /// The return is additionally ramped to zero while frozen (FR-033 step 5).
    void setShimmerOctaveSend(float v) noexcept {
        applyControl(shimmerOctSm_, v, kDefaultSend, 0.0f, 1.0f);
    }

    /// @brief 0..1, default 0.0, 100 ms smoother. FR-051, INDEPENDENT of the octave.
    void setShimmerFifthSend(float v) noexcept {
        applyControl(shimmerFifthSm_, v, kDefaultSend, 0.0f, 1.0f);
    }

    /// @brief 0..1, default 0.0, 100 ms smoother. FR-055, INDEPENDENT of both
    ///        shimmer sends (roadmap line 276).
    ///
    /// The mapping is `sendGain = v * kBloomSendMax`; the injected gain is that
    /// times kBloomInjectionGain and the FR-058 normalisations. Muted for the
    /// duration of a freeze (FR-033 step 5, RA-5).
    void setBloomSend(float v) noexcept { applyControl(bloomSendSm_, v, kDefaultSend, 0.0f, 1.0f); }

    /// @brief 0..1, default 0.50, 200 ms smoother -> Q in [20, 400]. FR-057.
    /// @note Applied on the control grid to DRIVEN slots only; a slot that is
    ///       already ringing down keeps the coefficients it was released with, so
    ///       a decay change cannot put a step on a ring-down.
    void setBloomDecay(float v) noexcept {
        applyControl(bloomDecaySm_, v, kDefaultBloomDecay, 0.0f, 1.0f);
    }

    /// @brief 0..1, default 0.0, 100 ms smoother -> the per-bin phase-smear
    ///        amount and its g(a) coherence make-up. FR-060, FR-064, banner (13).
    /// @note Accepted and stored even when the stage was disabled at prepare; it
    ///       simply has nothing to drive (FR-065). The smoother behind it is the
    ///       ONE OnePoleSmoother advanced per STFT FRAME, not per control chunk.
    void setSpectralDiffusion(float v) noexcept {
        applyControl(spectralSm_, v, kDefaultSpectralDiffusion, 0.0f, 1.0f);
    }

    /// @brief 0..1, default 0.20, unsmoothed. FR-070.
    ///
    /// Scales the owned BreathingModulator's [-1,+1] output, which is then ADDED
    /// to the smoothed Size and the COMBINED value clamped to [0,1] BEFORE
    /// FR-012's S(v) mapping. The modulator's own depth stays at 1.0f, so this is
    /// the only depth in the path.
    void setSizeBreathDepth(float v) noexcept {
        sizeBreathDepth_ = std::clamp(isFinite(v) ? v : kDefaultSizeBreathDepth, 0.0f, 1.0f);
    }

    /// @brief 0..1, default 0.20, unsmoothed. FR-071.
    ///
    /// Scales the owned TidalModulator's [-1,+1] output, added to the smoothed
    /// Dimensionality BEFORE FR-023's [0,1] clamp.
    void setDimensionalityTideDepth(float v) noexcept {
        tideDepth_ = std::clamp(isFinite(v) ? v : kDefaultTideDepth, 0.0f, 1.0f);
    }

    /// 0..1, default 1.0, 50 ms smoother -> M/S width on the WET signal, FR-080.
    void setWidth(float v) noexcept { applyControl(widthSm_, v, kDefaultWidth, 0.0f, 1.0f); }

    /// 0..1, default 0.35, 50 ms smoother -> equal-power dry/wet mix, FR-081.
    void setMix(float v) noexcept { applyControl(mixSm_, v, kDefaultMix, 0.0f, 1.0f); }

    /// @brief Store the seed and re-seed every audio-thread-safe stream. FR-073.
    ///
    /// Re-seeds, via deriveStreamSeed(seed, salt) with a distinct compile-time
    /// salt per stream: the BreathingModulator (kBreathSalt), the TidalModulator
    /// (kTideSalt), every modulated channel's BrownianDrift (kDriftSaltBase + j)
    /// and the two spectral-smear streams (kSmearSaltL/R).
    ///
    /// THE RANDOM-ORTHOGONAL MATRIX ENDPOINT M2 IS NOT IN THAT LIST. It is a
    /// prepare()-time object (FR-021): rebuilding it needs an O(N^3) Gram-Schmidt
    /// over scratch this method may not allocate. The stored seed is what the
    /// NEXT prepare() would use if the caller passes it through
    /// PrepareConfig::seed.
    ///
    /// Each stream's seed only takes effect through its owner's reset(), because
    /// setSeed on those classes re-seeds the RNG but does not rewind the phase or
    /// walk state (e.g. TidalModulator's seed varies only the six initial sine
    /// phases, processors/tidal_modulator.h:190-196, which are drawn in
    /// initState). Mid-render this is therefore a discontinuity in the drift and
    /// tide - bounded by BrownianDrift's [-1,+1] clamp
    /// (processors/brownian_drift.h:212-214) and by the geometry's own control
    /// smoothing - which Edge case 23 accepts as the documented consequence.
    ///
    /// @note Real-time safe. Allocates nothing.
    void setSeed(std::uint32_t seed) noexcept {
        seed_ = seed;
        reseedStreams();
    }

    // -------------------------------------------------------------------------
    // Harmonic-bloom note API (FR-056, RA-7)
    // -------------------------------------------------------------------------

    /// @brief Tune a bloom voice to a partial set. A full bank retires its oldest voice.
    ///
    /// @param voiceId  Opaque retire key, exactly as `SympatheticResonance::noteOff`
    ///                 uses it (systems/sympathetic_resonance.h:264). Values below
    ///                 zero are reserved by this class as "no owner" and are
    ///                 rejected.
    /// @param partialHz Partial frequencies in Hz. Nothing is read past @p count.
    /// @param count    Clamped to kMaxBloomResonators (32).
    ///
    /// Edge cases 27, 28, 29, 30, 31 (spec.md:2302-2311):
    ///  - `partialHz == nullptr`, `count == 0` or `!isPrepared()` -> no-op;
    ///  - every frequency is tested with the FR-008 bit-pattern guard and clamped
    ///    to [kBloomMinFreqHz, kBloomMaxFreqFraction * sampleRate] BEFORE ANY
    ///    COEFFICIENT MATHS, so no non-finite coefficient can reach the kernel;
    ///  - a `voiceId` that is already live REPLACES its own partial set;
    ///  - a full bank (kMaxBloomVoices = 8) retires its OLDEST voice by
    ///    bloomVoiceAge_;
    ///  - accepted while frozen, but the return stays muted until the freeze is
    ///    released (FR-033 step 5).
    ///
    /// @note AUDIO-THREAD-CALLABLE. Allocation-free, lock-free, noexcept (FR-008).
    ///       O(kMaxBloomResonators * count) worst case with count <= 32.
    void bloomNoteOn(std::int32_t voiceId, const float* partialHz, std::size_t count) noexcept {
        if (!prepared_ || !bloomEnabled_ || (partialHz == nullptr) || (count == 0u) ||
            (voiceId < 0)) {
            return;
        }
        const std::size_t wanted =
            std::min(count, static_cast<std::size_t>(kMaxBloomResonators));

        // Find this voice's existing slot, else a free one, else the oldest.
        std::size_t voice = kMaxBloomVoices;
        for (std::size_t v = 0; v < kMaxBloomVoices; ++v) {
            if (bloomVoiceId_[v] == voiceId) {
                voice = v;
                break;
            }
        }
        if (voice == kMaxBloomVoices) {
            for (std::size_t v = 0; v < kMaxBloomVoices; ++v) {
                if (bloomVoiceId_[v] == kBloomNoOwner) {
                    voice = v;
                    break;
                }
            }
        }
        if (voice == kMaxBloomVoices) {
            std::size_t oldest = 0;
            for (std::size_t v = 1; v < kMaxBloomVoices; ++v) {
                if (bloomVoiceAge_[v] < bloomVoiceAge_[oldest]) {
                    oldest = v;
                }
            }
            releaseBloomVoice(bloomVoiceId_[oldest]);
            voice = oldest;
        } else if (bloomVoiceId_[voice] == voiceId) {
            // Edge case 29: a re-note REPLACES the set rather than accumulating a
            // second copy. The old resonators are RELEASED, not cut, so the
            // replacement is click-free.
            releaseBloomVoice(voiceId);
        }

        bloomVoiceId_[voice] = voiceId;
        ++bloomAgeCounter_;
        bloomVoiceAge_[voice] = bloomAgeCounter_;

        const float maxHz = kBloomMaxFreqFraction * static_cast<float>(sampleRate_);
        const float q = currentBloomQ();
        for (std::size_t p = 0; p < wanted; ++p) {
            const float raw = partialHz[p];
            // FR-056 / edge case 28: the guard and the clamp both run BEFORE
            // computeBloomSlot, so std::exp/std::cos never see a non-finite input.
            const float f = std::clamp(isFinite(raw) ? raw : kBloomMinFreqHz, kBloomMinFreqHz,
                                       std::max(maxHz, kBloomMinFreqHz));
            const int slot = acquireBloomSlot();
            if (slot < 0) {
                break;  // edge case 27: no allocation, no out-of-bounds write
            }
            const auto k = static_cast<std::size_t>(slot);
            bloomFreq_[k] = f;
            bloomOwner_[k] = voiceId;
            bloomDriven_[k] = true;
            bloomY1_[k] = 0.0f;
            bloomY2_[k] = 0.0f;
            bloomEnv_[k] = 0.0f;
            computeBloomSlot(k, q);
        }
        // lastBloomQ_ is deliberately NOT written here. It is updateBloom()'s
        // retune sentinel, and suppressing that retune would leave the OTHER
        // live voices' slots on a stale Q if setBloomDecay moved between chunks.
        refreshBloomDerived();
    }

    /// @brief Release a bloom voice; its resonators ring down rather than stopping.
    ///
    /// Sets those slots' `gains[k] = 0` and `bloomDriven_[k] = false`. The
    /// resonator keeps its coefficients (r < 1) and rings down through the
    /// kernel's own state, and the CONTROL-GRID reclaim pass frees the slot once
    /// `bloomEnv_[k] < kBloomReclaimThresholdLinear`. Nothing is hard-cut, so the
    /// retirement is click-free by construction. A `voiceId` that was never noted
    /// on is a no-op (edge case 29).
    ///
    /// @note AUDIO-THREAD-CALLABLE. Allocation-free, lock-free, noexcept.
    void bloomNoteOff(std::int32_t voiceId) noexcept {
        if (!prepared_ || (voiceId < 0)) {
            return;
        }
        releaseBloomVoice(voiceId);
        refreshBloomDerived();
    }

    // -------------------------------------------------------------------------
    // Introspection (FR-086). All const, allocation-free, never called from
    // processStereoBlock().
    // -------------------------------------------------------------------------

    [[nodiscard]] bool isPrepared() const noexcept { return prepared_; }  // FR-085

    /// @brief FR-037: true only once the 50 ms latch has COMPLETED.
    /// @note Deliberately NOT `freezeTarget_` alone. During the latch the loop is
    ///       still partly lossy (the crossfades are mid-flight), so a caller that
    ///       asked for freeze is not yet holding one; SC-002 samples
    ///       getStateEnergy() only after this reports true.
    [[nodiscard]] bool isFrozen() const noexcept {
        return freezeTarget_ && (freezeRamp_.getCurrentValue() >= 1.0f);
    }

    /// @brief False below 44.1 kHz or when shimmer was disabled at prepare (RA-6).
    [[nodiscard]] bool isShimmerActive() const noexcept { return shimmerAllocated_; }

    /// @brief ||M^T M - I||_F for the currently applied morph matrix. FR-027.
    /// @note Cached: recomputed only when matrix_ is re-materialised on the
    ///       control grid, so this accessor performs no work.
    [[nodiscard]] float getMatrixOrthogonalityError() const noexcept { return orthogonalityError_; }

    /// @brief Current, Size-scaled delay length of one channel, in samples.
    [[nodiscard]] float getEffectiveDelayLengthSamples(std::size_t channel) const noexcept {
        return (channel < kMaxChannels) ? effectiveDelay_[channel] : 0.0f;
    }

    /// @brief sum(effectiveDelay_[i]) / sampleRate_ from the CURRENT lengths. FR-013.
    [[nodiscard]] float getModalDensityPerHz() const noexcept {
        if (!prepared_ || (sampleRate_ <= 0.0)) {
            return 0.0f;
        }
        double sum = 0.0;
        for (std::size_t i = 0; i < numChannels_; ++i) {
            sum += static_cast<double>(effectiveDelay_[i]);
        }
        return static_cast<float>(sum / sampleRate_);
    }

    /// @brief Largest Size scale the prepared geometry can reach. FR-012.
    [[nodiscard]] float getMaxSizeScale() const noexcept { return maxSizeScale_; }

    /// @brief Global morph position t in [0,1]. FR-023.
    [[nodiscard]] float getCurrentMorphPosition() const noexcept { return morphPosition_; }

    /// @brief Total energy held in the recirculating state. FR-086, Q8.
    ///
    /// THE SUMMATION WINDOW IS NORMATIVE (plan S7.15): per channel, the
    /// `m_i = ceil(effectiveDelay_[i])` MOST RECENT samples - the length the
    /// reads actually use this control chunk - and NOT the whole power-of-two
    /// section. FR-086's prose says "the entire FDN delay-line contents", which
    /// taken literally is not the quantity orthogonality conserves and is not
    /// what SC-002 can be written against:
    ///
    ///   - Sections are nextPowerOf2(ceil(ref_i * S_max * 1.005) + 4), e.g.
    ///     32768 floats for a 20 348-sample line at S = 4, so a whole-section
    ///     sweep carries ~40 % stale history at S = 4 and ~96 % at S = 0.25.
    ///   - That stale span still holds PRE-freeze content for the first ~0.7 s
    ///     after a latch, landing straight inside SC-002 clause 1's +/-0.5 dB
    ///     window.
    ///   - The conserved quantity implied by FR-025 is the L2 norm of the
    ///     N-channel STATE VECTOR: each freeze step drops the sample at offset
    ///     m_i and adds ||M * read||^2 == ||read||^2. Summing to sectionSize
    ///     instead drops the sample at offset sectionSize, and conservation does
    ///     not follow.
    ///
    /// Under freeze effectiveDelay_ is latched (FR-034) and the reads are integer
    /// at round(latched) (FR-033 step 2), so the window this sums is the window
    /// the loop recirculates, to within the one sample by which ceil and round
    /// can differ - about 1 part in m_i, i.e. ~1e-4 of the total at N = 8, three
    /// orders of magnitude inside SC-002's bound.
    ///
    /// @note Diagnostic accessor. An on-demand full sweep accumulated in double
    ///       (Q8), allocation-free and const. NEVER called from
    ///       processStereoBlock() - it is O(sum m_i), ~82 k reads at N = 8,
    ///       S = 4.
    [[nodiscard]] float getStateEnergy() const noexcept {
        if (!prepared_) {
            return 0.0f;
        }
        double e = 0.0;
        for (std::size_t i = 0; i < numChannels_; ++i) {
            const auto m = static_cast<std::size_t>(std::ceil(effectiveDelay_[i]));
            for (std::size_t k = 0; k < m; ++k) {
                // writePos_ - 1 - k wraps modulo 2^64 and sectionMask_ + 1 is a
                // power of two, so the mask recovers the right index without a
                // signed subtraction.
                const std::size_t idx =
                    sectionOffset_[i] + ((writePos_[i] - 1u - k) & sectionMask_[i]);
                const auto s = static_cast<double>(delayBuffer_[idx]);
                e += s * s;
            }
        }
        return static_cast<float>(e);
    }

    /// @brief Number of DRIVEN bloom resonators. FR-055, FR-086.
    /// @note Driven, not allocated - a slot released by bloomNoteOff is still
    ///       ringing (and still audible) but is no longer counted here, which is
    ///       what makes SC-016 clause 3's release assertion measurable.
    [[nodiscard]] std::size_t getActiveBloomResonatorCount() const noexcept {
        return bloomActiveCount_;
    }

    /// @brief Number of times the FR-083 sweep found non-finite state and recovered.
    [[nodiscard]] std::size_t getNonFiniteRecoveryCount() const noexcept {
        return nonFiniteRecoveries_;
    }

    /// @brief True while a silence() fade or an emergencyClear() is still in
    ///        flight - i.e. the amortized bulk clear has not finished OR the
    ///        output gate has not returned to unity.
    ///
    /// This is the accessor SC-014 clause 3's RECOVERY POINT is defined against
    /// (plan S7.14: "the first sample at which both `clearPending_` is finished
    /// and the fade-in has completed"). Without it that clause would have to
    /// re-derive the recovery instant from kSilenceRampMs, kControlChunkSamples
    /// and delayBuffer_.size(), i.e. re-implement the amortization it is
    /// supposed to be measuring. Both terms are updated on the CONTROL GRID, so
    /// a caller polling between blocks resolves the point to one control chunk.
    [[nodiscard]] bool isRecovering() const noexcept {
        return clearPending_ || (gate_ != GateState::Open);
    }

    /// @brief Reported algorithmic latency. FR-084, RA-2.
    /// @return diffusionFftSize when the spectral stage is enabled at prepare,
    ///         otherwise exactly 0. Constant for a prepared configuration - no
    ///         setter changes it. The shimmer taps' latency is NOT included:
    ///         they live inside the feedback loop.
    [[nodiscard]] std::size_t getLatencySamples() const noexcept {
        return spectralEnabled_ ? diffusionFftSize_ : std::size_t{0};
    }

    /// @brief Copy the currently applied N x N morph matrix, row-major.
    /// @param dstRowMajor Destination, n * n floats.
    /// @param n Must equal the prepared channel count; anything else is a no-op
    ///          (FR-026 makes the order a prepare-time property).
    void copyCurrentMatrix(float* dstRowMajor, std::size_t n) const noexcept {
        if ((dstRowMajor == nullptr) || !prepared_ || (n != numChannels_)) {
            return;
        }
        const std::size_t count = n * n;
        for (std::size_t i = 0; i < count; ++i) {
            dstRowMajor[i] = matrix_[i];
        }
    }

    /// @brief Apply the currently applied morph matrix to an N-vector.
    /// @param in  N input samples.
    /// @param out N output samples. Must not alias @p in.
    /// @note Introspection only - the per-sample loop (FR-024) inlines its own
    ///       multiply. The accumulator is double so SC-004 clause 2's 1e-6
    ///       agreement with copyCurrentMatrix is a statement about the MATRIX,
    ///       not about float summation order at N = 16.
    void applyCurrentMatrix(const float* in, float* out) const noexcept {
        if ((in == nullptr) || (out == nullptr) || !prepared_) {
            return;
        }
        const std::size_t n = numChannels_;
        for (std::size_t i = 0; i < n; ++i) {
            double acc = 0.0;
            for (std::size_t j = 0; j < n; ++j) {
                acc += static_cast<double>(matrix_[(i * n) + j]) * static_cast<double>(in[j]);
            }
            out[i] = static_cast<float>(acc);
        }
    }

    // -------------------------------------------------------------------------
    // Prepare-time linear algebra. PUBLIC so SC-004 clause 6 can test it without
    // friend-declaring the test.
    // -------------------------------------------------------------------------

    /// @brief Real-Schur reduction of R in SO(n): R == V * B(theta) * V^T, with V
    ///        orthogonal and B block-diagonal in 2x2 rotations.
    /// @param rRowMajor Input n*n matrix, row-major.
    /// @param n Must be even and <= 16.
    /// @param vRowMajor Out: n*n orthogonal V, row-major.
    /// @param thetas Out: n/2 rotation angles.
    /// @return false if R is not numerically in SO(n).
    /// @note Prepare-time only. Not real-time safe to rely on for timing, but it
    ///       is allocation-free and bounded-iteration all the same. Internally
    ///       everything is computed in double; only the two outputs are floats.
    [[nodiscard]] static bool schurReduceSO(const float* rRowMajor, std::size_t n,
                                            float* vRowMajor, float* thetas) noexcept {
        if ((rRowMajor == nullptr) || (vRowMajor == nullptr) || (thetas == nullptr)) {
            return false;
        }
        if ((n < 2u) || (n > kMaxChannels) || ((n % 2u) != 0u)) {
            return false;
        }
        double r[kMaxChannels * kMaxChannels]{};
        double v[kMaxChannels * kMaxChannels]{};
        double angles[kMaxChannels / 2u]{};
        for (std::size_t i = 0; i < (n * n); ++i) {
            r[i] = static_cast<double>(rRowMajor[i]);
        }
        if (!detail::aetherSchurReduceSO(r, n, v, angles)) {
            return false;
        }
        for (std::size_t i = 0; i < (n * n); ++i) {
            vRowMajor[i] = static_cast<float>(v[i]);
        }
        for (std::size_t b = 0; b < (n / 2u); ++b) {
            thetas[b] = static_cast<float>(angles[b]);
        }
        return true;
    }

#if defined(KRATE_DSP_AETHER_TEST_HOOKS)
    // ---- FR-083 fault injection - TEST BUILDS ONLY --------------------------
    /// @brief Writes a bit-pattern NaN into filterState_[0] - the exact array
    ///        FR-083's control-grid sweep tests.
    ///
    /// This is the ONLY way to make internal state non-finite: every input path
    /// is sealed (FR-082 replaces non-finite input with 0.0f, every setter falls
    /// back to its default, bloomNoteOn clamps every partial before coefficient
    /// computation) and FR-025 + FR-032 make the unfrozen loop structurally
    /// non-expansive, so no legal call sequence can drive the state to Inf.
    /// Without this hook SC-014 clause 3 is unimplementable and FR-083's
    /// detect -> emergencyClear -> count branch is dead code.
    ///
    /// @pre MUST be called at a control-chunk boundary, i.e. between
    ///      processStereoBlock() calls whose cumulative sample count is a
    ///      multiple of kControlChunkSamples. The FR-083 sweep runs at the TOP of
    ///      the next control step, before any sample of that chunk is rendered,
    ///      so the fault is caught before it can reach the output. Called
    ///      mid-chunk, up to 63 non-finite samples reach the wet path - outside
    ///      FR-083's contract and not what SC-014 clause 1 asserts against.
    /// @note Absent from the shipping build. Never called from process().
    void injectNonFiniteStateForTest() noexcept {
        // Built from a BIT PATTERN through a volatile sink, never from
        // std::numeric_limits: this header is compiled with -ffast-math by four
        // of the five Phase 6 test TUs and by every shipping target, where
        // quiet_NaN() folds to finite garbage and would inject nothing at all.
        volatile std::uint32_t bits = 0x7FC00000u;      // quiet NaN
        const std::uint32_t materialized = bits;        // the volatile READ is the sink
        filterState_[0] = std::bit_cast<float>(materialized);
    }
#endif

private:
    static constexpr std::size_t kMaxChannels = 16;
    static_assert(kMaxChannels == detail::kAetherMaxMatrixOrder,
                  "the matrix helpers' stack arrays must be sized for kMaxChannels");

    // ---- FR-009 defaults / smoothing times (the whole control table) --------
    static constexpr float kDefaultSize = 0.50f;
    static constexpr float kSizeSmoothingMs = 300.0f;
    static constexpr float kDefaultDensity = 0.70f;
    static constexpr float kDensitySmoothingMs = 100.0f;
    static constexpr float kDefaultDecaySeconds = 4.0f;
    static constexpr float kDecayMinSeconds = 0.5f;   ///< RA-4: the range tops out at 60 s,
    static constexpr float kDecayMaxSeconds = 60.0f;  ///<        "infinite" is setFreeze.
    static constexpr float kDecaySmoothingMs = 200.0f;
    static constexpr float kDefaultDimensionality = 0.35f;
    static constexpr float kDimSmoothingMs = 200.0f;
    static constexpr float kDefaultDamping = 0.40f;
    static constexpr float kDampSmoothingMs = 200.0f;
    static constexpr float kDefaultPreDelayMs = 0.0f;
    static constexpr float kMaxPreDelayMs = 200.0f;
    static constexpr float kPreDelayMaxSeconds = 0.200f;
    static constexpr float kPreDelaySmoothingMs = 50.0f;
    static constexpr float kDefaultModDepth = 0.25f;
    static constexpr float kModDepthSmoothingMs = 100.0f;
    static constexpr float kDefaultModSmoothness = 0.60f;   ///< -> tau ~= 18 s
    static constexpr float kDefaultSizeBreathDepth = 0.20f;  ///< FR-070
    static constexpr float kDefaultTideDepth = 0.20f;        ///< FR-071
    /// FR-070's PINNED breath rate: 0.05 Hz == a 20 s period, inside
    /// BreathingModulator's [kMinRate 0.01, kMaxRate 0.5]
    /// (processors/breathing_modulator.h:108-110). NOT the class default 0.1f.
    static constexpr float kBreathRateHz = 0.05f;
    /// FR-071's PINNED tide rate, NORMALIZED (TidalModulator::setRate takes 0..1,
    /// processors/tidal_modulator.h:202-205). 1.0 == the fastest setting, giving
    /// getBasePeriodSeconds() == kMinPeriod == 30 s and layer periods
    /// 30 / 42.43 / 51.96 s. NOT the class default 0.5f, which would be ~188 s.
    static constexpr float kTideRateNormalised = 1.0f;
    static constexpr float kDefaultSend = 0.0f;
    static constexpr float kSendSmoothingMs = 100.0f;
    static constexpr float kDefaultBloomDecay = 0.50f;
    static constexpr float kBloomDecaySmoothingMs = 200.0f;
    static constexpr float kDefaultSpectralDiffusion = 0.0f;
    static constexpr float kSpectralSmoothingMs = 100.0f;
    /// OverlapAdd::prepare's kaiserBeta argument. Named rather than written as a
    /// bare 9.0f because it is INERT here - it is consumed only when the window
    /// type is Kaiser (primitives/stft.h:216-219, :239) and this stage is Hann.
    static constexpr float kSpectralKaiserBetaUnused = 9.0f;
    /// Banner item (13b). g(a) at a = 0, 0.25, 0.5, 0.75, 1.0 - the reciprocals of
    /// the measured outRMS/inRMS of the randomised-phase OverlapAdd, which loses
    /// coherence as the smear grows and which OverlapAdd's own fixed COLA factor
    /// (primitives/stft.h:243-262) cannot know about.
    static constexpr std::size_t kCoherenceKnotCount = 5;
    static constexpr float kCoherenceMakeup[kCoherenceKnotCount] = {1.0000f, 1.0799f, 1.3435f,
                                                                    1.7746f, 1.9996f};
    static constexpr float kDefaultWidth = 1.0f;
    static constexpr float kWidthSmoothingMs = 50.0f;
    static constexpr float kDefaultMix = 0.35f;
    static constexpr float kMixSmoothingMs = 50.0f;

    /// Recompute gate for the Jot/damping coefficients (plan S7.6). 16*N powf
    /// calls is the heaviest control-grid item, so it is skipped entirely
    /// unless S, decaySeconds or damping actually moved.
    static constexpr float kJotRecomputeEpsilon = 1e-7f;
    /// FR-031: T60_nyq = T60_dc * kDampingNyquistRatio^damping, i.e. 20x
    /// shorter at Nyquist at damping = 1 (fdn_reverb.h:578-580).
    static constexpr float kDampingNyquistRatio = 0.05f;

    /// "No owner" sentinel for bloomOwner_ / bloomVoiceId_, matching
    /// SympatheticResonance's own convention (`ownerVoiceIds_[idx].fill(-1)`,
    /// systems/sympathetic_resonance.h:349). Negative voiceIds are therefore
    /// RESERVED and bloomNoteOn/bloomNoteOff reject them.
    static constexpr std::int32_t kBloomNoOwner = -1;

    // -------------------------------------------------------------------------
    // MatrixMorph - the real-Schur geodesic (FR-022, Q4, plan S7.5)
    // -------------------------------------------------------------------------

    /// @brief Prepare-time factors of the Dimensionality morph, plus its
    ///        control-grid evaluation.
    ///
    /// For each of the two segments (A1 = M0, B1 = M1; A2 = M1, B2 = M2) prepare
    /// forms the relative rotation R = A^T B - which is in SO(N) precisely
    /// because C-8 pinned BOTH endpoints to det = -1 - reduces it to real Schur
    /// form R = V B(theta) V^T, and precomputes AV = A V. On the control grid
    ///
    ///     M(u) = AV * B(u * theta) * V^T,   u = 2t  (t < 0.5)  or  2t - 1
    ///
    /// which is a product of orthogonal factors and therefore EXACTLY orthogonal
    /// at every u. There is deliberately no re-orthonormalisation step: there is
    /// nothing for one to fix, and Newton-Schulz has sigma = 0 as a fixed point
    /// (C-8), so a mechanism that needs one is the wrong mechanism.
    ///
    /// Endpoint-exact by construction: B(0) = I gives M = A, and B(theta) = R
    /// gives M = A A^T B = B.
    ///
    /// State is double. See the detail-namespace banner for why.
    struct MatrixMorph {
        static constexpr std::size_t kOrder = detail::kAetherMaxMatrixOrder;
        static constexpr std::size_t kEntries = kOrder * kOrder;

        std::size_t order = 8;   ///< N
        bool valid = false;      ///< false only if a reduction failed
        double endpoint[3][kEntries]{};    ///< M0 (t=0), M1 (t=0.5), M2 (t=1)
        double basis[2][kEntries]{};       ///< V, per segment
        double preRotated[2][kEntries]{};  ///< A_seg * V_seg, per segment
        double angle[2][kOrder / 2u]{};    ///< theta, per segment

        /// @brief Build the three endpoints and both segments' Schur factors.
        /// @note prepare()-time only. Allocation-free, bounded iteration.
        void build(std::size_t channels, std::uint32_t matrixStreamSeed) noexcept {
            order = channels;
            valid = false;
            const std::size_t n = order;
            if ((n < 2u) || (n > kOrder) || ((n % 2u) != 0u)) {
                return;
            }

            detail::aetherBuildHouseholder(endpoint[0], n);
            detail::aetherBuildHadamard(endpoint[1], n);
            detail::aetherBuildRandomOrthogonal(endpoint[2], n, matrixStreamSeed);

            double relative[kEntries]{};
            for (std::size_t seg = 0; seg < 2u; ++seg) {
                const double* a = endpoint[seg];
                const double* b = endpoint[seg + 1u];
                for (std::size_t i = 0; i < n; ++i) {  // R = A^T B
                    for (std::size_t j = 0; j < n; ++j) {
                        double acc = 0.0;
                        for (std::size_t k = 0; k < n; ++k) {
                            acc += a[(k * n) + i] * b[(k * n) + j];
                        }
                        relative[(i * n) + j] = acc;
                    }
                }
                if (!detail::aetherSchurReduceSO(relative, n, basis[seg], angle[seg])) {
                    return;
                }
                for (std::size_t i = 0; i < n; ++i) {  // AV = A V
                    for (std::size_t j = 0; j < n; ++j) {
                        double acc = 0.0;
                        for (std::size_t k = 0; k < n; ++k) {
                            acc += a[(i * n) + k] * basis[seg][(k * n) + j];
                        }
                        preRotated[seg][(i * n) + j] = acc;
                    }
                }
            }
            valid = true;
        }

        /// @brief Materialise M(t) into an n * n row-major float destination.
        /// @note Control-grid rate (once per kControlChunkSamples), never per
        ///       sample. Allocation-free.
        void evaluate(float t, float* dstRowMajor) const noexcept {
            const std::size_t n = order;
            const float clamped = std::clamp(t, 0.0f, 1.0f);
            const std::size_t seg = (clamped < 0.5f) ? 0u : 1u;
            const double global = static_cast<double>(clamped);
            const double u = std::clamp((seg == 0u) ? (2.0 * global) : ((2.0 * global) - 1.0),
                                        0.0, 1.0);

            if (!valid) {
                // Unreachable for the shipped endpoints. Fall back to the nearer
                // endpoint, which is still exactly orthogonal with det = -1.
                const std::size_t nearest = (u < 0.5) ? seg : (seg + 1u);
                for (std::size_t i = 0; i < (n * n); ++i) {
                    dstRowMajor[i] = static_cast<float>(endpoint[nearest][i]);
                }
                return;
            }

            // tmp = AV * B(u*theta). B is block-diagonal, so each block touches
            // exactly two columns: 4N MACs per block, 4N^2 in total.
            double tmp[kEntries]{};
            const double* av = preRotated[seg];
            for (std::size_t blk = 0; blk < (n / 2u); ++blk) {
                const double a = u * angle[seg][blk];
                const double c = std::cos(a);
                const double s = std::sin(a);
                const std::size_t c0 = 2u * blk;
                const std::size_t c1 = c0 + 1u;
                for (std::size_t i = 0; i < n; ++i) {
                    const double x0 = av[(i * n) + c0];
                    const double x1 = av[(i * n) + c1];
                    tmp[(i * n) + c0] = (x0 * c) + (x1 * s);
                    tmp[(i * n) + c1] = (x1 * c) - (x0 * s);
                }
            }

            // M = tmp * V^T.
            const double* vb = basis[seg];
            for (std::size_t i = 0; i < n; ++i) {
                for (std::size_t j = 0; j < n; ++j) {
                    double acc = 0.0;
                    for (std::size_t k = 0; k < n; ++k) {
                        acc += tmp[(i * n) + k] * vb[(j * n) + k];
                    }
                    dstRowMajor[(i * n) + j] = static_cast<float>(acc);
                }
            }
        }
    };

    // -------------------------------------------------------------------------
    // Setter contract (FR-008, FR-009)
    // -------------------------------------------------------------------------

    /// @brief Bit-pattern finiteness test, deliberately NOT inlined.
    ///
    /// ITERUM_NOINLINE (primitives/smoother.h:39-45) is LOAD-BEARING, not style:
    /// without it the guard is inlined and folded away under -ffast-math on the
    /// macOS leg. Composes the existing Layer 0 helpers detail::isNaN
    /// (core/db_utils.h:54) and detail::isInf (:175) - FR-008 forbids a fourth
    /// reimplementation of the bit test.
    [[nodiscard]] ITERUM_NOINLINE static bool isFinite(float v) noexcept {
        return !detail::isNaN(v) && !detail::isInf(v);
    }

    /// @brief The uniform FR-009 setter body: fall back to the control's default
    ///        if the argument is not finite, clamp to range, then either SNAP or
    ///        ramp.
    ///
    /// Smoother-initialisation rule (FR-009, binding): a setter called while
    /// prepared and before any sample has been processed since the last
    /// prepare()/reset() snaps instead of ramping, so "configure then render"
    /// means what the caller expects and a post-reset render matches the
    /// original (SC-010 clause 3).
    void applyControl(OnePoleSmoother& smoother, float v, float fallback, float lo,
                      float hi) noexcept {
        const float clamped = std::clamp(isFinite(v) ? v : fallback, lo, hi);
        if (prepared_ && !anySamplesProcessed_) {
            smoother.snapTo(clamped);
        } else {
            smoother.setTarget(clamped);
        }
    }

    // -------------------------------------------------------------------------
    // Stochastic streams (FR-006, FR-073, plan S5.2)
    // -------------------------------------------------------------------------

    /// @brief Re-seed every audio-thread-safe stochastic stream from seed_.
    ///
    /// THE DERIVED SEED MUST BE RE-APPLIED EXPLICITLY. Each modulator re-seeds
    /// its own RNG inside reset() (processors/brownian_drift.h:133, :242-247 and
    /// the equivalents in breathing_modulator.h:152, :234-240 and
    /// tidal_modulator.h:178), but only from the seed it was last GIVEN - so a
    /// reset() without this call would rewind the streams to whatever seed was
    /// current, not to deriveStreamSeed(seed_, salt). Re-applying it here is what
    /// makes a post-reset render match the original (SC-010 clause 3).
    ///
    /// The order is setSeed-then-reset per object, because setSeed only stores
    /// the value and re-seeds the RNG; reset() is what draws the initial state
    /// from it.
    ///
    /// Every drift slot is re-seeded, not only the numChannels_/2 that are in
    /// use, so a later prepare() at N = 16 cannot inherit a stale stream.
    void reseedStreams() noexcept {
        breath_.setSeed(deriveStreamSeed(seed_, kBreathSalt));
        breath_.reset();
        tide_.setSeed(deriveStreamSeed(seed_, kTideSalt));
        tide_.reset();
        for (std::size_t j = 0; j < (kMaxChannels / 2u); ++j) {
            drift_[j].setSeed(deriveStreamSeed(seed_, kDriftSaltBase + j));
            drift_[j].reset();
        }
        smearRngL_.seed(deriveStreamSeed(seed_, kSmearSaltL));
        smearRngR_.seed(deriveStreamSeed(seed_, kSmearSaltR));
    }

    /// @brief (1 - t) * a + t * b, the FR-033 crossfade primitive.
    ///
    /// WRITTEN IN THIS FORM DELIBERATELY. Under IEEE semantics t == 1 gives
    /// `0 * a + 1 * b`, i.e. EXACTLY b, where the algebraically identical
    /// `a + (b - a) * t` does not. Step 2 wants an exactly-integer read length at
    /// freezeRamp == 1 so delayReadInterpolated() takes its integer path and no
    /// interpolation touches the frozen signal (C-4), and step 6 wants an
    /// exactly-unity loop gain. Neither is worth a ULP of drift.
    [[nodiscard]] static float crossfade(float a, float b, float t) noexcept {
        return ((1.0f - t) * a) + (t * b);
    }

    // -------------------------------------------------------------------------
    // Geometry (FR-012, FR-013, plan S7.2 / S7.3)
    // -------------------------------------------------------------------------

    /// @brief S(v) = 0.25 * 2^(4v), clamped to the prepared geometry's reach.
    ///
    /// exp2 rather than pow: one intrinsic, and S(0) = 0.25, S(0.5) = 1.0,
    /// S(1) = 4.0 come out exactly. Evaluated once per control chunk.
    [[nodiscard]] float sizeScale(float v) const noexcept {
        return std::min(0.25f * std::exp2(4.0f * v), maxSizeScale_);
    }

    /// @brief Materialise the Size-scaled, breath- and drift-modulated delay
    ///        lengths (plan S6.2 step 4).
    ///
    /// FR-070: the breath is added to the SMOOTHED Size and the COMBINED value is
    /// clamped to [0,1] BEFORE the S(v) mapping - not after - because S is
    /// exponential and clamping the scale instead of the control would move the
    /// mapping rather than the range. BreathingModulator's output is a fixed
    /// bipolar [-1,+1] that does not shrink with its own depth
    /// (processors/breathing_modulator.h:103-104), so at sizeBreathDepth = 1 and
    /// Size = 0.5 the clamp is genuinely active and the reachable span is the
    /// whole S in [0.25, 4.0].
    ///
    /// FR-072: only the LONGEST HALF of the channels are jittered (i >= N/2), as
    /// FDNReverb does (fdn_reverb.h:191-196), each by its OWN BrownianDrift and
    /// by a fraction of its OWN current length - see banner item (8). Channel 0
    /// is therefore never drift-modulated, which is what makes it the clean
    /// FR-070 depth-0 control.
    ///
    /// @note NOT CALLED WHILE FROZEN - see refreshControlState() and FR-034.
    void updateGeometry() noexcept {
        const float breath = sizeBreathDepth_ * breath_.getCurrentValue();
        sizeCombined_ = std::clamp(sizeSm_.getCurrentValue() + breath, 0.0f, 1.0f);
        currentSizeScale_ = sizeScale(sizeCombined_);

        const float modDepth = std::clamp(modDepthSm_.getCurrentValue(), 0.0f, 1.0f);
        const std::size_t firstModulated = numChannels_ / 2u;
        for (std::size_t i = 0; i < kMaxChannels; ++i) {
            if (i >= numChannels_) {
                effectiveDelay_[i] = 0.0f;
                continue;
            }
            const float base = refDelaySamples_[i] * currentSizeScale_;
            float length = base;
            if (i >= firstModulated) {
                length += modDepth * kModExcursionFraction * base *
                          drift_[i - firstModulated].getCurrentValue();
            }
            effectiveDelay_[i] = length;
        }

        // FR-033 step 2's latch target: the integer length the reads collapse to
        // at freezeRamp == 1. It tracks the geometry ONLY while the engine is
        // fully thawed. Updating it while the ramp is still unwinding after a
        // setFreeze(false) would move the crossfade's frozen endpoint underneath
        // it and put a step on the read pointer - the staircase SC-015 forbids.
        if (freezeRamp_.getCurrentValue() <= 0.0f) {
            for (std::size_t i = 0; i < kMaxChannels; ++i) {
                latchedDelay_[i] = std::round(std::max(1.0f, effectiveDelay_[i]));
            }
        }
    }

    // -------------------------------------------------------------------------
    // Decay and damping (FR-030 - FR-032, plan S7.6)
    // -------------------------------------------------------------------------

    /// @brief Recompute the Jot per-line gains and the damping one-pole
    ///        coefficients (plan S6.2 step 5).
    ///
    /// The formulas are FDNReverb's, re-derived from effects/fdn_reverb.h:576-600
    /// with ONE structural difference: THERE IS NO SEPARATE BASE fbGain. That
    /// class divides gDC by a roomSize-derived base (:589-591); here the
    /// per-line gain IS gDC, which is what makes FR-032's "loop gain <= 1.0 at
    /// all times outside freeze" a property of the construction rather than a
    /// correction factor. Combined with FR-025 (M(t) exactly orthogonal, hence
    /// exactly unit loop gain) the unfrozen loop is unconditionally
    /// non-expansive at every Size, Decay and Dimensionality.
    ///
    /// The one-pole y = c*x + (1-c)*y has DC gain exactly 1 and Nyquist gain
    /// c/(2-c), so the absorption gain is a separate multiply.
    ///
    /// JOT GAIN PLACEMENT (renderSlice step 9) - the input injection is INSIDE
    /// the per-line gain, `chanIn = (matrixOut + inject) * g_i`, and that is a
    /// deliberate divergence from plan S6.3 step 9's literal transcription of
    /// fdn_reverb.h:336-338. The reason is a verified difference in what that
    /// line multiplies:
    ///
    ///   FDNReverb applies its PER-LINE Jot correction filterGainDC_[i] on the
    ///   READ side (:304), and :337's write-side feedbackGains_[i] is a single
    ///   UNIFORM base (:567, `feedbackGains_[i] = fbGain` for every i). Adding
    ///   the input after a uniform gain cannot distort anything per line.
    ///   AetherReverb has no base gain at all - the per-line gDC IS the gain -
    ///   so putting it in :337's slot with the injection outside it would make
    ///   the injection the ONLY signal in the loop that never pays for the
    ///   transit it is about to make.
    ///
    /// The consequence is not cosmetic. With g_i = alpha^(m_i),
    /// alpha = 10^(-3/(T60*sr)), writing everything through g_i makes the whole
    /// impulse response exactly `alpha^n * (the same network with g = 1)`: the
    /// substitution y_i(n) = g_i * p_i(n - m_i) turns the state recursion into a
    /// lossless FDN, and since the output taps read the STORED value (FR-018,
    /// before damping) the taps see exactly y_i(n). Every arrival, including
    /// the very first one out of each line, therefore lands on the T60 envelope.
    /// Leaving the injection outside makes the direct arrival out of line i too
    /// loud by 1/g_i = alpha^(-m_i). At the SC-005 grid point decaySeconds = 0.5
    /// with size = 1.0 the longest line is 20 348 samples and alpha^(-m) is
    /// 350x - i.e. +51 dB - so the impulse response degenerates into eight
    /// unattenuated echoes spread over 424 ms and the measured T60 is ~0.9 s
    /// against a requested 0.5 s. That is FR-030's own criterion, which B-4
    /// forbids relaxing; the ordering is what has to move.
    ///
    /// Freeze is unaffected: at freezeRamp = 1 the gain is 1 and the injection
    /// is 0, so both orderings collapse to the same expression.
    void updateDecayAndDamping() noexcept {
        if (!prepared_ || (sampleRate_ <= 0.0)) {
            return;
        }
        const float decaySeconds =
            std::clamp(decaySm_.getCurrentValue(), kDecayMinSeconds, kDecayMaxSeconds);
        const float damping = std::clamp(dampSm_.getCurrentValue(), 0.0f, 1.0f);

        // 16*N powf calls; skip entirely unless an input actually moved.
        if ((std::abs(currentSizeScale_ - lastJotScale_) <= kJotRecomputeEpsilon) &&
            (std::abs(decaySeconds - lastJotDecay_) <= kJotRecomputeEpsilon) &&
            (std::abs(damping - lastJotDamping_) <= kJotRecomputeEpsilon)) {
            return;
        }
        lastJotScale_ = currentSizeScale_;
        lastJotDecay_ = decaySeconds;
        lastJotDamping_ = damping;

        const auto sr = static_cast<float>(sampleRate_);
        const float t60dc = decaySeconds;
        const float t60nyq = t60dc * std::pow(kDampingNyquistRatio, damping);
        for (std::size_t i = 0; i < numChannels_; ++i) {
            const float m = effectiveDelay_[i];
            const float gDC = std::pow(10.0f, -3.0f * m / (t60dc * sr));
            const float gNyq = std::pow(10.0f, -3.0f * m / (t60nyq * sr));
            feedbackGain_[i] = std::min(gDC, 1.0f);
            float ratio = (gDC > 1e-10f) ? (gNyq / gDC) : 1.0f;
            ratio = std::clamp(ratio, 0.0f, 1.0f);
            dampCoeff_[i] = std::clamp((2.0f * ratio) / (1.0f + ratio), 0.001f, 1.0f);
        }
    }

    /// @brief Materialise M(t) for this control chunk (plan S6.2 step 6).
    ///
    /// FR-023 / FR-071: the morph position is the smoothed setDimensionality
    /// value plus the tide term, clamped to [0, 1]. Under P-1 the tide term is
    /// zero, so t is exactly the setter's. The recompute is gated on
    /// kMorphEpsilon so a settled Dimensionality costs nothing per chunk -
    /// morphPosition_ itself is written UNCONDITIONALLY above the gate, so the
    /// FR-086 accessor tracks the tide even when the matrix is not
    /// re-materialised.
    void updateMorph() noexcept {
        const float tide = tideDepth_ * tide_.getCurrentValue();
        const float t = std::clamp(dimSm_.getCurrentValue() + tide, 0.0f, 1.0f);
        morphPosition_ = t;
        if (!prepared_) {
            return;
        }
        if (std::abs(t - lastMorphPosition_) <= kMorphEpsilon) {
            return;
        }
        lastMorphPosition_ = t;
        morph_.evaluate(t, matrix_);
        orthogonalityError_ = detail::aetherOrthoErrorFloat(matrix_, numChannels_);
    }

    // -------------------------------------------------------------------------
    // Density push (FR-040 - FR-042, plan S6.2 step 7)
    // -------------------------------------------------------------------------

    /// @brief Forward the control-grid Size and Density to the input diffuser.
    ///
    /// snapSmoothers() (processors/diffusion_network.h:361-369) is MANDATORY,
    /// not tidiness: the network's own doc block (:347-360) states that a caller
    /// which already smooths on its own control grid must call it. Without it
    /// nothing inside the network ever settles, its static fast path (:534,
    /// :550) is permanently defeated, and a second 10 ms lag sits in series with
    /// ours. The static path is also what makes DiffusionNetwork slice-length
    /// independent, which SC-011 depends on.
    void updateDiffuser() noexcept {
        if (!prepared_) {
            return;
        }
        diffuser_.setSize(sizeCombined_ * 100.0f);
        diffuser_.setDensity(std::clamp(densitySm_.getCurrentValue(), 0.0f, 1.0f) * 100.0f);
        diffuser_.snapSmoothers();
    }

    // -------------------------------------------------------------------------
    // Block-rate scalars (FR-019, FR-081, plan S6.2 step 11, delta D-3)
    // -------------------------------------------------------------------------

    /// @brief Snapshot the values the render loop holds constant for the chunk.
    ///
    /// FR-009's table calls width, mix and pre-delay "per sample"; FR-019 and
    /// FR-081 require block-rate values to be snapshotted per sub-block. Plan
    /// delta D-3 resolves in favour of FR-019: the gains are computed once here
    /// from the smoothers and applied per sample.
    void snapshotBlockScalars() noexcept {
        chunkWidth_ = std::clamp(widthSm_.getCurrentValue(), 0.0f, 1.0f);
        const float mix = std::clamp(mixSm_.getCurrentValue(), 0.0f, 1.0f);
        chunkDryGain_ = std::cos(mix * kHalfPi);  // equal power, fdn_reverb.h:374-377
        chunkWetGain_ = std::sin(mix * kHalfPi);
        const float ms = std::clamp(preDelaySm_.getCurrentValue(), 0.0f, kMaxPreDelayMs);
        chunkPreDelaySamples_ = ms * 0.001f * static_cast<float>(sampleRate_);
        // FR-051: kShimmerInjectionGain is exactly 1.0, so the send value is the
        // injected gain. It is written through the multiply anyway so a future
        // change of the pinned pair size cannot silently drop the normalisation.
        chunkShimmerOctGain_ =
            std::clamp(shimmerOctSm_.getCurrentValue(), 0.0f, 1.0f) * kShimmerInjectionGain;
        chunkShimmerFifthGain_ =
            std::clamp(shimmerFifthSm_.getCurrentValue(), 0.0f, 1.0f) * kShimmerInjectionGain;
    }

    // -------------------------------------------------------------------------
    // Shimmer taps (FR-050 - FR-054, FR-059, plan S6.2 step 8 / S7.9)
    // -------------------------------------------------------------------------

    /// @brief FR-059's first-order high shelf on one tap return path.
    ///
    /// y = lp + hfGain * (x - lp) with lp a one-pole lowpass at the corner, i.e.
    /// H(f) = (1 + j*g*f/fc) / (1 + j*f/fc): unity at DC, kReturnShelfHfGain at
    /// Nyquist, monotone in between, and never expansive because g < 1. One
    /// state per path, so the two shimmer legs and the bloom cannot share a
    /// filter memory and cross-modulate.
    ///
    /// kReturnShelfHfGain SHIPS AT 0, so this is the pure one-pole lowpass
    /// 1/(1 + j*f/fc) and the expression collapses to `return lp`. The multiply
    /// is written out anyway: the floor is plan R-1's tuning lever and a future
    /// re-measurement must not have to re-derive the algebra. Banner item (5c)
    /// records why no non-zero floor passes SC-006 clause 3.
    ///
    /// @param state The caller's per-path lowpass memory, updated in place.
    /// @param x     The tap return before the shelf.
    [[nodiscard]] float returnShelf(float& state, float x) const noexcept {
        return returnShelfWith(returnShelfCoeff_, state, x);
    }

    /// @brief The same shelf at an explicit corner coefficient.
    /// @note The bloom uses its own corner (kBloomShelfCornerHz, banner item
    ///       (5e)); the two shimmer legs use kReturnShelfCornerHz.
    [[nodiscard]] static float returnShelfWith(float coeff, float& state, float x) noexcept {
        const float lp = detail::flushDenormal(state + (coeff * (x - state)));
        state = lp;
        return lp + (kReturnShelfHfGain * (x - lp));
    }

    /// @brief One-pole coefficient for an FR-059 shelf at @p cornerHz.
    /// @note The corner is clamped to kReturnShelfMaxCornerFraction of the rate so
    ///       the pole stays well inside the band at every supported sample rate -
    ///       which is what keeps the 6 kHz bloom corner meaningful at 8 kHz.
    [[nodiscard]] static float shelfCoefficient(float cornerHz, float sampleRate) noexcept {
        const float corner = std::min(cornerHz, kReturnShelfMaxCornerFraction * sampleRate);
        return std::clamp(1.0f - std::exp(-kTwoPi * corner / sampleRate), 0.0f, 1.0f);
    }

    /// @brief |H(f)| of the FR-059 shelf whose lowpass coefficient is @p coeff.
    ///
    /// Exact, not approximated: H = g + (1 - g)*Hlp with
    /// Hlp(e^jw) = coeff / (1 - (1 - coeff) e^-jw), evaluated as a complex number
    /// so a future non-zero kReturnShelfHfGain stays correct (the two terms are
    /// not in phase, so magnitudes cannot simply be blended).
    ///
    /// Control-rate only - FR-058's guard evaluates it once per chunk per active
    /// resonator, never per sample.
    [[nodiscard]] float shelfMagnitude(float coeff, float freqHz) const noexcept {
        if (sampleRate_ <= 0.0) {
            return 1.0f;
        }
        const float w = kTwoPi * freqHz / static_cast<float>(sampleRate_);
        const float b = 1.0f - coeff;  // pole radius
        const float dRe = 1.0f - (b * std::cos(w));
        const float dIm = b * std::sin(w);
        const float dMagSq = std::max((dRe * dRe) + (dIm * dIm), kDenormalGuard);
        const float lpRe = (coeff * dRe) / dMagSq;
        const float lpIm = (-coeff * dIm) / dMagSq;
        const float hRe = kReturnShelfHfGain + ((1.0f - kReturnShelfHfGain) * lpRe);
        const float hIm = (1.0f - kReturnShelfHfGain) * lpIm;
        return std::sqrt((hRe * hRe) + (hIm * hIm));
    }

    /// @brief Drive both taps once per control chunk (plan S6.2 step 8, Q5).
    ///
    /// THE CADENCE IS STRUCTURAL, NOT AN OPTIMISATION. tapSumScratch_ holds the
    /// PREVIOUS chunk's 64 mono tap sums; each shifter turns those into 64
    /// samples that renderSlice injects across the chunk that is about to be
    /// rendered. Every leg therefore carries exactly kControlChunkSamples of
    /// deferral ON TOP OF its mode latency (FR-054, banner item (5)).
    ///
    /// Because this is anchored to sampleCounter_ % kControlChunkSamples == 0 and
    /// never to a caller block boundary, the shifters' internal grain and phase
    /// state does not depend on how the host partitions its blocks - which is
    /// what makes SC-011's 1e-6 invariance structural rather than hoped-for.
    ///
    /// tapSumScratch_ is NOT zeroed afterwards: renderSlice ASSIGNS (not
    /// accumulates) index `chunkIdx` for every sample it renders, so each slot
    /// is rewritten exactly once before the next boundary reads it. A caller that
    /// stops mid-chunk simply leaves the remaining slots holding the previous
    /// chunk's values until it resumes and overwrites them.
    ///
    /// @note Deliberately NOT called from refreshControlState(): that helper also
    ///       runs from reset(), and driving a pitch shifter from reset() would
    ///       consume 64 samples of its state on a path that is supposed to
    ///       restore it.
    void updateShimmerTaps() noexcept {
        if (!shimmerAllocated_) {
            return;
        }
        shifterOctave_.process(tapSumScratch_.data(), shimmerOutOctave_.data(),
                               kControlChunkSamples);
        shifterFifth_.process(tapSumScratch_.data(), shimmerOutFifth_.data(),
                              kControlChunkSamples);
    }

    // -------------------------------------------------------------------------
    // Harmonic bloom (FR-055 - FR-059, C-7, Q1, Q7, plan S7.10)
    // -------------------------------------------------------------------------

    /// @brief Free a slot outright. Only ever called on a slot whose ring-down
    ///        has already fallen below kBloomReclaimThresholdLinear (-96 dB), so
    ///        it is silent by construction.
    void clearBloomSlot(std::size_t k) noexcept {
        bloomY1_[k] = 0.0f;
        bloomY2_[k] = 0.0f;
        bloomCoeff_[k] = 0.0f;
        bloomRSq_[k] = 0.0f;
        bloomGain_[k] = 0.0f;
        bloomEnv_[k] = 0.0f;
        bloomFreq_[k] = 0.0f;
        bloomOwner_[k] = kBloomNoOwner;
        bloomDriven_[k] = false;
    }

    /// @brief Acquire a resonator slot: a free one first, else the QUIETEST slot
    ///        that is already ringing down.
    ///
    /// The three slot states are encoded in the arrays the plan already lists,
    /// with no fourth array: `bloomFreq_ == 0` means free, `bloomDriven_` means
    /// driven, and `bloomFreq_ > 0 && !bloomDriven_` means releasing. Every
    /// allocated frequency is >= kBloomMinFreqHz, so the encoding is unambiguous.
    ///
    /// @return the slot index, or -1 if every slot is driven.
    [[nodiscard]] int acquireBloomSlot() noexcept {
        for (int k = 0; k < kMaxBloomResonators; ++k) {
            if (bloomFreq_[static_cast<std::size_t>(k)] == 0.0f) {
                return k;
            }
        }
        int best = -1;
        float bestEnv = 0.0f;
        for (int k = 0; k < kMaxBloomResonators; ++k) {
            const auto idx = static_cast<std::size_t>(k);
            if (bloomDriven_[idx]) {
                continue;
            }
            if ((best < 0) || (bloomEnv_[idx] < bestEnv)) {
                best = k;
                bestEnv = bloomEnv_[idx];
            }
        }
        if (best >= 0) {
            clearBloomSlot(static_cast<std::size_t>(best));
        }
        return best;
    }

    /// @brief Release every slot owned by @p voiceId and free the voice entry.
    ///
    /// Gain to zero and driven to false; the COEFFICIENTS AND THE STATE ARE LEFT
    /// ALONE, which is the whole of FR-056's click-free retirement: r < 1, so the
    /// resonator rings down on its own and the control-grid reclaim pass frees the
    /// slot when the ring has passed -96 dB.
    void releaseBloomVoice(std::int32_t voiceId) noexcept {
        if (voiceId < 0) {
            return;
        }
        for (std::size_t v = 0; v < kMaxBloomVoices; ++v) {
            if (bloomVoiceId_[v] == voiceId) {
                bloomVoiceId_[v] = kBloomNoOwner;
                bloomVoiceAge_[v] = 0u;
            }
        }
        for (std::size_t k = 0; k < static_cast<std::size_t>(kMaxBloomResonators); ++k) {
            if (bloomDriven_[k] && (bloomOwner_[k] == voiceId)) {
                bloomDriven_[k] = false;
                bloomOwner_[k] = kBloomNoOwner;
                bloomGain_[k] = 0.0f;
            }
        }
    }

    /// @brief FR-057's Q mapping: 0..1 -> Q = kBloomQMin * 20^v, i.e. [20, 400].
    [[nodiscard]] float currentBloomQ() const noexcept {
        const float v = std::clamp(bloomDecaySm_.getCurrentValue(), 0.0f, 1.0f);
        return kBloomQMin * std::pow(kBloomQMax / kBloomQMin, v);
    }

    /// @brief FR-058 factor 1: the inverse of a driven second-order resonator's
    ///        peak gain at its own resonance.
    ///
    /// Re-derived from systems/sympathetic_resonance.h:397-420 (the original is a
    /// PRIVATE static below `:383` and is not reachable):
    ///   peakInv = (1 - r) * sqrt(1 - 2r*cos(2w) + r^2),
    /// with cos(2w) recovered from the stored coefficient by the double-angle
    /// identity, cos(w) = coeff / (2r). kDenormalGuard is core/audio_constants.h:40.
    [[nodiscard]] static float bloomPeakGainInverse(float coeff, float rSquared) noexcept {
        const float r = std::sqrt(rSquared);
        const float oneMinusR = 1.0f - r;
        if (oneMinusR < kDenormalGuard) {
            return 1e-6f;  // degenerate: r ~= 1
        }
        const float cosOmega = (r > kDenormalGuard) ? (coeff / (2.0f * r)) : 1.0f;
        const float cos2Omega = (2.0f * cosOmega * cosOmega) - 1.0f;
        float inner = 1.0f - (2.0f * r * cos2Omega) + rSquared;
        if (inner < kDenormalGuard) {
            inner = kDenormalGuard;
        }
        return oneMinusR * std::sqrt(inner);
    }

    /// @brief FR-057's coefficients for one slot at Q @p q.
    /// @pre bloomFreq_[k] is finite and already clamped into
    ///      [kBloomMinFreqHz, kBloomMaxFreqFraction * sampleRate] - bloomNoteOn
    ///      does that BEFORE calling here, which is what makes edge case 28 a
    ///      structural property rather than a downstream check.
    void computeBloomSlot(std::size_t k, float q) noexcept {
        const auto sr = static_cast<float>(sampleRate_);
        const float f = bloomFreq_[k];
        // Q_eff = Q * clamp(500/f, 0.5, 1) - sympathetic_resonance.h:440-446.
        const float qScale = std::clamp(kBloomQFreqRef / f, kBloomMinQScale, 1.0f);
        const float qEff = std::max(q * qScale, 1.0f);
        const float r = std::exp(-kPi * (f / qEff) / sr);
        const float omega = kTwoPi * f / sr;
        const float coeff = 2.0f * r * std::cos(omega);
        const float rSq = r * r;
        bloomCoeff_[k] = coeff;
        bloomRSq_[k] = rSq;
        bloomGain_[k] = bloomPeakGainInverse(coeff, rSq);
    }

    /// @brief |T_pk|: the RMS magnitude of the FDN's modal peaks, from the bloom's
    ///        injection node back to the tap sum the bloom reads.
    ///
    /// `sqrt(K) / sum_i (1 - g_i^2)` with K the number of injected channels and
    /// g_i the Jot per-line gains. Banner item (5f) derives it; the two things it
    /// already contains, and which callers must therefore NOT multiply in again,
    /// are kTapReadNormalisation and the per-line gain.
    ///
    /// Control-rate only. Floored at the direct-arrival magnitude
    /// `kTapReadNormalisation * sqrt(K)` so a very lossy network (short decay,
    /// small Size) cannot make the guard claim the FDN attenuates the return below
    /// its own first pass.
    [[nodiscard]] float fdnRecirculationGain() const noexcept {
        float dissipation = 0.0f;
        float injectCount = 0.0f;
        for (std::size_t i = 0; i < numChannels_; ++i) {
            const float g = feedbackGain_[i];
            dissipation += 1.0f - (g * g);
            injectCount += (bloomInjectMask_[i] > 0.0f) ? 1.0f : 0.0f;
        }
        const float rootK = std::sqrt(std::max(injectCount, 1.0f));
        const float modal = rootK / std::max(dissipation, kDenormalGuard);
        return std::max(modal, kTapReadNormalisation * rootK);
    }

    /// @brief Recompute the counts, the FR-058 guard and this chunk's return gain.
    ///
    /// Called from bloomNoteOn / bloomNoteOff (so a note taken between blocks is
    /// audible in the very next sample) and from updateBloom() on the control grid.
    ///
    /// TWO COUNTS, DELIBERATELY:
    ///  - `bloomActiveCount_` counts DRIVEN slots. That is what
    ///    getActiveBloomResonatorCount() reports, so it drops on note-off, which
    ///    is what SC-016 clause 3's release assertion needs.
    ///  - the local `allocated` counts driven PLUS still-ringing slots, and it is
    ///    what the 1/sqrt(count) normalisation and the guard use. FR-058 words the
    ///    normalisation as `count = getActiveBloomResonatorCount()`, and the two
    ///    agree exactly whenever the bank is not mid-ring-down. They differ ONLY
    ///    while released resonators are still sounding, where using the driven
    ///    count would take 1/sqrt(count) to 1/sqrt(0) = 0 and HARD-CUT a live
    ///    return - the click FR-056's "rings down naturally" forbids. Using the
    ///    larger count is also the conservative direction for the guard.
    void refreshBloomDerived() noexcept {
        std::size_t driven = 0;
        std::size_t allocated = 0;
        for (std::size_t k = 0; k < static_cast<std::size_t>(kMaxBloomResonators); ++k) {
            if (bloomDriven_[k]) {
                ++driven;
            }
            if (bloomFreq_[k] > 0.0f) {
                ++allocated;
            }
        }
        bloomActiveCount_ = driven;
        bloomBankLive_ = bloomEnabled_ && (allocated > 0u);
        bloomInvSqrtCount_ =
            (allocated > 0u) ? (1.0f / std::sqrt(static_cast<float>(allocated))) : 0.0f;

        if (!bloomBankLive_) {
            bloomGuardScale_ = 1.0f;
            chunkBloomGain_ = 0.0f;
            chunkBloomEmphasisGain_ = 0.0f;
            return;
        }

        const float send01 = std::clamp(bloomSendSm_.getCurrentValue(), 0.0f, 1.0f);
        // Banner item (5g): the out-of-loop emphasis return. It carries FR-058
        // factor 1 (the per-resonator peak-gain inverse, already inside
        // bloomGain_[k]) and factor 2 (1/sqrt(count)), so its level is
        // independent of how many partials are held - but not bloomGuardScale_,
        // which bounds a loop this path is not part of.
        chunkBloomEmphasisGain_ = send01 * kBloomEmphasisGain * bloomInvSqrtCount_;

        const float send = send01 * kBloomSendMax;
        const float base = send * bloomInjectionGain_ * bloomInvSqrtCount_;

        // FR-058's computable criterion, CORRECTED for the fact that the loop the
        // bloom closes runs THROUGH the FDN. Banner item (5f) has the derivation
        // and the measured evidence; the short version is that the stated model
        // treats the injection-node-to-tap-node transfer as unity, and the real
        // one is the FDN's narrowband recirculation gain, 6.8 at SC-016's own
        // configuration. Two terms are evaluated and the LARGER binds:
        //
        //   (a) the literal FR-058 product - kTapReadNormalisation, the worst Jot
        //       per-line gain of the injected channels, the shelf - kept verbatim
        //       so the stated criterion is still enforced;
        //   (b) the same return gain against |T_pk| = sqrt(K)/sum_i(1 - g_i^2).
        //       kTapReadNormalisation and the per-line gain are deliberately NOT
        //       re-applied here: both are already inside |T_pk|'s derivation.
        float gLine = 0.0f;
        for (std::size_t i = 0; i < numChannels_; ++i) {
            if (bloomInjectMask_[i] > 0.0f) {
                gLine = std::max(gLine, feedbackGain_[i]);
            }
        }
        const float fdnGain = fdnRecirculationGain();
        float worst = 0.0f;
        for (std::size_t k = 0; k < static_cast<std::size_t>(kMaxBloomResonators); ++k) {
            if (bloomFreq_[k] <= 0.0f) {
                continue;
            }
            const float shelf = shelfMagnitude(bloomShelfCoeff_, bloomFreq_[k]);
            const float stated = kTapReadNormalisation * base * shelf * gLine;
            const float recirculating = base * shelf * fdnGain;
            worst = std::max(worst, std::max(stated, recirculating));
        }
        bloomGuardScale_ =
            (worst > kBloomLoopGainCeiling) ? (kBloomLoopGainCeiling / worst) : 1.0f;
        chunkBloomGain_ = base * bloomGuardScale_;
    }

    /// @brief Control-grid bloom pass (plan S6.2 step 9).
    ///
    /// The reclaim loop is systems/sympathetic_resonance.h:337-352 MOVED TO
    /// CONTROL RATE: that class runs it per sample after the SIMD call, which is a
    /// per-resonator branch inside the hot path and would defeat the vectorised
    /// loop here. At 64-sample granularity a slot is freed at most 64 samples late,
    /// and it is already 96 dB down when it qualifies.
    void updateBloom() noexcept {
        if (!prepared_ || !bloomEnabled_) {
            return;
        }
        for (std::size_t k = 0; k < static_cast<std::size_t>(kMaxBloomResonators); ++k) {
            if (!bloomDriven_[k] && (bloomFreq_[k] > 0.0f) &&
                (bloomEnv_[k] < kBloomReclaimThresholdLinear)) {
                clearBloomSlot(k);
            }
        }

        // FR-057: retune the DRIVEN slots when setBloomDecay has moved. Releasing
        // slots keep the coefficients they were released with, so a decay change
        // cannot put a step on a ring-down.
        const float q = currentBloomQ();
        if (std::abs(q - lastBloomQ_) > kJotRecomputeEpsilon) {
            lastBloomQ_ = q;
            for (std::size_t k = 0; k < static_cast<std::size_t>(kMaxBloomResonators); ++k) {
                if (bloomDriven_[k]) {
                    computeBloomSlot(k, q);
                }
            }
        }

        refreshBloomDerived();
    }

    /// @brief Plan S6.2 steps 4, 5, 6, 7 and 11 - everything derived from the
    ///        smoothers' current values. Called from runControlStep() and from
    ///        reset(), so a post-reset render starts fully materialised.
    ///
    /// FR-034: WHILE FROZEN, STEPS 4 AND 5 ARE SKIPPED ENTIRELY. setSize,
    /// setDecaySeconds and setDamping are still accepted and still drive their
    /// smoothers - the values are stored - but effectiveDelay_, feedbackGain_
    /// and dampCoeff_ keep their latched values until the freeze is released. A
    /// size change is the one operation that cannot be lossless (C-4), and the
    /// per-line gains are crossfaded to unity anyway (FR-033 step 6), so
    /// recomputing either under freeze could only break SC-002.
    ///
    /// Steps 6, 7 and 11 DO run: the matrix morph is the only motion freeze
    /// leaves alive (RA-5), and width/mix/pre-delay are output-stage controls
    /// outside the recirculating loop.
    void refreshControlState() noexcept {
        if (!freezeTarget_) {
            updateGeometry();         // step 4
            updateDecayAndDamping();  // step 5
        }
        updateMorph();           // step 6
        updateDiffuser();        // step 7
        snapshotBlockScalars();  // step 11
    }

    // -------------------------------------------------------------------------
    // The amortized state clear (plan S5.3, S7.14) - shared by silence() and
    // emergencyClear(). Both are real-time safe and allocate nothing.
    // -------------------------------------------------------------------------

    /// Number of deferred sub-object reset stages, one per control chunk.
    /// The ORDER is the one plan S5.3 fixes; the count must match runClearStage.
    ///
    /// ONE DELIBERATE OMISSION FROM PLAN S5.3'S LIST: dryAlignL_/dryAlignR_ are
    /// NOT reset (which is why this is 12 and not 14). Those two lines carry
    /// INPUT HISTORY, not engine state - FR-062 uses them only to delay the dry
    /// signal by the spectral stage's fftSize - and nothing recirculates through
    /// them, so leaving them alone cannot preserve any of the state silence()
    /// exists to discard. Resetting them WOULD punch an fftSize-long hole in the
    /// dry path that ends in a step of full input amplitude (the line reads 0
    /// until fftSize new samples have been written, then jumps to the sample
    /// written at the reset instant) - roughly 21 ms after the reset at the
    /// default, i.e. AFTER the 20 ms gate has already returned to unity, so the
    /// step is not masked. That is exactly the single-sample discontinuity
    /// SC-015 forbids and FR-007 promises not to produce.
    static constexpr std::size_t kClearStageCount = 12;

    /// @brief The O(N)- and O(kMaxBloomResonators)-sized scalar loop state.
    ///
    /// A few hundred floats, so it is cleared IMMEDIATELY rather than amortized
    /// - and it is where a detected non-finite value lives, which is why
    /// emergencyClear() must run it before anything else.
    void clearScalarLoopState() noexcept {
        for (std::size_t i = 0; i < kMaxChannels; ++i) {
            writePos_[i] = 0;
            filterState_[i] = 0.0f;
            dcBlockX_[i] = 0.0f;
            dcBlockY_[i] = 0.0f;
            chanIn_[i] = 0.0f;
            chanOut_[i] = 0.0f;
        }
        // Empty vectors when the shimmer taps are force-disabled (RA-6); a fill
        // over an empty range is well-defined and free.
        std::fill(tapSumScratch_.begin(), tapSumScratch_.end(), 0.0f);
        std::fill(shimmerOutOctave_.begin(), shimmerOutOctave_.end(), 0.0f);
        std::fill(shimmerOutFifth_.begin(), shimmerOutFifth_.end(), 0.0f);
        shimmerShelfStateOct_ = 0.0f;
        shimmerShelfStateFifth_ = 0.0f;
        bloomShelfState_ = 0.0f;
    }

    /// @brief The whole bloom bank, including the voice slots (plan S5.3 step 1).
    /// @note refreshBloomDerived() is what puts getActiveBloomResonatorCount(),
    ///       bloomBankLive_ and the two chunk gains back in agreement with the
    ///       zeroed arrays; writing them by hand here would be a second source
    ///       of truth.
    void clearBloomBankState() noexcept {
        for (std::size_t k = 0; k < static_cast<std::size_t>(kMaxBloomResonators); ++k) {
            bloomY1_[k] = 0.0f;
            bloomY2_[k] = 0.0f;
            bloomCoeff_[k] = 0.0f;
            bloomRSq_[k] = 0.0f;
            bloomGain_[k] = 0.0f;
            bloomEnv_[k] = 0.0f;
            bloomFreq_[k] = 0.0f;
            bloomOwner_[k] = kBloomNoOwner;
            bloomDriven_[k] = false;
        }
        for (std::size_t v = 0; v < kMaxBloomVoices; ++v) {
            bloomVoiceId_[v] = kBloomNoOwner;
            bloomVoiceAge_[v] = 0u;
        }
        bloomAgeCounter_ = 0u;
        lastBloomQ_ = -1.0f;
        refreshBloomDerived();
    }

    /// @brief One deferred sub-object reset. Each is at least an order of
    ///        magnitude smaller than delayBuffer_ and cannot be split, so one
    ///        per control chunk is the granularity.
    void runClearStage(std::size_t stage) noexcept {
        switch (stage) {
            case 0: preDelayL_.reset(); break;
            case 1: preDelayR_.reset(); break;
            case 2: diffuser_.reset(); break;
            case 3: if (spectralEnabled_) { stftL_.reset(); } break;
            case 4: if (spectralEnabled_) { stftR_.reset(); } break;
            case 5: if (spectralEnabled_) { olaL_.reset(); } break;
            case 6: if (spectralEnabled_) { olaR_.reset(); } break;
            case 7: if (spectralEnabled_) { specL_.reset(); } break;
            case 8: if (spectralEnabled_) { specR_.reset(); } break;
            case 9:
                // The wet FIFO and the warm-up counter together. Re-arming the
                // counter is what keeps getLatencySamples() honest across a
                // clear: the stage owes the caller diffusionFftSize_ literal
                // zeros again, exactly as after prepare() (FR-062, FR-084).
                std::fill(wetFifoL_.begin(), wetFifoL_.end(), 0.0f);
                std::fill(wetFifoR_.begin(), wetFifoR_.end(), 0.0f);
                wetFifoRead_ = 0;
                wetFifoWrite_ = 0;
                wetFifoCount_ = 0;
                spectralWarmupRemaining_ =
                    spectralEnabled_ ? diffusionFftSize_ : std::size_t{0};
                break;
            // dryAlignL_/R_ are deliberately absent - see kClearStageCount.
            // PitchShiftProcessor::reset() is a documented no-op when unprepared
            // (processors/pitch_shift_processor.h:150-152); the guard is about
            // not touching an object the sub-44.1 kHz path never prepared.
            case 10: if (shimmerAllocated_) { shifterOctave_.reset(); } break;
            case 11: if (shimmerAllocated_) { shifterFifth_.reset(); } break;
            default: break;
        }
    }

    /// @brief One work unit of the amortized clear: one delayBuffer_ slab AND at
    ///        most one deferred sub-object reset. Called from runControlStep().
    void advanceAmortizedClear() noexcept {
        bool bufferDone = (clearCursor_ >= delayBuffer_.size());
        if (!bufferDone) {
            const std::size_t end =
                std::min(delayBuffer_.size(), clearCursor_ + clearQuotaFloats_);
            std::fill(delayBuffer_.begin() + static_cast<std::ptrdiff_t>(clearCursor_),
                      delayBuffer_.begin() + static_cast<std::ptrdiff_t>(end), 0.0f);
            clearCursor_ = end;
            bufferDone = (clearCursor_ >= delayBuffer_.size());
        }
        if (clearStage_ < kClearStageCount) {
            runClearStage(clearStage_);
            ++clearStage_;
        }
        if (bufferDone && (clearStage_ >= kClearStageCount)) {
            // The loop kept reading the delay lines while the cursor was still
            // walking them, so filterState_ / the DC blockers hold a residue of
            // whatever had not been reached yet. One more O(N) pass costs
            // nothing and makes "cleared" mean cleared.
            clearScalarLoopState();
            clearPending_ = false;
        }
    }

    /// @brief FR-083's recovery. A REFINEMENT of FR-083's "invokes silence()"
    ///        (plan delta D-4), and documented as such here.
    ///
    /// There is NO FADE-OUT: ramping a non-finite value down over 20 ms is not
    /// possible - every intermediate product is still non-finite. The gate is
    /// SNAPPED to 0 and the engine fades back IN instead. The audible cost is a
    /// single discontinuity into silence, which is strictly better than 20 ms of
    /// NaN in the host's buffer.
    ///
    /// Phases (2) and (3) are byte-for-byte silence()'s, so the wall-clock
    /// argument in banner item (11) applies unchanged.
    void emergencyClear() noexcept {
        outputGate_.snapTo(0.0f);  // (2) - no fade-out is possible
        // FadingOut with the gate ALREADY at 0. runGateMachine() then holds the
        // gate at 0 until clearPending_ finishes and only then fades in, which
        // is the same phase ordering silence() uses - the fade-in must not run
        // concurrently with the clear, or the clear becomes audible again.
        gate_ = GateState::FadingOut;
        clearArmed_ = false;
        beginDeferredClear();  // (1), (3), (4) - immediate, the NaN lives here
    }

    /// @brief Phase 2 of FR-007, entered ONLY at gate 0 (banner item (11b)).
    void beginDeferredClear() noexcept {
        // Edge case 8: the freeze latch is abandoned, not preserved.
        freezeTarget_ = false;
        freezeRamp_.snapTo(0.0f);

        clearScalarLoopState();
        clearBloomBankState();

        clearPending_ = true;
        clearCursor_ = 0;
        clearStage_ = 0;
    }

    /// @brief FR-083's once-per-control-chunk sweep of the recirculating state.
    ///
    /// Tests filterState_[0..N) and the current matrix's diagonal - the two
    /// places a non-finite value can persist across chunks (every other loop
    /// scalar is rewritten from them each sample). On detection:
    /// emergencyClear() and ++nonFiniteRecoveries_.
    ///
    /// SUPPRESSED WHILE A CLEAR IS ALREADY IN FLIGHT, and that is load-bearing
    /// rather than an optimisation: emergencyClear() rewinds clearCursor_ and
    /// clearStage_ to 0, so re-entering it every chunk while the bulk clear is
    /// still walking the buffer would restart the clear forever and the engine
    /// would never resume. The state it would test is the state being zeroed.
    ///
    /// THIS BRANCH IS UNREACHABLE BY ANY LEGAL CALL SEQUENCE - see
    /// injectNonFiniteStateForTest() for why, and for the test hook that exists
    /// solely to reach it.
    void runNonFiniteSweep() noexcept {
        if (clearPending_) {
            return;
        }
        bool bad = false;
        for (std::size_t i = 0; i < numChannels_; ++i) {
            if (!isFinite(filterState_[i])) {
                bad = true;
                break;
            }
        }
        if (!bad) {
            for (std::size_t i = 0; i < numChannels_; ++i) {
                if (!isFinite(matrix_[(i * numChannels_) + i])) {
                    bad = true;
                    break;
                }
            }
        }
        if (bad) {
            emergencyClear();
            ++nonFiniteRecoveries_;
        }
    }

    /// @brief Plan S6.2 step 3 - the silence()/emergencyClear() gate machine.
    ///
    /// THE RAMPS ARE NOT ADVANCED HERE. LinearRamp has no advanceSamples, and a
    /// per-chunk advance would turn a 20 ms crossfade into a ~0.6 dB staircase
    /// (banner item (10)); renderSlice advances outputGate_ per sample. Only the
    /// TARGETS and the state transitions are set on the control grid, which is
    /// what keeps them anchored to sampleCounter_ (SC-011).
    ///
    /// The gate can therefore sit at 0 for up to 63 extra samples after the
    /// clear finishes. That window is silence, so it is inaudible.
    void runGateMachine() noexcept {
        if (clearPending_) {
            advanceAmortizedClear();
        }
        if (gate_ == GateState::FadingOut) {
            if (outputGate_.isComplete() && clearArmed_) {
                // The gate has just reached EXACTLY 0. Phase 2 starts HERE, not
                // at the silence() call: from this instant every state change
                // the clear makes is multiplied by 0 and therefore inaudible by
                // construction (banner item (11b)). The clear's first work unit
                // runs on the NEXT control step, which is still at gate 0.
                clearArmed_ = false;
                beginDeferredClear();
            }
            // Both conditions, per FR-007 phase 3. If the fade window is shorter
            // than the quota needs (very low sample rates), the gate simply
            // holds at 0 for the extra chunks - the sequence is defined by
            // clearPending_, not by a fixed 40 ms.
            if (outputGate_.isComplete() && !clearPending_ && !clearArmed_) {
                gate_ = GateState::FadingIn;
                outputGate_.setTarget(1.0f);
            }
        } else if (gate_ == GateState::FadingIn) {
            if (outputGate_.isComplete()) {
                gate_ = GateState::Open;
            }
        }
    }

    /// @brief One control step, run at every absolute 64-sample boundary.
    void runControlStep() noexcept {
        // Step 1. FR-074: THE LIFE MODULATORS ADVANCE UNCONDITIONALLY. There is
        // no input-activity gate anywhere in this engine - "the space engine
        // breathes at idle" (roadmap lines 71-72), and SC-017 clauses 1a/2a
        // render digital silence and require the introspection values to move.
        //
        // Each is advanced by a FULL kControlChunkSamples, never by a slice
        // length: BreathingModulator::processBlock is
        // advancePhase(n) + setTarget + advanceSamples(n)
        // (processors/breathing_modulator.h:209-216), so 36 + 28 inserts an extra
        // setTarget and is NOT the same state as one call of 64. The same trap
        // applies to TidalModulator (:250-257) and BrownianDrift (:194-206).
        // Anchoring this to sampleCounter_ % 64 == 0 is what makes SC-011's
        // partition invariance structural.
        breath_.processBlock(kControlChunkSamples);
        tide_.processBlock(kControlChunkSamples);
        for (std::size_t j = 0; j < (numChannels_ / 2u); ++j) {
            drift_[j].processBlock(kControlChunkSamples);
        }

        // Step 2. EVERY OnePoleSmoother is advanced by a FULL control chunk,
        // never by a slice length (banner item (10)). spectralSm_ is excluded:
        // FR-064 advances it by advanceSamples(hopSize_) per STFT frame.
        sizeSm_.advanceSamples(kControlChunkSamples);
        densitySm_.advanceSamples(kControlChunkSamples);
        decaySm_.advanceSamples(kControlChunkSamples);
        dimSm_.advanceSamples(kControlChunkSamples);
        dampSm_.advanceSamples(kControlChunkSamples);
        preDelaySm_.advanceSamples(kControlChunkSamples);
        modDepthSm_.advanceSamples(kControlChunkSamples);
        shimmerOctSm_.advanceSamples(kControlChunkSamples);
        shimmerFifthSm_.advanceSamples(kControlChunkSamples);
        bloomSendSm_.advanceSamples(kControlChunkSamples);
        bloomDecaySm_.advanceSamples(kControlChunkSamples);
        widthSm_.advanceSamples(kControlChunkSamples);
        mixSm_.advanceSamples(kControlChunkSamples);

        // Step 3. The silence() / emergencyClear() gate machine, and one work
        // unit of the amortized bulk clear (plan S5.3, S6.2 step 3).
        runGateMachine();

        refreshControlState();

        // Step 8. The shimmer chunk boundary. AFTER refreshControlState() only
        // because that helper is shared with reset() (see updateShimmerTaps);
        // the two are independent, so the order carries no coupling.
        updateShimmerTaps();

        // Step 9. The bloom reclaim / retune / guard pass. STRICTLY AFTER
        // refreshControlState(): FR-058's guard reads feedbackGain_, which
        // updateDecayAndDamping() has just written for this chunk.
        updateBloom();

        // Step 10. FR-083. LAST, so it sees the matrix this chunk will actually
        // apply (step 6 re-materialises matrix_) and fires BEFORE any sample of
        // the chunk is rendered - the guarantee injectNonFiniteStateForTest()'s
        // control-chunk-boundary precondition rests on.
        runNonFiniteSweep();
    }

    // -------------------------------------------------------------------------
    // Delay-buffer access - the fdn_reverb.h:638-689 layout, re-derived (C-1)
    // -------------------------------------------------------------------------

    void delayWrite(std::size_t channel, float sample) noexcept {
        delayBuffer_[sectionOffset_[channel] + writePos_[channel]] = sample;
        writePos_[channel] = (writePos_[channel] + 1u) & sectionMask_[channel];
    }

    [[nodiscard]] float delayRead(std::size_t channel, std::size_t delaySamples) const noexcept {
        const std::size_t pos = (writePos_[channel] - 1u - delaySamples) & sectionMask_[channel];
        return delayBuffer_[sectionOffset_[channel] + pos];
    }

    /// @brief Read one line at a possibly fractional length.
    ///
    /// Integer read(size_t) when the fractional part is exactly 0 - which is
    /// the settled case at every Size whose scale lands on an integer multiple
    /// of the reference length, and unconditionally under the FR-033 freeze
    /// latch. Otherwise cubic Hermite (core/interpolation.h:84), the call
    /// fdn_reverb.h:669 makes. Every section is sized with kInterpMarginSamples
    /// of headroom so the y[-1] .. y[+2] window is always inside the line's own
    /// history.
    [[nodiscard]] float delayReadInterpolated(std::size_t channel,
                                              float delaySamples) const noexcept {
        const float floored = std::floor(delaySamples);
        const float frac = delaySamples - floored;
        const auto idx = static_cast<std::size_t>(floored);
        if (frac == 0.0f) {
            return delayRead(channel, idx);
        }
        const float ym1 = delayRead(channel, (idx > 0u) ? (idx - 1u) : 0u);
        const float y0 = delayRead(channel, idx);
        const float y1 = delayRead(channel, idx + 1u);
        const float y2 = delayRead(channel, idx + 2u);
        return Interpolation::cubicHermiteInterpolate(ym1, y0, y1, y2, frac);
    }

    /// @brief One-pole DC blocker, FR-016.
    ///
    /// R = 1 - 250/sr is FDNReverb's pole (fdn_reverb.h:207), giving the same
    /// ~40 Hz -3 dB corner at any rate. THE (1+R)/2 PREFACTOR IS AN ADDITION AND
    /// IS LOAD-BEARING: the bare difference equation y = x - x[n-1] + R*y[n-1]
    /// has |H| = 2/(1+R) > 1 for everything above ~550 Hz (+0.023 dB per pass at
    /// 48 kHz). Inside a recirculating loop that is an ENERGY SOURCE, and it
    /// breaks FR-032 outright: at decaySeconds = 60 the Jot gain of the shortest
    /// line is 0.9977, so the round-trip gain at Nyquist would be 1.0003 - a
    /// growing loop, and a measured T60 of ~105 s against a requested 60 s.
    /// Normalising by (1+R)/2 puts the peak magnitude at exactly 1.0 while
    /// leaving the pole, and therefore the corner frequency, untouched.
    [[nodiscard]] float dcBlock(std::size_t channel, float x) noexcept {
        const float y = (dcBlockGain_ * (x - dcBlockX_[channel])) + (dcBlockR_ * dcBlockY_[channel]);
        dcBlockX_[channel] = x;
        dcBlockY_[channel] = y;
        return y;
    }

    // -------------------------------------------------------------------------
    // Spectral diffusion (FR-060 - FR-065, plan S7.11, banner item (13))
    // -------------------------------------------------------------------------

    /// @brief FR-061's per-bin phase smear, REDRAWN EVERY HOP.
    ///
    /// A draw taken once and held is a static dispersive allpass: fixed
    /// colouration, no smearing over time. Redrawing decorrelates successive
    /// frames, which is the whole point of the stage. Banner item (13a) states
    /// this normatively.
    ///
    /// MAGNITUDES ARE NEVER TOUCHED. Xorshift32::nextFloat() is bipolar
    /// (core/random.h:59), so the offset is uniform on [-a*pi, +a*pi] and the
    /// stage is EXACTLY the identity at a == 0: 0.0f * finite is 0.0f, and
    /// setPhase(b, phase + 0) writes back what getPhase(b) returned. The RNG is
    /// still drawn at a == 0 so the stream position depends only on the frame
    /// count, never on the amount.
    static void smearSpectrum(SpectralBuffer& spec, Xorshift32& rng, float amount) noexcept {
        const std::size_t bins = spec.numBins();
        for (std::size_t b = 0; b < bins; ++b) {
            spec.setPhase(b, spec.getPhase(b) + (amount * rng.nextFloat() * kPi));
        }
    }

    /// @brief The g(a) coherence make-up, banner item (13b).
    ///
    /// Five measured knots at 0.25 spacing, cubic Hermite between them with the
    /// end tangents CLAMPED (ym1 == y0 at the left end, y2 == y1 at the right),
    /// which is what keeps g(0) exactly 1.0 and g(1) exactly the last knot -
    /// cubicHermiteInterpolate returns c0 == y0 at t == 0 (core/interpolation.h:84).
    [[nodiscard]] static float coherenceMakeup(float amount) noexcept {
        const float x = std::clamp(amount, 0.0f, 1.0f) *
                        static_cast<float>(kCoherenceKnotCount - 1u);
        const float floored = std::floor(x);
        auto k = static_cast<std::size_t>(floored);
        float t = x - floored;
        if (k >= (kCoherenceKnotCount - 1u)) {
            k = kCoherenceKnotCount - 2u;
            t = 1.0f;
        }
        const float ym1 = kCoherenceMakeup[(k > 0u) ? (k - 1u) : 0u];
        const float y0 = kCoherenceMakeup[k];
        const float y1 = kCoherenceMakeup[k + 1u];
        const float y2 =
            kCoherenceMakeup[((k + 2u) < kCoherenceKnotCount) ? (k + 2u) : (kCoherenceKnotCount - 1u)];
        return Interpolation::cubicHermiteInterpolate(ym1, y0, y1, y2, t);
    }

    /// @brief One slice through the STFT -> smear -> OverlapAdd pump (plan S7.11).
    ///
    /// Reads and REWRITES wetScratchL_/R_[0, slice), and runs the dry through
    /// dryAlignL_/R_ so both paths carry the same fftSize.
    ///
    /// The pump is self-balancing and therefore partition-invariant (SC-011):
    /// STFT::analyze consumes exactly hopSize (primitives/stft.h:171) and
    /// OverlapAdd::synthesize marks exactly hopSize ready (:311), so the number of
    /// frames produced depends only on the TOTAL number of samples pushed, never
    /// on how the caller split them.
    void runSpectralStage(std::size_t slice) noexcept {
        // The dry alignment runs first and unconditionally over the slice: it is a
        // plain delay line and must see every sample exactly once.
        for (std::size_t k = 0; k < slice; ++k) {
            dryAlignL_.write(dryScratchL_[k]);
            dryAlignR_.write(dryScratchR_[k]);
            dryScratchL_[k] = dryAlignL_.read(diffusionFftSize_);
            dryScratchR_[k] = dryAlignR_.read(diffusionFftSize_);
        }

        stftL_.pushSamples(wetScratchL_.data(), slice);
        stftR_.pushSamples(wetScratchR_.data(), slice);

        const std::size_t hop = diffusionHopSize_;
        const std::size_t ring = wetFifoL_.size();
        while (stftL_.canAnalyze()) {
            // FR-064's cadence: spectralSm_ is the ONE OnePoleSmoother advanced
            // per FRAME rather than per control chunk. Advancing it once per
            // process() call would stretch its 100 ms constant to ~25 s at the
            // default hop (banner item (10)).
            spectralSm_.advanceSamples(hop);
            const float amount = std::clamp(spectralSm_.getCurrentValue(), 0.0f, 1.0f);

            stftL_.analyze(specL_);
            stftR_.analyze(specR_);
            smearSpectrum(specL_, smearRngL_, amount);
            smearSpectrum(specR_, smearRngR_, amount);
            olaL_.synthesize(specL_);
            olaR_.synthesize(specR_);

            // Defensive only: the occupancy never exceeds hop + one slice, and
            // the ring is 2 * fftSize == 8 * hop. If it ever did, dropping the
            // OLDEST samples is the one behaviour that cannot read unwritten
            // memory or run the read cursor past the write cursor.
            if ((wetFifoCount_ + hop) > ring) {
                const std::size_t drop = (wetFifoCount_ + hop) - ring;
                wetFifoRead_ = (wetFifoRead_ + drop) & wetFifoMask_;
                wetFifoCount_ -= drop;
            }

            // wetFifoWrite_ is always a multiple of hop and the ring is an exact
            // multiple of hop, so this destination is hop contiguous floats and
            // the pull never wraps.
            float* dstL = wetFifoL_.data() + wetFifoWrite_;
            float* dstR = wetFifoR_.data() + wetFifoWrite_;
            olaL_.pullSamples(dstL, hop);
            olaR_.pullSamples(dstR, hop);

            // g(a) is applied HERE, on the pulled time-domain samples, never on
            // the bins - which is what makes FR-061's "magnitudes are never
            // modified" true literally rather than approximately.
            const float g = coherenceMakeup(amount);
            for (std::size_t i = 0; i < hop; ++i) {
                dstL[i] *= g;
                dstR[i] *= g;
            }

            wetFifoWrite_ = (wetFifoWrite_ + hop) & wetFifoMask_;
            wetFifoCount_ += hop;
        }

        // The FIFO underflow rule (banner item (13c)): emit literal 0.0f for the
        // first spectralWarmupRemaining_ output samples and DO NOT touch the read
        // cursor while doing so. That zero-fill IS the fftSize offset the dry path
        // was just aligned to.
        //
        // THE COUNTER IS THE RULE, NOT "emit zeros whenever the FIFO happens to be
        // empty" - and that difference is load-bearing, not defensive. Draining as
        // soon as anything is available makes the offset a function of the CALLER'S
        // BLOCK SIZE: the pump produces its first hop inside the same slice whose
        // push crosses fftSize, so the zero count comes out as
        // (ceil(fftSize / slice) - 1) * slice. At fftSize = 1024 that is 960 zeros
        // at slice 64, 1020 at slice 30 and 1022 at slice 7 - never fftSize, and
        // never the same twice. getLatencySamples() would then be a lie in every
        // host, SC-011's partition invariance would fail, and SC-018 clause 5's
        // "the first getLatencySamples() samples are exactly 0.0f" would be
        // unreachable. Counting down from diffusionFftSize_ makes the offset
        // EXACTLY fftSize for every partition.
        //
        // After the warm-up the FIFO can never run dry: by output index n the pump
        // has been given n + 1 samples, which is at least
        // ceil((n - fftSize + 1) / hop) * hop + fftSize - hop, i.e. one more
        // reconstructed sample than has been consumed. The else-branch below is
        // therefore unreachable in a prepared engine and exists so that a future
        // cadence change degrades to silence rather than to unwritten memory.
        for (std::size_t k = 0; k < slice; ++k) {
            if (spectralWarmupRemaining_ > 0u) {
                --spectralWarmupRemaining_;
                wetScratchL_[k] = 0.0f;
                wetScratchR_[k] = 0.0f;
            } else if (wetFifoCount_ > 0u) {
                wetScratchL_[k] = wetFifoL_[wetFifoRead_];
                wetScratchR_[k] = wetFifoR_[wetFifoRead_];
                wetFifoRead_ = (wetFifoRead_ + 1u) & wetFifoMask_;
                --wetFifoCount_;
            } else {
                wetScratchL_[k] = 0.0f;
                wetScratchR_[k] = 0.0f;
            }
        }
    }

    // -------------------------------------------------------------------------
    // renderSlice (plan S6.3) - order is normative
    // -------------------------------------------------------------------------

    /// @brief Render up to kControlChunkSamples samples inside one control chunk.
    /// @param baseIndex Absolute sample index of slice sample 0, so anything
    ///        derived from sample position stays partition-invariant (SC-011).
    /// @note In-place safe: the slice's input is copied into preScratch* and
    ///       dryScratch* before anything is written to out*.
    void renderSlice(const float* inL, const float* inR, float* outL, float* outR,
                     std::size_t slice, std::uint64_t baseIndex) noexcept {
        const std::size_t n = numChannels_;

        // --- A: FR-082 input finiteness guard (fdn_reverb.h:264-265) ---------
        // A replacement, never a counter increment: SC-014 clause 2 requires
        // this path to leave getNonFiniteRecoveryCount() at 0.
        for (std::size_t k = 0; k < slice; ++k) {
            const float l = inL[k];
            const float r = inR[k];
            preScratchL_[k] = isFinite(l) ? l : 0.0f;
            preScratchR_[k] = isFinite(r) ? r : 0.0f;
            dryScratchL_[k] = preScratchL_[k];
            dryScratchR_[k] = preScratchR_[k];
        }

        // --- B: stereo pre-delay, the SAME smoothed length on both channels
        //        so the pair stays phase-coherent (FR-015) ------------------
        for (std::size_t k = 0; k < slice; ++k) {
            preDelayL_.write(preScratchL_[k]);
            preDelayR_.write(preScratchR_[k]);
            preScratchL_[k] = preDelayL_.readLinear(chunkPreDelaySamples_);
            preScratchR_[k] = preDelayR_.readLinear(chunkPreDelaySamples_);
        }

        // --- C: Density (FR-040, FR-043) -------------------------------------
        diffuser_.process(preScratchL_.data(), preScratchR_.data(), diffScratchL_.data(),
                          diffScratchR_.data(), slice);

        alignas(32) float delRead[kMaxChannels]{};

        // outputGate_ is advanced per sample in the loop below but consumed in
        // step F, after the width and spectral stages have rewritten the wet
        // slice - so the per-sample values are parked here rather than folded
        // in early. 64 floats of stack; slice <= kControlChunkSamples always
        // (plan S6.1).
        alignas(32) float gateScratch[kControlChunkSamples]{};

        // While a silence() / emergencyClear() bulk clear is in flight the loop
        // writes LITERAL 0.0f into the delay lines and forces the wet
        // contribution to LITERAL 0.0f (plan S5.3, S7.14). ASSIGNMENTS, NOT
        // `x * 0` PRODUCTS: a delayBuffer_ region the cursor has not reached yet
        // may still hold a non-finite value, and NaN * 0 is NaN. clearPending_
        // changes only at a control step, so hoisting it out of the sample loop
        // is exact, not an approximation.
        const bool clearing = clearPending_;

        // Position of slice sample 0 inside the current control chunk. Plan S6.1
        // guarantees a slice never crosses a chunk boundary, so chunkBase + k is
        // always in [0, kControlChunkSamples) and indexes both the tap-sum
        // scratch and the two one-chunk-late shimmer returns.
        const auto chunkBase = static_cast<std::size_t>(baseIndex % kControlChunkSamples);

        for (std::size_t k = 0; k < slice; ++k) {
            // --- 0: THE FREEZE RAMP IS ADVANCED PER SAMPLE (FR-033) -------
            // LinearRamp has no advanceSamples (that method exists only on
            // OnePoleSmoother, primitives/smoother.h:243) and RA-1 forbids
            // adding one - but the per-sample cadence is also required on its
            // own merits: this coefficient moves the delay READ POINTER (step
            // 1) and the per-line LOOP GAIN (step 9), so a per-chunk snapshot
            // would step both once every 64 samples, ~2.7 % per step over the
            // 50 ms window. That is the staircase SC-015's zero-detection
            // requirement forbids. process() is a pure per-sample recurrence
            // with no block-boundary state (:370-389), so the value at absolute
            // sample n is a function of n alone and SC-011's partition
            // invariance is unaffected.
            //
            // outputGate_ rides the same per-sample cadence, for the same
            // reason: a 20 ms fade stepped once per 64 samples is a ~0.6 dB
            // staircase, which SC-015's zero-click requirement forbids.
            const float freezeRamp = freezeRamp_.process();
            // FR-007's gate is SHAPED, not linear (banner item (11c)). LinearRamp
            // is C0 only: its value has a slope corner at each end of the ramp,
            // and a corner in an envelope is a STEP in the derivative of the
            // signal it multiplies. ClickDetector measures exactly that
            // derivative, and in the near-silent frames around gate 0 the frame
            // sigma collapses, so a corner that is inaudible is still a
            // detection. MEASURED on SC-015's transition render with the linear
            // gate: 105.0190 s (|dy| 0.0034, the gate reaching 0), 105.0441 and
            // 105.0486 s (0.009 / 0.0238, the fade-in leaving 0). smoothstep has
            // zero derivative at both endpoints, which removes all three, and it
            // is 2 multiplies and an add - no transcendental on the audio path.
            const float gateLinear = outputGate_.process();
            gateScratch[k] = gateLinear * gateLinear * (3.0f - (2.0f * gateLinear));
            const float unfreeze = 1.0f - freezeRamp;
            // FR-036: the tickle is an energy SOURCE, so it is switched off once
            // the loop reaches unity gain - leaving it on breaks SC-002.
            const float tickleScale = (freezeRamp < 1.0f) ? 1.0f : 0.0f;

            // --- 1: read the N delay lines. FR-033 STEP 2: the read length
            //        crossfades to the LATCHED INTEGER length, so at
            //        freezeRamp == 1 the fractional part is exactly 0 and
            //        delayReadInterpolated() takes its integer read(size_t)
            //        path. No interpolation, hence no interpolation loss -
            //        which is the whole of C-4: a moving fractional read is a
            //        time-varying lowpass, and it is what makes FDNReverb's
            //        "energy-conserving" freeze lose its top octave.
            for (std::size_t i = 0; i < n; ++i) {
                const float dynamicDelay = std::max(1.0f, effectiveDelay_[i]);
                const float d = (freezeRamp > 0.0f)
                                    ? crossfade(dynamicDelay, latchedDelay_[i], freezeRamp)
                                    : dynamicDelay;
                delRead[i] = delayReadInterpolated(i, d);
            }

            // --- 2: output taps, taken BEFORE damping
            //        (fdn_reverb.h:280-292, :356-364), even -> L, odd -> R --
            float wetL = 0.0f;
            float wetR = 0.0f;
            for (std::size_t i = 0; i < n; i += 2u) {
                wetL += delRead[i];
            }
            for (std::size_t i = 1u; i < n; i += 2u) {
                wetR += delRead[i];
            }
            wetScratchL_[k] = wetL * outputTapScale_;
            wetScratchR_[k] = wetR * outputTapScale_;

            for (std::size_t i = 0; i < n; ++i) {
                // --- 3: damping one-pole (FR-031). At damping = 0 the
                //        coefficient is exactly 1, so this is a pass-through.
                //        FR-033 STEP 3 crossfades it out under freeze - the
                //        state keeps updating so that leaving freeze is
                //        continuous, only the contribution is faded.
                const float c = dampCoeff_[i];
                filterState_[i] = (c * delRead[i]) + ((1.0f - c) * filterState_[i]);
                const float damped = crossfade(filterState_[i], delRead[i], freezeRamp);
                // --- 4: DC blocker (FR-016), crossfaded out the same way
                //        (fdn_reverb.h:311-322 bypasses it outright; here it is
                //        a ramp so both directions are click-free).
                chanOut_[i] = crossfade(dcBlock(i, damped), damped, freezeRamp);
            }

            // --- 5: the ONE mono tap sum (FR-050) -------------------------
            // The four LONGEST lines - channels [N-4, N) given FR-011's
            // ascending tables, i.e. {4,5,6,7} at N = 8 and {12,13,14,15} at
            // N = 16 - at kTapReadNormalisation = 1/4. Exactly one tap sum is
            // formed per sample; the harmonic bloom reads this same value
            // (FR-055) rather than forming a second one.
            float tapSum = 0.0f;
            for (std::size_t i = n - kTapReadCount; i < n; ++i) {
                tapSum += chanOut_[i];
            }
            tapSum *= kTapReadNormalisation;

            // --- 6: the harmonic bloom (FR-055 - FR-059) -------------------
            // The REUSED Layer 3 free-function kernel over Aether-owned plain
            // arrays (systems/sympathetic_resonance_simd.h:39-50), called with
            // count = kMaxBloomResonators so the vectorised loop never branches
            // per resonator; unused slots hold coeff = gain = y1 = y2 = 0 and
            // contribute exactly nothing, which is how
            // SympatheticResonance::process drives it too (:326-333).
            //
            // bloomBankLive_ is a CONTROL-CHUNK-ANCHORED snapshot (it changes
            // only in refreshBloomDerived(), i.e. from the control step or from
            // a note call between blocks), so gating on it is partition-
            // invariant. It is a pure cost gate, never a behaviour change: with
            // every slot free the kernel writes y = 0 into an already-zero state
            // and env = max(0, 0*release) = 0, so skipping it is bit-identical
            // and keeps the 32-lane cost off every configuration that holds no
            // chord (SC-006, SC-011 and SC-012 all enable the bloom and never
            // call bloomNoteOn).
            float bloomRet = 0.0f;
            if (bloomBankLive_) {
                float bloomOut = 0.0f;
                processSympatheticBankSIMD(bloomY1_, bloomY2_, bloomCoeff_, bloomRSq_, bloomGain_,
                                           kMaxBloomResonators, tapSum, &bloomOut,
                                           bloomReleaseCoeff_, bloomEnv_);
                // ONE shelf, ONE state, TWO consumers (FR-059): the in-loop
                // injection below and the out-of-loop emphasis return of banner
                // item (5g). Calling returnShelfWith twice would advance the
                // one-pole twice per sample and halve its corner.
                const float shelved =
                    returnShelfWith(bloomShelfCoeff_, bloomShelfState_, bloomOut);
                // FR-033 step 5 mutes the bloom for the freeze's duration, the
                // same (1 - freezeRamp) the two shimmer legs carry (RA-5). BOTH
                // return paths ride it, so "freeze mutes all three sends"
                // (SC-016 clause 4) stays true of the whole bloom stage.
                bloomRet = unfreeze * chunkBloomGain_ * shelved;
                // Banner item (5g). Summed into the wet bus, equally into both
                // channels - the bank is mono (it reads the ONE mono tap sum),
                // so it is pure MID and step D's M/S width leaves it alone.
                const float emphasis = unfreeze * chunkBloomEmphasisGain_ * shelved;
                wetScratchL_[k] += emphasis;
                wetScratchR_[k] += emphasis;
            }

            // --- 7: the two shimmer returns --------------------------------
            // shimmerOut*[chunkIdx] is what updateShimmerTaps() produced at the
            // TOP of this chunk from the PREVIOUS chunk's tap sums, hence the
            // 64-sample deferral FR-054's loop-time table adds to every mode.
            // Writing this chunk's tapSum into the same index is safe: the
            // shifter has already consumed the slot.
            float octRet = 0.0f;
            float fifthRet = 0.0f;
            if (shimmerAllocated_) {
                const std::size_t chunkIdx = chunkBase + k;
                tapSumScratch_[chunkIdx] = tapSum;
                octRet = unfreeze * chunkShimmerOctGain_ *
                         returnShelf(shimmerShelfStateOct_, shimmerOutOctave_[chunkIdx]);
                fifthRet = unfreeze * chunkShimmerFifthGain_ *
                           returnShelf(shimmerShelfStateFifth_, shimmerOutFifth_[chunkIdx]);
            }

            // --- 8: the feedback matrix, once per sample (FR-024) ---------
            for (std::size_t i = 0; i < n; ++i) {
                const float* row = &matrix_[i * n];
                float acc = 0.0f;
                for (std::size_t j = 0; j < n; ++j) {
                    acc += row[j] * chanOut_[j];
                }
                chanIn_[i] = acc;
            }

            // --- 9: injection into the channel subsets, then the per-line
            //        gain over BOTH (FR-017, FR-015a). diffL -> EVEN channels,
            //        diffR -> ODD, mirroring step 2's tap split so the
            //        diffuser's stereo image survives the network.
            //
            // THE INJECTION IS INSIDE THE GAIN, AND THAT IS DELIBERATE - see
            // the "Jot gain placement" note below the loop for the derivation.
            //
            // FR-033 STEP 4 scales the injection by (1 - freezeRamp) and STEP 6
            // crossfades the per-line gain to exactly 1. At freezeRamp == 1 the
            // whole expression collapses to chanIn_[i] = matrixOut, i.e. the
            // gain-placement question above becomes moot - both orderings agree.
            const float injectL = unfreeze * inputInjectionGain_ * diffScratchL_[k];
            const float injectR = unfreeze * inputInjectionGain_ * diffScratchR_[k];
            // FR-036 denormal tickle: alternating in BOTH channel and absolute
            // sample index, so it never sums to a DC offset, and OUTSIDE the
            // gain so a small g cannot scale it away. Gated to freezeRamp < 1:
            // inside a unity-gain loop it is an energy source, and SC-002's
            // +/-0.5 dB over 60 s does not survive one.
            const auto parity = static_cast<std::size_t>((baseIndex + k) & 1u);
            for (std::size_t i = 0; i < n; ++i) {
                const bool even = ((i & 1u) == 0u);
                // FR-050's and FR-055's pinned subsets, applied as 0/1 masks so
                // the per-sample loop stays branchless. All three returns are
                // already scaled by (1 - freezeRamp), which is FR-033 step 5,
                // and by their own kTapInjectionGain -
                // kShimmerInjectionGain == sqrt(2/2) == 1 for the two shimmer
                // legs, bloomInjectionGain_ == sqrt(2/|subset|) for the bloom.
                const float inject = (even ? injectL : injectR) +
                                     (octRet * shimmerOctMask_[i]) +
                                     (fifthRet * shimmerFifthMask_[i]) +
                                     (bloomRet * bloomInjectMask_[i]);
                const float tickle =
                    tickleScale *
                    ((((i + parity) & 1u) == 0u) ? kDenormalTickle : -kDenormalTickle);
                const float g = crossfade(feedbackGain_[i], 1.0f, freezeRamp);
                chanIn_[i] = ((chanIn_[i] + inject) * g) + tickle;
            }

            // --- 10: write back -------------------------------------------
            // The literal-zero rule (plan S5.3): while the bulk clear is in
            // flight the network is fed exact zeros, so nothing recirculates
            // past the cursor.
            for (std::size_t i = 0; i < n; ++i) {
                delayWrite(i, clearing ? 0.0f : chanIn_[i]);
            }

            // ...and the wet bus is REPLACED, not attenuated, for the same
            // reason. Placed after step 6 so the bloom emphasis return of
            // banner item (5g) is covered too.
            if (clearing) {
                wetScratchL_[k] = 0.0f;
                wetScratchR_[k] = 0.0f;
            }
        }

        // --- D: M/S width on the WET signal only (fdn_reverb.h:368-371) ------
        for (std::size_t k = 0; k < slice; ++k) {
            const float mid = 0.5f * (wetScratchL_[k] + wetScratchR_[k]);
            const float side = 0.5f * (wetScratchL_[k] - wetScratchR_[k]);
            wetScratchL_[k] = mid + (chunkWidth_ * side);
            wetScratchR_[k] = mid - (chunkWidth_ * side);
        }

        // --- E: spectral diffusion, and the dry-path alignment that pays for
        //        it (FR-060 - FR-065, FR-062). Rewrites BOTH wetScratch* (the
        //        STFT -> smear -> OverlapAdd pump) and BOTH dryScratch* (the
        //        matching fftSize delay), so the engine reports ONE latency.
        //        Skipped entirely when the stage was disabled at prepare, which
        //        is FR-065's zero-latency escape hatch.
        if (spectralEnabled_) {
            runSpectralStage(slice);
        }

        // --- F: equal-power dry/wet mix (fdn_reverb.h:374-377), then the
        //        FR-007 output gate. The gate multiplies the WHOLE mix, dry
        //        included: silence() means silence, not "wet muted".
        for (std::size_t k = 0; k < slice; ++k) {
            const float mixedL =
                (chunkDryGain_ * dryScratchL_[k]) + (chunkWetGain_ * wetScratchL_[k]);
            const float mixedR =
                (chunkDryGain_ * dryScratchR_[k]) + (chunkWetGain_ * wetScratchR_[k]);
            outL[k] = gateScratch[k] * mixedL;
            outR[k] = gateScratch[k] * mixedR;
        }
    }

    // ---- configuration snapshot ----
    double sampleRate_ = 0.0;
    bool prepared_ = false;
    std::size_t numChannels_ = 8;
    std::size_t maxBlockSamples_ = 2048;
    float maxDelaySeconds_ = 0.50f;
    std::size_t diffusionFftSize_ = 1024;
    std::size_t diffusionHopSize_ = 256;
    bool spectralEnabled_ = true;
    bool bloomEnabled_ = true;
    bool shimmerAllocated_ = true;
    PitchMode shimmerMode_ = PitchMode::Granular;
    float maxSizeScale_ = kSizeScaleMax;
    std::uint32_t seed_ = 1;
    std::uint64_t sampleCounter_ = 0;
    bool anySamplesProcessed_ = false;

    // ---- FDN core: ONE contiguous buffer, N power-of-two sections
    //      (the fdn_reverb.h:638-689 layout, re-derived) ----
    std::vector<float> delayBuffer_;
    std::size_t sectionOffset_[kMaxChannels]{};
    std::size_t sectionMask_[kMaxChannels]{};
    std::size_t writePos_[kMaxChannels]{};
    float refDelaySamples_[kMaxChannels]{};             ///< reference length x (sr / 48000)
    alignas(32) float effectiveDelay_[kMaxChannels]{};  ///< current, Size + drift + breath scaled
    /// FR-033 step 2: round(effectiveDelay_), tracked while thawed and HELD from
    /// the moment setFreeze(true) skips updateGeometry(). The read length
    /// crossfades to this, so at freezeRamp == 1 every read is integer.
    alignas(32) float latchedDelay_[kMaxChannels]{};
    alignas(32) float feedbackGain_[kMaxChannels]{};    ///< Jot per-line absorption, FR-030
    alignas(32) float dampCoeff_[kMaxChannels]{};       ///< one-pole damping, FR-031
    alignas(32) float filterState_[kMaxChannels]{};
    alignas(32) float dcBlockX_[kMaxChannels]{};
    alignas(32) float dcBlockY_[kMaxChannels]{};
    float dcBlockR_ = 0.0f;     ///< 1 - 250/sr
    float dcBlockGain_ = 1.0f;  ///< (1 + R) / 2, see dcBlock()
    alignas(32) float chanIn_[kMaxChannels]{};
    alignas(32) float chanOut_[kMaxChannels]{};
    float inputInjectionGain_ = 0.5f;  ///< sqrt(2/N), FR-015a
    float outputTapScale_ = 0.25f;     ///< 2/N, FR-018

    // ---- matrix morph ----
    alignas(32) float matrix_[kMaxChannels * kMaxChannels]{};  ///< the applied M(t)
    MatrixMorph morph_;
    float morphPosition_ = kDefaultDimensionality;
    float lastMorphPosition_ = -1.0f;
    float orthogonalityError_ = 0.0f;

    // ---- control-chunk snapshots (plan S6.2 steps 4, 5, 11) ----
    float sizeCombined_ = kDefaultSize;   ///< Size + breath, clamped to [0,1]
    float currentSizeScale_ = 1.0f;       ///< S(sizeCombined_)
    float chunkWidth_ = kDefaultWidth;
    float chunkDryGain_ = 0.0f;
    float chunkWetGain_ = 0.0f;
    float chunkPreDelaySamples_ = 0.0f;
    float chunkShimmerOctGain_ = 0.0f;    ///< send x kShimmerInjectionGain, FR-051
    float chunkShimmerFifthGain_ = 0.0f;
    /// send x kBloomSendMax x bloomInjectionGain_ x (1/sqrt(count)) x guard.
    /// Written by refreshBloomDerived(), not by snapshotBlockScalars(): the FR-058
    /// guard needs the bank state, and a note taken between blocks must be
    /// audible in the very next sample rather than at the next chunk boundary.
    float chunkBloomGain_ = 0.0f;
    /// send x kBloomEmphasisGain x (1/sqrt(count)). The OUT-OF-LOOP twin of
    /// chunkBloomGain_ (banner item (5g)); deliberately NOT multiplied by
    /// bloomGuardScale_, because this path closes no loop and therefore has no
    /// loop gain for FR-058 to bound. Written by the same refreshBloomDerived().
    float chunkBloomEmphasisGain_ = 0.0f;
    float lastJotScale_ = -1.0f;    ///< recompute sentinels for updateDecayAndDamping()
    float lastJotDecay_ = -1.0f;
    float lastJotDamping_ = -1.0f;

    // ---- input path ----
    DelayLine preDelayL_;
    DelayLine preDelayR_;
    DiffusionNetwork diffuser_;
    // All eight are exactly kControlChunkSamples long: plan S6.1 guarantees
    // slice <= kControlChunkSamples, so this is the size, not an upper bound.
    // dryScratch* and wetScratch* complete plan S4's list: step B overwrites
    // preScratch* with the pre-delayed signal, but step F's equal-power mix
    // needs the UNDELAYED dry, and step E (spectral) needs the whole wet slice
    // available after the per-sample loop has ended.
    std::vector<float> preScratchL_;
    std::vector<float> preScratchR_;
    std::vector<float> diffScratchL_;
    std::vector<float> diffScratchR_;
    std::vector<float> dryScratchL_;
    std::vector<float> dryScratchR_;
    std::vector<float> wetScratchL_;
    std::vector<float> wetScratchR_;

    // ---- shimmer taps ----
    PitchShiftProcessor shifterOctave_;
    PitchShiftProcessor shifterFifth_;
    /// kControlChunkSamples each; empty when the taps are force-disabled (RA-6).
    std::vector<float> tapSumScratch_;     ///< this chunk's mono tap sums
    std::vector<float> shimmerOutOctave_;  ///< the PREVIOUS chunk's sums, shifted +12
    std::vector<float> shimmerOutFifth_;   ///< ... and +7
    /// FR-050 pinned injection subsets as per-channel 0/1 masks, materialised in
    /// prepare() and all-zero when the taps are force-disabled.
    alignas(32) float shimmerOctMask_[kMaxChannels]{};
    alignas(32) float shimmerFifthMask_[kMaxChannels]{};
    /// FR-059, one shelf state per return path.
    float shimmerShelfStateOct_ = 0.0f;
    float shimmerShelfStateFifth_ = 0.0f;
    float returnShelfCoeff_ = 1.0f;  ///< one-pole coefficient of the shelf's LP leg

    // ---- harmonic bloom ----
    alignas(32) float bloomY1_[kMaxBloomResonators]{};
    alignas(32) float bloomY2_[kMaxBloomResonators]{};
    alignas(32) float bloomCoeff_[kMaxBloomResonators]{};
    alignas(32) float bloomRSq_[kMaxBloomResonators]{};
    alignas(32) float bloomGain_[kMaxBloomResonators]{};
    alignas(32) float bloomEnv_[kMaxBloomResonators]{};
    float bloomFreq_[kMaxBloomResonators]{};
    std::int32_t bloomOwner_[kMaxBloomResonators]{};  ///< voiceId, -1 = free
    bool bloomDriven_[kMaxBloomResonators]{};         ///< false => released, ringing down
    std::int32_t bloomVoiceId_[kMaxBloomVoices]{};
    std::uint64_t bloomVoiceAge_[kMaxBloomVoices]{};
    /// Driven slots only - what getActiveBloomResonatorCount() reports. The
    /// 1/sqrt(count) normalisation deliberately uses a DIFFERENT count (driven
    /// plus still-ringing); see refreshBloomDerived() for why.
    std::size_t bloomActiveCount_ = 0;
    float bloomInvSqrtCount_ = 0.0f;
    float bloomReleaseCoeff_ = 0.0f;
    float bloomGuardScale_ = 1.0f;
    float bloomShelfState_ = 0.0f;
    float bloomShelfCoeff_ = 1.0f;  ///< FR-059 at kBloomShelfCornerHz, banner (5e)
    /// FR-055's pinned injection subset as a per-channel 0/1 mask, materialised in
    /// prepare() and all-zero when the bloom is disabled there.
    alignas(32) float bloomInjectMask_[kMaxChannels]{};
    float bloomInjectionGain_ = 0.0f;  ///< sqrt(2/|subset|)
    /// Monotonic allocation counter behind bloomVoiceAge_'s oldest-voice retire.
    std::uint64_t bloomAgeCounter_ = 0;
    float lastBloomQ_ = -1.0f;    ///< FR-057 retune sentinel
    bool bloomBankLive_ = false;  ///< any slot allocated; a control-chunk snapshot

    // ---- spectral diffusion ----
    STFT stftL_;
    STFT stftR_;
    OverlapAdd olaL_;
    OverlapAdd olaR_;
    SpectralBuffer specL_;
    SpectralBuffer specR_;
    Xorshift32 smearRngL_{1u};
    Xorshift32 smearRngR_{1u};
    /// Exactly 2 * diffusionFftSize_ == 8 * diffusionHopSize_ floats. Power of
    /// two, so the per-sample read wraps with wetFifoMask_; exact multiple of the
    /// hop, so every hop-sized OverlapAdd::pullSamples lands in one contiguous
    /// run. Empty when the stage is disabled at prepare.
    std::vector<float> wetFifoL_;
    std::vector<float> wetFifoR_;
    std::size_t wetFifoMask_ = 0;
    std::size_t wetFifoRead_ = 0;
    std::size_t wetFifoWrite_ = 0;
    std::size_t wetFifoCount_ = 0;
    /// Output samples still owed a literal 0.0f, counted down from
    /// diffusionFftSize_. This - not "the FIFO happens to be empty" - is what
    /// makes the reported latency exact and partition-independent; see the long
    /// note in runSpectralStage().
    std::size_t spectralWarmupRemaining_ = 0;
    DelayLine dryAlignL_;  ///< FR-062, exactly fftSize samples
    DelayLine dryAlignR_;

    // ---- life modulators (6 objects at N = 8, 10 at N = 16 - FR-006) ----
    BreathingModulator breath_;  ///< FR-070, pinned to kBreathRateHz in prepare()
    TidalModulator tide_;        ///< FR-071, pinned to kTideRateNormalised
    BrownianDrift drift_[kMaxChannels / 2];  ///< FR-072, one per modulated channel
    /// FR-070/FR-071/FR-072 depths. Unsmoothed (FR-009's table says "none"):
    /// they scale modulators whose own output is already slew-limited by a 20 ms
    /// (breath, tide) or 150 ms (drift) internal smoother.
    float sizeBreathDepth_ = kDefaultSizeBreathDepth;
    float tideDepth_ = kDefaultTideDepth;
    /// Mirrored here so prepare() can push FR-009's 0.6 default onto every
    /// BrownianDrift (whose own default is 0.5) without a setter call ordering
    /// dependency.
    float modSmoothness_ = kDefaultModSmoothness;

    // ---- smoothers (FR-009) ----
    OnePoleSmoother sizeSm_;
    OnePoleSmoother densitySm_;
    OnePoleSmoother decaySm_;
    OnePoleSmoother dimSm_;
    OnePoleSmoother dampSm_;
    OnePoleSmoother preDelaySm_;
    OnePoleSmoother modDepthSm_;
    OnePoleSmoother shimmerOctSm_;
    OnePoleSmoother shimmerFifthSm_;
    OnePoleSmoother bloomSendSm_;
    OnePoleSmoother bloomDecaySm_;
    OnePoleSmoother spectralSm_;
    OnePoleSmoother widthSm_;
    OnePoleSmoother mixSm_;
    LinearRamp freezeRamp_;  ///< 0 = running, 1 = frozen
    LinearRamp outputGate_;  ///< silence() / recovery fade
    bool freezeTarget_ = false;

    enum class GateState : std::uint8_t { Open, FadingOut, FadingIn };
    GateState gate_ = GateState::Open;

    // ---- amortized state clear (silence() / emergencyClear(), plan S5.3, S7.14) ----
    bool clearPending_ = false;      ///< while true: delay writes are literal 0, wet is literal 0
    /// FR-007 phase 1 -> phase 2 handshake (banner item (11b)): silence() sets
    /// this and touches NOTHING else; runGateMachine() converts it into
    /// clearPending_ at the control step on which outputGate_ reaches exactly 0.
    bool clearArmed_ = false;
    std::size_t clearCursor_ = 0;    ///< float index into delayBuffer_
    std::size_t clearStage_ = 0;     ///< index into the deferred sub-object reset list
    std::size_t clearQuotaFloats_ = 0;  ///< per-control-chunk slab, sized at prepare

    std::size_t nonFiniteRecoveries_ = 0;
};

// -----------------------------------------------------------------------------
// FR-011 compile-time invariants on the two shipped reference-delay tables.
// Placed at namespace scope so the class is complete. A future table edit that
// breaks coprimality or ordering fails the build rather than degrading the
// network's mode distribution silently; AetherReverb_GeometryAndModalDensity is
// the runtime companion.
// -----------------------------------------------------------------------------
static_assert(detail::aetherTableStrictlyAscending(AetherReverb::kRefDelays8),
              "FR-011: kRefDelays8 must be strictly ascending (index order IS length order)");
static_assert(detail::aetherTableStrictlyAscending(AetherReverb::kRefDelays16),
              "FR-011: kRefDelays16 must be strictly ascending (index order IS length order)");
static_assert(detail::aetherTablePairwiseCoprime(AetherReverb::kRefDelays8),
              "FR-011: kRefDelays8 entries must be pairwise coprime");
static_assert(detail::aetherTablePairwiseCoprime(AetherReverb::kRefDelays16),
              "FR-011: kRefDelays16 entries must be pairwise coprime");

// -----------------------------------------------------------------------------
// FR-050: each shimmer injection pair must span BOTH parities, so neither
// interval is hard-panned by FR-018's even->L / odd->R output tap split. A
// future re-pinning that put both members on one side would ship the octave in
// the left image and the fifth in the right; this fails the build instead.
// -----------------------------------------------------------------------------
static_assert((AetherReverb::kShimmerOctaveInjectChannels8[0] & 1u) !=
                  (AetherReverb::kShimmerOctaveInjectChannels8[1] & 1u),
              "FR-050: the N=8 +12 injection pair must span both parities");
static_assert((AetherReverb::kShimmerOctaveInjectChannels16[0] & 1u) !=
                  (AetherReverb::kShimmerOctaveInjectChannels16[1] & 1u),
              "FR-050: the N=16 +12 injection pair must span both parities");
static_assert((AetherReverb::kShimmerFifthInjectChannels8[0] & 1u) !=
                  (AetherReverb::kShimmerFifthInjectChannels8[1] & 1u),
              "FR-050: the N=8 +7 injection pair must span both parities");
static_assert((AetherReverb::kShimmerFifthInjectChannels16[0] & 1u) !=
                  (AetherReverb::kShimmerFifthInjectChannels16[1] & 1u),
              "FR-050: the N=16 +7 injection pair must span both parities");

// -----------------------------------------------------------------------------
// FR-055: kBloomInjectChannels is "the channels neither shimmer pair uses". That
// is a relationship between three tables, so a future re-pinning of ANY of them
// has to be checked against the other two - here, not by inspection.
// -----------------------------------------------------------------------------
namespace detail {

template <std::size_t A, std::size_t B>
[[nodiscard]] constexpr bool aetherSubsetsDisjoint(const std::size_t (&lhs)[A],
                                                   const std::size_t (&rhs)[B]) noexcept {
    for (std::size_t i = 0; i < A; ++i) {
        for (std::size_t j = 0; j < B; ++j) {
            if (lhs[i] == rhs[j]) {
                return false;
            }
        }
    }
    return true;
}

template <std::size_t A>
[[nodiscard]] constexpr bool aetherSubsetInRange(const std::size_t (&table)[A],
                                                 std::size_t order) noexcept {
    for (std::size_t i = 0; i < A; ++i) {
        if (table[i] >= order) {
            return false;
        }
        for (std::size_t j = i + 1; j < A; ++j) {
            if (table[i] == table[j]) {
                return false;
            }
        }
    }
    return true;
}

}  // namespace detail

static_assert(detail::aetherSubsetInRange(AetherReverb::kBloomInjectChannels8, 8u),
              "FR-055: the N=8 bloom subset must be distinct channels inside [0, 8)");
static_assert(detail::aetherSubsetInRange(AetherReverb::kBloomInjectChannels16, 16u),
              "FR-055: the N=16 bloom subset must be distinct channels inside [0, 16)");
static_assert(detail::aetherSubsetsDisjoint(AetherReverb::kBloomInjectChannels8,
                                            AetherReverb::kShimmerOctaveInjectChannels8) &&
                  detail::aetherSubsetsDisjoint(AetherReverb::kBloomInjectChannels8,
                                                AetherReverb::kShimmerFifthInjectChannels8),
              "FR-055: at N=8 the bloom subset is the channels neither shimmer pair uses");
static_assert(detail::aetherSubsetsDisjoint(AetherReverb::kBloomInjectChannels16,
                                            AetherReverb::kShimmerOctaveInjectChannels16) &&
                  detail::aetherSubsetsDisjoint(AetherReverb::kBloomInjectChannels16,
                                                AetherReverb::kShimmerFifthInjectChannels16),
              "FR-055: at N=16 the bloom subset is the channels neither shimmer pair uses");
static_assert(AetherReverb::kBloomInjectCount8 + (2u * AetherReverb::kShimmerInjectPairSize) == 8u,
              "FR-055: the three N=8 subsets must partition all eight channels");
static_assert(AetherReverb::kBloomInjectCount16 + (2u * AetherReverb::kShimmerInjectPairSize) ==
                  16u,
              "FR-055: the three N=16 subsets must partition all sixteen channels");

}  // namespace DSP
}  // namespace Krate

#ifdef _MSC_VER
#pragma warning(pop)
#endif
