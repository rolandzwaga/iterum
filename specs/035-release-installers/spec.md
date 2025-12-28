# Feature Specification: Release Workflow with Platform-Specific Installers

**Feature Branch**: `035-release-installers`
**Created**: 2025-12-28
**Status**: Draft
**Input**: User description: "Create a release workflow that generates platform-specific installers for the Iterum VST3 plugin. The workflow should: 1) Run the existing CI workflow first to build and test, 2) Grab the build artifacts from CI, 3) Generate a Windows installer using Inno Setup (Minionguyjpro/Inno-Setup-Action@v1.2.2), 4) Generate a macOS installer using pkgbuild, 5) Generate a Linux tar.gz archive."

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Windows User Installs Plugin (Priority: P1)

A Windows user downloads the Iterum installer (.exe), runs it, and has the VST3 plugin automatically installed to the correct system location. They can immediately open their DAW and find Iterum in the plugin list.

**Why this priority**: Windows is the largest DAW user platform. A proper installer provides a professional user experience and ensures the plugin is discoverable by all VST3 hosts.

**Independent Test**: Can be fully tested by downloading the installer artifact, running it on a Windows machine, and verifying the plugin appears in a DAW like Reaper or Ableton.

**Acceptance Scenarios**:

1. **Given** a Windows user downloads the installer, **When** they run the .exe installer, **Then** the plugin is installed to `C:\Program Files\Common Files\VST3\Iterum.vst3`
2. **Given** the installation completes, **When** the user opens their DAW and rescans plugins, **Then** Iterum appears in the VST3 plugin list
3. **Given** the user wants to uninstall, **When** they use Windows Add/Remove Programs, **Then** the plugin is cleanly removed

---

### User Story 2 - macOS User Installs Plugin (Priority: P1)

A macOS user downloads the Iterum installer (.pkg), double-clicks it, and has the VST3 plugin automatically installed to the system Audio Plug-Ins folder. They can immediately open their DAW and find Iterum.

**Why this priority**: macOS is the second largest DAW platform. A .pkg installer provides native macOS installation experience with proper system integration.

**Independent Test**: Can be fully tested by downloading the .pkg artifact, running it on macOS, and verifying the plugin appears in Logic Pro, Ableton, or another DAW.

**Acceptance Scenarios**:

1. **Given** a macOS user downloads the installer, **When** they double-click the .pkg file, **Then** the standard macOS installer wizard appears
2. **Given** installation completes, **When** the plugin is installed, **Then** it exists at `/Library/Audio/Plug-Ins/VST3/Iterum.vst3`
3. **Given** the installation completes, **When** the user opens their DAW, **Then** Iterum appears in the VST3 plugin list

---

### User Story 3 - Linux User Installs Plugin (Priority: P2)

A Linux user downloads a tar.gz archive containing the Iterum VST3 bundle, extracts it, and copies it to their VST3 plugin directory. They can then open their DAW and find Iterum.

**Why this priority**: Linux is a smaller but dedicated DAW user base. A tar.gz archive is the standard distribution format and allows users flexibility in installation location.

**Independent Test**: Can be fully tested by downloading the tar.gz, extracting it, copying to ~/.vst3/, and verifying the plugin loads in Bitwig or Ardour.

**Acceptance Scenarios**:

1. **Given** a Linux user downloads the tar.gz, **When** they extract it, **Then** they find Iterum.vst3 bundle ready to copy
2. **Given** the user copies the bundle to `~/.vst3/`, **When** they open their DAW, **Then** Iterum appears in the VST3 plugin list
3. **Given** the archive, **When** extracted, **Then** it includes a README with installation instructions

---

### User Story 4 - Developer Creates Release (Priority: P1)

A developer creates a Git tag for a new version. The release workflow automatically builds all platforms, runs tests, generates installers for each platform, and publishes them as GitHub Release assets.

**Why this priority**: Automated release process ensures consistent, reproducible builds and reduces manual error in the release process.

**Independent Test**: Can be tested by pushing a version tag and verifying all platform installers appear on the GitHub Releases page.

