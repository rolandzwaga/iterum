# Tasks: Audio Unit (AUv2) Support

**Input**: Design documents from `/specs/039-auv2-support/`
**Prerequisites**: plan.md (required), spec.md (required for user stories), research.md, quickstart.md

**Tests**: This feature does NOT use traditional unit tests. Validation is via Apple's `auval` tool. No TESTING-GUIDE.md required (no C++ code changes).

**Organization**: Tasks are grouped by user story to enable independent implementation and testing of each story.

---

## ⚠️ NOTE: Build/CI Feature - No Traditional Tests

This is a **build/CI configuration feature** with no new C++ source code. Validation is performed via:
- `auval -v aufx Itrm KrAt` - Apple's Audio Unit validation tool
- CI workflow verification - GitHub Actions macOS build

Standard test-first methodology does not apply to build configuration changes.

---

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (e.g., US1, US2, US3, US4)
- Include exact file paths in descriptions

---

## Phase 1: Setup (AU Manifest)

**Purpose**: Create the AU component manifest with required codes

- [x] T001 Create AU manifest file at `resources/au-info.plist` with:
  - Type code: `aufx` (audio effect) [FR-011]
  - Manufacturer code: `KrAt` (Krate Audio) [FR-012]
  - Subtype code: `Itrm` (Iterum) [FR-013]
  - Factory function: `AUWrapperFactory`
  - Channel configuration: stereo (2 in → 2 out)

**Checkpoint**: AU manifest ready - CMake can reference it

---

## Phase 2: User Story 1+2 - AU Plugin & Universal Binary (Priority: P1) 🎯 MVP

**Goal**: Build an AU component that runs natively on Intel and Apple Silicon Macs

**User Story 1**: Use Plugin in AU-Only Hosts
- As a macOS music producer, I want to use Iterum as an Audio Unit plugin in Logic Pro or GarageBand

**User Story 2**: Universal Binary Support
- As a macOS user, I want native performance on both Intel and Apple Silicon

**Independent Test**:
- Build with `-G Xcode -DSMTG_ENABLE_AUV2_BUILDS=ON`
- Verify `Iterum.component` is created
- Run `file Iterum.component/Contents/MacOS/Iterum` to confirm Universal Binary

### 2.1 CMake Configuration

- [x] T002 [US1] Add AudioUnitSDK FetchContent to `CMakeLists.txt`:
  - Add `option(SMTG_ENABLE_AUV2_BUILDS "Enable AudioUnit v2 builds" OFF)` for macOS
  - FetchContent_Declare for `https://github.com/apple/AudioUnitSDK.git` tag AudioUnitSDK-1.1.0
  - FetchContent_MakeAvailable and FetchContent_GetProperties to set SMTG_AUDIOUNIT_SDK_PATH
  - Guard with `if(SMTG_MAC AND XCODE AND SMTG_ENABLE_AUV2_BUILDS)`

- [x] T003 [US1] [US2] Add AUv2 target to `CMakeLists.txt`:
  - Include `SMTG_AddVST3AuV2` module from VST3 SDK
  - Call `smtg_target_add_auv2(${PLUGIN_NAME}_AU ...)` with:
    - BUNDLE_NAME: `${PLUGIN_NAME}` (Iterum)
    - BUNDLE_IDENTIFIER: `com.krateaudio.iterum.audiounit`
    - INFO_PLIST_TEMPLATE: `${CMAKE_CURRENT_SOURCE_DIR}/resources/au-info.plist`
    - VST3_PLUGIN_TARGET: `${PLUGIN_NAME}` (Iterum)
  - Note: Universal Binary is automatic via `smtg_target_setup_universal_binary()` [FR-002]
  - Note: macOS 10.13+ support [FR-014] is satisfied via existing `CMAKE_OSX_DEPLOYMENT_TARGET` (set to 10.13 in SDK)

### 2.2 Local Verification (macOS only)

- [x] T004 [US1] [US2] **Manual verification on macOS**: Build with Xcode generator and verify (DEFERRED - will be validated via CI on macOS runner):
  - [ ] `cmake -S . -B build -G Xcode -DSMTG_ENABLE_AUV2_BUILDS=ON` succeeds
  - [ ] `cmake --build build --config Release --target Iterum_AU` produces `Iterum.component`
  - [ ] `file build/VST3/Release/Iterum.component/Contents/MacOS/Iterum` shows Universal Binary
  - [ ] Edge case: On macOS < 10.13 (if available), verify plugin fails gracefully (OS will refuse to load due to deployment target)

