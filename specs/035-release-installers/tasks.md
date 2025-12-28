# Tasks: Release Workflow with Platform-Specific Installers

**Input**: Design documents from `/specs/035-release-installers/`
**Prerequisites**: plan.md (required), spec.md (required for user stories), research.md, quickstart.md

**Tests**: This feature is CI/CD infrastructure. Testing is performed through workflow execution and manual installer verification, not unit tests.

**Organization**: Tasks are grouped to enable independent implementation while respecting dependencies between installer components.

---

## User Stories Summary

| Story | Description | Priority |
|-------|-------------|----------|
| US1 | Windows User Installs Plugin | P1 |
| US2 | macOS User Installs Plugin | P1 |
| US3 | Linux User Installs Plugin | P2 |
| US4 | Developer Creates Release | P1 |

---

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (e.g., US1, US2, US3, US4)
- Include exact file paths in descriptions

---

## Phase 1: Setup

**Purpose**: Create directory structure for installer files

- [x] T001 Create installer directories: `installers/windows/` and `installers/linux/`
- [x] T002 Verify existing CI workflow artifact names match expected: `Iterum-Windows-x64`, `Iterum-macOS`, `Iterum-Linux-x64`

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Modify CI workflow to support being called by release workflow

**CRITICAL**: The release workflow cannot call CI until this is complete

- [x] T003 Add `workflow_call` trigger to `.github/workflows/ci.yml` (add to existing `on:` block)
- [ ] T004 Verify CI workflow still works for push/PR triggers after modification

**Checkpoint**: CI workflow supports both direct triggers and workflow_call

---

## Phase 3: User Story 1 - Windows User Installs Plugin (Priority: P1)

**Goal**: Windows users can download and run an .exe installer that places the plugin in the correct VST3 folder

**Independent Test**: Download installer, run it on Windows, verify plugin appears in `C:\Program Files\Common Files\VST3\Iterum.vst3`

### 3.1 Implementation