**Acceptance Scenarios**:

1. **Given** a developer pushes a tag matching `v*` pattern, **When** GitHub Actions runs, **Then** the release workflow is triggered
2. **Given** CI tests pass, **When** the release workflow completes, **Then** Windows .exe, macOS .pkg, and Linux .tar.gz are attached to a GitHub Release
3. **Given** a CI test fails, **When** the release workflow runs, **Then** no installers are created and the release is not published

---

### Edge Cases

- What happens when the CI build fails? Release workflow should not proceed; no partial releases should be created.
- What happens when artifact download fails? Workflow should fail with clear error message.
- What happens when Inno Setup compilation fails? Workflow should fail with the Inno Setup error logs available.
- How does the installer handle existing installations? Installers should support upgrade installations, replacing previous versions cleanly.
- What happens on macOS if the user doesn't have admin privileges? The .pkg installer should request elevation as standard macOS behavior.

## Requirements *(mandatory)*

### Functional Requirements

#### GitHub Workflow Requirements

- **FR-001**: System MUST trigger the release workflow when a Git tag matching `v*` pattern is pushed
- **FR-002**: System MUST run the existing CI workflow first to build and test all platforms before creating installers
- **FR-003**: System MUST download build artifacts from the CI workflow for each platform
- **FR-004**: System MUST fail the entire release if any CI tests fail
- **FR-005**: System MUST create a GitHub Release with all platform installers attached
- **FR-006**: System MUST use the Git tag as the version number in all installers

#### Windows Installer Requirements