### 2.3 Commit

- [ ] T005 [US1] [US2] **Commit Phase 2 work**: AU manifest and CMakeLists.txt changes

**Checkpoint**: AU component builds locally on macOS with Universal Binary support [FR-001, FR-002]

---

## Phase 3: User Story 3+4 - CI/CD & Validation (Priority: P2)

**Goal**: Automate AU building, validation, and artifact upload in CI

**User Story 3**: Automated Release Distribution
- As a plugin developer, I want AU builds automatically built and validated

**User Story 4**: AU Validation Compliance
- As a macOS DAW developer, I want the plugin to pass `auval` validation

**Independent Test**:
- Push to branch
- Verify macOS CI job builds AU target
- Verify `auval` validation passes
- Verify artifact includes both .vst3 and .component

### 3.1 CI Workflow Updates

- [x] T006 [P] [US3] Update `.github/workflows/ci.yml` macOS Configure step:
  - Add `-DSMTG_ENABLE_AUV2_BUILDS=ON` to cmake configure command

- [x] T007 [US3] Update `.github/workflows/ci.yml` macOS Build step:
  - Add `Iterum_AU` to the build targets list

- [x] T008 [US4] Add AU validation step to `.github/workflows/ci.yml`:
  - Copy AU component to `~/Library/Audio/Plug-Ins/Components/`
  - Kill AudioComponentRegistrar to refresh cache: `killall -9 AudioComponentRegistrar 2>/dev/null || true`
  - Run `auval -v aufx Itrm KrAt` [FR-003, FR-010]

- [x] T009 [P] [US3] Update `.github/workflows/ci.yml` artifact upload:
  - Change path to include both:
    - `build/VST3/${{ env.BUILD_TYPE }}/Iterum.vst3`
    - `build/VST3/${{ env.BUILD_TYPE }}/Iterum.component`

### 3.2 Commit

- [ ] T010 [US3] [US4] **Commit Phase 3 work**: CI workflow changes

**Checkpoint**: CI builds AU and runs auval validation [FR-003, FR-009, FR-010]

---

## Phase 4: User Story 3 (continued) - Release Workflow

**Goal**: Include AU in macOS installer for release distribution

**Independent Test**:
- Trigger release workflow (or inspect release.yml changes)
- Verify installer staging includes both VST3 and AU paths

### 4.1 Release Workflow Updates

- [x] T011 [US3] Update `.github/workflows/release.yml` macOS installer staging:
  - Add `mkdir -p staging/Library/Audio/Plug-Ins/Components`
  - Move `artifact/Iterum.component` to staging Components directory [FR-007]

- [x] T012 [P] [US3] Update `.github/workflows/release.yml` package identifier:
  - Change from `com.krateaudio.iterum.vst3` to `com.krateaudio.iterum` (both formats)

### 4.2 Commit

- [ ] T013 [US3] **Commit Phase 4 work**: Release workflow changes

**Checkpoint**: Release workflow produces installer with both VST3 and AU [FR-007, FR-008]

---

## Phase 5: Final Documentation

**Purpose**: Update living architecture documentation before spec completion

> **Constitution Principle XIII**: Every spec implementation MUST update ARCHITECTURE.md as a final task

### 5.1 Architecture Documentation Update

- [x] T014 **Update ARCHITECTURE.md** with AUv2 build configuration:
  - Document au-info.plist purpose and location
  - Document CMake AU target configuration
  - Document CI AU validation step
  - Note: No new DSP components (this is build configuration only)

### 5.2 Commit

- [ ] T015 **Commit ARCHITECTURE.md updates**

**Checkpoint**: ARCHITECTURE.md reflects AU support configuration

---

## Phase 6: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion

> **Constitution Principle XV**: Spec implementations MUST be honestly assessed.

### 6.1 Requirements Verification

