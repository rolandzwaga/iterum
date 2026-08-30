# Seraphis Phase 12 — Presets & Release: Compliance Report

**Spec**: `seraphis-phase12-presets-release`
**Report generated**: 2026-08-05
**Overall status**: **INCOMPLETE**

## Gap list (honest, most severe first)

Zero fail/partial items is the bar for COMPLETE. This phase has **5 FAIL** and **3 PARTIAL** item(s) (counting both the FR-lens and SC-lens passes over the same criteria as separate reviewer findings). None of these are code defects still open in the source tree — all are either measurement/verification work the executing agent cannot perform (no audio output, no macOS/CI runner), or a genuine unmet numeric threshold with no fix landed yet.

| # | ID | Verdict | Why it's not a clean pass |
|---|----|---------|-----------------------------|
| 1 | SC-025 | FAIL | Whole-`process()` CPU at the 8-voice operating point measures 31.74% (gate open) / 31.30% (gate closed) against a ≤25.00% ceiling — a +6.74-point breach. No post-Phase-11.5-optimization re-measurement exists anywhere in the tree (arms are `[.perf]`-tagged and were not exercised by any green suite run). Release verdict is explicitly recorded as DEFERRED because of this. |
| 2 | FR-034a | FAIL | Enforcing FR for the SC-029 listening checkpoint. Requires a human at a DAW; C-10 forbids an automated substitute. |
| 3 | SC-029 | FAIL (fr + sc lens) | 0 of 42 factory presets have a recorded audition outcome — the worksheet exists with all 42 rows honestly marked **NOT AUDITIONED**. Blocks release sign-off, not the codebase. |
| 4 | FR-029a | PARTIAL | Windows/MSVC and Linux/GCC legs are measured and the tolerance constants are pinned from real cross-toolchain data. The macOS/Apple-Clang leg and the mandated CI dry run have not been run — the shipped test still prints "macOS leg: NOT YET MEASURED". |
| 5 | SC-017 | PARTIAL (fr + sc lens) | Same macOS gap as FR-029a — the criterion requires Windows **and** macOS **and** Linux; only two of three legs are evidenced. |

None of the five gaps above requires new source code. Four require access this agent does not have (a macOS/Apple-Clang toolchain, a CI runner, a DAW with audio output) and are properly the phase owner's or CI's job. The fifth (SC-025) requires either a real performance-optimization pass on `Processor::process()` or an explicit decision to ship 0.5.0 with the DEFERRED verdict already recorded in this document.

## Compliance table

One row per reviewed item. Evidence is reproduced **verbatim** from the reviewing agent's findings — nothing softened, nothing paraphrased. Where the same criterion ID was independently reviewed under more than one lens (an FR/SC pass and a separate SC-focused re-review pass), each pass is a distinct row so neither is hidden.

| ID | Lens | Verdict |
|----|------|---------|
| FR-001 | FR/SC review | PASS |
| FR-002 | FR/SC review | PASS |
| FR-003 | FR/SC review | PASS |
| FR-004 | FR/SC review | PASS |
| FR-005 | FR/SC review | PASS |
| FR-006 | FR/SC review | PASS |
| FR-006a | FR/SC review | PASS |
| FR-007 | FR/SC review | PASS |
| FR-008 | FR/SC review | PASS |
| FR-008a | FR/SC review | PASS |
| FR-009 | FR/SC review | PASS |
| FR-010 | FR/SC review | PASS |
| FR-011 | FR/SC review | PASS |
| FR-012 | FR/SC review | PASS |
| FR-013 | FR/SC review | PASS |
| FR-014 | FR/SC review | PASS |
| FR-015 | FR/SC review | PASS |
| FR-016 | FR/SC review | PASS |
| FR-016a | FR/SC review | PASS |
| FR-017 | FR/SC review | PASS |
| FR-018 | FR/SC review | PASS |
| FR-019 | FR/SC review | PASS |
| FR-020 | FR/SC review | PASS |
| FR-021 | FR/SC review | PASS |
| FR-022 | FR/SC review | PASS |
| FR-023 | FR/SC review | PASS |
| FR-024 | FR/SC review | PASS |
| FR-024a | FR/SC review | PASS |
| FR-025 | FR/SC review | PASS |
| FR-025a | FR/SC review | PASS |
| FR-026 | FR/SC review | PASS |
| FR-027 | FR/SC review | PASS |
| FR-027a | FR/SC review | PASS |
| FR-027b | FR/SC review | PASS |
| FR-028 | FR/SC review | PASS |
| FR-028a | FR/SC review | PASS |
| FR-029 | FR/SC review | PASS |
| FR-029a | FR/SC review | PARTIAL |
| FR-030 | FR/SC review | PASS |
| FR-031 | FR/SC review | PASS |
| FR-032 | FR/SC review | PASS |
| FR-033 | FR/SC review | PASS |
| FR-034 | FR/SC review | PASS |
| FR-034a | FR/SC review | FAIL |
| FR-035 | FR/SC review | PASS |
| FR-035a | FR/SC review | PASS |
| SC-001 | FR/SC review | PASS |
| SC-002 | FR/SC review | PASS |
| SC-003 | FR/SC review | PASS |
| SC-004 | FR/SC review | PASS |
| SC-005 | FR/SC review | PASS |
| SC-006 | FR/SC review | PASS |
| SC-007 | FR/SC review | PASS |
| SC-008 | FR/SC review | PASS |
| SC-009 | FR/SC review | PASS |
| SC-010 | FR/SC review | PASS |
| SC-010a | FR/SC review | PASS |
| SC-011 | FR/SC review | PASS |
| SC-012 | FR/SC review | PASS |
| SC-013 | FR/SC review | PASS |
| SC-014 | FR/SC review | PASS |
| SC-014a | FR/SC review | PASS |
| SC-015 | FR/SC review | PASS |
| SC-015a | FR/SC review | PASS |
| SC-016 | FR/SC review | PASS |
| SC-017 | FR/SC review | PARTIAL |
| SC-018 | FR/SC review | PASS |
| SC-019 | FR/SC review | PASS |
| SC-020 | FR/SC review | PASS |
| SC-021 | FR/SC review | PASS |
| SC-022 | FR/SC review | PASS |
| SC-023 | FR/SC review | PASS |
| SC-024 | FR/SC review | PASS |
| SC-025 | FR/SC review | FAIL |
| SC-026 | FR/SC review | PASS |
| SC-027 | FR/SC review | PASS |
| SC-028 | FR/SC review | PASS |
| SC-029 | FR/SC review | FAIL |
| SC-001 | SC re-review | PASS |
| SC-002 | SC re-review | PASS |
| SC-003 | SC re-review | PASS |
| SC-004 | SC re-review | PASS |
| SC-005 | SC re-review | PASS |
| SC-006 | SC re-review | PASS |
| SC-007 | SC re-review | PASS |
| SC-008 | SC re-review | PASS |
| SC-009 | SC re-review | PASS |
| SC-010 | SC re-review | PASS |
| SC-010a | SC re-review | PASS |
| SC-011 | SC re-review | PASS |
| SC-012 | SC re-review | PASS |
| SC-013 | SC re-review | PASS |
| SC-014 | SC re-review | PASS |
| SC-014a | SC re-review | PASS |
| SC-015 | SC re-review | PASS |
| SC-015a | SC re-review | PASS |
| SC-016 | SC re-review | PASS |
| SC-017 | SC re-review | PARTIAL |
| SC-018 | SC re-review | PASS |
| SC-019 | SC re-review | PASS |
| SC-020 | SC re-review | PASS |
| SC-021 | SC re-review | PASS |
| SC-022 | SC re-review | PASS |
| SC-023 | SC re-review | PASS |
| SC-024 | SC re-review | PASS |
| SC-025 | SC re-review | FAIL |
| SC-026 | SC re-review | PASS |
| SC-027 | SC re-review | PASS |
| SC-028 | SC re-review | PASS |
| SC-029 | SC re-review | FAIL |
| CC-rt | Constitution constraint | PASS |
| CC-layers | Constitution constraint | PASS |
| CC-naming | Constitution constraint | PASS |
| CC-warnings | Constitution constraint | PASS |
| CC-portability | Constitution constraint | PASS |

### Evidence detail

#### FR-001 — FR/SC review — **PASS**

plugins/seraphis/src/preset/seraphis_preset_config.h:37-38 — `{"Textures", "Pads", "Drones", "Bells", "Choirs", "Motion", "Cinematic"}` in C-1 order, `Textures` byte-identical; :34-36 keeps `kProcessorUID` / "Seraphis" / "Synth" and designated-initializer field order. Verified green: `Seraphis_FactoryPresets_CategoriesMatchConfig` (factory_preset_test.cpp:390) in my 109-case run.

---

#### FR-002 — FR/SC review — **PASS**

`ls plugins/seraphis/resources/presets/` → exactly Bells, Choirs, Cinematic, Drones, Motion, Pads, Textures (7 dirs, no others); `find … -name '*.vstpreset' | wc -l` → 42, all directly inside a category dir. Asserted by factory_preset_test.cpp:390 (bijection) and :548.

---

#### FR-003 — FR/SC review — **PASS**

Info XML built by the single `PresetDefs::buildSeraphisInfoXml(def.name, def.category)` (tools/seraphis_preset_generator.cpp:297-298), written as the second `List` entry (:226-228). All six attribute equalities asserted green by `Seraphis_FactoryPresets_InfoMetadataMatchesDirectory` (factory_preset_test.cpp:959) in my run.

---

#### FR-004 — FR/SC review — **PASS**

Measured: 42 `.vstpreset` files, 6 in each of the 7 category dirs (per-dir `ls | wc -l` = 6 for all seven). Gated by `Seraphis_FactoryPresets_CountAndDistribution` (factory_preset_test.cpp:548), green.

---

#### FR-005 — FR/SC review — **PASS**

factory_preset_test.cpp:611-675 enumerates four failure buckets incl. `PresetManager::isValidPresetName(pf.stem)` (:628) and library-wide (not per-directory) uniqueness; `Seraphis_FactoryPresets_NamesAreValidAndUnique` (:602) green in my run.

---

#### FR-006 — FR/SC review — **PASS**

`Seraphis_FactoryPresets_StreamIsCurrentVersion` (factory_preset_test.cpp:789) green in my run; the 2868-byte constant is `kSeraphisStateBytes = 2868` (plugins/seraphis/tests/preset_test_support.h:92) and the decoder rejects any other size at :407-412.

---

#### FR-006a — FR/SC review — **PASS**

`Seraphis_FactoryPresets_PartialsBlockIsInert` (factory_preset_test.cpp:1454) green over 42 files. Generator has no IMessage/notify surface — `grep notify tools/seraphis_preset_generator.cpp` finds none; the file's banner states it at :42-46 and its only drive is the `fx.setParam` fan-out (:142-144).

---

#### FR-007 — FR/SC review — **PASS**

`Seraphis_FactoryPresets_CoversShippedSurface` (factory_preset_test.cpp:1211) green in my 109-case run — asserts the C-2 matrix against decoded state (via decodePresetState), not against the authoring table.

---

#### FR-008 — FR/SC review — **PASS**

tools/seraphis_preset_defs.h:100-107 — no entry touches ID 1 or 2, so registered defaults (polyphony 8, softLimit 1.0) are written unchanged. Gated green by `Seraphis_FactoryPresets_RespectVoiceBudget` (factory_preset_test.cpp:1357).

---

#### FR-008a — FR/SC review — **PASS**

`Seraphis_FactoryPresets_RespectTimingCeiling` (factory_preset_test.cpp:1397) green over 42 presets; the three Growth-mode entries set IDs 701 and 703 explicitly to stay under 12 s (defs banner rule 2, tools/seraphis_preset_defs.h:108-120).

---

#### FR-009 — FR/SC review — **PASS**

Non-finiteness is checked as a typed field enumeration, not a 4-byte walk (factory_preset_test.cpp:1460-1471 documents why); the TU is compiled with `-fno-fast-math -fno-finite-math-only` on Clang/GNU (plugins/seraphis/tests/CMakeLists.txt, diff hunk adding `unit/preset/factory_preset_test.cpp` to the PROPERTIES block). Case green.

