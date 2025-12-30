# Quickstart: Audio Unit (AUv2) Support

**Branch**: `039-auv2-support` | **Date**: 2025-12-30 | **Spec**: [spec.md](spec.md)

## Overview

This feature adds Audio Unit v2 support to Iterum using the VST3 SDK's built-in AU wrapper. No C++ code changes required - only build configuration and CI/CD modifications.

## Implementation Steps

### Step 1: Create AU Manifest (resources/au-info.plist)

Create the AU component metadata file with:
- Type code: `aufx` (audio effect)
- Manufacturer code: `KrAt` (Krate Audio)
- Subtype code: `Itrm` (Iterum)
- Stereo channel configuration (2 in → 2 out)

**Template**: See [research.md](research.md#au-manifest-au-infoplist) for complete plist structure.

### Step 2: Update CMakeLists.txt

#### 2a. Add AudioUnitSDK FetchContent

After existing FetchContent declarations:

```cmake
# ==============================================================================
# AudioUnit SDK (macOS only, for AUv2 wrapper)
# ==============================================================================
if(SMTG_MAC)
    option(SMTG_ENABLE_AUV2_BUILDS "Enable AudioUnit v2 builds" OFF)

    if(XCODE AND SMTG_ENABLE_AUV2_BUILDS)
        FetchContent_Declare(
            AudioUnitSDK
            GIT_REPOSITORY https://github.com/apple/AudioUnitSDK.git
            GIT_TAG AudioUnitSDK-1.1.0
        )
        FetchContent_MakeAvailable(AudioUnitSDK)
        FetchContent_GetProperties(
            AudioUnitSDK
            SOURCE_DIR SMTG_AUDIOUNIT_SDK_PATH
        )
    endif()
endif()
```

#### 2b. Add AU Target

After the VST3 plugin target configuration:

```cmake
# ==============================================================================
# Audio Unit v2 Target (macOS only)
# ==============================================================================
if(SMTG_MAC AND XCODE AND SMTG_ENABLE_AUV2_BUILDS)
    list(APPEND CMAKE_MODULE_PATH "${vst3sdk_SOURCE_DIR}/cmake/modules")
    include(SMTG_AddVST3AuV2)

    smtg_target_add_auv2(${PLUGIN_NAME}_AU
        BUNDLE_NAME ${PLUGIN_NAME}
        BUNDLE_IDENTIFIER com.krateaudio.iterum.audiounit
        INFO_PLIST_TEMPLATE ${CMAKE_CURRENT_SOURCE_DIR}/resources/au-info.plist
        VST3_PLUGIN_TARGET ${PLUGIN_NAME}
    )
endif()
```

### Step 3: Update CI Workflow (.github/workflows/ci.yml)

#### 3a. Enable AUv2 Builds

In the macOS Configure step, add the option:

```yaml
- name: Configure CMake
  run: cmake -S . -B build -G Xcode -DUSE_CCACHE=ON -DSMTG_ENABLE_AUV2_BUILDS=ON
```

#### 3b. Build AU Target

Update build step to include AU:

```yaml
- name: Build Plugin and Validator
  run: cmake --build build --config ${{ env.BUILD_TYPE }} --target Iterum Iterum_AU validator --parallel
```

#### 3c. Add AU Validation Step

After the VST3 validator step:

```yaml
- name: Run AU Validation
  run: |
    # Copy AU component to system location for validation
    cp -r build/VST3/${{ env.BUILD_TYPE }}/Iterum.component ~/Library/Audio/Plug-Ins/Components/
    # Force AU cache refresh
    killall -9 AudioComponentRegistrar 2>/dev/null || true
    # Run validation
    auval -v aufx Itrm KrAt
```

#### 3d. Update Artifact Upload

```yaml
- name: Upload Plugin Artifact
  uses: actions/upload-artifact@v4
  with:
    name: Iterum-macOS
    path: |
      build/VST3/${{ env.BUILD_TYPE }}/Iterum.vst3
      build/VST3/${{ env.BUILD_TYPE }}/Iterum.component
    if-no-files-found: warn
```

### Step 4: Update Release Workflow (.github/workflows/release.yml)

#### 4a. Update Installer Staging

In the macOS installer job:

```yaml
- name: Prepare Installer Files
  run: |
    mkdir -p staging/Library/Audio/Plug-Ins/VST3
    mkdir -p staging/Library/Audio/Plug-Ins/Components
    # VST3 bundle
    mv artifact/Iterum.vst3 staging/Library/Audio/Plug-Ins/VST3/
    # AU component
    mv artifact/Iterum.component staging/Library/Audio/Plug-Ins/Components/
```

#### 4b. Update Package Identifier

Change the identifier to reflect both formats:

```yaml
- name: Build Installer
  run: |
    pkgbuild \
      --root staging \
      --identifier com.krateaudio.iterum \
      --version ${{ needs.version.outputs.version }} \
      --install-location / \
      Iterum-${{ needs.version.outputs.version }}-macOS.pkg
```

## Verification

### Local Build (macOS with Xcode)

```bash
# Configure with AU support
cmake -S . -B build -G Xcode -DSMTG_ENABLE_AUV2_BUILDS=ON

# Build both targets
cmake --build build --config Release --target Iterum Iterum_AU

# Verify Universal Binary
file build/VST3/Release/Iterum.component/Contents/MacOS/Iterum
# Expected: Mach-O universal binary with 2 architectures

# Copy to system location
cp -r build/VST3/Release/Iterum.component ~/Library/Audio/Plug-Ins/Components/

# Run validation
auval -v aufx Itrm KrAt
# Expected: AU VALIDATION SUCCEEDED
```

### CI Verification

1. Push changes to branch
2. Verify macOS CI job:
   - Builds Iterum_AU target successfully
   - auval validation passes
   - Artifact includes both .vst3 and .component

### Release Verification

1. Trigger release workflow
2. Download macOS installer
3. Run installer
4. Verify installation:
   - `/Library/Audio/Plug-Ins/VST3/Iterum.vst3` exists
   - `/Library/Audio/Plug-Ins/Components/Iterum.component` exists
5. Test in Logic Pro or GarageBand

## Files Changed Summary

| File | Action | Description |
|------|--------|-------------|
| `resources/au-info.plist` | Create | AU manifest with codes KrAt/Itrm/aufx |
| `CMakeLists.txt` | Modify | Add AudioUnitSDK FetchContent, AU target |
| `.github/workflows/ci.yml` | Modify | Enable AU builds, add validation step |
| `.github/workflows/release.yml` | Modify | Add AU to installer staging |

## Troubleshooting

### auval Fails with "No AU Found"

1. Verify AU component is copied to `~/Library/Audio/Plug-Ins/Components/`
2. Kill AudioComponentRegistrar: `killall -9 AudioComponentRegistrar`
3. Check codes match plist: `auval -a | grep KrAt`

### Build Fails with AudioUnitSDK Errors

1. Verify Xcode is installed: `xcode-select -p`
2. Accept Xcode license: `sudo xcodebuild -license accept`
3. Verify using Xcode generator: `-G Xcode`

### Universal Binary Shows Single Architecture

1. Verify `SMTG_BUILD_UNIVERSAL_BINARY` is ON (default for Xcode 12+)
2. Build in Release mode (Debug uses active arch only)
3. Check with: `lipo -info Iterum.component/Contents/MacOS/Iterum`
