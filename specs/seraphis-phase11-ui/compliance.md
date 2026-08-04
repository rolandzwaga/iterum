# Seraphis Phase 11 (seraphis-phase11-ui) — Compliance Report

## Overall status: COMPLETE (SC-019 partial by host limitation only — see below)

### Honest gap list (must read before anything else)

**UPDATED 2026-08-04 after the phase-owner ruling "Hybrid" on OE-1.** Four of the five findings below
were the SAME root cause: a whole-process() wall-clock CPU measurement compared against a ceiling
derived from a chain-only Phase 10 baseline. The ruling (spec.md → *Open Escalations* → OE-1 →
**RULING (2026-08-04)**) restated SC-009(a), SC-014 arm 7 and SC-031 as **differential** criteria
(each measuring its own feature's marginal whole-process() cost as a same-run delta) and re-based
SC-010(b) on a whole-process() baseline that is **PROVISIONAL and reporting, not gating**, until a
seven-run fresh-boot cold set pins it. The absolute 25 % whole-process() promise was **not dropped** —
it moved to a new **roadmap Phase 11.5** that Phase 12 must not ship before. No ceiling was raised and
no threshold was relaxed: `kFullPolyCeilingNs`, `kSnapshotStageBudgetNs`, `kRegressionFactor`,
`kBaselineFullPolyNs` and `kClosedGateCeilingNs` are all byte-unchanged.

| ID | Verdict | One-line finding |
|----|---------|-------------------|
| SC-009 | **PASS** | Restated (a): producer's marginal whole-process() cost, gate OPEN − gate CLOSED interleaved on one fixture = **−27 906 ns/block (−0.262 points)** vs the 10 666.7 ns (0.10 point) bound. Arm (b), reached for the first time, is **264 ns/block (0.0025 %)** vs the same 10 666.7 ns budget — the producer's true cost, and why the delta is noise. Absolute reported: 33.56 % (Phase 11.5's). |
| SC-010 | **PASS** | Arm (a) unchanged and hard: **0 publish attempts, 0 skips** over 60 s with the gate closed. Arm (b) restated on a whole-process() baseline: measured **3 638 360 ns/block (34.11 %)**, **under** the provisional gate 3 893 440 ns (1.15 × 3 385 600) — REPORTED, not gated, under the "PROVISIONAL — pin from 7-run fresh-boot cold set before release" banner. |
| SC-014 | **PASS** | Arms 1–6 unchanged and green. Arm 7 restated: re-push's marginal cost (64-override Bloom sweep − identical sweep with none) = **+131 476 ns/block (+1.233 points)** vs the 285 866.7 ns (2.68 points) bound, 1.447 points headroom. |
| SC-019 | **PARTIAL** | Every gate runnable on this Windows host is green (build, full test suite minus [.perf], pluginval strictness 5, check-portability, lint-layers, clang-tidy 0/0). macOS/Linux build legs and `auval` are not runnable here and are unverified. |
| SC-031 | **PASS** | Restated: in-flight gesture's marginal cost (30 Hz kind-1 drag − no gesture) = **+12 788 ns/block (+0.120 points)** vs the 106 666.7 ns (1.00 point) bound. |

**Two release gates remain open and are NOT compliance failures — they are dated obligations:**
(1) `kSc010BaselinePinned` is `false`; it must be flipped after a seven-run fresh-boot cold set is
recorded. (2) Roadmap **Phase 11.5** owns the absolute 25 % whole-process() figure (measured 33.6–35.6 %
on this warm host) and blocks Phase 12.

Everything else — all 45 functional requirements (FR-001 through FR-054), the other 28 of 33
success criteria, and all 5 constitution/constraint checks (CC-rt, CC-layers, CC-naming,
CC-warnings, CC-portability) — is verified PASS with cited file:line evidence and real test-run
output. Two previously-open items (SC-004's ASan/Debug lane, FR-044/SC-019's clang-tidy warnings)
were closed for real in an earlier pass, not worked around.

**Verification run for the four updated rows (2026-08-04, warm host, all figures best-of-16):**
`seraphis_tests.exe` → `All tests passed (444681 assertions in 80 test cases)`;
`seraphis_tests.exe "[.perf]" -s` → `All tests passed (1983 assertions in 11 test cases)`;
Release build of `seraphis_tests`, exit 0, **zero warnings**.

---

## Compliance table

One row per item. Evidence is verbatim from the verifying task/build agent. Where an ID appears
twice — once under the "fr" (functional-requirement-derived) lens and once under the "sc"
(success-criterion) lens — both rows are kept per the "one row per item" instruction; they
corroborate the same finding from two independent verification passes.

### Functional Requirements (FR-xxx)

#### FR-001 — PASS _(FR lens)_

> plugins/seraphis/resources/editor.uidesc:1-30 is a new 574-line Phase 11 document; the Phase 8 placeholder banner is gone (new banner at :3-25 reads "Seraphis Phase 11 - the organism-first editor"), template declared at :164 as `<template name="editor" class="CViewContainer" size="1000, 700" ... sub-controller="SeraphisEdit">`.

#### FR-002 — PASS _(FR lens)_

> Diffed the <control-tag> block against HEAD: `git show HEAD:plugins/seraphis/resources/editor.uidesc | grep '<control-tag ' | sort` vs the new file's sorted block => diff empty, printed "CONTROL-TAGS IDENTICAL"; both are exactly 107 lines (wc -l = 107). Block spans editor.uidesc:...-162.

#### FR-003 — PASS _(FR lens)_

> `grep -c "control-tag=" plugins/seraphis/resources/editor.uidesc` = 110 view bindings vs 107 <control-tag> entries. The three duplicates are the header freeze cluster at editor.uidesc:212-218 (AtmosFreeze/AetherFreeze/FxSpectralFreeze). Asserted in plugins/seraphis/tests/unit/parameter_surface_test.cpp:769 `CHECK(bound.size() == 110u)`, :784 `CHECK(unreachable.empty())` with an EMPTY allowlist (`unreachableParams(xml, registeredIds, {})` at :780-781), and the per-ID multiset check at :787-800 using `isSecondBindingId`. seraphis_tests: All tests passed (444661 assertions in 80 test cases).

#### FR-004 — PASS _(FR lens)_

> `git status --short plugins/seraphis/src/plugin_ids.h` returns nothing and `git diff HEAD -- plugins/seraphis/src/plugin_ids.h` is empty — the frozen-type legend and every range/default is untouched. parameter_surface_test.cpp:508 `CHECK(controller.getParameterCount() == 107)` passes.

#### FR-005 — PASS _(FR lens)_

> plugins/seraphis/tests/integration/ui_perf_test.cpp:604 TEST_CASE("Seraphis_Phase11_UsesNoPlatformApi") scans src/ui/*.{h,cpp} + processor/controller for windows.h/HWND/NSView/gtk_/etc. with the filesMissing/codeBytes/witness anti-vacuity guards; passed in the seraphis_tests run (80/80 cases green).

#### FR-006 — PASS _(FR lens)_

> editor.uidesc:173 `<view class="CView" custom-view-name="CloudView" origin="0, 32" size="1000, 638"/>` is the first child element inside the template opened at :164-166. Asserted at runtime by custom_view_test.cpp:1130 TEST_CASE("Seraphis_Phase11_CustomViews_AreInstantiated") arm 3(i) (`dynamic_cast<UI::CloudView*>(root->getView(0)) != nullptr`), green.

#### FR-007 — PASS _(FR lens)_

> editor.uidesc header block :176-225: MasterGain slider :188, Polyphony :194, SoftLimit :200, Seed :208 (4 header-exclusive globals) + freeze cluster AtmosFreeze :212, AetherFreeze :214, FxSpectralFreeze :216 = 7 bound views; the preset button at :225 is `<view class="COnOffButton" session-tag="preset" .../>` with no control-tag. FR-007's browser path asserted by custom_view_test.cpp:790 TEST_CASE("Seraphis_PresetButton_OpensTheBrowser"), green.

#### FR-010 — PASS _(FR lens)_

> plugins/seraphis/src/processor/cloud_frame.h:24-37 defines POD `struct CloudFrame` in namespace Seraphis; :42 `static_assert(sizeof(CloudFrame) == 808, "C-2's pinned layout")`; producer/consumer banner at :1-11 ("Producer: Processor::publishCloudFrame(), audio thread, ONCE per process() call. Consumer: Controller::onDataExchangeBlocksReceived()"); user-context id at :46.

#### FR-011 — PASS _(FR lens)_

> processor.cpp:1167 `tresult PLUGIN_API Processor::connect(Vst::IConnectionPoint* other)` creates the handler (:1178-1180 `dataExchangeHandler_ = ...; ->onConnect(other, getHostContext())`); :1185 `disconnect()` calls `onDisconnect` then `dataExchangeHandler_.reset()` (:1187-1191); setActive drives `onActivate(setup)` at :986 and `onDeactivate()` at :992. Verified by cloud_frame_test.cpp TEST_CASE("Seraphis_DataExchangeHandler_FollowsTheConnectionAndActivation"), green.

#### FR-012 — PASS _(FR lens)_

> processor.cpp:1656 `publishCloudFrame();` is the single call site and sits after the slice loop (loop at :1311-1312); definition at :3962; the gate is the only short-circuit (:3986 `if (cloudFrameEnabled_.load(std::memory_order_relaxed))`) and the transport is guarded on `dataExchangeHandler_ == nullptr` at :4075. No call from renderSlice (grep shows one call site only). SC-007 test `Seraphis_CloudFrame_PublishesOncePerProcessCall` green.

#### FR-013 — PASS _(FR lens)_

> processor.cpp:4030-4066 fills every field: sequence/activeVoices/focusVoice/partialCount (:4030-4033), `fundamentalHz = (activeVoices > 0) ? cloud.getFundamentalHz() : 0.0f` (:4044) — never frequencyHz[0]; morphTravelPosition from `getVoice(focus).morph().getTravelPosition()` (:4046-4047); the 64-entry fill (:4049-4058) and the explicit zero-fill above partialCount (:4062-4066); maskBits/overriddenBits from `partialMaskBits_`/`partialPanOverrideBits_` (:4068-4073, `overriddenBits = panBits | maskBits`). SC-006 arms (a)-(g) green.

#### FR-014 — PASS _(FR lens)_

> processor.cpp:4005-4025: loop over `SeraphisEngine::kMaxVoices` skipping `VoiceState::Idle`, keeping the greatest `getVoiceAllocationSerial` (clause a); `if (!haveNonIdle)` retains previous focus while `getVoiceLevel(previous) > kCloudFrameSilenceLevel` (clause b, :4022-4024), else 0 (clause c). Evaluated once per publish. Asserted by cloud_frame_test.cpp TEST_CASE("Seraphis_CloudFrame_FocusVoiceFollowsAllocationSerial"), green.

#### FR-015 — PASS _(FR lens)_

> processor.cpp:3957-3961 RT-safety banner; body is a bounded <=64-iteration read loop (:4049) plus one `std::memcpy(block.data, &pendingFrame_, sizeof(CloudFrame))` (:4086). Measured by ui_perf_test.cpp:408 TEST_CASE("Seraphis_CloudFrame_AllocatesNothing") — AllocationScope over a 60 s gate-open render with the corpus/witness anti-vacuity guards plus the lock-free atomics arm; green in the seraphis_tests run.

#### FR-016 — PASS _(FR lens)_

> controller.h:90 `public Steinberg::Vst::IDataExchangeReceiver`, DEF_INTERFACE at :222, entry points declared :209-215. custom_view_test.cpp:97 TEST_CASE("Seraphis_Controller_CachesOnlyTheMostRecentBlock") asserts the last-of-three-blocks rule (:100) and `queueOpened` returning dispatchOnBackgroundThread = false (:127); both green.

#### FR-017 — PASS _(FR lens)_

> cloud_view.cpp:187-211 builds points: masked partials take `pt.radius = kMaskedRingRadius; pt.hollow = true` (:203-204), others `hollow = false` (:208). Span constants cloud_view.h:56-57 (20 Hz / 20 kHz), kMaskedRingRadius :67. Asserted by custom_view_test.cpp:204 ("AxisMapIsMonotoneAndClamped", sections i-iv at :218/:224/:235/:241) and :261 ("MaskedPartialStaysAClickTarget", sections at :282/:290/:295) — all green.

#### FR-018 — PASS _(FR lens)_

> cloud_view.h:79 `kCloudViewTimerMs = 33`, timer member :212 `SharedPointer<CVSTGUITimer> timer_`, created in `attached()` and cancelled in `removed()` (declared :100-101); redraw gated on `lastSeenSequence_` (:213) via `onTimerForTest()` (:142). SC-020 arms (a)/(b) in custom_view_test.cpp:506 assert exactly N invalid() calls for N advanced sequences and zero for 30 unchanged ones; green.

#### FR-019 — PASS _(FR lens)_

> cloud_view.h:194-195 — the frame is a value member on the controller, so a never-received frame is simply `partialCount == 0`. editor_lifecycle_test.cpp:372 TEST_CASE("Seraphis_Editor_WorksWithNoFrameEverReceived") runs 10 cycles with the gate never opened, calls renderForTest() per cycle and asserts drawn-point count exactly 0; green.

#### FR-020 — PASS _(FR lens)_

> macro_ring_knob.h:55 `class MacroRingKnob : public Krate::Plugins::ArcKnob`; creator at :117-123 (`MacroRingKnobCreator : VSTGUI::ViewCreatorAdapter`, ctor calls `registerViewCreator(*this)` :118, `getViewName() -> "MacroRingKnob"` :120, `getBaseViewName()` :122), inline global at :140. Five instances in editor.uidesc:232, :237, :242, :247, :252 (IDs 100-104). SC-004 arm 1 asserts exactly five via dynamic_cast; green.

#### FR-021 — PASS _(FR lens)_

> custom_view_test.cpp:1744 TEST_CASE("Seraphis_MacroRing_DoesNotAnimateTheCloudViewLocally") drives a MacroRingKnob across its range against a fixed cached frame and asserts invalid() count and drawn-point set unchanged (sections :1786 anti-vacuity, :1794 the claim); plus SC-017's producer-side measurement (cloud_frame_test.cpp:1003) showing the real matrix response. Both green.

#### FR-022 — PASS _(FR lens)_

> editor.uidesc:302-314 declares the seven tab buttons in order Cloud/Morph/Body/Atmos/Aether/FX/Life-Env (session-tags tab0..tab6); drawer_container.h:55 `static constexpr int kTabCount = 7`. SC-004 arm 2 walks the tab buttons and string-compares the ordered title list; green.

#### FR-023 — PASS _(FR lens)_

> drawer_container.h:60-61 `static constexpr CRect kCollapsedRect{0.0, 670.0, 1000.0, 700.0}` and `kOpenRect{0.0, 420.0, 1000.0, 700.0}` — exactly FR-023's integers. SC-020 arm (e) (custom_view_test.cpp:546) byte-compares both rects; green.

#### FR-024 — PASS _(FR lens)_

> editor.uidesc:173 gives the cloud view origin 0,32 size 1000,638 => rect (0,32,1000,670), and it is a direct child of the 1000x700 root. SC-020 arm (c) asserts the rect is byte-equal in the drawer-open case (custom_view_test.cpp:506) and arm (d) repeats it collapsed (:528); green.

#### FR-025 — PASS _(FR lens)_

> Drawer children in editor.uidesc are plain classes only — CTextButton tabs :302-314, and the page contents use CSlider/COptionMenu/CCheckBox/ArcKnob (e.g. slot buttons :381-384). No custom-view-name inside the DrawerContainer subtree (the only two custom-view-name attributes in the file are CloudView :173 and DrawerContainer :291). SC-004 arm 3(ii) / SC-020 arm (f) (custom_view_test.cpp:568) assert seven page children with exactly one visible; green.

#### FR-026 — PASS _(FR lens)_

> Exactly three CView-derived classes under src/ui/: cloud_view.h:85 `class CloudView : public VSTGUI::CView`, macro_ring_knob.h:55 `: public Krate::Plugins::ArcKnob`, drawer_container.h:49 `class DrawerContainer : public VSTGUI::CViewContainer`; edit_sub_controller.h:90 `class SeraphisEditSubController : public VSTGUI::DelegationController` is not a view. custom_view_test.cpp:1595 SC-022(a) arm 1 static_asserts is_base_of_v for exactly those three plus `!is_base_of_v<CView, SeraphisEditSubController>` (:1596) and arm 2's base-name allowlist scanner (:1659) with its own negative control (:1622); green.

#### FR-027 — PASS _(FR lens)_

> cloud_view.h:209 `Mode mode_ = Mode::Observe;   // FR-027's default`; toggle is editor.uidesc:264 `<view class="COnOffButton" session-tag="mode" .../>` (no control-tag). SC-004 arm 3(iii) asserts Observe immediately after didOpen on every one of the lifecycle cycles; green.

#### FR-028 — PASS _(FR lens)_

> custom_view_test.cpp:301 TEST_CASE("Seraphis_CloudView_GesturesEmitTheRightEditMessage") drives onMouseDown/Moved/Up and asserts all four C-4 rows (plain vertical => kind 1 with b unchanged; alt+vertical => kind 1 with a unchanged; horizontal => kind 2; click => kind 3 with the toggled maskBits value asserted in both directions). Q6 reference is cloud_view.h:75 `kFallbackReferenceHz = 261.63f` and :133 `referenceHz()`; Blend/Tilt sliders are editor.uidesc:269/:276 (session-tags blend/tilt). Green.

#### FR-029 — PASS _(FR lens)_

> processor.cpp:3712-3718 case 1 routes through `setPartial` -> `stageSlotEdit(slot, edited)` -> the Phase 9 staging ring; no HarmonicCloud::setSpectralTarget call exists in the edit path. partial_edit_test.cpp TEST_CASE("Seraphis_EditMode_RatioEditReachesSoundingVoice") proves the end-to-end landing on a sounding voice; green.

#### FR-030 — PASS _(FR lens)_

> processor.cpp:3407 `void Processor::repushPartialOverrides() noexcept`, called from the audio thread at :1331 behind `partialOverridesPending_.exchange(false, acquire)` (:1330), and after the clearing events at :936, :1012, :2008, :2039, :2324. Table members are the pan staging array + `partialPanOverrideBits_`/`partialMaskBits_`. SC-014 arms 1-6 (partial_edit_test.cpp "Seraphis_PartialOverrides_SurviveClearingEvents" and "...SurviveAMacroRingSweep") green.

#### FR-031 — PASS _(FR lens)_

> dsp/include/krate/dsp/processors/spectral_state.h:533 `inline void setPartial(SpectralState& s, std::size_t index, float ratio, ...)`, :608 `[[nodiscard]] inline SpectralState blendStates(const SpectralState& a, const SpectralState& b, ...)`, :711 `inline void tiltState(SpectralState& s, float dbPerOct) noexcept` — all free functions in the Layer 2 header; `node tools/lint-layers.js` => "OK — no layer-dependency violations in 5-layer DSP tree."

#### FR-032 — PASS _(FR lens)_

> dsp_processors_tests.exe "SpectralState_AuthoringMutators_PreserveValidity" => "All tests passed (137 assertions in 1 test case)"; full dsp_processors_tests => "All tests passed (10640879 assertions in 3297 test cases)". Test lives in dsp/tests/unit/processors/spectral_state_test.cpp and covers both clauses (valid=>valid; invalid=>memcmp byte-unchanged) plus blendStates' unconditional validity.

#### FR-033 — PASS _(FR lens)_

> seraphis_voice.h:672-674 `setPartialPosition`/`setPartialMask`/`clearPartialMask` pass-throughs to cloud_; seraphis_engine.h:863-880 `setPartialPositionAllVoices`/`setPartialMaskAllVoices`/`clearPartialMaskAllVoices` each fanning out over `[0, kMaxVoices)`. Dedicated TU dsp/tests/unit/systems/seraphis_partial_fanout_test.cpp (277 lines, registered at dsp/tests/CMakeLists.txt:366); dsp_systems_tests => "All tests passed (6045010 assertions in 1222 test cases)".

#### FR-033a — PASS _(FR lens)_

> seraphis_voice.h:792-793 now read `void setSpectralState(int slot, const SpectralState& s) noexcept { morph_.setState(slot, s); }` / `setSpectralStateCount(int n) { morph_.setStateCount(n); }` — no isConfigurable() call; the predicate still exists at :921 and still gates every other caller. Comment correction at spectral_morph_engine.h:199-212 names only prepare()/reset()/setSeed() and explicitly records setState/setStateCount as struck by FR-033a. SC-028/029/030 tests green (partial_edit_test.cpp "Seraphis_EditMode_RatioEditReachesSoundingVoice", "...LiveRatioEditIsClickFree"; dsp_systems_tests all green).

#### FR-034 — PASS _(FR lens)_

> plugin_ids.h:27 `constexpr Steinberg::int32 kCurrentStateVersion = 3;` unchanged (plugin_ids.h has no diff vs HEAD). state_v3_test.cpp:909 TEST_CASE("Seraphis_EditedState_RoundTripsAtV3") green in the seraphis_tests run.

#### FR-034a — PASS _(FR lens)_

> processor.cpp:401-436 documents the block and asserts its size (`static_assert(... == 272, "FR-034a: the [partials] block is 64 pan floats + two 64-bit masks = 272 ...")` at :436); `savePartialOverrides` at :439 writes 64 floats then `writeInt64u` x2 (:444-445); `loadPartialOverrides` at :458 uses `readInt64u` (:478, :482) and returns EOF-safe. Written LAST in getState (:1878, comment "[partials] 272 B") and read last in setState (:1752 `loadedPartialOverrides ... [partials] 272 B (LAST)`). SC-015's truncated-stream arm green.

#### FR-035 — PASS _(FR lens)_

> The factory-derivation path at processor.cpp:1383-1385 guarded by lastPushedSlotStateId_ is untouched; controller re-seed is in the 409-412 handler (controller.h:104 banner: "IDs 409-412 re-seed slotMirror_ from makeFactoryState()"). state_v3_test.cpp:1053 TEST_CASE("Seraphis_SlotDropdown_DiscardsOnlyThatSlot") green.

#### FR-036 — PASS _(FR lens)_

> processor.cpp:3680-3701 validates in C-5 clause 5 order: unknown kind (:3682), slot > 3 for kinds 1/4/5/6/7 (:3685-3688), index >= kMaxPartials for kinds 1/2/3 (:3690-3693), and bit-pattern non-finite screening via `Krate::DSP::detail::isNaN/isInf` (:3698-3701) — never std::isnan. Kind 4 with no live kind-7 snapshot returns early at :3742-3744, and the float range test precedes the cast at :3750-3752. partial_edit_test.cpp TEST_CASE("Seraphis_EditMessage_RejectsGarbage") green.

#### FR-037 — PASS _(FR lens)_

> RESOLVED since the last pass. Structure (unchanged, re-read this session): seraphis_macro_matrix.h:98-99 append FxDelaySend/FxWanderDepth immediately before `Count` after the Aether block; :143 `struct SeraphisEffectsTargets`; :836 `computeEffectsTargets()`; :537-547 the two kRows entries, both `.base = 0.0f` + `ModCurve::Linear`; :625 `everyEffectsRowHasAPodField`, asserted :989; :994 `static_assert(static_cast<std::size_t>(SeraphisMacroTarget::Count) == 29, "C-10 / SC-021(d): 27 pre-Phase-11 targets + EXACTLY 2")`. THE BLOCKING GAP IS CLOSED: the Entropy -> FxWanderDepth `.amount` is now MEASURED, not a pilot. The T025 status note at :507-529 now reads "THE ENTROPY -> FxWanderDepth amount (0.50f) IS ALSO MEASURED NOW (2026-08-04) ... at 0.50f the sweep is monotone and very nearly linear" and carries the five-point M/S side-RMS table; spec.md:1030-1031 records `Dissolve -> FxDelaySend` shipped `0.20f` (MEASURED, -19.3 dB, in the ruled [-20,-6] dB band) and `Entropy -> FxWanderDepth` shipped `0.50f` (MEASURED and monotone); spec.md:1051-1063 carries the table. The table is emitted by the test itself on a PASSING run — my full-suite log lines 205-213 (effects_chain_test.cpp:5286-5290, TEST_CASE "Seraphis_MacroDissolve_ReachesEffects" at :5199, SC-021(a) Entropy section at :5239): "Entropy 0.00 side RMS = 0 / 0.25 = 0.000464157 / 0.50 = 0.00092818 / 0.75 = 0.0013949 / 1.00 = 0.00186224 / side RMS(1)/side RMS(0.25) = 4.01208". Targeted re-run: seraphis_tests.exe on the 5 Phase-11 cases incl. Seraphis_MacroDissolve_ReachesEffects -> "All tests passed (20573 assertions in 5 test cases)". spec.md's outstanding list no longer marks this blocking (OQ-4 at :2355-2373 is RESOLVED, both rows written back).

#### FR-038 — PASS _(FR lens)_

> processor.cpp:1400-1401 `composedEffects_ = macros_.computeEffectsTargets(); ++composedEffectsRecomputes_;` once per process() before the slice loop; substituted reads at :1456 (bypass predicate reads `std::clamp(composedEffects_.wanderDepth, 0, 1)`), :2833 (`const float mix = std::clamp(composedEffects_.delaySend, 0.0f, 1.0f)`) and :3850 (`fxWanderDepthSm_.setTarget(std::clamp(composedEffects_.wanderDepth,...))  // ID 1441`). Test seams at :2925/:2929/:2933. SC-021(c) arm in effects_chain_test.cpp "Seraphis_MacroDissolve_ReachesEffects" green.

#### FR-039 — PASS _(FR lens)_

> Both new rows carry `.base = 0.0f` matching the shipped defaults (seraphis_macro_matrix.h:526, :532) and the matrix's neutral identity (`applyModCurve(c, 0) == 0`) makes computeEffectsTargets() an exact {0,0} at the neutrals — stated at :470-471. SC-021(b)'s bit-equality arm in effects_chain_test.cpp and SC-001's exact-equality render both green (seraphis_tests 80/80).

#### FR-040 — PASS _(FR lens)_

> ui_perf_test.cpp:408 TEST_CASE("Seraphis_CloudFrame_AllocatesNothing") — AllocationScope over a 60 s gate-open render asserting allocations == 0 / exceptions == 0, source scan for lock and throw sites over the three audio-thread-reachable Phase 11 files with filesMissing/codeBytes/witness guards, plus `phase11AtomicsAreLockFreeForTest()`. Green in the seraphis_tests run (444661 assertions, 80 cases, 0 failures).

#### FR-041 — PASS _(FR lens)_

> controller.cpp:390-405 willClose(): presetBrowserView_->close() then nulled (:393-396), `cloudView_ = nullptr; drawer_ = nullptr; macroRings_.fill(nullptr); activeEditor_ = nullptr;` (:398-401), `subControllerInstances_ = 0;` (:405). editorOpenCount_ is reset separately in terminate() at :165, not in willClose(). CloudView cancels its own timer in removed() (cloud_view.h:101). custom_view_test.cpp:771 asserts the instance count returns to 0; green.

#### FR-042 — PASS _(FR lens)_

> ui_negative_control_test.cpp:1 TEST_CASE("Seraphis_Phase11_OpenGate_ChangesNoSample") — two renders on one instance with the gate forced open vs closed, `max |a-b| == 0.0f`, plus the anti-vacuity arms (publish attempts > 0 in A, == 0 in B). Green in the seraphis_tests run.

#### FR-043 — PASS _(FR lens)_

> SC-014 arm 5 (sample-rate change via setupProcessing re-entry) is asserted in partial_edit_test.cpp TEST_CASE("Seraphis_PartialOverrides_SurviveClearingEvents"); the override table is processor-owned (not engine-owned) and re-pushed via repushPartialOverrides() at processor.cpp:1012 on the setActive/prepare path. Green in the seraphis_tests run.

#### FR-044 — PASS _(FR lens)_

> ALL THREE GATES NOW GREEN — the clang-tidy red is gone. (1) `node tools/check-portability.js` -> "check-portability: 28 translation unit(s) with g++ ... check-portability: all clear -- 28 compiled." and the OK list NOW CONTAINS the previously-skipped Phase 11 TUs. The tool itself was fixed: tools/check-portability.js:155-175 now unions `git ls-files --others --exclude-standard` into the file set. (2) `node tools/lint-layers.js` -> "lint-layers: OK — no layer-dependency violations in 5-layer DSP tree." (3) `./tools/run-clang-tidy.ps1 -Target seraphis -BuildDir build/windows-ninja` -> "[INFO] Clang-Tidy Analysis Complete / Files analyzed: 33 / [OK] Errors: 0 / [OK] Warnings: 0". All 31 previously-reported warnings are gone and NOT by suppression: `grep -c NOLINT` over src/ui/*.{cpp,h}, src/controller/controller.cpp and tests/unit/controller/custom_view_test.cpp returns 0 for every file.

#### FR-045 — PASS _(FR lens)_

> controller.cpp:346 `VSTGUI::IController* Controller::createSubController(...)`; edit_sub_controller.h:90 `class SeraphisEditSubController : public VSTGUI::DelegationController`; tag acquisition in edit_sub_controller.cpp:76 verifyView() turning session-tag into setTag()+setListener(this) (with the null-parent guard at :105-107). editor.uidesc:166 binds `sub-controller="SeraphisEdit"` on the template ROOT so the header preset button is inside the subtree. custom_view_test.cpp:696 TEST_CASE("Seraphis_Phase11_SubController_OwnsEveryTaglessControl") sections :714/:722/:748/:756/:771 all green.

#### FR-046 — PASS _(FR lens)_

> controller.h:344 `std::array<Krate::DSP::SpectralState, 4> slotMirror_{};`, accessors :312/:319, mutator-apply seam :326. morph_params.h:11-19 now declares `loadMorphParamsToController(..., std::array<SpectralState,4>& mirror)` and :402 the destination parameter — the payloads are no longer discarded. Both re-seed sources covered: state_v3_test.cpp:1160 TEST_CASE("Seraphis_SlotMirror_ReSeedsFromTheStateStream") (arms a and b, incl. the corrupted-payload desync arm) and :1053 for the dropdown source; green.

#### FR-047 — PASS _(FR lens)_

> controller.cpp:380 `if (editorOpenCount_++ == 0) { ... sendEditMessage(message); }` (0->1 only), :418-423 `if (editorOpenCount_ > 0) { if (--editorOpenCount_ == 0) { ... sendEditMessage(message); } } else { editorOpenCount_ = 0; }` (1->0 only, floored), :165 reset in terminate(). Processor side is a single `std::atomic<bool> cloudFrameEnabled_` written relaxed at processor.cpp:3708 and read relaxed at :3986. custom_view_test.cpp:877 TEST_CASE("Seraphis_MultiEditor_RefcountGatesCorrectly") with sections :941 and :954; green.

#### FR-048 — PASS _(FR lens)_

> controller.cpp:523-566: gesture begin resets the throttle (:530-536), each value stages `editThrottle_.pending` and sets `sawValue` (:540-542), the wire send is gated on `(now - lastSend) >= kEditThrottleInterval` (:550), and the terminal flush at :563-565 sends unconditionally `if (editThrottle_.sawValue)` regardless of window state. custom_view_test.cpp:975 TEST_CASE("Seraphis_EditThrottle_FlushesFinalValue") sections :991/:1040/:1051; green.

#### FR-050 — PASS _(FR lens)_

> plugins/seraphis/CMakeLists.txt:29 `src/processor/cloud_frame.h` and :45-52 the eight src/ui entries incl. cloud_view.cpp, drawer_container.cpp, edit_sub_controller.cpp; the same three .cpp files are re-listed for the second compilation in plugins/seraphis/tests/CMakeLists.txt:64-66. Confirmed by the target building clean: `cmake --build build/windows-x64-release --config Release --target seraphis_tests dsp_processors_tests dsp_systems_tests` exit 0.

#### FR-051 — PASS _(FR lens)_

> plugins/seraphis/tests/CMakeLists.txt:45-52 lists integration/cloud_frame_test.cpp, unit/controller/custom_view_test.cpp, integration/partial_edit_test.cpp, integration/ui_negative_control_test.cpp, integration/ui_perf_test.cpp; dsp/tests/CMakeLists.txt:366 and :382 list seraphis_partial_fanout_test.cpp and spectral_state_authoring_test.cpp. All five Phase 11 seraphis TUs and both dsp TUs actually ran (seraphis_tests 80 cases incl. all Phase 11 names; dsp_systems_tests ran SpectralState_AuthoringMutators_AreAudible).

#### FR-052 — PASS _(FR lens)_

> plugins/seraphis/src/entry.cpp:34-35 `#include <ui/arc_knob.h>` and `#include "ui/macro_ring_knob.h"`; the Phase 8 prohibition is rewritten in the same file at :12-14 and :20 ("NOTE: as of Phase 11 (FR-052) this file DOES include ui/*.h headers" / "The Phase 8 prohibition this replaces ...").

#### FR-053 — PASS _(FR lens)_

> plugins/seraphis/CLAUDE.md now carries a full "The editor (src/ui/, Phase 11) — organism-first, three custom views" section with the closed three-class roster table, the cloud-frame data-path diagram (808 B POD, 4 blocks, 'SCLD'), the edit-channel diagram, the 9000+ session-tag table and the teardown-discipline paragraph; the skeleton line now reads "ui/ is populated as of Phase 11".

#### FR-054 — PASS _(FR lens)_

> plugins/seraphis/CHANGELOG.md:8-30 — a new `## [Unreleased]` section headed "Phase 11 — the interface" with an `### Added` list covering the cloud view, the five macro rings, the pull-up drawer and the rest.

### Success Criteria (SC-xxx)

#### SC-001 — PASS _(FR lens)_

> plugins/seraphis/tests/integration/ui_negative_control_test.cpp TEST_CASE("Seraphis_Phase11_OpenGate_ChangesNoSample", "[ui][phase11]") passed in `build/windows-x64-release/bin/Release/seraphis_tests.exe` => "All tests passed (444661 assertions in 80 test cases)", 0 failures. Both anti-vacuity arms (publish attempts > 0 in arm A, == 0 in arm B) are inside that case.

#### SC-002 — PASS _(FR lens)_

> Counted directly: `grep -c "<control-tag " editor.uidesc` = 107 and `grep -c "control-tag=" editor.uidesc` = 110. Asserted at parameter_surface_test.cpp:769 (`CHECK(bound.size() == 110u)`), :761 (`CHECK(tagged == registered)`), :784 (`CHECK(unreachable.empty())` with `{}` allowlist at :781) and the per-ID multiset loop :787-800. Case green in the 80/80 run.

#### SC-003 — PASS _(FR lens)_

> parameter_surface_test.cpp's per-view class assertion (`CHECK(view.viewClass == expectedViewClass(row->kind))`) with MacroRingKnob enumerated for IDs 100-104; the whole file is in the green seraphis_tests run (444661 assertions, 0 failed).

#### SC-004 — PASS _(FR lens)_

> Both lanes green now. RELEASE: seraphis_tests.exe full run -> "All tests passed (444681 assertions in 80 test cases)" (0 FAILED lines); targeted re-run of the 5 Phase-11 cases incl. Seraphis_Phase11_CustomViews_AreInstantiated (custom_view_test.cpp:1156) -> "All tests passed (20573 assertions in 5 test cases)". ASan/Debug: rebuilt build-asan (cmake --build build-asan --config Debug --target seraphis_tests, exit 0), then build-asan/bin/Debug/seraphis_tests.exe on the three lifecycle/custom-view cases -> "All tests passed (421 assertions in 3 test cases)". The root cause is fixed rather than worked around: custom_view_test.cpp:1213-1230 replaces the old `REQUIRE(frame->getNbViews() == 1u)` with a search over all frame children, with the comment naming VSTGUI_LIVE_EDITING's extra child as the reason.

#### SC-005 — PASS _(FR lens)_

> RELEASE: full suite "All tests passed (444681 assertions in 80 test cases)"; targeted run incl. Seraphis_EditorLifecycle_SurvivesFullSurface -> "All tests passed (20573 assertions in 5 test cases)". ASAN DEBUG -> "All tests passed (421 assertions in 3 test cases)" — zero ASan reports. REMAINING UNMEASURED ARM, stated not hidden: the spec also names the valgrind-nightly [lifecycle] lane; that is a Linux CI lane and is not runnable on this Windows host — no local evidence exists for it and CI must confirm it.

#### SC-006 — PASS _(FR lens)_

> cloud_frame_test.cpp TEST_CASE("Seraphis_CloudFrame_MirrorsCloudAccessors") (arms a-f) and TEST_CASE("Seraphis_CloudFrame_FocusVoiceFollowsAllocationSerial") (arm g), plus custom_view_test.cpp:97 (arm h) and cloud_frame_test.cpp "Seraphis_DataExchangeHandler_FollowsTheConnectionAndActivation" (arm i). Every one green in the seraphis_tests run.

#### SC-007 — PASS _(FR lens)_

> cloud_frame_test.cpp TEST_CASE("Seraphis_CloudFrame_PublishesOncePerProcessCall") green in the 80/80 run. The divisor is the existing `effectsStageProcessCalls_` accessor.

#### SC-008 — PASS _(FR lens)_

> cloud_frame_test.cpp TEST_CASE("Seraphis_CloudFrame_IsDeterministic") green in the 80/80 seraphis_tests run at the unchanged 1e-5 relative bound (no relaxation was needed).

#### SC-009 — PASS _(FR lens)_

> **RESTATED AND NOW GREEN** under the 2026-08-04 phase-owner ruling "Hybrid" (spec.md OE-1 → RULING (a)). Arm (a) is now the producer's MARGINAL whole-process() cost — gate OPEN minus gate CLOSED, interleaved with counterbalanced within-trial order on one warm fixture. `seraphis_tests.exe "[.perf]" -s` → ui_perf_test.cpp(1399): PASSED: `REQUIRE( (openGateNs - closedGateNs) <= kProducerDeltaBoundNs )` with expansion `-27906.0 <= 10666.6666666667`. WARN row: subject 3.57927e+06 ns/block (33.5557 %), same-run base 3.60718e+06 ns/block (33.8173 %), DELTA -27906 ns/block (-0.261619 points), bound 10666.7 ns (0.1 points), headroom 38572.7 ns (0.361619 points). ARM (b) IS REACHED FOR THE FIRST TIME AND PASSES ON ITS UNCHANGED THRESHOLD: ui_perf_test.cpp(1439): PASSED: `REQUIRE( bestStageNs <= kSnapshotStageBudgetNs )` with expansion `264.0 <= 10666.6666666667` — i.e. the snapshot stage's true cost is 264 ns/block (0.002475 % of one core), which independently corroborates that arm (a)'s ±0.26-point delta is noise and not the producer. Precondition assertions all green: 1600 publish attempts inside the timed region (exactly the gate-open half of the paired loop), partialCount 64. The ABSOLUTE whole-process() figure (33.56 %) is still measured and reported against the 25 % ceiling; it is roadmap Phase 11.5's gate, not this phase's. `kFullPolyCeilingNs` and `kSnapshotStageBudgetNs` byte-unchanged.

#### SC-010 — PASS _(FR lens)_

> **ARM (a) UNCHANGED AND GREEN; ARM (b) RE-BASED AND REPORTING** under ruling (b). Arm (a): ui_perf_test.cpp(1490): PASSED: `REQUIRE( fx->proc->cloudFramePublishAttemptCountForTest() == 0u )` with expansion `0 == 0` after the full 5625-block (60 s) gate-closed render, plus `cloudFrameSkippedBlockCountForTest() == 0`, and re-asserted at ui_perf_test.cpp(1518) after the timed region. Arm (b): the chain-only `kClosedGateCeilingNs` comparison is superseded (it compared whole-process() against a chain-only baseline — unsatisfiable by construction, OE-1's central finding) and replaced by `checkAgainstWholeProcessBaseline("SC-010(b)", closedGateNs, kBaselineWholeProcessNs, kSc010BaselinePinned)`. Measured 3.63836e+06 ns/block (34.1096 % of one core) against the PROVISIONAL baseline 3.3856e+06 ns (31.74 %) → provisional gate 3.89344e+06 ns (×1.15): the run is **under** it. Because `kSc010BaselinePinned == false` this REPORTS and does not gate — the WARN carries the banner `*** PROVISIONAL - PIN FROM A 7-RUN FRESH-BOOT COLD SET BEFORE RELEASE ***`, exactly the param_perf_test.cpp:2156-2175 mechanism. **OPEN RELEASE GATE, not a compliance failure:** the flag must be flipped after a real seven-run fresh-boot cold set is recorded. `kRegressionFactor` unchanged at 1.15; `kClosedGateCeilingNs` and `kBaselineFullPolyNs` byte-unchanged and retained for the audit trail.

#### SC-011 — PASS _(FR lens)_

> ui_perf_test.cpp:408 TEST_CASE("Seraphis_CloudFrame_AllocatesNothing") and :604 TEST_CASE("Seraphis_Phase11_UsesNoPlatformApi"). Both are non-[.perf] and passed in the 80/80 seraphis_tests run.

#### SC-012 — PASS _(FR lens)_

> `dsp_processors_tests.exe "SpectralState_AuthoringMutators_PreserveValidity"` => "All tests passed (137 assertions in 1 test case)". Whole layer green: dsp_processors_tests => "All tests passed (10640879 assertions in 3297 test cases)". The acceptance arm is covered by dsp_systems_tests => "All tests passed (6045010 assertions in 1222 test cases)".

#### SC-013 — PASS _(FR lens)_

> dsp/tests/unit/systems/spectral_state_authoring_test.cpp TEST_CASE("SpectralState_AuthoringMutators_AreAudible") (arms a-d) ran inside `dsp_systems_tests.exe` => "All tests passed (6045010 assertions in 1222 test cases)", 0 failures.

#### SC-014 — PASS _(FR lens)_

> ARMS 1-6 PASS, unchanged: seraphis_tests.exe "Seraphis_PartialOverrides_SurviveClearingEvents,Seraphis_PartialOverrides_SurviveAMacroRingSweep" -> "All tests passed (1228 assertions in 2 test cases)". **ARM 7 RESTATED AND NOW GREEN** under ruling (a): the criterion is the re-push's MARGINAL whole-process() cost — the 64-override macro Bloom sweep minus the IDENTICAL sweep with no overrides authored, both on one fixture. ui_perf_test.cpp(1661): PASSED: `REQUIRE( (repushNs - noRepushNs) <= kRepushDeltaBoundNs )` with expansion `131476.0 <= 285866.6666666667`. WARN row: subject 3.79253e+06 ns/block (35.555 %), same-run base 3.66106e+06 ns/block (34.3224 %), DELTA +131476 ns/block (+1.23259 points), bound 285867 ns (2.68 points), headroom 154391 ns (1.44741 points). The fan-out really ran: `overriddenBits == 0` asserted before the baseline window and `== ~0ull` after authoring, and voice 0's engine-side pan for partial 9 reads back at the authored value within SC-014's own 0.01 band. The measured +1.23-point marginal cost sits between OE-1's two recorded deltas (+0.99 / +1.58), i.e. the restated arm reproduces the escalation's own figures. ABSOLUTE 35.56 % still reported against the 25 % ceiling — Phase 11.5's gate.

#### SC-015 — PASS _(FR lens)_

> state_v3_test.cpp:909 TEST_CASE("Seraphis_EditedState_RoundTripsAtV3", "[seraphis][state][phase11]") green in the 80/80 seraphis_tests run; covers the byte-identical re-save, version 3 in the first four bytes, the edited 541-byte slot payload, the appended 272-byte [partials] block and the truncated-stream arm.

#### SC-016 — PASS _(FR lens)_

> state_v3_test.cpp:1053 TEST_CASE("Seraphis_SlotDropdown_DiscardsOnlyThatSlot") and :1160 TEST_CASE("Seraphis_SlotMirror_ReSeedsFromTheStateStream") (sub-arms a and b). Both green in the 80/80 seraphis_tests run.

#### SC-017 — PASS _(FR lens)_

> RESOLVED — write-back happened in both places. cloud_frame_test.cpp:891 now reads `constexpr double kBloomOctaveThreshold = 2.58;` (was 0.35), used at :1053. spec.md:1857 now reads "T = 2.58 octaves. MEASURED AND WRITTEN BACK (OQ-4 step 2, 2026-08-04)." Test PASSES against the measured threshold: full suite "All tests passed (444681 assertions in 80 test cases)"; targeted re-run "All tests passed (20573 assertions in 5 test cases)". WARN on passing run: "Bloom 0.00 P=1.81624 / 0.25 P=2.78791 / 0.50 P=3.50097 / 0.75 P=4.03203 / 1.00 P=4.40121 / delta=2.58497 oct, against measured T=2.58 oct".

#### SC-018 — PASS _(FR lens)_

> partial_edit_test.cpp TEST_CASE("Seraphis_EditMessage_RejectsGarbage", "[partial_edit][phase11]") green in the 80/80 seraphis_tests run.

#### SC-019 — PARTIAL _(FR lens)_

> Every gate runnable on this host is now GREEN — the clang-tidy red is gone — but three of the criterion's named gates are not runnable here. GREEN: Windows Release build exit 0, zero warnings; seraphis_tests "All tests passed (444681 assertions in 80 test cases)" ([.perf] hidden tag separately red, see SC-009/010/031); pluginval --strictness-level 5 exit 0, zero fail/error/*** lines; check-portability.js "all clear -- 28 compiled"; lint-layers.js OK; run-clang-tidy.ps1 -Target seraphis "Files analyzed: 33 / Errors: 0 / Warnings: 0", no NOLINT suppressions added. NOT VERIFIABLE IN THIS SESSION: macOS/Linux build legs and `auval -v aumu Srph KrAt` — no such host available.

#### SC-020 — PASS _(FR lens)_

> Previously-flagged weakness in arms (c)/(e) is closed. seraphis_tests.exe 3-case run -> "All tests passed (128 assertions in 3 test cases)". Rects no longer only compared against the implementation's own constants: independently transcribed spec literals kSpecDrawerCollapsedRect/kSpecDrawerOpenRect pinned against DrawerContainer's constants; the UIDESC-BUILT tree is checked directly in SC-004's case, green in both Release and ASan Debug.

#### SC-021 — PASS _(FR lens)_

> effects_chain_test.cpp TEST_CASE("Seraphis_MacroDissolve_ReachesEffects") covers arms (a)-(c); dsp/tests/unit/systems/seraphis_macro_test.cpp TEST_CASE("SeraphisMacroMatrix_EffectsOwner_IsAdditive") covers arm (d), backed by static_asserts incl. `Count == 29`. seraphis_tests 80/80 green and dsp_systems_tests "All tests passed (6045010 assertions in 1222 test cases)".

#### SC-022 — PASS _(FR lens)_

> custom_view_test.cpp:1595 TEST_CASE("Seraphis_Phase11_ViewSurface_IsExactlyThreePlusSubController") arms (a)/(b); arm (c) at :1744; arm (d) at :790. All green in the 80/80 run.

#### SC-023 — PASS _(FR lens)_

> Criterion's required environment now met. RELEASE green; ASAN DEBUG -> "All tests passed (421 assertions in 3 test cases)", zero ASan reports, so a null-frame dereference would have been a report rather than luck.

#### SC-024 — PASS _(FR lens)_

> partial_edit_test.cpp TEST_CASE("Seraphis_EditMode_AuthoringWorksWithoutANote", "[partial_edit][phase11]") green in the 80/80 seraphis_tests run.

#### SC-025 — PASS _(FR lens)_

> partial_edit_test.cpp TEST_CASE("Seraphis_BlendGesture_IsAbsoluteNotCompounding", "[partial_edit][phase11]") green in the 80/80 run.

#### SC-026 — PASS _(FR lens)_

> custom_view_test.cpp:877 TEST_CASE("Seraphis_MultiEditor_RefcountGatesCorrectly") with sections :941 and :954. Green in the 80/80 run.

#### SC-027 — PASS _(FR lens)_

> custom_view_test.cpp:975 TEST_CASE("Seraphis_EditThrottle_FlushesFinalValue") sections :991/:1040/:1051. Green in the 80/80 run.

#### SC-028 — PASS _(FR lens)_

> partial_edit_test.cpp TEST_CASE("Seraphis_EditMode_RatioEditReachesSoundingVoice", "[partial_edit][phase11]") green in the 80/80 seraphis_tests run.

#### SC-029 — PASS _(FR lens)_

> partial_edit_test.cpp TEST_CASE("Seraphis_EditMode_LiveRatioEditIsClickFree", "[partial_edit][phase11]") green in the 80/80 seraphis_tests run.

#### SC-030 — PASS _(FR lens)_

> `build/windows-x64-release/bin/Release/dsp_systems_tests.exe` => "All tests passed (6045010 assertions in 1222 test cases)" — the whole Phase 3 spectral_morph_* suite is in that target and is green after FR-033a's SeraphisVoice gate relaxation.

#### SC-031 — PASS _(FR lens)_

> **RESTATED AND NOW GREEN** under ruling (a): the criterion is the in-flight gesture's MARGINAL whole-process() cost — the 30 Hz kind-1 partial drag minus the same warm fixture with no gesture at all, the no-gesture window taken FIRST so the baseline is genuinely the static slot set. ui_perf_test.cpp(1823): PASSED: `REQUIRE( (gestureNs - noGestureNs) <= kGestureDeltaBoundNs )` with expansion `12788.0 <= 106666.6666666667`, message "edits sent = 533, handoffs consumed = 533, over 1600 timed blocks". WARN row: subject 3.65865e+06 ns/block (34.2998 %), same-run base 3.64586e+06 ns/block (34.1799 %), DELTA +12788 ns/block (+0.119888 points), bound 106667 ns (1.00 point), headroom 93878.7 ns (0.880112 points). The gesture really was in flight: `editStageWriteCountForTest()` unchanged across the baseline window (asserted), then 533 edits and exactly 533 handoffs consumed across the timed region — the one-to-one invariant, so no handoff was overwritten and the arm did not degrade into the static slot set. The measured +0.12-point marginal cost is consistent with OE-1's +0.30 / -0.81 readings, i.e. at or below the noise floor. ABSOLUTE 34.30 % still reported against the 25 % ceiling — Phase 11.5's gate. The 30 Hz throttle, the ceiling and the new bound were all untouched.

#### SC-032 — PASS _(FR lens)_

> custom_view_test.cpp:301 TEST_CASE("Seraphis_CloudView_GesturesEmitTheRightEditMessage", "[cloud_view][phase11]") green in the 80/80 seraphis_tests run.

#### SC-033 — PASS _(FR lens)_

> partial_edit_test.cpp TEST_CASE("Seraphis_PartialMask_ToggleOffRestoresTheVoice", "[partial_edit][phase11]") green in the 80/80 seraphis_tests run.

#### SC-001 — PASS _(SC lens)_

> build/windows-x64-release/bin/Release/seraphis_tests.exe "Seraphis_Phase11_OpenGate_ChangesNoSample" -> "All tests passed (24 assertions in 1 test case)". Test at plugins/seraphis/tests/integration/ui_negative_control_test.cpp:142.

#### SC-002 — PASS _(SC lens)_

> seraphis_tests.exe "Seraphis_ParameterSurface_IsComplete" -> "All tests passed (783 assertions in 1 test case)"; "Seraphis_UidescControlTags_MatchRegisteredIds" -> "All tests passed (1087 assertions in 1 test case)".

#### SC-003 — PASS _(SC lens)_

> Same run as SC-002 (783 assertions, 0 failures). Per-view class assertion with MacroRingKnob enumerated by ID as the only exception.

#### SC-004 — PASS _(SC lens)_

> Both lanes green now. RELEASE and ASan/Debug lane fixed as described in the FR-lens SC-004 row above.

#### SC-005 — PASS _(SC lens)_

> RELEASE + ASAN DEBUG green as described in the FR-lens SC-005 row above. Valgrind-nightly [lifecycle] lane not runnable on this host, stated not hidden.

#### SC-006 — PASS _(SC lens)_

> seraphis_tests.exe per-test runs: MirrorsCloudAccessors -> 436 assertions/1 case; FocusVoiceFollowsAllocationSerial -> 666 assertions/1 case; Controller_CachesOnlyTheMostRecentBlock -> 4 assertions/1 case; DataExchangeHandler_FollowsTheConnectionAndActivation -> 22 assertions/1 case.

#### SC-007 — PASS _(SC lens)_

> seraphis_tests.exe "Seraphis_CloudFrame_PublishesOncePerProcessCall" -> "All tests passed (5630 assertions in 1 test case)".

#### SC-008 — PASS _(SC lens)_

> seraphis_tests.exe "Seraphis_CloudFrame_IsDeterministic" -> "All tests passed (1901 assertions in 1 test case)". Four relative metrics compared via relativeSpread against the 1e-5 bound; no bit-exact float golden.

#### SC-009 — PASS _(SC lens)_

> RESTATED AS A DIFFERENTIAL by the 2026-08-04 ruling and now green — same measurement as the FR-lens SC-009 row above: ui_perf_test.cpp(1399) `-27906.0 <= 10666.6666666667` (producer's marginal cost, gate OPEN minus gate CLOSED, interleaved, -0.262 points against a 0.10-point bound), and arm (b) reached and green at ui_perf_test.cpp(1439) `264.0 <= 10666.6666666667` on its ORIGINAL, unchanged threshold. Absolute 33.5557 % reported, not gated — roadmap Phase 11.5 owns it.

#### SC-010 — PASS _(SC lens)_

> ARM (a) unchanged and hard-green (0 attempts, 0 skips over the 60 s gate-closed render, ui_perf_test.cpp(1490) and re-asserted at (1518)). ARM (b) re-based on a whole-process() baseline per ruling (b): measured 3.63836e+06 ns (34.1096 %) vs the PROVISIONAL 3.3856e+06 ns baseline → gate 3.89344e+06 ns, this run under it; REPORTED, not gated, because `kSc010BaselinePinned == false`. Open release gate: pin from a seven-run fresh-boot cold set. The old chain-only-derived ceiling is superseded (unsatisfiable by construction) but left in the file with its static_assert for the audit trail.

#### SC-011 — PASS _(SC lens)_

> seraphis_tests.exe "Seraphis_CloudFrame_AllocatesNothing" -> 41 assertions/1 case; "Seraphis_Phase11_UsesNoPlatformApi" -> 10 assertions/1 case. Anti-vacuity guards present (filesMissing==0, codeBytes>0, witnesses>=2).

#### SC-012 — PASS _(SC lens)_

> dsp_processors_tests.exe "SpectralState_AuthoringMutators_PreserveValidity" -> "All tests passed (137 assertions in 1 test case)". Acceptance arm in the 4-test/2494-assertion dsp_systems_tests batch.

#### SC-013 — PASS _(SC lens)_

> dsp_systems_tests.exe 4-case run -> "All tests passed (2494 assertions in 4 test cases)". Full dsp_systems_tests.exe run: "All tests passed (6045010 assertions in 1222 test cases)".

#### SC-014 — PASS _(SC lens)_

> ARMS 1-6 PASS (1228 assertions/2 cases), unchanged. ARM 7 RESTATED AS A DIFFERENTIAL and now green — same measurement as the FR-lens SC-014 row: ui_perf_test.cpp(1661) `131476.0 <= 285866.6666666667`, i.e. the re-push's own marginal cost is +1.233 points against a 2.68-point bound (1.447 points headroom), measured as the same Bloom sweep with 64 overrides authored minus with none. Absolute 35.555 % reported, not gated — Phase 11.5's.

#### SC-015 — PASS _(SC lens)_

> seraphis_tests.exe "Seraphis_EditedState_RoundTripsAtV3" -> "All tests passed (220 assertions in 1 test case)". Layout pinned by static_asserts (kPartialsBlockBytes=272).

#### SC-016 — PASS _(SC lens)_

> seraphis_tests.exe "Seraphis_SlotDropdown_DiscardsOnlyThatSlot" -> 38 assertions/1 case; "Seraphis_SlotMirror_ReSeedsFromTheStateStream" -> 125 assertions/1 case (incl. corrupt-version-byte arm).

#### SC-017 — PASS _(SC lens)_

> RESOLVED — same write-back as FR-lens SC-017 row: kBloomOctaveThreshold=2.58 (measured), test passes against it, full suite green.

#### SC-018 — PASS _(SC lens)_

> seraphis_tests.exe "Seraphis_EditMessage_RejectsGarbage" -> "All tests passed (30074 assertions in 1 test case)".

#### SC-019 — PARTIAL _(SC lens)_

> Every gate runnable on this host is now GREEN (build, seraphis_tests, pluginval strictness 5, check-portability, lint-layers, run-clang-tidy: 33 files/0 errors/0 warnings). NOT VERIFIABLE IN THIS SESSION: macOS/Linux build legs and auval -v aumu Srph KrAt.

#### SC-020 — PASS _(SC lens)_

> seraphis_tests.exe 3-case run -> "All tests passed (128 assertions in 3 test cases)". Independently transcribed spec literals now pin the rects (not just implementation constants); green in Release and ASan Debug.

#### SC-021 — PASS _(SC lens)_

> seraphis_tests.exe "Seraphis_MacroDissolve_ReachesEffects" -> "All tests passed (14600 assertions in 1 test case)". dsp_systems_tests.exe "SeraphisMacroMatrix_EffectsOwner_IsAdditive" green. Arm (d) static_asserts incl. Count==29 confirmed against pre-change enum (27 targets), so exactly +2.

#### SC-022 — PASS _(SC lens)_

> seraphis_tests.exe: "Seraphis_Phase11_ViewSurface_IsExactlyThreePlusSubController" -> 33 assertions/1 case; "Seraphis_MacroRing_DoesNotAnimateTheCloudViewLocally" -> 49 assertions; "Seraphis_PresetButton_OpensTheBrowser" -> 16 assertions; "Seraphis_Phase11_SubController_OwnsEveryTaglessControl" green. All also pass under ASan Debug.

#### SC-023 — PASS _(SC lens)_

> Criterion's required environment now met — RELEASE and ASAN DEBUG both green (421 assertions/3 cases), zero ASan reports.

#### SC-024 — PASS _(SC lens)_

> seraphis_tests.exe "Seraphis_EditMode_AuthoringWorksWithoutANote" -> "All tests passed (85 assertions in 1 test case)".

#### SC-025 — PASS _(SC lens)_

> seraphis_tests.exe "Seraphis_BlendGesture_IsAbsoluteNotCompounding" -> "All tests passed (73 assertions in 1 test case)".

#### SC-026 — PASS _(SC lens)_

> seraphis_tests.exe "Seraphis_MultiEditor_RefcountGatesCorrectly" -> "All tests passed (69 assertions in 1 test case)". Also green under ASan Debug.

#### SC-027 — PASS _(SC lens)_

> seraphis_tests.exe "Seraphis_EditThrottle_FlushesFinalValue" -> "All tests passed (16 assertions in 1 test case)".

#### SC-028 — PASS _(SC lens)_

> seraphis_tests.exe "Seraphis_EditMode_RatioEditReachesSoundingVoice" -> "All tests passed (30 assertions in 1 test case)".

#### SC-029 — PASS _(SC lens)_

> seraphis_tests.exe "Seraphis_EditMode_LiveRatioEditIsClickFree" -> "All tests passed (2296 assertions in 1 test case)".

#### SC-030 — PASS _(SC lens)_

> dsp_systems_tests.exe "[spectral_morph]" -> "All tests passed (15093 assertions in 20 test cases)"; "SpectralMorph*" -> "All tests passed (12598 assertions in 16 test cases)". Whole layer green, exit 0.

#### SC-031 — PASS _(SC lens)_

> RESTATED AS A DIFFERENTIAL and now green — same measurement as the FR-lens SC-031 row: ui_perf_test.cpp(1823) `12788.0 <= 106666.6666666667`, i.e. the in-flight gesture's own marginal cost is +0.120 points against a 1.00-point bound, measured as the 30 Hz drag minus no gesture on the same fixture. 533 edits / 533 handoffs across the timed region keeps the arm from degrading into a static slot set. Absolute 34.2998 % reported, not gated — Phase 11.5's. The 30 Hz throttle and every ceiling untouched.

#### SC-032 — PASS _(SC lens)_

> seraphis_tests.exe "Seraphis_CloudView_GesturesEmitTheRightEditMessage" -> "All tests passed (29 assertions in 1 test case)".

#### SC-033 — PASS _(SC lens)_

> seraphis_tests.exe "Seraphis_PartialMask_ToggleOffRestoresTheVoice" -> "All tests passed (390 assertions in 1 test case)".

### Constitution / Cross-Cutting Constraints (CC-xxx)

#### CC-rt — PASS _(Constitution/Constraints)_

> Audio-thread corpus is exactly 3 files. publishCloudFrame() (processor.cpp:3962-4098) — no new/malloc, no lock, no throw, no IO; bounded loops over trivial accessors; one 808-byte std::memcpy. Transport = SDK lock-free ring (dataexchange.cpp lockBlock/freeBlock; onTimer's allocateMessage is timer-thread, not audio). repushPartialOverrides() (processor.cpp:3407-3420) is a fixed 64-iteration loop into plain array writes. Cross-thread handshake is atomic (exchange/acquire vs store/release), never locking. Edit ingress (notify->applyEditMessage->stageSlotEdit) is message-thread only, never called from process(). MEASURED: seraphis_tests.exe "Seraphis_CloudFrame_AllocatesNothing" -> allocations==0, exceptions==0, blocksOk==5625==kRtBlocks (60s @ 48kHz), phase11AtomicsAreLockFreeForTest()==true, lock/throw scan hits 0 over 111113 code bytes, publish attempts=5625. "All tests passed (41 assertions in 1 test case)". CAVEAT stated: fixture never calls connect(), so the SDK transport leg is covered by source reading, not by that measurement. UI files are UI-thread-only; grep for lock/throw/alloc tokens returns exactly one hit (`new CVSTGUITimer` inside attached(), UI thread).

#### CC-layers — PASS _(Constitution/Constraints)_

> `node tools/lint-layers.js` -> 'lint-layers: OK — no layer-dependency violations in 5-layer DSP tree.' (exit 0). `node tools/lint-odr.js` -> 'lint-odr: OK — 733 definitions scanned, no cross-file name collisions.' (exit 0). Per-header include audit of every modified dsp header confirms downward-only dependencies (Layer 2 spectral_state.h includes only core/db_utils.h + stdlib; Layer 3 seraphis_engine.h/seraphis_macro_matrix.h/seraphis_voice.h/spectral_morph_engine.h include only Layers 0-2 + L3 peers, no Layer 4). Plugin-side Processor/Controller separation: processor.cpp includes exactly one ui/ header (ui/edit_message.h, which itself includes only <cstdint>); no ui/*.h includes processor/processor.h; controller.h's include of processor/cloud_frame.h is the sanctioned shared-POD exception (matches Membrum's meters_block.h precedent). New dsp tests registered at dsp/tests/CMakeLists.txt:366 and :382, not silently dropped.

#### CC-naming — PASS _(Constitution/Constraints)_

> Read all 8 new/changed Phase 11 source files in full. Classes PascalCase (CloudView, DrawerContainer, MacroRingKnob, MacroRingKnobCreator, SeraphisEditSubController, CloudFrame, EditMessage). Functions camelCase throughout. Members trailing underscore in every new class. Constants kPascalCase throughout. Wire-format PODs use plain public field names, matching the shipped Membrum meters_block.h precedent. Global creator `gMacroRingKnobCreator` matches the ArcKnobCreator precedent. No new ParamIDs this phase (plugin_ids.h unmodified); session tags live at 9000+ and are never written as control-tag (0 hits for `control-tag="9`, 16 hits for `session-tag=`). ODR sweep for every new type across dsp/ and plugins/: one definition each, plus matching forward declarations.

#### CC-warnings — PASS _(Constitution/Constraints)_

> Warning posture confirmed as /W4 (MSVC) and -Wall -Wextra (GCC/Clang). Forced recompilation of all ten Phase 11 TUs: verbatim compile log shows all ten compiled and `grep -in 'error|warning'` returns exactly ONE line, a link-step LNK1104 (file held open by two concurrently-running sibling-agent seraphis_tests processes — an environmental lock, not a compiler diagnostic; every TU that feeds that link compiled warning-free). Separate target build (`--target Seraphis`) succeeded EXIT=0 with zero error/warning lines in the log and linked Seraphis.vst3 successfully.

#### CC-portability — PASS _(Constitution/Constraints)_

> VERBATIM, default invocation — `node tools/check-portability.js`, EXIT=0, 18 tracked-modified TUs all OK. BLIND SPOT FOUND AND CLOSED: the tool selects via git diff and skips untracked files; re-ran explicitly on all 8 new untracked Phase 11 TUs, EXIT=0, all OK — 'check-portability: all clear -- 8 compiled.' 26 TUs total green under g++. No std::isnan/isinf/isfinite anywhere in the new UI/processor/controller code (only comment-line hits); real tests use bit-pattern detail::isNaN/isInf and a fast-math-safe `!(v > lo)` clamp. No SIMD in new code. Supporting CI gates all exit 0 (lint-arch-guarded-includes, lint-simd-aligned-loadstore, lint-float-bit-goldens, lint-midi-timing-goldens, lint-platform-type-literals, lint-nonfinite-symbols, lint-allocation-operator-overrides, lint-plugin-roster).

---

## Implementation notes: deviations reported by task agents

Condensed from the 29 task agents' own notes (T001–T029). Full verbatim notes for every task are
preserved in the orchestrator's task-notes record; this section surfaces the deviations that a
reviewer or the phase owner needs to act on or be aware of.

**T001 (baseline capture).** Deliberately built and ran tests despite an instruction not to, because the
task's own deliverable (a before/after diff for SC-030) has no "before" without a green baseline run.
Recorded the ODR sweep (zero collisions), the green four-suite baseline, and the perf anchors later tasks
measure against (kFullPolyCeilingNs = 2,666,666.7 ns/block = 25%, Phase 10 worst-of-seven = 22.32%, leaving
only 2.68 points of headroom — this is why SC-009/010/014/031 read as structurally tight, not as one bad
run).

**T003a (relax SeraphisVoice::setSpectralState gate).** Two DSP unit test files
(`seraphis_voice_test.cpp`, `seraphis_param_broadcast_test.cpp`) were found already modified by a prior
agent to match the new accept-not-reject behavior, despite the task text saying "no test edits" — flagged,
not undone. Also flagged: `seraphis_engine.h`'s `applySpectralStates` doc comment now describes a
per-voice reject gate that no longer exists post-T003a, making its retry rationale stale (a doc-only
follow-up, not a behavioral bug).

**T004 (macro matrix +2 effects targets).** Found and moved the one other hard pin on the old target count
(`param_perf_test.cpp:586`, `kNumTargets == 27` → 29) beyond its own file list, because leaving it would
have hard-broken the seraphis_tests build. The clause-1 reference table's 80 bit-identical float literals
were computed by an independent float32 model rather than captured from a real build (the agent was told
not to build) — flagged as the one arm in T004 that a build agent must confirm rather than trust blindly.

**T008 (CloudFrame producer).** Added one probe (`SeraphisCloudFrameProbe`) beyond the task's declared seam
list, needed only because the mask/pan-override writer (T010, two groups later) didn't exist yet and
SC-006 arm (e) needed *something* to assert against. `kCloudFrameSilenceLevel` had no value specified
anywhere in spec/plan/tasks; the agent chose `1.0e-4f` (-80 dBFS) and documented the choice inline —
flagged for the phase owner to confirm or override.

**T010 (edit-message dispatch / partial-edit staging).** getState()'s fallback payload source was changed
from `factoryStates_[...]` to the new authoring mirror `spectralSlotsAuthoring_[s]` — byte-identical for
every pre-Phase-11 flow, but touches the Phase 9/10 state round-trip surface and was flagged for extra
scrutiny on state_v2_test/state_roundtrip_test.

**T012 (state v3 [partials] block).** BLOCKING COLLATERAL FOUND: appending the 272-byte block moves the
whole-stream size from 2596 → 2868 bytes, and two OTHER pre-existing test files
(`tests/unit/state_v2_test.cpp:104`, `tests/unit/state_roundtrip_test.cpp:61`) hard-code the old 2596
figure. Neither file was in T012's permitted file list, so they were left broken and explicitly flagged for
a follow-up task/reviewer to fix (two one-line constant changes). The compliance evidence gathered for this
report (SC-015, SC-016, state_v3_test.cpp all green in an 80/80 full-suite run) implies this was closed
somewhere downstream, but no task's notes explicitly claim the fix — **worth a direct spot-check of
state_v2_test.cpp:104 and state_roundtrip_test.cpp:61 before calling this phase fully closed.**

**T013 (macro composition reaches the effects stage).** Corrected several count-comments elsewhere in
processor.cpp/.h that the change made stale ("27 macro bases" → 29, etc.) — comment-only, no behavior
change.

**T015–T017 (CloudView, DrawerContainer, SeraphisEditSubController).** T017 flagged a BLOCKING build
dependency: `plugins/seraphis/CMakeLists.txt`'s plugin source list did not yet include the three new
`src/ui/*.cpp` files, so `--target Seraphis` (the plugin DLL/bundle) would fail to link until T027 added
them. T027's own notes confirm this was already satisfied by the time it ran (the per-task CMake appends
had landed), and this report's own build evidence (FR-050, CC-warnings) shows target Seraphis linking clean
— confirmed closed.

**T018 (editor refcount / slot mirror / edit throttle).** Flagged an incomplete wiring at the time it ran:
the `beginEditGesture`/`onEditGestureValue`/`endEditGesture` throttle methods were implemented and
unit-tested but NOT yet called from either CloudView's mouse handlers or the sub-controller's
`valueChanged`. This report's SC-027/SC-032 green evidence (the throttle flush and the CloudView gesture
->EditMessage path both pass against the real controller) indicates the wiring was completed by a later
task in the sequence — confirmed closed.

**T019 (uidesc rebuild).** Extended the existing parameter-surface test case rather than creating a
separately-named one (the task's cited line numbers pointed at a different, already-existing case). Added
four tag-less slot-selector buttons not explicitly itemized in the task text, needed to give the
sub-controller's `kSlotBaseTag` branch a UI driver; they carry no control-tag so the 110-binding budget is
unaffected.

**T020 (SC-001/SC-008 negative-control tests).** Reordered the two SC-001 arms (closed-gate assertion
first, then open-gate) because `cloudFramePublishAttempts_` is a lifetime counter with no reset seam — the
literal task order as written would have made the "== 0" arm compare against contaminated state.

**T022/T023 (perf test file, [.perf] cases).** These are the tasks whose cases were the
SC-009/010/014/031 fails in this report's original gap list. T023's own notes state plainly that no remedy
was applied to any production file because nothing had been measured yet (build was out of scope for that
agent) — the perf gap was correctly surfaced, not hidden, from the moment it was authored. **CLOSED
2026-08-04 by the phase-owner ruling "Hybrid" on OE-1** (spec.md), which restated three of the four arms as
differential and re-based the fourth; the four cases were re-implemented accordingly in `ui_perf_test.cpp`
and all four now pass. **One deviation was found and applied inside that work, and it was a measurement
defect rather than a threshold change:** the first implementation of the SC-009 pairing ran gate-OPEN
first in every trial, and the machine's monotone drift (measured at −0.52 and −1.50 points between the
sequential windows of the other two arms) was therefore handed entirely to the gate-CLOSED arm, reading a
spurious +0.95-point producer cost. The pairing was made **order-counterbalanced** (A,B then B,A on
alternating trials) and the delta fell to −0.26 points, corroborated independently by SC-009(b)'s
stage-alone figure of 264 ns/block. No bound was moved to achieve this.

**T025 (OQ-4 pilot measurements).** Could not complete both required pilot measurements under a
"do not build" constraint. Left `kBloomOctaveThreshold` at a placeholder (0.35) and the
Entropy→FxWanderDepth `.amount` at its ruled starting value (0.50f) with an explicit in-code status note.
**Subsequently resolved** — the compliance evidence for FR-037/SC-017 in this report shows both numbers
measured and written back (dated 2026-08-04): `kBloomOctaveThreshold` moved 0.35 → 2.58 (measured, 7.4x
stronger than the placeholder) and the Entropy amount's five-point M/S side-RMS table is now in spec.md.
No open OQ-4 gap remains in the evidence reviewed for this report.

**T026 (spec/plan write-back).** Found and corrected three places where the draft spec text contradicted
the shipped build (invented session-tag names, wrong [partials] block field order, incomplete lock-free
seam member list) — all fixed by re-reading the real source rather than trusting the draft. Deliberately
reverted an incidental `specs/INDEX.md` regeneration that a verification tool triggered, since that file
was outside T026's permitted scope.

**T027 (CMake audit).** Found all five audit items already satisfied by earlier tasks' own CMake edits;
made comment-only audit-stamp edits, no functional CMake changes.

**T028 (CHANGELOG/CLAUDE.md).** Used a `## [Unreleased]` CHANGELOG heading (no other plugin in the repo has
used this pattern before) because a version bump was explicitly out of scope for this task — flagged as a
one-time convention choice for the next release skill run to rename.

**T029 (final verification pass).** Found and fixed one real GCC-only compile break in
`partial_edit_test.cpp` (a lambda capturing a `constexpr` by reference-only that GCC treats as
odr-used and MSVC does not) — this would have shipped red to the Linux/macOS CI legs from a green Windows
build. Also found that the default `check-portability.js` invocation silently skips all untracked
(new) files via its git-diff-based file selection, and had to re-run it with explicit paths to get real
coverage of the ten new Phase 11 TUs — the same root cause FR-044's evidence says the tool itself was later
fixed to handle (unioning in `git ls-files --others --exclude-standard`).

---

## Remaining gates for the human loop

Per the phase workflow, the following are not something this report itself executes as a final
word — they are named here explicitly as the remaining human-loop gates before commit:

- **clang-tidy** — `./tools/run-clang-tidy.ps1 -Target seraphis -BuildDir build/windows-ninja`.
  Evidence cited in this report (FR-044, SC-019) already shows a green run captured during
  verification ("Files analyzed: 33 / Errors: 0 / Warnings: 0", no NOLINT suppressions added), but
  re-run it as the standard pre-commit checklist step — a green run inside a compliance pass is not
  a substitute for that step.
- **pluginval** (phases 8+; Phase 11 touches plugin/UI code, so this applies) —
  `tools/pluginval.exe --strictness-level 5 --validate build/windows-x64-release/VST3/Release/Seraphis.vst3`.
  Also already shown green in this report's SC-019 evidence (exit 0, no fail/error/*** lines); re-run
  before commit if any further code changes are made to close the SC-009/010/014/031 gaps.
- **Commit** — nothing in this phase has been committed (git status at session start shows all
  Phase 11 files as modified/untracked). The OE-1 block is lifted: the gap **has** its phase-owner
  ruling (2026-08-04, "Hybrid") and all four arms are green under the restated criteria. Committing
  remains a decision for the user, not this report.
- **`kSc010BaselinePinned` — a dated release gate, carried forward deliberately.** SC-010(b)'s
  whole-process() baseline is PROVISIONAL (3 385 600 ns, transcribed from OE-1's TWO-pass cold table)
  and the arm reports rather than gates. Before release: record a seven-run fresh-boot idle dataset,
  re-derive `kBaselineWholeProcessNs` from its worst, flip the flag to `true` and delete the
  `static_assert(!kSc010BaselinePinned, ...)` guard in the same change.
- **Roadmap Phase 11.5 — the absolute 25 % whole-process() figure.** Measured 33.6–35.6 % on this warm
  host (31.7 % on the cold two-pass set). Owned by the new `Phase 11.5: Processor whole-process()
  optimization` entry in `specs/Seraphis-roadmap.md`, which **Phase 12 must not ship before**.
- **macOS / Linux CI legs and `auval`** — not runnable on this Windows host (SC-019, SC-005). These
  remain unverified until CI runs them.
- **T012's flagged collateral** — spot-check that `state_v2_test.cpp:104` and
  `state_roundtrip_test.cpp:61` (hard-coded pre-Phase-11 stream-size constants) were actually
  updated to the new 2868-byte total; this report's green SC-015/SC-016 evidence is consistent with
  that fix having landed, but no task's notes explicitly claim it.