---

#### FR-010 — FR/SC review — **PASS**

CMakeLists.txt:603-604 `add_executable(seraphis_preset_generator tools/seraphis_preset_generator.cpp …)`; :635-637 `set_target_properties(… RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")`. Built this session: build/windows-x64-release/bin/Release/seraphis_preset_generator.exe and build/linux-release/bin/seraphis_preset_generator.

---

#### FR-011 — FR/SC review — **PASS**

tools/seraphis_preset_generator.cpp:246-250 takes `argv[1]`; :261-270 creates all seven subdirs. Verified by running it: `./build/linux-release/bin/seraphis_preset_generator /tmp/sp-verify` → EXIT=0, "Generated 42 of 42 Seraphis factory presets.", 42 files, 7 directories.

---

#### FR-012 — FR/SC review — **PASS**

CMakeLists.txt:663-665 — `add_custom_target(generate_seraphis_presets COMMAND seraphis_preset_generator "${CMAKE_SOURCE_DIR}/plugins/seraphis/resources/presets" DEPENDS seraphis_preset_generator)`.

---

#### FR-013 — FR/SC review — **PASS**

tools/seraphis_preset_generator.cpp:155-159 calls the shipped `fx.proc->getState(&stream)`; class id derived at runtime at :111-115 via `Seraphis::kProcessorUID.toString(buf)`. `ls tools/seraphis_preset_format.h` → does not exist; no preset_format_compat_test for seraphis.

---

#### FR-014 — FR/SC review — **PASS**

`node tools/check-preset-generator-determinism.js` run this session: "OK — 42 file(s); 0 differing between two fresh runs, 0 changed by a third run over an existing tree." EXIT=0, 4 s.

---

#### FR-015 — FR/SC review — **PASS**

CMakeLists.txt:622 `target_link_libraries(seraphis_preset_generator PRIVATE KrateDSP KratePluginsShared sdk)` — no vstgui_support. Built and ran under WSL/GCC 13 this session: `cmake --build build/linux-release --target seraphis_preset_generator` BUILD_EXIT=0, binary emitted 42 presets. (Warnings are SC-018's problem, not this FR's.)

---

#### FR-016 — FR/SC review — **PASS**

tools/seraphis_preset_defs.h:53-56 `struct ParamSetting { ParamID id; double normalized; }`; :70-77 `SeraphisPresetDef` carries name/category/description/params/stimulus and **no** EditMessage list. Generator consumes only `s.id, s.normalized` (generator.cpp:142-144).

---

#### FR-016a — FR/SC review — **PASS**

tools/seraphis_preset_defs.h is data-only (banner :12-33), includes only plugin_ids.h + stdlib (:35-43), carries the optional `AuditionStimulus` override (:61-65), and is on the include path of both the generator (CMakeLists.txt:629 `${CMAKE_SOURCE_DIR}/tools`) and the FR-029 test TU (used at factory_preset_test.cpp for in-process regeneration).

---

#### FR-017 — FR/SC review — **PASS**

plugins/seraphis/CMakeLists.txt:113 `krate_plugin_install_presets(${PLUGIN_NAME})` — unchanged form, no SRC_SUBDIR/DEST_SUBDIR. Verified live: after my Release build, `C:\ProgramData\Krate Audio\Seraphis` holds 7 category dirs and 42 `.vstpreset` files.

---

#### FR-018 — FR/SC review — **PASS**

plugins/seraphis/installers/windows/setup.iss:68 is the sole preset line (`Source: "presets\*"; DestDir: "{commonappdata}\Krate Audio\Seraphis"`). Linux README:38 `~/Documents/Krate Audio/Seraphis/` and :44 `/usr/share/krate-audio/seraphis/` both match the **implementation** — preset_paths.cpp:45-49 lower-cases the plugin name on Linux (the header comment at preset_paths.h:26 is the thing that is wrong, not the README).

---

#### FR-019 — FR/SC review — **PASS**

`Seraphis_FactoryPresets_ContainerIsValid` (factory_preset_test.cpp:711) green over 42 files in my run (0.004 s).

---

#### FR-020 — FR/SC review — **PASS**

`Seraphis_FactoryPresets_RoundTripByteIdentical` (factory_preset_test.cpp:838) green, 0.997 s in my run — 42 setState/getState round-trips, 0 differing bytes.

---

#### FR-021 — FR/SC review — **PASS**

`Seraphis_FactoryPresets_InfoMetadataMatchesDirectory` (factory_preset_test.cpp:959) green in my run; all six attributes plus directory-name membership in `makeSeraphisPresetConfig().subcategoryNames`.

---

#### FR-022 — FR/SC review — **PASS**

`Seraphis_FactoryPresets_BrowserScanFilesEveryPreset` (factory_preset_test.cpp:1053) green in my run — count 42, non-empty subcategory, isFactory, per-category counts, against a PresetManager built with an empty temp userDirOverride.

---

#### FR-023 — FR/SC review — **PASS**

`Seraphis_PresetBrowser_TabsMatchConfig` (factory_preset_test.cpp:1145) green in my run — 8-element `{"All"} ∪ subcategoryNames` element-wise.

---

#### FR-024 — FR/SC review — **PASS**

Timeline is computed from decoded state, not hardcoded: preset_render_sweep_test.cpp:560-578 derives `susBegin/susEnd` and `settle + W` from `out.timeline`; stimulus resolution is mandatory (`unmatchedDefinitions(files)` REQUIRE-empty at :656-659, no silent fallback to 60/0.8). `Seraphis_PresetSweep_NoSilence` (:649) green, 93.701 s.

---

#### FR-024a — FR/SC review — **PASS**

preset_render_sweep_test.cpp:801 `spec.sampleRate = kRate44100; // FR-024a: 44 100 Hz only`, chord render asserted bounded-only. `Seraphis_PresetSweep_ChordBoundedAndFinite` (:783) green, 47.856 s in my run.

---

#### FR-025 — FR/SC review — **PASS**

preset_render_sweep_test.cpp:170-172 reuses the shipped constants verbatim — `kLimiterCeilingLin = 0.8912509f`, `kCeilingAllowanceDb = 0.1f`, `kPeakBound = kLimiterCeilingLin * pow(10, 0.1/20)`. `Seraphis_PresetSweep_BoundedAndFinite` (:710) green over both rates.

---

#### FR-025a — FR/SC review — **PASS**

The deviation is GONE — the payloads are now literally hand-skipped. plugins/seraphis/tests/preset_test_support.h:523 calls the SCALAR overload `Seraphis::loadMorphParams(out.morph, streamer)` (2-arg; declared morph_params.h:418) asserting cumulative 136, then :526-531 advances the stream by hand — `streamer.seek(kSpectralPayloadBlockBytes, kSeekCurrent)` — with the tripwire step "[morph payloads] (hand-skipped)" asserting tell()==2300. kSpectralPayloadBlockBytes/kSpectralPayloadOffset are re-derived at preset_test_support.h:107-114 with `static_assert(kSpectralPayloadOffset + kSpectralPayloadBlockBytes == 2300)`. Every other block goes through its shipped loader in getState() order with a cumulative tell() assertion at each step (:485-498 step lambda; :501-546), ending at kSeraphisStateBytes = 2868 (:92, :574). The [partials] block is read raw at fixed widths (:552-572), i.e. the streamer is advanced by its known 272 bytes and never through a load*Params pack — the mechanism FR-025a forbids for it. SpectralState payloads are decoded in a separate pass via the shipped `Krate::DSP::deserializeSpectralState` (:438-450, called at :581 AFTER the tripwire), which is FR-029 clause 5, not FR-025a. Verified live: `seraphis_tests.exe "Seraphis_FactoryPresets_TreeMatchesGenerator"` → "All tests passed (186 assertions in 1 test case)", 1.105 s.

---

#### FR-026 — FR/SC review — **PASS**

Classification is from the three decoded freeze toggles and measurement starts at `settle` (preset_render_sweep_test.cpp:571-577 builds `tailRms` from `timeline.settle` to `settle + W`). `Seraphis_PresetSweep_DecayMatchesRt60` (:865) and `Seraphis_PresetSweep_FrozenPresetsHold` (:993) both green; the 60 s Aether arm is guarded `#if defined(_WIN32)` at :1115 per C-9.

---

#### FR-027 — FR/SC review — **PASS**

preset_render_sweep_test.cpp:664 and :726 iterate `{kRate44100, kRate48000}`; :616-620 sets `RenderLength::HoldPlusFive` for the non-development rate. Windows-only tagging is `#if defined(_WIN32)` INSIDE the cases (:1115, :1805), so the case count is leg-invariant.

---

#### FR-027a — FR/SC review — **PASS**

`Seraphis_PresetSweep_RendersAreReproducible` (preset_render_sweep_test.cpp:1804) green, 48.129 s. Its own reported figure from my run: "SC-026 worst over 42 preset(s): metric relative error 0 (limit 1e-05), checkpoint error 0 (limit 0.0001)". No float bit digest introduced (comparison is `compareFingerprints`).

---

#### FR-027b — FR/SC review — **PASS**

`Seraphis_PresetSweep_PresetsAreDistinct` (preset_render_sweep_test.cpp:1939) green over all 861 pairs; reported: "observedMinimum = 0.036591 held by Bells/Bronze Halo vs Drones/Continuum | median = 0.527398 | max = 0.940877 | pinned floor = max(0.02, 0.5 x observedMinimum) = 0.02". Caveat: the `0.5 × observedMinimum` term is self-referential, so the only real gate is the absolute 0.02 — validated from below by the [.measure] negative control at :2111 (twin scores 0.000210024).

---

#### FR-028 — FR/SC review — **PASS**

`Seraphis_PresetSweep_NoAudioThreadAllocation` (preset_render_sweep_test.cpp:1303) green, 1.538 s in my run — quiescent-load arm with AllocationScope after warm-up.

---

#### FR-028a — FR/SC review — **PASS**

`Seraphis_PresetSweep_ConcurrentLoadIsRtSafe` (preset_render_sweep_test.cpp:1445) green, 0.114 s, using the new thread-scoped instrument: tests/test_helpers/allocation_detector.h adds `tAllocationTrackThisThread` thread_local, `setThreadFilterEnabled` (default OFF, `std::atomic<bool> threadFilter_{false}`) and `class ThreadScopedAllocationScope` that restores both flags in its destructor — additive, so the six other plugins' AllocationScope usage is byte-for-byte unchanged.

---

#### FR-029 — FR/SC review — **PASS**

`Seraphis_FactoryPresets_TreeMatchesGenerator` (factory_preset_test.cpp:2544) green, 1.034 s. The only `_WIN32` token in the whole TU is a comment at :2359, so the case is unguarded and runs on every leg; `node tools/check-portability.js` compiled the TU with g++ → OK. Only the Windows leg has actually been observed; my run's reported margins were SCALAR 0.000000000e+00 / PAYLOAD 0.000000000e+00 (same-toolchain comparison, so cross-toolchain behaviour is untested here).

---

#### FR-029a — FR/SC review — **PARTIAL**

Two of the three required legs are now measured, recorded and pinned; the mandated CI dry run is still outstanding. MEASURED BY ME THIS SESSION, both legs: Windows/MSVC `seraphis_tests.exe "Seraphis_FactoryPresets_TreeMatchesGenerator" -d yes` → "toolchain: MSVC _MSC_VER=1944, presets compared 42, SCALAR worst relative error = 0.000000000e+00, PAYLOAD worst relative error = 0.000000000e+00"; WSL/GCC (I rebuilt seraphis_tests under build/linux-release, 6 phase TUs recompiled, 0 warnings, then ran the same case) → "toolchain: GCC 13.3.0, presets compared 42, SCALAR worst = 0.000000000e+00, PAYLOAD worst relative error = 1.175471576e-07, preset Bells/Bell Garden, field payload[1].amplitudes[20], committed 1.267675012e-01 vs regenerated 1.267675161e-01" — green, 186 assertions. The constants are now PINNED, not provisional: factory_preset_test.cpp:2436-2437 `kScalarFieldTolerance = 1.2e-7` / `kPayloadFieldTolerance = 1.2e-6`, with the derivation banner at :2397-2435 and three static_asserts at :2443-2451 (classes distinct, both > 0, scalar tighter). The payload pin is exactly 10x my own measured GCC worst (1.175471576e-07 -> 1.2e-6). The scalar pin is a DOCUMENTED deviation from the x10 rule (:2419-2429): measured worst is exactly 0 on both legs, 10x0 = 0 would be a forbidden bit-exact golden, so it is pinned at 1 float ULP (2^-23 = 1.1920929e-07 -> 1.2e-7). The unconditional PROVISIONAL WARN is gone (:2582-2584 records its deliberate removal); the shipped WARN now prints the pinned values and their provenance. compliance.md:1029 "## FR-029a measured tolerances" carries the three-leg table and the deviation rationale. GAP: FR-029a's text requires "a local WSL/GCC probe PLUS one CI dry run on the real macOS and Linux legs". The macOS/Apple-Clang leg is recorded as "NOT RUN / OUTSTANDING" in that table and the shipped test prints "macOS leg: NOT YET MEASURED" — I confirmed that string in both my Windows and Linux runs. No CI dry run has been performed.

