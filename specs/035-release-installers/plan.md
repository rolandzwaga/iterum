# Implementation Plan: Release Workflow with Platform-Specific Installers

**Branch**: `035-release-installers` | **Date**: 2025-12-28 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/035-release-installers/spec.md`

## Summary

Implement a GitHub Actions release workflow triggered by version tags (`v*`) that:
1. Calls the existing CI workflow to build and test all platforms
2. Downloads the build artifacts
3. Generates platform-specific installers (Windows .exe via Inno Setup, macOS .pkg via pkgbuild, Linux .tar.gz)
4. Publishes them as GitHub Release assets

## Technical Context

**Language/Version**: YAML (GitHub Actions), Inno Setup Script (.iss), Bash/PowerShell
**Primary Dependencies**: GitHub Actions, Inno Setup (Minionguyjpro/Inno-Setup-Action@v1.2.2), pkgbuild (macOS native), tar (Linux)
**Storage**: N/A (artifacts are ephemeral)
**Testing**: Manual verification + CI workflow success gates (Constitution Principle XII - testing through CI validation)
**Target Platform**: GitHub Actions runners (windows-latest, macos-latest, ubuntu-latest)
**Project Type**: CI/CD Infrastructure
**Performance Goals**: Complete release workflow in under 15 minutes (SC-001)
**Constraints**: No code signing for v1, existing CI must pass before release
**Scale/Scope**: Single plugin, 3 platforms, triggered by git tags

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

This feature is CI/CD infrastructure, not DSP code. Most constitution principles don't directly apply, but relevant ones are checked:

**Required Check - Principle VI (Cross-Platform Compatibility):**
- [x] All three platforms (Windows, macOS, Linux) will have installers
- [x] Installation paths follow official Steinberg specifications

**Required Check - Principle VIII (Testing Discipline):**
- [x] CI tests must pass before release proceeds (FR-004)
- [x] VST3 validator runs on all platforms in CI

**Required Check - Principle XII (Debugging Discipline):**
- [x] Workflow will include error logging and clear failure messages
- [x] Each step will have descriptive names for debugging

**Required Check - Principle XVI (Honest Completion):**
- [x] All FR/SC requirements will be verified against implementation

## Codebase Research (Principle XIV - ODR Prevention)

*GATE: N/A for CI/CD features - no C++ code created.*

This feature creates workflow files and scripts, not C++ classes. ODR prevention is not applicable.

### Existing Components to Reuse

| Component | Location | How It Will Be Used |
|-----------|----------|---------------------|
| CI Workflow | `.github/workflows/ci.yml` | Called via `workflow_call` before release |
| Build Artifacts | CI `upload-artifact` steps | Downloaded for installer packaging |
| Version Info | `CMakeLists.txt`, `src/version.h` | Extracted from git tag |
| Plugin Metadata | `src/version.h` | Vendor name, copyright for installers |

### Files Checked for Conflicts

- [x] `.github/workflows/` - Only ci.yml exists, no existing release workflow
- [x] `installers/` - Directory does not exist, will be created

### ODR Risk Assessment

**Risk Level**: N/A (No C++ code in this feature)

**Justification**: This feature creates YAML workflow files and installer scripts. No risk of C++ ODR violations.

## Dependency API Contracts (Principle XIV Extension)

### CI Workflow Contract

The release workflow depends on the CI workflow producing artifacts with specific names:

| Artifact Name | Platform | Content Path |
|---------------|----------|--------------|
| `Iterum-Windows-x64` | Windows | `build/VST3/Release/Iterum.vst3/` |
| `Iterum-macOS` | macOS | `build/VST3/Release/Iterum.vst3/` |
| `Iterum-Linux-x64` | Linux | `build/VST3/Release/Iterum.vst3/` |

### GitHub Actions API Contract

| Action | Version | Purpose |
|--------|---------|---------|
| `actions/checkout@v4` | v4 | Checkout repository |
| `actions/download-artifact@v4` | v4 | Download CI build artifacts |
| `softprops/action-gh-release@v2` | v2 | Create GitHub Release |
| `Minionguyjpro/Inno-Setup-Action@v1.2.2` | v1.2.2 | Compile Windows installer |

### Plugin Metadata (from version.h)

| Field | Value | Used In |
|-------|-------|---------|
| `stringPluginName` | "Iterum" | All installers |
| `stringCompanyName` | "Krate Audio" | Inno Setup publisher |
| `stringVendorURL` | "https://github.com/rolandzwaga/iterum" | Inno Setup |
| `stringLegalCopyright` | "Copyright (c) 2025 Krate Audio" | Inno Setup |

## Layer 0 Candidate Analysis

*N/A - This feature creates CI/CD infrastructure, not DSP code.*

## Higher-Layer Reusability Analysis

*N/A - This feature creates CI/CD infrastructure, not DSP code.*

### Forward Reusability Notes

The release workflow pattern established here could be reused for:
- Future VST plugins from the same organization
- AU (Audio Unit) format releases (with modifications)
- AAX format releases (with Pro Tools signing requirements)

The Inno Setup script could be parameterized as a template for other plugins.

## Project Structure

### Documentation (this feature)

```text
specs/035-release-installers/
├── plan.md              # This file
├── research.md          # Best practices research
├── quickstart.md        # How to create a release
├── checklists/
│   └── requirements.md  # Spec quality checklist
└── spec.md              # Feature specification
```

### Source Code (repository root)

```text
.github/
└── workflows/
    ├── ci.yml           # Existing CI workflow (modified to support workflow_call)
    └── release.yml      # NEW: Release workflow