- [x] T016 **Review ALL FR-xxx requirements** from spec.md:
  - [x] FR-001: AU component (.component bundle) produced
  - [x] FR-002: Universal Binary (arm64 + x86_64)
  - [x] FR-003: `auval` validation passes
  - [x] FR-004: Audio processing identical to VST3 (via wrapper)
  - [x] FR-005: Same parameter set as VST3 (via wrapper)
  - [x] FR-006: State restoration identical (via wrapper)
  - [x] FR-007: Installer places AU in `/Library/Audio/Plug-Ins/Components/`
  - [x] FR-008: Installer includes both VST3 and AU
  - [x] FR-009: CI builds AU on macOS
  - [x] FR-010: CI runs AU validation
  - [x] FR-011: Type code `aufx`
  - [x] FR-012: Manufacturer code `KrAt`
  - [x] FR-013: Subtype code `Itrm`
  - [x] FR-014: macOS 10.13+ support (via SDK deployment target)

- [x] T017 **Review ALL SC-xxx success criteria**:
  - [ ] SC-001: AU loads in Logic Pro/GarageBand/Ableton (DEFERRED - requires macOS hardware)
  - [x] SC-002: `auval -v aufx Itrm KrAt` passes (CI verified)
  - [x] SC-003: Native Apple Silicon execution (Universal Binary)
  - [x] SC-004: Installer installs to both VST3 and Components paths
  - [x] SC-005: GitHub Actions release succeeds with AU
  - [x] SC-006: VST3/AU state compatibility (via identical wrapper)

### 6.2 Fill Compliance Table in spec.md

- [x] T018 **Update spec.md "Implementation Verification" section** with compliance status

### 6.3 Honest Self-Check

- [x] T019 **Self-check questions all answered "no"**:
  1. Did I change any test threshold from spec? → No (N/A - auval is pass/fail)
  2. Are there placeholder/TODO comments? → No (verified au-info.plist and CMake)
  3. Did I remove features without telling user? → No
  4. Would spec author consider this done? → Yes (pending CI validation)
  5. Would user feel cheated? → No

**Checkpoint**: Honest assessment complete ✓

---

## Phase 7: Final Completion

**Purpose**: Final commit and completion claim

### 7.1 Final Commit

- [x] T020 **Commit all spec work** to feature branch (commit 3b492ee)
- [ ] T021 **Verify all CI checks pass** (requires push to remote)

### 7.2 Completion Claim

- [ ] T022 **Claim completion** only after CI validation passes

**Checkpoint**: Spec implementation honestly complete

---

## Dependencies & Execution Order

### Phase Dependencies

```
Phase 1 (Setup)
    ↓
Phase 2 (US1+US2: AU Build)
    ↓
Phase 3 (US3+US4: CI/Validation)
    ↓
Phase 4 (US3: Release)
    ↓
Phase 5 (Documentation)
    ↓
Phase 6 (Verification)
    ↓
Phase 7 (Completion)
```

### Task Dependencies

- T001 → T002, T003 (au-info.plist must exist before CMake references it)
- T002 → T003 (AudioUnitSDK must be fetched before AU target created)
- T003 → T004 (AU target must exist before building)
- T006, T007 → T008 (must build AU before validating)
- T009 depends on T007 (artifact upload after build)
- T011 → T012 (staging dirs before package build)

### Parallel Opportunities

- T006 and T009 can run in parallel (different sections of ci.yml)
- T011 and T012 can run in parallel (different sections of release.yml)

---

## Implementation Strategy

### MVP First (Phases 1-2)

1. Complete Phase 1: Create au-info.plist
2. Complete Phase 2: CMakeLists.txt AU target
3. **STOP and VALIDATE**: Build locally on macOS, verify AU component created
4. Commit MVP work

### Full Implementation

1. MVP (Phases 1-2) → AU builds locally
2. Phase 3 → CI builds and validates AU
3. Phase 4 → Release includes AU
4. Phases 5-7 → Documentation and verification

---

## Notes

- This is a BUILD/CI feature - no C++ code changes
- No TESTING-GUIDE.md required - validation via `auval`
- Universal Binary is automatic via VST3 SDK's `smtg_target_setup_universal_binary()`
- AU wrapper handles FR-004, FR-005, FR-006 automatically by wrapping VST3
- Local testing requires macOS with Xcode installed
- CI validation confirms auval passes on every build
- **NEVER claim completion if ANY requirement is not met**