---

#### FR-030 — FR/SC review — **PASS**

compliance.md:2471-2473 records "### VERDICT: **`DEFERRED`**" explicitly, and § SC-025 cites the measured figure with provenance — I verified that provenance independently: ui_perf_test.cpp:719-720 records `SC-009(a) whole-process(), gate OPEN 30.69 % / 31.74 % → 31.74 % BREACH` and `SC-010(b) gate CLOSED 31.19 % / 31.30 % → 31.30 % BREACH` against the 25 % ceiling. The measured number, not the target, is what is cited.

---

#### FR-031 — FR/SC review — **PASS**

.claude/workflows/release-readiness.js PLUGIN_MAP now contains `seraphis: { testTarget: 'seraphis_tests', bundle: 'Seraphis.vst3' }` (git diff shows it added after the `membrum` line at :21).

---

#### FR-032 — FR/SC review — **PASS**

.claude/skills/release/SKILL.md — `seraphis` added to the Plugin input list (:15-16) and the row `| seraphis | \`seraphis_tests\` | \`Seraphis.vst3\` |` added to the target/bundle table (:31), both confirmed in git diff.

---

#### FR-033 — FR/SC review — **PASS**

plugins/seraphis/version.json diff: `"version": "0.4.0"` → `"0.5.0"` (version field only; src/version.h is generated and untouched). CHANGELOG.md:8 `## [0.5.0] - 2026-08-05`. `node tools/check-changelog-coverage.js seraphis` run this session → EXIT=0, "Entry \"## [0.5.0]\" has 29 bullet(s)."

---

#### FR-034 — FR/SC review — **PASS**

plugins/seraphis/CLAUDE.md:230 "### 2. Preset categories are **additive-only** — the shipped set is these SEVEN" with a 7-row #/Category/Directory/count table starting :238, and :252 "The list is **additive-only**: … a rename orphans every preset a user has already saved against the old name."

---

#### FR-034a — FR/SC review — **FAIL**

NOT DONE — unchanged. compliance.md:2446 "FILLED by T029, 2026-08-05 — with the outcome column HONESTLY EMPTY. Verdict: SC-029 is NOT MET." The 42-row worksheet at :2440-2528 has every Audition-outcome cell reading **NOT AUDITIONED** (rows 1-42, `Vellum`…`Aftermath`); `grep -c 'NOT AUDITIONED' compliance.md` = 46 (42 rows + 4 prose mentions). compliance.md:2681 records FR-034a itself as "NOT DONE" with a ❌. No automated substitute was used (correct per C-10 clause 2), but FR-034a's closing clause makes a missing record verdict-blocking, and the record is missing for 42 of 42 presets. Phase owner action, not an agent action.

---

#### FR-035 — FR/SC review — **PASS**

Every named clause of the gate now passes on the release toolchain — I ran all seven myself. (1) BUILD: after `touch`ing all 8 phase TUs, `cmake --build build/windows-x64-release --config Release --target seraphis_tests seraphis_preset_generator shared_tests Seraphis` → BUILD_EXIT=0, 11 .cpp recompiles (controller.cpp, processor.cpp x3, preset_render_sweep_test.cpp, processor_audio_test.cpp, editor_lifecycle_test.cpp, factory_preset_test.cpp, seraphis_preset_generator.cpp, test_allocation_detector.cpp), `grep -ci warning` = **0**. (2) SUITE: `seraphis_tests.exe` → EXIT=0, "All tests passed (445343 assertions in 109 test cases)". (3) PLUGINVAL: `tools/pluginval.exe --strictness-level 5 --validate build/windows-x64-release/VST3/Release/Seraphis.vst3` → PV_EXIT=0, ends "Completed tests in pluginval / Restoring default layout". (4) VERSION/CHANGELOG: version.json "version": "0.5.0", CHANGELOG.md:8 "## [0.5.0] - 2026-08-05", `node tools/check-changelog-coverage.js seraphis` EXIT=0. (5) PORTABILITY: `node tools/check-portability.js` → "all clear -- 7 compiled, 1 skipped", EXIT=0. (6) CLANG-TIDY: `./tools/run-clang-tidy.ps1 -Target seraphis -BuildDir build/windows-ninja` → "Files analyzed: 35 / [OK] Errors: 0 / [OK] Warnings: 0"; `grep -c 'warning:'` over the log = 0 — the previous 50 are gone, and the log confirms the three offending files were analysed ([6/35] processor_audio_test.cpp, [22/35] preset_render_sweep_test.cpp, [28/35] factory_preset_test.cpp). (7) DETERMINISM: `node tools/check-preset-generator-determinism.js` → EXIT=0, "42 file(s); 0 differing between two fresh runs, 0 changed by a third run over an existing tree". MATERIAL FINDING OUTSIDE THIS GATE'S CLAUSES, reported because it must not be hidden: `seraphis_tests` is NOT green on the GCC leg. I built and ran the full suite under WSL/GCC 13.3.0 → EXIT=1, "test cases: 109 | 108 passed | 1 failed" — `Seraphis_EveryParameter_ReachesDsp`, section "2 kSoftLimitId - the harmonic-signature carve-out", param_reach_test.cpp:1185 `REQUIRE(onDb - offDb >= kSoftLimitHarmonicFloorDb)` → "-0.0101864972 >= 0.035", "soft limit ON -38.384 dB, OFF -38.3739 dB". Reproduced after a forced rebuild of that TU. The same section on Windows passes by 0.0023 dB ("ON -35.944, OFF -35.9813" → +0.0373 vs a 0.035 floor), i.e. 6 % margin. param_reach_test.cpp was last touched in Phase 10 (d63ba7dd), so this is a cross-toolchain-marginal Phase-8/9 arm, not Phase-12 code — but the CI Linux leg will fail on it.

---

#### FR-035a — FR/SC review — **PASS**

tools/check-preset-generator-determinism.js exists (257 lines, Node per the project rule) and does all three clauses — two fresh temp dirs diffed, a third run over a populated tree, exit 1 on any difference (banner :8-23). My run: "OK — 42 file(s); 0 differing between two fresh runs, 0 changed by a third run over an existing tree." EXIT=0.

---

#### SC-001 — FR/SC review — **PASS**

`Seraphis_FactoryPresets_CategoriesMatchConfig` green (0.001 s) in `seraphis_tests.exe -d yes`; filesystem cross-check: 7 directories, symmetric difference 0 against the seven `subcategoryNames`.

---

#### SC-002 — FR/SC review — **PASS**

Measured 42 total files, 6 per category (≥ 5). `Seraphis_FactoryPresets_CountAndDistribution` green, 0.000 s.

---

#### SC-003 — FR/SC review — **PASS**

`Seraphis_FactoryPresets_ContainerIsValid` green, 0.004 s — 42/42 files parse (VST3 magic, runtime-derived class id, List with Comp+Info, in-range offsets).

---

#### SC-004 — FR/SC review — **PASS**

`Seraphis_FactoryPresets_StreamIsCurrentVersion` green, 0.003 s — 42/42 chunks begin int32 3 and are exactly 2868 bytes (`kSeraphisStateBytes = 2868`, preset_test_support.h:92).

---

#### SC-005 — FR/SC review — **PASS**

`Seraphis_FactoryPresets_RoundTripByteIdentical` green, 0.997 s — 0 failed setState, 0 differing bytes over 42 presets.

---

#### SC-006 — FR/SC review — **PASS**

`Seraphis_FactoryPresets_InfoMetadataMatchesDirectory` green, 0.003 s — 0 mismatches over the six attributes × 42 files.

---

#### SC-007 — FR/SC review — **PASS**

`Seraphis_FactoryPresets_BrowserScanFilesEveryPreset` green, 0.001 s — 42 entries, 0 empty subcategory, 0 non-factory, per-category counts equal, against the empty-temp userDirOverride fixture.

---

#### SC-008 — FR/SC review — **PASS**

The test-built copy is gone; the test now invokes the shipped construction. factory_preset_test.cpp:1169 `const std::vector<std::string> tabLabels = Seraphis::makeSeraphisPresetTabLabels();` and controller.cpp:466 `std::vector<std::string> tabLabels = makeSeraphisPresetTabLabels();` — the same function, defined once at plugins/seraphis/src/preset/seraphis_preset_config.h:52 (`grep -rn makeSeraphisPresetTabLabels plugins/seraphis/src/` returns exactly those two sites). controller.cpp:468-470 hands that vector straight to `new PresetBrowserView(..., std::move(tabLabels))`, so the value under test is the value the browser receives. The comparison is against an independent literal (:1176-1177 `{"All","Textures","Pads","Drones","Bells","Choirs","Motion","Cinematic"}`), element-wise at :1186-1190 plus a separate `tabLabels[0] == "All"` at :1196 — not against config.subcategoryNames, so it is not a tautology. Ran it: `seraphis_tests.exe "Seraphis_PresetBrowser_TabsMatchConfig" -s` → "All tests passed (10 assertions in 1 test case)", message "tab labels: All | Textures | Pads | Drones | Bells | Choirs | Motion | Cinematic".

---

#### SC-009 — FR/SC review — **PASS**

`Seraphis_PresetSweep_NoSilence` green, 93.701 s, over 42 presets × 2 rates, against the > 1e-3 linear (−60 dBFS) floor. GAP in evidence quality: unlike SC-011/SC-012/SC-026/SC-028, this case emits no measured-margin WARN, so the tightest observed value is not reproducible from a suite run — compliance.md:1224 records 0.00407 (Pads/Distant Choir) from a separate probe, which I did not re-measure.

---

#### SC-010 — FR/SC review — **PASS**