installers/
├── windows/
│   └── setup.iss        # NEW: Inno Setup script
└── linux/
    └── README.txt       # NEW: Installation instructions
```

**Structure Decision**: Installer scripts live in `installers/` directory at repo root. The release workflow calls CI via `workflow_call` trigger.

## Implementation Tasks

### Task Group 1: Modify CI Workflow for Reusability

1. Add `workflow_call` trigger to existing `ci.yml`
2. Add output for build success status
3. Ensure artifact names are consistent

### Task Group 2: Create Inno Setup Script

1. Create `installers/windows/setup.iss` with:
   - Plugin name, version, publisher from metadata
   - Installation to `C:\Program Files\Common Files\VST3\`
   - Uninstaller entry in Add/Remove Programs
   - Silent installation support (`/SILENT`)
   - Admin privileges request

### Task Group 3: Create Linux README

1. Create `installers/linux/README.txt` with:
   - Installation instructions for `~/.vst3/` (user)
   - Installation instructions for `/usr/lib/vst3/` (system)
   - DAW verification steps

### Task Group 4: Create Release Workflow

1. Create `.github/workflows/release.yml`:
   - Trigger on `v*` tag push
   - Call CI workflow and wait for completion
   - Download artifacts from CI run
   - Build Windows installer using Inno Setup action
   - Build macOS installer using pkgbuild
   - Create Linux tar.gz archive
   - Create GitHub Release with all installers

### Task Group 5: Testing and Verification

1. Test workflow with a test tag (e.g., `v0.0.0-test`)
2. Verify all installers are created
3. Test Windows installer on Windows
4. Test macOS pkg on macOS
5. Test Linux tar.gz extraction
6. Update spec compliance table

## Complexity Tracking

No constitution violations - this is straightforward CI/CD infrastructure.

## Requirements Mapping

| Requirement | Implementation |
|-------------|----------------|
| FR-001: v* tag trigger | `release.yml` on: push: tags: ['v*'] |
| FR-002: Run CI first | workflow_call to ci.yml |
| FR-003: Download artifacts | actions/download-artifact@v4 |
| FR-004: Fail on CI failure | needs: build, if: success() |
| FR-005: Create GitHub Release | softprops/action-gh-release@v2 |
| FR-006: Version from tag | ${{ github.ref_name }} |
| FR-010: Inno Setup | Minionguyjpro/Inno-Setup-Action@v1.2.2 |
| FR-011: Windows install path | setup.iss DefaultDirName |
| FR-012: Uninstaller | setup.iss Uninstall section |
| FR-013: Metadata | setup.iss AppName, AppVersion, AppPublisher |
| FR-014: Silent install | setup.iss: /SILENT flag support (built-in) |
| FR-015: Admin privileges | setup.iss: PrivilegesRequired=admin |
| FR-016: Upgrade installs | setup.iss: DefaultDirName with overwrite (built-in Inno behavior) |
| FR-020: pkgbuild | macOS runner step |
| FR-021: macOS install path | pkgbuild --install-location |
| FR-022: Package identifier | pkgbuild --identifier com.krateaudio.iterum |
| FR-023: Version in pkg | pkgbuild --version |
| FR-030: Linux tar.gz | tar -czvf command |
| FR-031: README included | Include installers/linux/README.txt |
| FR-032: Paths documented | README.txt content |
| FR-040-042: Naming | Iterum-{version}-{platform}.{ext} |