- [x] T005 [US1] Create Inno Setup script in `installers/windows/setup.iss` with:
  - AppName=Iterum, AppVersion={#Version}, AppPublisher=Krate Audio
  - DefaultDirName={commonpf}\Common Files\VST3
  - PrivilegesRequired=admin
  - Uninstaller entry
  - RecurseCopy of Iterum.vst3 bundle (FR-010 through FR-015)

### 3.2 Verification

- [ ] T006 [US1] Verify Inno Setup script syntax is valid (compile test with iscc if available locally)

**Checkpoint**: Inno Setup script ready for use in release workflow

---

## Phase 4: User Story 3 - Linux User Installs Plugin (Priority: P2)

**Goal**: Linux users can download a tar.gz archive with clear installation instructions

**Independent Test**: Download archive, extract, copy to `~/.vst3/`, verify plugin loads in Bitwig/Ardour

### 4.1 Implementation

- [x] T007 [P] [US3] Create installation README in `installers/linux/README.txt` with:
  - Quick install command: `cp -r Iterum.vst3 ~/.vst3/`
  - System-wide install: `sudo cp -r Iterum.vst3 /usr/lib/vst3/`
  - Verification steps
  - Supported DAWs list
  - GitHub URL for issues (FR-031, FR-032)

**Checkpoint**: Linux README ready for inclusion in archive

---

## Phase 5: User Story 4 - Developer Creates Release (Priority: P1)

**Goal**: Developers can push a version tag and have all platform installers automatically created and published

**Independent Test**: Push tag `v0.0.0-test`, verify workflow runs, all three installers attached to GitHub Release

### 5.1 Implementation - Release Workflow Core

- [x] T008 [US4] Create `.github/workflows/release.yml` with:
  - Trigger: `on: push: tags: ['v*']` (FR-001)
  - Job `build`: Call CI workflow via `uses: ./.github/workflows/ci.yml` (FR-002)
  - Concurrency settings to prevent duplicate runs

### 5.2 Implementation - Windows Installer Job

- [x] T009 [US1] [US4] Add Windows installer job to `release.yml`:
  - `needs: build`
  - Download `Iterum-Windows-x64` artifact (FR-003)
  - Extract version from tag: `${{ github.ref_name }}` (FR-006)
  - Use `Minionguyjpro/Inno-Setup-Action@v1.2.2` with `installers/windows/setup.iss` (FR-010)
  - Output: `Iterum-{version}-Windows-x64.exe` (FR-040)

### 5.3 Implementation - macOS Installer Job (US2)

- [x] T010 [US2] [US4] Add macOS installer job to `release.yml`:
  - `needs: build`
  - Download `Iterum-macOS` artifact (FR-003)
  - Run `pkgbuild` with (FR-020):
    - `--identifier com.krateaudio.iterum.vst3` (FR-022)
    - `--version {version}` (FR-023)
    - `--install-location /Library/Audio/Plug-Ins/VST3/Iterum.vst3` (FR-021)
  - Output: `Iterum-{version}-macOS.pkg` (FR-041)

### 5.4 Implementation - Linux Archive Job (US3)

- [x] T011 [US3] [US4] Add Linux archive job to `release.yml`:
  - `needs: build`
  - Download `Iterum-Linux-x64` artifact (FR-003)
  - Copy `installers/linux/README.txt` to staging directory (FR-031)
  - Create archive: `tar -czvf Iterum-{version}-Linux-x64.tar.gz Iterum.vst3 README.txt` (FR-030)
  - Output: `Iterum-{version}-Linux-x64.tar.gz` (FR-042)

### 5.5 Implementation - GitHub Release Job

- [x] T012 [US4] Add GitHub Release job to `release.yml`:
  - `needs: [windows-installer, macos-installer, linux-archive]`
  - Gate: Only proceed if all installer jobs succeed (FR-004)
  - Use `softprops/action-gh-release@v2` (FR-005)
  - Attach all three installer files
  - Use tag as release name

### 5.6 Commit

- [x] T013 [US4] Commit all release workflow files to feature branch

**Checkpoint**: Complete release workflow ready for testing

---

## Phase 6: Integration Testing

**Purpose**: Verify the complete workflow works end-to-end

- [ ] T014 Push feature branch and create PR to verify CI still passes
- [ ] T015 After PR merge, create test tag `v0.0.0-test` to trigger release workflow
- [ ] T016 Verify release workflow completes successfully (SC-001: under 15 minutes)
- [ ] T017 Verify all three installers are attached to GitHub Release (SC-002)

---

## Phase 7: Platform Verification

**Purpose**: Manually verify each installer works correctly on its target platform

### 7.1 Windows Verification (US1)

- [ ] T018 [US1] Download and run Windows installer on Windows machine
- [ ] T019 [US1] Verify plugin installed to `C:\Program Files\Common Files\VST3\Iterum.vst3` (SC-003)
- [ ] T020 [US1] Verify Iterum appears in DAW plugin list
- [ ] T021 [US1] Verify uninstaller appears in Add/Remove Programs (FR-012)
- [ ] T022 [US1] Test silent install with `/SILENT` flag (FR-014)

### 7.2 macOS Verification (US2)

- [ ] T023 [US2] Download and run macOS installer on Mac
- [ ] T024 [US2] Verify plugin installed to `/Library/Audio/Plug-Ins/VST3/Iterum.vst3` (SC-004)
- [ ] T025 [US2] Verify Iterum appears in DAW plugin list

### 7.3 Linux Verification (US3)

- [ ] T026 [US3] Download and extract Linux archive
- [ ] T027 [US3] Verify README.txt is included with correct instructions (FR-031, FR-032)
- [ ] T028 [US3] Copy to `~/.vst3/` and verify plugin loads in DAW (SC-005)

### 7.4 Failure Case Verification (US4)

- [ ] T029 [US4] Verify that if CI tests fail, release is NOT created (SC-006)
- [ ] T030 [US4] Verify installer file sizes are under 50MB each (SC-007)

---

## Phase 8: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion

> **Constitution Principle XVI**: Spec implementations MUST be honestly assessed.

> **Note**: ARCHITECTURE.md update is NOT required for this feature. Constitution Principle XIII applies to DSP/code components, not CI/CD infrastructure. This feature creates workflow files and installer scripts that don't add to the layered DSP architecture.

### 8.1 Requirements Verification

- [ ] T031 **Review ALL FR-xxx requirements** from spec.md against implementation:
  - FR-001 through FR-006 (Workflow)
  - FR-010 through FR-016 (Windows)
  - FR-020 through FR-023 (macOS)
  - FR-030 through FR-032 (Linux)
  - FR-040 through FR-042 (Naming)

- [ ] T032 **Review ALL SC-xxx success criteria**:
  - SC-001: Workflow completes in under 15 minutes
  - SC-002: All three installers attached to release
  - SC-003: Windows installer places plugin correctly
  - SC-004: macOS installer places plugin correctly
  - SC-005: Linux archive works correctly
  - SC-006: Failed CI prevents release
  - SC-007: Installer sizes under 50MB

### 8.2 Fill Compliance Table in spec.md

- [ ] T033 **Update spec.md "Implementation Verification" section** with compliance status for each requirement
- [ ] T034 **Mark overall status honestly**: COMPLETE / NOT COMPLETE / PARTIAL

### 8.3 Final Commit

- [ ] T035 **Commit all work** including spec.md compliance updates
- [ ] T036 **Verify all tests pass** (CI workflow must pass)

**Checkpoint**: Honest assessment complete - spec implementation verified

---

## Dependencies & Execution Order

### Phase Dependencies

```
Phase 1 (Setup) ──────────────────────────────────────────────────────────►
                 │
                 ▼
Phase 2 (Foundational: CI workflow_call) ─────────────────────────────────►
                 │                                                         │
                 ├────────────────┬─────────────────┐                      │
                 ▼                ▼                 ▼                      │
Phase 3 (US1)    Phase 4 (US3)                                            │
Inno Setup       Linux README     ─────────────────────────────────────────┤
                 │                │                                        │
                 └────────────────┴─────────────────┐                      │
                                                    ▼                      │
                              Phase 5 (US4: Release Workflow) ◄────────────┘
                                  │
                                  ▼
                              Phase 6 (Integration Testing)
                                  │
                                  ▼
                              Phase 7 (Platform Verification)
                                  │
                                  ▼
                              Phase 8 (Completion)
```

### User Story Dependencies

| Story | Dependencies | Can Start After |
|-------|--------------|-----------------|
| US1 (Windows) | Phase 2 | Foundational complete |
| US2 (macOS) | Phase 2 | Foundational complete (no external files needed) |
| US3 (Linux) | Phase 2 | Foundational complete |
| US4 (Release) | US1, US2, US3 setup files | All installer files ready |

### Parallel Opportunities

- **Phase 3 & 4**: Inno Setup script (T005) and Linux README (T007) can be created in parallel
- **Phase 5.2, 5.3, 5.4**: All installer jobs in release.yml can be written in parallel (different sections)
- **Phase 7.1, 7.2, 7.3**: Platform verifications can run in parallel (different machines)

---

## Implementation Strategy

### MVP First (All User Stories Required)

Unlike typical features, this is infrastructure where ALL user stories must work for the feature to be useful. However, you can validate incrementally:

1. Complete Phase 1-2: Setup + CI modification
2. Complete Phase 3-4: Create Inno Setup script + Linux README
3. Complete Phase 5: Create full release.yml
4. **STOP and TEST**: Push tag, verify workflow runs
5. Fix any issues before platform verification

### Verification Order

1. **Workflow runs**: Tag triggers workflow, CI is called, installers are created
2. **Artifacts correct**: All three installers attached to release with correct names
3. **Installers work**: Each installer functions on its target platform

---

## Notes

- This is CI/CD infrastructure, not DSP code - no TESTING-GUIDE.md or unit tests required
- Testing is through workflow execution and manual installer verification
- All installer jobs depend on CI success (FR-004)
- Code signing is OUT OF SCOPE for v1 (documented in spec assumptions)
- Workflow should complete in under 15 minutes (SC-001)
- Each installer should be under 50MB (SC-007)