`Seraphis_PresetSweep_BoundedAndFinite` green (0.000 s — it reads the cached renders produced by SC-009's 93.701 s pass). Ceiling is the shipped constant pair, preset_render_sweep_test.cpp:170-172 → 0.8912509 × 10^(0.1/20) ≈ 0.9016. Same evidence gap as SC-009: no margin is printed; compliance.md:1225 records largest peak 0.394, unre-measured by me.

---

#### SC-010a — FR/SC review — **PASS**

`Seraphis_PresetSweep_ChordBoundedAndFinite` green, 47.856 s in my run — 42 chord renders at 44 100 Hz, 0 non-finite, peak within the same ceiling. Margin not transcribed by the case (same reporting gap compliance.md:2609 already flags ⚠️).

---

#### SC-011 — FR/SC review — **PASS**

Verbatim from my run (preset_render_sweep_test.cpp:957): "SC-011 tightest margin over 38 unfrozen preset(s): 9.12235 dB above its own bound, held by Drones/Stone Circle @ 44100 Hz". 0 presets below their own `min(0.5·60·10/RT60, 20)` bound.

---

#### SC-012 — FR/SC review — **PASS**

Verbatim from my run — arm 1 (:1193): "Cinematic/Event Horizon (Aether freeze): band 0.637886 dB over 60 s (margin 1.36211 dB) | Cinematic/Vast (Aether freeze): band 0.654912 dB over 60 s (margin 1.34509 dB)" vs the 2.0 dB limit. Arm 2 (:1093): "Choirs/Ghost Choir … loudest 0.534321 dB (margin 0.465679 dB) … | Cinematic/Signal Lost … loudest 0.00458527 dB (margin 0.995415 dB) vs the 1 dB allowance". 0 outside either band.

---

#### SC-013 — FR/SC review — **PASS**

`Seraphis_FactoryPresets_CoversShippedSurface` green, 0.003 s — 0 unmet rows of the C-2 matrix, evaluated against decoded state.

---

#### SC-014 — FR/SC review — **PASS**

`Seraphis_FactoryPresets_RespectVoiceBudget` green, 0.003 s — 42/42 decode polyphony ≤ 8 and softLimit == true.

---

#### SC-014a — FR/SC review — **PASS**

`Seraphis_FactoryPresets_RespectTimingCeiling` green, 0.003 s — 42/42 decode A ≤ 12.0 s and Rel ≤ 10.0 s.

---

#### SC-015 — FR/SC review — **PASS**

`Seraphis_PresetSweep_NoAudioThreadAllocation` green, 1.538 s — 0 allocations inside the AllocationScope around the warm render calls.

---

#### SC-015a — FR/SC review — **PASS**

`Seraphis_PresetSweep_ConcurrentLoadIsRtSafe` green, 0.114 s, instrumented by `ThreadScopedAllocationScope` (tests/test_helpers/allocation_detector.h, new class in the diff) rather than the unfiltered global scope — which is what the criterion requires.

---

#### SC-016 — FR/SC review — **PASS**

Verbatim, my run: "check-preset-generator-determinism: OK — 42 file(s); 0 differing between two fresh runs, 0 changed by a third run over an existing tree." EXIT=0, 4 s.

---

#### SC-017 — FR/SC review — **PARTIAL**

Two of the three legs the criterion names are now genuinely green, and the tolerances they gate on are no longer provisional — but the macOS leg has still not been run. WINDOWS/MSVC (my run): `seraphis_tests.exe "Seraphis_FactoryPresets_TreeMatchesGenerator" -d yes` → "All tests passed (186 assertions in 1 test case)", 1.105 s; WARN block "toolchain MSVC _MSC_VER=1944, presets compared 42, SCALAR worst relative error = 0.000000000e+00, PAYLOAD worst relative error = 0.000000000e+00". LINUX/GCC (my run — I rebuilt seraphis_tests under WSL, all 6 phase TUs recompiled with 0 warnings, then ran the same case): "All tests passed (186 assertions in 1 test case)", WARN "toolchain GCC 13.3.0, presets compared 42, SCALAR worst = 0.000000000e+00, PAYLOAD worst relative error = 1.175471576e-07 (Bells/Bell Garden, payload[1].amplitudes[20], committed 1.267675012e-01 vs regenerated 1.267675161e-01)" — a genuine cross-toolchain comparison (MSVC-committed tree vs GCC regeneration), inside the pinned 1.2e-6. The tolerances are now FR-029a-measured-and-pinned (factory_preset_test.cpp:2436-2437, banner :2397-2435), so the earlier "gates on provisional anchors" objection is resolved. REMAINING GAP: SC-017 says "asserted on Windows, macOS **and** Linux legs"; the macOS/Apple-Clang leg has not been run, and the shipped test still prints "macOS leg: NOT YET MEASURED - a disagreement there is INVESTIGATED, never widened" (confirmed in both of my runs).

---

#### SC-018 — FR/SC review — **PASS**

Both halves now measured clean on both toolchains, by me, with forced recompiles. LINUX/GCC (the leg that previously failed): I deleted build/linux-release/CMakeFiles/seraphis_preset_generator.dir/{tools/seraphis_preset_generator.cpp.o, plugins/seraphis/src/processor/processor.cpp.o} and rebuilt — `wsl cmake --build build/linux-release --target seraphis_preset_generator` → BUILD_EXIT=0, `grep -c 'warning'` over the full log = **0**, with both TUs genuinely rebuilt ([1/3] seraphis_preset_generator.cpp.o, [2/3] processor.cpp.o). The 7 warnings are fixed at source, not suppressed: spectral_state.h now routes both label copies through `detail::copyStateName` (strlen+min+memcpy, spectral_state.h:348-365 — the `-Warray-bounds` on subscripts 10-14 of a `const char[10]` came from GCC unrolling the old `label[i] != '\0'` loop), processor.cpp:928-937 casts to `void*` with a `static_assert(std::is_trivially_copyable_v<CloudFrame>)` for `-Wclass-memaccess`, and seraphis_engine.h:1362-1370 clamps the steal loop with `const std::size_t voiceCount = std::min(polyphony_, kMaxVoices)` for the `array:203` subscript-32-into-SeraphisVoice[16] warning. Binary output: `./build/linux-release/bin/seraphis_preset_generator /tmp/p12gen` → "Generated 42 of 42 Seraphis factory presets.", 42 .vstpreset files, 7 directories. WINDOWS/MSVC: same target in my touched full build → 0 warnings; `seraphis_preset_generator.exe /f/tmp/p12gen_win` → "Generated 42 of 42 Seraphis factory presets.", 42 files, 7 dirs. (The CI/release Linux leg itself has not been exercised, but SC-018's stated verification vehicle — the local WSL probe — is met.)

---

#### SC-019 — FR/SC review — **PASS**

Measured after my Release build (post-build step logged "[VSTWORK] Factory presets installed to C:\ProgramData/Krate Audio/Seraphis"): `ls "/c/ProgramData/Krate Audio/Seraphis/"` → the 7 category dirs; `find … -name '*.vstpreset' | wc -l` → 42; total files 49 (the 7 extra are stale `.gitkeep`s).

---

#### SC-020 — FR/SC review — **PASS**

`tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Seraphis.vst3"` run this session on a freshly built bundle → EXIT=0, 0 lines matching /fail/i, ending "Completed tests in pluginval / Restoring default layout".

---

#### SC-021 — FR/SC review — **PASS**

My full run: "All tests passed (445342 assertions in 109 test cases)", 480 s wall. Baseline recorded at 88fca556 was 84 cases / 444887 assertions (compliance.md:62), so +25 cases — which reconciles: 15 visible cases in factory_preset_test.cpp + 9 in preset_render_sweep_test.cpp (2 more are `[.measure]`-hidden) + 1 regression case added with the Growth-note fix in processor_audio_test.cpp.

---

#### SC-022 — FR/SC review — **PASS**

All four clauses measured green by me this session. (1) `node tools/check-portability.js` → "check-portability: all clear -- 7 compiled, 1 skipped", EXIT=0. The one skip is still `tools/seraphis_preset_generator.cpp (plugin_ids.h: No such file or directory)` — a real coverage gap, but its two clean compiles (MSVC and GCC, SC-018) cover it. (2) `node tools/check-preset-generator-determinism.js` → EXIT=0, "42 file(s); 0 differing between two fresh runs, 0 changed by a third run over an existing tree". (3) `./tools/run-clang-tidy.ps1 -Target seraphis -BuildDir build/windows-ninja` → ran to completion ([35/35] processor.cpp), "Files analyzed: 35 / [OK] Errors: 0 / [OK] Warnings: **0**"; independent `grep -c 'warning:'` over the captured log = 0. The previous 50 (37 factory_preset_test.cpp, 11 preset_render_sweep_test.cpp, 2 processor_audio_test.cpp) are gone and all three files are still in the analysed set (log lines [6/35], [22/35], [28/35]). (4) Compiler warnings: full touched rebuild of seraphis_tests + seraphis_preset_generator + shared_tests + Seraphis → BUILD_EXIT=0, `grep -ci warning` = 0; the 5 DSP layer targets rebuilt likewise → BUILD_EXIT=0, 0 warnings.

---

#### SC-023 — FR/SC review — **PASS**

`node tools/check-changelog-coverage.js seraphis` → EXIT=0, "=== seraphis (version.json: 0.5.0) === … Entry \"## [0.5.0]\" has 29 bullet(s)." and CHANGELOG.md:8 carries `## [0.5.0] - 2026-08-05`.

---

#### SC-024 — FR/SC review — **PASS**

Read back after the change: `.claude/workflows/release-readiness.js` PLUGIN_MAP entry `seraphis: { testTarget: 'seraphis_tests', bundle: 'Seraphis.vst3' }` (so the name is no longer filtered out at :26-29), and `.claude/skills/release/SKILL.md:31` carries the `| seraphis | seraphis_tests | Seraphis.vst3 |` row with `seraphis` added to the input list at :15-16.

---

#### SC-025 — FR/SC review — **FAIL**

Unchanged and still breaching — no post-optimisation measurement exists. Threshold: whole-`process()` at the 8-voice operating point ≤ 25.00 % of one core. Recorded figures, read at source this session: plugins/seraphis/tests/integration/ui_perf_test.cpp:719 "SC-009(a) whole-process(), gate OPEN 30.69 % 31.74 % **31.74 % BREACH**" and :720 "SC-010(b) whole-process(), gate CLOSED 31.19 % 31.30 % **31.30 % BREACH**". The ceiling is `kFullPolyCeilingNs = kBlockBudgetNs * 0.25` (:828) = 2 666 666.7 ns; the baseline is `kBaselineWholeProcessNs = 3385600.0` (:865), self-checked as 31.74 % of a block by the static_assert at :881-885. Breach = +6.74 points (1.27x). Exit criterion 3 is provably unmet: `constexpr bool kSc010BaselinePinned = false;` (:872), held false by `static_assert(!kSc010BaselinePinned, ...)`. Despite the three Phase 11.5 perf commits on this branch (7666aa83, 7881a6ff, 88fca556), NO re-measured figure exists: `grep -n "2026-08-05|POST-OPT|post-opt|re-measured" ui_perf_test.cpp` returns nothing, and the arms are `[.perf]`-tagged so they did not run in my green 109-case suite. FR-030's procedural half is satisfied — compliance.md:2545 "### VERDICT: **`DEFERRED`**", :2676 records FR-030 ✅ citing 31.74 %/31.30 % with provenance — but SC-025's own threshold (a recorded ≤ 25 % figure) is not met.

---

#### SC-026 — FR/SC review — **PASS**

Verbatim from my run (preset_render_sweep_test.cpp:1903): "SC-026 worst over 42 preset(s): metric relative error 0 (limit 1e-05), checkpoint error 0 (limit 0.0001)". 0 presets failing. Float-bit-golden prohibition holds — the comparison is `compareFingerprints`, and no digest constant appears in either new TU.

---

#### SC-027 — FR/SC review — **PASS**

Summed from my own `-d yes` durations: NoSilence 93.701 + BoundedAndFinite 0.000 + ChordBounded 47.856 + DecayMatchesRt60 0.000 + FrozenPresetsHold 0.000 + NoAudioThreadAllocation 1.538 + ConcurrentLoad 0.114 + RendersAreReproducible 48.129 + PresetsAreDistinct 0.066 = **191.40 s** (3 min 11 s) against the 360 s budget — 1.88× margin, no lever needed. (Higher than compliance.md's 163.338 s because clang-tidy was contending for cores during part of my run.)

---

#### SC-028 — FR/SC review — **PASS**

Verbatim from my run (preset_render_sweep_test.cpp:2072): "SC-028 over 861 pair(s): observedMinimum = 0.036591 held by Bells/Bronze Halo vs Drones/Continuum | median = 0.527398 | max = 0.940877 | pinned floor = max(0.02, 0.5 x observedMinimum) = 0.02" — 0 pairs below, min is 1.83× the absolute floor.

---

#### SC-029 — FR/SC review — **FAIL**

Unchanged. Threshold: 0 presets without a recorded audition outcome; measured 42 without one. compliance.md:2440 "## SC-029 listening checkpoint (42 rows)"; :2446 "FILLED by T029, 2026-08-05 — with the outcome column HONESTLY EMPTY. Verdict: SC-029 is NOT MET." All 42 worksheet rows (:2472-2513, `Vellum` through `Aftermath`) carry **NOT AUDITIONED**; `grep -c 'NOT AUDITIONED'` = 46. The document's own clause table at :2519-2523 scores it: rows recorded 42/42 ✅, automated substitute none-used ✅, "Presets with a recorded audition outcome: 42 required — **0**, all 42 read NOT AUDITIONED" ❌. compliance.md:2720 repeats the ❌ in the SC roster. Requires the phase owner at a DAW; nothing in the codebase blocks it.

---

#### SC-001 — SC re-review — **PASS**

Ran build/windows-x64-release/bin/Release/seraphis_tests.exe -d yes (my own run, 484 s wall): `0.001 s: Seraphis_FactoryPresets_CategoriesMatchConfig` green inside `All tests passed (445342 assertions in 109 test cases)`. Independently confirmed on disk: `ls plugins/seraphis/resources/presets/` -> Bells Choirs Cinematic Drones Motion Pads Textures (7 dirs, no others); `find plugins/seraphis/resources/presets -name '*.vstpreset' | wc -l` -> 42, all inside those 7. Test asserts cfg.subcategoryNames.size()==7 and ==kExpectedCategories at factory_preset_test.cpp:394-398. Symmetric difference = 0.

---

#### SC-002 — SC re-review — **PASS**

My own per-directory count: Bells 6, Choirs 6, Cinematic 6, Drones 6, Motion 6, Pads 6, Textures 6 = 42 total; minimum per category 6 >= threshold 5. Test `Seraphis_FactoryPresets_CountAndDistribution` green (0.001 s) with REQUIRE(files.size() == kExpectedPresetCount) where kExpectedPresetCount = 42 at factory_preset_test.cpp:178, :577.

---

#### SC-003 — SC re-review — **PASS**

`seraphis_tests.exe "Seraphis_FactoryPresets_*,Seraphis_PresetBrowser_*,Seraphis_PresetSupport_*"` -> `All tests passed (375 assertions in 16 test cases)`. `Seraphis_FactoryPresets_ContainerIsValid` (factory_preset_test.cpp:711) green over 42/42 files: VST3 magic, class id == FUID::toString(kProcessorUID), List with Comp+Info, offsets in range. 0 failures.

---

#### SC-004 — SC re-review — **PASS**

`Seraphis_FactoryPresets_StreamIsCurrentVersion` green (0.003 s Windows, 0.531 s Linux). Constant kComponentChunkBytes = 2868 at factory_preset_test.cpp:187; hard REQUIRE(comp.size() == std::size_t{2868}) at :508; the decode tripwire kSeraphisStateBytes = 2868 at preset_test_support.h:92. Generator's own stdout also prints `Wrote 2868 state bytes to ...` per file. 42/42.

---

#### SC-005 — SC re-review — **PASS**

`Seraphis_FactoryPresets_RoundTripByteIdentical` green, 1.000 s. Body (factory_preset_test.cpp:890-939) does setState -> getState -> size compare -> std::memcmp, with four separate REQUIREs: unreadable.empty(), notPrepared.empty(), rejected.empty() (0 failed setState), reSaveFailed.empty(), mismatched.empty() (0 differing bytes). All empty in the green run.

---

#### SC-006 — SC re-review — **PASS**

`Seraphis_FactoryPresets_InfoMetadataMatchesDirectory` green (0.003 s). All six attributes enumerated as kRequiredAttributeIds{"MediaType","PlugInName","PlugInCategory","Name","MusicalCategory","MusicalInstrument"} at factory_preset_test.cpp:971-973, with expect("MediaType","VstPreset"), expect("PlugInName","Seraphis"), expect("PlugInCategory","Synth") at :1017-1019 plus Name/MusicalCategory/MusicalInstrument vs directory. 0 mismatches over 42.

---

#### SC-007 — SC re-review — **PASS**

`Seraphis_FactoryPresets_BrowserScanFilesEveryPreset` green (0.002 s). Test builds PresetManager with an empty temp userDirOverride (std::filesystem::temp_directory_path at factory_preset_test.cpp:124) and factoryDirOverride = resources/presets; REQUIRE(scanned.size() == kExpectedPresetCount /*42*/) at :1123; emptySubcategory and notFactory lists both REQUIRE'd empty at :1125-1128; per-category REQUIRE(filtered.size() == onDisk.at(category)) for all 7 at :1136-1142. 42 entries, 0 empty subcategory, 0 non-factory.

---

#### SC-008 — SC re-review — **PASS**

The test-built copy is gone; the test now invokes the shipped construction. factory_preset_test.cpp:1169 `const std::vector<std::string> tabLabels = Seraphis::makeSeraphisPresetTabLabels();` and controller.cpp:466 `std::vector<std::string> tabLabels = makeSeraphisPresetTabLabels();` — the same function, defined once at plugins/seraphis/src/preset/seraphis_preset_config.h:52 (`grep -rn makeSeraphisPresetTabLabels plugins/seraphis/src/` returns exactly those two sites). controller.cpp:468-470 hands that vector straight to `new PresetBrowserView(..., std::move(tabLabels))`, so the value under test is the value the browser receives. The comparison is against an independent literal (:1176-1177 `{"All","Textures","Pads","Drones","Bells","Choirs","Motion","Cinematic"}`), element-wise at :1186-1190 plus a separate `tabLabels[0] == "All"` at :1196 — not against config.subcategoryNames, so it is not a tautology. Ran it: `seraphis_tests.exe "Seraphis_PresetBrowser_TabsMatchConfig" -s` → "All tests passed (10 assertions in 1 test case)", message "tab labels: All | Textures | Pads | Drones | Bells | Choirs | Motion | Cinematic".

---

#### SC-009 — SC re-review — **PASS**

`Seraphis_PresetSweep_NoSilence` green, 93.906 s in my run, covering 42 presets x {44100, 48000} = 84 renders (loop at preset_render_sweep_test.cpp:664-683). Floor kSustainRmsFloor = 1.0e-3 linear = -60.0 dBFS exactly (:175); assertion REQUIRE(silent.empty()) at :689 -> 0 of 84 renders at or below the floor. Sustain window is the preset's own [A+1.0, A+4.0] from decoded state (:561-563), not a fixed guess. Render-failure list also REQUIRE'd empty (:686), so no render was silently skipped.

---

#### SC-010 — SC re-review — **PASS**

`Seraphis_PresetSweep_BoundedAndFinite` green (reads the shared cache filled by NoSilence's 84 renders). kPeakBound = kLimiterCeilingLin(0.8912509f) * 10^(0.1/20) = 0.9016 at preset_render_sweep_test.cpp:170-172; non-finite detected by bit pattern via isFiniteFloat (:439), not std::isnan. Four REQUIREs green at :754-763: renderFailures 0, nonFinite 0 samples, overPeak 0 samples, canaries intact — over the WHOLE [0, Total] render including the NoteOn-only hold, at both rates.

---

#### SC-010a — SC re-review — **PASS**

`Seraphis_PresetSweep_ChordBoundedAndFinite` green, 47.710 s in my run — a real second render set (not cache-served). 4 simultaneous NoteOns at t=0 (root + kChordIntervals{4,7,12}, preset_render_sweep_test.cpp:163, :534-543), 44100 Hz only. REQUIREs at :831-840: 0 render failures, 0 non-finite, 0 samples above the 0.9016 ceiling, canaries intact, over all 42 presets. Per-preset peak margins are not transcribed anywhere, but the criterion's two thresholds (0 non-finite, peak <= 0.9016) are both gated and green.

---

#### SC-011 — SC re-review — **PASS**

`Seraphis_PresetSweep_DecayMatchesRt60` green. Its own unconditional WARN (preset_render_sweep_test.cpp:957), verbatim from my run: "SC-011 tightest margin over 38 unfrozen preset(s): 9.12235 dB above its own bound, held by Drones/Stone Circle @ 44100 Hz". Non-vacuity gated by REQUIRE(qualifying > 0u) at :951 (38 > 0); REQUIRE(belowBound.empty()) at :961 -> 0 presets below min(0.5*60*10/RT60, 20.0) dB.

---

#### SC-012 — SC re-review — **PASS**

`Seraphis_PresetSweep_FrozenPresetsHold` green. Verbatim WARNs from my run — arm 1 (:1193): "Cinematic/Event Horizon (Aether freeze): band 0.637886 dB over 60 s (margin 1.36211 dB) | Cinematic/Vast (Aether freeze): band 0.654912 dB over 60 s (margin 1.34509 dB)" — worst 0.654912 dB vs the 2.0 dB band, 60 s duration unreduced. Arm 2 (:1093): "Choirs/Ghost Choir (Atmos/FX freeze): final -0.447095 dB (margin 1.44709 dB), loudest 0.534321 dB (margin 0.465679 dB) | Cinematic/Signal Lost: final -0.0029211 dB (margin 1.00292 dB), loudest 0.00458527 dB (margin 0.995415 dB)" vs the +1.0 dB allowance. Both arms guarded by REQUIRE(qualifying > 0u) (:1089, :1191); outOfBand/grewEndToEnd/grewInterior all REQUIRE'd empty. 2 + 2 + 38 = 42 presets classified.

---

#### SC-013 — SC re-review — **PASS**

`Seraphis_FactoryPresets_CoversShippedSurface` green (0.003 s). Ledger built from decoded state, never from the definitions table (factory_preset_test.cpp:1238-1255): body material, all four spectral slots, morph stateCount, travelMode, envelope mode, grainEnvelope, atmos/aether/fx freeze, delaySync, resonator bypass, input AGC. requireEveryIndex/requireBothPolarities accumulate into one `missing` list REQUIRE'd empty at the end (:1289-1331). 0 unmet rows.

---

#### SC-014 — SC re-review — **PASS**

`Seraphis_FactoryPresets_RespectVoiceBudget` green (0.003 s, factory_preset_test.cpp:1357). Values read through the FR-025a decode of the real stream (decodePresetState), not re-derived arithmetically — the banner at :1190-1201 states that explicitly. 42/42 presets decode polyphony <= 8 and softLimit == true; 0 violations.

---

#### SC-014a — SC re-review — **PASS**

`Seraphis_FactoryPresets_RespectTimingCeiling` green (0.003 s, factory_preset_test.cpp:1397). 42/42 decode A <= 12.0 s and Rel <= 10.0 s; 0 presets over either ceiling. Consistency check: the sweep's own longest arm (Aether-freeze Total <= 89 s under this ceiling) completed inside the measured 93.906 s NoSilence case covering both rates.

---

#### SC-015 — SC re-review — **PASS**

`Seraphis_PresetSweep_NoAudioThreadAllocation` green, 1.585 s (a real run, not cached). setState strictly between process() calls; TestHelpers::AllocationScope wraps the render calls at preset_render_sweep_test.cpp:1368, :1398; REQUIRE(rejected.empty()), REQUIRE(allocating.empty()), REQUIRE(fx.checkCanaries()) at :1381-1386, plus REQUIRE(probe >= 1u) at :1406 which proves the detector itself is live. 0 allocations over all 42 presets.

---

#### SC-015a — SC re-review — **PASS**

`Seraphis_PresetSweep_ConcurrentLoadIsRtSafe` green, 0.128 s. Real interleaving: std::thread audio thread at preset_render_sweep_test.cpp:1493 rendering while the message thread setStates all 42 chunks. Instrument is the thread-scoped TestHelpers::ThreadScopedAllocationScope (:1523; class at tests/test_helpers/allocation_detector.h:149, backed by `inline thread_local bool tAllocationTrackThisThread` at :41) — NOT the unfiltered global scope. REQUIREs at :1599-1643: failures empty (0 failed setState), blocksOk == blocksRendered, final state matches a committed chunk, nonFiniteSamples == 0, overPeakSamples == 0, canariesIntact, allocations == 0, probe >= 1 (non-vacuity).

---

#### SC-016 — SC re-review — **PASS**

I ran it myself: `node tools/check-preset-generator-determinism.js` -> "check-preset-generator-determinism: OK — 42 file(s); 0 differing between two fresh runs, 0 changed by a third run over an existing tree." EXIT=0. Threshold 0 differing / 0 changed / exit 0: all met, against the 42-file library (not the 3-file pilot).

---

#### SC-017 — SC re-review — **PARTIAL**

Two of the three legs the criterion names are now genuinely green, and the tolerances they gate on are no longer provisional — but the macOS leg has still not been run. WINDOWS/MSVC (my run): `seraphis_tests.exe "Seraphis_FactoryPresets_TreeMatchesGenerator" -d yes` → "All tests passed (186 assertions in 1 test case)", 1.105 s; WARN block "toolchain MSVC _MSC_VER=1944, presets compared 42, SCALAR worst relative error = 0.000000000e+00, PAYLOAD worst relative error = 0.000000000e+00". LINUX/GCC (my run — I rebuilt seraphis_tests under WSL, all 6 phase TUs recompiled with 0 warnings, then ran the same case): "All tests passed (186 assertions in 1 test case)", WARN "toolchain GCC 13.3.0, presets compared 42, SCALAR worst = 0.000000000e+00, PAYLOAD worst relative error = 1.175471576e-07 (Bells/Bell Garden, payload[1].amplitudes[20], committed 1.267675012e-01 vs regenerated 1.267675161e-01)" — a genuine cross-toolchain comparison (MSVC-committed tree vs GCC regeneration), inside the pinned 1.2e-6. The tolerances are now FR-029a-measured-and-pinned (factory_preset_test.cpp:2436-2437, banner :2397-2435), so the earlier "gates on provisional anchors" objection is resolved. REMAINING GAP: SC-017 says "asserted on Windows, macOS **and** Linux legs"; the macOS/Apple-Clang leg has not been run, and the shipped test still prints "macOS leg: NOT YET MEASURED - a disagreement there is INVESTIGATED, never widened" (confirmed in both of my runs).

---

#### SC-018 — SC re-review — **PASS**

Both halves now measured clean on both toolchains, by me, with forced recompiles. LINUX/GCC (the leg that previously failed): I deleted build/linux-release/CMakeFiles/seraphis_preset_generator.dir/{tools/seraphis_preset_generator.cpp.o, plugins/seraphis/src/processor/processor.cpp.o} and rebuilt — `wsl cmake --build build/linux-release --target seraphis_preset_generator` → BUILD_EXIT=0, `grep -c 'warning'` over the full log = **0**, with both TUs genuinely rebuilt ([1/3] seraphis_preset_generator.cpp.o, [2/3] processor.cpp.o). The 7 warnings are fixed at source, not suppressed: spectral_state.h now routes both label copies through `detail::copyStateName` (strlen+min+memcpy, spectral_state.h:348-365 — the `-Warray-bounds` on subscripts 10-14 of a `const char[10]` came from GCC unrolling the old `label[i] != '\0'` loop), processor.cpp:928-937 casts to `void*` with a `static_assert(std::is_trivially_copyable_v<CloudFrame>)` for `-Wclass-memaccess`, and seraphis_engine.h:1362-1370 clamps the steal loop with `const std::size_t voiceCount = std::min(polyphony_, kMaxVoices)` for the `array:203` subscript-32-into-SeraphisVoice[16] warning. Binary output: `./build/linux-release/bin/seraphis_preset_generator /tmp/p12gen` → "Generated 42 of 42 Seraphis factory presets.", 42 .vstpreset files, 7 directories. WINDOWS/MSVC: same target in my touched full build → 0 warnings; `seraphis_preset_generator.exe /f/tmp/p12gen_win` → "Generated 42 of 42 Seraphis factory presets.", 42 files, 7 dirs. (The CI/release Linux leg itself has not been exercised, but SC-018's stated verification vehicle — the local WSL probe — is met.)

---

#### SC-019 — SC re-review — **PASS**

After my `cmake --build ... --target Seraphis` (EXIT=0, log line "[VSTWORK] Factory presets installed to C:\ProgramData/Krate Audio/Seraphis"), I listed the destination: Bells 6, Choirs 6, Cinematic 6, Drones 6, Motion 6, Pads 6, Textures 6 = 7 directories, 42 .vstpreset. Destination matches Platform::getFactoryPresetDirectory("Seraphis") = %PROGRAMDATA%\Krate Audio\Seraphis (preset_paths.h:24, KratePlugin.cmake:299 with target=Seraphis). Disclosed residue: 7 stale .gitkeep files also present (copy_directory never deletes) — inert to the browser (extension filter, preset_manager.cpp:63) and absent from the installer path.

---

#### SC-020 — SC re-review — **PASS**

I ran `tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Seraphis.vst3"` against the bundle from my own fresh Release build: EXIT=0, and `grep -ciE '^\s*\*\*\* FAILED'` over the captured log = 0. Log tail ends with "Completed tests in pluginval / Restoring default layout".

---

#### SC-021 — SC re-review — **PASS**

My full run: `All tests passed (445342 assertions in 109 test cases)`, 0 failures. Pre-phase baseline recorded at 84 cases / 444887 assertions (compliance.md B-1). Delta +25 reconciles exactly against source: 26 new TEST_CASEs in the two new preset TUs minus the 2 hidden [.measure] cases = 24, plus 1 new case added to processor_audio_test.cpp (`git diff -U0` shows exactly one added TEST_CASE: Seraphis_GrowthNoteInParameterBlockSounds) = 25. 84 + 25 = 109.

---

#### SC-022 — SC re-review — **PASS**

All four clauses measured green by me this session. (1) `node tools/check-portability.js` → "check-portability: all clear -- 7 compiled, 1 skipped", EXIT=0. The one skip is still `tools/seraphis_preset_generator.cpp (plugin_ids.h: No such file or directory)` — a real coverage gap, but its two clean compiles (MSVC and GCC, SC-018) cover it. (2) `node tools/check-preset-generator-determinism.js` → EXIT=0, "42 file(s); 0 differing between two fresh runs, 0 changed by a third run over an existing tree". (3) `./tools/run-clang-tidy.ps1 -Target seraphis -BuildDir build/windows-ninja` → ran to completion ([35/35] processor.cpp), "Files analyzed: 35 / [OK] Errors: 0 / [OK] Warnings: **0**"; independent `grep -c 'warning:'` over the captured log = 0. The previous 50 (37 factory_preset_test.cpp, 11 preset_render_sweep_test.cpp, 2 processor_audio_test.cpp) are gone and all three files are still in the analysed set (log lines [6/35], [22/35], [28/35]). (4) Compiler warnings: full touched rebuild of seraphis_tests + seraphis_preset_generator + shared_tests + Seraphis → BUILD_EXIT=0, `grep -ci warning` = 0; the 5 DSP layer targets rebuilt likewise → BUILD_EXIT=0, 0 warnings.

---

#### SC-023 — SC re-review — **PASS**

I ran `node tools/check-changelog-coverage.js seraphis` -> "=== seraphis (version.json: 0.5.0) === ... Entry \"## [0.5.0]\" has 29 bullet(s)." CHANGELOG_EXIT=0. plugins/seraphis/version.json:2 reads "version": "0.5.0" (was 0.4.0); plugins/seraphis/CHANGELOG.md:8 reads `## [0.5.0] - 2026-08-05`. Version and heading are in sync.

---

#### SC-024 — SC re-review — **PASS**

.claude/workflows/release-readiness.js:21 — `seraphis: { testTarget: 'seraphis_tests', bundle: 'Seraphis.vst3' },` inside PLUGIN_MAP, so `args.plugins.filter((p) => PLUGIN_MAP[p])` at :29-30 no longer drops the name. .claude/skills/release/SKILL.md:15-16 lists `seraphis` in the required Plugin input, and :31 carries the table row `| seraphis | \`seraphis_tests\` | \`Seraphis.vst3\` |`. Both resolved identifiers are real: the seraphis_tests target builds (my run) and Seraphis.vst3 exists at build/windows-x64-release/VST3/Release/. The six pre-existing rows are undisturbed.

---

#### SC-025 — SC re-review — **FAIL**

Unchanged and still breaching — no post-optimisation measurement exists. Threshold: whole-`process()` at the 8-voice operating point ≤ 25.00 % of one core. Recorded figures, read at source this session: plugins/seraphis/tests/integration/ui_perf_test.cpp:719 "SC-009(a) whole-process(), gate OPEN 30.69 % 31.74 % **31.74 % BREACH**" and :720 "SC-010(b) whole-process(), gate CLOSED 31.19 % 31.30 % **31.30 % BREACH**". The ceiling is `kFullPolyCeilingNs = kBlockBudgetNs * 0.25` (:828) = 2 666 666.7 ns; the baseline is `kBaselineWholeProcessNs = 3385600.0` (:865), self-checked as 31.74 % of a block by the static_assert at :881-885. Breach = +6.74 points (1.27x). Exit criterion 3 is provably unmet: `constexpr bool kSc010BaselinePinned = false;` (:872), held false by `static_assert(!kSc010BaselinePinned, ...)`. Despite the three Phase 11.5 perf commits on this branch (7666aa83, 7881a6ff, 88fca556), NO re-measured figure exists: `grep -n "2026-08-05|POST-OPT|post-opt|re-measured" ui_perf_test.cpp` returns nothing, and the arms are `[.perf]`-tagged so they did not run in my green 109-case suite. FR-030's procedural half is satisfied — compliance.md:2545 "### VERDICT: **`DEFERRED`**", :2676 records FR-030 ✅ citing 31.74 %/31.30 % with provenance — but SC-025's own threshold (a recorded ≤ 25 % figure) is not met.

---

#### SC-026 — SC re-review — **PASS**

`Seraphis_PresetSweep_RendersAreReproducible` green, 48.643 s in my run (a real second render set). Its unconditional WARN (preset_render_sweep_test.cpp:1903), verbatim: "SC-026 worst over 42 preset(s): metric relative error 0 (limit 1e-05), checkpoint error 0 (limit 0.0001)". Non-vacuity gated by REQUIRE(compared == files.size()) at :1895 and REQUIRE(diverged.empty()) at :1907. No float bit digest introduced — comparison is via compareFingerprints/withinTolerance, and the ci.yml float-bit-golden lint is part of the check-portability/lint set that ran clean.

---

#### SC-027 — SC re-review — **PASS**

Measured from my own timed run (`seraphis_tests.exe -d yes`, whole-suite wall clock 484 s). The preset_render_sweep_test.cpp TU's nine cases: NoSilence 93.906 + BoundedAndFinite 0.002 + ChordBoundedAndFinite 47.710 + DecayMatchesRt60 0.000 + FrozenPresetsHold 0.009 + NoAudioThreadAllocation 1.585 + ConcurrentLoadIsRtSafe 0.128 + RendersAreReproducible 48.643 + PresetsAreDistinct 0.078 = 192.061 s = 3 min 12 s, against the 360 s (6 min) budget — 1.87x margin. No C-9 lever applied; no presets, rates or arms dropped.

---

#### SC-028 — SC re-review — **PASS**

`Seraphis_PresetSweep_PresetsAreDistinct` green. Unconditional WARN (preset_render_sweep_test.cpp:2072), verbatim from my run: "SC-028 over 861 pair(s): observedMinimum = 0.036591 held by Bells/Bronze Halo vs Drones/Continuum | median = 0.527398 | max = 0.940877 | pinned floor = max(0.02, 0.5 x observedMinimum) = 0.02". REQUIRE(pairCount == 861u) at :2005 and REQUIRE(belowFloor == 0u) at :2078 -> 0 of 861 pairs below the floor; min is 1.83x the floor. Validated from below by the [.measure] negative control, which I ran: "SC-028 negative control: d(original, level-only twin) = 0.000210024, must sit below the absolute floor 0.02" — so the floor demonstrably fires.

---

#### SC-029 — SC re-review — **FAIL**

Unchanged. Threshold: 0 presets without a recorded audition outcome; measured 42 without one. compliance.md:2440 "## SC-029 listening checkpoint (42 rows)"; :2446 "FILLED by T029, 2026-08-05 — with the outcome column HONESTLY EMPTY. Verdict: SC-029 is NOT MET." All 42 worksheet rows (:2472-2513, `Vellum` through `Aftermath`) carry **NOT AUDITIONED**; `grep -c 'NOT AUDITIONED'` = 46. The document's own clause table at :2519-2523 scores it: rows recorded 42/42 ✅, automated substitute none-used ✅, "Presets with a recorded audition outcome: 42 required — **0**, all 42 read NOT AUDITIONED" ❌. compliance.md:2720 repeats the ❌ in the SC roster. Requires the phase owner at a DAW; nothing in the codebase blocks it.

---

#### CC-rt — Constitution constraint — **PASS**

Only ONE audio-thread path changed this phase: `Processor::process()`'s event slice loop, `plugins/seraphis/src/processor/processor.cpp:1599-1708` (read in full this session). The change splits the event scan (`:1631-1645`) from the dispatch (`:1697-1703`) so `dispatchEvent` runs after `pushVoiceParams()`/`pushMacroSurfaces()` (`:1685-1686`). RT audit of the new lines: the only object constructed is `Vst::Event event{}` on the stack (`:1634`, `:1698`) — no heap, no container, no `std::function`; no mutex/atomic-wait/lock of any kind; no `throw`, no `try`, no exception-throwing call (`data.inputEvents->getEvent` is a host C-ABI call returning `tresult`, checked at `:1635` and `:1699`); control flow is `int32`/`std::size_t` arithmetic plus `std::min` (`:1641`, `:1652`, `:1674`). Neutral cost note (not a defect): `getEvent()` is now called twice per event (scan + dispatch) instead of once.
No other src/ file changed — `git diff --stat` lists only `processor.cpp` and `preset/seraphis_preset_config.h` (data-only category list) under `plugins/seraphis/src/`.
Measured, not asserted: `build/windows-x64-release/bin/Release/seraphis_tests.exe "Seraphis_PresetSweep_NoAudioThreadAllocation*,Seraphis_PresetSweep_ConcurrentLoadIsRtSafe*"` → verbatim `All tests passed (25 assertions in 2 test cases)`. Neither case is vacuous: the SC-015 case asserts `REQUIRE(!files.empty())` and `REQUIRE(chunks.size() == files.size())` then iterates every chunk (`plugins/seraphis/tests/integration/preset_render_sweep_test.cpp:1309`, `:1319`, `:1343`).
New test-thread code is RT-clean by construction: the audio-thread lambda at `preset_render_sweep_test.cpp:1493-1530` accumulates into locals and asserts nothing inside the scope (`:1505-1506` banner, `scanSample` at `:1507-1519`), sets the TLS opt-in before the scope opens (`:1497`) and calls `enableFTZDAZ()` (`:1500`) because MXCSR is fresh per thread on Windows.
The shared instrument change (`tests/test_helpers/allocation_detector.h`) is default-off and cannot perturb existing consumers: `recordAllocation()` reads the atomic filter FIRST and only then the `thread_local` (`:88` in the new body — `if (threadFilter_.load(acquire) && !tAllocationTrackThisThread) return;`), so with the filter off no TLS is touched from inside `operator new`; `ThreadScopedAllocationScope` restores both the filter and the per-thread flag in its destructor.

---

#### CC-layers — Constitution constraint — **PASS**

No new DSP component entered the layered tree at all: `git status --short dsp/` is EMPTY and `git ls-files --others --exclude-standard dsp/` returns nothing — so there is no new header that owes a layer declaration. Verified mechanically: `node tools/lint-layers.js` → exit 0, verbatim `lint-layers: OK — no layer-dependency violations in 5-layer DSP tree.` `node tools/lint-odr.js` → exit 0, verbatim `lint-odr: OK — 735 definitions scanned, no cross-file name collisions.`
The three new headers/TUs all sit ABOVE layer 4 (tools/ and plugins/*/tests/), so every include points downward:
- `tools/seraphis_preset_defs.h:36-43` — `"plugin_ids.h"` + `<array> <optional> <string> <string_view> <vector>`. No dsp include, no state layout, no serialization (C-3 held).
- `tools/seraphis_preset_generator.cpp:49-64` — plugin_ids.h, seraphis_preset_defs.h, seraphis_test_fixture.h, `public.sdk/.../memorystream.h`, stdlib. No VSTGUI header (FR-015), matched by the link line `target_link_libraries(seraphis_preset_generator PRIVATE KrateDSP KratePluginsShared sdk)` with no `vstgui_support` (`CMakeLists.txt` new block, git diff hunk at +592).
- `plugins/seraphis/tests/preset_test_support.h:45-81` — Seraphis `parameters/*.h`, SDK `fstreamer.h`/`memorystream.h`, and dsp layers 0/2/3 (`core/db_utils.h`, `processors/spectral_state.h`, `systems/harmonic_cloud.h`, `systems/seraphis_voice.h`). A plugin test header consuming layers 0-3 is downward-only.
Supporting lints all exit 0: `lint-simd-aligned-loadstore` (`clean -- 1472 file(s) scanned.`), `lint-float-bit-goldens` (`clean (1472 files scanned)`), `lint-midi-timing-goldens`, `lint-nonfinite-symbols` (`all clear (9 guarded files)`), `lint-allocation-operator-overrides` (`clean -- 1587 file(s) scanned.`), `lint-arch-guarded-includes`, `lint-platform-type-literals`, `lint-plugin-roster` (`7 plugins present in every roster`).

---

#### CC-naming — Constitution constraint — **PASS**

Checked every new file mechanically and by reading.
Classes/structs PascalCase: `ParamSetting` (`tools/seraphis_preset_defs.h:54`), `AuditionStimulus` (`:62`), `SeraphisPresetDef` (`:71`), `PresetFile` (`plugins/seraphis/tests/preset_test_support.h:146`), `DecodedPresetState` (`:367`), `SweepTimeline` (`:515`), `ThreadScopedAllocationScope` (`tests/test_helpers/allocation_detector.h`, new class in the diff). `SeraphisPresetDef` deliberately avoids the five existing anonymous-namespace `PresetDef` structs — ODR sweep confirms no collision (`lint-odr.js`: 735 definitions, no collisions).
Functions camelCase: `factoryPresetRoot` (`preset_test_support.h:117`), `allPresetFiles` (`:127`), `tagAt` (`:187`), `parseVstPreset` (`:202`), `parseInfoAttributes` (`:318`), `decodePresetState` (`:403`), `makeTimeline` (`:530`), `sampleIndex` (`:570`), `bufferIsFinite` (`:582`), `rmsOver` (`:597`), `perSecondRms` (`:620`), `fingerprintDistance` (`:656`); `allPresets` (`seraphis_preset_defs.h:204`), `findDef` (`:1076`), `buildSeraphisInfoXml` (`:1100`); `writeLE32`/`writeLE64`/`seraphisClassIdAscii`/`captureComponentState` (`tools/seraphis_preset_generator.cpp:98`, `:102`, `:111`, `:120`).
Constants kPascalCase: `kCategories` (`seraphis_preset_defs.h:93`), `kSampleRate`/`kBlockSize` (`seraphis_preset_generator.cpp:92-93`), `kSeraphisStateBytes` (`preset_test_support.h:92`), `kPartialCount` (`:95`), `kVstPresetHeaderBytes` (`:103`), `kChunkEntryBytes` (`:106`). A grep for `constexpr <type> <lowercase-name>` across all three new files returns only these k-prefixed hits.
Members trailing underscore: `priorFilterEnabled_`, `priorThreadOptIn_`, `count_`, `threadFilter_` in `allocation_detector.h`'s diff; the new structs are public aggregates (same convention as the existing `RenderFingerprint`), so no underscore is expected.
No new parameter IDs: `plugins/seraphis/src/plugin_ids.h` is absent from `git status --short`, so the k{Section}{Parameter}Id table is untouched and the surface stays at 107.
One judgement call, disclosed rather than hidden: `inline thread_local bool tAllocationTrackThisThread` (`tests/test_helpers/allocation_detector.h`, new) uses a `t` prefix for which the repo has NO precedent — `grep -rn thread_local dsp/include plugins/ tests/` returns zero other hits. It is mutable so `k` would be wrong; I judge it conforming.
Separate hygiene defect observed while auditing (NOT a naming-convention breach, recorded so it is not lost): `git status --short` shows eight junk untracked files in the repo ROOT, created by mis-quoted shell commands — `'`, `against`, `bl(x.H`, `last)`, `one`, `sample`, `setEffectsStageInstrumentedForTest(false)`, `sync)`, and `\316\223\303\252\303\20660`. They must be deleted before any commit.

---

#### CC-warnings — Constitution constraint — **PASS**

The zero-warning gate is now met on both toolchains and under clang-tidy — every number below is from a run I performed with forced recompilation, not read off the compliance document. (1) MSVC: I `touch`ed all 8 phase TUs (processor.cpp, controller.cpp, processor_audio_test.cpp, preset_render_sweep_test.cpp, editor_lifecycle_test.cpp, factory_preset_test.cpp, test_allocation_detector.cpp, seraphis_preset_generator.cpp) then built seraphis_tests + seraphis_preset_generator + shared_tests + Seraphis → BUILD_EXIT=0; the log shows 11 real .cpp compiles and `grep -ci warning` = **0**. The 5 DSP layer test targets rebuilt after the spectral_state.h / seraphis_engine.h edits → BUILD_EXIT=0, 0 warnings. (2) GCC 13.3.0 under WSL: after deleting both generator object files, `cmake --build build/linux-release --target seraphis_preset_generator` recompiled seraphis_preset_generator.cpp.o and processor.cpp.o → exit 0, `grep -c 'warning'` = **0** (was 7). Separately, `cmake --build build/linux-release --target seraphis_tests` recompiled all 6 Seraphis phase TUs (editor_lifecycle_test, controller.cpp, preset_render_sweep_test, factory_preset_test, processor_audio_test, processor.cpp) → exit 0, `grep -c 'warning'` = **0**. The three former GCC diagnostics are fixed at source, not suppressed: `detail::copyStateName` (spectral_state.h:348-365) replaces the unrollable `label[i] != '\0'` loop for `-Warray-bounds`; `std::memset(static_cast<void*>(&pendingFrame_), ...)` with `static_assert(std::is_trivially_copyable_v<CloudFrame>)` (processor.cpp:928-937) for `-Wclass-memaccess`; `const std::size_t voiceCount = std::min(polyphony_, kMaxVoices)` (seraphis_engine.h:1362-1370) for the subscript-32-into-`SeraphisVoice[16]` warning. All three are behaviour-preserving, and the DSP suites confirm it (dsp_core 1 588 257 assertions / 573 cases, dsp_primitives 4 549 290 / 1505, dsp_processors 10 640 879 / 3297, dsp_systems 6 045 010 / 1222, dsp_effects 98 155 / 477 — all "All tests passed"). (3) CLANG-TIDY: `-Target seraphis -BuildDir build/windows-ninja` → "Files analyzed: 35 / Errors: 0 / **Warnings: 0**" (was 50), `grep -c 'warning:'` = 0, with the three phase files confirmed in the analysed set. Residual coverage gap, unchanged: `tools/seraphis_preset_generator.cpp` is analysed by neither lint (run-clang-tidy.ps1's seraphis roots are plugins/seraphis/src + tests; check-portability.js skips it — "plugin_ids.h: No such file or directory"). Its only static evidence remains the two clean compiles above.

---

#### CC-portability — Constitution constraint — **PASS**

Ran as instructed. Verbatim result of `node tools/check-portability.js` (exit 0):
```
check-portability: 7 translation unit(s) with g++

  OK      plugins/seraphis/tests/integration/preset_render_sweep_test.cpp
  OK      plugins/seraphis/tests/unit/preset/factory_preset_test.cpp
  OK      plugins/shared/tests/test_allocation_detector.cpp
  skip    tools/seraphis_preset_generator.cpp  (plugin_ids.h: No such file or directory)
  OK      plugins/seraphis/src/processor/processor.cpp
  OK      plugins/seraphis/tests/integration/processor_audio_test.cpp
  OK      plugins/seraphis/tests/unit/controller/editor_lifecycle_test.cpp

check-portability: all clear -- 6 compiled, 1 skipped.
```
The one skip is a script include-path limitation, not a portability failure, and I closed it independently rather than accepting the tool's silence: the skipped TU compiles under the REAL GCC 13 toolchain with the real CMake include paths — WSL ninja step `[1/3] Building CXX object CMakeFiles/seraphis_preset_generator.dir/tools/seraphis_preset_generator.cpp.o` emitted **0** of the build's 7 warnings and the target linked (`[3/3] Linking CXX executable bin/seraphis_preset_generator`, exit 0). Toolchain: `g++ 13` under WSL, build dir `build/linux-release`.
Specific portability rules spot-checked in the new code:
- `-ffast-math` NaN rule honoured: `grep 'std::isnan\|std::isinf\|isfinite'` across all six new/changed files returns ONLY comment lines (`preset_test_support.h:39`, `:581`; `factory_preset_test.cpp:43`, `:279`; `preset_render_sweep_test.cpp:91`, `:109`). The real check is the bit-pattern pair at `preset_test_support.h:584` — `Krate::DSP::detail::isNaN(sample) || Krate::DSP::detail::isInf(sample)`, included from `<krate/dsp/core/db_utils.h>` at `:59`. `node tools/lint-nonfinite-symbols.js` → `all clear (9 guarded files)`.
- No bit-exact float golden: `node tools/lint-float-bit-goldens.js` → `clean (1472 files scanned)`; `node tools/lint-midi-timing-goldens.js` → `clean (1472 files scanned)`.
- SIMD alignment: `node tools/lint-simd-aligned-loadstore.js` → `clean -- 1472 file(s) scanned.` (no new SIMD in this phase).
- The `.vstpreset` class id is derived at run time, not hardcoded: `Seraphis::kProcessorUID.toString(buf)` in `seraphisClassIdAscii()` (`tools/seraphis_preset_generator.cpp:111-115`), and the container is written with explicit little-endian writers (`writeLE32`/`writeLE64`, `:98-104`).

---

## Implementation notes — deviations reported by task agents

These are the deviations, judgment calls, and corrections that the T001–T029 build/verification agents reported against the literal task text. Grouped by theme; task IDs cite the agent that reported each one. Full agent-by-agent notes are preserved in the workflow's task-note history — this section summarizes rather than re-quotes them in full, except where the exact wording matters.

**Instructed not to build/run, but did anyway where the task's own deliverable required it (disclosed every time, never silent):**
- **T001** built and ran `seraphis_tests` to populate the Baselines section, reasoning that skipping it would leave B-1 empty and give T026 nothing to reconcile SC-021 against.
- **T016** ran the *already-built* `seraphis_tests.exe -d yes` (no source change, no build) to get a real `[sweep]` timing instead of fabricating a projection — SC-027's own text states "the measured number governs; nothing in this task's arithmetic is evidence on its own."
- **T026** used `seraphis_tests.exe --list-tests` (enumerates the Catch2 registry, executes no test body) instead of a full run, as the minimum evidence that could verify SC-021's case-count claim without violating the no-build instruction.
- **T027** ran the full Release build, the full `seraphis_tests` suite, and pluginval, reasoning that T027's authoritative section (tasks.md:1361-1375) *is* a build/run/pluginval measurement task and that recording "pending" in every SC-020/SC-021/SC-027 cell would have been fabrication by omission.
- **T028** ran clang-tidy, the WSL/GCC generator build, and the determinism script twice, for the same reason as T027 — T028's deliverable is the recorded output of exactly those commands.

**Real defects found and fixed while executing a task (not dismissed as "pre-existing"):**
- **T027** found pluginval had validated a **stale 0.4.0 bundle** (T023's version.json bump never reached a linked binary because `cmake --build` alone does not re-run `configure_file` for `version.json` — no `CMAKE_CONFIGURE_DEPENDS` wiring exists). Fixed by re-running `cmake --preset windows-x64-release` and a full rebuild; re-validated pluginval against the correct v0.5.0.1 bundle. Flags a standing hazard for anyone building this branch after a version bump, outside this phase's file list.
- **T027** also caught that the rebuild above **relinked `seraphis_tests.exe`** after an earlier green run had already been recorded, and re-ran the full suite against the relinked binary rather than let a stale timestamp stand as evidence.
- **T028** found `clang-tidy` had been run against a **stale `compile_commands.json`** (dated before every Phase 12 file existed, which would have reported "clean" only because the new TUs had no compile command). Reconfigured first, then re-ran — this surfaced the real 50-warning count that FR-035/CC-warnings later drove to 0.
- **T028** found the WSL/GCC leg's 7 generator-build warnings were **not new** — replaying the shipped `Seraphis` plugin target's own compile command for `processor.cpp` reproduced the identical 7 warnings, i.e. a pre-existing Linux-leg condition of already-shipped source (`processor.cpp:926`, `spectral_state.h:478`), not something Phase 12 introduced. Recorded as such rather than attributed to new code; later cleaned to 0 (see CC-warnings / SC-018 in the table above).

**Deviations from the literal task pseudocode, all behavior-preserving:**
- **T018**: the task's literal `{ AllocationScope scope; ... } REQUIRE(scope.getAllocationCount() == 0);` pseudocode does not compile (`scope` is out of block scope at the `REQUIRE`). Used this repo's existing normative pattern instead (read the live atomic count as the last statement inside the scope, assert after it closes) — same semantics, matches `ui_perf_test.cpp` / `effects_perf_test.cpp` precedent.
- **T012**: added a vacuous-pass guard (fails instead of trivially passing if a run produces 0 files) and a negative-control run proving the determinism gate can actually fail — both defensive additions beyond the literal spec, non-behavior-relaxing.
- **T019**: delivers the twin's −3 dB via a block-0 parameter-automation point on the shipped `kMasterGainId` path rather than literally "cloning the definition and changing one ID" — equivalent in effect, reuses the host-facing path a real preset takes, documented in-file.

**Provisional/pending values shipped honestly rather than invented:**
- **T020**: could not run its own probe (build/run forbidden for that task), so it shipped the instrument with **every measurement cell in compliance.md marked PENDING** and explicitly stated "no tolerance constant exists anywhere in the tree yet" rather than guessing a number.
- **T021** (which depends on T020's unmeasured probe) had to declare `kScalarFieldTolerance = 1e-6` / `kPayloadFieldTolerance = 1e-4` as literal **sanity-band anchors, not measurements**, guarded by a `static_assert` that the two constants can never collapse into one, a loud in-code banner naming the outstanding pin step, and an unconditional WARN on every run surfacing the unmeasured state. FR-029a's own later measurement (run directly by this compliance pass) superseded these with real pinned values (`1.2e-7` / `1.2e-6`, see the FR-029a row above) — T021's provisional constants are no longer what ships.
- **T017**: shipped both tail-decay cases with the numeric thresholds intact but **without a measured margin transcribed anywhere** at write time — margins are emitted via unconditional `WARN` for the build agent to transcribe. Clause (b) of the frozen-preset case was promoted to a gating `REQUIRE` *ahead of* its own measured margin being known, on the stated principle that a report-only clause can never fail and the requirement is a gate, not a report.

**Scope corrections an agent made against the task text itself:**
- **T001** flagged that its authoritative task text miscited a CPU baseline line range (claimed 1,267,675–1,530,620 ns/block; the actual supported band from the same file is 1,228,760–1,530,620 ns/block) and recorded the corrected band rather than propagating the miscitation.
- **T023** found the changelog entry's own green exit code (`check-changelog-coverage.js` exit 0) did **not** mean the prose was complete — the entry described none of the ten commits actually in range. Expanded the entry to cover all ten (six Phase 11 editor commits + four Phase 11.5 performance commits), with a per-commit reconciliation table, rather than accept the passing exit code as sufficient.
- **T029** corrected a mislabeled baseline in this document's own Baselines section (B-4): a chain-only CPU figure (24.21%, "no Processor, no effects stage") had been mislabeled as "whole process(), polyphony 8" by an earlier pass. Using the mislabeled figure would have reported SC-025 green off a measurement that never included the Processor at all; the correction is what keeps SC-025 correctly recorded as FAIL in the table above.
- **T029** also recorded, as an explicit and disclosed deviation rather than silent compliance, that FR-025a's implementation does not literally "hand-skip" all four SpectralState payloads as the spec's original prose implies — the shipped `loadMorphParams` overload's third parameter *is* the payload destination, so those payloads decode through the shipped loader. This is an applied, documented deviation under tasks.md's own OI-3 allowance, not a gap.

**Reported but explicitly out of scope for the reporting task (flagged, not fixed):**
- **Untracked junk files at the repo root.** Reported independently by at least eight different task agents (T003, T004, T007, T008, T009, T010, T011, T012, T026, T027, T028) across the whole build: filenames that are evidently fragments of a mis-quoted shell command from an unrelated earlier session — `'`, `last)`, `one`, `sample`, `sync)`, `setEffectsStageInstrumentedForTest(false)`, `bl(x.H`, and one file with mojibake bytes. None of these are in any task's file list; no agent deleted them. **They must be cleaned up before any `git add` / commit** — a blind `git add -A` would pick them up.
- **T013** flagged that including `allocation_operator_overrides.h` in `shared_tests` (needed so the new thread-filter test has any allocations to count) routes *all* of `shared_tests`' allocations through the tracked malloc/free path — the one part of an otherwise purely-additive change that is not additive to that binary's existing behavior. Confirmed inert by T027's full-suite run (`shared_tests` unchanged pass/fail count), but flagged as the first place to look if that suite's results ever move.
- **T029 / FR-035**: while proving FR-035's gate, the build agent found the GCC 13.3.0 leg of `seraphis_tests` is **not green** — `Seraphis_EveryParameter_ReachesDsp`'s soft-limit-harmonic-signature section fails by a small margin (measured `−0.0102 dB` vs. a `0.035 dB` floor) that passes on Windows/MSVC by a 6% margin. This is a cross-toolchain-marginal **Phase 10** test arm (`param_reach_test.cpp`, last touched at `d63ba7dd`), not Phase 12 code, and FR-035's own gate clauses (which are Windows/MSVC-scoped) are unaffected — but the CI Linux leg will fail on it until someone widens the floor or tightens the DSP. Not owned by this phase; reported so it is not lost.
- **`.claude/workflows/seraphis-phase.js`** showed up modified in `git status` (20 insertions / 4 deletions) during T022's session; T022 explicitly stated it never opened or wrote that file and that it was already dirty in the working tree before T022 started.

## Remaining gates for the human loop

The following gates are **not** re-run or re-verified by this compliance report — they are the standing checklist items CLAUDE.md reserves for the human-in-the-loop step, listed here so nothing is silently skipped:

1. **clang-tidy** — `./tools/run-clang-tidy.ps1 -Target seraphis -BuildDir build/windows-ninja` (or the `.sh` equivalent on Linux/macOS). Last recorded result in this phase's evidence: 35 files analyzed, 0 errors, 0 warnings (FR-035 / CC-warnings rows above) — but that was measured mid-session before the final source state in this table; re-run once more immediately before commit to confirm nothing regressed from the last edit.
2. **pluginval** (phases 8+ rule: run whenever plugin code changed — it did, this phase touches `plugins/seraphis/src/processor/processor.cpp` and `plugins/seraphis/src/preset/seraphis_preset_config.h`) — `tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Seraphis.vst3"`. Last recorded result: exit 0, all sections completed (SC-020 row above) — re-run against a freshly-built bundle if any further source edits land, since T027 already found one stale-bundle validation this phase.
3. **Repo-root junk-file cleanup** — the ~8 stray untracked files listed above must be removed (or confirmed intentionally left, if they belong to some other in-flight work) before staging, so they don't ride along on a `git add`.
4. **Commit** — nothing in this phase has been committed. `git status` still shows the full Phase 12 changeset as untracked/modified. This is a deliberate hold: per project policy commits happen only when explicitly requested, and per the gap list above, the release verdict is DEFERRED (SC-025 CPU breach, SC-029/FR-034a audition gap, FR-029a/SC-017 macOS leg outstanding) — the phase owner should decide whether to commit the Phase 12 work now with DEFERRED release status recorded, or hold for the performance/audition/macOS work first.
5. **Not gated here, but required before a real 0.5.0 release** (outside this report's scope, listed for completeness since the table above surfaces them): a macOS/Apple-Clang run of `Seraphis_FactoryPresets_TreeToleranceProbe` / `Seraphis_FactoryPresets_TreeMatchesGenerator` (FR-029a, SC-017), a CI dry run confirming the same, a real performance-optimization pass on `Processor::process()` to close the SC-025 CPU breach (or an explicit accepted-DEFERRED decision), and the phase owner's 42-preset DAW audition pass (SC-029 / FR-034a).
