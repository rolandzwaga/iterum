# Implementation Plan: Audio Unit (AUv2) Support

**Branch**: `039-auv2-support` | **Date**: 2025-12-30 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/039-auv2-support/spec.md`

**Note**: This template is filled in by the `/speckit.plan` command. See `.specify/templates/commands/plan.md` for the execution workflow.

## Summary

Add Audio Unit v2 (AUv2) support to the Iterum VST3 plugin using the VST3 SDK's built-in AU wrapper. The AU component will be macOS-only, built as a Universal Binary (arm64 + x86_64), validated with `auval`, and integrated into the GitHub Actions release workflow to produce a combined VST3+AU installer.

## Technical Context

**Language/Version**: CMake 3.20+, Bash (GitHub Actions), Objective-C++ (AU wrapper)
**Primary Dependencies**: VST3 SDK AU wrapper (`SMTG_AddVST3AuV2.cmake`), Apple AudioUnitSDK (FetchContent)
**Storage**: N/A
**Testing**: `auval -v aufx Itrm KrAt` validation *(Constitution Principle XII: Test-First Development)*
**Target Platform**: macOS 10.13+ (High Sierra), Universal Binary (arm64 + x86_64)
**Project Type**: Build/CI feature (no new C++ source code)
**Performance Goals**: N/A (wrapper provides same performance as VST3)
**Constraints**: Must pass Apple's AU validation (`auval`), must work in Logic Pro/GarageBand
**Scale/Scope**: Single AU component wrapping existing VST3 plugin

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Required Check - Principle VI (Cross-Platform):**
- [x] AU support is macOS-only by design (spec FR-001: "for macOS")
- [x] VST3 remains available on all platforms (Windows, macOS, Linux)
- [x] No platform-specific code added to shared plugin source

**Required Check - Principle XII (Test-First Development):**
- [x] Tasks will include auval validation step
- [x] CI will run AU validation before release artifact creation
- [x] Each task group will end with a commit step
- Note: TESTING-GUIDE.md not applicable (no C++ code changes)

**Required Check - Principle XIV (ODR Prevention):**
- [x] Codebase Research section below is complete
- [x] No new C++ classes/functions created (build configuration only)
- [x] VST3 SDK's existing AU wrapper used unchanged

## Codebase Research (Principle XIV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

This section prevents One Definition Rule (ODR) violations by documenting existing components that may be reused or would conflict with new implementations.

### Mandatory Searches Performed

**Classes/Structs to be created**: None - this is a build/CI configuration feature

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| N/A | N/A | N/A | No C++ types created |

**Utility Functions to be created**: None

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| N/A | N/A | N/A | N/A | No C++ functions created |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| VST3 Plugin Target | CMakeLists.txt | Build | AU wrapper depends on this target |
| smtg_target_add_auv2 | extern/vst3sdk/cmake/modules/SMTG_AddVST3AuV2.cmake | VST3 SDK | CMake function to create AU target |
| AU Wrapper | extern/vst3sdk/public.sdk/source/vst/auwrapper/ | VST3 SDK | Bridges VST3 to AU interface |

### Files Checked for Conflicts

- [x] `CMakeLists.txt` - No existing AU configuration
- [x] `resources/` - No existing au-info.plist
- [x] `.github/workflows/ci.yml` - Uses Xcode generator, no AU target
- [x] `.github/workflows/release.yml` - macOS installer exists, needs AU addition

### ODR Risk Assessment

**Risk Level**: None

**Justification**: This feature adds no C++ code. It configures the build system to use existing VST3 SDK AU wrapper code and adds CI/CD steps. No ODR violations possible.

## Dependency API Contracts (Principle XIV Extension)

*GATE: Must complete BEFORE implementation begins. Prevents compile-time API mismatch errors.*

This section documents the **exact API signatures** of CMake functions and variables that will be used.

### CMake API Signatures to Call

| Dependency | Function/Variable | Signature (from VST3 SDK) | Verified? |
|------------|-------------------|---------------------------|-----------|
| SMTG_AddVST3AuV2 | smtg_target_add_auv2 | `smtg_target_add_auv2(<target> BUNDLE_NAME <name> BUNDLE_IDENTIFIER <id> INFO_PLIST_TEMPLATE <path> VST3_PLUGIN_TARGET <target>)` | ✓ |
| SMTG_AddVST3AuV2 | SMTG_ENABLE_AUV2_BUILDS | CMake option, default OFF | ✓ |
| AudioUnitSDK | SMTG_AUDIOUNIT_SDK_PATH | Set by FetchContent after MakeAvailable | ✓ |
| VST3 SDK | SMTG_MAC | Boolean, TRUE on macOS | ✓ |
| Xcode Generator | XCODE | Boolean, TRUE when using -G Xcode | ✓ |

### Files Read for Verification

- [x] `extern/vst3sdk/cmake/modules/SMTG_AddVST3AuV2.cmake` - AU target function
- [x] `extern/vst3sdk/tutorials/audiounit-tutorial/CMakeLists.txt` - Reference usage
- [x] `extern/vst3sdk/tutorials/audiounit-tutorial/resource/au-info.plist` - Plist template

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| smtg_target_add_auv2 | Requires `XCODE` generator check | `if(SMTG_MAC AND XCODE AND SMTG_ENABLE_AUV2_BUILDS)` |
| smtg_target_add_auv2 | Target name should differ from VST3 | Use `${PLUGIN_NAME}_AU` not `${PLUGIN_NAME}` |
| AudioUnitSDK | Must FetchContent before smtg_target_add_auv2 | FetchContent_MakeAvailable sets SMTG_AUDIOUNIT_SDK_PATH |
| au-info.plist | Type code for effects is `aufx` | Not `aumu` (instrument) or `aumf` (music effect) |

## Layer 0 Candidate Analysis

*For Layer 2+ features: Identify utility functions that should be extracted to Layer 0 for reuse.*

**N/A** - This is a build/CI configuration feature with no C++ code.

## Higher-Layer Reusability Analysis

*Forward-looking analysis: What code from THIS feature could be reused by SIBLING features at the same layer?*

### Sibling Features Analysis

**This feature's layer**: Build/CI Configuration

**Related features at same layer** (from ROADMAP.md or known plans):
- Future AUv3 support: Would use similar pattern with `smtg_target_add_auv3`
- Future AAX (Pro Tools) support: Would follow similar wrapper approach

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| au-info.plist template | LOW | Only AUv2 | Keep in resources/ |
| CI AU validation pattern | MEDIUM | AUv3, AAX | Document pattern in workflow comments |

### Decision Log

| Decision | Rationale |
|----------|-----------|
| No shared validation script | auval is AU-specific; AAX has avid's validator |
| Keep plist in resources/ | Standard location, single consumer |

## Project Structure

### Documentation (this feature)

```text
specs/039-auv2-support/
├── spec.md              # Feature specification
├── plan.md              # This file
├── research.md          # Phase 0 output
├── quickstart.md        # Phase 1 output (simplified - no data model)
└── tasks.md             # Phase 2 output (/speckit.tasks command)
```

### Files to Create/Modify (repository root)

```text
# New files
resources/
└── au-info.plist        # AU manifest (type, manufacturer, subtype codes)

# Modified files
CMakeLists.txt           # Add AUv2 target with smtg_target_add_auv2
.github/workflows/
├── ci.yml               # Add SMTG_ENABLE_AUV2_BUILDS=ON, AU validation
└── release.yml          # Add AU to macOS installer staging
```

**Structure Decision**: Build/CI configuration feature - no source code changes. New au-info.plist in resources/, modifications to CMakeLists.txt and workflow files.

## Complexity Tracking

> **Fill ONLY if Constitution Check has violations that must be justified**

No violations - all constitution checks pass.