- **FR-010**: System MUST generate a Windows installer using Inno Setup via `Minionguyjpro/Inno-Setup-Action@v1.2.2`
- **FR-011**: System MUST install the VST3 plugin to `C:\Program Files\Common Files\VST3\`
- **FR-012**: System MUST create an uninstaller entry in Windows Add/Remove Programs
- **FR-013**: System MUST include plugin name, version, and publisher information in the installer
- **FR-014**: Installer MUST support silent installation via command-line flag (`/SILENT`)
- **FR-015**: Installer MUST request administrator privileges for system-wide installation
- **FR-016**: Installer MUST support upgrade installations, replacing previous versions cleanly without requiring manual uninstall

#### macOS Installer Requirements

- **FR-020**: System MUST generate a macOS installer package using `pkgbuild`
- **FR-021**: System MUST install the VST3 plugin to `/Library/Audio/Plug-Ins/VST3/`
- **FR-022**: System MUST include package identifier following reverse-domain convention (`com.krateaudio.iterum.vst3`)
- **FR-023**: System MUST include version information in the package metadata

#### Linux Archive Requirements

- **FR-030**: System MUST generate a tar.gz archive containing the VST3 bundle
- **FR-031**: Archive MUST include a README file with installation instructions
- **FR-032**: README MUST document standard VST3 paths: `~/.vst3/` (user) and `/usr/lib/vst3/` (system)

#### Installer Naming Requirements

- **FR-040**: Windows installer MUST be named `Iterum-{version}-Windows-x64.exe`
- **FR-041**: macOS installer MUST be named `Iterum-{version}-macOS.pkg`
- **FR-042**: Linux archive MUST be named `Iterum-{version}-Linux-x64.tar.gz`

### Key Entities

- **Release Workflow**: GitHub Actions workflow triggered by version tags that orchestrates the entire release process
- **Build Artifact**: Compiled VST3 plugin bundle from CI workflow for each platform
- **Installer Package**: Platform-specific installation package (exe/pkg/tar.gz) that delivers the plugin to users
- **Inno Setup Script (.iss)**: Configuration file defining Windows installer behavior, files, and registry entries
- **GitHub Release**: A named release on GitHub containing version information and downloadable installer assets

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: Release workflow completes in under 15 minutes total (including CI time)
- **SC-002**: All three platform installers are generated and attached to GitHub Release
- **SC-003**: Windows installer correctly places plugin in VST3 folder (verified by DAW detection)
- **SC-004**: macOS installer correctly places plugin in system Audio Plug-Ins folder (verified by DAW detection)
- **SC-005**: Linux archive extracts correctly and plugin loads in compatible DAW
- **SC-006**: Failed CI tests prevent release creation 100% of the time
- **SC-007**: Installer file sizes are appropriate (each under 50MB for the plugin bundle)

## Assumptions & Existing Components *(mandatory)*

### Assumptions

- The existing CI workflow produces valid build artifacts for all platforms
- GitHub Actions has access to required actions (Inno-Setup-Action, standard runners)
- No code signing is required for initial release (macOS notarization is out of scope for v1)
- The plugin name is "Iterum" and publisher is the repository owner
- Version numbers follow semantic versioning (v1.0.0 format)

### Existing Codebase Components (Principle XIV)

*GATE: Must identify before `/speckit.plan` to prevent ODR violations.*

**Relevant existing components that may be reused or extended:**

| Component | Location | Relevance |
|-----------|----------|-----------|
| CI Workflow | `.github/workflows/ci.yml` | Must be called before release workflow; produces artifacts |
| Build Artifacts | CI workflow `upload-artifact` steps | Must match names used in download step |
| Version Information | `CMakeLists.txt`, `version.h` | May need to extract version from tag |
| Plugin Metadata | `plugin_ids.h`, `CMakeLists.txt` | Publisher name, plugin ID for installers |

**Initial codebase search for key terms:**

```bash
# Run these searches to identify existing implementations
grep -r "release" .github/
grep -r "upload-artifact" .github/
grep -r "PLUGIN_VERSION" CMakeLists.txt
```

**Search Results Summary**: CI workflow exists and uploads artifacts for Windows, macOS, and Linux. Version is defined in CMakeLists.txt.

### Forward Reusability Consideration

*Note for planning phase: The Inno Setup script and release workflow patterns established here can be reused for other VST plugins in the organization.*

**Sibling features at same layer** (if known):
- Future plugins in the same product line would use identical installer infrastructure

**Potential shared components** (preliminary, refined in plan.md):
- Inno Setup script template could be parameterized for reuse
- Release workflow could be made into a reusable workflow for organization

## Implementation Verification *(mandatory at completion)*

### Compliance Status

*Fill this table when claiming completion. DO NOT claim completion if ANY requirement is NOT MET without explicit user approval.*

| Requirement | Status | Evidence |
|-------------|--------|----------|
| FR-001 | | |
| FR-002 | | |
| FR-003 | | |
| FR-004 | | |
| FR-005 | | |
| FR-006 | | |
| FR-010 | | |
| FR-011 | | |
| FR-012 | | |
| FR-013 | | |
| FR-014 | | |
| FR-015 | | |
| FR-016 | | |
| FR-020 | | |
| FR-021 | | |
| FR-022 | | |
| FR-023 | | |
| FR-030 | | |
| FR-031 | | |
| FR-032 | | |
| FR-040 | | |
| FR-041 | | |
| FR-042 | | |
| SC-001 | | |
| SC-002 | | |
| SC-003 | | |
| SC-004 | | |
| SC-005 | | |
| SC-006 | | |
| SC-007 | | |

**Status Key:**
- MET: Requirement fully satisfied with test evidence
- NOT MET: Requirement not satisfied (spec is NOT complete)
- PARTIAL: Partially met with documented gap
- DEFERRED: Explicitly moved to future work with user approval

### Completion Checklist

*All items must be checked before claiming completion:*

- [ ] All FR-xxx requirements verified against implementation
- [ ] All SC-xxx success criteria measured and documented
- [ ] No test thresholds relaxed from spec requirements
- [ ] No placeholder values or TODO comments in new code
- [ ] No features quietly removed from scope
- [ ] User would NOT feel cheated by this completion claim

### Honest Assessment

**Overall Status**: [COMPLETE / NOT COMPLETE / PARTIAL]

**If NOT COMPLETE, document gaps:**
- [Gap 1: FR-xxx not met because...]
- [Gap 2: SC-xxx achieves X instead of Y because...]

**Recommendation**: [What needs to happen to achieve completion]
